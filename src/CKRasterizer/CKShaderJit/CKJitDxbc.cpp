#include "CKJitDxbc.h"

#include <cstring>
#include <initializer_list>

namespace {

// The subset of the shader model 5.1 token format and of the container the
// emitter uses.
enum : uint32_t {
    DxbcOpAdd = 0,
    DxbcOpAnd = 1,
    DxbcOpBreakc = 3,
    DxbcOpDiscard = 13,
    DxbcOpDiv = 14,
    DxbcOpDp2 = 15,
    DxbcOpDp3 = 16,
    DxbcOpDp4 = 17,
    DxbcOpElse = 18,
    DxbcOpEndIf = 21,
    DxbcOpEndLoop = 22,
    DxbcOpEq = 24,
    DxbcOpExp = 25,
    DxbcOpFtoi = 27,
    DxbcOpGe = 29,
    DxbcOpIadd = 30,
    DxbcOpIf = 31,
    DxbcOpIeq = 32,
    DxbcOpIge = 33,
    DxbcOpIlt = 34,
    DxbcOpImax = 36,
    DxbcOpImin = 37,
    DxbcOpImul = 38,
    DxbcOpIne = 39,
    DxbcOpIshr = 42,
    DxbcOpItof = 43,
    DxbcOpLd = 45,
    DxbcOpLog = 47,
    DxbcOpLoop = 48,
    DxbcOpLt = 49,
    DxbcOpMin = 51,
    DxbcOpMax = 52,
    DxbcOpMov = 54,
    DxbcOpMovc = 55,
    DxbcOpMul = 56,
    DxbcOpNe = 57,
    DxbcOpNot = 59,
    DxbcOpOr = 60,
    DxbcOpResinfo = 61,
    DxbcOpRet = 62,
    DxbcOpRoundNe = 64,
    DxbcOpRoundNi = 65,
    DxbcOpRoundPi = 66,
    DxbcOpSample = 69,
    DxbcOpSampleC = 70,
    DxbcOpSampleCLz = 71,
    DxbcOpSampleL = 72,
    DxbcOpSampleD = 73,
    DxbcOpSampleB = 74,
    DxbcOpSqrt = 75,
    DxbcOpUdiv = 78,
    DxbcOpXor = 87,
    DxbcOpDclResource = 88,
    DxbcOpDclConstantBuffer = 89,
    DxbcOpDclSampler = 90,
    DxbcOpDclInputPs = 98,
    DxbcOpDclInputPsSiv = 100,
    DxbcOpDclOutput = 101,
    DxbcOpDclTemps = 104,
    DxbcOpDclGlobalFlags = 106,
    DxbcOpLod = 108,
    DxbcOpDerivRtxCoarse = 122,
    DxbcOpDerivRtyCoarse = 124,

    // Opcode token controls; the instruction length is in bits 24..30.
    DxbcRefactoringAllowed = 1u << 11,
    DxbcSaturate = 1u << 13,
    DxbcTestNonZero = 1u << 18,
    DxbcResinfoUint = 2u << 11,
    DxbcSamplerComparison = 1u << 11,
    DxbcInterpolationConstant = 1u << 11,
    DxbcInterpolationLinear = 2u << 11,
    DxbcInterpolationLinearNoPerspective = 4u << 11,
    DxbcResourceTexture2D = 3u << 11,
    DxbcResourceTexture3D = 5u << 11,
    DxbcResourceTextureCube = 6u << 11,
    DxbcLengthShift = 24,
    DxbcReturnTypeFloat = 0x5555, // float in every component

    // Operand token fields.
    DxbcComponents1 = 1,
    DxbcComponents4 = 2,
    DxbcSelectMask = 0u << 2,
    DxbcSelectSwizzle = 1u << 2,
    DxbcSelectOne = 2u << 2,
    DxbcSelectorShift = 4,
    DxbcSwizzleIdentity = 0xe4,
    DxbcOperandTemp = 0u << 12,
    DxbcOperandInput = 1u << 12,
    DxbcOperandOutput = 2u << 12,
    DxbcOperandImmediate32 = 4u << 12,
    DxbcOperandSampler = 6u << 12,
    DxbcOperandResource = 7u << 12,
    DxbcOperandConstantBuffer = 8u << 12,
    DxbcOperandNull = 13u << 12, // a discarded result: no components, no index
    DxbcIndex1D = 1u << 20,
    DxbcIndex2D = 2u << 20,
    DxbcIndex3D = 3u << 20,
    DxbcOperandExtended = 1u << 31,
    DxbcExtendedModifier = 1,
    DxbcModifierShift = 6,
    DxbcModifierNeg = 1, // with DxbcModifierAbs: -|x|
    DxbcModifierAbs = 2,

