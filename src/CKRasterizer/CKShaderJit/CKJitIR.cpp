#include "CKJitIR.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

struct OpInfo {
    const char *Name;
    uint32_t Operands;
    uint32_t Flags;
};

const OpInfo kOps[] = {
#define CKJIT_OP(NAME, operands, flags) {#NAME, operands, flags},
#include "CKJitOps.def"
#undef CKJIT_OP
};
static_assert(sizeof(kOps) / sizeof(kOps[0]) == CKJIT_OP_COUNT, "op table out of sync");

const char *TypeName(CKJitType type) {
    static const char *const kNames[] = {
        "bool", "bool2", "bool3", "bool4", "int", "int2", "int3", "int4",
        "float", "float2", "float3", "float4", "void",
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == CKJIT_TYPE_COUNT, "type names out of sync");
    return type < CKJIT_TYPE_COUNT ? kNames[type] : "?";
}

void Append(XString &out, const char *format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    out << buffer;
}

void AppendConstant(XString &out, const CKJitNode &node) {
    const uint32_t count = CKJitComponentCount(node.Type);
    out << " (";
    for (uint32_t i = 0; i < count; ++i) {
        if (i != 0)
            out << ", ";
        if (CKJitIsBool(node.Type)) {
            out << (node.Imm[i] != 0 ? "true" : "false");
        } else if (CKJitIsInt(node.Type)) {
            Append(out, "%d", (int32_t)node.Imm[i]);
        } else {
            float value;
            std::memcpy(&value, &node.Imm[i], sizeof(value));
            Append(out, "%.9g", value);
        }
    }
    out << ")";
}

