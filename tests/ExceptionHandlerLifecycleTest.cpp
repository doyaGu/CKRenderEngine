#include <gtest/gtest.h>
#include "../src/CKException.h"

#if CKRE_ENABLE_DEBUG_LOGGER
// Exercise the real private symbol-session helpers without production hooks.
#include "../src/CKException.cpp"
#include "CKRenderSettings.h"
#endif

static LONG WINAPI ExceptionHostFilterA(EXCEPTION_POINTERS *) {
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI ExceptionHostFilterB(EXCEPTION_POINTERS *) {
    return EXCEPTION_EXECUTE_HANDLER;
}

static LPTOP_LEVEL_EXCEPTION_FILTER CurrentExceptionFilter() {
    LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(nullptr);
    SetUnhandledExceptionFilter(current);
    return current;
}

class ExceptionHandlerLifecycleTest : public ::testing::Test {
protected:
    LPTOP_LEVEL_EXCEPTION_FILTER Previous = nullptr;
#if CKRE_ENABLE_DEBUG_LOGGER
    DWORD PreviousSymbolOptions = 0;
#endif

    void SetUp() override {
        Previous = SetUnhandledExceptionFilter(ExceptionHostFilterA);
#if CKRE_ENABLE_DEBUG_LOGGER
        PreviousSymbolOptions = SymGetOptions();
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Debug, "Output", "false");
        CKDebugLogger::Instance().EnableOutput(false);
#endif
    }

    void TearDown() override {
        CKRemoveExceptionHandler();
#if CKRE_ENABLE_DEBUG_LOGGER
        CleanupSymbols();
        SymSetOptions(PreviousSymbolOptions);
        CKRenderSettingsClearOverridesForTests();
#endif
        SetUnhandledExceptionFilter(Previous);
    }
};

#if CKRE_ENABLE_DEBUG_LOGGER

TEST_F(ExceptionHandlerLifecycleTest, InstallReplacesAndRemoveRestoresTheHostFilter) {
    CKInstallExceptionHandler();
    EXPECT_EQ(CKExceptionHandler, CurrentExceptionFilter());
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterA, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, RepeatedInstallRetainsTheOriginalHostFilter) {
    CKInstallExceptionHandler();
    CKInstallExceptionHandler();
    CKInstallExceptionHandler();
    EXPECT_EQ(CKExceptionHandler, CurrentExceptionFilter());
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterA, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, RemoveBeforeInstallDoesNotEraseTheHostFilter) {
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterA, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, RepeatedRemoveDoesNotEraseTheRestoredHostFilter) {
    CKInstallExceptionHandler();
    CKRemoveExceptionHandler();
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterA, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, ANewCycleCapturesTheCurrentHostInsteadOfTheOldOne) {
    CKInstallExceptionHandler();
    CKRemoveExceptionHandler();
    SetUnhandledExceptionFilter(ExceptionHostFilterB);
    CKInstallExceptionHandler();
    EXPECT_EQ(CKExceptionHandler, CurrentExceptionFilter());
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterB, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, NullIsAValidPredecessorToRestore) {
    SetUnhandledExceptionFilter(nullptr);
    CKInstallExceptionHandler();
    EXPECT_EQ(CKExceptionHandler, CurrentExceptionFilter());
    CKRemoveExceptionHandler();
    EXPECT_EQ(nullptr, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, RemovalPreservesAFilterInstalledLaterByTheHost) {
    CKInstallExceptionHandler();
    SetUnhandledExceptionFilter(ExceptionHostFilterB);
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterB, CurrentExceptionFilter());
    CKInstallExceptionHandler();
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterB, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, InstallingTheFilterDoesNotEagerlyOpenASymbolSession) {
    EXPECT_FALSE(g_SymbolsInitialized);
    CKInstallExceptionHandler();
    EXPECT_FALSE(g_SymbolsInitialized);
    CKRemoveExceptionHandler();
    EXPECT_FALSE(g_SymbolsInitialized);
}

