#ifndef CKBACKENDDRAWDATA_H
#define CKBACKENDDRAWDATA_H

#include "CKBackendProgram.h"
#include "CKError.h"
#include "CKRasterizerBackendTypes.h"

struct CKBackendConstantValue {
    std::vector<CKBYTE> Bytes;
    uint64_t Revision = 0;
};

// Producer-owned data. Draw borrows it only for the duration of the call;
// deferred backends snapshot the bytes before returning. Revisions skip
// redundant native packing, while Identity prevents cross-producer aliasing.
class CKBackendConstants {
public:
    CKBackendConstants();
    CKBackendConstants(const CKBackendConstants &) = delete;
    CKBackendConstants &operator=(const CKBackendConstants &) = delete;
    CKERROR Set(CKDWORD slot, const void *data, CKDWORD byteSize);
    const CKBackendConstantValue &operator[](CKDWORD slot) const { return m_Values[slot]; }
    uint64_t Identity() const { return m_Identity; }
private:
    uint64_t m_Identity;
    std::array<CKBackendConstantValue, CKBACKEND_MAX_CONSTANT_SLOTS> m_Values;
};

struct CKBackendTextureBinding {
    CKDWORD Texture = 0;
    CKSamplerDesc Sampler = {CKRST_FILTER_LINEAR, CKRST_FILTER_LINEAR, CKRST_FILTER_NONE,
        CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, 0, CKRST_COMPARE_NONE};
};
using CKBackendTextureBindings = std::array<CKBackendTextureBinding, CKBACKEND_MAX_TEXTURE_SLOTS>;

#endif
