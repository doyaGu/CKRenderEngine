#include "CKJitBuilder.h"
#include "CKJitSpirv.h"
#include "TestTriangleMultiset.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

// Structural checks of the SPIR-V backend. With a directory argument every
// module is also written there as <name>.spv for spirv-val.

namespace {

// The SPIR-V numbers the checks look for.
enum {
    kMagic = 0x07230203,
    kOpExtInstImport = 11,
    kOpExtInst = 12,
    kOpMemoryModel = 14,
    kOpEntryPoint = 15,
    kOpExecutionMode = 16,
    kOpCapability = 17,
    kOpTypeVoid = 19,
    kOpTypeBool = 20,
    kOpTypeInt = 21,
    kOpTypeFloat = 22,
    kOpTypeFunction = 33,
    kOpTypeVector = 23,
    kOpTypeImage = 25,
    kOpTypeSampledImage = 27,
    kOpTypeArray = 28,
    kOpConstantTrue = 41,
    kOpConstant = 43,
    kOpConstantComposite = 44,
    kOpVariable = 59,
    kOpLoad = 61,
    kOpStore = 62,
    kOpAccessChain = 65,
    kOpDecorate = 71,
    kOpMemberDecorate = 72,
    kOpVectorShuffle = 79,
    kOpCompositeConstruct = 80,
    kOpCompositeExtract = 81,
    kOpImageSampleImplicitLod = 87,
    kOpImageSampleExplicitLod = 88,
    kOpImageSampleDrefImplicitLod = 89,
    kOpImageSampleDrefExplicitLod = 90,
    kOpImageFetch = 95,
    kOpImage = 100,
    kOpImageQuerySizeLod = 103,
    kOpImageQueryLod = 105,
    kOpImageQueryLevels = 106,
    kOpConvertFToS = 110,
    kOpConvertSToF = 111,
    kOpIAdd = 128,
    kOpISub = 130,
    kOpIMul = 132,
    kOpSRem = 138,
    kOpSMod = 139,
    kOpAny = 154,
    kOpAll = 155,
    kOpSelect = 169,
    kOpINotEqual = 171,
    kOpSLessThan = 177,
    kOpSLessThanEqual = 179,
    kOpFOrdLessThan = 184,
    kOpShiftRightArithmetic = 195,
    kOpBitwiseXor = 198,
    kOpBitwiseAnd = 199,
    kOpDPdx = 207,
    kOpDPdy = 208,
    kOpPhi = 245,
    kOpLoopMerge = 246,
    kOpSelectionMerge = 247,
    kOpLabel = 248,
    kOpBranch = 249,
    kOpBranchConditional = 250,
    kOpKill = 252,
    kOpReturn = 253,
    kOpFunctionEnd = 56,

    kDecorationBlock = 2,
    kDecorationArrayStride = 6,
    kDecorationBuiltIn = 11,
    kDecorationFlat = 14,
    kDecorationLocation = 30,
    kDecorationBinding = 33,
    kDecorationDescriptorSet = 34,
    kDecorationOffset = 35,
    kBuiltInFragCoord = 15,
    kStorageUniformConstant = 0,
    kStorageInput = 1,
    kStorageUniform = 2,
    kStorageOutput = 3,
    kDim2D = 1,
    kDim3D = 2,
    kDimCube = 3,
    kImageOperandsBias = 0x1,
    kImageOperandsLod = 0x2,
    kImageOperandsGrad = 0x4,
    kCapabilityImageQuery = 50,

    kGlslRoundEven = 2,
    kGlslFAbs = 4,
    kGlslFloor = 8,
    kGlslCeil = 9,
    kGlslExp2 = 29,
    kGlslLog2 = 30,
    kGlslSqrt = 31,
    kGlslSMin = 39,
    kGlslSMax = 42,
    kGlslFClamp = 43,
    kGlslNMin = 79,
    kGlslNMax = 80,
};

// Matches any operand word in an instruction pattern.
const uint32_t kAny = 0xffffffffu;

// The SDL_gpu fragment ABI: uniforms in set 3, samplers in set 2.
const CKJitResourceLayout kLayout = {3, 0, 2};

const CKJitInput kColor0 = {0, 4, CKJIT_INPUT_SMOOTH};
const CKJitInput kFlatColor0 = {2, 4, CKJIT_INPUT_FLAT};
const CKJitInput kTexCoord0 = {4, 2, CKJIT_INPUT_SMOOTH};
const CKJitInput kTexCoord1 = {5, 3, CKJIT_INPUT_SMOOTH};
const CKJitInput kFragCoord = {0, 4, CKJIT_INPUT_FRAG_COORD};

const char *g_ModuleDirectory = nullptr;

struct Instruction {
    uint32_t Opcode;
    const uint32_t *Operands;
    uint32_t Count;

    bool Matches(uint32_t opcode, std::initializer_list<uint32_t> pattern) const {
        if (Opcode != opcode || Count < (uint32_t)pattern.size())
            return false;
        uint32_t i = 0;
        for (uint32_t word : pattern) {
            if (word != kAny && Operands[i] != word)
                return false;
            ++i;
        }
        return true;
    }
};

// Instruction view of a module; the words must outlive it.
class Module {
public:
    explicit Module(const XArray<uint32_t> &words) : m_WellFormed(false) {
        if (words.Size() < 5 || words[0] != kMagic)
            return;
        for (int offset = 5; offset < words.Size();) {
            const uint32_t length = words[offset] >> 16;
            if (length == 0 || offset + (int)length > words.Size())
                return;
            m_Instructions.PushBack(Instruction{words[offset] & 0xffffu, words.Begin() + offset + 1, length - 1});
            offset += (int)length;
        }
        m_WellFormed = true;
    }

    bool WellFormed() const { return m_WellFormed; }
    int Size() const { return m_Instructions.Size(); }
    const Instruction &operator[](int i) const { return m_Instructions[i]; }

    int Count(uint32_t opcode, std::initializer_list<uint32_t> pattern = {}) const {
        int count = 0;
        for (int i = 0; i < m_Instructions.Size(); ++i)
            count += m_Instructions[i].Matches(opcode, pattern) ? 1 : 0;
        return count;
    }

    // Index of the first match, or -1.
    int Find(uint32_t opcode, std::initializer_list<uint32_t> pattern = {}) const {
        for (int i = 0; i < m_Instructions.Size(); ++i) {
            if (m_Instructions[i].Matches(opcode, pattern))
                return i;
        }
        return -1;
    }

    // The result id of the first match (types lead with it, others follow
    // their result type).
    uint32_t TypeId(uint32_t opcode, std::initializer_list<uint32_t> pattern) const {
        const int index = Find(opcode, pattern);
        return index < 0 ? 0 : m_Instructions[index].Operands[0];
    }

    bool IsIntConstant(uint32_t id, uint32_t value) const {
        return Count(kOpConstant, {TypeId(kOpTypeInt, {kAny, 32, 1}), id, value}) == 1;
    }

