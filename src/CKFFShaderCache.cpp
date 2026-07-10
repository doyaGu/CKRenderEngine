#include "CKFFShaderCache.h"
#include "CKFFSpecializedModuleTable.h"
#include "CKFFSamplerLayout.h"
#include "CKFFShaderABI.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "shaders/generated/dx11/vs_ff_3d.bin.h"
#include "shaders/generated/dx11/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx11/vs_ff_3d_instanced.bin.h"
#include "shaders/generated/dx11/vs_ff_3d_instanced_clip.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx11/fs_ff_stage.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_volume.bin.h"
#include "shaders/generated/dx12/vs_ff_3d.bin.h"
#include "shaders/generated/dx12/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx12/vs_ff_3d_instanced.bin.h"
#include "shaders/generated/dx12/vs_ff_3d_instanced_clip.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx12/fs_ff_stage.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_volume.bin.h"
#include "shaders/generated/spirv/vs_ff_3d.bin.h"
#include "shaders/generated/spirv/vs_ff_3d_clip.bin.h"
#include "shaders/generated/spirv/vs_ff_3d_instanced.bin.h"
#include "shaders/generated/spirv/vs_ff_3d_instanced_clip.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/spirv/fs_ff_stage.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_volume.bin.h"
#include "shaders/generated/glsl/vs_ff_3d.bin.h"
#include "shaders/generated/glsl/vs_ff_3d_clip.bin.h"
#include "shaders/generated/glsl/vs_ff_3d_instanced.bin.h"
#include "shaders/generated/glsl/vs_ff_3d_instanced_clip.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/glsl/fs_ff_stage.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_volume.bin.h"
#include "shaders/generated/metal/vs_ff_3d.bin.h"
#include "shaders/generated/metal/vs_ff_3d_clip.bin.h"
#include "shaders/generated/metal/vs_ff_3d_instanced.bin.h"
#include "shaders/generated/metal/vs_ff_3d_instanced_clip.bin.h"
#include "shaders/generated/metal/vs_ff_positiont.bin.h"
#include "shaders/generated/metal/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/metal/fs_ff_stage.bin.h"
#include "shaders/generated/metal/fs_ff_stage_volume.bin.h"

void CKFFInitProgramContext(CKFFProgramContext *context,
                            const CKFFShaderKey &key,
                            const CKFFProgramBinding &binding)
{
    if (!context)
        return;
    context->ShaderKey = key;
    context->Binding = binding;
    context->Program = binding.Program;
    context->FullSpecialized = binding.FullSpecialized ? TRUE : FALSE;
    context->Specialization = binding.Specialization;
}

CKBOOL CKFFCanUseInstancedProgramForPacket(const CKFFProgramContext &normalContext,
                                           const CKFFProgramContext &instancedContext)
{
    if (normalContext.Program == 0 || instancedContext.Program == 0)
        return FALSE;
    if (normalContext.FullSpecialized && !instancedContext.FullSpecialized)
        return FALSE;
    return TRUE;
}

struct CKFFShaderBlobSet {
    CK_SHADER_PROFILE Profile;
    const char *Name;
    const unsigned char *VS3D;
    unsigned int VS3DSize;
    const unsigned char *VS3DClip;
    unsigned int VS3DClipSize;
    const unsigned char *VS3DInstanced;
    unsigned int VS3DInstancedSize;
    const unsigned char *VS3DInstancedClip;
    unsigned int VS3DInstancedClipSize;
    const unsigned char *VSPositionT;
    unsigned int VSPositionTSize;
    const unsigned char *VSPositionTClip;
    unsigned int VSPositionTClipSize;
    const unsigned char *FSStage;
    unsigned int FSStageSize;
    const unsigned char *FSStageVolume;
    unsigned int FSStageVolumeSize;
};

