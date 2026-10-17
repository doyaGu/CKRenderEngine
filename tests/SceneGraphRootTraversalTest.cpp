#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKSceneGraph.h"
#include "RCK2dEntity.h"
#include "RCK3dEntity.h"
#include "RCKAnimation.h"
#include "RCKCamera.h"
#include "RCKCharacter.h"
#include "RCKKeyedAnimation.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKPlace.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Observe genuine entity/mesh submissions, not a replacement Render method.
class RootTraversalBoundary : public MaterialRenderObserver {
public:
    RCKRenderContext *Device = nullptr;
    int Draws = 0;
    CK_ID Objects[64] = {};
    CKBOOL Transparent[64] = {};
    CKViewportData DrawViewport[64] = {};
    VxMatrix DrawProjection[64];
    CKBOOL BeginScene() override { return TRUE; }
    CKBOOL EndScene() override { return TRUE; }
    CKBOOL BeginOverlayPhase() override { return TRUE; }
    CKBOOL EnableLight(CKDWORD, CKBOOL) override { return TRUE; }
    void InitDefaultRenderStatesValue() override {}
    // No hardware resource provider is implemented by this observer. Rejection
    // leaves real mesh CPU fallback and stream construction in production code.
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *, const void *, CKDWORD *) override { return FALSE; }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_NE(nullptr, indices); EXPECT_EQ(3, count); EXPECT_NE(nullptr, data);
        EXPECT_LT(Draws, 64); EXPECT_NE(nullptr, Device);
        if (!data || Draws >= 64 || !Device) return FALSE;
        EXPECT_NE(nullptr, data->PositionPtr); EXPECT_NE(nullptr, Device->m_Current3dEntity);
        if (!Device->m_Current3dEntity) return FALSE;
        Objects[Draws] = Device->m_Current3dEntity->GetID();
        Transparent[Draws] = Device->m_SortTransparentObjects;
        DrawViewport[Draws] = Viewport; DrawProjection[Draws] = Matrices[VXMATRIX_PROJECTION];
        ++Draws; return TRUE;
    }
};

class SceneGraphRootTraversalTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr, *Other = nullptr;
    RootTraversalBoundary Boundary, OtherBoundary;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 8;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 8;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Device = View(Boundary); ASSERT_NE(nullptr, Device);
        Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
    }
    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; }
        if (Other) { Other->m_RasterizerContext = nullptr; Other->m_RasterizerDriver = nullptr; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCKRenderContext *View(RootTraversalBoundary &boundary) {
        if (!Manager->RegisterRasterizerContext(&boundary, 0)) return nullptr;
        RCKRenderContext *view = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        if (!view) return nullptr;
        view->DetachAll(); view->m_RasterizerContext = &boundary; view->m_RasterizerDriver = &Driver;
        boundary.Device = view;
        view->m_Settings.m_Rect = {0, 0, 100, 100};
        view->m_ViewportData.ViewWidth = view->m_ViewportData.ViewHeight = 100;
        view->m_ViewportData.ViewZMin = 0; view->m_ViewportData.ViewZMax = 1;
        boundary.Viewport = view->m_ViewportData;
        view->m_Fov = PI / 2; view->m_NearPlane = 1; view->m_FarPlane = 100;
        VxMatrix projection; projection.Perspective(PI / 2, 1, 1, 100);
        view->SetProjectionTransformationMatrix(projection); view->SetViewTransformationMatrix(VxMatrix::Identity());
        view->Activate(TRUE);
        return view;
    }
    RCK3dEntity *Triangle(float x = 0, float z = 5, CKBOOL transparent = TRUE, CK_CLASSID cid = CKCID_3DENTITY) {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(cid));
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        if (!entity || !mesh || !material || !mesh->SetVertexCount(3) || !mesh->SetFaceCount(1)) return nullptr;
        VxVector points[3] = {VxVector(-.25f, -.25f, 0), VxVector(0, .25f, 0), VxVector(.25f, -.25f, 0)};
        mesh->SetLitMode(VX_PRELITMESH);
        for (int i = 0; i < 3; ++i) { mesh->SetVertexPosition(i, &points[i]); mesh->SetVertexColor(i, 0xffffffffu); }
        mesh->SetFaceVertexIndex(0, 0, 1, 2); mesh->SetFaceMaterial(0, material);
        mesh->BuildFaceNormals(); mesh->VertexMove();
        entity->SetCurrentMesh(mesh); entity->SetPosition3f(x, 0, z); entity->UpdateBox(TRUE);
        entity->SetRenderAsTransparent(transparent);
        return entity;
    }
    void Attach(RCK3dEntity *entity, RCKRenderContext *view = nullptr) {
        if (!view) view = Device;
        view->AddObjectWithHierarchy(entity); view->AddRemoveSequence(FALSE);
    }
    RCKCamera *Camera(float x, RCKRenderContext *view) {
        RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
        if (!camera) return nullptr;
        camera->SetPosition3f(x, 0, 0); camera->SetFov(PI / 2); camera->SetFrontPlane(1); camera->SetBackPlane(100);
        view->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(camera)); return camera;
    }
    void RepeatedDraw(CKBOOL sorted, int count) {
        Manager->m_SortTransparentObjects.Value = sorted;
        RCK3dEntity *entities[2] = {Triangle(), count == 2 ? Triangle(0, 7) : nullptr};
        ASSERT_NE(nullptr, entities[0]); if (count == 2) ASSERT_NE(nullptr, entities[1]);
        for (int i = 0; i < count; ++i) Attach(entities[i]);
        const CKDWORD stamp = Device->m_TimeFpsCalc;
        ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
        ASSERT_EQ(count, Boundary.Draws);
        EXPECT_EQ(stamp, Device->m_TimeFpsCalc);
        for (int i = 0; i < count; ++i) { entities[i]->Show(CKHIDE); EXPECT_EQ(stamp, entities[i]->m_SceneGraphNode->m_TimeFpsCalc); }
        Device->AddRemoveSequence(FALSE);
        ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
        EXPECT_EQ(stamp, Device->m_TimeFpsCalc);
        EXPECT_EQ(count, Boundary.Draws) << "A new DrawScene must not replay the previous visibility sample";
        EXPECT_EQ(0, Manager->m_SceneGraphRootNode.m_TransparentObjects.Size());
        for (int i = 0; i < count; ++i) EXPECT_FALSE(entities[i]->m_SceneGraphNode->IsInTransparentList());
        entities[0]->Show(CKSHOW); Device->AddRemoveSequence(FALSE);
        const int beforeVisible = Boundary.Draws;
        ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
        ASSERT_EQ(beforeVisible + 1, Boundary.Draws); EXPECT_EQ(entities[0]->GetID(), Boundary.Objects[beforeVisible]);
        EXPECT_TRUE(Boundary.Transparent[beforeVisible]);
    }
    void EqualCounterViews(CKBOOL sorted, int count) {
        Manager->m_SortTransparentObjects.Value = sorted;
        Other = View(OtherBoundary); ASSERT_NE(nullptr, Other);
        ASSERT_NE(nullptr, Camera(0, Device)); RCKCamera *otherCamera = Camera(50, Other); ASSERT_NE(nullptr, otherCamera);
        RCK3dEntity *entities[2] = {Triangle(), count == 2 ? Triangle(0, 7) : nullptr};
        ASSERT_NE(nullptr, entities[0]); if (count == 2) ASSERT_NE(nullptr, entities[1]);
        for (int i = 0; i < count; ++i) { Attach(entities[i]); Attach(entities[i], Other); }
        // Both newly created contexts naturally start with the same counter.
        // No frame counter or private queue fields are assigned by this test.
        ASSERT_EQ(Device->m_TimeFpsCalc, Other->m_TimeFpsCalc);
        const CKDWORD sharedStamp = Other->m_TimeFpsCalc;
        ASSERT_EQ(CK_OK, Device->Render(CK_RENDER_DONOTUPDATEEXTENTS));
        ASSERT_EQ(count, Boundary.Draws);
        for (int i = 0; i < count; ++i) EXPECT_EQ(sharedStamp, entities[i]->m_SceneGraphNode->m_TimeFpsCalc);
        ASSERT_EQ(CK_OK, Other->Render(CK_RENDER_DONOTUPDATEEXTENTS));
        EXPECT_EQ(0, OtherBoundary.Draws) << "Other camera rejects every triangle; matching counters are not visibility";
        EXPECT_EQ(0, Manager->m_SceneGraphRootNode.m_TransparentObjects.Size());
        for (int i = 0; i < count; ++i) EXPECT_TRUE(entities[i]->m_SceneGraphNode->IsAllOutsideFrustum());
        // Sampling again from the formerly rejecting real camera must still
        // render normally once its public pose makes those objects visible.
        otherCamera->SetPosition3f(0, 0, 0);
        const int beforeVisible = OtherBoundary.Draws;
        ASSERT_EQ(CK_OK, Other->Render(CK_RENDER_DONOTUPDATEEXTENTS));
        EXPECT_EQ(beforeVisible + count, OtherBoundary.Draws);
    }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            EXPECT_NEAR(expected[row][column], actual[row][column], 1e-5f);
    }
};

