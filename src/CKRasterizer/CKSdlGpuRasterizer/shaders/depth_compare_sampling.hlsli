// Explicit-gradient depth comparison. Sampler declarations must precede this file.
#if !CKFF_HARDWARE_SAMPLING && \
    (CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 0 || CKFF_NATIVE_SAMPLER_LAYOUT != 0)
float ckManualCompareDepth(float depth, float reference, int function)
{
    if (function == 1) return reference < depth ? 1.0 : 0.0;
    if (function == 2) return reference <= depth ? 1.0 : 0.0;
    if (function == 3) return reference == depth ? 1.0 : 0.0;
    if (function == 4) return reference >= depth ? 1.0 : 0.0;
    if (function == 5) return reference > depth ? 1.0 : 0.0;
    if (function == 6) return reference != depth ? 1.0 : 0.0;
    if (function == 7) return 0.0;
    if (function == 8) return 1.0;
    return depth;
}

#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 0
#define CKFF_DISPATCH_COMPARE_RESOURCE(_ordinal, _result, _operation) \
    CKFF_DISPATCH_DEPTH_COMPARE(_ordinal, _result, _operation)
uint3 ckCompareDimensionsOf2D(CKFFDepthTexture2D image, uint mip)
#else
#define CKFF_DISPATCH_COMPARE_RESOURCE(_ordinal, _result, _operation) \
    CKFF_DISPATCH_2D_ALL(_ordinal, _result, _operation)
uint3 ckCompareDimensionsOf2D(CKFFTexture2D image, uint mip)
#endif
{
    uint width, height, levels;
    image.GetDimensions(mip, width, height, levels);
    return uint3(width, height, levels);
}

uint3 ckCompareDimensions2D(uint ordinal, uint mip)
{
    uint3 dimensions = uint3(1, 1, 1);
#define CKFF_COMPARE_DIMENSIONS(_sampler) \
    ckCompareDimensionsOf2D(_sampler, mip)
    CKFF_DISPATCH_COMPARE_RESOURCE(int(ordinal), dimensions,
                                   CKFF_COMPARE_DIMENSIONS)
#undef CKFF_COMPARE_DIMENSIONS
    return dimensions;
}

float ckCompareTap2D(uint ordinal, int2 p,
                     uint2 extent, uint mip, uint modes,
                     float reference, int function)
{
    bool outside = false;
    p.x = ckAddress(p.x, int(extent.x), modes & 15, outside);
    p.y = ckAddress(p.y, int(extent.y), (modes >> 4) & 15, outside);
    float depth = ck_borderColor[ordinal].r;
    if (!outside) {
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 0
#define CKFF_COMPARE_LOAD(_sampler) (_sampler).Load(int3(p, mip))
#else
#define CKFF_COMPARE_LOAD(_sampler) (_sampler).Load(int3(p, mip)).r
#endif
        CKFF_DISPATCH_COMPARE_RESOURCE(int(ordinal), depth,
                                       CKFF_COMPARE_LOAD)
#undef CKFF_COMPARE_LOAD
    }
    return ckManualCompareDepth(depth, reference, function);
}

float ckCompareLevel2D(uint ordinal, float2 uv,
                       uint mip, bool filtered, uint modes,
                       float reference, int function)
{
    uint2 extent = ckCompareDimensions2D(ordinal, mip).xy;
    if (!filtered)
        return ckCompareTap2D(ordinal, int2(floor(uv * extent)),
                              extent, mip, modes, reference, function);
    float2 coordinate = uv * extent - 0.5;
    int2 base = int2(floor(coordinate));
    float2 weight = frac(coordinate);
    float result = 0.0;
    [loop] for (int y = 0; y < 2; ++y) {
        [loop] for (int x = 0; x < 2; ++x) {
            float tapWeight = (x != 0 ? weight.x : 1.0 - weight.x) *
                              (y != 0 ? weight.y : 1.0 - weight.y);
            result += ckCompareTap2D(
                ordinal, base + int2(x, y), extent, mip, modes,
                reference, function) * tapWeight;
        }
    }
    return result;
}

float ckCompareMips2D(uint ordinal, float2 uv,
                      float lod, uint levels, bool filtered, uint modes,
                      uint mipFilter, float reference, int function)
{
    mipFilter &= 15;
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    if (mipFilter != 2 && mipFilter != 7)
        return ckCompareLevel2D(ordinal, uv,
                                uint(floor(lod + 0.5)), filtered, modes,
                                reference, function);
    uint lower = uint(floor(lod));
    uint upper = min(lower + 1, levels - 1);
    float weight = frac(lod);
    float result = 0.0;
    [loop] for (uint level = 0; level < 2; ++level) {
        uint mip = level == 0 ? lower : upper;
        result += ckCompareLevel2D(
            ordinal, uv, mip, filtered, modes, reference, function) *
            (level == 0 ? 1.0 - weight : weight);
    }
    return result;
}

float ckCompareSample2D(uint ordinal, float2 uv,
                        float2 dx, float2 dy, float bias, float minMip,
                        float reference, int function)
{
    uint modes = uint(ck_samplerInfo[ordinal].x);
    uint packedMipFilter = uint(ck_samplerInfo[ordinal].w);
    uint mipFilter = packedMipFilter & 15;
    float maxAnisotropy = max(1.0, float(packedMipFilter >> 4));
    uint3 dimensions = ckCompareDimensions2D(ordinal, 0);
    uint levels = dimensions.z;
    float2 extent = float2(dimensions.xy);
    float lx = length(dx * extent), ly = length(dy * extent);
    float lod = max(log2(max(max(lx, ly), 0.000001)) + bias, minMip);
    uint filter = uint(lod > 0.0 ? ck_samplerInfo[ordinal].y :
                                      ck_samplerInfo[ordinal].z);
    if (filter == 7 && maxAnisotropy > 1.0 && lod > 0.0) {
        float major = max(lx, ly);
        float minor = max(min(lx, ly), major / maxAnisotropy);
        uint taps = uint(clamp(ceil(major / max(minor, 1.0)),
                               1.0, maxAnisotropy));
        float2 step = (lx > ly ? dx : dy) / float(taps);
        float tapLod = max(log2(max(minor, 1.0)) + bias, minMip);
        float result = 0.0;
        [loop] for (uint tap = 0; tap < taps; ++tap)
            result += ckCompareMips2D(
                ordinal,
                uv + (float(tap) - float(taps - 1) * 0.5) * step,
                tapLod, levels, true, modes, mipFilter,
                reference, function);
        return result / float(taps);
    }
    return ckCompareMips2D(ordinal, uv, lod, levels, filter != 1,
                           modes, mipFilter, reference, function);
}
#undef CKFF_DISPATCH_COMPARE_RESOURCE
#endif
