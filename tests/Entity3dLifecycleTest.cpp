#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "CKKeyframeData.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
CK3dEntity *API(CK3dEntity *entity) { return entity; }

class EntityLifecycleProbe : public RCK3dEntity {
public:
    static XObjectPointerArray *Animations(RCK3dEntity &object) {
        return object.*(&EntityLifecycleProbe::m_ObjectAnimations);
    }
    static VxMatrix *LastFrame(RCK3dEntity &object) {
        return object.*(&EntityLifecycleProbe::m_LastFrameMatrix);
    }
    static void RecordLayout(RCK3dEntity &object) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&object);
        std::size_t previousEnd = sizeof(RCKRenderObject);
#define RECORD_FIELD(field) \
        { \
            auto member = &EntityLifecycleProbe::field; \
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(object.*member)); \
            const std::size_t offset = static_cast<std::size_t>(address - begin); \
            EXPECT_GE(offset, previousEnd) << #field; \
            EXPECT_EQ(0u, address % alignof(decltype(object.*member))) << #field; \
            EXPECT_LE(offset + sizeof(object.*member), sizeof(RCK3dEntity)) << #field; \
            ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
            ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(object.*member))); \
            previousEnd = offset + sizeof(object.*member); \
        }
        RECORD_FIELD(m_Parent);
        RECORD_FIELD(m_Meshes);
        RECORD_FIELD(m_CurrentMesh);
        RECORD_FIELD(m_Place);
        RECORD_FIELD(m_ObjectAnimations);
        RECORD_FIELD(m_3dEntityFlags);
        RECORD_FIELD(m_LastFrameMatrix);
        RECORD_FIELD(m_Skin);
        RECORD_FIELD(m_LocalMatrix);
        RECORD_FIELD(m_WorldMatrix);
        RECORD_FIELD(m_MoveableFlags);
        RECORD_FIELD(m_InverseWorldMatrix);
        RECORD_FIELD(m_Children);
        RECORD_FIELD(m_LocalBoundingBox);
        RECORD_FIELD(m_WorldBoundingBox);
        RECORD_FIELD(m_HierarchicalBox);
        RECORD_FIELD(m_RenderExtents);
        RECORD_FIELD(m_SceneGraphNode);
#undef RECORD_FIELD
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("SDL3dEntitySize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDL3dEntityAlignment", static_cast<int>(alignof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLRenderObjectSize", static_cast<int>(sizeof(RCKRenderObject)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
    }
};

CKBOOL BorrowedCallback(CKRenderContext *, CKRenderObject *, void *) { return TRUE; }

class Entity3dLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    RCKMesh *Mesh() { return static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH)); }
    RCKObjectAnimation *Animation() {
        return static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    }
    static void DestroyOnly(CKContext &context, CKObject *object) {
        CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, context.DestroyObject(object, 0, &dependencies));
    }
};

TEST_F(Entity3dLifecycleTest, RegisteredIdentityDefaultStateAndActualArchitectureLayout) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    CKObject *object = entity;
    EXPECT_EQ(static_cast<void *>(entity), static_cast<void *>(object));
    EXPECT_EQ(33, object->GetClassID());
    EXPECT_EQ(CKCID_3DENTITY, RCK3dEntity::m_ClassID);
    EXPECT_EQ(object, Context->GetObject(object->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(object, CKCID_RENDEROBJECT));
    EXPECT_STREQ("3D Entity", RCK3dEntity::GetClassName());
    EXPECT_EQ(0u, entity->GetFlags());
    EXPECT_EQ(0x4000bu, entity->GetMoveableFlags());
    EXPECT_EQ(nullptr, entity->GetParent());
    EXPECT_EQ(nullptr, entity->GetCurrentMesh());
    EXPECT_EQ(nullptr, entity->GetSkin());
    EXPECT_EQ(nullptr, entity->GetReferencePlace());
    EXPECT_EQ(0, entity->GetChildrenCount());
    EXPECT_EQ(0, entity->GetMeshCount());
    EXPECT_EQ(0, entity->GetObjectAnimationCount());
    EXPECT_EQ(nullptr, EntityLifecycleProbe::Animations(*entity));
    EXPECT_EQ(nullptr, EntityLifecycleProbe::LastFrame(*entity));
    EXPECT_EQ(nullptr, entity->m_SceneGraphNode);
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) {
        const float expected = row == col ? 1.0f : 0.0f;
        EXPECT_EQ(expected, entity->GetWorldMatrix()[row][col]);
        EXPECT_EQ(expected, entity->GetLocalMatrix()[row][col]);
        EXPECT_EQ(expected, entity->GetInverseWorldMatrix()[row][col]);
    }
    EntityLifecycleProbe::RecordLayout(*entity);
}

