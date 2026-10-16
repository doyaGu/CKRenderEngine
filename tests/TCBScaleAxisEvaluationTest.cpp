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

class InspectTCBScaleAxis : public RCKTCBScaleAxisController {
public:
    const VxQuaternion *Cache() const { return m_Tangents; }
};

VxQuaternion ZRotation(double degrees) {
    const double halfAngle = degrees * 3.14159265358979323846 / 360.0;
    return VxQuaternion(0, 0, static_cast<float>(std::sin(halfAngle)),
                        static_cast<float>(std::cos(halfAngle)));
}

VxQuaternion ZChord(double firstDegrees, double lastDegrees, double lastWeight = .5) {
    // Independent analytic components for the native close-angle LERP rule.
    // This is deliberately not normalized: Squad preserves this convention.
    const double first = firstDegrees * 3.14159265358979323846 / 360.0;
    const double last = lastDegrees * 3.14159265358979323846 / 360.0;
    return VxQuaternion(0, 0,
        static_cast<float>((1 - lastWeight) * std::sin(first) + lastWeight * std::sin(last)),
        static_cast<float>((1 - lastWeight) * std::cos(first) + lastWeight * std::cos(last)));
}

void AxisIs(const VxQuaternion &actual, const VxQuaternion &expected, float tolerance = 2.0e-6f) {
    EXPECT_NEAR(expected.x, actual.x, tolerance);
    EXPECT_NEAR(expected.y, actual.y, tolerance);
    EXPECT_NEAR(expected.z, actual.z, tolerance);
    EXPECT_NEAR(expected.w, actual.w, tolerance);
}

void AddAxis(RCKTCBScaleAxisController &controller, float time, VxQuaternion rotation,
                 float tension = 0, float continuity = 0, float bias = 0,
                 float easeTo = 0, float easeFrom = 0) {
    CKTCBScaleAxisKey key(time, rotation, tension, continuity, bias, easeTo, easeFrom);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void ReadWire(RCKTCBScaleAxisController &controller, const float *keys, int count) {
    // Independent 2.1 layout: count, then time,x,y,z,w,T,C,B,easeto,easefrom.
    unsigned char wire[4 + 40 * 8] = {};
    ASSERT_GE(count, 0); ASSERT_LE(count, 8);
    std::memcpy(wire, &count, 4);
    if (count) std::memcpy(wire + 4, keys, 40 * count);
    ASSERT_EQ(4 + 40 * count, controller.ReadKeysFrom(wire));
}

class TCBScaleAxisEvaluationTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailNextTangentArray = false; }
};
}

void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextTangentArray) { FailNextTangentArray = false; return nullptr; }
    try { return ::operator new[](bytes); }
    catch (const std::bad_alloc &) { return nullptr; }
}

namespace {
TEST_F(TCBScaleAxisEvaluationTest, EmptyReturnsFalseWithoutWritingOutputOrAllocating) {
    InspectTCBScaleAxis controller; VxQuaternion output(11, -22, 33, -44);
    FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(0, &output)); EXPECT_FALSE(controller.Evaluate(10, nullptr));
    AxisIs(output, VxQuaternion(11, -22, 33, -44), 0);
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBScaleAxisEvaluationTest, SingleKeyClampsRawQuaternionWithoutAllocatingOrOverwritingGuards) {
    InspectTCBScaleAxis controller; const VxQuaternion stored(-2, 3, -4, -5);
    AddAxis(controller, 3, stored);
    struct Guarded { CKDWORD Before; VxQuaternion Value; CKDWORD After; } output;
    const float times[] = {-FLT_MAX, -1, 3, 20, FLT_MAX};
    FailNextTangentArray = true;
    for (float time : times) {
        SCOPED_TRACE(time); output.Before = 0x13572468; output.After = 0xfedcba98;
        ASSERT_TRUE(controller.Evaluate(time, &output.Value)); AxisIs(output.Value, stored, 0);
        EXPECT_EQ(0x13572468u, output.Before); EXPECT_EQ(0xfedcba98u, output.After);
    }
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBScaleAxisEvaluationTest, TwoZeroTCBKeysReduceToConstantAngularSpeed) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 2, ZRotation(0)); AddAxis(controller, 10, ZRotation(90));
    const float times[] = {2, 3, 4, 6, 8, 10};
    const double angles[] = {0, 11.25, 22.5, 45, 67.5, 90};
    for (int index = 0; index < 6; ++index) {
        SCOPED_TRACE(times[index]); VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output)); AxisIs(output, ZRotation(angles[index]));
    }
    ASSERT_NE(nullptr, controller.Cache());
    AxisIs(controller.Cache()[1], ZRotation(0)); AxisIs(controller.Cache()[2], ZRotation(90));
}

