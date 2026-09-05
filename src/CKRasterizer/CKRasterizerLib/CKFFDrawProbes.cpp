#include "CKFFDrawProbes.h"

#include "CKDebugLogger.h"
#include "CKDrawStateCache.h"
#include "CKFFDebug.h"
#include "CKFFDrawTypes.h"

#include <string.h>

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

void CKFFDrawProbes::OnUniform(CKBackendConstantBlock block, CKDWORD count)
{
    if (StatsEnabled()) {
        ++Stats.UniformSets;
        Stats.UniformVec4s += count;
    }
    CKDWORD slot = Config.UniformHistEnabled ? CKFFUniformDebugSlot(block) : 64;
    if (slot < 64) {
        ++Stats.UniformHandleSets[slot];
        Stats.UniformHandleVec4s[slot] += count;
    }
}

void CKFFDrawProbes::LogAndReset(CKDrawStateCache &drawStateCache)
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
        CK_LOG_FMT("FFPStats.Timing",
                   "frame=%u prepareUs=%.1f stateUs=%.1f programUs=%.1f uniformUs=%.1f textureUs=%.1f transformUs=%.1f drawStateBuildUs=%.1f pipelineStateUs=%.1f stencilUs=%.1f layoutUs=%.1f bufferBindUs=%.1f submitUs=%.1f",
                   Stats.FrameIndex,
                   Stats.PrepareUs,
                   Stats.StateUs,
                   Stats.ProgramUs,
                   Stats.UniformUs,
                   Stats.TextureUs,
                   Stats.TransformUs,
                   Stats.DrawStateBuildUs,
                   Stats.PipelineStateUs,
                   Stats.StencilUs,
                   Stats.LayoutUs,
                   Stats.BufferBindUs,
                   Stats.SubmitUs);
        if (Config.UniformHistEnabled) {
            for (CKDWORD slot = 0; slot < 64; ++slot) {
                if (Stats.UniformHandleSets[slot] == 0)
                    continue;
                CK_LOG_FMT("FFPUniformHist",
                           "frame=%u uniform=%u name=%s sets=%u vec4=%u",
                           Stats.FrameIndex,
                           slot,
                           slot >= 1 ? CKFFUniformDebugName((CKBackendConstantBlock)(slot - 1)) : "unknown",
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

#else

void CKFFDrawProbes::OnTransientGeometry(CKDWORD, CKDWORD) {}
void CKFFDrawProbes::OnProgram(CKDWORD) {}
void CKFFDrawProbes::OnWorldMatrix(const VxMatrix &) {}
void CKFFDrawProbes::OnDrawState(const CKDrawState &) {}
void CKFFDrawProbes::OnTextureSet(CKDWORD, const CKDWORD *) {}
void CKFFDrawProbes::OnVertexBuffers(CKDWORD, CKDWORD, CKDWORD) {}
void CKFFDrawProbes::OnUniform(CKBackendConstantBlock, CKDWORD) {}
void CKFFDrawProbes::LogAndReset(CKDrawStateCache &) {}

#endif