// Called only after operand counts, indices and types have been checked.
// Builders insert splats explicitly; backends may assume exact IR widths.
bool CheckTypes(const CKJitShader &shader, const CKJitNode &node) {
    const auto operand = [&](uint32_t i) { return shader.Nodes[(int)node.Operands[i]].Type; };
    const auto same = [&]() {
        for (uint32_t i = 0; i < node.OperandCount; ++i) {
            if (operand(i) != node.Type)
                return false;
        }
        return true;
    };
    const auto comparison = [&](CKJitType kind) {
        return CKJitScalarOf(operand(0)) == kind && operand(0) == operand(1) &&
               node.Type == CKJitBoolType(CKJitComponentCount(operand(0)));
    };
    const CKJitType coordinate = node.Imm[1] == CKJIT_SAMPLER_CUBE || node.Imm[1] == CKJIT_SAMPLER_3D
                                    ? CKJIT_TYPE_FLOAT3 : CKJIT_TYPE_FLOAT2;
    switch (node.Op) {
    case CKJIT_OP_CONSTANT:
        if (CKJitIsBool(node.Type)) {
            for (uint32_t i = 0; i < CKJitComponentCount(node.Type); ++i) {
                if (node.Imm[i] > 1)
                    return false;
            }
        }
        return node.Type != CKJIT_TYPE_VOID;
    case CKJIT_OP_INPUT:
    case CKJIT_OP_UNIFORM:
    case CKJIT_OP_SWIZZLE:
        return true; // Checked with their references below.
    case CKJIT_OP_CONSTRUCT: {
        uint32_t width = 0;
        for (uint32_t i = 0; i < node.OperandCount; ++i) {
            if (CKJitScalarOf(operand(i)) != CKJitScalarOf(node.Type))
                return false;
            width += CKJitComponentCount(operand(i));
        }
        return node.Type != CKJIT_TYPE_VOID && width == CKJitComponentCount(node.Type);
    }
    case CKJIT_OP_ADD: case CKJIT_OP_SUB: case CKJIT_OP_MUL: case CKJIT_OP_MAD: case CKJIT_OP_MIX: case CKJIT_OP_DIV:
    case CKJIT_OP_MIN: case CKJIT_OP_MAX: case CKJIT_OP_NEG: case CKJIT_OP_ABS:
    case CKJIT_OP_SATURATE: case CKJIT_OP_FLOOR: case CKJIT_OP_CEIL: case CKJIT_OP_ROUND_EVEN:
    case CKJIT_OP_EXP2: case CKJIT_OP_LOG2: case CKJIT_OP_SQRT: case CKJIT_OP_DDX: case CKJIT_OP_DDY:
        return CKJitIsFloat(node.Type) && same();
    case CKJIT_OP_DOT:
        return node.Type == CKJIT_TYPE_FLOAT && CKJitIsFloat(operand(0)) &&
               CKJitComponentCount(operand(0)) >= 2 && operand(0) == operand(1);
    case CKJIT_OP_LT: case CKJIT_OP_LE: case CKJIT_OP_EQ: case CKJIT_OP_NE:
        return comparison(CKJIT_TYPE_FLOAT);
    case CKJIT_OP_FTOI:
        return CKJitIsFloat(operand(0)) && node.Type == CKJitIntType(CKJitComponentCount(operand(0)));
    case CKJIT_OP_ITOF:
        return CKJitIsInt(operand(0)) && node.Type == CKJitFloatType(CKJitComponentCount(operand(0)));
    case CKJIT_OP_IADD: case CKJIT_OP_ISUB: case CKJIT_OP_IMUL: case CKJIT_OP_IMIN: case CKJIT_OP_IMAX:
    case CKJIT_OP_IMOD: case CKJIT_OP_IAND: case CKJIT_OP_ISHR:
        return CKJitIsInt(node.Type) && same();
    case CKJIT_OP_ILT: case CKJIT_OP_ILE: case CKJIT_OP_IEQ: case CKJIT_OP_INE:
        return comparison(CKJIT_TYPE_INT);
    case CKJIT_OP_AND: case CKJIT_OP_OR: case CKJIT_OP_NOT:
        return CKJitIsBool(node.Type) && same();
    case CKJIT_OP_ANY: case CKJIT_OP_ALL:
        return node.Type == CKJIT_TYPE_BOOL && CKJitIsBool(operand(0)) && CKJitComponentCount(operand(0)) >= 2;
    case CKJIT_OP_SELECT:
        return node.Type != CKJIT_TYPE_VOID && operand(1) == node.Type && operand(2) == node.Type &&
               (operand(0) == CKJIT_TYPE_BOOL || operand(0) == CKJitBoolType(CKJitComponentCount(node.Type)));
    case CKJIT_OP_IF:
        return node.Type == CKJIT_TYPE_VOID && operand(0) == CKJIT_TYPE_BOOL;
    case CKJIT_OP_LOOP:
        return node.Type == CKJIT_TYPE_VOID && operand(0) == CKJIT_TYPE_INT;
    case CKJIT_OP_ELSE: case CKJIT_OP_ENDIF: case CKJIT_OP_ENDLOOP:
        return node.Type == CKJIT_TYPE_VOID && operand(0) == CKJIT_TYPE_VOID;
    case CKJIT_OP_INDEX:
        return node.Type == CKJIT_TYPE_INT && operand(0) == CKJIT_TYPE_VOID;
    case CKJIT_OP_CARRY:
        return node.Type != CKJIT_TYPE_VOID && operand(0) == node.Type && operand(1) == CKJIT_TYPE_VOID;
    case CKJIT_OP_PHI: case CKJIT_OP_RESULT:
        return node.Type != CKJIT_TYPE_VOID && operand(0) == node.Type && operand(1) == node.Type &&
               operand(2) == CKJIT_TYPE_VOID;
    case CKJIT_OP_SAMPLE: case CKJIT_OP_SAMPLE_LEVEL:
        return node.Type == CKJIT_TYPE_FLOAT4 && operand(0) == coordinate && operand(1) == CKJIT_TYPE_FLOAT;
    case CKJIT_OP_SAMPLE_GRAD:
        return node.Type == CKJIT_TYPE_FLOAT4 && operand(0) == coordinate && operand(1) == coordinate && operand(2) == coordinate;
    case CKJIT_OP_CALC_LOD:
        return node.Type == CKJIT_TYPE_FLOAT && operand(0) == coordinate;
    case CKJIT_OP_SAMPLE_CMP: case CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO:
        return node.Type == CKJIT_TYPE_FLOAT && operand(0) == CKJIT_TYPE_FLOAT2 && operand(1) == CKJIT_TYPE_FLOAT;
    case CKJIT_OP_LOAD:
        return node.Type == CKJIT_TYPE_FLOAT4 && operand(0) == (node.Imm[1] == CKJIT_SAMPLER_3D ? CKJIT_TYPE_INT4 : CKJIT_TYPE_INT3);
    case CKJIT_OP_SIZE:
        return node.Type == (node.Imm[1] == CKJIT_SAMPLER_3D ? CKJIT_TYPE_INT3 : CKJIT_TYPE_INT2) && operand(0) == CKJIT_TYPE_INT;
    case CKJIT_OP_LEVELS:
        return node.Type == CKJIT_TYPE_INT;
    default:
        return false;
    }
}

