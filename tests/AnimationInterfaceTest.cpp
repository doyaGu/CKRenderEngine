#include <gtest/gtest.h>
#include <type_traits>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();


namespace {

class AnimationInterfaceTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    CKKeyedAnimation *Group() {
        return CKKeyedAnimation::Cast(Context->CreateObject(CKCID_KEYEDANIMATION));
    }

    CKObjectAnimation *Curve(CK3dEntity *entity, float start, float end) {
        CKObjectAnimation *animation = CKObjectAnimation::Cast(Context->CreateObject(CKCID_OBJECTANIMATION));
        if (!animation) return nullptr;
        animation->Set3dEntity(entity);
        VxVector first(start, 0, 0), last(end, 0, 0);
        animation->AddPositionKey(0, &first);
        animation->AddPositionKey(10, &last);
        animation->SetLength(10);
        return animation;
    }

    CK3dEntity *Entity() {
        return CK3dEntity::Cast(Context->CreateObject(CKCID_3DENTITY));
    }
};

// Registered objects must be cloned by the context's dependency transaction.
// Traits inspect the compiler contract without executing an unsafe shallow
// copy, and also allow the same test to run against the pre-fix headers.
TEST_F(AnimationInterfaceTest, RegisteredIdentityRejectsCppCopyAndMove) {
    EXPECT_FALSE(std::is_copy_constructible<RCKAnimation>::value);
    EXPECT_FALSE(std::is_copy_assignable<RCKAnimation>::value);
    EXPECT_FALSE(std::is_move_constructible<RCKAnimation>::value);
    EXPECT_FALSE(std::is_move_assignable<RCKAnimation>::value);
    EXPECT_FALSE(std::is_copy_constructible<RCKKeyedAnimation>::value);
    EXPECT_FALSE(std::is_copy_assignable<RCKKeyedAnimation>::value);
    EXPECT_FALSE(std::is_move_constructible<RCKKeyedAnimation>::value);
    EXPECT_FALSE(std::is_move_assignable<RCKKeyedAnimation>::value);
}

// Use the same public SDK pointers returned to clients by the factory. These
// tests exercise virtual calls through the SDK surface, rather than calling
// RCK methods or comparing an implementation's own layout with itself.
TEST_F(AnimationInterfaceTest, FactoryBaseInterfaceDispatchesSettingsAndFrameCursor) {
    CKObject *object = Context->CreateObject(CKCID_ANIMATION, "Base interface");
    ASSERT_NE(nullptr, object);
    CKAnimation *animation = CKAnimation::Cast(object);
    ASSERT_NE(nullptr, animation);
    EXPECT_EQ(nullptr, CKKeyedAnimation::Cast(object));
    EXPECT_EQ(CKCID_ANIMATION, animation->GetClassID());
    EXPECT_EQ(object, Context->GetObject(animation->GetID()));
    animation->SetLength(20);
    animation->SetFrame(5);
    EXPECT_FLOAT_EQ(.25f, animation->GetStep());
    EXPECT_FLOAT_EQ(5, animation->GetFrame());
    animation->LinkToFrameRate(TRUE, 24);
    EXPECT_TRUE(animation->IsLinkedToFrameRate());
    EXPECT_FLOAT_EQ(24, animation->GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(8, animation->GetNextFrame(125));
    animation->SetTransitionMode(CK_TRANSITION_WARPSTART);
    animation->SetSecondaryAnimationMode(CKSECONDARYANIMATION_LOOP);
    animation->SetCanBeInterrupt(FALSE);
    animation->SetCharacterOrientation(TRUE);
    EXPECT_EQ(CK_TRANSITION_WARPSTART, animation->GetTransitionMode());
    EXPECT_EQ(CKSECONDARYANIMATION_LOOP, animation->GetSecondaryAnimationMode());
    EXPECT_FALSE(animation->CanBeInterrupt());
    EXPECT_TRUE(animation->DoesCharacterTakeOrientation());
    animation->SetLength(0);
    animation->SetFrame(2);
    EXPECT_FLOAT_EQ(0, animation->GetStep());
}

TEST_F(AnimationInterfaceTest, FactoryKeyedInterfaceDispatchesOverloadsLengthAndPlayback) {
    CKKeyedAnimation *group = Group();
    CK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, group); ASSERT_NE(nullptr, entity);
    CKObjectAnimation *child = Curve(entity, 2, 22);
    ASSERT_NE(nullptr, child);
    ASSERT_EQ(CK_OK, group->AddAnimation(child));
    EXPECT_EQ(1, group->GetAnimationCount());
    EXPECT_EQ(child, group->GetAnimation(0));
    EXPECT_EQ(child, group->GetAnimation(entity));
    CKAnimation *base = CKAnimation::Cast(group);
    ASSERT_NE(nullptr, base);
    base->SetLength(20);
    EXPECT_FLOAT_EQ(20, child->GetLength());
    base->SetFrame(5);
    EXPECT_FLOAT_EQ(12, entity->GetLocalMatrix()[3][0]);
    base->SetCurrentStep(.75f);
    EXPECT_FLOAT_EQ(12, entity->GetLocalMatrix()[3][0]);
    base->SetStep(1);
    EXPECT_FLOAT_EQ(22, entity->GetLocalMatrix()[3][0]);
    ASSERT_EQ(CK_OK, group->RemoveAnimation(child));
    EXPECT_EQ(0, group->GetAnimationCount());
    EXPECT_EQ(nullptr, group->GetAnimation(entity));
    ASSERT_EQ(CK_OK, group->AddAnimation(child));
    group->Clear();
    EXPECT_EQ(0, group->GetAnimationCount());
    EXPECT_EQ(child, Context->GetObject(child->GetID()));
}

