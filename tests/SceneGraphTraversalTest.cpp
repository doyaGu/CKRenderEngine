#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKSceneGraph.h"
#include "RCK2dEntity.h"
#include "RCK3dEntity.h"
#include "RCKAnimation.h"
#include "RCKCamera.h"
#include "RCKCharacter.h"
#include "RCKLight.h"
#include "RCKKeyedAnimation.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKPlace.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct TraversalTrace {
    int Count = 0;
    CK_ID Objects[64] = {};
    int Phases[64] = {};
    void Add(CK_ID object, int phase) {
        EXPECT_LT(Count, 64);
        if (Count < 64) { Objects[Count] = object; Phases[Count++] = phase; }
    }
};
struct TraversalCallback { TraversalTrace *Trace; int Phase; };
CKBOOL ObserveTraversal(CKRenderContext *, CKRenderObject *object, void *argument) {
    TraversalCallback *entry = static_cast<TraversalCallback *>(argument);
    entry->Trace->Add(object->GetID(), entry->Phase);
    return FALSE; // An observation callback does not replace the real mesh draw.
}

// Only the external device submission is observed. Nodes, entities, meshes,
// callback dispatch, transparency queues and context membership remain real.
class TraversalBoundary : public MaterialRenderObserver {
public:
    RCKRenderContext *Device = nullptr;
    TraversalTrace *Trace = nullptr;
    int Draws = 0, ViewportWrites = 0;
    CK_ID Objects[64] = {};
    CKViewportData DrawViewport[64] = {};
    VxMatrix DrawProjection[64];
    CKBOOL BeginScene() override { return TRUE; }
    CKBOOL EndScene() override { return TRUE; }
    CKBOOL BeginOverlayPhase() override { return TRUE; }
    CKBOOL EnableLight(CKDWORD, CKBOOL) override { return TRUE; }
    // This interface observer has no hardware resource implementation. Let the
    // production mesh take its real CPU submission fallback after rejection.
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *, const void *, CKDWORD *) override { return FALSE; }
    void InitDefaultRenderStatesValue() override {}
    CKBOOL SetViewport(const CKViewportData *data) override {
        ++ViewportWrites;
        return MaterialRenderObserver::SetViewport(data);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_NE(nullptr, indices); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, data); EXPECT_LT(Draws, 64);
        if (!data || Draws >= 64) return FALSE;
        EXPECT_NE(nullptr, data->PositionPtr);
        EXPECT_NE(nullptr, Device); EXPECT_NE(nullptr, Device ? Device->m_Current3dEntity : nullptr);
        if (!Device || !Device->m_Current3dEntity) return FALSE;
        CK_ID id = Device->m_Current3dEntity->GetID();
        Objects[Draws] = id; DrawViewport[Draws] = Viewport;
        DrawProjection[Draws] = Matrices[VXMATRIX_PROJECTION]; ++Draws;
        if (Trace) Trace->Add(id, 2);
        return TRUE;
    }
};

class SceneGraphTraversalTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr, *Other = nullptr;
    TraversalBoundary Boundary, OtherBoundary;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 8;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 8;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Device = CreateView(Boundary); ASSERT_NE(nullptr, Device);
    }
    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; }
        if (Other) { Other->m_RasterizerContext = nullptr; Other->m_RasterizerDriver = nullptr; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCKRenderContext *CreateView(TraversalBoundary &boundary) {
        if (!Manager->RegisterRasterizerContext(&boundary, 0)) return nullptr;
        RCKRenderContext *view = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        if (!view) return nullptr;
        view->DetachAll(); view->m_RasterizerContext = &boundary; view->m_RasterizerDriver = &Driver;
        boundary.Device = view;
        view->m_Settings.m_Rect = {0, 0, 100, 100};
        view->m_ViewportData.ViewWidth = view->m_ViewportData.ViewHeight = 100;
        view->m_ViewportData.ViewZMin = 0; view->m_ViewportData.ViewZMax = 1;
        boundary.Viewport = view->m_ViewportData;
        view->m_NearPlane = 1; view->m_FarPlane = 100; view->m_Fov = PI / 2;
        VxMatrix projection; projection.Perspective(PI / 2, 1, 1, 100);
        view->SetProjectionTransformationMatrix(projection);
        view->SetViewTransformationMatrix(VxMatrix::Identity());
        return view;
    }
    RCK3dEntity *Entity(CK_CLASSID type = CKCID_3DENTITY) {
        return static_cast<RCK3dEntity *>(Context->CreateObject(type));
    }
    RCK3dEntity *Triangle(CK_CLASSID type = CKCID_3DENTITY) {
        RCK3dEntity *entity = Entity(type);
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        if (!entity || !mesh || !material || !mesh->SetVertexCount(3) || !mesh->SetFaceCount(1)) return nullptr;
        VxVector points[3] = {VxVector(-.25f, -.25f, 0), VxVector(0, .25f, 0), VxVector(.25f, -.25f, 0)};
        mesh->SetLitMode(VX_PRELITMESH);
        for (int i = 0; i < 3; ++i) { mesh->SetVertexPosition(i, &points[i]); mesh->SetVertexColor(i, 0xffffffffu); }
        mesh->SetFaceVertexIndex(0, 0, 1, 2); mesh->SetFaceMaterial(0, material);
        mesh->BuildFaceNormals(); mesh->VertexMove();
        entity->SetCurrentMesh(mesh); entity->SetPosition3f(0, 0, 5); entity->UpdateBox(TRUE);
        return entity;
    }
    void Child(RCK3dEntity *parent, RCK3dEntity *child) {
        ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child);
        ASSERT_TRUE(child->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
    }
    void Attach(RCK3dEntity *entity, RCKRenderContext *view = nullptr) {
        if (!view) view = Device;
        view->AddObjectWithHierarchy(entity); view->AddRemoveSequence(FALSE);
    }
    static void Indices(CKSceneGraphNode *parent) {
        for (int i = 0; i < parent->m_Children.Size(); ++i) {
            EXPECT_EQ(i, parent->m_Children[i]->m_Index);
            EXPECT_EQ(parent, parent->m_Children[i]->m_Parent);
        }
    }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            EXPECT_NEAR(expected[row][column], actual[row][column], 1e-5f);
    }
};

TEST_F(SceneGraphTraversalTest, StablePackedPrioritySortTouchesOnlyTheParsedPrefixAndControlsRealDrawOrder) {
    RCK3dEntity *parent = Entity(), *subtree = Triangle(), *first = Triangle(), *equal = Triangle();
    RCK3dEntity *grandchild = Triangle(), *hidden = Triangle();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, subtree); ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, equal); ASSERT_NE(nullptr, grandchild); ASSERT_NE(nullptr, hidden);
    Child(parent, subtree); Child(parent, first); Child(parent, equal); Child(parent, hidden); Child(subtree, grandchild);
    subtree->SetZOrder(5); grandchild->SetZOrder(100); first->SetZOrder(10); equal->SetZOrder(10);
    hidden->SetZOrder(10000); hidden->Show(CKHIDE); Attach(parent);
    CKSceneGraphNode *node = parent->m_SceneGraphNode;
    ASSERT_EQ(3, node->m_ChildToBeParsedCount); ASSERT_EQ(4, node->m_Children.Size());
    EXPECT_GT(subtree->m_SceneGraphNode->m_Priority, first->m_SceneGraphNode->m_Priority);
    EXPECT_LT(subtree->m_SceneGraphNode->m_MaxPriority, first->m_SceneGraphNode->m_MaxPriority);
    CKSceneGraphNode *suffix = node->m_Children[3];
    node->SortNodes();
    EXPECT_FALSE(node->NeedsSort());
    EXPECT_EQ(first->m_SceneGraphNode, node->m_Children[0]);
    EXPECT_EQ(equal->m_SceneGraphNode, node->m_Children[1]);
    EXPECT_EQ(subtree->m_SceneGraphNode, node->m_Children[2]);
    EXPECT_EQ(hidden->m_SceneGraphNode, suffix); EXPECT_EQ(suffix, node->m_Children[3]); Indices(node);
    TraversalTrace trace; TraversalCallback pre = {&trace, 1}, post = {&trace, 3}; Boundary.Trace = &trace;
    RCK3dEntity *expected[4] = {first, equal, subtree, grandchild};
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(expected[i]->AddPreRenderCallBack(ObserveTraversal, &pre, FALSE));
        ASSERT_TRUE(expected[i]->AddPostRenderCallBack(ObserveTraversal, &post, FALSE));
    }
    node->MarkNeedSort(); node->NoTestsTraversal(Device, CKRENDER_UPDATEEXTENTS);
    ASSERT_EQ(4, Boundary.Draws); ASSERT_EQ(12, trace.Count);
    for (int i = 0; i < 4; ++i) for (int phase = 0; phase < 3; ++phase) {
        EXPECT_EQ(expected[i]->GetID(), trace.Objects[i * 3 + phase]); EXPECT_EQ(phase + 1, trace.Phases[i * 3 + phase]);
    }
    EXPECT_EQ(4, Device->m_ObjectExtents.Size()); Indices(node);
    equal->SetZOrder(11); node->NoTestsTraversal(Device, 0);
    ASSERT_EQ(8, Boundary.Draws); EXPECT_EQ(equal->GetID(), Boundary.Objects[4]); EXPECT_EQ(first->GetID(), Boundary.Objects[5]);
    EXPECT_EQ(4, Device->m_ObjectExtents.Size()); Indices(node);
}

