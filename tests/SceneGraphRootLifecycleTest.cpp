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
CK3dEntity *RootAPI(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
CKSceneGraphRootNode *ObservedRoot = nullptr;
CK_ID ObservedIds[2] = {};
int ObservedDestructions = 0;
bool EveryDyingNodeWasPruned = true;
bool RequireClearAllPhase = false;
bool EveryClearAllObservationWasActive = true;

// The real entity destructor and object-manager notifications run normally.
// This subclass only observes whether the queue still borrows a dying node.
class RootLifetimeEntity : public RCK3dEntity {
public:
    explicit RootLifetimeEntity(CKContext *context) : RCK3dEntity(context) {}
    ~RootLifetimeEntity() override {
        if (GetID() != ObservedIds[0] && GetID() != ObservedIds[1])
            return;
        ++ObservedDestructions;
        for (CKTransparentObject *it = ObservedRoot->m_TransparentObjects.Begin();
             it != ObservedRoot->m_TransparentObjects.End(); ++it) {
            if (it->m_Node == m_SceneGraphNode)
                EveryDyingNodeWasPruned = false;
        }
        if (RequireClearAllPhase && !m_Context->IsInClearAll())
            EveryClearAllObservationWasActive = false;
    }
    static CK3dEntity *CreateInstance(CKContext *context) {
        return reinterpret_cast<CK3dEntity *>(new RootLifetimeEntity(context));
    }
};

class SceneGraphRootLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    XClassArray<CKRasterizerInfo> Saved;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RootLifetimeEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        Saved = g_RasterizersInfo; g_RasterizersInfo.Clear();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ObservedRoot = Manager->GetRootNode();
        ObservedIds[0] = ObservedIds[1] = 0;
        ObservedDestructions = 0;
        EveryDyingNodeWasPruned = EveryClearAllObservationWasActive = true;
        RequireClearAllPhase = false;
    }
    void TearDown() override {
        ObservedIds[0] = ObservedIds[1] = 0;
        if (Context) CKCloseContext(Context);
        ObservedRoot = nullptr;
        g_RasterizersInfo = Saved; CKShutdown();
    }
    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }
    void Observe(RCK3dEntity *first, RCK3dEntity *second) {
        ObservedIds[0] = first->GetID(); ObservedIds[1] = second->GetID();
    }
};
}

TEST_F(SceneGraphRootLifecycleTest, ImplicitRootLifetimeOwnsOnlyItsArrayStorageAndBorrowsRealNodes) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    CKSceneGraphNode child(entity);
    {
        CKSceneGraphRootNode root;
        EXPECT_EQ(nullptr, root.m_Entity);
        EXPECT_EQ(nullptr, root.m_Parent);
        EXPECT_EQ(0, root.m_Children.Size());
        EXPECT_EQ(0, root.m_TransparentObjects.Size());
        EXPECT_EQ(0u, root.m_Flags);
        EXPECT_EQ(0u, root.m_RenderContextMask);
        EXPECT_EQ(0u, root.m_EntityMask);
        EXPECT_EQ(0, root.m_ChildToBeParsedCount);
        root.AddNode(&child);
        root.AddTransparentObject(&child);
        ASSERT_EQ(1, root.m_TransparentObjects.Size());
        EXPECT_EQ(&child, root.m_TransparentObjects[0].m_Node);
    }
    EXPECT_EQ(nullptr, child.m_Parent);
    EXPECT_EQ(entity, Context->GetObject(entity->GetID()));
    EXPECT_NE(nullptr, entity->m_SceneGraphNode);
    // Only the private stack root borrowed this independent child. Its flag is
    // not reused as membership of the manager's different root.
    child.ClearInTransparentList();
}