static const CKFFShaderBlobSet g_ShaderBlobSets[] = {
    {CKRST_SHADER_PROFILE_DX11, "dx11",
     s_dx11_vs_ff_3d, sizeof(s_dx11_vs_ff_3d),
     s_dx11_vs_ff_3d_clip, sizeof(s_dx11_vs_ff_3d_clip),
     s_dx11_vs_ff_3d_instanced, sizeof(s_dx11_vs_ff_3d_instanced),
     s_dx11_vs_ff_3d_instanced_clip, sizeof(s_dx11_vs_ff_3d_instanced_clip),
     s_dx11_vs_ff_positiont, sizeof(s_dx11_vs_ff_positiont),
     s_dx11_vs_ff_positiont_clip, sizeof(s_dx11_vs_ff_positiont_clip),
     s_dx11_fs_ff_stage, sizeof(s_dx11_fs_ff_stage),
     s_dx11_fs_ff_stage_volume, sizeof(s_dx11_fs_ff_stage_volume)},
    {CKRST_SHADER_PROFILE_DX12, "dx12",
     s_dx12_vs_ff_3d, sizeof(s_dx12_vs_ff_3d),
     s_dx12_vs_ff_3d_clip, sizeof(s_dx12_vs_ff_3d_clip),
     s_dx12_vs_ff_3d_instanced, sizeof(s_dx12_vs_ff_3d_instanced),
     s_dx12_vs_ff_3d_instanced_clip, sizeof(s_dx12_vs_ff_3d_instanced_clip),
     s_dx12_vs_ff_positiont, sizeof(s_dx12_vs_ff_positiont),
     s_dx12_vs_ff_positiont_clip, sizeof(s_dx12_vs_ff_positiont_clip),
     s_dx12_fs_ff_stage, sizeof(s_dx12_fs_ff_stage),
     s_dx12_fs_ff_stage_volume, sizeof(s_dx12_fs_ff_stage_volume)},
    {CKRST_SHADER_PROFILE_SPIRV, "spirv",
     s_spirv_vs_ff_3d, sizeof(s_spirv_vs_ff_3d),
     s_spirv_vs_ff_3d_clip, sizeof(s_spirv_vs_ff_3d_clip),
     s_spirv_vs_ff_3d_instanced, sizeof(s_spirv_vs_ff_3d_instanced),
     s_spirv_vs_ff_3d_instanced_clip, sizeof(s_spirv_vs_ff_3d_instanced_clip),
     s_spirv_vs_ff_positiont, sizeof(s_spirv_vs_ff_positiont),
     s_spirv_vs_ff_positiont_clip, sizeof(s_spirv_vs_ff_positiont_clip),
     s_spirv_fs_ff_stage, sizeof(s_spirv_fs_ff_stage),
     s_spirv_fs_ff_stage_volume, sizeof(s_spirv_fs_ff_stage_volume)},
    {CKRST_SHADER_PROFILE_GLSL, "glsl",
     s_glsl_vs_ff_3d, sizeof(s_glsl_vs_ff_3d),
     s_glsl_vs_ff_3d_clip, sizeof(s_glsl_vs_ff_3d_clip),
     s_glsl_vs_ff_3d_instanced, sizeof(s_glsl_vs_ff_3d_instanced),
     s_glsl_vs_ff_3d_instanced_clip, sizeof(s_glsl_vs_ff_3d_instanced_clip),
     s_glsl_vs_ff_positiont, sizeof(s_glsl_vs_ff_positiont),
     s_glsl_vs_ff_positiont_clip, sizeof(s_glsl_vs_ff_positiont_clip),
     s_glsl_fs_ff_stage, sizeof(s_glsl_fs_ff_stage),
     s_glsl_fs_ff_stage_volume, sizeof(s_glsl_fs_ff_stage_volume)},
    {CKRST_SHADER_PROFILE_MSL, "metal",
     s_metal_vs_ff_3d, sizeof(s_metal_vs_ff_3d),
     s_metal_vs_ff_3d_clip, sizeof(s_metal_vs_ff_3d_clip),
     s_metal_vs_ff_3d_instanced, sizeof(s_metal_vs_ff_3d_instanced),
     s_metal_vs_ff_3d_instanced_clip, sizeof(s_metal_vs_ff_3d_instanced_clip),
     s_metal_vs_ff_positiont, sizeof(s_metal_vs_ff_positiont),
     s_metal_vs_ff_positiont_clip, sizeof(s_metal_vs_ff_positiont_clip),
     s_metal_fs_ff_stage, sizeof(s_metal_fs_ff_stage),
     s_metal_fs_ff_stage_volume, sizeof(s_metal_fs_ff_stage_volume)},
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

static bool CKFFShaderKeyNeedsVolumeSampler(const CKFFShaderKey &key);
static bool CKFFShaderKeyNeedsCubeSampler(const CKFFShaderKey &key);
static CKDWORD CKFFShaderKeyActiveTextureMask(const CKFFShaderKey &key);
static void CKFFSelectVertexShaderBlob(const CKFFShaderBlobSet *set,
                                       const CKFFShaderKey &key,
                                       const unsigned char **vsData,
                                       unsigned int *vsSize);

CKFFShaderCache::CKFFShaderCache()
    : m_Context(nullptr), m_Target(), m_BlobSet(nullptr), m_UseUberShader(false) {}

CKFFShaderCache::~CKFFShaderCache() {
    Shutdown();
}

bool CKFFShaderCache::Init(CKRasterizerContext *ctx) {
    Shutdown();
    m_Context = ctx;
    m_UseUberShader = CKRenderFFPSettings().GetBool("UberShader", false);
    if (!ResolveShaderTarget() || !CreateUniforms()) {
        Shutdown();
        return false;
    }
    return true;
}

void CKFFShaderCache::Shutdown() {
    if (m_Context) {
        for (CKFFProgramModuleCacheTable::Iterator it = m_ModuleProgramCache.Begin();
             it != m_ModuleProgramCache.End(); ++it) {
            if (*it)
                m_Context->DeleteObject(*it, CKRST_OBJ_PROGRAM);
        }
        m_ModuleProgramCache.Clear();
        m_ProgramCache.Clear();
        CKDWORD *uniforms = reinterpret_cast<CKDWORD *>(&m_Uniforms);
        const size_t uniformCount = sizeof(m_Uniforms) / sizeof(CKDWORD);
        for (size_t i = 0; i < uniformCount; ++i) {
            if (uniforms[i])
                m_Context->DeleteObject(uniforms[i], CKRST_OBJ_UNIFORM);
        }
        m_Uniforms = CKFFUniformHandles();
    }
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
    desc.Name = (char *)"u_ckModelViewProj";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ckModelViewProj);

    desc.Name = (char *)"u_ckModel";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ckModel);

    desc.Name = (char *)"u_ckModelView";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ckModelView);

    desc.Name = (char *)"u_ckNormalMatrix";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ckNormalMatrix);

    desc.Name = (char *)"u_texMatrix";
    desc.Count = CKFF_MAX_TEXTURE_STAGES;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_texMatrix);

    desc.Type = CKRST_UNIFORM_VEC4;
    desc.Name = (char *)"u_ffVertexParams";
    desc.Count = 8;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ffVertexParams);

    desc.Name = (char *)"u_ffFragmentParams";
    desc.Count = 4;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ffFragmentParams);

    desc.Name = (char *)"u_lights";
    desc.Count = CKFF_MAX_LIGHTS * 7;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_lights);

    desc.Name = (char *)"u_lightParams";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_lightParams);

    desc.Name = (char *)"u_material";
    desc.Count = 5;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_material);

    desc.Name = (char *)"u_ffParams";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_ffParams);

    desc.Name = (char *)"u_lightModelParams";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_lightModelParams);

    desc.Name = (char *)"u_fogParams";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_fogParams);

    desc.Name = (char *)"u_fogColor";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_fogColor);

    desc.Name = (char *)"u_texFactor";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_texFactor);

    desc.Name = (char *)"u_alphaParams";
    desc.Count = 1;
    m_Context->CreateUniform(&desc, &m_Uniforms.u_alphaParams);

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
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "s_textureCube%d", i);
        desc.Name = name;
        m_Context->CreateUniform(&desc, &m_Uniforms.s_textureCube[i]);
    }
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "s_textureVolume%d", i);
        desc.Name = name;
        m_Context->CreateUniform(&desc, &m_Uniforms.s_textureVolume[i]);
    }

    CKDWORD *uniforms = reinterpret_cast<CKDWORD *>(&m_Uniforms);
    const size_t uniformCount = sizeof(m_Uniforms) / sizeof(CKDWORD);
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
    if (CKFFGeneratedShaderABIVersion() != CKFF_SHADER_ABI_VERSION ||
        CKFFGeneratedShaderInterfaceHash() != CKFF_SHADER_INTERFACE_HASH) {
        CK_LOG_FMT("ShaderCache",
                   "FFP shader ABI mismatch: generatedVersion=%u expectedVersion=%u generatedHash=0x%08X expectedHash=0x%08X",
                   (unsigned)CKFFGeneratedShaderABIVersion(),
                   (unsigned)CKFF_SHADER_ABI_VERSION,
                   (unsigned)CKFFGeneratedShaderInterfaceHash(),
                   (unsigned)CKFF_SHADER_INTERFACE_HASH);
        return false;
    }

    m_BlobSet = set;
    CK_LOG_FMT("ShaderCache",
               "FFP shader mode: backend=%s profile=0x%08X mode=%s specializedModules=%u samplerLayoutModules=%u abiDrawParams=%u abiStageParams=%u abiSpecDwords=%u",
                set->Name, m_Target.ShaderProfile,
               m_UseUberShader ? "uber" : "full-specialized",
               (unsigned)CKFFSpecializedModuleCount(),
               (unsigned)CKFFSamplerLayoutModuleCount(),
               (unsigned)CKFF_DRAW_PARAM_VEC4_COUNT,
               (unsigned)CKFF_STAGE_PARAM_VEC4_COUNT,
               (unsigned)CKFF_SPEC_UNIFORM_VEC4_COUNT);
    return true;
}

