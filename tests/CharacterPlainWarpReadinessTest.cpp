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
CK_CLASSID PlainReadinessDerivedID = 0;
class PlainReadinessDerived : public RCKKeyedAnimation {
public:
    explicit PlainReadinessDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return PlainReadinessDerivedID; }
};
static CK3dEntity *PlainReadinessEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class PlainReadinessRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation ExactChannel, DerivedChannel, KeyedTargetChannel, PendingChannel;
    RCKKeyedAnimation Exact, KeyedTarget, Pending;
    PlainReadinessDerived Derived;
    RCKAnimation PlainSource, PlainTarget;
    int Mode;
    explicit PlainReadinessRig(CKContext *context, int mode)
        : Character(context), Root(context), ExactChannel(context), DerivedChannel(context),
          KeyedTargetChannel(context), PendingChannel(context), Exact(context), KeyedTarget(context),
          Pending(context), Derived(context), PlainSource(context), PlainTarget(context), Mode(mode) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, PlainReadinessEntity(Character));
        Curve(ExactChannel, Exact, 5, 15); Curve(DerivedChannel, Derived, 5, 15);
        Curve(KeyedTargetChannel, KeyedTarget, 100, 120); Curve(PendingChannel, Pending, 200, 230);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Exact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Derived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&KeyedTarget)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Pending));
        PlainSource.SetLength(10); PlainSource.LinkToFrameRate(TRUE, 1024); PlainSource.SetCanBeInterrupt(FALSE);
        PlainTarget.SetLength(10); PlainTarget.LinkToFrameRate(TRUE, 1024); PlainTarget.SetCanBeInterrupt(FALSE);
        PlainTarget.SetFrame(3); EXPECT_EQ(CK_OK, Character.AddAnimation(&PlainSource));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&PlainTarget));
        if (Mode == 2) RootOffset(7);
        Source().SetFrame(0); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source()));
    }
    ~PlainReadinessRig() { Root.SetParent(nullptr, TRUE); }
    RCKAnimation &Source() {
        if (Mode == 0) return Exact;
        if (Mode == 1) return Derived;
        return PlainSource;
    }
    RCKObjectAnimation &SourceChannel() { return Mode == 1 ? DerivedChannel : ExactChannel; }
    void RootOffset(float x) { VxVector pos(x, 0, 0); Root.SetPosition(&pos, PlainReadinessEntity(Character)); }
    void Tick(float delta) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(delta);
    }
private:
    void Curve(RCKObjectAnimation &channel, RCKKeyedAnimation &animation, float first, float last) {
        channel.Set3dEntity(PlainReadinessEntity(Root)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel)); animation.SetLength(10);
        animation.LinkToFrameRate(TRUE, 1024); animation.SetCanBeInterrupt(FALSE);
    }
};
class CharacterPlainWarpReadinessTest : public ::testing::Test {
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
        PlainReadinessDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(PlainReadinessDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(PlainReadinessRig &rig, float x, float local = 0) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
                                     &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix()};
        const float translations[] = {x, x, x + local, local};
        for (int which = 0; which < 4; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0) expected = translations[which];
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0003f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(PlainReadinessEntity(rig.Character), rig.Root.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void SourceCache(PlainReadinessRig &rig, float frame) {
        if (rig.Mode == 2) return;
        RCKKeyedAnimation &keyed = static_cast<RCKKeyedAnimation &>(rig.Source());
        EXPECT_EQ(&rig.SourceChannel(), keyed.GetRootAnimationInternal());
        const VxVector &center = keyed.GetRootVectorInternal();
        EXPECT_NEAR(-(5 + frame), center.x, .0002f); EXPECT_FLOAT_EQ(0, center.y); EXPECT_FLOAT_EQ(0, center.z);
    }
    static void SourceState(PlainReadinessRig &rig, float frame, CKAnimation *pending = nullptr,
                            float initialWorld = 100) {
        EXPECT_EQ(&rig.Source(), rig.Character.GetActiveAnimation());
        EXPECT_EQ(pending, rig.Character.GetNextActiveAnimation()); EXPECT_NEAR(frame, rig.Source().GetFrame(), .0002f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Source().GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.PlainTarget.GetCharacter());
        EXPECT_FLOAT_EQ(3, rig.PlainTarget.GetFrame()); SourceCache(rig, frame);
        Pose(rig, rig.Mode == 2 ? initialWorld : initialWorld + frame, rig.Mode == 2 ? 7 : 0);
    }
    static void Warm(PlainReadinessRig &rig, float frame, float milliseconds, float initialWorld = 100) {
        ASSERT_EQ(frame, rig.Source().GetNextFrame(milliseconds)); rig.Tick(milliseconds);
        SourceState(rig, frame, nullptr, initialWorld);
    }
    static void Request(PlainReadinessRig &rig) {
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.PlainTarget, CK_TRANSITION_WARPSTART, 2));
    }
    static void PlainState(PlainReadinessRig &rig, float frame, float world, float local = 0) {
        EXPECT_EQ(&rig.PlainTarget, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); EXPECT_NEAR(frame, rig.PlainTarget.GetFrame(), .0002f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.PlainTarget.GetCharacter()); Pose(rig, world, local);
    }
    static void Curves(PlainReadinessRig &rig) {
        RCKKeyedAnimation *warp = static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper());
        ASSERT_NE(nullptr, warp); EXPECT_FLOAT_EQ(2, warp->GetLength()); ASSERT_EQ(1, warp->GetAnimationCount());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), warp->GetCharacter());
        CKObjectAnimation *channel = warp->GetAnimation(PlainReadinessEntity(rig.Root)); ASSERT_NE(nullptr, channel);
        const float times[] = {0, 1, 2}, expected[] = {7, 53.5f, 100};
        for (int i = 0; i < 3; ++i) {
            VxVector value; ASSERT_TRUE(channel->EvaluatePosition(times[i], value));
            EXPECT_NEAR(expected[i], value.x, .0002f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
        }
    }
    static void WarpParameters(PlainReadinessRig &rig, CKAnimation *destination) {
        CKDWORD mode = 0; CKAnimation *in = nullptr, *out = nullptr; float from = -1, to = -1;
        rig.Character.GetWarperParameters(&mode, &in, &from, &out, &to);
        EXPECT_EQ(CK_TRANSITION_WARPSTART, mode); EXPECT_EQ(&rig.Source(), in); EXPECT_EQ(destination, out);
        EXPECT_FLOAT_EQ(2, from); EXPECT_FLOAT_EQ(0, to);
    }
    static void StartRealWarp(PlainReadinessRig &rig) {
        rig.Source().SetCanBeInterrupt(TRUE); Warm(rig, 2, 1.953125f);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.KeyedTarget, CK_TRANSITION_WARPSTART, 2));
        EXPECT_EQ(rig.Character.GetWarper(), rig.Character.GetActiveAnimation());
        EXPECT_EQ(&rig.KeyedTarget, rig.Character.GetNextActiveAnimation());
        Curves(rig); WarpParameters(rig, &rig.KeyedTarget); Pose(rig, 102);
        EXPECT_NEAR(-7, static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper())->GetRootVectorInternal().x, .0002f);
    }
};

