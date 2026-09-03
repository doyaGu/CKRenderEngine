$input v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"

uniform vec4 u_ffDrawParams[20];
uniform vec4 u_bumpEnv[16];
uniform vec4 u_stageParams[16];
uniform vec4 u_ffSpec[5];

// Fixed sampler layout shared by every draw (spec 5.3): one 2D sampler per
// texture stage, then four cube and four volume samplers that the C++ side
// fills in stage order (the n-th cube stage binds s_textureCube{n}).
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
SAMPLER3D(s_textureVolume0, 12);
SAMPLER3D(s_textureVolume1, 13);
SAMPLER3D(s_textureVolume2, 14);
SAMPLER3D(s_textureVolume3, 15);

#include "fs_ff_common.sc"

float compareDepth(float depth, float ref, int func)
{
    if (func == 1) return ref < depth ? 1.0 : 0.0;
    if (func == 2) return ref <= depth ? 1.0 : 0.0;
    if (func == 3) return ref == depth ? 1.0 : 0.0;
    if (func == 4) return ref >= depth ? 1.0 : 0.0;
    if (func == 5) return ref > depth ? 1.0 : 0.0;
    if (func == 6) return ref != depth ? 1.0 : 0.0;
    if (func == 7) return 0.0;
    if (func == 8) return 1.0;
    return depth;
}

vec4 applyMirrorOnceCoord(vec4 coord, int mirrorOnceMask, int samplerType)
{
    if (samplerType == 1 || mirrorOnceMask == 0) return coord;
    if ((mirrorOnceMask & 1) != 0) coord.x = clamp(abs(coord.x), 0.0, 1.0);
    if ((mirrorOnceMask & 2) != 0) coord.y = clamp(abs(coord.y), 0.0, 1.0);
    if (samplerType == 3 && (mirrorOnceMask & 4) != 0) coord.z = clamp(abs(coord.z), 0.0, 1.0);
    return coord;
}

// Ordinal of this stage among the stages sampling the same sampler type
// (mirrors CKFFSamplerOrdinal on the C++ side).
int ckffSamplerOrdinal(int stage, int samplerType)
{
    int ordinal = 0;
    for (int previousStage = 0; previousStage < 8; ++previousStage) {
        if (previousStage >= stage) break;
        if (ckffSpecStage_SAMPLER_TYPE(previousStage) == samplerType)
            ++ordinal;
    }
    return ordinal;
}

