#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKFFConstants.h"
#include "CKRasterizer.h"

#include <algorithm>
#include <cmath>

static bool IsTriangleTopology(VXPRIMITIVETYPE primType) {
    return primType == VX_TRIANGLELIST ||
           primType == VX_TRIANGLEFAN ||
           primType == VX_TRIANGLESTRIP;
}

static VxVector TransformPoint(const VxVector &point, const VxMatrix &matrix) {
    return VxVector(
        point.x * matrix[0][0] + point.y * matrix[1][0] + point.z * matrix[2][0] + matrix[3][0],
        point.x * matrix[0][1] + point.y * matrix[1][1] + point.z * matrix[2][1] + matrix[3][1],
        point.x * matrix[0][2] + point.y * matrix[1][2] + point.z * matrix[2][2] + matrix[3][2]);
}

static VxVector TransformDirection(const VxVector &dir, const VxMatrix &matrix) {
    return VxVector(
        dir.x * matrix[0][0] + dir.y * matrix[1][0] + dir.z * matrix[2][0],
        dir.x * matrix[0][1] + dir.y * matrix[1][1] + dir.z * matrix[2][1],
        dir.x * matrix[0][2] + dir.y * matrix[1][2] + dir.z * matrix[2][2]);
}

static float ComputePointSpriteSize(const VxVector &localPos, const CKFFPointSpriteParams &params) {
    float distance = 0.0f;
    if (params.ScaleEnable) {
        const VxVector worldPos = TransformPoint(localPos, params.World);
        const VxVector viewPos = TransformPoint(worldPos, params.View);
        const float magnitude = viewPos.Magnitude();
        distance = magnitude > 0.0f ? magnitude : 0.0f;
    }

    return CKTransientGeometry::ComputePointSpriteSizeForDistance(
        params.Size, params.MinSize, params.MaxSize, params.ScaleEnable,
        params.ScaleA, params.ScaleB, params.ScaleC, distance);
}

float CKTransientGeometry::ComputePointSpriteSizeForDistance(float size, float minSize, float maxSize,
                                                             CKBOOL scaleEnable,
                                                             float scaleA, float scaleB, float scaleC,
                                                             float distance) {
    if (size <= 0.0f)
        size = 1.0f;
    if (scaleEnable) {
        if (distance < 0.0f)
            distance = 0.0f;
        const float denomSq = scaleA + scaleB * distance + scaleC * distance * distance;
        if (denomSq > 0.000001f)
            size /= sqrtf(denomSq);
    }

    if (minSize <= 0.0f)
        minSize = 1.0f;
    if (maxSize < minSize)
        maxSize = minSize;
    if (size < minSize)
        size = minSize;
    if (size > maxSize)
        size = maxSize;
    return size;
}

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

static void ReadTexcoord0(VxDrawPrimitiveData *data, CKDWORD srcIndex, float uv[2]) {
    uv[0] = 0.0f;
    uv[1] = 0.0f;
    if (data->TexCoordPtr) {
        memcpy(uv, (CKBYTE *)data->TexCoordPtr + srcIndex * data->TexCoordStride, 8);
    }
}

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
    const CKFFPointSpriteParams *pointParams)
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
        CKFFPointSpriteParams params;
        if (pointParams) {
            params = *pointParams;
        } else {
            params.Size = 1.0f;
            params.MinSize = 1.0f;
            params.MaxSize = 64.0f;
            params.ScaleEnable = FALSE;
            params.ScaleA = 1.0f;
            params.ScaleB = 0.0f;
            params.ScaleC = 0.0f;
            params.World = VxMatrix::Identity();
            params.View = VxMatrix::Identity();
        }
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

        VxMatrix invWorld;
        VxMatrix invView;
        Vx3DInverseMatrix(invWorld, params.World);
        Vx3DInverseMatrix(invView, params.View);
        VxVector cameraRightWorld(invView[0][0], invView[0][1], invView[0][2]);
        VxVector cameraUpWorld(invView[1][0], invView[1][1], invView[1][2]);
        VxVector cameraRightLocal = TransformDirection(cameraRightWorld, invWorld);
        VxVector cameraUpLocal = TransformDirection(cameraUpWorld, invWorld);
        cameraRightLocal.Normalize();
        cameraUpLocal.Normalize();

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
                const VxVector center(x, y, pos3[2]);
                const float half = ComputePointSpriteSize(center, params) * 0.5f;
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
                const VxVector center(pos3[0], pos3[1], pos3[2]);
                const float half = ComputePointSpriteSize(center, params) * 0.5f;
                const VxVector right = cameraRightLocal * half;
                const VxVector up = cameraUpLocal * half;
                VxVector cornerVecs[4] = {
                    center - right - up,
                    center + right - up,
                    center + right + up,
                    center - right + up
                };
                float corners[4][3] = {
                    {cornerVecs[0].x, cornerVecs[0].y, cornerVecs[0].z},
                    {cornerVecs[1].x, cornerVecs[1].y, cornerVecs[1].z},
                    {cornerVecs[2].x, cornerVecs[2].y, cornerVecs[2].z},
                    {cornerVecs[3].x, cornerVecs[3].y, cornerVecs[3].z}
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

        XArray<CKWORD> sourceIndices;
        if (indices && indexCount > 0) {
            sourceIndices.Resize(indexCount);
            for (int i = 0; i < indexCount; ++i)
                sourceIndices[i] = indices[i];
        } else {
            sourceIndices.Resize(srcCount);
            for (int i = 0; i < srcCount; ++i)
                sourceIndices[i] = (CKWORD)i;
        }

        XArray<CKWORD> triangleIndices;
        if (primType == VX_TRIANGLELIST) {
            const int triIndexCount = (srcCount / 3) * 3;
            if (triIndexCount <= 0)
                return FALSE;
            triangleIndices.Resize(triIndexCount);
            for (int i = 0; i < triIndexCount; ++i)
                triangleIndices[i] = sourceIndices[i];
        } else {
            const int maxTriListIndices = (srcCount - 2) * 3;
            if (maxTriListIndices <= 0)
                return FALSE;
            triangleIndices.Resize(maxTriListIndices);
            const int outCount = ConvertPrimitiveToTriangleList(
                primType, sourceIndices.Begin(), srcCount, triangleIndices.Begin());
            triangleIndices.Resize(outCount);
        }

        if (triangleIndices.IsEmpty())
            return FALSE;

        CKTransientVertexBuffer tvb;
        memset(&tvb, 0, sizeof(tvb));
        if (!m_Context->AllocTransientVertexBuffer(&tvb, (CKDWORD)triangleIndices.Size(), layoutHandle))
            return FALSE;

        for (int i = 0; i < triangleIndices.Size(); i += 3) {
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
            m_TempIndices.Resize(srcCount);
            for (int i = 0; i < srcCount; i++)
                m_TempIndices[i] = (CKWORD)i;
            ConvertPrimitiveToTriangleList(primType, m_TempIndices.Begin(), srcCount, (CKWORD *)tib.Data);
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
