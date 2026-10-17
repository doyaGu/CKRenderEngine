#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKSceneGraph.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CountingBoxEntity : public RCK3dEntity {
public:
    int BoxUpdates = 0;
    explicit CountingBoxEntity(CKContext *context) : RCK3dEntity(context) {}
    void UpdateBox(CKBOOL world) override {
        ++BoxUpdates;
        RCK3dEntity::UpdateBox(world);
    }
};

class SceneGraphVisibilityTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_ViewportData.ViewX = 10;
        Device->m_ViewportData.ViewY = 20;
        Device->m_ViewportData.ViewWidth = 640;
        Device->m_ViewportData.ViewHeight = 640;
        VxMatrix projection;
        projection.Perspective(PI / 2, 1, 1, 9);
        Device->SetProjectionTransformationMatrix(projection);
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        ASSERT_EQ(nullptr, Device->m_RasterizerContext);
    }

    void TearDown() override {
        delete Device;
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    static CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    CKSceneGraphNode *Node(RCK3dEntity *entity) {
        if (!entity->m_SceneGraphNode)
            entity->m_SceneGraphNode = Manager->CreateNode(entity);
        return entity->m_SceneGraphNode;
    }
    RCK3dEntity *Entity(float x = 0, float y = 0, float z = 0) {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        EXPECT_NE(nullptr, entity);
        if (!entity)
            return nullptr;
        entity->SetPosition3f(x, y, z);
        EXPECT_NE(nullptr, Node(entity));
        return entity;
    }
    static void LocalBox(RCK3dEntity *entity, float halfX = .5f, float halfY = .5f, float halfZ = .5f) {
        VxBbox box;
        box.Min.Set(-halfX, -halfY, -halfZ);
        box.Max.Set(halfX, halfY, halfZ);
        ASSERT_TRUE(entity->SetBoundingBox(&box, TRUE));
    }
    static void Box(const VxBbox &box, float x0, float y0, float z0, float x1, float y1, float z1) {
        EXPECT_FLOAT_EQ(x0, box.Min.x); EXPECT_FLOAT_EQ(y0, box.Min.y); EXPECT_FLOAT_EQ(z0, box.Min.z);
        EXPECT_FLOAT_EQ(x1, box.Max.x); EXPECT_FLOAT_EQ(y1, box.Max.y); EXPECT_FLOAT_EQ(z1, box.Max.z);
    }
};
}

TEST_F(SceneGraphVisibilityTest, RealWorldBoxCacheSkipsRepeatedUpdatesUntilTheEntityMoves) {
    CountingBoxEntity entity(Context);
    CKSceneGraphNode *node = Node(&entity);
    ASSERT_NE(nullptr, node);
    LocalBox(&entity, 1, 2, 3);
    entity.SetPosition3f(5, 7, 11);
    EXPECT_NE(0u, node->ComputeHierarchicalBox());
    EXPECT_EQ(1, entity.BoxUpdates);
    Box(node->m_Bbox, 4, 5, 8, 6, 9, 14);
    EXPECT_NE(0u, node->ComputeHierarchicalBox());
    EXPECT_EQ(1, entity.BoxUpdates);
    entity.SetPosition3f(13, 17, 19);
    EXPECT_FALSE(node->IsHierarchyBoxComputed());
    EXPECT_NE(0u, node->ComputeHierarchicalBox());
    EXPECT_EQ(2, entity.BoxUpdates);
    Box(node->m_Bbox, 12, 15, 16, 14, 19, 22);
}

TEST_F(SceneGraphVisibilityTest, HierarchyUnionIncludesOwnBoxAndHiddenUnparsedChildren) {
    RCK3dEntity *parent = Entity(3, 0, 5);
    RCK3dEntity *child = Entity(12, 2, 6);
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child);
    LocalBox(parent, 1, 1, 1);
    LocalBox(child, 2, 3, 1);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    child->Show(CKHIDE);
    CKSceneGraphNode *node = Node(parent);
    ASSERT_EQ(0, node->m_ChildToBeParsedCount);
    ASSERT_EQ(0u, node->m_EntityMask);
    node->SetFlags(CKSGN_INSIDEFRUSTUM | CKSGN_INTRANSPARENTLIST | 0x80000000u);
    EXPECT_NE(0u, node->ComputeHierarchicalBox());
    Box(node->m_Bbox, 2, -1, 4, 14, 5, 7);
    EXPECT_TRUE(node->HasAllFlags(CKSGN_BOX_MASK | CKSGN_INSIDEFRUSTUM | CKSGN_INTRANSPARENTLIST | 0x80000000u));
}

