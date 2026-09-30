#include "CKFFNativeFragmentJit.h"

#include "CKFFShaderInterface.h"
#include "CKFFStageState.h"
#include "CKJitBuilder.h"

#include <cstring>

namespace {

const CKDWORD kStageCount = CKFF_FRAGMENT_PROGRAM_STAGE_COUNT;

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
        {CKRST_BLOCK_BUMP_ENV, kStageCount * BUMP_ENV_ROWS_PER_STAGE, &rows.BumpEnv},
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

// ---------------------------------------------------------------------------
// Keys.

bool StageSwitch(const CKDWORD *switches, CKFFNativeFragmentSwitch first, CKDWORD stage) {
    return (switches[0] & (CKDWORD)first << stage) != 0;
}

// The byte of a stage in the switch words from the first one.
CKDWORD StageByte(const CKDWORD *switches, CKDWORD first, CKDWORD stage) {
    return switches[first + stage / 4] >> (stage % 4 * 8) & 0xFFu;
}

void SetStageByte(CKDWORD *switches, CKDWORD first, CKDWORD stage, CKDWORD value) {
    CKDWORD &word = switches[first + stage / 4];
    const CKDWORD shift = stage % 4 * 8;
    word = (word & ~(0xFFu << shift)) | (value & 0xFFu) << shift;
}

CKDWORD BlendPair(const CKDWORD *switches, CKDWORD stage) {
    return StageByte(switches, 1, stage);
}

void SetBlendPair(CKDWORD *switches, CKDWORD stage, CKDWORD pair) {
    SetStageByte(switches, 1, stage, pair);
}

CKDWORD Sampling(const CKDWORD *switches, CKDWORD stage) {
    return StageByte(switches, 3, stage);
}

void SetSampling(CKDWORD *switches, CKDWORD stage, CKDWORD sampling) {
    SetStageByte(switches, 3, stage, sampling);
}

// The sampling flags of a stage's sampler state word and texture flags.
CKDWORD DrawSampling(CKDWORD state, CKDWORD textureFlags) {
    CKDWORD sampling = 0;
    if ((textureFlags & CKFF_TTF_MIRRORONCE_U) != 0)
        sampling |= CKFF_NATIVE_FRAGMENT_MIRROR_U;
    if ((textureFlags & CKFF_TTF_MIRRORONCE_V) != 0)
        sampling |= CKFF_NATIVE_FRAGMENT_MIRROR_V;
    if ((textureFlags & CKFF_TTF_MIRRORONCE_W) != 0)
        sampling |= CKFF_NATIVE_FRAGMENT_MIRROR_W;
    if ((state >> CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT & CKFF_SAMPLER_SHADER_BORDER_AXIS_MASK) != 0)
        sampling |= CKFF_NATIVE_FRAGMENT_BORDER;
    if ((state & CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT) != 0)
        sampling |= CKFF_NATIVE_FRAGMENT_GRADIENT;
    if ((state & CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY) != 0)
        sampling |= CKFF_NATIVE_FRAGMENT_ANISOTROPY;
    if ((state >> CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT & CKFF_SAMPLER_SHADER_MIN_MIP_MASK) != 0)
        sampling |= CKFF_NATIVE_FRAGMENT_MIN_MIP;
    return sampling;
}

// The depth comparison of a stage as the shader-sampling shaders evaluate
// its functions 1 to 8. The wide 2D layout compares sampled depths, passing
// them through for an unknown function as for none; the other layouts compare
// every texel, passing an unknown function's depths through, as function 9.
CKDWORD CanonicalCompareFunc(CKDWORD func, CKFFSamplerLayout layout) {
    if (func <= 8)
        return func;
    return layout == CKFF_SAMPLER_LAYOUT_WIDE_2D ? 0 : 9;
}

// The sampling flags a shader-sampling shader tests for a stage that samples.
// Mirror-once applies to the axes of the sampler type, and never to cubes,
// which sample in hardware. A texture addressed by the shader tests border
// addressing, except an undeclared depth texture, whose texels are all the
// border. A 2D texture otherwise tests only explicit gradients, and a volume
// without anisotropy its minimum mip level only where that alone keeps it
// from hardware sampling.
CKDWORD CanonicalSampling(CKDWORD sampling, CKDWORD type, bool compared, bool declared) {
    const CKDWORD mirror2D = CKFF_NATIVE_FRAGMENT_MIRROR_U | CKFF_NATIVE_FRAGMENT_MIRROR_V;
    const CKDWORD mirror3D = mirror2D | CKFF_NATIVE_FRAGMENT_MIRROR_W;
    switch (type) {
    case CKFF_SAMPLER_CUBE:
        return 0;
    case CKFF_SAMPLER_VOLUME:
        if ((sampling & CKFF_NATIVE_FRAGMENT_ANISOTROPY) != 0)
            return sampling & (mirror3D | CKFF_NATIVE_FRAGMENT_BORDER | CKFF_NATIVE_FRAGMENT_GRADIENT |
                               CKFF_NATIVE_FRAGMENT_ANISOTROPY);
        if ((sampling & (mirror3D | CKFF_NATIVE_FRAGMENT_BORDER)) != 0)
            return sampling & (mirror3D | CKFF_NATIVE_FRAGMENT_BORDER);
        return sampling & CKFF_NATIVE_FRAGMENT_MIN_MIP;
    default:
        if (compared)
            return sampling & (declared ? mirror2D | CKFF_NATIVE_FRAGMENT_BORDER : mirror2D);
        return sampling & (mirror2D | CKFF_NATIVE_FRAGMENT_BORDER | CKFF_NATIVE_FRAGMENT_GRADIENT);
    }
}

// A STAGEBLEND factor as the shaders evaluate it: the BOTH* modes as their
// source factor, and values without a factor as ZERO.
CKDWORD CanonicalFactor(CKDWORD factor) {
    if (factor == VXBLEND_BOTHSRCALPHA)
        return VXBLEND_SRCALPHA;
    if (factor == VXBLEND_BOTHINVSRCALPHA)
        return VXBLEND_INVSRCALPHA;
    return factor >= VXBLEND_ZERO && factor <= VXBLEND_SRCALPHASAT ? factor : VXBLEND_ZERO;
}

// A factor pair, the source in the high nibble; the BOTH* source modes imply
// their destination.
CKDWORD CanonicalBlendPair(CKDWORD pair) {
    const CKDWORD source = pair >> 4 & 15;
    CKDWORD destination = pair & 15;
    if (source == VXBLEND_BOTHSRCALPHA)
        destination = VXBLEND_INVSRCALPHA;
    else if (source == VXBLEND_BOTHINVSRCALPHA)
        destination = VXBLEND_SRCALPHA;
    return CanonicalFactor(source) << 4 | CanonicalFactor(destination);
}

// The wide 2D layout keeps a 2D texture per stage; everything else is
// addressed by the stage's ordinal among the textures of its type.
bool StageIndexed(CKDWORD samplerType, CKFFSamplerLayout layout) {
    return (samplerType == CKFF_SAMPLER_2D || samplerType == CKFF_SAMPLER_DEPTH) &&
           layout == CKFF_SAMPLER_LAYOUT_WIDE_2D;
}

CKDWORD SamplerIndex(const CKFFFragmentProgram &program, CKDWORD stage, CKFFSamplerLayout layout) {
    const CKDWORD type = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    return StageIndexed(type, layout) ? stage : program.GetSamplerOrdinal(stage);
}

// Whether the layout declares the texture a stage samples.
bool SamplerDeclared(const CKFFFragmentProgram &program, CKDWORD stage, CKFFSamplerLayout layout) {
    const CKDWORD type = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    return SamplerIndex(program, stage, layout) < CKFFSamplerTypeSlotCount(type, layout);
}

// A float of a constant block, or zero past its end.
float ConstantFloat(const CKFFConstantSet &constants, CKFFConstantBlock block, CKDWORD row, CKDWORD component) {
    const XArray<CKBYTE> &bytes = constants[block].Bytes;
    const CKDWORD offset = (row * 4 + component) * (CKDWORD)sizeof(float);
    float value = 0.0f;
    if (offset + sizeof(float) <= (CKDWORD)bytes.Size())
        std::memcpy(&value, bytes.Begin() + offset, sizeof(value));
    return value;
}

// The shaders' float to int conversion where it is defined; saturating
// elsewhere, and 0 for NaN.
int32_t FloatToInt(float value) {
    if (!(value == value))
        return 0;
    if (value <= -2147483648.0f)
        return INT32_MIN;
    if (value >= 2147483648.0f)
        return INT32_MAX;
    return (int32_t)value;
}

// The combiner arguments an operation reads.
enum {
    READS_ARG1 = 1,
    READS_ARG2 = 2,
    READS_ARG0 = 4,
};

CKDWORD ArgumentsRead(CKDWORD op) {
    switch (op) {
    case CKRST_TOP_SELECTARG1:
    case CKRST_TOP_PREMODULATE: return READS_ARG1;
    case CKRST_TOP_SELECTARG2: return READS_ARG2;
    case CKRST_TOP_MULTIPLYADD:
    case CKRST_TOP_LERP: return READS_ARG1 | READS_ARG2 | READS_ARG0;
    default:
        return (op >= CKRST_TOP_MODULATE && op <= CKRST_TOP_BLENDCURRENTALPHA) ||
                       (op >= CKRST_TOP_MODULATEALPHA_ADDCOLOR && op <= CKRST_TOP_MODULATEINVCOLOR_ADDALPHA) ||
                       op == CKRST_TOP_DOTPRODUCT3
                   ? READS_ARG1 | READS_ARG2
                   : 0;
    }
}

const CKFFFragmentProgramStageField kArguments[2][3] = {
    {CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG2,
     CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG0},
    {CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG2,
     CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG0},
};

// ---------------------------------------------------------------------------
// Compiler.

// A combiner register or argument viewed as its colour and alpha channels,
// which the combiners evaluate separately.
struct Color {
    CKJitValue Rgb; // FLOAT3
    CKJitValue A;   // FLOAT
};

enum Channel { CHANNEL_RGB, CHANNEL_ALPHA };

class NativeFragmentCompiler {
public:
    NativeFragmentCompiler(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout, const UniformRows &rows)
        : m_B(rows.Count), m_Program(key.Program), m_Switches(key.Switches), m_Layout(layout), m_Rows(rows) {}

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
    bool Switch(CKFFNativeFragmentSwitch flag) const { return (m_Switches[0] & (CKDWORD)flag) != 0; }
    bool Switch(CKFFNativeFragmentSwitch first, CKDWORD stage) const {
        return StageSwitch(m_Switches, first, stage);
    }

