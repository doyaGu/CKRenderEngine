#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include "RCKKinematicChain.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class KinematicChainSolveTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKBodyPart *Root = nullptr, *Middle = nullptr, *End = nullptr;
    RCKKinematicChain *Chain = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKKinematicChain, CKCID_OBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Root = static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
        Middle = static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
        End = static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
        Chain = static_cast<RCKKinematicChain *>(Context->CreateObject(CKCID_KINEMATICCHAIN));
        ASSERT_NE(nullptr, Root); ASSERT_NE(nullptr, Middle); ASSERT_NE(nullptr, End); ASSERT_NE(nullptr, Chain);
        ASSERT_TRUE(End->SetParent(API(Root)));
        End->SetPosition3f(1, 0, 0, API(Root));
        ASSERT_EQ(CK_OK, Chain->SetStartEffector(Body(Root)));
        ASSERT_EQ(CK_OK, Chain->SetEndEffector(Body(End)));
        Joint(Root, CK_IKJOINT_ACTIVE_Z);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    static CKBodyPart *Body(RCKBodyPart *body) { return reinterpret_cast<CKBodyPart *>(body); }
    static void Joint(RCKBodyPart *body, CKDWORD flags, float minimum = -1, float maximum = 1) {
        CKIkJoint joint = {}; joint.m_Flags = flags;
        joint.m_Min.Set(minimum, minimum, minimum); joint.m_Max.Set(maximum, maximum, maximum);
        body->SetRotationJoint(&joint);
    }
    static VxVector Position(RCK3dEntity *entity) { VxVector p; entity->GetPosition(&p); return p; }
    static float Distance(const VxVector &a, const VxVector &b) {
        return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
    }
    static void Near(const VxVector &actual, const VxVector &expected, float tolerance = .05f) {
        EXPECT_LE(Distance(actual, expected), tolerance);
    }
    static void SameMatrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
            EXPECT_FLOAT_EQ(expected[row][col], actual[row][col]);
    }
};
}

TEST_F(KinematicChainSolveTest, NullTargetRejectsWithoutMovingTheRealHierarchy) {
    const VxMatrix root = Root->GetLocalMatrix(), end = End->GetLocalMatrix();
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Chain->IKSetEffectorPos(nullptr));
    SameMatrix(Root->GetLocalMatrix(), root); SameMatrix(End->GetLocalMatrix(), end);
}

TEST_F(KinematicChainSolveTest, MissingAndSingleEffectorsRejectWithoutMovingBodies) {
    VxVector target(0, 1, 0); const VxMatrix root = Root->GetLocalMatrix();
    Chain->SetStartEffector(nullptr); EXPECT_EQ(CKERR_INVALIDOPERATION, Chain->IKSetEffectorPos(&target));
    Chain->SetStartEffector(Body(Root)); Chain->SetEndEffector(nullptr);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Chain->IKSetEffectorPos(&target));
    Chain->SetEndEffector(Body(Root)); EXPECT_EQ(CKERR_INVALIDOPERATION, Chain->IKSetEffectorPos(&target));
    SameMatrix(Root->GetLocalMatrix(), root);
}

TEST_F(KinematicChainSolveTest, FullyInactiveJointsRejectAndKeepTheEntirePose) {
    Joint(Root, 0); VxVector target(0, 1, 0);
    const VxMatrix root = Root->GetLocalMatrix(), end = End->GetLocalMatrix();
    EXPECT_EQ(CKERR_INVALIDOPERATION, Chain->IKSetEffectorPos(&target));
    SameMatrix(Root->GetLocalMatrix(), root); SameMatrix(End->GetLocalMatrix(), end);
}

TEST_F(KinematicChainSolveTest, ZJointReachesKnownCircularTargetWithFixedRootAndLength) {
    VxVector target(std::cos(.6f), std::sin(.6f), 0); const VxVector input = target;
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), input);
    Near(Position(Root), VxVector(0, 0, 0), 1e-6f);
    EXPECT_NEAR(1, Distance(Position(Root), Position(End)), 1e-5f); Near(target, input, 0);
    EXPECT_NEAR(0, Root->GetLocalMatrix()[2][0], 1e-6f);
    EXPECT_NEAR(1, Root->GetLocalMatrix()[2][2], 1e-6f);
}

TEST_F(KinematicChainSolveTest, XJointRotatesOnlyItsNamedPlane) {
    Joint(Root, CK_IKJOINT_ACTIVE_X); End->SetPosition3f(0, 1, 0, API(Root));
    VxVector target(0, std::cos(.5f), std::sin(.5f));
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), target);
    EXPECT_NEAR(0, Position(End).x, 1e-6f); EXPECT_NEAR(1, Root->GetLocalMatrix()[0][0], 1e-6f);
}

TEST_F(KinematicChainSolveTest, YJointRotatesOnlyItsNamedPlane) {
    Joint(Root, CK_IKJOINT_ACTIVE_Y); End->SetPosition3f(0, 0, 1, API(Root));
    VxVector target(std::sin(.5f), 0, std::cos(.5f));
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), target);
    EXPECT_NEAR(0, Position(End).y, 1e-6f); EXPECT_NEAR(1, Root->GetLocalMatrix()[1][1], 1e-6f);
}

TEST_F(KinematicChainSolveTest, UnreachableTargetProjectsToTheActualOneLinkRadius) {
    VxVector direction(std::cos(.6f), std::sin(.6f), 0), target = direction * 3;
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), direction);
    EXPECT_NEAR(1, Distance(Position(Root), Position(End)), 1e-5f);
    Near(target, direction * 3, 0);
}

