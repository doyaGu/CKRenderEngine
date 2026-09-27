$input v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos, v_clipDistance0, v_clipDistance1

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"
#include "ff_sampler_shader_state.sh"

uniform vec4 u_ffDrawParams[20];
uniform vec4 u_bumpEnv[16];
uniform vec4 u_stageParams[16];
uniform vec4 u_borderColor[8];
uniform vec4 u_borderSampler[16];
uniform vec4 u_ffProgram[5];

// Three sixteen-slot layouts cover every combination of eight logical stages.
// 0 = 8x2D + 4xcube + 4xvolume, 1 = 4x2D + 8xcube + 4xvolume,
// 2 = 4x2D + 4xcube + 8xvolume.
#ifndef CKFF_NATIVE_SAMPLER_LAYOUT
#define CKFF_NATIVE_SAMPLER_LAYOUT 0
#endif

SAMPLER2D(s_texture0, 0);
SAMPLER2D(s_texture1, 1);
SAMPLER2D(s_texture2, 2);
SAMPLER2D(s_texture3, 3);
#if CKFF_NATIVE_SAMPLER_LAYOUT == 0
SAMPLER2D(s_texture4, 4);
SAMPLER2D(s_texture5, 5);
SAMPLER2D(s_texture6, 6);
SAMPLER2D(s_texture7, 7);
SAMPLERCUBE(s_textureCube0, 8);
SAMPLERCUBE(s_textureCube1, 9);
SAMPLERCUBE(s_textureCube2, 10);
SAMPLERCUBE(s_textureCube3, 11);
SAMPLER3D(s_textureVolume0, 12);
SAMPLER3D(s_textureVolume1, 13);
SAMPLER3D(s_textureVolume2, 14);
SAMPLER3D(s_textureVolume3, 15);
#elif CKFF_NATIVE_SAMPLER_LAYOUT == 1
SAMPLERCUBE(s_textureCube0, 4);
SAMPLERCUBE(s_textureCube1, 5);
SAMPLERCUBE(s_textureCube2, 6);
SAMPLERCUBE(s_textureCube3, 7);
SAMPLERCUBE(s_textureCube4, 8);
SAMPLERCUBE(s_textureCube5, 9);
SAMPLERCUBE(s_textureCube6, 10);
SAMPLERCUBE(s_textureCube7, 11);
SAMPLER3D(s_textureVolume0, 12);
SAMPLER3D(s_textureVolume1, 13);
SAMPLER3D(s_textureVolume2, 14);
SAMPLER3D(s_textureVolume3, 15);
#else
SAMPLERCUBE(s_textureCube0, 4);
SAMPLERCUBE(s_textureCube1, 5);
SAMPLERCUBE(s_textureCube2, 6);
SAMPLERCUBE(s_textureCube3, 7);
#if CKFF_NATIVE_SDL_GPU && !defined(__spirv__)
Texture3D<float4> s_textureVolume[8] : register(t8, space2);
SamplerState s_textureVolumeSampler[8] : register(s8, space2);
#else
SAMPLER3D(s_textureVolume0, 8);
SAMPLER3D(s_textureVolume1, 9);
SAMPLER3D(s_textureVolume2, 10);
SAMPLER3D(s_textureVolume3, 11);
SAMPLER3D(s_textureVolume4, 12);
SAMPLER3D(s_textureVolume5, 13);
SAMPLER3D(s_textureVolume6, 14);
SAMPLER3D(s_textureVolume7, 15);
#endif
#endif

#if CKFF_NATIVE_SAMPLER_LAYOUT == 0
#define CKFF_CUBE_SLOT_BASE 8
#define CKFF_VOLUME_SLOT_BASE 12
#elif CKFF_NATIVE_SAMPLER_LAYOUT == 1
#define CKFF_CUBE_SLOT_BASE 4
#define CKFF_VOLUME_SLOT_BASE 12
#else
#define CKFF_CUBE_SLOT_BASE 4
#define CKFF_VOLUME_SLOT_BASE 8
#endif

#include "fs_ff_common.sc"

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

#if BGFX_SHADER_LANGUAGE_GLSL
#define CKFF_TEXTURE_2D_BIAS(_sampler, _uv, _bias) texture2DBias(_sampler, _uv, _bias)
#define CKFF_TEXTURE_CUBE_BIAS(_sampler, _uv, _bias) textureCubeBias(_sampler, _uv, _bias)
#define CKFF_TEXTURE_3D_BIAS(_sampler, _uv, _bias) texture(_sampler, _uv, _bias)
#define CKFF_TEXTURE_2D_MIN_MIP(_sampler, _uv, _dx, _dy, _bias, _min) \
    textureLod(_sampler, _uv, ckffClampedLod2D(_dx, _dy, vec2(textureSize(_sampler, 0)), _bias, _min))
#define CKFF_TEXTURE_CUBE_MIN_MIP(_sampler, _uv, _dx, _dy, _bias, _min) \
    textureLod(_sampler, _uv, ckffClampedCubeLod(_uv, _dx, _dy, float(textureSize(_sampler, 0).x), _bias, _min))
#define CKFF_TEXTURE_3D_MIN_MIP(_sampler, _uv, _dx, _dy, _bias, _min) \
    textureLod(_sampler, _uv, ckffClampedLod3D(_dx, _dy, vec3(textureSize(_sampler, 0)), _bias, _min))
vec4 ckffAniso2D(sampler2D image, vec2 uv, vec2 dx, vec2 dy,
                 float bias, float minMip, float maxAnisotropy)
{
    vec2 size = vec2(textureSize(image, 0));
    vec3 plan = ckffAnisoPlan(length(dx * size), length(dy * size),
                              maxAnisotropy, bias, minMip);
    vec2 step = plan.z < 0.5 ? dx : dy;
    vec4 color = vec4_splat(0.0);
    for (int tap = 0; tap < int(plan.x); ++tap)
        color += textureLod(image, uv + step * ((float(tap) + 0.5) / plan.x - 0.5), plan.y);
    return color / plan.x;
}
vec4 ckffAnisoCube(samplerCube image, vec3 uv, vec3 dx, vec3 dy,
                   float bias, float minMip, float maxAnisotropy)
{
    float size = float(textureSize(image, 0).x) * 0.5;
    vec3 plan = ckffAnisoPlan(length(ckffCubeFaceDerivative(uv, dx)) * size,
                              length(ckffCubeFaceDerivative(uv, dy)) * size,
                              maxAnisotropy, bias, minMip);
    vec3 step = plan.z < 0.5 ? dx : dy;
    vec4 color = vec4_splat(0.0);
    for (int tap = 0; tap < int(plan.x); ++tap)
        color += textureLod(image, uv + step * ((float(tap) + 0.5) / plan.x - 0.5), plan.y);
    return color / plan.x;
}
vec4 ckffAniso3D(sampler3D image, vec3 uv, vec3 dx, vec3 dy,
                 float bias, float minMip, float maxAnisotropy)
{
    vec3 size = vec3(textureSize(image, 0));
    vec3 plan = ckffAnisoPlan(length(dx * size), length(dy * size),
                              maxAnisotropy, bias, minMip);
    vec3 step = plan.z < 0.5 ? dx : dy;
    vec4 color = vec4_splat(0.0);
    for (int tap = 0; tap < int(plan.x); ++tap)
        color += textureLod(image, uv + step * ((float(tap) + 0.5) / plan.x - 0.5), plan.y);
    return color / plan.x;
}
#define CKFF_TEXTURE_2D_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffAniso2D(_sampler, _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_CUBE_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffAnisoCube(_sampler, _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_3D_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffAniso3D(_sampler, _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_3D_GRAD(_sampler, _uv, _original, _dx, _dy, _mirror, _bias) \
    (_mirror != 0 ? textureGrad(_sampler, _uv, _dx * exp2(_bias), _dy * exp2(_bias)) : CKFF_TEXTURE_3D_BIAS(_sampler, _uv, _bias))
