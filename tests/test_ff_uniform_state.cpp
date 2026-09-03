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
    const CKDWORD repacked = CKFFSpecializationInfo::RepackArg(arg);
    const CKDWORD unpacked = (repacked & 0x7u) | ((repacked & 0x18u) << 1u);

    TestCheck(unpacked == arg,
              "Specialization repack/unpack must preserve complement and alpha replicate modifiers");
}

void ShaderABIConstantsMatchShaderUniformDeclarations() {
    TestCheck(CKFF_DRAW_PARAM_VEC4_COUNT == 20,
              "u_ffDrawParams ABI must include the tween parameter vec4");
    TestCheck(CKFF_STAGE_PARAM_VEC4_COUNT == 16,
              "u_stageParams ABI must be 16 vec4s (coord + constant per stage)");
    TestCheck(CKFF_SPEC_UNIFORM_VEC4_COUNT == CKFFSpecializationInfo::Vec4Count &&
                  CKFF_SPEC_UNIFORM_VEC4_COUNT == 5,
              "u_ffSpec ABI must mirror the specialization lane count (20 lanes = 5 vec4)");
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

    const std::string fs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_stage.sc");
    const std::string vs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_3d.sc");
    TestCheck(fs.find("uniform vec4 u_ffDrawParams[20]") != std::string::npos &&
                  vs.find("uniform vec4 u_ffDrawParams[20]") != std::string::npos,
              "Shader sources must declare u_ffDrawParams with the ABI count");
    TestCheck(fs.find("uniform vec4 u_stageParams[16]") != std::string::npos &&
                  vs.find("uniform vec4 u_stageParams[16]") != std::string::npos,
              "Shader sources must declare u_stageParams with the ABI count");
    TestCheck(fs.find("uniform vec4 u_ffSpec[5]") != std::string::npos,
              "Fragment shader must declare u_ffSpec with the specialization ABI count");
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

void ShaderSourcesDeclarePortableFlatAndClipSpaceContracts() {
    const std::string varying = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/varying.def.sc");
    const std::string compiler = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/compile_shaders.py");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_positiont.sc");

    TestCheck(varying.find("flat vec4 v_flatColor0") != std::string::npos &&
                  varying.find("flat vec4 v_flatColor1") != std::string::npos,
              "All shader backends must compile flat colors as non-interpolated varyings");
    TestCheck(compiler.find("varying_no_flat_color.def.sc") == std::string::npos &&
                  compiler.find("CKFF_NDC_MINUS_ONE_TO_ONE=1") != std::string::npos,
              "Shader generation must use one flat-varying ABI and mark the GLSL depth convention");
    TestCheck(vs3d.find("position.z = position.z * 2.0 - position.w") != std::string::npos &&
                  vsPositionT.find("position.z = position.z * 2.0 - position.w") != std::string::npos,
              "Both 3D and POSITIONT shaders must convert D3D clip depth for desktop OpenGL");
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

void MirrorOnceSpecializationPacksEveryStage() {
    CKFFFSStateDesc desc;
    CKDWORD expectedMask = 0;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        desc.SetStageColorOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageAlphaArg1(stage, CKRST_TA_TEXTURE);
        const CKDWORD mask = (stage % 7u) + 1u;
        desc.SetStageMirrorOnceMask(stage, mask);
        expectedMask |= mask << (stage * 3);
    }

    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0xFFu);
    CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);

    TestCheck(key.Stages[3].MirrorOnceMask == 4 && key.Stages[7].MirrorOnceMask == 1,
              "Shader key must preserve per-stage MIRRORONCE mask");
    TestCheck(spec.Get(CKFF_SPEC_MIRRORONCE_SAMPLER_MASK) == expectedMask,
              "Specialization data must pack the MIRRORONCE masks of all eight stages");
    TestCheck(spec.GetMirrorOnceMask(7) == 1 && spec.GetMirrorOnceMask(2) == 3,
              "Per-stage MIRRORONCE accessor must read three bits per stage");
}

void LastActiveTextureStageSpecializationRoundTrips() {
    CKFFSpecializationInfo spec;
    for (CKDWORD lastStage = 0; lastStage < 8; ++lastStage) {
        spec.Set(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE, lastStage);
        TestCheck(spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == lastStage,
                  "Last active texture stage must round-trip through specialization dwords");
    }
}

