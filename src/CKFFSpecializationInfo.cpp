#include "CKFFSpecializationInfo.h"

#include <cstring>

namespace {

CKFFSpecBitfield StageLayout(CKDWORD stage, CKDWORD field) {
    static const CKFFSpecBitfield fields[] = {
        {0, 0, 5},  // color op
        {1, 0, 5},  // color arg0
        {0, 5, 5},  // color arg1
        {0, 10, 5}, // color arg2
        {0, 15, 5}, // alpha op
        {2, 0, 5},  // alpha arg0
        {0, 20, 5}, // alpha arg1
        {0, 25, 5}, // alpha arg2
        {0, 30, 1}, // result is temp
    };
    CKFFSpecBitfield layout = fields[field];
    layout.DwordOffset += (layout.DwordOffset == 0) ? 6 + stage : 0;
    layout.BitOffset += (field == 1 || field == 5) ? stage * 5 : 0;
    return layout;
}

} // namespace

CKFFSpecializationInfo::CKFFSpecializationInfo() {
    memset(m_Data, 0, sizeof(m_Data));
}

CKFFSpecBitfield CKFFSpecializationInfo::Layout(CKFFSpecConstantId id) {
    switch (id) {
    case CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE:
        return {4, 16, 3};
    case CKFF_SPEC_GLOBAL_SPECULAR_ENABLED:
        return {6, 31, 1};
    case CKFF_SPEC_PROJECTED_SAMPLER_MASK:
        return {5, 0, 4};
    case CKFF_SPEC_ALPHA_TEST_ENABLED:
        return {5, 4, 1};
    case CKFF_SPEC_ALPHA_FUNC:
        return {5, 5, 4};
    case CKFF_SPEC_FOG_ENABLED:
        return {5, 9, 1};
    case CKFF_SPEC_VERTEX_FOG_MODE:
        return {5, 10, 2};
    case CKFF_SPEC_PIXEL_FOG_MODE:
        return {5, 12, 2};
    case CKFF_SPEC_RANGE_FOG:
        return {5, 14, 1};
    default:
        break;
    }

    const CKDWORD index = (CKDWORD)id - (CKDWORD)CKFF_SPEC_STAGE0_COLOR_OP;
    const CKDWORD stage = index / 9;
    const CKDWORD field = index % 9;
    if (stage < 4)
        return StageLayout(stage, field);
    return {0, 0, 0};
}

void CKFFSpecializationInfo::Set(CKFFSpecConstantId id, CKDWORD value) {
    const CKFFSpecBitfield layout = Layout(id);
    if (layout.BitCount == 0 || layout.DwordOffset >= MaxSpecDwords)
        return;

    const CKDWORD mask = ((1u << layout.BitCount) - 1u) << layout.BitOffset;
    m_Data[layout.DwordOffset] &= ~mask;
    m_Data[layout.DwordOffset] |= (value << layout.BitOffset) & mask;
}

CKDWORD CKFFSpecializationInfo::Get(CKFFSpecConstantId id) const {
    const CKFFSpecBitfield layout = Layout(id);
    if (layout.BitCount == 0 || layout.DwordOffset >= MaxSpecDwords)
        return 0;

    const CKDWORD mask = ((1u << layout.BitCount) - 1u) << layout.BitOffset;
    return (m_Data[layout.DwordOffset] & mask) >> layout.BitOffset;
}

void CKFFSpecializationInfo::SetDwords(const CKDWORD *data, CKDWORD count) {
    memset(m_Data, 0, sizeof(m_Data));
    if (!data)
        return;

    if (count > MaxSpecDwords)
        count = MaxSpecDwords;
    memcpy(m_Data, data, sizeof(CKDWORD) * count);
}

void CKFFSpecializationInfo::SetOptimized(bool optimized) {
    m_Data[0] = optimized ? 1u : 0u;
}

bool CKFFSpecializationInfo::IsOptimized() const {
    return m_Data[0] != 0;
}

CKDWORD CKFFSpecializationInfo::RepackArg(CKDWORD arg) {
    return (arg & 0b111u) | ((arg & 0b110000u) >> 1u);
}
