#include "FFPCoverageDomain.h"
#include "TestTriangleMultiset.h"

#include "CKFFShaderABI.h"
#include "CKFFFragmentProgram.h"

#include <math.h>

#include <stdarg.h>
#include <stdio.h>

namespace {

char g_FFPCoverageFailure[512];

CKFFFragmentProgram BuildTestFragmentProgram(const CKFFShaderKeyFS &key)
{
    const CKFFSamplerLayoutPlan samplerLayoutPlan =
        CKFFBuildSamplerLayoutPlan(key);
    return CKFFBuildFragmentProgram(key, samplerLayoutPlan);
}

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(g_FFPCoverageFailure, sizeof(g_FFPCoverageFailure), format, args);
    va_end(args);
    TestFail(g_FFPCoverageFailure);
}

void SetStageDefaults(CKFFFSStateDesc &desc, CKDWORD stage)
{
    desc.SetStageColorOp(stage, CKRST_TOP_SELECTARG1);
    desc.SetStageColorArg1(stage, CKRST_TA_CURRENT);
    desc.SetStageAlphaOp(stage, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(stage, CKRST_TA_CURRENT);
}

void SetStageColorArg(CKFFFSStateDesc &desc, CKDWORD stage, CKDWORD slot, CKDWORD arg)
{
    if (slot == 0)
        desc.SetStageColorArg0(stage, arg);
    else if (slot == 1)
        desc.SetStageColorArg1(stage, arg);
    else
        desc.SetStageColorArg2(stage, arg);
}

void SetStageAlphaArg(CKFFFSStateDesc &desc, CKDWORD stage, CKDWORD slot, CKDWORD arg)
{
    if (slot == 0)
        desc.SetStageAlphaArg0(stage, arg);
    else if (slot == 1)
        desc.SetStageAlphaArg1(stage, arg);
    else
        desc.SetStageAlphaArg2(stage, arg);
}

void FragmentProgramLayoutFieldsAreDisjointAndRoundTrip()
{
    // Every field of CKFFFragmentProgramLayout.def fits its 24-bit lane and no two fields
    // share a bit; setting one field never disturbs another.
    CKDWORD occupancy[CKFF_FRAGMENT_PROGRAM_LANE_COUNT] = {};
    CKDWORD checkedFields = 0;
    CKDWORD checkedValues = 0;

    auto occupy = [&](const CKFFFragmentProgramBitfield &layout, const char *name) {
        TestCheckf(layout.BitCount > 0 && layout.BitOffset + layout.BitCount <= CKFF_FRAGMENT_PROGRAM_LANE_BITS,
                   "Spec field %s must fit inside one 24-bit lane", name);
        TestCheckf(layout.Lane < CKFF_FRAGMENT_PROGRAM_LANE_COUNT,
                   "Spec field %s must use a lane below CKFF_FRAGMENT_PROGRAM_LANE_COUNT", name);
        const CKDWORD mask = ((1u << layout.BitCount) - 1u) << layout.BitOffset;
        TestCheckf((occupancy[layout.Lane] & mask) == 0,
                   "Spec field %s must not overlap another field in lane %u", name, layout.Lane);
        occupancy[layout.Lane] |= mask;
        ++checkedFields;
    };

    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT; ++fieldIndex) {
            const CKFFFragmentProgramStageField field = (CKFFFragmentProgramStageField)fieldIndex;
            occupy(CKFFFragmentProgramStageFieldLayout(stage, field), CKFFFragmentProgramStageFieldDesc(field).Name);
        }
    }
    for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT; ++fieldIndex) {
        const CKFFFragmentProgramGlobalField field = (CKFFFragmentProgramGlobalField)fieldIndex;
        const CKFFFragmentProgramBitfield layout = CKFFFragmentProgramGlobalFieldLayout(field);
        TestCheckf(layout.Lane >= CKFF_FRAGMENT_PROGRAM_GLOBAL_LANE_BASE,
                   "Global spec field %s must live in the global lanes", CKFFFragmentProgramGlobalFieldDesc(field).Name);
        occupy(layout, CKFFFragmentProgramGlobalFieldDesc(field).Name);
    }
    TestCheck(CKFF_FRAGMENT_PROGRAM_STAGE_LANE_BASE + CKFF_FRAGMENT_PROGRAM_STAGE_COUNT * CKFF_FRAGMENT_PROGRAM_STAGE_LANE_STRIDE <= CKFF_FRAGMENT_PROGRAM_GLOBAL_LANE_BASE,
              "Stage lanes must end before the global lanes");

    // Round trip every value of every field (sampled for fields wider than 8 bits)
    // against a background of all-ones.
    CKFFFragmentProgram background;
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT; ++fieldIndex)
            background.SetStage(stage, (CKFFFragmentProgramStageField)fieldIndex, 0xFFFFFFFFu);
    }
    for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT; ++fieldIndex)
        background.Set((CKFFFragmentProgramGlobalField)fieldIndex, 0xFFFFFFFFu);
    for (CKDWORD lane = 0; lane < CKFF_FRAGMENT_PROGRAM_LANE_COUNT; ++lane) {
        TestCheckf(background.Lanes()[lane] == occupancy[lane],
                   "Lane %u must hold exactly the bits of its fields", lane);
    }

    auto roundTrip = [&](CKDWORD bits, const char *name, auto setter, auto getter) {
        const CKDWORD maxValue = 1u << bits;
        const CKDWORD step = bits <= 8 ? 1u : (maxValue / 4096u);
        for (CKDWORD value = 0; value < maxValue; value += step) {
            CKFFFragmentProgram spec = background;
            setter(spec, value);
            TestCheckf(getter(spec) == value, "Spec field %s value %u must round-trip", name, value);
            setter(spec, maxValue - 1u);
            TestCheckf(spec == background, "Spec field %s must not disturb other fields", name);
            ++checkedValues;
        }
        CKFFFragmentProgram spec = background;
        setter(spec, maxValue - 1u);
        TestCheckf(getter(spec) == maxValue - 1u, "Spec field %s maximum must round-trip", name);
    };
    for (CKDWORD stage = 0; stage < CKFF_FRAGMENT_PROGRAM_STAGE_COUNT; ++stage) {
        for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_STAGE_FIELD_COUNT; ++fieldIndex) {
            const CKFFFragmentProgramStageField field = (CKFFFragmentProgramStageField)fieldIndex;
            roundTrip(CKFFFragmentProgramStageFieldDesc(field).Layout.BitCount, CKFFFragmentProgramStageFieldDesc(field).Name,
                      [&](CKFFFragmentProgram &spec, CKDWORD value) { spec.SetStage(stage, field, value); },
                      [&](const CKFFFragmentProgram &spec) { return spec.GetStage(stage, field); });
        }
        roundTrip(3, "SAMPLER_ORDINAL(stage)",
                  [&](CKFFFragmentProgram &program, CKDWORD value) { program.SetSamplerOrdinal(stage, value); },
                  [&](const CKFFFragmentProgram &program) { return program.GetSamplerOrdinal(stage); });
    }
    for (CKDWORD fieldIndex = 0; fieldIndex < (CKDWORD)CKFF_FRAGMENT_PROGRAM_GLOBAL_FIELD_COUNT; ++fieldIndex) {
        const CKFFFragmentProgramGlobalField field = (CKFFFragmentProgramGlobalField)fieldIndex;
        roundTrip(CKFFFragmentProgramGlobalFieldDesc(field).Layout.BitCount, CKFFFragmentProgramGlobalFieldDesc(field).Name,
                  [&](CKFFFragmentProgram &spec, CKDWORD value) { spec.Set(field, value); },
                  [&](const CKFFFragmentProgram &spec) { return spec.Get(field); });
    }

    printf("  coverage: specFields=%u specValues=%u\n", checkedFields, checkedValues);
}

