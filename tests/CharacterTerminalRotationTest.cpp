#include <gtest/gtest.h>
#include <cmath>
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

static CK3dEntity *TurnEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
static VxQuaternion PositiveYaw(float degrees) {
    const float half = degrees * 3.14159265358979323846f / 360;
    return VxQuaternion(0, std::sin(half), 0, std::cos(half));
}

class TerminalRotationRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation SourceChannel, DestinationChannel;
    RCKKeyedAnimation Source, Destination;
    explicit TerminalRotationRig(CKContext *context, bool pitch = false, float scale = 1)
        : Character(context), Root(context), SourceChannel(context), DestinationChannel(context),
          Source(context), Destination(context) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100;
        if (pitch) {
            world[1][1] = .8660254037844386f; world[1][2] = -.5f;
            world[2][1] = .5f; world[2][2] = .8660254037844386f;
        }
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) world[row][column] *= scale;
        Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, TurnEntity(Character));
        Curve(SourceChannel, Source, 10, 5, 15, 30, 120);
        Curve(DestinationChannel, Destination, 8, 100, 116, 45, 135);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Source)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Destination));
        Source.SetFrame(0); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source));
    }
    ~TerminalRotationRig() { Root.SetParent(nullptr, TRUE); }
    void Tick(float milliseconds) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(milliseconds);
    }
private:
    void Curve(RCKObjectAnimation &channel, RCKKeyedAnimation &animation, float length,
               float firstPosition, float lastPosition, float firstYaw, float lastYaw) {
        channel.Set3dEntity(TurnEntity(Root)); VxVector a(firstPosition, 0, 0), b(lastPosition, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b);
        VxQuaternion qa = PositiveYaw(firstYaw), qb = PositiveYaw(lastYaw);
        channel.AddRotationKey(0, &qa); channel.AddRotationKey(length, &qb);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel)); animation.SetLength(length);
        animation.LinkToFrameRate(TRUE, 1024); animation.SetCanBeInterrupt(FALSE); animation.SetCharacterOrientation(TRUE);
    }
};

