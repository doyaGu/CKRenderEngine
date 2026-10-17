#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class RenderContextPickingCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Context->m_BehaviorContext.CurrentRenderContext = Device;
        // Real picking consumes a frame's published extents, not allocation slots.
        Device->DetachAll();
        Device->m_Settings.m_Rect = {20, 30, 100, 100};
        Device->m_ViewportData.ViewX = 0; Device->m_ViewportData.ViewY = 0;
        Device->m_ViewportData.ViewWidth = 100; Device->m_ViewportData.ViewHeight = 100;
        Device->m_ViewportData.ViewZMin = 0; Device->m_ViewportData.ViewZMax = 1;
        Device->m_Fov = PI / 2; Device->m_NearPlane = 1; Device->m_FarPlane = 20;
        Device->m_Perspective = TRUE;
        VxMatrix projection; projection.Perspective(PI / 2, 1, 1, 20);
        Device->SetProjectionTransformationMatrix(projection);
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        Device->SetWorldTransformationMatrix(VxMatrix::Identity());
        ASSERT_NE(nullptr, Device->GetViewpoint());
    }
    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        delete Device;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    static CK2dEntity *API(RCK2dEntity *entity) { return reinterpret_cast<CK2dEntity *>(entity); }
    RCK3dEntity *Triangle(float x, float z) {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_NE(nullptr, entity); EXPECT_NE(nullptr, mesh);
        if (!entity || !mesh) return nullptr;
        EXPECT_TRUE(mesh->SetVertexCount(3)); EXPECT_TRUE(mesh->SetFaceCount(1));
        VxVector positions[3] = {VxVector(-1, -1, 0), VxVector(0, 1, 0), VxVector(1, -1, 0)};
        VxVector normal(0, 0, -1);
        for (int i = 0; i < 3; ++i) {
            mesh->SetVertexPosition(i, &positions[i]); mesh->SetVertexNormal(i, &normal);
        }
        mesh->SetFaceVertexIndex(0, 0, 1, 2); mesh->BuildFaceNormals(); mesh->VertexMove();
        entity->SetCurrentMesh(mesh); entity->SetPosition3f(x, 0, z); entity->UpdateBox(TRUE);
        Device->AddObject(entity);
        Device->AddExtents2D(VxRect(0, 0, 100, 100), entity);
        return entity;
    }
    RCK2dEntity *Overlay(const VxRect &rect, CKBOOL background) {
        RCK2dEntity *entity = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
        EXPECT_NE(nullptr, entity);
        if (!entity) return nullptr;
        entity->SetBackground(background); entity->SetRect(rect);
        Device->AddObject(entity);
        // This is the same saved screen-space extent consumed after a render.
        entity->SetExtents(VxRect(0, 0, 1, 1), rect);
        return entity;
    }
    RCKCamera *Camera(float x) {
        RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
        EXPECT_NE(nullptr, camera);
        if (!camera) return nullptr;
        camera->SetPosition3f(x, 0, 0); camera->SetFov(PI / 2); camera->SetBackPlane(20);
        return camera;
    }
    void CameraExtent(RCKCamera *camera, CKDWORD flags) {
        CKRenderExtents extent;
        extent.m_Rect = VxRect(0, 0, 100, 100); extent.m_Flags = flags; extent.m_Camera = camera->GetID();
        Device->m_Extents.PushBack(extent);
    }
};
}

TEST_F(RenderContextPickingCompletionTest, PerspectivePickingChoosesNearestActualTriangleAndHonorsFilterAndPickability) {
    RCK3dEntity *distant = Triangle(0, 8), *closest = Triangle(0, 4);
    ASSERT_NE(nullptr, distant); ASSERT_NE(nullptr, closest);
    VxIntersectionDesc hit = {};
    EXPECT_EQ(API(closest), Device->Pick3D(Vx2DVector(50, 50), &hit, nullptr, FALSE));
    EXPECT_NEAR(4, hit.Distance, 1e-4f); EXPECT_EQ(0, hit.FaceIndex);
    EXPECT_EQ(API(distant), Device->Pick3D(Vx2DVector(50, 50), &hit, API(distant), FALSE));
    closest->SetPickable(FALSE);
    EXPECT_EQ(API(distant), Device->Pick3D(Vx2DVector(50, 50), &hit, nullptr, FALSE));
    EXPECT_EQ(API(closest), Device->Pick3D(Vx2DVector(50, 50), &hit, nullptr, TRUE));
    EXPECT_EQ(nullptr, Device->Pick3D(Vx2DVector(-1, 50), &hit, nullptr, TRUE));
    CKPICKRESULT result = {};
    EXPECT_EQ(closest, Device->Pick(70, 80, &result, TRUE));
    EXPECT_NEAR(4, result.Distance, 1e-4f); EXPECT_EQ(0, result.Sprite);
    CKPOINT point = {70, 80};
    EXPECT_EQ(closest, Device->Pick(point, nullptr, TRUE));
}

