#ifndef CKBACKENDPROGRAMLAYOUT_H
#define CKBACKENDPROGRAMLAYOUT_H

#include "CKBackendProgram.h"

struct CKBackendConstantValue {
    std::vector<CKBYTE> Bytes;
    uint64_t Revision = 0;
};
using CKBackendConstantValues = std::array<CKBackendConstantValue, CKBACKEND_MAX_CONSTANT_SLOTS>;

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
    void Update(const CKBackendConstantValues &values);
    CKDWORD BufferOffset(CK_SHADER_STAGE stage, CKDWORD slot) const;
    std::vector<Buffer> Buffers;
    std::vector<CKBYTE> Data;
private:
    struct Copy {
        CKDWORD Slot, Offset, Size;
        uint64_t Revision = ~uint64_t(0);
    };
    std::vector<Copy> Copies;
};

#endif
