#include "CKFFNativeFragmentJit.h"
#include "CKFFNativePositionTJit.h"
#include "CKFFNative3dJit.h"
#include <limits>
#include "CKFFShaderInterface.h"
#include "CKFFStageState.h"
#include "CKFFStateDesc.h"
#if CKRE_ENABLE_DIRECTX
#include "CKJitDxbc.h"
#endif
#include "CKJitSpirv.h"
#include "TestTriangleMultiset.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#if defined(_MSC_VER) && defined(_M_IX86)
#include <cfloat>
#endif

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

// The fragment uniform rows, in the order of cbuffer CKFragment. NativeRows
// maps the rows of the native buffers the shaders read to them.
enum Row {
    ROW_DRAW_PARAMS = 0,
    ROW_BUMP_ENV = 20,
    ROW_STAGE_PARAMS = 36,
    ROW_PROGRAM = 52,       // u_ffProgram
    ROW_BORDER_COLOR = 57,  // of each sampler slot, then
    ROW_SAMPLER_INFO = 73,  // their address modes and filters
    ROW_COUNT = 89,
};

const CKFFSamplerLayout kLayouts[] = {
    CKFF_SAMPLER_LAYOUT_WIDE_2D,
    CKFF_SAMPLER_LAYOUT_WIDE_CUBE,
    CKFF_SAMPLER_LAYOUT_WIDE_VOLUME,
};
const char *const kShaderNames[] = {"fs_ff_stage_native", "fs_ff_stage_cube_native", "fs_ff_stage_volume_native"};

// The texture bound to a sampler slot: its base mip's extent and mip count.
struct Texture {
    int32_t Size[3];
    int32_t Levels;
};

// The fragment row each row of the native fragment uniform buffers holds, as
// CKFFBuildProgramInterface lays them out; ROW_COUNT where none.
class NativeRows {
public:
    NativeRows() : m_Complete(true) {
        for (uint32_t buffer = 0; buffer < CKJIT_MAX_UNIFORM_BUFFERS; ++buffer) {
            for (uint32_t row = 0; row < kMaxRows; ++row)
                m_Rows[buffer][row] = ROW_COUNT;
        }
        // Every sampler layout shares the uniform layout.
        const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, CKRST_SHADER_FORMAT_SPIRV, FALSE, FALSE,
                                                               CKFF_SAMPLER_LAYOUT_WIDE_2D);
        const struct {
            CKFFConstantBlock Id;
            uint32_t First;
            uint32_t End;
        } blocks[] = {
            {CKRST_BLOCK_DRAW_PARAMS, ROW_DRAW_PARAMS, ROW_BUMP_ENV},
            {CKRST_BLOCK_BUMP_ENV, ROW_BUMP_ENV, ROW_STAGE_PARAMS},
            {CKRST_BLOCK_STAGE_PARAMS, ROW_STAGE_PARAMS, ROW_PROGRAM},
            {CKRST_BLOCK_FRAGMENT_PROGRAM, ROW_PROGRAM, ROW_BORDER_COLOR},
        };
        bool mapped[ROW_COUNT] = {};
        for (int i = 0; i < desc.Uniforms.Size(); ++i) {
            const CKFFUniformBinding &uniform = desc.Uniforms[i];
            for (const auto &block : blocks) {
                if (uniform.Stage == CKRST_SHADER_PIXEL && uniform.Slot == (CKDWORD)block.Id)
                    Map(uniform.BufferSlot, uniform.Offset / 16u, block.First, block.End - block.First, mapped);
            }
        }
        const CKFFSamplerBinding &sampler = desc.Samplers[0];
        Map(sampler.MetadataBufferSlot, sampler.BorderColorOffset / 16u, ROW_BORDER_COLOR, CKFF_SAMPLER_SLOT_COUNT,
            mapped);
        Map(sampler.MetadataBufferSlot, sampler.SamplerStateOffset / 16u, ROW_SAMPLER_INFO, CKFF_SAMPLER_SLOT_COUNT,
            mapped);
        for (bool row : mapped)
            m_Complete = m_Complete && row;
    }

    uint32_t Row(uint32_t buffer, uint32_t row) const {
        return buffer < CKJIT_MAX_UNIFORM_BUFFERS && row < kMaxRows ? m_Rows[buffer][row] : (uint32_t)ROW_COUNT;
    }
    // Whether each fragment row is in a native buffer exactly once.
    bool Complete() const { return m_Complete; }

private:
    static const uint32_t kMaxRows = 128;

    void Map(uint32_t buffer, uint32_t first, uint32_t row, uint32_t count, bool *mapped) {
        for (uint32_t i = 0; i < count; ++i) {
            if (buffer >= CKJIT_MAX_UNIFORM_BUFFERS || first + i >= kMaxRows || mapped[row + i] ||
                m_Rows[buffer][first + i] != ROW_COUNT) {
                m_Complete = false;
                continue;
            }
            m_Rows[buffer][first + i] = row + i;
            mapped[row + i] = true;
        }
    }

    uint32_t m_Rows[CKJIT_MAX_UNIFORM_BUFFERS][kMaxRows];
    bool m_Complete;
};

const NativeRows &Natives() {
    static const NativeRows rows;
    return rows;
}

struct Fragment {
    float Registers[REG_COUNT][4];
    float Uniforms[ROW_COUNT][4];
    Texture Textures[CKJIT_MAX_SAMPLERS];
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
// Textures: a texel is a hash of the access, its slot and dimension, and its
// operands, so an access only matches one of the same kind with all of them
// equal. Screen-space derivatives are a hash of the value too, so equal
// expressions have equal derivatives in a program and the reference.

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

uint32_t CoordinateCount(CKJitSamplerDim dim) {
    return dim == CKJIT_SAMPLER_2D || dim == CKJIT_SAMPLER_2D_COMPARE ? 2u : 3u;
}

class TexelHash {
public:
    TexelHash(CKJitOp op, uint32_t slot, CKJitSamplerDim dim)
        : m_Hash(Mix(Mix(0x6a09e667u, (uint32_t)op), slot * 4u + (uint32_t)dim)) {}

    TexelHash &Add(const float *values, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i)
            m_Hash = Mix(m_Hash, HashBits(values[i]));
        return *this;
    }
    TexelHash &Add(const int32_t *values, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i)
            m_Hash = Mix(m_Hash, (uint32_t)values[i]);
        return *this;
    }

    void Color(float out[4]) const {
        uint32_t h = m_Hash;
        for (uint32_t i = 0; i < 4; ++i) {
            h = Mix(h, i);
            // Some texels are exact 8-bit values, like most real textures.
            out[i] = (h & 3u) == 0 ? (float)((h >> 8) & 255u) / 255.0f : (float)(h >> 8) * (1.0f / 16777216.0f);
        }
    }

private:
    uint32_t m_Hash;
};

// The derivative of a value along the x (axis 0) or y axis: magnitudes from
// 2^-12 to 1 of either sign, and some zero.
float Derivative(float value, uint32_t axis) {
    const uint32_t h = Mix(Mix(0x3c6ef372u, axis), HashBits(value));
    if ((h & 15u) == 0)
        return 0.0f;
    const float magnitude = std::exp2((float)((h >> 8) & 0xfffu) * (12.0f / 4096.0f) - 12.0f);
    return (h & 16u) != 0 ? -magnitude : magnitude;
}

// The LOD SAMPLE takes, before clamping: of the longer derivative, in texels
// of the base mip.
float CalcLod(const Texture &texture, const float *coordinate, uint32_t count) {
    float lengths[2];
    for (uint32_t axis = 0; axis < 2; ++axis) {
        float sum = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            const float texels = Derivative(coordinate[i], axis) * (float)texture.Size[i];
            sum = sum + texels * texels;
        }
        lengths[axis] = std::sqrt(sum);
    }
    return std::log2(std::fmax(lengths[0], lengths[1]));
}

// The extent of a mip along an axis.
int32_t MipExtent(const Texture &texture, uint32_t axis, int32_t mip) {
    const int32_t extent = texture.Size[axis] >> mip;
    return extent > 0 ? extent : 1;
}

// ---------------------------------------------------------------------------
// IR interpreter.

struct Value {
    float F[4];
    int32_t I[4];
    bool B[4];
};

int32_t Truncate(float x) {
    if (!(x > -2147483648.0f))
        return x != x ? 0 : INT32_MIN;
    return x < 2147483648.0f ? (int32_t)x : INT32_MAX;
}

// Runs a shader for a fragment. Regions and loops run as structured control
// flow: an IF skips the arm it does not take, and a loop's body runs again
// from its ENDLOOP.
struct VertexInputs {
    float Attributes[16][4];
    float Uniforms[3][128][4];
};

