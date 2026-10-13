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

class ReverseCharacter : public RCKCharacter {
public:
    explicit ReverseCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot(int index) const { return m_SecondaryAnimations[index]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};

struct ReverseScene {
    ReverseCharacter Character;
    RCKBodyPart Root, Limb, Other;
    RCKObjectAnimation RootChannel, PrimaryChannel, SecondaryChannel, OtherChannel;
    RCKKeyedAnimation Primary, Secondary, Later;
    explicit ReverseScene(CKContext *context)
        : Character(context), Root(context), Limb(context), Other(context),
          RootChannel(context), PrimaryChannel(context), SecondaryChannel(context), OtherChannel(context),
          Primary(context), Secondary(context), Later(context) {}
    ~ReverseScene() {
        Character.FlushSecondaryAnimations();
        Limb.SetParent(nullptr, TRUE); Other.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
};

class CharacterSecondaryReverseTest : public ::testing::Test {
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
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKBodyPart *Part(RCKBodyPart &part) { return reinterpret_cast<CKBodyPart *>(&part); }
    static CKAnimation *Animation(RCKKeyedAnimation &animation) { return static_cast<CKAnimation *>(&animation); }
    static CK_SECONDARYANIMATION_FLAGS Flags(CKDWORD flags) { return static_cast<CK_SECONDARYANIMATION_FLAGS>(flags); }
    static void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
                      float first, float last) {
        channel.Set3dEntity(Entity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b);
        ASSERT_EQ(CK_OK, group.AddAnimation(&channel)); group.SetLength(10);
    }
    static void Prepare(ReverseScene &scene) {
        scene.Character.SetMoveableFlags(scene.Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        scene.Root.SetMoveableFlags(scene.Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        scene.Limb.SetMoveableFlags(scene.Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        scene.Other.SetMoveableFlags(scene.Other.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        ASSERT_EQ(CK_OK, scene.Character.AddBodyPart(Part(scene.Root)));
        ASSERT_TRUE(scene.Limb.SetParent(Entity(scene.Root), TRUE));
        ASSERT_TRUE(scene.Other.SetParent(Entity(scene.Root), TRUE));
        ASSERT_EQ(CK_OK, scene.Character.AddBodyPart(Part(scene.Limb)));
        ASSERT_EQ(CK_OK, scene.Character.AddBodyPart(Part(scene.Other)));
        Curve(scene.Primary, scene.RootChannel, scene.Root, 0, 0);
        Curve(scene.Primary, scene.PrimaryChannel, scene.Limb, 10, 20);
        Curve(scene.Secondary, scene.SecondaryChannel, scene.Limb, 100, 200);
        Curve(scene.Later, scene.OtherChannel, scene.Other, 200, 300);
        ASSERT_EQ(CK_OK, scene.Character.AddAnimation(Animation(scene.Primary)));
        ASSERT_EQ(CK_OK, scene.Character.AddAnimation(Animation(scene.Secondary)));
        ASSERT_EQ(CK_OK, scene.Character.AddAnimation(Animation(scene.Later)));
        ASSERT_EQ(Entity(scene.Root), scene.Secondary.GetRootEntity());
        scene.Character.SetAnimationLevelOfDetail(1);
        scene.Primary.SetCanBeInterrupt(FALSE); scene.Primary.LinkToFrameRate(TRUE, 0);
        scene.Secondary.LinkToFrameRate(TRUE, -1000); scene.Later.LinkToFrameRate(TRUE, 1000);
        ASSERT_EQ(CK_OK, scene.Character.SetActiveAnimation(Animation(scene.Primary)));
    }
    static void Pose(RCKBodyPart &part, float expected) {
        EXPECT_NEAR(expected, part.GetLocalMatrix()[3][0], .0001f);
        VxVector world; part.GetPosition(&world, nullptr); EXPECT_NEAR(expected, world.x, .0001f);
    }
    static void Sample(RCKKeyedAnimation &group, RCKBodyPart &part, float frame, float expected) {
        CKObjectAnimation *channel = group.GetAnimation(Entity(part)); ASSERT_NE(nullptr, channel);
        VxVector value; ASSERT_TRUE(channel->EvaluatePosition(frame, value)); EXPECT_NEAR(expected, value.x, .0001f);
    }
    static void Play(ReverseScene &scene, float frame, CKDWORD flags, int loops = 0) {
        ASSERT_EQ(CK_OK, scene.Character.PlaySecondaryAnimation(Animation(scene.Secondary), frame, Flags(flags), 4, loops));
        ASSERT_EQ(1, scene.Character.GetSecondaryAnimationsCount());
        if (scene.Character.Slot(0).Mode == CKSECONDARYANIMATIONRUNTIME_STARTINGWARP) {
            RCKKeyedAnimation *warp = scene.Character.Slot(0).Animation; ASSERT_NE(nullptr, warp);
            EXPECT_EQ(Animation(*warp), scene.Limb.GetExclusiveAnimation());
        } else {
            EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, scene.Character.Slot(0).Mode);
            EXPECT_EQ(Animation(scene.Secondary), scene.Limb.GetExclusiveAnimation());
        }
    }
    static void BeginReturnSource(ReverseScene &scene, CKDWORD flags, int loops = 0) {
        scene.Primary.LinkToFrameRate(TRUE, 1000);
        scene.Primary.SetFrame(0);
        Play(scene, 2, flags | CKSECONDARYANIMATION_DOWARP, loops);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, scene.Character.Slot(0).Mode);
        scene.Character.Tick(5);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, scene.Character.Slot(0).Mode);
        EXPECT_EQ(Animation(scene.Secondary), scene.Limb.GetExclusiveAnimation());
        scene.Primary.SetFrame(2); scene.Secondary.SetFrame(2);
    }
};

TEST_F(CharacterSecondaryReverseTest, SignedFrameRatePredictsReverseAndUnlinkedPlaybackStillAdvancesOne) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_LOOP);
    EXPECT_FLOAT_EQ(-1000, s.Secondary.GetLinkedFrameRate()); EXPECT_FLOAT_EQ(-1, s.Secondary.GetNextFrame(3));
    s.Secondary.LinkToFrameRate(FALSE, -1000); EXPECT_FLOAT_EQ(3, s.Secondary.GetNextFrame(300));
    s.Character.Tick(300); EXPECT_FLOAT_EQ(3, s.Secondary.GetFrame()); Pose(s.Limb, 130);
}

