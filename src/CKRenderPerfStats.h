#ifndef CK_RENDER_PERF_STATS_H
#define CK_RENDER_PERF_STATS_H

#include "CKRenderConfig.h"
#include "CKRenderPerfClock.h"
#include "CKTypes.h"

#include "CKRenderDiagnostics.h"

#if CKRE_ENABLE_RENDER_STATS
bool CKRenderPerfStatsEnabled();
void CKRenderPerfBeginFrame(CKDWORD entities3D, CKDWORD entities2D, CKDWORD cameras, CKDWORD lights);
void CKRenderPerfAddSection(CKRenderPerfSection section, double us);
void CKRenderPerfLogAndReset();
CKRenderPerfStats &CKRenderPerfCurrent();

#define CK_RENDER_PERF_DECLARE_ENABLED(Name) const bool Name = CKRenderPerfStatsEnabled()
#define CK_RENDER_PERF_DECLARE_TIMER(Name, Enabled) const double Name = (Enabled) ? CKRenderPerfNow() : 0.0
#define CK_RENDER_PERF_INC(Enabled, Field) do { if (Enabled) ++CKRenderPerfCurrent().Field; } while (0)
#define CK_RENDER_PERF_ADD(Enabled, Field, Value) do { if (Enabled) CKRenderPerfCurrent().Field += (Value); } while (0)
#define CK_RENDER_PERF_IF(Enabled, Code) do { if (Enabled) { Code } } while (0)

#else
inline bool CKRenderPerfStatsEnabled() { return false; }
inline void CKRenderPerfBeginFrame(CKDWORD, CKDWORD, CKDWORD, CKDWORD) {}
inline void CKRenderPerfAddSection(CKRenderPerfSection, double) {}
inline void CKRenderPerfLogAndReset() {}
inline CKRenderPerfStats &CKRenderPerfCurrent() { static CKRenderPerfStats s = {}; return s; }

#define CK_RENDER_PERF_DECLARE_ENABLED(Name) do {} while (0)
#define CK_RENDER_PERF_DECLARE_TIMER(Name, Enabled) do {} while (0)
#define CK_RENDER_PERF_INC(Enabled, Field) do {} while (0)
#define CK_RENDER_PERF_ADD(Enabled, Field, Value) do {} while (0)
#define CK_RENDER_PERF_IF(Enabled, Code) do {} while (0)

#endif

#endif
