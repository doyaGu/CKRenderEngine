#ifndef CKFF_VS_CLIP_DISTANCE
#define CKFF_VS_CLIP_DISTANCE 0
#endif
$input a_position, a_normal, a_tangent, a_bitangent, a_indices, a_weight, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos, v_lineOffset, v_clipDistance0, v_clipDistance1

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"

#ifndef CKFF_NDC_MINUS_ONE_TO_ONE
#define CKFF_NDC_MINUS_ONE_TO_ONE 0
#endif

void ckffApplyBackendClipSpace(inout vec4 position)
{
#if CKFF_NDC_MINUS_ONE_TO_ONE
    position.z = position.z * 2.0 - position.w;
#endif
}

uniform mat4 u_ffMatrices[8];
uniform mat4 u_vertexBlendMatrices[4];
uniform mat4 u_texMatrix[8];
uniform vec4 u_ffDrawParams[20];
uniform vec4 u_lights[56];
uniform vec4 u_stageParams[16];
uniform vec4 u_viewport;
#if CKFF_VS_CLIP_DISTANCE
uniform vec4 u_clipPlanes[6];
#endif
#if CKFF_VS_CLIP_DISTANCE || !CKFF_NATIVE_SDL_GPU
uniform vec4 u_clipParams;
#endif

#if !CKFF_NATIVE_SDL_GPU
vec2 ckffDepthClipDistances(vec4 position)
{
    if (u_clipParams.y > 0.5) {
        return vec2(position.z, position.w - position.z);
    }
    return vec2(abs(position.w), abs(position.w));
}
#endif

int ckffVsTexGenMode(int packedIndex)
{
    return packedIndex / 65536;
}

int ckffVsTexcoordIndex(int packedIndex)
{
    return packedIndex & 7;
}

int ckffVsVertexBlendMode()
{
    return int(u_ffDrawParams[19].y + 0.5);
}

int ckffVsVertexBlendCount()
{
    return int(u_ffDrawParams[19].z + 0.5);
}

bool ckffVsVertexBlendIndexed()
{
    return u_ffDrawParams[19].w > 0.5;
}

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
    // Indices beyond the palette clamp to the last matrix (spec appendix C).
    if (index <= 0) return u_vertexBlendMatrices[0];
    if (index == 1) return u_vertexBlendMatrices[1];
    if (index == 2) return u_vertexBlendMatrices[2];
    return u_vertexBlendMatrices[3];
}

vec4 selectMaterialSource(float source, vec4 materialValue, vec4 color0, vec4 color1)
{
    int src = int(source + 0.5);
    if (src == 1) return color0;
    if (src == 2) return color1;
    return materialValue;
}

vec4 selectTexcoord(int index, vec4 tc0, vec4 tc1, vec4 tc2, vec4 tc3, vec4 tc4, vec4 tc5, vec4 tc6, vec4 tc7)
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

vec4 generateTexcoord(int packedIndex, vec4 tc0, vec4 tc1, vec4 tc2, vec4 tc3, vec4 tc4, vec4 tc5, vec4 tc6, vec4 tc7, vec3 viewPos, vec3 viewNormal)
{
    int generation = ckffVsTexGenMode(packedIndex);
    int index = ckffVsTexcoordIndex(packedIndex);
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
    // Declared texcoord components: the canonical vertex layout always feeds
    // two components per texcoord attribute.
    vec4 result = selectTexcoord(index, tc0, tc1, tc2, tc3, tc4, tc5, tc6, tc7);
    result.z = 0.0;
    result.w = 1.0;
    return result;
}

