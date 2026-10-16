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
CKBezierScaleKey ScaleKey(float time, const VxVector &value,
                         CKBEZIERKEY_FLAGS inMode = BEZIER_KEY_LINEAR,
                         CKBEZIERKEY_FLAGS outMode = BEZIER_KEY_LINEAR,
                         const VxVector &in = VxVector(0, 0, 0),
                         const VxVector &out = VxVector(0, 0, 0)) {
    CKBezierScaleKey key;
    key.TimeStep = time; key.Pos = value; key.In = in; key.Out = out;
    key.Flags.SetInTangentMode(inMode); key.Flags.SetOutTangentMode(outMode);
    return key;
}

class InspectBezierScale : public RCKBezierScaleController {
public:
    CKBOOL Computed() const { return m_TangentsComputed; }
    float Distance(int first, int second) { return ComputeKeyDistance(first, second); }
};

class BezierScaleEvaluationTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    static CKBezierScaleKey *Key(CKAnimController &controller, int index) {
        return static_cast<CKBezierScaleKey *>(controller.GetKey(index));
    }
    static void Vec(const VxVector &actual, double x, double y, double z, double tolerance = 0.00002) {
        EXPECT_NEAR(x, actual.x, tolerance); EXPECT_NEAR(y, actual.y, tolerance); EXPECT_NEAR(z, actual.z, tolerance);
    }
    static void Fill(CKAnimController &controller, CKBEZIERKEY_FLAGS mode = BEZIER_KEY_LINEAR) {
        CKBezierScaleKey a = ScaleKey(0, VxVector(2, 4, 6), mode, mode);
        CKBezierScaleKey b = ScaleKey(8, VxVector(10, 20, 30), mode, mode);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b)); controller.SetLength(12);
    }
};

TEST_F(BezierScaleEvaluationTest, EmptyAndSingleKeyRespectTheThreeComponentOutputContract) {
    InspectBezierScale controller;
    float output[] = { 70, 80, 90, 123 };
    EXPECT_FALSE(controller.Evaluate(2, output)); EXPECT_FLOAT_EQ(70, output[0]); EXPECT_FALSE(controller.Computed());
    CKBezierScaleKey key = ScaleKey(2, VxVector(-2, 0, 3), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS,
                                  VxVector(1, 2, 3), VxVector(4, 5, 6));
    ASSERT_EQ(0, controller.AddKey(&key));
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(controller.Evaluate(static_cast<float>(i * 2), output));
        EXPECT_FLOAT_EQ(-2, output[0]); EXPECT_FLOAT_EQ(0, output[1]); EXPECT_FLOAT_EQ(3, output[2]);
        EXPECT_FLOAT_EQ(123, output[3]);
    }
    EXPECT_TRUE(controller.Computed()); Vec(Key(controller, 0)->In, 1, 2, 3); Vec(Key(controller, 0)->Out, 4, 5, 6);
}

TEST_F(BezierScaleEvaluationTest, LinearDerivativeModesInterpolateSignedScaleAndRetainLength) {
    InspectBezierScale controller; Fill(controller);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 4, 8, 12);
    Vec(Key(controller, 0)->Out, 1, 2, 3); Vec(Key(controller, 1)->In, -1, -2, -3);
    EXPECT_FLOAT_EQ(12, controller.GetLength()); EXPECT_TRUE(controller.Computed());
}

TEST_F(BezierScaleEvaluationTest, SlowModesUseZeroDerivativesAndSmoothstep) {
    InspectBezierScale controller; Fill(controller, BEZIER_KEY_SLOW);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 3.25, 6.5, 9.75);
    Vec(Key(controller, 0)->Out, 0, 0, 0); Vec(Key(controller, 1)->In, 0, 0, 0);
}

TEST_F(BezierScaleEvaluationTest, FastModesUseTwiceTheSecantDerivative) {
    InspectBezierScale controller; Fill(controller, BEZIER_KEY_FAST);
    VxVector output; ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 4.75, 9.5, 14.25);
    Vec(Key(controller, 0)->Out, 2, 4, 6); Vec(Key(controller, 1)->In, -2, -4, -6);
}

