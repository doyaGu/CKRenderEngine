#include <gtest/gtest.h>
#include "CKContext.h"
#include "CKRasterizerRegistration.h"
#include <Windows.h>

static int DiscoveryInfoCalls;
static void DiscoveryValidInfo(void *value) {
    ++DiscoveryInfoCalls;
    CKRasterizerInfo *info = static_cast<CKRasterizerInfo *>(value);
    CKNullRasterizerGetInfo(info);
    info->Desc = "discovery provider";
    info->DllName = "provider must not choose its filename";
    info->DllInstance = reinterpret_cast<INSTANCE_HANDLE>(static_cast<size_t>(1));
}
static void DiscoveryWrongRevision(void *value) {
    DiscoveryValidInfo(value);
    static_cast<CKRasterizerInfo *>(value)->InterfaceRevision = CKRST_INTERFACE_REVISION + 1;
}
static void DiscoveryNoStart(void *value) {
    DiscoveryValidInfo(value); static_cast<CKRasterizerInfo *>(value)->StartFct = nullptr;
}
static void DiscoveryNoClose(void *value) {
    DiscoveryValidInfo(value); static_cast<CKRasterizerInfo *>(value)->CloseFct = nullptr;
}
static void DiscoveryStaticInfo(CKRasterizerInfo *info) { DiscoveryValidInfo(info); }
static void DiscoveryStaticInvalid(CKRasterizerInfo *info) { DiscoveryWrongRevision(info); }
static void DiscoveryStaticNoStart(CKRasterizerInfo *info) { DiscoveryNoStart(info); }
static void DiscoveryStaticNoClose(CKRasterizerInfo *info) { DiscoveryNoClose(info); }

class RenderEngineRasterizerRegistrationTest : public ::testing::Test {
protected:
    struct Probe {
        char Path[2048] = {};
        HMODULE Initial = nullptr;
        int Detaches = 0;
    } Probes[6];
    int ProbeCount = 0;
    char SourceDirectory[2048], Directory[2048], OriginalDirectory[2048];
    HMODULE Module = nullptr;
    bool Started = false;
    static LONG Serial;
    int (*Count)() = nullptr;
    const CKRasterizerInfo *(*Record)(int) = nullptr;
    void (*Register)(const char *) = nullptr;
    void (*RegisterStatic)(CKRST_GETINFO, const char *) = nullptr;
    void (*RegisterNull)() = nullptr;
    void (*Enumerate)() = nullptr;
    CKBOOL (*EnumerationDone)() = nullptr;
    void (*Release)() = nullptr;
    void (*ModulePath)(char *, int) = nullptr;
    const char *(*Mask)(const char *) = nullptr;

