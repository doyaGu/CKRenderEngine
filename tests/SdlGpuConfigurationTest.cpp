#include <gtest/gtest.h>
#include "CKSdlGpuConfiguration.h"
#include "CKSdlGpuFFJitManifest.h"
#include "CKRenderDiagnosticsContext.h"
#include "CKRenderSettings.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>

namespace {
const char *const Variables[] = {
    "CKRE_SDL_GPU_DRIVER", "CKRE_SDL_GPU_DEBUG", "CKRE_SDL_GPU_WORKERS",
    "CKRE_SDL_GPU_FF_JIT", "CKRE_SDL_GPU_FF_VERTEX_JIT",
    "CKRE_SDL_GPU_FF_JIT_CACHE", "CKRE_SDL_GPU_FF_JIT_STATS"
};

struct Settings {
    CKRenderSettingsState State;
    CKRenderDiagnosticsContext Owner;
    void Parse(const char *entries) {
        const std::string text = std::string("<CK2_3D>\n<SdlGpu>\n") + entries + "\n</SdlGpu>\n</CK2_3D>\n";
        int line = 0;
        XString error;
        ASSERT_TRUE(State.Configuration.BuildFromMemory(text.c_str(), line, error)) << error.CStr();
        Owner.Configure(&State);
    }
};

class SdlGpuConfigurationTest : public ::testing::Test {
    struct Saved { const char *Name; bool Present; std::string Value; };
    std::vector<Saved> Previous;
protected:
    void SetUp() override {
        CKRenderSettingsClearOverridesForTests();
        for (const char *name : Variables) {
            const char *value = SDL_getenv(name);
            Previous.push_back({name, value != nullptr, value ? value : ""});
            ASSERT_TRUE(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name));
        }
    }
    void TearDown() override {
        for (const auto &saved : Previous) {
            if (saved.Present) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), saved.Name, saved.Value.c_str(), true);
            else SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), saved.Name);
        }
        CKRenderSettingsClearOverridesForTests();
    }
    void Env(const char *suffix, const char *value) {
        const std::string name = std::string("CKRE_SDL_GPU_") + suffix;
        ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name.c_str(), value, true));
    }
};

TEST_F(SdlGpuConfigurationTest, AbsentOptionsPreserveBuiltInDefaults) {
    Settings settings;
    settings.Parse("");
    CKSdlGpuConfiguration config;
    config.Load(settings.Owner.Handle());
    EXPECT_TRUE(config.Driver.Empty());
    EXPECT_FALSE(config.Debug);
    EXPECT_EQ(SDL_GetNumLogicalCPUCores() >= 4 ? 2 : 1, config.Workers);
    EXPECT_TRUE(config.FFJit);
    EXPECT_TRUE(config.FFVertexJit);
    EXPECT_TRUE(config.FFJitCache.Empty());
    EXPECT_FALSE(config.FFJitStats);
}

TEST_F(SdlGpuConfigurationTest, EnvironmentFallbackRetainsLegacyValueSemantics) {
    Env("DRIVER", "vulkan");
    Env("DEBUG", "false"); // Legacy debug accepts every present value except "0".
    Env("WORKERS", "3");
    Env("FF_JIT", "false"); // Legacy JIT disables only on "0".
    Env("FF_VERTEX_JIT", "0");
    Env("FF_JIT_CACHE", "/cache/from-env");
    Env("FF_JIT_STATS", "true"); // Legacy statistics enables only on "1".
    Settings settings;
    settings.Parse("");
    CKSdlGpuConfiguration config;
    config.Load(settings.Owner.Handle());
    EXPECT_STREQ("vulkan", config.Driver.CStr());
    EXPECT_TRUE(config.Debug);
    EXPECT_EQ(3, config.Workers);
    EXPECT_TRUE(config.FFJit);
    EXPECT_FALSE(config.FFVertexJit);
    EXPECT_STREQ("/cache/from-env", config.FFJitCache.CStr());
    EXPECT_FALSE(config.FFJitStats);
    Env("FF_JIT", "0"); Env("FF_JIT_STATS", "1");
    config.Load(settings.Owner.Handle());
    EXPECT_FALSE(config.FFJit);
    EXPECT_TRUE(config.FFJitStats);
    Env("DEBUG", "");
    config.Load(settings.Owner.Handle());
    EXPECT_TRUE(config.Debug);
}

