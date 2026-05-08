#include "CKVertexLayoutCache.h"
#include "CKRasterizer.h"
#include "CKFFConstants.h"

static int ActiveTextureCountFromDPFlags(CKDWORD dpFlags) {
    CKDWORD mask = dpFlags & CKRST_DP_STAGESMASK;
    int count = 0;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (mask & CKRST_DP_STAGE(stage))
            count = stage + 1;
    }
    return count;
}

static CK_VERTEX_ATTRIB TexCoordAttrib(int stage) {
    return (CK_VERTEX_ATTRIB)(CKRST_ATTRIB_TEXCOORD0 + stage);
}

CKVertexLayoutCache::CKVertexLayoutCache()
    : m_Context(nullptr), m_NextHandle(1) {}

CKVertexLayoutCache::~CKVertexLayoutCache() {
    Shutdown();
}

void CKVertexLayoutCache::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_Cache.Clear();
    m_NextHandle = 1;
}

void CKVertexLayoutCache::Shutdown() {
    if (m_Context) {
        for (XHashTable<CKDWORD, CKDWORD>::Iterator it = m_Cache.Begin(); it != m_Cache.End(); ++it) {
            m_Context->DeleteObject(*it, CKRST_OBJ_VERTEXLAYOUT);
        }
    }
    m_Cache.Clear();
    m_Context = nullptr;
}

CKDWORD CKVertexLayoutCache::ComputeStride(CKDWORD formatFlags) {
    CKDWORD stride = 0;
    if (formatFlags & CKFF_VF_POSITION)  stride += 12; // float3
    if (formatFlags & CKFF_VF_POSITIONT) stride += 16; // float4 XYZRHW
    if (formatFlags & CKFF_VF_NORMAL)    stride += 12; // float3
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (formatFlags & CKFF_VF_TEXCOORD(stage))
            stride += 8; // float2
    }
    if (formatFlags & CKFF_VF_COLOR0)    stride += 4;  // uint8x4 normalized
    if (formatFlags & CKFF_VF_COLOR1)    stride += 4;  // uint8x4 normalized
    return stride;
}

CKDWORD CKVertexLayoutCache::DPFlagsToFormatFlags(CKDWORD dpFlags, bool hasNormal, bool hasUV) {
    CKDWORD flags = 0;
    const bool transformed = (dpFlags & CKRST_DP_TRANSFORM) != 0;

    if (transformed) {
        flags |= CKFF_VF_POSITION;
        if (hasNormal && (dpFlags & CKRST_DP_LIGHT))
            flags |= CKFF_VF_NORMAL;
    } else {
        flags |= CKFF_VF_POSITIONT;
    }

    // Canonical fixed-function shaders always consume diffuse/specular color.
    flags |= CKFF_VF_COLOR0 | CKFF_VF_COLOR1 | CKFF_VF_TEXCOORD0;

    int activeTextureCount = ActiveTextureCountFromDPFlags(dpFlags);
    if (activeTextureCount == 0 && hasUV)
        activeTextureCount = 1;
    for (int stage = 1; stage < activeTextureCount; ++stage) {
        flags |= CKFF_VF_TEXCOORD(stage);
    }

    return flags;
}

CKDWORD CKVertexLayoutCache::GetLayout(CKDWORD formatFlags, CKDWORD *outStride) {
    CKDWORD layout = 0;
    if (m_Cache.LookUp(formatFlags, layout)) {
        if (outStride) *outStride = ComputeStride(formatFlags);
        return layout;
    }

    // Build element array
    CKVertexElementDesc elements[16];
    CKDWORD count = 0;
    CKWORD offset = 0;

    if (formatFlags & CKFF_VF_POSITION) {
        elements[count].Attrib = CKRST_ATTRIB_POSITION;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 3;
        elements[count].Normalized = FALSE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 12;
    }
    if (formatFlags & CKFF_VF_POSITIONT) {
        elements[count].Attrib = CKRST_ATTRIB_POSITION;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 4;
        elements[count].Normalized = FALSE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 16;
    }
    if (formatFlags & CKFF_VF_NORMAL) {
        elements[count].Attrib = CKRST_ATTRIB_NORMAL;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 3;
        elements[count].Normalized = FALSE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 12;
    }
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (formatFlags & CKFF_VF_TEXCOORD(stage)) {
            elements[count].Attrib = TexCoordAttrib(stage);
            elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
            elements[count].Count = 2;
            elements[count].Normalized = FALSE;
            elements[count].Offset = offset;
            elements[count].Stream = 0;
            count++;
            offset += 8;
        }
    }
    if (formatFlags & CKFF_VF_COLOR0) {
        elements[count].Attrib = CKRST_ATTRIB_COLOR0;
        elements[count].Type = CKRST_ATTRIBTYPE_UINT8;
        elements[count].Count = 4;
        elements[count].Normalized = TRUE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 4;
    }
    if (formatFlags & CKFF_VF_COLOR1) {
        elements[count].Attrib = CKRST_ATTRIB_COLOR1;
        elements[count].Type = CKRST_ATTRIBTYPE_UINT8;
        elements[count].Count = 4;
        elements[count].Normalized = TRUE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 4;
    }

    CKVertexLayoutDesc desc;
    desc.Elements = elements;
    desc.ElementCount = count;
    memset(desc.Stride, 0, sizeof(desc.Stride));
    desc.Stride[0] = offset;

    CKDWORD handle = m_NextHandle++;
    m_Context->CreateVertexLayout(handle, &desc);
    m_Cache.Insert(formatFlags, handle);

    if (outStride) *outStride = offset;
    return handle;
}
