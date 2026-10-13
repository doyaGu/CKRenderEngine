#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class CallbackAccess : public RCKRenderObject {
public:
    static CKCallbacksContainer *Container(RCKRenderObject &object) {
        return object.*&CallbackAccess::m_Callbacks;
    }
};

struct CallbackTrace {
    int Count = 0;
    int Values[16] = {};
    CKRenderContext *Devices[16] = {};
    CKRenderObject *Objects[16] = {};
};

struct CallbackArgument {
    CallbackTrace *Trace;
    int Value;
};

CKBOOL RecordCallback(CKRenderContext *device, CKRenderObject *object, void *argument) {
    CallbackArgument *entry = static_cast<CallbackArgument *>(argument);
    EXPECT_NE(nullptr, entry);
    if (!entry || entry->Trace->Count >= 16) return FALSE;
    const int index = entry->Trace->Count++;
    entry->Trace->Values[index] = entry->Value;
    entry->Trace->Devices[index] = device;
    entry->Trace->Objects[index] = object;
    return FALSE; // The consumer runs all callbacks independently of this return value.
}

CKBOOL AlternateCallback(CKRenderContext *device, CKRenderObject *object, void *argument) {
    return RecordCallback(device, object, argument);
}

class RenderObjectCallbacksTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        // Direct construction registers the device identity, but does not add
        // it to the class list. The manager and factory entities belong to CKContext.
        delete Device;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    void PrepareConsumer() {
        Manager = new RCKRenderManager(Context);
        ASSERT_EQ(Manager, Context->GetRenderManager());
        Device = new RCKRenderContext(Context);
        ASSERT_EQ(nullptr, Device->m_RasterizerContext);
    }

    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }

    void RenderCallbacks(RCK3dEntity &entity) {
        // Real object consumer, with no mesh, GPU context, frustum work, inverse
        // winding or debug rendering. This tests all three SDK callback arguments.
        entity.m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE;
        ASSERT_EQ(nullptr, entity.GetCurrentMesh());
        EXPECT_TRUE(entity.Render(Device, CK_RENDER_CLEARVIEWPORT));
    }

    void ExpectTrace(const CallbackTrace &trace, RCK3dEntity *entity,
                     const int *values, int count) {
        ASSERT_EQ(count, trace.Count);
        for (int i = 0; i < count; ++i) {
            EXPECT_EQ(values[i], trace.Values[i]);
            EXPECT_EQ(Device, trace.Devices[i]);
            EXPECT_EQ(entity, trace.Objects[i]);
        }
    }
};

TEST_F(RenderObjectCallbacksTest, EmptyRemovalDoesNotAllocateAndRepeatedRemoveAllIsSafe) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    EXPECT_FALSE(entity->RemovePreRenderCallBack(RecordCallback, nullptr));
    EXPECT_FALSE(entity->RemovePostRenderCallBack(RecordCallback, nullptr));
    EXPECT_FALSE(entity->RemoveRenderCallBack());
    entity->RemoveAllCallbacks();
    entity->RemoveAllCallbacks();
    EXPECT_EQ(nullptr, CallbackAccess::Container(*entity));
}

TEST_F(RenderObjectCallbacksTest, PreCallbacksUseFunctionAndArgumentIdentityAndKeepInsertionOrder) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackTrace trace;
    CallbackArgument first = {&trace, 11}, second = {&trace, 12};
    EXPECT_FALSE(entity->AddPreRenderCallBack(nullptr, &first, FALSE));
    ASSERT_NE(nullptr, CallbackAccess::Container(*entity));
    EXPECT_EQ(0, CallbackAccess::Container(*entity)->m_PreCallBacks.Size());
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &first, FALSE));
    EXPECT_FALSE(entity->AddPreRenderCallBack(RecordCallback, &first, TRUE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &second, FALSE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(AlternateCallback, &first, FALSE));
    EXPECT_FALSE(entity->RemovePreRenderCallBack(nullptr, &first));
    EXPECT_FALSE(entity->RemovePreRenderCallBack(AlternateCallback, &second));
    RenderCallbacks(*entity);
    const int all[] = {11, 12, 11};
    ExpectTrace(trace, entity, all, 3);
    ASSERT_TRUE(entity->RemovePreRenderCallBack(RecordCallback, &first));
    EXPECT_FALSE(entity->RemovePreRenderCallBack(RecordCallback, &first));
    trace.Count = 0;
    RenderCallbacks(*entity);
    const int remaining[] = {12, 11};
    ExpectTrace(trace, entity, remaining, 2);
}

