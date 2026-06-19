#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKFFStateResolver.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfStats.h"
#include "CKRenderFrameCostStats.h"

#include <cmath>
#include <cstring>


CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr),
      m_DisableTextureFiltering(FALSE), m_DisableMipmaps(FALSE),
      m_ForceAnisotropicFiltering(FALSE),
#if CKRE_ENABLE_FFP_DIAGNOSTICS
      m_TextureBinder(m_State, m_ShaderCache, m_Probes),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache, m_Probes),
#else
      m_TextureBinder(m_State, m_ShaderCache),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache),
#endif
      m_OpaquePackets() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_Probes.Config.StatsEnabled = settings.Enabled;
    m_Probes.Config.UniformHistEnabled = settings.UniformHistogram;
    m_Probes.Config.StatsInterval = settings.Interval;
#endif
    m_OpaquePackets.SetSortingEnabled(CKRenderFFPSettings().GetBool("SortOpaqueObjects", false) ? TRUE : FALSE);
    m_OpaquePackets.SetInstancingEnabled(CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true) ? TRUE : FALSE);

    m_State.Reset();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    memset(&m_Probes.Stats, 0, sizeof(m_Probes.Stats));
#endif
}

#if !CKRE_ENABLE_FFP_DIAGNOSTICS
const CKFFFrameStats &CKFixedFunctionPipeline::GetFrameStats() const {
    static const CKFFFrameStats s_EmptyStats = {};
    return s_EmptyStats;
}
#endif

CKFixedFunctionPipeline::~CKFixedFunctionPipeline() {
    Shutdown();
}

void CKFixedFunctionPipeline::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_ShaderCache.Init(ctx);
    m_DrawStateCache.Reset();
    m_VertexLayoutCache.Init(ctx);
    m_OpaquePackets.SetInstanceLayout(m_VertexLayoutCache.GetLayout(CKFF_VF_TEXCOORD0 |
                                                                    CKFF_VF_TEXCOORD1 |
                                                                    CKFF_VF_TEXCOORD2 |
                                                                    CKFF_VF_TEXCOORD3));
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_State.DirtyFlags = CKFF_DIRTY_ALL;
    m_State.MarkViewProjectionDirty();
    MarkPacketProgramDirty();
    m_OpaquePackets.ClearRenderPackets();
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
}

void CKFixedFunctionPipeline::Shutdown() {
    m_OpaquePackets.ClearRenderPackets();
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_OpaquePackets.SetInstanceLayout(0);
    m_ShaderCache.Shutdown();
    m_RenderPipeline.Shutdown();
    m_Context = nullptr;
}

void CKFixedFunctionPipeline::SetOpaqueSortingEnabled(CKBOOL enabled)
{
    m_OpaquePackets.SetSortingEnabled(enabled);
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
}

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::MarkStaticUniformsDirty()
{
    m_OpaquePackets.MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::MarkPacketProgramDirty()
{
    m_OpaquePackets.MarkPacketProgramDirty();
}

void CKFixedFunctionPipeline::OnFixedFunctionStateChanged(CKDWORD changeMask)
{
    if (changeMask & CKFF_CHANGE_PROGRAM)
        MarkPacketProgramDirty();
    if (changeMask & CKFF_CHANGE_STATIC_UNIFORM)
        MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                                            CKDWORD activeTextureCount)
{
    m_TextureBinder.BuildBindingSet(bindingSet, activeTextureCount);
}

void CKFixedFunctionPipeline::BeginDebugFrame() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_DebugState.BeginFrame();
    LogAndResetFrameStats();
#endif
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
}

// ============================================================================
// Drawing
// ============================================================================

