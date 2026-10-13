#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class CharacterDependencyMap : public CKDependenciesContext {
public:
    explicit CharacterDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject &from, CKObject *to) { m_MapID.Insert(from.GetID(), to ? to->GetID() : 0); }
};

class CharacterDependenciesProbe : public RCKCharacter {
public:
    explicit CharacterDependenciesProbe(CKContext *context) : RCKCharacter(context) {}
    void Body(CKObject *body) { m_BodyParts.PushBack(body); }
    void Animation(CKObject *animation) { m_Animations.PushBack(animation); }
    void References(RCKBodyPart *root, RCKKeyedAnimation *active, RCKAnimation *next, RCK3dEntity *floor) {
        m_RootBodyPart = root; m_ActiveAnimation = active; m_AnimDest = next; m_FloorRef = floor;
    }
    void Transition(float frame, CKDWORD value) { m_FrameDest = frame; field_1D4 = value; }
    CKDWORD TransitionValue() const { return field_1D4; }
    void RemoveBodySlot(int index) { m_BodyParts.RemoveAt(index); }
    void RemoveAnimationSlot(int index) { m_Animations.RemoveAt(index); }
    void RetainedSecondary(RCKKeyedAnimation *first, RCKKeyedAnimation *retained) {
        // Legal post-removal state: allocated slots are initialized, but one
        // transition survives outside the active prefix for subsequent reuse.
        m_SecondaryAnimations = new CKSecondaryAnimation[3]();
        m_SecondaryAnimationsAllocated = 3;
        m_SecondaryAnimationsCount = 1;
        m_SecondaryAnimations[0].Animation = first;
        m_SecondaryAnimations[2].Animation = retained;
    }
};

class CharacterDependencyBody : public RCKBodyPart {
public:
    explicit CharacterDependencyBody(CKContext *context) : RCKBodyPart(context) {}
    void Character(RCKCharacter *character) { m_Character = character; }
    CKERROR PrepareDependencies(CKDependenciesContext &context) override {
        ++PrepareCalls; return RCKBodyPart::PrepareDependencies(context);
    }
    int PrepareCalls = 0;
};

class CharacterDependencyAnimation : public RCKKeyedAnimation {
public:
    explicit CharacterDependencyAnimation(CKContext *context) : RCKKeyedAnimation(context) {}
    void Character(RCKCharacter *character) { m_Character = character; }
    CKERROR PrepareDependencies(CKDependenciesContext &context) override {
        ++PrepareCalls; return RCKKeyedAnimation::PrepareDependencies(context);
    }
    int PrepareCalls = 0;
};

class CharacterDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    CKScene *Scene() { return static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); }
    static void Options(CKDependencies &options, CKDWORD entity, CKDWORD character, CKDWORD animation = 0) {
        options.Resize(80); for (int index = 0; index < 80; ++index) options[index] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM;
        options[CKCID_3DENTITY] = entity; options[CKCID_CHARACTER] = character; options[CKCID_ANIMATION] = animation;
    }
    static void Membership(CKScene &scene, CKSceneObject &object, bool present) {
        EXPECT_EQ(present, object.IsInScene(&scene) != FALSE);
        EXPECT_EQ(present, scene.GetSceneObjectDesc(&object) != nullptr);
    }
};

TEST_F(CharacterDependenciesTest, AddWithDependenciesIncludesBodyAndAnimationOnceAndKeepsOtherScene) {
    CharacterDependencyBody first(Context), second(Context); CharacterDependencyAnimation animation(Context);
    CharacterDependenciesProbe character(Context);
    character.Body(&first); character.Body(nullptr); character.Body(&second); character.Body(&first);
    character.Animation(nullptr); character.Animation(&animation); character.Animation(&animation);
    CKScene *scene = Scene(), *other = Scene(); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, other);
    animation.AddToScene(other, FALSE);
    character.AddToScene(scene, TRUE); character.AddToScene(scene, TRUE);
    Membership(*scene, character, true); Membership(*scene, first, true);
    Membership(*scene, second, true); Membership(*scene, animation, true); Membership(*other, animation, true);
    EXPECT_EQ(4, scene->GetObjectCount()); EXPECT_EQ(1, other->GetObjectCount());
}

