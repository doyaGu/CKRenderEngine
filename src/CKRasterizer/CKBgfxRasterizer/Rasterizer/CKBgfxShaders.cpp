#include "CKBgfxRasterizer.h"
#include "CKRasterizerContextData.h"
#include "shaders/generated/CKFFShaderABI.generated.h"

// The profiles of the build's bgfx renderers. CMake defines CKBGFX_SHADER_<X>
// for each compile_shaders.py backend.
#if !CKBGFX_SHADER_DX11 && !CKBGFX_SHADER_DX12 && !CKBGFX_SHADER_SPIRV && \
    !CKBGFX_SHADER_GLSL && !CKBGFX_SHADER_ESSL && !CKBGFX_SHADER_METAL
#error "The build embeds the shaders of no bgfx renderer"
#endif

#if CKBGFX_SHADER_DX11
#include "shaders/generated/dx11/vs_ff_3d.bin.h"
#include "shaders/generated/dx11/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont.bin.h"
#include "shaders/generated/dx11/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx11/fs_ff_stage.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_cube.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_volume.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_native.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_cube_native.bin.h"
#include "shaders/generated/dx11/fs_ff_stage_volume_native.bin.h"
#include "shaders/generated/dx11/vs_postprocess.bin.h"
#include "shaders/generated/dx11/fs_postprocess.bin.h"
#include "shaders/generated/dx11/fs_dither_resolve.bin.h"
#endif

#if CKBGFX_SHADER_DX12
#include "shaders/generated/dx12/vs_ff_3d.bin.h"
#include "shaders/generated/dx12/vs_ff_3d_clip.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont.bin.h"
#include "shaders/generated/dx12/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/dx12/fs_ff_stage.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_cube.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_volume.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_native.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_cube_native.bin.h"
#include "shaders/generated/dx12/fs_ff_stage_volume_native.bin.h"
#include "shaders/generated/dx12/vs_postprocess.bin.h"
#include "shaders/generated/dx12/fs_postprocess.bin.h"
#include "shaders/generated/dx12/fs_dither_resolve.bin.h"
#endif

#if CKBGFX_SHADER_SPIRV
#include "shaders/generated/spirv/vs_ff_3d.bin.h"
#include "shaders/generated/spirv/vs_ff_3d_clip.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont.bin.h"
#include "shaders/generated/spirv/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/spirv/fs_ff_stage.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_cube.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_volume.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_native.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_cube_native.bin.h"
#include "shaders/generated/spirv/fs_ff_stage_volume_native.bin.h"
#include "shaders/generated/spirv/vs_postprocess.bin.h"
#include "shaders/generated/spirv/fs_postprocess.bin.h"
#include "shaders/generated/spirv/fs_dither_resolve.bin.h"
#endif

#if CKBGFX_SHADER_GLSL
#include "shaders/generated/glsl/vs_ff_3d.bin.h"
#include "shaders/generated/glsl/vs_ff_3d_clip.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont.bin.h"
#include "shaders/generated/glsl/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/glsl/fs_ff_stage.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_cube.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_volume.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_native.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_cube_native.bin.h"
#include "shaders/generated/glsl/fs_ff_stage_volume_native.bin.h"
#include "shaders/generated/glsl/vs_postprocess.bin.h"
#include "shaders/generated/glsl/fs_postprocess.bin.h"
#include "shaders/generated/glsl/fs_dither_resolve.bin.h"
#endif

#if CKBGFX_SHADER_ESSL
#include "shaders/generated/essl/vs_ff_3d.bin.h"
#include "shaders/generated/essl/vs_ff_3d_clip.bin.h"
#include "shaders/generated/essl/vs_ff_positiont.bin.h"
#include "shaders/generated/essl/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/essl/fs_ff_stage.bin.h"
#include "shaders/generated/essl/fs_ff_stage_cube.bin.h"
#include "shaders/generated/essl/fs_ff_stage_volume.bin.h"
#include "shaders/generated/essl/fs_ff_stage_native.bin.h"
#include "shaders/generated/essl/fs_ff_stage_cube_native.bin.h"
#include "shaders/generated/essl/fs_ff_stage_volume_native.bin.h"
#include "shaders/generated/essl/vs_postprocess.bin.h"
#include "shaders/generated/essl/fs_postprocess.bin.h"
#include "shaders/generated/essl/fs_dither_resolve.bin.h"
#endif

