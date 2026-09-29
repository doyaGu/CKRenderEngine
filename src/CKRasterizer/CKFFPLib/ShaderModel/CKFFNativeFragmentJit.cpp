#include "CKFFNativeFragmentJit.h"

#include "CKFFShaderInterface.h"
#include "CKFFStageState.h"
#include "CKJitBuilder.h"

namespace {

// Rows of the native fragment uniform block (CKFFBuildProgramInterface).
struct UniformRows {
    uint32_t DrawParams;
    uint32_t BumpEnv;
    uint32_t StageParams;
    uint32_t Count;
};

// u_bumpEnv rows of a stage (CKFFPackBumpEnvUniform).
enum BumpEnvRow {
    BUMP_ENV_MATRIX = 0,    // M00, M01, M10, M11
    BUMP_ENV_LUMINANCE = 1, // luminance scale, luminance offset, LOD bias, sampler state
    BUMP_ENV_ROWS_PER_STAGE = 2,
};

// The varyings of the native fragment shaders in declaration order. Varying v
// is TEXCOORDv at SPIR-V location v and DXBC register v + 1, after
// SV_Position.
enum Varying {
    VARYING_COLOR0,
    VARYING_COLOR1,
    VARYING_FLAT_COLOR0,
    VARYING_FLAT_COLOR1,
    VARYING_TEXCOORD0,
    VARYING_TEXCOORD7_FOG = VARYING_TEXCOORD0 + 7, // w: vertex fog factor in z
    VARYING_FOG_POS,                               // x: affine w; z / w: pixel fog depth
    VARYING_LINE_OFFSET,
    VARYING_COUNT,
};

bool ResolveUniformRows(CKFFSamplerLayout layout, UniformRows &rows) {
    struct Block {
        CKFFConstantBlock Id;
        uint32_t MinRows;
        uint32_t *Row;
    };
    const Block blocks[] = {
        {CKRST_BLOCK_DRAW_PARAMS, CKFF_DRAW_PARAM_VEC4_COUNT, &rows.DrawParams},
        {CKRST_BLOCK_BUMP_ENV, CKFF_FRAGMENT_PROGRAM_STAGE_COUNT * BUMP_ENV_ROWS_PER_STAGE, &rows.BumpEnv},
        {CKRST_BLOCK_STAGE_PARAMS, CKFF_STAGE_PARAM_VEC4_COUNT, &rows.StageParams},
    };

    // Every packed format shares the native layout; SPIR-V stands for them.
    const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, CKRST_SHADER_FORMAT_SPIRV, FALSE, FALSE, layout);
    CKDWORD buffer = UINT32_MAX;
    for (const Block &block : blocks) {
        bool found = false;
        for (int i = 0; i < desc.Uniforms.Size(); ++i) {
            const CKFFUniformBinding &uniform = desc.Uniforms[i];
            if (uniform.Stage != CKRST_SHADER_PIXEL || uniform.Slot != (CKDWORD)block.Id)
                continue;
            if (uniform.Offset % 16u != 0 || uniform.Count < block.MinRows ||
                (buffer != UINT32_MAX && uniform.BufferSlot != buffer)) {
                return false;
            }
            buffer = uniform.BufferSlot;
            *block.Row = uniform.Offset / 16u;
            found = true;
            break;
        }
        if (!found)
            return false;
    }

    rows.Count = 0;
    for (int i = 0; i < desc.UniformBuffers.Size(); ++i) {
        const CKFFUniformBufferBinding &binding = desc.UniformBuffers[i];
        if (binding.Stage == CKRST_SHADER_PIXEL && binding.Slot == buffer)
            rows.Count = binding.Size / 16u;
    }
    return rows.Count != 0;
}

// A combiner register or argument viewed as its colour and alpha channels,
// which the combiners evaluate separately.
struct Color {
    CKJitValue Rgb; // FLOAT3
    CKJitValue A;   // FLOAT
};

enum Channel { CHANNEL_RGB, CHANNEL_ALPHA };

