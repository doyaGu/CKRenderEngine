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
#include "RCKMesh.h"
#include "RCKKeyframeData.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CompletionCharacter : public RCKCharacter {
public:
    explicit CompletionCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }

struct CompletionRig {
    CompletionCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation MainRoot, MainLimb, TargetRoot, TargetLimb, SecondaryLimb;
    RCKKeyedAnimation Main, Target, Secondary;
    explicit CompletionRig(CKContext *context)
        : Character(context), Root(context), Limb(context), MainRoot(context), MainLimb(context),
          TargetRoot(context), TargetLimb(context), SecondaryLimb(context), Main(context), Target(context), Secondary(context) {
        Character.SetMoveableFlags((Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED) | VX_MOVEABLE_CHARACTERRENDERED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Limb.SetMoveableFlags(Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, Entity(Character));
        EXPECT_TRUE(Limb.SetParent(Entity(Root), TRUE));
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Limb)));
        Curve(Main, MainRoot, Root, 10, 0, 10); Curve(Main, MainLimb, Limb, 10, 10, 20);
        Curve(Target, TargetRoot, Root, 20, 50, 90); Curve(Target, TargetLimb, Limb, 20, 200, 260);
        Curve(Secondary, SecondaryLimb, Limb, 10, 100, 200);
        Main.SetLength(10); Target.SetLength(20); Secondary.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Main)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Target));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Secondary));
        Main.SetCanBeInterrupt(FALSE); Target.SetCanBeInterrupt(FALSE);
        Main.LinkToFrameRate(TRUE, 1024); Target.LinkToFrameRate(TRUE, 1024); Secondary.LinkToFrameRate(TRUE, 1024);
        EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Main));
    }
    ~CompletionRig() { Character.FlushSecondaryAnimations(); Limb.SetCurrentMesh(nullptr); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE); }
    static void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
                      float length, float first, float last) {
        channel.Set3dEntity(Entity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};

class CharacterPlaybackCompletionTest : public ::testing::Test {
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
        CKCLASSREGISTERCID(RCKMesh, CKCID_OBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Warm(CompletionRig &rig, float frame = 2) {
        rig.Character.Tick(frame * .9765625f); EXPECT_NEAR(frame, rig.Main.GetFrame(), .0002f);
        rig.Main.LinkToFrameRate(TRUE, 0);
    }
    static void Pose(CompletionRig &rig, float world, float limb) {
        EXPECT_NEAR(world, rig.Character.GetWorldMatrix()[3][0], .0005f);
        EXPECT_NEAR(world, rig.Root.GetWorldMatrix()[3][0], .0005f);
        EXPECT_NEAR(limb, rig.Limb.GetLocalMatrix()[3][0], .0005f);
        EXPECT_NEAR(world + limb, rig.Limb.GetWorldMatrix()[3][0], .0005f);
        EXPECT_EQ(Entity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(Entity(rig.Root), rig.Limb.GetParent());
    }
    static RCKKeyedAnimation *Start(CompletionRig &rig, float frame = 8, float duration = 2) {
        EXPECT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, frame,
            static_cast<CK_SECONDARYANIMATION_FLAGS>(CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP), duration));
        if (rig.Character.GetSecondaryAnimationsCount() != 1) return nullptr;
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; EXPECT_NE(nullptr, warp);
        if (!warp) return nullptr;
        warp->LinkToFrameRate(TRUE, 1024);
        rig.Character.Tick(duration * .9765625f);
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        rig.Character.Tick(.9765625f);
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation());
        return warp;
    }
    static void ReturnCurve(CompletionRig &rig, RCKKeyedAnimation *warp, float first, float last) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); ASSERT_NE(nullptr, warp);
        EXPECT_EQ(warp, rig.Character.Slot().Animation);
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        CKObjectAnimation *channel = warp->GetAnimation(Entity(rig.Limb)); ASSERT_NE(nullptr, channel);
        VxVector a, b, c;
        ASSERT_TRUE(channel->EvaluatePosition(0, a)); ASSERT_TRUE(channel->EvaluatePosition(1, b));
        ASSERT_TRUE(channel->EvaluatePosition(2, c));
        EXPECT_NEAR(first, a.x, .0005f); EXPECT_NEAR((first + last) * .5f, b.x, .0005f); EXPECT_NEAR(last, c.x, .0005f);
    }
    static void Finish(CompletionRig &rig, RCKKeyedAnimation &warp, float world, float last) {
        warp.LinkToFrameRate(TRUE, 1024); rig.Character.Tick(1.953125f);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); EXPECT_FLOAT_EQ(2, warp.GetFrame()); Pose(rig, world, last);
        rig.Character.Tick(0); EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation());
        rig.Character.Tick(.9765625f); EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation()); Pose(rig, world, last);
    }
};

