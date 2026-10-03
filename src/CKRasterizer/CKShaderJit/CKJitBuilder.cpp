#include "CKJitBuilder.h"

#include <cfenv>
#include <cmath>
#include <cstring>

namespace {

uint32_t FloatBits(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return bits;
}

float BitsFloat(uint32_t bits) {
    float x;
    std::memcpy(&x, &bits, sizeof(x));
    return x;
}

// GPUs may flush denormals and disagree on NaN propagation; keep both at
// runtime.
bool Foldable(float x) {
    return !std::isnan(x) && std::fpclassify(x) != FP_SUBNORMAL;
}

// Round binary32 to an integral value, ties to even, without consulting or
// changing the host FP environment. Preserve the sign when rounding to zero.
float RoundEven(float value) {
    const uint32_t bits = FloatBits(value);
    uint32_t magnitude = bits & 0x7fffffffu;
    const int exponent = (int)(magnitude >> 23) - 127;
    if (exponent >= 23)
        return value; // Already integral, or non-finite.
    if (exponent < 0) {
        magnitude = magnitude > 0x3f000000u ? 0x3f800000u : 0u;
    } else {
        const uint32_t unit = 1u << (23 - exponent);
        const uint32_t fraction = magnitude & (unit - 1u);
        magnitude &= ~(unit - 1u);
        if (fraction > unit / 2u || (fraction == unit / 2u && (magnitude & unit) != 0))
            magnitude += unit;
    }
    return BitsFloat((bits & 0x80000000u) | magnitude);
}

bool FoldFloat(CKJitOp op, float a, float b, float &out) {
    if (!Foldable(a) || !Foldable(b))
        return false;
    // The shader does not inherit the caller's directed rounding mode.
    // Keep arithmetic on the GPU in that case; the other folds are exact or
    // have an explicit rounding direction.
    if ((op == CKJIT_OP_ADD || op == CKJIT_OP_SUB || op == CKJIT_OP_MUL || op == CKJIT_OP_DIV) &&
        std::fegetround() != FE_TONEAREST) {
        return false;
    }
    switch (op) {
    case CKJIT_OP_ADD: out = a + b; break;
    case CKJIT_OP_SUB: out = a - b; break;
    case CKJIT_OP_MUL: out = a * b; break;
    case CKJIT_OP_DIV: out = a / b; break;
    case CKJIT_OP_MIN: out = b < a ? b : a; break;
    case CKJIT_OP_MAX: out = a < b ? b : a; break;
    case CKJIT_OP_NEG: out = -a; break;
    case CKJIT_OP_ABS: out = std::fabs(a); break;
    case CKJIT_OP_SATURATE: out = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); break;
    case CKJIT_OP_FLOOR: out = std::floor(a); break;
    case CKJIT_OP_CEIL: out = std::ceil(a); break;
    case CKJIT_OP_ROUND_EVEN: out = RoundEven(a); break;
    case CKJIT_OP_EXP2:
        // Integral exponents give exact powers of two on every GPU.
        if (a != std::floor(a) || a < -126.0f || a > 127.0f)
            return false;
        out = std::ldexp(1.0f, (int)a);
        break;
    case CKJIT_OP_DDX:
    case CKJIT_OP_DDY:
        // Every pixel of the quad holds the constant; an infinity minus
        // itself is NaN.
        if (std::isinf(a))
            return false;
        out = 0.0f;
        break;
    default:
        return false;
    }
    return Foldable(out);
}

bool FoldCompare(CKJitOp op, float a, float b, bool &out) {
    if (!Foldable(a) || !Foldable(b))
        return false;
    switch (op) {
    case CKJIT_OP_LT: out = a < b; return true;
    case CKJIT_OP_LE: out = a <= b; return true;
    case CKJIT_OP_EQ: out = a == b; return true;
    case CKJIT_OP_NE: out = a != b; return true;
    default: return false;
    }
}

// One component of an operation on constant bits; false leaves it to the
// GPU. Booleans are 0 or 1.
bool FoldComponent(CKJitOp op, uint32_t a, uint32_t b, uint32_t &out) {
    const int32_t x = (int32_t)a;
    const int32_t y = (int32_t)b;
    switch (op) {
    case CKJIT_OP_LT:
    case CKJIT_OP_LE:
    case CKJIT_OP_EQ:
    case CKJIT_OP_NE: {
        bool result;
        if (!FoldCompare(op, BitsFloat(a), BitsFloat(b), result))
            return false;
        out = result ? 1u : 0u;
        return true;
    }
    case CKJIT_OP_FTOI: {
        // Only finite values whose truncated result fits int32 are portable
        // across backends. In particular, SPIR-V does not promise saturation.
        const float value = BitsFloat(a);
        if (!Foldable(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return false;
        out = (uint32_t)(int32_t)value;
        return true;
    }
    case CKJIT_OP_ITOF:
        // Only exact conversions: GPUs need not round the others to nearest.
        if (x < -16777216 || x > 16777216)
            return false;
        out = FloatBits((float)x);
        return true;
    case CKJIT_OP_IADD: out = a + b; return true;
    case CKJIT_OP_ISUB: out = a - b; return true;
    case CKJIT_OP_IMUL: out = a * b; return true;
    case CKJIT_OP_IMIN: out = (uint32_t)(y < x ? y : x); return true;
    case CKJIT_OP_IMAX: out = (uint32_t)(x < y ? y : x); return true;
    case CKJIT_OP_IMOD: {
        if (y <= 0)
            return false;
        const int32_t remainder = x % y;
        out = (uint32_t)(remainder < 0 ? remainder + y : remainder);
        return true;
    }
    case CKJIT_OP_IAND: out = a & b; return true;
    // Shader shifts use the low five bits of the shift count.
    case CKJIT_OP_ISHR: out = (uint32_t)(x >> (b & 31u)); return true;
    case CKJIT_OP_ILT: out = x < y ? 1u : 0u; return true;
    case CKJIT_OP_ILE: out = x <= y ? 1u : 0u; return true;
    case CKJIT_OP_IEQ: out = a == b ? 1u : 0u; return true;
    case CKJIT_OP_INE: out = a != b ? 1u : 0u; return true;
    case CKJIT_OP_AND: out = a != 0 && b != 0 ? 1u : 0u; return true;
    case CKJIT_OP_OR: out = a != 0 || b != 0 ? 1u : 0u; return true;
    case CKJIT_OP_NOT: out = a == 0 ? 1u : 0u; return true;
    default: {
        float result;
        if (!FoldFloat(op, BitsFloat(a), BitsFloat(b), result))
            return false;
        out = FloatBits(result);
        return true;
    }
    }
}

int SwizzleIndex(char c) {
    switch (c) {
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    default: return -1;
    }
}

CKJitType CoordinateType(CKJitSamplerDim dim) {
    return dim == CKJIT_SAMPLER_CUBE || dim == CKJIT_SAMPLER_3D ? CKJIT_TYPE_FLOAT3 : CKJIT_TYPE_FLOAT2;
}

bool IsLeaf(CKJitOp op) {
    return op == CKJIT_OP_CONSTANT || op == CKJIT_OP_INPUT || op == CKJIT_OP_UNIFORM;
}

CKJitNode MakeNode(CKJitOp op, CKJitType type, std::initializer_list<uint32_t> operands) {
    CKJitNode node;
    std::memset(&node, 0, sizeof(node));
    node.Op = op;
    node.Type = type;
    for (uint32_t operand : operands)
        node.Operands[node.OperandCount++] = operand;
    return node;
}

} // namespace

int CKJitBuilder::NodeHash::operator()(const CKJitNode &node) const {
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&node);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(node); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    // The table indexes with the low bits; fold the well-mixed high ones in.
    return (int)(hash ^ (hash >> 16));
}

