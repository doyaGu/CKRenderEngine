#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class LifecycleAnimationProbe : public RCKAnimation {
public:
    explicit LifecycleAnimationProbe(CKContext *context, CKSTRING name = nullptr)
        : RCKAnimation(context, name) {}
    void References(RCK3dEntity *root, RCKCharacter *character) {
        m_RootEntity = root; m_Character = character;
    }
};

class LifecycleKeyedProbe : public RCKKeyedAnimation {
public:
    explicit LifecycleKeyedProbe(CKContext *context, CKSTRING name = nullptr)
        : RCKKeyedAnimation(context, name) {}
    void Character(RCKCharacter *character) { m_Character = character; }
    void EmptySlot() { m_Animations.PushBack(nullptr); }
};

class LifecycleChildProbe : public RCKObjectAnimation {
public:
    explicit LifecycleChildProbe(CKContext *context) : RCKObjectAnimation(context) {}
    RCKKeyedAnimation *Parent() const { return m_ParentKeyedAnimation; }
};

class AnimationLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    void DestroyOnly(CKObject *object) {
        CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        EXPECT_EQ(CK_OK, Context->DestroyObject(object, 0, &dependencies));
    }
    static void Position(LifecycleChildProbe &animation, RCK3dEntity &entity, const VxVector &value) {
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
        VxVector key = value; animation.AddPositionKey(0, &key); animation.SetLength(1);
    }
};

TEST_F(AnimationLifecycleTest, AnimationConstructorEstablishesUsableDefaultsAndOwnedName) {
    LifecycleAnimationProbe animation(Context, "Base lifecycle");
    EXPECT_EQ(CKCID_ANIMATION, animation.GetClassID()); EXPECT_STREQ("Base lifecycle", animation.GetName());
    EXPECT_EQ(&animation, Context->GetObject(animation.GetID()));
    EXPECT_FLOAT_EQ(100, animation.GetLength()); EXPECT_FLOAT_EQ(0, animation.GetStep());
    EXPECT_FLOAT_EQ(0, animation.GetFrame()); EXPECT_FLOAT_EQ(30, animation.GetLinkedFrameRate());
    EXPECT_EQ(CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK, animation.GetFlags());
    EXPECT_EQ(nullptr, animation.GetCharacter()); EXPECT_EQ(nullptr, animation.GetRootEntity());
    EXPECT_FLOAT_EQ(30, animation.GetNextFrame(1000));
}

TEST_F(AnimationLifecycleTest, KeyedConstructorHasNoChannelsOrCenteringAndCanPlayImmediately) {
    LifecycleKeyedProbe animation(Context, "Keyed lifecycle");
    EXPECT_EQ(CKCID_KEYEDANIMATION, animation.GetClassID()); EXPECT_STREQ("Keyed lifecycle", animation.GetName());
    EXPECT_EQ(&animation, Context->GetObject(animation.GetID())); EXPECT_EQ(0, animation.GetAnimationCount());
    EXPECT_FALSE(animation.IsMerged()); EXPECT_FLOAT_EQ(.5f, animation.GetMergeFactor());
    EXPECT_EQ(nullptr, animation.GetRootAnimationInternal());
    EXPECT_FLOAT_EQ(0, animation.GetRootVectorInternal().x); EXPECT_FLOAT_EQ(0, animation.GetRootVectorInternal().y);
    EXPECT_FLOAT_EQ(0, animation.GetRootVectorInternal().z); EXPECT_EQ(nullptr, animation.GetRootEntity());
    CKAnimation *api = &animation; api->SetFrame(25); EXPECT_FLOAT_EQ(.25f, api->GetStep());
    EXPECT_FLOAT_EQ(100, api->GetLength());
}

TEST_F(AnimationLifecycleTest, DirectFactoriesCreateCorrectUnnamedTypesAndUnregisterOnVirtualDeletion) {
    CKAnimation *base = RCKAnimation::CreateInstance(Context); ASSERT_NE(nullptr, base);
    CKAnimation *keyed = reinterpret_cast<CKAnimation *>(RCKKeyedAnimation::CreateInstance(Context));
    ASSERT_NE(nullptr, keyed); CK_ID baseID = base->GetID(), keyedID = keyed->GetID();
    EXPECT_NE(baseID, keyedID); EXPECT_EQ(base, Context->GetObject(baseID)); EXPECT_EQ(keyed, Context->GetObject(keyedID));
    EXPECT_EQ(CKCID_ANIMATION, base->GetClassID()); EXPECT_EQ(CKCID_KEYEDANIMATION, keyed->GetClassID());
    EXPECT_EQ(nullptr, base->GetName()); EXPECT_EQ(nullptr, keyed->GetName());
    EXPECT_TRUE(CKIsChildClassOf(keyed, CKCID_ANIMATION)); EXPECT_TRUE(CKIsChildClassOf(base, CKCID_SCENEOBJECT));
    delete base; delete keyed; EXPECT_EQ(nullptr, Context->GetObject(baseID)); EXPECT_EQ(nullptr, Context->GetObject(keyedID));
}

