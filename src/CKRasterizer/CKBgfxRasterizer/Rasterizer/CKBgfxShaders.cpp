#include "CKBgfxRasterizer.h"
#include "CKRasterizerContextData.h"
#include "shaders/generated/CKFFShaderABI.generated.h"
#include "shaders/generated/dx11/vs_ff_3d.bin.h"
#include "shaders/generated/dx11/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx11/fs_ff_stage.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_cube.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_volume.bin.h"
#include "shaders/generated/dx11/vs_postprocess.bin.h"
#include "shaders/generated/dx11/fs_postprocess.bin.h"
#include "shaders/generated/dx11/fs_dither_resolve.bin.h"
#include "shaders/generated/dx12/vs_ff_3d.bin.h"
#include "shaders/generated/dx12/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx12/fs_ff_stage.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_cube.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_volume.bin.h"
#include "shaders/generated/dx12/vs_postprocess.bin.h"
#include "shaders/generated/dx12/fs_postprocess.bin.h"
#include "shaders/generated/dx12/fs_dither_resolve.bin.h"
#include "shaders/generated/spirv/vs_ff_3d.bin.h"
#include "shaders/generated/spirv/vs_ff_3d_clip.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/spirv/fs_ff_stage.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_cube.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_volume.bin.h"
#include "shaders/generated/spirv/vs_postprocess.bin.h"
#include "shaders/generated/spirv/fs_postprocess.bin.h"
#include "shaders/generated/spirv/fs_dither_resolve.bin.h"
#include "shaders/generated/glsl/vs_ff_3d.bin.h"
#include "shaders/generated/glsl/vs_ff_3d_clip.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/glsl/fs_ff_stage.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_cube.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_volume.bin.h"
#include "shaders/generated/glsl/vs_postprocess.bin.h"
#include "shaders/generated/glsl/fs_postprocess.bin.h"
#include "shaders/generated/glsl/fs_dither_resolve.bin.h"
#include "shaders/generated/essl/vs_ff_3d.bin.h"
#include "shaders/generated/essl/vs_ff_3d_clip.bin.h"
#include "shaders/generated/essl/vs_ff_positiont.bin.h"
#include "shaders/generated/essl/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/essl/fs_ff_stage.bin.h"
#include "shaders/generated/essl/fs_ff_stage_cube.bin.h"
#include "shaders/generated/essl/fs_ff_stage_volume.bin.h"
#include "shaders/generated/essl/vs_postprocess.bin.h"
#include "shaders/generated/essl/fs_postprocess.bin.h"
#include "shaders/generated/essl/fs_dither_resolve.bin.h"
#include "shaders/generated/metal/vs_ff_3d.bin.h"
#include "shaders/generated/metal/vs_ff_3d_clip.bin.h"
#include "shaders/generated/metal/vs_ff_positiont.bin.h"
#include "shaders/generated/metal/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/metal/fs_ff_stage.bin.h"
#include "shaders/generated/metal/fs_ff_stage_cube.bin.h"
#include "shaders/generated/metal/fs_ff_stage_volume.bin.h"
#include "shaders/generated/metal/vs_postprocess.bin.h"
#include "shaders/generated/metal/fs_postprocess.bin.h"
#include "shaders/generated/metal/fs_dither_resolve.bin.h"

