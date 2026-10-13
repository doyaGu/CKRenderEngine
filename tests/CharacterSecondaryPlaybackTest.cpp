#include <gtest/gtest.h>
#include "CKAll.h"
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

class SecondaryCharacter : public RCKCharacter {
public:
    explicit SecondaryCharacter(CKContext *context) : RCKCharacter(context) {}
    void Own(RCKAnimation &animation) { m_Animations.PushBack(&animation); }
    const CKSecondaryAnimation &Slot(int index) const { return m_SecondaryAnimations[index]; }
    int Capacity() const { return m_SecondaryAnimationsAllocated; }
    void RemoveSlot(int index) { RemoveSecondaryAnimationAt(index); }
    void Establish(CKAnimation *animation) { PreDeleteBodyPartsForAnimation(animation); }
};

class SecondaryKeyed : public RCKKeyedAnimation {
public:
    explicit SecondaryKeyed(CKContext *context) : RCKKeyedAnimation(context) {}
    void Character(RCKCharacter *character) { m_Character = character; }
    void EmptySlot() { m_Animations.PushBack(nullptr); }
};

class SecondaryBasic : public RCKAnimation {
public:
    explicit SecondaryBasic(CKContext *context) : RCKAnimation(context) {}
    void Character(RCKCharacter *character) { m_Character = character; }
};

class CharacterSecondaryPlaybackTest : public ::testing::Test {
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
    static CKBodyPart *BodyAPI(RCKBodyPart &body) { return reinterpret_cast<CKBodyPart *>(&body); }
    static CKAnimation *AnimAPI(RCKKeyedAnimation &animation) { return reinterpret_cast<CKAnimation *>(&animation); }
    static CK3dEntity *EntityAPI(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CK_SECONDARYANIMATION_FLAGS Warp() {
        return static_cast<CK_SECONDARYANIMATION_FLAGS>(CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
    }
    static void Curve(SecondaryKeyed &animation, RCKObjectAnimation &channel, RCK3dEntity &entity,
                      RCKCharacter &character, float start, float end, float length = 10) {
        entity.SetMoveableFlags(entity.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        channel.Set3dEntity(EntityAPI(entity)); VxVector a(start, 0, 0), b(end, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b);
        ASSERT_EQ(CK_OK, animation.AddAnimation(&channel)); animation.SetLength(length); animation.Character(&character);
    }
    static void Attach(SecondaryCharacter &character, RCKBodyPart &body) {
        ASSERT_EQ(CK_OK, character.AddBodyPart(BodyAPI(body)));
    }
    static void Sample(CKAnimation *animation, float frame, float expected) {
        CKKeyedAnimation *keyed = CKKeyedAnimation::Cast(animation); ASSERT_NE(nullptr, keyed);
        ASSERT_EQ(1, keyed->GetAnimationCount()); CKObjectAnimation *channel = keyed->GetAnimation(0);
        ASSERT_NE(nullptr, channel); VxVector position; ASSERT_TRUE(channel->EvaluatePosition(frame, position));
        EXPECT_NEAR(expected, position.x, 0.0001f);
    }
    static void World(RCKBodyPart &body, float expected) {
        EXPECT_NEAR(expected, body.GetLocalMatrix()[3][0], 0.0001f);
        VxVector position; body.GetPosition(&position); EXPECT_NEAR(expected, position.x, 0.0001f);
    }
};

TEST_F(CharacterSecondaryPlaybackTest, DirectPlaybackSetsPoseExclusiveGateAndKeepsBodyHierarchy) {
    RCKBodyPart body(Context); RCKObjectAnimation primaryChannel(Context), secondaryChannel(Context);
    SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, primaryChannel, body, character, 0, 10);
    Curve(secondary, secondaryChannel, body, character, 100, 200); character.Own(primary); character.Own(secondary);
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(primary))); primary.SetFrame(2); World(body, 2);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 5, CKSECONDARYANIMATION_LOOPNTIMES, 4, 3));
    EXPECT_EQ(1, character.GetSecondaryAnimationsCount()); EXPECT_EQ(AnimAPI(secondary), character.GetSecondaryAnimation(0));
    EXPECT_EQ(AnimAPI(secondary), body.GetExclusiveAnimation()); EXPECT_EQ(EntityAPI(character), body.GetParent()); World(body, 150);
    primary.SetFrame(8); World(body, 150); secondary.SetFrame(6); World(body, 160);
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, character.Slot(0).Mode);
    EXPECT_FLOAT_EQ(5, character.Slot(0).GetStartingFrame()); EXPECT_EQ(3u, character.Slot(0).LoopCountRemaining);
    ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, InvalidOwnerAndDuplicatePlaybackLeaveCursorAndListUnchanged) {
    SecondaryBasic owned(Context), other(Context); SecondaryCharacter character(Context); owned.Character(&character); character.Own(owned);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.PlaySecondaryAnimation(nullptr));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.PlaySecondaryAnimation(&other)); EXPECT_EQ(0, character.GetSecondaryAnimationsCount());
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(&owned, 7, CKSECONDARYANIMATION_LOOPNTIMES, 3, 2));
    EXPECT_FLOAT_EQ(7, owned.GetFrame()); ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(&owned, 19, CKSECONDARYANIMATION_LASTFRAME, 1, 8));
    EXPECT_EQ(1, character.GetSecondaryAnimationsCount()); EXPECT_FLOAT_EQ(7, owned.GetFrame());
    EXPECT_FLOAT_EQ(7, character.Slot(0).GetStartingFrame()); EXPECT_EQ(2u, character.Slot(0).LoopCountRemaining);
    EXPECT_EQ(CKSECONDARYANIMATION_LOOPNTIMES, character.Slot(0).Flags);
}

