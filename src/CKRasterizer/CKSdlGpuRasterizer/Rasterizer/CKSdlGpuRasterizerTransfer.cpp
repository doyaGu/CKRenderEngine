#include "CKSdlGpuRasterizerContext.h"
#include "CKFFImage.h"

// CKSdlGpuRasterizerContext texture and backbuffer memory transfers.


#include <string.h>

namespace {

// The input snapshot also isolates cube faces and crops from the sampling
// footprint. The output target lets ordinary (non-RT) textures receive a
// scaled copy without changing their usage or discarding untouched pixels.
// Concrete contexts retain native resources referenced by queued commands after these
// temporary handles are released.
struct TextureCopyScratch {
    CKSdlGpuRasterizerContext *Context;
    CKDWORD Source = 0;
    CKDWORD Output = 0;
    CKDWORD Target = 0;

    explicit TextureCopyScratch(CKSdlGpuRasterizerContext *context) : Context(context) {}
    ~TextureCopyScratch()
    {
        if (Target) Context->DestroyObject(Target, CKRST_OBJ_RENDERTARGET);
        if (Output) Context->DestroyObject(Output, CKRST_OBJ_TEXTURE);
        if (Source) Context->DestroyObject(Source, CKRST_OBJ_TEXTURE);
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
        if (Context->CreateTexture(&desc, NULL, &Source) != CK_OK) return FALSE;
        desc.Format = outputFormat;
        desc.Format.Image = NULL;
        desc.Format.Width = outputWidth;
        desc.Format.Height = outputHeight;
        desc.Format.BytesPerLine = 0;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
        if (Context->CreateTexture(&desc, NULL, &Output) != CK_OK) return FALSE;
        CKRenderTargetDesc target;
        target.ColorTexture = Output;
        return Context->CreateRenderTarget(&target, &Target) == CK_OK;
    }
};

} // namespace

// ===========================================================================
// Texture copy
// ===========================================================================

CKBOOL CKSdlGpuRasterizerContext::CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const CKTextureDesc *texture = m_PublicResources.FindTexture(Texture);
    if (!texture) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (texture->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
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
    dstRect.right = texture->Format.Width;
    dstRect.bottom = texture->Format.Height;
    if (Dst) {
        dstRect.left = (int)Dst->left;
        dstRect.top = (int)Dst->top;
        dstRect.right = (int)Dst->right;
        dstRect.bottom = (int)Dst->bottom;
    }
    if (!ValidateRect(&dstRect, (CKDWORD)texture->Format.Width,
                      (CKDWORD)texture->Format.Height)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    if (m_FrameState.Open) {
        const CKDWORD source = m_TargetState.IsActive()
            ? m_TargetState.Texture : m_Present.NativeTarget().ColorTexture;
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
                copied = Blit(Texture, 0, Face, dstRect.left, dstRect.top,
                                         source, 0,
                                         m_TargetState.IsActive() ? m_TargetState.Face : 0,
                                         &srcRect) == CK_OK;
            } else {
                VxImageDescEx sourceFormat;
                if (m_TargetState.IsActive())
                    sourceFormat =
                        m_PublicResources.FindTexture(m_TargetState.Texture)->Format;
                else VxPixelFormat2ImageDesc(_32_ARGB8888, sourceFormat);
                TextureCopyScratch scratch(this);
                const CKRECT outputRect = {0, 0, dw, dh};
                copied = scratch.Create(sourceFormat, texture->Format, sw, sh, dw, dh) &&
                    Blit(scratch.Source, 0, 0, 0, 0, source, 0,
                                    m_TargetState.IsActive() ? m_TargetState.Face : 0,
                                    &srcRect) == CK_OK &&
                    OpenPass(scratch.Target, outputRect, 0, 0, 1, 0, "copy-scale") &&
                    m_Present.SubmitCopy(scratch.Source, sw, sh) == CK_OK &&
                    Blit(Texture, 0, Face, dstRect.left, dstRect.top,
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
    XArray<CKBYTE> pixels(size);
    pixels.Resize(size);
    image.Image = pixels.Begin();
    if (CopyToMemoryBuffer(&srcRect, VXBUFFER_BACKBUFFER, image) != size)
        return FALSE;

    const int dstWidth = dstRect.right - dstRect.left;
    const int dstHeight = dstRect.bottom - dstRect.top;
    if (!CKFFScaleImagePoint(image, pixels, dstWidth, dstHeight))
        return FALSE;
    return LoadTexture(Texture, image, 0, Face, &dstRect);
}

// ===========================================================================
// Backbuffer upload
// ===========================================================================

int CKSdlGpuRasterizerContext::CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image)
{
    if (!CheckDeviceForDraw())
        return 0;
    if (Buffer != VXBUFFER_BACKBUFFER || !Image.Image || Image.BitsPerPixel <= 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    if (!PrepareFrameTarget()) return FALSE;
    const CKRECT target = LogicalTargetRect();
    CKFFMemoryBufferCopy copy;
    if (!copy.Prepare(Rect, target, Image)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    const VxImageDescEx &videoFormat = copy.GetTextureFormat();
    const VxImageDescEx &uploadDesc = copy.GetUploadImage();
    const int width = videoFormat.Width;
    const int height = videoFormat.Height;

    CKERROR err = CK_OK;
    if (m_CopyWidth != (CKDWORD)width || m_CopyHeight != (CKDWORD)height ||
        !IsNativeObjectAlive(m_CopyTexture, CKRST_OBJ_TEXTURE)) {
        if (m_CopyTexture != 0) {
            DestroyObject(m_CopyTexture, CKRST_OBJ_TEXTURE);
            m_CopyTexture = 0;
        }
        CKTextureDesc texDesc;
        texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
        texDesc.Format = videoFormat;
        texDesc.MipMapCount = 1;
        err = CreateTexture(&texDesc, &uploadDesc, &m_CopyTexture);
        if (err == CK_OK) {
            m_CopyWidth = (CKDWORD)width;
            m_CopyHeight = (CKDWORD)height;
        }
    } else {
        err = UpdateTexture(m_CopyTexture, 0, 0, NULL, &uploadDesc);
    }
    if (err != CK_OK)
        return 0;
    ++m_FrameTextureUploads;

    if (!EnsureDrawPass())
        return 0;

    CKFFStateGuard guard(m_FFP);
    copy.PrepareDraw(m_FFP, m_CopyTexture);
    CKBOOL drawn = m_FFP.PreparePrimitive(
        VX_TRIANGLEFAN, NULL, 4, copy.GetDrawData());
    if (drawn)
        drawn = SubmitPreparedDraw();
    guard.Restore();
    if (!drawn) {
        Diag(DrawRejectDiagnostic());
        return 0;
    }
    RecordDrawApproximations();
    CountDraw(VX_TRIANGLEFAN, 4);
    return copy.GetImageSize();
}