int CKJitBuilder::NodeEqual::operator()(const CKJitNode &a, const CKJitNode &b) const {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

CKJitBuilder::CKJitBuilder(uint32_t uniformVec4Count)
    : CKJitBuilder(&uniformVec4Count, 1) {}

CKJitBuilder::CKJitBuilder(const uint32_t *uniformVec4Counts, uint32_t uniformBufferCount)
    : m_UniformBufferCount(0), m_Failed(uniformBufferCount > CKJIT_MAX_UNIFORM_BUFFERS) {
    std::memset(m_UniformVec4Counts, 0, sizeof(m_UniformVec4Counts));
    if (!m_Failed) {
        m_UniformBufferCount = uniformBufferCount;
        for (uint32_t buffer = 0; buffer < uniformBufferCount; ++buffer)
            m_UniformVec4Counts[buffer] = uniformVec4Counts[buffer];
    }
    std::memset(m_SamplerDims, 0xff, sizeof(m_SamplerDims));
    m_OpenScopes.PushBack(1);
}

CKJitValue CKJitBuilder::Fail() {
    m_Failed = true;
    return CKJitValue();
}

CKJitValue CKJitBuilder::Emit(const CKJitNode &node) {
    for (uint32_t i = 0; i < node.OperandCount; ++i) {
        if (!Valid(CKJitValue{node.Operands[i]}))
            return Fail();
    }
    return Intern(node);
}

// The seen value equal to the node, or the node added to the arm being built.
CKJitValue CKJitBuilder::Intern(const CKJitNode &source) {
    CKJitNode node = source;
    if ((CKJitOpFlags(node.Op) & CKJIT_OPFLAG_COMMUTATIVE) != 0 && node.Operands[1] < node.Operands[0]) {
        const uint32_t first = node.Operands[0];
        node.Operands[0] = node.Operands[1];
        node.Operands[1] = first;
    }
    if (const uint32_t *found = m_Lookup.FindPtr(node))
        return CKJitValue{*found};
    // The quad neighbours run it too only where control flow is uniform.
    if ((CKJitOpFlags(node.Op) & CKJIT_OPFLAG_QUAD) != 0 && Divergent())
        return Fail();
    const CKJitValue value = Push(node, CurrentScope());
    m_Lookup.Insert(node, value.Id, TRUE);
    return value;
}

CKJitValue CKJitBuilder::Push(const CKJitNode &node, uint32_t scope) {
    const uint32_t id = (uint32_t)m_Nodes.Size();
    // Carried values vary, as the verifier does not look ahead to their nexts.
    uint8_t varying = node.Op == CKJIT_OP_INPUT || node.Op == CKJIT_OP_CARRY;
    for (uint32_t operand = 0; operand < node.OperandCount; ++operand)
        varying |= m_Varying[node.Operands[operand]];
    m_Nodes.PushBack(node);
    m_Scopes.PushBack(IsLeaf(node.Op) ? 0u : scope);
    m_Varying.PushBack(varying);
    return CKJitValue{id};
}

// Markers are never shared: each bounds its own region.
uint32_t CKJitBuilder::PushMarker(CKJitOp op, uint32_t operand, uint32_t scope, uint32_t imm) {
    CKJitNode node = MakeNode(op, CKJIT_TYPE_VOID, {operand});
    node.Imm[0] = imm;
    return Push(node, scope).Id;
}

uint32_t CKJitBuilder::OpenScope() {
    m_OpenScopes.PushBack(1);
    return (uint32_t)m_OpenScopes.Size() - 1u;
}

// Ends the arm being built: its values are no longer seen, nor shared.
void CKJitBuilder::CloseArm(const Region &region) {
    m_OpenScopes[region.Scope] = 0;
    for (int i = (int)region.Marker + 1; i < m_Nodes.Size(); ++i) {
        if (m_Scopes[i] == region.Scope && (CKJitOpFlags(m_Nodes[i].Op) & CKJIT_OPFLAG_MARKER) == 0)
            m_Lookup.Remove(m_Nodes[i]);
    }
}

CKJitValue CKJitBuilder::Emit(CKJitOp op, CKJitType type, std::initializer_list<CKJitValue> operands,
                              std::initializer_list<uint32_t> imm) {
    CKJitNode node;
    std::memset(&node, 0, sizeof(node));
    node.Op = op;
    node.Type = type;
    for (CKJitValue operand : operands)
        node.Operands[node.OperandCount++] = operand.Id;
    uint32_t count = 0;
    for (uint32_t value : imm)
        node.Imm[count++] = value;
    return Emit(node);
}

CKJitValue CKJitBuilder::Constant(CKJitType type, const uint32_t *bits) {
    CKJitNode node;
    std::memset(&node, 0, sizeof(node));
    node.Op = CKJIT_OP_CONSTANT;
    node.Type = type;
    for (uint32_t i = 0; i < CKJitComponentCount(type); ++i)
        node.Imm[i] = bits[i];
    return Emit(node);
}

CKJitValue CKJitBuilder::ConstantSplat(CKJitType type, uint32_t bits) {
    const uint32_t components[4] = {bits, bits, bits, bits};
    return Constant(type, components);
}

// The constant of an operation on constants, folded per component; invalid
// when a component is left to the GPU. b is invalid for unary operations.
CKJitValue CKJitBuilder::Fold(CKJitOp op, CKJitType type, CKJitValue a, CKJitValue b) {
    if (!IsConstant(a) || (b.IsValid() && !IsConstant(b)))
        return CKJitValue();
    uint32_t bits[4] = {};
    for (uint32_t i = 0; i < CKJitComponentCount(type); ++i) {
        if (!FoldComponent(op, m_Nodes[a.Id].Imm[i], b.IsValid() ? m_Nodes[b.Id].Imm[i] : 0u, bits[i]))
            return CKJitValue();
    }
    return Constant(type, bits);
}

CKJitValue CKJitBuilder::Float(float x) {
    const uint32_t bits[1] = {FloatBits(x)};
    return Constant(CKJIT_TYPE_FLOAT, bits);
}

CKJitValue CKJitBuilder::Float2(float x, float y) {
    const uint32_t bits[2] = {FloatBits(x), FloatBits(y)};
    return Constant(CKJIT_TYPE_FLOAT2, bits);
}

CKJitValue CKJitBuilder::Float3(float x, float y, float z) {
    const uint32_t bits[3] = {FloatBits(x), FloatBits(y), FloatBits(z)};
    return Constant(CKJIT_TYPE_FLOAT3, bits);
}

CKJitValue CKJitBuilder::Float4(float x, float y, float z, float w) {
    const uint32_t bits[4] = {FloatBits(x), FloatBits(y), FloatBits(z), FloatBits(w)};
    return Constant(CKJIT_TYPE_FLOAT4, bits);
}

CKJitValue CKJitBuilder::Int(int32_t x) {
    const uint32_t bits[1] = {(uint32_t)x};
    return Constant(CKJIT_TYPE_INT, bits);
}

CKJitValue CKJitBuilder::Bool(bool x) {
    const uint32_t bits[1] = {x ? 1u : 0u};
    return Constant(CKJIT_TYPE_BOOL, bits);
}

CKJitValue CKJitBuilder::Input(const CKJitInput &input) {
    if (input.Components < 1 || input.Components > 4 || input.Kind > CKJIT_INPUT_ATTRIBUTE ||
        (input.Kind == CKJIT_INPUT_FRAG_COORD && input.Components != 4) ||
        input.Scalar > CKJIT_INPUT_UINT || (input.Scalar == CKJIT_INPUT_UINT && input.Kind != CKJIT_INPUT_ATTRIBUTE)) {
        return Fail();
    }
    // The position is one input and every varying location another.
    const bool position = input.Kind == CKJIT_INPUT_FRAG_COORD;
    uint32_t index = 0;
    for (; index < (uint32_t)m_Inputs.Size(); ++index) {
        const CKJitInput &other = m_Inputs[index];
        if ((other.Kind == CKJIT_INPUT_FRAG_COORD) != position || (!position && other.Location != input.Location))
            continue;
        if (other.Components != input.Components || other.Kind != input.Kind || other.Scalar != input.Scalar)
            return Fail();
        break;
    }
    if (index == (uint32_t)m_Inputs.Size())
        m_Inputs.PushBack(input);
    return Emit(CKJIT_OP_INPUT, input.Type(), {}, {index});
}

CKJitValue CKJitBuilder::Uniform(uint32_t buffer, uint32_t row) {
    if (buffer >= m_UniformBufferCount || row >= m_UniformVec4Counts[buffer])
        return Fail();
    return Emit(CKJIT_OP_UNIFORM, CKJIT_TYPE_FLOAT4, {}, {row, buffer});
}

CKJitBuilder::ComponentRef CKJitBuilder::Source(CKJitValue value, uint32_t component) const {
    const CKJitNode &node = m_Nodes[value.Id];
    if (node.Op == CKJIT_OP_SWIZZLE)
        return Source(CKJitValue{node.Operands[0]}, node.Imm[component]);
    if (node.Op == CKJIT_OP_CONSTRUCT) {
        for (uint32_t i = 0; i < node.OperandCount; ++i) {
            const uint32_t width = CKJitComponentCount(m_Nodes[node.Operands[i]].Type);
            if (component < width)
                return Source(CKJitValue{node.Operands[i]}, component);
            component -= width;
        }
    }
    return ComponentRef{value.Id, component};
}

CKJitValue CKJitBuilder::SwizzleComponents(CKJitValue value, const uint32_t *selectors, uint32_t count) {
    if (!Valid(value) || count < 1 || count > 4)
        return Fail();
    const CKJitType type = TypeOf(value);
    const uint32_t width = CKJitComponentCount(type);
    bool identity = count == width;
    for (uint32_t i = 0; i < count; ++i) {
        if (selectors[i] >= width)
            return Fail();
        identity = identity && selectors[i] == i;
    }
    if (identity)
        return value;

    const CKJitNode node = m_Nodes[value.Id];
    uint32_t composed[4] = {};
    switch (node.Op) {
    case CKJIT_OP_CONSTANT:
        for (uint32_t i = 0; i < count; ++i)
            composed[i] = node.Imm[selectors[i]];
        return Constant(CKJitMakeType(type, count), composed);
    case CKJIT_OP_SWIZZLE:
        for (uint32_t i = 0; i < count; ++i)
            composed[i] = node.Imm[selectors[i]];
        return SwizzleComponents(CKJitValue{node.Operands[0]}, composed, count);
    case CKJIT_OP_CONSTRUCT: {
        // Selecting within a single part skips the construction.
        const ComponentRef first = Source(value, selectors[0]);
        composed[0] = first.Component;
        bool single = true;
        for (uint32_t i = 1; i < count && single; ++i) {
            const ComponentRef ref = Source(value, selectors[i]);
            single = ref.Value == first.Value;
            composed[i] = ref.Component;
        }
        if (single)
            return SwizzleComponents(CKJitValue{first.Value}, composed, count);
        break;
    }
    default:
        break;
    }

    CKJitNode swizzle;
    std::memset(&swizzle, 0, sizeof(swizzle));
    swizzle.Op = CKJIT_OP_SWIZZLE;
    swizzle.Type = CKJitMakeType(type, count);
    swizzle.OperandCount = 1;
    swizzle.Operands[0] = value.Id;
    for (uint32_t i = 0; i < count; ++i)
        swizzle.Imm[i] = selectors[i];
    return Emit(swizzle);
}

CKJitValue CKJitBuilder::Swizzle(CKJitValue value, const char *components) {
    uint32_t selectors[4];
    uint32_t count = 0;
    for (; components && components[count] != '\0'; ++count) {
        const int index = SwizzleIndex(components[count]);
        if (count == 4 || index < 0)
            return Fail();
        selectors[count] = (uint32_t)index;
    }
    return SwizzleComponents(value, selectors, count);
}

CKJitValue CKJitBuilder::Component(CKJitValue value, uint32_t index) {
    return SwizzleComponents(value, &index, 1);
}

CKJitValue CKJitBuilder::Splat(CKJitValue scalar, uint32_t components) {
    if (!Valid(scalar) || CKJitComponentCount(TypeOf(scalar)) != 1)
        return Fail();
    static const uint32_t kZero[4] = {};
    return SwizzleComponents(scalar, kZero, components);
}

CKJitValue CKJitBuilder::Construct(std::initializer_list<CKJitValue> parts) {
    ComponentRef refs[4];
    uint32_t count = 0;
    CKJitType kind = CKJIT_TYPE_COUNT; // of the first part, which every part has
    for (CKJitValue part : parts) {
        if (!Valid(part))
            return Fail();
        if (kind == CKJIT_TYPE_COUNT)
            kind = CKJitScalarOf(TypeOf(part));
        const uint32_t width = CKJitComponentCount(TypeOf(part));
        if (CKJitScalarOf(TypeOf(part)) != kind || count + width > 4)
            return Fail();
        for (uint32_t c = 0; c < width; ++c)
            refs[count++] = Source(part, c);
    }
    if (count == 0)
        return Fail();
    return ConstructComponents(refs, count);
}

// Canonical construction: one part per run of components taken in order from
// the same value, with adjacent constant components merged. Every component
// has one kind.
CKJitValue CKJitBuilder::ConstructComponents(const ComponentRef *refs, uint32_t count) {
    const CKJitType kind = CKJitScalarOf(m_Nodes[refs[0].Value].Type);
    CKJitNode node;
    std::memset(&node, 0, sizeof(node));
    node.Op = CKJIT_OP_CONSTRUCT;
    node.Type = CKJitMakeType(kind, count);
    for (uint32_t begin = 0; begin < count;) {
        const bool constant = m_Nodes[refs[begin].Value].Op == CKJIT_OP_CONSTANT;
        uint32_t selected[4];
        uint32_t end = begin;
        for (; end < count; ++end) {
            const ComponentRef ref = refs[end];
            if (constant ? m_Nodes[ref.Value].Op != CKJIT_OP_CONSTANT : ref.Value != refs[begin].Value)
                break;
            selected[end - begin] = constant ? m_Nodes[ref.Value].Imm[ref.Component] : ref.Component;
        }
        const CKJitValue part = constant ? Constant(CKJitMakeType(kind, end - begin), selected)
                                         : SwizzleComponents(CKJitValue{refs[begin].Value}, selected, end - begin);
        if (!part.IsValid())
            return part;
        if (begin == 0 && end == count)
            return part;
        node.Operands[node.OperandCount++] = part.Id;
        begin = end;
    }
    return Emit(node);
}

bool CKJitBuilder::Unify(CKJitValue &a, CKJitValue &b) {
    const uint32_t widthA = CKJitComponentCount(TypeOf(a));
    const uint32_t widthB = CKJitComponentCount(TypeOf(b));
    if (widthA == widthB)
        return true;
    if (widthA == 1)
        a = Splat(a, widthB);
    else if (widthB == 1)
        b = Splat(b, widthA);
    else
        return false;
    return a.IsValid() && b.IsValid();
}

// Both operands of the scalar kind, at one width.
bool CKJitBuilder::Operands(CKJitType kind, CKJitValue &a, CKJitValue &b) {
    return Valid(a) && Valid(b) && CKJitScalarOf(TypeOf(a)) == kind && CKJitScalarOf(TypeOf(b)) == kind &&
           Unify(a, b);
}

bool CKJitBuilder::IsConstant(CKJitValue value) const {
    return Valid(value) && m_Nodes[value.Id].Op == CKJIT_OP_CONSTANT;
}

bool CKJitBuilder::IsConstantSplat(CKJitValue value, float x) const {
    if (!IsConstant(value) || !CKJitIsFloat(TypeOf(value)))
        return false;
    const CKJitNode &node = m_Nodes[value.Id];
    for (uint32_t i = 0; i < CKJitComponentCount(node.Type); ++i) {
        if (BitsFloat(node.Imm[i]) != x)
            return false;
    }
    return true;
}

bool CKJitBuilder::IsConstantInt(CKJitValue value, int32_t x) const {
    if (!IsConstant(value) || !CKJitIsInt(TypeOf(value)))
        return false;
    const CKJitNode &node = m_Nodes[value.Id];
    for (uint32_t i = 0; i < CKJitComponentCount(node.Type); ++i) {
        if ((int32_t)node.Imm[i] != x)
            return false;
    }
    return true;
}

bool CKJitBuilder::IsConstantZero(CKJitValue value, bool negative) const {
    if (!IsConstant(value) || !CKJitIsFloat(TypeOf(value)))
        return false;
    const CKJitNode &node = m_Nodes[value.Id];
    for (uint32_t i = 0; i < CKJitComponentCount(node.Type); ++i) {
        if (node.Imm[i] != (negative ? 0x80000000u : 0u))
            return false;
    }
    return true;
}

bool CKJitBuilder::IsConstantBool(CKJitValue value, bool x) const {
    if (!IsConstant(value) || !CKJitIsBool(TypeOf(value)))
        return false;
    const CKJitNode &node = m_Nodes[value.Id];
    for (uint32_t i = 0; i < CKJitComponentCount(node.Type); ++i) {
        if ((node.Imm[i] != 0) != x)
            return false;
    }
    return true;
}

CKJitValue CKJitBuilder::FloatBinary(CKJitOp op, CKJitValue a, CKJitValue b) {
    if (!Operands(CKJIT_TYPE_FLOAT, a, b))
        return Fail();
    const CKJitType type = TypeOf(a);
    const CKJitValue folded = Fold(op, type, a, b);
    if (folded.IsValid())
        return folded;
    switch (op) {
    case CKJIT_OP_ADD:
        // With round-to-nearest, x + -0 and x - +0 preserve either zero's
        // sign. The opposite signs can turn a negative zero into +0.
        if (IsConstantZero(b, true))
            return a;
        if (IsConstantZero(a, true))
            return b;
        break;
    case CKJIT_OP_SUB:
        if (IsConstantZero(b, false))
            return a;
        break;
    case CKJIT_OP_MUL:
        if (IsConstantSplat(b, 1.0f))
            return a;
        if (IsConstantSplat(a, 1.0f))
            return b;
        break;
    case CKJIT_OP_DIV:
        if (IsConstantSplat(b, 1.0f))
            return a;
        break;
    case CKJIT_OP_MIN:
    case CKJIT_OP_MAX:
        if (a == b)
            return a;
        break;
    default:
        break;
    }
    return Emit(op, type, {a, b});
}

CKJitValue CKJitBuilder::FloatUnary(CKJitOp op, CKJitValue x) {
    if (!Valid(x) || !CKJitIsFloat(TypeOf(x)))
        return Fail();
    const CKJitType type = TypeOf(x);
    const CKJitValue folded = Fold(op, type, x);
    if (folded.IsValid())
        return folded;
    const CKJitNode node = m_Nodes[x.Id];
    switch (op) {
    case CKJIT_OP_NEG:
        if (node.Op == CKJIT_OP_NEG)
            return CKJitValue{node.Operands[0]};
        break;
    case CKJIT_OP_ABS:
        if (node.Op == CKJIT_OP_NEG)
            return Abs(CKJitValue{node.Operands[0]});
        if (node.Op == CKJIT_OP_ABS)
            return x;
        break;
    case CKJIT_OP_SATURATE:
        if (node.Op == op)
            return x;
        break;
    case CKJIT_OP_FLOOR:
    case CKJIT_OP_CEIL:
    case CKJIT_OP_ROUND_EVEN:
        // Rounding keeps an integral value, which every rounding and every
        // conversion from an integer produces.
        if (node.Op == CKJIT_OP_FLOOR || node.Op == CKJIT_OP_CEIL || node.Op == CKJIT_OP_ROUND_EVEN ||
            node.Op == CKJIT_OP_ITOF) {
            return x;
        }
        break;
    default:
        break;
    }
    return Emit(op, type, {x});
}

CKJitValue CKJitBuilder::Add(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_ADD, a, b); }
CKJitValue CKJitBuilder::Sub(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_SUB, a, b); }
CKJitValue CKJitBuilder::Mul(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_MUL, a, b); }
CKJitValue CKJitBuilder::Mad(CKJitValue a, CKJitValue b, CKJitValue c) {
    if (!Operands(CKJIT_TYPE_FLOAT, a, b) || !Operands(CKJIT_TYPE_FLOAT, a, c) || !Unify(a, b))
        return Fail();
    return Emit(CKJIT_OP_MAD, TypeOf(a), {a, b, c});
}
CKJitValue CKJitBuilder::Div(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_DIV, a, b); }
CKJitValue CKJitBuilder::Min(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_MIN, a, b); }
CKJitValue CKJitBuilder::Max(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_MAX, a, b); }
CKJitValue CKJitBuilder::Neg(CKJitValue x) { return FloatUnary(CKJIT_OP_NEG, x); }
CKJitValue CKJitBuilder::Abs(CKJitValue x) { return FloatUnary(CKJIT_OP_ABS, x); }
CKJitValue CKJitBuilder::Saturate(CKJitValue x) { return FloatUnary(CKJIT_OP_SATURATE, x); }
CKJitValue CKJitBuilder::Floor(CKJitValue x) { return FloatUnary(CKJIT_OP_FLOOR, x); }
CKJitValue CKJitBuilder::Ceil(CKJitValue x) { return FloatUnary(CKJIT_OP_CEIL, x); }
CKJitValue CKJitBuilder::RoundEven(CKJitValue x) { return FloatUnary(CKJIT_OP_ROUND_EVEN, x); }
CKJitValue CKJitBuilder::Exp2(CKJitValue x) { return FloatUnary(CKJIT_OP_EXP2, x); }
CKJitValue CKJitBuilder::Log2(CKJitValue x) { return FloatUnary(CKJIT_OP_LOG2, x); }
CKJitValue CKJitBuilder::Sqrt(CKJitValue x) { return FloatUnary(CKJIT_OP_SQRT, x); }
CKJitValue CKJitBuilder::Ddx(CKJitValue x) { return FloatUnary(CKJIT_OP_DDX, x); }
CKJitValue CKJitBuilder::Ddy(CKJitValue x) { return FloatUnary(CKJIT_OP_DDY, x); }

