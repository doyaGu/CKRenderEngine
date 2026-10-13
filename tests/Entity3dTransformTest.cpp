#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Entity3dTransformTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    static CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    static void Expect(const VxVector &v, float x, float y, float z, float tolerance = 1e-5f) {
        EXPECT_NEAR(x, v.x, tolerance); EXPECT_NEAR(y, v.y, tolerance); EXPECT_NEAR(z, v.z, tolerance);
    }
    static VxMatrix Pose() {
        VxMatrix matrix = VxMatrix::Identity();
        matrix[0].Set(0, 2, 0); matrix[1].Set(-3, 0, 0); matrix[2].Set(0, 0, 4); matrix[3].Set(10, 20, 30);
        return matrix;
    }
};

TEST_F(Entity3dTransformTest, PointAndDirectionConsumersRespectIndependentReferenceCoordinates) {
    RCK3dEntity *object = Entity(), *reference = Entity();
    object->SetWorldMatrix(Pose());
    VxMatrix ref = VxMatrix::Identity(); ref[0].Set(2, 0, 0); ref[3].Set(4, 5, 6); reference->SetWorldMatrix(ref);
    const VxVector input(1, 2, 3); VxVector result;
    object->Transform(&result, &input); Expect(result, 4, 22, 42);
    object->Transform(&result, &input, API(reference)); Expect(result, 0, 17, 36);
    object->TransformVector(&result, &input); Expect(result, -6, 2, 12);
    object->TransformVector(&result, &input, API(reference)); Expect(result, -3, 2, 12);
    const VxVector world(4, 22, 42), refPoint(0, 17, 36), worldDirection(-6, 2, 12), refDirection(-3, 2, 12);
    object->InverseTransform(&result, &world); Expect(result, 1, 2, 3);
    object->InverseTransform(&result, &refPoint, API(reference)); Expect(result, 1, 2, 3);
    object->InverseTransformVector(&result, &worldDirection); Expect(result, 1, 2, 3);
    object->InverseTransformVector(&result, &refDirection, API(reference)); Expect(result, 1, 2, 3);
}

TEST_F(Entity3dTransformTest, BatchConsumersTransformEveryPointIncludingInPlaceWithoutTouchingSentinel) {
    RCK3dEntity *object = Entity(), *reference = Entity(); object->SetWorldMatrix(Pose());
    reference->SetPosition3f(4, 5, 6);
    const VxVector inputs[2] = {VxVector(1, 2, 3), VxVector(-2, -1, 0)};
    VxVector output[3] = {VxVector(), VxVector(), VxVector(91, 92, 93)};
    object->TransformMany(output, inputs, 2); Expect(output[0], 4, 22, 42); Expect(output[1], 13, 16, 30); Expect(output[2], 91, 92, 93);
    object->TransformMany(output, inputs, 2, API(reference)); Expect(output[0], 0, 17, 36); Expect(output[1], 9, 11, 24);
    const VxVector refInput[2] = {VxVector(0, 17, 36), VxVector(9, 11, 24)};
    object->InverseTransformMany(output, refInput, 2, API(reference)); Expect(output[0], 1, 2, 3); Expect(output[1], -2, -1, 0);
    VxVector worldInput[2] = {VxVector(4, 22, 42), VxVector(13, 16, 30)};
    object->InverseTransformMany(worldInput, worldInput, 2); Expect(worldInput[0], 1, 2, 3); Expect(worldInput[1], -2, -1, 0);
    object->TransformMany(worldInput, worldInput, 2); Expect(worldInput[0], 4, 22, 42); Expect(worldInput[1], 13, 16, 30);
}

