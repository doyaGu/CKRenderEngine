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
    uint32_t BorderColor; // of sampler slot 0, the other slots' following
    uint32_t SamplerInfo; // likewise
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

    // The border colours and sampler metadata the shader-sampling shaders
    // read: a row of each per sampler slot, in the same buffer.
    if (desc.Samplers.Size() != CKFF_SAMPLER_SLOT_COUNT)
        return false;
    for (int slot = 0; slot < desc.Samplers.Size(); ++slot) {
        const CKFFSamplerBinding &sampler = desc.Samplers[slot];
        if (slot == 0) {
            rows.BorderColor = sampler.BorderColorOffset / 16u;
            rows.SamplerInfo = sampler.SamplerStateOffset / 16u;
        }
        if (sampler.NativeSlot != (CKDWORD)slot || sampler.MetadataBufferSlot != buffer ||
            sampler.BorderColorOffset != (rows.BorderColor + slot) * 16u ||
            sampler.SamplerStateOffset != (rows.SamplerInfo + slot) * 16u) {
            return false;
        }
    }
    return rows.Count != 0 && rows.BorderColor + CKFF_SAMPLER_SLOT_COUNT <= rows.Count &&
           rows.SamplerInfo + CKFF_SAMPLER_SLOT_COUNT <= rows.Count;
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

// A 2D texture the shader filters itself: its slot, its sampler's metadata
// and the stage's sampler state.
struct ShaderSampler {
    uint32_t Slot;
    CKJitValue Border;        // FLOAT4
    CKJitValue Info;          // FLOAT4: address modes, min, mag and mip filters
    CKJitValue Modes;         // INT2: the U and V address modes
    CKJitValue MipFilter;     // INT
    CKJitValue Levels;        // INT
    CKJitValue MinMip;        // FLOAT: the lowest mip level sampled
    CKJitValue MaxAnisotropy; // FLOAT
};

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
    // A field of an INT bit word.
    CKJitValue Bits(CKJitValue word, uint32_t shift, uint32_t mask) {
        const CKJitValue shifted = shift != 0 ? m_B.IntShiftRight(word, m_B.Int((int32_t)shift)) : word;
        return m_B.IntAnd(shifted, m_B.Int((int32_t)mask));
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
    CKJitValue ShaderSample2D(CKDWORD stage, uint32_t slot, CKJitValue coordinate, CKJitValue lodBias);
    CKJitValue MirrorOnce(CKJitValue coordinate, CKDWORD sampling);
    CKJitValue Filter2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue dx, CKJitValue dy,
                        CKJitValue implicitLod, CKJitValue lodBias);
    CKJitValue Mips2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue lod, CKJitValue filtered);
    CKJitValue Level2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue mip, CKJitValue filtered);
    CKJitValue Texel2D(const ShaderSampler &sampler, CKJitValue texel, CKJitValue outside, CKJitValue mip);
    void Address(CKJitValue texel, CKJitValue extent, CKJitValue mode, CKJitValue &index, CKJitValue &outside);
    CKJitValue CompareDepth(CKJitValue depth, CKJitValue reference, CKDWORD func);
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
    const uint32_t slot = CKFFSamplerSlot(type, SamplerIndex(m_Program, stage, m_Layout), m_Layout);
    const CKJitValue lodBias = Switch(CKFF_NATIVE_FRAGMENT_LOD_BIAS, stage)
        ? m_B.Component(BumpEnv(stage, BUMP_ENV_LUMINANCE), 2)
        : m_B.Float(0.0f);
    if (dim != CKJIT_SAMPLER_2D || !Switch(CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING)) {
        const CKJitValue color = m_B.Sample(slot, dim, SampleCoordinate(stage, dim == CKJIT_SAMPLER_2D ? 2 : 3), lodBias);
        return type == CKFF_SAMPLER_DEPTH ? m_B.Swizzle(color, "xxxx") : color;
    }

    // The shader-sampling shaders sample 2D and depth textures themselves;
    // the wide 2D layout compares the depths sampled with the coordinate's z.
    const CKJitValue coordinate = SampleCoordinate(stage, type == CKFF_SAMPLER_DEPTH ? 3 : 2);
    const CKJitValue color = ShaderSample2D(stage, slot, m_B.Swizzle(coordinate, "xy"), lodBias);
    if (type != CKFF_SAMPLER_DEPTH)
        return color;
    const CKDWORD func = m_Layout == CKFF_SAMPLER_LAYOUT_WIDE_2D
        ? CanonicalCompareFunc(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC), m_Layout)
        : 0;
    return func != 0 ? m_B.Splat(CompareDepth(m_B.Component(color, 0), m_B.Component(coordinate, 2), func), 4)
                     : m_B.Swizzle(color, "xxxx");
}