    // Whether id is an integer constant, or a composite of them, with every
    // component within [low, high].
    bool IsIntConstantWithin(uint32_t id, uint32_t low, uint32_t high) const {
        const int scalar = Find(kOpConstant, {TypeId(kOpTypeInt, {kAny, 32, 1}), id});
        if (scalar >= 0)
            return m_Instructions[scalar].Operands[2] >= low && m_Instructions[scalar].Operands[2] <= high;
        const int composite = Find(kOpConstantComposite, {kAny, id});
        if (composite < 0)
            return false;
        for (uint32_t i = 2; i < m_Instructions[composite].Count; ++i) {
            if (!IsIntConstantWithin(m_Instructions[composite].Operands[i], low, high))
                return false;
        }
        return true;
    }

    uint32_t GlslImport() const { return TypeId(kOpExtInstImport, {}); }

    int CountGlsl(uint32_t instruction) const {
        return Count(kOpExtInst, {kAny, kAny, GlslImport(), instruction});
    }

private:
    XArray<Instruction> m_Instructions;
    bool m_WellFormed;
};

void Save(const char *name, const XArray<uint32_t> &words) {
    if (!g_ModuleDirectory)
        return;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.spv", g_ModuleDirectory, name);
    FILE *file = std::fopen(path, "wb");
    TestCheck(file != nullptr, "the module file opens");
    if (file) {
        TestCheck(std::fwrite(words.Begin(), sizeof(uint32_t), (size_t)words.Size(), file) == (size_t)words.Size(),
                  "the module is written");
        std::fclose(file);
    }
}

bool Compile(const CKJitBuilder &b, CKJitValue color, CKJitValue discard, XArray<uint32_t> &words,
             const CKJitResourceLayout &layout = kLayout) {
    CKJitFragmentShader shader;
    return b.Finish(color, discard, shader) && CKJitEmitSpirv(shader, layout, words);
}

// Every IR operation, all reachable from the outputs.
void BuildEveryOperation(CKJitBuilder &b, CKJitValue &color, CKJitValue &discard) {
    const CKJitValue diffuse = b.Input(kColor0);
    const CKJitValue flat = b.Input(kFlatColor0);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue direction = b.Input(kTexCoord1);
    const CKJitValue position = b.Input(kFragCoord);
    const CKJitValue tint = b.Uniform(36);
    const CKJitValue params = b.Uniform(37);
    const CKJitValue bias = b.Component(params, 0);

    const CKJitValue base = b.Sample(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    const CKJitValue cube = b.Sample(9, CKJIT_SAMPLER_CUBE, direction, bias);
    const CKJitValue volume = b.Sample(13, CKJIT_SAMPLER_3D, b.Saturate(direction), bias);
    const CKJitValue lodSample = b.SampleLevel(9, CKJIT_SAMPLER_CUBE, direction, b.CalcLod(0, CKJIT_SAMPLER_2D, uv));
    const CKJitValue gradSample =
        b.SampleGrad(13, CKJIT_SAMPLER_3D, direction, b.Swizzle(tint, "xyz"), b.Neg(b.Swizzle(params, "xyz")));
    const CKJitValue shadow = b.Mul(b.SampleCmp(5, uv, b.Component(params, 1)),
                                    b.SampleCmpLevelZero(5, b.Swizzle(direction, "xy"), b.Component(tint, 0)));

    CKJitValue value = b.Add(b.Mul(base, tint), b.Sub(cube, volume));
    value = b.Add(value, b.Mul(lodSample, gradSample));
    value = b.Div(value, b.Max(b.Abs(flat), b.Float(0.001f)));
    value = b.Min(value, b.Neg(diffuse));
    const CKJitValue rounded = b.Add(b.RoundEven(value), b.Mul(b.Floor(diffuse), b.Ceil(tint)));
    const CKJitValue shade = b.Dot(b.Swizzle(diffuse, "xyz"), direction);
    const CKJitValue spot = b.Sqrt(b.Exp2(b.Log2(b.Component(params, 1))));
    const CKJitValue slope = b.Add(b.Ddx(uv), b.Ddy(b.Neg(b.Swizzle(direction, "xy"))));
    const CKJitValue assembled = b.Construct({b.Add(b.Swizzle(value, "xy"), slope), shade, spot});

    const CKJitValue lanes = b.FloatToInt(b.Component(params, 2));
    const CKJitValue shifted = b.IntShiftRight(lanes, b.FloatToInt(b.Component(params, 3)));
    const CKJitValue bit = b.IntEqual(b.IntAnd(b.IntShiftRight(lanes, b.Int(33)), b.Int(1)), b.Int(1));
    const CKJitValue less = b.Less(shade, spot);
    const CKJitValue lessEqual = b.LessEqual(b.Component(position, 0), b.Component(position, 1));
    const CKJitValue equal = b.Equal(shade, b.Component(tint, 3));
    const CKJitValue notEqual = b.NotEqual(spot, b.Component(tint, 2));
    const CKJitValue picked = b.Select(less, equal, notEqual);
    const CKJitValue mask = b.Select(lessEqual, shifted, b.Int(7));

    const CKJitValue texel = b.FloatToInt(b.Swizzle(position, "xy"));
    const CKJitValue extent = b.IntMax(b.FloatToInt(b.Swizzle(tint, "xy")), b.Int(1));
    const CKJitValue wrapped = b.IntMod(b.IntAdd(b.IntMul(texel, extent), b.Int(3)), extent);
    const CKJitValue offset = b.IntMin(b.IntSub(wrapped, texel), b.Int(255));
    const CKJitValue inside = b.All(b.IntLessEqual(offset, extent));
    const CKJitValue moved = b.Any(b.IntNotEqual(offset, texel));
    const CKJitValue low = b.IntLess(b.Component(wrapped, 0), b.Int(16));
    const CKJitValue fetched = b.Load(0, CKJIT_SAMPLER_2D, b.Construct({wrapped, mask}));
    const CKJitValue slice = b.Load(13, CKJIT_SAMPLER_3D, b.Construct({wrapped, lanes, mask}));
    const CKJitValue extents = b.Construct({b.TextureSize(9, CKJIT_SAMPLER_CUBE, mask),
                                            b.Component(b.TextureSize(13, CKJIT_SAMPLER_3D, lanes), 2),
                                            b.TextureLevels(0, CKJIT_SAMPLER_2D)});
    const CKJitValue texels = b.Add(b.Mul(fetched, slice), b.IntToFloat(extents));

    const CKJitValue flags = b.Or(b.And(picked, b.Not(bit)), b.IntEqual(mask, b.Int(3)));
    const CKJitValue condition = b.Or(flags, b.And(b.And(inside, moved), low));
    const CKJitValue alpha = b.Select(condition, shade, b.Mul(spot, shadow));
    const CKJitValue ramp = b.Construct({b.IntToFloat(offset), b.Swizzle(rounded, "zw")});
    const CKJitValue graded = b.Select(b.Less(assembled, diffuse), assembled, ramp);
    color = b.Mul(b.Select(condition, graded, b.Add(rounded, texels)), alpha);
    discard = b.Less(b.Component(color, 3), b.Component(position, 2));
}

// Two regions, one in the other's then arm, with PHI results of every kind
// and a select of values from before them. The outer condition is uniform, so
// its then arm samples.
void BuildRegions(CKJitBuilder &b, CKJitValue &color, CKJitValue &discard) {
    const CKJitValue diffuse = b.Input(kColor0);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue params = b.Uniform(0);
    const CKJitValue scale = b.Mul(diffuse, b.Uniform(1));
    b.If(b.Less(b.Component(params, 0), b.Float(0.5f)));
    const CKJitValue texel = b.Sample(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    const CKJitValue shaded = b.Mul(texel, scale);
    b.If(b.Less(b.Component(shaded, 3), b.Component(params, 1)));
    const CKJitValue dimmed = b.Mul(shaded, b.Component(params, 2));
    b.Else({dimmed});
    const CKJitValue inner = b.EndIf(shaded);
    const CKJitValue count = b.FloatToInt(b.Component(inner, 0));
    const CKJitValue faint = b.Less(b.Component(texel, 3), b.Float(0.25f));
    b.Else({inner, count, faint, scale});
    const CKJitValue fallback = b.Add(scale, b.Uniform(2));
    CKJitValue results[4];
    b.EndIf({fallback, b.Int(3), b.Bool(false), diffuse}, results);
    color = b.Mul(b.Add(results[0], results[3]), b.IntToFloat(results[1]));
    discard = results[2];
}

// A loop nesting a region and another loop, with carried values of every
// kind: a pair swapping each iteration, one whose next is from before the loop
// and one the nested loop starts. The body reads a step computed before the
// loop. A loop whose constant count exceeds its bound follows. The outer count
// is uniform, so its body samples.
void BuildLoops(CKJitBuilder &b, CKJitValue &color, CKJitValue &discard) {
    const CKJitValue diffuse = b.Input(kColor0);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue params = b.Uniform(0);
    const CKJitValue step = b.Mul(b.Swizzle(b.Uniform(1), "xy"), b.Swizzle(params, "zw"));
    const CKJitValue decay = b.Component(params, 1);
    const CKJitValue swapped = b.Swizzle(uv, "yx");
    CKJitValue carried[5];
    const CKJitValue index =
        b.Loop(b.FloatToInt(b.Component(params, 0)), 8, {diffuse, b.Float(1.0f), uv, swapped, b.Bool(false)}, carried);
    const CKJitValue offset = b.Add(carried[2], b.Mul(step, b.Splat(b.IntToFloat(index), 2)));
    const CKJitValue texel = b.Sample(0, CKJIT_SAMPLER_2D, offset, b.Float(0.0f));
    b.If(b.IntLess(index, b.Int(2)));
    const CKJitValue near = b.Mul(texel, b.Splat(carried[1], 4));
    b.Else({near});
    const CKJitValue weighted = b.EndIf(texel);
    CKJitValue taps;
    const CKJitValue tap = b.Loop(b.FloatToInt(b.Mul(b.Component(texel, 3), b.Float(4.0f))), 4, {index}, &taps);
    const CKJitValue counted = b.EndLoop(b.IntAdd(taps, tap));
    const CKJitValue sum = b.Add(carried[0], b.Mul(weighted, b.Splat(b.IntToFloat(counted), 4)));
    const CKJitValue faint = b.Or(carried[4], b.Less(b.Component(texel, 3), b.Float(0.25f)));
    CKJitValue results[5];
    b.EndLoop({sum, decay, carried[3], carried[2], faint}, results);
    CKJitValue fade;
    b.Loop(b.Int(6), 3, {results[1]}, &fade);
    const CKJitValue faded = b.EndLoop(b.Mul(fade, decay));
    color = b.Mul(b.Add(results[0], b.Construct({results[2], results[3]})), b.Splat(faded, 4));
    discard = results[4];
}

// Whether the block a label starts ends branching to another.
bool BranchesTo(const Module &module, uint32_t block, uint32_t target) {
    int end = module.Find(kOpLabel, {block});
    if (end < 0)
        return false;
    while (end + 1 < module.Size() && module[end + 1].Opcode != kOpLabel)
        ++end;
    return module[end].Matches(kOpBranch, {target});
}

void TestModuleLayout() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Input(kColor0);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, CKJitValue(), words), "a pass-through shader compiles");
    Save("pass_through", words);

