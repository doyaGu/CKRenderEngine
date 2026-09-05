#ifndef CKRE_SCENE_CAPTURE_CAPTUREAPP_H
#define CKRE_SCENE_CAPTURE_CAPTUREAPP_H

#include <string>
#include <vector>

#include "ImageIO.h"
#include "SceneRegistry.h"

struct SDL_Window;
class CKPluginManager;

struct CaptureOptions {
    std::string RenderEngineDir;
    int Driver = 0;
    std::string Rasterizer;
    int HoldMilliseconds = 0;
    int Frames = 1;
    int Width = 640;
    int Height = 480;
    bool NativeWindowHandle = false;
    bool HiddenWindow = false;
    bool Verbose = false;
    std::string SettingsIni;     // CKRE_SETTINGS_FILE override for our engine
    std::string ProfileJson;     // optional per-scene CPU wall-time report
    int ProfileWarmup = 1;       // excludes first-render shader/pipeline creation
    bool ProfileInteractiveStart = false;
    // Present the last frame before reading the back buffer. Our engine needs
    // the frame to be complete (DumpToMemory requires an idle rasterizer);
    // swap chains with discard semantics may need this off.
    bool PresentLastFrame = true;
};

// Owns the SDL window, the CK context and the render context for one scene.
class CaptureApp {
public:
    CaptureApp();
    ~CaptureApp();

    // Boots SDL + CK2 + the render engine found in RenderEngineDir. On failure
    // Error() describes why. Nothing is rendered yet.
    bool Boot(const CaptureOptions &options);

    // Writes the selected driver's VxDriverDesc as JSON. Requires Boot().
    bool WriteCapsJson(const std::string &path);

    // Builds and renders the scene, then captures the back buffer.
    bool CaptureScene(const SceneDef &scene, RgbaImage &out);
    void HoldVisibleFrame();

    // Reports the engine that was actually loaded (DLL path, driver name).
    const std::string &RenderEngineDll() const { return m_RenderEngineDll; }
    const std::string &DriverName() const { return m_DriverName; }
    int DriverCount() const { return m_DriverCount; }
    const std::string &Error() const { return m_Error; }
    bool Cancelled() const { return m_Cancelled; }

    void Shutdown();

private:
    bool InitWindow(const CaptureOptions &options);
    bool InitEngine(const CaptureOptions &options);
    int FindRenderEngine(CKPluginManager *pm, std::string &dllPath) const;
    bool CaptureBackBuffer(RgbaImage &out);
    bool WriteProfileJson(const SceneDef &scene);
    bool WaitForProfileStart(const SceneDef &scene);
    void Fail(const std::string &message);

    CaptureOptions m_Options;
    SDL_Window *m_Window = NULL;
    void *m_WindowHandle = NULL;
    CKContext *m_Context = NULL;
    CKRenderManager *m_RenderManager = NULL;
    CKRenderContext *m_RenderContext = NULL;
    std::string m_RenderEngineDll;
    std::string m_DriverName;
    int m_DriverCount = 0;
    std::string m_Error;
    bool m_SdlInitialized = false;
    bool m_CkStarted = false;
    bool m_Cancelled = false;
    struct ProfileFrame {
        double RenderMilliseconds = 0;
        int Width = 0, Height = 0;
        bool HasRasterizerStats = false;
        bool WindowFocused = false;
        unsigned DrawCalls = 0, Primitives = 0, Passes = 0, Clears = 0;
        unsigned TextureUploads = 0, BufferUploads = 0;
    };
    std::vector<ProfileFrame> m_ProfileFrames;
    double m_ProfileTickMilliseconds = 0;
    double m_ProfileEngineInitMilliseconds = 0;
    double m_ProfileContextCreateMilliseconds = 0;
    double m_ProfileSceneBuildMilliseconds = 0;
};

#endif // CKRE_SCENE_CAPTURE_CAPTUREAPP_H
