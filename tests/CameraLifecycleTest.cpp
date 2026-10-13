#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKCamera.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CameraLifecycleProbe : public RCKCamera {
public:
    static void RecordLayout(RCKCamera &camera) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&camera);
        std::size_t previous = sizeof(RCK3dEntity);
#define FIELD(field) { auto member = &CameraLifecycleProbe::field; const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(camera.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); EXPECT_LE(offset + sizeof(camera.*member), sizeof(RCKCamera)); \
        EXPECT_EQ(0u, address % alignof(decltype(camera.*member))); ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(camera.*member))); previous = offset + sizeof(camera.*member); }
        FIELD(m_Fov); FIELD(m_FrontPlane); FIELD(m_BackPlane); FIELD(m_ProjectionType);
        FIELD(m_OrthographicZoom); FIELD(m_Width); FIELD(m_Height);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKCamera)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKCamera)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class CameraLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
}
TEST_F(CameraLifecycleTest, RegisteredIdentityDefaultsAndActualPlatformLayout) {
    RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA)); ASSERT_NE(nullptr, camera);
    EXPECT_EQ(34, camera->GetClassID()); EXPECT_EQ(camera, Context->GetObject(camera->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(camera, CKCID_3DENTITY)); EXPECT_TRUE(CKIsChildClassOf(camera, CKCID_RENDEROBJECT));
    EXPECT_EQ(0.5f, camera->GetFov()); EXPECT_EQ(1.0f, camera->GetFrontPlane()); EXPECT_EQ(4000.0f, camera->GetBackPlane());
    EXPECT_EQ(CK_PERSPECTIVEPROJECTION, camera->GetProjectionType()); EXPECT_EQ(1.0f, camera->GetOrthographicZoom());
    int width = 0, height = 0; camera->GetAspectRatio(width, height); EXPECT_EQ(4, width); EXPECT_EQ(3, height);
    EXPECT_EQ(0u, camera->GetObjectFlags() & CK_OBJECT_UPTODATE); EXPECT_EQ(nullptr, camera->GetSkin());
    CameraLifecycleProbe::RecordLayout(*camera);
}
TEST_F(CameraLifecycleTest, ClassNameMatchesNativeDisplayName) {
    EXPECT_STREQ("Camera", RCKCamera::GetClassName());
}
TEST_F(CameraLifecycleTest, RegistrationPublishesNativeParameterAndNoOwnDependencies) {
    CKClassDesc *description = CKGetClassDesc(CKCID_CAMERA); ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID(); RCKCamera::Register();
    EXPECT_EQ(CKPGUID_CAMERA, description->Parameter); EXPECT_EQ(0x3cf24d6fu, description->Parameter.d1); EXPECT_EQ(0x216204f9u, description->Parameter.d2);
    const int modes[] = {-1, 0, 1, 2, 3, 4, 5};
    for (int mode : modes) { EXPECT_EQ(0, RCKCamera::GetDependenciesCount(mode)); EXPECT_EQ(nullptr, RCKCamera::GetDependencies(0, mode)); }
}
TEST_F(CameraLifecycleTest, DirectFactoryVirtualDestructorReleasesOwnedSkinAndRegistration) {
    CKCamera *api = RCKCamera::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKCamera *camera = reinterpret_cast<RCKCamera *>(api); EXPECT_EQ(CKCID_CAMERA, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    const CK_ID id = camera->GetID(); ASSERT_NE(nullptr, camera->CreateSkin()); camera->GetSkin()->SetBoneCount(2);
    delete static_cast<CKObject *>(camera); EXPECT_EQ(nullptr, Context->GetObject(id));
}
TEST_F(CameraLifecycleTest, DestructionUnlinksCameraFromSurvivingParent) {
    new RCKRenderManager(Context); RCK3dEntity parent(Context);
    RCKCamera *camera = new RCKCamera(Context); camera->SetParent(reinterpret_cast<CK3dEntity *>(&parent), FALSE);
    ASSERT_EQ(1, parent.GetChildrenCount()); const CK_ID id = camera->GetID();
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(camera, 0, &dependencies));
    EXPECT_EQ(0, parent.GetChildrenCount()); EXPECT_EQ(nullptr, Context->GetObject(id));
}
TEST_F(CameraLifecycleTest, MemoryOccupationAddsActualPlatformTailToInheritedStorage) {
    RCKCamera camera(Context); const int tail = static_cast<int>(sizeof(RCKCamera) - sizeof(RCK3dEntity));
    EXPECT_GE(tail, 28); EXPECT_EQ(camera.RCK3dEntity::GetMemoryOccupation() + tail, camera.GetMemoryOccupation());
    char name[] = "Camera memory"; camera.SetName(name);
    EXPECT_EQ(camera.RCK3dEntity::GetMemoryOccupation() + tail, camera.GetMemoryOccupation());
    CKSkin *skin = camera.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(2); skin->SetVertexCount(3);
    EXPECT_EQ(camera.RCK3dEntity::GetMemoryOccupation() + tail, camera.GetMemoryOccupation());
}
