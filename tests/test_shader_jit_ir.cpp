#include "CKJitBuilder.h"
#include "TestTriangleMultiset.h"

#include <cstring>
#include <limits>

namespace {

const CKJitInput kColor = {"TEXCOORD", 1, 2, 1, 4, CKJIT_INPUT_SMOOTH};
const CKJitInput kTexCoord = {"TEXCOORD", 4, 5, 4, 2, CKJIT_INPUT_SMOOTH};
const CKJitInput kFragCoord = {"SV_Position", 0, 0, 0, 4, CKJIT_INPUT_FRAG_COORD};

bool Same(CKJitValue a, CKJitValue b) {
    return a.IsValid() && a == b;
}

bool IsOp(const CKJitBuilder &b, CKJitValue value, CKJitOp op) {
    return value.IsValid() && b.Node(value).Op == op;
}

void TestHashConsing() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Input(kColor);
    const CKJitValue tint = b.Uniform(1);
    const CKJitValue product = b.Mul(color, tint);
    const uint32_t count = b.NodeCount();
    TestCheck(Same(b.Mul(color, tint), product), "equal nodes are one value");
    TestCheck(Same(b.Mul(tint, color), product), "commutative operands are canonical");
    TestCheck(b.NodeCount() == count, "reused nodes are not appended");
    TestCheck(!Same(b.Sub(color, tint), b.Sub(tint, color)), "ordered operands stay distinct");
    TestCheck(Same(b.Input(kColor), color), "an input is declared once");
    TestCheck(Same(b.Float(2.0f), b.Float(2.0f)), "constants are shared");
    TestCheck(!Same(b.Float(0.0f), b.Float(-0.0f)), "constants compare by bits");

    // Enough nodes to grow the lookup table several times.
    CKJitValue values[1000];
    for (int i = 0; i < 1000; ++i)
        values[i] = b.Float((float)i + 0.5f);
    const uint32_t grown = b.NodeCount();
    for (int i = 0; i < 1000; ++i)
        TestCheck(Same(b.Float((float)i + 0.5f), values[i]), "lookups survive table growth");
    TestCheck(b.NodeCount() == grown && !b.Failed(), "no node is duplicated after growth");
}

void TestConstantFolding() {
    CKJitBuilder b(4);
    TestCheck(b.IsConstantSplat(b.Add(b.Float(1.0f), b.Float(2.0f)), 3.0f), "add folds");
    TestCheck(Same(b.Mul(b.Float2(2.0f, 3.0f), b.Float2(4.0f, 0.5f)), b.Float2(8.0f, 1.5f)),
              "vector arithmetic folds per component");
    TestCheck(Same(b.Add(b.Float3(1.0f, 2.0f, 3.0f), b.Float(1.0f)), b.Float3(2.0f, 3.0f, 4.0f)),
              "a scalar constant splats before folding");
    TestCheck(Same(b.Min(b.Float(-1.0f), b.Float(2.0f)), b.Float(-1.0f)), "min folds");
    TestCheck(Same(b.Max(b.Float(-1.0f), b.Float(2.0f)), b.Float(2.0f)), "max folds");
    TestCheck(Same(b.Saturate(b.Float4(-1.0f, 0.25f, 1.0f, 7.0f)), b.Float4(0.0f, 0.25f, 1.0f, 1.0f)),
              "saturate folds");
    TestCheck(Same(b.Floor(b.Float(-1.5f)), b.Float(-2.0f)), "floor folds");
    TestCheck(Same(b.RoundEven(b.Float2(2.5f, 3.5f)), b.Float2(2.0f, 4.0f)), "round is to even");
    TestCheck(Same(b.Exp2(b.Float(-3.0f)), b.Float(0.125f)), "integral exp2 folds exactly");
    TestCheck(Same(b.Neg(b.Float(0.0f)), b.Float(-0.0f)), "negation flips the sign bit");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float denormal = std::numeric_limits<float>::denorm_min();
    TestCheck(IsOp(b, b.Add(b.Float(nan), b.Float(1.0f)), CKJIT_OP_ADD), "NaN operands are not folded");
    TestCheck(IsOp(b, b.Mul(b.Float(1e-20f), b.Float(1e-20f)), CKJIT_OP_MUL), "denormal results are not folded");
    TestCheck(IsOp(b, b.Add(b.Float(denormal), b.Float(1.0f)), CKJIT_OP_ADD), "denormal operands are not folded");
    TestCheck(IsOp(b, b.Sub(b.Float(0.0f), b.Float(0.0f)), CKJIT_OP_CONSTANT), "zero is not a denormal");
    TestCheck(IsOp(b, b.Exp2(b.Float(0.5f)), CKJIT_OP_EXP2), "fractional exp2 stays for the GPU");
    TestCheck(IsOp(b, b.Exp2(b.Float(-127.0f)), CKJIT_OP_EXP2), "exp2 into the denormal range stays");
    TestCheck(IsOp(b, b.Sqrt(b.Float(4.0f)), CKJIT_OP_SQRT), "sqrt is never folded");
    TestCheck(IsOp(b, b.Dot(b.Float2(1.0f, 2.0f), b.Float2(3.0f, 4.0f)), CKJIT_OP_DOT), "dot is never folded");
    TestCheck(!b.Failed(), "unfolded forms are not errors");
}

