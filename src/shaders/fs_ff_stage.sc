$input v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"

uniform vec4 u_ffDrawParams[12];
uniform vec4 u_bumpEnv[16];
uniform vec4 u_stageParams[32];
uniform vec4 u_ffSpec[10];

#if defined(CKFF_FULL_SPECIALIZED)
#ifndef CKFF_FS_STAGE0_SAMPLER_TYPE
#define CKFF_FS_STAGE0_SAMPLER_TYPE 0
#endif
#ifndef CKFF_FS_STAGE1_SAMPLER_TYPE
#define CKFF_FS_STAGE1_SAMPLER_TYPE 0
#endif
#ifndef CKFF_FS_STAGE2_SAMPLER_TYPE
#define CKFF_FS_STAGE2_SAMPLER_TYPE 0
#endif
#ifndef CKFF_FS_STAGE3_SAMPLER_TYPE
#define CKFF_FS_STAGE3_SAMPLER_TYPE 0
#endif
#ifndef CKFF_FS_STAGE4_SAMPLER_TYPE
#define CKFF_FS_STAGE4_SAMPLER_TYPE 0
#endif
#ifndef CKFF_FS_STAGE5_SAMPLER_TYPE
#define CKFF_FS_STAGE5_SAMPLER_TYPE 0
#endif
#ifndef CKFF_FS_STAGE6_SAMPLER_TYPE
#define CKFF_FS_STAGE6_SAMPLER_TYPE 0
#endif
#ifndef CKFF_FS_STAGE7_SAMPLER_TYPE
#define CKFF_FS_STAGE7_SAMPLER_TYPE 0
#endif
#if CKFF_FS_STAGE0_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube0, 8);
#elif CKFF_FS_STAGE0_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume0, 0);
#else
SAMPLER2D(s_texture0, 0);
#endif
#if CKFF_FS_STAGE1_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube1, 9);
#elif CKFF_FS_STAGE1_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume1, 1);
#else
SAMPLER2D(s_texture1, 1);
#endif
#if CKFF_FS_STAGE2_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube2, 10);
#elif CKFF_FS_STAGE2_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume2, 2);
#else
SAMPLER2D(s_texture2, 2);
#endif
#if CKFF_FS_STAGE3_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube3, 11);
#elif CKFF_FS_STAGE3_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume3, 3);
#else
SAMPLER2D(s_texture3, 3);
#endif
#if CKFF_FS_STAGE4_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube4, 12);
#elif CKFF_FS_STAGE4_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume4, 4);
#else
SAMPLER2D(s_texture4, 4);
#endif
#if CKFF_FS_STAGE5_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube5, 13);
#elif CKFF_FS_STAGE5_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume5, 5);
#else
SAMPLER2D(s_texture5, 5);
#endif
#if CKFF_FS_STAGE6_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube6, 14);
#elif CKFF_FS_STAGE6_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume6, 6);
#else
SAMPLER2D(s_texture6, 6);
#endif
#if CKFF_FS_STAGE7_SAMPLER_TYPE == 1
SAMPLERCUBE(s_textureCube7, 15);
#elif CKFF_FS_STAGE7_SAMPLER_TYPE == 3
SAMPLER3D(s_textureVolume7, 7);
#else
SAMPLER2D(s_texture7, 7);
#endif
#else
SAMPLER2D(s_texture0, 0);
SAMPLER2D(s_texture1, 1);
SAMPLER2D(s_texture2, 2);
SAMPLER2D(s_texture3, 3);
SAMPLER2D(s_texture4, 4);
SAMPLER2D(s_texture5, 5);
SAMPLER2D(s_texture6, 6);
SAMPLER2D(s_texture7, 7);
SAMPLERCUBE(s_textureCube0, 8);
SAMPLERCUBE(s_textureCube1, 9);
SAMPLERCUBE(s_textureCube2, 10);
SAMPLERCUBE(s_textureCube3, 11);
SAMPLERCUBE(s_textureCube4, 12);
SAMPLERCUBE(s_textureCube5, 13);
SAMPLERCUBE(s_textureCube6, 14);
SAMPLERCUBE(s_textureCube7, 15);
#endif

#include "fs_ff_common.sc"

#ifndef CKFF_FS_ACTIVE_STAGE_COUNT
#define CKFF_FS_ACTIVE_STAGE_COUNT 8
#endif

float compareDepth(float depth, float ref, int func)
{
    if (func == 1) return depth < ref ? 1.0 : 0.0;
    if (func == 2) return depth <= ref ? 1.0 : 0.0;
    if (func == 3) return depth == ref ? 1.0 : 0.0;
    if (func == 4) return depth >= ref ? 1.0 : 0.0;
    if (func == 5) return depth > ref ? 1.0 : 0.0;
    if (func == 6) return depth != ref ? 1.0 : 0.0;
    if (func == 7) return 0.0;
    if (func == 8) return 1.0;
    return depth;
}

