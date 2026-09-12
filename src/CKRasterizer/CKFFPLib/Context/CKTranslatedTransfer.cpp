// CKTranslatedContext texture and backbuffer memory transfers.

#include "CKFFRasterizerContextInternal.h"

#include <string.h>

namespace {

CKBOOL SameImageFormat(const VxImageDescEx &a, const VxImageDescEx &b)
{
    return a.BitsPerPixel == b.BitsPerPixel && a.RedMask == b.RedMask && a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask && a.AlphaMask == b.AlphaMask;
}

// The input snapshot also isolates cube faces and crops from the sampling
// footprint. The output target lets ordinary (non-RT) textures receive a
// scaled copy without changing their usage or discarding untouched pixels.
// Backends retain native resources referenced by queued commands after these
// temporary handles are released.
struct TextureCopyScratch {
    CKRasterizerBackend *Backend;
    CKDWORD Source = 0;
    CKDWORD Output = 0;
    CKDWORD Target = 0;

    explicit TextureCopyScratch(CKRasterizerBackend *backend) : Backend(backend) {}
    ~TextureCopyScratch()
    {
        if (Target) Backend->DestroyObject(Target, CKRST_OBJ_RENDERTARGET);
        if (Output) Backend->DestroyObject(Output, CKRST_OBJ_TEXTURE);
        if (Source) Backend->DestroyObject(Source, CKRST_OBJ_TEXTURE);
    }
    TextureCopyScratch(const TextureCopyScratch &) = delete;
    TextureCopyScratch &operator=(const TextureCopyScratch &) = delete;

    CKBOOL Create(const VxImageDescEx &sourceFormat, const VxImageDescEx &outputFormat,
                  int sourceWidth, int sourceHeight, int outputWidth, int outputHeight)
    {
        CKTextureDesc desc;
        desc.Format = sourceFormat;
        desc.Format.Image = NULL;
        desc.Format.Width = sourceWidth;
        desc.Format.Height = sourceHeight;
        desc.Format.BytesPerLine = 0;
        desc.Depth = desc.MipMapCount = 1;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_BLIT_DST;
        if (Backend->CreateTexture(&desc, NULL, &Source) != CK_OK) return FALSE;
        desc.Format = outputFormat;
        desc.Format.Image = NULL;
        desc.Format.Width = outputWidth;
        desc.Format.Height = outputHeight;
        desc.Format.BytesPerLine = 0;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
        if (Backend->CreateTexture(&desc, NULL, &Output) != CK_OK) return FALSE;
        CKBackendRenderTargetDesc target;
        target.ColorTexture = Output;
        return Backend->CreateRenderTarget(&target, &Target) == CK_OK;
    }
};

} // namespace

// ===========================================================================
// Texture copy
// ===========================================================================

