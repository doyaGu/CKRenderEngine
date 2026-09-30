#include "CKFFNativeFragmentJit.h"
#include "CKFFShaderInterface.h"
#include "CKFFStageState.h"
#include "CKFFStateDesc.h"
#include "CKJitDxbc.h"
#include "CKJitSpirv.h"
#include "TestTriangleMultiset.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// The native fixed-function fragment JIT against the shaders it replaces.
// Compiled keys run in an IR interpreter and must reproduce, bit for bit, a
// statement-by-statement transliteration of ckffEvaluate() of
// fs_ff_stage_native, fs_ff_stage_cube_native and fs_ff_stage_volume_native
// on random programs, draw state and fragments, both as the keys of their
// draws and canonical. Both sides run the same float operations in the same
// order, so any difference is a front end bug. With a directory argument the
// SPIR-V and DXBC of some keys are also written there for the validators.

namespace {

const char *g_ShaderDirectory = nullptr;

const CKJitResourceLayout kLayout = {3, 0, 2}; // SDL_gpu fragment resources

// Input registers of the native fragment shaders (struct CKVaryings).
enum Register {
    REG_POSITION,
    REG_COLOR0,
    REG_COLOR1,
    REG_FLAT_COLOR0,
    REG_FLAT_COLOR1,
    REG_TEXCOORD0,
    REG_TEXCOORD7_FOG = REG_TEXCOORD0 + 7,
    REG_FOG_POS,
    REG_LINE_OFFSET,
    REG_COUNT,
};

// Rows of their fragment constant buffer (cbuffer CKFragment).
enum Row {
    ROW_DRAW_PARAMS = 0,
    ROW_BUMP_ENV = 20,
    ROW_STAGE_PARAMS = 36,
    ROW_PROGRAM = 52, // u_ffProgram, then the emulated sampler state
    ROW_COUNT = 89,
};

const CKFFSamplerLayout kLayouts[] = {
    CKFF_SAMPLER_LAYOUT_WIDE_2D,
    CKFF_SAMPLER_LAYOUT_WIDE_CUBE,
    CKFF_SAMPLER_LAYOUT_WIDE_VOLUME,
};
const char *const kShaderNames[] = {"fs_ff_stage_native", "fs_ff_stage_cube_native", "fs_ff_stage_volume_native"};

struct Fragment {
    float Registers[REG_COUNT][4];
    float Uniforms[ROW_COUNT][4];
};

struct Outcome {
    float Color[4];
    bool Discard;
};

class Random {
public:
    explicit Random(uint64_t seed) : m_State(seed) {}

    uint32_t Next() {
        // splitmix64
        uint64_t z = (m_State += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return (uint32_t)((z ^ (z >> 31)) >> 32);
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
    float Unit() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); }
    float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }

private:
    uint64_t m_State;
};

// ---------------------------------------------------------------------------
// Textures: a texel is a hash of the slot, dimension, coordinate and LOD bias,
// so a sample only matches when all of them match.

