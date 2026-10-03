// Legacy single-level cube maps filter within the selected face. Modern
// seamless cube samplers otherwise blend the neighbouring face at its edge.
// With equal min/mag filters and one mip, clamping the direction cannot
// change the selected filter or mip. Other footprints keep their native path.
float4 ckSampleCubeBias(CKFFTextureCube image, CKFFSampler state, uint slot,
                       float3 direction, float bias)
{
    uint width, height, levels;
    image.GetDimensions(0, width, height, levels);
    // A wider anisotropic footprint and mip filtering require independent
    // per-face taps at each level. Retain their existing native path here.
    if (levels != 1 || ck_samplerInfo[slot].y != ck_samplerInfo[slot].z ||
        ck_samplerInfo[slot].y == 7.0)
        return image.SampleBias(state, direction, bias);
    float3 axes = abs(direction);
    bool x = axes.x > axes.y && axes.x > axes.z;
    bool y = !x && axes.y > axes.z;
    float major = max(max(axes.x, axes.y), axes.z);
    float limit = major * (1.0 - 1.0 / float(width));
    float3 inside = clamp(direction, -limit, limit);
    float3 at = float3(x ? direction.x : inside.x,
                      y ? direction.y : inside.y,
                      (!x && !y) ? direction.z : inside.z);
    return image.SampleBias(state, at, bias);
}
