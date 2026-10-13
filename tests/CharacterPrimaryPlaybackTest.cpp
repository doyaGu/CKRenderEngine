#include <gtest/gtest.h>
#include <cmath>
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
CK_CLASSID PrimaryDerivedID = 0;

class PrimaryCharacter : public RCKCharacter {
public:
    explicit PrimaryCharacter(CKContext *context) : RCKCharacter(context) {}
    CKDWORD EntityFlags() const { return m_3dEntityFlags; }
    void EntityFlags(CKDWORD flags) { m_3dEntityFlags = flags; }
    void Parameters(CKDWORD mode, RCKAnimation *source, float from, RCKAnimation *destination, float to) {
        m_TransitionMode = mode; m_AnimSrc = source; m_FrameSrc = from;
        m_AnimDest = destination; m_FrameDest = to;
    }
};

class PrimaryDerived : public RCKKeyedAnimation {
public:
    explicit PrimaryDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return PrimaryDerivedID; }
};

class PrimaryInterruptPolicy : public RCKAnimation {
public:
    explicit PrimaryInterruptPolicy(CKContext *context) : RCKAnimation(context) {}
    CKDWORD GetFlags() override { return RCKAnimation::GetFlags() | CKANIMATION_CANBEBREAK; }
};

static CK3dEntity *EntityAPI(RCK3dEntity *object) { return reinterpret_cast<CK3dEntity *>(object); }
static CKBodyPart *PartAPI(RCKBodyPart *object) { return reinterpret_cast<CKBodyPart *>(object); }
static void PositionCurve(RCKObjectAnimation &channel, RCK3dEntity &entity, float first, float last) {
    channel.Set3dEntity(EntityAPI(&entity));
    VxVector a(first, 0, 0), b(last, 0, 0);
    channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b); channel.SetLength(10);
}
static void ExpectMatrix(const VxMatrix &expected, const VxMatrix &actual) {
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            EXPECT_NEAR(expected[row][col], actual[row][col], 0.0002f) << row << ',' << col;
}
static void Position(RCK3dEntity &entity, float x) {
    VxVector value(x, 0, 0); entity.SetPosition(&value);
}
static float Position(RCK3dEntity &entity) {
    VxVector value; entity.GetPosition(&value); return value.x;
}

class PrimaryRig {
public:
    PrimaryCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation InChannel, OutChannel;
    RCKKeyedAnimation In, Out;
    explicit PrimaryRig(CKContext *context)
        : Character(context), Root(context), InChannel(context), OutChannel(context), In(context), Out(context) {
        Character.SetMoveableFlags((Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED) | VX_MOVEABLE_CHARACTERRENDERED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1.0f); Position(Character, 10);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(PartAPI(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, EntityAPI(&Character));
        PositionCurve(InChannel, Root, 0, 10); PositionCurve(OutChannel, Root, 20, 40);
        EXPECT_EQ(CK_OK, In.AddAnimation(&InChannel)); EXPECT_EQ(CK_OK, Out.AddAnimation(&OutChannel));
        In.SetLength(10); Out.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&In)); EXPECT_EQ(CK_OK, Character.AddAnimation(&Out));
        In.SetCanBeInterrupt(TRUE); Out.SetCanBeInterrupt(TRUE);
        In.SetStep(.4f); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&In));
    }
    ~PrimaryRig() { Root.SetParent(nullptr); }
};

class CharacterPrimaryPlaybackTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        PrimaryDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(PrimaryDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void ExpectWarp(PrimaryRig &rig, float sourceFrame, float targetFrame, CKDWORD mode) {
        CKDWORD actualMode = 0; CKAnimation *source = nullptr, *destination = nullptr;
        float from = -100, to = -100;
        rig.Character.GetWarperParameters(&actualMode, &source, &from, &destination, &to);
        EXPECT_EQ(mode, actualMode); EXPECT_EQ(&rig.In, source); EXPECT_FLOAT_EQ(sourceFrame, from);
        EXPECT_EQ(&rig.Out, destination); EXPECT_FLOAT_EQ(targetFrame, to);
        ASSERT_EQ(rig.Character.GetWarper(), rig.Character.GetActiveAnimation());
        ASSERT_EQ(&rig.Out, rig.Character.GetNextActiveAnimation());
        RCKKeyedAnimation *warp = static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper());
        ASSERT_NE(nullptr, warp); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        CKObjectAnimation *channel = warp->GetAnimation(EntityAPI(&rig.Root)); ASSERT_NE(nullptr, channel);
        VxVector first, last; ASSERT_TRUE(channel->EvaluatePosition(0, first));
        ASSERT_TRUE(channel->EvaluatePosition(warp->GetLength(), last));
        EXPECT_NEAR(sourceFrame, first.x, .0001f); EXPECT_NEAR(20 + targetFrame * 2, last.x, .0001f);
        EXPECT_NEAR(0, rig.Root.GetLocalMatrix()[3][0], .0001f);
    }
};

TEST_F(CharacterPrimaryPlaybackTest, ForcedActiveKeepsBorrowedIdentityQueueFrameAndOwnership) {
    PrimaryCharacter character(Context); RCKAnimation foreign(Context), queued(Context);
    foreign.SetLength(10); foreign.SetFrame(3); ASSERT_EQ(CK_OK, character.AddAnimation(&queued));
    ASSERT_EQ(CK_OK, character.SetNextActiveAnimation(&queued, 0));
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(&foreign));
    EXPECT_EQ(&foreign, character.GetActiveAnimation()); EXPECT_EQ(&queued, character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(3, foreign.GetFrame()); EXPECT_EQ(nullptr, foreign.GetCharacter());
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(nullptr));
    EXPECT_EQ(nullptr, character.GetActiveAnimation()); EXPECT_EQ(&queued, character.GetNextActiveAnimation());
}

TEST_F(CharacterPrimaryPlaybackTest, NextRejectsForeignOwnerWithoutEditingExistingSelection) {
    PrimaryRig rig(Context); RCKAnimation foreign(Context);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_TOSTART));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, rig.Character.SetNextActiveAnimation(&foreign, CK_TRANSITION_FROMNOW));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Out, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(.4f, rig.In.GetStep()); EXPECT_FLOAT_EQ(4, rig.Root.GetLocalMatrix()[3][0]);
}

TEST_F(CharacterPrimaryPlaybackTest, NullNextClearsQueueAndOnlyInterruptibleActive) {
    PrimaryRig rig(Context); rig.In.SetCanBeInterrupt(FALSE);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_TOSTART));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, rig.Character.SetNextActiveAnimation(nullptr, 0));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    rig.In.SetCanBeInterrupt(TRUE);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, rig.Character.SetNextActiveAnimation(nullptr, 0));
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
    PrimaryInterruptPolicy policy(Context); policy.SetCanBeInterrupt(FALSE);
    rig.Character.SetActiveAnimation(&policy);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, rig.Character.SetNextActiveAnimation(nullptr, 0));
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
}

TEST_F(CharacterPrimaryPlaybackTest, FirstQueuedKeyedRewindsAndCentersActualRootPose) {
    PrimaryRig rig(Context); rig.Character.SetActiveAnimation(nullptr);
    rig.Out.CenterAnimation(5); rig.Out.SetStep(.8f);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, 0));
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Out, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(0, rig.Out.GetFrame()); EXPECT_NEAR(0, rig.Root.GetLocalMatrix()[3][0], .0001f);
}

TEST_F(CharacterPrimaryPlaybackTest, LoopEqualRequiresStepStrictlyAbovePointThreeWithoutRewinding) {
    PrimaryRig rig(Context); rig.In.SetStep(.3f);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.In, CK_TRANSITION_LOOPIFEQUAL));
    EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    rig.In.SetStep(.31f);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.In, CK_TRANSITION_LOOPIFEQUAL));
    EXPECT_EQ(&rig.In, rig.Character.GetNextActiveAnimation()); EXPECT_FLOAT_EQ(.31f, rig.In.GetStep());
}

