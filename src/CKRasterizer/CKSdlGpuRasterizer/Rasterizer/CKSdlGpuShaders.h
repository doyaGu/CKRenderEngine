#ifndef CKSDLGPU_SHADERS_H
#define CKSDLGPU_SHADERS_H

#include "CKBuiltinShaders.h"
#include <SDL3/SDL_gpu.h>

// FFP/presentation artifacts supplied by the rasterizer plugin. Private
// backend image operations have their own CKSdlGpuNativeShaders interface.
CKBOOL CKSdlGpuShaderSet(SDL_GPUShaderFormat Format, CKFFShaderSet &Out);

#endif
