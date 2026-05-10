#include "CKBgfxInternal.h"
#include "CKBgfxConfig.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

static FILE *g_BgfxLogFile = nullptr;

static bool CKBgfxLogNameEquals(const char *lhs, const char *rhs)
{
    return lhs && rhs && _stricmp(lhs, rhs) == 0;
}

static CKBgfxDebugConfig CKBgfxReadDebugSettings()
{
    CKBgfxDebugConfig config = {};

    if (CKBgfxConfigBool("Debug.Bgfx", "Wireframe", false)) config.BgfxFlags |= BGFX_DEBUG_WIREFRAME;
    if (CKBgfxConfigBool("Debug.Bgfx", "IFH", false))       config.BgfxFlags |= BGFX_DEBUG_IFH;
    if (CKBgfxConfigBool("Debug.Bgfx", "Stats", false))     config.BgfxFlags |= BGFX_DEBUG_STATS;
    if (CKBgfxConfigBool("Debug.Bgfx", "Text", false))      config.BgfxFlags |= BGFX_DEBUG_TEXT;
    if (CKBgfxConfigBool("Debug.Bgfx", "Profiler", false))  config.BgfxFlags |= BGFX_DEBUG_PROFILER;
    if (CKBgfxConfigBool("Debug.Bgfx", "All", false))
        config.BgfxFlags |= BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS | BGFX_DEBUG_PROFILER;

    config.Overlay = CKBgfxConfigBool("Debug", "Overlay", (config.BgfxFlags & BGFX_DEBUG_TEXT) != 0);
    if (config.Overlay)
        config.BgfxFlags |= BGFX_DEBUG_TEXT;

    config.Log.File = CKBgfxConfigBool("Debug.Log", "File", false);
    config.Log.Trace = CKBgfxConfigBool("Debug.Log", "Trace", false);
    config.Log.Config = CKBgfxConfigBool("Debug.Log", "Config", false);
    config.Log.Textures = CKBgfxConfigBool("Debug.Log", "Textures", false);
    config.Log.TextureBindings = CKBgfxConfigBool("Debug.Log", "TextureBindings", false);
    config.Log.Uniforms = CKBgfxConfigBool("Debug.Log", "Uniforms", false);
    config.Log.PresentSync = CKBgfxConfigBool("Debug.Log", "PresentSync", false);

    return config;
}

const CKBgfxDebugConfig &CKBgfxDebugSettings()
{
    static const CKBgfxDebugConfig s_Config = CKBgfxReadDebugSettings();
    return s_Config;
}

bool CKBgfxLogEnabled(const char *name, bool fallback)
{
    const CKBgfxLogConfig &log = CKBgfxDebugSettings().Log;
    if (CKBgfxLogNameEquals(name, "File")) return log.File;
    if (CKBgfxLogNameEquals(name, "Trace")) return log.Trace;
    if (CKBgfxLogNameEquals(name, "Config")) return log.Config;
    if (CKBgfxLogNameEquals(name, "Textures")) return log.Textures;
    if (CKBgfxLogNameEquals(name, "TextureBindings")) return log.TextureBindings;
    if (CKBgfxLogNameEquals(name, "Uniforms")) return log.Uniforms;
    if (CKBgfxLogNameEquals(name, "PresentSync")) return log.PresentSync;
    return fallback;
}

static bool CKBgfxFileLogEnabled()
{
    return CKBgfxLogEnabled("File", false);
}

static FILE *CKBgfxGetLogFile()
{
    if (!g_BgfxLogFile) {
        char path[MAX_PATH] = {0};
        HMODULE hMod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&CKBgfxGetLogFile, &hMod)) {
            GetModuleFileNameA(hMod, path, MAX_PATH);
            char *last = strrchr(path, '\\');
            if (last)
                strcpy_s(last + 1, MAX_PATH - (last + 1 - path), "CKBgfx_Trace.log");
        }
        if (path[0] == '\0')
            strcpy_s(path, "CKBgfx_Trace.log");
        fopen_s(&g_BgfxLogFile, path, "w");
    }
    return g_BgfxLogFile;
}

void CKBgfxCloseLogFile()
{
    if (g_BgfxLogFile) {
        fclose(g_BgfxLogFile);
        g_BgfxLogFile = nullptr;
    }
}

void CKBgfxLogf(const char *tag, const char *fmt, ...)
{
    char msg[2048];
    int n = _snprintf_s(msg, sizeof(msg), _TRUNCATE, "[CKBgfx] [%s] ", tag ? tag : "?");
    if (n < 0)
        n = 0;

    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(msg + n, sizeof(msg) - n, _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringA(msg);
    OutputDebugStringA("\n");

    if (CKBgfxFileLogEnabled()) {
        FILE *f = CKBgfxGetLogFile();
        if (f) {
            fputs(msg, f);
            fputc('\n', f);
            fflush(f);
        }
    }
}

const char *CKBgfxRendererTypeName(bgfx::RendererType::Enum type)
{
    switch (type) {
    case bgfx::RendererType::Direct3D11: return "Direct3D11";
    case bgfx::RendererType::Direct3D12: return "Direct3D12";
    case bgfx::RendererType::Vulkan:     return "Vulkan";
    case bgfx::RendererType::OpenGL:     return "OpenGL";
    case bgfx::RendererType::OpenGLES:   return "OpenGLES";
    case bgfx::RendererType::Noop:       return "Noop";
    default:                             return "Auto";
    }
}

CK_SHADER_PROFILE CKBgfxShaderProfile(bgfx::RendererType::Enum type)
{
    switch (type) {
    case bgfx::RendererType::Direct3D11: return CKRST_SHADER_PROFILE_DX11;
    case bgfx::RendererType::Direct3D12: return CKRST_SHADER_PROFILE_DX12;
    case bgfx::RendererType::Vulkan:     return CKRST_SHADER_PROFILE_SPIRV;
    case bgfx::RendererType::OpenGL:
    case bgfx::RendererType::OpenGLES:   return CKRST_SHADER_PROFILE_GLSL;
    default:                             return CKRST_SHADER_PROFILE_UNKNOWN;
    }
}

bgfx::RendererType::Enum CKBgfxParseRequestedRenderer()
{
    char value[32] = {0};
    if (!CKBgfxConfigString("Renderer", "Backend", value, (CKDWORD)sizeof(value)) ||
        _stricmp(value, "auto") == 0)
        return bgfx::RendererType::Count;
    if (_stricmp(value, "d3d11") == 0 || _stricmp(value, "direct3d11") == 0)
        return bgfx::RendererType::Direct3D11;
    if (_stricmp(value, "d3d12") == 0 || _stricmp(value, "direct3d12") == 0)
        return bgfx::RendererType::Direct3D12;
    if (_stricmp(value, "vulkan") == 0)
        return bgfx::RendererType::Vulkan;
    if (_stricmp(value, "opengl") == 0 || _stricmp(value, "gl") == 0)
        return bgfx::RendererType::OpenGL;

    CKBgfxLogf("Init", "unknown Renderer/Backend='%s', falling back to auto", value);
    return bgfx::RendererType::Count;
}
