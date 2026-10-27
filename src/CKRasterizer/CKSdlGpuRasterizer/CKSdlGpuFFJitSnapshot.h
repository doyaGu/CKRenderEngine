#ifndef CKSDLGPUFFJITSNAPSHOT_H
#define CKSDLGPUFFJITSNAPSHOT_H

#include <cstdint>

class CKRasterizerContext;

// Optional diagnostic export, independent of the rasterizer vtable. V1 is a
// frozen sequence of uint64_t counters; future layouts need a new export name.
// Call on the context thread, between renders. No jobs are waited for or
// collected by the query. An absent export means unavailable, not zero work.
#define CKSDL_GPU_FF_JIT_SNAPSHOT_V1_FIELDS(X) \
    X(Requests) X(Specialized) X(Unavailable) X(Capacity) X(Rejected) X(QueueDeferred) \
    X(CompileQueued) X(CompileCompleted) X(CompileFailed) \
    X(VertexCompileCompleted) X(VertexCompileFailed) \
    X(PipelineSelections) X(PipelineReady) \
    X(PositionTReady) X(UnlitReady) X(LitReady) X(TweenReady) X(MatrixBlendReady) X(ClipReady) X(DepthPadReady) \
    X(ShaderPending) X(PipelinePending) X(PipelineDeferred) X(PipelineFailed) \
    X(PipelineQueued) X(PipelineCompleted) X(PipelineBuildFailed) \
    X(SynchronousRequests) X(Evictions) X(PipelineEvictions)

struct CKSdlGpuFFJitSnapshotV1 {
#define CKSDL_GPU_SNAPSHOT_MEMBER(Name) uint64_t Name = 0;
    CKSDL_GPU_FF_JIT_SNAPSHOT_V1_FIELDS(CKSDL_GPU_SNAPSHOT_MEMBER)
#undef CKSDL_GPU_SNAPSHOT_MEMBER
};
static_assert(sizeof(CKSdlGpuFFJitSnapshotV1) == 30 * sizeof(uint64_t), "snapshot V1 ABI");

using CKSdlGpuQueryFFJitSnapshotV1Function = int (*)(
    const CKRasterizerContext *, uint32_t, CKSdlGpuFFJitSnapshotV1 *);

// Returns 1 on success. A null argument, wrong size, foreign/uninitialized
// context or wrong thread returns 0 without modifying the destination.
#if defined(CKSdlGpuRasterizer_EXPORTS) && defined(_WIN32)
#define CKSDL_GPU_SNAPSHOT_API __declspec(dllexport)
#elif defined(CKSdlGpuRasterizer_EXPORTS)
#define CKSDL_GPU_SNAPSHOT_API __attribute__((visibility("default")))
#else
#define CKSDL_GPU_SNAPSHOT_API
#endif
extern "C" CKSDL_GPU_SNAPSHOT_API int CKSdlGpuQueryFFJitSnapshotV1(
    const CKRasterizerContext *context, uint32_t size, CKSdlGpuFFJitSnapshotV1 *snapshot);
#undef CKSDL_GPU_SNAPSHOT_API

#endif