TEST_F(CharacterPrimaryPlaybackTest, ImmediateSameAnimationQueuesRestartWithoutChangingCurrentPose) {
    PrimaryRig rig(Context);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.In, CK_TRANSITION_FROMNOW));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.In, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(4, rig.In.GetFrame()); EXPECT_FLOAT_EQ(4, rig.Root.GetLocalMatrix()[3][0]);
}

TEST_F(CharacterPrimaryPlaybackTest, ImmediateDifferentAnimationConsumesRootMotionAndCentersDestination) {
    PrimaryRig rig(Context);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_FROMNOW));
    EXPECT_EQ(&rig.Out, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(0, rig.Out.GetFrame()); EXPECT_NEAR(14, Position(rig.Character), .0001f);
    EXPECT_NEAR(14, Position(rig.Root), .0001f); EXPECT_NEAR(0, rig.Root.GetLocalMatrix()[3][0], .0001f);
    VxVector raw; ASSERT_TRUE(rig.OutChannel.EvaluatePosition(0, raw)); EXPECT_FLOAT_EQ(20, raw.x);
}

TEST_F(CharacterPrimaryPlaybackTest, ZeroStepSwitchesImmediatelyEvenWhenUnbreakableAndToStart) {
    PrimaryRig rig(Context); rig.In.SetCanBeInterrupt(FALSE); rig.In.SetStep(0);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_TOSTART));
    EXPECT_EQ(&rig.Out, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
}

TEST_F(CharacterPrimaryPlaybackTest, UnbreakableToStartQueuesWithoutConsumingPose) {
    PrimaryRig rig(Context); rig.In.SetCanBeInterrupt(FALSE);
    VxMatrix before = rig.Root.GetWorldMatrix();
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_TOSTART));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Out, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(4, rig.In.GetFrame()); ExpectMatrix(before, rig.Root.GetWorldMatrix());
}

TEST_F(CharacterPrimaryPlaybackTest, FromAnimationUsesStoredModeAndZeroStoredModeKeepsRequestedBits) {
    PrimaryRig rig(Context); rig.Out.SetTransitionMode(CK_TRANSITION_FROMNOW);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_FROMANIMATION | CK_TRANSITION_TOSTART));
    EXPECT_EQ(&rig.Out, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    rig.Character.SetActiveAnimation(&rig.In); rig.In.SetStep(.4f); rig.Out.SetTransitionMode(static_cast<CK_ANIMATION_TRANSITION_MODE>(0));
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_FROMANIMATION | CK_TRANSITION_TOSTART));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Out, rig.Character.GetNextActiveAnimation());
}

TEST_F(CharacterPrimaryPlaybackTest, InterruptibleWarpBuildsActualCurveAndReportsItsSourceAndTarget) {
    PrimaryRig rig(Context);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
    ExpectWarp(rig, 4, 0, CK_TRANSITION_WARPSTART);
    RCKKeyedAnimation *warp = static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper());
    warp->SetStep(1); EXPECT_NEAR(16, rig.Root.GetLocalMatrix()[3][0], .0001f);
    const float previousFrame = warp->GetFrame();
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.In, CK_TRANSITION_WARPSTART, 5));
    CKAnimation *source = nullptr; float from = -1;
    rig.Character.GetWarperParameters(nullptr, &source, &from, nullptr, nullptr);
    EXPECT_EQ(warp, source); EXPECT_FLOAT_EQ(previousFrame, from);
    EXPECT_FLOAT_EQ(5, warp->GetLength());
}

TEST_F(CharacterPrimaryPlaybackTest, UnbreakableWarpDoesNothingBeforeEndAndReportsParametersAtEnd) {
    PrimaryRig rig(Context); rig.In.SetCanBeInterrupt(FALSE);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    rig.In.SetStep(1);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
    ExpectWarp(rig, 10, 0, CK_TRANSITION_WARPSTART);
}

TEST_F(CharacterPrimaryPlaybackTest, LinkedUnbreakableWarpUsesPointThreeFrameEndTolerance) {
    PrimaryRig rig(Context); rig.In.SetCanBeInterrupt(FALSE); rig.In.LinkToFrameRate(TRUE, 30);
    rig.In.SetFrame(9.6f); ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation());
    rig.In.SetFrame(9.7f); ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
    ExpectWarp(rig, rig.In.GetFrame(), 0, CK_TRANSITION_WARPSTART);
}

