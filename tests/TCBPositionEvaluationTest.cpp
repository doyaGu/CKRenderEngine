#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "TCBReferenceSamples.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
thread_local bool FailNextTangentArray = false;

class InspectTCBPosition : public RCKTCBPositionController {
public:
    const VxVector *Cache() const { return m_Tangents; }
};

void AddPosition(RCKTCBPositionController &controller, float time, float x, float y = 0, float z = 0,
                 float tension = 0, float continuity = 0, float bias = 0,
                 float easeTo = 0, float easeFrom = 0) {
    VxVector position(x, y, z);
    CKTCBPositionKey key(time, position, tension, continuity, bias, easeTo, easeFrom);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void PositionIs(const VxVector &position, float x, float y = 0, float z = 0) {
    EXPECT_FLOAT_EQ(x, position.x); EXPECT_FLOAT_EQ(y, position.y); EXPECT_FLOAT_EQ(z, position.z);
}

void ReadWire(RCKTCBPositionController &controller, const float *keys, int count) {
    // 2.1 wire: DWORD count, then time,x,y,z,T,C,B,easeto,easefrom.
    unsigned char wire[4 + 36 * 8] = {};
    ASSERT_GE(count, 0); ASSERT_LE(count, 8);
    std::memcpy(wire, &count, 4);
    if (count) std::memcpy(wire + 4, keys, 36 * count);
    ASSERT_EQ(4 + 36 * count, controller.ReadKeysFrom(wire));
}

class TCBPositionEvaluationTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailNextTangentArray = false; }
};
}

// A real failed cache allocation must leave the observable curve/output intact.
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextTangentArray) { FailNextTangentArray = false; return nullptr; }
    try { return ::operator new[](bytes); }
    catch (const std::bad_alloc &) { return nullptr; }
}

namespace {
TEST_F(TCBPositionEvaluationTest, EmptyControllerReturnsFalseWithoutWritingOutputOrAllocating) {
    InspectTCBPosition controller; VxVector output(11, -22, 33);
    FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(0, &output)); PositionIs(output, 11, -22, 33);
    EXPECT_FALSE(controller.Evaluate(10, nullptr)); EXPECT_TRUE(FailNextTangentArray);
    EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBPositionEvaluationTest, SingleKeyClampsWithoutAllocatingAndWritesExactlyThreeComponents) {
    InspectTCBPosition controller; AddPosition(controller, 3, 2, -7, 11);
    struct Guarded { CKDWORD Before; VxVector Value; CKDWORD After; } output;
    const float times[] = {-FLT_MAX, -1, 3, 20, FLT_MAX};
    FailNextTangentArray = true;
    for (float time : times) {
        SCOPED_TRACE(time); output.Before = 0x13572468; output.After = 0xfedcba98;
        ASSERT_TRUE(controller.Evaluate(time, &output.Value)); PositionIs(output.Value, 2, -7, 11);
        EXPECT_EQ(0x13572468u, output.Before); EXPECT_EQ(0xfedcba98u, output.After);
    }
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBPositionEvaluationTest, TwoKeysUseEndpointTensionAndIgnoreContinuityAndBias) {
    InspectTCBPosition controller;
    AddPosition(controller, 2, 0, 0, 0, .5f, .8f, -.7f);
    AddPosition(controller, 10, 8, 0, 0, -.5f, -.9f, .6f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(6, &output)); PositionIs(output, 3);
    ASSERT_NE(nullptr, controller.Cache()); PositionIs(controller.Cache()[1], 4); PositionIs(controller.Cache()[2], 12);
    ASSERT_TRUE(controller.Evaluate(4, &output)); PositionIs(output, 1.25f);
}

TEST_F(TCBPositionEvaluationTest, UnevenIntervalsUseNativeIncomingEndpointRule) {
    InspectTCBPosition controller;
    AddPosition(controller, 0, 0); AddPosition(controller, 1, 4); AddPosition(controller, 4, 10);
    VxVector output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); PositionIs(output, 2.28125f);
    ASSERT_NE(nullptr, controller.Cache());
    PositionIs(controller.Cache()[1], 4.75f); PositionIs(controller.Cache()[2], 2.5f);
    PositionIs(controller.Cache()[3], 7.5f); PositionIs(controller.Cache()[4], 10.25f);
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); PositionIs(output, 6.65625f);
}

