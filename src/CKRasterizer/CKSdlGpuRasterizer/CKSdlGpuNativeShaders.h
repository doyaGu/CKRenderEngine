#ifndef CKSDLGPU_NATIVE_SHADERS_H
#define CKSDLGPU_NATIVE_SHADERS_H

#include "CKBackendProgram.h"
#include <SDL3/SDL_gpu.h>

// Private image operations of this backend, independent of rasterizer shaders.
CKBOOL CKSdlGpuNativeClearShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment);
CKBOOL CKSdlGpuNativeVolumeShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment);
CKBackendProgramDesc CKSdlGpuNativeProgram(CKDWORD vertex, CKDWORD fragment, bool volume);

#endif