TEST_F(BezierScaleEvaluationTest, ExplicitDerivativesUseDurationAndIncomingSignInTheCubic) {
    InspectBezierScale controller;
    CKBezierScaleKey a = ScaleKey(0, VxVector(0, 2, -1), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS,
                                  VxVector(8, 9, 10), VxVector(3, 0, -3));
    CKBezierScaleKey b = ScaleKey(4, VxVector(8, 10, 3), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS,
                                  VxVector(-6, 3, 0), VxVector(4, 5, 6));
    ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output));
    // Cubic Bernstein weights at 1/4 are 27,27,9,1 over64.
    // Handles are (4,2,-5) and (0,14,3), independently of Evaluate.
    Vec(output, 1.8125, 3.8125, -2.0625);
    EXPECT_EQ(0, std::memcmp(&a, controller.GetKey(0), sizeof(a)));
}

TEST_F(BezierScaleEvaluationTest, EitherStepSideHoldsThePreviousKeyUntilTheExactBoundary) {
    for (int side = 0; side < 2; ++side) {
        InspectBezierScale controller; Fill(controller);
        if (side == 0) Key(controller, 0)->Flags.SetOutTangentMode(BEZIER_KEY_STEP);
        else Key(controller, 1)->Flags.SetInTangentMode(BEZIER_KEY_STEP);
        VxVector output; ASSERT_TRUE(controller.Evaluate(7, &output)); Vec(output, 2, 4, 6);
        ASSERT_TRUE(controller.Evaluate(8, &output)); Vec(output, 10, 20, 30);
    }
}

TEST_F(BezierScaleEvaluationTest, StepDoesNotJumpEarlyWhenTheFloatNormalizedPhaseRoundsToOne) {
    InspectBezierScale controller;
    CKBezierScaleKey a = ScaleKey(-1.0e30f, VxVector(2, 4, 6), BEZIER_KEY_STEP, BEZIER_KEY_STEP);
    CKBezierScaleKey b = ScaleKey(2, VxVector(10, 20, 30), BEZIER_KEY_STEP, BEZIER_KEY_STEP);
    ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output)); Vec(output, 2, 4, 6);
    ASSERT_TRUE(controller.Evaluate(2, &output)); Vec(output, 10, 20, 30);
}

TEST_F(BezierScaleEvaluationTest, NativeCaptureFixturesCoverAllModesTangentsAndSerializedPayloads) {
    int samples = 0, fixtures = 0;
    for (const BezierReference::Fixture &fixture : BezierReference::Fixtures) {
        SCOPED_TRACE(fixture.name); InspectBezierScale controller; controller.SetLength(20);
        ASSERT_EQ(fixture.wordCount * 4, controller.ReadKeysFrom(const_cast<unsigned int *>(fixture.words)));
        for (int i = 0; i < fixture.sampleCount; ++i) {
            const BezierReference::Sample &sample = fixture.samples[i];
            VxVector output; ASSERT_TRUE(controller.Evaluate(sample.time, &output));
            Vec(output, sample.value[0], sample.value[1], sample.value[2], 0.00005);
            ++samples;
        }
        for (int i = 0; i < controller.GetKeyCount(); ++i) {
            Vec(Key(controller, i)->In, fixture.tangents[i][0], fixture.tangents[i][1], fixture.tangents[i][2], 0.00005);
            Vec(Key(controller, i)->Out, fixture.tangents[i][3], fixture.tangents[i][4], fixture.tangents[i][5], 0.00005);
        }
        CKDWORD wire[64]; ASSERT_LE(fixture.wordCount, 64);
        ASSERT_EQ(fixture.wordCount * 4, controller.DumpKeysTo(wire));
        EXPECT_EQ(0, std::memcmp(wire, fixture.saved, fixture.wordCount * 4)); ++fixtures;
    }
    EXPECT_EQ(43, fixtures); EXPECT_EQ(611, samples);
}