uint32_t Mix(uint32_t h, uint32_t value) {
    h ^= value;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

uint32_t HashBits(float value) {
    uint32_t bits = 0;
    if (value != 0.0f) // -0 addresses the same texel as +0
        std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void Texel(uint32_t slot, CKJitSamplerDim dim, const float *coordinate, float lodBias, float out[4]) {
    uint32_t h = Mix(0x6a09e667u, slot * 4u + (uint32_t)dim);
    const uint32_t components = dim == CKJIT_SAMPLER_2D ? 2u : 3u;
    for (uint32_t i = 0; i < components; ++i)
        h = Mix(h, HashBits(coordinate[i]));
    h = Mix(h, HashBits(lodBias));
    for (uint32_t i = 0; i < 4; ++i) {
        h = Mix(h, i);
        // Some texels are exact 8-bit values, like most real textures.
        out[i] = (h & 3u) == 0 ? (float)((h >> 8) & 255u) / 255.0f : (float)(h >> 8) * (1.0f / 16777216.0f);
    }
}

// ---------------------------------------------------------------------------
// IR interpreter.

struct Value {
    float F[4];
    int32_t I;
    bool B;
};

Outcome Execute(const CKJitFragmentShader &shader, const Fragment &fragment) {
    XArray<Value> values;
    values.Resize(shader.Nodes.Size());
    for (int n = 0; n < shader.Nodes.Size(); ++n) {
        const CKJitNode &node = shader.Nodes[n];
        Value &out = values[n];
        std::memset(&out, 0, sizeof(out));
        const Value &a = values[node.OperandCount > 0 ? (int)node.Operands[0] : n];
        const Value &b = values[node.OperandCount > 1 ? (int)node.Operands[1] : n];
        const uint32_t width = CKJitComponentCount(node.Type);
        switch (node.Op) {
        case CKJIT_OP_CONSTANT:
            if (node.Type == CKJIT_TYPE_BOOL)
                out.B = node.Imm[0] != 0;
            else if (node.Type == CKJIT_TYPE_INT)
                out.I = (int32_t)node.Imm[0];
            else
                std::memcpy(out.F, node.Imm, width * sizeof(float));
            break;
        case CKJIT_OP_INPUT: {
            const CKJitInput &input = shader.Inputs[(int)node.Imm[0]];
            TestCheck(input.Register < REG_COUNT, "inputs are native shader registers");
            std::memcpy(out.F, fragment.Registers[input.Register], input.Components * sizeof(float));
            break;
        }
        case CKJIT_OP_UNIFORM:
            TestCheck(node.Imm[0] < ROW_COUNT, "uniforms are native constant buffer rows");
            std::memcpy(out.F, fragment.Uniforms[node.Imm[0]], sizeof(out.F));
            break;
        case CKJIT_OP_SWIZZLE:
            for (uint32_t i = 0; i < width; ++i)
                out.F[i] = a.F[node.Imm[i]];
            break;
        case CKJIT_OP_CONSTRUCT: {
            uint32_t component = 0;
            for (uint32_t o = 0; o < node.OperandCount; ++o) {
                const int part = (int)node.Operands[o];
                for (uint32_t i = 0; i < CKJitComponentCount(shader.Nodes[part].Type); ++i)
                    out.F[component++] = values[part].F[i];
            }
            break;
        }
        case CKJIT_OP_ADD: for (uint32_t i = 0; i < width; ++i) out.F[i] = a.F[i] + b.F[i]; break;
        case CKJIT_OP_SUB: for (uint32_t i = 0; i < width; ++i) out.F[i] = a.F[i] - b.F[i]; break;
        case CKJIT_OP_MUL: for (uint32_t i = 0; i < width; ++i) out.F[i] = a.F[i] * b.F[i]; break;
        case CKJIT_OP_DIV: for (uint32_t i = 0; i < width; ++i) out.F[i] = a.F[i] / b.F[i]; break;
        case CKJIT_OP_MIN: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::fmin(a.F[i], b.F[i]); break;
        case CKJIT_OP_MAX: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::fmax(a.F[i], b.F[i]); break;
        case CKJIT_OP_NEG: for (uint32_t i = 0; i < width; ++i) out.F[i] = -a.F[i]; break;
        case CKJIT_OP_ABS: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::fabs(a.F[i]); break;
        case CKJIT_OP_SATURATE:
            for (uint32_t i = 0; i < width; ++i)
                out.F[i] = std::fmin(std::fmax(a.F[i], 0.0f), 1.0f);
            break;
        case CKJIT_OP_FLOOR: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::floor(a.F[i]); break;
        case CKJIT_OP_ROUND_EVEN: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::nearbyint(a.F[i]); break;
        case CKJIT_OP_EXP2: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::exp2(a.F[i]); break;
        case CKJIT_OP_SQRT: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::sqrt(a.F[i]); break;
        case CKJIT_OP_DOT: {
            const uint32_t components = CKJitComponentCount(shader.Nodes[(int)node.Operands[0]].Type);
            float sum = a.F[0] * b.F[0];
            for (uint32_t i = 1; i < components; ++i) {
                const float product = a.F[i] * b.F[i];
                sum = sum + product;
            }
            out.F[0] = sum;
            break;
        }
        case CKJIT_OP_LT: out.B = a.F[0] < b.F[0]; break;
        case CKJIT_OP_LE: out.B = a.F[0] <= b.F[0]; break;
        case CKJIT_OP_EQ: out.B = a.F[0] == b.F[0]; break;
        case CKJIT_OP_NE: out.B = a.F[0] != b.F[0]; break;
        case CKJIT_OP_FTOI: out.I = (int32_t)a.F[0]; break;
        case CKJIT_OP_IEQ: out.B = a.I == b.I; break;
        case CKJIT_OP_IAND: out.I = a.I & b.I; break;
        case CKJIT_OP_ISHR: out.I = a.I >> b.I; break;
        case CKJIT_OP_AND: out.B = a.B && b.B; break;
        case CKJIT_OP_OR: out.B = a.B || b.B; break;
        case CKJIT_OP_NOT: out.B = !a.B; break;
        case CKJIT_OP_SELECT: out = a.B ? b : values[(int)node.Operands[2]]; break;
        case CKJIT_OP_SAMPLE: Texel(node.Imm[0], (CKJitSamplerDim)node.Imm[1], a.F, b.F[0], out.F); break;
        default: TestFail("the interpreter knows every operation");
        }
    }

    Outcome outcome;
    std::memcpy(outcome.Color, values[(int)shader.Color.Id].F, sizeof(outcome.Color));
    outcome.Discard = shader.Discard.IsValid() && values[(int)shader.Discard.Id].B;
    return outcome;
}

// ---------------------------------------------------------------------------
// Reference: ckffEvaluate() of the native fragment shaders, transliterated
// from the HLSL. The program lanes are decoded at run time with the shaders'
// shifts and masks, and every expression keeps the HLSL operation order.

struct Float4 {
    float x, y, z, w;
};

Float4 Splat(float v) { return {v, v, v, v}; }
Float4 operator+(const Float4 &a, const Float4 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
Float4 operator-(const Float4 &a, const Float4 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
Float4 operator*(const Float4 &a, const Float4 &b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
Float4 operator/(const Float4 &a, float b) { return {a.x / b, a.y / b, a.z / b, a.w / b}; }
float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
Float4 Saturate(const Float4 &v) {
    return {Clamp(v.x, 0.0f, 1.0f), Clamp(v.y, 0.0f, 1.0f), Clamp(v.z, 0.0f, 1.0f), Clamp(v.w, 0.0f, 1.0f)};
}
Float4 Lerp(const Float4 &x, const Float4 &y, float s) { return x + Splat(s) * (y - x); }
float Exp(float x) { return std::exp2(x * 1.4426950408889634f); } // HLSL exp() as compiled

struct Reference {
    const Fragment &F;
    CKFFSamplerLayout Layout;

    Float4 Varying(int reg) const {
        const float *v = F.Registers[reg];
        return {v[0], v[1], v[2], v[3]};
    }
    Float4 Uniform(int row) const {
        const float *v = F.Uniforms[row];
        return {v[0], v[1], v[2], v[3]};
    }
    Float4 DrawParams(int i) const { return Uniform(ROW_DRAW_PARAMS + i); }
    Float4 BumpEnv(int i) const { return Uniform(ROW_BUMP_ENV + i); }
    Float4 StageParams(int i) const { return Uniform(ROW_STAGE_PARAMS + i); }

    static int UnpackProgramArg(int arg) { return (arg & 0x7) | ((arg & 0x18) << 1); }

    Float4 SampleSlot(int slot, CKJitSamplerDim dim, const Float4 &coord, float lodBias) const {
        const float c[3] = {coord.x, coord.y, coord.z};
        float texel[4];
        Texel((uint32_t)slot, dim, c, lodBias, texel);
        return {texel[0], texel[1], texel[2], texel[3]};
    }

    // CKFFSampleNative2D / Cube / Volume of each shader: its texture registers,
    // and zero for an ordinal it does not declare.
    Float4 Sample(int samplerType, int stage, int ordinal, const Float4 &coord, float lodBias) const {
        struct Registers {
            int Count2D;
            bool ByStage2D;
            int BaseCube;
            int CountCube;
            int BaseVolume;
            int CountVolume;
        };
        static const Registers table[] = {
            {8, true, 8, 4, 12, 4},  // fs_ff_stage_native
            {4, false, 4, 8, 12, 4}, // fs_ff_stage_cube_native
            {4, false, 4, 4, 8, 8},  // fs_ff_stage_volume_native
        };
        const Registers &r = table[Layout];
        if (samplerType == 1)
            return ordinal < r.CountCube ? SampleSlot(r.BaseCube + ordinal, CKJIT_SAMPLER_CUBE, coord, lodBias)
                                         : Splat(0);
        if (samplerType == 3)
            return ordinal < r.CountVolume ? SampleSlot(r.BaseVolume + ordinal, CKJIT_SAMPLER_3D, coord, lodBias)
                                           : Splat(0);
        const int index = r.ByStage2D ? stage : ordinal;
        return index < r.Count2D ? SampleSlot(index, CKJIT_SAMPLER_2D, coord, lodBias) : Splat(0);
    }

    Float4 SampleTexture(int stage, const Float4 &coord, int samplerType, int samplerOrdinal, bool hasTexture) const {
        if (!hasTexture)
            return {0.0f, 0.0f, 0.0f, 1.0f};
        const float lodBias = BumpEnv(stage * 2 + 1).z;
        if (samplerType == 1 || samplerType == 3)
            return Sample(samplerType, stage, samplerOrdinal, coord, lodBias);
        const Float4 color = Sample(samplerType, stage, samplerOrdinal, coord, lodBias);
        return samplerType == 2 ? Splat(color.x) : color;
    }

    static Float4 GetSampleCoord(Float4 coord, int transformFlags) {
        if ((transformFlags & 0x100) != 0)
            coord = coord / (std::fabs(coord.w) < 0.0001f ? (coord.w < 0.0f ? -0.0001f : 0.0001f) : coord.w);
        return coord;
    }

    static Float4 ApplyArgModifiers(Float4 value, int arg) {
        if ((arg & 0x10) != 0) {
            const Float4 rgb = Splat(1.0f) - value;
            value = {rgb.x, rgb.y, rgb.z, 1.0f - value.w};
        }
        if ((arg & 0x20) != 0)
            value = Splat(value.w);
        return value;
    }

    Float4 GetArg(int arg, const Float4 &textureColor, const Float4 &current, const Float4 &diffuse,
                  const Float4 &specular, const Float4 &temp, const Float4 &stageConstant,
                  bool premodulateCurrent) const {
        const int baseArg = arg & ~(0x10 | 0x20);
        Float4 value = current;
        if (baseArg == 0) value = diffuse;
        else if (baseArg == 1) value = premodulateCurrent ? current * textureColor : current;
        else if (baseArg == 2) value = textureColor;
        else if (baseArg == 3) value = DrawParams(9);
        else if (baseArg == 4) value = specular;
        else if (baseArg == 5) value = temp;
        else if (baseArg == 6) value = stageConstant;
        return ApplyArgModifiers(value, arg);
    }

    Float4 ApplyOp(int op, const Float4 &a, const Float4 &b, const Float4 &c, const Float4 &dst,
                   const Float4 &current, const Float4 &diffuse, const Float4 &textureColor) const {
        const Float4 one = Splat(1.0f);
        if (op == 1) return dst;
        if (op == 2) return a;
        if (op == 3) return b;
        if (op == 4) return a * b;
        if (op == 5) return Saturate(a * b * Splat(2.0f));
        if (op == 6) return Saturate(a * b * Splat(4.0f));
        if (op == 7) return Saturate(a + b);
        if (op == 8) return Saturate(a + b - Splat(0.5f));
        if (op == 9) return Saturate((a + b - Splat(0.5f)) * Splat(2.0f));
        if (op == 10) return Saturate(a - b);
        if (op == 11) return Saturate(a + b - a * b);
        if (op == 12) return Lerp(b, a, diffuse.w);
        if (op == 13) return Lerp(b, a, textureColor.w);
        if (op == 14) return Lerp(b, a, DrawParams(9).w);
        if (op == 15) return Saturate(a + b * Splat(1.0f - textureColor.w));
        if (op == 16) return Lerp(b, a, current.w);
        if (op == 17) return a;
        if (op == 18) return Saturate(a + Splat(a.w) * b);
        if (op == 19) return Saturate(a * b + Splat(a.w));
        if (op == 20) return Saturate(a + Splat(1.0f - a.w) * b);
        if (op == 21) return Saturate((one - a) * b + Splat(a.w));
        if (op == 22 || op == 23) return dst;
        if (op == 24) {
            const Float4 x = a - Splat(0.5f);
            const Float4 y = b - Splat(0.5f);
            return Splat(Clamp((x.x * y.x + x.y * y.y + x.z * y.z) * 4.0f, 0.0f, 1.0f));
        }
        if (op == 25) return Saturate(a * b + c);
        if (op == 26) return Saturate(c * a + (one - c) * b);
        return current;
    }

    static Float4 BlendFactor(int factor, const Float4 &source, const Float4 &destination) {
        const Float4 one = Splat(1.0f);
        if (factor == 1) return Splat(0.0f);
        if (factor == 2) return one;
        if (factor == 3) return source;
        if (factor == 4) return one - source;
        if (factor == 5 || factor == 12) return Splat(source.w);
        if (factor == 6 || factor == 13) return one - Splat(source.w);
        if (factor == 7) return Splat(destination.w);
        if (factor == 8) return one - Splat(destination.w);
        if (factor == 9) return destination;
        if (factor == 10) return one - destination;
        if (factor == 11) {
            const float saturated = std::fmin(source.w, 1.0f - destination.w);
            return {saturated, saturated, saturated, 1.0f};
        }
        return Splat(0.0f);
    }

    static Float4 StageBlend(const Float4 &source, const Float4 &destination, int packedFactors) {
        int src = (packedFactors >> 4) & 15;
        int dst = packedFactors & 15;
        if (src == 12) {
            src = 5;
            dst = 6;
        } else if (src == 13) {
            src = 6;
            dst = 5;
        }
        return Saturate(source * BlendFactor(src, source, destination) +
                        destination * BlendFactor(dst, source, destination));
    }

    bool AlphaPass(float alpha, int func) const {
        float ref = DrawParams(8).x;
        int alphaPrecision = func / 16;
        func = func - alphaPrecision * 16;
        float alphaTestValue = alpha;
        if (alphaPrecision != 15) {
            alphaPrecision = alphaPrecision < 8 ? alphaPrecision : 8;
            const float precisionScale = std::ldexp(1.0f, 8 + alphaPrecision);
            const float factor = precisionScale - 1.0f;
            const float refScale = std::ldexp(1.0f, alphaPrecision);
            const float refWrap = std::ldexp(1.0f, 8 - alphaPrecision);
            alphaTestValue = std::nearbyint(alpha * factor);
            ref = std::floor(ref) * refScale + std::floor(std::floor(ref) / refWrap);
        } else {
            ref = ref / 255.0f;
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

    static float FogFactor(float depth, int mode, const Float4 &params) {
        if (mode == 0) return 1.0f;
        if (mode == 1) {
            const float e = params.z * depth;
            return Clamp(Exp(-e), 0.0f, 1.0f);
        }
        if (mode == 2) {
            const float e = params.z * depth;
            return Clamp(Exp(-(e * e)), 0.0f, 1.0f);
        }
        float denom = params.y - params.x;
        if (std::fabs(denom) < 0.0001f)
            denom = denom < 0.0f ? -0.0001f : 0.0001f;
        return Clamp((params.y - depth) / denom, 0.0f, 1.0f);
    }

    static Float4 DecodeBump(const Float4 &bump, bool unormEncoded) {
        if (!unormEncoded)
            return bump;
        return {Clamp((bump.x * 255.0f - 128.0f) / 127.0f, -1.0f, 1.0f),
                Clamp((bump.y * 255.0f - 128.0f) / 127.0f, -1.0f, 1.0f), 0.0f, 0.0f};
    }

    Outcome Evaluate(const CKDWORD *lanes) const {
        Outcome outcome = {};
        const Float4 fragCoord = Varying(REG_POSITION);
        const Float4 fogPos = Varying(REG_FOG_POS);
        const Float4 texcoord7Fog = Varying(REG_TEXCOORD7_FOG);

        float edgeCoverage = 1.0f;
        if (DrawParams(4).w > 2.5f) {
            const Float4 lineOffset = Varying(REG_LINE_OFFSET);
            const float x = lineOffset.x * fragCoord.w;
            const float y = lineOffset.y * fragCoord.w;
            edgeCoverage = Clamp(1.0f - std::sqrt(x * x + y * y), 0.0f, 1.0f);
            if (edgeCoverage <= 0.0f) {
                outcome.Discard = true;
                return outcome;
            }
        }

        const int globalWord = (int)lanes[16];
        const int lastActiveTextureStage = globalWord & 0x7;
        const bool alphaTestEnabled = ((globalWord >> 3) & 0x1) != 0;
        const int alphaFunc = (globalWord >> 4) & 0xf;
        const bool fogEnabled = ((globalWord >> 8) & 0x1) != 0;
        const int pixelFogMode = (globalWord >> 11) & 0x3;
        const bool flatShade = ((globalWord >> 14) & 0x1) != 0;
        const bool globalSpecularEnabled = ((globalWord >> 15) & 0x1) != 0;
        const int samplerOrdinals = (int)lanes[17];

        const Float4 diffuse = Varying(flatShade ? REG_FLAT_COLOR0 : REG_COLOR0);
        const Float4 specular = Varying(flatShade ? REG_FLAT_COLOR1 : REG_COLOR1);
        Float4 current = diffuse;
        Float4 temp = Splat(0.0f);
        Float4 previousTexture = {0.0f, 0.0f, 0.0f, 1.0f};
        bool previousBumpUnorm = false;
        int previousColorOp = 0;
        int previousAlphaOp = 0;
        for (int stage = 0; stage < 8; ++stage) {
            if (stage > lastActiveTextureStage)
                break;
            const int colorWord = (int)lanes[stage * 2];
            const int alphaWord = (int)lanes[stage * 2 + 1];
            const int colorOp = colorWord & 0x1f;
            const int samplerType = (colorWord >> 21) & 0x3;
            const bool projected = ((colorWord >> 23) & 0x1) != 0;
            const int samplerOrdinal = (samplerOrdinals >> (stage * 3)) & 7;
            const int colorArg0 = UnpackProgramArg((colorWord >> 15) & 0x1f);
            const int colorArg1 = UnpackProgramArg((colorWord >> 5) & 0x1f);
            const int colorArg2 = UnpackProgramArg((colorWord >> 10) & 0x1f);
            const bool resultIsTemp = ((colorWord >> 20) & 0x1) != 0;
            const int alphaOp = alphaWord & 0x1f;
            const int alphaArg0 = UnpackProgramArg((alphaWord >> 15) & 0x1f);
            const int alphaArg1 = UnpackProgramArg((alphaWord >> 5) & 0x1f);
            const int alphaArg2 = UnpackProgramArg((alphaWord >> 10) & 0x1f);

            const Float4 stageConstant = StageParams(stage * 2 + 1);
            const Float4 coordParams = StageParams(stage * 2 + 0);
            const int stageBlend = (int)(coordParams.w + 0.5f);
            int flags = (int)coordParams.y;
            if (projected)
                flags |= 0x100;
            else
                flags &= ~0x100;
            const bool bumpUnorm = (flags & 0x2000) != 0;
            const bool hasTexture = coordParams.z > 0.5f;
            if (colorOp == 1)
                break;

            Float4 stageCoord = Varying(REG_TEXCOORD0 + stage);
            if (DrawParams(4).z > 0.5f) {
                const float affineW =
                    std::fabs(fogPos.x) < 0.000001f ? (fogPos.x < 0.0f ? -0.000001f : 0.000001f) : fogPos.x;
                if (stage == 7) {
                    stageCoord.x /= affineW;
                    stageCoord.y /= affineW;
                    stageCoord.w /= affineW;
                } else {
                    stageCoord = stageCoord / affineW;
                }
            }
            Float4 sampleCoord = GetSampleCoord(stageCoord, flags);
            if (stage != 0 && (previousColorOp == 22 || previousColorOp == 23)) {
                const Float4 bump = DecodeBump(previousTexture, previousBumpUnorm);
                const Float4 matrix = BumpEnv((stage - 1) * 2);
                sampleCoord.x += matrix.x * bump.x + matrix.y * bump.y;
                sampleCoord.y += matrix.z * bump.x + matrix.w * bump.y;
            }
            Float4 texColor = SampleTexture(stage, sampleCoord, samplerType, samplerOrdinal, hasTexture);
            if (stage != 0 && previousColorOp == 23) {
                const Float4 luminance = BumpEnv((stage - 1) * 2 + 1);
                const float lum = Clamp(previousTexture.z * luminance.x + luminance.y, 0.0f, 1.0f);
                texColor = texColor * Splat(lum);
            }

            const bool premodulateColor = previousColorOp == 17 && hasTexture;
            const bool premodulateAlpha = previousAlphaOp == 17 && hasTexture;
            const Float4 colorA =
                GetArg(colorArg1, texColor, current, diffuse, specular, temp, stageConstant, premodulateColor);
            const Float4 colorB =
                GetArg(colorArg2, texColor, current, diffuse, specular, temp, stageConstant, premodulateColor);
            const Float4 colorC =
                GetArg(colorArg0, texColor, current, diffuse, specular, temp, stageConstant, premodulateColor);
            const Float4 alphaA =
                GetArg(alphaArg1, texColor, current, diffuse, specular, temp, stageConstant, premodulateAlpha);
            const Float4 alphaB =
                GetArg(alphaArg2, texColor, current, diffuse, specular, temp, stageConstant, premodulateAlpha);
            const Float4 alphaC =
                GetArg(alphaArg0, texColor, current, diffuse, specular, temp, stageConstant, premodulateAlpha);
            const int resultArg = resultIsTemp ? 5 : 1;
            Float4 stageResult = resultArg == 5 ? temp : current;
            const Float4 colorResult =
                colorOp == 27 ? StageBlend(texColor, current, stageBlend)
                              : ApplyOp(colorOp, colorA, colorB, colorC, stageResult, current, diffuse, texColor);
            const Float4 alphaResult =
                ApplyOp(alphaOp, alphaA, alphaB, alphaC, stageResult, current, diffuse, texColor);
            stageResult = {colorResult.x, colorResult.y, colorResult.z, alphaResult.w};
            if (colorOp == 24)
                stageResult = colorResult;
            if (resultArg == 5)
                temp = stageResult;
            else
                current = stageResult;
            previousTexture = texColor;
            previousBumpUnorm = bumpUnorm;
            previousColorOp = colorOp;
            previousAlphaOp = alphaOp;
        }
        if (globalSpecularEnabled) {
            current.x += specular.x;
            current.y += specular.y;
            current.z += specular.z;
        }
        if (alphaTestEnabled && !AlphaPass(current.w, alphaFunc)) {
            outcome.Discard = true;
            return outcome;
        }
        if (fogEnabled) {
            const float fogFactor = pixelFogMode == 0
                ? texcoord7Fog.z
                : FogFactor(fogPos.z / fogPos.w, pixelFogMode, DrawParams(10));
            const Float4 fogged = Lerp(DrawParams(11), current, fogFactor);
            current = {fogged.x, fogged.y, fogged.z, current.w};
        }
        current.w *= edgeCoverage;
        const Float4 color = Saturate(current);
        outcome.Color[0] = color.x;
        outcome.Color[1] = color.y;
        outcome.Color[2] = color.z;
        outcome.Color[3] = color.w;
        return outcome;
    }
};

// ---------------------------------------------------------------------------
// Random programs and fragments.

CKDWORD RandomColorOp(Random &random) {
    // Operations that carry state into the next stage come up more often.
    static const CKDWORD chained[] = {CKRST_TOP_PREMODULATE, CKRST_TOP_BUMPENVMAP, CKRST_TOP_BUMPENVMAPLUMINANCE,
                                      CKRST_TOP_DOTPRODUCT3, CKFF_TOP_STAGEBLEND};
    switch (random.Below(4)) {
    case 0: return chained[random.Below(sizeof(chained) / sizeof(chained[0]))];
    case 1: return CKRST_TOP_SELECTARG1 + random.Below(CKFF_TOP_STAGEBLEND - CKRST_TOP_SELECTARG1 + 1);
    default: return random.Below(32);
    }
}

CKDWORD RandomAlphaOp(Random &random) {
    static const CKDWORD special[] = {CKRST_TOP_DISABLE, CKRST_TOP_PREMODULATE, CKRST_TOP_DOTPRODUCT3};
    return random.OneIn(4) ? special[random.Below(sizeof(special) / sizeof(special[0]))] : random.Below(32);
}

// Every lane is random 24-bit data outside the fields the program sets.
CKFFFragmentProgram RandomProgram(Random &random, bool allStages) {
    CKDWORD lanes[CKFFFragmentProgram::LaneCount];
    for (CKDWORD i = 0; i < CKFFFragmentProgram::LaneCount; ++i)
        lanes[i] = random.Next() & CKFFFragmentProgram::LaneMask;
    CKFFFragmentProgram program;
    program.SetLanes(lanes, CKFFFragmentProgram::LaneCount);
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        CKDWORD colorOp = RandomColorOp(random);
        if (allStages && colorOp == CKRST_TOP_DISABLE)
            colorOp = CKRST_TOP_MODULATE;
        program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, colorOp);
        program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP, RandomAlphaOp(random));
        program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_RESULT_IS_TEMP, random.OneIn(4) ? 1 : 0);
    }
    if (allStages)
        program.Set(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE, CKFF_FRAGMENT_PROGRAM_STAGE_COUNT - 1);
    return program;
}

float RandomColorComponent(Random &random) {
    switch (random.Below(4)) {
    case 0: return (float)random.Below(256) / 255.0f;
    case 1: return random.OneIn(2) ? 0.0f : 1.0f;
    default: return random.Unit();
    }
}

void RandomColor(Random &random, float *out) {
    for (int i = 0; i < 4; ++i)
        out[i] = RandomColorComponent(random);
}

// Near-zero divisors exercise the shaders' division guards.
float RandomDivisor(Random &random, float epsilon, float lo, float hi) {
    switch (random.Below(6)) {
    case 0: return 0.0f;
    case 1: return random.Range(-epsilon, epsilon);
    case 2: return -random.Range(lo, hi);
    default: return random.Range(lo, hi);
    }
}

void RandomFragment(Random &random, Fragment &fragment) {
    for (int reg = 0; reg < REG_COUNT; ++reg) {
        for (int i = 0; i < 4; ++i)
            fragment.Registers[reg][i] = random.Range(-2.0f, 2.0f);
    }
    for (int row = 0; row < ROW_COUNT; ++row) {
        for (int i = 0; i < 4; ++i)
            fragment.Uniforms[row][i] = random.Range(-1.0f, 1.0f);
    }

    fragment.Registers[REG_POSITION][3] = random.Range(0.2f, 2.0f);
    for (int reg = REG_COLOR0; reg <= REG_FLAT_COLOR1; ++reg)
        RandomColor(random, fragment.Registers[reg]);
    for (int stage = 0; stage < 8; ++stage)
        fragment.Registers[REG_TEXCOORD0 + stage][3] = RandomDivisor(random, 0.0002f, 0.25f, 2.0f);
    fragment.Registers[REG_TEXCOORD7_FOG][2] = random.Unit();
    float *fogPos = fragment.Registers[REG_FOG_POS];
    fogPos[0] = RandomDivisor(random, 0.000002f, 0.5f, 3.0f);
    fogPos[2] = random.Range(0.0f, 10.0f);
    fogPos[3] = random.Range(0.5f, 3.0f);

    float(*drawParams)[4] = fragment.Uniforms + ROW_DRAW_PARAMS;
    drawParams[4][2] = (float)random.Below(2); // affine texture coordinates
    drawParams[4][3] = (float)random.Below(4); // line mode
    drawParams[8][0] = (float)random.Below(256) + (random.OneIn(4) ? random.Unit() : 0.0f); // alpha reference
    RandomColor(random, drawParams[9]);                                                   // texture factor
    const float fogStart = random.Range(0.0f, 10.0f);
    drawParams[10][0] = fogStart;
    const float fogEnd = random.OneIn(4) ? fogStart + random.Range(-0.0002f, 0.0002f) : random.Range(0.0f, 10.0f);
    drawParams[10][1] = fogEnd;
    drawParams[10][2] = random.Range(0.0f, 2.0f); // density
    RandomColor(random, drawParams[11]);          // fog colour
    if (random.OneIn(8)) // a depth at the end of linear fog, where its range guard matters
        fogPos[2] = (fogEnd + random.Range(-0.001f, 0.001f)) * fogPos[3];

    for (int stage = 0; stage < 8; ++stage) {
        float *luminance = fragment.Uniforms[ROW_BUMP_ENV + stage * 2 + 1];
        luminance[0] = random.Range(0.0f, 2.0f);
        luminance[1] = random.Range(-0.5f, 0.5f);
        luminance[2] = random.OneIn(3) ? (random.OneIn(2) ? -0.0f : 0.0f) : random.Range(-2.0f, 2.0f); // LOD bias

        float *coord = fragment.Uniforms[ROW_STAGE_PARAMS + stage * 2];
        coord[1] = (float)random.Below(0x4000);                            // texture transform flags
        coord[2] = random.OneIn(4) ? random.Unit() : 1.0f;                 // texture presence
        coord[3] = (float)random.Below(256) + random.Range(-0.49f, 0.49f); // stage blend factors
        RandomColor(random, fragment.Uniforms[ROW_STAGE_PARAMS + stage * 2 + 1]);
    }
}

// Gives a fragment the draw state of another that keys depend on: the line
// and affine modes, the texture coordinate parameters, the sampler state
// words, and which LOD biases are zero.
void ShareDrawState(const Fragment &from, Fragment &to, Random &random) {
    to.Uniforms[ROW_DRAW_PARAMS + 4][2] = from.Uniforms[ROW_DRAW_PARAMS + 4][2];
    to.Uniforms[ROW_DRAW_PARAMS + 4][3] = from.Uniforms[ROW_DRAW_PARAMS + 4][3];
    for (int stage = 0; stage < 8; ++stage) {
        std::memcpy(to.Uniforms[ROW_STAGE_PARAMS + stage * 2], from.Uniforms[ROW_STAGE_PARAMS + stage * 2],
                    sizeof(to.Uniforms[0]));
        to.Uniforms[ROW_BUMP_ENV + stage * 2 + 1][3] = from.Uniforms[ROW_BUMP_ENV + stage * 2 + 1][3];
        const float fromBias = from.Uniforms[ROW_BUMP_ENV + stage * 2 + 1][2];
        float &bias = to.Uniforms[ROW_BUMP_ENV + stage * 2 + 1][2];
        if (fromBias == 0.0f)
            bias = random.OneIn(2) ? -0.0f : 0.0f;
        else if (bias == 0.0f)
            bias = fromBias;
    }
}

// ---------------------------------------------------------------------------
// Keys.

// The key of a fragment's draw, from the constant blocks the runtime passes.
CKFFNativeFragmentKey DrawKey(const CKFFFragmentProgram &program, const Fragment &fragment,
                              bool shaderSampling = false) {
    CKFFConstantSet constants;
    constants.Set(CKRST_BLOCK_DRAW_PARAMS, fragment.Uniforms[ROW_DRAW_PARAMS], (ROW_BUMP_ENV - ROW_DRAW_PARAMS) * 16);
    constants.Set(CKRST_BLOCK_BUMP_ENV, fragment.Uniforms[ROW_BUMP_ENV], (ROW_STAGE_PARAMS - ROW_BUMP_ENV) * 16);
    constants.Set(CKRST_BLOCK_STAGE_PARAMS, fragment.Uniforms[ROW_STAGE_PARAMS], (ROW_PROGRAM - ROW_STAGE_PARAMS) * 16);
    return CKFFNativeFragmentDrawKey(program, constants, shaderSampling);
}

CKFFNativeFragmentKey Canonical(CKFFNativeFragmentKey key, CKFFSamplerLayout layout) {
    CKFFCanonicalizeNativeFragmentKey(key, layout);
    return key;
}

// The stages a program evaluates, as the shaders decode them.
CKDWORD EvaluatedStages(const CKFFFragmentProgram &program) {
    CKDWORD count = 0;
    while (count < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT &&
           count <= program.Get(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE) &&
           program.GetStage(count, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP) != CKRST_TOP_DISABLE) {
        ++count;
    }
    return count;
}

// The arguments an operation ignores: bit 0 for ARG1, 1 for ARG2, 2 for ARG0.
CKDWORD IgnoredArguments(CKDWORD op) {
    if (op == CKRST_TOP_SELECTARG1 || op == CKRST_TOP_PREMODULATE)
        return 6;
    if (op == CKRST_TOP_SELECTARG2)
        return 5;
    if (op == CKRST_TOP_MULTIPLYADD || op == CKRST_TOP_LERP)
        return 0;
    if ((op >= CKRST_TOP_MODULATE && op <= CKRST_TOP_BLENDCURRENTALPHA) ||
        (op >= CKRST_TOP_MODULATEALPHA_ADDCOLOR && op <= CKRST_TOP_MODULATEINVCOLOR_ADDALPHA) ||
        op == CKRST_TOP_DOTPRODUCT3) {
        return 4;
    }
    return 7;
}

void Scramble(CKFFFragmentProgram &program, CKFFFragmentProgramGlobalField field, Random &random) {
    if (random.OneIn(2))
        program.Set(field, random.Next());
}

void Scramble(CKFFFragmentProgram &program, CKDWORD stage, CKFFFragmentProgramStageField field, Random &random) {
    if (random.OneIn(2))
        program.SetStage(stage, field, random.Next());
}

void Scramble(CKDWORD &switches, CKDWORD bits, Random &random) {
    switches ^= bits & random.Next();
}

// Scrambles what a shader-sampling shader never reads of a textured stage's
// sampling flags and depth comparison.
void ScrambleSampling(CKFFNativeFragmentKey &key, CKDWORD stage, CKFFSamplerLayout layout, Random &random) {
    CKFFFragmentProgram &program = key.Program;
    const CKDWORD type = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    const CKDWORD shift = stage % 4 * 8;
    CKDWORD &word = key.Switches[3 + stage / 4];
    const CKDWORD sampling = word >> shift & 0xFFu;
    const CKDWORD mirror3D = CKFF_NATIVE_FRAGMENT_MIRROR_U | CKFF_NATIVE_FRAGMENT_MIRROR_V |
                             CKFF_NATIVE_FRAGMENT_MIRROR_W;
    CKDWORD ignored = 0x80u;
    if (type == CKFF_SAMPLER_CUBE) {
        ignored = 0xFFu;
    } else if (type == CKFF_SAMPLER_VOLUME) {
        // Hardware sampling is chosen by the minimum mip level only without
        // anisotropy, mirror-once or border addressing.
        if ((sampling & CKFF_NATIVE_FRAGMENT_ANISOTROPY) != 0)
            ignored |= CKFF_NATIVE_FRAGMENT_MIN_MIP;
        else if ((sampling & (mirror3D | CKFF_NATIVE_FRAGMENT_BORDER)) != 0)
            ignored |= CKFF_NATIVE_FRAGMENT_GRADIENT | CKFF_NATIVE_FRAGMENT_MIN_MIP;
        else
            ignored |= CKFF_NATIVE_FRAGMENT_GRADIENT;
    } else {
        ignored |= CKFF_NATIVE_FRAGMENT_MIRROR_W | CKFF_NATIVE_FRAGMENT_ANISOTROPY | CKFF_NATIVE_FRAGMENT_MIN_MIP;
    }

    const CKDWORD func = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC);
    if (type != CKFF_SAMPLER_DEPTH) {
        Scramble(program, stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, random);
    } else if (layout == CKFF_SAMPLER_LAYOUT_WIDE_2D) {
        // Unknown functions pass sampled depths through, like none.
        if ((func == 0 || func > 8) && random.OneIn(2))
            program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC,
                             random.OneIn(2) ? 0 : 9 + random.Below(7));
    } else if (func != 0) {
        // Every texel is compared, and unknown functions pass them all
        // through; an undeclared texture's texels are all the border.
        if (func > 8 && random.OneIn(2))
            program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, 9 + random.Below(7));
        ignored |= CKFF_NATIVE_FRAGMENT_GRADIENT;
        if (program.GetSamplerOrdinal(stage) >= CKFFSamplerTypeSlotCount(CKFF_SAMPLER_DEPTH, layout))
            ignored |= CKFF_NATIVE_FRAGMENT_BORDER;
    }
    Scramble(word, ignored << shift, random);
}

