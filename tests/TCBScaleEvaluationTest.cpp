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

class InspectTCBScale : public RCKTCBScaleController {
public:
    const VxVector *Cache() const { return m_Tangents; }
};

void AddScale(RCKTCBScaleController &controller, float time, float x, float y = 0, float z = 0,
                 float tension = 0, float continuity = 0, float bias = 0,
                 float easeTo = 0, float easeFrom = 0) {
    VxVector scale(x, y, z);
    CKTCBScaleKey key(time, scale, tension, continuity, bias, easeTo, easeFrom);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void ScaleIs(const VxVector &scale, float x, float y = 0, float z = 0) {
    EXPECT_FLOAT_EQ(x, scale.x); EXPECT_FLOAT_EQ(y, scale.y); EXPECT_FLOAT_EQ(z, scale.z);
}

void ReadWire(RCKTCBScaleController &controller, const float *keys, int count) {
    // 2.1 wire: DWORD count, then time,x,y,z,T,C,B,easeto,easefrom.
    unsigned char wire[4 + 36 * 8] = {};
    ASSERT_GE(count, 0); ASSERT_LE(count, 8);
    std::memcpy(wire, &count, 4);
    if (count) std::memcpy(wire + 4, keys, 36 * count);
    ASSERT_EQ(4 + 36 * count, controller.ReadKeysFrom(wire));
}

class TCBScaleEvaluationTest : public ::testing::Test {
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
TEST_F(TCBScaleEvaluationTest, EmptyControllerReturnsFalseWithoutWritingOutputOrAllocating) {
    InspectTCBScale controller; VxVector output(11, -22, 33);
    FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(0, &output)); ScaleIs(output, 11, -22, 33);
    EXPECT_FALSE(controller.Evaluate(10, nullptr)); EXPECT_TRUE(FailNextTangentArray);
    EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBScaleEvaluationTest, SingleKeyClampsWithoutAllocatingAndWritesExactlyThreeComponents) {
    InspectTCBScale controller; AddScale(controller, 3, 2, -7, 11);
    struct Guarded { CKDWORD Before; VxVector Value; CKDWORD After; } output;
    const float times[] = {-FLT_MAX, -1, 3, 20, FLT_MAX};
    FailNextTangentArray = true;
    for (float time : times) {
        SCOPED_TRACE(time); output.Before = 0x13572468; output.After = 0xfedcba98;
        ASSERT_TRUE(controller.Evaluate(time, &output.Value)); ScaleIs(output.Value, 2, -7, 11);
        EXPECT_EQ(0x13572468u, output.Before); EXPECT_EQ(0xfedcba98u, output.After);
    }
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBScaleEvaluationTest, TwoKeysUseEndpointTensionAndIgnoreContinuityAndBias) {
    InspectTCBScale controller;
    AddScale(controller, 2, 0, 0, 0, .5f, .8f, -.7f);
    AddScale(controller, 10, 8, 0, 0, -.5f, -.9f, .6f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(6, &output)); ScaleIs(output, 3);
    ASSERT_NE(nullptr, controller.Cache()); ScaleIs(controller.Cache()[1], 4); ScaleIs(controller.Cache()[2], 12);
    ASSERT_TRUE(controller.Evaluate(4, &output)); ScaleIs(output, 1.25f);
}

TEST_F(TCBScaleEvaluationTest, UnevenIntervalsUseNativeIncomingEndpointRule) {
    InspectTCBScale controller;
    AddScale(controller, 0, 0); AddScale(controller, 1, 4); AddScale(controller, 4, 10);
    VxVector output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); ScaleIs(output, 2.28125f);
    ASSERT_NE(nullptr, controller.Cache());
    ScaleIs(controller.Cache()[1], 4.75f); ScaleIs(controller.Cache()[2], 2.5f);
    ScaleIs(controller.Cache()[3], 7.5f); ScaleIs(controller.Cache()[4], 10.25f);
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); ScaleIs(output, 6.65625f);
}

