#ifndef CKFFCONSTANTSET_H
#define CKFFCONSTANTSET_H

#include "VxMath.h"
#include "CKError.h"

#define CKFF_CONSTANT_SLOT_COUNT 32
#define CKFF_MAX_CONSTANT_BYTES 16384

struct CKFFConstantValue {
    XArray<CKBYTE> Bytes;
    CKQWORD Change;

    CKFFConstantValue() : Change(0) {}
};

// Constant blocks produced by CKFFPLib. Storage belongs to the fixed-function
// implementation and remains valid until the next prepared draw.
class CKFFConstantSet {
public:
    CKFFConstantSet();
    CKFFConstantSet(const CKFFConstantSet &) = delete;
    CKFFConstantSet &operator=(const CKFFConstantSet &) = delete;

    CKERROR Set(CKDWORD Slot, const void *Data, CKDWORD ByteSize);
    const CKFFConstantValue &operator[](CKDWORD Slot) const { return m_Values[Slot]; }
    CKQWORD Identity() const { return m_Identity; }

private:
    CKQWORD m_Identity;
    CKFFConstantValue m_Values[CKFF_CONSTANT_SLOT_COUNT];
};

#endif