TEST_F(Entity3dLifecycleTest, DirectFactoriesCopyNameAndDestroyOwnedSkinThroughVirtualBase) {
    char name[] = "Entity owner";
    RCK3dEntity *named = new RCK3dEntity(Context, name);
    CK3dEntity *factory = RCK3dEntity::CreateInstance(Context);
    ASSERT_NE(nullptr, factory); const CK_ID namedID = named->GetID(), factoryID = factory->GetID();
    name[0] = 'X'; EXPECT_STREQ("Entity owner", named->GetName());
    EXPECT_NE(namedID, factoryID); EXPECT_EQ(nullptr, factory->GetName());
    ASSERT_NE(nullptr, named->CreateSkin());
    ASSERT_TRUE(named->AddPreRenderCallBack(BorrowedCallback, nullptr, FALSE));
    delete static_cast<CKObject *>(named);
    EXPECT_EQ(nullptr, Context->GetObject(namedID));
    EXPECT_EQ(factory, Context->GetObject(factoryID));
    delete static_cast<CKObject *>(factory);
    EXPECT_EQ(nullptr, Context->GetObject(factoryID));
}

TEST_F(Entity3dLifecycleTest, RegisterPublishesNativeGuidNotificationsAndOperationDependencies) {
    CKClassDesc *description = CKGetClassDesc(CKCID_3DENTITY); ASSERT_NE(nullptr, description);
    description->ToBeNotify.Clear(); description->Parameter = CKGUID();
    RCK3dEntity::Register();
    EXPECT_EQ(CKPGUID_3DENTITY, description->Parameter);
    EXPECT_EQ(0x5b8a05d5u, description->Parameter.d1);
    EXPECT_EQ(0x31ea28d4u, description->Parameter.d2);
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_OBJECTANIMATION));
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_MESH));
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_3DENTITY));
    EXPECT_EQ(6u, description->DefaultCopyDependencies);
    EXPECT_EQ(4u, description->DefaultDeleteDependencies);
    EXPECT_EQ(7u, description->DefaultSaveDependencies);
    const int modes[] = {0, 1, 2, 3, 4, 5, -1};
    for (int mode : modes) {
        const int expected = mode == 1 || mode == 2 || mode == 4 ? 3 : 0;
        EXPECT_EQ(expected, RCK3dEntity::GetDependenciesCount(mode));
        EXPECT_STREQ("Meshes", RCK3dEntity::GetDependencies(0, mode));
        EXPECT_STREQ("Children", RCK3dEntity::GetDependencies(1, mode));
        EXPECT_STREQ("Animation", RCK3dEntity::GetDependencies(2, mode));
        EXPECT_EQ(nullptr, RCK3dEntity::GetDependencies(-1, mode));
        EXPECT_EQ(nullptr, RCK3dEntity::GetDependencies(3, mode));
    }
}

