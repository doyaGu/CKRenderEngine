#include "CKNullRasterizer.h"

#include "CKNullRasterizerInternal.h"
#include "CKRasterizerCapsBaseline.h"

#include <cstring>
#include <new>

namespace {

void CKNullInitializeDriverCaps(CKRasterizerDriver &driver)
{
    driver.m_Desc = "NULL Rasterizer";
    driver.m_Hardware = FALSE;
    driver.m_CapsUpToDate = TRUE;

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
            driver.m_DisplayModes.PushBack(mode);
        }
    }

    CKTextureDesc texture;
    texture.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texture.Format);
    driver.m_TextureFormats.PushBack(texture);

    std::memset(&driver.m_3DCaps, 0, sizeof(driver.m_3DCaps));
    std::memset(&driver.m_2DCaps, 0, sizeof(driver.m_2DCaps));
    if (!CKRSTGetCapsBaseline(&driver.m_3DCaps, &driver.m_2DCaps)) {
        driver.m_3DCaps.MinTextureWidth = driver.m_3DCaps.MinTextureHeight = 1;
        driver.m_3DCaps.MaxTextureWidth = driver.m_3DCaps.MaxTextureHeight = 4096;
        driver.m_3DCaps.MaxTextureRatio = 4096;
        driver.m_3DCaps.MaxClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
        driver.m_3DCaps.MaxActiveLights = CKRST_MAX_LIGHTS;
        driver.m_3DCaps.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        driver.m_3DCaps.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        driver.m_2DCaps.Caps =
            CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D | CKRST_2DCAPS_GDI;
    }
    driver.m_3DCaps.CKRasterizerSpecificCaps &=
        ~(CKRST_SPECIFICCAPS_HARDWARE | CKRST_SPECIFICCAPS_HARDWARETL);
    driver.m_3DCaps.CKRasterizerSpecificCaps |= CKRST_SPECIFICCAPS_SOFTWARE;
}

class CKNullRasterizerDriver final : public CKRasterizerDriver {
public:
    explicit CKNullRasterizerDriver(CKRasterizer *owner)
    {
        m_Owner = owner;
        m_DriverIndex = 0;
        CKNullInitializeDriverCaps(*this);
    }

    ~CKNullRasterizerDriver() override
    {
        while (m_Contexts.Size() > 0)
            DestroyContext(m_Contexts[m_Contexts.Size() - 1]);
    }

    CKRasterizerContext *CreateContext() override
    {
        CKRasterizerContext *context = CKNullCreateRasterizerContext(this);
        if (!context)
            return NULL;
        m_Contexts.PushBack(context);
        return context;
    }

    CKBOOL DestroyContext(CKRasterizerContext *context) override
    {
        if (!context)
            return FALSE;
        for (int i = 0; i < m_Contexts.Size(); ++i) {
            if (m_Contexts[i] != context)
                continue;
            if (!CKNullDestroyRasterizerContext(context))
                return FALSE;
            m_Contexts.RemoveAt(i);
            return TRUE;
        }
        return FALSE;
    }
};

class CKNullRasterizer final : public CKRasterizer {
public:
    ~CKNullRasterizer() override
    {
        Close();
    }

    CKBOOL Start(WIN_HANDLE appWindow) override
    {
        m_MainWindow = appWindow;
        if (m_Drivers.Size() > 0)
            return TRUE;
        CKNullRasterizerDriver *driver =
            new (std::nothrow) CKNullRasterizerDriver(this);
        if (!driver)
            return FALSE;
        m_Drivers.PushBack(driver);
        return TRUE;
    }

    void Close() override
    {
        for (int i = 0; i < m_Drivers.Size(); ++i)
            delete static_cast<CKNullRasterizerDriver *>(m_Drivers[i]);
        m_Drivers.Clear();
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