TEST_F(CharacterPlaybackCompletionTest, ZeroLengthSecondaryHasFiniteLoopAndLastFramePoliciesWithoutDividingByLength) {
    const CK_SECONDARYANIMATION_FLAGS flags[] = {CKSECONDARYANIMATION_ONESHOT, CKSECONDARYANIMATION_LOOP,
        CKSECONDARYANIMATION_LASTFRAME, CKSECONDARYANIMATION_LOOPNTIMES};
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(i); CompletionRig rig(Context); Warm(rig); rig.Secondary.SetLength(0);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 0, flags[i], 0, 2));
        Pose(rig, 102, 100); rig.Character.Tick(.9765625f); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        if (i == 0) {
            EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
            Pose(rig, 102, 100); rig.Character.Tick(0); Pose(rig, 102, 12);
        } else {
            ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation());
            Pose(rig, 102, 100);
            if (i == 3) {
                EXPECT_EQ(1u, rig.Character.Slot().LoopCountRemaining); rig.Character.Tick(.9765625f);
                EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); rig.Character.Tick(0); Pose(rig, 102, 12);
            } else { rig.Character.Tick(100); EXPECT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); Pose(rig, 102, 100); }
        }
    }
}

TEST_F(CharacterPlaybackCompletionTest, ZeroLengthSecondaryCanCompleteBothTransitionPhasesWithFinitePoses) {
    CompletionRig rig(Context); Warm(rig); rig.Secondary.SetLength(0);
    RCKKeyedAnimation *warp = Start(rig, 0); ASSERT_NE(nullptr, warp); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
    Pose(rig, 102, 100); rig.Character.Tick(.9765625f); ReturnCurve(rig, warp, 100, 12);
    ASSERT_FALSE(::testing::Test::HasFatalFailure()); EXPECT_FLOAT_EQ(0, warp->GetLinkedFrameRate());
    Finish(rig, *warp, 102, 12);
}

TEST_F(CharacterPlaybackCompletionTest, ZeroLengthQueuedMainConsumesOnceAndUsesItsInitialPoseForNaturalReturn) {
    CompletionRig rig(Context); Warm(rig); RCKKeyedAnimation *warp = Start(rig); ASSERT_NE(nullptr, warp);
    rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Main.LinkToFrameRate(TRUE, 1024);
    rig.Character.Tick(6.8359375f); ASSERT_NEAR(9, rig.Main.GetFrame(), .0002f);
    rig.Target.SetLength(0); ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target, CK_TRANSITION_TOSTART));
    rig.Secondary.LinkToFrameRate(TRUE, 1024); rig.Character.Tick(1.953125f);
    EXPECT_EQ(&rig.Target, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(0, rig.Target.GetFrame()); ReturnCurve(rig, warp, 190, 200); Pose(rig, 110, 190);
    ASSERT_FALSE(::testing::Test::HasFatalFailure()); rig.Character.Tick(.9765625f);
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); Pose(rig, 110, 195);
    rig.Character.Tick(.9765625f); Pose(rig, 110, 200);
    rig.Character.Tick(.9765625f); EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); Pose(rig, 110, 200);
}

TEST_F(CharacterPlaybackCompletionTest, PausedQueuedTargetRetainsLiteralEntryAndPausesTheNaturalReturnClock) {
    CompletionRig rig(Context); Warm(rig); RCKKeyedAnimation *warp = Start(rig); ASSERT_NE(nullptr, warp);
    rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Main.LinkToFrameRate(TRUE, 1024);
    rig.Character.Tick(6.8359375f); ASSERT_NEAR(9, rig.Main.GetFrame(), .0002f);
    rig.Target.LinkToFrameRate(TRUE, 0); ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target, CK_TRANSITION_TOSTART));
    rig.Secondary.LinkToFrameRate(TRUE, 1024); rig.Character.Tick(1.953125f);
    EXPECT_EQ(&rig.Target, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(0, rig.Target.GetFrame()); EXPECT_FLOAT_EQ(0, warp->GetLinkedFrameRate());
    ReturnCurve(rig, warp, 190, 200); Pose(rig, 110, 190);
    ASSERT_FALSE(::testing::Test::HasFatalFailure()); rig.Character.Tick(100); Pose(rig, 110, 190);
    EXPECT_FLOAT_EQ(0, warp->GetFrame()); Finish(rig, *warp, 110, 200);
    rig.Character.Tick(0); EXPECT_FLOAT_EQ(0, rig.Target.GetFrame()); Pose(rig, 110, 200);
}

