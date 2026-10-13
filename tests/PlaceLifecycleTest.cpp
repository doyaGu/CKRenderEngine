#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "RCKPlace.h"
#include "RCKCamera.h"
#include "RCKRenderObject.h"
#include "RCKMesh.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class PlaceLayoutProbe : public RCKPlace {
public:
    static CK_ID Level(RCKPlace &place) { return place.*(&PlaceLayoutProbe::m_Level); }
    static void RecordLayout(RCKPlace &place) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&place);
        std::size_t previous = sizeof(RCK3dEntity);
#define FIELD(field) { auto member = &PlaceLayoutProbe::field; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(place.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(place.*member), sizeof(RCKPlace)); \
        EXPECT_EQ(0u, address % alignof(decltype(place.*member))); \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(place.*member))); \
        previous = offset + sizeof(place.*member); }
        FIELD(m_Portals); FIELD(m_Level); FIELD(m_Camera); FIELD(m_ClippingRect);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKPlace)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKPlace)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class PlaceLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKPlace *API(RCKPlace &place) { return reinterpret_cast<CKPlace *>(&place); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
};
}

TEST_F(PlaceLifecycleTest, RegisteredDefaultsAndActualPlatformLayout) {
    RCKPlace *place = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE)); ASSERT_NE(nullptr, place);
    EXPECT_EQ(22, place->GetClassID()); EXPECT_TRUE(CKIsChildClassOf(place, CKCID_3DENTITY));
    EXPECT_EQ(place, Context->GetObject(place->GetID())); EXPECT_EQ(nullptr, place->GetDefaultCamera());
    EXPECT_EQ(0u, PlaceLayoutProbe::Level(*place)); EXPECT_EQ(0, place->GetPortalCount());
    EXPECT_EQ(20000, place->GetPriority()); EXPECT_EQ(nullptr, place->GetParent());
    const VxRect &clip = place->ViewportClip(); EXPECT_EQ(0.0f, clip.left); EXPECT_EQ(0.0f, clip.top);
    EXPECT_EQ(0.0f, clip.right); EXPECT_EQ(0.0f, clip.bottom); PlaceLayoutProbe::RecordLayout(*place);
}
TEST_F(PlaceLifecycleTest, NamedConstructionCopiesNameAndKeepsIndependentDefaults) {
    char name[] = "Place name"; RCKPlace first(Context, name), second(Context); name[0] = 'X';
    EXPECT_STREQ("Place name", first.GetName()); EXPECT_EQ(nullptr, second.GetName());
    first.ViewportClip().left = 12; EXPECT_EQ(0.0f, second.ViewportClip().left);
    EXPECT_EQ(20000, first.GetPriority()); EXPECT_EQ(20000, second.GetPriority());
}
TEST_F(PlaceLifecycleTest, ClassNameParameterAndNotificationRegistration) {
    EXPECT_STREQ("Place", RCKPlace::GetClassName()); CKClassDesc *desc = CKGetClassDesc(CKCID_PLACE);
    ASSERT_NE(nullptr, desc); desc->ToBeNotify.Clear(); desc->Parameter = CKGUID(); RCKPlace::Register();
    EXPECT_EQ(CKPGUID_PLACE, desc->Parameter); EXPECT_EQ(613757165u, desc->Parameter.d1);
    EXPECT_EQ(1275618803u, desc->Parameter.d2); EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_3DENTITY));
    EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_LEVEL)); EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_CAMERA));
}
TEST_F(PlaceLifecycleTest, DependencyCountAndLabelFollowNativeModeTable) {
    const int modes[] = {-1, 0, 1, 2, 3, 4, 100};
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(modes[i] == 1 || modes[i] == 2 || modes[i] == 4 ? 1 : 0, RCKPlace::GetDependenciesCount(modes[i]));
        EXPECT_STREQ("Portals", RCKPlace::GetDependencies(0, modes[i]));
        EXPECT_EQ(nullptr, RCKPlace::GetDependencies(-1, modes[i]));
        EXPECT_EQ(nullptr, RCKPlace::GetDependencies(1, modes[i]));
    }
}
TEST_F(PlaceLifecycleTest, DirectFactorySDKDispatchAndVirtualDestruction) {
    RCK3dEntity unrelated(Context); CKPlace *api = RCKPlace::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKPlace *place = reinterpret_cast<RCKPlace *>(api); const CK_ID id = place->GetID();
    EXPECT_EQ(CKCID_PLACE, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName()); EXPECT_EQ(0, api->GetPortalCount());
    api->ViewportClip().right = 0.75f; EXPECT_EQ(0.75f, place->ViewportClip().right);
    delete static_cast<CKObject *>(place); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&unrelated, Context->GetObject(unrelated.GetID()));
}
TEST_F(PlaceLifecycleTest, MemoryOccupationIncludesActualTailAndLivePortalStorage) {
    RCKPlace first(Context), second(Context), third(Context);
    const int tail = static_cast<int>(sizeof(RCKPlace) - sizeof(RCK3dEntity));
    EXPECT_EQ(first.RCK3dEntity::GetMemoryOccupation() + tail, first.GetMemoryOccupation());
    first.AddPortal(API(second), nullptr); first.AddPortal(API(third), nullptr);
    EXPECT_EQ(first.RCK3dEntity::GetMemoryOccupation() + tail + 2 * static_cast<int>(sizeof(CKPortalEntry)), first.GetMemoryOccupation());
    first.RemovePortal(API(second), nullptr);
    EXPECT_EQ(first.RCK3dEntity::GetMemoryOccupation() + tail + static_cast<int>(sizeof(CKPortalEntry)), first.GetMemoryOccupation());
    first.RemovePortal(API(third), nullptr); EXPECT_EQ(first.RCK3dEntity::GetMemoryOccupation() + tail, first.GetMemoryOccupation());
}
TEST_F(PlaceLifecycleTest, DirectDeletionDetachesFromSurvivingParent) {
    RCK3dEntity parent(Context); RCKPlace *place = new RCKPlace(Context); const CK_ID id = place->GetID();
    ASSERT_TRUE(place->SetParent(Entity(parent), TRUE)); ASSERT_EQ(1, parent.GetChildrenCount());
    delete place; EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(0, parent.GetChildrenCount());
    // Clear stale raw entries after asserting the old implementation failure, so teardown is safe.
    parent.m_Children.Clear();
}
TEST_F(PlaceLifecycleTest, DirectDeletionDetachesSurvivingDirectChildrenAndClearsTheirPlace) {
    RCK3dEntity child(Context); child.SetPosition3f(7, 11, 13); RCKPlace *place = new RCKPlace(Context);
    ASSERT_TRUE(child.SetParent(Entity(*place), TRUE)); EXPECT_EQ(API(*place), child.GetReferencePlace());
    const CK_ID id = place->GetID(); delete place; EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(nullptr, child.GetParent()); EXPECT_EQ(nullptr, child.GetReferencePlace());
    VxVector position; child.GetPosition(&position); EXPECT_EQ(7.0f, position.x); EXPECT_EQ(11.0f, position.y); EXPECT_EQ(13.0f, position.z);
    child.m_Parent = nullptr; child.m_Place = 0;
}
TEST_F(PlaceLifecycleTest, ContextDeletionUsesNativePreDeleteToDetachSurvivingChild) {
    new RCKRenderManager(Context);
    RCKPlace *place = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE)); ASSERT_NE(nullptr, place);
    RCK3dEntity *child = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(Entity(*place), TRUE)); const CK_ID childID = child->GetID(), placeID = place->GetID();
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(place, 0, &dependencies)); EXPECT_EQ(nullptr, Context->GetObject(placeID));
    ASSERT_EQ(child, Context->GetObject(childID)); EXPECT_EQ(nullptr, child->GetParent()); EXPECT_EQ(nullptr, child->GetReferencePlace());
}
