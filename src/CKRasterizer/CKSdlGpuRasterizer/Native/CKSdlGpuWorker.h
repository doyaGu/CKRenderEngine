#ifndef CKSDLGPU_WORKER_H
#define CKSDLGPU_WORKER_H

#include "XArray.h"

#include <SDL3/SDL.h>

// Idle jobs are speculative. They run only while no normal job is queued,
// so they never delay work that a draw is waiting for; one that a draw comes
// to wait for is promoted.
enum CKSdlGpuJobPriority {
    CKSDLGPU_JOB_NORMAL,
    CKSDLGPU_JOB_IDLE,
};

// Work for CKSdlGpuWorker. Run() executes on the worker thread; the job is
// created and destroyed on the thread that owns the worker.
class CKSdlGpuJob {
public:
    virtual ~CKSdlGpuJob() {}
    virtual void Run() = 0;
    // Called by the owner on its own thread after collecting the job. Jobs
    // that Stop deletes are never completed.
    virtual void Complete() {}

private:
    friend class CKSdlGpuWorker;
    // The worker's, under its lock.
    const CKSdlGpuJob *After = nullptr;
    CKSdlGpuJobPriority Priority = CKSDLGPU_JOB_NORMAL;
    bool Ran = false;
};

// One low-priority thread that runs jobs of each priority in submission
// order, except that a job waiting for another runs as soon as that one has
// run. The owner submits and collects without waiting for a running job;
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
    // A job submitted after another waits until that one has run, then runs
    // before the jobs queued at its priority. The other is a job submitted
    // here that the owner has not deleted.
    bool Submit(CKSdlGpuJob *job, CKSdlGpuJobPriority priority = CKSDLGPU_JOB_NORMAL,
                const CKSdlGpuJob *after = nullptr);
    // Queues an idle job that has not started behind the normal jobs, and
    // with it the jobs it waits for. False when neither waits at idle
    // priority; the job is only compared, so it may have been deleted.
    bool Promote(const CKSdlGpuJob *job);
    // Moves the first job to have run of those not yet collected to the
    // caller, who then owns it. Null when there is none.
    CKSdlGpuJob *Collect();
    // Jobs of either priority queued, waiting or running.
    int Pending() const;
    // Blocks until no job of either priority is queued, waiting or running,
    // or the timeout elapses.
    bool WaitIdle(Sint32 timeoutMs);

private:
    static int SDLCALL Main(void *data);
    // Queues the jobs that waited for one that has run.
    void Release(const CKSdlGpuJob *job);
    bool Idling() const;

    SDL_Thread *Thread = nullptr;
    SDL_Mutex *Lock = nullptr;
    SDL_Condition *Work = nullptr;
    SDL_Condition *Idle = nullptr;
    XArray<CKSdlGpuJob *> Queued;
    XArray<CKSdlGpuJob *> IdleQueued;
    XArray<CKSdlGpuJob *> Waiting;
    XArray<CKSdlGpuJob *> Finished;
    bool Active = false;
    bool Stopping = false;
};

#endif
