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

TEST(LinearScaleAxisEvaluationTest, EmptyControllerReturnsFalseWithoutWritingTheOutput) {
    RCKLinearScaleAxisController controller;
    VxQuaternion output(11, -22, 33, -44);
    CKAnimController *api = &controller;
    EXPECT_FALSE(api->Evaluate(0, &output));
    AxisIs(output, VxQuaternion(11, -22, 33, -44), 0);
    EXPECT_FALSE(api->Evaluate(10, nullptr));
}

TEST(LinearScaleAxisEvaluationTest, SingleKeyClampsWithoutNormalizingOrChangingQuaternionSign) {
    RCKLinearScaleAxisController controller;
    const VxQuaternion stored(-2, 3, -4, -5);
    AddAxis(controller, 3, stored);
    struct GuardedRotation {
        CKDWORD Before;
        VxQuaternion Rotation;
        CKDWORD After;
    } output;
    const float times[] = {-FLT_MAX, -1, 3, 20, FLT_MAX};
    for (int index = 0; index < 5; ++index) {
        SCOPED_TRACE(times[index]);
        output.Before = 0x13572468u;
        output.After = 0xfedcba98u;
        ASSERT_TRUE(controller.Evaluate(times[index], &output.Rotation));
        AxisIs(output.Rotation, stored, 0);
        EXPECT_EQ(0x13572468u, output.Before);
        EXPECT_EQ(0xfedcba98u, output.After);
    }
}

TEST(LinearScaleAxisEvaluationTest, UnevenIntervalsInterpolateConstantAngularSpeedWithinEachSegment) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, 2, ZRotation(0));
    AddAxis(controller, 6, ZRotation(90));
    AddAxis(controller, 14, ZRotation(180));
    const float times[] = {-5, 2, 3, 5, 6, 8, 12, 14, 20};
    const double degrees[] = {0, 0, 22.5, 67.5, 90, 112.5, 157.5, 180, 180};
    for (int index = 0; index < 9; ++index) {
        SCOPED_TRACE(times[index]);
        VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output));
        AxisIs(output, ZRotation(degrees[index]));
    }
}

TEST(LinearScaleAxisEvaluationTest, OriginalRuntimeScaleAxisFixturesMatchAllThirtySixSamples) {
    int fixtureCount = 0;
    int sampleCount = 0;
    for (const LinearReference::Fixture &fixture : LinearReference::Fixtures) {
        if (!fixture.rotation)
            continue;
        SCOPED_TRACE(fixture.name);
        ++fixtureCount;
        RCKLinearScaleAxisController controller;
        ReadAxisWire(controller, fixture.keys, fixture.keyCount);
        for (int index = 0; index < fixture.sampleCount; ++index) {
            const LinearReference::Sample &sample = fixture.samples[index];
            SCOPED_TRACE(sample.time);
            VxQuaternion output;
            ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            AxisIs(output, VxQuaternion(sample.value[0], sample.value[1],
                                           sample.value[2], sample.value[3]), 5.0e-5f);
            ++sampleCount;
        }
    }
    EXPECT_EQ(3, fixtureCount);
    EXPECT_EQ(36, sampleCount);
}

TEST(LinearScaleAxisEvaluationTest, ExactInteriorKeyUsesPrecedingSegmentAndItsShortestArcSign) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, 0, VxQuaternion(0, 0, 0, 1));
    AddAxis(controller, 4, VxQuaternion(0, 0, -.6f, -.8f));
    AddAxis(controller, 8, VxQuaternion(0, .8f, 0, .6f));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    AxisIs(output, VxQuaternion(0, 0, .6f, .8f));
    const CKScaleAxisKey *stored = static_cast<CKScaleAxisKey *>(controller.GetKey(1));
    ASSERT_NE(nullptr, stored);
    AxisIs(stored->Rot, VxQuaternion(0, 0, -.6f, -.8f), 0);
    ASSERT_TRUE(controller.Evaluate(8, &output));
    AxisIs(output, VxQuaternion(0, .8f, 0, .6f), 0);
}

