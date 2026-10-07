#include "CKSdlGpuShaders.h"
#include "CKFFShaderABI.h"
#include "CKSdlGpuShaderPack.h"
#include "shaders/generated/abi.h"

namespace {

CKBOOL SetShaderCode(SDL_GPUShaderFormat format, CKSdlShader shader, CKShaderDesc &out)
{
    return CKSdlGpuShaderPayload(format, out) &&
           CKSdlGpuShaderCode(format, shader, out.Code, out.CodeSize);
}

} // namespace

CKBOOL CKSdlGpuFFDepthPadVertexShader(SDL_GPUShaderFormat format,
                                     CKBOOL clipping,
                                     CKShaderDesc &out)
{
    out = CKShaderDesc();
    out.Stage = CKRST_SHADER_VERTEX;
    out.UniformBufferCount = CKSDL_SHADER_FF_POSITIONT_UNIFORM_BUFFERS;
    return SetShaderCode(format, clipping ? CKSDL_SHADER_VS_FF_POSITIONT_CLIP_DEPTH_PAD :
                                            CKSDL_SHADER_VS_FF_POSITIONT_DEPTH_PAD, out);
}

#if CKRE_ENABLE_DIRECTX
CKBOOL CKSdlGpuFFDxbcVertexShader(CKFFProgramVariant variant,
                                  CKShaderDesc &out)
{
    out = CKShaderDesc();
    out.Stage = CKRST_SHADER_VERTEX;
    out.UniformBufferCount = CKSDL_SHADER_FF_POSITIONT_UNIFORM_BUFFERS;
    CKSdlShader shader;
    switch (variant) {
    case CKFF_PROGRAM_3D:
        shader = CKSDL_SHADER_VS_FF_3D;
        out.UniformBufferCount = CKSDL_SHADER_FF_3D_UNIFORM_BUFFERS;
        break;
    case CKFF_PROGRAM_3D_CLIP:
        shader = CKSDL_SHADER_VS_FF_3D_CLIP;
        out.UniformBufferCount = CKSDL_SHADER_FF_3D_UNIFORM_BUFFERS;
        break;
    case CKFF_PROGRAM_POSITIONT:
        shader = CKSDL_SHADER_VS_FF_POSITIONT;
        break;
    case CKFF_PROGRAM_POSITIONT_CLIP:
        shader = CKSDL_SHADER_VS_FF_POSITIONT_CLIP;
        break;
    default:
        return FALSE;
    }
    return SetShaderCode(SDL_GPU_SHADERFORMAT_DXBC, shader, out);
}
#endif

CKBOOL CKSdlGpuBuildFFFragmentArtifactKey(
    const CKFFSamplerLayoutPlan &samplerLayoutPlan,
    CKBOOL requiresShaderSampling,
    CKSdlGpuFFFragmentArtifactKey &out)
{
    const CKFFSamplerLayout samplerLayout = samplerLayoutPlan.Layout;
    const CKDWORD compareSamplerCount =
        samplerLayoutPlan.CompareSamplerCount;
    if ((CKDWORD)samplerLayout >= CKFF_SAMPLER_LAYOUT_COUNT ||
        (requiresShaderSampling != FALSE &&
         requiresShaderSampling != TRUE))
        return FALSE;
    const CKDWORD twoDCount = CKFFSamplerTypeSlotCount(CKFF_SAMPLER_2D,
                                                       samplerLayout);
    // Selecting a wide cube or volume layout requires at least five stages of
    // that type, so at most three of the eight logical stages can be depth
    // comparison stages.
    const CKDWORD maximumCompareSamplerCount =
        samplerLayout == CKFF_SAMPLER_LAYOUT_WIDE_2D ? twoDCount : 3u;
    if (compareSamplerCount > maximumCompareSamplerCount)
        return FALSE;

    out = CKSdlGpuFFFragmentArtifactKey();
    out.SamplerLayout = samplerLayout;
    out.ComparisonResourceCount =
        samplerLayout == CKFF_SAMPLER_LAYOUT_WIDE_2D
            ? (CKBYTE)compareSamplerCount : 0;
    // Comparison resources need the shader sampling implementation for
    // explicit-gradient comparison and for mixed resource layouts. The
    // specialized wide-2D artifacts still use hardware SampleCmp whenever
    // the stage does not require an explicit footprint.
    out.UsesShaderSampling = requiresShaderSampling ||
        compareSamplerCount != 0;
    return TRUE;
}

