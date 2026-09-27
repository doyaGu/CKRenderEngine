#ifndef CKFFFRAGMENTPROGRAMLAYOUT_H
#define CKFFFRAGMENTPROGRAMLAYOUT_H

// C++ view of CKFFFragmentProgramLayout.def: layout constants, field enums and the
// per-field bit positions. The shader side is generated from the same file
// (shaders/ff_fragment_program_layout.sh); never edit one without the other.

#include "CKRenderEngineTypes.h"

#define CKFF_FRAGMENT_PROGRAM_CONST(NAME, value) static const CKDWORD CKFF_FRAGMENT_PROGRAM_##NAME = value;
#define CKFF_FRAGMENT_PROGRAM_STAGE_FIELD(NAME, lane, offset, bits)
#define CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD(NAME, lane, offset, bits)
#include "CKFFFragmentProgramLayout.def"
#undef CKFF_FRAGMENT_PROGRAM_CONST
#undef CKFF_FRAGMENT_PROGRAM_STAGE_FIELD
#undef CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD

enum CKFFFragmentProgramStageField {
#define CKFF_FRAGMENT_PROGRAM_CONST(NAME, value)
#define CKFF_FRAGMENT_PROGRAM_STAGE_FIELD(NAME, lane, offset, bits) CKFF_FRAGMENT_PROGRAM_STAGE_##NAME,
#define CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD(NAME, lane, offset, bits)
#include "CKFFFragmentProgramLayout.def"
#undef CKFF_FRAGMENT_PROGRAM_CONST
#undef CKFF_FRAGMENT_PROGRAM_STAGE_FIELD
#undef CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD
    CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT
};

enum CKFFFragmentProgramGlobalField {
#define CKFF_FRAGMENT_PROGRAM_CONST(NAME, value)
#define CKFF_FRAGMENT_PROGRAM_STAGE_FIELD(NAME, lane, offset, bits)
#define CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD(NAME, lane, offset, bits) CKFF_FRAGMENT_PROGRAM_##NAME,
#include "CKFFFragmentProgramLayout.def"
#undef CKFF_FRAGMENT_PROGRAM_CONST
#undef CKFF_FRAGMENT_PROGRAM_STAGE_FIELD
#undef CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD
    CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT
};

struct CKFFFragmentProgramBitfield {
    CKDWORD Lane;
    CKDWORD BitOffset;
    CKDWORD BitCount;
};

struct CKFFFragmentProgramFieldDesc {
    const char *Name;
    CKFFFragmentProgramBitfield Layout; // stage fields: Lane relative to the stage base
};

inline const CKFFFragmentProgramFieldDesc &CKFFFragmentProgramStageFieldDesc(CKFFFragmentProgramStageField field) {
    static const CKFFFragmentProgramFieldDesc table[CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT] = {
#define CKFF_FRAGMENT_PROGRAM_CONST(NAME, value)
#define CKFF_FRAGMENT_PROGRAM_STAGE_FIELD(NAME, lane, offset, bits) {#NAME, {lane, offset, bits}},
#define CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD(NAME, lane, offset, bits)
#include "CKFFFragmentProgramLayout.def"
#undef CKFF_FRAGMENT_PROGRAM_CONST
#undef CKFF_FRAGMENT_PROGRAM_STAGE_FIELD
#undef CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD
    };
    return table[field];
}

inline const CKFFFragmentProgramFieldDesc &CKFFFragmentProgramGlobalFieldDesc(CKFFFragmentProgramGlobalField field) {
    static const CKFFFragmentProgramFieldDesc table[CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT] = {
#define CKFF_FRAGMENT_PROGRAM_CONST(NAME, value)
#define CKFF_FRAGMENT_PROGRAM_STAGE_FIELD(NAME, lane, offset, bits)
#define CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD(NAME, lane, offset, bits) {#NAME, {lane, offset, bits}},
#include "CKFFFragmentProgramLayout.def"
#undef CKFF_FRAGMENT_PROGRAM_CONST
#undef CKFF_FRAGMENT_PROGRAM_STAGE_FIELD
#undef CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD
    };
    return table[field];
}

// Absolute lane of a stage field.
inline CKFFFragmentProgramBitfield CKFFFragmentProgramStageFieldLayout(CKDWORD stage, CKFFFragmentProgramStageField field) {
    CKFFFragmentProgramBitfield layout = CKFFFragmentProgramStageFieldDesc(field).Layout;
    layout.Lane += CKFF_FRAGMENT_PROGRAM_STAGE_LANE_BASE + stage * CKFF_FRAGMENT_PROGRAM_STAGE_LANE_STRIDE;
    return layout;
}

inline CKFFFragmentProgramBitfield CKFFFragmentProgramGlobalFieldLayout(CKFFFragmentProgramGlobalField field) {
    return CKFFFragmentProgramGlobalFieldDesc(field).Layout;
}

static_assert(CKFF_FRAGMENT_PROGRAM_LANE_COUNT == CKFF_FRAGMENT_PROGRAM_VEC4_COUNT * 4,
              "fragment program lanes must fill whole vec4 uniforms");
static_assert(CKFF_FRAGMENT_PROGRAM_STAGE_LANE_BASE + CKFF_FRAGMENT_PROGRAM_STAGE_COUNT * CKFF_FRAGMENT_PROGRAM_STAGE_LANE_STRIDE <= CKFF_FRAGMENT_PROGRAM_GLOBAL_LANE_BASE,
              "stage lanes must end before the global lanes");
static_assert(CKFF_FRAGMENT_PROGRAM_LANE_BITS == 24, "lanes travel as exact fp32 integers below 2^24");

#endif // CKFFFRAGMENTPROGRAMLAYOUT_H
