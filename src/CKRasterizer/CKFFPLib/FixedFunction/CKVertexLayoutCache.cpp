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

CKDWORD CKFFVertexLayout::ComputeStride(CKDWORD formatFlags) {
    CKDWORD stride = 0;
    if (formatFlags & CKFF_VF_POSITION)  stride += 12; // float3
    if (formatFlags & CKFF_VF_POSITIONT) stride += 16; // float4 XYZRHW
    if (formatFlags & CKFF_VF_NORMAL)    stride += 12; // float3
    if (formatFlags & CKFF_VF_TWEENPOSITION) stride += 12; // float3
    if (formatFlags & CKFF_VF_TWEENNORMAL)   stride += 12; // float3
    if (formatFlags & CKFF_VF_BLENDWEIGHT) stride += 12; // float3 blend weights
    if (formatFlags & CKFF_VF_BLENDINDEX)  stride += 4;  // uint8x4 blend indices
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (formatFlags & CKFF_VF_TEXCOORD(stage))
            stride += 16; // float4
    }
    if (formatFlags & CKFF_VF_COLOR0)    stride += 4;  // uint8x4 normalized
    if (formatFlags & CKFF_VF_COLOR1)    stride += 4;  // uint8x4 normalized
    if (formatFlags & CKFF_VF_POINTOFFSET) stride += 8; // two float pixel offsets
    return stride;
}

CKDWORD CKFFVertexLayout::DPFlagsToFormatFlags(CKDWORD dpFlags, bool hasNormal, bool hasUV) {
    return DPFlagsToFormatFlags(dpFlags, hasNormal, hasUV, 0);
}

CKDWORD CKFFVertexLayout::DrawPrimitiveDataToFormatFlags(
    const VxDrawPrimitiveData *data) {
    if (!data)
        return 0;

    const bool hasNormal = data->NormalPtr != nullptr;
    const bool hasUV = data->TexCoordPtr != nullptr;
    CKDWORD flags = DPFlagsToFormatFlags(
        data->Flags, hasNormal, hasUV, data->PositionStride);
    if ((data->Flags & CKRST_DP_TWEEN) != 0) {
        if (data->TweenPositionPtr)
            flags |= CKFF_VF_TWEENPOSITION;
        if (hasNormal && data->TweenNormalPtr)
            flags |= CKFF_VF_TWEENNORMAL;
    }
    return flags;
}

CKDWORD CKFFVertexLayout::DPFlagsToBlendWeightCount(CKDWORD dpFlags) {
    CKDWORD weightCount = 0;
    const CKDWORD weightFlags = dpFlags & CKRST_DP_WEIGHTMASK;
    if (weightFlags & CKRST_DP_WEIGHTS1) weightCount = 1;
    if (weightFlags & CKRST_DP_WEIGHTS2) weightCount = 2;
    if (weightFlags & CKRST_DP_WEIGHTS3) weightCount = 3;
    if (weightFlags & CKRST_DP_WEIGHTS4) weightCount = 4;
    if (weightFlags & CKRST_DP_WEIGHTS5) weightCount = 5;
    return weightCount > 3 ? 3 : weightCount;
}

CKDWORD CKFFVertexLayout::DPFlagsToBlendIndexOffset(CKDWORD dpFlags) {
    return 12 + DPFlagsToBlendWeightCount(dpFlags) * 4;
}

CKDWORD CKFFVertexLayout::DPFlagsToBlendRecordSize(CKDWORD dpFlags) {
    return CKRSTGetBlendVertexSize(dpFlags);
}

