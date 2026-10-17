#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKRenderEngineTypes.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct RegistrationLog {
    int Count = 0;
    int Functions[32] = {};
    void *Arguments[32] = {};
    CKRenderContext *Devices[32] = {};
    CKRenderObject *Objects[32] = {};
    void Reset() { Count = 0; }
    void Add(int function, void *argument, CKRenderContext *device, CKRenderObject *object = nullptr) {
        EXPECT_LT(Count, 32);
        if (Count < 32) {
            Functions[Count] = function; Arguments[Count] = argument;
            Devices[Count] = device; Objects[Count++] = object;
        }
    }
};
RegistrationLog *ActiveLog = nullptr;
void FirstSimple(CKRenderContext *device, void *argument) { ActiveLog->Add(1, argument, device); }
void SecondSimple(CKRenderContext *device, void *argument) { ActiveLog->Add(2, argument, device); }
CKBOOL ObjectCallback(CKRenderContext *device, CKRenderObject *object, void *argument) {
    ActiveLog->Add(3, argument, device, object); return FALSE;
}

class RegistrationAccess : public RCKRenderObject {
public:
    static CKCallbacksContainer *Callbacks(RCKRenderObject &object) {
        return object.*&RegistrationAccess::m_Callbacks;
    }
};

class CallbacksRegistrationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RegistrationLog Log;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); ActiveLog = &Log;
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        ActiveLog = nullptr; CKShutdown();
    }
    static void *First() { return reinterpret_cast<void *>(FirstSimple); }
    static void *Second() { return reinterpret_cast<void *>(SecondSimple); }
    void Expect(int index, int function, void *argument) {
        ASSERT_LT(index, Log.Count); EXPECT_EQ(function, Log.Functions[index]);
        EXPECT_EQ(argument, Log.Arguments[index]);
    }
    void RenderObjectCallbacks(RCK3dEntity &entity, RCKRenderContext &device) {
        // The callback consumer is real. Frustum/GPU submission is deliberately
        // outside this registration test; use its already-visible clear path.
        entity.m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE;
        ASSERT_EQ(nullptr, entity.GetCurrentMesh());
        ASSERT_EQ(nullptr, device.m_RasterizerContext);
        ASSERT_TRUE(entity.Render(&device, CK_RENDER_CLEARVIEWPORT));
    }
};

TEST_F(CallbacksRegistrationTest, PreIdentityIsTheFunctionArgumentPairAndNullArgumentRemainsCallable) {
    CKCallbacksContainer callbacks; int first = 1, second = 2;
    EXPECT_FALSE(callbacks.AddPreCallback(nullptr, &first, -7, Manager));
    EXPECT_FALSE(callbacks.RemovePreCallback(nullptr, &first));
    EXPECT_EQ(0, callbacks.m_PreCallBacks.Size()); EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &first, FALSE, Manager));
    EXPECT_FALSE(callbacks.AddPreCallback(First(), &first, TRUE, Manager));
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &second, FALSE, Manager));
    ASSERT_TRUE(callbacks.AddPreCallback(Second(), &first, FALSE, Manager));
    ASSERT_TRUE(callbacks.AddPreCallback(First(), nullptr, FALSE, nullptr));
    EXPECT_FALSE(callbacks.RemovePreCallback(Second(), &second));
    callbacks.ExecutePreCallbacks(nullptr); ASSERT_EQ(4, Log.Count);
    Expect(0, 1, &first); Expect(1, 1, &second); Expect(2, 2, &first); Expect(3, 1, nullptr);
    ASSERT_TRUE(callbacks.RemovePreCallback(First(), &first));
    EXPECT_FALSE(callbacks.RemovePreCallback(First(), &first)); Log.Reset();
    callbacks.ExecutePreCallbacks(nullptr); ASSERT_EQ(3, Log.Count);
    Expect(0, 1, &second); Expect(1, 2, &first); Expect(2, 1, nullptr);
    ASSERT_TRUE(callbacks.RemovePreCallback(First(), nullptr));
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
}

