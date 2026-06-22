#include "CKFFSpecializedModuleTable.h"
#include "CKFFSamplerLayout.h"
#include "CKFFShaderCache.h"
#include "CKFFShaderKey.h"
#include "CKRenderSettings.h"
#include "CKRasterizerEnums.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace {

struct BackendProfile {
    const char *Name;
    CK_SHADER_PROFILE Profile;
};

static const BackendProfile kBackends[] = {
    {"dx11", CKRST_SHADER_PROFILE_DX11},
    {"dx12", CKRST_SHADER_PROFILE_DX12},
    {"spirv", CKRST_SHADER_PROFILE_SPIRV},
    {"glsl", CKRST_SHADER_PROFILE_GLSL},
    {"metal", CKRST_SHADER_PROFILE_MSL},
};

char g_BackendParityFailure[512];

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;

    va_list args;
    va_start(args, format);
    std::vsnprintf(g_BackendParityFailure, sizeof(g_BackendParityFailure), format, args);
    va_end(args);
    TestFail(g_BackendParityFailure);
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

const CKFFSpecializedModuleEntry *FindFirstGeneratedSpecializedEntry(CK_SHADER_PROFILE profile)
{
    for (std::size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        if (g_CKFFSpecializedModules[i].Profile == profile)
            return &g_CKFFSpecializedModules[i];
    }
    return nullptr;
}

int CountGeneratedSpecializedModules(CK_SHADER_PROFILE profile)
{
    int count = 0;
    for (std::size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        if (g_CKFFSpecializedModules[i].Profile == profile)
            ++count;
    }
    return count;
}

int CountGeneratedSamplerLayoutModules(CK_SHADER_PROFILE profile)
{
    int count = 0;
    for (std::size_t i = 0; i < g_CKFFSamplerLayoutModuleCount; ++i) {
        if (g_CKFFSamplerLayoutModules[i].Profile == profile)
            ++count;
    }
    return count;
}

void TestContextSpecializationMatches(const FFPDiagnosticContext &context,
                                      const CKFFSpecializationInfo &expected,
                                      const char *caseName,
                                      const char *backendName)
{
    const CKDWORD count = (CKDWORD)context.LastProgramSpecializationDwords.size();
    TestCheckf(count == expected.DwordCount(),
               "%s backend %s program specialization dword count %u must match expected %u",
               caseName, backendName, count, expected.DwordCount());
    for (CKDWORD i = 0; i < expected.DwordCount(); ++i) {
        TestCheckf(context.LastProgramSpecializationDwords[i] == expected.Data()[i],
                   "%s backend %s program specialization dword %u mismatch",
                   caseName, backendName, i);
    }
}

