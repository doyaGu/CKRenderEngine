// CKTranslatedContext: lifecycle, fixed-function state access, resources,
// render targets, readback and statistics. The frame flow and the draws live
// in CKTranslatedFrame.cpp.

#include "CKTranslatedRasterizerInternal.h"
#include "CKFFUniformState.h"
#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKDebugLogger.h"
#include <stdio.h>

#include <string.h>

namespace {

CKBOOL SameImageFormat(const VxImageDescEx &a, const VxImageDescEx &b)
{
    return a.BitsPerPixel == b.BitsPerPixel && a.RedMask == b.RedMask && a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask && a.AlphaMask == b.AlphaMask;
}

// The input snapshot also isolates cube faces and crops from the sampling
// footprint. The output target lets ordinary (non-RT) textures receive a
// scaled copy without changing their usage or discarding untouched pixels.
// Backends retain native resources referenced by queued commands after these
// temporary handles are released.
struct TextureCopyScratch {
    CKRasterizerBackend *Backend;
    CKDWORD Source = 0;
    CKDWORD Output = 0;
    CKDWORD Target = 0;

    explicit TextureCopyScratch(CKRasterizerBackend *backend) : Backend(backend) {}
    ~TextureCopyScratch()
    {
        if (Target) Backend->DestroyObject(Target, CKRST_OBJ_RENDERTARGET);
        if (Output) Backend->DestroyObject(Output, CKRST_OBJ_TEXTURE);
        if (Source) Backend->DestroyObject(Source, CKRST_OBJ_TEXTURE);
    }
    TextureCopyScratch(const TextureCopyScratch &) = delete;
    TextureCopyScratch &operator=(const TextureCopyScratch &) = delete;

    CKBOOL Create(const VxImageDescEx &sourceFormat, const VxImageDescEx &outputFormat,
                  int sourceWidth, int sourceHeight, int outputWidth, int outputHeight)
    {
        CKTextureDesc desc;
        desc.Format = sourceFormat;
        desc.Format.Image = NULL;
        desc.Format.Width = sourceWidth;
        desc.Format.Height = sourceHeight;
        desc.Format.BytesPerLine = 0;
        desc.Depth = desc.MipMapCount = 1;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_BLIT_DST;
        if (Backend->CreateTexture(&desc, NULL, &Source) != CK_OK) return FALSE;
        desc.Format = outputFormat;
        desc.Format.Image = NULL;
        desc.Format.Width = outputWidth;
        desc.Format.Height = outputHeight;
        desc.Format.BytesPerLine = 0;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
        if (Backend->CreateTexture(&desc, NULL, &Output) != CK_OK) return FALSE;
        CKBackendRenderTargetDesc target;
        target.ColorTexture = Output;
        return Backend->CreateRenderTarget(&target, &Target) == CK_OK;
    }
};

} // namespace

// ===========================================================================
// Construction / lifecycle
// ===========================================================================

