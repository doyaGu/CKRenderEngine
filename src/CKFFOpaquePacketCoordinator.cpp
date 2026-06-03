#include "CKFFOpaquePacketCoordinator.h"

#include <cstring>

CKFFOpaquePacketCoordinator::CKFFOpaquePacketCoordinator()
    : m_InstancingEnabled(TRUE),
      m_InstanceLayout(0),
      m_SortingEnabled(FALSE),
      m_PacketsAllowed(TRUE),
      m_PacketProgramCacheValid(FALSE),
      m_PacketProgramCacheDPFlags(0),
      m_PacketProgramCacheFormatFlags(0),
      m_PacketProgramCacheActiveTextureCount(0)
{
    CKFFInitPreparedState(&m_PacketProgramCachePreparedState);
    memset(&m_PacketProgramCacheContext, 0, sizeof(m_PacketProgramCacheContext));
}

CKBOOL CKFFOpaquePacketCoordinator::TryGetCachedProgram(CKDWORD dpFlags,
                                                        CKDWORD formatFlags,
                                                        CKDWORD activeTextureCount,
                                                        CKFFPreparedState *preparedState,
                                                        CKFFProgramContext *programContext) const
{
    if (!preparedState || !programContext)
        return FALSE;
    if (!m_PacketProgramCacheValid)
        return FALSE;
    if (m_PacketProgramCacheDPFlags != dpFlags ||
        m_PacketProgramCacheFormatFlags != formatFlags ||
        m_PacketProgramCacheActiveTextureCount != activeTextureCount) {
        return FALSE;
    }

    *preparedState = m_PacketProgramCachePreparedState;
    *programContext = m_PacketProgramCacheContext;
    return TRUE;
}

void CKFFOpaquePacketCoordinator::CacheProgram(CKDWORD dpFlags,
                                               CKDWORD formatFlags,
                                               CKDWORD activeTextureCount,
                                               const CKFFPreparedState &preparedState,
                                               const CKFFProgramContext &programContext)
{
    m_PacketProgramCacheDPFlags = dpFlags;
    m_PacketProgramCacheFormatFlags = formatFlags;
    m_PacketProgramCacheActiveTextureCount = activeTextureCount;
    m_PacketProgramCachePreparedState = preparedState;
    m_PacketProgramCacheContext = programContext;
    m_PacketProgramCacheValid = TRUE;
}
