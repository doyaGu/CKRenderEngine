#ifndef CKJITIR_H
#define CKJITIR_H

#include "XArray.h"
#include "XString.h"

#include <cstdint>

// Typed SSA dataflow IR of the runtime shader compiler. A program is a list
// of pure nodes in dependency order, which structured regions divide into
// arms (IF, ELSE and ENDIF markers with PHI results) and loop bodies (LOOP and
// ENDLOOP markers with carried values and their RESULTs); the only side
// effects are the fragment outputs. CKJitBuilder creates the nodes
// (hash-consed and folded); the SPIR-V and DXBC backends translate a finished
// CKJitFragmentShader.
//
// Control flow is uniform outside regions, and in the arms and bodies of
// uniform flow whose condition or count does not vary: a value varies when
// it depends on an input or a carried value. Only uniform flow runs QUAD
// operations, as their quad neighbours must run them too.

// A scalar kind and 1..4 components: the scalar type plus components - 1.
// Integers are signed 32-bit.
enum CKJitType : uint8_t {
    CKJIT_TYPE_BOOL,
    CKJIT_TYPE_BOOL2,
    CKJIT_TYPE_BOOL3,
    CKJIT_TYPE_BOOL4,
    CKJIT_TYPE_INT,
    CKJIT_TYPE_INT2,
    CKJIT_TYPE_INT3,
    CKJIT_TYPE_INT4,
    CKJIT_TYPE_FLOAT,
    CKJIT_TYPE_FLOAT2,
    CKJIT_TYPE_FLOAT3,
    CKJIT_TYPE_FLOAT4,
    CKJIT_TYPE_VOID, // region markers, which have no value
    CKJIT_TYPE_COUNT
};

inline CKJitType CKJitScalarOf(CKJitType type) { return (CKJitType)(type & ~3u); }
inline uint32_t CKJitComponentCount(CKJitType type) { return (type & 3u) + 1u; }
inline CKJitType CKJitMakeType(CKJitType scalar, uint32_t components) {
    return (CKJitType)(CKJitScalarOf(scalar) + components - 1u);
}
inline bool CKJitIsBool(CKJitType type) { return CKJitScalarOf(type) == CKJIT_TYPE_BOOL; }
inline bool CKJitIsInt(CKJitType type) { return CKJitScalarOf(type) == CKJIT_TYPE_INT; }
inline bool CKJitIsFloat(CKJitType type) { return CKJitScalarOf(type) == CKJIT_TYPE_FLOAT; }
inline CKJitType CKJitBoolType(uint32_t components) { return CKJitMakeType(CKJIT_TYPE_BOOL, components); }
inline CKJitType CKJitIntType(uint32_t components) { return CKJitMakeType(CKJIT_TYPE_INT, components); }
inline CKJitType CKJitFloatType(uint32_t components) { return CKJitMakeType(CKJIT_TYPE_FLOAT, components); }

enum CKJitOpFlag {
    CKJIT_OPFLAG_COMMUTATIVE = 0x1,
    CKJIT_OPFLAG_VARIADIC = 0x2,
    CKJIT_OPFLAG_TEXTURE = 0x4, // reads sampler slot Imm[0] of dimension Imm[1]
    CKJIT_OPFLAG_MARKER = 0x8,  // bounds a region's arms or a loop's body: VOID, never shared
    CKJIT_OPFLAG_QUAD = 0x10,   // reads the quad neighbours: in uniform control flow only
};

enum CKJitOp : uint8_t {
#define CKJIT_OP(NAME, operands, flags) CKJIT_OP_##NAME,
#include "CKJitOps.def"
#undef CKJIT_OP
    CKJIT_OP_COUNT
};

const char *CKJitOpName(CKJitOp op);
uint32_t CKJitOpOperandCount(CKJitOp op); // the maximum for VARIADIC operations
uint32_t CKJitOpFlags(CKJitOp op);

enum CKJitSamplerDim : uint8_t {
    CKJIT_SAMPLER_2D,
    CKJIT_SAMPLER_CUBE,
    CKJIT_SAMPLER_3D,
    CKJIT_SAMPLER_2D_COMPARE, // a 2D depth texture read through a comparison sampler
};

// Whether a texture operation reads slots of the dimension: comparisons read
// only 2D_COMPARE slots, which the other samples and LOD queries do not, loads
// read all but cubes and size queries read any.
bool CKJitTextureAccepts(CKJitOp op, CKJitSamplerDim dim);

static const uint32_t CKJIT_MAX_SAMPLERS = 16;
static const uint32_t CKJIT_MAX_UNIFORM_BUFFERS = 4;

enum CKJitInputKind : uint8_t {
    CKJIT_INPUT_SMOOTH,     // perspective-correct varying
    CKJIT_INPUT_FLAT,       // provoking-vertex value
    CKJIT_INPUT_FRAG_COORD, // SV_Position / FragCoord exactly as the API delivers it
};

// One fragment input: the fragment position, or the varying the vertex shader
// writes at Location. Each backend derives its linkage from the location and
// keeps every declared input in its interface, whether or not the program
// reads it.
struct CKJitInput {
    uint32_t Location;  // varying location (ignored for FRAG_COORD)
    uint8_t Components; // 1..4 floats
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
// node reachable from an output, the leaves ahead of every region.
struct CKJitFragmentShader {
    XArray<CKJitInput> Inputs;
    XArray<CKJitNode> Nodes;
    uint32_t UniformBufferCount = 0;                            // fragment uniform blocks
    uint32_t UniformVec4Counts[CKJIT_MAX_UNIFORM_BUFFERS] = {}; // float4 rows of each
    uint32_t SamplerCount = 0;                                  // sampler slots bound, above every slot read
    CKJitValue Color;              // FLOAT4 written to render target 0
    CKJitValue Discard;            // optional scalar BOOL; true discards the fragment

    const CKJitNode &Node(CKJitValue value) const { return Nodes[(int)value.Id]; }
};

// Where a backend places the fragment resources. Uniform buffer b is at
// binding (register) UniformBinding + b of UniformSpace and sampler slot s is
// one combined texture and sampler at binding (register) s of SamplerSpace.
struct CKJitResourceLayout {
    uint32_t UniformSpace;   // SPIR-V descriptor set / DXBC register space
    uint32_t UniformBinding; // SPIR-V binding / DXBC constant buffer register of buffer 0
    uint32_t SamplerSpace;
};

// Checks what backends rely on without re-checking it: every operation has
// its operand count and operand/result types, operands precede their users,
// boolean constants are 0 or 1, input, uniform, swizzle
// and sampler references are in range, a sampler slot has one dimension its
// operations accept, regions and loops nest with their PHIs, headers and
// RESULTs in place, nodes read only values their arm or body sees, QUAD
// operations run in uniform control flow and the outputs have their types.
// This also checks shaders assembled or changed without CKJitBuilder.
bool CKJitVerify(const CKJitFragmentShader &shader);

// Readable listing for tests and diagnostics.
XString CKJitDump(const CKJitFragmentShader &shader);

#endif // CKJITIR_H