void ExecuteNodes(const CKJitShader &shader, const Fragment &fragment, XArray<Value> &values,
                  const VertexInputs *vertex = nullptr) {
    const int count = shader.Nodes.Size();
    values.Resize(count);
    // The ELSE of an IF, the ENDIF of an ELSE and the ENDLOOP of a LOOP; the
    // arm an IF takes, or a loop's iterations and the iteration it runs.
    XArray<int> closing;
    XArray<int32_t> taken;
    XArray<int32_t> iteration;
    closing.Resize(count);
    taken.Resize(count);
    iteration.Resize(count);
    for (int n = 0; n < count; ++n) {
        const CKJitNode &node = shader.Nodes[n];
        if (node.Op == CKJIT_OP_ELSE || node.Op == CKJIT_OP_ENDIF || node.Op == CKJIT_OP_ENDLOOP)
            closing[(int)node.Operands[0]] = n;
    }
    // The first node of a loop's body, past its INDEX and CARRY nodes.
    auto body = [&](int loop) {
        int n = loop + 1;
        while (n < count && (shader.Nodes[n].Op == CKJIT_OP_INDEX || shader.Nodes[n].Op == CKJIT_OP_CARRY))
            ++n;
        return n;
    };

    for (int n = 0; n < count; ++n) {
        const CKJitNode &node = shader.Nodes[n];
        Value &out = values[n];
        std::memset(&out, 0, sizeof(out));
        const Value &a = values[node.OperandCount > 0 ? (int)node.Operands[0] : n];
        const Value &b = values[node.OperandCount > 1 ? (int)node.Operands[1] : n];
        const Value &c = values[node.OperandCount > 2 ? (int)node.Operands[2] : n];
        const uint32_t width = CKJitComponentCount(node.Type);
        const uint32_t operandWidth =
            node.OperandCount > 0 ? CKJitComponentCount(shader.Nodes[(int)node.Operands[0]].Type) : 0;
        const Texture &texture = fragment.Textures[node.Imm[0] % CKJIT_MAX_SAMPLERS];
        const CKJitSamplerDim dim = (CKJitSamplerDim)node.Imm[1];
        switch (node.Op) {
        case CKJIT_OP_CONSTANT:
            for (uint32_t i = 0; i < width; ++i) {
                if (CKJitIsBool(node.Type))
                    out.B[i] = node.Imm[i] != 0;
                else if (CKJitIsInt(node.Type))
                    out.I[i] = (int32_t)node.Imm[i];
                else
                    std::memcpy(&out.F[i], &node.Imm[i], sizeof(float));
            }
            break;
        case CKJIT_OP_INPUT: {
            const CKJitInput &input = shader.Inputs[(int)node.Imm[0]];
            if (vertex) {
                TestCheck(input.Kind == CKJIT_INPUT_ATTRIBUTE && input.Location < 16, "native vertex attributes");
                if (input.Scalar == CKJIT_INPUT_UINT)
                    std::memcpy(out.I, vertex->Attributes[input.Location], input.Components * sizeof(uint32_t));
                else
                    std::memcpy(out.F, vertex->Attributes[input.Location], input.Components * sizeof(float));
                break;
            }
            const uint32_t reg = input.Kind == CKJIT_INPUT_FRAG_COORD ? REG_POSITION : input.Location + 1;
            TestCheck(reg < REG_COUNT, "inputs are native shader registers");
            std::memcpy(out.F, fragment.Registers[reg], input.Components * sizeof(float));
            break;
        }
        case CKJIT_OP_UNIFORM: {
            if (vertex) {
                TestCheck(node.Imm[1] < 3 && node.Imm[0] < 128, "native vertex uniform rows");
                std::memcpy(out.F, vertex->Uniforms[node.Imm[1]][node.Imm[0]], sizeof(out.F));
                break;
            }
            const uint32_t row = Natives().Row(node.Imm[1], node.Imm[0]);
            TestCheck(row < ROW_COUNT, "uniforms are native constant buffer rows");
            if (row < ROW_COUNT)
                std::memcpy(out.F, fragment.Uniforms[row], sizeof(out.F));
            break;
        }
        case CKJIT_OP_SWIZZLE:
            for (uint32_t i = 0; i < width; ++i) {
                out.F[i] = a.F[node.Imm[i]];
                out.I[i] = a.I[node.Imm[i]];
                out.B[i] = a.B[node.Imm[i]];
            }
            break;
        case CKJIT_OP_CONSTRUCT: {
            uint32_t component = 0;
            for (uint32_t o = 0; o < node.OperandCount; ++o) {
                const int part = (int)node.Operands[o];
                for (uint32_t i = 0; i < CKJitComponentCount(shader.Nodes[part].Type); ++i, ++component) {
                    out.F[component] = values[part].F[i];
                    out.I[component] = values[part].I[i];
                    out.B[component] = values[part].B[i];
                }
            }
            break;
        }
        case CKJIT_OP_ADD: for (uint32_t i = 0; i < width; ++i) out.F[i] = a.F[i] + b.F[i]; break;
        case CKJIT_OP_SUB: for (uint32_t i = 0; i < width; ++i) out.F[i] = a.F[i] - b.F[i]; break;
        case CKJIT_OP_MUL: for (uint32_t i = 0; i < width; ++i) out.F[i] = a.F[i] * b.F[i]; break;
        case CKJIT_OP_MAD:
            for (uint32_t i = 0; i < width; ++i)
                out.F[i] = std::fma(a.F[i], b.F[i], values[(int)node.Operands[2]].F[i]);
            break;
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
        case CKJIT_OP_CEIL: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::ceil(a.F[i]); break;
        case CKJIT_OP_ROUND_EVEN: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::nearbyint(a.F[i]); break;
        case CKJIT_OP_EXP2: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::exp2(a.F[i]); break;
        case CKJIT_OP_LOG2: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::log2(a.F[i]); break;
        case CKJIT_OP_SQRT: for (uint32_t i = 0; i < width; ++i) out.F[i] = std::sqrt(a.F[i]); break;
        case CKJIT_OP_DOT: {
            float sum = a.F[0] * b.F[0];
            for (uint32_t i = 1; i < operandWidth; ++i) {
                const float product = a.F[i] * b.F[i];
                sum = sum + product;
            }
            out.F[0] = sum;
            break;
        }
        case CKJIT_OP_DDX: for (uint32_t i = 0; i < width; ++i) out.F[i] = Derivative(a.F[i], 0); break;
        case CKJIT_OP_DDY: for (uint32_t i = 0; i < width; ++i) out.F[i] = Derivative(a.F[i], 1); break;
        case CKJIT_OP_LT: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.F[i] < b.F[i]; break;
        case CKJIT_OP_LE: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.F[i] <= b.F[i]; break;
        case CKJIT_OP_EQ: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.F[i] == b.F[i]; break;
        case CKJIT_OP_NE: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.F[i] != b.F[i]; break;
        case CKJIT_OP_FTOI: for (uint32_t i = 0; i < width; ++i) out.I[i] = Truncate(a.F[i]); break;
        case CKJIT_OP_ITOF: for (uint32_t i = 0; i < width; ++i) out.F[i] = (float)a.I[i]; break;
        case CKJIT_OP_IADD:
            for (uint32_t i = 0; i < width; ++i)
                out.I[i] = (int32_t)((uint32_t)a.I[i] + (uint32_t)b.I[i]);
            break;
        case CKJIT_OP_ISUB:
            for (uint32_t i = 0; i < width; ++i)
                out.I[i] = (int32_t)((uint32_t)a.I[i] - (uint32_t)b.I[i]);
            break;
        case CKJIT_OP_IMUL:
            for (uint32_t i = 0; i < width; ++i)
                out.I[i] = (int32_t)((uint32_t)a.I[i] * (uint32_t)b.I[i]);
            break;
        case CKJIT_OP_IMIN: for (uint32_t i = 0; i < width; ++i) out.I[i] = a.I[i] < b.I[i] ? a.I[i] : b.I[i]; break;
        case CKJIT_OP_IMAX: for (uint32_t i = 0; i < width; ++i) out.I[i] = a.I[i] > b.I[i] ? a.I[i] : b.I[i]; break;
        case CKJIT_OP_IMOD:
            for (uint32_t i = 0; i < width; ++i) {
                TestCheck(b.I[i] > 0, "remainders have positive divisors");
                const int32_t r = a.I[i] % b.I[i];
                out.I[i] = r < 0 ? r + b.I[i] : r;
            }
            break;
        case CKJIT_OP_IAND: for (uint32_t i = 0; i < width; ++i) out.I[i] = a.I[i] & b.I[i]; break;
        case CKJIT_OP_ISHR: for (uint32_t i = 0; i < width; ++i) out.I[i] = a.I[i] >> (b.I[i] & 31); break;
        case CKJIT_OP_ILT: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.I[i] < b.I[i]; break;
        case CKJIT_OP_ILE: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.I[i] <= b.I[i]; break;
        case CKJIT_OP_IEQ: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.I[i] == b.I[i]; break;
        case CKJIT_OP_INE: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.I[i] != b.I[i]; break;
        case CKJIT_OP_AND: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.B[i] && b.B[i]; break;
        case CKJIT_OP_OR: for (uint32_t i = 0; i < width; ++i) out.B[i] = a.B[i] || b.B[i]; break;
        case CKJIT_OP_NOT: for (uint32_t i = 0; i < width; ++i) out.B[i] = !a.B[i]; break;
        case CKJIT_OP_ANY:
            for (uint32_t i = 0; i < operandWidth; ++i)
                out.B[0] = out.B[0] || a.B[i];
            break;
        case CKJIT_OP_ALL:
            out.B[0] = true;
            for (uint32_t i = 0; i < operandWidth; ++i)
                out.B[0] = out.B[0] && a.B[i];
            break;
        case CKJIT_OP_SELECT:
            // A BOOL condition picks whole arms, a vector one components.
            for (uint32_t i = 0; i < 4; ++i) {
                const bool pick = a.B[operandWidth == 1 ? 0 : i];
                out.F[i] = pick ? b.F[i] : c.F[i];
                out.I[i] = pick ? b.I[i] : c.I[i];
                out.B[i] = pick ? b.B[i] : c.B[i];
            }
            break;

        case CKJIT_OP_IF:
            taken[n] = a.B[0];
            if (!a.B[0])
                n = closing[n]; // on past the ELSE
            break;
        case CKJIT_OP_ELSE:
            if (taken[(int)node.Operands[0]])
                n = closing[n]; // on past the ENDIF
            break;
        case CKJIT_OP_ENDIF: break;
        case CKJIT_OP_PHI: {
            const int ifNode = (int)shader.Nodes[(int)shader.Nodes[(int)node.Operands[2]].Operands[0]].Operands[0];
            out = taken[ifNode] ? a : b;
            break;
        }
        case CKJIT_OP_LOOP:
            taken[n] = a.I[0] < (int32_t)node.Imm[0] ? a.I[0] : (int32_t)node.Imm[0];
            iteration[n] = 0;
            if (taken[n] <= 0) {
                // The carried values keep their initials.
                for (int m = n + 1; m < body(n); ++m) {
                    if (shader.Nodes[m].Op == CKJIT_OP_CARRY)
                        values[m] = values[(int)shader.Nodes[m].Operands[0]];
                }
                n = closing[n]; // on past the ENDLOOP
            }
            break;
        case CKJIT_OP_INDEX: out.I[0] = iteration[(int)node.Operands[0]]; break;
        case CKJIT_OP_CARRY: out = a; break;
        case CKJIT_OP_ENDLOOP: {
            // The RESULT nodes following it pair each carried value with its next.
            const int loop = (int)node.Operands[0];
            XArray<Value> nexts;
            for (int m = n + 1;
                 m < count && shader.Nodes[m].Op == CKJIT_OP_RESULT && (int)shader.Nodes[m].Operands[2] == n; ++m)
                nexts.PushBack(values[(int)shader.Nodes[m].Operands[1]]);
            for (int r = 0; r < nexts.Size(); ++r)
                values[(int)shader.Nodes[n + 1 + r].Operands[0]] = nexts[r];
            if (++iteration[loop] < taken[loop]) {
                const int start = body(loop);
                for (int m = loop + 1; m < start; ++m) {
                    if (shader.Nodes[m].Op == CKJIT_OP_INDEX)
                        values[m].I[0] = iteration[loop];
                }
                n = start - 1;
            }
            break;
        }
        case CKJIT_OP_RESULT: out = a; break;

        case CKJIT_OP_SAMPLE:
            TexelHash(node.Op, node.Imm[0], dim).Add(a.F, CoordinateCount(dim)).Add(b.F, 1).Color(out.F);
            break;
        case CKJIT_OP_SAMPLE_LEVEL:
            TexelHash(node.Op, node.Imm[0], dim).Add(a.F, CoordinateCount(dim)).Add(b.F, 1).Color(out.F);
            break;
        case CKJIT_OP_SAMPLE_GRAD:
            TexelHash(node.Op, node.Imm[0], dim)
                .Add(a.F, CoordinateCount(dim))
                .Add(b.F, CoordinateCount(dim))
                .Add(c.F, CoordinateCount(dim))
                .Color(out.F);
            break;
        case CKJIT_OP_CALC_LOD: out.F[0] = CalcLod(texture, a.F, CoordinateCount(dim)); break;
        case CKJIT_OP_SAMPLE_CMP:
        case CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO: {
            float texel[4];
            TexelHash(node.Op, node.Imm[0], dim).Add(a.F, 2).Add(b.F, 1).Color(texel);
            out.F[0] = texel[0];
            break;
        }
        case CKJIT_OP_LOAD: {
            const uint32_t axes = operandWidth - 1;
            const int32_t mip = a.I[axes];
            TestCheck(mip >= 0 && mip < texture.Levels, "loads read mips of the texture");
            for (uint32_t i = 0; i < axes; ++i)
                TestCheck(a.I[i] >= 0 && a.I[i] < MipExtent(texture, i, mip), "loads read texels within the mip");
            TexelHash(node.Op, node.Imm[0], dim).Add(a.I, operandWidth).Color(out.F);
            break;
        }
        case CKJIT_OP_SIZE:
            TestCheck(a.I[0] >= 0 && a.I[0] < texture.Levels, "sizes are of mips of the texture");
            for (uint32_t i = 0; i < width; ++i)
                out.I[i] = MipExtent(texture, i, a.I[0]);
            break;
        case CKJIT_OP_LEVELS: out.I[0] = texture.Levels; break;
        default: TestFail("the interpreter knows every operation");
        }
    }

}

