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
static CK3dEntity *MotionEntityAPI(RCK3dEntity *entity) {
    return reinterpret_cast<CK3dEntity *>(entity);
}

class PrimaryMotionRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation SourceChannel, DestinationChannel;
    RCKKeyedAnimation Source, Destination;

    explicit PrimaryMotionRig(CKContext *context, float destinationLength = 100, bool quarterTurn = false)
        : Character(context), Root(context), SourceChannel(context), DestinationChannel(context),
          Source(context), Destination(context) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100;
        if (quarterTurn) {
            // Independent exact quarter-turn basis: local +X maps to world -Z.
            world[0][0] = 0; world[0][2] = -1;
            world[2][0] = 1; world[2][2] = 0; world[3][2] = 20;
        }
        Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, MotionEntityAPI(&Character));
        Curve(SourceChannel, Source, 10, 5, 15);
        Curve(DestinationChannel, Destination, destinationLength, 100, 100 + destinationLength * 2);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Source));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Destination));
        Source.SetFrame(0); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source));
    }

    ~PrimaryMotionRig() { Root.SetParent(nullptr, TRUE); }

    void Tick(float milliseconds) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(milliseconds);
    }

private:
    void Curve(RCKObjectAnimation &channel, RCKKeyedAnimation &animation, float length, float first, float last) {
        channel.Set3dEntity(MotionEntityAPI(&Root));
        VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel));
        animation.SetLength(length); animation.LinkToFrameRate(TRUE, 1000);
        animation.SetCanBeInterrupt(FALSE);
    }
};

class CharacterPrimaryMotionTest : public ::testing::Test {
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

    static void ExpectPose(PrimaryMotionRig &rig, float x, float z = 0) {
        VxVector character, root;
        rig.Character.GetPosition(&character, nullptr); rig.Root.GetPosition(&root, nullptr);
        EXPECT_NEAR(x, character.x, .0002f); EXPECT_NEAR(0, character.y, .0002f); EXPECT_NEAR(z, character.z, .0002f);
        EXPECT_NEAR(x, root.x, .0002f); EXPECT_NEAR(0, root.y, .0002f); EXPECT_NEAR(z, root.z, .0002f);
        const VxMatrix &local = rig.Root.GetLocalMatrix();
        EXPECT_NEAR(0, local[3][0], .0002f); EXPECT_NEAR(0, local[3][1], .0002f); EXPECT_NEAR(0, local[3][2], .0002f);
    }
    static void ExpectQuarterTurn(const VxMatrix &matrix) {
        const float basis[3][3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                EXPECT_NEAR(basis[row][column], matrix[row][column], .0002f) << row << ',' << column;
    }
    static void ExpectPreservedRootBasis(PrimaryMotionRig &rig) {
        // AddBodyPart keeps the new root's original world pose. Its identity
        // world rotation therefore has the inverse quarter-turn local basis.
        // Position-only channels preserve both sides of this composition.
        const float inverse[3][3] = {{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}};
        const VxMatrix &world = rig.Root.GetWorldMatrix(), &local = rig.Root.GetLocalMatrix();
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) {
                EXPECT_NEAR(row == column ? 1.0f : 0.0f, world[row][column], .0002f)
                    << "root world " << row << ',' << column;
                EXPECT_NEAR(inverse[row][column], local[row][column], .0002f)
                    << "root local " << row << ',' << column;
            }
    }
};

TEST_F(CharacterPrimaryMotionTest, SelfQueuedRestartPlaysOneNewClipAndClampsAfterItsEnd) {
    const float deltas[] = {2, 12, 25, 42};
    const float frames[] = {0, 10, 10, 10};
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(deltas[i]); PrimaryMotionRig rig(Context);
        rig.Tick(8); ExpectPose(rig, 108);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Source, CK_TRANSITION_LOOPIFEQUAL));
        ASSERT_EQ(&rig.Source, rig.Character.GetNextActiveAnimation());
        rig.Tick(deltas[i]);
        EXPECT_EQ(&rig.Source, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frames[i], rig.Source.GetFrame(), .0001f);
        // A single queued restart consumes source to its end, then plays
        // one new 5..15 clip. It does not queue further copies of the clip.
        ExpectPose(rig, 110 + frames[i]);
    }
}

TEST_F(CharacterPrimaryMotionTest, ConsumedSelfQueueDoesNotInventPersistentLoopingOnLaterUpdates) {
    PrimaryMotionRig rig(Context); rig.Tick(8);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Source, CK_TRANSITION_LOOPIFEQUAL));
    rig.Tick(5); ExpectPose(rig, 113); EXPECT_FLOAT_EQ(3, rig.Source.GetFrame());
    EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    rig.Tick(1); ExpectPose(rig, 114); EXPECT_FLOAT_EQ(4, rig.Source.GetFrame());
    rig.Tick(7); ExpectPose(rig, 120); EXPECT_FLOAT_EQ(10, rig.Source.GetFrame());
    EXPECT_EQ(&rig.Source, rig.Character.GetActiveAnimation());
    rig.Tick(1); ExpectPose(rig, 120); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
}

TEST_F(CharacterPrimaryMotionTest, SelfQueuedRestartMovesInTheCharactersWorldOrientation) {
    PrimaryMotionRig rig(Context, 100, true);
    ExpectQuarterTurn(rig.Character.GetWorldMatrix()); ExpectPreservedRootBasis(rig);
    rig.Tick(8); ExpectPose(rig, 100, 12); ExpectPreservedRootBasis(rig);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Source, CK_TRANSITION_LOOPIFEQUAL));
    rig.Tick(25); EXPECT_FLOAT_EQ(10, rig.Source.GetFrame()); ExpectPose(rig, 100, 0);
    ExpectQuarterTurn(rig.Character.GetWorldMatrix()); ExpectPreservedRootBasis(rig);
}

