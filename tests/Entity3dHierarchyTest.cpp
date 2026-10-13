#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKPlace.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
CK3dEntity *API(CK3dEntity *entity) { return entity; }

class Entity3dHierarchyTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    CKPlace *Place() { return static_cast<CKPlace *>(Context->CreateObject(CKCID_PLACE)); }
    static void Position(RCK3dEntity *entity, float x, float y, float z) {
        VxVector value(x, y, z); entity->SetPosition(&value);
    }
    static void ExpectPosition(RCK3dEntity *entity, float x, float y, float z, RCK3dEntity *reference = nullptr) {
        VxVector value; entity->GetPosition(&value, API(reference));
        EXPECT_FLOAT_EQ(x, value.x); EXPECT_FLOAT_EQ(y, value.y); EXPECT_FLOAT_EQ(z, value.z);
    }
};

TEST_F(Entity3dHierarchyTest, ReparentKeepWorldUpdatesBothChildListsLocalMatrixAndSceneGraphParent) {
    RCK3dEntity *first = Entity(), *second = Entity(), *child = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, child);
    Position(first, 10, 20, 30); Position(second, -5, 4, 7); Position(child, 13, 25, 39);
    ASSERT_TRUE(first->AddChild(API(child), TRUE));
    EXPECT_EQ(API(first), child->GetParent()); EXPECT_EQ(1, first->GetChildrenCount());
    EXPECT_EQ(API(child), first->GetChild(0)); ExpectPosition(child, 13, 25, 39);
    ExpectPosition(child, 3, 5, 9, first);
    EXPECT_EQ(first->m_SceneGraphNode, child->m_SceneGraphNode->m_Parent);
    ASSERT_TRUE(child->SetParent(API(second), TRUE));
    EXPECT_EQ(0, first->GetChildrenCount()); EXPECT_EQ(1, second->GetChildrenCount());
    ExpectPosition(child, 13, 25, 39); ExpectPosition(child, 18, 21, 32, second);
    EXPECT_EQ(second->m_SceneGraphNode, child->m_SceneGraphNode->m_Parent);
    EXPECT_TRUE(second->AddChild(API(child), TRUE)); EXPECT_EQ(1, second->GetChildrenCount());
    ASSERT_TRUE(second->RemoveChild(API(child)));
    EXPECT_EQ(nullptr, child->GetParent()); EXPECT_EQ(0, second->GetChildrenCount());
    ExpectPosition(child, 13, 25, 39);
    EXPECT_EQ(static_cast<RCKRenderManager *>(Context->GetRenderManager())->GetRootNode(),
              child->m_SceneGraphNode->m_Parent);
}

TEST_F(Entity3dHierarchyTest, ReparentKeepLocalRecomputesWorldAndRejectsCyclesWithoutMutation) {
    RCK3dEntity *first = Entity(), *second = Entity(), *child = Entity(), *grandchild = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, grandchild);
    Position(first, 10, 0, 0); Position(second, 100, 0, 0); Position(child, 2, 0, 0);
    ASSERT_TRUE(child->SetParent(API(first), FALSE)); ExpectPosition(child, 12, 0, 0);
    ASSERT_TRUE(child->SetParent(API(second), FALSE)); ExpectPosition(child, 102, 0, 0);
    ASSERT_TRUE(child->AddChild(API(grandchild), TRUE));
    EXPECT_FALSE(second->SetParent(API(grandchild), TRUE));
    EXPECT_FALSE(child->SetParent(API(child), TRUE));
    EXPECT_EQ(nullptr, second->GetParent()); EXPECT_EQ(API(second), child->GetParent());
    EXPECT_EQ(API(child), grandchild->GetParent()); EXPECT_EQ(1, second->GetChildrenCount());
    EXPECT_FALSE(second->AddChild(nullptr)); EXPECT_FALSE(second->RemoveChild(nullptr));
    ExpectPosition(child, 102, 0, 0);
}