CKJitValue CKJitBuilder::Dot(CKJitValue a, CKJitValue b) {
    if (!Valid(a) || !Valid(b) || TypeOf(a) != TypeOf(b) || !CKJitIsFloat(TypeOf(a)) ||
        CKJitComponentCount(TypeOf(a)) < 2) {
        return Fail();
    }
    return Emit(CKJIT_OP_DOT, CKJIT_TYPE_FLOAT, {a, b});
}

CKJitValue CKJitBuilder::Lerp(CKJitValue x, CKJitValue y, CKJitValue s) {
    return Add(x, Mul(s, Sub(y, x)));
}

CKJitValue CKJitBuilder::Length(CKJitValue v) {
    if (Valid(v) && TypeOf(v) == CKJIT_TYPE_FLOAT)
        return Abs(v);
    return Sqrt(Dot(v, v));
}

CKJitValue CKJitBuilder::Exp(CKJitValue x) {
    return Exp2(Mul(x, Float(1.4426950408889634f)));
}

CKJitValue CKJitBuilder::Compare(CKJitOp op, CKJitType kind, CKJitValue a, CKJitValue b) {
    if (!Operands(kind, a, b))
        return Fail();
    const CKJitType type = CKJitBoolType(CKJitComponentCount(TypeOf(a)));
    const CKJitValue folded = Fold(op, type, a, b);
    if (folded.IsValid())
        return folded;
    // A value against itself: x < x is false even for NaN, and integers
    // have no NaN.
    if (a == b) {
        switch (op) {
        case CKJIT_OP_LT:
        case CKJIT_OP_ILT:
        case CKJIT_OP_INE:
            return ConstantSplat(type, 0u);
        case CKJIT_OP_ILE:
        case CKJIT_OP_IEQ:
            return ConstantSplat(type, 1u);
        default:
            break;
        }
    }
    return Emit(op, type, {a, b});
}

