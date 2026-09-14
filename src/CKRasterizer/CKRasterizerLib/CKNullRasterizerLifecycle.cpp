#include "CKNullRasterizer.h"

#include "CKNullRasterizerInternal.h"
#include <cstring>
#include <new>

namespace {

void CKNullInitializeDriverCaps(
    XArray<VxDisplayMode> &displayModes,
    XClassArray<CKTextureDesc> &textureFormats,
    CKRasterizerNativeCapsDesc &caps)
{
    static const int resolutions[][2] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960},
        {1280, 1024}, {1366, 768}, {1600, 900}, {1920, 1080},
    };
    static const int bpps[] = {16, 32};
    for (const auto &resolution : resolutions) {
        for (int bpp : bpps) {
            VxDisplayMode mode;
            mode.Width = resolution[0];
            mode.Height = resolution[1];
            mode.Bpp = bpp;
            mode.RefreshRate = 60;
            displayModes.PushBack(mode);
        }
    }

    CKTextureDesc texture;
    texture.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texture.Format);
    textureFormats.PushBack(texture);

    caps = CKRasterizerNativeCapsDesc();
    caps.MaxTextureSize = 4096;
    caps.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    caps.MaxAnisotropy = 1;
    caps.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    caps.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
    caps.MaxMSAASamples = 1;
    caps.MaxPointSize = 1.0f;
    caps.MaxLights = CKRST_MAX_LIGHTS;
}

class CKNullRasterizerDriver final : public CKRasterizerDriver {
public:
    explicit CKNullRasterizerDriver(CKRasterizer *owner)
        : CKRasterizerDriver(owner, 0, "NULL Rasterizer", FALSE)
    {
        CKNullInitializeDriverCaps(
            m_DisplayModes, m_TextureFormats, m_NativeCaps);
        m_CapsFinal = TRUE;
    }

    ~CKNullRasterizerDriver() override
    {
        DestroyContexts();
    }

    CKRasterizerContext *CreateContext() override
    {
        CKRasterizerContext *context = CKNullCreateRasterizerContext(this);
        if (!context)
            return NULL;
        AddContext(context);
        return context;
    }
};

class CKNullRasterizer final : public CKRasterizer {
public:
    CKBOOL Start(WIN_HANDLE appWindow) override
    {
        if (GetDriverCount() != 0)
            return TRUE;
        CKRasterizer::Start(appWindow);
        CKNullRasterizerDriver *driver =
            new (std::nothrow) CKNullRasterizerDriver(this);
        if (!driver)
            return FALSE;
        AddDriver(driver);
        return TRUE;
    }
};

CKRasterizer *CKNullRasterizerStart(WIN_HANDLE appWindow)
{
    CKNullRasterizer *rasterizer = new (std::nothrow) CKNullRasterizer();
    if (!rasterizer)
        return NULL;
    if (!rasterizer->Start(appWindow)) {
        delete rasterizer;
        return NULL;
    }
    return rasterizer;
}

void CKNullRasterizerClose(CKRasterizer *rasterizer)
{
    delete rasterizer;
}

} // namespace

void CKNullRasterizerGetInfo(CKRasterizerInfo *info)
{
    if (!info)
        return;
    info->DllName = "CK2_3D";
    info->Desc = "NULL Rasterizer";
    info->DllInstance = NULL;
    info->StartFct = CKNullRasterizerStart;
    info->CloseFct = CKNullRasterizerClose;
    info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