TEST_F(SceneGraphVisibilityTest, MeshlessParentSkipsEmptyChildrenAndMergesAllValidDescendants) {
    RCK3dEntity *parent = Entity(100, 200, -100);
    RCK3dEntity *empty = Entity(500, 500, -500);
    RCK3dEntity *first = Entity(-3, 1, 5);
    RCK3dEntity *second = Entity(7, 2, 6);
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, empty); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    LocalBox(first, 1, 2, 1); LocalBox(second, 2, 1, 2);
    ASSERT_TRUE(empty->SetParent(API(parent), TRUE));
    ASSERT_TRUE(first->SetParent(API(parent), TRUE));
    ASSERT_TRUE(second->SetParent(API(empty), TRUE));
    EXPECT_NE(0u, Node(parent)->ComputeHierarchicalBox());
    Box(Node(parent)->m_Bbox, -4, -1, 4, 9, 3, 8);
    Box(Node(empty)->m_Bbox, 5, 1, 4, 9, 3, 8);
    EXPECT_TRUE(Node(empty)->IsHierarchyBoxValid());
}

TEST_F(SceneGraphVisibilityTest, EntirelyEmptyHierarchyCachesAbsenceWithoutTreatingWorldPointsAsGeometry) {
    RCK3dEntity *parent = Entity(100, 100, -100), *child = Entity(200, 200, -200);
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    CKSceneGraphNode *node = Node(parent);
    node->SetFlags(CKSGN_NEEDSORT | CKSGN_INTRANSPARENTLIST);
    EXPECT_EQ(0u, node->ComputeHierarchicalBox());
    EXPECT_TRUE(node->IsHierarchyBoxComputed());
    EXPECT_FALSE(node->IsHierarchyBoxValid());
    EXPECT_TRUE(node->HasAllFlags(CKSGN_NEEDSORT | CKSGN_INTRANSPARENTLIST));
    EXPECT_TRUE(parent->IsInViewFrustrumHierarchic(Device));
    EXPECT_FALSE(node->CheckHierarchyFrustum());
    EXPECT_FALSE(node->IsAllOutsideFrustum());
}

TEST_F(SceneGraphVisibilityTest, MovingARealChildInvalidatesTheAncestorUnionAndReclassifiesTheClipVolume) {
    RCK3dEntity *parent = Entity(), *child = Entity(20, 0, 5);
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child);
    LocalBox(child);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device));
    EXPECT_TRUE(Node(parent)->IsAllOutsideFrustum());
    child->SetPosition3f(0, 0, 5);
    EXPECT_FALSE(Node(parent)->IsHierarchyBoxComputed());
    EXPECT_TRUE(parent->IsInViewFrustrumHierarchic(Device));
    EXPECT_TRUE(Node(parent)->CheckHierarchyFrustum());
    EXPECT_FALSE(Node(parent)->IsAllOutsideFrustum());
    Box(Node(parent)->m_Bbox, -.5f, -.5f, 4.5f, .5f, .5f, 5.5f);
    child->SetPosition3f(0, 0, .25f);
    EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device));
    EXPECT_FALSE(Node(parent)->CheckHierarchyFrustum());
    EXPECT_TRUE(Node(parent)->IsAllOutsideFrustum());
}

TEST_F(SceneGraphVisibilityTest, IntersectingHierarchyIsVisibleWithoutTheWholeHierarchyInsideShortcut) {
    RCK3dEntity *parent = Entity(12, 0, 5), *child = Entity(0, 0, 5);
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child);
    LocalBox(parent); LocalBox(child);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    Node(parent)->SetFlags(CKSGN_FRUSTUM_MASK);
    EXPECT_TRUE(parent->IsInViewFrustrumHierarchic(Device));
    EXPECT_FALSE(Node(parent)->CheckHierarchyFrustum());
    EXPECT_FALSE(Node(parent)->IsAllOutsideFrustum());
    Box(Node(parent)->m_Bbox, -.5f, -.5f, 4.5f, 12.5f, .5f, 5.5f);
}