TEST_F(CharacterPlaybackCompletionTest, MultiPeriodFiniteExpiryBuildsOnlyOneReturnFromTheAppliedPose) {
    CompletionRig rig(Context); Warm(rig);
    ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 8,
        static_cast<CK_SECONDARYANIMATION_FLAGS>(CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_DOWARP), 2, 3));
    RCKKeyedAnimation *warp = rig.Character.Slot().Animation; ASSERT_NE(nullptr, warp); warp->LinkToFrameRate(TRUE, 1024);
    rig.Character.Tick(1.953125f); rig.Character.Tick(.9765625f); EXPECT_NEAR(9, rig.Secondary.GetFrame(), .0002f);
    rig.Character.Tick(30.2734375f); EXPECT_EQ(0u, rig.Character.Slot().LoopCountRemaining);
    ReturnCurve(rig, warp, 190, 12); Pose(rig, 102, 190);
    ASSERT_FALSE(::testing::Test::HasFatalFailure()); EXPECT_FLOAT_EQ(0, warp->GetFrame()); Finish(rig, *warp, 102, 12);
}

TEST_F(CharacterPlaybackCompletionTest, RootBearingSecondaryWithoutMainAppliesItsOwnPoseAndPriorityGate) {
    CompletionRig rig(Context); ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(nullptr));
    RCKObjectAnimation secondaryRoot(Context); CompletionRig::Curve(rig.Secondary, secondaryRoot, rig.Root, 10, 100, 200);
    rig.Secondary.SetLength(10); ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 1, CKSECONDARYANIMATION_ONESHOT));
    EXPECT_EQ(&rig.Secondary, rig.Root.GetExclusiveAnimation()); EXPECT_NEAR(210, rig.Root.GetWorldMatrix()[3][0], .0005f);
    rig.Character.Tick(.9765625f); EXPECT_NEAR(2, rig.Secondary.GetFrame(), .0002f);
    // With no main, the previous root pose is transferred to the character
    // before the secondary publishes its next local pose.
    EXPECT_NEAR(210, rig.Character.GetWorldMatrix()[3][0], .0005f); EXPECT_NEAR(330, rig.Root.GetWorldMatrix()[3][0], .0005f);
    EXPECT_NEAR(120, rig.Limb.GetLocalMatrix()[3][0], .0005f); EXPECT_NEAR(450, rig.Limb.GetWorldMatrix()[3][0], .0005f);
    rig.Character.Tick(9.765625f); EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    EXPECT_NEAR(330, rig.Character.GetWorldMatrix()[3][0], .0005f);
    EXPECT_NEAR(330, rig.Root.GetWorldMatrix()[3][0], .0005f);
    EXPECT_NEAR(450, rig.Limb.GetWorldMatrix()[3][0], .0005f); secondaryRoot.Set3dEntity(nullptr);
}

TEST_F(CharacterPlaybackCompletionTest, SecondaryReturnInterpolatesRotationAndNonuniformScaleThroughTheExclusiveGate) {
    CompletionRig rig(Context);
    VxQuaternion identity(0, 0, 0, 1), quarter(0, 0, .707106781f, .707106781f);
    VxVector unit(1, 1, 1), expanded(3, 5, 7);
    rig.MainLimb.AddRotationKey(0, &identity); rig.MainLimb.AddScaleKey(0, &unit);
    rig.SecondaryLimb.AddRotationKey(0, &quarter); rig.SecondaryLimb.AddScaleKey(0, &expanded);
    Warm(rig); RCKKeyedAnimation *warp = Start(rig); ASSERT_NE(nullptr, warp);
    rig.Character.Tick(1.953125f); ReturnCurve(rig, warp, 190, 12);
    ASSERT_FALSE(::testing::Test::HasFatalFailure()); warp->LinkToFrameRate(TRUE, 1024); rig.Character.Tick(.9765625f);
    const VxMatrix &m = rig.Limb.GetLocalMatrix(); const float c = .707106781f;
    EXPECT_NEAR(2*c, m[0][0], .0006f); EXPECT_NEAR(-2*c, m[0][1], .0006f);
    EXPECT_NEAR(3*c, m[1][0], .0006f); EXPECT_NEAR(3*c, m[1][1], .0006f); EXPECT_NEAR(4, m[2][2], .0006f);
    Pose(rig, 102, 101); rig.Character.Tick(.9765625f);
    const VxMatrix &last = rig.Limb.GetLocalMatrix();
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) EXPECT_NEAR(row == col ? 1 : 0, last[row][col], .0006f);
    EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
    rig.Character.Tick(.9765625f); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation()); Pose(rig, 102, 12);
}

