#ifndef CKSDLGPU_SHADERS_H
#define CKSDLGPU_SHADERS_H

#include "CKBuiltinShaders.h"
#include "CKFFConstants.h"
#include "CKFFProgram.h"
#include <SDL3/SDL_gpu.h>

// FFP/presentation artifacts supplied by the rasterizer plugin. Private
// backend image operations have their own CKSdlGpuNativeShaders interface.
struct CKSdlGpuFFFragmentArtifactKey {
    CKFFSamplerLayout SamplerLayout;
    CKBYTE ComparisonResourceCount;
    CKBOOL UsesShaderSampling;

    CKSdlGpuFFFragmentArtifactKey()
        : SamplerLayout(CKFF_SAMPLER_LAYOUT_WIDE_2D),
          ComparisonResourceCount(0), UsesShaderSampling(FALSE) {}
};

enum {
    CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT =
        CKFF_SAMPLER_LAYOUT_COUNT * 2 + CKFF_MAX_TEXTURE_STAGES
};

// The shader targets the plugin has artifacts for, in device preference order.
void CKSdlGpuShaderTargets(XClassArray<CKFFShaderTarget> &Out);
CKBOOL CKSdlGpuShaderSet(SDL_GPUShaderFormat Format, CKFFShaderSet &Out);
CKBOOL CKSdlGpuBuildFFFragmentArtifactKey(
    const CKFFSamplerLayoutPlan &SamplerLayoutPlan,
    CKBOOL RequiresShaderSampling,
    CKSdlGpuFFFragmentArtifactKey &Out);
CKDWORD CKSdlGpuFFFragmentArtifactIndex(
    const CKSdlGpuFFFragmentArtifactKey &ArtifactKey);
// The key CKSdlGpuFFFragmentArtifactIndex gives an index. False past the
// artifacts.
CKBOOL CKSdlGpuFFFragmentArtifactKeyAt(CKDWORD Index,
                                       CKSdlGpuFFFragmentArtifactKey &Out);
CKBOOL CKSdlGpuFFFragmentShader(SDL_GPUShaderFormat Format,
                               const CKSdlGpuFFFragmentArtifactKey &ArtifactKey,
                               CKShaderDesc &Out);
CKBOOL CKSdlGpuFFDepthPadVertexShader(SDL_GPUShaderFormat Format,
                                     CKBOOL Clipping,
                                     CKShaderDesc &Out);
// The vertex shader of a program variant as shader model 5.1 DXBC. A D3D12
// pipeline cannot mix DXBC and DXIL, so it pairs with a DXBC fragment shader.
CKBOOL CKSdlGpuFFDxbcVertexShader(CKFFProgramVariant Variant,
                                  CKShaderDesc &Out);

#endif
