$input v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos, v_clipDistance0, v_clipDistance1

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"

uniform vec4 u_ffDrawParams[20];
uniform vec4 u_bumpEnv[16];
uniform vec4 u_stageParams[16];
uniform vec4 u_borderColor[8];
uniform vec4 u_borderSampler[16];
uniform vec4 u_ffSpec[5];

// Fixed sampler layout shared by every draw (spec 5.3): one 2D sampler per
// texture stage, then four cube and four volume samplers that the C++ side
// fills in stage order (the n-th cube stage binds s_textureCube{n}).
SAMPLER2D(s_texture0, 0);
SAMPLER2D(s_texture1, 1);
SAMPLER2D(s_texture2, 2);
SAMPLER2D(s_texture3, 3);
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
#define CKFF_TEXTURE_3D_ANISO(_sampler, _uv, _dx, _dy, _bias, _min, _max) \
    ckffNative3DAniso(_sampler, _sampler##Sampler, _sampler##Slot, \
                      _uv, _dx, _dy, _bias, _min, _max)
#define CKFF_TEXTURE_2D_BIAS(_sampler, _uv, _bias) texture2DBias(_sampler, _uv, _bias, minMip)
#define CKFF_TEXTURE_CUBE_BIAS(_sampler, _uv, _bias) textureCubeBias(_sampler, _uv, _bias)
#define CKFF_TEXTURE_3D_BIAS(_sampler, _uv, _bias) texture3DBias(_sampler, _uv, _bias, minMip)
#define CKFF_TEXTURE_3D_GRAD(_sampler, _uv, _original, _dx, _dy, _mirror, _bias) \
    texture3DGrad(_sampler, _uv, _original, _mirror, _bias, minMip)
#endif

// Ordinal of this stage among the stages sampling the same sampler type
// (mirrors CKFFSamplerOrdinal on the C++ side).
int ckffSamplerOrdinal(int stage, int samplerType)
{
    int ordinal = 0;
    for (int previousStage = 0; previousStage < 8; ++previousStage) {
        if (previousStage >= stage) break;
        if (ckffSpecStage_SAMPLER_TYPE(previousStage) == samplerType)
            ++ordinal;
    }
    return ordinal;
}

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
                         int stage, int mask, bool minLinear, bool magLinear)
{
    vec2 size = CKFF_BORDER_SIZE_2D(image);
    int mipCount = max(1, int(u_borderSampler[stage].x));
    int mipFilter = int(u_borderSampler[stage].y);
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
#endif
// CKFF_BGFX_ONLY_END

vec4 getTextureColor(int stage, vec4 coord, int samplerType, int compareFunc, int mirrorOnceMask, bool hasTexture)
{
    if (!hasTexture) return vec4(0.0, 0.0, 0.0, 1.0);
    float lodBias = u_bumpEnv[stage * 2 + 1].z;
    int packedSamplerLod = int(u_bumpEnv[stage * 2 + 1].w);
    float minMip = float(packedSamplerLod & 31);
    float maxAnisotropy = float(packedSamplerLod >> 5);
// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
    maxAnisotropy = float((packedSamplerLod >> 5) & 31);
#endif
// CKFF_BGFX_ONLY_END
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
    int borderMask = (packedSamplerLod >> 10) & 7;
    bool minLinear = ((packedSamplerLod >> 13) & 1) != 0;
    bool magLinear = ((packedSamplerLod >> 14) & 1) != 0;
    bool borderMip = borderMask != 0 &&
        (u_borderSampler[stage].y > 0.5 || maxAnisotropy > 1.0);
#endif
// CKFF_BGFX_ONLY_END
    if (samplerType == 1) {
        int ordinal = ckffSamplerOrdinal(stage, samplerType);
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_CUBE(_sampler) CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)
#else
#define CKFF_SAMPLE_CUBE(_sampler) (maxAnisotropy > 1.0 ? \
    CKFF_TEXTURE_CUBE_ANISO(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip, maxAnisotropy) : \
    (minMip > 0.0 ? CKFF_TEXTURE_CUBE_MIN_MIP(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip) : \
    CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)))
#endif
        if (ordinal == 0) return CKFF_SAMPLE_CUBE(s_textureCube0);
        if (ordinal == 1) return CKFF_SAMPLE_CUBE(s_textureCube1);
        if (ordinal == 2) return CKFF_SAMPLE_CUBE(s_textureCube2);
        return CKFF_SAMPLE_CUBE(s_textureCube3);
