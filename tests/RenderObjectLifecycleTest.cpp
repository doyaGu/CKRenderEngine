#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class LifecycleRenderProbe : public RCKRenderObject {
public:
    static CKCallbacksContainer *Callbacks(RCKRenderObject &object) {
        return object.*(&LifecycleRenderProbe::m_Callbacks);
    }

    static void RecordLayout(RCKRenderObject &object) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&object);
        auto maskMember = &LifecycleRenderProbe::m_InRenderContext;
        auto callbackMember = &LifecycleRenderProbe::m_Callbacks;
        const std::uintptr_t maskAddress = reinterpret_cast<std::uintptr_t>(&(object.*maskMember));
        const std::uintptr_t callbackAddress = reinterpret_cast<std::uintptr_t>(&(object.*callbackMember));
        EXPECT_GE(maskAddress - begin, sizeof(CKBeObject));
        EXPECT_GE(callbackAddress - begin, maskAddress - begin + sizeof(CKDWORD));
        EXPECT_EQ(0u, maskAddress % alignof(CKDWORD));
        EXPECT_EQ(0u, callbackAddress % alignof(CKCallbacksContainer *));
        EXPECT_LE(callbackAddress - begin + sizeof(CKCallbacksContainer *), sizeof(RCKRenderObject));
        ::testing::Test::RecordProperty("m_InRenderContext_offset", static_cast<int>(maskAddress - begin));
        ::testing::Test::RecordProperty("m_InRenderContext_size", static_cast<int>(sizeof(CKDWORD)));
        ::testing::Test::RecordProperty("m_Callbacks_offset", static_cast<int>(callbackAddress - begin));
        ::testing::Test::RecordProperty("m_Callbacks_size", static_cast<int>(sizeof(CKCallbacksContainer *)));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("SDLRenderObjectSize", static_cast<int>(sizeof(RCKRenderObject)));
        ::testing::Test::RecordProperty("SDLRenderObjectAlignment", static_cast<int>(alignof(RCKRenderObject)));
        ::testing::Test::RecordProperty("SDLBeObjectSize", static_cast<int>(sizeof(CKBeObject)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
    }
};

CKBOOL CountCallback(CKRenderContext *, CKRenderObject *, void *argument) {
    ++*static_cast<int *>(argument);
    return TRUE;
}

class RenderObjectLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCKRenderObject *Object() {
        return static_cast<RCKRenderObject *>(Context->CreateObject(CKCID_RENDEROBJECT));
    }

    static void DestroyOnly(CKContext &context, CKObject *object) {
        CKDependencies dependencies;
        dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, context.DestroyObject(object, 0, &dependencies));
    }
};

TEST_F(RenderObjectLifecycleTest, NamedConstructorCopiesNameRegistersIdentityAndInitializesOwnedState) {
    CK_ID id = 0;
    {
        char name[] = "Direct render object";
        RCKRenderObject object(Context, name);
        id = object.GetID();
        ASSERT_NE(0u, id);
        EXPECT_EQ(&object, Context->GetObject(id));
        EXPECT_STREQ("Direct render object", object.GetName());
        name[0] = 'X';
        EXPECT_STREQ("Direct render object", object.GetName());
        EXPECT_EQ(CKCID_RENDEROBJECT, object.GetClassID());
        EXPECT_EQ(0u, object.GetInRenderContextMask());
        EXPECT_EQ(nullptr, LifecycleRenderProbe::Callbacks(object));
        EXPECT_EQ(nullptr, Context->GetRenderManager());
    }
    EXPECT_EQ(nullptr, Context->GetObject(id));
}

TEST_F(RenderObjectLifecycleTest, RegisteredFactoryDispatchesSdkIdentityClassNameAndBaseHierarchy) {
    const int before = Context->GetObjectsCountByClassID(CKCID_RENDEROBJECT);
    CKObject *base = Context->CreateObject(CKCID_RENDEROBJECT, "Registered render object");
    ASSERT_NE(nullptr, base);
    CKRenderObject *render = CKRenderObject::Cast(base);
    ASSERT_NE(nullptr, render);
    EXPECT_EQ(base, Context->GetObject(base->GetID()));
    EXPECT_EQ(before + 1, Context->GetObjectsCountByClassID(CKCID_RENDEROBJECT));
    EXPECT_EQ(47, base->GetClassID());
    EXPECT_TRUE(CKIsChildClassOf(base, CKCID_BEOBJECT));
    EXPECT_STREQ("Registered render object", base->GetName());
    EXPECT_STREQ("Render Object", RCKRenderObject::GetClassName());
    CKClassDesc *description = CKGetClassDesc(CKCID_RENDEROBJECT);
    ASSERT_NE(nullptr, description);
    EXPECT_EQ(CKCID_BEOBJECT, description->Parent);
    EXPECT_STREQ("Render Object", description->NameFct());
    EXPECT_EQ(0u, static_cast<RCKRenderObject *>(base)->GetInRenderContextMask());
}

