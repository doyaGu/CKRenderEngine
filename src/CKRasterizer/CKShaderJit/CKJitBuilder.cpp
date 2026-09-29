#include "CKJitBuilder.h"

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

bool FoldFloat(CKJitOp op, float a, float b, float &out) {
    if (!Foldable(a) || !Foldable(b))
        return false;
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
    case CKJIT_OP_ROUND_EVEN: out = std::nearbyint(a); break;
    case CKJIT_OP_EXP2:
        // Integral exponents give exact powers of two on every GPU.
        if (a != std::floor(a) || a < -126.0f || a > 127.0f)
            return false;
        out = std::ldexp(1.0f, (int)a);
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

int SwizzleIndex(char c) {
    switch (c) {
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    default: return -1;
    }
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
    : m_UniformVec4Count(uniformVec4Count), m_Failed(false) {
    std::memset(m_SamplerDims, 0xff, sizeof(m_SamplerDims));
}

CKJitValue CKJitBuilder::Fail() {
    m_Failed = true;
    return CKJitValue();
}

CKJitValue CKJitBuilder::Emit(const CKJitNode &source) {
    CKJitNode node = source;
    for (uint32_t i = 0; i < node.OperandCount; ++i) {
        if (node.Operands[i] >= (uint32_t)m_Nodes.Size())
            return Fail();
    }
    if ((CKJitOpFlags(node.Op) & CKJIT_OPFLAG_COMMUTATIVE) != 0 && node.Operands[1] < node.Operands[0]) {
        const uint32_t first = node.Operands[0];
        node.Operands[0] = node.Operands[1];
        node.Operands[1] = first;
    }
    if (const uint32_t *found = m_Lookup.FindPtr(node))
        return CKJitValue{*found};
    const uint32_t id = (uint32_t)m_Nodes.Size();
    m_Nodes.PushBack(node);
    m_Lookup.Insert(node, id, TRUE);
    return CKJitValue{id};
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
    if (!input.Semantic || input.Components < 1 || input.Components > 4 ||
        (input.Kind == CKJIT_INPUT_FRAG_COORD && input.Components != 4)) {
        return Fail();
    }
    uint32_t index = 0;
    for (; index < (uint32_t)m_Inputs.Size(); ++index) {
        const CKJitInput &other = m_Inputs[index];
        if (other.Register != input.Register)
            continue;
        if (std::strcmp(other.Semantic, input.Semantic) != 0 || other.SemanticIndex != input.SemanticIndex ||
            other.Location != input.Location || other.Components != input.Components || other.Kind != input.Kind) {
            return Fail();
        }
        break;
    }
    if (index == (uint32_t)m_Inputs.Size())
        m_Inputs.PushBack(input);
    return Emit(CKJIT_OP_INPUT, CKJitFloatType(input.Components), {}, {index});
}

CKJitValue CKJitBuilder::Uniform(uint32_t row) {
    if (row >= m_UniformVec4Count)
        return Fail();
    return Emit(CKJIT_OP_UNIFORM, CKJIT_TYPE_FLOAT4, {}, {row});
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
    if (!CKJitIsFloat(type))
        return Fail();

    const CKJitNode node = m_Nodes[value.Id];
    uint32_t composed[4] = {};
    switch (node.Op) {
    case CKJIT_OP_CONSTANT:
        for (uint32_t i = 0; i < count; ++i)
            composed[i] = node.Imm[selectors[i]];
        return Constant(CKJitFloatType(count), composed);
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
    swizzle.Type = CKJitFloatType(count);
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
    if (!Valid(scalar) || TypeOf(scalar) != CKJIT_TYPE_FLOAT)
        return Fail();
    static const uint32_t kZero[4] = {};
    return SwizzleComponents(scalar, kZero, components);
}

CKJitValue CKJitBuilder::Construct(std::initializer_list<CKJitValue> parts) {
    ComponentRef refs[4];
    uint32_t count = 0;
    for (CKJitValue part : parts) {
        if (!Valid(part) || !CKJitIsFloat(TypeOf(part)))
            return Fail();
        const uint32_t width = CKJitComponentCount(TypeOf(part));
        if (count + width > 4)
            return Fail();
        for (uint32_t c = 0; c < width; ++c)
            refs[count++] = Source(part, c);
    }
    if (count == 0)
        return Fail();
    return ConstructComponents(refs, count);
}

// Canonical construction: one part per run of components taken in order from
// the same value, with adjacent constant components merged.
CKJitValue CKJitBuilder::ConstructComponents(const ComponentRef *refs, uint32_t count) {
    CKJitNode node;
    std::memset(&node, 0, sizeof(node));
    node.Op = CKJIT_OP_CONSTRUCT;
    node.Type = CKJitFloatType(count);
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
        const CKJitValue part = constant ? Constant(CKJitFloatType(end - begin), selected)
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

bool CKJitBuilder::IsConstantBool(CKJitValue value, bool x) const {
    return IsConstant(value) && TypeOf(value) == CKJIT_TYPE_BOOL && (m_Nodes[value.Id].Imm[0] != 0) == x;
}

CKJitValue CKJitBuilder::FloatBinary(CKJitOp op, CKJitValue a, CKJitValue b) {
    if (!Valid(a) || !Valid(b) || !CKJitIsFloat(TypeOf(a)) || !CKJitIsFloat(TypeOf(b)) || !Unify(a, b))
        return Fail();
    const CKJitType type = TypeOf(a);
    if (IsConstant(a) && IsConstant(b)) {
        uint32_t bits[4] = {};
        bool folded = true;
        for (uint32_t i = 0; i < CKJitComponentCount(type) && folded; ++i) {
            float result;
            folded = FoldFloat(op, BitsFloat(m_Nodes[a.Id].Imm[i]), BitsFloat(m_Nodes[b.Id].Imm[i]), result);
            bits[i] = FloatBits(result);
        }
        if (folded)
            return Constant(type, bits);
    }
    switch (op) {
    case CKJIT_OP_ADD:
        if (IsConstantSplat(b, 0.0f))
            return a;
        if (IsConstantSplat(a, 0.0f))
            return b;
        break;
    case CKJIT_OP_SUB:
        if (IsConstantSplat(b, 0.0f))
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
    const CKJitNode node = m_Nodes[x.Id];
    if (node.Op == CKJIT_OP_CONSTANT) {
        uint32_t bits[4] = {};
        bool folded = true;
        for (uint32_t i = 0; i < CKJitComponentCount(type) && folded; ++i) {
            float result;
            folded = FoldFloat(op, BitsFloat(node.Imm[i]), 0.0f, result);
            bits[i] = FloatBits(result);
        }
        if (folded)
            return Constant(type, bits);
    }
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
    case CKJIT_OP_FLOOR:
    case CKJIT_OP_ROUND_EVEN:
        if (node.Op == op)
            return x;
        break;
    default:
        break;
    }
    return Emit(op, type, {x});
}

CKJitValue CKJitBuilder::Add(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_ADD, a, b); }
CKJitValue CKJitBuilder::Sub(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_SUB, a, b); }
CKJitValue CKJitBuilder::Mul(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_MUL, a, b); }
CKJitValue CKJitBuilder::Div(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_DIV, a, b); }
CKJitValue CKJitBuilder::Min(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_MIN, a, b); }
CKJitValue CKJitBuilder::Max(CKJitValue a, CKJitValue b) { return FloatBinary(CKJIT_OP_MAX, a, b); }
CKJitValue CKJitBuilder::Neg(CKJitValue x) { return FloatUnary(CKJIT_OP_NEG, x); }
CKJitValue CKJitBuilder::Abs(CKJitValue x) { return FloatUnary(CKJIT_OP_ABS, x); }
CKJitValue CKJitBuilder::Saturate(CKJitValue x) { return FloatUnary(CKJIT_OP_SATURATE, x); }
CKJitValue CKJitBuilder::Floor(CKJitValue x) { return FloatUnary(CKJIT_OP_FLOOR, x); }
CKJitValue CKJitBuilder::RoundEven(CKJitValue x) { return FloatUnary(CKJIT_OP_ROUND_EVEN, x); }
CKJitValue CKJitBuilder::Exp2(CKJitValue x) { return FloatUnary(CKJIT_OP_EXP2, x); }
CKJitValue CKJitBuilder::Sqrt(CKJitValue x) { return FloatUnary(CKJIT_OP_SQRT, x); }

