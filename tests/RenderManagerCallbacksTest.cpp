#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
void Count(CKRenderContext *, void *argument) {
    ++*static_cast<int *>(argument);
}

class RenderManagerCallbacksTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(RenderManagerCallbacksTest, FrameCleanupRemovesOnlyTemporaryPreAndPostCallbacksAcrossContainers) {
    CKCallbacksContainer first, second;
    int temporaryPre = 0, permanentPre = 0, temporaryPost = 0, permanentPost = 0, other = 0;
    ASSERT_TRUE(first.AddPreCallback(reinterpret_cast<void *>(Count), &temporaryPre, TRUE, Manager));
    ASSERT_TRUE(first.AddPreCallback(reinterpret_cast<void *>(Count), &permanentPre, FALSE, Manager));
    ASSERT_TRUE(first.AddPostCallback(reinterpret_cast<void *>(Count), &temporaryPost, TRUE, Manager));
    ASSERT_TRUE(first.AddPostCallback(reinterpret_cast<void *>(Count), &permanentPost, FALSE, Manager));
    ASSERT_TRUE(second.AddPostCallback(reinterpret_cast<void *>(Count), &other, TRUE, Manager));
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(2, Manager->m_TemporaryPostRenderCallbacks.Size());
    EXPECT_EQ(CK_OK, Manager->PreProcess());
    first.ExecutePreCallbacks(nullptr);
    first.ExecutePostCallbacks(nullptr);
    second.ExecutePostCallbacks(nullptr);
    EXPECT_EQ(0, temporaryPre);
    EXPECT_EQ(0, temporaryPost);
    EXPECT_EQ(0, other);
    EXPECT_EQ(1, permanentPre);
    EXPECT_EQ(1, permanentPost);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
}

TEST_F(RenderManagerCallbacksTest, PauseRemovesTemporaryCallbacksWithoutCallingThem) {
    CKCallbacksContainer callbacks;
    int temporary = 0, permanent = 0;
    ASSERT_TRUE(callbacks.AddPreCallback(reinterpret_cast<void *>(Count), &temporary, TRUE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(reinterpret_cast<void *>(Count), &permanent, FALSE, Manager));
    EXPECT_EQ(CK_OK, Manager->OnCKPause());
    EXPECT_EQ(0, temporary);
    EXPECT_EQ(0, permanent);
    callbacks.ExecutePreCallbacks(nullptr);
    callbacks.ExecutePostCallbacks(nullptr);
    EXPECT_EQ(0, temporary);
    EXPECT_EQ(1, permanent);
}

TEST_F(RenderManagerCallbacksTest, ForgettingOneContainerRemovesAllItsRegistrationsAndLeavesOthersIntact) {
    CKCallbacksContainer first, second;
    int one = 0, two = 0, three = 0, surviving = 0;
    ASSERT_TRUE(first.AddPreCallback(reinterpret_cast<void *>(Count), &one, TRUE, Manager));
    ASSERT_TRUE(first.AddPreCallback(reinterpret_cast<void *>(Count), &two, TRUE, Manager));
    ASSERT_TRUE(first.AddPostCallback(reinterpret_cast<void *>(Count), &three, TRUE, Manager));
    ASSERT_TRUE(second.AddPreCallback(reinterpret_cast<void *>(Count), &surviving, TRUE, Manager));
    Manager->RemoveTemporaryCallback(&first);
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(&second, Manager->m_TemporaryPreRenderCallbacks[0].callback);
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    Manager->RemoveAllTemporaryCallbacks();
    first.ExecutePreCallbacks(nullptr);
    first.ExecutePostCallbacks(nullptr);
    second.ExecutePreCallbacks(nullptr);
    EXPECT_EQ(1, one);
    EXPECT_EQ(1, two);
    EXPECT_EQ(1, three);
    EXPECT_EQ(0, surviving);
}

TEST_F(RenderManagerCallbacksTest, ClearingRegistrationsDoesNotRemoveCallbacksFromTheirOwner) {
    CKCallbacksContainer callbacks;
    int pre = 0, post = 0;
    ASSERT_TRUE(callbacks.AddPreCallback(reinterpret_cast<void *>(Count), &pre, TRUE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(reinterpret_cast<void *>(Count), &post, TRUE, Manager));
    ASSERT_EQ(&callbacks, Manager->m_TemporaryPreRenderCallbacks[0].callback);
    EXPECT_EQ(reinterpret_cast<void *>(Count), Manager->m_TemporaryPreRenderCallbacks[0].argument);
    EXPECT_EQ(&pre, Manager->m_TemporaryPreRenderCallbacks[0].arg2);
    EXPECT_EQ(&post, Manager->m_TemporaryPostRenderCallbacks[0].arg2);
    Manager->ClearTemporaryCallbacks();
    Manager->RemoveAllTemporaryCallbacks();
    callbacks.ExecutePreCallbacks(nullptr);
    callbacks.ExecutePostCallbacks(nullptr);
    EXPECT_EQ(1, pre);
    EXPECT_EQ(1, post);
}
} // namespace