    const Module module(words);
    TestCheck(module.WellFormed(), "the instruction stream covers the module exactly");
    TestCheck(words[1] == 0x00010000u && words[4] == 0, "SPIR-V 1.0 with schema 0");
    TestCheck(module.Size() >= 5 && module[0].Opcode == kOpCapability && module[1].Opcode == kOpExtInstImport &&
                  module[2].Opcode == kOpMemoryModel && module[3].Opcode == kOpEntryPoint &&
                  module[4].Opcode == kOpExecutionMode,
              "the preamble comes in logical layout order");
    TestCheck(module.Count(kOpCapability) == 1 && module.Count(kOpCapability, {1}) == 1, "only the Shader capability");
    TestCheck(module.Count(kOpMemoryModel, {0, 1}) == 1, "logical addressing, GLSL450 memory model");
    TestCheck(module.Count(kOpExecutionMode, {kAny, 7}) == 1, "the origin is upper left");

    const Instruction &entry = module[3];
    uint32_t name[2];
    std::memcpy(name, "main\0\0\0", sizeof(name));
    TestCheck(entry.Matches(kOpEntryPoint, {4, kAny, name[0], name[1]}), "the fragment entry point is main");
    TestCheck(entry.Count == 4 + 2, "the interface is the input and the output");
    for (uint32_t i = 4; i < entry.Count; ++i)
        TestCheck(entry.Operands[i] < words[3], "interface ids are within the bound");

    TestCheck(module.Count(kOpVariable, {kAny, kAny, kStorageUniform}) == 0 &&
                  module.Count(kOpVariable, {kAny, kAny, kStorageUniformConstant}) == 0,
              "unused resources are not declared");
    TestCheck(module.Count(kOpKill) == 0 && module.Count(kOpSelectionMerge) == 0, "no discard, no branch");
    TestCheck(module.Count(kOpStore) == 1 && module.Count(kOpReturn) == 1 && module.Count(kOpFunctionEnd) == 1,
              "the colour is stored once and the function returns");
    TestCheck(module[module.Size() - 1].Opcode == kOpFunctionEnd, "the function ends the module");
}

void TestInterface() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Input(kColor0);
    const CKJitValue flat = b.Input(kFlatColor0);
    b.Input(kTexCoord0); // declared, never read
    const CKJitValue position = b.Input(kFragCoord);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Add(b.Mul(color, flat), position), CKJitValue(), words), "the shader compiles");
    Save("interface", words);

    const Module module(words);
    const Instruction &entry = module[module.Find(kOpEntryPoint)];
    TestCheck(entry.Count == 4 + 5, "every declared input and the output are in the interface");
    TestCheck(module.Count(kOpVariable, {kAny, kAny, kStorageInput}) == 4, "one variable per input");
    TestCheck(module.Count(kOpVariable, {kAny, kAny, kStorageOutput}) == 1, "one output");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationLocation, 0}) == 2, "colour 0 and the target share location 0");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationLocation, 2}) == 1 &&
                  module.Count(kOpDecorate, {kAny, kDecorationLocation, 4}) == 1,
              "inputs keep their locations");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationFlat}) == 1, "flat inputs are decorated");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationBuiltIn, kBuiltInFragCoord}) == 1 &&
                  module.Count(kOpDecorate, {kAny, kDecorationLocation}) == 4,
              "the position is the FragCoord built-in without a location");
    TestCheck(module.Count(kOpLoad) == 3, "only read inputs are loaded");
}

