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

void AddScale(RCKLinearScaleController &controller, float time, float x, float y, float z) {
    CKScaleKey key;
    key.TimeStep = time;
    key.Pos.Set(x, y, z);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void ScaleIs(const VxVector &scale, float x, float y, float z) {
    EXPECT_FLOAT_EQ(x, scale.x);
    EXPECT_FLOAT_EQ(y, scale.y);
    EXPECT_FLOAT_EQ(z, scale.z);
}

void ReadScaleWire(RCKLinearScaleController &controller, const float *keys, int count) {
    // Independent 2.1 wire: DWORD count followed by time,x,y,z for each key.
    unsigned char wire[4 + 16 * 8] = {};
    ASSERT_GE(count, 0);
    ASSERT_LE(count, 8);
    const CKDWORD keyCount = static_cast<CKDWORD>(count);
    std::memcpy(wire, &keyCount, 4);
    if (count)
        std::memcpy(wire + 4, keys, 16 * count);
    ASSERT_EQ(4 + 16 * count, controller.ReadKeysFrom(wire));
}

TEST(LinearScaleEvaluationTest, EmptyControllerReturnsFalseWithoutWritingTheOutput) {
    RCKLinearScaleController controller;
    VxVector output(11.0f, -22.0f, 33.0f);
    CKAnimController *api = &controller;
    EXPECT_FALSE(api->Evaluate(0.0f, &output));
    ScaleIs(output, 11.0f, -22.0f, 33.0f);
    EXPECT_FALSE(api->Evaluate(10.0f, nullptr));
}

TEST(LinearScaleEvaluationTest, SingleKeyClampsTheEntireFiniteTimelineAndWritesOnlyThreeComponents) {
    RCKLinearScaleController controller;
    AddScale(controller, 3.0f, 2.0f, -7.0f, 11.0f);
    struct GuardedScale {
        CKDWORD Before;
        VxVector Scale;
        CKDWORD After;
    } output;
    const float samples[] = {-FLT_MAX, -1.0f, 3.0f, 20.0f, FLT_MAX};
    for (int i = 0; i < 5; ++i) {
        SCOPED_TRACE(samples[i]);
        output.Before = 0x13572468u;
        output.After = 0xfedcba98u;
        ASSERT_TRUE(controller.Evaluate(samples[i], &output.Scale));
        ScaleIs(output.Scale, 2.0f, -7.0f, 11.0f);
        EXPECT_EQ(0x13572468u, output.Before);
        EXPECT_EQ(0xfedcba98u, output.After);
    }
}

TEST(LinearScaleEvaluationTest, UnevenIntervalsUseTheirOwnTimeSpanAndClampBothEndpoints) {
    RCKLinearScaleController controller;
    AddScale(controller, 2.0f, 2.0f, 4.0f, -6.0f);
    AddScale(controller, 6.0f, 10.0f, -4.0f, 2.0f);
    AddScale(controller, 14.0f, -6.0f, 12.0f, 18.0f);
    const float samples[] = {-5.0f, 2.0f, 3.0f, 5.0f, 6.0f, 8.0f, 12.0f, 14.0f, 20.0f};
    const float expected[][3] = {
        {2, 4, -6}, {2, 4, -6}, {4, 2, -4}, {8, -2, 0}, {10, -4, 2},
        {6, 0, 6}, {-2, 8, 14}, {-6, 12, 18}, {-6, 12, 18}
    };
    for (int i = 0; i < 9; ++i) {
        SCOPED_TRACE(samples[i]);
        VxVector output;
        ASSERT_TRUE(controller.Evaluate(samples[i], &output));
        ScaleIs(output, expected[i][0], expected[i][1], expected[i][2]);
    }
}

TEST(LinearScaleEvaluationTest, MirroredAndCollapsedAxesInterpolateWithoutClampingOrNormalization) {
    RCKLinearScaleController controller;
    AddScale(controller, 0, -4, -2, 1);
    AddScale(controller, 8, 0, 2, 5);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(4, &output));
    ScaleIs(output, -2, 0, 3);
    ASSERT_TRUE(controller.Evaluate(6, &output));
    ScaleIs(output, -1, 1, 4);
}

