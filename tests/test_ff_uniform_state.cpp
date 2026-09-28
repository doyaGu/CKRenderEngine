#include "CKFFStageState.h"
#include "CKFFShaderABI.h"
#include "CKFFShaderKey.h"
#include "CKFFUniformState.h"
#include "CKFixedFunctionPipeline.h"
#include "CKVertexLayoutCache.h"
#include "TestTriangleMultiset.h"

namespace {

CKFFFragmentProgram BuildTestFragmentProgram(const CKFFShaderKeyFS &key)
{
    const CKFFSamplerLayoutPlan samplerLayoutPlan =
        CKFFBuildSamplerLayoutPlan(key);
    return CKFFBuildFragmentProgram(key, samplerLayoutPlan);
}

CKDWORD FloatStageState(float value) {
    union {
        float F;
        CKDWORD D;
    } u;
    u.F = value;
    return u.D;
}

void BumpEnvUniformsPackEachStageIndependently() {
    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    float bumpEnv[CKFF_MAX_TEXTURE_STAGES * 2][4] = {};

    stages[0][CKRST_TSS_BUMPENVMAT00] = FloatStageState(1.0f);
    stages[0][CKRST_TSS_BUMPENVMAT01] = FloatStageState(2.0f);
    stages[0][CKRST_TSS_BUMPENVMAT10] = FloatStageState(3.0f);
    stages[0][CKRST_TSS_BUMPENVMAT11] = FloatStageState(4.0f);
    stages[0][CKRST_TSS_BUMPENVLSCALE] = FloatStageState(5.0f);
    stages[0][CKRST_TSS_BUMPENVLOFFSET] = FloatStageState(6.0f);

    stages[2][CKRST_TSS_BUMPENVMAT00] = FloatStageState(7.0f);
    stages[2][CKRST_TSS_BUMPENVMAT01] = FloatStageState(8.0f);
    stages[2][CKRST_TSS_BUMPENVMAT10] = FloatStageState(9.0f);
    stages[2][CKRST_TSS_BUMPENVMAT11] = FloatStageState(10.0f);
    stages[2][CKRST_TSS_BUMPENVLSCALE] = FloatStageState(11.0f);
    stages[2][CKRST_TSS_BUMPENVLOFFSET] = FloatStageState(12.0f);

    CKFFPackBumpEnvUniforms(stages, bumpEnv);

    TestCheck(bumpEnv[0][0] == 1.0f && bumpEnv[0][1] == 2.0f &&
                  bumpEnv[0][2] == 3.0f && bumpEnv[0][3] == 4.0f,
              "Stage 0 bump matrix must pack at slot 0");
    TestCheck(bumpEnv[1][0] == 5.0f && bumpEnv[1][1] == 6.0f,
              "Stage 0 bump luminance must pack at slot 1");
    TestCheck(bumpEnv[4][0] == 7.0f && bumpEnv[4][1] == 8.0f &&
                  bumpEnv[4][2] == 9.0f && bumpEnv[4][3] == 10.0f,
              "Stage 2 bump matrix must pack at slot 4");
    TestCheck(bumpEnv[5][0] == 11.0f && bumpEnv[5][1] == 12.0f,
              "Stage 2 bump luminance must pack at slot 5");
}

void TextureArgModifierRepackRoundTripsBothModifierBits() {
    const CKDWORD arg = CKRST_TA_TEMP | CKRST_TA_COMPLEMENT | CKRST_TA_ALPHAREPLICATE;
    const CKDWORD repacked = CKFFFragmentProgram::RepackArg(arg);
    const CKDWORD unpacked = (repacked & 0x7u) | ((repacked & 0x18u) << 1u);

    TestCheck(unpacked == arg,
              "FragmentProgram repack/unpack must preserve complement and alpha replicate modifiers");
}

void ShaderABIConstantsAreConsistent() {
    TestCheck(CKFF_DRAW_PARAM_VEC4_COUNT == 20,
              "u_ffDrawParams ABI must include the tween parameter vec4");
    TestCheck(CKFF_STAGE_PARAM_VEC4_COUNT == 16,
              "u_stageParams ABI must be 16 vec4s (coord + constant per stage)");
    TestCheck(CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT == CKFFFragmentProgram::Vec4Count &&
                  CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT == 5,
              "u_ffProgram ABI must mirror the fragment program lane count (20 lanes = 5 vec4)");
    TestCheck(CKFFStageParamIndex(3, CKFF_STAGE_PARAM_CONSTANT) == 7,
              "Stage parameter index helper must encode two vec4s per stage");
    TestCheck(CKFFSamplerSlot(CKFF_SAMPLER_2D, 2) == 2 &&
                  CKFFSamplerSlot(CKFF_SAMPLER_DEPTH, 7) == 7,
              "2D and depth samplers must bind to the texture slot of their stage (0..7)");
    TestCheck(CKFFSamplerSlot(CKFF_SAMPLER_CUBE, 2) == 10 &&
                  CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, 2) == 14,
              "Cube samplers must bind to slots 8..11 and volume samplers to 12..15 by ordinal");
    TestCheck(CKFF_SAMPLER_SLOT_COUNT == 16 &&
                  CKFFSamplerTypeSlotCount(CKFF_SAMPLER_CUBE) == 4 &&
                  CKFFSamplerTypeSlotCount(CKFF_SAMPLER_VOLUME) == 4,
              "Fixed sampler layout must hold 8 + 4 + 4 samplers");

}

