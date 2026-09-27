// SDL has no native border address mode. Loads evaluate the border per tap,
// including bilinear/trilinear footprints that straddle an edge. Other axes
// retain their own wrap/mirror/clamp mode. Cube directions have no outside
// domain and use the native seamless cube sampler.
int ckAddress(int index, int extent, uint mode, inout bool outside)
{
    if (mode == 4) outside = outside || index < 0 || index >= extent;
    if (mode == 1) return (index % extent + extent) % extent;
    if (mode == 2) {
        int folded = (index % (2 * extent) + 2 * extent) % (2 * extent);
        return folded < extent ? folded : 2 * extent - folded - 1;
    }
    return clamp(index, 0, extent - 1);
}

float4 ckTap2D(Texture2D<float4> image, uint slot, int2 p, uint2 extent, uint mip, uint modes)
{
    bool outside = false;
    p.x = ckAddress(p.x, int(extent.x), modes & 15, outside);
    p.y = ckAddress(p.y, int(extent.y), (modes >> 4) & 15, outside);
    return outside ? ck_borderColor[slot] : image.Load(int3(p, mip));
}

float4 ckLevel2D(Texture2D<float4> image, uint slot, float2 uv, uint mip, bool filtered, uint modes)
{
    uint width, height, levels;
    image.GetDimensions(mip, width, height, levels);
    uint2 extent = uint2(width, height);
    if (!filtered) return ckTap2D(image, slot, int2(floor(uv * extent)), extent, mip, modes);
    float2 coord = uv * extent - 0.5;
    int2 p = int2(floor(coord));
    float2 f = frac(coord);
    return lerp(lerp(ckTap2D(image, slot, p, extent, mip, modes), ckTap2D(image, slot, p + int2(1,0), extent, mip, modes), f.x),
                lerp(ckTap2D(image, slot, p + int2(0,1), extent, mip, modes), ckTap2D(image, slot, p + 1, extent, mip, modes), f.x), f.y);
}

float4 ckMips2D(Texture2D<float4> image, uint slot, float2 uv, float lod, uint levels, bool filtered, uint modes, uint mipFilter)
{
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    if (mipFilter != 2 && mipFilter != 7) return ckLevel2D(image, slot, uv, uint(floor(lod + 0.5)), filtered, modes);
    uint lower = uint(floor(lod)), upper = min(lower + 1, levels - 1);
    return lerp(ckLevel2D(image, slot, uv, lower, filtered, modes), ckLevel2D(image, slot, uv, upper, filtered, modes), frac(lod));
}

float4 ckSample2DBias(Texture2D<float4> image, SamplerState state, uint slot,
                      float2 uv, float bias, float minMip,
                      float maxAnisotropy)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4)
        return image.SampleBias(state, uv, bias);
    uint width, height, levels;
    image.GetDimensions(0, width, height, levels);
    float lod = max(image.CalculateLevelOfDetailUnclamped(state, uv) + bias, minMip);
    uint filter = uint(lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z);
    if (filter == 7 && lod > 0.0) {
        float2 dx = ddx(uv), dy = ddy(uv);
        float lx = length(dx * float2(width, height)), ly = length(dy * float2(width, height));
        float major = max(lx, ly);
        float tapLimit = max(maxAnisotropy, 1.0);
        float minor = max(min(lx, ly), major / tapLimit);
        uint taps = uint(clamp(ceil(major / max(minor, 1.0)), 1.0, tapLimit));
        float2 step = (lx > ly ? dx : dy) / float(taps);
        float4 result = 0.0;
        [loop] for (uint i = 0; i < taps; ++i)
            result += ckMips2D(image, slot, uv + (float(i) - float(taps - 1) * 0.5) * step,
                               max(log2(max(minor, 1.0)) + bias, minMip),
                               levels, true, modes, uint(ck_samplerInfo[slot].w));
        return result / float(taps);
    }
    return ckMips2D(image, slot, uv, lod, levels, filter != 1, modes, uint(ck_samplerInfo[slot].w));
}

float4 ckSample2D(Texture2D<float4> image, SamplerState state, uint slot, float2 uv)
{
    return ckSample2DBias(image, state, slot, uv, 0.0, 0.0, 0.0);
}

float4 ckSample2DGrad(Texture2D<float4> image, SamplerState state, uint slot,
                      float2 uv, float2 dx, float2 dy, float minMip,
                      float maxAnisotropy)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4)
        return image.SampleGrad(state, uv, dx, dy);
    uint width, height, levels;
    image.GetDimensions(0, width, height, levels);
    float2 extent = float2(width, height);
    float lx = length(dx * extent), ly = length(dy * extent);
    float lod = max(log2(max(max(lx, ly), 0.000001)), minMip);
    uint filter = uint(lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z);
    if (filter == 7 && lod > 0.0) {
        float major = max(lx, ly);
        float tapLimit = max(maxAnisotropy, 1.0);
        float minor = max(min(lx, ly), major / tapLimit);
        uint taps = uint(clamp(ceil(major / max(minor, 1.0)), 1.0, tapLimit));
        float2 step = (lx > ly ? dx : dy) / float(taps);
        float4 result = 0.0;
        [loop] for (uint i = 0; i < taps; ++i)
            result += ckMips2D(image, slot, uv + (float(i) - float(taps - 1) * 0.5) * step,
                               max(log2(max(minor, 1.0)), minMip),
                               levels, true, modes, uint(ck_samplerInfo[slot].w));
        return result / float(taps);
    }
    return ckMips2D(image, slot, uv, lod, levels, filter != 1, modes, uint(ck_samplerInfo[slot].w));
}