CKJitValue CKJitBuilder::Less(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LT, CKJIT_TYPE_FLOAT, a, b); }
CKJitValue CKJitBuilder::LessEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LE, CKJIT_TYPE_FLOAT, a, b); }
CKJitValue CKJitBuilder::Greater(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LT, CKJIT_TYPE_FLOAT, b, a); }
CKJitValue CKJitBuilder::GreaterEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LE, CKJIT_TYPE_FLOAT, b, a); }
CKJitValue CKJitBuilder::Equal(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_EQ, CKJIT_TYPE_FLOAT, a, b); }
CKJitValue CKJitBuilder::NotEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_NE, CKJIT_TYPE_FLOAT, a, b); }
CKJitValue CKJitBuilder::IntLess(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_ILT, CKJIT_TYPE_INT, a, b); }
CKJitValue CKJitBuilder::IntLessEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_ILE, CKJIT_TYPE_INT, a, b); }
CKJitValue CKJitBuilder::IntGreater(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_ILT, CKJIT_TYPE_INT, b, a); }
CKJitValue CKJitBuilder::IntGreaterEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_ILE, CKJIT_TYPE_INT, b, a); }
CKJitValue CKJitBuilder::IntEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_IEQ, CKJIT_TYPE_INT, a, b); }
CKJitValue CKJitBuilder::IntNotEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_INE, CKJIT_TYPE_INT, a, b); }

