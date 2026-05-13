#include "CKFFShaderCache.h"
#include "CKFFSpecializedModuleTable.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"

#include <cstdio>
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

CKFFShaderCache::CKFFShaderCache()
    : m_Context(nullptr), m_Target(), m_BlobSet(nullptr), m_UseUberShader(false),
      m_NextShaderHandle(100), m_NextProgramHandle(200), m_NextUniformHandle(300) {}

CKFFShaderCache::~CKFFShaderCache() {
    Shutdown();
}

void CKFFShaderCache::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_UseUberShader = CKRenderFFPSettings().GetBool("UberShader", false);
    CreateUniforms();
    ResolveShaderTarget();
}

void CKFFShaderCache::Shutdown() {
    if (m_Context) {
        for (CKFFProgramCacheTable::Iterator it = m_ProgramCache.Begin(); it != m_ProgramCache.End(); ++it) {
            if ((*it).Program)
                m_Context->DeleteObject((*it).Program, CKRST_OBJ_PROGRAM);
        }
        m_ProgramCache.Clear();
    }
    m_Context = nullptr;
    m_BlobSet = nullptr;
}

void CKFFShaderCache::CreateUniforms() {
    if (!m_Context) return;

    CKUniformDesc desc;

    desc.Type = CKRST_UNIFORM_MATRIX4;
    desc.Name = (char *)"u_ffMatrices";
    desc.Count = 8;
    m_Uniforms.u_ffMatrices = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ffMatrices, &desc);

    desc.Type = CKRST_UNIFORM_FLOAT4;
    desc.Name = (char *)"u_ffDrawParams";
    desc.Count = 19;
    m_Uniforms.u_ffDrawParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ffDrawParams, &desc);

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
    desc.Name = (char *)"u_ffVertexParams";
    desc.Count = 8;
    m_Uniforms.u_ffVertexParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ffVertexParams, &desc);

    desc.Name = (char *)"u_ffFragmentParams";
    desc.Count = 4;
    m_Uniforms.u_ffFragmentParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ffFragmentParams, &desc);

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
    desc.Count = CKFF_MAX_TEXTURE_STAGES * 2;
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
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        char name[32];
        std::snprintf(name, sizeof(name), "s_textureCube%d", i);
        desc.Name = name;
        m_Uniforms.s_textureCube[i] = AllocUniformHandle();
        m_Context->CreateUniform(m_Uniforms.s_textureCube[i], &desc);
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
    CK_LOG_FMT("ShaderCache", "FFP shader mode: backend=%s mode=%s",
               set->Name, m_UseUberShader ? "uber" : "full-specialized");
}

CKFFProgramBinding CKFFShaderCache::CreateVariantProgram(const CKFFShaderKey &key) {
    if (!m_UseUberShader)
        return CreateFullSpecializedProgram(key);
    return CreateUberSpecializedProgram(key);
}

