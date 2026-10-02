#ifndef CKFFSHADERABI_H
#define CKFFSHADERABI_H

#include "CKBuiltinShaderIdentity.h"
#include "CKFFConstants.h"
#include "CKFFSamplerLayout.h"
#include "CKFFShaderKey.h"
#include "CKFFFragmentProgram.h"
#include "CKRasterizerContextEnums.h"

// Internal fixed-function shader ABI. These values define the logical C++
// data consumed by the shared shader calculations. CKFFShaderInterface maps
// it to named uniforms or native stage buffers for each artifact family.
// u_bumpEnv[stage * 2 + 1].w carries CKFFSamplerShaderState as an exact
// 24-bit integer float. bgfx's u_borderSampler[native slot] stores actual mip
// count and mip filter in xy.

// This enum is also parsed by ShaderModel/shader_abi_codegen.py. Keep every
// value as a numeric literal so generated shader definitions have this header
// as their single source of truth.
enum CKFFSamplerShaderStateABI {
    CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT = 0,
    CKFF_SAMPLER_SHADER_MIN_MIP_MASK = 0x0000001fu,
    CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT = 5,
    CKFF_SAMPLER_SHADER_ANISOTROPY_MASK = 0x0000001fu,
    CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT = 10,
    CKFF_SAMPLER_SHADER_BORDER_AXIS_MASK = 0x00000007u,
    CKFF_SAMPLER_SHADER_MIN_FILTER_LINEAR = 0x00002000u,
    CKFF_SAMPLER_SHADER_MAG_FILTER_LINEAR = 0x00004000u,
    CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT = 0x00008000u,
    CKFF_SAMPLER_SHADER_MANUAL_LOD = 0x00010000u,
    CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY = 0x00020000u,
    CKFF_SAMPLER_SHADER_MANUAL_BORDER = 0x00040000u,
    CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE = 0x00080000u,
    CKFF_SAMPLER_SHADER_STATE_VALID_MASK = 0x000fffffu,
};

struct CKFFSamplerShaderState {
    CKDWORD Bits;

    CKFFSamplerShaderState(CKDWORD bits = 0) : Bits(bits) {}

    CKDWORD MinimumMipLevel() const {
        return (Bits >> CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT) &
               CKFF_SAMPLER_SHADER_MIN_MIP_MASK;
    }
    CKDWORD AnisotropyTapCount() const {
        return (Bits >> CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT) &
               CKFF_SAMPLER_SHADER_ANISOTROPY_MASK;
    }
    CKDWORD BorderAxisMask() const {
        return (Bits >> CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT) &
               CKFF_SAMPLER_SHADER_BORDER_AXIS_MASK;
    }
    CKBOOL Has(CKDWORD flag) const {
        return (Bits & flag) != 0 ? TRUE : FALSE;
    }
};

static_assert(CKFF_SAMPLER_SHADER_STATE_VALID_MASK < (1u << 24),
              "Sampler shader state must remain exactly representable in fp32");

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
    // x = specular power, y = ZBIAS, z = affine interpolation,
    // w = geometry expansion mode (point offset or antialiased line).
    CKFF_DRAW_PARAM_MATERIAL_POWER = 4,
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

// u_stageParams: per-draw stage data that is not part of the fragment program
// (combiner ops / args, sampler kinds and switches live in u_ffProgram).
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
    CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT = CKFFFragmentProgram::Vec4Count,
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
    const CKDWORD typeIndex = samplerType == CKFF_SAMPLER_CUBE ? 1u :
                              samplerType == CKFF_SAMPLER_VOLUME ? 2u : 0u;
    switch (layout) {
#define CKFF_SAMPLER_LAYOUT(name, value, twoD, cube, volume) \
    case CKFF_SAMPLER_LAYOUT_##name: { \
        static const CKBYTE counts[3] = {twoD, cube, volume}; \
        return counts[typeIndex]; \
    }
#include "CKFFSamplerLayout.def"
#undef CKFF_SAMPLER_LAYOUT
    default:
        return 0;
    }
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
static_assert(CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT == 5, "ABI break: fragment program vec4 count changed");
static_assert(CKFF_MATRIX_VEC4_COUNT == 8, "ABI break: matrix vec4 count changed");
static_assert(CKFF_CLIP_PLANE_COUNT == 6, "ABI break: clip plane count changed");
static_assert(CKFF_DRAW_PARAM_INLINE_LIGHT_BASE == 12, "ABI break: inline light base changed");
static_assert(CKFF_SAMPLER_SLOT_COUNT == CKFF_SHADER_SAMPLER_SLOT_COUNT,
              "ABI break: fixed sampler layout changed");

#endif // CKFFSHADERABI_H