void MirrorOnceShaderSourceAppliesOnlyTo2DAndVolume() {
    const std::string fs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_stage.sc");
    const std::string common = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_common.sc");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_positiont.sc");

    TestCheck(!fs.empty() && !common.empty() && !vs3d.empty() && !vsPositionT.empty(),
              "FFP shader sources must be readable");
    TestCheck(common.find("int MirrorOnceMask;") != std::string::npos &&
                  common.find("ckffSpecMirrorOnceMask") != std::string::npos &&
                  common.find("ckffSpec_MIRRORONCE_SAMPLER_MASK() >> (stage * 3)") != std::string::npos,
              "Fragment common shader must read MIRRORONCE masks from the specialization data");
    TestCheck(fs.find("vec4 applyMirrorOnceCoord") != std::string::npos &&
                  fs.find("if (samplerType == 1 || mirrorOnceMask == 0) return coord") != std::string::npos &&
                  fs.find("samplerType == 3 && (mirrorOnceMask & 4)") != std::string::npos,
              "Fragment shader must remap 2D/volume coordinates while leaving cube coordinates untouched");
    TestCheck(fs.find("coord = applyMirrorOnceCoord(coord, mirrorOnceMask, samplerType);") != std::string::npos,
              "MIRRORONCE remap must happen inside texture sampling after projected coordinate preparation");
    TestCheck(vs3d.find("int count = flags & 0xff;") != std::string::npos &&
                  vsPositionT.find("int count = flags & 0xff;") != std::string::npos,
              "Vertex shaders must keep texture-transform component count isolated from MIRRORONCE high bits");
}