Outcome Execute(const CKJitFragmentShader &shader, const Fragment &fragment) {
    XArray<Value> values;
    ExecuteNodes(shader, fragment, values);
    Outcome outcome;
    std::memcpy(outcome.Color, values[(int)shader.Color.Id].F, sizeof(outcome.Color));
    outcome.Discard = shader.Discard.IsValid() && values[(int)shader.Discard.Id].B[0];
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
    bool ShaderSampling = false;
    int Comparisons = 0; // of the wide 2D layout's comparison shader fs_ff_stage_compareN

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
        TexelHash(CKJIT_OP_SAMPLE, (uint32_t)slot, dim).Add(c, CoordinateCount(dim)).Add(&lodBias, 1).Color(texel);
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

    // native_sampling.hlsli, which the shader-sampling shaders sample 2D and
    // volume textures with. The address of a texel along an axis, and whether
    // it is outside a border-addressed texture.
    static int Address(int i, int extent, int mode, bool &outside) {
        if (mode == 4)
            outside = outside || i < 0 || i >= extent;
        if (mode == 1)
            return ((i % extent) + extent) % extent;
        if (mode == 2) {
            const int period = extent * 2;
            const int folded = ((i % period) + period) % period;
            return folded < extent ? folded : period - folded - 1;
        }
        return i < 0 ? 0 : i > extent - 1 ? extent - 1 : i;
    }

    Float4 Tap2D(int slot, int x, int y, int mip, int width, int height) const {
        const int modes = (int)Uniform(ROW_SAMPLER_INFO + slot).x;
        bool outside = false;
        const int texel[3] = {Address(x, width, modes & 15, outside), Address(y, height, (modes >> 4) & 15, outside),
                              mip};
        if (outside)
            return Uniform(ROW_BORDER_COLOR + slot);
        float color[4];
        TexelHash(CKJIT_OP_LOAD, (uint32_t)slot, CKJIT_SAMPLER_2D).Add(texel, 3).Color(color);
        return {color[0], color[1], color[2], color[3]};
    }

    static int Next(int i) { return (int)((uint32_t)i + 1u); }

    Float4 Level2D(int slot, float u, float v, int mip, bool filtered) const {
        const Texture &texture = F.Textures[slot];
        const int width = MipExtent(texture, 0, mip);
        const int height = MipExtent(texture, 1, mip);
        const float x = u * (float)width;
        const float y = v * (float)height;
        if (!filtered)
            return Tap2D(slot, Truncate(std::floor(x)), Truncate(std::floor(y)), mip, width, height);
        const float cx = x - 0.5f;
        const float cy = y - 0.5f;
        const float bx = std::floor(cx);
        const float by = std::floor(cy);
        const int x0 = Truncate(bx);
        const int y0 = Truncate(by);
        const Float4 top = Lerp(Tap2D(slot, x0, y0, mip, width, height), Tap2D(slot, Next(x0), y0, mip, width, height),
                                cx - bx);
        const Float4 bottom = Lerp(Tap2D(slot, x0, Next(y0), mip, width, height),
                                   Tap2D(slot, Next(x0), Next(y0), mip, width, height), cx - bx);
        return Lerp(top, bottom, cy - by);
    }

    // ckCompareVariantBorderLevel2D: a hardware sample of a mip, at the centre
    // of the texel unless filtered, blended with the border colour by the
    // coverage of the texels.
    Float4 BorderLevel2D(int slot, float u, float v, int mip, bool filtered) const {
        const Texture &texture = F.Textures[slot];
        const int modes = (int)Uniform(ROW_SAMPLER_INFO + slot).x;
        const int width = MipExtent(texture, 0, mip);
        const int height = MipExtent(texture, 1, mip);
        const float coverage = BorderAxisCoverage(u, width, modes & 15, filtered) *
                               BorderAxisCoverage(v, height, (modes >> 4) & 15, filtered);
        const float at[2] = {filtered ? u : (std::floor(u * (float)width) + 0.5f) / (float)width,
                             filtered ? v : (std::floor(v * (float)height) + 0.5f) / (float)height};
        const float lod = (float)mip;
        float texel[4];
        TexelHash(CKJIT_OP_SAMPLE_LEVEL, (uint32_t)slot, CKJIT_SAMPLER_2D).Add(at, 2).Add(&lod, 1).Color(texel);
        return Lerp(Uniform(ROW_BORDER_COLOR + slot), {texel[0], texel[1], texel[2], texel[3]}, coverage);
    }

    Float4 Mips2D(int slot, float u, float v, float lod, bool filtered, bool blendBorder) const {
        const int levels = F.Textures[slot].Levels;
        const int mipFilter = (int)Uniform(ROW_SAMPLER_INFO + slot).w & 15;
        lod = mipFilter == 0 ? 0.0f : std::fmin(std::fmax(lod, 0.0f), (float)(levels - 1));
        const auto level = [&](int mip) {
            return blendBorder ? BorderLevel2D(slot, u, v, mip, filtered) : Level2D(slot, u, v, mip, filtered);
        };
        if (mipFilter != 2 && mipFilter != 7)
            return level(Truncate(std::floor(lod + 0.5f)));
        const float below = std::floor(lod);
        const int lower = Truncate(below);
        const int upper = lower + 1 < levels - 1 ? lower + 1 : levels - 1;
        return Lerp(level(lower), level(upper), lod - below);
    }

    static float Length(float x, float y) {
        const float xx = x * x;
        const float yy = y * y;
        return std::sqrt(xx + yy);
    }

    // Anisotropic filtering: taps along the longer derivative, each at the
    // LOD of the shorter.
    Float4 Anisotropic2D(int slot, float u, float v, const float dx[2], const float dy[2], float tapBias,
                         float minMip, float maxAnisotropy, bool blendBorder) const {
        const Texture &texture = F.Textures[slot];
        const float width = (float)MipExtent(texture, 0, 0);
        const float height = (float)MipExtent(texture, 1, 0);
        const float lx = Length(dx[0] * width, dx[1] * height);
        const float ly = Length(dy[0] * width, dy[1] * height);
        const float major = std::fmax(lx, ly);
        const float tapLimit = std::fmax(maxAnisotropy, 1.0f);
        const float minor = std::fmax(std::fmin(lx, ly), major / tapLimit);
        const int taps = Truncate(std::fmin(std::fmax(std::ceil(major / std::fmax(minor, 1.0f)), 1.0f), tapLimit));
        const float *axis = lx > ly ? dx : dy;
        const float step[2] = {axis[0] / (float)taps, axis[1] / (float)taps};
        const float tapLod = std::fmax(std::log2(std::fmax(minor, 1.0f)) + tapBias, minMip);
        Float4 result = Splat(0.0f);
        for (int i = 0; i < taps; ++i) {
            const float offset = (float)i - (float)(taps - 1) * 0.5f;
            result = result + Mips2D(slot, u + offset * step[0], v + offset * step[1], tapLod, true, blendBorder);
        }
        return result / (float)taps;
    }

    bool Border2D(int slot) const {
        const int modes = (int)Uniform(ROW_SAMPLER_INFO + slot).x;
        return (modes & 15) == 4 || ((modes >> 4) & 15) == 4;
    }

    Float4 Sample2DBias(int slot, float u, float v, float lodBias, float minMip, float maxAnisotropy) const {
        if (!Border2D(slot))
            return SampleSlot(slot, CKJIT_SAMPLER_2D, {u, v, 0.0f, 0.0f}, lodBias);
        const float uv[2] = {u, v};
        const float lod = std::fmax(CalcLod(F.Textures[slot], uv, 2) + lodBias, minMip);
        const Float4 info = Uniform(ROW_SAMPLER_INFO + slot);
        const int filter = Truncate(lod > 0.0f ? info.y : info.z);
        if (filter == 7 && lod > 0.0f) {
            const float dx[2] = {Derivative(u, 0), Derivative(v, 0)};
            const float dy[2] = {Derivative(u, 1), Derivative(v, 1)};
            return Anisotropic2D(slot, u, v, dx, dy, lodBias, minMip, maxAnisotropy, false);
        }
        return Mips2D(slot, u, v, lod, filter != 1, false);
    }

    // Filters at the level of gradients, with a LOD bias they do not carry.
    Float4 Filter2D(int slot, float u, float v, const float dx[2], const float dy[2], float bias, float minMip,
                    float maxAnisotropy, bool blendBorder) const {
        const Texture &texture = F.Textures[slot];
        const float width = (float)MipExtent(texture, 0, 0);
        const float height = (float)MipExtent(texture, 1, 0);
        const float lx = Length(dx[0] * width, dx[1] * height);
        const float ly = Length(dy[0] * width, dy[1] * height);
        const float lod = std::fmax(std::log2(std::fmax(std::fmax(lx, ly), 0.000001f)) + bias, minMip);
        const Float4 info = Uniform(ROW_SAMPLER_INFO + slot);
        const int filter = Truncate(lod > 0.0f ? info.y : info.z);
        if (filter == 7 && lod > 0.0f)
            return Anisotropic2D(slot, u, v, dx, dy, bias, minMip, maxAnisotropy, blendBorder);
        return Mips2D(slot, u, v, lod, filter != 1, blendBorder);
    }

    Float4 Sample2DGrad(int slot, float u, float v, const float dx[2], const float dy[2], float minMip,
                        float maxAnisotropy) const {
        if (!Border2D(slot)) {
            const float uv[2] = {u, v};
            float texel[4];
            TexelHash(CKJIT_OP_SAMPLE_GRAD, (uint32_t)slot, CKJIT_SAMPLER_2D)
                .Add(uv, 2).Add(dx, 2).Add(dy, 2).Color(texel);
            return {texel[0], texel[1], texel[2], texel[3]};
        }
        return Filter2D(slot, u, v, dx, dy, 0.0f, minMip, maxAnisotropy, false);
    }

    static float CompareDepth(float depth, float reference, int func) {
        switch (func) {
        case 1: return reference < depth ? 1.0f : 0.0f;
        case 2: return reference <= depth ? 1.0f : 0.0f;
        case 3: return reference == depth ? 1.0f : 0.0f;
        case 4: return reference >= depth ? 1.0f : 0.0f;
        case 5: return reference > depth ? 1.0f : 0.0f;
        case 6: return reference != depth ? 1.0f : 0.0f;
        case 7: return 0.0f;
        case 8: return 1.0f;
        default: return depth;
        }
    }

    // depth_compare_sampling.hlsli, which the shader-sampling shaders of the
    // layouts without stage-indexed textures, and the comparison shaders with
    // explicit gradients, compare depths with: filters blend the comparisons
    // of texels, whose metadata rows are their ordinal's. A texture the layout
    // does not declare, or a comparison shader's comparison samplers do not
    // hold, is a single texel of the border depth.
    float CompareTap2D(int ordinal, bool declared, int x, int y, int mip, int width, int height, float reference,
                       int func) const {
        const int modes = (int)Uniform(ROW_SAMPLER_INFO + ordinal).x;
        bool outside = false;
        const int texel[3] = {Address(x, width, modes & 15, outside), Address(y, height, (modes >> 4) & 15, outside),
                              mip};
        float depth = Uniform(ROW_BORDER_COLOR + ordinal).x;
        if (!outside && declared) {
            float color[4];
            const CKJitSamplerDim dim = Comparisons != 0 ? CKJIT_SAMPLER_2D_COMPARE : CKJIT_SAMPLER_2D;
            TexelHash(CKJIT_OP_LOAD, (uint32_t)ordinal, dim).Add(texel, 3).Color(color);
            depth = color[0];
        }
        return CompareDepth(depth, reference, func);
    }

    float CompareLevel2D(int ordinal, bool declared, float u, float v, int mip, bool filtered, float reference,
                         int func) const {
        const int width = declared ? MipExtent(F.Textures[ordinal], 0, mip) : 1;
        const int height = declared ? MipExtent(F.Textures[ordinal], 1, mip) : 1;
        const float x = u * (float)width;
        const float y = v * (float)height;
        if (!filtered) {
            return CompareTap2D(ordinal, declared, Truncate(std::floor(x)), Truncate(std::floor(y)), mip, width, height,
                                reference, func);
        }
        const float cx = x - 0.5f;
        const float cy = y - 0.5f;
        const float bx = std::floor(cx);
        const float by = std::floor(cy);
        const int x0 = Truncate(bx);
        const int y0 = Truncate(by);
        const float wx = cx - bx;
        const float wy = cy - by;
        float result = 0.0f;
        for (int j = 0; j < 2; ++j) {
            for (int i = 0; i < 2; ++i) {
                const float tapWeight = (i != 0 ? wx : 1.0f - wx) * (j != 0 ? wy : 1.0f - wy);
                result += CompareTap2D(ordinal, declared, i != 0 ? Next(x0) : x0, j != 0 ? Next(y0) : y0, mip, width,
                                       height, reference, func) *
                          tapWeight;
            }
        }
        return result;
    }

    float CompareMips2D(int ordinal, bool declared, float u, float v, float lod, int levels, bool filtered,
                        float reference, int func) const {
        const int mipFilter = (int)Uniform(ROW_SAMPLER_INFO + ordinal).w & 15;
        lod = mipFilter == 0 ? 0.0f : std::fmin(std::fmax(lod, 0.0f), (float)(levels - 1));
        if (mipFilter != 2 && mipFilter != 7) {
            return CompareLevel2D(ordinal, declared, u, v, Truncate(std::floor(lod + 0.5f)), filtered, reference,
                                  func);
        }
        const float below = std::floor(lod);
        const int lower = Truncate(below);
        const int upper = lower + 1 < levels - 1 ? lower + 1 : levels - 1;
        const float weight = lod - below;
        float result = 0.0f;
        result += CompareLevel2D(ordinal, declared, u, v, lower, filtered, reference, func) * (1.0f - weight);
        result += CompareLevel2D(ordinal, declared, u, v, upper, filtered, reference, func) * weight;
        return result;
    }

    float CompareSample2D(int stage, int ordinal, float u, float v, const float dx[2], const float dy[2],
                          float lodBias, float reference, int func) const {
        const bool declared = Comparisons != 0
                                  ? ordinal < Comparisons
                                  : (CKDWORD)ordinal < CKFFSamplerTypeSlotCount(CKFF_SAMPLER_DEPTH, Layout);
        const Float4 info = Uniform(ROW_SAMPLER_INFO + ordinal);
        const float maxAnisotropy = std::fmax(1.0f, (float)((int)info.w >> 4));
        const float minMip = (float)((int)BumpEnv(stage * 2 + 1).w & 31);
        const int levels = declared ? F.Textures[ordinal].Levels : 1;
        const float width = declared ? (float)MipExtent(F.Textures[ordinal], 0, 0) : 1.0f;
        const float height = declared ? (float)MipExtent(F.Textures[ordinal], 1, 0) : 1.0f;
        const float lx = Length(dx[0] * width, dx[1] * height);
        const float ly = Length(dy[0] * width, dy[1] * height);
        const float lod = std::fmax(std::log2(std::fmax(std::fmax(lx, ly), 0.000001f)) + lodBias, minMip);
        const int filter = Truncate(lod > 0.0f ? info.y : info.z);
        if (filter == 7 && maxAnisotropy > 1.0f && lod > 0.0f) {
            const float major = std::fmax(lx, ly);
            const float minor = std::fmax(std::fmin(lx, ly), major / maxAnisotropy);
            const int taps =
                Truncate(std::fmin(std::fmax(std::ceil(major / std::fmax(minor, 1.0f)), 1.0f), maxAnisotropy));
            const float *axis = lx > ly ? dx : dy;
            const float step[2] = {axis[0] / (float)taps, axis[1] / (float)taps};
            const float tapLod = std::fmax(std::log2(std::fmax(minor, 1.0f)) + lodBias, minMip);
            float result = 0.0f;
            for (int i = 0; i < taps; ++i) {
                const float offset = (float)i - (float)(taps - 1) * 0.5f;
                result += CompareMips2D(ordinal, declared, u + offset * step[0], v + offset * step[1], tapLod, levels,
                                        true, reference, func);
            }
            return result / (float)taps;
        }
        return CompareMips2D(ordinal, declared, u, v, lod, levels, filter != 1, reference, func);
    }

    // A comparison shader's hardware comparison, with the function of its
    // comparison sampler.
    float SampleCmp(int slot, float u, float v, float reference) const {
        const float uv[2] = {u, v};
        float texel[4];
        TexelHash(CKJIT_OP_SAMPLE_CMP, (uint32_t)slot, CKJIT_SAMPLER_2D_COMPARE)
            .Add(uv, 2)
            .Add(&reference, 1)
            .Color(texel);
        return texel[0];
    }

    // The shader-sampling shaders' 2D and depth textures: mirror-once folds
    // the coordinate, and explicit gradients are of the unfolded one. The
    // layouts without stage-indexed textures compare the texels of depth
    // textures, even undeclared ones, and so do the comparison shaders with
    // explicit gradients; without, they compare in hardware, and only the
    // depths of their comparison samplers. The comparison shaders' textures
    // past their comparison samplers are ckCompareVariantSample2D*, whose
    // border-addressed levels blend the border colour into hardware samples,
    // at the level of the folded coordinate's derivatives with the LOD bias.
    Float4 ShaderSample2D(int stage, const Float4 &coord, bool depth, int ordinal, int flags, int compareFunc,
                          float lodBias) const {
        const bool wide = Layout == CKFF_SAMPLER_LAYOUT_WIDE_2D;
        const int mirror = (flags >> 9) & 7;
        const float u = (mirror & 1) != 0 ? Clamp(std::fabs(coord.x), 0.0f, 1.0f) : coord.x;
        const float v = (mirror & 2) != 0 ? Clamp(std::fabs(coord.y), 0.0f, 1.0f) : coord.y;
        const bool compareVariant = wide && Comparisons != 0;
        const int state = (int)BumpEnv(stage * 2 + 1).w;
        if (depth && compareFunc != 0 && (!wide || compareVariant)) {
            if (compareVariant && (state & 0x8000) == 0)
                return Splat(ordinal < Comparisons ? SampleCmp(ordinal, u, v, coord.z) : 0.0f);
            const float dx[2] = {Derivative(coord.x, 0), Derivative(coord.y, 0)};
            const float dy[2] = {Derivative(coord.x, 1), Derivative(coord.y, 1)};
            return Splat(CompareSample2D(stage, ordinal, u, v, dx, dy, lodBias, coord.z, compareFunc));
        }
        if (!wide && ordinal >= 4)
            return Splat(0.0f);
        if (compareVariant && (ordinal < Comparisons || ordinal >= 8))
            return Splat(0.0f);
        const int slot = wide && !compareVariant ? stage : ordinal;
        const float minMip = (float)(state & 31);
        const float maxAnisotropy = (float)((state >> 5) & 31);
        const bool blendBorder = compareVariant && Border2D(slot);
        Float4 color;
        if ((state & 0x8000) != 0) {
            const float scale = std::exp2(lodBias);
            const float dx[2] = {Derivative(coord.x, 0) * scale, Derivative(coord.y, 0) * scale};
            const float dy[2] = {Derivative(coord.x, 1) * scale, Derivative(coord.y, 1) * scale};
            color = blendBorder ? Filter2D(slot, u, v, dx, dy, 0.0f, minMip, maxAnisotropy, true)
                                : Sample2DGrad(slot, u, v, dx, dy, minMip, maxAnisotropy);
        } else if (blendBorder) {
            const float dx[2] = {Derivative(u, 0), Derivative(v, 0)};
            const float dy[2] = {Derivative(u, 1), Derivative(v, 1)};
            color = Filter2D(slot, u, v, dx, dy, lodBias, minMip, maxAnisotropy, true);
        } else {
            color = Sample2DBias(slot, u, v, lodBias, minMip, maxAnisotropy);
        }
        if (!depth)
            return color;
        return Splat(compareFunc != 0 ? CompareDepth(color.x, coord.z, compareFunc) : color.x);
    }

    Float4 SampleLevel3D(int slot, const float uvw[3], float lod) const {
        float texel[4];
        TexelHash(CKJIT_OP_SAMPLE_LEVEL, (uint32_t)slot, CKJIT_SAMPLER_3D).Add(uvw, 3).Add(&lod, 1).Color(texel);
        return {texel[0], texel[1], texel[2], texel[3]};
    }

    Float4 Tap3D(int slot, const int p[3], int mip, const int extent[3]) const {
        const int modes = (int)Uniform(ROW_SAMPLER_INFO + slot).x;
        bool outside = false;
        const int texel[4] = {Address(p[0], extent[0], modes & 15, outside),
                              Address(p[1], extent[1], (modes >> 4) & 15, outside),
                              Address(p[2], extent[2], (modes >> 8) & 15, outside), mip};
        if (outside)
            return Uniform(ROW_BORDER_COLOR + slot);
        float color[4];
        TexelHash(CKJIT_OP_LOAD, (uint32_t)slot, CKJIT_SAMPLER_3D).Add(texel, 4).Color(color);
        return {color[0], color[1], color[2], color[3]};
    }

    Float4 Level3D(int slot, const float uvw[3], int mip, bool filtered) const {
        const Texture &texture = F.Textures[slot];
        int extent[3];
        float scaled[3];
        for (int axis = 0; axis < 3; ++axis) {
            extent[axis] = MipExtent(texture, (uint32_t)axis, mip);
            scaled[axis] = uvw[axis] * (float)extent[axis];
        }
        if (!filtered) {
            const int p[3] = {Truncate(std::floor(scaled[0])), Truncate(std::floor(scaled[1])),
                              Truncate(std::floor(scaled[2]))};
            return Tap3D(slot, p, mip, extent);
        }
        int base[3];
        float weight[3];
        for (int axis = 0; axis < 3; ++axis) {
            const float corner = scaled[axis] - 0.5f;
            const float below = std::floor(corner);
            base[axis] = Truncate(below);
            weight[axis] = corner - below;
        }
        Float4 value = Splat(0.0f);
        for (int z = 0; z < 2; ++z) {
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x) {
                    const int p[3] = {x ? Next(base[0]) : base[0], y ? Next(base[1]) : base[1],
                                      z ? Next(base[2]) : base[2]};
                    const Float4 tap = Tap3D(slot, p, mip, extent) * Splat(x ? weight[0] : 1.0f - weight[0]) *
                                       Splat(y ? weight[1] : 1.0f - weight[1]) *
                                       Splat(z ? weight[2] : 1.0f - weight[2]);
                    value = value + tap;
                }
            }
        }
        return value;
    }

    // The weight of the texels a border-addressed axis filters that are
    // inside the mip.
    static float BorderAxisCoverage(float uv, int extent, int mode, bool filtered) {
        if (mode != 4)
            return 1.0f;
        if (!filtered)
            return uv >= 0.0f && uv < 1.0f ? 1.0f : 0.0f;
        const float coord = uv * (float)extent - 0.5f;
        const float below = std::floor(coord);
        const int base = Truncate(below);
        const float fraction = coord - below;
        return (base >= 0 && base < extent ? 1.0f - fraction : 0.0f) +
               (Next(base) >= 0 && Next(base) < extent ? fraction : 0.0f);
    }

    Float4 BorderLevel3D(int slot, const float uvw[3], int mip, bool filtered) const {
        const Texture &texture = F.Textures[slot];
        const int modes = (int)Uniform(ROW_SAMPLER_INFO + slot).x;
        const float coverage = BorderAxisCoverage(uvw[0], MipExtent(texture, 0, mip), modes & 15, filtered) *
                               BorderAxisCoverage(uvw[1], MipExtent(texture, 1, mip), (modes >> 4) & 15, filtered) *
                               BorderAxisCoverage(uvw[2], MipExtent(texture, 2, mip), (modes >> 8) & 15, filtered);
        return Lerp(Uniform(ROW_BORDER_COLOR + slot), SampleLevel3D(slot, uvw, (float)mip), coverage);
    }

    // ckSample3DAtLod, whose levels filter as the level given asks, and
    // ckSample3DBorderLod, whose levels blend the border colour in and filter
    // as the level picked asks.
    Float4 Mips3D(int slot, const float uvw[3], float lod, bool blendBorder) const {
        const Float4 info = Uniform(ROW_SAMPLER_INFO + slot);
        const int levels = F.Textures[slot].Levels;
        const int mipFilter = (int)info.w & 15;
        bool filtered = (lod > 0.0f ? info.y : info.z) != 1.0f;
        lod = mipFilter == 0 ? 0.0f : std::fmin(std::fmax(lod, 0.0f), (float)(levels - 1));
        if (blendBorder)
            filtered = (lod > 0.0f ? info.y : info.z) != 1.0f;
        const auto level = [&](int mip) {
            return blendBorder ? BorderLevel3D(slot, uvw, mip, filtered) : Level3D(slot, uvw, mip, filtered);
        };
        if (mipFilter != 2 && mipFilter != 7)
            return level(Truncate(std::floor(lod + 0.5f)));
        const float below = std::floor(lod);
        const int lower = Truncate(below);
        const int upper = lower + 1 < levels - 1 ? lower + 1 : levels - 1;
        return Lerp(level(lower), level(upper), lod - below);
    }

    bool Border3D(int slot) const {
        const int modes = (int)Uniform(ROW_SAMPLER_INFO + slot).x;
        return (modes & 15) == 4 || ((modes >> 4) & 15) == 4 || ((modes >> 8) & 15) == 4;
    }

    static float Length(const float v[3], const float size[3]) {
        const float x = v[0] * size[0];
        const float y = v[1] * size[1];
        const float z = v[2] * size[2];
        const float xx = x * x;
        const float yy = y * y;
        const float zz = z * z;
        return std::sqrt(xx + yy + zz);
    }

    // ckffNative3DAniso: taps along the longer gradient across its footprint.
    Float4 Anisotropic3D(int slot, const float uvw[3], const float dx[3], const float dy[3], float lodBias,
                         float minMip, float maxAnisotropy) const {
        const Texture &texture = F.Textures[slot];
        const float size[3] = {(float)MipExtent(texture, 0, 0), (float)MipExtent(texture, 1, 0),
                               (float)MipExtent(texture, 2, 0)};
        const float footprintX = Length(dx, size);
        const float footprintY = Length(dy, size);
        const float major = std::fmax(footprintX, footprintY);
        const float minor = std::fmin(footprintX, footprintY);
        const float count = major <= 1.0f ? 1.0f
                                          : std::fmin(maxAnisotropy,
                                                      std::fmax(1.0f, std::ceil(major / std::fmax(minor, 0.0001f))));
        const float lod = std::fmax(std::log2(std::fmax(std::fmax(major / count, minor), 0.000001f)) + lodBias, minMip);
        const float *step = footprintX >= footprintY ? dx : dy;
        const bool border = Border3D(slot);
        Float4 color = Splat(0.0f);
        for (int tap = 0; tap < Truncate(count); ++tap) {
            const float offset = ((float)tap + 0.5f) / count - 0.5f;
            const float at[3] = {uvw[0] + step[0] * offset, uvw[1] + step[1] * offset, uvw[2] + step[2] * offset};
            color = color + (border ? Mips3D(slot, at, lod, true) : SampleLevel3D(slot, at, lod));
        }
        return color / count;
    }

    // The shader-sampling shaders' volume textures (ff_sampler_common.sc):
    // mirror-once folds the coordinate, and anisotropic samplers average taps
    // along the longer explicit gradient of the unfolded one. The others
    // sample at the level of the unfolded coordinate, no lower than the
    // minimum mip level.
    Float4 ShaderSample3D(int stage, const Float4 &coord, int slot, int flags, float lodBias) const {
        const int state = (int)BumpEnv(stage * 2 + 1).w;
        const float minMip = (float)(state & 31);
        const float maxAnisotropy = (float)((state >> 5) & 31);
        const int mirror = (flags >> 9) & 7;
        const float uvw[3] = {(mirror & 1) != 0 ? Clamp(std::fabs(coord.x), 0.0f, 1.0f) : coord.x,
                              (mirror & 2) != 0 ? Clamp(std::fabs(coord.y), 0.0f, 1.0f) : coord.y,
                              (mirror & 4) != 0 ? Clamp(std::fabs(coord.z), 0.0f, 1.0f) : coord.z};
        if ((state & 0x20000) != 0) {
            const bool explicitGradient = (state & 0x8000) != 0;
            const float dx[3] = {explicitGradient ? Derivative(coord.x, 0) : 0.0f,
                                 explicitGradient ? Derivative(coord.y, 0) : 0.0f,
                                 explicitGradient ? Derivative(coord.z, 0) : 0.0f};
            const float dy[3] = {explicitGradient ? Derivative(coord.x, 1) : 0.0f,
                                 explicitGradient ? Derivative(coord.y, 1) : 0.0f,
                                 explicitGradient ? Derivative(coord.z, 1) : 0.0f};
            return Anisotropic3D(slot, uvw, dx, dy, lodBias, minMip, maxAnisotropy);
        }
        const bool border = Border3D(slot);
        if (mirror == 0 && minMip <= 0.0f && !border)
            return SampleSlot(slot, CKJIT_SAMPLER_3D, {uvw[0], uvw[1], uvw[2], 0.0f}, lodBias);
        const float original[3] = {coord.x, coord.y, coord.z};
        const float lod = std::fmax(CalcLod(F.Textures[slot], original, 3) + lodBias, minMip);
        return border ? Mips3D(slot, uvw, lod, false) : SampleLevel3D(slot, uvw, lod);
    }

    Float4 SampleTexture(int stage, const Float4 &coord, int samplerType, int samplerOrdinal, bool hasTexture,
                         int flags, int compareFunc) const {
        if (!hasTexture)
            return {0.0f, 0.0f, 0.0f, 1.0f};
        const float lodBias = BumpEnv(stage * 2 + 1).z;
        if (ShaderSampling && (samplerType == 0 || samplerType == 2))
            return ShaderSample2D(stage, coord, samplerType == 2, samplerOrdinal, flags, compareFunc, lodBias);
        if (ShaderSampling && samplerType == 3 &&
            (CKDWORD)samplerOrdinal < CKFFSamplerTypeSlotCount(CKFF_SAMPLER_VOLUME, Layout)) {
            const int slot = (int)CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, (CKDWORD)samplerOrdinal, Layout);
            return ShaderSample3D(stage, coord, slot, flags, lodBias);
        }
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
            Float4 texColor = SampleTexture(stage, sampleCoord, samplerType, samplerOrdinal, hasTexture, flags,
                                            (alphaWord >> 20) & 0xf);
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

