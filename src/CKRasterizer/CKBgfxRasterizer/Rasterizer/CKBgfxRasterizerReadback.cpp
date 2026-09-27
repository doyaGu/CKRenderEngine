#include "CKBgfxRasterizerContext.h"
#include "CKFFImage.h"

// CKBgfxRasterizerContext synchronous and asynchronous readback.


#include <string.h>

// ===========================================================================
// Readback
// ===========================================================================

CKBOOL CKBgfxRasterizerContext::ValidateRect(const CKRECT *Rect, CKDWORD Width, CKDWORD Height) const
{
    return CKFFValidateRect(Rect, Width, Height);
}

CKBOOL CKBgfxRasterizerContext::BuildReadbackImage(const PendingReadback &Readback, VxImageDescEx &Desc,
                                               XArray<CKBYTE> &Pixels) const
{
    return CKFFBuildReadbackImage(
        Readback.Success, Readback.Width, Readback.Height, Readback.Pitch,
        Readback.Format, Readback.YFlip,
        Readback.HasRect ? &Readback.Rect : NULL, Readback.Data, Desc, Pixels);
}

CKBOOL CKBgfxRasterizerContext::CanReadNativeTarget()
{
    if (!PrepareFrameTarget()) return FALSE;
    const CKBgfxPresentTarget &native = m_Present.NativeTarget();
    if (m_Target.IsActive() || !m_Frame.InternalTargets || !native.IsActive())
        return FALSE;
    return m_Present.AcquireReadbackTexture(native.Width, native.Height) != 0;
}

CKBOOL CKBgfxRasterizerContext::CanReadTargetTexture()
{
    if (!m_Target.IsActive())
        return FALSE;
    const CKTextureDesc *texture =
        m_PublicResources.FindTexture(m_Target.Texture);
    if (!texture || (texture->Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) != 0)
        return FALSE;
    return m_Present.AcquireReadbackTexture(m_Target.Width, m_Target.Height) != 0;
}

CKBOOL CKBgfxRasterizerContext::CanReadCurrentTarget()
{
    return m_Target.IsActive() ? CanReadTargetTexture() : CanReadNativeTarget();
}

CKBOOL CKBgfxRasterizerContext::BlitForReadback()
{
    const CKDWORD readbackTexture = m_Present.GetReadbackTexture();
    CKDWORD source = 0;
    if (m_Target.IsActive()) {
        source = m_Target.Texture;
    } else if (m_Present.NativeTarget().IsActive()) {
        source = m_Present.NativeTarget().ColorTexture;
    }
    if (!source || !readbackTexture)
        return FALSE;
    const CKERROR copied = Blit(readbackTexture, 0, 0, 0, 0,
                                source, 0, 0, NULL);
    if (copied == CK_OK)
        return TRUE;
    if (copied != CKERR_NOTIMPLEMENTED)
        return FALSE;

    CKBOOL sourceBottomLeft = FALSE;
    if (!GetTextureBottomLeft(source, sourceBottomLeft))
        return FALSE;
    CKRenderPassDesc pass;
    pass.RenderTarget = m_Present.AcquireReadbackFrameBuffer();
    if (!pass.RenderTarget)
        return FALSE;
    pass.Rect = CKFFMakeRect((int)m_Target.Width, (int)m_Target.Height);
    pass.Name = "readback-convert";
    if (BeginPass(&pass) != CK_OK ||
        m_Present.SubmitCopy(source, m_Target.Width, m_Target.Height,
            sourceBottomLeft == GetCaps().OriginBottomLeft) != CK_OK)
        return FALSE;
    return Blit(readbackTexture, 0, 0, 0, 0,
                m_Present.GetReadbackRenderTexture(), 0, 0, NULL) == CK_OK
        ? TRUE : FALSE;
}

CKBOOL CKBgfxRasterizerContext::IssueTextureReadback(PendingReadback &Readback)
{
    const CKDWORD readbackTexture = m_Present.GetReadbackTexture();
    if (!readbackTexture)
        return FALSE;
    CKReadbackDesc desc;
    if (ReadTexture(readbackTexture, 0, &desc, NULL) != CK_OK || desc.RequiredSize == 0 ||
        desc.Width == 0 || desc.Height == 0 || desc.Format == UNKNOWN_PF)
        return FALSE;
    if (ReadTexture(readbackTexture, 0, &desc, &Readback.Ticket) != CK_OK) {
        Readback.Data.Clear();
        return FALSE;
    }
    Readback.Width = desc.Width;
    Readback.Height = desc.Height;
    Readback.Pitch = desc.RowPitch;
    Readback.Format = desc.Format;
    // A target texture was rendered in the D3D (top-down) layout already
    // The flip reported for framebuffer writes on
    // bottom-left backends has been done at render time.
    Readback.YFlip = m_Target.IsActive() ? FALSE : desc.YFlip;
    return TRUE;
}

