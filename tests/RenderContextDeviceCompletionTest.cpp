#include "MaterialRenderObserver.h"
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include <SDL3/SDL.h>

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
struct DeviceCompletionCalls {
    int Allocations = 0, Creates = 0, Destructions = 0, DeleteRequests = 0, Shutdowns = 0;
    CKBOOL AllocateResult = TRUE, CreateResult = TRUE, ShutdownResult = TRUE, RefuseDelete = FALSE, Negotiate = FALSE;
    CKRasterizerContextDesc Requested = {};
};
class DeviceCompletionBoundary : public MaterialRenderObserver {
public:
    DeviceCompletionCalls *Calls;
    DeviceCompletionBoundary(CKRasterizerDriver *driver, DeviceCompletionCalls *calls) : Calls(calls) { m_Driver = driver; }
    ~DeviceCompletionBoundary() override { ++Calls->Destructions; }
    CKBOOL Create(WIN_HANDLE window, int x, int y, int width, int height, int bpp,
                  CKBOOL fullscreen, int refresh, int zbpp, int stencil) override {
        ++Calls->Creates;
        SetContextDesc(window, x, y, width, height, bpp, fullscreen, refresh, zbpp, stencil);
        GetDesc(&Calls->Requested);
        if (Calls->Negotiate) {
            m_PosX = 7; m_PosY = 11; m_Width = 193; m_Height = 107;
            m_Bpp = 24; m_ZBpp = 16; m_StencilBpp = 0;
        }
        return Calls->CreateResult;
    }
    CKBOOL SetOptions(const CKRasterizerOptions *options) override { SetContextOptions(*options); return TRUE; }
    CKBOOL BeginShutdown() override { ++Calls->Shutdowns; return Calls->ShutdownResult; }
    CKBOOL Clear(CKDWORD, CKDWORD, float, CKDWORD, int, CKRECT *) override { return TRUE; }
};
class DeviceCompletionDriver : public CKRasterizerDriver {
public:
    DeviceCompletionCalls Calls;
    DeviceCompletionDriver(CKRasterizer *owner, CKDWORD index) : CKRasterizerDriver(owner, index, "Device lifecycle observer", FALSE) { m_CapsFinal = TRUE; }
    ~DeviceCompletionDriver() override { DestroyContexts(); }
    CKBOOL GetNativeCaps(CKRasterizerNativeCapsDesc *) const override { return FALSE; }
    CKRasterizerContext *CreateContext() override {
        ++Calls.Allocations;
        if (!Calls.AllocateResult) return nullptr;
        DeviceCompletionBoundary *context = new DeviceCompletionBoundary(this, &Calls); AddContext(context); return context;
    }
    CKBOOL DestroyContext(CKRasterizerContext *context) override {
        ++Calls.DeleteRequests;
        return Calls.RefuseDelete ? FALSE : CKRasterizerDriver::DestroyContext(context);
    }
    int OwnedCount() const { return m_Contexts.Size(); }
};
class DeviceCompletionRasterizer : public CKRasterizer {
public:
    DeviceCompletionRasterizer() {
        AddDriver(new DeviceCompletionDriver(this, 0)); AddDriver(new DeviceCompletionDriver(this, 1));
    }
};
CKRasterizer *StartDeviceCompletionRasterizer(WIN_HANDLE) { return new DeviceCompletionRasterizer; }
void CloseDeviceCompletionRasterizer(CKRasterizer *rasterizer) { delete rasterizer; }
class RenderContextDeviceCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *View = nullptr;
    SDL_Window *Window = nullptr; XClassArray<CKRasterizerInfo> Saved;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT); CKBuildClassHierarchyTable();
        Saved = g_RasterizersInfo; g_RasterizersInfo.Clear();
        CKRasterizerInfo info; info.StartFct = StartDeviceCompletionRasterizer; info.CloseFct = CloseDeviceCompletionRasterizer;
        info.InterfaceRevision = CKRST_INTERFACE_REVISION; g_RasterizersInfo.PushBack(info);
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        ASSERT_EQ(2, Manager->GetRenderDriverCount()); View = new RCKRenderContext(Context);
    }
    void TearDown() override {
        if (Manager) for (int i = 0; i < 2; ++i) { Driver(i)->Calls.RefuseDelete = FALSE; Driver(i)->Calls.ShutdownResult = TRUE; }
        delete View;
        if (Context) CKCloseContext(Context);
        if (Window) { SDL_DestroyWindow(Window); SDL_QuitSubSystem(SDL_INIT_VIDEO); }
        g_RasterizersInfo = Saved; CKShutdown();
    }
    DeviceCompletionDriver *Driver(int index = 0) { return static_cast<DeviceCompletionDriver *>(Manager->GetDriver(index)); }
    CKERROR Create(int index = 0) { CKRECT rect = {3, 5, 323, 245}; return View->Create(Window, index, &rect, FALSE, 32, 24, 8, 75); }
    void CreateHiddenWindow() {
        ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
        Window = SDL_CreateWindow("Render context device transitions", 320, 240, SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
        ASSERT_NE(nullptr, Window) << SDL_GetError();
        ASSERT_TRUE(SDL_SetWindowPosition(Window, 101, 123));
    }
};
}