class NativeFragmentCompiler {
public:
    NativeFragmentCompiler(const CKFFFragmentProgram &program, CKFFSamplerLayout layout, const UniformRows &rows)
        : m_B(rows.Count), m_Program(program), m_Layout(layout), m_Rows(rows) {}

    bool Compile(CKJitFragmentShader &out);

private:
    CKJitValue DrawParam(CKFFDrawParamSlot slot) { return m_B.Uniform(m_Rows.DrawParams + slot); }
    CKJitValue BumpEnv(CKDWORD stage, BumpEnvRow row) {
        return m_B.Uniform(m_Rows.BumpEnv + stage * BUMP_ENV_ROWS_PER_STAGE + row);
    }
    CKJitValue StageParam(CKDWORD stage, CKFFStageParamSlot slot) {
        return m_B.Uniform(m_Rows.StageParams + CKFFStageParamIndex(stage, slot));
    }
    CKDWORD StageField(CKDWORD stage, CKFFFragmentProgramStageField field) const {
        return m_Program.GetStage(stage, field);
    }

    Color Split(CKJitValue value) { return {m_B.Swizzle(value, "xyz"), m_B.Component(value, 3)}; }
    CKJitValue Join(const Color &color) { return m_B.Construct({color.Rgb, color.A}); }
    CKJitValue Pick(const Color &color, Channel channel) const { return channel == CHANNEL_RGB ? color.Rgb : color.A; }
    Color Select(CKJitValue condition, const Color &whenTrue, const Color &whenFalse) {
        return {m_B.Select(condition, whenTrue.Rgb, whenFalse.Rgb), m_B.Select(condition, whenTrue.A, whenFalse.A)};
    }
    // abs(v) < epsilon ? (v < 0 ? -epsilon : epsilon) : v
    CKJitValue SafeDivisor(CKJitValue v, float epsilon);

    void DeclareInputs();
    CKJitValue LineCoverage(CKJitValue &discard);
    void EvaluateStage(CKDWORD stage);
    CKJitValue SampleCoordinate(CKDWORD stage, uint32_t components);
    CKJitValue SampleTexture(CKDWORD stage, CKJitValue hasTexture);
    Color Argument(CKDWORD packedArg, const Color &texture, const Color &current, const Color &stageConstant);
    CKJitValue Combine(CKDWORD op, Channel channel, const Color &arg1, const Color &arg2, const Color &arg0,
                       const Color &destination, const Color &texture);
    CKJitValue DotProduct3(const Color &a, const Color &b);
    CKJitValue StageBlend(const Color &source, const Color &destination, CKJitValue packedFactors);
    CKJitValue BlendFactor(CKJitValue factor, const Color &source, const Color &destination);
    CKJitValue AlphaTestPass(CKJitValue alpha);
    CKJitValue FogFactor();

    CKJitBuilder m_B;
    const CKFFFragmentProgram &m_Program;
    CKFFSamplerLayout m_Layout;
    UniformRows m_Rows;

    CKJitValue m_FragCoord;
    CKJitValue m_Varyings[VARYING_COUNT];

    // Combiner state carried between stages.
    Color m_Diffuse;
    Color m_Specular;
    Color m_Current;
    Color m_Temp;
    Color m_TextureFactor;
    CKJitValue m_PreviousTexture;    // FLOAT4
    CKJitValue m_PreviousBumpUnorm;  // BOOL
    CKDWORD m_PreviousColorOp = 0;
    CKDWORD m_PreviousAlphaOp = 0;
};

CKJitValue NativeFragmentCompiler::SafeDivisor(CKJitValue v, float epsilon) {
    const CKJitValue sign = m_B.Select(m_B.Less(v, m_B.Float(0.0f)), m_B.Float(-epsilon), m_B.Float(epsilon));
    return m_B.Select(m_B.Less(m_B.Abs(v), m_B.Float(epsilon)), sign, v);
}

