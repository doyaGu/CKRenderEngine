#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

class Entity3dScalePrecisionTest : public ::testing::Test {
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

    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }

    static CK3dEntity *API(RCK3dEntity *entity) {
        return reinterpret_cast<CK3dEntity *>(entity);
    }

    static void ExpectVector(const VxVector &actual, float x, float y, float z, float tolerance = 1e-6f) {
        EXPECT_NEAR(x, actual.x, tolerance);
        EXPECT_NEAR(y, actual.y, tolerance);
        EXPECT_NEAR(z, actual.z, tolerance);
    }
};

TEST_F(Entity3dScalePrecisionTest, RepeatedUnitScalePreservesRepresentableIdentityGeometry) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    const VxVector unit(1, 1, 1);
    const VxVector point(16777216, -8388608, 4194304);
    VxVector transformed;

    // All coordinates are exact powers of two. A unit scale must not move
    // this point; the tolerance permits a quarter unit in world coordinates.
    for (int iteration = 0; iteration < 16; ++iteration) {
        entity->SetScale(&unit, FALSE, TRUE);
        entity->Transform(&transformed, &point);
        ExpectVector(transformed, point.x, point.y, point.z, .25f);
    }
    ExpectVector(entity->GetWorldMatrix()[3], 0, 0, 0);
}

TEST_F(Entity3dScalePrecisionTest, NonunitRotatedAxesApplySignedTargetLengthsWithoutMovingTheOrigin) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(6, 8, 0);
    pose[1].Set(-12, 9, 0);
    pose[2].Set(0, 0, 2);
    pose[3].Set(17, -9, 4);
    entity->SetWorldMatrix(pose);

    // The original normalizes each row with a stored float reciprocal, then
    // stores the normalized components before applying the target length.
    // For the second row this makes x one float below the ideal value -24.
    const VxVector requested(-5, 30, .5f);
    entity->SetScale(&requested, FALSE, FALSE);
    ExpectVector(entity->GetWorldMatrix()[0], -3, -4, 0);
#if VXMATH_ENABLE_X87_EMULATION
    ExpectVector(entity->GetWorldMatrix()[1], -24.000001907348633f, 18, 0, 0);
#else
    ExpectVector(entity->GetWorldMatrix()[1], -24, 18, 0, 0);
#endif
    ExpectVector(entity->GetWorldMatrix()[2], 0, 0, .5f);
    ExpectVector(entity->GetWorldMatrix()[3], 17, -9, 4);

    const VxVector x(1048576, 0, 0);
    VxVector transformed;
    entity->TransformVector(&transformed, &x);
    ExpectVector(transformed, -3145728, -4194304, 0, .0625f);
    VxVector measured;
    entity->GetScale(&measured, FALSE);
    ExpectVector(measured, 5, 30, .5f, 4e-6f);
}

TEST_F(Entity3dScalePrecisionTest, LocalAndWorldScalePreserveParentCoordinatesAndKeepChildWorldPose) {
    RCK3dEntity *parent = Entity(), *entity = Entity(), *child = Entity();
    ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, child);
    VxMatrix parentPose = VxMatrix::Identity();
    parentPose[0].Set(0, 2, 0);
    parentPose[1].Set(-4, 0, 0);
    parentPose[2].Set(0, 0, .5f);
    parentPose[3].Set(10, 20, 30);
    parent->SetWorldMatrix(parentPose);
    ASSERT_TRUE(entity->SetParent(API(parent), TRUE));
    VxMatrix local = VxMatrix::Identity();
    local[3].Set(3, 5, 7);
    entity->SetLocalMatrix(local);
    ASSERT_TRUE(child->SetParent(API(entity), TRUE));
    local[3].Set(2, 0, 0);
    child->SetLocalMatrix(local);

    const VxVector localScale(2, 3, 4);
    entity->SetScale(&localScale, FALSE, TRUE);
    ExpectVector(entity->GetLocalMatrix()[0], 2, 0, 0);
    ExpectVector(entity->GetLocalMatrix()[1], 0, 3, 0);
    ExpectVector(entity->GetLocalMatrix()[2], 0, 0, 4);
    ExpectVector(entity->GetWorldMatrix()[3], -10, 26, 33.5f);
    ExpectVector(child->GetWorldMatrix()[3], -10, 34, 33.5f);

    const VxVector x(16777216, 0, 0);
    VxVector transformed;
    entity->TransformVector(&transformed, &x);
    ExpectVector(transformed, 0, 67108864, 0, 1);

    const VxMatrix childWorld = child->GetWorldMatrix();
    const VxVector worldScale(8, 6, 2);
    entity->SetScale(&worldScale, TRUE, FALSE);
    ExpectVector(entity->GetWorldMatrix()[0], 0, 8, 0);
    ExpectVector(entity->GetWorldMatrix()[1], -6, 0, 0);
    ExpectVector(entity->GetWorldMatrix()[2], 0, 0, 2);
    ExpectVector(entity->GetWorldMatrix()[3], -10, 26, 33.5f);
    ExpectVector(entity->GetLocalMatrix()[0], 4, 0, 0);
    ExpectVector(entity->GetLocalMatrix()[1], 0, 1.5f, 0);
    ExpectVector(entity->GetLocalMatrix()[2], 0, 0, 4);
    for (int row = 0; row < 4; ++row)
        ExpectVector(child->GetWorldMatrix()[row], childWorld[row].x, childWorld[row].y, childWorld[row].z);
    entity->TransformVector(&transformed, &x);
    ExpectVector(transformed, 0, 134217728, 0, 2);
}