CKJitValue CKJitBuilder::Convert(CKJitOp op, CKJitType from, CKJitType to, CKJitValue x) {
    if (!Valid(x) || CKJitScalarOf(TypeOf(x)) != from)
        return Fail();
    const CKJitType type = CKJitMakeType(to, CKJitComponentCount(TypeOf(x)));
    const CKJitValue folded = Fold(op, type, x);
    if (folded.IsValid())
        return folded;
    return Emit(op, type, {x});
}

CKJitValue CKJitBuilder::FloatToInt(CKJitValue x) { return Convert(CKJIT_OP_FTOI, CKJIT_TYPE_FLOAT, CKJIT_TYPE_INT, x); }
CKJitValue CKJitBuilder::IntToFloat(CKJitValue x) { return Convert(CKJIT_OP_ITOF, CKJIT_TYPE_INT, CKJIT_TYPE_FLOAT, x); }

CKJitValue CKJitBuilder::IntBinary(CKJitOp op, CKJitValue a, CKJitValue b) {
    if (!Operands(CKJIT_TYPE_INT, a, b))
        return Fail();
    const CKJitType type = TypeOf(a);
    const CKJitValue folded = Fold(op, type, a, b);
    if (folded.IsValid())
        return folded;
    switch (op) {
    case CKJIT_OP_IADD:
        if (IsConstantInt(b, 0))
            return a;
        if (IsConstantInt(a, 0))
            return b;
        break;
    case CKJIT_OP_ISUB:
        if (IsConstantInt(b, 0))
            return a;
        if (a == b)
            return ConstantSplat(type, 0u);
        break;
    case CKJIT_OP_IMUL:
        if (IsConstantInt(b, 1) || IsConstantInt(a, 0))
            return a;
        if (IsConstantInt(a, 1) || IsConstantInt(b, 0))
            return b;
        break;
    case CKJIT_OP_IMIN:
    case CKJIT_OP_IMAX:
        if (a == b)
            return a;
        break;
    case CKJIT_OP_IMOD:
        // The floored remainder of a power of two keeps the low bits, of
        // negative dividends too.
        if (IsConstant(b)) {
            uint32_t masks[4];
            bool powers = true;
            for (uint32_t i = 0; i < CKJitComponentCount(type) && powers; ++i) {
                const int32_t divisor = (int32_t)m_Nodes[b.Id].Imm[i];
                powers = divisor > 0 && (divisor & (divisor - 1)) == 0;
                masks[i] = (uint32_t)divisor - 1u;
            }
            if (powers)
                return IntAnd(a, Constant(type, masks));
        }
        break;
    case CKJIT_OP_IAND:
        if (a == b || IsConstantInt(b, -1) || IsConstantInt(a, 0))
            return a;
        if (IsConstantInt(a, -1) || IsConstantInt(b, 0))
            return b;
        break;
    case CKJIT_OP_ISHR:
        if (IsConstant(b)) {
            bool none = true;
            for (uint32_t i = 0; i < CKJitComponentCount(type); ++i)
                none = none && (m_Nodes[b.Id].Imm[i] & 31u) == 0;
            if (none)
                return a;
        }
        break;
    default:
        break;
    }
    return Emit(op, type, {a, b});
}

CKJitValue CKJitBuilder::IntAdd(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IADD, a, b); }
CKJitValue CKJitBuilder::IntSub(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_ISUB, a, b); }
CKJitValue CKJitBuilder::IntMul(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IMUL, a, b); }
CKJitValue CKJitBuilder::IntMin(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IMIN, a, b); }
CKJitValue CKJitBuilder::IntMax(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IMAX, a, b); }
CKJitValue CKJitBuilder::IntMod(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IMOD, a, b); }
CKJitValue CKJitBuilder::IntAnd(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IAND, a, b); }
CKJitValue CKJitBuilder::IntShiftRight(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_ISHR, a, b); }

