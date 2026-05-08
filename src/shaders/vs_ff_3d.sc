$input a_position, a_normal, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_clipPos

#include "bgfx_shader.sh"

uniform mat4 u_ckModelViewProj;
uniform mat4 u_ckModel;
uniform mat4 u_ckModelView;
uniform mat4 u_ckNormalMatrix;
uniform mat4 u_texMatrix[8];
uniform vec4 u_lightParams;
uniform vec4 u_material[5];
uniform vec4 u_fogParams;
uniform vec4 u_ffParams;
uniform vec4 u_lightModelParams;
uniform vec4 u_lights[56];
uniform vec4 u_stageParams[32];

#if defined(CKFF_FULL_SPECIALIZED)
#ifndef CKFF_VS_BITS
#define CKFF_VS_BITS 0
#endif
#ifndef CKFF_VS_TEXGEN0
#define CKFF_VS_TEXGEN0 0
#endif
#ifndef CKFF_VS_TEXGEN1
#define CKFF_VS_TEXGEN1 0
#endif
#ifndef CKFF_VS_TEXGEN2
#define CKFF_VS_TEXGEN2 0
#endif
#ifndef CKFF_VS_TEXGEN3
#define CKFF_VS_TEXGEN3 0
#endif
#ifndef CKFF_VS_TEXGEN4
#define CKFF_VS_TEXGEN4 0
#endif
#ifndef CKFF_VS_TEXGEN5
#define CKFF_VS_TEXGEN5 0
#endif
#ifndef CKFF_VS_TEXGEN6
#define CKFF_VS_TEXGEN6 0
#endif
#ifndef CKFF_VS_TEXGEN7
#define CKFF_VS_TEXGEN7 0
#endif
#endif

int ckffVsTexGenMode(int stage, int packedIndex)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (stage == 0) return CKFF_VS_TEXGEN0 & 7;
    if (stage == 1) return CKFF_VS_TEXGEN1 & 7;
    if (stage == 2) return CKFF_VS_TEXGEN2 & 7;
    if (stage == 3) return CKFF_VS_TEXGEN3 & 7;
    if (stage == 4) return CKFF_VS_TEXGEN4 & 7;
    if (stage == 5) return CKFF_VS_TEXGEN5 & 7;
    if (stage == 6) return CKFF_VS_TEXGEN6 & 7;
    if (stage == 7) return CKFF_VS_TEXGEN7 & 7;
    return 0;
#else
    return packedIndex / 65536;
#endif
}

bool ckffVsBit(int bit, bool runtimeValue)
{
#if defined(CKFF_FULL_SPECIALIZED)
    return (CKFF_VS_BITS & (1 << bit)) != 0;
#else
    return runtimeValue;
#endif
}

int ckffVsBits(int shift, int mask, int runtimeValue)
{
#if defined(CKFF_FULL_SPECIALIZED)
    return (CKFF_VS_BITS >> shift) & mask;
#else
    return runtimeValue;
#endif
}

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

vec2 selectTexcoord(int index, vec2 tc0, vec2 tc1, vec2 tc2, vec2 tc3, vec2 tc4, vec2 tc5, vec2 tc6, vec2 tc7)
{
    if (index == 1) return tc1;
    if (index == 2) return tc2;
    if (index == 3) return tc3;
    if (index == 4) return tc4;
    if (index == 5) return tc5;
    if (index == 6) return tc6;
    if (index == 7) return tc7;
    return tc0;
}

vec4 generateTexcoord(int stage, int packedIndex, vec2 tc0, vec2 tc1, vec2 tc2, vec2 tc3, vec2 tc4, vec2 tc5, vec2 tc6, vec2 tc7, vec3 viewPos, vec3 viewNormal)
{
    int generation = ckffVsTexGenMode(stage, packedIndex);
    int index = packedIndex & 65535;
    if (generation == 1) return vec4(viewNormal, 1.0);
    if (generation == 2) return vec4(viewPos, 1.0);
    if (generation == 3) {
        vec3 eye = normalize(viewPos);
        vec3 refl = reflect(eye, viewNormal);
        return vec4(refl, 1.0);
    }
    if (generation == 4) {
        vec3 eye = normalize(viewPos);
        vec3 refl = reflect(eye, viewNormal);
        float m = length(refl + vec3(0.0, 0.0, 1.0)) * 2.0;
        return vec4(refl.xy / max(m, 0.0001) + 0.5, 0.0, 1.0);
    }
    return vec4(selectTexcoord(index & 7, tc0, tc1, tc2, tc3, tc4, tc5, tc6, tc7), 0.0, 0.0);
}

