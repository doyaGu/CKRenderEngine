#ifndef CKFFTEXTUREBINDER_H
#define CKFFTEXTUREBINDER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFShaderCache.h"
#include "CKFFStageState.h"
#include "CKFFStateStore.h"
#include "XHashTable.h"

class CKRasterizerBackend;

class CKFFTextureBinder {
public:
    CKFFTextureBinder(const CKFFStateStore &state,
                      CKFFShaderCache &shaderCache,
                      CKFFDrawProbes &probes);

    void SetRenderOptions(CKBOOL disableFilter, CKBOOL disableMipmaps, CKBOOL forceAniso);
    void ResetProgramBindings();
    void BuildBindingSet(CKFFTextureBindingSet *out, CKDWORD activeTextureCount,
                         CKDWORD sampledTextureMask) const;
    CKBOOL InitializeProgramSamplers(CKRasterizerBackend *backend,
                                     CKDWORD program);
    // Binds the set's textures on their slots and clears the slots the
    // previous draw used but this one does not (backend bindings are sticky).
    void Bind(CKRasterizerBackend *backend, CKDWORD program,
              const CKFFTextureBindingSet *set);
    CKSamplerDesc BuildSamplerDesc(int stage) const;

private:
    const CKFFStateStore &m_State;
    CKFFShaderCache &m_ShaderCache;
    XHashTable<CKBOOL, CKDWORD> m_InitializedPrograms;
    CKDWORD m_BoundSlotMask;
    CKFFDrawProbes &m_Probes;
    CKFFSamplerOverrides m_SamplerOverrides;
};

#endif // CKFFTEXTUREBINDER_H
