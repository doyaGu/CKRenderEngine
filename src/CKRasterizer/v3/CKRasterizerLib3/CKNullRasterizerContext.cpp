#include "CKNullRasterizer.h"

#include <string.h>

namespace {

const CKDWORD kSlotMask = 0x0000FFFFu;
const CKDWORD kGenerationShift = 16;

CKDWORD MakeHandle(CKDWORD Slot, CKDWORD Generation)
{
    return ((Generation & 0x7FFFu) << kGenerationShift) | ((Slot + 1) & kSlotMask);
}

CKDWORD HandleSlot(CKDWORD Handle)
{
    return (Handle & kSlotMask) - 1;
}

CKDWORD HandleGeneration(CKDWORD Handle)
{
    return (Handle >> kGenerationShift) & 0x7FFFu;
}

CKDWORD Fnv1a(const void *Data, size_t Size, CKDWORD Seed = 0x811C9DC5u)
{
    const CKBYTE *bytes = static_cast<const CKBYTE *>(Data);
    CKDWORD hash = Seed;
    for (size_t i = 0; i < Size; ++i) {
        hash ^= bytes[i];
        hash *= 0x01000193u;
    }
    return hash;
}

CKDWORD FullMipCount(CKDWORD Width, CKDWORD Height, CKDWORD Depth)
{
    CKDWORD size = Width > Height ? Width : Height;
    if (Depth > size)
        size = Depth;
    CKDWORD count = 1;
    while (size > 1) {
        size >>= 1;
        ++count;
    }
    return count;
}

CKDWORD LevelSize(CKDWORD Size, int Level)
{
    CKDWORD s = Size >> Level;
    return s == 0 ? 1 : s;
}

int PrimitiveCount(VXPRIMITIVETYPE Type, int Elements)
{
    switch (Type) {
    case VX_POINTLIST:     return Elements;
    case VX_LINELIST:      return Elements / 2;
    case VX_LINESTRIP:     return Elements - 1;
    case VX_TRIANGLELIST:  return Elements / 3;
    case VX_TRIANGLESTRIP: return Elements - 2;
    case VX_TRIANGLEFAN:   return Elements - 2;
    default:               return 0;
    }
}

CKBOOL IsValidBufferType(VXBUFFER_TYPE Buffer)
{
    return Buffer == VXBUFFER_BACKBUFFER || Buffer == VXBUFFER_ZBUFFER ||
           Buffer == VXBUFFER_STENCILBUFFER ? TRUE : FALSE;
}

} // namespace

// ===========================================================================
// Construction
// ===========================================================================

CKNullRasterizerContext::CKNullRasterizerContext()
    : m_Created(FALSE), m_ShuttingDown(FALSE), m_InScene(FALSE), m_OverlayPhase(FALSE),
      m_PassOpen(FALSE), m_PassIndex(0), m_Target(0), m_TargetFace(CKRST_CUBEFACE_XPOS),
      m_TargetWidth(0), m_TargetHeight(0), m_FrameDrawCalls(0), m_FramePrimitives(0),
      m_FramePasses(0), m_FrameClears(0), m_FrameTextureUploads(0), m_FrameBufferUploads(0)
{
    memset(m_RenderStates, 0, sizeof(m_RenderStates));
    memset(m_StageStates, 0, sizeof(m_StageStates));
    memset(m_Textures, 0, sizeof(m_Textures));
    memset(m_Lights, 0, sizeof(m_Lights));
    memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
    memset(&m_Material, 0, sizeof(m_Material));
    memset(&m_Stats, 0, sizeof(m_Stats));
    Vx3DMatrixIdentity(m_Identity);
    for (int i = 0; i < CKRST_MATRIX_SLOT_COUNT; ++i)
        Vx3DMatrixIdentity(m_Matrices[i]);
    for (int i = 0; i < CKRST_MAX_USER_CLIP_PLANES; ++i)
        m_ClipPlanes[i] = VxPlane(VxVector(0.0f, 0.0f, 0.0f), 0.0f);
    m_Stats.CpuTimerFreq = 1;
    m_Stats.GpuTimerFreq = 1;
    InitDefaultRenderStatesValue();
}

CKNullRasterizerContext::~CKNullRasterizerContext()
{
    FlushObjects(CKRST_OBJ_ALL);
}

// ===========================================================================
// Internal helpers
// ===========================================================================

CKNullEvent &CKNullRasterizerContext::PushEvent(CKNullEventKind Kind)
{
    CKNullEvent event;
    event.Kind = Kind;
    event.Pass = m_PassIndex;
    event.Target = m_Target;
    m_Events.PushBack(event);
    return m_Events[m_Events.Size() - 1];
}

void CKNullRasterizerContext::BeginPass(const char *Reason)
{
    if (m_PassOpen)
        EndPass();
    ++m_PassIndex;
    m_PassOpen = TRUE;
    ++m_FramePasses;
    CKNullEvent &event = PushEvent(CKNULL_EVENT_PASS_BEGIN);
    event.A = m_PassIndex;
    event.B = m_Target;
    event.C = (CKDWORD)m_TargetFace;
    event.Name = Reason ? Reason : "";
}

