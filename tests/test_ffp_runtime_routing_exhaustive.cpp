#include "FFPCoverageDomain.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

#include "CKFFSamplerLayout.h"
#include "CKFFShaderCache.h"
#include "CKFFSpecializedModuleTable.h"
#include "CKRenderSettings.h"

#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

char g_RuntimeRoutingFailure[512];

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(g_RuntimeRoutingFailure, sizeof(g_RuntimeRoutingFailure), format, args);
    va_end(args);
    TestFail(g_RuntimeRoutingFailure);
}

bool SameSpecialization(const CKFFSpecializationInfo &lhs,
                        const CKFFSpecializationInfo &rhs)
{
    if (lhs.DwordCount() != rhs.DwordCount())
        return false;
    for (CKDWORD i = 0; i < lhs.DwordCount(); ++i) {
        if (lhs.Data()[i] != rhs.Data()[i])
            return false;
    }
    return true;
}

void TestProgramSpecialization(const FFPDiagnosticContext &context,
                               const CKFFSpecializationInfo &expected,
                               const char *route,
                               const char *backend)
{
    TestCheckf(context.LastProgramSpecializationDwords.size() == expected.DwordCount(),
               "%s backend %s must submit %u specialization dwords",
               route, backend, expected.DwordCount());
    for (CKDWORD i = 0; i < expected.DwordCount(); ++i) {
        TestCheckf(context.LastProgramSpecializationDwords[i] == expected.Data()[i],
                   "%s backend %s specialization dword %u mismatch",
                   route, backend, i);
    }
}

void TestSecondLookupUsesCache(CKFFShaderCache &cache,
                               FFPDiagnosticContext &context,
                               const CKFFShaderKey &key,
                               const CKFFProgramBinding &first,
                               const char *route,
                               const char *backend)
{
    const CKDWORD createdPrograms = context.CreatedProgramCount;
    const CKFFProgramBinding second = cache.GetProgram(key);
    TestCheckf(second.Program == first.Program,
               "%s backend %s second lookup must return cached program",
               route, backend);
    TestCheckf(second.FullSpecialized == first.FullSpecialized,
               "%s backend %s second lookup must preserve route type",
               route, backend);
    TestCheckf(context.CreatedProgramCount == createdPrograms,
               "%s backend %s second lookup must not create a new program",
               route, backend);
}

void SetSelectStage(CKFFShaderKeyFSStage &stage,
                    CKDWORD arg,
                    bool hasTexture,
                    CKDWORD samplerType)
{
    stage = CKFFShaderKeyFSStage();
    stage.ColorOp = CKRST_TOP_SELECTARG1;
    stage.ColorArg1 = arg;
    stage.AlphaOp = CKRST_TOP_SELECTARG1;
    stage.AlphaArg1 = arg;
    stage.HasTexture = hasTexture;
    stage.SamplerType = samplerType;
}

const CKFFSpecializedModuleEntry *FirstDx11GeneratedEntry()
{
    for (size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        if (g_CKFFSpecializedModules[i].Profile == CKRST_SHADER_PROFILE_DX11)
            return &g_CKFFSpecializedModules[i];
    }
    return nullptr;
}

CKFFShaderKey MakeFallbackKey(CKDWORD lastStage, CKDWORD routeKind)
{
    const CKFFSpecializedModuleEntry *entry = FirstDx11GeneratedEntry();
    TestCheck(entry != nullptr,
              "Runtime routing exhaustive test requires a generated DX11 baseline key");

    CKFFShaderKey key;
    key.VS = entry->Key.VS;
    key.FS = CKFFShaderKeyFS();

    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage)
        SetSelectStage(key.FS.Stages[stage], CKRST_TA_CURRENT, false, CKFF_SAMPLER_2D);

    if (routeKind == 0) {
        SetSelectStage(key.FS.Stages[lastStage], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_2D);
    } else if (routeKind == 1) {
        SetSelectStage(key.FS.Stages[lastStage], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_VOLUME);
    } else {
        SetSelectStage(key.FS.Stages[0], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_VOLUME);
        SetSelectStage(key.FS.Stages[1], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_CUBE);
    }
    key.FS.LastActiveTextureStage = lastStage;
    return key;
}

void EveryGeneratedFullSpecializedKeyRoutesForEveryBackend()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader", "0");

    CKDWORD checkedRoutes = 0;
    for (CKDWORD backend = 0; backend < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageBackends); ++backend) {
        FFPDiagnosticDriver driver(kFFPCoverageBackends[backend].Profile);
        FFPDiagnosticContext context(&driver);
        CKFFShaderCache cache;
        cache.Init(&context);

        for (size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
            const CKFFSpecializedModuleEntry &baseline = g_CKFFSpecializedModules[i];
            if (baseline.Profile != CKRST_SHADER_PROFILE_DX11)
                continue;

            CKFFSpecializedModule module;
            TestCheckf(CKFFFindSpecializedModule(
                           baseline.Key, kFFPCoverageBackends[backend].Profile, module),
                       "Backend %s must contain generated key %u",
                       kFFPCoverageBackends[backend].Name, (unsigned)i);

            const CKDWORD createdPrograms = context.CreatedProgramCount;
            const CKFFProgramBinding binding = cache.GetProgram(baseline.Key);
            TestCheckf(binding.Program != 0 && binding.FullSpecialized,
                       "Backend %s generated key %u must route full-specialized",
                       kFFPCoverageBackends[backend].Name, (unsigned)i);
            TestCheckf(SameSpecialization(binding.Specialization, module.Specialization),
                       "Backend %s generated key %u binding specialization mismatch",
                       kFFPCoverageBackends[backend].Name, (unsigned)i);
            if (context.CreatedProgramCount != createdPrograms)
                TestProgramSpecialization(context, module.Specialization,
                                          "generated full-specialized",
                                          kFFPCoverageBackends[backend].Name);
            TestSecondLookupUsesCache(cache, context, baseline.Key, binding,
                                      "generated full-specialized",
                                      kFFPCoverageBackends[backend].Name);
            ++checkedRoutes;
        }

        cache.Shutdown();
    }

    CKRenderSettingsClearOverridesForTests();
    printf("  coverage: generatedFullSpecializedRoutes=%u\n", checkedRoutes);
}

