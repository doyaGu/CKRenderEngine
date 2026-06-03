#include "CKFFTextureBinder.h"

#include "CKFFShaderABI.h"
#include "CKFFStageState.h"
#include "CKRasterizer.h"

static CKDWORD CKFFSamplerTypeFromTextureFlags(CKDWORD textureFlags)
{
    if ((textureFlags & CKRST_TEXTURE_CUBEMAP) != 0)
        return CKFF_SAMPLER_CUBE;
    if ((textureFlags & CKRST_TEXTURE_VOLUMEMAP) != 0)
        return CKFF_SAMPLER_VOLUME;
    if ((textureFlags & CKRST_TEXTURE_DEPTHSTENCIL) != 0)
        return CKFF_SAMPLER_DEPTH;
    return CKFF_SAMPLER_2D;
}

static CKDWORD CKFFTextureBindingSamplerType(CKDWORD samplerType)
{
    if (samplerType == CKFF_SAMPLER_CUBE || samplerType == CKFF_SAMPLER_VOLUME)
        return samplerType;
    return CKFF_SAMPLER_2D;
}

static CKDWORD CKFFTextureBindingUniform(const CKFFUniformHandles &uniforms,
                                         CKDWORD stage,
                                         CKDWORD samplerType)
{
    if (stage >= CKFF_MAX_TEXTURE_STAGES)
        return 0;
    if (samplerType == CKFF_SAMPLER_CUBE)
        return uniforms.s_textureCube[stage];
    if (samplerType == CKFF_SAMPLER_VOLUME)
        return uniforms.s_textureVolume[stage];
    return uniforms.s_texture[stage];
}

static void CKFFBuildTextureBindingSet(CKFFTextureBindingSet *set,
                                       const CKFFUniformHandles &uniforms,
                                       CKDWORD activeTextureCount,
                                       const CKDWORD *textureHandles,
                                       const CKDWORD *textureFlags,
                                       const CKSamplerDesc *samplers)
{
    if (!set)
        return;
    CKFFInitTextureBindingSet(set);
    set->ActiveTextureCount = activeTextureCount;
    if (set->ActiveTextureCount > CKFF_MAX_TEXTURE_STAGES)
        set->ActiveTextureCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < set->ActiveTextureCount; ++stage) {
        const CKDWORD samplerType = CKFFTextureBindingSamplerType(
            CKFFSamplerTypeFromTextureFlags(textureFlags[stage]));
        set->Bindings[stage].Stage = CKFFSamplerBindStage(stage, samplerType);
        set->Bindings[stage].Uniform = CKFFTextureBindingUniform(uniforms, stage, samplerType);
        set->Bindings[stage].Texture = textureHandles[stage];
        set->Bindings[stage].TextureFlags = textureFlags[stage];
        set->Bindings[stage].Sampler = samplers[stage];
    }
    set->Hash = CKFFHashRenderPacketTextureSet(set->ActiveTextureCount, set->Bindings);
}

#if CKRE_ENABLE_FFP_DIAGNOSTICS
CKFFTextureBinder::CKFFTextureBinder(const CKFFStateStore &state,
                                     CKFFShaderCache &shaderCache,
                                     CKFFDrawProbes &probes)
#else
CKFFTextureBinder::CKFFTextureBinder(const CKFFStateStore &state,
                                     CKFFShaderCache &shaderCache)
#endif
    : m_State(state),
      m_ShaderCache(shaderCache),
#if CKRE_ENABLE_FFP_DIAGNOSTICS
      m_Probes(probes),
#endif
      m_DisableTextureFiltering(FALSE),
      m_DisableMipmaps(FALSE),
      m_ForceAnisotropicFiltering(FALSE)
{
}

void CKFFTextureBinder::SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso)
{
    m_DisableTextureFiltering = disableFilter;
    m_DisableMipmaps = disableMipmaps;
    m_ForceAnisotropicFiltering = forceAniso;
}

void CKFFTextureBinder::BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount) const
{
    if (!out)
        return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    CKDWORD activeCount = activeTextureCount;
    if (activeCount > CKFF_MAX_TEXTURE_STAGES)
        activeCount = CKFF_MAX_TEXTURE_STAGES;
    CKSamplerDesc samplers[CKFF_MAX_TEXTURE_STAGES];
    for (CKDWORD i = 0; i < activeCount; ++i)
        samplers[i] = BuildSamplerDesc((int)i);
    CKFFBuildTextureBindingSet(out, u, activeCount,
                               m_State.TextureHandles, m_State.TextureFlags, samplers);
}

void CKFFTextureBinder::Bind(CKRasterizerEncoder *encoder, const CKFFTextureBindingSet *set) const
{
    if (!encoder || !set)
        return;

    CKDWORD desiredTextures[CKFF_MAX_TEXTURE_STAGES] = {};
    for (CKDWORD i = 0; i < set->ActiveTextureCount; ++i)
        desiredTextures[i] = set->Bindings[i].Texture;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_Probes.OnTextureSet(set->ActiveTextureCount, desiredTextures);
#endif

    for (CKDWORD i = 0; i < set->ActiveTextureCount; ++i) {
        const CKFFRenderPacketTextureBinding &binding = set->Bindings[i];
        if (binding.Texture == 0)
            continue;
        CKSamplerDesc sampler = binding.Sampler;
        encoder->SetTexture(binding.Stage, binding.Uniform, binding.Texture, &sampler);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        m_Probes.OnTextureBind();
#endif
    }
}

CKSamplerDesc CKFFTextureBinder::BuildSamplerDesc(int stage) const
{
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    CKSamplerDesc desc = CKFFBuildSamplerDesc(m_State.StageStates[stage]);
    if (m_DisableTextureFiltering) {
        desc.MinFilter = CKRST_FILTER_NEAREST;
        desc.MagFilter = CKRST_FILTER_NEAREST;
        desc.MipFilter = m_DisableMipmaps ? CKRST_FILTER_NONE : CKRST_FILTER_NEAREST;
    } else if (m_DisableMipmaps) {
        desc.MipFilter = CKRST_FILTER_NONE;
    } else if (m_ForceAnisotropicFiltering) {
        desc.MinFilter = CKRST_FILTER_ANISOTROPIC;
        desc.MagFilter = CKRST_FILTER_ANISOTROPIC;
        desc.MipFilter = CKRST_FILTER_ANISOTROPIC;
    }
    return desc;
}
