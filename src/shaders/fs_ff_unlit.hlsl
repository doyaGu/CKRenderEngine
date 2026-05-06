// Fixed-function unlit fragment shader for bgfx
// Outputs vertex color with fog blend (no texture)
//
// Fragment uniform layout (cb0):
//   [0]  u_fogColor

cbuffer Uniforms : register(b0)
{
    float4 u_fogColor;
};

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
    float4 result = input.color0;
    result.rgb = lerp(u_fogColor.rgb, result.rgb, input.fogFactor);
    return result;
}