#if CKBGFX_SHADER_METAL
#include "shaders/generated/metal/vs_ff_3d.bin.h"
#include "shaders/generated/metal/vs_ff_3d_clip.bin.h"
#include "shaders/generated/metal/vs_ff_positiont.bin.h"
#include "shaders/generated/metal/vs_ff_positiont_clip.bin.h"
#include "shaders/generated/metal/fs_ff_stage.bin.h"
#include "shaders/generated/metal/fs_ff_stage_cube.bin.h"
#include "shaders/generated/metal/fs_ff_stage_volume.bin.h"
#include "shaders/generated/metal/fs_ff_stage_native.bin.h"
#include "shaders/generated/metal/fs_ff_stage_cube_native.bin.h"
#include "shaders/generated/metal/fs_ff_stage_volume_native.bin.h"
#include "shaders/generated/metal/vs_postprocess.bin.h"
#include "shaders/generated/metal/fs_postprocess.bin.h"
#include "shaders/generated/metal/fs_dither_resolve.bin.h"
#endif

namespace {

// The shaders compile_shaders.py compiles for each profile.
enum CKBgfxShader {
    VS_FF_3D,
    VS_FF_3D_CLIP,
    VS_FF_POSITIONT,
    VS_FF_POSITIONT_CLIP,
    FS_FF_STAGE,
    FS_FF_STAGE_CUBE,
    FS_FF_STAGE_VOLUME,
    FS_FF_STAGE_NATIVE,
    FS_FF_STAGE_CUBE_NATIVE,
    FS_FF_STAGE_VOLUME_NATIVE,
    VS_POSTPROCESS,
    FS_POSTPROCESS,
    FS_DITHER_RESOLVE,
    SHADER_COUNT
};

struct ProfileShaders {
    CK_SHADER_PROFILE Profile;
    struct {
        const CKBYTE *Code;
        CKDWORD Size;
    } Shaders[SHADER_COUNT];
};

#define CKBGFX_SHADER(_backend, _name) {s_##_backend##_##_name, (CKDWORD)sizeof(s_##_backend##_##_name)}
#define CKBGFX_PROFILE(_profile, _backend) {_profile, { \
    CKBGFX_SHADER(_backend, vs_ff_3d), CKBGFX_SHADER(_backend, vs_ff_3d_clip), \
    CKBGFX_SHADER(_backend, vs_ff_positiont), CKBGFX_SHADER(_backend, vs_ff_positiont_clip), \
    CKBGFX_SHADER(_backend, fs_ff_stage), CKBGFX_SHADER(_backend, fs_ff_stage_cube), \
    CKBGFX_SHADER(_backend, fs_ff_stage_volume), CKBGFX_SHADER(_backend, fs_ff_stage_native), \
    CKBGFX_SHADER(_backend, fs_ff_stage_cube_native), CKBGFX_SHADER(_backend, fs_ff_stage_volume_native), \
    CKBGFX_SHADER(_backend, vs_postprocess), CKBGFX_SHADER(_backend, fs_postprocess), \
    CKBGFX_SHADER(_backend, fs_dither_resolve)}}

// In the order the driver advertises them.
const ProfileShaders s_Profiles[] = {
#if CKBGFX_SHADER_DX11
    CKBGFX_PROFILE(CKRST_SHADER_PROFILE_DX11, dx11),
#endif
#if CKBGFX_SHADER_DX12
    CKBGFX_PROFILE(CKRST_SHADER_PROFILE_DX12, dx12),
#endif
#if CKBGFX_SHADER_SPIRV
    CKBGFX_PROFILE(CKRST_SHADER_PROFILE_SPIRV, spirv),
#endif
#if CKBGFX_SHADER_GLSL
    CKBGFX_PROFILE(CKRST_SHADER_PROFILE_GLSL, glsl),
#endif
#if CKBGFX_SHADER_ESSL
    CKBGFX_PROFILE(CKRST_SHADER_PROFILE_ESSL, essl),
#endif
#if CKBGFX_SHADER_METAL
    CKBGFX_PROFILE(CKRST_SHADER_PROFILE_MSL, metal),
#endif
};

#undef CKBGFX_PROFILE
#undef CKBGFX_SHADER

