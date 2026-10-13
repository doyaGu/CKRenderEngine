#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKTargetLight.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TargetLightLifecycleProbe : public RCKTargetLight {
public:
    explicit TargetLightLifecycleProbe(CKContext *context) : RCKTargetLight(context) {}
    CK_ID TargetID() const { return m_Target; }
    void SetRawTarget(CK_ID id) { m_Target = id; }
    static void RecordLayout(RCKTargetLight &light) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&light);
        auto member = &TargetLightLifecycleProbe::m_Target;
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(light.*member));
        const std::size_t offset = address - begin;
        EXPECT_GE(offset, sizeof(RCKLight));
        EXPECT_LE(offset + sizeof(light.*member), sizeof(RCKTargetLight));
        EXPECT_EQ(0u, address % alignof(CK_ID));
        ::testing::Test::RecordProperty("m_Target_offset", static_cast<int>(offset));
        ::testing::Test::RecordProperty("m_Target_size", static_cast<int>(sizeof(light.*member)));
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKTargetLight)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKTargetLight)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCKLight)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class TargetLightLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetLight, CKCID_LIGHT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *API(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKDWORD Bound(CKDWORD flags) { return (flags | CK_3DENTITY_TARGETLIGHT) & ~CK_3DENTITY_FRAME; }
    static CKDWORD Released(CKDWORD flags) { return (flags & ~CK_3DENTITY_TARGETLIGHT) | CK_3DENTITY_FRAME; }
};
}

