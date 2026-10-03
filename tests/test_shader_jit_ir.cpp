#include "CKJitBuilder.h"
#include "TestTriangleMultiset.h"
#include "TestShaderJitVertex.h"

#include <cfenv>
#include <cstring>
#include <limits>

namespace {

const CKJitInput kColor = {1, 4, CKJIT_INPUT_SMOOTH};
const CKJitInput kTexCoord = {4, 2, CKJIT_INPUT_SMOOTH};
const CKJitInput kFragCoord = {0, 4, CKJIT_INPUT_FRAG_COORD};

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
    TestCheck(Same(b.Ceil(b.Float2(-1.5f, 1.25f)), b.Float2(-1.0f, 2.0f)), "ceil folds");
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
    TestCheck(IsOp(b, b.Log2(b.Float(8.0f)), CKJIT_OP_LOG2), "log2 is never folded");
    TestCheck(Same(b.Ddx(b.Float3(1.0f, -2.0f, 0.5f)), b.Float3(0.0f, 0.0f, 0.0f)) &&
                  Same(b.Ddy(b.Float(-3.0f)), b.Float(0.0f)),
              "a constant has no derivative");
    TestCheck(IsOp(b, b.Ddx(b.Float(std::numeric_limits<float>::infinity())), CKJIT_OP_DDX),
              "the derivative of an infinity is NaN, left to the GPU");
    TestCheck(IsOp(b, b.Dot(b.Float2(1.0f, 2.0f), b.Float2(3.0f, 4.0f)), CKJIT_OP_DOT), "dot is never folded");
    TestCheck(!b.Failed(), "unfolded forms are not errors");
}

void TestAlgebraicIdentities() {
    CKJitBuilder b(4);
    const CKJitValue x = b.Input(kColor);
    const CKJitValue y = b.Uniform(0);
    TestCheck(IsOp(b, b.Add(x, b.Float(0.0f)), CKJIT_OP_ADD) &&
                  IsOp(b, b.Add(b.Float4(0, 0, 0, 0), x), CKJIT_OP_ADD),
              "adding positive zero must retain its effect on negative zero");
    TestCheck(Same(b.Add(x, b.Float(-0.0f)), x) && Same(b.Add(b.Float(-0.0f), x), x), "x + -0");
    TestCheck(Same(b.Sub(x, b.Float(0.0f)), x), "x - 0");
    TestCheck(IsOp(b, b.Sub(x, b.Float(-0.0f)), CKJIT_OP_SUB), "subtracting negative zero can change a zero's sign");
    TestCheck(IsOp(b, b.Add(x, b.Float4(-0.0f, 0.0f, -0.0f, 0.0f)), CKJIT_OP_ADD),
              "zero identities check every component's sign");
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
    const CKJitValue ceiled = b.Ceil(x);
    TestCheck(Same(b.Floor(ceiled), ceiled) && Same(b.Ceil(b.Floor(x)), b.Floor(x)) &&
                  Same(b.RoundEven(ceiled), ceiled) && Same(b.Ceil(b.RoundEven(y)), b.RoundEven(y)),
              "rounding keeps integral values");
    const CKJitValue converted = b.IntToFloat(b.FloatToInt(x));
    TestCheck(Same(b.Floor(converted), converted) && Same(b.Ceil(converted), converted),
              "converted integers are integral");
    TestCheck(IsOp(b, b.Floor(b.Saturate(x)), CKJIT_OP_FLOOR), "other values are rounded");
    const CKJitValue slope = b.Ddx(b.Swizzle(x, "xy"));
    TestCheck(IsOp(b, slope, CKJIT_OP_DDX) && b.TypeOf(slope) == CKJIT_TYPE_FLOAT2 &&
                  IsOp(b, b.Ddy(slope), CKJIT_OP_DDY),
              "derivatives are component-wise");
    TestCheck(IsOp(b, b.Lerp(x, y, b.Float(0.0f)), CKJIT_OP_ADD), "lerp at zero still evaluates the difference");
    TestCheck(Same(b.Length(b.Component(x, 0)), b.Abs(b.Component(x, 0))), "scalar length is abs");
    TestCheck(b.TypeOf(b.Mul(x, b.Component(y, 2))) == CKJIT_TYPE_FLOAT4, "scalars splat against vectors");
    TestCheck(!b.Failed(), "identities are not errors");
}

void TestRoundingEnvironment() {
    const int previous = std::fegetround();
    const int modes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    bool configured = true;
    bool rounded = true;
    bool arithmetic = true;
    bool preserved = true;
    for (int mode : modes) {
        configured &= std::fesetround(mode) == 0;
        CKJitBuilder b(0);
        rounded &= Same(b.RoundEven(b.Float4(0.5f, 1.5f, 2.5f, 3.5f)), b.Float4(0, 2, 2, 4));
        rounded &= Same(b.RoundEven(b.Float4(-0.5f, -1.5f, -2.5f, -3.5f)), b.Float4(-0.0f, -2, -2, -4));
        rounded &= Same(b.RoundEven(b.Float4(0.0f, -0.0f, 0.25f, -0.25f)), b.Float4(0.0f, -0.0f, 0.0f, -0.0f));
        rounded &= Same(b.RoundEven(b.Float4(0.49999997f, 0.50000006f, -0.49999997f, -0.50000006f)),
                        b.Float4(0.0f, 1.0f, -0.0f, -1.0f));
        rounded &= Same(b.RoundEven(b.Float4(8388607.5f, -8388607.5f, 8388608.0f, -8388608.0f)),
                        b.Float4(8388608.0f, -8388608.0f, 8388608.0f, -8388608.0f));
        const float infinity = std::numeric_limits<float>::infinity();
        rounded &= Same(b.RoundEven(b.Float2(infinity, -infinity)), b.Float2(infinity, -infinity));
        const CKJitValue sum = b.Add(b.Float(1.0f), b.Float(0x1p-24f));
        arithmetic &= mode == FE_TONEAREST ? Same(sum, b.Float(1.0f)) : IsOp(b, sum, CKJIT_OP_ADD);
        preserved &= std::fegetround() == mode;
    }
    // The test harness uses longjmp, so restore before any assertion can fail.
    const bool restored = std::fesetround(previous) == 0;
    TestCheck(configured && restored, "the test can set and restore the rounding environment");
    TestCheck(rounded, "round-even folds ties, signs and large values independently of the host rounding mode");
    TestCheck(arithmetic, "inexact host arithmetic is not folded under a directed rounding mode");
    TestCheck(preserved, "folding preserves the caller's rounding mode");
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
    TestCheck(Same(b.FloatToInt(b.Float2(-2147483648.0f, 2147483520.0f)),
                   b.Construct({b.Int(INT32_MIN), b.Int(2147483520)})),
              "both representable conversion boundaries fold");
    TestCheck(IsOp(b, b.FloatToInt(b.Float(2147483648.0f)), CKJIT_OP_FTOI) &&
                  IsOp(b, b.FloatToInt(b.Float(-2147483904.0f)), CKJIT_OP_FTOI) &&
                  IsOp(b, b.FloatToInt(b.Float(std::numeric_limits<float>::infinity())), CKJIT_OP_FTOI) &&
                  IsOp(b, b.FloatToInt(b.Float(std::numeric_limits<float>::quiet_NaN())), CKJIT_OP_FTOI),
              "conversions outside the portable domain are left to the backend");
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
    TestCheck(!bad.Ddx(ints).IsValid() && !bad.Ceil(ints).IsValid(), "derivatives and rounding take floats");
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
    TestCheck(!inputs.Input(conflicting).IsValid(), "a location has one declaration");
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
    TestCheck(shader.Inputs.Size() == 2 && shader.Inputs[0].Kind == CKJIT_INPUT_FRAG_COORD,
              "every declared input is kept");
    TestCheck(shader.UniformBufferCount == 1 && shader.UniformVec4Counts[0] == 4, "the uniform block size is kept");

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

void TestMad() {
    for (uint32_t width = 1; width <= 4; ++width) for (unsigned vectorOperand = 0; vectorOperand < 3; ++vectorOperand) {
        CKJitBuilder b(0);
        const CKJitValue position = b.Input({0, 4, CKJIT_INPUT_ATTRIBUTE});
        const CKJitValue vector = b.Input({1, (uint8_t)width, CKJIT_INPUT_ATTRIBUTE});
        const CKJitValue scalar = b.Component(position, 0);
        CKJitValue operands[] = {scalar, scalar, scalar};
        operands[vectorOperand] = vector;
        const CKJitValue mad = b.Mad(operands[0], operands[1], operands[2]);
        TestCheck(IsOp(b, mad, CKJIT_OP_MAD) && b.Node(mad).Type == CKJitFloatType(width),
                  "MAD broadcasts scalars in each operand position");
        TestCheck(Same(mad, b.Mad(operands[0], operands[1], operands[2])), "MAD reuses identical nodes");
        CKJitVertexShader shader;
        TestCheck(b.FinishVertex(position, {{0, CKJIT_INPUT_SMOOTH, mad}, {1, CKJIT_INPUT_SMOOTH, scalar}}, shader),
                  "MAD operand widths verify after broadcasting");
        CKJitVertexShader corrupt = shader;
        corrupt.Nodes[corrupt.Outputs[0].Value.Id].OperandCount = 2;
        TestCheck(!CKJitVerify(corrupt), "MAD requires its addend");
        corrupt = shader;
        corrupt.Nodes[corrupt.Outputs[0].Value.Id].Type = CKJitIntType(width);
        TestCheck(!CKJitVerify(corrupt), "MAD is floating point only");
        if (width > 1) {
            corrupt = shader;
            corrupt.Nodes[corrupt.Outputs[0].Value.Id].Operands[2] = corrupt.Outputs[1].Value.Id;
            TestCheck(!CKJitVerify(corrupt), "raw MAD operands require equal widths");
        }
    }
    CKJitBuilder constants(0);
    TestCheck(IsOp(constants, constants.Mad(constants.Float(1), constants.Float(2), constants.Float(3)), CKJIT_OP_MAD),
              "native MAD rounding is not folded on the host");
    for (unsigned bad = 0; bad < 4; ++bad) {
        CKJitBuilder b(0);
        CKJitValue operands[] = {b.Float2(1, 2), b.Float(3), b.Float(4)};
        if (bad < 3) operands[bad] = b.Int(1);
        else operands[2] = b.Float3(1, 2, 3);
        TestCheck(!b.Mad(operands[0], operands[1], operands[2]).IsValid() && b.Failed(),
                  "MAD rejects integer operands and mismatched vectors");
    }
}

template <typename Corrupt>
bool VerifiesAfter(const CKJitFragmentShader &shader, Corrupt corrupt) {
    CKJitFragmentShader copy = shader;
    corrupt(copy);
    return CKJitVerify(copy);
}

// Gives every texture operation of a slot another dimension.
void Redimension(CKJitFragmentShader &shader, uint32_t slot, CKJitSamplerDim dim) {
    for (int i = 0; i < shader.Nodes.Size(); ++i) {
        if ((CKJitOpFlags(shader.Nodes[i].Op) & CKJIT_OPFLAG_TEXTURE) != 0 && shader.Nodes[i].Imm[0] == slot)
            shader.Nodes[i].Imm[1] = dim;
    }
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
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[sampled].Imm[1] = 4; }),
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
    TestCheck(twoSlots.SamplerCount == 2, "a shader binds the slots up to the highest it reads");
    TestCheck(!VerifiesAfter(twoSlots, [&](CKJitFragmentShader &s) { s.SamplerCount = 1; }),
              "sampler slots are below the bound count");
    TestCheck(!VerifiesAfter(twoSlots, [&](CKJitFragmentShader &s) { s.SamplerCount = CKJIT_MAX_SAMPLERS + 1; }),
              "the bound count is bounded");

    CKJitBuilder dead(4);
    const CKJitValue kept = dead.Sample(2, CKJIT_SAMPLER_2D, dead.Input(kTexCoord), dead.Float(0.0f));
    dead.Sample(9, CKJIT_SAMPLER_2D, dead.Input(kTexCoord), dead.Float(0.0f));
    CKJitFragmentShader live;
    TestCheck(dead.Finish(kept, CKJitValue(), live) && CKJitVerify(live) && live.SamplerCount == 3,
              "slots that only dead operations read are not bound");
}

