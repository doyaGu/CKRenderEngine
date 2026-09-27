#include <stdio.h>

#include "CKFFStageState.h"
#include "CKFFShaderABI.h"
#include "CKFFShaderKey.h"
#include "CKFFUniformState.h"
#include "CKFixedFunctionPipeline.h"
#include "CKVertexLayoutCache.h"
#include "TestTriangleMultiset.h"

#include <cstring>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

std::string ReadTextFile(const char *path) {
    auto read = [](const std::string &candidate) {
        std::ifstream file(candidate.c_str());
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    };

    std::string contents = read(path);
    if (!contents.empty())
        return contents;

    const char *sourcePrefix = "Source/RenderEngine/";
    const size_t sourcePrefixLen = std::strlen(sourcePrefix);
    if (std::strncmp(path, sourcePrefix, sourcePrefixLen) == 0) {
        const char *relativePath = path + sourcePrefixLen;

        std::string thisFile = __FILE__;
        std::string::size_type slash = thisFile.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string testsDir = thisFile.substr(0, slash);
            slash = testsDir.find_last_of("/\\");
            if (slash != std::string::npos) {
                contents = read(testsDir.substr(0, slash + 1) + relativePath);
                if (!contents.empty())
                    return contents;
            }
        }

        contents = read(std::string("../") + path);
        if (!contents.empty())
            return contents;

        contents = read(relativePath);
        if (!contents.empty())
            return contents;

        contents = read(std::string("../../") + relativePath);
        if (!contents.empty())
            return contents;

        contents = read(std::string("../../../") + relativePath);
    }

    return contents;
}

std::string ReadFragmentShaderSources() {
    static const char *const paths[] = {
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc",
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_common.sc",
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_common.sc",
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_2d.sc",
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_cube.sc",
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_volume.sc",
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_depth.sc",
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_texture_ops.sc",
    };
    std::string contents;
    for (const char *path : paths) {
        contents += ReadTextFile(path);
        contents += '\n';
    }
    return contents;
}

