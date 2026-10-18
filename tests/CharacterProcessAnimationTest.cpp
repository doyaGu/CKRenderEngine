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

class ProcessKeyed : public RCKKeyedAnimation {
public:
    explicit ProcessKeyed(CKContext *context) : RCKKeyedAnimation(context) {}
    void Bind(RCKCharacter &character, RCK3dEntity &root) { m_Character = &character; m_RootEntity = &root; }
    void Cache(float x) { m_Vector.Set(x, 0, 0); }
};

class ProcessCharacter : public RCKCharacter {
public:
    explicit ProcessCharacter(CKContext *context) : RCKCharacter(context) {}
    void Main(RCKKeyedAnimation *active, RCKAnimation *next = nullptr, float offset = 0) {
        m_ActiveAnimation = active; m_AnimDest = next; m_FrameDest = offset;
    }
    void Root(RCKBodyPart *root) { m_RootBodyPart = root; }
    CKSecondaryAnimation &Secondary(CKAnimation &animation, CK_SECONDARYANIMATION_RUNTIME_MODE mode,
                                   CKDWORD flags, int loops = 0, RCKKeyedAnimation *warp = nullptr,
                                   float start = 0, float duration = 2) {
        if (!m_SecondaryAnimations) {
            m_SecondaryAnimations = new CKSecondaryAnimation[4]();
            m_SecondaryAnimationsAllocated = 4;
        }
        CKSecondaryAnimation &slot = m_SecondaryAnimations[m_SecondaryAnimationsCount++];
        slot.AnimID = animation.GetID(); slot.Animation = warp; slot.Mode = mode;
        slot.Flags = flags; slot.LoopCountRemaining = loops; slot.WarpLength = duration;
        slot.SetStartingFrame(start);
        return slot;
    }
    int Count() const { return m_SecondaryAnimationsCount; }
    CKSecondaryAnimation &Slot(int index) { return m_SecondaryAnimations[index]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};

// A valid virtual customization: leave an independently supplied root center
// intact so the ProcessAnimation consumer cannot reconstruct it from GetFrame.
class CachedCenterCharacter : public ProcessCharacter {
public:
    explicit CachedCenterCharacter(CKContext *context) : ProcessCharacter(context) {}
    void AlignCharacterWithRootPosition() override {}
};

class CharacterProcessAnimationTest : public ::testing::Test {
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
    void TearDown() override { CKCloseContext(Context); CKShutdown(); }
    static void Curve(ProcessCharacter &character, RCKBodyPart &body, ProcessKeyed &group,
                      RCKObjectAnimation &channel, float length = 10, float first = 0, float last = 10) {
        character.SetMoveableFlags(character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        body.SetMoveableFlags(body.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        ASSERT_TRUE(character.AddChild(reinterpret_cast<CK3dEntity *>(&body), TRUE)); character.Root(&body);
        channel.Set3dEntity(reinterpret_cast<CK3dEntity *>(&body));
        VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); channel.SetLength(length);
        ASSERT_EQ(CK_OK, group.AddAnimation(&channel)); group.SetLength(length);
        group.Bind(character, body); group.LinkToFrameRate(TRUE, 1000); group.SetCanBeInterrupt(FALSE);
        group.CenterAnimation(0); group.SetFrame(0);
    }
    static void Pose(RCK3dEntity &entity, float x) {
        VxVector value; entity.GetPosition(&value, nullptr);
        EXPECT_NEAR(x, value.x, 0.0001f); EXPECT_NEAR(0, value.y, 0.0001f); EXPECT_NEAR(0, value.z, 0.0001f);
    }
    static void Detach(RCKBodyPart &body) { ASSERT_TRUE(body.SetParent(nullptr, TRUE)); }
};

TEST_F(CharacterProcessAnimationTest, MainFramesAccumulateWorldMotionAndClearRenderedFlag) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation channel(Context);
    Curve(c, body, a, channel); c.Main(&a);
    c.Tick(2); EXPECT_FLOAT_EQ(2, a.GetFrame()); Pose(c, 2); Pose(body, 2);
    c.Tick(3); EXPECT_FLOAT_EQ(5, a.GetFrame()); Pose(c, 5); Pose(body, 5);
    EXPECT_FLOAT_EQ(0, body.GetLocalMatrix()[3][0]);
    EXPECT_EQ(0, c.GetMoveableFlags() & VX_MOVEABLE_CHARACTERRENDERED); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, NonInterruptibleMainDisplaysEndBeforeClearingOnNextTick) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation channel(Context);
    Curve(c, body, a, channel); a.SetFrame(8); c.Main(&a);
    c.Tick(4); EXPECT_EQ(&a, c.GetActiveAnimation()); EXPECT_FLOAT_EQ(10, a.GetFrame()); Pose(c, 10);
    c.Tick(1); EXPECT_EQ(nullptr, c.GetActiveAnimation()); Pose(c, 10); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, CompletedMainUsesSourcePeriodWhenDestinationHasDifferentLength) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context), b(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, a, x, 10, 0, 10); Curve(c, body, b, y, 30, 0, 30);
    a.SetFrame(10); a.CenterAnimation(10); c.Main(&a, &b, 2);
    c.Tick(999); EXPECT_EQ(&b, c.GetActiveAnimation()); EXPECT_EQ(nullptr, c.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(2, b.GetFrame()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, MainForwardSwitchCarriesRemainderAndDestinationOffset) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context), b(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, a, x); Curve(c, body, b, y, 20, 0, 40);
    a.SetFrame(8); a.CenterAnimation(8); c.Main(&a, &b, 3);
    c.Tick(5); EXPECT_EQ(&b, c.GetActiveAnimation()); EXPECT_FLOAT_EQ(6, b.GetFrame()); Pose(c, 16);
    EXPECT_FLOAT_EQ(0, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, MainSelfQueueConsumesOneDestinationClipAndClampsAtItsEnd) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); a.SetFrame(8); a.CenterAnimation(8); c.Main(&a, &a);
    c.Tick(25); EXPECT_EQ(&a, c.GetActiveAnimation()); EXPECT_EQ(nullptr, c.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(10, a.GetFrame()); Pose(c, 20); Pose(body, 20); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, MainReverseHandoffPreservesExplicitStartAndConvertsSignedRate) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context), b(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, a, x); Curve(c, body, b, y, 20, 0, 20);
    c.Main(&a); c.Tick(2);
    a.LinkToFrameRate(TRUE, -1000); c.Main(&a, &b, 1);
    c.Tick(5); EXPECT_EQ(&b, c.GetActiveAnimation()); EXPECT_FLOAT_EQ(4, b.GetFrame());
    Pose(c, 3); Pose(body, 3);
    EXPECT_FLOAT_EQ(0, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, MainReverseWithoutDestinationConsumesSourceStartBeforeStopping) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Main(&a); c.Tick(2);
    a.LinkToFrameRate(TRUE, -1000);
    c.Tick(5); EXPECT_EQ(nullptr, c.GetActiveAnimation()); Pose(c, 0); Pose(body, 0); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, MainInterruptibleEndWithoutDestinationConsumesRemainingRootMotion) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); a.SetFrame(8); a.SetCanBeInterrupt(TRUE); c.Main(&a);
    c.Tick(3); EXPECT_EQ(nullptr, c.GetActiveAnimation()); Pose(c, 10); Pose(body, 10); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, MainEndAppliesAbsoluteControllerEndpointWithCachedCenter) {
    CachedCenterCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); a.Cache(-2); a.SetFrame(3); a.SetCanBeInterrupt(TRUE); c.Main(&a);
    // Endpoint position is the controller value 10 plus the existing cache -2.
    // Re-evaluating the cursor cache would give 7; adding old local 1 would give 9.
    c.Tick(8); EXPECT_EQ(nullptr, c.GetActiveAnimation()); Pose(c, 8); Pose(body, 8); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, NoActivePromotesQueuedAnimationAndAlignsRootWithoutAdvancing) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); a.SetFrame(4); c.Main(nullptr, &a);
    c.Tick(99); EXPECT_EQ(&a, c.GetActiveAnimation()); EXPECT_EQ(nullptr, c.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(4, a.GetFrame()); Pose(c, 4); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryOneshotDisplaysExactLastFrameBeforeRemoval) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_ONESHOT);
    ASSERT_EQ(10.0f, a.GetNextFrame(10));
    c.Tick(10); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(10, a.GetFrame()); Pose(body, 10);
    c.Tick(1); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryLoopDisplaysEndThenCarriesSinglePeriodRemainder) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOP);
    c.Tick(10); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(10, a.GetFrame());
    c.Tick(2); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(2, a.GetFrame());
    EXPECT_FLOAT_EQ(2, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryFiniteLoopsDecrementOnlyAfterEndAndRemoveInSameTick) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOPNTIMES, 2);
    c.Tick(10); ASSERT_EQ(1, c.Count()); EXPECT_EQ(2u, c.Slot(0).LoopCountRemaining);
    c.Tick(1); EXPECT_EQ(1u, c.Slot(0).LoopCountRemaining); EXPECT_FLOAT_EQ(1, a.GetFrame());
    c.Tick(9); ASSERT_EQ(1, c.Count()); EXPECT_EQ(1u, c.Slot(0).LoopCountRemaining);
    c.Tick(1); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryFiniteLoopsLastFrameClearsLoopFlagAndRetainsPose) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING,
        CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_LASTFRAME, 1);
    c.Tick(11); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(10, a.GetFrame());
    EXPECT_EQ(0u, c.Slot(0).Flags & CKSECONDARYANIMATION_LOOPNTIMES); EXPECT_EQ(0u, c.Slot(0).LoopCountRemaining);
    c.Tick(3); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(10, a.GetFrame()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryLoopCarriesRemainderAcrossSeveralPeriods) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOP);
    c.Tick(25); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(5, a.GetFrame()); Pose(body, 5);
    c.Tick(2); EXPECT_FLOAT_EQ(7, a.GetFrame()); EXPECT_FLOAT_EQ(7, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryLoopExactMultipleDisplaysLastFrameThenStartsNextPeriod) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOP);
    ASSERT_EQ(20.0f, a.GetNextFrame(20));
    c.Tick(20); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(10, a.GetFrame()); Pose(body, 10);
    c.Tick(1); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(1, a.GetFrame());
    EXPECT_FLOAT_EQ(1, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryFiniteLoopsConsumeEveryCrossedPeriodAndKeepRemainder) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOPNTIMES, 4);
    c.Tick(35); ASSERT_EQ(1, c.Count()); EXPECT_EQ(1u, c.Slot(0).LoopCountRemaining);
    EXPECT_FLOAT_EQ(5, a.GetFrame()); Pose(body, 5); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryFiniteLoopsLargeDeltaExhaustsAndRemovesInSameTick) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOPNTIMES, 3);
    c.Tick(35); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryFiniteLoopsLargeDeltaExhaustsAndRetainsLastFrame) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING,
        CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_LASTFRAME, 3);
    c.Tick(35); ASSERT_EQ(1, c.Count()); EXPECT_EQ(0u, c.Slot(0).LoopCountRemaining);
    EXPECT_EQ(0u, c.Slot(0).Flags & CKSECONDARYANIMATION_LOOPNTIMES); EXPECT_FLOAT_EQ(10, a.GetFrame()); Pose(body, 10);
    c.Tick(1); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(10, a.GetFrame()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryFiniteLoopsExactFinalEndSurvivesUntilStrictOverflow) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOPNTIMES, 2);
    ASSERT_EQ(20.0f, a.GetNextFrame(20));
    c.Tick(20); ASSERT_EQ(1, c.Count()); EXPECT_EQ(1u, c.Slot(0).LoopCountRemaining);
    EXPECT_FLOAT_EQ(10, a.GetFrame()); Pose(body, 10);
    c.Tick(1); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryLoopUsesActualTinyRemainderStrictlyAboveMultiple) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOP);
    // At 1000 fps each millisecond advances one frame. One float ULP above
    // 30 ms leaves exactly 1/524288 frame after three 10-frame periods.
    const float remainder = 1.0f / 524288.0f;
    const float elapsed = 30.0f + remainder;
    ASSERT_EQ(elapsed, a.GetNextFrame(elapsed));
    c.Tick(elapsed); ASSERT_EQ(1, c.Count()); EXPECT_EQ(remainder, a.GetFrame());
    EXPECT_EQ(remainder, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryFiniteLoopsStrictlyAboveFinalMultipleRemovesWithoutEpsilon) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LOOPNTIMES, 3);
    // This exact one-ULP excess must exhaust the final loop without an epsilon.
    const float elapsed = 30.0f + 1.0f / 524288.0f;
    ASSERT_EQ(elapsed, a.GetNextFrame(elapsed));
    c.Tick(elapsed); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryReverseInsideRangeEvaluatesRealCurve) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); a.SetFrame(7); a.LinkToFrameRate(TRUE, -1000);
    c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_ONESHOT);
    c.Tick(2); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(5, a.GetFrame()); EXPECT_FLOAT_EQ(5, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryUnlinkedCurveAdvancesOneFrameRegardlessOfMilliseconds) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); a.LinkToFrameRate(FALSE, 500);
    c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LASTFRAME);
    c.Tick(5000); ASSERT_EQ(1, c.Count()); EXPECT_FLOAT_EQ(1, a.GetFrame());
    EXPECT_FLOAT_EQ(1, body.GetLocalMatrix()[3][0]); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, SecondaryRemovalProcessesShiftedNeighboursInSameTick) {
    ProcessCharacter c(Context); RCKBodyPart aBody(Context), bBody(Context), liveBody(Context);
    ProcessKeyed a(Context), b(Context), live(Context); RCKObjectAnimation x(Context), y(Context), z(Context);
    Curve(c, aBody, a, x); Curve(c, bBody, b, y); Curve(c, liveBody, live, z);
    c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_ONESHOT);
    c.Secondary(b, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_ONESHOT);
    c.Secondary(live, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LASTFRAME);
    c.Tick(11); ASSERT_EQ(1, c.Count()); EXPECT_EQ(live.GetID(), c.Slot(0).AnimID); EXPECT_FLOAT_EQ(10, live.GetFrame());
    Detach(aBody); Detach(bBody); Detach(liveBody);
}