void CKNullRasterizerContext::EndPass()
{
    if (!m_PassOpen)
        return;
    CKNullEvent &event = PushEvent(CKNULL_EVENT_PASS_END);
    event.A = m_PassIndex;
    m_PassOpen = FALSE;
}

CKDWORD CKNullRasterizerContext::HashRenderStates() const
{
    return Fnv1a(m_RenderStates, sizeof(m_RenderStates));
}

CKDWORD CKNullRasterizerContext::HashStageStates() const
{
    return Fnv1a(m_StageStates, sizeof(m_StageStates));
}

void CKNullRasterizerContext::ResetFrameStats()
{
    m_FrameDrawCalls = 0;
    m_FramePrimitives = 0;
    m_FramePasses = 0;
    m_FrameClears = 0;
    m_FrameTextureUploads = 0;
    m_FrameBufferUploads = 0;
}

CKBOOL CKNullRasterizerContext::ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount) const
{
    if ((CKDWORD)Type < (CKDWORD)VX_POINTLIST || (CKDWORD)Type > (CKDWORD)VX_TRIANGLEFAN)
        return FALSE;
    return PrimitiveCount(Type, ElementCount) > 0 ? TRUE : FALSE;
}

CKBOOL CKNullRasterizerContext::RecordDraw(VXPRIMITIVETYPE Type, int ElementCount, CKDWORD VB, CKDWORD IB)
{
    if (!m_PassOpen)
        BeginPass("implicit");
    const int primitives = PrimitiveCount(Type, ElementCount);
    ++m_FrameDrawCalls;
    m_FramePrimitives += (CKDWORD)primitives;

    CKNullEvent &event = PushEvent(CKNULL_EVENT_DRAW);
    event.A = (CKDWORD)Type;
    event.B = (CKDWORD)primitives;
    event.C = VB;
    event.D = IB;
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
        event.Textures[i] = m_Textures[i];
    event.RenderStateHash = HashRenderStates();
    event.StageStateHash = HashStageStates();
    event.Name = m_Marker;
    m_Marker = "";
    return TRUE;
}

// --- Resources -------------------------------------------------------------

CKDWORD CKNullRasterizerContext::AllocateResource(CKDWORD Type)
{
    CKDWORD slot;
    if (m_FreeSlots.Size() > 0) {
        slot = m_FreeSlots[m_FreeSlots.Size() - 1];
        m_FreeSlots.PopBack();
    } else {
        if (m_Resources.Size() >= (int)kSlotMask)
            return 0;
        slot = (CKDWORD)m_Resources.Size();
        m_Resources.PushBack(CKNullResource());
    }
    CKNullResource &resource = m_Resources[(int)slot];
    const CKDWORD generation = (resource.Generation % 0x7FFFu) + 1;
    resource = CKNullResource();
    resource.Type = Type;
    resource.Generation = generation;
    return MakeHandle(slot, generation);
}

void CKNullRasterizerContext::ReleaseResource(CKDWORD Handle)
{
    const CKDWORD slot = HandleSlot(Handle);
    CKNullResource &resource = m_Resources[(int)slot];
    const CKDWORD generation = resource.Generation;
    resource = CKNullResource();
    resource.Generation = generation; // keep advancing so stale handles fail
    m_FreeSlots.PushBack(slot);
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
        if (m_Textures[i] == Handle)
            m_Textures[i] = 0;
    if (m_Target == Handle) {
        m_Target = 0;
        m_TargetFace = CKRST_CUBEFACE_XPOS;
        m_TargetWidth = m_Width;
        m_TargetHeight = m_Height;
    }
}

const CKNullResource *CKNullRasterizerContext::FindResource(CKDWORD Handle, CKDWORD Type) const
{
    if (Handle == 0)
        return NULL;
    const CKDWORD slot = HandleSlot(Handle);
    if (slot >= (CKDWORD)m_Resources.Size())
        return NULL;
    const CKNullResource &resource = m_Resources[(int)slot];
    if (resource.Type == 0 || resource.Generation != HandleGeneration(Handle))
        return NULL;
    if (Type != CKRST_OBJ_ALL && resource.Type != Type)
        return NULL;
    return &resource;
}

CKNullResource *CKNullRasterizerContext::FindResource(CKDWORD Handle, CKDWORD Type)
{
    return const_cast<CKNullResource *>(
        static_cast<const CKNullRasterizerContext *>(this)->FindResource(Handle, Type));
}

const CKNullResource *CKNullRasterizerContext::GetResource(CKDWORD Handle) const
{
    return FindResource(Handle, CKRST_OBJ_ALL);
}

int CKNullRasterizerContext::GetLiveResourceCount(CKDWORD TypeMask) const
{
    int count = 0;
    for (int i = 0; i < m_Resources.Size(); ++i)
        if (m_Resources[i].Type != 0 && (m_Resources[i].Type & TypeMask) != 0)
            ++count;
    return count;
}