CKFFProgramBinding CKFFShaderCache::CreateFullSpecializedProgram(const CKFFShaderKey &key) {
    CKFFSpecializedModule module;
    if (CKFFFindSpecializedModule(key, m_Target.Profile, module)) {
        CKDWORD program = CreateProgramFromBinary(
            m_Target,
            module.VSData, module.VSSize,
            module.FSData, module.FSSize,
            module.Specialization);
        return CKFFProgramBinding(program, program != 0, module.Specialization);
    }

    CKFFSpecializationInfo specInfo = CKFFBuildSpecializationInfo(key.FS);
    CK_LOG_FMT("ShaderCache",
               "Full FFP specialized module cache miss: backend=%s positionT=%u vsBits=%llu vsTexcoordDeclMask=%u vsTexGen0=%u vsTexGen1=%u vsTexGen2=%u vsTexGen3=%u vsTexGen4=%u vsTexGen5=%u vsTexGen6=%u vsTexGen7=%u vsTexCoordIndex0=%u vsTexCoordIndex1=%u vsTexCoordIndex2=%u vsTexCoordIndex3=%u vsTexCoordIndex4=%u vsTexCoordIndex5=%u vsTexCoordIndex6=%u vsTexCoordIndex7=%u vsTexTransformFlags0=%u vsTexTransformFlags1=%u vsTexTransformFlags2=%u vsTexTransformFlags3=%u vsTexTransformFlags4=%u vsTexTransformFlags5=%u vsTexTransformFlags6=%u vsTexTransformFlags7=%u lastStage=%u specular=%u alphaTest=%u alphaFunc=%u fog=%u projectedMask=%u specDword0=%u specDword1=%u specDword2=%u specDword3=%u specDword4=%u specDword5=%u specDword6=%u specDword7=%u specDword8=%u specDword9=%u",
               m_BlobSet ? static_cast<const CKFFShaderBlobSet *>(m_BlobSet)->Name : "unknown",
               key.VS.GetHasPositionT() ? 1u : 0u,
               (unsigned long long)key.VS.Bits,
               key.VS.VertexTexcoordDeclMask,
               key.VS.TexGen[0], key.VS.TexGen[1], key.VS.TexGen[2], key.VS.TexGen[3],
               key.VS.TexGen[4], key.VS.TexGen[5], key.VS.TexGen[6], key.VS.TexGen[7],
               key.VS.TexCoordIndex[0], key.VS.TexCoordIndex[1], key.VS.TexCoordIndex[2], key.VS.TexCoordIndex[3],
               key.VS.TexCoordIndex[4], key.VS.TexCoordIndex[5], key.VS.TexCoordIndex[6], key.VS.TexCoordIndex[7],
               key.VS.TexTransformFlags[0], key.VS.TexTransformFlags[1], key.VS.TexTransformFlags[2], key.VS.TexTransformFlags[3],
               key.VS.TexTransformFlags[4], key.VS.TexTransformFlags[5], key.VS.TexTransformFlags[6], key.VS.TexTransformFlags[7],
               key.FS.LastActiveTextureStage,
               key.FS.GlobalSpecularEnable ? 1u : 0u,
               key.FS.AlphaTestEnable ? 1u : 0u,
               key.FS.AlphaFunc,
               key.FS.FogEnable ? 1u : 0u,
               specInfo.Get(CKFF_SPEC_PROJECTED_SAMPLER_MASK),
               specInfo.Data()[0], specInfo.Data()[1], specInfo.Data()[2],
               specInfo.Data()[3], specInfo.Data()[4], specInfo.Data()[5],
               specInfo.Data()[6], specInfo.Data()[7], specInfo.Data()[8],
               specInfo.Data()[9]);
    return CreateUberSpecializedProgram(key);
}

CKFFProgramBinding CKFFShaderCache::CreateUberSpecializedProgram(const CKFFShaderKey &key) {
    const CKFFShaderBlobSet *set = static_cast<const CKFFShaderBlobSet *>(m_BlobSet);
    if (!set)
        return CKFFProgramBinding();

    CKFFSpecializationInfo specInfo = CKFFBuildSpecializationInfo(key.FS);
    const unsigned char *vsData = key.VS.GetHasPositionT() ? set->VSPositionT : set->VS3D;
    const unsigned int vsSize = key.VS.GetHasPositionT() ? set->VSPositionTSize : set->VS3DSize;
    CKDWORD program = CreateProgramFromBinary(
        m_Target, vsData, vsSize, set->FSStage, set->FSStageSize, specInfo);

    CK_LOG_FMT("ShaderCache",
               "FFP variant program: %u backend=%s ubershader=%u positionT=%u lastStage=%u specular=%u alphaTest=%u alphaFunc=%u fog=%u",
               program, set->Name, m_UseUberShader ? 1u : 0u, key.VS.GetHasPositionT() ? 1u : 0u,
               key.FS.LastActiveTextureStage, key.FS.GlobalSpecularEnable ? 1u : 0u,
               key.FS.AlphaTestEnable ? 1u : 0u, key.FS.AlphaFunc,
               key.FS.FogEnable ? 1u : 0u);
    return CKFFProgramBinding(program, false, specInfo);
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

CKFFProgramBinding CKFFShaderCache::GetProgram(const CKFFShaderKey &key) {
    CKFFProgramBinding binding;
    if (m_ProgramCache.LookUp(key, binding))
        return binding;

    binding = CreateVariantProgram(key);
    if (binding.Program) {
        m_ProgramCache.Insert(key, binding);
    }
    return binding;
}