TEST_F(AnimationLifecycleTest, ClassRegistrationNamesFactoriesAndInheritanceDriveContextCreation) {
    CKClassDesc *base = CKGetClassDesc(CKCID_ANIMATION), *keyed = CKGetClassDesc(CKCID_KEYEDANIMATION);
    ASSERT_NE(nullptr, base); ASSERT_NE(nullptr, keyed);
    EXPECT_EQ(CKCID_SCENEOBJECT, base->Parent); EXPECT_EQ(CKCID_ANIMATION, keyed->Parent);
    ASSERT_NE(nullptr, base->NameFct); ASSERT_NE(nullptr, keyed->NameFct);
    EXPECT_STREQ("Animation", base->NameFct()); EXPECT_STREQ("Keyed Animation", keyed->NameFct());
    EXPECT_STREQ("Animation", RCKAnimation::GetClassName()); EXPECT_STREQ("Keyed Animation", RCKKeyedAnimation::GetClassName());
    EXPECT_EQ(CKPGUID_ANIMATION, base->Parameter); EXPECT_EQ(CKPGUID_ANIMATION, keyed->Parameter);
    EXPECT_TRUE(base->ToBeNotify.IsSet(CKCID_3DENTITY)); EXPECT_TRUE(keyed->ToBeNotify.IsSet(CKCID_OBJECTANIMATION));
    EXPECT_TRUE(keyed->CommonToBeNotify.IsSet(CKCID_3DENTITY));
    CKObject *a = Context->CreateObject(CKCID_ANIMATION, "Registered base");
    CKObject *b = Context->CreateObject(CKCID_KEYEDANIMATION, "Registered keyed");
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
    EXPECT_EQ(CKCID_ANIMATION, a->GetClassID()); EXPECT_EQ(CKCID_KEYEDANIMATION, b->GetClassID());
    EXPECT_STREQ("Registered base", a->GetName()); EXPECT_STREQ("Registered keyed", b->GetName());
    EXPECT_TRUE(CKIsChildClassOf(b, CKCID_SCENEOBJECT)); DestroyOnly(a); DestroyOnly(b);
}

TEST_F(AnimationLifecycleTest, AnimationRegistrationNotifiesBeforeReferencedEntityDeletion) {
    RCKAnimation *animation = static_cast<RCKAnimation *>(Context->CreateObject(CKCID_ANIMATION));
    RCK3dEntity *root = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, root);
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_ANIMATION, nullptr); ASSERT_NE(nullptr, chunk);
    chunk->StartWrite(); chunk->WriteIdentifier(CK_STATESAVE_ANIMATIONBODYPARTS);
    chunk->WriteObjectArray(nullptr, 0); chunk->WriteObject(root); chunk->CloseChunk();
    ASSERT_EQ(CK_OK, animation->Load(chunk, nullptr)); DeleteCKStateChunk(chunk);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(root), animation->GetRootEntity());
    CK_ID animationID = animation->GetID(); DestroyOnly(root);
    EXPECT_EQ(animation, Context->GetObject(animationID)); EXPECT_EQ(nullptr, animation->GetRootEntity());
    DestroyOnly(animation);
}

TEST_F(AnimationLifecycleTest, KeyedRegistrationRemovesDeletedFactoryChildAndLeavesOtherChannelPlayable) {
    RCKKeyedAnimation *animation = static_cast<RCKKeyedAnimation *>(Context->CreateObject(CKCID_KEYEDANIMATION));
    RCKObjectAnimation *dead = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    RCKObjectAnimation *live = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    RCK3dEntity entity(Context); ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, dead); ASSERT_NE(nullptr, live);
    live->Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); VxVector position(7, 0, 0); live->AddPositionKey(0, &position);
    ASSERT_EQ(CK_OK, animation->AddAnimation(dead)); ASSERT_EQ(CK_OK, animation->AddAnimation(live));
    DestroyOnly(dead); EXPECT_EQ(1, animation->GetAnimationCount()); EXPECT_EQ(live, animation->GetAnimation(0));
    animation->SetStep(0); EXPECT_FLOAT_EQ(7, entity.GetLocalMatrix()[3][0]);
    // Context deletion prepares the keyed object's child dependency list even
    // when optional class dependencies are NONE. Delete the surviving child
    // explicitly to verify the second notification, then delete the empty group.
    CK_ID liveID = live->GetID(); DestroyOnly(live);
    EXPECT_EQ(nullptr, Context->GetObject(liveID)); EXPECT_EQ(0, animation->GetAnimationCount());
    DestroyOnly(animation);
}