void TestCachedSecondLookup(CKFFShaderCache &cache,
                            FFPDiagnosticContext &context,
                            const CKFFShaderKey &key,
                            const CKFFProgramBinding &first,
                            const char *caseName,
                            const char *backendName)
{
    const CKDWORD createdPrograms = context.CreatedProgramCount;
    const CKFFProgramBinding second = cache.GetProgram(key);
    TestCheckf(second.Program == first.Program,
               "%s backend %s second lookup must return the cached program",
               caseName, backendName);
    TestCheckf(second.FullSpecialized == first.FullSpecialized,
               "%s backend %s cached binding must preserve the selected route",
               caseName, backendName);
    TestCheckf(context.CreatedProgramCount == createdPrograms,
               "%s backend %s second lookup must not create another program",
               caseName, backendName);
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

CKFFShaderKey MakeRuntimeKeySkeleton()
{
    const CKFFSpecializedModuleEntry *entry = FindFirstGeneratedSpecializedEntry(CKRST_SHADER_PROFILE_DX11);
    TestCheck(entry != nullptr,
              "Runtime shader cache parity tests require a generated DX11 full-specialized baseline key");

    CKFFShaderKey key;
    key.VS = entry->Key.VS;
    key.FS = CKFFShaderKeyFS();
    return key;
}

CKFFShaderKey MakeStageFourFallbackKey()
{
    CKFFShaderKey key = MakeRuntimeKeySkeleton();
    for (CKDWORD stage = 0; stage < 4; ++stage)
        SetSelectStage(key.FS.Stages[stage], CKRST_TA_CURRENT, false, CKFF_SAMPLER_2D);
    SetSelectStage(key.FS.Stages[4], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_2D);
    key.FS.LastActiveTextureStage = 4;
    return key;
}

CKFFShaderKey MakeVolumeCubeStaticLayoutKey()
{
    CKFFShaderKey key = MakeRuntimeKeySkeleton();
    SetSelectStage(key.FS.Stages[0], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_VOLUME);
    SetSelectStage(key.FS.Stages[1], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_CUBE);
    key.FS.LastActiveTextureStage = 1;
    return key;
}

void GeneratedFullSpecializedModulesCoverEveryBackend()
{
    TestCheck(CKFFSpecializedModuleCount() == g_CKFFSpecializedModuleCount,
              "Public specialized module count must match generated table count");

    const int baselineCount = CountGeneratedSpecializedModules(CKRST_SHADER_PROFILE_DX11);
    TestCheck(baselineCount > 0,
              "Generated specialized module table must contain DX11 baseline entries");

    for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex) {
        const BackendProfile &backend = kBackends[backendIndex];
        const int count = CountGeneratedSpecializedModules(backend.Profile);
        TestCheckf(count == baselineCount,
                   "Backend %s specialized module count %d must match DX11 count %d",
                   backend.Name, count, baselineCount);
    }

    for (std::size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        const CKFFSpecializedModuleEntry &baseline = g_CKFFSpecializedModules[i];
        if (baseline.Profile != CKRST_SHADER_PROFILE_DX11)
            continue;

        const CKFFSpecializationInfo expected = CKFFBuildSpecializationInfo(baseline.Key.FS);
        TestCheckf(SameSpecialization(baseline.Module.Specialization, expected),
                   "DX11 generated specialization dwords must match C++ packer for entry %u",
                   (unsigned)i);

        for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex) {
            const BackendProfile &backend = kBackends[backendIndex];
            CKFFSpecializedModule module;
            const bool found = CKFFFindSpecializedModule(baseline.Key, backend.Profile, module);
            TestCheckf(found,
                       "Backend %s must contain every DX11 full-specialized shader key",
                       backend.Name);
            TestCheckf(module.VSData != nullptr && module.VSSize > 0 &&
                           module.FSData != nullptr && module.FSSize > 0,
                       "Backend %s specialized module must contain non-empty shader blobs",
                       backend.Name);
            TestCheckf(SameSpecialization(module.Specialization, baseline.Module.Specialization),
                       "Backend %s specialization dwords must match DX11 for the same shader key",
                       backend.Name);
            TestCheckf(SameSpecialization(module.Specialization, expected),
                       "Backend %s specialization dwords must match C++ packer",
                       backend.Name);
        }
    }
}

void GeneratedSamplerLayoutModulesCoverEveryBackend()
{
    TestCheck(CKFFSamplerLayoutModuleCount() == g_CKFFSamplerLayoutModuleCount,
              "Public sampler-layout module count must match generated table count");

    const int baselineCount = CountGeneratedSamplerLayoutModules(CKRST_SHADER_PROFILE_DX11);
    TestCheck(baselineCount > 0,
              "Generated sampler-layout table must contain DX11 baseline entries");

    for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex) {
        const BackendProfile &backend = kBackends[backendIndex];
        const int count = CountGeneratedSamplerLayoutModules(backend.Profile);
        TestCheckf(count == baselineCount,
                   "Backend %s sampler-layout module count %d must match DX11 count %d",
                   backend.Name, count, baselineCount);
    }

    for (std::size_t i = 0; i < g_CKFFSamplerLayoutModuleCount; ++i) {
        const CKFFSamplerLayoutModuleEntry &baseline = g_CKFFSamplerLayoutModules[i];
        if (baseline.Profile != CKRST_SHADER_PROFILE_DX11)
            continue;

        for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex) {
            const BackendProfile &backend = kBackends[backendIndex];
            CKFFSamplerLayoutModule module;
            const bool found = CKFFFindSamplerLayoutModule(baseline.Key, backend.Profile, module);
            TestCheckf(found,
                       "Backend %s must contain every DX11 sampler-layout key",
                       backend.Name);
            TestCheckf(module.FSData != nullptr && module.FSSize > 0,
                       "Backend %s sampler-layout module must contain a non-empty fragment shader blob",
                       backend.Name);
        }
    }
}

