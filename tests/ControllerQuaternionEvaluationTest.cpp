#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
const double Pi = 3.14159265358979323846;

VxQuaternion ZRotation(double degrees) {
    const double half = degrees * Pi / 360.0;
    return VxQuaternion(0, 0, static_cast<float>(std::sin(half)), static_cast<float>(std::cos(half)));
}

void Add(CKAnimController &controller, float time, VxQuaternion value) {
    CKRotationKey key(time, value);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void QuaternionIs(const VxQuaternion &actual, const VxQuaternion &expected) {
    EXPECT_FLOAT_EQ(expected.x, actual.x); EXPECT_FLOAT_EQ(expected.y, actual.y);
    EXPECT_NEAR(expected.z, actual.z, 2.0e-6f); EXPECT_NEAR(expected.w, actual.w, 2.0e-6f);
}

class ControllerQuaternionEvaluationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};

TEST_F(ControllerQuaternionEvaluationTest, BothLinearChannelsRetainSmallComponentsNearEitherEndpoint) {
    const float large = 1.0e30f;
    const float expected = static_cast<float>((Pi / 4) / static_cast<double>(large));
    for (int channel = 0; channel < 2; ++channel) {
        for (int reverse = 0; reverse < 2; ++reverse) {
            for (int negative = 0; negative < 2; ++negative) {
                SCOPED_TRACE(channel * 4 + reverse * 2 + negative);
                RCKLinearRotationController rotation; RCKLinearScaleAxisController axis;
                CKAnimController &controller = channel ? static_cast<CKAnimController &>(axis) : static_cast<CKAnimController &>(rotation);
                VxQuaternion first = ZRotation(reverse ? 90 : 0), last = ZRotation(reverse ? 0 : 90);
                if (negative) last = -last;
                const float start = reverse ? -large : -2.0f, end = reverse ? 2.0f : large;
                Add(controller, start, first); Add(controller, end, last);
                VxQuaternion value; ASSERT_TRUE(controller.Evaluate(reverse ? 1.0f : -1.0f, &value));
                EXPECT_FLOAT_EQ(0, value.x); EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(1, value.w);
                EXPECT_GT(value.z, 0); EXPECT_NEAR(expected, value.z, expected * 2.0e-6f);
                ASSERT_TRUE(controller.Evaluate(end, &value)); QuaternionIs(value, last);
            }
        }
    }
}

TEST_F(ControllerQuaternionEvaluationTest, ReversingTheSegmentKeepsItsOriginalHemisphere) {
    for (int channel = 0; channel < 2; ++channel) {
        RCKLinearRotationController rotation; RCKLinearScaleAxisController axis;
        CKAnimController &controller = channel ? static_cast<CKAnimController &>(axis) : static_cast<CKAnimController &>(rotation);
        Add(controller, 0, ZRotation(0)); Add(controller, 8, -ZRotation(90));
        VxQuaternion value; ASSERT_TRUE(controller.Evaluate(6, &value)); QuaternionIs(value, ZRotation(67.5));
        ASSERT_TRUE(controller.Evaluate(8, &value)); QuaternionIs(value, -ZRotation(90));
    }
}

TEST_F(ControllerQuaternionEvaluationTest, CloseAnglesKeepTheNativeUnnormalizedChord) {
    for (int channel = 0; channel < 2; ++channel) {
        RCKLinearRotationController rotation; RCKLinearScaleAxisController axis;
        CKAnimController &controller = channel ? static_cast<CKAnimController &>(axis) : static_cast<CKAnimController &>(rotation);
        Add(controller, 0, ZRotation(10)); Add(controller, 8, -ZRotation(20));
        const VxQuaternion expected(0, 0,
            static_cast<float>(.25 * std::sin(5 * Pi / 180) + .75 * std::sin(10 * Pi / 180)),
            static_cast<float>(.25 * std::cos(5 * Pi / 180) + .75 * std::cos(10 * Pi / 180)));
        VxQuaternion value; ASSERT_TRUE(controller.Evaluate(6, &value)); QuaternionIs(value, expected);
        EXPECT_LT(value.z * value.z + value.w * value.w, .999f);
    }
}

TEST_F(ControllerQuaternionEvaluationTest, OppositeFiniteTimeExtremesKeepTheMidpointAndEndpointCopies) {
    for (int channel = 0; channel < 2; ++channel) {
        RCKLinearRotationController rotation; RCKLinearScaleAxisController axis;
        CKAnimController &controller = channel ? static_cast<CKAnimController &>(axis) : static_cast<CKAnimController &>(rotation);
        Add(controller, -FLT_MAX, ZRotation(0)); Add(controller, FLT_MAX, ZRotation(90));
        VxQuaternion value; ASSERT_TRUE(controller.Evaluate(0, &value)); QuaternionIs(value, ZRotation(45));
        ASSERT_TRUE(controller.Evaluate(FLT_MAX, &value)); QuaternionIs(value, ZRotation(90));
    }
}

TEST_F(ControllerQuaternionEvaluationTest, LinearAxisPublishesTheFiniteShearFromItsSmallRemainingAngle) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    CKAnimController *controller = animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL); ASSERT_NE(nullptr, controller);
    const float large = 1.0e30f; Add(*controller, -large, ZRotation(90)); Add(*controller, 2, ZRotation(0));
    VxVector scale(large, 2 * large, 3), position(5, 6, 7); VxQuaternion rotation(0, 0, 0, 1);
    animation.AddScaleKey(0, &scale); animation.AddPositionKey(0, &position); animation.AddRotationKey(0, &rotation);
    animation.SetLength(2); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &matrix = entity.GetLocalMatrix();
    EXPECT_NEAR(-Pi / 2, matrix[0][1], 2.0e-5); EXPECT_NEAR(-Pi / 2, matrix[1][0], 2.0e-5);
    EXPECT_FLOAT_EQ(large, matrix[0][0]); EXPECT_FLOAT_EQ(2 * large, matrix[1][1]); EXPECT_FLOAT_EQ(3, matrix[2][2]);
    EXPECT_FLOAT_EQ(5, matrix[3][0]); EXPECT_FLOAT_EQ(6, matrix[3][1]); EXPECT_FLOAT_EQ(7, matrix[3][2]);
    animation.Set3dEntity(nullptr);
}

TEST_F(ControllerQuaternionEvaluationTest, LinearRotationPublishesTheFiniteRotatedBasisFromItsSmallRemainingAngle) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    CKAnimController *controller = animation.CreateController(CKANIMATION_LINROT_CONTROL); ASSERT_NE(nullptr, controller);
    const float large = 1.0e30f; Add(*controller, -large, ZRotation(90)); Add(*controller, 2, ZRotation(0));
    VxVector scale(large, 2 * large, 3), position(5, 6, 7);
    animation.AddScaleKey(0, &scale); animation.AddPositionKey(0, &position);
    animation.SetLength(2); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &matrix = entity.GetLocalMatrix();
    EXPECT_NEAR(-Pi / 2, matrix[0][1], 2.0e-5); EXPECT_NEAR(Pi, matrix[1][0], 2.0e-5);
    EXPECT_FLOAT_EQ(large, matrix[0][0]); EXPECT_FLOAT_EQ(2 * large, matrix[1][1]); EXPECT_FLOAT_EQ(3, matrix[2][2]);
    EXPECT_FLOAT_EQ(5, matrix[3][0]); EXPECT_FLOAT_EQ(6, matrix[3][1]); EXPECT_FLOAT_EQ(7, matrix[3][2]);
    animation.Set3dEntity(nullptr);
}
} // namespace
