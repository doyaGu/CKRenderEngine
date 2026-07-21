#ifndef CKFFDRAWPREPARER_H
#define CKFFDRAWPREPARER_H

#include "CKFFDrawTypes.h"
#include "CKRenderConfig.h"

class CKDrawStateCache;
class CKFFDrawProbes;
class CKFFShaderCache;
struct CKFFStateStore;

class CKFFDrawPreparer {
public:
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawPreparer(const CKFFStateStore &state,
                     const CKDrawStateCache &drawState,
                     CKFFShaderCache &shaderCache,
                     CKFFDrawProbes &probes);
#else
    CKFFDrawPreparer(const CKFFStateStore &state,
                     const CKDrawStateCache &drawState,
                     CKFFShaderCache &shaderCache);
#endif

    // Resolve shader state without touching an encoder. The result can be
    // consumed by either immediate submission or deferred packet capture.
    CKFFProgramPrepareStatus PrepareProgram(
        CKFFProgramPreparation *preparation,
        CKDWORD dpFlags,
        CKDWORD activeTextureCount,
        CKDWORD formatFlags = 0,
        const CKBYTE *texcoordComponentCounts = nullptr,
        CKBOOL pointSprite = FALSE) const;

    CKFFProgramPrepareStatus PrepareVertexBufferProgram(
        CKFFProgramPreparation *preparation,
        CKDWORD dpFlags,
        CKDWORD formatFlags);
    void InvalidateVertexBufferProgramCache();

private:
    const CKFFStateStore &m_State;
    const CKDrawStateCache &m_DrawState;
    CKFFShaderCache &m_ShaderCache;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes &m_Probes;
#endif
    CKBOOL m_VertexBufferCacheValid;
    CKDWORD m_VertexBufferCacheDPFlags;
    CKDWORD m_VertexBufferCacheFormatFlags;
    CKDWORD m_VertexBufferCacheActiveTextureCount;
    CKFFProgramPreparation m_VertexBufferCache;
};

#endif // CKFFDRAWPREPARER_H