TEST(LinearScaleEvaluationTest, OriginalRuntimeScaleFixturesMatchAllThirtySixSamples) {
    int checked = 0;
    for (const LinearReference::Fixture &fixture : LinearReference::Fixtures) {
        if (fixture.rotation)
            continue;
        SCOPED_TRACE(fixture.name);
        RCKLinearScaleController controller;
        ReadScaleWire(controller, fixture.keys, fixture.keyCount);
        for (int index = 0; index < fixture.sampleCount; ++index) {
            const LinearReference::Sample &sample = fixture.samples[index];
            SCOPED_TRACE(sample.time);
            VxVector output;
            ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            EXPECT_NEAR(sample.value[0], output.x, 5.0e-5f);
            EXPECT_NEAR(sample.value[1], output.y, 5.0e-5f);
            EXPECT_NEAR(sample.value[2], output.z, 5.0e-5f);
            ++checked;
        }
    }
    EXPECT_EQ(36, checked);
}

TEST(LinearScaleEvaluationTest, RepeatedInteriorTimesSelectTheFirstTieThenLeaveFromTheLastTie) {
    const float keys[] = {
        0, 0, 0, 0,
        4, 10, -10, 5,
        4, 20, -20, 10,
        4, 30, -30, 15,
        8, 40, -40, 20
    };
    RCKLinearScaleController controller;
    ReadScaleWire(controller, keys, 5);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(4.0f, &output));
    ScaleIs(output, 10.0f, -10.0f, 5.0f);
    ASSERT_TRUE(controller.Evaluate(5.0f, &output));
    ScaleIs(output, 32.5f, -32.5f, 16.25f);
}

TEST(LinearScaleEvaluationTest, RepeatedBoundaryTimesRetainFirstAndLastClampValues) {
    const float keys[] = {1, 2, 4, 6, 1, 20, 40, 60, 5, 30, 50, 70, 5, 3, 5, 7};
    RCKLinearScaleController controller;
    ReadScaleWire(controller, keys, 4);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(1.0f, &output));
    ScaleIs(output, 2.0f, 4.0f, 6.0f);
    ASSERT_TRUE(controller.Evaluate(5.0f, &output));
    ScaleIs(output, 3.0f, 5.0f, 7.0f);
    ASSERT_TRUE(controller.Evaluate(3.0f, &output));
    ScaleIs(output, 25.0f, 45.0f, 65.0f);
}

TEST(LinearScaleEvaluationTest, OpposingFiniteScalesDoNotOverflowTheirRepresentableConvexSamples) {
    RCKLinearScaleController controller;
    AddScale(controller, 0.0f, FLT_MAX, -FLT_MAX, FLT_MAX);
    AddScale(controller, 4.0f, -FLT_MAX, FLT_MAX, FLT_MAX);
    const float samples[] = {1.0f, 2.0f, 3.0f};
    const float expected[] = {FLT_MAX * .5f, 0.0f, -FLT_MAX * .5f};
    for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(samples[i]);
        VxVector output;
        ASSERT_TRUE(controller.Evaluate(samples[i], &output));
        ScaleIs(output, expected[i], -expected[i], FLT_MAX);
        EXPECT_TRUE(std::isfinite(output.x));
        EXPECT_TRUE(std::isfinite(output.y));
        EXPECT_TRUE(std::isfinite(output.z));
    }
}

TEST(LinearScaleEvaluationTest, OpposingFiniteKeyTimesRetainTheHalfwaySample) {
    RCKLinearScaleController controller;
    AddScale(controller, -FLT_MAX, -8.0f, 2.0f, 20.0f);
    AddScale(controller, FLT_MAX, 12.0f, -10.0f, 4.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(0.0f, &output));
    ScaleIs(output, 2.0f, -4.0f, 12.0f);
}

TEST(LinearScaleEvaluationTest, ExactInteriorKeyDoesNotLoseItsSmallScaleToCancellation) {
    RCKLinearScaleController controller;
    AddScale(controller, 0.0f, 1.0e30f, -1.0e30f, FLT_MAX);
    AddScale(controller, 1.0f, 1.0f, -2.0f, 3.0f);
    AddScale(controller, 2.0f, 7.0f, 8.0f, 9.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(1.0f, &output));
    ScaleIs(output, 1.0f, -2.0f, 3.0f);
}

TEST(LinearScaleEvaluationTest, ExtremelyAsymmetricTimeSpanKeepsTheSmallEndpointContribution) {
    RCKLinearScaleController controller;
    AddScale(controller, -1.0e30f, 1.0e30f, -2.0e30f, 0.0f);
    AddScale(controller, 2.0f, 0.0f, 0.0f, 0.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(1.0f, &output));
    ScaleIs(output, 1.0f, -2.0f, 0.0f);
}