#if defined(CKFF_FULL_SPECIALIZED)
#define CKFF_DEPTH_TEXTURE_COLOR(_sample) ((samplerType == 2) ? ((compareFunc != 0) ? vec4_splat(compareDepth((_sample).r, coord.z, compareFunc)) : (_sample).rrrr) : (_sample))

vec4 getTextureColor(int stage, vec4 coord, int samplerType, int compareFunc, bool hasTexture)
{
    if (!hasTexture) return vec4(0.0, 0.0, 0.0, 1.0);
    if (stage == 0) {
#if CKFF_FS_STAGE0_SAMPLER_TYPE == 1
        return textureCube(s_textureCube0, coord.xyz);
#elif CKFF_FS_STAGE0_SAMPLER_TYPE == 3
        return texture3D(s_textureVolume0, coord.xyz);
#else
        vec4 color = texture2D(s_texture0, coord.xy);
        return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
    }
    if (stage == 1) {
#if CKFF_FS_STAGE1_SAMPLER_TYPE == 1
        return textureCube(s_textureCube1, coord.xyz);
#elif CKFF_FS_STAGE1_SAMPLER_TYPE == 3
        return texture3D(s_textureVolume1, coord.xyz);
#else
        vec4 color = texture2D(s_texture1, coord.xy);
        return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
    }
    if (stage == 2) {
#if CKFF_FS_STAGE2_SAMPLER_TYPE == 1
        return textureCube(s_textureCube2, coord.xyz);
#elif CKFF_FS_STAGE2_SAMPLER_TYPE == 3
        return texture3D(s_textureVolume2, coord.xyz);
#else
        vec4 color = texture2D(s_texture2, coord.xy);
        return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
    }
    if (stage == 3) {
#if CKFF_FS_STAGE3_SAMPLER_TYPE == 1
        return textureCube(s_textureCube3, coord.xyz);
#elif CKFF_FS_STAGE3_SAMPLER_TYPE == 3
        return texture3D(s_textureVolume3, coord.xyz);
#else
        vec4 color = texture2D(s_texture3, coord.xy);
        return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
    }
    if (stage == 4) {
#if CKFF_FS_STAGE4_SAMPLER_TYPE == 1
        return textureCube(s_textureCube4, coord.xyz);
#elif CKFF_FS_STAGE4_SAMPLER_TYPE == 3
        return texture3D(s_textureVolume4, coord.xyz);
#else
        vec4 color = texture2D(s_texture4, coord.xy);
        return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
    }
    if (stage == 5) {
#if CKFF_FS_STAGE5_SAMPLER_TYPE == 1
        return textureCube(s_textureCube5, coord.xyz);
#elif CKFF_FS_STAGE5_SAMPLER_TYPE == 3
        return texture3D(s_textureVolume5, coord.xyz);
#else
        vec4 color = texture2D(s_texture5, coord.xy);
        return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
    }
    if (stage == 6) {
#if CKFF_FS_STAGE6_SAMPLER_TYPE == 1
        return textureCube(s_textureCube6, coord.xyz);
#elif CKFF_FS_STAGE6_SAMPLER_TYPE == 3
        return texture3D(s_textureVolume6, coord.xyz);
#else
        vec4 color = texture2D(s_texture6, coord.xy);
        return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
    }
#if CKFF_FS_STAGE7_SAMPLER_TYPE == 1
    return textureCube(s_textureCube7, coord.xyz);
#elif CKFF_FS_STAGE7_SAMPLER_TYPE == 3
    return texture3D(s_textureVolume7, coord.xyz);
#else
    vec4 color = texture2D(s_texture7, coord.xy);
    return CKFF_DEPTH_TEXTURE_COLOR(color);
#endif
}
#else
vec4 getVolumeTextureColor(int stage, vec4 coord)
{
    // The generic shader already consumes 16 D3D sampler slots for 2D/cube.
    // Volume sampling is emitted by full-specialized variants instead.
    return vec4(0.0, 0.0, 0.0, 1.0);
}

