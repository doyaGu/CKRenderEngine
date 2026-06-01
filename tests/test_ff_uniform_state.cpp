#include <stdio.h>

#include "CKFFStageState.h"
#include "CKFFShaderABI.h"
#include "CKFFShaderKey.h"
#include "CKFFSamplerLayout.h"
#include "CKFFUniformState.h"
#include "CKFixedFunctionPipeline.h"
#include "CKVertexLayoutCache.h"
#include "TestTriangleMultiset.h"

#include <cstring>
#include <fstream>
#include <string>

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

std::string::size_type FindFullSpecializedBlockEnd(const std::string &contents) {
    const std::string blockStart = "#if defined(CKFF_FULL_SPECIALIZED)";
    std::string::size_type lineStart = contents.find(blockStart);
    if (lineStart == std::string::npos)
        return std::string::npos;

    int depth = 0;
    while (lineStart != std::string::npos) {
        std::string::size_type lineEnd = contents.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = contents.size();

        std::string::size_type first = lineStart;
        while (first < lineEnd && (contents[first] == ' ' || contents[first] == '\t'))
            ++first;

        if (contents.compare(first, 3, "#if") == 0) {
            ++depth;
        } else if (contents.compare(first, 6, "#endif") == 0) {
            --depth;
            if (depth == 0)
                return first;
        }

        lineStart = lineEnd == contents.size() ? std::string::npos : lineEnd + 1;
    }

    return std::string::npos;
}