TEST_F(CharacterPrimaryPlaybackTest, SamePositionWarpReturnsRealDestinationFrameAndCurveEndpoint) {
    PrimaryRig rig(Context);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSAMEPOS, 3));
    ExpectWarp(rig, 4, 4, CK_TRANSITION_WARPSAMEPOS);
}

TEST_F(CharacterPrimaryPlaybackTest, SignedSamePositionFrameIncludingMinusOneIsSuccessfulTransitionData) {
    const float steps[] = {-.25f, -.1f};
    for (float step : steps) {
        PrimaryRig rig(Context); rig.In.SetCurrentStep(step);
        const float frame = step * 10.0f;
        // An independent actual builder proves that a negative result can
        // accompany a complete transition and valid endpoint controllers.
        RCKKeyedAnimation independent(Context);
        EXPECT_FLOAT_EQ(frame, independent.CreateTransition(&rig.In, &rig.Out, CK_TRANSITION_WARPSAMEPOS, 2, 0));
        ASSERT_EQ(1, independent.GetAnimationCount());
        CKObjectAnimation *built = independent.GetAnimation(EntityAPI(&rig.Root)); ASSERT_NE(nullptr, built);
        VxVector first, last; ASSERT_TRUE(built->EvaluatePosition(0, first));
        ASSERT_TRUE(built->EvaluatePosition(2, last));
        EXPECT_FLOAT_EQ(0, first.x); EXPECT_FLOAT_EQ(20, last.x);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSAMEPOS, 2));
        EXPECT_EQ(rig.Character.GetWarper(), rig.Character.GetActiveAnimation());
        EXPECT_EQ(&rig.Out, rig.Character.GetNextActiveAnimation());
        CKDWORD mode = 0; CKAnimation *source = nullptr, *destination = nullptr;
        float from = 0, to = 0;
        rig.Character.GetWarperParameters(&mode, &source, &from, &destination, &to);
        EXPECT_EQ(CK_TRANSITION_WARPSAMEPOS, mode); EXPECT_EQ(&rig.In, source); EXPECT_EQ(&rig.Out, destination);
        EXPECT_FLOAT_EQ(frame, from); EXPECT_FLOAT_EQ(frame, to);
        RCKKeyedAnimation *warp = static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper());
        ASSERT_EQ(1, warp->GetAnimationCount());
        built = warp->GetAnimation(EntityAPI(&rig.Root)); ASSERT_NE(nullptr, built);
        ASSERT_TRUE(built->EvaluatePosition(0, first)); ASSERT_TRUE(built->EvaluatePosition(2, last));
        EXPECT_FLOAT_EQ(0, first.x); EXPECT_FLOAT_EQ(20, last.x);
        EXPECT_NEAR(0, rig.Root.GetLocalMatrix()[3][0], .0001f);
    }
}

TEST_F(CharacterPrimaryPlaybackTest, PendingUnbreakableWarpKeepsPriorityAfterNonzeroStepButZeroStepSwitchesImmediately) {
    for (int advanced = 0; advanced < 2; ++advanced) {
        SCOPED_TRACE(advanced);
        PrimaryRig rig(Context); rig.Out.SetCanBeInterrupt(FALSE);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
        CKAnimation *warp = rig.Character.GetActiveAnimation();
        if (advanced) warp->SetStep(.5f);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.In, CK_TRANSITION_WARPSTART, 5));
        EXPECT_EQ(advanced ? warp : &rig.In, rig.Character.GetActiveAnimation());
        EXPECT_EQ(advanced ? &rig.Out : nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(2, warp->GetLength());
    }
}

