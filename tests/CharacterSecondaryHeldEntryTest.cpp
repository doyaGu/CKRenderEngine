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
CK_CLASSID HeldEntryDerivedID = 0;
class HeldEntryDerived : public RCKKeyedAnimation {
public:
    explicit HeldEntryDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return HeldEntryDerivedID; }
};
class HeldEntryCharacter : public RCKCharacter {
public:
    explicit HeldEntryCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *HeldEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class HeldEntryRig {
public:
    HeldEntryCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    HeldEntryDerived SourceDerived, TargetDerived;
    int Types;
    explicit HeldEntryRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, HeldEntity(Character));
        EXPECT_TRUE(Limb.SetParent(HeldEntity(Root), TRUE));
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
    ~HeldEntryRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(HeldEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryHeldEntryTest : public ::testing::Test {
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
        HeldEntryDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(HeldEntryDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(HeldEntryRig &rig, float world, float limb) {
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
        EXPECT_EQ(HeldEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(HeldEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(HeldEntryRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Prepare(HeldEntryRig &rig, CKBOOL targetLinked, float targetRate, bool natural = false,
                        float warm = 10, bool queue = true) {
        rig.Secondary.LinkToFrameRate(TRUE, natural ? 1024 : 0);
        if (natural) {
            const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
                CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
            ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 0, flags, 2));
            RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
            ASSERT_EQ(2.0f, starting->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
            ASSERT_GT(starting->GetNextFrame(.9765625f), 2); rig.Character.Tick(.9765625f);
            ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
            EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); rig.Secondary.LinkToFrameRate(TRUE, 0);
        } else {
            ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 4, CKSECONDARYANIMATION_ONESHOT));
            ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode); EXPECT_FLOAT_EQ(4, rig.Secondary.GetFrame());
        }
        rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(warm, rig.Source().GetNextFrame(warm * .9765625f)); rig.Character.Tick(warm * .9765625f);
        State(rig, &rig.Source(), nullptr, warm, 0, -warm, -50); Pose(rig, 100 + warm, natural ? 110 : 140);
        EXPECT_FALSE(rig.Source().CanBeInterrupt()); EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation());
        rig.Target().LinkToFrameRate(targetLinked, targetRate);
        if (queue) ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), queue ? &rig.Target() : nullptr, warm, 0, -warm, -50);
    }
    static void Stop(HeldEntryRig &rig, bool mode = false) {
        if (mode) {
            rig.Secondary.SetSecondaryAnimationMode(CKSECONDARYANIMATION_DOWARP);
            ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, 2.0f));
        } else ASSERT_EQ(CK_OK, rig.Character.StopSecondaryAnimation(&rig.Secondary, TRUE, 2));
    }
    static void Curve(HeldEntryRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(HeldEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(HeldEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(HeldEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(HeldEntryRig &rig, RCKKeyedAnimation *retained,
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
    static void Gate(HeldEntryRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(HeldEntryRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};


TEST_F(CharacterSecondaryHeldEntryTest, ExplicitHeldEntryWholeAndSplitReachDifferentActualLinkedTargets) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig whole(Context, types), split(Context, types);
        Prepare(whole, TRUE, 512); Prepare(split, TRUE, 512); Stop(whole); Stop(split);
        RCKKeyedAnimation *a = whole.Character.Slot().Animation, *b = split.Character.Slot().Animation;
        ASSERT_NE(nullptr, Return(whole, a, TRUE, 1024, 2, 140, 20)); ASSERT_NE(nullptr, Return(split, b, TRUE, 1024, 2, 140, 20));
        State(whole, &whole.Source(), &whole.Target(), 10, 0, -10, -50); Pose(whole, 110, 140);
        ASSERT_EQ(2.0f, a->GetNextFrame(1.953125f)); whole.Character.Tick(1.953125f);
        State(whole, &whole.Target(), nullptr, 10, 0, -10, -50); Pose(whole, 110, 200); Gate(whole, *a, 2); Curve(whole, *a, 2, 140, 200);
        split.Character.Tick(.9765625f); State(split, &split.Target(), nullptr, 10, 0, -10, -50); Pose(split, 110, 170); Curve(split, *b, 2, 140, 200);
        split.Character.Tick(.9765625f); State(split, &split.Target(), nullptr, 10, .5f, -10, -51); Pose(split, 111, 201.5f); Gate(split, *b, 2); Curve(split, *b, 2, 140, 201.5f);
        EXPECT_NE(whole.Target().GetFrame(), split.Target().GetFrame());
    }
}
TEST_F(CharacterSecondaryHeldEntryTest, ModeStopZeroTimeEntryDoesNotConsumeTheLinkedMainBudget) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig rig(Context, types); Prepare(rig, TRUE, 512); Stop(rig, true);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 20));
        ASSERT_EQ(0.0f, warp->GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 140); Gate(rig, *warp, 0); Curve(rig, *warp, 2, 140, 200);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, .5f, -10, -51); Pose(rig, 111, 170.75f);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, 1, -10, -52); Pose(rig, 112, 203); Gate(rig, *warp, 2);
        rig.Character.Tick(0); Pose(rig, 112, 203); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 10, 1.5f, -10, -53); Pose(rig, 113, 204.5f);
        rig.Character.Tick(0); Pose(rig, 113, 204.5f);
    }
}
TEST_F(CharacterSecondaryHeldEntryTest, HeldEntryIntoNegativeLinkedTargetKeepsLiteralZeroAndStopsAtItsRealEndpoint) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig rig(Context, types); Prepare(rig, TRUE, -512); Stop(rig);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 20));
        State(rig, &rig.Source(), &rig.Target(), 10, 0, -10, -50); Pose(rig, 110, 140);
        rig.Character.Tick(1.953125f); State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 200); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 140, 200);
        rig.Character.Tick(0); Pose(rig, 110, 200); Gate(rig, *warp, 2);
        ASSERT_LT(rig.Target().GetNextFrame(.9765625f), 0); rig.Character.Tick(.9765625f);
        Released(rig); State(rig, nullptr, nullptr, 10, 0, -10, -50); Pose(rig, 110, 200); rig.Character.Tick(0); Pose(rig, 110, 200);
    }
}
TEST_F(CharacterSecondaryHeldEntryTest, ExistingMixedHeldEntryTracksUnlinkedCallsEvenAtZeroTime) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig rig(Context, types); Prepare(rig, FALSE, -2048); Stop(rig, true);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 20));
        rig.Character.Tick(0); State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 140); Gate(rig, *warp, 0);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, 1, -10, -52); Pose(rig, 112, 171.5f); Curve(rig, *warp, 2, 140, 203);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, 2, -10, -54); Pose(rig, 114, 206); Gate(rig, *warp, 2);
        rig.Character.Tick(0); State(rig, &rig.Target(), nullptr, 10, 3, -10, -56); Pose(rig, 116, 209); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 10, 4, -10, -58); Pose(rig, 118, 212);
    }
}
TEST_F(CharacterSecondaryHeldEntryTest, InterruptibleLengthUsesNormalRemainderAndKeepsFixedFutureReturn) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig rig(Context, types); Prepare(rig, TRUE, 512); rig.Source().SetCanBeInterrupt(TRUE); Stop(rig);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 203));
        State(rig, &rig.Source(), &rig.Target(), 10, 0, -10, -50); Pose(rig, 110, 140);
        ASSERT_EQ(11.0f, rig.Source().GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 10, .5f, -10, -51); Pose(rig, 111, 171.5f); Curve(rig, *warp, 2, 140, 203);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 10, 1, -10, -52); Pose(rig, 112, 203); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 10, 1.5f, -10, -53); Pose(rig, 113, 203);
        rig.Character.Tick(0); Pose(rig, 113, 204.5f);
    }
}
TEST_F(CharacterSecondaryHeldEntryTest, NaturalExpiryObservesTheAlreadyPromotedTargetAndItsOwnClock) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig rig(Context, types); Prepare(rig, TRUE, 512, true);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Secondary.LinkToFrameRate(TRUE, -2048);
        ASSERT_EQ(-1.0f, rig.Secondary.GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 512, 2, 100, 206));
        State(rig, &rig.Target(), nullptr, 10, 0, -10, -50); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); Pose(rig, 110, 100);
        ASSERT_EQ(2.0f, retained->GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        State(rig, &rig.Target(), nullptr, 10, 2, -10, -54); Pose(rig, 114, 206); Gate(rig, *retained, 2);
        rig.Character.Tick(0); Pose(rig, 114, 206); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Target(), nullptr, 10, 2.5f, -10, -55); Pose(rig, 115, 206);
        rig.Character.Tick(0); Pose(rig, 115, 207.5f);
    }
}
TEST_F(CharacterSecondaryHeldEntryTest, ExplicitHeldLengthWithoutQueueReturnsToTheDisplayedTerminalAfterMainStops) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig rig(Context, types); Prepare(rig, TRUE, 512, false, 10, false); Stop(rig, true);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 140, 20));
        State(rig, &rig.Source(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 140);
        rig.Character.Tick(.9765625f); State(rig, nullptr, nullptr, 10, 0, -10, -50); Pose(rig, 110, 80); Gate(rig, *warp, 1); Curve(rig, *warp, 2, 140, 20);
        rig.Character.Tick(.9765625f); Pose(rig, 110, 20); Gate(rig, *warp, 2);
        rig.Character.Tick(0); Pose(rig, 110, 20); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 110, 20); rig.Character.Tick(0); Pose(rig, 110, 20);
    }
}
TEST_F(CharacterSecondaryHeldEntryTest, NaturalExpiryAfterClampingUnqueuedMainLengthKeepsItsDisplayedTerminal) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); HeldEntryRig rig(Context, types); Prepare(rig, TRUE, 512, true, 9, false);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Secondary.LinkToFrameRate(TRUE, -2048);
        ASSERT_EQ(10.0f, rig.Source().GetNextFrame(.9765625f)); ASSERT_EQ(-1.0f, rig.Secondary.GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 100, 20));
        State(rig, &rig.Source(), nullptr, 10, 0, -10, -50); Pose(rig, 110, 100);
        rig.Character.Tick(.9765625f); State(rig, nullptr, nullptr, 10, 0, -10, -50); Pose(rig, 110, 60); Gate(rig, *retained, 1); Curve(rig, *retained, 2, 100, 20);
        rig.Character.Tick(.9765625f); Pose(rig, 110, 20); Gate(rig, *retained, 2);
        rig.Character.Tick(0); Pose(rig, 110, 20); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 110, 20); rig.Character.Tick(0); Pose(rig, 110, 20);
    }
}
} // namespace
