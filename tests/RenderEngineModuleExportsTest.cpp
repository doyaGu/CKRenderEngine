#include <gtest/gtest.h>
#include "CKContext.h"
#include "CKRasterizerRegistration.h"
#include "CKRenderFrameCostStats.h"
#include <Windows.h>

static void ExportProbeInfo(void *value) { CKNullRasterizerGetInfo(static_cast<CKRasterizerInfo *>(value)); }

class RenderEngineModuleExportsTest : public ::testing::Test {
protected:
    struct ModuleAPI {
        HMODULE Handle = nullptr;
        int (*WantsTiming)() = nullptr;
        void (*AddTiming)(double, double, double, double, int, int) = nullptr;
        void (*Configure)(int, int, int) = nullptr;
        void (*Begin)() = nullptr;
        void (*End)() = nullptr;
        CKBOOL (*InsideFrame)() = nullptr;
        void (*Snapshot)(CKRenderFrameCostStatsSnapshot *) = nullptr;
        CKPluginInfo *(*Query)(int) = nullptr;
        CKERROR (*Exit)(CKContext *) = nullptr;
        void (*Register)(const char *) = nullptr;
        const CKRasterizerInfo *(*Provider)(int) = nullptr;
        int (*ProviderCount)() = nullptr;
    } Enabled, Disabled;
    CKContext *First = nullptr, *Second = nullptr;
    HMODULE InitialProbe = nullptr;
    char Directory[2048];
    int Detaches = 0;
    void Load(ModuleAPI &api, const char *filename) {
        char path[2048]; snprintf(path, sizeof(path), "%s%s", Directory, filename);
        api.Handle = LoadLibraryA(path); ASSERT_NE(nullptr, api.Handle);
#define EXPORT_BIND(Member, Symbol) api.Member = reinterpret_cast<decltype(api.Member)>(GetProcAddress(api.Handle, Symbol)); ASSERT_NE(nullptr, api.Member)
        EXPORT_BIND(WantsTiming, "CK2_3D_FrameCostStatsIsCollecting");
        EXPORT_BIND(AddTiming, "CK2_3D_FrameCostStatsAddPlayerTiming");
        EXPORT_BIND(Configure, "CKRETestTimingConfigure"); EXPORT_BIND(Begin, "CKRETestTimingBeginFrame");
        EXPORT_BIND(End, "CKRETestTimingEndFrame"); EXPORT_BIND(InsideFrame, "CKRETestTimingInsideFrame");
        EXPORT_BIND(Snapshot, "CKRETestTimingSnapshot"); EXPORT_BIND(Query, "CKGetPluginInfo");
        EXPORT_BIND(Exit, "CKRETestExitInstance"); EXPORT_BIND(Register, "CKRETestRegisterProvider");
        EXPORT_BIND(Provider, "CKRETestProvider"); EXPORT_BIND(ProviderCount, "CKRETestProviderCount");
#undef EXPORT_BIND
        api.Configure(0, 0, 2);
    }
    CKRenderFrameCostStatsSnapshot Read(ModuleAPI &api) {
        CKRenderFrameCostStatsSnapshot result = {}; api.Snapshot(&result); return result;
    }
    void CreateContextsWithProvider() {
        CKPluginInfo *info = Disabled.Query(0); ASSERT_NE(nullptr, info);
        ASSERT_EQ(nullptr, info->m_ExitInstanceFct);
        char path[2048]; snprintf(path, sizeof(path), "%sCKREPluginLifetimeProbe.dll", Directory);
        InitialProbe = LoadLibraryA(path); ASSERT_NE(nullptr, InitialProbe);
        typedef void (*ConfigureProbe)(void (*)(void *), int *);
        ConfigureProbe configure = reinterpret_cast<ConfigureProbe>(GetProcAddress(InitialProbe, "CKREConfigureLifetimeProbe"));
        ASSERT_NE(nullptr, configure); configure(ExportProbeInfo, &Detaches);
        Disabled.Register(path); ASSERT_EQ(2, Disabled.ProviderCount());
        ASSERT_TRUE(FreeLibrary(InitialProbe)); InitialProbe = nullptr;
        ASSERT_EQ(CK_OK, CKCreateContext(&First, nullptr, 0, 0)); ASSERT_EQ(CK_OK, info->m_InitInstanceFct(First));
        ASSERT_EQ(CK_OK, CKCreateContext(&Second, nullptr, 0, 0)); ASSERT_EQ(CK_OK, info->m_InitInstanceFct(Second));
        ASSERT_NE(nullptr, First->GetRenderManager()); ASSERT_NE(nullptr, Second->GetRenderManager());
    }
    void SetUp() override {
        DWORD count = GetModuleFileNameA(nullptr, Directory, sizeof(Directory));
        ASSERT_GT(count, 0u); ASSERT_LT(count, static_cast<DWORD>(sizeof(Directory)));
        char *slash = strrchr(Directory, '\\'); ASSERT_NE(nullptr, slash); slash[1] = '\0';
        ASSERT_EQ(CK_OK, CKStartUp());
        Load(Enabled, "CKREModuleTimingEngine.dll"); Load(Disabled, "CKREModuleLifetimeEngine.dll");
    }
    void TearDown() override {
        if (First) CKCloseContext(First);
        if (Second) CKCloseContext(Second);
        CKShutdown();
        if (Disabled.Handle) FreeLibrary(Disabled.Handle);
        if (Enabled.Handle) FreeLibrary(Enabled.Handle);
        if (InitialProbe) FreeLibrary(InitialProbe);
    }
};