TEST_F(SceneGraphVisibilityTest, ExactNearAndFarPlaneContactRemainsInsideTheClosedClipVolume) {
    RCK3dEntity *entity = Entity(0, 0, 1);
    ASSERT_NE(nullptr, entity);
    LocalBox(entity, .25f, .25f, 0);
    EXPECT_TRUE(entity->IsInViewFrustrumHierarchic(Device));
    EXPECT_TRUE(Node(entity)->CheckHierarchyFrustum());
    entity->SetPosition3f(0, 0, 9);
    EXPECT_TRUE(entity->IsInViewFrustrumHierarchic(Device));
    EXPECT_TRUE(Node(entity)->CheckHierarchyFrustum());
}

TEST_F(SceneGraphVisibilityTest, FrustumSettersAccumulateAndPotentialResetPreservesAllOtherState) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CKSceneGraphNode *node = Node(entity);
    const CKDWORD retained = CKSGN_BOX_MASK | CKSGN_NEEDSORT | CKSGN_INTRANSPARENTLIST | 0x80000000u;
    node->SetFlags(retained);
    node->SetAsInsideFrustum();
    EXPECT_TRUE(node->CheckHierarchyFrustum());
    EXPECT_FALSE(node->IsAllOutsideFrustum());
    node->SetAsOutsideFrustum();
    EXPECT_TRUE(node->CheckHierarchyFrustum());
    EXPECT_TRUE(node->IsAllOutsideFrustum());
    EXPECT_EQ(retained | CKSGN_FRUSTUM_MASK, node->GetFlags());
    node->SetAsPotentiallyVisible();
    EXPECT_EQ(retained, node->GetFlags());
    EXPECT_FALSE(node->CheckHierarchyFrustum());
    EXPECT_FALSE(node->IsAllOutsideFrustum());
}

TEST_F(SceneGraphVisibilityTest, GeneralFlagQueriesAndUpdatesPreserveDisjointBitsAndUseBooleanEnable) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CKSceneGraphNode *node = Node(entity);
    const CKDWORD retained = 0x80000000u | CKSGN_NEEDSORT;
    node->SetFlags(retained);
    node->SetFlags(CKSGN_INSIDEFRUSTUM | CKSGN_BOXCOMPUTED);
    EXPECT_TRUE(node->HasAllFlags(CKSGN_INSIDEFRUSTUM | CKSGN_BOXCOMPUTED));
    EXPECT_TRUE(node->HasAnyFlags(CKSGN_BOX_MASK));
    EXPECT_FALSE(node->HasAllFlags(CKSGN_BOX_MASK));
    EXPECT_TRUE(node->HasAllFlags(0));
    EXPECT_FALSE(node->HasAnyFlags(0));
    node->ClearFlags(CKSGN_INSIDEFRUSTUM);
    node->SetFlagState(CKSGN_OUTSIDEFRUSTUM | CKSGN_BOXVALID, -7);
    EXPECT_TRUE(node->HasAllFlags(CKSGN_OUTSIDEFRUSTUM | CKSGN_BOX_MASK));
    node->SetFlagState(CKSGN_FRUSTUM_MASK, FALSE);
    EXPECT_EQ(retained | CKSGN_BOX_MASK, node->GetFlags());
}

TEST_F(SceneGraphVisibilityTest, SortHelpersOnlyChangeTheNeedSortBitAndNeverPriorityOrMembership) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CKSceneGraphNode *node = Node(entity);
    const short priority = node->m_Priority, maximum = node->m_MaxPriority;
    const CKDWORD mask = node->m_EntityMask;
    node->SetFlags(CKSGN_INSIDEFRUSTUM | CKSGN_INTRANSPARENTLIST);
    node->ClearNeedSort();
    EXPECT_FALSE(node->NeedsSort());
    node->MarkNeedSort();
    EXPECT_TRUE(node->NeedsSort());
    node->ClearNeedSort();
    EXPECT_FALSE(node->NeedsSort());
    EXPECT_EQ(CKSGN_INSIDEFRUSTUM | CKSGN_INTRANSPARENTLIST, node->GetFlags());
    EXPECT_EQ(priority, node->m_Priority); EXPECT_EQ(maximum, node->m_MaxPriority);
    EXPECT_EQ(mask, node->m_EntityMask);
}

