// FFPRecordingContext frame flow and fixed-function draw submission.

#include "FFPRecordingContext.h"
#include "CKFFContextState.h"

// ===========================================================================
// Pass management
// ===========================================================================

CKRECT FFPRecordingContext::WindowRect() const
{
    return CKFFMakeRect(m_Width, m_Height);
}

CKRECT FFPRecordingContext::CurrentTargetRect() const
{
    if (m_Target.IsActive())
        return CKFFMakeRect(m_Target.Width, m_Target.Height);
    if (m_Frame.InternalTargets) {
        const FFPRecordingPresentTarget &scene = m_Frame.SceneUsesNative ? m_Present.NativeTarget() : m_Present.SceneTarget();
        return CKFFMakeRect(scene.Width, scene.Height);
    }
    return WindowRect();
}

CKRECT FFPRecordingContext::LogicalTargetRect() const
{
    return m_Target.IsActive()
        ? CKFFMakeRect(m_Target.Width, m_Target.Height) : WindowRect();
}

// Window-pixel rectangle -> pixels of the pass target, rounded outwards so
// scaled edges never leave a gap.
CKRECT FFPRecordingContext::ScaleToPhysical(const CKRECT &Rect) const
{
    return CKFFScaleRect(Rect, LogicalTargetRect(), CurrentPassRect());
}

// Tells the pipeline the logical and physical extents of the current pass
// target so the engine's viewport (window pixels) lands on the right pixels.
void FFPRecordingContext::UpdateTargetExtents()
{
    const CKRECT logical = LogicalTargetRect();
    const CKRECT physical = m_Target.IsActive() ? logical : CurrentPassRect();
    m_FFP.SetTargetExtents((CKDWORD)logical.right, (CKDWORD)logical.bottom,
                           (CKDWORD)physical.right, (CKDWORD)physical.bottom);
}

CKDWORD FFPRecordingContext::CurrentSceneFrameBuffer() const
{
    if (m_Target.IsActive())
        return m_TargetFrameBuffer;
    if (!m_Frame.InternalTargets)
        return 0;
    return m_Frame.SceneUsesNative ? m_Present.NativeTarget().FrameBuffer : m_Present.SceneTarget().FrameBuffer;
}

CKDWORD FFPRecordingContext::OverlayFrameBuffer() const
{
    return m_Frame.InternalTargets ? m_Present.NativeTarget().FrameBuffer : 0;
}

// Frame buffer / rect the next implicit or clear pass goes to: the native
// target during the overlay phase, the scene target (or the user target)
// otherwise.
CKDWORD FFPRecordingContext::CurrentPassFrameBuffer() const
{
    return m_Frame.IsOverlayActive() ? OverlayFrameBuffer() : CurrentSceneFrameBuffer();
}

CKRECT FFPRecordingContext::CurrentPassRect() const
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
// A requested MSAA sample count is never downgraded.
CKBOOL FFPRecordingContext::PrepareFrameTarget()
{
    const bool required = m_Backend->GetCaps().RequiresIntermediateTarget &&
                          !m_Target.IsActive();
    const bool msaaRequired = m_Options.MSAASamples > 1 && !m_Target.IsActive();
    if (m_Frame.TargetDecided)
        return (!required && !msaaRequired) || m_Frame.InternalTargets;
    m_Frame.TargetDecided = TRUE;
    m_Frame.InternalTargets = FALSE;
    m_Frame.SceneUsesNative = FALSE;
    m_Frame.Composited = FALSE;
    const CKRasterizerDeviceCaps &caps = m_Backend->GetCaps();
    if (caps.MaxTextureSize == 0)
        return !required && !msaaRequired;
    const CKDWORD width = CKFFScaledDimension(
        m_Width, m_Options.RenderScale, caps.MaxTextureSize);
    const CKDWORD height = CKFFScaledDimension(
        m_Height, m_Options.RenderScale, caps.MaxTextureSize);
    const CKDWORD nativeWidth = CKFFScaledDimension(m_Width, 1.0f, caps.MaxTextureSize);
    const CKDWORD nativeHeight = CKFFScaledDimension(m_Height, 1.0f, caps.MaxTextureSize);
    const bool sceneUsesNative = m_Options.MSAASamples == 0 && !m_Options.FXAA &&
                                 m_Options.Sharpness == 0.0f &&
                                 width == nativeWidth && height == nativeHeight;
    const CKBOOL sceneReady = sceneUsesNative ? TRUE :
        m_Present.EnsureSceneTarget(width, height, m_Options.MSAASamples);
    const CKDWORD nativeBefore = m_Present.NativeTarget().ColorTexture;
    if (sceneReady && m_Present.EnsureNativeTarget(nativeWidth, nativeHeight) && m_Present.EnsureResources()) {
        m_Frame.InternalTargets = TRUE;
        m_Frame.SceneUsesNative = sceneUsesNative ? TRUE : FALSE;
    } else {
        m_Present.DestroyTargets();
    }
    if (msaaRequired && !m_Frame.InternalTargets)
        Diag(CKRST_DIAG_REJECT_UNSUPPORTED_STATE);
    if (m_Present.NativeTarget().ColorTexture != nativeBefore)
        m_Frame.NativePresented = FALSE;
    m_FFP.SetMultisampledTarget(m_Frame.InternalTargets && !m_Frame.SceneUsesNative &&
                                m_Present.SceneTarget().Samples > 0);
    UpdateTargetExtents();
    return (!required && !msaaRequired) || m_Frame.InternalTargets;
}