CKJitValue CKJitBuilder::BoolBinary(CKJitOp op, CKJitValue a, CKJitValue b) {
    if (!Operands(CKJIT_TYPE_BOOL, a, b))
        return Fail();
    const CKJitType type = TypeOf(a);
    const CKJitValue folded = Fold(op, type, a, b);
    if (folded.IsValid())
        return folded;
    // AND absorbs false and ignores true; OR the other way around.
    const bool absorbing = op == CKJIT_OP_OR;
    if (IsConstantBool(a, absorbing) || IsConstantBool(b, !absorbing))
        return a;
    if (IsConstantBool(b, absorbing) || IsConstantBool(a, !absorbing) || a == b)
        return b;
    return Emit(op, type, {a, b});
}

CKJitValue CKJitBuilder::And(CKJitValue a, CKJitValue b) { return BoolBinary(CKJIT_OP_AND, a, b); }
CKJitValue CKJitBuilder::Or(CKJitValue a, CKJitValue b) { return BoolBinary(CKJIT_OP_OR, a, b); }

CKJitValue CKJitBuilder::Not(CKJitValue x) {
    if (!Valid(x) || !CKJitIsBool(TypeOf(x)))
        return Fail();
    const CKJitValue folded = Fold(CKJIT_OP_NOT, TypeOf(x), x);
    if (folded.IsValid())
        return folded;
    if (IsOp(x, CKJIT_OP_NOT))
        return CKJitValue{m_Nodes[x.Id].Operands[0]};
    return Emit(CKJIT_OP_NOT, TypeOf(x), {x});
}

// ANY is decided by a true component and ALL by a false one; the other
// constant components and repeated ones drop out.
CKJitValue CKJitBuilder::Reduce(CKJitOp op, CKJitValue x) {
    if (!Valid(x) || !CKJitIsBool(TypeOf(x)))
        return Fail();
    const bool decisive = op == CKJIT_OP_ANY;
    ComponentRef refs[4];
    uint32_t count = 0;
    for (uint32_t c = 0; c < CKJitComponentCount(TypeOf(x)); ++c) {
        const ComponentRef ref = Source(x, c);
        const CKJitNode &node = m_Nodes[ref.Value];
        if (node.Op == CKJIT_OP_CONSTANT) {
            if ((node.Imm[ref.Component] != 0) == decisive)
                return Bool(decisive);
            continue;
        }
        bool repeated = false;
        for (uint32_t i = 0; i < count && !repeated; ++i)
            repeated = refs[i].Value == ref.Value && refs[i].Component == ref.Component;
        if (!repeated)
            refs[count++] = ref;
    }
    if (count == 0)
        return Bool(!decisive);
    const CKJitValue remaining = ConstructComponents(refs, count);
    if (!remaining.IsValid() || count == 1)
        return remaining;
    return Emit(op, CKJIT_TYPE_BOOL, {remaining});
}

CKJitValue CKJitBuilder::Any(CKJitValue x) { return Reduce(CKJIT_OP_ANY, x); }
CKJitValue CKJitBuilder::All(CKJitValue x) { return Reduce(CKJIT_OP_ALL, x); }

CKJitValue CKJitBuilder::Select(CKJitValue condition, CKJitValue whenTrue, CKJitValue whenFalse) {
    if (!Valid(condition) || !CKJitIsBool(TypeOf(condition)) || !Valid(whenTrue) || !Valid(whenFalse) ||
        CKJitScalarOf(TypeOf(whenTrue)) != CKJitScalarOf(TypeOf(whenFalse)) || !Unify(whenTrue, whenFalse)) {
        return Fail();
    }
    const uint32_t width = CKJitComponentCount(TypeOf(condition));
    if (width > 1) {
        if (CKJitComponentCount(TypeOf(whenTrue)) == 1) {
            whenTrue = Splat(whenTrue, width);
            whenFalse = Splat(whenFalse, width);
        }
        if (CKJitComponentCount(TypeOf(whenTrue)) != width)
            return Fail();
        // A constant condition takes every component from its side.
        if (IsConstant(condition)) {
            ComponentRef refs[4];
            for (uint32_t c = 0; c < width; ++c)
                refs[c] = Source(m_Nodes[condition.Id].Imm[c] != 0 ? whenTrue : whenFalse, c);
            return ConstructComponents(refs, width);
        }
        // A splatted condition picks whole arms.
        const ComponentRef first = Source(condition, 0);
        bool splat = true;
        for (uint32_t c = 1; c < width && splat; ++c) {
            const ComponentRef ref = Source(condition, c);
            splat = ref.Value == first.Value && ref.Component == first.Component;
        }
        if (splat)
            condition = Component(CKJitValue{first.Value}, first.Component);
    }
    if (IsConstant(condition))
        return m_Nodes[condition.Id].Imm[0] != 0 ? whenTrue : whenFalse;
    if (whenTrue == whenFalse)
        return whenTrue;
    if (IsOp(condition, CKJIT_OP_NOT))
        return Select(CKJitValue{m_Nodes[condition.Id].Operands[0]}, whenFalse, whenTrue);
    // An arm selected on the same condition resolves to its matching side.
    if (IsOp(whenTrue, CKJIT_OP_SELECT) && m_Nodes[whenTrue.Id].Operands[0] == condition.Id)
        return Select(condition, CKJitValue{m_Nodes[whenTrue.Id].Operands[1]}, whenFalse);
    if (IsOp(whenFalse, CKJIT_OP_SELECT) && m_Nodes[whenFalse.Id].Operands[0] == condition.Id)
        return Select(condition, whenTrue, CKJitValue{m_Nodes[whenFalse.Id].Operands[2]});
    if (CKJitIsBool(TypeOf(whenTrue))) {
        if (IsConstantBool(whenFalse, false))
            return And(condition, whenTrue);
        if (IsConstantBool(whenTrue, true))
            return Or(condition, whenFalse);
        if (IsConstantBool(whenTrue, false))
            return And(Not(condition), whenFalse);
        if (IsConstantBool(whenFalse, true))
            return Or(Not(condition), whenTrue);
    }
    return Emit(CKJIT_OP_SELECT, TypeOf(whenTrue), {condition, whenTrue, whenFalse});
}

void CKJitBuilder::If(CKJitValue condition) {
    Region region;
    region.Condition = condition;
    region.Marker = CKJitValue::InvalidId;
    region.Scope = CurrentScope();
    region.Parent = region.Scope;
    region.Yields = m_Yields.Size();
    region.IsLoop = false;
    region.InElse = false;
    region.Taken = true;
    region.Divergent = Divergent();
    if (!HasType(condition, CKJIT_TYPE_BOOL)) {
        Fail();
    } else if (IsConstant(condition)) {
        region.Taken = m_Nodes[condition.Id].Imm[0] != 0;
    } else {
        region.Marker = PushMarker(CKJIT_OP_IF, condition.Id, region.Parent);
        region.Scope = OpenScope();
        region.Divergent = region.Divergent || m_Varying[condition.Id] != 0;
    }
    m_Regions.PushBack(region);
}

void CKJitBuilder::Else(std::initializer_list<CKJitValue> thenResults) {
    if (m_Regions.Size() == 0 || m_Regions.Back().IsLoop || m_Regions.Back().InElse) {
        Fail();
        return;
    }
    Region &region = m_Regions.Back();
    for (CKJitValue value : thenResults)
        m_Yields.PushBack(Valid(value) ? value : CKJitValue());
    region.InElse = true;
    if (region.Marker != CKJitValue::InvalidId) {
        CloseArm(region);
        region.Marker = PushMarker(CKJIT_OP_ELSE, region.Marker, region.Parent);
        region.Scope = OpenScope();
    }
}