CKBOOL CKTranslatedContext::CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    if ((CKDWORD)Face >= CKRST_CUBEFACE_COUNT || (!cube && Face != CKRST_CUBEFACE_XPOS)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    const CKRECT target = LogicalTargetRect();
    CKRECT srcRect = target;
    if (Src) {
        srcRect.left = (int)Src->left;
        srcRect.top = (int)Src->top;
        srcRect.right = (int)Src->right;
        srcRect.bottom = (int)Src->bottom;
    }
    if (!ValidateRect(&srcRect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKRECT dstRect;
    dstRect.left = 0;
    dstRect.top = 0;
    dstRect.right = resource->Texture.Format.Width;
    dstRect.bottom = resource->Texture.Format.Height;
    if (Dst) {
        dstRect.left = (int)Dst->left;
        dstRect.top = (int)Dst->top;
        dstRect.right = (int)Dst->right;
        dstRect.bottom = (int)Dst->bottom;
    }
    if (!ValidateRect(&dstRect, (CKDWORD)resource->Texture.Format.Width, (CKDWORD)resource->Texture.Format.Height)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    if (m_Frame.Open) {
        const CKDWORD source = m_Target ? m_Target : m_Present.NativeTarget().ColorTexture;
        if (!source || source == Texture) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        const CKDWORD resumeTarget = CurrentPassFrameBuffer();
        const CKRECT resumeRect = CurrentPassRect();
        CKBOOL copied = FALSE;
        if (ResolveCopySource()) {
            const int sw = srcRect.right - srcRect.left, sh = srcRect.bottom - srcRect.top;
            const int dw = dstRect.right - dstRect.left, dh = dstRect.bottom - dstRect.top;
            if (sw == dw && sh == dh) {
                copied = m_Backend->Blit(Texture, 0, Face, dstRect.left, dstRect.top,
                                         source, 0, m_Target ? m_TargetFace : 0, &srcRect) == CK_OK;
            } else {
                VxImageDescEx sourceFormat;
                if (m_Target) sourceFormat = FindResource(CKRST_OBJ_TEXTURE, m_Target)->Texture.Format;
                else VxPixelFormat2ImageDesc(_32_ARGB8888, sourceFormat);
                TextureCopyScratch scratch(m_Backend);
                const CKRECT outputRect = {0, 0, dw, dh};
                copied = scratch.Create(sourceFormat, resource->Texture.Format, sw, sh, dw, dh) &&
                    m_Backend->Blit(scratch.Source, 0, 0, 0, 0, source, 0,
                                    m_Target ? m_TargetFace : 0, &srcRect) == CK_OK &&
                    OpenPass(scratch.Target, outputRect, 0, 0, 1, 0, "copy-scale") &&
                    m_Present.SubmitCopy(scratch.Source, sw, sh) == CK_OK &&
                    m_Backend->Blit(Texture, 0, Face, dstRect.left, dstRect.top,
                                    scratch.Output, 0, 0, &outputRect) == CK_OK;
            }
        }
        const CKBOOL resumed = OpenPass(resumeTarget, resumeRect, 0, 0, 1, 0, "copy-resume");
        return copied && resumed;
    }

    VxImageDescEx image;
    const int size = CopyToMemoryBuffer(&srcRect, VXBUFFER_BACKBUFFER, image);
    if (size <= 0)
        return FALSE;
    std::vector<CKBYTE> pixels((size_t)size);
    image.Image = pixels.data();
    if (CopyToMemoryBuffer(&srcRect, VXBUFFER_BACKBUFFER, image) != size)
        return FALSE;

    const int dstWidth = dstRect.right - dstRect.left;
    const int dstHeight = dstRect.bottom - dstRect.top;
    if (dstWidth != image.Width || dstHeight != image.Height) {
        // Match the GPU point sampler: map destination pixel centers to
        // source texels, for both enlarging and shrinking rectangles.
        std::vector<CKBYTE> scaled((size_t)dstWidth * dstHeight * 4);
        for (int y = 0; y < dstHeight; ++y) {
            const int sy = (int)(((int64_t)y * 2 + 1) * image.Height / ((int64_t)dstHeight * 2));
            const CKDWORD *srcRow = (const CKDWORD *)(pixels.data() + (size_t)sy * image.BytesPerLine);
            CKDWORD *dstRow = (CKDWORD *)(scaled.data() + (size_t)y * dstWidth * 4);
            for (int x = 0; x < dstWidth; ++x)
                dstRow[x] = srcRow[(int)(((int64_t)x * 2 + 1) * image.Width / ((int64_t)dstWidth * 2))];
        }
        pixels.swap(scaled);
        image.Width = dstWidth;
        image.Height = dstHeight;
        image.BytesPerLine = dstWidth * 4;
        image.Image = pixels.data();
    }
    return LoadTexture(Texture, image, 0, Face, &dstRect);
}

// ===========================================================================
// Backbuffer upload
// ===========================================================================

int CKTranslatedContext::CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image)
{
    if (!CheckDeviceForDraw())
        return 0;
    if (Buffer != VXBUFFER_BACKBUFFER || !Image.Image || Image.BitsPerPixel <= 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    if (!PrepareFrameTarget()) return FALSE;
    const CKRECT target = LogicalTargetRect();
    CKRECT rect = Rect ? *Rect : target;
    if (rect.left < 0) rect.left = 0;
    if (rect.top < 0) rect.top = 0;
    if (rect.right > target.right) rect.right = target.right;
    if (rect.bottom > target.bottom) rect.bottom = target.bottom;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0 || Image.Width != width || Image.Height != height) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }

    VxImageDescEx videoFormat;
    VxPixelFormat2ImageDesc(_32_ARGB8888, videoFormat);
    videoFormat.Width = width;
    videoFormat.Height = height;
    videoFormat.BytesPerLine = width * 4;
    const int videoImageSize = videoFormat.BytesPerLine * height;

    VxImageDescEx uploadDesc = Image;
    std::vector<CKBYTE> converted;
    if (!SameImageFormat(Image, videoFormat)) {
        converted.resize((size_t)videoImageSize);
        uploadDesc = videoFormat;
        uploadDesc.Image = converted.data();
        VxDoBlit(Image, uploadDesc);
    } else if (uploadDesc.BytesPerLine <= 0) {
        uploadDesc.BytesPerLine = width * uploadDesc.BitsPerPixel / 8;
    }

    CKERROR err = CK_OK;
    if (m_CopyWidth != (CKDWORD)width || m_CopyHeight != (CKDWORD)height ||
        !m_Backend->IsObjectAlive(m_CopyTexture, CKRST_OBJ_TEXTURE)) {
        if (m_CopyTexture != 0) {
            m_Backend->DestroyObject(m_CopyTexture, CKRST_OBJ_TEXTURE);
            m_CopyTexture = 0;
        }
        CKTextureDesc texDesc;
        texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
        texDesc.Format = videoFormat;
        texDesc.MipMapCount = 1;
        err = m_Backend->CreateTexture(&texDesc, &uploadDesc, &m_CopyTexture);
        if (err == CK_OK) {
            m_CopyWidth = (CKDWORD)width;
            m_CopyHeight = (CKDWORD)height;
        }
    } else {
        err = m_Backend->UpdateTexture(m_CopyTexture, 0, 0, NULL, &uploadDesc);
    }
    if (err != CK_OK)
        return 0;
    ++m_FrameTextureUploads;

    if (!EnsureDrawPass())
        return 0;

    CKFFStateGuard guard(m_FFP);
    m_FFP.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    m_FFP.SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    m_FFP.SetColorWriteMask(TRUE, TRUE, TRUE, TRUE);
    m_FFP.SetRenderState(VXRENDERSTATE_STENCILENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_ALWAYS);
    m_FFP.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
    m_FFP.DisableTextureStagesFrom(0);
    m_FFP.SetTexture(0, m_CopyTexture, CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA);
    m_FFP.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    m_FFP.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    m_FFP.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    m_FFP.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    m_FFP.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    m_FFP.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    m_FFP.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);

    CKViewportData fullViewport;
    fullViewport.ViewX = 0;
    fullViewport.ViewY = 0;
    fullViewport.ViewWidth = (CKDWORD)target.right;
    fullViewport.ViewHeight = (CKDWORD)target.bottom;
    fullViewport.ViewZMin = 0.0f;
    fullViewport.ViewZMax = 1.0f;
    m_FFP.SetViewport(fullViewport);

    float positions[4][4];
    CKDWORD colors[4];
    float uvs[4][2];
    // POSITIONT follows the legacy integer pixel-center convention. Put quad
    // edges half a pixel before those centers, so point sampling lands in the
    // middle of each source texel rather than on unstable texel boundaries.
    const float x0 = (float)rect.left - 0.5f, y0 = (float)rect.top - 0.5f;
    const float x1 = (float)rect.right - 0.5f, y1 = (float)rect.bottom - 0.5f;
    const float coords[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; ++i) {
        positions[i][0] = xs[i];
        positions[i][1] = ys[i];
        positions[i][2] = 0.0f;
        positions[i][3] = 1.0f;
        colors[i] = 0xFFFFFFFF;
        uvs[i][0] = coords[i * 2];
        uvs[i][1] = coords[i * 2 + 1];
    }
    VxDrawPrimitiveData dp;
    memset(&dp, 0, sizeof(dp));
    dp.VertexCount = 4;
    dp.Flags = CKRST_DP_CL_VCT;
    dp.PositionPtr = positions;
    dp.PositionStride = sizeof(positions[0]);
    dp.ColorPtr = colors;
    dp.ColorStride = sizeof(colors[0]);
    dp.TexCoordPtr = uvs;
    dp.TexCoordStride = sizeof(uvs[0]);

    const CKBOOL drawn = m_FFP.DrawPrimitive(VX_TRIANGLEFAN, NULL, 4, &dp);
    guard.Restore();
    if (!drawn) {
        Diag(DrawRejectDiagnostic());
        return 0;
    }
    RecordDrawApproximations();
    CountDraw(VX_TRIANGLEFAN, 4);
    return videoImageSize;
}
