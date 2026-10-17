#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKSceneGraph.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
CK3dEntity *GraphAPI(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }

class SceneGraphLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *View = nullptr;
    XClassArray<CKRasterizerInfo> Saved;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        Saved = g_RasterizersInfo;
        g_RasterizersInfo.Clear();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        View = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        ASSERT_NE(nullptr, View);
        View->DetachAll();
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        g_RasterizersInfo = Saved;
        CKShutdown();
    }

    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }

    RCKRenderContext *OtherView() {
        RCKRenderContext *other = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        if (other)
            other->DetachAll();
        return other;
    }

    static void ExpectPrefix(CKSceneGraphNode *parent) {
        ASSERT_NE(nullptr, parent);
        ASSERT_GE(parent->m_ChildToBeParsedCount, 0);
        ASSERT_LE(parent->m_ChildToBeParsedCount, parent->m_Children.Size());
        for (int i = 0; i < parent->m_Children.Size(); ++i) {
            SCOPED_TRACE(i);
            CKSceneGraphNode *child = parent->m_Children[i];
            ASSERT_NE(nullptr, child);
            EXPECT_EQ(parent, child->m_Parent);
            EXPECT_EQ(i, child->m_Index);
            EXPECT_EQ(i < parent->m_ChildToBeParsedCount, child->IsToBeParsed() != FALSE);
        }
    }
};
}

TEST_F(SceneGraphLifecycleTest, ConstructorInitializesAnIndependentBorrowedEntityAndAnEmptyRoot) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CKSceneGraphNode node(entity), root;
    EXPECT_EQ(entity, node.m_Entity);
    EXPECT_EQ(nullptr, root.m_Entity);
    CKSceneGraphNode *nodes[] = {&node, &root};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(0u, nodes[i]->m_TimeFpsCalc);
        EXPECT_EQ(0u, nodes[i]->m_Flags);
        EXPECT_EQ(0, nodes[i]->m_Index);
        EXPECT_FALSE(nodes[i]->m_Bbox.IsValid());
        EXPECT_EQ(10000, nodes[i]->m_Priority);
        EXPECT_EQ(10000, nodes[i]->m_MaxPriority);
        EXPECT_EQ(0u, nodes[i]->m_RenderContextMask);
        EXPECT_EQ(0u, nodes[i]->m_EntityMask);
        EXPECT_EQ(nullptr, nodes[i]->m_Parent);
        EXPECT_EQ(0, nodes[i]->m_Children.Size());
        EXPECT_EQ(0, nodes[i]->m_ChildToBeParsedCount);
    }
    EXPECT_EQ(entity, Context->GetObject(entity->GetID()));
    EXPECT_NE(&node, entity->m_SceneGraphNode);
}

TEST_F(SceneGraphLifecycleTest, DestroyingANodeDetachesItsBorrowedChildrenWithoutDestroyingTheirEntities) {
    RCK3dEntity *entity = Entity(), *first = Entity(), *second = Entity();
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    CKSceneGraphNode left(first), right(second);
    {
        CKSceneGraphNode owner(entity);
        owner.AddNode(&left);
        owner.AddNode(&right);
        ASSERT_EQ(2, owner.m_Children.Size());
        ASSERT_EQ(&owner, left.m_Parent); ASSERT_EQ(&owner, right.m_Parent);
    }
    EXPECT_EQ(nullptr, left.m_Parent); EXPECT_EQ(nullptr, right.m_Parent);
    EXPECT_EQ(0, left.m_Index); EXPECT_EQ(0, right.m_Index);
    EXPECT_EQ(entity, Context->GetObject(entity->GetID()));
    EXPECT_EQ(first, Context->GetObject(first->GetID()));
    EXPECT_EQ(second, Context->GetObject(second->GetID()));
    EXPECT_NE(nullptr, first->m_SceneGraphNode);
    EXPECT_NE(nullptr, second->m_SceneGraphNode);
}