#elif !CKFF_NATIVE_SDL_GPU
vec4 ckffTexture2DBias(BgfxSampler2D sampleState, vec2 uv, float bias)
{
    return sampleState.m_texture.SampleBias(sampleState.m_sampler, uv, bias);
}
vec4 ckffTextureCubeBias(BgfxSamplerCube sampleState, vec3 uv, float bias)
{
    return sampleState.m_texture.SampleBias(sampleState.m_sampler, uv, bias);
}
vec4 ckffTexture3DBias(BgfxSampler3D sampleState, vec3 uv, float bias)
{
    return sampleState.m_texture.SampleBias(sampleState.m_sampler, uv, bias);
}
vec4 ckffTexture3DGrad(BgfxSampler3D sampleState, vec3 uv, vec3 dx, vec3 dy)
{
    return sampleState.m_texture.SampleGrad(sampleState.m_sampler, uv, dx, dy);
}
vec4 ckffTexture2DMinMip(BgfxSampler2D sampleState, vec2 uv, vec2 dx, vec2 dy,
                         float bias, float minMip)
{
    uint width, height, levels;
    sampleState.m_texture.GetDimensions(0, width, height, levels);
    float lod = ckffClampedLod2D(dx, dy, vec2(width, height), bias, minMip);
    return sampleState.m_texture.SampleLevel(sampleState.m_sampler, uv, lod);
}
vec4 ckffTextureCubeMinMip(BgfxSamplerCube sampleState, vec3 uv, vec3 dx, vec3 dy,
                           float bias, float minMip)
{
    uint width, height, levels;
    sampleState.m_texture.GetDimensions(0, width, height, levels);
    float lod = ckffClampedCubeLod(uv, dx, dy, float(width), bias, minMip);
    return sampleState.m_texture.SampleLevel(sampleState.m_sampler, uv, lod);
}
vec4 ckffTexture3DMinMip(BgfxSampler3D sampleState, vec3 uv, vec3 dx, vec3 dy,
                         float bias, float minMip)
{
    uint width, height, depth, levels;
    sampleState.m_texture.GetDimensions(0, width, height, depth, levels);
    float lod = ckffClampedLod3D(dx, dy, vec3(width, height, depth), bias, minMip);
    return sampleState.m_texture.SampleLevel(sampleState.m_sampler, uv, lod);
}
vec4 ckffTexture2DAniso(BgfxSampler2D sampleState, vec2 uv, vec2 dx, vec2 dy,
                        float bias, float minMip, float maxAnisotropy)
{
    uint width, height, levels;
    sampleState.m_texture.GetDimensions(0, width, height, levels);
    vec3 plan = ckffAnisoPlan(length(dx * vec2(width, height)),
                              length(dy * vec2(width, height)),
                              maxAnisotropy, bias, minMip);
    vec2 step = plan.z < 0.5 ? dx : dy;
    vec4 color = vec4_splat(0.0);
    for (int tap = 0; tap < int(plan.x); ++tap)
        color += sampleState.m_texture.SampleLevel(sampleState.m_sampler,
            uv + step * ((float(tap) + 0.5) / plan.x - 0.5), plan.y);
    return color / plan.x;
}
vec4 ckffTextureCubeAniso(BgfxSamplerCube sampleState, vec3 uv, vec3 dx, vec3 dy,
                          float bias, float minMip, float maxAnisotropy)
{
    uint width, height, levels;
    sampleState.m_texture.GetDimensions(0, width, height, levels);
    float size = float(width) * 0.5;
    vec3 plan = ckffAnisoPlan(length(ckffCubeFaceDerivative(uv, dx)) * size,
                              length(ckffCubeFaceDerivative(uv, dy)) * size,
                              maxAnisotropy, bias, minMip);
    vec3 step = plan.z < 0.5 ? dx : dy;
    vec4 color = vec4_splat(0.0);
    for (int tap = 0; tap < int(plan.x); ++tap)
        color += sampleState.m_texture.SampleLevel(sampleState.m_sampler,
            uv + step * ((float(tap) + 0.5) / plan.x - 0.5), plan.y);
    return color / plan.x;
}
vec4 ckffTexture3DAniso(BgfxSampler3D sampleState, vec3 uv, vec3 dx, vec3 dy,
                        float bias, float minMip, float maxAnisotropy)
{
    uint width, height, depth, levels;
    sampleState.m_texture.GetDimensions(0, width, height, depth, levels);
    vec3 size = vec3(width, height, depth);
    vec3 plan = ckffAnisoPlan(length(dx * size), length(dy * size),
                              maxAnisotropy, bias, minMip);
    vec3 step = plan.z < 0.5 ? dx : dy;
    vec4 color = vec4_splat(0.0);
    for (int tap = 0; tap < int(plan.x); ++tap)
        color += sampleState.m_texture.SampleLevel(sampleState.m_sampler,
            uv + step * ((float(tap) + 0.5) / plan.x - 0.5), plan.y);
    return color / plan.x;
}
#define CKFF_TEXTURE_2D_BIAS(_sampler, _uv, _bias) ckffTexture2DBias(_sampler, _uv, _bias)
#define CKFF_TEXTURE_CUBE_BIAS(_sampler, _uv, _bias) ckffTextureCubeBias(_sampler, _uv, _bias)
#define CKFF_TEXTURE_3D_BIAS(_sampler, _uv, _bias) ckffTexture3DBias(_sampler, _uv, _bias)
#define CKFF_TEXTURE_2D_MIN_MIP(_sampler, _uv, _dx, _dy, _bias, _min) \
    ckffTexture2DMinMip(_sampler, _uv, _dx, _dy, _bias, _min)