// Samples a 2D texture as native_sampling.hlsli: mirror-once folds the
// coordinate, explicit gradients of the unfolded one, scaled by the LOD bias,
// stand for the implicit ones, and border addressing filters texels loaded
// by the shader.
CKJitValue NativeFragmentCompiler::ShaderSample2D(CKDWORD stage, uint32_t slot, CKJitValue coordinate,
                                                  CKJitValue lodBias) {
    const CKDWORD sampling = Sampling(m_Switches, stage);
    const CKJitValue uv = MirrorOnce(coordinate, sampling);
    const bool border = (sampling & CKFF_NATIVE_FRAGMENT_BORDER) != 0;
    // Levels and derivatives are taken ahead of the filter's regions, which
    // not every pixel of a quad may run.
    CKJitValue implicitLod, dx, dy;
    if ((sampling & CKFF_NATIVE_FRAGMENT_GRADIENT) != 0) {
        dx = m_B.Ddx(coordinate);
        dy = m_B.Ddy(coordinate);
        if (Switch(CKFF_NATIVE_FRAGMENT_LOD_BIAS, stage)) {
            const CKJitValue scale = m_B.Exp2(lodBias);
            dx = m_B.Mul(dx, scale);
            dy = m_B.Mul(dy, scale);
        }
        if (!border)
            return m_B.SampleGrad(slot, CKJIT_SAMPLER_2D, uv, dx, dy);
    } else {
        if (!border)
            return m_B.Sample(slot, CKJIT_SAMPLER_2D, uv, lodBias);
        implicitLod = m_B.CalcLod(slot, CKJIT_SAMPLER_2D, uv);
        dx = m_B.Ddx(uv);
        dy = m_B.Ddy(uv);
    }

    const CKJitValue state = m_B.FloatToInt(m_B.Component(BumpEnv(stage, BUMP_ENV_LUMINANCE), 3));
    const CKJitValue info = m_B.Uniform(m_Rows.SamplerInfo + slot);
    const CKJitValue modes = m_B.FloatToInt(m_B.Component(info, 0));
    ShaderSampler sampler;
    sampler.Slot = slot;
    sampler.Border = m_B.Uniform(m_Rows.BorderColor + slot);
    sampler.Info = info;
    sampler.Modes = m_B.Construct({Bits(modes, 0, 15), Bits(modes, 4, 15)});
    sampler.MipFilter = Bits(m_B.FloatToInt(m_B.Component(info, 3)), 0, 15);
    sampler.Levels = m_B.TextureLevels(slot, CKJIT_SAMPLER_2D);
    sampler.MinMip =
        m_B.IntToFloat(Bits(state, CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT, CKFF_SAMPLER_SHADER_MIN_MIP_MASK));
    sampler.MaxAnisotropy =
        m_B.IntToFloat(Bits(state, CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT, CKFF_SAMPLER_SHADER_ANISOTROPY_MASK));
    return Filter2D(sampler, uv, dx, dy, implicitLod, lodBias);
}

