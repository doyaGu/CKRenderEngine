#include "CKSdlGpuWorker.h"

// The position of a job in a list, or -1.
static int Position(const XArray<CKSdlGpuJob *> &jobs, const CKSdlGpuJob *job)
{
    for (int i = 0; i < jobs.Size(); ++i) {
        if (jobs[i] == job) return i;
    }
    return -1;
}

bool CKSdlGpuWorker::Start(const char *name)
{
    if (Thread) return true;
    Lock = SDL_CreateMutex();
    Work = SDL_CreateCondition();
    Idle = SDL_CreateCondition();
    Stopping = false;
    if (Lock && Work && Idle)
        Thread = SDL_CreateThread(Main, name, this);
    if (Thread) return true;
    Stop();
    return false;
}

void CKSdlGpuWorker::Stop()
{
    if (Thread) {
        SDL_LockMutex(Lock);
        Stopping = true;
        SDL_SignalCondition(Work);
        SDL_UnlockMutex(Lock);
        SDL_WaitThread(Thread, nullptr);
        Thread = nullptr;
    }
    for (int i = 0; i < Queued.Size(); ++i) delete Queued[i];
    for (int i = 0; i < IdleQueued.Size(); ++i) delete IdleQueued[i];
    for (int i = 0; i < Waiting.Size(); ++i) delete Waiting[i];
    for (int i = 0; i < Finished.Size(); ++i) delete Finished[i];
    Queued.Clear(); IdleQueued.Clear(); Waiting.Clear(); Finished.Clear();
    Active = false;
    if (Idle) SDL_DestroyCondition(Idle);
    if (Work) SDL_DestroyCondition(Work);
    if (Lock) SDL_DestroyMutex(Lock);
    Idle = Work = nullptr; Lock = nullptr;
}

bool CKSdlGpuWorker::Submit(CKSdlGpuJob *job, CKSdlGpuJobPriority priority,
                            const CKSdlGpuJob *after)
{
    if (!job) return false;
    if (!Thread) {
        delete job;
        return false;
    }
    SDL_LockMutex(Lock);
    job->Priority = priority;
    job->After = after && !after->Ran ? after : nullptr;
    if (job->After) {
        Waiting.PushBack(job);
    } else {
        (priority == CKSDLGPU_JOB_IDLE ? IdleQueued : Queued).PushBack(job);
        SDL_SignalCondition(Work);
    }
    SDL_UnlockMutex(Lock);
    return true;
}

bool CKSdlGpuWorker::Promote(const CKSdlGpuJob *job)
{
    if (!Thread || !job) return false;
    bool promoted = false;
    SDL_LockMutex(Lock);
    // A waiting job is queued at its priority once the job it waits for has
    // run, so that one is promoted too.
    for (int waiting; (waiting = Position(Waiting, job)) >= 0; job = Waiting[waiting]->After) {
        if (Waiting[waiting]->Priority == CKSDLGPU_JOB_NORMAL) continue;
        Waiting[waiting]->Priority = CKSDLGPU_JOB_NORMAL;
        promoted = true;
    }
    const int idle = Position(IdleQueued, job);
    if (idle >= 0) {
        Queued.PushBack(IdleQueued[idle]);
        IdleQueued.RemoveAt(idle);
        promoted = true;
    }
    SDL_UnlockMutex(Lock);
    return promoted;
}

CKSdlGpuJob *CKSdlGpuWorker::Collect()
{
    if (!Thread) return nullptr;
    CKSdlGpuJob *job = nullptr;
    SDL_LockMutex(Lock);
    if (Finished.Size() != 0) {
        job = Finished.Front();
        Finished.PopFront();
    }
    SDL_UnlockMutex(Lock);
    return job;
}

int CKSdlGpuWorker::Pending() const
{
    if (!Thread) return 0;
    SDL_LockMutex(Lock);
    const int pending = Queued.Size() + IdleQueued.Size() + Waiting.Size() + (Active ? 1 : 0);
    SDL_UnlockMutex(Lock);
    return pending;
}

bool CKSdlGpuWorker::WaitIdle(Sint32 timeoutMs)
{
    if (!Thread) return true;
    const Uint64 deadline = SDL_GetTicks() + Uint64(timeoutMs < 0 ? 0 : timeoutMs);
    SDL_LockMutex(Lock);
    while (!Idling()) {
        const Uint64 now = SDL_GetTicks();
        if (now >= deadline) break;
        SDL_WaitConditionTimeout(Idle, Lock, Sint32(deadline - now));
    }
    const bool idle = Idling();
    SDL_UnlockMutex(Lock);
    return idle;
}

void CKSdlGpuWorker::Release(const CKSdlGpuJob *job)
{
    // Ahead of the queued jobs, in the order they were submitted.
    int front[2] = {};
    for (int i = 0; i < Waiting.Size();) {
        CKSdlGpuJob *waiting = Waiting[i];
        if (waiting->After != job) {
            ++i;
            continue;
        }
        waiting->After = nullptr;
        const bool idle = waiting->Priority == CKSDLGPU_JOB_IDLE;
        (idle ? IdleQueued : Queued).Insert(front[idle]++, waiting);
        Waiting.RemoveAt(i);
    }
}

bool CKSdlGpuWorker::Idling() const
{
    return !Active && Queued.Size() == 0 && IdleQueued.Size() == 0 && Waiting.Size() == 0;
}

int SDLCALL CKSdlGpuWorker::Main(void *data)
{
    CKSdlGpuWorker &worker = *static_cast<CKSdlGpuWorker *>(data);
    // Background work must not compete with the render thread.
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    SDL_LockMutex(worker.Lock);
    for (;;) {
        while (!worker.Stopping && worker.Queued.Size() == 0 &&
               worker.IdleQueued.Size() == 0)
            SDL_WaitCondition(worker.Work, worker.Lock);
        if (worker.Stopping) break;
        XArray<CKSdlGpuJob *> &queue =
            worker.Queued.Size() != 0 ? worker.Queued : worker.IdleQueued;
        CKSdlGpuJob *job = queue.Front();
        queue.PopFront();
        worker.Active = true;
        SDL_UnlockMutex(worker.Lock);
        job->Run();
        SDL_LockMutex(worker.Lock);
        worker.Active = false;
        job->Ran = true;
        worker.Release(job);
        worker.Finished.PushBack(job);
        if (worker.Idling())
            SDL_BroadcastCondition(worker.Idle);
    }
    SDL_UnlockMutex(worker.Lock);
    return 0;
}
