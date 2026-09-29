#ifndef CKFFNATIVEFRAGMENTJIT_H
#define CKFFNATIVEFRAGMENTJIT_H

#include "CKFFConstants.h"
#include "CKFFFragmentProgram.h"
#include "CKJitIR.h"

// Runtime specialization of the native hardware-sampling fixed-function
// fragment shaders, whose ckffEvaluate() decodes u_ffProgram per fragment:
// fs_ff_stage_native (CKFF_SAMPLER_LAYOUT_WIDE_2D), fs_ff_stage_cube_native
// (WIDE_CUBE) and fs_ff_stage_volume_native (WIDE_VOLUME).
//
// Compiles what the shader of the layout evaluates for one fragment program.
// Only the program becomes constant, and the result keeps the shader's
// interface so that it can replace the shader in the same pipeline: every
// varying at its location and register, the fragment uniform block of the
// native program interface, and the layout's sampler slots. The draw state
// outside the program is still read from the uniforms (texture presence,
// stage constants and flags, bump and LOD parameters, texture factor, alpha
// reference, fog parameters, line and affine modes), so one compiled program
// serves every draw of its fragment program.
//
// Returns false for a layout without native shaders, or if the IR builder
// rejects the program, which is a front end bug.
bool CKFFCompileNativeFragmentProgram(const CKFFFragmentProgram &program, CKFFSamplerLayout layout,
                                      CKJitFragmentShader &out);

#endif // CKFFNATIVEFRAGMENTJIT_H