TEST_F(BezierScaleEvaluationTest, KeyDistanceIncludesTheAuthoredWrapAndRetainsFiniteCancellation) {
    InspectBezierScale controller; Fill(controller); controller.SetLength(20);
    EXPECT_FLOAT_EQ(8, controller.Distance(0, 1)); EXPECT_FLOAT_EQ(12, controller.Distance(1, 0));
    EXPECT_FLOAT_EQ(20, controller.Distance(0, 0)); EXPECT_FLOAT_EQ(0, controller.Distance(-1, 0));
    Key(controller, 0)->TimeStep = 2.0e38f; Key(controller, 1)->TimeStep = 3.0e38f; controller.SetLength(3.0e38f);
    EXPECT_FLOAT_EQ(2.0e38f, controller.Distance(1, 0));
}

TEST_F(BezierScaleEvaluationTest, WideFiniteLinearDifferencesKeepRepresentableUnitDerivatives) {
    InspectBezierScale controller;
    CKBezierScaleKey a = ScaleKey(-FLT_MAX, VxVector(-FLT_MAX, 2, 3));
    CKBezierScaleKey b = ScaleKey(FLT_MAX, VxVector(FLT_MAX, 2, 3));
    ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
    VxVector output; ASSERT_TRUE(controller.Evaluate(0, &output)); Vec(output, 0, 2, 3);
    Vec(Key(controller, 0)->Out, 1, 0, 0); Vec(Key(controller, 1)->In, -1, 0, 0);
}

TEST_F(BezierScaleEvaluationTest, SubnormalTimeAndValueSpansKeepRepresentableUnitDerivatives) {
    InspectBezierScale controller; const float tiny = std::ldexp(1.0f, -130);
    CKBezierScaleKey a = ScaleKey(0, VxVector(0, 0, 0));
    CKBezierScaleKey b = ScaleKey(tiny, VxVector(tiny, tiny, tiny));
    ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b)); controller.ComputeBezierPts();
    Vec(Key(controller, 0)->Out, 1, 1, 1); Vec(Key(controller, 1)->In, -1, -1, -1);
}

TEST_F(BezierScaleEvaluationTest, ExplicitHandlesMayOverflowFloatWhileTheActualCurveRemainsFinite) {
    InspectBezierScale controller;
    CKBezierScaleKey a = ScaleKey(0, VxVector(0, 2, 3), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS,
                                  VxVector(0, 0, 0), VxVector(1.0e30f, 0, 0));
    CKBezierScaleKey b = ScaleKey(1.0e30f, VxVector(0, 2, 3), BEZIER_KEY_TANGENTS, BEZIER_KEY_TANGENTS,
                                  VxVector(-1.0e30f, 0, 0), VxVector(0, 0, 0));
    ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
    VxVector output; ASSERT_TRUE(controller.Evaluate(5.0e29f, &output)); Vec(output, 0, 2, 3);
}

TEST_F(BezierScaleEvaluationTest, NearestEndpointRetainsTheSmallRepresentableScaleContribution) {
    InspectBezierScale controller;
    CKBezierScaleKey a = ScaleKey(-1.0e30f, VxVector(1.0e30f, 2.0e30f, -1.0e30f));
    CKBezierScaleKey b = ScaleKey(2, VxVector(0, 0, 0));
    ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
    VxVector output; ASSERT_TRUE(controller.Evaluate(1, &output));
    // The linear curve leaves one unit of x and minus one unit of z.
    Vec(output, 1, 2, -1, 0.000002);
}

TEST_F(BezierScaleEvaluationTest, CompareUsesInclusiveTimeAndValueThresholdAndExactFlags) {
    InspectBezierScale a, b; Fill(a); Fill(b);
    CKBezierScaleKey *key = Key(b, 0); key->TimeStep += .125f; key->Pos += VxVector(.125f, -.125f, .125f);
    EXPECT_TRUE(a.Compare(&b, .125f)); EXPECT_FALSE(a.Compare(&b, .124f));
    key->Flags.SetOutTangentMode(BEZIER_KEY_FAST); EXPECT_FALSE(a.Compare(&b, 100));
}

