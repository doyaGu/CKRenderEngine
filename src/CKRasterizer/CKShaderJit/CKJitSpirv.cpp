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
    SpvOpImageFetch = 95,
    SpvOpImage = 100,
    SpvOpImageQuerySizeLod = 103,
    SpvOpImageQueryLod = 105,
    SpvOpImageQueryLevels = 106,
    SpvOpConvertFToS = 110,
    SpvOpConvertSToF = 111,
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
    SpvOpSelectionMerge = 247,
    SpvOpLabel = 248,
    SpvOpBranchConditional = 250,
    SpvOpKill = 252,
    SpvOpReturn = 253,

    SpvCapabilityShader = 1,
    SpvCapabilityImageQuery = 50,
    SpvAddressingModelLogical = 0,
    SpvMemoryModelGLSL450 = 1,
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
    SpirvEmitter(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout);

    void Emit(XArray<uint32_t> &words);

private:
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

    void DeclareInterface();
    uint32_t UniformBlock();
    uint32_t ImageType(uint32_t dim);
    uint32_t SampledImage(const CKJitNode &node);
    uint32_t Image(const CKJitNode &node);
    uint32_t Query(uint32_t opcode, uint32_t type, std::initializer_list<uint32_t> operands);
    uint32_t Value(uint32_t node) const { return m_Values[(int)node]; }
    uint32_t Translate(const CKJitNode &node);
    uint32_t Swizzle(const CKJitNode &node);
    uint32_t Select(const CKJitNode &node);
    uint32_t Sample(const CKJitNode &node);
    uint32_t Load(const CKJitNode &node, uint32_t texel);
    uint32_t Modulo(const CKJitNode &node, uint32_t a, uint32_t b);
    uint32_t ShiftCount(uint32_t node);

    const CKJitFragmentShader &m_Shader;
    const CKJitResourceLayout &m_Layout;
    SpirvSection m_Annotations;
    SpirvSection m_Globals; // types, constants and variables
    SpirvSection m_Body;
    XSHashTable<uint32_t, Declaration, DeclarationHash, DeclarationEqual> m_Declarations;
    XArray<uint32_t> m_Values;    // SPIR-V id of every node
    XArray<uint32_t> m_Inputs;    // variable of every shader input
    XArray<uint32_t> m_Interface; // entry point interface: inputs and output
    uint32_t m_SampledImages[CKJIT_MAX_SAMPLERS]; // loaded on first use
    uint32_t m_Images[CKJIT_MAX_SAMPLERS];        // taken from the sampled image on first use
    uint32_t m_Bound;
    uint32_t m_Main;
    uint32_t m_GlslImport;
    uint32_t m_Output;
    uint32_t m_UniformBlock;
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

