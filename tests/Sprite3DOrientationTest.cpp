#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKSprite3D.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCKCamera.h"
#include "RCKMaterial.h"
#include "RCK2dEntity.h"
#include "CKRenderedScene.h"
#include "CKSceneGraph.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SpriteOrientationProbe : public RCKSprite3D {
public:
    explicit SpriteOrientationProbe(CKContext *context) : RCKSprite3D(context) {}
    void Orient(CKRenderContext *view) { UpdateOrientation(view); }
};
class SpriteViewProbe : public RCKRenderContext {
public:
    explicit SpriteViewProbe(CKContext *context) : RCKRenderContext(context) {}
    CKRenderedScene &Scene() { return *m_RenderedScene; }
};
class Sprite3DOrientationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKRenderContext *API(RCKRenderContext &view) { return reinterpret_cast<CKRenderContext *>(&view); }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, .0001f); EXPECT_NEAR(y, v.y, .0001f); EXPECT_NEAR(z, v.z, .0001f);
    }
    static void Same(const VxMatrix &a, const VxMatrix &b) {
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) EXPECT_NEAR(a[row][column], b[row][column], .0001f);
    }
    static RCK3dEntity &Root(SpriteViewProbe &view) { return *reinterpret_cast<RCK3dEntity *>(view.Scene().GetRootEntity()); }
    static void Perspective(SpriteViewProbe &view) {
        view.m_ViewportData.ViewX = 10; view.m_ViewportData.ViewY = 20;
        view.m_ViewportData.ViewWidth = 200; view.m_ViewportData.ViewHeight = 200;
        view.m_ViewportData.ViewZMin = 0; view.m_ViewportData.ViewZMax = 1;
        VxMatrix projection; projection.Perspective(PI / 2, 1, 1, 10);
        view.SetProjectionTransformationMatrix(projection); view.SetViewTransformationMatrix(VxMatrix::Identity());
    }
};
}

