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
CK_CLASSID PrimaryTypesDerivedID = 0;
class PrimaryTypesDerived : public RCKKeyedAnimation {
public:
    explicit PrimaryTypesDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return PrimaryTypesDerivedID; }
};
static CK3dEntity *TypesEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }

class PrimaryTypesRig {
public:
    RCKCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation SourceChannel, DestinationChannel, DerivedChannel;
    RCKKeyedAnimation Source, Destination;
    PrimaryTypesDerived Derived;
    RCKAnimation PlainSource, PlainDestination;
    explicit PrimaryTypesRig(CKContext *context)
        : Character(context), Root(context), SourceChannel(context), DestinationChannel(context),
          DerivedChannel(context), Source(context), Destination(context), Derived(context),
          PlainSource(context), PlainDestination(context) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, TypesEntity(Character));
        Curve(SourceChannel, Source, 5, 15); Curve(DestinationChannel, Destination, 100, 120);
        Curve(DerivedChannel, Derived, 200, 230);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Source));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Destination));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Derived));
        PlainSource.SetLength(10); PlainDestination.SetLength(10);
        PlainSource.LinkToFrameRate(TRUE, 1024); PlainDestination.LinkToFrameRate(TRUE, 1024);
        PlainSource.SetCanBeInterrupt(FALSE); PlainDestination.SetCanBeInterrupt(FALSE);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&PlainSource));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&PlainDestination));
    }
    ~PrimaryTypesRig() { Root.SetParent(nullptr, TRUE); }
    void Tick(float milliseconds) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.ProcessAnimation(milliseconds);
    }
    void RootOffset(float x) { VxVector pos(x, 0, 0); Root.SetPosition(&pos, TypesEntity(Character)); }
private:
    void Curve(RCKObjectAnimation &channel, RCKKeyedAnimation &animation, float first, float last) {
        channel.Set3dEntity(TypesEntity(Root)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel)); animation.SetLength(10);
        animation.LinkToFrameRate(TRUE, 1024); animation.SetCanBeInterrupt(FALSE);
    }
};

class CharacterPrimaryTypesTest : public ::testing::Test {
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
        PrimaryTypesDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(PrimaryTypesDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(PrimaryTypesRig &rig, float characterX, float rootLocalX = 0) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
                                     &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix()};
        const float translations[] = {characterX, characterX, characterX + rootLocalX, rootLocalX};
        for (int which = 0; which < 4; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0) expected = translations[which];
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0003f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(TypesEntity(rig.Character), rig.Root.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void Active(PrimaryTypesRig &rig, CKAnimation &animation, float frame) {
        EXPECT_EQ(&animation, rig.Character.GetActiveAnimation());
        EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(frame, animation.GetFrame(), .0002f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), animation.GetCharacter());
    }
    static void Cache(RCKKeyedAnimation &animation, RCKObjectAnimation &channel, float rawPosition) {
        EXPECT_EQ(&channel, animation.GetRootAnimationInternal());
        const VxVector &value = animation.GetRootVectorInternal();
        EXPECT_NEAR(-rawPosition, value.x, .0002f); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
    }
    static void Start(PrimaryTypesRig &rig, CKAnimation &animation) {
        animation.SetFrame(0); ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(&animation));
    }
    static void WarmEight(PrimaryTypesRig &rig, CKAnimation &animation) {
        Start(rig, animation); ASSERT_EQ(8.0f, animation.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
        Active(rig, animation, 8);
    }
    static void Queue(PrimaryTypesRig &rig, CKAnimation &animation) {
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&animation, CK_TRANSITION_TOSTART));
        ASSERT_EQ(&animation, rig.Character.GetNextActiveAnimation());
    }
    static void Warp(PrimaryTypesRig &rig, RCKKeyedAnimation &source, RCKKeyedAnimation &destination,
                     float first, float last, float endWorld) {
        Start(rig, source); source.SetCanBeInterrupt(TRUE);
        ASSERT_EQ(2.0f, source.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
        const float initialWorld = &source == &rig.Derived ? 106.0f : 102.0f;
        Active(rig, source, 2); Pose(rig, initialWorld);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&destination, CK_TRANSITION_WARPSTART, 2));
        CKAnimation *warp = rig.Character.GetWarper(); ASSERT_NE(nullptr, warp);
        EXPECT_EQ(warp, rig.Character.GetActiveAnimation());
        EXPECT_EQ(&destination, rig.Character.GetNextActiveAnimation());
        EXPECT_FLOAT_EQ(2, warp->GetLength()); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        CKDWORD mode = 0; CKAnimation *in = nullptr, *out = nullptr; float from = -1, to = -1;
        rig.Character.GetWarperParameters(&mode, &in, &from, &out, &to);
        EXPECT_EQ(CK_TRANSITION_WARPSTART, mode); EXPECT_EQ(&source, in); EXPECT_EQ(&destination, out);
        EXPECT_FLOAT_EQ(2, from); EXPECT_FLOAT_EQ(0, to);
        RCKKeyedAnimation *keyed = static_cast<RCKKeyedAnimation *>(warp);
        ASSERT_EQ(1, keyed->GetAnimationCount());
        CKObjectAnimation *channel = keyed->GetAnimation(TypesEntity(rig.Root)); ASSERT_NE(nullptr, channel);
        VxVector a, b; ASSERT_TRUE(channel->EvaluatePosition(0, a)); ASSERT_TRUE(channel->EvaluatePosition(2, b));
        EXPECT_NEAR(first, a.x, .0002f); EXPECT_NEAR(last, b.x, .0002f);
        EXPECT_FLOAT_EQ(0, a.y); EXPECT_FLOAT_EQ(0, a.z); EXPECT_FLOAT_EQ(0, b.y); EXPECT_FLOAT_EQ(0, b.z);
        Pose(rig, initialWorld);
        ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Tick(1.953125f);
        Active(rig, destination, 0); Pose(rig, endWorld);
        rig.Tick(1.953125f); Active(rig, destination, 2);
        Pose(rig, endWorld + (&destination == &rig.Derived ? 6 : 4));
    }
};

