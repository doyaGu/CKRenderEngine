#ifndef CKFFOPAQUEPACKETCOORDINATOR_H
#define CKFFOPAQUEPACKETCOORDINATOR_H

#include "CKFFDrawTypes.h"
#include "CKFFRenderPacketQueue.h"

class CKFixedFunctionPipeline;
struct CKFFRenderPacketReplayContext;
struct CKFFPipelineTestAccess;

class CKFFOpaquePacketCoordinator {
public:
    CKFFOpaquePacketCoordinator();

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

    CKDWORD GetAdaptiveSamples() const { return m_Queue.GetAdaptiveSamples(); }
    CKDWORD GetAdaptiveBypasses() const { return m_Queue.GetAdaptiveBypasses(); }
    CKDWORD GetAdaptiveSavedBindEstimate() const { return m_Queue.GetAdaptiveSavedBindEstimate(); }
    CKDWORD GetAdaptiveRunBypasses() const { return m_Queue.GetAdaptiveRunBypasses(); }
    CKDWORD GetAdaptiveSampleRuns() const { return m_Queue.GetAdaptiveSampleRuns(); }
    CKDWORD GetAdaptiveSampleMaxRun() const { return m_Queue.GetAdaptiveSampleMaxRun(); }
    CKDWORD GetAdaptiveSubmitSavedEstimate() const { return m_Queue.GetAdaptiveSubmitSavedEstimate(); }
    CKDWORD GetAdaptiveCooldownBypasses() const { return m_Queue.GetAdaptiveCooldownBypasses(); }
    CKDWORD GetAdaptiveCooldownFrames() const { return m_Queue.GetAdaptiveCooldownFrames(); }
    CKDWORD GetAdaptiveFrameEndEvaluations() const { return m_Queue.GetAdaptiveFrameEndEvaluations(); }
    CKDWORD GetAdaptiveFrameEndRunBypasses() const { return m_Queue.GetAdaptiveFrameEndRunBypasses(); }

    void DrawVertexBuffer(CKFixedFunctionPipeline &pipeline,
                          CKRasterizerEncoder *encoder,
                          CKRenderView view,
                          VXPRIMITIVETYPE type,
                          CKDWORD vb,
                          CKDWORD ib,
                          CKDWORD baseVertex,
                          CKDWORD vertexCount,
                          CKDWORD startIndex,
                          CKDWORD indexCount,
                          CKDWORD dpFlags,
                          CKDWORD formatFlags,
                          CKDWORD vertexLayout);
    void ClearRenderPackets() { m_Queue.Clear(); }
    void ResetRenderPacketFrameState(CKFixedFunctionPipeline &pipeline);
    void FlushRenderPackets(CKFixedFunctionPipeline &pipeline,
                            CKRasterizerEncoder *encoder,
                            CKBOOL forceDirectReplay,
                            CKBOOL allowAdaptiveLearning);

private:
#if CKRE_ENABLE_TEST_ACCESS
    friend struct CKFFPipelineTestAccess;
#endif

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
    CKDWORD GetPacketObjectUniformRejectReason(const CKFFProgramContext *programContext) const;
    CKDWORD GetVertexBufferPacketInstancingRejectReason(
        const CKFFProgramContext *programContext) const;
    CKDWORD InternStaticUniformPayload(CKFixedFunctionPipeline &pipeline,
                                       const CKFFRenderPacketUniformPayload &payload);
    void BuildRenderPacketSortKey(CKRenderPacket *packet) const;
    void InitVertexBufferPacketForCapture(CKRenderPacket *packet) const;
    void TrackOpaqueRenderPacket(CKFixedFunctionPipeline &pipeline, const CKRenderPacket &packet);
    void TrackOpaqueRenderPacketReject(CKFixedFunctionPipeline &pipeline, CKDWORD rejectReason);
    void UpdateAdaptiveStats(CKFixedFunctionPipeline &pipeline);
    CKBOOL CheckAdaptiveBypass(CKFixedFunctionPipeline &pipeline, CKRasterizerEncoder *encoder);
    CKBOOL ResolveVertexBufferPacketProgram(CKFixedFunctionPipeline &pipeline,
                                            CKDWORD dpFlags,
                                            CKDWORD formatFlags,
                                            CKFFPreparedState *preparedState,
                                            CKFFProgramContext *programContext);
    void CaptureVertexBufferPacketIdentity(CKFixedFunctionPipeline &pipeline,
                                           CKRenderPacket *packet,
                                           const CKFFProgramContext *programContext,
                                           CKRenderView view,
                                           VXPRIMITIVETYPE type,
                                           CKDWORD vb,
                                           CKDWORD ib,
                                           CKDWORD baseVertex,
                                           CKDWORD vertexCount,
                                           CKDWORD startIndex,
                                           CKDWORD indexCount,
                                           CKDWORD vertexLayout);
    void CaptureVertexBufferPacketTextures(CKRenderPacket *packet,
                                           const CKFFTextureBindingSet *bindingSet);
    CKBOOL CaptureVertexBufferPacketObjectUniforms(CKFixedFunctionPipeline &pipeline,
                                                   CKRenderPacket *packet,
                                                   const CKFFProgramContext *programContext);
    void CaptureVertexBufferPacketInstancing(CKFixedFunctionPipeline &pipeline,
                                             CKRenderPacket *packet,
                                             const CKFFProgramContext *programContext);
    CKBOOL CaptureVertexBufferPacketStaticUniforms(CKFixedFunctionPipeline &pipeline,
                                                   CKRenderPacket *packet,
                                                   const CKFFProgramContext *programContext,
                                                   CKBOOL collectStats);
    CKDWORD GetOpaqueVertexBufferPacketRejectReason(const CKFixedFunctionPipeline &pipeline,
                                                    CKRenderView view,
                                                    VXPRIMITIVETYPE type,
                                                    CKDWORD vb,
                                                    CKDWORD ib,
                                                    CKDWORD vertexLayout) const;
    void BuildVertexBufferPacket(CKFixedFunctionPipeline &pipeline,
                                 CKFFVertexBufferPacketBuildResult *result,
                                 CKRasterizerEncoder *encoder,
                                 CKRenderView view,
                                 VXPRIMITIVETYPE type,
                                 CKDWORD vb,
                                 CKDWORD ib,
                                 CKDWORD baseVertex,
                                 CKDWORD vertexCount,
                                 CKDWORD startIndex,
                                 CKDWORD indexCount,
                                 CKDWORD dpFlags,
                                 CKDWORD formatFlags,
                                 CKDWORD vertexLayout);
    void SortRenderPackets(XArray<CKDWORD> &indices);
    void InitRenderPacketReplayContext(CKFixedFunctionPipeline &pipeline,
                                       CKFFRenderPacketReplayContext *context,
                                       CKRasterizerEncoder *encoder);

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