vec4 transformTexcoord(int stage, vec4 coord)
{
    int flags = int(u_stageParams[stage * 2].y);
    if (flags == 0) return coord;

    int count = flags & 0xff;
    bool applyTransform = count >= 1 && count <= 4;

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

bool ckffVsStageHasTexture(int stage)
{
    return u_stageParams[stage * 2].z > 0.5;
}

void main()
{
    vec4 localPos = vec4(a_position.xyz, 1.0);
    vec3 localNormal = a_normal;
    int vertexBlendMode = ckffVsVertexBlendMode();
    int vertexBlendCount = ckffVsVertexBlendCount();
    if (vertexBlendMode == 2) {
        if (vertexBlendCount == 1 || vertexBlendCount == 3)
            localPos.xyz = mix(a_position.xyz, a_tangent.xyz, u_ffDrawParams[19].x);
        if (vertexBlendCount >= 2)
            localNormal = mix(a_normal, a_bitangent, u_ffDrawParams[19].x);
    }

    vec4 viewPos;
    vec3 viewNormal;
    vec4 worldClipPos;
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
                blendedNormal += mul(blendMatrix, vec4(localNormal, 0.0)).xyz * weight;
            }
        }
        viewPos = mul(u_ffMatrices[2], blendedWorldPos);
        viewNormal = mul(u_ffMatrices[3], vec4(blendedNormal, 0.0)).xyz;
        gl_Position = mul(u_ffMatrices[0], blendedWorldPos);
        worldClipPos = blendedWorldPos;
    } else {
        gl_Position = mul(u_ffMatrices[0], localPos);
        worldClipPos = mul(u_ffMatrices[1], localPos);
        viewPos = mul(u_ffMatrices[2], localPos);
        viewNormal = mul(u_ffMatrices[3], vec4(localNormal, 0.0)).xyz;
    }
#if CKFF_VS_CLIP_DISTANCE
    int clipCount = int(u_clipParams.x);
    v_clipDistance0.x = clipCount > 0 ? dot(worldClipPos, u_clipPlanes[0]) : 0.0;
    v_clipDistance0.y = clipCount > 1 ? dot(worldClipPos, u_clipPlanes[1]) : 0.0;
    v_clipDistance0.z = clipCount > 2 ? dot(worldClipPos, u_clipPlanes[2]) : 0.0;
    v_clipDistance0.w = clipCount > 3 ? dot(worldClipPos, u_clipPlanes[3]) : 0.0;
    v_clipDistance1.x = clipCount > 4 ? dot(worldClipPos, u_clipPlanes[4]) : 0.0;
    v_clipDistance1.y = clipCount > 5 ? dot(worldClipPos, u_clipPlanes[5]) : 0.0;
    v_clipDistance1.zw = vec2(0.0, 0.0);
#elif !CKFF_NATIVE_SDL_GPU
    v_clipDistance0 = vec4_splat(0.0);
    v_clipDistance1.xy = vec2(0.0, 0.0);