TEST_F(CharacterSecondaryReverseTest, ExactZeroDisplaysStartWithoutConsumingFiniteLoop) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_LOOPNTIMES, 3);
    s.Character.Tick(2); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame()); EXPECT_EQ(3u, s.Character.Slot(0).LoopCountRemaining);
    EXPECT_EQ(Animation(s.Secondary), s.Limb.GetExclusiveAnimation()); Pose(s.Limb, 100);
}

TEST_F(CharacterSecondaryReverseTest, StrictNegativeWrapsUnlimitedPlaybackAndPreservesGate) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_LOOP);
    s.Character.Tick(3); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    EXPECT_FLOAT_EQ(9, s.Secondary.GetFrame()); Pose(s.Limb, 190);
    EXPECT_EQ(Animation(s.Secondary), s.Limb.GetExclusiveAnimation());
    s.Character.SetMoveableFlags(s.Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
    s.Primary.SetFrame(8); Pose(s.Limb, 190);
}

TEST_F(CharacterSecondaryReverseTest, UnlimitedReverseMultiplePeriodsKeepExactZeroThenWrapOnNextCrossing) {
    ReverseScene s(Context); Prepare(s); Play(s, 0, CKSECONDARYANIMATION_LOOP);
    s.Secondary.LinkToFrameRate(TRUE, -1024);
    ASSERT_EQ(-20.0f, s.Secondary.GetNextFrame(19.53125f));
    s.Character.Tick(19.53125f); EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame()); Pose(s.Limb, 100);
    s.Character.Tick(.48828125f); EXPECT_FLOAT_EQ(9.5f, s.Secondary.GetFrame()); Pose(s.Limb, 195);
    s.Character.Tick(31.25f); EXPECT_FLOAT_EQ(7.5f, s.Secondary.GetFrame()); Pose(s.Limb, 175);
}