void NativeFragmentCompiler::DeclareInputs() {
    // The whole interface is declared, used or not, so that the shader links
    // with the native vertex shaders exactly like the one it replaces.
    m_FragCoord = m_B.Input({"SV_Position", 0, 0, 0, 4, CKJIT_INPUT_FRAG_COORD});
    for (uint32_t v = 0; v < VARYING_COUNT; ++v) {
        const bool flat = v == VARYING_FLAT_COLOR0 || v == VARYING_FLAT_COLOR1;
        const uint8_t components = v == VARYING_LINE_OFFSET ? 2 : 4;
        m_Varyings[v] =
            m_B.Input({"TEXCOORD", v, v + 1, v, components, flat ? CKJIT_INPUT_FLAT : CKJIT_INPUT_SMOOTH});
    }
}

CKJitValue NativeFragmentCompiler::LineCoverage(CKJitValue &discard) {
    // Antialiased lines fade with the distance to the line centre.
    const CKJitValue lineMode =
        m_B.Greater(m_B.Component(DrawParam(CKFF_DRAW_PARAM_MATERIAL_POWER), 3), m_B.Float(2.5f));
    const CKJitValue offset = m_B.Mul(m_Varyings[VARYING_LINE_OFFSET], m_B.Component(m_FragCoord, 3));
    const CKJitValue coverage = m_B.Saturate(m_B.Sub(m_B.Float(1.0f), m_B.Length(offset)));
    discard = m_B.And(lineMode, m_B.LessEqual(coverage, m_B.Float(0.0f)));
    return m_B.Select(lineMode, coverage, m_B.Float(1.0f));
}

bool NativeFragmentCompiler::Compile(CKJitFragmentShader &out) {
    DeclareInputs();

    CKJitValue discard;
    const CKJitValue edgeCoverage = LineCoverage(discard);

    const bool flatShade = m_Program.Get(CKFF_FRAGMENT_PROGRAM_FLAT_SHADE) != 0;
    m_Diffuse = Split(m_Varyings[flatShade ? VARYING_FLAT_COLOR0 : VARYING_COLOR0]);
    m_Specular = Split(m_Varyings[flatShade ? VARYING_FLAT_COLOR1 : VARYING_COLOR1]);
    m_Current = m_Diffuse;
    m_Temp = {m_B.Float3(0.0f, 0.0f, 0.0f), m_B.Float(0.0f)};
    m_TextureFactor = Split(DrawParam(CKFF_DRAW_PARAM_TEXTURE_FACTOR));
    m_PreviousTexture = m_B.Float4(0.0f, 0.0f, 0.0f, 1.0f);
    m_PreviousBumpUnorm = m_B.Bool(false);

    const CKDWORD lastStage = m_Program.Get(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE);
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT && stage <= lastStage; ++stage) {
        if (StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP) == CKRST_TOP_DISABLE)
            break;
        EvaluateStage(stage);
    }

    if (m_Program.Get(CKFF_FRAGMENT_PROGRAM_GLOBAL_SPECULAR_ENABLED))
        m_Current.Rgb = m_B.Add(m_Current.Rgb, m_Specular.Rgb);
    if (m_Program.Get(CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED))
        discard = m_B.Or(discard, m_B.Not(AlphaTestPass(m_Current.A)));
    if (m_Program.Get(CKFF_FRAGMENT_PROGRAM_FOG_ENABLED)) {
        const CKJitValue fogColor = m_B.Swizzle(DrawParam(CKFF_DRAW_PARAM_FOG_COLOR), "xyz");
        m_Current.Rgb = m_B.Lerp(fogColor, m_Current.Rgb, FogFactor());
    }
    m_Current.A = m_B.Mul(m_Current.A, edgeCoverage);

    return m_B.Finish(m_B.Saturate(Join(m_Current)), discard, out);
}

