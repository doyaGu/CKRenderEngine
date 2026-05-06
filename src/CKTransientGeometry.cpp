#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKRasterizer.h"

CKTransientGeometry::CKTransientGeometry()
    : m_Context(nullptr), m_LayoutCache(nullptr), m_LastLayout(0) {}

CKTransientGeometry::~CKTransientGeometry() {
    Shutdown();
}

void CKTransientGeometry::Init(CKRasterizerContext *ctx, CKVertexLayoutCache *layoutCache) {
    m_Context = ctx;
    m_LayoutCache = layoutCache;
}

void CKTransientGeometry::Shutdown() {
    m_Context = nullptr;
    m_LayoutCache = nullptr;
}

CKBOOL CKTransientGeometry::Prepare(
    CKRasterizerEncoder *encoder,
    VXPRIMITIVETYPE primType,
    CKWORD *indices,
    int indexCount,
    VxDrawPrimitiveData *data)
{
    if (!data || data->VertexCount == 0 || !m_Context || !encoder)
        return FALSE;

    // Determine vertex format from data
    bool hasNormal = (data->NormalPtr != nullptr);
    bool hasUV = (data->TexCoordPtr != nullptr);
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(
        data->Flags, hasNormal, hasUV);

    CKDWORD stride = 0;
    CKDWORD layoutHandle = m_LayoutCache->GetLayout(formatFlags, &stride);
    m_LastLayout = layoutHandle;

    CKDWORD vertexCount = data->VertexCount;

    // Allocate transient vertex buffer
    CKTransientVertexBuffer tvb;
    memset(&tvb, 0, sizeof(tvb));
    if (!m_Context->AllocTransientVertexBuffer(&tvb, vertexCount, layoutHandle))
        return FALSE;

    // Interleave vertex data into the transient buffer
    InterleaveVertices(tvb.Data, stride, vertexCount, formatFlags, data);

    // Bind transient VB
    encoder->SetTransientVertexBuffer(0, &tvb);

    // Handle indices and topology conversion
    if (primType == VX_TRIANGLEFAN || primType == VX_TRIANGLESTRIP) {
        // Must convert to triangle list (bgfx doesn't support fan/strip natively)
        int srcCount = (indices && indexCount > 0) ? indexCount : (int)vertexCount;
        int maxTriListIndices = (srcCount - 2) * 3;
        if (maxTriListIndices <= 0) return FALSE;

        CKTransientIndexBuffer tib;
        memset(&tib, 0, sizeof(tib));
        if (!m_Context->AllocTransientIndexBuffer(&tib, maxTriListIndices, FALSE))
            return FALSE;

        if (indices && indexCount > 0) {
            ConvertPrimitiveToTriangleList(primType, indices, srcCount, (CKWORD *)tib.Data);
        } else {
            // Generate sequential indices for non-indexed fan/strip
            m_TempIndices.resize(srcCount);
            for (int i = 0; i < srcCount; i++)
                m_TempIndices[i] = (CKWORD)i;
            ConvertPrimitiveToTriangleList(primType, m_TempIndices.data(), srcCount, (CKWORD *)tib.Data);
        }
        encoder->SetTransientIndexBuffer(&tib);
    } else if (indices && indexCount > 0) {
        // Triangle list or line list with explicit indices
        CKTransientIndexBuffer tib;
        memset(&tib, 0, sizeof(tib));
        if (!m_Context->AllocTransientIndexBuffer(&tib, indexCount, FALSE))
            return FALSE;
        memcpy(tib.Data, indices, indexCount * sizeof(CKWORD));
        encoder->SetTransientIndexBuffer(&tib);
    }

    return TRUE;
}

