#include "CKJitSpirv.h"

#include "XSHashTable.h"

#include <cstring>
#include <initializer_list>

namespace {

// The subset of the SPIR-V 1.0 and GLSL.std.450 enumerants the emitter uses.
enum {
    SpvMagicNumber = 0x07230203,
    SpvVersion10 = 0x00010000,

    SpvOpExtInstImport = 11,
    SpvOpExtInst = 12,
    SpvOpMemoryModel = 14,
    SpvOpEntryPoint = 15,
    SpvOpExecutionMode = 16,
    SpvOpCapability = 17,
    SpvOpTypeVoid = 19,
    SpvOpTypeBool = 20,
    SpvOpTypeInt = 21,
    SpvOpTypeFloat = 22,
    SpvOpTypeVector = 23,
    SpvOpTypeImage = 25,
    SpvOpTypeSampledImage = 27,
    SpvOpTypeArray = 28,
    SpvOpTypeStruct = 30,
    SpvOpTypePointer = 32,
    SpvOpTypeFunction = 33,
    SpvOpConstantTrue = 41,
    SpvOpConstantFalse = 42,
    SpvOpConstant = 43,
    SpvOpConstantComposite = 44,
    SpvOpFunction = 54,
    SpvOpFunctionEnd = 56,
    SpvOpVariable = 59,
    SpvOpLoad = 61,
    SpvOpStore = 62,
    SpvOpAccessChain = 65,
    SpvOpDecorate = 71,
    SpvOpMemberDecorate = 72,
    SpvOpVectorShuffle = 79,
    SpvOpCompositeConstruct = 80,
    SpvOpCompositeExtract = 81,
    SpvOpImageSampleImplicitLod = 87,
    SpvOpImageSampleExplicitLod = 88,
    SpvOpImageSampleDrefImplicitLod = 89,
    SpvOpImageSampleDrefExplicitLod = 90,
    SpvOpImageFetch = 95,
    SpvOpImage = 100,
    SpvOpImageQuerySizeLod = 103,
    SpvOpImageQueryLod = 105,
    SpvOpImageQueryLevels = 106,
    SpvOpConvertFToS = 110,
    SpvOpConvertSToF = 111,
    SpvOpBitcast = 124,
    SpvOpFNegate = 127,
    SpvOpIAdd = 128,
    SpvOpFAdd = 129,
    SpvOpISub = 130,
    SpvOpFSub = 131,
    SpvOpIMul = 132,
    SpvOpFMul = 133,
    SpvOpFDiv = 136,
    SpvOpSRem = 138,
    SpvOpDot = 148,
    SpvOpAny = 154,
    SpvOpAll = 155,
    SpvOpLogicalOr = 166,
    SpvOpLogicalAnd = 167,
    SpvOpLogicalNot = 168,
    SpvOpSelect = 169,
    SpvOpIEqual = 170,
    SpvOpINotEqual = 171,
    SpvOpSLessThan = 177,
    SpvOpSLessThanEqual = 179,
    SpvOpFOrdEqual = 180,
    SpvOpFUnordNotEqual = 183,
    SpvOpFOrdLessThan = 184,
    SpvOpFOrdLessThanEqual = 188,
    SpvOpShiftRightArithmetic = 195,
    SpvOpBitwiseXor = 198,
    SpvOpBitwiseAnd = 199,
    SpvOpDPdx = 207,
    SpvOpDPdy = 208,
    SpvOpPhi = 245,
    SpvOpLoopMerge = 246,
    SpvOpSelectionMerge = 247,
    SpvOpLabel = 248,
    SpvOpBranch = 249,
    SpvOpBranchConditional = 250,
    SpvOpKill = 252,
    SpvOpReturn = 253,

    SpvCapabilityShader = 1,
    SpvCapabilityClipDistance = 32,
    SpvCapabilityImageQuery = 50,
    SpvAddressingModelLogical = 0,
    SpvMemoryModelGLSL450 = 1,
    SpvExecutionModelVertex = 0,
    SpvExecutionModelFragment = 4,
    SpvExecutionModeOriginUpperLeft = 7,

    SpvDecorationBlock = 2,
    SpvDecorationArrayStride = 6,
    SpvDecorationBuiltIn = 11,
    SpvDecorationFlat = 14,
    SpvDecorationLocation = 30,
    SpvDecorationBinding = 33,
    SpvDecorationDescriptorSet = 34,
    SpvDecorationOffset = 35,
    SpvBuiltInPosition = 0,
    SpvBuiltInClipDistance = 3,
    SpvBuiltInFragCoord = 15,

    SpvStorageClassUniformConstant = 0,
    SpvStorageClassInput = 1,
    SpvStorageClassUniform = 2,
    SpvStorageClassOutput = 3,

    SpvDim2D = 1,
    SpvDim3D = 2,
    SpvDimCube = 3,
    SpvImageFormatUnknown = 0,
    SpvImageOperandsBiasMask = 0x1,
    SpvImageOperandsLodMask = 0x2,
    SpvImageOperandsGradMask = 0x4,
    SpvFunctionControlNone = 0,
    SpvSelectionControlNone = 0,
    SpvLoopControlNone = 0,