void NativeFragmentCompiler::EvaluateStage(CKDWORD stage) {
    const CKDWORD colorOp = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP);
    const CKDWORD alphaOp = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP);

    const CKJitValue coordParams = StageParam(stage, CKFF_STAGE_PARAM_COORD);
    const Color stageConstant = Split(StageParam(stage, CKFF_STAGE_PARAM_CONSTANT));
    const CKJitValue hasTexture = m_B.Greater(m_B.Component(coordParams, 2), m_B.Float(0.5f));
    const CKJitValue stageBlend = m_B.FloatToInt(m_B.Add(m_B.Component(coordParams, 3), m_B.Float(0.5f)));
    const CKJitValue transformFlags = m_B.FloatToInt(m_B.Component(coordParams, 1));
    const CKJitValue bumpUnorm =
        m_B.Not(m_B.IntEqual(m_B.IntAnd(transformFlags, m_B.Int((int32_t)CKFF_TTF_BUMP_UNORM)), m_B.Int(0)));

    CKJitValue texture = SampleTexture(stage, hasTexture);
    if (stage != 0 && m_PreviousColorOp == CKRST_TOP_BUMPENVMAPLUMINANCE) {
        const CKJitValue luminance = BumpEnv(stage - 1, BUMP_ENV_LUMINANCE);
        texture = m_B.Mul(texture, m_B.Saturate(m_B.Add(m_B.Mul(m_B.Component(m_PreviousTexture, 2),
                                                                m_B.Component(luminance, 0)),
                                                        m_B.Component(luminance, 1))));
    }
    const Color tex = Split(texture);

    // CURRENT after a PREMODULATE stage is modulated by this stage's texture,
    // separately for the colour and the alpha combiner.
    const Color premodulated = {m_B.Mul(m_Current.Rgb, tex.Rgb), m_B.Mul(m_Current.A, tex.A)};
    const Color colorCurrent =
        m_PreviousColorOp == CKRST_TOP_PREMODULATE ? Select(hasTexture, premodulated, m_Current) : m_Current;
    const Color alphaCurrent =
        m_PreviousAlphaOp == CKRST_TOP_PREMODULATE ? Select(hasTexture, premodulated, m_Current) : m_Current;

    const Color colorArg1 =
        Argument(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1), tex, colorCurrent, stageConstant);
    const Color colorArg2 =
        Argument(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG2), tex, colorCurrent, stageConstant);
    const Color colorArg0 =
        Argument(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG0), tex, colorCurrent, stageConstant);
    const Color alphaArg1 =
        Argument(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1), tex, alphaCurrent, stageConstant);
    const Color alphaArg2 =
        Argument(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG2), tex, alphaCurrent, stageConstant);
    const Color alphaArg0 =
        Argument(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG0), tex, alphaCurrent, stageConstant);

    const bool resultIsTemp = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_RESULT_IS_TEMP) != 0;
    const Color destination = resultIsTemp ? m_Temp : m_Current;

    Color result;
    result.Rgb = colorOp == CKFF_TOP_STAGEBLEND
        ? StageBlend(tex, m_Current, stageBlend)
        : Combine(colorOp, CHANNEL_RGB, colorArg1, colorArg2, colorArg0, destination, tex);
    result.A = Combine(alphaOp, CHANNEL_ALPHA, alphaArg1, alphaArg2, alphaArg0, destination, tex);
    // DOTPRODUCT3 replicates into alpha whatever the alpha combiner computes.
    if (colorOp == CKRST_TOP_DOTPRODUCT3)
        result.A = DotProduct3(colorArg1, colorArg2);

    if (resultIsTemp)
        m_Temp = result;
    else
        m_Current = result;
    m_PreviousTexture = texture;
    m_PreviousBumpUnorm = bumpUnorm;
    m_PreviousColorOp = colorOp;
    m_PreviousAlphaOp = alphaOp;
}