TEST_F(CharacterPrimaryPlaybackTest, ExistingNonKeyedWarpBranchSwitchesBorrowedPointerWithoutInventedKeyedAccess) {
    PrimaryRig rig(Context); RCKAnimation pure(Context); pure.SetLength(10); pure.SetFrame(3);
    ASSERT_EQ(CK_OK, rig.Character.AddAnimation(&pure));
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&pure, CK_TRANSITION_WARPSTART, 2));
    EXPECT_EQ(&pure, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(.4f, rig.In.GetStep()); EXPECT_FLOAT_EQ(4, rig.Root.GetLocalMatrix()[3][0]);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
    // A plain source cannot build keyed channels. The centered fallback
    // replaces the old successful no-op without changing its source cursor.
    EXPECT_EQ(&rig.Out, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(0, rig.Out.GetFrame()); EXPECT_FLOAT_EQ(3, pure.GetFrame());
    EXPECT_FLOAT_EQ(-20, rig.Out.GetRootVectorInternal().x);
    VxMatrix world, local; world.SetIdentity(); world[3][0] = 10; local.SetIdentity();
    ExpectMatrix(world, rig.Character.GetWorldMatrix()); ExpectMatrix(world, rig.Character.GetLocalMatrix());
    ExpectMatrix(world, rig.Root.GetWorldMatrix()); ExpectMatrix(local, rig.Root.GetLocalMatrix());
    rig.Character.SetActiveAnimation(&rig.In);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&pure, 0));
    EXPECT_EQ(&rig.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(&pure, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(3, pure.GetFrame());
}

TEST_F(CharacterPrimaryPlaybackTest, FailedRealOwnerMismatchWarpDoesNotPublishFalseSuccessInEitherEndBranch) {
    for (int atEnd = 0; atEnd < 2; ++atEnd) {
        PrimaryRig rig(Context); PrimaryRig foreign(Context);
        foreign.In.SetCanBeInterrupt(!atEnd); foreign.In.SetStep(atEnd ? 1.0f : .4f);
        ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(&foreign.In));
        CKAnimation *warp = rig.Character.GetWarper(); ASSERT_NE(nullptr, warp);
        const float originalLength = warp->GetLength();
        VxMatrix beforeCharacter = rig.Character.GetWorldMatrix(), beforeForeign = foreign.Root.GetWorldMatrix();
        EXPECT_EQ(CKERR_INVALIDPARAMETER, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
        EXPECT_EQ(&foreign.In, rig.Character.GetActiveAnimation()); EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(originalLength, warp->GetLength());
        ExpectMatrix(beforeCharacter, rig.Character.GetWorldMatrix()); ExpectMatrix(beforeForeign, foreign.Root.GetWorldMatrix());
    }
}

TEST_F(CharacterPrimaryPlaybackTest, ContextDeletedPrivateWarperReturnsFailureInsteadOfCallingNull) {
    // Factory registration makes DestroyObject deliver the actual class-list
    // CheckPreDeletion notification; stack-only characters are not recipients.
    RCKCharacter *character = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER));
    RCKKeyedAnimation *in = static_cast<RCKKeyedAnimation *>(Context->CreateObject(CKCID_KEYEDANIMATION));
    RCKKeyedAnimation *out = static_cast<RCKKeyedAnimation *>(Context->CreateObject(CKCID_KEYEDANIMATION));
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, in); ASSERT_NE(nullptr, out);
    ASSERT_EQ(CK_OK, character->AddAnimation(in)); ASSERT_EQ(CK_OK, character->AddAnimation(out));
    in->SetLength(10); in->SetStep(.4f); in->SetCanBeInterrupt(TRUE);
    out->SetLength(10); ASSERT_EQ(CK_OK, character->SetActiveAnimation(in));
    CKAnimation *warp = character->GetWarper(); ASSERT_NE(nullptr, warp);
    CKDependencies none; none.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(warp, 0, &none));
    ASSERT_EQ(nullptr, character->GetWarper());
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character->SetNextActiveAnimation(out, CK_TRANSITION_WARPSTART, 2));
    EXPECT_EQ(in, character->GetActiveAnimation()); EXPECT_EQ(nullptr, character->GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(4, in->GetFrame());
}

