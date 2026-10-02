#include "CKJitBuilder.h"
#include "CKJitDxbc.h"
#include "TestTriangleMultiset.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

// Structural checks of the DXBC backend. Every compiled container is decoded
// and checked for a valid digest, declared registers and temporaries that are
// written before they are read. With a directory argument every container is
// also written there as <name>.dxbc for the D3D12 validator.

namespace {

// The shader model 5.1 numbers the checks look for.
enum {
    kOpAdd = 0,
    kOpAnd = 1,
    kOpBreakc = 3,
    kOpDiscard = 13,
    kOpDiv = 14,
    kOpDp3 = 16,
    kOpElse = 18,
    kOpEndIf = 21,
    kOpEndLoop = 22,
    kOpEq = 24,
    kOpExp = 25,
    kOpFtoi = 27,
    kOpGe = 29,
    kOpIadd = 30,
    kOpIf = 31,
    kOpIeq = 32,
    kOpIge = 33,
    kOpIlt = 34,
    kOpImax = 36,
    kOpImin = 37,
    kOpImul = 38,
    kOpIne = 39,
    kOpIshr = 42,
    kOpItof = 43,
    kOpLd = 45,
    kOpLog = 47,
    kOpLoop = 48,
    kOpLt = 49,
    kOpMin = 51,
    kOpMax = 52,
    kOpMov = 54,
    kOpMovc = 55,
    kOpMul = 56,
    kOpNe = 57,
    kOpNot = 59,
    kOpOr = 60,
    kOpResinfo = 61,
    kOpRet = 62,
    kOpRoundNe = 64,
    kOpRoundNi = 65,
    kOpRoundPi = 66,
    kOpSample = 69,
    kOpSampleC = 70,
    kOpSampleCLz = 71,
    kOpSampleL = 72,
    kOpSampleD = 73,
    kOpSampleB = 74,
    kOpSqrt = 75,
    kOpUdiv = 78,
    kOpXor = 87,
    kOpDclResource = 88,
    kOpDclConstantBuffer = 89,
    kOpDclSampler = 90,
    kOpDclInputPs = 98,
    kOpDclInputPsSiv = 100,
    kOpDclOutput = 101,
    kOpDclTemps = 104,
    kOpDclGlobalFlags = 106,
    kOpLod = 108,
    kOpDerivRtxCoarse = 122,
    kOpDerivRtyCoarse = 124,

    kOperandTemp = 0,
    kOperandInput = 1,
    kOperandOutput = 2,
    kOperandImmediate32 = 4,
    kOperandSampler = 6,
    kOperandResource = 7,
    kOperandConstantBuffer = 8,
    kOperandNull = 13,

    kSelectMask = 0,
    kSelectSwizzle = 1,
    kSelectOne = 2,
    kModifierNeg = 1,
    kModifierAbs = 2,

    kInterpolationConstant = 1,
    kInterpolationLinear = 2,
    kInterpolationLinearNoPerspective = 4,
    kResourceTexture2D = 3,
    kResourceTexture3D = 5,
    kResourceTextureCube = 6,
    kResinfoUint = 2 << 11,
    kSamplerComparison = 1,
    kSaturate = 1 << 13,
    kTestNonZero = 1 << 18,
};

// The SDL_gpu fragment ABI: uniforms in space 3, samplers in space 2.
const CKJitResourceLayout kLayout = {3, 0, 2};

const CKJitInput kColor0 = {0, 4, CKJIT_INPUT_SMOOTH};
const CKJitInput kFlatColor0 = {2, 4, CKJIT_INPUT_FLAT};
const CKJitInput kTexCoord0 = {4, 2, CKJIT_INPUT_SMOOTH};
const CKJitInput kTexCoord1 = {5, 3, CKJIT_INPUT_SMOOTH};
const CKJitInput kFragCoord = {0, 4, CKJIT_INPUT_FRAG_COORD};

// FXC 10.1 output (fxc /T ps_5_1 /Qstrip_reflect) for
//   float4 main(float4 p : SV_Position) : SV_Target { return p; }
const uint32_t kFxcPassThrough[] = {
    0x43425844, 0xb809cf08, 0x8a1b4bff, 0xad1ac64e, 0xe85d2f30, 0x00000001, 0x000000dc, 0x00000003, 0x0000002c,
    0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001,
    0x00000003, 0x00000000, 0x00000f0f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x4e47534f, 0x0000002c, 0x00000001,
    0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x65677261,
    0xabab0074, 0x58454853, 0x00000040, 0x00000051, 0x00000010, 0x0100086a, 0x04002064, 0x001010f2, 0x00000000,
    0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000,
    0x0100003e,
};

// FXC output for
//   float4 main(float4 p : SV_Position) : SV_Target { return float4(p.xy * 2, 0, 1); }
// whose digest input ends 15 dwords into a block.
const uint32_t kFxcLongTail[] = {
    0x43425844, 0x6c75e843, 0x9d1c625a, 0x1c1c8196, 0x0417a0c8, 0x00000001, 0x00000110, 0x00000003, 0x0000002c,
    0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001,
    0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x4e47534f, 0x0000002c, 0x00000001,
    0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x65677261,
    0xabab0074, 0x58454853, 0x00000074, 0x00000051, 0x0000001d, 0x0100086a, 0x04002064, 0x00101032, 0x00000000,
    0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x0a000038, 0x00102032, 0x00000000, 0x00101046, 0x00000000,
    0x00004002, 0x40000000, 0x40000000, 0x00000000, 0x00000000, 0x08000036, 0x001020c2, 0x00000000, 0x00004002,
    0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e,
};

const char *g_ContainerDirectory = nullptr;

uint32_t FourCC(const char *code) {
    uint32_t value;
    std::memcpy(&value, code, sizeof(value));
    return value;
}

struct Chunk {
    uint32_t Tag;
    const uint32_t *Body;
    uint32_t Size; // dwords
};

// Chunk view of a container; the words must outlive it.
class Container {
public:
    explicit Container(const XArray<uint32_t> &words) : m_WellFormed(false) {
        const uint32_t count = (uint32_t)words.Size();
        if (count < 8 || words[0] != FourCC("DXBC") || words[5] != 1 || words[6] != count * 4)
            return;
        uint32_t expected = 8 + words[7];
        for (uint32_t i = 0; i < words[7]; ++i) {
            const uint32_t offset = words[8 + (int)i];
            if (offset != expected * 4 || expected + 2 > count || expected + 2 + words[(int)expected + 1] / 4 > count)
                return;
            m_Chunks.PushBack(Chunk{words[(int)expected], words.Begin() + expected + 2, words[(int)expected + 1] / 4});
            expected += 2 + words[(int)expected + 1] / 4;
        }
        m_WellFormed = expected == count;
    }

    bool WellFormed() const { return m_WellFormed; }
    int Size() const { return m_Chunks.Size(); }
    const Chunk &operator[](int i) const { return m_Chunks[i]; }

    const Chunk *Find(const char *tag) const {
        for (int i = 0; i < m_Chunks.Size(); ++i) {
            if (m_Chunks[i].Tag == FourCC(tag))
                return &m_Chunks[i];
        }
        return nullptr;
    }

private:
    XArray<Chunk> m_Chunks;
    bool m_WellFormed;
};

struct Element {
    const char *Name;
    uint32_t NameOffset;
    uint32_t SemanticIndex;
    uint32_t SystemValue;
    uint32_t ComponentType;
    uint32_t Register;
    uint32_t Mask;
    uint32_t ReadWriteMask;
};

// Elements of a signature chunk; the chunk must outlive them.
bool ReadSignature(const Chunk *chunk, XArray<Element> &elements) {
    elements.Clear();
    if (!chunk || chunk->Size < 2 || chunk->Body[1] != 8 || 2 + chunk->Body[0] * 6 > chunk->Size)
        return false;
    const char *bytes = (const char *)chunk->Body;
    for (uint32_t i = 0; i < chunk->Body[0]; ++i) {
        const uint32_t *e = chunk->Body + 2 + i * 6;
        if (e[0] >= chunk->Size * 4 || !std::memchr(bytes + e[0], 0, chunk->Size * 4 - e[0]))
            return false;
        elements.PushBack(Element{bytes + e[0], e[0], e[1], e[2], e[3], e[4], e[5] & 0xff, e[5] >> 8 & 0xff});
    }
    return true;
}

struct Operand {
    uint32_t Type;
    uint32_t Components; // 0, 1 or 4
    uint32_t Selection;  // for four components
    uint32_t Selector;   // mask, swizzle or component
    uint32_t Modifier;
    uint32_t Indices[3];
    uint32_t IndexCount;
    uint32_t Values[4]; // immediates
    uint32_t ValueCount;

    // The register components an instruction source reads: unread positions
    // repeat a read component, so every swizzle component is read.
    uint32_t Lanes() const {
        if (Components == 1)
            return 1;
        if (Selection == kSelectMask)
            return Selector;
        if (Selection == kSelectOne)
            return 1u << Selector;
        uint32_t lanes = 0;
        for (uint32_t p = 0; p < 4; ++p)
            lanes |= 1u << (Selector >> (2 * p) & 3);
        return lanes;
    }

