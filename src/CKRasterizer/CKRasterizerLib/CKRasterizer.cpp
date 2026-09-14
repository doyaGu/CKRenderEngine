#include "CKRasterizer.h"

#include <string.h>

CKRasterizer::CKRasterizer()
    : m_MainWindow(NULL)
{
}

CKRasterizer::~CKRasterizer()
{
    Close();
}

CKBOOL CKRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    return TRUE;
}

void CKRasterizer::Close()
{
    while (m_Drivers.Size() > 0) {
        delete m_Drivers[m_Drivers.Size() - 1];
        m_Drivers.PopBack();
    }
    m_MainWindow = NULL;
}

int CKRasterizer::GetDriverCount() const
{
    return m_Drivers.Size();
}

CKRasterizerDriver *CKRasterizer::GetDriver(CKDWORD Index) const
{
    return Index < (CKDWORD)m_Drivers.Size() ? m_Drivers[Index] : NULL;
}

void CKRasterizer::AddDriver(CKRasterizerDriver *Driver)
{
    if (Driver)
        m_Drivers.PushBack(Driver);
}

CKRasterizerDriver::CKRasterizerDriver(CKRasterizer *Owner, CKDWORD DriverIndex,
                                       CKSTRING Description, CKBOOL Hardware)
    : m_Hardware(Hardware), m_CapsFinal(FALSE), m_Owner(Owner),
      m_DriverIndex(DriverIndex), m_Description(Description ? Description : "")
{
}

CKRasterizerDriver::~CKRasterizerDriver()
{
    DestroyContexts();
}

CKBOOL CKRasterizerDriver::GetDesc(CKRasterizerDriverDesc *Desc) const
{
    if (!Desc)
        return FALSE;
    Desc->DriverIndex = m_DriverIndex;
    Desc->Hardware = m_Hardware;
    Desc->CapsFinal = m_CapsFinal;
    Desc->Description = m_Description;
    return TRUE;
}

int CKRasterizerDriver::GetDisplayModeCount() const
{
    return m_DisplayModes.Size();
}

CKBOOL CKRasterizerDriver::GetDisplayMode(CKDWORD Index, VxDisplayMode *Mode) const
{
    if (!Mode || Index >= (CKDWORD)m_DisplayModes.Size())
        return FALSE;
    *Mode = m_DisplayModes[Index];
    return TRUE;
}

int CKRasterizerDriver::GetTextureFormatCount() const
{
    return m_TextureFormats.Size();
}

CKBOOL CKRasterizerDriver::GetTextureFormat(CKDWORD Index, CKTextureDesc *Desc) const
{
    if (!Desc || Index >= (CKDWORD)m_TextureFormats.Size())
        return FALSE;
    *Desc = m_TextureFormats[Index];
    return TRUE;
}

CKBOOL CKRasterizerDriver::GetNativeCaps(CKRasterizerNativeCapsDesc *Caps) const
{
    if (!Caps)
        return FALSE;
    *Caps = m_NativeCaps;
    return TRUE;
}

CKRasterizerContext *CKRasterizerDriver::CreateContext()
{
    return NULL;
}

CKBOOL CKRasterizerDriver::DestroyContext(CKRasterizerContext *Context)
{
    if (!Context)
        return FALSE;
    for (int Index = 0; Index < m_Contexts.Size(); ++Index) {
        if (m_Contexts[Index] != Context)
            continue;
        if (!Context->BeginShutdown() || !Context->IsIdle())
            return FALSE;
        delete Context;
        m_Contexts.RemoveAt(Index);
        return TRUE;
    }
    return FALSE;
}

void CKRasterizerDriver::AddContext(CKRasterizerContext *Context)
{
    if (Context)
        m_Contexts.PushBack(Context);
}

