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

static CKDWORD CKFFShaderKeyVertexBlendMode(const CKFFShaderKeyVS &vs) {
    return (CKDWORD)((vs.Bits >> 35) & 3u);
}

static bool CKFFTextureSetEquals(
    CKDWORD aCount, const CKDWORD *a,
    CKDWORD bCount, const CKDWORD *b)
{
    if (aCount != bCount)
        return false;
    for (CKDWORD i = 0; i < aCount && i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

static CKBYTE CKFFTexcoordComponentCount(CKDWORD count) {
    if (count < 1 || count > 4)
        return 2;
    return (CKBYTE)count;
}

static CKBOOL CKFFRenderStateAffectsProgram(VXRENDERSTATETYPE state)
{
    switch (state) {
    case VXRENDERSTATE_LIGHTING:
    case VXRENDERSTATE_SPECULARENABLE:
    case VXRENDERSTATE_NORMALIZENORMALS:
    case VXRENDERSTATE_LOCALVIEWER:
    case VXRENDERSTATE_VERTEXBLEND:
    case VXRENDERSTATE_INDEXVBLENDENABLE:
    case VXRENDERSTATE_COLORVERTEX:
    case VXRENDERSTATE_DIFFUSEFROMVERTEX:
    case VXRENDERSTATE_AMBIENTFROMVERTEX:
    case VXRENDERSTATE_SPECULARFROMVERTEX:
    case VXRENDERSTATE_EMISSIVEFROMVERTEX:
    case VXRENDERSTATE_FOGENABLE:
    case VXRENDERSTATE_FOGVERTEXMODE:
    case VXRENDERSTATE_FOGPIXELMODE:
    case VXRENDERSTATE_RANGEFOGENABLE:
    case VXRENDERSTATE_SHADEMODE:
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_CLIPPLANEENABLE:
        return TRUE;
    default:
        return FALSE;
    }
}

#if CKRE_ENABLE_FFP_DIAGNOSTICS
void CKFFDrawProbes::OnTransientGeometry(CKDWORD vertexBytes, CKDWORD indexBytes)
{
    if (!StatsEnabled())
        return;
    Stats.TransientVertexBytes += vertexBytes;
    Stats.TransientIndexBytes += indexBytes;
}

void CKFFDrawProbes::OnProgram(CKDWORD program)
{
    if (!StatsEnabled())
        return;
    if (Stats.HasLastProgram && Stats.LastProgram == program)
        ++Stats.ConsecutiveProgramRepeats;
    Stats.LastProgram = program;
    Stats.HasLastProgram = TRUE;
}

void CKFFDrawProbes::OnWorldMatrix(const VxMatrix &world)
{
    if (!StatsEnabled())
        return;
    if (Stats.HasLastWorldMatrix && memcmp(&Stats.LastWorldMatrix, &world, sizeof(VxMatrix)) == 0)
        ++Stats.ConsecutiveWorldMatrixRepeats;
    memcpy(&Stats.LastWorldMatrix, &world, sizeof(VxMatrix));
    Stats.HasLastWorldMatrix = TRUE;
}

void CKFFDrawProbes::OnDrawState(const CKDrawState &drawState)
{
    if (!StatsEnabled())
        return;
    if (Stats.HasLastDrawState && CKFFDrawStateEquals(Stats.LastDrawState, drawState))
        ++Stats.ConsecutiveDrawStateRepeats;
    Stats.LastDrawState = drawState;
    Stats.HasLastDrawState = TRUE;
}

void CKFFDrawProbes::OnTextureSet(CKDWORD activeTextureCount, const CKDWORD *textures)
{
    if (!StatsEnabled() || !textures)
        return;
    if (Stats.HasLastTextureSet &&
        CKFFTextureSetEquals(Stats.LastActiveTextureCount, Stats.LastTextureHandles,
                             activeTextureCount, textures))
        ++Stats.ConsecutiveTextureSetRepeats;
    Stats.LastActiveTextureCount = activeTextureCount;
    memcpy(Stats.LastTextureHandles, textures, sizeof(Stats.LastTextureHandles));
    Stats.HasLastTextureSet = TRUE;
}

void CKFFDrawProbes::OnVertexBuffers(CKDWORD vb, CKDWORD ib, CKDWORD vertexLayout)
{
    if (!StatsEnabled())
        return;
    if (Stats.HasLastVertexBuffer &&
        Stats.LastVertexBuffer == vb &&
        Stats.LastVertexLayout == vertexLayout)
        ++Stats.ConsecutiveVertexBufferRepeats;
    Stats.LastVertexBuffer = vb;
    Stats.LastVertexLayout = vertexLayout;
    Stats.HasLastVertexBuffer = TRUE;
    if (ib && Stats.HasLastIndexBuffer && Stats.LastIndexBuffer == ib)
        ++Stats.ConsecutiveIndexBufferRepeats;
    Stats.LastIndexBuffer = ib;
    Stats.HasLastIndexBuffer = TRUE;
}

void CKFFDrawProbes::OnUniform(const CKFFUniformHandles &uniforms, CKDWORD uniform, CKDWORD count)
{
    if (StatsEnabled()) {
        ++Stats.UniformSets;
        Stats.UniformVec4s += count;
    }
    CKDWORD slot = Config.UniformHistEnabled ? CKFFUniformDebugSlot(uniforms, uniform) : 64;
    if (slot < 64) {
        ++Stats.UniformHandleSets[slot];
        Stats.UniformHandleVec4s[slot] += count;
    }
}

void CKFFDrawProbes::OnAdaptiveStats(const CKFFRenderPacketQueue &queue)
{
    if (!StatsEnabled())
        return;
    Stats.RenderPacketAdaptiveSamples = queue.GetAdaptiveSamples();
    Stats.RenderPacketAdaptiveSavedBindEstimate = queue.GetAdaptiveSavedBindEstimate();
    Stats.RenderPacketAdaptiveRunBypasses = queue.GetAdaptiveRunBypasses();
    Stats.RenderPacketAdaptiveSampleRuns = queue.GetAdaptiveSampleRuns();
    Stats.RenderPacketAdaptiveSampleMaxRun = queue.GetAdaptiveSampleMaxRun();
    Stats.RenderPacketAdaptiveSubmitSavedEstimate = queue.GetAdaptiveSubmitSavedEstimate();
    Stats.RenderPacketAdaptiveCooldownBypasses = queue.GetAdaptiveCooldownBypasses();
    Stats.RenderPacketAdaptiveCooldownFrames = queue.GetAdaptiveCooldownFrames();
    Stats.RenderPacketAdaptiveFrameEndEvaluations = queue.GetAdaptiveFrameEndEvaluations();
    Stats.RenderPacketAdaptiveFrameEndRunBypasses = queue.GetAdaptiveFrameEndRunBypasses();
}

void CKFFDrawProbes::OnAdaptiveBypass(const CKFFRenderPacketQueue &queue)
{
    if (!StatsEnabled())
        return;
    ++Stats.RenderPacketAdaptiveBypasses;
    Stats.RenderPacketAdaptiveRunBypasses = queue.GetAdaptiveRunBypasses();
    Stats.RenderPacketAdaptiveSampleRuns = queue.GetAdaptiveSampleRuns();
    Stats.RenderPacketAdaptiveSampleMaxRun = queue.GetAdaptiveSampleMaxRun();
    Stats.RenderPacketAdaptiveSubmitSavedEstimate = queue.GetAdaptiveSubmitSavedEstimate();
}

void CKFFDrawProbes::OnRenderPacketRuns(CKDWORD runCount, CKDWORD maxRun)
{
    if (!TimingEnabled())
        return;
    Stats.RenderPacketRuns += runCount;
    if (maxRun > Stats.RenderPacketMaxRunLength)
        Stats.RenderPacketMaxRunLength = maxRun;
}

void CKFFDrawProbes::FillReplayDiagnostics(CKFFRenderPacketReplayDiagnostics *diagnostics,
                                           const CKFFUniformHandles &uniforms)
{
    if (!diagnostics)
        return;
    diagnostics->StatsEnabled = Config.StatsEnabled ? TRUE : FALSE;
    diagnostics->UniformHistEnabled = Config.UniformHistEnabled ? TRUE : FALSE;
    diagnostics->Uniforms = &uniforms;
    diagnostics->UniformSets = &Stats.UniformSets;
    diagnostics->UniformVec4s = &Stats.UniformVec4s;
    diagnostics->UniformHandleSets = Stats.UniformHandleSets;
    diagnostics->UniformHandleVec4s = Stats.UniformHandleVec4s;
    diagnostics->TextureBinds = &Stats.TextureBinds;
    diagnostics->VertexLayoutSets = &Stats.VertexLayoutSets;
    diagnostics->VertexBufferSets = &Stats.VertexBufferSets;
    diagnostics->IndexBufferSets = &Stats.IndexBufferSets;
    diagnostics->TransformSets = &Stats.TransformSets;
    diagnostics->SubmittedDraws = &Stats.SubmittedDraws;
    diagnostics->ReplayedRenderPackets = &Stats.ReplayedRenderPackets;
    diagnostics->RenderPacketSkippedStates = &Stats.RenderPacketSkippedStates;
    diagnostics->RenderPacketSkippedTextures = &Stats.RenderPacketSkippedTextures;
    diagnostics->RenderPacketSkippedUniforms = &Stats.RenderPacketSkippedUniforms;
    diagnostics->RenderPacketStaticUniformUploads = &Stats.RenderPacketStaticUniformUploads;
    diagnostics->RenderPacketStaticUniformSkips = &Stats.RenderPacketStaticUniformSkips;
    diagnostics->RenderPacketObjectUniformUploads = &Stats.RenderPacketObjectUniformUploads;
    diagnostics->RenderPacketSkippedVertexBuffers = &Stats.RenderPacketSkippedVertexBuffers;
    diagnostics->RenderPacketSkippedIndexBuffers = &Stats.RenderPacketSkippedIndexBuffers;
    diagnostics->RenderPacketInstancedRuns = &Stats.RenderPacketInstancedRuns;
    diagnostics->RenderPacketInstancedPackets = &Stats.RenderPacketInstancedPackets;
    diagnostics->RenderPacketInstancedSubmits = &Stats.RenderPacketInstancedSubmits;
    diagnostics->RenderPacketInstanceBufferBytes = &Stats.RenderPacketInstanceBufferBytes;
    diagnostics->RenderPacketInstanceAllocFailures = &Stats.RenderPacketInstanceAllocFailures;
    diagnostics->RenderPacketSubmitSavedEstimate = &Stats.RenderPacketSubmitSavedEstimate;
    diagnostics->RenderPacketInstancingFallbacks = &Stats.RenderPacketInstancingFallbacks;
}

void CKFFDrawProbes::LogAndReset(CKDrawStateCache &drawStateCache, const CKFFUniformHandles &uniforms)
{
    if (!StatsEnabled())
        return;

    Stats.DrawStateCacheHits = drawStateCache.GetBuildCacheHits();
    Stats.DrawStateRebuilds = drawStateCache.GetBuildRebuilds();
    if (Config.StatsEnabled && Stats.FrameIndex > 0 &&
        (Config.StatsInterval == 1 || (Stats.FrameIndex % (CKDWORD)Config.StatsInterval) == 0)) {
        const double vec4PerDraw = Stats.SubmittedDraws > 0
            ? (double)Stats.UniformVec4s / (double)Stats.SubmittedDraws
            : 0.0;
        const double uniformsPerDraw = Stats.SubmittedDraws > 0
            ? (double)Stats.UniformSets / (double)Stats.SubmittedDraws
            : 0.0;
        const double texBindsPerDraw = Stats.SubmittedDraws > 0
            ? (double)Stats.TextureBinds / (double)Stats.SubmittedDraws
            : 0.0;
        CK_LOG_FMT("FFPStats.Core",
                   "frame=%u sw=%u hw=%u submitted=%u prepareFail=%u programMiss=%u uniforms=%u uniformsPerDraw=%.2f vec4=%u vec4PerDraw=%.2f texBinds=%u texBindsPerDraw=%.2f layouts=%u vbSets=%u ibSets=%u transforms=%u repeatProgram=%u repeatState=%u repeatTexSet=%u repeatVB=%u repeatIB=%u repeatWorld=%u drawStateHits=%u drawStateRebuilds=%u transientVB=%u transientIB=%u",
                   Stats.FrameIndex,
                   Stats.SoftwareDraws,
                   Stats.HardwareDraws,
                   Stats.SubmittedDraws,
                   Stats.PrepareFailures,
                   Stats.ProgramMisses,
                   Stats.UniformSets,
                   uniformsPerDraw,
                   Stats.UniformVec4s,
                   vec4PerDraw,
                   Stats.TextureBinds,
                   texBindsPerDraw,
                   Stats.VertexLayoutSets,
                   Stats.VertexBufferSets,
                   Stats.IndexBufferSets,
                   Stats.TransformSets,
                   Stats.ConsecutiveProgramRepeats,
                   Stats.ConsecutiveDrawStateRepeats,
                   Stats.ConsecutiveTextureSetRepeats,
                   Stats.ConsecutiveVertexBufferRepeats,
                   Stats.ConsecutiveIndexBufferRepeats,
                   Stats.ConsecutiveWorldMatrixRepeats,
                   Stats.DrawStateCacheHits,
                   Stats.DrawStateRebuilds,
                   Stats.TransientVertexBytes,
                   Stats.TransientIndexBytes);
        CK_LOG_FMT("FFPStats.Packet",
                   "frame=%u q=%u replay=%u fb=%u flush=%u overflow=%u runs=%u maxRun=%u skipState=%u skipTex=%u skipUniform=%u staticUp=%u staticSkip=%u objectUp=%u objectSkip=%u skipVB=%u skipIB=%u staticBuild=%u staticReuse=%u staticIntern=%u sortSkip=%u adaptiveSamples=%u adaptiveBypasses=%u adaptiveRunBypasses=%u adaptiveCooldownBypasses=%u adaptiveCooldownFrames=%u adaptiveFrameEndEvals=%u adaptiveFrameEndRunBypasses=%u adaptiveSaved=%u adaptiveSampleRuns=%u adaptiveSampleMaxRun=%u adaptiveSubmitSaved=%u viewProjRebuild=%u instRuns=%u instPackets=%u instSubmits=%u instBytes=%u instAllocFail=%u submitSaved=%u instFallbacks=%u",
                   Stats.FrameIndex,
                   Stats.QueuedRenderPackets,
                   Stats.ReplayedRenderPackets,
                   Stats.RenderPacketFallbacks,
                   Stats.RenderPacketFlushes,
                   Stats.RenderPacketUniformOverflows,
                   Stats.RenderPacketRuns,
                   Stats.RenderPacketMaxRunLength,
                   Stats.RenderPacketSkippedStates,
                   Stats.RenderPacketSkippedTextures,
                   Stats.RenderPacketSkippedUniforms,
                   Stats.RenderPacketStaticUniformUploads,
                   Stats.RenderPacketStaticUniformSkips,
                   Stats.RenderPacketObjectUniformUploads,
                   Stats.RenderPacketObjectUniformSkips,
                   Stats.RenderPacketSkippedVertexBuffers,
                   Stats.RenderPacketSkippedIndexBuffers,
                   Stats.RenderPacketStaticPayloadBuilds,
                   Stats.RenderPacketStaticPayloadReuses,
                   Stats.RenderPacketStaticPayloadInterns,
                   Stats.RenderPacketSortSkips,
                   Stats.RenderPacketAdaptiveSamples,
                   Stats.RenderPacketAdaptiveBypasses,
                   Stats.RenderPacketAdaptiveRunBypasses,
                   Stats.RenderPacketAdaptiveCooldownBypasses,
                   Stats.RenderPacketAdaptiveCooldownFrames,
                   Stats.RenderPacketAdaptiveFrameEndEvaluations,
                   Stats.RenderPacketAdaptiveFrameEndRunBypasses,
                   Stats.RenderPacketAdaptiveSavedBindEstimate,
                   Stats.RenderPacketAdaptiveSampleRuns,
                   Stats.RenderPacketAdaptiveSampleMaxRun,
                   Stats.RenderPacketAdaptiveSubmitSavedEstimate,
                   Stats.RenderPacketViewProjectionRebuilds,
                   Stats.RenderPacketInstancedRuns,
                   Stats.RenderPacketInstancedPackets,
                   Stats.RenderPacketInstancedSubmits,
                   Stats.RenderPacketInstanceBufferBytes,
                   Stats.RenderPacketInstanceAllocFailures,
                   Stats.RenderPacketSubmitSavedEstimate,
                   Stats.RenderPacketInstancingFallbacks);
        CK_LOG_FMT("FFPStats.Timing",
                   "frame=%u prepareUs=%.1f stateUs=%.1f programUs=%.1f uniformUs=%.1f textureUs=%.1f transformUs=%.1f drawStateBuildUs=%.1f encoderStateUs=%.1f stencilUs=%.1f layoutUs=%.1f bufferBindUs=%.1f submitUs=%.1f packetBuildUs=%.1f packetSortUs=%.1f packetReplayUs=%.1f",
                   Stats.FrameIndex,
                   Stats.PrepareUs,
                   Stats.StateUs,
                   Stats.ProgramUs,
                   Stats.UniformUs,
                   Stats.TextureUs,
                   Stats.TransformUs,
                   Stats.DrawStateBuildUs,
                   Stats.EncoderStateUs,
                   Stats.StencilUs,
                   Stats.LayoutUs,
                   Stats.BufferBindUs,
                   Stats.SubmitUs,
                   Stats.RenderPacketBuildUs,
                   Stats.RenderPacketSortUs,
                   Stats.RenderPacketReplayUs);
        if (Config.UniformHistEnabled) {
            for (CKDWORD slot = 0; slot < 64; ++slot) {
                if (Stats.UniformHandleSets[slot] == 0)
                    continue;
                CK_LOG_FMT("FFPUniformHist",
                           "frame=%u uniform=%u name=%s sets=%u vec4=%u",
                           Stats.FrameIndex,
                           slot,
                           CKFFUniformDebugName(uniforms, slot),
                           Stats.UniformHandleSets[slot],
                           Stats.UniformHandleVec4s[slot]);
            }
        }
    }

    CKDWORD nextFrame = Stats.FrameIndex + 1;
    drawStateCache.ResetBuildStats();
    memset(&Stats, 0, sizeof(Stats));
    Stats.FrameIndex = nextFrame;
}
#endif

static CKFFShaderKey CKFFBuildCurrentShaderKey(const CKFFPreparedState *prepared)
{
    if (!prepared)
        return CKFFShaderKey();
    return CKFFBuildShaderKey(prepared->StateDesc, prepared->TextureBoundMask);
}

static void CKFFInitVertexBufferPacketBuildResult(CKFFVertexBufferPacketBuildResult *result)
{
    if (!result)
        return;
    result->Success = FALSE;
    result->RejectReason = CKFF_RENDER_PACKET_ELIGIBLE;
    CKFFInitProgramContext(&result->ProgramContext, CKFFShaderKey(), CKFFProgramBinding());
    CKFFInitTextureBindingSet(&result->TextureBindingSet);
    memset(&result->Packet, 0, sizeof(result->Packet));
}

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
      m_OpaqueInstancingEnabled(TRUE), m_InstanceLayout(0),
      m_OpaqueSortingEnabled(FALSE), m_OpaquePacketAllowed(TRUE) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_Probes.Config.StatsEnabled = settings.Enabled;
    m_Probes.Config.UniformHistEnabled = settings.UniformHistogram;
    m_Probes.Config.StatsInterval = settings.Interval;
#endif
    m_OpaqueSortingEnabled = CKRenderFFPSettings().GetBool("SortOpaqueObjects", false) ? TRUE : FALSE;
    m_OpaqueInstancingEnabled = CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true) ? TRUE : FALSE;

    m_State.Reset();
    m_PacketProgramCacheValid = FALSE;
    m_PacketProgramCacheDPFlags = 0;
    m_PacketProgramCacheFormatFlags = 0;
    m_PacketProgramCacheActiveTextureCount = 0;
    CKFFInitPreparedState(&m_PacketProgramCachePreparedState);
    memset(&m_PacketProgramCacheContext, 0, sizeof(m_PacketProgramCacheContext));
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
    m_InstanceLayout = m_VertexLayoutCache.GetLayout(CKFF_VF_TEXCOORD0 |
                                                     CKFF_VF_TEXCOORD1 |
                                                     CKFF_VF_TEXCOORD2 |
                                                     CKFF_VF_TEXCOORD3);
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_State.DirtyFlags = CKFF_DIRTY_ALL;
    m_State.MarkViewProjectionDirty();
    MarkPacketProgramDirty();
    ClearOpaqueRenderPackets();
    ResetOpaqueRenderPacketFrameState();
}

