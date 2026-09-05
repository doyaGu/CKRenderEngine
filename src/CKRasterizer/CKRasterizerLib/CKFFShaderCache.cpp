#include "CKFFShaderCache.h"
#include "CKFFShaderABI.h"
#include "CKFFShaderInterface.h"
#include "CKRasterizerBackend.h"
#include "CKDebugLogger.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

CKFFShaderCache::CKFFShaderCache()
    : m_Backend(nullptr), m_Target(), m_Shaders(), m_PixelShader(0), m_SamplerLayout() {
    memset(m_Programs, 0, sizeof(m_Programs));
    memset(m_VertexShaders, 0, sizeof(m_VertexShaders));
}

CKFFShaderCache::~CKFFShaderCache() {
    Shutdown();
}

bool CKFFShaderCache::Init(CKRasterizerBackend *backend, const CKBackendShaderSet &shaders) {
    Shutdown();
    m_Backend = backend;
    m_Shaders = shaders;
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
    m_Shaders = CKBackendShaderSet();
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
    if (!m_Shaders.Matches(m_Target.ShaderFormat, m_Target.ShaderProfile)) {
        CK_LOG_FMT("ShaderCache", "Missing or incompatible rasterizer shader set: format=0x%08X profile=0x%08X",
                   m_Target.ShaderFormat, m_Target.ShaderProfile);
        return false;
    }

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
    if (!m_Backend || variant >= CKFF_PROGRAM_VARIANT_COUNT)
        return 0;
    if (!m_PixelShader && m_Backend->CreateShader(
            &m_Shaders.Shaders[CKRST_SHADER_FF_FRAGMENT], &m_PixelShader) != CK_OK)
        return 0;
    if (!m_VertexShaders[variant] && m_Backend->CreateShader(
            &m_Shaders.Shaders[variant], &m_VertexShaders[variant]) != CK_OK)
        return 0;
    CKDWORD program = 0;
    const CKBackendProgramDesc desc = CKFFBuildProgramInterface(
        m_VertexShaders[variant], m_PixelShader, m_Target.ShaderFormat);
    if (m_Backend->CreateProgram(&desc, &program) != CK_OK)
        return 0;

    return program;
}


CKFFProgramBinding CKFFShaderCache::GetProgram(const CKFFShaderKey &key) {
    const CKFFProgramVariant variant = ProgramVariantForKey(key);
    if (m_Programs[variant] == 0)
        m_Programs[variant] = CreateProgramVariant(variant);
    return CKFFProgramBinding(m_Programs[variant], CKFFBuildSpecializationInfo(key.FS));
}
