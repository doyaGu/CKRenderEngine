#ifndef CKJITBUILDER_H
#define CKJITBUILDER_H

#include "CKJitIR.h"
#include "XSHashTable.h"

#include <initializer_list>

// Creates IR values. Every node is hash-consed, so structurally equal values
// are the same value, and constant or trivial forms are folded on creation.
// Folding evaluates correctly rounded IEEE arithmetic only: no
// transcendentals, and no NaN or denormal operands or results, so it never
// moves a result outside what the GPU computes for the unfolded form.
//
// A scalar operand is splatted against a vector operand of its kind.
// Ill-typed operands never reach a backend: the operation returns an invalid
// value, the builder reports Failed() and Finish() refuses to produce a shader.
// So do values of an arm (see If) used after it ends.
class CKJitBuilder {
public:
    explicit CKJitBuilder(uint32_t uniformVec4Count);

    bool Failed() const { return m_Failed; }
    CKJitType TypeOf(CKJitValue value) const { return m_Nodes[value.Id].Type; }
    const CKJitNode &Node(CKJitValue value) const { return m_Nodes[value.Id]; }
    uint32_t NodeCount() const { return (uint32_t)m_Nodes.Size(); }

    // Leaves.
    CKJitValue Float(float x);
    CKJitValue Float2(float x, float y);
    CKJitValue Float3(float x, float y, float z);
    CKJitValue Float4(float x, float y, float z, float w);
    CKJitValue Int(int32_t x);
    CKJitValue Bool(bool x);
    CKJitValue Input(const CKJitInput &input);
    CKJitValue Uniform(uint32_t row);

    // Component routing. Swizzle takes "xyzw" or "rgba" letters.
    CKJitValue Swizzle(CKJitValue value, const char *components);
    CKJitValue Component(CKJitValue value, uint32_t index);
    CKJitValue Splat(CKJitValue scalar, uint32_t components);
    CKJitValue Construct(std::initializer_list<CKJitValue> parts);

    // Float arithmetic.
    CKJitValue Add(CKJitValue a, CKJitValue b);
    CKJitValue Sub(CKJitValue a, CKJitValue b);
    CKJitValue Mul(CKJitValue a, CKJitValue b);
    CKJitValue Div(CKJitValue a, CKJitValue b);
    CKJitValue Min(CKJitValue a, CKJitValue b);
    CKJitValue Max(CKJitValue a, CKJitValue b);
    CKJitValue Neg(CKJitValue x);
    CKJitValue Abs(CKJitValue x);
    CKJitValue Saturate(CKJitValue x);
    CKJitValue Floor(CKJitValue x);
    CKJitValue Ceil(CKJitValue x);
    CKJitValue RoundEven(CKJitValue x);
    CKJitValue Exp2(CKJitValue x);
    CKJitValue Log2(CKJitValue x);
    CKJitValue Sqrt(CKJitValue x);
    CKJitValue Dot(CKJitValue a, CKJitValue b);
    CKJitValue Ddx(CKJitValue x);
    CKJitValue Ddy(CKJitValue x);

    // HLSL intrinsics in terms of the primitives above.
    CKJitValue Lerp(CKJitValue x, CKJitValue y, CKJitValue s); // x + s * (y - x)
    CKJitValue Length(CKJitValue v);                          // sqrt(dot(v, v))
    CKJitValue Exp(CKJitValue x);                             // exp2(x * log2(e))

    // Float comparisons, producing BOOLs of the operands' width.
    CKJitValue Less(CKJitValue a, CKJitValue b);
    CKJitValue LessEqual(CKJitValue a, CKJitValue b);
    CKJitValue Greater(CKJitValue a, CKJitValue b);
    CKJitValue GreaterEqual(CKJitValue a, CKJitValue b);
    CKJitValue Equal(CKJitValue a, CKJitValue b);
    CKJitValue NotEqual(CKJitValue a, CKJitValue b);