void TestAlgebraicIdentities() {
    CKJitBuilder b(4);
    const CKJitValue x = b.Input(kColor);
    const CKJitValue y = b.Uniform(0);
    TestCheck(Same(b.Add(x, b.Float(0.0f)), x) && Same(b.Add(b.Float4(0, 0, 0, 0), x), x), "x + 0");
    TestCheck(Same(b.Sub(x, b.Float(0.0f)), x), "x - 0");
    TestCheck(IsOp(b, b.Sub(b.Float(0.0f), x), CKJIT_OP_SUB), "0 - x is not x");
    TestCheck(Same(b.Mul(x, b.Float(1.0f)), x) && Same(b.Mul(b.Float(1.0f), x), x), "x * 1");
    TestCheck(Same(b.Div(x, b.Float(1.0f)), x), "x / 1");
    TestCheck(IsOp(b, b.Mul(x, b.Float(0.0f)), CKJIT_OP_MUL), "x * 0 keeps NaN and infinity behaviour");
    TestCheck(Same(b.Min(x, x), x) && Same(b.Max(y, y), y), "min and max of one value");
    TestCheck(Same(b.Neg(b.Neg(x)), x), "double negation");
    TestCheck(Same(b.Abs(b.Neg(x)), b.Abs(x)), "abs ignores negation");
    TestCheck(Same(b.Abs(b.Abs(x)), b.Abs(x)), "abs is idempotent");
    const CKJitValue saturated = b.Saturate(x);
    TestCheck(Same(b.Saturate(saturated), saturated), "saturate is idempotent");
    TestCheck(Same(b.Floor(b.Floor(x)), b.Floor(x)), "floor is idempotent");
    TestCheck(IsOp(b, b.Lerp(x, y, b.Float(0.0f)), CKJIT_OP_ADD), "lerp at zero still evaluates the difference");
    TestCheck(Same(b.Length(b.Component(x, 0)), b.Abs(b.Component(x, 0))), "scalar length is abs");
    TestCheck(b.TypeOf(b.Mul(x, b.Component(y, 2))) == CKJIT_TYPE_FLOAT4, "scalars splat against vectors");
    TestCheck(!b.Failed(), "identities are not errors");
}

