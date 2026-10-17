#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKSceneGraph.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class GeometryContextObserver : public MaterialRenderObserver {
public:
    VxPlane Planes[6];
    CKBOOL AcceptSet = TRUE, AcceptGet = TRUE;
    int PlaneSets = 0, PlaneGets = 0, ViewportWrites = 0, ProjectionWrites = 0;
    CKBOOL SetUserClipPlane(CKDWORD index, const VxPlane &plane) override {
        ++PlaneSets;
        if (!AcceptSet) return FALSE;
        Planes[index] = plane;
        return TRUE;
    }
    CKBOOL GetUserClipPlane(CKDWORD index, VxPlane &plane) override {
        ++PlaneGets;
        if (!AcceptGet) return FALSE;
        plane = Planes[index];
        return TRUE;
    }
    CKBOOL SetViewport(const CKViewportData *viewport) override {
        ++ViewportWrites;
        return MaterialRenderObserver::SetViewport(viewport);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        if (type == VXMATRIX_PROJECTION) ++ProjectionWrites;
        return MaterialRenderObserver::SetTransformMatrix(type, matrix);
    }
};

class RenderContextGeometryCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    GeometryContextObserver Observer;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Context->m_BehaviorContext.CurrentRenderContext = Device;
        // Start from an empty frame, independently of constructor capacity.
        Device->DetachAll();
        ASSERT_NE(nullptr, Device->GetViewpoint());
        ASSERT_NE(nullptr, Device->Get2dRoot(FALSE));
        ASSERT_NE(nullptr, Device->Get2dRoot(TRUE));
    }
    void TearDown() override {
        if (Device) Device->m_RasterizerContext = nullptr;
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        delete Device;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }
    void Viewport(int x, int y, int width, int height, float minZ = 0, float maxZ = 1) {
        Device->m_ViewportData.ViewX = x; Device->m_ViewportData.ViewY = y;
        Device->m_ViewportData.ViewWidth = width; Device->m_ViewportData.ViewHeight = height;
        Device->m_ViewportData.ViewZMin = minZ; Device->m_ViewportData.ViewZMax = maxZ;
    }
    static VxBbox UnitBox() {
        VxBbox box;
        box.Min.Set(-1, -1, -1); box.Max.Set(1, 1, 1);
        return box;
    }
    static void Box(const VxBbox &box, float minX, float maxX) {
        EXPECT_FLOAT_EQ(minX, box.Min.x); EXPECT_FLOAT_EQ(maxX, box.Max.x);
        EXPECT_FLOAT_EQ(-1, box.Min.y); EXPECT_FLOAT_EQ(1, box.Max.y);
        EXPECT_FLOAT_EQ(-1, box.Min.z); EXPECT_FLOAT_EQ(1, box.Max.z);
    }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_NEAR(expected[row][column], actual[row][column], 1e-5f);
    }
    static void Plane(const VxPlane &actual, const VxPlane &expected) {
        EXPECT_FLOAT_EQ(expected.m_Normal.x, actual.m_Normal.x);
        EXPECT_FLOAT_EQ(expected.m_Normal.y, actual.m_Normal.y);
        EXPECT_FLOAT_EQ(expected.m_Normal.z, actual.m_Normal.z);
        EXPECT_FLOAT_EQ(expected.m_D, actual.m_D);
    }
};
}

TEST_F(RenderContextGeometryCompletionTest, BoundsRefreshLiveHierarchyAndExcludeUnattachedRoots) {
    RCK3dEntity *parent = Entity(), *child = Entity(), *excluded = Entity();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, excluded);
    const VxBbox box = UnitBox();
    ASSERT_TRUE(parent->SetBoundingBox(&box, TRUE));
    ASSERT_TRUE(child->SetBoundingBox(&box, TRUE));
    ASSERT_TRUE(excluded->SetBoundingBox(&box, TRUE));
    parent->SetPosition3f(10, 0, 0); child->SetPosition3f(20, 0, 0);
    excluded->SetPosition3f(1000, 0, 0);
    ASSERT_TRUE(child->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
    Device->AddObject(parent);
    VxBbox actual;
    Device->GetBoundingBox(&actual); Box(actual, 9, 21);
    child->SetPosition3f(30, 0, 0);
    Device->GetBoundingBox(&actual); Box(actual, 9, 31);
    Device->RemoveObject(parent);
    Device->GetBoundingBox(&actual); EXPECT_FALSE(actual.IsValid());
}

