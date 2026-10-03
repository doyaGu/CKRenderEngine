// Face-local filtering. Derivatives use the original direction before any
// addressing; all taps and mips stay on the center sample's selected face.
float2 ckCubeMinor(float3 v, bool x, bool y, float sign)
{
    return x ? float2(-sign * v.z, -v.y) : (y ? float2(v.x, sign * v.z) : float2(sign * v.x, -v.y));
}

float3 ckCubeDirection(float2 uv, bool x, bool y, float sign)
{
    float2 v = uv * 2.0 - 1.0;
    return x ? float3(sign, -v.y, -sign * v.x) : (y ? float3(v.x, sign, sign * v.y) : float3(sign * v.x, -v.y, sign));
}

float4 ckCubeTexel(CKFFTextureCube image, CKFFSampler state, float2 p,
                    float extent, uint mip, bool x, bool y, float sign)
{
    float2 uv = (clamp(p, 0.0, extent - 1.0) + 0.5) / extent;
    return image.SampleLevel(state, ckCubeDirection(uv, x, y, sign), float(mip));
}

float4 ckCubeLevel(CKFFTextureCube image, CKFFSampler state, float2 uv,
                    uint mip, bool filtered, bool x, bool y, float sign)
{
    uint width, height, levels;
    image.GetDimensions(mip, width, height, levels);
    float2 scaled = uv * float(width) - (filtered ? 0.5 : 0.0);
    float2 p = floor(scaled);
    float2 weight = filtered ? frac(scaled) : 0.0;
    float4 a = ckCubeTexel(image, state, p, float(width), mip, x, y, sign);
    if (!filtered) return a;
    float4 b = ckCubeTexel(image, state, p + float2(1,0), float(width), mip, x, y, sign);
    float4 c = ckCubeTexel(image, state, p + float2(0,1), float(width), mip, x, y, sign);
    float4 d = ckCubeTexel(image, state, p + float2(1,1), float(width), mip, x, y, sign);
    return lerp(lerp(a,b,weight.x), lerp(c,d,weight.x), weight.y);
}

float4 ckSampleCubeBias(CKFFTextureCube image, CKFFSampler state, uint slot,
                       float3 direction, float bias)
{
    uint width, height, levels;
    image.GetDimensions(0, width, height, levels);
    float4 info = ck_samplerInfo[slot];
    float3 axes = abs(direction);
    bool x = axes.x > axes.y && axes.x > axes.z;
    bool y = !x && axes.y > axes.z;
    float major = max(max(axes.x, axes.y), axes.z);
    if (levels == 1 && info.y == info.z && info.y != 7.0) {
        // The cheap single-level path cannot change the selected filter/mip.
        float limit = major * (1.0 - 1.0 / float(width));
        float3 inside = clamp(direction, -limit, limit);
        float3 at = float3(x ? direction.x : inside.x, y ? direction.y : inside.y,
                          (!x && !y) ? direction.z : inside.z);
        return image.SampleBias(state, at, bias);
    }
    major = max(major, 1.0e-20);
    float signedMajor = x ? direction.x : (y ? direction.y : direction.z);
    float sign = signedMajor < 0.0 ? -1.0 : 1.0;
    float2 projected = ckCubeMinor(direction, x, y, sign) / major;
    float2 uv = projected * 0.5 + 0.5;
    float3 dx3 = ddx(direction), dy3 = ddy(direction);
    float dxMajor = (x ? dx3.x : (y ? dx3.y : dx3.z)) * sign;
    float dyMajor = (x ? dy3.x : (y ? dy3.y : dy3.z)) * sign;
    float2 dx = (ckCubeMinor(dx3, x, y, sign) - projected * dxMajor) / major * 0.5;
    float2 dy = (ckCubeMinor(dy3, x, y, sign) - projected * dyMajor) / major * 0.5;
    uint packed = uint(info.w);
    float minMip = float((packed >> 12) & 31);
    float lod = max(image.CalculateLevelOfDetailUnclamped(state, direction) + bias, minMip);
    uint filter = uint(lod > 0.0 ? info.y : info.z);
    uint taps = 1;
    float2 step = 0.0;
    if (filter == 7 && lod > 0.0) {
        float lx = length(dx * float(width)), ly = length(dy * float(width));
        float longest = max(lx, ly);
        float tapLimit = clamp(float((packed >> 4) & 255), 1.0, 16.0);
        float minor = max(min(lx, ly), longest / tapLimit);
        taps = uint(clamp(ceil(longest / max(minor, 1.0)), 1.0, tapLimit));
        step = (lx > ly ? dx : dy) / float(taps);
        lod = max(log2(max(minor, 1.0)) + bias, minMip);
    }
    uint mipFilter = packed & 15;
    lod = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(levels - 1));
    bool blend = mipFilter == 2 || mipFilter == 7;
    uint lower = uint(floor(blend ? lod : lod + 0.5));
    uint upper = min(lower + 1, levels - 1);
    float4 sum = 0.0;
    [loop] for (uint tap = 0; tap < taps; ++tap) {
        float2 at = uv + (float(tap) - float(taps - 1) * 0.5) * step;
        float4 color = ckCubeLevel(image, state, at, lower, filter != 1, x, y, sign);
        if (blend) color = lerp(color, ckCubeLevel(image, state, at, upper, filter != 1, x, y, sign), frac(lod));
        sum += color;
    }
    return sum / float(taps);
}