CKDWORD CKSdlGpuFFFragmentArtifactIndex(
    const CKSdlGpuFFFragmentArtifactKey &artifactKey)
{
    const CKDWORD layout = (CKDWORD)artifactKey.SamplerLayout;
    const CKDWORD compareCount = artifactKey.ComparisonResourceCount;
    if (layout >= CKFF_SAMPLER_LAYOUT_COUNT ||
        (artifactKey.UsesShaderSampling != FALSE &&
         artifactKey.UsesShaderSampling != TRUE))
        return CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT;

    if (layout == CKFF_SAMPLER_LAYOUT_WIDE_2D) {
        if (compareCount > CKFF_MAX_TEXTURE_STAGES ||
            (compareCount != 0 && !artifactKey.UsesShaderSampling))
            return CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT;
        if (compareCount != 0)
            return compareCount + 1;
        return artifactKey.UsesShaderSampling ? 1u : 0u;
    }

    if (compareCount != 0)
        return CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT;
    const CKDWORD mixedLayoutBase = CKFF_MAX_TEXTURE_STAGES + 2u;
    return mixedLayoutBase + (layout - 1u) * 2u +
        (artifactKey.UsesShaderSampling ? 1u : 0u);
}

CKBOOL CKSdlGpuFFFragmentArtifactKeyAt(CKDWORD index,
                                       CKSdlGpuFFFragmentArtifactKey &out)
{
    out = CKSdlGpuFFFragmentArtifactKey();
    if (index >= CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT)
        return FALSE;
    // The wide 2D layout without shader sampling, with it, then with each
    // comparison count; then each other layout without and with it.
    const CKDWORD mixedLayoutBase = CKFF_MAX_TEXTURE_STAGES + 2u;
    if (index < mixedLayoutBase) {
        out.UsesShaderSampling = index != 0 ? TRUE : FALSE;
        out.ComparisonResourceCount = (CKBYTE)(index > 1 ? index - 1 : 0);
    } else {
        out.SamplerLayout = (CKFFSamplerLayout)((index - mixedLayoutBase) / 2u + 1u);
        out.UsesShaderSampling = ((index - mixedLayoutBase) & 1u) ? TRUE : FALSE;
    }
    return TRUE;
}

CKBOOL CKSdlGpuFFFragmentShader(SDL_GPUShaderFormat format,
                               const CKSdlGpuFFFragmentArtifactKey &artifactKey,
                               CKShaderDesc &out)
{
    // By artifact index: the wide 2D layout without and with shader
    // sampling, then with each comparison count; then the cube and volume
    // layouts without and with it.
    static const CKSdlShader shaders[CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT] = {
        CKSDL_SHADER_FS_FF_STAGE_NATIVE, CKSDL_SHADER_FS_FF_STAGE,
        CKSDL_SHADER_FS_FF_STAGE_COMPARE1, CKSDL_SHADER_FS_FF_STAGE_COMPARE2,
        CKSDL_SHADER_FS_FF_STAGE_COMPARE3, CKSDL_SHADER_FS_FF_STAGE_COMPARE4,
        CKSDL_SHADER_FS_FF_STAGE_COMPARE5, CKSDL_SHADER_FS_FF_STAGE_COMPARE6,
        CKSDL_SHADER_FS_FF_STAGE_COMPARE7, CKSDL_SHADER_FS_FF_STAGE_COMPARE8,
        CKSDL_SHADER_FS_FF_STAGE_CUBE_NATIVE, CKSDL_SHADER_FS_FF_STAGE_CUBE,
        CKSDL_SHADER_FS_FF_STAGE_VOLUME_NATIVE, CKSDL_SHADER_FS_FF_STAGE_VOLUME,
    };
    const CKDWORD index = CKSdlGpuFFFragmentArtifactIndex(artifactKey);
    out = CKShaderDesc();
    if (index >= CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT || format == SDL_GPU_SHADERFORMAT_DXBC)
        return FALSE;
    out.Stage = CKRST_SHADER_PIXEL;
    out.SamplerCount = CKFF_SAMPLER_SLOT_COUNT;
    out.UniformBufferCount = CKSDL_SHADER_FF_FRAGMENT_UNIFORM_BUFFERS;
    return SetShaderCode(format, shaders[index], out);
}