CKBOOL FFPRecordingContext::OpenPass(CKDWORD RenderTarget, const CKRECT &Rect, CKDWORD ClearFlags, CKDWORD Color,
                                     float Z, CKDWORD Stencil, const char *Name)
{
    CKRenderPassDesc pass;
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
    m_PassTarget = RenderTarget;
    m_Frame.PassRect = Rect;
    ++m_FramePasses;
    return TRUE;
}

CKBOOL FFPRecordingContext::CanContinuePass(CKDWORD RenderTarget, const CKRECT &Rect) const
{
    return CKFFCanContinuePass(m_Frame, m_PassTarget, RenderTarget, Rect);
}

CKBOOL FFPRecordingContext::EnsureDrawPass()
{
    if (!PrepareFrameTarget()) return FALSE;
    if (m_Frame.PassOpen)
        return TRUE;
    return OpenPass(CurrentPassFrameBuffer(), CurrentPassRect(), 0, 0, 1.0f, 0, "implicit");
}

// Resolve: scene target -> native target (RenderScale / MSAA / FXAA / Sharpness).
CKBOOL FFPRecordingContext::CompositeScene()
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
CKBOOL FFPRecordingContext::PresentInternalTarget(CKPresentSync sync)
{
    if (!m_Frame.InternalTargets)
        return TRUE;
    const FFPRecordingPresentTarget &native = m_Present.NativeTarget();
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

void FFPRecordingContext::ReleaseFrameScratch()
{
}

void FFPRecordingContext::FinishFrame()
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

CKBOOL FFPRecordingContext::Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects)
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

CKBOOL FFPRecordingContext::BeginScene()
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
    m_Frame.Phase = CKFF_FRAME_SCENE;
    return TRUE;
}

CKBOOL FFPRecordingContext::EndScene()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (!m_Frame.IsSceneActive()) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    m_Frame.Phase = CKFF_FRAME_IDLE;
    return TRUE;
}

CKBOOL FFPRecordingContext::BeginOverlayPhase()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Target.IsActive()) {
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
    m_Frame.Phase = CKFF_FRAME_OVERLAY;
    UpdateTargetExtents();
    return TRUE;
}

CKBOOL FFPRecordingContext::BackToFront(CKBOOL VSync)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Frame.IsSceneActive()) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    CKBOOL frameSucceeded = TRUE;
    if (m_Frame.Open) {
        if (!m_Target.IsActive()) {
            if (!CompositeScene()) {
                frameSucceeded = FALSE;
            } else if (PresentInternalTarget(VSync ? CKRST_PRESENT_VSYNC : CKRST_PRESENT_IMMEDIATE)) {
                m_Frame.NativePresented = m_Frame.InternalTargets;
            } else {
                frameSucceeded = FALSE;
            }
        }
    }
    const CKPresentSync mode = m_Target.IsActive() ? CKRST_PRESENT_UNCHANGED
                                      : (VSync ? CKRST_PRESENT_VSYNC : CKRST_PRESENT_IMMEDIATE);
    CKDWORD frameNumber = 0;
    const CKERROR status = m_Backend->Submit(
        mode, !m_Target.IsActive(), &frameNumber);
    m_Frame.Open = FALSE;
    if (status == CK_OK) {
        m_LastDeviceFrame = frameNumber;
        m_BufferUses.MarkSubmitted(m_Backend->GetLastSubmitId());
        m_BufferUses.Retire(m_Backend->GetCompletedSubmitId());
    }
    FinishFrame();
    DeliverReadbacks();
    return frameSucceeded && status == CK_OK ? TRUE : FALSE;
}

// ===========================================================================
// Draws
// ===========================================================================

CKBOOL FFPRecordingContext::ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount)
{
    if (!CKFFValidatePrimitive(Type, ElementCount)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

CKBOOL FFPRecordingContext::CheckDeviceForDraw()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Backend->GetDeviceStatus() != CK_OK) {
        Diag(CKRST_DIAG_REJECT_DEVICE_LOST);
        return FALSE;
    }
    return TRUE;
}

void FFPRecordingContext::CountDraw(VXPRIMITIVETYPE Type, int ElementCount)
{
    CKFFCountDraw(m_FrameDrawCalls, m_FramePrimitives, Type, ElementCount);
}

