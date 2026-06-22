#include "CKFFSpecializedModuleTable.h"
#include "CKFFShaderKey.h"
#include "TestTriangleMultiset.h"

#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

#include <cstdarg>
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

} // namespace

int main()
{
    TestFramework tests;
    tests.Run("Generated full-specialized modules cover every backend",
              &GeneratedFullSpecializedModulesCoverEveryBackend);
    tests.Run("Generated sampler-layout modules cover every backend",
              &GeneratedSamplerLayoutModulesCoverEveryBackend);
    return tests.ExitCode();
}
