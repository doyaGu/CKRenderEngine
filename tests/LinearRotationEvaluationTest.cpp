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

TEST(LinearRotationEvaluationTest, EmptyControllerReturnsFalseWithoutWritingTheOutput) {
    RCKLinearRotationController controller;
    VxQuaternion output(11, -22, 33, -44);
    CKAnimController *api = &controller;
    EXPECT_FALSE(api->Evaluate(0, &output));
    RotationIs(output, VxQuaternion(11, -22, 33, -44), 0);
    EXPECT_FALSE(api->Evaluate(10, nullptr));
}

TEST(LinearRotationEvaluationTest, SingleKeyClampsWithoutNormalizingOrChangingQuaternionSign) {
    RCKLinearRotationController controller;
    const VxQuaternion stored(-2, 3, -4, -5);
    AddRotation(controller, 3, stored);
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
        RotationIs(output.Rotation, stored, 0);
        EXPECT_EQ(0x13572468u, output.Before);
        EXPECT_EQ(0xfedcba98u, output.After);
    }
}

TEST(LinearRotationEvaluationTest, UnevenIntervalsInterpolateConstantAngularSpeedWithinEachSegment) {
    RCKLinearRotationController controller;
    AddRotation(controller, 2, ZRotation(0));
    AddRotation(controller, 6, ZRotation(90));
    AddRotation(controller, 14, ZRotation(180));
    const float times[] = {-5, 2, 3, 5, 6, 8, 12, 14, 20};
    const double degrees[] = {0, 0, 22.5, 67.5, 90, 112.5, 157.5, 180, 180};
    for (int index = 0; index < 9; ++index) {
        SCOPED_TRACE(times[index]);
        VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output));
        RotationIs(output, ZRotation(degrees[index]));
    }
}

TEST(LinearRotationEvaluationTest, OriginalRuntimeRotationFixturesMatchAllThirtySixSamples) {
    int fixtureCount = 0;
    int sampleCount = 0;
    for (const LinearReference::Fixture &fixture : LinearReference::Fixtures) {
        if (!fixture.rotation)
            continue;
        SCOPED_TRACE(fixture.name);
        ++fixtureCount;
        RCKLinearRotationController controller;
        ReadRotationWire(controller, fixture.keys, fixture.keyCount);
        for (int index = 0; index < fixture.sampleCount; ++index) {
            const LinearReference::Sample &sample = fixture.samples[index];
            SCOPED_TRACE(sample.time);
            VxQuaternion output;
            ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            RotationIs(output, VxQuaternion(sample.value[0], sample.value[1],
                                           sample.value[2], sample.value[3]), 5.0e-5f);
            ++sampleCount;
        }
    }
    EXPECT_EQ(3, fixtureCount);
    EXPECT_EQ(36, sampleCount);
}

TEST(LinearRotationEvaluationTest, ExactInteriorKeyUsesPrecedingSegmentAndItsShortestArcSign) {
    RCKLinearRotationController controller;
    AddRotation(controller, 0, VxQuaternion(0, 0, 0, 1));
    AddRotation(controller, 4, VxQuaternion(0, 0, -.6f, -.8f));
    AddRotation(controller, 8, VxQuaternion(0, .8f, 0, .6f));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    RotationIs(output, VxQuaternion(0, 0, .6f, .8f));
    const CKRotationKey *stored = static_cast<CKRotationKey *>(controller.GetKey(1));
    ASSERT_NE(nullptr, stored);
    RotationIs(stored->Rot, VxQuaternion(0, 0, -.6f, -.8f), 0);
    ASSERT_TRUE(controller.Evaluate(8, &output));
    RotationIs(output, VxQuaternion(0, .8f, 0, .6f), 0);
}

TEST(LinearRotationEvaluationTest, RepeatedInteriorTimesSelectTheFirstTieThenLeaveFromTheLastTie) {
    const float keys[] = {
        0, 0, 0, 0, 1,
        4, 0, 0, 1, 0,
        4, 0, 0, 0, 1,
        8, 0, 0, .6f, .8f
    };
    RCKLinearRotationController controller;
    ReadRotationWire(controller, keys, 4);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    RotationIs(output, VxQuaternion(0, 0, 1, 0));
    ASSERT_TRUE(controller.Evaluate(6, &output));
    RotationIs(output, VxQuaternion(0, 0, .316227766f, .948683298f));
}