TEST_F(TCBPositionEvaluationTest, NonzeroContinuityAndBiasWeightIncomingAndOutgoingTangentsIndependently) {
    InspectTCBPosition controller;
    AddPosition(controller, 0, 0); AddPosition(controller, 1, 4, 0, 0, .25f, .5f, -.5f); AddPosition(controller, 4, 10);
    VxVector output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); PositionIs(output, 1.9853515625f);
    PositionIs(controller.Cache()[2], 4.078125f); PositionIs(controller.Cache()[3], 3.515625f);
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); PositionIs(output, 6.0595703125f);
}

TEST_F(TCBPositionEvaluationTest, EaseHasQuadraticEndsAndLinearMiddleBeforeHermite) {
    RCKTCBPositionController controller;
    AddPosition(controller, 0, 0, 0, 0, 1, 0, 0, .9f, .25f);
    AddPosition(controller, 8, 10, 0, 0, 1, 0, 0, .5f, .9f);
    const float times[] = {1, 2, 3, 4, 6};
    // Remapped t = .05,.2,.4,.6,.9; zero tangents give 10*(3*t^2-2*t^3).
    const float expected[] = {.0725f, 1.04f, 3.52f, 6.48f, 9.72f};
    for (int index = 0; index < 5; ++index) {
        SCOPED_TRACE(times[index]); VxVector output; ASSERT_TRUE(controller.Evaluate(times[index], &output));
        EXPECT_NEAR(expected[index], output.x, 2.0e-6f); EXPECT_FLOAT_EQ(0, output.y); EXPECT_FLOAT_EQ(0, output.z);
    }
}

TEST_F(TCBPositionEvaluationTest, EaseAboveOneNormalizesBothEndsTogether) {
    RCKTCBPositionController controller;
    AddPosition(controller, 0, 0, 0, 0, 1, 0, 0, 0, 2);
    AddPosition(controller, 8, 16, 0, 0, 1, 0, 0, 2, 0);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); PositionIs(output, .6875f);
    ASSERT_TRUE(controller.Evaluate(6, &output)); PositionIs(output, 15.3125f);
}

TEST_F(TCBPositionEvaluationTest, OriginalRuntimeVectorFixturesMatchAllTwentyNineSamples) {
    int fixtures = 0, samples = 0;
    for (const TCBReference::Fixture &fixture : TCBReference::Fixtures) {
        if (fixture.rotation) continue;
        SCOPED_TRACE(fixture.name); ++fixtures;
        RCKTCBPositionController controller; ReadWire(controller, fixture.keys, fixture.keyCount);
        for (int index = 0; index < fixture.sampleCount; ++index) {
            const TCBReference::Sample &sample = fixture.samples[index]; SCOPED_TRACE(sample.time);
            VxVector output; ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            EXPECT_NEAR(sample.value[0], output.x, 5.0e-5f); EXPECT_NEAR(sample.value[1], output.y, 5.0e-5f);
            EXPECT_NEAR(sample.value[2], output.z, 5.0e-5f); ++samples;
        }
    }
    EXPECT_EQ(3, fixtures); EXPECT_EQ(29, samples);
}

TEST_F(TCBPositionEvaluationTest, DuplicateInteriorTimesSelectFirstTieThenLeaveFromLastTie) {
    const float keys[] = {
        0, 0, 0, 0, 1, 0, 0, 0, 0,
        4, 10, -10, 5, 1, 0, 0, 0, 0,
        4, 20, -20, 10, 1, 0, 0, 0, 0,
        8, 40, -40, 20, 1, 0, 0, 0, 0
    };
    RCKTCBPositionController controller; ReadWire(controller, keys, 4);
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); PositionIs(output, 10, -10, 5);
    ASSERT_TRUE(controller.Evaluate(6, &output)); PositionIs(output, 30, -30, 15);
}

TEST_F(TCBPositionEvaluationTest, DuplicateBoundaryTimesClampRawFirstAndLastValues) {
    const float keys[] = {
        1, 2, 4, 6, 1, 0, 0, 0, 0,
        1, 20, 40, 60, 1, 0, 0, 0, 0,
        5, 30, 50, 70, 1, 0, 0, 0, 0,
        5, 3, 5, 7, 1, 0, 0, 0, 0
    };
    RCKTCBPositionController controller; ReadWire(controller, keys, 4);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output)); PositionIs(output, 2, 4, 6);
    ASSERT_TRUE(controller.Evaluate(5, &output)); PositionIs(output, 3, 5, 7);
    ASSERT_TRUE(controller.Evaluate(3, &output)); PositionIs(output, 25, 45, 65);
}

