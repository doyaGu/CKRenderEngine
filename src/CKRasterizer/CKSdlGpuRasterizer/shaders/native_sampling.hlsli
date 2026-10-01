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

float4 ckTap2D(CKFFTexture2D image, uint slot, int2 p, uint2 extent, uint mip, uint modes)
{
    bool outside = false;
    p.x = ckAddress(p.x, int(extent.x), modes & 15, outside);
    p.y = ckAddress(p.y, int(extent.y), (modes >> 4) & 15, outside);
    return outside ? ck_borderColor[slot] : image.Load(int3(p, mip));
}

float4 ckLevel2D(CKFFTexture2D image, uint slot, float2 uv, uint mip, bool filtered, uint modes)
{
    uint width, height, levels;
    image.GetDimensions(mip, width, height, levels);
    uint2 extent = uint2(width, height);
    // A filtered level blends the rows of its 2x2 footprint; an unfiltered
    // one takes its single tap.
    float2 coord = filtered ? uv * extent - 0.5 : uv * extent;
    int2 p = int2(floor(coord));
    float2 f = frac(coord);
    int span = filtered ? 2 : 1;
    float4 result = 0.0;
    [loop] for (int y = 0; y < span; ++y) {
        float4 row = 0.0;
        [loop] for (int x = 0; x < span; ++x) {
            float4 value = ckTap2D(image, slot, p + int2(x, y), extent, mip, modes);
            row = x == 0 ? value : lerp(row, value, f.x);
        }
        result = y == 0 ? row : lerp(result, row, f.y);
    }
    return result;
}

float4 ckMips2D(CKFFTexture2D image, uint slot, float2 uv, float lod, uint levels, bool filtered, uint modes, uint mipFilter)
{
    mipFilter &= 15;
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    // A linear mip filter blends the levels about lod; others take the nearest.
    bool blend = mipFilter == 2 || mipFilter == 7;
    uint lower = uint(floor(blend ? lod : lod + 0.5)), upper = min(lower + 1, levels - 1);
    float4 result = 0.0;
    [loop] for (uint level = 0; level < (blend ? 2u : 1u); ++level) {
        float4 value = ckLevel2D(image, slot, uv, level == 0 ? lower : upper, filtered, modes);
        result = level == 0 ? value : lerp(result, value, frac(lod));
    }
    return result;
}

// Averages taps spaced step apart along a line centred on uv; a single tap
// samples uv itself.
float4 ckTaps2D(CKFFTexture2D image, uint slot, float2 uv, float2 step, uint taps,
                float lod, uint levels, bool filtered, uint modes)
{
    float4 result = 0.0;
    [loop] for (uint i = 0; i < taps; ++i)
        result += ckMips2D(image, slot, uv + (float(i) - float(taps - 1) * 0.5) * step,
                           lod, levels, filtered, modes, uint(ck_samplerInfo[slot].w));
    return result / float(taps);
}

float4 ckSample2DBias(CKFFTexture2D image, CKFFSampler state, uint slot,
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
    uint taps = 1;
    float2 step = 0.0;
    if (filter == 7 && lod > 0.0) {
        float2 dx = ddx(uv), dy = ddy(uv);
        float lx = length(dx * float2(width, height)), ly = length(dy * float2(width, height));
        float major = max(lx, ly);
        float tapLimit = max(maxAnisotropy, 1.0);
        float minor = max(min(lx, ly), major / tapLimit);
        taps = uint(clamp(ceil(major / max(minor, 1.0)), 1.0, tapLimit));
        step = (lx > ly ? dx : dy) / float(taps);
        lod = max(log2(max(minor, 1.0)) + bias, minMip);
    }
    return ckTaps2D(image, slot, uv, step, taps, lod, levels, filter != 1, modes);
}

float4 ckSample2D(CKFFTexture2D image, CKFFSampler state, uint slot, float2 uv)
{
    return ckSample2DBias(image, state, slot, uv, 0.0, 0.0, 0.0);
}

float4 ckSample2DGrad(CKFFTexture2D image, CKFFSampler state, uint slot,
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
    uint taps = 1;
    float2 step = 0.0;
    if (filter == 7 && lod > 0.0) {
        float major = max(lx, ly);
        float tapLimit = max(maxAnisotropy, 1.0);
        float minor = max(min(lx, ly), major / tapLimit);
        taps = uint(clamp(ceil(major / max(minor, 1.0)), 1.0, tapLimit));
        step = (lx > ly ? dx : dy) / float(taps);
        lod = max(log2(max(minor, 1.0)), minMip);
    }
    return ckTaps2D(image, slot, uv, step, taps, lod, levels, filter != 1, modes);
}


float4 ckTap3D(CKFFTexture3D image, uint slot, int3 p, uint3 extent, uint mip, uint modes)
{
    bool outside = false;
    p.x = ckAddress(p.x, int(extent.x), modes & 15, outside);
    p.y = ckAddress(p.y, int(extent.y), (modes >> 4) & 15, outside);
    p.z = ckAddress(p.z, int(extent.z), (modes >> 8) & 15, outside);
    return outside ? ck_borderColor[slot] : image.Load(int4(p, mip));
}