    bool Is(uint32_t type, uint32_t index) const { return Type == type && IndexCount >= 1 && Indices[0] == index; }
};

bool DecodeOperand(const uint32_t *&cursor, const uint32_t *end, Operand &operand) {
    std::memset(&operand, 0, sizeof(operand));
    if (cursor == end)
        return false;
    const uint32_t token = *cursor++;
    operand.Type = token >> 12 & 0xff;
    operand.Components = (token & 3) == 2 ? 4 : token & 3;
    operand.Selection = token >> 2 & 3;
    operand.Selector = operand.Selection == kSelectOne ? token >> 4 & 3 : token >> 4 & 0xff;
    operand.IndexCount = token >> 20 & 3;
    if ((token >> 22 & 0x1ff) != 0) // only immediate indices
        return false;
    if (token >> 31) {
        if (cursor == end || (*cursor & 0x3f) != 1 || (*cursor >> 31) != 0)
            return false;
        operand.Modifier = *cursor++ >> 6 & 0xff;
    }
    if (operand.Type == kOperandImmediate32) {
        operand.ValueCount = operand.Components;
        for (uint32_t k = 0; k < operand.ValueCount; ++k) {
            if (cursor == end)
                return false;
            operand.Values[k] = *cursor++;
        }
        return operand.IndexCount == 0;
    }
    for (uint32_t k = 0; k < operand.IndexCount; ++k) {
        if (cursor == end)
            return false;
        operand.Indices[k] = *cursor++;
    }
    return true;
}

// XArray relocates its elements bytewise, so instructions hold their
// operands inline.
const uint32_t kMaxOperands = 6;

struct Instruction {
    uint32_t Opcode;
    uint32_t Controls; // the opcode token without opcode and length
    const uint32_t *Tokens;
    uint32_t Length;
    Operand Operands[kMaxOperands]; // code only
    uint32_t OperandCount;
};

// The backend declares with the shader model 4 declarations only; later
// opcodes, like the shader model 5 derivatives, are code again.
bool IsDeclaration(uint32_t opcode) { return opcode >= kOpDclResource && opcode <= kOpDclGlobalFlags; }

// Instruction view of the SHEX chunk; the chunk must outlive it.
class Program {
public:
    explicit Program(const Chunk *chunk) : m_WellFormed(false), m_FirstCode(-1) {
        if (!chunk || chunk->Size < 2 || chunk->Body[0] != 0x51 || chunk->Body[1] != chunk->Size)
            return;
        const uint32_t *end = chunk->Body + chunk->Size;
        for (const uint32_t *at = chunk->Body + 2; at < end;) {
            const uint32_t length = at[0] >> 24 & 0x7f;
            if (length == 0 || at + length > end || (at[0] >> 31) != 0)
                return;
            Instruction instruction;
            std::memset(&instruction, 0, sizeof(instruction));
            instruction.Opcode = at[0] & 0x7ff;
            instruction.Controls = at[0] & 0x00fff800u;
            instruction.Tokens = at + 1;
            instruction.Length = length;
            if (!IsDeclaration(instruction.Opcode)) {
                for (const uint32_t *cursor = at + 1; cursor < at + length;) {
                    if (instruction.OperandCount == kMaxOperands ||
                        !DecodeOperand(cursor, at + length, instruction.Operands[instruction.OperandCount++]))
                        return;
                }
            } else if (m_FirstCode >= 0) {
                return; // declarations precede the code
            }
            if (!IsDeclaration(instruction.Opcode) && m_FirstCode < 0)
                m_FirstCode = m_Instructions.Size();
            m_Instructions.PushBack(instruction);
            at += length;
        }
        m_WellFormed = m_FirstCode >= 0 && m_Instructions[m_Instructions.Size() - 1].Opcode == kOpRet;
    }

    bool WellFormed() const { return m_WellFormed; }
    int Size() const { return m_Instructions.Size(); }
    int FirstCode() const { return m_FirstCode; }
    const Instruction &operator[](int i) const { return m_Instructions[i]; }

    int Count(uint32_t opcode, uint32_t controls = 0) const {
        int count = 0;
        for (int i = 0; i < m_Instructions.Size(); ++i)
            count += m_Instructions[i].Opcode == opcode && (m_Instructions[i].Controls & controls) == controls ? 1 : 0;
        return count;
    }

    int Find(uint32_t opcode, int from = 0) const {
        for (int i = from; i < m_Instructions.Size(); ++i) {
            if (m_Instructions[i].Opcode == opcode)
                return i;
        }
        return -1;
    }

    int FindLast(uint32_t opcode) const {
        for (int i = m_Instructions.Size() - 1; i >= 0; --i) {
            if (m_Instructions[i].Opcode == opcode)
                return i;
        }
        return -1;
    }

    // Declarations of an input register, or -1.
    int FindInputDeclaration(uint32_t reg) const {
        for (int i = 0; i < m_FirstCode; ++i) {
            const Instruction &dcl = m_Instructions[i];
            if ((dcl.Opcode == kOpDclInputPs || dcl.Opcode == kOpDclInputPsSiv) && dcl.Tokens[1] == reg)
                return i;
        }
        return -1;
    }

