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
                         const uint64_t *stageStateSetMasks) {
    memset(&outParams, 0, sizeof(outParams));
    if (!stageStates)
        return;

    float stageConstants[CKFF_MAX_TEXTURE_STAGES][4] = {};
    CKFFPackStageConstantUniforms(stageStates, stageConstants);

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < activeTextureCount;
        const bool hasTexture = stageActive && textureHandles && textureHandles[stage] != 0;
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

void CKFFPackFragmentProgram(const CKFFFragmentProgram &program,
                             CKFFFragmentProgramUniform &outProgram) {
    memset(&outProgram, 0, sizeof(outProgram));
    program.Pack24(outProgram.Values);
}

CKFFSamplerShaderState CKFFBuildSamplerShaderState(
    const CKSamplerDesc &sampler,
    CKDWORD textureFlags,
    CKDWORD textureTransformFlags,
    CKDWORD shaderTargetFlags) {
    const bool cube = (textureFlags & CKRST_TEXTURE_CUBEMAP) != 0;
    const bool volume = (textureFlags & CKRST_TEXTURE_VOLUMEMAP) != 0;
    const bool depth = (textureFlags & CKRST_TEXTURE_DEPTHSTENCIL) != 0;

    CKDWORD borderAxisMask = 0;
    if (!cube) {
        if (sampler.AddressU == CKRST_ADDRESS_BORDER)
            borderAxisMask |= 1u;
        if (sampler.AddressV == CKRST_ADDRESS_BORDER)
            borderAxisMask |= 2u;
        if (volume && sampler.AddressW == CKRST_ADDRESS_BORDER)
            borderAxisMask |= 4u;
    }

    const bool depthCompare = depth && sampler.CompareFunc != CKRST_COMPARE_NONE;
    const bool manualDepthCompare = depthCompare &&
        (shaderTargetFlags & CKRST_SHADER_TARGET_MANUAL_DEPTH_COMPARE) != 0;
    // SDL GPU pads comparison depth textures before binding them. That path
    // needs neither shader border evaluation nor a second depth comparison.
    const bool manualBorder = borderAxisMask != 0 &&
        (shaderTargetFlags & CKRST_SHADER_TARGET_MANUAL_BORDER) != 0 &&
        (!depthCompare || manualDepthCompare);
    const bool manualAnisotropy = sampler.ShaderAnisotropy != 0 &&
        (((shaderTargetFlags & CKRST_SHADER_TARGET_MANUAL_ANISOTROPY) != 0) ||
         (volume &&
          (shaderTargetFlags & CKRST_SHADER_TARGET_MANUAL_VOLUME_ANISO) != 0) ||
         manualBorder);
    const bool mirrorOnce = !cube &&
        (textureTransformFlags & CKFF_TTF_MIRRORONCE_MASK) != 0;
    const bool manualLod = sampler.MinMipLevel != 0 &&
        (((shaderTargetFlags & CKRST_SHADER_TARGET_MANUAL_LOD) != 0) ||
         manualAnisotropy || manualBorder || manualDepthCompare || mirrorOnce);
    const bool explicitGradient = mirrorOnce || manualLod ||
        manualAnisotropy || manualBorder || manualDepthCompare;

    const CKDWORD minimumMip = sampler.MinMipLevel >
            CKFF_SAMPLER_SHADER_MIN_MIP_MASK
        ? CKFF_SAMPLER_SHADER_MIN_MIP_MASK
        : sampler.MinMipLevel;
    const CKDWORD anisotropyTaps = manualAnisotropy
        ? ((sampler.MaxAnisotropy > CKFF_SAMPLER_SHADER_ANISOTROPY_MASK
                ? CKFF_SAMPLER_SHADER_ANISOTROPY_MASK
                : sampler.MaxAnisotropy) &
           CKFF_SAMPLER_SHADER_ANISOTROPY_MASK)
        : 0u;

    CKDWORD bits =
        (minimumMip << CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT) |
        (anisotropyTaps << CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT) |
        ((borderAxisMask & CKFF_SAMPLER_SHADER_BORDER_AXIS_MASK) <<
         CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT);
    if (sampler.MinFilter != CKRST_FILTER_NEAREST)
        bits |= CKFF_SAMPLER_SHADER_MIN_FILTER_LINEAR;
    if (sampler.MagFilter != CKRST_FILTER_NEAREST)
        bits |= CKFF_SAMPLER_SHADER_MAG_FILTER_LINEAR;
    if (explicitGradient)
        bits |= CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT;
    if (manualLod)
        bits |= CKFF_SAMPLER_SHADER_MANUAL_LOD;
    if (manualAnisotropy)
        bits |= CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY;
    if (manualBorder)
        bits |= CKFF_SAMPLER_SHADER_MANUAL_BORDER;
    if (manualDepthCompare)
        bits |= CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE;
    return CKFFSamplerShaderState(bits);
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