void CKFixedFunctionPipeline::Shutdown() {
    ClearOpaqueRenderPackets();
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_InstanceLayout = 0;
    m_ShaderCache.Shutdown();
    m_RenderPipeline.Shutdown();
    m_Context = nullptr;
}

void CKFixedFunctionPipeline::SetRenderOptions(CKBOOL DisableTextureFiltering, CKBOOL DisableMipmaps,
                                               CKBOOL ForceAnisotropicFiltering) {
    if (m_DisableTextureFiltering == DisableTextureFiltering &&
        m_DisableMipmaps == DisableMipmaps &&
        m_ForceAnisotropicFiltering == ForceAnisotropicFiltering)
        return;
    m_DisableTextureFiltering = DisableTextureFiltering;
    m_DisableMipmaps = DisableMipmaps;
    m_ForceAnisotropicFiltering = ForceAnisotropicFiltering;
    m_TextureBinder.SetRenderOptions(DisableTextureFiltering, DisableMipmaps, ForceAnisotropicFiltering);
}

void CKFixedFunctionPipeline::SetAlphaTestPrecision(CKDWORD precision) {
    precision &= 0xFu;
    if (m_State.AlphaTestPrecision == precision)
        return;
    m_State.AlphaTestPrecision = precision;
    m_State.DirtyFlags |= CKFF_DIRTY_ALPHATEST;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

CKDWORD CKFixedFunctionPipeline::GetAlphaTestPrecision() const {
    return m_State.AlphaTestPrecision;
}

void CKFixedFunctionPipeline::SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix) {
    if (index >= CKFF_VERTEX_BLEND_MATRIX_COUNT)
        return;
    m_State.VertexBlendMatrices[index] = matrix;
    m_State.VertexBlendMatrixSet[index] = TRUE;
    m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::ResetVertexBlendMatrices() {
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(m_State.VertexBlendMatrices[i]);
        m_State.VertexBlendMatrixSet[i] = FALSE;
    }
    m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTexcoordComponentCount(CKDWORD stage, CKDWORD count) {
    if (stage >= CKFF_MAX_TEXTURE_STAGES)
        return;
    CKBYTE componentCount = CKFFTexcoordComponentCount(count);
    if (m_State.TexcoordComponentCounts[stage] == componentCount)
        return;
    m_State.TexcoordComponentCounts[stage] = componentCount;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM);
}

