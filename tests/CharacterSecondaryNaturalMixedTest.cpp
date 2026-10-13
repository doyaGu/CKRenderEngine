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
CK_CLASSID NaturalMixedDerivedID = 0;
class NaturalMixedDerived : public RCKKeyedAnimation {
public:
    explicit NaturalMixedDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return NaturalMixedDerivedID; }
};
class NaturalMixedCharacter : public RCKCharacter {
public:
    explicit NaturalMixedCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *DistinctEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class NaturalMixedRig {
public:
    NaturalMixedCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    NaturalMixedDerived SourceDerived, TargetDerived;
    int Types;
    explicit NaturalMixedRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, DistinctEntity(Character));
        EXPECT_TRUE(Limb.SetParent(DistinctEntity(Root), TRUE));
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
    ~NaturalMixedRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(DistinctEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryNaturalMixedTest : public ::testing::Test {
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
        NaturalMixedDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(NaturalMixedDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(NaturalMixedRig &rig, float world, float limb) {
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
        EXPECT_EQ(DistinctEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(DistinctEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(NaturalMixedRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Prepare(NaturalMixedRig &rig, float warm, CKBOOL sourceLinked, float sourceFPS,
                        CKBOOL targetLinked, float targetFPS, bool reverseSecondary = true, float requested = 2) {
        const float duration = requested < 1 ? 1 : requested;
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, reverseSecondary ? 0 : 8, flags, requested));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
        ASSERT_EQ(duration, starting->GetNextFrame(duration * .9765625f)); rig.Character.Tick(duration * .9765625f);
        ASSERT_GT(starting->GetNextFrame(.9765625f), duration); rig.Character.Tick(.9765625f);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_FLOAT_EQ(reverseSecondary ? 1 : 9, rig.Secondary.GetFrame());
        rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_NEAR(warm, rig.Source().GetNextFrame(warm * .9765625f), .00001f); rig.Character.Tick(warm * .9765625f);
        State(rig, &rig.Source(), nullptr, warm, 0, -warm, -50); Pose(rig, 100 + warm, reverseSecondary ? 110 : 190);
        rig.Source().LinkToFrameRate(sourceLinked, sourceFPS); rig.Target().LinkToFrameRate(targetLinked, targetFPS);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), &rig.Target(), warm, 0, -warm, -50);
        rig.Secondary.LinkToFrameRate(TRUE, reverseSecondary ? -1024 : 1024);
        ASSERT_EQ(reverseSecondary ? -1.0f : 11.0f, rig.Secondary.GetNextFrame(1.953125f));
    }
    static void Curve(NaturalMixedRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(DistinctEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(DistinctEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(DistinctEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(NaturalMixedRig &rig, RCKKeyedAnimation *retained,
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
    static void Gate(NaturalMixedRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(NaturalMixedRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};

TEST_F(CharacterSecondaryNaturalMixedTest, LinkedReverseWholeAndSplitHaveDifferentRealUnlinkedEndpoints) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig whole(Context, types), split(Context, types);
        Prepare(whole, 3, TRUE, -1024, FALSE, -2048); Prepare(split, 3, TRUE, -1024, FALSE, -2048);
        RCKKeyedAnimation *a = whole.Character.Slot().Animation, *b = split.Character.Slot().Animation;
        whole.Character.Tick(1.953125f); split.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(whole, a, TRUE, 1024, 2, 100, 11)); ASSERT_NE(nullptr, Return(split, b, TRUE, 1024, 2, 100, 11));
        ASSERT_EQ(2.0f, a->GetNextFrame(1.953125f)); whole.Character.Tick(1.953125f);
        State(whole, &whole.Target(), nullptr, 1, .5f, 0, -51); Pose(whole, 101, 201.5f); Gate(whole, *a, 2); Curve(whole, *a, 2, 100, 201.5f);
        ASSERT_EQ(1.0f, b->GetNextFrame(.9765625f)); split.Character.Tick(.9765625f);
        State(split, &split.Source(), &split.Target(), 0, 0, 0, -50); Pose(split, 100, 55); Gate(split, *b, 1); Curve(split, *b, 2, 100, 10);
        ASSERT_EQ(2.0f, b->GetNextFrame(.9765625f)); split.Character.Tick(.9765625f);
        State(split, &split.Target(), nullptr, 0, 1, 0, -52); Pose(split, 102, 203); Gate(split, *b, 2); Curve(split, *b, 2, 100, 203);
        EXPECT_NE(whole.Target().GetFrame(), split.Target().GetFrame());
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, LinkedForwardExactHandoffSplitDiffersFromWholeProcessFraction) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig whole(Context, types), split(Context, types);
        Prepare(whole, 7, TRUE, 1024, FALSE, 0); Prepare(split, 7, TRUE, 1024, FALSE, 0);
        RCKKeyedAnimation *a = whole.Character.Slot().Animation, *b = split.Character.Slot().Animation;
        whole.Character.Tick(1.953125f); split.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(whole, a, TRUE, 1024, 2, 100, 19)); ASSERT_NE(nullptr, Return(split, b, TRUE, 1024, 2, 100, 19));
        ASSERT_EQ(2.0f, a->GetNextFrame(1.953125f)); whole.Character.Tick(1.953125f);
        State(whole, &whole.Target(), nullptr, 9, .5f, -10, -51); Pose(whole, 111, 201.5f); Curve(whole, *a, 2, 100, 201.5f);
        ASSERT_EQ(10.0f, split.Source().GetNextFrame(.9765625f)); split.Character.Tick(.9765625f);
        State(split, &split.Target(), nullptr, 9, 0, -10, -50); Pose(split, 110, 150); Curve(split, *b, 2, 100, 200);
        ASSERT_EQ(2.0f, b->GetNextFrame(.9765625f)); split.Character.Tick(.9765625f);
        State(split, &split.Target(), nullptr, 9, 1, -10, -52); Pose(split, 112, 203); Gate(split, *b, 2); Curve(split, *b, 2, 100, 203);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, HeldZeroTimeAndStrictReleaseTrackEveryActualUnlinkedMainCall) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 3, TRUE, -1024, FALSE, 4096);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 100, 11));
        ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f); Pose(rig, 101, 201.5f); Gate(rig, *warp, 2);
        ASSERT_EQ(2.0f, warp->GetNextFrame(0)); ASSERT_EQ(1.5f, rig.Target().GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 1, 1.5f, 0, -53); Pose(rig, 103, 204.5f); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 100, 204.5f);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 2); rig.Character.Tick(.9765625f); Released(rig);
        State(rig, &rig.Target(), nullptr, 1, 2.5f, 0, -55); Pose(rig, 105, 207.5f);
        rig.Character.Tick(0); State(rig, &rig.Target(), nullptr, 1, 3.5f, 0, -57); Pose(rig, 107, 210.5f);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, UnlinkedToLinkedSameCallsDifferentTimeDistributionProduceDifferentEndpoints) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig late(Context, types), early(Context, types);
        Prepare(late, 8.5f, FALSE, -2048, TRUE, 512); Prepare(early, 8.5f, FALSE, -2048, TRUE, 512);
        RCKKeyedAnimation *a = late.Character.Slot().Animation, *b = early.Character.Slot().Animation;
        late.Character.Tick(1.953125f); early.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(late, a, FALSE, 2048, 2, 100, 19.5f)); ASSERT_NE(nullptr, Return(early, b, FALSE, 2048, 2, 100, 19.5f));
        ASSERT_EQ(10.5f, late.Source().GetNextFrame(0)); ASSERT_EQ(1.0f, a->GetNextFrame(0)); late.Character.Tick(0);
        State(late, &late.Target(), nullptr, 9.5f, 0, -10, -50); Pose(late, 110, 150); Curve(late, *a, 2, 100, 200);
        ASSERT_EQ(1.0f, b->GetNextFrame(7.8125f)); early.Character.Tick(7.8125f);
        State(early, &early.Target(), nullptr, 9.5f, 2, -10, -54); Pose(early, 114, 153); Curve(early, *b, 2, 100, 206);
        ASSERT_EQ(2.0f, a->GetNextFrame(7.8125f)); late.Character.Tick(7.8125f);
        State(late, &late.Target(), nullptr, 9.5f, 4, -10, -58); Pose(late, 118, 212); Gate(late, *a, 2); Curve(late, *a, 2, 100, 212);
        ASSERT_EQ(2.0f, b->GetNextFrame(0)); early.Character.Tick(0);
        State(early, &early.Target(), nullptr, 9.5f, 2, -10, -54); Pose(early, 114, 206); Gate(early, *b, 2); Curve(early, *b, 2, 100, 206);
        EXPECT_NE(late.Target().GetFrame(), early.Target().GetFrame());
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, HalfRateLinkedTargetBlendsCurrentPoseOnAnUnlinkedReturnClock) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 8.5f, FALSE, 0, TRUE, 256);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 0, 2, 100, 19.5f));
        ASSERT_EQ(1.0f, warp->GetNextFrame(7.8125f)); rig.Character.Tick(7.8125f);
        State(rig, &rig.Target(), nullptr, 9.5f, 1, -10, -52); Pose(rig, 112, 151.5f); Curve(rig, *warp, 2, 100, 203);
        ASSERT_EQ(2.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); Gate(rig, *warp, 2); Pose(rig, 112, 203);
        ASSERT_GT(warp->GetNextFrame(0), 2); rig.Character.Tick(0); Released(rig); Pose(rig, 112, 203);
        rig.Character.Tick(0); Pose(rig, 112, 203);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, NegativeLinkedTargetStoppingAtLiteralZeroKeepsItsTrueTerminalPose) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 8.5f, FALSE, -2048, TRUE, -512);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 2048, 2, 100, 19.5f));
        rig.Character.Tick(7.8125f); State(rig, &rig.Target(), nullptr, 9.5f, 0, -10, -50); Pose(rig, 110, 150);
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); ASSERT_LT(rig.Target().GetNextFrame(.9765625f), 0);
        rig.Character.Tick(.9765625f); State(rig, nullptr, nullptr, 9.5f, 0, -10, -50); Pose(rig, 110, 200); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 100, 200);
        rig.Character.Tick(0); Released(rig); Pose(rig, 110, 200); rig.Character.Tick(0); Pose(rig, 110, 200);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, PausedLinkedTargetDoesNotPauseTheTriggeringUnlinkedReturn) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 8.5f, FALSE, 4096, TRUE, 0);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 4096, 2, 100, 19.5f));
        ASSERT_EQ(1.0f, warp->GetNextFrame(1000)); rig.Character.Tick(1000); Pose(rig, 110, 150); Curve(rig, *warp, 2, 100, 200);
        ASSERT_EQ(2.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); Gate(rig, *warp, 2); Pose(rig, 110, 200);
        rig.Character.Tick(0); Released(rig); State(rig, &rig.Target(), nullptr, 9.5f, 0, -10, -50); Pose(rig, 110, 200);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, NegativeStoredUnlinkedSourceStillCrossesForwardAtZeroElapsedTime) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 8.5f, FALSE, -2048, TRUE, 512);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 2048, 2, 100, 19.5f));
        ASSERT_EQ(10.5f, rig.Source().GetNextFrame(0)); ASSERT_EQ(1.0f, warp->GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 9.5f, 0, -10, -50); Pose(rig, 110, 150); Curve(rig, *warp, 2, 100, 200);
        ASSERT_EQ(2.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); Gate(rig, *warp, 2); Pose(rig, 110, 200);
        rig.Character.Tick(0); Released(rig); Pose(rig, 110, 200);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, MinimumDurationOneSamplesActualUnlinkedTargetAfterReverseCrossing) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 2.5f, TRUE, -1024, FALSE, -2048, true, .5f);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 1, 100, 10.5f));
        ASSERT_LT(rig.Source().GetNextFrame(.9765625f), 0); ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, .5f, .5f, 0, -51); Pose(rig, 101, 201.5f); Gate(rig, *warp, 1); Curve(rig, *warp, 1, 100, 201.5f);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 103, 204.5f);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, ForwardSecondaryReturnRetainsItsOriginalStartAcrossRepeatedRebuilds) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 3, TRUE, -1024, FALSE, -2048, false);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 190, 11)); EXPECT_FLOAT_EQ(9, rig.Secondary.GetFrame());
        // A legal cursor edit must not change the already captured return start.
        rig.Secondary.SetCurrentStep(.2f); EXPECT_FLOAT_EQ(2, rig.Secondary.GetFrame()); Pose(rig, 101, 190);
        rig.Character.Tick(.9765625f); Pose(rig, 100, 100); Curve(rig, *warp, 2, 190, 10);
        rig.Character.Tick(.9765625f); Pose(rig, 102, 203); Curve(rig, *warp, 2, 190, 203);
        rig.Character.Tick(0); Pose(rig, 104, 206); Curve(rig, *warp, 2, 190, 206);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 106, 209);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, ReplayedSlotReturnsToOrdinaryFixedSameClockInterpolation) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 3, TRUE, -1024, FALSE, -2048);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 100, 11));
        rig.Character.Tick(1.953125f); rig.Character.Tick(.9765625f); Released(rig);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; rig.Character.SetWorldMatrix(world);
        VxVector zero(0, 0, 0); rig.Root.SetPosition(&zero, DistinctEntity(rig.Character));
        rig.Source().CenterAnimation(0); rig.Source().SetFrame(0); rig.Source().LinkToFrameRate(TRUE, 0);
        ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(&rig.Source()));
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        rig.Secondary.LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 0, flags, 2));
        EXPECT_EQ(retained, rig.Character.Slot().Animation);
        rig.Character.Tick(1.953125f); rig.Character.Tick(.9765625f);
        rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Source().LinkToFrameRate(TRUE, 1024); rig.Character.Tick(7.8125f);
        rig.Source().LinkToFrameRate(TRUE, -1024); rig.Secondary.LinkToFrameRate(TRUE, -1024); rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 100, 14));
        rig.Character.Tick(.9765625f); State(rig, &rig.Source(), nullptr, 5, 1.5f, -5, -53); Pose(rig, 105, 57); Curve(rig, *retained, 2, 100, 14);
        rig.Character.Tick(.9765625f); Pose(rig, 104, 14); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 103, 14);
        rig.Character.Tick(0); Pose(rig, 103, 13);
    }
}
TEST_F(CharacterSecondaryNaturalMixedTest, ZeroTimeBeforeLinkedBoundaryKeepsTheRealQueueAndCapturedStart) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalMixedRig rig(Context, types); Prepare(rig, 3, TRUE, -1024, FALSE, -2048);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 100, 11));
        ASSERT_EQ(0.0f, warp->GetNextFrame(0)); ASSERT_EQ(1.0f, rig.Source().GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50); Pose(rig, 101, 100); Gate(rig, *warp, 0); Curve(rig, *warp, 2, 100, 11);
        rig.Character.Tick(1.953125f); State(rig, &rig.Target(), nullptr, 1, .5f, 0, -51); Pose(rig, 101, 201.5f); Gate(rig, *warp, 2);
    }
}
} // namespace
