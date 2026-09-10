float ckffFogFactor(float depth, int mode, vec4 params)
{
    if (mode == 0) return 1.0;
    if (mode == 1) {
        float e = params.z * depth;
        return clamp(exp(-e), 0.0, 1.0);
    }
    if (mode == 2) {
        float e = params.z * depth;
        return clamp(exp(-(e * e)), 0.0, 1.0);
    }
    float denom = params.y - params.x;
    if (abs(denom) < 0.0001) {
        denom = denom < 0.0 ? -0.0001 : 0.0001;
    }
    return clamp((params.y - depth) / denom, 0.0, 1.0);
}

float ckffPositionTFogFactor(bool fogEnabled, float specularAlpha)
{
    return fogEnabled ? specularAlpha : 1.0;
}