void CKFixedFunctionPipeline::ResetTexcoordComponentCounts() {
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_State.TexcoordComponentCounts[stage] = 2;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM);
}

CKFFStateGuard::CKFFStateGuard(CKFixedFunctionPipeline &pipeline)
    : m_Pipeline(&pipeline),
      m_ColorWriteMask(pipeline.GetColorWriteMask()),
      m_World(pipeline.GetWorldMatrix()),
      m_View(pipeline.GetViewMatrix()),
      m_Projection(pipeline.GetProjectionMatrix()) {
    for (int i = 0; i < CKFF_RS_COUNT; ++i)
        m_RenderStates[i] = pipeline.GetRenderState((VXRENDERSTATETYPE)i);
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        pipeline.SaveTextureStage(stage, m_TextureStages[stage]);
}

CKFFStateGuard::~CKFFStateGuard() {
    Restore();
}

void CKFFStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_Pipeline->RestoreTextureStage(stage, m_TextureStages[stage]);
    m_Pipeline->SetTransform(VXMATRIX_WORLD, m_World);
    m_Pipeline->SetTransform(VXMATRIX_VIEW, m_View);
    m_Pipeline->SetTransform(VXMATRIX_PROJECTION, m_Projection);
    m_Pipeline->SetColorWriteMask(m_ColorWriteMask);
    for (int i = 0; i < CKFF_RS_COUNT; ++i)
        m_Pipeline->SetRenderState((VXRENDERSTATETYPE)i, m_RenderStates[i]);
    m_Pipeline = nullptr;
}

void CKFFStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}

CKFFRenderStateGuard::CKFFRenderStateGuard(CKFixedFunctionPipeline &pipeline, VXRENDERSTATETYPE state, CKBOOL active)
    : m_Pipeline(active ? &pipeline : nullptr),
      m_State(state),
      m_Value(active ? pipeline.GetRenderState(state) : 0) {
}

CKFFRenderStateGuard::~CKFFRenderStateGuard() {
    Restore();
}

void CKFFRenderStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    m_Pipeline->SetRenderState(m_State, m_Value);
    m_Pipeline = nullptr;
}

void CKFFRenderStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}

CKFFOpaquePacketGuard::CKFFOpaquePacketGuard(CKFixedFunctionPipeline &pipeline, CKBOOL active)
    : m_Pipeline(active ? &pipeline : nullptr),
      m_SavedAllowed(active ? pipeline.GetOpaqueRenderPacketsAllowed() : TRUE) {
    if (m_Pipeline) {
        m_Pipeline->FlushOpaqueRenderPackets(nullptr, FALSE, FALSE);
        m_Pipeline->SetOpaqueRenderPacketsAllowed(FALSE);
    }
}

CKFFOpaquePacketGuard::~CKFFOpaquePacketGuard() {
    Restore();
}

void CKFFOpaquePacketGuard::Restore() {
    if (!m_Pipeline)
        return;
    m_Pipeline->SetOpaqueRenderPacketsAllowed(m_SavedAllowed);
    m_Pipeline = nullptr;
}