void SamplerShaderStateResolvesBackendResponsibilities() {
    const CKDWORD bgfxFlags =
        CKRST_SHADER_TARGET_MANUAL_LOD |
        CKRST_SHADER_TARGET_MANUAL_ANISOTROPY |
        CKRST_SHADER_TARGET_MANUAL_BORDER |
        CKRST_SHADER_TARGET_MANUAL_DEPTH_COMPARE;
    const CKDWORD sdlFlags =
        CKRST_SHADER_TARGET_MANUAL_VOLUME_ANISO |
        CKRST_SHADER_TARGET_MANUAL_BORDER;

    CKSamplerDesc sampler = {};
    sampler.MinFilter = CKRST_FILTER_ANISOTROPIC;
    sampler.MagFilter = CKRST_FILTER_LINEAR;
    sampler.MipFilter = CKRST_FILTER_ANISOTROPIC;
    sampler.AddressU = CKRST_ADDRESS_WRAP;
    sampler.AddressV = CKRST_ADDRESS_CLAMP;
    sampler.AddressW = CKRST_ADDRESS_WRAP;
    sampler.MinMipLevel = 6;
    sampler.MaxAnisotropy = 12;
    sampler.ShaderAnisotropy = 1;

    const CKFFSamplerShaderState bgfx = CKFFBuildSamplerShaderState(
        sampler, 0, 0, bgfxFlags);
    const CKFFSamplerShaderState sdl = CKFFBuildSamplerShaderState(
        sampler, 0, 0, sdlFlags);
    TestCheck(bgfx.MinimumMipLevel() == 6 &&
                  bgfx.AnisotropyTapCount() == 12 &&
                  bgfx.Has(CKFF_SAMPLER_SHADER_MANUAL_LOD) &&
                  bgfx.Has(CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY) &&
                  bgfx.Has(CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT),
              "bgfx must resolve minimum LOD and anisotropy into exact shader work");
    TestCheck(sdl.MinimumMipLevel() == 6 &&
                  sdl.AnisotropyTapCount() == 0 &&
                  !sdl.Has(CKFF_SAMPLER_SHADER_MANUAL_LOD) &&
                  !sdl.Has(CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY) &&
                  !sdl.Has(CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT),
              "SDL GPU ordinary 2D sampling must retain native minimum LOD and anisotropy");
    TestCheck(bgfx.Has(CKFF_SAMPLER_SHADER_MIN_FILTER_LINEAR) &&
                  bgfx.Has(CKFF_SAMPLER_SHADER_MAG_FILTER_LINEAR),
              "Anisotropic and linear filters must both expose linear footprint filtering");

    const CKFFSamplerShaderState sdlVolume = CKFFBuildSamplerShaderState(
        sampler, CKRST_TEXTURE_VOLUMEMAP, 0, sdlFlags);
    TestCheck(sdlVolume.AnisotropyTapCount() == 12 &&
                  sdlVolume.Has(CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY) &&
                  sdlVolume.Has(CKFF_SAMPLER_SHADER_MANUAL_LOD) &&
                  sdlVolume.Has(CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT),
              "SDL GPU volume anisotropy must resolve to shader taps and explicit gradients");

    sampler.AddressU = CKRST_ADDRESS_BORDER;
    sampler.AddressV = CKRST_ADDRESS_BORDER;
    sampler.AddressW = CKRST_ADDRESS_BORDER;
    const CKFFSamplerShaderState sdlBorder = CKFFBuildSamplerShaderState(
        sampler, 0, 0, sdlFlags);
    TestCheck(sdlBorder.BorderAxisMask() == 3 &&
                  sdlBorder.Has(CKFF_SAMPLER_SHADER_MANUAL_BORDER) &&
                  sdlBorder.Has(CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY),
              "SDL GPU 2D border sampling must preserve both axes and the exact tap cap");
    const CKFFSamplerShaderState volumeBorder = CKFFBuildSamplerShaderState(
        sampler, CKRST_TEXTURE_VOLUMEMAP, 0, sdlFlags);
    TestCheck(volumeBorder.BorderAxisMask() == 7,
              "Volume border sampling must preserve all three address axes");
    const CKFFSamplerShaderState cubeBorder = CKFFBuildSamplerShaderState(
        sampler, CKRST_TEXTURE_CUBEMAP, 0, bgfxFlags);
    TestCheck(cubeBorder.BorderAxisMask() == 0 &&
                  !cubeBorder.Has(CKFF_SAMPLER_SHADER_MANUAL_BORDER),
              "Cube directions must not acquire 2D/3D border-domain handling");

    sampler.MinFilter = CKRST_FILTER_LINEAR;
    sampler.MagFilter = CKRST_FILTER_NEAREST;
    sampler.MipFilter = CKRST_FILTER_LINEAR;
    sampler.ShaderAnisotropy = 0;
    sampler.CompareFunc = CKRST_COMPARE_LEQUAL;
    const CKFFSamplerShaderState bgfxDepth = CKFFBuildSamplerShaderState(
        sampler, CKRST_TEXTURE_DEPTHSTENCIL, 0, bgfxFlags);
    const CKFFSamplerShaderState sdlDepth = CKFFBuildSamplerShaderState(
        sampler, CKRST_TEXTURE_DEPTHSTENCIL, 0, sdlFlags);
    TestCheck(bgfxDepth.Has(CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE) &&
                  bgfxDepth.Has(CKFF_SAMPLER_SHADER_MANUAL_BORDER),
              "bgfx depth comparison and border filtering must remain shader exact");
    TestCheck(!sdlDepth.Has(CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE) &&
                  !sdlDepth.Has(CKFF_SAMPLER_SHADER_MANUAL_BORDER),
              "SDL GPU padded comparison textures must retain native comparison sampling");

    sampler.AddressU = CKRST_ADDRESS_CLAMP;
    sampler.AddressV = CKRST_ADDRESS_CLAMP;
    sampler.MinMipLevel = 0;
    sampler.CompareFunc = CKRST_COMPARE_NONE;
    const CKFFSamplerShaderState mirrorOnce = CKFFBuildSamplerShaderState(
        sampler, 0, CKFF_TTF_MIRRORONCE_U, sdlFlags);
    TestCheck(mirrorOnce.Has(CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT) &&
                  !mirrorOnce.Has(CKFF_SAMPLER_SHADER_MANUAL_LOD),
              "MIRRORONCE must preserve the original footprint without inventing manual LOD");
}

void NativeExactSamplingUsesFinalBindingState() {
    CKFFTextureBindingSet textures;
    CKFFInitTextureBindingSet(&textures);
    textures.ActiveTextureCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        textures.Bindings[stage].Texture = stage + 1;
        textures.Bindings[stage].ShaderState = CKFFSamplerShaderState(
            CKFF_SAMPLER_SHADER_MIN_FILTER_LINEAR |
            CKFF_SAMPLER_SHADER_MAG_FILTER_LINEAR);
    }
    TestCheck(CKFFResolveFragmentSamplingMode(textures) ==
                  CKFF_FRAGMENT_SAMPLING_NATIVE_EXACT,
              "Native-exact sampling must accept states fully represented by native samplers");

    const CKDWORD manualFlags[] = {
        CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT,
        CKFF_SAMPLER_SHADER_MANUAL_LOD,
        CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY,
        CKFF_SAMPLER_SHADER_MANUAL_BORDER,
        CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE,
    };
    for (CKDWORD flag : manualFlags) {
        textures.Bindings[5].ShaderState.Bits |= flag;
        TestCheck(CKFFResolveFragmentSamplingMode(textures) ==
                      CKFF_FRAGMENT_SAMPLING_FULL_EXACT,
                  "Every manual sampler responsibility must select the full exact fragment program");
        textures.Bindings[5].ShaderState.Bits &= ~flag;
    }

    textures.SamplerLayoutPlan.CompareSamplerCount = 1;
    TestCheck(CKFFResolveFragmentSamplingMode(textures) ==
                  CKFF_FRAGMENT_SAMPLING_FULL_EXACT,
              "Comparison samplers must select the full exact fragment program");
    textures.SamplerLayoutPlan.CompareSamplerCount = 0;

    const CKDWORD before = CKFFHashTextureBindingSet(
        textures.ActiveTextureCount, textures.Bindings);
    textures.Bindings[3].ShaderState.Bits |= CKFF_SAMPLER_SHADER_MANUAL_BORDER;
    const CKDWORD after = CKFFHashTextureBindingSet(
        textures.ActiveTextureCount, textures.Bindings);
    TestCheck(before != after,
              "Resolved sampler shader state must participate in texture binding identity");

    CKFFStateStore state;
    state.Reset();
    state.TextureHandles[0] = 17;
    CKFFDrawProbes probes;
    const CKDWORD targetFlags = CKRST_SHADER_TARGET_MANUAL_LOD |
        CKRST_SHADER_TARGET_MANUAL_ANISOTROPY |
        CKRST_SHADER_TARGET_MANUAL_BORDER |
        CKRST_SHADER_TARGET_MANUAL_DEPTH_COMPARE;
    CKFFTextureBinder binder(state, targetFlags, probes);
    CKFFShaderKeyFS key;
    key.Stages[0].HasTexture = true;
    const CKFFSamplerLayoutPlan plan = CKFFBuildSamplerLayoutPlan(key);
    CKFFTextureBindingSet built;
    binder.BuildBindingSet(&built, 1, 1u, plan);
    const CKDWORD transformFlags =
        state.StageStates[0][CKRST_TSS_TEXTURETRANSFORMFLAGS] |
        CKFFResolveMirrorOnceAddressMask(state.StageStates[0]);
    const CKFFSamplerShaderState expected = CKFFBuildSamplerShaderState(
        built.Bindings[0].Sampler, built.Bindings[0].TextureFlags,
        transformFlags, targetFlags);
    TestCheck(built.Bindings[0].ShaderState.Bits == expected.Bits,
              "Texture bindings must retain the shader state resolved from the final sampler descriptor");
}