// Structural checks of the regions and loops, in node order, and of where
// control flow is uniform. A node's scope is the IF, ELSE or LOOP marker that
// opened its arm or body, or Root.
class RegionChecker {
public:
    static const uint32_t Root = 0xffffffffu;

    explicit RegionChecker(const CKJitShader &shader) : m_Nodes(shader.Nodes), m_Results{Root, Root, 0} {
        m_Scopes.Resize(m_Nodes.Size());
        m_Open.Resize(m_Nodes.Size());
        m_Open.Memset(0);
        m_Varying.Resize(m_Nodes.Size());
    }

    bool Check(uint32_t i) {
        const CKJitNode &node = m_Nodes[(int)i];
        const uint32_t flags = kOps[node.Op].Flags;
        if (((flags & CKJIT_OPFLAG_MARKER) != 0) != (node.Type == CKJIT_TYPE_VOID) ||
            (m_Results.Left != 0) != (node.Op == CKJIT_OP_RESULT) ||
            ((flags & CKJIT_OPFLAG_QUAD) != 0 && Divergent())) {
            return false;
        }
        // Carried values vary: the check does not look ahead to their nexts.
        uint8_t varying = node.Op == CKJIT_OP_INPUT || node.Op == CKJIT_OP_CARRY;
        for (uint32_t operand = 0; operand < node.OperandCount; ++operand)
            varying |= m_Varying[(int)node.Operands[operand]];
        m_Varying[(int)i] = varying;
        m_Scopes[(int)i] = Current();
        switch (node.Op) {
        case CKJIT_OP_IF:
            if (!Visible(node.Operands[0]) || m_Nodes[(int)node.Operands[0]].Type != CKJIT_TYPE_BOOL)
                return false;
            Open(i);
            return true;
        case CKJIT_OP_ELSE:
            if (!Innermost(node.Operands[0], CKJIT_OP_IF) || m_Regions.Back().Else != Root)
                return false;
            m_Open[(int)node.Operands[0]] = 0;
            m_Open[(int)i] = 1;
            m_Regions.Back().Else = i;
            m_Scopes[(int)i] = m_Scopes[(int)node.Operands[0]];
            return true;
        case CKJIT_OP_ENDIF:
            if (m_Regions.Size() == 0 || m_Regions.Back().Else != node.Operands[0])
                return false;
            Close(i);
            return true;
        case CKJIT_OP_PHI: {
            // Directly after its ENDIF, each operand from its arm or seen here.
            const uint32_t endif = node.Operands[2];
            const CKJitNode &previous = m_Nodes[(int)i - 1];
            if (m_Nodes[(int)endif].Op != CKJIT_OP_ENDIF ||
                (i - 1 != endif && (previous.Op != CKJIT_OP_PHI || previous.Operands[2] != endif))) {
                return false;
            }
            const uint32_t elseMarker = m_Nodes[(int)endif].Operands[0];
            const uint32_t ifMarker = m_Nodes[(int)elseMarker].Operands[0];
            return FromArm(node.Operands[0], ifMarker) && FromArm(node.Operands[1], elseMarker);
        }
        case CKJIT_OP_LOOP:
            if (!Visible(node.Operands[0]) || m_Nodes[(int)node.Operands[0]].Type != CKJIT_TYPE_INT ||
                node.Imm[0] == 0 || node.Imm[0] > 0x7fffffffu) {
                return false;
            }
            Open(i);
            return true;
        case CKJIT_OP_INDEX:
            return node.Operands[0] == i - 1 && m_Nodes[(int)i - 1].Op == CKJIT_OP_LOOP;
        case CKJIT_OP_CARRY: {
            // In the header of the loop being checked, carrying a value from
            // before it.
            const uint32_t loop = node.Operands[1];
            const CKJitNode &previous = m_Nodes[(int)i - 1];
            const bool header = previous.Op == CKJIT_OP_INDEX || previous.Op == CKJIT_OP_CARRY;
            if (!Innermost(loop, CKJIT_OP_LOOP) ||
                (i - 1 != loop && (!header || previous.Operands[previous.OperandCount - 1] != loop)) ||
                !Visible(node.Operands[0]) || m_Scopes[(int)node.Operands[0]] == loop) {
                return false;
            }
            if (m_Regions.Back().Carries++ == 0)
                m_Regions.Back().FirstCarry = i;
            return true;
        }
        case CKJIT_OP_ENDLOOP: {
            if (!Innermost(node.Operands[0], CKJIT_OP_LOOP))
                return false;
            const Region &loop = m_Regions.Back();
            m_Results = Results{i, loop.FirstCarry, loop.Carries};
            Close(i);
            return true;
        }
        case CKJIT_OP_RESULT:
            // The carries in order, each with a next the body's end sees.
            if (node.Operands[2] != m_Results.EndLoop || node.Operands[0] != m_Results.Carry)
                return false;
            ++m_Results.Carry;
            --m_Results.Left;
            return FromArm(node.Operands[1], m_Nodes[(int)m_Results.EndLoop].Operands[0]);
        default:
            for (uint32_t operand = 0; operand < node.OperandCount; ++operand) {
                if (!Visible(node.Operands[operand]))
                    return false;
            }
            return true;
        }
    }