#define CKFF_TEXTURE_CUBE_MIN_MIP(_sampler, _uv, _dx, _dy, _bias, _min) \
    ckffTextureCubeMinMip(_sampler, _uv, _dx, _dy, _bias, _min)
#define CKFF_TEXTURE_3D_MIN_MIP(_sampler, _uv, _dx, _dy, _bias, _min) \
    ckffTexture3DMinMip(_sampler, _uv, _dx, _dy, _bias, _min)
#define CKFF_TEXTURE_2D_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffTexture2DAniso(_sampler, _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_CUBE_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffTextureCubeAniso(_sampler, _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_3D_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffTexture3DAniso(_sampler, _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_3D_GRAD(_sampler, _uv, _original, _dx, _dy, _mirror, _bias) \
    (_mirror != 0 ? ckffTexture3DGrad(_sampler, _uv, _dx * exp2(_bias), _dy * exp2(_bias)) : CKFF_TEXTURE_3D_BIAS(_sampler, _uv, _bias))
#else
vec4 ckffNative3DAniso(Texture3D<float4> image, SamplerState state, uint slot,
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
vec4 ckffNative3DSample(Texture3D<float4> image, SamplerState state, uint slot,
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
#endif

// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
float ckffBorderAxisCoverage(float uv, float extent, bool border, bool filtered)
{
    if (!border) return 1.0;
    if (!filtered) return uv >= 0.0 && uv < 1.0 ? 1.0 : 0.0;
    float coordinate = uv * extent - 0.5;
    float base = floor(coordinate);
    float fraction = coordinate - base;
    return (base >= 0.0 && base < extent ? 1.0 - fraction : 0.0) +
           (base + 1.0 >= 0.0 && base + 1.0 < extent ? fraction : 0.0);
}

float ckffBorderCoverage2D(vec2 uv, vec2 size, int mask, bool filtered)
{
    return ckffBorderAxisCoverage(uv.x, size.x, (mask & 1) != 0, filtered) *
           ckffBorderAxisCoverage(uv.y, size.y, (mask & 2) != 0, filtered);
}

float ckffBorderCoverage3D(vec3 uv, vec3 size, int mask, bool filtered)
{
    return ckffBorderCoverage2D(uv.xy, size.xy, mask, filtered) *
           ckffBorderAxisCoverage(uv.z, size.z, (mask & 4) != 0, filtered);
}

#if BGFX_SHADER_LANGUAGE_GLSL
#define CKFF_BORDER_SAMPLER_2D sampler2D
#define CKFF_BORDER_SAMPLER_3D sampler3D
#define CKFF_BORDER_SIZE_2D(_sampler) vec2(textureSize(_sampler, 0))
#define CKFF_BORDER_SIZE_3D(_sampler) vec3(textureSize(_sampler, 0))
#define CKFF_BORDER_SIZE_2D_LEVEL(_sampler, _mip) vec2(textureSize(_sampler, _mip))
#define CKFF_BORDER_SIZE_3D_LEVEL(_sampler, _mip) vec3(textureSize(_sampler, _mip))
#define CKFF_BORDER_SAMPLE_2D(_sampler, _uv, _mip) textureLod(_sampler, _uv, float(_mip))
#define CKFF_BORDER_SAMPLE_3D(_sampler, _uv, _mip) textureLod(_sampler, _uv, float(_mip))
#else
#define CKFF_BORDER_SAMPLER_2D BgfxSampler2D
#define CKFF_BORDER_SAMPLER_3D BgfxSampler3D
vec2 ckffBorderSize2D(BgfxSampler2D sampleState)
{
    uint width, height, levels;
    sampleState.m_texture.GetDimensions(0, width, height, levels);
    return vec2(width, height);
}
vec3 ckffBorderSize3D(BgfxSampler3D sampleState)
{
    uint width, height, depth, levels;
    sampleState.m_texture.GetDimensions(0, width, height, depth, levels);
    return vec3(width, height, depth);
}
vec2 ckffBorderSize2DLevel(BgfxSampler2D sampleState, int mip)
{
    uint width, height, levels;
    sampleState.m_texture.GetDimensions(uint(mip), width, height, levels);
    return vec2(width, height);
}
vec3 ckffBorderSize3DLevel(BgfxSampler3D sampleState, int mip)
{
    uint width, height, depth, levels;
    sampleState.m_texture.GetDimensions(uint(mip), width, height, depth, levels);
    return vec3(width, height, depth);
}
#define CKFF_BORDER_SIZE_2D(_sampler) ckffBorderSize2D(_sampler)
#define CKFF_BORDER_SIZE_3D(_sampler) ckffBorderSize3D(_sampler)
#define CKFF_BORDER_SIZE_2D_LEVEL(_sampler, _mip) ckffBorderSize2DLevel(_sampler, _mip)
#define CKFF_BORDER_SIZE_3D_LEVEL(_sampler, _mip) ckffBorderSize3DLevel(_sampler, _mip)
#define CKFF_BORDER_SAMPLE_2D(_sampler, _uv, _mip) \
    (_sampler).m_texture.SampleLevel((_sampler).m_sampler, _uv, float(_mip))
#define CKFF_BORDER_SAMPLE_3D(_sampler, _uv, _mip) \
    (_sampler).m_texture.SampleLevel((_sampler).m_sampler, _uv, float(_mip))
#endif

vec4 ckffBorderLevel2D(CKFF_BORDER_SAMPLER_2D image, vec2 uv, int mip,
                        bool filtered, int mask, vec4 border)
{
    vec2 size = CKFF_BORDER_SIZE_2D_LEVEL(image, mip);
    vec2 sampleUv = filtered ? uv : (floor(uv * size) + vec2_splat(0.5)) / size;
    return mix(border, CKFF_BORDER_SAMPLE_2D(image, sampleUv, mip),
               ckffBorderCoverage2D(uv, size, mask, filtered));
}

vec4 ckffBorderLevel3D(CKFF_BORDER_SAMPLER_3D image, vec3 uv, int mip,
                        bool filtered, int mask, vec4 border)
{
    vec3 size = CKFF_BORDER_SIZE_3D_LEVEL(image, mip);
    vec3 sampleUv = filtered ? uv : (floor(uv * size) + vec3_splat(0.5)) / size;
    return mix(border, CKFF_BORDER_SAMPLE_3D(image, sampleUv, mip),
               ckffBorderCoverage3D(uv, size, mask, filtered));
}

bool ckffBorderLinearMips(int filterMode)
{
    return filterMode == 2 || filterMode == 4 || filterMode == 6 || filterMode == 7;
}

vec4 ckffBorderMips2D(CKFF_BORDER_SAMPLER_2D image, vec2 uv, float lod,
                       int mipCount, int mipFilter, bool filtered, int mask, vec4 border)
{
    float selected = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(mipCount - 1));
    if (!ckffBorderLinearMips(mipFilter))
        return ckffBorderLevel2D(image, uv, int(floor(selected + 0.5)), filtered, mask, border);
    int lower = int(floor(selected));
    int upper = min(lower + 1, mipCount - 1);
    return mix(ckffBorderLevel2D(image, uv, lower, filtered, mask, border),
               ckffBorderLevel2D(image, uv, upper, filtered, mask, border),
               selected - float(lower));
}

vec4 ckffBorderMips3D(CKFF_BORDER_SAMPLER_3D image, vec3 uv, float lod,
                       int mipCount, int mipFilter, bool filtered, int mask, vec4 border)
{
    float selected = mipFilter == 0 ? 0.0 : clamp(lod, 0.0, float(mipCount - 1));
    if (!ckffBorderLinearMips(mipFilter))
        return ckffBorderLevel3D(image, uv, int(floor(selected + 0.5)), filtered, mask, border);
    int lower = int(floor(selected));
    int upper = min(lower + 1, mipCount - 1);
    return mix(ckffBorderLevel3D(image, uv, lower, filtered, mask, border),
               ckffBorderLevel3D(image, uv, upper, filtered, mask, border),
               selected - float(lower));
}

vec4 ckffBorderSample2D(CKFF_BORDER_SAMPLER_2D image, vec2 uv, vec2 dx, vec2 dy,
                         float bias, float minMip, float maxAnisotropy,
                         int stage, int slot, int mask,
                         bool minLinear, bool magLinear)
{
    vec2 size = CKFF_BORDER_SIZE_2D(image);
    int mipCount = max(1, int(u_borderSampler[slot].x));
    int mipFilter = int(u_borderSampler[slot].y);
    float lod = ckffClampedLod2D(dx, dy, size, bias, minMip);
    vec4 border = u_borderColor[stage];
    if (maxAnisotropy > 1.0) {
        vec3 plan = ckffAnisoPlan(length(dx * size), length(dy * size),
                                  maxAnisotropy, bias, minMip);
        vec2 step = plan.z < 0.5 ? dx : dy;
        vec4 color = vec4_splat(0.0);
        for (int tap = 0; tap < int(plan.x); ++tap)
            color += ckffBorderMips2D(image,
                uv + step * ((float(tap) + 0.5) / plan.x - 0.5),
                plan.y, mipCount, mipFilter, lod > 0.0 ? minLinear : magLinear,
                mask, border);
        return color / plan.x;
    }
    return ckffBorderMips2D(image, uv, lod, mipCount, mipFilter,
                             lod > 0.0 ? minLinear : magLinear, mask, border);
}

vec4 ckffBorderSample3D(CKFF_BORDER_SAMPLER_3D image, vec3 uv, vec3 dx, vec3 dy,
                         float bias, float minMip, float maxAnisotropy,
                         int stage, int slot, int mask, bool minLinear, bool magLinear)
{
    vec3 size = CKFF_BORDER_SIZE_3D(image);
    int mipCount = max(1, int(u_borderSampler[slot].x));
    int mipFilter = int(u_borderSampler[slot].y);
    float lod = ckffClampedLod3D(dx, dy, size, bias, minMip);
    vec4 border = u_borderColor[stage];
    if (maxAnisotropy > 1.0) {
        vec3 plan = ckffAnisoPlan(length(dx * size), length(dy * size),
                                  maxAnisotropy, bias, minMip);
        vec3 step = plan.z < 0.5 ? dx : dy;
        vec4 color = vec4_splat(0.0);
        for (int tap = 0; tap < int(plan.x); ++tap)
            color += ckffBorderMips3D(image,
                uv + step * ((float(tap) + 0.5) / plan.x - 0.5),
                plan.y, mipCount, mipFilter, lod > 0.0 ? minLinear : magLinear,
                mask, border);
        return color / plan.x;
    }
    return ckffBorderMips3D(image, uv, lod, mipCount, mipFilter,
                             lod > 0.0 ? minLinear : magLinear, mask, border);
}

vec2 ckffCompareSize2D(int ordinal, int mip)
{
    if (ordinal == 0) return CKFF_BORDER_SIZE_2D_LEVEL(s_texture0, mip);
    if (ordinal == 1) return CKFF_BORDER_SIZE_2D_LEVEL(s_texture1, mip);
    if (ordinal == 2) return CKFF_BORDER_SIZE_2D_LEVEL(s_texture2, mip);
#if CKFF_NATIVE_SAMPLER_LAYOUT != 0
    return CKFF_BORDER_SIZE_2D_LEVEL(s_texture3, mip);
#else
    if (ordinal == 3) return CKFF_BORDER_SIZE_2D_LEVEL(s_texture3, mip);
    if (ordinal == 4) return CKFF_BORDER_SIZE_2D_LEVEL(s_texture4, mip);
    if (ordinal == 5) return CKFF_BORDER_SIZE_2D_LEVEL(s_texture5, mip);
    if (ordinal == 6) return CKFF_BORDER_SIZE_2D_LEVEL(s_texture6, mip);
    return CKFF_BORDER_SIZE_2D_LEVEL(s_texture7, mip);
#endif
}

float ckffCompareDepth2D(int ordinal, vec2 uv, int mip)
{
    if (ordinal == 0) return CKFF_BORDER_SAMPLE_2D(s_texture0, uv, mip).r;
    if (ordinal == 1) return CKFF_BORDER_SAMPLE_2D(s_texture1, uv, mip).r;
    if (ordinal == 2) return CKFF_BORDER_SAMPLE_2D(s_texture2, uv, mip).r;
#if CKFF_NATIVE_SAMPLER_LAYOUT != 0
    return CKFF_BORDER_SAMPLE_2D(s_texture3, uv, mip).r;
#else
    if (ordinal == 3) return CKFF_BORDER_SAMPLE_2D(s_texture3, uv, mip).r;
    if (ordinal == 4) return CKFF_BORDER_SAMPLE_2D(s_texture4, uv, mip).r;
    if (ordinal == 5) return CKFF_BORDER_SAMPLE_2D(s_texture5, uv, mip).r;
    if (ordinal == 6) return CKFF_BORDER_SAMPLE_2D(s_texture6, uv, mip).r;
    return CKFF_BORDER_SAMPLE_2D(s_texture7, uv, mip).r;
#endif
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

vec4 getTextureColor(int stage, vec4 coord, int samplerType, int compareFunc,
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
    // Addressing must not change the derivatives used to choose a mip level.
    // In particular, clamping the coordinate outside [0, 1] would otherwise
    // force the LOD to zero instead of preserving the source footprint.
    vec2 originalDx = dFdx(coord.xy);
    vec2 originalDy = dFdy(coord.xy);
    vec3 originalCoord3 = coord.xyz;
    vec3 originalDx3 = dFdx(coord.xyz);
    vec3 originalDy3 = dFdy(coord.xyz);
    coord = applyMirrorOnceCoord(coord, mirrorOnceMask, samplerType);
// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
    // bgfx has only sixteen border palette entries for the whole frame.
    // The shader computes the coverage of texels inside each border axis.
    bool borderMip = manualBorder &&
        (u_borderSampler[samplerOrdinal].y > 0.5 || manualAnisotropy);
#endif
// CKFF_BGFX_ONLY_END
    if (samplerType == 1) {
        int ordinal = samplerOrdinal;
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_CUBE(_sampler) CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)
#else
#define CKFF_SAMPLE_CUBE(_sampler) (manualAnisotropy ? \
    CKFF_TEXTURE_CUBE_ANISO(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip, maxAnisotropy) : \
    (manualLod ? CKFF_TEXTURE_CUBE_MIN_MIP(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip) : \
    CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)))
#endif
        if (ordinal == 0) return CKFF_SAMPLE_CUBE(s_textureCube0);
        if (ordinal == 1) return CKFF_SAMPLE_CUBE(s_textureCube1);
        if (ordinal == 2) return CKFF_SAMPLE_CUBE(s_textureCube2);
#if CKFF_NATIVE_SAMPLER_LAYOUT == 1
        if (ordinal == 3) return CKFF_SAMPLE_CUBE(s_textureCube3);
        if (ordinal == 4) return CKFF_SAMPLE_CUBE(s_textureCube4);
        if (ordinal == 5) return CKFF_SAMPLE_CUBE(s_textureCube5);
        if (ordinal == 6) return CKFF_SAMPLE_CUBE(s_textureCube6);
        return CKFF_SAMPLE_CUBE(s_textureCube7);
#else
        return CKFF_SAMPLE_CUBE(s_textureCube3);
#endif
#undef CKFF_SAMPLE_CUBE
    }
    if (samplerType == 3) {
        int ordinal = samplerOrdinal;
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
#if CKFF_NATIVE_SAMPLER_LAYOUT == 2 && !defined(__spirv__)
        return ckffNative3DSample(s_textureVolume[ordinal],
                                  s_textureVolumeSampler[ordinal],
                                  uint(CKFF_VOLUME_SLOT_BASE + ordinal),
                                  coord.xyz, originalCoord3, originalDx3,
                                  originalDy3, mirrorOnceMask, lodBias, minMip,
                                  maxAnisotropy);
#else
        if (ordinal == 0) return CKFF_SAMPLE_3D(s_textureVolume0);
        if (ordinal == 1) return CKFF_SAMPLE_3D(s_textureVolume1);
        if (ordinal == 2) return CKFF_SAMPLE_3D(s_textureVolume2);
#if CKFF_NATIVE_SAMPLER_LAYOUT == 2
        if (ordinal == 3) return CKFF_SAMPLE_3D(s_textureVolume3);
        if (ordinal == 4) return CKFF_SAMPLE_3D(s_textureVolume4);
        if (ordinal == 5) return CKFF_SAMPLE_3D(s_textureVolume5);
        if (ordinal == 6) return CKFF_SAMPLE_3D(s_textureVolume6);
        return CKFF_SAMPLE_3D(s_textureVolume7);
#else
        return CKFF_SAMPLE_3D(s_textureVolume3);
#endif
#endif
#else
// CKFF_BGFX_ONLY_BEGIN
        vec4 volumeColor;
        bool volumeBorderMip = manualBorder &&
            (u_borderSampler[CKFF_VOLUME_SLOT_BASE + ordinal].y > 0.5 ||
             manualAnisotropy);
#define CKFF_SAMPLE_VOLUME(_sampler) (volumeBorderMip ? \
    ckffBorderSample3D(_sampler, coord.xyz, originalDx3, originalDy3, \
        lodBias, minMip, maxAnisotropy, stage, CKFF_VOLUME_SLOT_BASE + ordinal, borderMask, minLinear, magLinear) : \
    CKFF_SAMPLE_3D(_sampler))
        if (ordinal == 0) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume0);
        else if (ordinal == 1) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume1);
        else if (ordinal == 2) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume2);