void RuntimeVertexShaderKeepsAdditionalTexcoordsActive(const char *path,
                                                       const char *readErrorMessage) {
    const std::string contents = ReadTextFile(path);
    const std::string defaultGuard = "#ifndef CKFF_VS_ACTIVE_TEXCOORD_COUNT";
    const std::string defaultDefine = "#define CKFF_VS_ACTIVE_TEXCOORD_COUNT 8";
    const std::string firstTexcoordUse = "#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 1";

    TestCheck(!contents.empty(), readErrorMessage);

    const std::string::size_type specializedEnd = FindFullSpecializedBlockEnd(contents);
    const std::string::size_type guardPos = contents.find(defaultGuard);
    const std::string::size_type definePos = contents.find(defaultDefine, guardPos);
    const std::string::size_type usePos = contents.find(firstTexcoordUse);

    TestCheck(specializedEnd != std::string::npos,
              "Runtime vertex shader test must find the CKFF_FULL_SPECIALIZED block");
    TestCheck(guardPos != std::string::npos && definePos != std::string::npos,
              "Runtime vertex shader must define a default active texcoord count");
    TestCheck(usePos != std::string::npos,
              "Runtime vertex shader test must find texcoord output gating");
    TestCheck(specializedEnd < guardPos,
              "Runtime vertex shader active texcoord default must be visible outside CKFF_FULL_SPECIALIZED");
    TestCheck(definePos < usePos,
              "Runtime vertex shader active texcoord default must precede texcoord output gating");
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
    TestCheck(CKFF_DRAW_PARAM_VEC4_COUNT == 19,
              "u_ffDrawParams ABI must remain 19 vec4s");
    TestCheck(CKFF_STAGE_PARAM_VEC4_COUNT == 32,
              "u_stageParams ABI must remain 32 vec4s");
    TestCheck(CKFF_SPEC_UNIFORM_VEC4_COUNT == CKFFSpecializationInfo::MaxSpecDwords,
              "u_ffSpec ABI must mirror the specialization dword count");
    TestCheck(CKFFStageParamIndex(3, CKFF_STAGE_PARAM_ALPHA_EXTRA) == 15,
              "Stage parameter index helper must encode four vec4s per stage");
    TestCheck(CKFFSamplerBindStage(2, CKFF_SAMPLER_2D) == 2,
              "2D samplers must bind to texture slots 0..7");
    TestCheck(CKFFSamplerBindStage(2, CKFF_SAMPLER_CUBE) == 10 &&
                  CKFFSamplerBindStage(2, CKFF_SAMPLER_VOLUME) == 10,
              "Cube and volume samplers must bind to texture slots 8..15");

    const std::string fs = ReadTextFile("Source/RenderEngine/src/shaders/fs_ff_stage.sc");
    const std::string vs = ReadTextFile("Source/RenderEngine/src/shaders/vs_ff_3d.sc");
    TestCheck(fs.find("uniform vec4 u_ffDrawParams[19]") != std::string::npos &&
                  vs.find("uniform vec4 u_ffDrawParams[19]") != std::string::npos,
              "Shader sources must declare u_ffDrawParams with the ABI count");
    TestCheck(fs.find("uniform vec4 u_stageParams[32]") != std::string::npos &&
                  vs.find("uniform vec4 u_stageParams[32]") != std::string::npos,
              "Shader sources must declare u_stageParams with the ABI count");
    TestCheck(fs.find("uniform vec4 u_ffSpec[10]") != std::string::npos,
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

    CKFFPackStageParams(stages, textures, 3, params);

    const float *color = params.Values[CKFFStageParamIndex(2, CKFF_STAGE_PARAM_COLOR)];
    const float *alpha = params.Values[CKFFStageParamIndex(2, CKFF_STAGE_PARAM_ALPHA)];
    const float *colorExtra = params.Values[CKFFStageParamIndex(2, CKFF_STAGE_PARAM_COLOR_EXTRA)];
    const float *alphaExtra = params.Values[CKFFStageParamIndex(2, CKFF_STAGE_PARAM_ALPHA_EXTRA)];

    TestCheck(color[0] == (float)CKRST_TOP_SELECTARG1 &&
                  color[1] == (float)CKRST_TA_TEXTURE &&
                  color[2] == (float)CKRST_TA_TFACTOR &&
                  color[3] == 1.0f,
              "Stage color params must pack through the declared ABI slot");
    TestCheck(alpha[0] == (float)CKRST_TOP_SELECTARG2 &&
                  alpha[1] == (float)CKRST_TA_CURRENT &&
                  alpha[2] == (float)CKRST_TA_TEXTURE,
              "Stage alpha params must pack through the declared ABI slot");
    TestCheck(colorExtra[0] == (float)CKRST_TA_CONSTANT &&
                  colorExtra[1] == 6.0f &&
                  colorExtra[2] == (float)0x103,
              "Stage color extra params must pack texcoord and transform ABI fields");
    TestCheck(colorExtra[3] > 0.24f && colorExtra[3] < 0.26f &&
                  alphaExtra[1] > 0.12f && alphaExtra[1] < 0.13f &&
                  alphaExtra[2] > 0.06f && alphaExtra[2] < 0.07f &&
                  alphaExtra[3] > 0.49f && alphaExtra[3] < 0.51f,
              "Stage constant must pack RGBA into color/alpha extra ABI fields");
}

void SpecUniformMirrorsSpecializationDwordsAsBytes() {
    CKFFSpecializationInfo info;
    info.SetOptimized(true);
    info.Set(CKFF_SPEC_ALPHA_TEST_ENABLED, 1);
    info.Set(CKFF_SPEC_ALPHA_FUNC, VXCMP_GREATER);
    info.Set(CKFF_SPEC_SAMPLER_TYPE_MASK,
             CKFF_SAMPLER_CUBE | (CKFF_SAMPLER_VOLUME << 2));

    CKFFSpecUniform packed;
    CKFFPackSpecializationDwords(info, packed);

    const CKDWORD *dwords = info.Data();
    for (CKDWORD i = 0; i < CKFF_SPEC_UNIFORM_VEC4_COUNT; ++i) {
        CKDWORD unpacked = ((CKDWORD)packed.Values[i][0] & 0xFFu) |
                           (((CKDWORD)packed.Values[i][1] & 0xFFu) << 8) |
                           (((CKDWORD)packed.Values[i][2] & 0xFFu) << 16) |
                           (((CKDWORD)packed.Values[i][3] & 0xFFu) << 24);
        TestCheck(unpacked == dwords[i],
                  "u_ffSpec uniform byte mirror must round-trip every specialization dword");
    }
}

void PremodulateCoverageIsFallback() {
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_PREMODULATE) == CKFF_COVERAGE_FALLBACK,
              "PREMODULATE must remain marked as fallback coverage");
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_MODULATE) == CKFF_COVERAGE_EXACT,
              "MODULATE coverage must remain exact as a control case");
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
    const std::string contents = ReadTextFile("Source/RenderEngine/src/shaders/fs_ff_stage.sc");

    TestCheck(!contents.empty(),
              "FFP fragment shader source must be readable from the test working directory");
    TestCheck(contents.find("vec4 temp = vec4(0.0, 0.0, 0.0, 0.0)") != std::string::npos,
              "FFP TEMP register must initialize all channels to zero");
    TestCheck(contents.find("vec4 temp = vec4(0.0, 0.0, 0.0, 1.0)") == std::string::npos,
              "FFP TEMP alpha must not initialize to one");
}