TEST_F(TCBScaleAxisEvaluationTest, EndpointTensionsChangeSquadControlsAndAngularPhase) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 2, ZRotation(0), .5f); AddAxis(controller, 10, ZRotation(90), -.5f);
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(6, &output)); AxisIs(output, ZRotation(33.75));
    // For a common axis the log-angle controls are a0-T0*d/2 and a1+T1*d/2.
    AxisIs(controller.Cache()[1], ZRotation(-22.5)); AxisIs(controller.Cache()[2], ZRotation(67.5));
    ASSERT_TRUE(controller.Evaluate(4, &output)); AxisIs(output, ZRotation(14.0625));
}

TEST_F(TCBScaleAxisEvaluationTest, EndpointContinuityAndBiasRemainActiveForTwoRotationKeys) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 0, ZRotation(0), 0, .5f, .5f);
    AddAxis(controller, 8, ZRotation(90), 0, .5f, .5f);
    // The inner arcs end at 45 and 56.25 degrees. Their close-angle final
    // interpolation is a component chord, rather than a normalized 50.625 arc.
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); AxisIs(output, ZChord(45, 56.25));
    AxisIs(controller.Cache()[1], ZRotation(11.25)); AxisIs(controller.Cache()[2], ZRotation(101.25));
}

TEST_F(TCBScaleAxisEvaluationTest, UnevenTimesUseWeightedLogDifferencesAtTheMiddleKey) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 0, ZRotation(0)); AddAxis(controller, 1, ZRotation(60)); AddAxis(controller, 4, ZRotation(120));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); AxisIs(output, ZChord(30, 37.5));
    AxisIs(controller.Cache()[2], ZRotation(75)); AxisIs(controller.Cache()[3], ZRotation(75));
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); AxisIs(output, ZChord(90, 97.5));
}

TEST_F(TCBScaleAxisEvaluationTest, NonzeroTCBWeightsIncomingAndOutgoingControlsIndependently) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 0, ZRotation(0));
    AddAxis(controller, 1, ZRotation(60), .25f, .5f, -.5f);
    AddAxis(controller, 4, ZRotation(120));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); AxisIs(output, ZChord(30, 34.453125));
    AxisIs(controller.Cache()[2], ZRotation(68.90625)); AxisIs(controller.Cache()[3], ZRotation(51.09375));
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); AxisIs(output, ZChord(90, 85.546875));
}

TEST_F(TCBScaleAxisEvaluationTest, OriginalRuntimeQuaternionFixturesMatchAllThirtyThreeSamples) {
    int fixtures = 0, samples = 0;
    for (const TCBReference::Fixture &fixture : TCBReference::Fixtures) {
        if (!fixture.rotation) continue;
        ++fixtures; SCOPED_TRACE(fixture.name); RCKTCBScaleAxisController controller;
        ReadWire(controller, fixture.keys, fixture.keyCount);
        for (int index = 0; index < fixture.sampleCount; ++index) {
            const TCBReference::Sample &sample = fixture.samples[index]; SCOPED_TRACE(sample.time);
            VxQuaternion output; ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            AxisIs(output, VxQuaternion(sample.value[0], sample.value[1], sample.value[2], sample.value[3]), 5.0e-5f);
            ++samples;
        }
    }
    EXPECT_EQ(3, fixtures); EXPECT_EQ(33, samples);
}