TEST_F(RenderEngineModuleExportsTest, PublicSymbolsResolveInBothCompileVariants) {
    EXPECT_EQ(nullptr, GetProcAddress(Enabled.Handle, "ExitInstanceFct"));
    EXPECT_EQ(nullptr, GetProcAddress(Disabled.Handle, "ExitInstanceFct"));
    EXPECT_EQ(nullptr, GetProcAddress(Enabled.Handle, "CKGetPluginInfoCount"));
    EXPECT_EQ(nullptr, GetProcAddress(Disabled.Handle, "CKGetPluginInfoCount"));
    EXPECT_NE(Enabled.WantsTiming, Disabled.WantsTiming);
}

TEST_F(RenderEngineModuleExportsTest, CompileDisabledExportsStayInertWhenRuntimeSettingsEnableTiming) {
    Disabled.Configure(1, 0, 1); EXPECT_EQ(0, Disabled.WantsTiming());
    Disabled.Begin(); Disabled.AddTiming(1.25, 2.5, 3.75, 4.5, -1, 7); Disabled.End();
    CKRenderFrameCostStatsSnapshot result = Read(Disabled);
    EXPECT_FALSE(result.Enabled); EXPECT_FALSE(result.Completed); EXPECT_EQ(0u, result.SampledFrames);
    EXPECT_DOUBLE_EQ(0.0, result.PlayerUpdateUs); EXPECT_EQ(0u, result.PlayerUpdateCalls);
}

TEST_F(RenderEngineModuleExportsTest, RuntimeDisabledExportsDoNotAccumulateSamples) {
    Enabled.Configure(0, 0, 1); EXPECT_EQ(0, Enabled.WantsTiming());
    Enabled.Begin(); Enabled.AddTiming(1, 2, 3, 4, 1, 1); Enabled.End();
    CKRenderFrameCostStatsSnapshot result = Read(Enabled);
    EXPECT_FALSE(result.Enabled); EXPECT_EQ(0u, result.SampledFrames); EXPECT_EQ(0u, result.PlayerUpdateCalls);
}

TEST_F(RenderEngineModuleExportsTest, WarmupRequestsFrontEndTimingWithoutCollectingSamples) {
    Enabled.Configure(1, 2, 2);
    EXPECT_EQ(1, Enabled.WantsTiming()); EXPECT_FALSE(Enabled.InsideFrame());
    for (int i = 0; i < 2; ++i) {
        Enabled.Begin(); EXPECT_FALSE(Enabled.InsideFrame()); EXPECT_EQ(1, Enabled.WantsTiming());
        Enabled.AddTiming(100, 200, 300, 400, 1, 1); Enabled.End();
    }
    CKRenderFrameCostStatsSnapshot result = Read(Enabled);
    EXPECT_EQ(0u, result.SampledFrames); EXPECT_EQ(0u, result.PlayerUpdateCalls);
    Enabled.Begin(); EXPECT_TRUE(Enabled.InsideFrame()); EXPECT_EQ(1, Enabled.WantsTiming());
}

TEST_F(RenderEngineModuleExportsTest, TimingValuesAndNonzeroFlagsReachTheRealAccumulator) {
    Enabled.Configure(1, 0, 2); Enabled.Begin();
    Enabled.AddTiming(1.25, 2.5, 3.75, 4.5, -7, 99); Enabled.End();
    CKRenderFrameCostStatsSnapshot result = Read(Enabled);
    EXPECT_DOUBLE_EQ(1.25, result.PlayerUpdateUs); EXPECT_DOUBLE_EQ(2.5, result.PlayerInputUs);
    EXPECT_DOUBLE_EQ(3.75, result.PlayerProcessUs); EXPECT_DOUBLE_EQ(4.5, result.PlayerRenderUs);
    EXPECT_EQ(1u, result.PlayerUpdateCalls); EXPECT_EQ(1u, result.PlayerProcessCalls);
    EXPECT_EQ(1u, result.PlayerRenderCalls); EXPECT_EQ(1u, result.SampledFrames);
}

TEST_F(RenderEngineModuleExportsTest, ZeroFlagsPreserveDurationsWithoutCountingProcessOrRenderCalls) {
    Enabled.Configure(1, 0, 2); Enabled.Begin();
    Enabled.AddTiming(0.125, 0.25, 0.5, 0.75, 0, 0); Enabled.End();
    CKRenderFrameCostStatsSnapshot result = Read(Enabled);
    EXPECT_DOUBLE_EQ(0.5, result.PlayerProcessUs); EXPECT_DOUBLE_EQ(0.75, result.PlayerRenderUs);
    EXPECT_EQ(1u, result.PlayerUpdateCalls); EXPECT_EQ(0u, result.PlayerProcessCalls); EXPECT_EQ(0u, result.PlayerRenderCalls);
}

