#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SceneProbeContext : public RCKRenderContext {
public:
    explicit SceneProbeContext(CKContext *context) : RCKRenderContext(context) {}
    int Renders = 0;
    int Added = 0;
    int Removed = 0;
    int Detaches = 0;
    CK_RENDER_FLAGS LastFlags = CK_RENDER_DEFAULTSETTINGS;
    CKRenderObject *LastObject = nullptr;

    CKERROR Render(CK_RENDER_FLAGS flags = CK_RENDER_USECURRENTSETTINGS) override {
        ++Renders;
        LastFlags = flags;
        return CK_OK;
    }
    void AddObject(CKRenderObject *object) override {
        ++Added;
        LastObject = object;
        RCKRenderContext::AddObject(object);
    }
    void RemoveObject(CKRenderObject *object) override {
        ++Removed;
        LastObject = object;
        RCKRenderContext::RemoveObject(object);
    }
    void DetachAll() override {
        ++Detaches;
        RCKRenderContext::DetachAll();
    }
};

void CountCallback(CKRenderContext *, void *argument) {
    ++*static_cast<int *>(argument);
}

CKBOOL CountObjectCallback(CKRenderContext *, CKRenderObject *, void *argument) {
    ++*static_cast<int *>(argument);
    return TRUE;
}

void CountMeshCallback(CKRenderContext *, CK3dEntity *, CKMesh *, void *argument) {
    ++*static_cast<int *>(argument);
}

class RenderManagerSceneTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }
    SceneProbeContext *Device() {
        SceneProbeContext *device = new SceneProbeContext(Context);
        Manager->m_RenderContexts.PushBack(device->GetID());
        return device;
    }
    static CKRenderObject *API(RCK3dEntity *entity) {
        return reinterpret_cast<CKRenderObject *>(entity);
    }
    static void Position(RCK3dEntity *entity, float x, float y, float z) {
        const VxVector position(x, y, z);
        entity->SetPosition(&position);
    }
    static void ExpectMatrix(const VxMatrix &expected, const VxMatrix &actual) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_FLOAT_EQ(expected[row][column], actual[row][column]);
    }
};

TEST_F(RenderManagerSceneTest, FrameBoundarySnapshotsTrackedEntitiesAndCompactsOnlySettledMoves) {
    RCK3dEntity *settled = Entity(), *changed = Entity();
    ASSERT_NE(nullptr, settled);
    ASSERT_NE(nullptr, changed);
    changed->SetFlags(changed->GetFlags() | CK_3DENTITY_UPDATELASTFRAME);
    Position(settled, 1, 2, 3);
    Position(changed, 4, 5, 6);
    ASSERT_EQ(2, Manager->m_MovedEntities.Size());
    ASSERT_EQ(1, Manager->m_Entities.Size());
    EXPECT_EQ(CK_OK, Manager->PostProcess());
    EXPECT_NE(0u, settled->m_MoveableFlags & VX_MOVEABLE_RESERVED2);
    EXPECT_NE(0u, changed->m_MoveableFlags & VX_MOVEABLE_RESERVED2);

    // Moving after PostProcess invalidates the settled marker, keeping the
    // entity in the next frame's moved list while the other entry expires.
    Position(changed, 7, 8, 9);
    const VxMatrix current = changed->GetWorldMatrix();
    EXPECT_EQ(CK_OK, Manager->PreProcess());
    ASSERT_NE(nullptr, changed->m_LastFrameMatrix);
    ExpectMatrix(current, *changed->m_LastFrameMatrix);
    EXPECT_EQ(0u, settled->m_MoveableFlags & (VX_MOVEABLE_HASMOVED | VX_MOVEABLE_RESERVED2));
    EXPECT_NE(0u, changed->m_MoveableFlags & VX_MOVEABLE_HASMOVED);
    EXPECT_EQ(0u, changed->m_MoveableFlags & VX_MOVEABLE_RESERVED2);
    ASSERT_EQ(1, Manager->m_MovedEntities.Size());
    EXPECT_EQ(changed, Manager->m_MovedEntities[0]);

    EXPECT_EQ(CK_OK, Manager->PostProcess());
    EXPECT_EQ(CK_OK, Manager->PreProcess());
    EXPECT_EQ(0, Manager->m_MovedEntities.Size());
    EXPECT_EQ(0u, changed->m_MoveableFlags & (VX_MOVEABLE_HASMOVED | VX_MOVEABLE_RESERVED2));
    changed->SetFlags(changed->GetFlags() & ~CK_3DENTITY_UPDATELASTFRAME);
    EXPECT_EQ(0, Manager->m_Entities.Size());
    EXPECT_EQ(nullptr, changed->m_LastFrameMatrix);
}