TEST_F(Entity3dTransformTest, PositionAndTranslationUsePointVersusDirectionReferenceSemantics) {
    RCK3dEntity *object = Entity(), *reference = Entity(); reference->SetWorldMatrix(Pose());
    const VxVector point(1, 2, 3); object->SetPosition(&point, API(reference)); VxVector result;
    object->GetPosition(&result); Expect(result, 4, 22, 42); object->GetPosition(&result, API(reference)); Expect(result, 1, 2, 3);
    object->Translate3f(1, 2, 3, API(reference)); object->GetPosition(&result); Expect(result, -2, 24, 54);
    const VxVector move(1, -2, 3); object->Translate(&move); object->GetPosition(&result); Expect(result, -1, 22, 57);
    object->SetPosition3f(-3, 5, 1); object->GetPosition(&result); Expect(result, -3, 5, 1);
}

TEST_F(Entity3dTransformTest, ParentMatrixPropagationKeepsOrMovesChildAndGrandchildWorldCoordinates) {
    RCK3dEntity *parent = Entity(), *child = Entity(), *grandchild = Entity();
    child->SetPosition3f(2, 0, 0); grandchild->SetPosition3f(3, 0, 0);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE)); ASSERT_TRUE(grandchild->SetParent(API(child), TRUE));
    parent->SetPosition3f(10, 0, 0, nullptr, FALSE); VxVector result;
    child->GetPosition(&result); Expect(result, 12, 0, 0); grandchild->GetPosition(&result); Expect(result, 13, 0, 0);
    parent->SetPosition3f(20, 0, 0, nullptr, TRUE); child->GetPosition(&result); Expect(result, 12, 0, 0);
    child->GetPosition(&result, API(parent)); Expect(result, -8, 0, 0); grandchild->GetPosition(&result); Expect(result, 13, 0, 0);
    VxMatrix matrix = VxMatrix::Identity(); matrix[3].Set(30, 0, 0); parent->SetLocalMatrix(matrix, FALSE);
    child->GetPosition(&result); Expect(result, 22, 0, 0); grandchild->GetPosition(&result); Expect(result, 23, 0, 0);
    matrix[3].Set(40, 0, 0); parent->SetWorldMatrix(matrix, TRUE); child->GetPosition(&result); Expect(result, 22, 0, 0);
    Expect(child->GetLocalMatrix()[3], -18, 0, 0); Expect(parent->GetWorldMatrix()[3], 40, 0, 0);
}

TEST_F(Entity3dTransformTest, DontUpdateFromParentLeavesFrozenChildAndItsDescendantUntouched) {
    RCK3dEntity *parent = Entity(), *child = Entity(), *grandchild = Entity(); child->SetPosition3f(2, 3, 4); grandchild->SetPosition3f(4, 5, 6);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE)); ASSERT_TRUE(grandchild->SetParent(API(child), TRUE));
    child->ModifyMoveableFlags(VX_MOVEABLE_DONTUPDATEFROMPARENT, 0); parent->SetWorldMatrix(Pose(), FALSE);
    VxVector result; child->GetPosition(&result); Expect(result, 2, 3, 4); grandchild->GetPosition(&result); Expect(result, 4, 5, 6);
    parent->SetPosition3f(-4, -5, -6, nullptr, TRUE); child->GetPosition(&result); Expect(result, 2, 3, 4);
}

TEST_F(Entity3dTransformTest, CachedInverseTranslationRemainsCorrectAfterPositionOnlyUpdate) {
    RCK3dEntity *object = Entity(); object->SetWorldMatrix(Pose()); const VxMatrix &inverse = object->GetInverseWorldMatrix();
    EXPECT_NEAR(-10.0f, inverse[3].x, 1e-5f); EXPECT_NEAR(10.0f / 3.0f, inverse[3].y, 1e-5f); EXPECT_NEAR(-7.5f, inverse[3].z, 1e-5f);
    object->SetPosition3f(7, 8, 9); const VxVector world(1, 10, 21); VxVector result;
    object->InverseTransform(&result, &world); Expect(result, 1, 2, 3);
    EXPECT_TRUE(object->GetMoveableFlags() & VX_MOVEABLE_INVERSEWORLDMATVALID);
    object->SetScale3f(4, 6, 8, FALSE, FALSE); object->InverseTransform(&result, &world); Expect(result, .5f, 1, 1.5f);
}

