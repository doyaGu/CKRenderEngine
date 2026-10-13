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
static CK3dEntity *MixedEntity(RCK3dEntity &entity) {
    return reinterpret_cast<CK3dEntity *>(&entity);
}

class PrimaryMixedTimeRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation SourceChannel, DestinationChannel;
    RCKKeyedAnimation Source, Destination;

    explicit PrimaryMixedTimeRig(CKContext *context, float destinationLength = 32)
        : Character(context), Root(context), SourceChannel(context), DestinationChannel(context),
          Source(context), Destination(context) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100;
        Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, MixedEntity(Character));
        Curve(SourceChannel, Source, 10, 5, 15);
        Curve(DestinationChannel, Destination, destinationLength, 100, 100 + 2 * destinationLength);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Source));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Destination));
        Source.SetFrame(0); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source));
    }
    ~PrimaryMixedTimeRig() { Root.SetParent(nullptr, TRUE); }
    void Tick(float milliseconds) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(milliseconds);
    }
private:
    void Curve(RCKObjectAnimation &channel, RCKKeyedAnimation &animation,
               float length, float first, float last) {
        channel.Set3dEntity(MixedEntity(Root));
        VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel)); animation.SetLength(length);
        animation.LinkToFrameRate(TRUE, 1024); animation.SetCanBeInterrupt(FALSE);
    }
};

class CharacterPrimaryMixedTimeTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(PrimaryMixedTimeRig &rig, float x) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
                                     &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix()};
        // Independent complete rigid-pose oracle: both world poses translate
        // by x; the centered root has identity local pose.
        for (int which = 0; which < 4; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0 && which != 3) expected = x;
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0003f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(MixedEntity(rig.Character), rig.Root.GetParent());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void Cursor(PrimaryMixedTimeRig &rig, RCKKeyedAnimation &animation,
                       float frame, float rawRootPosition) {
        EXPECT_NEAR(frame, animation.GetFrame(), .0002f);
        const VxVector &center = animation.GetRootVectorInternal();
        EXPECT_NEAR(-rawRootPosition, center.x, .0002f);
        EXPECT_FLOAT_EQ(0, center.y); EXPECT_FLOAT_EQ(0, center.z);
        EXPECT_EQ(&animation == &rig.Source ? &rig.SourceChannel : &rig.DestinationChannel,
                  animation.GetRootAnimationInternal());
    }
    static void Active(PrimaryMixedTimeRig &rig, RCKKeyedAnimation &animation,
                       float frame, float rawRootPosition) {
        EXPECT_EQ(&animation, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        Cursor(rig, animation, frame, rawRootPosition);
    }
    static void WarmEight(PrimaryMixedTimeRig &rig) {
        ASSERT_EQ(8.0f, rig.Source.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
        Active(rig, rig.Source, 8, 13); Pose(rig, 108);
    }
    static void WarmNineAndHalf(PrimaryMixedTimeRig &rig) {
        ASSERT_EQ(9.5f, rig.Source.GetNextFrame(9.27734375f)); rig.Tick(9.27734375f);
        Active(rig, rig.Source, 9.5f, 14.5f); Pose(rig, 109.5f);
    }
    static void QueueDestination(PrimaryMixedTimeRig &rig) {
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART));
        ASSERT_EQ(&rig.Destination, rig.Character.GetNextActiveAnimation());
    }
};

TEST_F(CharacterPrimaryMixedTimeTest, UnlinkedPlaybackCountsProcessInvocationsAndIgnoresStoredFPS) {
    const float rates[] = {-2048, 0, 4096};
    for (float rate : rates) {
        SCOPED_TRACE(rate); PrimaryMixedTimeRig one(Context), two(Context);
        one.Source.LinkToFrameRate(FALSE, rate); two.Source.LinkToFrameRate(FALSE, rate);
        ASSERT_EQ(1.0f, one.Source.GetNextFrame(15.625f));
        one.Tick(15.625f); two.Tick(7.8125f); two.Tick(7.8125f);
        Active(one, one.Source, 1, 6); Pose(one, 101);
        Active(two, two.Source, 2, 7); Pose(two, 102);
        // Equal elapsed time has different native Process counts.
    }
}

TEST_F(CharacterPrimaryMixedTimeTest, LinkedToUnlinkedReceivesRemainingFractionThenOneFramePerCall) {
    const float deltas[] = {3.90625f, 7.8125f};
    const float next[] = {12, 16}, fractions[] = {.5f, .75f};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(i); PrimaryMixedTimeRig rig(Context); WarmEight(rig);
        rig.Destination.LinkToFrameRate(FALSE, -2048); QueueDestination(rig);
        ASSERT_EQ(next[i], rig.Source.GetNextFrame(deltas[i])); rig.Tick(deltas[i]);
        Active(rig, rig.Destination, fractions[i], 100 + 2 * fractions[i]);
        Pose(rig, 110 + 2 * fractions[i]);
        ASSERT_EQ(fractions[i] + 1, rig.Destination.GetNextFrame(0)); rig.Tick(0);
        Active(rig, rig.Destination, fractions[i] + 1, 102 + 2 * fractions[i]);
        Pose(rig, 112 + 2 * fractions[i]);
    }
}

