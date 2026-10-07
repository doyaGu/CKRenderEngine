// FFPRecordingContext lifecycle, fixed-function state access and statistics.

#include "FFPRecordingContext.h"
#include "CKFFContextState.h"
#include <new>

#include <string.h>

// ===========================================================================
// Construction / lifecycle
// ===========================================================================

FFPRecordingContext::FFPRecordingContext(const FFPRecordingContextDesc &Desc)
    : CKRasterizerContext(Desc.Driver), m_ShaderLibrary(*Desc.Shaders), m_BackendReady(Desc.BackendReady),
      m_BackendReadyUser(Desc.BackendReadyUser), m_Backend(Desc.Backend),
      m_PassTarget(0), m_LastDeviceFrame(0),
      m_TargetFrameBuffer(0), m_TargetDepthTexture(0),
      m_CopyTexture(0), m_CopyWidth(0), m_CopyHeight(0),
      m_FrameDrawCalls(0), m_FramePrimitives(0), m_FramePasses(0), m_FrameClears(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0), m_LayoutMismatchLogged(FALSE)
{
}

FFPRecordingContext::~FFPRecordingContext()
{
    if (m_Created && !m_ShuttingDown)
        BeginShutdown();
    for (size_t i = 0; i < m_Readbacks.size(); ++i)
        delete m_Readbacks[i];
    m_Readbacks.clear();
}

CKBOOL FFPRecordingContext::Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp,
                                   CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp)
{
    if (m_Created || !m_Backend || !m_Driver || m_ShaderLibrary.Empty())
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
    if (m_Backend->Init(&init) != CK_OK)
        return FALSE;

    SetContextDesc(Window, PosX, PosY, Width, Height, Bpp, Fullscreen, RefreshRate, Zbpp, StencilBpp);

    CKFFShaderSet shaders;
    const CKRasterizerDeviceCaps &backendCaps = m_Backend->GetCaps();
    if (!m_ShaderLibrary.Find(backendCaps.ShaderFormat, backendCaps.ShaderProfile, shaders) ||
        !m_FFP.Init(m_Backend, shaders)) {
        m_Backend->Shutdown();
        return FALSE;
    }
    m_Present.Init(m_Backend, shaders);
    if (m_BackendReady)
        m_BackendReady(m_BackendReadyUser, m_Backend);

    m_Created = TRUE;
    m_ShuttingDown = FALSE;
    memset(&m_Stats, 0, sizeof(m_Stats));
    m_FrameNumber = 0;
    m_Frame.Reset();

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
    if (m_Backend->GetCaps().RequiresIntermediateTarget && !PrepareFrameTarget()) {
        BeginShutdown();
        m_Created = FALSE;
        return FALSE;
    }
    return TRUE;
}

