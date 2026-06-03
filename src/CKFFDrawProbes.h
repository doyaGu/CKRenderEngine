#ifndef CKFFDRAWPROBES_H
#define CKFFDRAWPROBES_H

#include "CKFFConstants.h"
#include "CKFFShaderCache.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "CKRenderConfig.h"
#include "CKRenderPerfStats.h"

class CKDrawStateCache;
class CKFFRenderPacketQueue;
struct CKFFRenderPacketReplayDiagnostics;

struct CKFFFrameStats {
    CKDWORD FrameIndex;
    CKDWORD SoftwareDraws;
    CKDWORD HardwareDraws;
    CKDWORD SubmittedDraws;
    CKDWORD PrepareFailures;
    CKDWORD ProgramMisses;
    CKDWORD UniformSets;
    CKDWORD UniformVec4s;
    CKDWORD UniformHandleSets[64];
    CKDWORD UniformHandleVec4s[64];
    CKDWORD TextureBinds;
    CKDWORD VertexLayoutSets;
    CKDWORD VertexBufferSets;
    CKDWORD IndexBufferSets;
    CKDWORD TransformSets;
    CKDWORD ConsecutiveProgramRepeats;
    CKDWORD ConsecutiveDrawStateRepeats;
    CKDWORD ConsecutiveTextureSetRepeats;
    CKDWORD ConsecutiveVertexBufferRepeats;
    CKDWORD ConsecutiveIndexBufferRepeats;
    CKDWORD ConsecutiveWorldMatrixRepeats;
    CKDWORD QueuedRenderPackets;
    CKDWORD ReplayedRenderPackets;
    CKDWORD RenderPacketFallbacks;
    CKDWORD RenderPacketFlushes;
    CKDWORD RenderPacketUniformOverflows;
    CKDWORD RenderPacketSkippedStates;
    CKDWORD RenderPacketSkippedTextures;
    CKDWORD RenderPacketSkippedUniforms;
    CKDWORD RenderPacketStaticUniformUploads;
    CKDWORD RenderPacketStaticUniformSkips;
    CKDWORD RenderPacketObjectUniformUploads;
    CKDWORD RenderPacketObjectUniformSkips;
    CKDWORD RenderPacketSkippedVertexBuffers;
    CKDWORD RenderPacketSkippedIndexBuffers;
    CKDWORD RenderPacketRuns;
    CKDWORD RenderPacketMaxRunLength;
    CKDWORD RenderPacketStaticPayloadBuilds;
    CKDWORD RenderPacketStaticPayloadReuses;
    CKDWORD RenderPacketStaticPayloadInterns;
    CKDWORD RenderPacketSortSkips;
    CKDWORD RenderPacketAdaptiveSamples;
    CKDWORD RenderPacketAdaptiveBypasses;
    CKDWORD RenderPacketAdaptiveSavedBindEstimate;
    CKDWORD RenderPacketAdaptiveRunBypasses;
    CKDWORD RenderPacketAdaptiveSampleRuns;
    CKDWORD RenderPacketAdaptiveSampleMaxRun;
    CKDWORD RenderPacketAdaptiveSubmitSavedEstimate;
    CKDWORD RenderPacketAdaptiveCooldownBypasses;
    CKDWORD RenderPacketAdaptiveCooldownFrames;
    CKDWORD RenderPacketAdaptiveFrameEndEvaluations;
    CKDWORD RenderPacketAdaptiveFrameEndRunBypasses;
    CKDWORD RenderPacketViewProjectionRebuilds;
    CKDWORD RenderPacketInstancedRuns;
    CKDWORD RenderPacketInstancedPackets;
    CKDWORD RenderPacketInstancedSubmits;
    CKDWORD RenderPacketInstanceBufferBytes;
    CKDWORD RenderPacketInstanceAllocFailures;
    CKDWORD RenderPacketSubmitSavedEstimate;
    CKDWORD RenderPacketInstancingFallbacks;
    double RenderPacketBuildUs;
    double RenderPacketSortUs;
    double RenderPacketReplayUs;
    CKDWORD DrawStateCacheHits;
    CKDWORD DrawStateRebuilds;
    CKDWORD TransientVertexBytes;
    CKDWORD TransientIndexBytes;
    double PrepareUs;
    double StateUs;
    double ProgramUs;
    double UniformUs;
    double TextureUs;
    double TransformUs;
    double DrawStateBuildUs;
    double EncoderStateUs;
    double StencilUs;
    double LayoutUs;
    double BufferBindUs;
    double SubmitUs;
    CKDWORD LastProgram;
    CKDrawState LastDrawState;
    CKDWORD LastActiveTextureCount;
    CKDWORD LastTextureHandles[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD LastVertexBuffer;
    CKDWORD LastIndexBuffer;
    CKDWORD LastVertexLayout;
    VxMatrix LastWorldMatrix;
    CKBOOL HasLastProgram;
    CKBOOL HasLastDrawState;
    CKBOOL HasLastTextureSet;
    CKBOOL HasLastVertexBuffer;
    CKBOOL HasLastIndexBuffer;
    CKBOOL HasLastWorldMatrix;
};

#if CKRE_ENABLE_FFP_DIAGNOSTICS

struct CKFFDiagnosticConfig {
    bool StatsEnabled;
    bool UniformHistEnabled;
    int StatsInterval;
};

class CKFFScopeTimer {
public:
    CKFFScopeTimer(double *accum, bool enabled)
        : m_Accum(enabled ? accum : nullptr),
          m_Start(enabled ? CKRenderPerfNow() : 0.0) {}
    ~CKFFScopeTimer() { if (m_Accum) *m_Accum += CKRenderPerfElapsedUs(m_Start); }
private:
    double *m_Accum;
    double m_Start;
};

class CKFFDrawProbes {
public:
    CKFFDrawProbes() : Stats(), Config() {}