TEST_F(TCBScaleEvaluationTest, NonzeroContinuityAndBiasWeightIncomingAndOutgoingTangentsIndependently) {
    InspectTCBScale controller;
    AddScale(controller, 0, 0); AddScale(controller, 1, 4, 0, 0, .25f, .5f, -.5f); AddScale(controller, 4, 10);
    VxVector output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); ScaleIs(output, 1.9853515625f);
    ScaleIs(controller.Cache()[2], 4.078125f); ScaleIs(controller.Cache()[3], 3.515625f);
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); ScaleIs(output, 6.0595703125f);
}

TEST_F(TCBScaleEvaluationTest, EaseHasQuadraticEndsAndLinearMiddleBeforeHermite) {
    RCKTCBScaleController controller;
    AddScale(controller, 0, 0, 0, 0, 1, 0, 0, .9f, .25f);
    AddScale(controller, 8, 10, 0, 0, 1, 0, 0, .5f, .9f);
    const float times[] = {1, 2, 3, 4, 6};
    // Remapped t = .05,.2,.4,.6,.9; zero tangents give 10*(3*t^2-2*t^3).
    const float expected[] = {.0725f, 1.04f, 3.52f, 6.48f, 9.72f};
    for (int index = 0; index < 5; ++index) {
        SCOPED_TRACE(times[index]); VxVector output; ASSERT_TRUE(controller.Evaluate(times[index], &output));
        EXPECT_NEAR(expected[index], output.x, 2.0e-6f); EXPECT_FLOAT_EQ(0, output.y); EXPECT_FLOAT_EQ(0, output.z);
    }
}

TEST_F(TCBScaleEvaluationTest, EaseAboveOneNormalizesBothEndsTogether) {
    RCKTCBScaleController controller;
    AddScale(controller, 0, 0, 0, 0, 1, 0, 0, 0, 2);
    AddScale(controller, 8, 16, 0, 0, 1, 0, 0, 2, 0);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); ScaleIs(output, .6875f);
    ASSERT_TRUE(controller.Evaluate(6, &output)); ScaleIs(output, 15.3125f);
}

TEST_F(TCBScaleEvaluationTest, OriginalRuntimeVectorFixturesMatchAllTwentyNineSamples) {
    int fixtures = 0, samples = 0;
    for (const TCBReference::Fixture &fixture : TCBReference::Fixtures) {
        if (fixture.rotation) continue;
        SCOPED_TRACE(fixture.name); ++fixtures;
        RCKTCBScaleController controller; ReadWire(controller, fixture.keys, fixture.keyCount);
        for (int index = 0; index < fixture.sampleCount; ++index) {
            const TCBReference::Sample &sample = fixture.samples[index]; SCOPED_TRACE(sample.time);
            VxVector output; ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            EXPECT_NEAR(sample.value[0], output.x, 5.0e-5f); EXPECT_NEAR(sample.value[1], output.y, 5.0e-5f);
            EXPECT_NEAR(sample.value[2], output.z, 5.0e-5f); ++samples;
        }
    }
    EXPECT_EQ(3, fixtures); EXPECT_EQ(29, samples);
}

TEST_F(TCBScaleEvaluationTest, DuplicateInteriorTimesSelectFirstTieThenLeaveFromLastTie) {
    const float keys[] = {
        0, 0, 0, 0, 1, 0, 0, 0, 0,
        4, 10, -10, 5, 1, 0, 0, 0, 0,
        4, 20, -20, 10, 1, 0, 0, 0, 0,
        8, 40, -40, 20, 1, 0, 0, 0, 0
    };
    RCKTCBScaleController controller; ReadWire(controller, keys, 4);
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); ScaleIs(output, 10, -10, 5);
    ASSERT_TRUE(controller.Evaluate(6, &output)); ScaleIs(output, 30, -30, 15);
}

TEST_F(TCBScaleEvaluationTest, DuplicateBoundaryTimesClampRawFirstAndLastValues) {
    const float keys[] = {
        1, 2, 4, 6, 1, 0, 0, 0, 0,
        1, 20, 40, 60, 1, 0, 0, 0, 0,
        5, 30, 50, 70, 1, 0, 0, 0, 0,
        5, 3, 5, 7, 1, 0, 0, 0, 0
    };
    RCKTCBScaleController controller; ReadWire(controller, keys, 4);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output)); ScaleIs(output, 2, 4, 6);
    ASSERT_TRUE(controller.Evaluate(5, &output)); ScaleIs(output, 3, 5, 7);
    ASSERT_TRUE(controller.Evaluate(3, &output)); ScaleIs(output, 25, 45, 65);
}