void TestResources() {
    CKJitBuilder b(89);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue direction = b.Input(kTexCoord1);
    const CKJitValue first = b.Uniform(0);
    const CKJitValue last = b.Uniform(88);
    CKJitValue color = b.Mul(b.Sample(0, CKJIT_SAMPLER_2D, uv, b.Float(0.0f)), first);
    color = b.Add(color, b.Sample(0, CKJIT_SAMPLER_2D, b.Swizzle(uv, "yx"), b.Float(0.0f)));
    color = b.Add(color, b.Sample(9, CKJIT_SAMPLER_CUBE, direction, b.Float(0.0f)));
    color = b.Mul(color, b.Sample(13, CKJIT_SAMPLER_3D, direction, b.Component(last, 0)));
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, CKJitValue(), words), "the shader compiles");
    Save("resources", words);

    const Module module(words);
    TestCheck(module.Count(kOpVariable, {kAny, kAny, kStorageUniform}) == 1, "one uniform block");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationBlock}) == 1 &&
                  module.Count(kOpMemberDecorate, {kAny, 0, kDecorationOffset, 0}) == 1 &&
                  module.Count(kOpDecorate, {kAny, kDecorationArrayStride, 16}) == 1,
              "the block is an array of float4 rows");
    const int array = module.Find(kOpTypeArray);
    TestCheck(array >= 0 && module.IsIntConstant(module[array].Operands[2], 89), "the block has every row");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationDescriptorSet, 3}) == 1, "the block is in set 3");
    TestCheck(module.Count(kOpAccessChain) == 2, "each row is read once");

    TestCheck(module.Count(kOpTypeImage, {kAny, kAny, kDim2D, 0, 0, 0, 1, 0}) == 1 &&
                  module.Count(kOpTypeImage, {kAny, kAny, kDimCube, 0, 0, 0, 1, 0}) == 1 &&
                  module.Count(kOpTypeImage, {kAny, kAny, kDim3D, 0, 0, 0, 1, 0}) == 1,
              "one sampled colour image type per dimension");
    TestCheck(module.Count(kOpVariable, {kAny, kAny, kStorageUniformConstant}) == 3, "one sampler per used slot");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationDescriptorSet, 2}) == 3, "samplers are in set 2");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationBinding, 0}) == 2 &&
                  module.Count(kOpDecorate, {kAny, kDecorationBinding, 9}) == 1 &&
                  module.Count(kOpDecorate, {kAny, kDecorationBinding, 13}) == 1,
              "a sampler binds at its slot, beside uniform binding 0");
    for (int i = 0; i < module.Size(); ++i) {
        if (module[i].Matches(kOpVariable, {kAny, kAny, kStorageUniformConstant}))
            TestCheck(module.Count(kOpLoad, {kAny, kAny, module[i].Operands[1]}) == 1, "a slot is loaded once");
    }

    CKJitBuilder moved(89);
    XArray<uint32_t> relocated;
    const CKJitValue sampled = moved.Sample(1, CKJIT_SAMPLER_2D, moved.Input(kTexCoord0), moved.Float(0.0f));
    TestCheck(Compile(moved, moved.Mul(sampled, moved.Uniform(5)), CKJitValue(), relocated, {7, 4, 1}),
              "another layout compiles");
    const Module other(relocated);
    TestCheck(other.Count(kOpDecorate, {kAny, kDecorationDescriptorSet, 7}) == 1 &&
                  other.Count(kOpDecorate, {kAny, kDecorationBinding, 4}) == 1 &&
                  other.Count(kOpDecorate, {kAny, kDecorationDescriptorSet, 1}) == 1 &&
                  other.Count(kOpDecorate, {kAny, kDecorationBinding, 1}) == 1,
              "the resource layout places the block and the samplers");
}

void TestUniformBuffers() {
    // Buffers 0 and 2 have one size and 1 another; 3 is never read.
    const uint32_t counts[] = {4, 6, 4, 2};
    CKJitBuilder b(counts, 4);
    const CKJitValue first = b.Uniform(0, 3);
    const CKJitValue second = b.Uniform(1, 5);
    const CKJitValue third = b.Uniform(2, 1);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Add(b.Mul(first, second), third), CKJitValue(), words, {3, 1, 2}), "the shader compiles");
    Save("uniform_buffers", words);

    const Module module(words);
    TestCheck(module.Count(kOpVariable, {kAny, kAny, kStorageUniform}) == 3, "a block per buffer read");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationBlock}) == 2 &&
                  module.Count(kOpMemberDecorate, {kAny, 0, kDecorationOffset, 0}) == 2 &&
                  module.Count(kOpDecorate, {kAny, kDecorationArrayStride, 16}) == 2,
              "buffers of one size share a block type, decorated once");
    TestCheck(module.Count(kOpDecorate, {kAny, kDecorationDescriptorSet, 3}) == 3 &&
                  module.Count(kOpDecorate, {kAny, kDecorationBinding, 1}) == 1 &&
                  module.Count(kOpDecorate, {kAny, kDecorationBinding, 2}) == 1 &&
                  module.Count(kOpDecorate, {kAny, kDecorationBinding, 3}) == 1 &&
                  module.Count(kOpDecorate, {kAny, kDecorationBinding, 4}) == 0,
              "buffer b binds at the layout's uniform binding plus b");
    TestCheck(module.Count(kOpAccessChain) == 3, "each row is read once");
}

