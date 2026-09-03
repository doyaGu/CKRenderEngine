#include "CKFFShaderCache.h"
#include "CKFFShaderABI.h"
#include "CKRasterizerDevice.h"
#include "CKDebugLogger.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "shaders/generated/CKFFShaderABI.generated.h"
#include "shaders/generated/dx11/vs_ff_3d.bin.h"
#include "shaders/generated/dx11/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx11/fs_ff_stage.bin.h"
#include "shaders/generated/dx12/vs_ff_3d.bin.h"
#include "shaders/generated/dx12/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx12/fs_ff_stage.bin.h"
#include "shaders/generated/spirv/vs_ff_3d.bin.h"
#include "shaders/generated/spirv/vs_ff_3d_clip.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/spirv/fs_ff_stage.bin.h"
#include "shaders/generated/glsl/vs_ff_3d.bin.h"
#include "shaders/generated/glsl/vs_ff_3d_clip.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/glsl/fs_ff_stage.bin.h"
#include "shaders/generated/essl/vs_ff_3d.bin.h"
#include "shaders/generated/essl/vs_ff_3d_clip.bin.h"
#include "shaders/generated/essl/vs_ff_positiont.bin.h"
#include "shaders/generated/essl/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/essl/fs_ff_stage.bin.h"
#include "shaders/generated/metal/vs_ff_3d.bin.h"
#include "shaders/generated/metal/vs_ff_3d_clip.bin.h"
#include "shaders/generated/metal/vs_ff_positiont.bin.h"
#include "shaders/generated/metal/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/metal/fs_ff_stage.bin.h"

void CKFFInitProgramContext(CKFFProgramContext *context,
                            const CKFFShaderKey &key,
                            const CKFFProgramBinding &binding)
{
    if (!context)
        return;
    context->ShaderKey = key;
    context->Binding = binding;
    context->Program = binding.Program;
    context->Specialization = binding.Specialization;
}

struct CKFFShaderBlob {
    const unsigned char *Data;
    unsigned int Size;
};

struct CKFFShaderBlobSet {
    CK_SHADER_PROFILE Profile;
    const char *Name;
    CKFFShaderBlob VS[CKFF_PROGRAM_VARIANT_COUNT]; // indexed by CKFFProgramVariant
    CKFFShaderBlob FS;
};

