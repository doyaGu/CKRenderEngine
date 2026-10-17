#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCK2dEntity.h"
#include "RCK3dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct CallbackDispatchTrace {
    int Count = 0;
    int Values[128] = {};
    CKRenderContext *Contexts[128] = {};
    CKRenderObject *Objects[128] = {};
    void *Arguments[128] = {};
    void Add(int value, CKRenderContext *context, CKRenderObject *object, void *argument) {
        EXPECT_LT(Count, 128);
        if (Count >= 128) return;
        Values[Count] = value; Contexts[Count] = context;
        Objects[Count] = object; Arguments[Count++] = argument;
    }
};
struct CallbackDispatchCall { CallbackDispatchTrace *Trace; int Value; };
void RecordContextCallback(CKRenderContext *context, void *argument) {
    CallbackDispatchCall *call = static_cast<CallbackDispatchCall *>(argument);
    call->Trace->Add(call->Value, context, nullptr, argument);
}
CKBOOL RecordEntityCallback(CKRenderContext *context, CKRenderObject *object, void *argument) {
    CallbackDispatchCall *call = static_cast<CallbackDispatchCall *>(argument);
    call->Trace->Add(call->Value, context, object, argument);
    return FALSE; // The real object consumer still executes subsequent callbacks.
}

// The engine's actual DrawScene, entity Render and callback dispatch run intact.
// These tests have no mesh and observe only the external frame phase boundary.
class CallbackPhaseBoundary : public MaterialRenderObserver {
public:
    CallbackDispatchTrace *Trace = nullptr;
    CKRenderContext *Device = nullptr;
    CKBOOL BeginScene() override { if (Trace) Trace->Add(10, Device, nullptr, nullptr); return TRUE; }
    CKBOOL EndScene() override { if (Trace) Trace->Add(80, Device, nullptr, nullptr); return TRUE; }
    CKBOOL BeginOverlayPhase() override { if (Trace) Trace->Add(90, Device, nullptr, nullptr); return TRUE; }
    void InitDefaultRenderStatesValue() override {}
    CKBOOL EnableLight(CKDWORD, CKBOOL) override { return TRUE; }
};

class CallbacksDispatchTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CallbackPhaseBoundary Boundary;
    CKRasterizerDriver Driver;
    CallbackDispatchTrace Trace;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 8;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 8;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary, 0));
        Device = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT)); ASSERT_NE(nullptr, Device);
        Device->DetachAll(); Device->m_RasterizerContext = &Boundary; Device->m_RasterizerDriver = &Driver;
        Boundary.Device = Device;
        Device->m_Settings.m_Rect = {0, 0, 100, 100};
        Device->m_ViewportData.ViewWidth = Device->m_ViewportData.ViewHeight = 100;
        Device->m_ViewportData.ViewZMin = 0; Device->m_ViewportData.ViewZMax = 1;
        Boundary.Viewport = Device->m_ViewportData;
        Device->m_Fov = PI / 2; Device->m_NearPlane = 1; Device->m_FarPlane = 100;
        VxMatrix projection; projection.Perspective(PI / 2, 1, 1, 100);
        Device->SetProjectionTransformationMatrix(projection); Device->SetViewTransformationMatrix(VxMatrix::Identity());
        Device->Activate(TRUE); Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
        Context->m_BehaviorContext.CurrentRenderContext = Device;
    }
    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCK3dEntity *Entity() {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        if (!entity) return nullptr;
        VxBbox box; box.Min.Set(-.1f, -.1f, -.1f); box.Max.Set(.1f, .1f, .1f);
        if (!entity->SetBoundingBox(&box, TRUE)) return nullptr;
        entity->SetPosition3f(0, 0, 5); Device->AddObject(entity); Device->AddRemoveSequence(FALSE);
        return entity;
    }
    void Expect(int index, CallbackDispatchCall &call, RCK3dEntity *object = nullptr) {
        ASSERT_LT(index, Trace.Count); EXPECT_EQ(call.Value, Trace.Values[index]);
        EXPECT_EQ(Device, Trace.Contexts[index]);
        EXPECT_EQ(reinterpret_cast<CKRenderObject *>(object), Trace.Objects[index]);
        EXPECT_EQ(&call, Trace.Arguments[index]);
    }
};

