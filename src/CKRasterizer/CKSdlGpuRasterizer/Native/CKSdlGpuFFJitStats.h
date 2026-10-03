#ifndef CKSDLGPUFFJITSTATS_H
#define CKSDLGPUFFJITSTATS_H

#include <cstdint>

// Cumulative since device initialization; read on the context thread. Worker
// jobs publish their timings only in Complete(), never into these counters
// from Run(). Durations are CPU wall time in nanoseconds, not GPU timings.
struct CKSdlGpuFFJitStats {
    // Program resolution requests can be reused by several draws. Each request
    // takes exactly one of these paths; they are not rendered-draw counts.
    uint64_t Requests = 0, Specialized = 0;
    uint64_t Unavailable = 0, Capacity = 0, Rejected = 0, QueueDeferred = 0;
    uint64_t Evictions = 0, CompilePendingPeak = 0;
    uint64_t CompileQueued = 0, CompileCompleted = 0, CompileFailed = 0;
    uint64_t CompileNs = 0, CompileMaxNs = 0;
    // POSITIONT and 3D companions share the bounded fragment compilation jobs.
    // Completed counts shader attempts, at most six per fragment job.
    uint64_t VertexCompileCompleted = 0, VertexCompileFailed = 0;

    // Actual pipeline selections for programs with a specialized alternative.
    // Each selection is ready, shader pending, pipeline pending, deferred by
    // the queue budget, or failed.
    uint64_t PipelineSelections = 0, PipelineReady = 0;
    // Ready selections that actually bind a generated vertex shader.
    uint64_t PositionTReady = 0, UnlitReady = 0, LitReady = 0;
    // Subsets of lit/unlit ready selections with deformation active in this draw.
    uint64_t TweenReady = 0, MatrixBlendReady = 0;
    uint64_t DepthPadReady = 0; // generated POSITIONT draws sampling padded depth textures
    uint64_t ClipReady = 0; // subset of generated vertex selections with user clip outputs
    uint64_t ShaderPending = 0, PipelinePending = 0, PipelineDeferred = 0, PipelineFailed = 0;
    // Background pipelines include precompiled manifest entries.
    uint64_t PipelineQueued = 0, PipelineCompleted = 0, PipelineBuildFailed = 0;
    // Queue deferrals include idle prewarming; selection deferrals above only
    // count draws. Pending includes finished-but-uncollected jobs.
    uint64_t PipelineQueueDeferred = 0, PipelinePrewarmDeferred = 0, PipelinePendingPeak = 0;
    // Global PSO cache replacements, including fallback and helper programs.
    uint64_t PipelineEvictions = 0;
    uint64_t PipelineNs = 0, PipelineMaxNs = 0;
    // All synchronous pipeline requests, including helpers and precompiled
    // draws. Includes claiming/waiting for shaders and prewarm jobs.
    uint64_t SynchronousRequests = 0, SynchronousNs = 0, SynchronousMaxNs = 0;
};

#endif