TEST_F(CharacterPrimaryPlaybackTest, SameSourceWarpRejectsWithoutChangingPoseSelectionOrRetainedCurve) {
    for (int retained = 0; retained < 2; ++retained) {
        SCOPED_TRACE(retained);
        PrimaryRig rig(Context);
        RCKKeyedAnimation *warp = static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper());
        ASSERT_NE(nullptr, warp);
        if (retained) {
            ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Out, CK_TRANSITION_WARPSTART, 2));
            ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(&rig.In));
            rig.In.SetStep(.6f);
        }
        const int count = warp->GetAnimationCount();
        ASSERT_EQ(retained, count);
        CKObjectAnimation *curve = warp->GetAnimation(EntityAPI(&rig.Root));
        VxVector samples[3];
        if (retained) {
            ASSERT_NE(nullptr, curve);
            for (int i = 0; i < 3; ++i)
                ASSERT_TRUE(curve->EvaluatePosition(warp->GetLength() * .5f * i, samples[i]));
            EXPECT_FLOAT_EQ(4, samples[0].x); EXPECT_FLOAT_EQ(20, samples[2].x);
        }
        CKDWORD mode; CKAnimation *source, *destination; float from, to;
        rig.Character.GetWarperParameters(&mode, &source, &from, &destination, &to);
        CKAnimation *active = rig.Character.GetActiveAnimation(), *queued = rig.Character.GetNextActiveAnimation();
        const float length = warp->GetLength(), warpFrame = warp->GetFrame(), warpStep = warp->GetStep();
        const float inFrame = rig.In.GetFrame(), inStep = rig.In.GetStep();
        VxMatrix characterWorld = rig.Character.GetWorldMatrix();
        VxMatrix rootWorld = rig.Root.GetWorldMatrix(), rootLocal = rig.Root.GetLocalMatrix();
        EXPECT_EQ(CKERR_INVALIDPARAMETER, rig.Character.SetNextActiveAnimation(&rig.In, CK_TRANSITION_WARPSTART, 5));
        EXPECT_EQ(active, rig.Character.GetActiveAnimation()); EXPECT_EQ(queued, rig.Character.GetNextActiveAnimation());
        CKDWORD actualMode; CKAnimation *actualSource, *actualDestination; float actualFrom, actualTo;
        rig.Character.GetWarperParameters(&actualMode, &actualSource, &actualFrom, &actualDestination, &actualTo);
        EXPECT_EQ(mode, actualMode); EXPECT_EQ(source, actualSource); EXPECT_EQ(destination, actualDestination);
        EXPECT_FLOAT_EQ(from, actualFrom); EXPECT_FLOAT_EQ(to, actualTo);
        EXPECT_FLOAT_EQ(length, warp->GetLength()); EXPECT_FLOAT_EQ(warpFrame, warp->GetFrame());
        EXPECT_FLOAT_EQ(warpStep, warp->GetStep()); EXPECT_EQ(count, warp->GetAnimationCount());
        EXPECT_FLOAT_EQ(inFrame, rig.In.GetFrame()); EXPECT_FLOAT_EQ(inStep, rig.In.GetStep());
        ExpectMatrix(characterWorld, rig.Character.GetWorldMatrix());
        ExpectMatrix(rootWorld, rig.Root.GetWorldMatrix()); ExpectMatrix(rootLocal, rig.Root.GetLocalMatrix());
        EXPECT_EQ(curve, warp->GetAnimation(EntityAPI(&rig.Root)));
        if (retained) {
            for (int i = 0; i < 3; ++i) {
                VxVector actual; ASSERT_TRUE(curve->EvaluatePosition(length * .5f * i, actual));
                EXPECT_FLOAT_EQ(samples[i].x, actual.x); EXPECT_FLOAT_EQ(samples[i].y, actual.y);
                EXPECT_FLOAT_EQ(samples[i].z, actual.z);
            }
        }
    }
}

TEST_F(CharacterPrimaryPlaybackTest, AlignWithoutActiveMovesToRootAndPreservesRootWorldPose) {
    PrimaryRig rig(Context); rig.Character.SetActiveAnimation(nullptr);
    VxMatrix before = rig.Root.GetWorldMatrix(); rig.Character.AlignCharacterWithRootPosition();
    EXPECT_NEAR(14, Position(rig.Character), .0001f); ExpectMatrix(before, rig.Root.GetWorldMatrix());
    rig.Character.SetRootBodyPart(nullptr); before = rig.Character.GetWorldMatrix();
    rig.Character.AlignCharacterWithRootPosition(); ExpectMatrix(before, rig.Character.GetWorldMatrix());
    rig.Character.SetActiveAnimation(&rig.In); rig.Character.AlignCharacterWithRootPosition();
    rig.In.SetStep(.4f); EXPECT_NEAR(0, rig.Root.GetLocalMatrix()[3][0], .0001f);
}