TEST_F(SceneGraphVisibilityTest, TransparentMembershipHelpersDoNotEraseVisibilityOrBoxState) {
    RCK3dEntity *entity = Entity(0, 0, 5);
    ASSERT_NE(nullptr, entity);
    LocalBox(entity);
    CKSceneGraphNode *node = Node(entity);
    ASSERT_NE(0u, node->ComputeHierarchicalBox());
    node->SetAsInsideFrustum();
    node->MarkInTransparentList();
    EXPECT_TRUE(node->IsInTransparentList());
    node->ClearInTransparentList();
    EXPECT_FALSE(node->IsInTransparentList());
    EXPECT_TRUE(node->CheckHierarchyFrustum());
    EXPECT_TRUE(node->IsHierarchyBoxComputed());
    EXPECT_TRUE(node->IsHierarchyBoxValid());
}

TEST_F(SceneGraphVisibilityTest, BoxFlagHelpersClearBothCacheBitsWithoutTouchingVisibilityOrSort) {
    RCK3dEntity *entity = Entity(0, 0, 5);
    ASSERT_NE(nullptr, entity);
    LocalBox(entity);
    CKSceneGraphNode *node = Node(entity);
    ASSERT_NE(0u, node->ComputeHierarchicalBox());
    node->SetFlags(CKSGN_INSIDEFRUSTUM | CKSGN_NEEDSORT | CKSGN_INTRANSPARENTLIST);
    EXPECT_TRUE(node->IsHierarchyBoxComputed());
    EXPECT_TRUE(node->IsHierarchyBoxValid());
    node->InvalidateHierarchyBox();
    EXPECT_FALSE(node->IsHierarchyBoxComputed());
    EXPECT_FALSE(node->IsHierarchyBoxValid());
    EXPECT_EQ(CKSGN_INSIDEFRUSTUM | CKSGN_NEEDSORT | CKSGN_INTRANSPARENTLIST, node->GetFlags());
    ASSERT_NE(0u, node->ComputeHierarchicalBox());
    Box(node->m_Bbox, -.5f, -.5f, 4.5f, .5f, .5f, 5.5f);
}

TEST_F(SceneGraphVisibilityTest, AnimatedIntermediateThenLeafScaleCannotLeaveTheGrandparentWorldBoxCached) {
    RCK3dEntity *top = Entity(), *middle = Entity(), *leaf = Entity(0, 0, 5);
    ASSERT_NE(nullptr, top); ASSERT_NE(nullptr, middle); ASSERT_NE(nullptr, leaf);
    LocalBox(leaf);
    ASSERT_TRUE(middle->SetParent(API(top), TRUE));
    ASSERT_TRUE(leaf->SetParent(API(middle), TRUE));
    RCKObjectAnimation animation(Context);
    animation.SetLength(1);
    VxVector start(0, 0, 0), end(20, 0, 0);
    animation.AddPositionKey(0, &start);
    animation.AddPositionKey(1, &end);
    animation.Set3dEntity(API(middle));
    ASSERT_EQ(CK_OK, animation.SetStep(0));
    ASSERT_TRUE(top->IsInViewFrustrumHierarchic(Device));
    ASSERT_TRUE(Node(top)->IsHierarchyBoxComputed());
    ASSERT_EQ(CK_OK, animation.SetStep(1));
    leaf->SetScale3f(2, 2, 2);
    EXPECT_FALSE(Node(top)->IsHierarchyBoxComputed());
    EXPECT_NE(0u, Node(top)->ComputeHierarchicalBox());
    Box(Node(top)->m_Bbox, 19, -1, 4, 21, 1, 6);
    EXPECT_FALSE(top->IsInViewFrustrumHierarchic(Device));
    EXPECT_TRUE(Node(top)->IsAllOutsideFrustum());
    animation.Set3dEntity(nullptr);
}