TEST_F(CharacterPrimaryTypesTest, OrdinaryLinkedPlaybackAdvancesCursorWithoutAbsorbingUnrelatedRoot) {
    PrimaryTypesRig rig(Context); rig.RootOffset(7); Start(rig, rig.PlainSource);
    ASSERT_EQ(2.0f, rig.PlainSource.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
    Active(rig, rig.PlainSource, 2); Pose(rig, 100, 7);
    Cache(rig.Source, rig.SourceChannel, 5);
}
TEST_F(CharacterPrimaryTypesTest, OrdinaryUnlinkedPlaybackIgnoresNegativeStoredRateAndZeroDelta) {
    PrimaryTypesRig rig(Context); rig.RootOffset(7); Start(rig, rig.PlainSource);
    rig.PlainSource.LinkToFrameRate(FALSE, -2048);
    ASSERT_EQ(1.0f, rig.PlainSource.GetNextFrame(0)); rig.Tick(0);
    Active(rig, rig.PlainSource, 1); Pose(rig, 100, 7);
    rig.Tick(1000); Active(rig, rig.PlainSource, 2); Pose(rig, 100, 7);
}
TEST_F(CharacterPrimaryTypesTest, OrdinaryReversePlaybackUsesVirtualCursorAndLeavesPoseUntouched) {
    PrimaryTypesRig rig(Context); rig.RootOffset(7); WarmEight(rig, rig.PlainSource); Pose(rig, 100, 7);
    rig.PlainSource.LinkToFrameRate(TRUE, -1024);
    ASSERT_EQ(6.0f, rig.PlainSource.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
    Active(rig, rig.PlainSource, 6); Pose(rig, 100, 7);
}
TEST_F(CharacterPrimaryTypesTest, OrdinaryNoninterruptibleClipDisplaysItsEndThenStopsOnce) {
    PrimaryTypesRig rig(Context); rig.RootOffset(7); WarmEight(rig, rig.PlainSource);
    ASSERT_EQ(12.0f, rig.PlainSource.GetNextFrame(3.90625f)); rig.Tick(3.90625f);
    Active(rig, rig.PlainSource, 10); Pose(rig, 100, 7);
    rig.Tick(0); EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation());
    EXPECT_EQ(nullptr, rig.Character.GetNextActiveAnimation()); Pose(rig, 100, 7);
}
TEST_F(CharacterPrimaryTypesTest, OrdinaryToOrdinaryCarriesTimeWithoutConsumingUnrelatedRootOffset) {
    PrimaryTypesRig rig(Context); rig.RootOffset(7); WarmEight(rig, rig.PlainSource);
    rig.PlainDestination.LinkToFrameRate(TRUE, 512); Queue(rig, rig.PlainDestination);
    ASSERT_EQ(12.0f, rig.PlainSource.GetNextFrame(3.90625f)); rig.Tick(3.90625f);
    Active(rig, rig.PlainDestination, 1); Pose(rig, 100, 7);
    rig.Tick(1.953125f); Active(rig, rig.PlainDestination, 2); Pose(rig, 100, 7);
}
TEST_F(CharacterPrimaryTypesTest, OrdinarySourcePromotesKeyedDestinationWithActualRemainderMotion) {
    PrimaryTypesRig rig(Context); WarmEight(rig, rig.PlainSource); Pose(rig, 100);
    Queue(rig, rig.Destination); rig.Tick(3.90625f);
    Active(rig, rig.Destination, 2); Cache(rig.Destination, rig.DestinationChannel, 104); Pose(rig, 104);
}
TEST_F(CharacterPrimaryTypesTest, KeyedSourceFinishesRootMotionBeforeOrdinaryDestinationAdvances) {
    PrimaryTypesRig rig(Context); WarmEight(rig, rig.Source); Pose(rig, 108);
    Queue(rig, rig.PlainDestination); rig.Tick(3.90625f);
    Active(rig, rig.PlainDestination, 2); Pose(rig, 110);
    rig.Tick(1.953125f); Active(rig, rig.PlainDestination, 4); Pose(rig, 110);
}
TEST_F(CharacterPrimaryTypesTest, RegisteredDerivedKeyedDirectPlaybackUsesItsRealRootChannel) {
    PrimaryTypesRig rig(Context); Start(rig, rig.Derived);
    ASSERT_TRUE(CKIsChildClassOf(&rig.Derived, CKCID_KEYEDANIMATION));
    ASSERT_NE(CKCID_KEYEDANIMATION, rig.Derived.GetClassID());
    rig.Tick(1.953125f); Active(rig, rig.Derived, 2);
    Cache(rig.Derived, rig.DerivedChannel, 206); Pose(rig, 106);
}
TEST_F(CharacterPrimaryTypesTest, DerivedNoActiveEntryRecentersPreviouslyPlayedNonzeroFirstCurve) {
    PrimaryTypesRig rig(Context); Start(rig, rig.Derived); rig.Tick(1.953125f);
    Active(rig, rig.Derived, 2); Cache(rig.Derived, rig.DerivedChannel, 206); Pose(rig, 106);
    ASSERT_EQ(CK_OK, rig.Character.SetActiveAnimation(nullptr));
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Derived, CK_TRANSITION_TOSTART));
    EXPECT_EQ(nullptr, rig.Character.GetActiveAnimation()); EXPECT_EQ(&rig.Derived, rig.Character.GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(0, rig.Derived.GetFrame()); Cache(rig.Derived, rig.DerivedChannel, 200); Pose(rig, 106);
    rig.Tick(0); Active(rig, rig.Derived, 0); Pose(rig, 106);
    rig.Tick(1.953125f); Active(rig, rig.Derived, 2); Pose(rig, 112);
}
TEST_F(CharacterPrimaryTypesTest, ImmediateDerivedSwitchRefreshesStaleCenterBeforeApplyingFrameZero) {
    PrimaryTypesRig rig(Context); Start(rig, rig.Derived); rig.Tick(1.953125f); Pose(rig, 106);
    Cache(rig.Derived, rig.DerivedChannel, 206);
    WarmEight(rig, rig.Source); Pose(rig, 114); rig.Source.SetCanBeInterrupt(TRUE);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Derived, CK_TRANSITION_FROMNOW));
    Active(rig, rig.Derived, 0); Cache(rig.Derived, rig.DerivedChannel, 200); Pose(rig, 114);
    rig.Tick(1.953125f); Active(rig, rig.Derived, 2); Pose(rig, 120);
}
TEST_F(CharacterPrimaryTypesTest, ExactKeyedToDerivedCarriesTerminalAndDestinationRootMotion) {
    PrimaryTypesRig rig(Context); WarmEight(rig, rig.Source); Pose(rig, 108);
    Queue(rig, rig.Derived); rig.Tick(3.90625f);
    Active(rig, rig.Derived, 2); Cache(rig.Derived, rig.DerivedChannel, 206); Pose(rig, 116);
}
TEST_F(CharacterPrimaryTypesTest, DerivedToExactKeyedCarriesTerminalAndDestinationRootMotion) {
    PrimaryTypesRig rig(Context); WarmEight(rig, rig.Derived); Pose(rig, 124);
    Queue(rig, rig.Destination); rig.Tick(3.90625f);
    Active(rig, rig.Destination, 2); Cache(rig.Destination, rig.DestinationChannel, 104); Pose(rig, 134);
}
TEST_F(CharacterPrimaryTypesTest, DerivedSourceBuildsAndPlaysActualWarpToExactKeyedDestination) {
    PrimaryTypesRig rig(Context); Warp(rig, rig.Derived, rig.Destination, 206, 100, 0);
}
TEST_F(CharacterPrimaryTypesTest, DerivedDestinationBuildsAndPlaysActualWarpFromExactKeyedSource) {
    PrimaryTypesRig rig(Context); Warp(rig, rig.Source, rig.Derived, 7, 200, 295);
}
TEST_F(CharacterPrimaryTypesTest, ForeignDerivedTransitionCannotChangeOwnerCursorOrCharacterPose) {
    PrimaryTypesRig rig(Context); RCKCharacter foreign(Context); PrimaryTypesDerived other(Context);
    ASSERT_EQ(CK_OK, foreign.AddAnimation(&other)); other.SetLength(10); other.SetCurrentStep(.4f);
    WarmEight(rig, rig.Source); Pose(rig, 108);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, rig.Character.SetNextActiveAnimation(&other, CK_TRANSITION_FROMNOW));
    Active(rig, rig.Source, 8); Pose(rig, 108);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&foreign), other.GetCharacter()); EXPECT_FLOAT_EQ(4, other.GetFrame());
}
TEST_F(CharacterPrimaryTypesTest, InterruptibleOrdinarySourceWarpFallsBackToCenteredImmediateKeyedPlayback) {
    PrimaryTypesRig rig(Context); Start(rig, rig.PlainSource); rig.Tick(1.953125f);
    Active(rig, rig.PlainSource, 2); Pose(rig, 100); rig.PlainSource.SetCanBeInterrupt(TRUE);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_WARPSTART, 2));
    Active(rig, rig.Destination, 0); Cache(rig.Destination, rig.DestinationChannel, 100); Pose(rig, 100);
    EXPECT_EQ(0, static_cast<RCKKeyedAnimation *>(rig.Character.GetWarper())->GetAnimationCount());
    rig.Tick(1.953125f); Active(rig, rig.Destination, 2); Pose(rig, 104);
}
TEST_F(CharacterPrimaryTypesTest, NoninterruptibleOrdinaryWarpWaitsUntilActualForwardEnd) {
    PrimaryTypesRig rig(Context); Start(rig, rig.PlainSource); rig.Tick(1.953125f);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_WARPSTART, 2));
    Active(rig, rig.PlainSource, 2); Pose(rig, 100);
    ASSERT_EQ(10.0f, rig.PlainSource.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
    Active(rig, rig.PlainSource, 10); Pose(rig, 100);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_WARPSTART, 2));
    Active(rig, rig.Destination, 0); Cache(rig.Destination, rig.DestinationChannel, 100); Pose(rig, 100);
    rig.Tick(1.953125f); Active(rig, rig.Destination, 2); Pose(rig, 104);
}
TEST_F(CharacterPrimaryTypesTest, NoninterruptibleOrdinaryReverseWarpWaitsAtLengthAndThroughInterior) {
    PrimaryTypesRig rig(Context); Start(rig, rig.PlainSource);
    ASSERT_EQ(10.0f, rig.PlainSource.GetNextFrame(9.765625f)); rig.Tick(9.765625f);
    Active(rig, rig.PlainSource, 10); Pose(rig, 100); rig.PlainSource.LinkToFrameRate(TRUE, -1024);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_WARPSTART, 2));
    Active(rig, rig.PlainSource, 10); Pose(rig, 100);
    ASSERT_EQ(8.0f, rig.PlainSource.GetNextFrame(1.953125f)); rig.Tick(1.953125f);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_WARPSTART, 2));
    Active(rig, rig.PlainSource, 8); Pose(rig, 100);
    ASSERT_EQ(0.0f, rig.PlainSource.GetNextFrame(7.8125f)); rig.Tick(7.8125f);
    Active(rig, rig.PlainSource, 0); Pose(rig, 100);
    ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_WARPSTART, 2));
    Active(rig, rig.Destination, 0); Cache(rig.Destination, rig.DestinationChannel, 100); Pose(rig, 100);
    rig.Tick(1.953125f); Active(rig, rig.Destination, 2); Pose(rig, 104);
}
} // namespace
