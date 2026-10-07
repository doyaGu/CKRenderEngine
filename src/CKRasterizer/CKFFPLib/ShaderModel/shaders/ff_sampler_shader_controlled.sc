// Fixed-function shader-controlled sampler path.
// Keep resource dispatch and typed sampling in one function: DXC otherwise
// clones the advanced sampler graph for every resource-bearing helper.

// Fixed-function depth texture sampling and comparison.

float compareDepth(float depth, float ref, int func)
{
    if (func == 1) return ref < depth ? 1.0 : 0.0;
    if (func == 2) return ref <= depth ? 1.0 : 0.0;
    if (func == 3) return ref == depth ? 1.0 : 0.0;
    if (func == 4) return ref >= depth ? 1.0 : 0.0;
    if (func == 5) return ref > depth ? 1.0 : 0.0;
    if (func == 6) return ref != depth ? 1.0 : 0.0;
    if (func == 7) return 0.0;
    if (func == 8) return 1.0;
    return depth;
}


vec4 CKFFSampleTexture(int stage, vec4 coord, int samplerType, int compareFunc,
                     int samplerOrdinal, int mirrorOnceMask, bool hasTexture)
{
    if (!hasTexture) return vec4(0.0, 0.0, 0.0, 1.0);
    float lodBias = u_bumpEnv[stage * 2 + 1].z;
    int samplerState = int(u_bumpEnv[stage * 2 + 1].w);
    float minMip = float((samplerState >> CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT) &
                         CKFF_SAMPLER_SHADER_MIN_MIP_MASK);
    float maxAnisotropy = float(
        (samplerState >> CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT) &
        CKFF_SAMPLER_SHADER_ANISOTROPY_MASK);
    int borderMask =
        (samplerState >> CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT) &
        CKFF_SAMPLER_SHADER_BORDER_AXIS_MASK;
    bool minLinear =
        (samplerState & CKFF_SAMPLER_SHADER_MIN_FILTER_LINEAR) != 0;
    bool magLinear =
        (samplerState & CKFF_SAMPLER_SHADER_MAG_FILTER_LINEAR) != 0;
    bool requiresExplicitGradient =
        (samplerState & CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT) != 0;
    bool manualLod =
        (samplerState & CKFF_SAMPLER_SHADER_MANUAL_LOD) != 0;
    bool manualAnisotropy =
        (samplerState & CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY) != 0;
    bool manualBorder =
        (samplerState & CKFF_SAMPLER_SHADER_MANUAL_BORDER) != 0;
    bool manualDepthCompare =
        (samplerState & CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE) != 0;
    // Addressing must not change the footprint used to choose a mip level.
    // Preserve the source coordinates, then calculate only the dimensional
    // derivatives needed by the selected typed sampler path.
    vec4 originalCoord = coord;
    coord = applyMirrorOnceCoord(coord, mirrorOnceMask, samplerType);
    if (samplerType == 1) {
        int ordinal = samplerOrdinal;
        vec3 originalDx3 = vec3_splat(0.0);
        vec3 originalDy3 = vec3_splat(0.0);
        if (requiresExplicitGradient) {
            originalDx3 = dFdx(originalCoord.xyz);
            originalDy3 = dFdy(originalCoord.xyz);
        }
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_CUBE(_sampler) CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)
#else
#define CKFF_SAMPLE_CUBE(_sampler) (manualAnisotropy ? \
    CKFF_TEXTURE_CUBE_ANISO(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip, maxAnisotropy) : \
    (manualLod ? CKFF_TEXTURE_CUBE_MIN_MIP(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip) : \
    CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)))
#endif
        vec4 color = vec4_splat(0.0);
        CKFF_DISPATCH_CUBE(ordinal, color, CKFF_SAMPLE_CUBE)
#undef CKFF_SAMPLE_CUBE
        return color;
    }
    if (samplerType == 3) {
        int ordinal = samplerOrdinal;
        vec3 originalCoord3 = originalCoord.xyz;
        vec3 originalDx3 = vec3_splat(0.0);
        vec3 originalDy3 = vec3_splat(0.0);
        if (requiresExplicitGradient) {
            originalDx3 = dFdx(originalCoord3);
            originalDy3 = dFdy(originalCoord3);
        }
#if CKFF_NATIVE_SDL_GPU && CKFF_VOLUME_RESOURCE_ARRAY
#define CKFF_SAMPLE_3D(_sampler) ckffNative3DSample( \
    _sampler, _sampler##Sampler, _sampler##Slot, coord.xyz, originalCoord3, \
    originalDx3, originalDy3, mirrorOnceMask, lodBias, minMip, maxAnisotropy)
#elif CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_3D(_sampler) (manualAnisotropy ? \
    CKFF_TEXTURE_3D_ANISO(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip, maxAnisotropy) : \
    CKFF_TEXTURE_3D_GRAD(_sampler, coord.xyz, originalCoord3, originalDx3, originalDy3, mirrorOnceMask, lodBias))
#else
#define CKFF_SAMPLE_3D(_sampler) (manualAnisotropy ? \
    CKFF_TEXTURE_3D_ANISO(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip, maxAnisotropy) : \
    (manualLod ? CKFF_TEXTURE_3D_MIN_MIP(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip) : \
    CKFF_TEXTURE_3D_GRAD(_sampler, coord.xyz, originalCoord3, originalDx3, originalDy3, mirrorOnceMask, lodBias)))