    bool Closed() const { return m_Regions.Size() == 0 && m_Results.Left == 0; }
    bool AtRoot(CKJitValue value) const { return m_Scopes[(int)value.Id] == Root; }

private:
    struct Region {
        uint32_t Marker;     // the IF or LOOP
        uint32_t Else;       // Root in a then arm or a loop body
        uint32_t FirstCarry; // of a loop
        uint32_t Carries;
        bool Divergent; // not every pixel of a quad may run the arm or body
    };
    // The RESULTs due after an ENDLOOP.
    struct Results {
        uint32_t EndLoop;
        uint32_t Carry; // the next one to bind
        uint32_t Left;
    };

    uint32_t Current() const {
        if (m_Regions.Size() == 0)
            return Root;
        return m_Regions.Back().Else != Root ? m_Regions.Back().Else : m_Regions.Back().Marker;
    }
    bool Divergent() const { return m_Regions.Size() != 0 && m_Regions.Back().Divergent; }
    void Open(uint32_t marker) {
        m_Regions.PushBack(Region{marker, Root, Root, 0, Divergent() || m_Varying[(int)marker] != 0});
        m_Open[(int)marker] = 1;
    }
    void Close(uint32_t end) {
        m_Open[(int)m_Nodes[(int)end].Operands[0]] = 0;
        m_Regions.PopBack();
        m_Scopes[(int)end] = Current();
    }
    // Whether the marker of the op opened the arm or body being checked.
    bool Innermost(uint32_t marker, CKJitOp op) const {
        return m_Regions.Size() != 0 && m_Regions.Back().Marker == marker && m_Nodes[(int)marker].Op == op;
    }
    bool IsMarker(uint32_t id) const { return (kOps[m_Nodes[(int)id].Op].Flags & CKJIT_OPFLAG_MARKER) != 0; }
    bool Visible(uint32_t id) const {
        const uint32_t scope = m_Scopes[(int)id];
        return !IsMarker(id) && (scope == Root || m_Open[(int)scope]);
    }
    bool FromArm(uint32_t id, uint32_t arm) const { return Visible(id) || (!IsMarker(id) && m_Scopes[(int)id] == arm); }

    const XArray<CKJitNode> &m_Nodes;
    XArray<uint32_t> m_Scopes;
    XArray<uint8_t> m_Open;    // by marker: its arm or body is being checked
    XArray<uint8_t> m_Varying; // by node
    XArray<Region> m_Regions;
    Results m_Results;
};

} // namespace

const char *CKJitOpName(CKJitOp op) {
    return op < CKJIT_OP_COUNT ? kOps[op].Name : "?";
}

uint32_t CKJitOpOperandCount(CKJitOp op) {
    return op < CKJIT_OP_COUNT ? kOps[op].Operands : 0u;
}

uint32_t CKJitOpFlags(CKJitOp op) {
    return op < CKJIT_OP_COUNT ? kOps[op].Flags : 0u;
}

bool CKJitTextureAccepts(CKJitOp op, CKJitSamplerDim dim) {
    switch (op) {
    case CKJIT_OP_SAMPLE_CMP:
    case CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO: return dim == CKJIT_SAMPLER_2D_COMPARE;
    case CKJIT_OP_LOAD: return dim != CKJIT_SAMPLER_CUBE;
    case CKJIT_OP_SIZE:
    case CKJIT_OP_LEVELS: return true;
    default: return dim != CKJIT_SAMPLER_2D_COMPARE;
    }
}