TEST_F(TCBScaleEvaluationTest, EndpointSamplesDoNotAllocateEvenWhenAnInteriorCacheWouldFail) {
    InspectTCBScale controller; AddScale(controller, 1, 2, 4, 6); AddScale(controller, 5, 8, 10, 12);
    FailNextTangentArray = true; VxVector output;
    ASSERT_TRUE(controller.Evaluate(1, &output)); ScaleIs(output, 2, 4, 6);
    ASSERT_TRUE(controller.Evaluate(5, &output)); ScaleIs(output, 8, 10, 12);
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBScaleEvaluationTest, LazyCacheFailurePreservesOutputAndRetryComputesTheCurve) {
    InspectTCBScale controller; AddScale(controller, 0, 0); AddScale(controller, 8, 8);
    VxVector output(11, 22, 33); FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(4, &output)); ScaleIs(output, 11, 22, 33); EXPECT_EQ(nullptr, controller.Cache());
    EXPECT_FALSE(FailNextTangentArray); ASSERT_TRUE(controller.Evaluate(4, &output)); ScaleIs(output, 4);
    EXPECT_NE(nullptr, controller.Cache());
}

TEST_F(TCBScaleEvaluationTest, ComputeTangentsReplacesCacheOnlyAfterSuccessfulAllocation) {
    InspectTCBScale controller; AddScale(controller, 0, 0); AddScale(controller, 8, 8);
    controller.ComputeTangents(); const VxVector *old = controller.Cache(); ASSERT_NE(nullptr, old);
    FailNextTangentArray = true; controller.ComputeTangents(); EXPECT_EQ(old, controller.Cache());
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); ScaleIs(output, 4);
    EXPECT_FALSE(FailNextTangentArray);
}

TEST_F(TCBScaleEvaluationTest, ExplicitRecomputationRefreshesEditsThroughBorrowedKeys) {
    InspectTCBScale controller; AddScale(controller, 0, 0); AddScale(controller, 8, 8);
    controller.ComputeTangents();
    CKTCBScaleKey *first = static_cast<CKTCBScaleKey *>(controller.GetKey(0)); ASSERT_NE(nullptr, first);
    first->tension = 1; controller.ComputeTangents();
    ScaleIs(controller.Cache()[1], 0); ScaleIs(controller.Cache()[2], 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); ScaleIs(output, 3);
}

TEST_F(TCBScaleEvaluationTest, ZeroAndOneKeyExplicitTangentComputationsDoNotAllocate) {
    InspectTCBScale controller; FailNextTangentArray = true; controller.ComputeTangents();
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache()); FailNextTangentArray = false;
    AddScale(controller, 3, 2); FailNextTangentArray = true; controller.ComputeTangents();
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBScaleEvaluationTest, OpposingFiniteTimeSpansKeepUnitInteriorWeightsAndCorrectCurve) {
    InspectTCBScale controller;
    AddScale(controller, -FLT_MAX, 0); AddScale(controller, 0, 4); AddScale(controller, FLT_MAX, 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(-FLT_MAX * .5f, &output)); ScaleIs(output, 2);
    ScaleIs(controller.Cache()[1], 4); ScaleIs(controller.Cache()[2], 4); ScaleIs(controller.Cache()[3], 4);
    ASSERT_TRUE(controller.Evaluate(FLT_MAX * .5f, &output)); ScaleIs(output, 5.5f);
    // Native's asymmetric last endpoint rule gives incoming tangent 8, not 4.
    ScaleIs(controller.Cache()[4], 8);
}

TEST_F(TCBScaleEvaluationTest, SmallFiniteTimesDoNotOverflowTheReciprocalUsedForWeights) {
    const float tiny = std::ldexp(1.0f, -130);
    InspectTCBScale controller;
    AddScale(controller, -tiny, 0); AddScale(controller, 0, 4); AddScale(controller, tiny, 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(-tiny * .5f, &output)); ScaleIs(output, 2);
    ScaleIs(controller.Cache()[2], 4); ScaleIs(controller.Cache()[3], 4);
}