void SpecPack24IsExactInFp32()
{
    // u_ffProgram lanes travel as float components: every lane value must survive
    // the float conversion bit-exactly, including the 24-bit maximum.
    CKDWORD checkedLanes = 0;
    const CKDWORD patterns[] = {
        0u, 1u, 0x7FFFFFu, 0x800000u, 0xFFFFFEu, 0xFFFFFFu, 0xAAAAAAu, 0x555555u, 0x123456u, 0xFEDCBAu,
    };
    for (CKDWORD patternIndex = 0; patternIndex < (CKDWORD)FFPCoverageArrayCount(patterns); ++patternIndex) {
        CKDWORD lanes[CKFF_FRAGMENT_PROGRAM_LANE_COUNT];
        for (CKDWORD lane = 0; lane < CKFF_FRAGMENT_PROGRAM_LANE_COUNT; ++lane)
            lanes[lane] = (patterns[patternIndex] + lane * 0x010101u) & CKFFFragmentProgram::LaneMask;
        CKFFFragmentProgram spec;
        spec.SetLanes(lanes, CKFF_FRAGMENT_PROGRAM_LANE_COUNT);

        float packed[CKFF_FRAGMENT_PROGRAM_VEC4_COUNT][4];
        spec.Pack24(packed);
        for (CKDWORD lane = 0; lane < CKFF_FRAGMENT_PROGRAM_LANE_COUNT; ++lane) {
            const float value = packed[lane / 4][lane % 4];
            TestCheckf(value == floorf(value) && value >= 0.0f && value < 16777216.0f,
                       "Packed lane %u must be a non-negative integer below 2^24", lane);
            TestCheckf((CKDWORD)value == lanes[lane],
                       "Packed lane %u must equal its 24-bit value exactly", lane);
            ++checkedLanes;
        }
        const CKFFFragmentProgram unpacked =
            CKFFFragmentProgram::Unpack24(&packed[0][0], CKFF_FRAGMENT_PROGRAM_LANE_COUNT);
        TestCheckf(unpacked == spec, "Pack24 / Unpack24 must round-trip pattern %u", patternIndex);
    }
    TestCheck(CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT == CKFF_FRAGMENT_PROGRAM_VEC4_COUNT &&
                  CKFF_FRAGMENT_PROGRAM_LANE_COUNT == CKFF_FRAGMENT_PROGRAM_VEC4_COUNT * 4,
              "u_ffProgram must carry exactly the fragment program lanes");
    // fp32 exactness of every 24-bit integer: sample the full range densely
    // plus every value near the top.
    for (CKDWORD value = 0; value <= 0xFFFFFFu; value += 4093u) {
        TestCheckf((CKDWORD)(float)value == value, "24-bit value %u must be exact in fp32", value);
    }
    for (CKDWORD value = 0xFFFFF0u; value <= 0xFFFFFFu; ++value) {
        TestCheckf((CKDWORD)(float)value == value, "24-bit value %u must be exact in fp32", value);
    }
    printf("  coverage: packedLanes=%u\n", checkedLanes);
}