// A texture of powers of two or not, with some or all of its mips.
void RandomTexture(Random &random, Texture &texture) {
    int32_t largest = 1;
    for (int32_t &extent : texture.Size) {
        extent = random.OneIn(4) ? 1 + (int32_t)random.Below(256) : 1 << random.Below(9);
        largest = extent > largest ? extent : largest;
    }
    int32_t chain = 1;
    while (largest >> chain != 0)
        ++chain;
    texture.Levels = random.OneIn(2) ? chain : 1 + (int32_t)random.Below((uint32_t)chain);
}

void RandomFragment(Random &random, Fragment &fragment) {
    for (Texture &texture : fragment.Textures)
        RandomTexture(random, texture);
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

// The samplers the runtime resolves for a draw's slots: the metadata rows of
// the emulated sampler state, and the state word of the stages sampling them.
struct DrawSamplers {
    float Border[CKFF_SAMPLER_SLOT_COUNT][4];
    float Info[CKFF_SAMPLER_SLOT_COUNT][4];
    int32_t State[CKFF_SAMPLER_SLOT_COUNT];
};

void RandomSamplers(Random &random, CKFFSamplerLayout layout, DrawSamplers &samplers) {
    const CKDWORD volumeBase = CKFFSamplerTypeSlotBase(CKFF_SAMPLER_VOLUME, layout);
    for (CKDWORD slot = 0; slot < CKFF_SAMPLER_SLOT_COUNT; ++slot) {
        const bool volume = slot >= volumeBase;
        int modes[3];
        for (int &mode : modes)
            mode = 1 + (int)random.Below(5);
        const int mipFilter = (int)random.Below(8);
        const int anisotropy = (int)random.Below(random.OneIn(3) ? 2 : 32);
        float *info = samplers.Info[slot];
        info[0] = (float)(modes[0] | modes[1] << 4 | modes[2] << 8);
        info[1] = random.OneIn(3) ? 7.0f : (float)(1 + random.Below(7)); // minification filter
        info[2] = (float)(1 + random.Below(7));                          // magnification filter
        info[3] = (float)(mipFilter | anisotropy << 4);
        for (float &channel : samplers.Border[slot])
            channel = (float)random.Below(256) / 255.0f;

        const int minMip = random.OneIn(2) ? 0 : (int)random.Below(32);
        const bool manualAnisotropy = anisotropy >= 2;
        const bool explicitGradient = manualAnisotropy || random.OneIn(2);
        const int borderAxes = (modes[0] == 4 ? 1 : 0) | (modes[1] == 4 ? 2 : 0) | (volume && modes[2] == 4 ? 4 : 0);
        const int ignored = (int)(random.Next() & (CKFF_SAMPLER_SHADER_MIN_FILTER_LINEAR |
                                                   CKFF_SAMPLER_SHADER_MAG_FILTER_LINEAR | CKFF_SAMPLER_SHADER_MANUAL_LOD |
                                                   CKFF_SAMPLER_SHADER_MANUAL_BORDER |
                                                   CKFF_SAMPLER_SHADER_MANUAL_DEPTH_COMPARE));
        samplers.State[slot] = (int32_t)(minMip << CKFF_SAMPLER_SHADER_MIN_MIP_SHIFT |
                                         anisotropy << CKFF_SAMPLER_SHADER_ANISOTROPY_SHIFT |
                                         borderAxes << CKFF_SAMPLER_SHADER_BORDER_AXIS_SHIFT |
                                         (explicitGradient ? CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT : 0u) |
                                         (manualAnisotropy ? CKFF_SAMPLER_SHADER_MANUAL_ANISOTROPY : 0u) | ignored);
    }
}

// The sampler slot of a stage's texture. Nothing samples a texture the
// layout does not declare, whose stage may have any sampler's state. The
// comparison samplers of a comparison shader hold the first 2D textures, the
// depth textures it compares.
CKDWORD StageSlot(const CKFFFragmentProgram &program, CKDWORD stage, CKFFSamplerLayout layout,
                  CKDWORD comparisons = 0) {
    const CKDWORD type = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
    const bool texture2D = type == CKFF_SAMPLER_2D || type == CKFF_SAMPLER_DEPTH;
    const bool stageIndexed = texture2D && layout == CKFF_SAMPLER_LAYOUT_WIDE_2D && comparisons == 0;
    const CKDWORD index = stageIndexed ? stage : program.GetSamplerOrdinal(stage);
    bool declared = index < CKFFSamplerTypeSlotCount(type, layout);
    if (comparisons != 0 && texture2D) {
        const bool compared = type == CKFF_SAMPLER_DEPTH &&
                              program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC) != 0;
        declared = declared && (compared ? index < comparisons : index >= comparisons);
    }
    return declared ? CKFFSamplerSlot(type, index, layout) : stage;
}

