#include "CKFFSpecializationInfo.h"

#include <string.h>

CKFFSpecializationInfo::CKFFSpecializationInfo() {
    memset(m_Lanes, 0, sizeof(m_Lanes));
}

void CKFFSpecializationInfo::SetBits(const CKFFSpecBitfield &layout, CKDWORD value) {
    if (layout.BitCount == 0 || layout.Lane >= LaneCount ||
        layout.BitOffset + layout.BitCount > CKFF_SPEC_LANE_BITS)
        return;
    const CKDWORD mask = ((1u << layout.BitCount) - 1u) << layout.BitOffset;
    m_Lanes[layout.Lane] &= ~mask;
    m_Lanes[layout.Lane] |= (value << layout.BitOffset) & mask;
}

CKDWORD CKFFSpecializationInfo::GetBits(const CKFFSpecBitfield &layout) const {
    if (layout.BitCount == 0 || layout.Lane >= LaneCount ||
        layout.BitOffset + layout.BitCount > CKFF_SPEC_LANE_BITS)
        return 0;
    const CKDWORD mask = (1u << layout.BitCount) - 1u;
    return (m_Lanes[layout.Lane] >> layout.BitOffset) & mask;
}

void CKFFSpecializationInfo::Set(CKFFSpecGlobalField field, CKDWORD value) {
    if (field >= CKFF_SPEC_GLOBAL_FIELD_COUNT)
        return;
    SetBits(CKFFSpecGlobalFieldLayout(field), value);
}

CKDWORD CKFFSpecializationInfo::Get(CKFFSpecGlobalField field) const {
    if (field >= CKFF_SPEC_GLOBAL_FIELD_COUNT)
        return 0;
    return GetBits(CKFFSpecGlobalFieldLayout(field));
}

void CKFFSpecializationInfo::SetStage(CKDWORD stage, CKFFSpecStageField field, CKDWORD value) {
    if (stage >= CKFF_SPEC_STAGE_COUNT || field >= CKFF_SPEC_STAGE_FIELD_COUNT)
        return;
    SetBits(CKFFSpecStageFieldLayout(stage, field), value);
}

CKDWORD CKFFSpecializationInfo::GetStage(CKDWORD stage, CKFFSpecStageField field) const {
    if (stage >= CKFF_SPEC_STAGE_COUNT || field >= CKFF_SPEC_STAGE_FIELD_COUNT)
        return 0;
    return GetBits(CKFFSpecStageFieldLayout(stage, field));
}

void CKFFSpecializationInfo::SetMirrorOnceMask(CKDWORD stage, CKDWORD mask) {
    if (stage >= CKFF_SPEC_STAGE_COUNT)
        return;
    CKFFSpecBitfield layout = CKFFSpecGlobalFieldLayout(CKFF_SPEC_MIRRORONCE_SAMPLER_MASK);
    layout.BitOffset += stage * 3;
    layout.BitCount = 3;
    SetBits(layout, mask);
}

CKDWORD CKFFSpecializationInfo::GetMirrorOnceMask(CKDWORD stage) const {
    if (stage >= CKFF_SPEC_STAGE_COUNT)
        return 0;
    CKFFSpecBitfield layout = CKFFSpecGlobalFieldLayout(CKFF_SPEC_MIRRORONCE_SAMPLER_MASK);
    layout.BitOffset += stage * 3;
    layout.BitCount = 3;
    return GetBits(layout);
}

void CKFFSpecializationInfo::SetLanes(const CKDWORD *lanes, CKDWORD count) {
    memset(m_Lanes, 0, sizeof(m_Lanes));
    if (!lanes)
        return;
    if (count > LaneCount)
        count = LaneCount;
    for (CKDWORD i = 0; i < count; ++i)
        m_Lanes[i] = lanes[i] & LaneMask;
}

void CKFFSpecializationInfo::Pack24(float outVec4[CKFF_SPEC_VEC4_COUNT][4]) const {
    for (CKDWORD lane = 0; lane < LaneCount; ++lane)
        outVec4[lane / 4][lane % 4] = (float)(m_Lanes[lane] & LaneMask);
}

CKFFSpecializationInfo CKFFSpecializationInfo::Unpack24(const float *floats, CKDWORD floatCount) {
    CKFFSpecializationInfo info;
    if (!floats)
        return info;
    if (floatCount > LaneCount)
        floatCount = LaneCount;
    for (CKDWORD lane = 0; lane < floatCount; ++lane) {
        const float value = floats[lane];
        info.m_Lanes[lane] = value <= 0.0f ? 0u : ((CKDWORD)value & LaneMask);
    }
    return info;
}

bool CKFFSpecializationInfo::operator==(const CKFFSpecializationInfo &other) const {
    return memcmp(m_Lanes, other.m_Lanes, sizeof(m_Lanes)) == 0;
}

CKDWORD CKFFSpecializationInfo::RepackArg(CKDWORD arg) {
    return (arg & 0b111u) | ((arg & 0b110000u) >> 1u);
}

CKDWORD CKFFSpecializationInfo::UnpackArg(CKDWORD packed) {
    return (packed & 0b111u) | ((packed & 0b11000u) << 1u);
}
