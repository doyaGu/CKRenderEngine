#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class AnimationClockDispatchProbe : public RCKAnimation {
public:
    explicit AnimationClockDispatchProbe(CKContext *context)
        : RCKAnimation(context), StepCalls(0) {}

    void SetStep(float step) override {
        ++StepCalls;
        RCKAnimation::SetStep(step);
    }

    int StepCalls;
};

class AnimationClockTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(AnimationClockTest, DefaultClockUsesOneHundredFramesAndThirtyFramesPerSecond) {
    RCKAnimation animation(Context);
    CKAnimation *api = &animation;
    EXPECT_FLOAT_EQ(100.0f, api->GetLength());
    EXPECT_FLOAT_EQ(0.0f, api->GetStep());
    EXPECT_FLOAT_EQ(0.0f, api->GetFrame());
    EXPECT_EQ(TRUE, api->IsLinkedToFrameRate());
    EXPECT_FLOAT_EQ(30.0f, api->GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(15.0f, api->GetNextFrame(500.0f));
}

TEST_F(AnimationClockTest, LinkedDeltaUsesMillisecondsAndDoesNotAdvanceStoredCursor) {
    RCKAnimation animation(Context);
    animation.SetLength(80.0f);
    animation.SetStep(0.25f);
    animation.LinkToFrameRate(TRUE, 24.0f);
    EXPECT_FLOAT_EQ(26.0f, animation.GetNextFrame(250.0f));
    EXPECT_FLOAT_EQ(26.0f, animation.GetNextFrame(250.0f));
    EXPECT_FLOAT_EQ(20.0f, animation.GetNextFrame(0.0f));
    EXPECT_FLOAT_EQ(14.0f, animation.GetNextFrame(-250.0f));
    EXPECT_FLOAT_EQ(0.25f, animation.GetStep());
    EXPECT_FLOAT_EQ(20.0f, animation.GetFrame());
}

TEST_F(AnimationClockTest, PredictionPastEndDoesNotClampOrWrap) {
    RCKAnimation animation(Context);
    animation.SetLength(10.0f);
    animation.SetFrame(9.0f);
    animation.LinkToFrameRate(TRUE, 20.0f);
    EXPECT_FLOAT_EQ(11.0f, animation.GetNextFrame(100.0f));
    EXPECT_FLOAT_EQ(9.0f, animation.GetFrame());
    animation.LinkToFrameRate(FALSE, 20.0f);
    animation.SetFrame(10.0f);
    EXPECT_FLOAT_EQ(11.0f, animation.GetNextFrame(100.0f));
}

TEST_F(AnimationClockTest, UnlinkedPredictionAdvancesOneFrameRegardlessOfDeltaAndStoredRate) {
    RCKAnimation animation(Context);
    animation.SetLength(40.0f);
    animation.SetStep(0.5f);
    animation.LinkToFrameRate(FALSE, -120.0f);
    EXPECT_EQ(FALSE, animation.IsLinkedToFrameRate());
    EXPECT_FLOAT_EQ(-120.0f, animation.GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(21.0f, animation.GetNextFrame(0.0f));
    EXPECT_FLOAT_EQ(21.0f, animation.GetNextFrame(-1000.0f));
    EXPECT_FLOAT_EQ(21.0f, animation.GetNextFrame(100000.0f));
    EXPECT_FLOAT_EQ(0.5f, animation.GetStep());
}

TEST_F(AnimationClockTest, SignedFrameRateSupportsReversePlaybackAndZeroRateHoldsPosition) {
    RCKAnimation animation(Context);
    animation.SetLength(40.0f);
    animation.SetFrame(20.0f);
    animation.LinkToFrameRate(TRUE, -8.0f);
    EXPECT_FLOAT_EQ(-8.0f, animation.GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(18.0f, animation.GetNextFrame(250.0f));
    animation.LinkToFrameRate(TRUE, 0.0f);
    EXPECT_EQ(TRUE, animation.IsLinkedToFrameRate());
    EXPECT_FLOAT_EQ(20.0f, animation.GetNextFrame(1000.0f));
}

TEST_F(AnimationClockTest, LinkChangesOnlyLinkBitAndStoresRateWhenDisabled) {
    RCKAnimation animation(Context);
    const CKDWORD unrelated = CKANIMATION_CANBEBREAK | CKANIMATION_ALIGNORIENTATION |
        CKANIMATION_SUBANIMSSORTED;
    animation.SetFlags(unrelated);
    animation.LinkToFrameRate(-7, 12.0f);
    EXPECT_EQ(unrelated | CKANIMATION_LINKTOFRAMERATE, animation.GetFlags());
    EXPECT_EQ(TRUE, animation.IsLinkedToFrameRate());
    EXPECT_FLOAT_EQ(12.0f, animation.GetLinkedFrameRate());
    animation.LinkToFrameRate(FALSE, -3.0f);
    EXPECT_EQ(unrelated, animation.GetFlags());
    EXPECT_EQ(FALSE, animation.IsLinkedToFrameRate());
    EXPECT_FLOAT_EQ(-3.0f, animation.GetLinkedFrameRate());
}

TEST_F(AnimationClockTest, BaseFrameSetterNormalizesWithoutClampingOrVirtualStepDispatch) {
    AnimationClockDispatchProbe animation(Context);
    CKAnimation *api = &animation;
    api->SetLength(40.0f);
    api->SetFrame(10.0f);
    EXPECT_FLOAT_EQ(0.25f, api->GetStep());
    EXPECT_FLOAT_EQ(10.0f, api->GetFrame());
    api->SetFrame(-10.0f);
    EXPECT_FLOAT_EQ(-0.25f, api->GetStep());
    EXPECT_FLOAT_EQ(-10.0f, api->GetFrame());
    api->SetFrame(60.0f);
    EXPECT_FLOAT_EQ(1.5f, api->GetStep());
    EXPECT_FLOAT_EQ(60.0f, api->GetFrame());
    EXPECT_EQ(0, animation.StepCalls);
}

TEST_F(AnimationClockTest, BaseStepSetterStoresUnclampedCursor) {
    RCKAnimation animation(Context);
    CKAnimation *api = &animation;
    api->SetLength(16.0f);
    api->SetStep(0.375f);
    EXPECT_FLOAT_EQ(0.375f, api->GetStep());
    EXPECT_FLOAT_EQ(6.0f, api->GetFrame());
    api->SetStep(-0.5f);
    EXPECT_FLOAT_EQ(-0.5f, api->GetStep());
    EXPECT_FLOAT_EQ(-8.0f, api->GetFrame());
    api->SetStep(1.25f);
    EXPECT_FLOAT_EQ(1.25f, api->GetStep());
    EXPECT_FLOAT_EQ(20.0f, api->GetFrame());
}

TEST_F(AnimationClockTest, CurrentStepIsStoreOnlyAndDoesNotDispatchPlayback) {
    AnimationClockDispatchProbe animation(Context);
    CKAnimation *api = &animation;
    api->SetLength(32.0f);
    api->SetCurrentStep(0.25f);
    EXPECT_FLOAT_EQ(0.25f, api->GetStep());
    EXPECT_FLOAT_EQ(8.0f, api->GetFrame());
    api->SetCurrentStep(-0.5f);
    EXPECT_FLOAT_EQ(-16.0f, api->GetFrame());
    api->SetCurrentStep(1.5f);
    EXPECT_FLOAT_EQ(48.0f, api->GetFrame());
    EXPECT_EQ(0, animation.StepCalls);
}

TEST_F(AnimationClockTest, LengthChangePreservesNormalizedCursorAndFrameRate) {
    RCKAnimation animation(Context);
    animation.SetLength(40.0f);
    animation.SetStep(0.25f);
    animation.LinkToFrameRate(TRUE, 12.0f);
    animation.SetLength(80.0f);
    EXPECT_FLOAT_EQ(80.0f, animation.GetLength());
    EXPECT_FLOAT_EQ(0.25f, animation.GetStep());
    EXPECT_FLOAT_EQ(20.0f, animation.GetFrame());
    EXPECT_FLOAT_EQ(23.0f, animation.GetNextFrame(250.0f));
    EXPECT_FLOAT_EQ(12.0f, animation.GetLinkedFrameRate());
}

TEST_F(AnimationClockTest, ZeroDurationFrameSetterSelectsInitialCursor) {
    RCKAnimation animation(Context);
    animation.SetLength(0.0f);
    animation.SetCurrentStep(0.5f);
    animation.SetFrame(0.0f);
    EXPECT_FLOAT_EQ(0.0f, animation.GetStep());
    EXPECT_FLOAT_EQ(0.0f, animation.GetFrame());
    animation.SetFrame(7.0f);
    EXPECT_FLOAT_EQ(0.0f, animation.GetStep());
    EXPECT_FLOAT_EQ(0.0f, animation.GetFrame());
    animation.SetFrame(-7.0f);
    EXPECT_FLOAT_EQ(0.0f, animation.GetStep());
    EXPECT_FLOAT_EQ(0.0f, animation.GetFrame());
    animation.SetLength(20.0f);
    EXPECT_FLOAT_EQ(0.0f, animation.GetFrame());
}

TEST_F(AnimationClockTest, ZeroDurationRetainsStoredStepAndAllowsIndependentClockPrediction) {
    RCKAnimation animation(Context);
    animation.SetLength(0.0f);
    animation.SetStep(0.75f);
    animation.LinkToFrameRate(TRUE, 8.0f);
    EXPECT_FLOAT_EQ(0.75f, animation.GetStep());
    EXPECT_FLOAT_EQ(0.0f, animation.GetFrame());
    EXPECT_FLOAT_EQ(2.0f, animation.GetNextFrame(250.0f));
    EXPECT_FLOAT_EQ(0.75f, animation.GetStep());
    animation.LinkToFrameRate(FALSE, 8.0f);
    EXPECT_FLOAT_EQ(1.0f, animation.GetNextFrame(0.0f));
}

} // namespace