    Color Split(CKJitValue value) { return {m_B.Swizzle(value, "xyz"), m_B.Component(value, 3)}; }
    CKJitValue Join(const Color &color) { return m_B.Construct({color.Rgb, color.A}); }
    CKJitValue Pick(const Color &color, Channel channel) const { return channel == CHANNEL_RGB ? color.Rgb : color.A; }
    // abs(v) < epsilon ? (v < 0 ? -epsilon : epsilon) : v
    CKJitValue SafeDivisor(CKJitValue v, float epsilon);

    void DeclareInputs();
    void EvaluateStage(CKDWORD stage);
    CKJitValue SampleCoordinate(CKDWORD stage, uint32_t components);
    CKJitValue SampleTexture(CKDWORD stage);
    Color Argument(CKDWORD packedArg, const Color &texture, const Color &current, const Color &stageConstant);
    CKJitValue Combine(CKDWORD op, Channel channel, const Color &arg1, const Color &arg2, const Color &arg0,
                       const Color &destination, const Color &texture);
    CKJitValue DotProduct3(const Color &a, const Color &b);
    CKJitValue StageBlend(const Color &source, const Color &destination, CKDWORD pair);
    CKJitValue BlendFactor(CKDWORD factor, const Color &source, const Color &destination);
    CKJitValue AlphaTestPass(CKJitValue alpha);
    CKJitValue FogFactor();