TEST_F(Entity3dLifecycleTest, MeshListDeduplicatesAndCurrentRemovalSelectsFirstRemainingBorrowedMesh) {
    RCK3dEntity *entity = Entity(); RCKMesh *first = Mesh(), *second = Mesh();
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    EXPECT_EQ(CK_OK, entity->AddMesh(first)); EXPECT_EQ(CK_OK, entity->AddMesh(first));
    EXPECT_EQ(1, entity->GetMeshCount()); EXPECT_EQ(first, entity->GetMesh(0));
    EXPECT_EQ(nullptr, entity->SetCurrentMesh(second, FALSE));
    EXPECT_EQ(second, entity->GetCurrentMesh()); EXPECT_EQ(1, entity->GetMeshCount());
    EXPECT_EQ(second, entity->SetCurrentMesh(first, TRUE));
    EXPECT_EQ(first, entity->GetCurrentMesh());
    EXPECT_EQ(CK_OK, entity->AddMesh(second));
    EXPECT_EQ(CK_OK, entity->RemoveMesh(first));
    EXPECT_EQ(second, entity->GetCurrentMesh()); EXPECT_EQ(second, entity->GetMesh(0));
    EXPECT_EQ(CKERR_NOTFOUND, entity->RemoveMesh(first));
    EXPECT_EQ(CK_OK, entity->RemoveMesh(second));
    EXPECT_EQ(nullptr, entity->GetCurrentMesh()); EXPECT_EQ(0, entity->GetMeshCount());
    EXPECT_EQ(nullptr, entity->GetMesh(-1)); EXPECT_EQ(nullptr, entity->GetMesh(0));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity->AddMesh(nullptr));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity->RemoveMesh(nullptr));
    const CK_ID firstID = first->GetID(), secondID = second->GetID();
    DestroyOnly(*Context, entity);
    EXPECT_EQ(first, Context->GetObject(firstID)); EXPECT_EQ(second, Context->GetObject(secondID));
}

TEST_F(Entity3dLifecycleTest, AnimationAttachmentAddsMissingScaleKeyDeduplicatesAndFollowsEntityScenes) {
    RCK3dEntity *entity = Entity(); RCKObjectAnimation *animation = Animation();
    CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE));
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, scene);
    scene->AddObject(entity);
    VxVector scale(2.0f, 3.0f, 4.0f); entity->SetScale(&scale, FALSE, TRUE);
    ASSERT_NE(nullptr, animation->CreateController(CKANIMATION_LINROT_CONTROL));
    animation->Set3dEntity(API(entity));
    EXPECT_EQ(API(entity), animation->Get3dEntity());
    EXPECT_EQ(1, entity->GetObjectAnimationCount());
    EXPECT_EQ(animation, entity->GetObjectAnimation(0));
    EXPECT_EQ(1, animation->GetSceneInCount());
    EXPECT_EQ(scene, animation->GetSceneIn(0));
    CKAnimController *controller = animation->GetScaleController();
    ASSERT_NE(nullptr, controller); ASSERT_EQ(1, controller->GetKeyCount());
    CKScaleKey *key = static_cast<CKScaleKey *>(controller->GetKey(0)); ASSERT_NE(nullptr, key);
    EXPECT_EQ(0.0f, key->TimeStep); EXPECT_NEAR(2.0f, key->Pos.x, 1.0e-5f);
    EXPECT_NEAR(3.0f, key->Pos.y, 1.0e-5f); EXPECT_NEAR(4.0f, key->Pos.z, 1.0e-5f);
    entity->AddObjectAnimation(animation);
    EXPECT_EQ(1, entity->GetObjectAnimationCount());
    EXPECT_EQ(1, animation->GetSceneInCount()); EXPECT_EQ(1, controller->GetKeyCount());
}

TEST_F(Entity3dLifecycleTest, AnimationRemovalClearsLastOwnedArrayAndUnlinksScenesWithoutDeletingAnimation) {
    RCK3dEntity *entity = Entity(); RCKObjectAnimation *first = Animation(), *second = Animation();
    CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE));
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, scene);
    scene->AddObject(entity); first->Set3dEntity(API(entity)); second->Set3dEntity(API(entity));
    ASSERT_EQ(2, entity->GetObjectAnimationCount());
    first->Set3dEntity(nullptr);
    EXPECT_EQ(1, entity->GetObjectAnimationCount()); EXPECT_EQ(second, entity->GetObjectAnimation(0));
    EXPECT_EQ(0, first->GetSceneInCount());
    second->Set3dEntity(nullptr);
    EXPECT_EQ(0, entity->GetObjectAnimationCount());
    EXPECT_EQ(nullptr, EntityLifecycleProbe::Animations(*entity));
    EXPECT_EQ(0, second->GetSceneInCount());
    EXPECT_EQ(first, Context->GetObject(first->GetID()));
    EXPECT_EQ(second, Context->GetObject(second->GetID()));
    EXPECT_EQ(nullptr, entity->GetObjectAnimation(-1)); EXPECT_EQ(nullptr, entity->GetObjectAnimation(0));
    entity->RemoveObjectAnimation(nullptr); entity->AddObjectAnimation(nullptr);
    EXPECT_EQ(nullptr, EntityLifecycleProbe::Animations(*entity));
}