TEST(LinearScaleAxisEvaluationTest, RepeatedInteriorTimesSelectTheFirstTieThenLeaveFromTheLastTie) {
    const float keys[] = {
        0, 0, 0, 0, 1,
        4, 0, 0, 1, 0,
        4, 0, 0, 0, 1,
        8, 0, 0, .6f, .8f
    };
    RCKLinearScaleAxisController controller;
    ReadAxisWire(controller, keys, 4);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    AxisIs(output, VxQuaternion(0, 0, 1, 0));
    ASSERT_TRUE(controller.Evaluate(6, &output));
    AxisIs(output, VxQuaternion(0, 0, .316227766f, .948683298f));
}

TEST(LinearScaleAxisEvaluationTest, RepeatedBoundaryTimesRetainFirstAndLastRawQuaternion) {
    const float keys[] = {
        1, 0, 0, 0, 1,
        1, 0, 0, 1, 0,
        5, 0, 0, 1, 0,
        5, 0, 0, 0, -1
    };
    RCKLinearScaleAxisController controller;
    ReadAxisWire(controller, keys, 4);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(1, &output));
    AxisIs(output, VxQuaternion(0, 0, 0, 1), 0);
    ASSERT_TRUE(controller.Evaluate(5, &output));
    AxisIs(output, VxQuaternion(0, 0, 0, -1), 0);
    ASSERT_TRUE(controller.Evaluate(3, &output));
    AxisIs(output, VxQuaternion(0, 0, 1, 0));
}

TEST(LinearScaleAxisEvaluationTest, OppositeQuaternionSignsFollowTheShortestArc) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, 0, VxQuaternion(0, 0, 0, 1));
    AddAxis(controller, 8, VxQuaternion(0, 0, -.6f, -.8f));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    AxisIs(output, VxQuaternion(0, 0, .316227766f, .948683298f));
    ASSERT_TRUE(controller.Evaluate(8, &output));
    AxisIs(output, VxQuaternion(0, 0, -.6f, -.8f), 0);
}

TEST(LinearScaleAxisEvaluationTest, AntipodalKeysKeepOnePoseWhileEndpointClampRetainsStoredSign) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, 0, VxQuaternion(.6f, 0, 0, .8f));
    AddAxis(controller, 4, VxQuaternion(-.6f, 0, 0, -.8f));
    const float times[] = {1, 2, 3};
    for (int index = 0; index < 3; ++index) {
        VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output));
        AxisIs(output, VxQuaternion(.6f, 0, 0, .8f));
    }
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    AxisIs(output, VxQuaternion(-.6f, 0, 0, -.8f), 0);
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

TEST(LinearScaleAxisEvaluationTest, SmallFiniteTimeSpanKeepsItsHalfwayRotation) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, -FLT_MIN, VxQuaternion(0, 0, 0, 1));
    AddAxis(controller, FLT_MIN, VxQuaternion(0, 0, 1, 0));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(0, &output));
    AxisIs(output, ZRotation(90));
}

TEST(LinearScaleAxisEvaluationTest, SamplingDoesNotChangeKeyStorageOrControllerLength) {
    RCKLinearScaleAxisController controller;
    AddAxis(controller, 2, VxQuaternion(0, 0, 0, 1));
    AddAxis(controller, 6, VxQuaternion(0, 0, -.6f, -.8f));
    controller.SetLength(42);
    unsigned char before[44], after[44];
    ASSERT_EQ(44, controller.DumpKeysTo(before));
    CKKey *first = controller.GetKey(0);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    ASSERT_EQ(44, controller.DumpKeysTo(after));
    EXPECT_EQ(0, std::memcmp(before, after, sizeof(before)));
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(42, controller.GetLength());
}