void TestSwizzles() {
    CKJitBuilder b(4);
    const CKJitValue v = b.Input(kColor);
    TestCheck(Same(b.Swizzle(v, "xyzw"), v) && Same(b.Swizzle(v, "rgba"), v), "identity swizzles vanish");
    TestCheck(Same(b.Swizzle(v, "bgr"), b.Swizzle(v, "zyx")), "colour and position letters agree");
    TestCheck(Same(b.Swizzle(b.Swizzle(v, "wzyx"), "wzyx"), v), "swizzles compose");
    TestCheck(Same(b.Swizzle(b.Swizzle(v, "yx"), "y"), b.Component(v, 0)), "selections compose");
    TestCheck(Same(b.Swizzle(b.Float4(1, 2, 3, 4), "wx"), b.Float2(4, 1)), "constant swizzles fold");
    TestCheck(Same(b.Splat(b.Float(2.0f), 3), b.Float3(2, 2, 2)), "constant splats fold");
    const CKJitValue alpha = b.Component(v, 3);
    const CKJitValue splat = b.Splat(alpha, 4);
    TestCheck(IsOp(b, splat, CKJIT_OP_SWIZZLE) && b.Node(splat).Operands[0] == v.Id &&
                  b.Node(splat).Imm[0] == 3 && b.Node(splat).Imm[3] == 3,
              "a splat of a component selects from the vector");
    TestCheck(Same(b.Splat(alpha, 1), alpha), "a one-wide splat is the scalar");

    CKJitBuilder bad(4);
    const CKJitValue texCoord = bad.Input(kTexCoord);
    TestCheck(!bad.Swizzle(texCoord, "z").IsValid(), "selectors stay within the value");
    TestCheck(!bad.Swizzle(texCoord, "xyxyx").IsValid(), "at most four components");
    TestCheck(!bad.Swizzle(texCoord, "xq").IsValid(), "unknown letters fail");
    TestCheck(!bad.Splat(texCoord, 2).IsValid(), "only scalars splat");
    TestCheck(bad.Failed(), "bad routing fails the builder");
}

void TestConstructs() {
    CKJitBuilder b(4);
    const CKJitValue v = b.Input(kColor);
    const CKJitValue t = b.Input(kTexCoord);
    TestCheck(Same(b.Construct({b.Component(v, 0), b.Component(v, 1), b.Component(v, 2), b.Component(v, 3)}), v),
              "reassembling a value is the value");
    TestCheck(Same(b.Construct({b.Swizzle(v, "xy"), b.Swizzle(v, "zw")}), v), "adjacent parts merge");
    TestCheck(Same(b.Construct({b.Float(1.0f), b.Float2(2.0f, 3.0f)}), b.Float3(1, 2, 3)), "constants merge");
    TestCheck(Same(b.Construct({b.Component(v, 3), b.Component(v, 2)}), b.Swizzle(v, "wz")),
              "one source is a swizzle");

    const CKJitValue mixed = b.Construct({b.Swizzle(v, "xy"), b.Float(0.0f), b.Float(1.0f)});
    TestCheck(IsOp(b, mixed, CKJIT_OP_CONSTRUCT) && b.TypeOf(mixed) == CKJIT_TYPE_FLOAT4, "mixed sources construct");
    TestCheck(b.Node(mixed).OperandCount == 2 && b.Node(mixed).Operands[0] == b.Swizzle(v, "xy").Id &&
                  b.Node(mixed).Operands[1] == b.Float2(0.0f, 1.0f).Id,
              "construct parts are canonical runs");
    TestCheck(Same(b.Construct({t, b.Float2(0.0f, 1.0f)}), b.Construct({b.Component(t, 0), b.Component(t, 1),
                                                                          b.Float(0.0f), b.Float(1.0f)})),
              "equal constructions are one value");
    TestCheck(Same(b.Swizzle(mixed, "zw"), b.Float2(0.0f, 1.0f)), "selecting a constant part folds");
    TestCheck(Same(b.Swizzle(mixed, "yx"), b.Swizzle(v, "yx")), "selecting within a part skips the construct");
    TestCheck(IsOp(b, b.Swizzle(mixed, "xz"), CKJIT_OP_SWIZZLE), "selecting across parts swizzles the construct");

    CKJitBuilder bad(4);
    const CKJitValue color = bad.Input(kColor);
    TestCheck(!bad.Construct({color, bad.Float(1.0f)}).IsValid(), "at most four components");
    TestCheck(!bad.Construct({bad.Bool(true)}).IsValid(), "only floats construct");
    TestCheck(bad.Failed(), "bad constructions fail the builder");
}