CKBOOL CKBgfxRasterizerFFFragmentShader(const CKRasterizerDeviceCaps &caps,
                                        CKFFSamplerLayout layout,
                                        CKShaderDesc &out)
{
    if (caps.ShaderFormat != CKRST_SHADER_FORMAT_BGFX ||
        layout >= CKFF_SAMPLER_LAYOUT_COUNT)
        return FALSE;
    out = CKShaderDesc();
    out.Stage = CKRST_SHADER_PIXEL;
    out.Format = caps.ShaderFormat;
    out.Profile = caps.ShaderProfile;
#define CKFF_SELECT_LAYOUT(_backend) \
    if (layout == CKFF_SAMPLER_LAYOUT_WIDE_2D) { \
        out.Code = s_##_backend##_fs_ff_stage; \
        out.CodeSize = sizeof(s_##_backend##_fs_ff_stage); \
    } else if (layout == CKFF_SAMPLER_LAYOUT_WIDE_CUBE) { \
        out.Code = s_##_backend##_fs_ff_stage_cube; \
        out.CodeSize = sizeof(s_##_backend##_fs_ff_stage_cube); \
    } else { \
        out.Code = s_##_backend##_fs_ff_stage_volume; \
        out.CodeSize = sizeof(s_##_backend##_fs_ff_stage_volume); \
    }
    switch (caps.ShaderProfile) {
    case CKRST_SHADER_PROFILE_DX11: CKFF_SELECT_LAYOUT(dx11); break;
    case CKRST_SHADER_PROFILE_DX12: CKFF_SELECT_LAYOUT(dx12); break;
    case CKRST_SHADER_PROFILE_SPIRV: CKFF_SELECT_LAYOUT(spirv); break;
    case CKRST_SHADER_PROFILE_GLSL: CKFF_SELECT_LAYOUT(glsl); break;
    case CKRST_SHADER_PROFILE_ESSL: CKFF_SELECT_LAYOUT(essl); break;
    case CKRST_SHADER_PROFILE_MSL: CKFF_SELECT_LAYOUT(metal); break;
    default: return FALSE;
    }
#undef CKFF_SELECT_LAYOUT
    return out.Code && out.CodeSize ? TRUE : FALSE;
}

CKBOOL CKBgfxRasterizerDitherFragmentShader(
    const CKRasterizerDeviceCaps &caps, CKShaderDesc &out)
{
    if (caps.ShaderFormat != CKRST_SHADER_FORMAT_BGFX)
        return FALSE;
    out = CKShaderDesc();
    out.Stage = CKRST_SHADER_PIXEL;
    out.Format = caps.ShaderFormat;
    out.Profile = caps.ShaderProfile;
#define CKFF_SELECT_DITHER(_backend) \
    out.Code = s_##_backend##_fs_dither_resolve; \
    out.CodeSize = sizeof(s_##_backend##_fs_dither_resolve)
    switch (caps.ShaderProfile) {
    case CKRST_SHADER_PROFILE_DX11: CKFF_SELECT_DITHER(dx11); break;
    case CKRST_SHADER_PROFILE_DX12: CKFF_SELECT_DITHER(dx12); break;
    case CKRST_SHADER_PROFILE_SPIRV: CKFF_SELECT_DITHER(spirv); break;
    case CKRST_SHADER_PROFILE_GLSL: CKFF_SELECT_DITHER(glsl); break;
    case CKRST_SHADER_PROFILE_ESSL: CKFF_SELECT_DITHER(essl); break;
    case CKRST_SHADER_PROFILE_MSL: CKFF_SELECT_DITHER(metal); break;
    default: return FALSE;
    }
#undef CKFF_SELECT_DITHER
    return out.Code && out.CodeSize ? TRUE : FALSE;
}

CKBOOL CKBgfxRasterizerShaderSet(const CKRasterizerDeviceCaps &caps, CKFFShaderSet &out)
{
    out = CKFFShaderSet();
    if (caps.ShaderFormat != CKRST_SHADER_FORMAT_BGFX) return FALSE;
    out.ABIVersion = g_CKFFGeneratedShaderABIVersion;
    out.InterfaceHash = g_CKFFGeneratedShaderInterfaceHash;
    switch (caps.ShaderProfile) {
    case CKRST_SHADER_PROFILE_DX11:
        out.Shaders[0].Code = s_dx11_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_dx11_vs_ff_3d);
        out.Shaders[1].Code = s_dx11_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_dx11_vs_ff_3d_clip);
        out.Shaders[2].Code = s_dx11_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_dx11_vs_ff_positiont);
        out.Shaders[3].Code = s_dx11_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_dx11_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_dx11_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_dx11_fs_ff_stage);
        out.Shaders[5].Code = s_dx11_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_dx11_vs_postprocess);
        out.Shaders[6].Code = s_dx11_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_dx11_fs_postprocess);
        break;
    case CKRST_SHADER_PROFILE_DX12:
        out.Shaders[0].Code = s_dx12_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_dx12_vs_ff_3d);
        out.Shaders[1].Code = s_dx12_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_dx12_vs_ff_3d_clip);
        out.Shaders[2].Code = s_dx12_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_dx12_vs_ff_positiont);
        out.Shaders[3].Code = s_dx12_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_dx12_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_dx12_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_dx12_fs_ff_stage);
        out.Shaders[5].Code = s_dx12_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_dx12_vs_postprocess);
        out.Shaders[6].Code = s_dx12_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_dx12_fs_postprocess);
        break;
    case CKRST_SHADER_PROFILE_SPIRV:
        out.Shaders[0].Code = s_spirv_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_spirv_vs_ff_3d);
        out.Shaders[1].Code = s_spirv_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_spirv_vs_ff_3d_clip);
        out.Shaders[2].Code = s_spirv_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_spirv_vs_ff_positiont);
        out.Shaders[3].Code = s_spirv_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_spirv_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_spirv_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_spirv_fs_ff_stage);
        out.Shaders[5].Code = s_spirv_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_spirv_vs_postprocess);
        out.Shaders[6].Code = s_spirv_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_spirv_fs_postprocess);
        break;
    case CKRST_SHADER_PROFILE_GLSL:
        out.Shaders[0].Code = s_glsl_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_glsl_vs_ff_3d);
        out.Shaders[1].Code = s_glsl_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_glsl_vs_ff_3d_clip);
        out.Shaders[2].Code = s_glsl_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_glsl_vs_ff_positiont);
        out.Shaders[3].Code = s_glsl_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_glsl_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_glsl_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_glsl_fs_ff_stage);
        out.Shaders[5].Code = s_glsl_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_glsl_vs_postprocess);
        out.Shaders[6].Code = s_glsl_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_glsl_fs_postprocess);
        break;
    case CKRST_SHADER_PROFILE_ESSL:
        out.Shaders[0].Code = s_essl_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_essl_vs_ff_3d);
        out.Shaders[1].Code = s_essl_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_essl_vs_ff_3d_clip);
        out.Shaders[2].Code = s_essl_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_essl_vs_ff_positiont);
        out.Shaders[3].Code = s_essl_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_essl_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_essl_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_essl_fs_ff_stage);
        out.Shaders[5].Code = s_essl_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_essl_vs_postprocess);
        out.Shaders[6].Code = s_essl_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_essl_fs_postprocess);
        break;
    case CKRST_SHADER_PROFILE_MSL:
        out.Shaders[0].Code = s_metal_vs_ff_3d; out.Shaders[0].CodeSize = sizeof(s_metal_vs_ff_3d);
        out.Shaders[1].Code = s_metal_vs_ff_3d_clip; out.Shaders[1].CodeSize = sizeof(s_metal_vs_ff_3d_clip);
        out.Shaders[2].Code = s_metal_vs_ff_positiont; out.Shaders[2].CodeSize = sizeof(s_metal_vs_ff_positiont);
        out.Shaders[3].Code = s_metal_vs_ff_positiont_clip; out.Shaders[3].CodeSize = sizeof(s_metal_vs_ff_positiont_clip);
        out.Shaders[4].Code = s_metal_fs_ff_stage; out.Shaders[4].CodeSize = sizeof(s_metal_fs_ff_stage);
        out.Shaders[5].Code = s_metal_vs_postprocess; out.Shaders[5].CodeSize = sizeof(s_metal_vs_postprocess);
        out.Shaders[6].Code = s_metal_fs_postprocess; out.Shaders[6].CodeSize = sizeof(s_metal_fs_postprocess);
        break;
    default: return FALSE;
    }
    for (unsigned i = 0; i < CKRST_BUILTIN_SHADER_COUNT; ++i) {
        out.Shaders[i].Format = caps.ShaderFormat;
        out.Shaders[i].Profile = caps.ShaderProfile;
        out.Shaders[i].Stage = i == CKRST_SHADER_FF_FRAGMENT || i == CKRST_SHADER_PRESENT_FRAGMENT ? CKRST_SHADER_PIXEL : CKRST_SHADER_VERTEX;
    }
    return out.Matches(caps.ShaderFormat, caps.ShaderProfile) ? TRUE : FALSE;
}