TEST_F(CharacterPrimaryMotionTest, SingleSelfHandoffMatchesBoundarySplitWithoutAnotherQueueRequest) {
    const float deltas[] = {2, 12, 25, 42};
    const float frames[] = {0, 10, 10, 10};
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(deltas[i]); PrimaryMotionRig single(Context), split(Context);
        single.Tick(8); split.Tick(8);
        ASSERT_EQ(CK_OK, single.Character.SetNextActiveAnimation(&single.Source, CK_TRANSITION_LOOPIFEQUAL));
        ASSERT_EQ(CK_OK, split.Character.SetNextActiveAnimation(&split.Source, CK_TRANSITION_LOOPIFEQUAL));
        single.Tick(deltas[i]); split.Tick(2); split.Tick(deltas[i] - 2);
        EXPECT_EQ(&single.Source, single.Character.GetActiveAnimation());
        EXPECT_EQ(&split.Source, split.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, single.Character.GetNextActiveAnimation());
        EXPECT_EQ(nullptr, split.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frames[i], single.Source.GetFrame(), .0001f);
        EXPECT_NEAR(frames[i], split.Source.GetFrame(), .0001f);
        ExpectPose(single, 110 + frames[i]); ExpectPose(split, 110 + frames[i]);
    }
}

TEST_F(CharacterPrimaryMotionTest, DifferentDestinationConsumesSourceOnceAndReceivesAllRemainingFrames) {
    const float deltas[] = {5, 12, 25, 45};
    for (float delta : deltas) {
        SCOPED_TRACE(delta); PrimaryMotionRig rig(Context); rig.Tick(8);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART));
        rig.Tick(delta);
        EXPECT_EQ(&rig.Destination, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        const float remaining = delta - 2;
        EXPECT_NEAR(remaining, rig.Destination.GetFrame(), .0001f);
        // Finish source 5..15 exactly once, then target 100..300 moves at
        // two units/frame. Raw target first=100 must not become displacement.
        ExpectPose(rig, 110 + remaining * 2);
    }
}

TEST_F(CharacterPrimaryMotionTest, DifferentDestinationOvershootClampsAtItsEndWithoutLoopingOrRepeatingSource) {
    PrimaryMotionRig rig(Context, 30); rig.Tick(8);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART));
    rig.Tick(100);
    EXPECT_EQ(&rig.Destination, rig.Character.GetActiveAnimation());
    EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(30, rig.Destination.GetFrame()); ExpectPose(rig, 170);
    rig.Tick(1); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); ExpectPose(rig, 170);
}

TEST_F(CharacterPrimaryMotionTest, DifferentDestinationCarriesCompleteRemainderInWorldOrientation) {
    PrimaryMotionRig rig(Context, 100, true);
    ExpectQuarterTurn(rig.Character.GetWorldMatrix()); ExpectPreservedRootBasis(rig);
    rig.Tick(8); ExpectPreservedRootBasis(rig);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART));
    rig.Tick(12); EXPECT_FLOAT_EQ(10, rig.Destination.GetFrame()); ExpectPose(rig, 100, -10);
    ExpectQuarterTurn(rig.Character.GetWorldMatrix()); ExpectPreservedRootBasis(rig);
}

TEST_F(CharacterPrimaryMotionTest, LinkedHandoffConvertsRemainingFramesAtTheDestinationRate) {
    const float deltas[] = {5, 12};
    const float frames[] = {.3f, 1};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(deltas[i]); PrimaryMotionRig single(Context), split(Context);
        single.Destination.LinkToFrameRate(TRUE, 100); split.Destination.LinkToFrameRate(TRUE, 100);
        single.Tick(8); split.Tick(8);
        ASSERT_EQ(CK_OK, single.Character.SetNextActiveAnimation(&single.Destination, CK_TRANSITION_TOSTART));
        ASSERT_EQ(CK_OK, split.Character.SetNextActiveAnimation(&split.Destination, CK_TRANSITION_TOSTART));
        single.Tick(deltas[i]); split.Tick(2); split.Tick(deltas[i] - 2);
        EXPECT_EQ(&single.Destination, single.Character.GetActiveAnimation());
        EXPECT_EQ(&split.Destination, split.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, single.Character.GetNextActiveAnimation());
        EXPECT_EQ(nullptr, split.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frames[i], single.Destination.GetFrame(), .0001f);
        EXPECT_NEAR(frames[i], split.Destination.GetFrame(), .0001f);
        ExpectPose(single, 110 + frames[i] * 2); ExpectPose(split, 110 + frames[i] * 2);
    }
}

TEST_F(CharacterPrimaryMotionTest, NoQueuedDestinationLargeDeltaConsumesOneshotSourceEndOnlyOnce) {
    for (int interruptible = 0; interruptible < 2; ++interruptible) {
        SCOPED_TRACE(interruptible); PrimaryMotionRig rig(Context); rig.Tick(8);
        rig.Source.SetCanBeInterrupt(interruptible); rig.Tick(25);
        EXPECT_EQ(interruptible ? nullptr : &rig.Source, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); ExpectPose(rig, 110);
        if (!interruptible) {
            EXPECT_FLOAT_EQ(10, rig.Source.GetFrame());
            rig.Tick(1); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); ExpectPose(rig, 110);
        }
    }
}
} // namespace
