#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "BezierReferenceSamples.h"
extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class InspectBezierPosition : public RCKBezierPositionController {
public:
    bool Ready() const { return m_TangentsComputed != FALSE; }
    float Distance(int first, int second) { return ComputeKeyDistance(first, second); }
};
void AddPosition(RCKBezierPositionController &controller, float time, VxVector position,
                 CKBEZIERKEY_FLAGS inMode = BEZIER_KEY_AUTOSMOOTH,
                 CKBEZIERKEY_FLAGS outMode = BEZIER_KEY_AUTOSMOOTH,
                 VxVector in = VxVector(0, 0, 0), VxVector out = VxVector(0, 0, 0)) {
    CKBezierKeyFlags flags; flags.SetInTangentMode(inMode); flags.SetOutTangentMode(outMode);
    CKBezierPositionKey key(time, position, flags, in, out); ASSERT_GE(controller.AddKey(&key), 0);
}
CKBezierPositionKey &Key(RCKBezierPositionController &controller, int index) {
    return *static_cast<CKBezierPositionKey *>(controller.GetKey(index));
}
void Vec(const VxVector &actual, float x, float y = 0, float z = 0, float tolerance = 2.0e-6f) {
    EXPECT_NEAR(x, actual.x, tolerance); EXPECT_NEAR(y, actual.y, tolerance); EXPECT_NEAR(z, actual.z, tolerance);
}
class BezierPositionEvaluationTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
};

