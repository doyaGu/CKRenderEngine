// CKTranslatedContext lifecycle, fixed-function state access and statistics.

#include "CKFFRasterizerContextInternal.h"
#include <new>

#include <string.h>

// ===========================================================================
// Construction / lifecycle
// ===========================================================================

CKTranslatedContext::CKTranslatedContext(const CKFFRasterizerContextDesc &Desc)
    : m_ShaderLibrary(*Desc.Shaders), m_BackendReady(Desc.BackendReady),
      m_BackendReadyUser(Desc.BackendReadyUser), m_Backend(Desc.Backend),
      m_Created(FALSE), m_ShuttingDown(FALSE),
      m_FrameNumber(0), m_LastDeviceFrame(0), m_Target(0), m_TargetFace(CKRST_CUBEFACE_XPOS),
      m_TargetWidth(0), m_TargetHeight(0),
      m_TargetFrameBuffer(0), m_TargetDepthTexture(0), m_CopyTexture(0), m_CopyWidth(0), m_CopyHeight(0),
      m_FrameDrawCalls(0), m_FramePrimitives(0), m_FramePasses(0), m_FrameClears(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0), m_LayoutMismatchLogged(FALSE)
{
    m_Driver = Desc.Driver;
    memset(&m_Stats, 0, sizeof(m_Stats));
}

CKTranslatedContext::~CKTranslatedContext()
{
    if (m_Created && !m_ShuttingDown)
        BeginShutdown();
    for (size_t i = 0; i < m_Readbacks.size(); ++i)
        delete m_Readbacks[i];
    m_Readbacks.clear();
}