TEST_F(TCBPositionEvaluationTest, EndpointSamplesDoNotAllocateEvenWhenAnInteriorCacheWouldFail) {
    InspectTCBPosition controller; AddPosition(controller, 1, 2, 4, 6); AddPosition(controller, 5, 8, 10, 12);
    FailNextTangentArray = true; VxVector output;
    ASSERT_TRUE(controller.Evaluate(1, &output)); PositionIs(output, 2, 4, 6);
    ASSERT_TRUE(controller.Evaluate(5, &output)); PositionIs(output, 8, 10, 12);
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBPositionEvaluationTest, LazyCacheFailurePreservesOutputAndRetryComputesTheCurve) {
    InspectTCBPosition controller; AddPosition(controller, 0, 0); AddPosition(controller, 8, 8);
    VxVector output(11, 22, 33); FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(4, &output)); PositionIs(output, 11, 22, 33); EXPECT_EQ(nullptr, controller.Cache());
    EXPECT_FALSE(FailNextTangentArray); ASSERT_TRUE(controller.Evaluate(4, &output)); PositionIs(output, 4);
    EXPECT_NE(nullptr, controller.Cache());
}

TEST_F(TCBPositionEvaluationTest, ComputeTangentsReplacesCacheOnlyAfterSuccessfulAllocation) {
    InspectTCBPosition controller; AddPosition(controller, 0, 0); AddPosition(controller, 8, 8);
    controller.ComputeTangents(); const VxVector *old = controller.Cache(); ASSERT_NE(nullptr, old);
    FailNextTangentArray = true; controller.ComputeTangents(); EXPECT_EQ(old, controller.Cache());
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); PositionIs(output, 4);
    EXPECT_FALSE(FailNextTangentArray);
}

TEST_F(TCBPositionEvaluationTest, ExplicitRecomputationRefreshesEditsThroughBorrowedKeys) {
    InspectTCBPosition controller; AddPosition(controller, 0, 0); AddPosition(controller, 8, 8);
    controller.ComputeTangents();
    CKTCBPositionKey *first = static_cast<CKTCBPositionKey *>(controller.GetKey(0)); ASSERT_NE(nullptr, first);
    first->tension = 1; controller.ComputeTangents();
    PositionIs(controller.Cache()[1], 0); PositionIs(controller.Cache()[2], 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); PositionIs(output, 3);
}

TEST_F(TCBPositionEvaluationTest, ZeroAndOneKeyExplicitTangentComputationsDoNotAllocate) {
    InspectTCBPosition controller; FailNextTangentArray = true; controller.ComputeTangents();
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache()); FailNextTangentArray = false;
    AddPosition(controller, 3, 2); FailNextTangentArray = true; controller.ComputeTangents();
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBPositionEvaluationTest, OpposingFiniteTimeSpansKeepUnitInteriorWeightsAndCorrectCurve) {
    InspectTCBPosition controller;
    AddPosition(controller, -FLT_MAX, 0); AddPosition(controller, 0, 4); AddPosition(controller, FLT_MAX, 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(-FLT_MAX * .5f, &output)); PositionIs(output, 2);
    PositionIs(controller.Cache()[1], 4); PositionIs(controller.Cache()[2], 4); PositionIs(controller.Cache()[3], 4);
    ASSERT_TRUE(controller.Evaluate(FLT_MAX * .5f, &output)); PositionIs(output, 5.5f);
    // Native's asymmetric last endpoint rule gives incoming tangent 8, not 4.
    PositionIs(controller.Cache()[4], 8);
}

TEST_F(TCBPositionEvaluationTest, SmallFiniteTimesDoNotOverflowTheReciprocalUsedForWeights) {
    const float tiny = std::ldexp(1.0f, -130);
    InspectTCBPosition controller;
    AddPosition(controller, -tiny, 0); AddPosition(controller, 0, 4); AddPosition(controller, tiny, 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(-tiny * .5f, &output)); PositionIs(output, 2);
    PositionIs(controller.Cache()[2], 4); PositionIs(controller.Cache()[3], 4);
}

