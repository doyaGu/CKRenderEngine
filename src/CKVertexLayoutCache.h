#ifndef CKVERTEXLAYOUTCACHE_H
#define CKVERTEXLAYOUTCACHE_H

#include "VxDefines.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "XHashTable.h"

class CKRasterizerContext;

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

#define CKFF_VF_TEXCOORD(stage) (CKFF_VF_TEXCOORD0 << (stage))

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
    XHashTable<CKDWORD, CKDWORD> m_Cache; // formatFlags -> layout handle
    CKDWORD m_NextHandle;
};

#endif // CKVERTEXLAYOUTCACHE_H
