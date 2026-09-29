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
        "bool", "bool2", "bool3", "bool4", "int", "int2", "int3", "int4", "float", "float2", "float3", "float4",
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

bool CKJitVerify(const CKJitFragmentShader &shader) {
    for (int i = 0; i < shader.Inputs.Size(); ++i) {
        const CKJitInput &input = shader.Inputs[i];
        if (input.Components < 1 || input.Components > 4 || input.Kind > CKJIT_INPUT_FRAG_COORD ||
            (input.Kind == CKJIT_INPUT_FRAG_COORD && input.Components != 4)) {
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
        switch (node.Op) {
        case CKJIT_OP_INPUT:
            if (node.Imm[0] >= (uint32_t)shader.Inputs.Size() ||
                node.Type != CKJitFloatType(shader.Inputs[(int)node.Imm[0]].Components)) {
                return false;
            }
            break;
        case CKJIT_OP_UNIFORM:
            if (node.Imm[0] >= shader.UniformVec4Count || node.Type != CKJIT_TYPE_FLOAT4)
                return false;
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
        case CKJIT_OP_SAMPLE: {
            const uint32_t slot = node.Imm[0];
            const uint32_t dim = node.Imm[1];
            if (slot >= CKJIT_MAX_SAMPLERS || dim > CKJIT_SAMPLER_3D || (dims[slot] != 0xff && dims[slot] != dim))
                return false;
            dims[slot] = (uint8_t)dim;
            break;
        }
        default:
            break;
        }
    }

    if (!shader.Color.IsValid() || shader.Color.Id >= count || shader.Node(shader.Color).Type != CKJIT_TYPE_FLOAT4)
        return false;
    return !shader.Discard.IsValid() ||
           (shader.Discard.Id < count && shader.Node(shader.Discard).Type == CKJIT_TYPE_BOOL);
}

XString CKJitDump(const CKJitFragmentShader &shader) {
    static const char kComponents[] = "xyzw";
    XString out;
    for (int i = 0; i < shader.Nodes.Size(); ++i) {
        const CKJitNode &node = shader.Nodes[i];
        Append(out, "%%%d = %s %s", i, CKJitOpName(node.Op), TypeName(node.Type));
        for (uint32_t operand = 0; operand < node.OperandCount; ++operand)
            Append(out, "%s %%%u", operand == 0 ? "" : ",", node.Operands[operand]);
        switch (node.Op) {
        case CKJIT_OP_CONSTANT:
            AppendConstant(out, node);
            break;
        case CKJIT_OP_INPUT: {
            const CKJitInput &input = shader.Inputs[(int)node.Imm[0]];
            Append(out, " %s%u", input.Semantic, input.SemanticIndex);
            break;
        }
        case CKJIT_OP_UNIFORM:
            Append(out, " c%u", node.Imm[0]);
            break;
        case CKJIT_OP_SWIZZLE:
            out << " .";
            for (uint32_t c = 0; c < CKJitComponentCount(node.Type); ++c)
                out << kComponents[node.Imm[c] & 3];
            break;
        case CKJIT_OP_SAMPLE:
            Append(out, " slot %u dim %u", node.Imm[0], node.Imm[1]);
            break;
        default:
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