void CKRasterizerDriver::DestroyContexts()
{
    while (m_Contexts.Size() > 0) {
        CKRasterizerContext *Context = m_Contexts[m_Contexts.Size() - 1];
        Context->BeginShutdown();
        delete Context;
        m_Contexts.PopBack();
    }
}

CKRasterizerContext::CKRasterizerContext(CKRasterizerDriver *Driver)
    : m_Driver(Driver), m_PosX(0), m_PosY(0), m_Width(0), m_Height(0),
      m_Bpp(0), m_ZBpp(0), m_StencilBpp(0), m_Fullscreen(FALSE),
      m_RefreshRate(0), m_Window(NULL), m_Created(FALSE),
      m_ShuttingDown(FALSE), m_FrameNumber(0)
{
    memset(&m_Stats, 0, sizeof(m_Stats));
}

CKRasterizerContext::~CKRasterizerContext()
{
}

CKBOOL CKRasterizerContext::GetDesc(CKRasterizerContextDesc *Desc) const
{
    if (!Desc)
        return FALSE;
    Desc->PosX = m_PosX;
    Desc->PosY = m_PosY;
    Desc->Width = m_Width;
    Desc->Height = m_Height;
    Desc->Bpp = m_Bpp;
    Desc->ZBpp = m_ZBpp;
    Desc->StencilBpp = m_StencilBpp;
    Desc->Fullscreen = m_Fullscreen;
    Desc->RefreshRate = m_RefreshRate;
    Desc->Window = m_Window;
    return TRUE;
}

void CKRasterizerContext::SetContextDesc(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height,
                                         int Bpp, CKBOOL Fullscreen, int RefreshRate, int ZBpp, int StencilBpp)
{
    m_Window = Window;
    m_PosX = PosX;
    m_PosY = PosY;
    m_Width = Width;
    m_Height = Height;
    m_Bpp = Bpp > 0 ? Bpp : 32;
    m_ZBpp = ZBpp > 0 ? ZBpp : 24;
    m_StencilBpp = StencilBpp > 0 ? StencilBpp : 8;
    m_Fullscreen = Fullscreen ? TRUE : FALSE;
    m_RefreshRate = RefreshRate > 0 ? RefreshRate : 0;
}

CKBOOL CKRasterizerContext::ResolveResize(int &PosX, int &PosY, int &Width, int &Height, CKDWORD Flags) const
{
    if (Flags & ~(VX_RESIZE_NOMOVE | VX_RESIZE_NOSIZE))
        return FALSE;
    if (Flags & VX_RESIZE_NOMOVE) {
        PosX = m_PosX;
        PosY = m_PosY;
    }
    if (Flags & VX_RESIZE_NOSIZE) {
        Width = m_Width;
        Height = m_Height;
    }
    return Width > 0 && Height > 0 ? TRUE : FALSE;
}

void CKRasterizerContext::SetContextOptions(const CKRasterizerOptions &Options)
{
    m_Options = Options;
    if (!(m_Options.RenderScale > 0.0f) || !isfinite(m_Options.RenderScale))
        m_Options.RenderScale = 1.0f;
    else if (m_Options.RenderScale < 0.5f)
        m_Options.RenderScale = 0.5f;
    else if (m_Options.RenderScale > 2.0f)
        m_Options.RenderScale = 2.0f;
    if (!(m_Options.Sharpness > 0.0f) || !isfinite(m_Options.Sharpness))
        m_Options.Sharpness = 0.0f;
    else if (m_Options.Sharpness > 1.0f)
        m_Options.Sharpness = 1.0f;
    if (m_Options.MSAASamples <= 1)
        m_Options.MSAASamples = 0;
}

void CKRasterizerContext::SetDebugMarker(CKSTRING Name)
{
    m_Marker = Name ? Name : "";
}

void CKRasterizerContext::GetStats(CKRenderStats &Stats) const
{
    Stats = m_Stats;
    Stats.FrameNumber = m_FrameNumber;
    Stats.Width = m_Width;
    Stats.Height = m_Height;
}
