#include <gtest/gtest.h>
#include <climits>
#include "CKAll.h"
#include "CKSceneGraph.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class RenderObjectContextTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *First = nullptr;
    RCKRenderContext *Second = nullptr;
    RCKRenderContext *High = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); // Owned by CKContext.
        First = new RCKRenderContext(Context);
        Second = new RCKRenderContext(Context);
        High = new RCKRenderContext(Context);
    }

    void ProbeMasks() {
        // This fixture tests consumption of a render-context mask. Allocation
        // of those masks belongs to RCKRenderContext/RCKRenderManager.
        First->m_MaskFree = 1;
        Second->m_MaskFree = 4;
        High->m_MaskFree = 0x80000000u;
    }

    void TearDown() override {
        delete High;
        delete Second;
        delete First;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCKRenderObject *Object() {
        return static_cast<RCKRenderObject *>(Context->CreateObject(CKCID_RENDEROBJECT));
    }

    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }

    static CK3dEntity *API(RCK3dEntity *entity) {
        return reinterpret_cast<CK3dEntity *>(entity);
    }
};

TEST_F(RenderObjectContextTest, BasePoliciesRemainIndependentOfVisibilityAndMembership) {
    ProbeMasks();
    RCKRenderObject *object = Object();
    ASSERT_NE(nullptr, object);
    CKRenderObject *api = object;
    EXPECT_EQ(2, api->CanBeHide());
    EXPECT_TRUE(api->IsRootObject());
    EXPECT_TRUE(api->IsToBeRendered());
    EXPECT_FALSE(api->IsToBeRenderedLast());
    api->Show(CKHIDE);
    object->AddToRenderContext(First);
    EXPECT_TRUE(api->IsToBeRendered());
    EXPECT_TRUE(api->IsRootObject());
    EXPECT_FALSE(api->IsToBeRenderedLast());
    EXPECT_EQ(2, api->CanBeHide());
    EXPECT_EQ(1u, object->GetInRenderContextMask());
}

TEST_F(RenderObjectContextTest, BaseZOrderIsTheNativeNoOpForAllSignedExtremes) {
    ProbeMasks();
    RCKRenderObject *object = Object();
    ASSERT_NE(nullptr, object);
    CKRenderObject *api = object;
    const int orders[] = {INT_MIN, -10000, -1, 0, 1, 10000, INT_MAX};
    for (int order : orders) {
        api->SetZOrder(order);
        EXPECT_EQ(0, api->GetZOrder());
    }
    EXPECT_EQ(0u, object->GetInRenderContextMask());
}

TEST_F(RenderObjectContextTest, ThreeRealContextsKeepIndependentBitsIncludingTheSignBit) {
    ProbeMasks();
    RCKRenderObject *object = Object();
    ASSERT_NE(nullptr, object);
    EXPECT_EQ(0u, object->GetInRenderContextMask());
    EXPECT_FALSE(object->IsInRenderContext(First));
    EXPECT_FALSE(object->IsInRenderContext(Second));
    EXPECT_FALSE(object->IsInRenderContext(High));
    object->AddToRenderContext(High);
    EXPECT_TRUE(object->IsInRenderContext(High));
    EXPECT_EQ(0x80000000u, object->GetInRenderContextMask());
    object->AddToRenderContext(First);
    object->AddToRenderContext(Second);
    object->AddToRenderContext(First);
    EXPECT_EQ(0x80000005u, object->GetInRenderContextMask());
    object->RemoveFromRenderContext(Second);
    object->RemoveFromRenderContext(Second);
    EXPECT_EQ(0x80000001u, object->GetInRenderContextMask());
    EXPECT_FALSE(object->IsInRenderContext(Second));
    EXPECT_TRUE(object->IsInRenderContext(First));
    EXPECT_TRUE(object->IsInRenderContext(High));
    object->RemoveFromRenderContext(High);
    object->RemoveFromRenderContext(First);
    EXPECT_EQ(0u, object->GetInRenderContextMask());
}