const CKBgfxShader s_BuiltinShaders[CKRST_BUILTIN_SHADER_COUNT] = {
    VS_FF_3D,             // CKRST_SHADER_FF_3D
    VS_FF_3D_CLIP,        // CKRST_SHADER_FF_3D_CLIP
    VS_FF_POSITIONT,      // CKRST_SHADER_FF_POSITIONT
    VS_FF_POSITIONT_CLIP, // CKRST_SHADER_FF_POSITIONT_CLIP
    FS_FF_STAGE,          // CKRST_SHADER_FF_FRAGMENT
    VS_POSTPROCESS,       // CKRST_SHADER_PRESENT_VERTEX
    FS_POSTPROCESS,       // CKRST_SHADER_PRESENT_FRAGMENT
};

const ProfileShaders *FindProfile(const CKRasterizerDeviceCaps &caps)
{
    if (caps.ShaderFormat != CKRST_SHADER_FORMAT_BGFX)
        return NULL;
    for (const ProfileShaders &profile : s_Profiles) {
        if (profile.Profile == caps.ShaderProfile)
            return &profile;
    }
    return NULL;
}

void SetShader(const ProfileShaders &profile, CKBgfxShader shader, CK_SHADER_STAGE stage, CKShaderDesc &out)
{
    out = CKShaderDesc();
    out.Stage = stage;
    out.Format = CKRST_SHADER_FORMAT_BGFX;
    out.Profile = profile.Profile;
    out.Code = profile.Shaders[shader].Code;
    out.CodeSize = profile.Shaders[shader].Size;
}

} // namespace

CKDWORD CKBgfxRasterizerShaderProfileCount()
{
    return (CKDWORD)(sizeof(s_Profiles) / sizeof(s_Profiles[0]));
}

CK_SHADER_PROFILE CKBgfxRasterizerShaderProfile(CKDWORD index)
{
    return index < CKBgfxRasterizerShaderProfileCount() ? s_Profiles[index].Profile : CKRST_SHADER_PROFILE_UNKNOWN;
}

CKBOOL CKBgfxRasterizerFFFragmentShader(const CKRasterizerDeviceCaps &caps,
                                        CKFFSamplerLayout layout,
                                        CKBOOL requiresShaderSampling,
                                        CKShaderDesc &out)
{
    const ProfileShaders *profile = FindProfile(caps);
    if (!profile || (CKDWORD)layout >= CKFF_SAMPLER_LAYOUT_COUNT ||
        (requiresShaderSampling != FALSE &&
         requiresShaderSampling != TRUE))
        return FALSE;
    CKBgfxShader shader;
    if (layout == CKFF_SAMPLER_LAYOUT_WIDE_2D)
        shader = requiresShaderSampling ? FS_FF_STAGE : FS_FF_STAGE_NATIVE;
    else if (layout == CKFF_SAMPLER_LAYOUT_WIDE_CUBE)
        shader = requiresShaderSampling ? FS_FF_STAGE_CUBE : FS_FF_STAGE_CUBE_NATIVE;
    else
        shader = requiresShaderSampling ? FS_FF_STAGE_VOLUME : FS_FF_STAGE_VOLUME_NATIVE;
    SetShader(*profile, shader, CKRST_SHADER_PIXEL, out);
    return TRUE;
}

CKBOOL CKBgfxRasterizerDitherFragmentShader(
    const CKRasterizerDeviceCaps &caps, CKShaderDesc &out)
{
    const ProfileShaders *profile = FindProfile(caps);
    if (!profile)
        return FALSE;
    SetShader(*profile, FS_DITHER_RESOLVE, CKRST_SHADER_PIXEL, out);
    return TRUE;
}

CKBOOL CKBgfxRasterizerShaderSet(const CKRasterizerDeviceCaps &caps, CKFFShaderSet &out)
{
    out = CKFFShaderSet();
    const ProfileShaders *profile = FindProfile(caps);
    if (!profile)
        return FALSE;
    out.ABIVersion = g_CKFFGeneratedShaderABIVersion;
    out.InterfaceHash = g_CKFFGeneratedShaderInterfaceHash;
    for (unsigned i = 0; i < CKRST_BUILTIN_SHADER_COUNT; ++i) {
        const CK_SHADER_STAGE stage = i == CKRST_SHADER_FF_FRAGMENT || i == CKRST_SHADER_PRESENT_FRAGMENT
            ? CKRST_SHADER_PIXEL : CKRST_SHADER_VERTEX;
        SetShader(*profile, s_BuiltinShaders[i], stage, out.Shaders[i]);
    }
    return out.Matches(caps.ShaderFormat, caps.ShaderProfile) ? TRUE : FALSE;
}