// Scrambles, each with even odds, what the shaders of a layout never read in
// a key: the reserved lanes and the vertex shaders' state, stages past the
// evaluated ones, unread arguments, the state of tests that pass everything,
// switches of stages without textures or with operations that ignore them,
// the sampler state native shaders sample in hardware, and operations the
// combiners evaluate like others.
void Scramble(CKFFNativeFragmentKey &key, CKFFSamplerLayout layout, Random &random) {
    const bool shaderSampling = (key.Switches[0] & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) != 0;
    CKFFFragmentProgram &program = key.Program;
    const CKDWORD count = EvaluatedStages(program);
    const CKDWORD lastStage = program.Get(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE);
    const CKDWORD textures = key.Switches[0] >> 8 & ((1u << count) - 1u);

    CKDWORD lanes[CKFFFragmentProgram::LaneCount];
    std::memcpy(lanes, program.Lanes(), sizeof(lanes));
    for (CKDWORD i = 18; i < CKFFFragmentProgram::LaneCount; ++i)
        lanes[i] = random.OneIn(2) ? random.Next() & CKFFFragmentProgram::LaneMask : lanes[i];
    program.SetLanes(lanes, CKFFFragmentProgram::LaneCount);
    Scramble(program, CKFF_FRAGMENT_PROGRAM_VERTEX_FOG_MODE, random);
    Scramble(program, CKFF_FRAGMENT_PROGRAM_RANGE_FOG, random);
    const CKDWORD alphaFunc = program.Get(CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC);
    if (alphaFunc < VXCMP_NEVER || alphaFunc > VXCMP_GREATEREQUAL)
        Scramble(program, CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED, random);
    if (!program.Get(CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED))
        Scramble(program, CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC, random);
    if (!program.Get(CKFF_FRAGMENT_PROGRAM_FOG_ENABLED))
        Scramble(program, CKFF_FRAGMENT_PROGRAM_PIXEL_FOG_MODE, random);
    if (textures == 0)
        Scramble(key.Switches[0], CKFF_NATIVE_FRAGMENT_AFFINE, random);

    static const CKFFFragmentProgramStageField kFields[] = {
        CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP,       CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1,
        CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG2,     CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG0,
        CKFF_FRAGMENT_PROGRAM_STAGE_RESULT_IS_TEMP, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE,
        CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED,      CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP,
        CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1,     CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG2,
        CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG0,     CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC,
    };
    static const CKFFFragmentProgramStageField kArguments[2][3] = {
        {CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG2,
         CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG0},
        {CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG2,
         CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG0},
    };
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        const CKDWORD perStage = CKFF_NATIVE_FRAGMENT_TEXTURE | CKFF_NATIVE_FRAGMENT_BUMP_UNORM |
                                 CKFF_NATIVE_FRAGMENT_LOD_BIAS;
        const CKDWORD pair = 0xFFu << (stage % 4 * 8);
        if (stage >= count) {
            // The stage ending the evaluated ones stays disabled.
            for (const CKFFFragmentProgramStageField field : kFields) {
                if (field != CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP || stage != count || count > lastStage)
                    Scramble(program, stage, field, random);
            }
            if (random.OneIn(2))
                program.SetSamplerOrdinal(stage, random.Below(8));
            Scramble(key.Switches[0], perStage << stage, random);
            Scramble(key.Switches[1 + stage / 4], pair, random);
            Scramble(key.Switches[3 + stage / 4], pair, random);
            continue;
        }

        if (shaderSampling && (textures & 1u << stage) != 0) {
            ScrambleSampling(key, stage, layout, random);
        } else {
            Scramble(program, stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, random);
            Scramble(key.Switches[3 + stage / 4], pair, random);
        }
        if ((textures & 1u << stage) == 0) {
            Scramble(program, stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, random);
            Scramble(program, stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED, random);
            if (random.OneIn(2))
                program.SetSamplerOrdinal(stage, random.Below(8));
            Scramble(key.Switches[0], CKFF_NATIVE_FRAGMENT_LOD_BIAS << stage, random);
        }
        const CKDWORD colorOp = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP);
        const CKDWORD alphaOp = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP);
        if ((colorOp != CKRST_TOP_BUMPENVMAP && colorOp != CKRST_TOP_BUMPENVMAPLUMINANCE) || stage + 1 == count)
            Scramble(key.Switches[0], CKFF_NATIVE_FRAGMENT_BUMP_UNORM << stage, random);
        if (colorOp != CKFF_TOP_STAGEBLEND)
            Scramble(key.Switches[1 + stage / 4], pair, random);

        // DOTPRODUCT3 overwrites what the alpha combiner computes.
        const CKDWORD ignored[2] = {IgnoredArguments(colorOp),
                                    colorOp == CKRST_TOP_DOTPRODUCT3 ? 7u : IgnoredArguments(alphaOp)};
        for (int channel = 0; channel < 2; ++channel) {
            for (int k = 0; k < 3; ++k) {
                if (ignored[channel] & 1u << k)
                    Scramble(program, stage, kArguments[channel][k], random);
            }
        }

        // Unknown operations keep CURRENT like 0, the bump operations
        // compute no alpha, like DISABLE, and under DOTPRODUCT3 an alpha
        // operation only matters if it premodulates.
        if (colorOp == 0 || colorOp > CKFF_TOP_STAGEBLEND) {
            program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP,
                             random.OneIn(2) ? 0u : CKFF_TOP_STAGEBLEND + 1 + random.Below(4));
        }
        if (colorOp == CKRST_TOP_DOTPRODUCT3) {
            if (alphaOp != CKRST_TOP_PREMODULATE) {
                const CKDWORD op = random.Below(31);
                program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP, op + (op >= CKRST_TOP_PREMODULATE));
            }
        } else if (alphaOp == 0 || alphaOp >= CKFF_TOP_STAGEBLEND) {
            program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP,
                             random.OneIn(2) ? 0u : CKFF_TOP_STAGEBLEND + random.Below(5));
        } else if (alphaOp == CKRST_TOP_DISABLE || alphaOp == CKRST_TOP_BUMPENVMAP ||
                   alphaOp == CKRST_TOP_BUMPENVMAPLUMINANCE) {
            static const CKDWORD kDisabled[] = {CKRST_TOP_DISABLE, CKRST_TOP_BUMPENVMAP,
                                                CKRST_TOP_BUMPENVMAPLUMINANCE};
            program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP, kDisabled[random.Below(3)]);
        }
    }
}

