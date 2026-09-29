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
    TestCheck(!bad.Construct({bad.Bool(true), bad.Float(1.0f)}).IsValid(), "parts share a kind");
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
    TestCheck(!bad.Less(bad.Input(kTexCoord), bad.Swizzle(color, "xyz")).IsValid(), "comparisons take one width");
    TestCheck(!bad.IntAnd(bad.Float(1.0f), bad.Int(1)).IsValid(), "integer operations take integers");
    TestCheck(bad.Failed(), "ill-typed comparisons fail the builder");
}

void TestIntegers() {
    CKJitBuilder b(4);
    TestCheck(Same(b.IntAdd(b.Int(0x7fffffff), b.Int(1)), b.Int((int32_t)0x80000000u)), "addition wraps");
    TestCheck(Same(b.IntSub(b.Int(3), b.Int(5)), b.Int(-2)), "subtraction folds");
    TestCheck(Same(b.IntMul(b.Int(0x10000), b.Int(0x10001)), b.Int(0x10000)), "multiplication keeps the low bits");
    TestCheck(Same(b.IntMin(b.Int(-1), b.Int(1)), b.Int(-1)) && Same(b.IntMax(b.Int(-1), b.Int(1)), b.Int(1)),
              "min and max are signed");
    TestCheck(Same(b.IntMod(b.Int(7), b.Int(3)), b.Int(1)) && Same(b.IntMod(b.Int(-7), b.Int(3)), b.Int(2)),
              "the remainder is floored");
    TestCheck(IsOp(b, b.IntMod(b.Int(7), b.Int(-3)), CKJIT_OP_IMOD) && IsOp(b, b.IntMod(b.Int(7), b.Int(0)), CKJIT_OP_IMOD),
              "remainders of other divisors are not folded");
    TestCheck(b.IsConstantBool(b.IntLess(b.Int(-1), b.Int(0)), true), "integer comparisons are signed");
    TestCheck(Same(b.IntToFloat(b.Int(-3)), b.Float(-3.0f)), "exact conversions fold");
    TestCheck(IsOp(b, b.IntToFloat(b.Int(16777217)), CKJIT_OP_ITOF), "inexact conversions stay");

    const CKJitValue pair = b.Construct({b.Int(1), b.Int(-2)});
    TestCheck(IsOp(b, pair, CKJIT_OP_CONSTANT) && b.TypeOf(pair) == CKJIT_TYPE_INT2, "integer constants construct");
    TestCheck(Same(b.FloatToInt(b.Float2(1.5f, -2.5f)), pair), "vector conversions fold per component");
    TestCheck(Same(b.IntAdd(pair, b.Int(1)), b.Construct({b.Int(2), b.Int(-1)})), "a scalar integer splats");
    TestCheck(Same(b.Swizzle(pair, "yx"), b.Construct({b.Int(-2), b.Int(1)})), "integer swizzles fold");

    const CKJitValue lanes = b.FloatToInt(b.Swizzle(b.Input(kColor), "xy"));
    TestCheck(b.TypeOf(lanes) == CKJIT_TYPE_INT2 && b.TypeOf(b.IntToFloat(lanes)) == CKJIT_TYPE_FLOAT2,
              "conversions keep the width");
    TestCheck(Same(b.IntAdd(lanes, b.Int(0)), lanes) && Same(b.IntSub(lanes, b.Int(0)), lanes), "x + 0 and x - 0");
    TestCheck(b.IsConstantInt(b.IntSub(lanes, lanes), 0), "x - x");
    TestCheck(Same(b.IntMul(lanes, b.Int(1)), lanes) && b.IsConstantInt(b.IntMul(b.Int(0), lanes), 0),
              "x * 1 and x * 0");
    TestCheck(Same(b.IntMin(lanes, lanes), lanes) && Same(b.IntMax(lanes, lanes), lanes), "min and max of one value");
    TestCheck(Same(b.IntAnd(lanes, b.Int(-1)), lanes) && b.IsConstantInt(b.IntAnd(lanes, b.Int(0)), 0),
              "and with all bits and none");
    TestCheck(Same(b.IntShiftRight(lanes, b.Int(32)), lanes), "a shift by 32 is none");
    TestCheck(Same(b.IntAdd(lanes, b.Int(2)), b.IntAdd(b.Int(2), lanes)), "integer addition is commutative");
    TestCheck(Same(b.IntMod(lanes, b.Int(8)), b.IntAnd(lanes, b.Int(7))), "a power-of-two remainder is a mask");
    TestCheck(IsOp(b, b.IntMod(lanes, b.Construct({b.Int(4), b.Int(6)})), CKJIT_OP_IMOD),
              "a mask needs a power of two in every component");

    const CKJitValue less = b.IntLess(lanes, b.Int(3));
    TestCheck(IsOp(b, less, CKJIT_OP_ILT) && b.TypeOf(less) == CKJIT_TYPE_BOOL2, "integer comparisons keep the width");
    TestCheck(Same(b.IntGreater(lanes, b.Int(3)), b.IntLess(b.Int(3), lanes)) &&
                  Same(b.IntGreaterEqual(lanes, b.Int(3)), b.IntLessEqual(b.Int(3), lanes)),
              "greater comparisons swap their operands");
    TestCheck(b.IsConstantBool(b.IntLessEqual(lanes, lanes), true) && b.IsConstantBool(b.IntLess(lanes, lanes), false) &&
                  b.IsConstantBool(b.IntNotEqual(lanes, lanes), false),
              "integers against themselves fold");
    TestCheck(!b.Failed(), "integer vectors are well typed");

    CKJitBuilder bad(4);
    const CKJitValue ints = bad.FloatToInt(bad.Input(kColor));
    TestCheck(!bad.IntAdd(ints, bad.FloatToInt(bad.Input(kTexCoord))).IsValid(), "integer vectors share a width");
    TestCheck(!bad.IntToFloat(bad.Float(1.0f)).IsValid(), "only integers convert to floats");
    TestCheck(!bad.Add(ints, ints).IsValid(), "float arithmetic takes floats");
    TestCheck(bad.Failed(), "ill-typed integers fail the builder");
}

