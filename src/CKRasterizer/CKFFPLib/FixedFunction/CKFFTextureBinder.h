#ifndef CKFFTEXTUREBINDER_H
#define CKFFTEXTUREBINDER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFStageState.h"
#include "CKFFStateStore.h"

class CKFFTextureBinder {
public:
    CKFFTextureBinder(const CKFFStateStore &state,
                      CKFFDrawProbes &probes);

    CKBOOL SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso);
    void BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                         CKDWORD sampledTextureMask,
                         const CKFFSamplerLayoutPlan &layoutPlan) const;
    CKSamplerDesc BuildSamplerDesc(int stage) const;

private:
    const CKFFStateStore &m_State;
    CKFFDrawProbes &m_Probes;
    CKFFSamplerOverrides m_SamplerOverrides;
};

#endif // CKFFTEXTUREBINDER_H