TEST_F(RenderManagerSceneTest, SaveLastFrameUpdatesOnlyRegisteredHistoryWithoutChangingCurrentPose) {
    RCK3dEntity *tracked = Entity(), *ordinary = Entity();
    ASSERT_NE(nullptr, tracked);
    ASSERT_NE(nullptr, ordinary);
    tracked->SetFlags(tracked->GetFlags() | CK_3DENTITY_UPDATELASTFRAME);
    Position(tracked, 10, 20, 30);
    Position(ordinary, 1, 2, 3);
    const VxMatrix first = tracked->GetWorldMatrix();
    Manager->SaveLastFrameMatrix();
    ASSERT_NE(nullptr, tracked->m_LastFrameMatrix);
    ExpectMatrix(first, *tracked->m_LastFrameMatrix);
    EXPECT_EQ(nullptr, ordinary->m_LastFrameMatrix);
    Position(tracked, -1, -2, -3);
    ExpectMatrix(first, *tracked->m_LastFrameMatrix);
    const VxMatrix second = tracked->GetWorldMatrix();
    Manager->SaveLastFrameMatrix();
    ExpectMatrix(second, *tracked->m_LastFrameMatrix);
    ExpectMatrix(second, tracked->GetWorldMatrix());
}

TEST_F(RenderManagerSceneTest, ProcessDispatchesEachLiveContextWithItsCurrentRenderSettings) {
    SceneProbeContext *first = Device(), *second = Device();
    // A removed object ID must not prevent subsequent live contexts rendering.
    CKObject *removed = Context->CreateObject(CKCID_OBJECT);
    ASSERT_NE(nullptr, removed);
    const CK_ID removedID = removed->GetID();
    ASSERT_EQ(CK_OK, Context->DestroyObject(removed));
    Manager->m_RenderContexts.Insert(Manager->m_RenderContexts.Begin() + 1, removedID);
    Manager->Process();
    EXPECT_EQ(1, first->Renders);
    EXPECT_EQ(1, second->Renders);
    EXPECT_EQ(CK_RENDER_USECURRENTSETTINGS, first->LastFlags);
    EXPECT_EQ(CK_RENDER_USECURRENTSETTINGS, second->LastFlags);
    EXPECT_EQ(CK_OK, Manager->SequenceDeleted(nullptr, 0));
    ASSERT_EQ(2, Manager->m_RenderContexts.Size());
    EXPECT_EQ(first->GetID(), Manager->m_RenderContexts[0]);
    EXPECT_EQ(second->GetID(), Manager->m_RenderContexts[1]);
}

TEST_F(RenderManagerSceneTest, PostProcessClearsEveryContextExtentAndPauseClearsVisibilityInformation) {
    SceneProbeContext *first = Device(), *second = Device();
    first->m_Extents.Resize(2);
    second->m_Extents.Resize(1);
    first->m_PVInformation = 0x12345678u;
    second->m_PVInformation = 0x87654321u;
    EXPECT_EQ(CK_OK, Manager->PostProcess());
    EXPECT_EQ(0, first->m_Extents.Size());
    EXPECT_EQ(0, second->m_Extents.Size());
    EXPECT_EQ(0x12345678u, first->m_PVInformation);
    EXPECT_EQ(CK_OK, Manager->OnCKPause());
    EXPECT_EQ(0u, first->m_PVInformation);
    EXPECT_EQ(0u, second->m_PVInformation);
}