TEST_F(Entity3dLifecycleTest, IsObjectUsedRecognizesObjectAnimationClassAndBothMeshQueries) {
    RCK3dEntity *entity = Entity(); RCKMesh *mesh = Mesh(), *absentMesh = Mesh();
    RCKObjectAnimation *animation = Animation(), *absentAnimation = Animation();
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, mesh); ASSERT_NE(nullptr, absentMesh);
    ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, absentAnimation);
    entity->AddMesh(mesh); animation->Set3dEntity(API(entity));
    EXPECT_TRUE(entity->IsObjectUsed(animation, CKCID_OBJECTANIMATION));
    EXPECT_FALSE(entity->IsObjectUsed(absentAnimation, CKCID_OBJECTANIMATION));
    EXPECT_TRUE(entity->IsObjectUsed(mesh, CKCID_MESH));
    EXPECT_TRUE(entity->IsObjectUsed(mesh, CKCID_PATCHMESH));
    EXPECT_FALSE(entity->IsObjectUsed(absentMesh, CKCID_MESH));
    EXPECT_FALSE(entity->IsObjectUsed(animation, CKCID_ANIMATION));
}

TEST_F(Entity3dLifecycleTest, SkinOwnershipReplacementAndDestroyResultDistinguishPresence) {
    new RCKRenderManager(Context);
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, entity->m_SceneGraphNode);
    EXPECT_FALSE(entity->DestroySkin());
    CKSkin *skin = entity->CreateSkin(); ASSERT_NE(nullptr, skin); EXPECT_EQ(skin, entity->GetSkin());
    skin->SetBoneCount(2); EXPECT_EQ(2, entity->GetSkin()->GetBoneCount());
    ASSERT_NE(nullptr, entity->CreateSkin());
    EXPECT_EQ(0, entity->GetSkin()->GetBoneCount());
    EXPECT_TRUE(entity->DestroySkin()); EXPECT_EQ(nullptr, entity->GetSkin());
    EXPECT_FALSE(entity->DestroySkin());
}

TEST_F(Entity3dLifecycleTest, DeletionNotificationRemovesResourceReferencesAndDeadSkinBones) {
    RCK3dEntity *entity = Entity(), *bone = Entity(); RCKMesh *mesh = Mesh();
    RCKObjectAnimation *animation = Animation();
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, bone); ASSERT_NE(nullptr, mesh); ASSERT_NE(nullptr, animation);
    entity->SetCurrentMesh(mesh, TRUE); animation->Set3dEntity(API(entity));
    CKSkin *skin = entity->CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1);
    skin->GetBoneData(0)->SetBone(API(bone));
    mesh->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    animation->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    bone->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    entity->CheckPreDeletion();
    EXPECT_EQ(0, entity->GetMeshCount()); EXPECT_EQ(nullptr, entity->GetCurrentMesh());
    EXPECT_EQ(0, entity->GetObjectAnimationCount()); EXPECT_EQ(nullptr, skin->GetBoneData(0)->GetBone());
    mesh->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    animation->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    bone->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    animation->Set3dEntity(nullptr);
}

TEST_F(Entity3dLifecycleTest, FlagsPreserveIndependentBitsAndLastFrameRegistrationTracksTransitions) {
    RCKRenderManager *manager = new RCKRenderManager(Context);
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    entity->SetFlags(CK_3DENTITY_UPDATELASTFRAME | CK_3DENTITY_IGNOREANIMATION);
    manager->SaveLastFrameMatrix(); ASSERT_NE(nullptr, EntityLifecycleProbe::LastFrame(*entity));
    entity->IgnoreAnimations(FALSE); EXPECT_FALSE(entity->AreAnimationIgnored());
    EXPECT_EQ(CK_3DENTITY_UPDATELASTFRAME, entity->GetFlags());
    EXPECT_NE(nullptr, EntityLifecycleProbe::LastFrame(*entity));
    entity->IgnoreAnimations(TRUE); EXPECT_TRUE(entity->AreAnimationIgnored());
    entity->SetFlags(CK_3DENTITY_IGNOREANIMATION);
    EXPECT_EQ(nullptr, EntityLifecycleProbe::LastFrame(*entity));
    manager->SaveLastFrameMatrix(); EXPECT_EQ(nullptr, EntityLifecycleProbe::LastFrame(*entity));
    const CKDWORD before = entity->GetMoveableFlags();
    entity->SetPickable(FALSE); EXPECT_FALSE(entity->IsPickable());
    EXPECT_EQ(before & ~VX_MOVEABLE_PICKABLE, entity->GetMoveableFlags());
    entity->SetPickable(TRUE); EXPECT_TRUE(entity->IsPickable());
    entity->SetRenderChannels(FALSE); EXPECT_FALSE(entity->AreRenderChannelsVisible());
    entity->SetRenderChannels(TRUE); EXPECT_TRUE(entity->AreRenderChannelsVisible());
    EXPECT_EQ(before, entity->GetMoveableFlags());
}

