#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CurvePointTransformTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCurvePoint, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCurve, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKCurve *Curve(RCKCurve &curve) { return reinterpret_cast<CKCurve *>(&curve); }
    static void Vec(const VxVector &v, float x, float y, float z, float tolerance = .0001f) {
        EXPECT_NEAR(x, v.x, tolerance); EXPECT_NEAR(y, v.y, tolerance); EXPECT_NEAR(z, v.z, tolerance);
    }
    static VxMatrix Pose() {
        VxMatrix matrix = VxMatrix::Identity();
        matrix[0].Set(0, 2, 0); matrix[1].Set(-3, 0, 0); matrix[2].Set(0, 0, 4); matrix[3].Set(10, 20, 30);
        return matrix;
    }
    static void Prime(RCKCurve &owner, RCKCurve &other, RCKCurvePoint &point) {
        owner.Show(CKHIDE); other.Show(CKHIDE); point.SetCurve(Curve(owner));
        owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); other.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    }
    static void DirtyOnlyOwner(RCKCurve &owner, RCKCurve &other) {
        EXPECT_FALSE(owner.IsUpToDate()); EXPECT_TRUE(other.IsUpToDate());
    }
};
}

TEST_F(CurvePointTransformTest, RotateThroughSDKChangesAxesKeepsPositionAndInvalidatesOnlyOwner) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context);
    point.SetPosition3f(3, 4, 5); Prime(owner, other, point);
    const VxVector axis(0, 0, 1), x(1, 0, 0); VxVector result;
    Entity(point)->Rotate(&axis, 1.5707963267948966f); Entity(point)->TransformVector(&result, &x);
    Vec(result, 0, -1, 0); Vec(point.GetWorldMatrix()[3], 3, 4, 5); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, NegligibleRotationStillNotifiesOwnerWithoutChangingPose) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); Prime(owner, other, point);
    const VxVector axis(0, 0, 1); Entity(point)->Rotate(&axis, 0);
    Vec(point.GetWorldMatrix()[0], 1, 0, 0); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, TranslateUsesReferenceDirectionWithoutReferenceTranslation) {
    RCKCurve owner(Context), other(Context); RCK3dEntity reference(Context); RCKCurvePoint point(Context);
    reference.SetWorldMatrix(Pose()); point.SetPosition3f(1, 2, 3); Prime(owner, other, point);
    const VxVector delta(1, 2, 3); Entity(point)->Translate(&delta, Entity(reference));
    Vec(point.GetWorldMatrix()[3], -5, 4, 15); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, TranslateKeepChildrenPreservesChildWorldWhileFalseMovesIt) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); RCK3dEntity child(Context);
    child.SetPosition3f(2, 0, 0); ASSERT_TRUE(child.SetParent(Entity(point), TRUE)); Prime(owner, other, point);
    const VxVector delta(10, 0, 0); Entity(point)->Translate(&delta, nullptr, FALSE);
    Vec(child.GetWorldMatrix()[3], 12, 0, 0); DirtyOnlyOwner(owner, other);
    owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); Entity(point)->Translate(&delta, nullptr, TRUE);
    Vec(point.GetWorldMatrix()[3], 20, 0, 0); Vec(child.GetWorldMatrix()[3], 12, 0, 0);
    Vec(child.GetLocalMatrix()[3], -8, 0, 0); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, AddScaleMultipliesLocalAxisLengthsAndNotifiesOwner) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); point.SetScale3f(2, 3, 4); Prime(owner, other, point);
    const VxVector scale(3, 4, 5); VxVector result; Entity(point)->AddScale(&scale, FALSE, TRUE);
    Entity(point)->GetScale(&result, TRUE); Vec(result, 6, 12, 20); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, AddScaleWorldModeUsesWorldAxesWithTransformedParent) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); owner.SetWorldMatrix(Pose()); Prime(owner, other, point);
    const VxVector scale(2, 3, 4); VxVector result; Entity(point)->AddScale(&scale, FALSE, FALSE);
    Entity(point)->GetScale(&result, FALSE); Vec(result, 2, 3, 4); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetPositionTransformsReferencePointIncludingTranslation) {
    RCKCurve owner(Context), other(Context); RCK3dEntity reference(Context); RCKCurvePoint point(Context);
    reference.SetWorldMatrix(Pose()); Prime(owner, other, point); const VxVector position(1, 2, 3);
    Entity(point)->SetPosition(&position, Entity(reference)); Vec(point.GetWorldMatrix()[3], 4, 22, 42); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, ParentRelativeSetPositionRetainsLocalDisplacementAtLargeTranslation) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); owner.SetPosition3f(100000000, 0, 0);
    Prime(owner, other, point); const VxVector position(4, 2, 3); VxVector result;
    Entity(point)->SetPosition(&position, Entity(owner)); Entity(point)->GetPosition(&result, Entity(owner));
    Vec(result, 4, 2, 3); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetPositionKeepChildrenPreservesChildWorldCoordinates) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); RCK3dEntity child(Context);
    child.SetPosition3f(2, 3, 4); ASSERT_TRUE(child.SetParent(Entity(point), TRUE)); Prime(owner, other, point);
    const VxVector position(10, 20, 30); Entity(point)->SetPosition(&position, nullptr, TRUE);
    Vec(child.GetWorldMatrix()[3], 2, 3, 4); Vec(child.GetLocalMatrix()[3], -8, -17, -26); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetOrientationMapsReferenceAxesAndPreservesTranslation) {
    RCKCurve owner(Context), other(Context); RCK3dEntity reference(Context); RCKCurvePoint point(Context);
    VxMatrix matrix = VxMatrix::Identity(); matrix[0].Set(0, 1, 0); matrix[1].Set(-1, 0, 0); reference.SetWorldMatrix(matrix);
    point.SetPosition3f(3, 4, 5); Prime(owner, other, point);
    const VxVector direction(0, 0, 1), up(0, 1, 0), right(1, 0, 0); VxVector d, u, r;
    Entity(point)->SetOrientation(&direction, &up, &right, Entity(reference)); Entity(point)->GetOrientation(&d, &u, &r);
    Vec(d, 0, 0, 1); Vec(u, -1, 0, 0); Vec(r, 0, 1, 0); Vec(point.GetWorldMatrix()[3], 3, 4, 5); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetOrientationDerivesMissingRightFromDirectionAndUp) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); Prime(owner, other, point);
    const VxVector direction(1, 0, 0), up(0, 1, 0); VxVector d, u, r;
    Entity(point)->SetOrientation(&direction, &up); Entity(point)->GetOrientation(&d, &u, &r);
    Vec(d, 1, 0, 0); Vec(u, 0, 1, 0); Vec(r, 0, 0, -1); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetQuaternionKeepsScaleWhenRequestedAndReplacesOrientation) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); point.SetWorldMatrix(Pose()); Prime(owner, other, point);
    const VxQuaternion identity(0, 0, 0, 1); Entity(point)->SetQuaternion(&identity, nullptr, FALSE, TRUE);
    Vec(point.GetWorldMatrix()[0], 2, 0, 0); Vec(point.GetWorldMatrix()[1], 0, 3, 0); Vec(point.GetWorldMatrix()[2], 0, 0, 4);
    Vec(point.GetWorldMatrix()[3], 10, 20, 30); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetQuaternionReferenceModeWithoutKeepScaleUsesUnitWorldAxes) {
    RCKCurve owner(Context), other(Context); RCK3dEntity reference(Context); RCKCurvePoint point(Context);
    VxMatrix matrix = VxMatrix::Identity(); matrix[0].Set(0, 1, 0); matrix[1].Set(-1, 0, 0); reference.SetWorldMatrix(matrix);
    point.SetScale3f(2, 3, 4); Prime(owner, other, point); const VxQuaternion identity(0, 0, 0, 1);
    Entity(point)->SetQuaternion(&identity, Entity(reference), FALSE, FALSE);
    Vec(point.GetWorldMatrix()[0], 0, 1, 0); Vec(point.GetWorldMatrix()[1], -1, 0, 0); Vec(point.GetWorldMatrix()[2], 0, 0, 1); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetScaleLocalModeReplacesRatherThanMultipliesAxisLengths) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); point.SetWorldMatrix(Pose()); Prime(owner, other, point);
    const VxVector scale(5, 6, 7); Entity(point)->SetScale(&scale, FALSE, TRUE);
    Vec(point.GetLocalMatrix()[0], 0, 5, 0); Vec(point.GetLocalMatrix()[1], -6, 0, 0); Vec(point.GetLocalMatrix()[2], 0, 0, 7); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetScaleWorldModePreservesPositionAndNativeInvertibleZeroEpsilon) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); point.SetPosition3f(3, 4, 5); Prime(owner, other, point);
    const VxVector scale(0, -2, 3); Entity(point)->SetScale(&scale, FALSE, FALSE);
    Vec(point.GetWorldMatrix()[0], .000001f, 0, 0, .000000001f); Vec(point.GetWorldMatrix()[1], 0, -2, 0);
    Vec(point.GetWorldMatrix()[2], 0, 0, 3); Vec(point.GetWorldMatrix()[3], 3, 4, 5); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, ConstructWorldMatrixReturnsTrueAndAppliesIndependentPointCoordinates) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); owner.SetPosition3f(10, 20, 30); Prime(owner, other, point);
    const VxVector position(1, 2, 3), scale(2, 3, 4), input(1, 1, 1); const VxQuaternion identity(0, 0, 0, 1); VxVector result;
    ASSERT_TRUE(Entity(point)->ConstructWorldMatrix(&position, &scale, &identity)); Entity(point)->Transform(&result, &input);
    Vec(result, 3, 5, 7); Vec(point.GetLocalMatrix()[3], -9, -18, -27); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, ConstructLocalMatrixReturnsTrueAndIncludesParentWorldTranslation) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); owner.SetPosition3f(10, 20, 30); Prime(owner, other, point);
    const VxVector position(1, 2, 3), scale(2, 3, 4), input(1, 1, 1); const VxQuaternion identity(0, 0, 0, 1); VxVector result;
    ASSERT_TRUE(Entity(point)->ConstructLocalMatrix(&position, &scale, &identity)); Entity(point)->Transform(&result, &input);
    Vec(result, 13, 25, 37); Vec(point.GetLocalMatrix()[3], 1, 2, 3); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, ConstructWorldMatrixExUsesRotatedAnisotropicScaleAndReturnsTrue) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); Prime(owner, other, point);
    const VxVector position(1, 2, 3), scale(2, 4, 6), x(1, 0, 0), y(0, 1, 0);
    const VxQuaternion identity(0, 0, 0, 1), shear(0, 0, .382683432365f, .923879532511f); VxVector result;
    ASSERT_TRUE(Entity(point)->ConstructWorldMatrixEx(&position, &scale, &identity, &shear, 1));
    Entity(point)->Transform(&result, &x); Vec(result, 4, 1, 3); Entity(point)->Transform(&result, &y); Vec(result, 0, 5, 3); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, ConstructLocalMatrixExAddsParentAfterRotatedAnisotropicScale) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); owner.SetPosition3f(10, 20, 30); Prime(owner, other, point);
    const VxVector position(1, 2, 3), scale(2, 4, 6), x(1, 0, 0), y(0, 1, 0);
    const VxQuaternion identity(0, 0, 0, 1), shear(0, 0, .382683432365f, .923879532511f); VxVector result;
    ASSERT_TRUE(Entity(point)->ConstructLocalMatrixEx(&position, &scale, &identity, &shear, -1));
    Entity(point)->Transform(&result, &x); Vec(result, 14, 21, 33); Entity(point)->Transform(&result, &y); Vec(result, 10, 25, 33); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetLocalMatrixAppliesParentAndPropagatesChildUnlessKept) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); RCK3dEntity child(Context);
    owner.SetPosition3f(10, 0, 0); Prime(owner, other, point); child.SetPosition3f(2, 0, 0); ASSERT_TRUE(child.SetParent(Entity(point), TRUE));
    VxMatrix matrix = VxMatrix::Identity(); matrix[3].Set(3, 0, 0); Entity(point)->SetLocalMatrix(matrix, FALSE);
    Vec(point.GetWorldMatrix()[3], 13, 0, 0); Vec(child.GetWorldMatrix()[3], 15, 0, 0); DirtyOnlyOwner(owner, other);
    owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); matrix[3].Set(5, 0, 0); Entity(point)->SetLocalMatrix(matrix, TRUE);
    Vec(point.GetWorldMatrix()[3], 15, 0, 0); Vec(child.GetWorldMatrix()[3], 15, 0, 0); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, SetWorldMatrixKeepsChildAndRecomputesParentRelativeCoordinates) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); RCK3dEntity child(Context);
    owner.SetPosition3f(10, 20, 30); Prime(owner, other, point); child.SetPosition3f(2, 3, 4); ASSERT_TRUE(child.SetParent(Entity(point), TRUE));
    Entity(point)->SetWorldMatrix(Pose(), TRUE); Vec(point.GetLocalMatrix()[3], 0, 0, 0);
    Vec(child.GetWorldMatrix()[3], 2, 3, 4); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, InheritedThreeFloatHelpersDispatchToPointOverridesAndInvalidateOwner) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); Prime(owner, other, point);
    Entity(point)->SetPosition3f(1, 2, 3); Vec(point.GetWorldMatrix()[3], 1, 2, 3); DirtyOnlyOwner(owner, other);
    owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); Entity(point)->Translate3f(4, 5, 6); Vec(point.GetWorldMatrix()[3], 5, 7, 9); DirtyOnlyOwner(owner, other);
    owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); Entity(point)->SetScale3f(2, 3, 4); Vec(point.GetWorldMatrix()[0], 2, 0, 0); DirtyOnlyOwner(owner, other);
    owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); Entity(point)->AddScale3f(3, 2, 1); Vec(point.GetWorldMatrix()[0], 6, 0, 0); DirtyOnlyOwner(owner, other);
    owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); Entity(point)->Rotate3f(0, 0, 1, 0);
    EXPECT_TRUE(owner.IsUpToDate()); EXPECT_TRUE(other.IsUpToDate()); Vec(point.GetWorldMatrix()[0], 6, 0, 0);
}
TEST_F(CurvePointTransformTest, NonzeroInheritedRotate3fDispatchesActualRotationAndInvalidatesOnlyOwner) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); point.SetPosition3f(3, 4, 5); Prime(owner, other, point);
    Entity(point)->Rotate3f(0, 0, 1, 1.5707963267948966f);
    const VxVector x(1, 0, 0), y(0, 1, 0); VxVector result;
    Entity(point)->TransformVector(&result, &x); Vec(result, 0, -1, 0);
    Entity(point)->TransformVector(&result, &y); Vec(result, 1, 0, 0);
    Vec(point.GetWorldMatrix()[3], 3, 4, 5); DirtyOnlyOwner(owner, other);
}
TEST_F(CurvePointTransformTest, DetachedPointTransformsNormallyAndCannotInvalidateUnrelatedCurves) {
    RCKCurve owner(Context), other(Context); RCKCurvePoint point(Context); Prime(owner, other, point); point.SetCurve(nullptr);
    const VxVector position(3, 4, 5), scale(2, 3, 4); const VxQuaternion identity(0, 0, 0, 1);
    Entity(point)->SetPosition(&position); Entity(point)->SetScale(&scale);
    ASSERT_TRUE(Entity(point)->ConstructWorldMatrix(&position, &scale, &identity)); Vec(point.GetWorldMatrix()[3], 3, 4, 5);
    EXPECT_TRUE(owner.IsUpToDate()); EXPECT_TRUE(other.IsUpToDate());
}
