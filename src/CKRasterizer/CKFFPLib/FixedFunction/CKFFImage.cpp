#include "CKFFImage.h"
#include "CKFixedFunctionPipeline.h"

#include <string.h>

CKBOOL CKFFSameImageFormat(const VxImageDescEx &First,
                           const VxImageDescEx &Second)
{
    return First.BitsPerPixel == Second.BitsPerPixel &&
           First.RedMask == Second.RedMask &&
           First.GreenMask == Second.GreenMask &&
           First.BlueMask == Second.BlueMask &&
           First.AlphaMask == Second.AlphaMask;
}

CKBOOL CKFFValidateRect(const CKRECT *Rect, CKDWORD Width, CKDWORD Height)
{
    if (!Rect)
        return TRUE;
    return Rect->left >= 0 && Rect->top >= 0 &&
           Rect->right > Rect->left && Rect->bottom > Rect->top &&
           (CKDWORD)Rect->right <= Width && (CKDWORD)Rect->bottom <= Height;
}

CKBOOL CKFFBuildReadbackImage(CKBOOL Success, CKDWORD Width, CKDWORD Height,
                              CKDWORD Pitch, VX_PIXELFORMAT Format,
                              CKBOOL YFlip, const CKRECT *Rect,
                              const XArray<CKBYTE> &Data,
                              VxImageDescEx &Desc,
                              XArray<CKBYTE> &Pixels)
{
    if (!Success || Data.IsEmpty() || Width == 0 || Height == 0)
        return FALSE;

    VxImageDescEx source;
    VxPixelFormat2ImageDesc(Format, source);
    if (source.BitsPerPixel <= 0 || (source.BitsPerPixel % 8) != 0)
        return FALSE;
    const CKDWORD sourceBpp = (CKDWORD)source.BitsPerPixel / 8;
    if (Pitch < (CKQWORD)Width * sourceBpp)
        return FALSE;
    if ((CKQWORD)(Height - 1) * Pitch + (CKQWORD)Width * sourceBpp >
        (CKQWORD)Data.Size())
        return FALSE;

    int left = 0;
    int top = 0;
    int right = (int)Width;
    int bottom = (int)Height;
    if (Rect) {
        left = Rect->left > 0 ? Rect->left : 0;
        top = Rect->top > 0 ? Rect->top : 0;
        right = Rect->right < (int)Width ? Rect->right : (int)Width;
        bottom = Rect->bottom < (int)Height ? Rect->bottom : (int)Height;
    }
    if (right <= left || bottom <= top)
        return FALSE;

    const int width = right - left;
    const int height = bottom - top;
    const CKQWORD pixelCount = (CKQWORD)width * height;
    const CKQWORD croppedSize64 = pixelCount * sourceBpp;
    const CKQWORD outputSize64 = pixelCount * 4;
    if (croppedSize64 > 0x7fffffffu || outputSize64 > 0x7fffffffu)
        return FALSE;
    const int croppedSize = (int)croppedSize64;
    const int outputSize = (int)outputSize64;
    XArray<CKBYTE> cropped(croppedSize);
    cropped.Resize(croppedSize);
    for (int row = 0; row < height; ++row) {
        const CKDWORD logicalRow = (CKDWORD)(top + row);
        const CKDWORD sourceRow = YFlip ? Height - 1 - logicalRow : logicalRow;
        memcpy(cropped.Begin() + row * width * sourceBpp,
               Data.Begin() + sourceRow * Pitch + left * sourceBpp,
               width * sourceBpp);
    }

    source.Width = width;
    source.Height = height;
    source.BytesPerLine = width * (int)sourceBpp;
    source.Image = cropped.Begin();

    VxPixelFormat2ImageDesc(_32_ARGB8888, Desc);
    Desc.Width = width;
    Desc.Height = height;
    Desc.BytesPerLine = width * 4;
    Pixels.Resize(outputSize);
    Desc.Image = Pixels.Begin();
    if (CKFFSameImageFormat(source, Desc))
        memcpy(Pixels.Begin(), cropped.Begin(), Pixels.Size());
    else
        VxDoBlit(source, Desc);
    return TRUE;
}

CKBOOL CKFFScaleImagePoint(VxImageDescEx &Image, XArray<CKBYTE> &Pixels,
                           int Width, int Height)
{
    if (Image.Width <= 0 || Image.Height <= 0 || Image.BitsPerPixel != 32 ||
        Image.BytesPerLine <= 0 || (CKQWORD)Image.BytesPerLine < (CKQWORD)Image.Width * 4 || Width <= 0 || Height <= 0 ||
        (CKQWORD)Width * Height * 4 > 0x7fffffffu)
        return FALSE;
    const CKQWORD requiredSourceBytes =
        (CKQWORD)(Image.Height - 1) * Image.BytesPerLine + (CKQWORD)Image.Width * 4;
    if (requiredSourceBytes > (CKQWORD)Pixels.Size())
        return FALSE;
    if (Width == Image.Width && Height == Image.Height) {
        Image.Image = Pixels.Begin();
        return TRUE;
    }

    XArray<CKBYTE> scaled(Width * Height * 4);
    scaled.Resize(Width * Height * 4);
    for (int y = 0; y < Height; ++y) {
        const int sourceY = (int)(((CKQWORD)y * 2 + 1) * Image.Height / ((CKQWORD)Height * 2));
        const CKDWORD *sourceRow = (const CKDWORD *)(Pixels.Begin() + sourceY * Image.BytesPerLine);
        CKDWORD *destinationRow = (CKDWORD *)(scaled.Begin() + y * Width * 4);
        for (int x = 0; x < Width; ++x)
            destinationRow[x] = sourceRow[(int)(((CKQWORD)x * 2 + 1) * Image.Width / ((CKQWORD)Width * 2))];
    }
    Pixels.Swap(scaled);
    Image.Width = Width;
    Image.Height = Height;
    Image.BytesPerLine = Width * 4;
    Image.Image = Pixels.Begin();
    return TRUE;
}

