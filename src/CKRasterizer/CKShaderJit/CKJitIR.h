#ifndef CKJITIR_H
#define CKJITIR_H

#include "XArray.h"
#include "XString.h"

#include <cstdint>

// Typed SSA dataflow IR of the runtime shader compiler. A program is a DAG of
// pure nodes without control flow: runtime conditions are SELECTs and the only
// side effects are the fragment outputs. CKJitBuilder creates the nodes
// (hash-consed and folded); the SPIR-V and DXBC backends translate a finished
// CKJitFragmentShader.

enum CKJitType : uint8_t {
    CKJIT_TYPE_BOOL,
    CKJIT_TYPE_INT,
    CKJIT_TYPE_FLOAT,
    CKJIT_TYPE_FLOAT2,
    CKJIT_TYPE_FLOAT3,
    CKJIT_TYPE_FLOAT4,
};

inline bool CKJitIsFloat(CKJitType type) { return type >= CKJIT_TYPE_FLOAT; }
inline uint32_t CKJitComponentCount(CKJitType type) {
    return CKJitIsFloat(type) ? (uint32_t)(type - CKJIT_TYPE_FLOAT) + 1u : 1u;
}
inline CKJitType CKJitFloatType(uint32_t components) {
    return (CKJitType)(CKJIT_TYPE_FLOAT + components - 1u);
}

enum CKJitOpFlag {
    CKJIT_OPFLAG_COMMUTATIVE = 0x1,
};

enum CKJitOp : uint8_t {
#define CKJIT_OP(NAME, flags) CKJIT_OP_##NAME,
#include "CKJitOps.def"
#undef CKJIT_OP
    CKJIT_OP_COUNT
};

const char *CKJitOpName(CKJitOp op);
uint32_t CKJitOpFlags(CKJitOp op);

enum CKJitSamplerDim : uint8_t {
    CKJIT_SAMPLER_2D,
    CKJIT_SAMPLER_CUBE,
    CKJIT_SAMPLER_3D,
};

static const uint32_t CKJIT_MAX_SAMPLERS = 16;

enum CKJitInputKind : uint8_t {
    CKJIT_INPUT_SMOOTH,     // perspective-correct varying
    CKJIT_INPUT_FLAT,       // provoking-vertex value
    CKJIT_INPUT_FRAG_COORD, // SV_Position / FragCoord exactly as the API delivers it
};

// One fragment input. Backends keep every declared input in their signatures
// (DXBC linkage matches registers against the vertex outputs), whether or not
// the program reads it.
struct CKJitInput {
    const char *Semantic;   // DXBC signature name; static storage
    uint32_t SemanticIndex;
    uint32_t Register;      // DXBC input register
    uint32_t Location;      // SPIR-V location (ignored for FRAG_COORD)
    uint8_t Components;     // 1..4 floats
    CKJitInputKind Kind;
};

struct CKJitValue {
    static const uint32_t InvalidId = 0xffffffffu;

    uint32_t Id = InvalidId;

    bool IsValid() const { return Id != InvalidId; }
    bool operator==(const CKJitValue &other) const { return Id == other.Id; }
    bool operator!=(const CKJitValue &other) const { return Id != other.Id; }
};

// Nodes are compared and hashed bytewise: always start from a zeroed node.
struct CKJitNode {
    CKJitOp Op;
    CKJitType Type;
    uint8_t OperandCount;
    uint8_t Reserved;
    uint32_t Operands[4];
    uint32_t Imm[4];
};
static_assert(sizeof(CKJitNode) == 36, "CKJitNode must not contain padding");

// Finished program: nodes in dependency order (operands precede users), every
// node reachable from an output.
struct CKJitFragmentShader {
    XArray<CKJitInput> Inputs;
    XArray<CKJitNode> Nodes;
    uint32_t UniformVec4Count = 0; // float4 rows of the fragment uniform block
    CKJitValue Color;              // FLOAT4 written to render target 0
    CKJitValue Discard;            // optional BOOL; true discards the fragment

    const CKJitNode &Node(CKJitValue value) const { return Nodes[(int)value.Id]; }
};

// Readable listing for tests and diagnostics.
XString CKJitDump(const CKJitFragmentShader &shader);

#endif // CKJITIR_H