bool VerifiesTypedNode(CKJitOp op, CKJitType type, std::initializer_list<CKJitType> operands,
                       CKJitSamplerDim dim = CKJIT_SAMPLER_2D) {
    CKJitFragmentShader shader;
    CKJitNode color = {};
    color.Op = CKJIT_OP_CONSTANT;
    color.Type = CKJIT_TYPE_FLOAT4;
    shader.Nodes.PushBack(color);
    shader.Color = CKJitValue{0};
    shader.SamplerCount = 1;
    CKJitNode node = {};
    node.Op = op;
    node.Type = type;
    node.Imm[1] = dim;
    for (CKJitType operand : operands) {
        CKJitNode constant = {};
        constant.Op = CKJIT_OP_CONSTANT;
        constant.Type = operand;
        node.Operands[node.OperandCount++] = (uint32_t)shader.Nodes.Size();
        shader.Nodes.PushBack(constant);
    }
    shader.Nodes.PushBack(node);
    return CKJitVerify(shader);
}

void TestVerifyOperandTypes() {
    const CKJitOp floatBinary[] = {CKJIT_OP_ADD, CKJIT_OP_SUB, CKJIT_OP_MUL, CKJIT_OP_DIV, CKJIT_OP_MIN, CKJIT_OP_MAX};
    for (CKJitOp op : floatBinary) {
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_FLOAT2, {CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT2}), "float vector arithmetic verifies");
        TestCheck(!VerifiesTypedNode(op, CKJIT_TYPE_FLOAT2, {CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT}), "IR arithmetic needs explicit splats");
        TestCheck(!VerifiesTypedNode(op, CKJIT_TYPE_INT2, {CKJIT_TYPE_INT2, CKJIT_TYPE_INT2}), "float arithmetic rejects integer operands");
    }
    const CKJitOp floatUnary[] = {CKJIT_OP_NEG, CKJIT_OP_ABS, CKJIT_OP_SATURATE, CKJIT_OP_FLOOR, CKJIT_OP_CEIL,
                                CKJIT_OP_ROUND_EVEN, CKJIT_OP_EXP2, CKJIT_OP_LOG2, CKJIT_OP_SQRT, CKJIT_OP_DDX, CKJIT_OP_DDY};
    for (CKJitOp op : floatUnary) {
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_FLOAT3, {CKJIT_TYPE_FLOAT3}), "float unary operation verifies");
        TestCheck(!VerifiesTypedNode(op, CKJIT_TYPE_FLOAT2, {CKJIT_TYPE_FLOAT3}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_INT, {CKJIT_TYPE_INT}), "float unary operation preserves type");
    }
    const CKJitOp intBinary[] = {CKJIT_OP_IADD, CKJIT_OP_ISUB, CKJIT_OP_IMUL, CKJIT_OP_IMIN, CKJIT_OP_IMAX,
                               CKJIT_OP_IMOD, CKJIT_OP_IAND, CKJIT_OP_ISHR};
    for (CKJitOp op : intBinary) {
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_INT4, {CKJIT_TYPE_INT4, CKJIT_TYPE_INT4}), "integer arithmetic verifies");
        TestCheck(!VerifiesTypedNode(op, CKJIT_TYPE_INT4, {CKJIT_TYPE_INT, CKJIT_TYPE_INT4}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_FLOAT, {CKJIT_TYPE_FLOAT, CKJIT_TYPE_FLOAT}), "integer operands have one type");
    }
    const CKJitOp comparisons[] = {CKJIT_OP_LT, CKJIT_OP_LE, CKJIT_OP_EQ, CKJIT_OP_NE,
                                  CKJIT_OP_ILT, CKJIT_OP_ILE, CKJIT_OP_IEQ, CKJIT_OP_INE};
    for (CKJitOp op : comparisons) {
        const CKJitType input = op <= CKJIT_OP_NE ? CKJIT_TYPE_FLOAT2 : CKJIT_TYPE_INT2;
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_BOOL2, {input, input}), "comparison produces a boolean per lane");
        TestCheck(!VerifiesTypedNode(op, CKJIT_TYPE_BOOL, {input, input}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_BOOL2, {CKJIT_TYPE_BOOL2, CKJIT_TYPE_BOOL2}), "comparison types are checked");
    }
    TestCheck(VerifiesTypedNode(CKJIT_OP_CONSTRUCT, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT, CKJIT_TYPE_FLOAT3}) &&
                  !VerifiesTypedNode(CKJIT_OP_CONSTRUCT, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT3}) &&
                  !VerifiesTypedNode(CKJIT_OP_CONSTRUCT, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_INT, CKJIT_TYPE_FLOAT3}),
              "construct concatenates exactly the result width of one kind");
    TestCheck(VerifiesTypedNode(CKJIT_OP_DOT, CKJIT_TYPE_FLOAT, {CKJIT_TYPE_FLOAT3, CKJIT_TYPE_FLOAT3}) &&
                  !VerifiesTypedNode(CKJIT_OP_DOT, CKJIT_TYPE_FLOAT3, {CKJIT_TYPE_FLOAT3, CKJIT_TYPE_FLOAT3}) &&
                  !VerifiesTypedNode(CKJIT_OP_DOT, CKJIT_TYPE_FLOAT, {CKJIT_TYPE_FLOAT, CKJIT_TYPE_FLOAT}), "dot reduces float vectors");
    TestCheck(VerifiesTypedNode(CKJIT_OP_FTOI, CKJIT_TYPE_INT3, {CKJIT_TYPE_FLOAT3}) &&
                  !VerifiesTypedNode(CKJIT_OP_FTOI, CKJIT_TYPE_INT2, {CKJIT_TYPE_FLOAT3}) &&
                  !VerifiesTypedNode(CKJIT_OP_FTOI, CKJIT_TYPE_INT3, {CKJIT_TYPE_INT3}) &&
                  VerifiesTypedNode(CKJIT_OP_ITOF, CKJIT_TYPE_FLOAT3, {CKJIT_TYPE_INT3}) &&
                  !VerifiesTypedNode(CKJIT_OP_ITOF, CKJIT_TYPE_FLOAT3, {CKJIT_TYPE_FLOAT3}), "conversions check both kinds and widths");
    for (CKJitOp op : {CKJIT_OP_AND, CKJIT_OP_OR}) {
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_BOOL2, {CKJIT_TYPE_BOOL2, CKJIT_TYPE_BOOL2}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_BOOL2, {CKJIT_TYPE_BOOL, CKJIT_TYPE_BOOL2}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_INT, {CKJIT_TYPE_INT, CKJIT_TYPE_INT}), "boolean binary operands match");
    }
    TestCheck(VerifiesTypedNode(CKJIT_OP_NOT, CKJIT_TYPE_BOOL2, {CKJIT_TYPE_BOOL2}) &&
                  !VerifiesTypedNode(CKJIT_OP_NOT, CKJIT_TYPE_BOOL, {CKJIT_TYPE_BOOL2}), "not preserves its boolean type");
    for (CKJitOp op : {CKJIT_OP_ANY, CKJIT_OP_ALL}) {
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_BOOL, {CKJIT_TYPE_BOOL3}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_BOOL3, {CKJIT_TYPE_BOOL3}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_BOOL, {CKJIT_TYPE_INT3}), "reductions produce a scalar boolean");
    }
    TestCheck(VerifiesTypedNode(CKJIT_OP_SELECT, CKJIT_TYPE_INT2, {CKJIT_TYPE_BOOL, CKJIT_TYPE_INT2, CKJIT_TYPE_INT2}) &&
                  VerifiesTypedNode(CKJIT_OP_SELECT, CKJIT_TYPE_INT2, {CKJIT_TYPE_BOOL2, CKJIT_TYPE_INT2, CKJIT_TYPE_INT2}) &&
                  !VerifiesTypedNode(CKJIT_OP_SELECT, CKJIT_TYPE_INT2, {CKJIT_TYPE_BOOL3, CKJIT_TYPE_INT2, CKJIT_TYPE_INT2}) &&
                  !VerifiesTypedNode(CKJIT_OP_SELECT, CKJIT_TYPE_INT2, {CKJIT_TYPE_BOOL, CKJIT_TYPE_INT2, CKJIT_TYPE_FLOAT2}),
              "select checks its condition and both arms");
    for (CKJitOp op : {CKJIT_OP_SAMPLE, CKJIT_OP_SAMPLE_LEVEL}) {
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT3, CKJIT_TYPE_FLOAT}) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT2}), "samples check coordinates and scalar LOD");
    }
    TestCheck(VerifiesTypedNode(CKJIT_OP_SAMPLE_GRAD, CKJIT_TYPE_FLOAT4,
                               {CKJIT_TYPE_FLOAT3, CKJIT_TYPE_FLOAT3, CKJIT_TYPE_FLOAT3}, CKJIT_SAMPLER_3D) &&
                  !VerifiesTypedNode(CKJIT_OP_SAMPLE_GRAD, CKJIT_TYPE_FLOAT4,
                                     {CKJIT_TYPE_FLOAT3, CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT3}, CKJIT_SAMPLER_3D), "gradients match the coordinates");
    TestCheck(VerifiesTypedNode(CKJIT_OP_CALC_LOD, CKJIT_TYPE_FLOAT, {CKJIT_TYPE_FLOAT3}, CKJIT_SAMPLER_CUBE) &&
                  !VerifiesTypedNode(CKJIT_OP_CALC_LOD, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT3}, CKJIT_SAMPLER_CUBE), "LOD query is scalar");
    for (CKJitOp op : {CKJIT_OP_SAMPLE_CMP, CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO}) {
        TestCheck(VerifiesTypedNode(op, CKJIT_TYPE_FLOAT, {CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT}, CKJIT_SAMPLER_2D_COMPARE) &&
                      !VerifiesTypedNode(op, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT2, CKJIT_TYPE_FLOAT}, CKJIT_SAMPLER_2D_COMPARE),
                  "depth comparison samples produce a scalar");
    }
    TestCheck(VerifiesTypedNode(CKJIT_OP_LOAD, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_INT4}, CKJIT_SAMPLER_3D) &&
                  !VerifiesTypedNode(CKJIT_OP_LOAD, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_INT3}, CKJIT_SAMPLER_3D) &&
                  !VerifiesTypedNode(CKJIT_OP_LOAD, CKJIT_TYPE_FLOAT4, {CKJIT_TYPE_FLOAT3}), "loads use integer coordinates with a mip");
    TestCheck(VerifiesTypedNode(CKJIT_OP_SIZE, CKJIT_TYPE_INT2, {CKJIT_TYPE_INT}) &&
                  !VerifiesTypedNode(CKJIT_OP_SIZE, CKJIT_TYPE_INT3, {CKJIT_TYPE_INT}) &&
                  !VerifiesTypedNode(CKJIT_OP_SIZE, CKJIT_TYPE_INT2, {CKJIT_TYPE_FLOAT}) &&
                  VerifiesTypedNode(CKJIT_OP_LEVELS, CKJIT_TYPE_INT, {}) &&
                  !VerifiesTypedNode(CKJIT_OP_LEVELS, CKJIT_TYPE_FLOAT, {}), "texture queries return integer dimensions");
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
    const char *expected = "%0 = INPUT float4 location1\n"
                           "%1 = UNIFORM float4 c2\n"
                           "%2 = CONSTANT float (0.5)\n"
                           "%3 = MUL float4 %0, %1\n"
                           "%4 = SWIZZLE float %3 .w\n"
                           "%5 = LT bool %4, %2\n"
                           "color %3\n"
                           "discard %5\n";
    const XString dump = CKJitDump(shader);
    TestCheck(std::strcmp(dump.CStr(), expected) == 0, "the listing names every node");
}

