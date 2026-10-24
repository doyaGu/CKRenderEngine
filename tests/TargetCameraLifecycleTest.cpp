#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKTargetCamera.h"
#include "RCKRenderObject.h"
#include "RCKMesh.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TargetCameraLifecycleProbe : public RCKTargetCamera {
public:
    explicit TargetCameraLifecycleProbe(CKContext *context) : RCKTargetCamera(context) {}
    CK_ID TargetID() const { return m_Target; }
    static CK_ID StoredTargetID(RCKTargetCamera &camera) {
        auto member = &TargetCameraLifecycleProbe::m_Target;
        return camera.*member;
    }
    void SetRawTarget(CK_ID id) { m_Target = id; }
    void AttachMesh(RCKMesh &mesh) { m_Meshes.PushBack(&mesh); }
    static void RecordLayout(RCKTargetCamera &camera) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&camera);
        auto member = &TargetCameraLifecycleProbe::m_Target;
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(camera.*member));
        const std::size_t offset = address - begin;
        // Bound occupied base storage, excluding reusable tail padding.
        auto lastBaseMember = &TargetCameraLifecycleProbe::m_Height;
        const std::size_t baseEnd = reinterpret_cast<std::uintptr_t>(&(camera.*lastBaseMember))
            + sizeof(camera.*lastBaseMember) - begin;
        EXPECT_GE(offset, baseEnd);
        EXPECT_LE(offset + sizeof(camera.*member), sizeof(RCKTargetCamera));
        EXPECT_EQ(0u, address % alignof(CK_ID));
        ::testing::Test::RecordProperty("m_Target_offset", static_cast<int>(offset));
        ::testing::Test::RecordProperty("m_Target_size", static_cast<int>(sizeof(camera.*member)));
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKTargetCamera)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKTargetCamera)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCKCamera)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class TargetCameraLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKTargetCamera, CKCID_CAMERA);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    CKERROR Destroy(CKObject *object) {
        CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        return Context->DestroyObject(object, 0, &dependencies);
    }
};
}
TEST_F(TargetCameraLifecycleTest, RegisteredIdentityDefaultsAndActualPlatformLayout) {
    RCKTargetCamera *camera = static_cast<RCKTargetCamera *>(Context->CreateObject(CKCID_TARGETCAMERA)); ASSERT_NE(nullptr, camera);
    EXPECT_EQ(35, camera->GetClassID()); EXPECT_EQ(camera, Context->GetObject(camera->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(camera, CKCID_CAMERA)); EXPECT_TRUE(CKIsChildClassOf(camera, CKCID_3DENTITY));
    EXPECT_EQ(nullptr, camera->GetTarget()); EXPECT_EQ(0.5f, camera->GetFov());
    EXPECT_EQ(1.0f, camera->GetFrontPlane()); EXPECT_EQ(4000.0f, camera->GetBackPlane());
    EXPECT_EQ(CK_PERSPECTIVEPROJECTION, camera->GetProjectionType()); EXPECT_EQ(nullptr, camera->GetSkin());
    TargetCameraLifecycleProbe::RecordLayout(*camera);
}
TEST_F(TargetCameraLifecycleTest, ClassNameMatchesNativeDisplayName) {
    EXPECT_STREQ("Target Camera", RCKTargetCamera::GetClassName());
}
TEST_F(TargetCameraLifecycleTest, RegistrationPublishesNativeParameterCopyMaskAndEntityNotification) {
    CKClassDesc *description = CKGetClassDesc(CKCID_TARGETCAMERA); ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID(); description->DefaultCopyDependencies = 0;
    RCKTargetCamera::Register();
    EXPECT_EQ(CKPGUID_TARGETCAMERA, description->Parameter);
    EXPECT_EQ(299398501u, description->Parameter.d1); EXPECT_EQ(1561033097u, description->Parameter.d2);
    EXPECT_EQ(1u, description->DefaultCopyDependencies);
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_3DENTITY));
}
TEST_F(TargetCameraLifecycleTest, DirectFactoryVirtualDestructorReleasesTargetFlagAndOwnedSkin) {
    RCK3dEntity target(Context); target.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_IGNOREANIMATION);
    CKTargetCamera *api = RCKTargetCamera::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKTargetCamera *camera = reinterpret_cast<RCKTargetCamera *>(api);
    EXPECT_EQ(CKCID_TARGETCAMERA, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    const CK_ID id = camera->GetID(); ASSERT_NE(nullptr, camera->CreateSkin()); camera->GetSkin()->SetBoneCount(2);
    camera->SetTarget(reinterpret_cast<CK3dEntity *>(&target)); ASSERT_NE(0u, target.GetFlags() & CK_3DENTITY_TARGETCAMERA);
    delete static_cast<CKObject *>(camera);
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(0u, target.GetFlags() & CK_3DENTITY_TARGETCAMERA);
    EXPECT_NE(0u, target.GetFlags() & CK_3DENTITY_FRAME); EXPECT_NE(0u, target.GetFlags() & CK_3DENTITY_IGNOREANIMATION);
}
TEST_F(TargetCameraLifecycleTest, ContextDeletionUnlinksParentAndPreservesTargetWithoutDependencies) {
    new RCKRenderManager(Context); RCK3dEntity parent(Context), target(Context);
    RCKTargetCamera *camera = new RCKTargetCamera(Context);
    camera->SetParent(reinterpret_cast<CK3dEntity *>(&parent), FALSE);
    camera->SetTarget(reinterpret_cast<CK3dEntity *>(&target)); ASSERT_EQ(1, parent.GetChildrenCount());
    const CK_ID id = camera->GetID(); ASSERT_EQ(CK_OK, Destroy(camera));
    EXPECT_EQ(0, parent.GetChildrenCount()); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&target, Context->GetObject(target.GetID())); EXPECT_EQ(0u, target.GetFlags() & CK_3DENTITY_TARGETCAMERA);
}
TEST_F(TargetCameraLifecycleTest, TargetDeletionNotificationClearsStoredReference) {
    RCKTargetCamera *camera = static_cast<RCKTargetCamera *>(Context->CreateObject(CKCID_TARGETCAMERA)); ASSERT_NE(nullptr, camera);
    RCK3dEntity *target = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr, target);
    camera->SetTarget(reinterpret_cast<CK3dEntity *>(target)); ASSERT_NE(0u, TargetCameraLifecycleProbe::StoredTargetID(*camera));
    const CK_ID id = target->GetID(); ASSERT_EQ(CK_OK, Destroy(target));
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(0u, TargetCameraLifecycleProbe::StoredTargetID(*camera)); EXPECT_EQ(nullptr, camera->GetTarget());
}
TEST_F(TargetCameraLifecycleTest, ExplicitPostDeletionRemovesAnUnregisteredTargetID) {
    TargetCameraLifecycleProbe camera(Context); RCK3dEntity *target = new RCK3dEntity(Context);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(target)); const CK_ID id = camera.TargetID();
    delete target; EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(id, camera.TargetID());
    camera.CheckPostDeletion(); EXPECT_EQ(0u, camera.TargetID()); EXPECT_EQ(nullptr, camera.GetTarget());
}
TEST_F(TargetCameraLifecycleTest, PostDeletionPreservesLiveReferenceAndFlags) {
    RCK3dEntity target(Context); TargetCameraLifecycleProbe camera(Context);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(&target)); const CKDWORD flags = target.GetFlags();
    camera.CheckPostDeletion(); EXPECT_EQ(target.GetID(), camera.TargetID());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&target), camera.GetTarget()); EXPECT_EQ(flags, target.GetFlags());
}
TEST_F(TargetCameraLifecycleTest, UnrelatedEntityDeletionPreservesTargetReference) {
    RCKTargetCamera *camera = static_cast<RCKTargetCamera *>(Context->CreateObject(CKCID_TARGETCAMERA)); ASSERT_NE(nullptr, camera);
    RCK3dEntity *target = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr, target);
    camera->SetTarget(reinterpret_cast<CK3dEntity *>(target));
    RCK3dEntity *unrelated = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr, unrelated);
    ASSERT_EQ(CK_OK, Destroy(unrelated)); EXPECT_EQ(target->GetID(), TargetCameraLifecycleProbe::StoredTargetID(*camera));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(target), camera->GetTarget());
}
TEST_F(TargetCameraLifecycleTest, TargetUsageMatchesStoredIDIndependentOfCIDHint) {
    TargetCameraLifecycleProbe camera(Context); RCK3dEntity target(Context), unrelated(Context);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(&target));
    EXPECT_TRUE(camera.IsObjectUsed(&target, CKCID_3DENTITY)); EXPECT_TRUE(camera.IsObjectUsed(&target, CKCID_MESH));
    EXPECT_FALSE(camera.IsObjectUsed(&unrelated, CKCID_3DENTITY));
}
TEST_F(TargetCameraLifecycleTest, ObjectUsageDelegatesToInheritedMeshReferences) {
    RCKMesh mesh(Context), unrelated(Context); TargetCameraLifecycleProbe camera(Context);
    camera.AttachMesh(mesh); EXPECT_TRUE(camera.IsObjectUsed(&mesh, CKCID_MESH));
    EXPECT_FALSE(camera.IsObjectUsed(&unrelated, CKCID_MESH));
}
TEST_F(TargetCameraLifecycleTest, MemoryOccupationAddsActualPlatformTailToInheritedStorage) {
    RCKTargetCamera camera(Context); RCK3dEntity target(Context);
    const int tail = static_cast<int>(sizeof(RCKTargetCamera) - sizeof(RCKCamera)); EXPECT_GE(tail, 0);
    EXPECT_EQ(camera.RCKCamera::GetMemoryOccupation() + tail, camera.GetMemoryOccupation());
    char name[] = "Target camera memory"; camera.SetName(name); camera.SetTarget(reinterpret_cast<CK3dEntity *>(&target));
    ASSERT_NE(nullptr, camera.CreateSkin()); camera.GetSkin()->SetBoneCount(2); camera.GetSkin()->SetVertexCount(3);
    EXPECT_EQ(camera.RCKCamera::GetMemoryOccupation() + tail, camera.GetMemoryOccupation());
}