TEST_F(AnimationInterfaceTest, ObjectInterfaceSaveDispatchesToKeyedWireSections) {
    CKKeyedAnimation *group = Group();
    CK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, group); ASSERT_NE(nullptr, entity);
    CKObjectAnimation *child = Curve(entity, 0, 10);
    ASSERT_NE(nullptr, child);
    ASSERT_EQ(CK_OK, group->AddAnimation(child));
    group->SetLength(20); group->SetCurrentStep(.375f);
    CKObject *object = group;
    CKStateChunk *chunk = object->Save(nullptr, 0x40u | 0x200u | 0x1000u | 0x100000u);
    ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_KEYEDANIMATION, chunk->GetChunkClassID());
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x40));
    EXPECT_FLOAT_EQ(20, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x200));
    EXPECT_FLOAT_EQ(.375f, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x1000));
    EXPECT_EQ(1, chunk->StartReadSequence());
    EXPECT_EQ(child->GetID(), chunk->ReadObjectID());
    ASSERT_TRUE(chunk->SeekIdentifier(0x100000));
    EXPECT_EQ(FALSE, chunk->ReadInt());
    EXPECT_FLOAT_EQ(.5f, chunk->ReadFloat());
    EXPECT_FALSE(chunk->SeekIdentifier(0x200000));
    DeleteCKStateChunk(chunk);
}

TEST_F(AnimationInterfaceTest, ObjectInterfaceLoadConsumesIndependentListAndBaseWire) {
    CKKeyedAnimation *group = Group();
    CK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, group); ASSERT_NE(nullptr, entity);
    CKObjectAnimation *child = Curve(entity, 0, 10);
    ASSERT_NE(nullptr, child);
    CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(CHUNK_DEV_2_1);
    chunk.WriteIdentifier(0x40); chunk.WriteFloat(10);
    chunk.WriteIdentifier(0x10); chunk.WriteDword(5); chunk.WriteFloat(25);
    chunk.WriteIdentifier(0x200); chunk.WriteFloat(.75f);
    chunk.WriteIdentifier(0x1000); chunk.StartObjectIDSequence(1); chunk.WriteObjectSequence(child);
    chunk.WriteIdentifier(0x100000); chunk.WriteInt(FALSE); chunk.WriteFloat(.25f);
    chunk.CloseChunk();
    CKObject *object = group;
    ASSERT_EQ(CK_OK, object->Load(&chunk, nullptr));
    EXPECT_EQ(1, group->GetAnimationCount()); EXPECT_EQ(child, group->GetAnimation(entity));
    EXPECT_FLOAT_EQ(10, group->GetLength()); EXPECT_FLOAT_EQ(.75f, group->GetStep());
    EXPECT_FLOAT_EQ(25, group->GetLinkedFrameRate()); EXPECT_FLOAT_EQ(.25f, group->GetMergeFactor());
    EXPECT_FLOAT_EQ(0, entity->GetLocalMatrix()[3][0]);
    group->SetStep(.5f); EXPECT_FLOAT_EQ(5, entity->GetLocalMatrix()[3][0]);
}

