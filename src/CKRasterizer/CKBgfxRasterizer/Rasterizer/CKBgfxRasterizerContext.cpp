#include "CKBgfxRasterizerContext.h"
#include "CKFFContextState.h"
#include "CKBgfxRasterizer.h"

// Concrete bgfx context lifecycle, fixed-function state access and statistics.

#include <new>

#include <string.h>

// ===========================================================================
// Construction / lifecycle
// ===========================================================================

CKBgfxRasterizerContext::CKBgfxRasterizerContext(
    CKBgfxRasterizerDriver *Driver, const CKFFShaderLibrary *Shaders)
    : CKBgfxRasterizerContext()
{
    m_Driver = Driver;
    if (Shaders)
        m_ShaderLibrary = *Shaders;
}

CKBgfxRasterizerContext::~CKBgfxRasterizerContext()
{
    if (m_Created && !m_ShuttingDown)
        BeginShutdown();
    for (int i = 0; i < m_Readbacks.Size(); ++i)
        delete m_Readbacks[i];
    m_Readbacks.Clear();
    if (m_BgfxInitialized)
        Shutdown();
    delete m_Resources;
    m_Resources = NULL;
}

CKBOOL CKBgfxRasterizerContext::Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp,
                                   CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp)
{
    if (m_Created || !m_Driver || m_ShaderLibrary.Empty())
        return FALSE;
    CKRasterizerInitParameters init;
    init.Window = Window;
    init.PosX = PosX;
    init.PosY = PosY;
    init.Width = Width;
    init.Height = Height;
    init.Bpp = Bpp;
    init.ZBpp = Zbpp;
    init.StencilBpp = StencilBpp;
    init.Fullscreen = Fullscreen;
    init.RefreshRate = RefreshRate;
    init.DebugFlags = m_Options.DebugFlags;
    m_ShaderLibrary.GetTargets(init.ShaderTargets);
    if (Init(&init) != CK_OK)
        return FALSE;

    SetContextDesc(Window, PosX, PosY, Width, Height, Bpp, Fullscreen, RefreshRate, Zbpp, StencilBpp);

    CKFFShaderSet shaders;
    const CKRasterizerDeviceCaps &backendCaps = GetCaps();
    CKRasterizerTargetDesc shaderTarget;
    shaderTarget.ShaderFormat = backendCaps.ShaderFormat;
    shaderTarget.ShaderProfile = backendCaps.ShaderProfile;
    shaderTarget.HomogeneousDepth = backendCaps.HomogeneousDepth;
    shaderTarget.OriginBottomLeft = backendCaps.OriginBottomLeft;
    if (!m_ShaderLibrary.Find(backendCaps.ShaderFormat, backendCaps.ShaderProfile, shaders) ||
        !m_ShaderCache.Init(shaderTarget, shaders)) {
        Shutdown();
        return FALSE;
    }
    if (!m_FFP.Init(backendCaps.Features, backendCaps.MaxTextureBindings,
                    m_ShaderCache.GetTargetFlags())) {
        ClearNativeVertexLayouts();
        ClearNativeFFPrograms();
        Shutdown();
        return FALSE;
    }
    m_Present.Init(this, shaders);
    static_cast<CKBgfxRasterizerDriver *>(m_Driver)->RefreshCaps(*this);

    m_Created = TRUE;
    m_ShuttingDown = FALSE;
    memset(&m_Stats, 0, sizeof(m_Stats));
    m_FrameNumber = 0;
    m_Frame.Reset();
    m_PassTarget = 0;
    m_BufferUses.Clear();

    CKViewportData viewport;
    viewport.ViewX = 0;
    viewport.ViewY = 0;
    viewport.ViewWidth = m_Width;
    viewport.ViewHeight = m_Height;
    viewport.ViewZMin = 0.0f;
    viewport.ViewZMax = 1.0f;
    m_FFP.SetViewport(viewport);
    UpdateTargetExtents();

    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    SetTransformMatrix(VXMATRIX_WORLD, identity);
    SetTransformMatrix(VXMATRIX_VIEW, identity);
    SetTransformMatrix(VXMATRIX_PROJECTION, identity);
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
        SetTransformMatrix(VXMATRIX_TEXTURE(i), identity);
    for (int i = 0; i < CKRST_MAX_LIGHTS; ++i)
        EnableLight((CKDWORD)i, FALSE);

    ApplyOptions();
    UpdateAlphaTestPrecision();
    if (GetCaps().RequiresIntermediateTarget && !PrepareFrameTarget()) {
        BeginShutdown();
        m_Created = FALSE;
        return FALSE;
    }
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags)
{
    if (!m_Created || m_ShuttingDown || m_Frame.Open || !ResolveResize(PosX, PosY, Width, Height, Flags))
        return FALSE;
    const bool sizeChanged = Width != (int)m_Width || Height != (int)m_Height;
    if (!sizeChanged && PosX == (int)m_PosX && PosY == (int)m_PosY)
        return TRUE;
    if (Resize(PosX, PosY, Width, Height) != CK_OK)
        return FALSE;
    m_PosX = PosX;
    m_PosY = PosY;
    if (!sizeChanged)
        return TRUE;
    m_Width = Width;
    m_Height = Height;
    m_Present.DestroyTargets();
    m_Frame.NativePresented = FALSE;
    // The internal targets follow the new size at the next frame (a readback
    // between frames may have decided the previous ones already).
    m_Frame.InternalTargets = FALSE;
    m_Frame.TargetDecided = FALSE;
    // The viewport follows the window like on creation; the engine sets its
    // own viewport again after a resize anyway.
    CKViewportData viewport = m_FFP.GetViewport();
    viewport.ViewX = 0;
    viewport.ViewY = 0;
    viewport.ViewWidth = m_Width;
    viewport.ViewHeight = m_Height;
    m_FFP.SetViewport(viewport);
    UpdateTargetExtents();
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::SetOptions(const CKRasterizerOptions *Options)
{
    if (!Options) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    // Accepted at any time (render callbacks may change the options inside
    // the scene); the internal targets follow the options at the next frame
    // (PrepareFrameTarget).
    SetContextOptions(*Options);
    if (m_Created && !m_ShuttingDown) {
        // Between frames the next frame decides its targets again (a readback
        // may have prepared them with the previous options).
        if (!m_Frame.Open)
            m_Frame.TargetDecided = FALSE;
        ApplyOptions();
    }
    return TRUE;
}

void CKBgfxRasterizerContext::ApplyOptions()
{
    m_FFP.SetRenderOptions(m_Options.DisableTextureFiltering, m_Options.DisableMipmaps,
                           m_Options.ForceAnisotropicFiltering);
    SetDebugFlags(m_Options.DebugFlags);
}

CKBOOL CKBgfxRasterizerContext::GetCaps(CKRasterizerCapsDesc *Caps) const
{
    if (!Caps || !m_Created)
        return FALSE;
    return CKFFGetContextCaps(GetCaps(), m_Driver, Caps);
}

CKBOOL CKBgfxRasterizerContext::BeginShutdown()
{
    if (!m_Created || m_ShuttingDown)
        return TRUE;
    // Cancellation invokes user callbacks. Reject new work before any callback
    // can enqueue another readback or open a scene during resource teardown.
    m_ShuttingDown = TRUE;
    if (m_Frame.Open) {
        // An open frame (scene without BackToFront) ends without presenting.
        CKDWORD frame = 0;
        if (Submit(CKRST_PRESENT_UNCHANGED, FALSE, &frame) == CK_OK) {
            m_LastDeviceFrame = frame;
            m_BufferUses.MarkSubmitted(GetLastSubmitId());
        }
        m_Frame.Open = FALSE;
    }
    m_Frame.Reset();
    m_PassTarget = 0;
    CancelReadbacks();
    ReleaseTarget();
    if (m_CopyTexture) {
        DestroyObject(m_CopyTexture, CKRST_OBJ_TEXTURE);
        m_CopyTexture = 0;
        m_CopyWidth = m_CopyHeight = 0;
    }
    m_Present.Shutdown();
    if (!IsIdle()) {
        if (GetDeviceStatus() == CK_OK) {
            m_ShuttingDown = FALSE;
            return FALSE;
        }
        // A failed device cannot finish its queued pass through Submit. Its
        // shutdown abandons native work and releases tickets before the FFP
        // caches discard their now-invalid logical handles.
        Shutdown();
    }
    if (m_FFP.Shutdown() != CK_OK) {
        m_ShuttingDown = FALSE;
        return FALSE;
    }
    ClearNativeVertexLayouts();
    ClearNativeFFPrograms();
    Shutdown();
    m_PublicResources.Clear();
    m_BufferUses.Clear();
    return TRUE;
}

// ===========================================================================
// Fixed-function state
// ===========================================================================

CKBOOL CKBgfxRasterizerContext::SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value)
{
    return CKFFSetRenderState(m_FFP, m_Stats, State, Value);
}