TEST_F(CharacterPrimaryPlaybackTest, AlignNonKeyedActivePreservesCharacterAndRootPose) {
    PrimaryRig rig(Context); RCKAnimation pure(Context); rig.Character.SetActiveAnimation(&pure);
    VxMatrix character = rig.Character.GetWorldMatrix(), root = rig.Root.GetWorldMatrix();
    rig.Character.AlignCharacterWithRootPosition();
    ExpectMatrix(character, rig.Character.GetWorldMatrix()); ExpectMatrix(root, rig.Root.GetWorldMatrix());
}

TEST_F(CharacterPrimaryPlaybackTest, AlignConsumesRelativeYawPreservesScaleAndKeepsChildWorldPose) {
    PrimaryRig rig(Context);
    const float pi = 3.14159265358979323846f;
    VxQuaternion first(0, std::sin(pi / 12), 0, std::cos(pi / 12));
    VxQuaternion last(0, std::sin(pi / 4), 0, std::cos(pi / 4));
    rig.InChannel.AddRotationKey(0, &first); rig.InChannel.AddRotationKey(10, &last);
    rig.In.SetCharacterOrientation(TRUE);
    VxMatrix world; world.SetIdentity();
    float c = std::cos(pi / 12), s = std::sin(pi / 12);
    world[0][0] = 2 * c; world[0][2] = 2 * s; world[1][1] = 3;
    world[2][0] = -4 * s; world[2][2] = 4 * c; world[3][0] = 10;
    rig.Character.SetWorldMatrix(world); rig.In.SetStep(1);
    VxMatrix beforeRoot = rig.Root.GetWorldMatrix(), expected = rig.Character.GetWorldMatrix();
    const float turnCos = .5f, turnSin = std::sqrt(3.0f) * .5f;
    for (int row = 0; row < 3; ++row) {
        float x = expected[row][0], z = expected[row][2];
        expected[row][0] = x * turnCos - z * turnSin;
        expected[row][2] = x * turnSin + z * turnCos;
    }
    for (int col = 0; col < 3; ++col) expected[3][col] = beforeRoot[3][col];
    rig.Character.AlignCharacterWithRootPosition();
    ExpectMatrix(expected, rig.Character.GetWorldMatrix()); ExpectMatrix(beforeRoot, rig.Root.GetWorldMatrix());
    rig.In.SetStep(1); EXPECT_NEAR(0, rig.Root.GetLocalMatrix()[3][0], .0001f);
    VxVector raw; ASSERT_TRUE(rig.InChannel.EvaluatePosition(10, raw)); EXPECT_FLOAT_EQ(10, raw.x);
}

TEST_F(CharacterPrimaryPlaybackTest, AlignDoesNotTransferPitchIntoCharacterOrientation) {
    PrimaryRig rig(Context); VxQuaternion first(0, 0, 0, 1), last(.70710678f, 0, 0, .70710678f);
    rig.InChannel.AddRotationKey(0, &first); rig.InChannel.AddRotationKey(10, &last);
    rig.In.SetCharacterOrientation(TRUE); rig.In.SetStep(1);
    VxMatrix expected = rig.Character.GetWorldMatrix(), root = rig.Root.GetWorldMatrix();
    for (int col = 0; col < 3; ++col) expected[3][col] = root[3][col];
    rig.Character.AlignCharacterWithRootPosition(); ExpectMatrix(expected, rig.Character.GetWorldMatrix());
    ExpectMatrix(root, rig.Root.GetWorldMatrix());
}

