#include "CKFFNativeFragmentJit.h"

#include "CKFFShaderInterface.h"
#include "CKFFStageState.h"
#include "CKJitBuilder.h"

#include <cstring>

namespace {

const CKDWORD kStageCount = CKFF_FRAGMENT_PROGRAM_STAGE_COUNT;

// The first row of a block in the native fragment uniform buffers.
struct UniformBlock {
    uint32_t Buffer;
    uint32_t Row;
};

// The native fragment uniform buffers (CKFFBuildProgramInterface).
struct UniformRows {
    UniformBlock DrawParams;
    UniformBlock BumpEnv;
    UniformBlock StageParams;
    UniformBlock BorderColor; // of sampler slot 0, the other slots' following
    UniformBlock SamplerInfo; // likewise
    uint32_t BufferCount;
    uint32_t Counts[CKJIT_MAX_UNIFORM_BUFFERS]; // float4 rows of each buffer
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
        UniformBlock *Rows;
    };
    const Block blocks[] = {
        {CKRST_BLOCK_DRAW_PARAMS, CKFF_DRAW_PARAM_VEC4_COUNT, &rows.DrawParams},
        {CKRST_BLOCK_BUMP_ENV, kStageCount * BUMP_ENV_ROWS_PER_STAGE, &rows.BumpEnv},
        {CKRST_BLOCK_STAGE_PARAMS, CKFF_STAGE_PARAM_VEC4_COUNT, &rows.StageParams},
    };

    // Every packed format shares the native layout; SPIR-V stands for them.
    const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, CKRST_SHADER_FORMAT_SPIRV, FALSE, FALSE, layout);
    rows.BufferCount = 0;
    std::memset(rows.Counts, 0, sizeof(rows.Counts));
    for (int i = 0; i < desc.UniformBuffers.Size(); ++i) {
        const CKFFUniformBufferBinding &binding = desc.UniformBuffers[i];
        if (binding.Stage != CKRST_SHADER_PIXEL)
            continue;
        if (binding.Slot >= CKJIT_MAX_UNIFORM_BUFFERS)
            return false;
        rows.Counts[binding.Slot] = binding.Size / 16u;
        if (binding.Slot >= rows.BufferCount)
            rows.BufferCount = binding.Slot + 1u;
    }
    // A block's rows all lie in its buffer.
    auto fits = [&rows](const UniformBlock &block, uint32_t count) {
        return block.Buffer < rows.BufferCount && block.Row + count <= rows.Counts[block.Buffer];
    };

    for (const Block &block : blocks) {
        bool found = false;
        for (int i = 0; i < desc.Uniforms.Size(); ++i) {
            const CKFFUniformBinding &uniform = desc.Uniforms[i];
            if (uniform.Stage != CKRST_SHADER_PIXEL || uniform.Slot != (CKDWORD)block.Id)
                continue;
            *block.Rows = {(uint32_t)uniform.BufferSlot, (uint32_t)uniform.Offset / 16u};
            if (uniform.Offset % 16u != 0 || uniform.Count < block.MinRows || !fits(*block.Rows, block.MinRows))
                return false;
            found = true;
            break;
        }
        if (!found)
            return false;
    }

    // The border colours and sampler metadata the shader-sampling shaders
    // read: a row of each per sampler slot, in one buffer.
    if (desc.Samplers.Size() != CKFF_SAMPLER_SLOT_COUNT)
        return false;
    for (int slot = 0; slot < desc.Samplers.Size(); ++slot) {
        const CKFFSamplerBinding &sampler = desc.Samplers[slot];
        if (slot == 0) {
            rows.BorderColor = {(uint32_t)sampler.MetadataBufferSlot, (uint32_t)sampler.BorderColorOffset / 16u};
            rows.SamplerInfo = {(uint32_t)sampler.MetadataBufferSlot, (uint32_t)sampler.SamplerStateOffset / 16u};
        }
        if (sampler.NativeSlot != (CKDWORD)slot || sampler.MetadataBufferSlot != rows.BorderColor.Buffer ||
            sampler.BorderColorOffset != (rows.BorderColor.Row + slot) * 16u ||
            sampler.SamplerStateOffset != (rows.SamplerInfo.Row + slot) * 16u) {
            return false;
        }
    }
    return fits(rows.BorderColor, CKFF_SAMPLER_SLOT_COUNT) && fits(rows.SamplerInfo, CKFF_SAMPLER_SLOT_COUNT);
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
// them through for an unknown function as for none. The other layouts compare
// every texel, passing an unknown function's depths through, as function 9,
// and so do the wide 2D layout's comparison shaders with explicit gradients;
// without, they compare in hardware with the sampler's function, as 1.
CKDWORD CanonicalCompareFunc(CKDWORD func, CKFFSamplerLayout layout, CKDWORD comparisons, CKDWORD sampling) {
    if (func == 0)
        return 0;
    if (comparisons != 0 && (sampling & CKFF_NATIVE_FRAGMENT_GRADIENT) == 0)
        return 1;
    if (func <= 8)
        return func;
    return layout == CKFF_SAMPLER_LAYOUT_WIDE_2D && comparisons == 0 ? 0 : 9;
}

