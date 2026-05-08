#include "CKFFStageState.h"
#include "CKFFStateDesc.h"
#include "CKVertexLayoutCache.h"

#include <cstring>

CKDWORD CKFFBaseTextureArg(CKDWORD arg) {
    return arg & ~(CKRST_TA_COMPLEMENT | CKRST_TA_ALPHAREPLICATE);
}

CK_ADDRESS_MODE CKFFTranslateAddressMode(CKDWORD mode) {
    switch (mode) {
    case VXTEXTURE_ADDRESSMIRROR:
        return CKRST_ADDRESS_MIRROR;
    case VXTEXTURE_ADDRESSCLAMP:
        return CKRST_ADDRESS_CLAMP;
    case VXTEXTURE_ADDRESSBORDER:
        return CKRST_ADDRESS_BORDER;
    case VXTEXTURE_ADDRESSMIRRORONCE:
        // MIRRORONCE is not directly expressible in the current bgfx sampler contract.
        return CKRST_ADDRESS_CLAMP;
    case VXTEXTURE_ADDRESSWRAP:
    default:
        return CKRST_ADDRESS_WRAP;
    }
}

CKFFTextureStageOps CKFFLegacyTextureBlendToStageOps(CKDWORD blend) {
    CKFFTextureStageOps ops;
    ops.ColorOp = CKRST_TOP_MODULATE;
    ops.ColorArg0 = CKRST_TA_CURRENT;
    ops.ColorArg1 = CKRST_TA_TEXTURE;
    ops.ColorArg2 = CKRST_TA_CURRENT;
    ops.AlphaOp = CKRST_TOP_MODULATE;
    ops.AlphaArg0 = CKRST_TA_CURRENT;
    ops.AlphaArg1 = CKRST_TA_TEXTURE;
    ops.AlphaArg2 = CKRST_TA_CURRENT;
    ops.ResultArg = CKRST_TA_CURRENT;

    switch (blend & VXTEXTUREBLEND_MASK) {
    case VXTEXTUREBLEND_DECAL:
    case VXTEXTUREBLEND_COPY:
        ops.ColorOp = CKRST_TOP_SELECTARG1;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        break;
    case VXTEXTUREBLEND_DECALALPHA:
    case VXTEXTUREBLEND_DECALMASK:
        ops.ColorOp = CKRST_TOP_BLENDTEXTUREALPHA;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        ops.AlphaArg1 = CKRST_TA_DIFFUSE;
        break;
    case VXTEXTUREBLEND_MODULATEALPHA:
    case VXTEXTUREBLEND_MODULATEMASK:
        ops.AlphaOp = CKRST_TOP_MODULATE;
        break;
    case VXTEXTUREBLEND_ADD:
        ops.ColorOp = CKRST_TOP_ADD;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        ops.AlphaArg1 = CKRST_TA_CURRENT;
        break;
    case VXTEXTUREBLEND_DOTPRODUCT3:
        ops.ColorOp = CKRST_TOP_DOTPRODUCT3;
        ops.ColorArg2 = CKRST_TA_TFACTOR;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        ops.AlphaArg1 = CKRST_TA_CURRENT;
        break;
    case VXTEXTUREBLEND_MODULATE:
    default:
        break;
    }

    return ops;
}

CKDWORD CKFFLegacyTextureBlendToColorOp(CKDWORD blend) {
    return CKFFLegacyTextureBlendToStageOps(blend).ColorOp;
}

CKDWORD CKFFLegacyTextureBlendToAlphaOp(CKDWORD blend) {
    return CKFFLegacyTextureBlendToStageOps(blend).AlphaOp;
}

CKBOOL CKFFStageBlendToTextureOps(CKDWORD stageBlend,
                                  CKDWORD &colorOp, CKDWORD &colorArg1, CKDWORD &colorArg2,
                                  CKDWORD &alphaOp, CKDWORD &alphaArg1, CKDWORD &alphaArg2) {
    const CKDWORD src = (stageBlend >> 4) & 0xF;
    const CKDWORD dst = stageBlend & 0xF;

    if (!((src == VXBLEND_ZERO && dst == VXBLEND_SRCCOLOR) ||
          (src == VXBLEND_DESTCOLOR && dst == VXBLEND_ZERO))) {
        return FALSE;
    }

    colorOp = CKRST_TOP_MODULATE;
    colorArg1 = CKRST_TA_TEXTURE;
    colorArg2 = CKRST_TA_CURRENT;
    alphaOp = CKRST_TOP_SELECTARG2;
    alphaArg1 = CKRST_TA_TEXTURE;
    alphaArg2 = CKRST_TA_CURRENT;
    return TRUE;
}