TEST_F(CharacterSecondaryReverseTest, FiniteReverseChargesEveryStrictlyCrossedPeriod) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_LOOPNTIMES, 5);
    s.Character.Tick(25); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(2u, s.Character.Slot(0).LoopCountRemaining); EXPECT_FLOAT_EQ(7, s.Secondary.GetFrame()); Pose(s.Limb, 170);
}

TEST_F(CharacterSecondaryReverseTest, FiniteReverseExactNegativePeriodDoesNotConsumeFollowingLoop) {
    ReverseScene s(Context); Prepare(s); Play(s, 0, CKSECONDARYANIMATION_LOOPNTIMES, 4);
    s.Secondary.LinkToFrameRate(TRUE, -1024);
    ASSERT_EQ(-20.0f, s.Secondary.GetNextFrame(19.53125f));
    s.Character.Tick(19.53125f); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(2u, s.Character.Slot(0).LoopCountRemaining); EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame()); Pose(s.Limb, 100);
    s.Character.Tick(.48828125f); EXPECT_EQ(1u, s.Character.Slot(0).LoopCountRemaining);
    EXPECT_FLOAT_EQ(9.5f, s.Secondary.GetFrame()); Pose(s.Limb, 195);
}

TEST_F(CharacterSecondaryReverseTest, FiniteReverseExhaustionRemovesEntryAndReleasesLimbGate) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_LOOPNTIMES, 3);
    s.Character.Tick(25); EXPECT_EQ(0, s.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(nullptr, s.Limb.GetExclusiveAnimation()); EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame()); Pose(s.Limb, 100);
    s.Character.SetMoveableFlags(s.Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
    s.Primary.SetFrame(6); Pose(s.Limb, 16);
}

TEST_F(CharacterSecondaryReverseTest, OneShotKeepsExactZeroThenStopsAtActualReverseEndpoint) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_ONESHOT);
    s.Character.Tick(2); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount()); Pose(s.Limb, 100);
    s.Character.Tick(.5f); EXPECT_EQ(0, s.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(nullptr, s.Limb.GetExclusiveAnimation()); EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame()); Pose(s.Limb, 100);
}

TEST_F(CharacterSecondaryReverseTest, LastFrameHoldsReverseEndpointAndContinuesBlockingPrimaryPose) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_LASTFRAME);
    s.Character.Tick(3); s.Character.Tick(20); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame()); EXPECT_EQ(Animation(s.Secondary), s.Limb.GetExclusiveAnimation());
    s.Character.SetMoveableFlags(s.Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
    s.Primary.SetFrame(8); Pose(s.Limb, 100);
}

TEST_F(CharacterSecondaryReverseTest, FiniteLastFrameExhaustionClearsLoopModeAndHoldsReverseEndpoint) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_LASTFRAME, 3);
    s.Character.Tick(25); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(0u, s.Character.Slot(0).LoopCountRemaining);
    EXPECT_EQ(0u, s.Character.Slot(0).Flags & CKSECONDARYANIMATION_LOOPNTIMES);
    EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame()); Pose(s.Limb, 100);
    s.Character.Tick(1); EXPECT_EQ(1, s.Character.GetSecondaryAnimationsCount()); Pose(s.Limb, 100);
}

TEST_F(CharacterSecondaryReverseTest, RemovingReverseEntryStillProcessesFollowingRealAnimation) {
    ReverseScene s(Context); Prepare(s); Play(s, 2, CKSECONDARYANIMATION_ONESHOT);
    ASSERT_EQ(CK_OK, s.Character.PlaySecondaryAnimation(Animation(s.Later), 1, CKSECONDARYANIMATION_LOOP));
    s.Character.Tick(3); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(Animation(s.Later), s.Character.GetSecondaryAnimation(0)); EXPECT_FLOAT_EQ(4, s.Later.GetFrame());
    EXPECT_EQ(nullptr, s.Limb.GetExclusiveAnimation()); EXPECT_EQ(Animation(s.Later), s.Other.GetExclusiveAnimation());
    Pose(s.Other, 240);
}