void TextureOpsAndArgsDriveTextureDependency()
{
    CKDWORD checked = 0;
    CKDWORD textureDependent = 0;

    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        for (CKDWORD opIndex = 0; opIndex < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageTextureOps); ++opIndex) {
            const CKDWORD op = kFFPCoverageTextureOps[opIndex].Value;
            const CKDWORD opMask = FFPCoverageTextureOpArgsMask(op);

            for (CKDWORD slot = 0; slot < 3; ++slot) {
                for (CKDWORD argIndex = 0; argIndex < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageTextureArgs); ++argIndex) {
                    const CKDWORD arg = kFFPCoverageTextureArgs[argIndex].Value;
                    CKFFFSStateDesc desc;
                    for (CKDWORD prior = 0; prior < stage; ++prior)
                        SetStageDefaults(desc, prior);
                    SetStageDefaults(desc, stage);
                    desc.SetStageColorOp(stage, op);
                    SetStageColorArg(desc, stage, slot, arg);

                    const bool slotUsed = (opMask & (1u << slot)) != 0;
                    const bool explicitTextureDependency = op != CKRST_TOP_DISABLE &&
                        slotUsed && FFPCoverageArgUsesTexture(arg);
                    const bool bumpTextureDependency = op == CKRST_TOP_BUMPENVMAP ||
                        op == CKRST_TOP_BUMPENVMAPLUMINANCE;
                    const bool expectedColorHasTexture = explicitTextureDependency ||
                        bumpTextureDependency;
                    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 1u << stage);

                    TestCheckf(key.Stages[stage].HasTexture == expectedColorHasTexture,
                               "Color op %s stage %u slot %u arg %s texture dependency mismatch",
                               kFFPCoverageTextureOps[opIndex].Name, stage, slot,
                               kFFPCoverageTextureArgs[argIndex].Name);
                    if (expectedColorHasTexture)
                        ++textureDependent;
                    ++checked;

                    desc = CKFFFSStateDesc();
                    for (CKDWORD prior = 0; prior < stage; ++prior)
                        SetStageDefaults(desc, prior);
                    SetStageDefaults(desc, stage);
                    desc.SetStageAlphaOp(stage, op);
                    SetStageAlphaArg(desc, stage, slot, arg);
                    key = CKFFBuildShaderKeyFS(desc, 1u << stage);

                    TestCheckf(key.Stages[stage].HasTexture == explicitTextureDependency,
                               "Alpha op %s stage %u slot %u arg %s texture dependency mismatch",
                               kFFPCoverageTextureOps[opIndex].Name, stage, slot,
                               kFFPCoverageTextureArgs[argIndex].Name);
                    ++checked;
                }
            }
        }
    }

    TestCheck(textureDependent > 0,
              "Texture dependency exhaustive test must include texture-dependent cases");
    printf("  coverage: textureDependencyCases=%u textureDependentCases=%u\n",
           checked, textureDependent);
}