    GLSLstd450RoundEven = 2,
    GLSLstd450FAbs = 4,
    GLSLstd450Floor = 8,
    GLSLstd450Ceil = 9,
    GLSLstd450Exp2 = 29,
    GLSLstd450Log2 = 30,
    GLSLstd450Sqrt = 31,
    GLSLstd450SMin = 39,
    GLSLstd450SMax = 42,
    GLSLstd450FClamp = 43,
    GLSLstd450FMix = 46,
    GLSLstd450Fma = 50,
    GLSLstd450NMin = 79,
    GLSLstd450NMax = 80,
};

// Instructions of one logical section of the module.
class SpirvSection {
public:
    void Emit(uint32_t opcode, const uint32_t *operands, uint32_t count) {
        m_Words.PushBack((count + 1u) << 16 | opcode);
        for (uint32_t i = 0; i < count; ++i)
            m_Words.PushBack(operands[i]);
    }
    void Emit(uint32_t opcode, std::initializer_list<uint32_t> operands) {
        Emit(opcode, operands.begin(), (uint32_t)operands.size());
    }
    const XArray<uint32_t> &Words() const { return m_Words; }
    uint32_t Size() const { return (uint32_t)m_Words.Size(); }
    void Patch(uint32_t position, uint32_t word) { m_Words[(int)position] = word; }

private:
    XArray<uint32_t> m_Words;
};

// A nul-terminated literal string, packed little-endian into whole words.
void AppendString(XArray<uint32_t> &words, const char *text) {
    const uint32_t length = (uint32_t)std::strlen(text) + 1u;
    for (uint32_t offset = 0; offset < length; offset += 4) {
        uint32_t word = 0;
        for (uint32_t i = 0; i < 4 && offset + i < length; ++i)
            word |= (uint32_t)(uint8_t)text[offset + i] << (8 * i);
        words.PushBack(word);
    }
}

class SpirvEmitter {
public:
    SpirvEmitter(const CKJitShader &shader, const CKJitResourceLayout &layout, CKJitValue primary,
                 CKJitValue discard, const CKJitVertexShader *vertex = nullptr);

    void Emit(XArray<uint32_t> &words);

private:
    // The blocks a region branches to past its then arm, or a loop past its
    // body, and what the loop's back edge completes.
    struct Region {
        uint32_t Else; // or the loop's continue block
        uint32_t Merge;
        uint32_t Header;
        uint32_t Index; // the iteration, a phi of the header
        uint32_t Next;  // the following one, computed by the continue block
        int Patches;    // the first in m_Patches of the carried values' phis
    };
    // Types and constants are interned by their instruction, which also gives
    // SPIR-V the unique non-aggregate type declarations it requires.
    struct Declaration {
        uint32_t Header; // word count and opcode, as in the instruction
        uint32_t ResultType;
        uint32_t Operands[7];
    };
    struct DeclarationHash {
        int operator()(const Declaration &declaration) const;
    };
    struct DeclarationEqual {
        int operator()(const Declaration &a, const Declaration &b) const;
    };

    uint32_t NewId() { return m_Bound++; }
    uint32_t Declare(uint32_t opcode, uint32_t resultType, const uint32_t *operands, uint32_t count);
    uint32_t DeclareType(uint32_t opcode, std::initializer_list<uint32_t> operands) {
        return Declare(opcode, 0, operands.begin(), (uint32_t)operands.size());
    }
    uint32_t DeclareConstant(uint32_t opcode, uint32_t type, std::initializer_list<uint32_t> operands) {
        return Declare(opcode, type, operands.begin(), (uint32_t)operands.size());
    }
    uint32_t Variable(uint32_t storage, uint32_t type);
    void Decorate(uint32_t target, uint32_t decoration, uint32_t value) {
        m_Annotations.Emit(SpvOpDecorate, {target, decoration, value});
    }

    uint32_t TypeOf(CKJitType type);
    uint32_t FloatType(uint32_t components) { return TypeOf(CKJitFloatType(components)); }
    uint32_t BoolType(uint32_t components) { return TypeOf(CKJitBoolType(components)); }
    uint32_t IntType() { return TypeOf(CKJIT_TYPE_INT); }
    uint32_t InputType(const CKJitInput &input) {
        if (input.Scalar == CKJIT_INPUT_FLOAT) return FloatType(input.Components);
        const uint32_t scalar = DeclareType(SpvOpTypeInt, {32, 0});
        return input.Components == 1 ? scalar : DeclareType(SpvOpTypeVector, {scalar, input.Components});
    }
    uint32_t PointerType(uint32_t storage, uint32_t type) { return DeclareType(SpvOpTypePointer, {storage, type}); }
    uint32_t Constant(CKJitType type, const uint32_t *bits);
    uint32_t ConstantSplat(CKJitType type, uint32_t bits);
    uint32_t IntConstant(int32_t value) { return ConstantSplat(CKJIT_TYPE_INT, (uint32_t)value); }
    uint32_t FloatSplat(float value, CKJitType type);

    // Function body instructions; each returns its result id.
    uint32_t Op(uint32_t opcode, uint32_t type, const uint32_t *operands, uint32_t count);
    uint32_t Op(uint32_t opcode, uint32_t type, std::initializer_list<uint32_t> operands) {
        return Op(opcode, type, operands.begin(), (uint32_t)operands.size());
    }
    uint32_t Glsl(uint32_t instruction, uint32_t type, std::initializer_list<uint32_t> operands);
    uint32_t Enter(uint32_t label);

    void DeclareInterface();
    uint32_t UniformBlock(uint32_t buffer);
    uint32_t ImageType(uint32_t dim);
    uint32_t SampledImage(const CKJitNode &node);
    uint32_t Image(const CKJitNode &node);
    uint32_t Query(uint32_t opcode, uint32_t type, std::initializer_list<uint32_t> operands);
    uint32_t Value(uint32_t node) const { return m_Values[(int)node]; }
    uint32_t Translate(uint32_t index);
    uint32_t Control(uint32_t index);
    uint32_t Swizzle(const CKJitNode &node);
    uint32_t Select(const CKJitNode &node);
    uint32_t Sample(const CKJitNode &node);
    uint32_t Load(const CKJitNode &node, uint32_t texel);
    uint32_t Modulo(const CKJitNode &node, uint32_t a, uint32_t b);
    uint32_t ShiftCount(uint32_t node);

