#include "CKFFBufferUseTracker.h"

static CKBOOL CKFFBufferRangesOverlap(CKDWORD FirstOffset, CKDWORD FirstSize,
                                      CKDWORD SecondOffset, CKDWORD SecondSize)
{
    return (CKQWORD)FirstOffset < (CKQWORD)SecondOffset + SecondSize &&
           (CKQWORD)SecondOffset < (CKQWORD)FirstOffset + FirstSize;
}

void CKFFBufferUseTracker::Add(CKBufferKind Kind, CKDWORD Buffer,
                               CKDWORD Offset, CKDWORD Size)
{
    if (!Buffer || !Size)
        return;

    const CKQWORD end = (CKQWORD)Offset + Size;
    for (int index = m_Uses.Size() - 1; index >= 0; --index) {
        Use &use = m_Uses[index];
        if (use.SubmitId != 0)
            break;
        if (use.Kind != Kind || use.Buffer != Buffer)
            continue;
        const CKQWORD useEnd = (CKQWORD)use.Offset + use.Size;
        if ((CKQWORD)Offset > useEnd || (CKQWORD)use.Offset > end)
            continue;
        const CKDWORD first = Offset < use.Offset ? Offset : use.Offset;
        const CKQWORD last = end > useEnd ? end : useEnd;
        use.Offset = first;
        use.Size = (CKDWORD)(last - first);
        return;
    }

    Use use;
    use.Kind = Kind;
    use.Buffer = Buffer;
    use.Offset = Offset;
    use.Size = Size;
    m_Uses.PushBack(use);
}

CKBOOL CKFFBufferUseTracker::Overlaps(CKBufferKind Kind, CKDWORD Buffer,
                                      CKDWORD Offset, CKDWORD Size) const
{
    for (int index = 0; index < m_Uses.Size(); ++index) {
        const Use &use = m_Uses[index];
        if (use.Kind == Kind && use.Buffer == Buffer &&
            CKFFBufferRangesOverlap(use.Offset, use.Size, Offset, Size))
            return TRUE;
    }
    return FALSE;
}

CKERROR CKFFBufferUseTracker::PrepareUpdate(
    CKQWORD CompletedSubmitId, CKBufferKind Kind, CKDWORD Buffer,
    CKDWORD FullSize, const CKFFBufferUpload &Upload,
    CKBufferUpdateDesc &Update)
{
    Retire(CompletedSubmitId);

    Update.Kind = Kind;
    Update.Buffer = Buffer;
    Update.Mode = CKFFToContextBufferUpdateMode(Upload.Mode);
    Update.Offset = Upload.Offset;
    Update.Size = Upload.Size;
    Update.Data = Upload.Data;

    const CKDWORD usedOffset = Update.Mode == CKRST_BUFFER_UPDATE_DISCARD
        ? 0 : Update.Offset;
    const CKDWORD usedSize = Update.Mode == CKRST_BUFFER_UPDATE_DISCARD
        ? FullSize : Update.Size;
    const CKBOOL bufferInUse = Overlaps(
        Update.Kind, Update.Buffer, usedOffset, usedSize);
    if (Update.Mode == CKRST_BUFFER_UPDATE_NOOVERWRITE && bufferInUse)
        return CKERR_INVALIDPARAMETER;

    Update.Rename = bufferInUse;
    return CK_OK;
}

void CKFFBufferUseTracker::CommitUpdate(
    const CKBufferUpdateDesc &Update)
{
    if (Update.Mode == CKRST_BUFFER_UPDATE_DISCARD || Update.Rename)
        Remove(Update.Kind, Update.Buffer);
}

void CKFFBufferUseTracker::MarkSubmitted(CKQWORD SubmitId)
{
    if (!SubmitId)
        return;
    for (int index = 0; index < m_Uses.Size(); ++index) {
        if (m_Uses[index].SubmitId == 0)
            m_Uses[index].SubmitId = SubmitId;
    }
}

void CKFFBufferUseTracker::Retire(CKQWORD CompletedSubmitId)
{
    int output = 0;
    for (int input = 0; input < m_Uses.Size(); ++input) {
        const Use &use = m_Uses[input];
        if (use.SubmitId != 0 && use.SubmitId <= CompletedSubmitId)
            continue;
        if (output != input)
            m_Uses[output] = use;
        ++output;
    }
    m_Uses.Resize(output);
}

void CKFFBufferUseTracker::Remove(CKBufferKind Kind, CKDWORD Buffer)
{
    int output = 0;
    for (int input = 0; input < m_Uses.Size(); ++input) {
        const Use &use = m_Uses[input];
        if (use.Kind == Kind && use.Buffer == Buffer)
            continue;
        if (output != input)
            m_Uses[output] = use;
        ++output;
    }
    m_Uses.Resize(output);
}