CKFFProgramBinding CKFFShaderCache::CreateVariantProgram(const CKFFShaderKey &key) {
    if (!m_UseUberShader)
        return CreateFullSpecializedProgram(key);
    if (CKFFShaderKeyNeedsVolumeSampler(key)) {
        if (CKFFShaderKeyNeedsCubeSampler(key)) {
            return CreateStaticSamplerLayoutProgram(key);
        }
        return CreateVolumeSamplerLayoutProgram(key);
    }
    return CreateUberSpecializedProgram(key);
}

static bool CKFFShaderKeyNeedsVolumeSampler(const CKFFShaderKey &key) {
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = key.FS.Stages[stage];
        if (s.HasTexture && s.SamplerType == CKFF_SAMPLER_VOLUME)
            return true;
    }
    return false;
}

static bool CKFFShaderKeyNeedsCubeSampler(const CKFFShaderKey &key) {
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = key.FS.Stages[stage];
        if (s.HasTexture && s.SamplerType == CKFF_SAMPLER_CUBE)
            return true;
    }
    return false;
}

static CKDWORD CKFFShaderKeyActiveTextureMask(const CKFFShaderKey &key) {
    CKDWORD mask = 0;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        if (key.FS.Stages[stage].HasTexture)
            mask |= 1u << stage;
    }
    return mask;
}

