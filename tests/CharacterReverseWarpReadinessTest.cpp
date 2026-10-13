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
CK_CLASSID ReadinessDerivedID = 0;
class ReadinessDerived : public RCKKeyedAnimation {
public:
    explicit ReadinessDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return ReadinessDerivedID; }
};
static CK3dEntity *ReadinessEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class ReverseReadinessRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation ExactChannel, DerivedChannel, DestinationChannel, PendingChannel;
    RCKKeyedAnimation Exact, Destination, Pending;
    ReadinessDerived Derived;
    bool UseDerived;
    explicit ReverseReadinessRig(CKContext *context, bool derived)
        : Character(context), Root(context), ExactChannel(context), DerivedChannel(context),
          DestinationChannel(context), PendingChannel(context), Exact(context), Destination(context),
          Pending(context), Derived(context), UseDerived(derived) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, ReadinessEntity(Character));
        Curve(ExactChannel, Exact, 5, 15); Curve(DerivedChannel, Derived, 5, 15);
        Curve(DestinationChannel, Destination, 100, 120); Curve(PendingChannel, Pending, 200, 230);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Exact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Derived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Destination)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Pending));
        Source().SetFrame(0); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source()));
    }
    ~ReverseReadinessRig() { Root.SetParent(nullptr, TRUE); }
    RCKKeyedAnimation &Source() { return UseDerived ? static_cast<RCKKeyedAnimation &>(Derived) : Exact; }
    RCKObjectAnimation &SourceChannel() { return UseDerived ? DerivedChannel : ExactChannel; }
    void Tick(float delta) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(delta);
    }
private:
    void Curve(RCKObjectAnimation &channel, RCKKeyedAnimation &animation, float first, float last) {
        channel.Set3dEntity(ReadinessEntity(Root)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel)); animation.SetLength(10);
        animation.LinkToFrameRate(TRUE, 1024); animation.SetCanBeInterrupt(FALSE);
    }
};
class CharacterReverseWarpReadinessTest : public ::testing::Test {
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
        ReadinessDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(ReadinessDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(ReverseReadinessRig &rig, float x) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
                                     &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix()};
        for (int which = 0; which < 4; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0 && which != 3) expected = x;
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0003f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(ReadinessEntity(rig.Character), rig.Root.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void SourceState(ReverseReadinessRig &rig, float frame, float world, CKAnimation *pending = nullptr) {
        EXPECT_EQ(&rig.Source(), rig.Character.GetActiveAnimation());
        EXPECT_EQ(pending, rig.Character.GetNextActiveAnimation()); EXPECT_NEAR(frame, rig.Source().GetFrame(), .0002f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Source().GetCharacter());
        EXPECT_EQ(&rig.SourceChannel(), rig.Source().GetRootAnimationInternal());
        const VxVector &center = rig.Source().GetRootVectorInternal();
        EXPECT_NEAR(-(5 + frame), center.x, .0002f); EXPECT_FLOAT_EQ(0, center.y); EXPECT_FLOAT_EQ(0, center.z);
        Pose(rig, world);
    }
    static void WarmLengthAndReverse(ReverseReadinessRig &rig, float initialWorld = 100) {
        ASSERT_EQ(10.0f, rig.Source().GetNextFrame(9.765625f)); rig.Tick(9.765625f);
        SourceState(rig, 10, initialWorld + 10); rig.Source().LinkToFrameRate(TRUE, -1024);
    }
    static void Request(ReverseReadinessRig &rig) {
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_WARPSTART, 2));
    }
    static void ExistingCurves(ReverseReadinessRig &rig, float first, float last) {
        RCKKeyedAnimation *warp = static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper());
        ASSERT_NE(nullptr, warp); EXPECT_FLOAT_EQ(2, warp->GetLength()); ASSERT_EQ(1, warp->GetAnimationCount());
        CKObjectAnimation *channel = warp->GetAnimation(ReadinessEntity(rig.Root)); ASSERT_NE(nullptr, channel);
        VxVector a, b; ASSERT_TRUE(channel->EvaluatePosition(0, a)); ASSERT_TRUE(channel->EvaluatePosition(2, b));
        EXPECT_NEAR(first, a.x, .0002f); EXPECT_NEAR(last, b.x, .0002f);
        EXPECT_FLOAT_EQ(0, a.y); EXPECT_FLOAT_EQ(0, a.z); EXPECT_FLOAT_EQ(0, b.y); EXPECT_FLOAT_EQ(0, b.z);
    }
    static void Parameters(ReverseReadinessRig &rig, float sourceFrame, CKAnimation *destination) {
        CKDWORD mode = 0; CKAnimation *in = nullptr, *out = nullptr; float from = -1, to = -1;
        rig.Character.GetWarperParameters(&mode, &in, &from, &out, &to);
        EXPECT_EQ(CK_TRANSITION_WARPSTART, mode); EXPECT_EQ(&rig.Source(), in); EXPECT_EQ(destination, out);
        EXPECT_NEAR(sourceFrame, from, .0002f); EXPECT_FLOAT_EQ(0, to);
    }
    static void PrimeRealWarperAndRestartSource(ReverseReadinessRig &rig) {
        rig.Source().SetCanBeInterrupt(TRUE); rig.Tick(1.953125f); SourceState(rig, 2, 102);
        Request(rig); ExistingCurves(rig, 7, 100); Parameters(rig, 2, &rig.Destination);
        ASSERT_EQ(rig.Character.GetWarper(), rig.Character.GetActiveAnimation());
        rig.Tick(1.953125f); EXPECT_EQ(&rig.Destination, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); Pose(rig, 195);
        // Public reset of a previously played source: center its real initial
        // curve before selecting it, without changing accumulated world pose.
        rig.Source().CenterAnimation(0); rig.Source().SetFrame(0);
        ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(&rig.Source()));
        rig.Source().SetCanBeInterrupt(FALSE); SourceState(rig, 0, 195);
        ExistingCurves(rig, 7, 100);
    }
};

