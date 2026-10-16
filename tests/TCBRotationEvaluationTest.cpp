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

class InspectTCBRotation : public RCKTCBRotationController {
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

void RotationIs(const VxQuaternion &actual, const VxQuaternion &expected, float tolerance = 2.0e-6f) {
    EXPECT_NEAR(expected.x, actual.x, tolerance);
    EXPECT_NEAR(expected.y, actual.y, tolerance);
    EXPECT_NEAR(expected.z, actual.z, tolerance);
    EXPECT_NEAR(expected.w, actual.w, tolerance);
}

void AddRotation(RCKTCBRotationController &controller, float time, VxQuaternion rotation,
                 float tension = 0, float continuity = 0, float bias = 0,
                 float easeTo = 0, float easeFrom = 0) {
    CKTCBRotationKey key(time, rotation, tension, continuity, bias, easeTo, easeFrom);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void ReadWire(RCKTCBRotationController &controller, const float *keys, int count) {
    // Independent 2.1 layout: count, then time,x,y,z,w,T,C,B,easeto,easefrom.
    unsigned char wire[4 + 40 * 8] = {};
    ASSERT_GE(count, 0); ASSERT_LE(count, 8);
    std::memcpy(wire, &count, 4);
    if (count) std::memcpy(wire + 4, keys, 40 * count);
    ASSERT_EQ(4 + 40 * count, controller.ReadKeysFrom(wire));
}

class TCBRotationEvaluationTest : public ::testing::Test {
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
TEST_F(TCBRotationEvaluationTest, EmptyReturnsFalseWithoutWritingOutputOrAllocating) {
    InspectTCBRotation controller; VxQuaternion output(11, -22, 33, -44);
    FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(0, &output)); EXPECT_FALSE(controller.Evaluate(10, nullptr));
    RotationIs(output, VxQuaternion(11, -22, 33, -44), 0);
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBRotationEvaluationTest, SingleKeyClampsRawQuaternionWithoutAllocatingOrOverwritingGuards) {
    InspectTCBRotation controller; const VxQuaternion stored(-2, 3, -4, -5);
    AddRotation(controller, 3, stored);
    struct Guarded { CKDWORD Before; VxQuaternion Value; CKDWORD After; } output;
    const float times[] = {-FLT_MAX, -1, 3, 20, FLT_MAX};
    FailNextTangentArray = true;
    for (float time : times) {
        SCOPED_TRACE(time); output.Before = 0x13572468; output.After = 0xfedcba98;
        ASSERT_TRUE(controller.Evaluate(time, &output.Value)); RotationIs(output.Value, stored, 0);
        EXPECT_EQ(0x13572468u, output.Before); EXPECT_EQ(0xfedcba98u, output.After);
    }
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(nullptr, controller.Cache());
}

TEST_F(TCBRotationEvaluationTest, TwoZeroTCBKeysReduceToConstantAngularSpeed) {
    InspectTCBRotation controller;
    AddRotation(controller, 2, ZRotation(0)); AddRotation(controller, 10, ZRotation(90));
    const float times[] = {2, 3, 4, 6, 8, 10};
    const double angles[] = {0, 11.25, 22.5, 45, 67.5, 90};
    for (int index = 0; index < 6; ++index) {
        SCOPED_TRACE(times[index]); VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(times[index], &output)); RotationIs(output, ZRotation(angles[index]));
    }
    ASSERT_NE(nullptr, controller.Cache());
    RotationIs(controller.Cache()[1], ZRotation(0)); RotationIs(controller.Cache()[2], ZRotation(90));
}

TEST_F(TCBRotationEvaluationTest, EndpointTensionsChangeSquadControlsAndAngularPhase) {
    InspectTCBRotation controller;
    AddRotation(controller, 2, ZRotation(0), .5f); AddRotation(controller, 10, ZRotation(90), -.5f);
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(6, &output)); RotationIs(output, ZRotation(33.75));
    // For a common axis the log-angle controls are a0-T0*d/2 and a1+T1*d/2.
    RotationIs(controller.Cache()[1], ZRotation(-22.5)); RotationIs(controller.Cache()[2], ZRotation(67.5));
    ASSERT_TRUE(controller.Evaluate(4, &output)); RotationIs(output, ZRotation(14.0625));
}