TEST_F(TCBPositionEvaluationTest, ZeroTensionFactorDoesNotTurnWideFiniteDifferencesIntoNaN) {
    InspectTCBPosition controller;
    AddPosition(controller, 0, -FLT_MAX, FLT_MAX, 8, 1);
    AddPosition(controller, 4, FLT_MAX, -FLT_MAX, -4, 1);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); PositionIs(output, 0, 0, 2);
    PositionIs(controller.Cache()[1], 0); PositionIs(controller.Cache()[2], 0);
}

TEST_F(TCBPositionEvaluationTest, ScaledWideFiniteDifferencesKeepRepresentableTangents) {
    InspectTCBPosition controller;
    AddPosition(controller, 0, -FLT_MAX, 0, 0, .75f); AddPosition(controller, 4, FLT_MAX, 0, 0, .75f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); PositionIs(output, 0);
    PositionIs(controller.Cache()[1], FLT_MAX * .5f); PositionIs(controller.Cache()[2], FLT_MAX * .5f);
    ASSERT_TRUE(controller.Evaluate(1, &output));
    EXPECT_FLOAT_EQ(static_cast<float>(-.640625 * static_cast<double>(FLT_MAX)), output.x);
}

TEST_F(TCBPositionEvaluationTest, InteriorOpposingWideDifferencesCancelBeforeNarrowingToTangentStorage) {
    InspectTCBPosition controller;
    AddPosition(controller, 0, -FLT_MAX, 0, 0, .75f);
    AddPosition(controller, 2, FLT_MAX, 0, 0, .75f);
    AddPosition(controller, 4, -FLT_MAX, 0, 0, .75f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output));
    EXPECT_FLOAT_EQ(static_cast<float>(.09375 * static_cast<double>(FLT_MAX)), output.x);
    PositionIs(controller.Cache()[2], 0); PositionIs(controller.Cache()[3], 0);
    EXPECT_FLOAT_EQ(static_cast<float>(.75 * static_cast<double>(FLT_MAX)), controller.Cache()[1].x);
    EXPECT_FLOAT_EQ(static_cast<float>(-.75 * static_cast<double>(FLT_MAX)), controller.Cache()[4].x);
}

TEST_F(TCBPositionEvaluationTest, EndpointScalingKeepsRepresentableTangentsWhenThreeTimesDeltaOverflowsFloat) {
    InspectTCBPosition controller;
    AddPosition(controller, 0, 0, 0, 0, .5f); AddPosition(controller, 2, FLT_MAX, 0, 0, 1);
    AddPosition(controller, 4, 0, 0, 0, .5f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output));
    EXPECT_FLOAT_EQ(static_cast<float>(.59375 * static_cast<double>(FLT_MAX)), output.x);
    EXPECT_FLOAT_EQ(static_cast<float>(.75 * static_cast<double>(FLT_MAX)), controller.Cache()[1].x);
    EXPECT_FLOAT_EQ(static_cast<float>(-.75 * static_cast<double>(FLT_MAX)), controller.Cache()[4].x);
}

TEST_F(TCBPositionEvaluationTest, HermiteTermsCancelBeforeNarrowingTheFinalRepresentablePosition) {
    const float large = .95f * FLT_MAX;
    InspectTCBPosition controller;
    // Every stored tangent is representable: 0,large,large,large.
    AddPosition(controller, 0, -large, 0, 0, 1);
    AddPosition(controller, 1, large);
    AddPosition(controller, 2, large, 0, 0, -1);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1.5f, &output)); PositionIs(output, large);
    PositionIs(controller.Cache()[1], 0); PositionIs(controller.Cache()[2], large);
    PositionIs(controller.Cache()[3], large); PositionIs(controller.Cache()[4], large);
    EXPECT_TRUE(std::isfinite(output.x));
}

TEST_F(TCBPositionEvaluationTest, AsymmetricTimeSpansRetainSmallEndpointContributionsInBothDirections) {
    const float large = 1.0e30f;
    for (int reversed = 0; reversed < 2; ++reversed) {
        SCOPED_TRACE(reversed); RCKTCBPositionController controller;
        if (reversed) {
            AddPosition(controller, -large, large); AddPosition(controller, 2, 0);
        } else {
            AddPosition(controller, -2, 0); AddPosition(controller, large, large);
        }
        // With two zero-TCB keys, Hermite reduces to a line. A distance of
        // one time unit from the zero-valued endpoint contributes position 1.
        VxVector output; ASSERT_TRUE(controller.Evaluate(reversed ? 1.0f : -1.0f, &output));
        PositionIs(output, 1);
    }
}