void TextureCombinerOpFormulasStayDxvkCompatible() {
    const std::string fs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_stage.sc");

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

void SpecUniformCarriesLanesAsExactIntegers() {
    CKFFSpecializationInfo info;
    info.Set(CKFF_SPEC_ALPHA_TEST_ENABLED, 1);
    info.Set(CKFF_SPEC_ALPHA_FUNC, VXCMP_GREATER);
    info.Set(CKFF_SPEC_MIRRORONCE_SAMPLER_MASK, 0xFFFFFFu);
    info.SetStage(0, CKFF_SPEC_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_CUBE);
    info.SetStage(1, CKFF_SPEC_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_VOLUME);
    info.SetStage(7, CKFF_SPEC_STAGE_COLOR_OP, CKRST_TOP_LERP);

    CKFFSpecUniform packed;
    CKFFPackSpecialization(info, packed);

    const CKDWORD *lanes = info.Lanes();
    for (CKDWORD lane = 0; lane < CKFFSpecializationInfo::LaneCount; ++lane) {
        const float value = packed.Values[lane / 4][lane % 4];
        TestCheck((CKDWORD)value == lanes[lane] && value < 16777216.0f,
                  "u_ffSpec must carry every 24-bit lane as an exact integer float");
    }
    TestCheck(packed.Values[17 / 4][17 % 4] == 16777215.0f,
              "A full 24-bit lane must survive the float encoding");
    const CKFFSpecializationInfo unpacked =
        CKFFSpecializationInfo::Unpack24(&packed.Values[0][0], CKFF_SPEC_UNIFORM_VEC4_COUNT * 4);
    TestCheck(unpacked == info,
              "u_ffSpec encoding must round-trip the specialization data");
}

void SpecLayoutShaderHeaderMatchesTheDef() {
    const std::string generated = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/ff_spec_layout.sh");
    const std::string common = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_common.sc");
    const std::string script = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/compile_shaders.py");
    TestCheck(!generated.empty() && !common.empty() && !script.empty(),
              "Generated spec layout header, fragment common shader and codegen script must be readable");

    char line[160];
    snprintf(line, sizeof(line), "#define CKFF_SPEC_LANE_COUNT %u", (unsigned)CKFF_SPEC_LANE_COUNT);
    TestCheck(generated.find(line) != std::string::npos,
              "Generated spec layout must define the lane count");
    snprintf(line, sizeof(line), "#define CKFF_SPEC_STAGE_LANE_STRIDE %u", (unsigned)CKFF_SPEC_STAGE_LANE_STRIDE);
    TestCheck(generated.find(line) != std::string::npos,
              "Generated spec layout must define the stage lane stride");
    snprintf(line, sizeof(line), "#define CKFF_SPEC_GLOBAL_LANE_BASE %u", (unsigned)CKFF_SPEC_GLOBAL_LANE_BASE);
    TestCheck(generated.find(line) != std::string::npos,
              "Generated spec layout must define the global lane base");

    for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_SPEC_STAGE_FIELD_COUNT; ++fieldIndex) {
        const CKFFSpecFieldDesc &desc = CKFFSpecStageFieldDesc((CKFFSpecStageField)fieldIndex);
        snprintf(line, sizeof(line), "int ckffSpecStage_%s(int stage) { return ckffSpecStageBits(stage, %u, %u, %u); }",
                 desc.Name, desc.Layout.Lane, desc.Layout.BitOffset, desc.Layout.BitCount);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated spec layout must expose every stage field with the C++ bit positions");
    }
    for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_SPEC_GLOBAL_FIELD_COUNT; ++fieldIndex) {
        const CKFFSpecFieldDesc &desc = CKFFSpecGlobalFieldDesc((CKFFSpecGlobalField)fieldIndex);
        snprintf(line, sizeof(line), "int ckffSpec_%s() { return ckffSpecBits(%u, %u, %u); }",
                 desc.Name, desc.Layout.Lane, desc.Layout.BitOffset, desc.Layout.BitCount);
        TestCheck(generated.find(line) != std::string::npos,
                  "Generated spec layout must expose every global field with the C++ bit positions");
    }
    TestCheck(common.find("#include \"ff_spec_layout.sh\"") != std::string::npos &&
                  common.find("int ckffSpecLane(int lane)") != std::string::npos &&
                  common.find("return int(v.x)") != std::string::npos &&
                  common.find("floatBitsToUint") == std::string::npos &&
                  common.find("uint(255)") == std::string::npos,
              "Fragment shader must read u_ffSpec lanes with int() through the generated layout, not bytes or bit casts");
    TestCheck(script.find("gen-spec-layout") != std::string::npos &&
                  script.find("CKFFSpecLayout.def") != std::string::npos &&
                  script.find("def validate_spec_layout") != std::string::npos,
              "Shader codegen must generate and validate the spec layout from CKFFSpecLayout.def");
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

    const std::string fs = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_stage.sc");
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
    const std::string contents = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_stage.sc");

    TestCheck(!contents.empty(),
              "FFP fragment shader source must be readable from the test working directory");
    TestCheck(contents.find("vec4 temp = vec4(0.0, 0.0, 0.0, 0.0)") != std::string::npos,
              "FFP TEMP register must initialize all channels to zero");
    TestCheck(contents.find("vec4 temp = vec4(0.0, 0.0, 0.0, 1.0)") == std::string::npos,
              "FFP TEMP alpha must not initialize to one");
}