TEST_F(SceneGraphLifecycleTest, RealReparentingMaintainsIndicesAndTheParsedPrefixForRemovedMiddleAndSuffixChildren) {
    RCK3dEntity *first = Entity(), *second = Entity(), *a = Entity(), *b = Entity(), *c = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b); ASSERT_NE(nullptr, c);
    ASSERT_TRUE(a->SetParent(GraphAPI(first)));
    ASSERT_TRUE(b->SetParent(GraphAPI(first)));
    ASSERT_TRUE(c->SetParent(GraphAPI(first)));
    View->AddObject(first); View->AddObject(second);
    View->AddObject(a); View->AddObject(b);
    ASSERT_EQ(3, first->m_SceneGraphNode->m_Children.Size());
    ASSERT_EQ(2, first->m_SceneGraphNode->m_ChildToBeParsedCount);
    ExpectPrefix(first->m_SceneGraphNode);
    ASSERT_TRUE(b->SetParent(GraphAPI(second)));
    EXPECT_EQ(1, first->m_SceneGraphNode->m_ChildToBeParsedCount);
    ExpectPrefix(first->m_SceneGraphNode); ExpectPrefix(second->m_SceneGraphNode);
    EXPECT_EQ(GraphAPI(second), b->GetParent());
    ASSERT_TRUE(c->SetParent(GraphAPI(second)));
    EXPECT_EQ(1, first->m_SceneGraphNode->m_Children.Size());
    EXPECT_EQ(1, first->m_SceneGraphNode->m_ChildToBeParsedCount);
    ExpectPrefix(first->m_SceneGraphNode); ExpectPrefix(second->m_SceneGraphNode);
    ASSERT_TRUE(a->SetParent(nullptr));
    EXPECT_EQ(0, first->m_SceneGraphNode->m_Children.Size());
    EXPECT_EQ(0, first->m_SceneGraphNode->m_ChildToBeParsedCount);
    EXPECT_EQ(Manager->GetRootNode(), a->m_SceneGraphNode->m_Parent);
    ExpectPrefix(Manager->GetRootNode());
    EXPECT_TRUE(a->IsInRenderContext(View));
    EXPECT_TRUE(b->IsInRenderContext(View));
    EXPECT_FALSE(c->IsInRenderContext(View));
}

TEST_F(SceneGraphLifecycleTest, VisibilityChangesMoveOnlyEligibleChildrenAndPropagateThroughHiddenAncestors) {
    RCK3dEntity *parent = Entity(), *left = Entity(), *right = Entity();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, left); ASSERT_NE(nullptr, right);
    ASSERT_TRUE(left->SetParent(GraphAPI(parent)));
    ASSERT_TRUE(right->SetParent(GraphAPI(parent)));
    View->AddObject(parent); View->AddObject(left); View->AddObject(right);
    parent->Show(CKHIDE);
    EXPECT_TRUE(parent->m_SceneGraphNode->IsToBeParsed());
    left->Show(CKHIDE);
    EXPECT_EQ(1, parent->m_SceneGraphNode->m_ChildToBeParsedCount);
    EXPECT_EQ(right->m_SceneGraphNode, parent->m_SceneGraphNode->m_Children[0]);
    ExpectPrefix(parent->m_SceneGraphNode); ExpectPrefix(Manager->GetRootNode());
    right->Show(CKHIDE);
    EXPECT_EQ(0, parent->m_SceneGraphNode->m_ChildToBeParsedCount);
    EXPECT_FALSE(parent->m_SceneGraphNode->IsToBeParsed());
    ExpectPrefix(parent->m_SceneGraphNode); ExpectPrefix(Manager->GetRootNode());
    right->Show(CKSHOW);
    EXPECT_TRUE(parent->m_SceneGraphNode->IsToBeParsed());
    parent->Show(CKHIERARCHICALHIDE);
    EXPECT_FALSE(parent->m_SceneGraphNode->IsToBeParsed());
    EXPECT_TRUE(right->m_SceneGraphNode->IsToBeParsed());
    ExpectPrefix(parent->m_SceneGraphNode); ExpectPrefix(Manager->GetRootNode());
    parent->Show(CKSHOW);
    EXPECT_TRUE(parent->m_SceneGraphNode->IsToBeParsed());
    ExpectPrefix(Manager->GetRootNode());
}