CKBOOL CKBgfxRasterizerContext::GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value)
{
    return CKFFGetRenderState(m_FFP, m_Stats, State, Value);
}

CKBOOL CKBgfxRasterizerContext::SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value)
{
    return CKFFSetTextureStageState(m_FFP, m_Stats, Stage, Tss, Value);
}

CKBOOL CKBgfxRasterizerContext::ResetTextureStages(int FirstStage, int StageCount)
{
    return CKFFResetTextureStages(m_FFP, m_Stats, FirstStage, StageCount);
}

CKBOOL CKBgfxRasterizerContext::GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value)
{
    return CKFFGetTextureStageState(m_FFP, m_Stats, Stage, Tss, Value);
}

CKBOOL CKBgfxRasterizerContext::SetTexture(CKDWORD Texture, int Stage)
{
    return CKFFSetTextureBinding(m_FFP, m_Stats, Texture,
                                 m_PublicResources.FindTexture(Texture), Stage);
}

CKBOOL CKBgfxRasterizerContext::GetTexture(int Stage, CKDWORD *Texture)
{
    return CKFFGetTextureBinding(m_FFP, m_Stats, Stage, Texture);
}

CKBOOL CKBgfxRasterizerContext::GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat)
{
    return CKFFGetTransform(m_FFP, m_Stats, Type, Mat);
}

