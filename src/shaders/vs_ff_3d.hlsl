cbuffer Uniforms : register(b0)
{
    row_major float4x4 u_ckModelViewProj;
    row_major float4x4 u_ckModelView;
    row_major float4x4 u_ckNormalMatrix;
    float4 u_lightParams;
    float4 u_material[5];
    float4 u_fogParams;
    float4 u_viewport;
    float4 u_ffParams;
    float4 u_lights[56];
};

struct VSInput
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
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

float computeFog(float depth, float4 params)
{
    float mode = params.w;
    if (mode < 0.5) return 1.0;
    if (mode < 1.5) return saturate(exp(-(params.z * depth)));
    if (mode < 2.5) {
        float e = params.z * depth;
        return saturate(exp(-(e * e)));
    }
    return saturate((params.y - depth) / max(params.y - params.x, 0.0001));
}

float4 selectMaterialSource(float source, float4 materialValue, float4 color0, float4 color1)
{
    int src = (int)(source + 0.5);
    if (src == 1) return color0;
    if (src == 2) return color1;
    return materialValue;
}

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 localPos = float4(input.position, 1.0);
    output.position = mul(localPos, u_ckModelViewProj);

    float4 viewPos = mul(localPos, u_ckModelView);
    float3 viewNormal = normalize(mul(input.normal, (float3x3)u_ckNormalMatrix));

    float4 matDiffuse  = selectMaterialSource(u_ffParams.x, u_material[0], input.color0, input.color1);
    float4 matAmbient  = selectMaterialSource(u_ffParams.y, u_material[1], input.color0, input.color1);
    float4 matSpecular = selectMaterialSource(u_ffParams.z, u_material[2], input.color0, input.color1);
    float4 matEmissive = selectMaterialSource(u_ffParams.w, u_material[3], input.color0, input.color1);
    float  matPower    = u_material[4].x;

    int rawLightCount = (int)u_lightParams.x;
    bool lightingEnabled = rawLightCount >= 0;
    int lightCount = max(rawLightCount, 0);
    float3 globalAmbient = u_lightParams.yzw;

    float4 litDiffuse = matDiffuse;
    float3 litSpecular = input.color1.rgb;

    if (lightingEnabled)
    {
        float4 ambientAccum = 0.0;
        float4 diffuseAccum = 0.0;
        float3 specularAccum = 0.0;

        for (int i = 0; i < 8; i++)
        {
            if (i >= lightCount) break;

            int base = i * 7;
            float4 lPos   = u_lights[base + 0];
            float4 lDir   = u_lights[base + 1];
            float4 lDiff  = u_lights[base + 2];
            float4 lSpec  = u_lights[base + 3];
            float4 lAmb   = u_lights[base + 4];
            float4 lAtten = u_lights[base + 5];
            float4 lSpot  = u_lights[base + 6];

            float3 toLight;
            float atten = 1.0;

            if (lPos.w < 0.5)
            {
                toLight = -normalize(lDir.xyz);
            }
            else
            {
                float3 diff = lPos.xyz - viewPos.xyz;
                float dist = length(diff);
                toLight = diff / max(dist, 0.0001);
                atten = 1.0 / max(lAtten.x + lAtten.y * dist + lAtten.z * dist * dist, 0.0001);
                if (lDir.w > 0.0 && dist > lDir.w) atten = 0.0;

                if (lPos.w > 1.5)
                {
                    float rho = dot(-toLight, normalize(lDir.xyz));
                    float spotAtten = saturate((rho - lSpot.y) / max(lSpot.x - lSpot.y, 0.0001));
                    atten *= pow(max(spotAtten, 0.0001), lAtten.w);
                }
            }

            float nDotL = max(dot(viewNormal, normalize(toLight)), 0.0);
            ambientAccum += lAmb * atten;
            diffuseAccum += lDiff * nDotL * atten;

            if (nDotL > 0.0 && matPower > 0.0)
            {
                float3 halfVec = normalize(normalize(toLight) + float3(0, 0, 1));
                float nDotH = max(dot(viewNormal, halfVec), 0.0);
                specularAccum += lSpec.rgb * pow(nDotH, matPower) * atten;
            }
        }

        litDiffuse = matEmissive +
                     matAmbient * float4(globalAmbient, 1.0) +
                     matAmbient * ambientAccum +
                     matDiffuse * diffuseAccum;
        litSpecular = matSpecular.rgb * specularAccum;
    }

    output.color0 = saturate(litDiffuse);
    output.color0.a = matDiffuse.a;
    output.color1 = float4(saturate(litSpecular), input.color1.a);
    output.texcoord0 = input.texcoord0;
    output.fogFactor = computeFog(abs(viewPos.z), u_fogParams);
    return output;
}