// The sampling flags a shader-sampling shader tests for a stage that samples.
// Mirror-once applies to the axes of the sampler type, and never to cubes,
// which sample in hardware. A texture addressed by the shader tests border
// addressing, except an undeclared depth texture, whose texels are all the
// border. A 2D texture otherwise tests only explicit gradients, and a volume
// without anisotropy its minimum mip level only where that alone keeps it
// from hardware sampling. A depth texture a comparison shader compares tests
// mirror-once and explicit gradients, which choose between comparing its
// texels and comparing in hardware.
CKDWORD CanonicalSampling(CKDWORD sampling, CKDWORD type, bool compared, bool declared, CKDWORD comparisons) {
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
        if (compared && comparisons != 0)
            return sampling & (mirror2D | CKFF_NATIVE_FRAGMENT_GRADIENT);
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

// The comparison samplers of a key's shader, which only the shader-sampling
// shaders of the wide 2D layout have, at most as many as its 2D textures.
CKDWORD Comparisons(const CKDWORD *switches, CKFFSamplerLayout layout) {
    if (layout != CKFF_SAMPLER_LAYOUT_WIDE_2D || (switches[0] & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) == 0)
        return 0;
    const CKDWORD count = (switches[0] & CKFF_NATIVE_FRAGMENT_COMPARISONS) >> CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT;
    const CKDWORD textures = CKFFSamplerTypeSlotCount(CKFF_SAMPLER_2D, layout);
    return count < textures ? count : textures;
}

// The wide 2D layout keeps a 2D texture per stage, except in its comparison
// shaders; everything else is addressed by the stage's ordinal among the
// textures of its type.
bool StageIndexed(CKDWORD samplerType, CKFFSamplerLayout layout, CKDWORD comparisons) {
    return (samplerType == CKFF_SAMPLER_2D || samplerType == CKFF_SAMPLER_DEPTH) &&
           layout == CKFF_SAMPLER_LAYOUT_WIDE_2D && comparisons == 0;
}

CKDWORD SamplerIndex(const CKFFFragmentProgram &program, CKDWORD stage, CKFFSamplerLayout layout,
                     CKDWORD comparisons) {
    const CKDWORD type = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    return StageIndexed(type, layout, comparisons) ? stage : program.GetSamplerOrdinal(stage);
}

// Whether the layout declares the texture a stage samples. The comparison
// samplers hold the first 2D textures, the depth textures compared.
bool SamplerDeclared(const CKFFFragmentProgram &program, CKDWORD stage, CKFFSamplerLayout layout,
                     CKDWORD comparisons) {
    const CKDWORD type = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    const CKDWORD index = SamplerIndex(program, stage, layout, comparisons);
    if (comparisons == 0 || (type != CKFF_SAMPLER_2D && type != CKFF_SAMPLER_DEPTH))
        return index < CKFFSamplerTypeSlotCount(type, layout);
    const bool compared = type == CKFF_SAMPLER_DEPTH &&
                          program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC) != 0;
    return compared ? index < comparisons : index >= comparisons && index < CKFFSamplerTypeSlotCount(type, layout);
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

// A 2D or volume texture the shader filters itself: its slot, its sampler's
// metadata and the stage's sampler state.
struct ShaderSampler {
    uint32_t Slot;
    CKJitSamplerDim Dim;
    // Whether levels blend the filtered sample of the level with the border
    // colour by the texels' coverage, instead of filtering texels: those of
    // anisotropic volumes, and of the comparison shaders' 2D textures.
    bool BlendsBorder;
    CKJitValue Border;        // FLOAT4
    CKJitValue Info;          // FLOAT4: address modes, min, mag and mip filters
    CKJitValue Modes;         // INT2 or INT3: an address mode an axis
    CKJitValue MipFilter;     // INT
    CKJitValue Levels;        // INT
    CKJitValue MinMip;        // FLOAT: the lowest mip level sampled
    CKJitValue MaxAnisotropy; // FLOAT
};

// A depth texture a shader-sampling shader of a layout without stage-indexed
// textures compares texel by texel (depth_compare_sampling.hlsli). Its
// metadata rows are its ordinal's, and a texture the layout does not declare
// is a single texel of the border depth.
struct DepthComparison {
    uint32_t Slot;
    CKJitSamplerDim Dim; // 2D_COMPARE for the comparison samplers
    bool Declared;
    CKJitValue Border;    // FLOAT: the border depth
    CKJitValue Modes;     // INT2: an address mode an axis
    CKJitValue MipFilter; // INT
    CKJitValue Levels;    // INT
    CKJitValue Reference; // FLOAT
    CKDWORD Func;
};

// The taps a 2D filter averages (PlanTaps2D): a single one at the coordinate,
// or those an anisotropic filter spreads along the longer gradient.
struct FilterTaps {
    CKJitValue Count;    // INT
    CKJitValue Total;    // FLOAT: the count
    CKJitValue Step;     // FLOAT2: from a tap to the next
    CKJitValue Center;   // FLOAT: the index of the middle of the taps
    CKJitValue Lod;      // FLOAT: of every tap
    CKJitValue Filtered; // BOOL: whether the taps filter texels
};

// The mips a mip filter blends for a level (PlanMips).
struct MipBlend {
    CKJitValue Level;   // FLOAT: clamped to the mips
    CKJitValue Linear;  // BOOL: whether the two around the level blend
    CKJitValue Count;   // INT: the mips blended
    CKJitValue Lower;   // INT
    CKJitValue Upper;   // INT
    CKJitValue Nearest; // INT
    CKJitValue Weight;  // FLOAT: the upper mip's
};

// The four texels a bilinear filter blends (PlanBilinear).
struct BilinearTexels {
    CKJitValue Index;   // INT4: columns x and x + 1 in x and z, rows y and y + 1 in y and w
    CKJitValue Outside; // BOOL4: the same
    CKJitValue Weight;  // FLOAT2: of column x + 1 and row y + 1
};

class NativeFragmentCompiler {
public:
    NativeFragmentCompiler(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout, const UniformRows &rows)
        : m_B(rows.Counts, rows.BufferCount), m_Program(key.Program), m_Switches(key.Switches), m_Layout(layout),
          m_Comparisons(Comparisons(key.Switches, layout)), m_Rows(rows) {}

    bool Compile(CKJitFragmentShader &out);

private:
    CKJitValue Uniform(const UniformBlock &block, uint32_t row) { return m_B.Uniform(block.Buffer, block.Row + row); }
    CKJitValue DrawParam(CKFFDrawParamSlot slot) { return Uniform(m_Rows.DrawParams, slot); }
    CKJitValue BumpEnv(CKDWORD stage, BumpEnvRow row) {
        return Uniform(m_Rows.BumpEnv, stage * BUMP_ENV_ROWS_PER_STAGE + row);
    }
    CKJitValue StageParam(CKDWORD stage, CKFFStageParamSlot slot) {
        return Uniform(m_Rows.StageParams, CKFFStageParamIndex(stage, slot));
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
    CKJitValue ShaderSample3D(CKDWORD stage, uint32_t slot, CKJitValue coordinate, CKJitValue lodBias);
    CKJitValue SampleCube(uint32_t slot, CKJitValue direction, CKJitValue lodBias);
    ShaderSampler Sampler(CKDWORD stage, uint32_t slot, CKJitSamplerDim dim);
    CKJitValue MirrorOnce(CKJitValue coordinate, CKDWORD sampling);
    FilterTaps PlanTaps2D(CKJitValue info, CKJitValue size, CKJitValue dx, CKJitValue dy, CKJitValue implicitLod,
                          CKJitValue lodBias, CKJitValue minMip, CKJitValue maxAnisotropy, bool comparison);
    MipBlend PlanMips(CKJitValue levels, CKJitValue mipFilter, CKJitValue lod);
    // The mip of an iteration over those a mip filter blends.
    CKJitValue MipAt(const MipBlend &mips, CKJitValue first) {
        return m_B.Select(mips.Linear, m_B.Select(first, mips.Lower, mips.Upper), mips.Nearest);
    }
    BilinearTexels PlanBilinear(CKJitValue scaled, CKJitValue extent, CKJitValue modes);
    CKJitValue Filter2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue dx, CKJitValue dy,
                        CKJitValue implicitLod, CKJitValue bias);
    CKJitValue Anisotropic3D(const ShaderSampler &sampler, CKJitValue uvw, CKJitValue dx, CKJitValue dy,
                             CKJitValue lodBias, bool border);
    CKJitValue Filtered(const ShaderSampler &sampler, CKJitValue lod);
    CKJitValue Mips(const ShaderSampler &sampler, CKJitValue uv, CKJitValue lod, CKJitValue filtered);
    CKJitValue Level(const ShaderSampler &sampler, CKJitValue uv, CKJitValue mip, CKJitValue filtered) {
        if (sampler.BlendsBorder)
            return BorderLevel(sampler, uv, mip, filtered);
        if (sampler.Dim == CKJIT_SAMPLER_2D)
            return Level2D(sampler, uv, mip, filtered);
        return Level3D(sampler, uv, mip, filtered);
    }
    CKJitValue Level2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue mip, CKJitValue filtered);
    CKJitValue Level3D(const ShaderSampler &sampler, CKJitValue uvw, CKJitValue mip, CKJitValue filtered);
    CKJitValue BorderLevel(const ShaderSampler &sampler, CKJitValue uv, CKJitValue mip, CKJitValue filtered);
    CKJitValue Texel(const ShaderSampler &sampler, CKJitValue texel, CKJitValue outside, CKJitValue mip);
    void Address(CKJitValue texel, CKJitValue extent, CKJitValue mode, CKJitValue &index, CKJitValue &outside);
    CKJitValue CompareSample2D(CKDWORD stage, CKJitValue coordinate, CKJitValue lodBias, CKDWORD func);
    CKJitValue CompareMips(const DepthComparison &comparison, CKJitValue uv, CKJitValue lod, CKJitValue filtered);
    CKJitValue CompareLevel(const DepthComparison &comparison, CKJitValue uv, CKJitValue mip, CKJitValue filtered);
    CKJitValue CompareTap(const DepthComparison &comparison, CKJitValue texel, CKJitValue outside, CKJitValue mip);
    CKJitValue CompareExtent(const DepthComparison &comparison, CKJitValue mip) {
        return comparison.Declared ? m_B.TextureSize(comparison.Slot, comparison.Dim, mip) : m_B.Splat(m_B.Int(1), 2);
    }
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
    CKDWORD m_Comparisons;
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
    m_FragCoord = m_B.Input({0, 4, CKJIT_INPUT_FRAG_COORD});
    for (uint32_t v = 0; v < VARYING_COUNT; ++v) {
        const bool flat = v == VARYING_FLAT_COLOR0 || v == VARYING_FLAT_COLOR1;
        const uint8_t components = v == VARYING_LINE_OFFSET ? 2 : 4;
        m_Varyings[v] = m_B.Input({v, components, flat ? CKJIT_INPUT_FLAT : CKJIT_INPUT_SMOOTH});
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
    const CKDWORD type = StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    const bool shaderSampling = Switch(CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING);
    const CKDWORD sampling = Sampling(m_Switches, stage);
    const CKDWORD func = shaderSampling && type == CKFF_SAMPLER_DEPTH
        ? CanonicalCompareFunc(StageField(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC), m_Layout,
                               m_Comparisons, sampling)
        : 0;
    const CKJitValue lodBias = Switch(CKFF_NATIVE_FRAGMENT_LOD_BIAS, stage)
        ? m_B.Component(BumpEnv(stage, BUMP_ENV_LUMINANCE), 2)
        : m_B.Float(0.0f);
    // Without explicit gradients, the comparison shaders compare in hardware,
    // with the sampler's function, the depths their comparison samplers hold.
    if (func != 0 && m_Comparisons != 0 && (sampling & CKFF_NATIVE_FRAGMENT_GRADIENT) == 0) {
        if (!SamplerDeclared(m_Program, stage, m_Layout, m_Comparisons))
            return m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f);
        const CKJitValue coordinate = SampleCoordinate(stage, 3);
        const CKJitValue uv = MirrorOnce(m_B.Swizzle(coordinate, "xy"), sampling);
        const uint32_t slot = CKFFSamplerSlot(CKFF_SAMPLER_DEPTH, m_Program.GetSamplerOrdinal(stage), m_Layout);
        return m_B.Splat(m_B.SampleCmp(slot, uv, m_B.Component(coordinate, 2)), 4);
    }
    // The layouts without stage-indexed textures, and the comparison shaders
    // with explicit gradients, compare the texels of depth textures, and the
    // border depth of those they do not declare.
    if (func != 0 && !StageIndexed(type, m_Layout, m_Comparisons))
        return m_B.Splat(CompareSample2D(stage, SampleCoordinate(stage, 3), lodBias, func), 4);
    // An index past the textures the layout declares samples nothing.
    if (!SamplerDeclared(m_Program, stage, m_Layout, m_Comparisons))
        return m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f);

    const CKJitSamplerDim dim = type == CKFF_SAMPLER_CUBE ? CKJIT_SAMPLER_CUBE
                              : type == CKFF_SAMPLER_VOLUME ? CKJIT_SAMPLER_3D
                                                            : CKJIT_SAMPLER_2D;
    const uint32_t slot =
        CKFFSamplerSlot(type, SamplerIndex(m_Program, stage, m_Layout, m_Comparisons), m_Layout);
    if (dim == CKJIT_SAMPLER_CUBE)
        return SampleCube(slot, SampleCoordinate(stage, 3), lodBias);
    if (!shaderSampling) {
        const CKJitValue color = m_B.Sample(slot, dim, SampleCoordinate(stage, dim == CKJIT_SAMPLER_2D ? 2 : 3), lodBias);
        return type == CKFF_SAMPLER_DEPTH ? m_B.Swizzle(color, "xxxx") : color;
    }

    // The shader-sampling shaders sample 2D, depth and volume textures
    // themselves; the wide 2D layout compares the depths sampled with the
    // coordinate's z.
    if (dim == CKJIT_SAMPLER_3D)
        return ShaderSample3D(stage, slot, SampleCoordinate(stage, 3), lodBias);
    const CKJitValue coordinate = SampleCoordinate(stage, type == CKFF_SAMPLER_DEPTH ? 3 : 2);
    const CKJitValue color = ShaderSample2D(stage, slot, m_B.Swizzle(coordinate, "xy"), lodBias);
    if (type != CKFF_SAMPLER_DEPTH)
        return color;
    return func != 0 ? m_B.Splat(CompareDepth(m_B.Component(color, 0), m_B.Component(coordinate, 2), func), 4)
                     : m_B.Swizzle(color, "xxxx");
}