    CKJitBuilder m_B;
    const CKFFFragmentProgram &m_Program;
    const CKDWORD *m_Switches;
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
    CKJitValue m_PreviousTexture; // FLOAT4
    bool m_PreviousBumpUnorm = false;
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

bool NativeFragmentCompiler::Compile(CKJitFragmentShader &out) {
    DeclareInputs();

    // Antialiased lines fade with the distance to the line centre.
    CKJitValue discard = m_B.Bool(false);
    CKJitValue edgeCoverage;
    if (Switch(CKFF_NATIVE_FRAGMENT_LINE)) {
        const CKJitValue offset = m_B.Mul(m_Varyings[VARYING_LINE_OFFSET], m_B.Component(m_FragCoord, 3));
        edgeCoverage = m_B.Saturate(m_B.Sub(m_B.Float(1.0f), m_B.Length(offset)));
        discard = m_B.LessEqual(edgeCoverage, m_B.Float(0.0f));
    }

    const bool flatShade = m_Program.Get(CKFF_FRAGMENT_PROGRAM_FLAT_SHADE) != 0;
    m_Diffuse = Split(m_Varyings[flatShade ? VARYING_FLAT_COLOR0 : VARYING_COLOR0]);
    m_Specular = Split(m_Varyings[flatShade ? VARYING_FLAT_COLOR1 : VARYING_COLOR1]);
    m_Current = m_Diffuse;
    m_Temp = {m_B.Float3(0.0f, 0.0f, 0.0f), m_B.Float(0.0f)};
    m_TextureFactor = Split(DrawParam(CKFF_DRAW_PARAM_TEXTURE_FACTOR));
    m_PreviousTexture = m_B.Float4(0.0f, 0.0f, 0.0f, 1.0f);

    const CKDWORD lastStage = m_Program.Get(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE);
    for (CKDWORD stage = 0; stage < kStageCount && stage <= lastStage; ++stage) {
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
    if (edgeCoverage.IsValid())
        m_Current.A = m_B.Mul(m_Current.A, edgeCoverage);

    return m_B.Finish(m_B.Saturate(Join(m_Current)), discard, out);
}

void NativeFragmentCompiler::EvaluateStage(CKDWORD stage) {
    const CKDWORD colorOp = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP);
    const CKDWORD alphaOp = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP);
    const Color stageConstant = Split(StageParam(stage, CKFF_STAGE_PARAM_CONSTANT));
    const bool hasTexture = Switch(CKFF_NATIVE_FRAGMENT_TEXTURE, stage);

