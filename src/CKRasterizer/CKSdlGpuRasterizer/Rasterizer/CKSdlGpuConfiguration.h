#pragma once

#include "XString.h"

struct CKRenderDiagnosticHandle;

// Resolved once per device initialization. Strings are owned; subsequent
// environment changes cannot alter a running context's worker or JIT policy.
struct CKSdlGpuConfiguration {
    XString Driver;
    bool Debug = false;
    int Workers = 1;
    bool FFJit = true;
    bool FFVertexJit = true;
    XString FFJitCache;
    bool FFJitStats = false;

    void Load(const CKRenderDiagnosticHandle *owner);
};
