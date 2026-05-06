cbuffer Uniforms : register(b0)
{
    float4 u_fogParams;
    float4 u_viewport;
};

struct VSInput
{
    float4 position  : POSITION;
    float2 texcoord0 : TEXCOORD0;
    float4 color0    : COLOR0;
    float4 color1    : COLOR1;
};

struct VSOutput
{
    float4 position  : SV_POSITION;
    float4 color0    : COLOR0;
    float4 color1    : COLOR1;
    float2 texcoord0 : TEXCOORD0;
    float  fogFactor : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float rhw = input.position.w == 0.0 ? 1.0 : input.position.w;
    float clipW = 1.0 / rhw;
    float clipX = input.position.x * u_viewport.x + u_viewport.z;
    float clipY = input.position.y * u_viewport.y + u_viewport.w;
    output.position = float4(clipX * clipW, clipY * clipW, input.position.z * clipW, clipW);

    output.color0 = input.color0;
    output.color1 = input.color1;
    output.texcoord0 = input.texcoord0;
    output.fogFactor = 1.0;
    return output;
}