vec4 getTextureColor(int stage, vec4 coord, int samplerType, int compareFunc, bool hasTexture)
{
    if (!hasTexture) return vec4(0.0, 0.0, 0.0, 1.0);
    if (samplerType == 1) {
        if (stage == 0) return textureCube(s_textureCube0, coord.xyz);
        if (stage == 1) return textureCube(s_textureCube1, coord.xyz);
        if (stage == 2) return textureCube(s_textureCube2, coord.xyz);
        if (stage == 3) return textureCube(s_textureCube3, coord.xyz);
        if (stage == 4) return textureCube(s_textureCube4, coord.xyz);
        if (stage == 5) return textureCube(s_textureCube5, coord.xyz);
        if (stage == 6) return textureCube(s_textureCube6, coord.xyz);
        return textureCube(s_textureCube7, coord.xyz);
    }
    if (samplerType == 3) {
        return getVolumeTextureColor(stage, coord);
    }
    vec2 uv = coord.xy;
    vec4 color;
    if (stage == 0) color = texture2D(s_texture0, uv);
    else if (stage == 1) color = texture2D(s_texture1, uv);
    else if (stage == 2) color = texture2D(s_texture2, uv);
    else if (stage == 3) color = texture2D(s_texture3, uv);
    else if (stage == 4) color = texture2D(s_texture4, uv);
    else if (stage == 5) color = texture2D(s_texture5, uv);
    else if (stage == 6) color = texture2D(s_texture6, uv);
    else color = texture2D(s_texture7, uv);
    if (samplerType == 2) {
        float depth = color.r;
        if (compareFunc != 0) return vec4_splat(compareDepth(depth, coord.z, compareFunc));
        return color.rrrr;
    }
    return color;
}
#endif

vec4 getSampleCoord(vec4 coord, int transformFlags)
{
    if ((transformFlags & 0x100) == 0) return coord;
    return coord / (abs(coord.w) < 0.0001 ? (coord.w < 0.0 ? -0.0001 : 0.0001) : coord.w);
}

float computePixelFogFactor(float depth, int mode, float vertexFogFactor)
{
    if (mode == 0) return vertexFogFactor;
    return ckffFogFactor(depth, mode, u_ffDrawParams[10]);
}

vec4 applyArgModifiers(vec4 value, int arg)
{
    if ((arg & 0x10) != 0) {
        value.rgb = 1.0 - value.rgb;
        value.a = 1.0 - value.a;
    }
    if ((arg & 0x20) != 0) {
        value = value.aaaa;
    }
    return value;
}

vec4 getArg(int arg, vec4 textureColor, vec4 current, vec4 diffuse, vec4 specular, vec4 temp, vec4 stageConstant)
{
    int baseArg = arg & ~(0x10 | 0x20);
    vec4 value = current;
    if (baseArg == 0) value = diffuse;
    else if (baseArg == 1) value = current;
    else if (baseArg == 2) value = textureColor;
    else if (baseArg == 3) value = u_ffDrawParams[9];
    else if (baseArg == 4) value = specular;
    else if (baseArg == 5) value = temp;
    else if (baseArg == 6) value = stageConstant;
    return applyArgModifiers(value, arg);
}

vec4 applyOp(int op, vec4 a, vec4 b, vec4 c, vec4 current, vec4 diffuse, vec4 textureColor)
{
    if (op == 1) return current;
    if (op == 2) return a;
    if (op == 3) return b;
    if (op == 4) return a * b;
    if (op == 5) return a * b * 2.0;
    if (op == 6) return a * b * 4.0;
    if (op == 7) return clamp(a + b, 0.0, 1.0);
    if (op == 8) return clamp(a + b - 0.5, 0.0, 1.0);
    if (op == 9) return clamp((a + b - 0.5) * 2.0, 0.0, 1.0);
    if (op == 10) return clamp(a - b, 0.0, 1.0);
    if (op == 11) return clamp(a + b - a * b, 0.0, 1.0);
    if (op == 12) return mix(b, a, diffuse.a);
    if (op == 13) return mix(b, a, textureColor.a);
    if (op == 14) return mix(b, a, u_ffDrawParams[9].a);
    if (op == 15) return clamp(a + b * (1.0 - textureColor.a), 0.0, 1.0);
    if (op == 16) return mix(b, a, current.a);
    if (op == 17) return current;
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
    return current;
}

bool alphaPass(float alpha, int func)
{
    float ref = u_ffDrawParams[8].x;
    int alphaPrecision = func / 16;
    func = func - alphaPrecision * 16;
    float alphaTestValue = alpha;
    if (alphaPrecision != 15) {
        alphaPrecision = min(alphaPrecision, 8);
        float precisionScale = exp2(float(8 + alphaPrecision));
        float factor = precisionScale - 1.0;
        float refScale = exp2(float(alphaPrecision));
        float refWrap = exp2(float(8 - alphaPrecision));
        alphaTestValue = round(alpha * factor);
        ref = floor(ref) * refScale + floor(floor(ref) / refWrap);
    } else {
        ref = ref / 255.0;
    }
    if (func == 0 || func == 8) return true;
    if (func == 1) return false;
    if (func == 2) return alphaTestValue < ref;
    if (func == 3) return alphaTestValue == ref;
    if (func == 4) return alphaTestValue <= ref;
    if (func == 5) return alphaTestValue > ref;
    if (func == 6) return alphaTestValue != ref;
    if (func == 7) return alphaTestValue >= ref;
    return true;
}