TEST_F(CallbacksRegistrationTest, PostIdentityAndRemovalAreIndependentOfTheSamePrePair) {
    CKCallbacksContainer callbacks; int first = 1, second = 2;
    EXPECT_FALSE(callbacks.AddPostCallback(nullptr, &first, 9, Manager));
    EXPECT_FALSE(callbacks.RemovePostCallback(nullptr, &first));
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &first, FALSE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(First(), &first, FALSE, Manager));
    EXPECT_FALSE(callbacks.AddPostCallback(First(), &first, TRUE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(First(), &second, FALSE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(Second(), &first, FALSE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(First(), nullptr, FALSE, Manager));
    EXPECT_FALSE(callbacks.RemovePostCallback(Second(), &second));
    ASSERT_TRUE(callbacks.RemovePostCallback(First(), &first));
    EXPECT_FALSE(callbacks.RemovePostCallback(First(), &first));
    callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr);
    ASSERT_EQ(4, Log.Count); Expect(0, 1, &first); Expect(1, 1, &second);
    Expect(2, 2, &first); Expect(3, 1, nullptr);
    ASSERT_EQ(1, callbacks.m_PreCallBacks.Size()); EXPECT_EQ(&first, callbacks.m_PreCallBacks[0].argument);
    ASSERT_TRUE(callbacks.RemovePostCallback(First(), nullptr));
}

TEST_F(CallbacksRegistrationTest, RawNonzeroTemporaryFlagsArePreservedAndDuplicateAttemptsNeverDoubleEnroll) {
    CKCallbacksContainer callbacks; int pre = 11, post = 22;
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &pre, -7, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(Second(), &post, 0x40000000, Manager));
    ASSERT_EQ(1, callbacks.m_PreCallBacks.Size()); ASSERT_EQ(1, callbacks.m_PostCallBacks.Size());
    EXPECT_EQ(-7, callbacks.m_PreCallBacks[0].temp); EXPECT_EQ(0x40000000, callbacks.m_PostCallBacks[0].temp);
    EXPECT_FALSE(callbacks.AddPreCallback(First(), &pre, FALSE, Manager));
    EXPECT_FALSE(callbacks.AddPreCallback(First(), &pre, 2, Manager));
    EXPECT_FALSE(callbacks.AddPostCallback(Second(), &post, FALSE, Manager));
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    EXPECT_EQ(&callbacks, Manager->m_TemporaryPreRenderCallbacks[0].callback);
    EXPECT_EQ(First(), Manager->m_TemporaryPreRenderCallbacks[0].argument);
    EXPECT_EQ(&pre, Manager->m_TemporaryPreRenderCallbacks[0].arg2);
    EXPECT_EQ(&callbacks, Manager->m_TemporaryPostRenderCallbacks[0].callback);
    EXPECT_EQ(Second(), Manager->m_TemporaryPostRenderCallbacks[0].argument);
    EXPECT_EQ(&post, Manager->m_TemporaryPostRenderCallbacks[0].arg2);
    callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr);
    ASSERT_EQ(2, Log.Count); Expect(0, 1, &pre); Expect(1, 2, &post);
    ASSERT_EQ(CK_OK, Manager->PreProcess()); Log.Reset();
    callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr);
    EXPECT_EQ(0, Log.Count); EXPECT_EQ(0, callbacks.m_PreCallBacks.Size()); EXPECT_EQ(0, callbacks.m_PostCallBacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size()); EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
}

TEST_F(CallbacksRegistrationTest, AttemptingToPromoteExistingPermanentPairsDoesNotEnrollOrReclassifyThem) {
    CKCallbacksContainer callbacks; int pre = 11, post = 22;
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &pre, FALSE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(Second(), &post, FALSE, Manager));
    EXPECT_FALSE(callbacks.AddPreCallback(First(), &pre, -9, Manager));
    EXPECT_FALSE(callbacks.AddPostCallback(Second(), &post, 2, Manager));
    EXPECT_EQ(FALSE, callbacks.m_PreCallBacks[0].temp); EXPECT_EQ(FALSE, callbacks.m_PostCallBacks[0].temp);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size()); EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    ASSERT_EQ(CK_OK, Manager->PreProcess());
    callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr);
    ASSERT_EQ(2, Log.Count); Expect(0, 1, &pre); Expect(1, 2, &post);
}

TEST_F(CallbacksRegistrationTest, ExplicitRemovalIsLocalAndDeferredManagerCleanupKeepsUnrelatedPairs) {
    CKCallbacksContainer callbacks; int temporary = 1, permanent = 2;
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &temporary, TRUE, Manager));
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &permanent, FALSE, Manager));
    ASSERT_TRUE(callbacks.AddPostCallback(First(), &temporary, TRUE, Manager));
    EXPECT_FALSE(callbacks.RemovePreCallback(Second(), &temporary));
    ASSERT_TRUE(callbacks.RemovePreCallback(First(), &temporary));
    EXPECT_FALSE(callbacks.RemovePreCallback(First(), &temporary));
    EXPECT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr);
    ASSERT_EQ(2, Log.Count); Expect(0, 1, &permanent); Expect(1, 1, &temporary);
    ASSERT_EQ(CK_OK, Manager->PreProcess()); Log.Reset();
    callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr);
    ASSERT_EQ(1, Log.Count); Expect(0, 1, &permanent);
    EXPECT_FALSE(callbacks.RemovePostCallback(First(), &temporary));
}

