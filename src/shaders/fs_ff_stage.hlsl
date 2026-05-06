cbuffer Uniforms : register(b0)
{
    float4 u_fogColor;
    float4 u_alphaParams;
    float4 u_texFactor;
    float4 u_stageParams[6];
};

Texture2D s_texture0 : register(t0);
Texture2D s_texture1 : register(t1);
Texture2D s_texture2 : register(t2);
SamplerState s_sampler0 : register(s0);
SamplerState s_sampler1 : register(s1);
SamplerState s_sampler2 : register(s2);

struct PSInput
{
    float4 position  : SV_POSITION;
    float4 color0    : COLOR0;
    float4 color1    : COLOR1;
    float2 texcoord0 : TEXCOORD0;
    float  fogFactor : TEXCOORD1;
};

float4 getTextureColor(int stage, float2 uv, bool hasTexture)
{
    if (!hasTexture) return float4(1.0, 1.0, 1.0, 1.0);
    if (stage == 0) return s_texture0.Sample(s_sampler0, uv);
    if (stage == 1) return s_texture1.Sample(s_sampler1, uv);
    return s_texture2.Sample(s_sampler2, uv);
}

float4 applyArgModifiers(float4 value, int arg)
{
    if ((arg & 0x20) != 0) {
        value.rgb = value.aaa;
    }
    if ((arg & 0x10) != 0) {
        value.rgb = 1.0 - value.rgb;
        value.a = 1.0 - value.a;
    }
    return value;
}

float4 getArg(int arg, float4 textureColor, float4 current, PSInput input)
{
    int baseArg = arg & ~(0x10 | 0x20);
    float4 value = current;
    if (baseArg == 0) value = input.color0;
    else if (baseArg == 1) value = current;
    else if (baseArg == 2) value = textureColor;
    else if (baseArg == 3) value = u_texFactor;
    else if (baseArg == 4) value = input.color1;
    return applyArgModifiers(value, arg);
}

float4 applyOp(int op, float4 a, float4 b, float4 current)
{
    if (op == 1) return current;
    if (op == 2) return a;
    if (op == 3) return b;
    if (op == 4) return a * b;
    if (op == 5) return saturate(a * b * 2.0);
    if (op == 6) return saturate(a * b * 4.0);
    if (op == 7) return saturate(a + b);
    if (op == 8) return saturate(a + b - 0.5);
    if (op == 9) return saturate((a + b - 0.5) * 2.0);
    if (op == 10) return saturate(a - b);
    if (op == 11) return saturate(a + b - a * b);
    if (op == 12) return lerp(b, a, a.a);
    if (op == 13) return lerp(b, a, a.a);
    if (op == 14) return lerp(b, a, u_texFactor.a);
    if (op == 16) return lerp(b, a, current.a);
    if (op == 24) {
        float v = dot(a.rgb * 2.0 - 1.0, b.rgb * 2.0 - 1.0);
        return float4(v, v, v, current.a);
    }
    return a * b;
}

bool alphaPass(float alpha)
{
    float ref = u_alphaParams.x;
    int func = (int)u_alphaParams.y;
    if (func == 0 || func == 8) return true;
    if (func == 1) return false;
    if (func == 2) return alpha < ref;
    if (func == 3) return abs(alpha - ref) < 0.0001;
    if (func == 4) return alpha <= ref;
    if (func == 5) return alpha > ref;
    if (func == 6) return abs(alpha - ref) >= 0.0001;
    if (func == 7) return alpha >= ref;
    return true;
}

float4 main(PSInput input) : SV_TARGET
{
    float4 current = input.color0;

    [unroll]
    for (int stage = 0; stage < 3; ++stage) {
        float4 colorParams = u_stageParams[stage * 2 + 0];
        float4 alphaParams = u_stageParams[stage * 2 + 1];
        int colorOp = (int)colorParams.x;
        int alphaOp = (int)alphaParams.x;
        bool hasTexture = colorParams.w > 0.5;

        if (colorOp == 1) break;

        float4 texColor = getTextureColor(stage, input.texcoord0, hasTexture);
        float4 colorA = getArg((int)colorParams.y, texColor, current, input);
        float4 colorB = getArg((int)colorParams.z, texColor, current, input);
        float4 alphaA = getArg((int)alphaParams.y, texColor, current, input);
        float4 alphaB = getArg((int)alphaParams.z, texColor, current, input);

        float4 colorResult = applyOp(colorOp, colorA, colorB, current);
        float4 alphaResult = applyOp(alphaOp, alphaA, alphaB, current);
        current.rgb = colorResult.rgb;
        current.a = alphaResult.a;
    }

    current.rgb += input.color1.rgb;
    if (!alphaPass(current.a)) discard;
    current.rgb = lerp(u_fogColor.rgb, current.rgb, input.fogFactor);
    return saturate(current);
}
