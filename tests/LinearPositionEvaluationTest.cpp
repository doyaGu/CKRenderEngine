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

void AddPosition(RCKLinearPositionController &controller, float time, float x, float y, float z) {
    CKPositionKey key;
    key.TimeStep = time;
    key.Pos.Set(x, y, z);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void PositionIs(const VxVector &position, float x, float y, float z) {
    EXPECT_FLOAT_EQ(x, position.x);
    EXPECT_FLOAT_EQ(y, position.y);
    EXPECT_FLOAT_EQ(z, position.z);
}

void ReadPositionWire(RCKLinearPositionController &controller, const float *keys, int count) {
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

TEST(LinearPositionEvaluationTest, EmptyControllerReturnsFalseWithoutWritingTheOutput) {
    RCKLinearPositionController controller;
    VxVector output(11.0f, -22.0f, 33.0f);
    CKAnimController *api = &controller;
    EXPECT_FALSE(api->Evaluate(0.0f, &output));
    PositionIs(output, 11.0f, -22.0f, 33.0f);
    EXPECT_FALSE(api->Evaluate(10.0f, nullptr));
}

TEST(LinearPositionEvaluationTest, SingleKeyClampsTheEntireFiniteTimelineAndWritesOnlyThreeComponents) {
    RCKLinearPositionController controller;
    AddPosition(controller, 3.0f, 2.0f, -7.0f, 11.0f);
    struct GuardedPosition {
        CKDWORD Before;
        VxVector Position;
        CKDWORD After;
    } output;
    const float samples[] = {-FLT_MAX, -1.0f, 3.0f, 20.0f, FLT_MAX};
    for (int i = 0; i < 5; ++i) {
        SCOPED_TRACE(samples[i]);
        output.Before = 0x13572468u;
        output.After = 0xfedcba98u;
        ASSERT_TRUE(controller.Evaluate(samples[i], &output.Position));
        PositionIs(output.Position, 2.0f, -7.0f, 11.0f);
        EXPECT_EQ(0x13572468u, output.Before);
        EXPECT_EQ(0xfedcba98u, output.After);
    }
}

TEST(LinearPositionEvaluationTest, UnevenIntervalsUseTheirOwnTimeSpanAndClampBothEndpoints) {
    RCKLinearPositionController controller;
    AddPosition(controller, 2.0f, 2.0f, 4.0f, -6.0f);
    AddPosition(controller, 6.0f, 10.0f, -4.0f, 2.0f);
    AddPosition(controller, 14.0f, -6.0f, 12.0f, 18.0f);
    const float samples[] = {-5.0f, 2.0f, 3.0f, 5.0f, 6.0f, 8.0f, 12.0f, 14.0f, 20.0f};
    const float expected[][3] = {
        {2, 4, -6}, {2, 4, -6}, {4, 2, -4}, {8, -2, 0}, {10, -4, 2},
        {6, 0, 6}, {-2, 8, 14}, {-6, 12, 18}, {-6, 12, 18}
    };
    for (int i = 0; i < 9; ++i) {
        SCOPED_TRACE(samples[i]);
        VxVector output;
        ASSERT_TRUE(controller.Evaluate(samples[i], &output));
        PositionIs(output, expected[i][0], expected[i][1], expected[i][2]);
    }
}

TEST(LinearPositionEvaluationTest, OriginalRuntimePositionFixturesMatchAllThirtySixSamples) {
    int checked = 0;
    for (const LinearReference::Fixture &fixture : LinearReference::Fixtures) {
        if (fixture.rotation)
            continue;
        SCOPED_TRACE(fixture.name);
        RCKLinearPositionController controller;
        ReadPositionWire(controller, fixture.keys, fixture.keyCount);
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

TEST(LinearPositionEvaluationTest, RepeatedInteriorTimesSelectTheFirstTieThenLeaveFromTheLastTie) {
    const float keys[] = {
        0, 0, 0, 0,
        4, 10, -10, 5,
        4, 20, -20, 10,
        4, 30, -30, 15,
        8, 40, -40, 20
    };
    RCKLinearPositionController controller;
    ReadPositionWire(controller, keys, 5);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(4.0f, &output));
    PositionIs(output, 10.0f, -10.0f, 5.0f);
    ASSERT_TRUE(controller.Evaluate(5.0f, &output));
    PositionIs(output, 32.5f, -32.5f, 16.25f);
}

TEST(LinearPositionEvaluationTest, RepeatedBoundaryTimesRetainFirstAndLastClampValues) {
    const float keys[] = {1, 2, 4, 6, 1, 20, 40, 60, 5, 30, 50, 70, 5, 3, 5, 7};
    RCKLinearPositionController controller;
    ReadPositionWire(controller, keys, 4);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(1.0f, &output));
    PositionIs(output, 2.0f, 4.0f, 6.0f);
    ASSERT_TRUE(controller.Evaluate(5.0f, &output));
    PositionIs(output, 3.0f, 5.0f, 7.0f);
    ASSERT_TRUE(controller.Evaluate(3.0f, &output));
    PositionIs(output, 25.0f, 45.0f, 65.0f);
}

TEST(LinearPositionEvaluationTest, OpposingFinitePositionsDoNotOverflowTheirRepresentableConvexSamples) {
    RCKLinearPositionController controller;
    AddPosition(controller, 0.0f, FLT_MAX, -FLT_MAX, FLT_MAX);
    AddPosition(controller, 4.0f, -FLT_MAX, FLT_MAX, FLT_MAX);
    const float samples[] = {1.0f, 2.0f, 3.0f};
    const float expected[] = {FLT_MAX * .5f, 0.0f, -FLT_MAX * .5f};
    for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(samples[i]);
        VxVector output;
        ASSERT_TRUE(controller.Evaluate(samples[i], &output));
        PositionIs(output, expected[i], -expected[i], FLT_MAX);
        EXPECT_TRUE(std::isfinite(output.x));
        EXPECT_TRUE(std::isfinite(output.y));
        EXPECT_TRUE(std::isfinite(output.z));
    }
}

TEST(LinearPositionEvaluationTest, OpposingFiniteKeyTimesRetainTheHalfwaySample) {
    RCKLinearPositionController controller;
    AddPosition(controller, -FLT_MAX, -8.0f, 2.0f, 20.0f);
    AddPosition(controller, FLT_MAX, 12.0f, -10.0f, 4.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(0.0f, &output));
    PositionIs(output, 2.0f, -4.0f, 12.0f);
}

TEST(LinearPositionEvaluationTest, ExactInteriorKeyDoesNotLoseItsSmallPositionToCancellation) {
    RCKLinearPositionController controller;
    AddPosition(controller, 0.0f, 1.0e30f, -1.0e30f, FLT_MAX);
    AddPosition(controller, 1.0f, 1.0f, -2.0f, 3.0f);
    AddPosition(controller, 2.0f, 7.0f, 8.0f, 9.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(1.0f, &output));
    PositionIs(output, 1.0f, -2.0f, 3.0f);
}

TEST(LinearPositionEvaluationTest, ExtremelyAsymmetricTimeSpanKeepsTheSmallEndpointContribution) {
    RCKLinearPositionController controller;
    AddPosition(controller, -1.0e30f, 1.0e30f, -2.0e30f, 0.0f);
    AddPosition(controller, 2.0f, 0.0f, 0.0f, 0.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(1.0f, &output));
    PositionIs(output, 1.0f, -2.0f, 0.0f);
}

TEST(LinearPositionEvaluationTest, SmallFiniteTimeSpanInterpolatesAtItsMidpoint) {
    RCKLinearPositionController controller;
    const float tiny = FLT_MIN;
    AddPosition(controller, -tiny, -2.0f, 4.0f, 8.0f);
    AddPosition(controller, tiny, 6.0f, -4.0f, -8.0f);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(0.0f, &output));
    PositionIs(output, 2.0f, 0.0f, 0.0f);
}

