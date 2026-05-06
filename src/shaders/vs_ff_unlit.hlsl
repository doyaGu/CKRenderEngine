// Fixed-function unlit vertex shader for bgfx
// Passes vertex color through, computes fog
//
// Constant buffer layout (single flat cb0):
//   [0-3]   u_modelViewProj (predefined)
//   [4-7]   u_modelView     (predefined)
//   [8-11]  u_model         (predefined)
//   [12]    u_fogParams     (user: x=start, y=end, z=density, w=mode)

cbuffer Uniforms : register(b0)
{
    row_major float4x4 u_ckModelViewProj; // reg 0-3
    row_major float4x4 u_ckModelView;     // reg 4-7
    float4   u_fogParams;       // reg 8
};

struct VSInput
{
    float3 position  : POSITION;
    float4 color0    : COLOR0;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position  : SV_POSITION;
    float4 color0    : COLOR0;
    float4 color1    : COLOR1;
    float2 texcoord0 : TEXCOORD0;
    float  fogFactor : TEXCOORD1;
};

float computeFog(float depth, float4 params)
{
    float mode = params.w;
    if (mode < 0.5) return 1.0;
    if (mode < 1.5) return saturate((params.y - depth) / (params.y - params.x));
    if (mode < 2.5) return saturate(exp(-(params.z * depth)));
    float e = params.z * depth;
    return saturate(exp(-(e * e)));
}

VSOutput main(VSInput input)
{
    VSOutput output;

    output.position = mul(float4(input.position, 1.0), u_ckModelViewProj);

    float4 viewPos = mul(float4(input.position, 1.0), u_ckModelView);

    output.color0 = input.color0;
    output.color1 = float4(0, 0, 0, 0);
    output.texcoord0 = input.texcoord0;

    output.fogFactor = computeFog(abs(viewPos.z), u_fogParams);

    return output;
}