TEST(LinearScaleEvaluationTest, SmallFiniteTimeSpanInterpolatesAtItsMidpoint) {
    RCKLinearScaleController controller;
    const float tiny = FLT_MIN;
    AddScale(controller, -tiny, -2.0f, 4.0f, 8.0f);
    AddScale(controller, tiny, 6.0f, -4.0f, -8.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(0.0f, &output));
    ScaleIs(output, 2.0f, 0.0f, 0.0f);
}

TEST(LinearScaleEvaluationTest, SamplingDoesNotChangeKeyStorageOrControllerLength) {
    RCKLinearScaleController controller;
    AddScale(controller, 2.0f, 1.0f, 2.0f, 3.0f);
    AddScale(controller, 6.0f, 5.0f, 6.0f, 7.0f);
    controller.SetLength(42.0f);
    unsigned char before[36], after[36];
    ASSERT_EQ(36, controller.DumpKeysTo(before));
    CKKey *first = controller.GetKey(0);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(4.0f, &output));
    ASSERT_EQ(36, controller.DumpKeysTo(after));
    EXPECT_EQ(0, std::memcmp(before, after, sizeof(before)));
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(42.0f, controller.GetLength());
}

TEST(LinearScaleEvaluationTest, CompareRejectsNullWrongControllerTypeAndDifferentKeyCounts) {
    RCKLinearScaleController first, second;
    RCKLinearPositionController wrongType;
    EXPECT_FALSE(first.Compare(nullptr));
    EXPECT_FALSE(first.Compare(&wrongType));
    AddScale(first, 2.0f, 1.0f, 2.0f, 3.0f);
    EXPECT_FALSE(first.Compare(&second));
    EXPECT_FALSE(second.Compare(&first));
}

TEST(LinearScaleEvaluationTest, CompareUsesAnInclusivePerComponentThresholdRatherThanVectorDistance) {
    RCKLinearScaleController first, second;
    AddScale(first, 2.0f, 1.0f, -2.0f, 3.0f);
    AddScale(second, 2.25f, 1.25f, -2.25f, 3.25f);
    EXPECT_TRUE(first.Compare(&second, .25f));
    EXPECT_TRUE(second.Compare(&first, .25f));
    const float below = std::nextafter(.25f, 0.0f);
    EXPECT_FALSE(first.Compare(&second, below));
    EXPECT_FALSE(second.Compare(&first, below));
}

TEST(LinearScaleEvaluationTest, CompareChecksEachTimeAndScaleComponentOfEveryKey) {
    RCKLinearScaleController first, second;
    AddScale(first, 2.0f, 1.0f, -2.0f, 3.0f);
    AddScale(first, 6.0f, 4.0f, -5.0f, 6.0f);
    AddScale(second, 2.0f, 1.0f, -2.0f, 3.0f);
    AddScale(second, 6.0f, 4.0f, -5.0f, 6.0f);
    for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
        CKScaleKey *key = static_cast<CKScaleKey *>(second.GetKey(keyIndex));
        float *components[] = {&key->TimeStep, &key->Pos.x, &key->Pos.y, &key->Pos.z};
        for (int field = 0; field < 4; ++field) {
            SCOPED_TRACE(keyIndex * 4 + field);
            *components[field] += .5f;
            EXPECT_FALSE(first.Compare(&second, .25f));
            EXPECT_FALSE(second.Compare(&first, .25f));
            *components[field] -= .5f;
            EXPECT_TRUE(first.Compare(&second, 0.0f));
        }
    }
}

TEST(LinearScaleEvaluationTest, CompareIgnoresLengthAndSupportsEmptyAndSelfComparisons) {
    RCKLinearScaleController first, second;
    first.SetLength(1.0f);
    second.SetLength(200.0f);
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_TRUE(first.Compare(&second, -1.0f)); // Empty sequences have no failing component.
    AddScale(first, 2.0f, 1.0f, -2.0f, 3.0f);
    AddScale(second, 2.0f, 1.0f, -2.0f, 3.0f);
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_FALSE(first.Compare(&second, -1.0f));
}

