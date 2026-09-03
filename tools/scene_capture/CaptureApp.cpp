#include "CaptureApp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "CKAll.h"
#include "CKPluginManager.h"

namespace {

std::string BaseNameNoExt(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    return name;
}

bool EqualsNoCase(const std::string &a, const char *b)
{
    if (a.size() != strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}

CKERROR LogRedirect(CKUICallbackStruct &, void *)
{
    return CK_OK;
}

std::string JsonEscape(const char *text)
{
    std::string out;
    for (const char *p = text; p && *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

std::string Hex(CKDWORD value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "\"0x%08X\"", value);
    return buf;
}

std::string TodayIso()
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[32];
    if (t)
        strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    else
        snprintf(buf, sizeof(buf), "unknown");
    return buf;
}

} // namespace

CaptureApp::CaptureApp() = default;

CaptureApp::~CaptureApp()
{
    Shutdown();
}

void CaptureApp::Fail(const std::string &message)
{
    m_Error = message;
    fprintf(stderr, "ckre_scene_capture: %s\n", message.c_str());
}

bool CaptureApp::Boot(const CaptureOptions &options)
{
    m_Options = options;
    if (!InitWindow(options))
        return false;
    if (!InitEngine(options))
        return false;
    return true;
}

bool CaptureApp::InitWindow(const CaptureOptions &options)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        Fail(std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }
    m_SdlInitialized = true;

