#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKSceneGraph.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
CK3dEntity *ObjectAPI(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
CK2dEntity *ObjectAPI(RCK2dEntity *entity) { return reinterpret_cast<CK2dEntity *>(entity); }
class ObjectTraversalContext : public RCKRenderContext {
public:
    CKRenderObject *Visited[16] = {};
    int Visits = 0;
    explicit ObjectTraversalContext(CKContext *context) : RCKRenderContext(context) {}
    void AddObject(CKRenderObject *object) override {
        EXPECT_LT(Visits, 16);
        if (Visits < 16) Visited[Visits++] = object;
        RCKRenderContext::AddObject(object);
    }
};

class DependencyRefusalEntity : public RCK3dEntity {
public:
    int Calls = 0;
    explicit DependencyRefusalEntity(CKContext *context) : RCK3dEntity(context) {}
    CKERROR PrepareDependencies(CKDependenciesContext &) override {
        ++Calls;
        return CKERR_INVALIDPARAMETER;
    }
};

class RenderContextObjectCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *View = nullptr;
    XClassArray<CKRasterizerInfo> SavedRasterizers;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        SavedRasterizers = g_RasterizersInfo; g_RasterizersInfo.Clear();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        View = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        ASSERT_NE(nullptr, View); View->DetachAll();
    }
    void TearDown() override {
        if (View) View->m_RasterizerContext = nullptr;
        if (Context) CKCloseContext(Context);
        g_RasterizersInfo = SavedRasterizers; CKShutdown();
    }
    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }
    RCKRenderContext *OtherView() {
        RCKRenderContext *result = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        if (result) result->DetachAll();
        return result;
    }
};

TEST_F(RenderContextObjectCompletionTest, BatchedMembershipRebuildsCombinedMasksWithoutRemovingTheOtherContext) {
    RCKRenderContext *other = OtherView(); RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, other); ASSERT_NE(nullptr, entity);
    other->AddObject(entity); ASSERT_NE(nullptr, entity->m_SceneGraphNode);
    CKSceneGraphNode *node = entity->m_SceneGraphNode;
    const CKDWORD otherMask = other->m_MaskFree, both = otherMask | View->m_MaskFree;
    ASSERT_EQ(otherMask, node->m_EntityMask);
    View->AddRemoveSequence(7); View->AddObject(entity);
    EXPECT_EQ(7, View->m_Start); EXPECT_TRUE(View->IsObjectAttached(entity));
    EXPECT_EQ(both, node->m_RenderContextMask); EXPECT_EQ(otherMask, node->m_EntityMask);
    View->AddRemoveSequence(FALSE);
    EXPECT_FALSE(View->m_Start); EXPECT_EQ(both, node->m_EntityMask);
    EXPECT_EQ(both, Manager->m_SceneGraphRootNode.m_EntityMask & both);
    View->AddRemoveSequence(TRUE); View->RemoveObject(entity);
    EXPECT_FALSE(View->IsObjectAttached(entity)); EXPECT_TRUE(other->IsObjectAttached(entity));
    EXPECT_EQ(otherMask, node->m_RenderContextMask); EXPECT_EQ(both, node->m_EntityMask);
    View->AddRemoveSequence(FALSE);
    EXPECT_EQ(otherMask, node->m_EntityMask);
    EXPECT_EQ(otherMask, Manager->m_SceneGraphRootNode.m_EntityMask & both);
}

TEST_F(RenderContextObjectCompletionTest, EndingAnAdditionRemovalSequenceUpdatesTraversalEligibilityAndCanRepeat) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    View->AddObject(entity); ASSERT_NE(nullptr, entity->m_SceneGraphNode);
    EXPECT_TRUE(entity->m_SceneGraphNode->IsToBeParsed());
    View->AddRemoveSequence(TRUE); View->RemoveObject(entity);
    EXPECT_TRUE(entity->m_SceneGraphNode->IsToBeParsed());
    View->AddRemoveSequence(FALSE);
    EXPECT_FALSE(entity->m_SceneGraphNode->IsToBeParsed());
    EXPECT_EQ(0u, Manager->m_SceneGraphRootNode.m_EntityMask & View->m_MaskFree);
    View->AddRemoveSequence(FALSE);
    EXPECT_FALSE(entity->m_SceneGraphNode->IsToBeParsed());
    View->AddObject(entity);
    EXPECT_TRUE(entity->m_SceneGraphNode->IsToBeParsed());
}