TEST(LinearRotationEvaluationTest, RepeatedBoundaryTimesRetainFirstAndLastRawQuaternion) {
    const float keys[] = {
        1, 0, 0, 0, 1,
        1, 0, 0, 1, 0,
        5, 0, 0, 1, 0,
        5, 0, 0, 0, -1
    };
    RCKLinearRotationController controller;
    ReadRotationWire(controller, keys, 4);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(1, &output));
    RotationIs(output, VxQuaternion(0, 0, 0, 1), 0);
    ASSERT_TRUE(controller.Evaluate(5, &output));
    RotationIs(output, VxQuaternion(0, 0, 0, -1), 0);
    ASSERT_TRUE(controller.Evaluate(3, &output));
    RotationIs(output, VxQuaternion(0, 0, 1, 0));
}

TEST(LinearRotationEvaluationTest, OppositeQuaternionSignsFollowTheShortestArc) {
    RCKLinearRotationController controller;
    AddRotation(controller, 0, VxQuaternion(0, 0, 0, 1));
    AddRotation(controller, 8, VxQuaternion(0, 0, -.6f, -.8f));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    RotationIs(output, VxQuaternion(0, 0, .316227766f, .948683298f));
    ASSERT_TRUE(controller.Evaluate(8, &output));
    RotationIs(output, VxQuaternion(0, 0, -.6f, -.8f), 0);
}

TEST(LinearRotationEvaluationTest, AntipodalKeysKeepOnePoseWhileEndpointClampRetainsStoredSign) {
    RCKLinearRotationController controller;
    AddRotation(controller, 0, VxQuaternion(.6f, 0, 0, .8f));
    AddRotation(controller, 4, VxQuaternion(-.6f, 0, 0, -.8f));
    const float times[] = {1, 2, 3};
    for (int index = 0; index < 3; ++index) {
        VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output));
        RotationIs(output, VxQuaternion(.6f, 0, 0, .8f));
    }
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    RotationIs(output, VxQuaternion(-.6f, 0, 0, -.8f), 0);
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

TEST(LinearRotationEvaluationTest, SmallFiniteTimeSpanKeepsItsHalfwayRotation) {
    RCKLinearRotationController controller;
    AddRotation(controller, -FLT_MIN, VxQuaternion(0, 0, 0, 1));
    AddRotation(controller, FLT_MIN, VxQuaternion(0, 0, 1, 0));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(0, &output));
    RotationIs(output, ZRotation(90));
}

