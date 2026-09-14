#include "CKSdlGpuShaders.h"
#include "shaders/generated/abi.h"
#include "shaders/generated/dxil_vs_ff_3d.h"
#include "shaders/generated/dxil_vs_ff_3d_clip.h"
#include "shaders/generated/dxil_vs_ff_positiont.h"
#include "shaders/generated/dxil_vs_ff_positiont_clip.h"
#include "shaders/generated/dxil_fs_ff_stage.h"
#include "shaders/generated/dxil_vs_postprocess.h"
#include "shaders/generated/dxil_fs_postprocess.h"
#include "shaders/generated/spirv_vs_ff_3d.h"
#include "shaders/generated/spirv_vs_ff_3d_clip.h"
#include "shaders/generated/spirv_vs_ff_positiont.h"
#include "shaders/generated/spirv_vs_ff_positiont_clip.h"
#include "shaders/generated/spirv_fs_ff_stage.h"
#include "shaders/generated/spirv_vs_postprocess.h"
#include "shaders/generated/spirv_fs_postprocess.h"

CKBOOL CKSdlGpuShaderSet(SDL_GPUShaderFormat format, CKFFShaderSet &out)
{
    out = CKFFShaderSet();
    out.ABIVersion = CKSDL_SHADER_ABI_VERSION;
    out.InterfaceHash = CKSDL_SHADER_INTERFACE_HASH;
    switch (format) {
    case SDL_GPU_SHADERFORMAT_DXIL:
        out.Shaders[0].Code = s_sdl_dxil_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_sdl_dxil_vs_ff_3d);
        out.Shaders[1].Code = s_sdl_dxil_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_sdl_dxil_vs_ff_3d_clip);
        out.Shaders[2].Code = s_sdl_dxil_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_sdl_dxil_vs_ff_positiont);
        out.Shaders[3].Code = s_sdl_dxil_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_sdl_dxil_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_sdl_dxil_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_sdl_dxil_fs_ff_stage);
        out.Shaders[5].Code = s_sdl_dxil_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_sdl_dxil_vs_postprocess);
        out.Shaders[6].Code = s_sdl_dxil_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_sdl_dxil_fs_postprocess);
        break;
    case SDL_GPU_SHADERFORMAT_SPIRV:
        out.Shaders[0].Code = s_sdl_spirv_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_sdl_spirv_vs_ff_3d);
        out.Shaders[1].Code = s_sdl_spirv_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_sdl_spirv_vs_ff_3d_clip);
        out.Shaders[2].Code = s_sdl_spirv_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_sdl_spirv_vs_ff_positiont);
        out.Shaders[3].Code = s_sdl_spirv_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_sdl_spirv_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_sdl_spirv_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_sdl_spirv_fs_ff_stage);
        out.Shaders[5].Code = s_sdl_spirv_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_sdl_spirv_vs_postprocess);
        out.Shaders[6].Code = s_sdl_spirv_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_sdl_spirv_fs_postprocess);
        break;
    default: return FALSE;
    }
    const CK_SHADER_FORMAT payload = format == SDL_GPU_SHADERFORMAT_DXIL ? CKRST_SHADER_FORMAT_DXIL : CKRST_SHADER_FORMAT_SPIRV;
    const CK_SHADER_PROFILE profile = format == SDL_GPU_SHADERFORMAT_DXIL ? CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
    for (unsigned i = 0; i < CKRST_BUILTIN_SHADER_COUNT; ++i) {
        auto &shader = out.Shaders[i];
        shader.Stage = i == CKRST_SHADER_FF_FRAGMENT || i == CKRST_SHADER_PRESENT_FRAGMENT ? CKRST_SHADER_PIXEL : CKRST_SHADER_VERTEX;
        shader.Format = payload;
        shader.Profile = profile;
        if (i == CKRST_SHADER_FF_3D || i == CKRST_SHADER_FF_3D_CLIP)
            shader.UniformBufferCount = 2;
        else
            shader.UniformBufferCount = i == CKRST_SHADER_PRESENT_VERTEX ? 0 : 1;
        shader.SamplerCount = i == CKRST_SHADER_FF_FRAGMENT ? 16 : (i == CKRST_SHADER_PRESENT_FRAGMENT ? 1 : 0);
    }
    return out.Matches(payload, profile);
}
