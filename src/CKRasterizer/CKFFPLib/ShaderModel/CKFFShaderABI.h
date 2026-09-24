#ifndef CKFFSHADERABI_H
#define CKFFSHADERABI_H

#include "CKBuiltinShaderIdentity.h"
#include "CKFFConstants.h"
#include "CKFFShaderKey.h"
#include "CKFFSpecializationInfo.h"

// Internal fixed-function shader ABI. These values define the logical C++
// data consumed by the shared shader calculations. CKFFShaderInterface maps
// it to named uniforms or native stage buffers for each artifact family.
// u_bumpEnv[stage * 2 + 1].w packs minimum mip in bits 0..4 and the
// fixed-function anisotropy tap cap in bits 5..9 (zero disables manual taps).

// VXRENDERSTATE_ZBIAS (0..16) approximation: each unit moves the clip-space
// depth of the draw towards the viewer by this fraction of the depth range
// (u_ffDrawParams[CKFF_DRAW_PARAM_MATERIAL_POWER].y, applied in the vertex shaders).
static const float CKFF_ZBIAS_DEPTH_UNIT = 0.000005f;

enum CKFFDrawParamSlot {
    CKFF_DRAW_PARAM_MATERIAL_DIFFUSE = 0,
    CKFF_DRAW_PARAM_MATERIAL_AMBIENT = 1,
    CKFF_DRAW_PARAM_MATERIAL_SPECULAR = 2,
    CKFF_DRAW_PARAM_MATERIAL_EMISSIVE = 3,
    CKFF_DRAW_PARAM_MATERIAL_POWER = 4, // x = specular power, y = ZBIAS, z = affine texture interpolation
    CKFF_DRAW_PARAM_MATERIAL_SOURCES = 5,
    CKFF_DRAW_PARAM_LIGHTING = 6,
    CKFF_DRAW_PARAM_LIGHT_FLAGS = 7,
    CKFF_DRAW_PARAM_ALPHA = 8,
    CKFF_DRAW_PARAM_TEXTURE_FACTOR = 9,
    CKFF_DRAW_PARAM_FOG = 10,
    CKFF_DRAW_PARAM_FOG_COLOR = 11,
    CKFF_DRAW_PARAM_INLINE_LIGHT_BASE = 12,
    CKFF_DRAW_PARAM_TWEEN = 19,
    CKFF_DRAW_PARAM_VEC4_COUNT = 20,
};

// u_stageParams: per-draw stage data that is not part of the specialization
// (combiner ops / args, sampler kinds and switches live in u_ffSpec).
enum CKFFStageParamSlot {
    // x = packed TEXCOORDINDEX (index | texgen << 16), y = texture transform
    // flags (count, PROJECTED, MIRRORONCE axes, render-target flip, bump
    // unorm), z = 1 when the stage samples a bound texture, w = STAGEBLEND pair.
    CKFF_STAGE_PARAM_COORD = 0,
    // RGBA stage constant (CKRST_TSS_CONSTANT).
    CKFF_STAGE_PARAM_CONSTANT = 1,
    CKFF_STAGE_PARAM_VEC4S_PER_STAGE = 2,
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
    CKFF_SPEC_UNIFORM_VEC4_COUNT = CKFFSpecializationInfo::Vec4Count,
};

inline CKDWORD CKFFStageParamIndex(CKDWORD stage, CKFFStageParamSlot slot) {
    return stage * CKFF_STAGE_PARAM_VEC4S_PER_STAGE + (CKDWORD)slot;
}

enum CKFFSamplerSlotABI {
    CKFF_CUBE_SAMPLER_SLOT_BASE = CKFF_MAX_TEXTURE_STAGES,
    CKFF_VOLUME_SAMPLER_SLOT_BASE = CKFF_MAX_TEXTURE_STAGES + CKFF_CUBE_SAMPLER_COUNT,
    CKFF_SAMPLER_SLOT_COUNT = CKFF_MAX_TEXTURE_STAGES + CKFF_CUBE_SAMPLER_COUNT + CKFF_VOLUME_SAMPLER_COUNT,
};

// Texture slot of a sampler in the fixed layout: 2D and depth samplers sit on
// their stage index, cube and volume samplers on their type block indexed by
// the ordinal of the stage among the stages sampling the same type (see
// CKFFSamplerOrdinal / ckffSamplerOrdinal in fs_ff_stage.sc).
inline CKDWORD CKFFSamplerSlot(CKDWORD samplerType, CKDWORD stageOrOrdinal) {
    if (samplerType == CKFF_SAMPLER_CUBE)
        return CKFF_CUBE_SAMPLER_SLOT_BASE + stageOrOrdinal;
    if (samplerType == CKFF_SAMPLER_VOLUME)
        return CKFF_VOLUME_SAMPLER_SLOT_BASE + stageOrOrdinal;
    return stageOrOrdinal;
}

inline CKDWORD CKFFSamplerTypeSlotCount(CKDWORD samplerType) {
    if (samplerType == CKFF_SAMPLER_CUBE)
        return CKFF_CUBE_SAMPLER_COUNT;
    if (samplerType == CKFF_SAMPLER_VOLUME)
        return CKFF_VOLUME_SAMPLER_COUNT;
    return CKFF_MAX_TEXTURE_STAGES;
}

static_assert(CKFF_DRAW_PARAM_VEC4_COUNT == 20, "ABI break: draw param vec4 count changed");
static_assert(CKFF_STAGE_PARAM_VEC4S_PER_STAGE == 2, "ABI break: stage param vec4s per stage changed");
static_assert(CKFF_SPEC_UNIFORM_VEC4_COUNT == 5, "ABI break: specialization vec4 count changed");
static_assert(CKFF_MATRIX_VEC4_COUNT == 8, "ABI break: matrix vec4 count changed");
static_assert(CKFF_CLIP_PLANE_COUNT == 6, "ABI break: clip plane count changed");
static_assert(CKFF_DRAW_PARAM_INLINE_LIGHT_BASE == 12, "ABI break: inline light base changed");
static_assert(CKFF_SAMPLER_SLOT_COUNT == CKFF_SHADER_SAMPLER_SLOT_COUNT,
              "ABI break: fixed sampler layout changed");

#endif // CKFFSHADERABI_H
