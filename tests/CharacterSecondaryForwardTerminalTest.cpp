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
CK_CLASSID ForwardTerminalDerivedID = 0;
class ForwardTerminalDerived : public RCKKeyedAnimation {
public:
    explicit ForwardTerminalDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return ForwardTerminalDerivedID; }
};
class ForwardTerminalCharacter : public RCKCharacter {
public:
    explicit ForwardTerminalCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *ForwardEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class ForwardTerminalRig {
public:
    ForwardTerminalCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    ForwardTerminalDerived SourceDerived, TargetDerived;
    int Types;
    explicit ForwardTerminalRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, ForwardEntity(Character));
        EXPECT_TRUE(Limb.SetParent(ForwardEntity(Root), TRUE));
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
    ~ForwardTerminalRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(ForwardEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryForwardTerminalTest : public ::testing::Test {
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
        ForwardTerminalDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(ForwardTerminalDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(ForwardTerminalRig &rig, float world, float limb) {
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
        EXPECT_EQ(ForwardEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(ForwardEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(ForwardTerminalRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Main(ForwardTerminalRig &rig) {
        State(rig, &rig.Source(), nullptr, 2, 0, -2, -50); SourceClock(rig, TRUE, 0);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Secondary.GetCharacter());
        EXPECT_EQ(nullptr, rig.Secondary.GetAnimation(ForwardEntity(rig.Root)));
    }
    static void Starting(ForwardTerminalRig &rig, RCKKeyedAnimation &warp, float phase) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); EXPECT_FLOAT_EQ(4, warp.GetLength());
        EXPECT_FLOAT_EQ(phase, warp.GetFrame()); EXPECT_EQ(1, warp.GetAnimationCount());
        CKObjectAnimation *child = warp.GetAnimation(0); ASSERT_NE(nullptr, child);
        EXPECT_EQ(ForwardEntity(rig.Limb), child->Get3dEntity());
        const float times[] = {0, 2, 4}, values[] = {12, 96, 180};
        for (int i = 0; i < 3; ++i) {
            VxVector value; EXPECT_TRUE(child->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
            EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
        }
        Main(rig);
    }
    static void Playing(ForwardTerminalRig &rig, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&rig.Secondary), rig.Limb.GetExclusiveAnimation());
        EXPECT_NEAR(frame, rig.Secondary.GetFrame(), .0003f); Main(rig); Pose(rig, 102, 100 + 10 * frame);
    }
    static void Prepare(ForwardTerminalRig &rig,
                        CKDWORD flags = CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP,
                        int loops = 0) {
        rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_FLOAT_EQ(2, rig.Source().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        rig.Source().LinkToFrameRate(TRUE, 0); Main(rig); Pose(rig, 102, 12);
        rig.Secondary.LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 8,
            static_cast<CK_SECONDARYANIMATION_FLAGS>(flags), 4, loops));
        if (flags & CKSECONDARYANIMATION_DOWARP) {
            RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, warp);
            Starting(rig, *warp, 0); Pose(rig, 102, 12);
            ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
            Starting(rig, *warp, 4); Pose(rig, 102, 180);
            ASSERT_GT(warp->GetNextFrame(.9765625f), 4); rig.Character.Tick(.9765625f);
        } else {
            Playing(rig, 8); ASSERT_FLOAT_EQ(9, rig.Secondary.GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        }
        Playing(rig, 9); EXPECT_EQ(static_cast<CKDWORD>(loops), rig.Character.Slot().LoopCountRemaining);
    }
    static void ResumeAndFinish(ForwardTerminalRig &rig, RCKKeyedAnimation &warp, float first, float midpoint) {
        // Only this private return clock is explicitly resumed; main remains atframe2.
        warp.LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(2.0f, warp.GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        Gate(rig, warp, 2); Main(rig); Pose(rig, 102, midpoint); Curve(rig, warp, 4, first, 12);
        ASSERT_EQ(4.0f, warp.GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        Gate(rig, warp, 4); Main(rig); Pose(rig, 102, 12);
        rig.Character.Tick(0); Gate(rig, warp, 4); Pose(rig, 102, 12);
        ASSERT_GT(warp.GetNextFrame(.9765625f), 4); rig.Character.Tick(.9765625f);
        Released(rig); Main(rig); Pose(rig, 102, 12); rig.Character.Tick(0); Pose(rig, 102, 12);
    }
    static void SourceClock(ForwardTerminalRig &rig, CKBOOL linked, float rate) {
        EXPECT_EQ(linked != FALSE, rig.Source().IsLinkedToFrameRate() != FALSE);
        EXPECT_FLOAT_EQ(rate, rig.Source().GetLinkedFrameRate());
    }
    static void Stop(ForwardTerminalRig &rig, bool mode = false, float duration = 4) {
        if (mode) {
            rig.Secondary.SetSecondaryAnimationMode(CKSECONDARYANIMATION_DOWARP);
            ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, duration));
        } else ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, TRUE, duration));
    }
    static void Curve(ForwardTerminalRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(ForwardEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(ForwardEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(ForwardEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(ForwardTerminalRig &rig, RCKKeyedAnimation *retained,
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
    static void Gate(ForwardTerminalRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(ForwardTerminalRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};

TEST_F(CharacterSecondaryForwardTerminalTest, OneShotDirectOvershootReturnsFromTheLastAppliedPoseWithoutJumpingToLength) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_FLOAT_EQ(11, rig.Secondary.GetNextFrame(1.953125f)); ASSERT_GT(rig.Secondary.GetNextFrame(1.953125f), 10);
        rig.Character.Tick(1.953125f); ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 190, 12));
        Main(rig); Pose(rig, 102, 190); EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f);
        rig.Character.Tick(1000); Gate(rig, *warp, 0); Pose(rig, 102, 190);
        ResumeAndFinish(rig, *warp, 190, 101); EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, ExactLengthWasActuallyAppliedBeforeStrictExpirySoReturnStartsAtItsPose) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_EQ(10.0f, rig.Secondary.GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        Playing(rig, 10); Pose(rig, 102, 200);
        ASSERT_GT(rig.Secondary.GetNextFrame(.9765625f), 10); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 200, 12)); EXPECT_FLOAT_EQ(10, rig.Secondary.GetFrame());
        Pose(rig, 102, 200); ResumeAndFinish(rig, *warp, 200, 106);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, LargeOneShotOvershootStillCapturesTheAppliedPoseOnceWithoutImplicitLoops) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_FLOAT_EQ(25, rig.Secondary.GetNextFrame(15.625f)); ASSERT_GT(rig.Secondary.GetNextFrame(15.625f), 10);
        rig.Character.Tick(15.625f); ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 190, 12));
        EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f); Main(rig); Pose(rig, 102, 190); Gate(rig, *warp, 0);
        ResumeAndFinish(rig, *warp, 190, 101);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, FiniteLoopUsesItsRealRemainingCountBeforeReturningFromTheFinalAppliedPose) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types);
        Prepare(rig, CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_DOWARP, 2);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        ASSERT_GT(rig.Secondary.GetNextFrame(1.953125f), 10); rig.Character.Tick(1.953125f);
        Playing(rig, 1); EXPECT_EQ(1u, rig.Character.Slot().LoopCountRemaining);
        for (int i = 2; i <= 9; ++i) {
            ASSERT_FLOAT_EQ(static_cast<float>(i), rig.Secondary.GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
            Playing(rig, static_cast<float>(i)); EXPECT_EQ(1u, rig.Character.Slot().LoopCountRemaining);
        }
        ASSERT_GT(rig.Secondary.GetNextFrame(1.953125f), 10); rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 190, 12)); EXPECT_EQ(0u, rig.Character.Slot().LoopCountRemaining);
        EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f); Pose(rig, 102, 190); ResumeAndFinish(rig, *warp, 190, 101);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, FiniteLastFrameSamplesLengthAndKeepsTheOriginalGateInsteadOfReturning) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types);
        Prepare(rig, CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_LASTFRAME | CKSECONDARYANIMATION_DOWARP, 1);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_GT(rig.Secondary.GetNextFrame(1.953125f), 10); rig.Character.Tick(1.953125f);
        Playing(rig, 10); EXPECT_EQ(0u, rig.Character.Slot().LoopCountRemaining);
        EXPECT_EQ(0u, rig.Character.Slot().Flags & CKSECONDARYANIMATION_LOOPNTIMES);
        EXPECT_NE(0u, rig.Character.Slot().Flags & CKSECONDARYANIMATION_LASTFRAME);
        EXPECT_EQ(retained, rig.Character.Slot().Animation);
        rig.Character.Tick(0); Playing(rig, 10);
        rig.Character.Tick(.9765625f); Playing(rig, 10); EXPECT_EQ(retained, rig.Character.Slot().Animation);
        rig.Character.FlushSecondaryAnimations(); Released(rig); Pose(rig, 102, 200);
        rig.Character.Tick(0); Main(rig); Pose(rig, 102, 12);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, InfiniteLoopAppliesItsRemainderAndRetainsExactLengthUntilStrictCrossing) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types); Prepare(rig, CKSECONDARYANIMATION_LOOP | CKSECONDARYANIMATION_DOWARP);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        rig.Character.Tick(1.953125f); Playing(rig, 1);
        for (int i = 2; i <= 10; ++i) {
            if (i == 10) ASSERT_EQ(10.0f, rig.Secondary.GetNextFrame(.9765625f));
            else ASSERT_FLOAT_EQ(static_cast<float>(i), rig.Secondary.GetNextFrame(.9765625f));
            rig.Character.Tick(.9765625f); Playing(rig, static_cast<float>(i));
        }
        ASSERT_GT(rig.Secondary.GetNextFrame(.9765625f), 10); rig.Character.Tick(.9765625f);
        Playing(rig, 1); EXPECT_EQ(retained, rig.Character.Slot().Animation);
        EXPECT_NE(0u, rig.Character.Slot().Flags & CKSECONDARYANIMATION_LOOP);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, ExplicitStopUsesTheCurrentPoseAndDoesNotCompleteTheOriginalClip) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); RCKKeyedAnimation *warp = rig.Character.Slot().Animation;
        Stop(rig); ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 4, 190, 12));
        EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f); Main(rig); Pose(rig, 102, 190);
        ResumeAndFinish(rig, *warp, 190, 101); EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, OrdinaryNoWarpExpiryClearsTheGateAndNextMainTickReappliesItsPose) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig rig(Context, types); Prepare(rig, CKSECONDARYANIMATION_ONESHOT);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_GT(rig.Secondary.GetNextFrame(1.953125f), 10); rig.Character.Tick(1.953125f);
        Released(rig); Main(rig); Pose(rig, 102, 190); EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f);
        rig.Character.Tick(0); Released(rig); Main(rig); Pose(rig, 102, 12);
    }
}
TEST_F(CharacterSecondaryForwardTerminalTest, DifferentTimePartitionsCaptureDifferentLastAppliedPosesWithoutForcingEquivalence) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); ForwardTerminalRig whole(Context, types), split(Context, types);
        Prepare(whole); ASSERT_FALSE(::testing::Test::HasFatalFailure());
        Prepare(split); ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *wholeWarp = whole.Character.Slot().Animation, *splitWarp = split.Character.Slot().Animation;
        whole.Character.Tick(1.953125f);
        ASSERT_EQ(10.0f, split.Secondary.GetNextFrame(.9765625f)); split.Character.Tick(.9765625f);
        Playing(split, 10); split.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(whole, wholeWarp, TRUE, 0, 4, 190, 12));
        ASSERT_NE(nullptr, Return(split, splitWarp, TRUE, 0, 4, 200, 12));
        EXPECT_NEAR(9, whole.Secondary.GetFrame(), .0002f); EXPECT_FLOAT_EQ(10, split.Secondary.GetFrame());
        Pose(whole, 102, 190); Pose(split, 102, 200);
        ResumeAndFinish(whole, *wholeWarp, 190, 101); ResumeAndFinish(split, *splitWarp, 200, 106);
    }
}
} // namespace