void DepthTextureCompareUsesVxCompareOrdering() {
    const std::string contents = ReadTextFile("Source/RenderEngine/src/shaders/fs_ff_stage.sc");

    TestCheck(!contents.empty(),
              "FFP fragment shader source must be readable from the test working directory");
    TestCheck(contents.find("if (func == 1) return 0.0") != std::string::npos,
              "VXCMP_NEVER must always fail shader depth compares");
    TestCheck(contents.find("if (func == 2) return depth < ref ? 1.0 : 0.0") != std::string::npos,
              "VXCMP_LESS must use strict less-than shader depth compare");
    TestCheck(contents.find("if (func == 3) return depth == ref ? 1.0 : 0.0") != std::string::npos,
              "VXCMP_EQUAL must use equality shader depth compare");
    TestCheck(contents.find("if (func == 4) return depth <= ref ? 1.0 : 0.0") != std::string::npos,
              "VXCMP_LESSEQUAL must use less-or-equal shader depth compare");
    TestCheck(contents.find("if (func == 7) return depth >= ref ? 1.0 : 0.0") != std::string::npos,
              "VXCMP_GREATEREQUAL must use greater-or-equal shader depth compare");
    TestCheck(contents.find("if (func == 8) return 1.0") != std::string::npos,
              "VXCMP_ALWAYS must always pass shader depth compares");
}

void Runtime3DVertexShaderKeepsAdditionalTexcoordsActive() {
    RuntimeVertexShaderKeepsAdditionalTexcoordsActive(
        "Source/RenderEngine/src/shaders/vs_ff_3d.sc",
        "FFP 3D vertex shader source must be readable from the test working directory");
}

void RuntimePositionTVertexShaderKeepsAdditionalTexcoordsActive() {
    RuntimeVertexShaderKeepsAdditionalTexcoordsActive(
        "Source/RenderEngine/src/shaders/vs_ff_positiont.sc",
        "FFP POSITIONT vertex shader source must be readable from the test working directory");
}

