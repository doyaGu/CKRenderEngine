#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct Trace {
    int Count = 0;
    int Values[8] = {};
    CKRenderContext *Context = nullptr;
};
struct Call { Trace *Output; int Value; };
void Record(CKRenderContext *context, void *argument) {
    Call *call = static_cast<Call *>(argument);
    EXPECT_EQ(call->Output->Context, context);
    ASSERT_LT(call->Output->Count, 8);
    call->Output->Values[call->Output->Count++] = call->Value;
}
class RenderContextCallbackCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    Trace Output;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Output.Context = Device;
    }
    void TearDown() override {
        delete Device;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(RenderContextCallbackCompletionTest, PublicRegistrationSeparatesPrePostTransparentAndPostSpritePhases) {
    Call pre = {&Output, 10}, early = {&Output, 20}, late = {&Output, 30}, sprite = {&Output, 40};
    Device->AddPreRenderCallBack(Record, &pre);
    Device->AddPostRenderCallBack(Record, &early, FALSE, TRUE);
    Device->AddPostRenderCallBack(Record, &late, FALSE, FALSE);
    Device->AddPostSpriteRenderCallBack(Record, &sprite);
    Device->m_PreRenderCallBacks.ExecutePreCallbacks(Device);
    Device->m_PostRenderCallBacks.ExecutePostCallbacks(Device);
    Device->m_PreRenderCallBacks.ExecutePostCallbacks(Device);
    Device->m_PostSpriteRenderCallBacks.ExecutePostCallbacks(Device);
    ASSERT_EQ(4, Output.Count);
    const int expected[4] = {10, 20, 30, 40};
    for (int i = 0; i < 4; ++i) EXPECT_EQ(expected[i], Output.Values[i]);
}

TEST_F(RenderContextCallbackCompletionTest, RemovePostRemovesBothTransparentSlotsButLeavesPreAndSpriteCallbacks) {
    Call call = {&Output, 7};
    Device->AddPreRenderCallBack(Record, &call);
    Device->AddPostRenderCallBack(Record, &call, FALSE, TRUE);
    Device->AddPostRenderCallBack(Record, &call, FALSE, FALSE);
    Device->AddPostSpriteRenderCallBack(Record, &call);
    Device->RemovePostRenderCallBack(Record, &call);
    Device->m_PostRenderCallBacks.ExecutePostCallbacks(Device);
    Device->m_PreRenderCallBacks.ExecutePostCallbacks(Device);
    EXPECT_EQ(0, Output.Count);
    Device->m_PreRenderCallBacks.ExecutePreCallbacks(Device);
    Device->m_PostSpriteRenderCallBacks.ExecutePostCallbacks(Device);
    EXPECT_EQ(2, Output.Count);
    Device->RemovePreRenderCallBack(Record, &call);
    Device->RemovePostSpriteRenderCallBack(Record, &call);
    Device->m_PreRenderCallBacks.ExecutePreCallbacks(Device);
    Device->m_PostSpriteRenderCallBacks.ExecutePostCallbacks(Device);
    EXPECT_EQ(2, Output.Count);
}

TEST_F(RenderContextCallbackCompletionTest, ClearCallbacksRemovesTemporaryRegistrationsBeforeOwnerDestruction) {
    Call call = {&Output, 7};
    Device->AddPreRenderCallBack(Record, &call, TRUE);
    Device->AddPostRenderCallBack(Record, &call, TRUE, TRUE);
    Device->AddPostRenderCallBack(Record, &call, TRUE, FALSE);
    Device->AddPostSpriteRenderCallBack(Record, &call, TRUE);
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(3, Manager->m_TemporaryPostRenderCallbacks.Size());
    Device->ClearCallbacks();
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    EXPECT_EQ(0, Device->m_PreRenderCallBacks.m_PreCallBacks.Size());
    EXPECT_EQ(0, Device->m_PreRenderCallBacks.m_PostCallBacks.Size());
    EXPECT_EQ(0, Device->m_PostRenderCallBacks.m_PostCallBacks.Size());
    EXPECT_EQ(0, Device->m_PostSpriteRenderCallBacks.m_PostCallBacks.Size());
    Manager->RemoveAllTemporaryCallbacks();
    EXPECT_EQ(0, Output.Count);
}
} // namespace
