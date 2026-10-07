#include "CKSdlGpuNativeShaders.h"
#include "CKSdlGpuShaderPack.h"

CKBOOL CKSdlGpuNativeClearShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment)
{
    vertex = CKShaderDesc(); fragment = CKShaderDesc();
    vertex.Stage = CKRST_SHADER_VERTEX; fragment.Stage = CKRST_SHADER_PIXEL;
    vertex.UniformBufferCount = fragment.UniformBufferCount = 1;
    if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        vertex.Format = fragment.Format = CKRST_SHADER_FORMAT_DXIL;
        vertex.Profile = fragment.Profile = CKRST_SHADER_PROFILE_DX12;
    } else if (format == SDL_GPU_SHADERFORMAT_SPIRV) {
        vertex.Format = fragment.Format = CKRST_SHADER_FORMAT_SPIRV;
        vertex.Profile = fragment.Profile = CKRST_SHADER_PROFILE_SPIRV;
    } else if (format == SDL_GPU_SHADERFORMAT_MSL) {
        vertex.Format = fragment.Format = CKRST_SHADER_FORMAT_MSL;
        vertex.Profile = fragment.Profile = CKRST_SHADER_PROFILE_MSL;
    } else return FALSE;
    vertex.EntryPoint = fragment.EntryPoint = CKSdlGpuShaderEntryPoint(format);
    return CKSdlGpuShaderCode(format, CKSDL_SHADER_VS_CLEAR, vertex.Code, vertex.CodeSize) &&
           CKSdlGpuShaderCode(format, CKSDL_SHADER_FS_CLEAR, fragment.Code, fragment.CodeSize);
}

CKBOOL CKSdlGpuNativeVolumeShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment)
{
    if (!CKSdlGpuNativeClearShaders(format, vertex, fragment)) return FALSE;
    fragment.SamplerCount = 1;
    return CKSdlGpuShaderCode(format, CKSDL_SHADER_FS_VOLUME_MIP, fragment.Code, fragment.CodeSize);
}

CKBOOL CKSdlGpuNativeDitherShaders(SDL_GPUShaderFormat format, CKShaderDesc &vertex, CKShaderDesc &fragment)
{
    if (!CKSdlGpuNativeClearShaders(format, vertex, fragment)) return FALSE;
    fragment.SamplerCount = 1;
    return CKSdlGpuShaderCode(format, CKSDL_SHADER_FS_DITHER_RESOLVE, fragment.Code, fragment.CodeSize);
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
