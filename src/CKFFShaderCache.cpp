#include "CKFFShaderCache.h"
#include "CKFFSpecializedModuleTable.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "shaders/generated/dx11/vs_ff_3d.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont.bin.h"
#include "shaders/generated/dx11/fs_ff_stage.bin.h"
#include "shaders/generated/dx12/vs_ff_3d.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont.bin.h"
#include "shaders/generated/dx12/fs_ff_stage.bin.h"
#include "shaders/generated/spirv/vs_ff_3d.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont.bin.h"
#include "shaders/generated/spirv/fs_ff_stage.bin.h"
#include "shaders/generated/glsl/vs_ff_3d.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont.bin.h"
#include "shaders/generated/glsl/fs_ff_stage.bin.h"

#ifndef CKRE_FFP_VARIANTS
#define CKRE_FFP_VARIANTS 1
#endif

#if !CKRE_FFP_VARIANTS
#error "CKRE_FFP_VARIANTS=OFF is unsupported by the clean-break pure FFP variant pipeline"
#endif

namespace {

struct CKFFShaderBlobSet {
    CK_SHADER_PROFILE Profile;
    const char *Name;
    const unsigned char *VS3D;
    unsigned int VS3DSize;
    const unsigned char *VSPositionT;
    unsigned int VSPositionTSize;
    const unsigned char *FSStage;
    unsigned int FSStageSize;
};

static const CKFFShaderBlobSet g_ShaderBlobSets[] = {
    {CKRST_SHADER_PROFILE_DX11, "dx11",
     s_dx11_vs_ff_3d, sizeof(s_dx11_vs_ff_3d),
     s_dx11_vs_ff_positiont, sizeof(s_dx11_vs_ff_positiont),
     s_dx11_fs_ff_stage, sizeof(s_dx11_fs_ff_stage)},
    {CKRST_SHADER_PROFILE_DX12, "dx12",
     s_dx12_vs_ff_3d, sizeof(s_dx12_vs_ff_3d),
     s_dx12_vs_ff_positiont, sizeof(s_dx12_vs_ff_positiont),
     s_dx12_fs_ff_stage, sizeof(s_dx12_fs_ff_stage)},
    {CKRST_SHADER_PROFILE_SPIRV, "spirv",
     s_spirv_vs_ff_3d, sizeof(s_spirv_vs_ff_3d),
     s_spirv_vs_ff_positiont, sizeof(s_spirv_vs_ff_positiont),
     s_spirv_fs_ff_stage, sizeof(s_spirv_fs_ff_stage)},
    {CKRST_SHADER_PROFILE_GLSL, "glsl",
     s_glsl_vs_ff_3d, sizeof(s_glsl_vs_ff_3d),
     s_glsl_vs_ff_positiont, sizeof(s_glsl_vs_ff_positiont),
     s_glsl_fs_ff_stage, sizeof(s_glsl_fs_ff_stage)},
};

static const CKFFShaderBlobSet *FindShaderBlobSet(CK_SHADER_PROFILE profile)
{
    if (profile == CKRST_SHADER_PROFILE_UNKNOWN)
        return nullptr;
    for (const CKFFShaderBlobSet &set : g_ShaderBlobSets) {
        if (set.Profile == profile)
            return &set;
    }
    return nullptr;
}

#if CKRE_FFP_VARIANT_CAPTURE
CKDWORD ReadEnvDword(const char *name, CKDWORD fallback)
{
    const char *value = std::getenv(name);
    if (!value || !*value)
        return fallback;

    char *end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value)
        return fallback;
    return (CKDWORD)parsed;
}

void LogVariantStage(CKDWORD stage, const CKFFShaderKeyFSStage &s)
{
    CK_LOG_FMT("ShaderCache",
               "FFP variant stage[%u]: cop=%u carg0=%u carg1=%u carg2=%u aop=%u aarg0=%u aarg1=%u aarg2=%u temp=%u",
               stage,
               s.ColorOp, s.ColorArg0, s.ColorArg1, s.ColorArg2,
               s.AlphaOp, s.AlphaArg0, s.AlphaArg1, s.AlphaArg2,
               s.ResultIsTemp ? 1u : 0u);
}