namespace {

bool VerifyNodes(const CKJitShader &shader, bool vertex, RegionChecker &regions) {
    if (shader.UniformBufferCount > CKJIT_MAX_UNIFORM_BUFFERS || shader.SamplerCount > CKJIT_MAX_SAMPLERS)
        return false;
    for (int i = 0; i < shader.Inputs.Size(); ++i) {
        const CKJitInput &input = shader.Inputs[i];
        if (input.Components < 1 || input.Components > 4 ||
            (vertex ? input.Kind != CKJIT_INPUT_ATTRIBUTE : input.Kind > CKJIT_INPUT_FRAG_COORD) ||
            (input.Kind == CKJIT_INPUT_FRAG_COORD && input.Components != 4) ||
            input.Scalar > CKJIT_INPUT_UINT || (input.Scalar == CKJIT_INPUT_UINT && !vertex)) {
            return false;
        }
        for (int j = 0; j < i; ++j) {
            const CKJitInput &other = shader.Inputs[j];
            const bool position = input.Kind == CKJIT_INPUT_FRAG_COORD;
            if ((other.Kind == CKJIT_INPUT_FRAG_COORD) == position &&
                (position || other.Location == input.Location))
                return false;
        }
    }

    uint8_t dims[CKJIT_MAX_SAMPLERS];
    std::memset(dims, 0xff, sizeof(dims));
    const uint32_t count = (uint32_t)shader.Nodes.Size();
    for (uint32_t i = 0; i < count; ++i) {
        const CKJitNode &node = shader.Nodes[(int)i];
        if (node.Op >= CKJIT_OP_COUNT || node.Type >= CKJIT_TYPE_COUNT)
            return false;
        if (vertex && (kOps[node.Op].Flags & CKJIT_OPFLAG_QUAD) != 0)
            return false;
        const uint32_t operands = kOps[node.Op].Operands;
        if ((kOps[node.Op].Flags & CKJIT_OPFLAG_VARIADIC) != 0
                ? node.OperandCount < 2 || node.OperandCount > operands
                : node.OperandCount != operands) {
            return false;
        }
        for (uint32_t operand = 0; operand < node.OperandCount; ++operand) {
            if (node.Operands[operand] >= i)
                return false;
        }
        if (!CheckTypes(shader, node) || !regions.Check(i))
            return false;
        if ((kOps[node.Op].Flags & CKJIT_OPFLAG_TEXTURE) != 0) {
            const uint32_t slot = node.Imm[0];
            const uint32_t dim = node.Imm[1];
            if (slot >= shader.SamplerCount || dim > CKJIT_SAMPLER_2D_COMPARE ||
                (dims[slot] != 0xff && dims[slot] != dim) ||
                !CKJitTextureAccepts((CKJitOp)node.Op, (CKJitSamplerDim)dim)) {
                return false;
            }
            dims[slot] = (uint8_t)dim;
        }
        switch (node.Op) {
        case CKJIT_OP_INPUT:
            if (node.Imm[0] >= (uint32_t)shader.Inputs.Size() ||
                node.Type != shader.Inputs[(int)node.Imm[0]].Type()) {
                return false;
            }
            break;
        case CKJIT_OP_UNIFORM:
            if (node.Imm[1] >= shader.UniformBufferCount ||
                node.Imm[0] >= shader.UniformVec4Counts[node.Imm[1]] || node.Type != CKJIT_TYPE_FLOAT4) {
                return false;
            }
            break;
        case CKJIT_OP_SWIZZLE: {
            const CKJitType source = shader.Nodes[(int)node.Operands[0]].Type;
            if (CKJitScalarOf(node.Type) != CKJitScalarOf(source))
                return false;
            for (uint32_t c = 0; c < CKJitComponentCount(node.Type); ++c) {
                if (node.Imm[c] >= CKJitComponentCount(source))
                    return false;
            }
            break;
        }
        default:
            break;
        }
    }

    return regions.Closed();
}

} // namespace

bool CKJitVerify(const CKJitFragmentShader &shader) {
    RegionChecker regions(shader);
    const uint32_t count = (uint32_t)shader.Nodes.Size();
    if (!VerifyNodes(shader, false, regions) || !shader.Color.IsValid() || shader.Color.Id >= count ||
        shader.Node(shader.Color).Type != CKJIT_TYPE_FLOAT4 || !regions.AtRoot(shader.Color)) {
        return false;
    }
    return !shader.Discard.IsValid() || (shader.Discard.Id < count &&
                                         shader.Node(shader.Discard).Type == CKJIT_TYPE_BOOL &&
                                         regions.AtRoot(shader.Discard));
}

