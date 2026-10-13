#include <gtest/gtest.h>
#include <climits>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
CK2dEntity *API(RCK2dEntity *e) { return reinterpret_cast<CK2dEntity *>(e); }
RCK2dEntity *Implementation(CK2dEntity *e) { return reinterpret_cast<RCK2dEntity *>(e); }

class Entity2dHierarchyTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    RCK2dEntity *Foreground = nullptr, *Background = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Context->m_BehaviorContext.CurrentRenderContext = Device;
        Foreground = Implementation(Device->Get2dRoot(FALSE));
        Background = Implementation(Device->Get2dRoot(TRUE));
        ASSERT_NE(nullptr, Foreground); ASSERT_NE(nullptr, Background);
        EXPECT_FALSE(Foreground->IsVisible()); EXPECT_FALSE(Background->IsVisible());
    }
    void TearDown() override {
        // Keep the real root context available throughout object PreDelete.
        if (Context) { Context->m_BehaviorContext.CurrentRenderContext = nullptr; }
        delete Device; if (Context) CKCloseContext(Context); CKShutdown();
    }
    RCK2dEntity *Entity() { return static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY)); }
    static void DestroyOnly(CKContext *context, CKObject *object) {
        CKDependencies deps; deps.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, context->DestroyObject(object, 0, &deps));
    }
};

TEST_F(Entity2dHierarchyTest, RootAttachmentDoesNotExposeSyntheticParentAndReattachmentDeduplicates) {
    RCK2dEntity *e = Entity(); ASSERT_NE(nullptr, e);
    ASSERT_TRUE(e->SetParent(nullptr)); EXPECT_TRUE(e->IsRootObject()); EXPECT_EQ(nullptr, e->GetParent());
    ASSERT_EQ(1, Foreground->GetChildrenCount()); EXPECT_EQ(API(e), Foreground->GetChild(0));
    ASSERT_TRUE(e->SetParent(nullptr)); ASSERT_EQ(1, Foreground->GetChildrenCount());
    ASSERT_TRUE(e->SetParent(API(Background)));
    EXPECT_EQ(0, Foreground->GetChildrenCount()); ASSERT_EQ(1, Background->GetChildrenCount());
    EXPECT_EQ(API(e), Background->GetChild(0)); EXPECT_EQ(nullptr, e->GetParent());
    // The root argument selects the list; SetBackground separately sets flags.
    EXPECT_FALSE(e->IsBackground());
    ASSERT_TRUE(e->SetParent(API(Foreground)));
    EXPECT_EQ(0, Background->GetChildrenCount()); EXPECT_EQ(API(e), Foreground->GetChild(0));
}

TEST_F(Entity2dHierarchyTest, RealParentMoveMaintainsBothListsAndOwnRootStatus) {
    RCK2dEntity *a = Entity(), *b = Entity(), *child = Entity();
    ASSERT_TRUE(a->SetParent(nullptr)); ASSERT_TRUE(b->SetParent(nullptr)); ASSERT_TRUE(child->SetParent(nullptr));
    ASSERT_TRUE(child->SetParent(API(a)));
    EXPECT_EQ(API(a), child->GetParent()); EXPECT_FALSE(child->IsRootObject());
    EXPECT_EQ(2, Foreground->GetChildrenCount()); EXPECT_EQ(API(child), a->GetChild(0));
    ASSERT_TRUE(child->SetParent(API(b))); EXPECT_EQ(0, a->GetChildrenCount());
    ASSERT_EQ(1, b->GetChildrenCount()); EXPECT_EQ(API(child), b->GetChild(0));
    ASSERT_TRUE(child->SetParent(API(b))); EXPECT_EQ(1, b->GetChildrenCount());
    ASSERT_TRUE(child->SetParent(nullptr)); EXPECT_EQ(0, b->GetChildrenCount());
    EXPECT_TRUE(child->IsRootObject()); EXPECT_EQ(3, Foreground->GetChildrenCount());
}