// Gives a fragment a draw's samplers: its stages' state words are those of
// the samplers they sample, if they are textured.
void ApplySamplers(const CKFFFragmentProgram &program, CKFFSamplerLayout layout, const DrawSamplers &samplers,
                   Fragment &fragment, CKDWORD comparisons = 0) {
    std::memcpy(fragment.Uniforms[ROW_BORDER_COLOR], samplers.Border, sizeof(samplers.Border));
    std::memcpy(fragment.Uniforms[ROW_SAMPLER_INFO], samplers.Info, sizeof(samplers.Info));
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        const CKDWORD type = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
        const CKDWORD slot = StageSlot(program, stage, layout, comparisons);
        const float *coord = fragment.Uniforms[ROW_STAGE_PARAMS + stage * 2];
        fragment.Uniforms[ROW_BUMP_ENV + stage * 2 + 1][3] = coord[2] > 0.5f ? (float)samplers.State[slot] : 0.0f;
    }
}

// Gives the depth stages of a fragment their border's depth as the reference
// depth, which comparisons of border texels then meet exactly unless the
// coordinate is projected or affine. Outside the wide 2D layout's shaders
// with stage-indexed textures, the border is the one of the stage's ordinal,
// even if the layout does not declare it.
void MeetBorderDepths(const CKFFFragmentProgram &program, CKFFSamplerLayout layout, const DrawSamplers &samplers,
                      Fragment &fragment, CKDWORD comparisons = 0) {
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        if (program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE) != CKFF_SAMPLER_DEPTH)
            continue;
        const CKDWORD slot = layout == CKFF_SAMPLER_LAYOUT_WIDE_2D && comparisons == 0
                                 ? StageSlot(program, stage, layout)
                                 : program.GetSamplerOrdinal(stage);
        fragment.Registers[REG_TEXCOORD0 + stage][2] = samplers.Border[slot][0];
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
                              bool shaderSampling = false, CKDWORD comparisons = 0) {
    CKFFConstantSet constants;
    constants.Set(CKRST_BLOCK_DRAW_PARAMS, fragment.Uniforms[ROW_DRAW_PARAMS], (ROW_BUMP_ENV - ROW_DRAW_PARAMS) * 16);
    constants.Set(CKRST_BLOCK_BUMP_ENV, fragment.Uniforms[ROW_BUMP_ENV], (ROW_STAGE_PARAMS - ROW_BUMP_ENV) * 16);
    constants.Set(CKRST_BLOCK_STAGE_PARAMS, fragment.Uniforms[ROW_STAGE_PARAMS], (ROW_PROGRAM - ROW_STAGE_PARAMS) * 16);
    return CKFFNativeFragmentDrawKey(program, constants, shaderSampling, comparisons);
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
    const CKDWORD comparisons = layout == CKFF_SAMPLER_LAYOUT_WIDE_2D
        ? (key.Switches[0] & CKFF_NATIVE_FRAGMENT_COMPARISONS) >> CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT
        : 0;
    const bool gradient = (sampling & CKFF_NATIVE_FRAGMENT_GRADIENT) != 0;
    if (type != CKFF_SAMPLER_DEPTH) {
        Scramble(program, stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, random);
    } else if (comparisons != 0) {
        // The comparison shaders compare in hardware with the sampler's
        // function without explicit gradients, and compare every texel with
        // them, passing depths through for every unknown function.
        if (func != 0) {
            if (!gradient && random.OneIn(2))
                program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, 1 + random.Below(15));
            else if (func > 8 && random.OneIn(2))
                program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, 9 + random.Below(7));
            ignored |= CKFF_NATIVE_FRAGMENT_BORDER;
            if (!gradient)
                Scramble(key.Switches[0], CKFF_NATIVE_FRAGMENT_LOD_BIAS << stage, random);
        }
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
    // A comparison shader's 2D or depth texture it does not declare samples
    // nothing, like one of any other ordinal it does not declare; explicit
    // gradients would compare the texels of a depth texture.
    if (comparisons != 0 && (type == CKFF_SAMPLER_2D || type == CKFF_SAMPLER_DEPTH)) {
        const CKDWORD ordinal = program.GetSamplerOrdinal(stage);
        const bool compared = type == CKFF_SAMPLER_DEPTH && func != 0;
        const bool samples = compared ? gradient || ordinal < comparisons : ordinal >= comparisons;
        if (!samples) {
            ignored = compared ? 0xFFu & ~(CKDWORD)CKFF_NATIVE_FRAGMENT_GRADIENT : 0xFFu;
            Scramble(program, stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED, random);
            Scramble(key.Switches[0], CKFF_NATIVE_FRAGMENT_LOD_BIAS << stage, random);
            if (random.OneIn(2)) {
                program.SetSamplerOrdinal(stage, compared ? comparisons + random.Below(8 - comparisons)
                                                          : random.Below(comparisons));
            }
        }
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
// u_ffProgram, the line and affine modes, or the texture coordinate
// parameters.
bool ReadsOnlyOpenUniforms(const CKJitFragmentShader &shader) {
    for (int n = 0; n < shader.Nodes.Size(); ++n) {
        const CKJitNode &node = shader.Nodes[n];
        if (node.Op != CKJIT_OP_UNIFORM)
            continue;
        const uint32_t row = Natives().Row(node.Imm[1], node.Imm[0]);
        if ((row >= ROW_PROGRAM && row < ROW_BORDER_COLOR) || row == ROW_DRAW_PARAMS + 4 ||
            (row >= ROW_STAGE_PARAMS && row < ROW_PROGRAM && (row - ROW_STAGE_PARAMS) % 2 == 0)) {
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

// Shader-sampling draws: their keys hold the sampling of the samplers the
// runtime resolves, whose emulated state the shaders read.
void SetSelectArg1(CKFFFragmentProgram &program, CKDWORD stage, CKDWORD arg) {
    const CKDWORD packed = CKFFFragmentProgram::RepackArg(arg);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP, CKRST_TOP_SELECTARG1);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1, packed);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP, CKRST_TOP_SELECTARG1);
    program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1, packed);
}