TEST_F(CallbacksDispatchTest, PreHelperFiltersTemporaryEntriesWithoutConsumingThemOrCallingOtherSlots) {
    CKCallbacksContainer callbacks;
    CallbackDispatchCall calls[6] = {{&Trace, 1}, {&Trace, 2}, {&Trace, 3}, {&Trace, 4}, {&Trace, 80}, {&Trace, 90}};
    ASSERT_TRUE(callbacks.AddPreCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[0], FALSE, nullptr));
    ASSERT_TRUE(callbacks.AddPreCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[1], 7, nullptr));
    ASSERT_TRUE(callbacks.AddPreCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[2], FALSE, nullptr));
    ASSERT_TRUE(callbacks.AddPreCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[3], -2, nullptr));
    ASSERT_TRUE(callbacks.SetCallBack(reinterpret_cast<void *>(RecordContextCallback), &calls[4]));
    ASSERT_TRUE(callbacks.AddPostCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[5], TRUE, nullptr));
    callbacks.ExecutePreCallbacks(Device, TRUE);
    ASSERT_EQ(2, Trace.Count); Expect(0, calls[1]); Expect(1, calls[3]);
    callbacks.ExecutePreCallbacks(Device, -1);
    ASSERT_EQ(4, Trace.Count); Expect(2, calls[1]); Expect(3, calls[3]);
    callbacks.ExecutePreCallbacks(Device, FALSE);
    ASSERT_EQ(8, Trace.Count); for (int i = 0; i < 4; ++i) Expect(4 + i, calls[i]);
    ASSERT_EQ(4, callbacks.m_PreCallBacks.Size()); EXPECT_EQ(7, callbacks.m_PreCallBacks[1].temp);
    EXPECT_EQ(-2, callbacks.m_PreCallBacks[3].temp); EXPECT_EQ(1, callbacks.m_PostCallBacks.Size());
    EXPECT_EQ(&calls[4], callbacks.m_Callback->argument);
}

TEST_F(CallbacksDispatchTest, PostHelperFiltersTemporaryEntriesWithoutExecutingThePreOrMainSlot) {
    CKCallbacksContainer callbacks;
    CallbackDispatchCall calls[5] = {{&Trace, 1}, {&Trace, 2}, {&Trace, 3}, {&Trace, 80}, {&Trace, 90}};
    ASSERT_TRUE(callbacks.AddPostCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[0], FALSE, nullptr));
    ASSERT_TRUE(callbacks.AddPostCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[1], -3, nullptr));
    ASSERT_TRUE(callbacks.AddPostCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[2], TRUE, nullptr));
    ASSERT_TRUE(callbacks.SetCallBack(reinterpret_cast<void *>(RecordContextCallback), &calls[3]));
    ASSERT_TRUE(callbacks.AddPreCallback(reinterpret_cast<void *>(RecordContextCallback), &calls[4], TRUE, nullptr));
    callbacks.ExecutePostCallbacks(Device, TRUE);
    ASSERT_EQ(2, Trace.Count); Expect(0, calls[1]); Expect(1, calls[2]);
    callbacks.ExecutePostCallbacks(Device, FALSE);
    ASSERT_EQ(5, Trace.Count); for (int i = 0; i < 3; ++i) Expect(2 + i, calls[i]);
    callbacks.ExecutePostCallbacks(Device, TRUE);
    ASSERT_EQ(7, Trace.Count); Expect(5, calls[1]); Expect(6, calls[2]);
    EXPECT_EQ(3, callbacks.m_PostCallBacks.Size()); EXPECT_EQ(1, callbacks.m_PreCallBacks.Size());
    EXPECT_EQ(&calls[3], callbacks.m_Callback->argument);
}

