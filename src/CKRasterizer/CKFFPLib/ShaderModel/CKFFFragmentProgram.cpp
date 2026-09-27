#include "CKFFFragmentProgram.h"

#include <string.h>

CKFFFragmentProgram::CKFFFragmentProgram() {
    memset(m_Lanes, 0, sizeof(m_Lanes));
}

void CKFFFragmentProgram::SetBits(const CKFFFragmentProgramBitfield &layout, CKDWORD value) {
    if (layout.BitCount == 0 || layout.Lane >= LaneCount ||
        layout.BitOffset + layout.BitCount > CKFF_FRAGMENT_PROGRAM_LANE_BITS)
        return;
    const CKDWORD mask = ((1u << layout.BitCount) - 1u) << layout.BitOffset;
    m_Lanes[layout.Lane] &= ~mask;
    m_Lanes[layout.Lane] |= (value << layout.BitOffset) & mask;
}

CKDWORD CKFFFragmentProgram::GetBits(const CKFFFragmentProgramBitfield &layout) const {
    if (layout.BitCount == 0 || layout.Lane >= LaneCount ||
        layout.BitOffset + layout.BitCount > CKFF_FRAGMENT_PROGRAM_LANE_BITS)
        return 0;
    const CKDWORD mask = (1u << layout.BitCount) - 1u;
    return (m_Lanes[layout.Lane] >> layout.BitOffset) & mask;
}

void CKFFFragmentProgram::Set(CKFFFragmentProgramGlobalField field, CKDWORD value) {
    if (field >= CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT)
        return;
    SetBits(CKFFFragmentProgramGlobalFieldLayout(field), value);
}

CKDWORD CKFFFragmentProgram::Get(CKFFFragmentProgramGlobalField field) const {
    if (field >= CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT)
        return 0;
    return GetBits(CKFFFragmentProgramGlobalFieldLayout(field));
}

void CKFFFragmentProgram::SetStage(CKDWORD stage, CKFFFragmentProgramStageField field, CKDWORD value) {
    if (stage >= CKFF_FRAGMENT_PROGRAM_STAGE_COUNT || field >= CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT)
        return;
    SetBits(CKFFFragmentProgramStageFieldLayout(stage, field), value);
}

CKDWORD CKFFFragmentProgram::GetStage(CKDWORD stage, CKFFFragmentProgramStageField field) const {
    if (stage >= CKFF_FRAGMENT_PROGRAM_STAGE_COUNT || field >= CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT)
        return 0;
    return GetBits(CKFFFragmentProgramStageFieldLayout(stage, field));
}

void CKFFFragmentProgram::SetSamplerOrdinal(CKDWORD stage, CKDWORD ordinal) {
    if (stage >= CKFF_FRAGMENT_PROGRAM_STAGE_COUNT)
        return;
    CKFFFragmentProgramBitfield layout = CKFFFragmentProgramGlobalFieldLayout(CKFF_FRAGMENT_PROGRAM_SAMPLER_ORDINALS);
    layout.BitOffset += stage * 3;
    layout.BitCount = 3;
    SetBits(layout, ordinal);
}

CKDWORD CKFFFragmentProgram::GetSamplerOrdinal(CKDWORD stage) const {
    if (stage >= CKFF_FRAGMENT_PROGRAM_STAGE_COUNT)
        return 0;
    CKFFFragmentProgramBitfield layout = CKFFFragmentProgramGlobalFieldLayout(CKFF_FRAGMENT_PROGRAM_SAMPLER_ORDINALS);
    layout.BitOffset += stage * 3;
    layout.BitCount = 3;
    return GetBits(layout);
}

void CKFFFragmentProgram::SetLanes(const CKDWORD *lanes, CKDWORD count) {
    memset(m_Lanes, 0, sizeof(m_Lanes));
    if (!lanes)
        return;
    if (count > LaneCount)
        count = LaneCount;
    for (CKDWORD i = 0; i < count; ++i)
        m_Lanes[i] = lanes[i] & LaneMask;
}

void CKFFFragmentProgram::Pack24(float outVec4[CKFF_FRAGMENT_PROGRAM_VEC4_COUNT][4]) const {
    for (CKDWORD lane = 0; lane < LaneCount; ++lane)
        outVec4[lane / 4][lane % 4] = (float)(m_Lanes[lane] & LaneMask);
}

CKFFFragmentProgram CKFFFragmentProgram::Unpack24(const float *floats, CKDWORD floatCount) {
    CKFFFragmentProgram info;
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

bool CKFFFragmentProgram::operator==(const CKFFFragmentProgram &other) const {
    return memcmp(m_Lanes, other.m_Lanes, sizeof(m_Lanes)) == 0;
}

CKDWORD CKFFFragmentProgram::RepackArg(CKDWORD arg) {
    return (arg & 0b111u) | ((arg & 0b110000u) >> 1u);
}

CKDWORD CKFFFragmentProgram::UnpackArg(CKDWORD packed) {
    return (packed & 0b111u) | ((packed & 0b11000u) << 1u);
}