// Single-level cube filtering with equal isotropic min/mag filters, matching
// native_cube_sampling.hlsli. Other footprints keep the native sampling path.
CKJitValue NativeFragmentCompiler::SampleCube(uint32_t slot, CKJitValue direction, CKJitValue lodBias) {
    const CKJitValue info = Uniform(m_Rows.SamplerInfo, slot);
    const CKJitValue nativeFilter = m_B.Or(
        m_B.Equal(m_B.Component(info, 1), m_B.Float(float(VXTEXTUREFILTER_ANISOTROPIC))),
        m_B.NotEqual(m_B.Component(info, 1), m_B.Component(info, 2)));
    const CKJitValue native = m_B.Or(nativeFilter,
        m_B.IntNotEqual(m_B.TextureLevels(slot, CKJIT_SAMPLER_CUBE), m_B.Int(1)));
    m_B.If(native);
    const CKJitValue ordinary = m_B.Sample(slot, CKJIT_SAMPLER_CUBE, direction, lodBias);
    m_B.Else({ordinary});
    const CKJitValue axes = m_B.Abs(direction);
    const CKJitValue ax = m_B.Component(axes, 0), ay = m_B.Component(axes, 1), az = m_B.Component(axes, 2);
    const CKJitValue x = m_B.And(m_B.Greater(ax, ay), m_B.Greater(ax, az));
    const CKJitValue y = m_B.And(m_B.Not(x), m_B.Greater(ay, az));
    const CKJitValue major = m_B.Max(m_B.Max(ax, ay), az);
    const CKJitValue size = m_B.IntToFloat(m_B.Component(m_B.TextureSize(slot, CKJIT_SAMPLER_CUBE, m_B.Int(0)), 0));
    const CKJitValue limit = m_B.Mul(major, m_B.Sub(m_B.Float(1.0f), m_B.Div(m_B.Float(1.0f), size)));
    const CKJitValue inside = m_B.Min(m_B.Max(direction, m_B.Neg(limit)), limit);
    const CKJitValue at = m_B.Construct({
        m_B.Select(x, m_B.Component(direction, 0), m_B.Component(inside, 0)),
        m_B.Select(y, m_B.Component(direction, 1), m_B.Component(inside, 1)),
        m_B.Select(m_B.And(m_B.Not(x), m_B.Not(y)), m_B.Component(direction, 2), m_B.Component(inside, 2))});
    return m_B.EndIf(m_B.Sample(slot, CKJIT_SAMPLER_CUBE, at, lodBias));
}