    DxbcNamePosition = 1,
    DxbcComponentFloat32 = 3,
    DxbcPixelShader51 = 0x51,
    DxbcContainerVersion = 1,
    DxbcMaxInputRegisters = 32,
};

uint32_t FourCC(const char *code) {
    return (uint32_t)(uint8_t)code[0] | (uint32_t)(uint8_t)code[1] << 8 | (uint32_t)(uint8_t)code[2] << 16 |
           (uint32_t)(uint8_t)code[3] << 24;
}

// A token stream. An instruction is opened, given its operand tokens and
// closed, which stores its length in the opcode token.
class DxbcStream {
public:
    void Open(uint32_t opcode) {
        m_Start = m_Tokens.Size();
        m_Tokens.PushBack(opcode);
    }
    void Token(uint32_t token) { m_Tokens.PushBack(token); }
    void Close() { m_Tokens[m_Start] |= (uint32_t)(m_Tokens.Size() - m_Start) << DxbcLengthShift; }
    void Instruction(uint32_t opcode, std::initializer_list<uint32_t> operands) {
        Open(opcode);
        for (uint32_t operand : operands)
            Token(operand);
        Close();
    }
    const XArray<uint32_t> &Tokens() const { return m_Tokens; }

private:
    XArray<uint32_t> m_Tokens;
    int m_Start = 0;
};

// Where the components of a value are: a register read through a swizzle and
// a source modifier, or immediate bits with any modifier applied.
struct DxbcValue {
    uint32_t File;     // DxbcOperandTemp, Input, Output, ConstantBuffer or Immediate32
    uint32_t Index;    // register, or constant buffer row
    uint32_t Count;    // components
    uint32_t Modifier; // DxbcModifierNeg and DxbcModifierAbs bits
    uint32_t Lanes[4]; // register component of every value component
    uint32_t Bits[4];  // immediate components
};

uint32_t LaneMask(const DxbcValue &value) {
    uint32_t mask = 0;
    for (uint32_t k = 0; k < value.Count; ++k)
        mask |= 1u << value.Lanes[k];
    return mask;
}

DxbcValue Negated(DxbcValue value) {
    if (value.File == DxbcOperandImmediate32) {
        for (uint32_t k = 0; k < value.Count; ++k)
            value.Bits[k] ^= 0x80000000u;
    } else {
        value.Modifier ^= DxbcModifierNeg;
    }
    return value;
}

// Integer instructions negate a register operand in two's complement.
DxbcValue IntNegated(DxbcValue value) {
    if (value.File == DxbcOperandImmediate32) {
        for (uint32_t k = 0; k < value.Count; ++k)
            value.Bits[k] = 0u - value.Bits[k];
    } else {
        value.Modifier ^= DxbcModifierNeg;
    }
    return value;
}

DxbcValue Immediate(uint32_t count, uint32_t bits) {
    DxbcValue value;
    std::memset(&value, 0, sizeof(value));
    value.File = DxbcOperandImmediate32;
    value.Count = count;
    for (uint32_t k = 0; k < 4; ++k) {
        value.Lanes[k] = k;
        value.Bits[k] = bits;
    }
    return value;
}

// Component k of a value, alone.
DxbcValue Component(DxbcValue value, uint32_t k) {
    value.Lanes[0] = value.Lanes[k];
    value.Bits[0] = value.Bits[k];
    value.Count = 1;
    return value;
}

DxbcValue Absolute(DxbcValue value) {
    if (value.File == DxbcOperandImmediate32) {
        for (uint32_t k = 0; k < value.Count; ++k)
            value.Bits[k] &= 0x7fffffffu;
    } else {
        value.Modifier = DxbcModifierAbs;
    }
    return value;
}

// The value component each of the four positions of a source operand reads;
// -1 leaves a position unread.
struct DxbcReads {
    int Components[4];
};

// Position dest.Lanes[k] reads component k: component-wise instructions.
DxbcReads ComponentWise(const DxbcValue &dest) {
    DxbcReads reads = {{-1, -1, -1, -1}};
    for (uint32_t k = 0; k < dest.Count; ++k)
        reads.Components[dest.Lanes[k]] = (int)k;
    return reads;
}

// Every written position reads component 0: a scalar applied to a vector.
DxbcReads Broadcast(const DxbcValue &dest) {
    DxbcReads reads = {{-1, -1, -1, -1}};
    for (uint32_t k = 0; k < dest.Count; ++k)
        reads.Components[dest.Lanes[k]] = 0;
    return reads;
}

// Position dest.Lanes[0] reads component k: one component of a vector.
DxbcReads Single(const DxbcValue &dest, uint32_t k) {
    DxbcReads reads = {{-1, -1, -1, -1}};
    reads.Components[dest.Lanes[0]] = (int)k;
    return reads;
}

// Position k reads component k: dot products, coordinates and scalars.
DxbcReads Leading(uint32_t count) {
    DxbcReads reads = {{-1, -1, -1, -1}};
    for (uint32_t k = 0; k < count; ++k)
        reads.Components[k] = (int)k;
    return reads;
}

// The components of the temporary registers, allocated per value.
class DxbcTemps {
public:
    // The lowest count free components of the first register that has them.
    uint32_t Allocate(uint32_t count, uint32_t lanes[4]);
    void Release(uint32_t reg, uint32_t mask) { m_Used[(int)reg] &= (uint8_t)~mask; }
    uint32_t Count() const { return (uint32_t)m_Used.Size(); }

private:
    XArray<uint8_t> m_Used; // component mask of every register
};

uint32_t DxbcTemps::Allocate(uint32_t count, uint32_t lanes[4]) {
    int reg = 0;
    for (; reg < m_Used.Size(); ++reg) {
        uint32_t free = 0;
        for (uint32_t lane = 0; lane < 4; ++lane)
            free += (m_Used[reg] >> lane & 1u) == 0 ? 1u : 0u;
        if (free >= count)
            break;
    }
    if (reg == m_Used.Size())
        m_Used.PushBack(0);
    uint32_t taken = 0;
    for (uint32_t lane = 0; taken < count; ++lane) {
        if ((m_Used[reg] >> lane & 1u) == 0) {
            m_Used[reg] |= (uint8_t)(1u << lane);
            lanes[taken++] = lane;
        }
    }
    return (uint32_t)reg;
}

// Components for the operands a texture instruction must move first: it
// takes no source modifiers. The instruction reads its operands before it
// writes, so the destination lends its components while it has enough and
// is a temporary; the others are allocated and released with the scratch.
class DxbcScratch {
public:
    DxbcScratch(DxbcTemps &temps, const DxbcValue &dest) : m_Temps(temps), m_Dest(dest), m_Lent(0), m_Allocated(0) {}
    ~DxbcScratch() {
        for (uint32_t i = 0; i < m_Allocated; ++i)
            m_Temps.Release(m_Allocations[i].Index, LaneMask(m_Allocations[i]));
    }