TEST_F(Entity3dHierarchyTest, AddChildrenKeepsSelectedSubtreesAndClearsTemporaryMarkers) {
    RCK3dEntity *target = Entity(), *root = Entity(), *descendant = Entity(), *other = Entity();
    ASSERT_NE(nullptr, target); ASSERT_NE(nullptr, root); ASSERT_NE(nullptr, descendant); ASSERT_NE(nullptr, other);
    ASSERT_TRUE(root->AddChild(API(descendant), TRUE));
    CKObject *plain = Context->CreateObject(CKCID_OBJECT); ASSERT_NE(nullptr, plain);
    XObjectPointerArray selected; selected.PushBack(descendant); selected.PushBack(root);
    selected.PushBack(other); selected.PushBack(plain); selected.PushBack(nullptr);
    EXPECT_TRUE(target->AddChildren(selected, TRUE));
    EXPECT_EQ(2, target->GetChildrenCount()); EXPECT_EQ(API(target), root->GetParent());
    EXPECT_EQ(API(root), descendant->GetParent()); EXPECT_EQ(API(target), other->GetParent());
    for (int index = 0; index < selected.Size(); ++index) {
        if (selected[index]) EXPECT_EQ(0u, selected[index]->GetObjectFlags() & CK_OBJECT_TEMPMARKER);
    }
    XObjectPointerArray empty; EXPECT_FALSE(target->AddChildren(empty, TRUE));
}

TEST_F(Entity3dHierarchyTest, HierarchyParserVisitsDepthFirstWithinRootAndStopsBeforeExternalSibling) {
    RCK3dEntity *outside = Entity(), *root = Entity(), *left = Entity(), *leaf = Entity(), *right = Entity(), *sibling = Entity();
    ASSERT_NE(nullptr, outside); ASSERT_NE(nullptr, root); ASSERT_NE(nullptr, left);
    ASSERT_NE(nullptr, leaf); ASSERT_NE(nullptr, right); ASSERT_NE(nullptr, sibling);
    outside->AddChild(API(root), TRUE); outside->AddChild(API(sibling), TRUE);
    root->AddChild(API(left), TRUE); left->AddChild(API(leaf), TRUE); root->AddChild(API(right), TRUE);
    EXPECT_EQ(API(left), root->HierarchyParser(nullptr));
    EXPECT_EQ(API(leaf), root->HierarchyParser(API(left)));
    EXPECT_EQ(API(right), root->HierarchyParser(API(leaf)));
    EXPECT_EQ(nullptr, root->HierarchyParser(API(right)));
    EXPECT_EQ(nullptr, sibling->HierarchyParser(nullptr));
}

TEST_F(Entity3dHierarchyTest, OrderedAndUnorderedHierarchyComparisonDistinguishPermutationAndMultiplicity) {
    RCK3dEntity *first = Entity(), *second = Entity(), *aLeaf = Entity(), *aBranch = Entity(), *aGrand = Entity();
    RCK3dEntity *bLeaf = Entity(), *bBranch = Entity(), *bGrand = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, aLeaf); ASSERT_NE(nullptr, aBranch);
    ASSERT_NE(nullptr, aGrand); ASSERT_NE(nullptr, bLeaf); ASSERT_NE(nullptr, bBranch); ASSERT_NE(nullptr, bGrand);
    first->AddChild(API(aLeaf)); first->AddChild(API(aBranch)); aBranch->AddChild(API(aGrand));
    second->AddChild(API(bBranch)); second->AddChild(API(bLeaf)); bBranch->AddChild(API(bGrand));
    EXPECT_FALSE(first->CheckIfSameKindOfHierarchy(API(second), TRUE));
    EXPECT_TRUE(first->CheckIfSameKindOfHierarchy(API(second), FALSE));
    EXPECT_TRUE(second->CheckIfSameKindOfHierarchy(API(first), FALSE));
    EXPECT_FALSE(first->CheckIfSameKindOfHierarchy(nullptr, FALSE));
    ASSERT_TRUE(aBranch->RemoveChild(API(aGrand)));
    // Two leaves cannot match a leaf plus a branch. Reusing the one matching
    // leaf gives an asymmetric answer in the native implementation.
    EXPECT_FALSE(first->CheckIfSameKindOfHierarchy(API(second), FALSE));
    EXPECT_FALSE(second->CheckIfSameKindOfHierarchy(API(first), FALSE));
}

