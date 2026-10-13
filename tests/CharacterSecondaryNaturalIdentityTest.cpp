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
CK_CLASSID NaturalIdentityDerivedID = 0;
class NaturalIdentityDerived : public RCKKeyedAnimation {
public:
    explicit NaturalIdentityDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return NaturalIdentityDerivedID; }
};
class NaturalIdentityCharacter : public RCKCharacter {
public:
    explicit NaturalIdentityCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *IdentityEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class NaturalIdentityRig {
public:
    NaturalIdentityCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    NaturalIdentityDerived SourceDerived, TargetDerived;
    int Types;
    explicit NaturalIdentityRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, IdentityEntity(Character));
        EXPECT_TRUE(Limb.SetParent(IdentityEntity(Root), TRUE));
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
    ~NaturalIdentityRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(IdentityEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryNaturalIdentityTest : public ::testing::Test {
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
        NaturalIdentityDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(NaturalIdentityDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(NaturalIdentityRig &rig, float world, float limb) {
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
        EXPECT_EQ(IdentityEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(IdentityEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(NaturalIdentityRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Prepare(NaturalIdentityRig &rig, float warm, CKBOOL linked, float rate, bool reverseSecondary = false) {
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, reverseSecondary ? 0 : 8, flags, 2));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
        ASSERT_EQ(2.0f, starting->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        ASSERT_GT(starting->GetNextFrame(.9765625f), 2); rig.Character.Tick(.9765625f);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_FLOAT_EQ(reverseSecondary ? 1 : 9, rig.Secondary.GetFrame());
        rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_NEAR(warm, rig.Source().GetNextFrame(warm * .9765625f), .00001f); rig.Character.Tick(warm * .9765625f);
        State(rig, &rig.Source(), nullptr, warm, 0, -warm, -50); Pose(rig, 100 + warm, reverseSecondary ? 110 : 190);
        rig.Source().LinkToFrameRate(linked, rate); rig.Secondary.LinkToFrameRate(TRUE, reverseSecondary ? -512 : 512);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Secondary, CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), &rig.Secondary, warm, 0, -warm, -50);
        EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation());
        ASSERT_EQ(reverseSecondary ? -1.0f : 11.0f, rig.Secondary.GetNextFrame(3.90625f));
    }
    static void Curve(NaturalIdentityRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(IdentityEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(IdentityEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(IdentityEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(NaturalIdentityRig &rig, RCKKeyedAnimation *retained,
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
    static void Gate(NaturalIdentityRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(NaturalIdentityRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};


TEST_F(CharacterSecondaryNaturalIdentityTest, ForwardSecondaryExpiryCapturesCurrentPoseAndPredictsItAsReversePrimaryDestination) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); NaturalIdentityRig rig(Context, types); Prepare(rig, 5, TRUE, -1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_EQ(1.0f, rig.Source().GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 190, 105));
        State(rig, &rig.Source(), &rig.Secondary, 1, 0, -1, -50); EXPECT_FLOAT_EQ(9, rig.Secondary.GetFrame()); Pose(rig, 101, 190);
        rig.Character.Tick(.9765625f); State(rig, &rig.Source(), &rig.Secondary, 0, 0, 0, -50); Pose(rig, 100, 147.5f); Curve(rig, *retained, 2, 190, 105);
        rig.Character.Tick(.9765625f); State(rig, &rig.Secondary, nullptr, 0, 0, 0, -50); EXPECT_FLOAT_EQ(.5f, rig.Secondary.GetFrame());
        Pose(rig, 100, 105); Gate(rig, *retained, 2); Curve(rig, *retained, 2, 190, 105);
        rig.Character.Tick(0); EXPECT_FLOAT_EQ(.5f, rig.Secondary.GetFrame()); Pose(rig, 100, 105); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); Pose(rig, 100, 105);
        rig.Character.Tick(0); Pose(rig, 100, 110);
    }
}
TEST_F(CharacterSecondaryNaturalIdentityTest, ForwardPrimaryHandoffKeepsRootlessSecondaryEntryAndOriginalReturnClock) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); NaturalIdentityRig rig(Context, types); Prepare(rig, 5, TRUE, 1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_EQ(9.0f, rig.Source().GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 190, 105));
        State(rig, &rig.Source(), &rig.Secondary, 9, 0, -9, -50); Pose(rig, 109, 190);
        ASSERT_EQ(10.0f, rig.Source().GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Secondary, nullptr, 9, 0, -10, -50); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); Pose(rig, 110, 147.5f);
        rig.Character.Tick(.9765625f); EXPECT_FLOAT_EQ(.5f, rig.Secondary.GetFrame()); Pose(rig, 110, 105); Gate(rig, *retained, 2); Curve(rig, *retained, 2, 190, 105);
        rig.Character.Tick(0); Pose(rig, 110, 105); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); Pose(rig, 110, 105);
        rig.Character.Tick(0); Pose(rig, 110, 110);
    }
}
TEST_F(CharacterSecondaryNaturalIdentityTest, ReverseSecondaryExpirySamplesZeroBeforeQueuingItsLiteralZeroPrimaryEntry) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); NaturalIdentityRig rig(Context, types); Prepare(rig, 5, TRUE, -1024, true);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(3.90625f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 100, 100));
        State(rig, &rig.Source(), &rig.Secondary, 1, 0, -1, -50); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); Pose(rig, 101, 100);
        ASSERT_EQ(2.0f, retained->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        State(rig, &rig.Secondary, nullptr, 1, 0, 0, -50); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); Pose(rig, 100, 100); Gate(rig, *retained, 2);
        rig.Character.Tick(0); Pose(rig, 100, 100); Gate(rig, *retained, 2);
        ASSERT_LT(rig.Secondary.GetNextFrame(.9765625f), 0); rig.Character.Tick(.9765625f);
        Released(rig); State(rig, nullptr, nullptr, 1, 0, 0, -50); Pose(rig, 100, 100);
    }
}
TEST_F(CharacterSecondaryNaturalIdentityTest, MixedIdentityReturnFollowsCurrentPrimaryWithoutRecapturingTheNowActiveSecondary) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); NaturalIdentityRig rig(Context, types); Prepare(rig, 8.5f, FALSE, -2048);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_EQ(9.5f, rig.Source().GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        ASSERT_NE(nullptr, Return(rig, retained, FALSE, 2048, 2, 190, 19.5f));
        State(rig, &rig.Source(), &rig.Secondary, 9.5f, 0, -9.5f, -50); EXPECT_FLOAT_EQ(9, rig.Secondary.GetFrame()); Pose(rig, 109.5f, 190);
        ASSERT_EQ(10.5f, rig.Source().GetNextFrame(0)); ASSERT_EQ(1.0f, retained->GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Secondary, nullptr, 9.5f, 0, -10, -50); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); Pose(rig, 110, 145); Curve(rig, *retained, 2, 190, 100);
        ASSERT_EQ(2.0f, retained->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); Pose(rig, 110, 110); Gate(rig, *retained, 2); Curve(rig, *retained, 2, 190, 110);
        rig.Character.Tick(0); Released(rig); EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); Pose(rig, 110, 110);
        rig.Character.Tick(0); Pose(rig, 110, 110);
    }
}
TEST_F(CharacterSecondaryNaturalIdentityTest, ExactReverseZeroWithoutFutureHandoffKeepsCurrentPrimaryAndRealSecondaryQueue) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); NaturalIdentityRig rig(Context, types); Prepare(rig, 6, TRUE, -1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_EQ(2.0f, rig.Source().GetNextFrame(3.90625f)); rig.Character.Tick(3.90625f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 190, 10));
        State(rig, &rig.Source(), &rig.Secondary, 2, 0, -2, -50); Pose(rig, 102, 190);
        ASSERT_EQ(0.0f, rig.Source().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Secondary, 0, 0, 0, -50); Pose(rig, 100, 10); Gate(rig, *retained, 2);
        rig.Character.Tick(0); State(rig, &rig.Source(), &rig.Secondary, 0, 0, 0, -50); Pose(rig, 100, 10); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, &rig.Secondary, nullptr, 0, 0, 0, -50);
        EXPECT_FLOAT_EQ(.5f, rig.Secondary.GetFrame()); Pose(rig, 100, 10); rig.Character.Tick(0); Pose(rig, 100, 105);
    }
}
TEST_F(CharacterSecondaryNaturalIdentityTest, FractionalForwardExpiryCapturesRetainedCursorBeforeTheFirstBridgeResetsProgress) {
    for (int types = 0; types <= 2; types += 2) {
        SCOPED_TRACE(types); NaturalIdentityRig rig(Context, types); Prepare(rig, 1, TRUE, 0);
        ASSERT_EQ(9.75f, rig.Secondary.GetNextFrame(1.46484375f)); rig.Character.Tick(1.46484375f);
        State(rig, &rig.Source(), &rig.Secondary, 1, 0, -1, -50); EXPECT_FLOAT_EQ(9.75f, rig.Secondary.GetFrame()); Pose(rig, 101, 197.5f);
        rig.Source().LinkToFrameRate(TRUE, -1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_EQ(0.0f, rig.Source().GetNextFrame(.9765625f)); ASSERT_EQ(10.25f, rig.Secondary.GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        ASSERT_NE(nullptr, Return(rig, retained, TRUE, 1024, 2, 197.5f, 110));
        State(rig, &rig.Source(), &rig.Secondary, 0, 0, 0, -50); EXPECT_FLOAT_EQ(9.75f, rig.Secondary.GetFrame()); Pose(rig, 100, 197.5f);
        rig.Character.Tick(.9765625f); State(rig, &rig.Secondary, nullptr, 0, 0, 0, -50);
        EXPECT_FLOAT_EQ(.5f, rig.Secondary.GetFrame()); Pose(rig, 100, 153.75f); Curve(rig, *retained, 2, 197.5f, 110);
        rig.Character.Tick(.9765625f); EXPECT_FLOAT_EQ(1, rig.Secondary.GetFrame()); Pose(rig, 100, 110); Gate(rig, *retained, 2);
        rig.Character.Tick(0); Pose(rig, 100, 110); Gate(rig, *retained, 2);
        rig.Character.Tick(.9765625f); Released(rig); EXPECT_FLOAT_EQ(1.5f, rig.Secondary.GetFrame()); Pose(rig, 100, 110);
        rig.Character.Tick(0); Pose(rig, 100, 115);
    }
}
} // namespace
