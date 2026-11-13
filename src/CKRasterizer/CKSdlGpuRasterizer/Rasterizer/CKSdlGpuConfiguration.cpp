#include "CKSdlGpuConfiguration.h"
#include "CKRenderDiagnosticsContext.h"
#include "CKRenderSettings.h"
#include <SDL3/SDL.h>

namespace {
const char *Environment(const char *name) {
    const char *value = SDL_getenv(name);
    return value ? value : "";
}

void ReadString(const CKRenderSettingsView &settings, const char *name,
                const char *environment, XString &value) {
    if (!settings.GetString(name, value)) value = Environment(environment);
}
}

void CKSdlGpuConfiguration::Load(const CKRenderDiagnosticHandle *owner) {
    // Select the owner even when diagnostic collection is compiled out. A
    // standalone context deliberately uses the legacy settings, not its caller.
    CKRenderDiagnosticScope scope(owner);
    const CKRenderSettingsView settings(CKRenderSettingsSection::SdlGpu);
    ReadString(settings, "Driver", "CKRE_SDL_GPU_DRIVER", Driver);
    if (Driver == "auto") Driver = "";

    const char *debug = SDL_getenv("CKRE_SDL_GPU_DEBUG");
    Debug = settings.GetBool("Debug", debug && SDL_strcmp(debug, "0") != 0);
    const char *workers = Environment("CKRE_SDL_GPU_WORKERS");
    Workers = settings.GetInt("Workers", *workers ? SDL_atoi(workers) :
        (SDL_GetNumLogicalCPUCores() >= 4 ? 2 : 1));
    FFJit = settings.GetBool("FFJit", SDL_strcmp(Environment("CKRE_SDL_GPU_FF_JIT"), "0") != 0);
    FFVertexJit = settings.GetBool("FFVertexJit", SDL_strcmp(Environment("CKRE_SDL_GPU_FF_VERTEX_JIT"), "0") != 0);
    ReadString(settings, "FFJitCache", "CKRE_SDL_GPU_FF_JIT_CACHE", FFJitCache);
    FFJitStats = settings.GetBool("FFJitStats", SDL_strcmp(Environment("CKRE_SDL_GPU_FF_JIT_STATS"), "1") == 0);
}