bool CKJitVerify(const CKJitVertexShader &shader) {
    RegionChecker regions(shader);
    const uint32_t count = (uint32_t)shader.Nodes.Size();
    if (!VerifyNodes(shader, true, regions) || !shader.Position.IsValid() || shader.Position.Id >= count ||
        shader.Node(shader.Position).Type != CKJIT_TYPE_FLOAT4 || !regions.AtRoot(shader.Position))
        return false;
    if (shader.ClipDistances.Size() > 8) return false;
    for (const CKJitValue value : shader.ClipDistances) {
        if (!value.IsValid() || value.Id >= count || shader.Node(value).Type != CKJIT_TYPE_FLOAT ||
            !regions.AtRoot(value)) return false;
    }
    for (int i = 0; i < shader.Outputs.Size(); ++i) {
        const CKJitVertexOutput &output = shader.Outputs[i];
        if (output.Kind > CKJIT_INPUT_FLAT || !output.Value.IsValid() || output.Value.Id >= count ||
            !CKJitIsFloat(shader.Node(output.Value).Type) || !regions.AtRoot(output.Value))
            return false;
        for (int j = 0; j < i; ++j) {
            if (shader.Outputs[j].Location == output.Location)
                return false;
        }
    }
    return true;
}

static XString DumpNodes(const CKJitShader &shader) {
    static const char kComponents[] = "xyzw";
    XString out;
    uint32_t depth = 0; // of the arms around the node
    for (int i = 0; i < shader.Nodes.Size(); ++i) {
        const CKJitNode &node = shader.Nodes[i];
        // Arms and bodies are indented under their markers.
        if ((node.Op == CKJIT_OP_ENDIF || node.Op == CKJIT_OP_ENDLOOP) && depth != 0)
            --depth;
        const uint32_t indent = node.Op == CKJIT_OP_ELSE && depth != 0 ? depth - 1 : depth;
        for (uint32_t level = 0; level < indent; ++level)
            out << "  ";
        if (node.Op == CKJIT_OP_IF || node.Op == CKJIT_OP_LOOP)
            ++depth;
        Append(out, "%%%d = %s %s", i, CKJitOpName(node.Op), TypeName(node.Type));
        for (uint32_t operand = 0; operand < node.OperandCount; ++operand)
            Append(out, "%s %%%u", operand == 0 ? "" : ",", node.Operands[operand]);
        switch (node.Op) {
        case CKJIT_OP_CONSTANT:
            AppendConstant(out, node);
            break;
        case CKJIT_OP_INPUT: {
            const CKJitInput &input = shader.Inputs[(int)node.Imm[0]];
            if (input.Kind == CKJIT_INPUT_FRAG_COORD)
                Append(out, " position");
            else
                Append(out, " location%u", input.Location);
            break;
        }
        case CKJIT_OP_UNIFORM:
            if (node.Imm[1] != 0)
                Append(out, " cb%u", node.Imm[1]);
            Append(out, " c%u", node.Imm[0]);
            break;
        case CKJIT_OP_SWIZZLE:
            out << " .";
            for (uint32_t c = 0; c < CKJitComponentCount(node.Type); ++c)
                out << kComponents[node.Imm[c] & 3];
            break;
        case CKJIT_OP_LOOP:
            Append(out, " bound %u", node.Imm[0]);
            break;
        default:
            if ((kOps[node.Op].Flags & CKJIT_OPFLAG_TEXTURE) != 0)
                Append(out, " slot %u dim %u", node.Imm[0], node.Imm[1]);
            break;
        }
        out << "\n";
    }
    return out;
}

XString CKJitDump(const CKJitFragmentShader &shader) {
    XString out = DumpNodes(shader);
    if (shader.Color.IsValid())
        Append(out, "color %%%u\n", shader.Color.Id);
    if (shader.Discard.IsValid())
        Append(out, "discard %%%u\n", shader.Discard.Id);
    return out;
}

XString CKJitDump(const CKJitVertexShader &shader) {
    XString out = DumpNodes(shader);
    if (shader.Position.IsValid())
        Append(out, "position %%%u\n", shader.Position.Id);
    for (int i = 0; i < shader.ClipDistances.Size(); ++i)
        Append(out, "clipdistance %d %%%u\n", i, shader.ClipDistances[i].Id);
    for (int i = 0; i < shader.Outputs.Size(); ++i) {
        const CKJitVertexOutput &output = shader.Outputs[i];
        Append(out, "output location%u %s %%%u\n", output.Location,
               output.Kind == CKJIT_INPUT_FLAT ? "flat" : "smooth", output.Value.Id);
    }
    return out;
}