SpirvEmitter::SpirvEmitter(const CKJitFragmentShader &shader, const CKJitResourceLayout &layout)
    : m_Shader(shader), m_Layout(layout), m_Bound(1), m_Output(0), m_UniformBlock(0), m_ImageQuery(false) {
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
    uint32_t words[8];
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

// Every declared input is part of the interface, read or not.
void SpirvEmitter::DeclareInterface() {
    for (int i = 0; i < m_Shader.Inputs.Size(); ++i) {
        const CKJitInput &input = m_Shader.Inputs[i];
        const uint32_t variable = Variable(SpvStorageClassInput, FloatType(input.Components));
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
    Decorate(m_Output, SpvDecorationLocation, 0);
    m_Interface.PushBack(m_Output);
}

// struct { float4 rows[UniformVec4Count]; }, declared when first read.
uint32_t SpirvEmitter::UniformBlock() {
    if (m_UniformBlock == 0) {
        const uint32_t rows = DeclareType(SpvOpTypeArray, {FloatType(4), IntConstant((int32_t)m_Shader.UniformVec4Count)});
        Decorate(rows, SpvDecorationArrayStride, 16);
        const uint32_t block = DeclareType(SpvOpTypeStruct, {rows});
        m_Annotations.Emit(SpvOpMemberDecorate, {block, 0, SpvDecorationOffset, 0});
        m_Annotations.Emit(SpvOpDecorate, {block, SpvDecorationBlock});
        m_UniformBlock = Variable(SpvStorageClassUniform, block);
        Decorate(m_UniformBlock, SpvDecorationDescriptorSet, m_Layout.UniformSpace);
        Decorate(m_UniformBlock, SpvDecorationBinding, m_Layout.UniformBinding);
    }
    return m_UniformBlock;
}

uint32_t SpirvEmitter::ImageType(uint32_t dim) {
    static const uint32_t kDims[] = {SpvDim2D, SpvDimCube, SpvDim3D};
    return DeclareType(SpvOpTypeImage, {FloatType(1), kDims[dim], 0, 0, 0, 1, SpvImageFormatUnknown});
}

// The combined image sampler of a node's slot, declared and loaded when first
// used. The body is one block until the discard, so the load dominates every
// later use.
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

uint32_t SpirvEmitter::Translate(const CKJitNode &node) {
    const uint32_t type = TypeOf(node.Type);
    const uint32_t a = node.OperandCount > 0 ? Value(node.Operands[0]) : 0;
    const uint32_t b = node.OperandCount > 1 ? Value(node.Operands[1]) : 0;
    switch (node.Op) {
    case CKJIT_OP_CONSTANT: return Constant(node.Type, node.Imm);
    case CKJIT_OP_INPUT: return Op(SpvOpLoad, type, {m_Inputs[(int)node.Imm[0]]});
    case CKJIT_OP_UNIFORM: {
        const uint32_t row = Op(SpvOpAccessChain, PointerType(SpvStorageClassUniform, type),
                                {UniformBlock(), IntConstant(0), IntConstant((int32_t)node.Imm[0])});
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
    m_Body.Emit(SpvOpLabel, {NewId()});
    m_Values.Resize(m_Shader.Nodes.Size());
    for (int i = 0; i < m_Shader.Nodes.Size(); ++i)
        m_Values[i] = Translate(m_Shader.Nodes[i]);

    // Every value is computed before the discard, so no implicit-LOD sample,
    // LOD query or derivative runs after a quad neighbour was killed.
    if (m_Shader.Discard.IsValid()) {
        const uint32_t kill = NewId();
        const uint32_t merge = NewId();
        m_Body.Emit(SpvOpSelectionMerge, {merge, SpvSelectionControlNone});
        m_Body.Emit(SpvOpBranchConditional, {Value(m_Shader.Discard.Id), kill, merge});
        m_Body.Emit(SpvOpLabel, {kill});
        m_Body.Emit(SpvOpKill, {});
        m_Body.Emit(SpvOpLabel, {merge});
    }
    m_Body.Emit(SpvOpStore, {m_Output, Value(m_Shader.Color.Id)});
    m_Body.Emit(SpvOpReturn, {});
    m_Body.Emit(SpvOpFunctionEnd, {});

    // The preamble names ids that only now all exist.
    XArray<uint32_t> operands;
    SpirvSection preamble;
    preamble.Emit(SpvOpCapability, {SpvCapabilityShader});
    if (m_ImageQuery)
        preamble.Emit(SpvOpCapability, {SpvCapabilityImageQuery});
    operands.PushBack(m_GlslImport);
    AppendString(operands, "GLSL.std.450");
    preamble.Emit(SpvOpExtInstImport, operands.Begin(), (uint32_t)operands.Size());
    preamble.Emit(SpvOpMemoryModel, {SpvAddressingModelLogical, SpvMemoryModelGLSL450});
    operands.Clear();
    operands.PushBack(SpvExecutionModelFragment);
    operands.PushBack(m_Main);
    AppendString(operands, "main");
    operands += m_Interface;
    preamble.Emit(SpvOpEntryPoint, operands.Begin(), (uint32_t)operands.Size());
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
    SpirvEmitter emitter(shader, layout);
    emitter.Emit(words);
    return true;
}