TEST_F(RenderContextDeviceCompletionTest, CreatePublishesNegotiatedDimensionsDepthAndDriverAfterSuccess) {
    Driver(1)->Calls.Negotiate = TRUE; ASSERT_EQ(CK_OK, Create(1));
    EXPECT_EQ(1, View->GetDriverIndex()); EXPECT_EQ(Driver(1), View->m_RasterizerDriver);
    EXPECT_EQ(Manager->GetDriverDescription(1), Manager->GetDriverDescription(View->m_RasterizerContext));
    EXPECT_EQ(7, View->m_Settings.m_Rect.left); EXPECT_EQ(11, View->m_Settings.m_Rect.top);
    EXPECT_EQ(193, View->m_Settings.m_Rect.right); EXPECT_EQ(107, View->m_Settings.m_Rect.bottom);
    EXPECT_EQ(24u, View->m_Settings.m_Bpp); EXPECT_EQ(16u, View->m_Settings.m_Zbpp); EXPECT_EQ(0u, View->m_Settings.m_StencilBpp);
    EXPECT_EQ(193, View->m_ViewportData.ViewWidth); EXPECT_EQ(107, View->m_ViewportData.ViewHeight);
    EXPECT_TRUE(View->m_Active); EXPECT_FALSE(View->m_DeviceDestroying); EXPECT_FALSE(View->m_ProjectionUpdated);
    float separation = 0, focal = 0; View->GetStereoParameters(separation, focal);
    EXPECT_FLOAT_EQ(.4f, separation); EXPECT_FLOAT_EQ(100, focal);
    CKRasterizerContext *owner = View->m_RasterizerContext;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, Create(0)); EXPECT_EQ(owner, View->m_RasterizerContext); EXPECT_EQ(1, View->GetDriverIndex());
}

TEST_F(RenderContextDeviceCompletionTest, FailedCreatePreservesRejectedOwnerAndDoesNotCommitDriverIndex) {
    Driver(1)->Calls.CreateResult = FALSE; Driver(1)->Calls.RefuseDelete = TRUE;
    EXPECT_EQ(CKERR_CANCREATERENDERCONTEXT, Create(1)); EXPECT_EQ(0, View->GetDriverIndex());
    ASSERT_NE(nullptr, View->m_RasterizerContext); EXPECT_EQ(Driver(1), View->m_RasterizerDriver);
    EXPECT_EQ(1, Driver(1)->OwnedCount()); EXPECT_EQ(0, Driver(1)->Calls.Destructions);
    EXPECT_FALSE(View->m_DeviceDestroying); EXPECT_EQ(nullptr, Manager->GetDriverDescription(View->m_RasterizerContext));
    Driver(1)->Calls.RefuseDelete = FALSE; EXPECT_TRUE(View->DestroyDevice()); EXPECT_EQ(0, Driver(1)->OwnedCount());
    Driver(1)->Calls.AllocateResult = FALSE; EXPECT_EQ(CKERR_CANCREATERENDERCONTEXT, Create(1));
    EXPECT_EQ(nullptr, View->m_RasterizerContext); EXPECT_EQ(nullptr, View->m_RasterizerDriver); EXPECT_EQ(0, View->GetDriverIndex());
}