    // Instructions with a source operand carrying the modifier.
    int CountModified(uint32_t opcode, uint32_t modifier) const {
        int count = 0;
        for (int i = m_FirstCode; i < m_Instructions.Size(); ++i) {
            const Instruction &instruction = m_Instructions[i];
            bool modified = false;
            for (uint32_t k = 1; k < instruction.OperandCount; ++k)
                modified = modified || instruction.Operands[k].Modifier == modifier;
            count += instruction.Opcode == opcode && modified ? 1 : 0;
        }
        return count;
    }

private:
    XArray<Instruction> m_Instructions;
    bool m_WellFormed;
    int m_FirstCode;
};

// How many leading operands are destinations: imul and udiv write two
// results, the first of which the backend discards.
uint32_t DestinationCount(uint32_t opcode) {
    if (opcode == kOpDiscard || opcode == kOpRet || opcode == kOpIf || opcode == kOpElse || opcode == kOpEndIf ||
        opcode == kOpLoop || opcode == kOpBreakc || opcode == kOpEndLoop)
        return 0;
    return opcode == kOpImul || opcode == kOpUdiv ? 2 : 1;
}

// Temporary components written on every path to a point of the program.
struct Written {
    uint8_t Lanes[64];
};

// What was written before a region, and on its then arm; for a loop, on
// every path breaking out of it.
struct RegionWrites {
    Written Before;
    Written Then; // or where a loop breaks
    bool InElse;
    bool IsLoop;
    bool Breaks;
};

// What the runtime and the driver rely on: the digest, the chunks, declared
// inputs, resources and temporaries, balanced regions and loops, and
// temporaries written on every path before they are read. A loop's body sees
// what was written before it, as later iterations only add writes; only its
// breaks leave it.
void CheckProgram(const XArray<uint32_t> &words) {
    uint32_t digest[4];
    CKJitDxbcDigest(words.Begin(), (uint32_t)words.Size(), digest);
    TestCheck(std::memcmp(digest, words.Begin() + 1, sizeof(digest)) == 0, "the container is signed");

    const Container container(words);
    TestCheck(container.WellFormed() && container.Size() == 3, "the chunks cover the container");
    if (!container.WellFormed() || container.Size() != 3)
        return;
    TestCheck(container[0].Tag == FourCC("ISGN") && container[1].Tag == FourCC("OSGN") &&
                  container[2].Tag == FourCC("SHEX"),
              "input and output signatures precede the program");
    XArray<Element> inputs, outputs;
    TestCheck(ReadSignature(container.Find("ISGN"), inputs) && ReadSignature(container.Find("OSGN"), outputs),
              "the signatures are well formed");
    TestCheck(outputs.Size() == 1 && std::strcmp(outputs[0].Name, "SV_Target") == 0 && outputs[0].Register == 0 &&
                  outputs[0].Mask == 0xf && outputs[0].ReadWriteMask == 0,
              "the output is SV_Target 0");
    for (int i = 1; i < inputs.Size(); ++i)
        TestCheck(inputs[i - 1].Register < inputs[i].Register, "inputs are in register order");

    const Program program(container.Find("SHEX"));
    TestCheck(program.WellFormed(), "the program decodes and returns");
    if (!program.WellFormed())
        return;

    uint32_t temps = 0, inputMasks[32] = {}, samplers = 0, resources = 0;
    uint32_t uniforms = 0, uniformRegisters[CKJIT_MAX_UNIFORM_BUFFERS] = {};
    uint32_t uniformRows[CKJIT_MAX_UNIFORM_BUFFERS] = {};
    for (int i = 0; i < program.FirstCode(); ++i) {
        const Instruction &dcl = program[i];
        switch (dcl.Opcode) {
        case kOpDclTemps: temps = dcl.Tokens[0]; break;
        case kOpDclConstantBuffer:
            TestCheck(dcl.Tokens[1] == uniforms && uniforms < CKJIT_MAX_UNIFORM_BUFFERS &&
                          dcl.Tokens[2] == dcl.Tokens[3] &&
                          (uniforms == 0 || dcl.Tokens[2] > uniformRegisters[uniforms - 1]),
                      "uniform blocks are single registers, in order, with dense range ids");
            if (uniforms < CKJIT_MAX_UNIFORM_BUFFERS) {
                uniformRegisters[uniforms] = dcl.Tokens[2];
                uniformRows[uniforms++] = dcl.Tokens[4];
            }
            break;
        case kOpDclSampler: samplers |= 1u << dcl.Tokens[2]; break;
        case kOpDclResource: resources |= 1u << dcl.Tokens[2]; break;
        case kOpDclInputPs:
        case kOpDclInputPsSiv:
            if (dcl.Tokens[1] < 32)
                inputMasks[dcl.Tokens[1]] = dcl.Tokens[0] >> 4 & 0xf;
            break;
        default: break;
        }
    }
    TestCheck(program.Find(kOpDclGlobalFlags) == 0, "the global flags come first");
    TestCheck(program.Count(kOpDclOutput) == 1, "the output is declared");
    for (int i = 0; i < inputs.Size(); ++i) {
        TestCheck(inputs[i].Register < 32 && inputs[i].ReadWriteMask == inputMasks[inputs[i].Register],
                  "an input declares the components its signature reads");
    }

    Written written = {};
    XArray<RegionWrites> regions;
    uint32_t output = 0;
    for (int i = program.FirstCode(); i < program.Size(); ++i) {
        const Instruction &instruction = program[i];
        if (instruction.Opcode == kOpElse || instruction.Opcode == kOpEndIf) {
            TestCheck(regions.Size() != 0 && !regions.Back().IsLoop &&
                          !regions.Back().InElse == (instruction.Opcode == kOpElse),
                      "regions are balanced");
            if (regions.Size() == 0 || regions.Back().IsLoop)
                continue;
            RegionWrites &region = regions.Back();
            if (instruction.Opcode == kOpElse) {
                region.Then = written;
                written = region.Before;
                region.InElse = true;
            } else {
                const Written &other = region.InElse ? region.Then : region.Before;
                for (uint32_t r = 0; r < 64; ++r)
                    written.Lanes[r] &= other.Lanes[r];
                regions.PopBack();
            }
            continue;
        }
        if (instruction.Opcode == kOpLoop) {
            TestCheck(instruction.OperandCount == 0, "a loop has no operands");
            const RegionWrites loop = {written, {}, false, true, false};
            regions.PushBack(loop);
            continue;
        }
        if (instruction.Opcode == kOpEndLoop) {
            TestCheck(instruction.OperandCount == 0 && regions.Size() != 0 && regions.Back().IsLoop &&
                          regions.Back().Breaks,
                      "loops are balanced and break");
            if (regions.Size() == 0 || !regions.Back().IsLoop)
                continue;
            written = regions.Back().Then;
            regions.PopBack();
            continue;
        }
        const uint32_t first = DestinationCount(instruction.Opcode);
        for (uint32_t k = first; k < instruction.OperandCount; ++k) {
            const Operand &source = instruction.Operands[k];
            switch (source.Type) {
            case kOperandTemp:
                TestCheck(source.Indices[0] < temps && source.Indices[0] < 64 &&
                              (source.Lanes() & ~written.Lanes[source.Indices[0] & 63]) == 0,
                          "temporaries are written before they are read");
                break;
            case kOperandInput: {
                bool declared = false;
                for (int e = 0; e < inputs.Size(); ++e)
                    declared = declared || inputs[e].Register == source.Indices[0];
                TestCheck(declared && source.Indices[0] < 32 &&
                              (source.Lanes() & ~inputMasks[source.Indices[0] & 31]) == 0,
                          "inputs are read within their declarations");
                break;
            }
            case kOperandConstantBuffer:
                TestCheck(source.IndexCount == 3 && source.Indices[0] < uniforms &&
                              source.Indices[1] == uniformRegisters[source.Indices[0] % CKJIT_MAX_UNIFORM_BUFFERS] &&
                              source.Indices[2] < uniformRows[source.Indices[0] % CKJIT_MAX_UNIFORM_BUFFERS],
                          "uniform rows are within the declared block");
                break;
            case kOperandResource:
                TestCheck(source.IndexCount == 2 && (resources >> (source.Indices[1] & 31) & 1) != 0,
                          "sampled textures are declared");
                break;
            case kOperandSampler:
                TestCheck(source.IndexCount == 2 && (samplers >> (source.Indices[1] & 31) & 1) != 0,
                          "samplers are declared");
                break;
            case kOperandImmediate32: break;
            default: TestCheck(false, "sources are registers, uniforms, resources or immediates"); break;
            }
        }
        if (first == 2) {
            TestCheck(instruction.Operands[0].Type == kOperandNull && instruction.Operands[0].Components == 0 &&
                          instruction.Operands[0].IndexCount == 0,
                      "the discarded result is null");
        }
        if (first > 0 && instruction.OperandCount >= first) {
            const Operand &dest = instruction.Operands[first - 1];
            TestCheck(dest.Components == 4 && dest.Selection == kSelectMask && dest.Selector != 0 &&
                          dest.Modifier == 0,
                      "destinations are written through a mask");
            if (dest.Type == kOperandTemp) {
                TestCheck(dest.Indices[0] < temps, "temporaries are declared");
                written.Lanes[dest.Indices[0] & 63] |= (uint8_t)dest.Selector;
            } else {
                TestCheck(dest.Is(kOperandOutput, 0), "only temporaries and o0 are written");
                output |= dest.Selector;
            }
        }
        if (instruction.Opcode == kOpIf || instruction.Opcode == kOpBreakc) {
            TestCheck(instruction.OperandCount == 1 && (instruction.Operands[0].Components == 1 ||
                                                        instruction.Operands[0].Selection == kSelectOne),
                      "a region or break tests one component");
        }
        if (instruction.Opcode == kOpIf) {
            const RegionWrites region = {written, {}, false, false, false};
            regions.PushBack(region);
        }
        if (instruction.Opcode == kOpBreakc) {
            int loop = regions.Size() - 1;
            while (loop >= 0 && !regions[loop].IsLoop)
                --loop;
            TestCheck(loop >= 0, "breaks are in loops");
            if (loop < 0)
                continue;
            RegionWrites &exits = regions[loop];
            for (uint32_t r = 0; r < 64; ++r)
                exits.Then.Lanes[r] = exits.Breaks ? (uint8_t)(exits.Then.Lanes[r] & written.Lanes[r]) : written.Lanes[r];
            exits.Breaks = true;
        }
    }
    TestCheck(regions.Size() == 0, "every region and loop ends");
    TestCheck(output == 0xf, "every colour component is written");
}

void Save(const char *name, const XArray<uint32_t> &words) {
    if (!g_ContainerDirectory)
        return;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.dxbc", g_ContainerDirectory, name);
    FILE *file = std::fopen(path, "wb");
    TestCheck(file != nullptr, "the container file opens");
    if (file) {
        TestCheck(std::fwrite(words.Begin(), sizeof(uint32_t), (size_t)words.Size(), file) == (size_t)words.Size(),
                  "the container is written");
        std::fclose(file);
    }
}

bool Compile(const CKJitBuilder &b, CKJitValue color, CKJitValue discard, XArray<uint32_t> &words,
             const CKJitResourceLayout &layout = kLayout) {
    CKJitFragmentShader shader;
    if (!b.Finish(color, discard, shader) || !CKJitEmitDxbc(shader, layout, words))
        return false;
    CheckProgram(words);
    return true;
}

// Every IR operation, all reachable from the outputs.
void BuildEveryOperation(CKJitBuilder &b, CKJitValue &color, CKJitValue &discard) {
    const CKJitValue diffuse = b.Input(kColor0);
    const CKJitValue flat = b.Input(kFlatColor0);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue direction = b.Input(kTexCoord1);
    const CKJitValue position = b.Input(kFragCoord);
    const CKJitValue tint = b.Uniform(36);
    const CKJitValue params = b.Uniform(37);
    const CKJitValue bias = b.Component(params, 0);

    const CKJitValue base = b.Sample(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    const CKJitValue cube = b.Sample(9, CKJIT_SAMPLER_CUBE, direction, bias);
    const CKJitValue volume = b.Sample(13, CKJIT_SAMPLER_3D, b.Saturate(direction), bias);
    const CKJitValue lodSample = b.SampleLevel(9, CKJIT_SAMPLER_CUBE, direction, b.CalcLod(0, CKJIT_SAMPLER_2D, uv));
    const CKJitValue gradSample =
        b.SampleGrad(13, CKJIT_SAMPLER_3D, direction, b.Swizzle(tint, "xyz"), b.Neg(b.Swizzle(params, "xyz")));
    const CKJitValue shadow = b.Mul(b.SampleCmp(5, uv, b.Component(params, 1)),
                                    b.SampleCmpLevelZero(5, b.Swizzle(direction, "xy"), b.Component(tint, 0)));

    CKJitValue value = b.Add(b.Mul(base, tint), b.Sub(cube, volume));
    value = b.Add(value, b.Mul(lodSample, gradSample));
    value = b.Div(value, b.Max(b.Abs(flat), b.Float(0.001f)));
    value = b.Min(value, b.Neg(diffuse));
    const CKJitValue rounded = b.Add(b.RoundEven(value), b.Mul(b.Floor(diffuse), b.Ceil(tint)));
    const CKJitValue shade = b.Dot(b.Swizzle(diffuse, "xyz"), direction);
    const CKJitValue spot = b.Sqrt(b.Exp2(b.Log2(b.Component(params, 1))));
    const CKJitValue slope = b.Add(b.Ddx(uv), b.Ddy(b.Neg(b.Swizzle(direction, "xy"))));
    const CKJitValue assembled = b.Construct({b.Add(b.Swizzle(value, "xy"), slope), shade, spot});

    const CKJitValue lanes = b.FloatToInt(b.Component(params, 2));
    const CKJitValue shifted = b.IntShiftRight(lanes, b.FloatToInt(b.Component(params, 3)));
    const CKJitValue bit = b.IntEqual(b.IntAnd(b.IntShiftRight(lanes, b.Int(33)), b.Int(1)), b.Int(1));
    const CKJitValue less = b.Less(shade, spot);
    const CKJitValue lessEqual = b.LessEqual(b.Component(position, 0), b.Component(position, 1));
    const CKJitValue equal = b.Equal(shade, b.Component(tint, 3));
    const CKJitValue notEqual = b.NotEqual(spot, b.Component(tint, 2));
    const CKJitValue picked = b.Select(less, equal, notEqual);
    const CKJitValue mask = b.Select(lessEqual, shifted, b.Int(7));

    const CKJitValue texel = b.FloatToInt(b.Swizzle(position, "xy"));
    const CKJitValue extent = b.IntMax(b.FloatToInt(b.Swizzle(tint, "xy")), b.Int(1));
    const CKJitValue wrapped = b.IntMod(b.IntAdd(b.IntMul(texel, extent), b.Int(3)), extent);
    const CKJitValue offset = b.IntMin(b.IntSub(wrapped, texel), b.Int(255));
    const CKJitValue inside = b.All(b.IntLessEqual(offset, extent));
    const CKJitValue moved = b.Any(b.IntNotEqual(offset, texel));
    const CKJitValue low = b.IntLess(b.Component(wrapped, 0), b.Int(16));
    const CKJitValue fetched = b.Load(0, CKJIT_SAMPLER_2D, b.Construct({wrapped, mask}));
    const CKJitValue slice = b.Load(13, CKJIT_SAMPLER_3D, b.Construct({wrapped, lanes, mask}));
    const CKJitValue extents = b.Construct({b.TextureSize(9, CKJIT_SAMPLER_CUBE, mask),
                                            b.Component(b.TextureSize(13, CKJIT_SAMPLER_3D, lanes), 2),
                                            b.TextureLevels(0, CKJIT_SAMPLER_2D)});
    const CKJitValue texels = b.Add(b.Mul(fetched, slice), b.IntToFloat(extents));

    const CKJitValue flags = b.Or(b.And(picked, b.Not(bit)), b.IntEqual(mask, b.Int(3)));
    const CKJitValue condition = b.Or(flags, b.And(b.And(inside, moved), low));
    const CKJitValue alpha = b.Select(condition, shade, b.Mul(spot, shadow));
    const CKJitValue ramp = b.Construct({b.IntToFloat(offset), b.Swizzle(rounded, "zw")});
    const CKJitValue graded = b.Select(b.Less(assembled, diffuse), assembled, ramp);
    color = b.Mul(b.Select(condition, graded, b.Add(rounded, texels)), alpha);
    discard = b.Less(b.Component(color, 3), b.Component(position, 2));
}

// Two regions, one in the other's then arm, with PHI results of every kind
// and a select of values from before them. The outer condition is uniform, so
// its then arm samples.
void BuildRegions(CKJitBuilder &b, CKJitValue &color, CKJitValue &discard) {
    const CKJitValue diffuse = b.Input(kColor0);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue params = b.Uniform(0);
    const CKJitValue scale = b.Mul(diffuse, b.Uniform(1));
    b.If(b.Less(b.Component(params, 0), b.Float(0.5f)));
    const CKJitValue texel = b.Sample(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    const CKJitValue shaded = b.Mul(texel, scale);
    b.If(b.Less(b.Component(shaded, 3), b.Component(params, 1)));
    const CKJitValue dimmed = b.Mul(shaded, b.Component(params, 2));
    b.Else({dimmed});
    const CKJitValue inner = b.EndIf(shaded);
    const CKJitValue count = b.FloatToInt(b.Component(inner, 0));
    const CKJitValue faint = b.Less(b.Component(texel, 3), b.Float(0.25f));
    b.Else({inner, count, faint, scale});
    const CKJitValue fallback = b.Add(scale, b.Uniform(2));
    CKJitValue results[4];
    b.EndIf({fallback, b.Int(3), b.Bool(false), diffuse}, results);
    color = b.Mul(b.Add(results[0], results[3]), b.IntToFloat(results[1]));
    discard = results[2];
}

// A loop nesting a region and another loop, with carried values of every
// kind: a pair swapping each iteration, one whose next is from before the loop
// and one the nested loop starts. The body reads a step computed before the
// loop. A loop whose constant count exceeds its bound follows. The outer count
// is uniform, so its body samples.
void BuildLoops(CKJitBuilder &b, CKJitValue &color, CKJitValue &discard) {
    const CKJitValue diffuse = b.Input(kColor0);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue params = b.Uniform(0);
    const CKJitValue step = b.Mul(b.Swizzle(b.Uniform(1), "xy"), b.Swizzle(params, "zw"));
    const CKJitValue decay = b.Component(params, 1);
    const CKJitValue swapped = b.Swizzle(uv, "yx");
    CKJitValue carried[5];
    const CKJitValue index =
        b.Loop(b.FloatToInt(b.Component(params, 0)), 8, {diffuse, b.Float(1.0f), uv, swapped, b.Bool(false)}, carried);
    const CKJitValue offset = b.Add(carried[2], b.Mul(step, b.Splat(b.IntToFloat(index), 2)));
    const CKJitValue texel = b.Sample(0, CKJIT_SAMPLER_2D, offset, b.Float(0.0f));
    b.If(b.IntLess(index, b.Int(2)));
    const CKJitValue near = b.Mul(texel, b.Splat(carried[1], 4));
    b.Else({near});
    const CKJitValue weighted = b.EndIf(texel);
    CKJitValue taps;
    const CKJitValue tap = b.Loop(b.FloatToInt(b.Mul(b.Component(texel, 3), b.Float(4.0f))), 4, {index}, &taps);
    const CKJitValue counted = b.EndLoop(b.IntAdd(taps, tap));
    const CKJitValue sum = b.Add(carried[0], b.Mul(weighted, b.Splat(b.IntToFloat(counted), 4)));
    const CKJitValue faint = b.Or(carried[4], b.Less(b.Component(texel, 3), b.Float(0.25f)));
    CKJitValue results[5];
    b.EndLoop({sum, decay, carried[3], carried[2], faint}, results);
    CKJitValue fade;
    b.Loop(b.Int(6), 3, {results[1]}, &fade);
    const CKJitValue faded = b.EndLoop(b.Mul(fade, decay));
    color = b.Mul(b.Add(results[0], b.Construct({results[2], results[3]})), b.Splat(faded, 4));
    discard = results[4];
}

void TestContainerLayout() {
    CKJitBuilder b(4);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Input(kColor0), CKJitValue(), words), "a pass-through shader compiles");
    Save("pass_through", words);

    const Container container(words);
    TestCheck(container.WellFormed(), "the container is well formed");
    TestCheck(words[5] == 1 && words[7] == 3 && words[8] == 44, "version 1, three chunks after the header");
    const Program program(container.Find("SHEX"));
    TestCheck(program.WellFormed() && program.Size() - program.FirstCode() == 2 && program.Count(kOpMov) == 1,
              "the program moves the input to the output and returns");
    TestCheck(program.Count(kOpDclTemps) == 0 && program.Count(kOpDclConstantBuffer) == 0 &&
                  program.Count(kOpDclSampler) == 0 && program.Count(kOpDclResource) == 0,
              "unused resources are not declared");
}

void TestDigest() {
    const struct {
        const uint32_t *Words;
        uint32_t Count;
    } references[] = {
        {kFxcPassThrough, sizeof(kFxcPassThrough) / sizeof(uint32_t)},
        {kFxcLongTail, sizeof(kFxcLongTail) / sizeof(uint32_t)},
    };
    for (const auto &reference : references) {
        uint32_t digest[4];
        CKJitDxbcDigest(reference.Words, reference.Count, digest);
        TestCheck(std::memcmp(digest, reference.Words + 1, sizeof(digest)) == 0, "the digest matches FXC's");
    }
    TestCheck((sizeof(kFxcPassThrough) / 4 - 5) % 16 < 14 && (sizeof(kFxcLongTail) / 4 - 5) % 16 >= 14,
              "the references cover both final block layouts");

    uint32_t tampered[sizeof(kFxcPassThrough) / sizeof(uint32_t)];
    std::memcpy(tampered, kFxcPassThrough, sizeof(tampered));
    tampered[sizeof(tampered) / 4 - 1] ^= 1;
    uint32_t digest[4];
    CKJitDxbcDigest(tampered, sizeof(tampered) / 4, digest);
    TestCheck(std::memcmp(digest, kFxcPassThrough + 1, sizeof(digest)) != 0, "the digest covers the program");
}

void TestMatchesFxc() {
    CKJitBuilder b(4);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Input(kFragCoord), CKJitValue(), words), "the position shader compiles");
    Save("position", words);
    TestCheck(words.Size() == (int)(sizeof(kFxcPassThrough) / sizeof(uint32_t)) &&
                  std::memcmp(words.Begin(), kFxcPassThrough, sizeof(kFxcPassThrough)) == 0,
              "the container is FXC's, byte for byte");
}

