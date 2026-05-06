#include "CKVertexLayoutCache.h"
#include "CKRasterizer.h"

CKVertexLayoutCache::CKVertexLayoutCache()
    : m_Context(nullptr), m_NextHandle(1) {}

CKVertexLayoutCache::~CKVertexLayoutCache() {
    Shutdown();
}

void CKVertexLayoutCache::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_Cache.clear();
    m_NextHandle = 1;
}

void CKVertexLayoutCache::Shutdown() {
    if (m_Context) {
        for (auto &pair : m_Cache) {
            m_Context->DeleteObject(pair.second, CKRST_OBJ_VERTEXLAYOUT);
        }
    }
    m_Cache.clear();
    m_Context = nullptr;
}

CKDWORD CKVertexLayoutCache::ComputeStride(CKDWORD formatFlags) {
    CKDWORD stride = 0;
    if (formatFlags & CKFF_VF_POSITION)  stride += 12; // float3
    if (formatFlags & CKFF_VF_POSITIONT) stride += 16; // float4 XYZRHW
    if (formatFlags & CKFF_VF_NORMAL)    stride += 12; // float3
    if (formatFlags & CKFF_VF_TEXCOORD0) stride += 8;  // float2
    if (formatFlags & CKFF_VF_TEXCOORD1) stride += 8;  // float2
    if (formatFlags & CKFF_VF_TEXCOORD2) stride += 8;  // float2
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

    // Canonical fixed-function shaders always consume diffuse/specular color
    // and texcoord0. Interleave fills defaults when source data is absent.
    flags |= CKFF_VF_COLOR0 | CKFF_VF_COLOR1 | CKFF_VF_TEXCOORD0;

    if (!hasUV && ((dpFlags & CKRST_DP_STAGESMASK) == 0)) {
        flags |= CKFF_VF_TEXCOORD0;
    }

    return flags;
}

CKDWORD CKVertexLayoutCache::GetLayout(CKDWORD formatFlags, CKDWORD *outStride) {
    auto it = m_Cache.find(formatFlags);
    if (it != m_Cache.end()) {
        if (outStride) *outStride = ComputeStride(formatFlags);
        return it->second;
    }

    // Build element array
    CKVertexElementDesc elements[8];
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
    if (formatFlags & CKFF_VF_TEXCOORD0) {
        elements[count].Attrib = CKRST_ATTRIB_TEXCOORD0;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 2;
        elements[count].Normalized = FALSE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 8;
    }
    if (formatFlags & CKFF_VF_TEXCOORD1) {
        elements[count].Attrib = CKRST_ATTRIB_TEXCOORD1;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 2;
        elements[count].Normalized = FALSE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 8;
    }
    if (formatFlags & CKFF_VF_TEXCOORD2) {
        elements[count].Attrib = CKRST_ATTRIB_TEXCOORD2;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 2;
        elements[count].Normalized = FALSE;
        elements[count].Offset = offset;
        elements[count].Stream = 0;
        count++;
        offset += 8;
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
    m_Cache[formatFlags] = handle;

    if (outStride) *outStride = offset;
    return handle;
}