TEST_F(CharacterDependenciesTest, AddWithoutDependenciesAndNullSceneDoNotAddReferences) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context);
    CharacterDependenciesProbe character(Context); character.Body(&body); character.Animation(&animation);
    CKScene *scene = Scene(); ASSERT_NE(nullptr, scene);
    character.AddToScene(nullptr, TRUE); character.AddToScene(scene, FALSE);
    Membership(*scene, character, true); Membership(*scene, body, false); Membership(*scene, animation, false);
    EXPECT_EQ(1, scene->GetObjectCount());
}

TEST_F(CharacterDependenciesTest, RemoveWithDependenciesRemovesAnimationInsteadOfAddingItAndKeepsOtherScene) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation present(Context), absent(Context);
    CharacterDependenciesProbe character(Context); character.Body(&body); character.Body(nullptr);
    character.Animation(&present); character.Animation(nullptr); character.Animation(&present); character.Animation(&absent);
    CKScene *scene = Scene(), *other = Scene(); ASSERT_NE(nullptr, scene); ASSERT_NE(nullptr, other);
    character.AddToScene(scene, FALSE); body.AddToScene(scene, FALSE); present.AddToScene(scene, FALSE);
    present.AddToScene(other, FALSE); absent.AddToScene(other, FALSE);
    character.RemoveFromScene(scene, TRUE); character.RemoveFromScene(scene, TRUE);
    Membership(*scene, character, false); Membership(*scene, body, false);
    Membership(*scene, present, false); Membership(*scene, absent, false);
    Membership(*other, present, true); Membership(*other, absent, true);
    EXPECT_EQ(0, scene->GetObjectCount()); EXPECT_EQ(2, other->GetObjectCount());
}

TEST_F(CharacterDependenciesTest, RemoveWithoutDependenciesAndNullSceneKeepReferencedMembers) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context);
    CharacterDependenciesProbe character(Context); character.Body(&body); character.Animation(&animation);
    CKScene *scene = Scene(); ASSERT_NE(nullptr, scene); character.AddToScene(scene, TRUE);
    character.RemoveFromScene(nullptr, TRUE); Membership(*scene, character, true);
    character.RemoveFromScene(scene, FALSE);
    Membership(*scene, character, false); Membership(*scene, body, true); Membership(*scene, animation, true);
    EXPECT_EQ(2, scene->GetObjectCount());
}

TEST_F(CharacterDependenciesTest, IsObjectUsedUsesExactClassHintListsAndBaseAnimationFallback) {
    CharacterDependencyBody first(Context), second(Context), unrelated(Context);
    CharacterDependencyAnimation animation(Context); RCKObjectAnimation channel(Context);
    CharacterDependenciesProbe character(Context); character.Body(&first); character.Body(nullptr); character.Body(&second);
    character.Animation(&animation); character.Animation(nullptr); character.Animation(&animation);
    character.AddObjectAnimation(reinterpret_cast<CKObjectAnimation *>(&channel));
    EXPECT_TRUE(character.IsObjectUsed(&first, CKCID_BODYPART));
    EXPECT_TRUE(character.IsObjectUsed(&second, CKCID_BODYPART));
    EXPECT_FALSE(character.IsObjectUsed(&unrelated, CKCID_BODYPART));
    EXPECT_TRUE(character.IsObjectUsed(&animation, CKCID_ANIMATION));
    EXPECT_FALSE(character.IsObjectUsed(&animation, CKCID_KEYEDANIMATION));
    EXPECT_FALSE(character.IsObjectUsed(&first, CKCID_ANIMATION));
    EXPECT_TRUE(character.IsObjectUsed(&channel, CKCID_OBJECTANIMATION));
    EXPECT_FALSE(character.IsObjectUsed(&channel, CKCID_ANIMATION));
}

TEST_F(CharacterDependenciesTest, CopyPrepareUsesEntityAndCharacterFlagsRatherThanAnimationClassFlag) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context); RCK3dEntity floor(Context);
    CharacterDependenciesProbe character(Context); character.Body(&body); character.Body(nullptr); character.Body(&body);
    character.Animation(&animation); character.References(&body, &animation, &animation, &floor);
    const CKDWORD entity[] = {0, 4, 0, 0}, characterFlags[] = {0, 0, 1, 0}, animationFlags[] = {0, 0, 0, 4};
    for (int index = 0; index < 4; ++index) {
        SCOPED_TRACE(index); CKDependencies options; Options(options, entity[index], characterFlags[index], animationFlags[index]);
        CharacterDependencyMap dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
        dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, character.PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.IsDependenciesHere(character.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(body.GetID()));
        EXPECT_TRUE(dependencies.IsDependenciesHere(floor.GetID()));
        EXPECT_EQ(index == 1 || index == 2, dependencies.IsDependenciesHere(animation.GetID()) != FALSE);
        EXPECT_FALSE(dependencies.IsDependenciesHere(character.GetWarper()->GetID()));
        EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
        EXPECT_EQ(CKERR_ALREADYPRESENT, character.PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
    }
    EXPECT_EQ(8, body.PrepareCalls); EXPECT_EQ(2, animation.PrepareCalls);
}