TEST_F(CharacterSecondaryPlaybackTest, FromAnimationUsesNonzeroSettingsAndZeroSettingsKeepCallerMode) {
    SecondaryBasic first(Context), second(Context); SecondaryCharacter character(Context); first.Character(&character); second.Character(&character);
    character.Own(first); character.Own(second); first.SetSecondaryAnimationMode(CKSECONDARYANIMATION_LOOPNTIMES);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(&first, 3, CKSECONDARYANIMATION_FROMANIMATION, 2, 4));
    EXPECT_EQ(CKSECONDARYANIMATION_LOOPNTIMES, character.Slot(0).Flags); EXPECT_EQ(4u, character.Slot(0).LoopCountRemaining);
    const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(CKSECONDARYANIMATION_FROMANIMATION | CKSECONDARYANIMATION_LOOP);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(&second, 2, flags)); EXPECT_EQ(flags, character.Slot(1).Flags);
    EXPECT_FLOAT_EQ(3, first.GetFrame()); EXPECT_FLOAT_EQ(2, second.GetFrame());
}

TEST_F(CharacterSecondaryPlaybackTest, RequestedWarpWithoutKeyedPrimaryFallsBackToDirectPlayback) {
    RCKBodyPart body(Context); RCKObjectAnimation channel(Context); SecondaryBasic basicPrimary(Context);
    SecondaryKeyed secondary(Context); SecondaryCharacter character(Context); Attach(character, body);
    Curve(secondary, channel, body, character, 100, 200); character.Own(secondary);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 4, Warp(), 3));
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, character.Slot(0).Mode); EXPECT_EQ(nullptr, character.Slot(0).Animation);
    EXPECT_EQ(AnimAPI(secondary), body.GetExclusiveAnimation()); World(body, 140); character.FlushSecondaryAnimations();
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(&basicPrimary));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 6, Warp(), 3));
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, character.Slot(0).Mode); World(body, 160);
    ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, RequestedWarpForBasicSecondaryFallsBackToItsFrameCursor) {
    SecondaryBasic secondary(Context); SecondaryKeyed primary(Context); SecondaryCharacter character(Context);
    secondary.Character(&character); primary.Character(&character); character.Own(secondary); character.Own(primary);
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(primary)));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(&secondary, 8, Warp(), 4));
    EXPECT_FLOAT_EQ(8, secondary.GetFrame()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, character.Slot(0).Mode);
    EXPECT_EQ(&secondary, character.GetSecondaryAnimation(0)); EXPECT_EQ(nullptr, character.Slot(0).Animation);
}

