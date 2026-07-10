#ifndef CKFFSHADERABI_H
#define CKFFSHADERABI_H

#include "CKFFConstants.h"
#include "CKFFShaderKey.h"
#include "CKFFSpecializationInfo.h"

// Internal fixed-function shader ABI. These values define the C++ uniform
// packing contract consumed by the checked-in bgfx shader sources.

static const CKDWORD CKFF_SHADER_ABI_VERSION = 1u;
static const CKDWORD CKFF_SHADER_INTERFACE_HASH = 0x6f7e2a31u;

enum CKFFDrawParamSlot {
    CKFF_DRAW_PARAM_MATERIAL_DIFFUSE = 0,
    CKFF_DRAW_PARAM_MATERIAL_AMBIENT = 1,
    CKFF_DRAW_PARAM_MATERIAL_SPECULAR = 2,
    CKFF_DRAW_PARAM_MATERIAL_EMISSIVE = 3,
    CKFF_DRAW_PARAM_MATERIAL_POWER = 4,
    CKFF_DRAW_PARAM_MATERIAL_SOURCES = 5,
    CKFF_DRAW_PARAM_LIGHTING = 6,
    CKFF_DRAW_PARAM_LIGHT_FLAGS = 7,
    CKFF_DRAW_PARAM_ALPHA = 8,
    CKFF_DRAW_PARAM_TEXTURE_FACTOR = 9,
    CKFF_DRAW_PARAM_FOG = 10,
    CKFF_DRAW_PARAM_FOG_COLOR = 11,
    CKFF_DRAW_PARAM_INLINE_LIGHT_BASE = 12,
    CKFF_DRAW_PARAM_VEC4_COUNT = 19,
};

enum CKFFStageParamSlot {
    CKFF_STAGE_PARAM_COLOR = 0,
    CKFF_STAGE_PARAM_ALPHA = 1,
    CKFF_STAGE_PARAM_COLOR_EXTRA = 2,
    CKFF_STAGE_PARAM_ALPHA_EXTRA = 3,
    CKFF_STAGE_PARAM_VEC4S_PER_STAGE = 4,
    CKFF_STAGE_PARAM_VEC4_COUNT = CKFF_MAX_TEXTURE_STAGES * CKFF_STAGE_PARAM_VEC4S_PER_STAGE,
};

enum CKFFMatrixABI {
    CKFF_MATRIX_MVP_OR_VIEWPROJ = 0,
    CKFF_MATRIX_WORLD = 1,
    CKFF_MATRIX_MODELVIEW = 2,
    CKFF_MATRIX_NORMAL = 3,
    CKFF_MATRIX_VEC4_COUNT = 8,
};

enum CKFFClipABI {
    CKFF_CLIP_PLANE_COUNT = 6,
    CKFF_SPEC_UNIFORM_VEC4_COUNT = CKFFSpecializationInfo::MaxSpecDwords,
};

inline CKDWORD CKFFStageParamIndex(CKDWORD stage, CKFFStageParamSlot slot) {
    return stage * CKFF_STAGE_PARAM_VEC4S_PER_STAGE + (CKDWORD)slot;
}

inline CKDWORD CKFFSamplerBindStage(CKDWORD stage, CKDWORD samplerType) {
    if (samplerType == CKFF_SAMPLER_CUBE || samplerType == CKFF_SAMPLER_VOLUME)
        return stage + CKFF_MAX_TEXTURE_STAGES;
    return stage;
}

static_assert(CKFF_DRAW_PARAM_VEC4_COUNT == 19, "ABI break: draw param vec4 count changed");
static_assert(CKFF_STAGE_PARAM_VEC4S_PER_STAGE == 4, "ABI break: stage param vec4s per stage changed");
static_assert(CKFF_MATRIX_VEC4_COUNT == 8, "ABI break: matrix vec4 count changed");
static_assert(CKFF_CLIP_PLANE_COUNT == 6, "ABI break: clip plane count changed");
static_assert(CKFF_DRAW_PARAM_INLINE_LIGHT_BASE == 12, "ABI break: inline light base changed");

#endif // CKFFSHADERABI_H