void TestInterface() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Input(kColor0);
    const CKJitValue flat = b.Input(kFlatColor0);
    b.Input(kTexCoord0); // declared, never read
    const CKJitValue position = b.Input(kFragCoord);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Add(b.Mul(color, flat), b.Swizzle(position, "xyww")), CKJitValue(), words),
              "the shader compiles");
    Save("interface", words);

    const Container container(words);
    XArray<Element> inputs;
    TestCheck(ReadSignature(container.Find("ISGN"), inputs) && inputs.Size() == 4, "every input is in the signature");
    if (inputs.Size() != 4)
        return;
    TestCheck(inputs[0].Register == 0 && inputs[1].Register == 1 && inputs[2].Register == 3 &&
                  inputs[3].Register == 5,
              "inputs keep their registers");
    TestCheck(std::strcmp(inputs[0].Name, "SV_Position") == 0 && inputs[0].SystemValue == 1 &&
                  inputs[1].SystemValue == 0,
              "the position is the POSITION system value");
    TestCheck(std::strcmp(inputs[1].Name, "TEXCOORD") == 0 && inputs[1].NameOffset == inputs[2].NameOffset &&
                  inputs[2].NameOffset == inputs[3].NameOffset,
              "a shared semantic name is stored once");
    TestCheck(inputs[1].SemanticIndex == 0 && inputs[2].SemanticIndex == 2 && inputs[3].SemanticIndex == 4,
              "semantic indices are kept");
    TestCheck(inputs[3].Mask == 0x3 && inputs[3].ReadWriteMask == 0, "an unread input is in the signature, unread");
    TestCheck(inputs[0].ReadWriteMask == 0xb && inputs[1].ReadWriteMask == 0xf,
              "the signature records the components read");

    const Program program(container.Find("SHEX"));
    const int position0 = program.FindInputDeclaration(0);
    const int color1 = program.FindInputDeclaration(1);
    const int flat3 = program.FindInputDeclaration(3);
    TestCheck(position0 >= 0 && program[position0].Opcode == kOpDclInputPsSiv &&
                  program[position0].Controls >> 11 == kInterpolationLinearNoPerspective &&
                  program[position0].Tokens[2] == 1,
              "the position is a noperspective POSITION input");
    TestCheck(color1 >= 0 && program[color1].Opcode == kOpDclInputPs &&
                  program[color1].Controls >> 11 == kInterpolationLinear,
              "smooth inputs are linear");
    TestCheck(flat3 >= 0 && program[flat3].Controls >> 11 == kInterpolationConstant, "flat inputs are constant");
    TestCheck(program.FindInputDeclaration(5) < 0, "unread inputs are not declared");
}

void TestResources() {
    CKJitBuilder b(89);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue direction = b.Input(kTexCoord1);
    const CKJitValue first = b.Uniform(0);
    const CKJitValue last = b.Uniform(88);
    CKJitValue color = b.Mul(b.Sample(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f)), first);
    color = b.Add(color, b.Sample(0, CKJIT_SAMPLER_2D, b.Swizzle(uv, "yx"), b.Float(0.0f)));
    color = b.Add(color, b.Sample(9, CKJIT_SAMPLER_CUBE, direction, b.Float(0.0f)));
    color = b.Mul(color, b.Sample(13, CKJIT_SAMPLER_3D, direction, b.Component(last, 0)));
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, CKJitValue(), words), "the shader compiles");
    Save("resources", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const int block = program.Find(kOpDclConstantBuffer);
    TestCheck(block >= 0 && program.Count(kOpDclConstantBuffer) == 1, "one uniform block");
    TestCheck(block >= 0 && program[block].Tokens[1] == 0 && program[block].Tokens[2] == 0 &&
                  program[block].Tokens[3] == 0 && program[block].Tokens[4] == 89 && program[block].Tokens[5] == 3,
              "the block is b0 in space 3 with every row");

    const uint32_t slots[] = {0, 9, 13};
    const uint32_t dimensions[] = {kResourceTexture2D, kResourceTextureCube, kResourceTexture3D};
    TestCheck(program.Count(kOpDclSampler) == 3 && program.Count(kOpDclResource) == 3,
              "one sampler and texture per used slot");
    for (uint32_t i = 0; i < 3; ++i) {
        const int sampler = program.Find(kOpDclSampler) + (int)i;
        const int texture = program.Find(kOpDclResource) + (int)i;
        TestCheck(program[sampler].Opcode == kOpDclSampler && program[sampler].Tokens[1] == i &&
                      program[sampler].Tokens[2] == slots[i] && program[sampler].Tokens[3] == slots[i] &&
                      program[sampler].Tokens[4] == 2,
                  "a sampler is its slot in space 2, with a dense range id");
        TestCheck(program[texture].Opcode == kOpDclResource && program[texture].Controls >> 11 == dimensions[i] &&
                      program[texture].Tokens[1] == i && program[texture].Tokens[2] == slots[i] &&
                      program[texture].Tokens[4] == 0x5555 && program[texture].Tokens[5] == 2,
                  "a float texture of the slot's dimension shares the slot");
    }
    for (int i = program.FirstCode(); i < program.Size(); ++i) {
        const Instruction &instruction = program[i];
        if (instruction.Opcode != kOpSample && instruction.Opcode != kOpSampleB)
            continue;
        const Operand &texture = instruction.Operands[2];
        const Operand &sampler = instruction.Operands[3];
        TestCheck(texture.Type == kOperandResource && sampler.Type == kOperandSampler &&
                      texture.Indices[0] == sampler.Indices[0] && texture.Indices[1] == sampler.Indices[1] &&
                      texture.Indices[0] < 3 && slots[texture.Indices[0]] == texture.Indices[1],
                  "a sample reads the texture and sampler of one slot");
    }

    CKJitBuilder moved(89);
    XArray<uint32_t> relocated;
    const CKJitValue sampled = moved.Sample(1, CKJIT_SAMPLER_2D, moved.Input(kTexCoord0), moved.Float(0.0f));
    TestCheck(Compile(moved, moved.Mul(sampled, moved.Uniform(5)), CKJitValue(), relocated, {7, 4, 1}),
              "another layout compiles");
    const Container otherContainer(relocated);
    const Program other(otherContainer.Find("SHEX"));
    const int otherBlock = other.Find(kOpDclConstantBuffer);
    const int otherSampler = other.Find(kOpDclSampler);
    const int multiply = other.Find(kOpMul);
    TestCheck(otherBlock >= 0 && other[otherBlock].Tokens[2] == 4 && other[otherBlock].Tokens[5] == 7 &&
                  otherSampler >= 0 && other[otherSampler].Tokens[2] == 1 && other[otherSampler].Tokens[4] == 1,
              "the resource layout places the block and the samplers");
    TestCheck(multiply >= 0 && other[multiply].Operands[2].Type == kOperandConstantBuffer &&
                  other[multiply].Operands[2].Indices[1] == 4 && other[multiply].Operands[2].Indices[2] == 5,
              "uniform reads address the block's register");
}