void TestLowering() {
    CKJitBuilder b(89);
    CKJitValue color, discard;
    BuildEveryOperation(b, color, discard);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, discard, words), "every operation compiles");
    Save("every_operation", words);

    const Module module(words);
    TestCheck(module.WellFormed(), "the module is well formed");
    TestCheck(module.CountGlsl(kGlslNMin) == 1 && module.CountGlsl(kGlslNMax) == 1, "min and max ignore NaN");
    TestCheck(module.CountGlsl(kGlslFClamp) == 1, "saturate clamps");
    TestCheck(module.CountGlsl(kGlslFAbs) == 1 && module.CountGlsl(kGlslFloor) == 1 &&
                  module.CountGlsl(kGlslCeil) == 1 && module.CountGlsl(kGlslRoundEven) == 1 &&
                  module.CountGlsl(kGlslExp2) == 1 && module.CountGlsl(kGlslLog2) == 1 &&
                  module.CountGlsl(kGlslSqrt) == 1,
              "unary functions are GLSL.std.450 instructions");
    TestCheck(module.Count(kOpDPdx) == 1 && module.Count(kOpDPdy) == 1, "derivatives are OpDPdx and OpDPdy");

    int unbiased = 0, biased = 0;
    for (int i = 0; i < module.Size(); ++i) {
        if (module[i].Opcode == kOpImageSampleImplicitLod) {
            unbiased += module[i].Count == 4 ? 1 : 0;
            biased += module[i].Count == 6 && module[i].Operands[4] == kImageOperandsBias ? 1 : 0;
        }
    }
    TestCheck(unbiased == 1 && biased == 2, "a zero bias is omitted, others are Bias operands");
    TestCheck(module.Count(kOpImageSampleExplicitLod) == 2 && module.Count(kOpImageQueryLod) == 1 &&
                  module.Count(kOpImageFetch) == 2 && module.Count(kOpImageQuerySizeLod) == 2 &&
                  module.Count(kOpImageQueryLevels) == 1 && module.Count(kOpImageSampleDrefImplicitLod) == 1 &&
                  module.Count(kOpImageSampleDrefExplicitLod) == 1,
              "texture operations map to single instructions");

    const uint32_t boolType = module.TypeId(kOpTypeBool, {});
    const uint32_t bool4Type = module.TypeId(kOpTypeVector, {kAny, boolType, 4});
    TestCheck(module.Count(kOpSelect) == 5, "every select is one instruction");
    TestCheck(module.Count(kOpCompositeConstruct, {bool4Type}) == 1, "a scalar condition splats against vector arms");
    const int wide = module.Find(kOpFOrdLessThan, {bool4Type});
    TestCheck(wide >= 0 && module.Count(kOpSelect, {kAny, kAny, module[wide].Operands[1]}) == 1,
              "a vector condition selects per component");

    TestCheck(module.Count(kOpIAdd) == 2 && module.Count(kOpISub) == 1 && module.Count(kOpIMul) == 1 &&
                  module.CountGlsl(kGlslSMin) == 1 && module.CountGlsl(kGlslSMax) == 1 &&
                  module.Count(kOpConvertSToF) == 2,
              "integer arithmetic maps to single instructions");
    TestCheck(module.Count(kOpSLessThan) == 1 && module.Count(kOpSLessThanEqual) == 1 &&
                  module.Count(kOpINotEqual) == 1 && module.Count(kOpAny) == 1 && module.Count(kOpAll) == 1,
              "integer comparisons and reductions map to single instructions");
    TestCheck(module.Count(kOpSRem) == 1 && module.Count(kOpSMod) == 0 && module.Count(kOpBitwiseXor) == 2,
              "the remainder divides non-negative values");

    int shifts = 0, masked = 0;
    for (int i = 0; i < module.Size(); ++i) {
        if (module[i].Opcode != kOpShiftRightArithmetic)
            continue;
        ++shifts;
        const uint32_t count = module[i].Operands[3];
        const int mask = module.Find(kOpBitwiseAnd, {kAny, count});
        if (module.IsIntConstantWithin(count, 0, 31) ||
            (mask >= 0 && module.IsIntConstantWithin(module[mask].Operands[3], 31, 31)))
            ++masked;
    }
    TestCheck(shifts == 3 && masked == 3, "shift counts use their low five bits");

    // The discard branches around a kill after every sample, comparison and
    // LOD query.
    const int branch = module.Find(kOpBranchConditional);
    TestCheck(module.Count(kOpSelectionMerge) == 1 && branch > 0 &&
                  module[branch - 1].Opcode == kOpSelectionMerge,
              "the discard is a structured selection");
    TestCheck(branch > 0 && module.Count(kOpFOrdLessThan, {kAny, module[branch].Operands[0]}) == 1,
              "the branch tests the discard condition");
    TestCheck(branch > 0 && module[branch + 1].Opcode == kOpLabel && module[branch + 2].Opcode == kOpKill &&
                  module[branch + 3].Opcode == kOpLabel && module[branch + 4].Opcode == kOpStore,
              "the killing block precedes the colour store");
    int lastQuad = -1;
    for (int i = 0; i < module.Size(); ++i) {
        const uint32_t opcode = module[i].Opcode;
        if (opcode == kOpImageSampleImplicitLod || opcode == kOpImageSampleDrefImplicitLod ||
            opcode == kOpImageQueryLod || opcode == kOpDPdx || opcode == kOpDPdy)
            lastQuad = i;
    }
    TestCheck(lastQuad < branch, "every sample, comparison, LOD query and derivative runs before the discard");
}

void TestTextureAccess() {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue direction = b.Input(kTexCoord1);
    const CKJitValue position = b.Input(kFragCoord);

    // Slots 0 and 6 are only loaded from and queried, 1 and 4 are sampled.
    const CKJitValue mip = b.IntSub(b.TextureLevels(0, CKJIT_SAMPLER_2D), b.Int(1));
    const CKJitValue last = b.IntSub(b.TextureSize(0, CKJIT_SAMPLER_2D, mip), b.Int(1));
    const CKJitValue texel = b.IntMin(b.FloatToInt(b.Swizzle(position, "xy")), last);
    const CKJitValue fetched = b.Load(0, CKJIT_SAMPLER_2D, b.Construct({texel, mip}));
    const CKJitValue slice = b.Load(6, CKJIT_SAMPLER_3D, b.Construct({b.FloatToInt(direction), b.Int(0)}));
    const CKJitValue lod = b.CalcLod(1, CKJIT_SAMPLER_2D, uv);
    const CKJitValue level = b.SampleLevel(1, CKJIT_SAMPLER_2D, uv, b.Max(lod, b.Float(0.0f)));
    const CKJitValue graded = b.SampleGrad(4, CKJIT_SAMPLER_CUBE, direction, b.Ddx(direction), b.Ddy(direction));
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Add(b.Mul(fetched, slice), b.Mul(level, graded)), b.Less(lod, b.Float(0.0f)), words),
              "texture access compiles");
    Save("texture_access", words);

    const Module module(words);
    TestCheck(module.WellFormed(), "the module is well formed");
    TestCheck(module.Count(kOpCapability, {kCapabilityImageQuery}) == 1, "queries need the ImageQuery capability");
    const uint32_t intType = module.TypeId(kOpTypeInt, {kAny, 32, 1});
    const uint32_t floatType = module.TypeId(kOpTypeFloat, {});

    const int lodSample = module.Find(kOpImageSampleExplicitLod, {kAny, kAny, kAny, kAny, kImageOperandsLod});
    TestCheck(lodSample >= 0 && module[lodSample].Count == 6 &&
                  module.Count(kOpExtInst, {kAny, module[lodSample].Operands[5], module.GlslImport(), kGlslNMax}) == 1,
              "an explicit LOD is a Lod operand");
    const int gradSample = module.Find(kOpImageSampleExplicitLod, {kAny, kAny, kAny, kAny, kImageOperandsGrad});
    TestCheck(gradSample >= 0 && module[gradSample].Count == 7 &&
                  module.Count(kOpDPdx, {kAny, module[gradSample].Operands[5]}) == 1 &&
                  module.Count(kOpDPdy, {kAny, module[gradSample].Operands[6]}) == 1,
              "explicit derivatives are Grad operands");
    const int query = module.Find(kOpImageQueryLod);
    TestCheck(query >= 0 && module[query].Operands[0] == module.TypeId(kOpTypeVector, {kAny, floatType, 2}) &&
                  module.Count(kOpCompositeExtract, {floatType, kAny, module[query].Operands[1], 1}) == 1,
              "the LOD is the query's unclamped second component");
    TestCheck(module.Count(kOpImageQuerySizeLod, {module.TypeId(kOpTypeVector, {kAny, intType, 2})}) == 1 &&
                  module.Count(kOpImageQueryLevels, {intType}) == 1,
              "size queries return the extent and the mip count");

    TestCheck(module.Count(kOpImage) == 2, "loaded and queried slots take their image once");
    int fetches = 0;
    for (int i = 0; i < module.Size(); ++i) {
        const uint32_t opcode = module[i].Opcode;
        if (opcode != kOpImageFetch && opcode != kOpImageQuerySizeLod && opcode != kOpImageQueryLevels)
            continue;
        TestCheck(module.Count(kOpImage, {kAny, module[i].Operands[2]}) == 1,
                  "fetches and size queries take the image without its sampler");
        if (opcode != kOpImageFetch)
            continue;
        ++fetches;
        const int coordinate = module.Find(kOpVectorShuffle, {kAny, module[i].Operands[3]});
        const int lodOperand = module.Find(kOpCompositeExtract, {intType, module[i].Operands[5]});
        TestCheck(module[i].Count == 6 && module[i].Operands[4] == kImageOperandsLod && coordinate >= 0 &&
                      lodOperand >= 0 && module[coordinate].Operands[2] == module[lodOperand].Operands[2] &&
                      module[lodOperand].Operands[3] == module[coordinate].Count - 4 &&
                      module[coordinate].Operands[0] ==
                          module.TypeId(kOpTypeVector, {kAny, intType, module[coordinate].Count - 4}),
                  "a fetch splits the texel into its coordinate and the mip after it");
    }
    TestCheck(fetches == 2, "every load is a fetch");
    const int branch = module.Find(kOpBranchConditional);
    TestCheck(query >= 0 && query < branch, "the LOD query runs before the discard");

    CKJitBuilder plain(4);
    const CKJitValue origin = plain.FloatToInt(plain.Swizzle(plain.Input(kFragCoord), "xy"));
    const CKJitValue loaded = plain.Load(0, CKJIT_SAMPLER_2D, plain.Construct({origin, plain.Int(0)}));
    const CKJitValue sampled = plain.SampleLevel(1, CKJIT_SAMPLER_2D, plain.Input(kTexCoord0), plain.Float(0.0f));
    XArray<uint32_t> unqueried;
    TestCheck(Compile(plain, plain.Add(loaded, sampled), CKJitValue(), unqueried), "fetches and explicit samples compile");
    Save("unqueried_access", unqueried);
    TestCheck(Module(unqueried).Count(kOpCapability) == 1, "fetches and explicit samples need no query capability");
}