static void CKFFSelectVertexShaderBlob(const CKFFShaderBlobSet *set,
                                       const CKFFShaderKey &key,
                                       const unsigned char **vsData,
                                       unsigned int *vsSize) {
    if (!set || !vsData || !vsSize)
        return;
    const bool positionT = key.VS.GetHasPositionT();
    const bool clipDistance = (key.VS.Bits & (1ull << 34)) != 0;
    const bool instanced = key.VS.GetInstanced();
    if (positionT) {
        *vsData = clipDistance ? set->VSPositionTClip : set->VSPositionT;
        *vsSize = clipDistance ? set->VSPositionTClipSize : set->VSPositionTSize;
    } else if (instanced) {
        *vsData = clipDistance ? set->VS3DInstancedClip : set->VS3DInstanced;
        *vsSize = clipDistance ? set->VS3DInstancedClipSize : set->VS3DInstancedSize;
    } else {
        *vsData = clipDistance ? set->VS3DClip : set->VS3D;
        *vsSize = clipDistance ? set->VS3DClipSize : set->VS3DSize;
    }
}

CKFFProgramBinding CKFFShaderCache::CreateFullSpecializedProgram(const CKFFShaderKey &key) {
    if (key.FS.LastActiveTextureStage > 3) {
        CK_LOG_FMT("ShaderCache",
                   "FFP full-specialized rejected: lastStage=%u exceeds specialized stage limit; falling back",
                   key.FS.LastActiveTextureStage);
        if (CKFFShaderKeyNeedsVolumeSampler(key)) {
            if (CKFFShaderKeyNeedsCubeSampler(key)) {
                return CreateStaticSamplerLayoutProgram(key);
            }
            return CreateVolumeSamplerLayoutProgram(key);
        }
        return CreateUberSpecializedProgram(key);
    }

    CKFFSpecializedModule module;
    if (CKFFFindSpecializedModule(key, m_Target.ShaderProfile, module)) {
        CKDWORD program = CreateProgramFromBinary(
            m_Target,
            module.VSData, module.VSSize,
            module.FSData, module.FSSize,
            module.Specialization);
        return CKFFProgramBinding(program, program != 0, module.Specialization);
    }

    CKFFSpecializationInfo specInfo = CKFFBuildSpecializationInfo(key.FS);
    const CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key.FS);
    char stageTypes[32];
    CKFFFormatSamplerLayoutStageTypes(layout, stageTypes, sizeof(stageTypes));
    CK_LOG_FMT("ShaderCache",
               "Full FFP specialized module cache miss: backend=%s profile=0x%08X positionT=%u vsBits=%llu vsTexcoordDeclMask=%u vsTexGen0=%u vsTexGen1=%u vsTexGen2=%u vsTexGen3=%u vsTexGen4=%u vsTexGen5=%u vsTexGen6=%u vsTexGen7=%u vsTexCoordIndex0=%u vsTexCoordIndex1=%u vsTexCoordIndex2=%u vsTexCoordIndex3=%u vsTexCoordIndex4=%u vsTexCoordIndex5=%u vsTexCoordIndex6=%u vsTexCoordIndex7=%u vsTexTransformFlags0=%u vsTexTransformFlags1=%u vsTexTransformFlags2=%u vsTexTransformFlags3=%u vsTexTransformFlags4=%u vsTexTransformFlags5=%u vsTexTransformFlags6=%u vsTexTransformFlags7=%u lastStage=%u activeTextureMask=0x%02X layout=0x%04X stageTypes=%s mixedCubeVolume=%u specular=%u alphaTest=%u alphaFunc=%u fog=%u projectedMask=%u specDword0=%u specDword1=%u specDword2=%u specDword3=%u specDword4=%u specDword5=%u specDword6=%u specDword7=%u specDword8=%u specDword9=%u",
               m_BlobSet ? static_cast<const CKFFShaderBlobSet *>(m_BlobSet)->Name : "unknown",
               m_Target.ShaderProfile,
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
               CKFFShaderKeyActiveTextureMask(key),
               layout.Bits,
               stageTypes,
               CKFFSamplerLayoutNeedsMixedCubeVolume(layout) ? 1u : 0u,
               key.FS.GlobalSpecularEnable ? 1u : 0u,
               key.FS.AlphaTestEnable ? 1u : 0u,
               key.FS.AlphaFunc,
               key.FS.FogEnable ? 1u : 0u,
               specInfo.Get(CKFF_SPEC_PROJECTED_SAMPLER_MASK),
               specInfo.Data()[0], specInfo.Data()[1], specInfo.Data()[2],
               specInfo.Data()[3], specInfo.Data()[4], specInfo.Data()[5],
               specInfo.Data()[6], specInfo.Data()[7], specInfo.Data()[8],
               specInfo.Data()[9]);
    if (CKFFShaderKeyNeedsVolumeSampler(key)) {
        if (CKFFShaderKeyNeedsCubeSampler(key)) {
            return CreateStaticSamplerLayoutProgram(key);
        }
        return CreateVolumeSamplerLayoutProgram(key);
    }
    return CreateUberSpecializedProgram(key);
}