void TestUniformBuffers() {
    const uint32_t counts[] = {2, 5, 2};
    CKJitBuilder b(counts, 3);
    const CKJitValue first = b.Uniform(1);
    const CKJitValue other = b.Uniform(1, 1);
    const CKJitValue last = b.Uniform(1, 4);
    TestCheck(first.IsValid() && other.IsValid() && last.IsValid() && first.Id != other.Id,
              "a row is addressed by its buffer");
    TestCheck(b.Uniform(0, 1).Id == first.Id, "a bare row is in buffer 0");
    const CKJitValue color = b.Add(b.Mul(first, other), b.Mul(last, b.Uniform(2, 0)));
    CKJitFragmentShader shader;
    TestCheck(b.Finish(color, CKJitValue(), shader) && CKJitVerify(shader), "the shader verifies");
    TestCheck(shader.UniformBufferCount == 3 && shader.UniformVec4Counts[0] == 2 && shader.UniformVec4Counts[1] == 5 &&
                  shader.UniformVec4Counts[2] == 2 && shader.UniformVec4Counts[3] == 0,
              "every buffer size is kept");
    TestCheck(std::strstr(CKJitDump(shader).CStr(), "UNIFORM float4 cb1 c4\n") != nullptr,
              "the listing names the buffer of a row");

    int uniform = -1;
    for (int i = 0; i < shader.Nodes.Size(); ++i) {
        if (shader.Nodes[i].Op == CKJIT_OP_UNIFORM && shader.Nodes[i].Imm[1] == 1 && shader.Nodes[i].Imm[0] == 4)
            uniform = i;
    }
    TestCheck(uniform >= 0, "the program has the row to corrupt");
    if (uniform < 0)
        return;
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[uniform].Imm[1] = 3; }),
              "uniform buffers are declared");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { s.Nodes[uniform].Imm[1] = 2; }),
              "uniform rows are in their buffer");
    TestCheck(!VerifiesAfter(shader, [](CKJitFragmentShader &s) { s.UniformBufferCount = CKJIT_MAX_UNIFORM_BUFFERS + 1; }),
              "uniform buffers are bounded");

    CKJitBuilder undeclared(counts, 3);
    TestCheck(!undeclared.Uniform(3, 0).IsValid() && undeclared.Failed(), "an undeclared buffer fails");
    CKJitBuilder outside(counts, 3);
    TestCheck(!outside.Uniform(2, 2).IsValid() && outside.Failed(), "a row past its buffer fails");
    const uint32_t many[CKJIT_MAX_UNIFORM_BUFFERS + 1] = {1, 1, 1, 1, 1};
    CKJitBuilder tooMany(many, CKJIT_MAX_UNIFORM_BUFFERS + 1);
    TestCheck(tooMany.Failed() && !tooMany.Uniform(0).IsValid(), "too many buffers fail");
}