TEST_F(ExceptionHandlerLifecycleTest, RepeatedEnsureKeepsTheRealLoadedModuleSession) {
    const DWORD64 address = reinterpret_cast<DWORD64>(&CurrentExceptionFilter);
    ASSERT_TRUE(EnsureSymbolsInitialized());
    EXPECT_TRUE(g_SymbolsInitialized);
    DWORD64 base = SymGetModuleBase64(GetCurrentProcess(), address);
    ASSERT_NE(0u, base);
    EXPECT_TRUE(EnsureSymbolsInitialized());
    EXPECT_EQ(base, SymGetModuleBase64(GetCurrentProcess(), address));
}

TEST_F(ExceptionHandlerLifecycleTest, CleanupWithoutOwnershipPreservesAnExternalSymbolSession) {
    const DWORD64 address = reinterpret_cast<DWORD64>(&CurrentExceptionFilter);
    ASSERT_FALSE(g_SymbolsInitialized);
    ASSERT_TRUE(SymInitialize(GetCurrentProcess(), nullptr, TRUE));
    DWORD64 base = SymGetModuleBase64(GetCurrentProcess(), address);
    EXPECT_NE(0u, base);
    CleanupSymbols();
    EXPECT_EQ(base, SymGetModuleBase64(GetCurrentProcess(), address));
    EXPECT_TRUE(SymCleanup(GetCurrentProcess()));
}

TEST_F(ExceptionHandlerLifecycleTest, CleanupClosesOurRealSessionAndAllowsReinitialization) {
    const DWORD64 address = reinterpret_cast<DWORD64>(&CurrentExceptionFilter);
    ASSERT_TRUE(EnsureSymbolsInitialized());
    ASSERT_NE(0u, SymGetModuleBase64(GetCurrentProcess(), address));
    CleanupSymbols();
    EXPECT_FALSE(g_SymbolsInitialized);
    EXPECT_EQ(0u, SymGetModuleBase64(GetCurrentProcess(), address));
    CleanupSymbols();
    EXPECT_TRUE(EnsureSymbolsInitialized());
    EXPECT_NE(0u, SymGetModuleBase64(GetCurrentProcess(), address));
}

TEST_F(ExceptionHandlerLifecycleTest, RemoveClosesOurSymbolSessionAndRestoresTheHost) {
    const DWORD64 address = reinterpret_cast<DWORD64>(&CurrentExceptionFilter);
    CKInstallExceptionHandler();
    ASSERT_TRUE(EnsureSymbolsInitialized());
    ASSERT_NE(0u, SymGetModuleBase64(GetCurrentProcess(), address));
    CKRemoveExceptionHandler();
    EXPECT_FALSE(g_SymbolsInitialized);
    EXPECT_EQ(0u, SymGetModuleBase64(GetCurrentProcess(), address));
    EXPECT_EQ(ExceptionHostFilterA, CurrentExceptionFilter());
}

#else

TEST_F(ExceptionHandlerLifecycleTest, DisabledLifecycleLeavesTheHostFilterInPlace) {
    CKRemoveExceptionHandler();
    CKInstallExceptionHandler();
    CKInstallExceptionHandler();
    CKRemoveExceptionHandler();
    CKRemoveExceptionHandler();
    EXPECT_EQ(ExceptionHostFilterA, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, DisabledHandlerAlwaysContinuesTheExceptionSearch) {
    EXCEPTION_RECORD record = {};
    CONTEXT context = {};
    EXCEPTION_POINTERS pointers = {&record, &context};
    EXPECT_EQ(EXCEPTION_CONTINUE_SEARCH, CKExceptionHandler(nullptr));
    EXPECT_EQ(EXCEPTION_CONTINUE_SEARCH, CKExceptionHandler(&pointers));
    EXPECT_EQ(ExceptionHostFilterA, CurrentExceptionFilter());
}

TEST_F(ExceptionHandlerLifecycleTest, DisabledLifecyclePreservesTheNullHostFilter) {
    SetUnhandledExceptionFilter(nullptr);
    CKInstallExceptionHandler();
    CKRemoveExceptionHandler();
    EXPECT_EQ(nullptr, CurrentExceptionFilter());
}

#endif