CKJitValue NativeFragmentCompiler::SampleCoordinate(CKDWORD stage, uint32_t components) {
    const CKJitValue texcoord = m_Varyings[VARYING_TEXCOORD0 + stage];
    CKJitValue xy = m_B.Swizzle(texcoord, "xy");
    CKJitValue z = m_B.Component(texcoord, 2);
    CKJitValue w = m_B.Component(texcoord, 3);

    // Affine interpolation undoes the perspective division; the last stage
    // shares its register with the vertex fog factor in z.
    const CKJitValue affine = m_B.Greater(m_B.Component(DrawParam(CKFF_DRAW_PARAM_MATERIAL_POWER), 2), m_B.Float(0.5f));
    const CKJitValue affineW = SafeDivisor(m_B.Component(m_Varyings[VARYING_FOG_POS], 0), 0.000001f);
    xy = m_B.Select(affine, m_B.Div(xy, affineW), xy);
    if (VARYING_TEXCOORD0 + stage != VARYING_TEXCOORD7_FOG)
        z = m_B.Select(affine, m_B.Div(z, affineW), z);
    w = m_B.Select(affine, m_B.Div(w, affineW), w);

    if (StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED)) {
        const CKJitValue divisor = SafeDivisor(w, 0.0001f);
        xy = m_B.Div(xy, divisor);
        z = m_B.Div(z, divisor);
    }

    // BUMPENVMAP stages perturb the next stage's coordinate by their texel.
    if (stage != 0 &&
        (m_PreviousColorOp == CKRST_TOP_BUMPENVMAP || m_PreviousColorOp == CKRST_TOP_BUMPENVMAPLUMINANCE)) {
        const CKJitValue texel = m_B.Swizzle(m_PreviousTexture, "xy");
        const CKJitValue decoded = m_B.Min(
            m_B.Max(m_B.Div(m_B.Sub(m_B.Mul(texel, m_B.Float(255.0f)), m_B.Float(128.0f)), m_B.Float(127.0f)),
                    m_B.Float(-1.0f)),
            m_B.Float(1.0f));
        const CKJitValue bump = m_B.Select(m_PreviousBumpUnorm, decoded, texel);
        const CKJitValue matrix = BumpEnv(stage - 1, BUMP_ENV_MATRIX);
        xy = m_B.Add(xy, m_B.Construct({m_B.Dot(m_B.Swizzle(matrix, "xy"), bump),
                                        m_B.Dot(m_B.Swizzle(matrix, "zw"), bump)}));
    }

    return components == 2 ? xy : m_B.Construct({xy, z});
}

CKJitValue NativeFragmentCompiler::SampleTexture(CKDWORD stage, CKJitValue hasTexture) {
    const CKDWORD type = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    const CKJitSamplerDim dim = type == CKFF_SAMPLER_CUBE ? CKJIT_SAMPLER_CUBE
                              : type == CKFF_SAMPLER_VOLUME ? CKJIT_SAMPLER_3D
                                                            : CKJIT_SAMPLER_2D;
    // The wide 2D layout keeps a 2D texture per stage; everything else is
    // addressed by the stage's ordinal among the textures of its type.
    const CKDWORD index =
        dim == CKJIT_SAMPLER_2D && m_Layout == CKFF_SAMPLER_LAYOUT_WIDE_2D ? stage : m_Program.GetSamplerOrdinal(stage);

    // An index past the textures the layout declares samples nothing.
    CKJitValue color = m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (index < CKFFSamplerTypeSlotCount(type, m_Layout)) {
        const CKJitValue coordinate = SampleCoordinate(stage, dim == CKJIT_SAMPLER_2D ? 2 : 3);
        const CKJitValue lodBias = m_B.Component(BumpEnv(stage, BUMP_ENV_LUMINANCE), 2);
        color = m_B.Sample(CKFFSamplerSlot(type, index, m_Layout), dim, coordinate, lodBias);
        if (type == CKFF_SAMPLER_DEPTH)
            color = m_B.Swizzle(color, "xxxx");
    }
    return m_B.Select(hasTexture, color, m_B.Float4(0.0f, 0.0f, 0.0f, 1.0f));
}