#if CKFF_NATIVE_SAMPLER_LAYOUT == 2
        else if (ordinal == 3) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume3);
        else if (ordinal == 4) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume4);
        else if (ordinal == 5) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume5);
        else if (ordinal == 6) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume6);
        else volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume7);
#else
        else volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume3);
#endif
#undef CKFF_SAMPLE_VOLUME
        if (manualBorder && !volumeBorderMip) {
            vec3 size;
            if (ordinal == 0) size = CKFF_BORDER_SIZE_3D(s_textureVolume0);
            else if (ordinal == 1) size = CKFF_BORDER_SIZE_3D(s_textureVolume1);
            else if (ordinal == 2) size = CKFF_BORDER_SIZE_3D(s_textureVolume2);
#if CKFF_NATIVE_SAMPLER_LAYOUT == 2
            else if (ordinal == 3) size = CKFF_BORDER_SIZE_3D(s_textureVolume3);
            else if (ordinal == 4) size = CKFF_BORDER_SIZE_3D(s_textureVolume4);
            else if (ordinal == 5) size = CKFF_BORDER_SIZE_3D(s_textureVolume5);
            else if (ordinal == 6) size = CKFF_BORDER_SIZE_3D(s_textureVolume6);
            else size = CKFF_BORDER_SIZE_3D(s_textureVolume7);
#else
            else size = CKFF_BORDER_SIZE_3D(s_textureVolume3);
#endif
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
    vec4 color = vec4_splat(0.0);
    int ordinal = samplerOrdinal;

#if !CKFF_NATIVE_SDL_GPU || CKFF_NATIVE_COMPARE_COUNT > 0
    if (samplerType == 2 && compareFunc != 0) {
#if CKFF_NATIVE_SDL_GPU
#if CKFF_NATIVE_COMPARE_COUNT == 1
        float compared = texture2DCompare(s_texture0, uv, originalDx, originalDy,
            lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#else
        float compared = 0.0;
        if (ordinal == 0) compared = texture2DCompare(s_texture0, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#if CKFF_NATIVE_COMPARE_COUNT > 1
        else if (ordinal == 1) compared = texture2DCompare(s_texture1, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#endif
#if CKFF_NATIVE_COMPARE_COUNT > 2
        else if (ordinal == 2) compared = texture2DCompare(s_texture2, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#endif
#if CKFF_NATIVE_COMPARE_COUNT > 3
        else if (ordinal == 3) compared = texture2DCompare(s_texture3, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#endif
#if CKFF_NATIVE_COMPARE_COUNT > 4
        else if (ordinal == 4) compared = texture2DCompare(s_texture4, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#endif
#if CKFF_NATIVE_COMPARE_COUNT > 5
        else if (ordinal == 5) compared = texture2DCompare(s_texture5, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#endif
#if CKFF_NATIVE_COMPARE_COUNT > 6
        else if (ordinal == 6) compared = texture2DCompare(s_texture6, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#endif
#if CKFF_NATIVE_COMPARE_COUNT > 7
        else compared = texture2DCompare(s_texture7, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy, coord.z, compareFunc);
#endif
#endif
#else
// CKFF_BGFX_ONLY_BEGIN
        float compared = manualDepthCompare
            ? ckffCompareSample2D(ordinal, uv, originalDx, originalDy,
                lodBias, minMip, maxAnisotropy, stage, borderMask, minLinear,
                magLinear, coord.z, compareFunc)
            : color.r;
// CKFF_BGFX_ONLY_END
#endif
        return vec4_splat(compared);
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
#if CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_SAMPLER_LAYOUT != 0
#if CKFF_NATIVE_COMPARE_COUNT == 0
    if (ordinal == 0) color = CKFF_SAMPLE_2D_FINAL(s_texture0);
    else if (ordinal == 1) color = CKFF_SAMPLE_2D_FINAL(s_texture1);
    else if (ordinal == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
    else color = CKFF_SAMPLE_2D_FINAL(s_texture3);
#elif CKFF_NATIVE_COMPARE_COUNT == 1
    if (ordinal == 1) color = CKFF_SAMPLE_2D_FINAL(s_texture1);
    else if (ordinal == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
    else color = CKFF_SAMPLE_2D_FINAL(s_texture3);
#elif CKFF_NATIVE_COMPARE_COUNT == 2
    if (ordinal == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
    else color = CKFF_SAMPLE_2D_FINAL(s_texture3);
#elif CKFF_NATIVE_COMPARE_COUNT == 3
    color = CKFF_SAMPLE_2D_FINAL(s_texture3);
#endif
#elif CKFF_NATIVE_SDL_GPU && CKFF_NATIVE_COMPARE_COUNT == 0
    if (stage == 0) color = CKFF_SAMPLE_2D_FINAL(s_texture0);
    else if (stage == 1) color = CKFF_SAMPLE_2D_FINAL(s_texture1);
    else if (stage == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
    else if (stage == 3) color = CKFF_SAMPLE_2D_FINAL(s_texture3);
    else if (stage == 4) color = CKFF_SAMPLE_2D_FINAL(s_texture4);
    else if (stage == 5) color = CKFF_SAMPLE_2D_FINAL(s_texture5);
    else if (stage == 6) color = CKFF_SAMPLE_2D_FINAL(s_texture6);
    else color = CKFF_SAMPLE_2D_FINAL(s_texture7);
#elif CKFF_NATIVE_SDL_GPU
#if CKFF_NATIVE_COMPARE_COUNT == 1
    if (ordinal == 1) color = CKFF_SAMPLE_2D_FINAL(s_texture1);
#elif CKFF_NATIVE_COMPARE_COUNT == 2
    if (ordinal == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
#elif CKFF_NATIVE_COMPARE_COUNT == 3
    if (ordinal == 3) color = CKFF_SAMPLE_2D_FINAL(s_texture3);
#elif CKFF_NATIVE_COMPARE_COUNT == 4
    if (ordinal == 4) color = CKFF_SAMPLE_2D_FINAL(s_texture4);
#elif CKFF_NATIVE_COMPARE_COUNT == 5
    if (ordinal == 5) color = CKFF_SAMPLE_2D_FINAL(s_texture5);
#elif CKFF_NATIVE_COMPARE_COUNT == 6
    if (ordinal == 6) color = CKFF_SAMPLE_2D_FINAL(s_texture6);
#elif CKFF_NATIVE_COMPARE_COUNT == 7
    if (ordinal == 7) color = CKFF_SAMPLE_2D_FINAL(s_texture7);
#endif
#if CKFF_NATIVE_COMPARE_COUNT <= 1
    else if (ordinal == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
#endif
#if CKFF_NATIVE_COMPARE_COUNT <= 2
    else if (ordinal == 3) color = CKFF_SAMPLE_2D_FINAL(s_texture3);
#endif
#if CKFF_NATIVE_COMPARE_COUNT <= 3
    else if (ordinal == 4) color = CKFF_SAMPLE_2D_FINAL(s_texture4);
#endif
#if CKFF_NATIVE_COMPARE_COUNT <= 4
    else if (ordinal == 5) color = CKFF_SAMPLE_2D_FINAL(s_texture5);
#endif
#if CKFF_NATIVE_COMPARE_COUNT <= 5
    else if (ordinal == 6) color = CKFF_SAMPLE_2D_FINAL(s_texture6);
#endif
#if CKFF_NATIVE_COMPARE_COUNT <= 6
    else color = CKFF_SAMPLE_2D_FINAL(s_texture7);
#endif
#else
    if (ordinal == 0) color = CKFF_SAMPLE_2D_FINAL(s_texture0);
    else if (ordinal == 1) color = CKFF_SAMPLE_2D_FINAL(s_texture1);
    else if (ordinal == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
#if CKFF_NATIVE_SAMPLER_LAYOUT != 0
    else color = CKFF_SAMPLE_2D_FINAL(s_texture3);
#else
    else if (ordinal == 3) color = CKFF_SAMPLE_2D_FINAL(s_texture3);
    else if (ordinal == 4) color = CKFF_SAMPLE_2D_FINAL(s_texture4);
    else if (ordinal == 5) color = CKFF_SAMPLE_2D_FINAL(s_texture5);
    else if (ordinal == 6) color = CKFF_SAMPLE_2D_FINAL(s_texture6);
    else color = CKFF_SAMPLE_2D_FINAL(s_texture7);
#endif
#endif
#undef CKFF_SAMPLE_2D_FINAL
#undef CKFF_SAMPLE_2D
#undef CKFF_TEXTURE_2D_GRAD
// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
    if (manualBorder && !borderMip) {
        vec2 size;
        if (ordinal == 0) size = CKFF_BORDER_SIZE_2D(s_texture0);
        else if (ordinal == 1) size = CKFF_BORDER_SIZE_2D(s_texture1);
        else if (ordinal == 2) size = CKFF_BORDER_SIZE_2D(s_texture2);
#if CKFF_NATIVE_SAMPLER_LAYOUT != 0
        else size = CKFF_BORDER_SIZE_2D(s_texture3);
#else
        else if (ordinal == 3) size = CKFF_BORDER_SIZE_2D(s_texture3);
        else if (ordinal == 4) size = CKFF_BORDER_SIZE_2D(s_texture4);
        else if (ordinal == 5) size = CKFF_BORDER_SIZE_2D(s_texture5);
        else if (ordinal == 6) size = CKFF_BORDER_SIZE_2D(s_texture6);
        else size = CKFF_BORDER_SIZE_2D(s_texture7);
#endif
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
#endif
        return color.rrrr;
    }
    return color;
}

vec4 getSampleCoord(vec4 coord, int transformFlags)
{
    if ((transformFlags & 0x100) != 0) {
        coord /= abs(coord.w) < 0.0001 ? (coord.w < 0.0 ? -0.0001 : 0.0001) : coord.w;
    }
    return coord;
}

float computePixelFogFactor(float depth, int mode, float vertexFogFactor)
{
    if (mode == 0) return vertexFogFactor;
    return ckffFogFactor(depth, mode, u_ffDrawParams[10]);
}

vec4 applyArgModifiers(vec4 value, int arg)
{
    if ((arg & 0x10) != 0) {
        value.rgb = 1.0 - value.rgb;
        value.a = 1.0 - value.a;
    }
    if ((arg & 0x20) != 0) {
        value = value.aaaa;
    }
    return value;
}

vec4 getArg(int arg, vec4 textureColor, vec4 current, vec4 diffuse, vec4 specular,
            vec4 temp, vec4 stageConstant, bool premodulateCurrent)
{
    int baseArg = arg & ~(0x10 | 0x20);
    vec4 value = current;
    if (baseArg == 0) value = diffuse;
    else if (baseArg == 1) value = premodulateCurrent ? current * textureColor : current;
    else if (baseArg == 2) value = textureColor;
    else if (baseArg == 3) value = u_ffDrawParams[9];
    else if (baseArg == 4) value = specular;
    else if (baseArg == 5) value = temp;
    else if (baseArg == 6) value = stageConstant;
    return applyArgModifiers(value, arg);
}

vec4 applyOp(int op, vec4 a, vec4 b, vec4 c, vec4 dst, vec4 current, vec4 diffuse, vec4 textureColor)
{
    if (op == 1) return dst;
    if (op == 2) return a;
    if (op == 3) return b;
    if (op == 4) return a * b;
    if (op == 5) return clamp(a * b * 2.0, 0.0, 1.0);
    if (op == 6) return clamp(a * b * 4.0, 0.0, 1.0);
    if (op == 7) return clamp(a + b, 0.0, 1.0);
    if (op == 8) return clamp(a + b - 0.5, 0.0, 1.0);
    if (op == 9) return clamp((a + b - 0.5) * 2.0, 0.0, 1.0);
    if (op == 10) return clamp(a - b, 0.0, 1.0);
    if (op == 11) return clamp(a + b - a * b, 0.0, 1.0);
    if (op == 12) return mix(b, a, diffuse.a);
    if (op == 13) return mix(b, a, textureColor.a);
    if (op == 14) return mix(b, a, u_ffDrawParams[9].a);
    if (op == 15) return clamp(a + b * (1.0 - textureColor.a), 0.0, 1.0);
    if (op == 16) return mix(b, a, current.a);
    if (op == 17) return a;
    if (op == 18) return clamp(a + vec4_splat(a.a) * b, 0.0, 1.0);
    if (op == 19) return clamp(a * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 20) return clamp(a + (1.0 - a.a) * b, 0.0, 1.0);
    if (op == 21) return clamp((vec4_splat(1.0) - a) * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 22 || op == 23) return dst;
    if (op == 24) {
        float v = clamp(dot(a.rgb - 0.5, b.rgb - 0.5) * 4.0, 0.0, 1.0);
        return vec4_splat(v);
    }
    if (op == 25) return clamp(a * b + c, 0.0, 1.0);
    if (op == 26) return clamp(c * a + (vec4_splat(1.0) - c) * b, 0.0, 1.0);
    return current;
}

vec4 ckffBlendFactor(int factor, vec4 source, vec4 destination)
{
    if (factor == 1) return vec4_splat(0.0);
    if (factor == 2) return vec4_splat(1.0);
    if (factor == 3) return source;
    if (factor == 4) return vec4_splat(1.0) - source;
    if (factor == 5 || factor == 12) return source.aaaa;
    if (factor == 6 || factor == 13) return vec4_splat(1.0) - source.aaaa;
    if (factor == 7) return destination.aaaa;
    if (factor == 8) return vec4_splat(1.0) - destination.aaaa;
    if (factor == 9) return destination;
    if (factor == 10) return vec4_splat(1.0) - destination;
    if (factor == 11) {
        float saturated = min(source.a, 1.0 - destination.a);
        return vec4(saturated, saturated, saturated, 1.0);
    }
    return vec4_splat(0.0);
}

vec4 ckffStageBlend(vec4 source, vec4 destination, int packedFactors)
{
    int src = (packedFactors >> 4) & 15;
    int dst = packedFactors & 15;
    if (src == 12) { src = 5; dst = 6; }
    else if (src == 13) { src = 6; dst = 5; }
    return clamp(source * ckffBlendFactor(src, source, destination) +
                 destination * ckffBlendFactor(dst, source, destination), 0.0, 1.0);
}

bool alphaPass(float alpha, int func)
{
    float ref = u_ffDrawParams[8].x;
    int alphaPrecision = func / 16;
    func = func - alphaPrecision * 16;
    float alphaTestValue = alpha;
    if (alphaPrecision != 15) {
        alphaPrecision = min(alphaPrecision, 8);
        float precisionScale = exp2(float(8 + alphaPrecision));
        float factor = precisionScale - 1.0;
        float refScale = exp2(float(alphaPrecision));
        float refWrap = exp2(float(8 - alphaPrecision));
        alphaTestValue = round(alpha * factor);
        ref = floor(ref) * refScale + floor(floor(ref) / refWrap);
    } else {
        ref = ref / 255.0;
    }
    if (func == 0 || func == 8) return true;
    if (func == 1) return false;
    if (func == 2) return alphaTestValue < ref;
    if (func == 3) return alphaTestValue == ref;
    if (func == 4) return alphaTestValue <= ref;
    if (func == 5) return alphaTestValue > ref;
    if (func == 6) return alphaTestValue != ref;
    if (func == 7) return alphaTestValue >= ref;
    return true;
}

vec2 ckffDecodeBump(vec2 bump, bool unormEncoded)
{
    return unormEncoded
        ? clamp((bump * 255.0 - 128.0) / 127.0, vec2(-1.0, -1.0), vec2(1.0, 1.0))
        : bump;
}

void main()
{
// CKFF_BGFX_ONLY_BEGIN
    // Fragment clipping also covers bgfx profiles without native clip-distance state.
    if (v_clipDistance0.x < 0.0 || v_clipDistance0.y < 0.0 ||
        v_clipDistance0.z < 0.0 || v_clipDistance0.w < 0.0 ||
        v_clipDistance1.x < 0.0 || v_clipDistance1.y < 0.0)
        discard;
// CKFF_BGFX_ONLY_END
#if !CKFF_NATIVE_SDL_GPU
    int linePattern = int(u_ffDrawParams[3].w);
    int lineRepeat = int(u_ffDrawParams[11].w);
    if (lineRepeat > 0) {
        float linePhase = v_fogPos.y * gl_FragCoord.w;
        int lineBit = 15 - (int(linePhase) & 15);
        if (((linePattern >> lineBit) & 1) == 0)
            discard;
    }
#endif
    CKFFGlobalFragmentProgram fragmentProgram =
        ckffDecodeGlobalFragmentProgram();
    vec4 diffuse = fragmentProgram.FlatShade ? v_flatColor0 : v_color0;
    vec4 specular = fragmentProgram.FlatShade ? v_flatColor1 : v_color1;
    vec4 current = diffuse;
    vec4 temp = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 previousTexture = vec4(0.0, 0.0, 0.0, 1.0);
    bool previousBumpUnorm = false;
    int previousColorOp = 0;
    int previousAlphaOp = 0;

#if BGFX_SHADER_LANGUAGE_HLSL && !CKFF_NATIVE_SDL_GPU
    [loop]
#endif
    for (int stage = 0; stage < 8; ++stage) {
        if (stage > fragmentProgram.LastActiveTextureStage) break;

        CKFFTextureStageProgram stageProgram =
            ckffDecodeTextureStageProgram(stage,
                fragmentProgram.SamplerOrdinals);
        CKFFStageParams stageParams = ckffReadStageParams(
            stageProgram.Projected, u_stageParams[stage * 2 + 0],
            u_stageParams[stage * 2 + 1]);
        int colorOp = stageProgram.ColorOp;
        int alphaOp = stageProgram.AlphaOp;
        bool hasTexture = stageParams.HasTexture;

        if (colorOp == 1) break;

        vec4 stageCoord = v_texcoord0;
        if (stage == 1) stageCoord = v_texcoord1;
        else if (stage == 2) stageCoord = v_texcoord2;
        else if (stage == 3) stageCoord = v_texcoord3;
        else if (stage == 4) stageCoord = v_texcoord4;
        else if (stage == 5) stageCoord = v_texcoord5;
        else if (stage == 6) stageCoord = v_texcoord6;
        else if (stage == 7) stageCoord = v_texcoord7Fog;

        if (u_ffDrawParams[4].z > 0.5) {
            float affineW = abs(v_fogPos.x) < 0.000001
                ? (v_fogPos.x < 0.0 ? -0.000001 : 0.000001) : v_fogPos.x;
            if (stage == 7) stageCoord.xyw /= affineW;
            else stageCoord /= affineW;
        }

        vec4 sampleCoord = getSampleCoord(stageCoord, stageParams.TexcoordTransformFlags);

        if (stage != 0 && (previousColorOp == 22 || previousColorOp == 23)) {
            vec2 bump = ckffDecodeBump(previousTexture.xy, previousBumpUnorm);
            int bumpBase = (stage - 1) * 2;
            sampleCoord.x += dot(u_bumpEnv[bumpBase].xy, bump);
            sampleCoord.y += dot(u_bumpEnv[bumpBase].zw, bump);
        }

        vec4 texColor = getTextureColor(stage, sampleCoord, stageProgram.SamplerType,
            stageProgram.SamplerCompareFunc, stageProgram.SamplerOrdinal,
            stageParams.MirrorOnceMask, hasTexture);
        if (stage != 0 && previousColorOp == 23) {
            int bumpBase = (stage - 1) * 2;
            float lum = clamp(previousTexture.z * u_bumpEnv[bumpBase + 1].x + u_bumpEnv[bumpBase + 1].y, 0.0, 1.0);
            texColor *= lum;
        }
        bool premodulateColor = previousColorOp == 17 && hasTexture;
        bool premodulateAlpha = previousAlphaOp == 17 && hasTexture;
        vec4 colorA = getArg(stageProgram.ColorArg1, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 colorB = getArg(stageProgram.ColorArg2, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 colorC = getArg(stageProgram.ColorArg0, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 alphaA = getArg(stageProgram.AlphaArg1, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);
        vec4 alphaB = getArg(stageProgram.AlphaArg2, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);
        vec4 alphaC = getArg(stageProgram.AlphaArg0, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);

        int resultArg = stageProgram.ResultIsTemp ? 5 : 1;
        vec4 stageResult = resultArg == 5 ? temp : current;
        vec4 colorResult = colorOp == 27
            ? ckffStageBlend(texColor, current, stageParams.StageBlend)
            : applyOp(colorOp, colorA, colorB, colorC, stageResult, current, diffuse, texColor);
        vec4 alphaResult = applyOp(alphaOp, alphaA, alphaB, alphaC, stageResult, current, diffuse, texColor);
        stageResult.rgb = colorResult.rgb;
        stageResult.a = alphaResult.a;
        if (colorOp == 24) {
            stageResult = colorResult;
        }

        if (resultArg == 5) {
            temp = stageResult;
        } else {
            current = stageResult;
        }

        previousTexture = texColor;
        previousBumpUnorm = stageParams.BumpUnorm;
        previousColorOp = colorOp;
        previousAlphaOp = alphaOp;
    }

    if (fragmentProgram.GlobalSpecularEnabled) {
        current.rgb += specular.rgb;
    }
    // Alpha test precision (the high nibble of the packed alpha draw param) is not applied yet:
    // the 8-bit path matches the reference; wider alpha targets are a phase 2.3 item.
    if (fragmentProgram.AlphaTestEnabled &&
        !alphaPass(current.a, fragmentProgram.AlphaFunc)) discard;
    if (fragmentProgram.FogEnabled) {
        int pixelFogMode = fragmentProgram.PixelFogMode;
        float fogFactor = pixelFogMode == 0
            ? v_texcoord7Fog.z
            : computePixelFogFactor(v_fogPos.z / v_fogPos.w, pixelFogMode, v_texcoord7Fog.z);
        current.rgb = mix(u_ffDrawParams[11].rgb, current.rgb, fogFactor);
    }
    gl_FragColor = clamp(current, 0.0, 1.0);
}