CKBOOL CKTranslatedContext::Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp,
                                   CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp)
{
    if (m_Created || !m_Backend || !m_Driver || m_ShaderLibrary.Empty())
        return FALSE;
    CKBackendInitDesc init;
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

    m_Window = Window;
    m_PosX = (CKDWORD)PosX;
    m_PosY = (CKDWORD)PosY;
    m_Width = (CKDWORD)Width;
    m_Height = (CKDWORD)Height;
    m_Bpp = Bpp > 0 ? (CKDWORD)Bpp : 32;
    m_ZBpp = Zbpp > 0 ? (CKDWORD)Zbpp : 24;
    m_StencilBpp = StencilBpp > 0 ? (CKDWORD)StencilBpp : 8;
    m_Fullscreen = Fullscreen;
    m_RefreshRate = (CKDWORD)RefreshRate;

    CKBackendShaderSet shaders;
    const CKBackendCaps &backendCaps = m_Backend->GetCaps();
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

CKBOOL CKTranslatedContext::Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags)
{
    if (!m_Created || m_ShuttingDown || m_Frame.Open || (Flags & ~(VX_RESIZE_NOMOVE | VX_RESIZE_NOSIZE)))
        return FALSE;
    if (Flags & VX_RESIZE_NOMOVE) {
        PosX = (int)m_PosX;
        PosY = (int)m_PosY;
    }
    if (Flags & VX_RESIZE_NOSIZE) {
        Width = (int)m_Width;
        Height = (int)m_Height;
    }
    if (Width <= 0 || Height <= 0)
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

CKBOOL CKTranslatedContext::SetOptions(const CKRasterizerOptions *Options)
{
    if (!Options || Options->Size != sizeof(CKRasterizerOptions)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    // Accepted at any time (render callbacks may change the options inside
    // the scene); the internal targets follow the options at the next frame
    // (PrepareFrameTarget).
    m_Options = *Options;
    m_Options.Size = sizeof(CKRasterizerOptions);
    m_Options.RenderScale = CKPresentStage::ClampRenderScale(m_Options.RenderScale);
    m_Options.Sharpness = CKPresentStage::ClampSharpness(m_Options.Sharpness);
    if (m_Options.MSAASamples <= 1)
        m_Options.MSAASamples = 0;
    if (m_Created && !m_ShuttingDown) {
        // Between frames the next frame decides its targets again (a readback
        // may have prepared them with the previous options).
        if (!m_Frame.Open)
            m_Frame.TargetDecided = FALSE;
        ApplyOptions();
    }
    return TRUE;
}

void CKTranslatedContext::ApplyOptions()
{
    m_FFP.SetRenderOptions(m_Options.DisableTextureFiltering, m_Options.DisableMipmaps,
                           m_Options.ForceAnisotropicFiltering);
    m_Backend->SetDebugFlags(m_Options.DebugFlags);
}

CKBOOL CKTranslatedContext::GetCaps(CKRasterizerCapsDesc *Caps) const
{
    if (!Caps || !m_Backend || !m_Created)
        return FALSE;
    const CKBackendCaps &backend = m_Backend->GetCaps();

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
    features |= CKRST_CAPS_TEXTURE_DXT;
    caps.Features = features;
    caps.MaxTextureSize = backend.MaxTextureSize;
    caps.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    caps.MaxAnisotropy = 16;
    caps.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    caps.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
    caps.MaxMSAASamples = backend.MaxMSAASamples > 1 ? backend.MaxMSAASamples : 1;
    caps.MaxPointSize = 15.0f;
    caps.MaxLights = CKRST_MAX_LIGHTS;
    *Caps = caps;
    return TRUE;
}

CKERROR CKTranslatedContext::GetDeviceStatus() const
{
    if (!m_Created || !m_Backend)
        return CKERR_INVALIDRENDERCONTEXT;
    return m_Backend->GetDeviceStatus();
}

CKBOOL CKTranslatedContext::BeginShutdown()
{
    if (!m_Created || m_ShuttingDown)
        return TRUE;
    // Cancellation invokes user callbacks. Reject new work before any callback
    // can enqueue another readback or open a scene during resource teardown.
    m_ShuttingDown = TRUE;
    if (m_Frame.Open) {
        // An open frame (scene without BackToFront) ends without presenting.
        CKDWORD frame = 0;
        if (m_Backend->Submit(CKBackendSubmitDesc(CKRST_BACKEND_SYNC_UNCHANGED, FALSE), &frame) == CK_OK)
            m_LastDeviceFrame = frame;
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
    m_Resources.clear();
    return TRUE;
}

CKBOOL CKTranslatedContext::IsIdle() const
{
    if (m_Frame.Open)
        return FALSE;
    return !m_Backend || m_Backend->IsIdle();
}

// ===========================================================================
// Fixed-function state
// ===========================================================================

CKBOOL CKTranslatedContext::SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value)
{
    if (!CKRSTIsValidRenderStateType((CKDWORD)State)) {
        Diag(CKRST_DIAG_INVALID_RENDER_STATE);
        return FALSE;
    }
    m_FFP.SetRenderState(State, Value);
    return TRUE;
}

CKBOOL CKTranslatedContext::GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value)
{
    if (!Value)
        return FALSE;
    if (!CKRSTIsValidRenderStateType((CKDWORD)State)) {
        Diag(CKRST_DIAG_INVALID_RENDER_STATE);
        return FALSE;
    }
    *Value = m_FFP.QueryRenderState(State);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value)
{
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (!CKRSTIsValidTextureStageStateType((CKDWORD)Tss)) {
        Diag(CKRST_DIAG_INVALID_STAGE_STATE);
        return FALSE;
    }
    switch (Tss) {
    case CKRST_TSS_OP:
    case CKRST_TSS_ARG1:
    case CKRST_TSS_ARG2:
    case CKRST_TSS_AOP:
    case CKRST_TSS_AARG1:
    case CKRST_TSS_AARG2:
    case CKRST_TSS_COLORARG0:
    case CKRST_TSS_ALPHAARG0:
    case CKRST_TSS_RESULTARG0:
    case CKRST_TSS_STAGEBLEND:
        if (Value == 0) {
            m_FFP.ClearTextureStageState(Stage, Tss);
            return TRUE;
        }
        break;
    default:
        break;
    }
    m_FFP.SetTextureStageState(Stage, Tss, Value);
    return TRUE;
}

CKBOOL CKTranslatedContext::ResetTextureStages(int FirstStage, int StageCount)
{
    if (FirstStage < 0 || FirstStage > CKRST_MAX_TEXTURE_STAGES ||
        StageCount < 0 || StageCount > CKRST_MAX_TEXTURE_STAGES - FirstStage) {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    m_FFP.ResetTextureStages(FirstStage, StageCount);
    return TRUE;
}

CKBOOL CKTranslatedContext::GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value)
{
    if (!Value)
        return FALSE;
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (!CKRSTIsValidTextureStageStateType((CKDWORD)Tss)) {
        Diag(CKRST_DIAG_INVALID_STAGE_STATE);
        return FALSE;
    }
    *Value = m_FFP.QueryTextureStageState(Stage, Tss);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetTexture(CKDWORD Texture, int Stage)
{
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    CKDWORD flags = 0;
    if (Texture != 0) {
        const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
        if (!resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        flags = resource->Texture.Flags | CKRST_TEXTURE_VALID;
    }
    m_FFP.SetTexture(Stage, Texture, flags);
    return TRUE;
}

CKBOOL CKTranslatedContext::GetTexture(int Stage, CKDWORD *Texture)
{
    if (!Texture)
        return FALSE;
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    *Texture = m_FFP.GetTexture(Stage);
    return TRUE;
}

CKBOOL CKTranslatedContext::GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat)
{
    const int slot = CKRSTMatrixSlot(Type);
    if (slot < 0) {
        Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    return m_FFP.GetTransform(Type, Mat);
}

CKBOOL CKTranslatedContext::SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat)
{
    const int slot = CKRSTMatrixSlot(Type);
    if (slot < 0) {
        Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    const CKDWORD type = (CKDWORD)Type;
    if (slot == 0) {
        // VXMATRIX_WORLD and VXMATRIX_WORLDMATRIX(0) alias the same matrix.
        m_FFP.SetTransform(VXMATRIX_WORLD, Mat);
        m_FFP.SetVertexBlendMatrix(0, Mat);
    } else if (slot < CKRST_MAX_WORLD_MATRICES) {
        m_FFP.SetVertexBlendMatrix((CKDWORD)slot, Mat);
    } else if (type == (CKDWORD)VXMATRIX_VIEW || type == (CKDWORD)VXMATRIX_PROJECTION) {
        m_FFP.SetTransform(Type, Mat);
    } else {
        m_FFP.SetTransform(Type, Mat); // VXMATRIX_TEXTURE0..7
    }
    return TRUE;
}

CKBOOL CKTranslatedContext::SetLight(CKDWORD Index, const CKLightData *Data)
{
    if (Index >= CKRST_MAX_LIGHTS) {
        Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    if (!Data) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    m_FFP.SetLight((int)Index, Data);
    return TRUE;
}

CKBOOL CKTranslatedContext::EnableLight(CKDWORD Index, CKBOOL Enable)
{
    if (Index >= CKRST_MAX_LIGHTS) {
        Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    m_FFP.EnableLight((int)Index, Enable);
    return TRUE;
}

CKBOOL CKTranslatedContext::ApplyMaterial(const CKMaterialRenderState &State)
{
    m_FFP.ApplyMaterial(State);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetMaterial(const CKMaterialData *Data)
{
    if (!Data) {
        m_FFP.ResetMaterial();
        return TRUE;
    }
    m_FFP.SetMaterial(Data);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetViewport(const CKViewportData *Data)
{
    if (!Data) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    m_FFP.SetViewport(*Data);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetUserClipPlane(CKDWORD Index, const VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES) {
        Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    m_FFP.SetUserClipPlane((int)Index, Plane);
    return TRUE;
}

CKBOOL CKTranslatedContext::GetUserClipPlane(CKDWORD Index, VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES) {
        Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    Plane = m_FFP.GetUserClipPlane((int)Index);
    return TRUE;
}

void CKTranslatedContext::InitDefaultRenderStatesValue()
{
    m_FFP.InitDefaultStates();
}

// ===========================================================================
// Diagnostics
// ===========================================================================

void CKTranslatedContext::SetDebugMarker(CKSTRING Name)
{
    m_Marker = Name ? Name : "";
}

const CKRenderStats *CKTranslatedContext::GetStats()
{
    if (m_Backend && m_Created) {
        const CKBackendStats &backend = m_Backend->GetStats();
        m_Stats.CpuTimeFrame = backend.CpuTimeFrame;
        m_Stats.CpuTimerFreq = backend.CpuTimerFreq;
        m_Stats.GpuTimeFrame = backend.GpuTimeFrame;
        m_Stats.GpuTimerFreq = backend.GpuTimerFreq;
        m_Stats.GpuMemoryMax = backend.GpuMemoryMax;
        m_Stats.GpuMemoryUsed = backend.GpuMemoryUsed;
    }
    m_Stats.FrameNumber = m_FrameNumber;
    m_Stats.Width = m_Width;
    m_Stats.Height = m_Height;
    return &m_Stats;
}

CKRasterizerContext *CKFFCreateRasterizerContext(const CKFFRasterizerContextDesc &desc)
{
    if (!desc.Driver || !desc.Backend || !desc.Shaders || desc.Shaders->Empty())
        return NULL;
    return new (std::nothrow) CKTranslatedContext(desc);
}

void CKFFDeleteRasterizerContext(CKRasterizerContext *context)
{
    delete static_cast<CKTranslatedContext *>(context);
}
