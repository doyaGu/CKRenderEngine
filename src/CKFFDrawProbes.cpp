#include "CKFFDrawProbes.h"

#include "CKDebugLogger.h"
#include "CKDrawStateCache.h"
#include "CKFFRenderPacketQueue.h"
#include "CKFFRenderPacketReplay.h"

#include <cstring>

#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
