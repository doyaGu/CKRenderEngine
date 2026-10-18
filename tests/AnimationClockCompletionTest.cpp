#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include "CKAll.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class AnimationClockCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    CKAnimation *Clock() {
        return CKAnimation::Cast(Context->CreateObject(CKCID_ANIMATION));
    }

    CKKeyedAnimation *Curve(CK3dEntity *entity, float length) {
        CKKeyedAnimation *group = CKKeyedAnimation::Cast(Context->CreateObject(CKCID_KEYEDANIMATION));
        CKObjectAnimation *channel = CKObjectAnimation::Cast(Context->CreateObject(CKCID_OBJECTANIMATION));
        if (!group || !channel)
            return nullptr;
        channel->Set3dEntity(entity);
        VxVector first(0, 10, -20), last(1000, 10, -20);
        channel->AddPositionKey(0, &first);
        channel->AddPositionKey(length, &last);
        channel->SetLength(length);
        if (group->AddAnimation(channel) != CK_OK)
            return nullptr;
        group->SetLength(length);
        return group;
    }

    static void ExpectPosition(CK3dEntity *entity, float x) {
        VxVector position;
        entity->GetPosition(&position);
        EXPECT_NEAR(x, position.x, 0.0002f);
        EXPECT_FLOAT_EQ(10, position.y);
        EXPECT_FLOAT_EQ(-20, position.z);
    }
};

TEST_F(AnimationClockCompletionTest, LinkedPredictionRetainsTheNativeRoundedCurrentFrameBeforeTheAdvance) {
    CKAnimation *clock = Clock();
    ASSERT_NE(nullptr, clock);
    clock->SetLength(10);
    clock->SetCurrentStep(.6f);
    clock->LinkToFrameRate(TRUE, 1000);
    // Float(.6)*10 rounds to exactly6 in the native current-frame spill.
    // Retaining its extra bits until the final prediction instead produces
    // 7.000000477f, which changes exact animation-end/loop decisions.
    ASSERT_EQ(6.0f, clock->GetFrame());
    EXPECT_EQ(7.0f, clock->GetNextFrame(1));
    EXPECT_EQ(.6f, clock->GetStep());
}

TEST_F(AnimationClockCompletionTest, LinkedProductKeepsRepresentableFramesBeforeMillisecondConversion) {
    CKAnimation *clock = Clock();
    ASSERT_NE(nullptr, clock);
    const float rate = std::ldexp(1.0f, 120);
    clock->SetLength(std::ldexp(1.0f, 122));
    clock->SetCurrentStep(0);
    clock->SetFlags(CKANIMATION_CANBEBREAK | 0x80000000u);
    // Milliseconds mean 1/1000 second: 1024 * 2^120 / 1000 = 2^127/125.
    // The scaled prediction is finite although rate*1024 exceeds FLT_MAX.
    const float magnitude = static_cast<float>(std::ldexp(1.0, 127) / 125.0);
    for (int reverse = 0; reverse < 2; ++reverse) {
        const float fps = reverse ? -rate : rate;
        clock->LinkToFrameRate(TRUE, fps);
        EXPECT_FLOAT_EQ(reverse ? -magnitude : magnitude, clock->GetNextFrame(1024));
        EXPECT_FLOAT_EQ(reverse ? magnitude : -magnitude, clock->GetNextFrame(-1024));
        EXPECT_FLOAT_EQ(0, clock->GetStep());
        EXPECT_FLOAT_EQ(fps, clock->GetLinkedFrameRate());
        EXPECT_EQ(CKANIMATION_CANBEBREAK | CKANIMATION_LINKTOFRAMERATE | 0x80000000u, clock->GetFlags());
    }
    // The unlinked contract ignores both huge finite inputs and advances one
    // frame. The linked arithmetic change must not alter this branch.
    clock->SetLength(32);
    clock->SetCurrentStep(.25f);
    clock->LinkToFrameRate(FALSE, FLT_MAX);
    EXPECT_FLOAT_EQ(9, clock->GetNextFrame(FLT_MAX));
    EXPECT_FLOAT_EQ(8, clock->GetFrame());
    EXPECT_FLOAT_EQ(.25f, clock->GetStep());
    EXPECT_EQ(CKANIMATION_CANBEBREAK | 0x80000000u, clock->GetFlags());
}