// Samples a 2D texture as native_sampling.hlsli: mirror-once folds the
// coordinate, explicit gradients of the unfolded one, scaled by the LOD bias,
// stand for the implicit ones, and border addressing filters texels loaded
// by the shader. The comparison shaders, as ckCompareVariantSample2D*, take
// the level of the folded coordinate's derivatives instead of the implicit
// one, and blend the border colour into hardware samples of the mips.
CKJitValue NativeFragmentCompiler::ShaderSample2D(CKDWORD stage, uint32_t slot, CKJitValue coordinate,
                                                  CKJitValue lodBias) {
    const CKDWORD sampling = Sampling(m_Switches, stage);
    const CKJitValue uv = MirrorOnce(coordinate, sampling);
    const bool border = (sampling & CKFF_NATIVE_FRAGMENT_BORDER) != 0;
    // Levels and derivatives are taken ahead of the filter's regions, which
    // not every pixel of a quad may run.
    CKJitValue implicitLod, dx, dy, bias;
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
        bias = m_B.Float(0.0f);
    } else {
        if (!border)
            return m_B.Sample(slot, CKJIT_SAMPLER_2D, uv, lodBias);
        if (m_Comparisons == 0)
            implicitLod = m_B.CalcLod(slot, CKJIT_SAMPLER_2D, uv);
        dx = m_B.Ddx(uv);
        dy = m_B.Ddy(uv);
        bias = lodBias;
    }

    ShaderSampler sampler = Sampler(stage, slot, CKJIT_SAMPLER_2D);
    sampler.BlendsBorder = m_Comparisons != 0;
    return Filter2D(sampler, uv, dx, dy, implicitLod, bias);
}

// Samples a volume texture as native_sampling.hlsli and ff_sampler_common.sc:
// mirror-once folds the coordinate, and an anisotropic sampler averages taps
// along the longer explicit gradient of the unfolded one. The others sample
// at the level of the unfolded coordinate, no lower than the minimum mip
// level, border addressing filtering texels loaded by the shader.
CKJitValue NativeFragmentCompiler::ShaderSample3D(CKDWORD stage, uint32_t slot, CKJitValue coordinate,
                                                  CKJitValue lodBias) {
    const CKDWORD sampling = Sampling(m_Switches, stage);
    const CKJitValue uvw = MirrorOnce(coordinate, sampling);
    const bool border = (sampling & CKFF_NATIVE_FRAGMENT_BORDER) != 0;
    if ((sampling & CKFF_NATIVE_FRAGMENT_ANISOTROPY) != 0) {
        // Derivatives are taken ahead of the taps, which not every pixel of a
        // quad may run.
        CKJitValue dx = m_B.Float3(0.0f, 0.0f, 0.0f);
        CKJitValue dy = dx;
        if ((sampling & CKFF_NATIVE_FRAGMENT_GRADIENT) != 0) {
            dx = m_B.Ddx(coordinate);
            dy = m_B.Ddy(coordinate);
        }
        ShaderSampler sampler = Sampler(stage, slot, CKJIT_SAMPLER_3D);
        sampler.BlendsBorder = true;
        return Anisotropic3D(sampler, uvw, dx, dy, lodBias, border);
    }
    if ((sampling & (CKFF_NATIVE_FRAGMENT_MIRROR_U | CKFF_NATIVE_FRAGMENT_MIRROR_V | CKFF_NATIVE_FRAGMENT_MIRROR_W |
                     CKFF_NATIVE_FRAGMENT_BORDER | CKFF_NATIVE_FRAGMENT_MIN_MIP)) == 0) {
        return m_B.Sample(slot, CKJIT_SAMPLER_3D, uvw, lodBias);
    }
    const ShaderSampler sampler = Sampler(stage, slot, CKJIT_SAMPLER_3D);
    const CKJitValue lod = m_B.Max(m_B.Add(m_B.CalcLod(slot, CKJIT_SAMPLER_3D, coordinate), lodBias), sampler.MinMip);
    if (!border)
        return m_B.SampleLevel(slot, CKJIT_SAMPLER_3D, uvw, lod);
    return Mips(sampler, uvw, lod, Filtered(sampler, lod));
}