// ---------------------------------------------------------------------------

bool Compile(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout, CKJitFragmentShader &shader) {
    return CKFFCompileNativeFragmentProgram(key, layout, shader) && CKJitVerify(shader);
}

// Whether a shader only reads uniforms its key leaves open: none of
// u_ffProgram and the emulated sampler state, the line and affine modes, or
// the texture coordinate parameters.
bool ReadsOnlyOpenUniforms(const CKJitFragmentShader &shader) {
    for (int n = 0; n < shader.Nodes.Size(); ++n) {
        const CKJitNode &node = shader.Nodes[n];
        if (node.Op != CKJIT_OP_UNIFORM)
            continue;
        const uint32_t row = node.Imm[0];
        if (row >= ROW_PROGRAM || row == ROW_DRAW_PARAMS + 4 ||
            (row >= ROW_STAGE_PARAMS && (row - ROW_STAGE_PARAMS) % 2 == 0)) {
            return false;
        }
    }
    return true;
}

bool SameOutcome(const Outcome &a, const Outcome &b) {
    if (a.Discard || b.Discard)
        return a.Discard == b.Discard;
    for (int i = 0; i < 4; ++i) {
        if (!(a.Color[i] == b.Color[i]))
            return false;
    }
    return true;
}

void Report(CKFFSamplerLayout layout, const CKFFNativeFragmentKey &key, const CKJitFragmentShader &shader,
            const Outcome &expected, const Outcome &actual) {
    std::printf("\n%s, lanes:", kShaderNames[layout]);
    for (CKDWORD i = 0; i < CKFFFragmentProgram::LaneCount; ++i)
        std::printf(" %06x", key.Program.Lanes()[i]);
    std::printf("\nswitches:");
    for (CKDWORD word = 0; word < CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT; ++word)
        std::printf(" %08x", key.Switches[word]);
    const Outcome *outcomes[] = {&expected, &actual};
    const char *labels[] = {"expected", "actual"};
    for (int i = 0; i < 2; ++i) {
        const Outcome &o = *outcomes[i];
        std::printf("\n%-8s %s (%.9g, %.9g, %.9g, %.9g)", labels[i], o.Discard ? "discard" : "color", o.Color[0],
                    o.Color[1], o.Color[2], o.Color[3]);
    }
    std::printf("\n%s\n", CKJitDump(shader).CStr());
}