TEST_F(KinematicChainSolveTest, TwoBodiesCannotShortenTheirRigidSegmentForAnInteriorTarget) {
    VxVector direction(std::cos(.5f), std::sin(.5f), 0), target = direction * .25f;
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), direction);
    EXPECT_NEAR(1, Distance(Position(Root), Position(End)), 1e-5f);
}

TEST_F(KinematicChainSolveTest, ReferenceSpaceTargetIsTransformedOnceAndInputStaysUnchanged) {
    RCK3dEntity reference(Context); VxMatrix matrix;
    Vx3DMatrixFromEulerAngles(matrix, 0, 0, .35f); matrix[3][0] = 2; matrix[3][1] = -1;
    reference.SetWorldMatrix(matrix);
    const float x = std::cos(.6f)-2, y = std::sin(.6f)+1;
    VxVector input(std::cos(.35f)*x+std::sin(.35f)*y, -std::sin(.35f)*x+std::cos(.35f)*y, 0);
    const VxVector preserved = input;
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&input, API(&reference)));
    Near(Position(End), VxVector(std::cos(.6f), std::sin(.6f), 0)); Near(input, preserved, 0);
}

TEST_F(KinematicChainSolveTest, BodyOverrideSolvesOnlyThePrefixAndKeepsConfiguredEndEffector) {
    ASSERT_TRUE(Middle->SetParent(API(Root))); Middle->SetPosition3f(1, 0, 0, API(Root));
    ASSERT_TRUE(End->SetParent(API(Middle))); End->SetPosition3f(1, 0, 0, API(Middle));
    Joint(Middle, CK_IKJOINT_ACTIVE_Z); VxVector target(std::cos(.6f), std::sin(.6f), 0);
    const VxMatrix endLocal = End->GetLocalMatrix();
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target, nullptr, Body(Middle)));
    Near(Position(Middle), target); EXPECT_EQ(Body(End), Chain->GetEndEffector());
    SameMatrix(End->GetLocalMatrix(), endLocal); EXPECT_NEAR(2, Distance(Position(Root), Position(End)), 1e-5f);
}

TEST_F(KinematicChainSolveTest, RotatedTranslatedParentPreservesPivotAndUsesWorldJointAxes) {
    RCK3dEntity parent(Context); VxMatrix matrix;
    Vx3DMatrixFromEulerAngles(matrix, 0, 0, .4f); matrix[3][0] = 4; matrix[3][1] = -3; matrix[3][2] = 2;
    parent.SetWorldMatrix(matrix); ASSERT_TRUE(Root->SetParent(API(&parent)));
    Root->SetPosition3f(0, 0, 0, API(&parent));
    VxVector target(4+std::cos(.9f), -3+std::sin(.9f), 2);
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), target);
    Near(Position(Root), VxVector(4, -3, 2), 1e-5f); SameMatrix(parent.GetWorldMatrix(), matrix);
}

TEST_F(KinematicChainSolveTest, BentThreeBodyChainMovesBothJointsWithoutChangingLocalLengths) {
    ASSERT_TRUE(Middle->SetParent(API(Root))); Middle->SetPosition3f(1, 0, 0, API(Root));
    ASSERT_TRUE(End->SetParent(API(Middle))); End->SetPosition3f(0, 1, 0, API(Middle));
    Joint(Middle, CK_IKJOINT_ACTIVE_Z); VxVector target(1.15f, .9f, 0);
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), target);
    EXPECT_NEAR(1, Distance(Position(Root), Position(Middle)), 1e-5f);
    EXPECT_NEAR(1, Distance(Position(Middle), Position(End)), 1e-5f);
    Near(Position(Root), VxVector(0, 0, 0), 1e-5f);
}

TEST_F(KinematicChainSolveTest, ReachableLimitedTargetStaysInsideTheNamedZInterval) {
    Joint(Root, CK_IKJOINT_ACTIVE_Z | CK_IKJOINT_LIMIT_Z, -.5f, .5f);
    VxVector target(std::cos(.2f), std::sin(.2f), 0);
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target)); Near(Position(End), target);
    float x, y, z; Vx3DMatrixToEulerAngles(Root->GetLocalMatrix(), &x, &y, &z);
    EXPECT_GE(z, -.50001f); EXPECT_LE(z, .50001f);
}

TEST_F(KinematicChainSolveTest, TargetBeyondJointLimitCannotEscapeItOrWorsenTheInitialPose) {
    Joint(Root, CK_IKJOINT_ACTIVE_Z | CK_IKJOINT_LIMIT_Z, -.2f, .2f);
    VxVector target(std::cos(.6f), std::sin(.6f), 0); const float initial = Distance(Position(End), target);
    const CKERROR result = Chain->IKSetEffectorPos(&target);
    EXPECT_TRUE(result == CK_OK || result == CKERR_INVALIDOPERATION);
    float x, y, z; Vx3DMatrixToEulerAngles(Root->GetLocalMatrix(), &x, &y, &z);
    EXPECT_GE(z, -.20001f); EXPECT_LE(z, .20001f); EXPECT_LE(Distance(Position(End), target), initial+1e-5f);
}

TEST_F(KinematicChainSolveTest, AlreadyReachedTargetDoesNotRunASingularUnnecessaryRotation) {
    const VxMatrix root = Root->GetLocalMatrix(), end = End->GetLocalMatrix(); VxVector target = Position(End);
    ASSERT_EQ(CK_OK, Chain->IKSetEffectorPos(&target));
    SameMatrix(Root->GetLocalMatrix(), root); SameMatrix(End->GetLocalMatrix(), end);
}
