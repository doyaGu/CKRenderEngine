// CKTranslatedContext: frame flow (one device view per pass), draws and the
// backbuffer upload. See CKTranslatedRasterizer.h.

#include "CKTranslatedRasterizer.h"
#include "CKDebugLogger.h"

#include <string.h>

namespace {

CKBOOL SameImageFormat(const VxImageDescEx &a, const VxImageDescEx &b)
{
    return a.BitsPerPixel == b.BitsPerPixel && a.RedMask == b.RedMask && a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask && a.AlphaMask == b.AlphaMask;
}

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
// Pass / view management
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
    } else if (m_InternalTargets) {
        rect.right = (int)m_Postprocess.SceneTarget().Width;
        rect.bottom = (int)m_Postprocess.SceneTarget().Height;
    } else {
        rect.right = (int)m_Width;
        rect.bottom = (int)m_Height;
    }
    return rect;
}

CKDWORD CKTranslatedContext::CurrentSceneFrameBuffer() const
{
    if (m_Target)
        return m_TargetFrameBuffer;
    return m_InternalTargets ? m_Postprocess.SceneTarget().FrameBuffer : 0;
}

CKDWORD CKTranslatedContext::OverlayFrameBuffer() const
{
    return m_InternalTargets ? m_Postprocess.NativeTarget().FrameBuffer : 0;
}

// Frame buffer / rect the next implicit or clear pass goes to: the native
// target during the overlay phase, the scene target (or the user target)
// otherwise.
CKDWORD CKTranslatedContext::CurrentPassFrameBuffer() const
{
    return m_OverlayPhase ? OverlayFrameBuffer() : CurrentSceneFrameBuffer();
}

CKRECT CKTranslatedContext::CurrentPassRect() const
{
    return m_OverlayPhase ? WindowRect() : CurrentTargetRect();
}

// Decides once per frame whether the scene renders into the scaled scene
// framebuffer (RenderScale / FXAA / Sharpness) or straight into the target.
// Prepares the internal targets once per frame: the scene target (window size
// x RenderScale, MSAA) receives every pass that does not go to a user target,
// its resolve lands on the native target and the frame finally blits the
// native target to the swap chain. Falls back to drawing straight into the
// swap chain when the device cannot provide the targets.
void CKTranslatedContext::PrepareFrameTarget()
{
    if (m_FrameTargetDecided)
        return;
    m_FrameTargetDecided = TRUE;
    m_InternalTargets = FALSE;
    m_Composited = FALSE;
    CKRasterizerDeviceCapsDesc caps;
    if (m_Device->GetCaps(&caps) != CK_OK || caps.MaxTextureSize == 0)
        return;
    const CKDWORD width = CKPostprocessPass::ScaledDimension(m_Width, m_Options.RenderScale, caps.MaxTextureSize);
    const CKDWORD height = CKPostprocessPass::ScaledDimension(m_Height, m_Options.RenderScale, caps.MaxTextureSize);
    CKBOOL sceneReady = m_Postprocess.EnsureSceneTarget(width, height, m_Options.MSAASamples);
    if (!sceneReady && m_Options.MSAASamples > 1) {
        // The device has no multisampled targets: render single sampled.
        Diag(CKRST_DIAG_APPROX_MSAA);
        sceneReady = m_Postprocess.EnsureSceneTarget(width, height, 0);
    }
    const CKDWORD nativeWidth = m_Width < caps.MaxTextureSize ? m_Width : caps.MaxTextureSize;
    const CKDWORD nativeHeight = m_Height < caps.MaxTextureSize ? m_Height : caps.MaxTextureSize;
    if (sceneReady && m_Postprocess.EnsureNativeTarget(nativeWidth, nativeHeight) && m_Postprocess.EnsureResources())
        m_InternalTargets = TRUE;
    else
        m_Postprocess.DestroyTargets();
    m_FFP.SetMultisampledTarget(m_InternalTargets && m_Postprocess.SceneTarget().Samples > 0);
}

CKBOOL CKTranslatedContext::EnsureEncoder()
{
    if (m_Encoder)
        return TRUE;
    m_Encoder = m_Device->BeginEncoder();
    return m_Encoder != NULL;
}