TEST_F(SceneGraphRootTraversalTest, RepeatedDrawSceneCannotReplayAHiddenSingletonWithTheSameNaturalCounter) {
    RepeatedDraw(FALSE, 1);
}
TEST_F(SceneGraphRootTraversalTest, RepeatedDrawSceneCannotReplayHiddenSortedCandidatesWithTheSameNaturalCounter) {
    RepeatedDraw(TRUE, 2);
}
TEST_F(SceneGraphRootTraversalTest, TwoRealCameraRendersWithEqualCountersDoNotShareSingletonVisibility) {
    EqualCounterViews(FALSE, 1);
}
TEST_F(SceneGraphRootTraversalTest, TwoRealCameraRendersWithEqualCountersDoNotShareSortedVisibility) {
    EqualCounterViews(TRUE, 2);
}

TEST_F(SceneGraphRootTraversalTest, SwitchingATransparentEntityToOpaqueDoesNotAlsoReplayItsOldDeferredEntry) {
    RCK3dEntity *entity = Triangle(); ASSERT_NE(nullptr, entity); Attach(entity);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS)); ASSERT_EQ(1, Boundary.Draws);
    EXPECT_TRUE(Boundary.Transparent[0]);
    entity->SetRenderAsTransparent(FALSE);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(2, Boundary.Draws); EXPECT_FALSE(Boundary.Transparent[1]);
    EXPECT_EQ(0, Manager->m_SceneGraphRootNode.m_TransparentObjects.Size());
    EXPECT_FALSE(entity->m_SceneGraphNode->IsInTransparentList());
}

TEST_F(SceneGraphRootTraversalTest, IntersectingHierarchyClipsEachLeafThenSubmitsOpaqueBeforeTransparent) {
    RCK3dEntity *parent = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    RCK3dEntity *opaque = Triangle(0, 5, FALSE), *transparent = Triangle(0, 7), *outside = Triangle(30, 5, FALSE);
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, opaque); ASSERT_NE(nullptr, transparent); ASSERT_NE(nullptr, outside);
    ASSERT_TRUE(opaque->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
    ASSERT_TRUE(transparent->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
    ASSERT_TRUE(outside->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE)); Attach(parent);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(2, Boundary.Draws); EXPECT_EQ(opaque->GetID(), Boundary.Objects[0]); EXPECT_FALSE(Boundary.Transparent[0]);
    EXPECT_EQ(transparent->GetID(), Boundary.Objects[1]); EXPECT_TRUE(Boundary.Transparent[1]);
    EXPECT_FALSE(parent->m_SceneGraphNode->CheckHierarchyFrustum());
    EXPECT_TRUE(outside->m_SceneGraphNode->IsAllOutsideFrustum());
    outside->SetPosition3f(.5f, 0, 5);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(5, Boundary.Draws); EXPECT_TRUE(parent->m_SceneGraphNode->CheckHierarchyFrustum());
    EXPECT_EQ(opaque->GetID(), Boundary.Objects[2]); EXPECT_EQ(outside->GetID(), Boundary.Objects[3]);
    EXPECT_FALSE(Boundary.Transparent[2]); EXPECT_FALSE(Boundary.Transparent[3]);
    EXPECT_EQ(transparent->GetID(), Boundary.Objects[4]); EXPECT_TRUE(Boundary.Transparent[4]);
}

