#ifndef CKVERTEXLAYOUTCACHE_H
#define CKVERTEXLAYOUTCACHE_H

#include "VxDefines.h"
#include "CKError.h"
#include "CKRasterizerContextEnums.h"
#include "CKRasterizerContextTypes.h"
#include "CKFFVertexFormat.h"

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