void CKFFOpaquePacketGuard::Dismiss() {
    m_Pipeline = nullptr;
}

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::MarkStaticUniformsDirty()
{
    m_OpaquePacketQueue.MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::MarkPacketProgramDirty()
{
    m_PacketProgramCacheValid = FALSE;
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

void CKFixedFunctionPipeline::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) {
    if (m_DrawStateCache.GetRenderState(state) == value)
        return;
    m_DrawStateCache.SetRenderState(state, value);

    switch (state) {
    case VXRENDERSTATE_FOGENABLE:
    case VXRENDERSTATE_FOGVERTEXMODE:
    case VXRENDERSTATE_FOGPIXELMODE:
    case VXRENDERSTATE_FOGSTART:
    case VXRENDERSTATE_FOGEND:
    case VXRENDERSTATE_FOGDENSITY:
    case VXRENDERSTATE_FOGCOLOR:
        m_State.DirtyFlags |= CKFF_DIRTY_FOG;
        break;
    case VXRENDERSTATE_AMBIENT:
        m_State.DirtyFlags |= CKFF_DIRTY_LIGHTS;
        break;
    case VXRENDERSTATE_TEXTUREFACTOR:
        m_State.DirtyFlags |= CKFF_DIRTY_TEXFACTOR;
        break;
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_ALPHAREF:
        m_State.DirtyFlags |= CKFF_DIRTY_ALPHATEST;
        break;
    default:
        break;
    }
    CKDWORD changeMask = CKFF_CHANGE_STATIC_UNIFORM;
    if (CKFFRenderStateAffectsProgram(state))
        changeMask |= CKFF_CHANGE_PROGRAM;
    OnFixedFunctionStateChanged(changeMask);
}

CKDWORD CKFixedFunctionPipeline::GetRenderState(VXRENDERSTATETYPE state) const {
    return m_DrawStateCache.GetRenderState(state);
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKBOOL r, CKBOOL g, CKBOOL b, CKBOOL a) {
    m_DrawStateCache.SetColorWriteMask(r, g, b, a);
}

CKDWORD CKFixedFunctionPipeline::GetColorWriteMask() const {
    return m_DrawStateCache.GetColorWriteMask();
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKDWORD mask) {
    m_DrawStateCache.SetColorWriteMask(mask);
}

static void ClearExplicitTextureCombineState(CKDWORD *stageState) {
    if (!stageState)
        return;

    stageState[CKRST_TSS_OP] = 0;
    stageState[CKRST_TSS_ARG1] = 0;
    stageState[CKRST_TSS_ARG2] = 0;
    stageState[CKRST_TSS_AOP] = 0;
    stageState[CKRST_TSS_AARG1] = 0;
    stageState[CKRST_TSS_AARG2] = 0;
    stageState[CKRST_TSS_COLORARG0] = 0;
    stageState[CKRST_TSS_ALPHAARG0] = 0;
    stageState[CKRST_TSS_RESULTARG0] = 0;
}

void CKFixedFunctionPipeline::ResetTextureStage(int stage) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_State.TextureHandles[stage] = 0;
    m_State.TextureFlags[stage] = 0;
    memset(m_State.StageStates[stage], 0, sizeof(m_State.StageStates[stage]));
    m_State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    m_State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_NONE;
    Vx3DMatrixIdentity(m_State.TexMatrix[stage]);
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::DisableTextureStagesFrom(int firstStage) {
    if (firstStage < 0)
        firstStage = 0;
    for (int stage = firstStage; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        ResetTextureStage(stage);
}

void CKFixedFunctionPipeline::SaveTextureStage(int stage, CKFFTextureStageSnapshot &snapshot) const {
    memset(&snapshot, 0, sizeof(snapshot));
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    snapshot.Texture = m_State.TextureHandles[stage];
    snapshot.TextureFlags = m_State.TextureFlags[stage];
    memcpy(snapshot.States, m_State.StageStates[stage], sizeof(snapshot.States));
    snapshot.TextureMatrix = m_State.TexMatrix[stage];
}

void CKFixedFunctionPipeline::RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_State.TextureHandles[stage] = snapshot.Texture;
    m_State.TextureFlags[stage] = snapshot.TextureFlags;
    memcpy(m_State.StageStates[stage], snapshot.States, sizeof(m_State.StageStates[stage]));
    m_State.TexMatrix[stage] = snapshot.TextureMatrix;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;

    if (type == CKRST_TSS_STAGEBLEND && value == 0 && stage > 0) {
        DisableTextureStagesFrom(stage);
        return;
    }

    if (m_State.StageStates[stage][(int)type] == value)
        return;

    m_State.StageStates[stage][(int)type] = value;

    if (type == CKRST_TSS_TEXTUREMAPBLEND) {
        ClearExplicitTextureCombineState(m_State.StageStates[stage]);
    } else if (type == CKRST_TSS_STAGEBLEND) {
        CKDWORD colorOp = 0;
        CKDWORD colorArg1 = 0;
        CKDWORD colorArg2 = 0;
        CKDWORD alphaOp = 0;
        CKDWORD alphaArg1 = 0;
        CKDWORD alphaArg2 = 0;
        if (CKFFStageBlendToTextureOps(value,
                                       colorOp, colorArg1, colorArg2,
                                       alphaOp, alphaArg1, alphaArg2)) {
            m_State.StageStates[stage][CKRST_TSS_OP] = colorOp;
            m_State.StageStates[stage][CKRST_TSS_ARG1] = colorArg1;
            m_State.StageStates[stage][CKRST_TSS_ARG2] = colorArg2;
            m_State.StageStates[stage][CKRST_TSS_AOP] = alphaOp;
            m_State.StageStates[stage][CKRST_TSS_AARG1] = alphaArg1;
            m_State.StageStates[stage][CKRST_TSS_AARG2] = alphaArg2;
        }
    }
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

CKDWORD CKFixedFunctionPipeline::GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return 0;
    return m_State.StageStates[stage][(int)type];
}

void CKFixedFunctionPipeline::SetViewport(const CKViewportData &viewport) {
    CK_FRAME_COST_ADD_VIEWPORT_SET();

    const float w = viewport.ViewWidth > 0 ? (float)viewport.ViewWidth : 1.0f;
    const float h = viewport.ViewHeight > 0 ? (float)viewport.ViewHeight : 1.0f;
    const float x = (float)viewport.ViewX;
    const float y = (float)viewport.ViewY;

    m_State.Viewport[0] = 2.0f / w;
    m_State.Viewport[1] = -2.0f / h;
    m_State.Viewport[2] = -1.0f - (2.0f * x / w);
    m_State.Viewport[3] = 1.0f + (2.0f * y / h);
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetUserClipPlane(int index, const VxPlane &plane) {
    if (index < 0 || index >= 6)
        return;
    m_State.UserClipPlanes[index] = plane;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix) {
    switch (type) {
    case VXMATRIX_WORLD:
        m_State.World = matrix;
        m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    case VXMATRIX_VIEW:
        m_State.View = matrix;
        m_State.DirtyFlags |= CKFF_DIRTY_MATRICES | CKFF_DIRTY_LIGHTS;
        m_State.MarkViewProjectionDirty();
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    case VXMATRIX_PROJECTION:
        m_State.Projection = matrix;
        m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
        m_State.MarkViewProjectionDirty();
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    default:
        if (type >= VXMATRIX_TEXTURE0 && type <= VXMATRIX_TEXTURE7) {
            int idx = type - VXMATRIX_TEXTURE0;
            if (idx < CKFF_MAX_TEXTURE_STAGES) {
                m_State.TexMatrix[idx] = matrix;
                OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
            }
        }
        break;
    }
}

void CKFixedFunctionPipeline::ResetMaterial() {
    memset(&m_State.Material, 0, sizeof(m_State.Material));
    m_State.Material.Diffuse[0] = 1.0f;
    m_State.Material.Diffuse[1] = 1.0f;
    m_State.Material.Diffuse[2] = 1.0f;
    m_State.Material.Diffuse[3] = 1.0f;
    m_State.Material.Ambient[0] = 1.0f;
    m_State.Material.Ambient[1] = 1.0f;
    m_State.Material.Ambient[2] = 1.0f;
    m_State.Material.Ambient[3] = 1.0f;
    m_State.DirtyFlags |= CKFF_DIRTY_MATERIAL;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetMaterial(const CKMaterialData *mat) {
    if (!mat) return;
    m_State.Material.Diffuse[0] = mat->Diffuse.r;
    m_State.Material.Diffuse[1] = mat->Diffuse.g;
    m_State.Material.Diffuse[2] = mat->Diffuse.b;
    m_State.Material.Diffuse[3] = mat->Diffuse.a;
    m_State.Material.Ambient[0] = mat->Ambient.r;
    m_State.Material.Ambient[1] = mat->Ambient.g;
    m_State.Material.Ambient[2] = mat->Ambient.b;
    m_State.Material.Ambient[3] = mat->Ambient.a;
    m_State.Material.Specular[0] = mat->Specular.r;
    m_State.Material.Specular[1] = mat->Specular.g;
    m_State.Material.Specular[2] = mat->Specular.b;
    m_State.Material.Specular[3] = mat->Specular.a;
    m_State.Material.Emissive[0] = mat->Emissive.r;
    m_State.Material.Emissive[1] = mat->Emissive.g;
    m_State.Material.Emissive[2] = mat->Emissive.b;
    m_State.Material.Emissive[3] = mat->Emissive.a;
    m_State.Material.Power = mat->SpecularPower;
    m_State.DirtyFlags |= CKFF_DIRTY_MATERIAL;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetLight(int index, const CKLightData *light) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS || !light) return;

    CKFFLightData &dst = m_State.Lights[index];

    // Store in world space; will be transformed to view space at upload time
    dst.Position[0] = light->Position.x;
    dst.Position[1] = light->Position.y;
    dst.Position[2] = light->Position.z;
    dst.Position[3] = CKFFEncodeShaderLightType(light->Type);

    dst.Direction[0] = light->Direction.x;
    dst.Direction[1] = light->Direction.y;
    dst.Direction[2] = light->Direction.z;
    dst.Direction[3] = light->Range;

    dst.Diffuse[0] = light->Diffuse.r;
    dst.Diffuse[1] = light->Diffuse.g;
    dst.Diffuse[2] = light->Diffuse.b;
    dst.Diffuse[3] = light->Diffuse.a;

    dst.Specular[0] = light->Specular.r;
    dst.Specular[1] = light->Specular.g;
    dst.Specular[2] = light->Specular.b;
    dst.Specular[3] = light->Specular.a;

    dst.Ambient[0] = light->Ambient.r;
    dst.Ambient[1] = light->Ambient.g;
    dst.Ambient[2] = light->Ambient.b;
    dst.Ambient[3] = light->Ambient.a;

    dst.Attenuation[0] = light->Attenuation0;
    dst.Attenuation[1] = light->Attenuation1;
    dst.Attenuation[2] = light->Attenuation2;
    dst.Attenuation[3] = light->Falloff;

    dst.SpotParams[0] = cosf(light->InnerSpotCone * 0.5f);
    dst.SpotParams[1] = cosf(light->OuterSpotCone * 0.5f);
    dst.SpotParams[2] = 0.0f;
    dst.SpotParams[3] = 0.0f;

    m_State.DirtyFlags |= CKFF_DIRTY_LIGHTS;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::EnableLight(int index, CKBOOL enable) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS) return;
    if (m_State.LightEnabled[index] == enable)
        return;
    m_State.LightEnabled[index] = enable;

    m_State.ActiveLightCount = 0;
    for (int i = 0; i < CKFF_MAX_LIGHTS; i++) {
        if (m_State.LightEnabled[i]) m_State.ActiveLightCount++;
    }
    m_State.DirtyFlags |= CKFF_DIRTY_LIGHTS;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle) {
    SetTexture(stage, textureHandle, textureHandle != 0 ? CKRST_TEXTURE_VALID : 0);
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle, CKDWORD textureFlags) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    const CKDWORD normalizedFlags = textureHandle != 0 ? textureFlags : 0;
    if (m_State.TextureHandles[stage] == textureHandle && m_State.TextureFlags[stage] == normalizedFlags)
        return;
    const CKBOOL oldHasTexture = m_State.TextureHandles[stage] != 0 ? TRUE : FALSE;
    const CKBOOL newHasTexture = textureHandle != 0 ? TRUE : FALSE;
    const CKDWORD oldStaticFlags = CKFFStaticTextureFlags(m_State.TextureFlags[stage]);
    const CKDWORD newStaticFlags = CKFFStaticTextureFlags(normalizedFlags);
    m_State.TextureHandles[stage] = textureHandle;
    m_State.TextureFlags[stage] = normalizedFlags;
    if (oldHasTexture != newHasTexture || oldStaticFlags != newStaticFlags) {
        OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
    }
}