TEST_F(TCBScaleAxisEvaluationTest, HemisphereCorrectionDoesNotRewriteTheStoredAntipodalKey) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 0, ZRotation(0)); AddAxis(controller, 8, -ZRotation(90));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); AxisIs(output, ZRotation(45));
    const CKTCBScaleAxisKey *key = static_cast<CKTCBScaleAxisKey *>(controller.GetKey(1));
    ASSERT_NE(nullptr, key); AxisIs(key->Rot, -ZRotation(90), 0);
    AxisIs(controller.Cache()[2], -ZRotation(90));
    ASSERT_TRUE(controller.Evaluate(8, &output)); AxisIs(output, -ZRotation(90), 0);
}

TEST_F(TCBScaleAxisEvaluationTest, AntipodalRepresentationsOfOnePoseStayConstantInsideTheSegment) {
    RCKTCBScaleAxisController controller; const VxQuaternion rotation = ZRotation(60);
    AddAxis(controller, 0, rotation); AddAxis(controller, 8, -rotation);
    const float times[] = {1, 2, 4, 6, 7};
    for (float time : times) {
        SCOPED_TRACE(time); VxQuaternion output; ASSERT_TRUE(controller.Evaluate(time, &output)); AxisIs(output, rotation);
    }
}

TEST_F(TCBScaleAxisEvaluationTest, ExactInteriorKeyKeepsThePrecedingSegmentsShortestArcSign) {
    RCKTCBScaleAxisController controller;
    AddAxis(controller, 0, ZRotation(0)); AddAxis(controller, 4, -ZRotation(60)); AddAxis(controller, 8, ZRotation(120));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); AxisIs(output, ZRotation(60));
    ASSERT_TRUE(controller.Evaluate(8, &output)); AxisIs(output, ZRotation(120), 0);
}

TEST_F(TCBScaleAxisEvaluationTest, DuplicateInteriorTimesSelectFirstTieThenLeaveFromLastTie) {
    const float keys[] = {
        0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        1, 0, 0, .2588190451f, .9659258263f, 0, 0, 0, 0, 0,
        1, 0, 0, .5f, .8660254038f, 0, 0, 0, 0, 0,
        4, 0, 0, .7071067812f, .7071067812f, 0, 0, 0, 0, 0
    };
    RCKTCBScaleAxisController controller; ReadWire(controller, keys, 4);
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(1, &output)); AxisIs(output, ZRotation(30));
    // Controls 75/90 form an inner half chord, then the outer half chord
    // against the base 75-degree arc gives .75*q75 + .25*q90.
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); AxisIs(output, ZChord(75, 90, .25));
}

TEST_F(TCBScaleAxisEvaluationTest, DuplicateBoundaryTimesClampFirstAndLastRawValuesWithoutBuildingCache) {
    const float keys[] = {
        1, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
        5, 0, 0, .6f, .8f, 0, 0, 0, 0, 0,
        5, 0, 0, -.6f, -.8f, 0, 0, 0, 0, 0
    };
    InspectTCBScaleAxis controller; ReadWire(controller, keys, 4);
    FailNextTangentArray = true; VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(1, &output)); AxisIs(output, VxQuaternion(0, 0, 0, 1), 0);
    ASSERT_TRUE(controller.Evaluate(5, &output)); AxisIs(output, VxQuaternion(0, 0, -.6f, -.8f), 0);
    EXPECT_EQ(nullptr, controller.Cache()); EXPECT_TRUE(FailNextTangentArray);
}

TEST_F(TCBScaleAxisEvaluationTest, EaseUsesLeftEaseFromAndRightEaseToWithQuadraticEndsAndLinearMiddle) {
    RCKTCBScaleAxisController controller;
    AddAxis(controller, 0, ZRotation(0), 0, 0, 0, .9f, .25f);
    AddAxis(controller, 8, ZRotation(90), 0, 0, 0, .5f, .9f);
    const float times[] = {1, 2, 3, 4, 6};
    const double angles[] = {4.5, 18, 36, 54, 81};
    for (int index = 0; index < 5; ++index) {
        SCOPED_TRACE(times[index]); VxQuaternion output; ASSERT_TRUE(controller.Evaluate(times[index], &output));
        AxisIs(output, ZRotation(angles[index]));
    }
}

