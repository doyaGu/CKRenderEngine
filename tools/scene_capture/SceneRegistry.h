#ifndef CKRE_SCENE_CAPTURE_SCENEREGISTRY_H
#define CKRE_SCENE_CAPTURE_SCENEREGISTRY_H

#include "CKAll.h"
#include <string>

struct RgbaImage;

// State shared between the capture loop and a scene. One scene runs per
// process, so scene files may keep static state that Build() resets.
struct SceneContext {
    CKContext *Context = NULL;
    CKRenderContext *RenderContext = NULL;
    int Width = 0;
    int Height = 0;
    int FrameIndex = 0;   // 0-based index of the frame about to be rendered
    int FrameCount = 0;   // total frames rendered before the capture
    CKCamera *MainCamera = NULL; // set by Build(); restored after RTT passes
    bool Verbose = false;
    std::string Error; // callback failures must fail capture, even if Render succeeds
};

struct SceneDef {
    const char *Name;
    const char *Description;
    bool (*Build)(SceneContext &sc);        // called once before the first frame
    void (*PreFrame)(SceneContext &sc);     // optional, outside Render (RTT passes, uploads)
    void (*PostFrame)(SceneContext &sc);    // optional, outside Render after the frame
    bool HasOracle;                         // the original DX8 rasterizer can render it
    int Threshold;                          // suggested per-channel oracle threshold (0..255)
    float MinPass;                          // suggested oracle pass ratio (0..1)
    const char *IniOverrides;               // CK2_3D.ini <Render> lines, or NULL (present_* scenes)
    int MinFrames;                          // frames the scene needs before its capture is meaningful (0 = 1)
    bool (*ValidateImage)(SceneContext &sc, const RgbaImage &image) = NULL; // optional functional pixel assertions
};

int GetSceneCount();
const SceneDef &GetScene(int index);
const SceneDef *FindScene(const char *name);

// Scene tables provided by the scene source files.
extern const SceneDef g_Scenes3D[];
extern const int g_Scenes3DCount;
extern const SceneDef g_Scenes2D[];
extern const int g_Scenes2DCount;
extern const SceneDef g_ScenesRtt[];
extern const int g_ScenesRttCount;
extern const SceneDef g_ScenesStencil[];
extern const int g_ScenesStencilCount;
extern const SceneDef g_ScenesJit[];
extern const int g_ScenesJitCount;

#endif // CKRE_SCENE_CAPTURE_SCENEREGISTRY_H