// Fixed-function draw failures: invalid values are the caller's fault, the
// rest is the backend (program creation / refused draw). Approximated states are
// counted through the APPROX_* / IGNORE_* diagnostics after a successful draw.
CKRST_DIAGNOSTIC FFPRecordingContext::DrawRejectDiagnostic() const
{
    return CKFFDrawRejectDiagnostic(m_FFP);
}

void FFPRecordingContext::RecordDrawApproximations()
{
    CKFFRecordDrawApproximations(
        m_Stats, m_FFP.GetLastDrawApproximationMask());
}

CKBOOL FFPRecordingContext::DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data)
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

CKBOOL FFPRecordingContext::SubmitVertexBuffer(VXPRIMITIVETYPE Type,
                                               CKDWORD VBHandle,
                                               const CKFFVertexBufferData &VB,
                                               CKDWORD IBHandle,
                                               CKDWORD BaseVertex, CKDWORD VertexCount, CKDWORD StartIndex,
                                               CKDWORD IndexCount,
                                               const CKWORD *Indices)
{
    if (!EnsureDrawPass())
        return FALSE;
    const CKBOOL marker = m_Marker.Length() > 0;
    if (marker)
        m_FFP.SetDrawMarker(m_Marker.Str());
    const CKBOOL drawn = m_FFP.DrawVertexBuffer(
        Type, VBHandle, IBHandle, BaseVertex, VertexCount, StartIndex,
        IndexCount, VB.Desc.m_VertexFormat, VB.FormatFlags,
        GetNativeVertexLayout(VB.FormatFlags), Indices);
    if (marker) {
        m_FFP.SetDrawMarker(NULL);
        m_Marker = "";
    }
    if (!drawn) {
        Diag(DrawRejectDiagnostic());
        return FALSE;
    }
    RecordDrawApproximations();
    CountDraw(Type, (IBHandle || Indices) ? (int)IndexCount
                                         : (int)VertexCount);
    return TRUE;
}

CKBOOL FFPRecordingContext::DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount,
                                            CKWORD *Indices, int IndexCount)
{
    if (!CheckDeviceForDraw())
        return FALSE;
    const CKFFVertexBufferData *vb = m_PublicResources.FindVertexBuffer(VB);
    if (!vb) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (VertexCount == 0 ||
        StartVertex >= vb->Desc.m_MaxVertexCount ||
        VertexCount > vb->Desc.m_MaxVertexCount - StartVertex ||
        vb->Locked) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!Indices) {
        if (!ValidatePrimitive(Type, (int)VertexCount))
            return FALSE;
        if (!SubmitVertexBuffer(
                Type, VB, *vb, 0, StartVertex, VertexCount, 0, 0))
            return FALSE;
    } else {
        if (!ValidatePrimitive(Type, IndexCount))
            return FALSE;
        if (!SubmitVertexBuffer(Type, VB, *vb, 0, StartVertex, VertexCount,
                                0, (CKDWORD)IndexCount, Indices))
            return FALSE;
    }
    m_BufferUses.Add(CKRST_BUFFER_VERTEX, VB,
                     StartVertex * vb->NativeStride,
                     VertexCount * vb->NativeStride);
    return TRUE;
}

CKBOOL FFPRecordingContext::DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex,
                                              CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount)
{
    if (!CheckDeviceForDraw())
        return FALSE;
    const CKFFVertexBufferData *vb = m_PublicResources.FindVertexBuffer(VB);
    const CKFFIndexBufferData *ib = m_PublicResources.FindIndexBuffer(IB);
    if (!vb || !ib) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!ValidatePrimitive(Type, IndexCount))
        return FALSE;
    if (vb->Locked || ib->Locked ||
        VertexCount == 0 ||
        MinVertexIndex >= vb->Desc.m_MaxVertexCount ||
        VertexCount > vb->Desc.m_MaxVertexCount - MinVertexIndex ||
        StartIndex > ib->Desc.m_MaxIndexCount ||
        (CKDWORD)IndexCount > ib->Desc.m_MaxIndexCount - StartIndex) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    // D3D7 semantics: indices address the whole vertex buffer, MinVertexIndex
    // and VertexCount only describe the range they touch.
    if (!SubmitVertexBuffer(Type, VB, *vb, IB, 0,
                            MinVertexIndex + VertexCount, StartIndex,
                            (CKDWORD)IndexCount))
        return FALSE;
    m_BufferUses.Add(CKRST_BUFFER_VERTEX, VB,
                     MinVertexIndex * vb->NativeStride,
                     VertexCount * vb->NativeStride);
    m_BufferUses.Add(CKRST_BUFFER_INDEX, IB, StartIndex * 2,
                     (CKDWORD)IndexCount * 2);
    return TRUE;
}