void LogVariantSpecDwords(const CKFFSpecializationInfo &spec)
{
    const CKDWORD *d = spec.Data();
    CK_LOG_FMT("ShaderCache",
               "FFP variant specDwords: [%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",
               d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9]);
}

void LogVariantManifestStage(CKDWORD stage, const CKFFShaderKeyFSStage &s)
{
    CK_LOG_RAW("          {");
    CK_LOG_RAW_FMT("            \"colorOp\": %u,", s.ColorOp);
    CK_LOG_RAW_FMT("            \"colorArg0\": %u,", s.ColorArg0);
    CK_LOG_RAW_FMT("            \"colorArg1\": %u,", s.ColorArg1);
    CK_LOG_RAW_FMT("            \"colorArg2\": %u,", s.ColorArg2);
    CK_LOG_RAW_FMT("            \"alphaOp\": %u,", s.AlphaOp);
    CK_LOG_RAW_FMT("            \"alphaArg0\": %u,", s.AlphaArg0);
    CK_LOG_RAW_FMT("            \"alphaArg1\": %u,", s.AlphaArg1);
    CK_LOG_RAW_FMT("            \"alphaArg2\": %u,", s.AlphaArg2);
    CK_LOG_RAW_FMT("            \"resultIsTemp\": %s", s.ResultIsTemp ? "true" : "false");
    CK_LOG_RAW_FMT("          }%s", stage + 1u < CKFF_STATE_DESC_TEXTURE_STAGES ? "," : "");
}

void LogVariantManifestCandidate(CKDWORD index, const CKFFShaderKey &key)
{
    CK_LOG_RAW("FFP_VARIANT_MANIFEST_CANDIDATE_BEGIN");
    CK_LOG_RAW("    {");
    CK_LOG_RAW_FMT("      \"name\": \"captured_%u\",", index);
    CK_LOG_RAW_FMT("      \"vs\": \"%s\",", key.VS.GetHasPositionT() ? "positiont" : "3d");
    CK_LOG_RAW("      \"key\": {");
    CK_LOG_RAW_FMT("        \"vsBits\": %llu,", (unsigned long long)key.VS.Bits);
    CK_LOG_RAW_FMT("        \"vsTexGen\": [%u, %u, %u, %u, %u, %u, %u, %u],",
                   key.VS.TexGen[0], key.VS.TexGen[1], key.VS.TexGen[2], key.VS.TexGen[3],
                   key.VS.TexGen[4], key.VS.TexGen[5], key.VS.TexGen[6], key.VS.TexGen[7]);
    CK_LOG_RAW_FMT("        \"lastActiveTextureStage\": %u,", key.FS.LastActiveTextureStage);
    CK_LOG_RAW_FMT("        \"globalSpecularEnable\": %s,", key.FS.GlobalSpecularEnable ? "true" : "false");
    CK_LOG_RAW("        \"stages\": [");
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage)
        LogVariantManifestStage(stage, key.FS.Stages[stage]);
    CK_LOG_RAW("        ]");
    CK_LOG_RAW("      }");
    CK_LOG_RAW("    }");
    CK_LOG_RAW("FFP_VARIANT_MANIFEST_CANDIDATE_END");
}
#endif

} // namespace

CKFFShaderCache::CKFFShaderCache()
    : m_Context(nullptr), m_Target(), m_BlobSet(nullptr), m_UseUberShader(true),
#if CKRE_FFP_VARIANT_CAPTURE
      m_VariantLogLimit(0), m_VariantLogCount(0),
#endif
      m_NextShaderHandle(100), m_NextProgramHandle(200), m_NextUniformHandle(300) {}

CKFFShaderCache::~CKFFShaderCache() {
    Shutdown();
}

void CKFFShaderCache::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    const char *uber = std::getenv("CK2_FFP_UBERSHADER");
    m_UseUberShader = !(uber && (std::strcmp(uber, "0") == 0 ||
                                 _stricmp(uber, "false") == 0 ||
                                 _stricmp(uber, "off") == 0 ||
                                 _stricmp(uber, "no") == 0));