TEST_F(RenderObjectCallbacksTest, PostCallbacksUseFunctionAndArgumentIdentityWithoutChangingPreCallbacks) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackTrace trace;
    CallbackArgument first = {&trace, 81}, second = {&trace, 82};
    EXPECT_FALSE(entity->AddPostRenderCallBack(nullptr, &first, FALSE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &first, FALSE));
    EXPECT_FALSE(entity->AddPostRenderCallBack(RecordCallback, &first, TRUE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &second, FALSE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &first, FALSE));
    EXPECT_FALSE(entity->RemovePostRenderCallBack(nullptr, &first));
    EXPECT_FALSE(entity->RemovePostRenderCallBack(AlternateCallback, &first));
    ASSERT_TRUE(entity->RemovePostRenderCallBack(RecordCallback, &first));
    EXPECT_FALSE(entity->RemovePostRenderCallBack(RecordCallback, &first));
    RenderCallbacks(*entity);
    const int remaining[] = {81, 82};
    ExpectTrace(trace, entity, remaining, 2);
}

TEST_F(RenderObjectCallbacksTest, RealObjectConsumerPassesDeviceObjectAndArgumentsInPreRenderPostOrder) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackTrace trace;
    CallbackArgument pre1 = {&trace, 21}, pre2 = {&trace, 22}, render = {&trace, 50};
    CallbackArgument post1 = {&trace, 81}, post2 = {&trace, 82};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &pre1, FALSE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &pre2, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordCallback, &render));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &post1, FALSE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &post2, FALSE));
    RenderCallbacks(*entity);
    const int order[] = {21, 22, 50, 81, 82};
    ExpectTrace(trace, entity, order, 5);
}

TEST_F(RenderObjectCallbacksTest, SetRenderReplacesTheOwnedSlotAndBorrowedArgumentWithoutAppending) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackTrace trace;
    CallbackArgument old = {&trace, 31}, replacement = {&trace, 32};
    ASSERT_TRUE(entity->SetRenderCallBack(RecordCallback, &old));
    VxCallBack *slot = CallbackAccess::Container(*entity)->m_Callback;
    ASSERT_NE(nullptr, slot);
    ASSERT_TRUE(entity->SetRenderCallBack(AlternateCallback, &replacement));
    EXPECT_EQ(slot, CallbackAccess::Container(*entity)->m_Callback);
    EXPECT_EQ(&replacement, slot->argument);
    EXPECT_EQ(FALSE, slot->temp);
    RenderCallbacks(*entity);
    const int order[] = {32};
    ExpectTrace(trace, entity, order, 1);
    EXPECT_EQ(31, old.Value); // Arguments stay caller-owned.
}

TEST_F(RenderObjectCallbacksTest, RemovingRenderSlotPreservesPrePostAndReturnsTrueForAllocatedContainer) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    EXPECT_FALSE(entity->RemoveRenderCallBack());
    CallbackTrace trace;
    CallbackArgument pre = {&trace, 1}, render = {&trace, 2}, post = {&trace, 3};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &pre, FALSE));
    EXPECT_TRUE(entity->RemoveRenderCallBack());
    ASSERT_TRUE(entity->SetRenderCallBack(RecordCallback, &render));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &post, FALSE));
    ASSERT_TRUE(entity->RemoveRenderCallBack());
    EXPECT_TRUE(entity->RemoveRenderCallBack());
    EXPECT_EQ(nullptr, CallbackAccess::Container(*entity)->m_Callback);
    RenderCallbacks(*entity);
    const int order[] = {1, 3};
    ExpectTrace(trace, entity, order, 2);
}

TEST_F(RenderObjectCallbacksTest, NullRenderFunctionIsRejectedAndKeepsTheCallableExistingSlot) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    EXPECT_FALSE(entity->SetRenderCallBack(nullptr, nullptr));
    ASSERT_NE(nullptr, CallbackAccess::Container(*entity));
    EXPECT_EQ(nullptr, CallbackAccess::Container(*entity)->m_Callback);
    CallbackTrace trace;
    CallbackArgument valid = {&trace, 7}, invalid = {&trace, 8};
    ASSERT_TRUE(entity->SetRenderCallBack(RecordCallback, &valid));
    EXPECT_FALSE(entity->SetRenderCallBack(nullptr, &invalid));
    RenderCallbacks(*entity);
    const int order[] = {7};
    ExpectTrace(trace, entity, order, 1);
}