void StageFragmentProgramPacksAllOpsAndArgs()
{
    CKDWORD checkedOps = 0;
    CKDWORD checkedArgs = 0;

    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        for (CKDWORD opIndex = 0; opIndex < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageTextureOps); ++opIndex) {
            CKFFFSStateDesc desc;
            for (CKDWORD prior = 0; prior <= stage; ++prior)
                SetStageDefaults(desc, prior);
            desc.SetStageColorOp(stage, kFFPCoverageTextureOps[opIndex].Value);
            desc.SetStageAlphaOp(stage, kFFPCoverageTextureOps[opIndex].Value);

            CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0);
            CKFFFragmentProgram program = BuildTestFragmentProgram(key);
            const CKDWORD op = kFFPCoverageTextureOps[opIndex].Value;
            if (op == CKRST_TOP_DISABLE && stage > 0) {
                TestCheckf(key.Stages[stage].ColorOp == CKRST_TOP_DISABLE &&
                               key.LastActiveTextureStage == stage - 1,
                           "Stage %u DISABLE must terminate the resolved stage chain",
                           stage);
                TestCheckf(program.GetStage(
                               stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP) == 0 &&
                               program.GetStage(
                               stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP) == 0,
                           "Stage %u terminal words must remain canonical zero",
                           stage);
            } else {
                TestCheckf(program.GetStage(
                               stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP) == op,
                           "Stage %u color op %s must pack into the fragment program",
                           stage, kFFPCoverageTextureOps[opIndex].Name);
                TestCheckf(program.GetStage(
                               stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP) == op,
                           "Stage %u alpha op %s must pack into the fragment program",
                           stage, kFFPCoverageTextureOps[opIndex].Name);
            }
            checkedOps += 2;
        }

        for (CKDWORD argIndex = 0; argIndex < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageTextureArgs); ++argIndex) {
            CKFFFSStateDesc desc;
            for (CKDWORD prior = 0; prior <= stage; ++prior)
                SetStageDefaults(desc, prior);
            desc.SetStageColorOp(stage, CKRST_TOP_MULTIPLYADD);
            desc.SetStageAlphaOp(stage, CKRST_TOP_MULTIPLYADD);
            desc.SetStageColorArg0(stage, kFFPCoverageTextureArgs[argIndex].Value);
            desc.SetStageColorArg1(stage, kFFPCoverageTextureArgs[argIndex].Value);
            desc.SetStageColorArg2(stage, kFFPCoverageTextureArgs[argIndex].Value);
            desc.SetStageAlphaArg0(stage, kFFPCoverageTextureArgs[argIndex].Value);
            desc.SetStageAlphaArg1(stage, kFFPCoverageTextureArgs[argIndex].Value);
            desc.SetStageAlphaArg2(stage, kFFPCoverageTextureArgs[argIndex].Value);

            CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 1u << stage);
            CKFFFragmentProgram program = BuildTestFragmentProgram(key);
            const CKDWORD expected = CKFFFragmentProgram::RepackArg(kFFPCoverageTextureArgs[argIndex].Value);
            TestCheckf(program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG0) == expected &&
                           program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1) == expected &&
                           program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG2) == expected &&
                           program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG0) == expected &&
                           program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1) == expected &&
                           program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG2) == expected,
                       "Stage %u arg %s must pack into every color/alpha arg field",
                       stage, kFFPCoverageTextureArgs[argIndex].Name);
            TestCheckf(CKFFFragmentProgram::UnpackArg(expected) == kFFPCoverageTextureArgs[argIndex].Value,
                       "Arg %s must survive the 5-bit repack", kFFPCoverageTextureArgs[argIndex].Name);
            checkedArgs += 6;
        }
    }

    printf("  coverage: stageOps=%u stageArgs=%u\n", checkedOps, checkedArgs);
}