TEST_F(TCBRotationEvaluationTest, EndpointContinuityAndBiasRemainActiveForTwoRotationKeys) {
    InspectTCBRotation controller;
    AddRotation(controller, 0, ZRotation(0), 0, .5f, .5f);
    AddRotation(controller, 8, ZRotation(90), 0, .5f, .5f);
    // The inner arcs end at 45 and 56.25 degrees. Their close-angle final
    // interpolation is a component chord, rather than a normalized 50.625 arc.
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); RotationIs(output, ZChord(45, 56.25));
    RotationIs(controller.Cache()[1], ZRotation(11.25)); RotationIs(controller.Cache()[2], ZRotation(101.25));
}

TEST_F(TCBRotationEvaluationTest, UnevenTimesUseWeightedLogDifferencesAtTheMiddleKey) {
    InspectTCBRotation controller;
    AddRotation(controller, 0, ZRotation(0)); AddRotation(controller, 1, ZRotation(60)); AddRotation(controller, 4, ZRotation(120));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); RotationIs(output, ZChord(30, 37.5));
    RotationIs(controller.Cache()[2], ZRotation(75)); RotationIs(controller.Cache()[3], ZRotation(75));
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); RotationIs(output, ZChord(90, 97.5));
}

TEST_F(TCBRotationEvaluationTest, NonzeroTCBWeightsIncomingAndOutgoingControlsIndependently) {
    InspectTCBRotation controller;
    AddRotation(controller, 0, ZRotation(0));
    AddRotation(controller, 1, ZRotation(60), .25f, .5f, -.5f);
    AddRotation(controller, 4, ZRotation(120));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); RotationIs(output, ZChord(30, 34.453125));
    RotationIs(controller.Cache()[2], ZRotation(68.90625)); RotationIs(controller.Cache()[3], ZRotation(51.09375));
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); RotationIs(output, ZChord(90, 85.546875));
}

TEST_F(TCBRotationEvaluationTest, OriginalRuntimeQuaternionFixturesMatchAllThirtyThreeSamples) {
    int fixtures = 0, samples = 0;
    for (const TCBReference::Fixture &fixture : TCBReference::Fixtures) {
        if (!fixture.rotation) continue;
        ++fixtures; SCOPED_TRACE(fixture.name); RCKTCBRotationController controller;
        ReadWire(controller, fixture.keys, fixture.keyCount);
        for (int index = 0; index < fixture.sampleCount; ++index) {
            const TCBReference::Sample &sample = fixture.samples[index]; SCOPED_TRACE(sample.time);
            VxQuaternion output; ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            RotationIs(output, VxQuaternion(sample.value[0], sample.value[1], sample.value[2], sample.value[3]), 5.0e-5f);
            ++samples;
        }
    }
    EXPECT_EQ(3, fixtures); EXPECT_EQ(33, samples);
}

TEST_F(TCBRotationEvaluationTest, HemisphereCorrectionDoesNotRewriteTheStoredAntipodalKey) {
    InspectTCBRotation controller;
    AddRotation(controller, 0, ZRotation(0)); AddRotation(controller, 8, -ZRotation(90));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); RotationIs(output, ZRotation(45));
    const CKTCBRotationKey *key = static_cast<CKTCBRotationKey *>(controller.GetKey(1));
    ASSERT_NE(nullptr, key); RotationIs(key->Rot, -ZRotation(90), 0);
    RotationIs(controller.Cache()[2], -ZRotation(90));
    ASSERT_TRUE(controller.Evaluate(8, &output)); RotationIs(output, -ZRotation(90), 0);
}

TEST_F(TCBRotationEvaluationTest, AntipodalRepresentationsOfOnePoseStayConstantInsideTheSegment) {
    RCKTCBRotationController controller; const VxQuaternion rotation = ZRotation(60);
    AddRotation(controller, 0, rotation); AddRotation(controller, 8, -rotation);
    const float times[] = {1, 2, 4, 6, 7};
    for (float time : times) {
        SCOPED_TRACE(time); VxQuaternion output; ASSERT_TRUE(controller.Evaluate(time, &output)); RotationIs(output, rotation);
    }
}

TEST_F(TCBRotationEvaluationTest, ExactInteriorKeyKeepsThePrecedingSegmentsShortestArcSign) {
    RCKTCBRotationController controller;
    AddRotation(controller, 0, ZRotation(0)); AddRotation(controller, 4, -ZRotation(60)); AddRotation(controller, 8, ZRotation(120));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); RotationIs(output, ZRotation(60));
    ASSERT_TRUE(controller.Evaluate(8, &output)); RotationIs(output, ZRotation(120), 0);
}

