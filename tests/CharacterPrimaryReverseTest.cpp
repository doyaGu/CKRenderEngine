#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"
#include "RCKCharacter.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

static CK3dEntity *ReverseEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }

class PrimaryReverseRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation SourceChannel, DestinationChannel;
    RCKKeyedAnimation Source, Destination;
    bool QuarterTurn;

    explicit PrimaryReverseRig(CKContext *context, float destinationLength = 32, bool quarterTurn = false)
        : Character(context), Root(context), SourceChannel(context), DestinationChannel(context),
          Source(context), Destination(context), QuarterTurn(quarterTurn) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100;
        if (quarterTurn) {
            world[0][0] = 0; world[0][2] = -1;
            world[2][0] = 1; world[2][2] = 0; world[3][2] = 20;
        }
        Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, ReverseEntity(Character));
        Curve(SourceChannel, Source, 10, 5, 15);
        Curve(DestinationChannel, Destination, destinationLength, 100, 100 + destinationLength * 2);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Source));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Destination));
        Source.SetFrame(0); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source));
    }
    ~PrimaryReverseRig() { Root.SetParent(nullptr, TRUE); }
    void Tick(float milliseconds) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(milliseconds);
    }
private:
    void Curve(RCKObjectAnimation &channel, RCKKeyedAnimation &animation, float length, float first, float last) {
        channel.Set3dEntity(ReverseEntity(Root)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel)); animation.SetLength(length);
        animation.LinkToFrameRate(TRUE, 1024); animation.SetCanBeInterrupt(FALSE);
    }
};

class CharacterPrimaryReverseTest : public ::testing::Test {
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
    static void Pose(PrimaryReverseRig &rig, float x, float z = 0) {
        const float identity[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const float quarter[3][3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}};
        const float inverse[3][3] = {{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}};
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
                                     &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix()};
        for (int which = 0; which < 4; ++which) {
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    float expected = 0;
                    if (row < 3 && column < 3) {
                        if (which < 2) expected = rig.QuarterTurn ? quarter[row][column] : identity[row][column];
                        else if (which == 2) expected = identity[row][column];
                        else expected = rig.QuarterTurn ? inverse[row][column] : identity[row][column];
                    } else if (row == 3 && column == 3) expected = 1;
                    else if (row == 3 && which != 3) {
                        if (column == 0) expected = x;
                        if (column == 2) expected = z;
                    }
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0002f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
            }
        }
        EXPECT_EQ(ReverseEntity(rig.Character), rig.Root.GetParent());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void Active(PrimaryReverseRig &rig, RCKKeyedAnimation &animation, float frame, float rawRootPosition) {
        EXPECT_EQ(&animation, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frame, animation.GetFrame(), .0002f);
        const VxVector &center = animation.GetRootVectorInternal();
        EXPECT_NEAR(-rawRootPosition, center.x, .0002f); EXPECT_FLOAT_EQ(0, center.y); EXPECT_FLOAT_EQ(0, center.z);
        RCKObjectAnimation *expected = &animation == &rig.Source ? &rig.SourceChannel : &rig.DestinationChannel;
        EXPECT_EQ(expected, animation.GetRootAnimationInternal());
    }
    static void WarmEight(PrimaryReverseRig &rig) {
        ASSERT_EQ(8.0f, rig.Source.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
        Active(rig, rig.Source, 8, 13);
        Pose(rig, rig.QuarterTurn ? 100 : 108, rig.QuarterTurn ? 12 : 0);
        rig.Source.LinkToFrameRate(TRUE, -1024);
    }
    static void QueueSelf(PrimaryReverseRig &rig) {
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Source, CK_TRANSITION_LOOPIFEQUAL));
        ASSERT_EQ(&rig.Source, rig.Character.GetNextActiveAnimation());
        CKAnimation *destination = nullptr; float offset = -99;
        rig.Character.GetWarperParameters(nullptr, nullptr, nullptr, &destination, &offset);
        EXPECT_EQ(&rig.Source, destination); EXPECT_FLOAT_EQ(0, offset);
    }
};

TEST_F(CharacterPrimaryReverseTest, StartingAtLengthWithNegativeRateAdvancesNonInterruptibleClip) {
    PrimaryReverseRig rig(Context); ASSERT_EQ(10.0f, rig.Source.GetNextFrame(9.765625f)); rig.Tick(9.765625f);
    Active(rig, rig.Source, 10, 15); Pose(rig, 110);
    rig.Source.LinkToFrameRate(TRUE, -1024); ASSERT_EQ(8.0f, rig.Source.GetNextFrame(1.953125f));
    rig.Tick(1.953125f); Active(rig, rig.Source, 8, 13); Pose(rig, 108);
}