void SamplerAndStageFlagsPackAcrossAllStages()
{
    CKDWORD checkedSamplerTypes = 0;
    CKDWORD checkedCompareFuncs = 0;
    CKDWORD checkedProjected = 0;
    CKDWORD checkedMirror = 0;

    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        for (CKDWORD samplerIndex = 0; samplerIndex < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageSamplerTypes); ++samplerIndex) {
            CKFFFSStateDesc desc;
            for (CKDWORD prior = 0; prior <= stage; ++prior)
                SetStageDefaults(desc, prior);
            desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
            desc.SetStageSamplerType(stage, kFFPCoverageSamplerTypes[samplerIndex].Value);

            CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 1u << stage);
            CKFFFragmentProgram program = BuildTestFragmentProgram(key);
            const CKDWORD maskValue = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE);
            TestCheckf(maskValue == kFFPCoverageSamplerTypes[samplerIndex].Value,
                       "Stage %u sampler type %s must pack into its fragment-program field",
                       stage, kFFPCoverageSamplerTypes[samplerIndex].Name);
            ++checkedSamplerTypes;
        }

        for (CKDWORD funcIndex = 0; funcIndex < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageSamplerCompareFuncs); ++funcIndex) {
            CKFFFSStateDesc desc;
            for (CKDWORD prior = 0; prior <= stage; ++prior)
                SetStageDefaults(desc, prior);
            desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
            desc.SetStageSamplerType(stage, CKFF_SAMPLER_DEPTH);
            desc.SetStageSamplerCompareFunc(stage, kFFPCoverageSamplerCompareFuncs[funcIndex].Value);

            CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 1u << stage);
            CKFFFragmentProgram program = BuildTestFragmentProgram(key);
            const CKDWORD maskValue = program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC);
            TestCheckf(maskValue == kFFPCoverageSamplerCompareFuncs[funcIndex].Value,
                       "Stage %u sampler compare %s must pack into its fragment-program field",
                       stage, kFFPCoverageSamplerCompareFuncs[funcIndex].Name);
            ++checkedCompareFuncs;
        }

        CKFFFSStateDesc desc;
        for (CKDWORD prior = 0; prior <= stage; ++prior)
            SetStageDefaults(desc, prior);
        desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageProjectedSampler(stage, true);
        CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 1u << stage);
        CKFFFragmentProgram program = BuildTestFragmentProgram(key);
        TestCheckf(program.GetStage(stage, CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED) == 1,
                   "Stage %u projected sampler must enter its fragment-program field", stage);
        ++checkedProjected;

        CKFFFragmentProgram mirrorBaseline;
        for (CKDWORD mirror = 0; mirror < 8; ++mirror) {
            desc = CKFFFSStateDesc();
            for (CKDWORD prior = 0; prior <= stage; ++prior)
                SetStageDefaults(desc, prior);
            desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
            desc.SetStageMirrorOnceMask(stage, mirror);
            key = CKFFBuildShaderKeyFS(desc, 1u << stage);
            program = BuildTestFragmentProgram(key);
            if (mirror == 0)
                mirrorBaseline = program;
            TestCheckf(program == mirrorBaseline &&
                           key.Stages[stage].MirrorOnceMask == mirror,
                       "Stage %u mirror-once mask %u must remain in stage params without splitting the fragment program",
                       stage, mirror);
            ++checkedMirror;
        }
    }

    printf("  coverage: samplerTypes=%u samplerCompareFuncs=%u projected=%u mirror=%u\n",
           checkedSamplerTypes, checkedCompareFuncs, checkedProjected, checkedMirror);
}