TEST_F(SceneGraphRootLifecycleTest, TransparentInsertionDeduplicatesPreservesPayloadsAndGrowsForRealEntities) {
    CKSceneGraphRootNode *root = Manager->GetRootNode();
    RCK3dEntity *entities[9] = {};
    for (int i = 0; i < 9; ++i) {
        SCOPED_TRACE(i);
        entities[i] = Entity(); ASSERT_NE(nullptr, entities[i]);
        CKSceneGraphNode *node = entities[i]->m_SceneGraphNode; ASSERT_NE(nullptr, node);
        node->SetAsInsideFrustum();
        root->AddTransparentObject(node);
        ASSERT_EQ(i + 1, root->m_TransparentObjects.Size());
        EXPECT_EQ(node, root->m_TransparentObjects[i].m_Node);
        EXPECT_FLOAT_EQ(0, root->m_TransparentObjects[i].m_ZhMin);
        EXPECT_FLOAT_EQ(0, root->m_TransparentObjects[i].m_ZhMax);
        EXPECT_TRUE(node->IsInTransparentList());
        EXPECT_TRUE(node->CheckHierarchyFrustum());
        root->m_TransparentObjects[i].m_ZhMin = static_cast<float>(i + 1);
        root->m_TransparentObjects[i].m_ZhMax = static_cast<float>(i + 3);
        root->AddTransparentObject(node);
        EXPECT_EQ(i + 1, root->m_TransparentObjects.Size());
        EXPECT_FLOAT_EQ(static_cast<float>(i + 1), root->m_TransparentObjects[i].m_ZhMin);
        EXPECT_FLOAT_EQ(static_cast<float>(i + 3), root->m_TransparentObjects[i].m_ZhMax);
    }
    for (int i = 0; i < 9; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(entities[i]->m_SceneGraphNode, root->m_TransparentObjects[i].m_Node);
        EXPECT_FLOAT_EQ(static_cast<float>(i + 1), root->m_TransparentObjects[i].m_ZhMin);
        EXPECT_FLOAT_EQ(static_cast<float>(i + 3), root->m_TransparentObjects[i].m_ZhMax);
        EXPECT_EQ(entities[i], Context->GetObject(entities[i]->GetID()));
    }
}

TEST_F(SceneGraphRootLifecycleTest, CheckPrunesConsecutiveDoomedEntriesAndPreservesLiveOrderAndDepthPayloads) {
    CKSceneGraphRootNode *root = Manager->GetRootNode();
    RCK3dEntity *entities[5] = {};
    for (int i = 0; i < 5; ++i) {
        entities[i] = Entity(); ASSERT_NE(nullptr, entities[i]);
        root->AddTransparentObject(entities[i]->m_SceneGraphNode);
        root->m_TransparentObjects[i].m_ZhMin = static_cast<float>(10 + i);
        root->m_TransparentObjects[i].m_ZhMax = static_cast<float>(20 + i);
    }
    entities[1]->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    entities[2]->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    entities[4]->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    root->Check();
    ASSERT_EQ(2, root->m_TransparentObjects.Size());
    EXPECT_EQ(entities[0]->m_SceneGraphNode, root->m_TransparentObjects[0].m_Node);
    EXPECT_EQ(entities[3]->m_SceneGraphNode, root->m_TransparentObjects[1].m_Node);
    EXPECT_FLOAT_EQ(10, root->m_TransparentObjects[0].m_ZhMin);
    EXPECT_FLOAT_EQ(20, root->m_TransparentObjects[0].m_ZhMax);
    EXPECT_FLOAT_EQ(13, root->m_TransparentObjects[1].m_ZhMin);
    EXPECT_FLOAT_EQ(23, root->m_TransparentObjects[1].m_ZhMax);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(i == 0 || i == 3, entities[i]->m_SceneGraphNode->IsInTransparentList() != FALSE);
        EXPECT_EQ(entities[i], Context->GetObject(entities[i]->GetID()));
    }
    root->Check(); EXPECT_EQ(2, root->m_TransparentObjects.Size());
    // Check prunes references; the Context remains responsible for deletion.
    for (int i = 0; i < 5; ++i)
        entities[i]->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}