TEST_F(CharacterPrimaryReverseTest, ZeroElapsedReverseAtLengthKeepsSourceAndPendingQueue) {
    for (int distinct = 0; distinct < 2; ++distinct) {
        SCOPED_TRACE(distinct); PrimaryReverseRig rig(Context);
        ASSERT_EQ(10.0f, rig.Source.GetNextFrame(9.765625f)); rig.Tick(9.765625f);
        Active(rig, rig.Source, 10, 15); Pose(rig, 110);
        rig.Source.LinkToFrameRate(TRUE, -1024);
        RCKKeyedAnimation *pending = distinct ? &rig.Destination : &rig.Source;
        if (distinct) {
            ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(pending, CK_TRANSITION_TOSTART));
        } else {
            QueueSelf(rig);
        }
        ASSERT_EQ(pending, rig.Character.GetNextActiveAnimation());
        ASSERT_EQ(10.0f, rig.Source.GetNextFrame(0)); rig.Tick(0);
        EXPECT_EQ(&rig.Source, rig.Character.GetActiveAnimation());
        EXPECT_EQ(pending, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(10, rig.Source.GetFrame()); EXPECT_FLOAT_EQ(-15, rig.Source.GetRootVectorInternal().x);
        EXPECT_FLOAT_EQ(0, rig.Source.GetRootVectorInternal().y); EXPECT_FLOAT_EQ(0, rig.Source.GetRootVectorInternal().z);
        EXPECT_EQ(&rig.SourceChannel, rig.Source.GetRootAnimationInternal()); Pose(rig, 110);
        ASSERT_EQ(8.0f, rig.Source.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
        EXPECT_EQ(&rig.Source, rig.Character.GetActiveAnimation());
        EXPECT_EQ(pending, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(8, rig.Source.GetFrame()); EXPECT_FLOAT_EQ(-13, rig.Source.GetRootVectorInternal().x);
        EXPECT_FLOAT_EQ(0, rig.Source.GetRootVectorInternal().y); EXPECT_FLOAT_EQ(0, rig.Source.GetRootVectorInternal().z);
        EXPECT_EQ(&rig.SourceChannel, rig.Source.GetRootAnimationInternal()); Pose(rig, 108);
    }
}

TEST_F(CharacterPrimaryReverseTest, ExactZeroDisplaysSourceEndpointBeforeStrictUnderflowStopsIt) {
    PrimaryReverseRig rig(Context); WarmEight(rig); ASSERT_EQ(0.0f, rig.Source.GetNextFrame(7.8125f));
    rig.Tick(7.8125f); Active(rig, rig.Source, 0, 5); Pose(rig, 100);
    ASSERT_EQ(-1.0f, rig.Source.GetNextFrame(.9765625f)); rig.Tick(.9765625f);
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); Pose(rig, 100);
}

TEST_F(CharacterPrimaryReverseTest, NoQueueLargeReverseDeltaConsumesSourceEndpointOnce) {
    for (int interruptible = 0; interruptible < 2; ++interruptible) {
        SCOPED_TRACE(interruptible); PrimaryReverseRig rig(Context); WarmEight(rig);
        rig.Source.SetCanBeInterrupt(interruptible); ASSERT_EQ(-10.0f, rig.Source.GetNextFrame(17.578125f));
        rig.Tick(17.578125f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); Pose(rig, 100);
        // Inactive source cursor retains the last applied pose, matching the
        // forward handoff convention; endpoint motion is a character effect.
        EXPECT_FLOAT_EQ(8, rig.Source.GetFrame());
        rig.Tick(31.25f); Pose(rig, 100); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
    }
}

TEST_F(CharacterPrimaryReverseTest, SelfQueueTinyUnderflowRestartsFromEndAndAppliesOnlyRemainder) {
    PrimaryReverseRig rig(Context); WarmEight(rig); QueueSelf(rig);
    ASSERT_EQ(-.5f, rig.Source.GetNextFrame(8.30078125f)); rig.Tick(8.30078125f);
    Active(rig, rig.Source, 9.5f, 14.5f); Pose(rig, 99.5f);
}

TEST_F(CharacterPrimaryReverseTest, SelfQueueConsumesOneReverseClipAndClampsOvershoot) {
    const float milliseconds[] = {17.578125f, 31.25f};
    const float next[] = {-10, -24};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(milliseconds[i]); PrimaryReverseRig rig(Context); WarmEight(rig); QueueSelf(rig);
        ASSERT_EQ(next[i], rig.Source.GetNextFrame(milliseconds[i])); rig.Tick(milliseconds[i]);
        Active(rig, rig.Source, 0, 5); Pose(rig, 90);
    }
}

