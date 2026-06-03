#ifndef CKFFOPAQUEPACKETCOORDINATOR_H
#define CKFFOPAQUEPACKETCOORDINATOR_H

#include "CKFFDrawTypes.h"
#include "CKFFRenderPacketQueue.h"

class CKFFOpaquePacketCoordinator {
public:
    CKFFOpaquePacketCoordinator();

    CKFFRenderPacketQueue &Queue() { return m_Queue; }
    const CKFFRenderPacketQueue &Queue() const { return m_Queue; }

    CKBOOL HasPackets() const { return m_Queue.HasPackets(); }
    void Clear() { m_Queue.Clear(); }
    void ResetFrameState() { m_Queue.ResetFrameState(); }

    void MarkStaticUniformsDirty() { m_Queue.MarkStaticUniformsDirty(); }
    void MarkPacketProgramDirty() { m_PacketProgramCacheValid = FALSE; }

    void SetSortingEnabled(CKBOOL enabled) { m_SortingEnabled = enabled; }
    CKBOOL SortingEnabled() const { return m_SortingEnabled; }

    void SetInstancingEnabled(CKBOOL enabled) { m_InstancingEnabled = enabled; }
    CKBOOL InstancingEnabled() const { return m_InstancingEnabled; }

    void SetPacketsAllowed(CKBOOL allowed) { m_PacketsAllowed = allowed; }
    CKBOOL PacketsAllowed() const { return m_PacketsAllowed; }

    void SetInstanceLayout(CKDWORD instanceLayout) { m_InstanceLayout = instanceLayout; }
    CKDWORD InstanceLayout() const { return m_InstanceLayout; }

    CKBOOL TryGetCachedProgram(CKDWORD dpFlags,
                               CKDWORD formatFlags,
                               CKDWORD activeTextureCount,
                               CKFFPreparedState *preparedState,
                               CKFFProgramContext *programContext) const;
    void CacheProgram(CKDWORD dpFlags,
                      CKDWORD formatFlags,
                      CKDWORD activeTextureCount,
                      const CKFFPreparedState &preparedState,
                      const CKFFProgramContext &programContext);

private:
    CKFFRenderPacketQueue m_Queue;
    CKBOOL m_InstancingEnabled;
    CKDWORD m_InstanceLayout;
    CKBOOL m_SortingEnabled;
    CKBOOL m_PacketsAllowed;

    CKBOOL m_PacketProgramCacheValid;
    CKDWORD m_PacketProgramCacheDPFlags;
    CKDWORD m_PacketProgramCacheFormatFlags;
    CKDWORD m_PacketProgramCacheActiveTextureCount;
    CKFFPreparedState m_PacketProgramCachePreparedState;
    CKFFProgramContext m_PacketProgramCacheContext;
};

#endif // CKFFOPAQUEPACKETCOORDINATOR_H