void RunShaderCacheFullSpecializedRoute(const BackendProfile &backend,
                                        const CKFFShaderKey &key)
{
    FFPDiagnosticDriver driver(backend.Profile);
    FFPDiagnosticContext context(&driver);
    CKFFShaderCache cache;
    cache.Init(&context);

    CKFFSpecializedModule module;
    const bool found = CKFFFindSpecializedModule(key, backend.Profile, module);
    TestCheckf(found,
               "Backend %s must have the generated full-specialized module used by runtime parity",
               backend.Name);

    const CKFFProgramBinding binding = cache.GetProgram(key);
    TestCheckf(binding.Program != 0,
               "Backend %s full-specialized runtime route must create a program",
               backend.Name);
    TestCheckf(binding.FullSpecialized,
               "Backend %s generated key must route through full-specialized cache",
               backend.Name);
    TestCheckf(context.CreatedProgramCount == 1 && context.CreatedShaderCount == 2,
               "Backend %s full-specialized runtime route must create exactly one VS/FS program",
               backend.Name);
    TestCheckf(context.LastVertexShaderCode == module.VSData &&
                   context.LastVertexShaderCodeSize == module.VSSize &&
                   context.LastPixelShaderCode == module.FSData &&
                   context.LastPixelShaderCodeSize == module.FSSize,
               "Backend %s full-specialized runtime route must use the generated shader blobs",
               backend.Name);
    TestCheckf(SameSpecialization(binding.Specialization, module.Specialization),
               "Backend %s full-specialized binding specialization must match generated module",
               backend.Name);
    TestContextSpecializationMatches(context, module.Specialization,
                                     "full-specialized runtime route", backend.Name);
    TestCachedSecondLookup(cache, context, key, binding,
                           "full-specialized runtime route", backend.Name);

    cache.Shutdown();
}

void ShaderCacheFullSpecializedRouteMatchesEveryBackend()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader", "0");

    const CKFFSpecializedModuleEntry *entry = FindFirstGeneratedSpecializedEntry(CKRST_SHADER_PROFILE_DX11);
    TestCheck(entry != nullptr,
              "Generated specialized module table must contain a DX11 baseline entry");

    for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex)
        RunShaderCacheFullSpecializedRoute(kBackends[backendIndex], entry->Key);

    CKRenderSettingsClearOverridesForTests();
}

void RunShaderCacheStageFourFallbackRoute(const BackendProfile &backend,
                                          const CKFFShaderKey &key)
{
    FFPDiagnosticDriver driver(backend.Profile);
    FFPDiagnosticContext context(&driver);
    CKFFShaderCache cache;
    cache.Init(&context);

    const CKFFSpecializationInfo expected = CKFFBuildSpecializationInfo(key.FS);
    const CKFFProgramBinding binding = cache.GetProgram(key);
    TestCheckf(binding.Program != 0,
               "Backend %s stage 4 key must create a runtime fallback program",
               backend.Name);
    TestCheckf(!binding.FullSpecialized,
               "Backend %s stage 4 key must not be cached as full-specialized",
               backend.Name);
    TestCheckf(context.CreatedProgramCount == 1 && context.CreatedShaderCount == 2,
               "Backend %s stage 4 fallback must create exactly one VS/FS program",
               backend.Name);
    TestCheckf(SameSpecialization(binding.Specialization, expected),
               "Backend %s stage 4 fallback binding specialization must match C++ packer",
               backend.Name);
    TestContextSpecializationMatches(context, expected,
                                     "stage 4 fallback runtime route", backend.Name);
    TestCachedSecondLookup(cache, context, key, binding,
                           "stage 4 fallback runtime route", backend.Name);

    cache.Shutdown();
}

void ShaderCacheStageFourFallbackMatchesEveryBackend()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader", "0");

    const CKFFShaderKey key = MakeStageFourFallbackKey();
    for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex)
        RunShaderCacheStageFourFallbackRoute(kBackends[backendIndex], key);

    CKRenderSettingsClearOverridesForTests();
}

void RunShaderCacheStaticSamplerLayoutRoute(const BackendProfile &backend,
                                            const CKFFShaderKey &key)
{
    FFPDiagnosticDriver driver(backend.Profile);
    FFPDiagnosticContext context(&driver);
    CKFFShaderCache cache;
    cache.Init(&context);

    CKFFSpecializedModule specializedModule;
    TestCheckf(!CKFFFindSpecializedModule(key, backend.Profile, specializedModule),
               "Backend %s volume+cube key must exercise static sampler layout fallback, not a full-specialized hit",
               backend.Name);

    const CKFFSamplerLayoutKey layout = CKFFBuildSamplerLayoutKey(key.FS);
    CKFFSamplerLayoutModule layoutModule;
    TestCheckf(CKFFFindSamplerLayoutModule(layout, backend.Profile, layoutModule),
               "Backend %s must contain the volume+cube static sampler layout module",
               backend.Name);

    const CKFFSpecializationInfo expected = CKFFBuildSpecializationInfo(key.FS);
    const CKFFProgramBinding binding = cache.GetProgram(key);
    TestCheckf(binding.Program != 0,
               "Backend %s volume+cube key must create a static sampler layout program",
               backend.Name);
    TestCheckf(!binding.FullSpecialized,
               "Backend %s volume+cube key must not be marked full-specialized",
               backend.Name);
    TestCheckf(context.CreatedProgramCount == 1 && context.CreatedShaderCount == 2,
               "Backend %s static sampler layout fallback must create exactly one VS/FS program",
               backend.Name);
    TestCheckf(context.LastPixelShaderCode == layoutModule.FSData &&
                   context.LastPixelShaderCodeSize == layoutModule.FSSize,
               "Backend %s static sampler layout route must use the generated layout fragment shader",
               backend.Name);
    TestCheckf(SameSpecialization(binding.Specialization, expected),
               "Backend %s static sampler layout binding specialization must match C++ packer",
               backend.Name);
    TestContextSpecializationMatches(context, expected,
                                     "static sampler layout runtime route", backend.Name);
    TestCachedSecondLookup(cache, context, key, binding,
                           "static sampler layout runtime route", backend.Name);

    cache.Shutdown();
}