TEST_F(TCBScaleEvaluationTest, ZeroTensionFactorDoesNotTurnWideFiniteDifferencesIntoNaN) {
    InspectTCBScale controller;
    AddScale(controller, 0, -FLT_MAX, FLT_MAX, 8, 1);
    AddScale(controller, 4, FLT_MAX, -FLT_MAX, -4, 1);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); ScaleIs(output, 0, 0, 2);
    ScaleIs(controller.Cache()[1], 0); ScaleIs(controller.Cache()[2], 0);
}

TEST_F(TCBScaleEvaluationTest, ScaledWideFiniteDifferencesKeepRepresentableTangents) {
    InspectTCBScale controller;
    AddScale(controller, 0, -FLT_MAX, 0, 0, .75f); AddScale(controller, 4, FLT_MAX, 0, 0, .75f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); ScaleIs(output, 0);
    ScaleIs(controller.Cache()[1], FLT_MAX * .5f); ScaleIs(controller.Cache()[2], FLT_MAX * .5f);
    ASSERT_TRUE(controller.Evaluate(1, &output));
    EXPECT_FLOAT_EQ(static_cast<float>(-.640625 * static_cast<double>(FLT_MAX)), output.x);
}

TEST_F(TCBScaleEvaluationTest, InteriorOpposingWideDifferencesCancelBeforeNarrowingToTangentStorage) {
    InspectTCBScale controller;
    AddScale(controller, 0, -FLT_MAX, 0, 0, .75f);
    AddScale(controller, 2, FLT_MAX, 0, 0, .75f);
    AddScale(controller, 4, -FLT_MAX, 0, 0, .75f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output));
    EXPECT_FLOAT_EQ(static_cast<float>(.09375 * static_cast<double>(FLT_MAX)), output.x);
    ScaleIs(controller.Cache()[2], 0); ScaleIs(controller.Cache()[3], 0);
    EXPECT_FLOAT_EQ(static_cast<float>(.75 * static_cast<double>(FLT_MAX)), controller.Cache()[1].x);
    EXPECT_FLOAT_EQ(static_cast<float>(-.75 * static_cast<double>(FLT_MAX)), controller.Cache()[4].x);
}

TEST_F(TCBScaleEvaluationTest, EndpointScalingKeepsRepresentableTangentsWhenThreeTimesDeltaOverflowsFloat) {
    InspectTCBScale controller;
    AddScale(controller, 0, 0, 0, 0, .5f); AddScale(controller, 2, FLT_MAX, 0, 0, 1);
    AddScale(controller, 4, 0, 0, 0, .5f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output));
    EXPECT_FLOAT_EQ(static_cast<float>(.59375 * static_cast<double>(FLT_MAX)), output.x);
    EXPECT_FLOAT_EQ(static_cast<float>(.75 * static_cast<double>(FLT_MAX)), controller.Cache()[1].x);
    EXPECT_FLOAT_EQ(static_cast<float>(-.75 * static_cast<double>(FLT_MAX)), controller.Cache()[4].x);
}

TEST_F(TCBScaleEvaluationTest, HermiteTermsCancelBeforeNarrowingTheFinalRepresentableScale) {
    const float large = .95f * FLT_MAX;
    InspectTCBScale controller;
    // Every stored tangent is representable: 0,large,large,large.
    AddScale(controller, 0, -large, 0, 0, 1);
    AddScale(controller, 1, large);
    AddScale(controller, 2, large, 0, 0, -1);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1.5f, &output)); ScaleIs(output, large);
    ScaleIs(controller.Cache()[1], 0); ScaleIs(controller.Cache()[2], large);
    ScaleIs(controller.Cache()[3], large); ScaleIs(controller.Cache()[4], large);
    EXPECT_TRUE(std::isfinite(output.x));
}