CKDWORD CKFixedFunctionPipeline::GetTexture(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    return m_State.TextureHandles[stage];
}

void CKFixedFunctionPipeline::BeginDebugFrame() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_DebugState.BeginFrame();
    LogAndResetFrameStats();
#endif
    ResetOpaqueRenderPacketFrameState();
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
        shaderKey = CKFFBuildCurrentShaderKey(&preparedState);
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

    // Upload uniforms
    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        m_UniformEmitter.UploadUniforms(encoder, &programContext, preparedState.ActiveTextureCount);
    }

    // Set world transform
    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    {
        CKFF_SCOPE_TIME(m_Probes, TransformUs);
        encoder->SetTransform(transformIdx, 1);
    }
    CKFF_PROBE(m_Probes, OnTransformSet());

    // Set draw state
    CKDrawState drawState;
    {
        CKFF_SCOPE_TIME(m_Probes, DrawStateBuildUs);
        drawState = m_DrawStateCache.BuildDrawState(
            (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
             (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
                ? VX_TRIANGLELIST
                : type);
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

    // Bind textures
    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        BindTextures(encoder, &textureBindingSet);
    }

    // Submit
    float depth = ComputeDepthKey();
    {
        CKFF_SCOPE_TIME(m_Probes, SubmitUs);
        encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
        CK_FRAME_COST_ADD_PRIMITIVE_SUBMIT();
        CK_FRAME_COST_ADD_SUBMITTED_DRAW();
    }
    CKFF_PROBE(m_Probes, OnSubmittedDraw());
}

void CKFixedFunctionPipeline::DrawVertexBuffer(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    const CKDWORD packetRejectReason =
        GetOpaqueVertexBufferPacketRejectReason(view, type, vb, ib, vertexLayout);
    if (packetRejectReason == CKFF_RENDER_PACKET_ELIGIBLE) {
        CKFFVertexBufferPacketBuildResult buildResult;
        BuildVertexBufferPacket(&buildResult, encoder, view, type, vb, ib,
                                baseVertex, vertexCount,
                                startIndex, indexCount,
                                dpFlags, formatFlags,
                                vertexLayout);
        if (buildResult.Success) {
            TrackOpaqueRenderPacket(buildResult.Packet);
            CKFF_PROBE(m_Probes, OnQueuedRenderPacket());
            CheckOpaqueRenderPacketAdaptiveBypass(encoder);
            return;
        }
        TrackOpaqueRenderPacketReject(buildResult.RejectReason);
        CKFF_PROBE(m_Probes, OnRenderPacketFallback());
    } else {
        TrackOpaqueRenderPacketReject(packetRejectReason);
    }

    if (HasOpaqueRenderPackets())
        FlushOpaqueRenderPackets(encoder, FALSE, FALSE);
    SubmitVertexBufferPacketImmediate(encoder, view, type, vb, ib,
                                      baseVertex, vertexCount,
                                      startIndex, indexCount,
                                      dpFlags, formatFlags,
                                      vertexLayout);
}

void CKFixedFunctionPipeline::SubmitVertexBufferPacketImmediate(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!encoder || !vb) return;
    CKFF_PROBE(m_Probes, OnHardwareDraw());
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    // Build the fixed-function state description from the actual mesh vertex format.
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
        shaderKey = CKFFBuildCurrentShaderKey(&preparedState);
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
        debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_State.StageStates[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_State.StageStates[0], preparedState.ActiveTextureCount > 0, m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_State.StageStates[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawVertexBufferDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    BuildCurrentTextureBindingSet(&textureBindingSet, preparedState.ActiveTextureCount);

    // Upload uniforms
    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        m_UniformEmitter.UploadUniforms(encoder, &programContext, preparedState.ActiveTextureCount);
    }

    // Set world transform
    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    {
        CKFF_SCOPE_TIME(m_Probes, TransformUs);
        encoder->SetTransform(transformIdx, 1);
    }
    CKFF_PROBE(m_Probes, OnTransformSet());

    // Set draw state
    CKDrawState drawState;
    {
        CKFF_SCOPE_TIME(m_Probes, DrawStateBuildUs);
        drawState = m_DrawStateCache.BuildDrawState(type);
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

    // Set vertex layout
    if (vertexLayout) {
        {
            CKFF_SCOPE_TIME(m_Probes, LayoutUs);
            encoder->SetVertexLayout(vertexLayout);
        }
        CKFF_PROBE(m_Probes, OnVertexLayoutSet());
    }

    // Bind buffers
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

    // Bind textures
    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        BindTextures(encoder, &textureBindingSet);
    }

    // Submit
    float depth = ComputeDepthKey();
    {
        CKFF_SCOPE_TIME(m_Probes, SubmitUs);
        encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
        CK_FRAME_COST_ADD_MESH_SUBMIT();
        CK_FRAME_COST_ADD_SUBMITTED_DRAW();
    }
    CKFF_PROBE(m_Probes, OnSubmittedDraw());
}

// ============================================================================
// Internal methods
// ============================================================================

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

CKDWORD CKFixedFunctionPipeline::GetPacketObjectUniformRejectReason(const CKFFProgramContext *programContext) const
{
    if (!programContext)
        return CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_ELIGIBLE;
    if (CKFFShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

CKDWORD CKFixedFunctionPipeline::GetVertexBufferPacketInstancingRejectReason(
    const CKFFProgramContext *programContext) const
{
    if (!programContext)
        return CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;
    if (!m_OpaqueInstancingEnabled || !m_InstanceLayout)
        return CKFF_RENDER_PACKET_REJECT_INSTANCE_LAYOUT;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_REJECT_POSITIONT;
    if (CKFFShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
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

CKDWORD CKFixedFunctionPipeline::InternStaticUniformPayload(const CKFFRenderPacketUniformPayload &payload)
{
    CKBOOL interned = FALSE;
    CKDWORD index = m_OpaquePacketQueue.InternStaticUniformPayload(payload, &interned);
    if (interned)
        CKFF_PROBE(m_Probes, OnRenderPacketStaticPayloadIntern());
    return index;
}

void CKFixedFunctionPipeline::BuildRenderPacketSortKey(CKRenderPacket *packet) const
{
    m_OpaquePacketQueue.BuildSortKey(packet);
}

void CKFixedFunctionPipeline::InitVertexBufferPacketForCapture(CKRenderPacket *packet) const
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

void CKFixedFunctionPipeline::TrackOpaqueRenderPacket(const CKRenderPacket &packet)
{
    m_OpaquePacketQueue.AddPacket(packet);
    UpdateOpaqueRenderPacketAdaptiveStats();
}

void CKFixedFunctionPipeline::TrackOpaqueRenderPacketReject(CKDWORD rejectReason)
{
    if (rejectReason != CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS)
        return;
    if (!m_OpaquePacketQueue.IsAdaptiveCooldownActive())
        return;
    m_OpaquePacketQueue.MarkAdaptiveCooldownBypass();
    UpdateOpaqueRenderPacketAdaptiveStats();
}

void CKFixedFunctionPipeline::UpdateOpaqueRenderPacketAdaptiveStats()
{
    CKFF_PROBE(m_Probes, OnAdaptiveStats(m_OpaquePacketQueue));
}

CKBOOL CKFixedFunctionPipeline::CheckOpaqueRenderPacketAdaptiveBypass(CKRasterizerEncoder *encoder)
{
    if (m_OpaquePacketQueue.IsAdaptiveBypassed())
        return TRUE;
    if (!m_OpaquePacketQueue.ShouldAdaptiveBypass(m_OpaqueInstancingEnabled))
        return FALSE;

    m_OpaquePacketQueue.MarkAdaptiveBypass();
    CKFF_PROBE(m_Probes, OnAdaptiveBypass(m_OpaquePacketQueue));
    UpdateOpaqueRenderPacketAdaptiveStats();
    FlushOpaqueRenderPackets(encoder, TRUE, FALSE);
    return TRUE;
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder,
                                           const CKFFTextureBindingSet *bindingSet) {
    m_TextureBinder.Bind(encoder, bindingSet);
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKRST_DISCARD_ALL;
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    CKFF_PROBE(m_Probes, LogAndReset(m_DrawStateCache, m_ShaderCache.GetUniforms()));
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    return m_TextureBinder.BuildSamplerDesc(stage);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    // Depth key = distance from camera (view-space Z of the world origin)
    float z = m_State.World[3][0] * m_State.View[0][2] +
              m_State.World[3][1] * m_State.View[1][2] +
              m_State.World[3][2] * m_State.View[2][2] +
              m_State.View[3][2];
    return z;
}

CKBOOL CKFixedFunctionPipeline::ResolveVertexBufferPacketProgram(CKDWORD dpFlags,
                                                                 CKDWORD formatFlags,
                                                                 CKFFPreparedState *preparedState,
                                                                 CKFFProgramContext *programContext)
{
    if (!preparedState || !programContext)
        return FALSE;

    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, m_State.TextureHandles, m_State.StageStates);
    if (m_PacketProgramCacheValid &&
        m_PacketProgramCacheDPFlags == dpFlags &&
        m_PacketProgramCacheFormatFlags == formatFlags &&
        m_PacketProgramCacheActiveTextureCount == (int)activeTextureCount) {
        *preparedState = m_PacketProgramCachePreparedState;
        *programContext = m_PacketProgramCacheContext;
        return programContext->Program != 0 ? TRUE : FALSE;
    }

    BuildCurrentPreparedState(preparedState, dpFlags, activeTextureCount, formatFlags);
    CKFFShaderKey shaderKey = CKFFBuildCurrentShaderKey(preparedState);
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    CKFFInitProgramContext(programContext, shaderKey, programBinding);
    m_PacketProgramCacheDPFlags = dpFlags;
    m_PacketProgramCacheFormatFlags = formatFlags;
    m_PacketProgramCacheActiveTextureCount = (int)activeTextureCount;
    m_PacketProgramCachePreparedState = *preparedState;
    m_PacketProgramCacheContext = *programContext;
    m_PacketProgramCacheValid = TRUE;
    return programContext->Program != 0 ? TRUE : FALSE;
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketIdentity(
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

    packet->Serial = m_OpaquePacketQueue.NextSerial();
    packet->View = view;
    packet->Type = type;
    packet->Program = programContext->Program;
    float depth = ComputeDepthKey();
    packet->Depth = *(CKDWORD *)&depth;
    packet->DrawState = m_DrawStateCache.BuildDrawState(type);
    packet->StencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    packet->StencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    packet->StencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
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
    packet->World = m_State.World;
    packet->Marker[0] = '\0';
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketTextures(CKRenderPacket *packet,
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

CKBOOL CKFixedFunctionPipeline::CaptureVertexBufferPacketObjectUniforms(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext)
{
    if (!packet)
        return FALSE;

    if (packet->CanInstance) {
        UpdateViewProjectionCache();
        memset(&packet->ObjectUniforms, 0, sizeof(packet->ObjectUniforms));
        packet->ObjectUniforms.MatrixUniform = m_ShaderCache.GetUniforms().u_ffMatrices;
    } else {
        if (!BuildPacketObjectUniforms(&packet->ObjectUniforms, programContext))
            return FALSE;
    }
    packet->ViewProjection = m_State.ViewProjection();
    packet->ViewProjectionHash = m_State.ViewProjectionHash();
    return TRUE;
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketInstancing(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext)
{
    if (!packet || !programContext)
        return;
    if (GetVertexBufferPacketInstancingRejectReason(programContext) != CKFF_RENDER_PACKET_ELIGIBLE)
        return;

    CKFFShaderKey instancedKey = programContext->ShaderKey;
    instancedKey.VS.SetInstanced(true);
    CKFFProgramBinding instancedBinding = m_ShaderCache.GetProgram(instancedKey);
    CKFFProgramContext instancedContext;
    CKFFInitProgramContext(&instancedContext, instancedKey, instancedBinding);
    if (CKFFCanUseInstancedProgramForPacket(*programContext, instancedContext)) {
        packet->CanInstance = TRUE;
        packet->InstancedProgram = instancedContext.Program;
    }
}

CKBOOL CKFixedFunctionPipeline::CaptureVertexBufferPacketStaticUniforms(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext,
    CKBOOL collectStats)
{
    if (!packet || !programContext)
        return FALSE;

    if (m_OpaquePacketQueue.TryUseCachedStaticUniform(&packet->StaticUniformIndex)) {
        if (collectStats)
            CKFF_PROBE(m_Probes, OnRenderPacketStaticPayloadReuse());
    } else {
        CKFFRenderPacketUniformPayload staticPayload;
        CKBOOL payloadBuilt = FALSE;
        {
            CKFF_SCOPE_TIME(m_Probes, RenderPacketBuildUs);
            payloadBuilt = BuildStaticUniformPayload(&staticPayload, programContext, packet->ActiveTextureCount);
        }
        if (!payloadBuilt) {
            if (collectStats)
                CKFF_PROBE(m_Probes, OnRenderPacketUniformOverflow());
            return FALSE;
        }
        if (collectStats)
            CKFF_PROBE(m_Probes, OnRenderPacketStaticPayloadBuild());
        packet->StaticUniformIndex = InternStaticUniformPayload(staticPayload);
        m_OpaquePacketQueue.CacheStaticUniform(packet->StaticUniformIndex);
    }

    return TRUE;
}

CKDWORD CKFixedFunctionPipeline::GetOpaqueVertexBufferPacketRejectReason(CKRenderView view,
                                                                         VXPRIMITIVETYPE type,
                                                                         CKDWORD vb,
                                                                         CKDWORD ib,
                                                                         CKDWORD vertexLayout) const
{
    if (!m_OpaqueSortingEnabled)
        return CKFF_RENDER_PACKET_REJECT_SORT_DISABLED;
    if (!m_OpaquePacketAllowed)
        return CKFF_RENDER_PACKET_REJECT_PACKETS_DISALLOWED;
    if (!m_Context)
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
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE))
        return CKFF_RENDER_PACKET_REJECT_ALPHA_BLEND;
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZENABLE))
        return CKFF_RENDER_PACKET_REJECT_Z_DISABLED;
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZWRITEENABLE))
        return CKFF_RENDER_PACKET_REJECT_Z_WRITE_DISABLED;
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND) != VXVBLEND_DISABLE)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE))
        return CKFF_RENDER_PACKET_REJECT_INDEXED_VERTEX_BLEND;
    if (m_OpaquePacketQueue.IsAdaptiveBypassed())
        return CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFixedFunctionPipeline::BuildVertexBufferPacket(
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
    CKFFInitVertexBufferPacketBuildResult(result);
    if (!vb) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_BUFFER;
        return;
    }

    CKBOOL collectStats = FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    collectStats = m_Probes.StatsEnabled() ? TRUE : FALSE;