#endif
#if CKFF_NATIVE_SDL_GPU
        vec4 volumeColor = vec4_splat(0.0);
        CKFF_DISPATCH_VOLUME(ordinal, volumeColor, CKFF_SAMPLE_3D)
        return volumeColor;
#else
#endif
#undef CKFF_SAMPLE_3D
    }

    vec2 uv = coord.xy;
    vec2 originalDx = vec2_splat(0.0);
    vec2 originalDy = vec2_splat(0.0);
    if (requiresExplicitGradient) {
        originalDx = dFdx(originalCoord.xy);
        originalDy = dFdy(originalCoord.xy);
    }
    vec4 color = vec4_splat(0.0);
    int ordinal = samplerOrdinal;

#if CKFF_NATIVE_SDL_GPU && \
    (CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 0 || \
     CKFF_NATIVE_SAMPLER_LAYOUT != 0)
    if (samplerType == 2 && compareFunc != 0 &&
        (CKFF_DEPTH_COMPARE_SAMPLER_COUNT == 0 || requiresExplicitGradient)) {
        vec2 compareDx = dFdx(originalCoord.xy);
        vec2 compareDy = dFdy(originalCoord.xy);
        float compared = ckCompareSample2D(
            uint(ordinal), uv, compareDx, compareDy,
            lodBias, minMip, coord.z, compareFunc);
        return vec4_splat(compared);
    }
#endif
#if !CKFF_NATIVE_SDL_GPU || CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 0
    if (samplerType == 2 && compareFunc != 0) {
#if CKFF_NATIVE_SDL_GPU
#define CKFF_COMPARE_SAMPLE(_sampler) texture2DCompare( \
    _sampler, uv, originalDx, originalDy, lodBias, minMip, \
    maxAnisotropy, coord.z, compareFunc)
        float compared = 0.0;
        CKFF_DISPATCH_DEPTH_COMPARE(ordinal, compared, CKFF_COMPARE_SAMPLE)
#undef CKFF_COMPARE_SAMPLE
#else
#endif
#if CKFF_NATIVE_SDL_GPU
        return vec4_splat(compared);
#endif
    }
#endif
#if CKFF_NATIVE_SDL_GPU
#define CKFF_TEXTURE_2D_GRAD(_sampler) texture2DGrad(_sampler, uv, originalDx * exp2(lodBias), originalDy * exp2(lodBias), minMip, maxAnisotropy)
#elif BGFX_SHADER_LANGUAGE_GLSL
    // bgfx's OpenGL compatibility preamble aliases texture2DGrad to the ARB
    // extension even on core GLSL contexts; use the core entry point here.
#define CKFF_TEXTURE_2D_GRAD(_sampler) textureGrad(_sampler, uv, originalDx * exp2(lodBias), originalDy * exp2(lodBias))
#else
#define CKFF_TEXTURE_2D_GRAD(_sampler) texture2DGrad(_sampler, uv, originalDx * exp2(lodBias), originalDy * exp2(lodBias))
#endif
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_2D(_sampler) (requiresExplicitGradient ? \
    CKFF_TEXTURE_2D_GRAD(_sampler) : CKFF_TEXTURE_2D_BIAS(_sampler, uv, lodBias))
#else
#define CKFF_SAMPLE_2D(_sampler) (manualAnisotropy ? \
    CKFF_TEXTURE_2D_ANISO(_sampler, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy) : \
    (manualLod ? CKFF_TEXTURE_2D_MIN_MIP(_sampler, uv, originalDx, originalDy, lodBias, minMip) : \
    (requiresExplicitGradient ? CKFF_TEXTURE_2D_GRAD(_sampler) : \
    CKFF_TEXTURE_2D_BIAS(_sampler, uv, lodBias))))
#endif
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_2D_FINAL(_sampler) CKFF_SAMPLE_2D(_sampler)
#else
#endif
#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_SAMPLER_LAYOUT == 0 && \
    CKFF_DEPTH_COMPARE_SAMPLER_COUNT == 0
    // D3D12 requires the common wide-2D variant to retain sparse stage slots.
    CKFF_DISPATCH_2D_STAGE(stage, color, CKFF_SAMPLE_2D_FINAL)
#else
    CKFF_DISPATCH_2D_ORDINARY(ordinal, color, CKFF_SAMPLE_2D_FINAL)
#endif
#undef CKFF_SAMPLE_2D_FINAL
#undef CKFF_SAMPLE_2D
#undef CKFF_TEXTURE_2D_GRAD
    if (samplerType == 2) {
#if CKFF_NATIVE_SDL_GPU && CKFF_DEPTH_COMPARE_SAMPLER_COUNT == 0
        float depth = color.r;
        if (compareFunc != 0)
            return vec4_splat(compareDepth(depth, coord.z, compareFunc));
#elif !CKFF_NATIVE_SDL_GPU
        if (compareFunc != 0)
            return vec4_splat(compareDepth(color.r, coord.z, compareFunc));
#endif
        return color.rrrr;
    }
    return color;
}
