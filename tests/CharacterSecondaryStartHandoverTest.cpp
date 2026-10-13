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
CK_CLASSID StartHandoverDerivedID = 0;
class StartHandoverDerived : public RCKKeyedAnimation {
public:
    explicit StartHandoverDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return StartHandoverDerivedID; }
};
class StartHandoverCharacter : public RCKCharacter {
public:
    explicit StartHandoverCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *HandoverEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class StartHandoverRig {
public:
    StartHandoverCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    StartHandoverDerived SourceDerived, TargetDerived;
    int Types;
    explicit StartHandoverRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, HandoverEntity(Character));
        EXPECT_TRUE(Limb.SetParent(HandoverEntity(Root), TRUE));
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
    ~StartHandoverRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(HandoverEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryStartHandoverTest : public ::testing::Test {
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
        StartHandoverDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(StartHandoverDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(StartHandoverRig &rig, float world, float limb) {
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
        EXPECT_EQ(HandoverEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(HandoverEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(StartHandoverRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Prepare(StartHandoverRig &rig, CKBOOL originalLinked = TRUE, float originalRate = -1024) {
        rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_FLOAT_EQ(2, rig.Source().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        rig.Source().LinkToFrameRate(TRUE, 0); State(rig, &rig.Source(), nullptr, 2, 0, -2, -50); Pose(rig, 102, 12);
        rig.Secondary.LinkToFrameRate(originalLinked, originalRate);
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 4, flags, 4));
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, warp);
        Starting(rig, *warp, 0); Pose(rig, 102, 12);
        EXPECT_EQ(originalLinked != FALSE, warp->IsLinkedToFrameRate() != FALSE);
        EXPECT_FLOAT_EQ(originalRate < 0 ? -originalRate : originalRate, warp->GetLinkedFrameRate());
    }
    static void Starting(StartHandoverRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); EXPECT_FLOAT_EQ(4, warp.GetLength());
        EXPECT_NEAR(frame, warp.GetFrame(), .00002f); EXPECT_EQ(1, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(HandoverEntity(rig.Root)));
        CKObjectAnimation *child = warp.GetAnimation(0); ASSERT_NE(nullptr, child);
        EXPECT_EQ(HandoverEntity(rig.Limb), child->Get3dEntity());
        const float times[] = {0, 2, 4}, values[] = {12, 76, 140};
        for (int i = 0; i < 3; ++i) {
            VxVector value; EXPECT_TRUE(child->EvaluatePosition(times[i], value));
            EXPECT_NEAR(values[i], value.x, .0004f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
        }
        Main(rig);
    }
    static void Main(StartHandoverRig &rig) {
        State(rig, &rig.Source(), nullptr, 2, 0, -2, -50);
        EXPECT_TRUE(rig.Source().IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(0, rig.Source().GetLinkedFrameRate());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Secondary.GetCharacter());
        EXPECT_EQ(nullptr, rig.Secondary.GetAnimation(HandoverEntity(rig.Root)));
        EXPECT_EQ(&rig.SecondaryLimb, rig.Secondary.GetAnimation(HandoverEntity(rig.Limb)));
    }
    static void Playing(StartHandoverRig &rig, RCKKeyedAnimation &retained, float frame, float limb) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_EQ(&retained, rig.Character.Slot().Animation); EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&rig.Secondary), rig.Limb.GetExclusiveAnimation());
        EXPECT_NEAR(frame, rig.Secondary.GetFrame(), .0004f); Main(rig); Pose(rig, 102, limb);
        VxVector value; ASSERT_TRUE(rig.SecondaryLimb.EvaluatePosition(frame, value));
        EXPECT_NEAR(limb, value.x, .0005f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
    }
    static void Gate(StartHandoverRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }

};

TEST_F(CharacterSecondaryStartHandoverTest, NegativeOriginalClockStartsAtSignedRemainderAndLaterExpiresAtZero) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Starting(rig, *warp, 4); Pose(rig, 102, 140);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 4); rig.Character.Tick(.9765625f);
        Playing(rig, *warp, 3, 130);
        for (int i = 1; i <= 3; ++i) {
            if (i == 3) ASSERT_EQ(0.0f, rig.Secondary.GetNextFrame(.9765625f));
            rig.Character.Tick(.9765625f); Playing(rig, *warp, 3.0f - i, 130.0f - 10.0f * i);
        }
        ASSERT_LT(rig.Secondary.GetNextFrame(.9765625f), 0); rig.Character.Tick(.9765625f);
        Gate(rig, *warp, 0); Main(rig); Pose(rig, 102, 100); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        EXPECT_TRUE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(0, warp->GetLinkedFrameRate());
        EXPECT_FLOAT_EQ(4, warp->GetLength());
        // The paused main's existing future forecast is a separate contract. Here only
        // the real reverse endpoint capture, phase, retained identity and gate are certified.
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, PublicFasterPrivateClockConvertsUnusedTimeToNegativeOriginalFrames) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, TRUE, -512);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        // Explicit SDK private-clock control, not automatic clock restoration.
        warp->LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Starting(rig, *warp, 4); Pose(rig, 102, 140);
        rig.Character.Tick(.9765625f); Playing(rig, *warp, 3.5f, 135);
        rig.Character.Tick(1.953125f); Playing(rig, *warp, 2.5f, 125);
        EXPECT_FLOAT_EQ(-512, rig.Secondary.GetLinkedFrameRate()); EXPECT_FLOAT_EQ(1024, warp->GetLinkedFrameRate());
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, PublicFasterPrivateClockConvertsUnusedTimeToPositiveOriginalFrames) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, TRUE, 512);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        warp->LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Starting(rig, *warp, 4); Pose(rig, 102, 140);
        rig.Character.Tick(.9765625f); Playing(rig, *warp, 4.5f, 145);
        rig.Character.Tick(.9765625f); Playing(rig, *warp, 5, 150);
        EXPECT_FLOAT_EQ(512, rig.Secondary.GetLinkedFrameRate());
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, PausedOriginalAndPrivateRemainPausedUntilOnlyThePrivateClockIsChanged) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, TRUE, 0);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_EQ(0.0f, warp->GetNextFrame(1000)); rig.Character.Tick(1000);
        Starting(rig, *warp, 0); Pose(rig, 102, 12);
        // Changing the private clock is deliberate SDK control; the original remains paused.
        warp->LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Starting(rig, *warp, 4); Pose(rig, 102, 140);
        rig.Character.Tick(.9765625f); Playing(rig, *warp, 4, 140);
        rig.Character.Tick(1000); Playing(rig, *warp, 4, 140); EXPECT_FLOAT_EQ(0, rig.Secondary.GetLinkedFrameRate());
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, LinkedPrivateToUnlinkedOriginalUsesOnlyTheRemainingProcessFraction) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, FALSE, -2048);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        warp->LinkToFrameRate(TRUE, 1024);
        ASSERT_FLOAT_EQ(3, warp->GetNextFrame(2.9296875f)); rig.Character.Tick(2.9296875f);
        Starting(rig, *warp, 3); Pose(rig, 102, 108);
        ASSERT_GT(warp->GetNextFrame(1.953125f), 4); rig.Character.Tick(1.953125f);
        // One of this call's two private frames remains: half an unlinked process step.
        Playing(rig, *warp, 4.5f, 145);
        ASSERT_FLOAT_EQ(5.5f, rig.Secondary.GetNextFrame(0)); rig.Character.Tick(0); Playing(rig, *warp, 5.5f, 155);
        EXPECT_FALSE(rig.Secondary.IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(-2048, rig.Secondary.GetLinkedFrameRate());
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, BothUnlinkedClocksUseCallCountEvenAtZeroElapsedAndNegativeStoredRate) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, FALSE, -2048);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        for (int i = 1; i <= 4; ++i) {
            ASSERT_EQ(static_cast<float>(i), warp->GetNextFrame(0)); rig.Character.Tick(0);
            Starting(rig, *warp, static_cast<float>(i)); Pose(rig, 102, 12 + 32.0f * i);
        }
        ASSERT_EQ(5.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); Playing(rig, *warp, 5, 150);
        rig.Character.Tick(0); Playing(rig, *warp, 6, 160);
        EXPECT_FALSE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(2048, warp->GetLinkedFrameRate());
        EXPECT_FALSE(rig.Secondary.IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(-2048, rig.Secondary.GetLinkedFrameRate());
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, UnlinkedPrivateToPositiveLinkedOriginalUsesTheActualHandoverCallTime) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, TRUE, 512);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        warp->LinkToFrameRate(FALSE, -2048);
        for (int i = 1; i <= 3; ++i) rig.Character.Tick(0);
        Starting(rig, *warp, 3); Pose(rig, 102, 108);
        ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Starting(rig, *warp, 4); Pose(rig, 102, 140);
        ASSERT_EQ(5.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Playing(rig, *warp, 6, 160); rig.Character.Tick(0); Playing(rig, *warp, 6, 160);
        EXPECT_FALSE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(-2048, warp->GetLinkedFrameRate());
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, UnlinkedPrivateToNegativeLinkedOriginalUsesSignedActualCallTime) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, TRUE, -512);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        warp->LinkToFrameRate(FALSE, 2048);
        for (int i = 1; i <= 4; ++i) rig.Character.Tick(0);
        Starting(rig, *warp, 4); Pose(rig, 102, 140);
        ASSERT_EQ(5.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Playing(rig, *warp, 2, 120); rig.Character.Tick(1.953125f); Playing(rig, *warp, 1, 110);
        EXPECT_FLOAT_EQ(-512, rig.Secondary.GetLinkedFrameRate());
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, UnlinkedPrivateCrossingAtZeroTimeTouchesLinkedOriginalAtItsLiteralStartingFrame) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig, TRUE, -512);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        warp->LinkToFrameRate(FALSE, -2048);
        for (int i = 1; i <= 4; ++i) rig.Character.Tick(0);
        Starting(rig, *warp, 4); Pose(rig, 102, 140);
        ASSERT_EQ(5.0f, warp->GetNextFrame(0)); rig.Character.Tick(0);
        Playing(rig, *warp, 4, 140); rig.Character.Tick(0); Playing(rig, *warp, 4, 140);
    }
}
TEST_F(CharacterSecondaryStartHandoverTest, LargeReverseHandoverClampsTheOriginalAtZeroAndDefersNaturalExpiryOneCall) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); StartHandoverRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_GT(warp->GetNextFrame(19.53125f), 4); rig.Character.Tick(19.53125f);
        Playing(rig, *warp, 0, 100);
        // The handover phase always ends this iteration. The strict reverse expiry is later.
        ASSERT_LT(rig.Secondary.GetNextFrame(.9765625f), 0); rig.Character.Tick(.9765625f);
        Gate(rig, *warp, 0); Main(rig); Pose(rig, 102, 100); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        EXPECT_FLOAT_EQ(0, warp->GetLinkedFrameRate()); EXPECT_TRUE(warp->IsLinkedToFrameRate());
    }
}
} // namespace