TEST_F(BezierPositionEvaluationTest, EmptyReturnsFalseWithoutWritingOrComputing) {
    InspectBezierPosition controller; VxVector output(11, -22, 33);
    EXPECT_FALSE(controller.Evaluate(0, &output)); EXPECT_FALSE(controller.Evaluate(0, nullptr));
    Vec(output, 11, -22, 33, 0); EXPECT_FALSE(controller.Ready());
}
TEST_F(BezierPositionEvaluationTest, SingleSmoothKeyClampsAndClearsBothUnusedDerivatives) {
    InspectBezierPosition controller; AddPosition(controller, 3, VxVector(2, -7, 11));
    Key(controller, 0).In.Set(4, 5, 6); Key(controller, 0).Out.Set(7, 8, 9);
    struct Guarded { CKDWORD Before; VxVector Value; CKDWORD After; } output;
    const float times[] = {-FLT_MAX, -1, 3, 20, FLT_MAX};
    for (float time : times) {
        output.Before = 0x13572468; output.After = 0xfedcba98;
        ASSERT_TRUE(controller.Evaluate(time, &output.Value)); Vec(output.Value, 2, -7, 11, 0);
        EXPECT_EQ(0x13572468u, output.Before); EXPECT_EQ(0xfedcba98u, output.After);
    }
    EXPECT_TRUE(controller.Ready()); Vec(Key(controller, 0).In, 0); Vec(Key(controller, 0).Out, 0);
}
TEST_F(BezierPositionEvaluationTest, TwoSmoothKeysReduceToTheLineWithSignedIncomingDerivative) {
    InspectBezierPosition controller; AddPosition(controller, 2, VxVector(0, 2, -4)); AddPosition(controller, 10, VxVector(8, 6, 4));
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); Vec(output, 2, 3, -2);
    Vec(Key(controller, 0).Out, 1, .5f, 1); Vec(Key(controller, 1).In, -1, -.5f, -1);
    Vec(Key(controller, 0).In, 0); Vec(Key(controller, 1).Out, 0);
}
TEST_F(BezierPositionEvaluationTest, SlowDerivativesGiveIndependentCubicSmoothstep) {
    RCKBezierPositionController controller;
    AddPosition(controller, 0, VxVector(0, 2, -4), BEZIER_KEY_SLOW, BEZIER_KEY_SLOW);
    AddPosition(controller, 8, VxVector(8, 6, 4), BEZIER_KEY_SLOW, BEZIER_KEY_SLOW);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 1.25f, 2.625f, -2.75f);
    ASSERT_TRUE(controller.Evaluate(6, &output)); Vec(output, 6.75f, 5.375f, 2.75f);
}
TEST_F(BezierPositionEvaluationTest, ExplicitDerivativesAreTimeScaledAndRetainTheirStoredSigns) {
    RCKBezierPositionController controller;
    AddPosition(controller, 0, VxVector(0, 0, 0), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS, VxVector(9, 8, 7), VxVector(2, 4, -2));
    AddPosition(controller, 8, VxVector(8, 16, -8), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS, VxVector(0, 0, 0), VxVector(6, 5, 4));
    // At t=.25, h00=27/32,h01=5/32, dt*h10=9/8.
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 3.5f, 7, -3.5f);
    Vec(Key(controller, 0).In, 9, 8, 7, 0); Vec(Key(controller, 0).Out, 2, 4, -2, 0);
}
TEST_F(BezierPositionEvaluationTest, UnevenSmoothIntervalsHaveIndependentEndpointAndInteriorDerivatives) {
    RCKBezierPositionController controller;
    AddPosition(controller, 0, VxVector(0, 0, 0)); AddPosition(controller, 1, VxVector(4, 0, 0)); AddPosition(controller, 4, VxVector(10, 0, 0));
    controller.ComputeBezierPts(); Vec(Key(controller, 0).Out, 4.5f); Vec(Key(controller, 1).In, -3.5f);
    Vec(Key(controller, 1).Out, 3.5f); Vec(Key(controller, 2).In, -.5f);
    VxVector output; ASSERT_TRUE(controller.Evaluate(.5f, &output)); Vec(output, 2.125f);
    ASSERT_TRUE(controller.Evaluate(2.5f, &output)); Vec(output, 8.125f);
}
TEST_F(BezierPositionEvaluationTest, LinearAndFastModesUseOneAndTwoTimesSecantDerivatives) {
    for (int fast = 0; fast < 2; ++fast) {
        RCKBezierPositionController controller;
        const CKBEZIERKEY_FLAGS mode = fast ? BEZIER_KEY_FAST : BEZIER_KEY_LINEAR;
        AddPosition(controller, 2, VxVector(0, 0, 0), mode, mode); AddPosition(controller, 10, VxVector(8, 0, 0), mode, mode);
        controller.ComputeBezierPts(); Vec(Key(controller, 0).Out, fast ? 2 : 1); Vec(Key(controller, 1).In, fast ? -2 : -1);
        VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); Vec(output, fast ? 2.75f : 2);
    }
}
TEST_F(BezierPositionEvaluationTest, LinearNeighborMismatchUsesEndpointExtrapolation) {
    RCKBezierPositionController controller;
    AddPosition(controller, 0, VxVector(0, 0, 0), BEZIER_KEY_LINEAR, BEZIER_KEY_LINEAR);
    AddPosition(controller, 1, VxVector(4, 0, 0), BEZIER_KEY_SLOW, BEZIER_KEY_LINEAR);
    AddPosition(controller, 4, VxVector(10, 0, 0), BEZIER_KEY_SLOW, BEZIER_KEY_LINEAR);
    controller.ComputeBezierPts(); Vec(Key(controller, 0).Out, 4.5f); Vec(Key(controller, 1).Out, 2);
}
TEST_F(BezierPositionEvaluationTest, IndexedSmoothMirrorsTheAlreadyComputedExplicitOppositeSide) {
    InspectBezierPosition controller;
    AddPosition(controller, 0, VxVector(0, 0, 0));
    AddPosition(controller, 1, VxVector(4, 0, 0), BEZIER_KEY_AUTOSMOOTH, BEZIER_KEY_TANGENTS, VxVector(1, 2, 3), VxVector(7, -8, 9));
    AddPosition(controller, 4, VxVector(10, 0, 0));
    controller.ComputeBezierPts(1); Vec(Key(controller, 1).In, -7, 8, -9, 0); Vec(Key(controller, 1).Out, 7, -8, 9, 0);
    EXPECT_FALSE(controller.Ready());
}
TEST_F(BezierPositionEvaluationTest, IndexedSmoothWithOppositeStepUsesZeroWithoutTouchingOtherKeys) {
    RCKBezierPositionController controller;
    AddPosition(controller, 0, VxVector(0, 0, 0));
    AddPosition(controller, 1, VxVector(4, 0, 0), BEZIER_KEY_AUTOSMOOTH, BEZIER_KEY_STEP, VxVector(1, 2, 3), VxVector(7, -8, 9));
    AddPosition(controller, 4, VxVector(10, 0, 0)); Key(controller, 0).Out.Set(10, 20, 30);
    controller.ComputeBezierPts(1); Vec(Key(controller, 1).In, 0); Vec(Key(controller, 1).Out, 7, -8, 9, 0); Vec(Key(controller, 0).Out, 10, 20, 30, 0);
}
TEST_F(BezierPositionEvaluationTest, IndexedNonSmoothKeyIsAnExactNoOpAndDoesNotMarkTheWholeCacheReady) {
    InspectBezierPosition controller;
    AddPosition(controller, 0, VxVector(1, 2, 3), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS, VxVector(4, 5, 6), VxVector(7, 8, 9));
    CKBezierPositionKey before = Key(controller, 0); controller.ComputeBezierPts(0);
    EXPECT_EQ(0, std::memcmp(&before, &Key(controller, 0), sizeof(before))); EXPECT_FALSE(controller.Ready());
    controller.ComputeBezierPts(-1); controller.ComputeBezierPts(1); EXPECT_FALSE(controller.Ready());
}
TEST_F(BezierPositionEvaluationTest, EndpointSmoothRetainsNativeSingleKeyOppositeSideEffect) {
    RCKBezierPositionController controller;
    AddPosition(controller, 0, VxVector(1, 2, 3), BEZIER_KEY_AUTOSMOOTH, BEZIER_KEY_TANGENTS, VxVector(4, 5, 6), VxVector(7, 8, 9));
    controller.ComputeBezierPts(0); Vec(Key(controller, 0).In, 0); Vec(Key(controller, 0).Out, 0);
}
TEST_F(BezierPositionEvaluationTest, KeyDistanceMeasuresTimeAndWrapThroughControllerLength) {
    InspectBezierPosition controller; AddPosition(controller, 2, VxVector(100, 0, 0)); AddPosition(controller, 6, VxVector(-100, 0, 0));
    controller.SetLength(10); EXPECT_FLOAT_EQ(4, controller.Distance(0, 1)); EXPECT_FLOAT_EQ(6, controller.Distance(1, 0)); EXPECT_FLOAT_EQ(10, controller.Distance(1, 1));
    EXPECT_FLOAT_EQ(0, controller.Distance(-1, 0)); EXPECT_FLOAT_EQ(0, controller.Distance(0, 2));
}
TEST_F(BezierPositionEvaluationTest, KeyDistanceKeepsRepresentableWrapAfterTheWideIntermediateSum) {
    InspectBezierPosition controller;
    AddPosition(controller, .5f * FLT_MAX, VxVector(0, 0, 0));
    AddPosition(controller, .75f * FLT_MAX, VxVector(1, 0, 0));
    controller.SetLength(FLT_MAX);
    EXPECT_FLOAT_EQ(.75f * FLT_MAX, controller.Distance(1, 0));
    EXPECT_FLOAT_EQ(FLT_MAX, controller.Distance(1, 1));
}
TEST_F(BezierPositionEvaluationTest, StepIsActiveFromEitherAdjacentSideAndChangesOnlyAtTheExactRightTime) {
    for (int side = 0; side < 2; ++side) {
        RCKBezierPositionController controller;
        AddPosition(controller, 0, VxVector(2, 4, 6), BEZIER_KEY_SLOW, side ? BEZIER_KEY_SLOW : BEZIER_KEY_STEP);
        AddPosition(controller, 4, VxVector(8, 10, 12), side ? BEZIER_KEY_STEP : BEZIER_KEY_SLOW, BEZIER_KEY_SLOW);
        AddPosition(controller, 8, VxVector(20, 30, 40), BEZIER_KEY_SLOW, BEZIER_KEY_SLOW);
        VxVector output; ASSERT_TRUE(controller.Evaluate(std::nextafter(4.0f, 0.0f), &output)); Vec(output, 2, 4, 6, 0);
        ASSERT_TRUE(controller.Evaluate(4, &output)); Vec(output, 8, 10, 12, 0);
    }
}
TEST_F(BezierPositionEvaluationTest, StepDoesNotJumpEarlyWhenTheNormalizedTimeRoundsToOne) {
    RCKBezierPositionController controller;
    AddPosition(controller, -1.0e30f, VxVector(2, 4, 6), BEZIER_KEY_STEP, BEZIER_KEY_STEP);
    AddPosition(controller, 2, VxVector(8, 10, 12), BEZIER_KEY_STEP, BEZIER_KEY_STEP);
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output)); Vec(output, 2, 4, 6, 0);
}
TEST_F(BezierPositionEvaluationTest, DuplicateTimesSelectTheFirstExactTieAndDepartFromTheLastTie) {
    const unsigned int words[] = {4,0,0,0,0,0x00100010,0x40800000,0x41200000,0,0,0x00100010,0x40800000,0x41a00000,0,0,0x00100010,0x41000000,0x42200000,0,0,0x00100010};
    unsigned int wire[sizeof(words)/4]; std::memcpy(wire, words, sizeof(words));
    RCKBezierPositionController controller; ASSERT_EQ(sizeof(words), controller.ReadKeysFrom(wire));
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); Vec(output, 10, 0, 0, 0);
    ASSERT_TRUE(controller.Evaluate(6, &output)); Vec(output, 30);
}
TEST_F(BezierPositionEvaluationTest, EveryOriginalBezierFixtureRetainsItsSamplesAndComputedDerivatives) {
    int fixtures = 0, samples = 0;
    for (const BezierReference::Fixture &fixture : BezierReference::Fixtures) {
        SCOPED_TRACE(fixture.name); RCKBezierPositionController controller; unsigned int wire[128];
        ASSERT_LE(fixture.wordCount, 128); std::memcpy(wire, fixture.words, fixture.wordCount * 4);
        ASSERT_EQ(fixture.wordCount * 4, controller.ReadKeysFrom(wire)); controller.SetLength(20);
        for (int i = 0; i < fixture.sampleCount; ++i) {
            const BezierReference::Sample &sample = fixture.samples[i]; SCOPED_TRACE(sample.time); VxVector output;
            ASSERT_TRUE(controller.Evaluate(sample.time, &output)); Vec(output, sample.value[0], sample.value[1], sample.value[2], 5.0e-5f); ++samples;
        }
        for (int i = 0; i < controller.GetKeyCount(); ++i) {
            Vec(Key(controller, i).In, fixture.tangents[i][0], fixture.tangents[i][1], fixture.tangents[i][2], 5.0e-5f);
            Vec(Key(controller, i).Out, fixture.tangents[i][3], fixture.tangents[i][4], fixture.tangents[i][5], 5.0e-5f);
        }
        ++fixtures;
    }
    EXPECT_EQ(43, fixtures); EXPECT_EQ(611, samples);
}
TEST_F(BezierPositionEvaluationTest, WideFiniteTimesAndValuesRetainRepresentableAutomaticDerivatives) {
    RCKBezierPositionController controller;
    AddPosition(controller, -FLT_MAX, VxVector(-FLT_MAX, -FLT_MAX, 0)); AddPosition(controller, FLT_MAX, VxVector(FLT_MAX, FLT_MAX, 0));
    controller.ComputeBezierPts(); Vec(Key(controller, 0).Out, 1, 1, 0); Vec(Key(controller, 1).In, -1, -1, 0);
    VxVector output; ASSERT_TRUE(controller.Evaluate(0, &output)); Vec(output, 0);
}
TEST_F(BezierPositionEvaluationTest, TinyFiniteIntervalsKeepUnitSmoothDerivativesWithoutReciprocalOverflow) {
    const float tiny = std::ldexp(1.0f, -130); RCKBezierPositionController controller;
    AddPosition(controller, -tiny, VxVector(-tiny, 0, 0)); AddPosition(controller, 0, VxVector(0, 0, 0)); AddPosition(controller, tiny, VxVector(tiny, 0, 0));
    controller.ComputeBezierPts(); Vec(Key(controller, 0).Out, 1); Vec(Key(controller, 1).Out, 1); Vec(Key(controller, 1).In, -1); Vec(Key(controller, 2).In, -1);
}
TEST_F(BezierPositionEvaluationTest, FiniteExplicitControlOffsetsMayCancelBeforeTheFinalRepresentableValue) {
    RCKBezierPositionController controller; const float large = 1.0e30f;
    AddPosition(controller, 0, VxVector(1, 1, 2), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS, VxVector(0, 0, 0), VxVector(large, 0, 0));
    AddPosition(controller, large, VxVector(1, 1, 2), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS, VxVector(-large, 0, 0), VxVector(0, 0, 0));
    VxVector output; ASSERT_TRUE(controller.Evaluate(large * .5f, &output)); Vec(output, 1, 1, 2);
}
TEST_F(BezierPositionEvaluationTest, AsymmetricTimelineKeepsRepresentableNearEndpointContributionsInBothDirections) {
    const float large = 1.0e30f;
    for (int reversed = 0; reversed < 2; ++reversed) {
        SCOPED_TRACE(reversed); RCKBezierPositionController controller;
        AddPosition(controller, reversed ? -large : -2, VxVector(reversed ? large : 0, 0, 0));
        AddPosition(controller, reversed ? 2 : large, VxVector(reversed ? 0 : large, 0, 0));
        VxVector output; ASSERT_TRUE(controller.Evaluate(reversed ? 1.0f : -1.0f, &output)); Vec(output, 1);
    }
}
TEST_F(BezierPositionEvaluationTest, SamplingReusesComputedDerivativesUntilExplicitRecomputeWithoutChangingMetadata) {
    InspectBezierPosition controller; AddPosition(controller, 0, VxVector(0, 0, 0)); AddPosition(controller, 8, VxVector(8, 0, 0)); controller.SetLength(20);
    VxVector output; ASSERT_TRUE(controller.Evaluate(4, &output)); Vec(output, 4); EXPECT_TRUE(controller.Ready());
    Key(controller, 0).Out.Set(0, 0, 0); ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, .875f);
    controller.ComputeBezierPts(); ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 2); EXPECT_FLOAT_EQ(20, controller.GetLength());
}
TEST_F(BezierPositionEvaluationTest, CompareUsesInclusiveTimeAndPositionThresholdAndIgnoresNonExplicitDerivedVectors) {
    RCKBezierPositionController first, second;
    AddPosition(first, 2, VxVector(1, -2, 3)); AddPosition(second, 2.25f, VxVector(1.25f, -2.25f, 3.25f));
    Key(second, 0).In.Set(100, 200, 300); Key(second, 0).Out.Set(400, 500, 600);
    EXPECT_TRUE(first.Compare(&second, .25f)); EXPECT_TRUE(second.Compare(&first, .25f));
    EXPECT_FALSE(first.Compare(&second, std::nextafter(.25f, 0.0f)));
}
TEST_F(BezierPositionEvaluationTest, CompareRequiresExactFlagsAndExplicitTangentsDespiteLargeThreshold) {
    RCKBezierPositionController first, second;
    AddPosition(first, 2, VxVector(1, 2, 3), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS, VxVector(4, 5, 6), VxVector(7, 8, 9)); ASSERT_TRUE(second.Clone(&first));
    Key(second, 0).In.y += .001f; EXPECT_FALSE(first.Compare(&second, FLT_MAX)); Key(second, 0).In = Key(first, 0).In;
    Key(second, 0).Out.z += .001f; EXPECT_FALSE(first.Compare(&second, FLT_MAX)); Key(second, 0).Out = Key(first, 0).Out;
    Key(second, 0).Flags.SetOutTangentMode(BEZIER_KEY_LINEAR); EXPECT_FALSE(first.Compare(&second, FLT_MAX));
}
TEST_F(BezierPositionEvaluationTest, CompareRejectsWrongTypeAndCountAndIgnoresLengthAndCache) {
    InspectBezierPosition first, second; RCKBezierScaleController wrong;
    EXPECT_TRUE(first.Compare(&second, -1)); EXPECT_FALSE(first.Compare(nullptr)); EXPECT_FALSE(first.Compare(&wrong));
    AddPosition(first, 0, VxVector(0, 0, 0)); AddPosition(first, 8, VxVector(8, 0, 0)); ASSERT_TRUE(second.Clone(&first));
    first.ComputeBezierPts(); first.SetLength(42); second.SetLength(17); EXPECT_TRUE(first.Compare(&second)); EXPECT_TRUE(first.Compare(&first)); EXPECT_FALSE(first.Compare(&second, -1));
    second.RemoveKey(1); EXPECT_FALSE(first.Compare(&second));
}

class BezierPositionAnimationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp()); CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT); CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
TEST_F(BezierPositionAnimationTest, NonlinearBezierPositionReachesTheEntityAndPreservesOtherChannels) {
    RCK3dEntity entity(Context); VxMatrix initial = VxMatrix::Identity(); initial[0].Set(2, 0, 0); initial[1].Set(0, 3, 0); initial[2].Set(0, 0, 4); entity.SetWorldMatrix(initial);
    RCKObjectAnimation animation(Context); animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKBezierPositionController *controller = static_cast<RCKBezierPositionController *>(animation.CreateController(CKANIMATION_BEZIERPOS_CONTROL)); ASSERT_NE(nullptr, controller);
    AddPosition(*controller, 0, VxVector(0, 2, -4), BEZIER_KEY_SLOW, BEZIER_KEY_SLOW); AddPosition(*controller, 8, VxVector(8, 6, 4), BEZIER_KEY_SLOW, BEZIER_KEY_SLOW);
    animation.SetLength(8); ASSERT_EQ(CK_OK, animation.SetStep(.25f)); VxVector position; entity.GetPosition(&position); Vec(position, 1.25f, 2.625f, -2.75f);
    EXPECT_FLOAT_EQ(2, entity.GetLocalMatrix()[0][0]); EXPECT_FLOAT_EQ(3, entity.GetLocalMatrix()[1][1]); EXPECT_FLOAT_EQ(4, entity.GetLocalMatrix()[2][2]);
    animation.Set3dEntity(nullptr);
}
TEST_F(BezierPositionAnimationTest, StepPositionDoesNotPublishItsFinalKeyBeforeItsActualTime) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context); animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKBezierPositionController *controller = static_cast<RCKBezierPositionController *>(animation.CreateController(CKANIMATION_BEZIERPOS_CONTROL)); ASSERT_NE(nullptr, controller);
    AddPosition(*controller, -1.0e30f, VxVector(2, 4, 6), BEZIER_KEY_STEP, BEZIER_KEY_STEP); AddPosition(*controller, 2, VxVector(8, 10, 12), BEZIER_KEY_STEP, BEZIER_KEY_STEP);
    animation.SetLength(2); ASSERT_EQ(CK_OK, animation.SetStep(.5f)); VxVector position; entity.GetPosition(&position); Vec(position, 2, 4, 6, 0); animation.Set3dEntity(nullptr);
}
TEST_F(BezierPositionAnimationTest, NearEndpointCurveRetainsItsRepresentableEntityTranslation) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context); animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    RCKBezierPositionController *controller = static_cast<RCKBezierPositionController *>(animation.CreateController(CKANIMATION_BEZIERPOS_CONTROL)); ASSERT_NE(nullptr, controller);
    AddPosition(*controller, -1.0e30f, VxVector(1.0e30f, 0, 0)); AddPosition(*controller, 2, VxVector(0, 0, 0));
    animation.SetLength(2); ASSERT_EQ(CK_OK, animation.SetStep(.5f)); VxVector position; entity.GetPosition(&position); Vec(position, 1); animation.Set3dEntity(nullptr);
}
} // namespace
