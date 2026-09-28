// Fixed-function full-exact sampler path.
// Keep resource dispatch and typed sampling in one function: DXC otherwise
// clones the advanced sampler graph for every resource-bearing helper.

// Fixed-function depth texture sampling and comparison.

#ifndef CKFF_MANUAL_COMPARE_PROFILE
#define CKFF_MANUAL_COMPARE_PROFILE 0
#endif

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

// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU

vec2 ckffCompareSize2D(int ordinal, int mip)
{
#define CKFF_COMPARE_SIZE(_sampler) CKFF_BORDER_SIZE_2D_LEVEL(_sampler, mip)
    vec2 size = vec2_splat(1.0);
    CKFF_DISPATCH_2D_ALL(ordinal, size, CKFF_COMPARE_SIZE)
#undef CKFF_COMPARE_SIZE
    return size;
}

float ckffCompareDepth2D(int ordinal, vec2 uv, int mip)
{
#define CKFF_COMPARE_DEPTH(_sampler) \
    CKFF_BORDER_SAMPLE_2D(_sampler, uv, mip).r
    float depth = 0.0;
    CKFF_DISPATCH_2D_ALL(ordinal, depth, CKFF_COMPARE_DEPTH)
#undef CKFF_COMPARE_DEPTH
    return depth;
}

float ckffCompareTap2D(int ordinal, vec2 tap, vec2 size,
                       int mip, int borderMask, vec4 border,
                       float reference, int func)
{
    bool outside = ((borderMask & 1) != 0 &&
                    (tap.x < 0.0 || tap.x >= size.x)) ||
                   ((borderMask & 2) != 0 &&
                    (tap.y < 0.0 || tap.y >= size.y));
    vec2 tapUv = (tap + vec2_splat(0.5)) / size;
    float depth = outside ? border.r : ckffCompareDepth2D(ordinal, tapUv, mip);
    return compareDepth(depth, reference, func);
}

float ckffCompareLevel2D(int ordinal, vec2 uv,
                         int mip, bool filtered, int borderMask, vec4 border,
                         float reference, int func)
{
    vec2 size = ckffCompareSize2D(ordinal, mip);
    if (!filtered) {
        return ckffCompareTap2D(ordinal, floor(uv * size), size, mip,
                                borderMask, border, reference, func);
    }
    vec2 coordinate = uv * size - vec2_splat(0.5);
    vec2 base = floor(coordinate);
    vec2 weight = coordinate - base;
    float c00 = ckffCompareTap2D(ordinal, base, size, mip, borderMask,
                                 border, reference, func);
    float c10 = ckffCompareTap2D(ordinal, base + vec2(1.0, 0.0), size, mip,
                                 borderMask, border, reference, func);
    float c01 = ckffCompareTap2D(ordinal, base + vec2(0.0, 1.0), size, mip,
                                 borderMask, border, reference, func);
    float c11 = ckffCompareTap2D(ordinal, base + vec2(1.0, 1.0), size, mip,
                                 borderMask, border, reference, func);
    return mix(mix(c00, c10, weight.x), mix(c01, c11, weight.x), weight.y);
}

float ckffCompareMips2D(int ordinal, vec2 uv,
                        float lod, int mipCount, int mipFilter, bool filtered,
                        int borderMask, vec4 border, float reference, int func)
{
    float selected = mipFilter == 0 ? 0.0 :
        clamp(lod, 0.0, float(mipCount - 1));
    if (!ckffBorderLinearMips(mipFilter)) {
        return ckffCompareLevel2D(ordinal, uv, int(floor(selected + 0.5)),
                                  filtered, borderMask, border, reference, func);
    }
    int lower = int(floor(selected));
    int upper = min(lower + 1, mipCount - 1);
    return mix(ckffCompareLevel2D(ordinal, uv, lower, filtered, borderMask,
                                  border, reference, func),
               ckffCompareLevel2D(ordinal, uv, upper, filtered, borderMask,
                                  border, reference, func),
               selected - float(lower));
}

float ckffCompareSample2D(int ordinal, vec2 uv,
                          vec2 dx, vec2 dy, float bias, float minMip,
                          float maxAnisotropy, int stage, int borderMask,
                          bool minLinear, bool magLinear,
                          float reference, int func)
{
    vec2 size = ckffCompareSize2D(ordinal, 0);
    int mipCount = max(1, int(u_borderSampler[ordinal].x));
    int mipFilter = int(u_borderSampler[ordinal].y);
    vec4 border = u_borderColor[stage];
    if (maxAnisotropy > 1.0) {
        vec3 plan = ckffAnisoPlan(length(dx * size), length(dy * size),
                                  maxAnisotropy, bias, minMip);
        vec2 step = plan.z < 0.5 ? dx : dy;
        float value = 0.0;
        for (int tap = 0; tap < int(plan.x); ++tap) {
            value += ckffCompareMips2D(ordinal,
                uv + step * ((float(tap) + 0.5) / plan.x - 0.5),
                plan.y, mipCount, mipFilter, true, borderMask, border,
                reference, func);
        }
        return value / plan.x;
    }
    float lod = ckffClampedLod2D(dx, dy, size, bias, minMip);
    return ckffCompareMips2D(ordinal, uv, lod, mipCount, mipFilter,
                             lod > 0.0 ? minLinear : magLinear,
                             borderMask, border, reference, func);
}
#endif
// CKFF_BGFX_ONLY_END

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
#if CKFF_NATIVE_SDL_GPU
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
#if CKFF_VOLUME_RESOURCE_ARRAY
        return ckffNative3DSample(s_textureVolume[ordinal],
                                  s_textureVolumeSampler[ordinal],
                                  uint(CKFF_VOLUME_SLOT_BASE + ordinal),
                                  coord.xyz, originalCoord3, originalDx3,
                                  originalDy3, mirrorOnceMask, lodBias, minMip,
                                  maxAnisotropy);
