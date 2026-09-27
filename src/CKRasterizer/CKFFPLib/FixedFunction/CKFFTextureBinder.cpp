#include "CKFFTextureBinder.h"

#include "CKFFStageState.h"

static void CKFFBuildTextureBindingSet(CKFFTextureBindingSet *set,
                                       CKDWORD activeTextureCount,
                                       CKDWORD sampledTextureMask,
                                       const CKDWORD *textureHandles,
                                       const CKDWORD *textureFlags,
                                       const CKSamplerDesc *samplers,
                                       const CKFFSamplerLayoutPlan &layoutPlan)
{
    if (!set)
        return;
    CKFFInitTextureBindingSet(set);
    CKDWORD stageCount = activeTextureCount;
    if (stageCount > CKFF_MAX_TEXTURE_STAGES)
        stageCount = CKFF_MAX_TEXTURE_STAGES;
    set->SamplerLayoutPlan = layoutPlan;
    for (CKDWORD stage = 0; stage < stageCount; ++stage) {
        if ((sampledTextureMask & (1u << stage)) == 0)
            continue;
        set->ActiveTextureCount = stage + 1;
        set->Bindings[stage].Stage = layoutPlan.Stages[stage].NativeSlot;
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
                                        CKDWORD sampledTextureMask,
                                        const CKFFSamplerLayoutPlan &layoutPlan) const
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
                               m_State.TextureHandles, m_State.TextureFlags,
                               samplers, layoutPlan);
}

CKSamplerDesc CKFFTextureBinder::BuildSamplerDesc(int stage) const
{
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    return CKFFBuildSamplerDesc(m_State.StageStates[stage], m_SamplerOverrides);
}
