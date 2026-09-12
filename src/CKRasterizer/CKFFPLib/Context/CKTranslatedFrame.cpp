// CKTranslatedContext frame flow and fixed-function draw submission.

#include "CKFFRasterizerContextInternal.h"
#include <math.h>

namespace {

int PrimitiveCount(VXPRIMITIVETYPE Type, int ElementCount)
{
    switch (Type) {
    case VX_POINTLIST:     return ElementCount;
    case VX_LINELIST:      return ElementCount / 2;
    case VX_LINESTRIP:     return ElementCount > 1 ? ElementCount - 1 : 0;
    case VX_TRIANGLELIST:  return ElementCount / 3;
    case VX_TRIANGLESTRIP:
    case VX_TRIANGLEFAN:   return ElementCount > 2 ? ElementCount - 2 : 0;
    default:               return 0;
    }
}
} // namespace

// ===========================================================================
// Pass management
// ===========================================================================

CKRECT CKTranslatedContext::WindowRect() const
{
    CKRECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = (int)m_Width;
    rect.bottom = (int)m_Height;
    return rect;
}

CKRECT CKTranslatedContext::CurrentTargetRect() const
{
    CKRECT rect;
    rect.left = 0;
    rect.top = 0;
    if (m_Target) {
        rect.right = (int)m_TargetWidth;
        rect.bottom = (int)m_TargetHeight;
    } else if (m_Frame.InternalTargets) {
        const CKPresentTarget &scene = m_Frame.SceneUsesNative ? m_Present.NativeTarget() : m_Present.SceneTarget();
        rect.right = (int)scene.Width;
        rect.bottom = (int)scene.Height;
    } else {
        rect.right = (int)m_Width;
        rect.bottom = (int)m_Height;
    }
    return rect;
}

CKRECT CKTranslatedContext::LogicalTargetRect() const
{
    CKRECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = (int)(m_Target ? m_TargetWidth : m_Width);
    rect.bottom = (int)(m_Target ? m_TargetHeight : m_Height);
    return rect;
}

// Window-pixel rectangle -> pixels of the pass target, rounded outwards so
// scaled edges never leave a gap.
CKRECT CKTranslatedContext::ScaleToPhysical(const CKRECT &Rect) const
{
    const CKRECT logical = LogicalTargetRect();
    const CKRECT physical = CurrentPassRect();
    if (logical.right == physical.right && logical.bottom == physical.bottom)
        return Rect;
    if (logical.right <= 0 || logical.bottom <= 0)
        return Rect;
    const double sx = (double)physical.right / (double)logical.right;
    const double sy = (double)physical.bottom / (double)logical.bottom;
    CKRECT scaled;
    scaled.left = (int)floor(Rect.left * sx);
    scaled.top = (int)floor(Rect.top * sy);
    scaled.right = (int)ceil(Rect.right * sx);
    scaled.bottom = (int)ceil(Rect.bottom * sy);
    if (scaled.left < 0) scaled.left = 0;
    if (scaled.top < 0) scaled.top = 0;
    if (scaled.right > physical.right) scaled.right = physical.right;
    if (scaled.bottom > physical.bottom) scaled.bottom = physical.bottom;
    return scaled;
}

// Tells the pipeline the logical and physical extents of the current pass
// target so the engine's viewport (window pixels) lands on the right pixels.
void CKTranslatedContext::UpdateTargetExtents()
{
    const CKRECT logical = LogicalTargetRect();
    const CKRECT physical = m_Target ? logical : CurrentPassRect();
    m_FFP.SetTargetExtents((CKDWORD)logical.right, (CKDWORD)logical.bottom,
                           (CKDWORD)physical.right, (CKDWORD)physical.bottom);
}

CKDWORD CKTranslatedContext::CurrentSceneFrameBuffer() const
{
    if (m_Target)
        return m_TargetFrameBuffer;
    if (!m_Frame.InternalTargets)
        return 0;
    return m_Frame.SceneUsesNative ? m_Present.NativeTarget().FrameBuffer : m_Present.SceneTarget().FrameBuffer;
}