Color NativeFragmentCompiler::Argument(CKDWORD packedArg, const Color &texture, const Color &current,
                                       const Color &stageConstant) {
    const CKDWORD arg = CKFFFragmentProgram::UnpackArg(packedArg);
    Color value;
    switch (arg & ~(CKRST_TA_COMPLEMENT | CKRST_TA_ALPHAREPLICATE)) {
    case CKRST_TA_DIFFUSE: value = m_Diffuse; break;
    case CKRST_TA_CURRENT: value = current; break;
    case CKRST_TA_TEXTURE: value = texture; break;
    case CKRST_TA_TFACTOR: value = m_TextureFactor; break;
    case CKRST_TA_SPECULAR: value = m_Specular; break;
    case CKRST_TA_TEMP: value = m_Temp; break;
    case CKRST_TA_CONSTANT: value = stageConstant; break;
    default: value = m_Current; break;
    }
    if (arg & CKRST_TA_COMPLEMENT) {
        value.Rgb = m_B.Sub(m_B.Float(1.0f), value.Rgb);
        value.A = m_B.Sub(m_B.Float(1.0f), value.A);
    }
    if (arg & CKRST_TA_ALPHAREPLICATE)
        value.Rgb = m_B.Splat(value.A, 3);
    return value;
}

CKJitValue NativeFragmentCompiler::DotProduct3(const Color &a, const Color &b) {
    const CKJitValue half = m_B.Float(0.5f);
    return m_B.Saturate(m_B.Mul(m_B.Dot(m_B.Sub(a.Rgb, half), m_B.Sub(b.Rgb, half)), m_B.Float(4.0f)));
}

CKJitValue NativeFragmentCompiler::Combine(CKDWORD op, Channel channel, const Color &arg1, const Color &arg2,
                                           const Color &arg0, const Color &destination, const Color &texture) {
    const CKJitValue a = Pick(arg1, channel);
    const CKJitValue b = Pick(arg2, channel);
    const CKJitValue c = Pick(arg0, channel);
    const CKJitValue one = m_B.Float(1.0f);
    const CKJitValue half = m_B.Float(0.5f);
    switch (op) {
    case CKRST_TOP_DISABLE: return Pick(destination, channel);
    case CKRST_TOP_SELECTARG1: return a;
    case CKRST_TOP_SELECTARG2: return b;
    case CKRST_TOP_MODULATE: return m_B.Mul(a, b);
    case CKRST_TOP_MODULATE2X: return m_B.Saturate(m_B.Mul(m_B.Mul(a, b), m_B.Float(2.0f)));
    case CKRST_TOP_MODULATE4X: return m_B.Saturate(m_B.Mul(m_B.Mul(a, b), m_B.Float(4.0f)));
    case CKRST_TOP_ADD: return m_B.Saturate(m_B.Add(a, b));
    case CKRST_TOP_ADDSIGNED: return m_B.Saturate(m_B.Sub(m_B.Add(a, b), half));
    case CKRST_TOP_ADDSIGNED2X: return m_B.Saturate(m_B.Mul(m_B.Sub(m_B.Add(a, b), half), m_B.Float(2.0f)));
    case CKRST_TOP_SUBTRACT: return m_B.Saturate(m_B.Sub(a, b));
    case CKRST_TOP_ADDSMOOTH: return m_B.Saturate(m_B.Sub(m_B.Add(a, b), m_B.Mul(a, b)));
    case CKRST_TOP_BLENDDIFFUSEALPHA: return m_B.Lerp(b, a, m_Diffuse.A);
    case CKRST_TOP_BLENDTEXTUREALPHA: return m_B.Lerp(b, a, texture.A);
    case CKRST_TOP_BLENDFACTORALPHA: return m_B.Lerp(b, a, m_TextureFactor.A);
    case CKRST_TOP_BLENDTEXTUREALPHAPM: return m_B.Saturate(m_B.Add(a, m_B.Mul(b, m_B.Sub(one, texture.A))));
    case CKRST_TOP_BLENDCURRENTALPHA: return m_B.Lerp(b, a, m_Current.A);
    case CKRST_TOP_PREMODULATE: return a;
    case CKRST_TOP_MODULATEALPHA_ADDCOLOR: return m_B.Saturate(m_B.Add(a, m_B.Mul(arg1.A, b)));
    case CKRST_TOP_MODULATECOLOR_ADDALPHA: return m_B.Saturate(m_B.Add(m_B.Mul(a, b), arg1.A));
    case CKRST_TOP_MODULATEINVALPHA_ADDCOLOR: return m_B.Saturate(m_B.Add(a, m_B.Mul(m_B.Sub(one, arg1.A), b)));
    case CKRST_TOP_MODULATEINVCOLOR_ADDALPHA: return m_B.Saturate(m_B.Add(m_B.Mul(m_B.Sub(one, a), b), arg1.A));
    case CKRST_TOP_BUMPENVMAP:
    case CKRST_TOP_BUMPENVMAPLUMINANCE: return Pick(destination, channel);
    case CKRST_TOP_DOTPRODUCT3: {
        const CKJitValue v = DotProduct3(arg1, arg2);
        return channel == CHANNEL_RGB ? m_B.Splat(v, 3) : v;
    }
    case CKRST_TOP_MULTIPLYADD: return m_B.Saturate(m_B.Add(m_B.Mul(a, b), c));
    case CKRST_TOP_LERP: return m_B.Saturate(m_B.Add(m_B.Mul(c, a), m_B.Mul(m_B.Sub(one, c), b)));
    default: return Pick(m_Current, channel);
    }
}

