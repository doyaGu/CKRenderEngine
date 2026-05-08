$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_clipPos

#include "bgfx_shader.sh"

uniform vec4 u_fogColor;
uniform vec4 u_fogParams;
uniform vec4 u_alphaParams;
uniform vec4 u_texFactor;
uniform vec4 u_bumpEnv[2];
uniform vec4 u_stageParams[32];
uniform vec4 u_ffSpec[10];
uniform vec4 u_clipPlanes[6];
uniform vec4 u_clipParams;

SAMPLER2D(s_texture0, 0);
SAMPLER2D(s_texture1, 1);
SAMPLER2D(s_texture2, 2);
SAMPLER2D(s_texture3, 3);
SAMPLER2D(s_texture4, 4);
SAMPLER2D(s_texture5, 5);
SAMPLER2D(s_texture6, 6);
SAMPLER2D(s_texture7, 7);

#include "fs_ff_common.sc"

vec4 getTextureColor(int stage, vec2 uv, bool hasTexture)
{
    if (!hasTexture) return vec4(0.0, 0.0, 0.0, 1.0);
    if (stage == 0) return texture2D(s_texture0, uv);
    if (stage == 1) return texture2D(s_texture1, uv);
    if (stage == 2) return texture2D(s_texture2, uv);
    if (stage == 3) return texture2D(s_texture3, uv);
    if (stage == 4) return texture2D(s_texture4, uv);
    if (stage == 5) return texture2D(s_texture5, uv);
    if (stage == 6) return texture2D(s_texture6, uv);
    return texture2D(s_texture7, uv);
}

vec2 getSampleCoord(vec4 coord, int transformFlags)
{
    if ((transformFlags & 0x100) == 0) return coord.xy;
    return coord.xy / max(abs(coord.w), 0.0001);
}

float computePixelFogFactor(float depth, int mode, float vertexFogFactor)
{
    if (mode == 0) return vertexFogFactor;
    if (mode == 1) {
        float denom = max(u_fogParams.y - u_fogParams.x, 0.0001);
        return clamp((u_fogParams.y - depth) / denom, 0.0, 1.0);
    }
    if (mode == 2) return clamp(exp(-(u_fogParams.z * depth)), 0.0, 1.0);
    float e = u_fogParams.z * depth;
    return clamp(exp(-(e * e)), 0.0, 1.0);
}

vec4 applyArgModifiers(vec4 value, int arg)
{
    if ((arg & 0x20) != 0) {
        value.rgb = value.aaa;
    }
    if ((arg & 0x10) != 0) {
        value.rgb = 1.0 - value.rgb;
        value.a = 1.0 - value.a;
    }
    return value;
}

vec4 getArg(int arg, vec4 textureColor, vec4 current, vec4 diffuse, vec4 specular, vec4 temp)
{
    int baseArg = arg & ~(0x10 | 0x20);
    vec4 value = current;
    if (baseArg == 0) value = diffuse;
    else if (baseArg == 1) value = current;
    else if (baseArg == 2) value = textureColor;
    else if (baseArg == 3) value = u_texFactor;
    else if (baseArg == 4) value = specular;
    else if (baseArg == 5) value = temp;
    return applyArgModifiers(value, arg);
}

vec4 applyOp(int op, vec4 a, vec4 b, vec4 c, vec4 current, vec4 diffuse, vec4 textureColor)
{
    if (op == 1) return current;
    if (op == 2) return a;
    if (op == 3) return b;
    if (op == 4) return a * b;
    if (op == 5) return clamp(a * b * 2.0, 0.0, 1.0);
    if (op == 6) return clamp(a * b * 4.0, 0.0, 1.0);
    if (op == 7) return clamp(a + b, 0.0, 1.0);
    if (op == 8) return clamp(a + b - 0.5, 0.0, 1.0);
    if (op == 9) return clamp((a + b - 0.5) * 2.0, 0.0, 1.0);
    if (op == 10) return clamp(a - b, 0.0, 1.0);
    if (op == 11) return clamp(a + b - a * b, 0.0, 1.0);
    if (op == 12) return mix(b, a, diffuse.a);
    if (op == 13) return mix(b, a, textureColor.a);
    if (op == 14) return mix(b, a, u_texFactor.a);
    if (op == 15) return clamp(a + b * (1.0 - textureColor.a), 0.0, 1.0);
    if (op == 16) return mix(b, a, current.a);
    if (op == 18) return clamp(a + vec4_splat(a.a) * b, 0.0, 1.0);
    if (op == 19) return clamp(a * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 20) return clamp(a + (1.0 - a.a) * b, 0.0, 1.0);
    if (op == 21) return clamp((vec4_splat(1.0) - a) * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 22 || op == 23) return current;
    if (op == 24) {
        float v = clamp(dot(a.rgb - 0.5, b.rgb - 0.5) * 4.0, 0.0, 1.0);
        return vec4_splat(v);
    }
    if (op == 25) return clamp(a * b + c, 0.0, 1.0);
    if (op == 26) return clamp(c * a + (vec4_splat(1.0) - c) * b, 0.0, 1.0);
    return a * b;
}

