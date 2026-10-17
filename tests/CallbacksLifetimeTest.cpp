#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class CallbackOwnerAccess : public RCKRenderObject {
public:
    static CKCallbacksContainer *Container(RCKRenderObject &object) {
        return object.*(&CallbackOwnerAccess::m_Callbacks);
    }
};

struct CallbackLifetimeTrace {
    int Count = 0;
    int Values[32] = {};
    CKRenderContext *Devices[32] = {};
    CKRenderObject *Owners[32] = {};
};

struct BorrowedCallbackArgument {
    CallbackLifetimeTrace *Trace;
    int Value;
    int *Destructions;

    ~BorrowedCallbackArgument() { ++*Destructions; }
};

CKBOOL RecordObject(CKRenderContext *device, CKRenderObject *object, void *argument) {
    BorrowedCallbackArgument *entry = static_cast<BorrowedCallbackArgument *>(argument);
    const int index = entry->Trace->Count++;
    EXPECT_LT(index, 32);
    if (index < 32) {
        entry->Trace->Values[index] = entry->Value;
        entry->Trace->Devices[index] = device;
        entry->Trace->Owners[index] = object;
    }
    return FALSE;
}

CKBOOL RecordOther(CKRenderContext *device, CKRenderObject *object, void *argument) {
    return RecordObject(device, object, argument);
}

class CallbacksLifetimeTest : public ::testing::Test {
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
        Manager = new RCKRenderManager(Context);
        ASSERT_EQ(Manager, Context->GetRenderManager());
        Device = new RCKRenderContext(Context);
        ASSERT_EQ(nullptr, Device->m_RasterizerContext);
    }

    void TearDown() override {
        delete Device;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }

    void Render(RCK3dEntity &entity) {
        ASSERT_EQ(nullptr, entity.GetCurrentMesh());
        entity.m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE;
        EXPECT_TRUE(entity.Render(Device, CK_RENDER_CLEARVIEWPORT));
    }

    void ExpectTrace(const CallbackLifetimeTrace &trace, RCK3dEntity *owner,
                     const int *values, int count) {
        ASSERT_EQ(count, trace.Count);
        for (int i = 0; i < count; ++i) {
            EXPECT_EQ(values[i], trace.Values[i]);
            EXPECT_EQ(Device, trace.Devices[i]);
            EXPECT_EQ(owner, trace.Owners[i]);
        }
    }
};

TEST_F(CallbacksLifetimeTest, ConstructionAndRepeatedEmptyCleanupLeaveAReusableContainer) {
    CKCallbacksContainer callbacks;
    EXPECT_EQ(nullptr, callbacks.m_Callback);
    EXPECT_EQ(nullptr, callbacks.m_PreCallBacks.Begin());
    EXPECT_EQ(nullptr, callbacks.m_PostCallBacks.Begin());
    EXPECT_EQ(0, callbacks.m_PreCallBacks.Size());
    EXPECT_EQ(0, callbacks.m_PostCallBacks.Size());
    EXPECT_TRUE(callbacks.RemoveCallBack());
    callbacks.ClearPreCallbacks();
    callbacks.ClearPostCallbacks();
    callbacks.Clear();
    callbacks.Clear();

    CallbackLifetimeTrace trace;
    int destructions = 0;
    BorrowedCallbackArgument argument = {&trace, 7, &destructions};
    ASSERT_TRUE(callbacks.SetCallBack(reinterpret_cast<void *>(RecordObject), &argument));
    EXPECT_EQ(reinterpret_cast<void *>(RecordObject), callbacks.m_Callback->callback);
    EXPECT_EQ(&argument, callbacks.m_Callback->argument);
    EXPECT_EQ(FALSE, callbacks.m_Callback->temp);
    callbacks.Clear();
    EXPECT_EQ(nullptr, callbacks.m_Callback);
    EXPECT_EQ(0, trace.Count);
    EXPECT_EQ(0, destructions);
    EXPECT_EQ(7, argument.Value);
}

TEST_F(CallbacksLifetimeTest, MainSlotReplacementAndNullRejectionReachTheRealThreeArgumentConsumer) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackLifetimeTrace trace;
    int destructions = 0;
    BorrowedCallbackArgument first = {&trace, 11, &destructions};
    BorrowedCallbackArgument second = {&trace, 22, &destructions};
    EXPECT_FALSE(entity->SetRenderCallBack(nullptr, &first));
    CKCallbacksContainer *callbacks = CallbackOwnerAccess::Container(*entity);
    ASSERT_NE(nullptr, callbacks);
    EXPECT_EQ(nullptr, callbacks->m_Callback);
    ASSERT_TRUE(entity->SetRenderCallBack(RecordObject, &first));
    VxCallBack *slot = callbacks->m_Callback;
    ASSERT_NE(nullptr, slot);
    slot->temp = TRUE;
    ASSERT_TRUE(entity->SetRenderCallBack(RecordOther, &second));
    EXPECT_EQ(slot, callbacks->m_Callback);
    EXPECT_EQ(FALSE, slot->temp);
    EXPECT_FALSE(entity->SetRenderCallBack(nullptr, &first));
    EXPECT_EQ(reinterpret_cast<void *>(RecordOther), slot->callback);
    EXPECT_EQ(&second, slot->argument);
    Render(*entity);
    const int order[] = {22};
    ExpectTrace(trace, entity, order, 1);
    EXPECT_EQ(0, destructions);
    entity->RemoveAllCallbacks();
}