TEST_F(RenderObjectCallbacksTest, ManagerEndOfFrameRemovesOnlyTemporaryPreAndPostCallbacks) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackTrace trace;
    CallbackArgument permanent = {&trace, 1}, temporary = {&trace, 2};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &permanent, FALSE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &temporary, TRUE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &permanent, FALSE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &temporary, TRUE));
    EXPECT_FALSE(entity->AddPreRenderCallBack(RecordCallback, &temporary, FALSE));
    EXPECT_FALSE(entity->AddPostRenderCallBack(RecordCallback, &temporary, FALSE));
    EXPECT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    RenderCallbacks(*entity);
    const int all[] = {1, 2, 1, 2};
    ExpectTrace(trace, entity, all, 4);
    Manager->RemoveAllTemporaryCallbacks();
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    trace.Count = 0;
    RenderCallbacks(*entity);
    const int remaining[] = {1, 1};
    ExpectTrace(trace, entity, remaining, 2);
    Manager->RemoveAllTemporaryCallbacks();
}

TEST_F(RenderObjectCallbacksTest, ExplicitTemporaryRemovalLeavesSafeDeferredManagerCleanup) {
    PrepareConsumer();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackTrace trace;
    CallbackArgument temporary = {&trace, 1}, permanent = {&trace, 2};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &temporary, TRUE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &temporary, TRUE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &permanent, FALSE));
    ASSERT_TRUE(entity->RemovePreRenderCallBack(RecordCallback, &temporary));
    ASSERT_TRUE(entity->RemovePostRenderCallBack(RecordCallback, &temporary));
    // Native removal is local to the container; frame cleanup retries the
    // deleted pair, which safely returns false, and then clears its queue.
    EXPECT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    Manager->RemoveAllTemporaryCallbacks();
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    RenderCallbacks(*entity);
    const int order[] = {2};
    ExpectTrace(trace, entity, order, 1);
}

TEST_F(RenderObjectCallbacksTest, RemoveAllUnregistersOnlyThisContainerAndCanRecreateItsCallbacks) {
    PrepareConsumer();
    RCK3dEntity *first = Entity(), *second = Entity();
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    CallbackTrace trace;
    CallbackArgument firstArgument = {&trace, 1}, secondArgument = {&trace, 2};
    ASSERT_TRUE(first->AddPreRenderCallBack(RecordCallback, &firstArgument, TRUE));
    ASSERT_TRUE(first->SetRenderCallBack(RecordCallback, &firstArgument));
    ASSERT_TRUE(first->AddPostRenderCallBack(RecordCallback, &firstArgument, TRUE));
    ASSERT_TRUE(second->AddPreRenderCallBack(RecordCallback, &secondArgument, TRUE));
    ASSERT_TRUE(second->AddPostRenderCallBack(RecordCallback, &secondArgument, TRUE));
    first->RemoveAllCallbacks();
    first->RemoveAllCallbacks();
    EXPECT_EQ(nullptr, CallbackAccess::Container(*first));
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    EXPECT_EQ(CallbackAccess::Container(*second), Manager->m_TemporaryPreRenderCallbacks[0].callback);
    EXPECT_EQ(CallbackAccess::Container(*second), Manager->m_TemporaryPostRenderCallbacks[0].callback);
    RenderCallbacks(*second);
    const int secondOrder[] = {2, 2};
    ExpectTrace(trace, second, secondOrder, 2);
    Manager->RemoveAllTemporaryCallbacks();
    ASSERT_TRUE(first->SetRenderCallBack(RecordCallback, &firstArgument));
    trace.Count = 0;
    RenderCallbacks(*first);
    const int firstOrder[] = {1};
    ExpectTrace(trace, first, firstOrder, 1);
}

TEST_F(RenderObjectCallbacksTest, HeadlessPermanentCallbacksCanBeClearedAndRecreatedWithoutRenderManager) {
    EXPECT_EQ(nullptr, Context->GetRenderManager());
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackTrace trace;
    CallbackArgument argument = {&trace, 7};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordCallback, &argument, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordCallback, &argument));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordCallback, &argument, FALSE));
    entity->RemoveAllCallbacks();
    EXPECT_EQ(nullptr, CallbackAccess::Container(*entity));
    entity->RemoveAllCallbacks();
    ASSERT_TRUE(entity->SetRenderCallBack(RecordCallback, &argument));
    entity->RemoveAllCallbacks();
    EXPECT_EQ(nullptr, CallbackAccess::Container(*entity));
    EXPECT_EQ(0, trace.Count); // Clear and destruction never invoke the user function.
    EXPECT_EQ(7, argument.Value);
}

} // namespace