TEST_F(SceneGraphLifecycleTest, RemovingTheLastEligibleChildReconcilesAHiddenAncestorEvenWhenItsOwnContextMaskSurvives) {
    RCK3dEntity *hidden = Entity(), *destination = Entity(), *child = Entity();
    ASSERT_NE(nullptr, hidden); ASSERT_NE(nullptr, destination); ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(GraphAPI(hidden)));
    View->AddObject(hidden); View->AddObject(destination); View->AddObject(child);
    hidden->Show(CKHIDE);
    CKSceneGraphNode *parent = hidden->m_SceneGraphNode;
    ASSERT_EQ(1, parent->m_ChildToBeParsedCount);
    ASSERT_TRUE(parent->IsToBeParsed());
    ASSERT_LT(parent->m_Index, Manager->GetRootNode()->m_ChildToBeParsedCount);
    const CKDWORD mask = parent->m_EntityMask;
    ASSERT_TRUE(child->SetParent(GraphAPI(destination)));
    EXPECT_EQ(mask, parent->m_EntityMask);
    EXPECT_EQ(View->m_MaskFree, parent->m_RenderContextMask);
    EXPECT_EQ(0, parent->m_ChildToBeParsedCount);
    EXPECT_FALSE(parent->IsToBeParsed());
    EXPECT_GE(parent->m_Index, Manager->GetRootNode()->m_ChildToBeParsedCount);
    ExpectPrefix(Manager->GetRootNode());
    ExpectPrefix(destination->m_SceneGraphNode);
    EXPECT_TRUE(hidden->IsInRenderContext(View));
    EXPECT_TRUE(child->IsInRenderContext(View));
}

TEST_F(SceneGraphLifecycleTest, MultipleContextMasksAggregateDescendantsWhilePreservingTheParentsOwnMembership) {
    RCKRenderContext *other = OtherView();
    RCK3dEntity *grandparent = Entity(), *parent = Entity(), *leaf = Entity();
    ASSERT_NE(nullptr, other); ASSERT_NE(nullptr, grandparent);
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, leaf);
    ASSERT_TRUE(parent->SetParent(GraphAPI(grandparent)));
    ASSERT_TRUE(leaf->SetParent(GraphAPI(parent)));
    View->AddObject(parent);
    other->AddObject(leaf);
    const CKDWORD both = View->m_MaskFree | other->m_MaskFree;
    EXPECT_EQ(View->m_MaskFree, parent->m_SceneGraphNode->m_RenderContextMask);
    EXPECT_EQ(both, parent->m_SceneGraphNode->m_EntityMask);
    EXPECT_EQ(0u, grandparent->m_SceneGraphNode->m_RenderContextMask);
    EXPECT_EQ(both, grandparent->m_SceneGraphNode->m_EntityMask);
    View->AddObject(leaf);
    other->RemoveObject(leaf);
    EXPECT_EQ(View->m_MaskFree, leaf->m_SceneGraphNode->m_EntityMask);
    EXPECT_EQ(View->m_MaskFree, parent->m_SceneGraphNode->m_EntityMask);
    EXPECT_EQ(View->m_MaskFree, grandparent->m_SceneGraphNode->m_EntityMask);
    View->RemoveObject(parent);
    EXPECT_EQ(0u, parent->m_SceneGraphNode->m_RenderContextMask);
    EXPECT_EQ(View->m_MaskFree, parent->m_SceneGraphNode->m_EntityMask);
    View->RemoveObject(leaf);
    EXPECT_EQ(0u, parent->m_SceneGraphNode->m_EntityMask);
    EXPECT_EQ(0u, grandparent->m_SceneGraphNode->m_EntityMask);
    ExpectPrefix(parent->m_SceneGraphNode); ExpectPrefix(grandparent->m_SceneGraphNode);
    ExpectPrefix(Manager->GetRootNode());
}

