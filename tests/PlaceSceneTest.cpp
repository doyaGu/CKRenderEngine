#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKPlace.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKMesh.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class HierarchyPlace : public RCKPlace {
public:
    explicit HierarchyPlace(CKContext *context) : RCKPlace(context), Calls(0), Last(FALSE) {}
    const VxBbox &GetHierarchicalBox(CKBOOL local) override { ++Calls; Last = local; return RCK3dEntity::GetHierarchicalBox(local); }
    int Calls; CKBOOL Last;
};
class SceneRemovalEntity : public RCK3dEntity {
public:
    explicit SceneRemovalEntity(CKContext *context)
        : RCK3dEntity(context), Calls(0), LastScene(nullptr), LastDependencies(FALSE) {}
    void RemoveFromScene(CKScene *scene, CKBOOL dependencies) override {
        ++Calls; LastScene = scene; LastDependencies = dependencies;
        RCK3dEntity::RemoveFromScene(scene, dependencies);
    }
    int Calls; CKScene *LastScene; CKBOOL LastDependencies;
};
class PlaceSceneTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *API(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    RCKPlace *Place() { return static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE)); }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    CKScene *Scene() { return static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); }
    static void UnitBox(RCK3dEntity &entity) { VxBbox box; box.Min.Set(-1, -1, -1); box.Max.Set(1, 1, 1); ASSERT_TRUE(entity.SetBoundingBox(&box, TRUE)); }
    static void Box(const VxBbox &box, float lo, float hi) {
        EXPECT_FLOAT_EQ(lo, box.Min.x); EXPECT_FLOAT_EQ(hi, box.Max.x);
        EXPECT_FLOAT_EQ(-1, box.Min.y); EXPECT_FLOAT_EQ(-1, box.Min.z);
        EXPECT_FLOAT_EQ(1, box.Max.y); EXPECT_FLOAT_EQ(1, box.Max.z);
    }
};
}
TEST_F(PlaceSceneTest, BoundingBoxDispatchesToVirtualHierarchyAndKeepsLocalFlag) {
    HierarchyPlace place(Context); UnitBox(place); place.SetPosition3f(10, 0, 0);
    CKPlace *api = reinterpret_cast<CKPlace *>(&place); Box(api->GetBoundingBox(FALSE), 9, 11);
    EXPECT_EQ(1, place.Calls); EXPECT_EQ(FALSE, place.Last); Box(api->GetBoundingBox(TRUE), -1, 1);
    EXPECT_EQ(2, place.Calls); EXPECT_EQ(TRUE, place.Last);
}
TEST_F(PlaceSceneTest, BoundingBoxMergesActualSceneGraphChildrenAndInvalidatesAfterMovement) {
    new RCKRenderManager(Context); RCKPlace *place = Place(); RCK3dEntity *child = Entity();
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child); UnitBox(*place); UnitBox(*child);
    place->SetPosition3f(10, 0, 0); child->SetPosition3f(20, 0, 0); ASSERT_TRUE(child->SetParent(API(*place), TRUE));
    ASSERT_NE(nullptr, place->m_SceneGraphNode); Box(place->GetBoundingBox(FALSE), 9, 21); Box(place->GetBoundingBox(TRUE), -1, 11);
    child->SetPosition3f(30, 0, 0); Box(place->GetBoundingBox(FALSE), 9, 31); Box(place->GetBoundingBox(TRUE), -1, 21);
}
TEST_F(PlaceSceneTest, NullSceneAddAndRemoveLeaveExistingMembershipAndHierarchy) {
    RCKPlace *place = Place(); RCK3dEntity *child = Entity(); CKScene *scene = Scene();
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, scene);
    ASSERT_TRUE(child->SetParent(API(*place), TRUE)); place->AddToScene(scene, TRUE);
    place->AddToScene(nullptr, TRUE); place->RemoveFromScene(nullptr, TRUE);
    EXPECT_TRUE(place->IsInScene(scene)); EXPECT_TRUE(child->IsInScene(scene)); EXPECT_EQ(API(*place), child->GetParent());
}
TEST_F(PlaceSceneTest, AddWithoutDependenciesDoesNotAddChildOrMesh) {
    RCKPlace *place = Place(); RCK3dEntity *child = Entity(); CKScene *scene = Scene();
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, mesh);
    ASSERT_TRUE(child->SetParent(API(*place), TRUE)); place->SetCurrentMesh(mesh); place->AddToScene(scene, FALSE);
    EXPECT_TRUE(place->IsInScene(scene)); EXPECT_FALSE(child->IsInScene(scene)); EXPECT_FALSE(mesh->IsInScene(scene));
}
TEST_F(PlaceSceneTest, AddWithDependenciesAddsChildDescendantsAndMeshWithoutDuplicates) {
    RCKPlace *place = Place(); RCK3dEntity *child = Entity(), *grandchild = Entity(); CKScene *scene = Scene();
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, grandchild); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, mesh);
    ASSERT_TRUE(child->SetParent(API(*place), TRUE)); ASSERT_TRUE(grandchild->SetParent(API(*child), TRUE)); place->SetCurrentMesh(mesh);
    place->AddToScene(scene, TRUE); place->AddToScene(scene, TRUE);
    EXPECT_TRUE(place->IsInScene(scene)); EXPECT_TRUE(child->IsInScene(scene)); EXPECT_TRUE(grandchild->IsInScene(scene)); EXPECT_TRUE(mesh->IsInScene(scene));
    EXPECT_EQ(1, place->GetSceneInCount()); EXPECT_EQ(1, child->GetSceneInCount()); EXPECT_EQ(1, grandchild->GetSceneInCount());
}
TEST_F(PlaceSceneTest, RemoveWithoutDependenciesRemovesDirectChildButLeavesOrdinaryGrandchildAndMesh) {
    RCKPlace *place = Place(); RCK3dEntity *child = Entity(), *grandchild = Entity(); CKScene *scene = Scene();
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, grandchild); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, mesh);
    ASSERT_TRUE(child->SetParent(API(*place), TRUE)); ASSERT_TRUE(grandchild->SetParent(API(*child), TRUE)); place->SetCurrentMesh(mesh);
    place->AddToScene(scene, TRUE); place->RemoveFromScene(scene, FALSE);
    EXPECT_FALSE(place->IsInScene(scene)); EXPECT_FALSE(child->IsInScene(scene)); EXPECT_TRUE(grandchild->IsInScene(scene)); EXPECT_TRUE(mesh->IsInScene(scene));
    EXPECT_EQ(API(*place), child->GetParent()); EXPECT_EQ(API(*child), grandchild->GetParent());
}
TEST_F(PlaceSceneTest, RemoveWithDependenciesRemovesDirectChildResourcesButLeavesOrdinaryGrandchild) {
    RCKPlace *place = Place(); RCK3dEntity *child = Entity(), *grandchild = Entity(); CKScene *scene = Scene();
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, grandchild); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, mesh);
    ASSERT_TRUE(child->SetParent(API(*place), TRUE)); ASSERT_TRUE(grandchild->SetParent(API(*child), TRUE)); child->SetCurrentMesh(mesh);
    place->AddToScene(scene, TRUE); place->RemoveFromScene(scene, TRUE);
    EXPECT_FALSE(place->IsInScene(scene)); EXPECT_FALSE(child->IsInScene(scene)); EXPECT_FALSE(mesh->IsInScene(scene)); EXPECT_TRUE(grandchild->IsInScene(scene));
}
TEST_F(PlaceSceneTest, RemoveUsesLegalEntityChildVirtualOverrideAndForwardsDependencyFlag) {
    RCKPlace *place = Place(); SceneRemovalEntity child(Context); RCK3dEntity *leaf = Entity(); CKScene *scene = Scene();
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, leaf); ASSERT_NE(nullptr, scene);
    ASSERT_TRUE(child.SetParent(API(*place), TRUE)); ASSERT_TRUE(leaf->SetParent(API(child), TRUE));
    place->AddToScene(scene, TRUE); place->RemoveFromScene(scene, FALSE);
    EXPECT_EQ(1, child.Calls); EXPECT_EQ(scene, child.LastScene); EXPECT_EQ(FALSE, child.LastDependencies);
    EXPECT_FALSE(place->IsInScene(scene)); EXPECT_FALSE(child.IsInScene(scene)); EXPECT_TRUE(leaf->IsInScene(scene));
    place->AddToScene(scene, TRUE); place->RemoveFromScene(scene, TRUE);
    EXPECT_EQ(2, child.Calls); EXPECT_EQ(scene, child.LastScene); EXPECT_EQ(TRUE, child.LastDependencies);
    EXPECT_FALSE(place->IsInScene(scene)); EXPECT_FALSE(child.IsInScene(scene)); EXPECT_TRUE(leaf->IsInScene(scene));
    EXPECT_EQ(API(child), leaf->GetParent()); EXPECT_EQ(reinterpret_cast<CKPlace *>(place), leaf->GetReferencePlace());
    ASSERT_TRUE(leaf->SetParent(nullptr, TRUE));
}
TEST_F(PlaceSceneTest, RemovingFromOneScenePreservesOtherSceneMembership) {
    RCKPlace *place = Place(); RCK3dEntity *child = Entity(); CKScene *first = Scene(), *second = Scene();
    ASSERT_NE(nullptr, place); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    ASSERT_TRUE(child->SetParent(API(*place), TRUE)); place->AddToScene(first, TRUE); place->AddToScene(second, TRUE);
    place->RemoveFromScene(first, FALSE); EXPECT_FALSE(place->IsInScene(first)); EXPECT_FALSE(child->IsInScene(first));
    EXPECT_TRUE(place->IsInScene(second)); EXPECT_TRUE(child->IsInScene(second));
}
