// Fixed-function ordinary 2D texture sampling.

vec4 CKFFSample2D(int stage, vec2 uv, vec2 originalUv, int ordinal,
                  CKFFSamplerShaderProgram sampleProgram)
{
    float lodBias = sampleProgram.LodBias;
    float minMip = sampleProgram.MinimumMip;
    float maxAnisotropy = sampleProgram.AnisotropyTaps;
    vec2 originalDx = vec2_splat(0.0);
    vec2 originalDy = vec2_splat(0.0);
    if (sampleProgram.RequiresExplicitGradient) {
        originalDx = dFdx(originalUv);
        originalDy = dFdy(originalUv);
    }
// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
    bool borderMip = sampleProgram.ManualBorder &&
        (u_borderSampler[ordinal].y > 0.5 || sampleProgram.ManualAnisotropy);
#endif
// CKFF_BGFX_ONLY_END

#if CKFF_NATIVE_SDL_GPU
#define CKFF_TEXTURE_2D_GRAD(_sampler) \
    texture2DGrad(_sampler, uv, originalDx * exp2(lodBias), \
        originalDy * exp2(lodBias), minMip, maxAnisotropy)
#elif BGFX_SHADER_LANGUAGE_GLSL
    // bgfx aliases texture2DGrad to an ARB symbol even on core GLSL.
#define CKFF_TEXTURE_2D_GRAD(_sampler) \
    textureGrad(_sampler, uv, originalDx * exp2(lodBias), \
        originalDy * exp2(lodBias))
#else
#define CKFF_TEXTURE_2D_GRAD(_sampler) \
    texture2DGrad(_sampler, uv, originalDx * exp2(lodBias), \
        originalDy * exp2(lodBias))
#endif

#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_2D(_sampler) (sampleProgram.RequiresExplicitGradient ? \
    CKFF_TEXTURE_2D_GRAD(_sampler) : \
    CKFF_TEXTURE_2D_BIAS(_sampler, uv, lodBias))
#else
#define CKFF_SAMPLE_2D(_sampler) (sampleProgram.ManualAnisotropy ? \
    CKFF_TEXTURE_2D_ANISO(_sampler, uv, originalDx, originalDy, lodBias, \
        minMip, maxAnisotropy) : \
    (sampleProgram.ManualLod ? \
        CKFF_TEXTURE_2D_MIN_MIP(_sampler, uv, originalDx, originalDy, \
            lodBias, minMip) : \
        (sampleProgram.RequiresExplicitGradient ? CKFF_TEXTURE_2D_GRAD(_sampler) : \
            CKFF_TEXTURE_2D_BIAS(_sampler, uv, lodBias))))
#endif

#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_2D_FINAL(_sampler) CKFF_SAMPLE_2D(_sampler)
#else
// CKFF_BGFX_ONLY_BEGIN
#define CKFF_SAMPLE_2D_FINAL(_sampler) (borderMip ? \
    ckffBorderSample2D(_sampler, uv, originalDx, originalDy, lodBias, minMip, \
        maxAnisotropy, stage, ordinal, sampleProgram.BorderMask, sampleProgram.MinLinear, \
        sampleProgram.MagLinear) : CKFF_SAMPLE_2D(_sampler))
// CKFF_BGFX_ONLY_END
#endif

    vec4 color = vec4_splat(0.0);
#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_SAMPLER_LAYOUT == 0 && \
    CKFF_NATIVE_COMPARE_COUNT == 0
    // D3D12 requires the common wide-2D profile to retain sparse stage slots.
    CKFF_DISPATCH_2D_STAGE(stage, color, CKFF_SAMPLE_2D_FINAL)
#else
    CKFF_DISPATCH_2D_ORDINARY(ordinal, color, CKFF_SAMPLE_2D_FINAL)
#endif
#undef CKFF_SAMPLE_2D_FINAL
#undef CKFF_SAMPLE_2D
#undef CKFF_TEXTURE_2D_GRAD

// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
    if (sampleProgram.ManualBorder && !borderMip) {
#define CKFF_2D_SIZE(_sampler) CKFF_BORDER_SIZE_2D(_sampler)
        vec2 size = vec2_splat(1.0);
        CKFF_DISPATCH_2D_ALL(ordinal, size, CKFF_2D_SIZE)
#undef CKFF_2D_SIZE
        float lod = ckffClampedLod2D(
            originalDx, originalDy, size, lodBias, 0.0);
        bool filtered = lod > 0.0 ? sampleProgram.MinLinear : sampleProgram.MagLinear;
        color = mix(u_borderColor[stage], color,
            ckffBorderCoverage2D(uv, size, sampleProgram.BorderMask, filtered));
    }
#endif
// CKFF_BGFX_ONLY_END
    return color;
}
