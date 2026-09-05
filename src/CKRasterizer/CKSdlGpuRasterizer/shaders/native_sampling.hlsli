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
    if (mipFilter != 2) return ckLevel2D(image, slot, uv, uint(floor(lod + 0.5)), filtered, modes);
    uint lower = uint(floor(lod)), upper = min(lower + 1, levels - 1);
    return lerp(ckLevel2D(image, slot, uv, lower, filtered, modes), ckLevel2D(image, slot, uv, upper, filtered, modes), frac(lod));
}

float4 ckSample2D(Texture2D<float4> image, SamplerState state, uint slot, float2 uv)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4) return image.Sample(state, uv);
    uint width, height, levels;
    image.GetDimensions(0, width, height, levels);
    float lod = image.CalculateLevelOfDetailUnclamped(state, uv);
    uint filter = uint(lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z);
    if (filter == 7 && lod > 0.0) {
        float2 dx = ddx(uv), dy = ddy(uv);
        float lx = length(dx * float2(width, height)), ly = length(dy * float2(width, height));
        float major = max(lx, ly), minor = max(min(lx, ly), major / 16.0);
        uint taps = uint(clamp(ceil(major / max(minor, 1.0)), 1.0, 16.0));
        float2 step = (lx > ly ? dx : dy) / float(taps);
        float4 result = 0.0;
        [loop] for (uint i = 0; i < taps; ++i)
            result += ckMips2D(image, slot, uv + (float(i) - float(taps - 1) * 0.5) * step,
                               log2(max(minor, 1.0)), levels, true, modes, uint(ck_samplerInfo[slot].w));
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

float4 ckSample3D(Texture3D<float4> image, SamplerState state, uint slot, float3 uv)
{
    uint modes = uint(ck_samplerInfo[slot].x);
    if ((modes & 15) != 4 && ((modes >> 4) & 15) != 4 && ((modes >> 8) & 15) != 4) return image.Sample(state, uv);
    uint width, height, depth, levels;
    image.GetDimensions(0, width, height, depth, levels);
    float lod = image.CalculateLevelOfDetailUnclamped(state, uv);
    bool filtered = (lod > 0.0 ? ck_samplerInfo[slot].y : ck_samplerInfo[slot].z) != 1.0;
    uint mipFilter = uint(ck_samplerInfo[slot].w);
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    if (mipFilter != 2) return ckLevel3D(image, slot, uv, uint(floor(lod + 0.5)), filtered, modes);
    uint lower = uint(floor(lod)), upper = min(lower + 1, levels - 1);
    return lerp(ckLevel3D(image, slot, uv, lower, filtered, modes), ckLevel3D(image, slot, uv, upper, filtered, modes), frac(lod));
}