TEST_F(CharacterPlainWarpReadinessTest, ForwardPlayingNoninterruptibleSourceDoesNotSelectPlainTarget) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 8, 7.8125f);
        Request(rig); SourceState(rig, 8);
        rig.Tick(1.953125f); SourceState(rig, 10);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, RejectedPlainWarpPreservesDifferentPendingTargetAndItsCursor) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 8, 7.8125f);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Pending, CK_TRANSITION_TOSTART));
        Request(rig); SourceState(rig, 8, &rig.Pending); EXPECT_FLOAT_EQ(0, rig.Pending.GetFrame());
        rig.Tick(.9765625f); SourceState(rig, 9, &rig.Pending); EXPECT_FLOAT_EQ(0, rig.Pending.GetFrame());
    }
}
TEST_F(CharacterPlainWarpReadinessTest, ReverseLengthAndZeroDeltaDoNotCountAsCompletionForPlainTarget) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 10, 9.765625f);
        rig.Source().LinkToFrameRate(TRUE, -1024); ASSERT_EQ(10.0f, rig.Source().GetNextFrame(0)); rig.Tick(0);
        Request(rig); SourceState(rig, 10);
        rig.Tick(1.953125f); SourceState(rig, 8);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, ReverseInteriorAndPositiveQuarterFrameRemainNoninterruptible) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 10, 9.765625f);
        rig.Source().LinkToFrameRate(TRUE, -1024); rig.Tick(1.953125f); Request(rig); SourceState(rig, 8);
        ASSERT_NEAR(.25f, rig.Source().GetNextFrame(7.568359375f), .000002f); rig.Tick(7.568359375f);
        ASSERT_GT(rig.Source().GetFrame(), 0.0f);
        Request(rig); SourceState(rig, .25f); rig.Tick(0); Request(rig); SourceState(rig, .25f);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, ReverseExactZeroRetainsEarlierImmediateResetOfPlainTarget) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 10, 9.765625f);
        rig.Source().LinkToFrameRate(TRUE, -1024);
        ASSERT_EQ(0.0f, rig.Source().GetNextFrame(9.765625f)); rig.Tick(9.765625f); SourceState(rig, 0);
        Request(rig); PlainState(rig, 0, 100, mode == 2 ? 7 : 0);
        rig.Tick(1.953125f); PlainState(rig, 2, 100, mode == 2 ? 7 : 0);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, PositiveLinkedLegacyToleranceSelectsExistingPlainFrameWithoutAlignment) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 9.625f, 9.3994140625f);
        Request(rig); SourceState(rig, 9.625f);
        ASSERT_EQ(9.75f, rig.Source().GetNextFrame(.1220703125f)); rig.Tick(.1220703125f); SourceState(rig, 9.75f);
        Request(rig); const float world = mode == 2 ? 100 : 109.75f, local = mode == 2 ? 7 : 0;
        PlainState(rig, 3, world, local); SourceCache(rig, 9.75f);
        rig.Tick(1.953125f); PlainState(rig, 5, world, local);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, UnlinkedNegativeStoredRateRequiresExactForwardEndForPlainSelection) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 9.75f, 9.521484375f);
        rig.Source().LinkToFrameRate(FALSE, -1024); Request(rig); SourceState(rig, 9.75f);
        ASSERT_EQ(10.75f, rig.Source().GetNextFrame(0)); rig.Tick(0); SourceState(rig, 10);
        Request(rig); const float world = mode == 2 ? 100 : 110, local = mode == 2 ? 7 : 0;
        PlainState(rig, 3, world, local); rig.Tick(1.953125f); PlainState(rig, 5, world, local);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, InterruptibleReadySelectionPreservesUnabsorbedRootOffsetAndTargetCursor) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 2, 1.953125f);
        rig.Source().SetCanBeInterrupt(TRUE); const float world = mode == 2 ? 100 : 102;
        const float local = mode == 2 ? 7 : 1; rig.RootOffset(local); Pose(rig, world, local);
        Request(rig); PlainState(rig, 3, world, local); SourceCache(rig, 2);
        EXPECT_FLOAT_EQ(2, rig.Source().GetFrame()); rig.Tick(1.953125f); PlainState(rig, 5, world, local);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, PausedLinkedSourceUsesNonnegativeEndWindowRatherThanPauseAsReadiness) {
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); Warm(rig, 8, 7.8125f);
        rig.Source().LinkToFrameRate(TRUE, 0); Request(rig); SourceState(rig, 8);
        ASSERT_EQ(8.0f, rig.Source().GetNextFrame(1000)); rig.Tick(1000); SourceState(rig, 8);
        rig.Source().LinkToFrameRate(TRUE, 1024); Warm(rig, 9.75f, 1.708984375f);
        rig.Source().LinkToFrameRate(TRUE, 0); Request(rig);
        PlainState(rig, 3, mode == 2 ? 100 : 109.75f, mode == 2 ? 7 : 0);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, RejectedRequestRetainsCompletedPriorWarpCurvesAndNewPendingSelection) {
    for (int mode = 0; mode < 2; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); StartRealWarp(rig);
        rig.Tick(1.953125f); EXPECT_EQ(&rig.KeyedTarget, rig.Character.GetActiveAnimation()); Pose(rig, 195);
        static_cast<RCKKeyedAnimation &>(rig.Source()).CenterAnimation(0); rig.Source().SetFrame(0);
        ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(&rig.Source())); rig.Source().SetCanBeInterrupt(FALSE);
        Warm(rig, 8, 7.8125f, 195);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Pending, CK_TRANSITION_TOSTART));
        Request(rig); SourceState(rig, 8, &rig.Pending, 195); Curves(rig); WarpParameters(rig, &rig.Pending);
        EXPECT_FLOAT_EQ(0, rig.Pending.GetFrame());
    }
}
TEST_F(CharacterPlainWarpReadinessTest, PlayingPrivateWarpCannotBeStolenFromNoninterruptibleQueuedDestination) {
    for (int mode = 0; mode < 2; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); StartRealWarp(rig);
        CKAnimation *warp = rig.Character.GetWarper(); ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Tick(.9765625f);
        EXPECT_EQ(warp, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.KeyedTarget, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(1, warp->GetFrame()); Pose(rig, 148.5f);
        EXPECT_NEAR(-53.5f, static_cast<RCKKeyedAnimation *>(warp)->GetRootVectorInternal().x, .0002f);
        Request(rig); EXPECT_EQ(warp, rig.Character.GetActiveAnimation());
        EXPECT_EQ(&rig.KeyedTarget, rig.Character.GetNextActiveAnimation()); EXPECT_FLOAT_EQ(1, warp->GetFrame());
        EXPECT_FLOAT_EQ(3, rig.PlainTarget.GetFrame()); Curves(rig); WarpParameters(rig, &rig.KeyedTarget); Pose(rig, 148.5f);
        EXPECT_NEAR(-53.5f, static_cast<RCKKeyedAnimation *>(warp)->GetRootVectorInternal().x, .0002f);
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); rig.Tick(.9765625f);
        EXPECT_EQ(&rig.KeyedTarget, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(0, rig.KeyedTarget.GetFrame()); EXPECT_FLOAT_EQ(3, rig.PlainTarget.GetFrame()); Pose(rig, 195);
    }
}
TEST_F(CharacterPlainWarpReadinessTest, PrivateWarpZeroStepKeepsEarlierImmediatePlainResetException) {
    for (int mode = 0; mode < 2; ++mode) {
        SCOPED_TRACE(mode); PlainReadinessRig rig(Context, mode); StartRealWarp(rig);
        EXPECT_FLOAT_EQ(0, rig.Character.GetWarper()->GetFrame()); Request(rig);
        PlainState(rig, 0, 102); Curves(rig); WarpParameters(rig, nullptr);
        rig.Tick(1.953125f); PlainState(rig, 2, 102); Curves(rig);
    }
}
} // namespace