void TestUniformBuffers() {
    // Buffer 1 is never read.
    const uint32_t counts[] = {4, 6, 5};
    CKJitBuilder b(counts, 3);
    const CKJitValue first = b.Uniform(0, 3);
    const CKJitValue last = b.Uniform(2, 4);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Mul(first, last), CKJitValue(), words, {3, 1, 2}), "the shader compiles");
    Save("uniform_buffers", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const int block = program.Find(kOpDclConstantBuffer);
    TestCheck(block >= 0 && program.Count(kOpDclConstantBuffer) == 2, "a block per buffer read");
    if (block < 0 || program.Count(kOpDclConstantBuffer) != 2)
        return;
    const Instruction &zero = program[block];
    const Instruction &two = program[block + 1];
    TestCheck(zero.Tokens[1] == 0 && zero.Tokens[2] == 1 && zero.Tokens[3] == 1 && zero.Tokens[4] == 4 &&
                  zero.Tokens[5] == 3 && two.Tokens[1] == 1 && two.Tokens[2] == 3 && two.Tokens[3] == 3 &&
                  two.Tokens[4] == 5 && two.Tokens[5] == 3,
              "buffer b is register b1 + b in space 3, with a dense range id");
    int reads = 0;
    for (int i = program.FirstCode(); i < program.Size(); ++i) {
        const Instruction &instruction = program[i];
        for (uint32_t operand = 0; operand < instruction.OperandCount; ++operand) {
            const Operand &uniform = instruction.Operands[operand];
            if (uniform.Type != kOperandConstantBuffer)
                continue;
            ++reads;
            TestCheck((uniform.Indices[0] == 0 && uniform.Indices[1] == 1 && uniform.Indices[2] == 3) ||
                          (uniform.Indices[0] == 1 && uniform.Indices[1] == 3 && uniform.Indices[2] == 4),
                      "a read addresses its buffer's range and register");
        }
    }
    TestCheck(reads == 2, "each row is read once");
}

void TestLowering() {
    CKJitBuilder b(89);
    CKJitValue color, discard;
    BuildEveryOperation(b, color, discard);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, discard, words), "every operation compiles");
    Save("every_operation", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    TestCheck(program.Count(kOpMin) == 1 && program.Count(kOpMax) == 1 && program.Count(kOpDiv) == 1,
              "arithmetic maps to single instructions");
    TestCheck(program.Count(kOpMov, kSaturate) == 1, "saturate is a saturated move");
    TestCheck(program.Count(kOpRoundNi) == 1 && program.Count(kOpRoundPi) == 1 && program.Count(kOpRoundNe) == 1 &&
                  program.Count(kOpExp) == 1 && program.Count(kOpLog) == 1 && program.Count(kOpSqrt) == 1 &&
                  program.Count(kOpDp3) == 1,
              "unary functions and the dot product are single instructions");
    TestCheck(program.Count(kOpDerivRtxCoarse) == 1 && program.CountModified(kOpDerivRtyCoarse, kModifierNeg) == 1,
              "derivatives are coarse, like HLSL ddx and ddy, and take source modifiers");
    TestCheck(program.CountModified(kOpMax, kModifierAbs) == 1, "abs is a source modifier");
    TestCheck(program.CountModified(kOpMin, kModifierNeg) == 1 && program.CountModified(kOpAdd, kModifierNeg) == 1,
              "negation and subtraction use the negate modifier");
    TestCheck(program.Count(kOpSample) == 1 && program.Count(kOpSampleB) == 2,
              "a zero bias is omitted, others are sample_b");
    TestCheck(program.Count(kOpSampleL) == 1 && program.Count(kOpSampleD) == 1 && program.Count(kOpLod) == 1 &&
                  program.Count(kOpLd) == 2 && program.Count(kOpResinfo, kResinfoUint) == 3 &&
                  program.Count(kOpSampleC) == 1 && program.Count(kOpSampleCLz) == 1,
              "texture operations map to single instructions");
    TestCheck(program.Count(kOpMovc) == 5, "every select is one movc");
    TestCheck(program.Count(kOpLt) == 3 && program.Count(kOpEq) == 1 && program.Count(kOpNe) == 1 &&
                  program.Count(kOpGe) == 1,
              "comparisons map to single instructions");
    const int greaterEqual = program.Find(kOpGe);
    TestCheck(greaterEqual >= 0 && program[greaterEqual].Operands[1].Is(kOperandInput, 0) &&
                  program[greaterEqual].Operands[1].Lanes() == 0x2 &&
                  program[greaterEqual].Operands[2].Lanes() == 0x1,
              "a <= b is b >= a");
    TestCheck(program.Count(kOpIadd) == 3 && program.Count(kOpImul) == 1 && program.Count(kOpImin) == 1 &&
                  program.Count(kOpImax) == 1 && program.Count(kOpItof) == 2 && program.Count(kOpFtoi) == 4,
              "integer arithmetic maps to single instructions");
    TestCheck(program.CountModified(kOpIadd, kModifierNeg) == 1, "integer subtraction negates in two's complement");
    TestCheck(program.Count(kOpIlt) == 1 && program.Count(kOpIge) == 1 && program.Count(kOpIeq) == 2 &&
                  program.Count(kOpIne) == 1,
              "integer comparisons map to single instructions");
    TestCheck(program.Count(kOpUdiv) == 1 && program.Count(kOpXor) == 2, "the remainder divides non-negative values");
    // and: the mask, the boolean ands, the remainder's offset and the reduction
    // of all; or: the boolean ors and the reduction of any.
    TestCheck(program.Count(kOpAnd) == 6 && program.Count(kOpOr) == 3 && program.Count(kOpNot) == 1,
              "boolean operations and reductions map to single instructions");

    int shifts = 0, masked = 0, once = 0;
    for (int i = program.FirstCode(); i < program.Size(); ++i) {
        if (program[i].Opcode != kOpIshr)
            continue;
        ++shifts;
        const Operand &count = program[i].Operands[2];
        bool low = true;
        for (uint32_t k = 0; k < count.ValueCount; ++k)
            low = low && count.Values[k] < 32;
        masked += low ? 1 : 0;
        once += count.Type == kOperandImmediate32 && count.ValueCount == 1 && count.Values[0] == 1 ? 1 : 0;
    }
    TestCheck(shifts == 3 && masked == 3 && once == 1, "constant shift counts keep their low five bits");

    const int kill = program.Find(kOpDiscard);
    TestCheck(program.Count(kOpDiscard, kTestNonZero) == 1 && kill > program.FindLast(kOpSampleB) &&
                  kill > program.FindLast(kOpSample) && kill > program.FindLast(kOpSampleC) &&
                  kill > program.FindLast(kOpLod) && kill > program.FindLast(kOpDerivRtxCoarse) &&
                  kill > program.FindLast(kOpDerivRtyCoarse),
              "the discard follows every sample, comparison, LOD query and derivative");
    TestCheck(kill >= 0 && program[kill].Operands[0].Type == kOperandTemp && program[kill - 1].Opcode == kOpLt,
              "the discard tests the discard condition");
    TestCheck(kill >= 0 && kill + 3 == program.Size() && program[kill + 1].Opcode == kOpMov &&
                  program[kill + 1].Operands[0].Is(kOperandOutput, 0),
              "the colour is written after the discard");
}

// Whether a texture instruction's resource swizzle routes result component
// first + k to the k-th component its destination writes.
bool RoutesResult(const Instruction &instruction, uint32_t first) {
    const Operand &dest = instruction.Operands[0];
    const Operand &texture = instruction.Operands[2];
    if (instruction.OperandCount < 3 || texture.Type != kOperandResource || texture.Selection != kSelectSwizzle)
        return false;
    uint32_t k = first;
    for (uint32_t lane = 0; lane < 4; ++lane) {
        if ((dest.Selector >> lane & 1) != 0 && (texture.Selector >> (2 * lane) & 3) != k++)
            return false;
    }
    return true;
}

