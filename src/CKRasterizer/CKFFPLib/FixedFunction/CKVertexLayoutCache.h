#ifndef CKVERTEXLAYOUTCACHE_H
#define CKVERTEXLAYOUTCACHE_H

#include "VxDefines.h"
#include "CKError.h"
#include "CKRasterizerContextEnums.h"
#include "CKRasterizerContextTypes.h"

// Vertex layout flags (used as cache key)
#define CKFF_VF_POSITION   0x0001
#define CKFF_VF_NORMAL     0x0002
#define CKFF_VF_COLOR0     0x0004
#define CKFF_VF_COLOR1     0x0008
#define CKFF_VF_TEXCOORD0  0x0010
#define CKFF_VF_TEXCOORD1  0x0020
#define CKFF_VF_TEXCOORD2  0x0040
#define CKFF_VF_TEXCOORD3  0x0080
#define CKFF_VF_TEXCOORD4  0x0100
#define CKFF_VF_TEXCOORD5  0x0200
#define CKFF_VF_TEXCOORD6  0x0400
#define CKFF_VF_TEXCOORD7  0x0800
#define CKFF_VF_POSITIONT  0x1000
#define CKFF_VF_BLENDWEIGHT 0x2000
#define CKFF_VF_BLENDINDEX  0x4000
#define CKFF_VF_TWEENPOSITION 0x8000
#define CKFF_VF_TWEENNORMAL   0x10000
#define CKFF_VF_POINTOFFSET   0x20000
#define CKFF_VF_POINTOFFSET_WEIGHT 0x40000
#define CKFF_VF_LINEPATTERN   0x80000
#define CKFF_VF_LINEPATTERN_WEIGHT 0x100000

#define CKFF_VF_TEXCOORD(stage) (CKFF_VF_TEXCOORD0 << (stage))

class CKFFVertexLayout {
public:
    // Pure CPU rules shared by concrete contexts. Native layout caching and
    // object lifetime belong to each concrete rasterizer context.
    static CKDWORD DrawPrimitiveDataToFormatFlags(const VxDrawPrimitiveData *data);
    static CKDWORD DPFlagsToFormatFlags(CKDWORD dpFlags, bool hasNormal, bool hasUV);
    static CKDWORD DPFlagsToFormatFlags(CKDWORD dpFlags, bool hasNormal, bool hasUV, CKDWORD positionStride);
    static CKDWORD DPFlagsToBlendWeightCount(CKDWORD dpFlags);
    static CKDWORD DPFlagsToBlendIndexOffset(CKDWORD dpFlags);
    static CKDWORD DPFlagsToBlendRecordSize(CKDWORD dpFlags);

    // Get the stride for a given format flags combination
    static CKDWORD ComputeStride(CKDWORD formatFlags);
    static CKBOOL BuildLayout(CKDWORD formatFlags,
                              CKVertexElementDesc *elements,
                              CKDWORD capacity,
                              CKVertexLayoutDesc &desc);
};

#endif // CKVERTEXLAYOUTCACHE_H