void DepthTextureCompareUsesSamplerCompareOrdering() {
    const std::string contents = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_stage.sc");

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

void VertexBlendResolverRejectsMissingIndexedInputAndPositionT() {
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
    TestCheck(!missingTweenNormal.Supported &&
                  missingTweenNormal.UnsupportedReason ==
                      CKFF_VERTEX_BLEND_UNSUPPORTED_MISSING_TWEEN_NORMAL,
              "Lit tweening must reject a missing second normal");

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
    TestCheck(!indexedTween.Supported &&
                  indexedTween.UnsupportedReason ==
                      CKFF_VERTEX_BLEND_UNSUPPORTED_INDEXED_TWEEN,
              "Tweening and indexed matrix blending must remain mutually exclusive");
}

void TweeningInputsAndShaderAreWired() {
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_3d.sc");
    const std::string layout = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/CKVertexLayoutCache.cpp");
    const std::string transient = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/CKTransientGeometry.cpp");
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
    CKDWORD flags = CKVertexLayoutCache::DPFlagsToFormatFlags(
        (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2), FALSE, FALSE);
    TestCheck((flags & CKFF_VF_BLENDWEIGHT) != 0,
              "DP weight flags must request blend weight attribute");
    TestCheck((flags & CKFF_VF_BLENDINDEX) == 0,
              "Non-indexed DP weight flags must not request blend index attribute");

    CKDWORD indexed = CKVertexLayoutCache::DPFlagsToFormatFlags(
        (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2 | CKRST_DP_MATRIXPAL), FALSE, FALSE);
    TestCheck((indexed & CKFF_VF_BLENDWEIGHT) != 0 && (indexed & CKFF_VF_BLENDINDEX) != 0,
              "Matrix palette DP flags must request both weights and indices");
}

void SamplerTypesPackIntoSpecialization() {
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
    CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_CUBE &&
                  spec.GetStage(1, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_2D &&
                  spec.GetStage(2, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_2D &&
                  spec.GetStage(3, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_DEPTH,
              "Sampler type specialization must pack per stage");
}

void VolumeSamplerAndCompareFuncPackIntoSpecialization() {
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
    CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_VOLUME,
              "Volume sampler type must pack into specialization");
    TestCheck(spec.GetStage(1, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_DEPTH,
              "Depth sampler type must remain packed independently");
    TestCheck(spec.GetStage(1, CKFF_SPEC_STAGE_SAMPLER_COMPARE_FUNC) == CKRST_COMPARE_LEQUAL &&
                  spec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_COMPARE_FUNC) == CKRST_COMPARE_NONE,
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

void FragmentShaderDeclaresTheFixedSamplerLayout() {
    const std::string fs = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_stage.sc");
    const std::string common = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/fs_ff_common.sc");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/vs_ff_positiont.sc");
    TestCheck(!fs.empty() && !common.empty() && !vs3d.empty() && !vsPositionT.empty(),
              "FFP shader sources must be readable");

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        char decl[64];
        snprintf(decl, sizeof(decl), "SAMPLER2D(s_texture%d, %d);", stage, stage);
        TestCheck(fs.find(decl) != std::string::npos,
                  "Fragment shader must declare one 2D sampler per texture stage on slots 0..7");
    }
    for (int ordinal = 0; ordinal < CKFF_CUBE_SAMPLER_COUNT; ++ordinal) {
        char decl[64];
        snprintf(decl, sizeof(decl), "SAMPLERCUBE(s_textureCube%d, %d);", ordinal,
                 (int)CKFFSamplerSlot(CKFF_SAMPLER_CUBE, ordinal));
        TestCheck(fs.find(decl) != std::string::npos,
                  "Fragment shader must declare the cube samplers on slots 8..11");
    }
    for (int ordinal = 0; ordinal < CKFF_VOLUME_SAMPLER_COUNT; ++ordinal) {
        char decl[64];
        snprintf(decl, sizeof(decl), "SAMPLER3D(s_textureVolume%d, %d);", ordinal,
                 (int)CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, ordinal));
        TestCheck(fs.find(decl) != std::string::npos,
                  "Fragment shader must declare the volume samplers on slots 12..15");
    }
    TestCheck(fs.find("SAMPLERCUBE(s_textureCube4") == std::string::npos &&
                  fs.find("SAMPLER3D(s_textureVolume4") == std::string::npos,
              "Fragment shader must not declare more than four cube or volume samplers");
    TestCheck(fs.find("int ckffSamplerOrdinal(int stage, int samplerType)") != std::string::npos &&
                  fs.find("if (ckffSpecStage_SAMPLER_TYPE(previousStage) == samplerType)") != std::string::npos,
              "Fragment shader must pick cube / volume samplers by type ordinal from the specialization data");

    const std::string *sources[] = {&fs, &common, &vs3d, &vsPositionT};
    const char *retiredMacros[] = {
        "CKFF_FULL_SPECIALIZED", "CKFF_STATIC_SAMPLER_LAYOUT", "CKFF_VOLUME_SAMPLER_LAYOUT",
        "CKFF_MIXED_SAMPLER_LAYOUT", "CKFF_VS_INSTANCED", "CKFF_VS_ACTIVE_TEXCOORD_COUNT",
        "CKFF_FS_ACTIVE_STAGE_COUNT", "ckffSpecIsOptimized", "u_ffDrawParams[8].y",
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

void ShaderCodegenCompilesOneProgramFamily() {
    const std::string script = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/compile_shaders.py");
    const std::string cmake = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/CMakeLists.txt");
    const std::string abi = ReadTextFile("Source/RenderEngine/src/CKRasterizer/CKRasterizerLib/shaders/generated/CKFFShaderABI.generated.h");
    TestCheck(!script.empty() && !cmake.empty() && !abi.empty(),
              "Shader codegen script, CMake list and generated ABI stamp must be readable");

    const char *shaderNames[] = {
        "\"vs_ff_3d\"", "\"vs_ff_3d_clip\"", "\"vs_ff_positiont\"", "\"vs_ff_positiont_clip\"",
        "\"fs_ff_stage\"", "\"vs_postprocess\"", "\"fs_postprocess\"",
    };
    for (const char *name : shaderNames) {
        TestCheck(script.find(std::string("\"name\": ") + name) != std::string::npos,
                  "Shader codegen must compile every shader of the single program family");
    }
    TestCheck(script.find("ffp_specialized_variants") == std::string::npos &&
                  script.find("sampler_layout") == std::string::npos &&
                  script.find("CKFF_FULL_SPECIALIZED") == std::string::npos &&
                  script.find("instanced") == std::string::npos &&
                  script.find("fs_ff_stage_volume") == std::string::npos,
              "Shader codegen must not generate specialized, sampler-layout, instanced or volume variants");
    TestCheck(script.find("clean_stale_headers") != std::string::npos,
              "Shader codegen must remove generated headers of retired variants");

    char version[64];
    snprintf(version, sizeof(version), "g_CKFFGeneratedShaderABIVersion = %uu;", (unsigned)CKFF_SHADER_ABI_VERSION);
    char hash[64];
    snprintf(hash, sizeof(hash), "g_CKFFGeneratedShaderInterfaceHash = 0x%08xu;", (unsigned)CKFF_SHADER_INTERFACE_HASH);
    TestCheck(abi.find(version) != std::string::npos && abi.find(hash) != std::string::npos,
              "Generated shader ABI stamp must match CKFFShaderABI.h");
    char scriptVersion[64];
    snprintf(scriptVersion, sizeof(scriptVersion), "SHADER_ABI_VERSION = %u\n", (unsigned)CKFF_SHADER_ABI_VERSION);
    char scriptHash[64];
    snprintf(scriptHash, sizeof(scriptHash), "SHADER_INTERFACE_HASH = 0x%08X\n", (unsigned)CKFF_SHADER_INTERFACE_HASH);
    TestCheck(script.find(scriptVersion) != std::string::npos && script.find(scriptHash) != std::string::npos,
              "Shader codegen ABI stamp constants must match CKFFShaderABI.h");

    TestCheck(cmake.find("${CMAKE_CURRENT_SOURCE_DIR}/shaders/ff_fog_common.sc") != std::string::npos &&
                  cmake.find("${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_ff_common.sc") != std::string::npos,
              "Shader generation dependencies must include the shared shader includes");
    TestCheck(cmake.find(".json") == std::string::npos &&
                  cmake.find("CKFFSpecializedModuleTable") == std::string::npos &&
                  cmake.find("CKFFSamplerLayout.h") == std::string::npos,
              "Translation core build must not reference variant manifests or the module table");
    TestCheck(cmake.find("DEPENDS ${CKRE_SHADERC_DEPENDS} ${CKRE_SHADER_SOURCES}") != std::string::npos &&
                  cmake.find("SOURCES ${CKRE_SHADER_SOURCES}") != std::string::npos,
              "Shader generation must depend on CKRE_SHADER_SOURCES");
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
    TestCheck(!key.Stages[1].HasTexture && key.Stages[1].SamplerType == CKFF_SAMPLER_2D,
              "A non-sampling stage must normalize to a 2D sampler type");
    TestCheck(CKFFSamplerOrdinal(key, 0) == 0 && CKFFSamplerOrdinal(key, 3) == 1,
              "Cube ordinals must count only earlier sampling cube stages");
    TestCheck(CKFFSamplerOrdinal(key, 2) == 0 && CKFFSamplerOrdinal(key, 5) == 1,
              "Volume ordinals must count only earlier sampling volume stages");
    TestCheck(CKFFSamplerOrdinal(key, 4) == 0,
              "2D ordinals are not used for slot selection and must not count cube / volume stages");
    TestCheck(key.SamplerSlotOverflowMask == 0,
              "Within the fixed sampler budget no stage may overflow");
    TestCheck(CKFFSamplerSlot(CKFF_SAMPLER_CUBE, CKFFSamplerOrdinal(key, 3)) == 9 &&
                  CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, CKFFSamplerOrdinal(key, 5)) == 13,
              "Type ordinals must map onto the cube 8..11 and volume 12..15 slot blocks");
}

void SamplerSlotOverflowSamplesAsUnbound() {
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
    for (CKDWORD stage = 0; stage < 4; ++stage) {
        TestCheck(key.Stages[stage].HasTexture && key.Stages[stage].SamplerType == CKFF_SAMPLER_CUBE &&
                      CKFFSamplerOrdinal(key, stage) == stage,
                  "The first four cube stages must keep their cube sampler");
    }
    TestCheck(!key.Stages[4].HasTexture && key.Stages[4].SamplerType == CKFF_SAMPLER_2D,
              "The fifth cube stage must sample as unbound (spec 5.3 fixed sampler budget)");
    TestCheck(key.Stages[5].HasTexture && key.Stages[5].SamplerType == CKFF_SAMPLER_VOLUME &&
                  CKFFSamplerOrdinal(key, 5) == 0,
              "Volume stages keep their own four-slot budget");
    TestCheck(key.SamplerSlotOverflowMask == (1u << 4),
              "Overflowing stages must be reported in the shader key for diagnostics");
    TestCheck(key.LastActiveTextureStage == 5,
              "Sampler slot overflow must not truncate the active stage chain");

    // The stage params mirror the fallback: colorParams.w (has texture) drops to 0.
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
    CKFFPackStageParams(stages, textures, textureFlags, 6, params, NULL, key.SamplerSlotOverflowMask);
    TestCheck(params.Values[CKFFStageParamIndex(3, CKFF_STAGE_PARAM_COORD)][2] == 1.0f &&
                  params.Values[CKFFStageParamIndex(4, CKFF_STAGE_PARAM_COORD)][2] == 0.0f &&
                  params.Values[CKFFStageParamIndex(5, CKFF_STAGE_PARAM_COORD)][2] == 1.0f,
              "Stage params must clear the has-texture flag of overflowing stages only");
}

void TextureStageCompareFuncStaysOutOfSamplerDesc() {
    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    CKSamplerDesc sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.CompareFunc == CKRST_COMPARE_NONE,
              "Texture stage compare func must default to NONE");

    stages[0][CKRST_TSS_COMPAREFUNC] = CKRST_COMPARE_GREATER;
    sampler = CKFFBuildSamplerDesc(stages[0]);
    TestCheck(sampler.CompareFunc == CKRST_COMPARE_NONE,
              "FFP depth compare is shader-evaluated and must not enable sampler compare");
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

void BgfxTransientAllocationsPreflightAvailability() {
    const std::string contents = ReadTextFile(
        "Source/RenderEngine/src/CKRasterizer/CKBgfxRasterizer/CKBgfxRasterizerContext.cpp");
    TestCheck(!contents.empty(),
              "bgfx rasterizer context source must be readable");

    struct Contract {
        const char *Function;
        const char *Avail;
        const char *Alloc;
        const char *Miss;
    };
    const Contract contracts[] = {
        {
            "CKBOOL CKBgfxRasterizerContext::AllocTransientVertexBuffer",
            "bgfx::getAvailTransientVertexBuffer",
            "bgfx::allocTransientVertexBuffer",
            "RecordTransientAllocMiss(\"vertex\"",
        },
        {
            "CKBOOL CKBgfxRasterizerContext::AllocTransientIndexBuffer",
            "bgfx::getAvailTransientIndexBuffer",
            "bgfx::allocTransientIndexBuffer",
            "RecordTransientAllocMiss(\"index\"",
        },
        {
            "CKBOOL CKBgfxRasterizerContext::AllocTransientInstanceBuffer",
            "bgfx::getAvailInstanceDataBuffer",
            "bgfx::allocInstanceDataBuffer",
            "RecordTransientAllocMiss(\"instance\"",
        },
    };

    for (size_t i = 0; i < sizeof(contracts) / sizeof(contracts[0]); ++i) {
        const std::string::size_type functionStart = contents.find(contracts[i].Function);
        TestCheck(functionStart != std::string::npos,
                  "transient allocation function must exist");
        if (functionStart == std::string::npos)
            continue;

        const std::string::size_type availPos = contents.find(contracts[i].Avail, functionStart);
        const std::string::size_type allocPos = contents.find(contracts[i].Alloc, functionStart);
        const std::string::size_type missPos = contents.find(contracts[i].Miss, functionStart);

        TestCheck(availPos != std::string::npos && allocPos != std::string::npos && availPos < allocPos,
                  "transient allocation must query bgfx availability before allocation");
        TestCheck(missPos != std::string::npos && missPos < allocPos,
                  "transient allocation must record capacity misses before attempting allocation");
    }
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Bump env uniforms pack each stage independently",
              &BumpEnvUniformsPackEachStageIndependently);
    tests.Run("Texture arg modifier repack round trips both modifier bits",
              &TextureArgModifierRepackRoundTripsBothModifierBits);
    tests.Run("Shader ABI constants match shader uniform declarations",
              &ShaderABIConstantsMatchShaderUniformDeclarations);
    tests.Run("Shader sources declare portable flat and clip-space contracts",
              &ShaderSourcesDeclarePortableFlatAndClipSpaceContracts);
    tests.Run("Stage params pack through ABI indices",
              &StageParamsPackThroughABIIndices);
    tests.Run("MIRRORONCE address modes pack into stage params",
              &MirrorOnceAddressModesPackIntoStageParams);
    tests.Run("MIRRORONCE sampler desc falls back to clamp",
              &MirrorOnceSamplerDescFallsBackToClamp);
    tests.Run("MIRRORONCE specialization packs every stage",
              &MirrorOnceSpecializationPacksEveryStage);
    tests.Run("Last active texture stage specialization round trips",
              &LastActiveTextureStageSpecializationRoundTrips);
    tests.Run("MIRRORONCE shader source applies only to 2D and volume",
              &MirrorOnceShaderSourceAppliesOnlyTo2DAndVolume);
    tests.Run("Texture combiner op formulas stay DXVK compatible",
              &TextureCombinerOpFormulasStayDxvkCompatible);
    tests.Run("Spec uniform carries lanes as exact integers",
              &SpecUniformCarriesLanesAsExactIntegers);
    tests.Run("Spec layout shader header matches the def",
              &SpecLayoutShaderHeaderMatchesTheDef);
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
    tests.Run("Vertex blend resolver rejects missing indexed input and POSITIONT",
              &VertexBlendResolverRejectsMissingIndexedInputAndPositionT);
    tests.Run("TWEENING inputs and shader are wired",
              &TweeningInputsAndShaderAreWired);
    tests.Run("DP weight flags add blend layout flags",
              &DPWeightFlagsAddBlendLayoutFlags);
    tests.Run("Sampler types pack into specialization",
              &SamplerTypesPackIntoSpecialization);
    tests.Run("Volume sampler and compare func pack into specialization",
              &VolumeSamplerAndCompareFuncPackIntoSpecialization);
    tests.Run("Volume sampler mask can be derived from shader key",
              &VolumeSamplerMaskCanBeDerivedFromShaderKey);
    tests.Run("Fragment shader declares the fixed sampler layout",
              &FragmentShaderDeclaresTheFixedSamplerLayout);
    tests.Run("Shader codegen compiles one program family",
              &ShaderCodegenCompilesOneProgramFamily);
    tests.Run("Sampler ordinal counts only sampling stages of the same type",
              &SamplerOrdinalCountsOnlySamplingStagesOfTheSameType);
    tests.Run("Sampler slot overflow samples as unbound",
              &SamplerSlotOverflowSamplesAsUnbound);
    tests.Run("Texture stage compare func stays out of sampler desc",
              &TextureStageCompareFuncStaysOutOfSamplerDesc);
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
    tests.Run("bgfx transient allocations preflight availability",
              &BgfxTransientAllocationsPreflightAvailability);
    return tests.ExitCode();
}