    // Integers, with the IR semantics: IntMod is the floored remainder of a
    // positive divisor, IntShiftRight shifts by the low five bits.
    CKJitValue FloatToInt(CKJitValue x);
    CKJitValue IntToFloat(CKJitValue x);
    CKJitValue IntAdd(CKJitValue a, CKJitValue b);
    CKJitValue IntSub(CKJitValue a, CKJitValue b);
    CKJitValue IntMul(CKJitValue a, CKJitValue b);
    CKJitValue IntMin(CKJitValue a, CKJitValue b);
    CKJitValue IntMax(CKJitValue a, CKJitValue b);
    CKJitValue IntMod(CKJitValue a, CKJitValue b);
    CKJitValue IntAnd(CKJitValue a, CKJitValue b);
    CKJitValue IntShiftRight(CKJitValue a, CKJitValue b);

    // Integer comparisons.
    CKJitValue IntLess(CKJitValue a, CKJitValue b);
    CKJitValue IntLessEqual(CKJitValue a, CKJitValue b);
    CKJitValue IntGreater(CKJitValue a, CKJitValue b);
    CKJitValue IntGreaterEqual(CKJitValue a, CKJitValue b);
    CKJitValue IntEqual(CKJitValue a, CKJitValue b);
    CKJitValue IntNotEqual(CKJitValue a, CKJitValue b);

    // Booleans. Any and All reduce a vector to a BOOL.
    CKJitValue And(CKJitValue a, CKJitValue b);
    CKJitValue Or(CKJitValue a, CKJitValue b);
    CKJitValue Not(CKJitValue x);
    CKJitValue Any(CKJitValue x);
    CKJitValue All(CKJitValue x);

    // A BOOL condition picks whole arms; a vector one picks per component
    // (scalar arms splat to its width).
    CKJitValue Select(CKJitValue condition, CKJitValue whenTrue, CKJitValue whenFalse);

    // Structured conditionals, for work only some pixels need:
    //
    //     b.If(condition);             // a scalar BOOL
    //     ... then arm ...
    //     b.Else({thenResults...});
    //     ... else arm ...
    //     b.EndIf({elseResults...}, results);
    //
    // An arm's values are seen in it and the arms it encloses only. Each
    // result pairs a then and an else value of one type, each from its arm or
    // from before the region, and is the then value where the condition holds.
    // A constant condition builds both arms around the region and keeps the
    // taken arm's results. Derivatives and samples taking their LOD from them
    // need a condition every pixel of the quad shares.
    void If(CKJitValue condition);
    void Else(std::initializer_list<CKJitValue> thenResults);
    void EndIf(std::initializer_list<CKJitValue> elseResults, CKJitValue *results);
    CKJitValue EndIf(CKJitValue elseResult);

    // Texture access through a sampler slot, with FLOAT2 coordinates for 2D
    // slots and FLOAT3 for cube and volume slots. A slot keeps the dimension
    // it is first used with; comparisons make it a 2D_COMPARE slot, which
    // loads and size queries also read.
    CKJitValue Sample(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate, CKJitValue lodBias);
    CKJitValue SampleLevel(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate, CKJitValue lod);
    CKJitValue SampleGrad(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate, CKJitValue dx, CKJitValue dy);
    CKJitValue CalcLod(uint32_t slot, CKJitSamplerDim dim, CKJitValue coordinate);
    CKJitValue SampleCmp(uint32_t slot, CKJitValue coordinate, CKJitValue reference);
    CKJitValue SampleCmpLevelZero(uint32_t slot, CKJitValue coordinate, CKJitValue reference);
    CKJitValue Load(uint32_t slot, CKJitSamplerDim dim, CKJitValue texel); // the mip last, as HLSL Load
    CKJitValue TextureSize(uint32_t slot, CKJitSamplerDim dim, CKJitValue mip);
    CKJitValue TextureLevels(uint32_t slot, CKJitSamplerDim dim);

    // Constant inspection: every component of a constant of the kind is x;
    // false for non-constant values.
    bool IsConstant(CKJitValue value) const;
    bool IsConstantSplat(CKJitValue value, float x) const;
    bool IsConstantInt(CKJitValue value, int32_t x) const;
    bool IsConstantBool(CKJitValue value, bool x) const;