const VxMatrix &CKNullRasterizerContext::GetMatrix(VXMATRIX_TYPE Type) const
{
    const int slot = CKRSTMatrixSlot(Type);
    return slot < 0 ? m_Identity : m_Matrices[slot];
}

// ===========================================================================
// Lifecycle
// ===========================================================================

CKBOOL CKNullRasterizerContext::Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height,
                                       int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp)
{
    if (m_Created || Width < 0 || Height < 0)
        return FALSE;
    m_Window = Window;
    m_PosX = (CKDWORD)PosX;
    m_PosY = (CKDWORD)PosY;
    m_Width = (CKDWORD)Width;
    m_Height = (CKDWORD)Height;
    m_Bpp = Bpp < 0 ? 32 : (CKDWORD)Bpp;
    m_ZBpp = Zbpp < 0 ? 24 : (CKDWORD)Zbpp;
    m_StencilBpp = StencilBpp < 0 ? 8 : (CKDWORD)StencilBpp;
    m_Fullscreen = Fullscreen ? TRUE : FALSE;
    m_RefreshRate = (CKDWORD)RefreshRate;
    m_Created = TRUE;
    m_ShuttingDown = FALSE;
    m_Target = 0;
    m_TargetFace = CKRST_CUBEFACE_XPOS;
    m_TargetWidth = m_Width;
    m_TargetHeight = m_Height;
    m_Viewport = CKViewportData();
    m_Viewport.ViewWidth = m_Width;
    m_Viewport.ViewHeight = m_Height;
    m_Stats.Width = m_Width;
    m_Stats.Height = m_Height;
    InitDefaultRenderStatesValue();
    CKNullEvent &event = PushEvent(CKNULL_EVENT_CREATE);
    event.A = m_Width;
    event.B = m_Height;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags)
{
    (void)Flags;
    if (!m_Created || m_InScene || Width < 0 || Height < 0)
        return FALSE;
    m_PosX = (CKDWORD)PosX;
    m_PosY = (CKDWORD)PosY;
    m_Width = (CKDWORD)Width;
    m_Height = (CKDWORD)Height;
    if (m_Target == 0) {
        m_TargetWidth = m_Width;
        m_TargetHeight = m_Height;
    }
    m_Stats.Width = m_Width;
    m_Stats.Height = m_Height;
    CKNullEvent &event = PushEvent(CKNULL_EVENT_RESIZE);
    event.A = m_Width;
    event.B = m_Height;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetOptions(const CKRasterizerOptions *Options)
{
    if (!Options || Options->Size != sizeof(CKRasterizerOptions) || m_InScene)
        return FALSE;
    m_Options = *Options;
    if (m_Options.RenderScale < 0.5f) m_Options.RenderScale = 0.5f;
    if (m_Options.RenderScale > 2.0f) m_Options.RenderScale = 2.0f;
    if (m_Options.Sharpness < 0.0f) m_Options.Sharpness = 0.0f;
    if (m_Options.Sharpness > 1.0f) m_Options.Sharpness = 1.0f;
    PushEvent(CKNULL_EVENT_OPTIONS);
    return TRUE;
}

CKBOOL CKNullRasterizerContext::GetCaps(CKRasterizerCapsDesc *Caps) const
{
    if (!Caps || !m_Created)
        return FALSE;
    CKRasterizerCapsDesc caps;
    caps.Features = CKRST_CAPS_SYNC_READBACK | CKRST_CAPS_MIDFRAME_READBACK | CKRST_CAPS_POINT_SIZE |
                    CKRST_CAPS_DEPTH_BIAS | CKRST_CAPS_STENCIL_WRITE_MASK | CKRST_CAPS_SAMPLER_LOD_CONTROL |
                    CKRST_CAPS_ANISOTROPY_LEVEL | CKRST_CAPS_MSAA | CKRST_CAPS_TEXTURE_CUBE |
                    CKRST_CAPS_TEXTURE_VOLUME | CKRST_CAPS_BORDER_COLOR | CKRST_CAPS_SEPARATE_ALPHA_BLEND |
                    CKRST_CAPS_MIRROR_ONCE | CKRST_CAPS_TEXTURE_DXT;
    caps.MaxTextureSize = m_Driver ? m_Driver->m_3DCaps.MaxTextureWidth : 16384;
    caps.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    caps.MaxAnisotropy = 16;
    caps.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    caps.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
    caps.MaxMSAASamples = 8;
    caps.MaxPointSize = 64.0f;
    caps.MaxLights = CKRST_MAX_LIGHTS;
    *Caps = caps;
    return TRUE;
}

CKERROR CKNullRasterizerContext::GetDeviceStatus() const
{
    return m_Created ? CK_OK : CKERR_NOTINITIALIZED;
}

CKBOOL CKNullRasterizerContext::BeginShutdown()
{
    if (m_InScene)
        return FALSE;
    EndPass();
    m_ShuttingDown = TRUE;
    FlushObjects(CKRST_OBJ_ALL);
    return TRUE;
}

CKBOOL CKNullRasterizerContext::IsIdle() const
{
    return !m_InScene && !m_PassOpen ? TRUE : FALSE;
}

// ===========================================================================
// Frame
// ===========================================================================

CKBOOL CKNullRasterizerContext::Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil,
                                      int RectCount, CKRECT *Rects)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (RectCount < 0 || (RectCount > 0 && !Rects))
        return FALSE;

    CKBOOL resumeScene = FALSE;
    if (m_InScene) {
        // A clear inside the scene is its own pass; the scene continues in a
        // fresh pass afterwards (spec 4.3, ShadowStencil use case).
        BeginPass("clear");
        resumeScene = TRUE;
    } else if (!m_PassOpen) {
        BeginPass("clear");
    }

    ++m_FrameClears;
    const int count = RectCount == 0 ? 1 : RectCount;
    for (int i = 0; i < count; ++i) {
        CKNullEvent &event = PushEvent(CKNULL_EVENT_CLEAR);
        event.A = Flags;
        event.B = Color;
        event.C = Stencil;
        event.F = Z;
        if (RectCount == 0) {
            event.Rect.left = (int)m_Viewport.ViewX;
            event.Rect.top = (int)m_Viewport.ViewY;
            event.Rect.right = (int)(m_Viewport.ViewX + m_Viewport.ViewWidth);
            event.Rect.bottom = (int)(m_Viewport.ViewY + m_Viewport.ViewHeight);
        } else {
            event.Rect = Rects[i];
        }
    }

    if (resumeScene)
        BeginPass("scene");
    return TRUE;
}