TEST_F(RenderContextObjectCompletionTest, HierarchyTraversesStoredChildrenDepthFirstAndRepeatedAttachmentAddsNoNodes) {
    ObjectTraversalContext traversal(Context); traversal.DetachAll();
    RCK3dEntity *parent = Entity(), *first = Entity(), *grandchild = Entity(), *second = Entity();
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, grandchild); ASSERT_NE(nullptr, second);
    ASSERT_TRUE(first->SetParent(ObjectAPI(parent))); ASSERT_TRUE(grandchild->SetParent(ObjectAPI(first)));
    ASSERT_TRUE(second->SetParent(ObjectAPI(parent)));
    traversal.AddObjectWithHierarchy(parent);
    ASSERT_EQ(4, traversal.Visits);
    CKRenderObject *order[] = {parent, first, grandchild, second};
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(order[i], traversal.Visited[i]); EXPECT_TRUE(traversal.IsObjectAttached(order[i]));
    }
    const int rootNodes = Manager->m_SceneGraphRootNode.m_Children.Size();
    traversal.AddObjectWithHierarchy(parent);
    EXPECT_EQ(8, traversal.Visits); EXPECT_EQ(rootNodes, Manager->m_SceneGraphRootNode.m_Children.Size());
    traversal.AddObjectWithHierarchy(nullptr); EXPECT_EQ(8, traversal.Visits);
}

TEST_F(RenderContextObjectCompletionTest, AlreadyAttachedAncestorStillVisitsNewDescendants) {
    RCK3dEntity *parent = Entity(), *child = Entity(); ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child);
    View->AddObjectWithHierarchy(parent); ASSERT_TRUE(View->IsObjectAttached(parent));
    ASSERT_TRUE(child->SetParent(ObjectAPI(parent))); ASSERT_FALSE(View->IsObjectAttached(child));
    View->AddObjectWithHierarchy(parent);
    EXPECT_TRUE(View->IsObjectAttached(parent)); EXPECT_TRUE(View->IsObjectAttached(child));
    EXPECT_EQ(ObjectAPI(parent), child->GetParent());
}

TEST_F(RenderContextObjectCompletionTest, TwoDimensionalChildrenAreNotRecursedByTheThreeDimensionalHierarchyEntry) {
    RCK2dEntity *parent = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
    RCK2dEntity *child = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
    ASSERT_NE(nullptr, parent); ASSERT_NE(nullptr, child); ASSERT_TRUE(child->SetParent(ObjectAPI(parent)));
    View->AddObjectWithHierarchy(parent);
    EXPECT_TRUE(View->IsObjectAttached(parent)); EXPECT_FALSE(View->IsObjectAttached(child));
    EXPECT_EQ(ObjectAPI(parent), child->GetParent());
}

TEST_F(RenderContextObjectCompletionTest, DetachClearsOwnedListsCameraAndExtentsWhileRetainingOtherContextMembershipAndPhysicalState) {
    RCKRenderContext *other = OtherView(); RCK3dEntity *entity = Entity();
    RCK2dEntity *overlay = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
    RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    RCKLight *light = static_cast<RCKLight *>(Context->CreateObject(CKCID_LIGHT));
    ASSERT_NE(nullptr, other); ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, overlay);
    ASSERT_NE(nullptr, camera); ASSERT_NE(nullptr, light);
    View->AddObject(entity); View->AddObject(overlay); View->AddObject(camera); View->AddObject(light);
    other->AddObject(entity); View->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(camera));
    CKObjectExtents extent = {}; extent.m_Entity = ObjectAPI(entity); View->m_ObjectExtents.PushBack(extent);
    MaterialRenderObserver physical; physical.States[VXRENDERSTATE_ZENABLE] = 19;
    View->m_RasterizerContext = &physical;
    View->DetachAll();
    EXPECT_FALSE(View->IsObjectAttached(entity)); EXPECT_FALSE(View->IsObjectAttached(overlay));
    EXPECT_FALSE(View->IsObjectUsed(camera, CKCID_CAMERA)); EXPECT_FALSE(View->IsObjectUsed(light, CKCID_LIGHT));
    EXPECT_TRUE(other->IsObjectAttached(entity)); EXPECT_TRUE(other->IsObjectUsed(entity, CKCID_3DENTITY));
    EXPECT_EQ(nullptr, View->GetAttachedCamera()); EXPECT_EQ(0, View->m_ObjectExtents.Size());
    EXPECT_EQ(19u, physical.States[VXRENDERSTATE_ZENABLE]); EXPECT_EQ(0, physical.StateWrites);
    View->m_RasterizerContext = nullptr;
}

TEST_F(RenderContextObjectCompletionTest, DetachCanRepeatAndObjectsCanBeAttachedAgain) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    View->AddObject(entity); View->DetachAll(); View->DetachAll();
    EXPECT_FALSE(View->IsObjectUsed(entity, CKCID_3DENTITY));
    View->AddObject(entity);
    EXPECT_TRUE(View->IsObjectAttached(entity)); EXPECT_TRUE(View->IsObjectUsed(entity, CKCID_3DENTITY));
}