TEST_F(Entity3dScalePrecisionTest, SmallAndZeroAxesFollowTheSelectedArithmetic) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(0, 1e-8f, 0);
    pose[1].Set(-2e-8f, 0, 0);
    pose[2].Set(0, 0, 4e-8f);
    pose[3].Set(7, 11, 13);
    const VxVector requested(2, 3, 4), point(1, 1, 1);
    VxVector transformed, measured;

    entity->SetWorldMatrix(pose);
    entity->SetScale(&requested, FALSE, FALSE);
    entity->TransformVector(&transformed, &point);
    ExpectVector(transformed, -3, 2, 4);
    entity->GetScale(&measured, FALSE);
    ExpectVector(measured, 2, 3, 4);
    ExpectVector(entity->GetWorldMatrix()[3], 7, 11, 13);

    // The original divides by a stored zero length; the non-emulated math
    // retains its finite zero-direction handling.
    pose[2].Set(0, 0, 0);
    entity->SetWorldMatrix(pose);
    entity->SetScale(&requested, FALSE, FALSE);
#if VXMATH_ENABLE_X87_EMULATION
    for (int component = 0; component < 3; ++component) {
        const float value = entity->GetWorldMatrix()[2][component];
        EXPECT_NE(value, value);
    }
#else
    entity->TransformVector(&transformed, &point);
    ExpectVector(transformed, -3, 2, 0);
    ExpectVector(entity->GetWorldMatrix()[2], 0, 0, 0);
#endif
    ExpectVector(entity->GetWorldMatrix()[3], 7, 11, 13);
}

TEST_F(Entity3dScalePrecisionTest, FiniteAxesRespectTheSelectedIntermediateRange) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(0, 1e20f, 0);
    pose[1].Set(-1e-30f, 0, 0);
    pose[2].Set(0, 0, 4e20f);
    pose[3].Set(7, 11, 13);
    entity->SetWorldMatrix(pose);

    // All input rows are finite and nonzero. Their squared lengths need
    // a wider intermediate range than float, but the requested result fits.
    const VxVector requested(2, 3, 4), point(1, 1, 1);
    entity->SetScale(&requested, FALSE, FALSE);
#if VXMATH_ENABLE_X87_EMULATION
    // Original CK2_3D.dll's normalization helper: overflowing stored squares
    // yield zero, while underflowing squares produce infinity and NaNs.
    ExpectVector(entity->GetWorldMatrix()[0], 0, 0, 0, 0);
    ExpectVector(entity->GetWorldMatrix()[2], 0, 0, 0, 0);
    const VxVector &row = entity->GetWorldMatrix()[1];
    EXPECT_LT(row.x, -1e38f);
    EXPECT_NE(row.y, row.y);
    EXPECT_NE(row.z, row.z);
    ExpectVector(entity->GetWorldMatrix()[3], 7, 11, 13);
#else
    ExpectVector(entity->GetWorldMatrix()[0], 0, 2, 0);
    ExpectVector(entity->GetWorldMatrix()[1], -3, 0, 0);
    ExpectVector(entity->GetWorldMatrix()[2], 0, 0, 4);
    ExpectVector(entity->GetWorldMatrix()[3], 7, 11, 13);
    VxVector transformed, measured;
    entity->TransformVector(&transformed, &point);
    ExpectVector(transformed, -3, 2, 4);
    entity->GetScale(&measured, FALSE);
    ExpectVector(measured, 2, 3, 4);
#endif
}
