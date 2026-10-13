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
CK_CLASSID ReturnDerivedID = 0;
class ReturnDerived : public RCKKeyedAnimation {
public:
    explicit ReturnDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return ReturnDerivedID; }
};
class ReturnCharacter : public RCKCharacter {
public:
    explicit ReturnCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot(int index) const { return m_SecondaryAnimations[index]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *ReturnEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class SecondaryReturnRig {
public:
    ReturnCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation ExactRoot, ExactLimb, DerivedRoot, DerivedLimb, SecondaryLimb;
    RCKKeyedAnimation Exact, Secondary;
    ReturnDerived Derived;
    bool UseDerived;
    explicit SecondaryReturnRig(CKContext *context, bool derived)
        : Character(context), Root(context), Limb(context), ExactRoot(context), ExactLimb(context),
          DerivedRoot(context), DerivedLimb(context), SecondaryLimb(context), Exact(context),
          Secondary(context), Derived(context), UseDerived(derived) {
        Character.SetMoveableFlags((Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED) | VX_MOVEABLE_CHARACTERRENDERED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Limb.SetMoveableFlags(Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, ReturnEntity(Character));
        EXPECT_TRUE(Limb.SetParent(ReturnEntity(Root), TRUE));
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Limb)));
        Curve(Exact, ExactRoot, Root, 0, 0); Curve(Exact, ExactLimb, Limb, 10, 20);
        Curve(Derived, DerivedRoot, Root, 0, 0); Curve(Derived, DerivedLimb, Limb, 10, 20);
        Curve(Secondary, SecondaryLimb, Limb, 100, 200);
        Exact.SetLength(10); Derived.SetLength(10); Secondary.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Exact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Derived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Secondary));
        Primary().LinkToFrameRate(TRUE, 1024); Primary().SetCanBeInterrupt(FALSE);
        Secondary.LinkToFrameRate(TRUE, 1024); Primary().SetFrame(0);
        EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Primary()));
    }
    ~SecondaryReturnRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Primary() { return UseDerived ? static_cast<RCKKeyedAnimation &>(Derived) : Exact; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part, float first, float last) {
        channel.Set3dEntity(ReturnEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryReturnDirectionTest : public ::testing::Test {
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
        ReturnDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(ReturnDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(SecondaryReturnRig &rig, float limb) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
            &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix(), &rig.Limb.GetWorldMatrix(), &rig.Limb.GetLocalMatrix()};
        const float translations[] = {100, 100, 100, 0, 100 + limb, limb};
        for (int which = 0; which < 6; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0) expected = translations[which];
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0004f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(ReturnEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(ReturnEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void PrimaryState(SecondaryReturnRig &rig, float frame, bool active, CKAnimation *queue = nullptr) {
        EXPECT_EQ(active ? &rig.Primary() : nullptr, rig.Character.GetActiveAnimation());
        EXPECT_EQ(queue, rig.Character.GetNextActiveAnimation()); EXPECT_NEAR(frame, rig.Primary().GetFrame(), .0003f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Primary().GetCharacter());
        const VxVector &center = rig.Primary().GetRootVectorInternal();
        EXPECT_FLOAT_EQ(0, center.x); EXPECT_FLOAT_EQ(0, center.y); EXPECT_FLOAT_EQ(0, center.z);
    }
    static void WarmPrimary(SecondaryReturnRig &rig, float frame) {
        ASSERT_NEAR(frame, rig.Primary().GetNextFrame(frame * .9765625f), .00001f);
        rig.Character.Tick(frame * .9765625f); PrimaryState(rig, frame, true); Pose(rig, 10 + frame);
    }
    static void PlayExplicit(SecondaryReturnRig &rig, float primaryFrame, bool queueSelf = false) {
        WarmPrimary(rig, primaryFrame); rig.Primary().LinkToFrameRate(TRUE, -1024);
        if (queueSelf) {
            ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Primary(), CK_TRANSITION_TOSTART));
            ASSERT_EQ(&rig.Primary(), rig.Character.GetNextActiveAnimation());
        }
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 4, CKSECONDARYANIMATION_ONESHOT));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot(0).Mode);
        EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation()); Pose(rig, 140);
    }
    static void Stop(SecondaryReturnRig &rig, float length, bool modeOverload = false) {
        if (modeOverload) {
            rig.Secondary.SetSecondaryAnimationMode(CKSECONDARYANIMATION_DOWARP);
            ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, length));
        } else ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, TRUE, length));
    }
    static RCKKeyedAnimation *ReturnState(SecondaryReturnRig &rig, float duration, float first, float last) {
        EXPECT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        if (rig.Character.GetSecondaryAnimationsCount() != 1) return nullptr;
        const CKSecondaryAnimation &slot = rig.Character.Slot(0);
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, slot.Mode);
        RCKKeyedAnimation *warp = slot.Animation; EXPECT_NE(nullptr, warp); if (!warp) return nullptr;
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_NEAR(duration, warp->GetLength(), .00001f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), warp->GetCharacter());
        CKObjectAnimation *channel = warp->GetAnimation(ReturnEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration};
            const float values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value));
                EXPECT_NEAR(values[i], value.x, .0003f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
        return warp;
    }
    static void FinishLinked(SecondaryReturnRig &rig, float duration, float first, float last,
                             float endPrimaryFrame, bool activeAtEnd, float releaseFrame, bool activeAtRelease,
                             float firstReleasedPose) {
        RCKKeyedAnimation *warp = ReturnState(rig, duration, first, last); ASSERT_NE(nullptr, warp);
        EXPECT_TRUE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(1024, warp->GetLinkedFrameRate());
        ASSERT_NEAR(duration, warp->GetNextFrame(duration * .9765625f), .00001f);
        rig.Character.Tick(duration * .9765625f);
        PrimaryState(rig, endPrimaryFrame, activeAtEnd); EXPECT_NEAR(duration, warp->GetFrame(), .0002f);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation()); Pose(rig, last);
        rig.Character.Tick(0); ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); Pose(rig, last);
        rig.Character.Tick(.9765625f); EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation()); PrimaryState(rig, releaseFrame, activeAtRelease); Pose(rig, last);
        rig.Character.Tick(0); PrimaryState(rig, releaseFrame, activeAtRelease); Pose(rig, firstReleasedPose);
    }
    static void PrepareNatural(SecondaryReturnRig &rig, bool reverseSecondary, float requested = 2) {
        const float actual = requested < 1 ? 1 : requested;
        const float starting = reverseSecondary ? 0 : 8;
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, starting, flags, requested));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot(0).Mode);
        rig.Character.Tick(actual * .9765625f); rig.Character.Tick(.9765625f);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot(0).Mode);
        EXPECT_NEAR(reverseSecondary ? 1 : 9, rig.Secondary.GetFrame(), .0002f);
        rig.Secondary.LinkToFrameRate(TRUE, 0);
        const float current = actual + 1;
        ASSERT_NEAR(8, rig.Primary().GetNextFrame((8 - current) * .9765625f), .00001f);
        rig.Character.Tick((8 - current) * .9765625f); PrimaryState(rig, 8, true);
        EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation()); Pose(rig, reverseSecondary ? 110 : 190);
        rig.Primary().LinkToFrameRate(TRUE, -1024); rig.Secondary.LinkToFrameRate(TRUE, reverseSecondary ? -1024 : 1024);
    }
};

