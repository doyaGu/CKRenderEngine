#include <gtest/gtest.h>
#include <cstddef>
#include <type_traits>
#include "CKAll.h"
#include "RCKAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class AnimationLayoutProbe : public RCKAnimation {
public:
    explicit AnimationLayoutProbe(CKContext *context) : RCKAnimation(context) {}
    void References(RCKCharacter *character, RCK3dEntity *root) {
        m_Character = character;
        m_RootEntity = root;
    }
    std::size_t Offset(int field) const {
        const void *fields[] = {&m_Character, &m_Length, &m_Step, &m_RootEntity, &m_Flags, &m_FrameRate};
        return static_cast<const unsigned char *>(fields[field]) - reinterpret_cast<const unsigned char *>(this);
    }
};

struct Destructions { int Animation = 0; int Root = 0; int Character = 0; };
class ObservedAnimation : public RCKAnimation {
public:
    ObservedAnimation(CKContext *context, Destructions &counts) : RCKAnimation(context), Counts(counts) {}
    ~ObservedAnimation() override { ++Counts.Animation; }
    void References(RCKCharacter *character, RCK3dEntity *root) { m_Character = character; m_RootEntity = root; }
private:
    Destructions &Counts;
};
class ObservedRoot : public RCK3dEntity {
public:
    ObservedRoot(CKContext *context, Destructions &counts) : RCK3dEntity(context), Counts(counts) {}
    ~ObservedRoot() override { ++Counts.Root; }
private:
    Destructions &Counts;
};
class ObservedCharacter : public RCKCharacter {
public:
    ObservedCharacter(CKContext *context, Destructions &counts) : RCKCharacter(context), Counts(counts) {}
    ~ObservedCharacter() override { ++Counts.Character; }
private:
    Destructions &Counts;
};

class AnimationAbiCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    CKAnimation *Animation(const char *name) {
        return CKAnimation::Cast(Context->CreateObject(CKCID_ANIMATION, const_cast<char *>(name)));
    }
    void DestroyOnly(CKObject *object) {
        CKDependencies dependencies;
        dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, Context->DestroyObject(object, 0, &dependencies));
    }
    static void LoadReferences(CKAnimation &animation, CKObject *character, CKObject *root) {
        // Author only the two documented reference sections; no Animation::Save
        // round trip is used to construct the fixture.
        CKStateChunk chunk(CKCID_ANIMATION, nullptr);
        chunk.StartWrite();
        chunk.WriteIdentifier(CK_STATESAVE_ANIMATIONBODYPARTS);
        chunk.WriteObjectArray(nullptr, 0);
        chunk.WriteObject(root);
        chunk.WriteIdentifier(CK_STATESAVE_ANIMATIONCHARACTER);
        chunk.WriteObject(character);
        chunk.CloseChunk();
        CKObject &object = animation;
        ASSERT_EQ(CK_OK, object.Load(&chunk, nullptr));
    }
};

TEST_F(AnimationAbiCompletionTest, FactorySdkBasePrefixAndDefaultTailServicesKeepIndependentObjectState) {
    CKAnimation *animation = Animation("SDK base"), *other = Animation("Untouched");
    ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, other);
    CKObject *object = animation;
    CKSceneObject *sceneObject = animation;
    EXPECT_EQ(static_cast<void *>(animation), static_cast<void *>(object));
    EXPECT_EQ(static_cast<void *>(animation), static_cast<void *>(sceneObject));
    EXPECT_EQ(object, Context->GetObject(object->GetID()));
    EXPECT_EQ(CKCID_ANIMATION, object->GetClassID());
    EXPECT_TRUE(CKIsChildClassOf(object, CKCID_SCENEOBJECT));
    object->Show(CKHIDE); EXPECT_EQ(CKHIDE, object->IsVisible());
    object->Show(CKSHOW); EXPECT_EQ(CKSHOW, object->IsVisible());
    animation->SetLength(80); animation->SetFrame(20); animation->LinkToFrameRate(TRUE, -16);
    animation->SetFlags(0x84220515u);
    const CKDWORD flags = animation->GetFlags();
    animation->CenterAnimation(7);
    animation->SetMergeFactor(.75f);
    EXPECT_FLOAT_EQ(0, animation->GetMergeFactor());
    EXPECT_FALSE(animation->IsMerged());
    EXPECT_EQ(nullptr, animation->CreateMergedAnimation(other, TRUE));
    EXPECT_EQ(nullptr, animation->CreateMergedAnimation(other, FALSE));
    EXPECT_FLOAT_EQ(0, animation->CreateTransition(other, other, CK_TRANSITION_WARPSTART, 7, 3));
    EXPECT_FLOAT_EQ(80, animation->GetLength());
    EXPECT_FLOAT_EQ(.25f, animation->GetStep());
    EXPECT_FLOAT_EQ(20, animation->GetFrame());
    EXPECT_FLOAT_EQ(-16, animation->GetLinkedFrameRate());
    EXPECT_EQ(flags, animation->GetFlags());
    animation->SetCurrentStep(.5f);
    EXPECT_FLOAT_EQ(40, animation->GetFrame());
    EXPECT_FLOAT_EQ(36, animation->GetNextFrame(250));
    EXPECT_STREQ("SDK base", object->GetName());
    EXPECT_STREQ("Untouched", other->GetName());
    EXPECT_FLOAT_EQ(100, other->GetLength()); EXPECT_FLOAT_EQ(0, other->GetStep());
}

