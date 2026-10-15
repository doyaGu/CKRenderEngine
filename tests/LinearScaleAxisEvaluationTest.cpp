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

void AddAxis(RCKLinearScaleAxisController &controller, float time, const VxQuaternion &rotation) {
    CKScaleAxisKey key;
    key.TimeStep = time;
    key.Rot = rotation;
    ASSERT_GE(controller.AddKey(&key), 0);
}

VxQuaternion ZRotation(double degrees) {
    const double halfAngle = degrees * 3.14159265358979323846 / 360.0;
    return VxQuaternion(0, 0, static_cast<float>(std::sin(halfAngle)),
                        static_cast<float>(std::cos(halfAngle)));
}

void AxisIs(const VxQuaternion &actual, const VxQuaternion &expected, float tolerance = 2.0e-6f) {
    EXPECT_NEAR(expected.x, actual.x, tolerance);
    EXPECT_NEAR(expected.y, actual.y, tolerance);
    EXPECT_NEAR(expected.z, actual.z, tolerance);
    EXPECT_NEAR(expected.w, actual.w, tolerance);
}

void ReadAxisWire(RCKLinearScaleAxisController &controller, const float *keys, int count) {
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

TEST(LinearScaleAxisEvaluationTest, OpposingFiniteKeyTimesRetainTheHalfwayRotation) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddAxis(controller, FLT_MAX, VxQuaternion(0, 0, 1, 0));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(0, &output));
    AxisIs(output, ZRotation(90));
}

TEST(LinearScaleAxisEvaluationTest, WideFiniteTimelineRetainsQuarterAndThreeQuarterRotations) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddAxis(controller, FLT_MAX, VxQuaternion(0, 0, 1, 0));
    const float times[] = {-FLT_MAX * .5f, FLT_MAX * .5f};
    const double degrees[] = {45, 135};
    for (int index = 0; index < 2; ++index) {
        SCOPED_TRACE(times[index]);
        VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output));
        AxisIs(output, ZRotation(degrees[index]));
        EXPECT_TRUE(std::isfinite(output.z));
        EXPECT_TRUE(std::isfinite(output.w));
    }
}

TEST(LinearScaleAxisEvaluationTest, LargeOffsetTimelineKeepsFiniteNumeratorAndDenominator) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddAxis(controller, FLT_MAX * .5f, VxQuaternion(0, 0, 1, 0));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(FLT_MAX * .25f, &output));
    AxisIs(output, ZRotation(150));
}

class LinearScaleAxisAnimationTest : public ::testing::Test {
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

void MatrixIs(const VxMatrix &actual, const float expected[3][3]) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            SCOPED_TRACE(row * 3 + column);
            EXPECT_NEAR(expected[row][column], actual[row][column], 2.0e-5f);
        }
        EXPECT_FLOAT_EQ(0, actual[row][3]);
    }
    EXPECT_FLOAT_EQ(5, actual[3][0]);
    EXPECT_FLOAT_EQ(6, actual[3][1]);
    EXPECT_FLOAT_EQ(7, actual[3][2]);
    EXPECT_FLOAT_EQ(1, actual[3][3]);
}

void AddOtherChannels(RCKObjectAnimation &animation, const VxQuaternion &rotation) {
    VxVector position(5, 6, 7), scale(2, 4, 6);
    VxQuaternion value = rotation;
    animation.AddPositionKey(0, &position);
    animation.AddScaleKey(0, &scale);
    animation.AddRotationKey(0, &value);
}

TEST_F(LinearScaleAxisAnimationTest, WideFiniteTimelinePublishesTheMidpointStretchAxes) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleAxisController *controller = static_cast<RCKLinearScaleAxisController *>(
        animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddAxis(*controller, -FLT_MAX, VxQuaternion(0, 0, 0, 1));
    AddAxis(*controller, FLT_MAX, VxQuaternion(0, 0, 1, 0));
    AddOtherChannels(animation, VxQuaternion(0, 0, 0, 1));
    animation.SetLength(8);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    // A 90-degree midpoint axis swaps the x/y principal stretches.
    const float expected[3][3] = {{4, 0, 0}, {0, 2, 0}, {0, 0, 6}};
    MatrixIs(entity.GetLocalMatrix(), expected);
    animation.Set3dEntity(nullptr);
}

} // namespace