// Compiles the keys of draws of a program, and their canonical keys, and
// checks that they compute what the layout's shader-sampling shader does, or
// its comparison shader with the comparison samplers given.
void MatchShaderSampling(Random &random, CKFFSamplerLayout layout, const CKFFFragmentProgram &program,
                         CKDWORD comparisons = 0) {
    const int kDraws = 2;
    const int kFragments = 3;
    for (int d = 0; d < kDraws; ++d) {
        DrawSamplers samplers;
        RandomSamplers(random, layout, samplers);
        Fragment fragments[kFragments];
        for (int f = 0; f < kFragments; ++f) {
            RandomFragment(random, fragments[f]);
            if (f != 0)
                ShareDrawState(fragments[0], fragments[f], random);
            ApplySamplers(program, layout, samplers, fragments[f], comparisons);
            if (random.OneIn(2))
                MeetBorderDepths(program, layout, samplers, fragments[f], comparisons);
        }
        const CKFFNativeFragmentKey key = DrawKey(program, fragments[0], true, comparisons);
        for (int f = 1; f < kFragments; ++f) {
            TestCheck(DrawKey(program, fragments[f], true, comparisons) == key,
                      "draws sharing the state keys read share keys");
        }

        const CKFFNativeFragmentKey keys[] = {key, Canonical(key, layout)};
        TestCheck(Canonical(keys[1], layout) == keys[1], "canonical keys are left unchanged");
        for (const CKFFNativeFragmentKey &compiled : keys) {
            CKJitFragmentShader shader;
            TestCheck(Compile(compiled, layout, shader), "every key compiles");
            TestCheck(ReadsOnlyOpenUniforms(shader), "shaders never read the state their key decides");
            for (const Fragment &fragment : fragments) {
                const Outcome expected =
                    Reference{fragment, layout, true, (int)comparisons}.Evaluate(program.Lanes());
                const Outcome actual = Execute(shader, fragment);
                if (!SameOutcome(expected, actual)) {
                    Report(layout, compiled, shader, expected, actual);
                    TestFail("compiled keys compute what the shader-sampling shader computes");
                }
            }
        }
    }
}