TEST_F(CharacterDependenciesTest, NonCopyPrepareIncludesAnimationsAndDeleteCollectsRetainedCapacityWarper) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context), activeSecondary(Context), retained(Context);
    CharacterDependenciesProbe character(Context); character.Body(&body); character.Animation(&animation);
    character.RetainedSecondary(&activeSecondary, &retained);
    const CK_DEPENDENCIES_OPMODE modes[] = {CK_DEPENDENCIES_BUILD, CK_DEPENDENCIES_SAVE, CK_DEPENDENCIES_DELETE};
    for (int index = 0; index < 3; ++index) {
        SCOPED_TRACE(index); CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
        CharacterDependencyMap dependencies(Context); dependencies.SetOperationMode(modes[index]); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, character.PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.IsDependenciesHere(body.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(animation.GetID()));
        EXPECT_EQ(index == 2, dependencies.IsDependenciesHere(activeSecondary.GetID()) != FALSE);
        EXPECT_EQ(index == 2, dependencies.IsDependenciesHere(retained.GetID()) != FALSE);
        EXPECT_EQ(index == 2, dependencies.IsDependenciesHere(character.GetWarper()->GetID()) != FALSE);
        EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
    }
    EXPECT_EQ(3, animation.PrepareCalls); EXPECT_EQ(1, activeSecondary.PrepareCalls); EXPECT_EQ(1, retained.PrepareCalls);
}

TEST_F(CharacterDependenciesTest, SavePrepareBaseRejectionStopsBeforeCharacterReferences) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context);
    CharacterDependenciesProbe character(Context); character.Body(&body); character.Animation(&animation);
    character.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    CharacterDependencyMap dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, character.PrepareDependencies(dependencies));
    EXPECT_EQ(0, body.PrepareCalls); EXPECT_EQ(0, animation.PrepareCalls); EXPECT_EQ(0, dependencies.GetDependenciesMap().Size());
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
}

TEST_F(CharacterDependenciesTest, RemapUpdatesListsAndFourReferencesWithoutReplacingInternalWarper) {
    CharacterDependencyBody oldBody(Context), newBody(Context); CharacterDependencyAnimation oldAnimation(Context), newAnimation(Context);
    RCK3dEntity oldFloor(Context), newFloor(Context); CharacterDependenciesProbe character(Context);
    character.Body(&oldBody); character.Body(nullptr); character.Body(&oldBody); character.Animation(&oldAnimation); character.Animation(nullptr);
    character.References(&oldBody, &oldAnimation, &oldAnimation, &oldFloor); CKAnimation *warper = character.GetWarper();
    CharacterDependencyMap dependencies(Context); dependencies.Map(oldBody, &newBody); dependencies.Map(oldAnimation, &newAnimation);
    dependencies.Map(oldFloor, &newFloor); dependencies.Map(*reinterpret_cast<CKObject *>(warper), &newAnimation);
    ASSERT_EQ(CK_OK, character.RemapDependencies(dependencies));
    ASSERT_EQ(3, character.GetBodyPartCount()); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&newBody), character.GetBodyPart(0));
    EXPECT_EQ(nullptr, character.GetBodyPart(1)); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&newBody), character.GetBodyPart(2));
    EXPECT_EQ(reinterpret_cast<CKAnimation *>(&newAnimation), character.GetAnimation(0)); EXPECT_EQ(nullptr, character.GetAnimation(1));
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&newBody), character.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CKAnimation *>(&newAnimation), character.GetActiveAnimation());
    EXPECT_EQ(reinterpret_cast<CKAnimation *>(&newAnimation), character.GetNextActiveAnimation());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&newFloor), character.GetFloorReferenceObject()); EXPECT_EQ(warper, character.GetWarper());
}

