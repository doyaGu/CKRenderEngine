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
CK_CLASSID FloorVelocityDerivedID = 0;
class FloorVelocityDerived : public RCKKeyedAnimation {
public:
    explicit FloorVelocityDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return FloorVelocityDerivedID; }
};
class FloorVelocityCharacter : public RCKCharacter {
public:
    explicit FloorVelocityCharacter(CKContext *context) : RCKCharacter(context) {}
    void Scan() { FindFloorReference(); }
    RCK3dEntity *ObservedFloor() const { return m_FloorRef; }
    void AssignFloorForGetter(RCK3dEntity *floor) { m_FloorRef = floor; }
};
static CK3dEntity *FVEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
static CKBodyPart *FVPart(RCKBodyPart &part) { return reinterpret_cast<CKBodyPart *>(&part); }
static void FVReady(RCK3dEntity &entity) {
    entity.SetMoveableFlags(entity.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
}
static void FVPosition(RCK3dEntity &entity, float x, float y, float z, CK3dEntity *reference) {
    VxVector value(x, y, z); entity.SetPosition(&value, reference);
}
class FloorVelocityRig {
public:
    FloorVelocityCharacter Character;
    RCKBodyPart Root;
    RCKObjectAnimation RootCurve, DestinationCurve;
    RCKKeyedAnimation Exact, Destination;
    FloorVelocityDerived Derived;
    bool IsDerived;
    FloorVelocityRig(CKContext *context, bool derived)
        : Character(context), Root(context), RootCurve(context), DestinationCurve(context),
          Exact(context), Destination(context), Derived(context), IsDerived(derived) {
        FVReady(Character); FVReady(Root);
        Character.SetMoveableFlags(Character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; world[3][1] = 50; world[3][2] = -20;
        Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(FVPart(Root)));
        FVPosition(Root, 0, 0, 0, FVEntity(Character));
        RootCurve.Set3dEntity(FVEntity(Root));
        VxVector first(5, 7, 11), last(25, -3, 41);
        RootCurve.AddPositionKey(0, &first); RootCurve.AddPositionKey(10, &last);
        EXPECT_EQ(CK_OK, Active().AddAnimation(&RootCurve)); Active().SetLength(10);
        DestinationCurve.Set3dEntity(FVEntity(Root));
        VxVector targetFirst(1000, 2000, 3000), targetLast(9000, 4000, 6000);
        DestinationCurve.AddPositionKey(0, &targetFirst); DestinationCurve.AddPositionKey(10, &targetLast);
        EXPECT_EQ(CK_OK, Destination.AddAnimation(&DestinationCurve)); Destination.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Active())); EXPECT_EQ(CK_OK, Character.AddAnimation(&Destination));
        Active().SetCanBeInterrupt(FALSE); Active().LinkToFrameRate(TRUE, 1024);
        Active().SetFrame(2); EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Active()));
    }
    ~FloorVelocityRig() { Root.SetParent(nullptr, TRUE); }
    RCKKeyedAnimation &Active() { return IsDerived ? static_cast<RCKKeyedAnimation &>(Derived) : Exact; }
};
class CharacterFloorVelocityTest : public ::testing::Test {
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
        FloorVelocityDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(FloorVelocityDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Vector(const VxVector &value, float x, float y, float z) {
        EXPECT_NEAR(x, value.x, .0001f); EXPECT_NEAR(y, value.y, .0001f); EXPECT_NEAR(z, value.z, .0001f);
    }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_FLOAT_EQ(expected[row][column], actual[row][column]) << row << ',' << column;
    }
    static void Estimate(FloorVelocityRig &rig, float milliseconds, float x, float y, float z) {
        const VxMatrix character = rig.Character.GetWorldMatrix(), root = rig.Root.GetWorldMatrix();
        const VxMatrix rootLocal = rig.Root.GetLocalMatrix();
        const VxVector center = rig.Active().GetRootVectorInternal();
        const float frame = rig.Active().GetFrame(), destinationFrame = rig.Destination.GetFrame();
        CKAnimation *queue = rig.Character.GetNextActiveAnimation();
        VxVector result(999, 888, 777); rig.Character.GetEstimatedVelocity(milliseconds, &result);
        Vector(result, x, y, z);
        EXPECT_FLOAT_EQ(frame, rig.Active().GetFrame()); EXPECT_FLOAT_EQ(destinationFrame, rig.Destination.GetFrame());
        EXPECT_EQ(&rig.Active(), rig.Character.GetActiveAnimation()); EXPECT_EQ(queue, rig.Character.GetNextActiveAnimation());
        Vector(rig.Active().GetRootVectorInternal(), center.x, center.y, center.z);
        Matrix(rig.Character.GetWorldMatrix(), character); Matrix(rig.Root.GetWorldMatrix(), root);
        Matrix(rig.Root.GetLocalMatrix(), rootLocal); EXPECT_EQ(FVEntity(rig.Character), rig.Root.GetParent());
    }
};