void TestTextureAccess() {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord);
    const CKJitValue direction = b.Swizzle(b.Input(kColor), "xyz");
    const CKJitValue level = b.SampleLevel(0, CKJIT_SAMPLER_2D, uv, b.Float(1.5f));
    const CKJitValue graded = b.SampleGrad(1, CKJIT_SAMPLER_CUBE, direction, b.Ddx(direction), b.Ddy(direction));
    const CKJitValue lod = b.CalcLod(0, CKJIT_SAMPLER_2D, uv);
    const CKJitValue loaded = b.Load(2, CKJIT_SAMPLER_3D, b.Construct({b.FloatToInt(direction), b.Int(1)}));
    const CKJitValue fetched = b.Load(0, CKJIT_SAMPLER_2D, b.Construct({b.FloatToInt(uv), b.Int(0)}));
    const CKJitValue size = b.TextureSize(1, CKJIT_SAMPLER_CUBE, b.Int(0));
    const CKJitValue extent = b.TextureSize(2, CKJIT_SAMPLER_3D, b.Int(1));
    const CKJitValue levels = b.TextureLevels(0, CKJIT_SAMPLER_2D);
    TestCheck(IsOp(b, level, CKJIT_OP_SAMPLE_LEVEL) && b.TypeOf(level) == CKJIT_TYPE_FLOAT4 &&
                  IsOp(b, graded, CKJIT_OP_SAMPLE_GRAD) && b.TypeOf(graded) == CKJIT_TYPE_FLOAT4,
              "explicit-LOD samples are float4");
    TestCheck(IsOp(b, lod, CKJIT_OP_CALC_LOD) && b.TypeOf(lod) == CKJIT_TYPE_FLOAT, "an LOD is a float");
    TestCheck(IsOp(b, loaded, CKJIT_OP_LOAD) && b.TypeOf(loaded) == CKJIT_TYPE_FLOAT4 &&
                  b.TypeOf(fetched) == CKJIT_TYPE_FLOAT4,
              "loads are float4");
    TestCheck(b.TypeOf(size) == CKJIT_TYPE_INT2 && b.TypeOf(extent) == CKJIT_TYPE_INT3 &&
                  IsOp(b, levels, CKJIT_OP_LEVELS) && b.TypeOf(levels) == CKJIT_TYPE_INT,
              "a size has the slot's dimensions and the mip count is an int");
    TestCheck(b.Node(graded).Imm[0] == 1 && b.Node(graded).Imm[1] == CKJIT_SAMPLER_CUBE &&
                  b.Node(levels).Imm[0] == 0 && b.Node(levels).Imm[1] == CKJIT_SAMPLER_2D,
              "texture operations record slot and dim");
    TestCheck(Same(b.TextureLevels(0, CKJIT_SAMPLER_2D), levels) && Same(b.CalcLod(0, CKJIT_SAMPLER_2D, uv), lod) &&
                  !Same(b.TextureLevels(3, CKJIT_SAMPLER_2D), levels),
              "equal texture operations are shared");
    TestCheck(!b.Failed(), "texture access is well typed");

    CKJitBuilder bad(4);
    const CKJitValue badUv = bad.Input(kTexCoord);
    const CKJitValue texel = bad.Construct({bad.FloatToInt(badUv), bad.Int(0)});
    TestCheck(!bad.Load(0, CKJIT_SAMPLER_CUBE, texel).IsValid(), "cubes are not loaded from");
    TestCheck(!bad.Load(0, CKJIT_SAMPLER_2D, bad.FloatToInt(badUv)).IsValid(), "a load takes the mip after the texel");
    TestCheck(!bad.Load(0, CKJIT_SAMPLER_2D, bad.Construct({badUv, bad.Float(0.0f)})).IsValid(),
              "load addresses are integers");
    TestCheck(!bad.TextureSize(0, CKJIT_SAMPLER_2D, bad.Float(0.0f)).IsValid(), "mips are integers");
    TestCheck(!bad.SampleLevel(0, CKJIT_SAMPLER_2D, badUv, bad.Int(0)).IsValid(), "LODs are floats");
    TestCheck(!bad.SampleGrad(0, CKJIT_SAMPLER_2D, badUv, bad.Ddx(badUv), bad.Float(0.0f)).IsValid(),
              "derivatives have the coordinate's type");
    TestCheck(!bad.CalcLod(0, CKJIT_SAMPLER_3D, badUv).IsValid(), "volume LODs take three coordinates");
    TestCheck(!bad.TextureLevels(CKJIT_MAX_SAMPLERS, CKJIT_SAMPLER_2D).IsValid(), "slots are bounded");
    TestCheck(!bad.TextureLevels(0, (CKJitSamplerDim)4).IsValid(), "dimensions are known");
    TestCheck(bad.Failed(), "ill-typed texture access fails the builder");

    CKJitBuilder dims(4);
    const CKJitValue cube = dims.Sample(1, CKJIT_SAMPLER_CUBE, dims.Swizzle(dims.Input(kColor), "xyz"), dims.Float(0.0f));
    TestCheck(cube.IsValid() && !dims.TextureSize(1, CKJIT_SAMPLER_2D, dims.Int(0)).IsValid(),
              "a slot keeps its dimension across operations");

    CKJitFragmentShader shader;
    const CKJitValue sampled = b.Add(b.Add(level, graded), b.Mul(loaded, fetched));
    const CKJitValue sizes = b.IntToFloat(b.Construct({size, b.Component(extent, 2), levels}));
    TestCheck(b.Finish(b.Add(b.Mul(sampled, sizes), lod), CKJitValue(), shader) && CKJitVerify(shader),
              "texture access verifies");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { Redimension(s, 2, CKJIT_SAMPLER_CUBE); }),
              "no cube is loaded from");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { Redimension(s, 1, CKJIT_SAMPLER_3D); }),
              "changing a cube to a volume also requires a three-component size result");
    TestCheck(!VerifiesAfter(shader,
                             [&](CKJitFragmentShader &s) {
                                 s.Nodes[FindOp(s, CKJIT_OP_LEVELS)].Imm[0] = CKJIT_MAX_SAMPLERS;
                             }),
              "every texture operation's slot is checked");
    TestCheck(!VerifiesAfter(shader,
                             [&](CKJitFragmentShader &s) { s.Nodes[FindOp(s, CKJIT_OP_SIZE)].Imm[1] = 2; }),
              "every texture operation's slot has one dimension");

    CKJitBuilder listed(4);
    const CKJitValue mips = listed.TextureLevels(3, CKJIT_SAMPLER_3D);
    const CKJitValue color = listed.Splat(listed.IntToFloat(mips), 4);
    CKJitFragmentShader listing;
    TestCheck(listed.Finish(color, CKJitValue(), listing), "the query finishes");
    const char *expected = "%0 = LEVELS int slot 3 dim 2\n"
                           "%1 = ITOF float %0\n"
                           "%2 = SWIZZLE float4 %1 .xxxx\n"
                           "color %2\n";
    TestCheck(std::strcmp(CKJitDump(listing).CStr(), expected) == 0, "the listing names texture slots");
}

void TestDepthComparison() {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord);
    const CKJitValue reference = b.Component(b.Input(kColor), 2);
    const CKJitValue filtered = b.SampleCmp(5, uv, reference);
    const CKJitValue base = b.SampleCmpLevelZero(5, b.Swizzle(uv, "yx"), b.Float(0.5f));
    const CKJitValue depth = b.Load(5, CKJIT_SAMPLER_2D_COMPARE, b.Construct({b.FloatToInt(uv), b.Int(0)}));
    const CKJitValue size = b.TextureSize(5, CKJIT_SAMPLER_2D_COMPARE, b.Int(0));
    const CKJitValue levels = b.TextureLevels(5, CKJIT_SAMPLER_2D_COMPARE);
    const CKJitValue sampled = b.Sample(1, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    TestCheck(IsOp(b, filtered, CKJIT_OP_SAMPLE_CMP) && b.TypeOf(filtered) == CKJIT_TYPE_FLOAT &&
                  IsOp(b, base, CKJIT_OP_SAMPLE_CMP_LEVEL_ZERO) && b.TypeOf(base) == CKJIT_TYPE_FLOAT,
              "comparisons are floats");
    TestCheck(b.Node(filtered).Imm[0] == 5 && b.Node(filtered).Imm[1] == CKJIT_SAMPLER_2D_COMPARE &&
                  b.Node(base).Imm[1] == CKJIT_SAMPLER_2D_COMPARE,
              "comparisons make their slot a 2D_COMPARE slot");
    TestCheck(b.TypeOf(depth) == CKJIT_TYPE_FLOAT4 && b.TypeOf(size) == CKJIT_TYPE_INT2 &&
                  b.TypeOf(levels) == CKJIT_TYPE_INT,
              "a compared slot is loaded from and queried as a 2D slot");
    TestCheck(Same(b.SampleCmp(5, uv, reference), filtered) && !Same(b.SampleCmpLevelZero(5, uv, reference), filtered),
              "equal comparisons are shared, the two kinds are not");
    TestCheck(!b.Failed(), "depth comparison is well typed");

    CKJitBuilder bad(4);
    const CKJitValue badUv = bad.Input(kTexCoord);
    const CKJitValue color = bad.Input(kColor);
    const CKJitValue depthRef = bad.Component(color, 0);
    TestCheck(!bad.SampleCmp(0, bad.Swizzle(color, "xyz"), depthRef).IsValid(), "comparison coordinates are float2");
    TestCheck(!bad.SampleCmpLevelZero(0, badUv, bad.Swizzle(color, "xy")).IsValid(), "references are floats");
    TestCheck(!bad.SampleCmp(0, badUv, bad.Int(0)).IsValid(), "integer references are rejected");
    TestCheck(!bad.Sample(0, CKJIT_SAMPLER_2D_COMPARE, badUv, bad.Float(0.0f)).IsValid() &&
                  !bad.SampleLevel(0, CKJIT_SAMPLER_2D_COMPARE, badUv, bad.Float(0.0f)).IsValid() &&
                  !bad.SampleGrad(0, CKJIT_SAMPLER_2D_COMPARE, badUv, badUv, badUv).IsValid() &&
                  !bad.CalcLod(0, CKJIT_SAMPLER_2D_COMPARE, badUv).IsValid(),
              "only comparisons sample a 2D_COMPARE slot");
    TestCheck(bad.Failed(), "ill-typed comparisons fail the builder");

    CKJitBuilder slots(4);
    const CKJitValue slotUv = slots.Input(kTexCoord);
    const CKJitValue colour = slots.Sample(0, CKJIT_SAMPLER_2D, slotUv, slots.Float(0.0f));
    const CKJitValue shadow = slots.SampleCmp(1, slotUv, slots.Float(0.5f));
    TestCheck(colour.IsValid() && shadow.IsValid() && !slots.SampleCmp(0, slotUv, slots.Float(0.5f)).IsValid() &&
                  !slots.TextureSize(1, CKJIT_SAMPLER_2D, slots.Int(0)).IsValid(),
              "a colour slot is not compared, a compared slot not read as a colour one");

    CKJitFragmentShader shader;
    const CKJitValue shade = b.Construct({filtered, base, b.IntToFloat(b.IntAdd(b.Component(size, 0), levels))});
    TestCheck(b.Finish(b.Mul(b.Add(sampled, depth), b.Construct({shade, b.Float(1.0f)})),
                       b.Less(filtered, b.Float(0.5f)), shader) &&
                  CKJitVerify(shader),
              "depth comparison verifies");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { Redimension(s, 5, CKJIT_SAMPLER_2D); }),
              "comparisons read only 2D_COMPARE slots");
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &s) { Redimension(s, 1, CKJIT_SAMPLER_2D_COMPARE); }),
              "samples do not read 2D_COMPARE slots");
    TestCheck(!VerifiesAfter(shader,
                             [&](CKJitFragmentShader &s) {
                                 s.Nodes[FindOp(s, CKJIT_OP_LOAD)].Imm[1] = CKJIT_SAMPLER_2D;
                             }),
              "a compared slot is loaded from as a 2D_COMPARE slot");

    CKJitBuilder listed(4);
    const CKJitValue coordinate = listed.Input(kTexCoord);
    const CKJitValue threshold = listed.Float(0.25f);
    const CKJitValue lit = listed.SampleCmpLevelZero(5, coordinate, threshold);
    CKJitFragmentShader listing;
    TestCheck(listed.Finish(listed.Splat(lit, 4), CKJitValue(), listing), "the comparison finishes");
    const char *expected = "%0 = INPUT float2 location4\n"
                           "%1 = CONSTANT float (0.25)\n"
                           "%2 = SAMPLE_CMP_LEVEL_ZERO float %0, %1 slot 5 dim 3\n"
                           "%3 = SWIZZLE float4 %2 .xxxx\n"
                           "color %3\n";
    TestCheck(std::strcmp(CKJitDump(listing).CStr(), expected) == 0, "the listing names comparisons");
}