CKBOOL CKTranslatedContext::OpenPass(CKDWORD FrameBuffer, const CKRECT &Rect, CKDWORD ClearFlags, CKDWORD Color,
                                     float Z, CKDWORD Stencil, const char *Name)
{
    if (!EnsureEncoder())
        return FALSE;
    CKRenderView view;
    if (m_NextView < CKRST_MAX_RENDER_VIEWS) {
        view = (CKRenderView)m_NextView++;
    } else {
        // Out of device views: keep drawing into the last one. Clears would
        // apply to the whole pass, so drop them.
        view = m_CurrentView;
        ClearFlags = 0;
    }
    m_Device->SetViewMode(view, CKRST_VIEWMODE_SEQUENTIAL);
    m_Device->SetViewFrameBuffer(view, FrameBuffer);
    m_Device->SetViewRect(view, Rect);
    m_Device->SetViewClear(view, ClearFlags, Color, Z, Stencil);
    if (m_Options.DebugFlags & CKRST_DEBUG_DRAWMAP)
        m_Device->SetViewName(view, (CKSTRING)Name);
    m_Encoder->Touch(view);
    m_CurrentView = view;
    m_PassOpen = TRUE;
    ++m_FramePasses;
    return m_Encoder->GetStatus() == CK_OK ? TRUE : FALSE;
}

CKBOOL CKTranslatedContext::EnsureDrawPass()
{
    PrepareFrameTarget();
    if (m_PassOpen)
        return TRUE;
    return OpenPass(CurrentPassFrameBuffer(), CurrentPassRect(), 0, 0, 1.0f, 0, "implicit");
}

// Resolve: scene target -> native target (RenderScale / MSAA / FXAA / Sharpness).
CKBOOL CKTranslatedContext::CompositeScene()
{
    if (!m_InternalTargets || m_Composited)
        return TRUE;
    if (!OpenPass(OverlayFrameBuffer(), WindowRect(), 0, 0, 1.0f, 0, "composite"))
        return FALSE;
    if (m_Postprocess.SubmitResolve(m_Encoder, m_CurrentView, m_Options.FXAA, m_Options.Sharpness) != CK_OK)
        return FALSE;
    m_Composited = TRUE;
    return TRUE;
}

// Present: native target -> swap chain, the only pass that touches frame
// buffer 0 in a frame that renders through the internal targets.
CKBOOL CKTranslatedContext::PresentInternalTarget()
{
    if (!m_InternalTargets)
        return TRUE;
    if (!OpenPass(0, WindowRect(), 0, 0, 1.0f, 0, "present"))
        return FALSE;
    return m_Postprocess.SubmitBlit(m_Encoder, m_CurrentView) == CK_OK ? TRUE : FALSE;
}

void CKTranslatedContext::ReleaseFrameScratch()
{
    for (size_t i = 0; i < m_FrameIndexBuffers.size(); ++i)
        m_Device->DeleteObject(m_FrameIndexBuffers[i], CKRST_OBJ_INDEXBUFFER);
    m_FrameIndexBuffers.clear();
}

void CKTranslatedContext::FinishFrame()
{
    ReleaseFrameScratch();
    for (CKDWORD view = m_NextView; view < m_LastFrameViewCount; ++view)
        m_Device->ResetView((CKRenderView)view);
    m_LastFrameViewCount = m_NextView;
    m_NextView = 0;
    m_PassOpen = FALSE;
    m_OverlayPhase = FALSE;
    m_Composited = FALSE;
    m_FrameTargetDecided = FALSE;
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
    PrepareFrameTarget();

    // RectCount == 0 clears the current viewport (D3D7 semantics); otherwise
    // every rectangle gets a clear pass of its own.
    const CKRECT target = CurrentPassRect();
    CKRECT viewportRect;
    viewportRect.left = (int)m_Viewport.ViewX;
    viewportRect.top = (int)m_Viewport.ViewY;
    viewportRect.right = (int)(m_Viewport.ViewX + m_Viewport.ViewWidth);
    viewportRect.bottom = (int)(m_Viewport.ViewY + m_Viewport.ViewHeight);
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
        if (!OpenPass(frameBuffer, rect, Flags, Color, Z, Stencil, "clear"))
            return FALSE;
        ++m_FrameClears;
        cleared = TRUE;
    }
    if (cleared && m_InScene) {
        // Mid-scene clear: the following draws need a pass of their own so
        // the clear stays at the call position.
        if (!OpenPass(frameBuffer, target, 0, 0, 1.0f, 0, "scene"))
            return FALSE;
    }
    return TRUE;
}