TEST_F(CharacterFloorVelocityTest, FloorCategoryPriorityOverridesChildInsertionOrder) {
    FloorVelocityCharacter character(Context); RCKBodyPart root(Context), pas(Context), foot(Context), steps(Context), floor(Context);
    FVReady(character); FVReady(root); EXPECT_EQ(CK_OK, character.AddBodyPart(FVPart(root)));
    pas.SetName("LeftPasMarker"); foot.SetName("RightFootMarker"); steps.SetName("FootstepsMarker"); floor.SetName("GroundFloorRefMarker");
    EXPECT_TRUE(pas.SetParent(FVEntity(root), TRUE)); EXPECT_TRUE(foot.SetParent(FVEntity(root), TRUE));
    EXPECT_TRUE(steps.SetParent(FVEntity(root), TRUE)); EXPECT_TRUE(floor.SetParent(FVEntity(root), TRUE));
    FVPosition(floor, 2, 3, 4, FVEntity(root)); const VxMatrix before = floor.GetWorldMatrix();
    character.Scan(); EXPECT_EQ(&floor, character.ObservedFloor()); Matrix(floor.GetWorldMatrix(), before);
    floor.SetName("Marker"); character.Scan(); EXPECT_EQ(&steps, character.ObservedFloor());
    steps.SetName("Marker"); character.Scan(); EXPECT_EQ(&foot, character.ObservedFloor());
    foot.SetName("Marker"); character.Scan(); EXPECT_EQ(&pas, character.ObservedFloor());
    EXPECT_EQ(FVEntity(root), floor.GetParent()); EXPECT_EQ(FVEntity(root), pas.GetParent());
    floor.SetParent(nullptr, TRUE); steps.SetParent(nullptr, TRUE); foot.SetParent(nullptr, TRUE); pas.SetParent(nullptr, TRUE); root.SetParent(nullptr, TRUE);
}

TEST_F(CharacterFloorVelocityTest, FloorScanIsDirectCaseSensitiveAndPreservesOldReferenceOnMiss) {
    FloorVelocityCharacter character(Context); RCKBodyPart root(Context), branch(Context), nested(Context), lower(Context), unnamed(Context), custom(Context);
    character.AssignFloorForGetter(&custom); character.Scan(); EXPECT_EQ(&custom, character.ObservedFloor());
    EXPECT_EQ(CK_OK, character.AddBodyPart(FVPart(root))); branch.SetName("Branch"); nested.SetName("FloorRef"); lower.SetName("floorref_footsteps_pas");
    EXPECT_TRUE(branch.SetParent(FVEntity(root), TRUE)); EXPECT_TRUE(nested.SetParent(FVEntity(branch), TRUE));
    EXPECT_TRUE(lower.SetParent(FVEntity(root), TRUE)); EXPECT_TRUE(unnamed.SetParent(FVEntity(root), TRUE));
    character.Scan(); EXPECT_EQ(&custom, character.ObservedFloor()); EXPECT_EQ(nullptr, custom.GetParent());
    branch.SetName("LeftPas"); character.Scan(); EXPECT_EQ(&branch, character.ObservedFloor());
    branch.SetName("Branch"); character.Scan(); EXPECT_EQ(&branch, character.ObservedFloor());
    nested.SetParent(nullptr, TRUE); unnamed.SetParent(nullptr, TRUE); lower.SetParent(nullptr, TRUE); branch.SetParent(nullptr, TRUE); root.SetParent(nullptr, TRUE);
}

