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

size_t CountOccurrences(const std::string &text, const std::string &needle) {
    if (needle.empty())
        return 0;

    size_t count = 0;
    std::string::size_type pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::vector<int> ParseJsonIntArray(const std::string &arrayText) {
    std::vector<int> values;
    int value = 0;
    bool inNumber = false;
    for (char ch : arrayText) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            value = value * 10 + (ch - '0');
            inNumber = true;
        } else if (inNumber) {
            values.push_back(value);
            value = 0;
            inNumber = false;
        }
    }
    if (inNumber)
        values.push_back(value);
    return values;
}

std::vector<std::string> ParseJsonStringArray(const std::string &arrayText) {
    std::vector<std::string> values;
    std::string current;
    bool inString = false;
    bool escaping = false;
    for (char ch : arrayText) {
        if (!inString) {
            if (ch == '"') {
                inString = true;
                current.clear();
            }
            continue;
        }

        if (escaping) {
            current.push_back(ch);
            escaping = false;
        } else if (ch == '\\') {
            escaping = true;
        } else if (ch == '"') {
            values.push_back(current);
            inString = false;
        } else {
            current.push_back(ch);
        }
    }
    return values;
}

std::string SamplerLayoutIdentifier(const std::vector<int> &stageTypes) {
    std::ostringstream out;
    out << "layout";
    for (int type : stageTypes) {
        if (type == CKFF_SAMPLER_CUBE) {
            out << "_cube";
        } else if (type == CKFF_SAMPLER_VOLUME) {
            out << "_volume";
        } else {
            out << "_2d";
        }
    }
    return out.str();
}

