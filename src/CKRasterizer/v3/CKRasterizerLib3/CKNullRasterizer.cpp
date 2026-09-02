#include "CKNullRasterizer.h"
#include "CKRasterizerCapsBaseline.h"

#include <string.h>

// ===========================================================================
// CKNullRasterizer
// ===========================================================================

CKNullRasterizer::CKNullRasterizer() = default;

CKNullRasterizer::~CKNullRasterizer()
{
    Close();
}

CKBOOL CKNullRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    if (m_Drivers.Size() != 0)
        return TRUE;
    CKNullRasterizerDriver *driver = new CKNullRasterizerDriver;
    driver->InitCaps(this, 0);
    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKNullRasterizer::Close()
{
    for (CKRasterizerDriver **it = m_Drivers.Begin(); it != m_Drivers.End(); ++it)
        delete *it;
    m_Drivers.Clear();
}

// ===========================================================================
// CKNullRasterizerDriver
// ===========================================================================

CKNullRasterizerDriver::CKNullRasterizerDriver() = default;

CKNullRasterizerDriver::~CKNullRasterizerDriver()
{
    for (CKRasterizerContext **it = m_Contexts.Begin(); it != m_Contexts.End(); ++it)
        delete *it;
    m_Contexts.Clear();
}

CKRasterizerContext *CKNullRasterizerDriver::CreateContext()
{
    CKNullRasterizerContext *context = new CKNullRasterizerContext;
    context->m_Driver = this;
    m_Contexts.PushBack(context);
    return context;
}

CKBOOL CKNullRasterizerDriver::DestroyContext(CKRasterizerContext *Context)
{
    if (!Context)
        return FALSE;
    for (int i = 0; i < m_Contexts.Size(); ++i) {
        if (m_Contexts[i] != Context)
            continue;
        if (!Context->IsIdle())
            return FALSE;
        Context->BeginShutdown();
        delete m_Contexts[i];
        m_Contexts.RemoveAt(i);
        return TRUE;
    }
    return FALSE;
}

void CKNullRasterizerDriver::InitCaps(CKRasterizer *Owner, CKDWORD Index)
{
    m_Owner = Owner;
    m_DriverIndex = Index;
    m_Desc = "NULL Rasterizer";
    m_Hardware = FALSE;
    m_CapsUpToDate = TRUE;

    m_DisplayModes.Clear();
    static const int resolutions[][2] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960},
        {1280, 1024}, {1366, 768}, {1600, 900}, {1920, 1080},
    };
    static const int bpps[] = {16, 32};
    for (int i = 0; i < (int)(sizeof(resolutions) / sizeof(resolutions[0])); ++i) {
        for (int j = 0; j < (int)(sizeof(bpps) / sizeof(bpps[0])); ++j) {
            VxDisplayMode mode;
            mode.Width = resolutions[i][0];
            mode.Height = resolutions[i][1];
            mode.Bpp = bpps[j];
            mode.RefreshRate = 60;
            m_DisplayModes.PushBack(mode);
        }
    }

    m_TextureFormats.Clear();
    CKTextureDesc format;
    format.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, format.Format);
    m_TextureFormats.PushBack(format);

    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    if (!CKRSTGetCapsBaseline(&m_3DCaps, &m_2DCaps)) {
        // No captured baseline compiled in: minimal caps so the contract
        // tests and the engine can still run against the NULL rasterizer.
        m_3DCaps.MinTextureWidth = 1;
        m_3DCaps.MinTextureHeight = 1;
        m_3DCaps.MaxTextureWidth = 16384;
        m_3DCaps.MaxTextureHeight = 16384;
        m_3DCaps.MaxClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
        m_3DCaps.MaxActiveLights = CKRST_MAX_LIGHTS;
        m_3DCaps.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        m_3DCaps.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        m_3DCaps.MaxTextureRatio = 0;
        m_3DCaps.RenderBpps = VX_BPP16 | VX_BPP32;
        m_3DCaps.ZBufferBpps = VX_BPP16 | VX_BPP24 | VX_BPP32;
        m_3DCaps.StencilBpps = VX_BPP8;
        m_2DCaps.Family = CKRST_DIRECTX;
        m_2DCaps.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D;
    }
    // The NULL rasterizer is software; report it as such regardless of the
    // baseline's hardware bits.
    m_3DCaps.CKRasterizerSpecificCaps &= ~(XDWORD)(CKRST_SPECIFICCAPS_HARDWARE | CKRST_SPECIFICCAPS_HARDWARETL);
    m_3DCaps.CKRasterizerSpecificCaps |= CKRST_SPECIFICCAPS_SOFTWARE;
}

// ===========================================================================
// Entry points
// ===========================================================================

CKRasterizer *CKNullRasterizerStart(WIN_HANDLE AppWnd)
{
    CKNullRasterizer *rasterizer = new CKNullRasterizer;
    if (!rasterizer->Start(AppWnd)) {
        delete rasterizer;
        return NULL;
    }
    return rasterizer;
}

void CKNullRasterizerClose(CKRasterizer *Rasterizer)
{
    if (!Rasterizer)
        return;
    Rasterizer->Close();
    delete Rasterizer;
}

void CKNullRasterizerGetInfo(CKRasterizerInfo *Info)
{
    if (!Info)
        return;
    Info->Desc = "NULL Rasterizer";
    Info->StartFct = CKNullRasterizerStart;
    Info->CloseFct = CKNullRasterizerClose;
    Info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