CKFFMemoryBufferCopy::CKFFMemoryBufferCopy()
    : m_TargetRect{0, 0, 0, 0}, m_Rect{0, 0, 0, 0}, m_ImageSize(0)
{
    memset(&m_DrawData, 0, sizeof(m_DrawData));
}

CKBOOL CKFFMemoryBufferCopy::Prepare(
    const CKRECT *RequestedRect, const CKRECT &TargetRect,
    const VxImageDescEx &Source)
{
    m_ImageSize = 0;
    m_Converted.Clear();
    if (!Source.Image || Source.BitsPerPixel <= 0)
        return FALSE;

    m_TargetRect = TargetRect;
    m_Rect = RequestedRect ? *RequestedRect : TargetRect;
    if (m_Rect.left < 0) m_Rect.left = 0;
    if (m_Rect.top < 0) m_Rect.top = 0;
    if (m_Rect.right > TargetRect.right) m_Rect.right = TargetRect.right;
    if (m_Rect.bottom > TargetRect.bottom) m_Rect.bottom = TargetRect.bottom;
    const int width = m_Rect.right - m_Rect.left;
    const int height = m_Rect.bottom - m_Rect.top;
    if (width <= 0 || height <= 0 || Source.Width != width ||
        Source.Height != height ||
        (CKQWORD)width * (CKQWORD)height * 4 > 0x7fffffffu)
        return FALSE;

    VxPixelFormat2ImageDesc(_32_ARGB8888, m_TextureFormat);
    m_TextureFormat.Width = width;
    m_TextureFormat.Height = height;
    m_TextureFormat.BytesPerLine = width * 4;
    m_ImageSize = m_TextureFormat.BytesPerLine * height;

    m_UploadImage = Source;
    if (!CKFFSameImageFormat(Source, m_TextureFormat)) {
        m_Converted.Resize(m_ImageSize);
        m_UploadImage = m_TextureFormat;
        m_UploadImage.Image = m_Converted.Begin();
        VxDoBlit(Source, m_UploadImage);
    } else if (m_UploadImage.BytesPerLine <= 0) {
        m_UploadImage.BytesPerLine =
            width * m_UploadImage.BitsPerPixel / 8;
    }
    return TRUE;
}

void CKFFMemoryBufferCopy::PrepareDraw(
    CKFixedFunctionPipeline &Pipeline, CKDWORD Texture)
{
    Pipeline.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    Pipeline.SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    Pipeline.SetColorWriteMask(TRUE, TRUE, TRUE, TRUE);
    Pipeline.SetRenderState(VXRENDERSTATE_STENCILENABLE, FALSE);
    Pipeline.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    Pipeline.SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
    Pipeline.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    Pipeline.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
    Pipeline.SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_ALWAYS);
    Pipeline.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    Pipeline.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    Pipeline.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
    Pipeline.DisableTextureStagesFrom(0);
    Pipeline.SetTexture(0, Texture,
        CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA);
    Pipeline.SetTextureStageState(0, CKRST_TSS_OP,
                                  CKRST_TOP_SELECTARG1);
    Pipeline.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    Pipeline.SetTextureStageState(0, CKRST_TSS_AOP,
                                  CKRST_TOP_SELECTARG1);
    Pipeline.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    Pipeline.SetTextureStageState(0, CKRST_TSS_MAGFILTER,
                                  VXTEXTUREFILTER_NEAREST);
    Pipeline.SetTextureStageState(0, CKRST_TSS_MINFILTER,
                                  VXTEXTUREFILTER_NEAREST);
    Pipeline.SetTextureStageState(0, CKRST_TSS_ADDRESS,
                                  VXTEXTURE_ADDRESSCLAMP);

    CKViewportData viewport;
    viewport.ViewX = 0;
    viewport.ViewY = 0;
    viewport.ViewWidth = (CKDWORD)m_TargetRect.right;
    viewport.ViewHeight = (CKDWORD)m_TargetRect.bottom;
    viewport.ViewZMin = 0.0f;
    viewport.ViewZMax = 1.0f;
    Pipeline.SetViewport(viewport);

    const float x0 = (float)m_Rect.left - 0.5f;
    const float y0 = (float)m_Rect.top - 0.5f;
    const float x1 = (float)m_Rect.right - 0.5f;
    const float y1 = (float)m_Rect.bottom - 0.5f;
    const float coords[8] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; ++i) {
        m_Positions[i][0] = xs[i];
        m_Positions[i][1] = ys[i];
        m_Positions[i][2] = 0.0f;
        m_Positions[i][3] = 1.0f;
        m_Colors[i] = 0xFFFFFFFF;
        m_Uvs[i][0] = coords[i * 2];
        m_Uvs[i][1] = coords[i * 2 + 1];
    }

    memset(&m_DrawData, 0, sizeof(m_DrawData));
    m_DrawData.VertexCount = 4;
    m_DrawData.Flags = CKRST_DP_CL_VCT;
    m_DrawData.PositionPtr = m_Positions;
    m_DrawData.PositionStride = sizeof(m_Positions[0]);
    m_DrawData.ColorPtr = m_Colors;
    m_DrawData.ColorStride = sizeof(m_Colors[0]);
    m_DrawData.TexCoordPtr = m_Uvs;
    m_DrawData.TexCoordStride = sizeof(m_Uvs[0]);
}