// The metadata of the sampler of a slot and the sampler state of a stage
// sampling it.
ShaderSampler NativeFragmentCompiler::Sampler(CKDWORD stage, uint32_t slot, CKJitSamplerDim dim) {
    const CKJitValue state = m_B.FloatToInt(m_B.Component(BumpEnv(stage, BUMP_ENV_LUMINANCE), 3));
    const CKJitValue info = Uniform(m_Rows.SamplerInfo, slot);
    const CKJitValue modes = m_B.FloatToInt(m_B.Component(info, 0));
    ShaderSampler sampler;
    sampler.Slot = slot;
    sampler.Dim = dim;
    sampler.BlendsBorder = false;
    sampler.Border = Uniform(m_Rows.BorderColor, slot);
    sampler.Info = info;
    sampler.Modes = dim == CKJIT_SAMPLER_3D
        ? m_B.Construct({Bits(modes, 0, 15), Bits(modes, 4, 15), Bits(modes, 8, 15)})
        : m_B.Construct({Bits(modes, 0, 15), Bits(modes, 4, 15)});
    sampler.MipFilter = Bits(m_B.FloatToInt(m_B.Component(info, 3)), 0, 15);
    sampler.Levels = m_B.TextureLevels(slot, dim);
    sampler.MinMip =
        m_B.IntToFloat(Bits(state, CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT, CKFF_SAMPLER_SHADER_MIN_MIP_MASK));
    sampler.MaxAnisotropy =
        m_B.IntToFloat(Bits(state, CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT, CKFF_SAMPLER_SHADER_ANISOTROPY_MASK));
    return sampler;
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

// Plans the taps of a 2D filter at the level of the gradients, or of the
// implicit ones, with the LOD bias, no lower than the minimum mip level. An
// anisotropic filter minifying averages taps along the longer gradient at the
// level of the shorter, as many as fit, up to the maximum anisotropy, which a
// comparison needs above one; the others take a single tap at the coordinate.
FilterTaps NativeFragmentCompiler::PlanTaps2D(CKJitValue info, CKJitValue size, CKJitValue dx, CKJitValue dy,
                                              CKJitValue implicitLod, CKJitValue lodBias, CKJitValue minMip,
                                              CKJitValue maxAnisotropy, bool comparison) {
    const CKJitValue lengthX = m_B.Length(m_B.Mul(dx, size));
    const CKJitValue lengthY = m_B.Length(m_B.Mul(dy, size));
    const CKJitValue major = m_B.Max(lengthX, lengthY);
    const CKJitValue level = implicitLod.IsValid() ? implicitLod : m_B.Log2(m_B.Max(major, m_B.Float(0.000001f)));
    const CKJitValue lod = m_B.Max(m_B.Add(level, lodBias), minMip);

    const CKJitValue minifies = m_B.Greater(lod, m_B.Float(0.0f));
    const CKJitValue filter = m_B.FloatToInt(m_B.Select(minifies, m_B.Component(info, 1), m_B.Component(info, 2)));
    CKJitValue anisotropic = m_B.And(m_B.IntEqual(filter, m_B.Int(VXTEXTUREFILTER_ANISOTROPIC)), minifies);
    const CKJitValue tapLimit = m_B.Max(maxAnisotropy, m_B.Float(1.0f));
    if (comparison)
        anisotropic = m_B.And(anisotropic, m_B.Greater(tapLimit, m_B.Float(1.0f)));
    const CKJitValue minor = m_B.Max(m_B.Min(lengthX, lengthY), m_B.Div(major, tapLimit));
    const CKJitValue ratio = m_B.Ceil(m_B.Div(major, m_B.Max(minor, m_B.Float(1.0f))));
    FilterTaps taps;
    taps.Count =
        m_B.Select(anisotropic, m_B.FloatToInt(m_B.Min(m_B.Max(ratio, m_B.Float(1.0f)), tapLimit)), m_B.Int(1));
    taps.Total = m_B.IntToFloat(taps.Count);
    const CKJitValue longer = m_B.Select(m_B.Greater(lengthX, lengthY), dx, dy);
    taps.Step = m_B.Select(anisotropic, m_B.Div(longer, taps.Total), m_B.Float2(0.0f, 0.0f));
    taps.Lod = m_B.Select(anisotropic,
                          m_B.Max(m_B.Add(m_B.Log2(m_B.Max(minor, m_B.Float(1.0f))), lodBias), minMip), lod);
    taps.Filtered = m_B.IntNotEqual(filter, m_B.Int(VXTEXTUREFILTER_NEAREST));
    taps.Center = m_B.Mul(m_B.IntToFloat(m_B.IntSub(taps.Count, m_B.Int(1))), m_B.Float(0.5f));
    return taps;
}

// Averages the taps of a 2D filter, biasing their level by what of the LOD
// bias the gradients do not carry.
CKJitValue NativeFragmentCompiler::Filter2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue dx, CKJitValue dy,
                                            CKJitValue implicitLod, CKJitValue bias) {
    const CKJitValue size = m_B.IntToFloat(m_B.TextureSize(sampler.Slot, CKJIT_SAMPLER_2D, m_B.Int(0)));
    const FilterTaps taps =
        PlanTaps2D(sampler.Info, size, dx, dy, implicitLod, bias, sampler.MinMip, sampler.MaxAnisotropy, false);
    CKJitValue sum;
    const CKJitValue tap =
        m_B.Loop(taps.Count, CKFF_SAMPLER_SHADER_ANISOTROPY_MASK, {m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f)}, &sum);
    const CKJitValue offset = m_B.Mul(m_B.Sub(m_B.IntToFloat(tap), taps.Center), taps.Step);
    const CKJitValue color = Mips(sampler, m_B.Add(uv, offset), taps.Lod, taps.Filtered);
    return m_B.Div(m_B.EndLoop(m_B.Add(sum, color)), taps.Total);
}

// The volume filter of ff_sampler_common.sc: taps along the longer gradient
// across its footprint, as many as its ratio to the shorter one up to the
// maximum anisotropy, at the level of the longer one's share or the shorter
// one, with the LOD bias, no lower than the minimum mip level. A border
// addressed texture blends the border colour into every level it samples.
CKJitValue NativeFragmentCompiler::Anisotropic3D(const ShaderSampler &sampler, CKJitValue uvw, CKJitValue dx,
                                                 CKJitValue dy, CKJitValue lodBias, bool border) {
    const CKJitValue size = m_B.IntToFloat(m_B.TextureSize(sampler.Slot, CKJIT_SAMPLER_3D, m_B.Int(0)));
    const CKJitValue footprintX = m_B.Length(m_B.Mul(dx, size));
    const CKJitValue footprintY = m_B.Length(m_B.Mul(dy, size));
    const CKJitValue major = m_B.Max(footprintX, footprintY);
    const CKJitValue minor = m_B.Min(footprintX, footprintY);
    const CKJitValue ratio = m_B.Max(m_B.Float(1.0f), m_B.Ceil(m_B.Div(major, m_B.Max(minor, m_B.Float(0.0001f)))));
    const CKJitValue taps = m_B.Select(m_B.LessEqual(major, m_B.Float(1.0f)), m_B.Float(1.0f),
                                       m_B.Min(sampler.MaxAnisotropy, ratio));
    const CKJitValue share = m_B.Max(m_B.Div(major, taps), minor);
    const CKJitValue lod =
        m_B.Max(m_B.Add(m_B.Log2(m_B.Max(share, m_B.Float(0.000001f))), lodBias), sampler.MinMip);
    const CKJitValue step = m_B.Select(m_B.GreaterEqual(footprintX, footprintY), dx, dy);

    CKJitValue sum;
    const CKJitValue tap =
        m_B.Loop(m_B.FloatToInt(taps), CKFF_SAMPLER_SHADER_ANISOTROPY_MASK, {m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f)}, &sum);
    const CKJitValue offset = m_B.Sub(m_B.Div(m_B.Add(m_B.IntToFloat(tap), m_B.Float(0.5f)), taps), m_B.Float(0.5f));
    const CKJitValue at = m_B.Add(uvw, m_B.Mul(step, offset));
    const CKJitValue color =
        border ? Mips(sampler, at, lod, CKJitValue()) : m_B.SampleLevel(sampler.Slot, CKJIT_SAMPLER_3D, at, lod);
    return m_B.Div(m_B.EndLoop(m_B.Add(sum, color)), taps);
}

// Whether the filter of a level, the minification filter above the base mip
// and the magnification one at it, blends texels.
CKJitValue NativeFragmentCompiler::Filtered(const ShaderSampler &sampler, CKJitValue lod) {
    const CKJitValue filter = m_B.Select(m_B.Greater(lod, m_B.Float(0.0f)), m_B.Component(sampler.Info, 1),
                                         m_B.Component(sampler.Info, 2));
    return m_B.NotEqual(filter, m_B.Float((float)VXTEXTUREFILTER_NEAREST));
}