void TextureBindingCacheInvalidatesEveryBindingDependency() {
    CKFFStateStore state;
    state.Reset();
    state.TextureHandles[0] = 17;
    state.TextureFlags[0] = CKRST_TEXTURE_VALID;
    CKFFDrawProbes probes;
    CKDWORD targetFlags = CKRST_SHADER_TARGET_MANUAL_LOD |
        CKRST_SHADER_TARGET_MANUAL_ANISOTROPY |
        CKRST_SHADER_TARGET_MANUAL_BORDER |
        CKRST_SHADER_TARGET_MANUAL_DEPTH_COMPARE;
    CKFFTextureBinder binder(state, targetFlags, probes);

    CKFFShaderKeyFS key;
    key.Stages[0].HasTexture = true;
    CKFFSamplerLayoutPlan plan = CKFFBuildSamplerLayoutPlan(key);
    CKFFTextureBindingSet first;
    CKFFTextureBindingSet cached;
    binder.BuildBindingSet(&first, 1, 1u, plan);
    binder.BuildBindingSet(&cached, 1, 1u, plan);
    TestCheck(cached.Hash == first.Hash &&
                  cached.Bindings[0].Texture == 17,
              "A stable texture binding request must preserve the resolved binding set");

    state.TextureHandles[0] = 23;
    binder.InvalidateStage(0);
    CKFFTextureBindingSet changedTexture;
    binder.BuildBindingSet(&changedTexture, 1, 1u, plan);
    TestCheck(changedTexture.Bindings[0].Texture == 23 &&
                  changedTexture.Hash != first.Hash,
              "Invalidating a texture stage must rebuild the cached texture identity");

    state.StageStates[0][CKRST_TSS_ADDRESSU] = VXTEXTURE_ADDRESSBORDER;
    state.StageStates[0][CKRST_TSS_BORDERCOLOR] = 0x80402010u;
    binder.InvalidateStage(0);
    CKFFTextureBindingSet changedSampler;
    binder.BuildBindingSet(&changedSampler, 1, 1u, plan);
    TestCheck(changedSampler.Bindings[0].Sampler.AddressU == CKRST_ADDRESS_BORDER &&
                  changedSampler.Hash != changedTexture.Hash,
              "Invalidating a sampler stage must rebuild its native and shader state");

    CKFFSamplerLayoutPlan remappedPlan = plan;
    remappedPlan.Stages[0].NativeSlot = 7;
    CKFFTextureBindingSet remapped;
    binder.BuildBindingSet(&remapped, 1, 1u, remappedPlan);
    TestCheck(remapped.Bindings[0].Stage == 7 &&
                  remapped.Hash != changedSampler.Hash,
              "A sampler layout change must miss the final binding-set cache");

    CKFFTextureBindingSet unsampled;
    binder.BuildBindingSet(&unsampled, 1, 0u, plan);
    TestCheck(unsampled.ActiveTextureCount == 0 &&
                  unsampled.Bindings[0].Texture == 0,
              "A sampled-stage mask change must rebuild the final binding set");

    binder.SetRenderOptions(TRUE, FALSE, FALSE);
    CKFFTextureBindingSet overridden;
    binder.BuildBindingSet(&overridden, 1, 1u, plan);
    TestCheck(overridden.Bindings[0].Sampler.MinFilter == CKRST_FILTER_NEAREST &&
                  overridden.Bindings[0].Sampler.MagFilter == CKRST_FILTER_NEAREST,
              "Sampler render options must invalidate the final binding set");
}

void StageParamsPackThroughABIIndices() {
    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    CKDWORD textures[CKFF_MAX_TEXTURE_STAGES] = {};
    CKFFStageParamsUniform params;

    stages[2][CKRST_TSS_OP] = CKRST_TOP_SELECTARG1;
    stages[2][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
    stages[2][CKRST_TSS_ARG2] = CKRST_TA_TFACTOR;
    stages[2][CKRST_TSS_AOP] = CKRST_TOP_SELECTARG2;
    stages[2][CKRST_TSS_AARG1] = CKRST_TA_CURRENT;
    stages[2][CKRST_TSS_AARG2] = CKRST_TA_TEXTURE;
    stages[2][CKRST_TSS_COLORARG0] = CKRST_TA_CONSTANT;
    stages[2][CKRST_TSS_ALPHAARG0] = CKRST_TA_TEMP;
    stages[2][CKRST_TSS_TEXCOORDINDEX] = 6;
    stages[2][CKRST_TSS_TEXTURETRANSFORMFLAGS] = 0x103;
    stages[2][CKRST_TSS_CONSTANT] = 0x80402010;
    textures[2] = 77;

    CKDWORD textureFlags[CKFF_MAX_TEXTURE_STAGES] = {};
    CKFFPackStageParams(stages, textures, textureFlags, 3, params);

    const float *coord = params.Values[CKFFStageParamIndex(2, CKFF_STAGE_PARAM_COORD)];
    const float *constant = params.Values[CKFFStageParamIndex(2, CKFF_STAGE_PARAM_CONSTANT)];
    const float *inactive = params.Values[CKFFStageParamIndex(3, CKFF_STAGE_PARAM_COORD)];

    TestCheck(coord[0] == 6.0f && coord[1] == (float)0x103 && coord[2] == 1.0f && coord[3] == 0.0f,
              "Stage coord params must pack texcoord index, transform flags and the has-texture flag");
    TestCheck(constant[0] > 0.24f && constant[0] < 0.26f &&
                  constant[1] > 0.12f && constant[1] < 0.13f &&
                  constant[2] > 0.06f && constant[2] < 0.07f &&
                  constant[3] > 0.49f && constant[3] < 0.51f,
              "Stage constant must pack RGBA into the constant ABI slot");
    TestCheck(inactive[1] == 0.0f && inactive[2] == 0.0f,
              "Inactive stages must pack neither transform flags nor a texture");
}

void MirrorOnceAddressModesPackIntoStageParams() {
    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    CKDWORD textures[CKFF_MAX_TEXTURE_STAGES] = {};
    CKFFStageParamsUniform params;

    stages[1][CKRST_TSS_OP] = CKRST_TOP_SELECTARG1;
    stages[1][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
    stages[1][CKRST_TSS_TEXCOORDINDEX] = CKFFPackTexcoordIndex(3, CKFF_TEXGEN_NONE);
    stages[1][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_COUNT2 | CKRST_TTF_PROJECTED;
    stages[1][CKRST_TSS_ADDRESS] = VXTEXTURE_ADDRESSMIRRORONCE;
    stages[1][CKRST_TSS_ADDRESSV] = VXTEXTURE_ADDRESSCLAMP;
    textures[1] = 11;

    stages[5][CKRST_TSS_OP] = CKRST_TOP_SELECTARG1;
    stages[5][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
    stages[5][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_COUNT3;
    stages[5][CKRST_TSS_ADDRESS] = VXTEXTURE_ADDRESSCLAMP;
    stages[5][CKRST_TSS_ADDRESSU] = VXTEXTURE_ADDRESSMIRRORONCE;
    stages[5][CKRST_TSS_ADDRESSV] = VXTEXTURE_ADDRESSMIRRORONCE;
    stages[5][CKRST_TSS_ADDRESW] = VXTEXTURE_ADDRESSMIRRORONCE;
    textures[5] = 12;

    CKDWORD textureFlags[CKFF_MAX_TEXTURE_STAGES] = {};
    CKFFPackStageParams(stages, textures, textureFlags, 6, params);

    const float *stage1 = params.Values[CKFFStageParamIndex(1, CKFF_STAGE_PARAM_COORD)];
    const CKDWORD stage1Flags = (CKDWORD)stage1[1];
    TestCheck(stage1[0] == (float)CKFFPackTexcoordIndex(3, CKFF_TEXGEN_NONE),
              "MIRRORONCE packing must not overwrite packed texcoord index");
    TestCheck((stage1Flags & 0x1ffu) == (CKRST_TTF_COUNT2 | CKRST_TTF_PROJECTED),
              "MIRRORONCE packing must preserve transform count and projected bits");
    TestCheck((stage1Flags & CKFF_TTF_MIRRORONCE_MASK) == (CKFF_TTF_MIRRORONCE_U | CKFF_TTF_MIRRORONCE_W),
              "Inherited MIRRORONCE must apply per-axis override rules before packing");

    const float *stage5 = params.Values[CKFFStageParamIndex(5, CKFF_STAGE_PARAM_COORD)];
    const CKDWORD stage5Flags = (CKDWORD)stage5[1];
    TestCheck((stage5Flags & CKFF_TTF_MIRRORONCE_MASK) == CKFF_TTF_MIRRORONCE_MASK,
              "Runtime stages 4..7 must carry U/V/W MIRRORONCE masks through stage params");
}

void MirrorOnceSamplerDescFallsBackToClamp() {
    CKDWORD stage[CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    stage[CKRST_TSS_ADDRESS] = VXTEXTURE_ADDRESSMIRRORONCE;
    stage[CKRST_TSS_ADDRESSV] = VXTEXTURE_ADDRESSMIRROR;

    CKSamplerDesc sampler = CKFFBuildSamplerDesc(stage);
    TestCheck(sampler.AddressU == CKRST_ADDRESS_CLAMP &&
                  sampler.AddressW == CKRST_ADDRESS_CLAMP,
              "MIRRORONCE must remain a clamp sampler fallback in bgfx sampler desc");
    TestCheck(sampler.AddressV == CKRST_ADDRESS_MIRROR,
              "Explicit non-MIRRORONCE axis override must still win over inherited address mode");
    TestCheck((CKFFResolveMirrorOnceAddressMask(stage) & CKFF_TTF_MIRRORONCE_MASK) ==
                  (CKFF_TTF_MIRRORONCE_U | CKFF_TTF_MIRRORONCE_W),
              "FFP shader mask must preserve the original MIRRORONCE axes despite sampler fallback");
}

void FragmentProgramPacksSamplerOrdinalsEveryStage() {
    CKFFFSStateDesc desc;
    CKDWORD expectedOrdinals = 0;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        desc.SetStageColorOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageAlphaArg1(stage, CKRST_TA_TEXTURE);
        const CKDWORD mask = (stage % 7u) + 1u;
        desc.SetStageMirrorOnceMask(stage, mask);
        expectedOrdinals |= stage << (stage * 3);
    }

    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0xFFu);
    CKFFFragmentProgram program = BuildTestFragmentProgram(key);

    TestCheck(key.Stages[3].MirrorOnceMask == 4 && key.Stages[7].MirrorOnceMask == 1,
              "Shader key must preserve per-stage MIRRORONCE mask");
    TestCheck(program.Get(CKFF_FRAGMENT_PROGRAM_SAMPLER_ORDINALS) == expectedOrdinals,
              "Fragment program must pack the sampler ordinals of all eight stages");
    TestCheck(program.GetSamplerOrdinal(7) == 7 &&
                  program.GetSamplerOrdinal(2) == 2,
              "Per-stage sampler ordinal accessor must read three bits per stage");

    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage)
        desc.SetStageMirrorOnceMask(stage, 0);
    const CKFFFragmentProgram withoutMirror = BuildTestFragmentProgram(
        CKFFBuildShaderKeyFS(desc, 0xFFu));
    TestCheck(program == withoutMirror,
              "MIRRORONCE must not duplicate stage-param state in the fragment program");
}

void LastActiveTextureStageFragmentProgramRoundTrips() {
    CKFFFragmentProgram program;
    for (CKDWORD lastStage = 0; lastStage < 8; ++lastStage) {
        program.Set(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE, lastStage);
        TestCheck(program.Get(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE) == lastStage,
                  "Last active texture stage must round-trip through fragment-program lanes");
    }
}

void FragmentProgramUniformCarriesLanesAsExactIntegers() {
    CKFFFragmentProgram program;
    program.Set(CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED, 1);
    program.Set(CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC, VXCMP_GREATER);
    program.Set(CKFF_FRAGMENT_PROGRAM_SAMPLER_ORDINALS, 0xFFFFFFu);
    program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_CUBE);
    program.SetStage(1, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_VOLUME);
    program.SetStage(7, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, CKRST_TOP_LERP);

    CKFFFragmentProgramUniform packed;
    CKFFPackFragmentProgram(program, packed);

    const CKDWORD *lanes = program.Lanes();
    for (CKDWORD lane = 0; lane < CKFFFragmentProgram::LaneCount; ++lane) {
        const float value = packed.Values[lane / 4][lane % 4];
        TestCheck((CKDWORD)value == lanes[lane] && value < 16777216.0f,
                  "u_ffProgram must carry every 24-bit lane as an exact integer float");
    }
    TestCheck(packed.Values[17 / 4][17 % 4] == 16777215.0f,
              "A full 24-bit lane must survive the float encoding");
    const CKFFFragmentProgram unpacked =
        CKFFFragmentProgram::Unpack24(&packed.Values[0][0], CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT * 4);
    TestCheck(unpacked == program,
              "u_ffProgram encoding must round-trip the fragment program");
}

void PremodulateCoverageIsExact() {
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_PREMODULATE) == CKFF_COVERAGE_EXACT,
              "PREMODULATE must be marked as exact coverage");
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_MODULATE) == CKFF_COVERAGE_EXACT,
              "MODULATE coverage must remain exact as a control case");
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_BUMPENVMAP) == CKFF_COVERAGE_EXACT,
              "signed DuDv bump mapping must remain exact");
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_BUMPENVMAPLUMINANCE) == CKFF_COVERAGE_EXACT,
              "luminance bump mapping must be marked exact after packed format conversion");
    TestCheck(CKFFClassifyShaderSemanticCoverage(
                  CKFF_SHADER_SEMANTIC_BUMPENVMAPLUMINANCE) ==
                  CKFF_COVERAGE_EXACT,
              "luminance bump shader semantics must match texture-op coverage");
}