TEST_F(CharacterPrimaryReverseTest, SingleSelfRestartMatchesExactBoundarySplitWithoutAnotherQueue) {
    PrimaryReverseRig single(Context), split(Context); WarmEight(single); WarmEight(split); QueueSelf(single); QueueSelf(split);
    ASSERT_EQ(-10.0f, single.Source.GetNextFrame(17.578125f)); single.Tick(17.578125f);
    ASSERT_EQ(0.0f, split.Source.GetNextFrame(7.8125f)); split.Tick(7.8125f);
    EXPECT_EQ(&split.Source, split.Character.GetActiveAnimation()); EXPECT_EQ(&split.Source, split.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(0, split.Source.GetFrame()); Pose(split, 100);
    ASSERT_EQ(-10.0f, split.Source.GetNextFrame(9.765625f)); split.Tick(9.765625f);
    Active(single, single.Source, 0, 5); Active(split, split.Source, 0, 5); Pose(single, 90); Pose(split, 90);
}

TEST_F(CharacterPrimaryReverseTest, ConsumedSelfQueueDoesNotCreatePersistentReverseLoop) {
    PrimaryReverseRig rig(Context); WarmEight(rig); QueueSelf(rig);
    ASSERT_EQ(-1.0f, rig.Source.GetNextFrame(8.7890625f)); rig.Tick(8.7890625f);
    Active(rig, rig.Source, 9, 14); Pose(rig, 99);
    rig.Tick(.9765625f); Active(rig, rig.Source, 8, 13); Pose(rig, 98);
    ASSERT_EQ(0.0f, rig.Source.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
    Active(rig, rig.Source, 0, 5); Pose(rig, 90);
    rig.Tick(.9765625f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 90);
    rig.Tick(31.25f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 90);
}

TEST_F(CharacterPrimaryReverseTest, ReverseSelfRestartUsesCharacterWorldBasisAndPreservesRootBasis) {
    PrimaryReverseRig rig(Context, 32, true); WarmEight(rig); QueueSelf(rig);
    ASSERT_EQ(-10.0f, rig.Source.GetNextFrame(17.578125f)); rig.Tick(17.578125f);
    Active(rig, rig.Source, 0, 5); Pose(rig, 100, 30);
}

TEST_F(CharacterPrimaryReverseTest, DifferentPositiveRateTargetReceivesRemainingElapsedTimeFromLiteralStart) {
    PrimaryReverseRig single(Context), split(Context); WarmEight(single); WarmEight(split);
    single.Destination.LinkToFrameRate(TRUE, 512); split.Destination.LinkToFrameRate(TRUE, 512);
    ASSERT_EQ(CK_OK, single.Character.SetNextActiveAnimation(&single.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(CK_OK, split.Character.SetNextActiveAnimation(&split.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(-1.0f, single.Source.GetNextFrame(8.7890625f)); single.Tick(8.7890625f);
    split.Tick(7.8125f); Pose(split, 100); EXPECT_EQ(&split.Destination, split.Character.GetNextActiveAnimation());
    split.Tick(.9765625f);
    Active(single, single.Destination, .5f, 101); Active(split, split.Destination, .5f, 101);
    Pose(single, 101); Pose(split, 101);
}

TEST_F(CharacterPrimaryReverseTest, DifferentNegativeRateToStartKeepsAbsoluteZeroInsteadOfInventingEndEntry) {
    PrimaryReverseRig rig(Context); WarmEight(rig); rig.Destination.LinkToFrameRate(TRUE, -512);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(-1.0f, rig.Source.GetNextFrame(8.7890625f)); rig.Tick(8.7890625f);
    Active(rig, rig.Destination, 0, 100); Pose(rig, 100);
    rig.Tick(.9765625f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 100);
}

TEST_F(CharacterPrimaryReverseTest, DistinctPositiveTargetOvershootConsumesOnlyOneShortDestination) {
    PrimaryReverseRig rig(Context, 2); WarmEight(rig); rig.Destination.LinkToFrameRate(TRUE, 512);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(-10.0f, rig.Source.GetNextFrame(17.578125f)); rig.Tick(17.578125f);
    Active(rig, rig.Destination, 2, 104); Pose(rig, 104);
    rig.Tick(.9765625f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 104);
    rig.Tick(31.25f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 104);
}

TEST_F(CharacterPrimaryReverseTest, DistinctPositiveTargetRemainderMovesInRotatedCharacterWorldBasis) {
    PrimaryReverseRig rig(Context, 32, true); WarmEight(rig); rig.Destination.LinkToFrameRate(TRUE, 512);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(-1.0f, rig.Source.GetNextFrame(8.7890625f)); rig.Tick(8.7890625f);
    Active(rig, rig.Destination, .5f, 101); Pose(rig, 100, 19);
}

} // namespace