TEST_F(Entity3dLifecycleTest, DeletedBoneInvalidatesCachedSkinPointsBeforeRebindingWithNewInitialPosition) {
    RCK3dEntity *entity = Entity(), *bone = Entity(), *replacement = Entity();
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, bone); ASSERT_NE(nullptr, replacement);
    CKSkin *skin = entity->CreateSkin(); ASSERT_NE(nullptr, skin);
    skin->SetBoneCount(1); skin->SetVertexCount(1);
    skin->GetBoneData(0)->SetBone(API(bone));
    CKSkinVertexData *vertex = skin->GetVertexData(0); ASSERT_NE(nullptr, vertex);
    vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, 1.0f);
    VxVector initial(1.0f, 2.0f, 4.0f); vertex->SetInitialPos(initial);
    VxVector output;
    ASSERT_TRUE(skin->CalcPoints(1, reinterpret_cast<CKBYTE *>(&output), sizeof(output)));
    EXPECT_FLOAT_EQ(1.0f, output.x); EXPECT_FLOAT_EQ(2.0f, output.y); EXPECT_FLOAT_EQ(4.0f, output.z);
    bone->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    entity->CheckPreDeletion();
    EXPECT_EQ(nullptr, skin->GetBoneData(0)->GetBone());
    bone->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    initial.Set(3.0f, 5.0f, 7.0f); vertex->SetInitialPos(initial);
    skin->GetBoneData(0)->SetBone(API(replacement));
    ASSERT_TRUE(skin->CalcPoints(1, reinterpret_cast<CKBYTE *>(&output), sizeof(output)));
    EXPECT_FLOAT_EQ(3.0f, output.x); EXPECT_FLOAT_EQ(5.0f, output.y); EXPECT_FLOAT_EQ(7.0f, output.z);
}

TEST_F(Entity3dLifecycleTest, MoveableVisibilitySyncAndRenderOrderUseRealSceneGraphState) {
    new RCKRenderManager(Context); RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, entity->m_SceneGraphNode);
    const CKDWORD before = entity->GetMoveableFlags();
    entity->SetMoveableFlags(before & ~VX_MOVEABLE_VISIBLE);
    EXPECT_FALSE(entity->CKObject::IsVisible()); EXPECT_FALSE(entity->IsVisible());
    EXPECT_EQ((before & ~VX_MOVEABLE_VISIBLE) | VX_MOVEABLE_HIERARCHICALHIDE,
              entity->ModifyMoveableFlags(VX_MOVEABLE_HIERARCHICALHIDE, 0));
    EXPECT_NE(0u, entity->GetObjectFlags() & CK_OBJECT_HIERACHICALHIDE);
    entity->ModifyMoveableFlags(VX_MOVEABLE_VISIBLE, VX_MOVEABLE_HIERARCHICALHIDE);
    EXPECT_TRUE(entity->IsVisible()); EXPECT_EQ(0u, entity->GetObjectFlags() & CK_OBJECT_HIERACHICALHIDE);
    entity->SetZOrder(-123); EXPECT_EQ(-123, entity->GetZOrder());
    entity->SetZOrder(50000); EXPECT_EQ(10000, entity->GetZOrder());
    entity->SetZOrder(-50000); EXPECT_EQ(-10000, entity->GetZOrder());
    entity->ModifyMoveableFlags(VX_MOVEABLE_RENDERFIRST, 0); EXPECT_EQ(10000, entity->GetZOrder());
    entity->ModifyMoveableFlags(0, VX_MOVEABLE_RENDERFIRST); EXPECT_EQ(0, entity->GetZOrder());
    EXPECT_FALSE(entity->IsAllInsideFrustrum()); EXPECT_FALSE(entity->IsAllOutsideFrustrum());
    entity->m_SceneGraphNode->SetAsInsideFrustum();
    EXPECT_TRUE(entity->IsAllInsideFrustrum()); EXPECT_FALSE(entity->IsAllOutsideFrustrum());
    entity->m_SceneGraphNode->SetAsOutsideFrustum();
    // Native marks accumulate independently until a new test pass clears both.
    EXPECT_TRUE(entity->IsAllInsideFrustrum()); EXPECT_TRUE(entity->IsAllOutsideFrustrum());
    entity->m_SceneGraphNode->SetAsPotentiallyVisible();
    EXPECT_FALSE(entity->IsAllInsideFrustrum()); EXPECT_FALSE(entity->IsAllOutsideFrustrum());
    entity->m_SceneGraphNode->SetAsOutsideFrustum();
    EXPECT_FALSE(entity->IsAllInsideFrustrum()); EXPECT_TRUE(entity->IsAllOutsideFrustrum());
}