class LinearScaleAnimationTest : public ::testing::Test {
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

TEST_F(LinearScaleAnimationTest, ObjectAnimationUsesLinearSamplesAndHonorsIgnoreScale) {
    RCKObjectAnimation animation(Context);
    RCKLinearScaleController *controller = static_cast<RCKLinearScaleController *>(
        animation.CreateController(CKANIMATION_LINSCL_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddScale(*controller, 2.0f, 2.0f, 4.0f, -6.0f);
    AddScale(*controller, 10.0f, 10.0f, -4.0f, 2.0f);
    VxVector scale;
    ASSERT_TRUE(animation.EvaluateScale(4.0f, scale));
    ScaleIs(scale, 4.0f, 2.0f, -4.0f);
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNORESCALE);
    EXPECT_FALSE(animation.EvaluateScale(8.0f, scale));
    ScaleIs(scale, 0.0f, 0.0f, 0.0f);
}

TEST_F(LinearScaleAnimationTest, ObjectAnimationPlaybackPublishesNonuniformScaleAndPreservesTranslation) {
    RCK3dEntity entity(Context);
    VxMatrix initial = VxMatrix::Identity();
    initial[3].Set(5, 6, 7);
    entity.SetWorldMatrix(initial);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleController *controller = static_cast<RCKLinearScaleController *>(
        animation.CreateController(CKANIMATION_LINSCL_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddScale(*controller, 2, 2, 4, 6);
    AddScale(*controller, 10, 10, 12, 14);
    animation.SetLength(10);
    ASSERT_EQ(CK_OK, animation.SetStep(.6f));
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_FLOAT_EQ(6, local[0][0]);
    EXPECT_FLOAT_EQ(8, local[1][1]);
    EXPECT_FLOAT_EQ(10, local[2][2]);
    EXPECT_FLOAT_EQ(5, local[3][0]);
    EXPECT_FLOAT_EQ(6, local[3][1]);
    EXPECT_FLOAT_EQ(7, local[3][2]);
    animation.Set3dEntity(nullptr);
}

TEST_F(LinearScaleAnimationTest, ObjectAnimationPlaybackKeepsFiniteLargeScaleInterpolation) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleController *controller = static_cast<RCKLinearScaleController *>(
        animation.CreateController(CKANIMATION_LINSCL_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddScale(*controller, 0, FLT_MAX, -FLT_MAX, 8);
    AddScale(*controller, 10, -FLT_MAX, FLT_MAX, -4);
    animation.SetLength(10);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_FLOAT_EQ(0, local[0][0]);
    EXPECT_FLOAT_EQ(0, local[1][1]);
    EXPECT_FLOAT_EQ(2, local[2][2]);
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_TRUE(std::isfinite(local[row][column]));
    animation.Set3dEntity(nullptr);
}

TEST_F(LinearScaleAnimationTest, ObjectAnimationPlaybackRetainsMirroringAndCollapsedAxes) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleController *controller = static_cast<RCKLinearScaleController *>(
        animation.CreateController(CKANIMATION_LINSCL_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddScale(*controller, 0, -4, -2, 1);
    AddScale(*controller, 10, 0, 2, 5);
    animation.SetLength(10);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_FLOAT_EQ(-2, local[0][0]);
    EXPECT_FLOAT_EQ(0, local[1][1]);
    EXPECT_FLOAT_EQ(3, local[2][2]);
    EXPECT_FLOAT_EQ(0, local[0][1]);
    EXPECT_FLOAT_EQ(0, local[1][2]);
    EXPECT_FLOAT_EQ(0, local[2][0]);
    animation.Set3dEntity(nullptr);
}

TEST_F(LinearScaleAnimationTest, AnimatedScaleComposesWithRotationAndScaleAxisInEntityMatrix) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearScaleController *controller = static_cast<RCKLinearScaleController *>(
        animation.CreateController(CKANIMATION_LINSCL_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddScale(*controller, 0, 1, 2, 3);
    AddScale(*controller, 10, 3, 4, 5);
    VxVector translation(5, 6, 7);
    const float halfSqrt = static_cast<float>(std::sqrt(.5));
    VxQuaternion rotation(0, 0, halfSqrt, halfSqrt);
    VxQuaternion scaleAxis(0, 0, halfSqrt, halfSqrt);
    animation.AddPositionKey(0, &translation);
    animation.AddRotationKey(0, &rotation);
    animation.AddScaleAxisKey(0, &scaleAxis);
    animation.SetLength(10);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    // At the midpoint scale is (2,3,4). A quarter-turn scale axis swaps
    // x/y stretch, then the object quarter-turn rotates rows (3,2,4).
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
