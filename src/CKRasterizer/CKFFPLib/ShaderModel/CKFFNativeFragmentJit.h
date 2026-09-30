#ifndef CKFFNATIVEFRAGMENTJIT_H
#define CKFFNATIVEFRAGMENTJIT_H

#include "CKFFConstantSet.h"
#include "CKFFConstants.h"
#include "CKFFFragmentProgram.h"
#include "CKJitIR.h"

// Runtime specialization of the native hardware-sampling fixed-function
// fragment shaders, whose ckffEvaluate() decodes u_ffProgram per fragment:
// fs_ff_stage_native (CKFF_SAMPLER_LAYOUT_WIDE_2D), fs_ff_stage_cube_native
// (WIDE_CUBE) and fs_ff_stage_volume_native (WIDE_VOLUME).
//
// A key is what becomes constant: the fragment program, and switches for the
// draw state the shaders branch on (texture presence, bump encodings, LOD
// biases, STAGEBLEND factors, line and affine modes). The rest of the draw
// state is still read from the uniforms (stage constants, bump matrices and
// luminance, LOD bias values, texture factor, alpha reference, fog
// parameters), so one compiled key serves every draw it names.

enum {
    CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT = 3,
};

// Switch word 0. The per-stage switches are shifted left by the stage. Words
// 1 and 2 hold the STAGEBLEND factor pair of stage s, source factor in the
// high nibble, in byte s % 4 of word 1 + s / 4.
enum CKFFNativeFragmentSwitch {
    CKFF_NATIVE_FRAGMENT_AFFINE = 1u << 0,      // affine texture coordinates
    CKFF_NATIVE_FRAGMENT_LINE = 1u << 1,        // antialiased line coverage
    CKFF_NATIVE_FRAGMENT_TEXTURE = 1u << 8,     // the stage has a texture
    CKFF_NATIVE_FRAGMENT_BUMP_UNORM = 1u << 16, // the stage's bump texels are unsigned
    CKFF_NATIVE_FRAGMENT_LOD_BIAS = 1u << 24,   // the stage's LOD bias is not zero
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
// shaders test them. Constants past a block's end read as zero.
CKFFNativeFragmentKey CKFFNativeFragmentDrawKey(const CKFFFragmentProgram &program, const CKFFConstantSet &constants);

// Clears what the shader of a layout never reads from a key, so that keys
// compiling to the same shader are equal: stages past the first disabled one,
// operations and arguments that compute nothing, textures and switches no
// value depends on, and state the fragment shaders do not use. Canonical keys
// are left unchanged.
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