CKDWORD CKTranslatedContext::OverlayFrameBuffer() const
{
    return m_Frame.InternalTargets ? m_Present.NativeTarget().FrameBuffer : 0;
}

// Frame buffer / rect the next implicit or clear pass goes to: the native
// target during the overlay phase, the scene target (or the user target)
// otherwise.
CKDWORD CKTranslatedContext::CurrentPassFrameBuffer() const
{
    return m_Frame.IsOverlayActive() ? OverlayFrameBuffer() : CurrentSceneFrameBuffer();
}

CKRECT CKTranslatedContext::CurrentPassRect() const
{
    return m_Frame.IsOverlayActive() ? WindowRect() : CurrentTargetRect();
}

// Decides once per frame whether the scene renders into the scaled scene
// framebuffer (RenderScale / FXAA / Sharpness) or straight into the target.
// Prepares the internal targets once per frame. At native size without MSAA or
// postprocessing, scene and overlay draws share the native target and skip the
// identity resolve. Other configurations render through the scaled/MSAA scene
// target before resolving to native size. The final blit remains the only pass
// that touches the swap chain. Falls back to drawing straight into the swap
// chain when the backend does not require and cannot provide internal targets.
CKBOOL CKTranslatedContext::PrepareFrameTarget()
{
    const bool required = m_Backend->GetCaps().RequiresIntermediateTarget && !m_Target;
    if (m_Frame.TargetDecided)
        return !required || m_Frame.InternalTargets;
    m_Frame.TargetDecided = TRUE;
    m_Frame.InternalTargets = FALSE;
    m_Frame.SceneUsesNative = FALSE;
    m_Frame.Composited = FALSE;
    const CKBackendCaps &caps = m_Backend->GetCaps();
    if (caps.MaxTextureSize == 0)
        return !required;
    const CKDWORD width = CKPresentStage::ScaledDimension(m_Width, m_Options.RenderScale, caps.MaxTextureSize);
    const CKDWORD height = CKPresentStage::ScaledDimension(m_Height, m_Options.RenderScale, caps.MaxTextureSize);
    const CKDWORD nativeWidth = m_Width < caps.MaxTextureSize ? m_Width : caps.MaxTextureSize;
    const CKDWORD nativeHeight = m_Height < caps.MaxTextureSize ? m_Height : caps.MaxTextureSize;
    const bool sceneUsesNative = m_Options.MSAASamples == 0 && !m_Options.FXAA &&
                                 m_Options.Sharpness == 0.0f &&
                                 width == nativeWidth && height == nativeHeight;
    CKBOOL sceneReady = sceneUsesNative ? TRUE : m_Present.EnsureSceneTarget(width, height, m_Options.MSAASamples);
    if (!sceneUsesNative && !sceneReady && m_Options.MSAASamples > 1) {
        // The backend has no multisampled targets: render single sampled.
        Diag(CKRST_DIAG_APPROX_MSAA);
        sceneReady = m_Present.EnsureSceneTarget(width, height, 0);
    }
    const CKDWORD nativeBefore = m_Present.NativeTarget().ColorTexture;
    if (sceneReady && m_Present.EnsureNativeTarget(nativeWidth, nativeHeight) && m_Present.EnsureResources()) {
        m_Frame.InternalTargets = TRUE;
        m_Frame.SceneUsesNative = sceneUsesNative ? TRUE : FALSE;
    } else {
        m_Present.DestroyTargets();
    }
    if (m_Present.NativeTarget().ColorTexture != nativeBefore)
        m_Frame.NativePresented = FALSE;
    m_FFP.SetMultisampledTarget(m_Frame.InternalTargets && !m_Frame.SceneUsesNative &&
                                m_Present.SceneTarget().Samples > 0);
    UpdateTargetExtents();
    return !required || m_Frame.InternalTargets;
}