TEST_F(RenderManagerSceneTest, SceneSequencesFilterCurrentSceneAndOnlyUseTheLevelContexts) {
    CKLevel *level = static_cast<CKLevel *>(Context->CreateObject(CKCID_LEVEL));
    CKScene *other = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE));
    RCK3dEntity *entity = Entity();
    CKObject *plain = Context->CreateObject(CKCID_OBJECT);
    ASSERT_NE(nullptr, level);
    ASSERT_NE(nullptr, other);
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, plain);
    Context->SetCurrentLevel(level);
    SceneProbeContext *active = Device(), *outside = Device();
    level->AddRenderContext(active);
    active->Added = 0;
    active->Removed = 0;
    const CK_ID ids[3] = {plain->GetID(), 0, entity->GetID()};
    CK_ID sequence[3] = {ids[0], ids[1], ids[2]};
    EXPECT_EQ(CK_OK, Manager->SequenceAddedToScene(other, sequence, 3));
    EXPECT_EQ(0, active->Added);
    EXPECT_EQ(CK_OK, Manager->SequenceAddedToScene(level->GetCurrentScene(), sequence, 3));
    EXPECT_EQ(1, active->Added);
    EXPECT_EQ(API(entity), active->LastObject);
    EXPECT_TRUE(active->IsObjectAttached(API(entity)));
    EXPECT_EQ(0, outside->Added);
    EXPECT_EQ(CK_OK, Manager->SequenceRemovedFromScene(other, sequence, 3));
    EXPECT_EQ(0, active->Removed);
    EXPECT_TRUE(active->IsObjectAttached(API(entity)));
    EXPECT_EQ(CK_OK, Manager->SequenceRemovedFromScene(level->GetCurrentScene(), sequence, 3));
    EXPECT_EQ(1, active->Removed);
    EXPECT_FALSE(active->IsObjectAttached(API(entity)));
    EXPECT_EQ(0, outside->Removed);
}

TEST_F(RenderManagerSceneTest, DetachAllResetsTrackingAndAttachmentMasksInEveryContext) {
    SceneProbeContext *first = Device(), *second = Device();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    first->AddObject(API(entity));
    second->AddObject(API(entity));
    entity->SetFlags(entity->GetFlags() | CK_3DENTITY_UPDATELASTFRAME);
    Position(entity, 1, 2, 3);
    first->SetCurrentRenderOptions(0x1234u);
    second->SetCurrentRenderOptions(0x4321u);
    const int firstBefore = first->Detaches, secondBefore = second->Detaches;
    Manager->DetachAllObjects();
    EXPECT_EQ(0, Manager->m_MovedEntities.Size());
    EXPECT_EQ(0, Manager->m_Entities.Size());
    EXPECT_EQ(firstBefore + 1, first->Detaches);
    EXPECT_EQ(secondBefore + 1, second->Detaches);
    EXPECT_FALSE(first->IsObjectAttached(API(entity)));
    EXPECT_FALSE(second->IsObjectAttached(API(entity)));
    EXPECT_EQ(0u, entity->GetInRenderContextMask());
    EXPECT_EQ(255u, first->GetCurrentRenderOptions());
    EXPECT_EQ(255u, second->GetCurrentRenderOptions());
}

TEST_F(RenderManagerSceneTest, DeletionNotificationsPruneTrackedPointersAndTransparentEntries) {
    RCK3dEntity *alive = Entity(), *removed = Entity();
    ASSERT_NE(nullptr, alive);
    ASSERT_NE(nullptr, removed);
    alive->SetFlags(alive->GetFlags() | CK_3DENTITY_UPDATELASTFRAME);
    removed->SetFlags(removed->GetFlags() | CK_3DENTITY_UPDATELASTFRAME);
    Position(alive, 1, 2, 3);
    Position(removed, 4, 5, 6);
    Manager->GetRootNode()->AddTransparentObject(removed->m_SceneGraphNode);
    Manager->GetRootNode()->AddTransparentObject(alive->m_SceneGraphNode);
    removed->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    EXPECT_EQ(CK_OK, Manager->SequenceToBeDeleted(nullptr, 0));
    ASSERT_EQ(1, Manager->m_Entities.Size());
    ASSERT_EQ(1, Manager->m_MovedEntities.Size());
    EXPECT_EQ(alive, Manager->m_Entities[0]);
    EXPECT_EQ(alive, Manager->m_MovedEntities[0]);
    ASSERT_EQ(1, Manager->GetRootNode()->m_TransparentObjects.Size());
    EXPECT_EQ(alive->m_SceneGraphNode, Manager->GetRootNode()->m_TransparentObjects[0].m_Node);
    EXPECT_FALSE(removed->m_SceneGraphNode->IsInTransparentList());
    removed->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}

