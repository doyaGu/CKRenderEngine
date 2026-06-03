#include "CKFFOpaquePacketCoordinator.h"
#include "CKFixedFunctionPipeline.h"
#include "CKFFRenderPacketReplay.h"
#include "CKRasterizer.h"

#include <cstring>

static CKDWORD CKFFCoordinatorShaderKeyVertexBlendMode(const CKFFShaderKeyVS &vs)
{
    return (CKDWORD)((vs.Bits >> 35) & 3u);
}

static CKFFShaderKey CKFFCoordinatorBuildCurrentShaderKey(const CKFFPreparedState *prepared)
{
    if (!prepared)
        return CKFFShaderKey();
    return CKFFBuildShaderKey(prepared->StateDesc, prepared->TextureBoundMask);
}

static void CKFFCoordinatorInitVertexBufferPacketBuildResult(CKFFVertexBufferPacketBuildResult *result)
{
    if (!result)
        return;
    result->Success = FALSE;
    result->RejectReason = CKFF_RENDER_PACKET_ELIGIBLE;
    CKFFInitProgramContext(&result->ProgramContext, CKFFShaderKey(), CKFFProgramBinding());
    CKFFInitTextureBindingSet(&result->TextureBindingSet);
    memset(&result->Packet, 0, sizeof(result->Packet));
}

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