void TestIfRegions() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Input(kColor);
    const CKJitValue tint = b.Uniform(1);
    const CKJitValue scaled = b.Mul(color, tint);
    const CKJitValue alpha = b.Component(color, 3);
    b.If(b.Less(alpha, b.Float(0.5f)));
    const CKJitValue lit = b.Mul(scaled, b.Uniform(2));
    TestCheck(Same(b.Mul(b.Uniform(2), scaled), lit) && Same(b.Mul(tint, color), scaled),
              "an arm shares its values and those before the region");
    b.Else({lit, scaled, lit});
    const CKJitValue unlit = b.Mul(scaled, b.Uniform(2));
    TestCheck(unlit.IsValid() && !Same(unlit, lit), "an arm does not share the values of the one before it");
    CKJitValue results[3];
    b.EndIf({unlit, color, unlit}, results);
    TestCheck(IsOp(b, results[0], CKJIT_OP_PHI) && b.TypeOf(results[0]) == CKJIT_TYPE_FLOAT4,
              "a result an arm computes is a PHI");
    TestCheck(IsOp(b, results[1], CKJIT_OP_SELECT), "a result from before the region is a select");
    TestCheck(Same(results[2], results[0]), "equal results share their PHI");
    const CKJitValue after = b.Mul(scaled, b.Uniform(2));
    TestCheck(after.IsValid() && !Same(after, lit) && !Same(after, unlit), "a closed arm's values are not shared");

    b.If(b.Less(b.Component(results[0], 0), alpha));
    const CKJitValue outer = b.Add(results[0], tint);
    b.If(b.Less(b.Component(outer, 1), alpha));
    const CKJitValue inner = b.Sub(outer, color);
    b.Else({inner});
    const CKJitValue nested = b.EndIf(outer);
    TestCheck(IsOp(b, nested, CKJIT_OP_PHI), "an enclosed region sees its enclosing arm");
    b.Else({nested});
    const CKJitValue shade = b.EndIf(results[1]);
    TestCheck(IsOp(b, shade, CKJIT_OP_PHI) && !b.Failed(), "regions nest");

    CKJitFragmentShader shader;
    TestCheck(b.Finish(shade, b.Less(b.Component(shade, 3), alpha), shader) && CKJitVerify(shader), "regions verify");
    const int region = FindOp(shader, CKJIT_OP_IF);
    bool leavesFirst = region > 0;
    for (int i = region; i < shader.Nodes.Size() && leavesFirst; ++i) {
        const CKJitOp op = shader.Nodes[i].Op;
        leavesFirst = op != CKJIT_OP_CONSTANT && op != CKJIT_OP_INPUT && op != CKJIT_OP_UNIFORM;
    }
    TestCheck(leavesFirst, "the leaves precede every region");

    CKJitBuilder folded(4);
    const CKJitValue x = folded.Input(kColor);
    folded.If(folded.Bool(true));
    const CKJitValue taken = folded.Mul(x, folded.Uniform(0));
    folded.Else({taken});
    const CKJitValue skipped = folded.Add(x, folded.Uniform(0));
    TestCheck(Same(folded.EndIf(skipped), taken), "a true condition keeps the then results");
    TestCheck(Same(folded.Mul(x, folded.Uniform(0)), taken), "a constant condition's arms are built around it");
    folded.If(folded.Less(folded.Float(1.0f), folded.Float(0.0f)));
    folded.Else({taken});
    TestCheck(Same(folded.EndIf(skipped), skipped), "a folded false condition keeps the else results");
    CKJitFragmentShader flat;
    TestCheck(folded.Finish(folded.Add(taken, skipped), CKJitValue(), flat) && FindOp(flat, CKJIT_OP_IF) < 0 &&
                  FindOp(flat, CKJIT_OP_PHI) < 0,
              "constant conditions leave no region");

    CKJitBuilder scoped(4);
    const CKJitValue y = scoped.Input(kColor);
    scoped.If(scoped.Less(scoped.Component(y, 0), scoped.Float(0.5f)));
    const CKJitValue squared = scoped.Mul(y, y);
    scoped.Else({squared});
    TestCheck(!scoped.Add(squared, y).IsValid() && scoped.Failed(), "an arm's values are not seen after it");

    CKJitBuilder condition(4);
    condition.If(condition.Component(condition.Input(kColor), 0));
    TestCheck(condition.Failed(), "conditions are booleans");

    CKJitBuilder typed(4);
    const CKJitValue z = typed.Input(kColor);
    typed.If(typed.Less(typed.Component(z, 0), typed.Float(0.5f)));
    typed.Else({typed.Mul(z, z)});
    TestCheck(!typed.EndIf(typed.Component(z, 1)).IsValid() && typed.Failed(), "paired results share a type");

    CKJitBuilder counted(4);
    const CKJitValue w = counted.Input(kColor);
    counted.If(counted.Less(counted.Component(w, 0), counted.Float(0.5f)));
    counted.Else({counted.Mul(w, w), w});
    TestCheck(!counted.EndIf(w).IsValid() && counted.Failed(), "every then result has an else one");

    CKJitBuilder open(4);
    const CKJitValue v = open.Input(kColor);
    open.If(open.Less(open.Component(v, 0), open.Float(0.5f)));
    CKJitFragmentShader unfinished;
    TestCheck(!open.Finish(v, CKJitValue(), unfinished), "an open region does not finish");
    TestCheck(!open.EndIf(v).IsValid() && open.Failed(), "a region ends after its else arm");

    CKJitBuilder stray(4);
    stray.Else({});
    TestCheck(stray.Failed(), "an else needs its region");
    CKJitBuilder strayEnd(4);
    TestCheck(!strayEnd.EndIf(strayEnd.Float(0.0f)).IsValid() && strayEnd.Failed(), "an end needs its region");

    // Argument evaluation order is unspecified: create the nodes one by one.
    CKJitBuilder listed(4);
    const CKJitValue uv = listed.Input(kTexCoord);
    const CKJitValue u = listed.Component(uv, 0);
    const CKJitValue threshold = listed.Float(0.5f);
    listed.If(listed.Less(u, threshold));
    const CKJitValue square = listed.Mul(u, u);
    listed.Else({square});
    const CKJitValue root = listed.Sqrt(u);
    const CKJitValue magnitude = listed.EndIf(root);
    CKJitFragmentShader listing;
    TestCheck(listed.Finish(listed.Splat(magnitude, 4), CKJitValue(), listing) && CKJitVerify(listing),
              "the region finishes");
    const char *expected = "%0 = INPUT float2 location4\n"
                           "%1 = CONSTANT float (0.5)\n"
                           "%2 = SWIZZLE float %0 .x\n"
                           "%3 = LT bool %2, %1\n"
                           "%4 = IF void %3\n"
                           "  %5 = MUL float %2, %2\n"
                           "%6 = ELSE void %4\n"
                           "  %7 = SQRT float %2\n"
                           "%8 = ENDIF void %6\n"
                           "%9 = PHI float %5, %7, %8\n"
                           "%10 = SWIZZLE float4 %9 .xxxx\n"
                           "color %10\n";
    TestCheck(std::strcmp(CKJitDump(listing).CStr(), expected) == 0, "the listing indents arms under their markers");
    if (listing.Nodes.Size() != 11)
        return;

    // Each corruption is one a backend would emit unstructured or undefined
    // code for.
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[10].Operands[0] = 5; }),
              "a closed arm's values are not read");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[7].Operands[0] = 5; }),
              "an arm does not read the one before it");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[10].Operands[0] = 8; }),
              "markers are not values");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[4].Operands[0] = 2; }),
              "a condition is a boolean");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[4].Type = CKJIT_TYPE_BOOL; }),
              "markers are void");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[5].Type = CKJIT_TYPE_VOID; }),
              "values are not void");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[6].Operands[0] = 3; }),
              "an else follows its region's then arm");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[8].Operands[0] = 4; }),
              "an end follows its region's else arm");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[9].Operands[2] = 6; }),
              "a PHI follows its region's end");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &s) { s.Nodes[9].Operands[0] = 3; }),
              "PHI arms have the result's type");
    TestCheck(!VerifiesAfter(listing,
                             [&](CKJitFragmentShader &s) {
                                 s.Nodes[9].Operands[0] = 7;
                                 s.Nodes[9].Operands[1] = 5;
                             }),
              "a PHI takes each result from its arm");
    TestCheck(!VerifiesAfter(listing,
                             [&](CKJitFragmentShader &s) {
                                 s.Nodes.Resize(8);
                                 s.Nodes[7].Op = CKJIT_OP_SWIZZLE;
                                 s.Nodes[7].Type = CKJIT_TYPE_FLOAT4;
                                 s.Nodes[7].Operands[0] = 0;
                                 s.Nodes[7].Imm[2] = 0;
                                 s.Nodes[7].Imm[3] = 1;
                                 s.Color.Id = 7;
                             }),
              "regions end and the outputs are after them");
}

int CountOps(const CKJitFragmentShader &shader, CKJitOp op) {
    int count = 0;
    for (int i = 0; i < shader.Nodes.Size(); ++i)
        count += shader.Nodes[i].Op == op ? 1 : 0;
    return count;
}

