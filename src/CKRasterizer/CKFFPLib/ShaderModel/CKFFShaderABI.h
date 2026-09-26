#ifndef CKFFSHADERABI_H
#define CKFFSHADERABI_H

#include "CKBuiltinShaderIdentity.h"
#include "CKFFConstants.h"
#include "CKFFShaderKey.h"
#include "CKFFSpecializationInfo.h"
#include "CKRasterizerContextEnums.h"

// Internal fixed-function shader ABI. These values define the logical C++
// data consumed by the shared shader calculations. CKFFShaderInterface maps
// it to named uniforms or native stage buffers for each artifact family.
// u_bumpEnv[stage * 2 + 1].w packs minimum mip in bits 0..4 and the
// fixed-function anisotropy tap cap in bits 5..9 (zero disables manual taps),
// the per-axis manual border mask in bits 10..12, and the min/mag linear
// filter choices in bits 13..14. bgfx's
// u_borderSampler[native slot] stores actual mip count and mip filter in xy.

// VXRENDERSTATE_ZBIAS is converted to the D3D8 compatibility depth-bias
// scale selected for the active depth-buffer format. The resolved positive
// offset is subtracted in the vertex shaders.
inline float CKFFDepthBiasUnit(CK_DEPTH_FORMAT format)
{
    switch (format) {
    case CKRST_DEPTHFMT_D16:
        return 1.0f / 65535.0f;
    case CKRST_DEPTHFMT_D24:
    case CKRST_DEPTHFMT_D24S8:
    case CKRST_DEPTHFMT_D32F:
    default:
        // A true 24-bit epsilon rounds away in the D3D float DEPTHBIAS
        // representation. D3D8 compatibility layers use 20 effective bits.
        return 1.0f / 1048575.0f;
    }
}

enum CKFFDrawParamSlot {
    CKFF_DRAW_PARAM_MATERIAL_DIFFUSE = 0,
    CKFF_DRAW_PARAM_MATERIAL_AMBIENT = 1,
    CKFF_DRAW_PARAM_MATERIAL_SPECULAR = 2,
    // rgb = emissive material, w = 16-bit line pattern.
    CKFF_DRAW_PARAM_MATERIAL_EMISSIVE = 3,
    CKFF_DRAW_PARAM_MATERIAL_POWER = 4, // x = specular power, y = ZBIAS, z = affine interpolation, w = point offset source
    CKFF_DRAW_PARAM_MATERIAL_SOURCES = 5,
    CKFF_DRAW_PARAM_LIGHTING = 6,
    CKFF_DRAW_PARAM_LIGHT_FLAGS = 7,
    CKFF_DRAW_PARAM_ALPHA = 8,
    CKFF_DRAW_PARAM_TEXTURE_FACTOR = 9,
    CKFF_DRAW_PARAM_FOG = 10,
    // rgb = fog color, w = line-pattern repeat factor.
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
    CKFF_SAMPLER_SLOT_COUNT = CKFF_WIDE_SAMPLER_COUNT +
                              CKFF_NARROW_SAMPLER_COUNT * 2,
};

inline CKDWORD CKFFSamplerTypeSlotCount(CKDWORD samplerType,
                                        CKFFSamplerLayout layout =
                                            CKFF_SAMPLER_LAYOUT_WIDE_2D) {
    if (samplerType == CKFF_SAMPLER_CUBE)
        return layout == CKFF_SAMPLER_LAYOUT_WIDE_CUBE
            ? CKFF_WIDE_SAMPLER_COUNT : CKFF_NARROW_SAMPLER_COUNT;
    if (samplerType == CKFF_SAMPLER_VOLUME)
        return layout == CKFF_SAMPLER_LAYOUT_WIDE_VOLUME
            ? CKFF_WIDE_SAMPLER_COUNT : CKFF_NARROW_SAMPLER_COUNT;
    return layout == CKFF_SAMPLER_LAYOUT_WIDE_2D
        ? CKFF_WIDE_SAMPLER_COUNT : CKFF_NARROW_SAMPLER_COUNT;
}

inline CKDWORD CKFFSamplerTypeSlotBase(CKDWORD samplerType,
                                       CKFFSamplerLayout layout =
                                           CKFF_SAMPLER_LAYOUT_WIDE_2D) {
    if (samplerType == CKFF_SAMPLER_CUBE)
        return CKFFSamplerTypeSlotCount(CKFF_SAMPLER_2D, layout);
    if (samplerType == CKFF_SAMPLER_VOLUME)
        return CKFFSamplerTypeSlotCount(CKFF_SAMPLER_2D, layout) +
               CKFFSamplerTypeSlotCount(CKFF_SAMPLER_CUBE, layout);
    return 0;
}

// Texture slot of a sampler in the selected sixteen-slot layout. All sampler
// types are packed by type ordinal; comparison depth samplers lead the 2D
// block, followed by ordinary 2D/depth stages.
inline CKDWORD CKFFSamplerSlot(CKDWORD samplerType, CKDWORD ordinal,
                              CKFFSamplerLayout layout =
                                  CKFF_SAMPLER_LAYOUT_WIDE_2D) {
    return CKFFSamplerTypeSlotBase(samplerType, layout) + ordinal;
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
