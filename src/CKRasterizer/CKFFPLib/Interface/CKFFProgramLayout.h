#ifndef CKFFPROGRAMLAYOUT_H
#define CKFFPROGRAMLAYOUT_H

#include "CKFFConstantSet.h"
#include "CKFFProgramDesc.h"
#include "XArray.h"
#include "XClassArray.h"
#include <stdint.h>

// CPU-side constant packing reused by the concrete native program records.
class CKFFProgramLayout {
public:
    struct Buffer {
        CK_SHADER_STAGE Stage;
        CKDWORD Slot, Offset, Size;
        CKQWORD Change = 0;
    };

    void Init(const CKFFProgramDesc &desc);
    void Update(const CKFFConstantSet &values);
    bool MarkDataChanged(CKDWORD offset, CKDWORD size);
    CKDWORD BufferOffset(CK_SHADER_STAGE stage, CKDWORD slot) const;

    XClassArray<Buffer> Buffers;
    XArray<CKBYTE> Data;

private:
    XUINTPTR SourceIdentity = 0;
    struct Copy {
        CKDWORD Slot, Offset, Size;
        CKQWORD Change = UINT64_MAX;
    };
    XClassArray<Copy> Copies;
};

#endif // CKFFPROGRAMLAYOUT_H