TEST_F(RenderContextGeometryCompletionTest, ClipRectSubmitsMatchingViewportAndProjectionWithoutReplacingFullView) {
    Device->m_RasterizerContext = &Observer;
    Viewport(10, 20, 200, 100);
    Device->m_NearPlane = 1; Device->m_FarPlane = 11;
    VxMatrix full;
    full.Perspective(PI / 2, 2, 1, 11);
    Device->SetProjectionTransformationMatrix(full);
    Observer.SetViewport(&Device->m_ViewportData);
    VxRect clip(60, 45, 160, 95);
    Device->SetClipRect(&clip);
    EXPECT_EQ(60, Observer.Viewport.ViewX); EXPECT_EQ(45, Observer.Viewport.ViewY);
    EXPECT_EQ(100, Observer.Viewport.ViewWidth); EXPECT_EQ(50, Observer.Viewport.ViewHeight);
    EXPECT_FLOAT_EQ(0, Observer.Viewport.ViewZMin); EXPECT_FLOAT_EQ(1, Observer.Viewport.ViewZMax);
    const VxMatrix &cropped = Observer.Matrices[VXMATRIX_PROJECTION];
    EXPECT_NEAR(2, cropped[0][0], 1e-5f); EXPECT_NEAR(4, cropped[1][1], 1e-5f);
    // Project the same visible point through the actual delivered pair.
    // Cropping must preserve its original screen position, (135, 82.5).
    const float clipX = .5f * cropped[0][0] + 2 * cropped[2][0];
    const float clipY = -.25f * cropped[1][1] + 2 * cropped[2][1];
    const float clipW = 2 * cropped[2][3] + cropped[3][3];
    EXPECT_NEAR(135, Observer.Viewport.ViewX + (clipX / clipW + 1) * Observer.Viewport.ViewWidth * .5f, 1e-4f);
    EXPECT_NEAR(82.5f, Observer.Viewport.ViewY + (1 - clipY / clipW) * Observer.Viewport.ViewHeight * .5f, 1e-4f);
    EXPECT_EQ(10, Device->m_ViewportData.ViewX); EXPECT_EQ(200, Device->m_ViewportData.ViewWidth);
    Matrix(Device->GetProjectionTransformationMatrix(), full);
    // The saved full projection and viewport remain usable by scene traversal.
    Device->UpdateProjection(TRUE);
    EXPECT_EQ(10, Observer.Viewport.ViewX); EXPECT_EQ(20, Observer.Viewport.ViewY);
    EXPECT_EQ(200, Observer.Viewport.ViewWidth); EXPECT_EQ(100, Observer.Viewport.ViewHeight);
}

TEST_F(RenderContextGeometryCompletionTest, ClipPlaneFailureDoesNotCommitAndGetterObservesActualBackend) {
    const VxPlane original(1, 0, 0, -2), replacement(0, 1, 0, -3), external(0, 0, 1, -4);
    ASSERT_TRUE(Device->SetUserClipPlane(2, original));
    Device->m_RasterizerContext = &Observer;
    Observer.AcceptSet = FALSE;
    EXPECT_FALSE(Device->SetUserClipPlane(2, replacement));
    Device->m_RasterizerContext = nullptr;
    VxPlane actual;
    ASSERT_TRUE(Device->GetUserClipPlane(2, actual)); Plane(actual, original);
    Device->m_RasterizerContext = &Observer;
    Observer.AcceptSet = TRUE;
    ASSERT_TRUE(Device->SetUserClipPlane(2, replacement)); Plane(Observer.Planes[2], replacement);
    Observer.Planes[2] = external;
    ASSERT_TRUE(Device->GetUserClipPlane(2, actual)); Plane(actual, external);
    Observer.AcceptGet = FALSE;
    EXPECT_FALSE(Device->GetUserClipPlane(2, actual));
    EXPECT_FALSE(Device->SetUserClipPlane(6, replacement));
    EXPECT_FALSE(Device->GetUserClipPlane(6, actual));
    EXPECT_EQ(2, Observer.PlaneSets); EXPECT_EQ(2, Observer.PlaneGets);
    Device->m_RasterizerContext = nullptr;
    ASSERT_TRUE(Device->GetUserClipPlane(2, actual)); Plane(actual, replacement);
}

