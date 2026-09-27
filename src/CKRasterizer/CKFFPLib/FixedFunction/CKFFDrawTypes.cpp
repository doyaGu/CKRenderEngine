#include "CKFFDrawTypes.h"
#include "CKDrawStateCache.h"
#include "CKFFStateStore.h"
#include "CKRasterizerContextEnums.h"

#include <string.h>

float CKFFComputeDepthKey(const CKFFStateStore &state, const CKDrawStateCache &drawState)
{
    (void)drawState;

    return state.World[3][0] * state.View[0][2] +
           state.World[3][1] * state.View[1][2] +
           state.World[3][2] * state.View[2][2] +
           state.View[3][2];
}

CKDWORD CKFFEncodeDepthKey(float depth)
{
    CKDWORD bits = 0;
    memcpy(&bits, &depth, sizeof(bits));
    return bits;
}

CKBOOL CKFFDrawStateEquals(const CKDrawState &a, const CKDrawState &b)
{
    return a.Lo == b.Lo && a.Mid == b.Mid && a.Hi == b.Hi ? TRUE : FALSE;
}

CKDWORD CKFFHashBytes(const void *data, CKDWORD size, CKDWORD hash)
{
    const CKBYTE *bytes = (const CKBYTE *)data;
    for (CKDWORD i = 0; i < size; ++i) {
        hash ^= (CKDWORD)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

CKDWORD CKFFStaticTextureFlags(CKDWORD flags)
{
    return flags & (CKRST_TEXTURE_CUBEMAP |
                    CKRST_TEXTURE_VOLUMEMAP |
                    CKRST_TEXTURE_DEPTHSTENCIL);
}

CKDWORD CKFFHashTextureBindingSet(CKDWORD activeTextureCount, const CKFFTextureBinding *textures)
{
    CKDWORD hash = 2166136261u;
    hash = CKFFHashBytes(&activeTextureCount, sizeof(activeTextureCount), hash);
    for (CKDWORD i = 0; textures && i < activeTextureCount && i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        const CKFFTextureBinding &binding = textures[i];
        hash = CKFFHashBytes(&binding.Stage, sizeof(binding.Stage), hash);
        hash = CKFFHashBytes(&binding.Texture, sizeof(binding.Texture), hash);
        hash = CKFFHashBytes(&binding.TextureFlags, sizeof(binding.TextureFlags), hash);
        hash = CKFFHashBytes(&binding.Sampler, sizeof(binding.Sampler), hash);
        hash = CKFFHashBytes(&binding.ShaderState.Bits,
                             sizeof(binding.ShaderState.Bits), hash);
    }
    return hash;
}

CKFFFragmentSamplingMode CKFFResolveFragmentSamplingMode(
    const CKFFTextureBindingSet &textures)
{
    if (textures.SamplerLayoutPlan.CompareSamplerCount != 0)
        return CKFF_FRAGMENT_SAMPLING_FULL_EXACT;

    const CKDWORD manualSamplingMask =
        CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT |
        CKFF_SAMPLER_SHADER_MANUAL_LOD |
        CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY |
        CKFF_SAMPLER_SHADER_MANUAL_BORDER |
        CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE;
    const CKDWORD bindingCount = textures.ActiveTextureCount < CKFF_MAX_TEXTURE_STAGES
        ? textures.ActiveTextureCount : CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < bindingCount; ++stage) {
        const CKFFTextureBinding &binding = textures.Bindings[stage];
        if (binding.Texture != 0 &&
            (binding.ShaderState.Bits & manualSamplingMask) != 0)
            return CKFF_FRAGMENT_SAMPLING_FULL_EXACT;
    }
    return CKFF_FRAGMENT_SAMPLING_NATIVE_EXACT;
}
