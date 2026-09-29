#include "CKSdlGpuWorker.h"

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
    for (int i = 0; i < Finished.Size(); ++i) delete Finished[i];
    Queued.Clear(); IdleQueued.Clear(); Finished.Clear();
    Active = false;
    if (Idle) SDL_DestroyCondition(Idle);
    if (Work) SDL_DestroyCondition(Work);
    if (Lock) SDL_DestroyMutex(Lock);
    Idle = Work = nullptr; Lock = nullptr;
}

bool CKSdlGpuWorker::Submit(CKSdlGpuJob *job, CKSdlGpuJobPriority priority)
{
    if (!job) return false;
    if (!Thread) {
        delete job;
        return false;
    }
    SDL_LockMutex(Lock);
    (priority == CKSDLGPU_JOB_IDLE ? IdleQueued : Queued).PushBack(job);
    SDL_SignalCondition(Work);
    SDL_UnlockMutex(Lock);
    return true;
}

void CKSdlGpuWorker::Collect(XArray<CKSdlGpuJob *> &finished)
{
    if (!Thread) return;
    SDL_LockMutex(Lock);
    for (int i = 0; i < Finished.Size(); ++i) finished.PushBack(Finished[i]);
    Finished.Clear();
    SDL_UnlockMutex(Lock);
}

int CKSdlGpuWorker::Pending() const
{
    if (!Thread) return 0;
    SDL_LockMutex(Lock);
    const int pending = Queued.Size() + IdleQueued.Size() + (Active ? 1 : 0);
    SDL_UnlockMutex(Lock);
    return pending;
}

bool CKSdlGpuWorker::WaitIdle(Sint32 timeoutMs)
{
    if (!Thread) return true;
    const Uint64 deadline = SDL_GetTicks() + Uint64(timeoutMs < 0 ? 0 : timeoutMs);
    SDL_LockMutex(Lock);
    while (Active || Queued.Size() != 0 || IdleQueued.Size() != 0) {
        const Uint64 now = SDL_GetTicks();
        if (now >= deadline) break;
        SDL_WaitConditionTimeout(Idle, Lock, Sint32(deadline - now));
    }
    const bool idle = !Active && Queued.Size() == 0 && IdleQueued.Size() == 0;
    SDL_UnlockMutex(Lock);
    return idle;
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
        worker.Finished.PushBack(job);
        if (worker.Queued.Size() == 0 && worker.IdleQueued.Size() == 0)
            SDL_BroadcastCondition(worker.Idle);
    }
    SDL_UnlockMutex(worker.Lock);
    return 0;
}