size_t CountSubstring(const std::string &contents, const char *needle) {
    size_t count = 0;
    size_t offset = 0;
    while ((offset = contents.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += std::strlen(needle);
    }
    return count;
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

void ShaderABIConstantsMatchShaderUniformDeclarations() {
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

    const std::string fs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc");
    const std::string vs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_3d.sc");
    TestCheck(fs.find("uniform vec4 u_ffDrawParams[20]") != std::string::npos &&
                  vs.find("uniform vec4 u_ffDrawParams[20]") != std::string::npos,
              "Shader sources must declare u_ffDrawParams with the ABI count");
    TestCheck(fs.find("uniform vec4 u_stageParams[16]") != std::string::npos &&
                  vs.find("uniform vec4 u_stageParams[16]") != std::string::npos,
              "Shader sources must declare u_stageParams with the ABI count");
    TestCheck(fs.find("uniform vec4 u_ffProgram[5]") != std::string::npos,
              "Fragment shader must declare u_ffProgram with the fragment program ABI count");
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

void SamplerShaderStateShaderHeaderMatchesCppABI() {
    const std::string generated = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_shader_state.sh");
    const std::string generator = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shader_abi_codegen.py");
    const std::string fragment = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc");
    const std::string sampling = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_common.sc");
    TestCheck(!generated.empty() && !generator.empty() && !fragment.empty() &&
                  !sampling.empty(),
              "Sampler shader ABI sources must be readable");

    char line[160];
#define CKFF_CHECK_SAMPLER_SHADER_DEFINE(name, format) \
    snprintf(line, sizeof(line), "#define " #name " " format, (unsigned)name); \
    TestCheck(generated.find(line) != std::string::npos, \
              "Generated sampler shader state must match CKFFShaderABI.h")
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT, "%u");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MIN_MIP_MASK, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT, "%u");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_ANISOTROPY_MASK, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT, "%u");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_BORDER_AXIS_MASK, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MIN_FILTER_LINEAR, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MAG_FILTER_LINEAR, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MANUAL_LOD, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MANUAL_BORDER, "0x%08x");
    CKFF_CHECK_SAMPLER_SHADER_DEFINE(CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE, "0x%08x");
#undef CKFF_CHECK_SAMPLER_SHADER_DEFINE

    TestCheck(generator.find("CKFFSamplerShaderStateABI") != std::string::npos &&
                  fragment.find("#include \"ff_sampler_shader_state.sh\"") != std::string::npos &&
                  sampling.find("CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT") != std::string::npos &&
                  sampling.find("packedSamplerLod") == std::string::npos &&
                  sampling.find("(samplerState >> 5) & 31") == std::string::npos,
              "Shader code must consume generated sampler ABI names without packed-state literals");
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

void ShaderSourcesDeclarePortableFlatAndClipSpaceConventions() {
    const std::string varying = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/varying.def.sc");
    const std::string compiler = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKBgfxRasterizer/shaders/compile_shaders.py");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_positiont.sc");

    TestCheck(varying.find("flat vec4 v_flatColor0") != std::string::npos &&
                  varying.find("flat vec4 v_flatColor1") != std::string::npos,
              "All shader backends must compile flat colors as non-interpolated varyings");
    TestCheck(compiler.find("varying_no_flat_color.def.sc") == std::string::npos &&
                  compiler.find("CKFF_NDC_MINUS_ONE_TO_ONE=1") != std::string::npos,
              "Shader generation must use one flat-varying ABI and mark the GLSL depth convention");
    TestCheck(vs3d.find("position.z = position.z * 2.0 - position.w") != std::string::npos &&
                  vsPositionT.find("position.z = position.z * 2.0 - position.w") != std::string::npos,
              "Both 3D and POSITIONT shaders must convert D3D clip depth for desktop OpenGL");
    TestCheck(vsPositionT.find("(a_position.x + 0.5) * u_viewport.x") != std::string::npos &&
                  vsPositionT.find("(a_position.y + 0.5) * u_viewport.y") != std::string::npos,
              "POSITIONT vertices must apply the legacy half-pixel center exactly once");
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

void MirrorOnceShaderSourceAppliesOnlyTo2DAndVolume() {
    const std::string fs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc");
    const std::string common = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_common.sc");
    const std::string sampling = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_common.sc") +
        ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_depth.sc");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_positiont.sc");

    TestCheck(!fs.empty() && !common.empty() && !sampling.empty() &&
                  !vs3d.empty() && !vsPositionT.empty(),
              "FFP shader sources must be readable");
    TestCheck(common.find("int MirrorOnceMask;") != std::string::npos &&
                  common.find("params.MirrorOnceMask = (flags >> 9) & 7;") != std::string::npos &&
                  common.find("MIRRORONCE_SAMPLER_MASK") == std::string::npos,
              "Fragment common shader must read MIRRORONCE masks only from texture-transform flags");
    TestCheck(sampling.find("vec4 applyMirrorOnceCoord") != std::string::npos &&
                  sampling.find("if (samplerType == 1 || mirrorOnceMask == 0) return coord") != std::string::npos &&
                  sampling.find("samplerType == 3 && (mirrorOnceMask & 4)") != std::string::npos,
              "Fragment shader must remap 2D/volume coordinates while leaving cube coordinates untouched");
    TestCheck(sampling.find("coord = applyMirrorOnceCoord(coord, mirrorOnceMask, samplerType);") != std::string::npos,
              "MIRRORONCE remap must happen inside texture sampling after projected coordinate preparation");
    TestCheck(vs3d.find("int count = flags & 0xff;") != std::string::npos &&
                  vsPositionT.find("int count = flags & 0xff;") != std::string::npos,
              "Vertex shaders must keep texture-transform component count isolated from MIRRORONCE high bits");
}

