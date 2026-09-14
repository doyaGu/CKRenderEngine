// FFPRecordingContext synchronous and asynchronous readback.

#include "FFPRecordingContext.h"

#include <string.h>

namespace {

CKBOOL SameImageFormat(const VxImageDescEx &a, const VxImageDescEx &b)
{
    return a.BitsPerPixel == b.BitsPerPixel && a.RedMask == b.RedMask && a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask && a.AlphaMask == b.AlphaMask;
}

} // namespace

// ===========================================================================
// Readback
// ===========================================================================

CKBOOL FFPRecordingContext::ValidateRect(const CKRECT *Rect, CKDWORD Width, CKDWORD Height) const
{
    if (!Rect)
        return TRUE;
    return Rect->left >= 0 && Rect->top >= 0 && Rect->right > Rect->left && Rect->bottom > Rect->top &&
           (CKDWORD)Rect->right <= Width && (CKDWORD)Rect->bottom <= Height;
}

CKBOOL FFPRecordingContext::BuildReadbackImage(const PendingReadback &Readback, VxImageDescEx &Desc,
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

CKBOOL FFPRecordingContext::CanReadNativeTarget()
{
    if (!PrepareFrameTarget()) return FALSE;
    const FFPRecordingPresentTarget &native = m_Present.NativeTarget();
    if (m_Target.IsActive() || !m_Frame.InternalTargets || !native.IsActive())
        return FALSE;
    return m_Present.AcquireReadbackTexture(native.Width, native.Height) != 0;
}

CKBOOL FFPRecordingContext::CanReadTargetTexture()
{
    if (!m_Target.IsActive())
        return FALSE;
    const CKTextureDesc *texture =
        m_PublicResources.FindTexture(m_Target.Texture);
    if (!texture ||
        (texture->Flags &
         (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) != 0)
        return FALSE;
    return m_Present.AcquireReadbackTexture(
        m_Target.Width, m_Target.Height) != 0;
}

CKBOOL FFPRecordingContext::CanReadCurrentTarget()
{
    return m_Target.IsActive() ? CanReadTargetTexture()
                               : CanReadNativeTarget();
}

CKBOOL FFPRecordingContext::BlitForReadback()
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
    return m_Backend->Blit(readbackTexture, 0, 0, 0, 0, source, 0, 0, NULL) == CK_OK ? TRUE : FALSE;
}

CKBOOL FFPRecordingContext::IssueTextureReadback(PendingReadback &Readback)
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
    Readback.YFlip = m_Target.IsActive() ? FALSE : desc.YFlip;
    return TRUE;
}

// Resolve a snapshot without ending the logical scene or consuming the resolve
// that will later compose its remaining draws and the overlay.
CKBOOL FFPRecordingContext::ResolveCopySource()
{
    if (m_Target.IsActive() || !m_Frame.Open || m_Frame.Composited)
        return TRUE;
    if (m_Frame.SceneUsesNative)
        return m_Frame.InternalTargets;
    return m_Frame.InternalTargets &&
           OpenPass(OverlayFrameBuffer(), WindowRect(), 0, 0, 1, 0, "snapshot-resolve") &&
           m_Present.SubmitResolve(m_Options.FXAA, m_Options.Sharpness) == CK_OK;
}

CKBOOL FFPRecordingContext::CaptureReadback(PendingReadback &Readback)
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

CKBOOL FFPRecordingContext::SubmitReadbackFrame(CKBOOL Present, CKBOOL Blit, CKDWORD *FrameNumber)
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
    const CKERROR status = m_Backend->Submit(
        CKRST_PRESENT_UNCHANGED,
        Present && !m_Target.IsActive(), &frame);
    if (status == CK_OK) {
        m_BufferUses.MarkSubmitted(m_Backend->GetLastSubmitId());
        m_BufferUses.Retire(m_Backend->GetCompletedSubmitId());
    }
    m_Frame.Open = FALSE;
    m_Frame.PassOpen = passOpen;
    m_FramePasses = passes;
    if (FrameNumber)
        *FrameNumber = frame;
    return ok && status == CK_OK ? TRUE : FALSE;
}

CKBOOL FFPRecordingContext::RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback,
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

CKBOOL FFPRecordingContext::CompleteReadback(PendingReadback &readback, CKBOOL wait)
{
    for (;;) {
        const CKReadbackState state = m_Backend->PollReadback(readback.Ticket, wait);
        if (state == CKRST_READBACK_READY) {
            const XArray<CKBYTE> &data = readback.Ticket->Data;
            readback.Data.assign(data.Begin(), data.Begin() + data.Size());
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

void FFPRecordingContext::DeliverReadbacks()
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

void FFPRecordingContext::CancelReadbacks()
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

int FFPRecordingContext::CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image)
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
