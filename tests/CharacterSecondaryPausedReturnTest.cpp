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
CK_CLASSID PausedReturnDerivedID = 0;
class PausedReturnDerived : public RCKKeyedAnimation {
public:
    explicit PausedReturnDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return PausedReturnDerivedID; }
};
class PausedReturnCharacter : public RCKCharacter {
public:
    explicit PausedReturnCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *PausedEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class PausedReturnRig {
public:
    PausedReturnCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    PausedReturnDerived SourceDerived, TargetDerived;
    int Types;
    explicit PausedReturnRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, PausedEntity(Character));
        EXPECT_TRUE(Limb.SetParent(PausedEntity(Root), TRUE));
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
    ~PausedReturnRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(PausedEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryPausedReturnTest : public ::testing::Test {
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
        PausedReturnDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(PausedReturnDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(PausedReturnRig &rig, float world, float limb) {
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
        EXPECT_EQ(PausedEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(PausedEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(PausedReturnRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Main(PausedReturnRig &rig, float frame = 2) {
        State(rig, &rig.Source(), nullptr, frame, 0, -frame, -50);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Secondary.GetCharacter());
        EXPECT_EQ(nullptr, rig.Secondary.GetAnimation(PausedEntity(rig.Root)));
    }
    static void Starting(PausedReturnRig &rig, RCKKeyedAnimation &warp, float phase, float target) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); EXPECT_FLOAT_EQ(4, warp.GetLength());
        EXPECT_NEAR(phase, warp.GetFrame(), .00001f); EXPECT_EQ(1, warp.GetAnimationCount());
        CKObjectAnimation *child = warp.GetAnimation(0); ASSERT_NE(nullptr, child);
        EXPECT_EQ(PausedEntity(rig.Limb), child->Get3dEntity());
        const float times[] = {0, 2, 4}, values[] = {12, (12 + target) / 2, target};
        for (int i = 0; i < 3; ++i) {
            VxVector value; EXPECT_TRUE(child->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
            EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
        }
        Main(rig); SourceClock(rig, TRUE, 0);
    }
    static void Prepare(PausedReturnRig &rig, float start = 0, float rate = 1024, bool partial = false) {
        rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_FLOAT_EQ(2, rig.Source().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        rig.Source().LinkToFrameRate(TRUE, 0); Main(rig); Pose(rig, 102, 12);
        rig.Secondary.LinkToFrameRate(TRUE, rate);
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, start, flags, 4));
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, warp);
        Starting(rig, *warp, 0, 100 + 10 * start); Pose(rig, 102, 12);
        if (partial) {
            ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
            Starting(rig, *warp, 2, 140); Pose(rig, 102, 76); return;
        }
        ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        Starting(rig, *warp, 4, 100 + 10 * start); Pose(rig, 102, 100 + 10 * start);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 4); rig.Character.Tick(.9765625f);
        Playing(rig, start + (rate < 0 ? -1.0f : 1.0f));
    }
    static void Playing(PausedReturnRig &rig, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&rig.Secondary), rig.Limb.GetExclusiveAnimation());
        EXPECT_NEAR(frame, rig.Secondary.GetFrame(), .0002f); Main(rig); Pose(rig, 102, 100 + 10 * frame);
    }
    static void PositiveTerminal(PausedReturnRig &rig) {
        Prepare(rig); ASSERT_FALSE(::testing::Test::HasFatalFailure());
        for (int i = 2; i <= 10; ++i) {
            if (i == 10) ASSERT_EQ(10.0f, rig.Secondary.GetNextFrame(.9765625f));
            else ASSERT_FLOAT_EQ(static_cast<float>(i), rig.Secondary.GetNextFrame(.9765625f));
            rig.Character.Tick(.9765625f);
            Playing(rig, static_cast<float>(i));
        }
        // Natural positive expiry samples its real retained pose; reach exact10 first.
        EXPECT_FLOAT_EQ(10, rig.Secondary.GetFrame()); Pose(rig, 102, 200);
    }
    static void ReverseTerminal(PausedReturnRig &rig) {
        Prepare(rig, 4, -1024); ASSERT_FALSE(::testing::Test::HasFatalFailure());
        for (int i = 2; i >= 0; --i) {
            if (i == 0) ASSERT_EQ(0.0f, rig.Secondary.GetNextFrame(.9765625f));
            else ASSERT_FLOAT_EQ(static_cast<float>(i), rig.Secondary.GetNextFrame(.9765625f));
            rig.Character.Tick(.9765625f);
            Playing(rig, static_cast<float>(i));
        }
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); Pose(rig, 102, 100);
    }
    static void ResumeAndFinish(PausedReturnRig &rig, RCKKeyedAnimation &warp, float first, float midpoint) {
        // Deliberate public control resumes only the private return, never the paused main.
        warp.LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(2.0f, warp.GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        Gate(rig, warp, 2); Main(rig); Pose(rig, 102, midpoint); Curve(rig, warp, 4, first, 12);
        ASSERT_EQ(4.0f, warp.GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        Gate(rig, warp, 4); Main(rig); Pose(rig, 102, 12);
        rig.Character.Tick(0); Gate(rig, warp, 4); Pose(rig, 102, 12);
        ASSERT_GT(warp.GetNextFrame(.9765625f), 4); rig.Character.Tick(.9765625f);
        Released(rig); Main(rig); Pose(rig, 102, 12);
        rig.Character.Tick(0); Main(rig); Pose(rig, 102, 12); SourceClock(rig, TRUE, 0);
    }
    static void SourceClock(PausedReturnRig &rig, CKBOOL linked, float rate) {
        EXPECT_EQ(linked != FALSE, rig.Source().IsLinkedToFrameRate() != FALSE);
        EXPECT_FLOAT_EQ(rate, rig.Source().GetLinkedFrameRate());
    }
    static void Stop(PausedReturnRig &rig, bool mode = false, float duration = 4) {
        if (mode) {
            rig.Secondary.SetSecondaryAnimationMode(CKSECONDARYANIMATION_DOWARP);
            ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, duration));
        } else ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, TRUE, duration));
    }
    static void Curve(PausedReturnRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(PausedEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(PausedEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(PausedEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(PausedReturnRig &rig, RCKKeyedAnimation *retained,
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
    static void Gate(PausedReturnRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(PausedReturnRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};

TEST_F(CharacterSecondaryPausedReturnTest, NaturalPositiveEndpointReturnsToThePausedCurrentPoseWhenOnlyPrivateClockResumes) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); PositiveTerminal(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_GT(rig.Secondary.GetNextFrame(.9765625f), 10); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 200, 12)); Main(rig); Pose(rig, 102, 200);
        EXPECT_FLOAT_EQ(10, rig.Secondary.GetFrame());
        ASSERT_EQ(0.0f, warp->GetNextFrame(1000)); rig.Character.Tick(1000);
        Gate(rig, *warp, 0); Main(rig); Pose(rig, 102, 200); Curve(rig, *warp, 4, 200, 12);
        ResumeAndFinish(rig, *warp, 200, 106); EXPECT_FLOAT_EQ(10, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryPausedReturnTest, NaturalReverseEndpointReturnsToPausedCurrentPoseAndKeepsExactGate) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); ReverseTerminal(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_LT(rig.Secondary.GetNextFrame(.9765625f), 0); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 100, 12)); Main(rig); Pose(rig, 102, 100);
        rig.Character.Tick(1000); Gate(rig, *warp, 0); Main(rig); Pose(rig, 102, 100);
        ResumeAndFinish(rig, *warp, 100, 56); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryPausedReturnTest, ExplicitBoolStopFromRealPlayingCursorSamplesPausedMainWithoutForecastAdvance) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); Prepare(rig, 3);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        Playing(rig, 4); Stop(rig);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 140, 12)); Main(rig); Pose(rig, 102, 140);
        EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame()); ResumeAndFinish(rig, *warp, 140, 76);
        EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryPausedReturnTest, ExplicitModeStopCapturesTheActualPartialStartingPoseTowardPausedMain) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); Prepare(rig, 4, 1024, true);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        Starting(rig, *warp, 2, 140); Pose(rig, 102, 76); Stop(rig, true);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 76, 12)); Main(rig); Pose(rig, 102, 76);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); ResumeAndFinish(rig, *warp, 76, 44);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryPausedReturnTest, RepeatedStopsDoNotRecaptureThePausedNaturalReturnOrResetItsClock) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); PositiveTerminal(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        rig.Character.Tick(.9765625f); ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 200, 12));
        Stop(rig, false, 8); Stop(rig, true, 2); Gate(rig, *warp, 0); Curve(rig, *warp, 4, 200, 12);
        EXPECT_FLOAT_EQ(0, warp->GetLinkedFrameRate()); Pose(rig, 102, 200);
        // Public logical cursor control does not apply a pose; the already captured first remains200.
        rig.Secondary.SetCurrentStep(.4f); EXPECT_NEAR(4, rig.Secondary.GetFrame(), .0001f); Pose(rig, 102, 200);
        rig.Character.Tick(0); Gate(rig, *warp, 0); Curve(rig, *warp, 4, 200, 12); Pose(rig, 102, 200);
        ResumeAndFinish(rig, *warp, 200, 106); EXPECT_NEAR(4, rig.Secondary.GetFrame(), .0001f);
    }
}
TEST_F(CharacterSecondaryPausedReturnTest, FlushReleasesPausedNaturalReturnBeforeNextMainTickAppliesItsUnchangedPose) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); ReverseTerminal(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        rig.Character.Tick(.9765625f); ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 100, 12));
        rig.Character.Tick(1000); Gate(rig, *warp, 0); Main(rig); Pose(rig, 102, 100);
        rig.Character.FlushSecondaryAnimations(); Released(rig); Main(rig); Pose(rig, 102, 100);
        rig.Character.Tick(0); Main(rig); Pose(rig, 102, 12); SourceClock(rig, TRUE, 0); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryPausedReturnTest, PositiveMovingPrimaryKeepsItsExistingFutureFrameAndFixedReleasePolicy) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); PositiveTerminal(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        rig.Source().LinkToFrameRate(TRUE, 1024); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 4, 200, 17)); Main(rig, 3); Pose(rig, 103, 200);
        ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        Gate(rig, *warp, 2); Main(rig, 5); Pose(rig, 105, 108.5f);
        ASSERT_EQ(4.0f, warp->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        Gate(rig, *warp, 4); Main(rig, 7); Pose(rig, 107, 17);
        rig.Character.Tick(0); Gate(rig, *warp, 4); Main(rig, 7); Pose(rig, 107, 17);
        rig.Character.Tick(.9765625f); Released(rig); Main(rig, 8); Pose(rig, 108, 17);
        rig.Character.Tick(0); Main(rig, 8); Pose(rig, 108, 18); SourceClock(rig, TRUE, 1024);
    }
}
TEST_F(CharacterSecondaryPausedReturnTest, UnlinkedStoredZeroRateStillAdvancesPerCallAndIsNotTreatedAsPaused) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); PausedReturnRig rig(Context, types); PositiveTerminal(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        rig.Source().LinkToFrameRate(FALSE, 0); ASSERT_EQ(3.0f, rig.Source().GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 0, 4, 200, 17)); Main(rig, 3); Pose(rig, 103, 200);
        ASSERT_EQ(1.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); Gate(rig, *warp, 1); Main(rig, 4); Pose(rig, 104, 154.25f);
        rig.Character.Tick(0); Gate(rig, *warp, 2); Main(rig, 5); Pose(rig, 105, 108.5f);
        rig.Character.Tick(0); Gate(rig, *warp, 3); Main(rig, 6); Pose(rig, 106, 62.75f);
        rig.Character.Tick(0); Gate(rig, *warp, 4); Main(rig, 7); Pose(rig, 107, 17);
        ASSERT_GT(warp->GetNextFrame(0), 4); rig.Character.Tick(0); Released(rig); Main(rig, 8); Pose(rig, 108, 17);
        rig.Character.Tick(0); Main(rig, 9); Pose(rig, 109, 19); SourceClock(rig, FALSE, 0);
    }
}
} // namespace