TEST_F(AnimationClockCompletionTest, LinkedAdvanceCanCancelTheCurrentFrameBeforeTheFinalFloatConversion) {
    CKAnimation *clock = Clock();
    ASSERT_NE(nullptr, clock);
    const float power = std::ldexp(1.0f, 127);
    clock->SetLength(power);
    // A 2048ms advance is (256/125)*2^127. The finite current frame is
    // -7*2^125, leaving the representable sum (149/125)*2^125.
    const float magnitude = static_cast<float>(std::ldexp(149.0, 125) / 125.0);
    for (int reverse = 0; reverse < 2; ++reverse) {
        const float step = reverse ? 1.75f : -1.75f;
        clock->SetCurrentStep(step);
        clock->LinkToFrameRate(TRUE, reverse ? -power : power);
        EXPECT_TRUE(std::isfinite(clock->GetFrame()));
        const float next = clock->GetNextFrame(2048);
        EXPECT_TRUE(std::isfinite(next));
        EXPECT_FLOAT_EQ(reverse ? -magnitude : magnitude, next);
        EXPECT_FLOAT_EQ(step, clock->GetStep());
        EXPECT_FLOAT_EQ(power, clock->GetLength());
    }
}

TEST_F(AnimationClockCompletionTest, FiniteCursorProductCanCancelIntoARepresentablePrediction) {
    CKAnimation *clock = Clock();
    ASSERT_NE(nullptr, clock);
    const float power = std::ldexp(1.0f, 127);
    clock->SetLength(power);
    // 2*2^127 is just beyond the float range. Subtracting the finite-input
    // advance (256/125)*2^127 leaves -(6/125)*2^127.
    const float magnitude = static_cast<float>(std::ldexp(6.0, 127) / 125.0);
    for (int reverse = 0; reverse < 2; ++reverse) {
        const float step = reverse ? -2.0f : 2.0f;
        clock->SetCurrentStep(step);
        clock->LinkToFrameRate(TRUE, reverse ? power : -power);
        const float next = clock->GetNextFrame(2048);
        EXPECT_TRUE(std::isfinite(next));
        EXPECT_FLOAT_EQ(reverse ? magnitude : -magnitude, next);
        EXPECT_FLOAT_EQ(step, clock->GetStep());
    }
}

TEST_F(AnimationClockCompletionTest, ForwardClockPredictionSelectsAnInteriorObjectAnimationPose) {
    CK3dEntity *entity = CK3dEntity::Cast(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, entity);
    CKKeyedAnimation *group = Curve(entity, std::ldexp(1.0f, 122));
    ASSERT_NE(nullptr, group);
    CKAnimation *clock = group;
    clock->SetCurrentStep(0);
    clock->LinkToFrameRate(TRUE, std::ldexp(1.0f, 120));
    const float next = clock->GetNextFrame(1024);
    EXPECT_TRUE(std::isfinite(next));
    EXPECT_FLOAT_EQ(0, clock->GetStep());
    clock->SetFrame(next);
    // (1024 * 2^120 / 1000) / 2^122 is the interior phase 32/125.
    EXPECT_FLOAT_EQ(static_cast<float>(32.0 / 125.0), clock->GetStep());
    ExpectPosition(entity, 256.0f);
    EXPECT_FLOAT_EQ(1, entity->GetLocalMatrix()[0][0]);
    EXPECT_FLOAT_EQ(1, entity->GetLocalMatrix()[1][1]);
    EXPECT_FLOAT_EQ(1, entity->GetLocalMatrix()[2][2]);
}

TEST_F(AnimationClockCompletionTest, SignedReverseClockCanReenterTheCurveAfterTheWideFrameProduct) {
    CK3dEntity *entity = CK3dEntity::Cast(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, entity);
    const float power = std::ldexp(1.0f, 127);
    CKKeyedAnimation *group = Curve(entity, power);
    ASSERT_NE(nullptr, group);
    CKAnimation *clock = group;
    // SetCurrentStep is intentionally store-only, as in the base contract.
    // The prediction can return to the playable interval before SetFrame
    // performs the derived keyed animation's normal playback clamping.
    clock->SetCurrentStep(2.25f);
    clock->LinkToFrameRate(TRUE, -power);
    const float next = clock->GetNextFrame(2048);
    EXPECT_TRUE(std::isfinite(next));
    EXPECT_FLOAT_EQ(static_cast<float>(std::ldexp(101.0, 125) / 125.0), next);
    EXPECT_FLOAT_EQ(2.25f, clock->GetStep());
    clock->SetFrame(next);
    // 9/4 - 2048/1000 = 101/500, which is inside the playable interval.
    EXPECT_FLOAT_EQ(static_cast<float>(101.0 / 500.0), clock->GetStep());
    ExpectPosition(entity, 202.0f);
    EXPECT_FLOAT_EQ(-power, clock->GetLinkedFrameRate());
    EXPECT_EQ(TRUE, clock->IsLinkedToFrameRate());
}

} // namespace