TEST(LinearScaleAxisEvaluationTest, CompareRejectsNullWrongControllerTypeAndDifferentKeyCounts) {
    RCKLinearScaleAxisController first, second;
    RCKLinearPositionController wrongType;
    RCKLinearRotationController wrongQuaternionType;
    EXPECT_FALSE(first.Compare(nullptr));
    EXPECT_FALSE(first.Compare(&wrongType));
    EXPECT_FALSE(first.Compare(&wrongQuaternionType));
    AddAxis(first, 2, VxQuaternion(0, 0, 0, 1));
    EXPECT_FALSE(first.Compare(&second));
    EXPECT_FALSE(second.Compare(&first));
}

TEST(LinearScaleAxisEvaluationTest, CompareUsesAnInclusiveThresholdForTimeAndEachQuaternionComponent) {
    RCKLinearScaleAxisController first, second;
    AddAxis(first, 2, VxQuaternion(1, -2, 3, -4));
    AddAxis(second, 2.25f, VxQuaternion(1.25f, -2.25f, 3.25f, -4.25f));
    EXPECT_TRUE(first.Compare(&second, .25f));
    EXPECT_TRUE(second.Compare(&first, .25f));
    const float below = std::nextafter(.25f, 0.0f);
    EXPECT_FALSE(first.Compare(&second, below));
    EXPECT_FALSE(second.Compare(&first, below));
}

TEST(LinearScaleAxisEvaluationTest, CompareChecksEveryTimeAndQuaternionComponentOfEveryKey) {
    RCKLinearScaleAxisController first, second;
    AddAxis(first, 2, VxQuaternion(1, -2, 3, -4));
    AddAxis(first, 6, VxQuaternion(4, -5, 6, -7));
    AddAxis(second, 2, VxQuaternion(1, -2, 3, -4));
    AddAxis(second, 6, VxQuaternion(4, -5, 6, -7));
    for (int index = 0; index < 2; ++index) {
        CKScaleAxisKey *key = static_cast<CKScaleAxisKey *>(second.GetKey(index));
        float *components[] = {&key->TimeStep, &key->Rot.x, &key->Rot.y, &key->Rot.z, &key->Rot.w};
        for (int component = 0; component < 5; ++component) {
            SCOPED_TRACE(index * 5 + component);
            *components[component] += .5f;
            EXPECT_FALSE(first.Compare(&second, .25f));
            EXPECT_FALSE(second.Compare(&first, .25f));
            *components[component] -= .5f;
            EXPECT_TRUE(first.Compare(&second, 0));
        }
    }
}

TEST(LinearScaleAxisEvaluationTest, CompareTreatsAntipodalKeysAsDistinctStoredQuaternionValues) {
    RCKLinearScaleAxisController first, second;
    AddAxis(first, 2, VxQuaternion(.6f, 0, 0, .8f));
    AddAxis(second, 2, VxQuaternion(-.6f, 0, 0, -.8f));
    EXPECT_FALSE(first.Compare(&second, 0));
    EXPECT_FALSE(second.Compare(&first, 0));
    EXPECT_TRUE(first.Compare(&second, 1.6f));
}