CKJitValue NativeFragmentCompiler::BlendFactor(CKJitValue factor, const Color &source, const Color &destination) {
    // The factors are draw state, so the selection stays in the shader.
    const CKJitValue one = m_B.Float(1.0f);
    const struct {
        VXBLEND_MODE Mode;
        CKJitValue Rgb;
    } factors[] = {
        {VXBLEND_ZERO, m_B.Float3(0.0f, 0.0f, 0.0f)},
        {VXBLEND_ONE, m_B.Float3(1.0f, 1.0f, 1.0f)},
        {VXBLEND_SRCCOLOR, source.Rgb},
        {VXBLEND_INVSRCCOLOR, m_B.Sub(one, source.Rgb)},
        {VXBLEND_SRCALPHA, m_B.Splat(source.A, 3)},
        {VXBLEND_INVSRCALPHA, m_B.Splat(m_B.Sub(one, source.A), 3)},
        {VXBLEND_DESTALPHA, m_B.Splat(destination.A, 3)},
        {VXBLEND_INVDESTALPHA, m_B.Splat(m_B.Sub(one, destination.A), 3)},
        {VXBLEND_DESTCOLOR, destination.Rgb},
        {VXBLEND_INVDESTCOLOR, m_B.Sub(one, destination.Rgb)},
        {VXBLEND_SRCALPHASAT, m_B.Splat(m_B.Min(source.A, m_B.Sub(one, destination.A)), 3)},
        {VXBLEND_BOTHSRCALPHA, m_B.Splat(source.A, 3)},
        {VXBLEND_BOTHINVSRCALPHA, m_B.Splat(m_B.Sub(one, source.A), 3)},
    };
    CKJitValue result = m_B.Float3(0.0f, 0.0f, 0.0f);
    for (int i = (int)(sizeof(factors) / sizeof(factors[0])); i-- > 0;)
        result = m_B.Select(m_B.IntEqual(factor, m_B.Int((int32_t)factors[i].Mode)), factors[i].Rgb, result);
    return result;
}

