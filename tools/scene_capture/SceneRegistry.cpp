#include "SceneRegistry.h"

#include <string.h>
#include <vector>

namespace {

const std::vector<const SceneDef *> &AllScenes()
{
    static std::vector<const SceneDef *> scenes;
    if (scenes.empty()) {
        for (int i = 0; i < g_Scenes3DCount; ++i) scenes.push_back(&g_Scenes3D[i]);
        for (int i = 0; i < g_Scenes2DCount; ++i) scenes.push_back(&g_Scenes2D[i]);
        for (int i = 0; i < g_ScenesRttCount; ++i) scenes.push_back(&g_ScenesRtt[i]);
        for (int i = 0; i < g_ScenesStencilCount; ++i) scenes.push_back(&g_ScenesStencil[i]);
    }
    return scenes;
}

} // namespace

int GetSceneCount()
{
    return (int)AllScenes().size();
}

const SceneDef &GetScene(int index)
{
    return *AllScenes()[(size_t)index];
}

const SceneDef *FindScene(const char *name)
{
    if (!name)
        return NULL;
    for (const SceneDef *scene : AllScenes())
        if (strcmp(scene->Name, name) == 0)
            return scene;
    return NULL;
}