CKFFProgramBinding CKFFShaderCache::CreateVolumeSamplerLayoutProgram(const CKFFShaderKey &key) {
    const CKFFShaderBlobSet *set = static_cast<const CKFFShaderBlobSet *>(m_BlobSet);
    if (!set)
        return CKFFProgramBinding();

    CKFFSpecializationInfo specInfo = CKFFBuildSpecializationInfo(key.FS);
    const bool positionT = key.VS.GetHasPositionT();
    const bool clipDistance = (key.VS.Bits & (1ull << 34)) != 0;
    const bool instanced = key.VS.GetInstanced();
    const unsigned char *vsData = nullptr;
    unsigned int vsSize = 0;
    CKFFSelectVertexShaderBlob(set, key, &vsData, &vsSize);
    CKDWORD program = CreateProgramFromBinary(
        m_Target, vsData, vsSize, set->FSStageVolume, set->FSStageVolumeSize, specInfo);

    CK_LOG_FMT("ShaderCache",
               "FFP volume sampler program: %u backend=%s positionT=%u clip=%u instanced=%u lastStage=%u",
               program, set->Name, positionT ? 1u : 0u, clipDistance ? 1u : 0u,
               instanced ? 1u : 0u,
               key.FS.LastActiveTextureStage);
    return CKFFProgramBinding(program, false, specInfo);
}