// The loop a carried value heads.
CKJitValue LoopOf(const CKJitBuilder &b, CKJitValue carried) {
    return CKJitValue{b.Node(carried).Operands[1]};
}

void TestLoops() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Input(kColor);
    const CKJitValue tint = b.Uniform(1);
    const CKJitValue scaled = b.Mul(color, tint);
    const CKJitValue red = b.Component(tint, 0);
    const CKJitValue taps = b.IntMin(b.FloatToInt(b.Mul(b.Component(color, 0), b.Float(8.0f))), b.Int(16));
    CKJitValue carried[3];
    const CKJitValue index = b.Loop(taps, 16, {scaled, red, red}, carried);
    TestCheck(IsOp(b, index, CKJIT_OP_INDEX) && b.TypeOf(index) == CKJIT_TYPE_INT, "a loop's index is an INT");
    TestCheck(IsOp(b, carried[0], CKJIT_OP_CARRY) && b.TypeOf(carried[0]) == CKJIT_TYPE_FLOAT4 &&
                  IsOp(b, carried[1], CKJIT_OP_CARRY) && IsOp(b, carried[2], CKJIT_OP_CARRY) &&
                  !Same(carried[1], carried[2]),
              "carried values have their initials' types and are never shared");
    TestCheck(b.Node(LoopOf(b, carried[0])).Imm[0] == 16, "a loop keeps its bound");
    const CKJitValue lit = b.Mul(scaled, b.Uniform(2));
    TestCheck(Same(b.Mul(b.Uniform(2), scaled), lit) && Same(b.Mul(tint, color), scaled),
              "a body shares its values and those before the loop");
    const CKJitValue weight = b.Mul(carried[1], b.Component(tint, 1));
    const CKJitValue sum = b.Add(carried[0], b.Mul(lit, b.Splat(b.Mul(weight, b.IntToFloat(index)), 4)));
    CKJitValue inner;
    b.Loop(b.IntAdd(index, b.Int(1)), 4, {weight}, &inner);
    const CKJitValue halved = b.EndLoop(b.Mul(inner, b.Float(0.5f)));
    TestCheck(IsOp(b, halved, CKJIT_OP_RESULT) && !b.Failed(), "loops nest");
    CKJitValue results[3];
    b.EndLoop({sum, halved, b.Component(tint, 2)}, results);
    TestCheck(IsOp(b, results[0], CKJIT_OP_RESULT) && b.TypeOf(results[0]) == CKJIT_TYPE_FLOAT4 &&
                  IsOp(b, results[1], CKJIT_OP_RESULT) && IsOp(b, results[2], CKJIT_OP_RESULT),
              "every carried value has its RESULT, of a next from the body or before the loop");
    const CKJitValue after = b.Mul(scaled, b.Uniform(2));
    TestCheck(after.IsValid() && !Same(after, lit), "a closed body's values are not shared");

    CKJitFragmentShader shader;
    TestCheck(b.Finish(b.Add(results[0], b.Splat(results[1], 4)), CKJitValue(), shader) && CKJitVerify(shader),
              "loops verify");
    TestCheck(CountOps(shader, CKJIT_OP_LOOP) == 2 && CountOps(shader, CKJIT_OP_RESULT) == 4,
              "a kept loop keeps every carried value's RESULT");

    CKJitBuilder folded(4);
    const CKJitValue x = folded.Input(kColor);
    CKJitValue kept;
    TestCheck(Same(folded.Loop(folded.Int(0), 8, {x}, &kept), folded.Int(0)) && Same(kept, x),
              "a constant count of at most one iteration carries the initials");
    const CKJitValue skipped = folded.Mul(kept, folded.Uniform(0));
    TestCheck(Same(folded.EndLoop(skipped), x), "a loop running no iteration keeps the initials");
    TestCheck(Same(folded.Mul(x, folded.Uniform(0)), skipped), "a flattened body is built around the loop");
    folded.Loop(folded.Int(5), 1, {x}, &kept);
    const CKJitValue once = folded.Add(kept, folded.Uniform(1));
    TestCheck(Same(folded.EndLoop(once), once), "a single iteration keeps the nexts, also when the bound cuts the count");
    folded.Loop(folded.Int(-3), 8, {x}, &kept);
    TestCheck(Same(folded.EndLoop(once), x), "a negative count runs no iteration");
    CKJitFragmentShader flat;
    TestCheck(folded.Finish(folded.Add(skipped, once), CKJitValue(), flat) && FindOp(flat, CKJIT_OP_LOOP) < 0,
              "constant counts of at most one iteration leave no loop");

    CKJitBuilder clamped(4);
    const CKJitValue y = clamped.Input(kColor);
    CKJitValue twice;
    clamped.Loop(clamped.Int(3), 2, {y}, &twice);
    const CKJitValue loop = LoopOf(clamped, twice);
    TestCheck(IsOp(clamped, loop, CKJIT_OP_LOOP) && Same(CKJitValue{clamped.Node(loop).Operands[0]}, clamped.Int(2)),
              "a constant count takes the bound's place when that is lower");
    clamped.EndLoop(clamped.Add(twice, y));
    CKJitValue runtime;
    clamped.Loop(clamped.FloatToInt(clamped.Component(y, 0)), 1, {y}, &runtime);
    TestCheck(IsOp(clamped, runtime, CKJIT_OP_CARRY), "a runtime count keeps its loop with a bound of one");
    TestCheck(IsOp(clamped, clamped.EndLoop(clamped.Mul(runtime, y)), CKJIT_OP_RESULT) && !clamped.Failed(),
              "loops follow one another");

    CKJitBuilder counted(4);
    CKJitValue value;
    counted.Loop(counted.Float(2.0f), 4, {counted.Input(kColor)}, &value);
    TestCheck(counted.Failed() && !value.IsValid(), "a count is an INT");
    CKJitBuilder bounded(4);
    bounded.Loop(bounded.Int(2), 0, {}, nullptr);
    CKJitBuilder unbounded(4);
    unbounded.Loop(unbounded.Int(2), 0x80000000u, {}, nullptr);
    TestCheck(bounded.Failed() && unbounded.Failed(), "a bound is 1..2^31 - 1");
    CKJitBuilder initial(4);
    initial.Loop(initial.Int(2), 4, {CKJitValue()}, &value);
    TestCheck(initial.Failed(), "initials are valid");

    CKJitBuilder typed(4);
    const CKJitValue z = typed.Input(kColor);
    typed.Loop(typed.FloatToInt(typed.Component(z, 0)), 4, {z}, &value);
    TestCheck(!typed.EndLoop(typed.Component(value, 0)).IsValid() && typed.Failed(),
              "a next has its carried value's type");
    CKJitBuilder paired(4);
    const CKJitValue w = paired.Input(kColor);
    CKJitValue pair[2];
    paired.Loop(paired.FloatToInt(paired.Component(w, 0)), 4, {w, w}, pair);
    TestCheck(!paired.EndLoop(pair[0]).IsValid() && paired.Failed(), "every carried value has a next");

    CKJitBuilder scoped(4);
    const CKJitValue v = scoped.Input(kColor);
    scoped.Loop(scoped.FloatToInt(scoped.Component(v, 0)), 4, {v}, &value);
    const CKJitValue squared = scoped.Mul(value, value);
    scoped.EndLoop(squared);
    TestCheck(!scoped.Add(squared, v).IsValid() && scoped.Failed(), "a body's values are not seen after it");

    CKJitBuilder elsewise(4);
    elsewise.Loop(elsewise.FloatToInt(elsewise.Component(elsewise.Input(kColor), 0)), 4, {}, nullptr);
    elsewise.Else({});
    CKJitBuilder endif(4);
    endif.Loop(endif.FloatToInt(endif.Component(endif.Input(kColor), 0)), 4, {}, nullptr);
    TestCheck(!endif.EndIf(endif.Float(0.0f)).IsValid() && endif.Failed() && elsewise.Failed(),
              "a loop has no else and ends with an ENDLOOP");
    CKJitBuilder crossed(4);
    crossed.If(crossed.Less(crossed.Component(crossed.Input(kColor), 0), crossed.Float(0.5f)));
    TestCheck(!crossed.EndLoop(crossed.Float(0.0f)).IsValid() && crossed.Failed(), "an ENDLOOP needs its loop");
    CKJitBuilder open(4);
    const CKJitValue u = open.Input(kColor);
    open.Loop(open.FloatToInt(open.Component(u, 0)), 4, {}, nullptr);
    CKJitFragmentShader unfinished;
    TestCheck(!open.Finish(u, CKJitValue(), unfinished), "an open loop does not finish");

    // Argument evaluation order is unspecified: create the nodes one by one.
    CKJitBuilder listed(4);
    const CKJitValue uv = listed.Input(kTexCoord);
    const CKJitValue s = listed.Component(uv, 0);
    const CKJitValue steps = listed.FloatToInt(s);
    CKJitValue total;
    const CKJitValue i = listed.Loop(steps, 8, {s}, &total);
    const CKJitValue offset = listed.IntToFloat(i);
    const CKJitValue next = listed.Add(total, offset);
    const CKJitValue result = listed.EndLoop(next);
    CKJitFragmentShader listing;
    TestCheck(listed.Finish(listed.Splat(result, 4), CKJitValue(), listing) && CKJitVerify(listing),
              "the loop finishes");
    const char *expected = "%0 = INPUT float2 location4\n"
                           "%1 = SWIZZLE float %0 .x\n"
                           "%2 = FTOI int %1\n"
                           "%3 = LOOP void %2 bound 8\n"
                           "  %4 = INDEX int %3\n"
                           "  %5 = CARRY float %1, %3\n"
                           "  %6 = ITOF float %4\n"
                           "  %7 = ADD float %5, %6\n"
                           "%8 = ENDLOOP void %3\n"
                           "%9 = RESULT float %5, %7, %8\n"
                           "%10 = SWIZZLE float4 %9 .xxxx\n"
                           "color %10\n";
    TestCheck(std::strcmp(CKJitDump(listing).CStr(), expected) == 0, "the listing indents bodies under their loops");
    if (listing.Nodes.Size() != 11)
        return;

    // Each corruption is one a backend would emit unstructured or undefined
    // code for.
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[3].Operands[0] = 1; }),
              "a count is an INT");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[3].Imm[0] = 0; }) &&
                  !VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[3].Imm[0] = 0x80000000u; }),
              "a bound is 1..2^31 - 1");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[4].Operands[0] = 2; }),
              "an index directly follows its loop");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[4].Type = CKJIT_TYPE_FLOAT; }),
              "an index is an integer");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[5].Operands[0] = 2; }),
              "a carry has its initial value's type");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[9].Operands[1] = 4; }),
              "the next iteration preserves the carried type");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[5].Operands[1] = 2; }),
              "a carried value is of the loop being checked");
    TestCheck(!VerifiesAfter(listing,
                             [&](CKJitFragmentShader &c) {
                                 const CKJitNode carry = c.Nodes[5];
                                 c.Nodes[5] = c.Nodes[6];
                                 c.Nodes[6] = carry;
                                 c.Nodes[7].Operands[0] = 5;
                                 c.Nodes[7].Operands[1] = 6;
                                 c.Nodes[9].Operands[0] = 6;
                             }),
              "carried values head their loop's body");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[5].Operands[0] = 4; }),
              "an initial is from before its loop");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[10].Operands[0] = 7; }),
              "a closed body's values are not read");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[8].Op = CKJIT_OP_ELSE; }) &&
                  !VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[8].Op = CKJIT_OP_ENDIF; }),
              "a loop's body ends with an ENDLOOP");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[9].Operands[2] = 3; }),
              "a RESULT follows its loop's end");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[9].Operands[0] = 7; }),
              "a RESULT binds its loop's carried values in order");
    TestCheck(!VerifiesAfter(listing, [&](CKJitFragmentShader &c) { c.Nodes[9].Operands[1] = 8; }),
              "a next is a value");
    TestCheck(!VerifiesAfter(listing,
                             [&](CKJitFragmentShader &c) {
                                 c.Nodes[9] = c.Nodes[10];
                                 c.Nodes[9].Operands[0] = 1;
                                 c.Nodes.Resize(10);
                                 c.Color.Id = 9;
                             }),
              "every carried value's RESULT directly follows its loop's end");
    TestCheck(!VerifiesAfter(listing,
                             [&](CKJitFragmentShader &c) {
                                 const CKJitNode color = c.Nodes[10];
                                 c.Nodes[10] = c.Nodes[9];
                                 c.Nodes.PushBack(color);
                                 c.Color.Id = 11;
                             }),
              "a loop has one RESULT for each carried value");
    TestCheck(!VerifiesAfter(listing,
                             [&](CKJitFragmentShader &c) {
                                 c.Nodes[8] = c.Nodes[10];
                                 c.Nodes[8].Operands[0] = 1;
                                 c.Nodes.Resize(9);
                                 c.Color.Id = 8;
                             }),
              "loops end and the outputs are after them");
}