float4 ckTap3D(Texture3D<float4> image, uint slot, int3 p, uint3 extent, uint mip, uint modes)
{
    bool outside = false;
    p.x = ckAddress(p.x, int(extent.x), modes & 15, outside);
    p.y = ckAddress(p.y, int(extent.y), (modes >> 4) & 15, outside);
    p.z = ckAddress(p.z, int(extent.z), (modes >> 8) & 15, outside);
    return outside ? ck_borderColor[slot] : image.Load(int4(p, mip));
}

float4 ckLevel3D(Texture3D<float4> image, uint slot, float3 uv, uint mip, bool filtered, uint modes)
{
    uint width, height, depth, levels;
    image.GetDimensions(mip, width, height, depth, levels);
    uint3 extent = uint3(width, height, depth);
    if (!filtered) return ckTap3D(image, slot, int3(floor(uv * extent)), extent, mip, modes);
    float3 coord = uv * extent - 0.5;
    int3 p = int3(floor(coord));
    float3 f = frac(coord);
    float4 value = 0.0;
    [unroll] for (int z = 0; z < 2; ++z) [unroll] for (int y = 0; y < 2; ++y) [unroll] for (int x = 0; x < 2; ++x)
        value += ckTap3D(image, slot, p + int3(x,y,z), extent, mip, modes) *
                 (x ? f.x : 1.0 - f.x) * (y ? f.y : 1.0 - f.y) * (z ? f.z : 1.0 - f.z);
    return value;
}

float4 ckSample3DAtLod(Texture3D<float4> image, uint slot, float3 uv, float lod, uint modes)
{
    uint width, height, depth, levels;
    image.GetDimensions(0, width, height, depth, levels);
    bool filtered = (lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z) != 1.0;
    uint mipFilter = uint(ck_samplerInfo[slot].w);
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    if (mipFilter != 2 && mipFilter != 7) return ckLevel3D(image, slot, uv, uint(floor(lod + 0.5)), filtered, modes);
    uint lower = uint(floor(lod)), upper = min(lower + 1, levels - 1);
    return lerp(ckLevel3D(image, slot, uv, lower, filtered, modes), ckLevel3D(image, slot, uv, upper, filtered, modes), frac(lod));
}

float ckBorderAxisCoverage(float uv, uint extent, uint mode, bool filtered)
{
    if (mode != 4) return 1.0;
    if (!filtered) return uv >= 0.0 && uv < 1.0 ? 1.0 : 0.0;
    float coord = uv * float(extent) - 0.5;
    int base = int(floor(coord));
    float fraction = frac(coord);
    return (base >= 0 && base < int(extent) ? 1.0 - fraction : 0.0) +
           (base + 1 >= 0 && base + 1 < int(extent) ? fraction : 0.0);
}

float4 ckCompareVariantBorderLevel2D(Texture2D<float4> image,
                                     SamplerState state, uint slot,
                                     float2 uv, uint mip, bool filtered,
                                     uint modes)
{
    uint width, height, levels;
    image.GetDimensions(mip, width, height, levels);
    float coverage = ckBorderAxisCoverage(uv.x, width, modes & 15, filtered) *
                     ckBorderAxisCoverage(uv.y, height, (modes >> 4) & 15,
                                          filtered);
    float2 extent = float2(width, height);
    float2 sampleUv = filtered ? uv :
        (floor(uv * extent) + 0.5) / extent;
    return lerp(ck_borderColor[slot],
                image.SampleLevel(state, sampleUv, float(mip)), coverage);
}

float4 ckCompareVariantBorderMips2D(Texture2D<float4> image,
                                    SamplerState state, uint slot,
                                    float2 uv, float lod, uint levels,
                                    bool filtered, uint modes)
{
    uint mipFilter = uint(ck_samplerInfo[slot].w) & 15;
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    if (mipFilter != 2 && mipFilter != 7)
        return ckCompareVariantBorderLevel2D(
            image, state, slot, uv, uint(floor(lod + 0.5)), filtered, modes);
    uint lower = uint(floor(lod)), upper = min(lower + 1, levels - 1);
    return lerp(ckCompareVariantBorderLevel2D(
                    image, state, slot, uv, lower, filtered, modes),
                ckCompareVariantBorderLevel2D(
                    image, state, slot, uv, upper, filtered, modes),
                frac(lod));
}

