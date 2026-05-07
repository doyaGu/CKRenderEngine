#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKFFConstants.h"
#include "CKRasterizer.h"

namespace {

bool IsTriangleTopology(VXPRIMITIVETYPE primType) {
    return primType == VX_TRIANGLELIST ||
           primType == VX_TRIANGLEFAN ||
           primType == VX_TRIANGLESTRIP;
}

} // namespace

void CKTransientGeometry::InterleaveVertex(
    void *dst,
    CKDWORD stride,
    CKDWORD dstIndex,
    CKDWORD srcIndex,
    CKDWORD formatFlags,
    VxDrawPrimitiveData *data,
    const float *texcoord0Override,
    const float *positionOverride)
{
    CKBYTE *out = (CKBYTE *)dst + dstIndex * stride;
    CKDWORD offset = 0;

    if (formatFlags & CKFF_VF_POSITION) {
        if (positionOverride) {
            memcpy(out + offset, positionOverride, 12);
        } else if (data->PositionPtr) {
            memcpy(out + offset,
                   (CKBYTE *)data->PositionPtr + srcIndex * data->PositionStride, 12);
        } else {
            memset(out + offset, 0, 12);
        }
        offset += 12;
    }

    if (formatFlags & CKFF_VF_POSITIONT) {
        if (positionOverride) {
            memcpy(out + offset, positionOverride, 16);
        } else if (data->PositionPtr) {
            memcpy(out + offset,
                   (CKBYTE *)data->PositionPtr + srcIndex * data->PositionStride, 16);
        } else {
            float defPositionT[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            memcpy(out + offset, defPositionT, 16);
        }
        offset += 16;
    }

    if (formatFlags & CKFF_VF_NORMAL) {
        if (data->NormalPtr) {
            memcpy(out + offset,
                   (CKBYTE *)data->NormalPtr + srcIndex * data->NormalStride, 12);
        } else {
            float defNormal[3] = {0.0f, 0.0f, 1.0f};
            memcpy(out + offset, defNormal, 12);
        }
        offset += 12;
    }

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (formatFlags & CKFF_VF_TEXCOORD(stage)) {
            const void *src = nullptr;
            CKDWORD srcStride = 0;
            if (stage == 0) {
                src = data->TexCoordPtr;
                srcStride = data->TexCoordStride;
            } else {
                src = data->TexCoordPtrs[stage - 1];
                srcStride = data->TexCoordStrides[stage - 1];
            }

            if (stage == 0 && texcoord0Override) {
                memcpy(out + offset, texcoord0Override, 8);
            } else if (src) {
                memcpy(out + offset, (CKBYTE *)src + srcIndex * srcStride, 8);
            } else {
                memset(out + offset, 0, 8);
            }
            offset += 8;
        }
    }

    if (formatFlags & CKFF_VF_COLOR0) {
        if ((data->Flags & CKRST_DP_DIFFUSE) && data->ColorPtr) {
            CKDWORD argb;
            memcpy(&argb, (CKBYTE *)data->ColorPtr + srcIndex * data->ColorStride, 4);
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

    if (formatFlags & CKFF_VF_COLOR1) {
        if ((data->Flags & CKRST_DP_SPECULAR) && data->SpecularColorPtr) {
            CKDWORD argb;
            memcpy(&argb, (CKBYTE *)data->SpecularColorPtr + srcIndex * data->SpecularColorStride, 4);
            CKDWORD abgr = (argb & 0xFF00FF00) |
                           ((argb & 0x00FF0000) >> 16) |
                           ((argb & 0x000000FF) << 16);
            memcpy(out + offset, &abgr, 4);
        } else {
            CKDWORD black = (formatFlags & CKFF_VF_POSITIONT) ? 0xFF000000 : 0x00000000;
            memcpy(out + offset, &black, 4);
        }
    }
}

namespace {

void ReadTexcoord0(VxDrawPrimitiveData *data, CKDWORD srcIndex, float uv[2]) {
    uv[0] = 0.0f;
    uv[1] = 0.0f;
    if (data->TexCoordPtr) {
        memcpy(uv, (CKBYTE *)data->TexCoordPtr + srcIndex * data->TexCoordStride, 8);
    }
}

} // namespace

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
    VxDrawPrimitiveData *data,
    CKDWORD wrapMode,
    CKBOOL pointSprites,
    float pointSize)
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
    if (pointSprites && primType == VX_POINTLIST && data->PositionPtr) {
        if (pointSize <= 0.0f)
            pointSize = 1.0f;
        CKTransientVertexBuffer tvb;
        memset(&tvb, 0, sizeof(tvb));
        const CKDWORD spriteVertexCount = vertexCount * 4;
        if (!m_Context->AllocTransientVertexBuffer(&tvb, spriteVertexCount, layoutHandle))
            return FALSE;

        CKTransientIndexBuffer tib;
        memset(&tib, 0, sizeof(tib));
        const CKDWORD spriteIndexCount = vertexCount * 6;
        if (!m_Context->AllocTransientIndexBuffer(&tib, spriteIndexCount, FALSE))
            return FALSE;

        const float half = pointSize * 0.5f;
        const float uv[4][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        };
        for (CKDWORD i = 0; i < vertexCount; ++i) {
            const CKBYTE *src = (const CKBYTE *)data->PositionPtr + i * data->PositionStride;
            float pos3[4] = {};
            if (formatFlags & CKFF_VF_POSITIONT) {
                memcpy(pos3, src, 16);
                const float x = pos3[0];
                const float y = pos3[1];
                float corners[4][4] = {
                    {x - half, y - half, pos3[2], pos3[3]},
                    {x + half, y - half, pos3[2], pos3[3]},
                    {x + half, y + half, pos3[2], pos3[3]},
                    {x - half, y + half, pos3[2], pos3[3]}
                };
                for (int j = 0; j < 4; ++j)
                    InterleaveVertex(tvb.Data, stride, i * 4 + j, i, formatFlags, data, uv[j], corners[j]);
            } else {
                memcpy(pos3, src, 12);
                const float x = pos3[0];
                const float y = pos3[1];
                const float z = pos3[2];
                float corners[4][3] = {
                    {x - half, y - half, z},
                    {x + half, y - half, z},
                    {x + half, y + half, z},
                    {x - half, y + half, z}
                };
                for (int j = 0; j < 4; ++j)
                    InterleaveVertex(tvb.Data, stride, i * 4 + j, i, formatFlags, data, uv[j], corners[j]);
            }

            CKWORD *out = (CKWORD *)tib.Data + i * 6;
            const CKWORD base = (CKWORD)(i * 4);
            out[0] = base;
            out[1] = base + 1;
            out[2] = base + 2;
            out[3] = base;
            out[4] = base + 2;
            out[5] = base + 3;
        }

        encoder->SetTransientVertexBuffer(0, &tvb);
        encoder->SetTransientIndexBuffer(&tib);
        return TRUE;
    }

    const bool wrapStage0 =
        (wrapMode & (VXWRAP_U | VXWRAP_V)) != 0 &&
        (formatFlags & CKFF_VF_TEXCOORD0) != 0 &&
        data->TexCoordPtr != nullptr &&
        IsTriangleTopology(primType);

    if (wrapStage0) {
        int srcCount = (indices && indexCount > 0) ? indexCount : (int)vertexCount;
        if (srcCount < 3)
            return FALSE;

        std::vector<CKWORD> sourceIndices;
        if (indices && indexCount > 0) {
            sourceIndices.assign(indices, indices + indexCount);
        } else {
            sourceIndices.resize(srcCount);
            for (int i = 0; i < srcCount; ++i)
                sourceIndices[i] = (CKWORD)i;
        }

        std::vector<CKWORD> triangleIndices;
        if (primType == VX_TRIANGLELIST) {
            const int triIndexCount = (srcCount / 3) * 3;
            if (triIndexCount <= 0)
                return FALSE;
            triangleIndices.assign(sourceIndices.begin(), sourceIndices.begin() + triIndexCount);
        } else {
            const int maxTriListIndices = (srcCount - 2) * 3;
            if (maxTriListIndices <= 0)
                return FALSE;
            triangleIndices.resize(maxTriListIndices);
            const int outCount = ConvertPrimitiveToTriangleList(
                primType, sourceIndices.data(), srcCount, triangleIndices.data());
            triangleIndices.resize(outCount);
        }

        if (triangleIndices.empty())
            return FALSE;

        CKTransientVertexBuffer tvb;
        memset(&tvb, 0, sizeof(tvb));
        if (!m_Context->AllocTransientVertexBuffer(&tvb, (CKDWORD)triangleIndices.size(), layoutHandle))
            return FALSE;

        for (size_t i = 0; i < triangleIndices.size(); i += 3) {
            float uv[3][2];
            for (int j = 0; j < 3; ++j)
                ReadTexcoord0(data, triangleIndices[i + j], uv[j]);

            AdjustTriangleWrapTexcoords(uv, wrapMode);

            for (int j = 0; j < 3; ++j) {
                InterleaveVertex(tvb.Data, stride, (CKDWORD)(i + j), triangleIndices[i + j],
                                 formatFlags, data, uv[j]);
            }
        }

        encoder->SetTransientVertexBuffer(0, &tvb);
        return TRUE;
    }

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
    for (CKDWORD i = 0; i < vertexCount; i++) {
        InterleaveVertex(dst, stride, i, i, formatFlags, data, nullptr);
    }
}

void CKTransientGeometry::AdjustTriangleWrapTexcoords(float uv[3][2], CKDWORD wrapMode) {
    for (int component = 0; component < 2; ++component) {
        const CKDWORD flag = (component == 0) ? VXWRAP_U : VXWRAP_V;
        if ((wrapMode & flag) == 0)
            continue;

        for (int iteration = 0; iteration < 16; ++iteration) {
            float minValue = uv[0][component];
            float maxValue = uv[0][component];
            for (int i = 1; i < 3; ++i) {
                if (uv[i][component] < minValue)
                    minValue = uv[i][component];
                if (uv[i][component] > maxValue)
                    maxValue = uv[i][component];
            }

            if ((maxValue - minValue) <= 0.5f)
                break;

            for (int i = 0; i < 3; ++i) {
                if (uv[i][component] <= minValue + 0.00001f)
                    uv[i][component] += 1.0f;
            }
        }
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
