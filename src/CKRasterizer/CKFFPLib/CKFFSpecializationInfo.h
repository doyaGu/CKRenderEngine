#ifndef CKFFSPECIALIZATIONINFO_H
#define CKFFSPECIALIZATIONINFO_H

#include "CKFFSpecLayout.h"

// Per-draw specialization data of the fixed-function uber shader: which
// combiner ops / args, sampler kinds and global switches the fragment shader
// must apply. Stored as CKFF_SPEC_LANE_COUNT 24-bit lanes laid out by
// CKFFSpecLayout.def and uploaded as u_ffSpec (one lane per float component).
class CKFFSpecializationInfo {
public:
    static const CKDWORD LaneCount = CKFF_SPEC_LANE_COUNT;
    static const CKDWORD Vec4Count = CKFF_SPEC_VEC4_COUNT;
    static const CKDWORD LaneMask = (1u << CKFF_SPEC_LANE_BITS) - 1u;

    CKFFSpecializationInfo();

    void Set(CKFFSpecGlobalField field, CKDWORD value);
    CKDWORD Get(CKFFSpecGlobalField field) const;
    void SetStage(CKDWORD stage, CKFFSpecStageField field, CKDWORD value);
    CKDWORD GetStage(CKDWORD stage, CKFFSpecStageField field) const;

    // MIRRORONCE axes of one stage (3 bits inside MIRRORONCE_SAMPLER_MASK).
    void SetMirrorOnceMask(CKDWORD stage, CKDWORD mask);
    CKDWORD GetMirrorOnceMask(CKDWORD stage) const;

    void SetLanes(const CKDWORD *lanes, CKDWORD count);
    const CKDWORD *Lanes() const { return m_Lanes; }

    // u_ffSpec encoding: every lane becomes one float holding its integer
    // value (exact below 2^24), four lanes per vec4.
    void Pack24(float outVec4[CKFF_SPEC_VEC4_COUNT][4]) const;
    static CKFFSpecializationInfo Unpack24(const float *floats, CKDWORD floatCount);

    bool operator==(const CKFFSpecializationInfo &other) const;
    bool operator!=(const CKFFSpecializationInfo &other) const { return !(*this == other); }

    // Texture argument (CKRST_TA_*) <-> 5-bit field: base arg in bits 0..2,
    // COMPLEMENT / ALPHAREPLICATE modifiers in bits 3..4.
    static CKDWORD RepackArg(CKDWORD arg);
    static CKDWORD UnpackArg(CKDWORD packed);

private:
    void SetBits(const CKFFSpecBitfield &layout, CKDWORD value);
    CKDWORD GetBits(const CKFFSpecBitfield &layout) const;

    CKDWORD m_Lanes[CKFF_SPEC_LANE_COUNT];
};

#endif // CKFFSPECIALIZATIONINFO_H
