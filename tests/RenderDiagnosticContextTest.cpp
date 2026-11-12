#include <gtest/gtest.h>
#include "CKRenderDiagnosticsContext.h"
#include "CKRenderContextDiagnostics.h"
#include "CKRenderSettings.h"
#include "CKDebugLogger.h"
#include "CKRenderPerfStats.h"
#include "CKRenderFrameCostStats.h"
#include "CKFFDebug.h"
#include "CKFixedFunctionPipeline.h"
#include "VxWindowFunctions.h"
#include "VxSharedLibrary.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

class RenderDiagnosticContextTest : public ::testing::Test {
protected:
    std::filesystem::path Directory;
    void SetUp() override {
        Directory = std::filesystem::temp_directory_path() /
            ("nemo-diagnostics-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(Directory));
    }
    void TearDown() override { std::filesystem::remove_all(Directory); }
    std::string Path(const char *name) { return (Directory / name).string(); }
    void Configure(CKRenderSettingsState &settings, CKRenderDiagnosticsContext &context,
                   const char *name, int warmup, int samples, int drawLimit = 1, bool explicitPath = true) {
        const auto ini = Path((std::string(name) + ".ini").c_str());
        std::ofstream file(ini);
        file << "<CK2_3D>\n<Debug>\nOutput=1\nLogPath=" << (explicitPath ? (Directory / name).generic_string() : "")
             << "\n<RenderStats>\nEnabled=1\n</RenderStats>\n<FFPLog>\nDrawLimit=" << drawLimit
             << "\n</FFPLog>\n<FrameCostStats>\nEnabled=1\nWarmupFrames=" << warmup
             << "\nSampleFrames=" << samples << "\nOutput=log\n</FrameCostStats>\n</Debug>\n</CK2_3D>\n";
        file.close();
        ASSERT_TRUE(settings.Load(ini.c_str()));
        context.Configure(&settings);
        CKRenderDiagnosticScope scope(context.Handle());
        CKDebugLogger::Instance().EnableDebuggerOutput(false);
    }
    std::string Read(const char *name) {
        std::ifstream file(Path(name));
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(RenderDiagnosticContextTest, InterleavedScopesKeepLogsCountersAndWarmupIndependent) {
    CKRenderSettingsState aSettings, bSettings;
    CKRenderDiagnosticsContext a, b;
    Configure(aSettings, a, "a.log", 0, 2, 2);
    Configure(bSettings, b, "b.log", 1, 1, 7);
    ASSERT_NE(a.Handle()->Id, b.Handle()->Id);
    CKRenderFrameCostStatsSnapshot aSnapshot = {}, bSnapshot = {};
    {
        CKRenderDiagnosticScope outer(a.Handle());
        CK_LOG("Test", "alpha");
        CKRenderPerfBeginFrame(1, 0, 0, 0);
        CKRenderPerfCurrent().MeshRenderCalls = 13;
        CKRenderFrameCostStatsBeginRenderFrame(1, 0, 0, 0);
        CKRenderFrameCostStatsAddDrawPrimitive();
        EXPECT_EQ(2, CKFFDebugConfig::Get().DrawLogLimit);
        {
            CKRenderDiagnosticScope inner(b.Handle());
            CK_LOG("Test", "beta");
            CKRenderPerfBeginFrame(2, 0, 0, 0);
            CKRenderPerfCurrent().MeshRenderCalls = 29;
            CKRenderFrameCostStatsBeginRenderFrame(2, 0, 0, 0);
            EXPECT_FALSE(CKRenderFrameCostStatsIsCollecting());
            EXPECT_EQ(7, CKFFDebugConfig::Get().DrawLogLimit);
            struct Library { VxSharedLibrary Value; ~Library() { Value.ReleaseLibrary(); } } module;
            ASSERT_NE(nullptr, module.Value.Load(CKRE_DIAGNOSTIC_PROBE));
            using Probe = void (*)(const CKRenderDiagnosticHandle *);
            auto probe = reinterpret_cast<Probe>(module.Value.GetFunctionPtr("CKREDiagnosticProbe"));
            ASSERT_NE(nullptr, probe);
            probe(a.Handle());
            module.Value.ReleaseLibrary();
            EXPECT_EQ(b.Handle(), CKRenderCurrentDiagnostics());
            CKRenderFrameCostStatsEndRenderFrame();
            CKRenderFrameCostStatsCopySnapshot(&bSnapshot);
            EXPECT_EQ(29u, CKRenderPerfCurrent().MeshRenderCalls);
        }
        EXPECT_EQ(a.Handle(), CKRenderCurrentDiagnostics());
        EXPECT_EQ(13u, CKRenderPerfCurrent().MeshRenderCalls);
        CKRenderFrameCostStatsEndRenderFrame();
        CKRenderFrameCostStatsCopySnapshot(&aSnapshot);
    }
    EXPECT_EQ(nullptr, CKRenderCurrentDiagnostics());
    EXPECT_EQ(1u, aSnapshot.SampledFrames);
    EXPECT_EQ(2u, aSnapshot.DrawPrimitiveCalls);
    EXPECT_EQ(0u, bSnapshot.SampledFrames);
    EXPECT_EQ(0u, bSnapshot.DrawPrimitiveCalls);
    EXPECT_NE(std::string::npos, Read("a.log").find("alpha"));
    EXPECT_NE(std::string::npos, Read("a.log").find("from-module"));
    EXPECT_EQ(std::string::npos, Read("a.log").find("beta"));
    EXPECT_NE(std::string::npos, Read("b.log").find("beta"));
    EXPECT_EQ(std::string::npos, Read("b.log").find("alpha"));
}

TEST_F(RenderDiagnosticContextTest, ConcurrentContextsAndOutputGatesDoNotShareState) {
    CKRenderSettingsState aSettings, bSettings;
    CKRenderDiagnosticsContext a, b;
    Configure(aSettings, a, "a.log", 0, 1);
    Configure(bSettings, b, "b.log", 0, 1);
    auto run = [](CKRenderDiagnosticsContext &context, const char *message, int count) {
        CKRenderDiagnosticScope scope(context.Handle());
        CKRenderPerfBeginFrame(0, 0, 0, 0);
        CKRenderFrameCostStatsBeginRenderFrame(0, 0, 0, 0);
        for (int i = 0; i < count; ++i) {
            CK_LOG("Thread", message);
            ++CKRenderPerfCurrent().DrawPrimitiveCalls;
            CKRenderFrameCostStatsAddDrawPrimitive();
        }
        CKRenderFrameCostStatsEndRenderFrame();
        CKRenderFrameCostStatsAddPlayerTiming(1, 0, 0, 0, TRUE, TRUE);
    };
    std::thread first([&] { run(a, "thread-alpha", 19); });
    std::thread second([&] { run(b, "thread-beta", 37); });
    first.join(); second.join();
    CKRenderFrameCostStatsSnapshot snapshot = {};
    {
        CKRenderDiagnosticScope scope(a.Handle());
        EXPECT_EQ(19u, CKRenderPerfCurrent().DrawPrimitiveCalls);
        CKRenderFrameCostStatsCopySnapshot(&snapshot);
        EXPECT_EQ(19u, snapshot.DrawPrimitiveCalls);
        EXPECT_TRUE(snapshot.Completed);
        CKDebugLogger::Instance().EnableOutput(false);
        CK_LOG("Test", "suppressed-alpha");
        CKRenderFrameCostStatsResetForTests();
    }
    {
        CKRenderDiagnosticScope scope(b.Handle());
        EXPECT_TRUE(CKDebugLogger::OutputEnabled());
        EXPECT_EQ(37u, CKRenderPerfCurrent().DrawPrimitiveCalls);
        CKRenderFrameCostStatsCopySnapshot(&snapshot);
        EXPECT_EQ(37u, snapshot.DrawPrimitiveCalls);
        EXPECT_TRUE(snapshot.Completed);
        CK_LOG("Test", "beta-still-enabled");
    }
    EXPECT_EQ(std::string::npos, Read("a.log").find("thread-beta"));
    EXPECT_EQ(std::string::npos, Read("b.log").find("thread-alpha"));
    EXPECT_EQ(std::string::npos, Read("a.log").find("suppressed-alpha"));
    EXPECT_NE(std::string::npos, Read("b.log").find("beta-still-enabled"));
    EXPECT_NE(std::string::npos, Read("a.log").find("FrameCostStats.Core"));
    EXPECT_NE(std::string::npos, Read("b.log").find("FrameCostStats.Core"));
}

TEST_F(RenderDiagnosticContextTest, ResetAndRecreationPreserveOtherContextsAndTheUnboundFallback) {
    CKRenderDiagnosticScope unbound(nullptr);
    struct Overrides {
        ~Overrides() {
            CKRenderSettingsClearOverridesForTests();
            CKRenderFrameCostStatsResetForTests();
        }
    } overrides;
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Enabled", "1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "WarmupFrames", "0");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "SampleFrames", "2");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Output", "none");
    CKRenderFrameCostStatsResetForTests();
    CKRenderFrameCostStatsBeginRenderFrame(0, 0, 0, 0);
    CKRenderFrameCostStatsAddDrawPrimitive();
    CKRenderFrameCostStatsEndRenderFrame();

    CKRenderSettingsState aSettings, bSettings;
    CKRenderDiagnosticsContext a, b;
    Configure(aSettings, a, "a.log", 0, 2);
    Configure(bSettings, b, "b.log", 0, 2);
    {
        CKRenderDiagnosticScope scope(a.Handle());
        CKRenderContextLogLimits().CameraAttach = 16;
        CKRenderPerfCurrent().MeshRenderCalls = 13;
        CKRenderPerfResetNowCallCountForTests();
        CKRenderPerfNow();
        EXPECT_EQ(1u, CKRenderPerfNowCallCountForTests());
    }
    {
        CKRenderDiagnosticScope scope(b.Handle());
        EXPECT_EQ(0, CKRenderContextLogLimits().CameraAttach);
        EXPECT_EQ(0u, CKRenderPerfNowCallCountForTests());
        CKRenderPerfCurrent().MeshRenderCalls = 29;
    }
    a.Configure(&aSettings);
    {
        CKRenderDiagnosticScope scope(a.Handle());
        EXPECT_EQ(0, CKRenderContextLogLimits().CameraAttach);
        EXPECT_EQ(0u, CKRenderPerfCurrent().MeshRenderCalls);
        EXPECT_EQ(0u, CKRenderPerfNowCallCountForTests());
    }
    {
        CKRenderDiagnosticScope scope(b.Handle());
        EXPECT_EQ(29u, CKRenderPerfCurrent().MeshRenderCalls);
    }
    uint64_t retiredId;
    {
        CKRenderDiagnosticsContext retired;
        retiredId = retired.Handle()->Id;
        CKRenderDiagnosticScope scope(retired.Handle());
        CKRenderPerfCurrent().MeshRenderCalls = 99;
    }
    {
        CKRenderDiagnosticsContext replacement;
        EXPECT_NE(retiredId, replacement.Handle()->Id);
        CKRenderDiagnosticScope scope(replacement.Handle());
        EXPECT_EQ(0u, CKRenderPerfCurrent().MeshRenderCalls);
    }
    CKRenderFrameCostStatsSnapshot legacy = {};
    CKRenderFrameCostStatsCopySnapshot(&legacy);
    EXPECT_EQ(1u, legacy.SampledFrames);
    EXPECT_EQ(1u, legacy.DrawPrimitiveCalls);
}

TEST_F(RenderDiagnosticContextTest, FixedFunctionEntriesSelectTheirBoundOwner) {
    CKRenderSettingsState aSettings, bSettings;
    CKRenderDiagnosticsContext a, b;
    Configure(aSettings, a, "a.log", 0, 2);
    Configure(bSettings, b, "b.log", 0, 2);
    CKFixedFunctionPipeline first, second;
    first.SetDiagnosticContext(a.Handle());
    second.SetDiagnosticContext(b.Handle());
    {
        CKRenderDiagnosticScope scope(b.Handle());
        CKRenderFrameCostStatsBeginRenderFrame(0, 0, 0, 0);
    }
    CKRenderDiagnosticScope caller(a.Handle());
    CKRenderFrameCostStatsBeginRenderFrame(0, 0, 0, 0);
    EXPECT_TRUE(first.FinishDraw(CK_OK, 0));
    EXPECT_TRUE(first.FinishDraw(CK_OK, 0));
    EXPECT_TRUE(second.FinishDraw(CK_OK, 0));
    EXPECT_FALSE(second.FinishDraw(CKERR_INVALIDPARAMETER, 0));
    EXPECT_EQ(a.Handle(), CKRenderCurrentDiagnostics());
    EXPECT_EQ(0u, first.GetRejectedDrawCount(CKFF_DRAW_REJECT_BACKEND_ERROR));
    EXPECT_EQ(1u, second.GetRejectedDrawCount(CKFF_DRAW_REJECT_BACKEND_ERROR));
    CKRenderFrameCostStatsSnapshot snapshot = {};
    CKRenderFrameCostStatsCopySnapshot(&snapshot);
    EXPECT_EQ(2u, snapshot.SubmittedDraws);
    {
        CKRenderDiagnosticScope scope(b.Handle());
        CKRenderFrameCostStatsCopySnapshot(&snapshot);
        EXPECT_EQ(1u, snapshot.SubmittedDraws);
    }
    EXPECT_EQ(std::string::npos, Read("a.log").find("FFPReject"));
    EXPECT_NE(std::string::npos, Read("b.log").find("FFPReject"));
}

TEST_F(RenderDiagnosticContextTest, EmptyLogPathsSelectSeparateDefaultFiles) {
    CKRenderSettingsState aSettings, bSettings;
    CKRenderDiagnosticsContext a, b;
    Configure(aSettings, a, "a", 0, 1, 1, false);
    Configure(bSettings, b, "b", 0, 1, 1, false);
    auto write = [](CKRenderDiagnosticsContext &context, const char *message) {
        const std::string name = "CK2_3D_Debug.context-" + std::to_string(context.Handle()->Id) + ".log";
        XString resolved = CKRenderModuleSiblingFile((const void *)&CKRenderCurrentDiagnostics, name.c_str());
        const std::string path = resolved.Empty() ? name : resolved.CStr();
        std::remove(path.c_str());
        CKRenderDiagnosticScope scope(context.Handle());
        CK_LOG("Test", message);
        CKDebugLogger::Instance().EnableOutput(false);
        std::ifstream file(path);
        const std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        file.close();
        std::remove(path.c_str());
        return contents;
    };
    const auto first = write(a, "default-alpha");
    const auto second = write(b, "default-beta");
    EXPECT_NE(std::string::npos, first.find("default-alpha"));
    EXPECT_EQ(std::string::npos, first.find("default-beta"));
    EXPECT_NE(std::string::npos, second.find("default-beta"));
    EXPECT_EQ(std::string::npos, second.find("default-alpha"));
}