TEST_F(RenderManagerSceneTest, NodeAllocationAndDeletionMaintainParentMembershipWithoutOwningChildren) {
    CKSceneGraphNode *first = Manager->CreateNode(nullptr);
    CKSceneGraphNode *second = Manager->CreateNode(nullptr);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    CKSceneGraphRootNode *root = Manager->GetRootNode();
    EXPECT_EQ(root, first->m_Parent);
    EXPECT_EQ(root, second->m_Parent);
    root->RemoveNode(second);
    first->AddNode(second);
    EXPECT_EQ(first, second->m_Parent);
    Manager->DeleteNode(first);
    EXPECT_EQ(nullptr, second->m_Parent);
    EXPECT_EQ(nullptr, second->m_Entity);
    EXPECT_EQ(0, root->m_Children.Size());
    Manager->DeleteNode(second);
    Manager->DeleteNode(nullptr);
}

TEST_F(RenderManagerSceneTest, EndRemovesSurvivingBackgroundRootWhenForegroundWasAlreadyDeleted) {
    const CK_ID foreground = Manager->m_2DRootForeId;
    const CK_ID background = Manager->m_2DRootBackId;
    ASSERT_NE(0u, foreground);
    ASSERT_NE(0u, background);
    ASSERT_EQ(CK_OK, Context->DestroyObject(foreground));
    ASSERT_NE(nullptr, Context->GetObject(background));
    EXPECT_EQ(CK_OK, Manager->OnCKEnd());
    EXPECT_EQ(nullptr, Context->GetObject(background));
    EXPECT_EQ(nullptr, Manager->m_2DRootFore);
    EXPECT_EQ(nullptr, Manager->m_2DRootBack);
    EXPECT_EQ(0u, Manager->m_2DRootForeId);
    EXPECT_EQ(0u, Manager->m_2DRootBackId);
    EXPECT_EQ(CK_OK, Manager->OnCKEnd());
}

TEST_F(RenderManagerSceneTest, PreClearAllClearsGraphTrackingCallbacksAttachmentsAndDefaultMaterial) {
    // PreClearAll enumerates exact-class lists, so use the public factory's
    // complete registration rather than a directly constructed probe object.
    RCKRenderContext *device = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
    ASSERT_NE(nullptr, device);
    Manager->m_RenderContexts.PushBack(device->GetID());
    const int contexts = Context->GetObjectsCountByClassID(CKCID_RENDERCONTEXT);
    CK_ID *contextIDs = Context->GetObjectsListByClassID(CKCID_RENDERCONTEXT);
    bool registered = false;
    for (int i = 0; i < contexts; ++i)
        if (contextIDs[i] == device->GetID()) registered = true;
    ASSERT_TRUE(registered);
    RCK3dEntity *entity = Entity();
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, mesh);
    device->AddObject(API(entity));
    entity->SetFlags(entity->GetFlags() | CK_3DENTITY_UPDATELASTFRAME);
    Position(entity, 1, 2, 3);
    int calls = 0;
    device->AddPreRenderCallBack(CountCallback, &calls, TRUE);
    ASSERT_TRUE(entity->AddPreRenderCallBack(CountObjectCallback, &calls, FALSE));
    ASSERT_TRUE(mesh->AddPreRenderCallBack(CountMeshCallback, &calls, FALSE));
    ASSERT_GT(Manager->m_TemporaryPreRenderCallbacks.Size(), 0);
    ASSERT_NE(nullptr, Manager->GetDefaultMaterial());
    EXPECT_EQ(CK_OK, Manager->PreClearAll());
    EXPECT_EQ(0, Manager->m_MovedEntities.Size());
    EXPECT_EQ(0, Manager->m_Entities.Size());
    EXPECT_EQ(0, Manager->GetRootNode()->m_Children.Size());
    EXPECT_EQ(nullptr, entity->m_SceneGraphNode->m_Parent);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    EXPECT_EQ(nullptr, Manager->m_DefaultMat);
    EXPECT_FALSE(device->IsObjectAttached(API(entity)));
    EXPECT_FALSE(entity->RemovePreRenderCallBack(CountObjectCallback, &calls));
    EXPECT_FALSE(mesh->RemovePreRenderCallBack(CountMeshCallback, &calls));
    device->m_PreRenderCallBacks.ExecutePreCallbacks(device);
    EXPECT_EQ(0, calls);
}

TEST_F(RenderManagerSceneTest, EventMaskSelectsExactlyTheImplementedLifecycleAndSceneHooks) {
    EXPECT_EQ(0x000C029Fu, Manager->GetValidFunctionsMask());
}
} // namespace
