#include "CKFFUniformState.h"

#include "CKFFStageState.h"
#include "VxColor.h"

#include <string.h>

float CKFFReadFloatRenderState(const CKDrawStateCache &cache, VXRENDERSTATETYPE state, float fallback) {
    (void)fallback;
    CKDWORD bits = cache.GetRenderState(state);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

float CKFFNormalizeAlphaRef(CKDWORD alphaRef) {
    return (float)CKFFAlphaRefByte(alphaRef) / 255.0f;
}

CKBYTE CKFFAlphaRefByte(CKDWORD alphaRef) {
    return (CKBYTE)(alphaRef & 0xFFu);
}

float CKFFPackAlphaFuncPrecision(CKDWORD func, CKDWORD precision) {
    return (float)((func & 0xFu) | ((precision & 0xFu) << 4));
}

CKDWORD CKFFAlphaTestPrecisionForFormat(const VxImageDescEx &desc) {
    CKDWORD mask = desc.AlphaMask;
    CKDWORD alphaBits = 0;
    while (mask != 0) {
        alphaBits += mask & 1u;
        mask >>= 1;
    }

    if (alphaBits <= 8)
        return 0;
    if (alphaBits >= 16)
        return 8;
    return alphaBits - 8;
}

void CKFFPackColorARGB(CKDWORD color, float outColor[4]) {
    if (!outColor)
        return;

    outColor[0] = (float)ColorGetRed(color) / 255.0f;
    outColor[1] = (float)ColorGetGreen(color) / 255.0f;
    outColor[2] = (float)ColorGetBlue(color) / 255.0f;
    outColor[3] = (float)ColorGetAlpha(color) / 255.0f;
}

static void CKFFPackStageConstantUniforms(const CKDWORD stageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES],
                                          float outConstants[CKFF_MAX_TEXTURE_STAGES][4]) {
    if (!outConstants)
        return;

    memset(outConstants, 0, sizeof(float) * CKFF_MAX_TEXTURE_STAGES * 4);
    if (!stageStates)
        return;

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        CKFFPackColorARGB(stageStates[stage][CKRST_TSS_CONSTANT], outConstants[stage]);
    }
}

CKDWORD CKFFResolveMaterialSource(CKBOOL lighting,
                                  CKBOOL colorVertex,
                                  CKBOOL fromVertex,
                                  CKDWORD dpFlags,
                                  CKDWORD streamFlag,
                                  CKFFMaterialSource vertexSource) {
    (void)lighting;
    if (!colorVertex || !fromVertex || ((dpFlags & streamFlag) == 0))
        return CKFF_MS_MATERIAL;
    return vertexSource;
}

float CKFFEncodeShaderLightType(VXLIGHT_TYPE type) {
    switch (type) {
    case VX_LIGHTDIREC: return 0.0f;
    case VX_LIGHTSPOT:  return 2.0f;
    case VX_LIGHTPOINT:
    default:            return 1.0f;
    }
}

void CKFFPackStageParams(const CKDWORD stageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES],
                         const CKDWORD textureHandles[CKFF_MAX_TEXTURE_STAGES],
                         const CKDWORD textureFlags[CKFF_MAX_TEXTURE_STAGES],
                         int activeTextureCount,
                         CKFFStageParamsUniform &outParams,
                         const uint64_t *stageStateSetMasks,
                         CKDWORD samplerSlotOverflowMask) {
    memset(&outParams, 0, sizeof(outParams));
    if (!stageStates)
        return;

    float stageConstants[CKFF_MAX_TEXTURE_STAGES][4] = {};
    CKFFPackStageConstantUniforms(stageStates, stageConstants);

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < activeTextureCount;
        // Stages beyond the fixed cube / volume sampler budget sample as unbound.
        const bool hasTexture = stageActive && textureHandles && textureHandles[stage] != 0 &&
                                (samplerSlotOverflowMask & (1u << stage)) == 0;
        CKDWORD textureTransformFlags = stageActive
            ? (stageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] |
               CKFFResolveMirrorOnceAddressMask(stageStates[stage]))
            : 0;
        if (hasTexture && textureFlags &&
            (textureFlags[stage] & CKRST_TEXTURE_BUMPLUMINANCE) != 0) {
            textureTransformFlags |= CKFF_TTF_BUMP_UNORM;
        }
        float *coord = outParams.Values[CKFFStageParamIndex(stage, CKFF_STAGE_PARAM_COORD)];
        float *constant = outParams.Values[CKFFStageParamIndex(stage, CKFF_STAGE_PARAM_CONSTANT)];

        coord[0] = (float)stageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        coord[1] = (float)textureTransformFlags;
        coord[2] = hasTexture ? 1.0f : 0.0f;
        coord[3] = stageActive && stageStateSetMasks &&
                           (stageStateSetMasks[stage] & (1ull << CKRST_TSS_STAGEBLEND)) != 0
                       ? (float)stageStates[stage][CKRST_TSS_STAGEBLEND] : 0.0f;
        memcpy(constant, stageConstants[stage], sizeof(float) * 4);
    }
}

void CKFFPackSpecialization(const CKFFSpecializationInfo &info, CKFFSpecUniform &outSpec) {
    memset(&outSpec, 0, sizeof(outSpec));
    info.Pack24(outSpec.Values);
}

int CKFFPackClipPlaneUniforms(const VxPlane planes[6], CKDWORD clipMask, CKFFClipPlaneUniform &outClip) {
    memset(&outClip, 0, sizeof(outClip));
    if (!planes)
        return 0;

    int clipCount = 0;
    for (int i = 0; i < 6; ++i) {
        if ((clipMask & (1u << i)) == 0)
            continue;
        outClip.Planes[clipCount][0] = planes[i].m_Normal.x;
        outClip.Planes[clipCount][1] = planes[i].m_Normal.y;
        outClip.Planes[clipCount][2] = planes[i].m_Normal.z;
        outClip.Planes[clipCount][3] = planes[i].m_D;
        ++clipCount;
    }
    outClip.Params[0] = (float)clipCount;
    return clipCount;
}

int CKFFPackViewLights(const CKFFLightData lights[CKFF_MAX_LIGHTS],
                       const CKBOOL lightEnabled[CKFF_MAX_LIGHTS],
                       int activeLightCount,
                       CKBOOL lightingEnabled,
                       const VxMatrix &view,
                       CKFFLightData outViewLights[CKFF_MAX_LIGHTS]) {
    if (!lights || !lightEnabled || !outViewLights || !lightingEnabled)
        return 0;

    int packed = 0;
    for (int i = 0; i < CKFF_MAX_LIGHTS && packed < activeLightCount; i++) {
        if (!lightEnabled[i])
            continue;

        outViewLights[packed] = lights[i];

        VxVector pos(lights[i].Position[0], lights[i].Position[1], lights[i].Position[2]);
        VxVector viewPos;
        Vx3DMultiplyMatrixVector(&viewPos, view, &pos);
        outViewLights[packed].Position[0] = viewPos.x;
        outViewLights[packed].Position[1] = viewPos.y;
        outViewLights[packed].Position[2] = viewPos.z;

        VxVector dir(lights[i].Direction[0], lights[i].Direction[1], lights[i].Direction[2]);
        VxVector viewDir;
        Vx3DRotateVector(&viewDir, view, &dir);
        outViewLights[packed].Direction[0] = viewDir.x;
        outViewLights[packed].Direction[1] = viewDir.y;
        outViewLights[packed].Direction[2] = viewDir.z;

        packed++;
    }

    return packed;
}
