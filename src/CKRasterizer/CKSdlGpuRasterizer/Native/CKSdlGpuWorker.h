#ifndef CKSDLGPU_WORKER_H
#define CKSDLGPU_WORKER_H

#include "XArray.h"

#include <SDL3/SDL.h>

// Work for CKSdlGpuWorker. Run() executes on the worker thread; the job is
// created and destroyed on the thread that owns the worker.
class CKSdlGpuJob {
public:
    virtual ~CKSdlGpuJob() {}
    virtual void Run() = 0;
    // Called by the owner on its own thread after collecting the job. Jobs
    // that Stop deletes are never completed.
    virtual void Complete() {}
};

// Idle jobs are speculative. They run only while no normal job is queued,
// so they never delay work that a draw is waiting for.
enum CKSdlGpuJobPriority {
    CKSDLGPU_JOB_NORMAL,
    CKSDLGPU_JOB_IDLE,
};

// One low-priority thread that runs jobs of each priority in submission
// order. The owner submits and collects without waiting for a running job;
// only Stop waits.
class CKSdlGpuWorker {
public:
    CKSdlGpuWorker() = default;
    ~CKSdlGpuWorker() { Stop(); }
    CKSdlGpuWorker(const CKSdlGpuWorker &) = delete;
    CKSdlGpuWorker &operator=(const CKSdlGpuWorker &) = delete;

    bool Start(const char *name);
    // Waits for the running job, then deletes every job still owned here,
    // including queued jobs that never ran.
    void Stop();
    bool Running() const { return Thread != nullptr; }

    // Takes ownership. A stopped worker deletes the job and returns false.
    bool Submit(CKSdlGpuJob *job, CKSdlGpuJobPriority priority = CKSDLGPU_JOB_NORMAL);
    // Moves the jobs that have run to the caller, who then owns them.
    void Collect(XArray<CKSdlGpuJob *> &finished);
    // Jobs of either priority queued or running.
    int Pending() const;
    // Blocks until no job of either priority is queued or running, or the
    // timeout elapses.
    bool WaitIdle(Sint32 timeoutMs);

private:
    static int SDLCALL Main(void *data);

    SDL_Thread *Thread = nullptr;
    SDL_Mutex *Lock = nullptr;
    SDL_Condition *Work = nullptr;
    SDL_Condition *Idle = nullptr;
    XArray<CKSdlGpuJob *> Queued;
    XArray<CKSdlGpuJob *> IdleQueued;
    XArray<CKSdlGpuJob *> Finished;
    bool Active = false;
    bool Stopping = false;
};

#endif
