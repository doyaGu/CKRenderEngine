#include "CKBgfxRasterizerContext.h"

// CKBgfxRasterizerContext frame flow and fixed-function draw submission.

#include <string.h>

// ===========================================================================
// Pass management
// ===========================================================================

CKRECT CKBgfxRasterizerContext::WindowRect() const
{
    return CKFFMakeRect(m_Width, m_Height);
}
CKRECT CKBgfxRasterizerContext::CurrentTargetRect() const
{
    CKRECT rect;
    rect.left = 0;
    rect.top = 0;
    if (m_Target.IsActive()) {
        rect.right = (int)m_Target.Width;
        rect.bottom = (int)m_Target.Height;
    } else if (m_Frame.InternalTargets) {
        const CKBgfxPresentTarget &scene = m_Frame.SceneUsesNative ? m_Present.NativeTarget() : m_Present.SceneTarget();
        rect.right = (int)scene.Width;
        rect.bottom = (int)scene.Height;
    } else {
        rect.right = (int)m_Width;
        rect.bottom = (int)m_Height;
    }
    return rect;
}

CKRECT CKBgfxRasterizerContext::LogicalTargetRect() const
{
    return m_Target.IsActive()
        ? CKFFMakeRect((int)m_Target.Width, (int)m_Target.Height)
        : WindowRect();
}

// Window-pixel rectangle -> pixels of the pass target, rounded outwards so
// scaled edges never leave a gap.
CKRECT CKBgfxRasterizerContext::ScaleToPhysical(const CKRECT &Rect) const
{
    const CKRECT logical = LogicalTargetRect();
    const CKRECT physical = CurrentPassRect();
    return CKFFScaleRect(Rect, logical, physical);
}

// Tells the pipeline the logical and physical extents of the current pass
// target so the engine's viewport (window pixels) lands on the right pixels.
void CKBgfxRasterizerContext::UpdateTargetExtents()
{
    const CKRECT logical = LogicalTargetRect();
    const CKRECT physical = m_Target.IsActive() ? logical : CurrentPassRect();
    m_FFP.SetTargetExtents((CKDWORD)logical.right, (CKDWORD)logical.bottom,
                           (CKDWORD)physical.right, (CKDWORD)physical.bottom);
}

CKDWORD CKBgfxRasterizerContext::CurrentSceneFrameBuffer() const
{
    if (m_Target.IsActive())
        return m_TargetFrameBuffer;
    if (!m_Frame.InternalTargets)
        return 0;
    return m_Frame.SceneUsesNative ? m_Present.NativeTarget().FrameBuffer : m_Present.SceneTarget().FrameBuffer;
}

CKDWORD CKBgfxRasterizerContext::OverlayFrameBuffer() const
{
    return m_Frame.InternalTargets ? m_Present.NativeTarget().FrameBuffer : 0;
}

// Frame buffer / rect the next implicit or clear pass goes to: the native
// target during the overlay phase, the scene target (or the user target)
// otherwise.
CKDWORD CKBgfxRasterizerContext::CurrentPassFrameBuffer() const
{
    return m_Frame.IsOverlayActive() ? OverlayFrameBuffer() : CurrentSceneFrameBuffer();
}

CKRECT CKBgfxRasterizerContext::CurrentPassRect() const
{
    return m_Frame.IsOverlayActive() ? WindowRect() : CurrentTargetRect();
}