    void Copy(const char *sourceName, const char *destinationName) {
        char source[2048], destination[2048];
        snprintf(source, sizeof(source), "%s%s", SourceDirectory, sourceName);
        snprintf(destination, sizeof(destination), "%s\\%s", Directory, destinationName);
        ASSERT_TRUE(CopyFileA(source, destination, FALSE));
    }
    Probe &AddProbe(const char *filename, void (*fill)(void *) = DiscoveryValidInfo, bool missingExport = false) {
        Probe &probe = Probes[ProbeCount++];
        Copy(missingExport ? "CKRENoInfoLifetimeProbe.dll" : "CKREPluginLifetimeProbe.dll", filename);
        snprintf(probe.Path, sizeof(probe.Path), "%s\\%s", Directory, filename);
        probe.Initial = LoadLibraryA(probe.Path); EXPECT_NE(nullptr, probe.Initial);
        typedef void (*Configure)(void (*)(void *), int *);
        Configure configure = reinterpret_cast<Configure>(GetProcAddress(probe.Initial, "CKREConfigureLifetimeProbe"));
        EXPECT_NE(nullptr, configure);
        if (configure) configure(fill, &probe.Detaches);
        return probe;
    }
    static void DropInitial(Probe &probe) {
        ASSERT_NE(nullptr, probe.Initial); EXPECT_TRUE(FreeLibrary(probe.Initial)); probe.Initial = nullptr;
    }
    void AssertUsable(const CKRasterizerInfo *info) {
        ASSERT_NE(nullptr, info); ASSERT_NE(nullptr, info->StartFct); ASSERT_NE(nullptr, info->CloseFct);
        CKRasterizer *instance = info->StartFct(nullptr); ASSERT_NE(nullptr, instance);
        EXPECT_GT(instance->GetDriverCount(), 0); info->CloseFct(instance);
    }
    void AssertRejected(void (*fill)(void *), bool missingExport = false) {
        Probe &probe = AddProbe("rejected.dll", fill, missingExport);
        Register(probe.Path); EXPECT_EQ(0, Count()); DropInitial(probe); EXPECT_EQ(1, probe.Detaches);
        EXPECT_EQ(missingExport ? 0 : 1, DiscoveryInfoCalls);
    }
    void SetUp() override {
        DWORD length = GetModuleFileNameA(nullptr, SourceDirectory, sizeof(SourceDirectory));
        ASSERT_GT(length, 0u); ASSERT_LT(length, static_cast<DWORD>(sizeof(SourceDirectory)));
        char *slash = strrchr(SourceDirectory, '\\'); ASSERT_NE(nullptr, slash); slash[1] = '\0';
        ASSERT_GT(GetCurrentDirectoryA(sizeof(OriginalDirectory), OriginalDirectory), 0u);
        char temporary[2048]; ASSERT_GT(GetTempPathA(sizeof(temporary), temporary), 0u);
        snprintf(Directory, sizeof(Directory), "%sCKREDiscovery-%lu-%ld", temporary, GetCurrentProcessId(), InterlockedIncrement(&Serial));
        ASSERT_TRUE(CreateDirectoryA(Directory, nullptr));
        char nested[2048]; snprintf(nested, sizeof(nested), "%s\\nested", Directory); ASSERT_TRUE(CreateDirectoryA(nested, nullptr));
        snprintf(nested, sizeof(nested), "%s\\foreign", Directory); ASSERT_TRUE(CreateDirectoryA(nested, nullptr));
        DiscoveryInfoCalls = 0;
        ASSERT_EQ(CK_OK, CKStartUp()); Started = true;
        Copy("CKREModuleLifetimeEngine.dll", "CKREModuleLifetimeEngine.dll");
        char path[2048]; snprintf(path, sizeof(path), "%s\\CKREModuleLifetimeEngine.dll", Directory);
        Module = LoadLibraryA(path); ASSERT_NE(nullptr, Module);
#define DISCOVERY_BIND(Member, Export) Member = reinterpret_cast<decltype(Member)>(GetProcAddress(Module, Export)); ASSERT_NE(nullptr, Member)
        DISCOVERY_BIND(Count, "CKRETestProviderCount"); DISCOVERY_BIND(Record, "CKRETestProvider");
        DISCOVERY_BIND(Register, "CKRETestRegisterProvider"); DISCOVERY_BIND(RegisterStatic, "CKRETestRegisterStaticInfo");
        DISCOVERY_BIND(RegisterNull, "CKRETestRegisterNullProvider"); DISCOVERY_BIND(Enumerate, "CKRETestEnumerateProviders");
        DISCOVERY_BIND(EnumerationDone, "CKRETestEnumerationDone"); DISCOVERY_BIND(Release, "CKRETestReleaseProviders");
        DISCOVERY_BIND(ModulePath, "CKRETestModulePath"); DISCOVERY_BIND(Mask, "CKRETestProviderMask");
#undef DISCOVERY_BIND
        ASSERT_EQ(0, Count()); ASSERT_FALSE(EnumerationDone());
    }
    void TearDown() override {
        SetCurrentDirectoryA(OriginalDirectory);
        if (Started) CKShutdown();
        if (Module) FreeLibrary(Module);
        for (int i = 0; i < ProbeCount; ++i) {
            if (Probes[i].Initial) FreeLibrary(Probes[i].Initial);
            DeleteFileA(Probes[i].Path);
        }
        char path[2048]; snprintf(path, sizeof(path), "%s\\CKREModuleLifetimeEngine.dll", Directory); DeleteFileA(path);
        snprintf(path, sizeof(path), "%s\\nested", Directory); RemoveDirectoryA(path);
        snprintf(path, sizeof(path), "%s\\foreign", Directory); RemoveDirectoryA(path);
        RemoveDirectoryA(Directory);
    }
};
LONG RenderEngineRasterizerRegistrationTest::Serial = 0;

