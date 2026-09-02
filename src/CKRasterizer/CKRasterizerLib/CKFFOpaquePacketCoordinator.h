#ifndef CKFFOPAQUEPACKETCOORDINATOR_H
#define CKFFOPAQUEPACKETCOORDINATOR_H

#include "CKFFDrawTypes.h"
#include "CKFFRenderPacketQueue.h"

class CKFixedFunctionPipeline;
struct CKFFRenderPacketReplayContext;
struct CKFFPipelineTestAccess;

struct CKFFOpaquePacketAdaptiveStats {
    CKDWORD Samples;
    CKDWORD Bypasses;
    CKDWORD SavedBindEstimate;
    CKDWORD RunBypasses;
    CKDWORD SampleRuns;
    CKDWORD SampleMaxRun;
    CKDWORD SubmitSavedEstimate;
    CKDWORD CooldownBypasses;
    CKDWORD CooldownFrames;
    CKDWORD FrameEndEvaluations;
    CKDWORD FrameEndRunBypasses;
};

class CKFFOpaquePacketCoordinator {
public:
    CKFFOpaquePacketCoordinator();

    CKBOOL HasPackets() const { return m_Queue.HasPackets(); }
    void Clear() { m_Queue.Clear(); }
    void ResetFrameState() { m_Queue.ResetFrameState(); }

    void MarkStaticUniformsDirty() { m_Queue.MarkStaticUniformsDirty(); }

    void SetSortingEnabled(CKBOOL enabled) { m_SortingEnabled = enabled; }
    CKBOOL SortingEnabled() const { return m_SortingEnabled; }

    void SetInstancingEnabled(CKBOOL enabled) { m_InstancingEnabled = enabled; }
    CKBOOL InstancingEnabled() const { return m_InstancingEnabled; }

    void SetPacketsAllowed(CKBOOL allowed) { m_PacketsAllowed = allowed; }
    CKBOOL PacketsAllowed() const { return m_PacketsAllowed; }

    void SetInstanceLayout(CKDWORD instanceLayout) { m_InstanceLayout = instanceLayout; }
    CKDWORD InstanceLayout() const { return m_InstanceLayout; }

    CKFFOpaquePacketAdaptiveStats GetAdaptiveStats() const;

    CKBOOL DrawVertexBuffer(CKFixedFunctionPipeline &pipeline,
                           const CKFFProgramPreparation &preparation,
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
                                 const CKFFProgramPreparation &preparation,
                                 CKRasterizerEncoder *encoder,
                                 CKRenderView view,
                                 VXPRIMITIVETYPE type,
                                 CKDWORD vb,
                                 CKDWORD ib,
                                 CKDWORD baseVertex,
                                 CKDWORD vertexCount,
                                 CKDWORD startIndex,
                                 CKDWORD indexCount,
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

};

#endif // CKFFOPAQUEPACKETCOORDINATOR_H
