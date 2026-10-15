#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "LinearReferenceSamples.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

void AddRotation(RCKLinearRotationController &controller, float time, const VxQuaternion &rotation) {
    CKRotationKey key;
    key.TimeStep = time;
    key.Rot = rotation;
    ASSERT_GE(controller.AddKey(&key), 0);
}

VxQuaternion ZRotation(double degrees) {
    const double halfAngle = degrees * 3.14159265358979323846 / 360.0;
    return VxQuaternion(0, 0, static_cast<float>(std::sin(halfAngle)),
                        static_cast<float>(std::cos(halfAngle)));
}

void RotationIs(const VxQuaternion &actual, const VxQuaternion &expected, float tolerance = 2.0e-6f) {
    EXPECT_NEAR(expected.x, actual.x, tolerance);
    EXPECT_NEAR(expected.y, actual.y, tolerance);
    EXPECT_NEAR(expected.z, actual.z, tolerance);
    EXPECT_NEAR(expected.w, actual.w, tolerance);
}

void ReadRotationWire(RCKLinearRotationController &controller, const float *keys, int count) {
    // Independent 2.1 wire: DWORD count, then time,x,y,z,w for each key.
    unsigned char wire[4 + 20 * 8] = {};
    ASSERT_GE(count, 0);
    ASSERT_LE(count, 8);
    const CKDWORD keyCount = static_cast<CKDWORD>(count);
    std::memcpy(wire, &keyCount, 4);
    if (count)
        std::memcpy(wire + 4, keys, 20 * count);
    ASSERT_EQ(4 + 20 * count, controller.ReadKeysFrom(wire));
}

TEST(LinearRotationEvaluationTest, OpposingFiniteKeyTimesRetainTheHalfwayRotation) {
    RCKLinearRotationController controller;
    AddRotation(controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddRotation(controller, FLT_MAX, VxQuaternion(0, 0, 1, 0));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(0, &output));
    RotationIs(output, ZRotation(90));
}

TEST(LinearRotationEvaluationTest, WideFiniteTimelineRetainsQuarterAndThreeQuarterRotations) {
    RCKLinearRotationController controller;
    AddRotation(controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddRotation(controller, FLT_MAX, VxQuaternion(0, 0, 1, 0));
    const float times[] = {-FLT_MAX * .5f, FLT_MAX * .5f};
    const double degrees[] = {45, 135};
    for (int index = 0; index < 2; ++index) {
        SCOPED_TRACE(times[index]);
        VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output));
        RotationIs(output, ZRotation(degrees[index]));
        EXPECT_TRUE(std::isfinite(output.z));
        EXPECT_TRUE(std::isfinite(output.w));
    }
}

TEST(LinearRotationEvaluationTest, LargeOffsetTimelineKeepsFiniteNumeratorAndDenominator) {
    RCKLinearRotationController controller;
    AddRotation(controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddRotation(controller, FLT_MAX * .5f, VxQuaternion(0, 0, 1, 0));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(FLT_MAX * .25f, &output));
    RotationIs(output, ZRotation(150));
}

class LinearRotationAnimationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(LinearRotationAnimationTest, ObjectAnimationPlaybackKeepsTheWideTimelineMidpointPose) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearRotationController *controller = static_cast<RCKLinearRotationController *>(
        animation.CreateController(CKANIMATION_LINROT_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddRotation(*controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddRotation(*controller, FLT_MAX, VxQuaternion(0, 0, 1, 0));
    animation.SetLength(10);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_NEAR(0, local[0][0], 1.0e-5f);
    EXPECT_NEAR(-1, local[0][1], 1.0e-5f);
    EXPECT_NEAR(1, local[1][0], 1.0e-5f);
    EXPECT_NEAR(0, local[1][1], 1.0e-5f);
    EXPECT_NEAR(1, local[2][2], 1.0e-5f);
    animation.Set3dEntity(nullptr);
}

} // namespace