    const CKJitShader &m_Shader;
    const CKJitResourceLayout &m_Layout;
    CKJitValue m_Primary;
    CKJitValue m_Discard;
    const CKJitVertexShader *m_Vertex;
    SpirvSection m_Annotations;
    SpirvSection m_Globals; // types, constants and variables
    SpirvSection m_Body;
    XSHashTable<uint32_t, Declaration, DeclarationHash, DeclarationEqual> m_Declarations;
    XArray<uint32_t> m_Values;    // SPIR-V id of every node; a marker's is the block it ends
    XArray<uint32_t> m_Inputs;    // variable of every shader input
    XArray<uint32_t> m_Outputs;   // variables of the vertex varyings
    XArray<uint32_t> m_Interface; // entry point interface: inputs and output
    XArray<Region> m_Regions;     // enclosing the node being translated, innermost last
    XArray<uint32_t> m_Patches;   // in m_Body: where loop header phis take the nexts
    uint32_t m_SampledImages[CKJIT_MAX_SAMPLERS]; // loaded on first use
    uint32_t m_Images[CKJIT_MAX_SAMPLERS];        // taken from the sampled image on first use
    uint32_t m_Bound;
    uint32_t m_Block; // label of the block being emitted
    uint32_t m_Main;
    uint32_t m_GlslImport;
    uint32_t m_Output;
    uint32_t m_ClipOutput = 0, m_ClipType = 0;
    uint32_t m_UniformBlocks[CKJIT_MAX_UNIFORM_BUFFERS]; // declared on first read
    bool m_ImageQuery; // a size, level or LOD query needs the capability
};

int SpirvEmitter::DeclarationHash::operator()(const Declaration &declaration) const {
    const uint8_t *bytes = (const uint8_t *)&declaration;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(declaration); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return (int)(hash ^ (hash >> 16));
}

int SpirvEmitter::DeclarationEqual::operator()(const Declaration &a, const Declaration &b) const {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

SpirvEmitter::SpirvEmitter(const CKJitShader &shader, const CKJitResourceLayout &layout, CKJitValue primary,
                           CKJitValue discard, const CKJitVertexShader *vertex)
    : m_Shader(shader), m_Layout(layout), m_Primary(primary), m_Discard(discard), m_Vertex(vertex),
      m_Bound(1), m_Block(0), m_Output(0),
      m_ImageQuery(false) {
    std::memset(m_UniformBlocks, 0, sizeof(m_UniformBlocks));
    std::memset(m_SampledImages, 0, sizeof(m_SampledImages));
    std::memset(m_Images, 0, sizeof(m_Images));
    m_Main = NewId();
    m_GlslImport = NewId();
}

uint32_t SpirvEmitter::Declare(uint32_t opcode, uint32_t resultType, const uint32_t *operands, uint32_t count) {
    Declaration declaration;
    std::memset(&declaration, 0, sizeof(declaration));
    declaration.Header = (count + (resultType != 0 ? 3u : 2u)) << 16 | opcode;
    declaration.ResultType = resultType;
    for (uint32_t i = 0; i < count; ++i)
        declaration.Operands[i] = operands[i];
    if (const uint32_t *found = m_Declarations.FindPtr(declaration))
        return *found;

    const uint32_t id = NewId();
    uint32_t words[9];
    uint32_t length = 0;
    if (resultType != 0)
        words[length++] = resultType;
    words[length++] = id;
    for (uint32_t i = 0; i < count; ++i)
        words[length++] = operands[i];
    m_Globals.Emit(opcode, words, length);
    m_Declarations.Insert(declaration, id, TRUE);
    return id;
}

uint32_t SpirvEmitter::Variable(uint32_t storage, uint32_t type) {
    const uint32_t id = NewId();
    m_Globals.Emit(SpvOpVariable, {PointerType(storage, type), id, storage});
    return id;
}

uint32_t SpirvEmitter::TypeOf(CKJitType type) {
    uint32_t scalar;
    switch (CKJitScalarOf(type)) {
    case CKJIT_TYPE_VOID: return DeclareType(SpvOpTypeVoid, {});
    case CKJIT_TYPE_BOOL: scalar = DeclareType(SpvOpTypeBool, {}); break;
    case CKJIT_TYPE_INT: scalar = DeclareType(SpvOpTypeInt, {32, 1}); break;
    default: scalar = DeclareType(SpvOpTypeFloat, {32}); break;
    }
    const uint32_t components = CKJitComponentCount(type);
    return components == 1 ? scalar : DeclareType(SpvOpTypeVector, {scalar, components});
}

uint32_t SpirvEmitter::Constant(CKJitType type, const uint32_t *bits) {
    const uint32_t scalar = TypeOf(CKJitScalarOf(type));
    const uint32_t count = CKJitComponentCount(type);
    uint32_t components[4];
    for (uint32_t i = 0; i < count; ++i) {
        if (CKJitIsBool(type))
            components[i] = DeclareConstant(bits[i] != 0 ? SpvOpConstantTrue : SpvOpConstantFalse, scalar, {});
        else
            components[i] = DeclareConstant(SpvOpConstant, scalar, {bits[i]});
    }
    return count == 1 ? components[0] : Declare(SpvOpConstantComposite, TypeOf(type), components, count);
}

uint32_t SpirvEmitter::ConstantSplat(CKJitType type, uint32_t bits) {
    const uint32_t components[4] = {bits, bits, bits, bits};
    return Constant(type, components);
}

uint32_t SpirvEmitter::FloatSplat(float value, CKJitType type) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(value));
    return ConstantSplat(type, bits);
}