void TestMatchesUberShaders() {
    const int kPrograms = 2000;
    const int kDraws = 2;
    const int kFragments = 3;
    Random random(0x4a17f7a9e1ull);
    for (CKFFSamplerLayout layout : kLayouts) {
        for (int p = 0; p < kPrograms; ++p) {
            const CKFFFragmentProgram program = RandomProgram(random, p % 4 == 0);
            for (int d = 0; d < kDraws; ++d) {
                Fragment fragments[kFragments];
                RandomFragment(random, fragments[0]);
                const CKFFNativeFragmentKey key = DrawKey(program, fragments[0]);
                for (int f = 1; f < kFragments; ++f) {
                    RandomFragment(random, fragments[f]);
                    ShareDrawState(fragments[0], fragments[f], random);
                    TestCheck(DrawKey(program, fragments[f]) == key, "draws sharing the state keys read share keys");
                }

                const CKFFNativeFragmentKey keys[] = {key, Canonical(key, layout)};
                TestCheck(Canonical(keys[1], layout) == keys[1], "canonical keys are left unchanged");
                for (const CKFFNativeFragmentKey &compiled : keys) {
                    CKJitFragmentShader shader;
                    TestCheck(Compile(compiled, layout, shader), "every key compiles");
                    TestCheck(ReadsOnlyOpenUniforms(shader), "shaders never read the state their key decides");
                    for (const Fragment &fragment : fragments) {
                        const Outcome expected = Reference{fragment, layout}.Evaluate(program.Lanes());
                        const Outcome actual = Execute(shader, fragment);
                        if (!SameOutcome(expected, actual)) {
                            Report(layout, compiled, shader, expected, actual);
                            TestFail("compiled keys compute what the uber shader computes");
                        }
                    }
                }
            }
        }
    }
}