void TestMatchesShaderSampling() {
    const int kPrograms = 1000;
    Random random(0x9e0c2d4b37ull);
    for (CKFFSamplerLayout layout : kLayouts) {
        for (int p = 0; p < kPrograms; ++p)
            MatchShaderSampling(random, layout, RandomProgram(random, p % 4 == 0));
    }
}

// The wide 2D layout's comparison shaders sample the 2D and depth textures
// past their comparison samplers, and none before.
void TestMatchesComparisonShaders() {
    const int kPrograms = 250;
    Random random(0x2c6d1e93a5ull);
    for (CKDWORD comparisons = 1; comparisons <= 8; ++comparisons) {
        for (int p = 0; p < kPrograms; ++p) {
            CKFFFragmentProgram program = RandomProgram(random, p % 4 == 0);
            MatchShaderSampling(random, CKFF_SAMPLER_LAYOUT_WIDE_2D, program, comparisons);
        }
    }
}

// Random programs compare depths too rarely to reach every corner of the
// comparisons of the layouts without stage-indexed textures, and of the
// comparison shaders; these programs compare at every stage.
void TestComparesDepths() {
    const int kPrograms = 500;
    Random random(0x51c3e8a07dull);
    for (CKFFSamplerLayout layout : kLayouts) {
        if (layout == CKFF_SAMPLER_LAYOUT_WIDE_2D)
            continue;
        for (int p = 0; p < kPrograms; ++p) {
            CKFFFragmentProgram program = RandomProgram(random, p % 4 == 0);
            for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
                program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_DEPTH);
                program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, 1 + random.Below(15));
            }
            MatchShaderSampling(random, layout, program);
        }
    }
    for (CKDWORD comparisons = 1; comparisons <= 8; ++comparisons) {
        for (int p = 0; p < kPrograms / 4; ++p) {
            CKFFFragmentProgram program = RandomProgram(random, p % 4 == 0);
            for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
                program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_DEPTH);
                program.SetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, 1 + random.Below(15));
            }
            MatchShaderSampling(random, CKFF_SAMPLER_LAYOUT_WIDE_2D, program, comparisons);
        }
    }

    // Without a maximum anisotropy above one, an anisotropic filter takes a
    // single tap at the level of the gradients, not at the level of an
    // anisotropic tap, which gradients shorter than a texel that the LOD
    // bias makes minify would raise to the bias. A stage selecting the
    // comparison shows it.
    const int kFragments = 64;
    CKFFFragmentProgram program;
    SetSelectArg1(program, 0, CKRST_TA_TEXTURE);
    program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_DEPTH);
    program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, 2);
    program.Set(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE, 0);
    for (CKFFSamplerLayout layout : {CKFF_SAMPLER_LAYOUT_WIDE_CUBE, CKFF_SAMPLER_LAYOUT_WIDE_VOLUME}) {
        for (int f = 0; f < kFragments; ++f) {
            // The first depth texture, of slot and ordinal 0: eight texels
            // square with all its mips, filtered anisotropically when
            // minified, with linear mips and from the base mip.
            DrawSamplers samplers;
            RandomSamplers(random, layout, samplers);
            samplers.Info[0][1] = 7.0f;
            samplers.Info[0][3] = (float)(2 | (int)random.Below(2) << 4);
            samplers.State[0] = 0;
            Fragment fragment;
            RandomFragment(random, fragment);
            fragment.Textures[0] = {{8, 8, 1}, 4};
            fragment.Uniforms[ROW_STAGE_PARAMS][2] = 1.0f;                     // texture presence
            fragment.Uniforms[ROW_BUMP_ENV + 1][2] = random.Range(1.0f, 2.0f); // LOD bias
            ApplySamplers(program, layout, samplers, fragment);

            const CKFFNativeFragmentKey key = DrawKey(program, fragment, true);
            CKJitFragmentShader shader;
            TestCheck(Compile(key, layout, shader), "every key compiles");
            const Outcome expected = Reference{fragment, layout, true}.Evaluate(program.Lanes());
            const Outcome actual = Execute(shader, fragment);
            if (!SameOutcome(expected, actual)) {
                Report(layout, key, shader, expected, actual);
                TestFail("single anisotropic taps compare at the level of the gradients");
            }
        }
    }
}