CKDWORD CKFFOpaquePacketCoordinator::GetPacketObjectUniformRejectReason(
    const CKFFProgramContext *programContext) const
{
    if (!programContext)
        return CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_ELIGIBLE;
    if (CKFFCoordinatorShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

CKDWORD CKFFOpaquePacketCoordinator::GetVertexBufferPacketInstancingRejectReason(
    const CKFFProgramContext *programContext) const
{
    if (!programContext)
        return CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;
    if (!m_InstancingEnabled || !m_InstanceLayout)
        return CKFF_RENDER_PACKET_REJECT_INSTANCE_LAYOUT;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_REJECT_POSITIONT;
    if (CKFFCoordinatorShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    if (shaderKey.FS.LastActiveTextureStage >= 4)
        return CKFF_RENDER_PACKET_REJECT_TEXCOORD_RANGE;
    for (CKDWORD stage = 0; stage <= shaderKey.FS.LastActiveTextureStage; ++stage) {
        if ((shaderKey.VS.TexCoordIndex[stage] & 7u) >= 4)
            return CKFF_RENDER_PACKET_REJECT_TEXCOORD_RANGE;
        if ((shaderKey.VS.TexGen[stage] & 7u) != 0)
            return CKFF_RENDER_PACKET_REJECT_TEXGEN;
    }
    if ((shaderKey.VS.Bits & (1ull << 13)) != 0)
        return CKFF_RENDER_PACKET_REJECT_VIEW_SPACE_SHADER;
    if (shaderKey.FS.VertexFogMode != 0)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_FOG;
    if (shaderKey.FS.PixelFogMode != 0)
        return CKFF_RENDER_PACKET_REJECT_PIXEL_FOG;
    if (shaderKey.FS.RangeFog)
        return CKFF_RENDER_PACKET_REJECT_RANGE_FOG;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

CKDWORD CKFFOpaquePacketCoordinator::InternStaticUniformPayload(
    CKFixedFunctionPipeline &pipeline,
    const CKFFRenderPacketUniformPayload &payload)
{
    CKBOOL interned = FALSE;
    CKDWORD index = m_Queue.InternStaticUniformPayload(payload, &interned);
    if (interned)
        CKFF_PROBE(pipeline.m_Probes, OnRenderPacketStaticPayloadIntern());
    return index;
}

void CKFFOpaquePacketCoordinator::BuildRenderPacketSortKey(CKRenderPacket *packet) const
{
    m_Queue.BuildSortKey(packet);
}

void CKFFOpaquePacketCoordinator::InitVertexBufferPacketForCapture(CKRenderPacket *packet) const
{
    if (!packet)
        return;

    packet->Serial = 0;
    packet->View = CKRP_VIEW_OPAQUE3D;
    packet->Type = VX_TRIANGLELIST;
    packet->Program = 0;
    packet->Depth = 0;
    packet->DrawState.Lo = 0;
    packet->DrawState.Mid = 0;
    packet->DrawState.Hi = 0;
    packet->StencilRef = 0;
    packet->StencilReadMask = 0;
    packet->StencilWriteMask = 0;
    packet->VertexLayout = 0;
    packet->VertexBuffer = 0;
    packet->IndexBuffer = 0;
    packet->BaseVertex = 0;
    packet->VertexCount = 0;
    packet->StartIndex = 0;
    packet->IndexCount = 0;
    packet->ActiveTextureCount = 0;
    packet->TextureSetHash = 0;
    memset(packet->Textures, 0, sizeof(packet->Textures));
    packet->StaticUniformIndex = 0;
    memset(&packet->ObjectUniforms, 0, sizeof(packet->ObjectUniforms));
    memset(&packet->SortKey, 0, sizeof(packet->SortKey));
    packet->World.SetIdentity();
    packet->ViewProjection.SetIdentity();
    packet->ViewProjectionHash = 0;
    packet->CanInstance = FALSE;
    packet->InstancedProgram = 0;
    packet->Marker[0] = '\0';
}

void CKFFOpaquePacketCoordinator::TrackOpaqueRenderPacket(CKFixedFunctionPipeline &pipeline,
                                                          const CKRenderPacket &packet)
{
    m_Queue.AddPacket(packet);
    UpdateAdaptiveStats(pipeline);
}

void CKFFOpaquePacketCoordinator::TrackOpaqueRenderPacketReject(CKFixedFunctionPipeline &pipeline,
                                                                CKDWORD rejectReason)
{
    if (rejectReason != CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS)
        return;
    if (!m_Queue.IsAdaptiveCooldownActive())
        return;
    m_Queue.MarkAdaptiveCooldownBypass();
    UpdateAdaptiveStats(pipeline);
}

void CKFFOpaquePacketCoordinator::UpdateAdaptiveStats(CKFixedFunctionPipeline &pipeline)
{
    CKFF_PROBE(pipeline.m_Probes, OnAdaptiveStats(m_Queue));
}

CKBOOL CKFFOpaquePacketCoordinator::CheckAdaptiveBypass(CKFixedFunctionPipeline &pipeline,
                                                        CKRasterizerEncoder *encoder)
{
    if (m_Queue.IsAdaptiveBypassed())
        return TRUE;
    if (!m_Queue.ShouldAdaptiveBypass(m_InstancingEnabled))
        return FALSE;

    m_Queue.MarkAdaptiveBypass();
    CKFF_PROBE(pipeline.m_Probes, OnAdaptiveBypass(m_Queue));
    UpdateAdaptiveStats(pipeline);
    FlushRenderPackets(pipeline, encoder, TRUE, FALSE);
    return TRUE;
}

CKBOOL CKFFOpaquePacketCoordinator::ResolveVertexBufferPacketProgram(
    CKFixedFunctionPipeline &pipeline,
    CKDWORD dpFlags,
    CKDWORD formatFlags,
    CKFFPreparedState *preparedState,
    CKFFProgramContext *programContext)
{
    if (!preparedState || !programContext)
        return FALSE;

    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, pipeline.m_State.TextureHandles, pipeline.m_State.StageStates);
    if (TryGetCachedProgram(dpFlags, formatFlags, activeTextureCount,
                            preparedState, programContext)) {
        return programContext->Program != 0 ? TRUE : FALSE;
    }

    pipeline.BuildCurrentPreparedState(preparedState, dpFlags, activeTextureCount, formatFlags);
    CKFFShaderKey shaderKey = CKFFCoordinatorBuildCurrentShaderKey(preparedState);
    CKFFProgramBinding programBinding = pipeline.m_ShaderCache.GetProgram(shaderKey);
    CKFFInitProgramContext(programContext, shaderKey, programBinding);
    CacheProgram(dpFlags, formatFlags, activeTextureCount, *preparedState, *programContext);
    return programContext->Program != 0 ? TRUE : FALSE;
}

void CKFFOpaquePacketCoordinator::CaptureVertexBufferPacketIdentity(
    CKFixedFunctionPipeline &pipeline,
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
    CKDWORD vertexLayout)
{
    if (!packet || !programContext)
        return;

    packet->Serial = m_Queue.NextSerial();
    packet->View = view;
    packet->Type = type;
    packet->Program = programContext->Program;
    float depth = pipeline.ComputeDepthKey();
    packet->Depth = *(CKDWORD *)&depth;
    packet->DrawState = pipeline.m_DrawStateCache.BuildDrawState(type);
    packet->StencilRef = pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    packet->StencilReadMask = pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    packet->StencilWriteMask = pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    packet->VertexLayout = vertexLayout;
    packet->VertexBuffer = vb;
    packet->IndexBuffer = ib;
    packet->BaseVertex = baseVertex;
    packet->VertexCount = vertexCount;
    packet->StartIndex = startIndex;
    packet->IndexCount = indexCount;
    packet->ActiveTextureCount = 0;
    packet->TextureSetHash = 0;
    packet->StaticUniformIndex = 0;
    packet->CanInstance = FALSE;
    packet->InstancedProgram = 0;
    memset(&packet->ObjectUniforms, 0, sizeof(packet->ObjectUniforms));
    packet->World = pipeline.m_State.World;
    packet->Marker[0] = '\0';
}

void CKFFOpaquePacketCoordinator::CaptureVertexBufferPacketTextures(
    CKRenderPacket *packet,
    const CKFFTextureBindingSet *bindingSet)
{
    if (!packet || !bindingSet)
        return;

    packet->ActiveTextureCount = bindingSet->ActiveTextureCount;
    packet->TextureSetHash = bindingSet->Hash;
    for (CKDWORD i = 0; i < packet->ActiveTextureCount; ++i) {
        packet->Textures[i] = bindingSet->Bindings[i];
    }
}

CKBOOL CKFFOpaquePacketCoordinator::CaptureVertexBufferPacketObjectUniforms(
    CKFixedFunctionPipeline &pipeline,
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext)
{
    if (!packet)
        return FALSE;

    if (packet->CanInstance) {
        pipeline.UpdateViewProjectionCache();
        memset(&packet->ObjectUniforms, 0, sizeof(packet->ObjectUniforms));
        packet->ObjectUniforms.MatrixUniform = pipeline.m_ShaderCache.GetUniforms().u_ffMatrices;
    } else {
        if (!pipeline.BuildPacketObjectUniforms(&packet->ObjectUniforms, programContext))
            return FALSE;
    }
    packet->ViewProjection = pipeline.m_State.ViewProjection();
    packet->ViewProjectionHash = pipeline.m_State.ViewProjectionHash();
    return TRUE;
}

void CKFFOpaquePacketCoordinator::CaptureVertexBufferPacketInstancing(
    CKFixedFunctionPipeline &pipeline,
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext)
{
    if (!packet || !programContext)
        return;
    if (GetVertexBufferPacketInstancingRejectReason(programContext) != CKFF_RENDER_PACKET_ELIGIBLE)
        return;

    CKFFShaderKey instancedKey = programContext->ShaderKey;
    instancedKey.VS.SetInstanced(true);
    CKFFProgramBinding instancedBinding = pipeline.m_ShaderCache.GetProgram(instancedKey);
    CKFFProgramContext instancedContext;
    CKFFInitProgramContext(&instancedContext, instancedKey, instancedBinding);
    if (CKFFCanUseInstancedProgramForPacket(*programContext, instancedContext)) {
        packet->CanInstance = TRUE;
        packet->InstancedProgram = instancedContext.Program;
    }
}

CKBOOL CKFFOpaquePacketCoordinator::CaptureVertexBufferPacketStaticUniforms(
    CKFixedFunctionPipeline &pipeline,
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext,
    CKBOOL collectStats)
{
    if (!packet || !programContext)
        return FALSE;

    if (m_Queue.TryUseCachedStaticUniform(&packet->StaticUniformIndex)) {
        if (collectStats)
            CKFF_PROBE(pipeline.m_Probes, OnRenderPacketStaticPayloadReuse());
    } else {
        CKFFRenderPacketUniformPayload staticPayload;
        CKBOOL payloadBuilt = FALSE;
        {
            CKFF_SCOPE_TIME(pipeline.m_Probes, RenderPacketBuildUs);
            payloadBuilt = pipeline.BuildStaticUniformPayload(
                &staticPayload, programContext, packet->ActiveTextureCount);
        }
        if (!payloadBuilt) {
            if (collectStats)
                CKFF_PROBE(pipeline.m_Probes, OnRenderPacketUniformOverflow());
            return FALSE;
        }
        if (collectStats)
            CKFF_PROBE(pipeline.m_Probes, OnRenderPacketStaticPayloadBuild());
        packet->StaticUniformIndex = InternStaticUniformPayload(pipeline, staticPayload);
        m_Queue.CacheStaticUniform(packet->StaticUniformIndex);
    }

    return TRUE;
}

CKDWORD CKFFOpaquePacketCoordinator::GetOpaqueVertexBufferPacketRejectReason(
    const CKFixedFunctionPipeline &pipeline,
    CKRenderView view,
    VXPRIMITIVETYPE type,
    CKDWORD vb,
    CKDWORD ib,
    CKDWORD vertexLayout) const
{
    if (!m_SortingEnabled)
        return CKFF_RENDER_PACKET_REJECT_SORT_DISABLED;
    if (!m_PacketsAllowed)
        return CKFF_RENDER_PACKET_REJECT_PACKETS_DISALLOWED;
    if (!pipeline.m_Context)
        return CKFF_RENDER_PACKET_REJECT_NO_CONTEXT;
    if (!vb)
        return CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_BUFFER;
    if (!ib)
        return CKFF_RENDER_PACKET_REJECT_MISSING_INDEX_BUFFER;
    if (!vertexLayout)
        return CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_LAYOUT;
    if (view != CKRP_VIEW_OPAQUE3D)
        return CKFF_RENDER_PACKET_REJECT_WRONG_VIEW;
    if (type != VX_TRIANGLELIST)
        return CKFF_RENDER_PACKET_REJECT_WRONG_PRIMITIVE;
    if (pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE))
        return CKFF_RENDER_PACKET_REJECT_ALPHA_BLEND;
    if (!pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZENABLE))
        return CKFF_RENDER_PACKET_REJECT_Z_DISABLED;
    if (!pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZWRITEENABLE))
        return CKFF_RENDER_PACKET_REJECT_Z_WRITE_DISABLED;
    if (pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND) != VXVBLEND_DISABLE)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    if (pipeline.m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE))
        return CKFF_RENDER_PACKET_REJECT_INDEXED_VERTEX_BLEND;
    if (m_Queue.IsAdaptiveBypassed())
        return CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFFOpaquePacketCoordinator::BuildVertexBufferPacket(
    CKFixedFunctionPipeline &pipeline,
    CKFFVertexBufferPacketBuildResult *result,
    CKRasterizerEncoder *encoder,
    CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!result)
        return;
    CKFFCoordinatorInitVertexBufferPacketBuildResult(result);
    if (!vb) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_BUFFER;
        return;
    }

    CKBOOL collectStats = FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    collectStats = pipeline.m_Probes.StatsEnabled() ? TRUE : FALSE;