#define CKFF_BLOB(name) {name, sizeof(name)}
#define CKFF_BLOB_SET(profile, backend) \
    {profile, #backend, \
     {CKFF_BLOB(s_##backend##_vs_ff_3d), CKFF_BLOB(s_##backend##_vs_ff_3d_clip), \
      CKFF_BLOB(s_##backend##_vs_ff_positiont), CKFF_BLOB(s_##backend##_vs_ff_positiont_clip)}, \
     CKFF_BLOB(s_##backend##_fs_ff_stage)}

static const CKFFShaderBlobSet g_ShaderBlobSets[] = {
    CKFF_BLOB_SET(CKRST_SHADER_PROFILE_DX11, dx11),
    CKFF_BLOB_SET(CKRST_SHADER_PROFILE_DX12, dx12),
    CKFF_BLOB_SET(CKRST_SHADER_PROFILE_SPIRV, spirv),
    CKFF_BLOB_SET(CKRST_SHADER_PROFILE_GLSL, glsl),
    CKFF_BLOB_SET(CKRST_SHADER_PROFILE_ESSL, essl),
    CKFF_BLOB_SET(CKRST_SHADER_PROFILE_MSL, metal),
};

#undef CKFF_BLOB_SET
#undef CKFF_BLOB

static const char *const g_ProgramVariantNames[CKFF_PROGRAM_VARIANT_COUNT] = {
    "3d", "3d_clip", "positiont", "positiont_clip"
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
    : m_Context(nullptr), m_Target(), m_BlobSet(nullptr), m_SamplerLayout() {
    memset(m_Programs, 0, sizeof(m_Programs));
}

CKFFShaderCache::~CKFFShaderCache() {
    Shutdown();
}

bool CKFFShaderCache::Init(CKRasterizerDevice *ctx) {
    Shutdown();
    m_Context = ctx;
    if (!ResolveShaderTarget() || !CreateUniforms()) {
        Shutdown();
        return false;
    }
    BuildSamplerLayout();
    return true;
}

void CKFFShaderCache::Shutdown() {
    if (m_Context) {
        for (CKDWORD i = 0; i < CKFF_PROGRAM_VARIANT_COUNT; ++i) {
            if (m_Programs[i])
                m_Context->DeleteObject(m_Programs[i], CKRST_OBJ_PROGRAM);
        }
        CKDWORD uniforms[sizeof(m_Uniforms) / sizeof(CKDWORD)];
        memcpy(uniforms, &m_Uniforms, sizeof(uniforms));
        const size_t uniformCount = sizeof(uniforms) / sizeof(uniforms[0]);
        for (size_t i = 0; i < uniformCount; ++i) {
            if (uniforms[i])
                m_Context->DeleteObject(uniforms[i], CKRST_OBJ_UNIFORM);
        }
    }
    memset(m_Programs, 0, sizeof(m_Programs));
    m_SamplerLayout = CKFFProgramSamplerLayout();
    m_Uniforms = CKFFUniformHandles();
    m_Context = nullptr;
    m_BlobSet = nullptr;
}

bool CKFFShaderCache::CreateUniforms() {
    if (!m_Context) return false;

    CKUniformDesc desc;

    desc.Type = CKRST_UNIFORM_MAT4;
    desc.Name = (char *)"u_ffMatrices";
    desc.Count = CKFF_MATRIX_VEC4_COUNT;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ffMatrices);

    desc.Name = (char *)"u_vertexBlendMatrices";
    desc.Count = CKFF_VERTEX_BLEND_MATRIX_COUNT;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_vertexBlendMatrices);

    desc.Type = CKRST_UNIFORM_VEC4;
    desc.Name = (char *)"u_ffDrawParams";
    desc.Count = CKFF_DRAW_PARAM_VEC4_COUNT;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ffDrawParams);

    desc.Type = CKRST_UNIFORM_MAT4;
    desc.Name = (char *)"u_texMatrix";
    desc.Count = CKFF_MAX_TEXTURE_STAGES;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_texMatrix);

    desc.Type = CKRST_UNIFORM_VEC4;
    desc.Name = (char *)"u_lights";
    desc.Count = CKFF_MAX_LIGHTS * 7;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_lights);

    desc.Name = (char *)"u_bumpEnv";
    desc.Count = CKFF_MAX_TEXTURE_STAGES * 2;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_bumpEnv);

    desc.Name = (char *)"u_viewport";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_viewport);

    desc.Name = (char *)"u_stageParams";
    desc.Count = CKFF_STAGE_PARAM_VEC4_COUNT;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_stageParams);

    desc.Name = (char *)"u_ffSpec";
    desc.Count = CKFF_SPEC_UNIFORM_VEC4_COUNT;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ffSpec);

    desc.Name = (char *)"u_clipPlanes";
    desc.Count = CKFF_CLIP_PLANE_COUNT;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_clipPlanes);

    desc.Name = (char *)"u_clipParams";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_clipParams);

    desc.Type = CKRST_UNIFORM_SAMPLER;
    desc.Count = 1;
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "s_texture%d", i);
        desc.Name = name;
        m_Context->CreateUniform(&desc, &m_Uniforms.s_texture[i]);
    }
    for (int i = 0; i < CKFF_CUBE_SAMPLER_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof(name), "s_textureCube%d", i);
        desc.Name = name;
        m_Context->CreateUniform(&desc, &m_Uniforms.s_textureCube[i]);
    }
    for (int i = 0; i < CKFF_VOLUME_SAMPLER_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof(name), "s_textureVolume%d", i);
        desc.Name = name;
        m_Context->CreateUniform(&desc, &m_Uniforms.s_textureVolume[i]);
    }

    CKDWORD uniforms[sizeof(m_Uniforms) / sizeof(CKDWORD)];
    memcpy(uniforms, &m_Uniforms, sizeof(uniforms));
    const size_t uniformCount = sizeof(uniforms) / sizeof(uniforms[0]);
    for (size_t i = 0; i < uniformCount; ++i) {
        if (uniforms[i] != 0)
            continue;
        CK_LOG_FMT("ShaderCache", "FFP uniform initialization failed at slot=%u",
                   (unsigned)i);
        for (size_t j = 0; j < uniformCount; ++j) {
            if (uniforms[j])
                m_Context->DeleteObject(uniforms[j], CKRST_OBJ_UNIFORM);
        }
        m_Uniforms = CKFFUniformHandles();
        return false;
    }
    return true;
}