#if CKRE_FFP_VARIANT_CAPTURE
    m_VariantLogLimit = ReadEnvDword("CK2_FFP_VARIANT_LOG_LIMIT", 0);
    m_VariantLogCount = 0;
    m_VariantStats.clear();
#endif
    CreateUniforms();
    ResolveShaderTarget();
}

void CKFFShaderCache::Shutdown() {
    if (m_Context) {
        for (auto &entry : m_ProgramCache) {
            if (entry.second)
                m_Context->DeleteObject(entry.second, CKRST_OBJ_PROGRAM);
        }
        m_ProgramCache.clear();
    }
#if CKRE_FFP_VARIANT_CAPTURE
    m_VariantStats.clear();
#endif
    m_Context = nullptr;
    m_BlobSet = nullptr;
}

void CKFFShaderCache::CreateUniforms() {
    if (!m_Context) return;

    CKUniformDesc desc;

    desc.Type = CKRST_UNIFORM_MATRIX4;
    desc.Name = (char *)"u_ckModelViewProj";
    desc.Count = 1;
    m_Uniforms.u_ckModelViewProj = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ckModelViewProj, &desc);

    desc.Name = (char *)"u_ckModel";
    desc.Count = 1;
    m_Uniforms.u_ckModel = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ckModel, &desc);

    desc.Name = (char *)"u_ckModelView";
    desc.Count = 1;
    m_Uniforms.u_ckModelView = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ckModelView, &desc);

    desc.Name = (char *)"u_ckNormalMatrix";
    desc.Count = 1;
    m_Uniforms.u_ckNormalMatrix = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ckNormalMatrix, &desc);

    desc.Name = (char *)"u_texMatrix";
    desc.Count = CKFF_MAX_TEXTURE_STAGES;
    m_Uniforms.u_texMatrix = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_texMatrix, &desc);

    desc.Type = CKRST_UNIFORM_FLOAT4;
    desc.Name = (char *)"u_lights";
    desc.Count = CKFF_MAX_LIGHTS * 7;
    m_Uniforms.u_lights = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_lights, &desc);

    desc.Name = (char *)"u_lightParams";
    desc.Count = 1;
    m_Uniforms.u_lightParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_lightParams, &desc);

    desc.Name = (char *)"u_material";
    desc.Count = 5;
    m_Uniforms.u_material = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_material, &desc);

    desc.Name = (char *)"u_ffParams";
    desc.Count = 1;
    m_Uniforms.u_ffParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ffParams, &desc);

    desc.Name = (char *)"u_lightModelParams";
    desc.Count = 1;
    m_Uniforms.u_lightModelParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_lightModelParams, &desc);

    desc.Name = (char *)"u_fogParams";
    desc.Count = 1;
    m_Uniforms.u_fogParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_fogParams, &desc);

    desc.Name = (char *)"u_fogColor";
    desc.Count = 1;
    m_Uniforms.u_fogColor = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_fogColor, &desc);

    desc.Name = (char *)"u_texFactor";
    desc.Count = 1;
    m_Uniforms.u_texFactor = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_texFactor, &desc);

    desc.Name = (char *)"u_alphaParams";
    desc.Count = 1;
    m_Uniforms.u_alphaParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_alphaParams, &desc);

    desc.Name = (char *)"u_bumpEnv";
    desc.Count = 2;
    m_Uniforms.u_bumpEnv = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_bumpEnv, &desc);

    desc.Name = (char *)"u_viewport";
    desc.Count = 1;
    m_Uniforms.u_viewport = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_viewport, &desc);

    desc.Name = (char *)"u_stageParams";
    desc.Count = CKFF_MAX_TEXTURE_STAGES * 4;
    m_Uniforms.u_stageParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_stageParams, &desc);

    desc.Name = (char *)"u_ffSpec";
    desc.Count = CKFFSpecializationInfo::MaxSpecDwords;
    m_Uniforms.u_ffSpec = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ffSpec, &desc);

    desc.Name = (char *)"u_clipPlanes";
    desc.Count = 6;
    m_Uniforms.u_clipPlanes = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_clipPlanes, &desc);

    desc.Name = (char *)"u_clipParams";
    desc.Count = 1;
    m_Uniforms.u_clipParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_clipParams, &desc);

    desc.Type = CKRST_UNIFORM_SAMPLER;
    desc.Count = 1;
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        char name[32];
        std::snprintf(name, sizeof(name), "s_texture%d", i);
        desc.Name = name;
        m_Uniforms.s_texture[i] = AllocUniformHandle();
        m_Context->CreateUniform(m_Uniforms.s_texture[i], &desc);
    }
}