CKFFProgramBinding CKFFShaderCache::CreateStaticSamplerLayoutProgram(const CKFFShaderKey &key) {
    const CKFFShaderBlobSet *set = static_cast<const CKFFShaderBlobSet *>(m_BlobSet);
    if (!set)
        return CKFFProgramBinding();

    const CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key.FS);
    CKFFSamplerLayoutModule module;
    if (!CKFFFindSamplerLayoutModule(layout, m_Target.ShaderProfile, module)) {
        char stageTypes[32];
        char manifestEntry[128];
        CKFFFormatSamplerLayoutStageTypes(layout, stageTypes, sizeof(stageTypes));
        CKFFFormatSamplerLayoutManifestEntry(layout, set->Name, manifestEntry, sizeof(manifestEntry));
        CK_LOG_FMT("ShaderCache",
                   "FFP static sampler layout miss: backend=%s profile=0x%08X lastStage=%u activeTextureMask=0x%02X layout=0x%04X stageTypes=%s mixedCubeVolume=%u manifestEntry=%s",
                   set->Name, m_Target.ShaderProfile, key.FS.LastActiveTextureStage,
                   CKFFShaderKeyActiveTextureMask(key), layout.Bits,
                   stageTypes,
                   CKFFSamplerLayoutNeedsMixedCubeVolume(layout) ? 1u : 0u,
                   manifestEntry);
        return CKFFProgramBinding();
    }

    CKFFSpecializationInfo specInfo = CKFFBuildSpecializationInfo(key.FS);
    const bool positionT = key.VS.GetHasPositionT();
    const bool clipDistance = (key.VS.Bits & (1ull << 34)) != 0;
    const bool instanced = key.VS.GetInstanced();
    char stageTypes[32];
    CKFFFormatSamplerLayoutStageTypes(layout, stageTypes, sizeof(stageTypes));
    const unsigned char *vsData = nullptr;
    unsigned int vsSize = 0;
    CKFFSelectVertexShaderBlob(set, key, &vsData, &vsSize);
    CKDWORD program = CreateProgramFromBinary(
        m_Target, vsData, vsSize, module.FSData, module.FSSize, specInfo);

    CK_LOG_FMT("ShaderCache",
               "FFP static sampler layout program: %u backend=%s profile=0x%08X positionT=%u clip=%u instanced=%u lastStage=%u activeTextureMask=0x%02X layout=0x%04X stageTypes=%s mixedCubeVolume=%u",
               program, set->Name, m_Target.ShaderProfile, positionT ? 1u : 0u, clipDistance ? 1u : 0u,
               instanced ? 1u : 0u,
               key.FS.LastActiveTextureStage, CKFFShaderKeyActiveTextureMask(key), layout.Bits,
               stageTypes, CKFFSamplerLayoutNeedsMixedCubeVolume(layout) ? 1u : 0u);
    return CKFFProgramBinding(program, false, specInfo);
}

