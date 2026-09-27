#ifndef CKFFFRAGMENTPROGRAM_H
#define CKFFFRAGMENTPROGRAM_H

#include "CKFFFragmentProgramLayout.h"

// CPU-resolved fixed-function state consumed by the fragment shader. Stored as
// CKFF_FRAGMENT_PROGRAM_LANE_COUNT 24-bit lanes laid out by
// CKFFFragmentProgramLayout.def and uploaded as u_ffProgram (one lane per
// float component).
class CKFFFragmentProgram {
public:
    static const CKDWORD LaneCount = CKFF_FRAGMENT_PROGRAM_LANE_COUNT;
    static const CKDWORD Vec4Count = CKFF_FRAGMENT_PROGRAM_VEC4_COUNT;
    static const CKDWORD LaneMask = (1u << CKFF_FRAGMENT_PROGRAM_LANE_BITS) - 1u;

    CKFFFragmentProgram();

    void Set(CKFFFragmentProgramGlobalField field, CKDWORD value);
    CKDWORD Get(CKFFFragmentProgramGlobalField field) const;
    void SetStage(CKDWORD stage, CKFFFragmentProgramStageField field, CKDWORD value);
    CKDWORD GetStage(CKDWORD stage, CKFFFragmentProgramStageField field) const;

    // Native resource ordinal of one stage (3 bits inside SAMPLER_ORDINALS).
    void SetSamplerOrdinal(CKDWORD stage, CKDWORD ordinal);
    CKDWORD GetSamplerOrdinal(CKDWORD stage) const;

    void SetLanes(const CKDWORD *lanes, CKDWORD count);
    const CKDWORD *Lanes() const { return m_Lanes; }

    // u_ffProgram encoding: every lane becomes one float holding its integer
    // value (exact below 2^24), four lanes per vec4.
    void Pack24(float outVec4[CKFF_FRAGMENT_PROGRAM_VEC4_COUNT][4]) const;
    static CKFFFragmentProgram Unpack24(const float *floats, CKDWORD floatCount);

    bool operator==(const CKFFFragmentProgram &other) const;
    bool operator!=(const CKFFFragmentProgram &other) const { return !(*this == other); }

    // Texture argument (CKRST_TA_*) <-> 5-bit field: base arg in bits 0..2,
    // COMPLEMENT / ALPHAREPLICATE modifiers in bits 3..4.
    static CKDWORD RepackArg(CKDWORD arg);
    static CKDWORD UnpackArg(CKDWORD packed);

private:
    void SetBits(const CKFFFragmentProgramBitfield &layout, CKDWORD value);
    CKDWORD GetBits(const CKFFFragmentProgramBitfield &layout) const;

    CKDWORD m_Lanes[CKFF_FRAGMENT_PROGRAM_LANE_COUNT];
};

#endif // CKFFFRAGMENTPROGRAM_H