void TextureCombinerOpFormulasStayDxvkCompatible() {
    const std::string fs = ReadFragmentShaderSources();

    TestCheck(!fs.empty(),
              "FFP fragment shader source must be readable from the test working directory");
    TestCheck(fs.find("if (op == 5) return clamp(a * b * 2.0, 0.0, 1.0)") != std::string::npos &&
                  fs.find("if (op == 6) return clamp(a * b * 4.0, 0.0, 1.0)") != std::string::npos,
              "MODULATE2X and MODULATE4X must saturate before feeding the next texture stage");
    TestCheck(fs.find("if (op == 15) return clamp(a + b * (1.0 - textureColor.a), 0.0, 1.0)") != std::string::npos,
              "BLENDTEXTUREALPHAPM must stay texture-alpha premultiplied add");
    TestCheck(fs.find("if (op == 18) return clamp(a + vec4_splat(a.a) * b, 0.0, 1.0)") != std::string::npos &&
                  fs.find("if (op == 19) return clamp(a * b + vec4_splat(a.a), 0.0, 1.0)") != std::string::npos &&
                  fs.find("if (op == 20) return clamp(a + (1.0 - a.a) * b, 0.0, 1.0)") != std::string::npos &&
                  fs.find("if (op == 21) return clamp((vec4_splat(1.0) - a) * b + vec4_splat(a.a), 0.0, 1.0)") != std::string::npos,
              "MODULATE alpha/color add texture ops must keep their DXVK-compatible formulas");
    TestCheck(fs.find("dot(a.rgb - 0.5, b.rgb - 0.5) * 4.0") != std::string::npos &&
                  fs.find("if (op == 25) return clamp(a * b + c, 0.0, 1.0)") != std::string::npos &&
                  fs.find("if (op == 26) return clamp(c * a + (vec4_splat(1.0) - c) * b, 0.0, 1.0)") != std::string::npos,
              "DOTPRODUCT3, MULTIPLYADD, and LERP formulas must remain covered");
    TestCheck(fs.find("if (op == 17) return a") != std::string::npos &&
                  fs.find("current * textureColor") != std::string::npos &&
                  fs.find("previousColorOp == 17") != std::string::npos &&
                  fs.find("previousAlphaOp == 17") != std::string::npos,
              "PREMODULATE must feed Arg1 through and premultiply next-stage CURRENT arguments");
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

void FragmentProgramLayoutShaderHeaderMatchesTheDef() {
    const std::string generated = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_fragment_program_layout.sh");
    const std::string common = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_common.sc");
    const std::string script = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKBgfxRasterizer/shaders/compile_shaders.py");
    TestCheck(!generated.empty() && !common.empty() && !script.empty(),
              "Generated fragment-program layout header, fragment common shader and codegen script must be readable");

    char line[160];
    snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_LANE_COUNT %u", (unsigned)CKFF_FRAGMENT_PROGRAM_LANE_COUNT);
    TestCheck(generated.find(line) != std::string::npos,
              "Generated fragment-program layout must define the lane count");
    snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_STAGE_LANE_STRIDE %u", (unsigned)CKFF_FRAGMENT_PROGRAM_STAGE_LANE_STRIDE);
    TestCheck(generated.find(line) != std::string::npos,
              "Generated fragment-program layout must define the stage lane stride");
    snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_GLOBAL_LANE_BASE %u", (unsigned)CKFF_FRAGMENT_PROGRAM_GLOBAL_LANE_BASE);
    TestCheck(generated.find(line) != std::string::npos,
              "Generated fragment-program layout must define the global lane base");

    for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT; ++fieldIndex) {
        const CKFFFragmentProgramFieldDesc &desc = CKFFFragmentProgramStageFieldDesc((CKFFFragmentProgramStageField)fieldIndex);
        snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_STAGE_%s_WORD %u",
                 desc.Name, desc.Layout.Lane);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated fragment-program layout must expose every stage field word");
        snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_STAGE_%s_SHIFT %u",
                 desc.Name, desc.Layout.BitOffset);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated fragment-program layout must expose every stage field shift");
        snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_STAGE_%s_MASK 0x%x",
                 desc.Name, (1u << desc.Layout.BitCount) - 1u);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated fragment-program layout must expose every stage field mask");
    }
    for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT; ++fieldIndex) {
        const CKFFFragmentProgramFieldDesc &desc = CKFFFragmentProgramGlobalFieldDesc((CKFFFragmentProgramGlobalField)fieldIndex);
        snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_%s_LANE %u",
                 desc.Name, desc.Layout.Lane);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated fragment-program layout must expose every global field lane");
        snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_%s_SHIFT %u",
                 desc.Name, desc.Layout.BitOffset);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated fragment-program layout must expose every global field shift");
        snprintf(line, sizeof(line), "#define CKFF_FRAGMENT_PROGRAM_%s_MASK 0x%x",
                 desc.Name, (1u << desc.Layout.BitCount) - 1u);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated fragment-program layout must expose every global field mask");
    }
    TestCheck(common.find("#include \"ff_fragment_program_layout.sh\"") != std::string::npos &&
                  common.find("CKFFTextureStageProgram ckffDecodeTextureStageProgram") != std::string::npos &&
                  common.find("vec4 packedWords = u_ffProgram[stage / 2]") != std::string::npos &&
                  common.find("CKFFGlobalFragmentProgram ckffDecodeGlobalFragmentProgram") != std::string::npos &&
                  common.find("ckffProgramStage_") == std::string::npos &&
                  common.find("floatBitsToUint") == std::string::npos &&
                  common.find("uint(255)") == std::string::npos,
              "Fragment shader must decode each stage pair once through generated shifts and masks");
    TestCheck(script.find("gen-fragment-program-layout") != std::string::npos &&
                  script.find("CKFFFragmentProgramLayout.def") != std::string::npos &&
                  script.find("def validate_fragment_program_layout") != std::string::npos,
              "Shader codegen must generate and validate the fragment-program layout from CKFFFragmentProgramLayout.def");
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

    const std::string fs = ReadFragmentShaderSources();
    TestCheck(fs.find("vec4 stageResult = resultArg == 5 ? temp : current") !=
                  std::string::npos &&
                  fs.find("if (op == 1) return dst") != std::string::npos &&
                  fs.find("if (op == 22 || op == 23) return dst") !=
                  std::string::npos,
              "fragment stages must preserve the selected destination register");
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