TEST_F(TCBScaleEvaluationTest, AsymmetricTimeSpansRetainSmallEndpointContributionsInBothDirections) {
    const float large = 1.0e30f;
    for (int reversed = 0; reversed < 2; ++reversed) {
        SCOPED_TRACE(reversed); RCKTCBScaleController controller;
        if (reversed) {
            AddScale(controller, -large, large); AddScale(controller, 2, 0);
        } else {
            AddScale(controller, -2, 0); AddScale(controller, large, large);
        }
        // With two zero-TCB keys, Hermite reduces to a line. A distance of
        // one time unit from the zero-valued endpoint contributes scale 1.
        VxVector output; ASSERT_TRUE(controller.Evaluate(reversed ? 1.0f : -1.0f, &output));
        ScaleIs(output, 1);
    }
}

TEST_F(TCBScaleEvaluationTest, TinyEaseEndQuadraticKeepsItsRepresentableLargeTangentContribution) {
    const float large = 1.0e30f;
    for (int reversed = 0; reversed < 2; ++reversed) {
        SCOPED_TRACE(reversed); RCKTCBScaleController controller;
        if (reversed) {
            AddScale(controller, -large, large, 0, 0, 0, 0, 0, .875f, 0);
            AddScale(controller, 0, 0, 0, 0, 0, 0, 0, .5f, .875f);
        } else {
            AddScale(controller, 0, 0, 0, 0, 0, 0, 0, .875f, .5f);
            AddScale(controller, large, large, 0, 0, 0, 0, 0, 0, .875f);
        }
        // Ease from .5 gives F(t)=(4/3)*t^2. The uneased curve is a line:
        // F(1e-30)*1e30 = (4/3)*1e-30, though F itself underflows float.
        VxVector output; ASSERT_TRUE(controller.Evaluate(reversed ? -1.0f : 1.0f, &output));
        ScaleIs(output, 1.3333333333333333e-30f);
        EXPECT_GT(output.x, 0); EXPECT_TRUE(std::isfinite(output.x));
    }
}

TEST_F(TCBScaleEvaluationTest, SamplingDoesNotEditKeysLengthOrReuseTheCacheAsAnOutputBuffer) {
    InspectTCBScale controller; AddScale(controller, 0, 1, 2, 3); AddScale(controller, 8, 9, 10, 11);
    controller.SetLength(42); unsigned char before[76], after[76]; ASSERT_EQ(76, controller.DumpKeysTo(before));
    CKKey *first = controller.GetKey(0); VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output));
    const VxVector *cache = controller.Cache(); ASSERT_NE(nullptr, cache);
    FailNextTangentArray = true; ASSERT_TRUE(controller.Evaluate(6, &output)); EXPECT_TRUE(FailNextTangentArray);
    EXPECT_EQ(cache, controller.Cache()); ASSERT_EQ(76, controller.DumpKeysTo(after));
    EXPECT_EQ(0, std::memcmp(before, after, 76)); EXPECT_EQ(first, controller.GetKey(0)); EXPECT_FLOAT_EQ(42, controller.GetLength());
}

TEST_F(TCBScaleEvaluationTest, CompareRejectsNullWrongTypeAndDifferentCounts) {
    RCKTCBScaleController first, second; RCKTCBPositionController position;
    EXPECT_FALSE(first.Compare(nullptr)); EXPECT_FALSE(first.Compare(&position));
    AddScale(first, 0, 1); EXPECT_FALSE(first.Compare(&second)); EXPECT_FALSE(second.Compare(&first));
}

TEST_F(TCBScaleEvaluationTest, CompareUsesInclusiveThresholdForTimeAndAllScaleComponents) {
    RCKTCBScaleController first, second;
    AddScale(first, 2, 1, -2, 3); AddScale(second, 2.25f, 1.25f, -2.25f, 3.25f);
    EXPECT_TRUE(first.Compare(&second, .25f)); EXPECT_TRUE(second.Compare(&first, .25f));
    const float below = std::nextafter(.25f, 0.0f);
    EXPECT_FALSE(first.Compare(&second, below)); EXPECT_FALSE(second.Compare(&first, below));
}