CKFFProgramBinding CKFFShaderCache::CreateUberSpecializedProgram(const CKFFShaderKey &key) {
    const CKFFShaderBlobSet *set = static_cast<const CKFFShaderBlobSet *>(m_BlobSet);
    if (!set)
        return CKFFProgramBinding();

    CKFFSpecializationInfo specInfo = CKFFBuildSpecializationInfo(key.FS);
    const bool positionT = key.VS.GetHasPositionT();
    const bool clipDistance = (key.VS.Bits & (1ull << 34)) != 0;
    const bool instanced = key.VS.GetInstanced();
    const unsigned char *vsData = nullptr;
    unsigned int vsSize = 0;
    CKFFSelectVertexShaderBlob(set, key, &vsData, &vsSize);
    CKDWORD program = CreateProgramFromBinary(
        m_Target, vsData, vsSize, set->FSStage, set->FSStageSize, specInfo);

    CK_LOG_FMT("ShaderCache",
               "FFP variant program: %u backend=%s ubershader=%u positionT=%u clip=%u instanced=%u lastStage=%u specular=%u alphaTest=%u alphaFunc=%u fog=%u",
               program, set->Name, m_UseUberShader ? 1u : 0u, positionT ? 1u : 0u, clipDistance ? 1u : 0u,
               instanced ? 1u : 0u,
               key.FS.LastActiveTextureStage, key.FS.GlobalSpecularEnable ? 1u : 0u,
               key.FS.AlphaTestEnable ? 1u : 0u, key.FS.AlphaFunc,
               key.FS.FogEnable ? 1u : 0u);
    return CKFFProgramBinding(program, false, specInfo);
}

CKDWORD CKFFShaderCache::CreateProgramFromBinary(
    const CKRasterizerTargetDesc &target,
    const unsigned char *vsData, unsigned int vsSize,
    const unsigned char *fsData, unsigned int fsSize,
    const CKFFSpecializationInfo &specInfo)
{
    if (!m_Context) return 0;

    CKFFProgramModuleKey moduleKey = {
        target.ShaderProfile, vsData, vsSize, fsData, fsSize
    };
    CKDWORD cachedProgram = 0;
    if (m_ModuleProgramCache.LookUp(moduleKey, cachedProgram))
        return cachedProgram;

    CKDWORD hVS = 0;
    CKShaderDesc vsDesc = {};
    vsDesc.Stage = CKRST_SHADER_VERTEX;
    vsDesc.Format = CKRST_SHADER_FORMAT_NATIVE;
    vsDesc.Profile = target.ShaderProfile;
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
    fsDesc.Profile = target.ShaderProfile;
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
        const CKDWORD *spec = specInfo.Data();
        CK_LOG_FMT("ShaderCache",
                   "CreateProgram FAILED: err=%d backend=%s profile=0x%08X vs=%u fs=%u vsSize=%u fsSize=%u specDwordCount=%u specDword0=%u specDword1=%u specDword2=%u specDword3=%u specDword4=%u specDword5=%u specDword6=%u specDword7=%u specDword8=%u specDword9=%u",
                   err,
                   m_BlobSet ? static_cast<const CKFFShaderBlobSet *>(m_BlobSet)->Name : "unknown",
                   target.ShaderProfile,
                   hVS, hFS, vsSize, fsSize, specInfo.DwordCount(),
                   spec[0], spec[1], spec[2], spec[3], spec[4],
                   spec[5], spec[6], spec[7], spec[8], spec[9]);
        m_Context->DeleteObject(hVS, CKRST_OBJ_SHADER);
        m_Context->DeleteObject(hFS, CKRST_OBJ_SHADER);
        return 0;
    }

    CK_LOG_FMT("ShaderCache", "CreateProgram OK: program=%u vs=%u fs=%u", hProgram, hVS, hFS);
    m_ModuleProgramCache.Insert(moduleKey, hProgram);
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