CKBOOL CKNullRasterizerContext::BeginScene()
{
    if (!m_Created || m_ShuttingDown || m_InScene)
        return FALSE;
    if (!m_PassOpen)
        BeginPass("scene");
    m_InScene = TRUE;
    PushEvent(CKNULL_EVENT_BEGIN_SCENE);
    return TRUE;
}

CKBOOL CKNullRasterizerContext::EndScene()
{
    if (!m_InScene)
        return FALSE;
    m_InScene = FALSE;
    PushEvent(CKNULL_EVENT_END_SCENE);
    // The pass stays open: callbacks may draw between EndScene and BackToFront.
    return TRUE;
}

CKBOOL CKNullRasterizerContext::BeginOverlayPhase()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Target != 0) {
        Diag(CKRST_DIAG_OVERLAY_ON_TARGET);
        return FALSE;
    }
    if (m_OverlayPhase)
        return FALSE;
    m_OverlayPhase = TRUE;
    BeginPass("present");
    PushEvent(CKNULL_EVENT_BEGIN_OVERLAY);
    BeginPass("overlay");
    return TRUE;
}

CKBOOL CKNullRasterizerContext::BackToFront(CKBOOL VSync)
{
    if (!m_Created || m_InScene)
        return FALSE;
    EndPass();
    ++m_Stats.FrameNumber;
    CKNullEvent &event = PushEvent(CKNULL_EVENT_PRESENT);
    event.A = VSync ? 1 : 0;
    event.B = m_Stats.FrameNumber;
    m_OverlayPhase = FALSE;
    m_Stats.DrawCalls = m_FrameDrawCalls;
    m_Stats.Primitives = m_FramePrimitives;
    m_Stats.Passes = m_FramePasses;
    m_Stats.Clears = m_FrameClears;
    m_Stats.TextureUploads = m_FrameTextureUploads;
    m_Stats.BufferUploads = m_FrameBufferUploads;
    ResetFrameStats();
    return TRUE;
}

// ===========================================================================
// Fixed-function state
// ===========================================================================

CKBOOL CKNullRasterizerContext::SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value)
{
    if (!CKRSTIsValidRenderStateType((CKDWORD)State)) {
        Diag(CKRST_DIAG_INVALID_RENDER_STATE);
        return FALSE;
    }
    m_RenderStates[(CKDWORD)State] = Value;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value)
{
    if (!Value || !CKRSTIsValidRenderStateType((CKDWORD)State))
        return FALSE;
    *Value = m_RenderStates[(CKDWORD)State];
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value)
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
    if (Tss == CKRST_TSS_ADDRESS) {
        m_StageStates[Stage][CKRST_TSS_ADDRESSU] = Value;
        m_StageStates[Stage][CKRST_TSS_ADDRESSV] = Value;
        m_StageStates[Stage][CKRST_TSS_ADDRESW] = Value;
    }
    return TRUE;
}

