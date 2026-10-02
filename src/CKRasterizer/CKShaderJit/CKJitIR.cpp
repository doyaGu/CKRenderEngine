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

// Structural checks of the regions and loops, in node order, and of where
// control flow is uniform. A node's scope is the IF, ELSE or LOOP marker that
// opened its arm or body, or Root.
class RegionChecker {
public:
    static const uint32_t Root = 0xffffffffu;

    explicit RegionChecker(const CKJitFragmentShader &shader) : m_Nodes(shader.Nodes), m_Results{Root, Root, 0} {
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

bool CKJitVerify(const CKJitFragmentShader &shader) {
    if (shader.UniformBufferCount > CKJIT_MAX_UNIFORM_BUFFERS || shader.SamplerCount > CKJIT_MAX_SAMPLERS)
        return false;
    for (int i = 0; i < shader.Inputs.Size(); ++i) {
        const CKJitInput &input = shader.Inputs[i];
        if (input.Components < 1 || input.Components > 4 || input.Kind > CKJIT_INPUT_FRAG_COORD ||
            (input.Kind == CKJIT_INPUT_FRAG_COORD && input.Components != 4)) {
            return false;
        }
    }

    uint8_t dims[CKJIT_MAX_SAMPLERS];
    std::memset(dims, 0xff, sizeof(dims));
    RegionChecker regions(shader);
    const uint32_t count = (uint32_t)shader.Nodes.Size();
    for (uint32_t i = 0; i < count; ++i) {
        const CKJitNode &node = shader.Nodes[(int)i];
        if (node.Op >= CKJIT_OP_COUNT || node.Type >= CKJIT_TYPE_COUNT)
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
        if (!regions.Check(i))
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
                node.Type != CKJitFloatType(shader.Inputs[(int)node.Imm[0]].Components)) {
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

    if (!regions.Closed() || !shader.Color.IsValid() || shader.Color.Id >= count ||
        shader.Node(shader.Color).Type != CKJIT_TYPE_FLOAT4 || !regions.AtRoot(shader.Color)) {
        return false;
    }
    return !shader.Discard.IsValid() || (shader.Discard.Id < count &&
                                         shader.Node(shader.Discard).Type == CKJIT_TYPE_BOOL &&
                                         regions.AtRoot(shader.Discard));
}

XString CKJitDump(const CKJitFragmentShader &shader) {
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
    if (shader.Color.IsValid())
        Append(out, "color %%%u\n", shader.Color.Id);
    if (shader.Discard.IsValid())
        Append(out, "discard %%%u\n", shader.Discard.Id);
    return out;
}
