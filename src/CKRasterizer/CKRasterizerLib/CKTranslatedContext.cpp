// CKTranslatedContext: lifecycle, fixed-function state mirror, resources,
// render targets, readback and statistics. The frame flow and the draws live
// in CKTranslatedFrame.cpp.

#include "CKTranslatedRasterizer.h"
#include "CKFFUniformState.h"
#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKDebugLogger.h"

#include <chrono>
#include <string.h>
#include <thread>

namespace {

const CKDWORD kReadbackTimeoutMs = 30000;

CKBOOL SameImageFormat(const VxImageDescEx &a, const VxImageDescEx &b)
{
    return a.BitsPerPixel == b.BitsPerPixel && a.RedMask == b.RedMask && a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask && a.AlphaMask == b.AlphaMask;
}

void SleepMilliseconds(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

CKDWORD NowMilliseconds()
{
    return (CKDWORD)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

// ===========================================================================
// Construction / lifecycle
// ===========================================================================

CKTranslatedContext::CKTranslatedContext(CKTranslatedDriver *Driver, CKRasterizerDevice *Device)
    : m_TranslatedDriver(Driver), m_Device(Device), m_Created(FALSE), m_ShuttingDown(FALSE), m_InScene(FALSE),
      m_OverlayPhase(FALSE), m_PassOpen(FALSE), m_InternalTargets(FALSE), m_Composited(FALSE),
      m_FrameTargetDecided(FALSE), m_Encoder(NULL), m_CurrentView(0), m_NextView(0), m_LastFrameViewCount(0),
      m_FrameNumber(0), m_Target(0), m_TargetFace(CKRST_CUBEFACE_XPOS), m_TargetWidth(0), m_TargetHeight(0),
      m_TargetFrameBuffer(0), m_TargetDepthTexture(0), m_CopyTexture(0), m_CopyWidth(0), m_CopyHeight(0),
      m_FrameDrawCalls(0), m_FramePrimitives(0), m_FramePasses(0), m_FrameClears(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0), m_LayoutMismatchLogged(FALSE)
{
    m_Driver = Driver;
    memset(m_RenderStates, 0, sizeof(m_RenderStates));
    memset(m_StageStates, 0, sizeof(m_StageStates));
    memset(m_Textures, 0, sizeof(m_Textures));
    for (int i = 0; i < CKRST_MATRIX_SLOT_COUNT; ++i)
        Vx3DMatrixIdentity(m_Matrices[i]);
    memset(m_Lights, 0, sizeof(m_Lights));
    memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
    memset(&m_Material, 0, sizeof(m_Material));
    for (int i = 0; i < CKRST_MAX_USER_CLIP_PLANES; ++i)
        m_ClipPlanes[i] = VxPlane();
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
    if (m_Created || !m_Device)
        return FALSE;
    if (m_Device->Create(Window, PosX, PosY, Width, Height, Bpp, Fullscreen, RefreshRate, Zbpp, StencilBpp) != CK_OK)
        return FALSE;

    m_Window = Window;
    m_PosX = m_Device->m_PosX;
    m_PosY = m_Device->m_PosY;
    m_Width = m_Device->m_Width;
    m_Height = m_Device->m_Height;
    m_Bpp = m_Device->m_Bpp;
    m_ZBpp = m_Device->m_ZBpp;
    m_StencilBpp = m_Device->m_StencilBpp;
    m_Fullscreen = m_Device->m_Fullscreen;
    m_RefreshRate = m_Device->m_RefreshRate;

    if (!m_FFP.Init(m_Device)) {
        m_Device->BeginShutdown();
        return FALSE;
    }
    m_Postprocess.Init(m_Device);
    if (m_TranslatedDriver)
        m_TranslatedDriver->SyncCapsFromDevice();

    m_Created = TRUE;
    m_ShuttingDown = FALSE;
    memset(&m_Stats, 0, sizeof(m_Stats));
    m_FrameNumber = 0;
    m_NextView = 0;
    m_LastFrameViewCount = 0;

    ResetStateMirror();

    m_Viewport.ViewX = 0;
    m_Viewport.ViewY = 0;
    m_Viewport.ViewWidth = m_Width;
    m_Viewport.ViewHeight = m_Height;
    m_Viewport.ViewZMin = 0.0f;
    m_Viewport.ViewZMax = 1.0f;
    m_FFP.SetViewport(m_Viewport);

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
    return TRUE;
}

CKBOOL CKTranslatedContext::Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags)
{
    if (!m_Created || m_ShuttingDown || m_Encoder)
        return FALSE;
    if (m_Device->Resize(PosX, PosY, Width, Height, Flags) != CK_OK)
        return FALSE;
    m_PosX = m_Device->m_PosX;
    m_PosY = m_Device->m_PosY;
    m_Width = m_Device->m_Width;
    m_Height = m_Device->m_Height;
    m_Postprocess.DestroyTargets();
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
    m_Options.RenderScale = CKPostprocessPass::ClampRenderScale(m_Options.RenderScale);
    m_Options.Sharpness = CKPostprocessPass::ClampSharpness(m_Options.Sharpness);
    if (m_Options.MSAASamples <= 1)
        m_Options.MSAASamples = 0;
    if (m_Created && !m_ShuttingDown)
        ApplyOptions();
    return TRUE;
}

void CKTranslatedContext::ApplyOptions()
{
    m_FFP.SetRenderOptions(m_Options.DisableTextureFiltering, m_Options.DisableMipmaps,
                           m_Options.ForceAnisotropicFiltering);
    m_Device->SetDebug(m_Options.DebugFlags);
}

CKBOOL CKTranslatedContext::GetCaps(CKRasterizerCapsDesc *Caps) const
{
    if (!Caps)
        return FALSE;
    CKRasterizerDeviceCapsDesc device;
    if (!m_Device || m_Device->GetCaps(&device) != CK_OK)
        return FALSE;

    CKRasterizerCapsDesc caps;
    CKRST_CAPS features = 0;
    if (device.Features & CKRST_DEVCAPS_TEXTURE_READBACK)
        features |= CKRST_CAPS_SYNC_READBACK;
    features |= CKRST_CAPS_POINT_SIZE;   // 1..15 (approximated beyond)
    features |= CKRST_CAPS_MSAA;
    if (device.Features & CKRST_DEVCAPS_TEXTURE_CUBE)
        features |= CKRST_CAPS_TEXTURE_CUBE;
    if (device.Features & CKRST_DEVCAPS_TEXTURE_3D)
        features |= CKRST_CAPS_TEXTURE_VOLUME;
    features |= CKRST_CAPS_BORDER_COLOR;
    if (device.Features & CKRST_DEVCAPS_BLEND_EQUATION)
        features |= CKRST_CAPS_SEPARATE_ALPHA_BLEND;
    features |= CKRST_CAPS_TEXTURE_DXT;
    caps.Features = features;
    caps.MaxTextureSize = device.MaxTextureSize;
    caps.MaxTextureStages = device.MaxTextureStages < CKRST_MAX_TEXTURE_STAGES
                                ? device.MaxTextureStages : CKRST_MAX_TEXTURE_STAGES;
    caps.MaxAnisotropy = 16;
    caps.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    caps.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
    caps.MaxMSAASamples = 16;
    caps.MaxPointSize = 15.0f;
    caps.MaxLights = CKRST_MAX_LIGHTS;
    *Caps = caps;
    return TRUE;
}

CKERROR CKTranslatedContext::GetDeviceStatus() const
{
    if (!m_Created || !m_Device)
        return CKERR_INVALIDRENDERCONTEXT;
    return m_Device->GetDeviceStatus();
}

CKBOOL CKTranslatedContext::BeginShutdown()
{
    if (!m_Created || m_ShuttingDown)
        return TRUE;
    if (m_Encoder) {
        m_Device->EndEncoder(m_Encoder);
        m_Encoder = NULL;
    }
    m_InScene = FALSE;
    m_PassOpen = FALSE;
    m_OverlayPhase = FALSE;
    ReleaseFrameScratch();
    CancelReadbacks();
    ReleaseTarget();
    if (m_CopyTexture) {
        m_Device->DeleteObject(m_CopyTexture, CKRST_OBJ_TEXTURE);
        m_CopyTexture = 0;
        m_CopyWidth = m_CopyHeight = 0;
    }
    m_Postprocess.Shutdown();
    if (m_FFP.PrepareShutdown() != CK_OK)
        return FALSE;
    if (m_Device->BeginShutdown() != CK_OK)
        return FALSE;
    m_FFP.Shutdown();
    m_Resources.clear();
    m_ShuttingDown = TRUE;
    return TRUE;
}

CKBOOL CKTranslatedContext::IsIdle() const
{
    if (m_Encoder)
        return FALSE;
    return !m_Device || m_Device->IsIdle();
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
    m_RenderStates[(CKDWORD)State] = Value;
    if ((CKDWORD)State == (CKDWORD)VXRENDERSTATE_COLORWRITEENABLE)
        m_FFP.SetColorWriteMask(Value & CKRST_COLORWRITE_ALL);
    else
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
    *Value = m_RenderStates[(CKDWORD)State];
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
    m_StageStates[Stage][(CKDWORD)Tss] = Value;
    switch ((CKDWORD)Tss) {
    case CKRST_TSS_ADDRESS:
        m_StageStates[Stage][CKRST_TSS_ADDRESSU] = Value;
        m_StageStates[Stage][CKRST_TSS_ADDRESSV] = Value;
        m_StageStates[Stage][CKRST_TSS_ADDRESW] = Value;
        m_FFP.SetTextureStageState(Stage, CKRST_TSS_ADDRESSU, Value);
        m_FFP.SetTextureStageState(Stage, CKRST_TSS_ADDRESSV, Value);
        m_FFP.SetTextureStageState(Stage, CKRST_TSS_ADDRESW, Value);
        m_FFP.SetTextureStageState(Stage, Tss, Value);
        return TRUE;
    case CKRST_TSS_OP:
    case CKRST_TSS_ARG1:
    case CKRST_TSS_ARG2:
    case CKRST_TSS_AOP:
    case CKRST_TSS_AARG1:
    case CKRST_TSS_AARG2:
    case CKRST_TSS_COLORARG0:
    case CKRST_TSS_ALPHAARG0:
    case CKRST_TSS_RESULTARG0:
        // 0 = not set (spec 4.6): the pipeline derives the state from
        // TEXTUREMAPBLEND and the bound texture at draw time.
        if (Value == 0)
            m_FFP.ClearTextureStageState(Stage, Tss);
        else
            m_FFP.SetTextureStageState(Stage, Tss, Value);
        return TRUE;
    case CKRST_TSS_STAGEBLEND: {
        if (Value == 0) {
            m_FFP.ClearTextureStageState(Stage, Tss);
            return TRUE;
        }
        m_FFP.SetTextureStageState(Stage, Tss, Value);
        // The pipeline derives the combine states from the blend; the mirror
        // shows the same values so a save / restore replays them.
        CKDWORD colorOp = 0, colorArg1 = 0, colorArg2 = 0, alphaOp = 0, alphaArg1 = 0, alphaArg2 = 0;
        if (CKFFStageBlendToTextureOps(Value, colorOp, colorArg1, colorArg2, alphaOp, alphaArg1, alphaArg2)) {
            m_StageStates[Stage][CKRST_TSS_OP] = colorOp;
            m_StageStates[Stage][CKRST_TSS_ARG1] = colorArg1;
            m_StageStates[Stage][CKRST_TSS_ARG2] = colorArg2;
            m_StageStates[Stage][CKRST_TSS_AOP] = alphaOp;
            m_StageStates[Stage][CKRST_TSS_AARG1] = alphaArg1;
            m_StageStates[Stage][CKRST_TSS_AARG2] = alphaArg2;
        }
        return TRUE;
    }
    case CKRST_TSS_TEXTUREMAPBLEND:
        // The legacy blend replaces any explicit combine state (the pipeline
        // clears them too); they read back as "not set".
        m_FFP.SetTextureStageState(Stage, Tss, Value);
        m_StageStates[Stage][CKRST_TSS_OP] = 0;
        m_StageStates[Stage][CKRST_TSS_ARG1] = 0;
        m_StageStates[Stage][CKRST_TSS_ARG2] = 0;
        m_StageStates[Stage][CKRST_TSS_AOP] = 0;
        m_StageStates[Stage][CKRST_TSS_AARG1] = 0;
        m_StageStates[Stage][CKRST_TSS_AARG2] = 0;
        m_StageStates[Stage][CKRST_TSS_COLORARG0] = 0;
        m_StageStates[Stage][CKRST_TSS_ALPHAARG0] = 0;
        m_StageStates[Stage][CKRST_TSS_RESULTARG0] = 0;
        return TRUE;
    default:
        m_FFP.SetTextureStageState(Stage, Tss, Value);
        return TRUE;
    }
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
    *Value = m_StageStates[Stage][(CKDWORD)Tss];
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
    m_Textures[Stage] = Texture;
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
    *Texture = m_Textures[Stage];
    return TRUE;
}

CKBOOL CKTranslatedContext::GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat)
{
    const int slot = CKRSTMatrixSlot(Type);
    if (slot < 0) {
        Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    Mat = m_Matrices[slot];
    return TRUE;
}

CKBOOL CKTranslatedContext::SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat)
{
    const int slot = CKRSTMatrixSlot(Type);
    if (slot < 0) {
        Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    m_Matrices[slot] = Mat;
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
    m_Lights[Index] = *Data;
    m_FFP.SetLight((int)Index, Data);
    return TRUE;
}

CKBOOL CKTranslatedContext::EnableLight(CKDWORD Index, CKBOOL Enable)
{
    if (Index >= CKRST_MAX_LIGHTS) {
        Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    m_LightEnabled[Index] = Enable ? TRUE : FALSE;
    m_FFP.EnableLight((int)Index, Enable);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetMaterial(const CKMaterialData *Data)
{
    if (!Data) {
        // No material: white diffuse / ambient, everything else zero.
        memset(&m_Material, 0, sizeof(m_Material));
        m_Material.Diffuse = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
        m_Material.Ambient = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
        m_FFP.ResetMaterial();
        return TRUE;
    }
    m_Material = *Data;
    m_FFP.SetMaterial(Data);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetViewport(const CKViewportData *Data)
{
    if (!Data) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    m_Viewport = *Data;
    m_FFP.SetViewport(m_Viewport);
    return TRUE;
}

CKBOOL CKTranslatedContext::SetUserClipPlane(CKDWORD Index, const VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES) {
        Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    m_ClipPlanes[Index] = Plane;
    m_FFP.SetUserClipPlane((int)Index, Plane);
    return TRUE;
}

CKBOOL CKTranslatedContext::GetUserClipPlane(CKDWORD Index, VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES) {
        Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    Plane = m_ClipPlanes[Index];
    return TRUE;
}

void CKTranslatedContext::ResetStateMirror()
{
    // Contract-visible defaults (spec 4.6). Create() only resets the mirror:
    // the fixed-function pipeline keeps its own richer defaults (they are what
    // the engine renders with today), and the engine sets everything it
    // depends on every frame anyway.
    for (CKDWORD state = 0; state < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++state)
        m_RenderStates[state] = CKRSTDefaultRenderStateValue((VXRENDERSTATETYPE)state);
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        for (CKDWORD tss = 0; tss < (CKDWORD)CKRST_TSS_MAXSTATE; ++tss)
            m_StageStates[stage][tss] = CKRSTDefaultTextureStageStateValue(stage, (CKRST_TEXTURESTAGESTATETYPE)tss);
    }
}

void CKTranslatedContext::InitDefaultRenderStatesValue()
{
    ResetStateMirror();
    // Render states with a non-zero v1 default reach the pipeline; the ones
    // the v1 table leaves at 0 keep the pipeline's own draw-valid defaults.
    for (CKDWORD state = 0; state < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++state) {
        const CKDWORD value = m_RenderStates[state];
        if (value == 0 && state != (CKDWORD)VXRENDERSTATE_COLORWRITEENABLE)
            continue;
        if (state == (CKDWORD)VXRENDERSTATE_COLORWRITEENABLE)
            m_FFP.SetColorWriteMask(value & CKRST_COLORWRITE_ALL);
        else
            m_FFP.SetRenderState((VXRENDERSTATETYPE)state, value);
    }
    // Texture stages go back to the pipeline's own stage defaults. Pushing the
    // D3D8 defaults one state at a time would mark states such as COLORARG0 or
    // STAGEBLEND as explicitly set and make the pipeline reject draws.
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        m_FFP.ResetTextureStage(stage);
        m_FFP.SetTexture(stage, 0, 0);
        m_Textures[stage] = 0;
    }
}

// ===========================================================================
// Resources
// ===========================================================================

CKTranslatedContext::Resource *CKTranslatedContext::FindResource(CKDWORD Type, CKDWORD Handle)
{
    if (Handle == 0)
        return NULL;
    std::unordered_map<uint64_t, Resource>::iterator it = m_Resources.find(ResourceKey(Type, Handle));
    return it == m_Resources.end() ? NULL : &it->second;
}

const CKTranslatedContext::Resource *CKTranslatedContext::FindResource(CKDWORD Type, CKDWORD Handle) const
{
    if (Handle == 0)
        return NULL;
    std::unordered_map<uint64_t, Resource>::const_iterator it = m_Resources.find(ResourceKey(Type, Handle));
    return it == m_Resources.end() ? NULL : &it->second;
}

CKBOOL CKTranslatedContext::GetVertexBufferDescForTests(CKDWORD VB, CKVertexBufferDesc *Desc) const
{
    const Resource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    if (!resource || !Desc)
        return FALSE;
    *Desc = resource->VertexBuffer;
    return TRUE;
}

int CKTranslatedContext::GetLiveResourceCountForTests(CKDWORD TypeMask) const
{
    int count = 0;
    for (std::unordered_map<uint64_t, Resource>::const_iterator it = m_Resources.begin(); it != m_Resources.end(); ++it) {
        if (it->second.Type & TypeMask)
            ++count;
    }
    return count;
}

CKBOOL CKTranslatedContext::CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    if (Desc->Format.Width <= 0 || Desc->Format.Height <= 0 ||
        ((Desc->Flags & CKRST_TEXTURE_CUBEMAP) && Desc->Format.Width != Desc->Format.Height)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    // CKRST_MIPMAP_GENERATE ((CKDWORD)-1) is passed through: the device treats
    // it as an auto-mip request and builds the chain from level 0. 0 and 1
    // both mean "no mip levels" (spec 4.5).
    CKTextureDesc deviceDesc = *Desc;
    deviceDesc.Flags |= CKRST_TEXTURE_VALID;
    if (deviceDesc.Depth == 0)
        deviceDesc.Depth = 1;
    if (deviceDesc.MipMapCount == 0)
        deviceDesc.MipMapCount = 1;

    CKDWORD handle = 0;
    if (m_Device->CreateTexture(&deviceDesc, NULL, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Resource &resource = m_Resources[ResourceKey(CKRST_OBJ_TEXTURE, handle)];
    resource = Resource();
    resource.Type = CKRST_OBJ_TEXTURE;
    resource.Handle = handle;
    resource.Texture = deviceDesc;
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKTranslatedContext::LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel,
                                        CKRST_CUBEFACE Face, const CKRECT *Region)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    if (!Image.Image || Image.Width <= 0 || Image.Height <= 0 || MipLevel < 0 ||
        (CKDWORD)Face >= CKRST_CUBEFACE_COUNT || (!cube && Face != CKRST_CUBEFACE_XPOS)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKDWORD levels = resource->Texture.MipMapCount == CKRST_MIPMAP_GENERATE ? 1
                           : (resource->Texture.MipMapCount == 0 ? 1 : resource->Texture.MipMapCount);
    if ((CKDWORD)MipLevel >= levels) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (Region) {
        const CKDWORD levelWidth = XMax((CKDWORD)1, (CKDWORD)resource->Texture.Format.Width >> MipLevel);
        const CKDWORD levelHeight = XMax((CKDWORD)1, (CKDWORD)resource->Texture.Format.Height >> MipLevel);
        if (!ValidateRect(Region, levelWidth, levelHeight)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
    }
    if (m_Device->UpdateTexture(Texture, (CKDWORD)MipLevel, (CKDWORD)Face, Region, &Image) != CK_OK)
        return FALSE;
    ++m_FrameTextureUploads;
    return TRUE;
}

CKBOOL CKTranslatedContext::GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const
{
    if (!Desc)
        return FALSE;
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource)
        return FALSE;
    *Desc = resource->Texture;
    return TRUE;
}

CKBOOL CKTranslatedContext::CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    if (Desc->m_MaxVertexCount == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    Resource resource;
    resource.Type = CKRST_OBJ_VERTEXBUFFER;
    resource.VertexBuffer = *Desc;
    const CKDWORD canonicalStride = CKRSTGetVertexLayout(Desc->m_VertexFormat, Desc->m_TexcoordDims, &resource.Layout);
    if (canonicalStride == 0 || (Desc->m_VertexSize != 0 && Desc->m_VertexSize != canonicalStride)) {
        // The engine writes Lock memory in the canonical layout (spec 4.5); a
        // different explicit vertex size cannot be honoured.
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource.VertexBuffer.m_VertexSize = canonicalStride;

    // The device keeps the fixed-function pipeline's own interleaved layout;
    // Unlock converts from the canonical layout the engine writes.
    const bool hasNormal = resource.Layout.NormalOffset >= 0;
    const bool hasUV = resource.Layout.TexcoordCount > 0;
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(Desc->m_VertexFormat, hasNormal, hasUV);
    if (resource.Layout.TweenPositionOffset >= 0) {
        formatFlags |= CKFF_VF_TWEENPOSITION;
        if (resource.Layout.TweenNormalOffset >= 0)
            formatFlags |= CKFF_VF_TWEENNORMAL;
    }
    resource.FormatFlags = formatFlags;
    resource.DeviceStride = CKVertexLayoutCache::ComputeStride(formatFlags);
    resource.DeviceLayout = m_FFP.GetVertexLayoutCache().GetLayout(formatFlags);
    if (resource.DeviceLayout == 0 || resource.DeviceStride == 0) {
        if (!m_LayoutMismatchLogged) {
            m_LayoutMismatchLogged = TRUE;
            CK_LOG_FMT("Rasterizer", "no device vertex layout for vertex format 0x%08X (ffp 0x%08X)",
                       Desc->m_VertexFormat, formatFlags);
        }
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    CKVertexBufferDesc deviceDesc = *Desc;
    deviceDesc.m_Flags |= CKRST_VB_VALID;
    deviceDesc.m_VertexSize = resource.DeviceStride;
    std::vector<CKBYTE> converted;
    const void *deviceData = NULL;
    if (Data) {
        resource.Shadow.resize((size_t)Desc->m_MaxVertexCount * canonicalStride);
        memcpy(resource.Shadow.data(), Data, (size_t)Desc->m_MaxVertexCount * canonicalStride);
        resource.LockStart = 0;
        resource.LockCount = Desc->m_MaxVertexCount;
        resource.Locked = TRUE;
        // Reuse the Unlock conversion path on the whole buffer.
        converted.resize((size_t)Desc->m_MaxVertexCount * resource.DeviceStride);
        VxDrawPrimitiveData dp;
        memset(&dp, 0, sizeof(dp));
        const CKRSTVertexLayout &l = resource.Layout;
        const CKBYTE *base = resource.Shadow.data();
        dp.VertexCount = (int)Desc->m_MaxVertexCount;
        dp.Flags = Desc->m_VertexFormat;
        dp.PositionPtr = (void *)(base + l.PositionOffset);
        dp.PositionStride = canonicalStride;
        if (l.NormalOffset >= 0) { dp.NormalPtr = (void *)(base + l.NormalOffset); dp.NormalStride = canonicalStride; }
        if (l.DiffuseOffset >= 0) { dp.ColorPtr = (void *)(base + l.DiffuseOffset); dp.ColorStride = canonicalStride; }
        if (l.SpecularOffset >= 0) { dp.SpecularColorPtr = (void *)(base + l.SpecularOffset); dp.SpecularColorStride = canonicalStride; }
        if (l.TexcoordCount > 0) { dp.TexCoordPtr = (void *)(base + l.TexcoordOffset[0]); dp.TexCoordStride = canonicalStride; }
        for (int i = 1; i < l.TexcoordCount; ++i) {
            dp.TexCoordPtrs[i - 1] = (void *)(base + l.TexcoordOffset[i]);
            dp.TexCoordStrides[i - 1] = canonicalStride;
        }
        if (l.TweenPositionOffset >= 0) { dp.TweenPositionPtr = (void *)(base + l.TweenPositionOffset); dp.TweenPositionStride = canonicalStride; }
        if (l.TweenNormalOffset >= 0) { dp.TweenNormalPtr = (void *)(base + l.TweenNormalOffset); dp.TweenNormalStride = canonicalStride; }
        CKBYTE dims[CKRST_MAX_TEXTURE_STAGES];
        for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
            dims[i] = (CKBYTE)(i < l.TexcoordCount ? l.TexcoordDims[i] : 2);
        CKTransientGeometry::InterleaveVertices(converted.data(), resource.DeviceStride, Desc->m_MaxVertexCount,
                                                formatFlags, &dp, dims);
        resource.Locked = FALSE;
        resource.LockCount = 0;
        deviceData = converted.data();
    }

    CKDWORD handle = 0;
    if (m_Device->CreateVertexBuffer(&deviceDesc, deviceData, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource.Handle = handle;
    m_Resources[ResourceKey(CKRST_OBJ_VERTEXBUFFER, handle)] = resource;
    if (Data)
        ++m_FrameBufferUploads;
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKTranslatedContext::CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    if (Desc->m_MaxIndexCount == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKIndexBufferDesc deviceDesc = *Desc;
    deviceDesc.m_Flags |= CKRST_VB_VALID;
    CKDWORD handle = 0;
    if (m_Device->CreateIndexBuffer(&deviceDesc, FALSE, Data, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Resource &resource = m_Resources[ResourceKey(CKRST_OBJ_INDEXBUFFER, handle)];
    resource = Resource();
    resource.Type = CKRST_OBJ_INDEXBUFFER;
    resource.Handle = handle;
    resource.IndexBuffer = *Desc;
    if (Data) {
        resource.Shadow.resize((size_t)Desc->m_MaxIndexCount * 2);
        memcpy(resource.Shadow.data(), Data, (size_t)Desc->m_MaxIndexCount * 2);
        ++m_FrameBufferUploads;
    }
    *OutHandle = handle;
    return TRUE;
}

void *CKTranslatedContext::LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags)
{
    (void)Flags;
    Resource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD maxCount = resource->VertexBuffer.m_MaxVertexCount;
    if (VertexCount == 0)
        VertexCount = StartVertex < maxCount ? maxCount - StartVertex : 0;
    if (resource->Locked || StartVertex >= maxCount || VertexCount == 0 || StartVertex + VertexCount > maxCount) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    const CKDWORD stride = resource->VertexBuffer.m_VertexSize;
    if (resource->Shadow.size() < (size_t)maxCount * stride)
        resource->Shadow.resize((size_t)maxCount * stride, 0);
    resource->Locked = TRUE;
    resource->LockStart = StartVertex;
    resource->LockCount = VertexCount;
    return resource->Shadow.data() + (size_t)StartVertex * stride;
}

CKBOOL CKTranslatedContext::UnlockVertexBuffer(CKDWORD VB)
{
    Resource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!resource->Locked) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource->Locked = FALSE;
    const CKDWORD stride = resource->VertexBuffer.m_VertexSize;
    const CKDWORD start = resource->LockStart;
    const CKDWORD count = resource->LockCount;
    const CKRSTVertexLayout &l = resource->Layout;
    const CKBYTE *base = resource->Shadow.data() + (size_t)start * stride;

    VxDrawPrimitiveData dp;
    memset(&dp, 0, sizeof(dp));
    dp.VertexCount = (int)count;
    dp.Flags = resource->VertexBuffer.m_VertexFormat;
    dp.PositionPtr = (void *)(base + l.PositionOffset);
    dp.PositionStride = stride;
    if (l.NormalOffset >= 0) { dp.NormalPtr = (void *)(base + l.NormalOffset); dp.NormalStride = stride; }
    if (l.DiffuseOffset >= 0) { dp.ColorPtr = (void *)(base + l.DiffuseOffset); dp.ColorStride = stride; }
    if (l.SpecularOffset >= 0) { dp.SpecularColorPtr = (void *)(base + l.SpecularOffset); dp.SpecularColorStride = stride; }
    if (l.TexcoordCount > 0) { dp.TexCoordPtr = (void *)(base + l.TexcoordOffset[0]); dp.TexCoordStride = stride; }
    for (int i = 1; i < l.TexcoordCount; ++i) {
        dp.TexCoordPtrs[i - 1] = (void *)(base + l.TexcoordOffset[i]);
        dp.TexCoordStrides[i - 1] = stride;
    }
    if (l.TweenPositionOffset >= 0) { dp.TweenPositionPtr = (void *)(base + l.TweenPositionOffset); dp.TweenPositionStride = stride; }
    if (l.TweenNormalOffset >= 0) { dp.TweenNormalPtr = (void *)(base + l.TweenNormalOffset); dp.TweenNormalStride = stride; }
    CKBYTE dims[CKRST_MAX_TEXTURE_STAGES];
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
        dims[i] = (CKBYTE)(i < l.TexcoordCount ? l.TexcoordDims[i] : 2);

    const size_t deviceBytes = (size_t)count * resource->DeviceStride;
    if (resource->Scratch.size() < deviceBytes)
        resource->Scratch.resize(deviceBytes);
    CKTransientGeometry::InterleaveVertices(resource->Scratch.data(), resource->DeviceStride, count,
                                            resource->FormatFlags, &dp, dims);
    if (m_Device->UpdateVertexBuffer(VB, start * resource->DeviceStride, (CKDWORD)deviceBytes,
                                     resource->Scratch.data()) != CK_OK)
        return FALSE;
    ++m_FrameBufferUploads;
    return TRUE;
}

void *CKTranslatedContext::LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags)
{
    (void)Flags;
    Resource *resource = FindResource(CKRST_OBJ_INDEXBUFFER, IB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD maxCount = resource->IndexBuffer.m_MaxIndexCount;
    if (IndexCount == 0)
        IndexCount = StartIndex < maxCount ? maxCount - StartIndex : 0;
    if (resource->Locked || StartIndex >= maxCount || IndexCount == 0 || StartIndex + IndexCount > maxCount) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    if (resource->Shadow.size() < (size_t)maxCount * 2)
        resource->Shadow.resize((size_t)maxCount * 2, 0);
    resource->Locked = TRUE;
    resource->LockStart = StartIndex;
    resource->LockCount = IndexCount;
    return resource->Shadow.data() + (size_t)StartIndex * 2;
}

CKBOOL CKTranslatedContext::UnlockIndexBuffer(CKDWORD IB)
{
    Resource *resource = FindResource(CKRST_OBJ_INDEXBUFFER, IB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!resource->Locked) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource->Locked = FALSE;
    if (m_Device->UpdateIndexBuffer(IB, resource->LockStart * 2, resource->LockCount * 2,
                                    resource->Shadow.data() + (size_t)resource->LockStart * 2) != CK_OK)
        return FALSE;
    ++m_FrameBufferUploads;
    return TRUE;
}

CKBOOL CKTranslatedContext::DeleteObject(CKDWORD Handle, CKDWORD Type)
{
    if (Type != CKRST_OBJ_TEXTURE && Type != CKRST_OBJ_VERTEXBUFFER && Type != CKRST_OBJ_INDEXBUFFER) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Resource *resource = FindResource(Type, Handle);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (Type == CKRST_OBJ_TEXTURE) {
        if (m_Target == Handle)
            ReleaseTarget();
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            if (m_Textures[stage] == Handle) {
                m_Textures[stage] = 0;
                m_FFP.SetTexture(stage, 0, 0);
            }
        }
    }
    if (m_Device)
        m_Device->DeleteObject(Handle, Type);
    m_Resources.erase(ResourceKey(Type, Handle));
    return TRUE;
}

CKBOOL CKTranslatedContext::FlushObjects(CKDWORD TypeMask)
{
    std::vector<uint64_t> keys;
    for (std::unordered_map<uint64_t, Resource>::const_iterator it = m_Resources.begin(); it != m_Resources.end(); ++it) {
        if (it->second.Type & TypeMask)
            keys.push_back(it->first);
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        std::unordered_map<uint64_t, Resource>::iterator it = m_Resources.find(keys[i]);
        if (it == m_Resources.end())
            continue;
        DeleteObject(it->second.Handle, it->second.Type);
    }
    return TRUE;
}

void CKTranslatedContext::SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name)
{
    if (m_Device && FindResource(Type, Handle))
        m_Device->SetResourceName(Handle, Type, Name);
}

// ===========================================================================
// Render targets
// ===========================================================================

void CKTranslatedContext::ReleaseTarget()
{
    if (m_Device) {
        if (m_TargetFrameBuffer)
            m_Device->DeleteObject(m_TargetFrameBuffer, CKRST_OBJ_FRAMEBUFFER);
        if (m_TargetDepthTexture)
            m_Device->DeleteObject(m_TargetDepthTexture, CKRST_OBJ_TEXTURE);
    }
    m_TargetFrameBuffer = 0;
    m_TargetDepthTexture = 0;
    m_Target = 0;
    m_TargetFace = CKRST_CUBEFACE_XPOS;
    m_TargetWidth = 0;
    m_TargetHeight = 0;
    m_FFP.SetRenderTargetActive(FALSE);
}

void CKTranslatedContext::UpdateAlphaTestPrecision()
{
    if (m_Target) {
        const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, m_Target);
        if (resource) {
            m_FFP.SetAlphaTestPrecision(CKFFAlphaTestPrecisionForFormat(resource->Texture.Format));
            return;
        }
    }
    VxImageDescEx backbuffer;
    VxPixelFormat2ImageDesc(m_Bpp == 16 ? _16_RGB565 : _32_ARGB8888, backbuffer);
    m_FFP.SetAlphaTestPrecision(CKFFAlphaTestPrecisionForFormat(backbuffer));
}

CKBOOL CKTranslatedContext::SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_InScene) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    if (Texture == 0) {
        if (m_Target)
            ReleaseTarget();
        UpdateAlphaTestPrecision();
        return TRUE;
    }
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    const int textureWidth = resource->Texture.Format.Width;
    const int textureHeight = resource->Texture.Format.Height;
    if ((resource->Texture.Flags & CKRST_TEXTURE_RENDERTARGET) == 0 ||
        (CKDWORD)Face >= CKRST_CUBEFACE_COUNT || (!cube && Face != CKRST_CUBEFACE_XPOS) ||
        (cube && textureWidth != textureHeight) ||
        (Width > 0 && Width != textureWidth) || (Height > 0 && Height != textureHeight)) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    ReleaseTarget();

    CKDepthTextureDesc depthDesc = {};
    depthDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL;
    depthDesc.Width = (CKDWORD)textureWidth;
    depthDesc.Height = (CKDWORD)textureHeight;
    depthDesc.MipMapCount = 1;
    const CKBOOL needsStencil = m_StencilBpp > 0;
    depthDesc.DepthFormat = needsStencil ? CKRST_DEPTHFMT_D24S8 : CKRST_DEPTHFMT_D24;
    CKDWORD depthTexture = 0;
    CKERROR depthErr = m_Device->CreateDepthTexture(&depthDesc, &depthTexture);
    if (depthErr != CK_OK && !needsStencil) {
        depthDesc.DepthFormat = CKRST_DEPTHFMT_D16;
        depthErr = m_Device->CreateDepthTexture(&depthDesc, &depthTexture);
    }
    if (depthErr != CK_OK) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    CKFrameBufferAttachmentDesc color = {};
    color.Texture = Texture;
    color.Mip = 0;
    color.Layer = (CKDWORD)Face;
    CKFrameBufferDesc fbDesc = {};
    fbDesc.Color = &color;
    fbDesc.ColorCount = 1;
    fbDesc.DepthStencil.Texture = depthTexture;
    CKDWORD frameBuffer = 0;
    if (m_Device->CreateFrameBuffer(&fbDesc, &frameBuffer) != CK_OK) {
        m_Device->DeleteObject(depthTexture, CKRST_OBJ_TEXTURE);
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    m_Target = Texture;
    m_TargetFace = Face;
    m_TargetWidth = (CKDWORD)textureWidth;
    m_TargetHeight = (CKDWORD)textureHeight;
    m_TargetFrameBuffer = frameBuffer;
    m_TargetDepthTexture = depthTexture;
    m_FFP.SetRenderTargetActive(TRUE);
    UpdateAlphaTestPrecision();
    return TRUE;
}

// ===========================================================================
// Readback
// ===========================================================================

CKBOOL CKTranslatedContext::ValidateRect(const CKRECT *Rect, CKDWORD Width, CKDWORD Height) const
{
    if (!Rect)
        return TRUE;
    return Rect->left >= 0 && Rect->top >= 0 && Rect->right > Rect->left && Rect->bottom > Rect->top &&
           (CKDWORD)Rect->right <= Width && (CKDWORD)Rect->bottom <= Height;
}

void CKTranslatedContext::ReadbackCallbackAdapter(void *UserData, CKDWORD FrameBuffer, CKDWORD Width, CKDWORD Height,
                                                  CKDWORD Pitch, VX_PIXELFORMAT Format, const void *Data, CKDWORD Size,
                                                  CKBOOL YFlip)
{
    (void)FrameBuffer;
    PendingReadback *readback = static_cast<PendingReadback *>(UserData);
    if (!readback || !readback->Owner)
        return;
    VxMutexLock lock(readback->Owner->m_ReadbackMutex);
    if (Data && Size != 0 && Width != 0 && Height != 0 && Pitch != 0 && Format != UNKNOWN_PF) {
        readback->Width = Width;
        readback->Height = Height;
        readback->Pitch = Pitch;
        readback->Format = Format;
        readback->YFlip = YFlip;
        readback->Data.resize(Size);
        memcpy(readback->Data.data(), Data, Size);
        readback->Success = TRUE;
    }
    readback->Done = TRUE;
}

CKBOOL CKTranslatedContext::BuildReadbackImage(const PendingReadback &Readback, VxImageDescEx &Desc,
                                               std::vector<CKBYTE> &Pixels) const
{
    if (!Readback.Success || Readback.Data.empty() || Readback.Width == 0 || Readback.Height == 0)
        return FALSE;
    VxImageDescEx source;
    VxPixelFormat2ImageDesc(Readback.Format, source);
    if (source.BitsPerPixel <= 0 || (source.BitsPerPixel % 8) != 0)
        return FALSE;
    const CKDWORD sourceBpp = (CKDWORD)source.BitsPerPixel / 8;
    if (Readback.Pitch < (uint64_t)Readback.Width * sourceBpp)
        return FALSE;
    if ((uint64_t)(Readback.Height - 1) * Readback.Pitch + (uint64_t)Readback.Width * sourceBpp > Readback.Data.size())
        return FALSE;

    int left = 0, top = 0, right = (int)Readback.Width, bottom = (int)Readback.Height;
    if (Readback.HasRect) {
        left = Readback.Rect.left > 0 ? Readback.Rect.left : 0;
        top = Readback.Rect.top > 0 ? Readback.Rect.top : 0;
        right = Readback.Rect.right < (int)Readback.Width ? Readback.Rect.right : (int)Readback.Width;
        bottom = Readback.Rect.bottom < (int)Readback.Height ? Readback.Rect.bottom : (int)Readback.Height;
    }
    if (right <= left || bottom <= top)
        return FALSE;
    const int width = right - left;
    const int height = bottom - top;

    // Cropped, top-down copy in the source format.
    std::vector<CKBYTE> cropped((size_t)width * height * sourceBpp);
    for (int row = 0; row < height; ++row) {
        const CKDWORD logicalRow = (CKDWORD)(top + row);
        const CKDWORD sourceRow = Readback.YFlip ? Readback.Height - 1 - logicalRow : logicalRow;
        memcpy(cropped.data() + (size_t)row * width * sourceBpp,
               Readback.Data.data() + (size_t)sourceRow * Readback.Pitch + (size_t)left * sourceBpp,
               (size_t)width * sourceBpp);
    }
    source.Width = width;
    source.Height = height;
    source.BytesPerLine = width * (int)sourceBpp;
    source.Image = cropped.data();

    VxPixelFormat2ImageDesc(_32_ARGB8888, Desc);
    Desc.Width = width;
    Desc.Height = height;
    Desc.BytesPerLine = width * 4;
    Pixels.resize((size_t)width * height * 4);
    Desc.Image = Pixels.data();
    if (SameImageFormat(source, Desc))
        memcpy(Pixels.data(), cropped.data(), Pixels.size());
    else
        VxDoBlit(source, Desc);
    return TRUE;
}

CKBOOL CKTranslatedContext::RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback,
                                            void *User)
{
    if (!m_Created || m_ShuttingDown || !Callback)
        return FALSE;
    if (Buffer != VXBUFFER_BACKBUFFER) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKRECT target = CurrentTargetRect();
    if (!ValidateRect(Rect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    PendingReadback *readback = new PendingReadback();
    readback->Owner = this;
    readback->Callback = Callback;
    readback->User = User;
    readback->Buffer = Buffer;
    if (Rect) {
        readback->Rect = *Rect;
        readback->HasRect = TRUE;
    }
    {
        VxMutexLock lock(m_ReadbackMutex);
        m_Readbacks.push_back(readback);
    }
    if (m_Device->RequestScreenShot(0, ReadbackCallbackAdapter, readback) != CK_OK) {
        VxMutexLock lock(m_ReadbackMutex);
        for (size_t i = 0; i < m_Readbacks.size(); ++i) {
            if (m_Readbacks[i] == readback) {
                m_Readbacks.erase(m_Readbacks.begin() + (ptrdiff_t)i);
                break;
            }
        }
        delete readback;
        return FALSE;
    }
    return TRUE;
}

void CKTranslatedContext::DeliverReadbacks()
{
    std::vector<PendingReadback *> ready;
    {
        VxMutexLock lock(m_ReadbackMutex);
        for (size_t i = 0; i < m_Readbacks.size();) {
            if (m_Readbacks[i]->Done) {
                ready.push_back(m_Readbacks[i]);
                m_Readbacks.erase(m_Readbacks.begin() + (ptrdiff_t)i);
            } else {
                ++i;
            }
        }
    }
    for (size_t i = 0; i < ready.size(); ++i) {
        PendingReadback *readback = ready[i];
        VxImageDescEx image;
        std::vector<CKBYTE> pixels;
        const CKBOOL ok = BuildReadbackImage(*readback, image, pixels);
        readback->Callback(readback->User, readback->HasRect ? &readback->Rect : NULL, readback->Buffer,
                           ok ? &image : NULL, ok);
        delete readback;
    }
}

void CKTranslatedContext::CancelReadbacks()
{
    std::vector<PendingReadback *> outstanding;
    {
        VxMutexLock lock(m_ReadbackMutex);
        for (size_t i = 0; i < m_Readbacks.size(); ++i) {
            if (!m_Readbacks[i]->Done)
                outstanding.push_back(m_Readbacks[i]);
        }
    }
    for (size_t i = 0; i < outstanding.size(); ++i) {
        m_Device->CancelScreenShots(outstanding[i]);
        const CKDWORD start = NowMilliseconds();
        for (;;) {
            {
                VxMutexLock lock(m_ReadbackMutex);
                if (outstanding[i]->Done)
                    break;
            }
            if (NowMilliseconds() - start >= kReadbackTimeoutMs) {
                VxMutexLock lock(m_ReadbackMutex);
                outstanding[i]->Done = TRUE;
                outstanding[i]->Success = FALSE;
                break;
            }
            SleepMilliseconds(1);
        }
    }
    DeliverReadbacks();
}

int CKTranslatedContext::CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image)
{
    if (!m_Created || m_ShuttingDown)
        return 0;
    if (Buffer != VXBUFFER_BACKBUFFER) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    if (m_InScene || m_Encoder) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return 0;
    }
    const CKRECT target = CurrentTargetRect();
    if (!ValidateRect(Rect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }

    PendingReadback readback;
    readback.Owner = this;
    readback.Buffer = Buffer;
    if (Rect) {
        readback.Rect = *Rect;
        readback.HasRect = TRUE;
    }
    if (m_Device->RequestScreenShot(0, ReadbackCallbackAdapter, &readback) != CK_OK)
        return 0;

    const CKDWORD start = NowMilliseconds();
    for (;;) {
        {
            VxMutexLock lock(m_ReadbackMutex);
            if (readback.Done)
                break;
        }
        if (m_Device->Frame(CKRST_FRAME_SYNC_PRESERVE_PRESENT, CKRST_FRAME_NONE, NULL) != CK_OK)
            break;
        if (NowMilliseconds() - start >= kReadbackTimeoutMs)
            break;
        SleepMilliseconds(1);
    }
    CKBOOL done = FALSE;
    {
        VxMutexLock lock(m_ReadbackMutex);
        done = readback.Done;
    }
    if (!done) {
        m_Device->CancelScreenShots(&readback);
        const CKDWORD cancelStart = NowMilliseconds();
        for (;;) {
            {
                VxMutexLock lock(m_ReadbackMutex);
                if (readback.Done)
                    break;
            }
            if (NowMilliseconds() - cancelStart >= kReadbackTimeoutMs)
                return 0; // the device still owns the request; leak the copy rather than a use-after-free
            SleepMilliseconds(1);
        }
        return 0;
    }

    VxImageDescEx captured;
    std::vector<CKBYTE> pixels;
    if (!BuildReadbackImage(readback, captured, pixels))
        return 0;

    CKBYTE *destination = Image.Image;
    Image = captured;
    const int size = (int)pixels.size();
    if (!destination) {
        Image.Image = NULL;
        return size;
    }
    Image.Image = destination;
    memcpy(destination, pixels.data(), pixels.size());
    return size;
}

CKBOOL CKTranslatedContext::CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    if ((CKDWORD)Face >= CKRST_CUBEFACE_COUNT || (!cube && Face != CKRST_CUBEFACE_XPOS)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (m_InScene || m_Encoder) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }

    const CKRECT target = CurrentTargetRect();
    CKRECT srcRect = target;
    if (Src) {
        srcRect.left = (int)Src->left;
        srcRect.top = (int)Src->top;
        srcRect.right = (int)Src->right;
        srcRect.bottom = (int)Src->bottom;
    }
    if (!ValidateRect(&srcRect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKRECT dstRect;
    dstRect.left = 0;
    dstRect.top = 0;
    dstRect.right = resource->Texture.Format.Width;
    dstRect.bottom = resource->Texture.Format.Height;
    if (Dst) {
        dstRect.left = (int)Dst->left;
        dstRect.top = (int)Dst->top;
        dstRect.right = (int)Dst->right;
        dstRect.bottom = (int)Dst->bottom;
    }
    if (!ValidateRect(&dstRect, (CKDWORD)resource->Texture.Format.Width, (CKDWORD)resource->Texture.Format.Height)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    VxImageDescEx image;
    const int size = CopyToMemoryBuffer(&srcRect, VXBUFFER_BACKBUFFER, image);
    if (size <= 0)
        return FALSE;
    std::vector<CKBYTE> pixels((size_t)size);
    image.Image = pixels.data();
    if (CopyToMemoryBuffer(&srcRect, VXBUFFER_BACKBUFFER, image) != size)
        return FALSE;

    const int dstWidth = dstRect.right - dstRect.left;
    const int dstHeight = dstRect.bottom - dstRect.top;
    if (dstWidth != image.Width || dstHeight != image.Height) {
        // Nearest-neighbour resample into the destination rectangle.
        std::vector<CKBYTE> scaled((size_t)dstWidth * dstHeight * 4);
        for (int y = 0; y < dstHeight; ++y) {
            const int sy = (int)((int64_t)y * image.Height / dstHeight);
            const CKDWORD *srcRow = (const CKDWORD *)(pixels.data() + (size_t)sy * image.BytesPerLine);
            CKDWORD *dstRow = (CKDWORD *)(scaled.data() + (size_t)y * dstWidth * 4);
            for (int x = 0; x < dstWidth; ++x)
                dstRow[x] = srcRow[(int)((int64_t)x * image.Width / dstWidth)];
        }
        pixels.swap(scaled);
        image.Width = dstWidth;
        image.Height = dstHeight;
        image.BytesPerLine = dstWidth * 4;
        image.Image = pixels.data();
    }
    return LoadTexture(Texture, image, 0, Face, &dstRect);
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
    const CKRasterizerDeviceStats *device = m_Device ? m_Device->GetStats() : NULL;
    if (device) {
        m_Stats.CpuTimeFrame = device->CpuTimeFrame;
        m_Stats.CpuTimerFreq = device->CpuTimerFreq;
        m_Stats.GpuTimeFrame = device->GpuTimeEnd - device->GpuTimeBegin;
        m_Stats.GpuTimerFreq = device->GpuTimerFreq;
        m_Stats.GpuMemoryMax = device->GpuMemoryMax;
        m_Stats.GpuMemoryUsed = device->GpuMemoryUsed;
    }
    m_Stats.FrameNumber = m_FrameNumber;
    m_Stats.Width = m_Width;
    m_Stats.Height = m_Height;
    return &m_Stats;
}