TEST_F(AnimationAbiCompletionTest, ContextDeletionNotifiesBothReferenceKindsInEitherOrder) {
    for (int characterFirst = 0; characterFirst < 2; ++characterFirst) {
        CKAnimation *animation = Animation(characterFirst ? "Character first" : "Root first");
        CKObject *character = Context->CreateObject(CKCID_CHARACTER);
        CKObject *root = Context->CreateObject(CKCID_3DENTITY);
        ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, root);
        const CK_ID animationId = animation->GetID(), characterId = character->GetID(), rootId = root->GetID();
        LoadReferences(*animation, character, root);
        EXPECT_EQ(static_cast<void *>(character), static_cast<void *>(animation->GetCharacter()));
        EXPECT_EQ(static_cast<void *>(root), static_cast<void *>(animation->GetRootEntity()));
        DestroyOnly(characterFirst ? character : root);
        EXPECT_EQ(animation, Context->GetObject(animationId));
        if (characterFirst) {
            EXPECT_EQ(nullptr, animation->GetCharacter());
            EXPECT_EQ(static_cast<void *>(root), static_cast<void *>(animation->GetRootEntity()));
            EXPECT_EQ(root, Context->GetObject(rootId));
            DestroyOnly(root);
        } else {
            EXPECT_EQ(nullptr, animation->GetRootEntity());
            EXPECT_EQ(static_cast<void *>(character), static_cast<void *>(animation->GetCharacter()));
            EXPECT_EQ(character, Context->GetObject(characterId));
            DestroyOnly(character);
        }
        EXPECT_EQ(nullptr, animation->GetCharacter()); EXPECT_EQ(nullptr, animation->GetRootEntity());
        EXPECT_EQ(nullptr, Context->GetObject(rootId)); EXPECT_EQ(nullptr, Context->GetObject(characterId));
        animation->SetLength(24); animation->SetFrame(6); EXPECT_FLOAT_EQ(.25f, animation->GetStep());
        DestroyOnly(animation); EXPECT_EQ(nullptr, Context->GetObject(animationId));
    }
}

TEST_F(AnimationAbiCompletionTest, FactoryVirtualDeletionUnregistersOnlyTheAnimationAndKeepsReferencedTargetsUsable) {
    CKAnimation *animation = Animation("Deleted base"), *peer = Animation("Surviving base");
    CKObject *character = Context->CreateObject(CKCID_CHARACTER);
    CKObject *root = Context->CreateObject(CKCID_3DENTITY);
    ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, peer); ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, root);
    LoadReferences(*animation, character, root);
    const CK_ID animationId = animation->GetID(), peerId = peer->GetID();
    const CK_ID characterId = character->GetID(), rootId = root->GetID();
    delete animation;
    EXPECT_EQ(nullptr, Context->GetObject(animationId));
    EXPECT_EQ(peer, Context->GetObject(peerId));
    EXPECT_EQ(character, Context->GetObject(characterId)); EXPECT_EQ(root, Context->GetObject(rootId));
    peer->SetLength(8); peer->SetStep(.75f); EXPECT_FLOAT_EQ(6, peer->GetFrame());
    RCK3dEntity *entity = static_cast<RCK3dEntity *>(root);
    VxVector position(3, 5, 7); entity->SetPosition(&position);
    EXPECT_FLOAT_EQ(3, entity->GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(5, entity->GetLocalMatrix()[3][1]); EXPECT_FLOAT_EQ(7, entity->GetLocalMatrix()[3][2]);
}