uint32_t SpirvEmitter::Op(uint32_t opcode, uint32_t type, const uint32_t *operands, uint32_t count) {
    const uint32_t id = NewId();
    uint32_t words[10]; // result type/id plus up to eight clip-array constituents
    words[0] = type;
    words[1] = id;
    for (uint32_t i = 0; i < count; ++i)
        words[2 + i] = operands[i];
    m_Body.Emit(opcode, words, count + 2);
    return id;
}

uint32_t SpirvEmitter::Glsl(uint32_t instruction, uint32_t type, std::initializer_list<uint32_t> operands) {
    uint32_t words[5] = {m_GlslImport, instruction};
    uint32_t count = 2;
    for (uint32_t operand : operands)
        words[count++] = operand;
    return Op(SpvOpExtInst, type, words, count);
}

// Starts a block; returns the one it follows.
uint32_t SpirvEmitter::Enter(uint32_t label) {
    const uint32_t previous = m_Block;
    m_Body.Emit(SpvOpLabel, {label});
    m_Block = label;
    return previous;
}

// Every declared input is part of the interface, read or not.
void SpirvEmitter::DeclareInterface() {
    for (int i = 0; i < m_Shader.Inputs.Size(); ++i) {
        const CKJitInput &input = m_Shader.Inputs[i];
        const uint32_t variable = Variable(SpvStorageClassInput, InputType(input));
        if (input.Kind == CKJIT_INPUT_FRAG_COORD) {
            Decorate(variable, SpvDecorationBuiltIn, SpvBuiltInFragCoord);
        } else {
            Decorate(variable, SpvDecorationLocation, input.Location);
            if (input.Kind == CKJIT_INPUT_FLAT)
                m_Annotations.Emit(SpvOpDecorate, {variable, SpvDecorationFlat});
        }
        m_Inputs.PushBack(variable);
        m_Interface.PushBack(variable);
    }
    m_Output = Variable(SpvStorageClassOutput, FloatType(4));
    if (m_Vertex)
        Decorate(m_Output, SpvDecorationBuiltIn, SpvBuiltInPosition);
    else
        Decorate(m_Output, SpvDecorationLocation, 0);
    m_Interface.PushBack(m_Output);
    if (m_Vertex) {
        if (m_Vertex->ClipDistances.Size()) {
            m_ClipType = DeclareType(SpvOpTypeArray, {FloatType(1), IntConstant(m_Vertex->ClipDistances.Size())});
            m_ClipOutput = Variable(SpvStorageClassOutput, m_ClipType);
            Decorate(m_ClipOutput, SpvDecorationBuiltIn, SpvBuiltInClipDistance);
            m_Interface.PushBack(m_ClipOutput);
        }
        for (int i = 0; i < m_Vertex->Outputs.Size(); ++i) {
            const CKJitVertexOutput &output = m_Vertex->Outputs[i];
            const uint32_t variable = Variable(SpvStorageClassOutput, TypeOf(m_Shader.Node(output.Value).Type));
            Decorate(variable, SpvDecorationLocation, output.Location);
            if (output.Kind == CKJIT_INPUT_FLAT)
                m_Annotations.Emit(SpvOpDecorate, {variable, SpvDecorationFlat});
            m_Outputs.PushBack(variable);
            m_Interface.PushBack(variable);
        }
    }
}

// struct { float4 rows[UniformVec4Counts[buffer]]; }, declared when first
// read. Buffers of one size share the block type, decorated once.
uint32_t SpirvEmitter::UniformBlock(uint32_t buffer) {
    if (m_UniformBlocks[buffer] == 0) {
        const uint32_t count = m_Shader.UniformVec4Counts[buffer];
        const uint32_t rows = DeclareType(SpvOpTypeArray, {FloatType(4), IntConstant((int32_t)count)});
        const uint32_t block = DeclareType(SpvOpTypeStruct, {rows});
        bool decorated = false;
        for (uint32_t other = 0; other < m_Shader.UniformBufferCount; ++other)
            decorated = decorated || (m_UniformBlocks[other] != 0 && m_Shader.UniformVec4Counts[other] == count);
        if (!decorated) {
            Decorate(rows, SpvDecorationArrayStride, 16);
            m_Annotations.Emit(SpvOpMemberDecorate, {block, 0, SpvDecorationOffset, 0});
            m_Annotations.Emit(SpvOpDecorate, {block, SpvDecorationBlock});
        }
        m_UniformBlocks[buffer] = Variable(SpvStorageClassUniform, block);
        Decorate(m_UniformBlocks[buffer], SpvDecorationDescriptorSet, m_Layout.UniformSpace);
        Decorate(m_UniformBlocks[buffer], SpvDecorationBinding, m_Layout.UniformBinding + buffer);
    }
    return m_UniformBlocks[buffer];
}

uint32_t SpirvEmitter::ImageType(uint32_t dim) {
    static const uint32_t kDims[] = {SpvDim2D, SpvDimCube, SpvDim3D, SpvDim2D};
    const uint32_t depth = dim == CKJIT_SAMPLER_2D_COMPARE ? 1 : 0;
    return DeclareType(SpvOpTypeImage, {FloatType(1), kDims[dim], depth, 0, 0, 1, SpvImageFormatUnknown});
}

// The combined image sampler of a node's slot, declared and loaded when first
// used. Emit uses every slot in the entry block, so the load dominates every
// arm.
uint32_t SpirvEmitter::SampledImage(const CKJitNode &node) {
    const uint32_t slot = node.Imm[0];
    if (m_SampledImages[slot] == 0) {
        const uint32_t sampledImage = DeclareType(SpvOpTypeSampledImage, {ImageType(node.Imm[1])});
        const uint32_t variable = Variable(SpvStorageClassUniformConstant, sampledImage);
        Decorate(variable, SpvDecorationDescriptorSet, m_Layout.SamplerSpace);
        Decorate(variable, SpvDecorationBinding, slot);
        m_SampledImages[slot] = Op(SpvOpLoad, sampledImage, {variable});
    }
    return m_SampledImages[slot];
}

