#ifndef CKVERTEXPACKING_H
#define CKVERTEXPACKING_H

// Engine-side helpers that write VxDrawPrimitiveData into the canonical
// interleaved vertex layout of the public CKRasterizer v3 interface
// (CKRSTGetVertexLayout in CKRasterizer.h). This is the memory the engine
// hands to LockVertexBuffer / UnlockVertexBuffer.

#include "VxMath.h"
#include "CKRasterizer.h"

#include <string.h>

// Vertex format of a hardware vertex buffer built from draw data: the
// vertex-data subset of the draw flags, without the components the data does
// not actually provide (a LIGHT flag without normals, TWEEN without a second
// position set).
inline CKDWORD CKRSTVertexFormatFromDrawData(const VxDrawPrimitiveData *data)
{
    if (!data)
        return 0;
    CKDWORD format = data->Flags & CKRST_VF_MASK;
    if (!data->NormalPtr)
        format &= ~CKRST_DP_LIGHT;
    if (!data->TweenPositionPtr)
        format &= ~CKRST_DP_TWEEN;
    return format;
}

// Writes vertex `srcIndex` of `data` as vertex `dstIndex` of the canonical
// buffer. Missing components get the same defaults the fixed-function
// interleaver uses (normal +Z, white diffuse, black specular, zero texcoords).
// `texcoord0Override` (2 floats) replaces the first texture coordinate set.
inline void CKRSTPackVertex(const CKRSTVertexLayout &layout, CKBYTE *dst, CKDWORD dstIndex,
                            const VxDrawPrimitiveData *data, CKDWORD srcIndex,
                            const float *texcoord0Override = NULL)
{
    CKBYTE *out = dst + (size_t)dstIndex * layout.Stride;
    const CKBYTE *position = data->PositionPtr
        ? (const CKBYTE *)data->PositionPtr + (size_t)srcIndex * data->PositionStride
        : NULL;

    // Position (3 or 4 floats)
    {
        float *p = (float *)(out + layout.PositionOffset);
        if (position) {
            memcpy(p, position, (size_t)layout.PositionComponents * 4);
        } else {
            p[0] = p[1] = p[2] = 0.0f;
            if (layout.PositionComponents == 4)
                p[3] = 1.0f;
        }
    }

    // Blend weights and indices follow the position inside the source record
    if (layout.WeightOffset >= 0) {
        float *w = (float *)(out + layout.WeightOffset);
        const CKDWORD needed = 12 + (CKDWORD)layout.WeightCount * 4;
        if (position && data->PositionStride >= needed)
            memcpy(w, position + 12, (size_t)layout.WeightCount * 4);
        else
            memset(w, 0, (size_t)layout.WeightCount * 4);
    }
    if (layout.BlendIndexOffset >= 0) {
        const CKDWORD indexOffset = 12 + (CKDWORD)layout.WeightCount * 4;
        CKDWORD indices = 0;
        if (position && data->PositionStride >= indexOffset + 4)
            memcpy(&indices, position + indexOffset, 4);
        memcpy(out + layout.BlendIndexOffset, &indices, 4);
    }

    if (layout.NormalOffset >= 0) {
        float *n = (float *)(out + layout.NormalOffset);
        if (data->NormalPtr) {
            memcpy(n, (const CKBYTE *)data->NormalPtr + (size_t)srcIndex * data->NormalStride, 12);
        } else {
            n[0] = 0.0f; n[1] = 0.0f; n[2] = 1.0f;
        }
    }
    if (layout.TweenPositionOffset >= 0) {
        float *t = (float *)(out + layout.TweenPositionOffset);
        if (data->TweenPositionPtr)
            memcpy(t, (const CKBYTE *)data->TweenPositionPtr + (size_t)srcIndex * data->TweenPositionStride, 12);
        else
            t[0] = t[1] = t[2] = 0.0f;
    }
    if (layout.TweenNormalOffset >= 0) {
        float *t = (float *)(out + layout.TweenNormalOffset);
        if (data->TweenNormalPtr) {
            memcpy(t, (const CKBYTE *)data->TweenNormalPtr + (size_t)srcIndex * data->TweenNormalStride, 12);
        } else {
            t[0] = 0.0f; t[1] = 0.0f; t[2] = 1.0f;
        }
    }
    if (layout.PointSizeOffset >= 0) {
        const float one = 1.0f;
        memcpy(out + layout.PointSizeOffset, &one, 4);
    }
    if (layout.DiffuseOffset >= 0) {
        CKDWORD color = 0xFFFFFFFFu;
        if (data->ColorPtr)
            memcpy(&color, (const CKBYTE *)data->ColorPtr + (size_t)srcIndex * data->ColorStride, 4);
        memcpy(out + layout.DiffuseOffset, &color, 4);
    }
    if (layout.SpecularOffset >= 0) {
        CKDWORD color = layout.PositionComponents == 4 ? 0xFF000000u : 0u;
        if (data->SpecularColorPtr)
            memcpy(&color, (const CKBYTE *)data->SpecularColorPtr + (size_t)srcIndex * data->SpecularColorStride, 4);
        memcpy(out + layout.SpecularOffset, &color, 4);
    }
    for (int stage = 0; stage < layout.TexcoordCount; ++stage) {
        float *uv = (float *)(out + layout.TexcoordOffset[stage]);
        const int dims = layout.TexcoordDims[stage];
        for (int c = 0; c < dims; ++c)
            uv[c] = 0.0f;
        if (stage == 0 && texcoord0Override) {
            uv[0] = texcoord0Override[0];
            if (dims > 1)
                uv[1] = texcoord0Override[1];
            continue;
        }
        const void *src = stage == 0 ? data->TexCoordPtr : data->TexCoordPtrs[stage - 1];
        const CKDWORD stride = stage == 0 ? data->TexCoordStride : data->TexCoordStrides[stage - 1];
        if (src)
            memcpy(uv, (const CKBYTE *)src + (size_t)srcIndex * stride, (size_t)dims * 4);
    }
}

inline void CKRSTPackVertices(const CKRSTVertexLayout &layout, CKBYTE *dst, CKDWORD count,
                              const VxDrawPrimitiveData *data)
{
    for (CKDWORD i = 0; i < count; ++i)
        CKRSTPackVertex(layout, dst, i, data, i);
}

#endif // CKVERTEXPACKING_H