TEST_F(SceneGraphTraversalTest, OwnContextMaskDoesNotHideAnEligibleDescendantOrVisitAnUnrelatedContext) {
    Other = CreateView(OtherBoundary); ASSERT_NE(nullptr, Other);
    ASSERT_NE(Device->m_MaskFree, Other->m_MaskFree);
    RCK3dEntity *parent = Triangle(), *own = Triangle(), *other = Triangle(), *shared = Triangle();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, own); ASSERT_NE(nullptr, other); ASSERT_NE(nullptr, shared);
    Child(parent, own); Child(parent, other); Child(parent, shared);
    own->SetZOrder(30); other->SetZOrder(20); shared->SetZOrder(10);
    Other->AddObject(parent); Other->AddObject(other); Other->AddObject(shared); Other->AddRemoveSequence(FALSE);
    Device->AddObject(own); Device->AddObject(shared); Device->AddRemoveSequence(FALSE);
    CKSceneGraphNode *node = parent->m_SceneGraphNode;
    ASSERT_EQ(3, node->m_ChildToBeParsedCount);
    EXPECT_EQ(Other->m_MaskFree, node->m_RenderContextMask);
    EXPECT_EQ(Device->m_MaskFree | Other->m_MaskFree, node->m_EntityMask);
    other->m_SceneGraphNode->ClearTransparentFlags();
    const CKDWORD before = Device->m_SceneTraversalCalls;
    node->NoTestsTraversal(Device, 0);
    ASSERT_EQ(2, Boundary.Draws); EXPECT_EQ(own->GetID(), Boundary.Objects[0]); EXPECT_EQ(shared->GetID(), Boundary.Objects[1]);
    EXPECT_EQ(before + 3, Device->m_SceneTraversalCalls);
    EXPECT_TRUE(other->m_SceneGraphNode->IsAllOutsideFrustum());
    EXPECT_FALSE(other->m_SceneGraphNode->CheckHierarchyFrustum());
    node->NoTestsTraversal(Other, 0);
    ASSERT_EQ(3, OtherBoundary.Draws); EXPECT_EQ(parent->GetID(), OtherBoundary.Objects[0]);
    EXPECT_EQ(other->GetID(), OtherBoundary.Objects[1]); EXPECT_EQ(shared->GetID(), OtherBoundary.Objects[2]);
    EXPECT_TRUE(other->m_SceneGraphNode->CheckHierarchyFrustum()); EXPECT_FALSE(other->m_SceneGraphNode->IsAllOutsideFrustum());
    EXPECT_EQ(0, Device->m_ObjectExtents.Size()); EXPECT_EQ(0, Other->m_ObjectExtents.Size());
}

