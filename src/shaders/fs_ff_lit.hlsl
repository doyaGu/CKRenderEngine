// Fixed-function lit fragment shader for bgfx
// Texture modulate + specular add + fog + alpha test
//
// Fragment uniform layout (cb0, offsets in float4 registers):
//   [0]  u_fogColor
//   [1]  u_alphaParams (x=ref, y=func)
//   [2]  u_texFactor

cbuffer Uniforms : register(b0)
{
    float4 u_fogColor;
    float4 u_alphaParams;
    float4 u_texFactor;
};

Texture2D s_texture0 : register(t0);
SamplerState s_sampler0 : register(s0);

struct PSInput
{
    float4 position  : SV_POSITION;
    float4 color0    : COLOR0;
    float4 color1    : COLOR1;
    float2 texcoord0 : TEXCOORD0;
    float  fogFactor : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 texColor = s_texture0.Sample(s_sampler0, input.texcoord0);
    // Stage 0: MODULATE (texture * diffuse)
    float4 result;
    result.rgb = texColor.rgb * input.color0.rgb;
    result.a = texColor.a * input.color0.a;

    // Add specular after texturing
    result.rgb += input.color1.rgb;

    // Alpha test
    float alphaRef = u_alphaParams.x;
    float alphaFunc = u_alphaParams.y;
    if (alphaFunc > 4.5 && alphaFunc < 5.5)      // GREATER
    {
        if (result.a <= alphaRef) discard;
    }
    else if (alphaFunc > 6.5 && alphaFunc < 7.5)  // GREATEREQUAL
    {
        if (result.a < alphaRef) discard;
    }
    else if (alphaFunc > 1.5 && alphaFunc < 2.5)  // LESS
    {
        if (result.a >= alphaRef) discard;
    }
    else if (alphaFunc > 0.5 && alphaFunc < 1.5)  // NEVER
    {
        discard;
    }

    // Fog
    result.rgb = lerp(u_fogColor.rgb, result.rgb, input.fogFactor);

    return result;
}
