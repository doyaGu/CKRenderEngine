#ifndef CKSDLGPU_SHADER_TARGET_H
#define CKSDLGPU_SHADER_TARGET_H

#include "CKRasterizerBackendEnums.h"

#include <SDL3/SDL_gpu.h>

struct CKSdlGpuShaderTarget {
    SDL_GPUShaderFormat NativeFormat;
    CK_SHADER_FORMAT Format;
    CK_SHADER_PROFILE Profile;
    const char *EntryPoint;

    CKSdlGpuShaderTarget()
        : NativeFormat(SDL_GPU_SHADERFORMAT_INVALID),
          Format(CKRST_SHADER_FORMAT_UNKNOWN),
          Profile(CKRST_SHADER_PROFILE_UNKNOWN),
          EntryPoint(NULL) {}
};

// Selects one exact shader payload shared by the SDL_gpu device and the
// checked-in shader artifact catalog. SupportedFormats normally comes from
// SDL_GetGPUShaderFormats(); ArtifactFormats describes what the build ships.
CKBOOL CKSdlGpuSelectShaderTarget(SDL_GPUShaderFormat SupportedFormats,
                                  SDL_GPUShaderFormat ArtifactFormats,
                                  CKSdlGpuShaderTarget *OutTarget);

CKBOOL CKSdlGpuMapShaderFormat(SDL_GPUShaderFormat NativeFormat,
                               CKSdlGpuShaderTarget *OutTarget);

#endif // CKSDLGPU_SHADER_TARGET_H