    CKJitValue texture = SampleTexture(stage);
    if (stage != 0 && m_PreviousColorOp == CKRST_TOP_BUMPENVMAPLUMINANCE) {
        const CKJitValue luminance = BumpEnv(stage - 1, BUMP_ENV_LUMINANCE);
        texture = m_B.Mul(texture, m_B.Saturate(m_B.Add(m_B.Mul(m_B.Component(m_PreviousTexture, 2),
                                                                m_B.Component(luminance, 0)),
                                                        m_B.Component(luminance, 1))));
    }
    const Color tex = Split(texture);

    // CURRENT after a PREMODULATE stage is modulated by this stage's texture,
    // if it has one, separately for the colour and the alpha combiner.
    const Color premodulated =
        hasTexture ? Color{m_B.Mul(m_Current.Rgb, tex.Rgb), m_B.Mul(m_Current.A, tex.A)} : m_Current;
    const Color colorCurrent = m_PreviousColorOp == CKRST_TOP_PREMODULATE ? premodulated : m_Current;
    const Color alphaCurrent = m_PreviousAlphaOp == CKRST_TOP_PREMODULATE ? premodulated : m_Current;

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
        ? StageBlend(tex, m_Current, BlendPair(m_Switches, stage))
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
    m_PreviousBumpUnorm = Switch(CKFF_NATIVE_FRAGMENT_BUMP_UNORM, stage);
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
    if (Switch(CKFF_NATIVE_FRAGMENT_AFFINE)) {
        const CKJitValue affineW = SafeDivisor(m_B.Component(m_Varyings[VARYING_FOG_POS], 0), 0.000001f);
        xy = m_B.Div(xy, affineW);
        if (VARYING_TEXCOORD0 + stage != VARYING_TEXCOORD7_FOG)
            z = m_B.Div(z, affineW);
        w = m_B.Div(w, affineW);
    }