// Resolve a snapshot without ending the logical scene or consuming the resolve
// that will later compose its remaining draws and the overlay.
CKBOOL CKBgfxRasterizerContext::ResolveCopySource()
{
    if (m_Target.IsActive() || !m_Frame.Open || m_Frame.Composited)
        return TRUE;
    if (m_Frame.SceneUsesNative)
        return m_Frame.InternalTargets;
    return m_Frame.InternalTargets &&
           OpenPass(OverlayFrameBuffer(), WindowRect(), 0, 0, 1, 0, "snapshot-resolve") &&
           m_Present.SubmitResolve(m_Options.FXAA, m_Options.Sharpness) == CK_OK;
}

CKBOOL CKBgfxRasterizerContext::CaptureReadback(PendingReadback &Readback)
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

CKBOOL CKBgfxRasterizerContext::SubmitReadbackFrame(CKBOOL Present, CKBOOL Blit, CKDWORD *FrameNumber)
{
    if (m_Frame.Open)
        return FALSE;
    const CKBOOL passOpen = m_Frame.PassOpen;
    const CKDWORD passes = m_FramePasses;
    CKBOOL ok = TRUE;
    if (Present && !m_Target.IsActive())
        ok = PresentInternalTarget(CKRST_PRESENT_UNCHANGED);
    if (ok && Blit)
        ok = BlitForReadback();
    CKDWORD frame = 0;
    const CKERROR status = Submit(CKRST_PRESENT_UNCHANGED,
                                  Present && !m_Target.IsActive(), &frame);
    if (status == CK_OK) {
        m_BufferUses.MarkSubmitted(GetLastSubmitId());
        m_BufferUses.Retire(GetCompletedSubmitId());
    }
    m_Frame.Open = FALSE;
    m_Frame.PassOpen = passOpen;
    m_FramePasses = passes;
    if (FrameNumber)
        *FrameNumber = frame;
    return ok && status == CK_OK ? TRUE : FALSE;
}

CKBOOL CKBgfxRasterizerContext::RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback,
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
    m_Readbacks.PushBack(readback);
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::CompleteReadback(PendingReadback &readback, CKBOOL wait)
{
    for (;;) {
        const CKReadbackState state = PollReadback(readback.Ticket, wait);
        if (state == CKRST_READBACK_READY) {
            const int size = readback.Ticket->Data.Size();
            readback.Data.Resize(size);
            if (size > 0)
                memcpy(readback.Data.Begin(), readback.Ticket->Data.Begin(), size);
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

void CKBgfxRasterizerContext::DeliverReadbacks()
{
    XArray<PendingReadback *> ready;
    for (int i = 0; i < m_Readbacks.Size();) {
        PendingReadback *pending = m_Readbacks[i];
        if (!pending->Done)
            CompleteReadback(*pending, FALSE);
        if (pending->Done) {
            ready.PushBack(pending);
            m_Readbacks.EraseAt(i);
        } else {
            ++i;
        }
    }
    for (int i = 0; i < ready.Size(); ++i) {
        PendingReadback *readback = ready[i];
        VxImageDescEx image;
        XArray<CKBYTE> pixels;
        const CKBOOL ok = BuildReadbackImage(*readback, image, pixels);
        readback->Callback(readback->User, readback->HasRect ? &readback->Rect : NULL, readback->Buffer,
                           ok ? &image : NULL, ok);
        delete readback;
    }
}

void CKBgfxRasterizerContext::CancelReadbacks()
{
    for (int i = 0; i < m_Readbacks.Size(); ++i) {
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

int CKBgfxRasterizerContext::CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image)
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
    XArray<CKBYTE> pixels;
    if (!BuildReadbackImage(readback, captured, pixels))
        return 0;

    CKBYTE *destination = Image.Image;
    Image = captured;
    const int size = pixels.Size();
    if (!destination) {
        Image.Image = NULL;
        return size;
    }
    Image.Image = destination;
    memcpy(destination, pixels.Begin(), size);
    return size;
}