TEST_F(Entity3dTransformTest, ScaleSetAndMultiplyPreserveOrientationWithSeparateLocalAndWorldModes) {
    RCK3dEntity *object = Entity(); object->SetWorldMatrix(Pose()); VxVector result;
    object->SetScale3f(5, 6, 7, FALSE, FALSE); object->GetScale(&result, FALSE); Expect(result, 5, 6, 7); Expect(object->GetWorldMatrix()[0], 0, 5, 0);
    const VxVector scale(2, 3, 4); object->AddScale(&scale, FALSE, TRUE); object->GetScale(&result, TRUE); Expect(result, 10, 18, 28);
    object->AddScale3f(2, 3, 4, FALSE, FALSE); object->GetScale(&result, FALSE); Expect(result, 20, 54, 112);
    const VxVector replacement(3, 4, 5); object->SetScale(&replacement, FALSE, TRUE); object->GetScale(&result, TRUE); Expect(result, 3, 4, 5);
}

TEST_F(Entity3dTransformTest, ZeroScaleComponentsUseNativeInvertibleEpsilonAndNegativeScaleRetainsSign) {
    RCK3dEntity *object = Entity(); object->SetScale3f(0, -2, 3); const VxVector input(1, 1, 1); VxVector result;
    object->TransformVector(&result, &input); EXPECT_NEAR(1e-6f, result.x, 1e-12f); EXPECT_NEAR(-2, result.y, 1e-5f); EXPECT_NEAR(3, result.z, 1e-5f);
    object->AddScale3f(0, 1, 1); object->TransformVector(&result, &input); EXPECT_NEAR(1e-12f, result.x, 1e-16f); EXPECT_NEAR(-2, result.y, 1e-5f); EXPECT_NEAR(3, result.z, 1e-5f);
}

TEST_F(Entity3dTransformTest, RotationAndLookAtPreservePositionAndScaleWithIndependentNumericAxes) {
    RCK3dEntity *object = Entity(); object->SetPosition3f(3, 4, 5); object->Rotate3f(0, 0, 1, 1.5707963267948966f);
    const VxVector x(1, 0, 0); VxVector result; object->TransformVector(&result, &x); Expect(result, 0, -1, 0); Expect(object->GetWorldMatrix()[3], 3, 4, 5);
    const VxVector axis(0, 0, 1); object->Rotate(&axis, -1.5707963267948966f); object->TransformVector(&result, &x); Expect(result, 1, 0, 0);
    object->SetScale3f(2, 3, 4); const VxVector target(13, 4, 15); object->LookAt(&target); VxVector direction, up, right;
    object->GetOrientation(&direction, &up, &right); Expect(direction, .70710678f, 0, .70710678f); Expect(up, 0, 1, 0); Expect(right, .70710678f, 0, -.70710678f);
    object->GetScale(&result, FALSE); Expect(result, 2, 3, 4); Expect(object->GetWorldMatrix()[3], 3, 4, 5);
}

TEST_F(Entity3dTransformTest, OrientationAndQuaternionReferenceAccessorsHaveIndependentWorldAxisOracles) {
    RCK3dEntity *object = Entity(), *reference = Entity(); VxMatrix ref = VxMatrix::Identity(); ref[0].Set(0, 1, 0); ref[1].Set(-1, 0, 0); reference->SetWorldMatrix(ref);
    const VxVector direction(0, 0, 1), up(0, 1, 0), right(1, 0, 0); object->SetOrientation(&direction, &up, &right, API(reference));
    VxVector d, u, r; object->GetOrientation(&d, &u, &r); Expect(d, 0, 0, 1); Expect(u, -1, 0, 0); Expect(r, 0, 1, 0);
    object->GetOrientation(&d, &u, &r, API(reference)); Expect(d, 0, 0, 1); Expect(u, 0, 1, 0); Expect(r, 1, 0, 0);
    object->SetPosition3f(3, 4, 5); object->SetScale3f(2, 3, 4); const VxQuaternion identity(0, 0, 0, 1);
    object->SetQuaternion(&identity, API(reference), FALSE, TRUE); object->GetScale(&d, TRUE); Expect(d, 2, 3, 4); Expect(object->GetWorldMatrix()[3], 3, 4, 5);
    VxQuaternion q; object->GetQuaternion(&q, API(reference)); EXPECT_NEAR(0, q.x, 1e-5f); EXPECT_NEAR(0, q.y, 1e-5f); EXPECT_NEAR(0, q.z, 1e-5f); EXPECT_NEAR(1, q.w, 1e-5f);
    object->SetQuaternion(&identity, nullptr, FALSE, FALSE); object->GetScale(&d, FALSE); Expect(d, 1, 1, 1); object->GetQuaternion(&q); EXPECT_NEAR(1, q.w, 1e-5f);
}