TEST_F(CharacterSecondaryReturnDirectionTest, ExplicitNegativeInteriorReturnUsesDirectionThroughBothStopOverloads) {
    for (int derived = 0; derived < 2; ++derived) for (int overload = 0; overload < 2; ++overload) {
        SCOPED_TRACE(derived);
        SCOPED_TRACE(overload);
        SecondaryReturnRig rig(Context, derived != 0);
        PlayExplicit(rig, 8); Stop(rig, 2, overload != 0); ReturnState(rig, 2, 140, 16);
        EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame()); PrimaryState(rig, 8, true); Pose(rig, 140);
        FinishLinked(rig, 2, 140, 16, 6, true, 5, true, 15);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, NegativeReturnAtExactZeroKeepsPrimaryEndpointUntilStrictCrossing) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PlayExplicit(rig, 2); Stop(rig, 2);
        FinishLinked(rig, 2, 140, 10, 0, true, 0, false, 10);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, NegativeUnderflowWithoutQueueClampsEndpointAndDoesNotInventLoop) {
    for (int derived = 0; derived < 2; ++derived) for (int interruptible = 0; interruptible < 2; ++interruptible) {
        SCOPED_TRACE(derived);
        SCOPED_TRACE(interruptible);
        SecondaryReturnRig rig(Context, derived != 0);
        PlayExplicit(rig, 1); rig.Primary().SetCanBeInterrupt(interruptible); Stop(rig, 2);
        // Completed inactive sources retain the logical previous cursor1.
        FinishLinked(rig, 2, 140, 10, 1, false, 1, false, 10);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, QueuedSelfReverseReturnPredictsOneRestartAcrossZero) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PlayExplicit(rig, 1, true); Stop(rig, 2);
        FinishLinked(rig, 2, 140, 19, 9, true, 8, true, 18);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, QueuedSelfExactNegativePeriodPredictsZeroRatherThanLength) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PlayExplicit(rig, 8, true); Stop(rig, 18);
        FinishLinked(rig, 18, 140, 10, 0, true, 0, false, 10);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, QueuedSelfLargeReverseOvershootClampsAfterOnlyOneRestart) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PlayExplicit(rig, 8, true); Stop(rig, 32);
        RCKKeyedAnimation *warp = rig.Character.Slot(0).Animation; ASSERT_NE(nullptr, warp);
        ASSERT_EQ(32.0f, warp->GetNextFrame(31.25f));
        FinishLinked(rig, 32, 140, 10, 0, true, 0, false, 10);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, NaturalReverseExpiryReturnsFromActualSecondaryZeroToNegativePrimaryFuture) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PrepareNatural(rig, true);
        RCKKeyedAnimation *retained = rig.Character.Slot(0).Animation;
        rig.Character.Tick(1.953125f); PrimaryState(rig, 6, true); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        EXPECT_EQ(retained, ReturnState(rig, 2, 100, 14)); Pose(rig, 100);
        FinishLinked(rig, 2, 100, 14, 4, true, 3, true, 13);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, NaturalForwardExpiryPreservesCurrentSecondarySourceAndPredictsNegativePrimary) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PrepareNatural(rig, false);
        RCKKeyedAnimation *retained = rig.Character.Slot(0).Animation;
        rig.Character.Tick(1.953125f); PrimaryState(rig, 6, true); EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f);
        EXPECT_EQ(retained, ReturnState(rig, 2, 190, 14)); Pose(rig, 190);
        FinishLinked(rig, 2, 190, 14, 4, true, 3, true, 13);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, UnlinkedNegativeStoredPrimaryRateKeepsPositiveFramePredictionAndCallBudget) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PlayExplicit(rig, 8);
        rig.Primary().LinkToFrameRate(FALSE, -1024); Stop(rig, 2);
        RCKKeyedAnimation *warp = ReturnState(rig, 2, 140, 20); ASSERT_NE(nullptr, warp); EXPECT_FALSE(warp->IsLinkedToFrameRate());
        rig.Character.Tick(0); PrimaryState(rig, 9, true); Pose(rig, 80);
        rig.Character.Tick(0); PrimaryState(rig, 10, true); Pose(rig, 20);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        rig.Character.Tick(0); EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
        PrimaryState(rig, 10, false); Pose(rig, 20); rig.Character.Tick(0); Pose(rig, 20);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, PositivePrimaryReturnRetainsExactLengthEndpointAndStrictGateRelease) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PlayExplicit(rig, 8);
        rig.Primary().LinkToFrameRate(TRUE, 1024); Stop(rig, 2);
        FinishLinked(rig, 2, 140, 20, 10, true, 10, false, 20);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, ExplicitHalfFrameRequestPredictsActualOneFrameTransitionDuration) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PlayExplicit(rig, 8); Stop(rig, .5f);
        FinishLinked(rig, 1, 140, 17, 7, true, 6, true, 16);
    }
}
TEST_F(CharacterSecondaryReturnDirectionTest, NaturalHalfFrameRequestPredictsActualOneFrameTransitionDuration) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); SecondaryReturnRig rig(Context, derived != 0); PrepareNatural(rig, true, .5f);
        rig.Character.Tick(1.953125f); PrimaryState(rig, 6, true); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        ReturnState(rig, 1, 100, 15); FinishLinked(rig, 1, 100, 15, 5, true, 4, true, 14);
    }
}
} // namespace