TEST_F(SceneGraphRootTraversalTest, CulledCharacterUsesExpandedVisibilityOnlyForItsRenderedMarker) {
    RCK3dEntity *character = Triangle(30, 5, FALSE, CKCID_CHARACTER), *child = Triangle(30, 5, FALSE);
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, child);
    VxBbox local; local.Min.Set(-.1f, -.1f, -.1f); local.Max.Set(.1f, .1f, .1f);
    ASSERT_TRUE(character->SetBoundingBox(&local, TRUE)); ASSERT_TRUE(child->SetBoundingBox(&local, TRUE));
    ASSERT_TRUE(child->SetParent(reinterpret_cast<CK3dEntity *>(character), TRUE)); Attach(character);
    ASSERT_EQ(1, character->m_SceneGraphNode->m_ChildToBeParsedCount);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(0, Boundary.Draws); EXPECT_TRUE(character->m_SceneGraphNode->IsAllOutsideFrustum());
    EXPECT_EQ(0u, character->GetMoveableFlags() & VX_MOVEABLE_CHARACTERRENDERED);
    character->ModifyMoveableFlags(0, VX_MOVEABLE_CHARACTERRENDERED);
    character->SetPosition3f(0, 0, .6f);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(0, Boundary.Draws); EXPECT_TRUE(character->m_SceneGraphNode->IsAllOutsideFrustum());
    EXPECT_NE(0u, character->GetMoveableFlags() & VX_MOVEABLE_CHARACTERRENDERED);
    EXPECT_TRUE(child->m_SceneGraphNode->IsAllOutsideFrustum());
}

TEST_F(SceneGraphRootTraversalTest, VisiblePlaceCropsTheActualSubtreeDrawAndRestoresTheSceneProjection) {
    RCKPlace *place = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE));
    RCK3dEntity *child = Triangle(0, 5, FALSE); ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(reinterpret_cast<CK3dEntity *>(place), TRUE)); Attach(place);
    place->ViewportClip() = VxRect(.25f, .25f, .75f, .75f);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS)); ASSERT_EQ(1, Boundary.Draws);
    EXPECT_EQ(child->GetID(), Boundary.Objects[0]); EXPECT_FALSE(Boundary.Transparent[0]);
    EXPECT_EQ(25u, Boundary.DrawViewport[0].ViewX); EXPECT_EQ(25u, Boundary.DrawViewport[0].ViewY);
    EXPECT_EQ(50u, Boundary.DrawViewport[0].ViewWidth); EXPECT_EQ(50u, Boundary.DrawViewport[0].ViewHeight);
    VxMatrix expected = Device->GetProjectionTransformationMatrix();
    expected[0][0] *= 2; expected[1][1] *= 2;
    Matrix(Boundary.DrawProjection[0], expected);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], Device->GetProjectionTransformationMatrix());
    EXPECT_EQ(0u, Boundary.Viewport.ViewX); EXPECT_EQ(0u, Boundary.Viewport.ViewY);
    EXPECT_EQ(100u, Boundary.Viewport.ViewWidth); EXPECT_EQ(100u, Boundary.Viewport.ViewHeight);
}
}