TEST_F(RenderContextPickingCompletionTest, LatestCameraExtentSelectsItsWorldRayAndClearBackStopsEarlierCameraFallback) {
    RCK3dEntity *first = Triangle(0, 4), *second = Triangle(20, 6);
    RCKCamera *left = Camera(0), *right = Camera(20), *empty = Camera(100);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    ASSERT_NE(nullptr, left); ASSERT_NE(nullptr, right); ASSERT_NE(nullptr, empty);
    CameraExtent(left, 0); CameraExtent(right, 0);
    VxIntersectionDesc hit = {};
    EXPECT_EQ(API(second), Device->Pick3D(Vx2DVector(50, 50), &hit, nullptr, FALSE));
    EXPECT_NEAR(6, hit.Distance, .01f);
    CameraExtent(empty, CK_RENDER_CLEARBACK);
    EXPECT_EQ(nullptr, Device->Pick3D(Vx2DVector(50, 50), &hit, nullptr, FALSE));
    Device->m_Extents.Back().m_Flags = 0;
    EXPECT_EQ(API(second), Device->Pick3D(Vx2DVector(50, 50), &hit, nullptr, FALSE));
}

TEST_F(RenderContextPickingCompletionTest, EmptyViewportDoesNotGenerateAPickRayOrOverwriteTheHit) {
    ASSERT_NE(nullptr, Triangle(0, 4));
    VxIntersectionDesc hit = {};
    hit.Distance = 123;
    hit.FaceIndex = 71;
    Device->m_ViewportData.ViewWidth = 0;
    EXPECT_EQ(nullptr, Device->Pick3D(Vx2DVector(0, 50), &hit, nullptr, FALSE));
    Device->m_ViewportData.ViewWidth = 100;
    Device->m_ViewportData.ViewHeight = 0;
    EXPECT_EQ(nullptr, Device->Pick3D(Vx2DVector(50, 0), &hit, nullptr, FALSE));
    EXPECT_FLOAT_EQ(123, hit.Distance);
    EXPECT_EQ(71, hit.FaceIndex);
}

TEST_F(RenderContextPickingCompletionTest, ForegroundPickingUsesWindowOffsetAndPublicPickReportsSpriteSeparatelyFrom3dResult) {
    RCK2dEntity *back = Overlay(VxRect(40, 40, 60, 60), TRUE);
    RCK2dEntity *front = Overlay(VxRect(40, 40, 60, 60), FALSE);
    ASSERT_NE(nullptr, back); ASSERT_NE(nullptr, front);
    const Vx2DVector screen(70, 80);
    EXPECT_EQ(API(front), Device->Pick2D(screen));
    front->ModifyFlags(CK_2DENTITY_NOTPICKABLE, 0);
    EXPECT_EQ(API(back), Device->Pick2D(screen));
    EXPECT_EQ(API(front), Device->_Pick2D(screen, TRUE));
    CKPICKRESULT result = {};
    EXPECT_EQ(nullptr, Device->Pick(70, 80, &result, FALSE));
    EXPECT_EQ(back->GetID(), result.Sprite); EXPECT_EQ(-1, result.FaceIndex);
    RCK3dEntity *triangle = Triangle(0, 4); ASSERT_NE(nullptr, triangle);
    EXPECT_EQ(triangle, Device->Pick(70, 80, &result, FALSE));
    EXPECT_EQ(back->GetID(), result.Sprite); EXPECT_NEAR(4, result.Distance, 1e-4f);
}

TEST_F(RenderContextPickingCompletionTest, RectPickNormalizesWindowCoordinatesAndDistinguishesContainmentFromOverlap) {
    RCK3dEntity *inside = Triangle(0, 4), *overlap = Triangle(0, 8);
    ASSERT_NE(nullptr, inside); ASSERT_NE(nullptr, overlap);
    inside->m_RenderExtents = VxRect(20, 20, 30, 30);
    overlap->m_RenderExtents = VxRect(35, 35, 55, 55);
    RCK2dEntity *back = Overlay(VxRect(22, 22, 28, 28), TRUE);
    RCK2dEntity *front = Overlay(VxRect(24, 24, 26, 26), FALSE);
    RCK2dEntity *unpickable = Overlay(VxRect(21, 21, 29, 29), FALSE);
    ASSERT_NE(nullptr, back); ASSERT_NE(nullptr, front); ASSERT_NE(nullptr, unpickable);
    unpickable->ModifyFlags(CK_2DENTITY_NOTPICKABLE, 0);
    XObjectPointerArray contained;
    // Reversed global rectangle corresponds to local (10,10)-(40,40).
    ASSERT_EQ(CK_OK, Device->RectPick(VxRect(60, 70, 30, 40), contained, TRUE));
    ASSERT_EQ(3, contained.Size());
    EXPECT_EQ(front, contained[0]); EXPECT_EQ(back, contained[1]); EXPECT_EQ(inside, contained[2]);
    XObjectPointerArray overlapping;
    ASSERT_EQ(CK_OK, Device->RectPick(VxRect(30, 40, 60, 70), overlapping, FALSE));
    ASSERT_EQ(4, overlapping.Size());
    EXPECT_EQ(front, overlapping[0]); EXPECT_EQ(back, overlapping[1]);
    EXPECT_EQ(overlap, overlapping[2]); EXPECT_EQ(inside, overlapping[3]);
}
