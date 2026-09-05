// A separate source volume avoids sampling a view that overlaps the attachment.
#if defined(__spirv__)
[[vk::combinedImageSampler]]
#endif
Texture3D<float4> ck_source : register(t0, space2);
#if defined(__spirv__)
[[vk::combinedImageSampler]]
#endif
SamplerState ck_sourceSampler : register(s0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    uint3 sourceSize = uint3(ckVolumeParams[0].xyz);
    uint3 targetSize = uint3(ckVolumeParams[1].xyz);
    uint3 target = uint3(uint2(position.xy), uint(ckVolumeParams[1].w));
    // Partition the whole source, including trailing texels of odd dimensions.
    uint3 lo = target * sourceSize / targetSize;
    uint3 hi = (target + 1) * sourceSize / targetSize;
    float4 sum = 0;
    for (uint z = lo.z; z < hi.z; ++z)
        for (uint y = lo.y; y < hi.y; ++y)
            for (uint x = lo.x; x < hi.x; ++x)
                sum += ck_source.SampleLevel(ck_sourceSampler,
                    (float3(x, y, z) + 0.5) / float3(sourceSize), 0);
    uint3 extent = hi - lo;
    return sum / float(extent.x * extent.y * extent.z);
}