TEST_F(RenderEngineModuleExportsTest, UnopenedAndDuplicateTimingSubmissionsAreIgnored) {
    Enabled.Configure(1, 0, 2); Enabled.AddTiming(100, 100, 100, 100, 1, 1);
    EXPECT_EQ(0u, Read(Enabled).PlayerUpdateCalls);
    Enabled.Begin(); Enabled.AddTiming(1, 2, 3, 4, 1, 0);
    Enabled.AddTiming(100, 100, 100, 100, 1, 1); Enabled.End();
    CKRenderFrameCostStatsSnapshot result = Read(Enabled);
    EXPECT_DOUBLE_EQ(1, result.PlayerUpdateUs); EXPECT_EQ(1u, result.PlayerUpdateCalls); EXPECT_EQ(0u, result.PlayerRenderCalls);
}

TEST_F(RenderEngineModuleExportsTest, FinalFrameTimingIsAcceptedAfterEndEvenWhenTheRequestGateHasClosed) {
    Enabled.Configure(1, 0, 1); Enabled.Begin(); Enabled.End();
    EXPECT_EQ(0, Enabled.WantsTiming()); EXPECT_FALSE(Enabled.InsideFrame());
    EXPECT_TRUE(Read(Enabled).Completed); EXPECT_EQ(0u, Read(Enabled).PlayerUpdateCalls);
    Enabled.AddTiming(1.5, 2.5, 3.5, 4.5, 1, 1);
    CKRenderFrameCostStatsSnapshot result = Read(Enabled);
    EXPECT_EQ(1u, result.SampledFrames); EXPECT_TRUE(result.Completed);
    EXPECT_DOUBLE_EQ(4.5, result.PlayerRenderUs); EXPECT_EQ(1u, result.PlayerUpdateCalls);
    Enabled.AddTiming(100, 100, 100, 100, 1, 1); EXPECT_EQ(1u, Read(Enabled).PlayerUpdateCalls);
}

TEST_F(RenderEngineModuleExportsTest, CompletedCollectionRejectsFurtherFramesAndTiming) {
    Enabled.Configure(1, 0, 1); Enabled.Begin(); Enabled.AddTiming(1, 2, 3, 4, 1, 1); Enabled.End();
    EXPECT_EQ(0, Enabled.WantsTiming()); Enabled.Begin(); Enabled.AddTiming(100, 100, 100, 100, 1, 1); Enabled.End();
    CKRenderFrameCostStatsSnapshot result = Read(Enabled);
    EXPECT_EQ(1u, result.SampledFrames); EXPECT_EQ(1u, result.PlayerUpdateCalls); EXPECT_DOUBLE_EQ(1, result.PlayerUpdateUs);
}

TEST_F(RenderEngineModuleExportsTest, DisabledModuleDoesNotChangeTheEnabledModulesMeasurement) {
    Enabled.Configure(1, 0, 2); Disabled.Configure(1, 0, 2);
    Enabled.Begin(); Disabled.Begin(); Enabled.AddTiming(2, 4, 6, 8, 1, 1);
    Disabled.AddTiming(100, 100, 100, 100, 1, 1); Disabled.End(); Enabled.End();
    EXPECT_EQ(1u, Read(Enabled).PlayerUpdateCalls); EXPECT_DOUBLE_EQ(2, Read(Enabled).PlayerUpdateUs);
    EXPECT_EQ(0u, Read(Disabled).PlayerUpdateCalls); EXPECT_EQ(1, Enabled.WantsTiming()); EXPECT_EQ(0, Disabled.WantsTiming());
}

TEST_F(RenderEngineModuleExportsTest, LegacyInstanceExitCannotReleaseAProviderUsedByAnotherContext) {
    CreateContextsWithProvider(); INSTANCE_HANDLE owned = Disabled.Provider(1)->DllInstance;
    ASSERT_NE(nullptr, owned); EXPECT_EQ(CK_OK, Disabled.Exit(First));
    EXPECT_EQ(0, Detaches); EXPECT_EQ(owned, Disabled.Provider(1)->DllInstance);
    CKCloseContext(First); First = nullptr;
    ASSERT_NE(nullptr, Second->CreateObject(32, "remaining-context-mesh"));
    EXPECT_EQ(0, Detaches);
}

TEST_F(RenderEngineModuleExportsTest, RepeatedInstanceExitKeepsContextManagersAndModuleOwnershipIntact) {
    CreateContextsWithProvider(); CKRenderManager *manager = First->GetRenderManager();
    const int objects = First->GetObjectCount(), managers = First->GetManagerCount();
    for (int i = 0; i < 3; ++i) EXPECT_EQ(CK_OK, Disabled.Exit(First));
    EXPECT_EQ(manager, First->GetRenderManager()); EXPECT_EQ(objects, First->GetObjectCount());
    EXPECT_EQ(managers, First->GetManagerCount()); EXPECT_EQ(0, Detaches);
    EXPECT_EQ(nullptr, Disabled.Query(0)->m_ExitInstanceFct);
}
