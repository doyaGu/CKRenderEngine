#include "CKFFDrawPreparer.h"

#include "CKDrawStateCache.h"
#include "CKFFDrawProbes.h"
#include "CKFFShaderCache.h"
#include "CKFFStageState.h"
#include "CKFFStateResolver.h"
#include "CKFFStateStore.h"

#if CKRE_ENABLE_FFP_DIAGNOSTICS
CKFFDrawPreparer::CKFFDrawPreparer(const CKFFStateStore &state,
                                   const CKDrawStateCache &drawState,
                                   CKFFShaderCache &shaderCache,
                                   CKFFDrawProbes &probes)
    : m_State(state), m_DrawState(drawState), m_ShaderCache(shaderCache),
      m_Probes(probes), m_VertexBufferCacheValid(FALSE),
      m_VertexBufferCacheDPFlags(0), m_VertexBufferCacheFormatFlags(0),
      m_VertexBufferCacheActiveTextureCount(0)
{
    CKFFInitPreparedState(&m_VertexBufferCache.PreparedState);
    CKFFInitProgramContext(
        &m_VertexBufferCache.ProgramContext,
        CKFFShaderKey(), CKFFProgramBinding());
}
#else
CKFFDrawPreparer::CKFFDrawPreparer(const CKFFStateStore &state,
                                   const CKDrawStateCache &drawState,
                                   CKFFShaderCache &shaderCache)
    : m_State(state), m_DrawState(drawState), m_ShaderCache(shaderCache),
      m_VertexBufferCacheValid(FALSE), m_VertexBufferCacheDPFlags(0),
      m_VertexBufferCacheFormatFlags(0),
      m_VertexBufferCacheActiveTextureCount(0)
{
    CKFFInitPreparedState(&m_VertexBufferCache.PreparedState);
    CKFFInitProgramContext(
        &m_VertexBufferCache.ProgramContext,
        CKFFShaderKey(), CKFFProgramBinding());
}
#endif

CKFFProgramPrepareStatus CKFFDrawPreparer::PrepareProgram(
    CKFFProgramPreparation *preparation,
    CKDWORD dpFlags,
    CKDWORD activeTextureCount,
    CKDWORD formatFlags,
    const CKBYTE *texcoordComponentCounts,
    CKBOOL pointSprite) const
{
    if (!preparation)
        return CKFF_PROGRAM_PREPARE_INVALID_INPUT;

    {
        CKFF_SCOPE_TIME(m_Probes, StateUs);
        CKFFStateResolver::BuildPreparedState(
            m_State, m_DrawState, &preparation->PreparedState,
            dpFlags, activeTextureCount, formatFlags,
            texcoordComponentCounts, pointSprite);
    }

    const CKFFShaderKey shaderKey =
        CKFFBuildShaderKeyFromPreparedState(&preparation->PreparedState);
    CKFFInitProgramContext(
        &preparation->ProgramContext, shaderKey, CKFFProgramBinding());
    if (!m_ShaderCache.SupportsSamplerLayout(shaderKey))
        return CKFF_PROGRAM_PREPARE_SAMPLER_LAYOUT;

    CKFFProgramBinding programBinding;
    {
        CKFF_SCOPE_TIME(m_Probes, ProgramUs);
        programBinding = m_ShaderCache.GetProgram(shaderKey);
    }
    CKFFInitProgramContext(
        &preparation->ProgramContext, shaderKey, programBinding);
    return preparation->ProgramContext.Program != 0
        ? CKFF_PROGRAM_PREPARE_OK
        : CKFF_PROGRAM_PREPARE_PROGRAM_MISSING;
}

CKFFProgramPrepareStatus CKFFDrawPreparer::PrepareVertexBufferProgram(
    CKFFProgramPreparation *preparation,
    CKDWORD dpFlags,
    CKDWORD formatFlags)
{
    if (!preparation)
        return CKFF_PROGRAM_PREPARE_INVALID_INPUT;

    const CKDWORD activeTextureCount =
        (CKDWORD)CKFFResolveActiveTextureStageCount(
            m_State.TextureHandles, m_State.StageStates);
    if (m_VertexBufferCacheValid &&
        m_VertexBufferCacheDPFlags == dpFlags &&
        m_VertexBufferCacheFormatFlags == formatFlags &&
        m_VertexBufferCacheActiveTextureCount == activeTextureCount) {
        *preparation = m_VertexBufferCache;
        return CKFF_PROGRAM_PREPARE_OK;
    }

    const CKFFProgramPrepareStatus status = PrepareProgram(
        preparation, dpFlags, activeTextureCount, formatFlags);
    if (status == CKFF_PROGRAM_PREPARE_OK) {
        m_VertexBufferCacheDPFlags = dpFlags;
        m_VertexBufferCacheFormatFlags = formatFlags;
        m_VertexBufferCacheActiveTextureCount = activeTextureCount;
        m_VertexBufferCache = *preparation;
        m_VertexBufferCacheValid = TRUE;
    }
    return status;
}

void CKFFDrawPreparer::InvalidateVertexBufferProgramCache()
{
    m_VertexBufferCacheValid = FALSE;
}