TEST_F(CallbacksRegistrationTest, NoManagerKeepsLocalRegistrationFlagsWithoutInventingDeferredOwnership) {
    CKCallbacksContainer callbacks; int pre = 1, post = 2;
    ASSERT_TRUE(callbacks.AddPreCallback(First(), &pre, -5, nullptr));
    ASSERT_TRUE(callbacks.AddPostCallback(Second(), &post, 2, nullptr));
    EXPECT_EQ(-5, callbacks.m_PreCallBacks[0].temp); EXPECT_EQ(2, callbacks.m_PostCallBacks[0].temp);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size()); EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr);
    ASSERT_EQ(2, Log.Count); Expect(0, 1, &pre); Expect(1, 2, &post);
    ASSERT_TRUE(callbacks.RemovePreCallback(First(), &pre)); ASSERT_TRUE(callbacks.RemovePostCallback(Second(), &post));
    Log.Reset(); callbacks.ExecutePreCallbacks(nullptr); callbacks.ExecutePostCallbacks(nullptr); EXPECT_EQ(0, Log.Count);
}

TEST_F(CallbacksRegistrationTest, PublicRemoveThenPermanentReregistrationSurvivesTheOldDeferredCancellation) {
    RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    RCKRenderContext *device = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, device); int pre = 11, post = 22;
    ASSERT_TRUE(entity->AddPreRenderCallBack(ObjectCallback, &pre, -7));
    ASSERT_TRUE(entity->AddPostRenderCallBack(ObjectCallback, &post, 2));
    ASSERT_TRUE(entity->RemovePreRenderCallBack(ObjectCallback, &pre));
    ASSERT_TRUE(entity->RemovePostRenderCallBack(ObjectCallback, &post));
    ASSERT_TRUE(entity->AddPreRenderCallBack(ObjectCallback, &pre, FALSE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(ObjectCallback, &post, FALSE));
    CKCallbacksContainer *callbacks = RegistrationAccess::Callbacks(*entity); ASSERT_NE(nullptr, callbacks);
    ASSERT_EQ(1, callbacks->m_PreCallBacks.Size()); ASSERT_EQ(1, callbacks->m_PostCallBacks.Size());
    EXPECT_EQ(FALSE, callbacks->m_PreCallBacks[0].temp); EXPECT_EQ(FALSE, callbacks->m_PostCallBacks[0].temp);
    RenderObjectCallbacks(*entity, *device); ASSERT_EQ(2, Log.Count);
    Expect(0, 3, &pre); Expect(1, 3, &post); Log.Reset();
    ASSERT_EQ(CK_OK, Manager->PreProcess());
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size()); EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    RenderObjectCallbacks(*entity, *device); ASSERT_EQ(2, Log.Count);
    Expect(0, 3, &pre); Expect(1, 3, &post);
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(reinterpret_cast<CKRenderContext *>(device), Log.Devices[i]);
        EXPECT_EQ(reinterpret_cast<CKRenderObject *>(entity), Log.Objects[i]);
    }
    ASSERT_EQ(1, callbacks->m_PreCallBacks.Size()); ASSERT_EQ(1, callbacks->m_PostCallBacks.Size());
    EXPECT_EQ(FALSE, callbacks->m_PreCallBacks[0].temp); EXPECT_EQ(FALSE, callbacks->m_PostCallBacks[0].temp);
}

TEST_F(CallbacksRegistrationTest, CleanupIdentityIncludesTheContainerAndPhaseEvenForTheSameFunctionAndArgument) {
    CKCallbacksContainer temporaryOwner, permanentOwner; int shared = 91;
    ASSERT_TRUE(temporaryOwner.AddPreCallback(First(), &shared, TRUE, Manager));
    ASSERT_TRUE(temporaryOwner.AddPostCallback(First(), &shared, 2, Manager));
    ASSERT_TRUE(permanentOwner.AddPreCallback(First(), &shared, FALSE, Manager));
    ASSERT_TRUE(permanentOwner.AddPostCallback(First(), &shared, FALSE, Manager));
    ASSERT_EQ(CK_OK, Manager->PreProcess());
    temporaryOwner.ExecutePreCallbacks(nullptr); temporaryOwner.ExecutePostCallbacks(nullptr);
    EXPECT_EQ(0, Log.Count);
    permanentOwner.ExecutePreCallbacks(nullptr); permanentOwner.ExecutePostCallbacks(nullptr);
    ASSERT_EQ(2, Log.Count); Expect(0, 1, &shared); Expect(1, 1, &shared);
    EXPECT_FALSE(temporaryOwner.RemovePreCallback(First(), &shared));
    EXPECT_FALSE(temporaryOwner.RemovePostCallback(First(), &shared));
    EXPECT_TRUE(permanentOwner.RemovePreCallback(First(), &shared));
    EXPECT_TRUE(permanentOwner.RemovePostCallback(First(), &shared));
}
}