TEST_F(AnimationLifecycleTest, BaseDestructionUnregistersObjectWithoutOwningReferencedHierarchy) {
    RCKCharacter character(Context); RCK3dEntity root(Context);
    LifecycleAnimationProbe *animation = new LifecycleAnimationProbe(Context, "Temporary");
    animation->References(&root, &character); CK_ID id = animation->GetID(); delete animation;
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(&root, Context->GetObject(root.GetID()));
    EXPECT_EQ(&character, Context->GetObject(character.GetID()));
    VxVector position(3, 4, 5); root.SetPosition(&position); EXPECT_FLOAT_EQ(3, root.GetLocalMatrix()[3][0]);
}

TEST_F(AnimationLifecycleTest, ContextDeletionIncludesKeyedChildrenEvenWithoutOptionalClassDependencies) {
    RCKKeyedAnimation *group = static_cast<RCKKeyedAnimation *>(Context->CreateObject(CKCID_KEYEDANIMATION));
    RCKObjectAnimation *first = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    RCKObjectAnimation *second = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, group); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, entity);
    first->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
    second->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
    ASSERT_EQ(CK_OK, group->AddAnimation(first)); ASSERT_EQ(CK_OK, group->AddAnimation(second));
    CK_ID groupID = group->GetID(), firstID = first->GetID(), secondID = second->GetID(), entityID = entity->GetID();
    DestroyOnly(group);
    EXPECT_EQ(nullptr, Context->GetObject(groupID)); EXPECT_EQ(nullptr, Context->GetObject(firstID));
    EXPECT_EQ(nullptr, Context->GetObject(secondID));
    // Entity is an optional ObjectAnimation dependency; it survives NONE.
    CKObject *survivingEntity = Context->GetObject(entityID); ASSERT_NE(nullptr, survivingEntity);
    DestroyOnly(survivingEntity);
}