void VertexStateDomainPacksIntoShaderKey()
{
    CKDWORD checked = 0;

    for (CKDWORD positionT = 0; positionT <= 1; ++positionT) {
        for (CKDWORD lighting = 0; lighting <= 1; ++lighting) {
            for (CKDWORD clip = 0; clip <= 1; ++clip) {
                CKFFVSStateDesc desc;
                desc.SetHasPosition(positionT == 0);
                desc.SetHasPositionT(positionT != 0);
                desc.SetHasNormal(positionT == 0);
                desc.SetHasColor0(true);
                desc.SetLightingEnabled(lighting != 0);
                desc.SetVertexClipping(clip != 0);
                CKFFShaderKeyVS key(desc);
                TestCheckf(key.GetHasPositionT() == (positionT != 0),
                           "POSITIONT bit must round-trip through VS shader key");
                TestCheckf(((key.Bits >> 13) & 1u) == lighting,
                           "Lighting bit must round-trip through VS shader key");
                TestCheckf(((key.Bits >> 34) & 1u) == clip,
                           "Clip bit must round-trip through VS shader key");
                ++checked;
            }
        }
    }

    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        for (CKDWORD count = 0; count <= 4; ++count) {
            CKFFVSStateDesc desc;
            desc.SetTexcoordComponentCount(stage, count);
            CKFFShaderKeyVS key(desc);
            TestCheckf(((key.VertexTexcoordDeclMask >> (stage * 3u)) & 7u) == count,
                       "Texcoord component count stage %u value %u must round-trip",
                       stage, count);
            ++checked;
        }
        for (CKDWORD texGen = 0; texGen < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageTexGenModes); ++texGen) {
            CKFFVSStateDesc desc;
            desc.SetTexGen(stage, kFFPCoverageTexGenModes[texGen].Value, true);
            CKFFShaderKeyVS key(desc);
            TestCheckf((key.TexGen[stage] & 7u) == kFFPCoverageTexGenModes[texGen].Value &&
                           ((key.TexGen[stage] & 8u) != 0),
                       "TexGen stage %u mode %s must round-trip",
                       stage, kFFPCoverageTexGenModes[texGen].Name);
            ++checked;
        }
    }

    for (CKDWORD source = 0; source < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageMaterialSources); ++source) {
        CKFFVSStateDesc desc;
        desc.SetDiffuseSource(kFFPCoverageMaterialSources[source].Value);
        desc.SetAmbientSource(kFFPCoverageMaterialSources[source].Value);
        desc.SetSpecularSource(kFFPCoverageMaterialSources[source].Value);
        desc.SetEmissiveSource(kFFPCoverageMaterialSources[source].Value);
        CKFFShaderKeyVS key(desc);
        TestCheckf(desc.GetDiffuseSource() == kFFPCoverageMaterialSources[source].Value &&
                       desc.GetAmbientSource() == kFFPCoverageMaterialSources[source].Value &&
                       desc.GetSpecularSource() == kFFPCoverageMaterialSources[source].Value &&
                       desc.GetEmissiveSource() == kFFPCoverageMaterialSources[source].Value &&
                       key.Bits == desc.bits,
                   "Material source %s must stay in VS shader key bits",
                   kFFPCoverageMaterialSources[source].Name);
        ++checked;
    }

    for (CKDWORD mode = 0; mode < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageVertexBlendModes); ++mode) {
        for (CKDWORD indexed = 0; indexed <= 1; ++indexed) {
            for (CKDWORD count = 0; count < 4; ++count) {
                CKFFVSStateDesc desc;
                desc.SetVertexBlendMode(kFFPCoverageVertexBlendModes[mode].Value);
                desc.SetVertexBlendIndexed(indexed != 0);
                desc.SetVertexBlendCount(count);
                CKFFShaderKeyVS key(desc);
                TestCheckf(((key.Bits >> 35) & 3u) == kFFPCoverageVertexBlendModes[mode].Value &&
                               ((key.Bits >> 37) & 1u) == indexed &&
                               ((key.Bits >> 38) & 3u) == count,
                           "Vertex blend mode %s indexed %u count %u must round-trip",
                           kFFPCoverageVertexBlendModes[mode].Name, indexed, count);
                ++checked;
            }
        }
    }

    printf("  coverage: vertexStateCases=%u\n", checked);
}