// The mip the mip filter picks for a level, or the two around it for linear
// and anisotropic mip filters; without a mip filter, the base mip.
MipBlend NativeFragmentCompiler::PlanMips(CKJitValue levels, CKJitValue mipFilter, CKJitValue lod) {
    const CKJitValue top = m_B.IntSub(levels, m_B.Int(1));
    MipBlend mips;
    mips.Level = m_B.Select(m_B.IntEqual(mipFilter, m_B.Int(0)), m_B.Float(0.0f),
                            m_B.Min(m_B.Max(lod, m_B.Float(0.0f)), m_B.IntToFloat(top)));
    mips.Linear = m_B.Or(m_B.IntEqual(mipFilter, m_B.Int(VXTEXTUREFILTER_LINEAR)),
                         m_B.IntEqual(mipFilter, m_B.Int(VXTEXTUREFILTER_ANISOTROPIC)));
    mips.Count = m_B.Select(mips.Linear, m_B.Int(2), m_B.Int(1));
    const CKJitValue below = m_B.Floor(mips.Level);
    mips.Lower = m_B.FloatToInt(below);
    mips.Upper = m_B.IntMin(m_B.IntAdd(mips.Lower, m_B.Int(1)), top);
    mips.Nearest = m_B.FloatToInt(m_B.Floor(m_B.Add(mips.Level, m_B.Float(0.5f))));
    mips.Weight = m_B.Sub(mips.Level, below);
    return mips;
}

// The texel of the mips the mip filter blends for a level. Without a
// filtering given, the filter of the level decides it.
CKJitValue NativeFragmentCompiler::Mips(const ShaderSampler &sampler, CKJitValue uv, CKJitValue lod,
                                        CKJitValue filtered) {
    const MipBlend mips = PlanMips(sampler.Levels, sampler.MipFilter, lod);
    if (!filtered.IsValid())
        filtered = Filtered(sampler, mips.Level);

    // One iteration a mip; the first keeps the lower or only one.
    const CKJitValue zero = m_B.Float4(0.0f, 0.0f, 0.0f, 0.0f);
    CKJitValue texels[2];
    const CKJitValue index = m_B.Loop(mips.Count, 2, {zero, zero}, texels);
    const CKJitValue first = m_B.IntEqual(index, m_B.Int(0));
    const CKJitValue texel = Level(sampler, uv, MipAt(mips, first), filtered);
    m_B.EndLoop({m_B.Select(first, texel, texels[0]), texel}, texels);
    return m_B.Select(mips.Linear, m_B.Lerp(texels[0], texels[1], mips.Weight), texels[0]);
}

// The texels a bilinear filter blends at a coordinate scaled to a mip: columns
// x and x + 1 and rows y and y + 1 from the texel below and left of it,
// addressed together.
BilinearTexels NativeFragmentCompiler::PlanBilinear(CKJitValue scaled, CKJitValue extent, CKJitValue modes) {
    const CKJitValue corner = m_B.Sub(scaled, m_B.Float(0.5f));
    const CKJitValue base = m_B.Floor(corner);
    const CKJitValue texel = m_B.FloatToInt(base);
    BilinearTexels texels;
    Address(m_B.Construct({texel, m_B.IntAdd(texel, m_B.Int(1))}), m_B.Construct({extent, extent}),
            m_B.Construct({modes, modes}), texels.Index, texels.Outside);
    texels.Weight = m_B.Sub(corner, base);
    return texels;
}

// The texel of a mip at a coordinate, or the bilinear blend of the four
// around it.
CKJitValue NativeFragmentCompiler::Level2D(const ShaderSampler &sampler, CKJitValue uv, CKJitValue mip,
                                           CKJitValue filtered) {
    const CKJitValue extent = m_B.TextureSize(sampler.Slot, CKJIT_SAMPLER_2D, mip);
    const CKJitValue scaled = m_B.Mul(uv, m_B.IntToFloat(extent));
    m_B.If(filtered);
    const BilinearTexels texels = PlanBilinear(scaled, extent, sampler.Modes);
    const auto tap = [&](const char *axes) {
        return Texel(sampler, m_B.Swizzle(texels.Index, axes), m_B.Any(m_B.Swizzle(texels.Outside, axes)), mip);
    };
    const CKJitValue t00 = tap("xy");
    const CKJitValue t10 = tap("zy");
    const CKJitValue t01 = tap("xw");
    const CKJitValue t11 = tap("zw");
    const CKJitValue wx = m_B.Component(texels.Weight, 0);
    const CKJitValue bilinear =
        m_B.Lerp(m_B.Lerp(t00, t10, wx), m_B.Lerp(t01, t11, wx), m_B.Component(texels.Weight, 1));
    m_B.Else({bilinear});
    CKJitValue index, outside;
    Address(m_B.FloatToInt(m_B.Floor(scaled)), extent, sampler.Modes, index, outside);
    return m_B.EndIf(Texel(sampler, index, m_B.Any(outside), mip));
}

// The texel of a volume mip at a coordinate, or the trilinear blend of the
// eight around it.
CKJitValue NativeFragmentCompiler::Level3D(const ShaderSampler &sampler, CKJitValue uvw, CKJitValue mip,
                                           CKJitValue filtered) {
    const CKJitValue extent = m_B.TextureSize(sampler.Slot, CKJIT_SAMPLER_3D, mip);
    const CKJitValue scaled = m_B.Mul(uvw, m_B.IntToFloat(extent));
    CKJitValue index, outside;
    m_B.If(filtered);
    // The texels from the one below the coordinate on every axis, x first.
    const CKJitValue corner = m_B.Sub(scaled, m_B.Float(0.5f));
    const CKJitValue base = m_B.Floor(corner);
    const CKJitValue weight = m_B.Sub(corner, base);
    const CKJitValue texel = m_B.FloatToInt(base);
    CKJitValue lowIndex, lowOutside, highIndex, highOutside;
    Address(texel, extent, sampler.Modes, lowIndex, lowOutside);
    Address(m_B.IntAdd(texel, m_B.Int(1)), extent, sampler.Modes, highIndex, highOutside);
    CKJitValue trilinear;
    for (uint32_t corners = 0; corners < 8; ++corners) {
        CKJitValue axes[3], out, product;
        for (uint32_t axis = 0; axis < 3; ++axis) {
            const bool high = ((corners >> axis) & 1) != 0;
            axes[axis] = m_B.Component(high ? highIndex : lowIndex, axis);
            const CKJitValue beyond = m_B.Component(high ? highOutside : lowOutside, axis);
            out = axis == 0 ? beyond : m_B.Or(out, beyond);
        }
        product = Texel(sampler, m_B.Construct({axes[0], axes[1], axes[2]}), out, mip);
        for (uint32_t axis = 0; axis < 3; ++axis) {
            const CKJitValue fraction = m_B.Component(weight, axis);
            product = m_B.Mul(product, ((corners >> axis) & 1) != 0 ? fraction : m_B.Sub(m_B.Float(1.0f), fraction));
        }
        trilinear = corners == 0 ? product : m_B.Add(trilinear, product);
    }
    m_B.Else({trilinear});
    Address(m_B.FloatToInt(m_B.Floor(scaled)), extent, sampler.Modes, index, outside);
    return m_B.EndIf(Texel(sampler, index, m_B.Any(outside), mip));
}