TEST_F(CharacterProcessAnimationTest, StartingWarpDisplaysEndThenTransfersOverflowAndStartingFrame) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed from(Context), to(Context), warp(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, from, x, 10, 0, 0); Curve(c, body, to, y, 10, 10, 20);
    ASSERT_FLOAT_EQ(3, warp.CreateTransition(&from, &to, 0, 2, 3)); warp.SetFrame(0);
    c.Secondary(to, CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, CKSECONDARYANIMATION_ONESHOT, 0, &warp, 3);
    c.Tick(1); ASSERT_EQ(1, c.Count()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, c.Slot(0).Mode);
    EXPECT_FLOAT_EQ(1, warp.GetFrame()); EXPECT_NEAR(6.5f, body.GetLocalMatrix()[3][0], 0.0001f);
    c.Tick(1); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, c.Slot(0).Mode); EXPECT_FLOAT_EQ(2, warp.GetFrame());
    c.Tick(1); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, c.Slot(0).Mode); EXPECT_FLOAT_EQ(4, to.GetFrame());
    EXPECT_NEAR(4, body.GetLocalMatrix()[3][0], 0.0001f); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, StoppingWarpDisplaysEndThenRemovesAndAdvancesNextSlot) {
    ProcessCharacter c(Context); RCKBodyPart body(Context), liveBody(Context); ProcessKeyed from(Context), to(Context), warp(Context), live(Context);
    RCKObjectAnimation x(Context), y(Context), z(Context); Curve(c, body, from, x, 10, 0, 0);
    Curve(c, body, to, y); Curve(c, liveBody, live, z);
    ASSERT_FLOAT_EQ(5, warp.CreateTransition(&from, &to, 0, 2, 5)); warp.SetFrame(0);
    c.Secondary(to, CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, CKSECONDARYANIMATION_DOWARP, 0, &warp);
    c.Secondary(live, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_LASTFRAME);
    c.Tick(2); ASSERT_EQ(2, c.Count()); EXPECT_FLOAT_EQ(2, warp.GetFrame()); EXPECT_NEAR(5, body.GetLocalMatrix()[3][0], .0001f);
    c.Tick(1); ASSERT_EQ(1, c.Count()); EXPECT_EQ(live.GetID(), c.Slot(0).AnimID); EXPECT_FLOAT_EQ(3, live.GetFrame());
    Detach(body); Detach(liveBody);
}