// Fetches and size queries take the image without its sampler.
uint32_t SpirvEmitter::Image(const CKJitNode &node) {
    const uint32_t slot = node.Imm[0];
    if (m_Images[slot] == 0) {
        const uint32_t sampledImage = SampledImage(node);
        m_Images[slot] = Op(SpvOpImage, ImageType(node.Imm[1]), {sampledImage});
    }
    return m_Images[slot];
}

uint32_t SpirvEmitter::Query(uint32_t opcode, uint32_t type, std::initializer_list<uint32_t> operands) {
    m_ImageQuery = true;
    return Op(opcode, type, operands);
}

uint32_t SpirvEmitter::Swizzle(const CKJitNode &node) {
    const uint32_t source = Value(node.Operands[0]);
    const uint32_t count = CKJitComponentCount(node.Type);
    const uint32_t type = TypeOf(node.Type);
    // A scalar source is a splat (a scalar-to-scalar swizzle is the identity
    // the builder never emits).
    if (CKJitComponentCount(m_Shader.Nodes[(int)node.Operands[0]].Type) == 1) {
        const uint32_t parts[4] = {source, source, source, source};
        return Op(SpvOpCompositeConstruct, type, parts, count);
    }
    if (count == 1)
        return Op(SpvOpCompositeExtract, type, {source, node.Imm[0]});
    uint32_t operands[6] = {source, source};
    for (uint32_t i = 0; i < count; ++i)
        operands[2 + i] = node.Imm[i];
    return Op(SpvOpVectorShuffle, type, operands, count + 2);
}

// SPIR-V 1.0 selects component-wise: a vector select needs a condition of
// the arms' width, so a scalar one is splatted.
uint32_t SpirvEmitter::Select(const CKJitNode &node) {
    uint32_t condition = Value(node.Operands[0]);
    const uint32_t count = CKJitComponentCount(node.Type);
    if (count > 1 && CKJitComponentCount(m_Shader.Nodes[(int)node.Operands[0]].Type) == 1) {
        const uint32_t parts[4] = {condition, condition, condition, condition};
        condition = Op(SpvOpCompositeConstruct, BoolType(count), parts, count);
    }
    return Op(SpvOpSelect, TypeOf(node.Type), {condition, Value(node.Operands[1]), Value(node.Operands[2])});
}

uint32_t SpirvEmitter::Sample(const CKJitNode &node) {
    const uint32_t image = SampledImage(node);
    const uint32_t coordinate = Value(node.Operands[0]);
    const CKJitNode &bias = m_Shader.Nodes[(int)node.Operands[1]];
    if (bias.Op == CKJIT_OP_CONSTANT && (bias.Imm[0] & 0x7fffffffu) == 0)
        return Op(SpvOpImageSampleImplicitLod, FloatType(4), {image, coordinate});
    return Op(SpvOpImageSampleImplicitLod, FloatType(4),
              {image, coordinate, SpvImageOperandsBiasMask, Value(node.Operands[1])});
}

// Regions are selection constructs: the header branches to the then arm or the
// else block, both arms to the merge block, where PHIs take each result from
// the block that ended its arm. Loops are loop constructs: phis in the header
// take the index and each carried value from before the loop or from the
// continue block, which the body branches to and which increments the index
// and branches back. The header branches to the body while the index is below
// the count and the bound, and otherwise to the merge block, which the header
// dominates: the carried values' phis are the loop's RESULTs there.
uint32_t SpirvEmitter::Control(uint32_t index) {
    const CKJitNode &node = m_Shader.Nodes[(int)index];
    switch (node.Op) {
    case CKJIT_OP_IF: {
        const uint32_t then = NewId();
        const Region region = {NewId(), NewId()};
        m_Body.Emit(SpvOpSelectionMerge, {region.Merge, SpvSelectionControlNone});
        m_Body.Emit(SpvOpBranchConditional, {Value(node.Operands[0]), then, region.Else});
        m_Regions.PushBack(region);
        return Enter(then);
    }
    case CKJIT_OP_ELSE:
        m_Body.Emit(SpvOpBranch, {m_Regions.Back().Merge});
        return Enter(m_Regions.Back().Else);
    case CKJIT_OP_ENDIF: {
        m_Body.Emit(SpvOpBranch, {m_Regions.Back().Merge});
        const uint32_t end = Enter(m_Regions.Back().Merge);
        m_Regions.PopBack();
        return end;
    }
    case CKJIT_OP_LOOP: {
        const CKJitNode &count = m_Shader.Nodes[(int)node.Operands[0]];
        const int32_t bound = (int32_t)node.Imm[0];
        uint32_t trips;
        if (count.Op == CKJIT_OP_CONSTANT)
            trips = IntConstant((int32_t)count.Imm[0] < bound ? (int32_t)count.Imm[0] : bound);
        else
            trips = Glsl(GLSLstd450SMin, IntType(), {Value(node.Operands[0]), IntConstant(bound)});
        const Region region = {NewId(), NewId(), NewId(), NewId(), NewId(), m_Patches.Size()};
        m_Body.Emit(SpvOpBranch, {region.Header});
        const uint32_t preheader = Enter(region.Header);
        m_Body.Emit(SpvOpPhi, {IntType(), region.Index, IntConstant(0), preheader, region.Next, region.Else});
        // The INDEX and CARRYs heading the body are the header's phis.
        uint32_t next = index + 1;
        if (next < (uint32_t)m_Shader.Nodes.Size() && m_Shader.Nodes[(int)next].Op == CKJIT_OP_INDEX)
            m_Values[(int)next++] = region.Index;
        for (; next < (uint32_t)m_Shader.Nodes.Size() && m_Shader.Nodes[(int)next].Op == CKJIT_OP_CARRY; ++next) {
            const CKJitNode &carry = m_Shader.Nodes[(int)next];
            m_Values[(int)next] =
                Op(SpvOpPhi, TypeOf(carry.Type), {Value(carry.Operands[0]), preheader, 0, region.Else});
            m_Patches.PushBack(m_Body.Size() - 2);
        }
        const uint32_t body = NewId();
        const uint32_t test = Op(SpvOpSLessThan, BoolType(1), {region.Index, trips});
        m_Body.Emit(SpvOpLoopMerge, {region.Merge, region.Else, SpvLoopControlNone});
        m_Body.Emit(SpvOpBranchConditional, {test, body, region.Merge});
        m_Regions.PushBack(region);
        Enter(body);
        return preheader;
    }
    case CKJIT_OP_INDEX:
    case CKJIT_OP_CARRY: return Value(index);
    case CKJIT_OP_ENDLOOP: {
        const Region region = m_Regions.PopBack();
        m_Body.Emit(SpvOpBranch, {region.Else});
        const uint32_t end = Enter(region.Else);
        m_Body.Emit(SpvOpIAdd, {IntType(), region.Next, region.Index, IntConstant(1)});
        m_Body.Emit(SpvOpBranch, {region.Header});
        Enter(region.Merge);
        // The RESULTs directly after name the nexts, in the carried values'
        // order.
        for (int k = region.Patches; k < m_Patches.Size(); ++k) {
            const CKJitNode &result = m_Shader.Nodes[(int)index + 1 + (k - region.Patches)];
            m_Body.Patch(m_Patches[k], Value(result.Operands[1]));
        }
        m_Patches.Resize(region.Patches);
        return end;
    }
    case CKJIT_OP_RESULT: return Value(node.Operands[0]);
    default: {
        const uint32_t endif = node.Operands[2];
        const uint32_t elseMarker = m_Shader.Nodes[(int)endif].Operands[0];
        return Op(SpvOpPhi, TypeOf(node.Type),
                  {Value(node.Operands[0]), Value(elseMarker), Value(node.Operands[1]), Value(endif)});
    }
    }
}