#endif
    CKFF_PROBE(pipeline.m_Probes, OnHardwareDraw());

    CKFFPreparedState preparedState;
    CKFFProgramContext programContext;
    if (!ResolveVertexBufferPacketProgram(pipeline, dpFlags, formatFlags,
                                          &preparedState, &programContext)) {
        if (collectStats)
            CKFF_PROBE(pipeline.m_Probes, OnProgramMiss());
        result->ProgramContext = programContext;
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;
        return;
    }
    result->ProgramContext = programContext;
    pipeline.BuildCurrentTextureBindingSet(&result->TextureBindingSet, preparedState.ActiveTextureCount);
    result->RejectReason = GetPacketObjectUniformRejectReason(&programContext);
    if (result->RejectReason != CKFF_RENDER_PACKET_ELIGIBLE) {
        return;
    }

    InitVertexBufferPacketForCapture(&result->Packet);
    CaptureVertexBufferPacketIdentity(pipeline, &result->Packet, &programContext,
                                      view, type, vb, ib, baseVertex, vertexCount,
                                      startIndex, indexCount, vertexLayout);
    CaptureVertexBufferPacketTextures(&result->Packet, &result->TextureBindingSet);
    CaptureVertexBufferPacketInstancing(pipeline, &result->Packet, &programContext);

    CKBOOL objectUniformsBuilt = FALSE;
    {
        CKFF_SCOPE_TIME(pipeline.m_Probes, RenderPacketBuildUs);
        objectUniformsBuilt = CaptureVertexBufferPacketObjectUniforms(
            pipeline, &result->Packet, &programContext);
    }
    if (!objectUniformsBuilt) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_OBJECT_UNIFORMS;
        return;
    }

    if (!CaptureVertexBufferPacketStaticUniforms(
            pipeline, &result->Packet, &programContext, collectStats)) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_STATIC_UNIFORMS;
        return;
    }

    if (encoder)
        encoder->ConsumeMarker(result->Packet.Marker, sizeof(result->Packet.Marker));
    BuildRenderPacketSortKey(&result->Packet);
    result->Success = TRUE;
    result->RejectReason = CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFFOpaquePacketCoordinator::ResetRenderPacketFrameState(CKFixedFunctionPipeline &pipeline)
{
    m_Queue.ResetFrameState();
    UpdateAdaptiveStats(pipeline);
}