TEST_F(RenderContextGeometryCompletionTest, ClippingPreservesOrthographicAndOffCenterProjectionAndDepth) {
    Device->m_RasterizerContext = &Observer;
    Viewport(10, 20, 200, 100);
    VxRect clip(60, 30, 160, 80);
    VxMatrix projection;
    projection.Orthographic(2, 2, 1, 11);
    projection[3][0] = .1f; projection[3][1] = -.2f;
    Device->SetProjectionTransformationMatrix(projection);
    Device->SetClipRect(&clip);
    const VxMatrix orthographic = Observer.Matrices[VXMATRIX_PROJECTION];
    EXPECT_FLOAT_EQ(0, orthographic[2][3]); EXPECT_FLOAT_EQ(1, orthographic[3][3]);
    EXPECT_FLOAT_EQ(projection[2][2], orthographic[2][2]);
    EXPECT_FLOAT_EQ(projection[3][2], orthographic[3][2]);
    // With an orthographic camera, changing depth cannot move the pixel.
    for (int z = 2; z <= 4; z += 2) {
        const float x = .1f * orthographic[0][0] + z * orthographic[2][0] + orthographic[3][0];
        const float y = .1f * orthographic[1][1] + z * orthographic[2][1] + orthographic[3][1];
        EXPECT_NEAR(140, Observer.Viewport.ViewX + (x + 1) * Observer.Viewport.ViewWidth * .5f, 1e-4f);
        EXPECT_NEAR(60, Observer.Viewport.ViewY + (1 - y) * Observer.Viewport.ViewHeight * .5f, 1e-4f);
    }
    projection.Perspective(PI / 2, 2, 1, 11);
    projection[2][0] = .15f; projection[2][1] = -.1f;
    Device->SetProjectionTransformationMatrix(projection);
    Device->SetClipRect(&clip);
    const VxMatrix perspective = Observer.Matrices[VXMATRIX_PROJECTION];
    const float x = .2f * perspective[0][0] + 2 * perspective[2][0];
    const float y = .1f * perspective[1][1] + 2 * perspective[2][1];
    EXPECT_NEAR(135, Observer.Viewport.ViewX + (x / 2 + 1) * Observer.Viewport.ViewWidth * .5f, 1e-4f);
    EXPECT_NEAR(70, Observer.Viewport.ViewY + (1 - y / 2) * Observer.Viewport.ViewHeight * .5f, 1e-4f);
    EXPECT_FLOAT_EQ(projection[2][2], perspective[2][2]);
    EXPECT_FLOAT_EQ(projection[3][2], perspective[3][2]);
    const int viewportWrites = Observer.ViewportWrites, projectionWrites = Observer.ProjectionWrites;
    VxRect empty(60, 30, 60, 80);
    Device->SetClipRect(&empty);
    EXPECT_EQ(viewportWrites, Observer.ViewportWrites); EXPECT_EQ(projectionWrites, Observer.ProjectionWrites);
    Matrix(Observer.Matrices[VXMATRIX_PROJECTION], perspective);
}

TEST_F(RenderContextGeometryCompletionTest, AttachmentDeduplicatesAndRootEnumerationIsSharedWhileDetachClearsMembership) {
    RCK3dEntity *attached = Entity(), *unattached = Entity();
    ASSERT_NE(nullptr, attached); ASSERT_NE(nullptr, unattached);
    EXPECT_FALSE(Device->IsObjectAttached(nullptr));
    Device->AddObjectWithHierarchy(attached); Device->AddObject(attached);
    EXPECT_TRUE(Device->IsObjectAttached(attached));
    EXPECT_FALSE(Device->IsObjectAttached(unattached));
    attached->m_RenderExtents = VxRect(20, 20, 30, 30);
    XObjectPointerArray selected;
    ASSERT_EQ(CK_OK, Device->RectPick(VxRect(10, 10, 40, 40), selected));
    ASSERT_EQ(1, selected.Size()); EXPECT_EQ(attached, selected[0]);
    const XObjectArray &roots = Device->Compute3dRootObjects();
    EXPECT_TRUE(roots.IsHere(attached->GetID())); EXPECT_TRUE(roots.IsHere(unattached->GetID()));
    RCK2dEntity *foreground = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
    RCK2dEntity *background = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
    ASSERT_NE(nullptr, foreground); ASSERT_NE(nullptr, background);
    background->SetBackground(TRUE); Device->AddObject(foreground); Device->AddObject(background);
    const XObjectArray &roots2d = Device->Compute2dRootObjects();
    ASSERT_EQ(2, roots2d.Size());
    EXPECT_EQ(background->GetID(), roots2d[0]); EXPECT_EQ(foreground->GetID(), roots2d[1]);
    Device->AddExtents2D(VxRect(1, 2, 3, 4), attached);
    Device->RemoveObject(attached);
    EXPECT_FALSE(Device->IsObjectAttached(attached));
    ASSERT_EQ(1, Device->m_ObjectExtents.Size()); EXPECT_EQ(nullptr, Device->m_ObjectExtents[0].m_Entity);
    Device->AddObject(attached); Device->DetachAll();
    EXPECT_FALSE(Device->IsObjectAttached(attached)); EXPECT_EQ(0, Device->m_ObjectExtents.Size());
    EXPECT_FALSE(Device->IsObjectAttached(foreground)); EXPECT_FALSE(Device->IsObjectAttached(background));
    selected.Clear();
    ASSERT_EQ(CK_OK, Device->RectPick(VxRect(10, 10, 40, 40), selected));
    EXPECT_EQ(0, selected.Size());
}