CKJitValue CKJitBuilder::Dot(CKJitValue a, CKJitValue b) {
    if (!Valid(a) || !Valid(b) || TypeOf(a) != TypeOf(b) || TypeOf(a) < CKJIT_TYPE_FLOAT2)
        return Fail();
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

CKJitValue CKJitBuilder::Compare(CKJitOp op, CKJitValue a, CKJitValue b) {
    if (!Valid(a) || !Valid(b) || TypeOf(a) != CKJIT_TYPE_FLOAT || TypeOf(b) != CKJIT_TYPE_FLOAT)
        return Fail();
    bool result;
    if (IsConstant(a) && IsConstant(b) &&
        FoldCompare(op, BitsFloat(m_Nodes[a.Id].Imm[0]), BitsFloat(m_Nodes[b.Id].Imm[0]), result)) {
        return Bool(result);
    }
    if (op == CKJIT_OP_LT && a == b)
        return Bool(false);
    return Emit(op, CKJIT_TYPE_BOOL, {a, b});
}

CKJitValue CKJitBuilder::Less(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LT, a, b); }
CKJitValue CKJitBuilder::LessEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LE, a, b); }
CKJitValue CKJitBuilder::Greater(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LT, b, a); }
CKJitValue CKJitBuilder::GreaterEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_LE, b, a); }
CKJitValue CKJitBuilder::Equal(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_EQ, a, b); }
CKJitValue CKJitBuilder::NotEqual(CKJitValue a, CKJitValue b) { return Compare(CKJIT_OP_NE, a, b); }

