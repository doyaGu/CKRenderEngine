#include <gtest/gtest.h>
#include <cstddef>
#include <type_traits>
#include "CKAll.h"
#include "CKRasterizer.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
int StartsA, StartsB, ClosedA, ClosedB, WrongCloser;
CKBOOL FailFirstA;

class RecordingRasterizer : public CKRasterizer {
public:
    explicit RecordingRasterizer(int kind, CKBOOL hardware = FALSE) : Kind(kind) {
        AddDriver(new CKRasterizerDriver(this, 0, kind == 1 ? "Driver A" : "Driver B", hardware));
    }
    int Kind;
};

CKRasterizer *StartA(WIN_HANDLE) {
    ++StartsA;
    return FailFirstA && StartsA == 1 ? nullptr : new RecordingRasterizer(1);
}
CKRasterizer *StartB(WIN_HANDLE) { ++StartsB; return new RecordingRasterizer(2); }
CKRasterizer *StartHardware(WIN_HANDLE) { ++StartsA; return new RecordingRasterizer(1, TRUE); }
void CloseA(CKRasterizer *rasterizer) {
    ++ClosedA;
    if (static_cast<RecordingRasterizer *>(rasterizer)->Kind != 1) ++WrongCloser;
    delete rasterizer;
}
void CloseB(CKRasterizer *rasterizer) {
    ++ClosedB;
    if (static_cast<RecordingRasterizer *>(rasterizer)->Kind != 2) ++WrongCloser;
    delete rasterizer;
}
CKRasterizerInfo Info(CKRST_STARTFUNCTION start, CKRST_CLOSEFUNCTION close) {
    CKRasterizerInfo info;
    info.StartFct = start; info.CloseFct = close;
    info.InterfaceRevision = CKRST_INTERFACE_REVISION;
    return info;
}

class RenderManagerLifecycleTest : public ::testing::Test {
protected:
    CKContext *First = nullptr, *Second = nullptr;
    XClassArray<CKRasterizerInfo> Previous;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        Previous = g_RasterizersInfo;
        g_RasterizersInfo.Clear();
        StartsA = StartsB = ClosedA = ClosedB = WrongCloser = 0;
        FailFirstA = FALSE;
        ASSERT_EQ(CK_OK, CKCreateContext(&First, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Second) CKCloseContext(Second);
        if (First) CKCloseContext(First);
        g_RasterizersInfo = Previous;
        CKShutdown();
    }
};