    // Keeps the nodes reachable from the outputs. discard may be invalid
    // (never discards). Fails on an earlier error, an open region or
    // ill-typed outputs.
    bool Finish(CKJitValue color, CKJitValue discard, CKJitFragmentShader &out) const;

private:
    struct NodeHash {
        int operator()(const CKJitNode &node) const;
    };
    struct NodeEqual {
        int operator()(const CKJitNode &a, const CKJitNode &b) const;
    };
    // Where one component of a value comes from, looking through routing.
    struct ComponentRef {
        uint32_t Value;
        uint32_t Component;
    };
    // A region being built. A constant or ill-typed condition flattens it:
    // its arms are built in the scope around it.
    struct Region {
        CKJitValue Condition;
        uint32_t Marker; // the IF, then the ELSE; InvalidId when flattened
        uint32_t Scope;  // of the arm being built
        uint32_t Parent; // the scope around the region
        int Yields;      // its then results in m_Yields
        bool InElse;
        bool Taken; // flattened: the arm the results come from
    };

    CKJitValue Emit(const CKJitNode &node);
    CKJitValue Intern(const CKJitNode &node);
    CKJitValue Push(const CKJitNode &node, uint32_t scope);
    uint32_t PushMarker(CKJitOp op, uint32_t operand, uint32_t scope);
    uint32_t OpenScope();
    void CloseArm(const Region &region);
    uint32_t CurrentScope() const { return m_Regions.Size() != 0 ? m_Regions.Back().Scope : 0u; }
    CKJitValue Emit(CKJitOp op, CKJitType type, std::initializer_list<CKJitValue> operands,
                    std::initializer_list<uint32_t> imm = {});
    CKJitValue Constant(CKJitType type, const uint32_t *bits);
    CKJitValue ConstantSplat(CKJitType type, uint32_t bits);
    CKJitValue Fold(CKJitOp op, CKJitType type, CKJitValue a, CKJitValue b = CKJitValue());
    CKJitValue SwizzleComponents(CKJitValue value, const uint32_t *selectors, uint32_t count);
    CKJitValue ConstructComponents(const ComponentRef *refs, uint32_t count);
    CKJitValue FloatBinary(CKJitOp op, CKJitValue a, CKJitValue b);
    CKJitValue FloatUnary(CKJitOp op, CKJitValue x);
    CKJitValue Compare(CKJitOp op, CKJitType kind, CKJitValue a, CKJitValue b);
    CKJitValue Convert(CKJitOp op, CKJitType from, CKJitType to, CKJitValue x);
    CKJitValue IntBinary(CKJitOp op, CKJitValue a, CKJitValue b);
    CKJitValue BoolBinary(CKJitOp op, CKJitValue a, CKJitValue b);
    CKJitValue Reduce(CKJitOp op, CKJitValue x);
    CKJitValue Texture(CKJitOp op, CKJitType type, uint32_t slot, CKJitSamplerDim dim,
                       std::initializer_list<CKJitValue> operands);
    bool Operands(CKJitType kind, CKJitValue &a, CKJitValue &b);
    bool Unify(CKJitValue &a, CKJitValue &b);
    // A value of this builder the arm being built sees.
    bool Valid(CKJitValue value) const {
        return value.Id < (uint32_t)m_Nodes.Size() && m_OpenScopes[m_Scopes[value.Id]] != 0;
    }
    bool HasType(CKJitValue value, CKJitType type) const { return Valid(value) && TypeOf(value) == type; }
    bool IsOp(CKJitValue value, CKJitOp op) const { return m_Nodes[value.Id].Op == op; }
    ComponentRef Source(CKJitValue value, uint32_t component) const;
    CKJitValue Fail();

    XArray<CKJitNode> m_Nodes;
    XArray<uint32_t> m_Scopes;    // by node: the arm it was built in, the root (0) for leaves
    XArray<uint8_t> m_OpenScopes; // by scope: whether its values are seen
    XArray<Region> m_Regions;     // being built, innermost last
    XArray<CKJitValue> m_Yields;  // their then results
    XArray<CKJitInput> m_Inputs;
    XSHashTable<uint32_t, CKJitNode, NodeHash, NodeEqual> m_Lookup; // of the values seen
    uint32_t m_UniformVec4Count;
    uint8_t m_SamplerDims[CKJIT_MAX_SAMPLERS]; // 0xff until a slot is used
    bool m_Failed;
};

#endif // CKJITBUILDER_H