TEST_F(CharacterPrimaryMixedTimeTest, UnlinkedToLinkedReceivesRemainingFractionOfActualTime) {
    PrimaryMixedTimeRig rig(Context); WarmNineAndHalf(rig);
    rig.Source.LinkToFrameRate(FALSE, -2048); rig.Destination.LinkToFrameRate(TRUE, 512);
    QueueDestination(rig);
    ASSERT_EQ(10.5f, rig.Source.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
    // Half a Process remains; 512 FPS * 7.8125 ms gives four frames.
    Active(rig, rig.Destination, 2, 104); Pose(rig, 114);
    ASSERT_EQ(3.0f, rig.Destination.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
    Active(rig, rig.Destination, 3, 106); Pose(rig, 116);
}

TEST_F(CharacterPrimaryMixedTimeTest, ZeroElapsedUnlinkedSourceCrossesButLinkedTargetReceivesZeroTime) {
    PrimaryMixedTimeRig rig(Context); WarmNineAndHalf(rig);
    rig.Source.LinkToFrameRate(FALSE, -2048); rig.Destination.LinkToFrameRate(TRUE, 512);
    QueueDestination(rig); ASSERT_EQ(10.5f, rig.Source.GetNextFrame(0)); rig.Tick(0);
    Active(rig, rig.Destination, 0, 100); Pose(rig, 110);
    ASSERT_EQ(0.0f, rig.Destination.GetNextFrame(0)); rig.Tick(0);
    Active(rig, rig.Destination, 0, 100); Pose(rig, 110);
}

TEST_F(CharacterPrimaryMixedTimeTest, BothUnlinkedIgnoreStoredFPSAndElapsedTimeDuringHandoff) {
    const float sourceRates[] = {-2048, 0, 4096};
    const float targetRates[] = {0, -2048, 4096};
    const float deltas[] = {0, 1000, .25f};
    for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(i); PrimaryMixedTimeRig rig(Context); WarmNineAndHalf(rig);
        rig.Source.LinkToFrameRate(FALSE, sourceRates[i]);
        rig.Destination.LinkToFrameRate(FALSE, targetRates[i]); QueueDestination(rig);
        ASSERT_EQ(10.5f, rig.Source.GetNextFrame(deltas[i])); rig.Tick(deltas[i]);
        Active(rig, rig.Destination, .5f, 101); Pose(rig, 111);
    }
}

TEST_F(CharacterPrimaryMixedTimeTest, UnlinkedExactSourceEndHandsOffZeroRemainder) {
    PrimaryMixedTimeRig rig(Context);
    ASSERT_EQ(9.0f, rig.Source.GetNextFrame(8.7890625f)); rig.Tick(8.7890625f);
    Active(rig, rig.Source, 9, 14); Pose(rig, 109);
    rig.Source.LinkToFrameRate(FALSE, -2048); rig.Destination.LinkToFrameRate(FALSE, 0);
    QueueDestination(rig); ASSERT_EQ(10.0f, rig.Source.GetNextFrame(0)); rig.Tick(0);
    Active(rig, rig.Destination, 0, 100); Pose(rig, 110);
}

TEST_F(CharacterPrimaryMixedTimeTest, LinkedExactEndToUnlinkedHandsOffZeroProcessFraction) {
    PrimaryMixedTimeRig rig(Context); WarmEight(rig);
    rig.Destination.LinkToFrameRate(FALSE, -2048); QueueDestination(rig);
    ASSERT_EQ(10.0f, rig.Source.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
    Active(rig, rig.Destination, 0, 100); Pose(rig, 110);
    rig.Tick(0); Active(rig, rig.Destination, 1, 102); Pose(rig, 112);
}

TEST_F(CharacterPrimaryMixedTimeTest, ZeroElapsedLinkedSourceKeepsPendingUnlinkedDestination) {
    PrimaryMixedTimeRig rig(Context); WarmEight(rig);
    rig.Destination.LinkToFrameRate(FALSE, -2048); QueueDestination(rig);
    ASSERT_EQ(8.0f, rig.Source.GetNextFrame(0)); rig.Tick(0);
    EXPECT_EQ(&rig.Source, rig.Character.GetActiveAnimation());
    EXPECT_EQ(&rig.Destination, rig.Character.GetNextActiveAnimation());
    Cursor(rig, rig.Source, 8, 13); Pose(rig, 108);
}

TEST_F(CharacterPrimaryMixedTimeTest, UnlinkedToNegativeLinkedToStartKeepsLiteralZero) {
    PrimaryMixedTimeRig rig(Context); WarmNineAndHalf(rig);
    rig.Source.LinkToFrameRate(FALSE, 0); rig.Destination.LinkToFrameRate(TRUE, -512);
    QueueDestination(rig); ASSERT_EQ(10.5f, rig.Source.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
    // The signed target remainder is -2, but distinct TOSTART is absolute
    // frame zero, not an implicit reverse restart from the target's end.
    Active(rig, rig.Destination, 0, 100); Pose(rig, 110);
}

TEST_F(CharacterPrimaryMixedTimeTest, UnlinkedToPausedLinkedTargetDoesNotUseStoredSourceFPS) {
    PrimaryMixedTimeRig rig(Context); WarmNineAndHalf(rig);
    rig.Source.LinkToFrameRate(FALSE, -2048); rig.Destination.LinkToFrameRate(TRUE, 0);
    QueueDestination(rig); ASSERT_EQ(10.5f, rig.Source.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
    Active(rig, rig.Destination, 0, 100); Pose(rig, 110);
    ASSERT_EQ(0.0f, rig.Destination.GetNextFrame(1000)); rig.Tick(1000);
    Active(rig, rig.Destination, 0, 100); Pose(rig, 110);
}

TEST_F(CharacterPrimaryMixedTimeTest, ReverseLinkedSourceToUnlinkedUsesPositiveFractionFromLiteralStart) {
    PrimaryMixedTimeRig rig(Context); WarmEight(rig);
    rig.Source.LinkToFrameRate(TRUE, -1024); rig.Destination.LinkToFrameRate(FALSE, -2048);
    QueueDestination(rig); ASSERT_EQ(-10.0f, rig.Source.GetNextFrame(17.578125f)); rig.Tick(17.578125f);
    // Source advances -18, finishes after -8; ten eighteenths of one
    // unlinked target frame remains. Its stored negative FPS is irrelevant.
    Active(rig, rig.Destination, 5.0f / 9, 100 + 10.0f / 9); Pose(rig, 100 + 10.0f / 9);
    rig.Tick(0); Active(rig, rig.Destination, 14.0f / 9, 100 + 28.0f / 9);
    Pose(rig, 100 + 28.0f / 9);
}

TEST_F(CharacterPrimaryMixedTimeTest, SelfUnlinkedQueueConsumesFractionOnceWithoutPersistentLoop) {
    PrimaryMixedTimeRig rig(Context); WarmNineAndHalf(rig); rig.Source.LinkToFrameRate(FALSE, -2048);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Source, CK_TRANSITION_LOOPIFEQUAL));
    ASSERT_EQ(&rig.Source, rig.Character.GetNextActiveAnimation());
    ASSERT_EQ(10.5f, rig.Source.GetNextFrame(0)); rig.Tick(0);
    Active(rig, rig.Source, .5f, 5.5f); Pose(rig, 110.5f);
    rig.Tick(0); Active(rig, rig.Source, 1.5f, 6.5f); Pose(rig, 111.5f);
}

TEST_F(CharacterPrimaryMixedTimeTest, MixedRemainderClampsOnlyOneShortDestination) {
    PrimaryMixedTimeRig rig(Context, .25f); WarmEight(rig);
    rig.Destination.LinkToFrameRate(FALSE, -2048); QueueDestination(rig);
    ASSERT_EQ(12.0f, rig.Source.GetNextFrame(3.90625f)); rig.Tick(3.90625f);
    Active(rig, rig.Destination, .25f, 100.5f); Pose(rig, 110.5f);
    rig.Tick(0); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
    EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); Pose(rig, 110.5f);
    rig.Tick(1000); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 110.5f);
}

TEST_F(CharacterPrimaryMixedTimeTest, UnlinkedNoQueueCompletesSourceAccordingToInterruptibility) {
    for (int interruptible = 0; interruptible < 2; ++interruptible) {
        SCOPED_TRACE(interruptible); PrimaryMixedTimeRig rig(Context); WarmNineAndHalf(rig);
        rig.Source.LinkToFrameRate(FALSE, -2048); rig.Source.SetCanBeInterrupt(interruptible);
        ASSERT_EQ(10.5f, rig.Source.GetNextFrame(0)); rig.Tick(0);
        EXPECT_EQ(interruptible ? nullptr : &rig.Source, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); Pose(rig, 110);
        if (!interruptible) Cursor(rig, rig.Source, 10, 15);
        else EXPECT_NEAR(9.5f, rig.Source.GetFrame(), .0002f);
        rig.Tick(0); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); Pose(rig, 110);
    }
}
} // namespace