TEST_F(CharacterPrimaryPlaybackTest, DerivedKeyedRetainsExistingInheritedAlignmentSupport) {
    PrimaryRig rig(Context); RCKObjectAnimation channel(Context); PrimaryDerived derived(Context);
    PositionCurve(channel, rig.Root, 7, 17); ASSERT_EQ(CK_OK, derived.AddAnimation(&channel)); derived.SetLength(10);
    ASSERT_EQ(CK_OK, rig.Character.AddAnimation(&derived)); derived.SetStep(.5f);
    ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(&derived));
    rig.Character.AlignCharacterWithRootPosition(); EXPECT_NEAR(15, Position(rig.Character), .0001f);
    derived.SetStep(.5f); EXPECT_NEAR(0, rig.Root.GetLocalMatrix()[3][0], .0001f);
}

TEST_F(CharacterPrimaryPlaybackTest, WarperParametersSupportEveryOptionalOutputWithoutCrossWriting) {
    PrimaryCharacter character(Context); RCKAnimation source(Context), destination(Context);
    character.Parameters(0x12345678, &source, 3.25f, &destination, 6.5f);
    for (unsigned mask = 0; mask < 32; ++mask) {
        CKDWORD mode = 0xabcdef; CKAnimation *fromAnimation = nullptr, *toAnimation = nullptr;
        float from = -13, to = -17;
        character.GetWarperParameters(mask & 1 ? &mode : nullptr, mask & 2 ? &fromAnimation : nullptr,
            mask & 4 ? &from : nullptr, mask & 8 ? &toAnimation : nullptr, mask & 16 ? &to : nullptr);
        EXPECT_EQ(mask & 1 ? 0x12345678u : 0xabcdefu, mode);
        EXPECT_EQ(mask & 2 ? &source : nullptr, fromAnimation); EXPECT_EQ(mask & 8 ? &destination : nullptr, toAnimation);
        EXPECT_FLOAT_EQ(mask & 4 ? 3.25f : -13, from); EXPECT_FLOAT_EQ(mask & 16 ? 6.5f : -17, to);
    }
}

TEST_F(CharacterPrimaryPlaybackTest, AutomaticProcessingChangesOnlyItsBitAndUsesBooleanTruth) {
    PrimaryCharacter character(Context); const CKDWORD flags = 0x12345678u;
    character.EntityFlags(flags); EXPECT_FALSE(character.IsAutomaticProcess());
    character.SetAutomaticProcess(17); EXPECT_TRUE(character.IsAutomaticProcess());
    EXPECT_EQ(flags | CK_3DENTITY_CHARACTERDOPROCESS, character.EntityFlags());
    character.SetAutomaticProcess(FALSE); EXPECT_FALSE(character.IsAutomaticProcess()); EXPECT_EQ(flags, character.EntityFlags());
}

TEST_F(CharacterPrimaryPlaybackTest, LodStorageIsUnclampedAndValidLevelsSelectActualChannels) {
    PrimaryRig rig(Context); const float values[] = {-0.5f, 0, .25f, 1, 1.5f};
    for (float value : values) { rig.Character.SetAnimationLevelOfDetail(value); EXPECT_FLOAT_EQ(value, rig.Character.GetAnimationLevelOfDetail()); }
    RCKBodyPart joint(Context); RCKObjectAnimation channel(Context);
    joint.SetMoveableFlags(joint.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    ASSERT_TRUE(joint.SetParent(EntityAPI(&rig.Root))); ASSERT_EQ(CK_OK, rig.Character.AddBodyPart(PartAPI(&joint)));
    VxVector zero(0, 0, 0); joint.SetPosition(&zero, EntityAPI(&rig.Root));
    PositionCurve(channel, joint, 0, 30); ASSERT_EQ(CK_OK, rig.In.AddAnimation(&channel));
    rig.Character.SetAnimationLevelOfDetail(0); rig.In.SetStep(.5f);
    EXPECT_NEAR(5, rig.Root.GetLocalMatrix()[3][0], .0001f); EXPECT_NEAR(0, joint.GetLocalMatrix()[3][0], .0001f);
    rig.Character.SetAnimationLevelOfDetail(1); rig.In.SetStep(.5f); EXPECT_NEAR(15, joint.GetLocalMatrix()[3][0], .0001f);
    joint.SetParent(nullptr);
}
} // namespace
