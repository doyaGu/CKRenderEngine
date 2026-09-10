#ifndef CKFFSPECLAYOUT_H
#define CKFFSPECLAYOUT_H

// C++ view of CKFFSpecLayout.def: layout constants, field enums and the
// per-field bit positions. The shader side is generated from the same file
// (shaders/ff_spec_layout.sh); never edit one without the other.

#include "CKRenderEngineTypes.h"

#define CKFF_SPEC_CONST(NAME, value) static const CKDWORD CKFF_SPEC_##NAME = value;
#define CKFF_SPEC_STAGE_FIELD(NAME, lane, offset, bits)
#define CKFF_SPEC_GLOBAL_FIELD(NAME, lane, offset, bits)
#include "CKFFSpecLayout.def"
#undef CKFF_SPEC_CONST
#undef CKFF_SPEC_STAGE_FIELD
#undef CKFF_SPEC_GLOBAL_FIELD

enum CKFFSpecStageField {
#define CKFF_SPEC_CONST(NAME, value)
#define CKFF_SPEC_STAGE_FIELD(NAME, lane, offset, bits) CKFF_SPEC_STAGE_##NAME,
#define CKFF_SPEC_GLOBAL_FIELD(NAME, lane, offset, bits)
#include "CKFFSpecLayout.def"
#undef CKFF_SPEC_CONST
#undef CKFF_SPEC_STAGE_FIELD
#undef CKFF_SPEC_GLOBAL_FIELD
    CKFF_SPEC_STAGE_FIELD_COUNT
};

enum CKFFSpecGlobalField {
#define CKFF_SPEC_CONST(NAME, value)
#define CKFF_SPEC_STAGE_FIELD(NAME, lane, offset, bits)
#define CKFF_SPEC_GLOBAL_FIELD(NAME, lane, offset, bits) CKFF_SPEC_##NAME,
#include "CKFFSpecLayout.def"
#undef CKFF_SPEC_CONST
#undef CKFF_SPEC_STAGE_FIELD
#undef CKFF_SPEC_GLOBAL_FIELD
    CKFF_SPEC_GLOBAL_FIELD_COUNT
};

struct CKFFSpecBitfield {
    CKDWORD Lane;
    CKDWORD BitOffset;
    CKDWORD BitCount;
};

struct CKFFSpecFieldDesc {
    const char *Name;
    CKFFSpecBitfield Layout; // stage fields: Lane relative to the stage base
};

inline const CKFFSpecFieldDesc &CKFFSpecStageFieldDesc(CKFFSpecStageField field) {
    static const CKFFSpecFieldDesc table[CKFF_SPEC_STAGE_FIELD_COUNT] = {
#define CKFF_SPEC_CONST(NAME, value)
#define CKFF_SPEC_STAGE_FIELD(NAME, lane, offset, bits) {#NAME, {lane, offset, bits}},
#define CKFF_SPEC_GLOBAL_FIELD(NAME, lane, offset, bits)
#include "CKFFSpecLayout.def"
#undef CKFF_SPEC_CONST
#undef CKFF_SPEC_STAGE_FIELD
#undef CKFF_SPEC_GLOBAL_FIELD
    };
    return table[field];
}

inline const CKFFSpecFieldDesc &CKFFSpecGlobalFieldDesc(CKFFSpecGlobalField field) {
    static const CKFFSpecFieldDesc table[CKFF_SPEC_GLOBAL_FIELD_COUNT] = {
#define CKFF_SPEC_CONST(NAME, value)
#define CKFF_SPEC_STAGE_FIELD(NAME, lane, offset, bits)
#define CKFF_SPEC_GLOBAL_FIELD(NAME, lane, offset, bits) {#NAME, {lane, offset, bits}},
#include "CKFFSpecLayout.def"
#undef CKFF_SPEC_CONST
#undef CKFF_SPEC_STAGE_FIELD
#undef CKFF_SPEC_GLOBAL_FIELD
    };
    return table[field];
}

// Absolute lane of a stage field.
inline CKFFSpecBitfield CKFFSpecStageFieldLayout(CKDWORD stage, CKFFSpecStageField field) {
    CKFFSpecBitfield layout = CKFFSpecStageFieldDesc(field).Layout;
    layout.Lane += CKFF_SPEC_STAGE_LANE_BASE + stage * CKFF_SPEC_STAGE_LANE_STRIDE;
    return layout;
}

inline CKFFSpecBitfield CKFFSpecGlobalFieldLayout(CKFFSpecGlobalField field) {
    return CKFFSpecGlobalFieldDesc(field).Layout;
}

static_assert(CKFF_SPEC_LANE_COUNT == CKFF_SPEC_VEC4_COUNT * 4,
              "specialization lanes must fill whole vec4 uniforms");
static_assert(CKFF_SPEC_STAGE_LANE_BASE + CKFF_SPEC_STAGE_COUNT * CKFF_SPEC_STAGE_LANE_STRIDE <= CKFF_SPEC_GLOBAL_LANE_BASE,
              "stage lanes must end before the global lanes");
static_assert(CKFF_SPEC_LANE_BITS == 24, "lanes travel as exact fp32 integers below 2^24");

#endif // CKFFSPECLAYOUT_H
