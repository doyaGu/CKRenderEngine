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
#if CKFF_NATIVE_FFP_STAGE
#define CKFF_SAMPLER2D_NORMAL(name, slot) CK_COMBINED Texture2D<float4> name : register(t##slot, space2); CK_COMBINED SamplerState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#define CKFF_SAMPLER2D_COMPARE(name, slot) CK_COMBINED Texture2D<float> name : register(t##slot, space2); CK_COMBINED SamplerComparisonState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 0
#define CKFF_SAMPLER2D_0 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_0 CKFF_SAMPLER2D_NORMAL
#endif
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 1
#define CKFF_SAMPLER2D_1 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_1 CKFF_SAMPLER2D_NORMAL
#endif
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 2
#define CKFF_SAMPLER2D_2 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_2 CKFF_SAMPLER2D_NORMAL
#endif
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 3
#define CKFF_SAMPLER2D_3 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_3 CKFF_SAMPLER2D_NORMAL
#endif
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 4
#define CKFF_SAMPLER2D_4 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_4 CKFF_SAMPLER2D_NORMAL
#endif
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 5
#define CKFF_SAMPLER2D_5 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_5 CKFF_SAMPLER2D_NORMAL
#endif
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 6
#define CKFF_SAMPLER2D_6 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_6 CKFF_SAMPLER2D_NORMAL
#endif
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 7
#define CKFF_SAMPLER2D_7 CKFF_SAMPLER2D_COMPARE
#else
#define CKFF_SAMPLER2D_7 CKFF_SAMPLER2D_NORMAL
#endif
#define CKFF_SAMPLER2D_SELECT(slot) CKFF_SAMPLER2D_##slot
#define SAMPLER2D(name, slot) CKFF_SAMPLER2D_SELECT(slot)(name, slot)
#if CKFF_DEPTH_COMPARE_SAMPLER_COUNT > 0
#define texture2D(name, uv) ckCompareVariantSample2DBias(name, name##Sampler, name##Slot, uv, 0.0, 0.0, 0.0)
#define texture2DBias(name, uv, bias, minMip, maxAnisotropy) ckCompareVariantSample2DBias(name, name##Sampler, name##Slot, uv, bias, minMip, maxAnisotropy)
#define texture2DGrad(name, uv, dx, dy, minMip, maxAnisotropy) ckCompareVariantSample2DGrad(name, name##Sampler, name##Slot, uv, dx, dy, minMip, maxAnisotropy)
#elif CKFF_HARDWARE_SAMPLING
#define texture2D(name, uv) name.Sample(name##Sampler, uv)
#define texture2DBias(name, uv, bias, minMip, maxAnisotropy) name.SampleBias(name##Sampler, uv, bias)
#define texture2DGrad(name, uv, dx, dy, minMip, maxAnisotropy) name.SampleGrad(name##Sampler, uv, dx, dy)
#else
#define texture2D(name, uv) ckSample2D(name, name##Sampler, name##Slot, uv)
#define texture2DBias(name, uv, bias, minMip, maxAnisotropy) ckSample2DBias(name, name##Sampler, name##Slot, uv, bias, minMip, maxAnisotropy)
#define texture2DGrad(name, uv, dx, dy, minMip, maxAnisotropy) ckSample2DGrad(name, name##Sampler, name##Slot, uv, dx, dy, minMip, maxAnisotropy)
#endif
#define texture2DCompare(name, uv, dx, dy, bias, minMip, maxAnisotropy, reference, func) name.SampleCmp(name##Sampler, uv, reference)
#else
#define SAMPLER2D(name, slot) CK_COMBINED Texture2D<float4> name : register(t##slot, space2); CK_COMBINED SamplerState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#define texture2D(name, uv) name.Sample(name##Sampler, uv)
#define texture2DBias(name, uv, bias, minMip, maxAnisotropy) name.SampleBias(name##Sampler, uv, bias)
#define texture2DGrad(name, uv, dx, dy, minMip, maxAnisotropy) name.SampleGrad(name##Sampler, uv, dx, dy)
#endif
#define SAMPLERCUBE(name, slot) CK_COMBINED TextureCube<float4> name : register(t##slot, space2); CK_COMBINED SamplerState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#define SAMPLER3D(name, slot) CK_COMBINED Texture3D<float4> name : register(t##slot, space2); CK_COMBINED SamplerState name##Sampler : register(s##slot, space2); static const uint name##Slot = slot
#define textureCube(name, uv) ckSampleCubeBias(name, name##Sampler, name##Slot, uv, 0.0)
#define textureCubeBias(name, uv, bias) ckSampleCubeBias(name, name##Sampler, name##Slot, uv, bias)
#if CKFF_HARDWARE_SAMPLING
#define texture3D(name, uv) name.Sample(name##Sampler, uv)
#define texture3DBias(name, uv, bias, minMip) name.SampleBias(name##Sampler, uv, bias)
#define texture3DGrad(name, uv, originalUv, mirrorOnceMask, bias, minMip) name.SampleBias(name##Sampler, uv, bias)
#else
#define texture3D(name, uv) ckSample3D(name, name##Sampler, name##Slot, uv)
#define texture3DBias(name, uv, bias, minMip) ckSample3DBias(name, name##Sampler, name##Slot, uv, bias, minMip)
#define texture3DGrad(name, uv, originalUv, mirrorOnceMask, bias, minMip) ckSample3DGrad(name, name##Sampler, name##Slot, uv, originalUv, mirrorOnceMask, bias, minMip)
#endif