CKBOOL CKTranslatedContext::OpenPass(CKDWORD RenderTarget, const CKRECT &Rect, CKDWORD ClearFlags, CKDWORD Color,
                                     float Z, CKDWORD Stencil, const char *Name)
{
    CKBackendPassDesc pass;
    pass.RenderTarget = RenderTarget;
    pass.Rect = Rect;
    pass.ClearFlags = ClearFlags;
    pass.ClearColor = Color;
    pass.ClearZ = Z;
    pass.ClearStencil = Stencil;
    pass.Name = Name;
    if (m_Backend->BeginPass(&pass) != CK_OK)
        return FALSE;
    m_Frame.Open = TRUE;
    m_Frame.PassOpen = TRUE;
    m_Frame.PassTarget = RenderTarget;
    m_Frame.PassRect = Rect;
    ++m_FramePasses;
    return TRUE;
}

CKBOOL CKTranslatedContext::CanContinuePass(CKDWORD RenderTarget, const CKRECT &Rect) const
{
    const CKRECT &open = m_Frame.PassRect;
    return m_Frame.PassOpen && m_Frame.PassTarget == RenderTarget &&
           open.left == Rect.left && open.top == Rect.top &&
           open.right == Rect.right && open.bottom == Rect.bottom;
}

CKBOOL CKTranslatedContext::EnsureDrawPass()
{
    if (!PrepareFrameTarget()) return FALSE;
    if (m_Frame.PassOpen)
        return TRUE;
    return OpenPass(CurrentPassFrameBuffer(), CurrentPassRect(), 0, 0, 1.0f, 0, "implicit");
}

// Resolve: scene target -> native target (RenderScale / MSAA / FXAA / Sharpness).
CKBOOL CKTranslatedContext::CompositeScene()
{
    if (!m_Frame.InternalTargets || m_Frame.Composited)
        return TRUE;
    if (m_Frame.SceneUsesNative) {
        m_Frame.Composited = TRUE;
        return TRUE;
    }
    if (!OpenPass(OverlayFrameBuffer(), WindowRect(), 0, 0, 1.0f, 0, "composite"))
        return FALSE;
    if (m_Present.SubmitResolve(m_Options.FXAA, m_Options.Sharpness) != CK_OK)
        return FALSE;
    m_Frame.Composited = TRUE;
    return TRUE;
}

// Present: native target -> swap chain. A backend may encode this as a direct
// texture blit; otherwise the portable fullscreen pass is used.
CKBOOL CKTranslatedContext::PresentInternalTarget(CKBackendPresentSync sync)
{
    if (!m_Frame.InternalTargets)
        return TRUE;
    const CKPresentTarget &native = m_Present.NativeTarget();
    const CKERROR direct = m_Backend->PresentTexture(native.ColorTexture, native.Width, native.Height, sync);
    if (direct == CK_OK) {
        m_Frame.Open = TRUE;
        m_Frame.PassOpen = FALSE;
        ++m_FramePasses;
        return TRUE;
    }
    if (direct != CKERR_NOTIMPLEMENTED)
        return FALSE;
    if (!OpenPass(0, WindowRect(), 0, 0, 1.0f, 0, "present"))
        return FALSE;
    return m_Present.SubmitBlit() == CK_OK ? TRUE : FALSE;
}

void CKTranslatedContext::ReleaseFrameScratch()
{
    for (size_t i = 0; i < m_FrameIndexBuffers.size(); ++i)
        m_Backend->DestroyObject(m_FrameIndexBuffers[i], CKRST_OBJ_INDEXBUFFER);
    m_FrameIndexBuffers.clear();
}

void CKTranslatedContext::FinishFrame()
{
    ReleaseFrameScratch();
    m_Frame.FinishFrame();
    UpdateTargetExtents();
    ++m_FrameNumber;
    m_FFP.SetFrameNumber(m_FrameNumber);

    m_Stats.DrawCalls = m_FrameDrawCalls;
    m_Stats.Primitives = m_FramePrimitives;
    m_Stats.Passes = m_FramePasses;
    m_Stats.Clears = m_FrameClears;
    m_Stats.TextureUploads = m_FrameTextureUploads;
    m_Stats.BufferUploads = m_FrameBufferUploads;
    m_FrameDrawCalls = m_FramePrimitives = m_FramePasses = m_FrameClears = 0;
    m_FrameTextureUploads = m_FrameBufferUploads = 0;
}