TEST_F(Entity2dHierarchyTest, CyclesRejectWithoutMutatingExistingParentLists) {
    RCK2dEntity *a = Entity(), *b = Entity(), *c = Entity();
    ASSERT_TRUE(a->SetParent(nullptr)); ASSERT_TRUE(b->SetParent(API(a))); ASSERT_TRUE(c->SetParent(API(b)));
    EXPECT_FALSE(a->SetParent(API(c))); EXPECT_FALSE(b->SetParent(API(b)));
    EXPECT_EQ(nullptr, a->GetParent()); EXPECT_EQ(API(a), b->GetParent()); EXPECT_EQ(API(b), c->GetParent());
    EXPECT_EQ(API(a), Foreground->GetChild(0)); EXPECT_EQ(API(b), a->GetChild(0)); EXPECT_EQ(API(c), b->GetChild(0));
    EXPECT_EQ(nullptr, c->GetChild(0)); EXPECT_EQ(nullptr, a->GetChild(-1)); EXPECT_EQ(nullptr, a->GetChild(1));
}

TEST_F(Entity2dHierarchyTest, OrdinaryHiddenAncestorKeepsDescendantsEligibleButHierarchicalHideStopsThem) {
    RCK2dEntity *a = Entity(), *b = Entity(), *c = Entity();
    ASSERT_TRUE(a->SetParent(nullptr)); ASSERT_TRUE(b->SetParent(API(a))); ASSERT_TRUE(c->SetParent(API(b)));
    a->Show(CKHIDE); b->Show(CKHIDE); c->Show(CKSHOW);
    EXPECT_FALSE(c->IsHiddenByParent()); EXPECT_FALSE(b->IsHiddenByParent());
    a->Show(CKHIERARCHICALHIDE); EXPECT_TRUE(c->IsHiddenByParent()); EXPECT_TRUE(b->IsHiddenByParent());
    EXPECT_FALSE(a->IsHiddenByParent());
    a->Show(CKSHOW); b->Show(CKHIERARCHICALHIDE); EXPECT_TRUE(c->IsHiddenByParent());
    EXPECT_FALSE(b->IsHiddenByParent()); b->Show(CKSHOW);
    c->Show(CKHIERARCHICALHIDE); EXPECT_FALSE(c->IsHiddenByParent());
}

TEST_F(Entity2dHierarchyTest, HiddenSyntheticRootsDoNotHideVisibleTopLevelEntities) {
    RCK2dEntity *e = Entity(); ASSERT_TRUE(e->SetParent(nullptr));
    EXPECT_FALSE(e->IsHiddenByParent()); e->SetBackground(TRUE);
    EXPECT_TRUE(e->IsBackground()); EXPECT_FALSE(e->IsHiddenByParent());
}

TEST_F(Entity2dHierarchyTest, BackgroundMigrationMovesRootAndRecursivelyChangesDescendants) {
    RCK2dEntity *root = Entity(), *child = Entity(), *grandchild = Entity();
    ASSERT_TRUE(root->SetParent(nullptr)); ASSERT_TRUE(child->SetParent(API(root)));
    ASSERT_TRUE(grandchild->SetParent(API(child)));
    root->SetBackground(TRUE); EXPECT_EQ(0, Foreground->GetChildrenCount());
    ASSERT_EQ(1, Background->GetChildrenCount()); EXPECT_EQ(API(root), Background->GetChild(0));
    EXPECT_TRUE(root->IsBackground()); EXPECT_TRUE(child->IsBackground()); EXPECT_TRUE(grandchild->IsBackground());
    child->SetBackground(FALSE); EXPECT_TRUE(child->IsBackground());
    root->SetBackground(FALSE); EXPECT_EQ(0, Background->GetChildrenCount());
    EXPECT_EQ(API(root), Foreground->GetChild(0)); EXPECT_FALSE(root->IsBackground());
    EXPECT_FALSE(child->IsBackground()); EXPECT_FALSE(grandchild->IsBackground());
}

