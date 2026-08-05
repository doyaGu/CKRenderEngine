#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"

#include <SDL3/SDL.h>

#include <new>

static void AddDisplayMode(XArray<VxDisplayMode> &displayModes, int width, int height, int bpp, int refreshRate)
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

static void AddDisplayMode(XArray<VxDisplayMode> &displayModes, const SDL_DisplayMode *mode)
{
    if (!mode)
        return;

    int refreshRate = mode->refresh_rate > 0.0f ? (int)(mode->refresh_rate + 0.5f) : 60;
    AddDisplayMode(displayModes, mode->w, mode->h, 32, refreshRate);
}

static void AddCompatibleDisplayMode(XArray<VxDisplayMode> &displayModes, int width, int height)
{
    AddDisplayMode(displayModes, width, height, 32, 60);
}

static int CompareDisplayModes(const void *lhs, const void *rhs)
{
    const VxDisplayMode &a = *(const VxDisplayMode *)lhs;
    const VxDisplayMode &b = *(const VxDisplayMode *)rhs;
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

CKBgfxRasterizerDriver::CKBgfxRasterizerDriver(CKBgfxRasterizer *owner)
{
    m_Owner = owner;
    m_Hardware = TRUE;
    m_CapsUpToDate = FALSE;
    m_DriverIndex = 0;
    m_Desc = "bgfx Driver";

    int displayCount = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
    for (int displayIndex = 0; displays && displayIndex < displayCount; ++displayIndex)
    {
        AddDisplayMode(m_DisplayModes, SDL_GetCurrentDisplayMode(displays[displayIndex]));
        AddDisplayMode(m_DisplayModes, SDL_GetDesktopDisplayMode(displays[displayIndex]));

        int modeCount = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(displays[displayIndex], &modeCount);
        for (int modeIndex = 0; modes && modeIndex < modeCount; ++modeIndex)
            AddDisplayMode(m_DisplayModes, modes[modeIndex]);
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
    // Windowed contexts are not limited to the display's exclusive fullscreen
    // modes. Keep the legacy resolutions selectable even on Retina displays,
    // which commonly omit 640x480 and 800x600 from the SDL mode list.
    for (int i = 0;
         i < (int)(sizeof(fallbackResolutions) / sizeof(fallbackResolutions[0]));
         ++i) {
        AddCompatibleDisplayMode(m_DisplayModes,
                                 fallbackResolutions[i][0],
                                 fallbackResolutions[i][1]);
    }

    m_DisplayModes.Sort(CompareDisplayModes);

    CKTextureDesc texDesc;
    texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texDesc.Format);
    m_TextureFormats.PushBack(texDesc);

    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    m_3DCaps.MinTextureWidth = 1;
    m_3DCaps.MinTextureHeight = 1;
    m_3DCaps.MaxTextureWidth = 16384;
    m_3DCaps.MaxTextureHeight = 16384;
    m_3DCaps.MaxTextureRatio = 16384;
    m_3DCaps.MaxClipPlanes = 6;
    m_3DCaps.MaxActiveLights = 8;
    m_3DCaps.MaxNumberBlendStage = 8;
    m_3DCaps.MaxNumberTextureStage = 8;
    m_3DCaps.TextureFilterCaps = CKRST_TFILTERCAPS_NEAREST
                               | CKRST_TFILTERCAPS_LINEAR
                               | CKRST_TFILTERCAPS_MIPNEAREST
                               | CKRST_TFILTERCAPS_MIPLINEAR
                               | CKRST_TFILTERCAPS_LINEARMIPNEAREST
                               | CKRST_TFILTERCAPS_LINEARMIPLINEAR
                               | CKRST_TFILTERCAPS_ANISOTROPIC;
    m_3DCaps.TextureAddressCaps = CKRST_TADDRESSCAPS_WRAP
                                | CKRST_TADDRESSCAPS_MIRROR
                                | CKRST_TADDRESSCAPS_CLAMP
                                | CKRST_TADDRESSCAPS_BORDER
                                | CKRST_TADDRESSCAPS_INDEPENDENTUV;
    m_3DCaps.CKRasterizerSpecificCaps = CKRST_SPECIFICCAPS_CANDOVERTEXBUFFER
                                      | CKRST_SPECIFICCAPS_CANDOINDEXBUFFER
                                      | CKRST_SPECIFICCAPS_COPYTEXTURE
                                      | CKRST_SPECIFICCAPS_HARDWARETL
                                      | CKRST_SPECIFICCAPS_SUPPORTSHADERS;

    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    m_2DCaps.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D;
#if defined(_WIN32)
    m_2DCaps.Caps |= CKRST_2DCAPS_GDI;
#endif

    m_CapsUpToDate = FALSE;
}

CKBgfxRasterizerDriver::~CKBgfxRasterizerDriver()
{
    for (int i = 0; i < m_Contexts.Size(); ++i) {
        CKBgfxRasterizerContext *context =
            static_cast<CKBgfxRasterizerContext *>(m_Contexts[i]);
        context->BeginForcedShutdown();
        context->EndCurrentThreadEncoders();
        if (!context->IsIdle())
            CKBgfxLogf("Shutdown", "waiting for active encoder threads");
        const Uint64 waitStart = SDL_GetTicks();
        while (!context->IsIdle() &&
               SDL_GetTicks() - waitStart < 30000u)
            SDL_Delay(1);
        if (!context->IsIdle()) {
            CKBgfxLogf("Shutdown",
                       "timed out waiting for active encoders; preserving disabled context");
            continue;
        }
        delete m_Contexts[i];
    }
    m_Contexts.Clear();
}

CKRasterizerContext *CKBgfxRasterizerDriver::CreateContext()
{
    if (m_Contexts.Size() != 0) {
        CKBgfxLogf("Init", "multiple bgfx rasterizer contexts are unsupported");
        return NULL;
    }
    auto *ctx = new (std::nothrow) CKBgfxRasterizerContext(this);
    if (!ctx)
        return NULL;

    m_Contexts.PushBack(ctx);
    return ctx;
}

CKBOOL CKBgfxRasterizerDriver::DestroyContext(CKRasterizerContext *Context)
{
    if (!Context)
        return FALSE;

    for (int i = 0; i < m_Contexts.Size(); ++i)
    {
        if (m_Contexts[i] == Context)
        {
            CKBgfxRasterizerContext *bgfxContext =
                static_cast<CKBgfxRasterizerContext *>(Context);
            if (!bgfxContext->TryBeginShutdown())
                return FALSE;
            delete m_Contexts[i];
            m_Contexts.RemoveAt(i);
            return TRUE;
        }
    }
    return FALSE;
}