void CKSdlGpuShaderTargets(XClassArray<CKFFShaderTarget> &out)
{
    out.Clear();
    CKFFShaderTarget target;
    target.Format = CKRST_SHADER_FORMAT_DXIL;
    target.Profile = CKRST_SHADER_PROFILE_DX12;
    out.PushBack(target);
    target.Format = CKRST_SHADER_FORMAT_SPIRV;
    target.Profile = CKRST_SHADER_PROFILE_SPIRV;
    out.PushBack(target);
    target.Format = CKRST_SHADER_FORMAT_MSL;
    target.Profile = CKRST_SHADER_PROFILE_MSL;
    out.PushBack(target);
}

CKBOOL CKSdlGpuShaderSet(SDL_GPUShaderFormat format, CKFFShaderSet &out)
{
    out = CKFFShaderSet();
    out.ABIVersion = CKSDL_SHADER_ABI_VERSION;
    out.InterfaceHash = CKSDL_SHADER_INTERFACE_HASH;
    static const CKSdlShader shaders[CKRST_BUILTIN_SHADER_COUNT] = {
        CKSDL_SHADER_VS_FF_3D, CKSDL_SHADER_VS_FF_3D_CLIP,
        CKSDL_SHADER_VS_FF_POSITIONT, CKSDL_SHADER_VS_FF_POSITIONT_CLIP,
        CKSDL_SHADER_FS_FF_STAGE, CKSDL_SHADER_VS_POSTPROCESS, CKSDL_SHADER_FS_POSTPROCESS,
    };
    if (format != SDL_GPU_SHADERFORMAT_DXIL && format != SDL_GPU_SHADERFORMAT_SPIRV &&
        format != SDL_GPU_SHADERFORMAT_MSL)
        return FALSE;
    for (unsigned i = 0; i < CKRST_BUILTIN_SHADER_COUNT; ++i)
        if (!CKSdlGpuShaderCode(format, shaders[i], out.Shaders[i].Code, out.Shaders[i].CodeSize))
            return FALSE;
    for (unsigned i = 0; i < CKRST_BUILTIN_SHADER_COUNT; ++i) {
        auto &shader = out.Shaders[i];
        shader.Stage = i == CKRST_SHADER_FF_FRAGMENT || i == CKRST_SHADER_PRESENT_FRAGMENT ? CKRST_SHADER_PIXEL : CKRST_SHADER_VERTEX;
        CKSdlGpuShaderPayload(format, shader);
        switch (i) {
        case CKRST_SHADER_FF_3D:
        case CKRST_SHADER_FF_3D_CLIP:
            shader.UniformBufferCount = CKSDL_SHADER_FF_3D_UNIFORM_BUFFERS;
            break;
        case CKRST_SHADER_FF_POSITIONT:
        case CKRST_SHADER_FF_POSITIONT_CLIP:
            shader.UniformBufferCount = CKSDL_SHADER_FF_POSITIONT_UNIFORM_BUFFERS;
            break;
        case CKRST_SHADER_FF_FRAGMENT:
            shader.UniformBufferCount = CKSDL_SHADER_FF_FRAGMENT_UNIFORM_BUFFERS;
            break;
        case CKRST_SHADER_PRESENT_FRAGMENT:
            shader.UniformBufferCount = CKSDL_SHADER_PRESENT_UNIFORM_BUFFERS;
            break;
        default:
            shader.UniformBufferCount = 0;
            break;
        }
        shader.SamplerCount = i == CKRST_SHADER_FF_FRAGMENT ? 16 : (i == CKRST_SHADER_PRESENT_FRAGMENT ? 1 : 0);
    }
    CKShaderDesc fragment;
    const CKFFSamplerLayoutPlan defaultSamplerLayout;
    CKSdlGpuFFFragmentArtifactKey fragmentArtifactKey;
    if (!CKSdlGpuBuildFFFragmentArtifactKey(
            defaultSamplerLayout, TRUE, fragmentArtifactKey) ||
        !CKSdlGpuFFFragmentShader(format, fragmentArtifactKey, fragment))
        return FALSE;
    out.Shaders[CKRST_SHADER_FF_FRAGMENT] = fragment;
    return out.Matches(fragment.Format, fragment.Profile);
}