// ===========================================================================
// Frame
// ===========================================================================

CKBOOL CKTranslatedContext::Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    Flags &= CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL;
    if (Flags == 0)
        return TRUE;
    if (RectCount > 0 && !Rects) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!PrepareFrameTarget()) return FALSE;

    // RectCount == 0 clears the current viewport (D3D7 semantics); otherwise
    // every rectangle gets a clear pass of its own. Rectangles are engine
    // (window) pixels and get scaled to the pass target.
    const CKRECT target = LogicalTargetRect();
    CKRECT viewportRect;
    const CKViewportData &viewport = m_FFP.GetViewport();
    viewportRect.left = (int)viewport.ViewX;
    viewportRect.top = (int)viewport.ViewY;
    viewportRect.right = (int)(viewport.ViewX + viewport.ViewWidth);
    viewportRect.bottom = (int)(viewport.ViewY + viewport.ViewHeight);
    const int count = RectCount > 0 ? RectCount : 1;
    const CKDWORD frameBuffer = CurrentPassFrameBuffer();
    CKBOOL cleared = FALSE;
    for (int i = 0; i < count; ++i) {
        CKRECT rect = RectCount > 0 ? Rects[i] : viewportRect;
        if (rect.left < 0) rect.left = 0;
        if (rect.top < 0) rect.top = 0;
        if (rect.right > target.right) rect.right = target.right;
        if (rect.bottom > target.bottom) rect.bottom = target.bottom;
        if (rect.right <= rect.left || rect.bottom <= rect.top)
            continue;
        if (!OpenPass(frameBuffer, ScaleToPhysical(rect), Flags, Color, Z, Stencil, "clear"))
            return FALSE;
        ++m_FrameClears;
        cleared = TRUE;
    }
    if (cleared && m_Frame.IsSceneActive()) {
        // Mid-scene clear: the following draws need a pass of their own so
        // the clear stays at the call position.
        if (!OpenPass(frameBuffer, CurrentPassRect(), 0, 0, 1.0f, 0, "scene"))
            return FALSE;
    }
    return TRUE;
}

CKBOOL CKTranslatedContext::BeginScene()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Frame.IsSceneActive() || m_Frame.IsOverlayActive()) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    if (m_Backend->GetDeviceStatus() != CK_OK) {
        Diag(CKRST_DIAG_REJECT_DEVICE_LOST);
        return FALSE;
    }
    DeliverReadbacks();
    if (!PrepareFrameTarget()) return FALSE;
    m_FFP.BeginDebugFrame();
    const CKDWORD target = CurrentSceneFrameBuffer();
    const CKRECT rect = CurrentTargetRect();
    if (!CanContinuePass(target, rect) && !OpenPass(target, rect, 0, 0, 1.0f, 0, "scene"))
        return FALSE;
    m_Frame.Phase = CKTRANSLATED_FRAME_SCENE;
    return TRUE;
}

CKBOOL CKTranslatedContext::EndScene()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (!m_Frame.IsSceneActive()) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    m_Frame.Phase = CKTRANSLATED_FRAME_IDLE;
    return TRUE;
}

CKBOOL CKTranslatedContext::BeginOverlayPhase()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Target) {
        Diag(CKRST_DIAG_OVERLAY_ON_TARGET);
        return FALSE;
    }
    if (m_Frame.IsSceneActive() || m_Frame.IsOverlayActive()) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    if (!PrepareFrameTarget()) return FALSE;
    if (!CompositeScene())
        return FALSE;
    // The default native-size path already has the same attachment and extent
    // open. Keep recording into it; draw state is carried by each packet, so a
    // render-pass split would only force another upload/encode batch.
    const CKDWORD target = OverlayFrameBuffer();
    const CKRECT rect = WindowRect();
    if (!CanContinuePass(target, rect) && !OpenPass(target, rect, 0, 0, 1.0f, 0, "overlay"))
        return FALSE;
    m_Frame.Phase = CKTRANSLATED_FRAME_OVERLAY;
    UpdateTargetExtents();
    return TRUE;
}