// A fetch takes the texel and its mip apart.
uint32_t SpirvEmitter::Load(const CKJitNode &node, uint32_t texel) {
    const uint32_t image = Image(node);
    const uint32_t count = CKJitComponentCount(m_Shader.Nodes[(int)node.Operands[0]].Type) - 1;
    const uint32_t selectors[5] = {texel, texel, 0, 1, 2};
    const uint32_t coordinate = Op(SpvOpVectorShuffle, TypeOf(CKJitIntType(count)), selectors, count + 2);
    const uint32_t mip = Op(SpvOpCompositeExtract, IntType(), {texel, count});
    return Op(SpvOpImageFetch, TypeOf(node.Type), {image, coordinate, SpvImageOperandsLodMask, mip});
}

// Vulkan leaves OpSRem and OpSMod undefined for negative operands, so the
// remainder is taken of non-negative values. With m = a >> 31, 0 or all
// ones, the floored a mod b is ((a ^ m) rem b ^ m) + (b & m).
uint32_t SpirvEmitter::Modulo(const CKJitNode &node, uint32_t a, uint32_t b) {
    const uint32_t type = TypeOf(node.Type);
    const uint32_t sign = Op(SpvOpShiftRightArithmetic, type, {a, ConstantSplat(node.Type, 31)});
    const uint32_t magnitude = Op(SpvOpBitwiseXor, type, {a, sign});
    const uint32_t remainder = Op(SpvOpSRem, type, {magnitude, b});
    const uint32_t restored = Op(SpvOpBitwiseXor, type, {remainder, sign});
    const uint32_t offset = Op(SpvOpBitwiseAnd, type, {b, sign});
    return Op(SpvOpIAdd, type, {restored, offset});
}

// HLSL uses the low five bits of a shift count; SPIR-V leaves larger counts
// undefined.
uint32_t SpirvEmitter::ShiftCount(uint32_t node) {
    const CKJitNode &count = m_Shader.Nodes[(int)node];
    if (count.Op == CKJIT_OP_CONSTANT) {
        uint32_t bits[4];
        for (uint32_t i = 0; i < CKJitComponentCount(count.Type); ++i)
            bits[i] = count.Imm[i] & 31u;
        return Constant(count.Type, bits);
    }
    return Op(SpvOpBitwiseAnd, TypeOf(count.Type), {Value(node), ConstantSplat(count.Type, 31)});
}

