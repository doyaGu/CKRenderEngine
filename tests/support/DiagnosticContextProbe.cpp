#include "CKRenderDiagnosticsContext.h"
#include "CKDebugLogger.h"
#include "CKRenderFrameCostStats.h"

#ifdef _WIN32
#define PROBE_EXPORT __declspec(dllexport)
#else
#define PROBE_EXPORT
#endif

// This DLL links its own rasterizer library: its thread-local scope is distinct.
extern "C" PROBE_EXPORT void CKREDiagnosticProbe(const CKRenderDiagnosticHandle *context) {
    CKRenderDiagnosticScope scope(context);
    CK_LOG("DiagnosticProbe", "from-module");
    CKRenderFrameCostStatsAddDrawPrimitive();
}