TEST_F(CharacterSecondaryPlaybackTest, BorrowedPrimaryAndIdenticalSourcesUseDirectPlaybackInsteadOfEmptyTransition) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context); SecondaryKeyed borrowed(Context), secondary(Context);
    SecondaryCharacter other(Context), character(Context); Attach(character, body);
    Curve(borrowed, a, body, other, 0, 10); Curve(secondary, b, body, character, 100, 200);
    character.Own(secondary); other.Own(borrowed); ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(borrowed)));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 4, Warp(), 3));
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, character.Slot(0).Mode); EXPECT_EQ(nullptr, character.Slot(0).Animation);
    EXPECT_EQ(AnimAPI(secondary), body.GetExclusiveAnimation()); World(body, 140);
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), TRUE, 3));
    EXPECT_EQ(0, character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, body.GetExclusiveAnimation());
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(secondary)));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 6, Warp(), 3));
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, character.Slot(0).Mode); EXPECT_EQ(nullptr, character.Slot(0).Animation);
    World(body, 160); character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, StartingWarpBuildsRealTransitionCurveAndExclusiveGate) {
    RCKBodyPart body(Context); RCKObjectAnimation primaryChannel(Context), secondaryChannel(Context);
    SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context); Attach(character, body);
    Curve(primary, primaryChannel, body, character, 0, 10); Curve(secondary, secondaryChannel, body, character, 100, 200);
    character.Own(primary); character.Own(secondary); ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(primary))); primary.SetFrame(2);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 5, Warp(), 4));
    RCKKeyedAnimation *transition = character.Slot(0).Animation; ASSERT_NE(nullptr, transition);
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, character.Slot(0).Mode);
    EXPECT_EQ(AnimAPI(*transition), body.GetExclusiveAnimation()); EXPECT_EQ(AnimAPI(secondary), character.GetSecondaryAnimation(0));
    EXPECT_TRUE(transition->GetFlags() & CKANIMATION_SECONDARYWARPER); EXPECT_FLOAT_EQ(4, transition->GetLength());
    Sample(AnimAPI(*transition), 0, 2); Sample(AnimAPI(*transition), 2, 76); Sample(AnimAPI(*transition), 4, 150);
    transition->SetFrame(2); World(body, 76); primary.SetFrame(8); World(body, 76);
    EXPECT_EQ(EntityAPI(character), body.GetParent()); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, TransitionLifetimeFollowsDynamicCharacterInsteadOfContextCreationMode) {
    for (int phase = 0; phase < 2; ++phase) {
        SCOPED_TRACE(phase); RCKBodyPart *body = static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
        ASSERT_NE(nullptr, body); RCKObjectAnimation first(Context), second(Context);
        SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
        character.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); Attach(character, *body);
        Curve(primary, first, *body, character, 0, 10); Curve(secondary, second, *body, character, 100, 200);
        character.Own(primary); character.Own(secondary);
        ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(primary))); EXPECT_FALSE(Context->IsInDynamicCreationMode());
        if (phase == 0) {
            ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 2, Warp(), 4));
        } else {
            ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 2, CKSECONDARYANIMATION_ONESHOT));
            EXPECT_EQ(nullptr, character.Slot(0).Animation);
            ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), TRUE, 4));
        }
        ASSERT_NE(nullptr, character.Slot(0).Animation); EXPECT_TRUE(character.Slot(0).Animation->IsDynamic());
        character.FlushSecondaryAnimations(); ASSERT_TRUE(body->SetParent(nullptr));
    }
}