// Whether a builder takes a quad operation of a texture coordinate where a
// callable leaves control flow.
template <typename Flow, typename Operation>
bool TakesQuadOp(Flow flow, Operation operation) {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord);
    flow(b);
    return operation(b, uv).IsValid() && !b.Failed();
}

void TestUniformity() {
    const auto uniformIf = [](CKJitBuilder &b) { b.If(b.Less(b.Component(b.Uniform(0), 0), b.Float(0.5f))); };
    const auto varyingIf = [](CKJitBuilder &b) { b.If(b.Less(b.Component(b.Input(kColor), 0), b.Float(0.5f))); };
    const auto nestedIf = [&](CKJitBuilder &b) {
        varyingIf(b);
        uniformIf(b);
    };
    const auto closedIf = [&](CKJitBuilder &b) {
        varyingIf(b);
        b.Else({});
        b.EndIf({}, nullptr);
    };
    const auto uniformLoop = [](CKJitBuilder &b) {
        b.Loop(b.FloatToInt(b.Component(b.Uniform(0), 1)), 4, {}, nullptr);
    };
    const auto varyingLoop = [](CKJitBuilder &b) {
        b.Loop(b.FloatToInt(b.Component(b.Input(kColor), 1)), 4, {}, nullptr);
    };
    const auto indexIf = [](CKJitBuilder &b) {
        const CKJitValue index = b.Loop(b.FloatToInt(b.Component(b.Uniform(0), 1)), 4, {}, nullptr);
        b.If(b.IntLess(index, b.Int(2)));
    };
    const auto carriedIf = [](CKJitBuilder &b) {
        CKJitValue carried;
        b.Loop(b.Int(4), 4, {b.Float(1.0f)}, &carried);
        b.If(b.Less(carried, b.Float(0.5f)));
    };
    const auto ddx = [](CKJitBuilder &b, CKJitValue uv) { return b.Ddx(uv); };
    const auto ddy = [](CKJitBuilder &b, CKJitValue uv) { return b.Ddy(uv); };
    const auto sample = [](CKJitBuilder &b, CKJitValue uv) { return b.Sample(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f)); };
    const auto lod = [](CKJitBuilder &b, CKJitValue uv) { return b.CalcLod(0, CKJIT_SAMPLER_2D, uv); };
    const auto compare = [](CKJitBuilder &b, CKJitValue uv) { return b.SampleCmp(0, uv, b.Float(0.5f)); };
    const auto level = [](CKJitBuilder &b, CKJitValue uv) {
        return b.SampleLevel(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    };
    const auto graded = [](CKJitBuilder &b, CKJitValue uv) { return b.SampleGrad(0, CKJIT_SAMPLER_2D, uv, uv, uv); };
    const auto zero = [](CKJitBuilder &b, CKJitValue uv) { return b.SampleCmpLevelZero(0, uv, b.Float(0.5f)); };

    TestCheck(TakesQuadOp(uniformIf, ddx) && TakesQuadOp(uniformLoop, sample) && TakesQuadOp(closedIf, lod) &&
                  TakesQuadOp(indexIf, compare),
              "quad operations run in uniform control flow");
    TestCheck(!TakesQuadOp(varyingIf, ddx) && !TakesQuadOp(varyingIf, ddy) && !TakesQuadOp(varyingLoop, sample) &&
                  !TakesQuadOp(varyingLoop, lod) && !TakesQuadOp(varyingIf, compare),
              "quad operations do not run where control flow diverges");
    TestCheck(!TakesQuadOp(nestedIf, ddx) && !TakesQuadOp(carriedIf, ddx),
              "flow diverges within divergent flow and on carried values");
    TestCheck(TakesQuadOp(varyingLoop, level) && TakesQuadOp(varyingIf, graded) && TakesQuadOp(varyingIf, zero),
              "explicit LODs and gradients run anywhere");

    CKJitBuilder hoisted(4);
    const CKJitValue coordinate = hoisted.Input(kTexCoord);
    const CKJitValue slope = hoisted.Ddx(coordinate);
    varyingIf(hoisted);
    TestCheck(Same(hoisted.Ddx(coordinate), slope) && !hoisted.Failed(),
              "a derivative from before divergent flow is shared in it");
    CKJitBuilder carried(4);
    CKJitValue value;
    carried.Loop(carried.Int(4), 4, {carried.Input(kTexCoord)}, &value);
    TestCheck(carried.Ddx(value).IsValid() && !carried.Failed(),
              "a uniform loop's body takes derivatives of its carried values");

    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord);
    const CKJitValue color = b.Input(kColor);
    const CKJitValue varying = b.Less(b.Component(color, 0), b.Float(0.5f));
    const CKJitValue steps = b.FloatToInt(b.Component(color, 1));
    uniformIf(b);
    const CKJitValue derived = b.Add(uv, b.Ddx(uv));
    b.Else({derived});
    const CKJitValue coordinates = b.EndIf(uv);
    CKJitValue sum;
    b.Loop(b.FloatToInt(b.Component(b.Uniform(0), 1)), 4, {b.IntToFloat(steps)}, &sum);
    const CKJitValue sampled = b.Sample(0, CKJIT_SAMPLER_2D, coordinates, b.Float(0.0f));
    const CKJitValue total = b.EndLoop(b.Add(sum, b.Component(sampled, 0)));
    CKJitFragmentShader shader;
    TestCheck(b.Finish(b.Construct({coordinates, b.Splat(total, 2)}), varying, shader) && CKJitVerify(shader),
              "quad operations in uniform flow verify");
    const int region = FindOp(shader, CKJIT_OP_IF);
    const int loop = FindOp(shader, CKJIT_OP_LOOP);
    const int converted = FindOp(shader, CKJIT_OP_ITOF);
    TestCheck(region >= 0 && loop >= 0 && converted >= 0, "the program has the nodes to corrupt");
    if (region < 0 || loop < 0 || converted < 0)
        return;
    TestCheck(!VerifiesAfter(shader, [&](CKJitFragmentShader &c) { c.Nodes[region].Operands[0] = c.Discard.Id; }),
              "derivatives do not run under a varying condition");
    TestCheck(!VerifiesAfter(shader,
                             [&](CKJitFragmentShader &c) {
                                 c.Nodes[loop].Operands[0] = c.Nodes[converted].Operands[0];
                             }),
              "implicit-LOD samples do not run for a varying count");
}

