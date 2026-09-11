#include "CKRasterizerDriverCaps.h"

#include "CKRasterizerCapsBaseline.h"

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

void InitializeFallbackCaps(Vx3DCapsDesc &caps3D, Vx2DCapsDesc &caps2D)
{
    caps3D.MinTextureWidth = 1;
    caps3D.MinTextureHeight = 1;
    caps3D.MaxTextureWidth = 16384;
    caps3D.MaxTextureHeight = 16384;
    caps3D.MaxTextureRatio = 16384;
    caps3D.MaxClipPlanes = 6;
    caps3D.MaxActiveLights = 8;
    caps3D.MaxNumberBlendStage = 8;
    caps3D.MaxNumberTextureStage = 8;
    caps3D.TextureFilterCaps = CKRST_TFILTERCAPS_NEAREST |
                              CKRST_TFILTERCAPS_LINEAR |
                              CKRST_TFILTERCAPS_MIPNEAREST |
                              CKRST_TFILTERCAPS_MIPLINEAR |
                              CKRST_TFILTERCAPS_LINEARMIPNEAREST |
                              CKRST_TFILTERCAPS_LINEARMIPLINEAR |
                              CKRST_TFILTERCAPS_ANISOTROPIC;
    caps3D.TextureAddressCaps = CKRST_TADDRESSCAPS_WRAP |
                               CKRST_TADDRESSCAPS_MIRROR |
                               CKRST_TADDRESSCAPS_CLAMP |
                               CKRST_TADDRESSCAPS_BORDER |
                               CKRST_TADDRESSCAPS_INDEPENDENTUV;
    caps3D.RasterCaps = CKRST_RASTERCAPS_FOGVERTEX |
                        CKRST_RASTERCAPS_FOGPIXEL |
                        CKRST_RASTERCAPS_FOGRANGE |
                        CKRST_RASTERCAPS_ZTEST;
    caps3D.CKRasterizerSpecificCaps = CKRST_SPECIFICCAPS_CANDOVERTEXBUFFER |
                                      CKRST_SPECIFICCAPS_CANDOINDEXBUFFER |
                                      CKRST_SPECIFICCAPS_COPYTEXTURE |
                                      CKRST_SPECIFICCAPS_HARDWARETL |
                                      CKRST_SPECIFICCAPS_DX8;
    caps2D.Family = CKRST_DIRECTX;
    caps2D.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D;
}

} // namespace

void CKRSTInitializeDriverCaps(CKRasterizerDriver *driver)
{
    if (!driver)
        return;

    driver->m_Hardware = TRUE;
    driver->m_CapsUpToDate = FALSE;
    driver->m_DriverIndex = 0;

    int displayCount = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
    for (int displayIndex = 0; displays && displayIndex < displayCount; ++displayIndex) {
        AddDisplayMode(driver->m_DisplayModes, SDL_GetCurrentDisplayMode(displays[displayIndex]));
        AddDisplayMode(driver->m_DisplayModes, SDL_GetDesktopDisplayMode(displays[displayIndex]));

        int modeCount = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(displays[displayIndex], &modeCount);
        for (int modeIndex = 0; modes && modeIndex < modeCount; ++modeIndex)
            AddDisplayMode(driver->m_DisplayModes, modes[modeIndex]);
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
        AddDisplayMode(driver->m_DisplayModes, fallbackResolutions[i][0],
                       fallbackResolutions[i][1], 32, 60);
    }
    driver->m_DisplayModes.Sort(CompareDisplayModes);

    CKTextureDesc texture;
    texture.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texture.Format);
    driver->m_TextureFormats.PushBack(texture);

    memset(&driver->m_3DCaps, 0, sizeof(driver->m_3DCaps));
    memset(&driver->m_2DCaps, 0, sizeof(driver->m_2DCaps));
    if (!CKRSTGetCapsBaseline(&driver->m_3DCaps, &driver->m_2DCaps))
        InitializeFallbackCaps(driver->m_3DCaps, driver->m_2DCaps);

    Vx3DCapsDesc limits;
    memset(&limits, 0, sizeof(limits));
    limits.MaxClipPlanes = 6;
    limits.MaxActiveLights = 8;
    limits.MaxNumberBlendStage = 8;
    limits.MaxNumberTextureStage = 8;
    CKRSTLowerCapsToLimits(&driver->m_3DCaps, &limits);
#if defined(_WIN32)
    driver->m_2DCaps.Caps |= CKRST_2DCAPS_GDI;
#endif

    driver->m_CapsUpToDate = FALSE;
}

