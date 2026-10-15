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

} // namespace
