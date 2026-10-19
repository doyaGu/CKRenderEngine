#include <gtest/gtest.h>
#include "CKContext.h"
#include "CKRasterizerRegistration.h"
#include "VxSharedLibrary.h"
#include <Windows.h>

namespace {
typedef CKRasterizer *(*StartFunction)(WIN_HANDLE);
typedef void (*CloseFunction)(CKRasterizer *);
StartFunction NullStart;
CloseFunction NullClose;
int Starts, Closes;

CKRasterizer *StartObserved(WIN_HANDLE window) { ++Starts; return NullStart(window); }
void CloseObserved(CKRasterizer *rasterizer) { ++Closes; NullClose(rasterizer); }
void FillObserved(void *value) {
    CKRasterizerInfo *info = static_cast<CKRasterizerInfo *>(value);
    CKNullRasterizerGetInfo(info);
    NullStart = info->StartFct; NullClose = info->CloseFct;
    info->StartFct = StartObserved; info->CloseFct = CloseObserved;
}
DWORD WINAPI EmptyThread(void *) { return 73; }

class RenderEngineModuleLifetimeTest : public ::testing::Test {
protected:
    struct Probe {
        int Detaches = 0;
        INSTANCE_HANDLE Owned = nullptr;
    } FirstProbe, SecondProbe;
    HMODULE Module = nullptr, ExtraModuleReference = nullptr;
    CKContext *First = nullptr, *Second = nullptr;
    bool Started = false;
    char Directory[2048];
    typedef CKPluginInfo *(*QueryFunction)(int);
    QueryFunction Query = nullptr;
    INSTANCE_HANDLE (*ModuleHandle)() = nullptr;
    int (*Count)() = nullptr;
    const CKRasterizerInfo *(*Record)(int) = nullptr;
    void (*Register)(const char *) = nullptr;
    void (*RegisterStatic)() = nullptr;
    void (*Release)() = nullptr;