TEST_F(RenderContextDeviceCompletionTest, DestroyAcknowledgementRetainsOwnerAndMappingsUntilAccepted) {
    ASSERT_EQ(CK_OK, Create()); CKRasterizerContext *owner = View->m_RasterizerContext;
    Driver()->Calls.ShutdownResult = FALSE; EXPECT_FALSE(View->DestroyDevice());
    EXPECT_EQ(owner, View->m_RasterizerContext); EXPECT_EQ(0, Driver()->Calls.DeleteRequests);
    EXPECT_EQ(Manager->GetDriverDescription(0), Manager->GetDriverDescription(owner));
    Driver()->Calls.ShutdownResult = TRUE; Driver()->Calls.RefuseDelete = TRUE; EXPECT_FALSE(View->DestroyDevice());
    EXPECT_EQ(owner, View->m_RasterizerContext); EXPECT_EQ(Driver(), View->m_RasterizerDriver);
    EXPECT_EQ(Manager->GetDriverDescription(0), Manager->GetDriverDescription(owner)); EXPECT_FALSE(View->m_DeviceDestroying);
    Driver()->Calls.RefuseDelete = FALSE; EXPECT_TRUE(View->DestroyDevice());
    EXPECT_EQ(nullptr, View->m_RasterizerContext); EXPECT_EQ(nullptr, View->m_RasterizerDriver); EXPECT_EQ(nullptr, Manager->GetDriverDescription(owner));
    EXPECT_EQ(1, Driver()->Calls.Destructions); EXPECT_TRUE(View->DestroyDevice()); EXPECT_EQ(1, Driver()->Calls.Destructions);
}

TEST_F(RenderContextDeviceCompletionTest, ChangeDriverUsesNegotiatedSettingsAndCommitsNewOwner) {
    Driver()->Calls.Negotiate = TRUE; ASSERT_EQ(CK_OK, Create()); Driver(1)->Calls.Negotiate = TRUE;
    ASSERT_TRUE(View->ChangeDriver(1)); EXPECT_EQ(1, View->GetDriverIndex()); EXPECT_EQ(0, Driver()->OwnedCount()); EXPECT_EQ(1, Driver(1)->OwnedCount());
    EXPECT_EQ(7, Driver(1)->Calls.Requested.PosX); EXPECT_EQ(11, Driver(1)->Calls.Requested.PosY);
    EXPECT_EQ(193, Driver(1)->Calls.Requested.Width); EXPECT_EQ(107, Driver(1)->Calls.Requested.Height);
    EXPECT_EQ(24, Driver(1)->Calls.Requested.Bpp); EXPECT_EQ(16, Driver(1)->Calls.Requested.ZBpp);
    EXPECT_EQ(193, View->m_ViewportData.ViewWidth); EXPECT_EQ(107, View->m_ViewportData.ViewHeight);
    EXPECT_EQ(0u, View->m_Settings.m_StencilBpp); EXPECT_FALSE(View->ChangeDriver(1));
}

TEST_F(RenderContextDeviceCompletionTest, FailedDriverReplacementRestoresPreviousOwnerWithSavedSettings) {
    Driver()->Calls.Negotiate = TRUE; ASSERT_EQ(CK_OK, Create()); Driver(1)->Calls.CreateResult = FALSE;
    EXPECT_FALSE(View->ChangeDriver(1)); EXPECT_EQ(0, View->GetDriverIndex()); EXPECT_EQ(Driver(), View->m_RasterizerDriver);
    ASSERT_NE(nullptr, View->m_RasterizerContext); EXPECT_EQ(1, Driver()->OwnedCount()); EXPECT_EQ(0, Driver(1)->OwnedCount());
    EXPECT_EQ(2, Driver()->Calls.Creates); EXPECT_EQ(193, Driver()->Calls.Requested.Width); EXPECT_EQ(107, Driver()->Calls.Requested.Height);
    EXPECT_EQ(193, View->m_Settings.m_Rect.right); EXPECT_EQ(107, View->m_Settings.m_Rect.bottom);
    EXPECT_EQ(Manager->GetDriverDescription(0), Manager->GetDriverDescription(View->m_RasterizerContext));
}

TEST_F(RenderContextDeviceCompletionTest, DriverChangeRefusalKeepsOriginalMappingAndDoesNotAllocateReplacement) {
    ASSERT_EQ(CK_OK, Create()); CKRasterizerContext *owner = View->m_RasterizerContext;
    Driver()->Calls.ShutdownResult = FALSE; EXPECT_FALSE(View->ChangeDriver(1)); EXPECT_EQ(0, Driver(1)->Calls.Allocations);
    Driver()->Calls.ShutdownResult = TRUE; Driver()->Calls.RefuseDelete = TRUE; EXPECT_FALSE(View->ChangeDriver(1));
    EXPECT_EQ(owner, View->m_RasterizerContext); EXPECT_EQ(0, View->GetDriverIndex()); EXPECT_EQ(0, Driver(1)->Calls.Allocations);
    EXPECT_EQ(Manager->GetDriverDescription(0), Manager->GetDriverDescription(owner)); EXPECT_FALSE(View->m_DeviceDestroying);
}

