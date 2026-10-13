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
CK_CLASSID HeldStartDerivedID = 0;
class HeldStartDerived : public RCKKeyedAnimation {
public:
    explicit HeldStartDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return HeldStartDerivedID; }
};
class HeldStartCharacter : public RCKCharacter {
public:
    explicit HeldStartCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *StartEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class HeldStartRig {
public:
    HeldStartCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    HeldStartDerived SourceDerived, TargetDerived;
    int Types;
    explicit HeldStartRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, StartEntity(Character));
        EXPECT_TRUE(Limb.SetParent(StartEntity(Root), TRUE));
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
    ~HeldStartRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(StartEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryHeldStartTest : public ::testing::Test {
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
        HeldStartDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(HeldStartDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(HeldStartRig &rig, float world, float limb) {
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
        EXPECT_EQ(StartEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(StartEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(HeldStartRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Starting(HeldStartRig &rig, RCKKeyedAnimation &warp, float first, float step) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&warp, rig.Character.Slot().Animation);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); EXPECT_FLOAT_EQ(4, warp.GetLength());
        EXPECT_FLOAT_EQ(step, warp.GetFrame()); EXPECT_EQ(1, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(StartEntity(rig.Root)));
        CKObjectAnimation *channel = warp.GetAnimation(0); ASSERT_NE(nullptr, channel);
        EXPECT_EQ(StartEntity(rig.Limb), channel->Get3dEntity());
        const float times[] = {0, 2, 4}, values[] = {first, (first + 140) / 2, 140};
        for (int i = 0; i < 3; ++i) {
            VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value));
            EXPECT_NEAR(values[i], value.x, .0004f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
        }
    }
    static void Prepare(HeldStartRig &rig, CKBOOL linked = TRUE, float rate = 1024, bool queue = true,
                        bool exactStartingEnd = false) {
        rig.Source().LinkToFrameRate(TRUE, 1024); rig.Target().LinkToFrameRate(TRUE, 512);
        const float warm = exactStartingEnd ? 6.0f : 8.0f;
        ASSERT_FLOAT_EQ(warm, rig.Source().GetNextFrame(warm * .9765625f)); rig.Character.Tick(warm * .9765625f);
        State(rig, &rig.Source(), nullptr, warm, 0, -warm, -50); Pose(rig, 100 + warm, 10 + warm);
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 4, flags, 4));
        RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
        Starting(rig, *starting, 10 + warm, 0); Pose(rig, 100 + warm, 10 + warm);
        if (exactStartingEnd) {
            ASSERT_EQ(10.0f, rig.Source().GetNextFrame(3.90625f));
            ASSERT_EQ(4.0f, starting->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
            Starting(rig, *starting, 16, 4); Pose(rig, 110, 140);
        } else {
            ASSERT_EQ(9.0f, rig.Source().GetNextFrame(.9765625f));
            ASSERT_EQ(1.0f, starting->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
            Starting(rig, *starting, 18, 1); Pose(rig, 109, 48.5f);
            ASSERT_EQ(10.0f, rig.Source().GetNextFrame(.9765625f));
            ASSERT_EQ(2.0f, starting->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
            Starting(rig, *starting, 18, 2); Pose(rig, 110, 79);
        }
        State(rig, &rig.Source(), nullptr, 10, 0, -10, -50);
        EXPECT_FALSE(rig.Source().CanBeInterrupt()); rig.Source().LinkToFrameRate(linked, rate);
        if (queue) ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), queue ? &rig.Target() : nullptr, 10, 0, -10, -50); SourceClock(rig, linked, rate);
    }
    static void PrepareZeroLength(HeldStartRig &rig, bool queue) {
        // Keep the distinct original key times 0 and 10. Public length changes do not rescale keys.
        rig.Source().SetLength(0); rig.Source().SetFrame(0); rig.Source().CenterAnimation(0);
        rig.Source().LinkToFrameRate(TRUE, 1024); rig.Target().LinkToFrameRate(TRUE, 512);
        State(rig, &rig.Source(), nullptr, 0, 0, 0, -50); Pose(rig, 100, 10);
        EXPECT_FLOAT_EQ(0, rig.Source().GetStep());
        if (queue) {
            // A public normalized end step leaves a zero-duration clip at frame zero,
            // while avoiding SetNext's established immediate switch at step zero.
            rig.Source().SetStep(1); rig.Source().CenterAnimation(0);
            EXPECT_FLOAT_EQ(1, rig.Source().GetStep()); EXPECT_FLOAT_EQ(0, rig.Source().GetFrame());
            ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
            State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 10);
        }
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 4, flags, 4));
        RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
        Starting(rig, *starting, 10, 0); Pose(rig, 100, 10);
    }
    static void SourceClock(HeldStartRig &rig, CKBOOL linked, float rate) {
        EXPECT_EQ(linked != FALSE, rig.Source().IsLinkedToFrameRate() != FALSE);
        EXPECT_FLOAT_EQ(rate, rig.Source().GetLinkedFrameRate());
    }
    static void Stop(HeldStartRig &rig, bool mode = false, float duration = 2) {
        if (mode) {
            rig.Secondary.SetSecondaryAnimationMode(CKSECONDARYANIMATION_DOWARP);
            ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, duration));
        } else ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, TRUE, duration));
    }
    static void Curve(HeldStartRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(StartEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(StartEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(StartEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(HeldStartRig &rig, RCKKeyedAnimation *retained,
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
    static void Gate(HeldStartRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(HeldStartRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};

TEST_F(CharacterSecondaryHeldStartTest, LinkedHeldReturnCapturesAppliedStartingBlendAndRepeatedStopsKeepIt) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 79, 20));
        State(rig, &rig.Source(), &rig.Target(), 10, 0, -10, -50); Pose(rig, 110, 79);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 139.5f);
        Gate(rig, *warp, 1); Curve(rig, *warp, 2, 79, 200);
        Stop(rig, false, 8); Stop(rig, true, 4);
        Gate(rig, *warp, 1); Curve(rig, *warp, 2, 79, 200); Pose(rig, 110, 139.5f);
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 10, .5f, -10, -51); Pose(rig, 111, 201.5f); Gate(rig, *warp, 2);
        rig.Character.Tick(0); Pose(rig, 111, 201.5f); Gate(rig, *warp, 2);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 2); rig.Character.Tick(.9765625f);
        Released(rig); State(rig, &rig.Target(), nullptr, 10, 1, -10, -52); Pose(rig, 112, 203);
        rig.Character.Tick(0); Pose(rig, 112, 203); SourceClock(rig, TRUE, 1024); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, UnlinkedStoredNegativeHeldReturnUsesCurrentStartingPoseAndOneStepPerCall) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig, FALSE, -2048);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig, true);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 2048, 2, 79, 20));
        ASSERT_EQ(1.0f, warp->GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 139.5f); Gate(rig, *warp, 1);
        Stop(rig); Gate(rig, *warp, 1); Curve(rig, *warp, 2, 79, 200);
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 10, .5f, -10, -51); Pose(rig, 111, 201.5f); Gate(rig, *warp, 2);
        ASSERT_GT(warp->GetNextFrame(0), 2); rig.Character.Tick(0);
        Released(rig); Pose(rig, 111, 201.5f); rig.Character.Tick(0); Pose(rig, 111, 201.5f);
        SourceClock(rig, FALSE, -2048); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, PausedReturnRetainsPartialStartingPoseAndFlushDefersMainPoseApplication) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig, TRUE, 0);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 0, 2, 79, 20));
        ASSERT_EQ(0.0f, warp->GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 79); Gate(rig, *warp, 0);
        ASSERT_EQ(0.0f, warp->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        State(rig, &rig.Target(), nullptr, 10, 2, -10, -54); Pose(rig, 114, 79); Curve(rig, *warp, 2, 79, 206);
        Stop(rig, true, 8); Stop(rig, false, 4); Gate(rig, *warp, 0); Curve(rig, *warp, 2, 79, 206);
        EXPECT_FLOAT_EQ(0, warp->GetLinkedFrameRate()); SourceClock(rig, TRUE, 0); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        rig.Character.FlushSecondaryAnimations(); Released(rig); Pose(rig, 114, 79);
        rig.Character.Tick(0); Pose(rig, 114, 206); State(rig, &rig.Target(), nullptr, 10, 2, -10, -54);
    }
}
TEST_F(CharacterSecondaryHeldStartTest, MixedTargetEachCallRebuildsFromTheCapturedStartingBlend) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure()); rig.Target().LinkToFrameRate(FALSE, -2048);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig, true);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 79, 20));
        rig.Character.Tick(0); State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 79); Gate(rig, *warp, 0);
        ASSERT_EQ(1.0f, rig.Target().GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 10, 1, -10, -52); Pose(rig, 112, 141); Gate(rig, *warp, 1); Curve(rig, *warp, 2, 79, 203);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, 2, -10, -54);
        Pose(rig, 114, 206); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 79, 206);
        ASSERT_EQ(3.0f, rig.Target().GetNextFrame(0)); ASSERT_EQ(2.0f, warp->GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 10, 3, -10, -56); Pose(rig, 116, 209); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 79, 209);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 10, 4, -10, -58); Pose(rig, 118, 212);
        SourceClock(rig, TRUE, 1024); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, NoQueueReturnSamplesTheHeldTerminalAfterMainStops) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig, TRUE, 1024, false);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 79, 20));
        rig.Character.Tick(.9765625f); State(rig, nullptr, nullptr, 10, 0, -10, -50); Pose(rig, 110, 49.5f); Gate(rig, *warp, 1);
        Stop(rig, true, 7); Gate(rig, *warp, 1); Curve(rig, *warp, 2, 79, 20);
        rig.Character.Tick(.9765625f); Pose(rig, 110, 20); Gate(rig, *warp, 2);
        rig.Character.Tick(0); Pose(rig, 110, 20); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 110, 20);
        SourceClock(rig, TRUE, 1024); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, PublicZeroReturnDurationCapturesTheBlendAndUsesOneActualFrame) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig, false, 0);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 1, 79, 20));
        ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 200); Gate(rig, *warp, 1); Curve(rig, *warp, 1, 79, 200);
        rig.Character.Tick(0); Gate(rig, *warp, 1); Pose(rig, 110, 200);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 1); rig.Character.Tick(.9765625f);
        Released(rig); State(rig, &rig.Target(), nullptr, 10, .5f, -10, -51); Pose(rig, 111, 201.5f);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, StopWithoutWarpReleasesStartingGateWithoutResamplingEitherOriginalAnimation) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig, TRUE, 1024, false);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, FALSE, 0));
        Released(rig); State(rig, &rig.Source(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 79);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        rig.Character.Tick(0); State(rig, nullptr, nullptr, 10, 0, -10, -50); Pose(rig, 110, 79);
        SourceClock(rig, TRUE, 1024); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, ZeroLengthSourceHasFiniteInitialCaptureAndZeroRequestedReturnUsesOneFrame) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); PrepareZeroLength(rig, false);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig, true, 0);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 1, 10, 10));
        State(rig, &rig.Source(), nullptr, 0, 0, 0, -50); Pose(rig, 100, 10);
        rig.Character.Tick(0); State(rig, nullptr, nullptr, 0, 0, 0, -50); Pose(rig, 100, 10); Gate(rig, *warp, 0);
        ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f); Pose(rig, 100, 10); Gate(rig, *warp, 1);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 1); rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 100, 10);
        SourceClock(rig, TRUE, 1024); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, ExactStartingEndpointStillUsesItsDisplayedPoseWhenStopped) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); Prepare(rig, TRUE, 1024, true, true);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 20)); Pose(rig, 110, 140);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 170); Gate(rig, *warp, 1);
        Curve(rig, *warp, 2, 140, 200);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, .5f, -10, -51); Pose(rig, 111, 201.5f); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 10, 1, -10, -52); Pose(rig, 112, 203);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldStartTest, PublicNormalizedEndOfZeroDurationSourceQueuesARealHeldEntry) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldStartRig rig(Context, types); PrepareZeroLength(rig, true);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        EXPECT_FLOAT_EQ(1, rig.Source().GetStep()); EXPECT_FLOAT_EQ(0, rig.Source().GetFrame());
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; Stop(rig);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 10, 10));
        State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 10);
        ASSERT_EQ(1.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 0, 0, 0, -50); Pose(rig, 100, 105); Gate(rig, *warp, 1); Curve(rig, *warp, 2, 10, 200);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 0, .5f, 0, -51); Pose(rig, 101, 201.5f); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 0, 1, 0, -52); Pose(rig, 102, 203);
        EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    }
}
} // namespace