CKJitValue CKJitBuilder::FloatToInt(CKJitValue x) {
    if (!Valid(x) || TypeOf(x) != CKJIT_TYPE_FLOAT)
        return Fail();
    if (IsConstant(x)) {
        const float value = BitsFloat(m_Nodes[x.Id].Imm[0]);
        // Out-of-range conversions saturate on GPUs and are undefined in C++.
        if (Foldable(value) && value > -2147483648.0f && value < 2147483648.0f)
            return Int((int32_t)value);
    }
    return Emit(CKJIT_OP_FTOI, CKJIT_TYPE_INT, {x});
}

CKJitValue CKJitBuilder::IntBinary(CKJitOp op, CKJitValue a, CKJitValue b) {
    if (!Valid(a) || !Valid(b) || TypeOf(a) != CKJIT_TYPE_INT || TypeOf(b) != CKJIT_TYPE_INT)
        return Fail();
    if (IsConstant(a) && IsConstant(b)) {
        const int32_t x = (int32_t)m_Nodes[a.Id].Imm[0];
        const int32_t y = (int32_t)m_Nodes[b.Id].Imm[0];
        switch (op) {
        case CKJIT_OP_IEQ: return Bool(x == y);
        case CKJIT_OP_IAND: return Int(x & y);
        // Shader shifts use the low five bits of the shift count.
        case CKJIT_OP_ISHR: return Int(x >> (y & 31));
        default: break;
        }
    }
    if (op == CKJIT_OP_IEQ && a == b)
        return Bool(true);
    if (op == CKJIT_OP_IAND && a == b)
        return a;
    return Emit(op, op == CKJIT_OP_IEQ ? CKJIT_TYPE_BOOL : CKJIT_TYPE_INT, {a, b});
}

CKJitValue CKJitBuilder::IntEqual(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IEQ, a, b); }
CKJitValue CKJitBuilder::IntAnd(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_IAND, a, b); }
CKJitValue CKJitBuilder::IntShiftRight(CKJitValue a, CKJitValue b) { return IntBinary(CKJIT_OP_ISHR, a, b); }

CKJitValue CKJitBuilder::BoolBinary(CKJitOp op, CKJitValue a, CKJitValue b) {
    if (!Valid(a) || !Valid(b) || TypeOf(a) != CKJIT_TYPE_BOOL || TypeOf(b) != CKJIT_TYPE_BOOL)
        return Fail();
    // AND absorbs false and ignores true; OR the other way around.
    const bool absorbing = op == CKJIT_OP_OR;
    if (IsConstantBool(a, absorbing) || IsConstantBool(b, !absorbing))
        return a;
    if (IsConstantBool(b, absorbing) || IsConstantBool(a, !absorbing) || a == b)
        return b;
    return Emit(op, CKJIT_TYPE_BOOL, {a, b});
}