void TextureCombinerTempInitializesAlphaToZero() {
    const std::string contents = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc");

    TestCheck(!contents.empty(),
              "FFP fragment shader source must be readable from the test working directory");
    TestCheck(contents.find("vec4 temp = vec4(0.0, 0.0, 0.0, 0.0)") != std::string::npos,
              "FFP TEMP register must initialize all channels to zero");
    TestCheck(contents.find("vec4 temp = vec4(0.0, 0.0, 0.0, 1.0)") == std::string::npos,
              "FFP TEMP alpha must not initialize to one");
}

void DepthTextureCompareUsesSamplerCompareOrdering() {
    const std::string contents = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_depth.sc");

    TestCheck(!contents.empty(),
              "FFP fragment shader source must be readable from the test working directory");
    TestCheck(contents.find("if (func == 1) return ref < depth ? 1.0 : 0.0") != std::string::npos,
              "CKRST_COMPARE_LESS must compare reference against sampled depth");
    TestCheck(contents.find("if (func == 2) return ref <= depth ? 1.0 : 0.0") != std::string::npos,
              "CKRST_COMPARE_LEQUAL must compare reference against sampled depth");
    TestCheck(contents.find("if (func == 3) return ref == depth ? 1.0 : 0.0") != std::string::npos,
              "CKRST_COMPARE_EQUAL must compare reference against sampled depth");
    TestCheck(contents.find("if (func == 4) return ref >= depth ? 1.0 : 0.0") != std::string::npos,
              "CKRST_COMPARE_GEQUAL must compare reference against sampled depth");
    TestCheck(contents.find("if (func == 7) return 0.0") != std::string::npos,
              "CKRST_COMPARE_NEVER must always fail shader depth compares");
    TestCheck(contents.find("if (func == 8) return 1.0") != std::string::npos,
              "CKRST_COMPARE_ALWAYS must always pass shader depth compares");
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

void TweeningInputsAndShaderAreWired() {
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_3d.sc");
    const std::string layout = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/FixedFunction/CKVertexLayoutCache.cpp");
    const std::string transient = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/FixedFunction/CKTransientGeometry.cpp");
    const std::string vertexBuffer = ReadTextFile("Source/RenderEngine/src/CKVertexBuffer.cpp");

    TestCheck(!vs3d.empty() && !layout.empty() && !transient.empty() &&
                  !vertexBuffer.empty(),
              "TWEENING implementation source files must be readable");
    TestCheck(vs3d.find("a_tangent") != std::string::npos &&
                  vs3d.find("a_bitangent") != std::string::npos &&
                  vs3d.find("mix(a_position.xyz, a_tangent.xyz") != std::string::npos &&
                  vs3d.find("u_ffDrawParams[19].x") != std::string::npos,
              "Vertex shader must blend both tween input sets with TWEENFACTOR");
    TestCheck(layout.find("CKFF_VF_TWEENPOSITION") != std::string::npos &&
                  layout.find("CKRST_ATTRIB_TANGENT") != std::string::npos &&
                  transient.find("TweenPositionPtr") != std::string::npos &&
                  transient.find("TweenNormalPtr") != std::string::npos,
              "Vertex layout and transient interleave must carry both tween streams");
    TestCheck(vertexBuffer.find("CKRST_DP_TWEEN") != std::string::npos &&
                  vertexBuffer.find("TweenPositionPtr") != std::string::npos,
              "Managed vertex buffers must allocate tween staging explicitly");
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

void FragmentShaderDeclaresAllExactSamplerLayouts() {
    const std::string fs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc");
    const std::string common = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_common.sc");
    const std::string layout = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_layout.sh");
    const std::string layoutDef = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/CKFFSamplerLayout.def");
    const std::string generator = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shader_abi_codegen.py");
    const std::string nativeCompiler = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKSdlGpuRasterizer/shaders/compile_native_shaders.py");
    const std::string samplers = ReadFragmentShaderSources();
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/vs_ff_positiont.sc");
    TestCheck(!fs.empty() && !common.empty() && !layout.empty() &&
                  !layoutDef.empty() && !generator.empty() &&
                  !nativeCompiler.empty() && !samplers.empty() &&
                  !vs3d.empty() && !vsPositionT.empty(),
              "FFP shader sources must be readable");

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        char decl[64];
        snprintf(decl, sizeof(decl), "SAMPLER2D(s_texture%d, %d);", stage, stage);
        TestCheck(layout.find(decl) != std::string::npos,
                  "Fragment shader must declare one 2D sampler per texture stage on slots 0..7");
    }
    for (int ordinal = 0; ordinal < CKFF_NARROW_SAMPLER_COUNT; ++ordinal) {
        char decl[64];
        snprintf(decl, sizeof(decl), "SAMPLERCUBE(s_textureCube%d, %d);", ordinal,
                 (int)CKFFSamplerSlot(CKFF_SAMPLER_CUBE, ordinal));
        TestCheck(layout.find(decl) != std::string::npos,
                  "Fragment shader must declare the cube samplers on slots 8..11");
    }
    for (int ordinal = 0; ordinal < CKFF_NARROW_SAMPLER_COUNT; ++ordinal) {
        char decl[64];
        snprintf(decl, sizeof(decl), "SAMPLER3D(s_textureVolume%d, %d);", ordinal,
                 (int)CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, ordinal));
        TestCheck(layout.find(decl) != std::string::npos,
                  "Fragment shader must declare the volume samplers on slots 12..15");
    }
    TestCheck(layout.find("SAMPLERCUBE(s_textureCube4, 8);") != std::string::npos &&
                  layout.find("SAMPLERCUBE(s_textureCube7, 11);") != std::string::npos &&
                  layout.find("SAMPLER3D(s_textureVolume4, 12);") != std::string::npos &&
                  layout.find("SAMPLER3D(s_textureVolume7, 15);") != std::string::npos &&
                  layout.find("CKFF_NATIVE_SAMPLER_LAYOUT") != std::string::npos,
              "Fragment shader must declare the wide cube and volume layouts");
    TestCheck(samplers.find("int ckffSamplerOrdinal(int stage, int samplerType)") == std::string::npos &&
                  common.find("program.SamplerOrdinal = (samplerOrdinals >> (stage * 3)) & 7;") != std::string::npos &&
                  samplers.find("int samplerOrdinal") != std::string::npos,
              "Fragment shader must consume CPU-resolved sampler ordinals from the fragment program");

    const CKDWORD expectedCounts[CKFF_SAMPLER_LAYOUT_COUNT][3] = {
        { 8, 4, 4 }, { 4, 8, 4 }, { 4, 4, 8 },
    };
    for (CKDWORD samplerLayout = 0;
         samplerLayout < CKFF_SAMPLER_LAYOUT_COUNT; ++samplerLayout) {
        for (CKDWORD type = 0; type < 3; ++type) {
            const CKDWORD samplerType = type == 1 ? CKFF_SAMPLER_CUBE :
                                        type == 2 ? CKFF_SAMPLER_VOLUME :
                                                    CKFF_SAMPLER_2D;
            TestCheck(CKFFSamplerTypeSlotCount(
                          samplerType, (CKFFSamplerLayout)samplerLayout) ==
                          expectedCounts[samplerLayout][type],
                      "C++ sampler slot helpers must be generated from the layout definition");
        }
    }
    TestCheck(layoutDef.find("CKFF_SAMPLER_LAYOUT(WIDE_2D,     0, 8, 4, 4)") != std::string::npos &&
                  layoutDef.find("CKFF_SAMPLER_LAYOUT(WIDE_CUBE,   1, 4, 8, 4)") != std::string::npos &&
                  layoutDef.find("CKFF_SAMPLER_LAYOUT(WIDE_VOLUME, 2, 4, 4, 8)") != std::string::npos &&
                  generator.find("def sampler_layouts") != std::string::npos &&
                  nativeCompiler.find("SAMPLER_LAYOUTS[sampler_layout].counts") != std::string::npos,
              "C++, generated shader declarations and SDL reflection must share one sampler layout definition");
    TestCheck(layout.find("CKFF_DISPATCH_2D_ORDINARY") != std::string::npos &&
                  layout.find("CKFF_DISPATCH_DEPTH_COMPARE") != std::string::npos &&
                  layout.find("CKFF_DISPATCH_CUBE") != std::string::npos &&
                  layout.find("CKFF_DISPATCH_VOLUME") != std::string::npos &&
                  samplers.find("CKFF_DISPATCH_2D_ORDINARY") != std::string::npos &&
                  samplers.find("CKFF_DISPATCH_DEPTH_COMPARE") != std::string::npos &&
                  samplers.find("CKFF_DISPATCH_CUBE") != std::string::npos &&
                  samplers.find("CKFF_DISPATCH_VOLUME") != std::string::npos,
              "Sampler declaration, ordinal dispatch and size queries must consume the generated layout");
    TestCheck(samplers.find("vec4 CKFFSample2D") != std::string::npos &&
                  samplers.find("vec4 CKFFSampleCube") != std::string::npos &&
                  samplers.find("vec4 CKFFSampleVolume") != std::string::npos &&
                  samplers.find("vec4 CKFFSampleDepth") != std::string::npos &&
                  CountSubstring(samplers, "vec4 applyOp(") == 1,
              "Fragment shader responsibilities must be split with one authoritative texture combiner");

    const std::string *sources[] = {&samplers, &vs3d, &vsPositionT};
    const char *retiredMacros[] = {
        "CKFF_FULL_SPECIALIZED", "CKFF_STATIC_SAMPLER_LAYOUT", "CKFF_VOLUME_SAMPLER_LAYOUT",
        "CKFF_MIXED_SAMPLER_LAYOUT", "CKFF_VS_INSTANCED", "CKFF_VS_ACTIVE_TEXCOORD_COUNT",
        "CKFF_FS_ACTIVE_STAGE_COUNT", "ckffProgramIsOptimized", "u_ffDrawParams[8].y",
    };
    for (const std::string *source : sources) {
        for (const char *macro : retiredMacros) {
            TestCheck(source->find(macro) == std::string::npos,
                      "Shader sources must not keep variant-selection macros of the retired shader routes");
        }
    }
    TestCheck(vs3d.find("CKFF_VS_CLIP_DISTANCE") != std::string::npos &&
                  vsPositionT.find("CKFF_VS_CLIP_DISTANCE") != std::string::npos,
              "Vertex shaders keep the clip-distance variant switch");
}