void ShaderCacheStaticSamplerLayoutRouteMatchesEveryBackend()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader", "0");

    const CKFFShaderKey key = MakeVolumeCubeStaticLayoutKey();
    for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex)
        RunShaderCacheStaticSamplerLayoutRoute(kBackends[backendIndex], key);

    CKRenderSettingsClearOverridesForTests();
}

void RunShaderCacheUberModeRoute(const BackendProfile &backend,
                                 const CKFFShaderKey &key)
{
    FFPDiagnosticDriver driver(backend.Profile);
    FFPDiagnosticContext context(&driver);
    CKFFShaderCache cache;
    cache.Init(&context);

    CKFFSpecializedModule fullModule;
    TestCheckf(CKFFFindSpecializedModule(key, backend.Profile, fullModule),
               "Backend %s must have a full-specialized module before testing uber override",
               backend.Name);

    const CKFFSpecializationInfo expected = CKFFBuildSpecializationInfo(key.FS);
    const CKFFProgramBinding binding = cache.GetProgram(key);
    TestCheckf(binding.Program != 0,
               "Backend %s uber mode must create a runtime program",
               backend.Name);
    TestCheckf(!binding.FullSpecialized,
               "Backend %s uber mode must not mark generated keys as full-specialized",
               backend.Name);
    TestCheckf(context.LastPixelShaderCode != fullModule.FSData ||
                   context.LastPixelShaderCodeSize != fullModule.FSSize,
               "Backend %s uber mode must not use the generated full-specialized fragment shader",
               backend.Name);
    TestCheckf(SameSpecialization(binding.Specialization, expected),
               "Backend %s uber binding specialization must match C++ packer",
               backend.Name);
    TestContextSpecializationMatches(context, expected,
                                     "uber runtime route", backend.Name);
    TestCachedSecondLookup(cache, context, key, binding,
                           "uber runtime route", backend.Name);

    cache.Shutdown();
}

void ShaderCacheUberModeRouteMatchesEveryBackend()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader", "1");

    const CKFFSpecializedModuleEntry *entry = FindFirstGeneratedSpecializedEntry(CKRST_SHADER_PROFILE_DX11);
    TestCheck(entry != nullptr,
              "Generated specialized module table must contain a DX11 baseline entry");

    for (std::size_t backendIndex = 0; backendIndex < sizeof(kBackends) / sizeof(kBackends[0]); ++backendIndex)
        RunShaderCacheUberModeRoute(kBackends[backendIndex], entry->Key);

    CKRenderSettingsClearOverridesForTests();
}

} // namespace

int main()
{
    TestFramework tests;
    tests.Run("Generated full-specialized modules cover every backend",
              &GeneratedFullSpecializedModulesCoverEveryBackend);
    tests.Run("Generated sampler-layout modules cover every backend",
              &GeneratedSamplerLayoutModulesCoverEveryBackend);
    tests.Run("Shader cache full-specialized route matches every backend",
              &ShaderCacheFullSpecializedRouteMatchesEveryBackend);
    tests.Run("Shader cache stage 4 fallback matches every backend",
              &ShaderCacheStageFourFallbackMatchesEveryBackend);
    tests.Run("Shader cache static sampler layout route matches every backend",
              &ShaderCacheStaticSamplerLayoutRouteMatchesEveryBackend);
    tests.Run("Shader cache uber mode route matches every backend",
              &ShaderCacheUberModeRouteMatchesEveryBackend);
    return tests.ExitCode();
}