    DxbcValue Take(uint32_t count);

private:
    DxbcTemps &m_Temps;
    const DxbcValue m_Dest;
    uint32_t m_Lent;
    DxbcValue m_Allocations[3]; // one per operand of the widest instruction
    uint32_t m_Allocated;
};

DxbcValue DxbcScratch::Take(uint32_t count) {
    DxbcValue value;
    std::memset(&value, 0, sizeof(value));
    value.File = DxbcOperandTemp;
    value.Count = count;
    if (m_Dest.File == DxbcOperandTemp && m_Lent + count <= m_Dest.Count) {
        value.Index = m_Dest.Index;
        for (uint32_t k = 0; k < count; ++k)
            value.Lanes[k] = m_Dest.Lanes[m_Lent + k];
        m_Lent += count;
        return value;
    }
    value.Index = m_Temps.Allocate(count, value.Lanes);
    m_Allocations[m_Allocated++] = value;
    return value;
}

struct DxbcSignatureElement {
    const char *Name;
    uint32_t SemanticIndex;
    uint32_t SystemValue;
    uint32_t Register;
    uint32_t Mask;
    uint32_t ReadWriteMask; // components an input reads, or an output never writes
};

// A signature chunk: the elements, then their names, each stored once and
// padded with 0xab to whole dwords as FXC does.
void AppendSignature(XArray<uint32_t> &chunk, const XArray<DxbcSignatureElement> &elements) {
    const uint32_t namesOffset = 8 + 24 * (uint32_t)elements.Size();
    XArray<uint8_t> names;
    XArray<uint32_t> offsets;
    chunk.PushBack((uint32_t)elements.Size());
    chunk.PushBack(8); // offset of the first element
    for (int i = 0; i < elements.Size(); ++i) {
        const DxbcSignatureElement &element = elements[i];
        int same = 0;
        while (same < i && std::strcmp(elements[same].Name, element.Name) != 0)
            ++same;
        // PushBack may reallocate before it copies, so never pass it an
        // element of the same array.
        const uint32_t offset = same < i ? offsets[same] : namesOffset + (uint32_t)names.Size();
        offsets.PushBack(offset);
        if (same == i) {
            for (const char *c = element.Name;; ++c) {
                names.PushBack((uint8_t)*c);
                if (*c == '\0')
                    break;
            }
        }
        chunk.PushBack(offset);
        chunk.PushBack(element.SemanticIndex);
        chunk.PushBack(element.SystemValue);
        chunk.PushBack(DxbcComponentFloat32);
        chunk.PushBack(element.Register);
        chunk.PushBack(element.Mask | element.ReadWriteMask << 8);
    }
    while (names.Size() % 4 != 0)
        names.PushBack(0xab);
    for (int i = 0; i < names.Size(); i += 4)
        chunk.PushBack((uint32_t)names[i] | (uint32_t)names[i + 1] << 8 | (uint32_t)names[i + 2] << 16 |
                       (uint32_t)names[i + 3] << 24);
}

void Md5Block(uint32_t state[4], const uint32_t block[16]) {
    static const uint32_t kSines[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const uint32_t kShifts[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t round = i >> 4;
        uint32_t f, g;
        switch (round) {
        case 0: f = (b & c) | (~b & d); g = i; break;
        case 1: f = (d & b) | (~d & c); g = 5 * i + 1; break;
        case 2: f = b ^ c ^ d; g = 3 * i + 5; break;
        default: f = c ^ (b | ~d); g = 7 * i; break;
        }
        const uint32_t sum = a + f + kSines[i] + block[g & 15];
        const uint32_t shift = kShifts[round * 4 + (i & 3)];
        a = d;
        d = c;
        c = b;
        b += sum << shift | sum >> (32 - shift);
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

class DxbcEmitter {
public:
    DxbcEmitter(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout);

    bool Emit(XArray<uint32_t> &words);

private:
    static const uint32_t NoRoot = 0xffffffffu;

    bool MapInputs();
    void Analyze();
    DxbcValue View(const CKJitNode &node) const;
    DxbcValue Destination(uint32_t node);
    void Release(uint32_t node);
    void ReleaseEnded(uint32_t marker);
    void Translate(uint32_t node);
    bool Control(uint32_t node);
    void Join(uint32_t marker, uint32_t arm);
    bool Clobbered(uint32_t result) const;

    void Dest(const DxbcValue &dest);
    void Source(const DxbcValue &value, const DxbcReads &reads);
    void Unary(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a);
    void Binary(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b);
    void Paired(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b);
    void Dot(const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b);
    void Modulo(const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b);
    void Reduce(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a);
    void Construct(const CKJitNode &node, const DxbcValue &dest);
    void Select(const CKJitNode &node, const DxbcValue &dest);
    DxbcValue Unmodified(const DxbcValue &value, DxbcScratch &scratch);
    void Resource(uint32_t slot, const DxbcValue &dest, uint32_t first);
    void Sampler(uint32_t slot);
    void Texture(const CKJitNode &node, const DxbcValue &dest);

    void Declare(DxbcStream &out) const;
    void Signatures(XArray<uint32_t> &input, XArray<uint32_t> &output) const;

    const CKJitFragmentShader &m_Shader;
    const CKJitResourceLayout &m_Layout;
    DxbcStream m_Code;
    DxbcTemps m_Temps;
    XArray<DxbcValue> m_Values; // where the value of every node is
    XArray<uint32_t> m_Roots;   // the node whose temporary holds a value, or NoRoot
    XArray<uint32_t> m_LastUse; // the last node reading a computed value (the node count for outputs, the
                                // ENDLOOP for what every iteration reads), or an ELSE's ENDIF
    int m_InputByRegister[DxbcMaxInputRegisters]; // input index, or -1
    uint8_t m_InputReads[DxbcMaxInputRegisters];  // components read of every input register
    uint8_t m_SamplerDims[CKJIT_MAX_SAMPLERS];    // 0xff for an unused slot
    uint32_t m_SampledSlots;                      // mask of the slots whose sampler is read
    uint32_t m_ResourceIds[CKJIT_MAX_SAMPLERS];   // range ids, dense over the used slots
    uint32_t m_SamplerIds[CKJIT_MAX_SAMPLERS];    // range ids, dense over the sampled slots
    bool m_ColorInOutput;
    bool m_ReadsUniforms;
};

DxbcEmitter::DxbcEmitter(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout)
    : m_Shader(shader), m_Layout(layout), m_SampledSlots(0), m_ColorInOutput(false), m_ReadsUniforms(false) {
    std::memset(m_InputByRegister, 0xff, sizeof(m_InputByRegister));
    std::memset(m_InputReads, 0, sizeof(m_InputReads));
    std::memset(m_SamplerDims, 0xff, sizeof(m_SamplerDims));
    std::memset(m_ResourceIds, 0, sizeof(m_ResourceIds));
    std::memset(m_SamplerIds, 0, sizeof(m_SamplerIds));
}

bool DxbcEmitter::MapInputs() {
    for (int i = 0; i < m_Shader.Inputs.Size(); ++i) {
        const CKJitInput &input = m_Shader.Inputs[i];
        if (!input.Semantic || input.Register >= DxbcMaxInputRegisters || m_InputByRegister[input.Register] >= 0)
            return false;
        m_InputByRegister[input.Register] = i;
    }
    return true;
}

// A computed value keeps its temporary components from its node to its last
// reader; views (swizzles and modifiers) of it extend that span. An arm reads
// a PHI's operand as it ends. A loop, whose temporary holds its INDEX, reads
// its count and initials as it starts; as it ends, it reads its temporary,
// carried values and nexts, and every value from before it its body reads.
void DxbcEmitter::Analyze() {
    const uint32_t count = (uint32_t)m_Shader.Nodes.Size();
    m_Roots.Resize((int)count);
    m_LastUse.Resize((int)count);
    for (uint32_t i = 0; i < count; ++i) {
        const CKJitNode &node = m_Shader.Nodes[(int)i];
        m_LastUse[(int)i] = i;
        switch (node.Op) {
        case CKJIT_OP_CONSTANT:
        case CKJIT_OP_INPUT:
        case CKJIT_OP_UNIFORM:
            m_Roots[(int)i] = NoRoot;
            continue;
        case CKJIT_OP_SWIZZLE:
        case CKJIT_OP_NEG:
        case CKJIT_OP_ABS:
        case CKJIT_OP_INDEX:
            m_Roots[(int)i] = m_Roots[(int)node.Operands[0]];
            continue;
        case CKJIT_OP_IF:
        case CKJIT_OP_ELSE:
        case CKJIT_OP_ENDIF: {
            m_Roots[(int)i] = NoRoot;
            const uint32_t operand = node.Operands[0];
            if (node.Op == CKJIT_OP_ENDIF)
                m_LastUse[(int)operand] = i;
            else if (node.Op == CKJIT_OP_IF && m_Roots[(int)operand] != NoRoot)
                m_LastUse[(int)m_Roots[(int)operand]] = i;
            continue;
        }
        case CKJIT_OP_PHI: {
            m_Roots[(int)i] = i;
            const uint32_t endif = node.Operands[2];
            const uint32_t ends[2] = {m_Shader.Nodes[(int)endif].Operands[0], endif};
            for (uint32_t arm = 0; arm < 2; ++arm) {
                const uint32_t root = m_Roots[(int)node.Operands[arm]];
                if (root != NoRoot && m_LastUse[(int)root] < ends[arm])
                    m_LastUse[(int)root] = ends[arm];
            }
            continue;
        }
        case CKJIT_OP_CARRY: {
            m_Roots[(int)i] = i;
            const uint32_t root = m_Roots[(int)node.Operands[0]];
            if (root != NoRoot)
                m_LastUse[(int)root] = node.Operands[1];
            continue;
        }
        case CKJIT_OP_ENDLOOP: {
            m_Roots[(int)i] = NoRoot;
            const uint32_t loop = node.Operands[0];
            for (uint32_t root = 0; root < i; ++root) {
                const CKJitNode &value = m_Shader.Nodes[(int)root];
                if (m_Roots[(int)root] != root)
                    continue;
                if (root < loop ? m_LastUse[(int)root] > loop
                                : root == loop || (value.Op == CKJIT_OP_CARRY && value.Operands[1] == loop)) {
                    m_LastUse[(int)root] = i;
                }
            }
            continue;
        }
        case CKJIT_OP_RESULT: {
            // The carried value, once the loop ends.
            m_Roots[(int)i] = m_Roots[(int)node.Operands[0]];
            const uint32_t root = m_Roots[(int)node.Operands[1]];
            const uint32_t endloop = node.Operands[2];
            if (root != NoRoot && m_LastUse[(int)root] < endloop)
                m_LastUse[(int)root] = endloop;
            continue;
        }
        case CKJIT_OP_LOAD:
        case CKJIT_OP_SIZE:
        case CKJIT_OP_LEVELS: // the texture without its sampler
            m_SamplerDims[node.Imm[0]] = (uint8_t)node.Imm[1];
            break;
        default:
            if ((CKJitOpFlags(node.Op) & CKJIT_OPFLAG_TEXTURE) != 0) {
                m_SamplerDims[node.Imm[0]] = (uint8_t)node.Imm[1];
                m_SampledSlots |= 1u << node.Imm[0];
            }
            break;
        }
        m_Roots[(int)i] = i;
        for (uint32_t operand = 0; operand < node.OperandCount; ++operand) {
            const uint32_t root = m_Roots[(int)node.Operands[operand]];
            if (root != NoRoot)
                m_LastUse[(int)root] = i;
        }
    }

    // A computed colour nothing else reads is computed into the output.
    const uint32_t color = m_Shader.Color.Id;
    m_ColorInOutput = m_Roots[(int)color] == color && m_LastUse[(int)color] == color;
    if (m_Roots[(int)color] != NoRoot)
        m_LastUse[(int)m_Roots[(int)color]] = count;
    if (m_Shader.Discard.IsValid() && m_Roots[(int)m_Shader.Discard.Id] != NoRoot)
        m_LastUse[(int)m_Roots[(int)m_Shader.Discard.Id]] = count;

    uint32_t resources = 0, samplers = 0;
    for (uint32_t slot = 0; slot < CKJIT_MAX_SAMPLERS; ++slot) {
        if (m_SamplerDims[slot] != 0xff)
            m_ResourceIds[slot] = resources++;
        if ((m_SampledSlots >> slot & 1u) != 0)
            m_SamplerIds[slot] = samplers++;
    }
}

// Leaves name their register or bits and views adjust their operand's; none
// of them emits code.
DxbcValue DxbcEmitter::View(const CKJitNode &node) const {
    DxbcValue value;
    std::memset(&value, 0, sizeof(value));
    value.Count = CKJitComponentCount(node.Type);
    for (uint32_t k = 0; k < 4; ++k)
        value.Lanes[k] = k;
    switch (node.Op) {
    case CKJIT_OP_CONSTANT:
        value.File = DxbcOperandImmediate32;
        for (uint32_t k = 0; k < value.Count; ++k)
            value.Bits[k] = CKJitIsBool(node.Type) ? (node.Imm[k] != 0 ? 0xffffffffu : 0u) : node.Imm[k];
        return value;
    case CKJIT_OP_INPUT:
        value.File = DxbcOperandInput;
        value.Index = m_Shader.Inputs[(int)node.Imm[0]].Register;
        return value;
    case CKJIT_OP_UNIFORM:
        value.File = DxbcOperandConstantBuffer;
        value.Index = node.Imm[0];
        return value;
    case CKJIT_OP_SWIZZLE: {
        const DxbcValue &source = m_Values[(int)node.Operands[0]];
        value.File = source.File;
        value.Index = source.Index;
        value.Modifier = source.Modifier;
        for (uint32_t k = 0; k < value.Count; ++k) {
            value.Lanes[k] = source.Lanes[node.Imm[k]];
            value.Bits[k] = source.Bits[node.Imm[k]];
        }
        return value;
    }
    case CKJIT_OP_NEG:
        return Negated(m_Values[(int)node.Operands[0]]);
    default:
        return Absolute(m_Values[(int)node.Operands[0]]);
    }
}

DxbcValue DxbcEmitter::Destination(uint32_t node) {
    DxbcValue dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.Count = CKJitComponentCount(m_Shader.Nodes[(int)node].Type);
    if (node == m_Shader.Color.Id && m_ColorInOutput) {
        dest.File = DxbcOperandOutput;
        for (uint32_t k = 0; k < 4; ++k)
            dest.Lanes[k] = k;
    } else {
        dest.File = DxbcOperandTemp;
        dest.Index = m_Temps.Allocate(dest.Count, dest.Lanes);
    }
    return dest;
}

void DxbcEmitter::Release(uint32_t node) {
    const DxbcValue &value = m_Values[(int)node];
    if (value.File == DxbcOperandTemp)
        m_Temps.Release(value.Index, LaneMask(value));
}

// The values a loop marker reads last: at a LOOP its count and initials, at
// an ENDLOOP what every iteration reads.
void DxbcEmitter::ReleaseEnded(uint32_t marker) {
    for (uint32_t root = 0; root < marker; ++root) {
        if (m_Roots[(int)root] == root && m_LastUse[(int)root] == marker)
            Release(root);
    }
}

void DxbcEmitter::Dest(const DxbcValue &dest) {
    m_Code.Token(dest.File | DxbcComponents4 | DxbcSelectMask | LaneMask(dest) << DxbcSelectorShift | DxbcIndex1D);
    m_Code.Token(dest.Index);
}

// One read position selects a single component (a scalar immediate); unread
// positions repeat the first read one or are zero immediates, as with FXC.
void DxbcEmitter::Source(const DxbcValue &value, const DxbcReads &reads) {
    int first = -1;
    uint32_t used = 0;
    for (int p = 0; p < 4; ++p) {
        if (reads.Components[p] >= 0) {
            first = first < 0 ? p : first;
            ++used;
        }
    }
    if (value.File == DxbcOperandImmediate32) {
        if (used == 1) {
            m_Code.Token(DxbcOperandImmediate32 | DxbcComponents1);
            m_Code.Token(value.Bits[reads.Components[first]]);
        } else {
            m_Code.Token(DxbcOperandImmediate32 | DxbcComponents4);
            for (int p = 0; p < 4; ++p)
                m_Code.Token(reads.Components[p] >= 0 ? value.Bits[reads.Components[p]] : 0u);
        }
        return;
    }

    uint32_t token = value.File | DxbcComponents4;
    uint32_t lanes = 0;
    if (used == 1) {
        const uint32_t lane = value.Lanes[reads.Components[first]];
        token |= DxbcSelectOne | lane << DxbcSelectorShift;
        lanes = 1u << lane;
    } else {
        uint32_t swizzle = 0;
        for (int p = 0; p < 4; ++p) {
            const uint32_t lane = value.Lanes[reads.Components[reads.Components[p] >= 0 ? p : first]];
            swizzle |= lane << (2 * p);
            lanes |= 1u << lane;
        }
        token |= DxbcSelectSwizzle | swizzle << DxbcSelectorShift;
    }
    token |= value.File == DxbcOperandConstantBuffer ? DxbcIndex3D : DxbcIndex1D;
    token |= value.Modifier != 0 ? DxbcOperandExtended : 0u;
    m_Code.Token(token);
    if (value.Modifier != 0)
        m_Code.Token(DxbcExtendedModifier | value.Modifier << DxbcModifierShift);
    if (value.File == DxbcOperandConstantBuffer) {
        m_Code.Token(0); // range id
        m_Code.Token(m_Layout.UniformBinding);
        m_ReadsUniforms = true;
    } else if (value.File == DxbcOperandInput) {
        m_InputReads[value.Index] |= (uint8_t)lanes;
    }
    m_Code.Token(value.Index);
}

void DxbcEmitter::Unary(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a) {
    m_Code.Open(opcode);
    Dest(dest);
    Source(a, ComponentWise(dest));
    m_Code.Close();
}

void DxbcEmitter::Binary(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b) {
    m_Code.Open(opcode);
    Dest(dest);
    Source(a, ComponentWise(dest));
    Source(b, ComponentWise(dest));
    m_Code.Close();
}

// imul and udiv write two results; the first, the high product or the
// quotient, is discarded.
void DxbcEmitter::Paired(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b) {
    m_Code.Open(opcode);
    m_Code.Token(DxbcOperandNull);
    Dest(dest);
    Source(a, ComponentWise(dest));
    Source(b, ComponentWise(dest));
    m_Code.Close();
}

void DxbcEmitter::Dot(const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b) {
    static const uint32_t kDots[] = {DxbcOpMul, DxbcOpDp2, DxbcOpDp3, DxbcOpDp4};
    m_Code.Open(kDots[a.Count - 1]);
    Dest(dest);
    Source(a, Leading(a.Count));
    Source(b, Leading(a.Count));
    m_Code.Close();
}

// Shader model 5 only divides unsigned values, so the remainder is taken of
// non-negative ones. With m = a >> 31, 0 or all ones, the floored a mod b is
// ((a ^ m) urem b ^ m) + (b & m). The integer result is never the output, so
// the destination holds the intermediate values.
void DxbcEmitter::Modulo(const DxbcValue &dest, const DxbcValue &a, const DxbcValue &b) {
    DxbcValue sign = dest;
    sign.Index = m_Temps.Allocate(dest.Count, sign.Lanes);
    Binary(DxbcOpIshr, sign, a, Immediate(dest.Count, 31));
    Binary(DxbcOpXor, dest, a, sign);
    Paired(DxbcOpUdiv, dest, dest, b);
    Binary(DxbcOpXor, dest, dest, sign);
    Binary(DxbcOpAnd, sign, b, sign);
    Binary(DxbcOpIadd, dest, dest, sign);
    m_Temps.Release(sign.Index, LaneMask(sign));
}

// A chain of ors or ands folds every component into the scalar destination.
void DxbcEmitter::Reduce(uint32_t opcode, const DxbcValue &dest, const DxbcValue &a) {
    const DxbcValue *folded = &a;
    for (uint32_t k = 1; k < a.Count; ++k) {
        m_Code.Open(opcode);
        Dest(dest);
        Source(*folded, Single(dest, 0));
        Source(a, Single(dest, k));
        m_Code.Close();
        folded = &dest;
    }
}

// One move per part, into its components of the destination.
void DxbcEmitter::Construct(const CKJitNode &node, const DxbcValue &dest) {
    uint32_t offset = 0;
    for (uint32_t i = 0; i < node.OperandCount; ++i) {
        const DxbcValue &part = m_Values[(int)node.Operands[i]];
        DxbcValue lanes = dest;
        lanes.Count = part.Count;
        for (uint32_t k = 0; k < part.Count; ++k)
            lanes.Lanes[k] = dest.Lanes[offset + k];
        Unary(DxbcOpMov, lanes, part);
        offset += part.Count;
    }
}

void DxbcEmitter::Select(const CKJitNode &node, const DxbcValue &dest) {
    const DxbcValue &condition = m_Values[(int)node.Operands[0]];
    m_Code.Open(DxbcOpMovc);
    Dest(dest);
    Source(condition, condition.Count == 1 ? Broadcast(dest) : ComponentWise(dest));
    Source(m_Values[(int)node.Operands[1]], ComponentWise(dest));
    Source(m_Values[(int)node.Operands[2]], ComponentWise(dest));
    m_Code.Close();
}

DxbcValue DxbcEmitter::Unmodified(const DxbcValue &value, DxbcScratch &scratch) {
    if (value.Modifier == 0)
        return value;
    const DxbcValue moved = scratch.Take(value.Count);
    Unary(DxbcOpMov, moved, value);
    return moved;
}

// A slot's texture; its swizzle routes result component first + k to the
// destination's component k. A scalar result is replicated, as fxc writes it
// for comparisons.
void DxbcEmitter::Resource(uint32_t slot, const DxbcValue &dest, uint32_t first) {
    uint32_t swizzle = dest.Count == 1 ? first * 0x55 : DxbcSwizzleIdentity;
    for (uint32_t k = 0; k < dest.Count; ++k) {
        const uint32_t shift = 2 * dest.Lanes[k];
        swizzle = (swizzle & ~(3u << shift)) | (first + k) << shift;
    }
    m_Code.Token(DxbcOperandResource | DxbcComponents4 | DxbcSelectSwizzle | swizzle << DxbcSelectorShift | DxbcIndex2D);
    m_Code.Token(m_ResourceIds[slot]);
    m_Code.Token(slot);
}

void DxbcEmitter::Sampler(uint32_t slot) {
    m_Code.Token(DxbcOperandSampler | DxbcIndex2D);
    m_Code.Token(m_SamplerIds[slot]);
    m_Code.Token(slot);
}

// dest, address, texture[, sampler][, extra operands]: the bias, the LOD,
// the two derivatives or the reference. Loads and size queries leave the
// sampler out.
void DxbcEmitter::Texture(const CKJitNode &node, const DxbcValue &dest) {
    DxbcScratch scratch(m_Temps, dest);
    DxbcValue operands[3] = {};
    for (uint32_t i = 0; i < node.OperandCount; ++i)
        operands[i] = Unmodified(m_Values[(int)node.Operands[i]], scratch);

    uint32_t opcode = DxbcOpSample;
    DxbcValue address = operands[0];
    DxbcReads reads = Leading(address.Count);
    uint32_t first = 0; // the result component of the value's first
    uint32_t extra = 0;
    bool sampled = true;
    switch (node.Op) {
    case CKJIT_OP_SAMPLE: {
        const CKJitNode &bias = m_Shader.Nodes[(int)node.Operands[1]];
        extra = bias.Op != CKJIT_OP_CONSTANT || (bias.Imm[0] & 0x7fffffffu) != 0 ? 1 : 0;
        opcode = extra != 0 ? DxbcOpSampleB : DxbcOpSample;
        break;
    }
    case CKJIT_OP_SAMPLE_LEVEL: opcode = DxbcOpSampleL; extra = 1; break;
    case CKJIT_OP_SAMPLE_GRAD: opcode = DxbcOpSampleD; extra = 2; break;
    case CKJIT_OP_CALC_LOD: opcode = DxbcOpLod; first = 1; break; // x is clamped, y is not
    case CKJIT_OP_SAMPLE_CMP: opcode = DxbcOpSampleC; extra = 1; break;
    case CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO: opcode = DxbcOpSampleCLz; extra = 1; break;
    case CKJIT_OP_LOAD:
        // The mip is the address's w, after the texel.
        opcode = DxbcOpLd;
        reads = address.Count == 3 ? DxbcReads{{0, 1, -1, 2}} : Leading(4);
        sampled = false;
        break;
    case CKJIT_OP_SIZE: opcode = DxbcOpResinfo | DxbcResinfoUint; sampled = false; break;
    default: // LEVELS: the mip count is the w of every mip's extent
        opcode = DxbcOpResinfo | DxbcResinfoUint;
        address = Immediate(1, 0);
        reads = Leading(1);
        first = 3;
        sampled = false;
        break;
    }

    m_Code.Open(opcode);
    Dest(dest);
    Source(address, reads);
    Resource(node.Imm[0], dest, first);
    if (sampled)
        Sampler(node.Imm[0]);
    for (uint32_t i = 1; i <= extra; ++i)
        Source(operands[i], Leading(operands[i].Count));
    m_Code.Close();
}

// Regions are if_nz, else and endif blocks. Loops are loop blocks that break
// once a counter reaches the trips, the count or the bound when lower: the
// loop's temporary holds the counter, the INDEX, and unless the count is
// constant the trips. Moves before the loop start the carried values and
// moves ending each iteration take the nexts, which the RESULTs directly
// after the ENDLOOP name.
bool DxbcEmitter::Control(uint32_t index) {
    const CKJitNode &node = m_Shader.Nodes[(int)index];
    switch (node.Op) {
    case CKJIT_OP_IF: {
        m_Code.Open(DxbcOpIf | DxbcTestNonZero);
        Source(m_Values[(int)node.Operands[0]], Leading(1));
        m_Code.Close();
        const uint32_t root = m_Roots[(int)node.Operands[0]];
        if (root != NoRoot && m_LastUse[(int)root] == index)
            Release(root);
        return true;
    }
    case CKJIT_OP_ELSE:
        Join(index, 0);
        m_Code.Instruction(DxbcOpElse, {});
        return true;
    case CKJIT_OP_ENDIF:
        Join(index, 1);
        m_Code.Instruction(DxbcOpEndIf, {});
        return true;
    case CKJIT_OP_PHI: // computed by the arms
        if (m_LastUse[(int)index] == index)
            Release(index);
        return true;
    case CKJIT_OP_LOOP: {
        const DxbcValue &count = m_Values[(int)node.Operands[0]];
        const bool constant = count.File == DxbcOperandImmediate32;
        DxbcValue state;
        std::memset(&state, 0, sizeof(state));
        state.File = DxbcOperandTemp;
        state.Count = constant ? 1 : 2;
        state.Index = m_Temps.Allocate(state.Count, state.Lanes);
        m_Values[(int)index] = state;
        // The carried values are allocated while their initials are live, so
        // no move overwrites another's source.
        const uint32_t size = (uint32_t)m_Shader.Nodes.Size();
        uint32_t header = index + 1;
        if (header < size && m_Shader.Nodes[(int)header].Op == CKJIT_OP_INDEX)
            m_Values[(int)header++] = Component(state, 0);
        for (; header < size && m_Shader.Nodes[(int)header].Op == CKJIT_OP_CARRY; ++header) {
            m_Values[(int)header] = Destination(header);
            Unary(DxbcOpMov, m_Values[(int)header], m_Values[(int)m_Shader.Nodes[(int)header].Operands[0]]);
        }
        const DxbcValue counter = Component(state, 0);
        DxbcValue trips = Immediate(1, node.Imm[0]);
        if (!constant) {
            trips = Component(state, 1);
            Binary(DxbcOpImin, trips, count, Immediate(1, node.Imm[0]));
        } else if ((int32_t)count.Bits[0] < (int32_t)node.Imm[0]) {
            trips.Bits[0] = count.Bits[0];
        }
        Unary(DxbcOpMov, counter, Immediate(1, 0));
        ReleaseEnded(index);

        m_Code.Instruction(DxbcOpLoop, {});
        DxbcValue test = counter;
        test.Index = m_Temps.Allocate(1, test.Lanes);
        Binary(DxbcOpIge, test, counter, trips);
        m_Code.Open(DxbcOpBreakc | DxbcTestNonZero);
        Source(test, Leading(1));
        m_Code.Close();
        m_Temps.Release(test.Index, LaneMask(test));
        return true;
    }
    case CKJIT_OP_INDEX:
    case CKJIT_OP_CARRY: // started by their LOOP
        return true;
    case CKJIT_OP_ENDLOOP: {
        // The moves are parallel: a next reading a carried value moved before
        // its own is copied first. Copies are allocated while every next and
        // carried value is live.
        const uint32_t size = (uint32_t)m_Shader.Nodes.Size();
        uint32_t end = index + 1;
        for (; end < size && m_Shader.Nodes[(int)end].Op == CKJIT_OP_RESULT; ++end) {
            m_Values[(int)end] = m_Values[(int)m_Shader.Nodes[(int)end].Operands[1]];
            if (Clobbered(end)) {
                const DxbcValue copy = Destination(end);
                Unary(DxbcOpMov, copy, m_Values[(int)end]);
                m_Values[(int)end] = copy;
            }
        }
        for (uint32_t result = index + 1; result < end; ++result) {
            const CKJitNode &binding = m_Shader.Nodes[(int)result];
            if (binding.Operands[1] != binding.Operands[0])
                Unary(DxbcOpMov, m_Values[(int)binding.Operands[0]], m_Values[(int)result]);
            if (Clobbered(result))
                Release(result);
        }
        const DxbcValue counter = Component(m_Values[(int)node.Operands[0]], 0);
        Binary(DxbcOpIadd, counter, counter, Immediate(1, 1));
        m_Code.Instruction(DxbcOpEndLoop, {});
        ReleaseEnded(index);
        return true;
    }
    case CKJIT_OP_RESULT:
        m_Values[(int)index] = m_Values[(int)node.Operands[0]];
        return true;
    default:
        return false;
    }
}

// Whether a RESULT's next reads a carried value of its loop that the moves
// ending an iteration overwrite first: one before its own carried value.
bool DxbcEmitter::Clobbered(uint32_t result) const {
    const CKJitNode &binding = m_Shader.Nodes[(int)result];
    const uint32_t root = m_Roots[(int)binding.Operands[1]];
    if (root == NoRoot || root >= binding.Operands[0])
        return false;
    const CKJitNode &value = m_Shader.Nodes[(int)root];
    return value.Op == CKJIT_OP_CARRY && value.Operands[1] == m_Shader.Nodes[(int)binding.Operands[0]].Operands[1];
}

// Moves the results of the arm a marker ends into the PHIs after the ENDIF.
// Their registers are taken at the ELSE, while every then result and every
// else result from before the region is live and before the else arm computes
// the others, so no move overwrites another's source.
void DxbcEmitter::Join(uint32_t marker, uint32_t arm) {
    const uint32_t endif = arm == 0 ? m_LastUse[(int)marker] : marker;
    const uint32_t count = (uint32_t)m_Shader.Nodes.Size();
    uint32_t end = endif + 1;
    for (; end < count && m_Shader.Nodes[(int)end].Op == CKJIT_OP_PHI; ++end) {
        if (arm == 0)
            m_Values[(int)end] = Destination(end);
        Unary(DxbcOpMov, m_Values[(int)end], m_Values[(int)m_Shader.Nodes[(int)end].Operands[arm]]);
    }
    for (uint32_t phi = endif + 1; phi < end; ++phi) {
        const uint32_t root = m_Roots[(int)m_Shader.Nodes[(int)phi].Operands[arm]];
        if (root != NoRoot && m_LastUse[(int)root] == marker)
            Release(root);
    }
}

void DxbcEmitter::Translate(uint32_t index) {
    const CKJitNode &node = m_Shader.Nodes[(int)index];
    if (Control(index))
        return;
    if (m_Roots[(int)index] != index) {
        m_Values[(int)index] = View(node);
        return;
    }

    // The destination is allocated while the operands are live, so it never
    // overlaps them.
    const DxbcValue dest = Destination(index);
    m_Values[(int)index] = dest;
    const DxbcValue &a = m_Values[(int)node.Operands[0]];
    const DxbcValue &b = m_Values[(int)node.Operands[node.OperandCount > 1 ? 1 : 0]];
    switch (node.Op) {
    case CKJIT_OP_CONSTRUCT: Construct(node, dest); break;
    case CKJIT_OP_ADD: Binary(DxbcOpAdd, dest, a, b); break;
    case CKJIT_OP_SUB: Binary(DxbcOpAdd, dest, a, Negated(b)); break;
    case CKJIT_OP_MUL: Binary(DxbcOpMul, dest, a, b); break;
    case CKJIT_OP_DIV: Binary(DxbcOpDiv, dest, a, b); break;
    case CKJIT_OP_MIN: Binary(DxbcOpMin, dest, a, b); break;
    case CKJIT_OP_MAX: Binary(DxbcOpMax, dest, a, b); break;
    case CKJIT_OP_SATURATE: Unary(DxbcOpMov | DxbcSaturate, dest, a); break;
    case CKJIT_OP_FLOOR: Unary(DxbcOpRoundNi, dest, a); break;
    case CKJIT_OP_CEIL: Unary(DxbcOpRoundPi, dest, a); break;
    case CKJIT_OP_ROUND_EVEN: Unary(DxbcOpRoundNe, dest, a); break;
    case CKJIT_OP_EXP2: Unary(DxbcOpExp, dest, a); break;
    case CKJIT_OP_LOG2: Unary(DxbcOpLog, dest, a); break;
    case CKJIT_OP_SQRT: Unary(DxbcOpSqrt, dest, a); break;
    case CKJIT_OP_DOT: Dot(dest, a, b); break;
    case CKJIT_OP_DDX: Unary(DxbcOpDerivRtxCoarse, dest, a); break;
    case CKJIT_OP_DDY: Unary(DxbcOpDerivRtyCoarse, dest, a); break;
    case CKJIT_OP_LT: Binary(DxbcOpLt, dest, a, b); break;
    case CKJIT_OP_LE: Binary(DxbcOpGe, dest, b, a); break;
    case CKJIT_OP_EQ: Binary(DxbcOpEq, dest, a, b); break;
    case CKJIT_OP_NE: Binary(DxbcOpNe, dest, a, b); break;
    case CKJIT_OP_FTOI: Unary(DxbcOpFtoi, dest, a); break;
    case CKJIT_OP_ITOF: Unary(DxbcOpItof, dest, a); break;
    case CKJIT_OP_IADD: Binary(DxbcOpIadd, dest, a, b); break;
    case CKJIT_OP_ISUB: Binary(DxbcOpIadd, dest, a, IntNegated(b)); break;
    case CKJIT_OP_IMUL: Paired(DxbcOpImul, dest, a, b); break;
    case CKJIT_OP_IMIN: Binary(DxbcOpImin, dest, a, b); break;
    case CKJIT_OP_IMAX: Binary(DxbcOpImax, dest, a, b); break;
    case CKJIT_OP_IMOD: Modulo(dest, a, b); break;
    case CKJIT_OP_IAND: Binary(DxbcOpAnd, dest, a, b); break;
    case CKJIT_OP_ISHR: {
        // The hardware shifts by the low five bits; constant counts are
        // stored that way, as FXC folds them.
        DxbcValue count = b;
        if (count.File == DxbcOperandImmediate32) {
            for (uint32_t k = 0; k < count.Count; ++k)
                count.Bits[k] &= 31u;
        }
        Binary(DxbcOpIshr, dest, a, count);
        break;
    }
    case CKJIT_OP_ILT: Binary(DxbcOpIlt, dest, a, b); break;
    case CKJIT_OP_ILE: Binary(DxbcOpIge, dest, b, a); break;
    case CKJIT_OP_IEQ: Binary(DxbcOpIeq, dest, a, b); break;
    case CKJIT_OP_INE: Binary(DxbcOpIne, dest, a, b); break;
    case CKJIT_OP_AND: Binary(DxbcOpAnd, dest, a, b); break;
    case CKJIT_OP_OR: Binary(DxbcOpOr, dest, a, b); break;
    case CKJIT_OP_NOT: Unary(DxbcOpNot, dest, a); break;
    case CKJIT_OP_ANY: Reduce(DxbcOpOr, dest, a); break;
    case CKJIT_OP_ALL: Reduce(DxbcOpAnd, dest, a); break;
    case CKJIT_OP_SELECT: Select(node, dest); break;
    case CKJIT_OP_SAMPLE:
    case CKJIT_OP_SAMPLE_LEVEL:
    case CKJIT_OP_SAMPLE_GRAD:
    case CKJIT_OP_CALC_LOD:
    case CKJIT_OP_SAMPLE_CMP:
    case CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO:
    case CKJIT_OP_LOAD:
    case CKJIT_OP_SIZE:
    case CKJIT_OP_LEVELS: Texture(node, dest); break;
    default: break; // leaves and views are handled above
    }

    for (uint32_t operand = 0; operand < node.OperandCount; ++operand) {
        const uint32_t root = m_Roots[(int)node.Operands[operand]];
        if (root != NoRoot && m_LastUse[(int)root] == index)
            Release(root);
    }
    if (m_LastUse[(int)index] == index)
        Release(index);
}

void DxbcEmitter::Declare(DxbcStream &out) const {
    const uint32_t range = DxbcComponents4 | DxbcSelectSwizzle | DxbcSwizzleIdentity << DxbcSelectorShift | DxbcIndex3D;
    out.Instruction(DxbcOpDclGlobalFlags | DxbcRefactoringAllowed, {});
    if (m_ReadsUniforms) {
        out.Instruction(DxbcOpDclConstantBuffer, {DxbcOperandConstantBuffer | range, 0, m_Layout.UniformBinding,
                                                  m_Layout.UniformBinding, m_Shader.UniformVec4Count,
                                                  m_Layout.UniformSpace});
    }
    for (uint32_t slot = 0; slot < CKJIT_MAX_SAMPLERS; ++slot) {
        if ((m_SampledSlots >> slot & 1u) != 0) {
            const uint32_t mode = m_SamplerDims[slot] == CKJIT_SAMPLER_2D_COMPARE ? DxbcSamplerComparison : 0u;
            out.Instruction(DxbcOpDclSampler | mode,
                            {DxbcOperandSampler | range, m_SamplerIds[slot], slot, slot, m_Layout.SamplerSpace});
        }
    }
    static const uint32_t kDimensions[] = {DxbcResourceTexture2D, DxbcResourceTextureCube, DxbcResourceTexture3D,
                                           DxbcResourceTexture2D};
    for (uint32_t slot = 0; slot < CKJIT_MAX_SAMPLERS; ++slot) {
        if (m_SamplerDims[slot] != 0xff) {
            out.Instruction(DxbcOpDclResource | kDimensions[m_SamplerDims[slot]],
                            {DxbcOperandResource | range, m_ResourceIds[slot], slot, slot, DxbcReturnTypeFloat,
                             m_Layout.SamplerSpace});
        }
    }

    // Only read components are declared; the signature keeps every input.
    for (uint32_t reg = 0; reg < DxbcMaxInputRegisters; ++reg) {
        if (m_InputByRegister[reg] < 0 || m_InputReads[reg] == 0)
            continue;
        const uint32_t operand = DxbcOperandInput | DxbcComponents4 | DxbcSelectMask |
                                 (uint32_t)m_InputReads[reg] << DxbcSelectorShift | DxbcIndex1D;
        switch (m_Shader.Inputs[m_InputByRegister[reg]].Kind) {
        case CKJIT_INPUT_FRAG_COORD:
            out.Instruction(DxbcOpDclInputPsSiv | DxbcInterpolationLinearNoPerspective,
                            {operand, reg, DxbcNamePosition});
            break;
        case CKJIT_INPUT_FLAT:
            out.Instruction(DxbcOpDclInputPs | DxbcInterpolationConstant, {operand, reg});
            break;
        default:
            out.Instruction(DxbcOpDclInputPs | DxbcInterpolationLinear, {operand, reg});
            break;
        }
    }
    out.Instruction(DxbcOpDclOutput,
                    {DxbcOperandOutput | DxbcComponents4 | DxbcSelectMask | 0xfu << DxbcSelectorShift | DxbcIndex1D, 0});
    if (m_Temps.Count() > 0)
        out.Instruction(DxbcOpDclTemps, {m_Temps.Count()});
}

void DxbcEmitter::Signatures(XArray<uint32_t> &input, XArray<uint32_t> &output) const {
    XArray<DxbcSignatureElement> elements;
    for (uint32_t reg = 0; reg < DxbcMaxInputRegisters; ++reg) {
        if (m_InputByRegister[reg] < 0)
            continue;
        const CKJitInput &source = m_Shader.Inputs[m_InputByRegister[reg]];
        const DxbcSignatureElement element = {
            source.Semantic,
            source.SemanticIndex,
            source.Kind == CKJIT_INPUT_FRAG_COORD ? (uint32_t)DxbcNamePosition : 0u,
            reg,
            (1u << source.Components) - 1u,
            m_InputReads[reg],
        };
        elements.PushBack(element);
    }
    AppendSignature(input, elements);

    elements.Clear();
    const DxbcSignatureElement target = {"SV_Target", 0, 0, 0, 0xf, 0};
    elements.PushBack(target);
    AppendSignature(output, elements);
}

bool DxbcEmitter::Emit(XArray<uint32_t> &words) {
    if (!MapInputs())
        return false;
    Analyze();
    m_Values.Resize(m_Shader.Nodes.Size());
    for (uint32_t i = 0; i < (uint32_t)m_Shader.Nodes.Size(); ++i)
        Translate(i);

    // Every value is computed before the discard, so no implicit-LOD sample,
    // LOD query or derivative runs after a quad neighbour was discarded.
    if (m_Shader.Discard.IsValid()) {
        m_Code.Open(DxbcOpDiscard | DxbcTestNonZero);
        Source(m_Values[(int)m_Shader.Discard.Id], Leading(1));
        m_Code.Close();
    }
    if (!m_ColorInOutput) {
        DxbcValue output;
        std::memset(&output, 0, sizeof(output));
        output.File = DxbcOperandOutput;
        output.Count = 4;
        for (uint32_t k = 0; k < 4; ++k)
            output.Lanes[k] = k;
        Unary(DxbcOpMov, output, m_Values[(int)m_Shader.Color.Id]);
    }
    m_Code.Instruction(DxbcOpRet, {});

    // The declarations follow from the code: what it reads, and its temps.
    DxbcStream declarations;
    Declare(declarations);
    XArray<uint32_t> program;
    program.PushBack(DxbcPixelShader51);
    program.PushBack(2u + (uint32_t)declarations.Tokens().Size() + (uint32_t)m_Code.Tokens().Size());
    program += declarations.Tokens();
    program += m_Code.Tokens();

    XArray<uint32_t> inputSignature, outputSignature;
    Signatures(inputSignature, outputSignature);

    const XArray<uint32_t> *chunks[] = {&inputSignature, &outputSignature, &program};
    static const char *const kChunkNames[] = {"ISGN", "OSGN", "SHEX"};
    const uint32_t chunkCount = sizeof(chunks) / sizeof(chunks[0]);
    words.Clear();
    words.PushBack(FourCC("DXBC"));
    for (uint32_t i = 0; i < 4; ++i)
        words.PushBack(0); // digest, once the rest is known
    words.PushBack(DxbcContainerVersion);
    words.PushBack(0); // total size
    words.PushBack(chunkCount);
    for (uint32_t i = 0; i < chunkCount; ++i)
        words.PushBack(0); // chunk offset
    for (uint32_t i = 0; i < chunkCount; ++i) {
        words[8 + (int)i] = (uint32_t)words.Size() * 4u;
        words.PushBack(FourCC(kChunkNames[i]));
        words.PushBack((uint32_t)chunks[i]->Size() * 4u);
        words += *chunks[i];
    }
    words[6] = (uint32_t)words.Size() * 4u;
    CKJitDxbcDigest(words.Begin(), (uint32_t)words.Size(), &words[1]);
    return true;
}

} // namespace

bool CKJitEmitDxbc(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout, XArray<uint32_t> &words) {
    if (!CKJitVerify(shader))
        return false;
    DxbcEmitter emitter(shader, layout);
    return emitter.Emit(words);
}

void CKJitDxbcDigest(const uint32_t *words, uint32_t count, uint32_t digest[4]) {
    uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const uint32_t *data = count > 5 ? words + 5 : words;
    const uint32_t length = count > 5 ? count - 5 : 0;
    const uint32_t full = length & ~15u;
    for (uint32_t i = 0; i < full; i += 16)
        Md5Block(state, data + i);

    // Unlike MD5 the bit count leads the final block, which ends with
    // (bytes * 2) | 1; a remainder too long for both gets a block of its own.
    const uint32_t rest = length - full;
    uint32_t block[16];
    std::memset(block, 0, sizeof(block));
    if (rest >= 14) {
        std::memcpy(block, data + full, rest * sizeof(uint32_t));
        block[rest] = 0x80;
        Md5Block(state, block);
        std::memset(block, 0, sizeof(block));
    } else {
        std::memcpy(block + 1, data + full, rest * sizeof(uint32_t));
        block[1 + rest] = 0x80;
    }
    block[0] = length * 32u;
    block[15] = length * 8u | 1u;
    Md5Block(state, block);
    std::memcpy(digest, state, sizeof(state));
}