#endif
    if (u_ffDrawParams[7].y > 0.5) {
        viewNormal = normalize(viewNormal);
    }
    float expansionMode = u_ffDrawParams[4].w;
    bool expansionUsesWeight =
        (expansionMode > 1.5 && expansionMode < 2.5) ||
        expansionMode > 3.5;
    vec3 expansionData = expansionUsesWeight ? a_weight : a_tangent;
    bool edgeAntialias = expansionMode > 2.5;
    // The CPU supplies screen phase multiplied by clip W. Perspective
    // interpolation and fragment reciprocal W reconstruct affine phase.
    float linePhase = edgeAntialias
        ? expansionData.z
        : (vertexBlendMode == 2 ? a_weight.x : a_tangent.x);
    v_fogPos = vec4(gl_Position.w, linePhase,
                    abs(viewPos.z), 1.0);
    // The expanded quad carries its signed pixel offset at each side. Store
    // offset * W so the fragment shader reconstructs a screen-linear value.
    v_lineOffset = edgeAntialias
        ? expansionData.xy * gl_Position.w : vec2(0.0, 0.0);

    vec4 matDiffuse  = selectMaterialSource(u_ffDrawParams[5].x, u_ffDrawParams[0], a_color0, a_color1);
    vec4 matAmbient  = selectMaterialSource(u_ffDrawParams[5].y, u_ffDrawParams[1], a_color0, a_color1);
    vec4 matSpecular = selectMaterialSource(u_ffDrawParams[5].z, u_ffDrawParams[2], a_color0, a_color1);
    vec4 matEmissive = selectMaterialSource(u_ffDrawParams[5].w, u_ffDrawParams[3], a_color0, a_color1);
    float matPower   = u_ffDrawParams[4].x;

    int rawLightCount = int(u_ffDrawParams[6].x);
    bool lightingEnabled = rawLightCount >= 0;
    bool specularOutputEnabled = u_ffDrawParams[8].z > 0.5;
    int lightCount = rawLightCount < 0 ? 0 : rawLightCount;
    vec3 globalAmbient = u_ffDrawParams[6].yzw;

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
                    float spotAtten = pow(clamp((rho - lSpot.y) / max(lSpot.x - lSpot.y, 0.0001), 0.0, 1.0), lAtten.w);
                    spotAtten = rho <= lSpot.y ? 0.0 : spotAtten;
                    spotAtten = rho > lSpot.x ? 1.0 : spotAtten;
                    atten *= spotAtten;
                }
            }

            float nDotL = max(dot(viewNormal, normalize(toLight)), 0.0);
            ambientAccum += lAmb * atten;
            diffuseAccum += lDiff * nDotL * atten;

            if (nDotL > 0.0 && matPower > 0.0) {
                vec3 halfVec;
                if (u_ffDrawParams[7].x > 0.5) {
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

    if (lightingEnabled) {
        v_color0 = clamp(litDiffuse, 0.0, 1.0);
        v_color0.a = matDiffuse.a;
        v_color1 = specularOutputEnabled
            ? vec4(clamp(litSpecular, 0.0, 1.0), a_color1.a)
            : a_color1;
    } else {
        v_color0 = a_color0;
        v_color1 = a_color1;
    }
    v_flatColor0 = v_color0;
    v_flatColor1 = v_color1;

    v_texcoord0 = vec4_splat(0.0);
    v_texcoord1 = vec4_splat(0.0);
    v_texcoord2 = vec4_splat(0.0);
    v_texcoord3 = vec4_splat(0.0);
    v_texcoord4 = vec4_splat(0.0);
    v_texcoord5 = vec4_splat(0.0);
    v_texcoord6 = vec4_splat(0.0);
    v_texcoord7Fog = vec4_splat(0.0);
    if (ckffVsStageHasTexture(0))
        v_texcoord0 = transformTexcoord(0, generateTexcoord(int(u_stageParams[0 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    if (ckffVsStageHasTexture(1))
        v_texcoord1 = transformTexcoord(1, generateTexcoord(int(u_stageParams[1 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    if (ckffVsStageHasTexture(2))
        v_texcoord2 = transformTexcoord(2, generateTexcoord(int(u_stageParams[2 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    if (ckffVsStageHasTexture(3))
        v_texcoord3 = transformTexcoord(3, generateTexcoord(int(u_stageParams[3 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    if (ckffVsStageHasTexture(4))
        v_texcoord4 = transformTexcoord(4, generateTexcoord(int(u_stageParams[4 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    if (ckffVsStageHasTexture(5))
        v_texcoord5 = transformTexcoord(5, generateTexcoord(int(u_stageParams[5 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    if (ckffVsStageHasTexture(6))
        v_texcoord6 = transformTexcoord(6, generateTexcoord(int(u_stageParams[6 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    if (ckffVsStageHasTexture(7))
        v_texcoord7Fog = transformTexcoord(7, generateTexcoord(int(u_stageParams[7 * 2].x), a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, viewPos.xyz, viewNormal));
    float fogDepth = u_ffDrawParams[7].z > 0.5 ? length(viewPos.xyz) : abs(viewPos.z);
    v_texcoord7Fog.z = ckffFogFactor(fogDepth, int(u_ffDrawParams[10].w + 0.5), u_ffDrawParams[10]);
    if (u_ffDrawParams[4].z > 0.5) {
        v_texcoord0 *= gl_Position.w;
        v_texcoord1 *= gl_Position.w;
        v_texcoord2 *= gl_Position.w;
        v_texcoord3 *= gl_Position.w;
        v_texcoord4 *= gl_Position.w;
        v_texcoord5 *= gl_Position.w;
        v_texcoord6 *= gl_Position.w;
        v_texcoord7Fog.xyw *= gl_Position.w;
    }
    // D3D8 ZBIAS compatibility offset, resolved for the active depth format.
    gl_Position.z -= u_ffDrawParams[4].y * gl_Position.w;
    // Expanded point-filled triangles retain the source point for lighting,
    // fog, texgen and user clipping. The active vertex mode leaves either
    // tangent or blend weight available for the final pixel-space offset.
    if (expansionMode > 0.5) {
        vec2 pointOffset = expansionData.xy;
        gl_Position.xy += pointOffset * u_viewport.xy * gl_Position.w;
    }
#if !CKFF_NATIVE_SDL_GPU
    v_clipDistance1.zw = ckffDepthClipDistances(gl_Position);
#endif
    ckffApplyBackendClipSpace(gl_Position);
}