TEST(LinearRotationEvaluationTest, SamplingDoesNotChangeKeyStorageOrControllerLength) {
    RCKLinearRotationController controller;
    AddRotation(controller, 2, VxQuaternion(0, 0, 0, 1));
    AddRotation(controller, 6, VxQuaternion(0, 0, -.6f, -.8f));
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

TEST(LinearRotationEvaluationTest, CompareRejectsNullWrongControllerTypeAndDifferentKeyCounts) {
    RCKLinearRotationController first, second;
    RCKLinearPositionController wrongType;
    EXPECT_FALSE(first.Compare(nullptr));
    EXPECT_FALSE(first.Compare(&wrongType));
    AddRotation(first, 2, VxQuaternion(0, 0, 0, 1));
    EXPECT_FALSE(first.Compare(&second));
    EXPECT_FALSE(second.Compare(&first));
}

TEST(LinearRotationEvaluationTest, CompareUsesAnInclusiveThresholdForTimeAndEachQuaternionComponent) {
    RCKLinearRotationController first, second;
    AddRotation(first, 2, VxQuaternion(1, -2, 3, -4));
    AddRotation(second, 2.25f, VxQuaternion(1.25f, -2.25f, 3.25f, -4.25f));
    EXPECT_TRUE(first.Compare(&second, .25f));
    EXPECT_TRUE(second.Compare(&first, .25f));
    const float below = std::nextafter(.25f, 0.0f);
    EXPECT_FALSE(first.Compare(&second, below));
    EXPECT_FALSE(second.Compare(&first, below));
}

TEST(LinearRotationEvaluationTest, CompareChecksEveryTimeAndQuaternionComponentOfEveryKey) {
    RCKLinearRotationController first, second;
    AddRotation(first, 2, VxQuaternion(1, -2, 3, -4));
    AddRotation(first, 6, VxQuaternion(4, -5, 6, -7));
    AddRotation(second, 2, VxQuaternion(1, -2, 3, -4));
    AddRotation(second, 6, VxQuaternion(4, -5, 6, -7));
    for (int index = 0; index < 2; ++index) {
        CKRotationKey *key = static_cast<CKRotationKey *>(second.GetKey(index));
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

TEST(LinearRotationEvaluationTest, CompareTreatsAntipodalKeysAsDistinctStoredQuaternionValues) {
    RCKLinearRotationController first, second;
    AddRotation(first, 2, VxQuaternion(.6f, 0, 0, .8f));
    AddRotation(second, 2, VxQuaternion(-.6f, 0, 0, -.8f));
    EXPECT_FALSE(first.Compare(&second, 0));
    EXPECT_FALSE(second.Compare(&first, 0));
    EXPECT_TRUE(first.Compare(&second, 1.6f));
}

TEST(LinearRotationEvaluationTest, CompareIgnoresLengthAndSupportsEmptyAndSelfComparisons) {
    RCKLinearRotationController first, second;
    first.SetLength(1);
    second.SetLength(200);
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_TRUE(first.Compare(&second, -1));
    AddRotation(first, 2, VxQuaternion(1, -2, 3, -4));
    AddRotation(second, 2, VxQuaternion(1, -2, 3, -4));
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_FALSE(first.Compare(&second, -1));
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

TEST_F(LinearRotationAnimationTest, ObjectAnimationUsesLinearRotationAndHonorsIgnoreRotation) {
    RCKObjectAnimation animation(Context);
    RCKLinearRotationController *controller = static_cast<RCKLinearRotationController *>(
        animation.CreateController(CKANIMATION_LINROT_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddRotation(*controller, 2, VxQuaternion(0, 0, 0, 1));
    AddRotation(*controller, 10, VxQuaternion(0, 0, 1, 0));
    VxQuaternion rotation;
    ASSERT_TRUE(animation.EvaluateRotation(4, rotation));
    RotationIs(rotation, ZRotation(45));
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNOREROT);
    EXPECT_FALSE(animation.EvaluateRotation(8, rotation));
    RotationIs(rotation, VxQuaternion(0, 0, 0, 1), 0);
}

TEST_F(LinearRotationAnimationTest, ObjectAnimationPlaybackPublishesRotationAndPreservesPositionAndScale) {
    RCK3dEntity entity(Context);
    VxMatrix initial = VxMatrix::Identity();
    initial[0].Set(2, 0, 0);
    initial[1].Set(0, 3, 0);
    initial[2].Set(0, 0, 4);
    initial[3].Set(5, 6, 7);
    entity.SetWorldMatrix(initial);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearRotationController *controller = static_cast<RCKLinearRotationController *>(
        animation.CreateController(CKANIMATION_LINROT_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddRotation(*controller, 0, VxQuaternion(0, 0, 0, 1));
    AddRotation(*controller, 10, VxQuaternion(0, 0, 1, 0));
    animation.SetLength(10);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_NEAR(0, local[0][0], 1.0e-5f);
    EXPECT_NEAR(-2, local[0][1], 1.0e-5f);
    EXPECT_NEAR(3, local[1][0], 1.0e-5f);
    EXPECT_NEAR(0, local[1][1], 1.0e-5f);
    EXPECT_NEAR(4, local[2][2], 1.0e-5f);
    EXPECT_FLOAT_EQ(5, local[3][0]);
    EXPECT_FLOAT_EQ(6, local[3][1]);
    EXPECT_FLOAT_EQ(7, local[3][2]);
    animation.Set3dEntity(nullptr);
}

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

TEST_F(LinearRotationAnimationTest, ObjectAnimationPlaybackComposesRotationAfterAnimatedScaleAxis) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearRotationController *controller = static_cast<RCKLinearRotationController *>(
        animation.CreateController(CKANIMATION_LINROT_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddRotation(*controller, 0, VxQuaternion(0, 0, 0, 1));
    AddRotation(*controller, 10, VxQuaternion(0, 0, 1, 0));
    VxVector position(5, 6, 7), scale(2, 3, 4);
    VxQuaternion axis = ZRotation(90);
    animation.AddPositionKey(0, &position);
    animation.AddScaleKey(0, &scale);
    animation.AddScaleAxisKey(0, &axis);
    animation.SetLength(10);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    // A 90-degree scale axis swaps x/y scales: (3,2,4). The native pose
    // helper multiplies that stretch by rotation, retaining each row scale.
    EXPECT_NEAR(0, local[0][0], 1.0e-5f);
    EXPECT_NEAR(-3, local[0][1], 1.0e-5f);
    EXPECT_NEAR(2, local[1][0], 1.0e-5f);
    EXPECT_NEAR(0, local[1][1], 1.0e-5f);
    EXPECT_NEAR(4, local[2][2], 1.0e-5f);
    EXPECT_FLOAT_EQ(5, local[3][0]);
    EXPECT_FLOAT_EQ(6, local[3][1]);
    EXPECT_FLOAT_EQ(7, local[3][2]);
    animation.Set3dEntity(nullptr);
}

} // namespace