TEST_F(RenderObjectLifecycleTest, DirectFactoriesDestroyOwnedCallbacksThroughVirtualBaseWithoutRenderManager) {
    CKRenderObject *first = RCKRenderObject::CreateInstance(Context);
    CKRenderObject *second = RCKRenderObject::CreateInstance(Context);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    const CK_ID firstID = first->GetID(), secondID = second->GetID();
    EXPECT_NE(firstID, secondID);
    EXPECT_EQ(first, Context->GetObject(firstID));
    EXPECT_EQ(second, Context->GetObject(secondID));
    EXPECT_EQ(nullptr, first->GetName());
    ASSERT_EQ(nullptr, Context->GetRenderManager());
    int argument = 7;
    ASSERT_TRUE(first->SetRenderCallBack(CountCallback, &argument));
    ASSERT_NE(nullptr, LifecycleRenderProbe::Callbacks(*static_cast<RCKRenderObject *>(first)));
    // The direct factory registers identity, while the context factory separately
    // finalizes class lists. Its owned container is still destroyed virtually.
    delete static_cast<CKObject *>(first);
    EXPECT_EQ(nullptr, Context->GetObject(firstID));
    EXPECT_EQ(second, Context->GetObject(secondID));
    EXPECT_EQ(7, argument);
    delete static_cast<CKObject *>(second);
    EXPECT_EQ(nullptr, Context->GetObject(secondID));
}

TEST_F(RenderObjectLifecycleTest, RegisterPublishesNativeParameterAndPreservesOtherClassOptions) {
    CKClassDesc *description = CKGetClassDesc(CKCID_RENDEROBJECT);
    ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID();
    description->ToBeNotify.Clear();
    description->ToBeNotify.Set(CKCID_BEOBJECT);
    description->DefaultCopyDependencies = 0x55;
    RCKRenderObject::Register();
    EXPECT_EQ(CKPGUID_RENDEROBJECT, description->Parameter);
    EXPECT_EQ(0x65706358u, description->Parameter.d1);
    EXPECT_EQ(0x63627B50u, description->Parameter.d2);
    EXPECT_EQ(0x55u, description->DefaultCopyDependencies);
    for (int classID = 0; classID <= CKCID_RENDEROBJECT; ++classID) {
        EXPECT_EQ(classID == CKCID_BEOBJECT, description->ToBeNotify.IsSet(classID) != 0);
    }
}

TEST_F(RenderObjectLifecycleTest, DependencyCallbacksAdvertiseNoOwnOptionsForAllOperationModes) {
    CKClassDesc *description = CKGetClassDesc(CKCID_RENDEROBJECT);
    ASSERT_NE(nullptr, description);
    const int modes[] = {CK_DEPENDENCIES_COPY, CK_DEPENDENCIES_DELETE, CK_DEPENDENCIES_REPLACE,
                         CK_DEPENDENCIES_SAVE, CK_DEPENDENCIES_BUILD, 0, -1, 99};
    for (int mode : modes) {
        EXPECT_EQ(0, RCKRenderObject::GetDependenciesCount(mode));
        EXPECT_EQ(0, description->DependsCountFct(mode));
        for (int index = -1; index < 3; ++index) {
            EXPECT_EQ(nullptr, RCKRenderObject::GetDependencies(index, mode));
            EXPECT_EQ(nullptr, description->DependsFct(index, mode));
        }
    }
}