TEST_F(SceneGraphLifecycleTest, EndingRealMaskTransactionsRebuildsNestedEligibilityAndPartitionsTheWholeChildArray) {
    RCKRenderContext *other = OtherView();
    RCK3dEntity *parent = Entity(), *a = Entity(), *b = Entity(), *c = Entity(), *d = Entity(), *e = Entity();
    RCK3dEntity *bChild = Entity(), *cChild = Entity();
    ASSERT_NE(nullptr, other); ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b); ASSERT_NE(nullptr, c);
    ASSERT_NE(nullptr, d); ASSERT_NE(nullptr, e); ASSERT_NE(nullptr, bChild); ASSERT_NE(nullptr, cChild);
    RCK3dEntity *children[] = {a, b, c, d, e};
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(children[i]->SetParent(GraphAPI(parent)));
    ASSERT_TRUE(bChild->SetParent(GraphAPI(b)));
    ASSERT_TRUE(cChild->SetParent(GraphAPI(c)));
    parent->Show(CKHIDE); b->Show(CKHIERARCHICALHIDE); c->Show(CKHIDE);
    View->AddRemoveSequence(TRUE); other->AddRemoveSequence(TRUE);
    View->AddObject(parent); View->AddObject(a); View->AddObject(c); View->AddObject(bChild);
    other->AddObject(b); other->AddObject(e); other->AddObject(cChild);
    EXPECT_EQ(0u, parent->m_SceneGraphNode->m_EntityMask);
    View->AddRemoveSequence(FALSE); other->AddRemoveSequence(FALSE);
    EXPECT_EQ(View->m_MaskFree | other->m_MaskFree, parent->m_SceneGraphNode->m_EntityMask);
    ASSERT_EQ(5, parent->m_SceneGraphNode->m_Children.Size());
    EXPECT_EQ(3, parent->m_SceneGraphNode->m_ChildToBeParsedCount);
    EXPECT_TRUE(a->m_SceneGraphNode->IsToBeParsed());
    EXPECT_FALSE(b->m_SceneGraphNode->IsToBeParsed());
    EXPECT_TRUE(c->m_SceneGraphNode->IsToBeParsed());
    EXPECT_FALSE(d->m_SceneGraphNode->IsToBeParsed());
    EXPECT_TRUE(e->m_SceneGraphNode->IsToBeParsed());
    ExpectPrefix(parent->m_SceneGraphNode); ExpectPrefix(b->m_SceneGraphNode);
    ExpectPrefix(c->m_SceneGraphNode); ExpectPrefix(Manager->GetRootNode());
    View->AddRemoveSequence(TRUE); other->AddRemoveSequence(TRUE);
    View->RemoveObject(a); other->RemoveObject(e); other->RemoveObject(cChild);
    View->AddRemoveSequence(FALSE); other->AddRemoveSequence(FALSE);
    EXPECT_EQ(0, parent->m_SceneGraphNode->m_ChildToBeParsedCount);
    EXPECT_FALSE(parent->m_SceneGraphNode->IsToBeParsed());
    ExpectPrefix(parent->m_SceneGraphNode); ExpectPrefix(Manager->GetRootNode());
}