TEST_F(SceneGraphRootLifecycleTest, ClearReleasesAllQueuedBorrowsAndOnlyDetachesDirectGraphChildren) {
    RCK3dEntity *parent = Entity(), *child = Entity(), *other = Entity();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, other);
    ASSERT_TRUE(child->SetParent(RootAPI(parent)));
    CKSceneGraphRootNode *root = Manager->GetRootNode();
    root->AddTransparentObject(child->m_SceneGraphNode);
    root->AddTransparentObject(parent->m_SceneGraphNode);
    root->AddTransparentObject(other->m_SceneGraphNode);
    ASSERT_EQ(3, root->m_TransparentObjects.Size());
    root->Clear();
    EXPECT_EQ(0, root->m_TransparentObjects.Size()); EXPECT_EQ(0, root->m_Children.Size());
    EXPECT_EQ(0, root->m_ChildToBeParsedCount); EXPECT_EQ(0, root->m_Index);
    EXPECT_EQ(0u, root->m_Flags); EXPECT_EQ(0u, root->m_RenderContextMask);
    EXPECT_EQ(0u, root->m_EntityMask); EXPECT_EQ(0u, root->m_TimeFpsCalc);
    EXPECT_EQ(10000, root->m_Priority); EXPECT_EQ(10000, root->m_MaxPriority);
    EXPECT_EQ(nullptr, parent->m_SceneGraphNode->m_Parent);
    EXPECT_EQ(nullptr, other->m_SceneGraphNode->m_Parent);
    EXPECT_EQ(parent->m_SceneGraphNode, child->m_SceneGraphNode->m_Parent);
    EXPECT_EQ(RootAPI(parent), child->GetParent());
    EXPECT_FALSE(parent->m_SceneGraphNode->IsInTransparentList());
    EXPECT_FALSE(child->m_SceneGraphNode->IsInTransparentList());
    EXPECT_FALSE(other->m_SceneGraphNode->IsInTransparentList());
    EXPECT_EQ(parent, Context->GetObject(parent->GetID()));
    EXPECT_EQ(child, Context->GetObject(child->GetID()));
    EXPECT_EQ(other, Context->GetObject(other->GetID()));
    root->Clear(); EXPECT_EQ(0, root->m_TransparentObjects.Size());
    RCK3dEntity *newParent = Entity(); ASSERT_NE(nullptr, newParent);
    ASSERT_TRUE(child->SetParent(RootAPI(newParent)));
    root->AddTransparentObject(child->m_SceneGraphNode);
    ASSERT_EQ(1, root->m_TransparentObjects.Size());
    EXPECT_EQ(child->m_SceneGraphNode, root->m_TransparentObjects[0].m_Node);
    EXPECT_EQ(newParent->m_SceneGraphNode, child->m_SceneGraphNode->m_Parent);
}

TEST_F(SceneGraphRootLifecycleTest, ActualDeletionNotificationsRemoveQueuedNodesBeforeTheirEntityDestructorsRun) {
    RCK3dEntity *first = Entity(), *second = Entity(), *survivor = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, survivor);
    CKSceneGraphRootNode *root = Manager->GetRootNode();
    root->AddTransparentObject(first->m_SceneGraphNode);
    root->AddTransparentObject(second->m_SceneGraphNode);
    root->AddTransparentObject(survivor->m_SceneGraphNode);
    root->m_TransparentObjects[2].m_ZhMin = 7;
    root->m_TransparentObjects[2].m_ZhMax = 9;
    Observe(first, second);
    CK_ID doomed[2] = {first->GetID(), second->GetID()};
    ASSERT_EQ(CK_OK, Context->DestroyObjects(doomed, 2));
    EXPECT_EQ(2, ObservedDestructions); EXPECT_TRUE(EveryDyingNodeWasPruned);
    EXPECT_EQ(nullptr, Context->GetObject(doomed[0])); EXPECT_EQ(nullptr, Context->GetObject(doomed[1]));
    ASSERT_EQ(1, root->m_TransparentObjects.Size());
    EXPECT_EQ(survivor->m_SceneGraphNode, root->m_TransparentObjects[0].m_Node);
    EXPECT_FLOAT_EQ(7, root->m_TransparentObjects[0].m_ZhMin);
    EXPECT_FLOAT_EQ(9, root->m_TransparentObjects[0].m_ZhMax);
    EXPECT_TRUE(survivor->m_SceneGraphNode->IsInTransparentList());
}

TEST_F(SceneGraphRootLifecycleTest, ActualClearAllDropsTheQueueBeforeObjectManagerDeletionWithoutDoubleOwnership) {
    RCK3dEntity *parent = Entity(), *child = Entity();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(RootAPI(parent)));
    CKSceneGraphRootNode *root = Manager->GetRootNode();
    root->AddTransparentObject(parent->m_SceneGraphNode);
    root->AddTransparentObject(child->m_SceneGraphNode);
    Observe(parent, child); RequireClearAllPhase = true;
    ASSERT_EQ(CK_OK, Context->ClearAll());
    EXPECT_EQ(2, ObservedDestructions);
    EXPECT_TRUE(EveryDyingNodeWasPruned);
    EXPECT_TRUE(EveryClearAllObservationWasActive);
    EXPECT_EQ(0, root->m_TransparentObjects.Size());
    EXPECT_EQ(0, root->m_Children.Size());
    EXPECT_EQ(0, root->m_ChildToBeParsedCount);
}