void TestVectorBooleans() {
    CKJitBuilder b(4);
    const CKJitValue v = b.Input(kColor);
    const CKJitValue other = b.Uniform(0);
    const CKJitValue less = b.Less(v, other);
    TestCheck(IsOp(b, less, CKJIT_OP_LT) && b.TypeOf(less) == CKJIT_TYPE_BOOL4, "vector comparisons are component-wise");
    TestCheck(b.TypeOf(b.Less(v, b.Float(0.5f))) == CKJIT_TYPE_BOOL4, "a scalar splats against a vector");
    TestCheck(Same(b.Less(b.Float2(1.0f, 3.0f), b.Float2(2.0f, 2.0f)), b.Construct({b.Bool(true), b.Bool(false)})),
              "vector comparisons fold per component");
    TestCheck(b.IsConstantBool(b.Less(v, v), false), "x < x is false in every component");
    TestCheck(b.TypeOf(b.Not(less)) == CKJIT_TYPE_BOOL4 && Same(b.Not(b.Not(less)), less), "not is component-wise");
    TestCheck(Same(b.And(less, b.Bool(true)), less), "a scalar boolean splats against a vector");

    const CKJitValue x = b.Component(less, 0);
    const CKJitValue y = b.Component(less, 1);
    TestCheck(IsOp(b, b.Any(less), CKJIT_OP_ANY) && b.TypeOf(b.Any(less)) == CKJIT_TYPE_BOOL &&
                  IsOp(b, b.All(less), CKJIT_OP_ALL),
              "any and all reduce to a bool");
    TestCheck(Same(b.Any(x), x) && Same(b.All(x), x), "a scalar reduces to itself");
    TestCheck(b.IsConstantBool(b.Any(b.Construct({x, b.Bool(true)})), true), "a true component decides any");
    TestCheck(b.IsConstantBool(b.All(b.Construct({x, b.Bool(false)})), false), "a false component decides all");
    TestCheck(Same(b.Any(b.Construct({x, b.Bool(false)})), x), "false components drop out of any");
    TestCheck(Same(b.All(b.Construct({b.Bool(true), y, y})), y), "repeated components drop out");
    TestCheck(Same(b.Any(b.Construct({x, y, x})), b.Any(b.Swizzle(less, "xy"))), "any is over the distinct components");
    TestCheck(b.IsConstantBool(b.All(b.Construct({b.Bool(true), b.Bool(true)})), true), "all of true is true");

    const CKJitValue picked = b.Select(less, v, other);
    TestCheck(IsOp(b, picked, CKJIT_OP_SELECT) && b.TypeOf(picked) == CKJIT_TYPE_FLOAT4,
              "a vector condition selects per component");
    TestCheck(b.TypeOf(b.Select(less, b.Float(1.0f), b.Float(0.0f))) == CKJIT_TYPE_FLOAT4,
              "scalar arms splat to the condition");
    const CKJitValue mask = b.Construct({b.Bool(true), b.Bool(false), b.Bool(false), b.Bool(true)});
    TestCheck(Same(b.Select(mask, v, other),
                   b.Construct({b.Component(v, 0), b.Swizzle(other, "yz"), b.Component(v, 3)})),
              "a constant condition picks each component");
    const CKJitValue c = b.Less(b.Component(v, 3), b.Float(0.5f));
    TestCheck(Same(b.Select(b.Splat(c, 4), v, other), b.Select(c, v, other)), "a splatted condition picks whole arms");
    TestCheck(Same(b.Select(b.Not(less), other, v), picked), "negated vector conditions swap the arms");
    const CKJitValue greater = b.Less(other, v);
    TestCheck(Same(b.Select(less, b.Bool(true), greater), b.Or(less, greater)), "boolean arms fold per component");
    TestCheck(!b.Failed(), "vector booleans are well typed");

    CKJitBuilder bad(4);
    const CKJitValue color = bad.Input(kColor);
    const CKJitValue wide = bad.Less(color, bad.Uniform(0));
    const CKJitValue uv = bad.Input(kTexCoord);
    TestCheck(!bad.Select(wide, uv, uv).IsValid(), "arms have the condition's width");
    TestCheck(!bad.And(wide, bad.Less(uv, bad.Float(0.0f))).IsValid(), "boolean vectors share a width");
    TestCheck(!bad.Any(color).IsValid(), "any takes booleans");
    TestCheck(bad.Failed(), "ill-typed booleans fail the builder");
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

int FindOp(const CKJitFragmentShader &shader, CKJitOp op) {
    for (int i = 0; i < shader.Nodes.Size(); ++i) {
        if (shader.Nodes[i].Op == op)
            return i;
    }
    return -1;
}

template <typename Corrupt>
bool VerifiesAfter(const CKJitFragmentShader &shader, Corrupt corrupt) {
    CKJitFragmentShader copy = shader;
    corrupt(copy);
    return CKJitVerify(copy);
}

void TestVerify() {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord);
    const CKJitValue position = b.Input(kFragCoord);
    const CKJitValue sample = b.Sample(1, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    const CKJitValue color = b.Construct({b.Swizzle(b.Mul(sample, b.Uniform(3)), "xyz"), b.Component(position, 3)});
    const CKJitValue discard = b.Less(b.Component(color, 3), b.Float(0.5f));
    CKJitFragmentShader shader;
    TestCheck(b.Finish(color, discard, shader) && CKJitVerify(shader), "finished shaders verify");
    TestCheck(CKJitOpOperandCount(CKJIT_OP_SELECT) == 3 && CKJitOpOperandCount(CKJIT_OP_CONSTANT) == 0 &&
                  (CKJitOpFlags(CKJIT_OP_CONSTRUCT) & CKJIT_OPFLAG_VARIADIC) != 0,
              "the op table records operand counts");

    const int mul = FindOp(shader, CKJIT_OP_MUL);
    const int input = FindOp(shader, CKJIT_OP_INPUT);
    const int uniform = FindOp(shader, CKJIT_OP_UNIFORM);
    const int swizzle = FindOp(shader, CKJIT_OP_SWIZZLE);
    const int construct = FindOp(shader, CKJIT_OP_CONSTRUCT);
    const int sampled = FindOp(shader, CKJIT_OP_SAMPLE);
    TestCheck(mul >= 0 && input >= 0 && uniform >= 0 && swizzle >= 0 && construct >= 0 && sampled >= 0,
              "the program has the nodes to corrupt");
    if (mul < 0 || input < 0 || uniform < 0 || swizzle < 0 || construct < 0 || sampled < 0)
        return;

    // Each corruption is one a backend would index with.
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[mul].Operands[0] = (uint32_t)mul; }),
              "operands precede their users");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[mul].OperandCount = 1; }),
              "operations have their operand count");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[construct].OperandCount = 1; }),
              "constructs have at least two parts");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[mul].Op = CKJIT_OP_COUNT; }),
              "operations are known");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[mul].Type = CKJIT_TYPE_COUNT; }),
              "types are known");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[input].Imm[0] = 2; }),
              "inputs are declared");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[input].Type = CKJIT_TYPE_FLOAT4; }),
              "an input has its declared width");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Inputs[1].Components = 3; }),
              "the fragment position is a float4");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[uniform].Imm[0] = 4; }),
              "uniform rows are in the block");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[swizzle].Imm[0] = 4; }),
              "swizzles select existing components");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[swizzle].Type = CKJIT_TYPE_INT3; }),
              "swizzles keep their operand's kind");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[sampled].Imm[0] = CKJIT_MAX_SAMPLERS; }),
              "sampler slots are bounded");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[sampled].Imm[1] = 3; }),
              "sampler dimensions are known");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Color = s.Discard; }), "the colour is a float4");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Discard = s.Color; }), "the discard is boolean");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Discard.Id = (uint32_t)s.Nodes.Size(); }),
              "outputs are nodes");

    CKJitBuilder dims(4);
    const CKJitValue flat = dims.Sample(0, CKJIT_SAMPLER_2D, dims.Input(kTexCoord), dims.Float(0.0f));
    const CKJitValue cube = dims.Sample(1, CKJIT_SAMPLER_CUBE, dims.Swizzle(dims.Input(kColor), "xyz"), dims.Float(0.0f));
    CKJitFragmentShader twoSlots;
    TestCheck(dims.Finish(dims.Add(flat, cube), CKJitValue(), twoSlots) && CKJitVerify(twoSlots), "two slots verify");
    TestCheck(!VerifiesAfter(twoSlots,
                             [&](CKJitFragmentShader &s) {
                                 s.Nodes[FindOp(s, CKJIT_OP_SAMPLE)].Imm[0] = 1;
                             }),
              "a sampler slot has one dimension");
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
    framework.Run("integers", TestIntegers);
    framework.Run("vector booleans", TestVectorBooleans);
    framework.Run("booleans and selects", TestBooleansAndSelects);
    framework.Run("samples and inputs", TestSamplesAndInputs);
    framework.Run("finish", TestFinish);
    framework.Run("failure propagation", TestFailurePropagation);
    framework.Run("verify", TestVerify);
    framework.Run("dump", TestDump);
    return framework.ExitCode();
}
