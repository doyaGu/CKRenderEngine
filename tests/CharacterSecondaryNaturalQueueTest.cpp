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
CK_CLASSID NaturalQueueDerivedID = 0;
class NaturalQueueDerived : public RCKKeyedAnimation {
public:
    explicit NaturalQueueDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return NaturalQueueDerivedID; }
};
class NaturalQueueCharacter : public RCKCharacter {
public:
    explicit NaturalQueueCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *NaturalQueueEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class NaturalQueueRig {
public:
    NaturalQueueCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation ExactRoot, ExactLimb, DerivedRoot, DerivedLimb, SecondaryLimb;
    RCKKeyedAnimation Exact, Secondary;
    NaturalQueueDerived Derived;
    bool UseDerived;
    explicit NaturalQueueRig(CKContext *context, bool derived)
        : Character(context), Root(context), Limb(context), ExactRoot(context), ExactLimb(context),
          DerivedRoot(context), DerivedLimb(context), SecondaryLimb(context), Exact(context),
          Secondary(context), Derived(context), UseDerived(derived) {
        Character.SetMoveableFlags((Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED) | VX_MOVEABLE_CHARACTERRENDERED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Limb.SetMoveableFlags(Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, NaturalQueueEntity(Character));
        EXPECT_TRUE(Limb.SetParent(NaturalQueueEntity(Root), TRUE));
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Limb)));
        Curve(Exact, ExactRoot, Root, 0, 10); Curve(Exact, ExactLimb, Limb, 10, 20);
        Curve(Derived, DerivedRoot, Root, 0, 10); Curve(Derived, DerivedLimb, Limb, 10, 20);
        Curve(Secondary, SecondaryLimb, Limb, 100, 200);
        Exact.SetLength(10); Derived.SetLength(10); Secondary.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Exact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Derived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Secondary));
        Primary().LinkToFrameRate(TRUE, 0); Primary().SetCanBeInterrupt(FALSE);
        Secondary.LinkToFrameRate(TRUE, 1024); Primary().SetFrame(0);
        EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Primary()));
    }
    ~NaturalQueueRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Primary() { return UseDerived ? static_cast<RCKKeyedAnimation &>(Derived) : Exact; }
    RCKObjectAnimation &PrimaryRoot() { return UseDerived ? DerivedRoot : ExactRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part, float first, float last) {
        channel.Set3dEntity(NaturalQueueEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryNaturalQueueTest : public ::testing::Test {
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
        NaturalQueueDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(NaturalQueueDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(NaturalQueueRig &rig, float world, float limb) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
            &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix(), &rig.Limb.GetWorldMatrix(), &rig.Limb.GetLocalMatrix()};
        const float translations[] = {world, world, world, 0, world + limb, limb};
        for (int which = 0; which < 6; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0) expected = translations[which];
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0005f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(NaturalQueueEntity(rig.Character), rig.Root.GetParent());
        EXPECT_EQ(NaturalQueueEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void PrimaryState(NaturalQueueRig &rig, float frame, bool active, bool queued, float center) {
        EXPECT_EQ(active ? &rig.Primary() : nullptr, rig.Character.GetActiveAnimation());
        EXPECT_EQ(queued ? &rig.Primary() : nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frame, rig.Primary().GetFrame(), .0003f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Primary().GetCharacter());
        EXPECT_EQ(&rig.PrimaryRoot(), rig.Primary().GetRootAnimationInternal());
        const VxVector &cached = rig.Primary().GetRootVectorInternal();
        EXPECT_NEAR(center, cached.x, .0003f); EXPECT_FLOAT_EQ(0, cached.y); EXPECT_FLOAT_EQ(0, cached.z);
        CKAnimation *destination = nullptr; float entry = -1;
        rig.Character.GetWarperParameters(nullptr, nullptr, nullptr, &destination, &entry);
        EXPECT_EQ(queued ? &rig.Primary() : nullptr, destination); EXPECT_FLOAT_EQ(0, entry);
    }
    // The starting transition is completed while the main clock is paused.
    // Then the secondary is paused while a real main update establishes its pose.
    static void Prepare(NaturalQueueRig &rig, bool reverseSecondary, float requested, float warm,
                        CKBOOL linked = TRUE, float fps = -1024, bool queued = true) {
        const float duration = requested < 1 ? 1 : requested;
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, reverseSecondary ? 0 : 8, flags, requested));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
        ASSERT_EQ(duration, starting->GetNextFrame(duration * .9765625f));
        rig.Character.Tick(duration * .9765625f);
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        ASSERT_GT(starting->GetNextFrame(.9765625f), duration); rig.Character.Tick(.9765625f);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_FLOAT_EQ(reverseSecondary ? 1 : 9, rig.Secondary.GetFrame());
        rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Primary().LinkToFrameRate(TRUE, 1024);
        ASSERT_NEAR(warm, rig.Primary().GetNextFrame(warm * .9765625f), .00001f);
        rig.Character.Tick(warm * .9765625f);
        PrimaryState(rig, warm, true, false, -warm); Pose(rig, 100 + warm, reverseSecondary ? 110 : 190);
        rig.Primary().LinkToFrameRate(linked, fps);
        if (queued) {
            ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Primary(), CK_TRANSITION_TOSTART));
            PrimaryState(rig, warm, true, true, -warm);
        }
        rig.Secondary.LinkToFrameRate(TRUE, reverseSecondary ? -1024 : 1024);
        ASSERT_EQ(reverseSecondary ? -1.0f : 11.0f, rig.Secondary.GetNextFrame(1.953125f));
    }
    static RCKKeyedAnimation *Return(NaturalQueueRig &rig, RCKKeyedAnimation *retained,
                                    float duration, float sourcePose, float endpoint) {
        EXPECT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        if (rig.Character.GetSecondaryAnimationsCount() != 1) return nullptr;
        const CKSecondaryAnimation &slot = rig.Character.Slot();
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, slot.Mode);
        RCKKeyedAnimation *warp = slot.Animation; EXPECT_EQ(retained, warp); if (!warp) return nullptr;
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), warp->GetCharacter());
        EXPECT_FLOAT_EQ(duration, warp->GetLength()); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        // The primary has two output slots. A missing secondary root channel
        // clears its allocated slot instead of removing it from the list.
        EXPECT_EQ(2, warp->GetAnimationCount()); EXPECT_EQ(nullptr, warp->GetAnimation(NaturalQueueEntity(rig.Root)));
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
        int detached = 0, mappedLimb = 0;
        for (int i = 0; i < warp->GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp->GetAnimation(i); EXPECT_NE(nullptr, child);
            if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached;
                EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo());
                EXPECT_FALSE(child->HasMorphInfo());
            } else {
                ++mappedLimb;
                EXPECT_EQ(NaturalQueueEntity(rig.Limb), child->Get3dEntity());
                EXPECT_EQ(child, warp->GetAnimation(NaturalQueueEntity(rig.Limb)));
            }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mappedLimb);
        CKObjectAnimation *channel = warp->GetAnimation(NaturalQueueEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration};
            const float values[] = {sourcePose, (sourcePose + endpoint) / 2, endpoint};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value));
                EXPECT_NEAR(values[i], value.x, .0003f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
        return warp;
    }
    static void Finish(NaturalQueueRig &rig, RCKKeyedAnimation &warp, float duration, float endpoint,
                       float frame, bool active, bool queued, float center, float world,
                       float releaseFrame, bool releaseActive, float releaseCenter, float releaseWorld, float releasedLimb) {
        EXPECT_TRUE(warp.IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(1024, warp.GetLinkedFrameRate());
        ASSERT_EQ(duration, warp.GetNextFrame(duration * .9765625f)); rig.Character.Tick(duration * .9765625f);
        PrimaryState(rig, frame, active, queued, center); EXPECT_FLOAT_EQ(duration, warp.GetFrame());
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); Pose(rig, world, endpoint);
        ASSERT_EQ(duration, warp.GetNextFrame(0)); rig.Character.Tick(0);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); Pose(rig, world, endpoint);
        ASSERT_GT(warp.GetNextFrame(.9765625f), duration); rig.Character.Tick(.9765625f);
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
        PrimaryState(rig, releaseFrame, releaseActive, false, releaseCenter); Pose(rig, releaseWorld, endpoint);
        rig.Character.Tick(0); PrimaryState(rig, releaseFrame, releaseActive, false, releaseCenter);
        Pose(rig, releaseWorld, releasedLimb);
    }
};