TEST_F(RenderContextObjectCompletionTest, ExtentDeletionSweepClearsEveryDoomedReferenceAndKeepsAssociatedData) {
    View->CheckObjectExtents();
    RCK3dEntity *live = Entity(), *doomed = Entity(); ASSERT_NE(nullptr, live); ASSERT_NE(nullptr, doomed);
    CKObjectExtents item = {}; item.m_Rect = VxRect(1.25f, 2.5f, 3.75f, 5); item.m_Camera = 93;
    item.m_Entity = ObjectAPI(live); View->m_ObjectExtents.PushBack(item);
    item.m_Entity = ObjectAPI(doomed); View->m_ObjectExtents.PushBack(item);
    item.m_Entity = nullptr; View->m_ObjectExtents.PushBack(item);
    item.m_Entity = ObjectAPI(doomed); View->m_ObjectExtents.PushBack(item);
    doomed->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); View->CheckObjectExtents();
    ASSERT_EQ(4, View->m_ObjectExtents.Size()); EXPECT_EQ(ObjectAPI(live), View->m_ObjectExtents[0].m_Entity);
    for (int i = 1; i < 4; ++i) EXPECT_EQ(nullptr, View->m_ObjectExtents[i].m_Entity);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(93u, View->m_ObjectExtents[i].m_Camera);
        EXPECT_FLOAT_EQ(1.25f, View->m_ObjectExtents[i].m_Rect.left);
        EXPECT_FLOAT_EQ(5, View->m_ObjectExtents[i].m_Rect.bottom);
    }
    doomed->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); View->CheckObjectExtents();
    EXPECT_EQ(nullptr, View->m_ObjectExtents[1].m_Entity);
}

TEST_F(RenderContextObjectCompletionTest, NonBuildModesCollectOnlyTheContextWithoutTraversingItsScene) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity); View->AddObject(entity);
    const CK_DEPENDENCIES_OPMODE modes[] = {CK_DEPENDENCIES_COPY, CK_DEPENDENCIES_DELETE, CK_DEPENDENCIES_REPLACE, CK_DEPENDENCIES_SAVE};
    for (CK_DEPENDENCIES_OPMODE mode : modes) {
        SCOPED_TRACE(static_cast<int>(mode));
        CKDependencies options; CKCopyDefaultClassDependencies(options, mode); options.m_Flags = CK_DEPENDENCIES_FULL;
        CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(mode); dependencies.StartDependencies(&options);
        EXPECT_EQ(CK_OK, View->PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.IsDependenciesHere(View->GetID()));
        EXPECT_FALSE(dependencies.IsDependenciesHere(entity->GetID()));
        EXPECT_FALSE(dependencies.IsDependenciesHere(View->Get2dRoot(TRUE)->GetID()));
        EXPECT_FALSE(dependencies.IsDependenciesHere(View->GetBackgroundMaterial()->GetID()));
        EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
    }
}

TEST_F(RenderContextObjectCompletionTest, SaveBaseRefusalDoesNotCollectTheContextOrAnySceneDependencies) {
    CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE);
    View->ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, View->PrepareDependencies(dependencies));
    EXPECT_FALSE(dependencies.IsDependenciesHere(View->GetID()));
    EXPECT_EQ(0, dependencies.GetDependenciesMap().Size()); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
    View->ModifyObjectFlags(0, CK_OBJECT_NOTTOBESAVED);
}

TEST_F(RenderContextObjectCompletionTest, DynamicParentBaseRefusalPreservesTheExistingDependencyStack) {
    RCK3dEntity *dynamic = Entity(); ASSERT_NE(nullptr, dynamic); dynamic->ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    CKDependenciesContext dependencies(Context); dependencies.m_DynamicObjects.PushBack(dynamic);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, View->PrepareDependencies(dependencies));
    EXPECT_EQ(0, dependencies.GetDependenciesMap().Size());
    ASSERT_EQ(1, dependencies.m_DynamicObjects.Size()); EXPECT_EQ(dynamic, dependencies.m_DynamicObjects.Back());
    dependencies.m_DynamicObjects.Clear();
}

TEST_F(RenderContextObjectCompletionTest, BuildContinuesAfterChildRefusalAndFinishesTheContextStack) {
    DependencyRefusalEntity refused(Context); RCK3dEntity *accepted = Entity(); ASSERT_NE(nullptr, accepted);
    View->AddObject(&refused); View->AddObject(accepted);
    CKDependencies options; CKCopyDefaultClassDependencies(options, CK_DEPENDENCIES_BUILD);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK, View->PrepareDependencies(dependencies));
    EXPECT_EQ(1, refused.Calls); EXPECT_FALSE(dependencies.IsDependenciesHere(refused.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(accepted->GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(View->GetBackgroundMaterial()->GetID()));
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
    dependencies.StopDependencies(); View->RemoveObject(&refused);
}

TEST_F(RenderContextObjectCompletionTest, RepeatedDependencyPreparationStopsAtTheBaseWithoutRevisitingChildren) {
    DependencyRefusalEntity child(Context); View->AddObject(&child);
    CKDependencies options; CKCopyDefaultClassDependencies(options, CK_DEPENDENCIES_BUILD);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, View->PrepareDependencies(dependencies)); ASSERT_EQ(1, child.Calls);
    const int count = dependencies.GetDependenciesMap().Size();
    EXPECT_EQ(CKERR_ALREADYPRESENT, View->PrepareDependencies(dependencies));
    EXPECT_EQ(1, child.Calls); EXPECT_EQ(count, dependencies.GetDependenciesMap().Size());
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
    dependencies.StopDependencies(); View->RemoveObject(&child);
}
} // namespace