#undef CKFF_SAMPLE_CUBE
    }
    if (samplerType == 3) {
        int ordinal = ckffSamplerOrdinal(stage, samplerType);
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_3D(_sampler) (maxAnisotropy > 1.0 ? \
    CKFF_TEXTURE_3D_ANISO(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip, maxAnisotropy) : \
    CKFF_TEXTURE_3D_GRAD(_sampler, coord.xyz, originalCoord3, originalDx3, originalDy3, mirrorOnceMask, lodBias))
#else
#define CKFF_SAMPLE_3D(_sampler) (maxAnisotropy > 1.0 ? \
    CKFF_TEXTURE_3D_ANISO(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip, maxAnisotropy) : \
    (minMip > 0.0 ? CKFF_TEXTURE_3D_MIN_MIP(_sampler, coord.xyz, originalDx3, originalDy3, lodBias, minMip) : \
    CKFF_TEXTURE_3D_GRAD(_sampler, coord.xyz, originalCoord3, originalDx3, originalDy3, mirrorOnceMask, lodBias)))
#endif
#if CKFF_NATIVE_SDL_GPU
        if (ordinal == 0) return CKFF_SAMPLE_3D(s_textureVolume0);
        if (ordinal == 1) return CKFF_SAMPLE_3D(s_textureVolume1);
        if (ordinal == 2) return CKFF_SAMPLE_3D(s_textureVolume2);
        return CKFF_SAMPLE_3D(s_textureVolume3);
#else
// CKFF_BGFX_ONLY_BEGIN
        vec4 volumeColor;
        bool volumeBorderMip = borderMask != 0 &&
            (u_borderSampler[12 + ordinal].y > 0.5 || maxAnisotropy > 1.0);
#define CKFF_SAMPLE_VOLUME(_sampler) (volumeBorderMip ? \
    ckffBorderSample3D(_sampler, coord.xyz, originalDx3, originalDy3, \
        lodBias, minMip, maxAnisotropy, stage, 12 + ordinal, borderMask, minLinear, magLinear) : \
    CKFF_SAMPLE_3D(_sampler))
        if (ordinal == 0) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume0);
        else if (ordinal == 1) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume1);
        else if (ordinal == 2) volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume2);
        else volumeColor = CKFF_SAMPLE_VOLUME(s_textureVolume3);
#undef CKFF_SAMPLE_VOLUME
        if (borderMask != 0 && !volumeBorderMip) {
            vec3 size;
            if (ordinal == 0) size = CKFF_BORDER_SIZE_3D(s_textureVolume0);
            else if (ordinal == 1) size = CKFF_BORDER_SIZE_3D(s_textureVolume1);
            else if (ordinal == 2) size = CKFF_BORDER_SIZE_3D(s_textureVolume2);
            else size = CKFF_BORDER_SIZE_3D(s_textureVolume3);
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
    vec4 color;
#if CKFF_NATIVE_SDL_GPU
#define CKFF_TEXTURE_2D_GRAD(_sampler) texture2DGrad(_sampler, uv, originalDx * exp2(lodBias), originalDy * exp2(lodBias), minMip)
#elif BGFX_SHADER_LANGUAGE_GLSL
    // bgfx's OpenGL compatibility preamble aliases texture2DGrad to the ARB
    // extension even on core GLSL contexts; use the core entry point here.
#define CKFF_TEXTURE_2D_GRAD(_sampler) textureGrad(_sampler, uv, originalDx * exp2(lodBias), originalDy * exp2(lodBias))
#else
#define CKFF_TEXTURE_2D_GRAD(_sampler) texture2DGrad(_sampler, uv, originalDx * exp2(lodBias), originalDy * exp2(lodBias))
#endif
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_2D(_sampler) (mirrorOnceMask != 0 ? \
    CKFF_TEXTURE_2D_GRAD(_sampler) : CKFF_TEXTURE_2D_BIAS(_sampler, uv, lodBias))
#else
#define CKFF_SAMPLE_2D(_sampler) (maxAnisotropy > 1.0 ? \
    CKFF_TEXTURE_2D_ANISO(_sampler, uv, originalDx, originalDy, lodBias, minMip, maxAnisotropy) : \
    (minMip > 0.0 ? CKFF_TEXTURE_2D_MIN_MIP(_sampler, uv, originalDx, originalDy, lodBias, minMip) : \
    (mirrorOnceMask != 0 ? CKFF_TEXTURE_2D_GRAD(_sampler) : \
    CKFF_TEXTURE_2D_BIAS(_sampler, uv, lodBias))))
#endif
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_2D_FINAL(_sampler) CKFF_SAMPLE_2D(_sampler)
#else
// CKFF_BGFX_ONLY_BEGIN
#define CKFF_SAMPLE_2D_FINAL(_sampler) (borderMip ? \
    ckffBorderSample2D(_sampler, uv, originalDx, originalDy, lodBias, minMip, \
        maxAnisotropy, stage, borderMask, minLinear, magLinear) : CKFF_SAMPLE_2D(_sampler))