CKBOOL CKBgfxRasterizerContext::SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat)
{
    return CKFFSetTransform(m_FFP, m_Stats, Type, Mat);
}

CKBOOL CKBgfxRasterizerContext::SetLight(CKDWORD Index, const CKLightData *Data)
{
    return CKFFSetLight(m_FFP, m_Stats, Index, Data);
}

CKBOOL CKBgfxRasterizerContext::EnableLight(CKDWORD Index, CKBOOL Enable)
{
    return CKFFEnableLight(m_FFP, m_Stats, Index, Enable);
}

CKBOOL CKBgfxRasterizerContext::ApplyMaterial(const CKMaterialRenderState &State)
{
    return CKFFApplyMaterial(m_FFP, State);
}

CKBOOL CKBgfxRasterizerContext::SetMaterial(const CKMaterialData *Data)
{
    return CKFFSetMaterial(m_FFP, Data);
}

CKBOOL CKBgfxRasterizerContext::SetViewport(const CKViewportData *Data)
{
    return CKFFSetViewport(m_FFP, m_Stats, Data);
}

CKBOOL CKBgfxRasterizerContext::SetUserClipPlane(CKDWORD Index, const VxPlane &Plane)
{
    return CKFFSetUserClipPlane(m_FFP, m_Stats, Index, Plane);
}

CKBOOL CKBgfxRasterizerContext::GetUserClipPlane(CKDWORD Index, VxPlane &Plane)
{
    return CKFFGetUserClipPlane(m_FFP, m_Stats, Index, Plane);
}

void CKBgfxRasterizerContext::InitDefaultRenderStatesValue()
{
    m_FFP.InitDefaultStates();
}

// ===========================================================================
// Diagnostics
// ===========================================================================

void CKBgfxRasterizerContext::GetStats(CKRenderStats &Stats) const
{
    CKRasterizerContext::GetStats(Stats);
    if (m_Created) {
        Stats.CpuTimeFrame = m_BgfxCpuTimeFrame;
        Stats.CpuTimerFreq = m_BgfxCpuTimerFreq;
        Stats.GpuTimeFrame = m_BgfxGpuTimeFrame;
        Stats.GpuTimerFreq = m_BgfxGpuTimerFreq;
        Stats.GpuMemoryMax = m_BgfxGpuMemoryMax;
        Stats.GpuMemoryUsed = m_BgfxGpuMemoryUsed;
    }
}