CKBOOL CKTranslatedContext::BackToFront(CKBOOL VSync)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Frame.IsSceneActive()) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    CKBOOL frameSucceeded = TRUE;
    if (m_Frame.Open) {
        if (!m_Target) {
            if (!CompositeScene()) {
                frameSucceeded = FALSE;
            } else if (PresentInternalTarget(VSync ? CKRST_BACKEND_SYNC_VSYNC : CKRST_BACKEND_SYNC_IMMEDIATE)) {
                m_Frame.NativePresented = m_Frame.InternalTargets;
            } else {
                frameSucceeded = FALSE;
            }
        }
    }
    const CKBackendPresentSync mode = m_Target ? CKRST_BACKEND_SYNC_UNCHANGED
                                      : (VSync ? CKRST_BACKEND_SYNC_VSYNC : CKRST_BACKEND_SYNC_IMMEDIATE);
    CKDWORD frameNumber = 0;
    const CKERROR status = m_Backend->Submit(CKBackendSubmitDesc(mode, !m_Target), &frameNumber);
    m_Frame.Open = FALSE;
    if (status == CK_OK)
        m_LastDeviceFrame = frameNumber;
    FinishFrame();
    DeliverReadbacks();
    return frameSucceeded && status == CK_OK ? TRUE : FALSE;
}

// ===========================================================================
// Draws
// ===========================================================================

CKBOOL CKTranslatedContext::ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount)
{
    int minimum = 0;
    switch (Type) {
    case VX_POINTLIST:     minimum = 1; break;
    case VX_LINELIST:
    case VX_LINESTRIP:     minimum = 2; break;
    case VX_TRIANGLELIST:
    case VX_TRIANGLESTRIP:
    case VX_TRIANGLEFAN:   minimum = 3; break;
    default:
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (ElementCount < minimum) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

CKBOOL CKTranslatedContext::CheckDeviceForDraw()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Backend->GetDeviceStatus() != CK_OK) {
        Diag(CKRST_DIAG_REJECT_DEVICE_LOST);
        return FALSE;
    }
    return TRUE;
}

void CKTranslatedContext::CountDraw(VXPRIMITIVETYPE Type, int ElementCount)
{
    ++m_FrameDrawCalls;
    m_FramePrimitives += (CKDWORD)PrimitiveCount(Type, ElementCount);
}

// Fixed-function draw failures: invalid values are the caller's fault, the
// rest is the backend (program creation / refused draw). Approximated states are
// counted through the APPROX_* / IGNORE_* diagnostics after a successful draw.
CKRST_DIAGNOSTIC CKTranslatedContext::DrawRejectDiagnostic() const
{
    switch (m_FFP.GetLastDrawRejectReason()) {
    case CKFF_DRAW_REJECT_INVALID_INPUT:
    case CKFF_DRAW_REJECT_TEXTURE_OP:
    case CKFF_DRAW_REJECT_STATE_VALUE:
        return CKRST_DIAG_REJECT_INVALID_PARAMETER;
    default:
        return CKRST_DIAG_REJECT_UNSUPPORTED_STATE;
    }
}

void CKTranslatedContext::RecordDrawApproximations()
{
    uint64_t mask = m_FFP.GetLastDrawApproximationMask();
    for (CKDWORD code = 0; mask != 0 && code < CKRST_DIAG_COUNT; ++code, mask >>= 1) {
        if ((mask & 1ull) != 0)
            Diag((CKRST_DIAGNOSTIC)code);
    }
}