void BumpMapAlwaysCreatesTextureDependency() {
    CKFFShaderKeyFSStage stage = {};
    stage.ColorOp = CKRST_TOP_BUMPENVMAP;
    stage.ColorArg1 = CKRST_TA_DIFFUSE;
    stage.ColorArg2 = CKRST_TA_CURRENT;
    stage.AlphaOp = CKRST_TOP_SELECTARG1;
    stage.AlphaArg1 = CKRST_TA_CURRENT;

    TestCheck(CKFFShaderKeyStageUsesTexture(stage, 0, 0),
              "BUMPENVMAP must sample its DuDv texture even when color args omit TEXTURE");
}

void TextureCombinerPreservesTempDestination() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(0, CKRST_TA_CONSTANT);
    desc.SetStageAlphaOp(0, CKRST_TOP_DISABLE);
    desc.SetStageResultIsTemp(0, true);
    desc.SetStageColorOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(1, CKRST_TA_TEMP);
    desc.SetStageAlphaOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(1, CKRST_TA_TEMP);
    desc.SetStageResultIsTemp(1, true);

    const CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0);
    TestCheck(key.Stages[0].ResultIsTemp &&
                  key.Stages[0].AlphaOp == CKRST_TOP_DISABLE,
              "TEMP writes with disabled alpha must preserve TEMP alpha");
    TestCheck(key.Stages[1].ResultIsTemp,
              "a final TEMP write must leave CURRENT unchanged");

}

void AlphaRefUsesLowByteOnly() {
    TestCheck(CKFFNormalizeAlphaRef(0x00000080) > 0.501f &&
                  CKFFNormalizeAlphaRef(0x00000080) < 0.503f,
              "Alpha ref 0x80 must normalize to 128/255");
    TestCheck(CKFFNormalizeAlphaRef(0x12345680) == CKFFNormalizeAlphaRef(0x00000080),
              "Alpha ref must ignore high DWORD bits");
    TestCheck(CKFFNormalizeAlphaRef(0xFFFFFF00) == 0.0f,
              "Alpha ref low byte 0 must normalize to 0");
}

void AlphaRefByteUsesLowByteOnly() {
    TestCheck(CKFFAlphaRefByte(0x12345680) == 0x80,
              "Alpha ref byte must ignore high DWORD bits");
    TestCheck(CKFFAlphaRefByte(0xFFFFFF00) == 0x00,
              "Alpha ref byte must keep low byte zero");
}

void AlphaFuncPrecisionPackKeepsCompareFunc() {
    const float packed = CKFFPackAlphaFuncPrecision(VXCMP_GREATER, 0x2);
    const CKDWORD raw = (CKDWORD)packed;

    TestCheck((raw & 0xFu) == VXCMP_GREATER,
              "Packed alpha func must preserve compare function");
    TestCheck(((raw >> 4) & 0xFu) == 0x2,
              "Packed alpha func must preserve precision");
}

void AlphaFuncPrecisionPackSupportsDxvkPrecisionCodes() {
    const CKDWORD precisions[] = { 0x0, 0x2, 0x8, 0xF };

    for (int i = 0; i < 4; ++i) {
        const float packed = CKFFPackAlphaFuncPrecision(VXCMP_LESSEQUAL, precisions[i]);
        const CKDWORD raw = (CKDWORD)packed;

        TestCheck((raw & 0xFu) == VXCMP_LESSEQUAL,
                  "Packed alpha func must preserve compare function");
        TestCheck(((raw >> 4) & 0xFu) == precisions[i],
                  "Packed alpha func must preserve dxvk precision code");
    }
}

void AlphaTestPrecisionForLegacyFormatsDefaultsToEightBit() {
    VxImageDescEx desc;

    VxPixelFormat2ImageDesc(_32_ARGB8888, desc);
    TestCheck(CKFFAlphaTestPrecisionForFormat(desc) == 0,
              "Legacy 32-bit ARGB targets must use the 8-bit alpha-test path");

    VxPixelFormat2ImageDesc(_16_ARGB4444, desc);
    TestCheck(CKFFAlphaTestPrecisionForFormat(desc) == 0,
              "Legacy 16-bit ARGB targets must use the 8-bit alpha-test path");
}

void AlphaTestPrecisionFollowsRenderTargetAlphaMask() {
    VxImageDescEx desc;
    memset(&desc, 0, sizeof(desc));

    desc.AlphaMask = 0;
    TestCheck(CKFFAlphaTestPrecisionForFormat(desc) == 0,
              "No alpha mask must keep the 8-bit alpha-test path");

    desc.AlphaMask = 0x000003FF;
    TestCheck(CKFFAlphaTestPrecisionForFormat(desc) == 2,
              "10-bit alpha mask must request precision code 2");

    desc.AlphaMask = 0x00007FFF;
    TestCheck(CKFFAlphaTestPrecisionForFormat(desc) == 7,
              "15-bit alpha mask must request precision code 7");

    desc.AlphaMask = 0x0000FFFF;
    TestCheck(CKFFAlphaTestPrecisionForFormat(desc) == 8,
              "16-bit alpha mask must request precision code 8");
}