CKDWORD CKFFResolveStageColorOp(const CKDWORD *stage, bool stageActive, bool hasTexture) {
    if (!stageActive)
        return CKRST_TOP_DISABLE;

    CKDWORD op = stage[CKRST_TSS_OP];
    if (op != 0)
        return op;
    if (!hasTexture)
        return CKRST_TOP_DISABLE;
    return CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorOp;
}

CKDWORD CKFFResolveStageAlphaOp(const CKDWORD *stage, bool stageActive, bool hasTexture) {
    if (!stageActive)
        return CKRST_TOP_DISABLE;

    CKDWORD op = stage[CKRST_TSS_AOP];
    if (op != 0)
        return op;
    return hasTexture ? CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaOp : CKRST_TOP_DISABLE;
}

CKDWORD CKFFResolveStageColorArg1(const CKDWORD *stage, bool hasTexture) {
    CKDWORD arg = stage[CKRST_TSS_ARG1];
    if (arg != 0)
        return arg;
    return hasTexture ? CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorArg1 : CKRST_TA_DIFFUSE;
}

CKDWORD CKFFResolveStageColorArg2(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_ARG2];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorArg2;
}

CKDWORD CKFFResolveStageColorArg0(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_COLORARG0];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorArg0;
}

CKDWORD CKFFResolveStageAlphaArg1(const CKDWORD *stage, bool hasTexture) {
    CKDWORD arg = stage[CKRST_TSS_AARG1];
    if (arg != 0)
        return arg;
    return hasTexture ? CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaArg1 : CKRST_TA_DIFFUSE;
}

CKDWORD CKFFResolveStageAlphaArg2(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_AARG2];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaArg2;
}

CKDWORD CKFFResolveStageAlphaArg0(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_ALPHAARG0];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaArg0;
}

CKDWORD CKFFResolveStageResultArg(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_RESULTARG0];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ResultArg;
}

static float StageStateAsFloat(CKDWORD value) {
    float result = 0.0f;
    memcpy(&result, &value, sizeof(float));
    return result;
}

void CKFFPackBumpEnvUniform(const CKDWORD *stageState, float outBumpEnv[2][4]) {
    if (!outBumpEnv)
        return;

    memset(outBumpEnv, 0, sizeof(float) * 8);
    if (!stageState)
        return;

    outBumpEnv[0][0] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT00]);
    outBumpEnv[0][1] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT01]);
    outBumpEnv[0][2] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT10]);
    outBumpEnv[0][3] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT11]);
    outBumpEnv[1][0] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVLSCALE]);
    outBumpEnv[1][1] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVLOFFSET]);
}

Vx2DVector CKFFEvaluateBumpEnvTexcoord(const Vx2DVector &baseUv,
                                       const VxColor &bumpValue,
                                       const float bumpEnv[2][4]) {
    return Vx2DVector(
        baseUv.x + bumpEnv[0][0] * bumpValue.r + bumpEnv[0][1] * bumpValue.g,
        baseUv.y + bumpEnv[0][2] * bumpValue.r + bumpEnv[0][3] * bumpValue.g);
}

float CKFFEvaluateBumpEnvLuminance(const VxColor &bumpValue,
                                   const float bumpEnv[2][4]) {
    float result = bumpValue.b * bumpEnv[1][0] + bumpEnv[1][1];
    XThreshold(result, 0.0f, 1.0f);
    return result;
}