void VertexBlendResolverMatchesDxvkWeightCounts() {
    CKFFVertexBlendState disabled = CKFFResolveVertexBlendState(
        VXVBLEND_DISABLE, FALSE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(disabled.Mode == CKFF_VERTEX_BLEND_DISABLED && disabled.Count == 0 && disabled.Supported,
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
    TestCheck(!missingIndices.Supported && missingIndices.Mode == CKFF_VERTEX_BLEND_DISABLED,
              "Indexed vertex blend must be unsupported without blend indices");

    CKFFVertexBlendState positionT = CKFFResolveVertexBlendState(
        VXVBLEND_2WEIGHTS, FALSE, CKFF_VF_POSITIONT | CKFF_VF_BLENDWEIGHT);
    TestCheck(!positionT.Supported && positionT.Mode == CKFF_VERTEX_BLEND_DISABLED,
              "POSITIONT must not enable vertex blend");

    CKFFVertexBlendState tween = CKFFResolveVertexBlendState(
        VXVBLEND_TWEENING, FALSE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(!tween.Supported && tween.Mode == CKFF_VERTEX_BLEND_DISABLED,
              "Tweening remains unsupported until second position/normal inputs exist");
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

    TestCheck(spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) ==
                  (CKFF_SAMPLER_CUBE | (CKFF_SAMPLER_DEPTH << 6)),
              "Sampler type specialization must pack two bits per stage");
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

    TestCheck((spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) & 0x3u) == CKFF_SAMPLER_VOLUME,
              "Volume sampler type must pack into specialization");
    TestCheck(((spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) >> 2) & 0x3u) == CKFF_SAMPLER_DEPTH,
              "Depth sampler type must remain packed independently");
    TestCheck(((spec.Get(CKFF_SPEC_SAMPLER_COMPARE_FUNC_MASK) >> 4) & 0xFu) == CKRST_COMPARE_LEQUAL,
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

void SamplerLayoutKeyNormalizesInactiveAndDepthStages() {
    CKFFShaderKeyFS key;
    key.Stages[0].HasTexture = true;
    key.Stages[0].SamplerType = CKFF_SAMPLER_DEPTH;
    key.Stages[1].HasTexture = false;
    key.Stages[1].SamplerType = CKFF_SAMPLER_CUBE;
    key.Stages[2].HasTexture = true;
    key.Stages[2].SamplerType = CKFF_SAMPLER_VOLUME;
    key.Stages[3].HasTexture = true;
    key.Stages[3].SamplerType = CKFF_SAMPLER_CUBE;

    CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key);

    TestCheck(CKFFSamplerLayoutStageType(layout, 0) == CKFF_SAMPLER_2D,
              "Depth samplers must use the 2D static sampler layout");
    TestCheck(CKFFSamplerLayoutStageType(layout, 1) == CKFF_SAMPLER_2D,
              "Inactive stages must not split static sampler layouts");
    TestCheck(CKFFSamplerLayoutStageType(layout, 2) == CKFF_SAMPLER_VOLUME,
              "Active volume stages must stay volume in the sampler layout");
    TestCheck(CKFFSamplerLayoutStageType(layout, 3) == CKFF_SAMPLER_CUBE,
              "Active cube stages must stay cube in the sampler layout");
    TestCheck(CKFFSamplerLayoutNeedsCubeSampler(layout) &&
                  CKFFSamplerLayoutNeedsVolumeSampler(layout) &&
                  CKFFSamplerLayoutNeedsMixedCubeVolume(layout),
              "Mixed cube+volume layouts must be detectable from the normalized key");
}

void FragmentShaderDeclaresStaticSamplerLayoutWithoutFullSpecialization() {
    const std::string contents = ReadTextFile("Source/RenderEngine/src/shaders/fs_ff_stage.sc");
    TestCheck(!contents.empty(),
              "FFP fragment shader source must be readable from the test working directory");

    const std::string staticLayout = "#elif defined(CKFF_STATIC_SAMPLER_LAYOUT)";
    const std::string fullSpecialized = "#if defined(CKFF_FULL_SPECIALIZED)";
    const std::string volumeLayout = "#elif defined(CKFF_VOLUME_SAMPLER_LAYOUT)";
    const std::string::size_type staticPos = contents.find(staticLayout);
    const std::string::size_type fullPos = contents.find(fullSpecialized);
    const std::string::size_type volumePos = contents.find(volumeLayout);

    TestCheck(staticPos != std::string::npos,
              "FFP fragment shader must declare a static sampler layout branch");
    TestCheck(fullPos != std::string::npos && staticPos != std::string::npos && fullPos < staticPos,
              "Static sampler layout must be separate from the full-specialized branch");
    TestCheck(staticPos != std::string::npos && volumePos != std::string::npos && staticPos < volumePos,
              "Static sampler layout must run before the volume-only runtime layout");
    TestCheck(contents.find("CKFF_STATIC_DEPTH_TEXTURE_COLOR") != std::string::npos,
              "Static sampler layout must keep depth compare in the 2D sampling path");
}

void SamplerLayoutCodegenUsesManifestInsteadOfDefaultEnumeration() {
    const std::string manifest = ReadTextFile("Source/RenderEngine/src/shaders/ffp_sampler_layouts.json");
    const std::string script = ReadTextFile("Source/RenderEngine/src/shaders/compile_shaders.py");
    const std::string generated = ReadTextFile("Source/RenderEngine/src/shaders/generated/CKFFSpecializedModuleTable.generated.h");
    struct BackendExpectation {
        const char *Name;
        const char *Profile;
    };
    const BackendExpectation backends[] = {
        {"dx11", "CKRST_SHADER_PROFILE_DX11"},
        {"dx12", "CKRST_SHADER_PROFILE_DX12"},
        {"spirv", "CKRST_SHADER_PROFILE_SPIRV"},
        {"glsl", "CKRST_SHADER_PROFILE_GLSL"},
        {"metal", "CKRST_SHADER_PROFILE_MSL"},
    };

    TestCheck(!manifest.empty(),
              "FFP sampler layout manifest must be present");
    TestCheck(manifest.find("\"stageTypes\": [3, 1, 0, 0, 0, 0, 0, 0]") != std::string::npos,
              "Sampler layout manifest must keep the exact observed volume+cube layout");
    TestCheck(manifest.find("\"backends\": [\"glsl\"]") == std::string::npos,
              "Sampler layout manifest must not remain GLSL-only");
    for (const BackendExpectation &backend : backends) {
        const std::string manifestName = std::string("\"") + backend.Name + "\"";
        TestCheck(manifest.find(manifestName) != std::string::npos,
                  "Sampler layout manifest must list every shader backend");
    }
    TestCheck(script.find("load_sampler_layout_manifest") != std::string::npos,
              "Shader codegen must load sampler layouts from the manifest");
    TestCheck(script.find("default_sampler_layout_variants") == std::string::npos &&
                  script.find("itertools.product((0, 1, 3), repeat=4)") == std::string::npos,
              "Shader codegen must not enumerate first-four mixed sampler layouts by default");
    TestCheck(script.find("SAMPLER_LAYOUT_SIZE_LIMIT_BYTES") != std::string::npos,
              "Shader codegen must enforce a small sampler-layout generated size budget");
    TestCheck(!generated.empty(),
              "Generated specialized module table must be readable");
    for (const BackendExpectation &backend : backends) {
        const std::string includePath = std::string("shaders/generated/") + backend.Name +
            "/sampler_layout/layout_volume_cube_2d_2d_2d_2d_2d_2d_fs_ff_stage.bin.h";
        TestCheck(generated.find(includePath) != std::string::npos,
                  "Generated table must include the exact sampler layout for every backend");
        TestCheck(generated.find(backend.Profile) != std::string::npos &&
                      generated.find(std::string("CKFFSamplerLayoutModule_") + backend.Name +
                                     "_layout_volume_cube_2d_2d_2d_2d_2d_2d") != std::string::npos,
                  "Generated table must expose a sampler-layout module entry for every profile");
    }
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
    tests.Run("Shader ABI constants match shader uniform declarations",
              &ShaderABIConstantsMatchShaderUniformDeclarations);
    tests.Run("Stage params pack through ABI indices",
              &StageParamsPackThroughABIIndices);
    tests.Run("Spec uniform mirrors specialization dwords as bytes",
              &SpecUniformMirrorsSpecializationDwordsAsBytes);
    tests.Run("PREMODULATE coverage is fallback",
              &PremodulateCoverageIsFallback);
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
    tests.Run("Depth texture compare uses VX compare ordering",
              &DepthTextureCompareUsesVxCompareOrdering);
    tests.Run("Runtime 3D vertex shader keeps additional texcoords active",
              &Runtime3DVertexShaderKeepsAdditionalTexcoordsActive);
    tests.Run("Runtime POSITIONT vertex shader keeps additional texcoords active",
              &RuntimePositionTVertexShaderKeepsAdditionalTexcoordsActive);
    tests.Run("Vertex blend resolver matches dxvk weight counts",
              &VertexBlendResolverMatchesDxvkWeightCounts);
    tests.Run("Vertex blend resolver rejects missing indexed input and POSITIONT",
              &VertexBlendResolverRejectsMissingIndexedInputAndPositionT);
    tests.Run("DP weight flags add blend layout flags",
              &DPWeightFlagsAddBlendLayoutFlags);
    tests.Run("Sampler types pack into specialization",
              &SamplerTypesPackIntoSpecialization);
    tests.Run("Volume sampler and compare func pack into specialization",
              &VolumeSamplerAndCompareFuncPackIntoSpecialization);
    tests.Run("Volume sampler mask can be derived from shader key",
              &VolumeSamplerMaskCanBeDerivedFromShaderKey);
    tests.Run("Sampler layout key normalizes inactive and depth stages",
              &SamplerLayoutKeyNormalizesInactiveAndDepthStages);
    tests.Run("Fragment shader declares static sampler layout without full specialization",
              &FragmentShaderDeclaresStaticSamplerLayoutWithoutFullSpecialization);
    tests.Run("Sampler layout codegen uses manifest instead of default enumeration",
              &SamplerLayoutCodegenUsesManifestInsteadOfDefaultEnumeration);
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
    tests.Run("Texture binding mask ignores inactive stages",
              &TextureBindingMaskIgnoresInactiveStages);
    return tests.ExitCode();
}