void DomainTablesCoverCurrentEnumShape()
{
    TestCheck(FFPCoverageArrayCount(kFFPCoverageTextureOps) == 26,
              "Texture op domain must list every named CKRST_TOP value");
    TestCheck(kFFPCoverageTextureOps[0].Value == CKRST_TOP_DISABLE &&
                  kFFPCoverageTextureOps[25].Value == CKRST_TOP_LERP &&
                  CKRST_TOP_LERP == 26,
              "Texture op domain must stay aligned with VxDefines CKRST_TOP range");
    TestCheck(FFPCoverageArrayCount(kFFPCoverageTextureArgs) == 28,
              "Texture arg domain must include every base arg times every modifier combination");
    TestCheck(FFPCoverageArrayCount(kFFPCoverageSamplerTypes) == 4,
              "Sampler type domain must list every CKFF sampler type");
    TestCheck(FFPCoverageArrayCount(kFFPCoverageVxCompareFuncs) == 8,
              "Vx compare func domain must list every VXCMP function");
    TestCheck(FFPCoverageArrayCount(kFFPCoverageSamplerCompareFuncs) == 9,
              "Sampler compare domain must include NONE plus every compare function");
}

} // namespace

int main()
{
    TestFramework tests;
    tests.Run("Domain tables cover current enum shape", &DomainTablesCoverCurrentEnumShape);
    tests.Run("Fragment program layout fields are disjoint and round-trip",
              &FragmentProgramLayoutFieldsAreDisjointAndRoundTrip);
    tests.Run("Spec Pack24 is exact in fp32", &SpecPack24IsExactInFp32);
    tests.Run("Texture ops and args drive texture dependency", &TextureOpsAndArgsDriveTextureDependency);
    tests.Run("Stage fragment program packs all ops and args", &StageFragmentProgramPacksAllOpsAndArgs);
    tests.Run("Sampler and stage flags pack across all stages", &SamplerAndStageFlagsPackAcrossAllStages);
    tests.Run("Vertex state domain packs into shader key", &VertexStateDomainPacksIntoShaderKey);
    return tests.ExitCode();
}