TEST_F(Sprite3DOrientationTest, BillboardUsesPreparedCameraOrientationAndPreservesSpriteTranslation) {
    SpriteViewProbe view(Context); RCKCamera camera(Context); SpriteOrientationProbe sprite(Context);
    VxMatrix cam = VxMatrix::Identity(); cam[0].Set(0, 1, 0); cam[1].Set(-1, 0, 0); cam[3].Set(7, 8, 9); camera.SetWorldMatrix(cam);
    view.AttachViewpointToCamera(reinterpret_cast<CKCamera *>(&camera)); view.PrepareCameras();
    ASSERT_NE(nullptr, view.Scene().GetRootEntity()); Same(cam, Root(view).GetWorldMatrix());
    sprite.SetPosition3f(30, 40, 50); sprite.UpdateCameraFacingOrientation(API(view));
    Vec(sprite.GetWorldMatrix()[0], 0, 1, 0); Vec(sprite.GetWorldMatrix()[1], -1, 0, 0);
    Vec(sprite.GetWorldMatrix()[2], 0, 0, 1); Vec(sprite.GetWorldMatrix()[3], 30, 40, 50);
    EXPECT_NEAR(29, sprite.GetBoundingBox(FALSE).Min.x, .0001f); EXPECT_NEAR(31, sprite.GetBoundingBox(FALSE).Max.x, .0001f);
    view.DetachViewpointFromCamera();
}
TEST_F(Sprite3DOrientationTest, XLockedModeProjectsCameraUpOntoYZPlaneAndBuildsOrthogonalAxes) {
    SpriteViewProbe view(Context); SpriteOrientationProbe sprite(Context); VxMatrix camera = VxMatrix::Identity();
    camera[0].Set(1, 0, 0); camera[1].Set(0, .8f, .6f); camera[2].Set(0, -.6f, .8f); Root(view).SetWorldMatrix(camera);
    sprite.SetPosition3f(2, 3, 4); sprite.SetMode(VXSPRITE3D_XROTATE); sprite.Orient(API(view));
    Vec(sprite.GetWorldMatrix()[0], 1, 0, 0); Vec(sprite.GetWorldMatrix()[1], 0, .8f, .6f);
    Vec(sprite.GetWorldMatrix()[2], 0, -.6f, .8f); Vec(sprite.GetWorldMatrix()[3], 2, 3, 4);
}
TEST_F(Sprite3DOrientationTest, YLockedModeProjectsCameraForwardOntoXZPlaneAndBuildsOrthogonalAxes) {
    SpriteViewProbe view(Context); SpriteOrientationProbe sprite(Context); VxMatrix camera = VxMatrix::Identity();
    camera[0].Set(.8f, 0, -.6f); camera[2].Set(.6f, 0, .8f); Root(view).SetWorldMatrix(camera);
    sprite.SetMode(VXSPRITE3D_YROTATE); sprite.UpdateCameraFacingOrientation(API(view));
    Vec(sprite.GetWorldMatrix()[0], .8f, 0, -.6f); Vec(sprite.GetWorldMatrix()[1], 0, 1, 0); Vec(sprite.GetWorldMatrix()[2], .6f, 0, .8f);
}
TEST_F(Sprite3DOrientationTest, OrientableAndUnknownModesLeaveActualWorldTransformUnchanged) {
    SpriteViewProbe view(Context); SpriteOrientationProbe sprite(Context); VxMatrix world = VxMatrix::Identity();
    world[0].Set(2, 0, 0); world[1].Set(0, 3, 0); world[3].Set(9, 8, 7); sprite.SetWorldMatrix(world);
    sprite.SetMode(VXSPRITE3D_ORIENTABLE); sprite.Orient(API(view)); Same(world, sprite.GetWorldMatrix());
    sprite.UpdateCameraFacingOrientation(API(view)); Same(world, sprite.GetWorldMatrix());
    sprite.SetMode(static_cast<VXSPRITE3D_TYPE>(17)); EXPECT_EQ(17, static_cast<int>(sprite.GetMode()));
    sprite.Orient(API(view)); Same(world, sprite.GetWorldMatrix());
}
TEST_F(Sprite3DOrientationTest, BillboardCopiesCameraAxisScaleAndHomogeneousComponentsExactly) {
    SpriteViewProbe view(Context); SpriteOrientationProbe sprite(Context); VxMatrix world = VxMatrix::Identity();
    world[0].Set(0, 2, 0); world[1].Set(-3, 0, 0); world[2].Set(0, 0, 4); Root(view).SetWorldMatrix(world);
    sprite.SetPosition3f(3, 5, 7); sprite.Orient(API(view));
    for (int row = 0; row < 3; ++row) for (int column = 0; column < 4; ++column) EXPECT_FLOAT_EQ(world[row][column], sprite.GetWorldMatrix()[row][column]);
    Vec(sprite.GetWorldMatrix()[3], 3, 5, 7);
}
TEST_F(Sprite3DOrientationTest, NullCameraContextLeavesTransformUnchanged) {
    SpriteOrientationProbe sprite(Context); sprite.SetPosition3f(3, 5, 7); const VxMatrix world = sprite.GetWorldMatrix();
    sprite.Orient(nullptr); sprite.UpdateCameraFacingOrientation(nullptr); Same(world, sprite.GetWorldMatrix());
}
TEST_F(Sprite3DOrientationTest, InsideFrustumProjectsActualQuadAndMarksExtentsAndInsideCache) {
    SpriteViewProbe view(Context); Perspective(view); SpriteOrientationProbe sprite(Context); sprite.SetMode(VXSPRITE3D_ORIENTABLE);
    Vx2DVector size(2, 2); sprite.SetSize(size); sprite.SetPosition3f(0, 0, 5); ASSERT_NE(nullptr, sprite.m_SceneGraphNode);
    sprite.m_SceneGraphNode->SetAsPotentiallyVisible(); ASSERT_TRUE(sprite.IsInViewFrustrum(API(view), CKRENDER_UPDATEEXTENTS));
    EXPECT_NEAR(90, sprite.m_RenderExtents.left, .06f); EXPECT_NEAR(100, sprite.m_RenderExtents.top, .06f);
    EXPECT_NEAR(130, sprite.m_RenderExtents.right, .06f); EXPECT_NEAR(140, sprite.m_RenderExtents.bottom, .06f);
    EXPECT_TRUE(sprite.GetMoveableFlags() & VX_MOVEABLE_EXTENTSUPTODATE);
    EXPECT_EQ(CKSGN_INSIDEFRUSTUM, sprite.m_SceneGraphNode->GetFlags() & CKSGN_FRUSTUM_MASK);
    Same(sprite.GetWorldMatrix(), view.GetWorldTransformationMatrix());
}
TEST_F(Sprite3DOrientationTest, OutsideEveryFrustumPlaneIsRejectedByActualProjection) {
    SpriteViewProbe view(Context); Perspective(view); const VxVector positions[] = {
        VxVector(-9, 0, 5), VxVector(9, 0, 5), VxVector(0, -9, 5), VxVector(0, 9, 5), VxVector(0, 0, .5f), VxVector(0, 0, 11)};
    for (int i = 0; i < 6; ++i) {
        SCOPED_TRACE(i); SpriteOrientationProbe sprite(Context); sprite.SetMode(VXSPRITE3D_ORIENTABLE); sprite.SetPosition(&positions[i]);
        ASSERT_NE(nullptr, sprite.m_SceneGraphNode); sprite.m_SceneGraphNode->SetAsPotentiallyVisible();
        EXPECT_FALSE(sprite.IsInViewFrustrum(API(view), CKRENDER_UPDATEEXTENTS));
        EXPECT_EQ(CKSGN_OUTSIDEFRUSTUM, sprite.m_SceneGraphNode->GetFlags() & CKSGN_FRUSTUM_MASK);
    }
}
TEST_F(Sprite3DOrientationTest, PartialFrustumIntersectionIsVisibleWithoutWholeInsideFlag) {
    SpriteViewProbe view(Context); Perspective(view); SpriteOrientationProbe sprite(Context); sprite.SetMode(VXSPRITE3D_ORIENTABLE);
    sprite.SetPosition3f(5, 0, 5); ASSERT_NE(nullptr, sprite.m_SceneGraphNode); sprite.m_SceneGraphNode->SetAsPotentiallyVisible();
    EXPECT_TRUE(sprite.IsInViewFrustrum(API(view), CKRENDER_UPDATEEXTENTS));
    EXPECT_EQ(0u, sprite.m_SceneGraphNode->GetFlags() & CKSGN_FRUSTUM_MASK);
}
TEST_F(Sprite3DOrientationTest, NoExtentsFlagPreservesRectangleAndDontSetMatrixPreservesContextTransform) {
    SpriteViewProbe view(Context); Perspective(view); SpriteOrientationProbe sprite(Context); sprite.SetMode(VXSPRITE3D_ORIENTABLE);
    sprite.SetPosition3f(0, 0, 5); sprite.m_RenderExtents = VxRect(7, 8, 9, 10);
    VxMatrix previous = VxMatrix::Identity(); previous[3].Set(31, 41, 51); view.SetWorldTransformationMatrix(previous);
    EXPECT_TRUE(sprite.IsInViewFrustrum(API(view), CKRENDER_DONTSETMATRIX)); Same(previous, view.GetWorldTransformationMatrix());
    EXPECT_FLOAT_EQ(7, sprite.m_RenderExtents.left); EXPECT_FLOAT_EQ(8, sprite.m_RenderExtents.top);
    EXPECT_FLOAT_EQ(9, sprite.m_RenderExtents.right); EXPECT_FLOAT_EQ(10, sprite.m_RenderExtents.bottom);
}
