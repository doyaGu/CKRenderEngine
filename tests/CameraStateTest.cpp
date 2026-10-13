#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCamera.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CameraStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static CKDWORD Ready(RCKCamera &camera) {
        camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
        return camera.GetObjectFlags();
    }
    static void Invalidated(RCKCamera &camera, CKDWORD previous) {
        EXPECT_EQ(previous & ~CK_OBJECT_UPTODATE, camera.GetObjectFlags());
    }
};
}

TEST_F(CameraStateTest, FrontGetterPreservesReadyAndOtherParameters) {
    RCKCamera camera(Context); camera.SetFrontPlane(2.75f); CKDWORD flags = Ready(camera);
    EXPECT_FLOAT_EQ(2.75f, camera.GetFrontPlane()); EXPECT_EQ(flags, camera.GetObjectFlags());
    EXPECT_FLOAT_EQ(4000.0f, camera.GetBackPlane()); EXPECT_FLOAT_EQ(.5f, camera.GetFov());
}
TEST_F(CameraStateTest, FrontSetterInvalidatesEvenWhenValueUnchanged) {
    RCKCamera camera(Context); CKDWORD flags = Ready(camera); camera.SetFrontPlane(1.0f);
    Invalidated(camera, flags); flags = Ready(camera); camera.SetFrontPlane(3.25f);
    EXPECT_FLOAT_EQ(3.25f, camera.GetFrontPlane()); Invalidated(camera, flags);
}
TEST_F(CameraStateTest, BackGetterPreservesReadyAndFrontPlane) {
    RCKCamera camera(Context); camera.SetBackPlane(123.5f); CKDWORD flags = Ready(camera);
    EXPECT_FLOAT_EQ(123.5f, camera.GetBackPlane()); EXPECT_EQ(flags, camera.GetObjectFlags());
    EXPECT_FLOAT_EQ(1.0f, camera.GetFrontPlane());
}
TEST_F(CameraStateTest, BackSetterInvalidatesEvenWhenValueUnchanged) {
    RCKCamera camera(Context); CKDWORD flags = Ready(camera); camera.SetBackPlane(4000.0f);
    Invalidated(camera, flags); flags = Ready(camera); camera.SetBackPlane(75.0f);
    EXPECT_FLOAT_EQ(75.0f, camera.GetBackPlane()); Invalidated(camera, flags);
}
TEST_F(CameraStateTest, FovGetterPreservesReadyAndProjectionType) {
    RCKCamera camera(Context); camera.SetFov(1.25f); CKDWORD flags = Ready(camera);
    EXPECT_FLOAT_EQ(1.25f, camera.GetFov()); EXPECT_EQ(flags, camera.GetObjectFlags());
    EXPECT_EQ(CK_PERSPECTIVEPROJECTION, camera.GetProjectionType());
}
TEST_F(CameraStateTest, FovSetterInvalidatesEvenWhenValueUnchanged) {
    RCKCamera camera(Context); CKDWORD flags = Ready(camera); camera.SetFov(.5f);
    Invalidated(camera, flags); flags = Ready(camera); camera.SetFov(.75f);
    EXPECT_FLOAT_EQ(.75f, camera.GetFov()); Invalidated(camera, flags);
}
TEST_F(CameraStateTest, ProjectionGetterPreservesReadyAndZoom) {
    RCKCamera camera(Context); camera.SetProjectionType(CK_ORTHOGRAPHICPROJECTION);
    CKDWORD flags = Ready(camera); EXPECT_EQ(CK_ORTHOGRAPHICPROJECTION, camera.GetProjectionType());
    EXPECT_EQ(flags, camera.GetObjectFlags()); EXPECT_FLOAT_EQ(1.0f, camera.GetOrthographicZoom());
}
TEST_F(CameraStateTest, ProjectionSetterInvalidatesEvenWhenValueUnchanged) {
    RCKCamera camera(Context); CKDWORD flags = Ready(camera); camera.SetProjectionType(CK_PERSPECTIVEPROJECTION);
    Invalidated(camera, flags); flags = Ready(camera); camera.SetProjectionType(CK_ORTHOGRAPHICPROJECTION);
    EXPECT_EQ(CK_ORTHOGRAPHICPROJECTION, camera.GetProjectionType()); Invalidated(camera, flags);
}
TEST_F(CameraStateTest, ZoomGetterPreservesReadyAndFov) {
    RCKCamera camera(Context); camera.SetOrthographicZoom(2.5f); CKDWORD flags = Ready(camera);
    EXPECT_FLOAT_EQ(2.5f, camera.GetOrthographicZoom()); EXPECT_EQ(flags, camera.GetObjectFlags());
    EXPECT_FLOAT_EQ(.5f, camera.GetFov());
}
TEST_F(CameraStateTest, ZoomSetterInvalidatesEvenWhenValueUnchanged) {
    RCKCamera camera(Context); CKDWORD flags = Ready(camera); camera.SetOrthographicZoom(1.0f);
    Invalidated(camera, flags); flags = Ready(camera); camera.SetOrthographicZoom(4.0f);
    EXPECT_FLOAT_EQ(4.0f, camera.GetOrthographicZoom()); Invalidated(camera, flags);
}
TEST_F(CameraStateTest, AspectGetterReturnsStoredPairWithoutChangingReady) {
    RCKCamera camera(Context); camera.SetAspectRatio(1920, 1080); CKDWORD flags = Ready(camera);
    int width = 0, height = 0; camera.GetAspectRatio(width, height);
    EXPECT_EQ(1920, width); EXPECT_EQ(1080, height); EXPECT_EQ(flags, camera.GetObjectFlags());
}
TEST_F(CameraStateTest, AspectSetterInvalidatesEvenWhenPairUnchanged) {
    RCKCamera camera(Context); CKDWORD flags = Ready(camera); camera.SetAspectRatio(4, 3);
    Invalidated(camera, flags); flags = Ready(camera); camera.SetAspectRatio(16, 9);
    int width = 0, height = 0; camera.GetAspectRatio(width, height);
    EXPECT_EQ(16, width); EXPECT_EQ(9, height); Invalidated(camera, flags);
}
TEST_F(CameraStateTest, BaseCameraGetTargetRemainsNull) {
    RCKCamera camera(Context); CKDWORD flags = Ready(camera);
    EXPECT_EQ(nullptr, camera.GetTarget()); EXPECT_EQ(flags, camera.GetObjectFlags());
}
TEST_F(CameraStateTest, BaseCameraSetTargetDoesNotAdoptObjectOrChangePose) {
    RCKCamera camera(Context); RCK3dEntity target(Context);
    VxVector position(3, 5, 7); camera.SetPosition(&position); CKDWORD flags = Ready(camera);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(&target)); camera.SetTarget(nullptr);
    EXPECT_EQ(nullptr, camera.GetTarget()); EXPECT_EQ(flags, camera.GetObjectFlags());
    EXPECT_EQ(nullptr, target.GetParent()); VxVector observed; camera.GetPosition(&observed);
    EXPECT_FLOAT_EQ(3, observed.x); EXPECT_FLOAT_EQ(5, observed.y); EXPECT_FLOAT_EQ(7, observed.z);
}
TEST_F(CameraStateTest, ParameterStorageDoesNotClampRequestedValues) {
    RCKCamera camera(Context);
    camera.SetFrontPlane(-2); camera.SetBackPlane(0); camera.SetFov(-.25f);
    camera.SetOrthographicZoom(-3); camera.SetProjectionType(0x42); camera.SetAspectRatio(-16, 0);
    EXPECT_FLOAT_EQ(-2, camera.GetFrontPlane()); EXPECT_FLOAT_EQ(0, camera.GetBackPlane());
    EXPECT_FLOAT_EQ(-.25f, camera.GetFov()); EXPECT_FLOAT_EQ(-3, camera.GetOrthographicZoom());
    EXPECT_EQ(0x42, camera.GetProjectionType()); int width, height; camera.GetAspectRatio(width, height);
    EXPECT_EQ(-16, width); EXPECT_EQ(0, height);
}
TEST_F(CameraStateTest, AspectGetterWithAliasedOutputsFinishesWithHeight) {
    RCKCamera camera(Context); camera.SetAspectRatio(16, 9); int dimension = 0;
    camera.GetAspectRatio(dimension, dimension); EXPECT_EQ(9, dimension);
}