TEST_F(CharacterFloorVelocityTest, FloorScanSelectsFirstMatchingSiblingAndTracksActualHierarchy) {
    FloorVelocityCharacter character(Context); RCKBodyPart root(Context), first(Context), second(Context);
    EXPECT_EQ(CK_OK, character.AddBodyPart(FVPart(root))); first.SetName("FloorRefFirst"); second.SetName("FloorRefSecond");
    EXPECT_TRUE(first.SetParent(FVEntity(root), TRUE)); EXPECT_TRUE(second.SetParent(FVEntity(root), TRUE));
    character.Scan(); EXPECT_EQ(&first, character.ObservedFloor());
    EXPECT_TRUE(first.SetParent(nullptr, TRUE)); character.Scan(); EXPECT_EQ(&second, character.ObservedFloor());
    EXPECT_EQ(FVEntity(root), second.GetParent()); EXPECT_EQ(nullptr, first.GetParent());
    second.SetParent(nullptr, TRUE); root.SetParent(nullptr, TRUE);
}

TEST_F(CharacterFloorVelocityTest, FloorGetterObservesIndependentBorrowedFieldWithoutReparenting) {
    FloorVelocityCharacter character(Context); RCK3dEntity parent(Context), custom(Context);
    FVReady(parent); FVReady(custom); FVPosition(parent, 30, 40, 50, nullptr);
    EXPECT_TRUE(custom.SetParent(FVEntity(parent), TRUE)); FVPosition(custom, 2, 3, 4, FVEntity(parent));
    character.AssignFloorForGetter(&custom);
    EXPECT_EQ(FVEntity(custom), character.GetFloorReferenceObject());
    Vector(VxVector(custom.GetWorldMatrix()[3]), 32, 43, 54); EXPECT_EQ(FVEntity(parent), custom.GetParent());
    character.AssignFloorForGetter(nullptr); EXPECT_EQ(nullptr, character.GetFloorReferenceObject());
    custom.SetParent(nullptr, TRUE);
}

