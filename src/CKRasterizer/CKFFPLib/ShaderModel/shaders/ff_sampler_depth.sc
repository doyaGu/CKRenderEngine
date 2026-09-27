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

vec4 CKFFSampleDepth(int stage, vec4 coord, vec2 originalDx,
                     vec2 originalDy, int ordinal, int compareFunc,
                     CKFFSamplerShaderProgram sampleProgram)
{
    vec2 uv = coord.xy;
    if (compareFunc != 0) {
#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_COMPARE_COUNT > 0
#define CKFF_COMPARE_SAMPLE(_sampler) texture2DCompare( \
    _sampler, uv, originalDx, originalDy, sampleProgram.LodBias, \
    sampleProgram.MinimumMip, sampleProgram.AnisotropyTaps, coord.z, compareFunc)
        float compared = 0.0;
        CKFF_DISPATCH_DEPTH_COMPARE(
            ordinal, compared, CKFF_COMPARE_SAMPLE)
#undef CKFF_COMPARE_SAMPLE
        return vec4_splat(compared);
#elif !CKFF_NATIVE_SDL_GPU
// CKFF_BGFX_ONLY_BEGIN
        if (sampleProgram.ManualDepthCompare) {
            float compared = ckffCompareSample2D(
                ordinal, uv, originalDx, originalDy, sampleProgram.LodBias,
                sampleProgram.MinimumMip, sampleProgram.AnisotropyTaps, stage,
                sampleProgram.BorderMask, sampleProgram.MinLinear, sampleProgram.MagLinear,
                coord.z, compareFunc);
            return vec4_splat(compared);
        }
// CKFF_BGFX_ONLY_END
#endif
    }

    vec4 color = CKFFSample2D(
        stage, uv, originalDx, originalDy, ordinal, sampleProgram);
#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_COMPARE_COUNT == 0
    if (compareFunc != 0)
        return vec4_splat(compareDepth(color.r, coord.z, compareFunc));
#elif !CKFF_NATIVE_SDL_GPU
    if (compareFunc != 0)
        return vec4_splat(compareDepth(color.r, coord.z, compareFunc));
#endif
    return color.rrrr;
}

vec4 CKFFSampleTexture(int stage, vec4 coord, int samplerType,
                       int compareFunc, int samplerOrdinal,
                       int mirrorOnceMask, bool hasTexture)
{
    if (!hasTexture)
        return vec4(0.0, 0.0, 0.0, 1.0);

    CKFFSamplerShaderProgram sampleProgram = ckffReadSamplerShaderProgram(stage);
    // Preserve the existing eager footprint evaluation until the derivative
    // pass moves each derivative into its typed sampling path.
    vec2 originalDx = dFdx(coord.xy);
    vec2 originalDy = dFdy(coord.xy);
    vec3 originalCoord3 = coord.xyz;
    vec3 originalDx3 = dFdx(coord.xyz);
    vec3 originalDy3 = dFdy(coord.xyz);
    coord = applyMirrorOnceCoord(coord, mirrorOnceMask, samplerType);

    if (samplerType == 1)
        return CKFFSampleCube(
            coord, originalDx3, originalDy3, samplerOrdinal, sampleProgram);
    if (samplerType == 3)
        return CKFFSampleVolume(
            stage, coord, originalCoord3, originalDx3, originalDy3,
            samplerOrdinal, mirrorOnceMask, sampleProgram);
    if (samplerType == 2)
        return CKFFSampleDepth(
            stage, coord, originalDx, originalDy, samplerOrdinal,
            compareFunc, sampleProgram);
    return CKFFSample2D(
        stage, coord.xy, originalDx, originalDy, samplerOrdinal, sampleProgram);
}