TEST_F(RenderObjectLifecycleTest, MemoryReportsNativeInlineContractWithRealCallbackOwnershipAndBorrowedArguments) {
    new RCKRenderManager(Context); // CKContext owns the registered manager.
    RCKRenderObject *object = Object();
    ASSERT_NE(nullptr, object);
    const int ownBytes = static_cast<int>(sizeof(RCKRenderObject) - sizeof(CKBeObject));
    EXPECT_EQ(ownBytes, object->GetMemoryOccupation() - object->CKBeObject::GetMemoryOccupation());
    int argument = 5;
    ASSERT_TRUE(object->AddPreRenderCallBack(CountCallback, &argument, FALSE));
    ASSERT_TRUE(object->SetRenderCallBack(CountCallback, &argument));
    ASSERT_TRUE(object->AddPostRenderCallBack(CountCallback, &argument, FALSE));
    CKCallbacksContainer *callbacks = LifecycleRenderProbe::Callbacks(*object);
    ASSERT_NE(nullptr, callbacks);
    EXPECT_EQ(1, callbacks->m_PreCallBacks.Size());
    EXPECT_EQ(1, callbacks->m_PostCallBacks.Size());
    ASSERT_NE(nullptr, callbacks->m_Callback);
    // Native GetMemoryOccupation adds the inline 8 bytes and does not enumerate
    // callback allocations. The SDL equivalent uses actual architecture sizes.
    EXPECT_EQ(ownBytes, object->GetMemoryOccupation() - object->CKBeObject::GetMemoryOccupation());
    object->RemoveAllCallbacks();
    EXPECT_EQ(nullptr, LifecycleRenderProbe::Callbacks(*object));
    EXPECT_EQ(ownBytes, object->GetMemoryOccupation() - object->CKBeObject::GetMemoryOccupation());
    EXPECT_EQ(5, argument);
}

TEST_F(RenderObjectLifecycleTest, ActualArchitectureFieldLayoutPreservesSdkBaseAndOwnsOnlyMaskAndContainerPointer) {
    RCKRenderObject *object = Object();
    ASSERT_NE(nullptr, object);
    CKObject *base = object;
    CKBeObject *behavior = object;
    CKRenderObject *render = object;
    EXPECT_EQ(static_cast<void *>(object), static_cast<void *>(base));
    EXPECT_EQ(static_cast<void *>(object), static_cast<void *>(behavior));
    EXPECT_EQ(static_cast<void *>(object), static_cast<void *>(render));
    EXPECT_EQ(CKCID_RENDEROBJECT, base->GetClassID());
    EXPECT_EQ(CKCID_RENDEROBJECT, RCKRenderObject::m_ClassID);
    EXPECT_EQ(0u, object->GetInRenderContextMask());
    EXPECT_EQ(nullptr, LifecycleRenderProbe::Callbacks(*object));
    EXPECT_EQ(sizeof(CKBeObject), sizeof(CKRenderObject));
    LifecycleRenderProbe::RecordLayout(*object);
}

TEST_F(RenderObjectLifecycleTest, RegisteredDestructionUnlinksOnlyItsOwnedTemporaryCallbacksAndKeepsOtherIdentity) {
    RCKRenderManager *manager = new RCKRenderManager(Context);
    ASSERT_EQ(manager, Context->GetRenderManager());
    RCKRenderObject *first = Object(), *second = Object();
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    int firstArgument = 3, secondArgument = 9;
    ASSERT_TRUE(first->AddPreRenderCallBack(CountCallback, &firstArgument, TRUE));
    ASSERT_TRUE(first->AddPostRenderCallBack(CountCallback, &firstArgument, TRUE));
    ASSERT_TRUE(first->SetRenderCallBack(CountCallback, &firstArgument));
    ASSERT_TRUE(second->AddPreRenderCallBack(CountCallback, &secondArgument, TRUE));
    ASSERT_TRUE(second->AddPostRenderCallBack(CountCallback, &secondArgument, TRUE));
    CKCallbacksContainer *secondCallbacks = LifecycleRenderProbe::Callbacks(*second);
    ASSERT_NE(nullptr, secondCallbacks);
    ASSERT_EQ(2, manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(2, manager->m_TemporaryPostRenderCallbacks.Size());
    const CK_ID firstID = first->GetID(), secondID = second->GetID();
    DestroyOnly(*Context, first);
    EXPECT_EQ(nullptr, Context->GetObject(firstID));
    ASSERT_EQ(second, Context->GetObject(secondID));
    ASSERT_EQ(1, manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(1, manager->m_TemporaryPostRenderCallbacks.Size());
    EXPECT_EQ(secondCallbacks, manager->m_TemporaryPreRenderCallbacks[0].callback);
    EXPECT_EQ(secondCallbacks, manager->m_TemporaryPostRenderCallbacks[0].callback);
    manager->RemoveAllTemporaryCallbacks();
    EXPECT_EQ(0, manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, manager->m_TemporaryPostRenderCallbacks.Size());
    EXPECT_EQ(0, secondCallbacks->m_PreCallBacks.Size());
    EXPECT_EQ(0, secondCallbacks->m_PostCallBacks.Size());
    ASSERT_TRUE(second->SetRenderCallBack(CountCallback, &secondArgument));
    DestroyOnly(*Context, second);
    EXPECT_EQ(nullptr, Context->GetObject(secondID));
    EXPECT_EQ(3, firstArgument);
    EXPECT_EQ(9, secondArgument);
}

} // namespace