TEST_F(TCBScaleAxisEvaluationTest, EaseAboveOneNormalizesBothEndsTogether) {
    RCKTCBScaleAxisController controller;
    AddAxis(controller, 0, ZRotation(0), 0, 0, 0, 0, 1);
    AddAxis(controller, 8, ZRotation(90), 0, 0, 0, 3, 0);
    // 1:3 normalizes to .25:.75. At t=.5 the final quadratic gives 2/3.
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); AxisIs(output, ZRotation(60));
}

TEST_F(TCBScaleAxisEvaluationTest, FiniteLargeEaseEndsNormalizeWithoutOverflowingTheirSum) {
    RCKTCBScaleAxisController controller;
    AddAxis(controller, 0, ZRotation(0), 0, 0, 0, 0, FLT_MAX);
    AddAxis(controller, 8, ZRotation(90), 0, 0, 0, FLT_MAX, 0);
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(2, &output)); AxisIs(output, ZRotation(11.25));
    ASSERT_TRUE(controller.Evaluate(6, &output)); AxisIs(output, ZRotation(78.75));

}

TEST_F(TCBScaleAxisEvaluationTest, OpposingWideFiniteTimesPreserveAngularPhaseAndInteriorControls) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, -FLT_MAX, ZRotation(0)); AddAxis(controller, 0, ZRotation(45)); AddAxis(controller, FLT_MAX, ZRotation(90));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(-FLT_MAX * .5f, &output)); AxisIs(output, ZRotation(22.5));
    ASSERT_TRUE(controller.Evaluate(FLT_MAX * .5f, &output)); AxisIs(output, ZRotation(67.5));
    AxisIs(controller.Cache()[2], ZRotation(45)); AxisIs(controller.Cache()[3], ZRotation(45));
}

TEST_F(TCBScaleAxisEvaluationTest, TinyFiniteIntervalsDoNotOverflowTangentTimeWeightReciprocal) {
    const float tiny = std::ldexp(1.0f, -130);
    InspectTCBScaleAxis controller;
    AddAxis(controller, -tiny, ZRotation(0)); AddAxis(controller, 0, ZRotation(45)); AddAxis(controller, tiny, ZRotation(90));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(-tiny * .5f, &output)); AxisIs(output, ZRotation(22.5));
    AxisIs(controller.Cache()[2], ZRotation(45)); AxisIs(controller.Cache()[3], ZRotation(45));
}

TEST_F(TCBScaleAxisEvaluationTest, AsymmetricTimeSpansRetainSmallAxisComponentsNearEitherEndpoint) {
    const float large = 1.0e30f;
    const float expected = static_cast<float>((3.14159265358979323846 / 4) / static_cast<double>(large));
    for (int reversed = 0; reversed < 2; ++reversed) {
        for (int negativeLast = 0; negativeLast < 2; ++negativeLast) {
            SCOPED_TRACE(reversed * 2 + negativeLast);
            RCKTCBScaleAxisController controller;
            VxQuaternion first = reversed ? ZRotation(90) : ZRotation(0);
            VxQuaternion last = reversed ? ZRotation(0) : ZRotation(90);
            if (negativeLast) last = -last;
            const float firstTime = reversed ? -large : -2.0f;
            const float lastTime = reversed ? 2.0f : large;
            AddAxis(controller, firstTime, first); AddAxis(controller, lastTime, last);
            const float sampleTime = reversed ? 1.0f : -1.0f;
            VxQuaternion output; ASSERT_TRUE(controller.Evaluate(sampleTime, &output));
            // The zero-TCB curve is a shortest-arc constant-speed rotation.
            // At one time unit from identity, z=sin(pi/(4*large)); its cubic
            // correction is below float range, leaving this representable term.
            EXPECT_FLOAT_EQ(0, output.x); EXPECT_FLOAT_EQ(0, output.y); EXPECT_FLOAT_EQ(1, output.w);
            EXPECT_NEAR(expected, output.z, expected * 2.0e-6f); EXPECT_GT(output.z, 0);
            ASSERT_TRUE(controller.Evaluate(lastTime, &output)); AxisIs(output, last, 0);
            // Rotation uses the same native Squad evaluator. Exercise it too,
            // including the hemisphere flip needed after reversing -identity.
            RCKTCBRotationController rotation;
            CKTCBRotationKey firstKey(firstTime, first), lastKey(lastTime, last);
            ASSERT_EQ(0, rotation.AddKey(&firstKey)); ASSERT_EQ(1, rotation.AddKey(&lastKey));
            ASSERT_TRUE(rotation.Evaluate(sampleTime, &output));
            EXPECT_FLOAT_EQ(0, output.x); EXPECT_FLOAT_EQ(0, output.y); EXPECT_FLOAT_EQ(1, output.w);
            EXPECT_NEAR(expected, output.z, expected * 2.0e-6f); EXPECT_GT(output.z, 0);
            ASSERT_TRUE(rotation.Evaluate(lastTime, &output)); AxisIs(output, last, 0);
        }
    }
}