    if (StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED)) {
        const CKJitValue divisor = SafeDivisor(w, 0.0001f);
        xy = m_B.Div(xy, divisor);
        z = m_B.Div(z, divisor);
    }

    // BUMPENVMAP stages perturb the next stage's coordinate by their texel.
    if (stage != 0 &&
        (m_PreviousColorOp == CKRST_TOP_BUMPENVMAP || m_PreviousColorOp == CKRST_TOP_BUMPENVMAPLUMINANCE)) {
        CKJitValue bump = m_B.Swizzle(m_PreviousTexture, "xy");
        if (m_PreviousBumpUnorm) {
            bump = m_B.Min(
                m_B.Max(m_B.Div(m_B.Sub(m_B.Mul(bump, m_B.Float(255.0f)), m_B.Float(128.0f)), m_B.Float(127.0f)),
                        m_B.Float(-1.0f)),
                m_B.Float(1.0f));
        }
        const CKJitValue matrix = BumpEnv(stage - 1, BUMP_ENV_MATRIX);
        xy = m_B.Add(xy, m_B.Construct({m_B.Dot(m_B.Swizzle(matrix, "xy"), bump),
                                        m_B.Dot(m_B.Swizzle(matrix, "zw"), bump)}));
    }

    return components == 2 ? xy : m_B.Construct({xy, z});
}

CKJitValue NativeFragmentCompiler::SampleTexture(CKDWORD stage) {
    if (!Switch(CKFF_NATIVE_FRAGMENT_TEXTURE, stage))
        return m_B.Float4(0.0f, 0.0f, 0.0f, 1.0f);
    // An index past the textures the layout declares samples nothing.
    if (!SamplerDeclared(m_Program, stage, m_Layout))
        return m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f);

    const CKDWORD type = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    const CKJitSamplerDim dim = type == CKFF_SAMPLER_CUBE ? CKJIT_SAMPLER_CUBE
                              : type == CKFF_SAMPLER_VOLUME ? CKJIT_SAMPLER_3D
                                                            : CKJIT_SAMPLER_2D;
    const CKJitValue coordinate = SampleCoordinate(stage, dim == CKJIT_SAMPLER_2D ? 2 : 3);
    const CKJitValue lodBias = Switch(CKFF_NATIVE_FRAGMENT_LOD_BIAS, stage)
        ? m_B.Component(BumpEnv(stage, BUMP_ENV_LUMINANCE), 2)
        : m_B.Float(0.0f);
    const CKJitValue color =
        m_B.Sample(CKFFSamplerSlot(type, SamplerIndex(m_Program, stage, m_Layout), m_Layout), dim, coordinate, lodBias);
    return type == CKFF_SAMPLER_DEPTH ? m_B.Swizzle(color, "xxxx") : color;
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

CKJitValue NativeFragmentCompiler::BlendFactor(CKDWORD factor, const Color &source, const Color &destination) {
    const CKJitValue one = m_B.Float(1.0f);
    switch (factor) {
    case VXBLEND_ONE: return m_B.Float3(1.0f, 1.0f, 1.0f);
    case VXBLEND_SRCCOLOR: return source.Rgb;
    case VXBLEND_INVSRCCOLOR: return m_B.Sub(one, source.Rgb);
    case VXBLEND_SRCALPHA: return m_B.Splat(source.A, 3);
    case VXBLEND_INVSRCALPHA: return m_B.Splat(m_B.Sub(one, source.A), 3);
    case VXBLEND_DESTALPHA: return m_B.Splat(destination.A, 3);
    case VXBLEND_INVDESTALPHA: return m_B.Splat(m_B.Sub(one, destination.A), 3);
    case VXBLEND_DESTCOLOR: return destination.Rgb;
    case VXBLEND_INVDESTCOLOR: return m_B.Sub(one, destination.Rgb);
    case VXBLEND_SRCALPHASAT: return m_B.Splat(m_B.Min(source.A, m_B.Sub(one, destination.A)), 3);
    default: return m_B.Float3(0.0f, 0.0f, 0.0f);
    }
}