void TestCanonicalKeys() {
    const int kKeys = 1000;
    const int kScrambles = 4;
    Random random(0x51c3a0e7d2ull);
    for (CKFFSamplerLayout layout : kLayouts) {
        for (int p = 0; p < kKeys; ++p) {
            Fragment fragment;
            RandomFragment(random, fragment);
            if (p % 4 == 0) { // no textures, so nothing reads the affine mode
                for (int stage = 0; stage < 8; ++stage)
                    fragment.Uniforms[ROW_STAGE_PARAMS + stage * 2][2] = 0.0f;
            }
            // Sampler state words with flags in every field, some without
            // any, and depth comparisons of every function.
            for (int stage = 0; stage < 8; ++stage) {
                float &state = fragment.Uniforms[ROW_BUMP_ENV + stage * 2 + 1][3];
                state = random.OneIn(4) ? 0.0f : (float)(random.Next() & 0xfffffu);
            }
            CKFFFragmentProgram program = RandomProgram(random, p % 8 == 0);
            for (CKDWORD stage = 0; stage < 8; ++stage) {
                if (random.OneIn(2))
                    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, random.Below(16));
            }

            const CKFFNativeFragmentKey native = Canonical(DrawKey(program, fragment), layout);
            for (int sampled = 0; sampled < 2; ++sampled) {
                const CKFFNativeFragmentKey key = DrawKey(program, fragment, sampled != 0);
                const CKFFNativeFragmentKey canonical = Canonical(key, layout);
                TestCheck(Canonical(canonical, layout) == canonical, "canonical keys are left unchanged");
                for (int s = 0; s < kScrambles; ++s) {
                    CKFFNativeFragmentKey scrambled = key;
                    Scramble(scrambled, layout, random);
                    TestCheck(Canonical(scrambled, layout) == canonical, "canonical keys hold only what shaders read");
                }

                // A shader-sampling key keeps what the native key does, and
                // is the native key if it emulates nothing.
                CKFFNativeFragmentKey stripped = canonical;
                stripped.Switches[0] &= ~(CKDWORD)CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;
                TestCheck(Canonical(stripped, layout) == native, "shader-sampling keys keep what native keys do");
                bool emulates = false;
                for (CKDWORD stage = 0; stage < 8; ++stage) {
                    emulates = emulates ||
                               canonical.Program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC) != 0;
                }
                emulates = emulates || canonical.Switches[3] != 0 || canonical.Switches[4] != 0;
                TestCheck(((canonical.Switches[0] & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) != 0) == emulates,
                          "shader-sampling keys that emulate nothing are native keys");
                if (!emulates)
                    TestCheck(canonical == native, "shader-sampling keys that emulate nothing are native keys");
            }
        }
    }

    // A draw's sampling flags come from its sampler state word and
    // mirror-once flags, and a shader-sampling key emulates only them.
    {
        Fragment fragment;
        Random draws(0x5a3e11ull);
        RandomFragment(draws, fragment);
        CKFFFragmentProgram program;
        program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, CKRST_TOP_SELECTARG1);
        program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1, CKFFFragmentProgram::RepackArg(CKRST_TA_TEXTURE));
        program.SetStage(1, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, CKRST_TOP_DISABLE);
        program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_VOLUME);
        fragment.Uniforms[ROW_STAGE_PARAMS][2] = 1.0f;
        fragment.Uniforms[ROW_STAGE_PARAMS][1] = (float)(CKFF_TTF_MIRRORONCE_U | CKFF_TTF_MIRRORONCE_W);
        fragment.Uniforms[ROW_BUMP_ENV + 1][3] =
            (float)(3u | 4u << CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT | 1u << CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT |
                    CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT | CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY);
        const CKFFNativeFragmentKey key = DrawKey(program, fragment, true);
        TestCheck((key.Switches[3] & 0xFFu) ==
                      (CKFF_NATIVE_FRAGMENT_MIRROR_U | CKFF_NATIVE_FRAGMENT_MIRROR_W | CKFF_NATIVE_FRAGMENT_BORDER |
                       CKFF_NATIVE_FRAGMENT_GRADIENT | CKFF_NATIVE_FRAGMENT_ANISOTROPY | CKFF_NATIVE_FRAGMENT_MIN_MIP),
                  "draw keys hold the sampling flags of their sampler state");
        TestCheck((DrawKey(program, fragment).Switches[3] & 0xFFu) == 0, "native draw keys hold no sampling flags");
        TestCheck((Canonical(key, CKFF_SAMPLER_LAYOUT_WIDE_VOLUME).Switches[3] & 0xFFu) ==
                      (CKFF_NATIVE_FRAGMENT_MIRROR_U | CKFF_NATIVE_FRAGMENT_MIRROR_W | CKFF_NATIVE_FRAGMENT_BORDER |
                       CKFF_NATIVE_FRAGMENT_GRADIENT | CKFF_NATIVE_FRAGMENT_ANISOTROPY),
                  "volume keys with anisotropy ignore the minimum mip level");
        program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_CUBE);
        TestCheck(Canonical(DrawKey(program, fragment, true), CKFF_SAMPLER_LAYOUT_WIDE_CUBE) ==
                      Canonical(DrawKey(program, fragment), CKFF_SAMPLER_LAYOUT_WIDE_CUBE),
                  "cube textures emulate nothing");
    }

    // The BOTH* source factors imply their destination factor.
    CKFFNativeFragmentKey blend;
    blend.Program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, CKFF_TOP_STAGEBLEND);
    const CKDWORD pairs[][2] = {
        {VXBLEND_BOTHSRCALPHA << 4 | VXBLEND_ONE, VXBLEND_SRCALPHA << 4 | VXBLEND_INVSRCALPHA},
        {VXBLEND_BOTHINVSRCALPHA << 4 | VXBLEND_ZERO, VXBLEND_INVSRCALPHA << 4 | VXBLEND_SRCALPHA},
        {VXBLEND_ONE << 4 | VXBLEND_BOTHSRCALPHA, VXBLEND_ONE << 4 | VXBLEND_SRCALPHA},
        {0xE << 4 | 0, VXBLEND_ZERO << 4 | VXBLEND_ZERO},
    };
    for (const CKDWORD *pair : pairs) {
        blend.Switches[1] = pair[0];
        TestCheck(Canonical(blend, CKFF_SAMPLER_LAYOUT_WIDE_2D).Switches[1] == pair[1],
                  "STAGEBLEND factor pairs are canonical");
    }
}

