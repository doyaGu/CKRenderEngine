#include "CKFFTextureBinder.h"

#include "CKFFStageState.h"
#include "CKFFUniformState.h"

static void CKFFBuildTextureBindingSet(CKFFTextureBindingSet *set,
                                       CKDWORD activeTextureCount,
                                       CKDWORD sampledTextureMask,
                                       const CKDWORD *textureHandles,
                                       const CKDWORD *textureFlags,
                                       const CKSamplerDesc *samplers,
                                       const CKFFSamplerShaderState *shaderStates,
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
        set->Bindings[stage].ShaderState = shaderStates[stage];
    }
    set->Hash = CKFFHashTextureBindingSet(set->ActiveTextureCount, set->Bindings);
}

CKFFTextureBinder::CKFFTextureBinder(const CKFFStateStore &state,
                                     const CKDWORD &shaderTargetFlags,
                                     CKFFDrawProbes &probes)
    : m_State(state),
      m_ShaderTargetFlags(shaderTargetFlags),
      m_Probes(probes),
      m_SamplerOverrides(),
      m_BindingSetValid(FALSE),
      m_BindingSetActiveTextureCount(0),
      m_BindingSetSampledTextureMask(0),
      m_BindingSetLayoutPlan(),
      m_BindingSet()
{
    CKFFInitTextureBindingSet(&m_BindingSet);
}

CKBOOL CKFFTextureBinder::SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso)
{
    const CKFFSamplerOverrides overrides(disableFilter, disableMipmaps, forceAniso);
    if (m_SamplerOverrides == overrides)
        return FALSE;
    m_SamplerOverrides = overrides;
    InvalidateAll();
    return TRUE;
}

void CKFFTextureBinder::InvalidateStage(int stage)
{
    if (stage >= 0 && stage < CKFF_MAX_TEXTURE_STAGES) {
        m_ResolvedSamplers[stage].Valid = FALSE;
        InvalidateBindingSet();
    }
}

void CKFFTextureBinder::InvalidateAll()
{
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_ResolvedSamplers[stage].Valid = FALSE;
    InvalidateBindingSet();
}

void CKFFTextureBinder::InvalidateBindingSet()
{
    m_BindingSetValid = FALSE;
}

CKBOOL CKFFTextureBinder::LayoutPlansEqual(
    const CKFFSamplerLayoutPlan &a,
    const CKFFSamplerLayoutPlan &b)
{
    if (a.Layout != b.Layout ||
        a.CompareSamplerCount != b.CompareSamplerCount)
        return FALSE;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (a.Stages[stage].Ordinal != b.Stages[stage].Ordinal ||
            a.Stages[stage].NativeSlot != b.Stages[stage].NativeSlot)
            return FALSE;
    }
    return TRUE;
}

void CKFFTextureBinder::ResolveSampler(
    int stage, CKSamplerDesc &sampler,
    CKFFSamplerShaderState &shaderState) const
{
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) {
        sampler = CKFFBuildSamplerDesc(nullptr);
        shaderState = CKFFSamplerShaderState();
        return;
    }
    ResolvedSampler &resolved = m_ResolvedSamplers[stage];
    if (!resolved.Valid) {
        resolved.Sampler = CKFFBuildSamplerDesc(
            m_State.StageStates[stage], m_SamplerOverrides);
        const CKDWORD transformFlags =
            m_State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] |
            CKFFResolveMirrorOnceAddressMask(m_State.StageStates[stage]);
        resolved.ShaderState = CKFFBuildSamplerShaderState(
            resolved.Sampler, m_State.TextureFlags[stage], transformFlags,
            m_ShaderTargetFlags);
        resolved.Valid = TRUE;
    }
    sampler = resolved.Sampler;
    shaderState = resolved.ShaderState;
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
    sampledTextureMask &= activeCount == CKFF_MAX_TEXTURE_STAGES
        ? 0xffu : ((1u << activeCount) - 1u);
    if (m_BindingSetValid &&
        m_BindingSetActiveTextureCount == activeCount &&
        m_BindingSetSampledTextureMask == sampledTextureMask &&
        LayoutPlansEqual(m_BindingSetLayoutPlan, layoutPlan)) {
        *out = m_BindingSet;
        return;
    }
    CKSamplerDesc samplers[CKFF_MAX_TEXTURE_STAGES];
    CKFFSamplerShaderState shaderStates[CKFF_MAX_TEXTURE_STAGES];
    for (CKDWORD i = 0; i < activeCount; ++i) {
        if ((sampledTextureMask & (1u << i)) != 0)
            ResolveSampler((int)i, samplers[i], shaderStates[i]);
    }
    CKFFBuildTextureBindingSet(&m_BindingSet, activeCount, sampledTextureMask,
                               m_State.TextureHandles, m_State.TextureFlags,
                               samplers, shaderStates, layoutPlan);
    m_BindingSetActiveTextureCount = activeCount;
    m_BindingSetSampledTextureMask = sampledTextureMask;
    m_BindingSetLayoutPlan = layoutPlan;
    m_BindingSetValid = TRUE;
    *out = m_BindingSet;
}

CKSamplerDesc CKFFTextureBinder::BuildSamplerDesc(int stage) const
{
    CKSamplerDesc sampler;
    CKFFSamplerShaderState shaderState;
    ResolveSampler(stage, sampler, shaderState);
    return sampler;
}