bool CKFFShaderCache::ResolveShaderTarget() {
    if (!m_Context) return false;

    CKERROR targetErr = m_Context->GetTargetDesc(&m_Target);
    if (targetErr != CK_OK) {
        CK_LOG_FMT("ShaderCache", "GetTargetDesc failed: err=%d", targetErr);
        return false;
    }
    const CKFFShaderBlobSet *set = FindShaderBlobSet(m_Target.ShaderProfile);
    if (!set) {
        CK_LOG_FMT("ShaderCache", "No FFP shader set for profile=0x%08X",
                   m_Target.ShaderProfile);
        return false;
    }
    if (g_CKFFGeneratedShaderABIVersion != CKFF_SHADER_ABI_VERSION ||
        g_CKFFGeneratedShaderInterfaceHash != CKFF_SHADER_INTERFACE_HASH) {
        CK_LOG_FMT("ShaderCache",
                   "FFP shader ABI mismatch: generatedVersion=%u expectedVersion=%u generatedHash=0x%08X expectedHash=0x%08X",
                   (unsigned)g_CKFFGeneratedShaderABIVersion,
                   (unsigned)CKFF_SHADER_ABI_VERSION,
                   (unsigned)g_CKFFGeneratedShaderInterfaceHash,
                   (unsigned)CKFF_SHADER_INTERFACE_HASH);
        return false;
    }

    m_BlobSet = set;
    CK_LOG_FMT("ShaderCache",
               "FFP shader family: backend=%s profile=0x%08X variants=%u abiDrawParams=%u abiStageParams=%u abiSpecDwords=%u",
               set->Name, m_Target.ShaderProfile,
               (unsigned)CKFF_PROGRAM_VARIANT_COUNT,
               (unsigned)CKFF_DRAW_PARAM_VEC4_COUNT,
               (unsigned)CKFF_STAGE_PARAM_VEC4_COUNT,
               (unsigned)CKFF_SPEC_UNIFORM_VEC4_COUNT);
    return true;
}

void CKFFShaderCache::BuildSamplerLayout()
{
    m_SamplerLayout = CKFFProgramSamplerLayout();
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        CKFFProgramSamplerBinding &binding = m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++];
        binding.Stage = CKFFSamplerSlot(CKFF_SAMPLER_2D, stage);
        binding.Uniform = m_Uniforms.s_texture[stage];
    }
    for (CKDWORD ordinal = 0; ordinal < CKFF_CUBE_SAMPLER_COUNT; ++ordinal) {
        CKFFProgramSamplerBinding &binding = m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++];
        binding.Stage = CKFFSamplerSlot(CKFF_SAMPLER_CUBE, ordinal);
        binding.Uniform = m_Uniforms.s_textureCube[ordinal];
    }
    for (CKDWORD ordinal = 0; ordinal < CKFF_VOLUME_SAMPLER_COUNT; ++ordinal) {
        CKFFProgramSamplerBinding &binding = m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++];
        binding.Stage = CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, ordinal);
        binding.Uniform = m_Uniforms.s_textureVolume[ordinal];
    }
}

CKFFProgramVariant CKFFShaderCache::ProgramVariantForKey(const CKFFShaderKey &key)
{
    const bool positionT = key.VS.GetHasPositionT();
    const bool clipDistance = key.VS.GetVertexClipping();
    if (positionT)
        return clipDistance ? CKFF_PROGRAM_POSITIONT_CLIP : CKFF_PROGRAM_POSITIONT;
    return clipDistance ? CKFF_PROGRAM_3D_CLIP : CKFF_PROGRAM_3D;
}