TEST_F(AnimationAbiCompletionTest, ContextCloseDestroysEachReferencedObjectExactlyOnceInEitherCreationOrder) {
    for (int animationFirst = 0; animationFirst < 2; ++animationFirst) {
        if (!Context) ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Destructions counts;
        ObservedAnimation *animation = nullptr;
        if (animationFirst) animation = new ObservedAnimation(Context, counts);
        ObservedRoot *root = new ObservedRoot(Context, counts);
        ObservedCharacter *character = new ObservedCharacter(Context, counts);
        if (!animationFirst) animation = new ObservedAnimation(Context, counts);
        animation->References(character, root);
        EXPECT_EQ(animation, Context->GetObject(animation->GetID()));
        EXPECT_EQ(root, Context->GetObject(root->GetID()));
        EXPECT_EQ(character, Context->GetObject(character->GetID()));
        CKContext *closing = Context; Context = nullptr;
        EXPECT_EQ(CK_OK, CKCloseContext(closing));
        EXPECT_EQ(1, counts.Animation); EXPECT_EQ(1, counts.Root); EXPECT_EQ(1, counts.Character);
    }
}

TEST_F(AnimationAbiCompletionTest, CurrentCompilerLayoutRecordsAllInstanceFieldsAndLegalSdkBaseSubobjects) {
    AnimationLayoutProbe animation(Context);
    const bool wide = sizeof(void *) == 8;
    const std::size_t expected[] = {wide ? 56u : 28u, wide ? 64u : 32u, wide ? 68u : 36u,
                                    wide ? 72u : 40u, wide ? 80u : 44u, wide ? 84u : 48u};
    const char *names[] = {"m_Character", "m_Length", "m_Step", "m_RootEntity", "m_Flags", "m_FrameRate"};
    RecordProperty("RCKAnimation_size", static_cast<int>(sizeof(RCKAnimation)));
    RecordProperty("RCKAnimation_alignment", static_cast<int>(alignof(RCKAnimation)));
    RecordProperty("CKAnimation_size", static_cast<int>(sizeof(CKAnimation)));
    RecordProperty("CKSceneObject_size", static_cast<int>(sizeof(CKSceneObject)));
    RecordProperty("pointer_size", static_cast<int>(sizeof(void *)));
    EXPECT_EQ(wide ? 88u : 52u, sizeof(RCKAnimation)); EXPECT_EQ(wide ? 8u : 4u, alignof(RCKAnimation));
    EXPECT_EQ(wide ? 56u : 28u, sizeof(CKAnimation)); EXPECT_EQ(sizeof(CKAnimation), sizeof(CKSceneObject));
    EXPECT_EQ(sizeof(RCKAnimation), sizeof(AnimationLayoutProbe));
    for (int field = 0; field < 6; ++field) {
        EXPECT_EQ(expected[field], animation.Offset(field)) << names[field];
        RecordProperty(names[field], static_cast<int>(animation.Offset(field)));
    }
    EXPECT_EQ(static_cast<void *>(&animation), static_cast<void *>(static_cast<CKAnimation *>(&animation)));
    EXPECT_EQ(static_cast<void *>(&animation), static_cast<void *>(static_cast<CKSceneObject *>(&animation)));
    EXPECT_EQ(static_cast<void *>(&animation), static_cast<void *>(static_cast<CKObject *>(&animation)));
    EXPECT_EQ(4u, sizeof(RCKAnimation::m_ClassID));
    EXPECT_FALSE(std::is_copy_constructible<RCKAnimation>::value);
    EXPECT_FALSE(std::is_copy_assignable<RCKAnimation>::value);
    EXPECT_FALSE(std::is_move_constructible<RCKAnimation>::value);
    EXPECT_FALSE(std::is_move_assignable<RCKAnimation>::value);
}
} // namespace