#endif
    CKFF_PROBE(m_Probes, OnHardwareDraw());

    CKFFPreparedState preparedState;
    CKFFProgramContext programContext;
    if (!ResolveVertexBufferPacketProgram(dpFlags, formatFlags, &preparedState, &programContext)) {
        if (collectStats)
            CKFF_PROBE(m_Probes, OnProgramMiss());
        result->ProgramContext = programContext;
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;
        return;
    }
    result->ProgramContext = programContext;
    BuildCurrentTextureBindingSet(&result->TextureBindingSet, preparedState.ActiveTextureCount);
    result->RejectReason = GetPacketObjectUniformRejectReason(&programContext);
    if (result->RejectReason != CKFF_RENDER_PACKET_ELIGIBLE) {
        return;
    }

    InitVertexBufferPacketForCapture(&result->Packet);
    CaptureVertexBufferPacketIdentity(&result->Packet, &programContext, view, type, vb, ib,
                                      baseVertex, vertexCount, startIndex, indexCount, vertexLayout);
    CaptureVertexBufferPacketTextures(&result->Packet, &result->TextureBindingSet);
    CaptureVertexBufferPacketInstancing(&result->Packet, &programContext);

    CKBOOL objectUniformsBuilt = FALSE;
    {
        CKFF_SCOPE_TIME(m_Probes, RenderPacketBuildUs);
        objectUniformsBuilt = CaptureVertexBufferPacketObjectUniforms(&result->Packet, &programContext);
    }
    if (!objectUniformsBuilt) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_OBJECT_UNIFORMS;
        return;
    }

    if (!CaptureVertexBufferPacketStaticUniforms(&result->Packet, &programContext, collectStats)) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_STATIC_UNIFORMS;
        return;
    }

    if (encoder)
        encoder->ConsumeMarker(result->Packet.Marker, sizeof(result->Packet.Marker));
    BuildRenderPacketSortKey(&result->Packet);
    result->Success = TRUE;
    result->RejectReason = CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFixedFunctionPipeline::ClearOpaqueRenderPackets()
{
    m_OpaquePacketQueue.Clear();
}

