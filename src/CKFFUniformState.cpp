#include "CKFFUniformState.h"

#include "CKFFStageState.h"
#include "VxColor.h"

#include <cstring>

float CKFFReadFloatRenderState(const CKDrawStateCache &cache, VXRENDERSTATETYPE state, float fallback) {
    (void)fallback;
    CKDWORD bits = cache.GetRenderState(state);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

float CKFFNormalizeAlphaRef(CKDWORD alphaRef) {
    return (float)(alphaRef & 0xFFu) / 255.0f;
}

void CKFFPackColorARGB(CKDWORD color, float outColor[4]) {
    if (!outColor)
        return;

    outColor[0] = (float)ColorGetRed(color) / 255.0f;
    outColor[1] = (float)ColorGetGreen(color) / 255.0f;
    outColor[2] = (float)ColorGetBlue(color) / 255.0f;
    outColor[3] = (float)ColorGetAlpha(color) / 255.0f;
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
                         int activeTextureCount,
                         CKFFStageParamsUniform &outParams) {
    memset(&outParams, 0, sizeof(outParams));
    if (!stageStates)
        return;

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < activeTextureCount;
        const bool hasTexture = stageActive && textureHandles && textureHandles[stage] != 0;
        outParams.Values[stage * 4 + 0][0] = (float)CKFFResolveStageColorOp(stageStates[stage], stageActive, hasTexture);
        outParams.Values[stage * 4 + 0][1] = (float)CKFFResolveStageColorArg1(stageStates[stage], hasTexture);
        outParams.Values[stage * 4 + 0][2] = (float)CKFFResolveStageColorArg2(stageStates[stage]);
        outParams.Values[stage * 4 + 0][3] = hasTexture ? 1.0f : 0.0f;
        outParams.Values[stage * 4 + 1][0] = (float)CKFFResolveStageAlphaOp(stageStates[stage], stageActive, hasTexture);
        outParams.Values[stage * 4 + 1][1] = (float)CKFFResolveStageAlphaArg1(stageStates[stage], hasTexture);
        outParams.Values[stage * 4 + 1][2] = (float)CKFFResolveStageAlphaArg2(stageStates[stage]);
        outParams.Values[stage * 4 + 1][3] = (float)CKFFResolveStageResultArg(stageStates[stage]);
        outParams.Values[stage * 4 + 2][0] = (float)CKFFResolveStageColorArg0(stageStates[stage]);
        outParams.Values[stage * 4 + 2][1] = (float)stageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        outParams.Values[stage * 4 + 2][2] = (float)stageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        outParams.Values[stage * 4 + 3][0] = (float)CKFFResolveStageAlphaArg0(stageStates[stage]);
    }
}

void CKFFPackSpecializationDwords(const CKFFSpecializationInfo &info, CKFFSpecUniform &outSpec) {
    memset(&outSpec, 0, sizeof(outSpec));
    const CKDWORD *specDwords = info.Data();
    for (CKDWORD i = 0; i < CKFFSpecializationInfo::MaxSpecDwords; ++i) {
        outSpec.Values[i][0] = (float)(specDwords[i] & 0xFFu);
        outSpec.Values[i][1] = (float)((specDwords[i] >> 8) & 0xFFu);
        outSpec.Values[i][2] = (float)((specDwords[i] >> 16) & 0xFFu);
        outSpec.Values[i][3] = (float)((specDwords[i] >> 24) & 0xFFu);
    }
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