// A comparison shader's draw key holds its comparison samplers, which its
// canonical key keeps only in the wide 2D layout, if it reads 2D or depth
// textures, whose slots and sampling they decide. It is otherwise the key of
// the shader-sampling shader.
void CheckComparisonKey(const CKFFFragmentProgram &program, const Fragment &fragment, CKFFSamplerLayout layout,
                        CKDWORD comparisons, const CKFFNativeFragmentKey &key,
                        const CKFFNativeFragmentKey &canonical) {
    TestCheck((key.Switches[0] & CKFF_NATIVE_FRAGMENT_COMPARISONS) ==
                  comparisons << CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT,
              "draw keys hold the comparison samplers");
    TestCheck(DrawKey(program, fragment, false, comparisons) == key, "comparison shaders sample in the shader");
    bool reads2D = false;
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        const CKDWORD type = canonical.Program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
        reads2D = reads2D || ((canonical.Switches[0] & (CKDWORD)CKFF_NATIVE_FRAGMENT_TEXTURE << stage) != 0 &&
                              (type == CKFF_SAMPLER_2D || type == CKFF_SAMPLER_DEPTH));
    }
    const bool kept = layout == CKFF_SAMPLER_LAYOUT_WIDE_2D && reads2D;
    TestCheck((canonical.Switches[0] & CKFF_NATIVE_FRAGMENT_COMPARISONS) ==
                  (kept ? comparisons << CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT : 0u),
              "canonical keys keep the comparison samplers of the wide 2D layout's 2D textures");
    if (!kept) {
        TestCheck(canonical == Canonical(DrawKey(program, fragment, true), layout),
                  "comparison samplers without 2D textures are the shader-sampling shader's");
        return;
    }
    TestCheck((canonical.Switches[0] & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) != 0,
              "comparison shaders are shader-sampling shaders");
    if (comparisons == 8) {
        TestCheck(Canonical(DrawKey(program, fragment, true, 15), layout) == canonical,
                  "comparison shaders have a comparison sampler per 2D texture at most");
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
            // Native, shader-sampling and comparison shader keys.
            for (int sampled = 0; sampled < 3; ++sampled) {
                const CKDWORD comparisons = sampled == 2 ? 1 + random.Below(8) : 0;
                const CKFFNativeFragmentKey key = DrawKey(program, fragment, sampled != 0, comparisons);
                const CKFFNativeFragmentKey canonical = Canonical(key, layout);
                TestCheck(Canonical(canonical, layout) == canonical, "canonical keys are left unchanged");
                for (int s = 0; s < kScrambles; ++s) {
                    CKFFNativeFragmentKey scrambled = key;
                    Scramble(scrambled, layout, random);
                    TestCheck(Canonical(scrambled, layout) == canonical, "canonical keys hold only what shaders read");
                }
                if (comparisons != 0) {
                    CheckComparisonKey(program, fragment, layout, comparisons, key, canonical);
                    continue;
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

// The expected boundary behavior is specified here independently of the key
// extractor. A nonzero bias's magnitude is uniform, while crossing zero is a
// specialization; inactive stage switches must disappear in canonical keys.
void TestStateDependencies() {
    const struct Dependency {
        const char *Name;
        int Row, Component;
        float A, B;
        bool RawChanges, CanonicalChanges;
    } dependencies[] = {
        {"affine below threshold", ROW_DRAW_PARAMS + 4, 2, 0.0f, 0.5f, false, false},
        {"affine threshold", ROW_DRAW_PARAMS + 4, 2, 0.5f, 0.50000006f, true, true},
        {"line below threshold", ROW_DRAW_PARAMS + 4, 3, 0.0f, 2.5f, false, false},
        {"line threshold", ROW_DRAW_PARAMS + 4, 3, 2.5f, 2.50000024f, true, true},
        {"texture below threshold", ROW_STAGE_PARAMS, 2, 0.0f, 0.5f, false, false},
        {"texture threshold", ROW_STAGE_PARAMS, 2, 0.5f, 0.50000006f, true, true},
        {"LOD signed zeros", ROW_BUMP_ENV + 1, 2, 0.0f, -0.0f, false, false},
        {"LOD zero boundary", ROW_BUMP_ENV + 1, 2, 0.0f, 1.0f, true, true},
        {"LOD nonzero magnitude", ROW_BUMP_ENV + 1, 2, 1.0f, 2.0f, false, false},
        {"LOD nonzero sign", ROW_BUMP_ENV + 1, 2, -1.0f, 1.0f, false, false},
        {"alpha reference", ROW_DRAW_PARAMS + 8, 0, 64.0f, 192.0f, false, false},
        {"texture factor", ROW_DRAW_PARAMS + 9, 0, 0.25f, 0.75f, false, false},
        {"fog bounds", ROW_DRAW_PARAMS + 10, 0, 0.25f, 0.75f, false, false},
        {"fog color", ROW_DRAW_PARAMS + 11, 2, 0.25f, 0.75f, false, false},
        {"stage constant", ROW_STAGE_PARAMS + 1, 1, 0.25f, 0.75f, false, false},
        {"bump matrix", ROW_BUMP_ENV, 0, 0.25f, 0.75f, false, false},
        {"bump luminance", ROW_BUMP_ENV + 1, 0, 0.25f, 0.75f, false, false},
        {"unused bump encoding", ROW_STAGE_PARAMS, 1, 0.0f, float(CKFF_TTF_BUMP_UNORM), true, false},
        {"unused STAGEBLEND factors", ROW_STAGE_PARAMS, 3, 0.0f, float(VXBLEND_ONE << 4), true, false},
        {"inactive texture presence", ROW_STAGE_PARAMS + 14, 2, 0.0f, 1.0f, true, false},
        {"inactive LOD bias", ROW_BUMP_ENV + 15, 2, 0.0f, 1.0f, true, false},
        {"inactive stage constant", ROW_STAGE_PARAMS + 15, 3, 0.25f, 0.75f, false, false},
    };
    CKFFFragmentProgram program;
    SetSelectArg1(program, 0, CKRST_TA_TEXTURE);
    program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_2D);
    for (CKFFSamplerLayout layout : kLayouts) {
        for (const auto &dependency : dependencies) {
            Fragment a = {}, b = {};
            a.Uniforms[ROW_STAGE_PARAMS][2] = b.Uniforms[ROW_STAGE_PARAMS][2] = 1;
            a.Uniforms[dependency.Row][dependency.Component] = dependency.A;
            b.Uniforms[dependency.Row][dependency.Component] = dependency.B;
            const auto ka = DrawKey(program, a), kb = DrawKey(program, b);
            const bool raw = (ka != kb) == dependency.RawChanges;
            const bool canonical = (Canonical(ka, layout) != Canonical(kb, layout)) == dependency.CanonicalChanges;
            if (!raw || !canonical) {
                std::printf("\nDependency %s, layout %u: raw changed=%d canonical changed=%d\n",
                            dependency.Name, unsigned(layout), ka != kb, Canonical(ka, layout) != Canonical(kb, layout));
                TestFail("constant changes obey the raw and canonical key dependency contract");
            }
        }
    }
    // Constants outside a supplied block read as zero, including partially
    // supplied rows. Non-key data must not accidentally select a switch.
    CKFFConstantSet empty, partial;
    const float data[] = {1.0f, 2.0f};
    partial.Set(CKRST_BLOCK_STAGE_PARAMS, data, sizeof(data));
    TestCheck(CKFFNativeFragmentDrawKey(program, empty, false, 0) ==
                  CKFFNativeFragmentDrawKey(program, partial, false, 0),
              "an incomplete row cannot select missing texture presence or blend factors");
}

void TestInterface() {
    for (CKFFSamplerLayout layout : kLayouts) {
        CKJitFragmentShader shader;
        TestCheck(Compile(CKFFNativeFragmentKey(), layout, shader), "the empty key compiles");
        const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, CKRST_SHADER_FORMAT_SPIRV, FALSE, FALSE, layout);
        uint32_t buffers = 0;
        bool sized = true;
        for (int i = 0; i < desc.UniformBuffers.Size(); ++i) {
            const CKFFUniformBufferBinding &binding = desc.UniformBuffers[i];
            if (binding.Stage != CKRST_SHADER_PIXEL)
                continue;
            ++buffers;
            sized = sized && binding.Slot < shader.UniformBufferCount &&
                    shader.UniformVec4Counts[binding.Slot] == binding.Size / 16u;
        }
        TestCheck(sized && buffers == shader.UniformBufferCount, "the uniform blocks are the native fragment buffers");
        TestCheck(Natives().Complete(), "the native buffers hold every fragment row once");
        TestCheck(shader.Inputs.Size() == REG_COUNT, "every native varying is declared");

        const CKJitInput &position = shader.Inputs[0];
        TestCheck(position.Kind == CKJIT_INPUT_FRAG_COORD && position.Components == 4, "the position comes first");
        for (uint32_t v = 0; v + 1 < REG_COUNT; ++v) {
            const CKJitInput &input = shader.Inputs[(int)v + 1];
            const bool flat = v + 1 == REG_FLAT_COLOR0 || v + 1 == REG_FLAT_COLOR1;
            TestCheck(input.Location == v, "varying v is at location v");
            TestCheck(input.Components == (v + 1 == REG_LINE_OFFSET ? 2 : 4), "varyings keep their widths");
            TestCheck(input.Kind == (flat ? CKJIT_INPUT_FLAT : CKJIT_INPUT_SMOOTH),
                      "only the flat colours are not interpolated");
        }
    }

    CKJitFragmentShader shader;
    TestCheck(!CKFFCompileNativeFragmentProgram(CKFFNativeFragmentKey(), CKFF_SAMPLER_LAYOUT_COUNT, shader),
              "layouts without native shaders are refused");
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
        CKDWORD Comparisons = 0; // of the comparison shader, which samples in the shader
    };
    const Case cases[] = {
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 5, CKFF_SAMPLER_2D, 0, 5, CKJIT_SAMPLER_2D},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 3, CKFF_SAMPLER_DEPTH, 6, 3, CKJIT_SAMPLER_2D},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 0, CKFF_SAMPLER_CUBE, 3, 11, CKJIT_SAMPLER_CUBE},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 0, CKFF_SAMPLER_CUBE, 4, -1, CKJIT_SAMPLER_CUBE},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 2, CKFF_SAMPLER_VOLUME, 1, 13, CKJIT_SAMPLER_3D},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 5, CKFF_SAMPLER_2D, 4, 4, CKJIT_SAMPLER_2D, 3},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 5, CKFF_SAMPLER_2D, 2, -1, CKJIT_SAMPLER_2D, 3},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 1, CKFF_SAMPLER_DEPTH, 7, 7, CKJIT_SAMPLER_2D, 7},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 6, CKFF_SAMPLER_DEPTH, 0, -1, CKJIT_SAMPLER_2D, 8},
        {CKFF_SAMPLER_LAYOUT_WIDE_2D, 0, CKFF_SAMPLER_CUBE, 3, 11, CKJIT_SAMPLER_CUBE, 8},
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
            if (c.Comparisons != 0) {
                key.Switches[0] |= c.Comparisons << CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT |
                                   CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;
            }
            TestCheck(Compile(key, c.Layout, shader), "the texture program compiles");
            const bool sampled = variant != 0 && c.Slot >= 0;

            int samples = 0;
            for (int n = 0; n < shader.Nodes.Size(); ++n) {
                const CKJitNode &node = shader.Nodes[n];
                if (node.Op == CKJIT_OP_UNIFORM) {
                    TestCheck(sampled && variant == 2 &&
                                  Natives().Row(node.Imm[1], node.Imm[0]) == ROW_BUMP_ENV + c.Stage * 2 + 1,
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

    // The shader-sampling shader takes the level of a bordered 2D texture
    // without explicit gradients from the hardware, and the comparison shaders
    // from the folded coordinate's derivatives, which the hardware's levels
    // of anisotropic samplers are not.
    CKFFNativeFragmentKey bordered;
    SetSelectArg1(bordered.Program, 0, CKRST_TA_TEXTURE);
    bordered.Program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_2D);
    bordered.Program.SetSamplerOrdinal(0, 3);
    bordered.Switches[3] = CKFF_NATIVE_FRAGMENT_BORDER;
    for (CKDWORD comparisons = 0; comparisons <= 3; comparisons += 3) {
        bordered.Switches[0] = CKFF_NATIVE_FRAGMENT_TEXTURE | comparisons << CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT |
                               CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;
        TestCheck(Compile(bordered, CKFF_SAMPLER_LAYOUT_WIDE_2D, shader), "the bordered program compiles");
        bool hardwareLevel = false;
        for (int n = 0; n < shader.Nodes.Size(); ++n)
            hardwareLevel = hardwareLevel || shader.Nodes[n].Op == CKJIT_OP_CALC_LOD;
        TestCheck(hardwareLevel == (comparisons == 0),
                  "only the shader-sampling shader takes levels from the hardware");
    }

    // Without explicit gradients, a comparison shader compares in hardware
    // the depths its comparison samplers hold, whatever the function and LOD
    // bias, and those of the other ordinals compare nothing.
    CKFFNativeFragmentKey compared;
    SetSelectArg1(compared.Program, 0, CKRST_TA_TEXTURE);
    compared.Program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE, CKFF_SAMPLER_DEPTH);
    compared.Program.SetStage(0, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC, 4);
    compared.Switches[0] = CKFF_NATIVE_FRAGMENT_TEXTURE | CKFF_NATIVE_FRAGMENT_LOD_BIAS |
                           3u << CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT | CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING;
    for (CKDWORD ordinal = 0; ordinal < 8; ++ordinal) {
        compared.Program.SetSamplerOrdinal(0, ordinal);
        TestCheck(Compile(compared, CKFF_SAMPLER_LAYOUT_WIDE_2D, shader), "the compared program compiles");
        int comparisons = 0;
        for (int n = 0; n < shader.Nodes.Size(); ++n) {
            const CKJitNode &node = shader.Nodes[n];
            TestCheck(node.Op != CKJIT_OP_UNIFORM, "hardware comparisons read no uniforms");
            if (node.Op != CKJIT_OP_SAMPLE_CMP)
                continue;
            ++comparisons;
            TestCheck(node.Imm[0] == ordinal && node.Imm[1] == CKJIT_SAMPLER_2D_COMPARE,
                      "a stage compares through the comparison sampler of its ordinal");
        }
        TestCheck(comparisons == (ordinal < 3 ? 1 : 0), "only the comparison samplers compare in hardware");
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
    const char *const suffixes[] = {"", "_sampling", "_compare"};
    for (CKFFSamplerLayout layout : kLayouts) {
        // The native, shader-sampling and, of the wide 2D layout, comparison
        // shaders.
        const int variants = layout == CKFF_SAMPLER_LAYOUT_WIDE_2D ? 3 : 2;
        for (int variant = 0; variant < variants; ++variant) {
            const bool shaderSampling = variant != 0;
            char name[64];
            std::snprintf(name, sizeof(name), "%s%s", kShaderNames[layout], suffixes[variant]);
            for (int p = 0; p < kPrograms; ++p) {
                CKFFFragmentProgram program = RandomProgram(random, p % 2 == 0);
                const CKDWORD comparisons = variant == 2 ? 1 + random.Below(8) : 0;
                Fragment fragment;
                RandomFragment(random, fragment);
                if (shaderSampling) {
                    DrawSamplers samplers;
                    RandomSamplers(random, layout, samplers);
                    ApplySamplers(program, layout, samplers, fragment, comparisons);
                }
                const CKFFNativeFragmentKey key =
                    Canonical(DrawKey(program, fragment, shaderSampling, comparisons), layout);
                CKJitFragmentShader shader;
                TestCheck(Compile(key, layout, shader), "every key compiles");
                XArray<uint32_t> spirv;
                TestCheck(CKJitEmitSpirv(shader, kLayout, spirv) && spirv.Size() > 0, "every program emits SPIR-V");
                if (g_ShaderDirectory && p < kSaved)
                    Save(name, p, "spv", spirv);
#if CKRE_ENABLE_DIRECTX
                XArray<uint32_t> dxbc;
                TestCheck(CKJitEmitDxbc(shader, kLayout, dxbc) && dxbc.Size() > 0, "every program emits DXBC");
                if (g_ShaderDirectory && p < kSaved)
                    Save(name, p, "dxbc", dxbc);
#endif
            }
        }
    }
}


} // namespace

int main(int argc, char **argv) {
#if defined(_MSC_VER) && defined(_M_IX86)
    // Shaders round every operation to float, as the interpreter does, but
    // x86 builds may evaluate the references' expressions on the x87, whose
    // intermediates are wider unless its precision is float's.
    unsigned int control;
    _controlfp_s(&control, _PC_24, _MCW_PC);
#endif
    if (argc > 1)
        g_ShaderDirectory = argv[1];
    TestFramework framework;
    framework.Run("interface", TestInterface);
    framework.Run("specialization", TestSpecialization);
    framework.Run("matches the uber shaders", TestMatchesUberShaders);
    framework.Run("matches the shader-sampling shaders", TestMatchesShaderSampling);
    framework.Run("matches the comparison shaders", TestMatchesComparisonShaders);
    framework.Run("compares depths as the shader-sampling shaders do", TestComparesDepths);
    framework.Run("canonical keys", TestCanonicalKeys);
    framework.Run("state dependency boundaries", TestStateDependencies);
    framework.Run("emission", TestEmission);
    return framework.ExitCode();
}