float4 ckLevel3D(CKFFTexture3D image, uint slot, float3 uv, uint mip, bool filtered, uint modes)
{
    uint width, height, depth, levels;
    image.GetDimensions(mip, width, height, depth, levels);
    uint3 extent = uint3(width, height, depth);
    // A filtered level weighs its 2x2x2 footprint; an unfiltered one takes its
    // single tap at unit weight.
    float3 coord = filtered ? uv * extent - 0.5 : uv * extent;
    int3 p = int3(floor(coord));
    float3 f = filtered ? frac(coord) : 0.0;
    int span = filtered ? 2 : 1;
    float4 value = 0.0;
    [loop] for (int z = 0; z < span; ++z) [loop] for (int y = 0; y < span; ++y) [loop] for (int x = 0; x < span; ++x)
        value += ckTap3D(image, slot, p + int3(x,y,z), extent, mip, modes) *
                 (x ? f.x : 1.0 - f.x) * (y ? f.y : 1.0 - f.y) * (z ? f.z : 1.0 - f.z);
    return value;
}

float4 ckSample3DAtLod(CKFFTexture3D image, uint slot, float3 uv, float lod, uint modes)
{
    uint width, height, depth, levels;
    image.GetDimensions(0, width, height, depth, levels);
    bool filtered = (lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z) != 1.0;
    uint mipFilter = uint(ck_samplerInfo[slot].w) & 15;
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    bool blend = mipFilter == 2 || mipFilter == 7;
    uint lower = uint(floor(blend ? lod : lod + 0.5)), upper = min(lower + 1, levels - 1);
    float4 result = 0.0;
    [loop] for (uint level = 0; level < (blend ? 2u : 1u); ++level) {
        float4 value = ckLevel3D(image, slot, uv, level == 0 ? lower : upper, filtered, modes);
        result = level == 0 ? value : lerp(result, value, frac(lod));
    }
    return result;
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

float4 ckCompareVariantBorderLevel2D(CKFFTexture2D image,
                                     CKFFSampler state, uint slot,
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

float4 ckCompareVariantBorderMips2D(CKFFTexture2D image,
                                    CKFFSampler state, uint slot,
                                    float2 uv, float lod, uint levels,
                                    bool filtered, uint modes)
{
    uint mipFilter = uint(ck_samplerInfo[slot].w) & 15;
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    bool blend = mipFilter == 2 || mipFilter == 7;
    uint lower = uint(floor(blend ? lod : lod + 0.5)), upper = min(lower + 1, levels - 1);
    float4 result = 0.0;
    [loop] for (uint level = 0; level < (blend ? 2u : 1u); ++level) {
        float4 value = ckCompareVariantBorderLevel2D(
            image, state, slot, uv, level == 0 ? lower : upper, filtered, modes);
        result = level == 0 ? value : lerp(result, value, frac(lod));
    }
    return result;
}

float4 ckCompareVariantBorder2D(CKFFTexture2D image,
                                CKFFSampler state, uint slot, float2 uv,
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
    uint taps = 1;
    float2 step = 0.0;
    if (filter == 7 && lod > 0.0) {
        float major = max(lx, ly);
        float tapLimit = max(maxAnisotropy, 1.0);
        float minor = max(min(lx, ly), major / tapLimit);
        taps = uint(clamp(ceil(major / max(minor, 1.0)),
                          1.0, tapLimit));
        step = (lx > ly ? dx : dy) / float(taps);
        lod = max(log2(max(minor, 1.0)) + bias, minMip);
    }
    float4 result = 0.0;
    [loop] for (uint tap = 0; tap < taps; ++tap)
        result += ckCompareVariantBorderMips2D(
            image, state, slot,
            uv + (float(tap) - float(taps - 1) * 0.5) * step,
            lod, levels, filter != 1, modes);
    return result / float(taps);
}

float4 ckCompareVariantSample2DBias(CKFFTexture2D image,
                                    CKFFSampler state, uint slot,
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

float4 ckCompareVariantSample2DGrad(CKFFTexture2D image,
                                    CKFFSampler state, uint slot,
                                    float2 uv, float2 dx, float2 dy,
                                    float minMip, float maxAnisotropy)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4)
        return image.SampleGrad(state, uv, dx, dy);
    return ckCompareVariantBorder2D(image, state, slot, uv,
                                    dx, dy, 0.0, minMip, maxAnisotropy);
}

float4 ckSample3DBorderLevel(CKFFTexture3D image, CKFFSampler state,
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

float4 ckSample3DBorderLod(CKFFTexture3D image, CKFFSampler state,
                           uint slot, float3 uv, float lod, uint modes)
{
    uint width, height, depth, levels;
    image.GetDimensions(0, width, height, depth, levels);
    uint mipFilter = uint(ck_samplerInfo[slot].w) & 15;
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    bool filtered = (lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z) != 1.0;
    bool blend = mipFilter == 2 || mipFilter == 7;
    uint lower = uint(floor(blend ? lod : lod + 0.5)), upper = min(lower + 1, levels - 1);
    float4 result = 0.0;
    [loop] for (uint level = 0; level < (blend ? 2u : 1u); ++level) {
        float4 value = ckSample3DBorderLevel(image, state, slot, uv,
                                             level == 0 ? lower : upper, modes, filtered);
        result = level == 0 ? value : lerp(result, value, frac(lod));
    }
    return result;
}

float4 ckSample3DBias(CKFFTexture3D image, CKFFSampler state, uint slot,
                      float3 uv, float bias, float minMip)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4 && ((modes >> 8) & 15) != 4)
        return image.SampleBias(state, uv, bias);
    float lod = max(image.CalculateLevelOfDetailUnclamped(state, uv) + bias, minMip);
    return ckSample3DAtLod(image, slot, uv, lod, modes);
}

float4 ckSample3D(CKFFTexture3D image, CKFFSampler state, uint slot, float3 uv)
{
    return ckSample3DBias(image, state, slot, uv, 0.0, 0.0);
}

float4 ckSample3DGrad(CKFFTexture3D image, CKFFSampler state, uint slot,
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
