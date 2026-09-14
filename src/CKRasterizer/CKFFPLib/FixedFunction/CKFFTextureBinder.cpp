#include "CKFFTextureBinder.h"

#include "CKFFShaderABI.h"
#include "CKFFStageState.h"

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


static void CKFFBuildTextureBindingSet(CKFFTextureBindingSet *set,
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
    // Cube and volume stages take the next slot of their type block in stage
    // order, matching ckffSamplerOrdinal in fs_ff_stage.sc. The shader key
    // already dropped stages beyond the per-type slot count from the mask.
    CKDWORD cubeOrdinal = 0;
    CKDWORD volumeOrdinal = 0;
    for (CKDWORD stage = 0; stage < stageCount; ++stage) {
        if ((sampledTextureMask & (1u << stage)) == 0)
            continue;
        set->ActiveTextureCount = stage + 1;
        const CKDWORD samplerType = CKFFTextureBindingSamplerType(
            CKFFSamplerTypeFromTextureFlags(textureFlags[stage]));
        CKDWORD slotIndex = stage;
        if (samplerType == CKFF_SAMPLER_CUBE)
            slotIndex = cubeOrdinal++;
        else if (samplerType == CKFF_SAMPLER_VOLUME)
            slotIndex = volumeOrdinal++;
        set->Bindings[stage].Stage = CKFFSamplerSlot(samplerType, slotIndex);
        set->Bindings[stage].Texture = textureHandles[stage];
        set->Bindings[stage].TextureFlags = textureFlags[stage];
        set->Bindings[stage].Sampler = samplers[stage];
    }
    set->Hash = CKFFHashTextureBindingSet(set->ActiveTextureCount, set->Bindings);
}

CKFFTextureBinder::CKFFTextureBinder(const CKFFStateStore &state,
                                     CKFFDrawProbes &probes)
    : m_State(state),
      m_Probes(probes),
      m_SamplerOverrides()
{
}

CKBOOL CKFFTextureBinder::SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso)
{
    const CKFFSamplerOverrides overrides(disableFilter, disableMipmaps, forceAniso);
    if (m_SamplerOverrides == overrides)
        return FALSE;
    m_SamplerOverrides = overrides;
    return TRUE;
}

void CKFFTextureBinder::BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                                        CKDWORD sampledTextureMask) const
{
    if (!out)
        return;
    CKDWORD activeCount = activeTextureCount;
    if (activeCount > CKFF_MAX_TEXTURE_STAGES)
        activeCount = CKFF_MAX_TEXTURE_STAGES;
    CKSamplerDesc samplers[CKFF_MAX_TEXTURE_STAGES];
    for (CKDWORD i = 0; i < activeCount; ++i) {
        if ((sampledTextureMask & (1u << i)) != 0)
            samplers[i] = BuildSamplerDesc((int)i);
    }
    CKFFBuildTextureBindingSet(out, activeCount, sampledTextureMask,
                               m_State.TextureHandles, m_State.TextureFlags, samplers);
}

CKSamplerDesc CKFFTextureBinder::BuildSamplerDesc(int stage) const
{
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    return CKFFBuildSamplerDesc(m_State.StageStates[stage], m_SamplerOverrides);
}