void TestVertexShaders() {
    for (unsigned i = 0; i < sizeof(kVertexCases) / sizeof(kVertexCases[0]); ++i) {
        CKJitVertexShader shader;
        TestCheck(BuildVertexCase(i, shader) && CKJitVerify(shader), kVertexCases[i]);
        TestCheck(std::strstr(CKJitDump(shader).CStr(), "position %") != nullptr, "vertex dumps name position");
    }

    CKJitBuilder clipBuilder(1);
    const CKJitValue clipPosition = clipBuilder.Input({0, 4, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue distance = clipBuilder.Dot(clipPosition, clipBuilder.Uniform(0));
    CKJitVertexShader clipShader;
    TestCheck(clipBuilder.FinishVertex(clipPosition, {}, clipShader, {distance}), "clip-only dependencies survive DCE");
    TestCheck(clipShader.ClipDistances.Size() == 1 && clipShader.Node(clipShader.ClipDistances[0]).Op == CKJIT_OP_DOT,
              "clip output retains its scalar expression");
    TestCheck(std::strstr(CKJitDump(clipShader).CStr(), "clipdistance 0 %") != nullptr, "dump identifies clip distances");
    CKJitVertexShader badClip = clipShader;
    badClip.ClipDistances[0] = badClip.Position;
    TestCheck(!CKJitVerify(badClip), "raw clip distances must be scalar floats");
    badClip.ClipDistances[0].Id = badClip.Nodes.Size();
    TestCheck(!CKJitVerify(badClip), "raw clip references must exist");
    badClip = clipShader;
    while (badClip.ClipDistances.Size() < 9) badClip.ClipDistances.PushBack(clipShader.ClipDistances[0]);
    TestCheck(!CKJitVerify(badClip), "raw clip arrays are limited to eight");
    TestCheck(!clipBuilder.FinishVertex(clipPosition, {}, clipShader, {clipPosition}), "builder rejects vector clip outputs");
    TestCheck(!clipBuilder.FinishVertex(clipPosition, {}, clipShader, {clipBuilder.Int(1)}), "builder rejects integer distances");
    TestCheck(!clipBuilder.FinishVertex(clipPosition, nullptr, 0, clipShader, nullptr, 1), "missing clip array fails");
    CKJitValue nine[9]; for (auto &value : nine) value = distance;
    TestCheck(!clipBuilder.FinishVertex(clipPosition, nullptr, 0, clipShader, nine, 9), "builder limits clip count");
    clipBuilder.If(clipBuilder.Less(distance, clipBuilder.Float(0)));
    const CKJitValue inner = clipBuilder.Mul(distance, clipBuilder.Float(2));
    clipBuilder.Else({inner});
    const CKJitValue outer = clipBuilder.EndIf(distance);
    TestCheck(!clipBuilder.FinishVertex(clipPosition, {}, clipShader, {inner}), "clip output cannot escape its arm");
    TestCheck(clipBuilder.FinishVertex(clipPosition, {}, clipShader, {outer}), "joined clip distance is valid");
    TestCheck(clipBuilder.FinishVertex(clipPosition, {}, clipShader) && clipShader.ClipDistances.Size() == 0,
              "reusing a shader clears its previous clip interface");

    CKJitBuilder typed(0);
    const CKJitValue uintInput = typed.Input({6, 4, CKJIT_INPUT_ATTRIBUTE, CKJIT_INPUT_UINT});
    CKJitVertexShader uintShader;
    TestCheck(typed.FinishVertex(typed.IntToFloat(uintInput), {}, uintShader), "uint attribute enters signed integer IR");
    CKJitVertexShader wrongScalar = uintShader;
    wrongScalar.Inputs[0].Scalar = CKJIT_INPUT_FLOAT;
    TestCheck(!CKJitVerify(wrongScalar), "input declaration and node scalar types must agree");
    wrongScalar.Inputs[0].Scalar = CKJitInputScalar(2);
    TestCheck(!CKJitVerify(wrongScalar), "unknown input scalar is rejected");
    TestCheck(!typed.Input({6, 4, CKJIT_INPUT_ATTRIBUTE}).IsValid(), "same location cannot change scalar type");
    CKJitBuilder badScalar(0);
    TestCheck(!badScalar.Input({1, 4, CKJIT_INPUT_FLAT, CKJIT_INPUT_UINT}).IsValid(), "uint fragment inputs are unsupported");

    CKJitBuilder b(4);
    const CKJitValue position = b.Input({0, 4, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue color = b.Mul(position, b.Uniform(1));
    b.Mul(position, b.Float(17.0f)); // unreachable
    CKJitVertexShader shader;
    TestCheck(b.FinishVertex(position, {{0, CKJIT_INPUT_FLAT, color}}, shader), "vertex outputs finish");
    TestCheck(shader.Nodes.Size() == 3 && shader.Node(shader.Outputs[0].Value).Op == CKJIT_OP_MUL,
              "DCE keeps varying-only dependencies and removes unused arithmetic");
    TestCheck(std::strstr(CKJitDump(shader).CStr(), "output location0 flat %") != nullptr,
              "vertex dumps include the varying contract");
    CKJitFragmentShader fragment;
    TestCheck(!b.Finish(position, CKJitValue(), fragment), "fragment programs reject vertex attributes");
    TestCheck(!b.FinishVertex(b.Swizzle(position, "xyz"), {}, shader), "position must have four components");
    TestCheck(!b.FinishVertex(position, {{0, CKJIT_INPUT_SMOOTH, b.Int(1)}}, shader), "varyings must be floats");
    TestCheck(!b.FinishVertex(position, {{0, CKJIT_INPUT_SMOOTH, CKJitValue()}}, shader), "invalid outputs fail");
    TestCheck(!b.FinishVertex(position, nullptr, 1, shader), "missing declarations fail");
    TestCheck(!b.FinishVertex(position, {{0, CKJIT_INPUT_SMOOTH, color}, {0, CKJIT_INPUT_FLAT, color}}, shader),
              "varying output locations are unique");
    TestCheck(!b.FinishVertex(position, {{0, CKJIT_INPUT_ATTRIBUTE, color}}, shader), "outputs specify interpolation");

    TestCheck(b.FinishVertex(position, {{0, CKJIT_INPUT_SMOOTH, color}}, shader), "valid shader rebuilds");
    CKJitVertexShader corrupted = shader;
    corrupted.Position = CKJitValue();
    TestCheck(!CKJitVerify(corrupted), "raw vertex programs require a position");
    corrupted = shader;
    corrupted.Outputs[0].Value.Id = (uint32_t)corrupted.Nodes.Size();
    TestCheck(!CKJitVerify(corrupted), "raw vertex output references are checked");
    corrupted = shader;
    corrupted.Inputs.PushBack(corrupted.Inputs[0]);
    TestCheck(!CKJitVerify(corrupted), "raw vertex input locations are unique");
    corrupted = shader;
    corrupted.Inputs[0].Kind = CKJIT_INPUT_FRAG_COORD;
    TestCheck(!CKJitVerify(corrupted), "FragCoord is not a vertex attribute");

    CKJitBuilder scoped(0);
    const CKJitValue input = scoped.Input({0, 4, CKJIT_INPUT_ATTRIBUTE});
    scoped.If(scoped.Less(scoped.Component(input, 0), scoped.Float(0.5f)));
    const CKJitValue arm = scoped.Mul(input, scoped.Float(0.25f));
    scoped.Else({arm});
    const CKJitValue joined = scoped.EndIf(input);
    TestCheck(!scoped.FinishVertex(input, {{0, CKJIT_INPUT_SMOOTH, arm}}, shader), "arm values cannot escape to outputs");
    TestCheck(scoped.FinishVertex(input, {{0, CKJIT_INPUT_SMOOTH, joined}}, shader), "joined values can be outputs");
    for (int i = 0; i < shader.Nodes.Size(); ++i) {
        if (shader.Nodes[i].Op == CKJIT_OP_MUL) {
            corrupted = shader;
            corrupted.Outputs[0].Value.Id = (uint32_t)i;
            TestCheck(!CKJitVerify(corrupted), "raw outputs cannot escape an arm either");
        }
    }

    // Stage restrictions apply even in uniform flow, but dead operations do
    // not prevent finishing a program that never uses them.
    for (unsigned op = 0; op < 5; ++op) {
        CKJitBuilder quad(0);
        const CKJitValue p = quad.Input({0, 4, CKJIT_INPUT_ATTRIBUTE});
        const CKJitValue uv = quad.Swizzle(p, "xy");
        CKJitValue result;
        switch (op) {
        case 0: result = quad.Ddx(p); break;
        case 1: result = quad.Ddy(p); break;
        case 2: result = quad.Sample(0, CKJIT_SAMPLER_2D, uv, quad.Float(0.0f)); break;
        case 3: result = quad.CalcLod(0, CKJIT_SAMPLER_2D, uv); break;
        default: result = quad.SampleCmp(0, uv, quad.Float(0.5f)); break;
        }
        TestCheck(!quad.FinishVertex(p, {{0, CKJIT_INPUT_SMOOTH, result}}, shader), "vertex QUAD operations fail");
        TestCheck(quad.FinishVertex(p, {}, shader) && shader.SamplerCount == 0, "dead QUAD operations are removed");
    }
}

} // namespace

int main() {
    TestFramework framework;
    framework.Run("hash consing", TestHashConsing);
    framework.Run("constant folding", TestConstantFolding);
    framework.Run("native multiply-add", TestMad);
    framework.Run("rounding environment", TestRoundingEnvironment);
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
    framework.Run("verify operand types", TestVerifyOperandTypes);
    framework.Run("dump", TestDump);
    framework.Run("uniform buffers", TestUniformBuffers);
    framework.Run("texture access", TestTextureAccess);
    framework.Run("depth comparison", TestDepthComparison);
    framework.Run("if regions", TestIfRegions);
    framework.Run("loops", TestLoops);
    framework.Run("uniformity", TestUniformity);
    framework.Run("vertex shaders", TestVertexShaders);
    return framework.ExitCode();
}