TEST_F(Entity2dHierarchyTest, ExplicitHierarchyBackgroundAndRealParentAdoptionRespectFlagOwnership) {
    RCK2dEntity *a = Entity(), *b = Entity(), *c = Entity();
    ASSERT_TRUE(a->SetParent(nullptr)); ASSERT_TRUE(b->SetParent(API(a))); ASSERT_TRUE(c->SetParent(API(b)));
    const CKDWORD unrelated = CK_2DENTITY_NOTPICKABLE | CK_2DENTITY_CLIPTOPARENT;
    b->ModifyFlags(unrelated, 0); a->HierarchySetBackground(TRUE);
    EXPECT_TRUE(a->IsBackground()); EXPECT_TRUE(b->IsBackground()); EXPECT_TRUE(c->IsBackground());
    EXPECT_EQ(unrelated, b->GetFlags() & unrelated);
    a->HierarchySetBackground(FALSE); EXPECT_FALSE(b->IsBackground()); EXPECT_FALSE(c->IsBackground());
    EXPECT_EQ(unrelated, b->GetFlags() & unrelated);
    RCK2dEntity *backParent = Entity(); backParent->HierarchySetBackground(TRUE);
    ASSERT_TRUE(b->SetParent(API(backParent))); EXPECT_TRUE(b->IsBackground());
    // Native SetParent changes only the attached node; recursive migration is
    // the separate HierarchySetBackground operation.
    EXPECT_FALSE(c->IsBackground());
}

TEST_F(Entity2dHierarchyTest, ZOrderSortsRealParentAndSyntheticRootListsAfterMutation) {
    RCK2dEntity *parent = Entity(), *low = Entity(), *middle = Entity(), *high = Entity();
    low->SetZOrder(-20); middle->SetZOrder(0); high->SetZOrder(30);
    ASSERT_TRUE(high->SetParent(API(parent))); ASSERT_TRUE(low->SetParent(API(parent)));
    ASSERT_TRUE(middle->SetParent(API(parent)));
    EXPECT_EQ(API(low), parent->GetChild(0)); EXPECT_EQ(API(middle), parent->GetChild(1)); EXPECT_EQ(API(high), parent->GetChild(2));
    high->SetZOrder(-30); EXPECT_EQ(API(high), parent->GetChild(0)); EXPECT_EQ(-30, high->GetZOrder());
    ASSERT_TRUE(high->SetParent(nullptr)); ASSERT_TRUE(low->SetParent(nullptr)); ASSERT_TRUE(middle->SetParent(nullptr));
    middle->SetZOrder(-100); EXPECT_EQ(API(middle), Foreground->GetChild(0));
    EXPECT_EQ(API(high), Foreground->GetChild(1)); EXPECT_EQ(API(low), Foreground->GetChild(2));
}

TEST_F(Entity2dHierarchyTest, FullSignedZOrderRangeSortsNumericallyWithoutComparatorOverflow) {
    RCK2dEntity *parent = Entity(), *minimum = Entity(), *zero = Entity(), *maximum = Entity();
    minimum->SetZOrder(INT_MIN); maximum->SetZOrder(INT_MAX);
    ASSERT_TRUE(maximum->SetParent(API(parent))); ASSERT_TRUE(minimum->SetParent(API(parent)));
    ASSERT_TRUE(zero->SetParent(API(parent)));
    EXPECT_EQ(API(minimum), parent->GetChild(0)); EXPECT_EQ(API(zero), parent->GetChild(1)); EXPECT_EQ(API(maximum), parent->GetChild(2));
    maximum->SetZOrder(INT_MIN + 1);
    EXPECT_EQ(API(minimum), parent->GetChild(0)); EXPECT_EQ(API(maximum), parent->GetChild(1)); EXPECT_EQ(API(zero), parent->GetChild(2));
}

TEST_F(Entity2dHierarchyTest, HierarchyParserTraversesDepthFirstAndStopsAtRequestedSubtree) {
    RCK2dEntity *root = Entity(), *a = Entity(), *b = Entity(), *c = Entity(), *d = Entity(), *outside = Entity();
    ASSERT_TRUE(root->SetParent(nullptr)); ASSERT_TRUE(outside->SetParent(nullptr));
    a->SetZOrder(1); b->SetZOrder(2);
    ASSERT_TRUE(b->SetParent(API(root))); ASSERT_TRUE(a->SetParent(API(root)));
    ASSERT_TRUE(c->SetParent(API(a))); ASSERT_TRUE(d->SetParent(API(c)));
    CK2dEntity *expected[] = {API(a), API(c), API(d), API(b)};
    CK2dEntity *cursor = nullptr;
    for (CK2dEntity *e : expected) { cursor = root->HierarchyParser(cursor); EXPECT_EQ(e, cursor); }
    EXPECT_EQ(nullptr, root->HierarchyParser(cursor));
    EXPECT_EQ(API(c), a->HierarchyParser(nullptr)); EXPECT_EQ(API(d), a->HierarchyParser(API(c)));
    EXPECT_EQ(nullptr, a->HierarchyParser(API(d))); EXPECT_EQ(nullptr, b->HierarchyParser(nullptr));
}