void CKFFShaderCache::ResolveShaderTarget() {
    if (!m_Context || !m_Context->m_Driver) return;

    // This uses the driver backend selected by the active bgfx context. It is
    // not the public Virtools legacy shader-target API, which remains
    // unsupported when no bgfx context has supplied a concrete native target.
    CKERROR targetErr = m_Context->m_Driver->GetShaderTarget(&m_Target);
    if (targetErr != CK_OK) {
        CK_LOG_FMT("ShaderCache", "GetShaderTarget failed: err=%d", targetErr);
        return;
    }
    const CKFFShaderBlobSet *set = FindShaderBlobSet(m_Target.Profile);
    if (!set) {
        CK_LOG_FMT("ShaderCache", "No FFP shader set for format=0x%08X profile=0x%08X",
                   m_Target.Format, m_Target.Profile);
        return;
    }

    m_BlobSet = set;
    CK_LOG_FMT("ShaderCache", "FFP specialized variants enabled: backend=%s ubershader=%u",
               set->Name, m_UseUberShader ? 1u : 0u);
}

CKDWORD CKFFShaderCache::CreateVariantProgram(const CKFFShaderKey &key) {
    if (!m_UseUberShader)
        return CreateFullSpecializedProgram(key);
    return CreateUberSpecializedProgram(key);
}

void CKFFShaderCache::RecordVariantKey(const CKFFShaderKey &key) {
#if CKRE_FFP_VARIANT_CAPTURE
    CKDWORD &count = m_VariantStats[key];
    ++count;

    if (count != 1 || m_VariantLogLimit == 0 || m_VariantLogCount >= m_VariantLogLimit)
        return;
    ++m_VariantLogCount;

    CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key.FS);
    CK_LOG_FMT("ShaderCache",
               "FFP variant key[%u]: seen=%u positionT=%u vsBits=%llu lastStage=%u specular=%u",
               m_VariantLogCount, count, key.VS.GetHasPositionT() ? 1u : 0u,
               (unsigned long long)key.VS.Bits,
               key.FS.LastActiveTextureStage, key.FS.GlobalSpecularEnable ? 1u : 0u);
    CK_LOG_FMT("ShaderCache",
               "FFP variant texGen: [%u,%u,%u,%u,%u,%u,%u,%u]",
               key.VS.TexGen[0], key.VS.TexGen[1], key.VS.TexGen[2], key.VS.TexGen[3],
               key.VS.TexGen[4], key.VS.TexGen[5], key.VS.TexGen[6], key.VS.TexGen[7]);
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage)
        LogVariantStage(stage, key.FS.Stages[stage]);
    LogVariantSpecDwords(spec);
    LogVariantManifestCandidate(m_VariantLogCount, key);
#else
    (void)key;
#endif
}

CKDWORD CKFFShaderCache::CreateFullSpecializedProgram(const CKFFShaderKey &key) {
    CKFFSpecializedModule module;
    if (CKFFFindSpecializedModule(key, m_Target.Profile, module)) {
        return CreateProgramFromBinary(
            m_Target,
            module.VSData, module.VSSize,
            module.FSData, module.FSSize,
            module.Specialization);
    }

    CK_LOG_FMT("ShaderCache",
               "Full FFP specialized module cache miss: no generated module table is available for CK2_FFP_UBERSHADER=0");
    return 0;
}

