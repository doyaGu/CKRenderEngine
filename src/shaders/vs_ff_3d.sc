#ifndef CKFF_VS_CLIP_DISTANCE
#define CKFF_VS_CLIP_DISTANCE 0
#endif
#if CKFF_VS_VERTEX_BLEND_MODE != 0
$input a_position, a_normal, a_indices, a_weight, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
#else
$input a_position, a_normal, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
#endif
#if CKFF_VS_CLIP_DISTANCE
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_clipPos, v_clipDistance0, v_clipDistance1
#else
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_clipPos
#endif

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"

uniform mat4 u_ffMatrices[8];
uniform mat4 u_texMatrix[8];
uniform vec4 u_ffDrawParams[19];
uniform vec4 u_lights[56];
uniform vec4 u_stageParams[32];
#if CKFF_VS_CLIP_DISTANCE
uniform vec4 u_clipPlanes[6];
uniform vec4 u_clipParams;
#endif

#if defined(CKFF_FULL_SPECIALIZED)
#ifndef CKFF_VS_BITS
#define CKFF_VS_BITS 0
#endif
#ifndef CKFF_VS_DIFFUSE_SOURCE
#define CKFF_VS_DIFFUSE_SOURCE 0
#endif
#ifndef CKFF_VS_AMBIENT_SOURCE
#define CKFF_VS_AMBIENT_SOURCE 0
#endif
#ifndef CKFF_VS_SPECULAR_SOURCE
#define CKFF_VS_SPECULAR_SOURCE 0
#endif
#ifndef CKFF_VS_EMISSIVE_SOURCE
#define CKFF_VS_EMISSIVE_SOURCE 0
#endif
#ifndef CKFF_VS_FOG_MODE
#define CKFF_VS_FOG_MODE 0
#endif
#ifndef CKFF_VS_VERTEX_BLEND_MODE
#define CKFF_VS_VERTEX_BLEND_MODE 0
#endif
#ifndef CKFF_VS_VERTEX_BLEND_INDEXED
#define CKFF_VS_VERTEX_BLEND_INDEXED 0
#endif
#ifndef CKFF_VS_VERTEX_BLEND_COUNT
#define CKFF_VS_VERTEX_BLEND_COUNT 0
#endif
#ifndef CKFF_VS_TEXCOORD_DECL_MASK
#define CKFF_VS_TEXCOORD_DECL_MASK 4793490
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
#ifndef CKFF_VS_TEXCOORD0
#define CKFF_VS_TEXCOORD0 0
#endif
#ifndef CKFF_VS_TEXCOORD1
#define CKFF_VS_TEXCOORD1 1
#endif
#ifndef CKFF_VS_TEXCOORD2
#define CKFF_VS_TEXCOORD2 2
#endif
#ifndef CKFF_VS_TEXCOORD3
#define CKFF_VS_TEXCOORD3 3
#endif
#ifndef CKFF_VS_TEXCOORD4
#define CKFF_VS_TEXCOORD4 4
#endif
#ifndef CKFF_VS_TEXCOORD5
#define CKFF_VS_TEXCOORD5 5
#endif
#ifndef CKFF_VS_TEXCOORD6
#define CKFF_VS_TEXCOORD6 6
#endif
#ifndef CKFF_VS_TEXCOORD7
#define CKFF_VS_TEXCOORD7 7
#endif
#ifndef CKFF_VS_TEXFLAGS0
#define CKFF_VS_TEXFLAGS0 0
#endif
#ifndef CKFF_VS_TEXFLAGS1
#define CKFF_VS_TEXFLAGS1 0
#endif
#ifndef CKFF_VS_TEXFLAGS2
#define CKFF_VS_TEXFLAGS2 0
#endif
#ifndef CKFF_VS_TEXFLAGS3
#define CKFF_VS_TEXFLAGS3 0
#endif
#ifndef CKFF_VS_TEXFLAGS4
#define CKFF_VS_TEXFLAGS4 0
#endif
#ifndef CKFF_VS_TEXFLAGS5
#define CKFF_VS_TEXFLAGS5 0
#endif
#ifndef CKFF_VS_TEXFLAGS6
#define CKFF_VS_TEXFLAGS6 0
#endif
#ifndef CKFF_VS_TEXFLAGS7
#define CKFF_VS_TEXFLAGS7 0
#endif
#ifndef CKFF_VS_ACTIVE_TEXCOORD_COUNT
#define CKFF_VS_ACTIVE_TEXCOORD_COUNT 8
#endif
#if ((CKFF_VS_BITS & (1 << 13)) != 0) || CKFF_VS_VERTEX_BLEND_MODE != 0 || CKFF_VS_FOG_MODE != 0 || CKFF_VS_TEXGEN0 != 0 || CKFF_VS_TEXGEN1 != 0 || CKFF_VS_TEXGEN2 != 0 || CKFF_VS_TEXGEN3 != 0 || CKFF_VS_TEXGEN4 != 0 || CKFF_VS_TEXGEN5 != 0 || CKFF_VS_TEXGEN6 != 0 || CKFF_VS_TEXGEN7 != 0
#define CKFF_VS_NEEDS_VIEW_SPACE 1
#else
#define CKFF_VS_NEEDS_VIEW_SPACE 0
#endif
#else
#define CKFF_VS_NEEDS_VIEW_SPACE 1
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