TEST_F(RenderContextGeometryCompletionTest, CameraAttachmentCopiesWorldPoseAndDetachKeepsViewpointPose) {
    RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    ASSERT_NE(nullptr, camera);
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(0, 0, -1); pose[2].Set(1, 0, 0); pose[3].Set(3, 5, 7);
    camera->SetWorldMatrix(pose);
    camera->ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    camera->SetFlags(camera->GetFlags() | CK_3DENTITY_CAMERAIGNOREASPECT);
    Device->m_Settings.m_Rect = {10, 20, 320, 240};
    Viewport(40, 50, 100, 80);
    Device->m_RasterizerContext = &Observer;
    CK3dEntity *viewpoint = Device->GetViewpoint();
    CKCamera *cameraApi = reinterpret_cast<CKCamera *>(camera);
    Device->AttachViewpointToCamera(cameraApi);
    EXPECT_EQ(cameraApi, Device->GetAttachedCamera()); EXPECT_EQ(viewpoint, Device->GetViewpoint());
    EXPECT_FALSE(camera->GetObjectFlags() & CK_OBJECT_UPTODATE);
    Matrix(viewpoint->GetWorldMatrix(), pose);
    EXPECT_EQ(0, Observer.Viewport.ViewX); EXPECT_EQ(0, Observer.Viewport.ViewY);
    EXPECT_EQ(320, Observer.Viewport.ViewWidth); EXPECT_EQ(240, Observer.Viewport.ViewHeight);
    Device->AttachViewpointToCamera(nullptr); EXPECT_EQ(cameraApi, Device->GetAttachedCamera());
    Device->DetachViewpointFromCamera(); EXPECT_EQ(nullptr, Device->GetAttachedCamera());
    Matrix(viewpoint->GetWorldMatrix(), pose);
}

TEST_F(RenderContextGeometryCompletionTest, ProjectionUpdateCacheAndMatrixDeliveryServeCpuAndDeviceConsumers) {
    Device->m_RasterizerContext = &Observer;
    Device->m_Settings.m_Rect = {0, 0, 300, 200};
    Viewport(10, 20, 200, 100, .25f, .75f);
    Device->m_Perspective = TRUE; Device->m_Fov = PI / 2;
    Device->m_NearPlane = 1; Device->m_FarPlane = 11;
    Device->m_ProjectionUpdated = FALSE; Device->UpdateProjection(FALSE);
    EXPECT_EQ(1, Observer.ViewportWrites); EXPECT_EQ(1, Observer.ProjectionWrites);
    VxMatrix expected; expected.Perspective(PI / 2, 2, 1, 11);
    Matrix(Device->GetProjectionTransformationMatrix(), expected);
    Matrix(Observer.Matrices[VXMATRIX_PROJECTION], expected);
    Device->m_Perspective = FALSE; Device->m_Zoom = 2;
    Device->UpdateProjection(FALSE); EXPECT_EQ(1, Observer.ProjectionWrites);
    Device->UpdateProjection(TRUE); EXPECT_EQ(2, Observer.ProjectionWrites);
    expected.Orthographic(2, 2, 1, 11);
    Matrix(Device->GetProjectionTransformationMatrix(), expected);
    VxMatrix world = VxMatrix::Identity(), view = VxMatrix::Identity();
    world[3].Set(1, 2, 3); view[3].Set(-4, -5, -6);
    Device->SetWorldTransformationMatrix(world); Device->SetViewTransformationMatrix(view);
    Matrix(Device->GetWorldTransformationMatrix(), world); Matrix(Observer.Matrices[VXMATRIX_WORLD], world);
    Matrix(Device->GetViewTransformationMatrix(), view); Matrix(Observer.Matrices[VXMATRIX_VIEW], view);
}