TEST_F(CharacterSecondaryReverseTest, ReverseOneShotReturnsFromActualStartPoseThroughPositiveWarp) {
    ReverseScene s(Context); Prepare(s); BeginReturnSource(s, CKSECONDARYANIMATION_ONESHOT);
    RCKKeyedAnimation *retained = s.Character.Slot(0).Animation; ASSERT_NE(nullptr, retained);
    s.Character.Tick(3); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, s.Character.Slot(0).Mode);
    EXPECT_EQ(retained, s.Character.Slot(0).Animation); EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame());
    EXPECT_FLOAT_EQ(1000, retained->GetLinkedFrameRate()); EXPECT_FLOAT_EQ(4, retained->GetLength());
    Sample(*retained, s.Limb, 0, 100); Sample(*retained, s.Limb, 4, 19);
    EXPECT_EQ(Animation(*retained), s.Limb.GetExclusiveAnimation()); Pose(s.Limb, 100);
    s.Character.Tick(4); ASSERT_EQ(1, s.Character.GetSecondaryAnimationsCount()); Pose(s.Limb, 19);
    s.Character.Tick(1); EXPECT_EQ(0, s.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, s.Limb.GetExclusiveAnimation());
    s.Character.SetMoveableFlags(s.Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
    s.Primary.SetFrame(6); Pose(s.Limb, 16);
}

TEST_F(CharacterSecondaryReverseTest, ReverseFiniteExhaustionReturnsOnlyAfterAllRequestedPeriods) {
    ReverseScene s(Context); Prepare(s); BeginReturnSource(s, CKSECONDARYANIMATION_LOOPNTIMES, 2);
    s.Character.Tick(3); ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, s.Character.Slot(0).Mode);
    EXPECT_EQ(1u, s.Character.Slot(0).LoopCountRemaining); EXPECT_FLOAT_EQ(9, s.Secondary.GetFrame()); Pose(s.Limb, 190);
    s.Character.Tick(10); ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, s.Character.Slot(0).Mode);
    EXPECT_EQ(0u, s.Character.Slot(0).LoopCountRemaining); EXPECT_FLOAT_EQ(0, s.Secondary.GetFrame());
    EXPECT_EQ(Animation(s.Primary), s.Character.GetActiveAnimation());
    EXPECT_EQ(nullptr, s.Character.GetNextActiveAnimation()); EXPECT_FLOAT_EQ(10, s.Primary.GetFrame());
    RCKKeyedAnimation *warp = s.Character.Slot(0).Animation; ASSERT_NE(nullptr, warp);
    // The primary reached its noninterruptible end without a queued restart.
    // Return to that displayed terminal pose, then keep it after the main stops.
    Sample(*warp, s.Limb, 0, 100); Sample(*warp, s.Limb, 4, 20);
    EXPECT_FLOAT_EQ(1000, warp->GetLinkedFrameRate()); s.Character.Tick(2);
    EXPECT_EQ(nullptr, s.Character.GetActiveAnimation()); EXPECT_FLOAT_EQ(10, s.Primary.GetFrame());
    EXPECT_FLOAT_EQ(2, warp->GetFrame()); Pose(s.Limb, 60);
    EXPECT_EQ(Animation(*warp), s.Limb.GetExclusiveAnimation());
    s.Character.Tick(2); EXPECT_FLOAT_EQ(4, warp->GetFrame()); Pose(s.Limb, 20);
    EXPECT_EQ(1, s.Character.GetSecondaryAnimationsCount());
    s.Character.Tick(0); EXPECT_FLOAT_EQ(4, warp->GetFrame()); Pose(s.Limb, 20);
    EXPECT_EQ(Animation(*warp), s.Limb.GetExclusiveAnimation());
    s.Character.Tick(1); Pose(s.Limb, 20);
    EXPECT_EQ(0, s.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, s.Limb.GetExclusiveAnimation());
}

TEST_F(CharacterSecondaryReverseTest, ReverseWarpWithoutPrimaryFallsBackToRemovalAndReleasesGate) {
    ReverseScene s(Context); Prepare(s); ASSERT_EQ(CK_OK, s.Character.SetActiveAnimation(nullptr));
    Play(s, 2, CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
    ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, s.Character.Slot(0).Mode);
    s.Character.Tick(3); EXPECT_EQ(0, s.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(nullptr, s.Limb.GetExclusiveAnimation()); Pose(s.Limb, 100);
}

} // namespace
