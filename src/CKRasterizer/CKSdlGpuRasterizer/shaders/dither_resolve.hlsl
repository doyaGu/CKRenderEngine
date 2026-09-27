#if defined(__spirv__)
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> ck_source_texture : register(t0, space2);
#if defined(__spirv__)
[[vk::combinedImageSampler]]
#endif
SamplerState ck_source_sampler : register(s0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    float2 uv = position.xy * ckDitherParams.xy;
    float4 color = ck_source_texture.SampleLevel(ck_source_sampler, uv, 0.0);
    uint2 pixel = uint2(position.xy);
    uint rank = (((pixel.x ^ pixel.y) & 1u) * 2u) + (pixel.y & 1u);
    float threshold = ckDitherParams.w > 0.5
        ? (float(rank) + 0.5) * 0.25
        : 0.5;
    uint encodedFormat = uint(ckDitherParams.z + 0.5);
    bool explicitQuantize = encodedFormat >= 4u;
    uint format = explicitQuantize ? encodedFormat - 4u : encodedFormat;
    float3 levels = format == 1u ? float3(31.0, 63.0, 31.0)
                                  : (format == 2u ? float3(31.0, 31.0, 31.0)
                                                  : float3(15.0, 15.0, 15.0));
    if (explicitQuantize || format == 3u) {
        color.rgb = floor(saturate(color.rgb) * levels + threshold) / levels;
        if (format == 1u)
            color.a = 1.0;
        else {
            float alphaLevels = format == 2u ? 1.0 : 15.0;
            color.a = floor(saturate(color.a) * alphaLevels + 0.5) /
                      alphaLevels;
        }
    } else {
        // The packed SDL target performs round-to-nearest conversion. Biasing
        // by threshold - 0.5 turns it into the selected ordered threshold.
        color.rgb += (threshold - 0.5) / levels;
    }
    return saturate(color);
}