void TestComparisonsAndIntegers() {
    CKJitBuilder b(4);
    const CKJitValue x = b.Component(b.Input(kColor), 0);
    const CKJitValue y = b.Component(b.Uniform(0), 1);
    TestCheck(Same(b.Greater(x, y), b.Less(y, x)), "greater is a swapped less");
    TestCheck(Same(b.GreaterEqual(x, y), b.LessEqual(y, x)), "greater-equal is a swapped less-equal");
    TestCheck(Same(b.Equal(x, y), b.Equal(y, x)), "equality is commutative");
    TestCheck(b.IsConstantBool(b.Less(x, x), false), "x < x is false even for NaN");
    TestCheck(IsOp(b, b.LessEqual(x, x), CKJIT_OP_LE), "x <= x is false for NaN");
    TestCheck(b.IsConstantBool(b.Less(b.Float(1.0f), b.Float(2.0f)), true), "comparisons fold");
    TestCheck(IsOp(b, b.NotEqual(b.Float(std::numeric_limits<float>::quiet_NaN()), b.Float(1.0f)), CKJIT_OP_NE),
              "NaN comparisons are not folded");

    TestCheck(Same(b.FloatToInt(b.Float(3.75f)), b.Int(3)), "conversion truncates");
    TestCheck(Same(b.FloatToInt(b.Float(-3.75f)), b.Int(-3)), "conversion truncates toward zero");
    TestCheck(IsOp(b, b.FloatToInt(b.Float(3e9f)), CKJIT_OP_FTOI), "out-of-range conversions stay");
    TestCheck(Same(b.IntAnd(b.Int(0x1234), b.Int(0xff)), b.Int(0x34)), "and folds");
    TestCheck(Same(b.IntShiftRight(b.Int(-8), b.Int(33)), b.Int(-4)), "shifts are arithmetic modulo 32");
    TestCheck(b.IsConstantBool(b.IntEqual(b.Int(4), b.Int(4)), true), "integer equality folds");
    const CKJitValue lanes = b.FloatToInt(x);
    TestCheck(b.IsConstantBool(b.IntEqual(lanes, lanes), true), "a value equals itself");
    TestCheck(Same(b.IntAnd(lanes, lanes), lanes), "and with itself");
    TestCheck(b.TypeOf(b.IntEqual(lanes, b.Int(1))) == CKJIT_TYPE_BOOL, "integer equality is boolean");
    TestCheck(!b.Failed(), "comparisons are well typed");

    CKJitBuilder bad(4);
    const CKJitValue color = bad.Input(kColor);
    TestCheck(!bad.Less(color, color).IsValid(), "comparisons are scalar");
    TestCheck(!bad.IntAnd(bad.Float(1.0f), bad.Int(1)).IsValid(), "integer operations take integers");
    TestCheck(bad.Failed(), "ill-typed comparisons fail the builder");
}

void TestBooleansAndSelects() {
    CKJitBuilder b(4);
    const CKJitValue v = b.Input(kColor);
    const CKJitValue a = b.Swizzle(v, "xyzw");
    const CKJitValue other = b.Uniform(2);
    const CKJitValue c = b.Less(b.Component(v, 3), b.Float(0.5f));
    const CKJitValue d = b.Less(b.Component(other, 0), b.Float(0.25f));

    TestCheck(Same(b.And(c, b.Bool(true)), c) && b.IsConstantBool(b.And(c, b.Bool(false)), false), "and folds");
    TestCheck(Same(b.Or(b.Bool(false), c), c) && b.IsConstantBool(b.Or(b.Bool(true), c), true), "or folds");
    TestCheck(Same(b.And(c, c), c) && Same(b.Or(d, d), d), "and and or are idempotent");
    TestCheck(Same(b.And(c, d), b.And(d, c)), "boolean operations are commutative");
    TestCheck(Same(b.Not(b.Not(c)), c) && b.IsConstantBool(b.Not(b.Bool(true)), false), "not folds");

    TestCheck(Same(b.Select(b.Bool(true), a, other), a), "constant conditions pick a side");
    TestCheck(Same(b.Select(c, a, a), a), "equal arms need no select");
    const CKJitValue chosen = b.Select(c, a, other);
    TestCheck(Same(b.Select(b.Not(c), other, a), chosen), "negated conditions swap the arms");
    TestCheck(Same(b.Select(c, b.Select(c, a, b.Uniform(3)), other), chosen), "nested true arms resolve");
    TestCheck(Same(b.Select(c, a, b.Select(c, b.Uniform(3), other)), chosen), "nested false arms resolve");
    TestCheck(Same(b.Select(c, d, b.Bool(false)), b.And(c, d)), "select to false is and");
    TestCheck(Same(b.Select(c, b.Bool(true), d), b.Or(c, d)), "select from true is or");
    TestCheck(Same(b.Select(c, b.Bool(false), d), b.And(b.Not(c), d)), "select from false is and-not");
    TestCheck(Same(b.Select(c, d, b.Bool(true)), b.Or(b.Not(c), d)), "select to true is or-not");
    TestCheck(b.TypeOf(b.Select(c, a, b.Float(0.0f))) == CKJIT_TYPE_FLOAT4, "scalar arms splat");
    TestCheck(!b.Failed(), "selects are well typed");

    CKJitBuilder bad(4);
    const CKJitValue color = bad.Input(kColor);
    TestCheck(!bad.Select(bad.Component(color, 0), color, color).IsValid(), "conditions are boolean");
    TestCheck(!bad.Select(bad.Bool(true), color, bad.Int(1)).IsValid(), "arms share a type");
    TestCheck(bad.Failed(), "ill-typed selects fail the builder");
}