void TestInterface() {
    for (CKFFSamplerLayout layout : kLayouts) {
        CKJitFragmentShader shader;
        TestCheck(Compile(CKFFNativeFragmentKey(), layout, shader), "the empty key compiles");
        TestCheck(shader.UniformVec4Count == ROW_COUNT, "the uniform block is the native fragment block");
        TestCheck(shader.Inputs.Size() == REG_COUNT, "every native varying is declared");

        const CKJitInput &position = shader.Inputs[0];
        TestCheck(position.Kind == CKJIT_INPUT_FRAG_COORD && position.Register == REG_POSITION &&
                      position.Components == 4 && std::strcmp(position.Semantic, "SV_Position") == 0,
                  "SV_Position comes first");
        for (uint32_t v = 0; v + 1 < REG_COUNT; ++v) {
            const CKJitInput &input = shader.Inputs[(int)v + 1];
            const bool flat = v + 1 == REG_FLAT_COLOR0 || v + 1 == REG_FLAT_COLOR1;
            TestCheck(std::strcmp(input.Semantic, "TEXCOORD") == 0 && input.SemanticIndex == v &&
                          input.Location == v && input.Register == v + 1,
                      "varying v is TEXCOORDv at location v and register v + 1");
            TestCheck(input.Components == (v + 1 == REG_LINE_OFFSET ? 2 : 4), "varyings keep their widths");
            TestCheck(input.Kind == (flat ? CKJIT_INPUT_FLAT : CKJIT_INPUT_SMOOTH),
                      "only the flat colours are not interpolated");
        }
    }

    CKJitFragmentShader shader;
    TestCheck(!CKFFCompileNativeFragmentProgram(CKFFNativeFragmentKey(), CKFF_SAMPLER_LAYOUT_COUNT, shader),
              "layouts without native shaders are refused");
}