CKJitValue NativeFragmentCompiler::StageBlend(const Color &source, const Color &destination, CKDWORD pair) {
    // STAGEBLEND blends this stage's texture over CURRENT with a factor pair.
    pair = CanonicalBlendPair(pair);
    return m_B.Saturate(m_B.Add(m_B.Mul(source.Rgb, BlendFactor(pair >> 4, source, destination)),
                                m_B.Mul(destination.Rgb, BlendFactor(pair & 15, source, destination))));
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

bool CKFFNativeFragmentKey::operator==(const CKFFNativeFragmentKey &other) const {
    return Program == other.Program && std::memcmp(Switches, other.Switches, sizeof(Switches)) == 0;
}

CKFFNativeFragmentKey CKFFNativeFragmentDrawKey(const CKFFFragmentProgram &program, const CKFFConstantSet &constants,
                                                bool shaderSampling) {
    CKFFNativeFragmentKey key;
    key.Program = program;
    CKDWORD &switches = key.Switches[0];
    if (shaderSampling)
        switches |= CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;
    if (ConstantFloat(constants, CKRST_BLOCK_DRAW_PARAMS, CKFF_DRAW_PARAM_MATERIAL_POWER, 2) > 0.5f)
        switches |= CKFF_NATIVE_FRAGMENT_AFFINE;
    if (ConstantFloat(constants, CKRST_BLOCK_DRAW_PARAMS, CKFF_DRAW_PARAM_MATERIAL_POWER, 3) > 2.5f)
        switches |= CKFF_NATIVE_FRAGMENT_LINE;
    for (CKDWORD stage = 0; stage < kStageCount; ++stage) {
        const CKDWORD coord = CKFFStageParamIndex(stage, CKFF_STAGE_PARAM_COORD);
        const CKDWORD luminance = stage * BUMP_ENV_ROWS_PER_STAGE + BUMP_ENV_LUMINANCE;
        if (ConstantFloat(constants, CKRST_BLOCK_STAGE_PARAMS, coord, 2) > 0.5f)
            switches |= (CKDWORD)CKFF_NATIVE_FRAGMENT_TEXTURE << stage;
        const CKDWORD textureFlags = (CKDWORD)FloatToInt(ConstantFloat(constants, CKRST_BLOCK_STAGE_PARAMS, coord, 1));
        if ((textureFlags & CKFF_TTF_BUMP_UNORM) != 0)
            switches |= (CKDWORD)CKFF_NATIVE_FRAGMENT_BUMP_UNORM << stage;
        if (ConstantFloat(constants, CKRST_BLOCK_BUMP_ENV, luminance, 2) != 0.0f)
            switches |= (CKDWORD)CKFF_NATIVE_FRAGMENT_LOD_BIAS << stage;
        if (shaderSampling) {
            const float state = ConstantFloat(constants, CKRST_BLOCK_BUMP_ENV, luminance, 3);
            SetSampling(key.Switches, stage, DrawSampling((CKDWORD)FloatToInt(state), textureFlags));
        }
        const float pair = ConstantFloat(constants, CKRST_BLOCK_STAGE_PARAMS, coord, 3) + 0.5f;
        SetBlendPair(key.Switches, stage, (CKDWORD)FloatToInt(pair));
    }
    return key;
}

void CKFFCanonicalizeNativeFragmentKey(CKFFNativeFragmentKey &key, CKFFSamplerLayout layout) {
    const CKFFNativeFragmentKey raw = key;
    const bool shaderSampling = (raw.Switches[0] & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) != 0;
    const CKFFFragmentProgram &from = raw.Program;
    CKFFFragmentProgram &to = key.Program;
    to = CKFFFragmentProgram();
    for (CKDWORD word = 0; word < CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT; ++word)
        key.Switches[word] = 0;

    // The stages the shaders evaluate.
    const CKDWORD lastStage = from.Get(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE);
    CKDWORD count = 0;
    while (count < kStageCount && count <= lastStage &&
           from.GetStage(count, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP) != CKRST_TOP_DISABLE) {
        ++count;
    }
    to.Set(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE, count != 0 ? count - 1 : 0);
    if (count == 0)
        to.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, CKRST_TOP_DISABLE);

    // Operations as the combiners evaluate them, and the arguments they read.
    // A stage reads its texture in its combiners, or to premodulate CURRENT.
    CKDWORD colorOps[kStageCount];
    bool readsTexture[kStageCount + 1] = {};
    CKDWORD previousColorOp = 0;
    CKDWORD previousAlphaOp = 0;
    for (CKDWORD stage = 0; stage < count; ++stage) {
        CKDWORD colorOp = from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP);
        CKDWORD alphaOp = from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP);
        // Unknown operations keep CURRENT, like 0; DOTPRODUCT3 overwrites the
        // alpha combiner's result, whose operation then only premodulates.
        if (colorOp > CKFF_TOP_STAGEBLEND)
            colorOp = 0;
        const bool alphaLive = colorOp != CKRST_TOP_DOTPRODUCT3;
        if (!alphaLive)
            alphaOp = alphaOp == CKRST_TOP_PREMODULATE ? alphaOp : 0;
        else if (alphaOp == CKRST_TOP_BUMPENVMAP || alphaOp == CKRST_TOP_BUMPENVMAPLUMINANCE)
            alphaOp = CKRST_TOP_DISABLE; // the destination, like DISABLE
        else if (alphaOp >= CKFF_TOP_STAGEBLEND)
            alphaOp = 0;
        to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, colorOp);
        to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP, alphaOp);
        to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_RESULT_IS_TEMP,
                    from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_RESULT_IS_TEMP));

        bool reads = colorOp == CKRST_TOP_BLENDTEXTUREALPHA || colorOp == CKRST_TOP_BLENDTEXTUREALPHAPM ||
                     colorOp == CKFF_TOP_STAGEBLEND || alphaOp == CKRST_TOP_BLENDTEXTUREALPHA ||
                     alphaOp == CKRST_TOP_BLENDTEXTUREALPHAPM;
        const CKDWORD ops[2] = {colorOp, alphaOp};
        const bool premodulated[2] = {previousColorOp == CKRST_TOP_PREMODULATE,
                                      previousAlphaOp == CKRST_TOP_PREMODULATE};
        for (int channel = 0; channel < 2; ++channel) {
            const CKDWORD read = channel == CHANNEL_ALPHA && !alphaLive ? 0 : ArgumentsRead(ops[channel]);
            for (int k = 0; k < 3; ++k) {
                if ((read & 1u << k) == 0)
                    continue;
                const CKDWORD arg = from.GetStage(stage, kArguments[channel][k]);
                to.SetStage(stage, kArguments[channel][k], arg);
                const CKDWORD base = CKFFFragmentProgram::UnpackArg(arg) & 7u;
                reads = reads || base == CKRST_TA_TEXTURE || (premodulated[channel] && base == CKRST_TA_CURRENT);
            }
        }
        colorOps[stage] = colorOp;
        readsTexture[stage] = reads;
        previousColorOp = colorOp;
        previousAlphaOp = alphaOp;
    }

    // From the last stage back: a texture is also read by the next stage's
    // bump offset where that samples, and by its luminance where its texture
    // is read. A texture no value depends on is dropped.
    bool samples[kStageCount + 1] = {};
    bool anySamples = false;
    bool emulates = false;
    for (CKDWORD stage = count; stage-- > 0;) {
        const bool bump = colorOps[stage] == CKRST_TOP_BUMPENVMAP || colorOps[stage] == CKRST_TOP_BUMPENVMAPLUMINANCE;
        if ((bump && samples[stage + 1]) ||
            (colorOps[stage] == CKRST_TOP_BUMPENVMAPLUMINANCE && readsTexture[stage + 1])) {
            readsTexture[stage] = true;
        }
        const bool textured = readsTexture[stage] && StageSwitch(raw.Switches, CKFF_NATIVE_FRAGMENT_TEXTURE, stage);
        // Outside the wide 2D layout, a shader-sampling shader compares an
        // undeclared depth texture's border.
        const CKDWORD type = from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
        const CKDWORD compareFunc =
            shaderSampling && type == CKFF_SAMPLER_DEPTH
                ? CanonicalCompareFunc(from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC), layout)
                : 0;
        const bool compared = compareFunc != 0 && layout != CKFF_SAMPLER_LAYOUT_WIDE_2D;
        const bool declared = SamplerDeclared(from, stage, layout);
        samples[stage] = textured && (declared || compared);
        anySamples = anySamples || samples[stage];

        if (textured) {
            key.Switches[0] |= (CKDWORD)CKFF_NATIVE_FRAGMENT_TEXTURE << stage;
            to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, type);
            if (!StageIndexed(type, layout))
                to.SetSamplerOrdinal(stage, from.GetSamplerOrdinal(stage));
        }
        if (samples[stage]) {
            to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED,
                        from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED));
            key.Switches[0] |= raw.Switches[0] & (CKDWORD)CKFF_NATIVE_FRAGMENT_LOD_BIAS << stage;
            if (shaderSampling) {
                const CKDWORD sampling = CanonicalSampling(Sampling(raw.Switches, stage), type, compared, declared);
                to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, compareFunc);
                SetSampling(key.Switches, stage, sampling);
                emulates = emulates || compareFunc != 0 || sampling != 0;
            }
        }
        if (bump && samples[stage + 1])
            key.Switches[0] |= raw.Switches[0] & (CKDWORD)CKFF_NATIVE_FRAGMENT_BUMP_UNORM << stage;
        if (colorOps[stage] == CKFF_TOP_STAGEBLEND)
            SetBlendPair(key.Switches, stage, CanonicalBlendPair(BlendPair(raw.Switches, stage)));
    }
    if (anySamples)
        key.Switches[0] |= raw.Switches[0] & CKFF_NATIVE_FRAGMENT_AFFINE;
    key.Switches[0] |= raw.Switches[0] & CKFF_NATIVE_FRAGMENT_LINE;
    // A shader-sampling key that emulates nothing compiles to the native
    // shader.
    if (emulates)
        key.Switches[0] |= CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;

    // The fragment state; the vertex fog mode and range fog are the vertex
    // shaders', and a test without a comparison passes every fragment.
    const CKDWORD alphaFunc = from.Get(CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC);
    if (from.Get(CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED) && alphaFunc >= VXCMP_NEVER &&
        alphaFunc <= VXCMP_GREATEREQUAL) {
        to.Set(CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED, 1);
        to.Set(CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC, alphaFunc);
    }
    if (from.Get(CKFF_FRAGMENT_PROGRAM_FOG_ENABLED)) {
        to.Set(CKFF_FRAGMENT_PROGRAM_FOG_ENABLED, 1);
        to.Set(CKFF_FRAGMENT_PROGRAM_PIXEL_FOG_MODE, from.Get(CKFF_FRAGMENT_PROGRAM_PIXEL_FOG_MODE));
    }
    to.Set(CKFF_FRAGMENT_PROGRAM_FLAT_SHADE, from.Get(CKFF_FRAGMENT_PROGRAM_FLAT_SHADE));
    to.Set(CKFF_FRAGMENT_PROGRAM_GLOBAL_SPECULAR_ENABLED, from.Get(CKFF_FRAGMENT_PROGRAM_GLOBAL_SPECULAR_ENABLED));
}

bool CKFFCompileNativeFragmentProgram(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                      CKJitFragmentShader &out) {
    UniformRows rows;
    if ((unsigned)layout >= CKFF_SAMPLER_LAYOUT_COUNT || !ResolveUniformRows(layout, rows))
        return false;
    if ((key.Switches[0] & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) != 0)
        return false;
    NativeFragmentCompiler compiler(key, layout, rows);
    return compiler.Compile(out);
}