uint32_t SpirvEmitter::Translate(uint32_t index) {
    const CKJitNode &node = m_Shader.Nodes[(int)index];
    const uint32_t type = TypeOf(node.Type);
    const uint32_t a = node.OperandCount > 0 ? Value(node.Operands[0]) : 0;
    const uint32_t b = node.OperandCount > 1 ? Value(node.Operands[1]) : 0;
    switch (node.Op) {
    case CKJIT_OP_CONSTANT: return Constant(node.Type, node.Imm);
    case CKJIT_OP_INPUT: {
        const CKJitInput &input = m_Shader.Inputs[(int)node.Imm[0]];
        const uint32_t value = Op(SpvOpLoad, InputType(input), {m_Inputs[(int)node.Imm[0]]});
        return input.Scalar == CKJIT_INPUT_UINT ? Op(SpvOpBitcast, type, {value}) : value;
    }
    case CKJIT_OP_UNIFORM: {
        const uint32_t row = Op(SpvOpAccessChain, PointerType(SpvStorageClassUniform, type),
                                {UniformBlock(node.Imm[1]), IntConstant(0), IntConstant((int32_t)node.Imm[0])});
        return Op(SpvOpLoad, type, {row});
    }
    case CKJIT_OP_SWIZZLE: return Swizzle(node);
    case CKJIT_OP_CONSTRUCT: {
        uint32_t parts[4];
        for (uint32_t i = 0; i < node.OperandCount; ++i)
            parts[i] = Value(node.Operands[i]);
        return Op(SpvOpCompositeConstruct, type, parts, node.OperandCount);
    }
    case CKJIT_OP_ADD: return Op(SpvOpFAdd, type, {a, b});
    case CKJIT_OP_SUB: return Op(SpvOpFSub, type, {a, b});
    case CKJIT_OP_MUL: return Op(SpvOpFMul, type, {a, b});
    case CKJIT_OP_MAD: return Glsl(GLSLstd450Fma, type, {a, b, Value(node.Operands[2])});
    case CKJIT_OP_MIX: return Glsl(GLSLstd450FMix, type, {a, b, Value(node.Operands[2])});
    case CKJIT_OP_DIV: return Op(SpvOpFDiv, type, {a, b});
    case CKJIT_OP_MIN: return Glsl(GLSLstd450NMin, type, {a, b});
    case CKJIT_OP_MAX: return Glsl(GLSLstd450NMax, type, {a, b});
    case CKJIT_OP_NEG: return Op(SpvOpFNegate, type, {a});
    case CKJIT_OP_ABS: return Glsl(GLSLstd450FAbs, type, {a});
    case CKJIT_OP_SATURATE:
        return Glsl(GLSLstd450FClamp, type, {a, FloatSplat(0.0f, node.Type), FloatSplat(1.0f, node.Type)});
    case CKJIT_OP_FLOOR: return Glsl(GLSLstd450Floor, type, {a});
    case CKJIT_OP_CEIL: return Glsl(GLSLstd450Ceil, type, {a});
    case CKJIT_OP_ROUND_EVEN: return Glsl(GLSLstd450RoundEven, type, {a});
    case CKJIT_OP_EXP2: return Glsl(GLSLstd450Exp2, type, {a});
    case CKJIT_OP_LOG2: return Glsl(GLSLstd450Log2, type, {a});
    case CKJIT_OP_SQRT: return Glsl(GLSLstd450Sqrt, type, {a});
    case CKJIT_OP_DOT: return Op(SpvOpDot, type, {a, b});
    case CKJIT_OP_DDX: return Op(SpvOpDPdx, type, {a});
    case CKJIT_OP_DDY: return Op(SpvOpDPdy, type, {a});
    case CKJIT_OP_LT: return Op(SpvOpFOrdLessThan, type, {a, b});
    case CKJIT_OP_LE: return Op(SpvOpFOrdLessThanEqual, type, {a, b});
    case CKJIT_OP_EQ: return Op(SpvOpFOrdEqual, type, {a, b});
    case CKJIT_OP_NE: return Op(SpvOpFUnordNotEqual, type, {a, b});
    case CKJIT_OP_FTOI: return Op(SpvOpConvertFToS, type, {a});
    case CKJIT_OP_ITOF: return Op(SpvOpConvertSToF, type, {a});
    case CKJIT_OP_IADD: return Op(SpvOpIAdd, type, {a, b});
    case CKJIT_OP_ISUB: return Op(SpvOpISub, type, {a, b});
    case CKJIT_OP_IMUL: return Op(SpvOpIMul, type, {a, b});
    case CKJIT_OP_IMIN: return Glsl(GLSLstd450SMin, type, {a, b});
    case CKJIT_OP_IMAX: return Glsl(GLSLstd450SMax, type, {a, b});
    case CKJIT_OP_IMOD: return Modulo(node, a, b);
    case CKJIT_OP_IAND: return Op(SpvOpBitwiseAnd, type, {a, b});
    case CKJIT_OP_ISHR: return Op(SpvOpShiftRightArithmetic, type, {a, ShiftCount(node.Operands[1])});
    case CKJIT_OP_ILT: return Op(SpvOpSLessThan, type, {a, b});
    case CKJIT_OP_ILE: return Op(SpvOpSLessThanEqual, type, {a, b});
    case CKJIT_OP_IEQ: return Op(SpvOpIEqual, type, {a, b});
    case CKJIT_OP_INE: return Op(SpvOpINotEqual, type, {a, b});
    case CKJIT_OP_AND: return Op(SpvOpLogicalAnd, type, {a, b});
    case CKJIT_OP_OR: return Op(SpvOpLogicalOr, type, {a, b});
    case CKJIT_OP_NOT: return Op(SpvOpLogicalNot, type, {a});
    case CKJIT_OP_ANY: return Op(SpvOpAny, type, {a});
    case CKJIT_OP_ALL: return Op(SpvOpAll, type, {a});
    case CKJIT_OP_SELECT: return Select(node);
    case CKJIT_OP_IF:
    case CKJIT_OP_ELSE:
    case CKJIT_OP_ENDIF:
    case CKJIT_OP_PHI:
    case CKJIT_OP_LOOP:
    case CKJIT_OP_INDEX:
    case CKJIT_OP_CARRY:
    case CKJIT_OP_ENDLOOP:
    case CKJIT_OP_RESULT: return Control(index);
    case CKJIT_OP_SAMPLE: return Sample(node);
    case CKJIT_OP_SAMPLE_LEVEL:
        return Op(SpvOpImageSampleExplicitLod, type, {SampledImage(node), a, SpvImageOperandsLodMask, b});
    case CKJIT_OP_SAMPLE_GRAD:
        return Op(SpvOpImageSampleExplicitLod, type,
                  {SampledImage(node), a, SpvImageOperandsGradMask, b, Value(node.Operands[2])});
    case CKJIT_OP_CALC_LOD: {
        // The second component is the LOD before clamping.
        const uint32_t lods = Query(SpvOpImageQueryLod, FloatType(2), {SampledImage(node), a});
        return Op(SpvOpCompositeExtract, type, {lods, 1});
    }
    case CKJIT_OP_SAMPLE_CMP: return Op(SpvOpImageSampleDrefImplicitLod, type, {SampledImage(node), a, b});
    case CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO:
        return Op(SpvOpImageSampleDrefExplicitLod, type,
                  {SampledImage(node), a, b, SpvImageOperandsLodMask, FloatSplat(0.0f, CKJIT_TYPE_FLOAT)});
    case CKJIT_OP_LOAD: return Load(node, a);
    case CKJIT_OP_SIZE: return Query(SpvOpImageQuerySizeLod, type, {Image(node), a});
    case CKJIT_OP_LEVELS: return Query(SpvOpImageQueryLevels, type, {Image(node)});
    case CKJIT_OP_COUNT: break;
    }
    return 0;
}