    void Path(char *output, size_t size, const char *name) {
        snprintf(output, size, "%s%s", Directory, name);
    }
    void LoadEngine() {
        char path[2048]; Path(path, sizeof(path), "CKREModuleLifetimeEngine.dll");
        Module = LoadLibraryA(path); ASSERT_NE(nullptr, Module);
        Query = reinterpret_cast<QueryFunction>(GetProcAddress(Module, "CKGetPluginInfo"));
        ModuleHandle = reinterpret_cast<INSTANCE_HANDLE (*)()>(GetProcAddress(Module, "CKRETestModuleHandle"));
        Count = reinterpret_cast<int (*)()>(GetProcAddress(Module, "CKRETestProviderCount"));
        Record = reinterpret_cast<const CKRasterizerInfo *(*)(int)>(GetProcAddress(Module, "CKRETestProvider"));
        Register = reinterpret_cast<void (*)(const char *)>(GetProcAddress(Module, "CKRETestRegisterProvider"));
        RegisterStatic = reinterpret_cast<void (*)()>(GetProcAddress(Module, "CKRETestRegisterStaticProvider"));
        Release = reinterpret_cast<void (*)()>(GetProcAddress(Module, "CKRETestReleaseProviders"));
        ASSERT_NE(nullptr, Query); ASSERT_NE(nullptr, ModuleHandle); ASSERT_NE(nullptr, Count);
        ASSERT_NE(nullptr, Record); ASSERT_NE(nullptr, Register); ASSERT_NE(nullptr, RegisterStatic); ASSERT_NE(nullptr, Release);
        EXPECT_EQ(static_cast<INSTANCE_HANDLE>(Module), ModuleHandle());
        EXPECT_EQ(0, Count());
    }
    void AddProbe(Probe &probe, const char *name = "CKREPluginLifetimeProbe.dll") {
        const int detachesBefore = probe.Detaches;
        char path[2048]; Path(path, sizeof(path), name);
        HMODULE initial = LoadLibraryA(path); ASSERT_NE(nullptr, initial);
        typedef void (*Configure)(void (*)(void *), int *);
        Configure configure = reinterpret_cast<Configure>(GetProcAddress(initial, "CKREConfigureLifetimeProbe"));
        ASSERT_NE(nullptr, configure); configure(FillObserved, &probe.Detaches);
        const int index = Count(); Register(path);
        EXPECT_EQ(index + 1, Count());
        if (Count() == index + 1) probe.Owned = Record(index)->DllInstance;
        EXPECT_NE(nullptr, probe.Owned);
        EXPECT_TRUE(FreeLibrary(initial)); EXPECT_EQ(detachesBefore, probe.Detaches);
    }
    void UnloadEngine() {
        if (First) { CKCloseContext(First); First = nullptr; }
        if (Second) { CKCloseContext(Second); Second = nullptr; }
        // Clear core class descriptors while their module hooks are still live.
        if (Started) { CKShutdown(); Started = false; }
        if (ExtraModuleReference) { EXPECT_TRUE(FreeLibrary(ExtraModuleReference)); ExtraModuleReference = nullptr; }
        if (Module) { EXPECT_TRUE(FreeLibrary(Module)); Module = nullptr; }
    }
    static void RecoverOldLeakedReference(Probe &probe) {
        // Only needed when running these regressions against the old module.
        // A successful unload has already destroyed this SDL handle wrapper.
        if (probe.Owned && probe.Detaches == 0) {
            VxSharedLibrary library; library.Attach(probe.Owned); library.ReleaseLibrary();
        }
        probe.Owned = nullptr;
    }
    void SetUp() override {
        DWORD length = GetModuleFileNameA(nullptr, Directory, sizeof(Directory));
        ASSERT_GT(length, 0u); ASSERT_LT(length, static_cast<DWORD>(sizeof(Directory)));
        char *slash = strrchr(Directory, '\\'); ASSERT_NE(nullptr, slash); slash[1] = '\0';
        Starts = Closes = 0;
        ASSERT_EQ(CK_OK, CKStartUp()); Started = true;
        LoadEngine();
    }
    void TearDown() override {
        UnloadEngine();
        RecoverOldLeakedReference(FirstProbe); RecoverOldLeakedReference(SecondProbe);
    }
};

TEST_F(RenderEngineModuleLifetimeTest, EmptyModuleCanUnload) {
    EXPECT_EQ(0, Count()); UnloadEngine();
}

TEST_F(RenderEngineModuleLifetimeTest, ExplicitReleasePreservesStaticEntries) {
    RegisterStatic(); ASSERT_EQ(1, Count());
    EXPECT_EQ(nullptr, Record(0)->DllInstance);
    StartFunction start = Record(0)->StartFct; CloseFunction close = Record(0)->CloseFct;
    Release(); Release();
    EXPECT_EQ(1, Count()); EXPECT_STREQ("test-static", Record(0)->DllName.CStr());
    EXPECT_EQ(start, Record(0)->StartFct); EXPECT_EQ(close, Record(0)->CloseFct);
}

TEST_F(RenderEngineModuleLifetimeTest, ExplicitReleaseClearsOnlyOwnedHandlesAndRetainsMetadata) {
    RegisterStatic(); AddProbe(FirstProbe); AddProbe(SecondProbe, "CKREPluginLifetimeProbe2.dll");
    ASSERT_EQ(3, Count());
    XString name = Record(1)->DllName, description = Record(1)->Desc;
    StartFunction start = Record(1)->StartFct; CloseFunction close = Record(1)->CloseFct;
    Release();
    EXPECT_EQ(1, FirstProbe.Detaches); EXPECT_EQ(1, SecondProbe.Detaches);
    EXPECT_EQ(3, Count());
    for (int i = 0; i < 3; ++i) EXPECT_EQ(nullptr, Record(i)->DllInstance);
    EXPECT_STREQ(name.CStr(), Record(1)->DllName.CStr()); EXPECT_STREQ(description.CStr(), Record(1)->Desc.CStr());
    EXPECT_EQ(start, Record(1)->StartFct); EXPECT_EQ(close, Record(1)->CloseFct);
    Release(); EXPECT_EQ(1, FirstProbe.Detaches); EXPECT_EQ(1, SecondProbe.Detaches);
}

TEST_F(RenderEngineModuleLifetimeTest, FinalModuleUnloadReleasesRegisteredProvider) {
    AddProbe(FirstProbe); UnloadEngine(); EXPECT_EQ(1, FirstProbe.Detaches);
}

TEST_F(RenderEngineModuleLifetimeTest, OnlyTheFinalModuleReferenceReleasesProviders) {
    AddProbe(FirstProbe);
    char path[2048]; Path(path, sizeof(path), "CKREModuleLifetimeEngine.dll");
    ExtraModuleReference = LoadLibraryA(path); ASSERT_EQ(Module, ExtraModuleReference);
    EXPECT_TRUE(FreeLibrary(ExtraModuleReference)); ExtraModuleReference = nullptr;
    EXPECT_EQ(0, FirstProbe.Detaches); EXPECT_NE(nullptr, Record(0)->DllInstance);
    UnloadEngine(); EXPECT_EQ(1, FirstProbe.Detaches);
}

TEST_F(RenderEngineModuleLifetimeTest, ContextInstancesCloseBeforeFinalProviderUnload) {
    CKPluginInfo *info = Query(0); ASSERT_NE(nullptr, info); ASSERT_EQ(nullptr, info->m_ExitInstanceFct);
    AddProbe(FirstProbe);
    ASSERT_EQ(CK_OK, CKCreateContext(&First, nullptr, 0, 0)); ASSERT_EQ(CK_OK, info->m_InitInstanceFct(First));
    ASSERT_EQ(CK_OK, CKCreateContext(&Second, nullptr, 0, 0)); ASSERT_EQ(CK_OK, info->m_InitInstanceFct(Second));
    ASSERT_NE(nullptr, First->GetRenderManager()); ASSERT_NE(nullptr, Second->GetRenderManager());
    EXPECT_EQ(2, Starts); EXPECT_EQ(0, Closes);
    CKCloseContext(First); First = nullptr;
    EXPECT_EQ(1, Closes); EXPECT_EQ(0, FirstProbe.Detaches);
    CKCloseContext(Second); Second = nullptr;
    EXPECT_EQ(2, Closes); EXPECT_EQ(0, FirstProbe.Detaches);
    UnloadEngine(); EXPECT_EQ(1, FirstProbe.Detaches); EXPECT_EQ(2, Closes);
}

TEST_F(RenderEngineModuleLifetimeTest, ExplicitReleaseThenModuleUnloadDoesNotReleaseTwice) {
    AddProbe(FirstProbe); Release(); EXPECT_EQ(1, FirstProbe.Detaches);
    UnloadEngine(); EXPECT_EQ(1, FirstProbe.Detaches);
}

TEST_F(RenderEngineModuleLifetimeTest, ThreeReloadCyclesStartWithEmptyModuleState) {
    for (int i = 0; i < 3; ++i) {
        if (i) LoadEngine();
        ASSERT_EQ(0, Count()); AddProbe(FirstProbe); UnloadEngine();
        ASSERT_EQ(i + 1, FirstProbe.Detaches);
    }
}

TEST_F(RenderEngineModuleLifetimeTest, ThreadNotificationsPreserveTheOwnerAndRegistry) {
    AddProbe(FirstProbe); INSTANCE_HANDLE owned = Record(0)->DllInstance;
    HANDLE thread = CreateThread(nullptr, 0, EmptyThread, nullptr, 0, nullptr); ASSERT_NE(nullptr, thread);
    EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(thread, 5000));
    DWORD result = 0; EXPECT_TRUE(GetExitCodeThread(thread, &result)); EXPECT_EQ(73u, result); CloseHandle(thread);
    EXPECT_EQ(static_cast<INSTANCE_HANDLE>(Module), ModuleHandle()); EXPECT_EQ(1, Count());
    EXPECT_EQ(owned, Record(0)->DllInstance); EXPECT_EQ(0, FirstProbe.Detaches);
    Release(); EXPECT_EQ(1, FirstProbe.Detaches);
}

TEST_F(RenderEngineModuleLifetimeTest, FinalUnloadReleasesEveryDynamicProviderAmongStaticEntries) {
    RegisterStatic(); AddProbe(FirstProbe); RegisterStatic();
    AddProbe(SecondProbe, "CKREPluginLifetimeProbe2.dll");
    ASSERT_EQ(4, Count()); UnloadEngine();
    EXPECT_EQ(1, FirstProbe.Detaches); EXPECT_EQ(1, SecondProbe.Detaches);
}
} // namespace