class CharacterTerminalRotationTest : public ::testing::Test {
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
    static void Matrix(const VxMatrix &actual, const float basis[3][3], float x, float y, float z, float scale = 1) {
        const float translation[] = {x, y, z};
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column) {
                float expected = 0;
                if (row < 3 && column < 3) expected = basis[row][column] * scale;
                else if (row == 3 && column < 3) expected = translation[column];
                else if (row == 3 && column == 3) expected = 1;
                EXPECT_NEAR(expected, actual[row][column], .0003f) << row << ',' << column;
            }
    }
    static void YawMatrix(const VxMatrix &actual, float degrees, float x, float y, float z, float scale = 1) {
        // Closed row-storage basis for a positive-Y quaternion; no engine
        // quaternion conversion, multiplication, decomposition or inverse.
        const double angle = degrees * 3.14159265358979323846 / 180;
        const float c = static_cast<float>(std::cos(angle)), s = static_cast<float>(std::sin(angle));
        const float basis[3][3] = {{c, 0, s}, {0, 1, 0}, {-s, 0, c}};
        Matrix(actual, basis, x, y, z, scale);
    }
    static void Pose(TerminalRotationRig &rig, float characterYaw, float rootWorldYaw, float rootLocalYaw,
                     float x, float z = 0, float scale = 1) {
        YawMatrix(rig.Character.GetWorldMatrix(), characterYaw, x, 0, z, scale);
        YawMatrix(rig.Character.GetLocalMatrix(), characterYaw, x, 0, z, scale);
        YawMatrix(rig.Root.GetWorldMatrix(), rootWorldYaw, x, 0, z);
        YawMatrix(rig.Root.GetLocalMatrix(), rootLocalYaw, 0, 0, 0, 1 / scale);
        EXPECT_EQ(TurnEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void WarmEight(TerminalRotationRig &rig) {
        ASSERT_EQ(8.0f, rig.Source.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
        EXPECT_EQ(&rig.Source, rig.Character.GetActiveAnimation()); EXPECT_FLOAT_EQ(8, rig.Source.GetFrame());
    }
    static void Active(TerminalRotationRig &rig, RCKKeyedAnimation &animation, float frame, float center) {
        EXPECT_EQ(&animation, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frame, animation.GetFrame(), .0003f); EXPECT_NEAR(center, animation.GetRootVectorInternal().x, .0003f);
        EXPECT_FLOAT_EQ(0, animation.GetRootVectorInternal().y); EXPECT_FLOAT_EQ(0, animation.GetRootVectorInternal().z);
    }
};

TEST_F(CharacterTerminalRotationTest, OrdinaryAndZeroElapsedTicksKeepCharacterBasisWhileRootUsesFullCurve) {
    TerminalRotationRig rig(Context); Pose(rig, 0, 30, 30, 100);
    ASSERT_EQ(0.0f, rig.Source.GetNextFrame(0)); rig.Tick(0); Active(rig, rig.Source, 0, -5); Pose(rig, 0, 30, 30, 100);
    WarmEight(rig); Pose(rig, 0, 102, 102, 108);
}

TEST_F(CharacterTerminalRotationTest, InterruptibleNoQueueCompletesRealRootEndBeforeTakingRelativeYaw) {
    TerminalRotationRig rig(Context); WarmEight(rig); rig.Source.SetCanBeInterrupt(TRUE);
    ASSERT_EQ(12.0f, rig.Source.GetNextFrame(3.90625f)); rig.Tick(3.90625f);
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(8, rig.Source.GetFrame()); Pose(rig, 90, 120, 30, 110);
    rig.Tick(0); Pose(rig, 90, 120, 30, 110);
}

TEST_F(CharacterTerminalRotationTest, NonInterruptibleHeldEndRealignsBeforeClearingOnFollowingUpdate) {
    TerminalRotationRig rig(Context); WarmEight(rig);
    ASSERT_EQ(10.0f, rig.Source.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
    Active(rig, rig.Source, 10, -15); Pose(rig, 0, 120, 120, 110);
    rig.Tick(.9765625f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 90, 120, 30, 110);
}

TEST_F(CharacterTerminalRotationTest, AlignmentFlagDisabledStillSamplesFullTerminalRootRotation) {
    TerminalRotationRig rig(Context); WarmEight(rig); rig.Source.SetCanBeInterrupt(TRUE);
    rig.Source.SetCharacterOrientation(FALSE); rig.Source.SetFlags(rig.Source.GetFlags() | CKANIMATION_ALLOWTURN);
    rig.Tick(3.90625f); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 0, 120, 120, 110);
}

TEST_F(CharacterTerminalRotationTest, DifferentClipLargeAndBoundarySplitPreserveNonzeroFirstAndEntryRotations) {
    TerminalRotationRig single(Context), split(Context); WarmEight(single); WarmEight(split);
    ASSERT_EQ(CK_OK, single.Character.SetNextActiveAnimation(&single.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(CK_OK, split.Character.SetNextActiveAnimation(&split.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(12.0f, single.Source.GetNextFrame(3.90625f)); single.Tick(3.90625f);
    split.Tick(1.953125f); Active(split, split.Destination, 0, -100); Pose(split, 90, 135, 45, 110);
    split.Tick(1.953125f);
    Active(single, single.Destination, 2, -104); Active(split, split.Destination, 2, -104);
    Pose(single, 90, 157.5f, 67.5f, 110, 4); Pose(split, 90, 157.5f, 67.5f, 110, 4);
}

TEST_F(CharacterTerminalRotationTest, SelfRestartTransfersSourceEndYawOnceBeforeNewClipEntry) {
    TerminalRotationRig single(Context), split(Context); WarmEight(single); WarmEight(split);
    ASSERT_EQ(CK_OK, single.Character.SetNextActiveAnimation(&single.Source, CK_TRANSITION_LOOPIFEQUAL));
    ASSERT_EQ(CK_OK, split.Character.SetNextActiveAnimation(&split.Source, CK_TRANSITION_LOOPIFEQUAL));
    single.Tick(3.90625f); split.Tick(1.953125f); split.Tick(1.953125f);
    Active(single, single.Source, 2, -7); Active(split, split.Source, 2, -7);
    Pose(single, 90, 138, 48, 110, 2); Pose(split, 90, 138, 48, 110, 2);
}

TEST_F(CharacterTerminalRotationTest, ReverseNoQueueUsesFirstRootPoseAndZeroRelativeYawSingleOrSplit) {
    TerminalRotationRig single(Context), split(Context); WarmEight(single); WarmEight(split);
    single.Source.LinkToFrameRate(TRUE, -1024); split.Source.LinkToFrameRate(TRUE, -1024);
    ASSERT_EQ(-10.0f, single.Source.GetNextFrame(17.578125f)); single.Tick(17.578125f);
    ASSERT_EQ(0.0f, split.Source.GetNextFrame(7.8125f)); split.Tick(7.8125f);
    Active(split, split.Source, 0, -5); Pose(split, 0, 30, 30, 100); split.Tick(9.765625f);
    EXPECT_EQ(nullptr, single.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, split.Character.GetActiveAnimation());
    Pose(single, 0, 30, 30, 100); Pose(split, 0, 30, 30, 100);
}

TEST_F(CharacterTerminalRotationTest, ReverseToDifferentPositiveClipKeepsLiteralEntryAndSourceTerminalYaw) {
    TerminalRotationRig single(Context), split(Context); WarmEight(single); WarmEight(split);
    single.Source.LinkToFrameRate(TRUE, -1024); split.Source.LinkToFrameRate(TRUE, -1024);
    ASSERT_EQ(CK_OK, single.Character.SetNextActiveAnimation(&single.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(CK_OK, split.Character.SetNextActiveAnimation(&split.Destination, CK_TRANSITION_TOSTART));
    ASSERT_EQ(-1.0f, single.Source.GetNextFrame(8.7890625f)); single.Tick(8.7890625f);
    split.Tick(7.8125f); split.Tick(.9765625f);
    Active(single, single.Destination, 1, -102); Active(split, split.Destination, 1, -102);
    Pose(single, 0, 56.25f, 56.25f, 102); Pose(split, 0, 56.25f, 56.25f, 102);
}

TEST_F(CharacterTerminalRotationTest, ReverseSelfEntryDoesNotInventCompensatingCharacterYaw) {
    TerminalRotationRig rig(Context); WarmEight(rig); rig.Source.LinkToFrameRate(TRUE, -1024);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Source, CK_TRANSITION_LOOPIFEQUAL));
    ASSERT_EQ(-10.0f, rig.Source.GetNextFrame(17.578125f)); rig.Tick(17.578125f);
    Active(rig, rig.Source, 0, -5); Pose(rig, 0, 30, 30, 90);
}

TEST_F(CharacterTerminalRotationTest, NoncommutingInheritedPitchUsesNativeAlignOrderAndPreservesFullRootWorldPose) {
    TerminalRotationRig rig(Context, true); WarmEight(rig); rig.Source.SetCanBeInterrupt(TRUE); rig.Tick(3.90625f);
    const float character[3][3] = {{0, 0, 1}, {.5f, .8660254038f, 0}, {-.8660254038f, .5f, 0}};
    const float rootWorld[3][3] = {{-.5f, .4330127019f, .75f}, {0, .8660254038f, -.5f}, {-.8660254038f, -.25f, -.4330127019f}};
    const float rootLocal[3][3] = {{.75f, .125f, .6495190528f}, {-.5f, .75f, .4330127019f}, {-.4330127019f, -.6495190528f, .625f}};
    // Explicit closed storage matrices: Character=X30*Y90,
    // RootWorld=Y120*X30, RootLocal=RootWorld*inverse(Character).
    Matrix(rig.Character.GetWorldMatrix(), character, 110, 0, 0);
    Matrix(rig.Character.GetLocalMatrix(), character, 110, 0, 0);
    Matrix(rig.Root.GetWorldMatrix(), rootWorld, 110, 0, 0); Matrix(rig.Root.GetLocalMatrix(), rootLocal, 0, 0, 0);
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
}

TEST_F(CharacterTerminalRotationTest, UniformParentScalePreservesRootScaleDuringTerminalRealignment) {
    TerminalRotationRig rig(Context, false, 2); WarmEight(rig); rig.Source.SetCanBeInterrupt(TRUE); rig.Tick(3.90625f);
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 90, 120, 30, 120, 0, 2);
}

TEST_F(CharacterTerminalRotationTest, FullPitchEndpointRemainsInRootWhileCharacterTakesOnlyRelativeYawTwist) {
    TerminalRotationRig rig(Context);
    VxQuaternion pitch(.5f, 0, 0, .8660254037844386f);
    rig.SourceChannel.AddRotationKey(10, &pitch);
    VxQuaternion endpoint; ASSERT_TRUE(rig.SourceChannel.EvaluateRotation(10, endpoint));
    EXPECT_NEAR(.5f, endpoint.x, .00001f); EXPECT_FLOAT_EQ(0, endpoint.y); EXPECT_FLOAT_EQ(0, endpoint.z);
    EXPECT_NEAR(.8660254037844386f, endpoint.w, .00001f);
    WarmEight(rig); rig.Source.SetCanBeInterrupt(TRUE);
    ASSERT_EQ(12.0f, rig.Source.GetNextFrame(3.90625f)); rig.Tick(3.90625f);
    // qEnd=X60, qFirst=Y30: the relative product's y/w=-tan15,
    // so its projected Y twist is -30, while the full root remains X60.
    YawMatrix(rig.Character.GetWorldMatrix(), -30, 110, 0, 0);
    YawMatrix(rig.Character.GetLocalMatrix(), -30, 110, 0, 0);
    const float rootWorld[3][3] = {{1, 0, 0}, {0, .5f, -.8660254038f}, {0, .8660254038f, .5f}};
    const float rootLocal[3][3] = {{.8660254038f, 0, .5f}, {.4330127019f, .5f, -.75f}, {-.25f, .8660254038f, .4330127019f}};
    Matrix(rig.Root.GetWorldMatrix(), rootWorld, 110, 0, 0); Matrix(rig.Root.GetLocalMatrix(), rootLocal, 0, 0, 0);
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
}

} // namespace