void VertexBlendResolverMatchesDxvkWeightCounts() {
    CKFFVertexBlendState disabled = CKFFResolveVertexBlendState(
        VXVBLEND_DISABLE, FALSE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(disabled.Mode == CKFF_VERTEX_BLEND_DISABLED && disabled.Count == 0 && disabled.Supported &&
                  disabled.UnsupportedReason == CKFF_VERTEX_BLEND_UNSUPPORTED_NONE,
              "Disabled vertex blend must remain supported no-op");

    CKFFVertexBlendState zeroWeights = CKFFResolveVertexBlendState(
        VXVBLEND_0WEIGHTS, FALSE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(zeroWeights.Mode == CKFF_VERTEX_BLEND_NORMAL && zeroWeights.Count == 0 && zeroWeights.Supported,
              "VXVBLEND_0WEIGHTS must enable normal blend with zero explicit weights");

    CKFFVertexBlendState oneWeight = CKFFResolveVertexBlendState(
        VXVBLEND_1WEIGHTS, FALSE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(oneWeight.Mode == CKFF_VERTEX_BLEND_NORMAL && oneWeight.Count == 1 && oneWeight.Supported,
              "VXVBLEND_1WEIGHTS must mean one explicit weight");

    CKFFVertexBlendState threeWeights = CKFFResolveVertexBlendState(
        VXVBLEND_3WEIGHTS, FALSE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(threeWeights.Mode == CKFF_VERTEX_BLEND_NORMAL && threeWeights.Count == 3 && threeWeights.Supported,
              "VXVBLEND_3WEIGHTS must mean three explicit weights");
}

void VertexBlendResolverHandlesMissingIndexedInputAndPositionT() {
    CKFFVertexBlendState missingIndices = CKFFResolveVertexBlendState(
        VXVBLEND_2WEIGHTS, TRUE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(!missingIndices.Supported && missingIndices.Mode == CKFF_VERTEX_BLEND_DISABLED &&
                  missingIndices.UnsupportedReason == CKFF_VERTEX_BLEND_UNSUPPORTED_MISSING_INDEX,
              "Indexed vertex blend must be unsupported without blend indices");

    CKFFVertexBlendState positionT = CKFFResolveVertexBlendState(
        VXVBLEND_2WEIGHTS, FALSE, CKFF_VF_POSITIONT | CKFF_VF_BLENDWEIGHT);
    TestCheck(!positionT.Supported && positionT.Mode == CKFF_VERTEX_BLEND_DISABLED &&
                  positionT.UnsupportedReason == CKFF_VERTEX_BLEND_UNSUPPORTED_POSITIONT,
              "POSITIONT must not enable vertex blend");

    CKFFVertexBlendState missingTweenPosition = CKFFResolveVertexBlendState(
        VXVBLEND_TWEENING, FALSE, CKFF_VF_POSITION);
    TestCheck(!missingTweenPosition.Supported &&
                  missingTweenPosition.UnsupportedReason ==
                      CKFF_VERTEX_BLEND_UNSUPPORTED_MISSING_TWEEN_POSITION,
              "Tweening must reject a missing second position");

    CKFFVertexBlendState missingTweenNormal = CKFFResolveVertexBlendState(
        VXVBLEND_TWEENING, FALSE,
        CKFF_VF_POSITION | CKFF_VF_NORMAL | CKFF_VF_TWEENPOSITION);
    TestCheck(missingTweenNormal.Supported &&
                  missingTweenNormal.Mode == CKFF_VERTEX_BLEND_TWEEN &&
                  missingTweenNormal.UnsupportedReason ==
                      CKFF_VERTEX_BLEND_UNSUPPORTED_NONE,
              "Lit tweening can retain the original normal without a second normal");

    CKFFVertexBlendState tween = CKFFResolveVertexBlendState(
        VXVBLEND_TWEENING, FALSE,
        CKFF_VF_POSITION | CKFF_VF_NORMAL |
        CKFF_VF_TWEENPOSITION | CKFF_VF_TWEENNORMAL);
    TestCheck(tween.Supported && tween.Mode == CKFF_VERTEX_BLEND_TWEEN &&
                  tween.UnsupportedReason == CKFF_VERTEX_BLEND_UNSUPPORTED_NONE,
              "Tweening must accept complete second position and normal inputs");

    CKFFVertexBlendState indexedTween = CKFFResolveVertexBlendState(
        VXVBLEND_TWEENING, TRUE,
        CKFF_VF_POSITION | CKFF_VF_TWEENPOSITION);
    TestCheck(indexedTween.Supported &&
                  indexedTween.Mode == CKFF_VERTEX_BLEND_TWEEN &&
                  indexedTween.UnsupportedReason == CKFF_VERTEX_BLEND_UNSUPPORTED_NONE,
              "Tweening ignores matrix-index enable without weighted blending");
}

void DPWeightFlagsAddBlendLayoutFlags() {
    CKDWORD flags = CKFFVertexLayout::DPFlagsToFormatFlags(
        (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2), FALSE, FALSE);
    TestCheck((flags & CKFF_VF_BLENDWEIGHT) != 0,
              "DP weight flags must request blend weight attribute");
    TestCheck((flags & CKFF_VF_BLENDINDEX) == 0,
              "Non-indexed DP weight flags must not request blend index attribute");

    CKDWORD indexed = CKFFVertexLayout::DPFlagsToFormatFlags(
        (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2 | CKRST_DP_MATRIXPAL), FALSE, FALSE);
    TestCheck((indexed & CKFF_VF_BLENDWEIGHT) != 0 && (indexed & CKFF_VF_BLENDINDEX) != 0,
              "Matrix palette DP flags must request both weights and indices");
}

void SamplerTypesPackIntoFragmentProgram() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageSamplerType(0, CKFF_SAMPLER_CUBE);

    desc.SetStageColorOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(1, CKRST_TA_CURRENT);
    desc.SetStageAlphaOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(1, CKRST_TA_CURRENT);

    desc.SetStageColorOp(2, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(2, CKRST_TA_CURRENT);
    desc.SetStageAlphaOp(2, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(2, CKRST_TA_CURRENT);

    desc.SetStageColorOp(3, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(3, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(3, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(3, CKRST_TA_TEXTURE);
    desc.SetStageSamplerType(3, CKFF_SAMPLER_DEPTH);

    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, (1u << 0) | (1u << 3));
    CKFFFragmentProgram spec = BuildTestFragmentProgram(key);

    TestCheck(spec.GetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_CUBE &&
                  spec.GetStage(1, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_2D &&
                  spec.GetStage(2, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_2D &&
                  spec.GetStage(3, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_DEPTH,
              "Sampler type fragment program must pack per stage");
}

void VolumeSamplerAndCompareFuncPackIntoFragmentProgram() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageSamplerType(0, CKFF_SAMPLER_VOLUME);

    desc.SetStageColorOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(1, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(1, CKRST_TA_TEXTURE);
    desc.SetStageSamplerType(1, CKFF_SAMPLER_DEPTH);
    desc.SetStageSamplerCompareFunc(1, CKRST_COMPARE_LEQUAL);

    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, (1u << 0) | (1u << 1));
    CKFFFragmentProgram spec = BuildTestFragmentProgram(key);

    TestCheck(spec.GetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_VOLUME,
              "Volume sampler type must pack into fragment program");
    TestCheck(spec.GetStage(1, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_DEPTH,
              "Depth sampler type must remain packed independently");
    TestCheck(spec.GetStage(1, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC) == CKRST_COMPARE_LEQUAL &&
                  spec.GetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC) == CKRST_COMPARE_NONE,
              "Depth compare func must pack four bits per stage");
}

void VolumeSamplerMaskCanBeDerivedFromShaderKey() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageSamplerType(0, CKFF_SAMPLER_VOLUME);

    for (CKDWORD stage = 1; stage < 7; ++stage) {
        desc.SetStageColorOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageColorArg1(stage, CKRST_TA_CURRENT);
        desc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageAlphaArg1(stage, CKRST_TA_CURRENT);
    }

    desc.SetStageColorArg1(2, CKRST_TA_TEXTURE);
    desc.SetStageAlphaArg1(2, CKRST_TA_TEXTURE);
    desc.SetStageSamplerType(2, CKFF_SAMPLER_VOLUME);

    desc.SetStageColorOp(7, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(7, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(7, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(7, CKRST_TA_TEXTURE);
    desc.SetStageSamplerType(7, CKFF_SAMPLER_VOLUME);

    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, (1u << 0) | (1u << 2) | (1u << 7));
    CKDWORD volumeMask = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (key.Stages[stage].HasTexture && key.Stages[stage].SamplerType == CKFF_SAMPLER_VOLUME)
            volumeMask |= 1u << stage;
    }

    TestCheck(volumeMask == ((1u << 0) | (1u << 2) | (1u << 7)),
              "Volume sampler mask must be derivable from active shader key stages");
}

void SamplerOrdinalCountsOnlySamplingStagesOfTheSameType() {
    CKFFFSStateDesc desc;
    for (CKDWORD stage = 0; stage < 6; ++stage) {
        desc.SetStageColorOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageAlphaArg1(stage, CKRST_TA_TEXTURE);
    }
    desc.SetStageSamplerType(0, CKFF_SAMPLER_CUBE);
    desc.SetStageColorArg1(1, CKRST_TA_CURRENT); // stage 1 does not sample
    desc.SetStageAlphaArg1(1, CKRST_TA_CURRENT);
    desc.SetStageSamplerType(1, CKFF_SAMPLER_CUBE);
    desc.SetStageSamplerType(2, CKFF_SAMPLER_VOLUME);
    desc.SetStageSamplerType(3, CKFF_SAMPLER_CUBE);
    desc.SetStageSamplerType(4, CKFF_SAMPLER_2D);
    desc.SetStageSamplerType(5, CKFF_SAMPLER_VOLUME);

    const CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0x3Fu);
    const CKFFSamplerLayoutPlan plan = CKFFBuildSamplerLayoutPlan(key);
    const CKFFFragmentProgram program = CKFFBuildFragmentProgram(key, plan);
    TestCheck(!key.Stages[1].HasTexture && key.Stages[1].SamplerType == CKFF_SAMPLER_2D,
              "A non-sampling stage must normalize to a 2D sampler type");
    TestCheck(plan.Stages[0].Ordinal == 0 && plan.Stages[3].Ordinal == 1,
              "Cube ordinals must count only earlier sampling cube stages");
    TestCheck(plan.Stages[2].Ordinal == 0 && plan.Stages[5].Ordinal == 1,
              "Volume ordinals must count only earlier sampling volume stages");
    TestCheck(plan.Stages[4].Ordinal == 4,
              "The wide-2D layout preserves logical 2D stage slots");
    TestCheck(plan.Stages[3].NativeSlot == 9 &&
                  plan.Stages[5].NativeSlot == 13,
              "Type ordinals must map onto the cube 8..11 and volume 12..15 slot blocks");
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        TestCheck(program.GetSamplerOrdinal(stage) == plan.Stages[stage].Ordinal,
                  "Fragment program sampler ordinals must match the binding layout plan");
    }

    CKFFFSStateDesc comparisonDesc;
    for (CKDWORD stage = 0; stage < 4; ++stage) {
        comparisonDesc.SetStageColorOp(stage, CKRST_TOP_SELECTARG1);
        comparisonDesc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        comparisonDesc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
        comparisonDesc.SetStageAlphaArg1(stage, CKRST_TA_TEXTURE);
    }
    comparisonDesc.SetStageSamplerType(0, CKFF_SAMPLER_2D);
    comparisonDesc.SetStageSamplerType(1, CKFF_SAMPLER_DEPTH);
    comparisonDesc.SetStageSamplerCompareFunc(1, CKRST_COMPARE_LEQUAL);
    comparisonDesc.SetStageSamplerType(2, CKFF_SAMPLER_DEPTH);
    comparisonDesc.SetStageSamplerType(3, CKFF_SAMPLER_DEPTH);
    comparisonDesc.SetStageSamplerCompareFunc(3, CKRST_COMPARE_GREATER);
    const CKFFShaderKeyFS comparisonKey =
        CKFFBuildShaderKeyFS(comparisonDesc, 0x0fu);
    const CKFFSamplerLayoutPlan comparisonPlan =
        CKFFBuildSamplerLayoutPlan(comparisonKey);
    TestCheck(comparisonPlan.CompareSamplerCount == 2 &&
                  comparisonPlan.Stages[1].Ordinal == 0 &&
                  comparisonPlan.Stages[3].Ordinal == 1 &&
                  comparisonPlan.Stages[0].Ordinal == 2 &&
                  comparisonPlan.Stages[2].Ordinal == 3,
              "Comparison depth samplers must precede ordinary 2D resources");
}

void SamplerLayoutsCoverAllEightStages() {
    CKFFFSStateDesc desc;
    for (CKDWORD stage = 0; stage < 6; ++stage) {
        desc.SetStageColorOp(stage, CKRST_TOP_MODULATE);
        desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageColorArg2(stage, CKRST_TA_CURRENT);
        desc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageAlphaArg1(stage, CKRST_TA_CURRENT);
        desc.SetStageSamplerType(stage, CKFF_SAMPLER_CUBE);
    }
    desc.SetStageSamplerType(5, CKFF_SAMPLER_VOLUME);

    const CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0x3Fu);
    const CKFFSamplerLayoutPlan plan = CKFFBuildSamplerLayoutPlan(key);
    for (CKDWORD stage = 0; stage < 5; ++stage) {
        TestCheck(key.Stages[stage].HasTexture && key.Stages[stage].SamplerType == CKFF_SAMPLER_CUBE &&
                      plan.Stages[stage].Ordinal == stage,
                  "Every cube stage must keep its cube sampler");
    }
    TestCheck(key.Stages[5].HasTexture && key.Stages[5].SamplerType == CKFF_SAMPLER_VOLUME &&
                  plan.Stages[5].Ordinal == 0,
              "The mixed volume stage remains bound");
    TestCheck(plan.Layout == CKFF_SAMPLER_LAYOUT_WIDE_CUBE &&
                  CKFFSamplerSlot(CKFF_SAMPLER_CUBE, 4,
                                  CKFF_SAMPLER_LAYOUT_WIDE_CUBE) == 8 &&
                  CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, 0,
                                  CKFF_SAMPLER_LAYOUT_WIDE_CUBE) == 12,
              "Five cube stages select the exact wide-cube layout");
    TestCheck(key.LastActiveTextureStage == 5,
              "Sampler layout selection must not truncate the active stage chain");

    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    CKDWORD textures[CKFF_MAX_TEXTURE_STAGES] = {1, 2, 3, 4, 5, 6, 0, 0};
    CKDWORD textureFlags[CKFF_MAX_TEXTURE_STAGES] = {};
    for (int stage = 0; stage < 6; ++stage) {
        stages[stage][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
        stages[stage][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
        stages[stage][CKRST_TSS_ARG2] = CKRST_TA_CURRENT;
        textureFlags[stage] = CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP;
    }
    CKFFStageParamsUniform params;
    CKFFPackStageParams(stages, textures, textureFlags, 6, params);
    TestCheck(params.Values[CKFFStageParamIndex(3, CKFF_STAGE_PARAM_COORD)][2] == 1.0f &&
                  params.Values[CKFFStageParamIndex(4, CKFF_STAGE_PARAM_COORD)][2] == 1.0f &&
                  params.Values[CKFFStageParamIndex(5, CKFF_STAGE_PARAM_COORD)][2] == 1.0f,
              "Stage params keep every texture visible to the shader");

    CKFFFSStateDesc volumeDesc = desc;
    for (CKDWORD stage = 0; stage < 6; ++stage)
        volumeDesc.SetStageSamplerType(stage, stage < 5 ? CKFF_SAMPLER_VOLUME
                                                        : CKFF_SAMPLER_CUBE);
    const CKFFShaderKeyFS volumeKey = CKFFBuildShaderKeyFS(volumeDesc, 0x3fu);
    const CKFFSamplerLayoutPlan volumePlan =
        CKFFBuildSamplerLayoutPlan(volumeKey);
    TestCheck(volumePlan.Layout == CKFF_SAMPLER_LAYOUT_WIDE_VOLUME &&
                  CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, 4,
                                  CKFF_SAMPLER_LAYOUT_WIDE_VOLUME) == 12 &&
                  CKFFSamplerSlot(CKFF_SAMPLER_CUBE, 0,
                                  CKFF_SAMPLER_LAYOUT_WIDE_VOLUME) == 4,
              "Five volume stages select the exact wide-volume layout");

    for (CKDWORD twoDCount = 0; twoDCount <= CKFF_MAX_TEXTURE_STAGES; ++twoDCount) {
        for (CKDWORD cubeCount = 0;
             cubeCount + twoDCount <= CKFF_MAX_TEXTURE_STAGES; ++cubeCount) {
            const CKDWORD volumeCount = CKFF_MAX_TEXTURE_STAGES -
                                        twoDCount - cubeCount;
            CKFFFSStateDesc allStages;
            CKDWORD stage = 0;
            for (; stage < twoDCount; ++stage) {
                allStages.SetStageColorOp(stage, CKRST_TOP_MODULATE);
                allStages.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
                allStages.SetStageSamplerType(stage, CKFF_SAMPLER_2D);
            }
            for (CKDWORD end = stage + cubeCount; stage < end; ++stage) {
                allStages.SetStageColorOp(stage, CKRST_TOP_MODULATE);
                allStages.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
                allStages.SetStageSamplerType(stage, CKFF_SAMPLER_CUBE);
            }
            for (; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
                allStages.SetStageColorOp(stage, CKRST_TOP_MODULATE);
                allStages.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
                allStages.SetStageSamplerType(stage, CKFF_SAMPLER_VOLUME);
            }
            const CKFFShaderKeyFS allKey = CKFFBuildShaderKeyFS(allStages, 0xffu);
            const CKFFSamplerLayout allLayout =
                CKFFBuildSamplerLayoutPlan(allKey).Layout;
            TestCheck(twoDCount <= CKFFSamplerTypeSlotCount(CKFF_SAMPLER_2D, allLayout) &&
                          cubeCount <= CKFFSamplerTypeSlotCount(CKFF_SAMPLER_CUBE, allLayout) &&
                          volumeCount <= CKFFSamplerTypeSlotCount(CKFF_SAMPLER_VOLUME, allLayout),
                      "Every eight-stage dimension count has sufficient native slots");
        }
    }
}

void SamplerLayoutPlanExhaustsEightStageTypeCombinations() {
    static const CKDWORD kCombinationCount = 390625u; // 5^8
    CKDWORD checkedStages = 0;
    for (CKDWORD combination = 0; combination < kCombinationCount;
         ++combination) {
        CKFFShaderKeyFS key;
        CKDWORD code = combination;
        CKDWORD cubeCount = 0;
        CKDWORD volumeCount = 0;
        CKDWORD comparisonCount = 0;
        for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
            const CKDWORD kind = code % 5u;
            code /= 5u;
            CKFFShaderKeyFSStage &stageKey = key.Stages[stage];
            stageKey.HasTexture = true;
            if (kind == 1u) {
                stageKey.SamplerType = CKFF_SAMPLER_CUBE;
                ++cubeCount;
            } else if (kind == 2u) {
                stageKey.SamplerType = CKFF_SAMPLER_VOLUME;
                ++volumeCount;
            } else if (kind >= 3u) {
                stageKey.SamplerType = CKFF_SAMPLER_DEPTH;
                if (kind == 4u) {
                    stageKey.SamplerCompareFunc = CKRST_COMPARE_LEQUAL;
                    ++comparisonCount;
                }
            } else {
                stageKey.SamplerType = CKFF_SAMPLER_2D;
            }
        }

        const CKFFSamplerLayout expectedLayout =
            cubeCount > CKFF_NARROW_SAMPLER_COUNT
                ? CKFF_SAMPLER_LAYOUT_WIDE_CUBE
                : volumeCount > CKFF_NARROW_SAMPLER_COUNT
                      ? CKFF_SAMPLER_LAYOUT_WIDE_VOLUME
                      : CKFF_SAMPLER_LAYOUT_WIDE_2D;
        const CKFFSamplerLayoutPlan plan =
            CKFFBuildSamplerLayoutPlan(key);
        TestCheck(plan.Layout == expectedLayout &&
                      plan.CompareSamplerCount == comparisonCount,
                  "Every sampler combination must resolve layout and compare count");

        CKDWORD comparisonOrdinal = 0;
        CKDWORD ordinaryOrdinal = comparisonCount;
        CKDWORD cubeOrdinal = 0;
        CKDWORD volumeOrdinal = 0;
        for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
            const CKFFShaderKeyFSStage &stageKey = key.Stages[stage];
            CKDWORD expectedOrdinal = 0;
            if (stageKey.SamplerType == CKFF_SAMPLER_CUBE) {
                expectedOrdinal = cubeOrdinal++;
            } else if (stageKey.SamplerType == CKFF_SAMPLER_VOLUME) {
                expectedOrdinal = volumeOrdinal++;
            } else if (stageKey.SamplerType == CKFF_SAMPLER_DEPTH &&
                       stageKey.SamplerCompareFunc != CKRST_COMPARE_NONE) {
                expectedOrdinal = comparisonOrdinal++;
            } else if (comparisonCount == 0 &&
                       expectedLayout == CKFF_SAMPLER_LAYOUT_WIDE_2D) {
                expectedOrdinal = stage;
            } else {
                expectedOrdinal = ordinaryOrdinal++;
            }
            const CKDWORD expectedNativeSlot = CKFFSamplerSlot(
                stageKey.SamplerType, expectedOrdinal, expectedLayout);
            TestCheck(plan.Stages[stage].Ordinal == expectedOrdinal &&
                          plan.Stages[stage].NativeSlot == expectedNativeSlot,
                      "Every sampler combination must resolve ordinal and native slot");
            ++checkedStages;
        }
    }
    TestCheck(checkedStages == kCombinationCount * CKFF_MAX_TEXTURE_STAGES,
              "Every eight-stage sampler type combination must be checked");
}

void TextureStageCompareFuncReachesSamplerDesc() {
    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    CKSamplerDesc sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.CompareFunc == CKRST_COMPARE_NONE,
              "Texture stage compare func must default to NONE");

    stages[0][CKRST_TSS_COMPAREFUNC] = CKRST_COMPARE_GREATER;
    sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.CompareFunc == CKRST_COMPARE_GREATER,
              "Texture stage compare func must reach native comparison samplers");

    stages[0][CKRST_TSS_COMPAREFUNC] = CKRST_COMPARE_ALWAYS + 1;
    sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.CompareFunc == CKRST_COMPARE_NONE,
              "Invalid texture stage compare funcs must not reach native samplers");
}

void TextureFilterLinearDoesNotRequestMipSampling() {
    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};

    stages[0][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_LINEAR;
    CKSamplerDesc sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.MinFilter == CKRST_FILTER_LINEAR &&
                  sampler.MipFilter == CKRST_FILTER_NONE,
              "VXTEXTUREFILTER_LINEAR must mean bilinear base-level sampling");

    stages[0][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_NEAREST;
    sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.MinFilter == CKRST_FILTER_NEAREST &&
                  sampler.MipFilter == CKRST_FILTER_NONE,
              "VXTEXTUREFILTER_NEAREST must not request mip sampling");

    stages[0][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_LINEARMIPLINEAR;
    sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.MinFilter == CKRST_FILTER_LINEAR &&
                  sampler.MipFilter == CKRST_FILTER_LINEAR,
              "VXTEXTUREFILTER_LINEARMIPLINEAR must keep trilinear mip sampling");
}

void DisableMipmapsForcesBaseLevelSampling() {
    CKFixedFunctionPipeline ffp;
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEARMIPLINEAR);
    ffp.SetRenderOptions(FALSE, TRUE);

    CKSamplerDesc sampler = ffp.BuildSamplerDesc(0);
    TestCheck(sampler.MinFilter == CKRST_FILTER_LINEAR &&
                  sampler.MipFilter == CKRST_FILTER_NONE,
              "DisableMipmap must disable mip sampling without changing minification filtering");

    ffp.SetRenderOptions(TRUE, TRUE);
    sampler = ffp.BuildSamplerDesc(0);
    TestCheck(sampler.MinFilter == CKRST_FILTER_NEAREST &&
                  sampler.MagFilter == CKRST_FILTER_NEAREST &&
                  sampler.MipFilter == CKRST_FILTER_NONE,
              "DisableMipmap must still disable mip sampling when texture filtering is disabled");
}

