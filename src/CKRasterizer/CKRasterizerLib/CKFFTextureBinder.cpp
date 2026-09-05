#include "CKFFTextureBinder.h"

#include "CKFFShaderABI.h"
#include "CKFFStageState.h"
#include "CKRasterizerBackend.h"

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
                                     CKFFShaderCache &shaderCache,
                                     CKFFDrawProbes &probes)
    : m_State(state),
      m_ShaderCache(shaderCache),
      m_Probes(probes),
      m_SamplerOverrides(),
      m_BoundSlotMask(0)
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

void CKFFTextureBinder::ResetProgramBindings()
{
    m_InitializedPrograms.Clear();
    m_BoundSlotMask = 0;
}

CKBOOL CKFFTextureBinder::InitializeProgramSamplers(
    CKRasterizerBackend *backend, CKDWORD program)
{
    if (!backend || program == 0 ||
        !m_ShaderCache.RequiresExplicitSamplerInitialization())
        return FALSE;

    CKBOOL initialized = FALSE;
    if (m_InitializedPrograms.LookUp(program, initialized))
        return FALSE;

    const CKFFProgramSamplerLayout &layout = m_ShaderCache.GetSamplerLayout();

    // GLSL rejects draws when active sampler types retain the shared default unit.
    for (CKDWORD i = 0; i < layout.BindingCount; ++i) {
        backend->BindTexture(layout.Bindings[i].Stage, 0, NULL);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        m_Probes.OnTextureBind();
#endif
    }
    m_InitializedPrograms.Insert(program, TRUE);
    return TRUE;
}

void CKFFTextureBinder::Bind(CKRasterizerBackend *backend, CKDWORD program,
                             const CKFFTextureBindingSet *set)
{
    if (!backend || !set)
        return;

    InitializeProgramSamplers(backend, program);

    CKDWORD desiredTextures[CKFF_MAX_TEXTURE_STAGES] = {};
    for (CKDWORD i = 0; i < set->ActiveTextureCount; ++i)
        desiredTextures[i] = set->Bindings[i].Texture;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_Probes.OnTextureSet(set->ActiveTextureCount, desiredTextures);
#endif

    CKDWORD boundMask = 0;
    for (CKDWORD i = 0; i < set->ActiveTextureCount; ++i) {
        const CKFFTextureBinding &binding = set->Bindings[i];
        if (binding.Texture == 0)
            continue;
        CKSamplerDesc sampler = binding.Sampler;
        backend->BindTexture(binding.Stage, binding.Texture, &sampler);
        boundMask |= 1u << binding.Stage;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        m_Probes.OnTextureBind();
#endif
    }
    // Slots the previous draw used and this one does not.
    CKDWORD stale = m_BoundSlotMask & ~boundMask;
    for (CKDWORD slot = 0; stale != 0 && slot < CKFF_SAMPLER_SLOT_COUNT; ++slot) {
        if (stale & (1u << slot)) {
            backend->BindTexture(slot, 0, NULL);
            stale &= ~(1u << slot);
        }
    }
    m_BoundSlotMask = boundMask;
}

CKSamplerDesc CKFFTextureBinder::BuildSamplerDesc(int stage) const
{
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    return CKFFBuildSamplerDesc(m_State.StageStates[stage], m_SamplerOverrides);
}