TEST_F(TCBRotationEvaluationTest, DuplicateInteriorTimesSelectFirstTieThenLeaveFromLastTie) {
    const float keys[] = {
        0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        1, 0, 0, .2588190451f, .9659258263f, 0, 0, 0, 0, 0,
        1, 0, 0, .5f, .8660254038f, 0, 0, 0, 0, 0,
        4, 0, 0, .7071067812f, .7071067812f, 0, 0, 0, 0, 0
    };
    RCKTCBRotationController controller; ReadWire(controller, keys, 4);
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(1, &output)); RotationIs(output, ZRotation(30));
    // Controls 75/90 form an inner half chord, then the outer half chord
    // against the base 75-degree arc gives .75*q75 + .25*q90.
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); RotationIs(output, ZChord(75, 90, .25));
}

TEST_F(TCBRotationEvaluationTest, DuplicateBoundaryTimesClampFirstAndLastRawValuesWithoutBuildingCache) {
    const float keys[] = {
        1, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
        5, 0, 0, .6f, .8f, 0, 0, 0, 0, 0,
        5, 0, 0, -.6f, -.8f, 0, 0, 0, 0, 0
    };
    InspectTCBRotation controller; ReadWire(controller, keys, 4);
    FailNextTangentArray = true; VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(1, &output)); RotationIs(output, VxQuaternion(0, 0, 0, 1), 0);
    ASSERT_TRUE(controller.Evaluate(5, &output)); RotationIs(output, VxQuaternion(0, 0, -.6f, -.8f), 0);
    EXPECT_EQ(nullptr, controller.Cache()); EXPECT_TRUE(FailNextTangentArray);
}

TEST_F(TCBRotationEvaluationTest, EaseUsesLeftEaseFromAndRightEaseToWithQuadraticEndsAndLinearMiddle) {
    RCKTCBRotationController controller;
    AddRotation(controller, 0, ZRotation(0), 0, 0, 0, .9f, .25f);
    AddRotation(controller, 8, ZRotation(90), 0, 0, 0, .5f, .9f);
    const float times[] = {1, 2, 3, 4, 6};
    const double angles[] = {4.5, 18, 36, 54, 81};
    for (int index = 0; index < 5; ++index) {
        SCOPED_TRACE(times[index]); VxQuaternion output; ASSERT_TRUE(controller.Evaluate(times[index], &output));
        RotationIs(output, ZRotation(angles[index]));
    }
}

TEST_F(TCBRotationEvaluationTest, EaseAboveOneNormalizesBothEndsTogether) {
    RCKTCBRotationController controller;
    AddRotation(controller, 0, ZRotation(0), 0, 0, 0, 0, 1);
    AddRotation(controller, 8, ZRotation(90), 0, 0, 0, 3, 0);
    // 1:3 normalizes to .25:.75. At t=.5 the final quadratic gives 2/3.
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output)); RotationIs(output, ZRotation(60));
}

TEST_F(TCBRotationEvaluationTest, FiniteLargeEaseEndsNormalizeWithoutOverflowingTheirSum) {
    RCKTCBRotationController controller;
    AddRotation(controller, 0, ZRotation(0), 0, 0, 0, 0, FLT_MAX);
    AddRotation(controller, 8, ZRotation(90), 0, 0, 0, FLT_MAX, 0);
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(2, &output)); RotationIs(output, ZRotation(11.25));
    ASSERT_TRUE(controller.Evaluate(6, &output)); RotationIs(output, ZRotation(78.75));
    RCKTCBScaleAxisController axis;
    VxQuaternion first = ZRotation(0), last = ZRotation(90);
    CKTCBScaleAxisKey axisFirst(0, first, 0, 0, 0, 0, FLT_MAX), axisLast(8, last, 0, 0, 0, FLT_MAX, 0);
    ASSERT_EQ(0, axis.AddKey(&axisFirst)); ASSERT_EQ(1, axis.AddKey(&axisLast));
    ASSERT_TRUE(axis.Evaluate(2, &output)); RotationIs(output, ZRotation(11.25));
    ASSERT_TRUE(axis.Evaluate(6, &output)); RotationIs(output, ZRotation(78.75));
    RCKTCBScaleController scale;
    VxVector firstScale(0, 0, 0), lastScale(8, 16, -8);
    CKTCBScaleKey scaleFirst(0, firstScale, 0, 0, 0, 0, FLT_MAX), scaleLast(8, lastScale, 0, 0, 0, FLT_MAX, 0);
    ASSERT_EQ(0, scale.AddKey(&scaleFirst)); ASSERT_EQ(1, scale.AddKey(&scaleLast));
    VxVector scaleOutput; ASSERT_TRUE(scale.Evaluate(2, &scaleOutput));
    EXPECT_NEAR(1, scaleOutput.x, 2.0e-6f); EXPECT_NEAR(2, scaleOutput.y, 2.0e-6f); EXPECT_NEAR(-1, scaleOutput.z, 2.0e-6f);
    ASSERT_TRUE(scale.Evaluate(6, &scaleOutput));
    EXPECT_NEAR(7, scaleOutput.x, 2.0e-6f); EXPECT_NEAR(14, scaleOutput.y, 2.0e-6f); EXPECT_NEAR(-7, scaleOutput.z, 2.0e-6f);
}

