#include "CKRasterizerDriverCaps.h"

#include <SDL3/SDL.h>

#include <string.h>

namespace {

void AddDisplayMode(XArray<VxDisplayMode> &displayModes, int width, int height,
                    int bpp, int refreshRate)
{
    if (refreshRate <= 0)
        refreshRate = 60;

    VxDisplayMode mode;
    mode.Width = width;
    mode.Height = height;
    mode.Bpp = bpp;
    mode.RefreshRate = refreshRate;
    if (!displayModes.IsHere(mode))
        displayModes.PushBack(mode);
}

void AddDisplayMode(XArray<VxDisplayMode> &displayModes, const SDL_DisplayMode *mode)
{
    if (!mode)
        return;

    const int refreshRate =
        mode->refresh_rate > 0.0f ? static_cast<int>(mode->refresh_rate + 0.5f) : 60;
    AddDisplayMode(displayModes, mode->w, mode->h, 32, refreshRate);
}

int CompareDisplayModes(const void *lhs, const void *rhs)
{
    const VxDisplayMode &a = *static_cast<const VxDisplayMode *>(lhs);
    const VxDisplayMode &b = *static_cast<const VxDisplayMode *>(rhs);
    if (a.Width != b.Width)
        return a.Width < b.Width ? -1 : 1;
    if (a.Height != b.Height)
        return a.Height < b.Height ? -1 : 1;
    if (a.Bpp != b.Bpp)
        return a.Bpp < b.Bpp ? -1 : 1;
    if (a.RefreshRate != b.RefreshRate)
        return a.RefreshRate < b.RefreshRate ? -1 : 1;
    return 0;
}

} // namespace

void CKRSTInitializeDriverCaps(
    XArray<VxDisplayMode> &displayModes,
    XClassArray<CKTextureDesc> &textureFormats,
    CKRasterizerNativeCapsDesc &caps)
{
    int displayCount = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
    for (int displayIndex = 0; displays && displayIndex < displayCount; ++displayIndex) {
        AddDisplayMode(displayModes, SDL_GetCurrentDisplayMode(displays[displayIndex]));
        AddDisplayMode(displayModes, SDL_GetDesktopDisplayMode(displays[displayIndex]));

        int modeCount = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(displays[displayIndex], &modeCount);
        for (int modeIndex = 0; modes && modeIndex < modeCount; ++modeIndex)
            AddDisplayMode(displayModes, modes[modeIndex]);
        SDL_free(modes);
    }
    SDL_free(displays);

    static const int fallbackResolutions[][2] = {
        {640, 480},
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1920, 1080},
    };
    for (int i = 0;
         i < static_cast<int>(sizeof(fallbackResolutions) / sizeof(fallbackResolutions[0]));
         ++i) {
        AddDisplayMode(displayModes, fallbackResolutions[i][0],
                       fallbackResolutions[i][1], 32, 60);
    }
    displayModes.Sort(CompareDisplayModes);

    CKTextureDesc texture;
    texture.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texture.Format);
    textureFormats.PushBack(texture);

    caps = CKRasterizerNativeCapsDesc();
    caps.MaxTextureSize = 4096;
    caps.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    caps.MaxAnisotropy = 1;
    caps.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    caps.MaxVertexBlendMatrices = 4;
    caps.MaxMSAASamples = 1;
    caps.MaxPointSize = 1.0f;
    caps.MaxLights = CKRST_MAX_LIGHTS;
}
