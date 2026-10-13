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
CK_CLASSID RemainderDerivedID = 0;
class RemainderDerived : public RCKKeyedAnimation {
public:
    explicit RemainderDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return RemainderDerivedID; }
};
static CK3dEntity *RemainderEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class WarpRemainderRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, ExactRoot, ExactLimb, DerivedRoot, DerivedLimb;
    RCKKeyedAnimation Source, Exact;
    RemainderDerived Derived;
    bool UseDerived;
    explicit WarpRemainderRig(CKContext *context, bool derived)
        : Character(context), Root(context), Limb(context), SourceRoot(context), SourceLimb(context),
          ExactRoot(context), ExactLimb(context), DerivedRoot(context), DerivedLimb(context),
          Source(context), Exact(context), Derived(context), UseDerived(derived) {
        Character.SetMoveableFlags((Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED) | VX_MOVEABLE_CHARACTERRENDERED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Limb.SetMoveableFlags(Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, RemainderEntity(Character));
        EXPECT_TRUE(Limb.SetParent(RemainderEntity(Root), TRUE));
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Limb)));
        Curve(Source, SourceRoot, Root, 5, 15); Curve(Source, SourceLimb, Limb, 10, 20);
        Curve(Exact, ExactRoot, Root, 100, 120); Curve(Exact, ExactLimb, Limb, 20, 40);
        Curve(Derived, DerivedRoot, Root, 100, 120); Curve(Derived, DerivedLimb, Limb, 20, 40);
        Source.SetLength(10); Exact.SetLength(10); Derived.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Source)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Exact));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Derived));
        Source.LinkToFrameRate(TRUE, 1024); Source.SetCanBeInterrupt(TRUE);
        Target().SetCanBeInterrupt(FALSE); Source.SetFrame(0);
        EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source));
    }
    ~WarpRemainderRig() { Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE); }
    RCKKeyedAnimation &Target() { return UseDerived ? static_cast<RCKKeyedAnimation &>(Derived) : Exact; }
    RCKObjectAnimation &TargetRoot() { return UseDerived ? DerivedRoot : ExactRoot; }
    void Tick(float milliseconds) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(milliseconds);
    }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part, float first, float last) {
        channel.Set3dEntity(RemainderEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterWarpRemainderTest : public ::testing::Test {
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
        RemainderDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(RemainderDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(WarpRemainderRig &rig, float x, float limb) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
            &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix(), &rig.Limb.GetWorldMatrix(), &rig.Limb.GetLocalMatrix()};
        const float translations[] = {x, x, x, 0, x + limb, limb};
        for (int which = 0; which < 6; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0) expected = translations[which];
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0005f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(RemainderEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(RemainderEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
    static void ActiveTarget(WarpRemainderRig &rig, float frame, float world, float limb) {
        EXPECT_EQ(&rig.Target(), rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frame, rig.Target().GetFrame(), .0003f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Target().GetCharacter());
        EXPECT_EQ(&rig.TargetRoot(), rig.Target().GetRootAnimationInternal());
        const VxVector &center = rig.Target().GetRootVectorInternal();
        EXPECT_NEAR(-(100 + 2 * frame), center.x, .0003f); EXPECT_FLOAT_EQ(0, center.y); EXPECT_FLOAT_EQ(0, center.z);
        Pose(rig, world, limb);
    }
    static RCKKeyedAnimation *Build(WarpRemainderRig &rig, CKBOOL targetLinked, float targetFPS,
                                    CKDWORD mode = CK_TRANSITION_WARPSAMEPOS) {
        EXPECT_EQ(2.0f, rig.Source.GetNextFrame(1.953125f));
        rig.Tick(1.953125f); Pose(rig, 102, 12); EXPECT_FLOAT_EQ(2, rig.Source.GetFrame());
        rig.Target().LinkToFrameRate(targetLinked, targetFPS);
        EXPECT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), mode, 2));
        RCKKeyedAnimation *warp = static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper());
        EXPECT_NE(nullptr, warp); if (!warp) return nullptr;
        EXPECT_EQ(warp, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Target(), rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(2, warp->GetLength()); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        EXPECT_EQ(targetLinked != FALSE, warp->IsLinkedToFrameRate() != FALSE);
        EXPECT_FLOAT_EQ(targetFPS < 0 ? -targetFPS : targetFPS, warp->GetLinkedFrameRate());
        const float entry = mode == CK_TRANSITION_WARPSAMEPOS ? 2.0f : 0.0f;
        CKDWORD actualMode = 0; CKAnimation *in = nullptr, *out = nullptr; float from = -1, to = -1;
        rig.Character.GetWarperParameters(&actualMode, &in, &from, &out, &to);
        EXPECT_EQ(mode, actualMode); EXPECT_EQ(&rig.Source, in); EXPECT_EQ(&rig.Target(), out);
        EXPECT_FLOAT_EQ(2, from); EXPECT_FLOAT_EQ(entry, to); EXPECT_EQ(2, warp->GetAnimationCount());
        RCKBodyPart *parts[] = {&rig.Root, &rig.Limb};
        const float first[] = {7, 12}, last[] = {100 + 2 * entry, 20 + 2 * entry};
        for (int i = 0; i < 2; ++i) {
            CKObjectAnimation *channel = warp->GetAnimation(RemainderEntity(*parts[i])); EXPECT_NE(nullptr, channel);
            if (!channel) continue;
            VxVector a, b; EXPECT_TRUE(channel->EvaluatePosition(0, a)); EXPECT_TRUE(channel->EvaluatePosition(2, b));
            EXPECT_NEAR(first[i], a.x, .0002f); EXPECT_NEAR(last[i], b.x, .0002f);
            EXPECT_FLOAT_EQ(0, a.y); EXPECT_FLOAT_EQ(0, a.z); EXPECT_FLOAT_EQ(0, b.y); EXPECT_FLOAT_EQ(0, b.z);
        }
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), warp->GetCharacter()); Pose(rig, 102, 12);
        return warp;
    }
    static void StageOne(WarpRemainderRig &rig, RCKKeyedAnimation &warp) {
        warp.LinkToFrameRate(TRUE, 1024); ASSERT_EQ(1.0f, warp.GetNextFrame(.9765625f)); rig.Tick(.9765625f);
        EXPECT_EQ(&warp, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Target(), rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(1, warp.GetFrame()); EXPECT_NEAR(-55.5f, warp.GetRootVectorInternal().x, .0002f); Pose(rig, 150.5f, 18);
    }
    static void StageOneAndHalf(WarpRemainderRig &rig, RCKKeyedAnimation &warp) {
        warp.LinkToFrameRate(TRUE, 1024); ASSERT_NEAR(1.5f, warp.GetNextFrame(1.46484375f), .000002f); rig.Tick(1.46484375f);
        EXPECT_EQ(&warp, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Target(), rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(1.5f, warp.GetFrame(), .000002f); Pose(rig, 174.75f, 21);
    }
};

TEST_F(CharacterWarpRemainderTest, PositiveLinkedWarpConvertsRemainderToSignedOrPausedTargetRate) {
    const float rates[] = {512, -512, 0}, frames[] = {3, 1, 2};
    const float worlds[] = {201, 197, 199}, limbs[] = {26, 22, 24};
    for (int derived = 0; derived < 2; ++derived) for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(derived);
        SCOPED_TRACE(i);
        WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, TRUE, rates[i]); ASSERT_NE(nullptr, warp);
        warp->LinkToFrameRate(TRUE, 1024); ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Tick(3.90625f);
        ActiveTarget(rig, frames[i], worlds[i], limbs[i]);
    }
}
TEST_F(CharacterWarpRemainderTest, ReverseLinkedWarpPromotesPositiveTargetAtActualCachedEntry) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, TRUE, 512);
        ASSERT_NE(nullptr, warp); StageOne(rig, *warp); warp->LinkToFrameRate(TRUE, -1024);
        ASSERT_EQ(-1.0f, warp->GetNextFrame(1.953125f)); rig.Tick(1.953125f);
        ActiveTarget(rig, 2.5f, 103, 25);
    }
}
TEST_F(CharacterWarpRemainderTest, ReverseLinkedWarpConvertsRemainderToNegativeTargetWithoutRestartingItsEnd) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, TRUE, -512);
        ASSERT_NE(nullptr, warp); StageOne(rig, *warp); warp->LinkToFrameRate(TRUE, -1024);
        ASSERT_EQ(-1.0f, warp->GetNextFrame(1.953125f)); rig.Tick(1.953125f);
        ActiveTarget(rig, 1.5f, 101, 23);
    }
}
TEST_F(CharacterWarpRemainderTest, LinkedWarpToUnlinkedTargetSharesOnlyRemainingProcessFraction) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, FALSE, -2048);
        ASSERT_NE(nullptr, warp); StageOne(rig, *warp); ASSERT_EQ(3.0f, warp->GetNextFrame(1.953125f)); rig.Tick(1.953125f);
        ActiveTarget(rig, 2.5f, 200, 25); ASSERT_EQ(3.5f, rig.Target().GetNextFrame(0)); rig.Tick(0);
        ActiveTarget(rig, 3.5f, 202, 27);
    }
}
TEST_F(CharacterWarpRemainderTest, UnlinkedWarpToPositiveLinkedTargetUsesRemainingActualMilliseconds) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, TRUE, 512);
        ASSERT_NE(nullptr, warp); StageOneAndHalf(rig, *warp); warp->LinkToFrameRate(FALSE, -2048);
        ASSERT_NEAR(2.5f, warp->GetNextFrame(7.8125f), .000002f); rig.Tick(7.8125f);
        ActiveTarget(rig, 4, 203, 28);
    }
}
TEST_F(CharacterWarpRemainderTest, UnlinkedWarpToNegativeLinkedTargetUsesSignedBudgetAtLiteralEntry) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, TRUE, -512);
        ASSERT_NE(nullptr, warp); StageOneAndHalf(rig, *warp); warp->LinkToFrameRate(FALSE, -2048);
        ASSERT_NEAR(2.5f, warp->GetNextFrame(7.8125f), .000002f); rig.Tick(7.8125f);
        ActiveTarget(rig, 0, 195, 20);
    }
}
TEST_F(CharacterWarpRemainderTest, ZeroTimeUnlinkedWarpCrossingGivesLinkedTargetZeroRemainingTime) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, TRUE, 512);
        ASSERT_NE(nullptr, warp); StageOneAndHalf(rig, *warp); warp->LinkToFrameRate(FALSE, -2048);
        ASSERT_NEAR(2.5f, warp->GetNextFrame(0), .000002f); rig.Tick(0);
        ActiveTarget(rig, 2, 199, 24); rig.Tick(0); ActiveTarget(rig, 2, 199, 24);
    }
}
TEST_F(CharacterWarpRemainderTest, UnlinkedWarpCanFinishIntoPausedLinkedTargetWithoutAdvancingItsFrame) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, TRUE, 0);
        ASSERT_NE(nullptr, warp); StageOneAndHalf(rig, *warp); warp->LinkToFrameRate(FALSE, 0); rig.Tick(1000);
        ActiveTarget(rig, 2, 199, 24); rig.Tick(1000); ActiveTarget(rig, 2, 199, 24);
    }
}
TEST_F(CharacterWarpRemainderTest, BothUnlinkedIgnoreNegativeStoredRatesAndRetainSingleCallRemainder) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0); RCKKeyedAnimation *warp = Build(rig, FALSE, -512);
        ASSERT_NE(nullptr, warp); StageOneAndHalf(rig, *warp); warp->LinkToFrameRate(FALSE, -2048); rig.Tick(0);
        ActiveTarget(rig, 2.5f, 200, 25); rig.Tick(1000); ActiveTarget(rig, 3.5f, 202, 27);
    }
}
TEST_F(CharacterWarpRemainderTest, ExactWarpEndpointHandsOffCachedFrameWithNoSignedOrMixedRemainder) {
    const float rates[] = {512, -512, 0};
    for (int derived = 0; derived < 2; ++derived) for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(derived);
        SCOPED_TRACE(i);
        WarpRemainderRig linked(Context, derived != 0); RCKKeyedAnimation *warp = Build(linked, TRUE, rates[i]);
        ASSERT_NE(nullptr, warp); warp->LinkToFrameRate(TRUE, 1024); ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f));
        linked.Tick(1.953125f); ActiveTarget(linked, 2, 199, 24);
        WarpRemainderRig unlinked(Context, derived != 0); warp = Build(unlinked, TRUE, rates[i]); ASSERT_NE(nullptr, warp);
        StageOne(unlinked, *warp); warp->LinkToFrameRate(FALSE, -2048); ASSERT_EQ(2.0f, warp->GetNextFrame(0));
        unlinked.Tick(0); ActiveTarget(unlinked, 2, 199, 24);
    }
}
TEST_F(CharacterWarpRemainderTest, WarpStartNegativeTargetKeepsLiteralZeroRatherThanInventingReverseEndEntry) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig rig(Context, derived != 0);
        RCKKeyedAnimation *warp = Build(rig, TRUE, -512, CK_TRANSITION_WARPSTART); ASSERT_NE(nullptr, warp);
        warp->LinkToFrameRate(TRUE, 1024); ASSERT_EQ(4.0f, warp->GetNextFrame(3.90625f)); rig.Tick(3.90625f);
        ActiveTarget(rig, 0, 195, 20);
    }
}
TEST_F(CharacterWarpRemainderTest, BothLinkedWholeUpdateMatchesExactBoundarySplitThroughActualWarp) {
    for (int derived = 0; derived < 2; ++derived) {
        SCOPED_TRACE(derived); WarpRemainderRig whole(Context, derived != 0), split(Context, derived != 0);
        RCKKeyedAnimation *a = Build(whole, TRUE, 512), *b = Build(split, TRUE, 512); ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
        a->LinkToFrameRate(TRUE, 1024); b->LinkToFrameRate(TRUE, 1024);
        ASSERT_EQ(4.0f, a->GetNextFrame(3.90625f)); ASSERT_EQ(2.0f, b->GetNextFrame(1.953125f));
        whole.Tick(3.90625f); split.Tick(1.953125f); ActiveTarget(split, 2, 199, 24);
        ASSERT_EQ(3.0f, split.Target().GetNextFrame(1.953125f)); split.Tick(1.953125f);
        ActiveTarget(whole, 3, 201, 26); ActiveTarget(split, 3, 201, 26);
    }
}
} // namespace