void StageFourThroughSevenFallbackRoutesForEveryBackend()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader", "0");

    CKDWORD checkedRoutes = 0;
    for (CKDWORD backend = 0; backend < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageBackends); ++backend) {
        FFPDiagnosticDriver driver(kFFPCoverageBackends[backend].Profile);
        FFPDiagnosticContext context(&driver);
        CKFFShaderCache cache;
        cache.Init(&context);

        for (CKDWORD lastStage = 4; lastStage < CKFF_STATE_DESC_TEXTURE_STAGES; ++lastStage) {
            for (CKDWORD routeKind = 0; routeKind < 3; ++routeKind) {
                const CKFFShaderKey key = MakeFallbackKey(lastStage, routeKind);
                const CKFFSpecializationInfo expected = CKFFBuildSpecializationInfo(key.FS);
                const CKDWORD createdPrograms = context.CreatedProgramCount;
                const CKFFProgramBinding binding = cache.GetProgram(key);
                TestCheckf(binding.Program != 0 && !binding.FullSpecialized,
                           "Backend %s stage %u fallback kind %u must route non-full-specialized",
                           kFFPCoverageBackends[backend].Name, lastStage, routeKind);
                TestCheckf(SameSpecialization(binding.Specialization, expected),
                           "Backend %s stage %u fallback kind %u specialization mismatch",
                           kFFPCoverageBackends[backend].Name, lastStage, routeKind);
                if (context.CreatedProgramCount != createdPrograms)
                    TestProgramSpecialization(context, expected,
                                              "stage 4-7 fallback",
                                              kFFPCoverageBackends[backend].Name);
                TestSecondLookupUsesCache(cache, context, key, binding,
                                          "stage 4-7 fallback",
                                          kFFPCoverageBackends[backend].Name);
                ++checkedRoutes;
            }
        }

        cache.Shutdown();
    }

    CKRenderSettingsClearOverridesForTests();
    printf("  coverage: fallbackRoutes=%u\n", checkedRoutes);
}

void UberOverrideRoutesEveryGeneratedKeyForEveryBackend()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader", "1");

    CKDWORD checkedRoutes = 0;
    for (CKDWORD backend = 0; backend < (CKDWORD)FFPCoverageArrayCount(kFFPCoverageBackends); ++backend) {
        FFPDiagnosticDriver driver(kFFPCoverageBackends[backend].Profile);
        FFPDiagnosticContext context(&driver);
        CKFFShaderCache cache;
        cache.Init(&context);

        for (size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
            const CKFFSpecializedModuleEntry &baseline = g_CKFFSpecializedModules[i];
            if (baseline.Profile != CKRST_SHADER_PROFILE_DX11)
                continue;

            const CKFFSpecializationInfo expected = CKFFBuildSpecializationInfo(baseline.Key.FS);
            const CKDWORD createdPrograms = context.CreatedProgramCount;
            const CKFFProgramBinding binding = cache.GetProgram(baseline.Key);
            TestCheckf(binding.Program != 0 && !binding.FullSpecialized,
                       "Backend %s generated key %u must route non-full-specialized in uber mode",
                       kFFPCoverageBackends[backend].Name, (unsigned)i);
            TestCheckf(SameSpecialization(binding.Specialization, expected),
                       "Backend %s generated key %u uber specialization mismatch",
                       kFFPCoverageBackends[backend].Name, (unsigned)i);
            if (context.CreatedProgramCount != createdPrograms)
                TestProgramSpecialization(context, expected,
                                          "uber override",
                                          kFFPCoverageBackends[backend].Name);
            TestSecondLookupUsesCache(cache, context, baseline.Key, binding,
                                      "uber override",
                                      kFFPCoverageBackends[backend].Name);
            ++checkedRoutes;
        }

        cache.Shutdown();
    }

    CKRenderSettingsClearOverridesForTests();
    printf("  coverage: uberOverrideRoutes=%u\n", checkedRoutes);
}

} // namespace

int main()
{
    TestFramework tests;
    tests.Run("Every generated full-specialized key routes for every backend",
              &EveryGeneratedFullSpecializedKeyRoutesForEveryBackend);
    tests.Run("Stage four through seven fallback routes for every backend",
              &StageFourThroughSevenFallbackRoutesForEveryBackend);
    tests.Run("Uber override routes every generated key for every backend",
              &UberOverrideRoutesEveryGeneratedKeyForEveryBackend);
    return tests.ExitCode();
}