// The filtered sample of a mip, blended with the border colour by the
// coverage of the texels filtered: along a border-addressed axis, the weight
// of those inside the mip. An unfiltered 2D mip is sampled at the centre of
// the texel.
CKJitValue NativeFragmentCompiler::BorderLevel(const ShaderSampler &sampler, CKJitValue uvw, CKJitValue mip,
                                               CKJitValue filtered) {
    const uint32_t axes = sampler.Dim == CKJIT_SAMPLER_3D ? 3 : 2;
    const CKJitValue extent = m_B.TextureSize(sampler.Slot, sampler.Dim, mip);
    const CKJitValue size = m_B.IntToFloat(extent);
    const CKJitValue zero = m_B.Splat(m_B.Float(0.0f), axes);
    const CKJitValue one = m_B.Splat(m_B.Float(1.0f), axes);
    const CKJitValue scaled = m_B.Mul(uvw, size);
    const CKJitValue corner = m_B.Sub(scaled, m_B.Float(0.5f));
    const CKJitValue base = m_B.Floor(corner);
    const CKJitValue fraction = m_B.Sub(corner, base);
    const CKJitValue low = m_B.FloatToInt(base);
    const CKJitValue high = m_B.IntAdd(low, m_B.Int(1));
    const auto inside = [&](CKJitValue texel) {
        return m_B.And(m_B.IntGreaterEqual(texel, m_B.Int(0)), m_B.IntLess(texel, extent));
    };
    const CKJitValue blended = m_B.Add(m_B.Select(inside(low), m_B.Sub(one, fraction), zero),
                                       m_B.Select(inside(high), fraction, zero));
    const CKJitValue unfiltered = m_B.Select(m_B.And(m_B.GreaterEqual(uvw, zero), m_B.Less(uvw, one)), one, zero);
    const CKJitValue covered =
        m_B.Select(m_B.IntEqual(sampler.Modes, m_B.Int(VXTEXTURE_ADDRESSBORDER)),
                   m_B.Select(filtered, blended, unfiltered), one);
    CKJitValue coverage = m_B.Mul(m_B.Component(covered, 0), m_B.Component(covered, 1));
    CKJitValue at = uvw;
    if (axes == 3)
        coverage = m_B.Mul(coverage, m_B.Component(covered, 2));
    else
        at = m_B.Select(filtered, uvw, m_B.Div(m_B.Add(m_B.Floor(scaled), m_B.Float(0.5f)), size));
    const CKJitValue sample = m_B.SampleLevel(sampler.Slot, sampler.Dim, at, m_B.IntToFloat(mip));
    return m_B.Lerp(sampler.Border, sample, coverage);
}