void TestTextureAccess() {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue direction = b.Input(kTexCoord1);
    const CKJitValue position = b.Input(kFragCoord);

    // Slots 0 and 6 are only loaded from and queried, 1 and 4 are sampled.
    const CKJitValue mip = b.IntSub(b.TextureLevels(0, CKJIT_SAMPLER_2D), b.Int(1));
    const CKJitValue last = b.IntSub(b.TextureSize(0, CKJIT_SAMPLER_2D, mip), b.Int(1));
    const CKJitValue texel = b.IntMin(b.FloatToInt(b.Swizzle(position, "xy")), last);
    const CKJitValue fetched = b.Load(0, CKJIT_SAMPLER_2D, b.Construct({texel, mip}));
    const CKJitValue slice = b.Load(6, CKJIT_SAMPLER_3D, b.Construct({b.FloatToInt(direction), b.Int(0)}));
    const CKJitValue lod = b.CalcLod(1, CKJIT_SAMPLER_2D, b.Neg(uv));
    const CKJitValue level = b.SampleLevel(1, CKJIT_SAMPLER_2D, uv, b.Abs(lod));
    const CKJitValue graded = b.SampleGrad(4, CKJIT_SAMPLER_CUBE, direction, b.Ddx(direction), b.Ddy(direction));
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Add(b.Mul(fetched, slice), b.Mul(level, graded)), b.Less(lod, b.Float(0.0f)), words),
              "texture access compiles");
    Save("texture_access", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const uint32_t textures[] = {0, 1, 4, 6};
    const uint32_t dimensions[] = {kResourceTexture2D, kResourceTexture2D, kResourceTextureCube, kResourceTexture3D};
    const uint32_t samplers[] = {1, 4};
    TestCheck(program.Count(kOpDclResource) == 4 && program.Count(kOpDclSampler) == 2,
              "every used slot declares its texture, only sampled slots their sampler");
    for (uint32_t i = 0; i < 4; ++i) {
        const int texture = program.Find(kOpDclResource) + (int)i;
        TestCheck(program[texture].Opcode == kOpDclResource && program[texture].Controls >> 11 == dimensions[i] &&
                      program[texture].Tokens[1] == i && program[texture].Tokens[2] == textures[i],
                  "textures have dense range ids");
    }
    for (uint32_t i = 0; i < 2; ++i) {
        const int sampler = program.Find(kOpDclSampler) + (int)i;
        TestCheck(program[sampler].Opcode == kOpDclSampler && program[sampler].Tokens[1] == i &&
                      program[sampler].Tokens[2] == samplers[i],
                  "samplers have range ids of their own");
    }
    for (int i = program.FirstCode(); i < program.Size(); ++i) {
        for (uint32_t k = 0; k < program[i].OperandCount; ++k) {
            const Operand &operand = program[i].Operands[k];
            if (operand.Type == kOperandResource) {
                TestCheck(operand.Indices[0] < 4 && textures[operand.Indices[0]] == operand.Indices[1],
                          "a texture is read through its range id");
            } else if (operand.Type == kOperandSampler) {
                TestCheck(operand.Indices[0] < 2 && samplers[operand.Indices[0]] == operand.Indices[1],
                          "a sampler is read through its range id");
            }
        }
    }

    int loads = 0, sizes = 0, counts = 0;
    for (int i = program.FirstCode(); i < program.Size(); ++i) {
        const Instruction &instruction = program[i];
        if (instruction.Opcode == kOpLd) {
            ++loads;
            TestCheck(instruction.OperandCount == 3 && RoutesResult(instruction, 0), "a load reads no sampler");
            const Operand &address = instruction.Operands[1];
            const uint32_t w = address.Selector >> 6 & 3;
            TestCheck(instruction.Operands[2].Indices[1] != 0 ||
                          (w != (address.Selector & 3) && w != (address.Selector >> 2 & 3)),
                      "a 2D load reads its mip as w");
        } else if (instruction.Opcode == kOpResinfo) {
            const Operand &address = instruction.Operands[1];
            const bool levels = address.Type == kOperandImmediate32 && address.ValueCount == 1 && address.Values[0] == 0;
            sizes += !levels && RoutesResult(instruction, 0) ? 1 : 0;
            counts += levels && RoutesResult(instruction, 3) ? 1 : 0;
            TestCheck(instruction.OperandCount == 3 && (instruction.Controls >> 11 & 3) == 2,
                      "size queries return integers and read no sampler");
        }
    }
    TestCheck(loads == 2 && sizes == 1 && counts == 1,
              "sizes are the leading components, the mip count is w of the base mip's");

    const int query = program.Find(kOpLod);
    TestCheck(query > 0 && program[query].OperandCount == 4 && RoutesResult(program[query], 1),
              "the LOD is the query's unclamped y");
    TestCheck(query > 0 && program[query].Operands[1].Type == kOperandTemp && program[query].Operands[1].Modifier == 0 &&
                  program[query - 1].Opcode == kOpMov && program[query - 1].Operands[1].Modifier == kModifierNeg &&
                  program[query - 1].Operands[0].Is(kOperandTemp, program[query].Operands[1].Indices[0]),
              "a modified coordinate moves to a scratch register first");
    const int lodSample = program.Find(kOpSampleL);
    TestCheck(lodSample > 0 && program[lodSample].OperandCount == 5 && RoutesResult(program[lodSample], 0) &&
                  program[lodSample].Operands[4].Is(kOperandTemp, program[lodSample].Operands[0].Indices[0]) &&
                  program[lodSample].Operands[4].Modifier == 0 &&
                  program[lodSample - 1].Operands[1].Modifier == kModifierAbs,
              "a modified LOD moves into the destination first");
    const int gradSample = program.Find(kOpSampleD);
    const int dx = program.Find(kOpDerivRtxCoarse);
    const int dy = program.Find(kOpDerivRtyCoarse);
    TestCheck(gradSample >= 0 && dx >= 0 && dy >= 0 && program[gradSample].OperandCount == 6 &&
                  RoutesResult(program[gradSample], 0) &&
                  program[gradSample].Operands[4].Is(kOperandTemp, program[dx].Operands[0].Indices[0]) &&
                  program[gradSample].Operands[5].Is(kOperandTemp, program[dy].Operands[0].Indices[0]),
              "a graded sample takes both derivatives");
    TestCheck(query >= 0 && program.Find(kOpDiscard) > query, "the LOD query runs before the discard");
}

