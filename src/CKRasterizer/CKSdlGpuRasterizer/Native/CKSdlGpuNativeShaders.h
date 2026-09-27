#ifndef CKSDLGPU_NATIVE_SHADERS_H
#define CKSDLGPU_NATIVE_SHADERS_H

#include "CKFFProgramDesc.h"
#include <SDL3/SDL_gpu.h>

// Private image operations of this backend, independent of rasterizer shaders.
CKBOOL CKSdlGpuNativeClearShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment);
CKBOOL CKSdlGpuNativeVolumeShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment);
CKBOOL CKSdlGpuNativeDitherShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment);
enum CKSdlGpuNativeProgramKind {
    CKSDL_NATIVE_CLEAR,
    CKSDL_NATIVE_VOLUME,
    CKSDL_NATIVE_DITHER,
};
CKFFProgramDesc CKSdlGpuNativeProgram(CKDWORD vertex, CKDWORD fragment,
                                      CKSdlGpuNativeProgramKind kind);

#endif