void TestDepthComparison() {
    CKJitBuilder b(4);
    const CKJitValue uv = b.Input(kTexCoord0);
    const CKJitValue color = b.Input(kColor0);
    const CKJitValue position = b.Input(kFragCoord);

    // Slot 2 is compared, loaded from and queried, 3 only compared at the base
    // mip and 1 is a colour texture.
    const CKJitValue filtered = b.SampleCmp(2, uv, b.Component(position, 2));
    const CKJitValue base = b.SampleCmpLevelZero(3, b.Swizzle(uv, "yx"), b.Component(color, 3));
    const CKJitValue texel = b.Construct({b.FloatToInt(b.Swizzle(position, "xy")), b.Int(0)});
    const CKJitValue depth = b.Load(2, CKJIT_SAMPLER_2D_COMPARE, texel);
    const CKJitValue size = b.IntToFloat(b.TextureSize(2, CKJIT_SAMPLER_2D_COMPARE, b.Int(0)));
    const CKJitValue sampled = b.Sample(1, CKJIT_SAMPLER_2D, uv, b.Float(0.0f));
    const CKJitValue shade = b.Mul(b.Construct({filtered, base, size}), b.Mul(depth, sampled));
    XArray<uint32_t> words;
    TestCheck(Compile(b, shade, b.Less(filtered, b.Float(0.5f)), words), "depth comparisons compile");
    Save("depth_comparison", words);

    const Module module(words);
    TestCheck(module.WellFormed(), "the module is well formed");
    const uint32_t floatType = module.TypeId(kOpTypeFloat, {});
    const uint32_t depthImage = module.TypeId(kOpTypeImage, {kAny, floatType, kDim2D, 1, 0, 0, 1, 0});
    const uint32_t depthSampler = module.TypeId(kOpTypeSampledImage, {kAny, depthImage});
    TestCheck(depthImage != 0 && module.Count(kOpTypeImage, {kAny, kAny, kDim2D, 1}) == 1 &&
                  module.Count(kOpTypeImage, {kAny, kAny, kDim2D, 0}) == 1,
              "compared slots are depth images, colour slots are not");
    TestCheck(depthSampler != 0 && module.Count(kOpLoad, {depthSampler}) == 2, "both compared slots are depth images");

    const int implicit = module.Find(kOpImageSampleDrefImplicitLod);
    TestCheck(implicit >= 0 && module[implicit].Count == 5 && module[implicit].Operands[0] == floatType &&
                  module.Count(kOpCompositeExtract, {floatType, module[implicit].Operands[4], kAny, 2}) == 1,
              "a comparison is a scalar Dref sample of the reference");
    const int explicitLod = module.Find(kOpImageSampleDrefExplicitLod);
    TestCheck(explicitLod >= 0 && module[explicitLod].Count == 7 && module[explicitLod].Operands[0] == floatType &&
                  module.Count(kOpCompositeExtract, {floatType, module[explicitLod].Operands[4], kAny, 3}) == 1 &&
                  module[explicitLod].Operands[5] == kImageOperandsLod &&
                  module.Count(kOpConstant, {floatType, module[explicitLod].Operands[6], 0}) == 1,
              "a base-mip comparison has a zero Lod operand");
    TestCheck(implicit >= 0 && explicitLod >= 0 && module[implicit].Operands[2] != module[explicitLod].Operands[2] &&
                  module.Count(kOpLoad, {depthSampler, module[implicit].Operands[2]}) == 1 &&
                  module.Count(kOpLoad, {depthSampler, module[explicitLod].Operands[2]}) == 1,
              "a comparison samples its slot's depth image");

    const int fetch = module.Find(kOpImageFetch);
    TestCheck(fetch >= 0 && module[fetch].Operands[0] == module.TypeId(kOpTypeVector, {kAny, floatType, 4}) &&
                  module.Count(kOpImage, {depthImage, module[fetch].Operands[2]}) == 1,
              "a compared slot is loaded from as a float4 through its depth image");
    TestCheck(module.Count(kOpImageQuerySizeLod, {kAny, kAny, module[fetch].Operands[2]}) == 1,
              "a compared slot's size is queried through the same image");
    TestCheck(implicit >= 0 && implicit < module.Find(kOpBranchConditional), "the comparison runs before the discard");
}