TEST_F(RenderObjectContextTest, MembershipQueriesUseOverlapForCompositeAndZeroMasks) {
    ProbeMasks();
    RCKRenderObject *object = Object();
    ASSERT_NE(nullptr, object);
    object->AddToRenderContext(First);
    Second->m_MaskFree = 5;
    EXPECT_TRUE(object->IsInRenderContext(Second));
    object->AddToRenderContext(Second);
    EXPECT_EQ(5u, object->GetInRenderContextMask());
    object->RemoveFromRenderContext(First);
    EXPECT_EQ(4u, object->GetInRenderContextMask());
    EXPECT_TRUE(object->IsInRenderContext(Second));
    First->m_MaskFree = 0;
    EXPECT_FALSE(object->IsInRenderContext(First));
    object->AddToRenderContext(First);
    object->RemoveFromRenderContext(First);
    EXPECT_EQ(4u, object->GetInRenderContextMask());
    object->RemoveFromRenderContext(Second);
    EXPECT_EQ(0u, object->GetInRenderContextMask());
}

TEST_F(RenderObjectContextTest, PublicContextAttachmentDispatchesTheRealMembershipOverrides) {
    ProbeMasks();
    RCKRenderObject *object = Object();
    ASSERT_NE(nullptr, object);
    First->AddObject(object);
    First->AddObject(object);
    Second->AddObject(object);
    EXPECT_TRUE(First->IsObjectAttached(object));
    EXPECT_TRUE(Second->IsObjectAttached(object));
    EXPECT_EQ(5u, object->GetInRenderContextMask());
    First->RemoveObject(object);
    First->RemoveObject(object);
    EXPECT_FALSE(First->IsObjectAttached(object));
    EXPECT_TRUE(Second->IsObjectAttached(object));
    EXPECT_EQ(4u, object->GetInRenderContextMask());
    Second->RemoveObject(object);
    EXPECT_EQ(0u, object->GetInRenderContextMask());
}

TEST_F(RenderObjectContextTest, NormalEntityMembershipUpdatesNodeAndAncestorsWithoutAttachingChildren) {
    ProbeMasks();
    RCK3dEntity *parent = Entity(), *child = Entity();
    ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(API(parent), FALSE));
    ASSERT_NE(nullptr, parent->m_SceneGraphNode);
    ASSERT_NE(nullptr, child->m_SceneGraphNode);
    CKSceneGraphNode *root = Manager->GetRootNode();
    child->AddToRenderContext(High);
    parent->AddToRenderContext(First);
    EXPECT_EQ(0x80000000u, child->m_SceneGraphNode->m_RenderContextMask);
    EXPECT_EQ(0x80000000u, child->m_SceneGraphNode->m_EntityMask);
    EXPECT_EQ(1u, parent->m_SceneGraphNode->m_RenderContextMask);
    EXPECT_EQ(0x80000001u, parent->m_SceneGraphNode->m_EntityMask);
    EXPECT_EQ(0x80000001u, root->m_EntityMask);
    EXPECT_FALSE(child->IsInRenderContext(First));
    EXPECT_FALSE(parent->IsInRenderContext(High));
    child->RemoveFromRenderContext(High);
    EXPECT_EQ(1u, parent->m_SceneGraphNode->m_EntityMask);
    EXPECT_EQ(1u, root->m_EntityMask);
    parent->RemoveFromRenderContext(First);
    EXPECT_EQ(0u, root->m_EntityMask);
}

TEST_F(RenderObjectContextTest, StartMembershipWritesOnlyTheDirectNodeUntilNormalUpdate) {
    ProbeMasks();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CKSceneGraphNode *node = entity->m_SceneGraphNode;
    ASSERT_NE(nullptr, node);
    CKSceneGraphNode *root = Manager->GetRootNode();
    First->m_Start = TRUE;
    entity->AddToRenderContext(First);
    EXPECT_EQ(1u, entity->GetInRenderContextMask());
    EXPECT_EQ(1u, node->m_RenderContextMask);
    EXPECT_EQ(0u, node->m_EntityMask);
    EXPECT_EQ(0u, root->m_EntityMask);
    First->m_Start = FALSE;
    entity->AddToRenderContext(Second);
    EXPECT_EQ(5u, node->m_RenderContextMask);
    EXPECT_EQ(5u, node->m_EntityMask);
    EXPECT_EQ(5u, root->m_EntityMask);
    First->m_Start = TRUE;
    entity->RemoveFromRenderContext(First);
    EXPECT_EQ(4u, entity->GetInRenderContextMask());
    EXPECT_EQ(4u, node->m_RenderContextMask);
    EXPECT_EQ(5u, node->m_EntityMask);
    EXPECT_EQ(5u, root->m_EntityMask);
    First->m_Start = FALSE;
    entity->RemoveFromRenderContext(Second);
    EXPECT_EQ(0u, node->m_RenderContextMask);
    EXPECT_EQ(0u, node->m_EntityMask);
    EXPECT_EQ(0u, root->m_EntityMask);
}