TEST_F(BezierScaleEvaluationTest, CompareIgnoresComputedDerivativesButExplicitDerivativesAreExact) {
    InspectBezierScale a, b; Fill(a); Fill(b); a.SetLength(100); b.ComputeBezierPts();
    Key(b, 0)->In.Set(90, 80, 70); Key(b, 0)->Out.Set(60, 50, 40); EXPECT_TRUE(a.Compare(&b, 0));
    Key(a, 0)->Flags.SetInTangentMode(BEZIER_KEY_TANGENTS); Key(b, 0)->Flags.SetInTangentMode(BEZIER_KEY_TANGENTS);
    Key(a, 0)->In = Key(b, 0)->In; EXPECT_TRUE(a.Compare(&b, 0));
    Key(b, 0)->In.x += .125f; EXPECT_FALSE(a.Compare(&b, 100));
}

TEST_F(BezierScaleEvaluationTest, CompareRejectsNullDifferentCategoryAndDifferentCount) {
    InspectBezierScale a, b; Fill(a); Fill(b); RCKBezierPositionController position;
    EXPECT_FALSE(a.Compare(nullptr)); EXPECT_FALSE(a.Compare(&position));
    b.RemoveKey(1); EXPECT_FALSE(a.Compare(&b)); EXPECT_EQ(2, a.GetKeyCount());
}

class BezierScaleAnimationTest : public BezierScaleEvaluationTest {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        BezierScaleEvaluationTest::SetUp(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};

TEST_F(BezierScaleAnimationTest, PlaybackUsesTheNonlinearCurveAndKeepsTranslation) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    CKAnimController *controller = animation.CreateController(CKANIMATION_BEZIERSCL_CONTROL); ASSERT_NE(nullptr, controller);
    Fill(*controller, BEZIER_KEY_SLOW); const VxVector translation(5, 6, 7); entity.SetPosition(&translation, nullptr, FALSE);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(8);
    ASSERT_EQ(CK_OK, animation.SetStep(.25f)); const VxMatrix &matrix = entity.GetLocalMatrix();
    Vec(VxVector(matrix[0][0], matrix[1][1], matrix[2][2]), 3.25, 6.5, 9.75); Vec(matrix[3], 5, 6, 7);
    animation.Set3dEntity(nullptr);
}

TEST_F(BezierScaleAnimationTest, PlaybackRetainsTheSmallScaleNearTheFinalZeroKey) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    CKAnimController *controller = animation.CreateController(CKANIMATION_BEZIERSCL_CONTROL); ASSERT_NE(nullptr, controller);
    CKBezierScaleKey a = ScaleKey(-1.0e30f, VxVector(1.0e30f, 2.0e30f, -1.0e30f));
    CKBezierScaleKey b = ScaleKey(2, VxVector(0, 0, 0));
    ASSERT_EQ(0, controller->AddKey(&a)); ASSERT_EQ(1, controller->AddKey(&b));
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(2);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f)); const VxMatrix &matrix = entity.GetLocalMatrix();
    Vec(VxVector(matrix[0][0], matrix[1][1], matrix[2][2]), 1, 2, -1, 0.000002);
    animation.Set3dEntity(nullptr);
}

TEST_F(BezierScaleAnimationTest, PlaybackKeepsSignedAndCollapsedAxesAndHonorsIgnoreScale) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    CKAnimController *controller = animation.CreateController(CKANIMATION_BEZIERSCL_CONTROL); ASSERT_NE(nullptr, controller);
    CKBezierScaleKey a = ScaleKey(0, VxVector(-2, 0, 3)), b = ScaleKey(8, VxVector(-10, 0, 11));
    ASSERT_EQ(0, controller->AddKey(&a)); ASSERT_EQ(1, controller->AddKey(&b));
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(8);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f)); const VxMatrix before = entity.GetLocalMatrix();
    Vec(VxVector(before[0][0], before[1][1], before[2][2]), -6, 0, 7);
    animation.SetFlags(animation.GetFlags() | CK_OBJECTANIMATION_IGNORESCALE);
    ASSERT_EQ(CK_OK, animation.SetStep(.25f));
    EXPECT_EQ(0, std::memcmp(&before, &entity.GetLocalMatrix(), sizeof(before))); animation.Set3dEntity(nullptr);
}
} // namespace