void SetSelectArg1(CKFFFragmentProgram &program, CKDWORD stage, CKDWORD arg) {
    const CKDWORD packed = CKFFFragmentProgram::RepackArg(arg);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, CKRST_TOP_SELECTARG1);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1, packed);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP, CKRST_TOP_SELECTARG1);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1, packed);
}

void TestSpecialization() {
    // Diffuse pass-through reads no uniforms; only antialiased lines discard.
    CKFFNativeFragmentKey passThrough;
    SetSelectArg1(passThrough.Program, 0, CKRST_TA_DIFFUSE);
    CKJitFragmentShader shader;
    for (int line = 0; line < 2; ++line) {
        passThrough.Switches[0] = line ? CKFF_NATIVE_FRAGMENT_LINE : 0;
        TestCheck(Compile(passThrough, CKFF_SAMPLER_LAYOUT_WIDE_2D, shader), "the pass-through program compiles");
        for (int n = 0; n < shader.Nodes.Size(); ++n) {
            const CKJitNode &node = shader.Nodes[n];
            TestCheck(node.Op != CKJIT_OP_SAMPLE && node.Op != CKJIT_OP_UNIFORM,
                      "the pass-through program samples and reads nothing");
        }
        TestCheck(shader.Discard.IsValid() == (line != 0), "only antialiased lines discard");
    }

    // A texture stage samples the one texture its type and ordinal select, if
    // it has one, and reads its LOD bias if that is not zero.
    struct Case {
        CKFFSamplerLayout Layout;
        CKDWORD Stage;
        CKDWORD SamplerType;
        CKDWORD Ordinal;
        int Slot; // -1: the layout declares no such texture
        CKJitSamplerDim Dim;
    };
    const Case cases[] = {
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 5, CKFF_SAMPLER_2D, 0, 5, CKJIT_SAMPLER_2D},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 3, CKFF_SAMPLER_DEPTH, 6, 3, CKJIT_SAMPLER_2D},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 0, CKFF_SAMPLER_CUBE, 3, 11, CKJIT_SAMPLER_CUBE},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 0, CKFF_SAMPLER_CUBE, 4, -1, CKJIT_SAMPLER_CUBE},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 2, CKFF_SAMPLER_VOLUME, 1, 13, CKJIT_SAMPLER_3D},
        {CKFF_SAMPLER_LAYOUT_WIDE_CUBE, 5, CKFF_SAMPLER_2D, 3, 3, CKJIT_SAMPLER_2D},
        {CKFF_SAMPLER_LAYOUT_WIDE_CUBE, 5, CKFF_SAMPLER_2D, 4, -1, CKJIT_SAMPLER_2D},
        {CKFF_SAMPLER_LAYOUT_WIDE_CUBE, 1, CKFF_SAMPLER_CUBE, 7, 11, CKJIT_SAMPLER_CUBE},
        {CKFF_SAMPLER_LAYOUT_WIDE_CUBE, 0, CKFF_SAMPLER_VOLUME, 0, 12, CKJIT_SAMPLER_3D},
        {CKFF_SAMPLER_LAYOUT_WIDE_VOLUME, 4, CKFF_SAMPLER_2D, 1, 1, CKJIT_SAMPLER_2D},
        {CKFF_SAMPLER_LAYOUT_WIDE_VOLUME, 0, CKFF_SAMPLER_CUBE, 2, 6, CKJIT_SAMPLER_CUBE},
        {CKFF_SAMPLER_LAYOUT_WIDE_VOLUME, 7, CKFF_SAMPLER_VOLUME, 7, 15, CKJIT_SAMPLER_3D},
    };
    for (const Case &c : cases) {
        CKFFNativeFragmentKey key;
        CKFFFragmentProgram &program = key.Program;
        for (CKDWORD stage = 0; stage < c.Stage; ++stage)
            SetSelectArg1(program, stage, CKRST_TA_CURRENT);
        SetSelectArg1(program, c.Stage, CKRST_TA_TEXTURE);
        program.SetStage(c.Stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, c.SamplerType);
        program.SetSamplerOrdinal(c.Stage, c.Ordinal);
        program.Set(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE, c.Stage);

        // Without a texture, with one, and with one and a LOD bias.
        for (int variant = 0; variant < 3; ++variant) {
            key.Switches[0] = variant == 0 ? 0u : (CKDWORD)CKFF_NATIVE_FRAGMENT_TEXTURE << c.Stage;
            if (variant == 2)
                key.Switches[0] |= (CKDWORD)CKFF_NATIVE_FRAGMENT_LOD_BIAS << c.Stage;
            TestCheck(Compile(key, c.Layout, shader), "the texture program compiles");
            const bool sampled = variant != 0 && c.Slot >= 0;

            int samples = 0;
            for (int n = 0; n < shader.Nodes.Size(); ++n) {
                const CKJitNode &node = shader.Nodes[n];
                if (node.Op == CKJIT_OP_UNIFORM) {
                    TestCheck(sampled && variant == 2 && node.Imm[0] == ROW_BUMP_ENV + c.Stage * 2 + 1,
                              "a texture stage reads only its LOD bias, and only if it has one");
                }
                if (node.Op != CKJIT_OP_SAMPLE)
                    continue;
                ++samples;
                TestCheck(node.Imm[0] == (uint32_t)c.Slot && node.Imm[1] == c.Dim,
                          "the stage samples the texture register of the native shader");
            }
            TestCheck(samples == (sampled ? 1 : 0), "a stage samples once, or not at all without a texture");
        }
    }
}

void Save(const char *name, int index, const char *extension, const XArray<uint32_t> &words) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s_%02d.%s", g_ShaderDirectory, name, index, extension);
    FILE *file = std::fopen(path, "wb");
    TestCheck(file != nullptr, "the shader file opens");
    if (file) {
        TestCheck(std::fwrite(words.Begin(), sizeof(uint32_t), (size_t)words.Size(), file) == (size_t)words.Size(),
                  "the shader is written");
        std::fclose(file);
    }
}

void TestEmission() {
    const int kPrograms = 400;
    const int kSaved = 12;
    Random random(0x2f6b3c1d5eull);
    for (CKFFSamplerLayout layout : kLayouts) {
        for (int p = 0; p < kPrograms; ++p) {
            Fragment fragment;
            RandomFragment(random, fragment);
            const CKFFNativeFragmentKey key = Canonical(DrawKey(RandomProgram(random, p % 2 == 0), fragment), layout);
            CKJitFragmentShader shader;
            TestCheck(Compile(key, layout, shader), "every key compiles");
            XArray<uint32_t> spirv;
            XArray<uint32_t> dxbc;
            TestCheck(CKJitEmitSpirv(shader, kLayout, spirv) && spirv.Size() > 0, "every program emits SPIR-V");
            TestCheck(CKJitEmitDxbc(shader, kLayout, dxbc) && dxbc.Size() > 0, "every program emits DXBC");
            if (g_ShaderDirectory && p < kSaved) {
                Save(kShaderNames[layout], p, "spv", spirv);
                Save(kShaderNames[layout], p, "dxbc", dxbc);
            }
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 1)
        g_ShaderDirectory = argv[1];
    TestFramework framework;
    framework.Run("interface", TestInterface);
    framework.Run("specialization", TestSpecialization);
    framework.Run("matches the uber shaders", TestMatchesUberShaders);
    framework.Run("canonical keys", TestCanonicalKeys);
    framework.Run("emission", TestEmission);
    return framework.ExitCode();
}