void TestIfRegions() {
    CKJitBuilder b(4);
    CKJitValue color, discard;
    BuildRegions(b, color, discard);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, discard, words), "regions compile");
    Save("if_regions", words);

    const Module module(words);
    TestCheck(module.WellFormed(), "the module is well formed");
    TestCheck(module.Count(kOpSelectionMerge) == 3 && module.Count(kOpBranchConditional) == 3 &&
                  module.Count(kOpBranch) == 4,
              "each region is a selection whose arms branch to its merge block");

    // Each PHI heads its merge block, taking each value from a block that
    // branches there: an arm ending after a nested region ends in its merge.
    int phis[4] = {-1, -1, -1, -1};
    uint32_t blocks[4] = {};
    int count = 0;
    uint32_t block = 0;
    for (int i = 0; i < module.Size(); ++i) {
        if (module[i].Opcode == kOpLabel)
            block = module[i].Operands[0];
        if (module[i].Opcode != kOpPhi)
            continue;
        if (count < 4) {
            phis[count] = i;
            blocks[count] = block;
        }
        ++count;
        TestCheck(module[i].Count == 6 && BranchesTo(module, module[i].Operands[3], block) &&
                      BranchesTo(module, module[i].Operands[5], block),
                  "a PHI takes each value from a block branching to its own");
    }
    TestCheck(count == 4, "every result an arm computes is a PHI");
    if (count != 4)
        return;
    TestCheck(blocks[0] != blocks[1] && blocks[1] == blocks[2] && blocks[2] == blocks[3],
              "a region's PHIs share its merge block");
    TestCheck(module[phis[1]].Operands[3] == blocks[0], "an arm ending in a nested region ends in its merge block");
    TestCheck(module.Count(kOpSelect) == 1 && module.Find(kOpSelect) > phis[3],
              "the result from before the region is selected after it");

    const uint32_t sampledImage = module.TypeId(kOpTypeSampledImage, {});
    const int sample = module.Find(kOpImageSampleImplicitLod);
    TestCheck(module.Find(kOpLoad, {sampledImage}) < module.Find(kOpSelectionMerge) &&
                  sample > module.Find(kOpBranchConditional) && sample < phis[0],
              "the arm samples through a sampler loaded in the entry block");
    const int kill = module.Find(kOpKill);
    TestCheck(kill > phis[3] && module.Count(kOpBranchConditional, {module[phis[3]].Operands[1]}) == 1,
              "the discard tests its PHI after the regions");
}

// The index of the instruction of the function body whose result is id, or
// -1.
int Defines(const Module &module, uint32_t id) {
    for (int i = module.Find(kOpLabel); i >= 0 && i < module.Size(); ++i) {
        if (module[i].Opcode != kOpLabel && module[i].Count >= 2 && module[i].Operands[1] == id)
            return i;
    }
    return -1;
}

// A loop construct: its header's phis, the index first, then the test the
// loop merge follows.
struct LoopConstruct {
    int Label; // of the header
    int Merge; // the loop merge
    uint32_t Header;
    uint32_t Continue;
    uint32_t Exit;
    int Phis;  // the first
    int Count; // of phis
};

LoopConstruct FindLoop(const Module &module, int merge) {
    LoopConstruct loop = {-1, merge, 0, module[merge].Operands[1], module[merge].Operands[0], -1, 0};
    for (int i = merge; i >= 0 && loop.Label < 0; --i) {
        if (module[i].Opcode == kOpLabel)
            loop.Label = i;
    }
    if (loop.Label < 0)
        return loop;
    loop.Header = module[loop.Label].Operands[0];
    loop.Phis = loop.Label + 1;
    while (module[loop.Phis + loop.Count].Opcode == kOpPhi)
        ++loop.Count;
    return loop;
}

void TestLoops() {
    CKJitBuilder b(4);
    CKJitValue color, discard;
    BuildLoops(b, color, discard);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, discard, words), "loops compile");
    Save("loops", words);

    const Module module(words);
    TestCheck(module.WellFormed(), "the module is well formed");
    TestCheck(module.Count(kOpLoopMerge) == 3 && module.Count(kOpSelectionMerge) == 2,
              "each loop is a loop construct and the region in one a selection");
    LoopConstruct loops[3];
    int count = 0;
    for (int i = 0; i < module.Size(); ++i) {
        if (module[i].Opcode == kOpLoopMerge && count < 3)
            loops[count++] = FindLoop(module, i);
    }
    if (count != 3)
        return;

    const uint32_t intType = module.TypeId(kOpTypeInt, {kAny, 32, 1});
    for (int k = 0; k < 3; ++k) {
        const LoopConstruct &loop = loops[k];
        TestCheck(loop.Count >= 2 && module[loop.Phis + loop.Count].Opcode == kOpSLessThan &&
                      loop.Phis + loop.Count + 1 == loop.Merge,
                  "a header holds its phis, then the test");
        if (loop.Count < 2 || loop.Phis + loop.Count + 1 != loop.Merge)
            continue;
        const Instruction &index = module[loop.Phis];
        const Instruction &test = module[loop.Merge - 1];
        TestCheck(test.Operands[2] == index.Operands[1] &&
                      module[loop.Merge + 1].Matches(kOpBranchConditional, {test.Operands[1], kAny, loop.Exit}),
                  "the header enters the body while the index is below the trips, and exits otherwise");
        for (int phi = loop.Phis; phi < loop.Phis + loop.Count; ++phi) {
            const Instruction &value = module[phi];
            TestCheck(value.Count == 6 && BranchesTo(module, value.Operands[3], loop.Header) &&
                          value.Operands[5] == loop.Continue && Defines(module, value.Operands[4]) >= 0,
                      "a header phi takes a value from before the loop and the next from the continue block");
        }
        const int start = module.Find(kOpLabel, {loop.Continue});
        TestCheck(start > loop.Merge && module.IsIntConstant(index.Operands[2], 0) &&
                      module[start + 1].Matches(kOpIAdd, {intType, index.Operands[4], index.Operands[1]}) &&
                      module.IsIntConstant(module[start + 1].Operands[3], 1) &&
                      BranchesTo(module, loop.Continue, loop.Header) &&
                      module[start + 3].Matches(kOpLabel, {loop.Exit}),
                  "the index counts from zero in the continue block, which branches back before the merge block");
    }

    const LoopConstruct &outer = loops[0];
    const LoopConstruct &inner = loops[1];
    const LoopConstruct &fixed = loops[2];
    TestCheck(outer.Count == 6 && inner.Count == 2 && fixed.Count == 2, "every carried value is a header phi");
    if (outer.Count != 6 || inner.Count != 2 || fixed.Count != 2)
        return;
    const int outerEnd = module.Find(kOpLabel, {outer.Continue});
    TestCheck(inner.Label > outer.Merge && module.Find(kOpLabel, {inner.Exit}) < outerEnd,
              "the nested loop is in the body");
    TestCheck(module[inner.Phis + 1].Operands[2] == module[outer.Phis].Operands[1],
              "the nested loop carries the index from the body around it");
    TestCheck(module[outer.Phis + 3].Operands[4] == module[outer.Phis + 4].Operands[1] &&
                  module[outer.Phis + 4].Operands[4] == module[outer.Phis + 3].Operands[1],
              "a swapped pair takes each other's phi");
    TestCheck(Defines(module, module[outer.Phis + 2].Operands[4]) < outer.Label,
              "a next from before the loop is computed before it");

    const int sample = module.Find(kOpImageSampleImplicitLod);
    TestCheck(sample > outer.Merge && sample < inner.Label, "the uniform loop's body samples");
    const int trips = module.Find(kOpExtInst, {intType, kAny, module.GlslImport(), kGlslSMin});
    TestCheck(module.CountGlsl(kGlslSMin) == 2 && trips >= 0 && trips < outer.Label &&
                  module[trips].Operands[1] == module[outer.Merge - 1].Operands[3] &&
                  module.IsIntConstant(module[trips].Operands[5], 8),
              "a runtime count is bounded before the loop");
    TestCheck(module.IsIntConstant(module[fixed.Merge - 1].Operands[3], 3),
              "a constant count takes the bound's place when that is lower");
    TestCheck(module[fixed.Phis + 1].Operands[2] == module[outer.Phis + 2].Operands[1] && fixed.Label > outerEnd,
              "a RESULT is its carried value's phi, which dominates the merge block");
    const int kill = module.Find(kOpKill);
    TestCheck(kill > fixed.Label && module.Count(kOpBranchConditional, {module[outer.Phis + 5].Operands[1]}) == 1,
              "the discard tests a carried value after the loops");
}

