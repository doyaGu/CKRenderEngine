#ifndef CKFFBUFFERUSETRACKER_H
#define CKFFBUFFERUSETRACKER_H

#include "CKFFBufferData.h"

// CPU-side range tracking used to choose safe persistent-buffer update modes.
// It stores public handles and submission numbers, never native resources.
class CKFFBufferUseTracker {
public:
    void Add(CKBufferKind Kind, CKDWORD Buffer,
             CKDWORD Offset, CKDWORD Size);
    CKBOOL Overlaps(CKBufferKind Kind, CKDWORD Buffer,
                    CKDWORD Offset, CKDWORD Size) const;
    CKERROR PrepareUpdate(CKQWORD CompletedSubmitId, CKBufferKind Kind,
                          CKDWORD Buffer, CKDWORD FullSize,
                          const CKFFBufferUpload &Upload,
                          CKBufferUpdateDesc &Update);
    void CommitUpdate(const CKBufferUpdateDesc &Update);
    void MarkSubmitted(CKQWORD SubmitId);
    void Retire(CKQWORD CompletedSubmitId);
    void Remove(CKBufferKind Kind, CKDWORD Buffer);
    void Clear() { m_Uses.Clear(); }

private:
    struct Use {
        CKBufferKind Kind;
        CKDWORD Buffer;
        CKDWORD Offset;
        CKDWORD Size;
        CKQWORD SubmitId;

        Use()
            : Kind(CKRST_BUFFER_VERTEX), Buffer(0), Offset(0),
              Size(0), SubmitId(0) {}
    };

    XArray<Use> m_Uses;
};

#endif
