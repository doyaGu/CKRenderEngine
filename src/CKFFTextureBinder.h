#ifndef CKFFTEXTUREBINDER_H
#define CKFFTEXTUREBINDER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFShaderCache.h"
#include "CKFFStageState.h"
#include "CKFFStateStore.h"
#include "XHashTable.h"

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
    void ResetProgramBindings();
    void BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                         CKDWORD sampledTextureMask) const;
    CKBOOL InitializeProgramSamplers(CKRasterizerEncoder *encoder,
                                     CKDWORD program);
    void Bind(CKRasterizerEncoder *encoder, CKDWORD program,
              const CKFFTextureBindingSet *set);
    CKSamplerDesc BuildSamplerDesc(int stage) const;

private:
    const CKFFStateStore &m_State;
    CKFFShaderCache &m_ShaderCache;
    XHashTable<CKBOOL, CKDWORD> m_InitializedPrograms;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes &m_Probes;
#endif
    CKFFSamplerOverrides m_SamplerOverrides;
};

#endif // CKFFTEXTUREBINDER_H