void TestIntegerLowering() {
    CKJitBuilder b(4);
    const CKJitValue position = b.Input(kFragCoord);
    const CKJitValue params = b.Uniform(0);
    const CKJitValue texel = b.FloatToInt(b.Swizzle(position, "xyz"));
    const CKJitValue extent = b.FloatToInt(b.Swizzle(params, "xyz"));
    const CKJitValue wrapped = b.IntMod(texel, extent);
    const CKJitValue bound = b.IntSub(texel, b.Int(5));
    const CKJitValue inside = b.All(b.IntLess(wrapped, bound));
    const CKJitValue scaled = b.IntMul(texel, b.Int(3));
    const CKJitValue ramp = b.IntToFloat(b.IntSub(wrapped, scaled));
    const CKJitValue color = b.Select(inside, b.Construct({ramp, b.Float(1.0f)}), b.Uniform(1));
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, CKJitValue(), words), "integer work compiles");
    Save("integer_lowering", words);

    const Module module(words);
    const uint32_t intType = module.TypeId(kOpTypeInt, {kAny, 32, 1});
    const uint32_t int3Type = module.TypeId(kOpTypeVector, {kAny, intType, 3});
    TestCheck(int3Type != 0 && module.Count(kOpTypeVector, {kAny, intType}) == 1, "integer vectors are vector types");
    TestCheck(module.Count(kOpConvertSToF, {module.TypeId(kOpTypeVector, {kAny, module.TypeId(kOpTypeFloat, {}), 3})}) == 1,
              "conversions keep the width");

    // sign = a >> 31; ((a ^ sign) rem b ^ sign) + (b & sign)
    const int remainder = module.Find(kOpSRem, {int3Type});
    TestCheck(remainder >= 0, "the remainder is an integer vector");
    if (remainder < 0)
        return;
    const uint32_t divisor = module[remainder].Operands[3];
    const int magnitude = module.Find(kOpBitwiseXor, {int3Type, module[remainder].Operands[2]});
    TestCheck(magnitude >= 0, "the dividend is made non-negative");
    if (magnitude < 0)
        return;
    const uint32_t dividend = module[magnitude].Operands[2];
    const uint32_t sign = module[magnitude].Operands[3];
    TestCheck(module.Count(kOpConvertFToS, {int3Type, dividend}) == 1 && module.Count(kOpConvertFToS, {int3Type, divisor}) == 1,
              "the operands are the converted values");
    const int shift = module.Find(kOpShiftRightArithmetic, {int3Type, sign, dividend});
    TestCheck(shift >= 0 && module.IsIntConstantWithin(module[shift].Operands[3], 31, 31),
              "the sign mask is the dividend shifted by 31");
    const int restored = module.Find(kOpBitwiseXor, {int3Type, kAny, module[remainder].Operands[1], sign});
    const int offset = module.Find(kOpBitwiseAnd, {int3Type, kAny, divisor, sign});
    TestCheck(restored >= 0 && offset >= 0 &&
                  module.Count(kOpIAdd, {int3Type, kAny, module[restored].Operands[1], module[offset].Operands[1]}) == 1,
              "a negative dividend takes the divisor's complement");

    const int less = module.Find(kOpSLessThan);
    TestCheck(less >= 0 && module.Count(kOpAll, {module.TypeId(kOpTypeBool, {}), kAny, module[less].Operands[1]}) == 1,
              "all reduces the comparison");
    TestCheck(module.Count(kOpISub) == 2 && module.Count(kOpIMul) == 1, "subtraction and multiplication are direct");
}

void TestDeclarationsAreUnique() {
    CKJitBuilder b(89);
    CKJitValue color, discard;
    BuildEveryOperation(b, color, discard);
    XArray<uint32_t> words;
    TestCheck(Compile(b, color, discard, words), "the shader compiles");

    const Module module(words);
    for (int i = 0; i < module.Size(); ++i) {
        const Instruction &a = module[i];
        const bool type = a.Opcode >= kOpTypeVoid && a.Opcode <= kOpTypeFunction;
        const bool constant = a.Opcode >= kOpConstantTrue && a.Opcode <= kOpConstantComposite;
        if (!type && !constant)
            continue;
        for (int j = i + 1; j < module.Size(); ++j) {
            const Instruction &other = module[j];
            if (other.Opcode != a.Opcode || other.Count != a.Count)
                continue;
            // Compare everything but the result id.
            const uint32_t result = type ? 0 : 1;
            bool same = true;
            for (uint32_t w = 0; w < a.Count && same; ++w)
                same = w == result || a.Operands[w] == other.Operands[w];
            TestCheck(!same, "types and constants are declared once");
        }
    }

    CKJitFragmentShader shader;
    XArray<uint32_t> again;
    TestCheck(b.Finish(color, discard, shader) && CKJitEmitSpirv(shader, kLayout, again), "the shader compiles again");
    TestCheck(again.Size() == words.Size() &&
                  std::memcmp(again.Begin(), words.Begin(), (size_t)words.Size() * sizeof(uint32_t)) == 0,
              "emission is deterministic");
}

void TestConstantOutputs() {
    CKJitBuilder b(4);
    b.Input(kColor0);
    XArray<uint32_t> words;
    TestCheck(Compile(b, b.Float4(0.25f, 0.5f, 0.75f, 1.0f), b.Bool(true), words), "constant outputs compile");
    Save("constant_outputs", words);

    const Module module(words);
    TestCheck(module.Count(kOpConstantComposite) == 1 && module.Count(kOpConstantTrue) == 1, "outputs are constants");
    TestCheck(module.Count(kOpKill) == 1, "an unconditional discard still kills");
    TestCheck(module.Count(kOpLoad) == 0, "nothing is read");
}

void TestRejects() {
    CKJitBuilder b(4);
    const CKJitValue color = b.Mul(b.Input(kColor0), b.Uniform(1));
    CKJitFragmentShader shader;
    TestCheck(b.Finish(color, CKJitValue(), shader), "the shader finishes");
    shader.Nodes[shader.Nodes.Size() - 1].Operands[0] = (uint32_t)shader.Nodes.Size() - 1;
    XArray<uint32_t> words;
    TestCheck(!CKJitEmitSpirv(shader, kLayout, words), "shaders that fail verification are refused");
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 1)
        g_ModuleDirectory = argv[1];
    TestFramework framework;
    framework.Run("module layout", TestModuleLayout);
    framework.Run("interface", TestInterface);
    framework.Run("resources", TestResources);
    framework.Run("uniform buffers", TestUniformBuffers);
    framework.Run("lowering", TestLowering);
    framework.Run("texture access", TestTextureAccess);
    framework.Run("depth comparison", TestDepthComparison);
    framework.Run("if regions", TestIfRegions);
    framework.Run("loops", TestLoops);
    framework.Run("integer lowering", TestIntegerLowering);
    framework.Run("declarations are unique", TestDeclarationsAreUnique);
    framework.Run("constant outputs", TestConstantOutputs);
    framework.Run("rejects", TestRejects);
    return framework.ExitCode();
}