void main()
{
    bool flatShade = ckffSpecIsOptimized() && ckffSpecFlatShade();
    vec4 diffuse = flatShade ? v_flatColor0 : v_color0;
    vec4 specular = flatShade ? v_flatColor1 : v_color1;
    vec4 current = diffuse;
    vec4 temp = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 previousTexture = vec4(0.0, 0.0, 0.0, 1.0);
    int previousColorOp = 0;

#if defined(CKFF_FULL_SPECIALIZED)
    for (int stage = 0; stage < CKFF_FS_ACTIVE_STAGE_COUNT; ++stage) {
#else
    for (int stage = 0; stage < 8; ++stage) {
#endif
#if !defined(CKFF_FULL_SPECIALIZED)
        if (ckffSpecIsOptimized() && stage > ckffSpecLastActiveTextureStage()) break;
#endif

#if defined(CKFF_FULL_SPECIALIZED)
        vec4 colorParams = vec4_splat(0.0);
        vec4 alphaParams = vec4_splat(0.0);
        vec4 colorExtra = u_stageParams[stage * 4 + 2];
        vec4 alphaExtra = u_stageParams[stage * 4 + 3];
#else
        vec4 colorParams = u_stageParams[stage * 4 + 0];
        vec4 alphaParams = u_stageParams[stage * 4 + 1];
        vec4 colorExtra = u_stageParams[stage * 4 + 2];
        vec4 alphaExtra = u_stageParams[stage * 4 + 3];
#endif
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

        vec4 sampleCoord = getSampleCoord(stageCoord, stageParams.TexcoordTransformFlags);

        if (stage != 0 && (previousColorOp == 22 || previousColorOp == 23)) {
            vec2 bump = previousTexture.xy;
            int bumpBase = (stage - 1) * 2;
            sampleCoord.x += dot(u_bumpEnv[bumpBase].xy, bump);
            sampleCoord.y += dot(u_bumpEnv[bumpBase].zw, bump);
        }

        vec4 texColor = getTextureColor(stage, sampleCoord, stageParams.SamplerType, stageParams.SamplerCompareFunc, hasTexture);
        if (stage != 0 && previousColorOp == 23) {
            int bumpBase = (stage - 1) * 2;
            float lum = clamp(previousTexture.z * u_bumpEnv[bumpBase + 1].x + u_bumpEnv[bumpBase + 1].y, 0.0, 1.0);
            texColor *= lum;
        }
        vec4 colorA = getArg(stageParams.ColorArg1, texColor, current, diffuse, specular, temp, stageParams.Constant);
        vec4 colorB = getArg(stageParams.ColorArg2, texColor, current, diffuse, specular, temp, stageParams.Constant);
        vec4 colorC = getArg(stageParams.ColorArg0, texColor, current, diffuse, specular, temp, stageParams.Constant);
        vec4 alphaA = getArg(stageParams.AlphaArg1, texColor, current, diffuse, specular, temp, stageParams.Constant);
        vec4 alphaB = getArg(stageParams.AlphaArg2, texColor, current, diffuse, specular, temp, stageParams.Constant);
        vec4 alphaC = getArg(stageParams.AlphaArg0, texColor, current, diffuse, specular, temp, stageParams.Constant);

        vec4 stageResult = current;
        vec4 colorResult = applyOp(colorOp, colorA, colorB, colorC, current, diffuse, texColor);
        vec4 alphaResult = applyOp(alphaOp, alphaA, alphaB, alphaC, current, diffuse, texColor);
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

    bool specularEnabled = ckffSpecIsOptimized() ? ckffSpecGlobalSpecularEnabled() : (u_ffDrawParams[8].z > 0.5);
    if (specularEnabled) {
        current.rgb += specular.rgb;
    }
    if (ckffSpecIsOptimized()) {
        if (ckffSpecAlphaTestEnabled() && !alphaPass(current.a, ckffSpecAlphaFunc())) discard;
    } else {
        if (!alphaPass(current.a, int(u_ffDrawParams[8].y))) discard;
    }
    bool fogEnabled = ckffSpecIsOptimized() ? ckffSpecFogEnabled() : true;
    if (fogEnabled) {
        int pixelFogMode = ckffSpecIsOptimized() ? ckffSpecPixelFogMode() : int(u_ffDrawParams[8].w);
        float fogFactor = computePixelFogFactor(v_fogPos.z / v_fogPos.w, pixelFogMode, v_texcoord7Fog.z);
        current.rgb = mix(u_ffDrawParams[11].rgb, current.rgb, fogFactor);
    }
    gl_FragColor = clamp(current, 0.0, 1.0);
}