TEST_F(AnimationInterfaceTest, BaseInterfaceMergeDispatchProducesActualBlendedPose) {
    CKKeyedAnimation *first = Group(), *second = Group();
    CK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, entity);
    CKObjectAnimation *a = Curve(entity, 0, 10), *b = Curve(entity, 20, 30);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
    ASSERT_EQ(CK_OK, first->AddAnimation(a)); ASSERT_EQ(CK_OK, second->AddAnimation(b));
    first->SetLength(10); second->SetLength(10);
    CKCharacter *character = CKCharacter::Cast(Context->CreateObject(CKCID_CHARACTER));
    ASSERT_NE(nullptr, character);
    ASSERT_EQ(CK_OK, character->AddAnimation(first));
    ASSERT_EQ(CK_OK, character->AddAnimation(second));
    first->ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    CKAnimation *base = first;
    CKAnimation *merged = base->CreateMergedAnimation(second, TRUE);
    ASSERT_NE(nullptr, merged); ASSERT_TRUE(merged->IsMerged());
    EXPECT_TRUE(merged->IsDynamic());
    CKKeyedAnimation *keyed = CKKeyedAnimation::Cast(merged);
    ASSERT_NE(nullptr, keyed); EXPECT_EQ(1, keyed->GetAnimationCount());
    merged->SetMergeFactor(.25f); merged->SetStep(.5f);
    EXPECT_FLOAT_EQ(.25f, merged->GetMergeFactor());
    EXPECT_NEAR(10, entity->GetLocalMatrix()[3][0], 1e-5f);
}

TEST_F(AnimationInterfaceTest, BaseInterfaceTransitionDispatchCreatesAndPlaysChannel) {
    CKKeyedAnimation *first = Group(), *second = Group(), *result = Group();
    CK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, result); ASSERT_NE(nullptr, entity);
    CKObjectAnimation *a = Curve(entity, 0, 10), *b = Curve(entity, 20, 30);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
    ASSERT_EQ(CK_OK, first->AddAnimation(a)); ASSERT_EQ(CK_OK, second->AddAnimation(b));
    first->SetLength(10); second->SetLength(10); first->SetCurrentStep(.2f);
    CKAnimation *base = result;
    EXPECT_FLOAT_EQ(6, base->CreateTransition(first, second, 0, 4, 6));
    ASSERT_EQ(1, result->GetAnimationCount());
    base->SetStep(0); EXPECT_NEAR(2, entity->GetLocalMatrix()[3][0], 1e-5f);
    base->SetStep(.5f); EXPECT_NEAR(14, entity->GetLocalMatrix()[3][0], 1e-5f);
    base->SetStep(1); EXPECT_NEAR(26, entity->GetLocalMatrix()[3][0], 1e-5f);
}

