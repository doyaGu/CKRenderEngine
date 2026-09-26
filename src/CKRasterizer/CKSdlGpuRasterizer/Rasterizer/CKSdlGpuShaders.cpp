#include "CKSdlGpuShaders.h"
#include "CKFFShaderABI.h"
#include "shaders/generated/abi.h"
#include "shaders/generated/dxil_vs_ff_3d.h"
#include "shaders/generated/dxil_vs_ff_3d_clip.h"
#include "shaders/generated/dxil_vs_ff_positiont.h"
#include "shaders/generated/dxil_vs_ff_positiont_clip.h"
#include "shaders/generated/dxil_vs_ff_positiont_depth_pad.h"
#include "shaders/generated/dxil_vs_ff_positiont_clip_depth_pad.h"
#include "shaders/generated/dxil_fs_ff_stage.h"
#include "shaders/generated/dxil_fs_ff_stage_compare1.h"
#include "shaders/generated/dxil_fs_ff_stage_compare2.h"
#include "shaders/generated/dxil_fs_ff_stage_compare3.h"
#include "shaders/generated/dxil_fs_ff_stage_compare4.h"
#include "shaders/generated/dxil_fs_ff_stage_compare5.h"
#include "shaders/generated/dxil_fs_ff_stage_compare6.h"
#include "shaders/generated/dxil_fs_ff_stage_compare7.h"
#include "shaders/generated/dxil_fs_ff_stage_compare8.h"
#include "shaders/generated/dxil_fs_ff_stage_cube.h"
#include "shaders/generated/dxil_fs_ff_stage_cube_compare1.h"
#include "shaders/generated/dxil_fs_ff_stage_cube_compare2.h"
#include "shaders/generated/dxil_fs_ff_stage_cube_compare3.h"
#include "shaders/generated/dxil_fs_ff_stage_cube_compare4.h"
#include "shaders/generated/dxil_fs_ff_stage_volume.h"
#include "shaders/generated/dxil_fs_ff_stage_volume_compare1.h"
#include "shaders/generated/dxil_fs_ff_stage_volume_compare2.h"
#include "shaders/generated/dxil_fs_ff_stage_volume_compare3.h"
#include "shaders/generated/dxil_fs_ff_stage_volume_compare4.h"
#include "shaders/generated/dxil_vs_postprocess.h"
#include "shaders/generated/dxil_fs_postprocess.h"
#include "shaders/generated/spirv_vs_ff_3d.h"
#include "shaders/generated/spirv_vs_ff_3d_clip.h"
#include "shaders/generated/spirv_vs_ff_positiont.h"
#include "shaders/generated/spirv_vs_ff_positiont_clip.h"
#include "shaders/generated/spirv_vs_ff_positiont_depth_pad.h"
#include "shaders/generated/spirv_vs_ff_positiont_clip_depth_pad.h"
#include "shaders/generated/spirv_fs_ff_stage.h"
#include "shaders/generated/spirv_fs_ff_stage_compare1.h"
#include "shaders/generated/spirv_fs_ff_stage_compare2.h"
#include "shaders/generated/spirv_fs_ff_stage_compare3.h"
#include "shaders/generated/spirv_fs_ff_stage_compare4.h"
#include "shaders/generated/spirv_fs_ff_stage_compare5.h"
#include "shaders/generated/spirv_fs_ff_stage_compare6.h"
#include "shaders/generated/spirv_fs_ff_stage_compare7.h"
#include "shaders/generated/spirv_fs_ff_stage_compare8.h"
#include "shaders/generated/spirv_fs_ff_stage_cube.h"
#include "shaders/generated/spirv_fs_ff_stage_cube_compare1.h"
#include "shaders/generated/spirv_fs_ff_stage_cube_compare2.h"
#include "shaders/generated/spirv_fs_ff_stage_cube_compare3.h"
#include "shaders/generated/spirv_fs_ff_stage_cube_compare4.h"
#include "shaders/generated/spirv_fs_ff_stage_volume.h"
#include "shaders/generated/spirv_fs_ff_stage_volume_compare1.h"
#include "shaders/generated/spirv_fs_ff_stage_volume_compare2.h"
#include "shaders/generated/spirv_fs_ff_stage_volume_compare3.h"
#include "shaders/generated/spirv_fs_ff_stage_volume_compare4.h"
#include "shaders/generated/spirv_vs_postprocess.h"
#include "shaders/generated/spirv_fs_postprocess.h"