void CKFixedFunctionPipeline::DrawPrimitive(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
    VxDrawPrimitiveData *data)
{
    if (!encoder || !data || data->VertexCount == 0) return;
    if (HasOpaqueRenderPackets())
        FlushOpaqueRenderPackets(encoder, FALSE, FALSE);
    CKFF_PROBE(m_Probes, OnSoftwareDraw());

    bool hasNormal = (data->NormalPtr != nullptr);
    bool hasUV = (data->TexCoordPtr != nullptr);
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(data->Flags, hasNormal, hasUV, data->PositionStride);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial(view) : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.Indices = indices;
        debugInfo.IndexCount = indexCount;
        debugInfo.Data = data;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.Viewport = m_State.Viewport;
        m_DebugState.LogDrawPrimitiveHeader(debugInfo);
    }
#endif

    // Prepare transient geometry
    const CKDWORD wrapMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_WRAP0);
    CKFFPointSpriteParams pointParams;
    pointParams.Size = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE, 1.0f);
    pointParams.MinSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MIN, 1.0f);
    pointParams.MaxSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MAX, 64.0f);
    pointParams.ScaleEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE);
    pointParams.ScaleA = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_A, 1.0f);
    pointParams.ScaleB = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_B, 0.0f);
    pointParams.ScaleC = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_C, 0.0f);
    pointParams.World = m_State.World;
    pointParams.View = m_State.View;
    CKBOOL prepared = FALSE;
    {
        CKFF_SCOPE_TIME(m_Probes, PrepareUs);
        prepared = m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams,
            m_State.TexcoordComponentCounts);
    }
    if (!prepared) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitivePrepareFailed();
#endif
        CKFF_PROBE(m_Probes, OnPrepareFailure());
        return;
    }
    CKFF_PROBE(m_Probes, OnTransientGeometry(m_TransientGeometry.GetLastVertexBytes(),
                                             m_TransientGeometry.GetLastIndexBytes()));

    // Build the fixed-function state description and select the matching program.
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        data->Flags, m_State.TextureHandles, m_State.StageStates);
    CKFFPreparedState preparedState;
    CKFFShaderKey shaderKey;
    {
        CKFF_SCOPE_TIME(m_Probes, StateUs);
        BuildCurrentPreparedState(&preparedState, data->Flags, activeTextureCount,
                                  formatFlags, m_State.TexcoordComponentCounts);
        shaderKey = CKFFBuildShaderKeyFromPreparedState(&preparedState);
    }
    CKFFProgramBinding programBinding;
    {
        CKFF_SCOPE_TIME(m_Probes, ProgramUs);
        programBinding = m_ShaderCache.GetProgram(shaderKey);
    }
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, shaderKey, programBinding);
    CKDWORD program = programContext.Program;
    if (program == 0) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitiveProgramMissing();