TEST_F(TCBScaleAxisEvaluationTest, LazyCacheFailureLeavesOutputUntouchedAndRetryBuildsTheCurve) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 0, ZRotation(0)); AddAxis(controller, 8, ZRotation(90));
    VxQuaternion output(11, -22, 33, -44); FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(4, &output)); AxisIs(output, VxQuaternion(11, -22, 33, -44), 0);
    EXPECT_EQ(nullptr, controller.Cache()); EXPECT_FALSE(FailNextTangentArray);
    ASSERT_TRUE(controller.Evaluate(4, &output)); AxisIs(output, ZRotation(45)); EXPECT_NE(nullptr, controller.Cache());
}

TEST_F(TCBScaleAxisEvaluationTest, ExplicitRecomputationPublishesBorrowedKeyEditsOnlyAfterSuccessfulAllocation) {
    InspectTCBScaleAxis controller;
    AddAxis(controller, 0, ZRotation(0)); AddAxis(controller, 1, ZRotation(60)); AddAxis(controller, 4, ZRotation(120));
    controller.ComputeTangents(); const VxQuaternion *cache = controller.Cache(); ASSERT_NE(nullptr, cache);
    CKTCBScaleAxisKey *key = static_cast<CKTCBScaleAxisKey *>(controller.GetKey(1)); ASSERT_NE(nullptr, key);
    key->Rot = ZRotation(30); FailNextTangentArray = true; controller.ComputeTangents(); EXPECT_EQ(cache, controller.Cache());
    AxisIs(controller.Cache()[2], ZRotation(75));
    controller.ComputeTangents(); ASSERT_NE(nullptr, controller.Cache()); EXPECT_NE(cache, controller.Cache());
    // Uneven weights .5/1.5 with adjacent angular differences 30/90:
    // incoming = 30 + .5*((1-.25)*30-.25*90) = 30 degrees.
    AxisIs(controller.Cache()[2], ZRotation(30)); AxisIs(controller.Cache()[3], ZRotation(30));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); AxisIs(output, ZRotation(15));
}

TEST_F(TCBScaleAxisEvaluationTest, SamplingReusesCacheAndPreservesKeysLengthAndBorrowedAddresses) {
    InspectTCBScaleAxis controller; AddAxis(controller, 0, ZRotation(0)); AddAxis(controller, 8, ZRotation(90));
    controller.SetLength(42); unsigned char before[84], after[84]; ASSERT_EQ(84, controller.DumpKeysTo(before));
    CKKey *key = controller.GetKey(0); VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output));
    const VxQuaternion *cache = controller.Cache(); FailNextTangentArray = true;
    ASSERT_TRUE(controller.Evaluate(6, &output)); AxisIs(output, ZRotation(67.5));
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(cache, controller.Cache()); EXPECT_EQ(key, controller.GetKey(0));
    EXPECT_FLOAT_EQ(42, controller.GetLength()); ASSERT_EQ(84, controller.DumpKeysTo(after)); EXPECT_EQ(0, std::memcmp(before, after, 84));
}