    bool StatsEnabled() const { return Config.StatsEnabled || Config.UniformHistEnabled; }
    bool TimingEnabled() const { return Config.StatsEnabled; }
    double *TimerSlot(double CKFFFrameStats::*member) { return &(Stats.*member); }
    void OnSoftwareDraw() { if (StatsEnabled()) ++Stats.SoftwareDraws; }
    void OnHardwareDraw() { if (StatsEnabled()) ++Stats.HardwareDraws; }
    void OnSubmittedDraw() { if (StatsEnabled()) ++Stats.SubmittedDraws; }
    void OnPrepareFailure() { if (StatsEnabled()) ++Stats.PrepareFailures; }
    void OnProgramMiss() { if (StatsEnabled()) ++Stats.ProgramMisses; }
    void OnQueuedRenderPacket() { if (StatsEnabled()) ++Stats.QueuedRenderPackets; }
    void OnRenderPacketFallback() { if (StatsEnabled()) ++Stats.RenderPacketFallbacks; }
    void OnTransformSet() { if (StatsEnabled()) ++Stats.TransformSets; }
    void OnVertexLayoutSet() { if (StatsEnabled()) ++Stats.VertexLayoutSets; }
    void OnVertexBufferSet() { if (StatsEnabled()) ++Stats.VertexBufferSets; }
    void OnIndexBufferSet() { if (StatsEnabled()) ++Stats.IndexBufferSets; }
    void OnTextureBind() { if (StatsEnabled()) ++Stats.TextureBinds; }
    void OnRenderPacketFlush() { if (StatsEnabled()) ++Stats.RenderPacketFlushes; }
    void OnRenderPacketUniformOverflow() { if (StatsEnabled()) ++Stats.RenderPacketUniformOverflows; }
    void OnRenderPacketStaticPayloadBuild() { if (StatsEnabled()) ++Stats.RenderPacketStaticPayloadBuilds; }
    void OnRenderPacketStaticPayloadReuse() { if (StatsEnabled()) ++Stats.RenderPacketStaticPayloadReuses; }
    void OnRenderPacketStaticPayloadIntern() { if (StatsEnabled()) ++Stats.RenderPacketStaticPayloadInterns; }
    void OnRenderPacketSortSkip() { if (StatsEnabled()) ++Stats.RenderPacketSortSkips; }
    void OnViewProjectionRebuild() { if (StatsEnabled()) ++Stats.RenderPacketViewProjectionRebuilds; }
    void OnTransientGeometry(CKDWORD vertexBytes, CKDWORD indexBytes);
    void OnProgram(CKDWORD program);
    void OnWorldMatrix(const VxMatrix &world);
    void OnDrawState(const CKDrawState &drawState);
    void OnTextureSet(CKDWORD activeTextureCount, const CKDWORD *textures);
    void OnVertexBuffers(CKDWORD vb, CKDWORD ib, CKDWORD vertexLayout);
    void OnUniform(const CKFFUniformHandles &uniforms, CKDWORD uniform, CKDWORD count);
    void OnAdaptiveStats(const CKFFRenderPacketQueue &queue);
    void OnAdaptiveBypass(const CKFFRenderPacketQueue &queue);
    void OnRenderPacketRuns(CKDWORD runCount, CKDWORD maxRun);
    void FillReplayDiagnostics(CKFFRenderPacketReplayDiagnostics *diagnostics,
                               const CKFFUniformHandles &uniforms);
    void LogAndReset(CKDrawStateCache &drawStateCache, const CKFFUniformHandles &uniforms);

    CKFFFrameStats Stats;
    CKFFDiagnosticConfig Config;
};

#define CKFF_SCOPE_TIME(probes, field) \
    CKFFScopeTimer _ckff_t_##field((probes).TimerSlot(&CKFFFrameStats::field), (probes).TimingEnabled())
#define CKFF_PROBE(probes, call) do { (probes).call; } while (0)

#else

#define CKFF_SCOPE_TIME(probes, field) do {} while (0)
#define CKFF_PROBE(probes, call) do {} while (0)

#endif

#endif // CKFFDRAWPROBES_H
