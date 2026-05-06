// Fixed-function lit vertex shader for bgfx
// Uses bgfx predefined uniforms for transforms, custom uniforms for lighting
//
// Constant buffer layout (single flat cb0, offsets in float4 registers):
//   [0-3]   u_modelViewProj (predefined, auto-filled)
//   [4-7]   u_modelView     (predefined, auto-filled)
//   [8-11]  u_model         (predefined, auto-filled, bones=1)
//   [12-15] u_viewProj      (predefined, auto-filled)
//   [16]    u_lightParams   (user: x=count, yzw=globalAmbient)
//   [17-21] u_material      (user: diffuse, ambient, specular, emissive, power)
//   [22]    u_fogParams     (user: x=start, y=end, z=density, w=mode)
//   [23-78] u_lights        (user: 8 lights * 7 vec4 = 56 vec4)

cbuffer Uniforms : register(b0)
{
    row_major float4x4 u_ckModelViewProj; // reg 0-3 (user)
    row_major float4x4 u_ckModelView;     // reg 4-7 (user)
    float4   u_lightParams;     // reg 8 (user)
    float4   u_material[5];     // reg 9-13 (user: diffuse, ambient, specular, emissive, power)
    float4   u_fogParams;       // reg 14 (user)
    float4   u_lights[56];      // reg 15-70 (user: 8 lights * 7 vec4)
};

struct VSInput
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
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

    // View-space position for fog and lighting
    float4 viewPos = mul(float4(input.position, 1.0), u_ckModelView);

    // Normal in view space (upper 3x3 of modelView, assumes uniform scale)
    float3 viewNormal = normalize(mul(input.normal, (float3x3)u_ckModelView));

    // Unpack material
    float4 matDiffuse  = u_material[0];
    float4 matAmbient  = u_material[1];
    float4 matSpecular = u_material[2];
    float4 matEmissive = u_material[3];
    float  matPower    = u_material[4].x;

    // Lighting
    int lightCount = (int)u_lightParams.x;
    float3 globalAmbient = u_lightParams.yzw;

    float4 litDiffuse = matEmissive + matAmbient * float4(globalAmbient, 1.0);
    float3 litSpecular = float3(0, 0, 0);

    for (int i = 0; i < 8; i++)
    {
        if (i >= lightCount) break;

        int base = i * 7;
        float4 lPos   = u_lights[base + 0]; // xyz=pos(view), w=type
        float4 lDir   = u_lights[base + 1]; // xyz=dir(view), w=range
        float4 lDiff  = u_lights[base + 2];
        float4 lSpec  = u_lights[base + 3];
        float4 lAmb   = u_lights[base + 4];
        float4 lAtten = u_lights[base + 5]; // constant, linear, quadratic, falloff
        float4 lSpot  = u_lights[base + 6]; // cos(theta/2), cos(phi/2), 0, 0

        float3 toLight;
        float atten = 1.0;

        if (lPos.w < 0.5) // directional
        {
            toLight = -lDir.xyz;
        }
        else
        {
            float3 diff = lPos.xyz - viewPos.xyz;
            float dist = length(diff);
            toLight = diff / max(dist, 0.0001);
            atten = 1.0 / (lAtten.x + lAtten.y * dist + lAtten.z * dist * dist);
            if (lDir.w > 0.0 && dist > lDir.w) atten = 0.0;

            if (lPos.w > 1.5) // spot
            {
                float rho = dot(-toLight, normalize(lDir.xyz));
                float denom = lSpot.x - lSpot.y;
                if (abs(denom) < 0.0001) denom = 0.0001;
                float spotAtten = saturate((rho - lSpot.y) / denom);
                atten *= pow(max(spotAtten, 0.0001), lAtten.w);
            }
        }

        float NdotL = max(dot(viewNormal, normalize(toLight)), 0.0);
        litDiffuse.rgb += lDiff.rgb * matDiffuse.rgb * NdotL * atten;
        litDiffuse.rgb += lAmb.rgb * matAmbient.rgb * atten;

        if (NdotL > 0.0 && matPower > 0.0)
        {
            float3 halfVec = normalize(normalize(toLight) + float3(0, 0, 1));
            float NdotH = max(dot(viewNormal, halfVec), 0.0);
            litSpecular += lSpec.rgb * matSpecular.rgb * pow(NdotH, matPower) * atten;
        }
    }

    output.color0 = saturate(litDiffuse);
    output.color0.a = matDiffuse.a;
    output.color1 = float4(saturate(litSpecular), 0.0);
    output.texcoord0 = input.texcoord0;

    // Fog
    output.fogFactor = computeFog(abs(viewPos.z), u_fogParams);

    return output;
}
