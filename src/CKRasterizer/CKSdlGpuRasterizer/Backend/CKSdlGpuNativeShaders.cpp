#include "CKSdlGpuNativeShaders.h"
#include "shaders/generated/dxil_vs_clear.h"
#include "shaders/generated/dxil_fs_clear.h"
#include "shaders/generated/spirv_vs_clear.h"
#include "shaders/generated/spirv_fs_clear.h"
#include "shaders/generated/dxil_fs_volume_mip.h"
#include "shaders/generated/spirv_fs_volume_mip.h"

CKBOOL CKSdlGpuNativeClearShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment)
{
    vertex = CKShaderDesc(); fragment = CKShaderDesc();
    vertex.Stage = CKRST_SHADER_VERTEX; fragment.Stage = CKRST_SHADER_PIXEL;
    vertex.UniformBufferCount = fragment.UniformBufferCount = 1;
    if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        vertex.Format = fragment.Format = CKRST_SHADER_FORMAT_DXIL;
        vertex.Profile = fragment.Profile = CKRST_SHADER_PROFILE_DX12;
        vertex.Code = s_sdl_dxil_vs_clear; vertex.CodeSize = sizeof(s_sdl_dxil_vs_clear);
        fragment.Code = s_sdl_dxil_fs_clear; fragment.CodeSize = sizeof(s_sdl_dxil_fs_clear);
    } else if (format == SDL_GPU_SHADERFORMAT_SPIRV) {
        vertex.Format = fragment.Format = CKRST_SHADER_FORMAT_SPIRV;
        vertex.Profile = fragment.Profile = CKRST_SHADER_PROFILE_SPIRV;
        vertex.Code = s_sdl_spirv_vs_clear; vertex.CodeSize = sizeof(s_sdl_spirv_vs_clear);
        fragment.Code = s_sdl_spirv_fs_clear; fragment.CodeSize = sizeof(s_sdl_spirv_fs_clear);
    } else return FALSE;
    return TRUE;
}

CKBOOL CKSdlGpuNativeVolumeShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment)
{
    if (!CKSdlGpuNativeClearShaders(format, vertex, fragment)) return FALSE;
    fragment.SamplerCount = 1;
    if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        fragment.Code = s_sdl_dxil_fs_volume_mip; fragment.CodeSize = sizeof(s_sdl_dxil_fs_volume_mip);
    } else {
        fragment.Code = s_sdl_spirv_fs_volume_mip; fragment.CodeSize = sizeof(s_sdl_spirv_fs_volume_mip);
    }
    return TRUE;
}

CKBackendProgramDesc CKSdlGpuNativeProgram(CKDWORD vertex, CKDWORD fragment, bool volume)
{
    CKBackendProgramDesc program;
    program.VertexShader = vertex; program.PixelShader = fragment;
    program.UniformBuffers = {{CKRST_SHADER_VERTEX, 0, 16}, {CKRST_SHADER_PIXEL, 0, volume ? 32u : 16u}};
    CKBackendUniformBinding uniform;
    uniform.Name = "ckClear";
    program.Uniforms.push_back(uniform);
    uniform.Slot = 1; uniform.Stage = CKRST_SHADER_PIXEL;
    uniform.Name = volume ? "ckVolumeParams" : "ckClear";
    uniform.Count = volume ? 2 : 1;
    program.Uniforms.push_back(uniform);
    if (volume) {
        CKBackendSamplerBinding sampler;
        sampler.Dimension = CKBACKEND_TEXTURE_3D;
        sampler.Name = "ck_source";
        program.Samplers.push_back(sampler);
    }
    return program;
}
