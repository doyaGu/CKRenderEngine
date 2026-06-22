#include "FFPCoverageDomain.h"
#include "TestTriangleMultiset.h"

#include "CKFFSamplerLayout.h"
#include "CKFFSpecializedModuleTable.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

char g_SamplerLayoutFailure[512];

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(g_SamplerLayoutFailure, sizeof(g_SamplerLayoutFailure), format, args);
    va_end(args);
    TestFail(g_SamplerLayoutFailure);
}

CKDWORD LayoutTypeFromBits(CKDWORD bits, CKDWORD stage)
{
    return (bits >> (stage * 2u)) & 3u;
}

void FillKeyFromRawLayout(CKFFShaderKeyFS &key, CKDWORD rawLayout, CKDWORD inactiveStage)
{
    key = CKFFShaderKeyFS();
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        key.Stages[stage].HasTexture = stage != inactiveStage;
        key.Stages[stage].SamplerType = LayoutTypeFromBits(rawLayout, stage);
    }
}

CKDWORD ExpectedNormalizedLayout(CKDWORD rawLayout, CKDWORD inactiveStage)
{
    CKDWORD expected = 0;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        const bool active = stage != inactiveStage;
        const CKDWORD rawType = LayoutTypeFromBits(rawLayout, stage);
        const CKDWORD normalized = FFPCoverageNormalizeLayoutType(rawType, active);
        expected |= (normalized & 3u) << (stage * 2u);
    }
    return expected;
}

bool ExpectedNeedsType(CKDWORD layoutBits, CKDWORD type)
{
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        if (LayoutTypeFromBits(layoutBits, stage) == type)
            return true;
    }
    return false;
}

void EveryRawSamplerLayoutNormalizesDeterministically()
{
    CKDWORD checkedLayouts = 0;
    CKDWORD checkedInactiveVariants = 0;
    CKDWORD cubeLayouts = 0;
    CKDWORD volumeLayouts = 0;
    CKDWORD mixedLayouts = 0;

    for (CKDWORD raw = 0; raw < 0x10000u; ++raw) {
        CKFFShaderKeyFS key;
        FillKeyFromRawLayout(key, raw, CKFF_STATE_DESC_TEXTURE_STAGES);
        const CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key);
        const CKDWORD expected = ExpectedNormalizedLayout(raw, CKFF_STATE_DESC_TEXTURE_STAGES);

        TestCheckf(layout.Bits == expected,
                   "Raw sampler layout 0x%04X normalized to 0x%04X, expected 0x%04X",
                   raw, layout.Bits, expected);

        const bool needsCube = ExpectedNeedsType(expected, CKFF_SAMPLER_CUBE);
        const bool needsVolume = ExpectedNeedsType(expected, CKFF_SAMPLER_VOLUME);
        TestCheckf(CKFFSamplerLayoutNeedsCubeSampler(layout) == needsCube,
                   "Raw sampler layout 0x%04X cube detection mismatch", raw);
        TestCheckf(CKFFSamplerLayoutNeedsVolumeSampler(layout) == needsVolume,
                   "Raw sampler layout 0x%04X volume detection mismatch", raw);
        TestCheckf(CKFFSamplerLayoutNeedsMixedCubeVolume(layout) == (needsCube && needsVolume),
                   "Raw sampler layout 0x%04X mixed detection mismatch", raw);
        if (needsCube)
            ++cubeLayouts;
        if (needsVolume)
            ++volumeLayouts;
        if (needsCube && needsVolume)
            ++mixedLayouts;

        for (CKDWORD inactiveStage = 0; inactiveStage < CKFF_STATE_DESC_TEXTURE_STAGES; ++inactiveStage) {
            FillKeyFromRawLayout(key, raw, inactiveStage);
            const CKFFSamplerLayoutKey inactiveLayout = CKFFBuildSamplerLayoutKey(key);
            const CKDWORD inactiveExpected = ExpectedNormalizedLayout(raw, inactiveStage);
            TestCheckf(inactiveLayout.Bits == inactiveExpected,
                       "Raw sampler layout 0x%04X inactive stage %u normalized to 0x%04X, expected 0x%04X",
                       raw, inactiveStage, inactiveLayout.Bits, inactiveExpected);
            TestCheckf(CKFFSamplerLayoutStageType(inactiveLayout, inactiveStage) == CKFF_SAMPLER_2D,
                       "Inactive stage %u must normalize to 2D in raw layout 0x%04X",
                       inactiveStage, raw);
            ++checkedInactiveVariants;
        }

        ++checkedLayouts;
    }

    TestCheck(checkedLayouts == 65536u,
              "Sampler layout exhaustive test must visit every 4^8 raw layout");
    printf("  coverage: rawLayouts=%u inactiveVariants=%u cubeLayouts=%u volumeLayouts=%u mixedLayouts=%u\n",
           checkedLayouts, checkedInactiveVariants, cubeLayouts, volumeLayouts, mixedLayouts);
}

