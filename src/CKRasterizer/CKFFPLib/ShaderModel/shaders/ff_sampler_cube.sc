// Fixed-function cube texture sampling.

vec4 CKFFSampleCube(vec4 coord, vec3 originalCoord, int ordinal,
                    CKFFSamplerShaderProgram sampleProgram)
{
    float lodBias = sampleProgram.LodBias;
    float minMip = sampleProgram.MinimumMip;
    float maxAnisotropy = sampleProgram.AnisotropyTaps;
    vec3 originalDx = vec3_splat(0.0);
    vec3 originalDy = vec3_splat(0.0);
    if (sampleProgram.RequiresExplicitGradient) {
        originalDx = dFdx(originalCoord);
        originalDy = dFdy(originalCoord);
    }
#if CKFF_NATIVE_SDL_GPU
#define CKFF_SAMPLE_CUBE(_sampler) \
    CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)
#else
#define CKFF_SAMPLE_CUBE(_sampler) (sampleProgram.ManualAnisotropy ? \
    CKFF_TEXTURE_CUBE_ANISO(_sampler, coord.xyz, originalDx, originalDy, \
        lodBias, minMip, maxAnisotropy) : \
    (sampleProgram.ManualLod ? \
        CKFF_TEXTURE_CUBE_MIN_MIP(_sampler, coord.xyz, originalDx, originalDy, \
            lodBias, minMip) : \
        CKFF_TEXTURE_CUBE_BIAS(_sampler, coord.xyz, lodBias)))
#endif
    vec4 color = vec4_splat(0.0);
    CKFF_DISPATCH_CUBE(ordinal, color, CKFF_SAMPLE_CUBE)
#undef CKFF_SAMPLE_CUBE
    return color;
}