void TestSamplesAndInputs() {
    CKJitBuilder b(4);
    const CKJitValue coordinate = b.Input(kTexCoord);
    const CKJitValue sample = b.Sample(2, CKJIT_SAMPLER_2D, coordinate, b.Float(0.0f));
    TestCheck(IsOp(b, sample, CKJIT_OP_SAMPLE) && b.TypeOf(sample) == CKJIT_TYPE_FLOAT4, "samples are float4");
    TestCheck(b.Node(sample).Imm[0] == 2 && b.Node(sample).Imm[1] == CKJIT_SAMPLER_2D, "samples record slot and dim");
    TestCheck(Same(b.Sample(2, CKJIT_SAMPLER_2D, coordinate, b.Float(0.0f)), sample), "equal samples are shared");
    const CKJitValue cube = b.Sample(3, CKJIT_SAMPLER_CUBE, b.Swizzle(b.Input(kColor), "xyz"), b.Float(-1.0f));
    TestCheck(b.TypeOf(cube) == CKJIT_TYPE_FLOAT4 && !b.Failed(), "cube samples take a direction");

    CKJitBuilder dims(4);
    const CKJitValue uv = dims.Input(kTexCoord);
    dims.Sample(0, CKJIT_SAMPLER_2D, uv, dims.Float(0.0f));
    TestCheck(!dims.Sample(0, CKJIT_SAMPLER_3D, dims.Float3(0, 0, 0), dims.Float(0.0f)).IsValid(),
              "a slot has one dimension");
    TestCheck(dims.Failed(), "dimension conflicts fail the builder");

    CKJitBuilder coordinates(4);
    TestCheck(!coordinates.Sample(0, CKJIT_SAMPLER_3D, coordinates.Input(kTexCoord), coordinates.Float(0.0f))
                   .IsValid(),
              "volume samples take three coordinates");
    TestCheck(!coordinates.Sample(CKJIT_MAX_SAMPLERS, CKJIT_SAMPLER_2D, coordinates.Input(kTexCoord),
                                  coordinates.Float(0.0f))
                   .IsValid(),
              "slots are bounded");

    CKJitBuilder inputs(4);
    CKJitInput conflicting = kTexCoord;
    conflicting.Kind = CKJIT_INPUT_FLAT;
    inputs.Input(kTexCoord);
    TestCheck(!inputs.Input(conflicting).IsValid(), "a register has one declaration");
    CKJitInput narrowFragCoord = kFragCoord;
    narrowFragCoord.Components = 2;
    TestCheck(!inputs.Input(narrowFragCoord).IsValid(), "the fragment position is a float4");
    TestCheck(!inputs.Uniform(4).IsValid(), "uniform rows are bounded");
    TestCheck(inputs.Failed(), "bad inputs fail the builder");
}

