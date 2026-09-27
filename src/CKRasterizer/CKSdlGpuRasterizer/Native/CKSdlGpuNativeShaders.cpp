#include "CKSdlGpuNativeShaders.h"
#include "shaders/generated/dxil_vs_clear.h"
#include "shaders/generated/dxil_fs_clear.h"
#include "shaders/generated/spirv_vs_clear.h"
#include "shaders/generated/spirv_fs_clear.h"
#include "shaders/generated/dxil_fs_volume_mip.h"
#include "shaders/generated/spirv_fs_volume_mip.h"
#include "shaders/generated/dxil_fs_dither_resolve.h"
#include "shaders/generated/spirv_fs_dither_resolve.h"

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

CKBOOL CKSdlGpuNativeDitherShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment)
{
    if (!CKSdlGpuNativeClearShaders(format, vertex, fragment)) return FALSE;
    fragment.SamplerCount = 1;
    if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        fragment.Code = s_sdl_dxil_fs_dither_resolve;
        fragment.CodeSize = sizeof(s_sdl_dxil_fs_dither_resolve);
    } else {
        fragment.Code = s_sdl_spirv_fs_dither_resolve;
        fragment.CodeSize = sizeof(s_sdl_spirv_fs_dither_resolve);
    }
    return TRUE;
}

CKFFProgramDesc CKSdlGpuNativeProgram(CKDWORD vertex, CKDWORD fragment,
                                      CKSdlGpuNativeProgramKind kind)
{
    CKFFProgramDesc program;
    program.VertexShader = vertex; program.PixelShader = fragment;
    CKFFUniformBufferBinding buffer;
    buffer.Stage = CKRST_SHADER_VERTEX;
    buffer.Size = 16;
    program.UniformBuffers.PushBack(buffer);
    buffer.Stage = CKRST_SHADER_PIXEL;
    buffer.Size = kind == CKSDL_NATIVE_VOLUME ? 32u : 16u;
    program.UniformBuffers.PushBack(buffer);
    CKFFUniformBinding uniform;
    uniform.Name = "ckClear";
    program.Uniforms.PushBack(uniform);
    uniform.Slot = 1; uniform.Stage = CKRST_SHADER_PIXEL;
    uniform.Name = kind == CKSDL_NATIVE_VOLUME ? "ckVolumeParams" :
                   (kind == CKSDL_NATIVE_DITHER ? "ckDitherParams" : "ckClear");
    uniform.Count = kind == CKSDL_NATIVE_VOLUME ? 2 : 1;
    program.Uniforms.PushBack(uniform);
    if (kind != CKSDL_NATIVE_CLEAR) {
        CKFFSamplerBinding sampler;
        sampler.Dimension = kind == CKSDL_NATIVE_VOLUME ? CKFF_TEXTURE_3D : CKFF_TEXTURE_2D;
        sampler.Name = "ck_source";
        program.Samplers.PushBack(sampler);
    }
    return program;
}
