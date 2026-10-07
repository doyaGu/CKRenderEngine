// Fixed-function sampler footprint, LOD, anisotropy and border calculations.

vec4 applyMirrorOnceCoord(vec4 coord, int mirrorOnceMask, int samplerType)
{
    if (samplerType == 1 || mirrorOnceMask == 0) return coord;
    if ((mirrorOnceMask & 1) != 0) coord.x = clamp(abs(coord.x), 0.0, 1.0);
    if ((mirrorOnceMask & 2) != 0) coord.y = clamp(abs(coord.y), 0.0, 1.0);
    if (samplerType == 3 && (mirrorOnceMask & 4) != 0) coord.z = clamp(abs(coord.z), 0.0, 1.0);
    return coord;
}

float ckffClampedLod2D(vec2 dx, vec2 dy, vec2 size, float bias, float minMip)
{
    vec2 footprintX = dx * size;
    vec2 footprintY = dy * size;
    float footprint2 = max(dot(footprintX, footprintX), dot(footprintY, footprintY));
    return max(0.5 * log2(max(footprint2, 1.0e-20)) + bias, minMip);
}

float ckffClampedLod3D(vec3 dx, vec3 dy, vec3 size, float bias, float minMip)
{
    vec3 footprintX = dx * size;
    vec3 footprintY = dy * size;
    float footprint2 = max(dot(footprintX, footprintX), dot(footprintY, footprintY));
    return max(0.5 * log2(max(footprint2, 1.0e-20)) + bias, minMip);
}

vec2 ckffCubeFaceDerivative(vec3 dir, vec3 derivative)
{
    vec3 axis = abs(dir);
    float major, derivativeMajor;
    vec2 other, derivativeOther;
    if (axis.x >= axis.y && axis.x >= axis.z) {
        major = dir.x; derivativeMajor = derivative.x;
        other = dir.yz; derivativeOther = derivative.yz;
    } else if (axis.y >= axis.z) {
        major = dir.y; derivativeMajor = derivative.y;
        other = dir.xz; derivativeOther = derivative.xz;
    } else {
        major = dir.z; derivativeMajor = derivative.z;
        other = dir.xy; derivativeOther = derivative.xy;
    }
    float denominator = max(abs(major), 1.0e-10);
    float signMajor = major < 0.0 ? -1.0 : 1.0;
    return (derivativeOther - other * (signMajor * derivativeMajor / denominator)) /
           denominator;
}

float ckffClampedCubeLod(vec3 dir, vec3 dx, vec3 dy, float size,
                         float bias, float minMip)
{
    vec2 faceDx = ckffCubeFaceDerivative(dir, dx);
    vec2 faceDy = ckffCubeFaceDerivative(dir, dy);
    return ckffClampedLod2D(faceDx, faceDy, vec2_splat(size * 0.5), bias, minMip);
}

vec3 ckffAnisoPlan(float footprintX, float footprintY, float maxAnisotropy,
                   float bias, float minMip)
{
    float major = max(footprintX, footprintY);
    float minor = min(footprintX, footprintY);
    float count = major <= 1.0 ? 1.0 :
        min(maxAnisotropy, max(1.0, ceil(major / max(minor, 0.0001))));
    float lod = max(log2(max(max(major / count, minor), 0.000001)) + bias, minMip);
    return vec3(count, lod, footprintX >= footprintY ? 0.0 : 1.0);
}

vec4 ckffNative3DAniso(CKFFTexture3D image, CKFFSampler state, uint slot,
                       vec3 uv, vec3 dx, vec3 dy, float bias,
                       float minMip, float maxAnisotropy)
{
    uint width, height, depth, levels;
    image.GetDimensions(0, width, height, depth, levels);
    vec3 size = vec3(width, height, depth);
    vec3 plan = ckffAnisoPlan(length(dx * size), length(dy * size),
                              maxAnisotropy, bias, minMip);
    vec3 step = plan.z < 0.5 ? dx : dy;
    uint modes = uint(ck_samplerInfo[slot].x);
    bool border = (modes & 15) == 4 || ((modes >> 4) & 15) == 4 ||
                  ((modes >> 8) & 15) == 4;
    vec4 color = vec4_splat(0.0);
    for (int tap = 0; tap < int(plan.x); ++tap) {
        vec3 tapUv = uv + step * ((float(tap) + 0.5) / plan.x - 0.5);
        color += border ? ckSample3DBorderLod(image, state, slot, tapUv,
                                               plan.y, modes) :
                          image.SampleLevel(state, tapUv, plan.y);
    }
    return color / plan.x;
}
vec4 ckffNative3DSample(CKFFTexture3D image, CKFFSampler state, uint slot,
                        vec3 uv, vec3 originalUv, vec3 dx, vec3 dy,
                        int mirrorOnceMask, float bias, float minMip,
                        float maxAnisotropy)
{
    if (maxAnisotropy > 1.0)
        return ckffNative3DAniso(image, state, slot, uv, dx, dy, bias,
                                 minMip, maxAnisotropy);
    return ckSample3DGrad(image, state, slot, uv, originalUv,
                          mirrorOnceMask, bias, minMip);
}
#define CKFF_TEXTURE_3D_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffNative3DAniso(_sampler, _sampler##Sampler, _sampler##Slot, \
                      _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_2D_BIAS(_sampler, _uv, _bias) texture2DBias(_sampler, _uv, _bias, minMip, maxAnisotropy)
#define CKFF_TEXTURE_CUBE_BIAS(_sampler, _uv, _bias) textureCubeBias(_sampler, _uv, _bias)
#define CKFF_TEXTURE_3D_BIAS(_sampler, _uv, _bias) texture3DBias(_sampler, _uv, _bias, minMip)
#define CKFF_TEXTURE_3D_GRAD(_sampler, _uv, _original, _dx, _dy, _mirror, _bias) \
    texture3DGrad(_sampler, _uv, _original, _mirror, _bias, minMip)