void CKJitBuilder::EndIf(std::initializer_list<CKJitValue> elseResults, CKJitValue *results) {
    const uint32_t count = (uint32_t)elseResults.size();
    for (uint32_t i = 0; i < count; ++i)
        results[i] = CKJitValue();
    if (m_Regions.Size() == 0 || m_Regions.Back().IsLoop) {
        Fail();
        return;
    }
    const Region region = m_Regions.PopBack();
    const CKJitValue *thens = m_Yields.Begin() + region.Yields;
    const CKJitValue *elses = elseResults.begin();
    bool typed = region.InElse && (uint32_t)(m_Yields.Size() - region.Yields) == count;
    for (uint32_t i = 0; i < count && typed; ++i)
        typed = thens[i].IsValid() && Valid(elses[i]) && TypeOf(thens[i]) == TypeOf(elses[i]);
    const bool flattened = region.Marker == CKJitValue::InvalidId;
    if (!flattened)
        CloseArm(region);

    if (!typed) {
        Fail();
    } else if (flattened) {
        for (uint32_t i = 0; i < count; ++i)
            results[i] = region.Taken ? thens[i] : elses[i];
    } else {
        // The results an arm computes meet in PHIs directly after the ENDIF;
        // the others come from before the region and are selected.
        const uint32_t endif = PushMarker(CKJIT_OP_ENDIF, region.Marker, region.Parent);
        for (uint32_t i = 0; i < count; ++i) {
            if (Valid(thens[i]) && Valid(elses[i]))
                continue;
            results[i] = Intern(MakeNode(CKJIT_OP_PHI, TypeOf(thens[i]), {thens[i].Id, elses[i].Id, endif}));
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (!results[i].IsValid())
                results[i] = Select(region.Condition, thens[i], elses[i]);
        }
    }
    m_Yields.Resize(region.Yields);
}

CKJitValue CKJitBuilder::EndIf(CKJitValue elseResult) {
    CKJitValue result;
    EndIf({elseResult}, &result);
    return result;
}

CKJitValue CKJitBuilder::Loop(CKJitValue count, uint32_t bound, std::initializer_list<CKJitValue> initials,
                              CKJitValue *carried) {
    Region region;
    region.Condition = count;
    region.Marker = CKJitValue::InvalidId;
    region.Scope = CurrentScope();
    region.Parent = region.Scope;
    region.Yields = m_Yields.Size();
    region.IsLoop = true;
    region.InElse = false;
    region.Taken = false;
    region.Divergent = Divergent();
    const uint32_t size = (uint32_t)initials.size();
    const CKJitValue *initial = initials.begin();
    bool typed = HasType(count, CKJIT_TYPE_INT) && bound != 0 && bound <= 0x7fffffffu;
    for (uint32_t i = 0; i < size && typed; ++i)
        typed = Valid(initial[i]);
    // A constant count takes the bound's place when that is lower.
    int32_t trips = 2;
    if (typed && IsConstant(count)) {
        trips = (int32_t)m_Nodes[count.Id].Imm[0];
        if (trips > (int32_t)bound)
            trips = (int32_t)bound;
        count = Int(trips);
    }

    CKJitValue index;
    if (!typed) {
        Fail();
    } else if (trips <= 1) {
        region.Taken = trips == 1;
        index = Int(0);
    } else {
        region.Marker = PushMarker(CKJIT_OP_LOOP, count.Id, region.Parent, bound);
        region.Scope = OpenScope();
        region.Divergent = region.Divergent || m_Varying[count.Id] != 0;
        index = Push(MakeNode(CKJIT_OP_INDEX, CKJIT_TYPE_INT, {region.Marker}), region.Scope);
    }
    // Carried values are never shared: equal initials may take different
    // nexts.
    for (uint32_t i = 0; i < size; ++i) {
        if (!typed)
            carried[i] = CKJitValue();
        else if (region.Marker == CKJitValue::InvalidId)
            carried[i] = initial[i];
        else
            carried[i] = Push(MakeNode(CKJIT_OP_CARRY, TypeOf(initial[i]), {initial[i].Id, region.Marker}),
                              region.Scope);
        m_Yields.PushBack(carried[i]);
    }
    m_Regions.PushBack(region);
    return index;
}

void CKJitBuilder::EndLoop(std::initializer_list<CKJitValue> nexts, CKJitValue *results) {
    const uint32_t count = (uint32_t)nexts.size();
    for (uint32_t i = 0; i < count; ++i)
        results[i] = CKJitValue();
    if (m_Regions.Size() == 0 || !m_Regions.Back().IsLoop) {
        Fail();
        return;
    }
    const Region region = m_Regions.PopBack();
    const CKJitValue *carried = m_Yields.Begin() + region.Yields;
    const CKJitValue *next = nexts.begin();
    bool typed = (uint32_t)(m_Yields.Size() - region.Yields) == count;
    for (uint32_t i = 0; i < count && typed; ++i)
        typed = carried[i].IsValid() && Valid(next[i]) && TypeOf(carried[i]) == TypeOf(next[i]);
    const bool flattened = region.Marker == CKJitValue::InvalidId;
    if (!flattened)
        CloseArm(region);

    if (!typed) {
        Fail();
    } else if (flattened) {
        for (uint32_t i = 0; i < count; ++i)
            results[i] = region.Taken ? next[i] : carried[i];
    } else {
        // Each carried value's RESULT directly after the ENDLOOP, in order.
        const uint32_t endloop = PushMarker(CKJIT_OP_ENDLOOP, region.Marker, region.Parent);
        for (uint32_t i = 0; i < count; ++i)
            results[i] = Intern(MakeNode(CKJIT_OP_RESULT, TypeOf(carried[i]), {carried[i].Id, next[i].Id, endloop}));
    }
    m_Yields.Resize(region.Yields);
}

CKJitValue CKJitBuilder::EndLoop(CKJitValue next) {
    CKJitValue result;
    EndLoop({next}, &result);
    return result;
}

CKJitValue CKJitBuilder::Texture(CKJitOp op, CKJitType type, uint32_t slot, CKJitSamplerDim dim,
                                 std::initializer_list<CKJitValue> operands) {
    // One slot is one resource declaration.
    if (slot >= CKJIT_MAX_SAMPLERS || dim > CKJIT_SAMPLER_2D_COMPARE || !CKJitTextureAccepts(op, dim) ||
        (m_SamplerDims[slot] != 0xff && m_SamplerDims[slot] != dim)) {
        return Fail();
    }
    m_SamplerDims[slot] = dim;
    return Emit(op, type, operands, {slot, (uint32_t)dim});
}

CKJitValue CKJitBuilder::Sample(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate, CKJitValue lodBias) {
    if (!HasType(coordinate, CoordinateType(dim)) || !HasType(lodBias, CKJIT_TYPE_FLOAT))
        return Fail();
    return Texture(CKJIT_OP_SAMPLE, CKJIT_TYPE_FLOAT4, slot, dim, {coordinate, lodBias});
}

CKJitValue CKJitBuilder::SampleLevel(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate, CKJitValue lod) {
    if (!HasType(coordinate, CoordinateType(dim)) || !HasType(lod, CKJIT_TYPE_FLOAT))
        return Fail();
    return Texture(CKJIT_OP_SAMPLE_LEVEL, CKJIT_TYPE_FLOAT4, slot, dim, {coordinate, lod});
}

CKJitValue CKJitBuilder::SampleGrad(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate, CKJitValue dx,
                                    CKJitValue dy) {
    const CKJitType type = CoordinateType(dim);
    if (!HasType(coordinate, type) || !HasType(dx, type) || !HasType(dy, type))
        return Fail();
    return Texture(CKJIT_OP_SAMPLE_GRAD, CKJIT_TYPE_FLOAT4, slot, dim, {coordinate, dx, dy});
}

