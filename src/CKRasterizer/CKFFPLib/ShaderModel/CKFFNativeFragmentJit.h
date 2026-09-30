#ifndef CKFFNATIVEFRAGMENTJIT_H
#define CKFFNATIVEFRAGMENTJIT_H

#include "CKFFConstantSet.h"
#include "CKFFConstants.h"
#include "CKFFFragmentProgram.h"
#include "CKJitIR.h"

// Runtime specialization of the fixed-function fragment shaders whose
// ckffEvaluate() decodes u_ffProgram per fragment. The native shaders sample
// in hardware: fs_ff_stage_native (CKFF_SAMPLER_LAYOUT_WIDE_2D),
// fs_ff_stage_cube_native (WIDE_CUBE) and fs_ff_stage_volume_native
// (WIDE_VOLUME). Their shader-sampling counterparts fs_ff_stage,
// fs_ff_stage_cube and fs_ff_stage_volume emulate the sampler state the
// hardware samplers lack (mirror-once, border addressing, minimum mip levels,
// explicit gradients, anisotropy and depth comparison) from the sampler
// metadata of the program interface.
//
// A key is what becomes constant: the fragment program, and switches for the
// draw state the shaders branch on (texture presence, bump encodings, LOD
// biases, STAGEBLEND factors, line and affine modes, emulated sampler state).
// The rest of the draw state is still read from the uniforms (stage
// constants, bump matrices and luminance, LOD bias values, texture factor,
// alpha reference, fog parameters, sampler metadata), so one compiled key
// serves every draw it names.

enum {
    CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT = 5,
};

// Switch word 0. The per-stage switches are shifted left by the stage. Words
// 1 and 2 hold the STAGEBLEND factor pair of stage s, source factor in the
// high nibble, in byte s % 4 of word 1 + s / 4. Words 3 and 4 hold the
// CKFFNativeFragmentSampling flags of stage s in byte s % 4 of word 3 + s / 4.
enum CKFFNativeFragmentSwitch {
    CKFF_NATIVE_FRAGMENT_AFFINE = 1u << 0,           // affine texture coordinates
    CKFF_NATIVE_FRAGMENT_LINE = 1u << 1,             // antialiased line coverage
    CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING = 1u << 2,  // the shader-sampling counterpart
    CKFF_NATIVE_FRAGMENT_TEXTURE = 1u << 8,          // the stage has a texture
    CKFF_NATIVE_FRAGMENT_BUMP_UNORM = 1u << 16,      // the stage's bump texels are unsigned
    CKFF_NATIVE_FRAGMENT_LOD_BIAS = 1u << 24,        // the stage's LOD bias is not zero
};

// The sampler state a shader-sampling key emulates for a stage, from the
// stage's sampler state word and mirror-once mask.
enum CKFFNativeFragmentSampling {
    CKFF_NATIVE_FRAGMENT_MIRROR_U = 1u << 0,    // mirror-once U
    CKFF_NATIVE_FRAGMENT_MIRROR_V = 1u << 1,    // mirror-once V
    CKFF_NATIVE_FRAGMENT_MIRROR_W = 1u << 2,    // mirror-once W
    CKFF_NATIVE_FRAGMENT_BORDER = 1u << 3,      // an axis has border addressing
    CKFF_NATIVE_FRAGMENT_GRADIENT = 1u << 4,    // explicit gradients
    CKFF_NATIVE_FRAGMENT_ANISOTROPY = 1u << 5,  // manual anisotropy
    CKFF_NATIVE_FRAGMENT_MIN_MIP = 1u << 6,     // a minimum mip level
};

struct CKFFNativeFragmentKey {
    CKFFFragmentProgram Program;
    CKDWORD Switches[CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT];

    CKFFNativeFragmentKey() : Switches() {}
    bool operator==(const CKFFNativeFragmentKey &other) const;
    bool operator!=(const CKFFNativeFragmentKey &other) const { return !(*this == other); }
};

// The key of a draw: its fragment program, and the switches its
// CKRST_BLOCK_DRAW_PARAMS, BUMP_ENV and STAGE_PARAMS constants select, as the
// shaders test them. Constants past a block's end read as zero. A draw of a
// shader-sampling shader passes shaderSampling.
CKFFNativeFragmentKey CKFFNativeFragmentDrawKey(const CKFFFragmentProgram &program, const CKFFConstantSet &constants,
                                                bool shaderSampling);

// Clears what the shader of a layout never reads from a key, so that keys
// compiling to the same shader are equal: stages past the first disabled one,
// operations and arguments that compute nothing, textures and switches no
// value depends on, and state the fragment shaders do not use. A
// shader-sampling key that emulates nothing becomes the native key, which
// compiles to the same shader. Canonical keys are left unchanged.
void CKFFCanonicalizeNativeFragmentKey(CKFFNativeFragmentKey &key, CKFFSamplerLayout layout);

// Compiles what the shader of the layout evaluates for the draws of a key,
// canonical or not. The result keeps the shader's interface so that it can
// replace the shader in the same pipeline: every varying at its location and
// register, the fragment uniform block of the native program interface, and
// the layout's sampler slots.
//
// Returns false for a layout without native shaders, or if the IR builder
// rejects the program, which is a front end bug.
bool CKFFCompileNativeFragmentProgram(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                      CKJitFragmentShader &out);

#endif // CKFFNATIVEFRAGMENTJIT_H
