#include "CKFFShaderCache.h"
#include "CKFFShaderABI.h"
#include "CKRasterizerBackend.h"
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
    CK_SHADER_FORMAT Format;
    CK_SHADER_PROFILE Profile;
    const char *Name;
    CKFFShaderBlob VS[CKFF_PROGRAM_VARIANT_COUNT]; // indexed by CKFFProgramVariant
    CKFFShaderBlob FS;
};

#define CKFF_BLOB(name) {name, sizeof(name)}
#define CKFF_BLOB_SET(format, profile, backend) \
    {format, profile, #backend, \
     {CKFF_BLOB(s_##backend##_vs_ff_3d), CKFF_BLOB(s_##backend##_vs_ff_3d_clip), \
      CKFF_BLOB(s_##backend##_vs_ff_positiont), CKFF_BLOB(s_##backend##_vs_ff_positiont_clip)}, \
     CKFF_BLOB(s_##backend##_fs_ff_stage)}

static const CKFFShaderBlobSet g_ShaderBlobSets[] = {
    CKFF_BLOB_SET(CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_DX11, dx11),
    CKFF_BLOB_SET(CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_DX12, dx12),
    CKFF_BLOB_SET(CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_SPIRV, spirv),
    CKFF_BLOB_SET(CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_GLSL, glsl),
    CKFF_BLOB_SET(CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_ESSL, essl),
    CKFF_BLOB_SET(CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_MSL, metal),
};

#undef CKFF_BLOB_SET
#undef CKFF_BLOB

static const char *const g_ProgramVariantNames[CKFF_PROGRAM_VARIANT_COUNT] = {
    "3d", "3d_clip", "positiont", "positiont_clip"
};

static const CKFFShaderBlobSet *FindShaderBlobSet(CK_SHADER_FORMAT format,
                                                   CK_SHADER_PROFILE profile)
{
    if (format == CKRST_SHADER_FORMAT_UNKNOWN ||
        profile == CKRST_SHADER_PROFILE_UNKNOWN)
        return nullptr;
    for (const CKFFShaderBlobSet &set : g_ShaderBlobSets) {
        if (set.Format == format && set.Profile == profile)
            return &set;
    }
    return nullptr;
}

CKFFShaderCache::CKFFShaderCache()
    : m_Backend(nullptr), m_Target(), m_BlobSet(nullptr), m_PixelShader(0), m_SamplerLayout() {
    memset(m_Programs, 0, sizeof(m_Programs));
    memset(m_VertexShaders, 0, sizeof(m_VertexShaders));
}

CKFFShaderCache::~CKFFShaderCache() {
    Shutdown();
}

bool CKFFShaderCache::Init(CKRasterizerBackend *backend) {
    Shutdown();
    m_Backend = backend;
    if (!ResolveShaderTarget()) {
        Shutdown();
        return false;
    }
    BuildSamplerLayout();
    return true;
}

void CKFFShaderCache::Shutdown() {
    if (m_Backend) {
        for (CKDWORD i = 0; i < CKFF_PROGRAM_VARIANT_COUNT; ++i) {
            if (m_Programs[i])
                m_Backend->DestroyObject(m_Programs[i], CKRST_OBJ_PROGRAM);
            if (m_VertexShaders[i])
                m_Backend->DestroyObject(m_VertexShaders[i], CKRST_OBJ_SHADER);
        }
        if (m_PixelShader)
            m_Backend->DestroyObject(m_PixelShader, CKRST_OBJ_SHADER);
    }
    memset(m_Programs, 0, sizeof(m_Programs));
    memset(m_VertexShaders, 0, sizeof(m_VertexShaders));
    m_PixelShader = 0;
    m_SamplerLayout = CKFFProgramSamplerLayout();
    m_Backend = nullptr;
    m_BlobSet = nullptr;
}


bool CKFFShaderCache::ResolveShaderTarget() {
    if (!m_Backend) return false;

    const CKBackendCaps &caps = m_Backend->GetCaps();
    m_Target = CKRasterizerTargetDesc();
    m_Target.ShaderFormat = caps.ShaderFormat;
    m_Target.ShaderProfile = caps.ShaderProfile;
    m_Target.HomogeneousDepth = caps.HomogeneousDepth;
    m_Target.OriginBottomLeft = caps.OriginBottomLeft;
    if (m_Target.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN ||
        m_Target.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN) {
        CK_LOG_FMT("ShaderCache", "backend reports no shader target");
        return false;
    }
    const CKFFShaderBlobSet *set = FindShaderBlobSet(m_Target.ShaderFormat,
                                                      m_Target.ShaderProfile);
    if (!set) {
        CK_LOG_FMT("ShaderCache", "No FFP shader set for format=0x%08X profile=0x%08X",
                   m_Target.ShaderFormat,
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
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++].Stage = CKFFSamplerSlot(CKFF_SAMPLER_2D, stage);
    for (CKDWORD ordinal = 0; ordinal < CKFF_CUBE_SAMPLER_COUNT; ++ordinal)
        m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++].Stage = CKFFSamplerSlot(CKFF_SAMPLER_CUBE, ordinal);
    for (CKDWORD ordinal = 0; ordinal < CKFF_VOLUME_SAMPLER_COUNT; ++ordinal)
        m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++].Stage = CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, ordinal);
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

// The fragment shader is shared by the four vertex variants; shader handles
// stay alive with the cache (the backend does not consume them).
CKDWORD CKFFShaderCache::CreateProgramVariant(CKFFProgramVariant variant)
{
    const CKFFShaderBlobSet *set = static_cast<const CKFFShaderBlobSet *>(m_BlobSet);
    if (!set || !m_Backend || variant >= CKFF_PROGRAM_VARIANT_COUNT)
        return 0;
    if (!m_PixelShader) {
        CKShaderDesc fsDesc = {};
        fsDesc.Stage = CKRST_SHADER_PIXEL;
        fsDesc.Format = m_Target.ShaderFormat;
        fsDesc.Profile = m_Target.ShaderProfile;
        fsDesc.Code = set->FS.Data;
        fsDesc.CodeSize = set->FS.Size;
        const CKERROR err = m_Backend->CreateShader(&fsDesc, &m_PixelShader);
        if (err != CK_OK) {
            CK_LOG_FMT("ShaderCache", "CreateShader(FS) FAILED: err=%d size=%u", err, set->FS.Size);
            m_PixelShader = 0;
            return 0;
        }
    }
    const CKFFShaderBlob &vs = set->VS[variant];
    if (!m_VertexShaders[variant]) {
        CKShaderDesc vsDesc = {};
        vsDesc.Stage = CKRST_SHADER_VERTEX;
        vsDesc.Format = m_Target.ShaderFormat;
        vsDesc.Profile = m_Target.ShaderProfile;
        vsDesc.Code = vs.Data;
        vsDesc.CodeSize = vs.Size;
        const CKERROR err = m_Backend->CreateShader(&vsDesc, &m_VertexShaders[variant]);
        if (err != CK_OK) {
            CK_LOG_FMT("ShaderCache", "CreateShader(VS %s) FAILED: err=%d size=%u", g_ProgramVariantNames[variant], err, vs.Size);
            m_VertexShaders[variant] = 0;
            return 0;
        }
    }
    CKDWORD program = 0;
    const CKERROR err = m_Backend->CreateProgram(m_VertexShaders[variant], m_PixelShader, &program);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateProgram FAILED: err=%d backend=%s profile=0x%08X variant=%s",
                   err, set->Name, m_Target.ShaderProfile, g_ProgramVariantNames[variant]);
        return 0;
    }
    CK_LOG_FMT("ShaderCache", "FFP program variant %s: %u backend=%s",
               g_ProgramVariantNames[variant], program, set->Name);
    return program;
}


CKFFProgramBinding CKFFShaderCache::GetProgram(const CKFFShaderKey &key) {
    const CKFFProgramVariant variant = ProgramVariantForKey(key);
    if (m_Programs[variant] == 0)
        m_Programs[variant] = CreateProgramVariant(variant);
    return CKFFProgramBinding(m_Programs[variant], CKFFBuildSpecializationInfo(key.FS));
}
