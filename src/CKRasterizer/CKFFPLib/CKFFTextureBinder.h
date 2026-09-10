#ifndef CKFFTEXTUREBINDER_H
#define CKFFTEXTUREBINDER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFShaderCache.h"
#include "CKFFStageState.h"
#include "CKFFStateStore.h"
#include "CKBackendDrawData.h"

class CKRasterizerBackend;

class CKFFTextureBinder {
public:
    CKFFTextureBinder(const CKFFStateStore &state,
                      CKFFShaderCache &shaderCache,
                      CKFFDrawProbes &probes);

    void SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso);
    void BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                         CKDWORD sampledTextureMask) const;
    const CKBackendTextureBindings &BuildDrawBindings(const CKFFTextureBindingSet *set);
    CKSamplerDesc BuildSamplerDesc(int stage) const;

private:
    const CKFFStateStore &m_State;
    CKFFShaderCache &m_ShaderCache;
    CKBackendTextureBindings m_Bindings;
    CKDWORD m_BoundSlotMask;
    CKFFDrawProbes &m_Probes;
    CKFFSamplerOverrides m_SamplerOverrides;
};

#endif // CKFFTEXTUREBINDER_H