TEST_F(CharacterSecondaryPlaybackTest, StopSearchHandlesEmptyListFirstEntryAndAbsentMember) {
    SecondaryBasic first(Context), absent(Context); SecondaryCharacter character(Context); first.Character(&character); absent.Character(&character);
    character.Own(first); character.Own(absent);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.StopSecondaryAnimation(nullptr, FALSE, 0));
    EXPECT_EQ(CK_OK, character.StopSecondaryAnimation(&first, FALSE, 0));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(&first, 3));
    EXPECT_EQ(CK_OK, character.StopSecondaryAnimation(&absent, FALSE, 0)); EXPECT_EQ(1, character.GetSecondaryAnimationsCount());
    EXPECT_EQ(CK_OK, character.StopSecondaryAnimation(&first, FALSE, 0)); EXPECT_EQ(0, character.GetSecondaryAnimationsCount());
    EXPECT_EQ(nullptr, character.GetSecondaryAnimation(0)); EXPECT_EQ(0, static_cast<int>(character.Slot(0).Mode));
}

TEST_F(CharacterSecondaryPlaybackTest, RemovingMiddleCompactsListAndReusesRemovedSlotsTransition) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context), c(Context), d(Context);
    SecondaryKeyed primary(Context), first(Context), second(Context), third(Context); SecondaryCharacter character(Context); Attach(character, body);
    Curve(primary, a, body, character, 0, 10); Curve(first, b, body, character, 100, 200);
    Curve(second, c, body, character, 200, 300); Curve(third, d, body, character, 300, 400);
    character.Own(primary); character.Own(first); character.Own(second); character.Own(third); character.SetActiveAnimation(AnimAPI(primary));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(first), 1, Warp(), 3));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(second), 2, Warp(), 4));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(third), 3, Warp(), 5));
    RCKKeyedAnimation *removed = character.Slot(1).Animation, *last = character.Slot(2).Animation;
    ASSERT_NE(nullptr, removed); ASSERT_NE(nullptr, last); EXPECT_EQ(4, character.Capacity());
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(second), FALSE, 0));
    EXPECT_EQ(2, character.GetSecondaryAnimationsCount()); EXPECT_EQ(AnimAPI(first), character.GetSecondaryAnimation(0));
    EXPECT_EQ(AnimAPI(third), character.GetSecondaryAnimation(1)); EXPECT_EQ(last, character.Slot(1).Animation);
    EXPECT_FLOAT_EQ(3, character.Slot(1).GetStartingFrame()); EXPECT_FLOAT_EQ(5, character.Slot(1).WarpLength);
    EXPECT_EQ(removed, character.Slot(2).Animation); EXPECT_EQ(0, static_cast<int>(character.Slot(2).Mode));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(second), 6, Warp(), 2));
    EXPECT_EQ(removed, character.Slot(2).Animation); EXPECT_EQ(AnimAPI(second), character.GetSecondaryAnimation(2));
    EXPECT_EQ(AnimAPI(*removed), body.GetExclusiveAnimation()); EXPECT_FLOAT_EQ(2, removed->GetLength());
    character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, RemovingLastClearsRuntimeValuesButKeepsReusableTransitionAndCapacity) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context); SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, a, body, character, 0, 10); Curve(secondary, b, body, character, 100, 200);
    character.Own(primary); character.Own(secondary); character.SetActiveAnimation(AnimAPI(primary));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 3, Warp(), 4, 6));
    RCKKeyedAnimation *transition = character.Slot(0).Animation; ASSERT_NE(nullptr, transition);
    character.RemoveSlot(-1); character.RemoveSlot(1); EXPECT_EQ(1, character.GetSecondaryAnimationsCount());
    character.RemoveSlot(0); EXPECT_EQ(0, character.GetSecondaryAnimationsCount()); EXPECT_EQ(2, character.Capacity());
    EXPECT_EQ(transition, character.Slot(0).Animation); EXPECT_EQ(0u, character.Slot(0).AnimID); EXPECT_EQ(0u, character.Slot(0).Flags);
    EXPECT_EQ(0, static_cast<int>(character.Slot(0).Mode)); EXPECT_FLOAT_EQ(0, character.Slot(0).GetStartingFrame());
    EXPECT_FLOAT_EQ(0, character.Slot(0).WarpLength); EXPECT_EQ(0u, character.Slot(0).LoopCountRemaining); EXPECT_EQ(nullptr, body.GetExclusiveAnimation());
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 4, Warp(), 2)); EXPECT_EQ(transition, character.Slot(0).Animation);
    character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, StopWarpBuildsReturnCurveAndRepeatedStopPreservesIt) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context); SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, a, body, character, 0, 10); Curve(secondary, b, body, character, 100, 200);
    character.Own(primary); character.Own(secondary); character.SetActiveAnimation(AnimAPI(primary)); primary.SetFrame(2);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 5));
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), TRUE, 4));
    RCKKeyedAnimation *transition = character.Slot(0).Animation; ASSERT_NE(nullptr, transition);
    EXPECT_EQ(1, character.GetSecondaryAnimationsCount()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, character.Slot(0).Mode);
    EXPECT_EQ(AnimAPI(*transition), body.GetExclusiveAnimation()); Sample(AnimAPI(*transition), 0, 150); Sample(AnimAPI(*transition), 2, 78); Sample(AnimAPI(*transition), 4, 6);
    transition->SetFrame(2); World(body, 78); ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), TRUE, 9));
    EXPECT_EQ(transition, character.Slot(0).Animation); EXPECT_FLOAT_EQ(4, character.Slot(0).WarpLength); EXPECT_FLOAT_EQ(2, transition->GetFrame());
    character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, StopWarpWithZeroLengthPrimaryTerminatesAndTargetsItsConstantPose) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context); SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, a, body, character, 7, 7); primary.SetLength(0);
    Curve(secondary, b, body, character, 100, 200); character.Own(primary); character.Own(secondary); character.SetActiveAnimation(AnimAPI(primary));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 5));
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), TRUE, 3));
    RCKKeyedAnimation *transition = character.Slot(0).Animation; ASSERT_NE(nullptr, transition);
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, character.Slot(0).Mode);
    Sample(AnimAPI(*transition), 3, 7); transition->SetFrame(3); World(body, 7);
    character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, ConvenienceStopUsesAnimationSettingsToChooseWarpOrImmediateRemoval) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context); SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, a, body, character, 0, 10); Curve(secondary, b, body, character, 100, 200);
    character.Own(primary); character.Own(secondary); character.SetActiveAnimation(AnimAPI(primary));
    secondary.SetSecondaryAnimationMode(CKSECONDARYANIMATION_ONESHOT);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 2)); ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), 3.0f));
    EXPECT_EQ(0, character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, body.GetExclusiveAnimation());
    secondary.SetSecondaryAnimationMode(Warp()); ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 4));
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), 3.0f));
    EXPECT_EQ(1, character.GetSecondaryAnimationsCount()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, character.Slot(0).Mode);
    ASSERT_NE(nullptr, character.Slot(0).Animation); EXPECT_EQ(AnimAPI(*character.Slot(0).Animation), body.GetExclusiveAnimation());
    character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, StoppingIdenticalPlayingSourceRemovesDirectEntryWithoutCreatingEmptyWarp) {
    RCKBodyPart body(Context); RCKObjectAnimation channel(Context); SecondaryKeyed primary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, channel, body, character, 0, 10);
    ASSERT_EQ(CK_OK, character.AddAnimation(AnimAPI(primary)));
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(primary)));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(primary), 4, Warp(), 3));
    ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, character.Slot(0).Mode);
    ASSERT_EQ(nullptr, character.Slot(0).Animation); EXPECT_EQ(AnimAPI(primary), body.GetExclusiveAnimation());
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(primary), TRUE, 3));
    EXPECT_EQ(0, character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, character.Slot(0).Animation);
    EXPECT_EQ(nullptr, body.GetExclusiveAnimation()); EXPECT_EQ(AnimAPI(primary), character.GetActiveAnimation());
    primary.SetFrame(5); World(body, 5); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, StoppingStartingWarpUsesActualTransitionEvenWhenApiSourceIsCurrentPrimary) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context);
    SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, a, body, character, 0, 10); Curve(secondary, b, body, character, 100, 200);
    ASSERT_EQ(CK_OK, character.AddAnimation(AnimAPI(primary))); ASSERT_EQ(CK_OK, character.AddAnimation(AnimAPI(secondary)));
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(primary))); primary.SetFrame(2);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 5, Warp(), 4));
    ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, character.Slot(0).Mode);
    RCKKeyedAnimation *transition = character.Slot(0).Animation; ASSERT_NE(nullptr, transition);
    transition->SetFrame(2); World(body, 76);
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(secondary)));
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), TRUE, 3));
    ASSERT_EQ(1, character.GetSecondaryAnimationsCount()); EXPECT_EQ(transition, character.Slot(0).Animation);
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, character.Slot(0).Mode);
    EXPECT_EQ(AnimAPI(*transition), body.GetExclusiveAnimation()); EXPECT_FLOAT_EQ(3, transition->GetLength());
    Sample(AnimAPI(*transition), 0, 76); Sample(AnimAPI(*transition), 3, 130);
    transition->SetFrame(3); World(body, 130); character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, ExplicitStoppingWarpKeepsExactPrimaryEndAfterOneOrMultiplePeriods) {
    const float lengths[] = {3, 13};
    for (float warpLength : lengths) {
        RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context);
        SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
        Attach(character, body); Curve(primary, a, body, character, 0, 10); Curve(secondary, b, body, character, 100, 200);
        ASSERT_EQ(CK_OK, character.AddAnimation(AnimAPI(primary))); ASSERT_EQ(CK_OK, character.AddAnimation(AnimAPI(secondary)));
        ASSERT_EQ(CK_OK, character.SetActiveAnimation(AnimAPI(primary))); primary.SetFrame(7);
        ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 4));
        ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(secondary), TRUE, warpLength));
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, character.Slot(0).Mode);
        RCKKeyedAnimation *transition = character.Slot(0).Animation; ASSERT_NE(nullptr, transition);
        EXPECT_FLOAT_EQ(warpLength, transition->GetLength());
        Sample(AnimAPI(*transition), warpLength, 10);
        transition->SetFrame(warpLength); World(body, 10);
        character.FlushSecondaryAnimations(); ASSERT_TRUE(body.SetParent(nullptr));
    }
}