void TextureBindingMaskSplitsNullTextureShaderKey() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);

    CKFFShaderKeyFS nullKey = CKFFBuildShaderKeyFS(desc, 0);
    CKFFShaderKeyFS boundKey = CKFFBuildShaderKeyFS(desc, 1u << 0);

    TestCheck(nullKey != boundKey,
              "Texture binding mask must split null texture and bound texture shader keys");
}

void TextureBindingMaskIgnoresStagesWithoutTextureArgs() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(0, CKRST_TA_DIFFUSE);
    desc.SetStageColorArg2(0, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(0, CKRST_TA_DIFFUSE);
    desc.SetStageAlphaArg2(0, CKRST_TA_TEXTURE);

    CKFFShaderKeyFS nullKey = CKFFBuildShaderKeyFS(desc, 0);
    CKFFShaderKeyFS boundKey = CKFFBuildShaderKeyFS(desc, 1u << 0);

    TestCheck(nullKey == boundKey,
              "Texture binding mask must not split keys when active ops do not use texture args");
}

void UnusedSamplerStateDoesNotSplitShaderKey() {
    CKFFFSStateDesc baselineDesc;
    baselineDesc.SetStageColorOp(0, CKRST_TOP_SELECTARG1);
    baselineDesc.SetStageColorArg1(0, CKRST_TA_DIFFUSE);
    baselineDesc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    baselineDesc.SetStageAlphaArg1(0, CKRST_TA_DIFFUSE);

    CKFFFSStateDesc samplerDesc = baselineDesc;
    samplerDesc.SetStageProjectedSampler(0, true);
    samplerDesc.SetStageSamplerType(0, CKFF_SAMPLER_VOLUME);
    samplerDesc.SetStageSamplerCompareFunc(0, CKRST_COMPARE_LESS);
    samplerDesc.SetStageMirrorOnceMask(0, 7);

    const CKFFShaderKeyFS baselineKey = CKFFBuildShaderKeyFS(baselineDesc, 1u << 0);
    const CKFFShaderKeyFS samplerKey = CKFFBuildShaderKeyFS(samplerDesc, 1u << 0);

    TestCheck(baselineKey == samplerKey,
              "Sampler state must not split shader keys when the stage does not sample a texture");
    TestCheck(!samplerKey.Stages[0].ProjectedSampler &&
                  samplerKey.Stages[0].SamplerType == CKFF_SAMPLER_2D &&
                  samplerKey.Stages[0].SamplerCompareFunc == CKRST_COMPARE_NONE &&
                  samplerKey.Stages[0].MirrorOnceMask == 0,
              "Unused sampler fields must normalize to their neutral shader-key values");
}

