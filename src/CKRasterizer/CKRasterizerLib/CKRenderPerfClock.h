#ifndef CK_RENDER_PERF_CLOCK_H
#define CK_RENDER_PERF_CLOCK_H

#include "CKRenderConfig.h"
#include "CKTypes.h"

// High-resolution clock shared by the engine's CKRenderPerfStats sections and
// the rasterizer's draw probes / present-sync log. The real
// implementation exists only with CKRE_ENABLE_RENDER_STATS; otherwise the
// inline stubs return 0 so callers compile to nothing.
#if CKRE_ENABLE_RENDER_STATS
double CKRenderPerfNow();
double CKRenderPerfElapsedUs(double start);
void CKRenderPerfResetNowCallCountForTests();
CKDWORD CKRenderPerfNowCallCountForTests();
#else
inline double CKRenderPerfNow() { return 0.0; }
inline double CKRenderPerfElapsedUs(double) { return 0.0; }
inline void CKRenderPerfResetNowCallCountForTests() {}
inline CKDWORD CKRenderPerfNowCallCountForTests() { return 0; }
#endif

#endif