CKDWORD CKFFShaderCache::CreateUberSpecializedProgram(const CKFFShaderKey &key) {
    const CKFFShaderBlobSet *set = static_cast<const CKFFShaderBlobSet *>(m_BlobSet);
    if (!set)
        return 0;

    CKFFSpecializationInfo specInfo = CKFFBuildSpecializationInfo(key.FS);
    const unsigned char *vsData = key.VS.GetHasPositionT() ? set->VSPositionT : set->VS3D;
    const unsigned int vsSize = key.VS.GetHasPositionT() ? set->VSPositionTSize : set->VS3DSize;
    CKDWORD program = CreateProgramFromBinary(
        m_Target, vsData, vsSize, set->FSStage, set->FSStageSize, specInfo);

    CK_LOG_FMT("ShaderCache",
               "FFP variant program: %u backend=%s ubershader=%u positionT=%u lastStage=%u specular=%u",
               program, set->Name, m_UseUberShader ? 1u : 0u, key.VS.GetHasPositionT() ? 1u : 0u,
               key.FS.LastActiveTextureStage, key.FS.GlobalSpecularEnable ? 1u : 0u);
    return program;
}

CKDWORD CKFFShaderCache::CreateProgramFromBinary(
    const CKShaderTargetDesc &target,
    const unsigned char *vsData, unsigned int vsSize,
    const unsigned char *fsData, unsigned int fsSize,
    const CKFFSpecializationInfo &specInfo)
{
    if (!m_Context) return 0;

    CKDWORD hVS = AllocShaderHandle();
    CKShaderDesc vsDesc = {};
    vsDesc.Stage = CKRST_SHADER_VERTEX;
    vsDesc.Format = target.Format;
    vsDesc.Profile = target.Profile;
    vsDesc.Code = (CKBYTE *)vsData;
    vsDesc.CodeSize = vsSize;
    CKERROR err = m_Context->CreateShader(hVS, &vsDesc);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateShader(VS) FAILED: err=%d handle=%u size=%u", err, hVS, vsSize);
        return 0;
    }

    CKDWORD hFS = AllocShaderHandle();
    CKShaderDesc fsDesc = {};
    fsDesc.Stage = CKRST_SHADER_PIXEL;
    fsDesc.Format = target.Format;
    fsDesc.Profile = target.Profile;
    fsDesc.Code = (CKBYTE *)fsData;
    fsDesc.CodeSize = fsSize;
    err = m_Context->CreateShader(hFS, &fsDesc);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateShader(FS) FAILED: err=%d handle=%u size=%u", err, hFS, fsSize);
        m_Context->DeleteObject(hVS, CKRST_OBJ_SHADER);
        return 0;
    }

    CKDWORD hProgram = AllocProgramHandle();
    CKProgramDesc progDesc = {};
    progDesc.VertexShader = hVS;
    progDesc.PixelShader = hFS;
    progDesc.ConsumeShaders = TRUE;
    progDesc.SpecializationDwords = specInfo.Data();
    progDesc.SpecializationDwordCount = specInfo.DwordCount();
    err = m_Context->CreateProgram(hProgram, &progDesc);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateProgram FAILED: err=%d vs=%u fs=%u", err, hVS, hFS);
        m_Context->DeleteObject(hVS, CKRST_OBJ_SHADER);
        m_Context->DeleteObject(hFS, CKRST_OBJ_SHADER);
        return 0;
    }

    CK_LOG_FMT("ShaderCache", "CreateProgram OK: program=%u vs=%u fs=%u", hProgram, hVS, hFS);
    return hProgram;
}

CKDWORD CKFFShaderCache::GetProgram(const CKFFShaderKey &key) {
    RecordVariantKey(key);

    auto it = m_ProgramCache.find(key);
    if (it != m_ProgramCache.end())
        return it->second;

    CKDWORD program = CreateVariantProgram(key);
    if (program)
        m_ProgramCache.emplace(key, program);
    return program;
}
