#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class AnimationClockBoundaryTest : public ::testing::Test {
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
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};
}

TEST_F(AnimationClockBoundaryTest, ExactWholeFrameMotionCancelsToZeroForEitherSignedClockDirection) {
    RCKAnimation animation(Context);
    animation.SetLength(16);
    const float frames[] = {2, -2, 2, -2};
    const float rates[] = {-1024, 1024, 1024, -1024};
    const float milliseconds[] = {1.953125f, 1.953125f, -1.953125f, -1.953125f};
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(i);
        animation.SetFrame(frames[i]);
        animation.LinkToFrameRate(TRUE, rates[i]);
        const float step = animation.GetStep();
        const CKDWORD flags = animation.GetFlags();
        // 1.953125 is exactly 125/64. At 1024 fps this is exactly two frames.
        // A tiny negative error would incorrectly trigger a strict reverse handover.
        EXPECT_EQ(0.0f, animation.GetNextFrame(milliseconds[i]));
        EXPECT_EQ(0.0f, animation.GetNextFrame(milliseconds[i]));
        EXPECT_EQ(step, animation.GetStep());
        EXPECT_EQ(frames[i], animation.GetFrame());
        EXPECT_EQ(rates[i], animation.GetLinkedFrameRate());
        EXPECT_EQ(flags, animation.GetFlags());
    }
}

TEST_F(AnimationClockBoundaryTest, AdjacentFloatDurationsStayOnTheirRespectiveSidesOfTheExactZeroBoundary) {
    RCKAnimation animation(Context);
    animation.SetLength(16);
    const float boundary = 1.953125f;
    const float shorter = std::nextafter(boundary, 0.0f);
    const float longer = std::nextafter(boundary, 2.0f);
    // Adjacent float times differ by 2^-23 ms here. Multiplying that duration by
    // 1024 frames/second gives exactly 1/8192000 frame, independently of the clock code.
    const float remainder = static_cast<float>(1.0 / 8192000.0);
    ASSERT_EQ(1.0f / 8388608.0f, boundary - shorter);
    ASSERT_EQ(1.0f / 8388608.0f, longer - boundary);
    const float signs[] = {1, -1};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(i);
        const float sign = signs[i];
        animation.SetFrame(sign * 2);
        animation.LinkToFrameRate(TRUE, sign * -1024);
        const float step = animation.GetStep();
        const CKDWORD flags = animation.GetFlags();
        const float before = animation.GetNextFrame(shorter);
        const float exact = animation.GetNextFrame(boundary);
        const float after = animation.GetNextFrame(longer);
        EXPECT_EQ(sign * remainder, before);
        EXPECT_EQ(0.0f, exact);
        EXPECT_EQ(-sign * remainder, after);
        EXPECT_GT(sign * before, 0);
        EXPECT_LT(sign * after, 0);
        EXPECT_EQ(step, animation.GetStep());
        EXPECT_EQ(sign * 2, animation.GetFrame());
        EXPECT_EQ(sign * -1024, animation.GetLinkedFrameRate());
        EXPECT_EQ(flags, animation.GetFlags());
    }
}