TEST_F(CharacterSecondaryPlaybackTest, FlushClearsSourceAndTransitionGatesRestoresPrimaryAndRetainsReuseStorage) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context); SecondaryKeyed primary(Context), secondary(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, a, body, character, 0, 10); Curve(secondary, b, body, character, 100, 200);
    character.Own(primary); character.Own(secondary); character.SetActiveAnimation(AnimAPI(primary));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 5, Warp(), 4));
    RCKKeyedAnimation *transition = character.Slot(0).Animation; ASSERT_NE(nullptr, transition); transition->SetFrame(2);
    character.FlushSecondaryAnimations(); EXPECT_EQ(0, character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, body.GetExclusiveAnimation());
    EXPECT_EQ(transition, character.Slot(0).Animation); EXPECT_EQ(2, character.Capacity()); EXPECT_EQ(transition, Context->GetObject(transition->GetID()));
    primary.SetFrame(8); World(body, 8);
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(secondary), 3, Warp(), 2)); EXPECT_EQ(transition, character.Slot(0).Animation);
    character.FlushSecondaryAnimations(); character.FlushSecondaryAnimations(); EXPECT_EQ(0, character.GetSecondaryAnimationsCount());
    ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, RemovingEarlierSecondaryPreservesLaterExclusiveGateAndPriority) {
    RCKBodyPart body(Context); RCKObjectAnimation a(Context), b(Context), c(Context);
    SecondaryKeyed primary(Context), first(Context), last(Context); SecondaryCharacter character(Context);
    Attach(character, body); Curve(primary, a, body, character, 0, 10); Curve(first, b, body, character, 100, 200); Curve(last, c, body, character, 200, 300);
    character.Own(primary); character.Own(first); character.Own(last); character.SetActiveAnimation(AnimAPI(primary));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(first), 2));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(AnimAPI(last), 4)); last.SetFrame(4); World(body, 240);
    ASSERT_EQ(CK_OK, character.StopSecondaryAnimation(AnimAPI(first), FALSE, 0));
    EXPECT_EQ(1, character.GetSecondaryAnimationsCount()); EXPECT_EQ(AnimAPI(last), character.GetSecondaryAnimation(0));
    EXPECT_EQ(AnimAPI(last), body.GetExclusiveAnimation()); primary.SetFrame(8); World(body, 240);
    character.FlushSecondaryAnimations(); EXPECT_EQ(nullptr, body.GetExclusiveAnimation()); primary.SetFrame(8); World(body, 8);
    ASSERT_TRUE(body.SetParent(nullptr));
}