CKJitValue CKJitBuilder::CalcLod(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate) {
    if (!HasType(coordinate, CoordinateType(dim)))
        return Fail();
    return Texture(CKJIT_OP_CALC_LOD, CKJIT_TYPE_FLOAT, slot, dim, {coordinate});
}

CKJitValue CKJitBuilder::SampleCmp(uint32_t slot, CKJitValue coordinate, CKJitValue reference) {
    if (!HasType(coordinate, CKJIT_TYPE_FLOAT2) || !HasType(reference, CKJIT_TYPE_FLOAT))
        return Fail();
    return Texture(CKJIT_OP_SAMPLE_CMP, CKJIT_TYPE_FLOAT, slot, CKJIT_SAMPLER_2D_COMPARE, {coordinate, reference});
}

CKJitValue CKJitBuilder::SampleCmpLevelZero(uint32_t slot, CKJitValue coordinate, CKJitValue reference) {
    if (!HasType(coordinate, CKJIT_TYPE_FLOAT2) || !HasType(reference, CKJIT_TYPE_FLOAT))
        return Fail();
    return Texture(CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO, CKJIT_TYPE_FLOAT, slot, CKJIT_SAMPLER_2D_COMPARE,
                   {coordinate, reference});
}

CKJitValue CKJitBuilder::Load(uint32_t slot, CKJitSamplerDim dim, CKJitValue texel) {
    if (!HasType(texel, dim == CKJIT_SAMPLER_3D ? CKJIT_TYPE_INT4 : CKJIT_TYPE_INT3))
        return Fail();
    return Texture(CKJIT_OP_LOAD, CKJIT_TYPE_FLOAT4, slot, dim, {texel});
}

CKJitValue CKJitBuilder::TextureSize(uint32_t slot, CKJitSamplerDim dim, CKJitValue mip) {
    if (!HasType(mip, CKJIT_TYPE_INT))
        return Fail();
    return Texture(CKJIT_OP_SIZE, dim == CKJIT_SAMPLER_3D ? CKJIT_TYPE_INT3 : CKJIT_TYPE_INT2, slot, dim, {mip});
}

CKJitValue CKJitBuilder::TextureLevels(uint32_t slot, CKJitSamplerDim dim) {
    return Texture(CKJIT_OP_LEVELS, CKJIT_TYPE_INT, slot, dim, {});
}

bool CKJitBuilder::Finish(CKJitValue color, CKJitValue discard, CKJitFragmentShader &out) const {
    if (m_Failed || m_Regions.Size() != 0 || !Valid(color) || TypeOf(color) != CKJIT_TYPE_FLOAT4)
        return false;
    if (discard.IsValid() && (!Valid(discard) || TypeOf(discard) != CKJIT_TYPE_BOOL))
        return false;
    if (IsConstantBool(discard, false))
        discard = CKJitValue();

    const CKJitValue roots[] = {color, discard};
    XArray<uint32_t> remap;
    if (!FinishNodes(roots, discard.IsValid() ? 2u : 1u, out, remap))
        return false;
    out.Color = CKJitValue{remap[color.Id]};
    out.Discard = discard.IsValid() ? CKJitValue{remap[discard.Id]} : CKJitValue();
    return CKJitVerify(out);
}

bool CKJitBuilder::FinishVertex(CKJitValue position, const CKJitVertexOutput *outputs, uint32_t outputCount,
                                CKJitVertexShader &out, const CKJitValue *clipDistances, uint32_t clipCount) const {
    if (!Valid(position) || TypeOf(position) != CKJIT_TYPE_FLOAT4 || (outputCount && !outputs) ||
        clipCount > 8 || (clipCount && !clipDistances))
        return false;
    XArray<CKJitValue> roots;
    roots.PushBack(position);
    for (uint32_t i = 0; i < outputCount; ++i) {
        if (!Valid(outputs[i].Value) || !CKJitIsFloat(TypeOf(outputs[i].Value)))
            return false;
        roots.PushBack(outputs[i].Value);
    }
    for (uint32_t i = 0; i < clipCount; ++i) {
        if (!Valid(clipDistances[i]) || TypeOf(clipDistances[i]) != CKJIT_TYPE_FLOAT) return false;
        roots.PushBack(clipDistances[i]);
    }
    XArray<uint32_t> remap;
    if (!FinishNodes(roots.Begin(), (uint32_t)roots.Size(), out, remap))
        return false;
    // Copy before clearing: callers may reuse out.Outputs as the declarations.
    XArray<CKJitVertexOutput> mapped;
    for (uint32_t i = 0; i < outputCount; ++i) {
        CKJitVertexOutput output = outputs[i];
        output.Value = CKJitValue{remap[output.Value.Id]};
        mapped.PushBack(output);
    }
    XArray<CKJitValue> mappedClip;
    for (uint32_t i = 0; i < clipCount; ++i)
        mappedClip.PushBack(CKJitValue{remap[clipDistances[i].Id]});
    out.Position = CKJitValue{remap[position.Id]};
    out.Outputs = mapped;
    out.ClipDistances = mappedClip;
    return CKJitVerify(out);
}

bool CKJitBuilder::FinishNodes(const CKJitValue *roots, uint32_t rootCount, CKJitShader &out,
                               XArray<uint32_t> &remap) const {
    if (m_Failed || m_Regions.Size() != 0)
        return false;

    const int count = m_Nodes.Size();
    XArray<uint8_t> live;
    live.Resize(count);
    live.Memset(0);
    for (uint32_t i = 0; i < rootCount; ++i)
        live[roots[i].Id] = 1;
    for (int i = count; i-- > 0;) {
        if (!live[i])
            continue;
        for (uint32_t operand = 0; operand < m_Nodes[i].OperandCount; ++operand)
            live[m_Nodes[i].Operands[operand]] = 1;
        // A loop kept binds every carried value to its RESULT.
        if (m_Nodes[i].Op != CKJIT_OP_ENDLOOP)
            continue;
        for (int j = i + 1; j < count && m_Nodes[j].Op == CKJIT_OP_RESULT && m_Nodes[j].Operands[2] == (uint32_t)i;
             ++j) {
            live[j] = 1;
            live[m_Nodes[j].Operands[0]] = 1;
            live[m_Nodes[j].Operands[1]] = 1;
        }
    }

    // Leaves first, so every arm sees them; the other nodes keep their
    // relative order, so operands stay ahead of users and arms within their
    // markers.
    remap.Resize(count);
    out.Nodes.Clear();
    uint32_t samplers = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < count; ++i) {
            if (!live[i] || IsLeaf(m_Nodes[i].Op) != (pass == 0))
                continue;
            CKJitNode node = m_Nodes[i];
            for (uint32_t operand = 0; operand < node.OperandCount; ++operand)
                node.Operands[operand] = remap[node.Operands[operand]];
            remap[i] = (uint32_t)out.Nodes.Size();
            out.Nodes.PushBack(node);
            // The shader binds the slots up to the highest one it reads.
            if ((CKJitOpFlags(node.Op) & CKJIT_OPFLAG_TEXTURE) != 0 && node.Imm[0] >= samplers)
                samplers = node.Imm[0] + 1;
        }
    }
    out.Inputs = m_Inputs;
    out.SamplerCount = samplers;
    out.UniformBufferCount = m_UniformBufferCount;
    std::memcpy(out.UniformVec4Counts, m_UniformVec4Counts, sizeof(out.UniformVec4Counts));
    return true;
}