TEST_F(RenderContextDeviceCompletionTest, RejectedFailedReplacementRemainsOwnedWithoutCreatingFallbackOverIt) {
    ASSERT_EQ(CK_OK, Create()); Driver(1)->Calls.CreateResult = FALSE; Driver(1)->Calls.RefuseDelete = TRUE;
    EXPECT_FALSE(View->ChangeDriver(1)); EXPECT_EQ(1, Driver()->Calls.Creates); EXPECT_EQ(0, Driver()->OwnedCount());
    ASSERT_NE(nullptr, View->m_RasterizerContext); EXPECT_EQ(Driver(1), View->m_RasterizerDriver);
    EXPECT_EQ(1, Driver(1)->OwnedCount()); EXPECT_EQ(0, Driver(1)->Calls.Destructions); EXPECT_FALSE(View->m_DeviceDestroying);
    Driver(1)->Calls.RefuseDelete = FALSE; EXPECT_TRUE(View->DestroyDevice()); EXPECT_EQ(0, Driver(1)->OwnedCount());
}

TEST_F(RenderContextDeviceCompletionTest, FullscreenOnAnotherDriverReturnsToTheWindowedDriver) {
    CreateHiddenWindow(); ASSERT_NE(nullptr, Window); ASSERT_EQ(CK_OK, Create());
    ASSERT_EQ(CK_OK, View->GoFullScreen(80, 60, 32, 1, 60)); EXPECT_TRUE(View->IsFullScreen()); EXPECT_EQ(1, View->GetDriverIndex());
    EXPECT_EQ(Driver(1), View->m_RasterizerDriver); EXPECT_EQ(View->m_RasterizerContext, Manager->GetFullscreenContext());
    EXPECT_FALSE(View->ChangeDriver(0)); EXPECT_EQ(CKERR_ALREADYFULLSCREEN, View->GoFullScreen(80, 60, 32, 0, 60));
    ASSERT_EQ(CK_OK, View->StopFullScreen()); EXPECT_FALSE(View->IsFullScreen()); EXPECT_EQ(0, View->GetDriverIndex());
    EXPECT_EQ(Driver(), View->m_RasterizerDriver); EXPECT_EQ(320, View->m_Settings.m_Rect.right); EXPECT_EQ(240, View->m_Settings.m_Rect.bottom);
    EXPECT_EQ(2, Driver()->Calls.Creates); EXPECT_EQ(1, Driver(1)->Calls.Creates); EXPECT_EQ(nullptr, Manager->GetFullscreenContext());
}

TEST_F(RenderContextDeviceCompletionTest, FullscreenFailureRestoresWindowedOwnerAndPreservesDriverSelection) {
    CreateHiddenWindow(); ASSERT_NE(nullptr, Window); ASSERT_EQ(CK_OK, Create()); Driver(1)->Calls.CreateResult = FALSE;
    EXPECT_EQ(CKERR_CANCREATERENDERCONTEXT, View->GoFullScreen(80, 60, 32, 1, 60));
    EXPECT_FALSE(View->IsFullScreen()); EXPECT_EQ(0, View->GetDriverIndex()); EXPECT_EQ(Driver(), View->m_RasterizerDriver);
    ASSERT_NE(nullptr, View->m_RasterizerContext); EXPECT_EQ(1, Driver()->OwnedCount()); EXPECT_EQ(0, Driver(1)->OwnedCount());
    EXPECT_EQ(320, View->m_Settings.m_Rect.right); EXPECT_EQ(240, View->m_Settings.m_Rect.bottom);
    EXPECT_EQ(nullptr, Manager->GetFullscreenContext());
}
TEST_F(RenderContextDeviceCompletionTest, DestructionReleasesOwnedDeviceMappingMembershipAndContextMask) {
    ASSERT_EQ(CK_OK, Create()); CKRasterizerContext *owner = View->m_RasterizerContext;
    RCK3dEntity entity(Context); View->AddObject(reinterpret_cast<CKRenderObject *>(&entity));
    CKDWORD mask = View->m_MaskFree; ASSERT_TRUE(entity.IsInRenderContext(View));
    delete View; View = nullptr;
    EXPECT_EQ(1, Driver()->Calls.Destructions); EXPECT_EQ(0, Driver()->OwnedCount());
    EXPECT_EQ(nullptr, Manager->GetDriverDescription(owner)); EXPECT_EQ(mask, Manager->m_RenderContextMaskFree & mask);
    EXPECT_EQ(0u, entity.GetInRenderContextMask() & mask);
}