TEST_F(Entity3dTransformTest, ConstructMatricesApplyScalePositionAndParentUsingActualPointConsumers) {
    RCK3dEntity *object = Entity(), *parent = Entity(); parent->SetPosition3f(10, 20, 30); ASSERT_TRUE(object->SetParent(API(parent), TRUE));
    const VxVector position(1, 2, 3), scale(2, 3, 4), input(1, 1, 1); const VxQuaternion identity(0, 0, 0, 1); VxVector output;
    ASSERT_TRUE(object->ConstructLocalMatrix(&position, &scale, &identity)); object->Transform(&output, &input); Expect(output, 13, 25, 37);
    ASSERT_TRUE(object->ConstructWorldMatrix(&position, &scale, &identity)); object->Transform(&output, &input); Expect(output, 3, 5, 7); Expect(object->GetLocalMatrix()[3], -9, -18, -27);
    ASSERT_TRUE(object->ConstructWorldMatrixEx(&position, &scale, &identity, &identity, -1)); object->Transform(&output, &input); Expect(output, 3, 5, 7);
    ASSERT_TRUE(object->ConstructLocalMatrixEx(&position, &scale, &identity, &identity, 1)); object->Transform(&output, &input); Expect(output, 13, 25, 37);
}

TEST_F(Entity3dTransformTest, LastFrameMatrixFallsBackToLiveWorldThenRetainsExplicitSnapshot) {
    RCK3dEntity *object = Entity(); object->SetPosition3f(1, 2, 3); Expect(object->GetLastFrameMatrix()[3], 1, 2, 3);
    object->SaveLastFrameMatrix(); object->SetPosition3f(4, 5, 6); Expect(object->GetLastFrameMatrix()[3], 1, 2, 3); Expect(object->GetWorldMatrix()[3], 4, 5, 6);
    object->SaveLastFrameMatrix(); Expect(object->GetLastFrameMatrix()[3], 4, 5, 6);
}

TEST_F(Entity3dTransformTest, ExtendedMatrixConstructionRotatesAnisotropicScaleAxesInTheShearFrame) {
    RCK3dEntity *object = Entity(), *parent = Entity(); parent->SetPosition3f(10, 20, 30); ASSERT_TRUE(object->SetParent(API(parent), TRUE));
    const VxVector position(1, 2, 3), scale(2, 4, 6), x(1, 0, 0), y(0, 1, 0);
    const VxQuaternion identity(0, 0, 0, 1), shear(0, 0, .382683432365f, .923879532511f); VxVector result;
    ASSERT_TRUE(object->ConstructWorldMatrixEx(&position, &scale, &identity, &shear, 1));
    object->Transform(&result, &x); Expect(result, 4, 1, 3); object->Transform(&result, &y); Expect(result, 0, 5, 3);
    ASSERT_TRUE(object->ConstructLocalMatrixEx(&position, &scale, &identity, &shear, -1));
    object->Transform(&result, &x); Expect(result, 14, 21, 33); object->Transform(&result, &y); Expect(result, 10, 25, 33);
}
} // namespace