void TestFinish() {
    CKJitBuilder b(4);
    b.Input(kFragCoord);
    const CKJitValue dead = b.Add(b.Uniform(0), b.Float(2.0f));
    const CKJitValue color = b.Mul(b.Input(kColor), b.Uniform(1));
    const CKJitValue alpha = b.Component(color, 3);
    b.Sqrt(alpha);
    TestCheck(dead.IsValid(), "dead values are still values");

    CKJitFragmentShader shader;
    TestCheck(b.Finish(color, b.Bool(false), shader), "the program finishes");
    TestCheck(!shader.Discard.IsValid(), "a constant false discard is dropped");
    TestCheck(shader.Nodes.Size() == 3, "unreachable nodes are removed");
    TestCheck(shader.Node(shader.Color).Op == CKJIT_OP_MUL, "the colour output is remapped");
    for (int i = 0; i < shader.Nodes.Size(); ++i) {
        const CKJitNode &node = shader.Nodes[i];
        for (uint32_t operand = 0; operand < node.OperandCount; ++operand)
            TestCheck(node.Operands[operand] < (uint32_t)i, "operands precede their users");
    }
    TestCheck(shader.Inputs.Size() == 2 && std::strcmp(shader.Inputs[0].Semantic, "SV_Position") == 0,
              "every declared input is kept");
    TestCheck(shader.UniformVec4Count == 4, "the uniform block size is kept");

    TestCheck(b.Finish(color, b.Less(alpha, b.Float(0.5f)), shader), "the program finishes with a discard");
    TestCheck(shader.Discard.IsValid() && shader.Node(shader.Discard).Op == CKJIT_OP_LT, "the discard is kept");
    TestCheck(shader.Nodes.Size() == 6, "a finished shader is rebuilt, not appended to");

    TestCheck(!b.Finish(alpha, CKJitValue(), shader), "the colour is a float4");
    TestCheck(!b.Finish(color, alpha, shader), "the discard is boolean");
    TestCheck(!b.Finish(CKJitValue(), CKJitValue(), shader), "the colour is required");
}

void TestFailurePropagation() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Input(kColor);
    const CKJitValue bad = b.Add(b.Input(kTexCoord), b.Swizzle(color, "xyz"));
    TestCheck(!bad.IsValid() && b.Failed(), "mismatched vectors fail");
    const CKJitValue derived = b.Mul(bad, color);
    TestCheck(!derived.IsValid(), "invalid operands propagate");
    TestCheck(!b.Select(b.Bool(true), bad, color).IsValid(), "invalid arms propagate");

    CKJitFragmentShader shader;
    TestCheck(!b.Finish(color, CKJitValue(), shader), "a failed builder never finishes");
}

void TestDump() {
    // Argument evaluation order is unspecified: create the nodes one by one.
    CKJitBuilder b(4);
    const CKJitValue input = b.Input(kColor);
    const CKJitValue tint = b.Uniform(2);
    const CKJitValue color = b.Mul(input, tint);
    const CKJitValue alpha = b.Component(color, 3);
    const CKJitValue reference = b.Float(0.5f);
    const CKJitValue discard = b.Less(alpha, reference);
    CKJitFragmentShader shader;
    TestCheck(b.Finish(color, discard, shader), "the program finishes");
    const char *expected = "%0 = INPUT float4 TEXCOORD1\n"
                           "%1 = UNIFORM float4 c2\n"
                           "%2 = MUL float4 %0, %1\n"
                           "%3 = SWIZZLE float %2 .w\n"
                           "%4 = CONSTANT float (0.5)\n"
                           "%5 = LT bool %3, %4\n"
                           "color %2\n"
                           "discard %5\n";
    const XString dump = CKJitDump(shader);
    TestCheck(std::strcmp(dump.CStr(), expected) == 0, "the listing names every node");
}

} // namespace

int main() {
    TestFramework framework;
    framework.Run("hash consing", TestHashConsing);
    framework.Run("constant folding", TestConstantFolding);
    framework.Run("algebraic identities", TestAlgebraicIdentities);
    framework.Run("swizzles", TestSwizzles);
    framework.Run("constructs", TestConstructs);
    framework.Run("comparisons and integers", TestComparisonsAndIntegers);
    framework.Run("booleans and selects", TestBooleansAndSelects);
    framework.Run("samples and inputs", TestSamplesAndInputs);
    framework.Run("finish", TestFinish);
    framework.Run("failure propagation", TestFailurePropagation);
    framework.Run("dump", TestDump);
    return framework.ExitCode();
}
