#include "FFPCoverageDomain.h"
#include "TestTriangleMultiset.h"

#include "CKFFSamplerLayout.h"
#include "CKFFSpecializationInfo.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

char g_FFPCoverageFailure[512];

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

CKDWORD RepackedStageSpecValue(const CKFFSpecializationInfo &spec, CKDWORD stage, CKDWORD field)
{
    const CKFFSpecConstantId id = (CKFFSpecConstantId)(
        (CKDWORD)CKFF_SPEC_STAGE0_COLOR_OP + stage * 9u + field);
    return spec.Get(id);
}

void SpecBitfieldsRoundTripSupportedValues()
{
    CKDWORD checkedIds = 0;
    CKDWORD checkedValues = 0;

    for (CKDWORD idValue = 0; idValue < (CKDWORD)CKFF_SPEC_CONSTANT_COUNT; ++idValue) {
        const CKFFSpecConstantId id = (CKFFSpecConstantId)idValue;
        const CKFFSpecBitfield layout = CKFFSpecializationInfo::Layout(id);
        TestCheckf(layout.BitCount > 0 && layout.DwordOffset < CKFFSpecializationInfo::MaxSpecDwords,
                   "Spec id %u must have a valid bitfield layout", idValue);
        ++checkedIds;

        if (layout.BitCount < 32) {
            const CKDWORD maxValue = 1u << layout.BitCount;
            for (CKDWORD value = 0; value < maxValue; ++value) {
                CKFFSpecializationInfo spec;
                spec.Set(id, value);
                TestCheckf(spec.Get(id) == value,
                           "Spec id %u value %u must round-trip", idValue, value);
                ++checkedValues;
            }
        } else {
            static const CKDWORD kWideValues[] = {
                0x00000000u, 0xffffffffu, 0x01234567u, 0x76543210u,
                0x11111111u, 0x88888888u,
            };
            for (CKDWORD i = 0; i < (CKDWORD)FFPCoverageArrayCount(kWideValues); ++i) {
                CKFFSpecializationInfo spec;
                spec.Set(id, kWideValues[i]);
                TestCheckf(spec.Get(id) == kWideValues[i],
                           "Spec id %u wide value 0x%08X must round-trip",
                           idValue, kWideValues[i]);
                ++checkedValues;
            }
            for (CKDWORD nibble = 0; nibble < 8; ++nibble) {
                for (CKDWORD value = 0; value < 16; ++value) {
                    const CKDWORD packed = value << (nibble * 4u);
                    CKFFSpecializationInfo spec;
                    spec.Set(id, packed);
                    TestCheckf(spec.Get(id) == packed,
                               "Spec id %u nibble %u value %u must round-trip",
                               idValue, nibble, value);
                    ++checkedValues;
                }
            }
        }
    }

    TestCheckf(checkedIds == (CKDWORD)CKFF_SPEC_CONSTANT_COUNT,
               "Spec coverage must visit every id, visited %u", checkedIds);
    printf("  coverage: specIds=%u specValues=%u\n", checkedIds, checkedValues);
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

void StageSpecializationPacksAllOpsAndArgs()
{
    CKDWORD checkedOps = 0;
    CKDWORD checkedArgs = 0;

    for (CKDWORD stage = 0; stage < 4; ++stage) {
        for (CKDWORD opIndex = 0; opIndex < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageTextureOps); ++opIndex) {
            CKFFFSStateDesc desc;
            for (CKDWORD prior = 0; prior <= stage; ++prior)
                SetStageDefaults(desc, prior);
            desc.SetStageColorOp(stage, kFFPCoverageTextureOps[opIndex].Value);
            desc.SetStageAlphaOp(stage, kFFPCoverageTextureOps[opIndex].Value);

            CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0);
            CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);
            TestCheckf(RepackedStageSpecValue(spec, stage, 0) == kFFPCoverageTextureOps[opIndex].Value,
                       "Stage %u color op %s must pack into specialization",
                       stage, kFFPCoverageTextureOps[opIndex].Name);
            TestCheckf(RepackedStageSpecValue(spec, stage, 4) == kFFPCoverageTextureOps[opIndex].Value,
                       "Stage %u alpha op %s must pack into specialization",
                       stage, kFFPCoverageTextureOps[opIndex].Name);
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
            CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);
            const CKDWORD expected = CKFFSpecializationInfo::RepackArg(kFFPCoverageTextureArgs[argIndex].Value);
            TestCheckf(RepackedStageSpecValue(spec, stage, 1) == expected &&
                           RepackedStageSpecValue(spec, stage, 2) == expected &&
                           RepackedStageSpecValue(spec, stage, 3) == expected &&
                           RepackedStageSpecValue(spec, stage, 5) == expected &&
                           RepackedStageSpecValue(spec, stage, 6) == expected &&
                           RepackedStageSpecValue(spec, stage, 7) == expected,
                       "Stage %u arg %s must pack into every color/alpha arg field",
                       stage, kFFPCoverageTextureArgs[argIndex].Name);
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
            CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);
            const CKDWORD maskValue = (spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) >> (stage * 2u)) & 3u;
            TestCheckf(maskValue == kFFPCoverageSamplerTypes[samplerIndex].Value,
                       "Stage %u sampler type %s must pack into sampler type mask",
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
            CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);
            const CKDWORD maskValue = (spec.Get(CKFF_SPEC_SAMPLER_COMPARE_FUNC_MASK) >> (stage * 4u)) & 0xFu;
            TestCheckf(maskValue == kFFPCoverageSamplerCompareFuncs[funcIndex].Value,
                       "Stage %u sampler compare %s must pack into compare mask",
                       stage, kFFPCoverageSamplerCompareFuncs[funcIndex].Name);
            ++checkedCompareFuncs;
        }

        CKFFFSStateDesc desc;
        for (CKDWORD prior = 0; prior <= stage; ++prior)
            SetStageDefaults(desc, prior);
        desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
        desc.SetStageProjectedSampler(stage, true);
        CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 1u << stage);
        CKFFSpecializationInfo spec = CKFFBuildSpecializationInfo(key);
        const bool expectedProjected = stage < 4;
        const bool projectedPacked = (spec.Get(CKFF_SPEC_PROJECTED_SAMPLER_MASK) & (1u << stage)) != 0;
        TestCheckf(projectedPacked == expectedProjected,
                   "Stage %u projected sampler must %s specialization mask",
                   stage, expectedProjected ? "enter" : "stay out of");
        ++checkedProjected;

        for (CKDWORD mirror = 0; mirror < 8; ++mirror) {
            desc = CKFFFSStateDesc();
            for (CKDWORD prior = 0; prior <= stage; ++prior)
                SetStageDefaults(desc, prior);
            desc.SetStageColorArg1(stage, CKRST_TA_TEXTURE);
            desc.SetStageMirrorOnceMask(stage, mirror);
            key = CKFFBuildShaderKeyFS(desc, 1u << stage);
            spec = CKFFBuildSpecializationInfo(key);
            const CKDWORD packed = (spec.Get(CKFF_SPEC_MIRRORONCE_SAMPLER_MASK) >> (stage * 3u)) & 7u;
            TestCheckf(packed == (stage < 4 ? mirror : 0u),
                       "Stage %u mirror-once mask %u must follow first-four-stage specialization ABI",
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
    tests.Run("Spec bitfields round-trip supported values", &SpecBitfieldsRoundTripSupportedValues);
    tests.Run("Texture ops and args drive texture dependency", &TextureOpsAndArgsDriveTextureDependency);
    tests.Run("Stage specialization packs all ops and args", &StageSpecializationPacksAllOpsAndArgs);
    tests.Run("Sampler and stage flags pack across all stages", &SamplerAndStageFlagsPackAcrossAllStages);
    tests.Run("Vertex state domain packs into shader key", &VertexStateDomainPacksIntoShaderKey);
    return tests.ExitCode();
}
