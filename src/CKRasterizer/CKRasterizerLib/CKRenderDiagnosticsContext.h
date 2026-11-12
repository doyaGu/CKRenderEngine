#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "CKRenderConfig.h"

struct CKRenderSettingsState;
class CKDebugLogger;

enum CKRenderDiagnosticSlot {
    CK_DIAGNOSTIC_FRAME_COST,
    CK_DIAGNOSTIC_RENDER_PERF,
    CK_DIAGNOSTIC_CLOCK,
    CK_DIAGNOSTIC_LOG_LIMITS,
    CK_DIAGNOSTIC_SLOT_COUNT
};

// Borrowed by rasterizers. Allocation and output remain in the owning module,
// including when the caller uses a different copy of the rasterizer library.
struct CKRenderDiagnosticHandle {
    void *Owner;
    void *(*State)(void *, CKRenderDiagnosticSlot, size_t);
    CKRenderSettingsState *(*Settings)(void *);
    CKDebugLogger *(*Logger)(void *);
    void (*Write)(void *, const char *, const char *, bool);
    bool (*OutputEnabled)(void *);
    uint64_t Id;
};

const CKRenderDiagnosticHandle *CKRenderCurrentDiagnostics();

class CKRenderDiagnosticScope {
public:
    explicit CKRenderDiagnosticScope(const CKRenderDiagnosticHandle *context);
    ~CKRenderDiagnosticScope();
    CKRenderDiagnosticScope(const CKRenderDiagnosticScope &) = delete;
    CKRenderDiagnosticScope &operator=(const CKRenderDiagnosticScope &) = delete;
private:
    const CKRenderDiagnosticHandle *m_Previous;
};

void *CKRenderDiagnosticState(CKRenderDiagnosticSlot slot, size_t size);
template<class T> T &CKRenderDiagnosticState(CKRenderDiagnosticSlot slot) {
    static_assert(std::is_trivially_copyable<T>::value, "Diagnostic slots store POD counters");
    static_assert(alignof(T) <= alignof(std::max_align_t), "Unsupported diagnostic alignment");
    return *static_cast<T *>(CKRenderDiagnosticState(slot, sizeof(T)));
}

class CKRenderDiagnosticsContext {
public:
    explicit CKRenderDiagnosticsContext(bool legacy = false);
    ~CKRenderDiagnosticsContext();
    void Configure(CKRenderSettingsState *settings);
    const CKRenderDiagnosticHandle *Handle() const;
    CKRenderDiagnosticsContext(const CKRenderDiagnosticsContext &) = delete;
    CKRenderDiagnosticsContext &operator=(const CKRenderDiagnosticsContext &) = delete;
private:
    struct Storage;
    Storage *m_Storage;
};

#if CKRE_ENABLE_DEBUG_LOGGER || CKRE_ENABLE_RENDER_STATS || CKRE_ENABLE_FRAME_COST_STATS || CKRE_ENABLE_FFP_DIAGNOSTICS
#define CK_DIAGNOSTIC_SCOPE(handle) CKRenderDiagnosticScope diagnosticScope(handle)
#else
#define CK_DIAGNOSTIC_SCOPE(handle) ((void)0)
#endif
