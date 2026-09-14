#ifndef CKFFIMAGE_H
#define CKFFIMAGE_H

#include "CKRasterizer.h"

class CKFixedFunctionPipeline;

CKBOOL CKFFSameImageFormat(const VxImageDescEx &First,
                           const VxImageDescEx &Second);
CKBOOL CKFFValidateRect(const CKRECT *Rect, CKDWORD Width, CKDWORD Height);
CKBOOL CKFFBuildReadbackImage(CKBOOL Success, CKDWORD Width, CKDWORD Height,
                              CKDWORD Pitch, VX_PIXELFORMAT Format,
                              CKBOOL YFlip, const CKRECT *Rect,
                              const XArray<CKBYTE> &Data,
                              VxImageDescEx &Desc,
                              XArray<CKBYTE> &Pixels);
CKBOOL CKFFScaleImagePoint(VxImageDescEx &Image, XArray<CKBYTE> &Pixels,
                           int Width, int Height);

// CPU preparation shared by CopyFromMemoryBuffer implementations. Native
// texture realization and command submission remain in each concrete context.
class CKFFMemoryBufferCopy {
public:
    CKFFMemoryBufferCopy();

    CKBOOL Prepare(const CKRECT *RequestedRect, const CKRECT &TargetRect,
                   const VxImageDescEx &Source);
    void PrepareDraw(CKFixedFunctionPipeline &Pipeline, CKDWORD Texture);

    const VxImageDescEx &GetTextureFormat() const { return m_TextureFormat; }
    const VxImageDescEx &GetUploadImage() const { return m_UploadImage; }
    VxDrawPrimitiveData *GetDrawData() { return &m_DrawData; }
    int GetImageSize() const { return m_ImageSize; }

private:
    CKFFMemoryBufferCopy(const CKFFMemoryBufferCopy &) = delete;
    CKFFMemoryBufferCopy &operator=(const CKFFMemoryBufferCopy &) = delete;

    CKRECT m_TargetRect;
    CKRECT m_Rect;
    VxImageDescEx m_TextureFormat;
    VxImageDescEx m_UploadImage;
    XArray<CKBYTE> m_Converted;
    float m_Positions[4][4];
    CKDWORD m_Colors[4];
    float m_Uvs[4][2];
    VxDrawPrimitiveData m_DrawData;
    int m_ImageSize;
};

#endif
