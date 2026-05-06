#ifndef CKVERTEXLAYOUTCACHE_H
#define CKVERTEXLAYOUTCACHE_H

#include "VxDefines.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"

#include <unordered_map>

class CKRasterizerContext;

// Vertex layout flags (used as cache key)
#define CKFF_VF_POSITION  0x01
#define CKFF_VF_NORMAL    0x02
#define CKFF_VF_COLOR0    0x04
#define CKFF_VF_COLOR1    0x08
#define CKFF_VF_TEXCOORD0 0x10
#define CKFF_VF_TEXCOORD1 0x20
#define CKFF_VF_TEXCOORD2 0x40
#define CKFF_VF_POSITIONT 0x80

class CKVertexLayoutCache {
public:
    CKVertexLayoutCache();
    ~CKVertexLayoutCache();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();

    // Get or create a layout handle for the given vertex format flags.
    // Returns the layout handle and outputs the stride.
    CKDWORD GetLayout(CKDWORD formatFlags, CKDWORD *outStride = nullptr);

    // Compute format flags from VxDrawPrimitiveData CKRST_DP_* flags
    static CKDWORD DPFlagsToFormatFlags(CKDWORD dpFlags, bool hasNormal, bool hasUV);

    // Get the stride for a given format flags combination
    static CKDWORD ComputeStride(CKDWORD formatFlags);

private:
    CKRasterizerContext *m_Context;
    std::unordered_map<CKDWORD, CKDWORD> m_Cache; // formatFlags → layout handle
    CKDWORD m_NextHandle;
};

#endif // CKVERTEXLAYOUTCACHE_H
