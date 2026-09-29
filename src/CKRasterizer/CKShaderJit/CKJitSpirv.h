#ifndef CKJITSPIRV_H
#define CKJITSPIRV_H

#include "CKJitIR.h"

// Translates a finished fragment shader to a Vulkan 1.0 SPIR-V module with
// the entry point "main", lowered the way DXC lowers the equivalent HLSL:
// min/max are NMin/NMax, saturate is FClamp, a vector select splats its
// condition and the discard is an OpKill after all other work. Inputs are
// declared by location (or as FragCoord), the uniform block is an array of
// float4 rows and every sampler slot is a combined image sampler. Returns
// false for a program the backend cannot express; words is then undefined.
bool CKJitEmitSpirv(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout,
                    XArray<uint32_t> &words);

#endif // CKJITSPIRV_H