TEST_F(CharacterDependenciesTest, RemapClearsExplicitNullMappingsAndKeepsUnmappedReferences) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context); RCK3dEntity floor(Context);
    CharacterDependenciesProbe character(Context); character.Body(&body); character.Animation(&animation);
    character.References(&body, &animation, &animation, &floor);
    CharacterDependencyMap dependencies(Context); dependencies.Map(animation, nullptr);
    ASSERT_EQ(CK_OK, character.RemapDependencies(dependencies));
    EXPECT_EQ(nullptr, character.GetAnimation(0)); EXPECT_EQ(nullptr, character.GetActiveAnimation()); EXPECT_EQ(nullptr, character.GetNextActiveAnimation());
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&body), character.GetBodyPart(0)); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&body), character.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&floor), character.GetFloorReferenceObject());
}

TEST_F(CharacterDependenciesTest, CopyUsesEntityAndCharacterMasksAndPreservesExcludedDestinationList) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context), oldAnimation(Context); RCK3dEntity floor(Context);
    CharacterDependenciesProbe source(Context); source.Body(&body); source.Body(nullptr); source.Animation(&animation); source.Animation(nullptr);
    source.References(&body, &animation, &animation, &floor); source.Transition(7.25f, 0x13579u);
    const CKDWORD entity[] = {0, 4, 0, 0}, character[] = {0, 0, 1, 0}, animationClass[] = {0, 0, 0, 4};
    for (int index = 0; index < 4; ++index) {
        SCOPED_TRACE(index); CharacterDependenciesProbe destination(Context); destination.Animation(&oldAnimation);
        destination.References(nullptr, &oldAnimation, &oldAnimation, nullptr);
        CKAnimation *warper = destination.GetWarper(); CKDependencies options; Options(options, entity[index], character[index], animationClass[index]);
        CharacterDependencyMap dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_COPY); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); const bool included = index == 1 || index == 2;
        EXPECT_EQ(included ? 2 : 1, destination.GetAnimationCount());
        EXPECT_EQ(reinterpret_cast<CKAnimation *>(included ? &animation : &oldAnimation), destination.GetAnimation(0));
        EXPECT_EQ(reinterpret_cast<CKAnimation *>(included ? &animation : &oldAnimation), destination.GetNextActiveAnimation());
        // Native active reference is copied even when list/destination copying is excluded.
        EXPECT_EQ(reinterpret_cast<CKAnimation *>(&animation), destination.GetActiveAnimation());
        ASSERT_EQ(2, destination.GetBodyPartCount()); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&body), destination.GetBodyPart(0));
        EXPECT_EQ(nullptr, destination.GetBodyPart(1)); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&body), destination.GetRootBodyPart());
        EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&floor), destination.GetFloorReferenceObject()); EXPECT_EQ(warper, destination.GetWarper());
        float frame = 0; destination.GetWarperParameters(nullptr, nullptr, nullptr, nullptr, &frame);
        EXPECT_FLOAT_EQ(7.25f, frame); EXPECT_EQ(0x13579u, destination.TransitionValue());
        destination.RemoveBodySlot(0); if (included) destination.RemoveAnimationSlot(0);
        EXPECT_EQ(2, source.GetBodyPartCount()); EXPECT_EQ(2, source.GetAnimationCount()); dependencies.StopDependencies();
    }
}

TEST_F(CharacterDependenciesTest, CopyRebindsSourceInternalDestinationToDestinationWarper) {
    CharacterDependencyAnimation active(Context); CharacterDependenciesProbe source(Context), destination(Context);
    CKAnimation *sourceWarper = source.GetWarper(), *destinationWarper = destination.GetWarper();
    ASSERT_NE(nullptr, sourceWarper); ASSERT_NE(nullptr, destinationWarper); ASSERT_NE(sourceWarper, destinationWarper);
    source.References(nullptr, &active, reinterpret_cast<RCKAnimation *>(sourceWarper), nullptr);
    CKDependencies options; Options(options, 4, 0); CharacterDependencyMap dependencies(Context);
    dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); dependencies.StopDependencies();
    EXPECT_EQ(destinationWarper, destination.GetNextActiveAnimation()); EXPECT_EQ(destinationWarper, destination.GetWarper());
    EXPECT_EQ(sourceWarper, source.GetNextActiveAnimation());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&destination), destinationWarper->GetCharacter());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&source), sourceWarper->GetCharacter());
}