void TestDepthComparison() {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue color = b.Input(kColor0);
    const CKJitValue position = b.Input(kFragCoord);

    // Slot 2 is compared, loaded from and queried, 3 only compared at the base
    // mip and 1 is a colour texture.
    const CKJitValue filtered = b.SampleCmp(2, uv, b.Component(position, 2));
    const CKJitValue base = b.SampleCmpLevelZero(3, b.Swizzle(uv, "yx"), b.Component(color, 3));
    const CKJitValue texel = b.Construct({b.FloatToInt(b.Swizzle(position, "xy")), b.Int(0)});
    const CKJitValue depth = b.Load(2, CKJIT_SAMPLER_2D_COMPARE, texel);
    const CKJitValue size = b.IntToFloat(b.TextureSize(2, CKJIT_SAMPLER_2D_COMPARE, b.Int(0)));
    const CKJitValue sampled = b.Sample(1, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    const CKJitValue shade = b.Mul(b.Construct({filtered, base, size}), b.Mul(depth, sampled));
    XArray<uint32_t> words;
    TestCheck(Compile(b, shade, b.Less(filtered, b.Float(0.5f)), words), "depth comparisons compile");
    Save("depth_comparison", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const uint32_t slots[] = {1, 2, 3};
    const uint32_t modes[] = {0, kSamplerComparison, kSamplerComparison};
    TestCheck(program.Count(kOpDclSampler) == 3 && program.Count(kOpDclResource) == 3,
              "every slot declares its texture and sampler");
    for (uint32_t i = 0; i < 3; ++i) {
        const int sampler = program.Find(kOpDclSampler) + (int)i;
        const int texture = program.Find(kOpDclResource) + (int)i;
        TestCheck(program[sampler].Opcode == kOpDclSampler && program[sampler].Tokens[2] == slots[i] &&
                      program[sampler].Controls >> 11 == modes[i],
                  "compared slots declare comparison samplers");
        TestCheck(program[texture].Opcode == kOpDclResource && program[texture].Controls >> 11 == kResourceTexture2D &&
                      program[texture].Tokens[2] == slots[i],
                  "compared slots are 2D textures");
    }

    const int compare = program.Find(kOpSampleC);
    TestCheck(compare >= 0 && program[compare].OperandCount == 5 && RoutesResult(program[compare], 0) &&
                  program[compare].Operands[2].Selector == 0 && program[compare].Operands[2].Indices[1] == 2 &&
                  program[compare].Operands[3].Type == kOperandSampler &&
                  program[compare].Operands[3].Indices[1] == 2,
              "a comparison reads its slot and replicates the result, as fxc writes it");
    TestCheck(compare >= 0 && program[compare].Operands[4].Is(kOperandInput, 0) &&
                  program[compare].Operands[4].Lanes() == 0x4,
              "the reference follows the sampler");
    const int level = program.Find(kOpSampleCLz);
    TestCheck(level >= 0 && program[level].OperandCount == 5 && RoutesResult(program[level], 0) &&
                  program[level].Operands[2].Indices[1] == 3 && program[level].Operands[4].Is(kOperandInput, 1) &&
                  program[level].Operands[4].Lanes() == 0x8,
              "a base-mip comparison takes no LOD operand");
    const int load = program.Find(kOpLd);
    TestCheck(load >= 0 && program[load].OperandCount == 3 && program[load].Operands[2].Indices[1] == 2,
              "a compared slot is loaded from without its sampler");
    TestCheck(program.Count(kOpResinfo, kResinfoUint) == 1, "a compared slot's size is queried");
    TestCheck(compare >= 0 && program.Find(kOpDiscard) > compare, "the comparison runs before the discard");
}

// Whether the instructions before an arm's end move into the same registers
// as those before another's.
bool MovesAlike(const Program &program, int end, int other, int count) {
    if (end < count || other < count)
        return false;
    for (int k = 1; k <= count; ++k) {
        const Instruction &a = program[end - k];
        const Instruction &b = program[other - k];
        if (a.Opcode != kOpMov || b.Opcode != kOpMov || a.Operands[0].Type != b.Operands[0].Type ||
            a.Operands[0].Indices[0] != b.Operands[0].Indices[0] || a.Operands[0].Selector != b.Operands[0].Selector)
            return false;
    }
    return true;
}

void TestIfRegions() {
    CKJitBuilder b(4);
    CKJitValue color, discard;
    BuildRegions(b, color, discard);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, discard, words), "regions compile");
    Save("if_regions", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const int outer = program.Find(kOpIf);
    const int inner = program.Find(kOpIf, outer + 1);
    const int innerElse = program.Find(kOpElse);
    const int innerEnd = program.Find(kOpEndIf);
    const int outerElse = program.Find(kOpElse, innerElse + 1);
    const int outerEnd = program.Find(kOpEndIf, innerEnd + 1);
    TestCheck(outer >= 0 && outer < inner && inner < innerElse && innerElse < innerEnd && innerEnd < outerElse &&
                  outerElse < outerEnd && program.Count(kOpIf, kTestNonZero) == 2 && program.Count(kOpElse) == 2 &&
                  program.Count(kOpEndIf) == 2,
              "regions nest as if_nz, else and endif blocks");
    if (outer < 0 || inner < 0 || innerElse < 0 || innerEnd < 0 || outerElse < 0 || outerEnd < 0)
        return;
    const int sample = program.Find(kOpSample);
    TestCheck(sample > outer && sample < inner, "the then arm samples");
    TestCheck(MovesAlike(program, innerElse, innerEnd, 1) && MovesAlike(program, outerElse, outerEnd, 3),
              "each arm ends moving its results into the PHI registers");
    const int kill = program.Find(kOpDiscard);
    TestCheck(kill > outerEnd && program[kill].Operands[0].Is(kOperandTemp, program[outerEnd - 1].Operands[0].Indices[0]),
              "the discard tests its PHI after the regions");
    TestCheck(program.Count(kOpMovc) == 1 && program.Find(kOpMovc) > outerEnd,
              "the result from before the region is selected after it");

    CKJitBuilder direct(4);
    const CKJitValue diffuse = direct.Input(kColor0);
    direct.If(direct.Less(direct.Component(diffuse, 3), direct.Float(0.5f)));
    const CKJitValue lit = direct.Mul(diffuse, direct.Uniform(1));
    direct.Else({lit});
    const CKJitValue shade = direct.EndIf(direct.Mul(diffuse, direct.Uniform(0)));
    TestCheck(Compile(direct, shade, CKJitValue(), words), "a colour PHI compiles");
    Save("phi_output", words);
    const Container directContainer(words);
    const Program directProgram(directContainer.Find("SHEX"));
    const int directElse = directProgram.Find(kOpElse);
    const int directEnd = directProgram.Find(kOpEndIf);
    TestCheck(directElse > 0 && directEnd > 0 && directProgram[directElse - 1].Operands[0].Is(kOperandOutput, 0) &&
                  MovesAlike(directProgram, directElse, directEnd, 1) && directProgram.Count(kOpMov) == 2,
              "a colour PHI nothing else reads is moved into the output by each arm");
}

// Whether a source reads exactly the temporary components a destination
// writes.
bool ReadsWritten(const Operand &source, const Operand &dest) {
    return source.Type == kOperandTemp && dest.Type == kOperandTemp && source.IndexCount >= 1 &&
           dest.IndexCount >= 1 && source.Indices[0] == dest.Indices[0] && source.Lanes() == dest.Selector;
}

bool IsImmediate(const Operand &operand, uint32_t value) {
    return operand.Type == kOperandImmediate32 && operand.ValueCount == 1 && operand.Values[0] == value;
}

// Whether a loop's body, up to the moves into its carried values, never
// writes temporary components it reads before writing them: the counter, the
// trips, the carried values and the values from before the loop survive every
// iteration.
bool KeepsLiveValues(const Program &program, int loop, int moves) {
    uint8_t written[64] = {}, live[64] = {};
    for (int i = loop + 1; i < moves; ++i) {
        const Instruction &instruction = program[i];
        const uint32_t first = DestinationCount(instruction.Opcode);
        for (uint32_t k = first; k < instruction.OperandCount; ++k) {
            const Operand &source = instruction.Operands[k];
            if (source.Type == kOperandTemp && source.Indices[0] < 64)
                live[source.Indices[0]] |= (uint8_t)(source.Lanes() & ~written[source.Indices[0]]);
        }
        if (first == 0 || instruction.OperandCount < first)
            continue;
        const Operand &dest = instruction.Operands[first - 1];
        if (dest.Type != kOperandTemp || dest.Indices[0] >= 64)
            continue;
        if ((dest.Selector & live[dest.Indices[0]]) != 0)
            return false;
        written[dest.Indices[0]] |= (uint8_t)dest.Selector;
    }
    return true;
}

void TestLoops() {
    CKJitBuilder b(4);
    CKJitValue color, discard;
    BuildLoops(b, color, discard);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, discard, words), "loops compile");
    Save("loops", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const int outer = program.Find(kOpLoop);
    const int inner = program.Find(kOpLoop, outer + 1);
    const int innerEnd = program.Find(kOpEndLoop);
    const int outerEnd = program.Find(kOpEndLoop, innerEnd + 1);
    const int fixed = program.Find(kOpLoop, outerEnd + 1);
    const int fixedEnd = program.Find(kOpEndLoop, outerEnd + 1);
    const bool nested = outer >= 7 && outer < inner && inner < innerEnd && innerEnd < outerEnd && outerEnd < fixed &&
                        fixed < fixedEnd;
    TestCheck(nested && program.Count(kOpLoop) == 3 && program.Count(kOpBreakc, kTestNonZero) == 3 &&
                  program.Count(kOpEndLoop) == 3,
              "loops nest as loop blocks, each breaking once");
    if (!nested)
        return;

    // Before a loop: moves starting the carried values, the trips unless the
    // count is constant, and the counter's start.
    const int loops[3] = {outer, inner, fixed};
    const int ends[3] = {outerEnd, innerEnd, fixedEnd};
    const uint32_t trips[3] = {8, 4, 3};
    Operand counters[3];
    for (int k = 0; k < 3; ++k) {
        const Instruction &start = program[loops[k] - 1];
        const Instruction &test = program[loops[k] + 1];
        const Instruction &exit = program[loops[k] + 2];
        const Instruction &step = program[ends[k] - 1];
        counters[k] = start.Operands[0];
        TestCheck(start.Opcode == kOpMov && IsImmediate(start.Operands[1], 0) && test.Opcode == kOpIge &&
                      ReadsWritten(test.Operands[1], counters[k]) && exit.Opcode == kOpBreakc &&
                      ReadsWritten(exit.Operands[0], test.Operands[0]),
                  "a loop starts its counter at zero and breaks once it reaches the trips");
        TestCheck(step.Opcode == kOpIadd && step.Operands[0].Is(kOperandTemp, counters[k].Indices[0]) &&
                      step.Operands[0].Selector == counters[k].Selector && ReadsWritten(step.Operands[1], counters[k]) &&
                      IsImmediate(step.Operands[2], 1),
                  "each iteration ends counting");
        if (k == 2) {
            TestCheck(IsImmediate(test.Operands[2], trips[k]),
                      "a constant count takes the bound's place when that is lower");
        } else {
            const Instruction &bounded = program[loops[k] - 2];
            TestCheck(bounded.Opcode == kOpImin && IsImmediate(bounded.Operands[2], trips[k]) &&
                          ReadsWritten(test.Operands[2], bounded.Operands[0]),
                      "a runtime count is bounded before the loop");
        }
    }

    // The outer loop's carried values: moves before it start them, moves
    // ending each iteration take the nexts in order, after a copy of the one
    // the swap overwrites first.
    const int copy = outerEnd - 7;
    TestCheck(MovesAlike(program, outerEnd - 1, outer - 2, 5), "each iteration ends moving the nexts into place");
    const Operand &first = program[outer - 5].Operands[0];
    const Operand &second = program[outer - 4].Operands[0];
    TestCheck(program[copy].Opcode == kOpMov && ReadsWritten(program[copy].Operands[1], first) &&
                  ReadsWritten(program[outerEnd - 4].Operands[1], second) &&
                  ReadsWritten(program[outerEnd - 3].Operands[1], program[copy].Operands[0]),
              "a swapped pair moves through a copy");
    TestCheck(program[copy - 1].Opcode != kOpMov, "only the value the swap overwrites is copied");
    TestCheck(KeepsLiveValues(program, outer, outerEnd - 6) && KeepsLiveValues(program, inner, innerEnd - 2) &&
                  KeepsLiveValues(program, fixed, fixedEnd - 2),
              "a body never overwrites what later iterations read");

    TestCheck(program[inner - 3].Opcode == kOpMov && ReadsWritten(program[inner - 3].Operands[1], counters[0]),
              "the nested loop carries the index from the body around it");
    const int sample = program.Find(kOpSample);
    TestCheck(sample > outer && sample < inner, "the uniform loop's body samples");
    TestCheck(program[fixed - 2].Opcode == kOpMov &&
                  ReadsWritten(program[fixed - 2].Operands[1], program[outer - 6].Operands[0]),
              "a RESULT reads its carried value's register after the loop");
    const int kill = program.Find(kOpDiscard);
    TestCheck(kill > fixedEnd && ReadsWritten(program[kill].Operands[0], program[outer - 3].Operands[0]),
              "the discard tests a carried value after the loops");
}

void TestIntegerLowering() {
    CKJitBuilder b(4);
    const CKJitValue position = b.Input(kFragCoord);
    const CKJitValue params = b.Uniform(0);
    const CKJitValue texel = b.FloatToInt(b.Swizzle(position, "xyz"));
    const CKJitValue extent = b.FloatToInt(b.Swizzle(params, "xyz"));
    const CKJitValue wrapped = b.IntMod(texel, extent);
    const CKJitValue bound = b.IntSub(texel, b.Int(5));
    const CKJitValue inside = b.All(b.IntLess(wrapped, bound));
    const CKJitValue scaled = b.IntMul(texel, b.Int(3));
    const CKJitValue ramp = b.IntToFloat(b.IntSub(wrapped, scaled));
    const CKJitValue color = b.Select(inside, b.Construct({ramp, b.Float(1.0f)}), b.Uniform(1));
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, CKJitValue(), words), "integer work compiles");
    Save("integer_lowering", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));

    // sign = a >> 31; ((a ^ sign) udiv b ^ sign) + (b & sign)
    const int divide = program.Find(kOpUdiv);
    TestCheck(divide >= 2 && divide + 3 < program.Size() && program[divide - 2].Opcode == kOpIshr &&
                  program[divide - 1].Opcode == kOpXor && program[divide + 1].Opcode == kOpXor &&
                  program[divide + 2].Opcode == kOpAnd && program[divide + 3].Opcode == kOpIadd,
              "the remainder takes the sign apart and back");
    if (divide < 2 || divide + 3 >= program.Size())
        return;
    const Instruction &udiv = program[divide];
    TestCheck(udiv.OperandCount == 4 && udiv.Operands[0].Type == kOperandNull, "the quotient is discarded");
    const Operand &sign = program[divide - 2].Operands[0];
    const Operand &count = program[divide - 2].Operands[2];
    TestCheck(count.Type == kOperandImmediate32 && count.Values[0] == 31, "the sign is the top bit");
    TestCheck(program[divide - 1].Operands[2].Is(kOperandTemp, sign.Indices[0]) &&
                  program[divide + 1].Operands[2].Is(kOperandTemp, sign.Indices[0]) &&
                  program[divide + 2].Operands[0].Is(kOperandTemp, sign.Indices[0]) &&
                  program[divide + 3].Operands[2].Is(kOperandTemp, sign.Indices[0]),
              "one register holds the sign and the offset");
    TestCheck(udiv.Operands[1].Is(kOperandTemp, udiv.Operands[2].Indices[0]) &&
                  !udiv.Operands[1].Is(kOperandTemp, sign.Indices[0]),
              "the remainder is taken in place, apart from the sign");

    const int multiply = program.Find(kOpImul);
    TestCheck(multiply >= 0 && program[multiply].OperandCount == 4 &&
                  program[multiply].Operands[0].Type == kOperandNull,
              "the high product is discarded");

    int negated = 0, immediate = 0;
    for (int i = program.FirstCode(); i < program.Size(); ++i) {
        if (program[i].Opcode != kOpIadd)
            continue;
        const Operand &dest = program[i].Operands[0];
        const Operand &subtrahend = program[i].Operands[2];
        negated += subtrahend.Type == kOperandTemp && subtrahend.Modifier == kModifierNeg ? 1 : 0;
        bool five = subtrahend.Type == kOperandImmediate32 && subtrahend.ValueCount == 4;
        for (uint32_t p = 0; p < 4 && five; ++p)
            five = (dest.Selector >> p & 1) == 0 || subtrahend.Values[p] == 0xfffffffbu;
        immediate += five ? 1 : 0;
    }
    TestCheck(negated == 1, "a register is subtracted through the negate modifier");
    TestCheck(immediate == 1, "a constant is subtracted as its negation");

    const int less = program.Find(kOpIlt);
    TestCheck(less >= 0 && less + 2 < program.Size() && program[less + 1].Opcode == kOpAnd &&
                  program[less + 2].Opcode == kOpAnd && program.Count(kOpAnd) == 3,
              "all of three components is two ands");
    if (less >= 0 && less + 2 < program.Size()) {
        const Operand &first = program[less + 1].Operands[0];
        const Operand &folded = program[less + 2].Operands[1];
        TestCheck(program[less + 1].Operands[1].Is(kOperandTemp, program[less].Operands[0].Indices[0]) &&
                      folded.Is(kOperandTemp, first.Indices[0]) && folded.Lanes() == first.Selector &&
                      program[less + 2].Operands[0].Selector == first.Selector,
                  "the second and folds the first into the same component");
    }
    TestCheck(program.Count(kOpItof) == 1 && program.Count(kOpFtoi) == 2, "conversions are single instructions");
}