CKBOOL CKSdlGpuFFDepthPadVertexShader(SDL_GPUShaderFormat format,
                                     CKBOOL clipping,
                                     CKShaderDesc &out)
{
    out = CKShaderDesc();
    out.Stage = CKRST_SHADER_VERTEX;
    out.Format = format == SDL_GPU_SHADERFORMAT_DXIL ?
        CKRST_SHADER_FORMAT_DXIL : CKRST_SHADER_FORMAT_SPIRV;
    out.Profile = format == SDL_GPU_SHADERFORMAT_DXIL ?
        CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
    out.UniformBufferCount = 1;
    if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        if (clipping) {
            out.Code = s_sdl_dxil_vs_ff_positiont_clip_depth_pad;
            out.CodeSize = sizeof(s_sdl_dxil_vs_ff_positiont_clip_depth_pad);
        } else {
            out.Code = s_sdl_dxil_vs_ff_positiont_depth_pad;
            out.CodeSize = sizeof(s_sdl_dxil_vs_ff_positiont_depth_pad);
        }
    } else if (format == SDL_GPU_SHADERFORMAT_SPIRV) {
        if (clipping) {
            out.Code = s_sdl_spirv_vs_ff_positiont_clip_depth_pad;
            out.CodeSize = sizeof(s_sdl_spirv_vs_ff_positiont_clip_depth_pad);
        } else {
            out.Code = s_sdl_spirv_vs_ff_positiont_depth_pad;
            out.CodeSize = sizeof(s_sdl_spirv_vs_ff_positiont_depth_pad);
        }
    } else {
        return FALSE;
    }
    return TRUE;
}

CKBOOL CKSdlGpuFFFragmentShader(SDL_GPUShaderFormat format,
                               CKFFSamplerLayout samplerLayout,
                               CKDWORD compareSamplerCount,
                               CKShaderDesc &out)
{
    const CKDWORD twoDCount = CKFFSamplerTypeSlotCount(CKFF_SAMPLER_2D,
                                                       samplerLayout);
    if (samplerLayout >= CKFF_SAMPLER_LAYOUT_COUNT ||
        compareSamplerCount > twoDCount)
        return FALSE;
    out = CKShaderDesc();
    out.Stage = CKRST_SHADER_PIXEL;
    out.Format = format == SDL_GPU_SHADERFORMAT_DXIL ?
        CKRST_SHADER_FORMAT_DXIL : CKRST_SHADER_FORMAT_SPIRV;
    out.Profile = format == SDL_GPU_SHADERFORMAT_DXIL ?
        CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
    out.SamplerCount = CKFF_SAMPLER_SLOT_COUNT;
    out.UniformBufferCount = 1;
#define CKFF_SET_SHADER(_format, _suffix) \
    out.Code = s_sdl_##_format##_fs_ff_stage##_suffix; \
    out.CodeSize = sizeof(s_sdl_##_format##_fs_ff_stage##_suffix)
#define CKFF_SELECT_COMPARE(_format, _layoutSuffix) \
    switch (compareSamplerCount) { \
    case 0: CKFF_SET_SHADER(_format, _layoutSuffix); break; \
    case 1: CKFF_SET_SHADER(_format, _layoutSuffix##_compare1); break; \
    case 2: CKFF_SET_SHADER(_format, _layoutSuffix##_compare2); break; \
    case 3: CKFF_SET_SHADER(_format, _layoutSuffix##_compare3); break; \
    case 4: CKFF_SET_SHADER(_format, _layoutSuffix##_compare4); break; \
    default: return FALSE; \
    }
#define CKFF_SELECT_WIDE_2D(_format) \
    switch (compareSamplerCount) { \
    case 0: out.Code = s_sdl_##_format##_fs_ff_stage; \
            out.CodeSize = sizeof(s_sdl_##_format##_fs_ff_stage); break; \
    case 1: CKFF_SET_SHADER(_format, _compare1); break; \
    case 2: CKFF_SET_SHADER(_format, _compare2); break; \
    case 3: CKFF_SET_SHADER(_format, _compare3); break; \
    case 4: CKFF_SET_SHADER(_format, _compare4); break; \
    case 5: CKFF_SET_SHADER(_format, _compare5); break; \
    case 6: CKFF_SET_SHADER(_format, _compare6); break; \
    case 7: CKFF_SET_SHADER(_format, _compare7); break; \
    case 8: CKFF_SET_SHADER(_format, _compare8); break; \
    }
    if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        if (samplerLayout == CKFF_SAMPLER_LAYOUT_WIDE_2D) {
            CKFF_SELECT_WIDE_2D(dxil);
        } else if (samplerLayout == CKFF_SAMPLER_LAYOUT_WIDE_CUBE) {
            CKFF_SELECT_COMPARE(dxil, _cube);
        } else {
            CKFF_SELECT_COMPARE(dxil, _volume);
        }
    } else if (format == SDL_GPU_SHADERFORMAT_SPIRV) {
        if (samplerLayout == CKFF_SAMPLER_LAYOUT_WIDE_2D) {
            CKFF_SELECT_WIDE_2D(spirv);
        } else if (samplerLayout == CKFF_SAMPLER_LAYOUT_WIDE_CUBE) {
            CKFF_SELECT_COMPARE(spirv, _cube);
        } else {
            CKFF_SELECT_COMPARE(spirv, _volume);
        }
    } else {
        return FALSE;
    }
#undef CKFF_SELECT_WIDE_2D
#undef CKFF_SELECT_COMPARE
#undef CKFF_SET_SHADER
    return out.Code && out.CodeSize;
}

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
    CKShaderDesc fragment;
    if (!CKSdlGpuFFFragmentShader(format, CKFF_SAMPLER_LAYOUT_WIDE_2D,
                                  0, fragment))
        return FALSE;
    out.Shaders[CKRST_SHADER_FF_FRAGMENT] = fragment;
    return out.Matches(payload, profile);
}
