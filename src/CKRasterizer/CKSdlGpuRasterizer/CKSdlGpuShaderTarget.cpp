#include "CKSdlGpuShaderTarget.h"

namespace {

static const SDL_GPUShaderFormat kKnownFormats =
    SDL_GPU_SHADERFORMAT_SPIRV |
    SDL_GPU_SHADERFORMAT_DXBC |
    SDL_GPU_SHADERFORMAT_DXIL |
    SDL_GPU_SHADERFORMAT_MSL |
    SDL_GPU_SHADERFORMAT_METALLIB;

}

CKBOOL CKSdlGpuMapShaderFormat(SDL_GPUShaderFormat NativeFormat,
                               CKSdlGpuShaderTarget *OutTarget)
{
    if (!OutTarget)
        return FALSE;

    *OutTarget = CKSdlGpuShaderTarget();
    OutTarget->NativeFormat = NativeFormat;
    OutTarget->EntryPoint = "main";

    switch (NativeFormat) {
    case SDL_GPU_SHADERFORMAT_SPIRV:
        OutTarget->Format = CKRST_SHADER_FORMAT_SPIRV;
        OutTarget->Profile = CKRST_SHADER_PROFILE_SPIRV;
        return TRUE;
    case SDL_GPU_SHADERFORMAT_DXBC:
        OutTarget->Format = CKRST_SHADER_FORMAT_DXBC;
        OutTarget->Profile = CKRST_SHADER_PROFILE_DX11;
        return TRUE;
    case SDL_GPU_SHADERFORMAT_DXIL:
        OutTarget->Format = CKRST_SHADER_FORMAT_DXIL;
        OutTarget->Profile = CKRST_SHADER_PROFILE_DX12;
        return TRUE;
    case SDL_GPU_SHADERFORMAT_MSL:
        OutTarget->Format = CKRST_SHADER_FORMAT_MSL;
        OutTarget->Profile = CKRST_SHADER_PROFILE_MSL;
        return TRUE;
    case SDL_GPU_SHADERFORMAT_METALLIB:
        OutTarget->Format = CKRST_SHADER_FORMAT_METALLIB;
        OutTarget->Profile = CKRST_SHADER_PROFILE_MSL;
        return TRUE;
    default:
        *OutTarget = CKSdlGpuShaderTarget();
        return FALSE;
    }
}

CKBOOL CKSdlGpuSelectShaderTarget(SDL_GPUShaderFormat SupportedFormats,
                                  SDL_GPUShaderFormat ArtifactFormats,
                                  CKSdlGpuShaderTarget *OutTarget)
{
    if (!OutTarget)
        return FALSE;

    *OutTarget = CKSdlGpuShaderTarget();
    const SDL_GPUShaderFormat compatible =
        SupportedFormats & ArtifactFormats & kKnownFormats;

#if defined(_WIN32)
    static const SDL_GPUShaderFormat preference[] = {
        SDL_GPU_SHADERFORMAT_DXIL,
        SDL_GPU_SHADERFORMAT_DXBC,
        SDL_GPU_SHADERFORMAT_SPIRV,
    };
#elif defined(__APPLE__)
    static const SDL_GPUShaderFormat preference[] = {
        SDL_GPU_SHADERFORMAT_METALLIB,
        SDL_GPU_SHADERFORMAT_MSL,
    };
#else
    static const SDL_GPUShaderFormat preference[] = {
        SDL_GPU_SHADERFORMAT_SPIRV,
    };
#endif

    for (SDL_GPUShaderFormat format : preference) {
        if ((compatible & format) != 0)
            return CKSdlGpuMapShaderFormat(format, OutTarget);
    }
    return FALSE;
}
