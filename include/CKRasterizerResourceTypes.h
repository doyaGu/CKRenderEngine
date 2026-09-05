#ifndef CKRASTERIZERRESOURCETYPES_H
#define CKRASTERIZERRESOURCETYPES_H

// Resource descriptors shared by the public rasterizer and native backends.
// Fixed-function vertex formats, lights, materials and options belong to
// CKRasterizerTypes.h and are not part of this resource interface.

#include "CKTypes.h"
#include "VxImageDescEx.h"

struct CKTextureDesc {
    CKDWORD Flags;         // CKRST_TEXTUREFLAGS
    VxImageDescEx Format;  // Width, Height, pixel format of level 0
    CKDWORD MipMapCount;   // 0 / 1 = none, N = engine-provided levels, CKRST_MIPMAP_GENERATE
    CKDWORD Depth;         // Volume depth (1 for 2D and cube)

    CKTextureDesc() : Flags(0), MipMapCount(0), Depth(1) {}
};

#endif // CKRASTERIZERRESOURCETYPES_H