CKBOOL CKTranslatedContext::BeginScene()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_InScene) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    if (m_Device->GetDeviceStatus() != CK_OK) {
        Diag(CKRST_DIAG_REJECT_DEVICE_LOST);
        return FALSE;
    }
    DeliverReadbacks();
    PrepareFrameTarget();
    m_FFP.BeginDebugFrame();
    if (!OpenPass(CurrentSceneFrameBuffer(), CurrentTargetRect(), 0, 0, 1.0f, 0, "scene"))
        return FALSE;
    m_InScene = TRUE;
    return TRUE;
}

CKBOOL CKTranslatedContext::EndScene()
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (!m_InScene) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    m_InScene = FALSE;
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
    if (m_InScene || m_OverlayPhase) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    PrepareFrameTarget();
    if (!CompositeScene())
        return FALSE;
    if (!OpenPass(OverlayFrameBuffer(), WindowRect(), 0, 0, 1.0f, 0, "overlay"))
        return FALSE;
    m_OverlayPhase = TRUE;
    return TRUE;
}

CKBOOL CKTranslatedContext::BackToFront(CKBOOL VSync)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_InScene) {
        Diag(CKRST_DIAG_REJECT_SCENE_STATE);
        return FALSE;
    }
    if (m_Encoder) {
        if (!m_Target) {
            CompositeScene();
            PresentInternalTarget();
        }
        m_Device->EndEncoder(m_Encoder);
        m_Encoder = NULL;
    }
    const CKRST_FRAME_SYNC_MODE mode = m_Target ? CKRST_FRAME_SYNC_PRESERVE_PRESENT
                                       : (VSync ? CKRST_FRAME_SYNC_VSYNC : CKRST_FRAME_SYNC_IMMEDIATE);
    CKDWORD frameNumber = 0;
    const CKERROR status = m_Device->Frame(mode, CKRST_FRAME_NONE, &frameNumber);
    FinishFrame();
    DeliverReadbacks();
    return status == CK_OK ? TRUE : FALSE;
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
    if (m_Device->GetDeviceStatus() != CK_OK) {
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
// rest is the device (program creation / encoder). Approximated states are
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
    if (m_Marker.Length() > 0) {
        m_Encoder->SetMarker((CKSTRING)m_Marker.Str());
        m_Marker = "";
    }
    if (!m_FFP.DrawPrimitive(m_Encoder, m_CurrentView, Type, Indices, elementCount, Data)) {
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
    if (m_Marker.Length() > 0) {
        m_Encoder->SetMarker((CKSTRING)m_Marker.Str());
        m_Marker = "";
    }
    if (!m_FFP.DrawVertexBuffer(m_Encoder, m_CurrentView, Type, VB.Handle, IBHandle, BaseVertex, VertexCount,
                                StartIndex, IndexCount, VB.VertexBuffer.m_VertexFormat, VB.FormatFlags,
                                VB.DeviceLayout)) {
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
    CKIndexBufferDesc desc;
    desc.m_Flags = CKRST_VB_VALID | CKRST_VB_DYNAMIC;
    desc.m_MaxIndexCount = (CKDWORD)IndexCount;
    desc.m_CurrentICount = (CKDWORD)IndexCount;
    CKDWORD ib = 0;
    if (m_Device->CreateIndexBuffer(&desc, FALSE, Indices, &ib) != CK_OK || ib == 0) {
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

// ===========================================================================
// Backbuffer upload
// ===========================================================================

int CKTranslatedContext::CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image)
{
    if (!CheckDeviceForDraw())
        return 0;
    if (Buffer != VXBUFFER_BACKBUFFER || !Image.Image || Image.BitsPerPixel <= 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }
    PrepareFrameTarget();
    const CKRECT target = CurrentTargetRect();
    CKRECT rect = Rect ? *Rect : target;
    if (rect.left < 0) rect.left = 0;
    if (rect.top < 0) rect.top = 0;
    if (rect.right > target.right) rect.right = target.right;
    if (rect.bottom > target.bottom) rect.bottom = target.bottom;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0 || Image.Width != width || Image.Height != height) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return 0;
    }

    VxImageDescEx videoFormat;
    VxPixelFormat2ImageDesc(_32_ARGB8888, videoFormat);
    videoFormat.Width = width;
    videoFormat.Height = height;
    videoFormat.BytesPerLine = width * 4;
    const int videoImageSize = videoFormat.BytesPerLine * height;

    VxImageDescEx uploadDesc = Image;
    std::vector<CKBYTE> converted;
    if (!SameImageFormat(Image, videoFormat)) {
        converted.resize((size_t)videoImageSize);
        uploadDesc = videoFormat;
        uploadDesc.Image = converted.data();
        VxDoBlit(Image, uploadDesc);
    } else if (uploadDesc.BytesPerLine <= 0) {
        uploadDesc.BytesPerLine = width * uploadDesc.BitsPerPixel / 8;
    }

    CKERROR err = CK_OK;
    if (m_CopyWidth != (CKDWORD)width || m_CopyHeight != (CKDWORD)height ||
        !m_Device->IsObjectAlive(m_CopyTexture, CKRST_OBJ_TEXTURE)) {
        if (m_CopyTexture != 0) {
            m_Device->DeleteObject(m_CopyTexture, CKRST_OBJ_TEXTURE);
            m_CopyTexture = 0;
        }
        CKTextureDesc texDesc;
        texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
        texDesc.Format = videoFormat;
        texDesc.MipMapCount = 1;
        err = m_Device->CreateTexture(&texDesc, &uploadDesc, &m_CopyTexture);
        if (err == CK_OK) {
            m_CopyWidth = (CKDWORD)width;
            m_CopyHeight = (CKDWORD)height;
        }
    } else {
        err = m_Device->UpdateTexture(m_CopyTexture, 0, 0, NULL, &uploadDesc);
    }
    if (err != CK_OK)
        return 0;
    ++m_FrameTextureUploads;

    if (!EnsureDrawPass())
        return 0;

    CKFFStateGuard guard(m_FFP);
    m_FFP.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    m_FFP.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_ALWAYS);
    m_FFP.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    m_FFP.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
    m_FFP.DisableTextureStagesFrom(0);
    m_FFP.SetTexture(0, m_CopyTexture, CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA);
    m_FFP.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    m_FFP.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    m_FFP.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    m_FFP.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    m_FFP.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    m_FFP.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    m_FFP.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);

    CKViewportData fullViewport;
    fullViewport.ViewX = 0;
    fullViewport.ViewY = 0;
    fullViewport.ViewWidth = (CKDWORD)target.right;
    fullViewport.ViewHeight = (CKDWORD)target.bottom;
    fullViewport.ViewZMin = 0.0f;
    fullViewport.ViewZMax = 1.0f;
    m_FFP.SetViewport(fullViewport);

    float positions[4][4];
    CKDWORD colors[4];
    float uvs[4][2];
    const float x0 = (float)rect.left, y0 = (float)rect.top, x1 = (float)rect.right, y1 = (float)rect.bottom;
    const float coords[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; ++i) {
        positions[i][0] = xs[i];
        positions[i][1] = ys[i];
        positions[i][2] = 0.0f;
        positions[i][3] = 1.0f;
        colors[i] = 0xFFFFFFFF;
        uvs[i][0] = coords[i * 2];
        uvs[i][1] = coords[i * 2 + 1];
    }
    VxDrawPrimitiveData dp;
    memset(&dp, 0, sizeof(dp));
    dp.VertexCount = 4;
    dp.Flags = CKRST_DP_CL_VCT;
    dp.PositionPtr = positions;
    dp.PositionStride = sizeof(positions[0]);
    dp.ColorPtr = colors;
    dp.ColorStride = sizeof(colors[0]);
    dp.TexCoordPtr = uvs;
    dp.TexCoordStride = sizeof(uvs[0]);

    const CKBOOL drawn = m_FFP.DrawPrimitive(m_Encoder, m_CurrentView, VX_TRIANGLEFAN, NULL, 4, &dp);
    guard.Restore();
    m_FFP.SetViewport(m_Viewport);
    if (!drawn) {
        Diag(DrawRejectDiagnostic());
        return 0;
    }
    RecordDrawApproximations();
    CountDraw(VX_TRIANGLEFAN, 4);
    return videoImageSize;
}