void CKFFOpaquePacketCoordinator::SortRenderPackets(XArray<CKDWORD> &indices)
{
    m_Queue.SortPackets(indices);
}

void CKFFOpaquePacketCoordinator::InitRenderPacketReplayContext(
    CKFixedFunctionPipeline &pipeline,
    CKFFRenderPacketReplayContext *context,
    CKRasterizerEncoder *encoder)
{
    if (!context)
        return;

    memset(context, 0, sizeof(CKFFRenderPacketReplayContext));
    context->Encoder = encoder;
    context->Context = pipeline.m_Context;
    context->Queue = &m_Queue;
    context->InstanceLayout = m_InstanceLayout;
    CKFFInitRenderPacketReplayDiagnostics(&context->Diagnostics);
    CKFF_PROBE(pipeline.m_Probes,
               FillReplayDiagnostics(&context->Diagnostics, pipeline.m_ShaderCache.GetUniforms()));
}

void CKFFOpaquePacketCoordinator::FlushRenderPackets(CKFixedFunctionPipeline &pipeline,
                                                     CKRasterizerEncoder *encoder,
                                                     CKBOOL forceDirectReplay,
                                                     CKBOOL allowAdaptiveLearning)
{
    if (!HasPackets())
        return;
    if (!encoder)
        encoder = pipeline.m_RenderPipeline.GetEncoder();
    if (!encoder) {
        ClearRenderPackets();
        return;
    }

    const int packetCount = m_Queue.GetPacketCount();
    const CKBOOL directReplay = m_Queue.IsDirectReplay(forceDirectReplay);
    XArray<CKDWORD> indices;
    {
        CKFF_SCOPE_TIME(pipeline.m_Probes, RenderPacketSortUs);
        if (!directReplay)
            SortRenderPackets(indices);
    }
    if (directReplay) {
        if (packetCount > 1)
            CKFF_PROBE(pipeline.m_Probes, OnRenderPacketSortSkip());
    }

    if (allowAdaptiveLearning) {
        m_Queue.EvaluateAdaptiveFrameEnd(m_InstancingEnabled);
        UpdateAdaptiveStats(pipeline);
    }

    CKRenderPacketReplayCache cache;
    memset(&cache, 0, sizeof(cache));
    CKFFRenderPacketReplayContext replayContext;
    InitRenderPacketReplayContext(pipeline, &replayContext, encoder);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (pipeline.m_Probes.TimingEnabled()) {
        CKDWORD runCount = 0;
        CKDWORD maxRun = 0;
        m_Queue.GetRunStats(&indices, directReplay, &runCount, &maxRun);
        pipeline.m_Probes.OnRenderPacketRuns(runCount, maxRun);
    }
#endif
    {
        CKFF_SCOPE_TIME(pipeline.m_Probes, RenderPacketReplayUs);
        XArray<CKFFRenderPacketRunPlan> runPlans;
        m_Queue.BuildRunPlans(&indices, directReplay, m_InstancingEnabled, runPlans);
        const int planCount = runPlans.Size();
        for (int planIndex = 0; planIndex < planCount; ++planIndex) {
            const CKFFRenderPacketRunPlan &plan = runPlans[planIndex];
            const CKBOOL lastPlan = (planIndex + 1 == planCount) ? TRUE : FALSE;
            if (plan.Instanced) {
                if (CKFFReplayVertexBufferPacketRunInstanced(&replayContext, &indices,
                                                             plan.Start, plan.Count,
                                                             directReplay, &cache, lastPlan)) {
                    continue;
                }
            }

            CKFFReplayVertexBufferPacketRange(&replayContext, &indices, plan.Start, 0,
                                              plan.Count, directReplay, &cache, lastPlan);
        }
    }

    ClearRenderPackets();
    CKFF_PROBE(pipeline.m_Probes, OnRenderPacketFlush());
}