TEST_F(CallbacksLifetimeTest, RemovingTheMainSlotPreservesCallablePreAndPostChannels) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackLifetimeTrace trace;
    int destructions = 0;
    BorrowedCallbackArgument pre = {&trace, 1, &destructions};
    BorrowedCallbackArgument main = {&trace, 2, &destructions};
    BorrowedCallbackArgument post = {&trace, 3, &destructions};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordObject, &pre, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordObject, &main));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordObject, &post, FALSE));
    CKCallbacksContainer *callbacks = CallbackOwnerAccess::Container(*entity);
    ASSERT_NE(nullptr, callbacks);
    ASSERT_TRUE(callbacks->RemoveCallBack());
    EXPECT_TRUE(callbacks->RemoveCallBack());
    EXPECT_EQ(nullptr, callbacks->m_Callback);
    Render(*entity);
    const int order[] = {1, 3};
    ExpectTrace(trace, entity, order, 2);
    EXPECT_EQ(0, destructions);
    entity->RemoveAllCallbacks();
}

TEST_F(CallbacksLifetimeTest, ClearingPreReleasesOnlyThatStorageAndPreservesMainAndPostDispatch) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackLifetimeTrace trace;
    int destructions = 0;
    BorrowedCallbackArgument pre = {&trace, 1, &destructions};
    BorrowedCallbackArgument main = {&trace, 2, &destructions};
    BorrowedCallbackArgument post = {&trace, 3, &destructions};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordObject, &pre, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordObject, &main));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordObject, &post, FALSE));
    CKCallbacksContainer *callbacks = CallbackOwnerAccess::Container(*entity);
    ASSERT_NE(nullptr, callbacks);
    VxCallBack *slot = callbacks->m_Callback;
    VxCallBack *postStorage = callbacks->m_PostCallBacks.Begin();
    callbacks->ClearPreCallbacks();
    callbacks->ClearPreCallbacks();
    EXPECT_EQ(nullptr, callbacks->m_PreCallBacks.Begin());
    EXPECT_EQ(0, callbacks->m_PreCallBacks.Size());
    EXPECT_EQ(slot, callbacks->m_Callback);
    EXPECT_EQ(postStorage, callbacks->m_PostCallBacks.Begin());
    Render(*entity);
    const int remaining[] = {2, 3};
    ExpectTrace(trace, entity, remaining, 2);
    trace.Count = 0;
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordOther, &pre, FALSE));
    Render(*entity);
    const int restored[] = {1, 2, 3};
    ExpectTrace(trace, entity, restored, 3);
    EXPECT_EQ(0, destructions);
    entity->RemoveAllCallbacks();
}

TEST_F(CallbacksLifetimeTest, ClearingPostReleasesOnlyThatStorageAndPreservesPreAndMainDispatch) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackLifetimeTrace trace;
    int destructions = 0;
    BorrowedCallbackArgument pre = {&trace, 1, &destructions};
    BorrowedCallbackArgument main = {&trace, 2, &destructions};
    BorrowedCallbackArgument post = {&trace, 3, &destructions};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordObject, &pre, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordObject, &main));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordObject, &post, FALSE));
    CKCallbacksContainer *callbacks = CallbackOwnerAccess::Container(*entity);
    ASSERT_NE(nullptr, callbacks);
    VxCallBack *slot = callbacks->m_Callback;
    VxCallBack *preStorage = callbacks->m_PreCallBacks.Begin();
    callbacks->ClearPostCallbacks();
    callbacks->ClearPostCallbacks();
    EXPECT_EQ(nullptr, callbacks->m_PostCallBacks.Begin());
    EXPECT_EQ(0, callbacks->m_PostCallBacks.Size());
    EXPECT_EQ(slot, callbacks->m_Callback);
    EXPECT_EQ(preStorage, callbacks->m_PreCallBacks.Begin());
    Render(*entity);
    const int remaining[] = {1, 2};
    ExpectTrace(trace, entity, remaining, 2);
    trace.Count = 0;
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordOther, &post, FALSE));
    Render(*entity);
    const int restored[] = {1, 2, 3};
    ExpectTrace(trace, entity, restored, 3);
    EXPECT_EQ(0, destructions);
    entity->RemoveAllCallbacks();
}