void SpirvEmitter::Emit(XArray<uint32_t> &words) {
    DeclareInterface();

    const uint32_t voidType = DeclareType(SpvOpTypeVoid, {});
    m_Body.Emit(SpvOpFunction, {voidType, m_Main, SpvFunctionControlNone, DeclareType(SpvOpTypeFunction, {voidType})});
    Enter(NewId());
    for (int i = 0; i < m_Shader.Nodes.Size(); ++i) {
        const CKJitNode &node = m_Shader.Nodes[i];
        if ((CKJitOpFlags(node.Op) & CKJIT_OPFLAG_TEXTURE) == 0)
            continue;
        if (node.Op == CKJIT_OP_LOAD || node.Op == CKJIT_OP_SIZE || node.Op == CKJIT_OP_LEVELS)
            Image(node);
        else
            SampledImage(node);
    }
    m_Values.Resize(m_Shader.Nodes.Size());
    for (int i = 0; i < m_Shader.Nodes.Size(); ++i)
        m_Values[i] = Translate((uint32_t)i);

    // Every value is computed before the discard, so no implicit-LOD sample,
    // LOD query or derivative runs after a quad neighbour was killed.
    if (m_Discard.IsValid()) {
        const uint32_t kill = NewId();
        const uint32_t merge = NewId();
        m_Body.Emit(SpvOpSelectionMerge, {merge, SpvSelectionControlNone});
        m_Body.Emit(SpvOpBranchConditional, {Value(m_Discard.Id), kill, merge});
        Enter(kill);
        m_Body.Emit(SpvOpKill, {});
        Enter(merge);
    }
    m_Body.Emit(SpvOpStore, {m_Output, Value(m_Primary.Id)});
    for (int i = 0; i < m_Outputs.Size(); ++i)
        m_Body.Emit(SpvOpStore, {m_Outputs[i], Value(m_Vertex->Outputs[i].Value.Id)});
    if (m_ClipOutput) {
        uint32_t distances[8];
        for (int i = 0; i < m_Vertex->ClipDistances.Size(); ++i)
            distances[i] = Value(m_Vertex->ClipDistances[i].Id);
        const uint32_t array = Op(SpvOpCompositeConstruct, m_ClipType, distances, m_Vertex->ClipDistances.Size());
        m_Body.Emit(SpvOpStore, {m_ClipOutput, array});
    }
    m_Body.Emit(SpvOpReturn, {});
    m_Body.Emit(SpvOpFunctionEnd, {});

    // The preamble names ids that only now all exist.
    XArray<uint32_t> operands;
    SpirvSection preamble;
    preamble.Emit(SpvOpCapability, {SpvCapabilityShader});
    if (m_ClipOutput)
        preamble.Emit(SpvOpCapability, {SpvCapabilityClipDistance});
    if (m_ImageQuery)
        preamble.Emit(SpvOpCapability, {SpvCapabilityImageQuery});
    operands.PushBack(m_GlslImport);
    AppendString(operands, "GLSL.std.450");
    preamble.Emit(SpvOpExtInstImport, operands.Begin(), (uint32_t)operands.Size());
    preamble.Emit(SpvOpMemoryModel, {SpvAddressingModelLogical, SpvMemoryModelGLSL450});
    operands.Clear();
    operands.PushBack(m_Vertex ? SpvExecutionModelVertex : SpvExecutionModelFragment);
    operands.PushBack(m_Main);
    AppendString(operands, "main");
    operands += m_Interface;
    preamble.Emit(SpvOpEntryPoint, operands.Begin(), (uint32_t)operands.Size());
    if (!m_Vertex)
        preamble.Emit(SpvOpExecutionMode, {m_Main, SpvExecutionModeOriginUpperLeft});

    words.Clear();
    words.PushBack(SpvMagicNumber);
    words.PushBack(SpvVersion10);
    words.PushBack(0); // generator: unregistered
    words.PushBack(m_Bound);
    words.PushBack(0); // schema
    words += preamble.Words();
    words += m_Annotations.Words();
    words += m_Globals.Words();
    words += m_Body.Words();
}

} // namespace

bool CKJitEmitSpirv(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout, XArray<uint32_t> &words) {
    if (!CKJitVerify(shader))
        return false;
    SpirvEmitter emitter(shader, layout, shader.Color, shader.Discard);
    emitter.Emit(words);
    return true;
}

bool CKJitEmitSpirv(const CKJitVertexShader &shader, const CKJitResourceLayout &layout, XArray<uint32_t> &words) {
    if (!CKJitVerify(shader))
        return false;
    SpirvEmitter emitter(shader, layout, shader.Position, CKJitValue(), &shader);
    emitter.Emit(words);
    return true;
}