TEST_F(CallbacksDispatchTest, ActualSceneDispatchSeparatesTwoArgumentContextAndThreeArgumentObjectCallbacks) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    CallbackDispatchCall pre = {&Trace, 20}, objectPre = {&Trace, 30}, main = {&Trace, 40}, objectPost = {&Trace, 50};
    CallbackDispatchCall beforeTransparent = {&Trace, 60}, afterTransparent = {&Trace, 70}, afterSprites = {&Trace, 100};
    Device->AddPreRenderCallBack(RecordContextCallback, &pre, FALSE);
    Device->AddPostRenderCallBack(RecordContextCallback, &beforeTransparent, FALSE, TRUE);
    Device->AddPostRenderCallBack(RecordContextCallback, &afterTransparent, FALSE, FALSE);
    Device->AddPostSpriteRenderCallBack(RecordContextCallback, &afterSprites, FALSE);
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordEntityCallback, &objectPre, FALSE));
    ASSERT_TRUE(entity->SetRenderCallBack(RecordEntityCallback, &main));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordEntityCallback, &objectPost, FALSE));
    Boundary.Trace = &Trace;
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(10, Trace.Count);
    EXPECT_EQ(10, Trace.Values[0]); Expect(1, pre); Expect(2, objectPre, entity); Expect(3, main, entity);
    Expect(4, objectPost, entity); Expect(5, beforeTransparent); Expect(6, afterTransparent);
    EXPECT_EQ(80, Trace.Values[7]); EXPECT_EQ(90, Trace.Values[8]); Expect(9, afterSprites);
}

TEST_F(CallbacksDispatchTest, TemporaryConsumersExecuteOnEveryDrawUntilPreProcessRemovesThemWithoutDispatch) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    CallbackDispatchCall calls[12]; for (int i = 0; i < 12; ++i) calls[i] = {&Trace, i + 1};
    Device->AddPreRenderCallBack(RecordContextCallback, &calls[0], TRUE);
    Device->AddPreRenderCallBack(RecordContextCallback, &calls[1], FALSE);
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordEntityCallback, &calls[2], TRUE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordEntityCallback, &calls[3], FALSE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordEntityCallback, &calls[4], TRUE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(RecordEntityCallback, &calls[5], FALSE));
    Device->AddPostRenderCallBack(RecordContextCallback, &calls[6], TRUE, TRUE);
    Device->AddPostRenderCallBack(RecordContextCallback, &calls[7], FALSE, TRUE);
    Device->AddPostRenderCallBack(RecordContextCallback, &calls[8], TRUE, FALSE);
    Device->AddPostRenderCallBack(RecordContextCallback, &calls[9], FALSE, FALSE);
    Device->AddPostSpriteRenderCallBack(RecordContextCallback, &calls[10], TRUE);
    Device->AddPostSpriteRenderCallBack(RecordContextCallback, &calls[11], FALSE);
    ASSERT_EQ(2, Manager->m_TemporaryPreRenderCallbacks.Size()); ASSERT_EQ(4, Manager->m_TemporaryPostRenderCallbacks.Size());
    for (int frame = 0; frame < 2; ++frame) {
        ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
        ASSERT_EQ((frame + 1) * 12, Trace.Count);
        for (int i = 0; i < 12; ++i) Expect(frame * 12 + i, calls[i], i >= 2 && i <= 5 ? entity : nullptr);
    }
    ASSERT_EQ(CK_OK, Manager->PreProcess()); EXPECT_EQ(24, Trace.Count);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size()); EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS)); ASSERT_EQ(30, Trace.Count);
    for (int i = 0; i < 6; ++i) Expect(24 + i, calls[i * 2 + 1], i >= 1 && i <= 2 ? entity : nullptr);
}

TEST_F(CallbacksDispatchTest, PauseRemovesPendingTemporaryContextAndObjectCallbacksBeforeTheirFirstInvocation) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    CallbackDispatchCall calls[6]; for (int i = 0; i < 6; ++i) calls[i] = {&Trace, i + 1};
    Device->AddPreRenderCallBack(RecordContextCallback, &calls[0], TRUE);
    Device->AddPreRenderCallBack(RecordContextCallback, &calls[1], FALSE);
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordEntityCallback, &calls[2], TRUE));
    ASSERT_TRUE(entity->AddPreRenderCallBack(RecordEntityCallback, &calls[3], FALSE));
    Device->AddPostSpriteRenderCallBack(RecordContextCallback, &calls[4], TRUE);
    Device->AddPostSpriteRenderCallBack(RecordContextCallback, &calls[5], FALSE);
    ASSERT_EQ(CK_OK, Manager->OnCKPause()); EXPECT_EQ(0, Trace.Count);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size()); EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS)); ASSERT_EQ(3, Trace.Count);
    Expect(0, calls[1]); Expect(1, calls[3], entity); Expect(2, calls[5]);
}
}