int ckffVsTexcoordIndex(int stage, int packedIndex)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (stage == 0) return CKFF_VS_TEXCOORD0;
    if (stage == 1) return CKFF_VS_TEXCOORD1;
    if (stage == 2) return CKFF_VS_TEXCOORD2;
    if (stage == 3) return CKFF_VS_TEXCOORD3;
    if (stage == 4) return CKFF_VS_TEXCOORD4;
    if (stage == 5) return CKFF_VS_TEXCOORD5;
    if (stage == 6) return CKFF_VS_TEXCOORD6;
    return CKFF_VS_TEXCOORD7;
#else
    return packedIndex & 7;
#endif
}

int ckffVsTexTransformFlags(int stage, float runtimeFlags)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (stage == 0) return CKFF_VS_TEXFLAGS0;
    if (stage == 1) return CKFF_VS_TEXFLAGS1;
    if (stage == 2) return CKFF_VS_TEXFLAGS2;
    if (stage == 3) return CKFF_VS_TEXFLAGS3;
    if (stage == 4) return CKFF_VS_TEXFLAGS4;
    if (stage == 5) return CKFF_VS_TEXFLAGS5;
    if (stage == 6) return CKFF_VS_TEXFLAGS6;
    return CKFF_VS_TEXFLAGS7;
#else
    return int(runtimeFlags);
#endif
}

int ckffVsTexcoordComponentCount(int stage)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (stage == 0) return (CKFF_VS_TEXCOORD_DECL_MASK >> 0) & 7;
    if (stage == 1) return (CKFF_VS_TEXCOORD_DECL_MASK >> 3) & 7;
    if (stage == 2) return (CKFF_VS_TEXCOORD_DECL_MASK >> 6) & 7;
    if (stage == 3) return (CKFF_VS_TEXCOORD_DECL_MASK >> 9) & 7;
    if (stage == 4) return (CKFF_VS_TEXCOORD_DECL_MASK >> 12) & 7;
    if (stage == 5) return (CKFF_VS_TEXCOORD_DECL_MASK >> 15) & 7;
    if (stage == 6) return (CKFF_VS_TEXCOORD_DECL_MASK >> 18) & 7;
    return (CKFF_VS_TEXCOORD_DECL_MASK >> 21) & 7;
#else
    return 2;
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