float4 ckCompareVariantBorder2D(Texture2D<float4> image,
                                SamplerState state, uint slot, float2 uv,
                                float2 dx, float2 dy, float bias,
                                float minMip, float maxAnisotropy)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    uint width, height, levels;
    image.GetDimensions(0, width, height, levels);
    float2 extent = float2(width, height);
    float lx = length(dx * extent), ly = length(dy * extent);
    float lod = max(log2(max(max(lx, ly), 0.000001)) + bias, minMip);
    uint filter = uint(lod > 0.0 ? ck_samplerInfo[slot].y :
                                      ck_samplerInfo[slot].z);
    if (filter == 7 && lod > 0.0) {
        float major = max(lx, ly);
        float tapLimit = max(maxAnisotropy, 1.0);
        float minor = max(min(lx, ly), major / tapLimit);
        uint taps = uint(clamp(ceil(major / max(minor, 1.0)),
                               1.0, tapLimit));
        float2 step = (lx > ly ? dx : dy) / float(taps);
        float tapLod = max(log2(max(minor, 1.0)) + bias, minMip);
        float4 result = 0.0;
        [loop] for (uint tap = 0; tap < taps; ++tap)
            result += ckCompareVariantBorderMips2D(
                image, state, slot,
                uv + (float(tap) - float(taps - 1) * 0.5) * step,
                tapLod, levels, true, modes);
        return result / float(taps);
    }
    return ckCompareVariantBorderMips2D(
        image, state, slot, uv, lod, levels, filter != 1, modes);
}

float4 ckCompareVariantSample2DBias(Texture2D<float4> image,
                                    SamplerState state, uint slot,
                                    float2 uv, float bias, float minMip,
                                    float maxAnisotropy)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4)
        return image.SampleBias(state, uv, bias);
    return ckCompareVariantBorder2D(image, state, slot, uv,
                                    ddx(uv), ddy(uv), bias, minMip,
                                    maxAnisotropy);
}

float4 ckCompareVariantSample2DGrad(Texture2D<float4> image,
                                    SamplerState state, uint slot,
                                    float2 uv, float2 dx, float2 dy,
                                    float minMip, float maxAnisotropy)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4)
        return image.SampleGrad(state, uv, dx, dy);
    return ckCompareVariantBorder2D(image, state, slot, uv,
                                    dx, dy, 0.0, minMip, maxAnisotropy);
}

float4 ckSample3DBorderLevel(Texture3D<float4> image, SamplerState state,
                             uint slot, float3 uv, uint mip, uint modes,
                             bool filtered)
{
    uint width, height, depth, levels;
    image.GetDimensions(mip, width, height, depth, levels);
    float coverage = ckBorderAxisCoverage(uv.x, width, modes & 15, filtered) *
                     ckBorderAxisCoverage(uv.y, height, (modes >> 4) & 15, filtered) *
                     ckBorderAxisCoverage(uv.z, depth, (modes >> 8) & 15, filtered);
    return lerp(ck_borderColor[slot], image.SampleLevel(state, uv, float(mip)),
                coverage);
}

float4 ckSample3DBorderLod(Texture3D<float4> image, SamplerState state,
                           uint slot, float3 uv, float lod, uint modes)
{
    uint width, height, depth, levels;
    image.GetDimensions(0, width, height, depth, levels);
    uint mipFilter = uint(ck_samplerInfo[slot].w);
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    bool filtered = (lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z) != 1.0;
    if (mipFilter != 2 && mipFilter != 7)
        return ckSample3DBorderLevel(image, state, slot, uv,
                                     uint(floor(lod + 0.5)), modes, filtered);
    uint lower = uint(floor(lod)), upper = min(lower + 1, levels - 1);
    return lerp(ckSample3DBorderLevel(image, state, slot, uv, lower, modes, filtered),
                ckSample3DBorderLevel(image, state, slot, uv, upper, modes, filtered),
                frac(lod));
}

float4 ckSample3DBias(Texture3D<float4> image, SamplerState state, uint slot,
                      float3 uv, float bias, float minMip)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4 && ((modes >> 8) & 15) != 4)
        return image.SampleBias(state, uv, bias);
    float lod = max(image.CalculateLevelOfDetailUnclamped(state, uv) + bias, minMip);
    return ckSample3DAtLod(image, slot, uv, lod, modes);
}

float4 ckSample3D(Texture3D<float4> image, SamplerState state, uint slot, float3 uv)
{
    return ckSample3DBias(image, state, slot, uv, 0.0, 0.0);
}

float4 ckSample3DGrad(Texture3D<float4> image, SamplerState state, uint slot,
                      float3 uv, float3 originalUv, int mirrorOnceMask,
                      float bias, float minMip)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if (mirrorOnceMask == 0 && minMip <= 0.0 && (modes & 15) != 4 &&
        ((modes >> 4) & 15) != 4 && ((modes >> 8) & 15) != 4)
        return image.SampleBias(state, uv, bias);
    // Compute mip selection before MIRRORONCE folds the coordinates. Keep the
    // per-tap border path for axes that use BORDER instead of the native clamp.
    float lod = max(image.CalculateLevelOfDetailUnclamped(state, originalUv) +
                    bias, minMip);
    if ((modes & 15) == 4 || ((modes >> 4) & 15) == 4 || ((modes >> 8) & 15) == 4)
        return ckSample3DAtLod(image, slot, uv, lod, modes);
    return image.SampleLevel(state, uv, lod);
}