// CKFF_BGFX_ONLY_END
#endif
    if (stage == 0) color = CKFF_SAMPLE_2D_FINAL(s_texture0);
    else if (stage == 1) color = CKFF_SAMPLE_2D_FINAL(s_texture1);
    else if (stage == 2) color = CKFF_SAMPLE_2D_FINAL(s_texture2);
    else if (stage == 3) color = CKFF_SAMPLE_2D_FINAL(s_texture3);
    else if (stage == 4) color = CKFF_SAMPLE_2D_FINAL(s_texture4);
    else if (stage == 5) color = CKFF_SAMPLE_2D_FINAL(s_texture5);
    else if (stage == 6) color = CKFF_SAMPLE_2D_FINAL(s_texture6);
    else color = CKFF_SAMPLE_2D_FINAL(s_texture7);
#undef CKFF_SAMPLE_2D_FINAL
#undef CKFF_SAMPLE_2D
#undef CKFF_TEXTURE_2D_GRAD
// CKFF_BGFX_ONLY_BEGIN
#if !CKFF_NATIVE_SDL_GPU
    if (borderMask != 0 && !borderMip) {
        vec2 size;
        if (stage == 0) size = CKFF_BORDER_SIZE_2D(s_texture0);
        else if (stage == 1) size = CKFF_BORDER_SIZE_2D(s_texture1);
        else if (stage == 2) size = CKFF_BORDER_SIZE_2D(s_texture2);
        else if (stage == 3) size = CKFF_BORDER_SIZE_2D(s_texture3);
        else if (stage == 4) size = CKFF_BORDER_SIZE_2D(s_texture4);
        else if (stage == 5) size = CKFF_BORDER_SIZE_2D(s_texture5);
        else if (stage == 6) size = CKFF_BORDER_SIZE_2D(s_texture6);
        else size = CKFF_BORDER_SIZE_2D(s_texture7);
        float lod = ckffClampedLod2D(originalDx, originalDy, size, lodBias, 0.0);
        bool filtered = lod > 0.0 ? minLinear : magLinear;
        color = mix(u_borderColor[stage], color,
            ckffBorderCoverage2D(uv, size, borderMask, filtered));
    }
#endif
// CKFF_BGFX_ONLY_END
    if (samplerType == 2) {
        float depth = color.r;
        if (compareFunc != 0) return vec4_splat(compareDepth(depth, coord.z, compareFunc));
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
    bool flatShade = ckffSpec_FLAT_SHADE() != 0;
    int lastActiveStage = ckffSpec_LAST_ACTIVE_TEXTURE_STAGE();
    vec4 diffuse = flatShade ? v_flatColor0 : v_color0;
    vec4 specular = flatShade ? v_flatColor1 : v_color1;
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
        if (stage > lastActiveStage) break;

        CKFFStageParams stageParams = ckffReadStageParams(stage, u_stageParams[stage * 2 + 0], u_stageParams[stage * 2 + 1]);
        int colorOp = stageParams.ColorOp;
        int alphaOp = stageParams.AlphaOp;
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

        vec4 texColor = getTextureColor(stage, sampleCoord, stageParams.SamplerType, stageParams.SamplerCompareFunc, stageParams.MirrorOnceMask, hasTexture);
        if (stage != 0 && previousColorOp == 23) {
            int bumpBase = (stage - 1) * 2;
            float lum = clamp(previousTexture.z * u_bumpEnv[bumpBase + 1].x + u_bumpEnv[bumpBase + 1].y, 0.0, 1.0);
            texColor *= lum;
        }
        bool premodulateColor = previousColorOp == 17 && hasTexture;
        bool premodulateAlpha = previousAlphaOp == 17 && hasTexture;
        vec4 colorA = getArg(stageParams.ColorArg1, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 colorB = getArg(stageParams.ColorArg2, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 colorC = getArg(stageParams.ColorArg0, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 alphaA = getArg(stageParams.AlphaArg1, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);
        vec4 alphaB = getArg(stageParams.AlphaArg2, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);
        vec4 alphaC = getArg(stageParams.AlphaArg0, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);

        int resultArg = stageParams.ResultArg;
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

    if (ckffSpec_GLOBAL_SPECULAR_ENABLED() != 0) {
        current.rgb += specular.rgb;
    }
    // Alpha test precision (the high nibble of the packed alpha draw param) is not applied yet:
    // the 8-bit path matches the reference; wider alpha targets are a phase 2.3 item.
    if (ckffSpec_ALPHA_TEST_ENABLED() != 0 && !alphaPass(current.a, ckffSpec_ALPHA_FUNC())) discard;
    if (ckffSpec_FOG_ENABLED() != 0) {
        int pixelFogMode = ckffSpec_PIXEL_FOG_MODE();
        float fogFactor = pixelFogMode == 0
            ? v_texcoord7Fog.z
            : computePixelFogFactor(v_fogPos.z / v_fogPos.w, pixelFogMode, v_texcoord7Fog.z);
        current.rgb = mix(u_ffDrawParams[11].rgb, current.rgb, fogFactor);
    }
    gl_FragColor = clamp(current, 0.0, 1.0);
}