CKFFVertexBlendState CKFFResolveVertexBlendState(CKDWORD vertexBlend,
                                                 CKBOOL indexed,
                                                 CKDWORD formatFlags) {
    CKFFVertexBlendState state = {};
    state.Mode = CKFF_VERTEX_BLEND_DISABLED;
    state.Count = 0;
    state.Indexed = FALSE;
    state.Supported = TRUE;

    if (vertexBlend == VXVBLEND_DISABLE)
        return state;

    if (vertexBlend == VXVBLEND_TWEENING) {
        const bool hasTweenInput = (formatFlags & CKFF_VF_TWEENPOSITION) != 0;
        state.Supported = hasTweenInput ? TRUE : FALSE;
        if (hasTweenInput)
            state.Mode = CKFF_VERTEX_BLEND_TWEEN;
        return state;
    }

    CKDWORD count = 0;
    if (vertexBlend == VXVBLEND_0WEIGHTS || vertexBlend == VXVBLEND_1WEIGHTS)
        count = 1;
    else if (vertexBlend == VXVBLEND_2WEIGHTS)
        count = 2;
    else if (vertexBlend == VXVBLEND_3WEIGHTS)
        count = 3;

    if (count == 0)
        return state;

    const bool hasBlendInput = (formatFlags & CKFF_VF_BLENDWEIGHT) != 0;
    const bool hasIndexInput = !indexed || ((formatFlags & CKFF_VF_BLENDINDEX) != 0);
    state.Supported = (hasBlendInput && hasIndexInput) ? TRUE : FALSE;
    if (state.Supported) {
        state.Mode = CKFF_VERTEX_BLEND_NORMAL;
        state.Count = count;
        state.Indexed = indexed ? TRUE : FALSE;
    }
    return state;
}

int CKFFActiveTextureCountFromDPFlags(CKDWORD dpFlags) {
    CKDWORD mask = dpFlags & CKRST_DP_STAGESMASK;
    int count = 0;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (mask & CKRST_DP_STAGE(stage))
            count = stage + 1;
    }
    return count;
}

int CKFFResolveActiveTextureCount(CKDWORD dpFlags,
                                  const CKDWORD textureHandles[CKFF_MAX_TEXTURE_STAGES],
                                  const CKDWORD stageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES]) {
    int count = CKFFActiveTextureCountFromDPFlags(dpFlags);
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = stageStates[stage][CKRST_TSS_OP];
        if (textureHandles[stage] != 0 ||
            (op != 0 && op != CKRST_TOP_DISABLE)) {
            if (count < stage + 1)
                count = stage + 1;
        }
    }
    return count;
}

CKSamplerDesc CKFFBuildSamplerDesc(const CKDWORD *stageState) {
    CKSamplerDesc desc;
    memset(&desc, 0, sizeof(desc));
    if (!stageState)
        return desc;

    const CKDWORD mag = stageState[CKRST_TSS_MAGFILTER];
    const CKDWORD min = stageState[CKRST_TSS_MINFILTER];
    const CKDWORD addr = stageState[CKRST_TSS_ADDRESS];
    const CKDWORD addrU = stageState[CKRST_TSS_ADDRESSU];
    const CKDWORD addrV = stageState[CKRST_TSS_ADDRESSV];
    const CKDWORD addrW = stageState[CKRST_TSS_ADDRESW];

    switch (mag) {
    case VXTEXTUREFILTER_NEAREST:
        desc.MagFilter = CKRST_FILTER_NEAREST;
        break;
    case VXTEXTUREFILTER_ANISOTROPIC:
        desc.MagFilter = CKRST_FILTER_ANISOTROPIC;
        break;
    default:
        desc.MagFilter = CKRST_FILTER_LINEAR;
        break;
    }

    switch (min) {
    case VXTEXTUREFILTER_NEAREST:
    case VXTEXTUREFILTER_MIPNEAREST:
        desc.MinFilter = CKRST_FILTER_NEAREST;
        desc.MipFilter = CKRST_FILTER_NEAREST;
        break;
    case VXTEXTUREFILTER_MIPLINEAR:
        desc.MinFilter = CKRST_FILTER_NEAREST;
        desc.MipFilter = CKRST_FILTER_LINEAR;
        break;
    case VXTEXTUREFILTER_LINEARMIPNEAREST:
        desc.MinFilter = CKRST_FILTER_LINEAR;
        desc.MipFilter = CKRST_FILTER_NEAREST;
        break;
    case VXTEXTUREFILTER_LINEARMIPLINEAR:
        desc.MinFilter = CKRST_FILTER_LINEAR;
        desc.MipFilter = CKRST_FILTER_LINEAR;
        break;
    case VXTEXTUREFILTER_ANISOTROPIC:
        desc.MinFilter = CKRST_FILTER_ANISOTROPIC;
        desc.MipFilter = CKRST_FILTER_ANISOTROPIC;
        break;
    case VXTEXTUREFILTER_LINEAR:
    default:
        desc.MinFilter = CKRST_FILTER_LINEAR;
        desc.MipFilter = CKRST_FILTER_LINEAR;
        break;
    }

    CK_ADDRESS_MODE translateAddr = CKFFTranslateAddressMode(addr);
    desc.AddressU = (addrU != 0) ? CKFFTranslateAddressMode(addrU) : translateAddr;
    desc.AddressV = (addrV != 0) ? CKFFTranslateAddressMode(addrV) : translateAddr;
    desc.AddressW = (addrW != 0) ? CKFFTranslateAddressMode(addrW) : translateAddr;

    desc.BorderColor = stageState[CKRST_TSS_BORDERCOLOR];
    desc.CompareFunc = CKRST_COMPARE_NONE;

    return desc;
}

