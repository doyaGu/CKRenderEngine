// Fixed-function volume texture sampling.

vec4 CKFFSampleVolume(int stage, vec4 coord, vec3 originalCoord,
                      int ordinal, int mirrorOnceMask,
                      CKFFSamplerShaderProgram sampleProgram)
{
    float lodBias = sampleProgram.LodBias;
    float minMip = sampleProgram.MinimumMip;
    float maxAnisotropy = sampleProgram.AnisotropyTaps;
    vec3 originalDx = vec3_splat(0.0);
    vec3 originalDy = vec3_splat(0.0);
    if (sampleProgram.RequiresExplicitGradient) {
        originalDx = dFdx(originalCoord);
        originalDy = dFdy(originalCoord);
    }
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_3D(_sampler) (sampleProgram.ManualAnisotropy ? \
    CKFF_TEXTURE_3D_ANISO(_sampler, coord.xyz, originalDx, originalDy, \
        lodBias, minMip, maxAnisotropy) : \
    (sampleProgram.RequiresExplicitGradient ? \
        CKFF_TEXTURE_3D_GRAD(_sampler, coord.xyz, originalDx, originalDy, \
            mirrorOnceMask, lodBias) : \
        CKFF_TEXTURE_3D_BIAS(_sampler, coord.xyz, lodBias)))
#else
#define CKFF_SAMPLE_3D(_sampler) (sampleProgram.ManualAnisotropy ? \
    CKFF_TEXTURE_3D_ANISO(_sampler, coord.xyz, originalDx, originalDy, \
        lodBias, minMip, maxAnisotropy) : \
    (sampleProgram.ManualLod ? \
        CKFF_TEXTURE_3D_MIN_MIP(_sampler, coord.xyz, originalDx, originalDy, \
            lodBias, minMip) : \
        (sampleProgram.RequiresExplicitGradient ? \
            CKFF_TEXTURE_3D_GRAD(_sampler, coord.xyz, originalDx, originalDy, \
                mirrorOnceMask, lodBias) : \
            CKFF_TEXTURE_3D_BIAS(_sampler, coord.xyz, lodBias))))
#endif

#if CKFF_NATIVE_SDL_GPU
#if CKFF_VOLUME_RESOURCE_ARRAY
    vec4 color = ckffNative3DSample(
        s_textureVolume[ordinal], s_textureVolumeSampler[ordinal],
        uint(CKFF_VOLUME_SLOT_BASE + ordinal), coord.xyz, originalDx,
        originalDy, sampleProgram.RequiresExplicitGradient, lodBias, minMip,
        maxAnisotropy);
#else
    vec4 color = vec4_splat(0.0);
    CKFF_DISPATCH_VOLUME(ordinal, color, CKFF_SAMPLE_3D)
#endif
#else
// CKFF_BGFX_ONLY_BEGIN
    bool borderMip = sampleProgram.ManualBorder &&
        (u_borderSampler[CKFF_VOLUME_SLOT_BASE + ordinal].y > 0.5 ||
         sampleProgram.ManualAnisotropy);
#define CKFF_SAMPLE_VOLUME(_sampler) (borderMip ? \
    ckffBorderSample3D(_sampler, coord.xyz, originalDx, originalDy, \
        lodBias, minMip, maxAnisotropy, stage, \
        CKFF_VOLUME_SLOT_BASE + ordinal, sampleProgram.BorderMask, \
        sampleProgram.MinLinear, sampleProgram.MagLinear) : CKFF_SAMPLE_3D(_sampler))
    vec4 color = vec4_splat(0.0);
    CKFF_DISPATCH_VOLUME(ordinal, color, CKFF_SAMPLE_VOLUME)
#undef CKFF_SAMPLE_VOLUME
    if (sampleProgram.ManualBorder && !borderMip) {
#define CKFF_VOLUME_SIZE(_sampler) CKFF_BORDER_SIZE_3D(_sampler)
        vec3 size = vec3_splat(1.0);
        CKFF_DISPATCH_VOLUME(ordinal, size, CKFF_VOLUME_SIZE)
#undef CKFF_VOLUME_SIZE
        float lod = ckffClampedLod3D(
            originalDx, originalDy, size, lodBias, 0.0);
        bool filtered = lod > 0.0 ? sampleProgram.MinLinear : sampleProgram.MagLinear;
        color = mix(u_borderColor[stage], color,
            ckffBorderCoverage3D(
                coord.xyz, size, sampleProgram.BorderMask, filtered));
    }
// CKFF_BGFX_ONLY_END
#endif
#undef CKFF_SAMPLE_3D
    return color;
}