TEST_F(Entity2dHierarchyTest, HierarchyParserTraversesActualForegroundAndBackgroundRootLists) {
    RCK2dEntity *a = Entity(), *b = Entity(), *c = Entity();
    a->SetZOrder(2); b->SetZOrder(1); ASSERT_TRUE(a->SetParent(nullptr)); ASSERT_TRUE(b->SetParent(nullptr));
    ASSERT_TRUE(c->SetParent(API(b)));
    EXPECT_EQ(API(b), Foreground->HierarchyParser(nullptr));
    EXPECT_EQ(API(c), Foreground->HierarchyParser(API(b)));
    EXPECT_EQ(API(a), Foreground->HierarchyParser(API(c)));
    EXPECT_EQ(nullptr, Foreground->HierarchyParser(API(a)));
    a->SetBackground(TRUE); EXPECT_EQ(API(a), Background->HierarchyParser(nullptr));
    EXPECT_EQ(nullptr, Background->HierarchyParser(API(a)));
}

TEST_F(Entity2dHierarchyTest, PreDeleteReparentsSurvivingChildrenToNearestLiveAncestor) {
    RCK2dEntity *ancestor = Entity(), *doomed = Entity(), *middle = Entity(), *child = Entity();
    ASSERT_TRUE(ancestor->SetParent(nullptr)); ASSERT_TRUE(doomed->SetParent(API(ancestor)));
    ASSERT_TRUE(middle->SetParent(API(doomed))); ASSERT_TRUE(child->SetParent(API(middle)));
    const CK_ID doomedID = doomed->GetID(), middleID = middle->GetID();
    CK_ID ids[] = {doomedID, middleID};
    CKDependencies deps; deps.m_Flags = CK_DEPENDENCIES_NONE;
    // Both doomed ancestors belong to the actual deletion batch. PreDelete
    // may leave transient pointers in their child arrays until destruction.
    ASSERT_EQ(CK_OK, Context->DestroyObjects(ids, 2, 0, &deps));
    EXPECT_EQ(nullptr, Context->GetObject(doomedID)); EXPECT_EQ(nullptr, Context->GetObject(middleID));
    EXPECT_EQ(API(ancestor), child->GetParent());
    ASSERT_EQ(1, ancestor->GetChildrenCount()); EXPECT_EQ(API(child), ancestor->GetChild(0));
}

TEST_F(Entity2dHierarchyTest, PreDeleteMovesLiveChildrenToTheirBackgroundRootAndSkipsDoomedChildren) {
    RCK2dEntity *parent = Entity(), *live = Entity(), *doomed = Entity();
    ASSERT_TRUE(parent->SetParent(nullptr)); parent->SetBackground(TRUE);
    ASSERT_TRUE(live->SetParent(API(parent))); ASSERT_TRUE(doomed->SetParent(API(parent)));
    doomed->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    parent->PreDelete(); EXPECT_EQ(nullptr, live->GetParent()); EXPECT_TRUE(live->IsBackground());
    ASSERT_EQ(1, Background->GetChildrenCount()); EXPECT_EQ(API(live), Background->GetChild(0));
    EXPECT_EQ(API(parent), doomed->GetParent());
    doomed->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); ASSERT_TRUE(doomed->SetParent(nullptr));
}

TEST_F(Entity2dHierarchyTest, PreDeleteOfSyntheticRootLeavesAttachedHierarchyUnchanged) {
    RCK2dEntity *a = Entity(), *b = Entity(); ASSERT_TRUE(a->SetParent(nullptr)); ASSERT_TRUE(b->SetParent(API(a)));
    Foreground->PreDelete(); EXPECT_EQ(API(a), Foreground->GetChild(0)); EXPECT_EQ(nullptr, a->GetParent());
    EXPECT_EQ(API(a), b->GetParent()); EXPECT_EQ(API(b), a->GetChild(0));
}
}