size_t CKFFShaderCache::CachedProgramCount() const
{
    size_t count = 0;
    for (CKDWORD i = 0; i < CKFF_PROGRAM_VARIANT_COUNT; ++i) {
        if (m_Programs[i])
            ++count;
    }
    return count;
}

CKDWORD CKFFShaderCache::CreateProgramVariant(CKFFProgramVariant variant)
{
    const CKFFShaderBlobSet *set = static_cast<const CKFFShaderBlobSet *>(m_BlobSet);
    if (!set || variant >= CKFF_PROGRAM_VARIANT_COUNT)
        return 0;
    const CKFFShaderBlob &vs = set->VS[variant];
    const CKDWORD program = CreateProgramFromBinary(vs.Data, vs.Size, set->FS.Data, set->FS.Size);
    CK_LOG_FMT("ShaderCache", "FFP program variant %s: %u backend=%s",
               g_ProgramVariantNames[variant], program, set->Name);
    return program;
}

CKDWORD CKFFShaderCache::CreateProgramFromBinary(
    const unsigned char *vsData, unsigned int vsSize,
    const unsigned char *fsData, unsigned int fsSize)
{
    if (!m_Context) return 0;

    CKDWORD hVS = 0;
    CKShaderDesc vsDesc = {};
    vsDesc.Stage = CKRST_SHADER_VERTEX;
    vsDesc.Format = CKRST_SHADER_FORMAT_NATIVE;
    vsDesc.Profile = m_Target.ShaderProfile;
    vsDesc.Code = vsData;
    vsDesc.CodeSize = vsSize;
    CKERROR err = m_Context->CreateShader(&vsDesc, &hVS);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateShader(VS) FAILED: err=%d handle=%u size=%u", err, hVS, vsSize);
        return 0;
    }

    CKDWORD hFS = 0;
    CKShaderDesc fsDesc = {};
    fsDesc.Stage = CKRST_SHADER_PIXEL;
    fsDesc.Format = CKRST_SHADER_FORMAT_NATIVE;
    fsDesc.Profile = m_Target.ShaderProfile;
    fsDesc.Code = fsData;
    fsDesc.CodeSize = fsSize;
    err = m_Context->CreateShader(&fsDesc, &hFS);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateShader(FS) FAILED: err=%d handle=%u size=%u", err, hFS, fsSize);
        m_Context->DeleteObject(hVS, CKRST_OBJ_SHADER);
        return 0;
    }

    CKDWORD hProgram = 0;
    CKProgramDesc progDesc = {};
    progDesc.VertexShader = hVS;
    progDesc.PixelShader = hFS;
    progDesc.ConsumeShaders = TRUE;
    err = m_Context->CreateProgram(&progDesc, &hProgram);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache",
                   "CreateProgram FAILED: err=%d backend=%s profile=0x%08X vs=%u fs=%u vsSize=%u fsSize=%u",
                   err,
                   m_BlobSet ? static_cast<const CKFFShaderBlobSet *>(m_BlobSet)->Name : "unknown",
                   m_Target.ShaderProfile,
                   hVS, hFS, vsSize, fsSize);
        m_Context->DeleteObject(hVS, CKRST_OBJ_SHADER);
        m_Context->DeleteObject(hFS, CKRST_OBJ_SHADER);
        return 0;
    }

    CK_LOG_FMT("ShaderCache", "CreateProgram OK: program=%u vs=%u fs=%u", hProgram, hVS, hFS);
    return hProgram;
}

CKFFProgramBinding CKFFShaderCache::GetProgram(const CKFFShaderKey &key) {
    const CKFFProgramVariant variant = ProgramVariantForKey(key);
    if (m_Programs[variant] == 0)
        m_Programs[variant] = CreateProgramVariant(variant);
    return CKFFProgramBinding(m_Programs[variant], CKFFBuildSpecializationInfo(key.FS));
}