CKBOOL CKNullRasterizerContext::GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value)
{
    if (!Value || Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES ||
        !CKRSTIsValidTextureStageStateType((CKDWORD)Tss))
        return FALSE;
    *Value = m_StageStates[Stage][(CKDWORD)Tss];
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetTexture(CKDWORD Texture, int Stage)
{
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (Texture != 0 && !FindResource(Texture, CKRST_OBJ_TEXTURE)) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    m_Textures[Stage] = Texture;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat)
{
    const int slot = CKRSTMatrixSlot(Type);
    if (slot < 0) {
        Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    m_Matrices[slot] = Mat;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetLight(CKDWORD Index, const CKLightData *Data)
{
    if (Index >= CKRST_MAX_LIGHTS) {
        Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    if (!Data)
        return FALSE;
    m_Lights[Index] = *Data;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::EnableLight(CKDWORD Index, CKBOOL Enable)
{
    if (Index >= CKRST_MAX_LIGHTS) {
        Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    m_LightEnabled[Index] = Enable ? TRUE : FALSE;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetMaterial(const CKMaterialData *Data)
{
    if (!Data)
        return FALSE;
    m_Material = *Data;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetViewport(const CKViewportData *Data)
{
    if (!Data)
        return FALSE;
    m_Viewport = *Data;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::SetUserClipPlane(CKDWORD Index, const VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES) {
        Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    m_ClipPlanes[Index] = Plane;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::GetUserClipPlane(CKDWORD Index, VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES)
        return FALSE;
    Plane = m_ClipPlanes[Index];
    return TRUE;
}

void CKNullRasterizerContext::InitDefaultRenderStatesValue()
{
    for (CKDWORD state = 0; state < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++state)
        m_RenderStates[state] = CKRSTDefaultRenderStateValue((VXRENDERSTATETYPE)state);
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
        for (CKDWORD tss = 0; tss < (CKDWORD)CKRST_TSS_MAXSTATE; ++tss)
            m_StageStates[stage][tss] = CKRSTDefaultTextureStageStateValue(stage, (CKRST_TEXTURESTAGESTATETYPE)tss);
}

// ===========================================================================
// Draw
// ===========================================================================

CKBOOL CKNullRasterizerContext::DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount,
                                              VxDrawPrimitiveData *Data)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (!Data || Data->VertexCount <= 0 || !Data->PositionPtr || IndexCount < 0 ||
        (IndexCount > 0 && !Indices)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const int elements = IndexCount > 0 ? IndexCount : Data->VertexCount;
    if (!ValidatePrimitive(Type, elements)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (m_Options.DebugFlags & CKRST_DEBUG_IFH)
        return TRUE;
    return RecordDraw(Type, elements, 0, 0);
}

CKBOOL CKNullRasterizerContext::DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex,
                                                CKDWORD VertexCount, CKWORD *Indices, int IndexCount)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const CKNullResource *vb = FindResource(VB, CKRST_OBJ_VERTEXBUFFER);
    if (!vb) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (vb->Locked || VertexCount == 0 || StartVertex > vb->VertexBuffer.m_MaxVertexCount ||
        VertexCount > vb->VertexBuffer.m_MaxVertexCount - StartVertex || IndexCount < 0 ||
        (IndexCount > 0 && !Indices)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const int elements = IndexCount > 0 ? IndexCount : (int)VertexCount;
    if (!ValidatePrimitive(Type, elements)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (m_Options.DebugFlags & CKRST_DEBUG_IFH)
        return TRUE;
    return RecordDraw(Type, elements, VB, 0);
}

CKBOOL CKNullRasterizerContext::DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB,
                                                  CKDWORD MinVertexIndex, CKDWORD VertexCount,
                                                  CKDWORD StartIndex, int IndexCount)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const CKNullResource *vb = FindResource(VB, CKRST_OBJ_VERTEXBUFFER);
    const CKNullResource *ib = FindResource(IB, CKRST_OBJ_INDEXBUFFER);
    if (!vb || !ib) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (vb->Locked || ib->Locked || VertexCount == 0 || IndexCount <= 0 ||
        MinVertexIndex > vb->VertexBuffer.m_MaxVertexCount ||
        VertexCount > vb->VertexBuffer.m_MaxVertexCount - MinVertexIndex ||
        StartIndex > ib->IndexBuffer.m_MaxIndexCount ||
        (CKDWORD)IndexCount > ib->IndexBuffer.m_MaxIndexCount - StartIndex) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!ValidatePrimitive(Type, IndexCount)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (m_Options.DebugFlags & CKRST_DEBUG_IFH)
        return TRUE;
    return RecordDraw(Type, IndexCount, VB, IB);
}

// ===========================================================================
// Resources
// ===========================================================================

CKBOOL CKNullRasterizerContext::CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle)
{
    if (!m_Created || !Desc || !OutHandle)
        return FALSE;
    *OutHandle = 0;
    const CKDWORD width = (CKDWORD)Desc->Format.Width;
    const CKDWORD height = (CKDWORD)Desc->Format.Height;
    const CKDWORD depth = Desc->Depth == 0 ? 1 : Desc->Depth;
    if (Desc->Format.Width <= 0 || Desc->Format.Height <= 0)
        return FALSE;
    const CKBOOL cube = (Desc->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    const CKBOOL volume = (Desc->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0;
    if (cube && volume)
        return FALSE;
    if (cube && width != height)
        return FALSE;
    if (!volume && depth != 1)
        return FALSE;
    if (m_Driver && (width > m_Driver->m_3DCaps.MaxTextureWidth || height > m_Driver->m_3DCaps.MaxTextureHeight))
        return FALSE;

    const CKDWORD handle = AllocateResource(CKRST_OBJ_TEXTURE);
    if (handle == 0)
        return FALSE;
    CKNullResource *resource = FindResource(handle, CKRST_OBJ_TEXTURE);
    resource->Texture = *Desc;
    resource->Texture.Flags |= CKRST_TEXTURE_VALID;
    resource->Texture.Depth = depth;
    if (Desc->MipMapCount == CKRST_MIPMAP_GENERATE)
        resource->Texture.MipMapCount = FullMipCount(width, height, volume ? depth : 1);
    else if (Desc->MipMapCount == 0)
        resource->Texture.MipMapCount = 1;
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel,
                                            CKRST_CUBEFACE Face, const CKRECT *Region)
{
    CKNullResource *resource = FindResource(Texture, CKRST_OBJ_TEXTURE);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKTextureDesc &desc = resource->Texture;
    const CKBOOL cube = (desc.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    if ((CKDWORD)Face >= CKRST_CUBEFACE_COUNT || (!cube && Face != CKRST_CUBEFACE_XPOS)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    // Generated mip chains accept level 0 only; the rasterizer builds the rest.
    const CKDWORD acceptedLevels = resource->LoadedLevelMask == 0 && desc.MipMapCount > 1 &&
                                   (CKDWORD)MipLevel == 0 ? desc.MipMapCount : desc.MipMapCount;
    if (MipLevel < 0 || (CKDWORD)MipLevel >= acceptedLevels || !Image.Image ||
        Image.Width <= 0 || Image.Height <= 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKDWORD levelWidth = LevelSize((CKDWORD)desc.Format.Width, MipLevel);
    const CKDWORD levelHeight = LevelSize((CKDWORD)desc.Format.Height, MipLevel);
    CKDWORD regionWidth = levelWidth;
    CKDWORD regionHeight = levelHeight;
    if (Region) {
        if (Region->left < 0 || Region->top < 0 || Region->right <= Region->left ||
            Region->bottom <= Region->top || (CKDWORD)Region->right > levelWidth ||
            (CKDWORD)Region->bottom > levelHeight) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        regionWidth = (CKDWORD)(Region->right - Region->left);
        regionHeight = (CKDWORD)(Region->bottom - Region->top);
    }
    if ((CKDWORD)Image.Width < regionWidth || (CKDWORD)Image.Height < regionHeight) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (Face == CKRST_CUBEFACE_XPOS && MipLevel < 32)
        resource->LoadedLevelMask |= 1u << MipLevel;
    ++m_FrameTextureUploads;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const
{
    const CKNullResource *resource = FindResource(Texture, CKRST_OBJ_TEXTURE);
    if (!resource || !Desc)
        return FALSE;
    *Desc = resource->Texture;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (!m_Created || !Desc || !OutHandle)
        return FALSE;
    *OutHandle = 0;
    if (Desc->m_MaxVertexCount == 0)
        return FALSE;
    const CKDWORD format = Desc->m_VertexFormat & CKRST_VF_MASK;
    const CKDWORD vertexSize = CKRSTGetVertexSize(format, Desc->m_TexcoordDims);
    if (Desc->m_VertexSize != 0 && Desc->m_VertexSize != vertexSize)
        return FALSE;

    const CKDWORD handle = AllocateResource(CKRST_OBJ_VERTEXBUFFER);
    if (handle == 0)
        return FALSE;
    CKNullResource *resource = FindResource(handle, CKRST_OBJ_VERTEXBUFFER);
    resource->VertexBuffer = *Desc;
    resource->VertexBuffer.m_Flags |= CKRST_VB_VALID;
    resource->VertexBuffer.m_VertexFormat = format;
    resource->VertexBuffer.m_VertexSize = vertexSize;
    resource->VertexBuffer.m_CurrentVCount = 0;
    resource->Storage.Resize((int)(vertexSize * Desc->m_MaxVertexCount));
    if (Data) {
        memcpy(resource->Storage.Begin(), Data, vertexSize * Desc->m_MaxVertexCount);
        resource->VertexBuffer.m_CurrentVCount = Desc->m_MaxVertexCount;
        ++m_FrameBufferUploads;
    } else {
        memset(resource->Storage.Begin(), 0, vertexSize * Desc->m_MaxVertexCount);
    }
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (!m_Created || !Desc || !OutHandle)
        return FALSE;
    *OutHandle = 0;
    if (Desc->m_MaxIndexCount == 0)
        return FALSE;

    const CKDWORD handle = AllocateResource(CKRST_OBJ_INDEXBUFFER);
    if (handle == 0)
        return FALSE;
    CKNullResource *resource = FindResource(handle, CKRST_OBJ_INDEXBUFFER);
    resource->IndexBuffer = *Desc;
    resource->IndexBuffer.m_Flags |= CKRST_VB_VALID;
    resource->IndexBuffer.m_CurrentICount = 0;
    resource->Storage.Resize((int)(Desc->m_MaxIndexCount * sizeof(CKWORD)));
    if (Data) {
        memcpy(resource->Storage.Begin(), Data, Desc->m_MaxIndexCount * sizeof(CKWORD));
        resource->IndexBuffer.m_CurrentICount = Desc->m_MaxIndexCount;
        ++m_FrameBufferUploads;
    } else {
        memset(resource->Storage.Begin(), 0, Desc->m_MaxIndexCount * sizeof(CKWORD));
    }
    *OutHandle = handle;
    return TRUE;
}

void *CKNullRasterizerContext::LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags)
{
    (void)Flags;
    CKNullResource *resource = FindResource(VB, CKRST_OBJ_VERTEXBUFFER);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD max = resource->VertexBuffer.m_MaxVertexCount;
    if (resource->Locked || StartVertex >= max) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    if (VertexCount == 0)
        VertexCount = max - StartVertex;
    if (VertexCount > max - StartVertex) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    resource->Locked = TRUE;
    resource->LockStart = StartVertex;
    resource->LockCount = VertexCount;
    return resource->Storage.Begin() + StartVertex * resource->VertexBuffer.m_VertexSize;
}

CKBOOL CKNullRasterizerContext::UnlockVertexBuffer(CKDWORD VB)
{
    CKNullResource *resource = FindResource(VB, CKRST_OBJ_VERTEXBUFFER);
    if (!resource || !resource->Locked)
        return FALSE;
    resource->Locked = FALSE;
    const CKDWORD end = resource->LockStart + resource->LockCount;
    if (end > resource->VertexBuffer.m_CurrentVCount)
        resource->VertexBuffer.m_CurrentVCount = end;
    ++m_FrameBufferUploads;
    return TRUE;
}

void *CKNullRasterizerContext::LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags)
{
    (void)Flags;
    CKNullResource *resource = FindResource(IB, CKRST_OBJ_INDEXBUFFER);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD max = resource->IndexBuffer.m_MaxIndexCount;
    if (resource->Locked || StartIndex >= max) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    if (IndexCount == 0)
        IndexCount = max - StartIndex;
    if (IndexCount > max - StartIndex) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    resource->Locked = TRUE;
    resource->LockStart = StartIndex;
    resource->LockCount = IndexCount;
    return resource->Storage.Begin() + StartIndex * sizeof(CKWORD);
}

CKBOOL CKNullRasterizerContext::UnlockIndexBuffer(CKDWORD IB)
{
    CKNullResource *resource = FindResource(IB, CKRST_OBJ_INDEXBUFFER);
    if (!resource || !resource->Locked)
        return FALSE;
    resource->Locked = FALSE;
    const CKDWORD end = resource->LockStart + resource->LockCount;
    if (end > resource->IndexBuffer.m_CurrentICount)
        resource->IndexBuffer.m_CurrentICount = end;
    ++m_FrameBufferUploads;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::DeleteObject(CKDWORD Handle, CKDWORD Type)
{
    if (!FindResource(Handle, Type))
        return FALSE;
    ReleaseResource(Handle);
    return TRUE;
}

CKBOOL CKNullRasterizerContext::FlushObjects(CKDWORD TypeMask)
{
    for (int i = 0; i < m_Resources.Size(); ++i) {
        CKNullResource &resource = m_Resources[i];
        if (resource.Type != 0 && (resource.Type & TypeMask) != 0)
            ReleaseResource(MakeHandle((CKDWORD)i, resource.Generation));
    }
    return TRUE;
}

void CKNullRasterizerContext::SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name)
{
    CKNullResource *resource = FindResource(Handle, Type);
    if (resource)
        resource->Name = Name ? Name : "";
}

// ===========================================================================
// Targets, readback, copies
// ===========================================================================

CKBOOL CKNullRasterizerContext::SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_InScene) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    CKDWORD targetWidth = m_Width;
    CKDWORD targetHeight = m_Height;
    if (Texture != 0) {
        const CKNullResource *resource = FindResource(Texture, CKRST_OBJ_TEXTURE);
        if (!resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        const CKTextureDesc &desc = resource->Texture;
        const CKBOOL cube = (desc.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
        if ((desc.Flags & CKRST_TEXTURE_RENDERTARGET) == 0 || (CKDWORD)Face >= CKRST_CUBEFACE_COUNT ||
            (!cube && Face != CKRST_CUBEFACE_XPOS) || Width < 0 || Height < 0 ||
            (CKDWORD)Width > (CKDWORD)desc.Format.Width || (CKDWORD)Height > (CKDWORD)desc.Format.Height) {
            Diag(CKRST_DIAG_INVALID_TARGET);
            return FALSE;
        }
        targetWidth = Width == 0 ? (CKDWORD)desc.Format.Width : (CKDWORD)Width;
        targetHeight = Height == 0 ? (CKDWORD)desc.Format.Height : (CKDWORD)Height;
    } else {
        Face = CKRST_CUBEFACE_XPOS;
    }
    // A target change always starts a new pass.
    EndPass();
    m_Target = Texture;
    m_TargetFace = Face;
    m_TargetWidth = targetWidth;
    m_TargetHeight = targetHeight;
    CKNullEvent &event = PushEvent(CKNULL_EVENT_SET_TARGET);
    event.A = Texture;
    event.B = targetWidth;
    event.C = targetHeight;
    event.D = (CKDWORD)Face;
    return TRUE;
}

CKBOOL CKNullRasterizerContext::CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face)
{
    (void)Src;
    (void)Dst;
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const CKNullResource *resource = FindResource(Texture, CKRST_OBJ_TEXTURE);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    if ((CKDWORD)Face >= CKRST_CUBEFACE_COUNT || (!cube && Face != CKRST_CUBEFACE_XPOS)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKNullEvent &event = PushEvent(CKNULL_EVENT_COPY_TO_TEXTURE);
    event.A = Texture;
    event.D = (CKDWORD)Face;
    return TRUE;
}

int CKNullRasterizerContext::CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image)
{
    if (!m_Created || m_ShuttingDown || !IsValidBufferType(Buffer))
        return 0;
    if (m_InScene) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return 0;
    }
    CKDWORD width = m_TargetWidth;
    CKDWORD height = m_TargetHeight;
    if (Rect) {
        if (Rect->left < 0 || Rect->top < 0 || Rect->right <= Rect->left || Rect->bottom <= Rect->top ||
            (CKDWORD)Rect->right > width || (CKDWORD)Rect->bottom > height)
            return 0;
        width = (CKDWORD)(Rect->right - Rect->left);
        height = (CKDWORD)(Rect->bottom - Rect->top);
    }
    const int bytes = (int)(width * height * 4);
    if (!Image.Image) {
        VxPixelFormat2ImageDesc(_32_ARGB8888, Image);
        Image.Width = (int)width;
        Image.Height = (int)height;
        Image.BytesPerLine = (int)width * 4;
        return bytes;
    }
    if (Image.Width < (int)width || Image.Height < (int)height || Image.BytesPerLine < (int)width * 4)
        return 0;
    for (CKDWORD y = 0; y < height; ++y)
        memset(Image.Image + y * Image.BytesPerLine, 0, width * 4);
    CKNullEvent &event = PushEvent(CKNULL_EVENT_COPY_TO_MEMORY);
    event.A = (CKDWORD)Buffer;
    event.B = (CKDWORD)bytes;
    return bytes;
}

int CKNullRasterizerContext::CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image)
{
    if (!m_Created || m_ShuttingDown || Buffer != VXBUFFER_BACKBUFFER || !Image.Image)
        return 0;
    if (m_InScene) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return 0;
    }
    CKDWORD width = m_TargetWidth;
    CKDWORD height = m_TargetHeight;
    if (Rect) {
        if (Rect->left < 0 || Rect->top < 0 || Rect->right <= Rect->left || Rect->bottom <= Rect->top ||
            (CKDWORD)Rect->right > width || (CKDWORD)Rect->bottom > height)
            return 0;
        width = (CKDWORD)(Rect->right - Rect->left);
        height = (CKDWORD)(Rect->bottom - Rect->top);
    }
    if (Image.Width <= 0 || Image.Height <= 0)
        return 0;
    if (!m_PassOpen)
        BeginPass("copy");
    const int bytes = (int)(width * height * 4);
    CKNullEvent &event = PushEvent(CKNULL_EVENT_COPY_FROM_MEMORY);
    event.A = (CKDWORD)Buffer;
    event.B = (CKDWORD)bytes;
    return bytes;
}

CKBOOL CKNullRasterizerContext::RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer,
                                                CKReadbackCallback Callback, void *User)
{
    if (!m_Created || m_ShuttingDown || !Callback || !IsValidBufferType(Buffer))
        return FALSE;
    CKDWORD width = m_TargetWidth;
    CKDWORD height = m_TargetHeight;
    if (Rect) {
        if (Rect->left < 0 || Rect->top < 0 || Rect->right <= Rect->left || Rect->bottom <= Rect->top ||
            (CKDWORD)Rect->right > width || (CKDWORD)Rect->bottom > height)
            return FALSE;
        width = (CKDWORD)(Rect->right - Rect->left);
        height = (CKDWORD)(Rect->bottom - Rect->top);
    }
    // The NULL rasterizer completes readbacks immediately with a black image.
    XArray<CKBYTE> pixels;
    pixels.Resize((int)(width * height * 4));
    memset(pixels.Begin(), 0, width * height * 4);
    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = (int)width;
    image.Height = (int)height;
    image.BytesPerLine = (int)width * 4;
    image.Image = pixels.Begin();
    Callback(User, Rect, Buffer, &image, TRUE);
    return TRUE;
}

// ===========================================================================
// Diagnostics
// ===========================================================================

void CKNullRasterizerContext::SetDebugMarker(CKSTRING Name)
{
    m_Marker = Name ? Name : "";
    CKNullEvent &event = PushEvent(CKNULL_EVENT_MARKER);
    event.Name = m_Marker;
}

const CKRenderStats *CKNullRasterizerContext::GetStats()
{
    return &m_Stats;
}
