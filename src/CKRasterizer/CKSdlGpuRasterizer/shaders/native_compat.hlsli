// Language adapter for the shared CK fixed-function shader calculations.
// Matrix bytes and multiplication follow the existing HLSL ABI.
#pragma pack_matrix(column_major)
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define uvec4 uint4
#define mat4 float4x4
#define mix lerp
#define vec2_splat(x) float2(x, x)
#define vec3_splat(x) float3(x, x, x)
#define vec4_splat(x) float4(x, x, x, x)
#define uvec4_splat(x) uint4(x, x, x, x)
#define CKFF_NATIVE_SDL_GPU 1
#define dFdx(x) ddx(x)
#define dFdy(x) ddy(x)

#if defined(__spirv__)
#define CK_COMBINED [[vk::combinedImageSampler]]
#define CK_LOCATION(n) [[vk::location(n)]]
#else
#define CK_COMBINED
#define CK_LOCATION(n)
#endif

// Sampler resource indices are dense within fragment space 2. Their logical
// roles (2D, cube and volume) are fixed by the shared FFP shader ABI.
#define SAMPLER2D(name, slot) CK_COMBINED Texture2D<float4> name : register(t##slot, space2); CK_COMBINED SamplerState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#define SAMPLERCUBE(name, slot) CK_COMBINED TextureCube<float4> name : register(t##slot, space2); CK_COMBINED SamplerState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#define SAMPLER3D(name, slot) CK_COMBINED Texture3D<float4> name : register(t##slot, space2); CK_COMBINED SamplerState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#define texture2D(name, uv) ckSample2D(name, name##Sampler, name##Slot, uv)
#define texture2DBias(name, uv, bias, minMip) ckSample2DBias(name, name##Sampler, name##Slot, uv, bias, minMip)
#define texture2DGrad(name, uv, dx, dy, minMip) ckSample2DGrad(name, name##Sampler, name##Slot, uv, dx, dy, minMip)
#define textureCube(name, uv) name.Sample(name##Sampler, uv)
#define textureCubeBias(name, uv, bias) name.SampleBias(name##Sampler, uv, bias)
#define texture3D(name, uv) ckSample3D(name, name##Sampler, name##Slot, uv)
#define texture3DBias(name, uv, bias, minMip) ckSample3DBias(name, name##Sampler, name##Slot, uv, bias, minMip)
#define texture3DGrad(name, uv, original, mirror, bias, minMip) ckSample3DGrad(name, name##Sampler, name##Slot, uv, original, mirror, bias, minMip)
