#ifndef CKSDLGPU_SHADERS_H
#define CKSDLGPU_SHADERS_H

#include "CKBuiltinShaders.h"
#include "CKFFConstants.h"
#include "CKFFProgram.h"
#include <SDL3/SDL_gpu.h>

// FFP/presentation artifacts supplied by the rasterizer plugin. Private
// backend image operations have their own CKSdlGpuNativeShaders interface.
enum CKSdlGpuFFComparisonProfile {
    CKSDL_GPU_FF_COMPARE_BASE = 0,
    CKSDL_GPU_FF_COMPARE_NATIVE_ONE,
    CKSDL_GPU_FF_COMPARE_MANUAL,
    CKSDL_GPU_FF_COMPARE_PROFILE_COUNT
};

CKBOOL CKSdlGpuShaderSet(SDL_GPUShaderFormat Format, CKFFShaderSet &Out);
CKSdlGpuFFComparisonProfile CKSdlGpuFFResolveComparisonProfile(
    CKFFSamplerLayout SamplerLayout,
    CKDWORD CompareSamplerCount,
    CKFFFragmentSamplingMode SamplingMode);
CKBOOL CKSdlGpuFFFragmentShader(SDL_GPUShaderFormat Format,
                               CKFFSamplerLayout SamplerLayout,
                               CKDWORD CompareSamplerCount,
                               CKFFFragmentSamplingMode SamplingMode,
                               CKShaderDesc &Out);
CKBOOL CKSdlGpuFFDepthPadVertexShader(SDL_GPUShaderFormat Format,
                                     CKBOOL Clipping,
                                     CKShaderDesc &Out);

#endif