int ckffVsMaterialSource(int slot, float runtimeSource)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (slot == 0) return CKFF_VS_DIFFUSE_SOURCE;
    if (slot == 1) return CKFF_VS_AMBIENT_SOURCE;
    if (slot == 2) return CKFF_VS_SPECULAR_SOURCE;
    if (slot == 3) return CKFF_VS_EMISSIVE_SOURCE;
    return 0;
#else
    return int(runtimeSource + 0.5);
#endif
}

int ckffVsFogMode(float runtimeMode)
{
#if defined(CKFF_FULL_SPECIALIZED)
    return CKFF_VS_FOG_MODE;
#else
    return int(runtimeMode + 0.5);
#endif
}

int ckffVsVertexBlendMode()
{
#if defined(CKFF_FULL_SPECIALIZED)
    return CKFF_VS_VERTEX_BLEND_MODE;
#else
    return 0;
#endif
}

int ckffVsVertexBlendCount()
{
#if defined(CKFF_FULL_SPECIALIZED)
    return CKFF_VS_VERTEX_BLEND_COUNT;
#else
    return 0;
#endif
}

bool ckffVsVertexBlendIndexed()
{
#if defined(CKFF_FULL_SPECIALIZED)
    return CKFF_VS_VERTEX_BLEND_INDEXED != 0;
#else
    return false;
#endif
}

#if CKFF_VS_VERTEX_BLEND_MODE != 0
float ckffBlendWeight(int index, vec3 blendWeight)
{
    if (index == 0) return blendWeight.x;
    if (index == 1) return blendWeight.y;
    if (index == 2) return blendWeight.z;
    return 0.0;
}

int ckffBlendIndex(int index, uvec4 blendIndices)
{
    if (!ckffVsVertexBlendIndexed())
        return index;
    if (index == 0) return int(blendIndices.x);
    if (index == 1) return int(blendIndices.y);
    if (index == 2) return int(blendIndices.z);
    return int(blendIndices.w);
}

mat4 ckffBlendMatrix(int index)
{
    if (index == 1) return u_ffMatrices[5];
    if (index == 2) return u_ffMatrices[6];
    if (index == 3) return u_ffMatrices[7];
    return u_ffMatrices[4];
}
#endif

vec4 selectMaterialSource(int slot, float source, vec4 materialValue, vec4 color0, vec4 color1)
{
    int src = ckffVsMaterialSource(slot, source);
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
    int index = ckffVsTexcoordIndex(stage, packedIndex);
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
    int declaredCount = ckffVsTexcoordComponentCount(index & 7);
    vec4 result = vec4(selectTexcoord(index & 7, tc0, tc1, tc2, tc3, tc4, tc5, tc6, tc7), 0.0, 1.0);
    if (declaredCount <= 1) result.y = 0.0;
    if (declaredCount <= 2) result.z = 0.0;
    if (declaredCount <= 3) result.w = 0.0;
    return result;
}