TEST_F(CharacterDependenciesTest, ActualContextCopyCreatesIndependentBodyAnimationAndWorkingPose) {
    CharacterDependencyBody body(Context); CharacterDependencyAnimation animation(Context); CharacterDependenciesProbe source(Context);
    body.SetMoveableFlags(body.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    ASSERT_TRUE(source.AddChild(reinterpret_cast<CK3dEntity *>(&body), TRUE));
    RCKObjectAnimation *channel = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION)); ASSERT_NE(nullptr, channel);
    VxVector position(4, 5, 6); channel->Set3dEntity(reinterpret_cast<CK3dEntity *>(&body)); channel->AddPositionKey(2, &position);
    ASSERT_EQ(CK_OK, animation.AddAnimation(channel)); body.Character(&source); animation.Character(&source);
    source.Body(&body); source.Animation(&animation); source.References(&body, &animation, &animation, nullptr);
    CKDependencies options; Options(options, 6, 0);
    CKObject *object = Context->CopyObject(&source, &options); ASSERT_NE(nullptr, object); ASSERT_NE(&source, object);
    CKCharacter *copy = CKCharacter::Cast(object); ASSERT_NE(nullptr, copy); EXPECT_NE(source.GetID(), copy->GetID());
    ASSERT_EQ(1, copy->GetBodyPartCount()); ASSERT_EQ(1, copy->GetAnimationCount());
    CKBodyPart *copiedBody = copy->GetBodyPart(0); CKKeyedAnimation *copiedAnimation = CKKeyedAnimation::Cast(copy->GetAnimation(0));
    ASSERT_NE(nullptr, copiedBody); ASSERT_NE(nullptr, copiedAnimation);
    EXPECT_NE(reinterpret_cast<CKBodyPart *>(&body), copiedBody); EXPECT_NE(reinterpret_cast<CKAnimation *>(&animation), copiedAnimation);
    EXPECT_EQ(copy, copiedBody->GetCharacter()); EXPECT_EQ(copy, copiedAnimation->GetCharacter()); EXPECT_EQ(copiedBody, copy->GetRootBodyPart());
    EXPECT_EQ(copy, copiedBody->GetParent()); EXPECT_FALSE(copiedBody->GetMoveableFlags() & VX_MOVEABLE_WORLDALIGNED);
    EXPECT_EQ(copiedAnimation, copy->GetActiveAnimation()); EXPECT_EQ(copiedAnimation, copy->GetNextActiveAnimation());
    EXPECT_NE(source.GetWarper(), copy->GetWarper()); EXPECT_EQ(copy, copy->GetWarper()->GetCharacter());
    ASSERT_EQ(1, copiedAnimation->GetAnimationCount()); CKObjectAnimation *copiedChannel = copiedAnimation->GetAnimation(0);
    ASSERT_NE(nullptr, copiedChannel); EXPECT_NE(reinterpret_cast<CKObjectAnimation *>(channel), copiedChannel);
    EXPECT_EQ(copiedBody, copiedChannel->Get3dEntity());
    // Character channels write local matrices first; the keyed group applies
    // the character hierarchy update after all selected channels are evaluated.
    ASSERT_EQ(CK_OK, copiedChannel->SetFrame(2, copiedAnimation));
    EXPECT_FLOAT_EQ(4, copiedBody->GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(5, copiedBody->GetLocalMatrix()[3][1]);
    EXPECT_FLOAT_EQ(6, copiedBody->GetLocalMatrix()[3][2]);
    copiedAnimation->SetFrame(2); VxVector copiedPosition; copiedBody->GetPosition(&copiedPosition, nullptr);
    EXPECT_FLOAT_EQ(4, copiedPosition.x); EXPECT_FLOAT_EQ(5, copiedPosition.y); EXPECT_FLOAT_EQ(6, copiedPosition.z);
    VxVector originalPosition; body.GetPosition(&originalPosition, nullptr); EXPECT_FLOAT_EQ(0, originalPosition.x);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&source), body.GetCharacter()); EXPECT_EQ(reinterpret_cast<CKCharacter *>(&source), animation.GetCharacter());
    // Context owns and later destroys the copied graph; source objects remain independently registered.
    EXPECT_EQ(&source, Context->GetObject(source.GetID())); EXPECT_EQ(&body, Context->GetObject(body.GetID()));
    ASSERT_TRUE(body.SetParent(nullptr, TRUE));
}

} // namespace
