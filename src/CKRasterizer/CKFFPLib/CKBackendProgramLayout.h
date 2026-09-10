#ifndef CKBACKENDPROGRAMLAYOUT_H
#define CKBACKENDPROGRAMLAYOUT_H

#include "CKBackendDrawData.h"

// Compiled once when a native program is created. Packing reuses storage and
// copies only slots changed since that program's previous draw. The caller
// snapshots Data into its batch arena before later values can change it.
class CKBackendProgramLayout {
public:
    struct Buffer {
        CK_SHADER_STAGE Stage;
        CKDWORD Slot, Offset, Size;
    };
    void Init(const CKBackendProgramDesc &desc);
    void Update(const CKBackendConstants &values);
    CKDWORD BufferOffset(CK_SHADER_STAGE stage, CKDWORD slot) const;
    std::vector<Buffer> Buffers;
    std::vector<CKBYTE> Data;
private:
    uint64_t SourceIdentity = 0;
    struct Copy {
        CKDWORD Slot, Offset, Size;
        uint64_t Revision = ~uint64_t(0);
    };
    std::vector<Copy> Copies;
};

#endif
