#include "CKRenderPerfClock.h"
#include "CKRenderDiagnosticsContext.h"

#if CKRE_ENABLE_RENDER_STATS

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

static CKDWORD &NowCallCount() {
    return CKRenderDiagnosticState<CKDWORD>(CK_DIAGNOSTIC_CLOCK);
}

double CKRenderPerfNow() {
    ++NowCallCount();
#if defined(_WIN32)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart;
#else
    return 0.0;
#endif
}

void CKRenderPerfResetNowCallCountForTests() {
    NowCallCount() = 0;
}

CKDWORD CKRenderPerfNowCallCountForTests() {
    return NowCallCount();
}

double CKRenderPerfElapsedUs(double start) {
#if defined(_WIN32)
    static double frequency = []() {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return freq.QuadPart > 0 ? (double)freq.QuadPart : 1.0;
    }();
    return (CKRenderPerfNow() - start) * 1000000.0 / frequency;
#else
    (void)start;
    return 0.0;
#endif
}

#endif // CKRE_ENABLE_RENDER_STATS