TEST_F(CharacterPlaybackCompletionTest, SecondaryMorphReturnPublishesInterpolatedMeshVerticesAndReleasesItsGate) {
    RCKMesh mesh(Context); CompletionRig rig(Context); ASSERT_TRUE(mesh.SetVertexCount(1));
    rig.Limb.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh));
    RCKObjectAnimation *channels[] = {&rig.MainLimb, &rig.SecondaryLimb};
    for (int i = 0; i < 2; ++i) {
        RCKMorphController *morph = static_cast<RCKMorphController *>(channels[i]->CreateController(CKANIMATION_MORPH_CONTROL));
        ASSERT_NE(nullptr, morph); morph->SetMorphVertexCount(1); ASSERT_EQ(1, morph->GetMorphVertexCount());
        for (int k = 0; k < 2; ++k) {
            CKMorphKey *key = static_cast<CKMorphKey *>(morph->GetKey(morph->AddKey(k ? 10.0f : 0.0f, FALSE)));
            ASSERT_NE(nullptr, key); key->PosArray[0].Set(i ? (k ? 200.0f : 100.0f) : (k ? 10.0f : 0.0f), 0, 0);
        }
    }
    Warm(rig); RCKKeyedAnimation *warp = Start(rig); ASSERT_NE(nullptr, warp);
    CKDWORD stride = 0; CKBYTE *vertices = mesh.GetModifierVertices(&stride); ASSERT_NE(nullptr, vertices);
    EXPECT_NEAR(190, reinterpret_cast<VxVector *>(vertices)->x, .0005f);
    rig.Character.Tick(1.953125f); ReturnCurve(rig, warp, 190, 12);
    ASSERT_FALSE(::testing::Test::HasFatalFailure()); EXPECT_NEAR(190, reinterpret_cast<VxVector *>(vertices)->x, .0005f);
    warp->LinkToFrameRate(TRUE, 1024); rig.Character.Tick(.9765625f);
    EXPECT_NEAR(96, reinterpret_cast<VxVector *>(vertices)->x, .0006f);
    rig.Character.Tick(.9765625f); EXPECT_NEAR(2, reinterpret_cast<VxVector *>(vertices)->x, .0005f);
    rig.Character.Tick(.9765625f); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    rig.Character.Tick(0); EXPECT_NEAR(2, reinterpret_cast<VxVector *>(vertices)->x, .0005f);
    rig.Limb.SetCurrentMesh(nullptr);
}

TEST_F(CharacterPlaybackCompletionTest, MergedSecondarySamplesItsReferencedCurvesWithoutAdvancingTheirCursors) {
    CompletionRig rig(Context); Warm(rig);
    CKKeyedAnimation *merged = CKKeyedAnimation::Cast(rig.Main.CreateMergedAnimation(&rig.Secondary, TRUE));
    ASSERT_NE(nullptr, merged); ASSERT_EQ(1, merged->GetAnimationCount());
    EXPECT_TRUE(merged->GetAnimation(0)->IsMerged()); merged->SetMergeFactor(.5f); merged->LinkToFrameRate(TRUE, 1024);
    ASSERT_EQ(CK_OK, rig.Character.AddAnimation(merged));
    ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(merged, 0, CKSECONDARYANIMATION_ONESHOT));
    Pose(rig, 102, 55); rig.Character.Tick(1.953125f); Pose(rig, 102, 66);
    EXPECT_NEAR(2, merged->GetFrame(), .0002f); EXPECT_NEAR(2, rig.Main.GetFrame(), .0002f);
    EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame()); EXPECT_EQ(merged, rig.Limb.GetExclusiveAnimation());
    rig.Character.Tick(8.7890625f); EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount());
    EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation()); Pose(rig, 102, 66);
    rig.Character.Tick(0); Pose(rig, 102, 12);
}
} // namespace