// Mirror-once folds the axes it applies to into [0, 1], where clamping to the
// edge leaves them.
CKJitValue NativeFragmentCompiler::MirrorOnce(CKJitValue coordinate, CKDWORD sampling) {
    static const CKDWORD kAxes[3] = {CKFF_NATIVE_FRAGMENT_MIRROR_U, CKFF_NATIVE_FRAGMENT_MIRROR_V,
                                     CKFF_NATIVE_FRAGMENT_MIRROR_W};
    const uint32_t components = CKJitComponentCount(m_B.TypeOf(coordinate));
    uint32_t mirrored = 0;
    for (uint32_t i = 0; i < components; ++i)
        mirrored += (sampling & kAxes[i]) != 0 ? 1 : 0;
    if (mirrored == 0)
        return coordinate;
    if (mirrored == components)
        return m_B.Saturate(m_B.Abs(coordinate));

    CKJitValue axes[3];
    for (uint32_t i = 0; i < components; ++i) {
        axes[i] = m_B.Component(coordinate, i);
        if ((sampling & kAxes[i]) != 0)
            axes[i] = m_B.Saturate(m_B.Abs(axes[i]));
    }
    return components == 2 ? m_B.Construct({axes[0], axes[1]}) : m_B.Construct({axes[0], axes[1], axes[2]});
}

// Filters texels at the level of the gradients, or of the implicit ones with
// the LOD bias, no lower than the minimum mip level. An anisotropic filter
// minifying averages taps along the longer gradient at the level of the
// shorter, as many as fit, up to the maximum anisotropy; the others take a
// single tap at the coordinate.
CKJitValue NativeFragmentCompiler::Filter2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue dx, CKJitValue dy,
                                            CKJitValue implicitLod, CKJitValue lodBias) {
    const CKJitValue size = m_B.IntToFloat(m_B.TextureSize(sampler.Slot, CKJIT_SAMPLER_2D, m_B.Int(0)));
    const CKJitValue lengthX = m_B.Length(m_B.Mul(dx, size));
    const CKJitValue lengthY = m_B.Length(m_B.Mul(dy, size));
    const CKJitValue major = m_B.Max(lengthX, lengthY);
    // Explicit gradients carry the LOD bias.
    const CKJitValue tapBias = implicitLod.IsValid() ? lodBias : m_B.Float(0.0f);
    const CKJitValue lod = implicitLod.IsValid()
        ? m_B.Max(m_B.Add(implicitLod, lodBias), sampler.MinMip)
        : m_B.Max(m_B.Log2(m_B.Max(major, m_B.Float(0.000001f))), sampler.MinMip);

    const CKJitValue minifies = m_B.Greater(lod, m_B.Float(0.0f));
    const CKJitValue filter =
        m_B.FloatToInt(m_B.Select(minifies, m_B.Component(sampler.Info, 1), m_B.Component(sampler.Info, 2)));
    const CKJitValue anisotropic = m_B.And(m_B.IntEqual(filter, m_B.Int(VXTEXTUREFILTER_ANISOTROPIC)), minifies);
    const CKJitValue tapLimit = m_B.Max(sampler.MaxAnisotropy, m_B.Float(1.0f));
    const CKJitValue minor = m_B.Max(m_B.Min(lengthX, lengthY), m_B.Div(major, tapLimit));
    const CKJitValue ratio = m_B.Ceil(m_B.Div(major, m_B.Max(minor, m_B.Float(1.0f))));
    const CKJitValue taps =
        m_B.Select(anisotropic, m_B.FloatToInt(m_B.Min(m_B.Max(ratio, m_B.Float(1.0f)), tapLimit)), m_B.Int(1));
    const CKJitValue tapCount = m_B.IntToFloat(taps);
    const CKJitValue longer = m_B.Select(m_B.Greater(lengthX, lengthY), dx, dy);
    const CKJitValue step = m_B.Select(anisotropic, m_B.Div(longer, tapCount), m_B.Float2(0.0f, 0.0f));
    const CKJitValue tapLod = m_B.Select(
        anisotropic, m_B.Max(m_B.Add(m_B.Log2(m_B.Max(minor, m_B.Float(1.0f))), tapBias), sampler.MinMip), lod);
    const CKJitValue filtered = m_B.IntNotEqual(filter, m_B.Int(VXTEXTUREFILTER_NEAREST));
    const CKJitValue center = m_B.Mul(m_B.IntToFloat(m_B.IntSub(taps, m_B.Int(1))), m_B.Float(0.5f));

    CKJitValue sum;
    const CKJitValue tap = m_B.Loop(taps, CKFF_SAMPLER_SHADER_ANISOTROPY_MASK, {m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f)}, &sum);
    const CKJitValue offset = m_B.Mul(m_B.Sub(m_B.IntToFloat(tap), center), step);
    const CKJitValue color = Mips2D(sampler, m_B.Add(uv, offset), tapLod, filtered);
    return m_B.Div(m_B.EndLoop(m_B.Add(sum, color)), tapCount);
}