vec4 transformTexcoord(int stage, vec4 coord)
{
    vec4 params = u_stageParams[stage * 4 + 2];
    int flags = int(params.z);
    if (flags == 0) return coord;

    int count = flags & 0xff;
    bool applyTransform = count > 1 && count <= 4;

    int packedIndex = int(params.y);
    int generation = ckffVsTexGenMode(stage, packedIndex);
    if (generation == 0 && applyTransform) {
        coord.z = 1.0;
    }

    vec4 transformed = applyTransform ? mul(u_texMatrix[stage], coord) : coord;
    if ((flags & 0x100) != 0) {
        float divisor = count == 1 ? transformed.x
                      : count == 2 ? transformed.y
                      : count == 3 ? transformed.z
                      : transformed.w;
        transformed.w = divisor;
    }
    if (count > 0 && count < 4) {
        if (count <= 1) transformed.y = 0.0;
        if (count <= 2) transformed.z = 0.0;
        if (count <= 3 && (flags & 0x100) == 0) transformed.w = 0.0;
    }
    return transformed;
}

void main()
{
    vec4 localPos = vec4(a_position.xyz, 1.0);
    gl_Position = mul(u_ckModelViewProj, localPos);
    v_clipPos = mul(u_ckModel, localPos);

    vec4 viewPos = mul(u_ckModelView, localPos);
    vec3 viewNormal = mul(u_ckNormalMatrix, vec4(a_normal, 0.0)).xyz;
    if (ckffVsBit(14, u_lightModelParams.y > 0.5)) {
        viewNormal = normalize(viewNormal);
    }

    vec4 matDiffuse  = selectMaterialSource(u_ffParams.x, u_material[0], a_color0, a_color1);
    vec4 matAmbient  = selectMaterialSource(u_ffParams.y, u_material[1], a_color0, a_color1);
    vec4 matSpecular = selectMaterialSource(u_ffParams.z, u_material[2], a_color0, a_color1);
    vec4 matEmissive = selectMaterialSource(u_ffParams.w, u_material[3], a_color0, a_color1);
    float matPower   = u_material[4].x;

    int rawLightCount = int(u_lightParams.x);
    bool lightingEnabled = ckffVsBit(13, rawLightCount >= 0);
    int lightCount = ckffVsBits(17, 15, rawLightCount < 0 ? 0 : rawLightCount);
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
                if (ckffVsBit(16, u_lightModelParams.x > 0.5)) {
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
    int tc0 = int(u_stageParams[0 * 4 + 2].y);
    int tc1 = int(u_stageParams[1 * 4 + 2].y);
    int tc2 = int(u_stageParams[2 * 4 + 2].y);
    int tc3 = int(u_stageParams[3 * 4 + 2].y);
    int tc4 = int(u_stageParams[4 * 4 + 2].y);
    int tc5 = int(u_stageParams[5 * 4 + 2].y);
    int tc6 = int(u_stageParams[6 * 4 + 2].y);
    int tc7 = int(u_stageParams[7 * 4 + 2].y);
    v_texcoord0 = transformTexcoord(0, generateTexcoord(0, tc0, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord1 = transformTexcoord(1, generateTexcoord(1, tc1, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord2 = transformTexcoord(2, generateTexcoord(2, tc2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord3 = transformTexcoord(3, generateTexcoord(3, tc3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord4 = transformTexcoord(4, generateTexcoord(4, tc4, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord5 = transformTexcoord(5, generateTexcoord(5, tc5, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord6 = transformTexcoord(6, generateTexcoord(6, tc6, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord7Fog = transformTexcoord(7, generateTexcoord(7, tc7, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    v_texcoord7Fog.z = computeFog(abs(viewPos.z), u_fogParams);
}