vec4 transformTexcoord(int stage, vec4 coord)
{
#if defined(CKFF_FULL_SPECIALIZED)
    int flags = ckffVsTexTransformFlags(stage, 0.0);
    int packedIndex = 0;
#else
    vec4 params = u_stageParams[stage * 4 + 2];
    int flags = ckffVsTexTransformFlags(stage, params.z);
    int packedIndex = int(params.y);
#endif
    if (flags == 0) return coord;

    int count = flags & 0xff;
    bool applyTransform = count > 1 && count <= 4;

    int generation = ckffVsTexGenMode(stage, packedIndex);
    int inputIndex = ckffVsTexcoordIndex(stage, packedIndex);
    int declaredCount = ckffVsTexcoordComponentCount(inputIndex & 7);
    if (generation == 0 && applyTransform && declaredCount >= 1 && declaredCount < count) {
        if (declaredCount == 1) coord.y = 1.0;
        else if (declaredCount == 2) coord.z = 1.0;
        else if (declaredCount == 3) coord.w = 1.0;
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
    int vertexBlendMode = ckffVsVertexBlendMode();
    int vertexBlendCount = ckffVsVertexBlendCount();

    vec4 viewPos;
    vec3 viewNormal;
#if CKFF_VS_VERTEX_BLEND_MODE != 0
    bool vertexBlendActive = vertexBlendMode == 1;
    if (vertexBlendActive) {
        float remainingWeight = 1.0;
        vec4 blendedWorldPos = vec4_splat(0.0);
        vec3 blendedNormal = vec3_splat(0.0);
        for (int blendSlot = 0; blendSlot < 4; ++blendSlot) {
            if (blendSlot <= vertexBlendCount) {
                float weight = remainingWeight;
                if (blendSlot != vertexBlendCount) {
                    weight = ckffBlendWeight(blendSlot, a_weight);
                    remainingWeight -= weight;
                }
                mat4 blendMatrix = ckffBlendMatrix(ckffBlendIndex(blendSlot, a_indices));
                blendedWorldPos += mul(blendMatrix, localPos) * weight;
                blendedNormal += mul(blendMatrix, vec4(a_normal, 0.0)).xyz * weight;
            }
        }
        viewPos = mul(u_ffMatrices[2], blendedWorldPos);
        viewNormal = mul(u_ffMatrices[3], vec4(blendedNormal, 0.0)).xyz;
        gl_Position = mul(u_ffMatrices[0], blendedWorldPos);
        v_clipPos = blendedWorldPos;
    } else {
#endif
        gl_Position = mul(u_ffMatrices[0], localPos);
        v_clipPos = mul(u_ffMatrices[1], localPos);
#if CKFF_VS_VERTEX_BLEND_MODE != 0
    }
#endif
#if CKFF_VS_CLIP_DISTANCE
    int clipCount = int(u_clipParams.x);
    v_clipDistance0.x = clipCount > 0 ? dot(v_clipPos, u_clipPlanes[0]) : 0.0;
    v_clipDistance0.y = clipCount > 1 ? dot(v_clipPos, u_clipPlanes[1]) : 0.0;
    v_clipDistance0.z = clipCount > 2 ? dot(v_clipPos, u_clipPlanes[2]) : 0.0;
    v_clipDistance0.w = clipCount > 3 ? dot(v_clipPos, u_clipPlanes[3]) : 0.0;
    v_clipDistance1.x = clipCount > 4 ? dot(v_clipPos, u_clipPlanes[4]) : 0.0;
    v_clipDistance1.y = clipCount > 5 ? dot(v_clipPos, u_clipPlanes[5]) : 0.0;
    v_clipDistance1.zw = vec2(0.0, 0.0);
#endif
#if CKFF_VS_NEEDS_VIEW_SPACE
#if CKFF_VS_VERTEX_BLEND_MODE != 0
    if (!vertexBlendActive) {
        viewPos = mul(u_ffMatrices[2], localPos);
        viewNormal = mul(u_ffMatrices[3], vec4(a_normal, 0.0)).xyz;
    }
#else
    viewPos = mul(u_ffMatrices[2], localPos);
    viewNormal = mul(u_ffMatrices[3], vec4(a_normal, 0.0)).xyz;
#endif
#if defined(CKFF_FULL_SPECIALIZED)
    if ((CKFF_VS_BITS & (1 << 14)) != 0) {
        viewNormal = normalize(viewNormal);
    }
#else
    if (ckffVsBit(14, u_ffDrawParams[7].y > 0.5)) {
        viewNormal = normalize(viewNormal);
    }
#endif
#else
    viewPos = vec4_splat(0.0);
    viewNormal = vec3_splat(0.0);
#endif

#if defined(CKFF_FULL_SPECIALIZED)
    #if CKFF_VS_DIFFUSE_SOURCE == 1
        vec4 matDiffuse = a_color0;
    #elif CKFF_VS_DIFFUSE_SOURCE == 2
        vec4 matDiffuse = a_color1;
    #else
        vec4 matDiffuse = u_ffDrawParams[0];
    #endif

    #if (CKFF_VS_BITS & (1 << 13)) != 0
        #if CKFF_VS_AMBIENT_SOURCE == 1
            vec4 matAmbient = a_color0;
        #elif CKFF_VS_AMBIENT_SOURCE == 2
            vec4 matAmbient = a_color1;
        #else
            vec4 matAmbient = u_ffDrawParams[1];
        #endif

        #if CKFF_VS_SPECULAR_SOURCE == 1
            vec4 matSpecular = a_color0;
        #elif CKFF_VS_SPECULAR_SOURCE == 2
            vec4 matSpecular = a_color1;
        #else
            vec4 matSpecular = u_ffDrawParams[2];
        #endif

        #if CKFF_VS_EMISSIVE_SOURCE == 1
            vec4 matEmissive = a_color0;
        #elif CKFF_VS_EMISSIVE_SOURCE == 2
            vec4 matEmissive = a_color1;
        #else
            vec4 matEmissive = u_ffDrawParams[3];
        #endif
        float matPower = u_ffDrawParams[4].x;
    #else
        vec4 matAmbient = vec4_splat(0.0);
        vec4 matSpecular = vec4_splat(0.0);
        vec4 matEmissive = vec4_splat(0.0);
        float matPower = 0.0;
    #endif
#else
    vec4 matDiffuse  = selectMaterialSource(0, u_ffDrawParams[5].x, u_ffDrawParams[0], a_color0, a_color1);
    vec4 matAmbient  = selectMaterialSource(1, u_ffDrawParams[5].y, u_ffDrawParams[1], a_color0, a_color1);
    vec4 matSpecular = selectMaterialSource(2, u_ffDrawParams[5].z, u_ffDrawParams[2], a_color0, a_color1);
    vec4 matEmissive = selectMaterialSource(3, u_ffDrawParams[5].w, u_ffDrawParams[3], a_color0, a_color1);
    float matPower   = u_ffDrawParams[4].x;
#endif

#if defined(CKFF_FULL_SPECIALIZED)
    bool lightingEnabled = (CKFF_VS_BITS & (1 << 13)) != 0;
    int lightCount = (CKFF_VS_BITS >> 17) & 15;
    vec3 globalAmbient = vec3(0.0, 0.0, 0.0);
#else
    int rawLightCount = int(u_ffDrawParams[6].x);
    bool lightingEnabled = ckffVsBit(13, rawLightCount >= 0);
    int lightCount = ckffVsBits(17, 15, rawLightCount < 0 ? 0 : rawLightCount);
    vec3 globalAmbient = u_ffDrawParams[6].yzw;
#endif

    vec4 litDiffuse = matDiffuse;
    vec3 litSpecular = a_color1.rgb;

    if (lightingEnabled) {
        vec4 ambientAccum = vec4(0.0, 0.0, 0.0, 0.0);
        vec4 diffuseAccum = vec4(0.0, 0.0, 0.0, 0.0);
        vec3 specularAccum = vec3(0.0, 0.0, 0.0);

        for (int i = 0; i < 8; ++i) {
            if (i >= lightCount) break;

            int base = i * 7;
            bool inlineLight = (i == 0 && u_ffDrawParams[7].w > 0.5);
            vec4 lPos   = inlineLight ? u_ffDrawParams[12] : u_lights[base + 0];
            vec4 lDir   = inlineLight ? u_ffDrawParams[13] : u_lights[base + 1];
            vec4 lDiff  = inlineLight ? u_ffDrawParams[14] : u_lights[base + 2];
            vec4 lSpec  = inlineLight ? u_ffDrawParams[15] : u_lights[base + 3];
            vec4 lAmb   = inlineLight ? u_ffDrawParams[16] : u_lights[base + 4];
            vec4 lAtten = inlineLight ? u_ffDrawParams[17] : u_lights[base + 5];
            vec4 lSpot  = inlineLight ? u_ffDrawParams[18] : u_lights[base + 6];

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
#if defined(CKFF_FULL_SPECIALIZED)
                if ((CKFF_VS_BITS & (1 << 16)) != 0) {
                    halfVec = toLight - normalize(viewPos.xyz);
                } else {
                    halfVec = toLight - vec3(0.0, 0.0, 1.0);
                }
#else
                if (ckffVsBit(16, u_ffDrawParams[7].x > 0.5)) {
                    halfVec = toLight - normalize(viewPos.xyz);
                } else {
                    halfVec = toLight - vec3(0.0, 0.0, 1.0);
                }
#endif
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
    v_flatColor0 = v_color0;
    v_flatColor1 = v_color1;
#if defined(CKFF_FULL_SPECIALIZED)
    int tc0 = ckffVsTexcoordIndex(0, 0);
    int tc1 = ckffVsTexcoordIndex(1, 0);
    int tc2 = ckffVsTexcoordIndex(2, 0);
    int tc3 = ckffVsTexcoordIndex(3, 0);
    int tc4 = ckffVsTexcoordIndex(4, 0);
    int tc5 = ckffVsTexcoordIndex(5, 0);
    int tc6 = ckffVsTexcoordIndex(6, 0);
    int tc7 = ckffVsTexcoordIndex(7, 0);
#else
    int tc0 = int(u_stageParams[0 * 4 + 2].y);
    int tc1 = int(u_stageParams[1 * 4 + 2].y);
    int tc2 = int(u_stageParams[2 * 4 + 2].y);
    int tc3 = int(u_stageParams[3 * 4 + 2].y);
    int tc4 = int(u_stageParams[4 * 4 + 2].y);
    int tc5 = int(u_stageParams[5 * 4 + 2].y);
    int tc6 = int(u_stageParams[6 * 4 + 2].y);
    int tc7 = int(u_stageParams[7 * 4 + 2].y);
#endif
    v_texcoord0 = transformTexcoord(0, generateTexcoord(0, tc0, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 1
    v_texcoord1 = transformTexcoord(1, generateTexcoord(1, tc1, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#else
    v_texcoord1 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 2
    v_texcoord2 = transformTexcoord(2, generateTexcoord(2, tc2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#else
    v_texcoord2 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 3
    v_texcoord3 = transformTexcoord(3, generateTexcoord(3, tc3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#else
    v_texcoord3 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 4
    v_texcoord4 = transformTexcoord(4, generateTexcoord(4, tc4, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#else
    v_texcoord4 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 5
    v_texcoord5 = transformTexcoord(5, generateTexcoord(5, tc5, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#else
    v_texcoord5 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 6
    v_texcoord6 = transformTexcoord(6, generateTexcoord(6, tc6, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#else
    v_texcoord6 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 7
    v_texcoord7Fog = transformTexcoord(7, generateTexcoord(7, tc7, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
#else
    v_texcoord7Fog = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if defined(CKFF_FULL_SPECIALIZED)
#if CKFF_VS_FOG_MODE != 0
    float fogDepth = ((CKFF_VS_BITS & (1 << 23)) != 0) ? length(viewPos.xyz) : abs(viewPos.z);
    v_texcoord7Fog.z = ckffFogFactor(fogDepth, CKFF_VS_FOG_MODE, u_ffDrawParams[10]);
#else
    v_texcoord7Fog.z = 1.0;
#endif
#else
    float fogDepth = ckffVsBit(23, u_ffDrawParams[7].z > 0.5) ? length(viewPos.xyz) : abs(viewPos.z);
    v_texcoord7Fog.z = ckffFogFactor(fogDepth, ckffVsFogMode(u_ffDrawParams[10].w), u_ffDrawParams[10]);
#endif
}