TEST(LinearScaleAxisEvaluationTest, CompareIgnoresLengthAndSupportsEmptyAndSelfComparisons) {
    RCKLinearScaleAxisController first, second;
    first.SetLength(1);
    second.SetLength(200);
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_TRUE(first.Compare(&second, -1));
    AddAxis(first, 2, VxQuaternion(1, -2, 3, -4));
    AddAxis(second, 2, VxQuaternion(1, -2, 3, -4));
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_FALSE(first.Compare(&second, -1));
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

TEST_F(LinearScaleAxisAnimationTest, ObjectAnimationUsesScaleAxisControllerAndHonorsIgnoreScaleRotation) {
    RCKObjectAnimation animation(Context);
    RCKLinearScaleAxisController *controller = static_cast<RCKLinearScaleAxisController *>(
        animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    EXPECT_EQ(controller, animation.GetScaleAxisController());
    AddAxis(*controller, 2, VxQuaternion(0, 0, 0, 1));
    AddAxis(*controller, 10, VxQuaternion(0, 0, 1, 0));
    VxQuaternion axis;
    ASSERT_TRUE(animation.EvaluateScaleAxis(4, axis));
    AxisIs(axis, ZRotation(45));
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNORESCALEROT);
    EXPECT_FALSE(animation.EvaluateScaleAxis(8, axis));
    AxisIs(axis, ZRotation(45));
}

TEST_F(LinearScaleAxisAnimationTest, AnimatedScaleAxisRotatesUnequalStretchAxesWithoutRotatingTheObject) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleAxisController *controller = static_cast<RCKLinearScaleAxisController *>(
        animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddAxis(*controller, 0, VxQuaternion(0, 0, 0, 1));
    AddAxis(*controller, 8, ZRotation(90));
    AddOtherChannels(animation, VxQuaternion(0, 0, 0, 1));
    animation.SetLength(8);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    // Rotating the principal (2,4,6) stretch axes by 45 degrees gives
    // a symmetric stretch with a nonzero xy shear, rather than a pose rotation.
    const float expected[3][3] = {{3, -1, 0}, {-1, 3, 0}, {0, 0, 6}};
    MatrixIs(entity.GetLocalMatrix(), expected);
    animation.Set3dEntity(nullptr);
}

TEST_F(LinearScaleAxisAnimationTest, AnimatedScaleAxisStretchPrecedesNoncommutingObjectRotation) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleAxisController *controller = static_cast<RCKLinearScaleAxisController *>(
        animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddAxis(*controller, 0, VxQuaternion(0, 0, 0, 1));
    AddAxis(*controller, 8, ZRotation(90));
    const float halfSqrt = static_cast<float>(std::sqrt(.5));
    AddOtherChannels(animation, VxQuaternion(halfSqrt, 0, 0, halfSqrt));
    animation.SetLength(8);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    // Independent product of the 45-degree symmetric xy stretch by a
    // 90-degree x rotation: the cross-axis entries distinguish the order.
    const float expected[3][3] = {{3, 0, 1}, {-1, 0, -3}, {0, 6, 0}};
    MatrixIs(entity.GetLocalMatrix(), expected);
    animation.Set3dEntity(nullptr);
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

TEST_F(LinearScaleAxisAnimationTest, AntipodalScaleAxisSignsProduceTheSamePhysicalStretch) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleAxisController *controller = static_cast<RCKLinearScaleAxisController *>(
        animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    const VxQuaternion positive = ZRotation(45);
    const VxQuaternion negative(-positive.x, -positive.y, -positive.z, -positive.w);
    AddAxis(*controller, 0, positive);
    AddAxis(*controller, 8, negative);
    AddOtherChannels(animation, VxQuaternion(0, 0, 0, 1));
    animation.SetLength(8);
    const float steps[] = {0, .25f, .5f, .75f, 1};
    const float expected[3][3] = {{3, -1, 0}, {-1, 3, 0}, {0, 0, 6}};
    for (int index = 0; index < 5; ++index) {
        SCOPED_TRACE(steps[index]);
        ASSERT_EQ(CK_OK, animation.SetStep(steps[index]));
        MatrixIs(entity.GetLocalMatrix(), expected);
    }
    animation.Set3dEntity(nullptr);
}

TEST_F(LinearScaleAxisAnimationTest, IgnoringScaleAxisUsesOrdinaryScaleWithoutChangingStoredKeys) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleAxisController *controller = static_cast<RCKLinearScaleAxisController *>(
        animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddAxis(*controller, 0, VxQuaternion(0, 0, 0, 1));
    AddAxis(*controller, 8, ZRotation(90));
    AddOtherChannels(animation, VxQuaternion(0, 0, 0, 1));
    animation.SetLength(8);
    unsigned char before[44], after[44];
    ASSERT_EQ(44, controller->DumpKeysTo(before));
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNORESCALEROT);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const float expected[3][3] = {{2, 0, 0}, {0, 4, 0}, {0, 0, 6}};
    MatrixIs(entity.GetLocalMatrix(), expected);
    ASSERT_EQ(44, controller->DumpKeysTo(after));
    EXPECT_EQ(0, std::memcmp(before, after, sizeof(before)));
    animation.Set3dEntity(nullptr);
}

} // namespace
