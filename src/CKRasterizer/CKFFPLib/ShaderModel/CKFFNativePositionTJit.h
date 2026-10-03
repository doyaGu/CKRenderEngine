#ifndef CKFFNATIVEPOSITIONTJIT_H
#define CKFFNATIVEPOSITIONTJIT_H

#include "CKFFNativeFragmentJit.h"

// POSITIONT with optional user clip distances.
// Specializes texture-coordinate work and affine interpolation from the
// canonical fragment key. Every other vertex switch remains a uniform, so
// the fragment cache key also identifies this paired vertex program.
// Outputs keep the complete native fragment interface. Viewport, RHW, depth
// bias, fog, line/point expansion, coordinate indices and projection flags
// retain the semantics of vs_ff_positiont.sc.
// referenceFormat identifies the precompiled fallback (DXIL or SPIR-V), not
// the JIT output format. Its optimized position arithmetic must be retained
// for EQUAL depth tests across the asynchronous fallback/JIT transition.
bool CKFFCompileNativePositionTProgram(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                       CK_SHADER_FORMAT referenceFormat, CKJitVertexShader &out, bool clipping = false);

#endif
