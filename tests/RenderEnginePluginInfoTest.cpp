#include <gtest/gtest.h>
#include "CKContext.h"
#define CKRE_STATIC_RENDER_ENGINE 1
#include "../src/CK2_3D.cpp"

extern void (*g_BuildNormalsFunc)(CKFace *, CKWORD *, int, VxVertex *, int);

namespace {
void FillProbeInfo(void *value) {
    CKNullRasterizerGetInfo(static_cast<CKRasterizerInfo *>(value));
}

class RenderEnginePluginInfoTest : public ::testing::Test {
protected:
    CKContext *First = nullptr, *Second = nullptr;
#ifdef _WIN32
    HMODULE InitialProbeReference = nullptr;
#endif
    int Detaches = 0;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(0, g_RasterizersInfo.Size()); ASSERT_FALSE(g_EnumerationDone);
    }
    void TearDown() override {
        if (Second) CKCloseContext(Second);
        if (First) CKCloseContext(First);
        // Module cleanup is explicit here, after all owned contexts are gone.
        ReleaseRasterizers(); g_RasterizersInfo.Clear(); g_EnumerationDone = FALSE;
#ifdef _WIN32
        if (InitialProbeReference) FreeLibrary(InitialProbeReference);
#endif
        CKShutdown();
    }
    static void Metadata(CKPluginInfo *info) {
        ASSERT_NE(nullptr, info);
        EXPECT_EQ(CKGUID(0x0aabcf63, 0), info->m_GUID);
        EXPECT_STREQ("Virtools", info->m_Author.CStr());
        EXPECT_STREQ("Default Render Engine", info->m_Description.CStr());
        EXPECT_STREQ("Virtools Default Rendering Engine", info->m_Summary.CStr());
        EXPECT_STREQ("", static_cast<char *>(info->m_Extension));
        EXPECT_EQ(5, static_cast<int>(info->m_Type)); EXPECT_EQ(1u, info->m_Version);
        EXPECT_EQ(&InitInstanceFct, info->m_InitInstanceFct);
        EXPECT_EQ(nullptr, info->m_ExitInstanceFct);
    }
    void CreateFirst(CKPluginInfo *info) {
        ASSERT_EQ(CK_OK, CKCreateContext(&First, nullptr, 0, 0));
        ASSERT_NE(nullptr, info->m_InitInstanceFct); ASSERT_EQ(CK_OK, info->m_InitInstanceFct(First));
        ASSERT_NE(nullptr, First->GetRenderManager());
    }
};

TEST_F(RenderEnginePluginInfoTest, DescriptorMatchesOriginalMetadataAndDoesNotPublishAnInstanceExit) {
    CKPluginInfo *info = CKGetPluginInfo(0); Metadata(info);
    EXPECT_EQ(&g_PluginInfo, info); EXPECT_TRUE(g_EnumerationDone); EXPECT_GT(g_RasterizersInfo.Size(), 0);
}

TEST_F(RenderEnginePluginInfoTest, RepeatedQueryRestoresMetadataWithoutReplacingItsStorage) {
    CKPluginInfo *info = CKGetPluginInfo(0); CKPluginInfo snapshot = *info;
    info->m_GUID = CKGUID(1, 2); info->m_Author = "changed"; info->m_Description = "changed";
    info->m_Summary = "changed"; info->m_Extension = "bad"; info->m_Version = 99;
    info->m_InitInstanceFct = nullptr; info->m_ExitInstanceFct = &ExitInstanceFct;
    EXPECT_EQ(info, CKGetPluginInfo(0)); Metadata(info);
    EXPECT_STREQ("Virtools", snapshot.m_Author.CStr()); EXPECT_STREQ("Default Render Engine", snapshot.m_Description.CStr());
    EXPECT_EQ(CKGUID(0x0aabcf63, 0), snapshot.m_GUID);
}

TEST_F(RenderEnginePluginInfoTest, HistoricalUnusedIndexDoesNotSelectAnotherDescriptor) {
    CKPluginInfo *info = CKGetPluginInfo(0);
    EXPECT_EQ(info, CKGetPluginInfo(1)); EXPECT_EQ(info, CKGetPluginInfo(-1)); EXPECT_EQ(info, CKGetPluginInfo(0x7fffffff));
    Metadata(info);
}

TEST_F(RenderEnginePluginInfoTest, QueryRestoresTheProcessorDispatcherAndRegistersUsableFactories) {
    g_BuildNormalsFunc = nullptr;
    CKPluginInfo *info = CKGetPluginInfo(0); ASSERT_NE(nullptr, g_BuildNormalsFunc);
    CKClassDesc *desc = CKGetClassDesc(32); ASSERT_NE(nullptr, desc); ASSERT_NE(nullptr, desc->CreationFct);
    CreateFirst(info);
    RCKMesh *mesh = static_cast<RCKMesh *>(First->CreateObject(32, "query_mesh")); ASSERT_NE(nullptr, mesh);
    ASSERT_TRUE(mesh->SetVertexCount(3)); ASSERT_TRUE(mesh->SetFaceCount(1)); mesh->SetFaceVertexIndex(0, 0, 1, 2);
    VxVector vertices[] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0)};
    for (int i = 0; i < 3; ++i) mesh->SetVertexPosition(i, &vertices[i]);
    mesh->BuildFaceNormals();
    const VxVector &normal = mesh->GetFaceNormal(0);
    EXPECT_FLOAT_EQ(0, normal.x); EXPECT_FLOAT_EQ(0, normal.y); EXPECT_FLOAT_EQ(1, normal.z);
}

