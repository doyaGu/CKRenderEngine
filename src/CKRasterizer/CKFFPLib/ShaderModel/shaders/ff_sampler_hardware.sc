// Compact fixed-function sampler path for states fully represented by hardware
// sampler objects. Selection excludes explicit gradients, manual LOD,
// anisotropy, border handling and depth comparison.

#define CKFF_NATIVE_2D_BIAS(_sampler, _uv, _bias) \
    texture2DBias(_sampler, _uv, _bias, 0.0, 0.0)
#define CKFF_NATIVE_CUBE_BIAS(_sampler, _uv, _bias) \
    textureCubeBias(_sampler, _uv, _bias)
#define CKFF_NATIVE_3D_BIAS(_sampler, _uv, _bias) \
    texture3DBias(_sampler, _uv, _bias, 0.0)

vec4 CKFFSampleNative2D(int stage, vec2 uv, int ordinal, float lodBias)
{
#define CKFF_SAMPLE_NATIVE_2D(_sampler) \
    CKFF_NATIVE_2D_BIAS(_sampler, uv, lodBias)
    vec4 color = vec4_splat(0.0);
#if CKFF_NATIVE_SAMPLER_LAYOUT == 0
    CKFF_DISPATCH_2D_STAGE(stage, color, CKFF_SAMPLE_NATIVE_2D)
#else
    CKFF_DISPATCH_2D_ORDINARY(ordinal, color, CKFF_SAMPLE_NATIVE_2D)
#endif
#undef CKFF_SAMPLE_NATIVE_2D
    return color;
}

vec4 CKFFSampleNativeCube(vec3 uv, int ordinal, float lodBias)
{
#define CKFF_SAMPLE_NATIVE_CUBE(_sampler) \
    CKFF_NATIVE_CUBE_BIAS(_sampler, uv, lodBias)
    vec4 color = vec4_splat(0.0);
    CKFF_DISPATCH_CUBE(ordinal, color, CKFF_SAMPLE_NATIVE_CUBE)
#undef CKFF_SAMPLE_NATIVE_CUBE
    return color;
}

vec4 CKFFSampleNativeVolume(vec3 uv, int ordinal, float lodBias)
{
#if CKFF_VOLUME_RESOURCE_ARRAY
    return s_textureVolume[ordinal].SampleBias(
        s_textureVolumeSampler[ordinal], uv, lodBias);
#else
#define CKFF_SAMPLE_NATIVE_VOLUME(_sampler) \
    CKFF_NATIVE_3D_BIAS(_sampler, uv, lodBias)
    vec4 color = vec4_splat(0.0);
    CKFF_DISPATCH_VOLUME(ordinal, color, CKFF_SAMPLE_NATIVE_VOLUME)
#undef CKFF_SAMPLE_NATIVE_VOLUME
    return color;
#endif
}

vec4 CKFFSampleTexture(int stage, vec4 coord, int samplerType,
                       int compareFunc, int samplerOrdinal,
                       int mirrorOnceMask, bool hasTexture)
{
    if (!hasTexture)
        return vec4(0.0, 0.0, 0.0, 1.0);

    float lodBias = u_bumpEnv[stage * 2 + 1].z;
    if (samplerType == 1)
        return CKFFSampleNativeCube(coord.xyz, samplerOrdinal, lodBias);
    if (samplerType == 3)
        return CKFFSampleNativeVolume(coord.xyz, samplerOrdinal, lodBias);
    vec4 color = CKFFSampleNative2D(
        stage, coord.xy, samplerOrdinal, lodBias);
    return samplerType == 2 ? color.rrrr : color;
}

#undef CKFF_NATIVE_3D_BIAS
#undef CKFF_NATIVE_CUBE_BIAS
#undef CKFF_NATIVE_2D_BIAS