TEST_F(RenderManagerLifecycleTest, RegisteredIdentityOwnsHiddenRootsAndDefaultMaterial) {
    g_RasterizersInfo.PushBack(Info(StartA, CloseA));
    RCKRenderManager *manager = new RCKRenderManager(First);
    EXPECT_EQ(manager, First->GetRenderManager());
    EXPECT_EQ(RENDER_MANAGER_GUID, manager->GetGuid());
    EXPECT_STREQ("Render Manager", manager->GetName());
    EXPECT_FALSE(std::is_copy_constructible<RCKRenderManager>::value);
    EXPECT_FALSE(std::is_copy_assignable<RCKRenderManager>::value);
    ASSERT_NE(nullptr, manager->m_DefaultMat);
    EXPECT_EQ(manager->m_DefaultMat, manager->GetDefaultMaterial());
    EXPECT_TRUE(manager->m_DefaultMat->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
    CK2dEntity *roots[2] = {manager->m_2DRootFore, manager->m_2DRootBack};
    CK_ID ids[2] = {manager->m_2DRootForeId, manager->m_2DRootBackId};
    for (int i = 0; i < 2; ++i) {
        ASSERT_NE(nullptr, roots[i]);
        EXPECT_EQ(roots[i], First->GetObject(ids[i]));
        EXPECT_TRUE(roots[i]->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
        EXPECT_EQ(CKHIDE, roots[i]->IsVisible());
        EXPECT_EQ(0U, roots[i]->GetFlags() & (CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOCAMERAVIEW));
    }
    EXPECT_EQ(0, manager->GetRenderContextCount());
    EXPECT_EQ(0xffffffffU, manager->m_RenderContextMaskFree);
    CKDWORD masks[32];
    for (int i = 0; i < 32; ++i) { masks[i] = manager->GetRenderContextMaskFree(); EXPECT_EQ(CKDWORD(1) << i, masks[i]); }
    EXPECT_EQ(0U, manager->GetRenderContextMaskFree());
    manager->ReleaseRenderContextMaskFree(masks[17]);
    EXPECT_EQ(masks[17], manager->GetRenderContextMaskFree());
}

TEST_F(RenderManagerLifecycleTest, SoftwareFallbackBelongsToItsManagerWithoutChangingTheFactoryRegistry) {
    g_RasterizersInfo.PushBack(Info(StartHardware, CloseA));
    RCKRenderManager *manager = new RCKRenderManager(First);
    ASSERT_EQ(2, manager->GetRenderDriverCount());
    EXPECT_EQ(1, g_RasterizersInfo.Size());
    EXPECT_EQ(StartHardware, g_RasterizersInfo[0].StartFct);
    EXPECT_TRUE(manager->GetRenderDriverDescription(0)->IsHardware);
    EXPECT_FALSE(manager->GetRenderDriverDescription(1)->IsHardware);
    EXPECT_EQ(1, manager->GetPreferredSoftwareDriver());
    ASSERT_EQ(CK_OK, CKCloseContext(First)); First = nullptr;
    EXPECT_EQ(1, ClosedA); EXPECT_EQ(0, WrongCloser);
    EXPECT_EQ(1, g_RasterizersInfo.Size());
}

TEST_F(RenderManagerLifecycleTest, AFailedStartDoesNotRemoveTheFactoryForAnotherContext) {
    FailFirstA = TRUE;
    g_RasterizersInfo.PushBack(Info(StartA, CloseA));
    g_RasterizersInfo.PushBack(Info(StartB, CloseB));
    RCKRenderManager *first = new RCKRenderManager(First);
    EXPECT_EQ(1, first->GetRenderDriverCount());
    EXPECT_EQ(2, g_RasterizersInfo.Size());
    ASSERT_EQ(CK_OK, CKCreateContext(&Second, nullptr, 0, 0));
    RCKRenderManager *second = new RCKRenderManager(Second);
    EXPECT_EQ(2, second->GetRenderDriverCount());
    EXPECT_EQ(2, StartsA); EXPECT_EQ(2, StartsB);
    ASSERT_EQ(CK_OK, CKCloseContext(Second)); Second = nullptr;
    ASSERT_EQ(CK_OK, CKCloseContext(First)); First = nullptr;
    EXPECT_EQ(1, ClosedA); EXPECT_EQ(2, ClosedB); EXPECT_EQ(0, WrongCloser);
}

TEST_F(RenderManagerLifecycleTest, ClosingUsesTheInstanceFactoryEvenAfterTheGlobalRegistryIsReordered) {
    g_RasterizersInfo.PushBack(Info(StartA, CloseA));
    g_RasterizersInfo.PushBack(Info(StartB, CloseB));
    new RCKRenderManager(First);
    g_RasterizersInfo[0] = Info(StartB, CloseB);
    g_RasterizersInfo[1] = Info(StartA, CloseA);
    ASSERT_EQ(CK_OK, CKCreateContext(&Second, nullptr, 0, 0));
    new RCKRenderManager(Second);
    ASSERT_EQ(CK_OK, CKCloseContext(First)); First = nullptr;
    EXPECT_EQ(0, WrongCloser);
    EXPECT_EQ(1, ClosedA); EXPECT_EQ(1, ClosedB);
    ASSERT_EQ(CK_OK, CKCloseContext(Second)); Second = nullptr;
    EXPECT_EQ(0, WrongCloser);
    EXPECT_EQ(2, ClosedA); EXPECT_EQ(2, ClosedB);
}

TEST_F(RenderManagerLifecycleTest, SdkBaseDispatchAndSharedPrefixFollowThePlatformLayout) {
    g_RasterizersInfo.PushBack(Info(StartA, CloseA));
    RCKRenderManager *manager = new RCKRenderManager(First);
    CKRenderManager *api = First->GetRenderManager();
    EXPECT_EQ(static_cast<void *>(manager), static_cast<void *>(api));
    EXPECT_EQ(1, api->GetRenderDriverCount());
    ASSERT_NE(nullptr, api->GetRenderDriverDescription(0));
    EXPECT_STREQ("Driver A", api->GetRenderDriverDescription(0)->DriverDesc);
    EXPECT_EQ(0, api->GetRenderContextCount());
    EXPECT_EQ(7, api->GetEffectCount());
    EXPECT_EQ(CK_OK, api->PreProcess());
    EXPECT_EQ(0x000c029fu, api->GetValidFunctionsMask());
    const unsigned char *origin = reinterpret_cast<const unsigned char *>(manager);
    const void *members[] = {
        &manager->m_TemporaryPreRenderCallbacks, &manager->m_TemporaryPostRenderCallbacks,
        &manager->m_RenderContexts, &manager->m_Rasterizers, &manager->m_Drivers,
        &manager->m_DriverCount, &manager->m_DefaultMat, &manager->m_RenderContextMaskFree,
        &manager->m_SceneGraphRootNode, &manager->m_MovedEntities, &manager->m_Entities,
        &manager->m_VertexCacheOptimizer
    };
    // The Win32 prefix is the original manager's SDK-facing prefix. The x64
    // layout widens pointers and containers while retaining the same fields.
    const std::size_t x86[] = {40, 52, 64, 72, 84, 88, 92, 96, 100, 184, 196, 208};
    const std::size_t x64[] = {56, 80, 104, 120, 144, 152, 160, 168, 176, 296, 320, 344};
    for (int i = 0; i < 12; ++i) {
        const std::size_t offset = reinterpret_cast<const unsigned char *>(members[i]) - origin;
        EXPECT_EQ(sizeof(void *) == 4 ? x86[i] : x64[i], offset);
    }
    EXPECT_EQ(sizeof(void *) == 4 ? 40u : 56u, sizeof(CKRenderManager));
    EXPECT_EQ(sizeof(void *) == 4 ? 640u : 1128u, sizeof(RCKRenderManager));
    EXPECT_EQ(sizeof(void *), alignof(RCKRenderManager));
}
} // namespace