TEST_F(AnimationInterfaceTest, ObjectInterfaceVirtualDestructorUnregistersOnlyGroupIdentity) {
    CKKeyedAnimation *group = Group();
    CK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, group); ASSERT_NE(nullptr, entity);
    CKObjectAnimation *child = Curve(entity, 0, 10);
    ASSERT_NE(nullptr, child);
    ASSERT_EQ(CK_OK, group->AddAnimation(child));
    const CK_ID groupID = group->GetID(), childID = child->GetID(), entityID = entity->GetID();
    CKObject *object = group;
    delete object;
    EXPECT_EQ(nullptr, Context->GetObject(groupID));
    EXPECT_EQ(child, Context->GetObject(childID)); EXPECT_EQ(entity, Context->GetObject(entityID));
    CKKeyedAnimation *replacement = Group(); ASSERT_NE(nullptr, replacement);
    ASSERT_EQ(CK_OK, replacement->AddAnimation(child)); replacement->SetLength(10); replacement->SetStep(.3f);
    EXPECT_FLOAT_EQ(3, entity->GetLocalMatrix()[3][0]);
}

TEST_F(AnimationInterfaceTest, ContextCopyPreservesLegalBaseCloneWithIndependentIdentityAndName) {
    CKAnimation *source = CKAnimation::Cast(Context->CreateObject(CKCID_ANIMATION, "Source"));
    ASSERT_NE(nullptr, source);
    source->SetLength(20); source->SetCurrentStep(.25f); source->LinkToFrameRate(TRUE, 24);
    CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
    CKAnimation *copy = CKAnimation::Cast(Context->CopyObject(source, &options));
    ASSERT_NE(nullptr, copy); ASSERT_NE(source, copy);
    EXPECT_NE(source->GetID(), copy->GetID());
    EXPECT_EQ(source, Context->GetObject(source->GetID())); EXPECT_EQ(copy, Context->GetObject(copy->GetID()));
    EXPECT_FLOAT_EQ(20, copy->GetLength()); EXPECT_FLOAT_EQ(.25f, copy->GetStep());
    EXPECT_FLOAT_EQ(24, copy->GetLinkedFrameRate());
    copy->SetName("Clone"); copy->SetLength(40); copy->SetCurrentStep(.5f);
    EXPECT_STREQ("Source", source->GetName()); EXPECT_FLOAT_EQ(20, source->GetLength());
    EXPECT_FLOAT_EQ(.25f, source->GetStep());
    const CK_ID copyID = copy->GetID(); delete static_cast<CKObject *>(copy);
    EXPECT_EQ(nullptr, Context->GetObject(copyID)); EXPECT_EQ(source, Context->GetObject(source->GetID()));
    EXPECT_STREQ("Source", source->GetName());
}

TEST_F(AnimationInterfaceTest, ContextCopyPreservesLegalKeyedCloneAndIndependentControllers) {
    CKKeyedAnimation *source = Group(); CK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, entity);
    CKObjectAnimation *child = Curve(entity, 0, 10); ASSERT_NE(nullptr, child);
    ASSERT_EQ(CK_OK, source->AddAnimation(child)); source->SetLength(10);
    CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
    CKKeyedAnimation *copy = CKKeyedAnimation::Cast(Context->CopyObject(source, &options));
    ASSERT_NE(nullptr, copy); ASSERT_NE(source, copy); ASSERT_EQ(1, copy->GetAnimationCount());
    CKObjectAnimation *copiedChild = copy->GetAnimation(0);
    ASSERT_NE(nullptr, copiedChild); ASSERT_NE(child, copiedChild);
    EXPECT_NE(source->GetID(), copy->GetID()); EXPECT_NE(child->GetID(), copiedChild->GetID());
    EXPECT_EQ(entity, copiedChild->Get3dEntity());
    VxVector extra(100, 0, 0); copiedChild->AddPositionKey(10, &extra);
    copy->SetStep(1); EXPECT_FLOAT_EQ(100, entity->GetLocalMatrix()[3][0]);
    source->SetStep(1); EXPECT_FLOAT_EQ(10, entity->GetLocalMatrix()[3][0]);
    copy->Clear(); EXPECT_EQ(0, copy->GetAnimationCount()); EXPECT_EQ(1, source->GetAnimationCount());
    EXPECT_EQ(child, Context->GetObject(child->GetID()));
    source->SetStep(.5f); EXPECT_FLOAT_EQ(5, entity->GetLocalMatrix()[3][0]);
}

} // namespace
