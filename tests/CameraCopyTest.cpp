#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKCamera.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CameraCopyTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Options(CKDependencies &options) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0; options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    }
    static void Configure(RCKCamera &camera) {
        camera.SetFov(1.125f); camera.SetFrontPlane(0.25f); camera.SetBackPlane(900.0f);
        camera.SetProjectionType(CK_ORTHOGRAPHICPROJECTION); camera.SetOrthographicZoom(7.5f); camera.SetAspectRatio(1920, 1080);
    }
    static void ExpectCamera(RCKCamera &camera) {
        EXPECT_EQ(1.125f, camera.GetFov()); EXPECT_EQ(0.25f, camera.GetFrontPlane()); EXPECT_EQ(900.0f, camera.GetBackPlane());
        EXPECT_EQ(CK_ORTHOGRAPHICPROJECTION, camera.GetProjectionType()); EXPECT_EQ(7.5f, camera.GetOrthographicZoom());
        int width = 0, height = 0; camera.GetAspectRatio(width, height); EXPECT_EQ(1920, width); EXPECT_EQ(1080, height);
    }
    static CKSkin *CreateSkin(RCKCamera &camera, RCK3dEntity &bone) {
        CKSkin *skin = camera.CreateSkin(); if (!skin) return nullptr;
        skin->SetBoneCount(1); skin->SetVertexCount(1); skin->GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone));
        CKSkinVertexData *vertex = skin->GetVertexData(0); vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, 1.0f);
        VxVector position(2.0f, 3.0f, 5.0f); vertex->SetInitialPos(position); return skin;
    }
};
}
TEST_F(CameraCopyTest, CopyTransfersEveryCameraFieldAndInvalidatesProjectionCache) {
    RCKCamera source(Context), destination(Context); Configure(source); destination.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); ExpectCamera(destination);
    EXPECT_EQ(0u, destination.GetObjectFlags() & CK_OBJECT_UPTODATE); dependencies.StopDependencies();
}
TEST_F(CameraCopyTest, CopyPreservesInheritedParentWorldPoseAndZOrder) {
    new RCKRenderManager(Context); RCK3dEntity parent(Context); RCKCamera source(Context), destination(Context);
    Configure(source); VxVector position(3.0f, 5.0f, 7.0f); source.SetPosition(&position, nullptr, FALSE);
    source.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE); source.SetZOrder(-29);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); ExpectCamera(destination);
    EXPECT_EQ(source.GetParent(), destination.GetParent()); EXPECT_EQ(2, parent.GetChildrenCount()); EXPECT_EQ(-29, destination.GetZOrder());
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) EXPECT_FLOAT_EQ(source.GetWorldMatrix()[row][col], destination.GetWorldMatrix()[row][col]);
    source.SetParent(nullptr, TRUE); destination.SetParent(nullptr, TRUE); dependencies.StopDependencies();
}
TEST_F(CameraCopyTest, CopiedProjectionIsIndependentOfSubsequentSourceState) {
    RCKCamera source(Context), destination(Context); Configure(source);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); VxMatrix expected, actual; source.ComputeProjectionMatrix(expected);
    source.SetFov(0.75f); source.SetProjectionType(CK_PERSPECTIVEPROJECTION); source.SetOrthographicZoom(2.0f); source.SetAspectRatio(4, 3);
    destination.ComputeProjectionMatrix(actual); ExpectCamera(destination);
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) EXPECT_FLOAT_EQ(expected[row][col], actual[row][col]);
    dependencies.StopDependencies();
}
TEST_F(CameraCopyTest, SourceDestructionLeavesIndependentCopiedSkinAndCameraFields) {
    RCK3dEntity bone(Context); RCKCamera destination(Context);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    { RCKCamera source(Context); Configure(source); CKSkin *skin = CreateSkin(source, bone); ASSERT_NE(nullptr, skin);
      ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_NE(skin, destination.GetSkin()); }
    ExpectCamera(destination); ASSERT_NE(nullptr, destination.GetSkin()); EXPECT_EQ(1, destination.GetSkin()->GetBoneCount()); EXPECT_EQ(1, destination.GetSkin()->GetVertexCount());
    VxVector actual; ASSERT_TRUE(destination.GetSkin()->CalcPoints(1, reinterpret_cast<CKBYTE *>(&actual), sizeof(actual)));
    EXPECT_FLOAT_EQ(2.0f, actual.x); EXPECT_FLOAT_EQ(3.0f, actual.y); EXPECT_FLOAT_EQ(5.0f, actual.z); dependencies.StopDependencies();
}
TEST_F(CameraCopyTest, SelfCopyPreservesSkinPointerWeightsAndSingleParentEntry) {
    new RCKRenderManager(Context); RCK3dEntity parent(Context), bone(Context); RCKCamera camera(Context); Configure(camera);
    camera.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE); CKSkin *skin = CreateSkin(camera, bone); ASSERT_NE(nullptr, skin);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); const CKDWORD flags = camera.GetObjectFlags();
    ASSERT_EQ(CK_OK, camera.Copy(camera, dependencies)); ExpectCamera(camera); EXPECT_EQ(skin, camera.GetSkin()); EXPECT_EQ(1, parent.GetChildrenCount());
    EXPECT_EQ(flags, camera.GetObjectFlags());
    ASSERT_NE(nullptr, camera.GetSkin()); ASSERT_EQ(1, camera.GetSkin()->GetBoneCount()); ASSERT_EQ(1, camera.GetSkin()->GetVertexCount());
    CKSkinVertexData *vertex = camera.GetSkin()->GetVertexData(0); ASSERT_NE(nullptr, vertex); EXPECT_EQ(1.0f, vertex->GetWeight(0));
    VxVector actual; ASSERT_TRUE(camera.GetSkin()->CalcPoints(1, reinterpret_cast<CKBYTE *>(&actual), sizeof(actual)));
    EXPECT_FLOAT_EQ(2.0f, actual.x); EXPECT_FLOAT_EQ(3.0f, actual.y); EXPECT_FLOAT_EQ(5.0f, actual.z);
    camera.SetParent(nullptr, TRUE); dependencies.StopDependencies();
}
TEST_F(CameraCopyTest, CopyOfDefaultCameraReplacesPreviousCameraConfiguration) {
    RCKCamera source(Context), destination(Context); Configure(destination);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(0.5f, destination.GetFov()); EXPECT_EQ(1.0f, destination.GetFrontPlane());
    EXPECT_EQ(4000.0f, destination.GetBackPlane()); EXPECT_EQ(CK_PERSPECTIVEPROJECTION, destination.GetProjectionType()); EXPECT_EQ(1.0f, destination.GetOrthographicZoom());
    int width = 0, height = 0; destination.GetAspectRatio(width, height); EXPECT_EQ(4, width); EXPECT_EQ(3, height); dependencies.StopDependencies();
}