    SDL_WindowFlags flags = 0;
    if (options.HiddenWindow)
        flags |= SDL_WINDOW_HIDDEN;
    m_Window = SDL_CreateWindow("ckre_scene_capture", options.Width, options.Height, flags);
    if (!m_Window) {
        Fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(m_Window, 32, 32);
    m_WindowHandle = m_Window;

    if (options.NativeWindowHandle) {
#if defined(_WIN32)
        void *hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(m_Window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (!hwnd) {
            Fail("--native-window-handle: the SDL window has no Win32 HWND");
            return false;
        }
        m_WindowHandle = hwnd;
#else
        Fail("--native-window-handle is only supported on Windows");
        return false;
#endif
    }
    // Let the window manager settle before the render engine measures it.
    for (int i = 0; i < 5; ++i) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    return true;
}

int CaptureApp::FindRenderEngine(CKPluginManager *pm, std::string &dllPath) const
{
    const int count = pm->GetPluginCount(CKPLUGIN_RENDERENGINE_DLL);
    int fallback = -1;
    for (int i = 0; i < count; ++i) {
        CKPluginEntry *entry = pm->GetPluginInfo(CKPLUGIN_RENDERENGINE_DLL, i);
        if (!entry)
            continue;
        CKPluginDll *dll = pm->GetPluginDllInfo(entry->m_PluginDllIndex);
        const std::string path = dll ? dll->m_DllFileName.CStr() : "";
        if (m_Options.Verbose)
            printf("render engine plugin %d: %s (%s)\n", i, path.c_str(), entry->m_PluginInfo.m_Description.CStr());
        if (fallback < 0) {
            fallback = i;
            dllPath = path;
        }
        if (EqualsNoCase(BaseNameNoExt(path), "CK2_3D")) {
            dllPath = path;
            return i;
        }
    }
    return fallback;
}

bool CaptureApp::InitEngine(const CaptureOptions &options)
{
    if (!options.SettingsIni.empty()) {
        // Consumed by CK2_3D (CKRenderSettings, getenv) when it loads
        // CK2_3D.ini. The engine DLL has its own C runtime, which copies the
        // process environment block when the DLL loads: set the variable on
        // the process (SetEnvironmentVariable), not only in SDL's runtime.
#ifdef _WIN32
        // Both: the Win32 block feeds C runtimes initialised later (static
        // CRT in a DLL), _putenv_s updates the shared UCRT copy getenv reads.
        SetEnvironmentVariableA("CKRE_SETTINGS_FILE", options.SettingsIni.c_str());
        _putenv_s("CKRE_SETTINGS_FILE", options.SettingsIni.c_str());
#else
        SDL_setenv_unsafe("CKRE_SETTINGS_FILE", options.SettingsIni.c_str(), 1);
#endif
    }

    if (CKStartUp() != CK_OK) {
        Fail("CKStartUp failed");
        return false;
    }
    m_CkStarted = true;

    CKPluginManager *pm = CKGetPluginManager();
    if (!pm) {
        Fail("CKGetPluginManager returned NULL");
        return false;
    }
    const int parsed = pm->ParsePlugins((CKSTRING)options.RenderEngineDir.c_str());
    if (options.Verbose)
        printf("ParsePlugins(%s) -> %d plugin dll(s)\n", options.RenderEngineDir.c_str(), parsed);

    std::string dllPath;
    const int renderEngine = FindRenderEngine(pm, dllPath);
    if (renderEngine < 0) {
        Fail("no render engine plugin found in " + options.RenderEngineDir +
             " (is CK2_3D.dll there, and does it load against this CK2.dll?)");
        return false;
    }
    m_RenderEngineDll = dllPath;

    CKERROR err = CKCreateContext(&m_Context, (WIN_HANDLE)m_WindowHandle, renderEngine, 0);
    if (err != CK_OK || !m_Context) {
        char buf[64];
        snprintf(buf, sizeof(buf), "CKCreateContext failed (%d)", (int)err);
        Fail(buf);
        return false;
    }
    m_Context->SetVirtoolsVersion(CK_VIRTOOLS_DEV, 0x2000043);
    m_Context->SetInterfaceMode(FALSE, LogRedirect, NULL);

    m_RenderManager = m_Context->GetRenderManager();
    if (!m_RenderManager) {
        Fail("render engine did not provide a render manager");
        return false;
    }
    m_DriverCount = m_RenderManager->GetRenderDriverCount();
    if (m_DriverCount <= 0) {
        Fail("render manager reports no drivers");
        return false;
    }
    if (options.Driver < 0 || options.Driver >= m_DriverCount) {
        char buf[96];
        snprintf(buf, sizeof(buf), "driver %d out of range (0..%d)", options.Driver, m_DriverCount - 1);
        Fail(buf);
        return false;
    }
    VxDriverDesc *desc = m_RenderManager->GetRenderDriverDescription(options.Driver);
    if (desc) {
        m_DriverName = desc->DriverName;
        if (options.Verbose) {
            for (int i = 0; i < m_DriverCount; ++i) {
                VxDriverDesc *d = m_RenderManager->GetRenderDriverDescription(i);
                if (d)
                    printf("driver %d: %s | %s | hardware=%d\n", i, d->DriverName, d->DriverDesc, (int)d->IsHardware);
            }
        }
    }

    CKRECT rect = {0, 0, options.Width, options.Height};
    m_RenderContext = m_RenderManager->CreateRenderContext(m_WindowHandle, options.Driver, &rect, FALSE, 32, -1, -1, 0);
    if (!m_RenderContext) {
        Fail("CreateRenderContext failed");
        return false;
    }
    m_RenderContext->SetClearBackground(TRUE);
    m_RenderContext->SetClearZBuffer(TRUE);

    // A level with its default scene and this render context as the player
    // context, as a loaded composition would have: 2D entities only render
    // when they are in the current scene and lay themselves out against the
    // player render context (SceneAddRenderObject adds the scene objects).
    CKLevel *level = static_cast<CKLevel *>(m_Context->CreateObject(CKCID_LEVEL, (CKSTRING)"Level", CK_OBJECTCREATION_NONAMECHECK));
    if (!level) {
        Fail("CreateObject(CKCID_LEVEL) failed");
        return false;
    }
    m_Context->SetCurrentLevel(level);
    level->AddRenderContext(m_RenderContext, TRUE);
    if (options.Verbose)
        printf("render context %dx%d on driver %d (%s)\n", m_RenderContext->GetWidth(),
               m_RenderContext->GetHeight(), options.Driver, m_DriverName.c_str());
    return true;
}

bool CaptureApp::WriteCapsJson(const std::string &path)
{
    if (!m_RenderManager) {
        Fail("WriteCapsJson before Boot");
        return false;
    }
    VxDriverDesc *d = m_RenderManager->GetRenderDriverDescription(m_Options.Driver);
    if (!d) {
        Fail("GetRenderDriverDescription returned NULL");
        return false;
    }
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        Fail("cannot write " + path);
        return false;
    }
    const Vx3DCapsDesc &c = d->Caps3D;
    const Vx2DCapsDesc &c2 = d->Caps2D;
    const std::string source = BaseNameNoExt(m_RenderEngineDll) + " | " + std::string(d->DriverName) + " | " +
                               std::string(d->DriverDesc) + " | " + TodayIso();
    fprintf(f, "{\n");
    fprintf(f, "  \"source\": \"%s\",\n", JsonEscape(source.c_str()).c_str());
    fprintf(f, "  \"renderEngineDll\": \"%s\",\n", JsonEscape(m_RenderEngineDll.c_str()).c_str());
    fprintf(f, "  \"driverIndex\": %d,\n", m_Options.Driver);
    fprintf(f, "  \"driverName\": \"%s\",\n", JsonEscape(d->DriverName).c_str());
    fprintf(f, "  \"driverDesc\": \"%s\",\n", JsonEscape(d->DriverDesc).c_str());
    fprintf(f, "  \"isHardware\": %s,\n", d->IsHardware ? "true" : "false");
    fprintf(f, "  \"caps3D\": {\n");
#define CAPS_FIELD(name, last) fprintf(f, "    \"" #name "\": %s%s\n", Hex(c.name).c_str(), last ? "" : ",")
    CAPS_FIELD(DevCaps, false);
    CAPS_FIELD(RenderBpps, false);
    CAPS_FIELD(ZBufferBpps, false);
    CAPS_FIELD(StencilBpps, false);
    CAPS_FIELD(StencilCaps, false);
    CAPS_FIELD(MinTextureWidth, false);
    CAPS_FIELD(MinTextureHeight, false);
    CAPS_FIELD(MaxTextureWidth, false);
    CAPS_FIELD(MaxTextureHeight, false);
    CAPS_FIELD(MaxClipPlanes, false);
    CAPS_FIELD(VertexCaps, false);
    CAPS_FIELD(MaxActiveLights, false);
    CAPS_FIELD(MaxNumberBlendStage, false);
    CAPS_FIELD(MaxNumberTextureStage, false);
    CAPS_FIELD(MaxTextureRatio, false);
    CAPS_FIELD(TextureFilterCaps, false);
    CAPS_FIELD(TextureAddressCaps, false);
    CAPS_FIELD(TextureCaps, false);
    CAPS_FIELD(MiscCaps, false);
    CAPS_FIELD(AlphaCmpCaps, false);
    CAPS_FIELD(ZCmpCaps, false);
    CAPS_FIELD(RasterCaps, false);
    CAPS_FIELD(SrcBlendCaps, false);
    CAPS_FIELD(DestBlendCaps, false);
    CAPS_FIELD(CKRasterizerSpecificCaps, true);
#undef CAPS_FIELD
    fprintf(f, "  },\n");
    fprintf(f, "  \"caps2D\": {\n");
    fprintf(f, "    \"Family\": %d,\n", (int)c2.Family);
    fprintf(f, "    \"MaxVideoMemory\": %s,\n", Hex(c2.MaxVideoMemory).c_str());
    fprintf(f, "    \"AvailableVideoMemory\": %s,\n", Hex(c2.AvailableVideoMemory).c_str());
    fprintf(f, "    \"Caps\": %s\n", Hex(c2.Caps).c_str());
    fprintf(f, "  },\n");
    fprintf(f, "  \"textureFormats\": [");
    for (int i = 0; i < d->TextureFormats.Size(); ++i) {
        const VX_PIXELFORMAT pf = VxImageDesc2PixelFormat(d->TextureFormats[i]);
        fprintf(f, "%s%d", i ? ", " : "", (int)pf);
    }
    fprintf(f, "],\n");
    fprintf(f, "  \"displayModeCount\": %d\n", d->DisplayModeCount);
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

bool CaptureApp::CaptureScene(const SceneDef &scene, RgbaImage &out)
{
    if (!m_RenderContext) {
        Fail("CaptureScene before Boot");
        return false;
    }
    SceneContext sc;
    sc.Context = m_Context;
    sc.RenderContext = m_RenderContext;
    sc.Width = m_Options.Width;
    sc.Height = m_Options.Height;
    sc.FrameCount = m_Options.Frames;
    sc.Verbose = m_Options.Verbose;

    if (!scene.Build(sc)) {
        Fail(std::string("scene '") + scene.Name + "' failed to build");
        return false;
    }
    if (sc.MainCamera)
        m_RenderContext->AttachViewpointToCamera(sc.MainCamera);

    int frames = m_Options.Frames < 1 ? 1 : m_Options.Frames;
    if (frames < scene.MinFrames)
        frames = scene.MinFrames; // e.g. dump_copy pastes the previous frame's dump
    sc.FrameCount = frames;
    for (int i = 0; i < frames; ++i) {
        sc.FrameIndex = i;
        SDL_PumpEvents();
        if (scene.PreFrame)
            scene.PreFrame(sc);
        CK_RENDER_FLAGS flags = CK_RENDER_DEFAULTSETTINGS;
        if (i == frames - 1 && !m_Options.PresentLastFrame)
            flags = (CK_RENDER_FLAGS)(flags & ~CK_RENDER_DOBACKTOFRONT);
        const CKERROR err = m_RenderContext->Render(flags);
        if (err != CK_OK) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Render failed on frame %d (%d)", i, (int)err);
            Fail(buf);
            return false;
        }
        if (scene.PostFrame)
            scene.PostFrame(sc);
    }
    return CaptureBackBuffer(out);
}

bool CaptureApp::CaptureBackBuffer(RgbaImage &out)
{
    VxImageDescEx desc;
    memset(&desc, 0, sizeof(desc));
    desc.Size = sizeof(desc);
    const int size = m_RenderContext->DumpToMemory(NULL, VXBUFFER_BACKBUFFER, desc);
    if (size <= 0) {
        Fail("DumpToMemory (size query) failed");
        return false;
    }
    std::vector<CKBYTE> buffer((size_t)size);
    desc.Image = buffer.data();
    const int written = m_RenderContext->DumpToMemory(NULL, VXBUFFER_BACKBUFFER, desc);
    if (written <= 0) {
        Fail("DumpToMemory (copy) failed");
        return false;
    }
    if (desc.Image != buffer.data())
        desc.Image = buffer.data();
    std::string error;
    if (!ConvertVxImageToRgba(desc, out, error)) {
        Fail("cannot convert captured image: " + error);
        return false;
    }
    if (m_Options.Verbose)
        printf("captured %dx%d, %d bpp, %d bytes\n", desc.Width, desc.Height, desc.BitsPerPixel, written);
    return true;
}

void CaptureApp::Shutdown()
{
    if (m_RenderContext && m_RenderManager) {
        m_RenderManager->DestroyRenderContext(m_RenderContext);
        m_RenderContext = NULL;
    }
    if (m_Context) {
        CKCloseContext(m_Context);
        m_Context = NULL;
    }
    if (m_CkStarted) {
        CKShutdown();
        m_CkStarted = false;
    }
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
        m_Window = NULL;
    }
    if (m_SdlInitialized) {
        SDL_Quit();
        m_SdlInitialized = false;
    }
}