TEST_F(TCBScaleEvaluationTest, CompareChecksEveryTimeAndScaleFieldAtEveryKey) {
    RCKTCBScaleController first, second;
    AddScale(first, 2, 1, 2, 3); AddScale(first, 8, 4, 5, 6);
    AddScale(second, 2, 1, 2, 3); AddScale(second, 8, 4, 5, 6);
    for (int index = 0; index < 2; ++index) {
        CKTCBScaleKey *key = static_cast<CKTCBScaleKey *>(second.GetKey(index));
        float *fields[] = {&key->TimeStep, &key->Pos.x, &key->Pos.y, &key->Pos.z};
        for (int field = 0; field < 4; ++field) {
            SCOPED_TRACE(index * 4 + field); *fields[field] += .5f;
            EXPECT_FALSE(first.Compare(&second, .25f)); *fields[field] -= .5f;
            EXPECT_TRUE(first.Compare(&second, 0));
        }
    }
}

TEST_F(TCBScaleEvaluationTest, CompareRequiresExactSplineParametersEvenWithHugeThreshold) {
    RCKTCBScaleController first, second;
    AddScale(first, 2, 1, 2, 3); AddScale(first, 8, 4, 5, 6);
    AddScale(second, 2, 1, 2, 3); AddScale(second, 8, 4, 5, 6);
    for (int index = 0; index < 2; ++index) {
        CKTCBScaleKey *key = static_cast<CKTCBScaleKey *>(second.GetKey(index));
        float *fields[] = {&key->tension, &key->continuity, &key->bias, &key->easeto, &key->easefrom};
        for (int field = 0; field < 5; ++field) {
            SCOPED_TRACE(index * 5 + field); *fields[field] = .0001f;
            EXPECT_FALSE(first.Compare(&second, FLT_MAX)); EXPECT_FALSE(second.Compare(&first, FLT_MAX));
            *fields[field] = 0; EXPECT_TRUE(first.Compare(&second, 0));
        }
    }
}

TEST_F(TCBScaleEvaluationTest, CompareIgnoresLengthAndCacheAndSupportsEmptyAndSelfComparison) {
    InspectTCBScale first, second; first.SetLength(1); second.SetLength(200);
    EXPECT_TRUE(first.Compare(&second, -1)); EXPECT_TRUE(first.Compare(&first));
    AddScale(first, 0, 0); AddScale(first, 8, 8); AddScale(second, 0, 0); AddScale(second, 8, 8);
    first.ComputeTangents(); EXPECT_NE(nullptr, first.Cache()); EXPECT_EQ(nullptr, second.Cache());
    EXPECT_TRUE(first.Compare(&second)); EXPECT_TRUE(first.Compare(&first)); EXPECT_FALSE(first.Compare(&second, -1));
}

TEST_F(TCBScaleEvaluationTest, LargeFiniteEaseEndsNormalizeTogetherBeforeHermite) {
    RCKTCBScaleController controller;
    AddScale(controller, 0, 0, 2, -4, 1, 0, 0, 0, FLT_MAX);
    AddScale(controller, 8, 16, 6, 4, 1, 0, 0, FLT_MAX, 0);
    // Both ease ends normalize to .5: F(.25)=.125. With zero tangents,
    // Hermite's far-key weight is 3/64 - 2/512 = 11/256.
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output));
    ScaleIs(output, .6875f, 2.171875f, -3.65625f);
}

TEST_F(TCBScaleEvaluationTest, NegativeAndCollapsedAxesRemainSignedCurveValues) {
    RCKTCBScaleController controller;
    AddScale(controller, 0, -4, -2, 1); AddScale(controller, 8, 0, 2, 5);
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); ScaleIs(output, -2, 0, 3);
    ASSERT_TRUE(controller.Evaluate(6, &output)); ScaleIs(output, -1, 1, 4);
}

class TCBScaleAnimationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(TCBScaleAnimationTest, ObjectAnimationUsesTheTCBCurveAndHonorsIgnoreScale) {
    RCKObjectAnimation animation(Context);
    RCKTCBScaleController *controller = static_cast<RCKTCBScaleController *>(animation.CreateController(CKANIMATION_TCBSCL_CONTROL));
    ASSERT_NE(nullptr, controller); AddScale(*controller, 0, 2, 4, -6); AddScale(*controller, 8, 10, 12, 2, 1);
    VxVector output; ASSERT_TRUE(animation.EvaluateScale(4, output)); ScaleIs(output, 7, 9, -1);
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNORESCALE);
    EXPECT_FALSE(animation.EvaluateScale(4, output)); ScaleIs(output, 0, 0, 0);
}