bool alphaPass(float alpha, int func)
{
    float ref = u_alphaParams.x;
    if (func == 0 || func == 8) return true;
    if (func == 1) return false;
    if (func == 2) return alpha < ref;
    if (func == 3) return abs(alpha - ref) < 0.0001;
    if (func == 4) return alpha <= ref;
    if (func == 5) return alpha > ref;
    if (func == 6) return abs(alpha - ref) >= 0.0001;
    if (func == 7) return alpha >= ref;
    return true;
}

void main()
{
    int clipCount = int(u_clipParams.x);
    for (int clipIndex = 0; clipIndex < 6; ++clipIndex) {
        if (clipIndex >= clipCount) break;
        if (dot(v_clipPos, u_clipPlanes[clipIndex]) < 0.0) discard;
    }

    vec4 current = v_color0;
    vec4 temp = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 previousTexture = vec4(0.0, 0.0, 0.0, 1.0);
    int previousColorOp = 0;

    for (int stage = 0; stage < 8; ++stage) {
        if (ckffSpecIsOptimized() && stage > ckffSpecLastActiveTextureStage()) break;

        vec4 colorParams = u_stageParams[stage * 4 + 0];
        vec4 alphaParams = u_stageParams[stage * 4 + 1];
        vec4 colorExtra = u_stageParams[stage * 4 + 2];
        vec4 alphaExtra = u_stageParams[stage * 4 + 3];
        CKFFStageParams stageParams = ckffReadStageParams(stage, colorParams, alphaParams, colorExtra, alphaExtra);
        int colorOp = stageParams.ColorOp;
        int alphaOp = stageParams.AlphaOp;
        bool hasTexture = stageParams.HasTexture;

        if (colorOp == 1) break;

        vec4 stageCoord = v_texcoord0;
        if (stage == 1) stageCoord = v_texcoord1;
        else if (stage == 2) stageCoord = v_texcoord2;
        else if (stage == 3) stageCoord = v_texcoord3;
        else if (stage == 4) stageCoord = v_texcoord4;
        else if (stage == 5) stageCoord = v_texcoord5;
        else if (stage == 6) stageCoord = v_texcoord6;
        else if (stage == 7) stageCoord = v_texcoord7Fog;

        vec2 stageUv = getSampleCoord(stageCoord, stageParams.TexcoordTransformFlags);

        if (stage != 0 && (previousColorOp == 22 || previousColorOp == 23)) {
            vec2 bump = previousTexture.xy;
            stageUv.x += dot(u_bumpEnv[0].xy, bump);
            stageUv.y += dot(u_bumpEnv[0].zw, bump);
        }

        vec4 texColor = getTextureColor(stage, stageUv, hasTexture);
        if (stage != 0 && previousColorOp == 23) {
            float lum = clamp(previousTexture.z * u_bumpEnv[1].x + u_bumpEnv[1].y, 0.0, 1.0);
            texColor *= lum;
        }
        vec4 colorA = getArg(stageParams.ColorArg1, texColor, current, v_color0, v_color1, temp);
        vec4 colorB = getArg(stageParams.ColorArg2, texColor, current, v_color0, v_color1, temp);
        vec4 colorC = getArg(stageParams.ColorArg0, texColor, current, v_color0, v_color1, temp);
        vec4 alphaA = getArg(stageParams.AlphaArg1, texColor, current, v_color0, v_color1, temp);
        vec4 alphaB = getArg(stageParams.AlphaArg2, texColor, current, v_color0, v_color1, temp);
        vec4 alphaC = getArg(stageParams.AlphaArg0, texColor, current, v_color0, v_color1, temp);

        vec4 stageResult = current;
        vec4 colorResult = applyOp(colorOp, colorA, colorB, colorC, current, v_color0, texColor);
        vec4 alphaResult = applyOp(alphaOp, alphaA, alphaB, alphaC, current, v_color0, texColor);
        stageResult.rgb = colorResult.rgb;
        stageResult.a = alphaResult.a;
        if (colorOp == 24) {
            stageResult = colorResult;
        }

        int resultArg = stageParams.ResultArg;
        if (resultArg == 5) {
            temp = stageResult;
        } else {
            current = stageResult;
        }

        previousTexture = texColor;
        previousColorOp = colorOp;
    }

    bool specularEnabled = ckffSpecIsOptimized() ? ckffSpecGlobalSpecularEnabled() : (u_alphaParams.z > 0.5);
    if (specularEnabled) {
        current.rgb += v_color1.rgb;
    }
    if (ckffSpecIsOptimized()) {
        if (ckffSpecAlphaTestEnabled() && !alphaPass(current.a, ckffSpecAlphaFunc())) discard;
    } else {
        if (!alphaPass(current.a, int(u_alphaParams.y))) discard;
    }
    bool fogEnabled = ckffSpecIsOptimized() ? ckffSpecFogEnabled() : true;
    if (fogEnabled) {
        int pixelFogMode = ckffSpecIsOptimized() ? ckffSpecPixelFogMode() : int(u_alphaParams.w);
        float fogFactor = computePixelFogFactor(abs(v_clipPos.z), pixelFogMode, v_texcoord7Fog.z);
        current.rgb = mix(u_fogColor.rgb, current.rgb, fogFactor);
    }
    gl_FragColor = clamp(current, 0.0, 1.0);
}