CKJitValue NativeFragmentCompiler::StageBlend(const Color &source, const Color &destination, CKJitValue packedFactors) {
    // STAGEBLEND carries a source and destination blend factor pair; the
    // BOTH* source modes imply their destination.
    const CKJitValue fifteen = m_B.Int(15);
    CKJitValue sourceFactor = m_B.IntAnd(m_B.IntShiftRight(packedFactors, m_B.Int(4)), fifteen);
    CKJitValue destinationFactor = m_B.IntAnd(packedFactors, fifteen);
    const CKJitValue srcAlpha = m_B.Int((int32_t)VXBLEND_SRCALPHA);
    const CKJitValue invSrcAlpha = m_B.Int((int32_t)VXBLEND_INVSRCALPHA);
    const CKJitValue bothSource = m_B.IntEqual(sourceFactor, m_B.Int((int32_t)VXBLEND_BOTHSRCALPHA));
    const CKJitValue bothInverse = m_B.IntEqual(sourceFactor, m_B.Int((int32_t)VXBLEND_BOTHINVSRCALPHA));
    destinationFactor =
        m_B.Select(bothSource, invSrcAlpha, m_B.Select(bothInverse, srcAlpha, destinationFactor));
    sourceFactor = m_B.Select(bothSource, srcAlpha, m_B.Select(bothInverse, invSrcAlpha, sourceFactor));
    return m_B.Saturate(m_B.Add(m_B.Mul(source.Rgb, BlendFactor(sourceFactor, source, destination)),
                                m_B.Mul(destination.Rgb, BlendFactor(destinationFactor, source, destination))));
}

CKJitValue NativeFragmentCompiler::AlphaTestPass(CKJitValue alpha) {
    // Alpha and reference compare as 8-bit values; the reference maps
    // 0..255 onto the 0..255 alpha steps the way D3D widens it.
    const CKJitValue value = m_B.RoundEven(m_B.Mul(alpha, m_B.Float(255.0f)));
    const CKJitValue reference = m_B.Floor(m_B.Component(DrawParam(CKFF_DRAW_PARAM_ALPHA), 0));
    const CKJitValue ref = m_B.Add(reference, m_B.Floor(m_B.Mul(reference, m_B.Float(1.0f / 256.0f))));
    switch (m_Program.Get(CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC)) {
    case VXCMP_NEVER: return m_B.Bool(false);
    case VXCMP_LESS: return m_B.Less(value, ref);
    case VXCMP_EQUAL: return m_B.Equal(value, ref);
    case VXCMP_LESSEQUAL: return m_B.LessEqual(value, ref);
    case VXCMP_GREATER: return m_B.Greater(value, ref);
    case VXCMP_NOTEQUAL: return m_B.NotEqual(value, ref);
    case VXCMP_GREATEREQUAL: return m_B.GreaterEqual(value, ref);
    default: return m_B.Bool(true);
    }
}

CKJitValue NativeFragmentCompiler::FogFactor() {
    const CKJitValue fogPos = m_Varyings[VARYING_FOG_POS];
    const CKDWORD mode = m_Program.Get(CKFF_FRAGMENT_PROGRAM_PIXEL_FOG_MODE);
    if (mode == VXFOG_NONE)
        return m_B.Component(m_Varyings[VARYING_TEXCOORD7_FOG], 2);

    const CKJitValue depth = m_B.Div(m_B.Component(fogPos, 2), m_B.Component(fogPos, 3));
    const CKJitValue params = DrawParam(CKFF_DRAW_PARAM_FOG); // start, end, density
    if (mode == VXFOG_EXP)
        return m_B.Saturate(m_B.Exp(m_B.Neg(m_B.Mul(m_B.Component(params, 2), depth))));
    if (mode == VXFOG_EXP2) {
        const CKJitValue e = m_B.Mul(m_B.Component(params, 2), depth);
        return m_B.Saturate(m_B.Exp(m_B.Neg(m_B.Mul(e, e))));
    }
    const CKJitValue start = m_B.Component(params, 0);
    const CKJitValue end = m_B.Component(params, 1);
    return m_B.Saturate(m_B.Div(m_B.Sub(end, depth), SafeDivisor(m_B.Sub(end, start), 0.0001f)));
}

} // namespace

bool CKFFCompileNativeFragmentProgram(const CKFFFragmentProgram &program, CKFFSamplerLayout layout,
                                      CKJitFragmentShader &out) {
    UniformRows rows;
    if ((unsigned)layout >= CKFF_SAMPLER_LAYOUT_COUNT || !ResolveUniformRows(layout, rows))
        return false;
    NativeFragmentCompiler compiler(program, layout, rows);
    return compiler.Compile(out);
}
