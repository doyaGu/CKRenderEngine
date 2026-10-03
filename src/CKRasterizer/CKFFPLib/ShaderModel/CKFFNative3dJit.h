#ifndef CKFFNATIVE3DJIT_H
#define CKFFNATIVE3DJIT_H

#include "CKFFNativeFragmentJit.h"

// Ordinary, tweened and matrix-blended 3D vertices. Eligibility is checked
// for every draw, before choosing a
// program binding; these switches do not enter the fragment compilation key.
bool CKFFNativeUnlitDraw(const CKFFConstantSet &constants);
bool CKFFNativeLitDraw(const CKFFConstantSet &constants);

// Keeps the native 3D vertex ABI. Texture presence and affine interpolation
// follow the canonical fragment key; all other supported state is uniform.
// referenceFormat names the precompiled fallback, not the emitted format.
bool CKFFCompileNativeUnlitProgram(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                   CK_SHADER_FORMAT referenceFormat, CKJitVertexShader &out, bool clipping = false);
bool CKFFCompileNativeLitProgram(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                 CK_SHADER_FORMAT referenceFormat, CKJitVertexShader &out, bool clipping = false);

#endif