TEST_F(SdlGpuConfigurationTest, ParsedOptionsOverrideTheEnvironmentAndStayWithTheirOwner) {
    Env("DRIVER", "from-environment"); Env("DEBUG", "1"); Env("WORKERS", "2");
    Env("FF_JIT", "1"); Env("FF_VERTEX_JIT", "1"); Env("FF_JIT_CACHE", "/env-cache"); Env("FF_JIT_STATS", "1");
    Settings first, second;
    first.Parse("Driver=auto\nDebug=off\nWorkers=1\nFFJit=no\nFFVertexJit=false\nFFJitCache=0\nFFJitStats=0");
    second.Parse("Driver=metal\nDebug=yes\nWorkers=4\nFFJit=on\nFFVertexJit=true\nFFJitCache=/project cache/\xE7\xBC\x93\xE5\xAD\x98\nFFJitStats=1");
    CKRenderDiagnosticScope caller(second.Owner.Handle());
    CKSdlGpuConfiguration a, b;
    a.Load(first.Owner.Handle());
    EXPECT_EQ(second.Owner.Handle(), CKRenderCurrentDiagnostics());
    b.Load(second.Owner.Handle());
    EXPECT_TRUE(a.Driver.Empty()); EXPECT_STREQ("metal", b.Driver.CStr());
    EXPECT_FALSE(a.Debug); EXPECT_TRUE(b.Debug);
    EXPECT_EQ(1, a.Workers); EXPECT_EQ(4, b.Workers);
    EXPECT_FALSE(a.FFJit); EXPECT_TRUE(b.FFJit);
    EXPECT_FALSE(a.FFVertexJit); EXPECT_TRUE(b.FFVertexJit);
    EXPECT_STREQ("0", a.FFJitCache.CStr());
    EXPECT_STREQ("/project cache/\xE7\xBC\x93\xE5\xAD\x98", b.FFJitCache.CStr());
    EXPECT_FALSE(a.FFJitStats); EXPECT_TRUE(b.FFJitStats);
}

TEST_F(SdlGpuConfigurationTest, PartialAndInvalidValuesFallBackPerOption) {
    Env("DRIVER", "vulkan"); Env("DEBUG", "1"); Env("WORKERS", "3");
    Env("FF_JIT", "0"); Env("FF_JIT_STATS", "1");
    Settings settings;
    settings.Parse("Debug=invalid\nWorkers=invalid\nFFJit=yes\nFFJitStats=invalid");
    CKSdlGpuConfiguration config;
    config.Load(settings.Owner.Handle());
    EXPECT_STREQ("vulkan", config.Driver.CStr());
    EXPECT_TRUE(config.Debug);
    EXPECT_EQ(3, config.Workers);
    EXPECT_TRUE(config.FFJit);
    EXPECT_TRUE(config.FFVertexJit);
    EXPECT_TRUE(config.FFJitStats);
}

TEST_F(SdlGpuConfigurationTest, ResolvedStringsAndPolicySurviveLaterEnvironmentChanges) {
    Env("DRIVER", "vulkan"); Env("WORKERS", "3"); Env("FF_JIT", "0");
    Env("FF_JIT_CACHE", "/initial-cache"); Env("FF_JIT_STATS", "1");
    Settings settings;
    settings.Parse("");
    CKSdlGpuConfiguration config;
    config.Load(settings.Owner.Handle());
    Env("DRIVER", "metal"); Env("WORKERS", "1"); Env("FF_JIT", "1");
    Env("FF_JIT_CACHE", "0"); Env("FF_JIT_STATS", "0");
    EXPECT_STREQ("vulkan", config.Driver.CStr());
    EXPECT_EQ(3, config.Workers);
    EXPECT_FALSE(config.FFJit);
    EXPECT_TRUE(config.FFJitStats);
    EXPECT_STREQ("/initial-cache/ffjit-0000000000000001.bin",
        CKSdlGpuFFJitManifestPath(1, config.FFJitCache.CStr()).CStr());
    EXPECT_TRUE(CKSdlGpuFFJitManifestPath(1).Empty());
}

TEST_F(SdlGpuConfigurationTest, EmptyStringsSelectDefaultsAndZeroDisablesCache) {
    Env("DRIVER", "vulkan"); Env("FF_JIT_CACHE", "/environment-cache");
    Settings settings;
    settings.Parse("Driver=\nFFJitCache=");
    CKSdlGpuConfiguration config;
    config.Load(settings.Owner.Handle());
    EXPECT_TRUE(config.Driver.Empty());
    const auto path = CKSdlGpuFFJitManifestPath(1, config.FFJitCache.CStr());
    EXPECT_NE(nullptr, SDL_strstr(path.CStr(), "CKSdlGpuCache/ffjit-0000000000000001.bin"));
    EXPECT_EQ(nullptr, SDL_strstr(path.CStr(), "environment-cache"));
    EXPECT_TRUE(CKSdlGpuFFJitManifestPath(1, "0").Empty());
}

TEST_F(SdlGpuConfigurationTest, StandaloneLoadingUsesLegacySettingsAndRestoresTheCaller) {
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::SdlGpu, "Driver", "vulkan");
    Settings settings;
    settings.Parse("Driver=metal");
    CKRenderDiagnosticScope caller(settings.Owner.Handle());
    CKSdlGpuConfiguration config;
    config.Load(nullptr);
    EXPECT_STREQ("vulkan", config.Driver.CStr());
    EXPECT_EQ(settings.Owner.Handle(), CKRenderCurrentDiagnostics());
}
} // namespace