TEST_F(TCBScaleAxisEvaluationTest, CompareRejectsNullWrongTypeAndDifferentCounts) {
    RCKTCBScaleAxisController first, second; RCKTCBRotationController rotation;
    EXPECT_FALSE(first.Compare(nullptr)); EXPECT_FALSE(first.Compare(&rotation));
    AddAxis(first, 0, ZRotation(0)); EXPECT_FALSE(first.Compare(&second)); EXPECT_FALSE(second.Compare(&first));
}

TEST_F(TCBScaleAxisEvaluationTest, CompareUsesInclusiveThresholdForTimeAndAllFourComponents) {
    RCKTCBScaleAxisController first, second;
    AddAxis(first, 2, VxQuaternion(1, -2, 3, -4)); AddAxis(second, 2.25f, VxQuaternion(1.25f, -2.25f, 3.25f, -4.25f));
    EXPECT_TRUE(first.Compare(&second, .25f)); EXPECT_TRUE(second.Compare(&first, .25f));
    EXPECT_FALSE(first.Compare(&second, std::nextafter(.25f, 0.0f)));
    EXPECT_FALSE(second.Compare(&first, std::nextafter(.25f, 0.0f)));
}

TEST_F(TCBScaleAxisEvaluationTest, CompareRequiresExactEqualityOfEverySplineParameterRegardlessOfThreshold) {
    RCKTCBScaleAxisController first, second;
    AddAxis(first, 2, ZRotation(0), .25f, -.5f, .75f, .125f, .375f); ASSERT_TRUE(second.Clone(&first));
    CKTCBScaleAxisKey *key = static_cast<CKTCBScaleAxisKey *>(second.GetKey(0)); ASSERT_NE(nullptr, key);
    float *parameters[] = {&key->tension, &key->continuity, &key->bias, &key->easeto, &key->easefrom};
    for (float *parameter : parameters) {
        const float saved = *parameter; *parameter = std::nextafter(saved, 1.0f);
        EXPECT_FALSE(first.Compare(&second, 100)); EXPECT_FALSE(second.Compare(&first, 100));
        *parameter = saved; EXPECT_TRUE(first.Compare(&second));
    }
}

TEST_F(TCBScaleAxisEvaluationTest, CompareChecksEveryTimeAndQuaternionComponentAtEveryKey) {
    RCKTCBScaleAxisController first, second;
    AddAxis(first, 0, ZRotation(0)); AddAxis(first, 8, ZRotation(90)); ASSERT_TRUE(second.Clone(&first));
    for (int index = 0; index < 2; ++index) {
        CKTCBScaleAxisKey *key = static_cast<CKTCBScaleAxisKey *>(second.GetKey(index)); ASSERT_NE(nullptr, key);
        float *fields[] = {&key->TimeStep, &key->Rot.x, &key->Rot.y, &key->Rot.z, &key->Rot.w};
        for (float *field : fields) {
            const float saved = *field; *field += .25f;
            EXPECT_FALSE(first.Compare(&second, .125f)); EXPECT_FALSE(second.Compare(&first, .125f));
            EXPECT_TRUE(first.Compare(&second, .25f)); *field = saved;
        }
    }
}

TEST_F(TCBScaleAxisEvaluationTest, CompareUsesStoredComponentsRatherThanRotationEquivalence) {
    RCKTCBScaleAxisController first, second;
    AddAxis(first, 0, ZRotation(60)); AddAxis(second, 0, -ZRotation(60));
    EXPECT_FALSE(first.Compare(&second)); EXPECT_TRUE(first.Compare(&second, 2));
}