TEST_F(RenderObjectContextTest, ClearAllChangesObjectMembershipWithoutTouchingSceneNodes) {
    ProbeMasks();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CKSceneGraphNode *node = entity->m_SceneGraphNode;
    ASSERT_NE(nullptr, node);
    entity->AddToRenderContext(First);
    Context->m_InClearAll = TRUE;
    entity->AddToRenderContext(Second);
    entity->RemoveFromRenderContext(First);
    Context->m_InClearAll = FALSE;
    EXPECT_EQ(4u, entity->GetInRenderContextMask());
    EXPECT_EQ(1u, node->m_RenderContextMask);
    EXPECT_EQ(1u, node->m_EntityMask);
    EXPECT_TRUE(entity->IsInRenderContext(Second));
    EXPECT_FALSE(entity->IsInRenderContext(First));
    entity->RemoveFromRenderContext(Second);
    EXPECT_EQ(0u, node->m_RenderContextMask);
    EXPECT_EQ(0u, node->m_EntityMask);
}

TEST_F(RenderObjectContextTest, SdlMembershipRecreatesAnAbsentSceneNodeAndRemovalKeepsTheMaskValid) {
    ProbeMasks();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, entity->m_SceneGraphNode);
    Manager->DeleteNode(entity->m_SceneGraphNode);
    entity->m_SceneGraphNode = nullptr;
    entity->RemoveFromRenderContext(First);
    EXPECT_EQ(0u, entity->GetInRenderContextMask());
    EXPECT_EQ(nullptr, entity->m_SceneGraphNode);
    entity->AddToRenderContext(High);
    ASSERT_NE(nullptr, entity->m_SceneGraphNode);
    EXPECT_EQ(0x80000000u, entity->GetInRenderContextMask());
    EXPECT_EQ(0x80000000u, entity->m_SceneGraphNode->m_RenderContextMask);
    EXPECT_EQ(0x80000000u, entity->m_SceneGraphNode->m_EntityMask);
    EXPECT_TRUE(entity->IsInRenderContext(High));
    entity->RemoveFromRenderContext(High);
    EXPECT_EQ(0u, entity->m_SceneGraphNode->m_EntityMask);
}

TEST_F(RenderObjectContextTest, ConstructedContextsAllocateIndependentBitsAndReleaseForRealReuse) {
    EXPECT_EQ(1u, First->m_MaskFree);
    EXPECT_EQ(2u, Second->m_MaskFree);
    EXPECT_EQ(4u, High->m_MaskFree);
    EXPECT_EQ(0xFFFFFFF8u, Manager->m_RenderContextMaskFree);
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    First->AddObject(entity);
    EXPECT_TRUE(entity->IsInRenderContext(First));
    EXPECT_FALSE(entity->IsInRenderContext(Second));
    EXPECT_FALSE(entity->IsInRenderContext(High));
    Second->AddObject(entity);
    EXPECT_EQ(3u, entity->GetInRenderContextMask());
    delete Second;
    Second = nullptr;
    EXPECT_EQ(1u, entity->GetInRenderContextMask());
    EXPECT_EQ(0xFFFFFFFAu, Manager->m_RenderContextMaskFree);
    Second = new RCKRenderContext(Context);
    EXPECT_EQ(2u, Second->m_MaskFree);
    EXPECT_FALSE(entity->IsInRenderContext(Second));
    EXPECT_TRUE(entity->IsInRenderContext(First));
    EXPECT_FALSE(entity->IsInRenderContext(High));
    Second->AddObject(entity);
    EXPECT_EQ(3u, entity->GetInRenderContextMask());
    EXPECT_EQ(3u, entity->m_SceneGraphNode->m_RenderContextMask);
    First->RemoveObject(entity);
    EXPECT_FALSE(entity->IsInRenderContext(First));
    EXPECT_TRUE(entity->IsInRenderContext(Second));
    EXPECT_EQ(2u, entity->GetInRenderContextMask());
}

} // namespace
