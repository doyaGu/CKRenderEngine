$input a_position, a_normal, a_texcoord0, a_color0, a_color1
$output v_color0, v_color1, v_texcoord0, v_fogFactor

#include "bgfx_shader.sh"

uniform mat4 u_ckModelViewProj;
uniform mat4 u_ckModelView;
uniform mat4 u_ckNormalMatrix;
uniform vec4 u_lightParams;
uniform vec4 u_material[5];
uniform vec4 u_fogParams;
uniform vec4 u_ffParams;
uniform vec4 u_lightModelParams;
uniform vec4 u_lights[56];

float computeFog(float depth, vec4 params)
{
    float mode = params.w;
    if (mode < 0.5) return 1.0;
    if (mode < 1.5) return clamp(exp(-(params.z * depth)), 0.0, 1.0);
    if (mode < 2.5) {
        float e = params.z * depth;
        return clamp(exp(-(e * e)), 0.0, 1.0);
    }
    return clamp((params.y - depth) / max(params.y - params.x, 0.0001), 0.0, 1.0);
}

vec4 selectMaterialSource(float source, vec4 materialValue, vec4 color0, vec4 color1)
{
    int src = int(source + 0.5);
    if (src == 1) return color0;
    if (src == 2) return color1;
    return materialValue;
}

void main()
{
    vec4 localPos = vec4(a_position.xyz, 1.0);
    gl_Position = mul(u_ckModelViewProj, localPos);

    vec4 viewPos = mul(u_ckModelView, localPos);
    vec3 viewNormal = normalize(mul(u_ckNormalMatrix, vec4(a_normal, 0.0)).xyz);

    vec4 matDiffuse  = selectMaterialSource(u_ffParams.x, u_material[0], a_color0, a_color1);
    vec4 matAmbient  = selectMaterialSource(u_ffParams.y, u_material[1], a_color0, a_color1);
    vec4 matSpecular = selectMaterialSource(u_ffParams.z, u_material[2], a_color0, a_color1);
    vec4 matEmissive = selectMaterialSource(u_ffParams.w, u_material[3], a_color0, a_color1);
    float matPower   = u_material[4].x;

    int rawLightCount = int(u_lightParams.x);
    bool lightingEnabled = rawLightCount >= 0;
    int lightCount = rawLightCount < 0 ? 0 : rawLightCount;
    vec3 globalAmbient = u_lightParams.yzw;

    vec4 litDiffuse = matDiffuse;
    vec3 litSpecular = a_color1.rgb;

    if (lightingEnabled) {
        vec4 ambientAccum = vec4(0.0, 0.0, 0.0, 0.0);
        vec4 diffuseAccum = vec4(0.0, 0.0, 0.0, 0.0);
        vec3 specularAccum = vec3(0.0, 0.0, 0.0);

        for (int i = 0; i < 8; ++i) {
            if (i >= lightCount) break;

            int base = i * 7;
            vec4 lPos   = u_lights[base + 0];
            vec4 lDir   = u_lights[base + 1];
            vec4 lDiff  = u_lights[base + 2];
            vec4 lSpec  = u_lights[base + 3];
            vec4 lAmb   = u_lights[base + 4];
            vec4 lAtten = u_lights[base + 5];
            vec4 lSpot  = u_lights[base + 6];

            vec3 toLight;
            float atten = 1.0;

            if (lPos.w < 0.5) {
                toLight = -normalize(lDir.xyz);
            } else {
                vec3 diff = lPos.xyz - viewPos.xyz;
                float dist = length(diff);
                toLight = diff / max(dist, 0.0001);
                atten = 1.0 / max(lAtten.x + lAtten.y * dist + lAtten.z * dist * dist, 0.0001);
                if (lDir.w > 0.0 && dist > lDir.w) atten = 0.0;

                if (lPos.w > 1.5) {
                    float rho = dot(-toLight, normalize(lDir.xyz));
                    float spotAtten = clamp((rho - lSpot.y) / max(lSpot.x - lSpot.y, 0.0001), 0.0, 1.0);
                    atten *= pow(max(spotAtten, 0.0001), lAtten.w);
                }
            }

            float nDotL = max(dot(viewNormal, normalize(toLight)), 0.0);
            ambientAccum += lAmb * atten;
            diffuseAccum += lDiff * nDotL * atten;

            if (nDotL > 0.0 && matPower > 0.0) {
                vec3 halfVec;
                if (u_lightModelParams.x > 0.5) {
                    halfVec = toLight - normalize(viewPos.xyz);
                } else {
                    halfVec = toLight - vec3(0.0, 0.0, 1.0);
                }
                halfVec = normalize(halfVec);
                float nDotH = max(dot(viewNormal, halfVec), 0.0);
                specularAccum += lSpec.rgb * pow(nDotH, matPower) * atten;
            }
        }

        litDiffuse = matEmissive
                   + matAmbient * vec4(globalAmbient, 1.0)
                   + matAmbient * ambientAccum
                   + matDiffuse * diffuseAccum;
        litSpecular = matSpecular.rgb * specularAccum;
    }

    v_color0 = clamp(litDiffuse, 0.0, 1.0);
    v_color0.a = matDiffuse.a;
    v_color1 = vec4(clamp(litSpecular, 0.0, 1.0), a_color1.a);
    v_texcoord0 = a_texcoord0;
    v_fogFactor = computeFog(abs(viewPos.z), u_fogParams);
}