TEST_F(CharacterReverseWarpReadinessTest, ReverseLengthAndZeroTickDoNotPermitUninterruptibleWarp) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0); WarmLengthAndReverse(rig);
        ASSERT_EQ(10.0f, rig.Source().GetNextFrame(0)); rig.Tick(0); SourceState(rig, 10, 110);
        Request(rig); SourceState(rig, 10, 110);
        EXPECT_EQ(0, static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper())->GetAnimationCount());
        ASSERT_EQ(8.0f, rig.Source().GetNextFrame(1.953125f)); rig.Tick(1.953125f); SourceState(rig, 8, 108);
    }
}
TEST_F(CharacterReverseWarpReadinessTest, ReverseInteriorRequestPreservesDifferentPendingTarget) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0); WarmLengthAndReverse(rig);
        rig.Tick(1.953125f); SourceState(rig, 8, 108);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Pending, CK_TRANSITION_TOSTART));
        Request(rig); SourceState(rig, 8, 108, &rig.Pending);
        rig.Tick(1.953125f); SourceState(rig, 6, 106, &rig.Pending);
        EXPECT_FLOAT_EQ(0, rig.Pending.GetFrame()); EXPECT_FLOAT_EQ(0, rig.Destination.GetFrame());
    }
}
TEST_F(CharacterReverseWarpReadinessTest, ReversePositiveQuarterFrameDoesNotInventMirroredEndTolerance) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0); WarmLengthAndReverse(rig);
        ASSERT_EQ(.25f, rig.Source().GetNextFrame(9.521484375f)); rig.Tick(9.521484375f);
        SourceState(rig, .25f, 100.25f); Request(rig); SourceState(rig, .25f, 100.25f);
        rig.Tick(0); Request(rig); SourceState(rig, .25f, 100.25f);
        EXPECT_EQ(0, static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper())->GetAnimationCount());
    }
}
TEST_F(CharacterReverseWarpReadinessTest, ReverseExactZeroUsesExistingImmediateCenteredEntryWithoutWarp) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0); WarmLengthAndReverse(rig);
        ASSERT_EQ(0.0f, rig.Source().GetNextFrame(9.765625f)); rig.Tick(9.765625f); SourceState(rig, 0, 100);
        Request(rig); EXPECT_EQ(&rig.Destination, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); EXPECT_FLOAT_EQ(0, rig.Destination.GetFrame());
        EXPECT_NEAR(-100, rig.Destination.GetRootVectorInternal().x, .0002f); Pose(rig, 100);
        EXPECT_EQ(0, static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper())->GetAnimationCount());
        rig.Tick(1.953125f); EXPECT_EQ(&rig.Destination, rig.Character.GetActiveAnimation());
        EXPECT_FLOAT_EQ(2, rig.Destination.GetFrame()); Pose(rig, 104);
    }
}
TEST_F(CharacterReverseWarpReadinessTest, ReverseLengthRejectedRequestPreservesPriorRealWarpCurvesAndSelection) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0); PrimeRealWarperAndRestartSource(rig);
        WarmLengthAndReverse(rig, 195); Request(rig); SourceState(rig, 10, 205);
        ExistingCurves(rig, 7, 100); Parameters(rig, 2, nullptr);
        rig.Tick(1.953125f); SourceState(rig, 8, 203); ExistingCurves(rig, 7, 100);
    }
}
TEST_F(CharacterReverseWarpReadinessTest, ReverseInteriorPreservesPriorWarpCurvesAndDifferentQueueMetadata) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0); PrimeRealWarperAndRestartSource(rig);
        WarmLengthAndReverse(rig, 195); rig.Tick(1.953125f); SourceState(rig, 8, 203);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Pending, CK_TRANSITION_TOSTART));
        Request(rig); SourceState(rig, 8, 203, &rig.Pending);
        ExistingCurves(rig, 7, 100); Parameters(rig, 2, &rig.Pending);
        rig.Tick(1.953125f); SourceState(rig, 6, 201, &rig.Pending); ExistingCurves(rig, 7, 100);
    }
}
TEST_F(CharacterReverseWarpReadinessTest, PositiveLinkedReadinessRetainsLegacyPointThreeFrameTolerance) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0);
        ASSERT_EQ(9.625f, rig.Source().GetNextFrame(9.3994140625f)); rig.Tick(9.3994140625f);
        Request(rig); SourceState(rig, 9.625f, 109.625f);
        ASSERT_EQ(9.75f, rig.Source().GetNextFrame(.1220703125f)); rig.Tick(.1220703125f);
        SourceState(rig, 9.75f, 109.75f); Request(rig);
        EXPECT_EQ(rig.Character.GetWarper(), rig.Character.GetActiveAnimation());
        EXPECT_EQ(&rig.Destination, rig.Character.GetNextActiveAnimation());
        ExistingCurves(rig, 14.75f, 100); Parameters(rig, 9.75f, &rig.Destination); Pose(rig, 109.75f);
    }
}
TEST_F(CharacterReverseWarpReadinessTest, UnlinkedNegativeStoredFPSStillRequiresExactForwardLength) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0);
        ASSERT_EQ(9.75f, rig.Source().GetNextFrame(9.521484375f)); rig.Tick(9.521484375f);
        rig.Source().LinkToFrameRate(FALSE, -1024); Request(rig); SourceState(rig, 9.75f, 109.75f);
        ASSERT_EQ(10.75f, rig.Source().GetNextFrame(0)); rig.Tick(0); SourceState(rig, 10, 110);
        Request(rig); EXPECT_EQ(rig.Character.GetWarper(), rig.Character.GetActiveAnimation());
        EXPECT_EQ(&rig.Destination, rig.Character.GetNextActiveAnimation());
        ExistingCurves(rig, 15, 100); Parameters(rig, 10, &rig.Destination); Pose(rig, 110);
    }
}
TEST_F(CharacterReverseWarpReadinessTest, InterruptibleReverseInteriorBuildsAndPlaysActualTransition) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); ReverseReadinessRig rig(Context, derived != 0); WarmLengthAndReverse(rig);
        rig.Tick(1.953125f); SourceState(rig, 8, 108); rig.Source().SetCanBeInterrupt(TRUE);
        Request(rig); EXPECT_EQ(rig.Character.GetWarper(), rig.Character.GetActiveAnimation());
        EXPECT_EQ(&rig.Destination, rig.Character.GetNextActiveAnimation());
        ExistingCurves(rig, 13, 100); Parameters(rig, 8, &rig.Destination); Pose(rig, 108);
        CKAnimation *warp = rig.Character.GetWarper(); ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Tick(1.953125f);
        EXPECT_EQ(&rig.Destination, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(0, rig.Destination.GetFrame()); Pose(rig, 195);
        rig.Tick(1.953125f); EXPECT_FLOAT_EQ(2, rig.Destination.GetFrame()); Pose(rig, 199);
    }
}
} // namespace