void TextureDerivativesStayInTypedSamplingPaths() {
    const std::string stage = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/fs_ff_stage.sc");
    const std::string twoD = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_2d.sc");
    const std::string cube = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_cube.sc");
    const std::string volume = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_volume.sc");
    const std::string depth = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_depth.sc");
    const std::string common = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKFFPLib/ShaderModel/shaders/ff_sampler_common.sc");
    const std::string nativeSampling = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKSdlGpuRasterizer/shaders/native_sampling.hlsli");
    TestCheck(!stage.empty() && !twoD.empty() && !cube.empty() &&
                  !volume.empty() && !depth.empty() && !common.empty() &&
                  !nativeSampling.empty(),
              "Typed sampler shader sources must be readable");

    const std::string::size_type textureDispatch =
        depth.find("vec4 CKFFSampleTexture(");
    const std::string dispatch = textureDispatch == std::string::npos
        ? std::string()
        : depth.substr(textureDispatch);
    TestCheck(stage.find("dFdx(") == std::string::npos &&
                  stage.find("dFdy(") == std::string::npos &&
                  dispatch.find("dFdx(") == std::string::npos &&
                  dispatch.find("dFdy(") == std::string::npos,
              "Texture dispatch must not eagerly evaluate derivatives");

    TestCheck(twoD.find("vec2 originalDx") != std::string::npos &&
                  twoD.find("dFdx(originalUv)") != std::string::npos &&
                  twoD.find("dFdy(originalUv)") != std::string::npos &&
                  twoD.find("if (sampleProgram.RequiresExplicitGradient)") != std::string::npos &&
                  CountSubstring(twoD, "dFdx(") == 1 &&
                  CountSubstring(twoD, "dFdy(") == 1,
              "2D sampling must compute one vec2 footprint only when required");
    TestCheck(depth.find("vec2 originalDx") != std::string::npos &&
                  depth.find("dFdx(originalUv)") != std::string::npos &&
                  depth.find("dFdy(originalUv)") != std::string::npos &&
                  depth.find("if (sampleProgram.RequiresExplicitGradient)") != std::string::npos &&
                  CountSubstring(depth, "dFdx(") == 1 &&
                  CountSubstring(depth, "dFdy(") == 1,
              "Depth sampling must compute one vec2 footprint only when required");
    TestCheck(cube.find("vec3 originalDx") != std::string::npos &&
                  cube.find("dFdx(originalCoord)") != std::string::npos &&
                  cube.find("dFdy(originalCoord)") != std::string::npos &&
                  cube.find("if (sampleProgram.RequiresExplicitGradient)") != std::string::npos &&
                  CountSubstring(cube, "dFdx(") == 1 &&
                  CountSubstring(cube, "dFdy(") == 1,
              "Cube sampling must compute one vec3 footprint only when required");
    TestCheck(volume.find("vec3 originalDx") != std::string::npos &&
                  volume.find("dFdx(originalCoord)") != std::string::npos &&
                  volume.find("dFdy(originalCoord)") != std::string::npos &&
                  volume.find("if (sampleProgram.RequiresExplicitGradient)") != std::string::npos &&
                  volume.find("CKFF_TEXTURE_3D_BIAS(_sampler, coord.xyz, lodBias)") != std::string::npos &&
                  CountSubstring(volume, "dFdx(") == 1 &&
                  CountSubstring(volume, "dFdy(") == 1,
              "Volume sampling must compute one vec3 footprint and retain implicit bias sampling");

    TestCheck(common.find("if (!requiresExplicitGradient)") != std::string::npos &&
                  common.find("return image.SampleBias(state, uv, bias);") != std::string::npos,
              "DXIL volume resource arrays must keep ordinary sampling on the implicit footprint path");
    TestCheck(nativeSampling.find("float3 uv, float3 dx, float3 dy, float bias") != std::string::npos &&
                  nativeSampling.find("float3 extent = float3(width, height, depth);") != std::string::npos &&
                  nativeSampling.find("length(dx * extent)") != std::string::npos &&
                  nativeSampling.find("length(dy * extent)") != std::string::npos &&
                  nativeSampling.find("CalculateLevelOfDetailUnclamped(state, originalUv)") == std::string::npos,
              "SDL volume explicit LOD must consume the original vec3 derivatives");
}

