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
                                       CKDWORD sampledTextureMask,
                                       const CKDWORD *textureHandles,
                                       const CKDWORD *textureFlags,
                                       const CKSamplerDesc *samplers)
{
    if (!set)
        return;
    CKFFInitTextureBindingSet(set);
    CKDWORD stageCount = activeTextureCount;
    if (stageCount > CKFF_MAX_TEXTURE_STAGES)
        stageCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < stageCount; ++stage) {
        if ((sampledTextureMask & (1u << stage)) == 0)
            continue;
        set->ActiveTextureCount = stage + 1;
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
      m_SamplerOverrides()
{
}

void CKFFTextureBinder::SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso)
{
    const CKFFSamplerOverrides overrides(disableFilter, disableMipmaps, forceAniso);
    if (m_SamplerOverrides != overrides)
        m_SamplerOverrides = overrides;
}

void CKFFTextureBinder::BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                                        CKDWORD sampledTextureMask) const
{
    if (!out)
        return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    CKDWORD activeCount = activeTextureCount;
    if (activeCount > CKFF_MAX_TEXTURE_STAGES)
        activeCount = CKFF_MAX_TEXTURE_STAGES;
    CKSamplerDesc samplers[CKFF_MAX_TEXTURE_STAGES];
    for (CKDWORD i = 0; i < activeCount; ++i) {
        if ((sampledTextureMask & (1u << i)) != 0)
            samplers[i] = BuildSamplerDesc((int)i);
    }
    CKFFBuildTextureBindingSet(out, u, activeCount, sampledTextureMask,
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
        if (encoder->GetStatus() != CK_OK)
            return;
        const CKFFRenderPacketTextureBinding &binding = set->Bindings[i];
        if (binding.Texture == 0)
            continue;
        CKSamplerDesc sampler = binding.Sampler;
        encoder->SetTexture(binding.Stage, binding.Uniform, binding.Texture, &sampler);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (encoder->GetStatus() == CK_OK)
            m_Probes.OnTextureBind();
#endif
    }
}

CKSamplerDesc CKFFTextureBinder::BuildSamplerDesc(int stage) const
{
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    return CKFFBuildSamplerDesc(m_State.StageStates[stage], m_SamplerOverrides);
}