TEST_F(RenderEnginePluginInfoTest, ReturnedInitCallbackCreatesIndependentManagersForTwoContexts) {
    CKPluginInfo *info = CKGetPluginInfo(0); CreateFirst(info);
    ASSERT_EQ(CK_OK, CKCreateContext(&Second, nullptr, 0, 0)); ASSERT_EQ(CK_OK, info->m_InitInstanceFct(Second));
    ASSERT_NE(nullptr, Second->GetRenderManager()); EXPECT_NE(First->GetRenderManager(), Second->GetRenderManager());
    CKObject *first = First->CreateObject(30, "first_material"), *second = Second->CreateObject(30, "second_material");
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); EXPECT_NE(first, second);
}

TEST_F(RenderEnginePluginInfoTest, QueryDoesNotDuplicateEnumerationOrReplaceALiveManager) {
    CKPluginInfo *info = CKGetPluginInfo(0); CreateFirst(info);
    CKRenderManager *manager = First->GetRenderManager(); const int objects = First->GetObjectCount();
    const int entries = g_RasterizersInfo.Size(), managers = First->GetManagerCount();
    for (int i = 0; i < 4; ++i) EXPECT_EQ(info, CKGetPluginInfo(i));
    EXPECT_EQ(entries, g_RasterizersInfo.Size()); EXPECT_EQ(manager, First->GetRenderManager());
    EXPECT_EQ(objects, First->GetObjectCount()); EXPECT_EQ(managers, First->GetManagerCount());
}

#ifdef _WIN32
TEST_F(RenderEnginePluginInfoTest, InstanceDispatchCannotUnloadAModuleOwnedProviderWhileAnotherContextIsLive) {
    CKPluginInfo *info = CKGetPluginInfo(0);
    char path[2048]; const DWORD count = GetModuleFileNameA(nullptr, path, sizeof(path));
    ASSERT_GT(count, 0u); ASSERT_LT(count, static_cast<DWORD>(sizeof(path)));
    char *slash = strrchr(path, '\\'); ASSERT_NE(nullptr, slash);
    snprintf(slash + 1, sizeof(path) - static_cast<size_t>(slash + 1 - path), "CKREPluginLifetimeProbe.dll");
    InitialProbeReference = LoadLibraryA(path); ASSERT_NE(nullptr, InitialProbeReference);
    typedef void (__cdecl *Configure)(void (__cdecl *)(void *), int *);
    Configure configure = reinterpret_cast<Configure>(GetProcAddress(InitialProbeReference, "CKREConfigureLifetimeProbe"));
    ASSERT_NE(nullptr, configure); configure(&FillProbeInfo, &Detaches);
    const int entry = g_RasterizersInfo.Size(); RegisterRasterizer(path);
    ASSERT_EQ(entry + 1, g_RasterizersInfo.Size());
    const INSTANCE_HANDLE owned = g_RasterizersInfo[entry].DllInstance; ASSERT_NE(nullptr, owned);
    ASSERT_EQ(CKRST_INTERFACE_REVISION, g_RasterizersInfo[entry].InterfaceRevision);
    ASSERT_TRUE(FreeLibrary(InitialProbeReference)); InitialProbeReference = nullptr;
    ASSERT_EQ(0, Detaches);
    CreateFirst(info); ASSERT_EQ(CK_OK, CKCreateContext(&Second, nullptr, 0, 0));
    ASSERT_EQ(CK_OK, info->m_InitInstanceFct(Second));
    // Dispatch the callback exactly as a host would; CKCloseContext itself is
    // not asserted to perform this dispatch.
    if (info->m_ExitInstanceFct) EXPECT_EQ(CK_OK, info->m_ExitInstanceFct(First));
    EXPECT_EQ(owned, g_RasterizersInfo[entry].DllInstance); EXPECT_EQ(0, Detaches);
    CKCloseContext(First); First = nullptr;
    ASSERT_NE(nullptr, Second->GetRenderManager()); ASSERT_NE(nullptr, Second->CreateObject(32, "still_usable"));
    CKCloseContext(Second); Second = nullptr;
    ReleaseRasterizers(); EXPECT_EQ(1, Detaches); EXPECT_EQ(nullptr, g_RasterizersInfo[entry].DllInstance);
}
#endif

TEST_F(RenderEnginePluginInfoTest, QueryAndReturnedInitRemainUsableAfterAContextHasClosed) {
    CKPluginInfo *info = CKGetPluginInfo(0); CreateFirst(info);
    CKCloseContext(First); First = nullptr;
    EXPECT_EQ(info, CKGetPluginInfo(0)); CreateFirst(info);
    CKObject *camera = First->CreateObject(34, "next_context_camera"); ASSERT_NE(nullptr, camera); EXPECT_EQ(34, camera->GetClassID());
}
} // namespace