CKBOOL CKTranslatedContext::DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data)
{
    if (!CheckDeviceForDraw())
        return FALSE;
    if (!Data || Data->VertexCount <= 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const int elementCount = Indices ? IndexCount : Data->VertexCount;
    if (!ValidatePrimitive(Type, elementCount))
        return FALSE;
    if (!EnsureDrawPass())
        return FALSE;
    const CKBOOL marker = m_Marker.Length() > 0;
    if (marker)
        m_FFP.SetDrawMarker(m_Marker.Str());
    const CKBOOL drawn = m_FFP.DrawPrimitive(Type, Indices, elementCount, Data);
    if (marker) {
        m_FFP.SetDrawMarker(NULL);
        m_Marker = "";
    }
    if (!drawn) {
        Diag(DrawRejectDiagnostic());
        return FALSE;
    }
    RecordDrawApproximations();
    CountDraw(Type, elementCount);
    return TRUE;
}

CKBOOL CKTranslatedContext::SubmitVertexBuffer(VXPRIMITIVETYPE Type, const Resource &VB, CKDWORD IBHandle,
                                               CKDWORD BaseVertex, CKDWORD VertexCount, CKDWORD StartIndex,
                                               CKDWORD IndexCount)
{
    if (!EnsureDrawPass())
        return FALSE;
    const CKBOOL marker = m_Marker.Length() > 0;
    if (marker)
        m_FFP.SetDrawMarker(m_Marker.Str());
    const CKBOOL drawn = m_FFP.DrawVertexBuffer(Type, VB.Handle, IBHandle, BaseVertex, VertexCount, StartIndex,
                                                IndexCount, VB.VertexBuffer.m_VertexFormat, VB.FormatFlags,
                                                VB.DeviceLayout);
    if (marker) {
        m_FFP.SetDrawMarker(NULL);
        m_Marker = "";
    }
    if (!drawn) {
        Diag(DrawRejectDiagnostic());
        return FALSE;
    }
    RecordDrawApproximations();
    CountDraw(Type, IBHandle ? (int)IndexCount : (int)VertexCount);
    return TRUE;
}

CKBOOL CKTranslatedContext::DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount,
                                            CKWORD *Indices, int IndexCount)
{
    if (!CheckDeviceForDraw())
        return FALSE;
    const Resource *vb = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    if (!vb) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (VertexCount == 0 || StartVertex + VertexCount > vb->VertexBuffer.m_MaxVertexCount || vb->Locked) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!Indices) {
        if (!ValidatePrimitive(Type, (int)VertexCount))
            return FALSE;
        return SubmitVertexBuffer(Type, *vb, 0, StartVertex, VertexCount, 0, 0);
    }
    if (!ValidatePrimitive(Type, IndexCount))
        return FALSE;
    // Indices are relative to StartVertex; upload them for this frame.
    CKBackendBufferDesc desc;
    desc.Kind = CKRST_BACKEND_BUFFER_INDEX;
    desc.Size = (CKDWORD)IndexCount * 2;
    desc.Index32 = FALSE;
    desc.Dynamic = TRUE;
    desc.InitialData = Indices;
    CKDWORD ib = 0;
    if (m_Backend->CreateBuffer(&desc, &ib) != CK_OK || ib == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    m_FrameIndexBuffers.push_back(ib);
    ++m_FrameBufferUploads;
    return SubmitVertexBuffer(Type, *vb, ib, StartVertex, VertexCount, 0, (CKDWORD)IndexCount);
}

CKBOOL CKTranslatedContext::DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex,
                                              CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount)
{
    if (!CheckDeviceForDraw())
        return FALSE;
    const Resource *vb = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    const Resource *ib = FindResource(CKRST_OBJ_INDEXBUFFER, IB);
    if (!vb || !ib) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!ValidatePrimitive(Type, IndexCount))
        return FALSE;
    if (vb->Locked || ib->Locked || VertexCount == 0 ||
        MinVertexIndex + VertexCount > vb->VertexBuffer.m_MaxVertexCount ||
        StartIndex + (CKDWORD)IndexCount > ib->IndexBuffer.m_MaxIndexCount) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    // D3D7 semantics: indices address the whole vertex buffer, MinVertexIndex
    // and VertexCount only describe the range they touch.
    return SubmitVertexBuffer(Type, *vb, IB, 0, MinVertexIndex + VertexCount, StartIndex, (CKDWORD)IndexCount);
}
