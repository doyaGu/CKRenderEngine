#include "CKJitIR.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

struct OpInfo {
    const char *Name;
    uint32_t Flags;
};

const OpInfo kOps[] = {
#define CKJIT_OP(NAME, flags) {#NAME, flags},
#include "CKJitOps.def"
#undef CKJIT_OP
};
static_assert(sizeof(kOps) / sizeof(kOps[0]) == CKJIT_OP_COUNT, "op table out of sync");

const char *TypeName(CKJitType type) {
    switch (type) {
    case CKJIT_TYPE_BOOL: return "bool";
    case CKJIT_TYPE_INT: return "int";
    case CKJIT_TYPE_FLOAT: return "float";
    case CKJIT_TYPE_FLOAT2: return "float2";
    case CKJIT_TYPE_FLOAT3: return "float3";
    case CKJIT_TYPE_FLOAT4: return "float4";
    }
    return "?";
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
        if (node.Type == CKJIT_TYPE_BOOL) {
            out << (node.Imm[i] != 0 ? "true" : "false");
        } else if (node.Type == CKJIT_TYPE_INT) {
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

uint32_t CKJitOpFlags(CKJitOp op) {
    return op < CKJIT_OP_COUNT ? kOps[op].Flags : 0u;
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