#else
        vec4 volumeColor = vec4_splat(0.0);
        CKFF_DISPATCH_VOLUME(ordinal, volumeColor, CKFF_SAMPLE_3D)
        return volumeColor;
#endif
#else
// CKFF_BGFX_ONLY_BEGIN
        vec4 volumeColor = vec4_splat(0.0);
        bool volumeBorderMip = manualBorder &&
            (u_borderSampler[CKFF_VOLUME_SLOT_BASE + ordinal].y > 0.5 ||
             manualAnisotropy);
#define CKFF_SAMPLE_VOLUME(_sampler) (volumeBorderMip ? \
    ckffBorderSample3D(_sampler, coord.xyz, originalDx3, originalDy3, \
        lodBias, minMip, maxAnisotropy, stage, CKFF_VOLUME_SLOT_BASE + ordinal, borderMask, minLinear, magLinear) : \
    CKFF_SAMPLE_3D(_sampler))
        CKFF_DISPATCH_VOLUME(ordinal, volumeColor, CKFF_SAMPLE_VOLUME)
#undef CKFF_SAMPLE_VOLUME
        if (manualBorder && !volumeBorderMip) {
            vec3 size = vec3_splat(1.0);
#define CKFF_VOLUME_SIZE(_sampler) CKFF_BORDER_SIZE_3D(_sampler)
            CKFF_DISPATCH_VOLUME(ordinal, size, CKFF_VOLUME_SIZE)
#undef CKFF_VOLUME_SIZE
            float lod = ckffClampedLod3D(originalDx3, originalDy3, size, lodBias, 0.0);
            bool filtered = lod > 0.0 ? minLinear : magLinear;
            volumeColor = mix(u_borderColor[stage], volumeColor,
                ckffBorderCoverage3D(coord.xyz, size, borderMask, filtered));
        }
        return volumeColor;
// CKFF_BGFX_ONLY_END
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
// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
    // bgfx has only sixteen border palette entries for the whole frame.
    // The shader computes the coverage of texels inside each border axis.
    bool borderMip = manualBorder &&
        (u_borderSampler[ordinal].y > 0.5 || manualAnisotropy);
#endif
// CKFF_BGFX_ONLY_END

#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_COMPARE_COUNT == 0 && \
    (CKFF_MANUAL_COMPARE_PROFILE || CKFF_NATIVE_SAMPLER_LAYOUT != 0)
    if (samplerType == 2 && compareFunc != 0) {
        vec2 compareDx = dFdx(originalCoord.xy);
        vec2 compareDy = dFdy(originalCoord.xy);
#define CKFF_COMPARE_SAMPLE_MANUAL(_sampler) texture2DCompareManual( \
    _sampler, uv, compareDx, compareDy, lodBias, minMip, coord.z, compareFunc)
        float compared = 0.0;
        CKFF_DISPATCH_2D_ORDINARY(ordinal, compared,
                                  CKFF_COMPARE_SAMPLE_MANUAL)
#undef CKFF_COMPARE_SAMPLE_MANUAL
        return vec4_splat(compared);
    }
#endif
#if !CKFF_NATIVE_SDL_GPU || CKFF_NATIVE_COMPARE_COUNT > 0
    if (samplerType == 2 && compareFunc != 0) {
#if CKFF_NATIVE_SDL_GPU
#define CKFF_COMPARE_SAMPLE(_sampler) texture2DCompare( \
    _sampler, uv, originalDx, originalDy, lodBias, minMip, \
    maxAnisotropy, coord.z, compareFunc)
        float compared = 0.0;
        CKFF_DISPATCH_DEPTH_COMPARE(ordinal, compared, CKFF_COMPARE_SAMPLE)
#undef CKFF_COMPARE_SAMPLE
#else
// CKFF_BGFX_ONLY_BEGIN
        if (manualDepthCompare) {
            float compared = ckffCompareSample2D(
                ordinal, uv, originalDx, originalDy,
                lodBias, minMip, maxAnisotropy, stage, borderMask, minLinear,
                magLinear, coord.z, compareFunc);
            return vec4_splat(compared);
        }
// CKFF_BGFX_ONLY_END
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
// CKFF_BGFX_ONLY_BEGIN
#define CKFF_SAMPLE_2D_FINAL(_sampler) (borderMip ? \
    ckffBorderSample2D(_sampler, uv, originalDx, originalDy, lodBias, minMip, \
        maxAnisotropy, stage, ordinal, borderMask, minLinear, magLinear) : CKFF_SAMPLE_2D(_sampler))
// CKFF_BGFX_ONLY_END
#endif
#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_SAMPLER_LAYOUT == 0 && \
    CKFF_NATIVE_COMPARE_COUNT == 0 && !CKFF_MANUAL_COMPARE_PROFILE
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
    if (manualBorder && !borderMip) {
        vec2 size = vec2_splat(1.0);
#define CKFF_2D_SIZE(_sampler) CKFF_BORDER_SIZE_2D(_sampler)
        CKFF_DISPATCH_2D_ALL(ordinal, size, CKFF_2D_SIZE)
#undef CKFF_2D_SIZE
        float lod = ckffClampedLod2D(originalDx, originalDy, size, lodBias, 0.0);
        bool filtered = lod > 0.0 ? minLinear : magLinear;
        color = mix(u_borderColor[stage], color,
            ckffBorderCoverage2D(uv, size, borderMask, filtered));
    }
#endif
// CKFF_BGFX_ONLY_END
    if (samplerType == 2) {
#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_COMPARE_COUNT == 0
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