TEST_F(TCBScaleAxisEvaluationTest, CompareIgnoresLengthAndLazyCacheState) {
    InspectTCBScaleAxis first, second;
    EXPECT_TRUE(first.Compare(&second)); EXPECT_TRUE(first.Compare(&second, -1));
    AddAxis(first, 0, ZRotation(0)); AddAxis(first, 8, ZRotation(90)); ASSERT_TRUE(second.Clone(&first));
    first.SetLength(7); second.SetLength(99); first.ComputeTangents();
    ASSERT_NE(nullptr, first.Cache()); EXPECT_EQ(nullptr, second.Cache()); EXPECT_TRUE(first.Compare(&second));
    EXPECT_FALSE(first.Compare(&second, -1));
}


class TCBScaleAxisAnimationTest : public ::testing::Test {
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

void AddOtherChannels(RCKObjectAnimation &animation, bool xQuarterTurn = false) {
    VxVector position(5, 6, 7), scale(2, 4, 6);
    const float halfSqrt = static_cast<float>(std::sqrt(.5));
    VxQuaternion rotation = xQuarterTurn ? VxQuaternion(halfSqrt, 0, 0, halfSqrt) : VxQuaternion(0, 0, 0, 1);
    animation.AddPositionKey(0, &position); animation.AddScaleKey(0, &scale); animation.AddRotationKey(0, &rotation);
}

void StretchIs(const VxMatrix &actual, double axisDegrees, bool xQuarterTurn = false) {
    // Independent symmetric stretch U*diag(2,4,6)*U^T from a Z principal-axis
    // angle, followed by an optional X object rotation. Do not call the
    // production quaternion/matrix interpolators to construct this oracle.
    const double radians = axisDegrees * 3.14159265358979323846 / 180;
    const double cosine = std::cos(radians), sine = std::sin(radians);
    const double xx = 2 * cosine * cosine + 4 * sine * sine;
    const double xy = -2 * cosine * sine;
    const double yy = 2 * sine * sine + 4 * cosine * cosine;
    const double plain[3][3] = {{xx, xy, 0}, {xy, yy, 0}, {0, 0, 6}};
    const double rotated[3][3] = {{xx, 0, -xy}, {xy, 0, -yy}, {0, 6, 0}};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            SCOPED_TRACE(row * 3 + column);
            EXPECT_NEAR(xQuarterTurn ? rotated[row][column] : plain[row][column], actual[row][column], 2.0e-5f);
        }
        EXPECT_FLOAT_EQ(0, actual[row][3]);
    }
    EXPECT_FLOAT_EQ(5, actual[3][0]); EXPECT_FLOAT_EQ(6, actual[3][1]); EXPECT_FLOAT_EQ(7, actual[3][2]);
    EXPECT_FLOAT_EQ(1, actual[3][3]);
}

TEST_F(TCBScaleAxisAnimationTest, ObjectAnimationUsesNonlinearAxisAndIgnoreLeavesOutputUntouched) {
    RCKObjectAnimation animation(Context);
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller); EXPECT_EQ(controller, animation.GetScaleAxisController());
    AddAxis(*controller, 2, ZRotation(0), .5f); AddAxis(*controller, 10, ZRotation(90), -.5f);
    VxQuaternion output; ASSERT_TRUE(animation.EvaluateScaleAxis(6, output)); AxisIs(output, ZRotation(33.75));
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNORESCALEROT);
    EXPECT_FALSE(animation.EvaluateScaleAxis(8, output)); AxisIs(output, ZRotation(33.75));
}

