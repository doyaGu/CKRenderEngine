#ifndef CKFFTEXTUREBINDER_H
#define CKFFTEXTUREBINDER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFShaderCache.h"
#include "CKFFStateStore.h"

class CKRasterizerEncoder;

class CKFFTextureBinder {
public:
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFTextureBinder(const CKFFStateStore &state,
                      CKFFShaderCache &shaderCache,
                      CKFFDrawProbes &probes);
#else
    CKFFTextureBinder(const CKFFStateStore &state,
                      CKFFShaderCache &shaderCache);
#endif

    void SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso);
    void BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                         CKDWORD sampledTextureMask) const;
    void Bind(CKRasterizerEncoder *encoder, const CKFFTextureBindingSet *set) const;
    CKSamplerDesc BuildSamplerDesc(int stage) const;

private:
    const CKFFStateStore &m_State;
    CKFFShaderCache &m_ShaderCache;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes &m_Probes;
#endif
    CKBOOL m_DisableTextureFiltering;
    CKBOOL m_DisableMipmaps;
    CKBOOL m_ForceAnisotropicFiltering;
};

#endif // CKFFTEXTUREBINDER_H