TEST_F(TCBPositionEvaluationTest, TinyEaseEndQuadraticKeepsItsRepresentableLargeTangentContribution) {
    const float large = 1.0e30f;
    for (int reversed = 0; reversed < 2; ++reversed) {
        SCOPED_TRACE(reversed); RCKTCBPositionController controller;
        if (reversed) {
            AddPosition(controller, -large, large, 0, 0, 0, 0, 0, .875f, 0);
            AddPosition(controller, 0, 0, 0, 0, 0, 0, 0, .5f, .875f);
        } else {
            AddPosition(controller, 0, 0, 0, 0, 0, 0, 0, .875f, .5f);
            AddPosition(controller, large, large, 0, 0, 0, 0, 0, 0, .875f);
        }
        // Ease from .5 gives F(t)=(4/3)*t^2. The uneased curve is a line:
        // F(1e-30)*1e30 = (4/3)*1e-30, though F itself underflows float.
        VxVector output; ASSERT_TRUE(controller.Evaluate(reversed ? -1.0f : 1.0f, &output));
        PositionIs(output, 1.3333333333333333e-30f);
        EXPECT_GT(output.x, 0); EXPECT_TRUE(std::isfinite(output.x));
    }
}

TEST_F(TCBPositionEvaluationTest, SamplingDoesNotEditKeysLengthOrReuseTheCacheAsAnOutputBuffer) {
    InspectTCBPosition controller; AddPosition(controller, 0, 1, 2, 3); AddPosition(controller, 8, 9, 10, 11);
    controller.SetLength(42); unsigned char before[76], after[76]; ASSERT_EQ(76, controller.DumpKeysTo(before));
    CKKey *first = controller.GetKey(0); VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output));
    const VxVector *cache = controller.Cache(); ASSERT_NE(nullptr, cache);
    FailNextTangentArray = true; ASSERT_TRUE(controller.Evaluate(6, &output)); EXPECT_TRUE(FailNextTangentArray);
    EXPECT_EQ(cache, controller.Cache()); ASSERT_EQ(76, controller.DumpKeysTo(after));
    EXPECT_EQ(0, std::memcmp(before, after, 76)); EXPECT_EQ(first, controller.GetKey(0)); EXPECT_FLOAT_EQ(42, controller.GetLength());
}

TEST_F(TCBPositionEvaluationTest, CompareRejectsNullWrongTypeAndDifferentCounts) {
    RCKTCBPositionController first, second; RCKTCBScaleController scale;
    EXPECT_FALSE(first.Compare(nullptr)); EXPECT_FALSE(first.Compare(&scale));
    AddPosition(first, 0, 1); EXPECT_FALSE(first.Compare(&second)); EXPECT_FALSE(second.Compare(&first));
}

TEST_F(TCBPositionEvaluationTest, CompareUsesInclusiveThresholdForTimeAndAllPositionComponents) {
    RCKTCBPositionController first, second;
    AddPosition(first, 2, 1, -2, 3); AddPosition(second, 2.25f, 1.25f, -2.25f, 3.25f);
    EXPECT_TRUE(first.Compare(&second, .25f)); EXPECT_TRUE(second.Compare(&first, .25f));
    const float below = std::nextafter(.25f, 0.0f);
    EXPECT_FALSE(first.Compare(&second, below)); EXPECT_FALSE(second.Compare(&first, below));
}

TEST_F(TCBPositionEvaluationTest, CompareChecksEveryTimeAndPositionFieldAtEveryKey) {
    RCKTCBPositionController first, second;
    AddPosition(first, 2, 1, 2, 3); AddPosition(first, 8, 4, 5, 6);
    AddPosition(second, 2, 1, 2, 3); AddPosition(second, 8, 4, 5, 6);
    for (int index = 0; index < 2; ++index) {
        CKTCBPositionKey *key = static_cast<CKTCBPositionKey *>(second.GetKey(index));
        float *fields[] = {&key->TimeStep, &key->Pos.x, &key->Pos.y, &key->Pos.z};
        for (int field = 0; field < 4; ++field) {
            SCOPED_TRACE(index * 4 + field); *fields[field] += .5f;
            EXPECT_FALSE(first.Compare(&second, .25f)); *fields[field] -= .5f;
            EXPECT_TRUE(first.Compare(&second, 0));
        }
    }
}