CKJitValue CKJitBuilder::And(CKJitValue a, CKJitValue b) { return BoolBinary(CKJIT_OP_AND, a, b); }
CKJitValue CKJitBuilder::Or(CKJitValue a, CKJitValue b) { return BoolBinary(CKJIT_OP_OR, a, b); }

CKJitValue CKJitBuilder::Not(CKJitValue x) {
    if (!Valid(x) || TypeOf(x) != CKJIT_TYPE_BOOL)
        return Fail();
    if (IsConstant(x))
        return Bool(m_Nodes[x.Id].Imm[0] == 0);
    if (IsOp(x, CKJIT_OP_NOT))
        return CKJitValue{m_Nodes[x.Id].Operands[0]};
    return Emit(CKJIT_OP_NOT, CKJIT_TYPE_BOOL, {x});
}

CKJitValue CKJitBuilder::Select(CKJitValue condition, CKJitValue whenTrue, CKJitValue whenFalse) {
    if (!Valid(condition) || TypeOf(condition) != CKJIT_TYPE_BOOL || !Valid(whenTrue) || !Valid(whenFalse))
        return Fail();
    if (TypeOf(whenTrue) != TypeOf(whenFalse) &&
        (!CKJitIsFloat(TypeOf(whenTrue)) || !CKJitIsFloat(TypeOf(whenFalse)) || !Unify(whenTrue, whenFalse))) {
        return Fail();
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
    if (TypeOf(whenTrue) == CKJIT_TYPE_BOOL) {
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

CKJitValue CKJitBuilder::Sample(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate, CKJitValue lodBias) {
    const CKJitType coordinateType = dim == CKJIT_SAMPLER_2D ? CKJIT_TYPE_FLOAT2 : CKJIT_TYPE_FLOAT3;
    if (slot >= CKJIT_MAX_SAMPLERS || dim > CKJIT_SAMPLER_3D || !Valid(coordinate) || !Valid(lodBias) ||
        TypeOf(coordinate) != coordinateType || TypeOf(lodBias) != CKJIT_TYPE_FLOAT) {
        return Fail();
    }
    // One slot is one resource declaration.
    if (m_SamplerDims[slot] != 0xff && m_SamplerDims[slot] != dim)
        return Fail();
    m_SamplerDims[slot] = dim;
    return Emit(CKJIT_OP_SAMPLE, CKJIT_TYPE_FLOAT4, {coordinate, lodBias}, {slot, (uint32_t)dim});
}

bool CKJitBuilder::Finish(CKJitValue color, CKJitValue discard, CKJitFragmentShader &out) const {
    if (m_Failed || !Valid(color) || TypeOf(color) != CKJIT_TYPE_FLOAT4)
        return false;
    if (discard.IsValid() && (!Valid(discard) || TypeOf(discard) != CKJIT_TYPE_BOOL))
        return false;
    if (IsConstantBool(discard, false))
        discard = CKJitValue();

    const int count = m_Nodes.Size();
    XArray<uint8_t> live;
    live.Resize(count);
    live.Memset(0);
    live[color.Id] = 1;
    if (discard.IsValid())
        live[discard.Id] = 1;
    for (int i = count; i-- > 0;) {
        if (!live[i])
            continue;
        for (uint32_t operand = 0; operand < m_Nodes[i].OperandCount; ++operand)
            live[m_Nodes[i].Operands[operand]] = 1;
    }

    // Nodes keep their relative order, so operands stay ahead of users.
    XArray<uint32_t> remap;
    remap.Resize(count);
    out.Nodes.Clear();
    for (int i = 0; i < count; ++i) {
        if (!live[i])
            continue;
        CKJitNode node = m_Nodes[i];
        for (uint32_t operand = 0; operand < node.OperandCount; ++operand)
            node.Operands[operand] = remap[node.Operands[operand]];
        remap[i] = (uint32_t)out.Nodes.Size();
        out.Nodes.PushBack(node);
    }
    out.Inputs = m_Inputs;
    out.UniformVec4Count = m_UniformVec4Count;
    out.Color = CKJitValue{remap[color.Id]};
    out.Discard = discard.IsValid() ? CKJitValue{remap[discard.Id]} : CKJitValue();
    return true;
}