TEST_F(TCBRotationEvaluationTest, OpposingWideFiniteTimesPreserveAngularPhaseAndInteriorControls) {
    InspectTCBRotation controller;
    AddRotation(controller, -FLT_MAX, ZRotation(0)); AddRotation(controller, 0, ZRotation(45)); AddRotation(controller, FLT_MAX, ZRotation(90));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(-FLT_MAX * .5f, &output)); RotationIs(output, ZRotation(22.5));
    ASSERT_TRUE(controller.Evaluate(FLT_MAX * .5f, &output)); RotationIs(output, ZRotation(67.5));
    RotationIs(controller.Cache()[2], ZRotation(45)); RotationIs(controller.Cache()[3], ZRotation(45));
}

TEST_F(TCBRotationEvaluationTest, TinyFiniteIntervalsDoNotOverflowTangentTimeWeightReciprocal) {
    const float tiny = std::ldexp(1.0f, -130);
    InspectTCBRotation controller;
    AddRotation(controller, -tiny, ZRotation(0)); AddRotation(controller, 0, ZRotation(45)); AddRotation(controller, tiny, ZRotation(90));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(-tiny * .5f, &output)); RotationIs(output, ZRotation(22.5));
    RotationIs(controller.Cache()[2], ZRotation(45)); RotationIs(controller.Cache()[3], ZRotation(45));
}

TEST_F(TCBRotationEvaluationTest, LazyCacheFailureLeavesOutputUntouchedAndRetryBuildsTheCurve) {
    InspectTCBRotation controller;
    AddRotation(controller, 0, ZRotation(0)); AddRotation(controller, 8, ZRotation(90));
    VxQuaternion output(11, -22, 33, -44); FailNextTangentArray = true;
    EXPECT_FALSE(controller.Evaluate(4, &output)); RotationIs(output, VxQuaternion(11, -22, 33, -44), 0);
    EXPECT_EQ(nullptr, controller.Cache()); EXPECT_FALSE(FailNextTangentArray);
    ASSERT_TRUE(controller.Evaluate(4, &output)); RotationIs(output, ZRotation(45)); EXPECT_NE(nullptr, controller.Cache());
}

TEST_F(TCBRotationEvaluationTest, ExplicitRecomputationPublishesBorrowedKeyEditsOnlyAfterSuccessfulAllocation) {
    InspectTCBRotation controller;
    AddRotation(controller, 0, ZRotation(0)); AddRotation(controller, 1, ZRotation(60)); AddRotation(controller, 4, ZRotation(120));
    controller.ComputeTangents(); const VxQuaternion *cache = controller.Cache(); ASSERT_NE(nullptr, cache);
    CKTCBRotationKey *key = static_cast<CKTCBRotationKey *>(controller.GetKey(1)); ASSERT_NE(nullptr, key);
    key->Rot = ZRotation(30); FailNextTangentArray = true; controller.ComputeTangents(); EXPECT_EQ(cache, controller.Cache());
    RotationIs(controller.Cache()[2], ZRotation(75));
    controller.ComputeTangents(); ASSERT_NE(nullptr, controller.Cache()); EXPECT_NE(cache, controller.Cache());
    // Uneven weights .5/1.5 with adjacent angular differences 30/90:
    // incoming = 30 + .5*((1-.25)*30-.25*90) = 30 degrees.
    RotationIs(controller.Cache()[2], ZRotation(30)); RotationIs(controller.Cache()[3], ZRotation(30));
    VxQuaternion output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); RotationIs(output, ZRotation(15));
}