CKBOOL FFPRecordingContext::Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags)
{
    if (!m_Created || m_ShuttingDown || m_Frame.Open || !ResolveResize(PosX, PosY, Width, Height, Flags))
        return FALSE;
    const bool sizeChanged = Width != (int)m_Width || Height != (int)m_Height;
    if (!sizeChanged && PosX == (int)m_PosX && PosY == (int)m_PosY)
        return TRUE;
    if (m_Backend->Resize(PosX, PosY, Width, Height) != CK_OK)
        return FALSE;
    m_PosX = (CKDWORD)PosX;
    m_PosY = (CKDWORD)PosY;
    if (!sizeChanged)
        return TRUE;
    m_Width = (CKDWORD)Width;
    m_Height = (CKDWORD)Height;
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

CKBOOL FFPRecordingContext::SetOptions(const CKRasterizerOptions *Options)
{
    if (!Options) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (m_Created && !CKFFSupportedMSAASamples(Options->MSAASamples,
                                               m_Backend->GetCaps().MaxMSAASamples)) {
        Diag(CKRST_DIAG_REJECT_UNSUPPORTED_STATE);
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

void FFPRecordingContext::ApplyOptions()
{
    m_FFP.SetRenderOptions(m_Options.DisableTextureFiltering, m_Options.DisableMipmaps,
                           m_Options.ForceAnisotropicFiltering);
    m_Backend->SetDebugFlags(m_Options.DebugFlags);
}

CKBOOL FFPRecordingContext::GetCaps(CKRasterizerCapsDesc *Caps) const
{
    if (!Caps || !m_Backend || !m_Created)
        return FALSE;
    const CKRasterizerDeviceCaps &backend = m_Backend->GetCaps();

    CKRasterizerCapsDesc caps;
    CKRST_CAPS features = 0;
    if (backend.Features & CKRST_DEVCAPS_TEXTURE_READBACK)
        features |= CKRST_CAPS_SYNC_READBACK;
    features |= CKRST_CAPS_POINT_SIZE;   // 1..15 (approximated beyond)
    features |= CKRST_CAPS_MSAA;
    if (backend.Features & CKRST_DEVCAPS_TEXTURE_CUBE)
        features |= CKRST_CAPS_TEXTURE_CUBE;
    if (backend.Features & CKRST_DEVCAPS_TEXTURE_3D)
        features |= CKRST_CAPS_TEXTURE_VOLUME;
    features |= CKRST_CAPS_BORDER_COLOR;
    if (backend.Features & CKRST_DEVCAPS_BLEND_EQUATION)
        features |= CKRST_CAPS_SEPARATE_ALPHA_BLEND;
    if (CKFFDriverHasCapability(m_Driver, CKRST_CAPS_TEXTURE_DXT))
        features |= CKRST_CAPS_TEXTURE_DXT;
    caps.Features = features;
    caps.MaxTextureSize = backend.MaxTextureSize;
    caps.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    caps.MaxAnisotropy = 16;
    caps.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    caps.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
    caps.MaxMSAASamples = backend.MaxMSAASamples > 1 ? backend.MaxMSAASamples : 1;
    caps.MaxPointSize = 8192.0f;
    caps.MaxLights = CKRST_MAX_LIGHTS;
    *Caps = caps;
    return TRUE;
}

CKERROR FFPRecordingContext::GetDeviceStatus() const
{
    if (!m_Created || !m_Backend)
        return CKERR_INVALIDRENDERCONTEXT;
    return m_Backend->GetDeviceStatus();
}

CKBOOL FFPRecordingContext::BeginShutdown()
{
    if (!m_Created || m_ShuttingDown)
        return TRUE;
    // Cancellation invokes user callbacks. Reject new work before any callback
    // can enqueue another readback or open a scene during resource teardown.
    m_ShuttingDown = TRUE;
    if (m_Frame.Open) {
        // An open frame (scene without BackToFront) ends without presenting.
        CKDWORD frame = 0;
        if (m_Backend->Submit(CKRST_PRESENT_UNCHANGED, FALSE, &frame) == CK_OK) {
            m_LastDeviceFrame = frame;
            m_BufferUses.MarkSubmitted(m_Backend->GetLastSubmitId());
            m_BufferUses.Retire(m_Backend->GetCompletedSubmitId());
        }
        m_Frame.Open = FALSE;
    }
    m_Frame.Reset();
    ReleaseFrameScratch();
    CancelReadbacks();
    ReleaseTarget();
    if (m_CopyTexture) {
        m_Backend->DestroyObject(m_CopyTexture, CKRST_OBJ_TEXTURE);
        m_CopyTexture = 0;
        m_CopyWidth = m_CopyHeight = 0;
    }
    m_Present.Shutdown();
    if (m_FFP.PrepareShutdown() != CK_OK) {
        if (m_Backend->GetDeviceStatus() == CK_OK) {
            m_ShuttingDown = FALSE;
            return FALSE;
        }
        // A failed device cannot finish its queued pass through Submit. Its
        // shutdown abandons native work and releases tickets before the FFP
        // caches discard their now-invalid logical handles.
        m_Backend->Shutdown();
    }
    if (m_FFP.Shutdown() != CK_OK) {
        m_ShuttingDown = FALSE;
        return FALSE;
    }
    m_Backend->Shutdown();
    m_PublicResources.Clear();
    m_BufferUses.Clear();
    return TRUE;
}

CKBOOL FFPRecordingContext::IsIdle() const
{
    if (m_Frame.Open)
        return FALSE;
    return !m_Backend || m_Backend->IsIdle();
}

// ===========================================================================
// Fixed-function state
// ===========================================================================

CKBOOL FFPRecordingContext::SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value)
{
    return CKFFSetRenderState(m_FFP, m_Stats, State, Value);
}

CKBOOL FFPRecordingContext::GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value)
{
    return CKFFGetRenderState(m_FFP, m_Stats, State, Value);
}

CKBOOL FFPRecordingContext::SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value)
{
    return CKFFSetTextureStageState(m_FFP, m_Stats, Stage, Tss, Value);
}

CKBOOL FFPRecordingContext::ResetTextureStages(int FirstStage, int StageCount)
{
    return CKFFResetTextureStages(m_FFP, m_Stats, FirstStage, StageCount);
}

CKBOOL FFPRecordingContext::GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value)
{
    return CKFFGetTextureStageState(m_FFP, m_Stats, Stage, Tss, Value);
}

CKBOOL FFPRecordingContext::SetTexture(CKDWORD Texture, int Stage)
{
    const CKTextureDesc *texture = m_PublicResources.FindTexture(Texture);
    return CKFFSetTextureBinding(
        m_FFP, m_Stats, Texture,
        texture, Stage);
}

CKBOOL FFPRecordingContext::GetTexture(int Stage, CKDWORD *Texture)
{
    return CKFFGetTextureBinding(m_FFP, m_Stats, Stage, Texture);
}

CKBOOL FFPRecordingContext::GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat)
{
    return CKFFGetTransform(m_FFP, m_Stats, Type, Mat);
}

CKBOOL FFPRecordingContext::SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat)
{
    return CKFFSetTransform(m_FFP, m_Stats, Type, Mat);
}

CKBOOL FFPRecordingContext::SetLight(CKDWORD Index, const CKLightData *Data)
{
    return CKFFSetLight(m_FFP, m_Stats, Index, Data);
}

CKBOOL FFPRecordingContext::EnableLight(CKDWORD Index, CKBOOL Enable)
{
    return CKFFEnableLight(m_FFP, m_Stats, Index, Enable);
}

CKBOOL FFPRecordingContext::ApplyMaterial(const CKMaterialRenderState &State)
{
    return CKFFApplyMaterial(m_FFP, State);
}

CKBOOL FFPRecordingContext::SetMaterial(const CKMaterialData *Data)
{
    return CKFFSetMaterial(m_FFP, Data);
}

CKBOOL FFPRecordingContext::SetViewport(const CKViewportData *Data)
{
    return CKFFSetViewport(m_FFP, m_Stats, Data);
}

CKBOOL FFPRecordingContext::SetUserClipPlane(CKDWORD Index, const VxPlane &Plane)
{
    return CKFFSetUserClipPlane(m_FFP, m_Stats, Index, Plane);
}

CKBOOL FFPRecordingContext::GetUserClipPlane(CKDWORD Index, VxPlane &Plane)
{
    return CKFFGetUserClipPlane(m_FFP, m_Stats, Index, Plane);
}

void FFPRecordingContext::InitDefaultRenderStatesValue()
{
    m_FFP.InitDefaultStates();
}

// ===========================================================================
// Diagnostics
// ===========================================================================
