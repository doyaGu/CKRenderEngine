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
CK_CLASSID ExplicitDistinctDerivedID = 0;
class ExplicitDistinctDerived : public RCKKeyedAnimation {
public:
    explicit ExplicitDistinctDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return ExplicitDistinctDerivedID; }
};
class ExplicitDistinctCharacter : public RCKCharacter {
public:
    explicit ExplicitDistinctCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *ExplicitEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class ExplicitDistinctRig {
public:
    ExplicitDistinctCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    ExplicitDistinctDerived SourceDerived, TargetDerived;
    int Types;
    explicit ExplicitDistinctRig(CKContext *context, int types)
        : Character(context), Root(context), Limb(context), SourceRoot(context), SourceLimb(context),
          TargetRoot(context), TargetLimb(context), DerivedSourceRoot(context), DerivedSourceLimb(context),
          DerivedTargetRoot(context), DerivedTargetLimb(context), SecondaryLimb(context), SourceExact(context),
          TargetExact(context), Secondary(context), SourceDerived(context), TargetDerived(context), Types(types) {
        Character.SetMoveableFlags((Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED) | VX_MOVEABLE_CHARACTERRENDERED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Limb.SetMoveableFlags(Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, ExplicitEntity(Character));
        EXPECT_TRUE(Limb.SetParent(ExplicitEntity(Root), TRUE));
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Limb)));
        Curve(SourceExact, SourceRoot, Root, 10, 0, 10); Curve(SourceExact, SourceLimb, Limb, 10, 10, 20);
        Curve(SourceDerived, DerivedSourceRoot, Root, 10, 0, 10); Curve(SourceDerived, DerivedSourceLimb, Limb, 10, 10, 20);
        Curve(TargetExact, TargetRoot, Root, 20, 50, 90); Curve(TargetExact, TargetLimb, Limb, 20, 200, 260);
        Curve(TargetDerived, DerivedTargetRoot, Root, 20, 50, 90); Curve(TargetDerived, DerivedTargetLimb, Limb, 20, 200, 260);
        Curve(Secondary, SecondaryLimb, Limb, 10, 100, 200);
        SourceExact.SetLength(10); SourceDerived.SetLength(10); TargetExact.SetLength(20); TargetDerived.SetLength(20);
        Secondary.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&SourceExact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&SourceDerived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&TargetExact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&TargetDerived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Secondary));
        Source().LinkToFrameRate(TRUE, 0); Source().SetCanBeInterrupt(FALSE); Target().SetCanBeInterrupt(FALSE);
        Secondary.LinkToFrameRate(TRUE, 1024); Source().SetFrame(0);
        EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source()));
    }
    ~ExplicitDistinctRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(ExplicitEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryExplicitDistinctTest : public ::testing::Test {
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
        ExplicitDistinctDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(ExplicitDistinctDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(ExplicitDistinctRig &rig, float world, float limb) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
            &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix(), &rig.Limb.GetWorldMatrix(), &rig.Limb.GetLocalMatrix()};
        const float translations[] = {world, world, world, 0, world + limb, limb};
        for (int which = 0; which < 6; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0) expected = translations[which];
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0006f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(ExplicitEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(ExplicitEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(ExplicitDistinctRig &rig, CKAnimation *active, CKAnimation *queue,
                      float sourceFrame, float targetFrame, float sourceCenter, float targetCenter) {
        EXPECT_EQ(active, rig.Character.GetActiveAnimation()); EXPECT_EQ(queue, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(sourceFrame, rig.Source().GetFrame(), .0003f); EXPECT_NEAR(targetFrame, rig.Target().GetFrame(), .0003f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Source().GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Target().GetCharacter());
        EXPECT_EQ(&rig.SourceChannel(), rig.Source().GetRootAnimationInternal());
        EXPECT_EQ(&rig.TargetChannel(), rig.Target().GetRootAnimationInternal());
        EXPECT_NEAR(sourceCenter, rig.Source().GetRootVectorInternal().x, .0003f);
        EXPECT_NEAR(targetCenter, rig.Target().GetRootVectorInternal().x, .0003f);
        EXPECT_FLOAT_EQ(0, rig.Source().GetRootVectorInternal().y); EXPECT_FLOAT_EQ(0, rig.Source().GetRootVectorInternal().z);
        EXPECT_FLOAT_EQ(0, rig.Target().GetRootVectorInternal().y); EXPECT_FLOAT_EQ(0, rig.Target().GetRootVectorInternal().z);
        CKAnimation *destination = nullptr; float entry = -1;
        rig.Character.GetWarperParameters(nullptr, nullptr, nullptr, &destination, &entry);
        EXPECT_EQ(queue, destination); EXPECT_FLOAT_EQ(0, entry);
    }
    static void Prepare(ExplicitDistinctRig &rig, float warm, CKBOOL sourceLinked, float sourceFPS,
                        CKBOOL targetLinked, float targetFPS) {
        rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_NEAR(warm, rig.Source().GetNextFrame(warm * .9765625f), .00001f); rig.Character.Tick(warm * .9765625f);
        State(rig, &rig.Source(), nullptr, warm, 0, -warm, -50); Pose(rig, 100 + warm, 10 + warm);
        rig.Source().LinkToFrameRate(sourceLinked, sourceFPS); rig.Target().LinkToFrameRate(targetLinked, targetFPS);
        rig.Secondary.LinkToFrameRate(TRUE, 0);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 4, CKSECONDARYANIMATION_ONESHOT));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation()); EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame());
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), &rig.Target(), warm, 0, -warm, -50); Pose(rig, 100 + warm, 140);
    }
    static void Stop(ExplicitDistinctRig &rig, float duration, bool modeOverload = false) {
        if (modeOverload) {
            rig.Secondary.SetSecondaryAnimationMode(CKSECONDARYANIMATION_DOWARP);
            ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, duration));
        } else {
            ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, TRUE, duration));
        }
    }
    static void Curve(ExplicitDistinctRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(ExplicitEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(ExplicitEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(ExplicitEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(ExplicitDistinctRig &rig, RCKKeyedAnimation *retained,
                                    CKBOOL linked, float rate, float duration, float first, float last) {
        EXPECT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); if (rig.Character.GetSecondaryAnimationsCount() != 1) return nullptr;
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; EXPECT_EQ(retained, warp); if (!warp) return nullptr;
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0)); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), warp->GetCharacter());
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_EQ(linked != FALSE, warp->IsLinkedToFrameRate() != FALSE); EXPECT_FLOAT_EQ(rate, warp->GetLinkedFrameRate());
        Curve(rig, *warp, duration, first, last); return warp;
    }
    static void Gate(ExplicitDistinctRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(ExplicitDistinctRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};


TEST_F(CharacterSecondaryExplicitDistinctTest, ReverseLinkedReturnPredictsDistinctHalfRateTargetWithoutAdvancingTheQueue) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); ExplicitDistinctRig rig(Context, types); Prepare(rig, 1, TRUE, -1024, TRUE, 512);
        Stop(rig, 2); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 201.5f));
        State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50); Pose(rig, 101, 140); EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame());
        ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 170.75f); Curve(rig, *warp, 2, 140, 201.5f);
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 0, .5f, 0, -51); Pose(rig, 101, 201.5f); Gate(rig, *warp, 2);
        rig.Character.Tick(0); Pose(rig, 101, 201.5f); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 0, 1, 0, -52); Pose(rig, 102, 201.5f);
        rig.Character.Tick(0); Pose(rig, 102, 203);
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, ForwardLinkedReturnThroughModeOverloadKeepsNegativeTargetLiteralZero) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); ExplicitDistinctRig rig(Context, types); Prepare(rig, 9, TRUE, 1024, TRUE, -512);
        Stop(rig, 2, true); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 200));
        State(rig, &rig.Source(), &rig.Target(), 9, 0, -9, -50); Pose(rig, 109, 140);
        ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        State(rig, &rig.Target(), nullptr, 9, 0, -10, -50); Pose(rig, 110, 200); Gate(rig, *warp, 2);
        rig.Character.Tick(0); Pose(rig, 110, 200); Gate(rig, *warp, 2);
        ASSERT_LT(rig.Target().GetNextFrame(.9765625f), 0); rig.Character.Tick(.9765625f);
        Released(rig); State(rig, nullptr, nullptr, 9, 0, -10, -50); Pose(rig, 110, 200);
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, ExactReverseZeroReturnRetainsCurrentPrimaryUntilStrictCrossing) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); ExplicitDistinctRig rig(Context, types); Prepare(rig, 2, TRUE, -1024, TRUE, 512);
        Stop(rig, 2); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 10));
        State(rig, &rig.Source(), &rig.Target(), 2, 0, -2, -50); Pose(rig, 102, 140);
        ASSERT_EQ(0.0f, rig.Source().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 10); Gate(rig, *warp, 2);
        rig.Character.Tick(0); State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 10); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 0, .5f, 0, -51); Pose(rig, 101, 10);
        rig.Character.Tick(0); Pose(rig, 101, 201.5f);
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, MixedReverseWholeAndSplitUseTheirActualProcessEndpoints) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); ExplicitDistinctRig whole(Context, types), split(Context, types);
        Prepare(whole, 1, TRUE, -1024, FALSE, -2048); Prepare(split, 1, TRUE, -1024, FALSE, -2048);
        Stop(whole, 2); Stop(split, 2);
        RCKKeyedAnimation *a = whole.Character.Slot().Animation, *b = split.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(whole, a, TRUE, 1024, 2, 140, 11)); ASSERT_NE(nullptr, Return(split, b, TRUE, 1024, 2, 140, 11));
        State(whole, &whole.Source(), &whole.Target(), 1, 0, -1, -50); Pose(whole, 101, 140);
        State(split, &split.Source(), &split.Target(), 1, 0, -1, -50); Pose(split, 101, 140);
        ASSERT_EQ(2.0f, a->GetNextFrame(1.953125f)); whole.Character.Tick(1.953125f);
        State(whole, &whole.Target(), nullptr, 1, .5f, 0, -51); Pose(whole, 101, 201.5f); Gate(whole, *a, 2); Curve(whole, *a, 2, 140, 201.5f);
        split.Character.Tick(.9765625f); State(split, &split.Source(), &split.Target(), 0, 0, 0, -50); Pose(split, 100, 75); Curve(split, *b, 2, 140, 10);
        split.Character.Tick(.9765625f); State(split, &split.Target(), nullptr, 0, 1, 0, -52); Pose(split, 102, 203); Gate(split, *b, 2); Curve(split, *b, 2, 140, 203);
        EXPECT_NE(whole.Target().GetFrame(), split.Target().GetFrame());
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, MixedHeldZeroAndStrictReleaseApplyEveryActualPrimaryPose) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); ExplicitDistinctRig rig(Context, types); Prepare(rig, 1, TRUE, -1024, FALSE, 4096);
        Stop(rig, 2, true); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 11));
        State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50); Pose(rig, 101, 140);
        rig.Character.Tick(1.953125f); Pose(rig, 101, 201.5f); Gate(rig, *warp, 2);
        ASSERT_EQ(2.0f, warp->GetNextFrame(0)); ASSERT_EQ(1.5f, rig.Target().GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 1, 1.5f, 0, -53); Pose(rig, 103, 204.5f); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 140, 204.5f);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 1, 2.5f, 0, -55); Pose(rig, 105, 207.5f);
        rig.Character.Tick(0); Pose(rig, 107, 210.5f);
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, MixedReturnSamplesStoppedLinkedTargetLengthInsteadOfItsOldCursor) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); ExplicitDistinctRig rig(Context, types); Prepare(rig, 9.5f, FALSE, -2048, TRUE, 512);
        rig.Target().SetCanBeInterrupt(TRUE); Stop(rig, 4); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 2048, 4, 140, 19.5f));
        State(rig, &rig.Source(), &rig.Target(), 9.5f, 0, -9.5f, -50); Pose(rig, 109.5f, 140);
        rig.Character.Tick(7.8125f); State(rig, &rig.Target(), nullptr, 9.5f, 2, -10, -54); Pose(rig, 114, 156.5f);
        ASSERT_GT(rig.Target().GetNextFrame(62.5f), 20); rig.Character.Tick(62.5f);
        State(rig, nullptr, nullptr, 9.5f, 2, -10, -90); Pose(rig, 150, 200); Gate(rig, *warp, 2); Curve(rig, *warp, 4, 140, 260);
        rig.Character.Tick(0); Pose(rig, 150, 230); Gate(rig, *warp, 3);
        rig.Character.Tick(0); Pose(rig, 150, 260); Gate(rig, *warp, 4);
        rig.Character.Tick(0); Released(rig); State(rig, nullptr, nullptr, 9.5f, 2, -10, -90); Pose(rig, 150, 260);
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, StoppingStartingWarpCapturesItsCurrentBlendThroughBothOverloads) {
    for (int types = 0; types < 3; ++types) for (int mode = 0; mode < 2; ++mode) {
        SCOPED_TRACE(types);
        SCOPED_TRACE(mode);
        ExplicitDistinctRig rig(Context, types); rig.Source().LinkToFrameRate(TRUE, 1024); rig.Character.Tick(.9765625f);
        rig.Source().LinkToFrameRate(TRUE, 0); rig.Secondary.LinkToFrameRate(TRUE, 1024);
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 4, flags, 4));
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; ASSERT_NE(nullptr, retained);
        ASSERT_EQ(1.0f, retained->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        EXPECT_EQ(1, retained->GetAnimationCount());
        EXPECT_EQ(nullptr, retained->GetAnimation(ExplicitEntity(rig.Root)));
        CKObjectAnimation *startingChild = retained->GetAnimation(0); ASSERT_NE(nullptr, startingChild);
        EXPECT_EQ(ExplicitEntity(rig.Limb), startingChild->Get3dEntity());
        const float startingTimes[] = {0, 2, 4}, startingValues[] = {11, 75.5f, 140};
        for (int i = 0; i < 3; ++i) {
            VxVector value; ASSERT_TRUE(startingChild->EvaluatePosition(startingTimes[i], value));
            EXPECT_NEAR(startingValues[i], value.x, .0005f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
        }
        Pose(rig, 101, 43.25f);
        rig.Source().LinkToFrameRate(TRUE, -1024); rig.Target().LinkToFrameRate(TRUE, 512);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50);
        Stop(rig, 2, mode != 0); ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 43.25f, 201.5f));
        State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50); Pose(rig, 101, 43.25f);
        rig.Character.Tick(1.953125f); State(rig, &rig.Target(), nullptr, 1, .5f, 0, -51); Pose(rig, 101, 201.5f); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 102, 201.5f); rig.Character.Tick(0); Pose(rig, 102, 203);
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, RepeatedBoolAndModeStopsKeepProgressDurationAndCapturedStart) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); ExplicitDistinctRig rig(Context, types); Prepare(rig, 1, TRUE, -1024, FALSE, -2048);
        Stop(rig, 2); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 11));
        rig.Character.Tick(.9765625f); State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 75); Gate(rig, *warp, 1); Curve(rig, *warp, 2, 140, 10);
        rig.Secondary.SetCurrentStep(.2f); EXPECT_FLOAT_EQ(2, rig.Secondary.GetFrame());
        Stop(rig, 8); Stop(rig, 4, true);
        State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 75); Gate(rig, *warp, 1);
        EXPECT_FLOAT_EQ(2, warp->GetLength()); EXPECT_TRUE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(1024, warp->GetLinkedFrameRate()); Curve(rig, *warp, 2, 140, 10);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 0, 1, 0, -52); Pose(rig, 102, 203); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 140, 203);
        Stop(rig, 16); Stop(rig, 32, true); Pose(rig, 102, 203); Gate(rig, *warp, 2); EXPECT_FLOAT_EQ(2, warp->GetLength());
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 104, 206);
    }
}
TEST_F(CharacterSecondaryExplicitDistinctTest, QueuedOriginalSecondaryUsesCapturedBridgeInsteadOfAliasingTheBuilder) {
    for (int types = 0; types <= 2; types += 2) for (int mode = 0; mode < 2; ++mode) {
        SCOPED_TRACE(types);
        SCOPED_TRACE(mode);
        ExplicitDistinctRig rig(Context, types); Prepare(rig, 1, TRUE, -1024, TRUE, 512);
        rig.Secondary.LinkToFrameRate(TRUE, 512);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Secondary, CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), &rig.Secondary, 1, 0, -1, -50); Pose(rig, 101, 140);
        EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame());
        Stop(rig, 2, mode != 0); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 105));
        State(rig, &rig.Source(), &rig.Secondary, 1, 0, -1, -50); Pose(rig, 101, 140);
        EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame());
        ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Source(), &rig.Secondary, 0, 0, 0, -50); Pose(rig, 100, 122.5f); Curve(rig, *warp, 2, 140, 105);
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Secondary, nullptr, 0, 0, 0, -50); EXPECT_FLOAT_EQ(.5f, rig.Secondary.GetFrame());
        Pose(rig, 100, 105); Gate(rig, *warp, 2);
        rig.Character.Tick(0); EXPECT_FLOAT_EQ(.5f, rig.Secondary.GetFrame()); Pose(rig, 100, 105); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Secondary, nullptr, 0, 0, 0, -50);
        EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); Pose(rig, 100, 105);
        rig.Character.Tick(0); EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); Pose(rig, 100, 110);
    }
}
} // namespace