TEST_F(CharacterProcessAnimationTest, StoppingWarpTargetKeepsExactMainEndInsteadOfWrappingToStart) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed main(Context), secondary(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, main, x); Curve(c, body, secondary, y, 10, 10, 20);
    main.SetFrame(6); main.CenterAnimation(6); c.Main(&main); secondary.SetFrame(9);
    c.Secondary(secondary, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_DOWARP, 0, nullptr, 0, 2);
    c.Tick(1); ASSERT_EQ(1, c.Count()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, c.Slot(0).Mode);
    c.Tick(1); ASSERT_EQ(1, c.Count()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, c.Slot(0).Mode);
    RCKKeyedAnimation *warp = c.Slot(0).Animation; ASSERT_NE(nullptr, warp);
    ASSERT_EQ(1, warp->GetAnimationCount()); VxVector end;
    ASSERT_TRUE(warp->GetAnimation(0)->EvaluatePosition(2, end)); EXPECT_NEAR(10, end.x, .0001f);
    Detach(body);
}

TEST_F(CharacterProcessAnimationTest, ZeroDurationMainAllowsSecondaryStoppingWarpToFinish) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed main(Context), secondary(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, main, x); Curve(c, body, secondary, y, 10, 10, 20);
    main.SetLength(0); main.SetCanBeInterrupt(TRUE); c.Main(&main, &main); secondary.SetFrame(9);
    c.Secondary(secondary, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_DOWARP, 0, nullptr, 0, 2);
    c.Tick(2); ASSERT_EQ(1, c.Count()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, c.Slot(0).Mode);
    ASSERT_NE(nullptr, c.Slot(0).Animation); c.Tick(3); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, AutomaticStoppingWarpUsesDynamicCharacterLifetimeOutsideCreationMode) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed main(Context), secondary(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, main, x); Curve(c, body, secondary, y, 10, 10, 20);
    Context->ChangeObjectDynamic(&c, TRUE); ASSERT_TRUE(c.IsDynamic()); ASSERT_FALSE(Context->IsInDynamicCreationMode());
    main.SetFrame(2); c.Main(&main); secondary.SetFrame(9);
    c.Secondary(secondary, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_DOWARP);
    c.Tick(2); ASSERT_EQ(1, c.Count()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, c.Slot(0).Mode);
    RCKKeyedAnimation *warp = c.Slot(0).Animation; ASSERT_NE(nullptr, warp); EXPECT_TRUE(warp->IsDynamic());
    EXPECT_EQ(Context->GetObject(warp->GetID()), warp); EXPECT_EQ(reinterpret_cast<CKCharacter *>(&c), warp->GetCharacter());
    EXPECT_EQ(CKCID_KEYEDANIMATION, warp->GetClassID()); c.Tick(3); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, AutomaticStoppingWarpUsesStaticCharacterLifetimeDuringDynamicCreationMode) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed main(Context), secondary(Context);
    RCKObjectAnimation x(Context), y(Context); Curve(c, body, main, x); Curve(c, body, secondary, y, 10, 10, 20);
    ASSERT_FALSE(c.IsDynamic()); Context->m_InDynamicCreationMode = TRUE;
    main.SetFrame(2); c.Main(&main); secondary.SetFrame(9);
    c.Secondary(secondary, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_DOWARP);
    c.Tick(2); Context->m_InDynamicCreationMode = FALSE;
    ASSERT_EQ(1, c.Count()); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, c.Slot(0).Mode);
    RCKKeyedAnimation *warp = c.Slot(0).Animation; ASSERT_NE(nullptr, warp); EXPECT_FALSE(warp->IsDynamic());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&c), warp->GetCharacter()); c.Tick(3); EXPECT_EQ(0, c.Count()); Detach(body);
}