void TestRegisterAllocation() {
    CKJitBuilder b(4);
    const CKJitValue flat = b.Input(kFlatColor0);
    CKJitValue value = b.Input(kColor0);
    for (uint32_t i = 0; i < 64; ++i)
        value = b.Add(b.Mul(value, b.Uniform(i % 4)), b.Swizzle(flat, i % 2 ? "wzyx" : "yxwz"));
    XArray<uint32_t> words;
    TestCheck(Compile(b, value, CKJitValue(), words), "a long chain compiles");

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const int temps = program.Find(kOpDclTemps);
    TestCheck(temps >= 0 && program[temps].Tokens[0] <= 2, "dead values free their temporaries");

    CKJitBuilder scalars(4);
    const CKJitValue params = scalars.Uniform(0);
    CKJitValue x = scalars.Component(params, 0), y = scalars.Component(params, 1);
    for (uint32_t i = 0; i < 8; ++i) {
        x = scalars.Add(scalars.Mul(x, y), scalars.Component(params, 2));
        y = scalars.Sub(y, x);
    }
    CKJitValue packed[3];
    for (uint32_t i = 0; i < 3; ++i)
        packed[i] = scalars.Mul(x, scalars.Component(params, i));
    TestCheck(Compile(scalars, scalars.Construct({packed[0], packed[1], packed[2], x}), CKJitValue(), words),
              "scalar work compiles");
    const Container scalarContainer(words);
    const Program scalarProgram(scalarContainer.Find("SHEX"));
    const int scalarTemps = scalarProgram.Find(kOpDclTemps);
    TestCheck(scalarTemps >= 0 && scalarProgram[scalarTemps].Tokens[0] == 1,
              "scalars share the components of one register");
}

void TestColorInOutput() {
    CKJitBuilder direct(4);
    XArray<uint32_t> words;
    TestCheck(Compile(direct, direct.Mul(direct.Input(kColor0), direct.Uniform(0)), CKJitValue(), words),
              "a computed colour compiles");
    Save("direct_output", words);
    const Container directContainer(words);
    const Program directProgram(directContainer.Find("SHEX"));
    const int multiply = directProgram.Find(kOpMul);
    TestCheck(multiply >= 0 && directProgram[multiply].Operands[0].Is(kOperandOutput, 0) &&
                  directProgram.Count(kOpMov) == 0 && directProgram.Count(kOpDclTemps) == 0,
              "a colour nothing else reads is computed into the output");

    CKJitBuilder read(4);
    const CKJitValue color = read.Mul(read.Input(kColor0), read.Uniform(0));
    TestCheck(Compile(read, color, read.Less(read.Component(color, 3), read.Float(0.5f)), words),
              "a colour the discard reads compiles");
    Save("discarded_output", words);
    const Container readContainer(words);
    const Program readProgram(readContainer.Find("SHEX"));
    const int readMultiply = readProgram.Find(kOpMul);
    TestCheck(readMultiply >= 0 && readProgram[readMultiply].Operands[0].Type == kOperandTemp &&
                  readProgram.Count(kOpMov) == 1 && readProgram.Find(kOpDiscard) < readProgram.Find(kOpMov),
              "a colour the discard reads is moved to the output after the discard");

    CKJitBuilder mirrored(4);
    const CKJitValue sampled =
        mirrored.Sample(0, CKJIT_SAMPLER_2D, mirrored.Neg(mirrored.Input(kTexCoord0)), mirrored.Float(0.0f));
    TestCheck(Compile(mirrored, sampled, CKJitValue(), words), "a mirrored sample compiles");
    Save("mirrored_sample", words);
    const Container mirroredContainer(words);
    const Program mirroredProgram(mirroredContainer.Find("SHEX"));
    const int mirroredSample = mirroredProgram.Find(kOpSample);
    TestCheck(mirroredSample > 0 && mirroredProgram[mirroredSample].Operands[0].Is(kOperandOutput, 0) &&
                  mirroredProgram[mirroredSample].Operands[1].Is(kOperandTemp, 0) &&
                  mirroredProgram[mirroredSample].Operands[1].Modifier == 0 &&
                  mirroredProgram[mirroredSample - 1].Opcode == kOpMov &&
                  mirroredProgram[mirroredSample - 1].Operands[1].Modifier == kModifierNeg,
              "a modified coordinate moves to a scratch register first");

    CKJitBuilder biased(4);
    const CKJitValue params = biased.Uniform(0);
    const CKJitValue texel = biased.Sample(0, CKJIT_SAMPLER_2D, biased.Neg(biased.Input(kTexCoord0)),
                                           biased.Abs(biased.Component(params, 0)));
    TestCheck(Compile(biased, biased.Mul(texel, biased.Input(kColor0)), CKJitValue(), words),
              "a modified bias compiles");
    Save("modified_bias", words);
    const Container biasedContainer(words);
    const Program biasedProgram(biasedContainer.Find("SHEX"));
    const int biasedSample = biasedProgram.Find(kOpSampleB);
    TestCheck(biasedSample >= 2 && biasedProgram[biasedSample].Operands[4].Is(kOperandTemp, 0) &&
                  biasedProgram[biasedSample].Operands[4].Modifier == 0 &&
                  biasedProgram[biasedSample].Operands[0].Is(kOperandTemp, 0) &&
                  biasedProgram[biasedSample - 1].Operands[1].Modifier == kModifierAbs,
              "a modified bias moves into the destination first");
    const int biasedTemps = biasedProgram.Find(kOpDclTemps);
    TestCheck(biasedTemps >= 0 && biasedProgram[biasedTemps].Tokens[0] == 1,
              "the destination is the scratch register");
}

void TestDeterminism() {
    CKJitBuilder b(89);
    CKJitValue color, discard;
    BuildEveryOperation(b, color, discard);
    XArray<uint32_t> words, again;
    TestCheck(Compile(b, color, discard, words) && Compile(b, color, discard, again), "the shader compiles twice");
    TestCheck(again.Size() == words.Size() &&
                  std::memcmp(again.Begin(), words.Begin(), (size_t)words.Size() * sizeof(uint32_t)) == 0,
              "emission is deterministic");
}

void TestConstantOutputs() {
    CKJitBuilder b(4);
    b.Input(kColor0);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Float4(0.25f, 0.5f, 0.75f, 1.0f), b.Bool(true), words), "constant outputs compile");
    Save("constant_outputs", words);

    const Container container(words);
    const Program program(container.Find("SHEX"));
    const int kill = program.Find(kOpDiscard);
    TestCheck(kill >= 0 && program[kill].Operands[0].Type == kOperandImmediate32 &&
                  program[kill].Operands[0].ValueCount == 1 && program[kill].Operands[0].Values[0] == 0xffffffffu,
              "an unconditional discard tests an all-ones immediate");
    const int move = program.Find(kOpMov);
    TestCheck(move >= 0 && program[move].Operands[1].ValueCount == 4 &&
                  program[move].Operands[1].Values[0] == 0x3e800000u &&
                  program[move].Operands[1].Values[3] == 0x3f800000u,
              "the colour is an immediate");
    TestCheck(program.Count(kOpDclInputPs) == 0 && program.Count(kOpDclTemps) == 0, "nothing is read");
}

void TestRejects() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Mul(b.Add(b.Input(kColor0), b.Input(kFlatColor0)), b.Uniform(1));
    CKJitFragmentShader shader;
    TestCheck(b.Finish(color, CKJitValue(), shader), "the shader finishes");
    XArray<uint32_t> words;
    TestCheck(CKJitEmitDxbc(shader, kLayout, words), "the shader compiles");

    CKJitFragmentShader corrupted = shader;
    corrupted.Nodes[corrupted.Nodes.Size() - 1].Operands[0] = (uint32_t)corrupted.Nodes.Size() - 1;
    TestCheck(!CKJitEmitDxbc(corrupted, kLayout, words), "shaders that fail verification are refused");

    CKJitFragmentShader highest = shader;
    highest.Inputs[1].Location = 30;
    TestCheck(CKJitEmitDxbc(highest, kLayout, words), "location 30 is in the last input register");

    CKJitFragmentShader high = shader;
    high.Inputs[1].Location = 31;
    TestCheck(!CKJitEmitDxbc(high, kLayout, words), "varying locations are below 31");

    CKJitFragmentShader shared = shader;
    shared.Inputs[1].Location = shared.Inputs[0].Location;
    TestCheck(!CKJitEmitDxbc(shared, kLayout, words), "varying locations are unique");
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 1)
        g_ContainerDirectory = argv[1];
    TestFramework framework;
    framework.Run("container layout", TestContainerLayout);
    framework.Run("digest", TestDigest);
    framework.Run("matches fxc", TestMatchesFxc);
    framework.Run("interface", TestInterface);
    framework.Run("resources", TestResources);
    framework.Run("uniform buffers", TestUniformBuffers);
    framework.Run("lowering", TestLowering);
    framework.Run("texture access", TestTextureAccess);
    framework.Run("depth comparison", TestDepthComparison);
    framework.Run("if regions", TestIfRegions);
    framework.Run("loops", TestLoops);
    framework.Run("integer lowering", TestIntegerLowering);
    framework.Run("register allocation", TestRegisterAllocation);
    framework.Run("color in output", TestColorInOutput);
    framework.Run("determinism", TestDeterminism);
    framework.Run("constant outputs", TestConstantOutputs);
    framework.Run("rejects", TestRejects);
    return framework.ExitCode();
}