TEST_F(TargetLightLifecycleTest, RegisteredIdentityDefaultsAndActualPlatformLayout) {
    RCKTargetLight *light = static_cast<RCKTargetLight *>(Context->CreateObject(CKCID_TARGETLIGHT)); ASSERT_NE(nullptr, light);
    EXPECT_EQ(39, light->GetClassID()); EXPECT_EQ(light, Context->GetObject(light->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(light, CKCID_LIGHT)); EXPECT_TRUE(CKIsChildClassOf(light, CKCID_3DENTITY));
    EXPECT_EQ(nullptr, light->GetTarget()); EXPECT_EQ(VX_LIGHTPOINT, light->GetType());
    EXPECT_TRUE(light->GetActivity()); EXPECT_FALSE(light->GetSpecularFlag()); EXPECT_EQ(1.0f, light->GetLightPower());
    EXPECT_EQ(nullptr, light->GetSkin()); TargetLightLifecycleProbe::RecordLayout(*light);
}
TEST_F(TargetLightLifecycleTest, NamedConstructorPreservesContextIdentityAndLightDefaults) {
    char name[] = "Target light lifecycle"; RCKTargetLight light(Context, name);
    EXPECT_STREQ(name, light.GetName()); EXPECT_EQ(&light, Context->GetObject(light.GetID()));
    EXPECT_EQ(CKCID_TARGETLIGHT, light.GetClassID()); EXPECT_EQ(nullptr, light.GetTarget()); EXPECT_TRUE(light.IsVisible());
    EXPECT_EQ(5000.0f, light.GetRange()); EXPECT_EQ(nullptr, light.GetParent());
}
TEST_F(TargetLightLifecycleTest, ClassNameMatchesNativeDisplayName) {
    EXPECT_STREQ("Target Light", RCKTargetLight::GetClassName());
}
TEST_F(TargetLightLifecycleTest, RegistrationPublishesNativeParameterCopyMaskAndEntityNotification) {
    CKClassDesc *description = CKGetClassDesc(CKCID_TARGETLIGHT); ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID(); description->DefaultCopyDependencies = 0; RCKTargetLight::Register();
    EXPECT_EQ(CKPGUID_TARGETLIGHT, description->Parameter);
    EXPECT_EQ(0x4e5f22fcu, description->Parameter.d1); EXPECT_EQ(0x194709d4u, description->Parameter.d2);
    EXPECT_EQ(1u, description->DefaultCopyDependencies); EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_3DENTITY));
}
TEST_F(TargetLightLifecycleTest, DirectFactoryVirtualDestructorReleasesTargetMarkerAndContextRegistration) {
    RCK3dEntity target(Context); target.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_IGNOREANIMATION | CK_3DENTITY_TARGETCAMERA);
    CKTargetLight *api = RCKTargetLight::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKTargetLight *light = reinterpret_cast<RCKTargetLight *>(api); const CK_ID id = light->GetID();
    EXPECT_EQ(CKCID_TARGETLIGHT, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    ASSERT_NE(nullptr, light->CreateSkin()); light->GetSkin()->SetBoneCount(2); light->GetSkin()->SetVertexCount(3);
    api->SetTarget(API(target)); const CKDWORD flags = target.GetFlags(); ASSERT_EQ(API(target), api->GetTarget());
    delete static_cast<CKObject *>(light);
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(Released(flags), target.GetFlags());
    EXPECT_EQ(&target, Context->GetObject(target.GetID()));
}
TEST_F(TargetLightLifecycleTest, ContextDeletionUnlinksParentAndPreservesTargetWithoutDependencies) {
    new RCKRenderManager(Context); RCK3dEntity parent(Context), target(Context);
    RCKTargetLight *light = static_cast<RCKTargetLight *>(Context->CreateObject(CKCID_TARGETLIGHT)); ASSERT_NE(nullptr, light);
    light->SetParent(API(parent), FALSE); light->SetTarget(API(target)); ASSERT_EQ(1, parent.GetChildrenCount());
    const CK_ID id = light->GetID(); const CKDWORD flags = target.GetFlags();
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(light, 0, &dependencies));
    EXPECT_EQ(0, parent.GetChildrenCount()); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&target, Context->GetObject(target.GetID())); EXPECT_EQ(Released(flags), target.GetFlags());
}
TEST_F(TargetLightLifecycleTest, MemoryOccupationAddsActualPlatformTailToInheritedStorage) {
    RCKTargetLight light(Context); RCK3dEntity target(Context);
    const int tail = static_cast<int>(sizeof(RCKTargetLight) - sizeof(RCKLight)); EXPECT_GE(tail, 4);
    EXPECT_EQ(light.RCKLight::GetMemoryOccupation() + tail, light.GetMemoryOccupation());
    char name[] = "Target light memory"; light.SetName(name); light.SetTarget(API(target));
    ASSERT_NE(nullptr, light.CreateSkin()); light.GetSkin()->SetBoneCount(2); light.GetSkin()->SetVertexCount(3);
    EXPECT_EQ(light.RCKLight::GetMemoryOccupation() + tail, light.GetMemoryOccupation()); light.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, GetTargetResolvesContextObjectWithoutChangingEitherObjectsFlags) {
    TargetLightLifecycleProbe light(Context); RCK3dEntity target(Context); light.SetTarget(API(target));
    const CKDWORD flags = target.GetFlags(), lightFlags = light.GetObjectFlags();
    EXPECT_EQ(API(target), light.GetTarget()); EXPECT_EQ(target.GetID(), light.TargetID());
    EXPECT_EQ(flags, target.GetFlags()); EXPECT_EQ(lightFlags, light.GetObjectFlags()); light.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, GetTargetKeepsUnresolvedStoredIdentifier) {
    TargetLightLifecycleProbe light(Context); light.SetRawTarget(0x7ffabcde);
    EXPECT_EQ(nullptr, light.GetTarget()); EXPECT_EQ(0x7ffabcdeu, light.TargetID()); light.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, BindChangesOnlyTargetAndFrameMarkersWithoutTakingOwnership) {
    TargetLightLifecycleProbe light(Context); RCK3dEntity target(Context);
    target.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_IGNOREANIMATION | CK_3DENTITY_TARGETCAMERA);
    const CKDWORD flags = target.GetFlags(); light.SetTarget(API(target));
    EXPECT_EQ(Bound(flags), target.GetFlags()); EXPECT_EQ(target.GetID(), light.TargetID());
    EXPECT_EQ(nullptr, target.GetParent()); EXPECT_EQ(nullptr, light.GetParent()); light.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, ReplaceReleasesOldMarkerAndMarksNewTarget) {
    TargetLightLifecycleProbe light(Context); RCK3dEntity first(Context), second(Context);
    first.SetFlags(CK_3DENTITY_IGNOREANIMATION); second.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_RESERVED0);
    light.SetTarget(API(first)); const CKDWORD oldFlags = first.GetFlags(), newFlags = second.GetFlags();
    light.SetTarget(API(second)); EXPECT_EQ(Released(oldFlags), first.GetFlags());
    EXPECT_EQ(Bound(newFlags), second.GetFlags()); EXPECT_EQ(API(second), light.GetTarget()); light.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, RebindingSameTargetReappliesMarkersAndPreservesPositions) {
    TargetLightLifecycleProbe light(Context); RCK3dEntity target(Context);
    light.SetPosition3f(1, 2, 3); target.SetPosition3f(7, 8, 9); light.SetTarget(API(target));
    target.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_IGNOREANIMATION | CK_3DENTITY_TARGETCAMERA);
    const CKDWORD flags = target.GetFlags(); light.SetTarget(API(target)); EXPECT_EQ(Bound(flags), target.GetFlags());
    VxVector position; light.GetPosition(&position); EXPECT_EQ(1.0f, position.x); EXPECT_EQ(2.0f, position.y); EXPECT_EQ(3.0f, position.z);
    target.GetPosition(&position); EXPECT_EQ(7.0f, position.x); EXPECT_EQ(8.0f, position.y); EXPECT_EQ(9.0f, position.z);
    light.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, SelfTargetIsRejectedWithoutReleasingExistingTarget) {
    TargetLightLifecycleProbe light(Context); RCK3dEntity target(Context); light.SetTarget(API(target));
    const CKDWORD flags = target.GetFlags(); light.SetTarget(API(light));
    EXPECT_EQ(API(target), light.GetTarget()); EXPECT_EQ(target.GetID(), light.TargetID());
    EXPECT_EQ(flags, target.GetFlags()); light.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, SelfTargetIsRejectedWhileReferenceIsEmpty) {
    TargetLightLifecycleProbe light(Context); const CKDWORD flags = light.GetFlags(); light.SetTarget(API(light));
    EXPECT_EQ(nullptr, light.GetTarget()); EXPECT_EQ(0u, light.TargetID()); EXPECT_EQ(flags, light.GetFlags());
}
TEST_F(TargetLightLifecycleTest, ClearResolvedAndDeferredReferencesAlwaysZeroesStoredIdentifier) {
    TargetLightLifecycleProbe light(Context); RCK3dEntity target(Context); light.SetTarget(API(target));
    const CKDWORD flags = target.GetFlags(); light.SetTarget(nullptr); EXPECT_EQ(Released(flags), target.GetFlags());
    EXPECT_EQ(0u, light.TargetID()); light.SetRawTarget(0x7ffabcde); light.SetTarget(nullptr);
    EXPECT_EQ(nullptr, light.GetTarget()); EXPECT_EQ(0u, light.TargetID()); light.SetTarget(nullptr); EXPECT_EQ(0u, light.TargetID());
}
TEST_F(TargetLightLifecycleTest, SharedTargetMarkerFollowsLastMutationWhileOtherReferenceSurvives) {
    TargetLightLifecycleProbe first(Context), second(Context); RCK3dEntity target(Context);
    first.SetTarget(API(target)); second.SetTarget(API(target)); const CKDWORD flags = target.GetFlags();
    first.SetTarget(nullptr); EXPECT_EQ(Released(flags), target.GetFlags()); EXPECT_EQ(API(target), second.GetTarget());
    second.SetTarget(API(target)); EXPECT_EQ(Bound(Released(flags)), target.GetFlags()); second.SetTarget(nullptr);
}
TEST_F(TargetLightLifecycleTest, DestructionOfDeferredReferenceDoesNotDeleteAnUnrelatedContextObject) {
    RCK3dEntity target(Context); TargetLightLifecycleProbe *light = new TargetLightLifecycleProbe(Context);
    const CK_ID id = light->GetID(); const CKDWORD flags = target.GetFlags(); light->SetRawTarget(0x7ffabcde);
    delete static_cast<CKObject *>(light); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&target, Context->GetObject(target.GetID())); EXPECT_EQ(flags, target.GetFlags());
}