void CKFixedFunctionPipeline::ResetOpaqueRenderPacketFrameState()
{
    m_OpaquePacketQueue.ResetFrameState();
    UpdateOpaqueRenderPacketAdaptiveStats();
}

void CKFixedFunctionPipeline::SortOpaqueRenderPackets(XArray<CKDWORD> &indices)
{
    m_OpaquePacketQueue.SortPackets(indices);
}

void CKFixedFunctionPipeline::InitRenderPacketReplayContext(CKFFRenderPacketReplayContext *context,
                                                            CKRasterizerEncoder *encoder)
{
    if (!context)
        return;

    memset(context, 0, sizeof(CKFFRenderPacketReplayContext));
    context->Encoder = encoder;
    context->Context = m_Context;
    context->Queue = &m_OpaquePacketQueue;
    context->InstanceLayout = m_InstanceLayout;
    CKFFInitRenderPacketReplayDiagnostics(&context->Diagnostics);
    CKFF_PROBE(m_Probes, FillReplayDiagnostics(&context->Diagnostics, m_ShaderCache.GetUniforms()));
}

void CKFixedFunctionPipeline::FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder,
                                                       CKBOOL forceDirectReplay,
                                                       CKBOOL allowAdaptiveLearning)
{
    if (!HasOpaqueRenderPackets())
        return;
    if (!encoder)
        encoder = m_RenderPipeline.GetEncoder();
    if (!encoder) {
        ClearOpaqueRenderPackets();
        return;
    }

    const int packetCount = m_OpaquePacketQueue.GetPacketCount();
    const CKBOOL directReplay = m_OpaquePacketQueue.IsDirectReplay(forceDirectReplay);
    XArray<CKDWORD> indices;
    {
        CKFF_SCOPE_TIME(m_Probes, RenderPacketSortUs);
        if (!directReplay)
            SortOpaqueRenderPackets(indices);
    }
    if (directReplay) {
        if (packetCount > 1)
            CKFF_PROBE(m_Probes, OnRenderPacketSortSkip());
    }

    if (allowAdaptiveLearning) {
        m_OpaquePacketQueue.EvaluateAdaptiveFrameEnd(m_OpaqueInstancingEnabled);
        UpdateOpaqueRenderPacketAdaptiveStats();
    }

    CKRenderPacketReplayCache cache;
    memset(&cache, 0, sizeof(cache));
    CKFFRenderPacketReplayContext replayContext;
    InitRenderPacketReplayContext(&replayContext, encoder);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (m_Probes.TimingEnabled()) {
        CKDWORD runCount = 0;
        CKDWORD maxRun = 0;
        m_OpaquePacketQueue.GetRunStats(&indices, directReplay, &runCount, &maxRun);
        m_Probes.OnRenderPacketRuns(runCount, maxRun);
    }
#endif
    {
        CKFF_SCOPE_TIME(m_Probes, RenderPacketReplayUs);
        XArray<CKFFRenderPacketRunPlan> runPlans;
        m_OpaquePacketQueue.BuildRunPlans(&indices, directReplay, m_OpaqueInstancingEnabled, runPlans);
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

    ClearOpaqueRenderPackets();
    CKFF_PROBE(m_Probes, OnRenderPacketFlush());
}