TEST_F(SceneGraphLifecycleTest, PriorityChangesClampOwnKeysPropagateDescendantMaximaAndPreserveUnchangedSortState) {
    RCK3dEntity *grandparent = Entity(), *parent = Entity(), *leaf = Entity(), *sibling = Entity();
    ASSERT_NE(nullptr, grandparent); ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, leaf); ASSERT_NE(nullptr, sibling);
    ASSERT_TRUE(parent->SetParent(GraphAPI(grandparent)));
    ASSERT_TRUE(leaf->SetParent(GraphAPI(parent)));
    ASSERT_TRUE(sibling->SetParent(GraphAPI(grandparent)));
    // Recompute the descendant-only priorities of the two leaves: neither has
    // descendants, while its independent own key still represents z-order0.
    leaf->m_SceneGraphNode->PrioritiesChanged();
    sibling->m_SceneGraphNode->PrioritiesChanged();
    parent->SetZOrder(-500);
    leaf->SetZOrder(25000);
    EXPECT_EQ(10000, leaf->GetZOrder());
    EXPECT_EQ(20000, leaf->m_SceneGraphNode->m_MaxPriority);
    EXPECT_EQ(0, leaf->m_SceneGraphNode->m_Priority);
    EXPECT_EQ(20000, parent->m_SceneGraphNode->m_Priority);
    EXPECT_EQ(20000, grandparent->m_SceneGraphNode->m_Priority);
    leaf->SetZOrder(-25000);
    EXPECT_EQ(-10000, leaf->GetZOrder());
    EXPECT_EQ(0, parent->m_SceneGraphNode->m_Priority);
    sibling->SetZOrder(-750);
    EXPECT_EQ(9500, grandparent->m_SceneGraphNode->m_Priority);
    EXPECT_EQ(-500, parent->GetZOrder());
    parent->m_SceneGraphNode->ClearNeedSort();
    leaf->SetZOrder(-25000);
    EXPECT_FALSE(parent->m_SceneGraphNode->NeedsSort());
    grandparent->m_SceneGraphNode->ClearNeedSort();
    grandparent->m_SceneGraphNode->PrioritiesChanged();
    EXPECT_FALSE(grandparent->m_SceneGraphNode->NeedsSort());
    leaf->m_SceneGraphNode->SetPriority(600, TRUE);
    EXPECT_EQ(10600, parent->m_SceneGraphNode->m_Priority);
    EXPECT_EQ(10600, grandparent->m_SceneGraphNode->m_Priority);
}

TEST_F(SceneGraphLifecycleTest, BoxInvalidationPreservesOtherStateAndStopsAtAnAlreadyUncomputedAncestor) {
    RCK3dEntity *top = Entity(), *middle = Entity(), *leaf = Entity();
    ASSERT_NE(nullptr, top); ASSERT_NE(nullptr, middle); ASSERT_NE(nullptr, leaf);
    ASSERT_TRUE(middle->SetParent(GraphAPI(top)));
    ASSERT_TRUE(leaf->SetParent(GraphAPI(middle)));
    VxBbox local; local.Min.Set(-1, -2, -3); local.Max.Set(1, 2, 3);
    ASSERT_TRUE(leaf->SetBoundingBox(&local, TRUE));
    ASSERT_NE(0u, top->m_SceneGraphNode->ComputeHierarchicalBox());
    ASSERT_TRUE(top->m_SceneGraphNode->IsHierarchyBoxComputed());
    ASSERT_TRUE(middle->m_SceneGraphNode->IsHierarchyBoxComputed());
    ASSERT_TRUE(leaf->m_SceneGraphNode->IsHierarchyBoxComputed());
    const CKDWORD retained = CKSGN_INSIDEFRUSTUM | CKSGN_NEEDSORT | CKSGN_INTRANSPARENTLIST;
    leaf->m_SceneGraphNode->SetFlags(retained);
    middle->m_SceneGraphNode->InvalidateBox(FALSE);
    leaf->m_SceneGraphNode->InvalidateBox(TRUE);
    EXPECT_FALSE(leaf->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_FALSE(middle->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_TRUE(top->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_EQ(retained, leaf->m_SceneGraphNode->GetFlags() & retained);
    top->m_SceneGraphNode->InvalidateBox(FALSE);
    ASSERT_NE(0u, top->m_SceneGraphNode->ComputeHierarchicalBox());
    leaf->m_SceneGraphNode->InvalidateBox(TRUE);
    EXPECT_FALSE(leaf->m_SceneGraphNode->IsHierarchyBoxValid());
    EXPECT_FALSE(middle->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_FALSE(top->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_EQ(retained, leaf->m_SceneGraphNode->GetFlags() & retained);
}