TEST_F(TCBRotationEvaluationTest, SamplingReusesCacheAndPreservesKeysLengthAndBorrowedAddresses) {
    InspectTCBRotation controller; AddRotation(controller, 0, ZRotation(0)); AddRotation(controller, 8, ZRotation(90));
    controller.SetLength(42); unsigned char before[84], after[84]; ASSERT_EQ(84, controller.DumpKeysTo(before));
    CKKey *key = controller.GetKey(0); VxQuaternion output; ASSERT_TRUE(controller.Evaluate(4, &output));
    const VxQuaternion *cache = controller.Cache(); FailNextTangentArray = true;
    ASSERT_TRUE(controller.Evaluate(6, &output)); RotationIs(output, ZRotation(67.5));
    EXPECT_TRUE(FailNextTangentArray); EXPECT_EQ(cache, controller.Cache()); EXPECT_EQ(key, controller.GetKey(0));
    EXPECT_FLOAT_EQ(42, controller.GetLength()); ASSERT_EQ(84, controller.DumpKeysTo(after)); EXPECT_EQ(0, std::memcmp(before, after, 84));
}

TEST_F(TCBRotationEvaluationTest, CompareRejectsNullWrongTypeAndDifferentCounts) {
    RCKTCBRotationController first, second; RCKTCBScaleAxisController axis;
    EXPECT_FALSE(first.Compare(nullptr)); EXPECT_FALSE(first.Compare(&axis));
    AddRotation(first, 0, ZRotation(0)); EXPECT_FALSE(first.Compare(&second)); EXPECT_FALSE(second.Compare(&first));
}

TEST_F(TCBRotationEvaluationTest, CompareUsesInclusiveThresholdForTimeAndAllFourComponents) {
    RCKTCBRotationController first, second;
    AddRotation(first, 2, VxQuaternion(1, -2, 3, -4)); AddRotation(second, 2.25f, VxQuaternion(1.25f, -2.25f, 3.25f, -4.25f));
    EXPECT_TRUE(first.Compare(&second, .25f)); EXPECT_TRUE(second.Compare(&first, .25f));
    EXPECT_FALSE(first.Compare(&second, std::nextafter(.25f, 0.0f)));
    EXPECT_FALSE(second.Compare(&first, std::nextafter(.25f, 0.0f)));
}

TEST_F(TCBRotationEvaluationTest, CompareRequiresExactEqualityOfEverySplineParameterRegardlessOfThreshold) {
    RCKTCBRotationController first, second;
    AddRotation(first, 2, ZRotation(0), .25f, -.5f, .75f, .125f, .375f); ASSERT_TRUE(second.Clone(&first));
    CKTCBRotationKey *key = static_cast<CKTCBRotationKey *>(second.GetKey(0)); ASSERT_NE(nullptr, key);
    float *parameters[] = {&key->tension, &key->continuity, &key->bias, &key->easeto, &key->easefrom};
    for (float *parameter : parameters) {
        const float saved = *parameter; *parameter = std::nextafter(saved, 1.0f);
        EXPECT_FALSE(first.Compare(&second, 100)); EXPECT_FALSE(second.Compare(&first, 100));
        *parameter = saved; EXPECT_TRUE(first.Compare(&second));
    }
}

TEST_F(TCBRotationEvaluationTest, CompareChecksEveryTimeAndQuaternionComponentAtEveryKey) {
    RCKTCBRotationController first, second;
    AddRotation(first, 0, ZRotation(0)); AddRotation(first, 8, ZRotation(90)); ASSERT_TRUE(second.Clone(&first));
    for (int index = 0; index < 2; ++index) {
        CKTCBRotationKey *key = static_cast<CKTCBRotationKey *>(second.GetKey(index)); ASSERT_NE(nullptr, key);
        float *fields[] = {&key->TimeStep, &key->Rot.x, &key->Rot.y, &key->Rot.z, &key->Rot.w};
        for (float *field : fields) {
            const float saved = *field; *field += .25f;
            EXPECT_FALSE(first.Compare(&second, .125f)); EXPECT_FALSE(second.Compare(&first, .125f));
            EXPECT_TRUE(first.Compare(&second, .25f)); *field = saved;
        }
    }
}

TEST_F(TCBRotationEvaluationTest, CompareUsesStoredComponentsRatherThanRotationEquivalence) {
    RCKTCBRotationController first, second;
    AddRotation(first, 0, ZRotation(60)); AddRotation(second, 0, -ZRotation(60));
    EXPECT_FALSE(first.Compare(&second)); EXPECT_TRUE(first.Compare(&second, 2));
}