#ifdef CKRE_TEST_BGFX_ARTIFACTS
void ShaderCodegenCompilesOneProgramFamily() {
    const std::string script = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKBgfxRasterizer/shaders/compile_shaders.py");
    const std::string cmake = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKBgfxRasterizer/CMakeLists.txt");
    const std::string abi = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKBgfxRasterizer/shaders/generated/CKFFShaderABI.generated.h");
    TestCheck(!script.empty() && !cmake.empty() && !abi.empty(),
              "Shader codegen script, CMake list and generated ABI stamp must be readable");

    const char *shaderNames[] = {
        "\"vs_ff_3d\"", "\"vs_ff_3d_clip\"", "\"vs_ff_positiont\"", "\"vs_ff_positiont_clip\"",
        "\"fs_ff_stage\"", "\"fs_ff_stage_cube\"", "\"fs_ff_stage_volume\"",
        "\"vs_postprocess\"", "\"fs_postprocess\"",
    };
    for (const char *name : shaderNames) {
        TestCheck(script.find(std::string("\"name\": ") + name) != std::string::npos,
                  "Shader codegen must compile every shader of the single program family");
    }
    TestCheck(script.find("ffp_specialized_variants") == std::string::npos &&
                  script.find("CKFF_FULL_SPECIALIZED") == std::string::npos &&
                  script.find("instanced") == std::string::npos,
              "Shader codegen must keep the bounded three-layout program family");
    TestCheck(script.find("clean_stale_headers") != std::string::npos,
              "Shader codegen must remove generated headers of retired variants");

    char version[64];
    snprintf(version, sizeof(version), "g_CKFFGeneratedShaderABIVersion = %uu;", (unsigned)CKFF_SHADER_ABI_VERSION);
    char hash[64];
    snprintf(hash, sizeof(hash), "g_CKFFGeneratedShaderInterfaceHash = 0x%08xu;", (unsigned)CKFF_SHADER_INTERFACE_HASH);
    TestCheck(abi.find(version) != std::string::npos && abi.find(hash) != std::string::npos,
              "Generated shader ABI stamp must match CKFFShaderABI.h");
    TestCheck(script.find("read_shader_identity(interface_dir)") != std::string::npos &&
                  script.find("write_abi_header(generated_dir, shader_abi_version, shader_interface_hash)") != std::string::npos,
              "Shader codegen must derive the ABI stamp from the CKFF interface identity");

    TestCheck(cmake.find("${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/ff_fog_common.sc") != std::string::npos &&
                  cmake.find("${CMAKE_CURRENT_SOURCE_DIR}/../CKFFPLib/ShaderModel/shaders/fs_ff_common.sc") != std::string::npos,
              "Shader generation dependencies must include the shared shader includes");
    TestCheck(cmake.find(".json") == std::string::npos &&
                  cmake.find("CKFFSpecializedModuleTable") == std::string::npos &&
                  cmake.find("CKFFSamplerLayout.h") == std::string::npos,
              "bgfx rasterizer build must not reference variant manifests or the module table");
    TestCheck(cmake.find("DEPENDS ${CKRE_SHADERC_DEPENDS} ${_ckbgfx_shader_sources}") != std::string::npos &&
                  cmake.find("SOURCES ${_ckbgfx_shader_sources}") != std::string::npos,
              "bgfx shader generation must depend on every rasterizer and FFP source");
}