TEST_F(AnimationLifecycleTest, KeyedDestructionDetachesOnlyItsChildrenAndPreservesCurves) {
    RCK3dEntity entity(Context); LifecycleChildProbe child(Context);
    Position(child, entity, VxVector(7, 2, 3));
    LifecycleKeyedProbe *old = new LifecycleKeyedProbe(Context), *current = new LifecycleKeyedProbe(Context);
    old->AddAnimation(&child); current->AddAnimation(&child); CK_ID oldID = old->GetID(), currentID = current->GetID();
    delete old; EXPECT_EQ(nullptr, Context->GetObject(oldID)); EXPECT_EQ(current, child.Parent());
    EXPECT_EQ(1, current->GetAnimationCount()); delete current;
    EXPECT_EQ(nullptr, Context->GetObject(currentID)); ASSERT_EQ(nullptr, child.Parent());
    ASSERT_EQ(CK_OK, child.SetFrame(0)); EXPECT_FLOAT_EQ(7, entity.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(2, entity.GetLocalMatrix()[3][1]); EXPECT_FLOAT_EQ(3, entity.GetLocalMatrix()[3][2]);
}

TEST_F(AnimationLifecycleTest, AnimationMemoryAccountsForOwnedNamesAndExcludesSharedNames) {
    RCKAnimation animation(Context); CKObject *api = &animation; int empty = api->GetMemoryOccupation();
    EXPECT_GE(empty, static_cast<int>(sizeof(RCKAnimation)));
    animation.SetName("walk"); EXPECT_EQ(empty + 5, api->GetMemoryOccupation());
    animation.SetName("walk-cycle"); EXPECT_EQ(empty + 11, api->GetMemoryOccupation());
    animation.SetName("Shared animation name", TRUE); EXPECT_EQ(empty, api->GetMemoryOccupation());
    animation.SetName(nullptr); EXPECT_EQ(empty, api->GetMemoryOccupation());
}

TEST_F(AnimationLifecycleTest, KeyedMemoryCountsPointerEntriesWithoutOwningChildControllers) {
    LifecycleKeyedProbe animation(Context); LifecycleChildProbe first(Context), second(Context); RCK3dEntity entity(Context);
    CKObject *api = &animation; int empty = api->GetMemoryOccupation();
    EXPECT_GE(empty, static_cast<int>(sizeof(RCKKeyedAnimation)));
    animation.AddAnimation(&first); EXPECT_EQ(empty + static_cast<int>(sizeof(CKObject *)), api->GetMemoryOccupation());
    animation.AddAnimation(&first); animation.AddAnimation(&second); animation.EmptySlot();
    EXPECT_EQ(empty + 4 * static_cast<int>(sizeof(CKObject *)), api->GetMemoryOccupation());
    int entries = api->GetMemoryOccupation(); Position(first, entity, VxVector(1, 2, 3));
    VxVector another(5, 6, 7); first.AddPositionKey(1, &another);
    EXPECT_EQ(entries, api->GetMemoryOccupation()); animation.SetName("four"); EXPECT_EQ(entries + 5, api->GetMemoryOccupation());
    animation.SetName(nullptr); animation.Clear(); EXPECT_EQ(empty, api->GetMemoryOccupation());
    VxVector position; ASSERT_TRUE(first.EvaluatePosition(1, position)); EXPECT_FLOAT_EQ(5, position.x);
}

TEST_F(AnimationLifecycleTest, RootLookupKeepsBaseReferencesAndDiscoversKeyedCharacterAncestorLazily) {
    RCKCharacter character(Context); RCK3dEntity root(Context), leaf(Context), explicitRoot(Context);
    ASSERT_TRUE(root.SetParent(reinterpret_cast<CK3dEntity *>(&character), FALSE));
    ASSERT_TRUE(leaf.SetParent(reinterpret_cast<CK3dEntity *>(&root), FALSE));
    LifecycleAnimationProbe base(Context); base.References(&explicitRoot, &character);
    CKAnimation *baseAPI = &base; EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&explicitRoot), baseAPI->GetRootEntity());
    LifecycleChildProbe child(Context); child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&leaf));
    LifecycleKeyedProbe keyed(Context); keyed.Character(&character); keyed.AddAnimation(&child);
    CKAnimation *keyedAPI = &keyed; EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), keyedAPI->GetRootEntity());
    EXPECT_EQ(nullptr, keyed.GetRootAnimationInternal()); // Discovery does not invent a root channel.
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), keyedAPI->GetRootEntity());
}

TEST_F(AnimationLifecycleTest, EmptyKeyedRootLookupCanDiscoverHierarchyAddedLater) {
    RCKCharacter character(Context); RCK3dEntity root(Context), leaf(Context); LifecycleKeyedProbe keyed(Context);
    keyed.Character(&character); CKAnimation *api = &keyed; EXPECT_EQ(nullptr, api->GetRootEntity());
    LifecycleChildProbe child(Context); child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&leaf)); keyed.AddAnimation(&child);
    ASSERT_TRUE(root.SetParent(reinterpret_cast<CK3dEntity *>(&character), FALSE));
    ASSERT_TRUE(leaf.SetParent(reinterpret_cast<CK3dEntity *>(&root), FALSE));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), api->GetRootEntity());
}

TEST_F(AnimationLifecycleTest, RootHelpersExposeCachedChannelAndCenterOffsetUsedByPlayback) {
    RCKCharacter character(Context); RCK3dEntity root(Context); LifecycleChildProbe child(Context);
    ASSERT_TRUE(root.SetParent(reinterpret_cast<CK3dEntity *>(&character), FALSE));
    LifecycleKeyedProbe keyed(Context); keyed.Character(&character);
    Position(child, root, VxVector(3, 4, 5)); keyed.AddAnimation(&child); keyed.CenterAnimation(0);
    EXPECT_EQ(&child, keyed.GetRootAnimationInternal()); EXPECT_FLOAT_EQ(-3, keyed.GetRootVectorInternal().x);
    EXPECT_FLOAT_EQ(-4, keyed.GetRootVectorInternal().y); EXPECT_FLOAT_EQ(-5, keyed.GetRootVectorInternal().z);
    ASSERT_EQ(CK_OK, child.SetFrame(0)); EXPECT_FLOAT_EQ(0, root.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(0, root.GetLocalMatrix()[3][1]); EXPECT_FLOAT_EQ(0, root.GetLocalMatrix()[3][2]);
    keyed.Clear(); EXPECT_EQ(nullptr, keyed.GetRootAnimationInternal()); EXPECT_FLOAT_EQ(0, keyed.GetRootVectorInternal().x);
    ASSERT_EQ(CK_OK, child.SetFrame(0)); EXPECT_FLOAT_EQ(3, root.GetLocalMatrix()[3][0]);
}
}
