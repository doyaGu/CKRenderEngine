$input v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos, v_lineOffset, v_clipDistance0, v_clipDistance1

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"
#include "ff_sampler_shader_state.sh"

uniform vec4 u_ffDrawParams[20];
uniform vec4 u_bumpEnv[16];
uniform vec4 u_stageParams[16];
uniform vec4 u_borderColor[8];
uniform vec4 u_borderSampler[16];
uniform vec4 u_ffProgram[5];

#include "ff_sampler_layout.sh"
#include "fs_ff_common.sc"
#ifndef CKFF_FRAGMENT_SAMPLING_NATIVE_EXACT
#define CKFF_FRAGMENT_SAMPLING_NATIVE_EXACT 0
#endif
#if !CKFF_NATIVE_SDL_GPU && BGFX_SHADER_LANGUAGE_HLSL >= 600
#define CKFF_DEFER_COMBINER_DECODE 1
#else
#define CKFF_DEFER_COMBINER_DECODE 0
#endif
#if CKFF_FRAGMENT_SAMPLING_NATIVE_EXACT
#include "ff_sampler_native_exact.sc"
#else
#include "ff_sampler_common.sc"
#include "ff_sampler_full_exact.sc"
#endif
#include "ff_texture_ops.sc"

void main()
{
// CKFF_BGFX_ONLY_BEGIN
    // Fragment clipping also covers bgfx profiles without native clip-distance state.
    if (v_clipDistance0.x < 0.0 || v_clipDistance0.y < 0.0 ||
        v_clipDistance0.z < 0.0 || v_clipDistance0.w < 0.0 ||
        v_clipDistance1.x < 0.0 || v_clipDistance1.y < 0.0 ||
        v_clipDistance1.z < 0.0 || v_clipDistance1.w < 0.0)
        discard;
// CKFF_BGFX_ONLY_END
    float edgeCoverage = 1.0;
    if (u_ffDrawParams[4].w > 2.5) {
        vec2 lineOffset = v_lineOffset * gl_FragCoord.w;
        edgeCoverage = clamp(1.0 - length(lineOffset), 0.0, 1.0);
        if (edgeCoverage <= 0.0)
            discard;
    }
#if !CKFF_NATIVE_SDL_GPU
    int linePattern = int(u_ffDrawParams[3].w);
    int lineRepeat = int(u_ffDrawParams[11].w);
    if (lineRepeat > 0) {
        float linePhase = v_fogPos.y * gl_FragCoord.w;
        int lineBit = 15 - (int(linePhase) & 15);
        if (((linePattern >> lineBit) & 1) == 0)
            discard;
    }
#endif
    CKFFGlobalFragmentProgram fragmentProgram =
        ckffDecodeGlobalFragmentProgram();
    vec4 diffuse = fragmentProgram.FlatShade ? v_flatColor0 : v_color0;
    vec4 specular = fragmentProgram.FlatShade ? v_flatColor1 : v_color1;
    vec4 current = diffuse;
    vec4 temp = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 previousTexture = vec4(0.0, 0.0, 0.0, 1.0);
    bool previousBumpUnorm = false;
    int previousColorOp = 0;
    int previousAlphaOp = 0;

#if BGFX_SHADER_LANGUAGE_HLSL && !CKFF_NATIVE_SDL_GPU
    [loop]
#endif
    for (int stage = 0; stage < 8; ++stage) {
        if (stage > fragmentProgram.LastActiveTextureStage) break;

        CKFFTextureStageProgramWords stageWords =
            ckffReadTextureStageProgramWords(stage,
                fragmentProgram.SamplerOrdinals);
        // Keep only sampling fields live across CKFFSampleTexture.  Decoding
        // all combiner fields here raises register pressure in the full-exact
        // DXIL program even though those fields are consumed afterwards.
        CKFFTextureStageSamplingProgram samplingProgram =
            ckffDecodeTextureStageSamplingProgram(stageWords);
        int colorOp = samplingProgram.ColorOp;
#if !CKFF_DEFER_COMBINER_DECODE
        CKFFTextureStageCombinerProgram combinerProgram =
            ckffDecodeTextureStageCombinerProgram(stageWords);
        vec4 stageConstant = u_stageParams[stage * 2 + 1];
        int stageBlend = int(u_stageParams[stage * 2 + 0].w + 0.5);
#endif
        CKFFStageParams stageParams = ckffReadStageParams(
            samplingProgram.Projected, u_stageParams[stage * 2 + 0]);
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

        if (u_ffDrawParams[4].z > 0.5) {
            float affineW = abs(v_fogPos.x) < 0.000001
                ? (v_fogPos.x < 0.0 ? -0.000001 : 0.000001) : v_fogPos.x;
            if (stage == 7) stageCoord.xyw /= affineW;
            else stageCoord /= affineW;
        }

        vec4 sampleCoord = getSampleCoord(stageCoord, stageParams.TexcoordTransformFlags);

        if (stage != 0 && (previousColorOp == 22 || previousColorOp == 23)) {
            vec2 bump = ckffDecodeBump(previousTexture.xy, previousBumpUnorm);
            int bumpBase = (stage - 1) * 2;
            sampleCoord.x += dot(u_bumpEnv[bumpBase].xy, bump);
            sampleCoord.y += dot(u_bumpEnv[bumpBase].zw, bump);
        }

        vec4 texColor = CKFFSampleTexture(stage, sampleCoord,
            samplingProgram.SamplerType,
            samplingProgram.SamplerCompareFunc,
            samplingProgram.SamplerOrdinal,
            stageParams.MirrorOnceMask, hasTexture);
        if (stage != 0 && previousColorOp == 23) {
            int bumpBase = (stage - 1) * 2;
            float lum = clamp(previousTexture.z * u_bumpEnv[bumpBase + 1].x + u_bumpEnv[bumpBase + 1].y, 0.0, 1.0);
            texColor *= lum;
        }
#if CKFF_DEFER_COMBINER_DECODE
        CKFFTextureStageCombinerProgram combinerProgram =
            ckffDecodeTextureStageCombinerProgram(stageWords);
        vec4 stageConstant = u_stageParams[stage * 2 + 1];
        int stageBlend = int(u_stageParams[stage * 2 + 0].w + 0.5);
#endif
        int alphaOp = combinerProgram.AlphaOp;
        bool premodulateColor = previousColorOp == 17 && hasTexture;
        bool premodulateAlpha = previousAlphaOp == 17 && hasTexture;
        vec4 colorA = getArg(combinerProgram.ColorArg1, texColor, current, diffuse, specular, temp, stageConstant, premodulateColor);
        vec4 colorB = getArg(combinerProgram.ColorArg2, texColor, current, diffuse, specular, temp, stageConstant, premodulateColor);
        vec4 colorC = getArg(combinerProgram.ColorArg0, texColor, current, diffuse, specular, temp, stageConstant, premodulateColor);
        vec4 alphaA = getArg(combinerProgram.AlphaArg1, texColor, current, diffuse, specular, temp, stageConstant, premodulateAlpha);
        vec4 alphaB = getArg(combinerProgram.AlphaArg2, texColor, current, diffuse, specular, temp, stageConstant, premodulateAlpha);
        vec4 alphaC = getArg(combinerProgram.AlphaArg0, texColor, current, diffuse, specular, temp, stageConstant, premodulateAlpha);

        int resultArg = combinerProgram.ResultIsTemp ? 5 : 1;
        vec4 stageResult = resultArg == 5 ? temp : current;
        vec4 colorResult = colorOp == 27
            ? ckffStageBlend(texColor, current, stageBlend)
            : applyOp(colorOp, colorA, colorB, colorC, stageResult, current, diffuse, texColor);
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

    if (fragmentProgram.GlobalSpecularEnabled) {
        current.rgb += specular.rgb;
    }
    // Alpha test precision (the high nibble of the packed alpha draw param) is not applied yet:
    // the 8-bit path matches the reference; wider alpha targets are a phase 2.3 item.
    if (fragmentProgram.AlphaTestEnabled &&
        !alphaPass(current.a, fragmentProgram.AlphaFunc)) discard;
    if (fragmentProgram.FogEnabled) {
        int pixelFogMode = fragmentProgram.PixelFogMode;
        float fogFactor = pixelFogMode == 0
            ? v_texcoord7Fog.z
            : computePixelFogFactor(v_fogPos.z / v_fogPos.w, pixelFogMode, v_texcoord7Fog.z);
        current.rgb = mix(u_ffDrawParams[11].rgb, current.rgb, fogFactor);
    }
    current.a *= edgeCoverage;
    gl_FragColor = clamp(current, 0.0, 1.0);
}