TEST_F(TCBRotationEvaluationTest, CompareIgnoresLengthAndLazyCacheState) {
    InspectTCBRotation first, second;
    EXPECT_TRUE(first.Compare(&second)); EXPECT_TRUE(first.Compare(&second, -1));
    AddRotation(first, 0, ZRotation(0)); AddRotation(first, 8, ZRotation(90)); ASSERT_TRUE(second.Clone(&first));
    first.SetLength(7); second.SetLength(99); first.ComputeTangents();
    ASSERT_NE(nullptr, first.Cache()); EXPECT_EQ(nullptr, second.Cache()); EXPECT_TRUE(first.Compare(&second));
    EXPECT_FALSE(first.Compare(&second, -1));
}

class TCBRotationAnimationTest : public ::testing::Test {
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

TEST_F(TCBRotationAnimationTest, ObjectAnimationUsesNonlinearTCBRotationAndHonorsIgnoreRotation) {
    RCKObjectAnimation animation(Context);
    RCKTCBRotationController *controller = static_cast<RCKTCBRotationController *>(animation.CreateController(CKANIMATION_TCBROT_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddRotation(*controller, 2, ZRotation(0), .5f); AddRotation(*controller, 10, ZRotation(90), -.5f);
    VxQuaternion output; ASSERT_TRUE(animation.EvaluateRotation(6, output)); RotationIs(output, ZRotation(33.75));
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNOREROT);
    EXPECT_FALSE(animation.EvaluateRotation(8, output)); RotationIs(output, VxQuaternion(0, 0, 0, 1), 0);
}

TEST_F(TCBRotationAnimationTest, PlaybackPublishesNonlinearRotationAndPreservesPositionAndScale) {
    RCK3dEntity entity(Context); VxMatrix initial = VxMatrix::Identity();
    initial[0].Set(2, 0, 0); initial[1].Set(0, 3, 0); initial[2].Set(0, 0, 4); initial[3].Set(5, 6, 7); entity.SetWorldMatrix(initial);
    RCKObjectAnimation animation(Context); animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBRotationController *controller = static_cast<RCKTCBRotationController *>(animation.CreateController(CKANIMATION_TCBROT_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddRotation(*controller, 0, ZRotation(0), .5f); AddRotation(*controller, 8, ZRotation(90), -.5f);
    animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const double angle = 33.75 * 3.14159265358979323846 / 180;
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_NEAR(2 * std::cos(angle), local[0][0], 1.0e-5f); EXPECT_NEAR(-2 * std::sin(angle), local[0][1], 1.0e-5f);
    EXPECT_NEAR(3 * std::sin(angle), local[1][0], 1.0e-5f); EXPECT_NEAR(3 * std::cos(angle), local[1][1], 1.0e-5f);
    EXPECT_NEAR(4, local[2][2], 1.0e-5f);
    EXPECT_FLOAT_EQ(5, local[3][0]); EXPECT_FLOAT_EQ(6, local[3][1]); EXPECT_FLOAT_EQ(7, local[3][2]); animation.Set3dEntity(nullptr);
}

TEST_F(TCBRotationAnimationTest, PlaybackUsesNormalizedFiniteLargeEaseAtTheQuarterPose) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKTCBRotationController *controller = static_cast<RCKTCBRotationController *>(animation.CreateController(CKANIMATION_TCBROT_CONTROL));
    ASSERT_NE(nullptr, controller);
    AddRotation(*controller, 0, ZRotation(0), 0, 0, 0, 0, FLT_MAX);
    AddRotation(*controller, 8, ZRotation(90), 0, 0, 0, FLT_MAX, 0);
    animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.25f));
    const double angle = 11.25 * 3.14159265358979323846 / 180;
    const VxMatrix &local = entity.GetLocalMatrix();
    EXPECT_NEAR(std::cos(angle), local[0][0], 1.0e-5f); EXPECT_NEAR(-std::sin(angle), local[0][1], 1.0e-5f);
    EXPECT_NEAR(std::sin(angle), local[1][0], 1.0e-5f); EXPECT_NEAR(std::cos(angle), local[1][1], 1.0e-5f);
    EXPECT_NEAR(1, local[2][2], 1.0e-5f); animation.Set3dEntity(nullptr);
}
} // namespace