TEST_F(SceneGraphTraversalTest, TransparentMembershipDeduplicatesWhileFrameStampsControlTheActualDeferredDraw) {
    RCK3dEntity *parent = Entity(), *opaque = Triangle(), *transparent = Triangle();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, opaque); ASSERT_NE(nullptr, transparent);
    Child(parent, opaque); Child(parent, transparent); transparent->SetRenderAsTransparent(TRUE); Attach(parent);
    Manager->m_SortTransparentObjects.Value = FALSE;
    CKSceneGraphRootNode &root = Manager->m_SceneGraphRootNode;
    CKSceneGraphNode *node = transparent->m_SceneGraphNode;
    Device->m_TimeFpsCalc = 100;
    parent->m_SceneGraphNode->NoTestsTraversal(Device, CKRENDER_UPDATEEXTENTS);
    ASSERT_EQ(1, Boundary.Draws); EXPECT_EQ(opaque->GetID(), Boundary.Objects[0]);
    ASSERT_EQ(1, root.m_TransparentObjects.Size()); EXPECT_EQ(node, root.m_TransparentObjects[0].m_Node);
    EXPECT_EQ(100u, node->m_TimeFpsCalc); EXPECT_TRUE(node->IsInTransparentList());
    parent->m_SceneGraphNode->NoTestsTraversal(Device, 0);
    EXPECT_EQ(2, Boundary.Draws); ASSERT_EQ(1, root.m_TransparentObjects.Size());
    root.SortTransparentObjects(Device, CKRENDER_UPDATEEXTENTS);
    ASSERT_EQ(3, Boundary.Draws); EXPECT_EQ(transparent->GetID(), Boundary.Objects[2]);
    EXPECT_EQ(2, Device->m_ObjectExtents.Size());
    ++Device->m_TimeFpsCalc; transparent->Show(CKHIDE); Device->AddRemoveSequence(FALSE);
    parent->m_SceneGraphNode->NoTestsTraversal(Device, 0);
    ASSERT_EQ(4, Boundary.Draws); root.SortTransparentObjects(Device, 0);
    EXPECT_EQ(4, Boundary.Draws); EXPECT_EQ(0, root.m_TransparentObjects.Size()); EXPECT_FALSE(node->IsInTransparentList());
    transparent->Show(CKSHOW); Device->AddRemoveSequence(FALSE);
    parent->m_SceneGraphNode->NoTestsTraversal(Device, 0);
    ASSERT_EQ(1, root.m_TransparentObjects.Size()); EXPECT_EQ(101u, node->m_TimeFpsCalc);
    root.SortTransparentObjects(Device, 0);
    ASSERT_EQ(6, Boundary.Draws); EXPECT_EQ(transparent->GetID(), Boundary.Objects[5]);
}

TEST_F(SceneGraphTraversalTest, ClearingVisibilityRecursesOnlyTheParsedPrefixAndRetainsLiveTransparentMembership) {
    RCK3dEntity *parent = Entity(), *transparent = Triangle(), *hidden = Triangle();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, transparent); ASSERT_NE(nullptr, hidden);
    Child(parent, transparent); Child(parent, hidden); transparent->SetRenderAsTransparent(TRUE);
    hidden->Show(CKHIDE); Attach(parent);
    CKSceneGraphNode *node = parent->m_SceneGraphNode, *active = transparent->m_SceneGraphNode, *suffix = hidden->m_SceneGraphNode;
    ASSERT_EQ(1, node->m_ChildToBeParsedCount); ASSERT_EQ(suffix, node->m_Children[1]);
    Manager->m_SortTransparentObjects.Value = FALSE; Device->m_TimeFpsCalc = 55;
    node->NoTestsTraversal(Device, 0);
    ASSERT_EQ(1, Manager->m_SceneGraphRootNode.m_TransparentObjects.Size()); EXPECT_EQ(0, Boundary.Draws);
    suffix->SetAsInsideFrustum(); node->MarkNeedSort();
    const CKDWORD oldNode = node->m_Flags, oldActive = active->m_Flags, oldSuffix = suffix->m_Flags;
    node->ClearTransparentFlags();
    EXPECT_EQ((oldNode & ~CKSGN_INSIDEFRUSTUM) | CKSGN_OUTSIDEFRUSTUM, node->m_Flags);
    EXPECT_EQ((oldActive & ~CKSGN_INSIDEFRUSTUM) | CKSGN_OUTSIDEFRUSTUM, active->m_Flags);
    EXPECT_EQ(oldSuffix, suffix->m_Flags); EXPECT_TRUE(active->IsInTransparentList()); EXPECT_TRUE(node->NeedsSort());
    Manager->m_SceneGraphRootNode.SortTransparentObjects(Device, 0);
    ASSERT_EQ(1, Boundary.Draws); EXPECT_EQ(transparent->GetID(), Boundary.Objects[0]);
    EXPECT_TRUE(active->IsInTransparentList()); EXPECT_TRUE(active->CheckHierarchyFrustum());
}