TEST_F(CharacterSecondaryNaturalQueueTest, ReverseExpiryWithPendingSelfPredictsAndConsumesOnlyOneRestart) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 2, 3);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_EQ(1.0f, rig.Primary().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        PrimaryState(rig, 1, true, true, -1); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); Pose(rig, 101, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 19); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 2, 19, 9, true, false, -9, 99, 8, true, -8, 98, 18);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, ForwardExpiryRetainsItsCurrentSourcePoseAcrossNegativeSelfReturn) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, false, 2, 3);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        PrimaryState(rig, 1, true, true, -1); EXPECT_FLOAT_EQ(9, rig.Secondary.GetFrame()); Pose(rig, 101, 190);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 190, 19); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 2, 19, 9, true, false, -9, 99, 8, true, -8, 98, 18);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, ReverseExpiryWithoutQueueClampsFutureToZeroAndStopsMain) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 2, 3, TRUE, -1024, false);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        PrimaryState(rig, 1, true, false, -1); Pose(rig, 101, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 10); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 2, 10, 1, false, false, 0, 100, 1, false, 0, 100, 10);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, ForwardExpiryWithoutQueueUsesSameNegativeClampWithRetainedSecondaryCursor) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, false, 2, 3, TRUE, -1024, false);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        EXPECT_FLOAT_EQ(9, rig.Secondary.GetFrame()); PrimaryState(rig, 1, true, false, -1); Pose(rig, 101, 190);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 190, 10); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 2, 10, 1, false, false, 0, 100, 1, false, 0, 100, 10);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, ExactFutureZeroRetainsPendingSelfUntilTheNextStrictUnderflow) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 2, 4);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        PrimaryState(rig, 2, true, true, -2); ASSERT_EQ(0.0f, rig.Primary().GetNextFrame(1.953125f));
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 10); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 2, 10, 0, true, true, 0, 100, 9, true, -9, 99, 19);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, ExactNegativePeriodAfterOneSelfRestartPredictsZeroRatherThanLength) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 16, 8);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        PrimaryState(rig, 6, true, true, -6); ASSERT_EQ(-10.0f, rig.Primary().GetNextFrame(15.625f));
        RCKKeyedAnimation *warp = Return(rig, retained, 16, 100, 10); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 16, 10, 0, true, false, 0, 90, 0, false, 0, 90, 10);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, LargeReturnDurationAddsOnePendingPeriodThenClampsInsteadOfLoopingAgain) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 32, 10);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        PrimaryState(rig, 8, true, true, -8); ASSERT_EQ(-24.0f, rig.Primary().GetNextFrame(31.25f));
        RCKKeyedAnimation *warp = Return(rig, retained, 32, 100, 10); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 32, 10, 0, true, false, 0, 90, 0, false, 0, 90, 10);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, HalfFrameRequestForecastsTheActualOneFrameBuilderDuration) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, .5f, 2.5f);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        PrimaryState(rig, .5f, true, true, -.5f); Pose(rig, 100.5f, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 1, 100, 19.5f); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 1, 19.5f, 9.5f, true, false, -9.5f, 99.5f, 8.5f, true, -8.5f, 98.5f, 18.5f);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, ExpiryTickConsumesMainQueueBeforeReturnForecastAndDoesNotInventAnotherRestart) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 2, 1);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_EQ(-1.0f, rig.Primary().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        PrimaryState(rig, 9, true, false, -9); Pose(rig, 99, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 17); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 2, 17, 7, true, false, -7, 97, 6, true, -6, 96, 16);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, PositivePrimaryControlRetainsExactLengthAndStrictSecondaryGateRelease) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 2, 6, TRUE, 1024, false);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        PrimaryState(rig, 8, true, false, -8); Pose(rig, 108, 100);
        ASSERT_EQ(10.0f, rig.Primary().GetNextFrame(1.953125f));
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 20); ASSERT_NE(nullptr, warp);
        Finish(rig, *warp, 2, 20, 10, true, false, -10, 110, 10, false, -10, 110, 20);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, UnlinkedStoredNegativePrimaryUsesForwardCallBudgetAndUnlinkedReturn) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 2, 7, FALSE, -1024, false);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; ASSERT_EQ(8.0f, rig.Primary().GetNextFrame(1.953125f));
        rig.Character.Tick(1.953125f); PrimaryState(rig, 8, true, false, -8); Pose(rig, 108, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 20); ASSERT_NE(nullptr, warp);
        EXPECT_FALSE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(1024, warp->GetLinkedFrameRate());
        ASSERT_EQ(1.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); PrimaryState(rig, 9, true, false, -9); Pose(rig, 109, 60);
        ASSERT_EQ(2.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); PrimaryState(rig, 10, true, false, -10); Pose(rig, 110, 20);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        ASSERT_GT(warp->GetNextFrame(0), 2); rig.Character.Tick(0);
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
        PrimaryState(rig, 10, false, false, -10); Pose(rig, 110, 20); rig.Character.Tick(0); Pose(rig, 110, 20);
    }
}
TEST_F(CharacterSecondaryNaturalQueueTest, PausedLinkedPrimaryRetainsCurrentPoseAndItsZeroRateReturnClock) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); NaturalQueueRig rig(Context, derived != 0); Prepare(rig, true, 2, 4, TRUE, 0, false);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; ASSERT_EQ(4.0f, rig.Primary().GetNextFrame(1.953125f));
        rig.Character.Tick(1.953125f); PrimaryState(rig, 4, true, false, -4); Pose(rig, 104, 100);
        // Only the private return is resumed below; the main stays at frame4.
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 14); ASSERT_NE(nullptr, warp);
        EXPECT_TRUE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(0, warp->GetLinkedFrameRate());
        ASSERT_EQ(0.0f, warp->GetNextFrame(1000)); rig.Character.Tick(1000);
        PrimaryState(rig, 4, true, false, -4); Pose(rig, 104, 100);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        warp->LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        PrimaryState(rig, 4, true, false, -4); Pose(rig, 104, 57);
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        PrimaryState(rig, 4, true, false, -4); Pose(rig, 104, 14);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); EXPECT_FLOAT_EQ(2, warp->GetFrame());
        rig.Character.Tick(0); ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); Pose(rig, 104, 14);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 2); rig.Character.Tick(.9765625f);
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
        PrimaryState(rig, 4, true, false, -4); Pose(rig, 104, 14);
        rig.Character.Tick(0); PrimaryState(rig, 4, true, false, -4); Pose(rig, 104, 14);
        EXPECT_TRUE(rig.Primary().IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(0, rig.Primary().GetLinkedFrameRate());
    }
}
} // namespace