#endif

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

#ifdef CKRE_TEST_BGFX_ARTIFACTS
void BgfxTransientAllocationsPreflightAvailability() {
    const std::string contents = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKBgfxRasterizer/Native/CKBgfxCommands.cpp");
    TestCheck(!contents.empty(),
              "bgfx rasterizer context source must be readable");

    struct AllocationSequence {
        const char *Function;
        const char *Avail;
        const char *Alloc;
        const char *Miss;
    };
    const AllocationSequence sequences[] = {
        {
            "CKBOOL CKBgfxRasterizerContext::AllocTransientVertices",
            "bgfx::getAvailTransientVertexBuffer",
            "bgfx::allocTransientVertexBuffer",
            "RecordTransientAllocMiss(\"vertex\"",
        },
        {
            "CKBOOL CKBgfxRasterizerContext::AllocTransientIndices",
            "bgfx::getAvailTransientIndexBuffer",
            "bgfx::allocTransientIndexBuffer",
            "RecordTransientAllocMiss(\"index\"",
        },
    };

    for (size_t i = 0; i < sizeof(sequences) / sizeof(sequences[0]); ++i) {
        const std::string::size_type functionStart = contents.find(sequences[i].Function);
        TestCheck(functionStart != std::string::npos,
                  "transient allocation function must exist");
        if (functionStart == std::string::npos)
            continue;

        const std::string::size_type availPos = contents.find(sequences[i].Avail, functionStart);
        const std::string::size_type allocPos = contents.find(sequences[i].Alloc, functionStart);
        const std::string::size_type missPos = contents.find(sequences[i].Miss, functionStart);

        TestCheck(availPos != std::string::npos && allocPos != std::string::npos && availPos < allocPos,
                  "transient allocation must query bgfx availability before allocation");
        TestCheck(missPos != std::string::npos && missPos < allocPos,
                  "transient allocation must record capacity misses before attempting allocation");
    }
}