#endif
        CKFF_PROBE(m_Probes, OnProgramMiss());
        return;
    }
    CKFF_PROBE(m_Probes, OnProgram(program));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.Indices = indices;
        debugInfo.IndexCount = indexCount;
        debugInfo.Data = data;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.Viewport = m_State.Viewport;
        debugInfo.Program = program;
        debugInfo.ActiveTextureCount = (int)preparedState.ActiveTextureCount;
        debugInfo.ActiveLightCount = m_State.ActiveLightCount;
        debugInfo.StateDesc = &preparedState.StateDesc;
        debugInfo.DrawState = &m_DrawStateCache;
        debugInfo.Stage0.ColorOp = preparedState.StateDesc.FS.GetStageColorOp(0);
        debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_State.StageStates[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_State.StageStates[0], preparedState.ActiveTextureCount > 0, m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_State.StageStates[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawPrimitiveDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    BuildCurrentTextureBindingSet(&textureBindingSet, preparedState.ActiveTextureCount);

    VXPRIMITIVETYPE drawStateType =
        (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
         (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
            ? VX_TRIANGLELIST : type;
    SubmitPrepared(encoder, view, drawStateType, &programContext, &textureBindingSet,
                   0, 0, 0, 0, 0, 0, 0, CKFF_SUBMIT_PRIMITIVE);
}

void CKFixedFunctionPipeline::DrawVertexBuffer(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    m_OpaquePackets.DrawVertexBuffer(
        *this, encoder, view, type, vb, ib,
        baseVertex, vertexCount, startIndex, indexCount,
        dpFlags, formatFlags, vertexLayout);
}

// ============================================================================
// Internal methods
// ============================================================================

void CKFixedFunctionPipeline::SubmitPrepared(
    CKRasterizerEncoder *encoder,
    CKRenderView view,
    VXPRIMITIVETYPE drawStateType,
    const CKFFProgramContext *programContext,
    const CKFFTextureBindingSet *textures,
    CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD vertexLayout,
    CKFFSubmitSource source)
{
    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        m_UniformEmitter.UploadUniforms(encoder, programContext, textures->ActiveTextureCount);
    }

    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    {
        CKFF_SCOPE_TIME(m_Probes, TransformUs);
        encoder->SetTransform(transformIdx, 1);
    }
    CKFF_PROBE(m_Probes, OnTransformSet());

    CKDrawState drawState;
    {
        CKFF_SCOPE_TIME(m_Probes, DrawStateBuildUs);
        drawState = m_DrawStateCache.BuildDrawState(drawStateType);
    }
    CKFF_PROBE(m_Probes, OnDrawState(drawState));
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    {
        CKFF_SCOPE_TIME(m_Probes, EncoderStateUs);
        encoder->SetState(drawState);
    }
    {
        CKFF_SCOPE_TIME(m_Probes, StencilUs);
        encoder->SetStencilRef(stencilRef);
        encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
    }

    if (vertexLayout) {
        {
            CKFF_SCOPE_TIME(m_Probes, LayoutUs);
            encoder->SetVertexLayout(vertexLayout);
        }
        CKFF_PROBE(m_Probes, OnVertexLayoutSet());
    }

    if (vb) {
        CKFF_PROBE(m_Probes, OnVertexBuffers(vb, ib, vertexLayout));
        {
            CKFF_SCOPE_TIME(m_Probes, BufferBindUs);
            encoder->SetVertexBuffer(0, vb, baseVertex, vertexCount);
            if (ib)
                encoder->SetIndexBuffer(ib, startIndex, indexCount);
        }
        CKFF_PROBE(m_Probes, OnVertexBufferSet());
        if (ib)
            CKFF_PROBE(m_Probes, OnIndexBufferSet());
    }

    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        BindTextures(encoder, textures);
    }

    float depth = ComputeDepthKey();
    {
        CKFF_SCOPE_TIME(m_Probes, SubmitUs);
        encoder->Submit(view, programContext->Program, *(CKDWORD *)&depth, SubmitDiscardFlags());
        if (source == CKFF_SUBMIT_PRIMITIVE) {
            CK_FRAME_COST_ADD_PRIMITIVE_SUBMIT();
        } else {
            CK_FRAME_COST_ADD_MESH_SUBMIT();
        }
        CK_FRAME_COST_ADD_SUBMITTED_DRAW();
    }
    CKFF_PROBE(m_Probes, OnSubmittedDraw());
}

void CKFixedFunctionPipeline::SubmitVertexBufferImmediate(
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
    CKDWORD vertexLayout)
{
    if (!encoder || !vb) return;
    CKFF_PROBE(m_Probes, OnHardwareDraw());
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial(view) : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.VertexBuffer = vb;
        debugInfo.IndexBuffer = ib;
        debugInfo.BaseVertex = baseVertex;
        debugInfo.VertexCount = vertexCount;
        debugInfo.StartIndex = startIndex;
        debugInfo.PersistentIndexCount = indexCount;
        debugInfo.DPFlags = dpFlags;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.VertexLayout = vertexLayout;
        debugInfo.DrawSerial = debugDrawSerial;
        m_DebugState.LogDrawVertexBufferHeader(debugInfo);
    }
#endif

    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, m_State.TextureHandles, m_State.StageStates);
    CKFFPreparedState preparedState;
    CKFFShaderKey shaderKey;
    {
        CKFF_SCOPE_TIME(m_Probes, StateUs);
        BuildCurrentPreparedState(&preparedState, dpFlags, activeTextureCount, formatFlags);
        shaderKey = CKFFBuildShaderKeyFromPreparedState(&preparedState);
    }
    CKFFProgramBinding programBinding;
    {
        CKFF_SCOPE_TIME(m_Probes, ProgramUs);
        programBinding = m_ShaderCache.GetProgram(shaderKey);
    }
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, shaderKey, programBinding);
    CKDWORD program = programContext.Program;
    if (program == 0) {
        CKFF_PROBE(m_Probes, OnProgramMiss());
        return;
    }
    CKFF_PROBE(m_Probes, OnProgram(program));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.VertexBuffer = vb;
        debugInfo.IndexBuffer = ib;
        debugInfo.BaseVertex = baseVertex;
        debugInfo.VertexCount = vertexCount;
        debugInfo.StartIndex = startIndex;
        debugInfo.PersistentIndexCount = indexCount;
        debugInfo.DPFlags = dpFlags;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.VertexLayout = vertexLayout;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.Program = program;
        debugInfo.ActiveTextureCount = (int)preparedState.ActiveTextureCount;
        debugInfo.ActiveLightCount = m_State.ActiveLightCount;
        debugInfo.StateDesc = &preparedState.StateDesc;
        debugInfo.DrawState = &m_DrawStateCache;
        debugInfo.Stage0.ColorOp = preparedState.StateDesc.FS.GetStageColorOp(0);
        debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_State.StageStates[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0,
            m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_State.StageStates[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawVertexBufferDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    BuildCurrentTextureBindingSet(&textureBindingSet, preparedState.ActiveTextureCount);

    SubmitPrepared(encoder, view, type, &programContext, &textureBindingSet,
                   vb, ib, baseVertex, vertexCount, startIndex, indexCount,
                   vertexLayout, CKFF_SUBMIT_VERTEX_BUFFER);
}

void CKFixedFunctionPipeline::BuildCurrentPreparedState(
    CKFFPreparedState *prepared, CKDWORD dpFlags, CKDWORD activeTextureCount,
    CKDWORD formatFlags, const CKBYTE *texcoordComponentCounts) {
    CKFFStateResolver::BuildPreparedState(m_State, m_DrawStateCache, prepared, dpFlags,
                                          activeTextureCount, formatFlags, texcoordComponentCounts);
}

CKBOOL CKFixedFunctionPipeline::BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                                          const CKFFProgramContext *programContext,
                                                          CKDWORD activeTextureCount)
{
    return m_UniformEmitter.BuildStaticUniformPayload(payload, programContext, activeTextureCount);
}

void CKFixedFunctionPipeline::UpdateViewProjectionCache()
{
    if (!m_State.EnsureViewProjection())
        return;
    CKFF_PROBE(m_Probes, OnViewProjectionRebuild());
}

CKBOOL CKFixedFunctionPipeline::BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                                          const CKFFProgramContext *programContext)
{
    UpdateViewProjectionCache();
    return m_UniformEmitter.BuildObjectUniforms(uniforms, programContext);
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder,
                                           const CKFFTextureBindingSet *bindingSet) {
    m_TextureBinder.Bind(encoder, bindingSet);
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKFFSubmitDiscardFlags(m_State, m_DrawStateCache);
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    CKFF_PROBE(m_Probes, LogAndReset(m_DrawStateCache, m_ShaderCache.GetUniforms()));
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    return m_TextureBinder.BuildSamplerDesc(stage);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    return CKFFComputeDepthKey(m_State, m_DrawStateCache);
}

void CKFixedFunctionPipeline::FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder,
                                                       CKBOOL forceDirectReplay,
                                                       CKBOOL allowAdaptiveLearning)
{
    m_OpaquePackets.FlushRenderPackets(*this, encoder, forceDirectReplay, allowAdaptiveLearning);
}