TEST_F(RenderEngineRasterizerRegistrationTest, MissingPathsDoNotCreateRecords) {
    Register(nullptr); Register(""); Register("CKRE-no-such-file.dll");
    EXPECT_EQ(0, Count()); EXPECT_FALSE(EnumerationDone());
}
TEST_F(RenderEngineRasterizerRegistrationTest, MissingExportReleasesTheAttemptedLoad) { AssertRejected(DiscoveryValidInfo, true); }
TEST_F(RenderEngineRasterizerRegistrationTest, WrongRevisionReleasesTheAttemptedLoad) { AssertRejected(DiscoveryWrongRevision); }
TEST_F(RenderEngineRasterizerRegistrationTest, MissingStartReleasesTheAttemptedLoad) { AssertRejected(DiscoveryNoStart); }
TEST_F(RenderEngineRasterizerRegistrationTest, MissingCloseReleasesTheAttemptedLoad) { AssertRejected(DiscoveryNoClose); }

TEST_F(RenderEngineRasterizerRegistrationTest, ValidRegistrationOwnsItsReferenceAndOverridesProviderFilenameAndHandle) {
    Probe &probe = AddProbe("valid.dll"); Register(probe.Path); ASSERT_EQ(1, Count());
    EXPECT_STREQ(probe.Path, Record(0)->DllName.CStr()); EXPECT_STREQ("discovery provider", Record(0)->Desc.CStr());
    EXPECT_NE(nullptr, Record(0)->DllInstance); EXPECT_NE(reinterpret_cast<INSTANCE_HANDLE>(static_cast<size_t>(1)), Record(0)->DllInstance);
    EXPECT_EQ(CKRST_INTERFACE_REVISION, Record(0)->InterfaceRevision); AssertUsable(Record(0));
    DropInitial(probe); EXPECT_EQ(0, probe.Detaches); Release(); EXPECT_EQ(1, probe.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, RepeatedPathKeepsOneRecordAndDoesNotCallGetInfoAgain) {
    Probe &probe = AddProbe("same.dll"); Register(probe.Path); ASSERT_EQ(1, Count());
    INSTANCE_HANDLE owned = Record(0)->DllInstance;
    for (int i = 0; i < 3; ++i) Register(probe.Path);
    EXPECT_EQ(1, Count()); EXPECT_EQ(owned, Record(0)->DllInstance); EXPECT_EQ(1, DiscoveryInfoCalls);
    DropInitial(probe); EXPECT_EQ(0, probe.Detaches); Release(); EXPECT_EQ(1, probe.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, RelativeAliasOfTheSameLoadedModuleIsNotAnotherProvider) {
    Probe &probe = AddProbe("alias.dll"); Register(probe.Path); ASSERT_EQ(1, Count());
    ASSERT_TRUE(SetCurrentDirectoryA(Directory)); Register(".\\alias.dll");
    EXPECT_EQ(1, Count()); EXPECT_EQ(1, DiscoveryInfoCalls); EXPECT_STREQ(probe.Path, Record(0)->DllName.CStr());
    DropInitial(probe); Release(); EXPECT_EQ(1, probe.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, DistinctDllsWithTheSameFactoriesRemainDistinctProviders) {
    Probe &first = AddProbe("first.dll"), &second = AddProbe("second.dll");
    Register(first.Path); Register(second.Path); ASSERT_EQ(2, Count());
    EXPECT_NE(Record(0)->DllInstance, Record(1)->DllInstance);
    EXPECT_EQ(Record(0)->StartFct, Record(1)->StartFct); EXPECT_EQ(Record(0)->CloseFct, Record(1)->CloseFct);
    EXPECT_EQ(2, DiscoveryInfoCalls); DropInitial(first); DropInitial(second); Release();
    EXPECT_EQ(1, first.Detaches); EXPECT_EQ(1, second.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, ModulePathAndMaskIdentifyTheLoadedModule) {
    char actual[2048], expected[2048]; ModulePath(actual, sizeof(actual));
    ASSERT_GT(GetModuleFileNameA(Module, expected, sizeof(expected)), 0u); EXPECT_STREQ(expected, actual);
    EXPECT_STREQ("*Rasterizer.dll", Mask("")); EXPECT_STREQ("*DX8Rasterizer.dll", Mask("DX8"));
}
TEST_F(RenderEngineRasterizerRegistrationTest, EmptyDirectoryInstallsOneUsableNullProvider) {
    Enumerate(); ASSERT_TRUE(EnumerationDone()); ASSERT_EQ(1, Count());
    EXPECT_EQ(nullptr, Record(0)->DllInstance); EXPECT_STREQ("", Record(0)->DllName.CStr());
    EXPECT_STREQ("NULL Rasterizer", Record(0)->Desc.CStr()); EXPECT_EQ(CKRST_INTERFACE_REVISION, Record(0)->InterfaceRevision);
    AssertUsable(Record(0)); Enumerate(); EXPECT_EQ(1, Count());
}
TEST_F(RenderEngineRasterizerRegistrationTest, ValidDiscoveryDoesNotAppendTheNullFallback) {
    Probe &probe = AddProbe("AlphaRasterizer.dll"); Enumerate(); ASSERT_EQ(1, Count());
    EXPECT_STREQ(probe.Path, Record(0)->DllName.CStr()); EXPECT_EQ(1, DiscoveryInfoCalls); EXPECT_TRUE(EnumerationDone());
    DropInitial(probe); EXPECT_EQ(0, probe.Detaches); Release(); EXPECT_EQ(1, probe.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, DiscoveryUsesTheModuleDirectoryAndIncludesNestedProviders) {
    Probe &root = AddProbe("AlphaRasterizer.dll"), &nested = AddProbe("nested\\BetaRasterizer.dll");
    Probe &ignored = AddProbe("Ignored.dll");
    char outside[2048], foreign[2048]; GetTempPathA(sizeof(outside), outside);
    snprintf(foreign, sizeof(foreign), "%sCKREForeign-%lu-%ld", outside, GetCurrentProcessId(), Serial);
    ASSERT_TRUE(CreateDirectoryA(foreign, nullptr)); ASSERT_TRUE(SetCurrentDirectoryA(foreign));
    Enumerate(); EXPECT_EQ(2, Count()); EXPECT_EQ(2, DiscoveryInfoCalls); EXPECT_TRUE(EnumerationDone());
    bool foundRoot = false, foundNested = false;
    for (int i = 0; i < Count(); ++i) {
        if (!strcmp(root.Path, Record(i)->DllName.CStr())) foundRoot = true;
        if (!strcmp(nested.Path, Record(i)->DllName.CStr())) foundNested = true;
    }
    EXPECT_TRUE(foundRoot); EXPECT_TRUE(foundNested);
    DropInitial(root); DropInitial(nested); DropInitial(ignored);
    EXPECT_EQ(0, root.Detaches); EXPECT_EQ(0, nested.Detaches); EXPECT_EQ(1, ignored.Detaches);
    ASSERT_TRUE(SetCurrentDirectoryA(OriginalDirectory)); EXPECT_TRUE(RemoveDirectoryA(foreign));
}
TEST_F(RenderEngineRasterizerRegistrationTest, RejectedDirectoryStillGetsAUsableNullFallback) {
    Probe &bad = AddProbe("BadRasterizer.dll", DiscoveryWrongRevision);
    Probe &missing = AddProbe("MissingRasterizer.dll", DiscoveryValidInfo, true);
    Enumerate(); ASSERT_EQ(1, Count()); EXPECT_EQ(nullptr, Record(0)->DllInstance); AssertUsable(Record(0));
    DropInitial(bad); DropInitial(missing); EXPECT_EQ(1, bad.Detaches); EXPECT_EQ(1, missing.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, CachedEnumerationDoesNotDiscoverALaterFile) {
    Enumerate(); ASSERT_EQ(1, Count());
    Probe &late = AddProbe("LateRasterizer.dll"); Enumerate();
    EXPECT_EQ(1, Count()); EXPECT_EQ(0, DiscoveryInfoCalls); EXPECT_EQ(nullptr, Record(0)->DllInstance);
    DropInitial(late); EXPECT_EQ(1, late.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, ExplicitRegistrationBeforeEnumerationIsNotDuplicated) {
    Probe &probe = AddProbe("KnownRasterizer.dll"); Register(probe.Path); Enumerate();
    EXPECT_EQ(1, Count()); EXPECT_EQ(1, DiscoveryInfoCalls); EXPECT_TRUE(EnumerationDone());
    DropInitial(probe); Release(); EXPECT_EQ(1, probe.Detaches);
}
TEST_F(RenderEngineRasterizerRegistrationTest, StaticRegistrationValidatesTheSharedInterfaceWithoutTakingAModuleReference) {
    RegisterStatic(nullptr, "bad"); RegisterStatic(DiscoveryStaticInfo, nullptr);
    RegisterStatic(DiscoveryStaticInvalid, "bad"); RegisterStatic(DiscoveryStaticNoStart, "bad"); RegisterStatic(DiscoveryStaticNoClose, "bad");
    EXPECT_EQ(0, Count());
    RegisterStatic(DiscoveryStaticInfo, "built-in"); ASSERT_EQ(1, Count());
    EXPECT_EQ(nullptr, Record(0)->DllInstance); EXPECT_STREQ("built-in", Record(0)->DllName.CStr()); AssertUsable(Record(0));
}
TEST_F(RenderEngineRasterizerRegistrationTest, ExistingStaticProviderPreventsAnExtraNullFallback) {
    RegisterStatic(DiscoveryStaticInfo, "built-in"); Enumerate(); ASSERT_EQ(1, Count());
    EXPECT_TRUE(EnumerationDone()); EXPECT_STREQ("built-in", Record(0)->DllName.CStr());
}
TEST_F(RenderEngineRasterizerRegistrationTest, ExplicitNullRegistrationHasUsableFactoriesAndNoDllOwnership) {
    RegisterNull(); ASSERT_EQ(1, Count()); EXPECT_EQ(nullptr, Record(0)->DllInstance);
    EXPECT_STREQ("NULL Rasterizer", Record(0)->Desc.CStr()); AssertUsable(Record(0));
    Release(); EXPECT_EQ(1, Count()); EXPECT_EQ(nullptr, Record(0)->DllInstance);
}
TEST_F(RenderEngineRasterizerRegistrationTest, ReleaseDoesNotResetTheOneShotEnumerationCache) {
    Probe &probe = AddProbe("OnceRasterizer.dll"); Enumerate(); DropInitial(probe); Release();
    EXPECT_EQ(1, probe.Detaches); EXPECT_TRUE(EnumerationDone());
    Enumerate(); EXPECT_EQ(1, Count()); EXPECT_EQ(1, DiscoveryInfoCalls); EXPECT_EQ(nullptr, Record(0)->DllInstance);
}