TEST_F(CharacterSecondaryPlaybackTest, GetterBoundsAndDeletedSourceIdentityDoNotExposeTheRetainedWarper) {
    SecondaryCharacter character(Context); CKObject *object = Context->CreateObject(CKCID_KEYEDANIMATION); ASSERT_NE(nullptr, object);
    CKAnimation *animation = CKAnimation::Cast(object); ASSERT_NE(nullptr, animation); ASSERT_EQ(CK_OK, character.AddAnimation(animation));
    ASSERT_EQ(CK_OK, character.PlaySecondaryAnimation(animation)); EXPECT_EQ(animation, character.GetSecondaryAnimation(0));
    EXPECT_EQ(nullptr, character.GetSecondaryAnimation(-1)); EXPECT_EQ(nullptr, character.GetSecondaryAnimation(1));
    CKDependencies none; none.m_Flags = CK_DEPENDENCIES_NONE; ASSERT_EQ(CK_OK, Context->DestroyObject(object, CK_DESTROY_NONOTIFY, &none));
    EXPECT_EQ(1, character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, character.GetSecondaryAnimation(0));
    character.FlushSecondaryAnimations(); EXPECT_EQ(0, character.GetSecondaryAnimationsCount());
}

TEST_F(CharacterSecondaryPlaybackTest, ExclusiveHelperIgnoresNullGenericEntitiesAndPreservesHierarchy) {
    RCKBodyPart body(Context); RCK3dEntity generic(Context); RCKObjectAnimation a(Context), b(Context);
    SecondaryKeyed animation(Context); SecondaryCharacter character(Context); Attach(character, body);
    ASSERT_TRUE(character.AddChild(EntityAPI(generic))); Curve(animation, a, body, character, 0, 10);
    b.Set3dEntity(EntityAPI(generic)); ASSERT_EQ(CK_OK, animation.AddAnimation(&b)); animation.EmptySlot(); character.Own(animation);
    character.Establish(nullptr); character.Establish(AnimAPI(animation));
    EXPECT_EQ(AnimAPI(animation), body.GetExclusiveAnimation()); EXPECT_EQ(EntityAPI(character), body.GetParent()); EXPECT_EQ(EntityAPI(character), generic.GetParent());
    body.SetExclusiveAnimation(nullptr); ASSERT_TRUE(body.SetParent(nullptr)); ASSERT_TRUE(generic.SetParent(nullptr));
}

} // namespace