TEST_F(Entity3dHierarchyTest, PlaceReferencePropagatesToDescendantsAndRejectsNestedPlaces) {
    CKPlace *first = Place(), *second = Place(); RCK3dEntity *child = Entity(), *grandchild = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, grandchild);
    child->AddChild(API(grandchild), TRUE); child->SetParent(API(first), TRUE);
    EXPECT_EQ(first, child->GetReferencePlace()); EXPECT_EQ(first, grandchild->GetReferencePlace());
    EXPECT_FALSE(first->SetParent(API(second), TRUE)); EXPECT_EQ(nullptr, first->GetParent());
    child->SetParent(API(second), TRUE);
    EXPECT_EQ(second, child->GetReferencePlace()); EXPECT_EQ(second, grandchild->GetReferencePlace());
    child->SetParent(nullptr, TRUE);
    EXPECT_EQ(nullptr, child->GetReferencePlace()); EXPECT_EQ(nullptr, grandchild->GetReferencePlace());
    child->UpdatePlace(first->GetID());
    EXPECT_EQ(first, child->GetReferencePlace()); EXPECT_EQ(first, grandchild->GetReferencePlace());
    child->UpdatePlace(0);
    EXPECT_EQ(nullptr, child->GetReferencePlace()); EXPECT_EQ(nullptr, grandchild->GetReferencePlace());
}

TEST_F(Entity3dHierarchyTest, HierarchicalHideAffectsDescendantsWhileOrdinaryHideKeepsTheirVisibility) {
    RCK3dEntity *root = Entity(), *child = Entity(), *grandchild = Entity();
    ASSERT_NE(nullptr, root); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, grandchild);
    root->AddChild(API(child)); child->AddChild(API(grandchild));
    root->Show(CKHIDE); EXPECT_FALSE(root->IsVisible());
    EXPECT_TRUE(child->IsVisible()); EXPECT_FALSE(child->IsHiddenByParent());
    root->Show(CKHIERARCHICALHIDE); EXPECT_TRUE(child->IsHiddenByParent());
    EXPECT_TRUE(grandchild->IsHiddenByParent()); EXPECT_FALSE(child->IsVisible());
    EXPECT_FALSE(grandchild->IsVisible());
    root->Show(CKSHOW); EXPECT_TRUE(child->IsVisible()); EXPECT_TRUE(grandchild->IsVisible());
    child->Show(CKHIERARCHICALHIDE); EXPECT_FALSE(grandchild->IsVisible());
    EXPECT_FALSE(child->IsHiddenByParent()); root->Show(CKHIERARCHICALHIDE);
    child->Show(CKSHOW); EXPECT_FALSE(child->IsVisible());
    root->Show(CKSHOW); EXPECT_TRUE(child->IsVisible()); EXPECT_TRUE(grandchild->IsVisible());
}

TEST_F(Entity3dHierarchyTest, ContextDeletionPreservesUnselectedChildrenWorldPositionAndGraphRootMembership) {
    RCK3dEntity *parent = Entity(), *doomed = Entity(), *survivor = Entity(), *grandchild = Entity();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, doomed); ASSERT_NE(nullptr, survivor); ASSERT_NE(nullptr, grandchild);
    Position(parent, 10, 0, 0); Position(doomed, 20, 0, 0); Position(survivor, 23, 4, 5);
    parent->AddChild(API(doomed), TRUE); doomed->AddChild(API(survivor), TRUE); survivor->AddChild(API(grandchild), TRUE);
    const CK_ID doomedID = doomed->GetID(), survivorID = survivor->GetID();
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(doomed, 0, &dependencies));
    EXPECT_EQ(nullptr, Context->GetObject(doomedID)); EXPECT_EQ(survivor, Context->GetObject(survivorID));
    EXPECT_EQ(0, parent->GetChildrenCount()); EXPECT_EQ(nullptr, survivor->GetParent());
    EXPECT_EQ(API(survivor), grandchild->GetParent()); ExpectPosition(survivor, 23, 4, 5);
    EXPECT_EQ(static_cast<RCKRenderManager *>(Context->GetRenderManager())->GetRootNode(),
              survivor->m_SceneGraphNode->m_Parent);
}

} // namespace