#endif

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Bump env uniforms pack each stage independently",
              &BumpEnvUniformsPackEachStageIndependently);
    tests.Run("Texture arg modifier repack round trips both modifier bits",
              &TextureArgModifierRepackRoundTripsBothModifierBits);
    tests.Run("Shader ABI constants match shader uniform declarations",
              &ShaderABIConstantsMatchShaderUniformDeclarations);
    tests.Run("Sampler shader state resolves backend responsibilities",
              &SamplerShaderStateResolvesBackendResponsibilities);
    tests.Run("Sampler shader state shader header matches C++ ABI",
              &SamplerShaderStateShaderHeaderMatchesCppABI);
    tests.Run("Shader sources declare portable flat and clip-space conventions",
              &ShaderSourcesDeclarePortableFlatAndClipSpaceConventions);
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
    tests.Run("MIRRORONCE shader source applies only to 2D and volume",
              &MirrorOnceShaderSourceAppliesOnlyTo2DAndVolume);
    tests.Run("Texture combiner op formulas stay DXVK compatible",
              &TextureCombinerOpFormulasStayDxvkCompatible);
    tests.Run("Fragment program uniform carries lanes as exact integers",
              &FragmentProgramUniformCarriesLanesAsExactIntegers);
    tests.Run("Fragment program layout shader header matches the def",
              &FragmentProgramLayoutShaderHeaderMatchesTheDef);
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
    tests.Run("Texture combiner TEMP initializes alpha to zero",
              &TextureCombinerTempInitializesAlphaToZero);
    tests.Run("Depth texture compare uses sampler compare ordering",
              &DepthTextureCompareUsesSamplerCompareOrdering);
    tests.Run("Vertex blend resolver matches dxvk weight counts",
              &VertexBlendResolverMatchesDxvkWeightCounts);
    tests.Run("Vertex blend resolver handles missing indexed input and POSITIONT",
              &VertexBlendResolverHandlesMissingIndexedInputAndPositionT);
    tests.Run("TWEENING inputs and shader are wired",
              &TweeningInputsAndShaderAreWired);
    tests.Run("DP weight flags add blend layout flags",
              &DPWeightFlagsAddBlendLayoutFlags);
    tests.Run("Sampler types pack into fragment program",
              &SamplerTypesPackIntoFragmentProgram);
    tests.Run("Volume sampler and compare func pack into fragment program",
              &VolumeSamplerAndCompareFuncPackIntoFragmentProgram);
    tests.Run("Volume sampler mask can be derived from shader key",
              &VolumeSamplerMaskCanBeDerivedFromShaderKey);
    tests.Run("Fragment shader declares all exact sampler layouts",
              &FragmentShaderDeclaresAllExactSamplerLayouts);
    tests.Run("Texture derivatives stay in typed sampling paths",
              &TextureDerivativesStayInTypedSamplingPaths);
#ifdef CKRE_TEST_BGFX_ARTIFACTS
    tests.Run("Shader codegen compiles one program family",
              &ShaderCodegenCompilesOneProgramFamily);
#endif
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
#ifdef CKRE_TEST_BGFX_ARTIFACTS
    tests.Run("bgfx transient allocations preflight availability",
              &BgfxTransientAllocationsPreflightAvailability);
#endif
    return tests.ExitCode();
}