// The texel of the mip the mip filter picks for a level, or the blend of the
// two around it for linear and anisotropic mip filters; without a mip filter,
// of the base mip.
CKJitValue NativeFragmentCompiler::Mips2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue lod,
                                          CKJitValue filtered) {
    const CKJitValue top = m_B.IntSub(sampler.Levels, m_B.Int(1));
    const CKJitValue level = m_B.Select(m_B.IntEqual(sampler.MipFilter, m_B.Int(0)), m_B.Float(0.0f),
                                        m_B.Min(m_B.Max(lod, m_B.Float(0.0f)), m_B.IntToFloat(top)));
    const CKJitValue linear = m_B.Or(m_B.IntEqual(sampler.MipFilter, m_B.Int(VXTEXTUREFILTER_LINEAR)),
                                     m_B.IntEqual(sampler.MipFilter, m_B.Int(VXTEXTUREFILTER_ANISOTROPIC)));
    const CKJitValue below = m_B.Floor(level);
    const CKJitValue lower = m_B.FloatToInt(below);
    const CKJitValue upper = m_B.IntMin(m_B.IntAdd(lower, m_B.Int(1)), top);
    const CKJitValue nearest = m_B.FloatToInt(m_B.Floor(m_B.Add(level, m_B.Float(0.5f))));

    // One iteration a mip; the first keeps the lower or only one.
    const CKJitValue zero = m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f);
    CKJitValue texels[2];
    const CKJitValue index = m_B.Loop(m_B.Select(linear, m_B.Int(2), m_B.Int(1)), 2, {zero, zero}, texels);
    const CKJitValue first = m_B.IntEqual(index, m_B.Int(0));
    const CKJitValue texel =
        Level2D(sampler, uv, m_B.Select(linear, m_B.Select(first, lower, upper), nearest), filtered);
    m_B.EndLoop({m_B.Select(first, texel, texels[0]), texel}, texels);
    return m_B.Select(linear, m_B.Lerp(texels[0], texels[1], m_B.Sub(level, below)), texels[0]);
}

// The texel of a mip at a coordinate, or the bilinear blend of the four
// around it.
CKJitValue NativeFragmentCompiler::Level2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue mip,
                                           CKJitValue filtered) {
    const CKJitValue extent = m_B.TextureSize(sampler.Slot, CKJIT_SAMPLER_2D, mip);
    const CKJitValue scaled = m_B.Mul(uv, m_B.IntToFloat(extent));
    CKJitValue index, outside;
    m_B.If(filtered);
    // Columns x and x + 1 and rows y and y + 1 from the texel below and left
    // of the coordinate, addressed together.
    const CKJitValue corner = m_B.Sub(scaled, m_B.Float(0.5f));
    const CKJitValue base = m_B.Floor(corner);
    const CKJitValue texel = m_B.FloatToInt(base);
    Address(m_B.Construct({texel, m_B.IntAdd(texel, m_B.Int(1))}), m_B.Construct({extent, extent}),
            m_B.Construct({sampler.Modes, sampler.Modes}), index, outside);
    const auto tap = [&](const char *axes) {
        return Texel2D(sampler, m_B.Swizzle(index, axes), m_B.Any(m_B.Swizzle(outside, axes)), mip);
    };
    const CKJitValue t00 = tap("xy");
    const CKJitValue t10 = tap("zy");
    const CKJitValue t01 = tap("xw");
    const CKJitValue t11 = tap("zw");
    const CKJitValue weight = m_B.Sub(corner, base);
    const CKJitValue wx = m_B.Component(weight, 0);
    const CKJitValue bilinear = m_B.Lerp(m_B.Lerp(t00, t10, wx), m_B.Lerp(t01, t11, wx), m_B.Component(weight, 1));
    m_B.Else({bilinear});
    Address(m_B.FloatToInt(m_B.Floor(scaled)), extent, sampler.Modes, index, outside);
    return m_B.EndIf(Texel2D(sampler, index, m_B.Any(outside), mip));
}

