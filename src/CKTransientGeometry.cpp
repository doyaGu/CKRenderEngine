#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKFFConstants.h"
#include "CKRasterizer.h"
#include "CKRenderFrameCostStats.h"

#include <math.h>

static bool SupportsWrapTopology(VXPRIMITIVETYPE primType) {
    return primType == VX_TRIANGLELIST ||
           primType == VX_TRIANGLEFAN ||
           primType == VX_TRIANGLESTRIP ||
           primType == VX_LINELIST ||
           primType == VX_LINESTRIP;
}

static void AdjustPrimitiveWrapTexcoords(float texcoords[3][4], int vertexCount,
                                         CKDWORD wrapMode, CKDWORD componentCount)
{
    static const CKDWORD wrapFlags[4] = {
        VXWRAP_U, VXWRAP_V, VXWRAP_S, VXWRAP_T
    };
    if (vertexCount < 2 || vertexCount > 3)
        return;
    if (componentCount > 4)
        componentCount = 4;

    for (CKDWORD component = 0; component < componentCount; ++component) {
        if ((wrapMode & wrapFlags[component]) == 0)
            continue;
        const float reference = texcoords[0][component];
        for (int vertex = 1; vertex < vertexCount; ++vertex) {
            const float offset = floorf(reference - texcoords[vertex][component] + 0.5f);
            texcoords[vertex][component] += offset;
        }
    }
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

static CKDWORD TexcoordComponentCount(const CKBYTE *texcoordComponentCounts, int stage) {
    if (!texcoordComponentCounts || stage < 0 || stage >= CKRST_MAX_STAGES)
        return 2;
    CKDWORD count = texcoordComponentCounts[stage];
    if (count == 0)
        return 2;
    if (count > 4)
        return 2;
    return count;
}

static float HomogeneousW(const VxVector &point, const VxMatrix &matrix) {
    return point.x * matrix[0][3] + point.y * matrix[1][3] +
           point.z * matrix[2][3] + matrix[3][3];
}

static float ComputePointSpritePixelSize(const VxVector &viewPos,
                                         const CKFFPointSpriteParams &params) {
    const float distance = params.ScaleEnable ? viewPos.Magnitude() : 0.0f;
    return CKTransientGeometry::ComputePointSpriteSizeForDistance(
        params.Size, params.MinSize, params.MaxSize, params.ScaleEnable,
        params.ScaleA, params.ScaleB, params.ScaleC, distance,
        params.ViewportHeight);
}

static CKDWORD PointSpriteSizeOffset(CKDWORD flags, CKDWORD formatFlags) {
    if (formatFlags & CKFF_VF_POSITIONT)
        return 16;
    if ((formatFlags & (CKFF_VF_BLENDWEIGHT | CKFF_VF_BLENDINDEX)) != 0)
        return CKVertexLayoutCache::DPFlagsToBlendRecordSize(flags);
    return 12;
}

static CKFFPointSpriteParams PointSpriteParamsForVertex(
    VxDrawPrimitiveData *data,
    CKDWORD srcIndex,
    CKDWORD formatFlags,
    const CKFFPointSpriteParams &baseParams)
{
    CKFFPointSpriteParams params = baseParams;
    if (!data || !data->PositionPtr || (data->Flags & CKRST_DP_PSIZE) == 0)
        return params;

    const CKDWORD offset = PointSpriteSizeOffset(data->Flags, formatFlags);
    if (data->PositionStride < offset + sizeof(float))
        return params;

    float size = 0.0f;
    const CKBYTE *src = (const CKBYTE *)data->PositionPtr + srcIndex * data->PositionStride + offset;
    memcpy(&size, src, sizeof(float));
    params.Size = size;
    return params;
}

float CKTransientGeometry::ComputePointSpriteSizeForDistance(float size, float minSize, float maxSize,
                                                             CKBOOL scaleEnable,
                                                             float scaleA, float scaleB, float scaleC,
                                                             float distance,
                                                             float viewportHeight) {
    if (size <= 0.0f)
        size = 1.0f;
    if (scaleEnable) {
        if (distance < 0.0f)
            distance = 0.0f;
        const float denomSq = scaleA + scaleB * distance + scaleC * distance * distance;
        if (denomSq > 0.000001f)
            size = (viewportHeight > 0.0f ? viewportHeight : 1.0f) *
                   size / sqrtf(denomSq);
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
    const float *positionOverride,
    const CKBYTE *texcoordComponentCounts,
    const float *texcoordOverrides)
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

    if (formatFlags & CKFF_VF_TWEENPOSITION) {
        if (data->TweenPositionPtr) {
            memcpy(out + offset,
                   (CKBYTE *)data->TweenPositionPtr +
                       srcIndex * data->TweenPositionStride,
                   12);
        } else {
            memset(out + offset, 0, 12);
        }
        offset += 12;
    }

    if (formatFlags & CKFF_VF_TWEENNORMAL) {
        if (data->TweenNormalPtr) {
            memcpy(out + offset,
                   (CKBYTE *)data->TweenNormalPtr +
                       srcIndex * data->TweenNormalStride,
                   12);
        } else {
            float defNormal[3] = {0.0f, 0.0f, 1.0f};
            memcpy(out + offset, defNormal, 12);
        }
        offset += 12;
    }

    if (formatFlags & CKFF_VF_BLENDWEIGHT) {
        float weights[3] = {};
        const CKDWORD weightCount = CKVertexLayoutCache::DPFlagsToBlendWeightCount(data->Flags);
        if (data->PositionPtr && data->PositionStride >= 12 + weightCount * 4) {
            const CKBYTE *src = (const CKBYTE *)data->PositionPtr + srcIndex * data->PositionStride + 12;
            memcpy(weights, src, weightCount * 4);
        }
        memcpy(out + offset, weights, 12);
        offset += 12;
    }

    if (formatFlags & CKFF_VF_BLENDINDEX) {
        CKDWORD indices = 0;
        const CKDWORD indexOffset = CKVertexLayoutCache::DPFlagsToBlendIndexOffset(data->Flags);
        if (data->PositionPtr && data->PositionStride >= indexOffset + 4) {
            const CKBYTE *src = (const CKBYTE *)data->PositionPtr + srcIndex * data->PositionStride + indexOffset;
            memcpy(&indices, src, 4);
        }
        memcpy(out + offset, &indices, 4);
        offset += 4;
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

            float texcoord[4] = {};
            if (texcoordOverrides) {
                memcpy(texcoord, texcoordOverrides + stage * 4, sizeof(texcoord));
            } else if (stage == 0 && texcoord0Override) {
                texcoord[0] = texcoord0Override[0];
                texcoord[1] = texcoord0Override[1];
            } else if (src) {
                const CKDWORD componentCount = TexcoordComponentCount(texcoordComponentCounts, stage);
                memcpy(texcoord, (CKBYTE *)src + srcIndex * srcStride, componentCount * sizeof(float));
            }
            memcpy(out + offset, texcoord, sizeof(texcoord));
            offset += 16;
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

static void ReadTexcoord(VxDrawPrimitiveData *data, int stage, CKDWORD srcIndex,
                         CKDWORD componentCount, float texcoord[4]) {
    memset(texcoord, 0, sizeof(float) * 4);
    if (!data || stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    const void *src = stage == 0 ? data->TexCoordPtr : data->TexCoordPtrs[stage - 1];
    const CKDWORD stride = stage == 0 ? data->TexCoordStride : data->TexCoordStrides[stage - 1];
    if (!src || stride == 0)
        return;
    if (componentCount == 0 || componentCount > 4)
        componentCount = 2;
    memcpy(texcoord, (const CKBYTE *)src + srcIndex * stride,
           componentCount * sizeof(float));
}

CKTransientGeometry::CKTransientGeometry()
    : m_Context(nullptr), m_LayoutCache(nullptr), m_LastLayout(0),
      m_LastVertexBytes(0), m_LastIndexBytes(0) {}

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
    const CKFFPointSpriteParams *pointParams,
    const CKBYTE *texcoordComponentCounts,
    const CKDWORD *wrapModes)
{
    if (!data || data->VertexCount == 0 || !m_Context || !encoder)
        return FALSE;
    m_LastVertexBytes = 0;
    m_LastIndexBytes = 0;

    // Determine vertex format from data
    const CKDWORD formatFlags =
        CKVertexLayoutCache::DrawPrimitiveDataToFormatFlags(data);

    CKDWORD stride = 0;
    CKDWORD layoutHandle = m_LayoutCache->GetLayout(formatFlags, &stride);
    m_LastLayout = layoutHandle;

    CKDWORD vertexCount = data->VertexCount;
    if (indices && indexCount > 0) {
        for (int i = 0; i < indexCount; ++i) {
            if (indices[i] >= vertexCount)
                return FALSE;
        }
    }

    if ((pointSprites || pointParams) && primType == VX_POINTLIST && data->PositionPtr) {
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
            params.Projection = VxMatrix::Identity();
            params.ViewportWidth = 1.0f;
            params.ViewportHeight = 1.0f;
        }
        const CKDWORD pointCount = indices && indexCount > 0
            ? (CKDWORD)indexCount : vertexCount;
        CKTransientVertexBuffer tvb;
        memset(&tvb, 0, sizeof(tvb));
        if (pointCount > 0x2aaaaaaau)
            return FALSE;
        const CKDWORD spriteIndexCount = pointCount * 6;
        const CKDWORD spriteVertexCount = pointCount * 4;
        if (!m_Context->AllocTransientVertexBuffer(&tvb, spriteVertexCount, layoutHandle))
            return FALSE;
        m_LastVertexBytes = tvb.Size;

        CKTransientIndexBuffer tib;
        memset(&tib, 0, sizeof(tib));
        const CKBOOL index32 = spriteVertexCount > 0x10000u ? TRUE : FALSE;
        if (!m_Context->AllocTransientIndexBuffer(&tib, spriteIndexCount, index32))
            return FALSE;
        m_LastIndexBytes = tib.Size;

        VxMatrix invWorld;
        VxMatrix invView;
        Vx3DInverseMatrix(invWorld, params.World);
        Vx3DInverseMatrix(invView, params.View);
        VxVector cameraRightWorld(invView[0][0], invView[0][1], invView[0][2]);
        VxVector cameraUpWorld(invView[1][0], invView[1][1], invView[1][2]);
        VxVector cameraRightLocal = TransformDirection(cameraRightWorld, invWorld);
        VxVector cameraUpLocal = TransformDirection(cameraUpWorld, invWorld);

        const float uv[4][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        };
        for (CKDWORD i = 0; i < pointCount; ++i) {
            const CKDWORD srcIndex = indices && indexCount > 0 ? indices[i] : i;
            const CKFFPointSpriteParams vertexParams = PointSpriteParamsForVertex(
                data, srcIndex, formatFlags, params);
            float pointTexcoords[4][CKFF_MAX_TEXTURE_STAGES][4] = {};
            if (pointSprites) {
                for (int corner = 0; corner < 4; ++corner) {
                    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
                        pointTexcoords[corner][stage][0] = uv[corner][0];
                        pointTexcoords[corner][stage][1] = uv[corner][1];
                    }
                }
            }
            const CKBYTE *src = (const CKBYTE *)data->PositionPtr +
                                srcIndex * data->PositionStride;
            float pos3[4] = {};
            if (formatFlags & CKFF_VF_POSITIONT) {
                memcpy(pos3, src, 16);
                const float x = pos3[0];
                const float y = pos3[1];
                const VxVector center(x, y, pos3[2]);
                const float half = ComputePointSpriteSizeForDistance(
                    vertexParams.Size, vertexParams.MinSize, vertexParams.MaxSize,
                    FALSE, vertexParams.ScaleA, vertexParams.ScaleB,
                    vertexParams.ScaleC, 0.0f) * 0.5f;
                float corners[4][4] = {
                    {x - half, y - half, pos3[2], pos3[3]},
                    {x + half, y - half, pos3[2], pos3[3]},
                    {x + half, y + half, pos3[2], pos3[3]},
                    {x - half, y + half, pos3[2], pos3[3]}
                };
                for (int j = 0; j < 4; ++j)
                    InterleaveVertex(tvb.Data, stride, i * 4 + j, srcIndex,
                                     formatFlags, data,
                                     nullptr, corners[j],
                                     texcoordComponentCounts,
                                     pointSprites
                                         ? &pointTexcoords[j][0][0]
                                         : nullptr);
            } else {
                memcpy(pos3, src, 12);
                const VxVector center(pos3[0], pos3[1], pos3[2]);
                const VxVector worldPos = TransformPoint(center, vertexParams.World);
                const VxVector viewPos = TransformPoint(worldPos, vertexParams.View);
                const float pixelSize = ComputePointSpritePixelSize(viewPos, vertexParams);
                float clipW = HomogeneousW(viewPos, vertexParams.Projection);
                if (clipW < 0.0f)
                    clipW = -clipW;
                if (clipW < 0.000001f)
                    clipW = 1.0f;
                const float projectionX = fabsf(vertexParams.Projection[0][0]);
                const float projectionY = fabsf(vertexParams.Projection[1][1]);
                const float viewportWidth = vertexParams.ViewportWidth > 0.0f
                    ? vertexParams.ViewportWidth : 1.0f;
                const float viewportHeight = vertexParams.ViewportHeight > 0.0f
                    ? vertexParams.ViewportHeight : 1.0f;
                const float halfX = projectionX > 0.000001f
                    ? pixelSize * clipW / (viewportWidth * projectionX)
                    : pixelSize * 0.5f;
                const float halfY = projectionY > 0.000001f
                    ? pixelSize * clipW / (viewportHeight * projectionY)
                    : pixelSize * 0.5f;
                const VxVector right = cameraRightLocal * halfX;
                const VxVector up = cameraUpLocal * halfY;
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
                    InterleaveVertex(tvb.Data, stride, i * 4 + j, srcIndex,
                                     formatFlags, data,
                                     nullptr, corners[j],
                                     texcoordComponentCounts,
                                     pointSprites
                                         ? &pointTexcoords[j][0][0]
                                         : nullptr);
            }

            const CKDWORD base = i * 4;
            if (index32) {
                CKDWORD *out = (CKDWORD *)tib.Data + i * 6;
                out[0] = base;
                out[1] = base + 1;
                out[2] = base + 2;
                out[3] = base;
                out[4] = base + 2;
                out[5] = base + 3;
            } else {
                CKWORD *out = (CKWORD *)tib.Data + i * 6;
                out[0] = (CKWORD)base;
                out[1] = (CKWORD)(base + 1);
                out[2] = (CKWORD)(base + 2);
                out[3] = (CKWORD)base;
                out[4] = (CKWORD)(base + 2);
                out[5] = (CKWORD)(base + 3);
            }
        }

        encoder->SetTransientVertexBuffer(0, &tvb);
        encoder->SetTransientIndexBuffer(&tib);
        CK_FRAME_COST_ADD_TRANSIENT_PREPARE(m_LastVertexBytes,
                                                  m_LastIndexBytes,
                                                  FALSE);
        return TRUE;
    }

    CKDWORD activeWrapModes[CKFF_MAX_TEXTURE_STAGES] = {};
    activeWrapModes[0] = wrapMode;
    if (wrapModes)
        memcpy(activeWrapModes, wrapModes, sizeof(activeWrapModes));

    bool wrapTexcoords = false;
    if (SupportsWrapTopology(primType)) {
        for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
            const void *texcoord = stage == 0
                ? data->TexCoordPtr
                : data->TexCoordPtrs[stage - 1];
            if ((activeWrapModes[stage] & VXWRAP_MASK) != 0 &&
                (formatFlags & CKFF_VF_TEXCOORD(stage)) != 0 && texcoord) {
                wrapTexcoords = true;
                break;
            }
        }
    }

    if (wrapTexcoords) {
        int srcCount = (indices && indexCount > 0) ? indexCount : (int)vertexCount;
        const int primitiveVertexCount =
            (primType == VX_LINELIST || primType == VX_LINESTRIP) ? 2 : 3;
        if (srcCount < primitiveVertexCount)
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

        XArray<CKWORD> primitiveIndices;
        if (primType == VX_TRIANGLELIST) {
            const int triIndexCount = (srcCount / 3) * 3;
            if (triIndexCount <= 0)
                return FALSE;
            primitiveIndices.Resize(triIndexCount);
            for (int i = 0; i < triIndexCount; ++i)
                primitiveIndices[i] = sourceIndices[i];
        } else if (primType == VX_TRIANGLEFAN || primType == VX_TRIANGLESTRIP) {
            const int maxTriListIndices = (srcCount - 2) * 3;
            if (maxTriListIndices <= 0)
                return FALSE;
            primitiveIndices.Resize(maxTriListIndices);
            const int outCount = ConvertPrimitiveToTriangleList(
                primType, sourceIndices.Begin(), srcCount, primitiveIndices.Begin());
            primitiveIndices.Resize(outCount);
        } else if (primType == VX_LINELIST) {
            const int lineIndexCount = (srcCount / 2) * 2;
            if (lineIndexCount <= 0)
                return FALSE;
            primitiveIndices.Resize(lineIndexCount);
            for (int i = 0; i < lineIndexCount; ++i)
                primitiveIndices[i] = sourceIndices[i];
        } else {
            const int lineIndexCount = (srcCount - 1) * 2;
            primitiveIndices.Resize(lineIndexCount);
            for (int i = 0; i < srcCount - 1; ++i) {
                primitiveIndices[i * 2] = sourceIndices[i];
                primitiveIndices[i * 2 + 1] = sourceIndices[i + 1];
            }
        }

        if (primitiveIndices.IsEmpty())
            return FALSE;

        CKTransientVertexBuffer tvb;
        memset(&tvb, 0, sizeof(tvb));
        if (!m_Context->AllocTransientVertexBuffer(&tvb, (CKDWORD)primitiveIndices.Size(), layoutHandle))
            return FALSE;
        m_LastVertexBytes = tvb.Size;

        for (int i = 0; i < primitiveIndices.Size(); i += primitiveVertexCount) {
            float texcoords[3][CKFF_MAX_TEXTURE_STAGES][4] = {};
            for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
                if ((formatFlags & CKFF_VF_TEXCOORD(stage)) == 0)
                    continue;
                const CKDWORD componentCount = TexcoordComponentCount(
                    texcoordComponentCounts, stage);
                float stageTexcoords[3][4];
                for (int j = 0; j < primitiveVertexCount; ++j) {
                    ReadTexcoord(data, stage, primitiveIndices[i + j],
                                 componentCount, stageTexcoords[j]);
                }
                AdjustPrimitiveWrapTexcoords(stageTexcoords, primitiveVertexCount,
                                             activeWrapModes[stage], componentCount);
                for (int j = 0; j < primitiveVertexCount; ++j)
                    memcpy(texcoords[j][stage], stageTexcoords[j], sizeof(stageTexcoords[j]));
            }
            for (int j = 0; j < primitiveVertexCount; ++j) {
                InterleaveVertex(tvb.Data, stride, (CKDWORD)(i + j), primitiveIndices[i + j],
                                 formatFlags, data, nullptr, nullptr, texcoordComponentCounts,
                                 &texcoords[j][0][0]);
            }
        }

        encoder->SetTransientVertexBuffer(0, &tvb);
        CK_FRAME_COST_ADD_TRANSIENT_PREPARE(m_LastVertexBytes,
                                                  m_LastIndexBytes,
                                                  primType == VX_TRIANGLEFAN ||
                                                  primType == VX_TRIANGLESTRIP);
        return TRUE;
    }

    // Allocate transient vertex buffer
    CKTransientVertexBuffer tvb;
    memset(&tvb, 0, sizeof(tvb));
    if (!m_Context->AllocTransientVertexBuffer(&tvb, vertexCount, layoutHandle))
        return FALSE;
    m_LastVertexBytes = tvb.Size;

    // Interleave vertex data into the transient buffer
    InterleaveVertices(tvb.Data, stride, vertexCount, formatFlags, data, texcoordComponentCounts);

    // Handle indices and topology conversion
    CKTransientIndexBuffer tib;
    memset(&tib, 0, sizeof(tib));
    CKBOOL hasIndexBuffer = FALSE;
    if (primType == VX_TRIANGLEFAN || primType == VX_TRIANGLESTRIP) {
        // Must convert to triangle list (bgfx doesn't support fan/strip natively)
        int srcCount = (indices && indexCount > 0) ? indexCount : (int)vertexCount;
        int maxTriListIndices = (srcCount - 2) * 3;
        if (maxTriListIndices <= 0) return FALSE;

        if (!m_Context->AllocTransientIndexBuffer(&tib, maxTriListIndices, FALSE))
            return FALSE;
        m_LastIndexBytes = tib.Size;
        hasIndexBuffer = TRUE;

        if (indices && indexCount > 0) {
            ConvertPrimitiveToTriangleList(primType, indices, srcCount, (CKWORD *)tib.Data);
        } else {
            // Generate sequential indices for non-indexed fan/strip
            m_TempIndices.Resize(srcCount);
            for (int i = 0; i < srcCount; i++)
                m_TempIndices[i] = (CKWORD)i;
            ConvertPrimitiveToTriangleList(primType, m_TempIndices.Begin(), srcCount, (CKWORD *)tib.Data);
        }
    } else if (indices && indexCount > 0) {
        // Triangle list or line list with explicit indices
        if (!m_Context->AllocTransientIndexBuffer(&tib, indexCount, FALSE))
            return FALSE;
        m_LastIndexBytes = tib.Size;
        hasIndexBuffer = TRUE;
        memcpy(tib.Data, indices, indexCount * sizeof(CKWORD));
    }

    encoder->SetTransientVertexBuffer(0, &tvb);
    if (hasIndexBuffer)
        encoder->SetTransientIndexBuffer(&tib);

    CK_FRAME_COST_ADD_TRANSIENT_PREPARE(m_LastVertexBytes,
                                              m_LastIndexBytes,
                                              primType == VX_TRIANGLEFAN ||
                                              primType == VX_TRIANGLESTRIP);
    return TRUE;
}