TEST_F(TCBPositionEvaluationTest, CompareRequiresExactSplineParametersEvenWithHugeThreshold) {
    RCKTCBPositionController first, second;
    AddPosition(first, 2, 1, 2, 3); AddPosition(first, 8, 4, 5, 6);
    AddPosition(second, 2, 1, 2, 3); AddPosition(second, 8, 4, 5, 6);
    for (int index = 0; index < 2; ++index) {
        CKTCBPositionKey *key = static_cast<CKTCBPositionKey *>(second.GetKey(index));
        float *fields[] = {&key->tension, &key->continuity, &key->bias, &key->easeto, &key->easefrom};
        for (int field = 0; field < 5; ++field) {
            SCOPED_TRACE(index * 5 + field); *fields[field] = .0001f;
            EXPECT_FALSE(first.Compare(&second, FLT_MAX)); EXPECT_FALSE(second.Compare(&first, FLT_MAX));
            *fields[field] = 0; EXPECT_TRUE(first.Compare(&second, 0));
        }
    }
}

TEST_F(TCBPositionEvaluationTest, CompareIgnoresLengthAndCacheAndSupportsEmptyAndSelfComparison) {
    InspectTCBPosition first, second; first.SetLength(1); second.SetLength(200);
    EXPECT_TRUE(first.Compare(&second, -1)); EXPECT_TRUE(first.Compare(&first));
    AddPosition(first, 0, 0); AddPosition(first, 8, 8); AddPosition(second, 0, 0); AddPosition(second, 8, 8);
    first.ComputeTangents(); EXPECT_NE(nullptr, first.Cache()); EXPECT_EQ(nullptr, second.Cache());
    EXPECT_TRUE(first.Compare(&second)); EXPECT_TRUE(first.Compare(&first)); EXPECT_FALSE(first.Compare(&second, -1));
}

class TCBPositionAnimationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};

TEST_F(TCBPositionAnimationTest, ObjectAnimationUsesTheTCBCurveAndHonorsIgnorePosition) {
    RCKObjectAnimation animation(Context);
    RCKTCBPositionController *controller = static_cast<RCKTCBPositionController *>(animation.CreateController(CKANIMATION_TCBPOS_CONTROL));
    ASSERT_NE(nullptr, controller); AddPosition(*controller, 0, 0); AddPosition(*controller, 8, 8, 0, 0, 1);
    VxVector output; ASSERT_TRUE(animation.EvaluatePosition(4, output)); PositionIs(output, 5);
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNOREPOS);
    EXPECT_FALSE(animation.EvaluatePosition(4, output)); PositionIs(output, 0);
}

TEST_F(TCBPositionAnimationTest, PlaybackPublishesNonlinearTCBSampleToTheEntity) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBPositionController *controller = static_cast<RCKTCBPositionController *>(animation.CreateController(CKANIMATION_TCBPOS_CONTROL));
    ASSERT_NE(nullptr, controller); AddPosition(*controller, 0, 0); AddPosition(*controller, 8, 8, 0, 0, 1);
    animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    VxVector position; entity.GetPosition(&position); PositionIs(position, 5);
    const VxMatrix &local = entity.GetLocalMatrix(); EXPECT_FLOAT_EQ(1, local[0][0]); EXPECT_FLOAT_EQ(1, local[1][1]); EXPECT_FLOAT_EQ(1, local[2][2]);
    animation.Set3dEntity(nullptr);
}

TEST_F(TCBPositionAnimationTest, PlaybackKeepsRepresentableWidePositionAndZeroTangentsFinite) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBPositionController *controller = static_cast<RCKTCBPositionController *>(animation.CreateController(CKANIMATION_TCBPOS_CONTROL));
    ASSERT_NE(nullptr, controller); AddPosition(*controller, 0, -FLT_MAX, FLT_MAX, 8, 1);
    AddPosition(*controller, 8, FLT_MAX, -FLT_MAX, -4, 1);
    animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    VxVector position; entity.GetPosition(&position); PositionIs(position, 0, 0, 2);
    EXPECT_TRUE(std::isfinite(position.x)); EXPECT_TRUE(std::isfinite(position.y));
    animation.Set3dEntity(nullptr);
}
} // namespace