TEST_F(Entity3dLifecycleTest, RenderEligibilityCombinesVisibilityMeshFlagsAndOwnedCallbackContainer) {
    RCK3dEntity *entity = Entity(); RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, mesh);
    EXPECT_FALSE(entity->IsToBeRendered()); EXPECT_FALSE(entity->IsToBeRenderedLast());
    entity->SetCurrentMesh(mesh, TRUE); mesh->SetFlags(mesh->GetFlags() | VXMESH_VISIBLE);
    EXPECT_TRUE(entity->IsToBeRendered());
    mesh->SetFlags(mesh->GetFlags() & ~VXMESH_VISIBLE); EXPECT_FALSE(entity->IsToBeRendered());
    entity->AddPostRenderCallBack(BorrowedCallback, nullptr, FALSE); EXPECT_TRUE(entity->IsToBeRendered());
    entity->SetRenderAsTransparent(TRUE); EXPECT_TRUE(entity->IsToBeRenderedLast());
    entity->ModifyMoveableFlags(VX_MOVEABLE_RENDERFIRST, 0); EXPECT_FALSE(entity->IsToBeRenderedLast());
    entity->ModifyMoveableFlags(0, VX_MOVEABLE_RENDERFIRST);
    entity->SetRenderAsTransparent(FALSE); EXPECT_FALSE(entity->IsToBeRenderedLast());
    entity->Show(CKHIDE); EXPECT_FALSE(entity->IsToBeRendered());
    entity->Show(CKSHOW); EXPECT_TRUE(entity->IsToBeRendered());
    entity->RemoveAllCallbacks(); EXPECT_FALSE(entity->IsToBeRendered());
}

TEST_F(Entity3dLifecycleTest, MemoryAccountsInlineMeshChildAndCallbackStorageWithoutOwningReferencedObjects) {
    RCK3dEntity *entity = Entity(), *child = Entity(); RCKMesh *mesh = Mesh();
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, mesh);
    const int initial = entity->GetMemoryOccupation();
    EXPECT_EQ(static_cast<int>(sizeof(RCK3dEntity) - sizeof(RCKRenderObject)),
              initial - entity->RCKRenderObject::GetMemoryOccupation());
    entity->AddMesh(mesh); entity->AddChild(API(child), TRUE);
    EXPECT_GT(entity->GetMemoryOccupation(), initial);
    const int resources = entity->GetMemoryOccupation();
    entity->AddPreRenderCallBack(BorrowedCallback, nullptr, FALSE);
    EXPECT_GT(entity->GetMemoryOccupation(), resources);
    entity->RemoveAllCallbacks(); EXPECT_EQ(resources, entity->GetMemoryOccupation());
    const CK_ID childID = child->GetID(), meshID = mesh->GetID();
    DestroyOnly(*Context, entity);
    EXPECT_EQ(child, Context->GetObject(childID)); EXPECT_EQ(mesh, Context->GetObject(meshID));
    EXPECT_EQ(nullptr, child->GetParent());
}

} // namespace