// A texel of a mip, or the border colour outside a border-addressed axis.
CKJitValue NativeFragmentCompiler::Texel(const ShaderSampler &sampler, CKJitValue texel, CKJitValue outside,
                                         CKJitValue mip) {
    const CKJitValue loaded = m_B.Load(sampler.Slot, sampler.Dim, m_B.Construct({texel, mip}));
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

// Compares the depths of a texture of a layout without stage-indexed ones as
// depth_compare_sampling.hlsli: a filter blends the comparisons of the texels
// with the reference, the coordinate's z, at the level of the gradients of the
// unfolded coordinate with the LOD bias.
CKJitValue NativeFragmentCompiler::CompareSample2D(CKDWORD stage, CKJitValue coordinate, CKJitValue lodBias,
                                                   CKDWORD func) {
    // The sampler information packs the maximum anisotropy in eight bits.
    const uint32_t kTapLimit = 255;
    const CKJitValue original = m_B.Swizzle(coordinate, "xy");
    // Derivatives are taken ahead of the taps, which not every pixel of a
    // quad may run.
    const CKJitValue dx = m_B.Ddx(original);
    const CKJitValue dy = m_B.Ddy(original);
    const uint32_t ordinal = m_Program.GetSamplerOrdinal(stage);
    const CKJitValue info = Uniform(m_Rows.SamplerInfo, ordinal);
    const CKJitValue modes = m_B.FloatToInt(m_B.Component(info, 0));
    const CKJitValue packed = m_B.FloatToInt(m_B.Component(info, 3));
    DepthComparison comparison;
    comparison.Declared = SamplerDeclared(m_Program, stage, m_Layout, m_Comparisons);
    comparison.Slot = comparison.Declared ? CKFFSamplerSlot(CKFF_SAMPLER_DEPTH, ordinal, m_Layout) : 0;
    comparison.Dim = m_Comparisons != 0 ? CKJIT_SAMPLER_2D_COMPARE : CKJIT_SAMPLER_2D;
    comparison.Border = m_B.Component(Uniform(m_Rows.BorderColor, ordinal), 0);
    comparison.Modes = m_B.Construct({Bits(modes, 0, 15), Bits(modes, 4, 15)});
    comparison.MipFilter = Bits(packed, 0, 15);
    comparison.Levels = comparison.Declared ? m_B.TextureLevels(comparison.Slot, comparison.Dim) : m_B.Int(1);
    comparison.Reference = m_B.Component(coordinate, 2);
    comparison.Func = func;

    const CKJitValue state = m_B.FloatToInt(m_B.Component(BumpEnv(stage, BUMP_ENV_LUMINANCE), 3));
    const CKJitValue minMip =
        m_B.IntToFloat(Bits(state, CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT, CKFF_SAMPLER_SHADER_MIN_MIP_MASK));
    const CKJitValue size = m_B.IntToFloat(CompareExtent(comparison, m_B.Int(0)));
    const CKJitValue maxAnisotropy = m_B.IntToFloat(m_B.IntShiftRight(packed, m_B.Int(4)));
    const FilterTaps taps = PlanTaps2D(info, size, dx, dy, CKJitValue(), lodBias, minMip, maxAnisotropy, true);
    const CKJitValue uv = MirrorOnce(original, Sampling(m_Switches, stage));

    CKJitValue sum;
    const CKJitValue tap = m_B.Loop(taps.Count, kTapLimit, {m_B.Float(0.0f)}, &sum);
    const CKJitValue offset = m_B.Mul(m_B.Sub(m_B.IntToFloat(tap), taps.Center), taps.Step);
    const CKJitValue compared = CompareMips(comparison, m_B.Add(uv, offset), taps.Lod, taps.Filtered);
    return m_B.Div(m_B.EndLoop(m_B.Add(sum, compared)), taps.Total);
}

// The comparison at the mips the mip filter blends for a level, each weighted
// by its share of the blend.
CKJitValue NativeFragmentCompiler::CompareMips(const DepthComparison &comparison, CKJitValue uv, CKJitValue lod,
                                               CKJitValue filtered) {
    const MipBlend mips = PlanMips(comparison.Levels, comparison.MipFilter, lod);
    const CKJitValue lowerShare = m_B.Sub(m_B.Float(1.0f), mips.Weight);
    CKJitValue sum;
    const CKJitValue index = m_B.Loop(mips.Count, 2, {m_B.Float(0.0f)}, &sum);
    const CKJitValue first = m_B.IntEqual(index, m_B.Int(0));
    const CKJitValue share = m_B.Select(mips.Linear, m_B.Select(first, lowerShare, mips.Weight), m_B.Float(1.0f));
    const CKJitValue compared = CompareLevel(comparison, uv, MipAt(mips, first), filtered);
    return m_B.EndLoop(m_B.Add(sum, m_B.Mul(compared, share)));
}

// The comparison at the texel of a mip at a coordinate, or the bilinear blend
// of those at the four around it, summed row by row.
CKJitValue NativeFragmentCompiler::CompareLevel(const DepthComparison &comparison, CKJitValue uv, CKJitValue mip,
                                                CKJitValue filtered) {
    const CKJitValue extent = CompareExtent(comparison, mip);
    const CKJitValue scaled = m_B.Mul(uv, m_B.IntToFloat(extent));
    m_B.If(filtered);
    const BilinearTexels texels = PlanBilinear(scaled, extent, comparison.Modes);
    const CKJitValue wx = m_B.Component(texels.Weight, 0);
    const CKJitValue wy = m_B.Component(texels.Weight, 1);
    const CKJitValue across[2] = {m_B.Sub(m_B.Float(1.0f), wx), wx};
    const CKJitValue down[2] = {m_B.Sub(m_B.Float(1.0f), wy), wy};
    static const char *const kCorners[4] = {"xy", "zy", "xw", "zw"};
    CKJitValue bilinear = m_B.Float(0.0f);
    for (uint32_t i = 0; i < 4; ++i) {
        const CKJitValue compared = CompareTap(comparison, m_B.Swizzle(texels.Index, kCorners[i]),
                                               m_B.Any(m_B.Swizzle(texels.Outside, kCorners[i])), mip);
        bilinear = m_B.Add(bilinear, m_B.Mul(compared, m_B.Mul(across[i & 1], down[i >> 1])));
    }
    m_B.Else({bilinear});
    CKJitValue index, outside;
    Address(m_B.FloatToInt(m_B.Floor(scaled)), extent, comparison.Modes, index, outside);
    return m_B.EndIf(CompareTap(comparison, index, m_B.Any(outside), mip));
}

// The comparison of a texel with the reference; the texels outside a border
// addressed texture, and those of an undeclared one, have the border depth.
CKJitValue NativeFragmentCompiler::CompareTap(const DepthComparison &comparison, CKJitValue texel,
                                              CKJitValue outside, CKJitValue mip) {
    CKJitValue depth = comparison.Border;
    if (comparison.Declared) {
        const CKJitValue loaded = m_B.Load(comparison.Slot, comparison.Dim, m_B.Construct({texel, mip}));
        depth = m_B.Select(outside, comparison.Border, m_B.Component(loaded, 0));
    }
    return CompareDepth(depth, comparison.Reference, comparison.Func);
}

// A comparison of a reference with a depth, 1 where it passes: functions 1
// to 6 are less to not equal, 7 never passes and 8 always does. The others
// pass the depth through.
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
    case 8: return m_B.Float(1.0f);
    default: return depth;
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
                                                bool shaderSampling, CKDWORD comparisons) {
    CKFFNativeFragmentKey key;
    key.Program = program;
    CKDWORD &switches = key.Switches[0];
    // The comparison shaders sample in the shader too.
    shaderSampling = shaderSampling || comparisons != 0;
    if (shaderSampling)
        switches |= CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;
    switches |= (comparisons << CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT) & CKFF_NATIVE_FRAGMENT_COMPARISONS;
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
    const CKDWORD comparisons = Comparisons(raw.Switches, layout);
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
    bool reads2D = false;
    for (CKDWORD stage = count; stage-- > 0;) {
        const bool bump = colorOps[stage] == CKRST_TOP_BUMPENVMAP || colorOps[stage] == CKRST_TOP_BUMPENVMAPLUMINANCE;
        if ((bump && samples[stage + 1]) ||
            (colorOps[stage] == CKRST_TOP_BUMPENVMAPLUMINANCE && readsTexture[stage + 1])) {
            readsTexture[stage] = true;
        }
        const bool textured = readsTexture[stage] && StageSwitch(raw.Switches, CKFF_NATIVE_FRAGMENT_TEXTURE, stage);
        // Outside the wide 2D layout, a shader-sampling shader compares the
        // texels of depth textures, and an undeclared one's border. So do the
        // wide 2D layout's comparison shaders with explicit gradients; without,
        // they compare in hardware.
        const CKDWORD type = from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
        const CKDWORD rawSampling = Sampling(raw.Switches, stage);
        const CKDWORD compareFunc =
            shaderSampling && type == CKFF_SAMPLER_DEPTH
                ? CanonicalCompareFunc(from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC), layout,
                                       comparisons, rawSampling)
                : 0;
        const bool compared = compareFunc != 0 && !StageIndexed(type, layout, comparisons);
        const bool inHardware = compared && comparisons != 0 && (rawSampling & CKFF_NATIVE_FRAGMENT_GRADIENT) == 0;
        const bool declared = SamplerDeclared(from, stage, layout, comparisons);
        samples[stage] = textured && (declared || (compared && !inHardware));
        anySamples = anySamples || samples[stage];

        const bool texture2D = type == CKFF_SAMPLER_2D || type == CKFF_SAMPLER_DEPTH;
        if (textured) {
            key.Switches[0] |= (CKDWORD)CKFF_NATIVE_FRAGMENT_TEXTURE << stage;
            to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, type);
            // A 2D texture of a comparison shader that samples nothing is an
            // undeclared one at the first ordinal.
            if (!StageIndexed(type, layout, comparisons) && (samples[stage] || comparisons == 0 || !texture2D))
                to.SetSamplerOrdinal(stage, from.GetSamplerOrdinal(stage));
            reads2D = reads2D || texture2D;
        }
        if (samples[stage]) {
            to.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED,
                        from.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED));
            if (!inHardware)
                key.Switches[0] |= raw.Switches[0] & (CKDWORD)CKFF_NATIVE_FRAGMENT_LOD_BIAS << stage;
            if (shaderSampling) {
                const CKDWORD sampling = CanonicalSampling(rawSampling, type, compared, declared, comparisons);
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
    // shader, unless its comparison samplers decide the slots and sampling of
    // the 2D and depth textures it reads.
    if (emulates)
        key.Switches[0] |= CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;
    if (comparisons != 0 && reads2D)
        key.Switches[0] |= comparisons << CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT | CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;

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
    NativeFragmentCompiler compiler(key, layout, rows);
    return compiler.Compile(out);
}