CKTranslatedContext::CKTranslatedContext(CKTranslatedDriver *Driver, CKRasterizerBackend *Backend)
    : m_TranslatedDriver(Driver), m_Backend(Backend), m_Created(FALSE), m_ShuttingDown(FALSE),
      m_FrameNumber(0), m_LastDeviceFrame(0), m_Target(0), m_TargetFace(CKRST_CUBEFACE_XPOS),
      m_TargetWidth(0), m_TargetHeight(0),
      m_TargetFrameBuffer(0), m_TargetDepthTexture(0), m_CopyTexture(0), m_CopyWidth(0), m_CopyHeight(0),
      m_FrameDrawCalls(0), m_FramePrimitives(0), m_FramePasses(0), m_FrameClears(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0), m_LayoutMismatchLogged(FALSE)
{
    m_Driver = Driver;
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
    if (m_Created || !m_Backend || !m_TranslatedDriver)
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
    CKRasterizerBackendDriver *provider = m_TranslatedDriver->GetBackendDriver();
    if (!provider)
        return FALSE;
    provider->GetShaderTargets(init.ShaderTargets);
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
    if (!provider->GetShaderSet(m_Backend->GetCaps(), shaders) || !m_FFP.Init(m_Backend, shaders)) {
        m_Backend->Shutdown();
        return FALSE;
    }
    m_Present.Init(m_Backend, shaders);
    if (m_TranslatedDriver)
        m_TranslatedDriver->SyncCapsFromBackend();

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
    // CKRST_MIPMAP_GENERATE ((CKDWORD)-1) is passed through: the backend treats
    // it as an auto-mip request and builds the chain from level 0. 0 and 1
    // both mean "no mip levels" (spec 4.5).
    CKTextureDesc deviceDesc = *Desc;
    deviceDesc.Flags |= CKRST_TEXTURE_VALID;
    if (deviceDesc.Depth == 0)
        deviceDesc.Depth = 1;
    if (deviceDesc.MipMapCount == 0)
        deviceDesc.MipMapCount = 1;

    CKDWORD handle = 0;
    if (m_Backend->CreateTexture(&deviceDesc, NULL, &handle) != CK_OK || handle == 0) {
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
    if (!Image.Image || Image.Width <= 0 || Image.Height <= 0 || MipLevel < 0 || MipLevel >= 32) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKBOOL volume = (resource->Texture.Flags & CKRST_TEXTURE_VOLUMEMAP) != 0;
    const CKDWORD layers = cube ? CKRST_CUBEFACE_COUNT :
        (volume ? XMax((CKDWORD)1, resource->Texture.Depth >> MipLevel) : 1);
    if ((CKDWORD)Face >= layers) {
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
    if (m_Backend->UpdateTexture(Texture, (CKDWORD)MipLevel, (CKDWORD)Face, Region, &Image) != CK_OK)
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

    // The backend keeps the fixed-function pipeline's own interleaved layout;
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
    resource.DeviceLayout = m_FFP.ResolveVertexLayout(formatFlags);
    if (resource.DeviceLayout == 0 || resource.DeviceStride == 0) {
        if (!m_LayoutMismatchLogged) {
            m_LayoutMismatchLogged = TRUE;
            CK_LOG_FMT("Rasterizer", "no device vertex layout for vertex format 0x%08X (ffp 0x%08X)",
                       Desc->m_VertexFormat, formatFlags);
        }
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    CKBackendBufferDesc deviceDesc;
    deviceDesc.Kind = CKRST_BACKEND_BUFFER_VERTEX;
    deviceDesc.Size = Desc->m_MaxVertexCount * resource.DeviceStride;
    deviceDesc.Stride = resource.DeviceStride;
    deviceDesc.Layout = resource.DeviceLayout;
    deviceDesc.Dynamic = (Desc->m_Flags & CKRST_VB_DYNAMIC) != 0 ? TRUE : FALSE;
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
    deviceDesc.InitialData = deviceData;
    if (m_Backend->CreateBuffer(&deviceDesc, &handle) != CK_OK || handle == 0) {
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
    CKBackendBufferDesc deviceDesc;
    deviceDesc.Kind = CKRST_BACKEND_BUFFER_INDEX;
    deviceDesc.Size = Desc->m_MaxIndexCount * 2;
    deviceDesc.Index32 = FALSE;
    deviceDesc.Dynamic = (Desc->m_Flags & CKRST_VB_DYNAMIC) != 0 ? TRUE : FALSE;
    deviceDesc.InitialData = Data;
    CKDWORD handle = 0;
    if (m_Backend->CreateBuffer(&deviceDesc, &handle) != CK_OK || handle == 0) {
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
    if (m_Backend->UpdateBuffer(CKRST_BACKEND_BUFFER_VERTEX, VB,
                                start * resource->DeviceStride, (CKDWORD)deviceBytes,
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
    if (m_Backend->UpdateBuffer(CKRST_BACKEND_BUFFER_INDEX, IB,
                                resource->LockStart * 2, resource->LockCount * 2,
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
            if (m_FFP.GetTexture(stage) == Handle) {
                m_FFP.SetTexture(stage, 0, 0);
            }
        }
    }
    if (m_Backend)
        m_Backend->DestroyObject(Handle, Type);
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
    if (m_Backend && FindResource(Type, Handle))
        m_Backend->SetObjectName(Handle, Type, Name);
}

// ===========================================================================
// Render targets
// ===========================================================================

void CKTranslatedContext::ReleaseTarget()
{
    if (m_Backend) {
        if (m_TargetFrameBuffer)
            m_Backend->DestroyObject(m_TargetFrameBuffer, CKRST_OBJ_RENDERTARGET);
        if (m_TargetDepthTexture)
            m_Backend->DestroyObject(m_TargetDepthTexture, CKRST_OBJ_TEXTURE);
    }
    m_TargetFrameBuffer = 0;
    m_TargetDepthTexture = 0;
    m_Target = 0;
    m_TargetFace = CKRST_CUBEFACE_XPOS;
    m_TargetWidth = 0;
    m_TargetHeight = 0;
    m_FFP.SetRenderTargetActive(FALSE);
    UpdateTargetExtents();
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
    if (m_Frame.IsSceneActive() || m_Frame.IsOverlayActive()) {
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

    CKBackendDepthDesc depthDesc;
    depthDesc.Width = (CKDWORD)textureWidth;
    depthDesc.Height = (CKDWORD)textureHeight;
    const CKBOOL needsStencil = m_StencilBpp > 0;
    depthDesc.Format = needsStencil ? CKRST_DEPTHFMT_D24S8 : CKRST_DEPTHFMT_D24;
    CKDWORD depthTexture = 0;
    CKERROR depthErr = m_Backend->CreateDepthTexture(&depthDesc, &depthTexture);
    if (depthErr != CK_OK && !needsStencil) {
        depthDesc.Format = CKRST_DEPTHFMT_D16;
        depthErr = m_Backend->CreateDepthTexture(&depthDesc, &depthTexture);
    }
    if (depthErr != CK_OK) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    CKBackendRenderTargetDesc rtDesc;
    rtDesc.ColorTexture = Texture;
    rtDesc.ColorMip = 0;
    rtDesc.ColorLayer = (CKDWORD)Face;
    rtDesc.DepthTexture = depthTexture;
    CKDWORD frameBuffer = 0;
    if (m_Backend->CreateRenderTarget(&rtDesc, &frameBuffer) != CK_OK) {
        m_Backend->DestroyObject(depthTexture, CKRST_OBJ_TEXTURE);
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
    UpdateTargetExtents();
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

CKBOOL CKTranslatedContext::CanReadNativeTarget()
{
    if (!PrepareFrameTarget()) return FALSE;
    const CKPresentTarget &native = m_Present.NativeTarget();
    if (m_Target || !m_Frame.InternalTargets || !native.IsActive())
        return FALSE;
    return m_Present.AcquireReadbackTexture(native.Width, native.Height) != 0;
}

CKBOOL CKTranslatedContext::CanReadTargetTexture()
{
    if (!m_Target)
        return FALSE;
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, m_Target);
    if (!resource || (resource->Texture.Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) != 0)
        return FALSE;
    return m_Present.AcquireReadbackTexture(m_TargetWidth, m_TargetHeight) != 0;
}

CKBOOL CKTranslatedContext::CanReadCurrentTarget()
{
    return m_Target ? CanReadTargetTexture() : CanReadNativeTarget();
}

CKBOOL CKTranslatedContext::BlitForReadback()
{
    const CKDWORD readbackTexture = m_Present.GetReadbackTexture();
    CKDWORD source = 0;
    if (m_Target) {
        source = m_Target;
    } else if (m_Present.NativeTarget().IsActive()) {
        source = m_Present.NativeTarget().ColorTexture;
    }
    if (!source || !readbackTexture)
        return FALSE;
    return m_Backend->Blit(readbackTexture, 0, 0, 0, 0, source, 0, 0, NULL) == CK_OK ? TRUE : FALSE;
}

CKBOOL CKTranslatedContext::IssueTextureReadback(PendingReadback &Readback)
{
    const CKDWORD readbackTexture = m_Present.GetReadbackTexture();
    if (!readbackTexture)
        return FALSE;
    CKReadbackDesc desc;
    if (m_Backend->ReadTexture(readbackTexture, 0, &desc, NULL) != CK_OK || desc.RequiredSize == 0 ||
        desc.Width == 0 || desc.Height == 0 || desc.Format == UNKNOWN_PF)
        return FALSE;
    if (m_Backend->ReadTexture(readbackTexture, 0, &desc, &Readback.Ticket) != CK_OK) {
        Readback.Data.clear();
        return FALSE;
    }
    Readback.Width = desc.Width;
    Readback.Height = desc.Height;
    Readback.Pitch = desc.RowPitch;
    Readback.Format = desc.Format;
    // A target texture was rendered in the D3D (top-down) layout already
    // (spec 5.9): the flip the backend reports for framebuffer writes on
    // bottom-left backends has been done at render time.
    Readback.YFlip = m_Target ? FALSE : desc.YFlip;
    return TRUE;
}

// Resolve a snapshot without ending the logical scene or consuming the resolve
// that will later compose its remaining draws and the overlay.
CKBOOL CKTranslatedContext::ResolveCopySource()
{
    if (m_Target || !m_Frame.Open || m_Frame.Composited)
        return TRUE;
    if (m_Frame.SceneUsesNative)
        return m_Frame.InternalTargets;
    return m_Frame.InternalTargets &&
           OpenPass(OverlayFrameBuffer(), WindowRect(), 0, 0, 1, 0, "snapshot-resolve") &&
           m_Present.SubmitResolve(m_Options.FXAA, m_Options.Sharpness) == CK_OK;
}

CKBOOL CKTranslatedContext::CaptureReadback(PendingReadback &Readback)
{
    const CKBOOL resume = m_Frame.Open;
    const CKDWORD target = CurrentPassFrameBuffer();
    const CKRECT rect = CurrentPassRect();
    const CKBOOL captured = ResolveCopySource() && BlitForReadback() && IssueTextureReadback(Readback);
    // Restore even after a failed snapshot: the next draw must use the logical
    // scene's attachment, viewport and LOAD semantics.
    const CKBOOL restored = !resume || OpenPass(target, rect, 0, 0, 1, 0, "snapshot-resume");
    return captured && restored;
}

CKBOOL CKTranslatedContext::SubmitReadbackFrame(CKBOOL Present, CKBOOL Blit, CKDWORD *FrameNumber)
{
    if (m_Frame.Open)
        return FALSE;
    const CKBOOL passOpen = m_Frame.PassOpen;
    const CKDWORD passes = m_FramePasses;
    CKBOOL ok = TRUE;
    if (Present && !m_Target)
        ok = PresentInternalTarget(CKRST_BACKEND_SYNC_UNCHANGED);
    if (ok && Blit)
        ok = BlitForReadback();
    CKDWORD frame = 0;
    const CKERROR status = m_Backend->Submit(CKBackendSubmitDesc(CKRST_BACKEND_SYNC_UNCHANGED, Present && !m_Target), &frame);
    m_Frame.Open = FALSE;
    m_Frame.PassOpen = passOpen;
    m_FramePasses = passes;
    if (FrameNumber)
        *FrameNumber = frame;
    return ok && status == CK_OK ? TRUE : FALSE;
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
    const CKRECT target = LogicalTargetRect();
    if (!ValidateRect(Rect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!CanReadCurrentTarget()) {
        // Cube-face targets and frames outside the internal targets have no
        // readback source.
        Diag(CKRST_DIAG_REJECT_UNSUPPORTED_STATE);
        return FALSE;
    }
    PendingReadback *readback = new PendingReadback();
    readback->Callback = Callback;
    readback->User = User;
    readback->Buffer = Buffer;
    if (Rect) {
        readback->Rect = *Rect;
        readback->HasRect = TRUE;
    }
    if (!CaptureReadback(*readback)) {
        delete readback;
        return FALSE;
    }
    if (!m_Frame.Open) {
        // Submit resource work between frames while preserving the last
        // presented image. Callback delivery remains at a frame boundary.
        CKDWORD frame = 0;
        if (!SubmitReadbackFrame(m_Frame.NativePresented, FALSE, &frame)) {
            delete readback;
            return FALSE;
        }
        m_LastDeviceFrame = frame;
    }
    m_Readbacks.push_back(readback);
    return TRUE;
}

CKBOOL CKTranslatedContext::CompleteReadback(PendingReadback &readback, CKBOOL wait)
{
    for (;;) {
        const CKBackendReadbackState state = m_Backend->PollReadback(readback.Ticket, wait);
        if (state == CKRST_READBACK_READY) {
            readback.Data = std::move(readback.Ticket->Data);
            readback.Ticket.reset();
            readback.Success = readback.Done = TRUE;
            return TRUE;
        }
        if (state == CKRST_READBACK_FAILED) break;
        if (!wait) return FALSE;
        if (state == CKRST_READBACK_NEEDS_SUBMIT) {
            CKDWORD submission = 0;
            if (!SubmitReadbackFrame(m_Frame.NativePresented, FALSE, &submission)) break;
            m_LastDeviceFrame = submission;
        }
    }
    readback.Ticket.reset();
    readback.Success = FALSE;
    readback.Done = TRUE;
    return FALSE;
}

void CKTranslatedContext::DeliverReadbacks()
{
    std::vector<PendingReadback *> ready;
    for (size_t i = 0; i < m_Readbacks.size();) {
        PendingReadback *pending = m_Readbacks[i];
        if (!pending->Done)
            CompleteReadback(*pending, FALSE);
        if (pending->Done) {
            ready.push_back(pending);
            m_Readbacks.erase(m_Readbacks.begin() + (ptrdiff_t)i);
        } else {
            ++i;
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
    for (size_t i = 0; i < m_Readbacks.size(); ++i) {
        PendingReadback *pending = m_Readbacks[i];
        if (pending->Done)
            continue;
        // The backend retains its storage until completion; cancellation
        // only drops this consumer, so no wait or caller-memory write remains.
        pending->Ticket.reset();
        pending->Done = TRUE;
        pending->Success = FALSE;
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
    if (m_Frame.IsSceneActive() || m_Frame.Open) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return 0;
    }
    const CKRECT target = LogicalTargetRect();
    if (!ValidateRect(Rect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    if (!CanReadCurrentTarget()) {
        Diag(CKRST_DIAG_REJECT_UNSUPPORTED_STATE); // cube-face targets, no internal targets
        return 0;
    }

    PendingReadback readback;
    readback.Buffer = Buffer;
    if (Rect) {
        readback.Rect = *Rect;
        readback.HasRect = TRUE;
    }
    CKDWORD frame = 0;
    if (!CaptureReadback(readback) || !SubmitReadbackFrame(m_Frame.NativePresented, FALSE, &frame))
        return 0;
    m_LastDeviceFrame = frame;
    if (!CompleteReadback(readback, TRUE))
        return 0;

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

    const CKRECT target = LogicalTargetRect();
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

    if (m_Frame.Open) {
        const CKDWORD source = m_Target ? m_Target : m_Present.NativeTarget().ColorTexture;
        if (!source || source == Texture) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        const CKDWORD resumeTarget = CurrentPassFrameBuffer();
        const CKRECT resumeRect = CurrentPassRect();
        CKBOOL copied = FALSE;
        if (ResolveCopySource()) {
            const int sw = srcRect.right - srcRect.left, sh = srcRect.bottom - srcRect.top;
            const int dw = dstRect.right - dstRect.left, dh = dstRect.bottom - dstRect.top;
            if (sw == dw && sh == dh) {
                copied = m_Backend->Blit(Texture, 0, Face, dstRect.left, dstRect.top,
                                         source, 0, m_Target ? m_TargetFace : 0, &srcRect) == CK_OK;
            } else {
                VxImageDescEx sourceFormat;
                if (m_Target) sourceFormat = FindResource(CKRST_OBJ_TEXTURE, m_Target)->Texture.Format;
                else VxPixelFormat2ImageDesc(_32_ARGB8888, sourceFormat);
                TextureCopyScratch scratch(m_Backend);
                const CKRECT outputRect = {0, 0, dw, dh};
                copied = scratch.Create(sourceFormat, resource->Texture.Format, sw, sh, dw, dh) &&
                    m_Backend->Blit(scratch.Source, 0, 0, 0, 0, source, 0,
                                    m_Target ? m_TargetFace : 0, &srcRect) == CK_OK &&
                    OpenPass(scratch.Target, outputRect, 0, 0, 1, 0, "copy-scale") &&
                    m_Present.SubmitCopy(scratch.Source, sw, sh) == CK_OK &&
                    m_Backend->Blit(Texture, 0, Face, dstRect.left, dstRect.top,
                                    scratch.Output, 0, 0, &outputRect) == CK_OK;
            }
        }
        const CKBOOL resumed = OpenPass(resumeTarget, resumeRect, 0, 0, 1, 0, "copy-resume");
        return copied && resumed;
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
        // Match the GPU point sampler: map destination pixel centers to
        // source texels, for both enlarging and shrinking rectangles.
        std::vector<CKBYTE> scaled((size_t)dstWidth * dstHeight * 4);
        for (int y = 0; y < dstHeight; ++y) {
            const int sy = (int)(((int64_t)y * 2 + 1) * image.Height / ((int64_t)dstHeight * 2));
            const CKDWORD *srcRow = (const CKDWORD *)(pixels.data() + (size_t)sy * image.BytesPerLine);
            CKDWORD *dstRow = (CKDWORD *)(scaled.data() + (size_t)y * dstWidth * 4);
            for (int x = 0; x < dstWidth; ++x)
                dstRow[x] = srcRow[(int)(((int64_t)x * 2 + 1) * image.Width / ((int64_t)dstWidth * 2))];
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