TEST(LinearPositionEvaluationTest, SamplingDoesNotChangeKeyStorageOrControllerLength) {
    RCKLinearPositionController controller;
    AddPosition(controller, 2.0f, 1.0f, 2.0f, 3.0f);
    AddPosition(controller, 6.0f, 5.0f, 6.0f, 7.0f);
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

TEST(LinearPositionEvaluationTest, CompareRejectsNullWrongControllerTypeAndDifferentKeyCounts) {
    RCKLinearPositionController first, second;
    RCKLinearScaleController wrongType;
    EXPECT_FALSE(first.Compare(nullptr));
    EXPECT_FALSE(first.Compare(&wrongType));
    AddPosition(first, 2.0f, 1.0f, 2.0f, 3.0f);
    EXPECT_FALSE(first.Compare(&second));
    EXPECT_FALSE(second.Compare(&first));
}

TEST(LinearPositionEvaluationTest, CompareUsesAnInclusivePerComponentThresholdRatherThanVectorDistance) {
    RCKLinearPositionController first, second;
    AddPosition(first, 2.0f, 1.0f, -2.0f, 3.0f);
    AddPosition(second, 2.25f, 1.25f, -2.25f, 3.25f);
    EXPECT_TRUE(first.Compare(&second, .25f));
    EXPECT_TRUE(second.Compare(&first, .25f));
    const float below = std::nextafter(.25f, 0.0f);
    EXPECT_FALSE(first.Compare(&second, below));
    EXPECT_FALSE(second.Compare(&first, below));
}

TEST(LinearPositionEvaluationTest, CompareChecksEachTimeAndPositionComponentOfEveryKey) {
    RCKLinearPositionController first, second;
    AddPosition(first, 2.0f, 1.0f, -2.0f, 3.0f);
    AddPosition(first, 6.0f, 4.0f, -5.0f, 6.0f);
    AddPosition(second, 2.0f, 1.0f, -2.0f, 3.0f);
    AddPosition(second, 6.0f, 4.0f, -5.0f, 6.0f);
    for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
        CKPositionKey *key = static_cast<CKPositionKey *>(second.GetKey(keyIndex));
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

TEST(LinearPositionEvaluationTest, CompareIgnoresLengthAndSupportsEmptyAndSelfComparisons) {
    RCKLinearPositionController first, second;
    first.SetLength(1.0f);
    second.SetLength(200.0f);
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_TRUE(first.Compare(&second, -1.0f)); // Empty sequences have no failing component.
    AddPosition(first, 2.0f, 1.0f, -2.0f, 3.0f);
    AddPosition(second, 2.0f, 1.0f, -2.0f, 3.0f);
    EXPECT_TRUE(first.Compare(&second));
    EXPECT_TRUE(first.Compare(&first));
    EXPECT_FALSE(first.Compare(&second, -1.0f));
}

class LinearPositionAnimationTest : public ::testing::Test {
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

TEST_F(LinearPositionAnimationTest, ObjectAnimationUsesLinearSamplesAndHonorsIgnorePosition) {
    RCKObjectAnimation animation(Context);
    RCKLinearPositionController *controller = static_cast<RCKLinearPositionController *>(
        animation.CreateController(CKANIMATION_LINPOS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddPosition(*controller, 2.0f, 2.0f, 4.0f, -6.0f);
    AddPosition(*controller, 10.0f, 10.0f, -4.0f, 2.0f);
    VxVector position;
    ASSERT_TRUE(animation.EvaluatePosition(4.0f, position));
    PositionIs(position, 4.0f, 2.0f, -4.0f);
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNOREPOS);
    EXPECT_FALSE(animation.EvaluatePosition(8.0f, position));
    PositionIs(position, 0.0f, 0.0f, 0.0f);
}

TEST_F(LinearPositionAnimationTest, ObjectAnimationPlaybackPublishesTheSampleToItsEntity) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearPositionController *controller = static_cast<RCKLinearPositionController *>(
        animation.CreateController(CKANIMATION_LINPOS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddPosition(*controller, 2.0f, 2.0f, 4.0f, -6.0f);
    AddPosition(*controller, 10.0f, 10.0f, -4.0f, 2.0f);
    animation.SetLength(10.0f);
    ASSERT_EQ(CK_OK, animation.SetStep(.6f));
    VxVector position;
    entity.GetPosition(&position);
    PositionIs(position, 6.0f, 0.0f, -2.0f);
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_FLOAT_EQ(1.0f, local[0][0]);
    EXPECT_FLOAT_EQ(1.0f, local[1][1]);
    EXPECT_FLOAT_EQ(1.0f, local[2][2]);
    animation.Set3dEntity(nullptr);
}

TEST_F(LinearPositionAnimationTest, ObjectAnimationPlaybackKeepsFiniteLargePositionInterpolation) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKLinearPositionController *controller = static_cast<RCKLinearPositionController *>(
        animation.CreateController(CKANIMATION_LINPOS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddPosition(*controller, 0.0f, FLT_MAX, -FLT_MAX, 8.0f);
    AddPosition(*controller, 10.0f, -FLT_MAX, FLT_MAX, -4.0f);
    animation.SetLength(10.0f);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    VxVector position;
    entity.GetPosition(&position);
    PositionIs(position, 0.0f, 0.0f, 2.0f);
    EXPECT_TRUE(std::isfinite(position.x));
    EXPECT_TRUE(std::isfinite(position.y));
    animation.Set3dEntity(nullptr);
}

} // namespace
