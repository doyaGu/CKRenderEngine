#ifndef CKFFTEXTUREBINDER_H
#define CKFFTEXTUREBINDER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFStageState.h"
#include "CKFFStateStore.h"

class CKFFTextureBinder {
public:
    CKFFTextureBinder(const CKFFStateStore &state,
                      const CKDWORD &shaderTargetFlags,
                      CKFFDrawProbes &probes);

    CKBOOL SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso);
    void InvalidateStage(int stage);
    void InvalidateAll();
    void BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                         CKDWORD sampledTextureMask,
                         const CKFFSamplerLayoutPlan &layoutPlan) const;
    const CKFFTextureBindingSet &ResolveBindingSet(
        CKDWORD activeTextureCount,
        CKDWORD sampledTextureMask,
        const CKFFSamplerLayoutPlan &layoutPlan) const;
    CKSamplerDesc BuildSamplerDesc(int stage) const;

private:
    struct ResolvedSampler {
        CKBOOL Valid;
        CKSamplerDesc Sampler;
        CKFFSamplerShaderState ShaderState;

        ResolvedSampler()
            : Valid(FALSE), Sampler(), ShaderState() {}
    };

    void ResolveSampler(int stage, CKSamplerDesc &sampler,
                        CKFFSamplerShaderState &shaderState) const;
    void InvalidateBindingSet();
    static CKBOOL LayoutPlansEqual(const CKFFSamplerLayoutPlan &a,
                                   const CKFFSamplerLayoutPlan &b);

    const CKFFStateStore &m_State;
    const CKDWORD &m_ShaderTargetFlags;
    CKFFDrawProbes &m_Probes;
    CKFFSamplerOverrides m_SamplerOverrides;
    mutable ResolvedSampler m_ResolvedSamplers[CKFF_MAX_TEXTURE_STAGES];
    mutable CKBOOL m_BindingSetValid;
    mutable CKDWORD m_BindingSetActiveTextureCount;
    mutable CKDWORD m_BindingSetSampledTextureMask;
    mutable CKFFSamplerLayoutPlan m_BindingSetLayoutPlan;
    mutable CKFFTextureBindingSet m_BindingSet;
};

#endif // CKFFTEXTUREBINDER_H