TEST_F(CharacterProcessAnimationTest, ForeignPrimaryCannotBuildReturnWarpAndExpiredSecondaryIsRemoved) {
    ProcessCharacter c(Context), foreign(Context); RCKBodyPart body(Context), foreignBody(Context);
    ProcessKeyed main(Context), secondary(Context); RCKObjectAnimation x(Context), y(Context);
    Curve(foreign, foreignBody, main, x); Curve(c, body, secondary, y, 10, 10, 20);
    main.SetFrame(2); c.Main(&main); secondary.SetFrame(9);
    c.Secondary(secondary, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_DOWARP);
    c.Tick(2); EXPECT_EQ(0, c.Count()); EXPECT_EQ(nullptr, c.Slot(0).Animation);
    EXPECT_EQ(&main, c.GetActiveAnimation()); EXPECT_EQ(reinterpret_cast<CKCharacter *>(&foreign), main.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&c), secondary.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&c), body.GetParent());
    Detach(body); Detach(foreignBody);
}

TEST_F(CharacterProcessAnimationTest, IdenticalMainAndSecondaryCannotBuildEmptyReturnWarp) {
    ProcessCharacter c(Context); RCKBodyPart body(Context); ProcessKeyed a(Context); RCKObjectAnimation x(Context);
    Curve(c, body, a, x); a.SetFrame(8); c.Main(&a);
    c.Secondary(a, CKSECONDARYANIMATIONRUNTIME_PLAYING, CKSECONDARYANIMATION_DOWARP);
    c.Tick(3); EXPECT_EQ(0, c.Count()); EXPECT_EQ(nullptr, c.Slot(0).Animation);
    EXPECT_EQ(&a, c.GetActiveAnimation()); EXPECT_FLOAT_EQ(10, a.GetFrame()); Pose(c, 10); Detach(body);
}

} // namespace