void GeneratedSamplerLayoutManifestHasConsistentBackendCoverage()
{
    CKDWORD manifestLayouts = 0;
    CKDWORD backendHits = 0;
    bool countedLayouts[0x10000] = {};

    for (CKDWORD raw = 0; raw < 0x10000u; ++raw) {
        CKFFShaderKeyFS key;
        FillKeyFromRawLayout(key, raw, CKFF_STATE_DESC_TEXTURE_STAGES);
        const CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key);

        bool anyHit = false;
        bool allHit = true;
        for (CKDWORD backend = 0; backend < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageBackends); ++backend) {
            CKFFSamplerLayoutModule module;
            const bool found = CKFFFindSamplerLayoutModule(
                layout, kFFPCoverageBackends[backend].Profile, module);
            anyHit = anyHit || found;
            allHit = allHit && found;
            if (found) {
                TestCheckf(module.FSData != nullptr && module.FSSize > 0,
                           "Sampler layout 0x%04X backend %s hit must have a fragment shader blob",
                           layout.Bits, kFFPCoverageBackends[backend].Name);
                ++backendHits;
            }
        }

        TestCheckf(!anyHit || allHit,
                   "Sampler layout 0x%04X manifest coverage must be all-backend or no-backend",
                   layout.Bits);
        if (anyHit && !countedLayouts[layout.Bits]) {
            countedLayouts[layout.Bits] = true;
            ++manifestLayouts;
        }
    }

    TestCheckf(manifestLayouts == CKFFSamplerLayoutModuleCount() / FFPCoverageArrayCount(kFFPCoverageBackends),
               "Manifest layout count %u must match generated table count %u divided by backend count",
               manifestLayouts, (unsigned)CKFFSamplerLayoutModuleCount());
    printf("  coverage: manifestLayouts=%u backendHits=%u generatedEntries=%u\n",
           manifestLayouts, backendHits, (unsigned)CKFFSamplerLayoutModuleCount());
}

void SamplerLayoutDiagnosticsCoverAllStageTypes()
{
    for (CKDWORD raw = 0; raw < 0x10000u; raw += 0x1111u) {
        CKFFShaderKeyFS key;
        FillKeyFromRawLayout(key, raw, CKFF_STATE_DESC_TEXTURE_STAGES);
        const CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key);

        char stageTypes[32];
        char manifestEntry[128];
        CKFFFormatSamplerLayoutStageTypes(layout, stageTypes, sizeof(stageTypes));
        CKFFFormatSamplerLayoutManifestEntry(layout, "dx11", manifestEntry, sizeof(manifestEntry));

        TestCheckf(stageTypes[0] == '[' && stageTypes[1] != '\0',
                   "Sampler layout 0x%04X must format stage type diagnostics", raw);
        TestCheckf(manifestEntry[0] == '{' && manifestEntry[1] != '\0',
                   "Sampler layout 0x%04X must format manifest diagnostics", raw);
    }

    printf("  coverage: diagnosticSampleLayouts=16\n");
}

} // namespace

int main()
{
    TestFramework tests;
    tests.Run("Every raw sampler layout normalizes deterministically",
              &EveryRawSamplerLayoutNormalizesDeterministically);
    tests.Run("Generated sampler layout manifest has consistent backend coverage",
              &GeneratedSamplerLayoutManifestHasConsistentBackendCoverage);
    tests.Run("Sampler layout diagnostics cover all stage types",
              &SamplerLayoutDiagnosticsCoverAllStageTypes);
    return tests.ExitCode();
}