const char *SamplerLayoutProfileName(const std::string &backend) {
    if (backend == "dx11") return "CKRST_SHADER_PROFILE_DX11";
    if (backend == "dx12") return "CKRST_SHADER_PROFILE_DX12";
    if (backend == "spirv") return "CKRST_SHADER_PROFILE_SPIRV";
    if (backend == "glsl") return "CKRST_SHADER_PROFILE_GLSL";
    if (backend == "essl") return "CKRST_SHADER_PROFILE_ESSL";
    if (backend == "metal") return "CKRST_SHADER_PROFILE_MSL";
    return "";
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

    CKDWORD textureFlags[CKFF_MAX_TEXTURE_STAGES] = {};
    CKFFPackStageParams(stages, textures, textureFlags, 3, 0, params);

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

void ShaderSourcesDeclarePortableFlatAndClipSpaceContracts() {
    const std::string varying = ReadTextFile("Source/RenderEngine/src/shaders/varying.def.sc");
    const std::string compiler = ReadTextFile("Source/RenderEngine/src/shaders/compile_shaders.py");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/shaders/vs_ff_positiont.sc");

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

void RenderTargetFlipFlagFollowsBackendOrigin() {
    CKDWORD stages[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    CKDWORD textures[CKFF_MAX_TEXTURE_STAGES] = {};
    CKDWORD textureFlags[CKFF_MAX_TEXTURE_STAGES] = {};
    CKFFStageParamsUniform params;

    stages[0][CKRST_TSS_OP] = CKRST_TOP_SELECTARG1;
    stages[0][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
    textures[0] = 77;
    textureFlags[0] = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RENDERTARGET;

    CKFFPackStageParams(stages, textures, textureFlags, 1, 0, params);
    const CKDWORD topLeftFlags = (CKDWORD)params.Values[
        CKFFStageParamIndex(0, CKFF_STAGE_PARAM_COLOR_EXTRA)][2];
    TestCheck((topLeftFlags & CKFF_TTF_RENDER_TARGET_FLIP_V) == 0,
              "Top-left backends must sample render targets without a V flip");

    CKFFPackStageParams(stages, textures, textureFlags, 1,
                        CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT, params);
    const CKDWORD bottomLeftFlags = (CKDWORD)params.Values[
        CKFFStageParamIndex(0, CKFF_STAGE_PARAM_COLOR_EXTRA)][2];
    TestCheck((bottomLeftFlags & CKFF_TTF_RENDER_TARGET_FLIP_V) != 0,
              "Bottom-left backends must flip 2D render-target sampling in V");

    textureFlags[0] = CKRST_TEXTURE_VALID;
    CKFFPackStageParams(stages, textures, textureFlags, 1,
                        CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT, params);
    const CKDWORD regularTextureFlags = (CKDWORD)params.Values[
        CKFFStageParamIndex(0, CKFF_STAGE_PARAM_COLOR_EXTRA)][2];
    TestCheck((regularTextureFlags & CKFF_TTF_RENDER_TARGET_FLIP_V) == 0,
              "Bottom-left backends must not flip ordinary texture assets");

    textureFlags[0] = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RENDERTARGET |
                      CKRST_TEXTURE_CUBEMAP;
    CKFFPackStageParams(stages, textures, textureFlags, 1,
                        CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT, params);
    const CKDWORD cubeFlags = (CKDWORD)params.Values[
        CKFFStageParamIndex(0, CKFF_STAGE_PARAM_COLOR_EXTRA)][2];
    TestCheck((cubeFlags & CKFF_TTF_RENDER_TARGET_FLIP_V) == 0,
              "Cube render targets must not receive an invalid 2D V flip");
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
    CKFFPackStageParams(stages, textures, textureFlags, 6, 0, params);

    const float *stage1 = params.Values[CKFFStageParamIndex(1, CKFF_STAGE_PARAM_COLOR_EXTRA)];
    const CKDWORD stage1Flags = (CKDWORD)stage1[2];
    TestCheck(stage1[1] == (float)CKFFPackTexcoordIndex(3, CKFF_TEXGEN_NONE),
              "MIRRORONCE packing must not overwrite packed texcoord index");
    TestCheck((stage1Flags & 0x1ffu) == (CKRST_TTF_COUNT2 | CKRST_TTF_PROJECTED),
              "MIRRORONCE packing must preserve transform count and projected bits");
    TestCheck((stage1Flags & CKFF_TTF_MIRRORONCE_MASK) == (CKFF_TTF_MIRRORONCE_U | CKFF_TTF_MIRRORONCE_W),
              "Inherited MIRRORONCE must apply per-axis override rules before packing");

    const float *stage5 = params.Values[CKFFStageParamIndex(5, CKFF_STAGE_PARAM_COLOR_EXTRA)];
    const CKDWORD stage5Flags = (CKDWORD)stage5[2];
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

void MirrorOnceSpecializationPacksFirstFourStages() {
    CKFFFSStateDesc desc;
    for (CKDWORD stage = 0; stage < 4; ++stage) {
        desc.SetStageColorOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
        desc.SetStageAlphaArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageMirrorOnceMask(stage, stage + 1);
    }

    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0x0Fu);
    CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);
    const CKDWORD expectedMask = 1u | (2u << 3) | (3u << 6) | (4u << 9);

    TestCheck(key.Stages[3].MirrorOnceMask == 4,
              "Shader key must preserve per-stage MIRRORONCE mask");
    TestCheck(spec.Get(CKFF_SPEC_MIRRORONCE_SAMPLER_MASK) == expectedMask,
              "Full-specialized spec dwords must pack stage 0..3 MIRRORONCE masks");
}

void LastActiveTextureStageSpecializationRoundTrips() {
    CKFFSpecializationInfo spec;
    for (CKDWORD lastStage = 0; lastStage < 8; ++lastStage) {
        spec.Set(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE, lastStage);
        TestCheck(spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == lastStage,
                  "Last active texture stage must round-trip through specialization dwords");
    }
}

void FullSpecializedRejectsRuntimeOnlyTextureStagesBeforeLookup() {
    const std::string shaderCache = ReadTextFile("Source/RenderEngine/src/CKFFShaderCache.cpp");
    TestCheck(!shaderCache.empty(),
              "Shader cache source must be readable");

    const std::string functionNeedle = "CKFFProgramBinding CKFFShaderCache::CreateFullSpecializedProgram";
    const std::string guardNeedle = "key.FS.LastActiveTextureStage > 3";
    const std::string lookupNeedle = "CKFFFindSpecializedModule";
    const std::string::size_type functionStart = shaderCache.find(functionNeedle);
    const std::string::size_type guardPos = shaderCache.find(guardNeedle, functionStart);
    const std::string::size_type lookupPos = shaderCache.find(lookupNeedle, functionStart);

    TestCheck(functionStart != std::string::npos &&
                  guardPos != std::string::npos &&
                  lookupPos != std::string::npos &&
                  guardPos < lookupPos,
              "Full-specialized stage 4..7 guard must run before generated module lookup");
}

void MirrorOnceShaderSourceAppliesOnlyTo2DAndVolume() {
    const std::string fs = ReadTextFile("Source/RenderEngine/src/shaders/fs_ff_stage.sc");
    const std::string common = ReadTextFile("Source/RenderEngine/src/shaders/fs_ff_common.sc");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/shaders/vs_ff_3d.sc");
    const std::string vsPositionT = ReadTextFile("Source/RenderEngine/src/shaders/vs_ff_positiont.sc");

    TestCheck(!fs.empty() && !common.empty() && !vs3d.empty() && !vsPositionT.empty(),
              "FFP shader sources must be readable");
    TestCheck(common.find("int MirrorOnceMask;") != std::string::npos &&
                  common.find("ckffSpecMirrorOnceMask") != std::string::npos &&
                  common.find(">> uint(9)") != std::string::npos,
              "Fragment common shader must read MIRRORONCE masks from spec/runtime stage params");
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
    const std::string fs = ReadTextFile("Source/RenderEngine/src/shaders/fs_ff_stage.sc");

    TestCheck(!fs.empty(),
              "FFP fragment shader source must be readable from the test working directory");
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

void PremodulateCoverageIsExact() {
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_PREMODULATE) == CKFF_COVERAGE_EXACT,
              "PREMODULATE must be marked as exact coverage");
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

    CKFFVertexBlendState tween = CKFFResolveVertexBlendState(
        VXVBLEND_TWEENING, FALSE, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT);
    TestCheck(!tween.Supported && tween.Mode == CKFF_VERTEX_BLEND_DISABLED &&
                  tween.UnsupportedReason == CKFF_VERTEX_BLEND_UNSUPPORTED_TWEENING,
              "Tweening must report a distinct unsupported reason until second position/normal inputs exist");
}

void TweeningDiagnosticsAndShaderRemainPreImplementation() {
    const std::string debug = ReadTextFile("Source/RenderEngine/src/CKFFDebug.cpp");
    const std::string packet = ReadTextFile("Source/RenderEngine/src/CKFFOpaquePacketCoordinator.cpp");
    const std::string vs3d = ReadTextFile("Source/RenderEngine/src/shaders/vs_ff_3d.sc");
    const std::string layout = ReadTextFile("Source/RenderEngine/src/CKVertexLayoutCache.cpp");
    const std::string transient = ReadTextFile("Source/RenderEngine/src/CKTransientGeometry.cpp");
    const std::string mesh = ReadTextFile("Source/RenderEngine/src/CKMesh.cpp");
    const std::string vertexBuffer = ReadTextFile("Source/RenderEngine/src/CKVertexBuffer.cpp");

    TestCheck(!debug.empty() && !packet.empty() && !vs3d.empty() &&
                  !layout.empty() && !transient.empty() && !mesh.empty() &&
                  !vertexBuffer.empty(),
              "TWEENING diagnostic source files must be readable");
    TestCheck(debug.find("vertexBlend=%s(%u)") != std::string::npos &&
                  debug.find("TWEENING") != std::string::npos &&
                  debug.find("positionStride=%u") != std::string::npos &&
                  debug.find("dpFlags=0x%X") != std::string::npos &&
                  debug.find("path=DrawPrimitive") != std::string::npos &&
                  debug.find("path=DrawVertexBuffer") != std::string::npos,
              "FFP diagnostics must expose enough TWEENING context to locate missing tween inputs");
    TestCheck(packet.find("CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND_TWEENING") != std::string::npos,
              "Opaque packet coordinator must keep TWEENING separate from normal vertex blend rejects");
    TestCheck(vs3d.find("vertexBlendMode == 1") != std::string::npos &&
                  vs3d.find("vertexBlendMode == 2") == std::string::npos &&
                  vs3d.find("a_tween") == std::string::npos,
              "Vertex shader must not fake TWEENING before second position/normal inputs are wired");
    TestCheck(layout.find("CKFF_VF_TWEENPOSITION") == std::string::npos &&
                  transient.find("CKFF_VF_TWEENPOSITION") == std::string::npos,
              "Vertex layout/interleave code must not claim tween input support before a data source exists");
    TestCheck(mesh.find("TweenPosition") == std::string::npos &&
                  vertexBuffer.find("TweenPosition") == std::string::npos,
              "Current mesh and vertex-buffer paths must not hide a second tween stream");
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

void SamplerLayoutMissDiagnosticsAreActionable() {
    CKFFShaderKeyFS key;
    key.Stages[0].HasTexture = true;
    key.Stages[0].SamplerType = CKFF_SAMPLER_VOLUME;
    key.Stages[2].HasTexture = true;
    key.Stages[2].SamplerType = CKFF_SAMPLER_CUBE;

    const CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key);
    char stageTypes[32];
    char manifestEntry[128];
    CKFFFormatSamplerLayoutStageTypes(layout, stageTypes, sizeof(stageTypes));
    CKFFFormatSamplerLayoutManifestEntry(layout, "dx11", manifestEntry, sizeof(manifestEntry));

    TestCheck(std::strcmp(stageTypes, "[3,0,1,0,0,0,0,0]") == 0,
              "Sampler layout diagnostics must report all eight normalized stage types");
    TestCheck(std::strcmp(manifestEntry,
                          "{\"backends\":[\"dx11\"],\"stageTypes\":[3,0,1,0,0,0,0,0]}") == 0,
              "Sampler layout diagnostics must emit a copyable manifest entry");

    const std::string shaderCache = ReadTextFile("Source/RenderEngine/src/CKFFShaderCache.cpp");
    TestCheck(shaderCache.find("FFP static sampler layout miss") != std::string::npos,
              "Static sampler layout miss must be logged");
    TestCheck(shaderCache.find("profile=0x%08X") != std::string::npos &&
                  shaderCache.find("lastStage=%u") != std::string::npos &&
                  shaderCache.find("activeTextureMask=0x%02X") != std::string::npos &&
                  shaderCache.find("layout=0x%04X") != std::string::npos &&
                  shaderCache.find("stageTypes=%s") != std::string::npos &&
                  shaderCache.find("mixedCubeVolume=%u") != std::string::npos &&
                  shaderCache.find("manifestEntry=%s") != std::string::npos,
              "Static sampler layout miss log must include enough context to reproduce and patch the manifest");
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

void CMakeShaderSourcesIncludeSamplerLayoutManifest() {
    const std::string cmake = ReadTextFile("Source/RenderEngine/src/CMakeLists.txt");
    TestCheck(!cmake.empty(),
              "RenderEngine CMakeLists must be readable from the test working directory");
    TestCheck(cmake.find("${CMAKE_CURRENT_SOURCE_DIR}/shaders/ffp_sampler_layouts.json") != std::string::npos,
              "Shader source dependencies must include the sampler layout manifest");
    TestCheck(cmake.find("DEPENDS ${CKRE_SHADERC_DEPENDS} ${CKRE_SHADER_SOURCES}") != std::string::npos,
              "Shader generation must depend on CKRE_SHADER_SOURCES");
    TestCheck(cmake.find("SOURCES ${CKRE_SHADER_SOURCES}") != std::string::npos,
              "Shader generation target must expose CKRE_SHADER_SOURCES");
}

void SamplerLayoutCodegenUsesManifestInsteadOfDefaultEnumeration() {
    const std::string manifest = ReadTextFile("Source/RenderEngine/src/shaders/ffp_sampler_layouts.json");
    const std::string script = ReadTextFile("Source/RenderEngine/src/shaders/compile_shaders.py");
    const std::string generated = ReadTextFile("Source/RenderEngine/src/shaders/generated/CKFFSpecializedModuleTable.generated.h");

    TestCheck(!manifest.empty(),
              "FFP sampler layout manifest must be present");
    TestCheck(manifest.find("\"stageTypes\": [3, 1, 0, 0, 0, 0, 0, 0]") != std::string::npos,
              "Sampler layout manifest must keep the exact observed volume+cube layout");
    TestCheck(manifest.find("\"backends\": [\"glsl\"]") == std::string::npos,
              "Sampler layout manifest must not remain GLSL-only");
    TestCheck(script.find("load_sampler_layout_manifest") != std::string::npos,
              "Shader codegen must load sampler layouts from the manifest");
    TestCheck(script.find("default_sampler_layout_variants") == std::string::npos &&
                  script.find("itertools.product((0, 1, 3), repeat=4)") == std::string::npos,
              "Shader codegen must not enumerate first-four mixed sampler layouts by default");
    TestCheck(script.find("SAMPLER_LAYOUT_SIZE_LIMIT_BYTES") != std::string::npos,
              "Shader codegen must enforce a small sampler-layout generated size budget");
    TestCheck(!generated.empty(),
              "Generated specialized module table must be readable");

    size_t checkedBackendLayouts = 0;
    std::string::size_type objectStart = 0;
    while ((objectStart = manifest.find('{', objectStart)) != std::string::npos) {
        const std::string::size_type objectEnd = manifest.find('}', objectStart);
        TestCheck(objectEnd != std::string::npos,
                  "Sampler layout manifest entries must be closed JSON objects");
        const std::string object = manifest.substr(objectStart, objectEnd - objectStart + 1);
        objectStart = objectEnd + 1;

        const std::string::size_type stageKey = object.find("\"stageTypes\"");
        if (stageKey == std::string::npos)
            continue;

        const std::string::size_type stageArrayStart = object.find('[', stageKey);
        const std::string::size_type stageArrayEnd = object.find(']', stageArrayStart);
        const std::string::size_type backendsKey = object.find("\"backends\"");
        const std::string::size_type backendsArrayStart = object.find('[', backendsKey);
        const std::string::size_type backendsArrayEnd = object.find(']', backendsArrayStart);
        TestCheck(stageArrayStart != std::string::npos && stageArrayEnd != std::string::npos &&
                      backendsKey != std::string::npos && backendsArrayStart != std::string::npos &&
                      backendsArrayEnd != std::string::npos,
                  "Sampler layout manifest entries must include backends and stageTypes arrays");

        const std::vector<int> stageTypes = ParseJsonIntArray(
            object.substr(stageArrayStart, stageArrayEnd - stageArrayStart + 1));
        const std::vector<std::string> backends = ParseJsonStringArray(
            object.substr(backendsArrayStart, backendsArrayEnd - backendsArrayStart + 1));
        TestCheck(stageTypes.size() == CKFF_STATE_DESC_TEXTURE_STAGES,
                  "Sampler layout manifest stageTypes must describe every FFP texture stage");
        TestCheck(!backends.empty(),
                  "Sampler layout manifest entries must list at least one backend");

        const std::string identifier = SamplerLayoutIdentifier(stageTypes);
        for (const std::string &backend : backends) {
            const char *profile = SamplerLayoutProfileName(backend);
            TestCheck(profile[0] != '\0',
                      "Sampler layout manifest backend must map to a shader profile");

            const std::string headerPath = std::string("shaders/generated/") + backend +
                "/sampler_layout/" + identifier + "_fs_ff_stage.bin.h";
            TestCheck(generated.find(headerPath) != std::string::npos,
                      "Generated table must include every manifest sampler layout header");
            TestCheck(!ReadTextFile((std::string("Source/RenderEngine/src/") + headerPath).c_str()).empty(),
                      "Every generated sampler layout table include must point at an existing binary header");

            const std::string moduleName = std::string("CKFFSamplerLayoutModule_") + backend + "_" + identifier;
            const std::string entryNeedle = std::string("{ ") + profile + ", CKFFSamplerLayoutKey_" +
                identifier + "(), " + moduleName + "() }";
            TestCheck(generated.find(moduleName) != std::string::npos &&
                          generated.find(entryNeedle) != std::string::npos,
                      "Generated table must register every manifest sampler layout backend");
            ++checkedBackendLayouts;
        }
    }
    TestCheck(checkedBackendLayouts > 0,
              "Sampler layout manifest consistency test must inspect at least one backend layout");
    TestCheck(CountOccurrences(generated, "static const CKFFSamplerLayoutModuleEntry g_CKFFSamplerLayoutModuleEntries[]") == 1,
              "Generated table must contain one sampler-layout module entry table");
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
    tests.Run("Render-target flip flag follows backend origin",
              &RenderTargetFlipFlagFollowsBackendOrigin);
    tests.Run("MIRRORONCE address modes pack into stage params",
              &MirrorOnceAddressModesPackIntoStageParams);
    tests.Run("MIRRORONCE sampler desc falls back to clamp",
              &MirrorOnceSamplerDescFallsBackToClamp);
    tests.Run("MIRRORONCE specialization packs first four stages",
              &MirrorOnceSpecializationPacksFirstFourStages);
    tests.Run("Last active texture stage specialization round trips",
              &LastActiveTextureStageSpecializationRoundTrips);
    tests.Run("Full-specialized rejects runtime-only texture stages before lookup",
              &FullSpecializedRejectsRuntimeOnlyTextureStagesBeforeLookup);
    tests.Run("MIRRORONCE shader source applies only to 2D and volume",
              &MirrorOnceShaderSourceAppliesOnlyTo2DAndVolume);
    tests.Run("Texture combiner op formulas stay DXVK compatible",
              &TextureCombinerOpFormulasStayDxvkCompatible);
    tests.Run("Spec uniform mirrors specialization dwords as bytes",
              &SpecUniformMirrorsSpecializationDwordsAsBytes);
    tests.Run("PREMODULATE coverage is exact",
              &PremodulateCoverageIsExact);
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
    tests.Run("TWEENING diagnostics and shader remain pre-implementation",
              &TweeningDiagnosticsAndShaderRemainPreImplementation);
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
    tests.Run("Sampler layout miss diagnostics are actionable",
              &SamplerLayoutMissDiagnosticsAreActionable);
    tests.Run("Fragment shader declares static sampler layout without full specialization",
              &FragmentShaderDeclaresStaticSamplerLayoutWithoutFullSpecialization);
    tests.Run("CMake shader sources include sampler layout manifest",
              &CMakeShaderSourcesIncludeSamplerLayoutManifest);
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