TEST_F(CallbacksLifetimeTest, FullClearDropsOwnedChannelsWithoutCallingBorrowedArgumentsAndCanRebind) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackLifetimeTrace trace;
    int destructions = 0;
    BorrowedCallbackArgument argument = {&trace, 5, &destructions};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordObject, &argument, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordObject, &argument));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordObject, &argument, FALSE));
    CKCallbacksContainer *callbacks = CallbackOwnerAccess::Container(*entity);
    ASSERT_NE(nullptr, callbacks);
    callbacks->Clear();
    callbacks->Clear();
    EXPECT_EQ(nullptr, callbacks->m_Callback);
    EXPECT_EQ(nullptr, callbacks->m_PreCallBacks.Begin());
    EXPECT_EQ(nullptr, callbacks->m_PostCallBacks.Begin());
    Render(*entity);
    EXPECT_EQ(0, trace.Count);
    EXPECT_EQ(0, destructions);
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordOther, &argument, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordOther, &argument));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordOther, &argument, FALSE));
    EXPECT_EQ(callbacks, CallbackOwnerAccess::Container(*entity));
    Render(*entity);
    const int order[] = {5, 5, 5};
    ExpectTrace(trace, entity, order, 3);
    entity->RemoveAllCallbacks();
    EXPECT_EQ(0, destructions);
}

TEST_F(CallbacksLifetimeTest, DestructionOfOwnedContainerNeverCallsOrDestroysItsBorrowedArguments) {
    CallbackLifetimeTrace trace;
    int destructions = 0;
    {
        BorrowedCallbackArgument argument = {&trace, 91, &destructions};
        CKCallbacksContainer *callbacks = new CKCallbacksContainer;
        ASSERT_TRUE(callbacks->AddPreCallback(reinterpret_cast<void *>(RecordObject), &argument, FALSE, nullptr));
        ASSERT_TRUE(callbacks->SetCallBack(reinterpret_cast<void *>(RecordObject), &argument));
        ASSERT_TRUE(callbacks->AddPostCallback(reinterpret_cast<void *>(RecordObject), &argument, FALSE, nullptr));
        delete callbacks;
        EXPECT_EQ(0, trace.Count);
        EXPECT_EQ(0, destructions);
        EXPECT_EQ(91, argument.Value);
    }
    EXPECT_EQ(1, destructions);
}

TEST_F(CallbacksLifetimeTest, TemporaryRegistryUnionRetainsThreeDistinctBorrowedPointersThroughCleanup) {
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, entity);
    CallbackLifetimeTrace trace;
    int destructions = 0;
    BorrowedCallbackArgument pre = {&trace, 1, &destructions};
    BorrowedCallbackArgument main = {&trace, 2, &destructions};
    BorrowedCallbackArgument post = {&trace, 3, &destructions};
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordObject, &pre, TRUE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordObject, &main));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordOther, &post, TRUE));
    CKCallbacksContainer *callbacks = CallbackOwnerAccess::Container(*entity);
    ASSERT_NE(nullptr, callbacks);
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    const VxCallBack &preRegistration = Manager->m_TemporaryPreRenderCallbacks[0];
    const VxCallBack &postRegistration = Manager->m_TemporaryPostRenderCallbacks[0];
    EXPECT_EQ(callbacks, preRegistration.callback);
    EXPECT_EQ(reinterpret_cast<void *>(RecordObject), preRegistration.argument);
    EXPECT_EQ(&pre, preRegistration.arg2);
    EXPECT_EQ(callbacks, postRegistration.callback);
    EXPECT_EQ(reinterpret_cast<void *>(RecordOther), postRegistration.argument);
    EXPECT_EQ(&post, postRegistration.arg2);
    EXPECT_NE(FALSE, callbacks->m_PreCallBacks[0].temp);
    EXPECT_NE(FALSE, callbacks->m_PostCallBacks[0].temp);
    Render(*entity);
    const int before[] = {1, 2, 3};
    ExpectTrace(trace, entity, before, 3);
    Manager->RemoveAllTemporaryCallbacks();
    EXPECT_EQ(0, callbacks->m_PreCallBacks.Size());
    EXPECT_EQ(0, callbacks->m_PostCallBacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    trace.Count = 0;
    Render(*entity);
    const int after[] = {2};
    ExpectTrace(trace, entity, after, 1);
    EXPECT_EQ(0, destructions);
    entity->RemoveAllCallbacks();
}

} // namespace