// A texel of a mip, or the border colour outside a border-addressed axis.
CKJitValue NativeFragmentCompiler::Texel2D(const ShaderSampler &sampler, CKJitValue texel, CKJitValue outside,
                                           CKJitValue mip) {
    const CKJitValue loaded = m_B.Load(sampler.Slot, CKJIT_SAMPLER_2D, m_B.Construct({texel, mip}));
    return m_B.Select(outside, sampler.Border, loaded);
}

// Addresses texels along axes of the given extents and address modes: wrap
// and mirror repeat the mip and the other modes clamp to its edge, border
// addressing marking the texels beyond it outside.
void NativeFragmentCompiler::Address(CKJitValue texel, CKJitValue extent, CKJitValue mode, CKJitValue &index,
                                     CKJitValue &outside) {
    const CKJitValue period = m_B.IntAdd(extent, extent);
    const CKJitValue folded = m_B.IntMod(texel, period);
    const CKJitValue mirrored =
        m_B.Select(m_B.IntLess(folded, extent), folded, m_B.IntSub(m_B.IntSub(period, folded), m_B.Int(1)));
    const CKJitValue wrapped = m_B.IntMod(texel, extent);
    const CKJitValue clamped = m_B.IntMin(m_B.IntMax(texel, m_B.Int(0)), m_B.IntSub(extent, m_B.Int(1)));
    index = m_B.Select(m_B.IntEqual(mode, m_B.Int(VXTEXTURE_ADDRESSWRAP)), wrapped,
                       m_B.Select(m_B.IntEqual(mode, m_B.Int(VXTEXTURE_ADDRESSMIRROR)), mirrored, clamped));
    outside = m_B.And(m_B.IntEqual(mode, m_B.Int(VXTEXTURE_ADDRESSBORDER)),
                      m_B.Or(m_B.IntLess(texel, m_B.Int(0)), m_B.IntGreaterEqual(texel, extent)));
}

// A comparison of a reference with a depth, 1 where it passes: functions 1
// to 6 are less to not equal, 7 never passes and 8 always does.
CKJitValue NativeFragmentCompiler::CompareDepth(CKJitValue depth, CKJitValue reference, CKDWORD func) {
    CKJitValue pass;
    switch (func) {
    case 1: pass = m_B.Less(reference, depth); break;
    case 2: pass = m_B.LessEqual(reference, depth); break;
    case 3: pass = m_B.Equal(reference, depth); break;
    case 4: pass = m_B.GreaterEqual(reference, depth); break;
    case 5: pass = m_B.Greater(reference, depth); break;
    case 6: pass = m_B.NotEqual(reference, depth); break;
    case 7: return m_B.Float(0.0f);
    default: return m_B.Float(1.0f);
    }
    return m_B.Select(pass, m_B.Float(1.0f), m_B.Float(0.0f));
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
    // Not emulated yet: volume textures, and depth comparisons outside the
    // wide 2D layout.
    CKFFNativeFragmentKey canonical = key;
    CKFFCanonicalizeNativeFragmentKey(canonical, layout);
    if ((canonical.Switches[0] & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) != 0) {
        for (CKDWORD stage = 0; stage < kStageCount; ++stage) {
            const CKDWORD type = canonical.Program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
            if ((type == CKFF_SAMPLER_VOLUME && Sampling(canonical.Switches, stage) != 0) ||
                (layout != CKFF_SAMPLER_LAYOUT_WIDE_2D &&
                 canonical.Program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC) != 0)) {
                return false;
            }
        }
    }
    NativeFragmentCompiler compiler(key, layout, rows);
    return compiler.Compile(out);
}