void PremodulateAddsImplicitNextStageTextureDependency() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_PREMODULATE);
    desc.SetStageColorArg1(0, CKRST_TA_DIFFUSE);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(0, CKRST_TA_DIFFUSE);
    desc.SetStageColorOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(1, CKRST_TA_CURRENT);
    desc.SetStageAlphaOp(1, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(1, CKRST_TA_CURRENT);

    const CKFFShaderKeyFS nullKey = CKFFBuildShaderKeyFS(desc, 0);
    const CKFFShaderKeyFS boundKey = CKFFBuildShaderKeyFS(desc, 1u << 1);

    TestCheck(!nullKey.Stages[1].HasTexture && boundKey.Stages[1].HasTexture,
              "PREMODULATE must make next-stage CURRENT depend on the next-stage texture");
    TestCheck(nullKey != boundKey,
              "PREMODULATE implicit texture binding must split specialized shader keys");
}

void TextureBindingMaskIgnoresInactiveStages() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_DISABLE);
    desc.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageAlphaOp(0, CKRST_TOP_DISABLE);
    desc.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);

    CKFFShaderKeyFS nullKey = CKFFBuildShaderKeyFS(desc, 0);
    CKFFShaderKeyFS boundKey = CKFFBuildShaderKeyFS(desc, 1u << 0);

    TestCheck(nullKey == boundKey,
              "Texture binding mask must not split disabled stage keys");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Bump env uniforms pack each stage independently",
              &BumpEnvUniformsPackEachStageIndependently);
    tests.Run("Texture arg modifier repack round trips both modifier bits",
              &TextureArgModifierRepackRoundTripsBothModifierBits);
    tests.Run("Shader ABI constants are consistent",
              &ShaderABIConstantsAreConsistent);
    tests.Run("Sampler shader state resolves backend responsibilities",
              &SamplerShaderStateResolvesBackendResponsibilities);
    tests.Run("Native exact sampling uses final binding state",
              &NativeExactSamplingUsesFinalBindingState);
    tests.Run("Texture binding cache invalidates every binding dependency",
              &TextureBindingCacheInvalidatesEveryBindingDependency);
    tests.Run("Stage params pack through ABI indices",
              &StageParamsPackThroughABIIndices);
    tests.Run("MIRRORONCE address modes pack into stage params",
              &MirrorOnceAddressModesPackIntoStageParams);
    tests.Run("MIRRORONCE sampler desc falls back to clamp",
              &MirrorOnceSamplerDescFallsBackToClamp);
    tests.Run("Fragment program packs sampler ordinals for every stage",
              &FragmentProgramPacksSamplerOrdinalsEveryStage);
    tests.Run("Last active texture stage fragment program round trips",
              &LastActiveTextureStageFragmentProgramRoundTrips);
    tests.Run("Fragment program uniform carries lanes as exact integers",
              &FragmentProgramUniformCarriesLanesAsExactIntegers);
    tests.Run("PREMODULATE coverage is exact",
              &PremodulateCoverageIsExact);
    tests.Run("BUMPENVMAP always creates texture dependency",
              &BumpMapAlwaysCreatesTextureDependency);
    tests.Run("Texture combiner preserves TEMP destination",
              &TextureCombinerPreservesTempDestination);
    tests.Run("Alpha ref uses low byte only",
              &AlphaRefUsesLowByteOnly);
    tests.Run("Alpha ref byte uses low byte only",
              &AlphaRefByteUsesLowByteOnly);
    tests.Run("Alpha func precision pack keeps compare func",
              &AlphaFuncPrecisionPackKeepsCompareFunc);
    tests.Run("Alpha func precision pack supports dxvk precision codes",
              &AlphaFuncPrecisionPackSupportsDxvkPrecisionCodes);
    tests.Run("Alpha test precision for legacy formats defaults to eight-bit",
              &AlphaTestPrecisionForLegacyFormatsDefaultsToEightBit);
    tests.Run("Alpha test precision follows render target alpha mask",
              &AlphaTestPrecisionFollowsRenderTargetAlphaMask);
    tests.Run("Vertex blend resolver matches dxvk weight counts",
              &VertexBlendResolverMatchesDxvkWeightCounts);
    tests.Run("Vertex blend resolver handles missing indexed input and POSITIONT",
              &VertexBlendResolverHandlesMissingIndexedInputAndPositionT);
    tests.Run("DP weight flags add blend layout flags",
              &DPWeightFlagsAddBlendLayoutFlags);
    tests.Run("Sampler types pack into fragment program",
              &SamplerTypesPackIntoFragmentProgram);
    tests.Run("Volume sampler and compare func pack into fragment program",
              &VolumeSamplerAndCompareFuncPackIntoFragmentProgram);
    tests.Run("Volume sampler mask can be derived from shader key",
              &VolumeSamplerMaskCanBeDerivedFromShaderKey);
    tests.Run("Sampler ordinal counts only sampling stages of the same type",
              &SamplerOrdinalCountsOnlySamplingStagesOfTheSameType);
    tests.Run("Sampler layouts cover all eight stages",
              &SamplerLayoutsCoverAllEightStages);
    tests.Run("Sampler layout plan exhausts eight-stage type combinations",
              &SamplerLayoutPlanExhaustsEightStageTypeCombinations);
    tests.Run("Texture stage compare func reaches sampler desc",
              &TextureStageCompareFuncReachesSamplerDesc);
    tests.Run("Texture filter linear does not request mip sampling",
              &TextureFilterLinearDoesNotRequestMipSampling);
    tests.Run("DisableMipmap forces base-level sampling",
              &DisableMipmapsForcesBaseLevelSampling);
    tests.Run("Texture binding mask splits null texture shader key",
              &TextureBindingMaskSplitsNullTextureShaderKey);
    tests.Run("Texture binding mask ignores stages without texture args",
              &TextureBindingMaskIgnoresStagesWithoutTextureArgs);
    tests.Run("Unused sampler state does not split shader key",
              &UnusedSamplerStateDoesNotSplitShaderKey);
    tests.Run("PREMODULATE adds implicit next-stage texture dependency",
              &PremodulateAddsImplicitNextStageTextureDependency);
    tests.Run("Texture binding mask ignores inactive stages",
              &TextureBindingMaskIgnoresInactiveStages);
    return tests.ExitCode();
}