vec4 getTextureColor(int stage, vec4 coord, int samplerType, int compareFunc, int mirrorOnceMask, bool hasTexture)
{
    if (!hasTexture) return vec4(0.0, 0.0, 0.0, 1.0);
    coord = applyMirrorOnceCoord(coord, mirrorOnceMask, samplerType);
    if (samplerType == 1) {
        int ordinal = ckffSamplerOrdinal(stage, samplerType);
        if (ordinal == 0) return textureCube(s_textureCube0, coord.xyz);
        if (ordinal == 1) return textureCube(s_textureCube1, coord.xyz);
        if (ordinal == 2) return textureCube(s_textureCube2, coord.xyz);
        return textureCube(s_textureCube3, coord.xyz);
    }
    if (samplerType == 3) {
        int ordinal = ckffSamplerOrdinal(stage, samplerType);
        if (ordinal == 0) return texture3D(s_textureVolume0, coord.xyz);
        if (ordinal == 1) return texture3D(s_textureVolume1, coord.xyz);
        if (ordinal == 2) return texture3D(s_textureVolume2, coord.xyz);
        return texture3D(s_textureVolume3, coord.xyz);
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

vec4 getSampleCoord(vec4 coord, int transformFlags)
{
    if ((transformFlags & 0x100) != 0) {
        coord /= abs(coord.w) < 0.0001 ? (coord.w < 0.0 ? -0.0001 : 0.0001) : coord.w;
    }
    if ((transformFlags & 0x1000) != 0) {
        coord.y = 1.0 - coord.y;
    }
    return coord;
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

vec4 getArg(int arg, vec4 textureColor, vec4 current, vec4 diffuse, vec4 specular,
            vec4 temp, vec4 stageConstant, bool premodulateCurrent)
{
    int baseArg = arg & ~(0x10 | 0x20);
    vec4 value = current;
    if (baseArg == 0) value = diffuse;
    else if (baseArg == 1) value = premodulateCurrent ? current * textureColor : current;
    else if (baseArg == 2) value = textureColor;
    else if (baseArg == 3) value = u_ffDrawParams[9];
    else if (baseArg == 4) value = specular;
    else if (baseArg == 5) value = temp;
    else if (baseArg == 6) value = stageConstant;
    return applyArgModifiers(value, arg);
}

vec4 applyOp(int op, vec4 a, vec4 b, vec4 c, vec4 dst, vec4 current, vec4 diffuse, vec4 textureColor)
{
    if (op == 1) return dst;
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
    if (op == 14) return mix(b, a, u_ffDrawParams[9].a);
    if (op == 15) return clamp(a + b * (1.0 - textureColor.a), 0.0, 1.0);
    if (op == 16) return mix(b, a, current.a);
    if (op == 17) return a;
    if (op == 18) return clamp(a + vec4_splat(a.a) * b, 0.0, 1.0);
    if (op == 19) return clamp(a * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 20) return clamp(a + (1.0 - a.a) * b, 0.0, 1.0);
    if (op == 21) return clamp((vec4_splat(1.0) - a) * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 22 || op == 23) return dst;
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

vec2 ckffDecodeBump(vec2 bump, bool unormEncoded)
{
    return unormEncoded
        ? clamp((bump * 255.0 - 128.0) / 127.0, vec2(-1.0, -1.0), vec2(1.0, 1.0))
        : bump;
}

void main()
{
    bool flatShade = ckffSpec_FLAT_SHADE() != 0;
    int lastActiveStage = ckffSpec_LAST_ACTIVE_TEXTURE_STAGE();
    vec4 diffuse = flatShade ? v_flatColor0 : v_color0;
    vec4 specular = flatShade ? v_flatColor1 : v_color1;
    vec4 current = diffuse;
    vec4 temp = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 previousTexture = vec4(0.0, 0.0, 0.0, 1.0);
    bool previousBumpUnorm = false;
    int previousColorOp = 0;
    int previousAlphaOp = 0;

    for (int stage = 0; stage < 8; ++stage) {
        if (stage > lastActiveStage) break;

        CKFFStageParams stageParams = ckffReadStageParams(stage, u_stageParams[stage * 2 + 0], u_stageParams[stage * 2 + 1]);
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
            vec2 bump = ckffDecodeBump(previousTexture.xy, previousBumpUnorm);
            int bumpBase = (stage - 1) * 2;
            sampleCoord.x += dot(u_bumpEnv[bumpBase].xy, bump);
            sampleCoord.y += dot(u_bumpEnv[bumpBase].zw, bump);
        }

        vec4 texColor = getTextureColor(stage, sampleCoord, stageParams.SamplerType, stageParams.SamplerCompareFunc, stageParams.MirrorOnceMask, hasTexture);
        if (stage != 0 && previousColorOp == 23) {
            int bumpBase = (stage - 1) * 2;
            float lum = clamp(previousTexture.z * u_bumpEnv[bumpBase + 1].x + u_bumpEnv[bumpBase + 1].y, 0.0, 1.0);
            texColor *= lum;
        }
        bool premodulateColor = previousColorOp == 17 && hasTexture;
        bool premodulateAlpha = previousAlphaOp == 17 && hasTexture;
        vec4 colorA = getArg(stageParams.ColorArg1, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 colorB = getArg(stageParams.ColorArg2, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 colorC = getArg(stageParams.ColorArg0, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateColor);
        vec4 alphaA = getArg(stageParams.AlphaArg1, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);
        vec4 alphaB = getArg(stageParams.AlphaArg2, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);
        vec4 alphaC = getArg(stageParams.AlphaArg0, texColor, current, diffuse, specular, temp, stageParams.Constant, premodulateAlpha);

        int resultArg = stageParams.ResultArg;
        vec4 stageResult = resultArg == 5 ? temp : current;
        vec4 colorResult = applyOp(colorOp, colorA, colorB, colorC, stageResult, current, diffuse, texColor);
        vec4 alphaResult = applyOp(alphaOp, alphaA, alphaB, alphaC, stageResult, current, diffuse, texColor);
        stageResult.rgb = colorResult.rgb;
        stageResult.a = alphaResult.a;
        if (colorOp == 24) {
            stageResult = colorResult;
        }

        if (resultArg == 5) {
            temp = stageResult;
        } else {
            current = stageResult;
        }

        previousTexture = texColor;
        previousBumpUnorm = stageParams.BumpUnorm;
        previousColorOp = colorOp;
        previousAlphaOp = alphaOp;
    }

    if (ckffSpec_GLOBAL_SPECULAR_ENABLED() != 0) {
        current.rgb += specular.rgb;
    }
    // Alpha test precision (the high nibble of the packed alpha draw param) is not applied yet:
    // the 8-bit path matches the reference; wider alpha targets are a phase 2.3 item.
    if (ckffSpec_ALPHA_TEST_ENABLED() != 0 && !alphaPass(current.a, ckffSpec_ALPHA_FUNC())) discard;
    if (ckffSpec_FOG_ENABLED() != 0) {
        int pixelFogMode = ckffSpec_PIXEL_FOG_MODE();
        float fogFactor = pixelFogMode == 0
            ? v_texcoord7Fog.z
            : computePixelFogFactor(v_fogPos.z / v_fogPos.w, pixelFogMode, v_texcoord7Fog.z);
        current.rgb = mix(u_ffDrawParams[11].rgb, current.rgb, fogFactor);
    }
    gl_FragColor = clamp(current, 0.0, 1.0);
}