void CKTransientGeometry::InterleaveVertices(
    void *dst, CKDWORD stride, CKDWORD vertexCount,
    CKDWORD formatFlags, VxDrawPrimitiveData *data,
    const CKBYTE *texcoordComponentCounts)
{
    for (CKDWORD i = 0; i < vertexCount; i++) {
        InterleaveVertex(dst, stride, i, i, formatFlags, data, nullptr, nullptr, texcoordComponentCounts);
    }
}

void CKTransientGeometry::AdjustTriangleWrapTexcoords(float uv[3][2], CKDWORD wrapMode) {
    float texcoords[3][4] = {};
    for (int vertex = 0; vertex < 3; ++vertex) {
        texcoords[vertex][0] = uv[vertex][0];
        texcoords[vertex][1] = uv[vertex][1];
    }
    AdjustTriangleWrapTexcoords(texcoords, wrapMode, 2);
    for (int vertex = 0; vertex < 3; ++vertex) {
        uv[vertex][0] = texcoords[vertex][0];
        uv[vertex][1] = texcoords[vertex][1];
    }
}

void CKTransientGeometry::AdjustTriangleWrapTexcoords(float texcoords[3][4],
                                                       CKDWORD wrapMode,
                                                       CKDWORD componentCount) {
    AdjustPrimitiveWrapTexcoords(texcoords, 3, wrapMode, componentCount);
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
