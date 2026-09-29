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

CKBOOL CKSdlGpuShaderSet(SDL_GPUShaderFormat Format, CKFFShaderSet &Out);
CKBOOL CKSdlGpuBuildFFFragmentArtifactKey(
    const CKFFSamplerLayoutPlan &SamplerLayoutPlan,
    CKBOOL RequiresShaderSampling,
    CKSdlGpuFFFragmentArtifactKey &Out);
CKDWORD CKSdlGpuFFFragmentArtifactIndex(
    const CKSdlGpuFFFragmentArtifactKey &ArtifactKey);
CKBOOL CKSdlGpuFFFragmentShader(SDL_GPUShaderFormat Format,
                               const CKSdlGpuFFFragmentArtifactKey &ArtifactKey,
                               CKShaderDesc &Out);
CKBOOL CKSdlGpuFFDepthPadVertexShader(SDL_GPUShaderFormat Format,
                                     CKBOOL Clipping,
                                     CKShaderDesc &Out);

#endif