TEST_F(TCBScaleAnimationTest, PlaybackPublishesNonlinearScaleAndPreservesTranslation) {
    RCK3dEntity entity(Context); VxMatrix initial = VxMatrix::Identity();
    initial[3].Set(5, 6, 7); entity.SetWorldMatrix(initial);
    RCKObjectAnimation animation(Context); animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleController *controller = static_cast<RCKTCBScaleController *>(animation.CreateController(CKANIMATION_TCBSCL_CONTROL));
    ASSERT_NE(nullptr, controller); AddScale(*controller, 0, 2, 4, 6); AddScale(*controller, 8, 10, 12, 14, 1);
    animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_FLOAT_EQ(7, local[0][0]); EXPECT_FLOAT_EQ(9, local[1][1]); EXPECT_FLOAT_EQ(11, local[2][2]);
    EXPECT_FLOAT_EQ(5, local[3][0]); EXPECT_FLOAT_EQ(6, local[3][1]); EXPECT_FLOAT_EQ(7, local[3][2]);
    animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAnimationTest, PlaybackRetainsTheSmallContributionNearTheFinalZeroScale) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleController *controller = static_cast<RCKTCBScaleController *>(animation.CreateController(CKANIMATION_TCBSCL_CONTROL));
    ASSERT_NE(nullptr, controller);
    const float large = 1.0e30f;
    AddScale(*controller, -large, large, 2 * large, 3 * large); AddScale(*controller, 2, 0, 0, 0);
    animation.SetLength(2); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    // The two-key zero-TCB curve is a line. Frame one is one time unit
    // before the final zero scale, so each axis retains its unit slope.
    EXPECT_FLOAT_EQ(1, local[0][0]); EXPECT_FLOAT_EQ(2, local[1][1]); EXPECT_FLOAT_EQ(3, local[2][2]);
    animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAnimationTest, PlaybackPublishesRepresentableScaleAfterWideHermiteTermsCancel) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleController *controller = static_cast<RCKTCBScaleController *>(animation.CreateController(CKANIMATION_TCBSCL_CONTROL));
    ASSERT_NE(nullptr, controller); const float large = .95f * FLT_MAX;
    AddScale(*controller, 0, -large, 2, 3, 1); AddScale(*controller, 1, large, 2, 3);
    AddScale(*controller, 2, large, 2, 3, -1);
    animation.SetLength(2); ASSERT_EQ(CK_OK, animation.SetStep(.75f));
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_FLOAT_EQ(large, local[0][0]); EXPECT_FLOAT_EQ(2, local[1][1]); EXPECT_FLOAT_EQ(3, local[2][2]);
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_TRUE(std::isfinite(local[row][column]));
    animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAnimationTest, PlaybackPreservesMirroringCollapsedAxesAndRotationComposition) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleController *controller = static_cast<RCKTCBScaleController *>(animation.CreateController(CKANIMATION_TCBSCL_CONTROL));
    ASSERT_NE(nullptr, controller); AddScale(*controller, 0, -4, -2, 1); AddScale(*controller, 8, 0, 2, 5);
    const float halfSqrt = static_cast<float>(std::sqrt(.5));
    VxQuaternion rotation(0, 0, halfSqrt, halfSqrt); animation.AddRotationKey(0, &rotation);
    VxVector translation(5, 6, 7); animation.AddPositionKey(0, &translation);
    animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &local = entity.GetLocalMatrix();
    // Scale (-2,0,3) followed by a Z quarter-turn leaves a mirrored Y row.
    EXPECT_NEAR(0, local[0][0], 1.0e-5f); EXPECT_NEAR(2, local[0][1], 1.0e-5f);
    EXPECT_NEAR(0, local[1][0], 1.0e-5f); EXPECT_NEAR(0, local[1][1], 1.0e-5f);
    EXPECT_NEAR(3, local[2][2], 1.0e-5f);
    EXPECT_FLOAT_EQ(5, local[3][0]); EXPECT_FLOAT_EQ(6, local[3][1]); EXPECT_FLOAT_EQ(7, local[3][2]);
    animation.Set3dEntity(nullptr);
}
} // namespace