TEST_F(TCBScaleAxisAnimationTest, NonlinearAxisRotatesUnequalStretchWithoutRotatingTheObject) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller); AddAxis(*controller, 0, ZRotation(0), .5f); AddAxis(*controller, 8, ZRotation(90), -.5f);
    AddOtherChannels(animation); animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    StretchIs(entity.GetLocalMatrix(), 33.75); animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAxisAnimationTest, NonlinearAxisStretchPrecedesNoncommutingObjectRotation) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller); AddAxis(*controller, 0, ZRotation(0), .5f); AddAxis(*controller, 8, ZRotation(90), -.5f);
    AddOtherChannels(animation, true); animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    StretchIs(entity.GetLocalMatrix(), 33.75, true); animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAxisAnimationTest, LargeFiniteEaseEndsPublishTheQuarterTimePrincipalStretch) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddAxis(*controller, 0, ZRotation(0), 0, 0, 0, 0, FLT_MAX);
    AddAxis(*controller, 8, ZRotation(90), 0, 0, 0, FLT_MAX, 0);
    AddOtherChannels(animation, true); animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.25f));
    StretchIs(entity.GetLocalMatrix(), 11.25, true); animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAxisAnimationTest, AntipodalAxisSignsProduceTheSamePhysicalStretch) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller); AddAxis(*controller, 0, ZRotation(45)); AddAxis(*controller, 8, -ZRotation(45));
    AddOtherChannels(animation, true); animation.SetLength(8);
    const float steps[] = {0, .25f, .5f, .75f, 1};
    for (float step : steps) {
        SCOPED_TRACE(step); ASSERT_EQ(CK_OK, animation.SetStep(step)); StretchIs(entity.GetLocalMatrix(), 45, true);
    }
    animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAxisAnimationTest, IgnoringAxisUsesOrdinaryScaleAndPreservesControllerKeys) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller); AddAxis(*controller, 0, ZRotation(0), .5f); AddAxis(*controller, 8, ZRotation(90), -.5f);
    AddOtherChannels(animation, true); animation.SetLength(8);
    unsigned char before[84], after[84]; ASSERT_EQ(84, controller->DumpKeysTo(before));
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNORESCALEROT);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f)); StretchIs(entity.GetLocalMatrix(), 0, true);
    ASSERT_EQ(84, controller->DumpKeysTo(after)); EXPECT_EQ(0, std::memcmp(before, after, sizeof(before)));
    animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAxisAnimationTest, CloseAngleChordControlsPreserveTheirPhysicalStretchDirection) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddAxis(*controller, 0, ZRotation(0), 0, .5f, .5f); AddAxis(*controller, 8, ZRotation(90), 0, .5f, .5f);
    AddOtherChannels(animation, true); animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    // The raw sample is the unnormalized chord .5*q45+.5*q56.25.
    // Quaternion-to-matrix removes only its common norm, so the resulting
    // principal-axis direction is the analytic angle bisector 50.625 degrees.
    StretchIs(entity.GetLocalMatrix(), 50.625, true); animation.Set3dEntity(nullptr);
}

TEST_F(TCBScaleAxisAnimationTest, SmallAxisPhaseKeepsTheRepresentableShearOfLargePrincipalScales) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBScaleAxisController *controller = static_cast<RCKTCBScaleAxisController *>(animation.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL));
    ASSERT_NE(nullptr, controller); const float large = 1.0e30f;
    AddAxis(*controller, -large, ZRotation(90)); AddAxis(*controller, 2, ZRotation(0));
    VxVector position(5, 6, 7), scale(large, 2 * large, 3);
    VxQuaternion rotation(0, 0, 0, 1);
    animation.AddPositionKey(0, &position); animation.AddScaleKey(0, &scale); animation.AddRotationKey(0, &rotation);
    animation.SetLength(2); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &matrix = entity.GetLocalMatrix();
    // theta=pi/(2*large); symmetric stretch off-diagonal is
    // (large-2*large)*cos(theta)*sin(theta) -> -pi/2, not zero.
    EXPECT_NEAR(-3.14159265358979323846 / 2, matrix[0][1], 2.0e-5f);
    EXPECT_NEAR(-3.14159265358979323846 / 2, matrix[1][0], 2.0e-5f);
    EXPECT_FLOAT_EQ(large, matrix[0][0]); EXPECT_FLOAT_EQ(2 * large, matrix[1][1]); EXPECT_FLOAT_EQ(3, matrix[2][2]);
    EXPECT_FLOAT_EQ(5, matrix[3][0]); EXPECT_FLOAT_EQ(6, matrix[3][1]); EXPECT_FLOAT_EQ(7, matrix[3][2]);
    animation.Set3dEntity(nullptr);
}
} // namespace