void CKTransientGeometry::InterleaveVertices(
    void *dst, CKDWORD stride, CKDWORD vertexCount,
    CKDWORD formatFlags, VxDrawPrimitiveData *data)
{
    CKBYTE *out = (CKBYTE *)dst;

    for (CKDWORD i = 0; i < vertexCount; i++) {
        CKDWORD offset = 0;

        // Position (float3, 12 bytes)
        if (formatFlags & CKFF_VF_POSITION) {
            if (data->PositionPtr) {
                memcpy(out + offset,
                       (CKBYTE *)data->PositionPtr + i * data->PositionStride, 12);
            } else {
                memset(out + offset, 0, 12);
            }
            offset += 12;
        }

        // Pre-transformed position (float4 XYZRHW, 16 bytes)
        if (formatFlags & CKFF_VF_POSITIONT) {
            if (data->PositionPtr) {
                memcpy(out + offset,
                       (CKBYTE *)data->PositionPtr + i * data->PositionStride, 16);
            } else {
                float defPositionT[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                memcpy(out + offset, defPositionT, 16);
            }
            offset += 16;
        }

        // Normal (float3, 12 bytes)
        if (formatFlags & CKFF_VF_NORMAL) {
            if (data->NormalPtr) {
                memcpy(out + offset,
                       (CKBYTE *)data->NormalPtr + i * data->NormalStride, 12);
            } else {
                // Default normal: (0, 0, 1)
                float defNormal[3] = {0.0f, 0.0f, 1.0f};
                memcpy(out + offset, defNormal, 12);
            }
            offset += 12;
        }

        // TexCoord0 (float2, 8 bytes)
        if (formatFlags & CKFF_VF_TEXCOORD0) {
            if (data->TexCoordPtr) {
                memcpy(out + offset,
                       (CKBYTE *)data->TexCoordPtr + i * data->TexCoordStride, 8);
            } else {
                memset(out + offset, 0, 8);
            }
            offset += 8;
        }

        // TexCoord1 (float2, 8 bytes)
        if (formatFlags & CKFF_VF_TEXCOORD1) {
            if (data->TexCoordPtrs && data->TexCoordPtrs[0]) {
                memcpy(out + offset,
                       (CKBYTE *)data->TexCoordPtrs[0] + i * data->TexCoordStrides[0], 8);
            } else {
                memset(out + offset, 0, 8);
            }
            offset += 8;
        }

        // TexCoord2 (float2, 8 bytes)
        if (formatFlags & CKFF_VF_TEXCOORD2) {
            if (data->TexCoordPtrs && data->TexCoordPtrs[1]) {
                memcpy(out + offset,
                       (CKBYTE *)data->TexCoordPtrs[1] + i * data->TexCoordStrides[1], 8);
            } else {
                memset(out + offset, 0, 8);
            }
            offset += 8;
        }

        // Color0 (ARGB DWORD → ABGR for bgfx, 4 bytes)
        if (formatFlags & CKFF_VF_COLOR0) {
            if ((data->Flags & CKRST_DP_DIFFUSE) && data->ColorPtr) {
                CKDWORD argb;
                memcpy(&argb, (CKBYTE *)data->ColorPtr + i * data->ColorStride, 4);
                // Swizzle ARGB → ABGR (swap R and B)
                CKDWORD abgr = (argb & 0xFF00FF00) |
                               ((argb & 0x00FF0000) >> 16) |
                               ((argb & 0x000000FF) << 16);
                memcpy(out + offset, &abgr, 4);
            } else {
                CKDWORD white = 0xFFFFFFFF;
                memcpy(out + offset, &white, 4);
            }
            offset += 4;
        }

        // Color1/Specular (same ARGB → ABGR swizzle, 4 bytes)
        if (formatFlags & CKFF_VF_COLOR1) {
            if ((data->Flags & CKRST_DP_SPECULAR) && data->SpecularColorPtr) {
                CKDWORD argb;
                memcpy(&argb, (CKBYTE *)data->SpecularColorPtr + i * data->SpecularColorStride, 4);
                CKDWORD abgr = (argb & 0xFF00FF00) |
                               ((argb & 0x00FF0000) >> 16) |
                               ((argb & 0x000000FF) << 16);
                memcpy(out + offset, &abgr, 4);
            } else {
                CKDWORD black = 0x00000000;
                memcpy(out + offset, &black, 4);
            }
            offset += 4;
        }

        out += stride;
    }
}

int CKTransientGeometry::ConvertToTriangleList(
    VXPRIMITIVETYPE srcType, CKWORD *srcIndices, int srcCount, CKWORD *dst)
{
    return ConvertPrimitiveToTriangleList(srcType, srcIndices, srcCount, dst);
}

int CKTransientGeometry::ConvertPrimitiveToTriangleList(
    VXPRIMITIVETYPE srcType, CKWORD *srcIndices, int srcCount, CKWORD *dst)
{
    if (srcCount < 3) return 0;

    int outCount = 0;

    if (srcType == VX_TRIANGLEFAN) {
        // Fan: vertex 0 is the hub
        for (int i = 1; i < srcCount - 1; i++) {
            dst[outCount++] = srcIndices[0];
            dst[outCount++] = srcIndices[i];
            dst[outCount++] = srcIndices[i + 1];
        }
    } else if (srcType == VX_TRIANGLESTRIP) {
        for (int i = 0; i < srcCount - 2; i++) {
            if (i & 1) {
                // Odd triangle: swap winding
                dst[outCount++] = srcIndices[i + 1];
                dst[outCount++] = srcIndices[i];
                dst[outCount++] = srcIndices[i + 2];
            } else {
                dst[outCount++] = srcIndices[i];
                dst[outCount++] = srcIndices[i + 1];
                dst[outCount++] = srcIndices[i + 2];
            }
        }
    }

    return outCount;
}