// Decides once per frame whether the scene renders into the scaled scene
// framebuffer (RenderScale / FXAA / Sharpness) or straight into the target.
// Prepares the internal targets once per frame. At native size without MSAA or
// postprocessing, scene and overlay draws share the native target and skip the
// identity resolve. Other configurations render through the scaled/MSAA scene
// target before resolving to native size. The final blit remains the only pass
// that touches the swap chain. Without MSAA, an optional internal target may
// fall back to the swap chain. A requested sample count must be preserved.
CKBOOL CKBgfxRasterizerContext::PrepareFrameTarget()
{
    const bool required = GetCaps().RequiresIntermediateTarget && !m_Target.IsActive();
    const bool msaaRequired = m_Options.MSAASamples > 1 && !m_Target.IsActive();
    if (m_Frame.TargetDecided)
        return (!required && !msaaRequired) || m_Frame.InternalTargets;
    m_Frame.TargetDecided = TRUE;
    m_Frame.InternalTargets = FALSE;
    m_Frame.SceneUsesNative = FALSE;
    m_Frame.Composited = FALSE;
    const CKRasterizerDeviceCaps &caps = GetCaps();
    if (caps.MaxTextureSize == 0)
        return !required && !msaaRequired;
    const CKDWORD width = CKFFScaledDimension(m_Width, m_Options.RenderScale, caps.MaxTextureSize);
    const CKDWORD height = CKFFScaledDimension(m_Height, m_Options.RenderScale, caps.MaxTextureSize);
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

CKBOOL CKBgfxRasterizerContext::OpenPass(CKDWORD RenderTarget, const CKRECT &Rect, CKDWORD ClearFlags, CKDWORD Color,
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
    if (BeginPass(&pass) != CK_OK)
        return FALSE;
    m_Frame.Open = TRUE;
    m_Frame.PassOpen = TRUE;
    m_PassTarget = RenderTarget;
    m_Frame.PassRect = Rect;
    ++m_FramePasses;
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::CanContinuePass(CKDWORD RenderTarget, const CKRECT &Rect) const
{
    return CKFFCanContinuePass(m_Frame, m_PassTarget, RenderTarget, Rect);
}

CKBOOL CKBgfxRasterizerContext::EnsureDrawPass()
{
    if (!PrepareFrameTarget()) return FALSE;
    if (m_Frame.PassOpen)
        return TRUE;
    return OpenPass(CurrentPassFrameBuffer(), CurrentPassRect(), 0, 0, 1.0f, 0, "implicit");
}

// Resolve: scene target -> native target (RenderScale / MSAA / FXAA / Sharpness).
CKBOOL CKBgfxRasterizerContext::CompositeScene()
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
CKBOOL CKBgfxRasterizerContext::PresentInternalTarget(CKPresentSync sync)
{
    if (!m_Frame.InternalTargets)
        return TRUE;
    const CKBgfxPresentTarget &native = m_Present.NativeTarget();
    const CKERROR direct = PresentTexture(native.ColorTexture, native.Width, native.Height, sync);
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

void CKBgfxRasterizerContext::FinishFrame()
{
    m_Frame.FinishFrame();
    m_PassTarget = 0;
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

CKBOOL CKBgfxRasterizerContext::Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects)
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

CKBOOL CKBgfxRasterizerContext::BeginScene()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Frame.IsSceneActive() || m_Frame.IsOverlayActive()) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    if (GetDeviceStatus() != CK_OK) {
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

CKBOOL CKBgfxRasterizerContext::EndScene()
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

CKBOOL CKBgfxRasterizerContext::BeginOverlayPhase()
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

CKBOOL CKBgfxRasterizerContext::BackToFront(CKBOOL VSync)
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
    const CKERROR status = Submit(mode, !m_Target.IsActive(), &frameNumber);
    m_Frame.Open = FALSE;
    if (status == CK_OK) {
        m_LastDeviceFrame = frameNumber;
        m_BufferUses.MarkSubmitted(GetLastSubmitId());
        m_BufferUses.Retire(GetCompletedSubmitId());
    }
    FinishFrame();
    DeliverReadbacks();
    return frameSucceeded && status == CK_OK ? TRUE : FALSE;
}

// ===========================================================================
// Draws
// ===========================================================================

CKBOOL CKBgfxRasterizerContext::ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount)
{
    if (!CKFFValidatePrimitive(Type, ElementCount)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::CheckDeviceForDraw()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (GetDeviceStatus() != CK_OK) {
        Diag(CKRST_DIAG_REJECT_DEVICE_LOST);
        return FALSE;
    }
    return TRUE;
}

void CKBgfxRasterizerContext::CountDraw(VXPRIMITIVETYPE Type, int ElementCount)
{
    CKFFCountDraw(m_FrameDrawCalls, m_FramePrimitives, Type, ElementCount);
}

// Fixed-function draw failures: invalid values are the caller's fault, the
// rest is the backend (program creation / refused draw). Approximated states are
// counted through the APPROX_* / IGNORE_* diagnostics after a successful draw.
CKRST_DIAGNOSTIC CKBgfxRasterizerContext::DrawRejectDiagnostic() const
{
    return CKFFDrawRejectDiagnostic(m_FFP);
}

void CKBgfxRasterizerContext::RecordDrawApproximations()
{
    CKFFRecordDrawApproximations(
        m_Stats, m_FFP.GetLastDrawApproximationMask());
}

CKFFProgramBinding CKBgfxRasterizerContext::ResolveNativeFFProgram(
    const CKFFShaderKey &Key)
{
    const CKFFProgramSelection selection = m_ShaderCache.ResolveProgram(Key);
    const CKDWORD variant = (CKDWORD)selection.Variant;
    if (variant >= CKFF_PROGRAM_VARIANT_COUNT)
        return CKFFProgramBinding();

    if (!m_NativeFFPixelShader &&
        CreateShader(&m_ShaderCache.GetPixelShader(),
                     &m_NativeFFPixelShader) != CK_OK)
        return CKFFProgramBinding();
    if (!m_NativeFFVertexShaders[variant] &&
        CreateShader(&m_ShaderCache.GetVertexShader(selection.Variant),
                     &m_NativeFFVertexShaders[variant]) != CK_OK)
        return CKFFProgramBinding();

    if (!m_NativeFFPrograms[variant]) {
        const CKBOOL positionT =
            selection.Variant == CKFF_PROGRAM_POSITIONT ||
            selection.Variant == CKFF_PROGRAM_POSITIONT_CLIP;
        const CKFFProgramDesc desc = CKFFBuildProgramInterface(
            m_NativeFFVertexShaders[variant], m_NativeFFPixelShader,
            m_ShaderCache.GetShaderFormat(), FALSE, positionT);
        if (CreateProgram(&desc, &m_NativeFFPrograms[variant]) != CK_OK)
            return CKFFProgramBinding();
    }
    return CKFFProgramBinding(m_NativeFFPrograms[variant],
                              selection.Specialization);
}

void CKBgfxRasterizerContext::ClearNativeFFPrograms()
{
    for (CKDWORD variant = 0;
         variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
        if (m_NativeFFPrograms[variant])
            DestroyObject(m_NativeFFPrograms[variant], CKRST_OBJ_PROGRAM);
        if (m_NativeFFVertexShaders[variant])
            DestroyObject(m_NativeFFVertexShaders[variant], CKRST_OBJ_SHADER);
        m_NativeFFPrograms[variant] = 0;
        m_NativeFFVertexShaders[variant] = 0;
    }
    if (m_NativeFFPixelShader)
        DestroyObject(m_NativeFFPixelShader, CKRST_OBJ_SHADER);
    m_NativeFFPixelShader = 0;
    m_ShaderCache.Shutdown();
}

CKBOOL CKBgfxRasterizerContext::SubmitPreparedDraw()
{
    const CKFFDraw &draw = m_FFP.GetDraw();
    const CKFFProgramBinding binding = ResolveNativeFFProgram(draw.ShaderKey);
    const CKDWORD vertexLayout = GetNativeVertexLayout(draw.VertexFormat);
    if (!binding.Program || !vertexLayout)
        return m_FFP.FinishDraw(CKERR_INVALIDOPERATION, 0);

    CKFFTextureBindings textures;
    for (CKDWORD i = 0; i < draw.Textures.ActiveTextureCount; ++i) {
        const CKFFTextureBinding &source = draw.Textures.Bindings[i];
        if (!source.Texture || source.Stage >= CKFF_TEXTURE_SLOT_COUNT)
            continue;
        textures[source.Stage].Texture = source.Texture;
        textures[source.Stage].Sampler = source.Sampler;
    }

    CKDrawCommand nativeDraw;
    nativeDraw.Pipeline = draw.Pipeline;
    nativeDraw.Textures = &textures;
    nativeDraw.Constants = draw.Constants;
    nativeDraw.Marker = draw.Marker;
    nativeDraw.Program = binding.Program;
    nativeDraw.Layout = vertexLayout;
    nativeDraw.VertexBuffer = draw.VertexBuffer;
    nativeDraw.StartVertex = draw.StartVertex;
    nativeDraw.VertexCount = draw.VertexCount;
    nativeDraw.IndexBuffer = draw.IndexBuffer;
    nativeDraw.StartIndex = draw.StartIndex;
    nativeDraw.IndexCount = draw.IndexCount;
    nativeDraw.SortKey = draw.SortKey;

    CKTransientVertexData vertices;
    CKTransientIndexData indices;
    if (draw.Vertices) {
        if (!AllocTransientVertices(
                draw.VertexCount, vertexLayout, &vertices) ||
            vertices.Stride != draw.VertexStride)
            return m_FFP.FinishDraw(CKERR_OUTOFMEMORY, 0);
        memcpy(vertices.Data, draw.Vertices,
               (size_t)draw.VertexCount * draw.VertexStride);
        nativeDraw.VertexBuffer = 0;
        nativeDraw.StartVertex = 0;
        nativeDraw.TransientVertices = &vertices;
    }
    if (draw.Indices) {
        if (!AllocTransientIndices(
                draw.IndexCount, draw.Index32, &indices))
            return m_FFP.FinishDraw(CKERR_OUTOFMEMORY, 0);
        memcpy(indices.Data, draw.Indices,
               (size_t)draw.IndexCount * (draw.Index32 ? 4u : 2u));
        nativeDraw.IndexBuffer = 0;
        nativeDraw.StartIndex = 0;
        nativeDraw.TransientIndices = &indices;
    }

    const CKERROR error = Draw(&nativeDraw);
    return m_FFP.FinishDraw(error, GetDrawApproximationMask());
}

CKBOOL CKBgfxRasterizerContext::DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data)
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
    CKBOOL drawn = m_FFP.PreparePrimitive(Type, Indices, elementCount, Data);
    if (drawn)
        drawn = SubmitPreparedDraw();
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

CKBOOL CKBgfxRasterizerContext::SubmitVertexBuffer(VXPRIMITIVETYPE Type, CKDWORD VBHandle,
                                               const VertexBufferData &VB, CKDWORD IBHandle,
                                               CKDWORD BaseVertex, CKDWORD VertexCount, CKDWORD StartIndex,
                                               CKDWORD IndexCount, const CKWORD *Indices)
{
    if (!EnsureDrawPass())
        return FALSE;
    const CKBOOL marker = m_Marker.Length() > 0;
    if (marker)
        m_FFP.SetDrawMarker(m_Marker.Str());
    const CKDWORD vertexLayout = GetNativeVertexLayout(VB.FormatFlags);
    CKBOOL drawn = m_FFP.PrepareVertexBuffer(Type, VBHandle, IBHandle, BaseVertex, VertexCount, StartIndex,
                                             IndexCount, VB.Desc.m_VertexFormat, VB.FormatFlags,
                                             vertexLayout, Indices);
    if (drawn)
        drawn = SubmitPreparedDraw();
    if (marker) {
        m_FFP.SetDrawMarker(NULL);
        m_Marker = "";
    }
    if (!drawn) {
        Diag(DrawRejectDiagnostic());
        return FALSE;
    }
    RecordDrawApproximations();
    CountDraw(Type, (IBHandle || Indices) ? (int)IndexCount : (int)VertexCount);
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount,
                                            CKWORD *Indices, int IndexCount)
{
    if (!CheckDeviceForDraw())
        return FALSE;
    const VertexBufferData *vb = m_PublicResources.FindVertexBuffer(VB);
    if (!vb) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (VertexCount == 0 || StartVertex >= vb->Desc.m_MaxVertexCount ||
        VertexCount > vb->Desc.m_MaxVertexCount - StartVertex || vb->Locked) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (m_FFP.NeedsVertexBufferWrap(vb->Layout.TexcoordCount) ||
        m_FFP.NeedsVertexBufferBlendValidation(vb->FormatFlags) ||
        ((Type == VX_TRIANGLESTRIP || Type == VX_TRIANGLEFAN) &&
         m_FFP.GetRenderState(VXRENDERSTATE_FILLMODE) == VXFILL_POINT) ||
        (Type == VX_POINTLIST &&
         m_FFP.NeedsVertexBufferPointExpansion(vb->Desc.m_VertexFormat))) {
        VxDrawPrimitiveData data;
        XArray<CKBYTE> pointPositions;
        if (Type == VX_POINTLIST)
            vb->SetupPointDrawData(data, StartVertex, VertexCount, pointPositions);
        else
            vb->SetupDrawData(data, StartVertex, VertexCount);
        for (int stage = 0; stage < vb->Layout.TexcoordCount; ++stage)
            m_FFP.SetTexcoordComponentCount(stage, vb->Layout.TexcoordDims[stage]);
        const CKBOOL result = DrawPrimitive(Type, Indices, IndexCount, &data);
        m_FFP.ResetTexcoordComponentCounts();
        return result;
    }
    if (!Indices) {
        if (!ValidatePrimitive(Type, (int)VertexCount))
            return FALSE;
        if (!SubmitVertexBuffer(Type, VB, *vb, 0,
                                StartVertex, VertexCount, 0, 0))
            return FALSE;
    } else {
        if (!ValidatePrimitive(Type, IndexCount))
            return FALSE;
        // The caller owns these indices only for this call. The fixed-function
        // path copies them into the backend's existing transient index storage.
        if (!SubmitVertexBuffer(Type, VB, *vb, 0, StartVertex,
                                VertexCount, 0, (CKDWORD)IndexCount, Indices))
            return FALSE;
    }
    m_BufferUses.Add(CKRST_BUFFER_VERTEX, VB,
                     StartVertex * vb->NativeStride,
                     VertexCount * vb->NativeStride);
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex,
                                              CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount)
{
    if (!CheckDeviceForDraw())
        return FALSE;
    const VertexBufferData *vb = m_PublicResources.FindVertexBuffer(VB);
    const IndexBufferData *ib = m_PublicResources.FindIndexBuffer(IB);
    if (!vb || !ib) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!ValidatePrimitive(Type, IndexCount))
        return FALSE;
    if (vb->Locked || ib->Locked || VertexCount == 0 ||
        MinVertexIndex >= vb->Desc.m_MaxVertexCount ||
        VertexCount > vb->Desc.m_MaxVertexCount - MinVertexIndex ||
        StartIndex > ib->Desc.m_MaxIndexCount ||
        (CKDWORD)IndexCount > ib->Desc.m_MaxIndexCount - StartIndex) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (m_FFP.NeedsVertexBufferWrap(vb->Layout.TexcoordCount) ||
        m_FFP.NeedsVertexBufferBlendValidation(vb->FormatFlags) ||
        ((Type == VX_TRIANGLESTRIP || Type == VX_TRIANGLEFAN) &&
         m_FFP.GetRenderState(VXRENDERSTATE_FILLMODE) == VXFILL_POINT) ||
        (Type == VX_POINTLIST &&
         m_FFP.NeedsVertexBufferPointExpansion(vb->Desc.m_VertexFormat))) {
        VxDrawPrimitiveData data;
        XArray<CKBYTE> pointPositions;
        if (Type == VX_POINTLIST)
            vb->SetupPointDrawData(data, 0, vb->Desc.m_MaxVertexCount,
                                   pointPositions);
        else
            vb->SetupDrawData(data, 0, vb->Desc.m_MaxVertexCount);
        for (int stage = 0; stage < vb->Layout.TexcoordCount; ++stage)
            m_FFP.SetTexcoordComponentCount(stage, vb->Layout.TexcoordDims[stage]);
        const CKBOOL result = DrawPrimitive(
            Type, const_cast<CKWORD *>(ib->DrawIndices(StartIndex)),
            IndexCount, &data);
        m_FFP.ResetTexcoordComponentCounts();
        return result;
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

