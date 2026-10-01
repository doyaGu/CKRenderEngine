// Device and command coverage for the complete SDL_gpu rasterizer.
#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuFFJitManifest.h"
#include "CKSdlGpuShaders.h"
#include "CKSdlGpuTextureData.h"
#include "CKSdlGpuWorker.h"
#include "CKFFShaderInterface.h"
#include <cstdio>

int main()
{
    unsigned failures = 0;
    auto check = [&](bool value, const char *name) { if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } };
    check(CKSdlGpuTextureFormat(_16_RGB565) == SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM &&
          CKSdlGpuTextureFormat(_16_ARGB1555) == SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM &&
          CKSdlGpuTextureFormat(_DXT1) == SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM &&
          CKSdlGpuTextureFormat(_DXT3) == SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM &&
          CKSdlGpuTextureFormat(_DXT5) == SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM &&
          CKSdlGpuTextureFormat(_DXT2) == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM &&
          CKSdlGpuTextureFormat(_DXT4) == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
          "premultiplied DXT formats use decoded BGRA storage");
    check(CKSdlGpuValidPresentSync(CKRST_PRESENT_UNCHANGED) &&
          CKSdlGpuValidPresentSync(CKRST_PRESENT_VSYNC) &&
          CKSdlGpuValidPresentSync(CKRST_PRESENT_IMMEDIATE) &&
          !CKSdlGpuValidPresentSync(static_cast<CKPresentSync>(99)),
          "only declared presentation synchronization modes are accepted");
    check(CKSdlGpuCanCopyPresent(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                 SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                 640, 480, 640, 480) &&
          !CKSdlGpuCanCopyPresent(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                  SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                  640, 480, 640, 480) &&
          !CKSdlGpuCanCopyPresent(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                  SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                  640, 480, 1280, 960),
          "present copy fast path requires identical format and extent");
    check(CKSdlGpuSupportsSwapchainCopy("direct3d12") &&
          !CKSdlGpuSupportsSwapchainCopy("vulkan") &&
          !CKSdlGpuSupportsSwapchainCopy(nullptr),
          "only the verified D3D12 swapchain path uses transfer copy");
    {
        const auto program = CKFFBuildProgramInterface(1, 2, CKRST_SHADER_FORMAT_DXIL);
        CKFFProgramLayout layout;
        layout.Init(program);
        CKFFConstantSet constants;
        layout.Update(constants);
        CKSdlGpuUniformBatch batch;
        CKSdlGpuUniformCursor cursor;
        CKSdlGpuUniformBindings native;
        unsigned first[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned moved[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned fragment[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned draw[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        batch.Snapshot(layout, cursor, first);
        check(layout.Buffers.Size() == 5 &&
              layout.Buffers[0].Stage == CKRST_SHADER_VERTEX && layout.Buffers[0].Slot == 0 &&
              layout.Buffers[0].Size == 512 &&
              layout.Buffers[1].Stage == CKRST_SHADER_VERTEX && layout.Buffers[1].Slot == 1 &&
              layout.Buffers[1].Size == 320 &&
              layout.Buffers[2].Stage == CKRST_SHADER_VERTEX && layout.Buffers[2].Slot == 2 &&
              layout.Buffers[2].Size == 2048 &&
              layout.Buffers[3].Stage == CKRST_SHADER_PIXEL && layout.Buffers[3].Slot == 0 &&
              layout.Buffers[3].Size == 320 &&
              layout.Buffers[4].Stage == CKRST_SHADER_PIXEL && layout.Buffers[4].Slot == 1 &&
              layout.Buffers[4].Size == 1104 && batch.Data.Size() == 3984 && first[3] == first[1] &&
              native.NeedsPush(layout.Buffers[0], first[0], batch.Data) &&
              native.NeedsPush(layout.Buffers[1], first[1], batch.Data) &&
              native.NeedsPush(layout.Buffers[2], first[2], batch.Data) &&
              native.NeedsPush(layout.Buffers[3], first[3], batch.Data) &&
              native.NeedsPush(layout.Buffers[4], first[4], batch.Data),
              "first FFP draw binds every isolated native buffer, snapshotting shared ones once");
        float matrix[16] = {}; matrix[12] = 2.0f;
        constants.Set(CKRST_BLOCK_MATRICES, matrix, sizeof(matrix));
        layout.Update(constants);
        batch.Snapshot(layout, cursor, moved);
        check(moved[0] != first[0] && moved[1] == first[1] && moved[2] == first[2] &&
              moved[3] == first[3] && moved[4] == first[4] && batch.Data.Size() == 3984 + 512 &&
              native.NeedsPush(layout.Buffers[0], moved[0], batch.Data) &&
              !native.NeedsPush(layout.Buffers[1], moved[1], batch.Data),
              "matrix-only changes snapshot/push 512 vertex bytes");
        const float bump[4] = {0.5f, 1, 0, 0};
        constants.Set(CKRST_BLOCK_BUMP_ENV, bump, sizeof(bump));
        layout.Update(constants);
        batch.Snapshot(layout, cursor, fragment);
        check(fragment[0] == moved[0] && fragment[1] == moved[1] && fragment[2] == moved[2] &&
              fragment[3] == moved[3] && fragment[4] != moved[4] && batch.Data.Size() == 3984 + 512 + 1104 &&
              !native.NeedsPush(layout.Buffers[0], fragment[0], batch.Data) &&
              !native.NeedsPush(layout.Buffers[3], fragment[3], batch.Data) &&
              native.NeedsPush(layout.Buffers[4], fragment[4], batch.Data),
              "fragment-only changes preserve the vertex buffer version and binding");
        auto shared = std::find_if(program.Uniforms.Begin(), program.Uniforms.End(), [](const auto &uniform) {
            return uniform.Slot == CKRST_BLOCK_DRAW_PARAMS && uniform.Stage == CKRST_SHADER_PIXEL;
        });
        const float factor[4] = {0.1f, 0.2f, 0.3f, 1};
        constants.Set(CKRST_BLOCK_DRAW_PARAMS, factor, sizeof(factor));
        layout.Update(constants);
        check(shared != program.Uniforms.End() &&
              std::memcmp(layout.Data.Begin() + layout.BufferOffset(CKRST_SHADER_PIXEL, 0) + shared->Offset,
                          factor, sizeof(factor)) == 0,
              "logical data used by both stages is copied into the pixel stage's declared range");
        batch.Snapshot(layout, cursor, draw);
        check(draw[0] == fragment[0] && draw[1] != fragment[1] && draw[2] == fragment[2] &&
              draw[3] == draw[1] && draw[4] == fragment[4] && batch.Data.Size() == 3984 + 512 + 1104 + 320 &&
              native.NeedsPush(layout.Buffers[1], draw[1], batch.Data) &&
              !native.NeedsPush(layout.Buffers[2], draw[2], batch.Data) &&
              native.NeedsPush(layout.Buffers[3], draw[3], batch.Data) &&
              !native.NeedsPush(layout.Buffers[4], draw[4], batch.Data),
              "draw-parameter changes snapshot 320 bytes once and push them to both stages");
    }
    {
        CKFFProgramLayout layout;
        CKFFProgramLayout::Buffer buffer = {CKRST_SHADER_VERTEX, 0, 0, 16};
        layout.Buffers.PushBack(buffer);
        buffer = {CKRST_SHADER_PIXEL, 0, 16, 32};
        layout.Buffers.PushBack(buffer);
        buffer = {CKRST_SHADER_VERTEX, 1, 16, 32};
        layout.Buffers.PushBack(buffer);
        layout.Data.Resize(48);
        memset(layout.Data.Begin(), 1, 48);
        CKSdlGpuUniformBatch batch;
        CKSdlGpuUniformCursor cursor, otherProgram;
        unsigned first[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned second[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned third[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned other[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        batch.Snapshot(layout, cursor, first);
        check(batch.Data.Size() == 48 && first[1] == first[2], "shared stage data is snapshotted once");
        for (unsigned i = 0; i < 256; ++i) batch.Snapshot(layout, cursor, second);
        check(batch.Data.Size() == 48 && memcmp(first, second, sizeof(first)) == 0,
              "256 unchanged draws reuse immutable buffer versions");
        layout.Data[0] = 2;
        check(layout.MarkDataChanged(0, 1), "object data mutation advances its buffer version");
        batch.Snapshot(layout, cursor, second);
        check(batch.Data.Size() == 64 && second[0] != first[0] && second[1] == first[1] &&
              batch.Data[first[0]] == 1 && batch.Data[second[0]] == 2,
              "object change leaves the shared fragment data and earlier draw intact");
        // Metadata is written directly by the backend, independently of the
        // producer's revision. It must participate in snapshot identity.
        layout.Data[40] = 3;
        check(layout.MarkDataChanged(40, 1), "sampler metadata mutation advances the shared buffer version");
        batch.Snapshot(layout, cursor, third);
        check(batch.Data.Size() == 96 && third[0] == second[0] && third[1] != second[1] &&
              batch.Data[second[1] + 24] == 1 && batch.Data[third[1] + 24] == 3,
              "sampler metadata changes cannot overwrite or reuse old draw bytes");
        CKSdlGpuUniformBindings bindings;
        check(bindings.NeedsPush(layout.Buffers[0], first[0], batch.Data) &&
              !bindings.NeedsPush(layout.Buffers[0], first[0], batch.Data) &&
              bindings.NeedsPush(layout.Buffers[0], second[0], batch.Data),
              "native push follows byte changes within the pass");
        batch.Snapshot(layout, otherProgram, other);
        check(!bindings.NeedsPush(layout.Buffers[0], other[0], batch.Data),
              "equal bytes at another arena offset do not require a second push");
        check(bindings.NeedsPush(layout.Buffers[1], third[1], batch.Data) &&
              bindings.NeedsPush(layout.Buffers[2], third[2], batch.Data),
              "shared bytes still bind independently to both shader stages");
        bindings.Invalidate();
        check(bindings.NeedsPush(layout.Buffers[0], other[0], batch.Data),
              "pass invalidation requires a fresh push");
        batch.Clear();
        layout.Data.Resize(48);
        memset(layout.Data.Begin(), 4, 48);
        check(layout.MarkDataChanged(0, 48) && !layout.MarkDataChanged(47, 2),
              "uniform data revisions validate the modified byte range");
        batch.Snapshot(layout, cursor, first);
        check(batch.Data.Size() == 48 && first[0] == 0 && batch.Data[0] == 4,
              "a new batch never follows a previous batch's offsets");
    }
    {
        CKSdlGpuProgram program;
        program.Identity = 0x10001;
        CKFFSamplerBinding declaration;
        declaration.Stage = CKRST_SHADER_PIXEL; declaration.NativeSlot = 7;
        program.Interface.Samplers.PushBack(declaration);
        CKSdlGpuTable<CKSdlGpuTexture> textures;
        auto texture = std::make_shared<CKSdlGpuTexture>();
        auto handle = textures.Add(texture);
        // Aliased inert tokens exercise ownership without constructing a GPU.
        auto token = std::make_shared<int>(1);
        std::shared_ptr<SDL_GPUSampler> sampler(token, reinterpret_cast<SDL_GPUSampler *>(token.get()));
        CKSdlGpuBindingBatch batch;
        CKSdlGpuBindingBatch::Inputs inputs;
        inputs.Hash = program.Identity;
        inputs.Textures[0] = texture.get(); inputs.Samplers[0] = sampler.get();
        inputs.TextureOwners[0] = &textures.Borrow(handle); inputs.SamplerOwners[0] = &sampler;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(sampler.get()) >> 4);
        const unsigned original = batch.Intern(program, inputs);
        const long owners = texture.use_count();
        for (unsigned i = 0; i < 256; ++i)
            check(batch.Intern(program, inputs) == original, "repeated draw finds its binding group");
        check(batch.Size() == 1 && texture.use_count() == owners,
              "resource retention follows unique binding groups, not draws");
        check(batch[original].Fragment[7].sampler == sampler.get(), "logical declaration maps to the native slot");
        batch.MarkReferenced();
        check(texture->Referenced, "encoded binding groups mark sampled textures for version preservation");
        textures.Remove(handle);
        auto replacementTexture = std::make_shared<CKSdlGpuTexture>();
        auto replacementHandle = textures.Add(replacementTexture);
        inputs.Textures[0] = replacementTexture.get();
        inputs.TextureOwners[0] = &textures.Borrow(replacementHandle);
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(replacementTexture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(sampler.get()) >> 4);
        const unsigned replacement = batch.Intern(program, inputs);
        check(replacement != original && batch[original].Textures[0] == texture.get() &&
              batch[replacement].Textures[0] == replacementTexture.get(), "deleted texture and reused handle keep separate group lifetimes");
        inputs.Textures[0] = texture.get(); inputs.TextureOwners[0] = &texture;
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(sampler.get()) >> 4);
        check(batch.Intern(program, inputs) == original, "nonconsecutive draws reuse an earlier group");
        auto secondToken = std::make_shared<int>(2);
        std::shared_ptr<SDL_GPUSampler> secondSampler(secondToken, reinterpret_cast<SDL_GPUSampler *>(secondToken.get()));
        inputs.Samplers[0] = secondSampler.get(); inputs.SamplerOwners[0] = &secondSampler;
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(secondSampler.get()) >> 4);
        check(batch.Intern(program, inputs) != original, "sampler state distinguishes binding groups");
        program.Identity = 0x20001;
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(secondSampler.get()) >> 4);
        check(batch.Intern(program, inputs) == 3, "program generation distinguishes native slot layouts");
        batch.Clear();
        check(batch.Size() == 0 && texture.use_count() == 1, "batch completion releases resource ownership");
    }
    {
        CKSdlGpuDrawResourceBatch batch;
        auto program = std::make_shared<CKSdlGpuProgram>();
        auto layout = std::make_shared<CKSdlGpuLayout>();
        auto buffer = std::make_shared<CKSdlGpuBuffer>();
        check(batch.Retain(program) == program.get() && batch.Retain(program) == program.get() &&
              batch.Retain(layout) == layout.get() && batch.Retain(buffer) == buffer.get() &&
              program.use_count() == 2 && layout.use_count() == 2 && buffer.use_count() == 2,
              "draw resources are retained once per distinct batch resource");
        batch.Clear();
        check(program.use_count() == 1 && layout.use_count() == 1 && buffer.use_count() == 1,
              "draw resource ownership ends with the encoded batch");
    }
    {
        struct Job : CKSdlGpuJob {
            SDL_AtomicInt *Sequence = nullptr;
            SDL_Semaphore *Started = nullptr, *Gate = nullptr;
            int *Deleted = nullptr;
            int Order = -1;
            SDL_ThreadID Thread = 0;
            ~Job() override { ++*Deleted; }
            void Run() override {
                Thread = SDL_GetCurrentThreadID();
                if (Started) SDL_SignalSemaphore(Started);
                if (Gate) SDL_WaitSemaphore(Gate);
                Order = SDL_AddAtomicInt(Sequence, 1);
            }
        };
        SDL_AtomicInt sequence;
        SDL_SetAtomicInt(&sequence, 0);
        int deleted = 0;
        auto job = [&](SDL_Semaphore *started = nullptr, SDL_Semaphore *gate = nullptr) {
            Job *result = new Job;
            result->Sequence = &sequence; result->Deleted = &deleted;
            result->Started = started; result->Gate = gate;
            return result;
        };
        CKSdlGpuWorker worker;
        auto collect = [&worker](XArray<CKSdlGpuJob *> &finished) {
            while (CKSdlGpuJob *each = worker.Collect()) finished.PushBack(each);
        };
        check(!worker.Collect(), "a stopped worker has no finished jobs");
        check(!worker.Submit(job()) && deleted == 1 && worker.Pending() == 0,
              "a stopped worker deletes submitted jobs");
        check(worker.Start("CKSdlGpuWorkerTest") && worker.Running(), "worker starts");
        Job *jobs[3];
        for (Job *&each : jobs) {
            each = job();
            check(worker.Submit(each), "running worker accepts jobs");
        }
        check(worker.WaitIdle(5000) && worker.Pending() == 0, "worker drains its queue");
        XArray<CKSdlGpuJob *> finished;
        collect(finished);
        check(finished.Size() == 3 && finished[0] == jobs[0] && finished[1] == jobs[1] &&
              finished[2] == jobs[2] && jobs[0]->Order == 0 && jobs[1]->Order == 1 &&
              jobs[2]->Order == 2 && deleted == 1,
              "jobs run and are collected in submission order");
        for (int i = 0; i < finished.Size(); ++i) delete finished[i];
        finished.Clear();
        SDL_Semaphore *started = SDL_CreateSemaphore(0), *gate = SDL_CreateSemaphore(0);
        Job *blocker = job(started, gate), *idle[3], *normal[2];
        worker.Submit(blocker);
        check(SDL_WaitSemaphoreTimeout(started, 5000), "blocking job starts");
        for (int i = 0; i < 3; ++i) {
            idle[i] = job();
            worker.Submit(idle[i], CKSDLGPU_JOB_IDLE);
            if (i < 2) {
                normal[i] = job();
                worker.Submit(normal[i]);
            }
        }
        check(worker.Pending() == 6 && !worker.WaitIdle(10), "idle jobs are pending work");
        check(worker.Promote(idle[2]) && !worker.Promote(idle[2]) && !worker.Promote(normal[0]) &&
              !worker.Promote(blocker) && worker.Pending() == 6,
              "only a queued idle job is promoted");
        SDL_SignalSemaphore(gate);
        check(worker.WaitIdle(5000) && worker.Pending() == 0, "worker drains both priorities");
        collect(finished);
        check(finished.Size() == 6 && blocker->Order == 3 && normal[0]->Order == 4 &&
              normal[1]->Order == 5 && idle[2]->Order == 6 && idle[0]->Order == 7 &&
              idle[1]->Order == 8,
              "normal jobs, then promoted ones, run before queued idle jobs");
        check(!worker.Promote(idle[0]), "a finished job is not promoted");
        for (int i = 0; i < finished.Size(); ++i) delete finished[i];
        finished.Clear();
        Job *first = job(started, gate), *queued = job(), *next = job(), *last = job();
        worker.Submit(first);
        check(SDL_WaitSemaphoreTimeout(started, 5000), "blocking job starts");
        worker.Submit(queued);
        worker.Submit(next, CKSDLGPU_JOB_NORMAL, first);
        worker.Submit(last, CKSDLGPU_JOB_NORMAL, next);
        check(worker.Pending() == 4 && !worker.WaitIdle(10), "waiting jobs are pending work");
        SDL_SignalSemaphore(gate);
        check(worker.WaitIdle(5000) && worker.Pending() == 0, "worker drains waiting jobs");
        collect(finished);
        check(finished.Size() == 4 && first->Order < next->Order && next->Order < last->Order &&
              last->Order < queued->Order,
              "a job runs once the one it waits for has run, before the jobs queued");
        Job *after = job();
        check(worker.Submit(after, CKSDLGPU_JOB_NORMAL, first) && worker.WaitIdle(5000) &&
              worker.Pending() == 0 && after->Order > last->Order,
              "a job submitted after one that has run is queued");
        collect(finished);
        for (int i = 0; i < finished.Size(); ++i) delete finished[i];
        finished.Clear();
        Job *gated = job(started, gate), *idleFirst = job(), *compile = job(), *pipeline = job();
        worker.Submit(gated);
        check(SDL_WaitSemaphoreTimeout(started, 5000), "blocking job starts");
        worker.Submit(idleFirst, CKSDLGPU_JOB_IDLE);
        worker.Submit(compile, CKSDLGPU_JOB_IDLE);
        worker.Submit(pipeline, CKSDLGPU_JOB_IDLE, compile);
        check(worker.Promote(pipeline) && !worker.Promote(pipeline) && !worker.Promote(compile),
              "a waiting job is promoted with the job it waits for");
        SDL_SignalSemaphore(gate);
        check(worker.WaitIdle(5000), "worker drains promoted waiting jobs");
        collect(finished);
        check(finished.Size() == 4 && compile->Order < pipeline->Order &&
              pipeline->Order < idleFirst->Order,
              "promoted waiting jobs run before queued idle jobs");
        for (int i = 0; i < finished.Size(); ++i) delete finished[i];
        finished.Clear();
        Job *idleLast = job();
        worker.Submit(job(started, gate));
        worker.Submit(idleLast, CKSDLGPU_JOB_IDLE);
        worker.Submit(job());
        worker.Submit(job(), CKSDLGPU_JOB_NORMAL, idleLast);
        check(SDL_WaitSemaphoreTimeout(started, 5000), "blocking job starts");
        collect(finished);
        check(finished.Size() == 0 && worker.Pending() == 4 && !worker.WaitIdle(10),
              "collection does not wait for a running job");
        SDL_SignalSemaphore(gate);
        worker.Stop();
        check(!worker.Running() && deleted == 23 && worker.Pending() == 0,
              "stopping deletes running, queued, idle, waiting and uncollected jobs");
        check(!worker.Submit(job()) && deleted == 24, "a stopped worker rejects jobs");
        check(worker.Start("CKSdlGpuWorkerTest") && worker.Submit(job()) &&
              worker.WaitIdle(5000), "a stopped worker restarts");
        worker.Stop();
        check(deleted == 25, "stopping deletes finished jobs");
        check(worker.Start("CKSdlGpuWorkerTest", 2) && worker.Running(), "worker starts two threads");
        worker.Submit(job(started, gate));
        worker.Submit(job(started, gate));
        check(SDL_WaitSemaphoreTimeout(started, 5000) && SDL_WaitSemaphoreTimeout(started, 5000),
              "two threads run two jobs at once");
        check(worker.Pending() == 2 && !worker.WaitIdle(10), "both running jobs are pending work");
        SDL_SignalSemaphore(gate);
        SDL_SignalSemaphore(gate);
        check(worker.WaitIdle(5000) && worker.Pending() == 0, "two threads drain their jobs");
        Job *before = job(started, gate);
        worker.Submit(before);
        check(SDL_WaitSemaphoreTimeout(started, 5000), "blocking job starts");
        worker.Submit(job(started, gate), CKSDLGPU_JOB_NORMAL, before);
        worker.Submit(job(started, gate), CKSDLGPU_JOB_NORMAL, before);
        SDL_SignalSemaphore(gate);
        check(SDL_WaitSemaphoreTimeout(started, 5000) && SDL_WaitSemaphoreTimeout(started, 5000),
              "the jobs waiting for one that has run start on both threads");
        SDL_SignalSemaphore(gate);
        SDL_SignalSemaphore(gate);
        worker.Stop();
        check(!worker.Running() && deleted == 30 && worker.Pending() == 0,
              "stopping two threads deletes their jobs");
        check(worker.Start("CKSdlGpuWorkerTest") && !worker.Claim(nullptr), "worker starts for claims");
        Job *running = job(started, gate), *queuedClaim = job(), *idleClaim = job(),
            *dependent = job(), *waitingClaim = job();
        worker.Submit(running);
        check(SDL_WaitSemaphoreTimeout(started, 5000), "blocking job starts");
        worker.Submit(queuedClaim);
        worker.Submit(idleClaim, CKSDLGPU_JOB_IDLE);
        worker.Submit(dependent, CKSDLGPU_JOB_NORMAL, queuedClaim);
        worker.Submit(waitingClaim, CKSDLGPU_JOB_NORMAL, running);
        check(!worker.Claim(dependent) && !worker.Claim(waitingClaim) && worker.Pending() == 5,
              "a job waiting for another is not claimed");
        const SDL_ThreadID owner = SDL_GetCurrentThreadID();
        check(worker.Claim(idleClaim) == idleClaim && idleClaim->Thread == owner &&
              worker.Claim(queuedClaim) == queuedClaim && queuedClaim->Thread == owner &&
              worker.Pending() == 3,
              "claiming a queued job runs it on the calling thread");
        check(worker.Claim(dependent) == dependent && dependent->Thread == owner,
              "a job waiting for a claimed one is queued once that has run");
        struct Opener {
            static int SDLCALL Run(void *gate)
            {
                SDL_Delay(20);
                SDL_SignalSemaphore(static_cast<SDL_Semaphore *>(gate));
                return 0;
            }
        };
        SDL_Thread *opener = SDL_CreateThread(Opener::Run, "CKSdlGpuWorkerTestGate", gate);
        check(opener && worker.Claim(running) == running && running->Order >= 0 &&
              running->Thread != owner,
              "claiming a running job waits for its thread");
        SDL_WaitThread(opener, nullptr);
        check(worker.WaitIdle(5000) && worker.Claim(waitingClaim) == waitingClaim &&
              waitingClaim->Thread != owner,
              "claiming a finished job takes it");
        check(!worker.Claim(waitingClaim) && !worker.Collect() && worker.Pending() == 0,
              "a claimed job is the caller's");
        for (Job *each : {running, queuedClaim, idleClaim, dependent, waitingClaim}) delete each;
        worker.Stop();
        check(deleted == 35, "stopping leaves claimed jobs to the caller");
        SDL_DestroySemaphore(started);
        SDL_DestroySemaphore(gate);
    }
    {
        auto program = [](CKDWORD seed) {
            CKSdlGpuFFJitProgramRecord result;
            CKDWORD *values = reinterpret_cast<CKDWORD *>(&result);
            for (size_t i = 0; i < sizeof(result) / sizeof(CKDWORD); ++i)
                values[i] = (seed * 2654435761u + CKDWORD(i) * 40503u) & CKFFFragmentProgram::LaneMask;
            // Switch word 0 has no bit 3, and the sampling bytes no bit 7.
            result.Switches[0] &= ~0x08u;
            result.Switches[3] &= 0x7f7f7f7fu;
            result.Switches[4] &= 0x7f7f7f7fu;
            result.SamplerLayout = seed % CKFF_SAMPLER_LAYOUT_COUNT;
            return result;
        };
        auto pipeline = [](CKDWORD seed, CKDWORD programs) {
            CKSdlGpuFFJitPipelineRecord result;
            result.Program = CKBYTE(seed % programs);
            result.Variant = CKBYTE(seed % CKFF_PROGRAM_VARIANT_COUNT);
            result.Flags = CKBYTE(seed % 4);
            result.ColorFormat = CKBYTE(seed * 7 + 1);
            result.DepthFormat = CKBYTE(seed * 5);
            result.SampleCount = CKBYTE(seed % 4);
            result.StencilReadMask = CKBYTE(seed * 13);
            result.StencilWriteMask = CKBYTE(seed * 17);
            result.VertexFormat = seed * 2654435761u;
            result.StateLo = seed * 40503u;
            result.StateMid = seed ^ 0x5bd1e995u;
            result.StateHi = ~seed;
            return result;
        };
        auto manifestOf = [&](CKDWORD programs, CKDWORD pipelines) {
            CKSdlGpuFFJitManifest result;
            for (CKDWORD i = 0; i < programs; ++i) result.Programs.PushBack(program(i + 1));
            for (CKDWORD i = 0; i < pipelines; ++i) result.Pipelines.PushBack(pipeline(i + 1, programs));
            return result;
        };
        auto same = [](const CKSdlGpuFFJitManifest &a, const CKSdlGpuFFJitManifest &b) {
            return a.Programs.Size() == b.Programs.Size() && a.Pipelines.Size() == b.Pipelines.Size() &&
                   (a.Programs.Size() == 0 ||
                    std::memcmp(a.Programs.Begin(), b.Programs.Begin(), a.Programs.Size() * sizeof(a.Programs[0])) == 0) &&
                   (a.Pipelines.Size() == 0 ||
                    std::memcmp(a.Pipelines.Begin(), b.Pipelines.Begin(), a.Pipelines.Size() * sizeof(a.Pipelines[0])) == 0);
        };
        auto empty = [](const CKSdlGpuFFJitManifest &manifest) {
            return manifest.Programs.Size() == 0 && manifest.Pipelines.Size() == 0;
        };
        // The 28-byte header ends with the checksum of the rest.
        const int header = 28;
        const int programSize = sizeof(CKSdlGpuFFJitProgramRecord);
        const int pipelineSize = sizeof(CKSdlGpuFFJitPipelineRecord);
        auto reseal = [&](XArray<CKBYTE> &bytes) {
            CKDWORD hash = CKFFHashBytes(bytes.Begin(), header - 4, 2166136261u);
            hash = CKFFHashBytes(bytes.Begin() + header, CKDWORD(bytes.Size() - header), hash);
            std::memcpy(bytes.Begin() + header - 4, &hash, sizeof(hash));
        };
        const uint64_t identity =
            CKSdlGpuFFJitManifestIdentity("direct3d12", "GPU", SDL_GPU_SHADERFORMAT_DXBC);
        check(identity == CKSdlGpuFFJitManifestIdentity("direct3d12", "GPU", SDL_GPU_SHADERFORMAT_DXBC) &&
              identity != CKSdlGpuFFJitManifestIdentity("vulkan", "GPU", SDL_GPU_SHADERFORMAT_DXBC) &&
              identity != CKSdlGpuFFJitManifestIdentity("direct3d12", "GPU 2", SDL_GPU_SHADERFORMAT_DXBC) &&
              identity != CKSdlGpuFFJitManifestIdentity("direct3d12", "GPU", SDL_GPU_SHADERFORMAT_SPIRV) &&
              identity != CKSdlGpuFFJitManifestIdentity("direct3d12G", "PU", SDL_GPU_SHADERFORMAT_DXBC),
              "FF JIT manifest identity follows driver, device and shader format");
        CKSdlGpuFFJitManifest manifest = manifestOf(3, 7), decoded;
        XArray<CKBYTE> data;
        CKSdlGpuEncodeFFJitManifest(identity, manifest, data);
        check(data.Size() == header + 3 * programSize + 7 * pipelineSize &&
              CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded) &&
              same(manifest, decoded), "FF JIT manifests round trip");
        XArray<CKBYTE> emptyData;
        CKSdlGpuEncodeFFJitManifest(identity, CKSdlGpuFFJitManifest(), emptyData);
        check(CKSdlGpuDecodeFFJitManifest(identity, emptyData.Begin(), emptyData.Size(), decoded) &&
              empty(decoded), "empty FF JIT manifests round trip");
        decoded = manifest;
        bool rejected = !CKSdlGpuDecodeFFJitManifest(identity + 1, data.Begin(), data.Size(), decoded) &&
                        empty(decoded) &&
                        !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size() - 1, decoded) &&
                        !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), 8, decoded) &&
                        !CKSdlGpuDecodeFFJitManifest(identity, nullptr, data.Size(), decoded);
        XArray<CKBYTE> longer = data;
        longer.PushBack(0);
        rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, longer.Begin(), longer.Size(), decoded);
        for (int offset : {0, 4, 8, 12, 16, 20, 24, header, header + 3 * programSize, data.Size() - 1}) {
            XArray<CKBYTE> corrupt = data;
            corrupt[offset] ^= 1;
            decoded = manifest;
            rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, corrupt.Begin(), corrupt.Size(), decoded) &&
                       empty(decoded);
        }
        check(rejected, "truncated, corrupt or foreign FF JIT manifests are rejected");
        XArray<CKBYTE> resealed = data;
        reseal(resealed);
        check(CKSdlGpuDecodeFFJitManifest(identity, resealed.Begin(), resealed.Size(), decoded) &&
              same(manifest, decoded), "the test reseals FF JIT manifests as they are written");
        CKSdlGpuFFJitManifest malformed = manifest;
        malformed.Programs[1].Lanes[0] = CKFFFragmentProgram::LaneMask + 1;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        rejected = !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded) && empty(decoded);
        malformed = manifest;
        malformed.Programs[1].Switches[0] |= CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING << 1;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded);
        malformed = manifest;
        malformed.Programs[1].Switches[4] |= 0x80u << 16;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded);
        malformed = manifest;
        malformed.Programs[0].SamplerLayout = CKFF_SAMPLER_LAYOUT_COUNT;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded);
        malformed = manifest;
        malformed.Pipelines[2].Variant = CKFF_PROGRAM_VARIANT_COUNT;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded);
        malformed = manifest;
        malformed.Pipelines[6].Flags = CKSDL_GPU_FF_JIT_PIPELINE_PRECOMPILED << 1;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded);
        // The writer leaves out pipelines of programs it does not have.
        CKSdlGpuEncodeFFJitManifest(identity, manifest, data);
        data[header + 3 * programSize + 4 * pipelineSize] = 3;
        reseal(data);
        rejected = rejected && !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded) &&
                   empty(decoded);
        check(rejected, "an FF JIT manifest with a malformed record is rejected as a whole");
        CKSdlGpuFFJitManifest many = manifestOf(CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS + 5,
                                                CKSDL_GPU_FF_JIT_MANIFEST_MAX_PIPELINES + 5);
        CKSdlGpuEncodeFFJitManifest(identity, many, data);
        many.Programs.Resize(CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS);
        many.Pipelines.Resize(CKSDL_GPU_FF_JIT_MANIFEST_MAX_PIPELINES);
        check(CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded) && same(many, decoded),
              "FF JIT manifests keep their first records up to the limits");
        malformed = manifest;
        malformed.Pipelines[1].Program = 3;
        malformed.Pipelines[4].Program = 200;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        malformed.Pipelines.RemoveAt(4);
        malformed.Pipelines.RemoveAt(1);
        check(CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded) && same(malformed, decoded),
              "FF JIT manifests leave out the pipelines of programs they do not have");
        // Precompiled pipelines index artifacts, whatever programs there are.
        malformed = manifest;
        malformed.Pipelines[1].Flags = CKSDL_GPU_FF_JIT_PIPELINE_PRECOMPILED;
        malformed.Pipelines[1].Program = CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT - 1;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        const bool precompiledKept =
            CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded) &&
            same(malformed, decoded);
        malformed.Pipelines[1].Program = CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT;
        CKSdlGpuEncodeFFJitManifest(identity, malformed, data);
        check(precompiledKept && manifest.Programs.Size() < CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT - 1 &&
                  !CKSdlGpuDecodeFFJitManifest(identity, data.Begin(), data.Size(), decoded),
              "FF JIT manifests keep the pipelines of precompiled artifacts");

        XString directory(SDL_GetBasePath() ? SDL_GetBasePath() : "");
        directory << "ffjit-manifest-test";
        char name[32];
        SDL_snprintf(name, sizeof(name), "ffjit-%016llX.bin", (unsigned long long)identity);
        SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", "0", 1);
        check(CKSdlGpuFFJitManifestPath(identity).Length() == 0,
              "CKRE_SDL_GPU_FF_JIT_CACHE=0 disables the FF JIT manifest");
        SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", directory.CStr(), 1);
        const XString path = CKSdlGpuFFJitManifestPath(identity);
        XString expected(directory);
        expected << "/" << name;
        check(path == expected, "CKRE_SDL_GPU_FF_JIT_CACHE names the FF JIT manifest directory");
        SDL_unsetenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE");
        const XString defaultPath = CKSdlGpuFFJitManifestPath(identity);
        check(SDL_strstr(defaultPath.CStr(), "CKSdlGpuCache") && SDL_strstr(defaultPath.CStr(), name),
              "the FF JIT manifest defaults to CKSdlGpuCache next to the rasterizer");
        SDL_RemovePath(path.CStr());
        decoded = manifest;
        check(!CKSdlGpuLoadFFJitManifest(path.CStr(), identity, decoded) && empty(decoded),
              "a missing FF JIT manifest does not load");
        check(CKSdlGpuSaveFFJitManifest(path.CStr(), identity, manifest) &&
              CKSdlGpuLoadFFJitManifest(path.CStr(), identity, decoded) && same(manifest, decoded),
              "saving an FF JIT manifest creates its directory");
        manifest.Programs.PushBack(program(9));
        manifest.Pipelines.PushBack(pipeline(9, 4));
        check(CKSdlGpuSaveFFJitManifest(path.CStr(), identity, manifest) &&
              CKSdlGpuLoadFFJitManifest(path.CStr(), identity, decoded) && same(manifest, decoded),
              "saving an FF JIT manifest replaces the file");
        check(!CKSdlGpuLoadFFJitManifest(path.CStr(), identity + 1, decoded) && empty(decoded),
              "an FF JIT manifest of another device does not load");
        int files = 0;
        SDL_EnumerateDirectory(directory.CStr(), [](void *count, const char *, const char *) {
            ++*static_cast<int *>(count);
            return SDL_ENUM_CONTINUE;
        }, &files);
        check(files == 1, "saving an FF JIT manifest leaves no temporary file");
        SDL_RemovePath(path.CStr());
        SDL_RemovePath(directory.CStr());
    }
    {
        CKSdlGpuRasterizerContext context;
        check(context.CompleteEmptySubmissionsForTests(),
              "empty submissions complete in order");
        check(context.CollectJobsWithinBudgetForTests(),
              "a frame completes the finished jobs its budget allows, at least one");
    }
    {
        CKSdlGpuBuffer buffer;
        const CKWORD initial[] = {4, 1, 7, 2, 6, 3};
        buffer.IndexData.Resize(sizeof(initial));
        std::memcpy(buffer.IndexData.Begin(), initial, sizeof(initial));
        unsigned maximum = 0;
        check(buffer.FindMaxIndex(1, 4, false, maximum) && maximum == 7,
              "16-bit persistent index ranges find their maximum");
        const CKWORD replacement = 5;
        std::memcpy(buffer.IndexData.Begin() + 2 * sizeof(CKWORD),
                    &replacement, sizeof(replacement));
        check(buffer.FindMaxIndex(1, 4, false, maximum) && maximum == 7,
              "persistent index range lookup reuses the validated cache");
        buffer.InvalidateIndexRanges();
        check(buffer.FindMaxIndex(1, 4, false, maximum) && maximum == 6,
              "buffer updates invalidate persistent index range results");
        const CKDWORD wide[] = {0x10002u, 9u, 0x10001u};
        buffer.IndexData.Resize(sizeof(wide));
        std::memcpy(buffer.IndexData.Begin(), wide, sizeof(wide));
        buffer.InvalidateIndexRanges();
        check(buffer.FindMaxIndex(0, 3, true, maximum) && maximum == 0x10002u &&
              !buffer.FindMaxIndex(3, 1, true, maximum),
              "32-bit index ranges preserve width and reject out-of-bounds requests");
        check(buffer.FindMaxIndex(0, 3, false, maximum) && maximum == 9,
              "persistent index range cache keys include the index element width");
    }
    {
        unsigned char blocks[32] = {};
        VxImageDescEx image;
        XArray<unsigned char> pixels;
        auto decode = [&](VX_PIXELFORMAT format, unsigned width = 4, unsigned height = 4) {
            VxPixelFormat2ImageDesc(format, image);
            image.Width = width; image.Height = height; image.TotalImageSize = sizeof(blocks); image.Image = blocks;
            return CKSdlGpuDecodeDXT(image, pixels);
        };
        auto pixel = [&](unsigned index) { CKDWORD value = 0; std::memcpy(&value, pixels.Begin() + index * 4, 4); return value; };
        // BC1 endpoints blue < red: indices 0,1,2,3 include transparent black.
        blocks[0] = 31; blocks[3] = 248;
        for (unsigned y = 0; y < 4; ++y) blocks[4 + y] = 0xe4;
        check(decode(_DXT1), "decode BC1");
        check(pixel(0) == 0xff0000ff && pixel(1) == 0xffff0000 &&
              pixel(2) == 0xff7f007f && pixel(3) == 0, "BC1 transparent palette");
        blocks[0] = 0; blocks[1] = 248; blocks[2] = 31; blocks[3] = 0;
        check(decode(_DXT1) && pixel(2) == 0xffaa0055 && pixel(3) == 0xff5500aa, "BC1 opaque palette");
        // A second green block covers the clipped last column of a 5x3 image.
        blocks[8] = 224; blocks[9] = 7;
        check(decode(_DXT1, 5, 3) && pixels.Size() == 60 && pixel(4) == 0xff00ff00 &&
              pixel(14) == 0xff00ff00, "BC1 odd dimensions retain edge blocks");
        image.TotalImageSize = 15;
        check(!CKSdlGpuDecodeDXT(image, pixels), "reject incomplete BC payload");
        std::memset(blocks, 0, sizeof(blocks));
        blocks[8] = 31; blocks[11] = 248;
        for (unsigned y = 0; y < 4; ++y) blocks[12 + y] = 0xff;
        for (unsigned i = 0; i < 8; ++i) blocks[i] = (2 * i) | ((2 * i + 1) << 4);
        check(decode(_DXT3), "decode BC2");
        for (unsigned i = 0; i < 16; ++i)
            check(pixel(i) == ((i * 17u << 24) | 0x00aa0055), "BC2 explicit alpha and four-color palette");
        std::memset(blocks, 0, sizeof(blocks));
        blocks[0] = 0x08; // alpha 136, then zero
        blocks[1] = 0x01; // alpha 17, then zero
        blocks[9] = 0x78; // premultiplied red endpoint decodes to 123
        check(decode(_DXT2) && pixel(0) == 0x88e70000 && pixel(1) == 0 &&
              pixel(2) == 0x11ff0000,
              "DXT2 unpremultiplies explicit alpha, zeroes transparent pixels and clamps color");
        std::memset(blocks, 0, sizeof(blocks));
        blocks[8] = 31; blocks[11] = 248;
        for (unsigned y = 0; y < 4; ++y) blocks[12 + y] = 0xff;
        uint64_t indices = 0;
        for (unsigned i = 0; i < 16; ++i) indices |= uint64_t(i % 8) << (i * 3);
        for (unsigned i = 0; i < 6; ++i) blocks[2 + i] = (unsigned char)(indices >> (i * 8));
        blocks[0] = 210; blocks[1] = 0;
        check(decode(_DXT5), "decode BC3 interpolated alpha");
        const unsigned alpha7[] = {210, 0, 180, 150, 120, 90, 60, 30};
        for (unsigned i = 0; i < 16; ++i) check(pixel(i) == ((alpha7[i % 8] << 24) | 0x00aa0055), "BC3 seven-step alpha");
        blocks[0] = 0; blocks[1] = 200;
        check(decode(_DXT5), "decode BC3 explicit extremes");
        const unsigned alpha5[] = {0, 200, 40, 80, 120, 160, 0, 255};
        for (unsigned i = 0; i < 16; ++i) check(pixel(i) == ((alpha5[i % 8] << 24) | 0x00aa0055), "BC3 five-step alpha");
        std::memset(blocks, 0, sizeof(blocks));
        blocks[0] = 128; blocks[2] = 0x08; // alpha 128, then zero
        blocks[9] = 0x78;
        check(decode(_DXT4) && pixel(0) == 0x80f50000 && pixel(1) == 0,
              "DXT4 unpremultiplies interpolated alpha and zeroes transparent pixels");
    }
    {
        bool seen[CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT] = {};
        CKDWORD uniqueArtifactCount = 0;
        for (CKDWORD layout = 0; layout < CKFF_SAMPLER_LAYOUT_COUNT; ++layout) {
            for (CKDWORD requiresShaderSampling = 0;
                 requiresShaderSampling < 2; ++requiresShaderSampling) {
                CKFFSamplerLayoutPlan plan;
                plan.Layout = (CKFFSamplerLayout)layout;
                CKSdlGpuFFFragmentArtifactKey key;
                check(CKSdlGpuBuildFFFragmentArtifactKey(
                          plan, requiresShaderSampling != 0, key),
                      "build no-compare fragment artifact key");
                const CKDWORD index = CKSdlGpuFFFragmentArtifactIndex(key);
                check(index < CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT &&
                          !seen[index],
                      "no-compare fragment artifact key has a unique cache index");
                if (index < CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT &&
                    !seen[index]) {
                    seen[index] = true;
                    ++uniqueArtifactCount;
                }
            }
        }
        for (CKDWORD comparisonCount = 1;
             comparisonCount <= CKFF_MAX_TEXTURE_STAGES; ++comparisonCount) {
            CKFFSamplerLayoutPlan plan;
            plan.CompareSamplerCount = (CKBYTE)comparisonCount;
            CKSdlGpuFFFragmentArtifactKey key;
            check(CKSdlGpuBuildFFFragmentArtifactKey(plan, FALSE, key),
                  "build comparison fragment artifact key");
            const CKDWORD index = CKSdlGpuFFFragmentArtifactIndex(key);
            check(index < CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT &&
                      !seen[index],
                  "comparison fragment artifact key has a unique cache index");
            if (index < CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT &&
                !seen[index]) {
                seen[index] = true;
                ++uniqueArtifactCount;
            }
        }
        check(uniqueArtifactCount == CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT,
              "fragment artifact cache index covers every precompiled artifact");
        bool inverted = true;
        for (CKDWORD index = 0; index <= CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT; ++index) {
            CKSdlGpuFFFragmentArtifactKey key;
            const bool named = CKSdlGpuFFFragmentArtifactKeyAt(index, key) != FALSE;
            inverted = inverted && named == (index < CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT) &&
                       (!named || CKSdlGpuFFFragmentArtifactIndex(key) == index);
        }
        check(inverted, "every fragment artifact cache index names its key");
    }
    for (auto format : {SDL_GPU_SHADERFORMAT_DXIL, SDL_GPU_SHADERFORMAT_SPIRV}) {
        CKFFShaderSet set;
        check(CKSdlGpuShaderSet(format, set) != FALSE, "complete native shader family");
        const auto payload = set.Shaders[0].Format, profile = set.Shaders[0].Profile;
        check(payload != CKRST_SHADER_FORMAT_BGFX, "native artifact ownership");
        for (unsigned role = 0; role < CKRST_BUILTIN_SHADER_COUNT; ++role) {
            auto incomplete = set;
            incomplete.Shaders[role].Code = nullptr;
            check(!incomplete.Matches(payload, profile), "missing variant excludes family");
        }
        auto mismatched = set;
        mismatched.InterfaceHash ^= 1;
        check(!mismatched.Matches(payload, profile), "ABI mismatch excludes family");
        mismatched = set;
        mismatched.InterfaceHash = CKFF_SHADER_INTERFACE_HASH;
        check(!mismatched.Matches(payload, profile), "old shared native layout hash is rejected");
        check(set.Shaders[CKRST_SHADER_FF_3D].UniformBufferCount == 3 &&
              set.Shaders[CKRST_SHADER_FF_3D_CLIP].UniformBufferCount == 3 &&
              set.Shaders[CKRST_SHADER_FF_POSITIONT].UniformBufferCount == 2 &&
              set.Shaders[CKRST_SHADER_FF_POSITIONT_CLIP].UniformBufferCount == 2 &&
              set.Shaders[CKRST_SHADER_FF_FRAGMENT].UniformBufferCount == 2,
              "native FFP shaders match variant-specific buffer layouts");
        check(set.Shaders[CKRST_SHADER_FF_FRAGMENT].SamplerCount == 16, "FFP logical slots retained");
        check(set.Shaders[CKRST_SHADER_PRESENT_FRAGMENT].SamplerCount == 1, "presentation uses native slot zero");
        for (CKDWORD samplerLayout = 0;
             samplerLayout < CKFF_SAMPLER_LAYOUT_COUNT; ++samplerLayout) {
            CKFFSamplerLayoutPlan plan;
            plan.Layout = (CKFFSamplerLayout)samplerLayout;
            CKSdlGpuFFFragmentArtifactKey shaderKey, hardwareKey;
            CKShaderDesc shaderSampling, hardwareSampling;
            check(CKSdlGpuBuildFFFragmentArtifactKey(
                      plan, TRUE, shaderKey) &&
                      shaderKey.UsesShaderSampling &&
                      CKSdlGpuFFFragmentArtifactIndex(shaderKey) <
                          CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT &&
                      CKSdlGpuFFFragmentShader(
                          format, shaderKey, shaderSampling) &&
                      shaderSampling.Code && shaderSampling.CodeSize,
                  "SDL exposes every shader-controlled sampler layout");
            check(CKSdlGpuBuildFFFragmentArtifactKey(
                      plan, FALSE, hardwareKey) &&
                      !hardwareKey.UsesShaderSampling &&
                      CKSdlGpuFFFragmentArtifactIndex(hardwareKey) <
                          CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT &&
                      CKSdlGpuFFFragmentShader(
                          format, hardwareKey, hardwareSampling) &&
                      hardwareSampling.Code && hardwareSampling.CodeSize &&
                      hardwareSampling.Code != shaderSampling.Code &&
                      hardwareSampling.CodeSize < shaderSampling.CodeSize,
                  "SDL exposes a smaller distinct hardware-sampling layout");
        }
        for (CKDWORD layoutIndex = 0;
             layoutIndex < CKFF_SAMPLER_LAYOUT_COUNT; ++layoutIndex) {
            const CKFFSamplerLayout layout =
                (CKFFSamplerLayout)layoutIndex;
            const CKDWORD maximum = layout == CKFF_SAMPLER_LAYOUT_WIDE_2D
                ? 8u : 3u;
            const void *previousCode = nullptr;
            const void *comparisonCode = nullptr;
            for (CKDWORD count = 0; count <= maximum; ++count) {
                CKFFSamplerLayoutPlan plan;
                plan.Layout = layout;
                plan.CompareSamplerCount = (CKBYTE)count;
                CKSdlGpuFFFragmentArtifactKey hardwareKey, shaderKey;
                CKShaderDesc hardwareRequest, shaderRequest;
                const bool hardwareSelected =
                    CKSdlGpuBuildFFFragmentArtifactKey(
                        plan, FALSE, hardwareKey) &&
                    CKSdlGpuFFFragmentShader(
                        format, hardwareKey, hardwareRequest);
                const bool shaderSelected =
                    CKSdlGpuBuildFFFragmentArtifactKey(
                        plan, TRUE, shaderKey) &&
                    CKSdlGpuFFFragmentShader(
                        format, shaderKey, shaderRequest);
                bool expectedArtifact = false;
                if (count == 0) {
                    expectedArtifact = hardwareRequest.Code !=
                        shaderRequest.Code &&
                        !hardwareKey.UsesShaderSampling &&
                        shaderKey.UsesShaderSampling;
                } else if (layout == CKFF_SAMPLER_LAYOUT_WIDE_2D) {
                    expectedArtifact = hardwareRequest.Code != previousCode &&
                        hardwareRequest.Code == shaderRequest.Code &&
                        hardwareKey.ComparisonResourceCount == count &&
                        shaderKey.ComparisonResourceCount == count &&
                        hardwareKey.UsesShaderSampling &&
                        shaderKey.UsesShaderSampling;
                } else {
                    if (count == 1)
                        comparisonCode = hardwareRequest.Code;
                    expectedArtifact = hardwareRequest.Code == comparisonCode &&
                        hardwareRequest.Code == shaderRequest.Code &&
                        hardwareKey.ComparisonResourceCount == 0 &&
                        shaderKey.ComparisonResourceCount == 0 &&
                        hardwareKey.UsesShaderSampling &&
                        shaderKey.UsesShaderSampling;
                }
                check(hardwareSelected && shaderSelected &&
                          hardwareRequest.Code && hardwareRequest.CodeSize &&
                          shaderRequest.Code && shaderRequest.CodeSize &&
                          expectedArtifact,
                      "comparison ABI remains independent of shader sampling requirements");
                previousCode = hardwareRequest.Code;
            }
            CKFFSamplerLayoutPlan impossiblePlan;
            impossiblePlan.Layout = layout;
            impossiblePlan.CompareSamplerCount = (CKBYTE)(maximum + 1);
            CKSdlGpuFFFragmentArtifactKey impossibleComparison;
            check(!CKSdlGpuBuildFFFragmentArtifactKey(
                      impossiblePlan, FALSE, impossibleComparison),
                  "SDL rejects comparison counts that cannot occur in an eight-stage layout");
        }
        CKFFSamplerLayoutPlan validPlan;
        CKSdlGpuFFFragmentArtifactKey invalidBoolean;
        check(!CKSdlGpuBuildFFFragmentArtifactKey(
                  validPlan, (CKBOOL)2, invalidBoolean),
              "SDL rejects a non-boolean shader sampling requirement");
        validPlan.Layout = (CKFFSamplerLayout)-1;
        check(!CKSdlGpuBuildFFFragmentArtifactKey(
                  validPlan, FALSE, invalidBoolean),
              "SDL rejects a negative sampler layout");
        CKShaderDesc vertex, fragment;
        check(CKSdlGpuNativeClearShaders(format, vertex, fragment) && vertex.UniformBufferCount == 1 &&
              fragment.UniformBufferCount == 1 && !vertex.SamplerCount && !fragment.SamplerCount,
              "clear shaders have private uniforms and no samplers");
        auto clearProgram = CKSdlGpuNativeProgram(1, 2, CKSDL_NATIVE_CLEAR);
        check(CKFFValidateProgram(clearProgram, vertex, fragment) == CK_OK &&
              clearProgram.UniformBuffers.Size() == 2 && clearProgram.UniformBuffers[0].Size == 16 &&
              clearProgram.UniformBuffers[1].Size == 16 && clearProgram.Uniforms.Size() == 2 &&
              clearProgram.Uniforms[0].Name == "ckClear" && clearProgram.Uniforms[1].Name == "ckClear" &&
              clearProgram.VertexInputs.Size() == 0 && clearProgram.Samplers.Size() == 0,
              "clear program declares independent 16-byte vertex and fragment data");
        check(CKSdlGpuNativeVolumeShaders(format, vertex, fragment) && vertex.UniformBufferCount == 1 &&
              fragment.UniformBufferCount == 1 && fragment.SamplerCount == 1,
              "volume mip helper exposes its own native shader family");
        auto volumeProgram = CKSdlGpuNativeProgram(1, 2, CKSDL_NATIVE_VOLUME);
        check(CKFFValidateProgram(volumeProgram, vertex, fragment) == CK_OK &&
              volumeProgram.UniformBuffers[0].Size == 16 && volumeProgram.UniformBuffers[1].Size == 32 &&
              volumeProgram.Uniforms[1].Name == "ckVolumeParams" && volumeProgram.Uniforms[1].Count == 2 &&
              volumeProgram.Samplers.Size() == 1 && volumeProgram.Samplers[0].Slot == 0 &&
              volumeProgram.Samplers[0].Dimension == CKFF_TEXTURE_3D &&
              volumeProgram.Samplers[0].MetadataBufferSlot == UINT32_MAX && volumeProgram.VertexInputs.Size() == 0,
              "volume helper uses 32-byte fragment parameters and one independent volume sampler");
        CKFFProgramLayout layout;
        layout.Init(volumeProgram);
        check(layout.Data.Size() == 48 && layout.BufferOffset(CKRST_SHADER_VERTEX, 0) == 0 &&
              layout.BufferOffset(CKRST_SHADER_PIXEL, 0) == 16,
              "private image operations do not allocate or share the FFP constant layout");
    }
    {
        CKFFShaderSet dxil;
        check(CKSdlGpuShaderSet(SDL_GPU_SHADERFORMAT_DXIL, dxil) != FALSE, "DXIL family for DXBC comparison");
        CKShaderDesc fragment = dxil.Shaders[CKRST_SHADER_FF_FRAGMENT];
        fragment.Format = CKRST_SHADER_FORMAT_DXBC;
        for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
            CKShaderDesc vertex;
            const bool found = CKSdlGpuFFDxbcVertexShader((CKFFProgramVariant)variant, vertex) != FALSE;
            check(found && vertex.Stage == CKRST_SHADER_VERTEX &&
                      vertex.Format == CKRST_SHADER_FORMAT_DXBC &&
                      vertex.Profile == CKRST_SHADER_PROFILE_DX12 &&
                      vertex.CodeSize > 4 && std::memcmp(vertex.Code, "DXBC", 4) == 0 &&
                      vertex.UniformBufferCount == dxil.Shaders[variant].UniformBufferCount &&
                      vertex.SamplerCount == dxil.Shaders[variant].SamplerCount,
                  "DXBC vertex shaders match their DXIL variants");
            const CKBOOL positionT = variant == CKFF_PROGRAM_POSITIONT ||
                variant == CKFF_PROGRAM_POSITIONT_CLIP;
            const CKFFProgramDesc program = CKFFBuildProgramInterface(
                1, 2, CKRST_SHADER_FORMAT_DXIL, FALSE, positionT);
            check(found && CKFFValidateProgram(program, vertex, fragment) == CK_OK,
                  "DXBC stages form a native fixed-function program");
        }
        CKShaderDesc invalid;
        check(!CKSdlGpuFFDxbcVertexShader(CKFF_PROGRAM_VARIANT_COUNT, invalid),
              "DXBC vertex shaders reject unknown variants");
    }
    CKFFShaderSet rejected;
    check(!CKSdlGpuShaderSet(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV, rejected), "ambiguous payload rejected");
    CKShaderDesc invalidVertex, invalidFragment;
    check(!CKSdlGpuNativeClearShaders(SDL_GPU_SHADERFORMAT_INVALID, invalidVertex, invalidFragment) &&
          !CKSdlGpuNativeVolumeShaders(SDL_GPU_SHADERFORMAT_INVALID, invalidVertex, invalidFragment),
          "private helper families reject unsupported payload formats");
    CKSdlGpuTable<int> handles;
    auto old = handles.Add(std::make_shared<int>(10));
    auto reference = handles.Get(old);
    check(handles.Remove(old), "remove live resource");
    auto replacement = handles.Add(std::make_shared<int>(20));
    check(old != replacement && !handles.Get(old), "reuse does not revive stale handle");
    check(*reference == 10 && *handles.Get(replacement) == 20, "recorded references retain original resource");
    std::printf("SDL_gpu rasterizer: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