TEST_F(SceneGraphTraversalTest, PlaceClipReachesTheActualMeshAndRestoresViewportAndProjectionAfterItsSubtree) {
    RCKPlace *place = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE));
    RCK3dEntity *child = Triangle(); ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child);
    Child(place, child); Attach(place);
    place->ViewportClip() = VxRect(.25f, .25f, .75f, .75f);
    const VxMatrix original = Device->GetProjectionTransformationMatrix();
    const int writes = Boundary.ViewportWrites;
    place->m_SceneGraphNode->NoTestsTraversal(Device, 0);
    ASSERT_EQ(1, Boundary.Draws); EXPECT_EQ(child->GetID(), Boundary.Objects[0]);
    EXPECT_EQ(25u, Boundary.DrawViewport[0].ViewX); EXPECT_EQ(25u, Boundary.DrawViewport[0].ViewY);
    EXPECT_EQ(50u, Boundary.DrawViewport[0].ViewWidth); EXPECT_EQ(50u, Boundary.DrawViewport[0].ViewHeight);
    VxMatrix clipped = original; for (int row = 0; row < 4; ++row) { clipped[row][0] *= 2; clipped[row][1] *= 2; }
    Matrix(Boundary.DrawProjection[0], clipped); Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], original);
    Matrix(Device->GetProjectionTransformationMatrix(), original);
    EXPECT_EQ(0u, Boundary.Viewport.ViewX); EXPECT_EQ(0u, Boundary.Viewport.ViewY);
    EXPECT_EQ(100u, Boundary.Viewport.ViewWidth); EXPECT_EQ(100u, Boundary.Viewport.ViewHeight);
    EXPECT_EQ(writes + 2, Boundary.ViewportWrites);
    const VxRect skipped[3] = {VxRect(0, 0, 0, 0), VxRect(0, 0, 1, 1), VxRect(.2f, .2f, .2f, .8f)};
    for (int i = 0; i < 3; ++i) {
        place->ViewportClip() = skipped[i]; const int count = Boundary.ViewportWrites;
        place->m_SceneGraphNode->NoTestsTraversal(Device, 0);
        EXPECT_EQ(count, Boundary.ViewportWrites); Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], original);
    }
    EXPECT_EQ(4, Boundary.Draws);
}

TEST_F(SceneGraphTraversalTest, CharacterTraversalMarksTheEnteredCharacterAndForwardsExtentsPolicyToItsMesh) {
    RCK3dEntity *character = Triangle(CKCID_CHARACTER); ASSERT_NE(nullptr, character); Attach(character);
    EXPECT_EQ(0u, character->GetMoveableFlags() & VX_MOVEABLE_CHARACTERRENDERED);
    character->m_SceneGraphNode->NoTestsTraversal(Device, CKRENDER_UPDATEEXTENTS);
    ASSERT_EQ(1, Boundary.Draws); EXPECT_EQ(character->GetID(), Boundary.Objects[0]);
    EXPECT_NE(0u, character->GetMoveableFlags() & VX_MOVEABLE_CHARACTERRENDERED);
    ASSERT_EQ(1, Device->m_ObjectExtents.Size());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(character), Device->m_ObjectExtents[0].m_Entity);
    character->ModifyMoveableFlags(0, VX_MOVEABLE_CHARACTERRENDERED);
    character->Show(CKHIDE); Device->AddRemoveSequence(FALSE);
    character->m_SceneGraphNode->NoTestsTraversal(Device, 0);
    EXPECT_EQ(1, Boundary.Draws); EXPECT_NE(0u, character->GetMoveableFlags() & VX_MOVEABLE_CHARACTERRENDERED);
    EXPECT_EQ(1, Device->m_ObjectExtents.Size());
}
}