CKDWORD CKFFPackTexcoordIndex(CKDWORD index, CKDWORD generation) {
    return (index & 0xFFFFu) | ((generation & 0xFFFFu) << 16);
}

CKDWORD CKFFTexcoordIndex(CKDWORD packed) {
    return packed & 0xFFFFu;
}

CKDWORD CKFFTexcoordGeneration(CKDWORD packed) {
    return (packed >> 16) & 0xFFFFu;
}

CKFFCoverage CKFFClassifyTextureOpCoverage(CKDWORD op) {
    switch (op) {
    case CKRST_TOP_DISABLE:
    case CKRST_TOP_SELECTARG1:
    case CKRST_TOP_SELECTARG2:
    case CKRST_TOP_MODULATE:
    case CKRST_TOP_MODULATE2X:
    case CKRST_TOP_MODULATE4X:
    case CKRST_TOP_ADD:
    case CKRST_TOP_ADDSIGNED:
    case CKRST_TOP_ADDSIGNED2X:
    case CKRST_TOP_SUBTRACT:
    case CKRST_TOP_ADDSMOOTH:
    case CKRST_TOP_BLENDDIFFUSEALPHA:
    case CKRST_TOP_BLENDTEXTUREALPHA:
    case CKRST_TOP_BLENDFACTORALPHA:
    case CKRST_TOP_BLENDTEXTUREALPHAPM:
    case CKRST_TOP_BLENDCURRENTALPHA:
    case CKRST_TOP_MODULATEALPHA_ADDCOLOR:
    case CKRST_TOP_MODULATECOLOR_ADDALPHA:
    case CKRST_TOP_MODULATEINVALPHA_ADDCOLOR:
    case CKRST_TOP_MODULATEINVCOLOR_ADDALPHA:
    case CKRST_TOP_DOTPRODUCT3:
    case CKRST_TOP_MULTIPLYADD:
    case CKRST_TOP_LERP:
    case CKRST_TOP_BUMPENVMAP:
    case CKRST_TOP_BUMPENVMAPLUMINANCE:
        return CKFF_COVERAGE_EXACT;
    case CKRST_TOP_PREMODULATE:
        return CKFF_COVERAGE_FALLBACK;
    default:
        return CKFF_COVERAGE_UNTESTED;
    }
}

CKFFCoverage CKFFClassifyShaderSemanticCoverage(CKFFShaderSemantic semantic) {
    switch (semantic) {
    case CKFF_SHADER_SEMANTIC_ARG_DIFFUSE:
    case CKFF_SHADER_SEMANTIC_ARG_CURRENT:
    case CKFF_SHADER_SEMANTIC_ARG_TEXTURE:
    case CKFF_SHADER_SEMANTIC_ARG_TFACTOR:
    case CKFF_SHADER_SEMANTIC_ARG_SPECULAR:
    case CKFF_SHADER_SEMANTIC_ARG_TEMP:
    case CKFF_SHADER_SEMANTIC_ARG_COMPLEMENT:
    case CKFF_SHADER_SEMANTIC_ARG_ALPHAREPLICATE:
    case CKFF_SHADER_SEMANTIC_RESULT_CURRENT:
    case CKFF_SHADER_SEMANTIC_RESULT_TEMP:
    case CKFF_SHADER_SEMANTIC_PROJECTED_SAMPLING:
    case CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACENORMAL:
    case CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACEPOSITION:
    case CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACEREFLECTION:
    case CKFF_SHADER_SEMANTIC_TEXGEN_SPHEREMAP:
    case CKFF_SHADER_SEMANTIC_BUMPENVMAP:
    case CKFF_SHADER_SEMANTIC_BUMPENVMAPLUMINANCE:
        return CKFF_COVERAGE_EXACT;
    default:
        return CKFF_COVERAGE_UNTESTED;
    }
}