TEST_F(CharacterFloorVelocityTest, FloorSetterControlsRealLowLodChannelSelectionAndNullRestoresRootOnly) {
    FloorVelocityCharacter character(Context); RCKBodyPart root(Context), floor(Context), limb(Context);
    RCKObjectAnimation rootCurve(Context), floorCurve(Context), limbCurve(Context); RCKKeyedAnimation animation(Context);
    FVReady(character); FVReady(root); FVReady(floor); FVReady(limb);
    character.SetAnimationLevelOfDetail(0); EXPECT_EQ(CK_OK, character.AddBodyPart(FVPart(root)));
    EXPECT_TRUE(floor.SetParent(FVEntity(root), TRUE)); EXPECT_TRUE(limb.SetParent(FVEntity(root), TRUE));
    FVPosition(root, 0, 0, 0, FVEntity(character)); FVPosition(floor, 2, 0, 0, FVEntity(root)); FVPosition(limb, 3, 0, 0, FVEntity(root));
    rootCurve.Set3dEntity(FVEntity(root)); floorCurve.Set3dEntity(FVEntity(floor)); limbCurve.Set3dEntity(FVEntity(limb));
    VxVector a(0, 0, 0), b(10, 0, 0), c(30, 0, 0), d(50, 0, 0), e(90, 0, 0), f(190, 0, 0);
    rootCurve.AddPositionKey(0, &a); rootCurve.AddPositionKey(10, &b);
    floorCurve.AddPositionKey(0, &c); floorCurve.AddPositionKey(10, &d);
    limbCurve.AddPositionKey(0, &e); limbCurve.AddPositionKey(10, &f);
    EXPECT_EQ(CK_OK, animation.AddAnimation(&rootCurve)); EXPECT_EQ(CK_OK, animation.AddAnimation(&limbCurve));
    EXPECT_EQ(CK_OK, animation.AddAnimation(&floorCurve)); animation.SetLength(10); EXPECT_EQ(CK_OK, character.AddAnimation(&animation));
    character.SetFloorReferenceObject(FVEntity(floor)); EXPECT_EQ(&floor, character.ObservedFloor());
    animation.SetFrame(5);
    EXPECT_FLOAT_EQ(40, floor.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(3, limb.GetLocalMatrix()[3][0]);
    EXPECT_EQ(FVEntity(root), floor.GetParent()); EXPECT_EQ(nullptr, floor.GetCharacter());
    character.SetFloorReferenceObject(nullptr); EXPECT_EQ(nullptr, character.ObservedFloor());
    animation.SetFrame(8); EXPECT_FLOAT_EQ(40, floor.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(3, limb.GetLocalMatrix()[3][0]);
    floor.SetParent(nullptr, TRUE); limb.SetParent(nullptr, TRUE); root.SetParent(nullptr, TRUE);
}

TEST_F(CharacterFloorVelocityTest, LinkedVelocityIsRootCurveDisplacementWithoutAdvancingPose) {
    for (int type = 0; type < 2; ++type) { FloorVelocityRig rig(Context, type != 0); Estimate(rig, 1.953125f, 4, -2, 6); }
}

TEST_F(CharacterFloorVelocityTest, ChangingFloorAfterCachedChannelSortUpdatesNewFloorAtLowLod) {
    FloorVelocityCharacter character(Context); RCKBodyPart root(Context), floorA(Context), floorB(Context);
    RCKObjectAnimation rootCurve(Context), curveA(Context), curveB(Context); RCKKeyedAnimation animation(Context);
    FVReady(character); FVReady(root); FVReady(floorA); FVReady(floorB);
    character.SetAnimationLevelOfDetail(0); FVPosition(character, 100, 0, 0, nullptr);
    ASSERT_EQ(CK_OK, character.AddBodyPart(FVPart(root)));
    ASSERT_TRUE(floorA.SetParent(FVEntity(root), TRUE)); ASSERT_TRUE(floorB.SetParent(FVEntity(root), TRUE));
    FVPosition(root, 0, 0, 0, FVEntity(character)); FVPosition(floorA, 2, 0, 0, FVEntity(root)); FVPosition(floorB, 4, 0, 0, FVEntity(root));
    rootCurve.Set3dEntity(FVEntity(root)); curveA.Set3dEntity(FVEntity(floorA)); curveB.Set3dEntity(FVEntity(floorB));
    VxVector rootFirst(0, 0, 0), rootLast(10, 0, 0), aFirst(30, 0, 0), aLast(50, 0, 0), bFirst(100, 0, 0), bLast(160, 0, 0);
    rootCurve.AddPositionKey(0, &rootFirst); rootCurve.AddPositionKey(10, &rootLast);
    curveA.AddPositionKey(0, &aFirst); curveA.AddPositionKey(10, &aLast);
    curveB.AddPositionKey(0, &bFirst); curveB.AddPositionKey(10, &bLast);
    ASSERT_EQ(CK_OK, animation.AddAnimation(&rootCurve)); ASSERT_EQ(CK_OK, animation.AddAnimation(&curveA));
    ASSERT_EQ(CK_OK, animation.AddAnimation(&curveB)); animation.SetLength(10); ASSERT_EQ(CK_OK, character.AddAnimation(&animation));
    character.SetFloorReferenceObject(FVEntity(floorA)); animation.SetFrame(5);
    EXPECT_FLOAT_EQ(5, root.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(40, floorA.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(4, floorB.GetLocalMatrix()[3][0]);
    character.SetFloorReferenceObject(FVEntity(floorB)); EXPECT_EQ(&floorB, character.ObservedFloor()); animation.SetFrame(8);
    EXPECT_FLOAT_EQ(8, root.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(40, floorA.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(148, floorB.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(148, floorA.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(256, floorB.GetWorldMatrix()[3][0]);
    character.SetFloorReferenceObject(nullptr); animation.SetFrame(9);
    EXPECT_FLOAT_EQ(9, root.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(40, floorA.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(148, floorB.GetLocalMatrix()[3][0]);
    EXPECT_EQ(FVEntity(root), floorA.GetParent()); EXPECT_EQ(FVEntity(root), floorB.GetParent());
    floorA.SetParent(nullptr, TRUE); floorB.SetParent(nullptr, TRUE); root.SetParent(nullptr, TRUE);
}

TEST_F(CharacterFloorVelocityTest, ReverseLinkedAndPausedClocksHaveSignedAndZeroDisplacements) {
    for (int type = 0; type < 2; ++type) {
        FloorVelocityRig rig(Context, type != 0); rig.Active().SetFrame(4); rig.Active().LinkToFrameRate(TRUE, -1024);
        Estimate(rig, 1.953125f, -4, 2, -6); rig.Active().LinkToFrameRate(TRUE, 0); Estimate(rig, 1000, 0, 0, 0);
    }
}

TEST_F(CharacterFloorVelocityTest, UnlinkedVelocityAdvancesOneCurveFrameRegardlessOfStoredRateOrMilliseconds) {
    for (int type = 0; type < 2; ++type) {
        FloorVelocityRig rig(Context, type != 0); const float rates[] = {-2048, 0, 1024};
        for (int i = 0; i < 3; ++i) { rig.Active().LinkToFrameRate(FALSE, rates[i]); Estimate(rig, 0, 2, -1, 3); Estimate(rig, 1000, 2, -1, 3); }
    }
}

TEST_F(CharacterFloorVelocityTest, RotationAndCharacterWorldBasisDoNotRotateCurveCoordinateEstimate) {
    for (int type = 0; type < 2; ++type) {
        FloorVelocityRig rig(Context, type != 0);
        VxMatrix world; world.SetIdentity(); world[0][0] = 0; world[0][2] = 1; world[2][0] = -1; world[2][2] = 0;
        world[3][0] = 100; world[3][1] = 50; world[3][2] = -20; rig.Character.SetWorldMatrix(world);
        VxQuaternion first(0, 0, 0, 1), last(0, .7071067812f, 0, .7071067812f);
        rig.RootCurve.AddRotationKey(0, &first); rig.RootCurve.AddRotationKey(10, &last); rig.Active().SetFrame(2);
        Estimate(rig, 1.953125f, 4, -2, 6);
    }
}

TEST_F(CharacterFloorVelocityTest, QueuedDestinationAndCenteringDoNotChangeActiveCurvePrediction) {
    for (int type = 0; type < 2; ++type) {
        FloorVelocityRig rig(Context, type != 0); rig.Active().CenterAnimation(2); rig.Active().SetFrame(2);
        EXPECT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Destination, CK_TRANSITION_TOSTART, 4));
        ASSERT_EQ(&rig.Destination, rig.Character.GetNextActiveAnimation()); Estimate(rig, 1.953125f, 4, -2, 6);
        rig.Active().SetFrame(9); Estimate(rig, 1.953125f, 2, -1, 3);
        EXPECT_EQ(&rig.Destination, rig.Character.GetNextActiveAnimation());
    }
}

TEST_F(CharacterFloorVelocityTest, MissingActivePlainAnimationAndMissingRootChannelResetOutputToZero) {
    FloorVelocityCharacter character(Context); RCKAnimation plain(Context); RCKKeyedAnimation empty(Context);
    VxVector value(9, 8, 7); character.GetEstimatedVelocity(1000, &value); Vector(value, 0, 0, 0);
    EXPECT_EQ(CK_OK, character.SetActiveAnimation(&plain)); value.Set(9, 8, 7); character.GetEstimatedVelocity(1000, &value); Vector(value, 0, 0, 0);
    EXPECT_EQ(CK_OK, character.SetActiveAnimation(&empty)); value.Set(9, 8, 7); character.GetEstimatedVelocity(1000, &value); Vector(value, 0, 0, 0);
    EXPECT_FLOAT_EQ(0, plain.GetFrame()); EXPECT_FLOAT_EQ(0, empty.GetFrame());
}

TEST_F(CharacterFloorVelocityTest, IgnoredPositionProducesZeroAndNullOutputDoesNotMutateState) {
    for (int type = 0; type < 2; ++type) {
        FloorVelocityRig rig(Context, type != 0); rig.RootCurve.SetFlags(rig.RootCurve.GetFlags() | CK_OBJECTANIMATION_IGNOREPOS);
        Estimate(rig, 1.953125f, 0, 0, 0);
        const float frame = rig.Active().GetFrame(); const VxMatrix before = rig.Root.GetWorldMatrix();
        rig.Character.GetEstimatedVelocity(1000, nullptr); EXPECT_FLOAT_EQ(frame, rig.Active().GetFrame()); Matrix(rig.Root.GetWorldMatrix(), before);
    }
}

TEST_F(CharacterFloorVelocityTest, RotationOnlyRootWithoutPositionControllerReturnsZero) {
    for (int type = 0; type < 2; ++type) {
        FloorVelocityRig rig(Context, type != 0);
        rig.RootCurve.ClearAll(); rig.RootCurve.Set3dEntity(FVEntity(rig.Root));
        rig.RootCurve.SetFlags(rig.RootCurve.GetFlags() & ~CK_OBJECTANIMATION_IGNOREPOS);
        VxQuaternion first(0, 0, 0, 1), last(0, .7071067812f, 0, .7071067812f);
        rig.RootCurve.AddRotationKey(0, &first); rig.RootCurve.AddRotationKey(10, &last);
        ASSERT_EQ(nullptr, rig.RootCurve.GetPositionController());
        ASSERT_NE(nullptr, rig.RootCurve.GetRotationController());
        VxVector noPosition(9, 8, 7);
        EXPECT_FALSE(rig.RootCurve.EvaluatePosition(2, noPosition)); Vector(noPosition, 0, 0, 0);
        Estimate(rig, 1.953125f, 0, 0, 0);
    }
}
} // namespace