CKDWORD CKFFVertexLayout::DPFlagsToFormatFlags(CKDWORD dpFlags, bool hasNormal, bool hasUV, CKDWORD positionStride) {
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

    const CKDWORD weightFlags = dpFlags & CKRST_DP_WEIGHTMASK;
    if (transformed && weightFlags != 0) {
        const bool indexed = (dpFlags & CKRST_DP_MATRIXPAL) != 0;
        CKDWORD requiredStride = DPFlagsToBlendRecordSize(dpFlags);
        if (positionStride == 0 || positionStride >= requiredStride) {
            flags |= CKFF_VF_BLENDWEIGHT;
            if (indexed)
                flags |= CKFF_VF_BLENDINDEX;
        }
    }

    return flags;
}

CKBOOL CKFFVertexLayout::BuildLayout(
    CKDWORD formatFlags, CKVertexElementDesc *elements,
    CKDWORD capacity, CKVertexLayoutDesc &desc) {
    if (!elements || capacity < 20)
        return FALSE;
    CKDWORD count = 0;
    CKWORD offset = 0;

    if (formatFlags & CKFF_VF_POSITION) {
        elements[count].Attrib = CKRST_ATTRIB_POSITION;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 3;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 12;
    }
    if (formatFlags & CKFF_VF_POSITIONT) {
        elements[count].Attrib = CKRST_ATTRIB_POSITION;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 4;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 16;
    }
    if (formatFlags & CKFF_VF_NORMAL) {
        elements[count].Attrib = CKRST_ATTRIB_NORMAL;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 3;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 12;
    }
    if (formatFlags & CKFF_VF_TWEENPOSITION) {
        elements[count].Attrib = CKRST_ATTRIB_TANGENT;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 3;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 12;
    }
    if (formatFlags & CKFF_VF_TWEENNORMAL) {
        elements[count].Attrib = CKRST_ATTRIB_BITANGENT;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 3;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 12;
    }
    if (formatFlags & CKFF_VF_BLENDWEIGHT) {
        elements[count].Attrib = CKRST_ATTRIB_WEIGHT;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 3;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 12;
    }
    if (formatFlags & CKFF_VF_BLENDINDEX) {
        elements[count].Attrib = CKRST_ATTRIB_INDICES;
        elements[count].Type = CKRST_ATTRIBTYPE_UINT8;
        elements[count].Count = 4;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = TRUE;
        elements[count].Offset = offset;
        count++;
        offset += 4;
    }
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (formatFlags & CKFF_VF_TEXCOORD(stage)) {
            elements[count].Attrib = TexCoordAttrib(stage);
            elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
            elements[count].Count = 4;
            elements[count].Normalized = FALSE;
            elements[count].AsInt = FALSE;
            elements[count].Offset = offset;
            count++;
            offset += 16;
        }
    }
    if (formatFlags & CKFF_VF_COLOR0) {
        elements[count].Attrib = CKRST_ATTRIB_COLOR0;
        elements[count].Type = CKRST_ATTRIBTYPE_UINT8;
        elements[count].Count = 4;
        elements[count].Normalized = TRUE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 4;
    }
    if (formatFlags & CKFF_VF_COLOR1) {
        elements[count].Attrib = CKRST_ATTRIB_COLOR1;
        elements[count].Type = CKRST_ATTRIBTYPE_UINT8;
        elements[count].Count = 4;
        elements[count].Normalized = TRUE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 4;
    }
    if (formatFlags & CKFF_VF_POINTOFFSET) {
        // Point-filled triangle quads keep the source vertex at the point
        // centre. Two otherwise-unused scalar attributes move only the final
        // clip position in the vertex shader.
        elements[count].Attrib = CKRST_ATTRIB_TANGENT;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 1;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 4;

        elements[count].Attrib = CKRST_ATTRIB_BITANGENT;
        elements[count].Type = CKRST_ATTRIBTYPE_FLOAT;
        elements[count].Count = 1;
        elements[count].Normalized = FALSE;
        elements[count].AsInt = FALSE;
        elements[count].Offset = offset;
        count++;
        offset += 4;
    }

    desc.Elements = elements;
    desc.ElementCount = count;
    desc.Stride = offset;
    return TRUE;
}
