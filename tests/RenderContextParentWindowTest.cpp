#include "MaterialRenderObserver.h"
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include <SDL3/SDL.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
struct ParentWindowCalls {
    CKBOOL CreateResult = TRUE;
    CKBOOL ShutdownResult = TRUE;
    int Creates = 0;
    int Shutdowns = 0;
};

class ParentWindowBoundary : public MaterialRenderObserver {
public:
    ParentWindowCalls *Calls;
    ParentWindowBoundary(CKRasterizerDriver *driver, ParentWindowCalls *calls) : Calls(calls) {
        m_Driver = driver;
    }
    CKBOOL Create(WIN_HANDLE window, int x, int y, int width, int height, int bpp,
                  CKBOOL fullscreen, int refresh, int zbpp, int stencil) override {
        ++Calls->Creates;
        SetContextDesc(window, x, y, width, height, bpp, fullscreen, refresh, zbpp, stencil);
        return Calls->CreateResult;
    }
    CKBOOL SetOptions(const CKRasterizerOptions *options) override {
        SetContextOptions(*options);
        return TRUE;
    }
    CKBOOL BeginShutdown() override {
        ++Calls->Shutdowns;
        return Calls->ShutdownResult;
    }
    CKBOOL Clear(CKDWORD, CKDWORD, float, CKDWORD, int, CKRECT *) override { return TRUE; }
};

class ParentWindowDriver : public CKRasterizerDriver {
public:
    ParentWindowCalls Calls;
    ParentWindowDriver(CKRasterizer *owner, CKDWORD index)
        : CKRasterizerDriver(owner, index, "Parent window transition observer", FALSE) {
        m_CapsFinal = TRUE;
    }
    ~ParentWindowDriver() override { DestroyContexts(); }
    CKBOOL GetNativeCaps(CKRasterizerNativeCapsDesc *) const override { return FALSE; }
    CKRasterizerContext *CreateContext() override {
        ParentWindowBoundary *context = new ParentWindowBoundary(this, &Calls);
        AddContext(context);
        return context;
    }
};

class ParentWindowRasterizer : public CKRasterizer {
public:
    ParentWindowRasterizer() {
        AddDriver(new ParentWindowDriver(this, 0));
        AddDriver(new ParentWindowDriver(this, 1));
    }
};

CKRasterizer *StartParentWindowRasterizer(WIN_HANDLE) { return new ParentWindowRasterizer; }
void CloseParentWindowRasterizer(CKRasterizer *rasterizer) { delete rasterizer; }

struct ParentWindowSnapshot {
    int X = 0, Y = 0, Width = 0, Height = 0;
    SDL_WindowFlags Flags = 0;
#if defined(_WIN32)
    HWND Native = nullptr;
    HWND NativeOwner = nullptr;
    LONG_PTR Style = 0, ExtendedStyle = 0;
    RECT NativeRect = {};
#endif
};

class RenderContextParentWindowTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *View = nullptr;
    SDL_Window *Parent = nullptr;
    SDL_Window *OtherParent = nullptr;
    SDL_Window *Window = nullptr;
    XClassArray<CKRasterizerInfo> Saved;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        Saved = g_RasterizersInfo;
        g_RasterizersInfo.Clear();
        CKRasterizerInfo info;
        info.StartFct = StartParentWindowRasterizer;
        info.CloseFct = CloseParentWindowRasterizer;
        info.InterfaceRevision = CKRST_INTERFACE_REVISION;
        g_RasterizersInfo.PushBack(info);
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_EQ(2, Manager->GetRenderDriverCount());
        View = new RCKRenderContext(Context);
        ASSERT_NE(nullptr, View->Get2dRoot(FALSE));
        ASSERT_NE(nullptr, View->Get2dRoot(TRUE));

        ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
        Parent = SDL_CreateWindow("Context parent", 600, 450, SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
        ASSERT_NE(nullptr, Parent) << SDL_GetError();
        Window = SDL_CreateWindow("Context owned render window", 240, 180, SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
        ASSERT_NE(nullptr, Window) << SDL_GetError();
        ASSERT_TRUE(SDL_SetWindowPosition(Parent, 137, 163)) << SDL_GetError();
        ASSERT_TRUE(SDL_SetWindowPosition(Window, 451, 469)) << SDL_GetError();
        ASSERT_TRUE(SDL_SetWindowParent(Window, Parent)) << SDL_GetError();
        ASSERT_EQ(Parent, SDL_GetWindowParent(Window));
        ASSERT_EQ(static_cast<WIN_HANDLE>(Parent), VxGetParent(Window));
    }

    void TearDown() override {
        if (Manager) {
            Driver(0)->Calls.ShutdownResult = TRUE;
            Driver(1)->Calls.ShutdownResult = TRUE;
        }
        delete View;
        if (Context)
            CKCloseContext(Context);
        if (Window)
            SDL_DestroyWindow(Window);
        if (OtherParent)
            SDL_DestroyWindow(OtherParent);
        if (Parent)
            SDL_DestroyWindow(Parent);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        g_RasterizersInfo = Saved;
        CKShutdown();
    }

    ParentWindowDriver *Driver(int index = 0) {
        return static_cast<ParentWindowDriver *>(Manager->GetDriver(index));
    }

    CKERROR CreateWindowed() {
        CKRECT rect = {0, 0, 240, 180};
        return View->Create(Window, 0, &rect, FALSE, 32, 24, 8, 0);
    }

    void Capture(ParentWindowSnapshot &snapshot) {
        ASSERT_TRUE(SDL_GetWindowPosition(Window, &snapshot.X, &snapshot.Y)) << SDL_GetError();
        ASSERT_TRUE(SDL_GetWindowSize(Window, &snapshot.Width, &snapshot.Height)) << SDL_GetError();
        snapshot.Flags = SDL_GetWindowFlags(Window);
        CKRECT rect = {};
        ASSERT_TRUE(VxGetWindowRect(Window, &rect));
        ASSERT_EQ(snapshot.X, rect.left);
        ASSERT_EQ(snapshot.Y, rect.top);
        ASSERT_EQ(snapshot.X + snapshot.Width, rect.right);
        ASSERT_EQ(snapshot.Y + snapshot.Height, rect.bottom);
#if defined(_WIN32)
        snapshot.Native = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(Window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        ASSERT_NE(nullptr, snapshot.Native);
        snapshot.NativeOwner = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(Parent), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        ASSERT_NE(nullptr, snapshot.NativeOwner);
        ASSERT_EQ(snapshot.NativeOwner, GetWindow(snapshot.Native, GW_OWNER));
        snapshot.Style = GetWindowLongPtr(snapshot.Native, GWL_STYLE);
        snapshot.ExtendedStyle = GetWindowLongPtr(snapshot.Native, GWL_EXSTYLE);
        // SDL's parent is an owned top-level window here, not a WS_CHILD HWND.
        ASSERT_EQ(0, snapshot.Style & WS_CHILD);
        ASSERT_TRUE(::GetWindowRect(snapshot.Native, &snapshot.NativeRect));
        ASSERT_EQ(snapshot.X, snapshot.NativeRect.left);
        ASSERT_EQ(snapshot.Y, snapshot.NativeRect.top);
#endif
    }

    void ExpectUnchangedWindow(const ParentWindowSnapshot &snapshot) {
        ParentWindowSnapshot current;
        Capture(current);
        EXPECT_EQ(Parent, SDL_GetWindowParent(Window));
        EXPECT_EQ(static_cast<WIN_HANDLE>(Parent), VxGetParent(Window));
        EXPECT_EQ(snapshot.X, current.X);
        EXPECT_EQ(snapshot.Y, current.Y);
        EXPECT_EQ(snapshot.Width, current.Width);
        EXPECT_EQ(snapshot.Height, current.Height);
        EXPECT_EQ(snapshot.Flags, current.Flags);
#if defined(_WIN32)
        EXPECT_EQ(snapshot.Native, current.Native);
        EXPECT_EQ(snapshot.NativeOwner, current.NativeOwner);
        EXPECT_EQ(snapshot.Style, current.Style);
        EXPECT_EQ(snapshot.ExtendedStyle, current.ExtendedStyle);
        EXPECT_EQ(snapshot.NativeRect.left, current.NativeRect.left);
        EXPECT_EQ(snapshot.NativeRect.top, current.NativeRect.top);
        EXPECT_EQ(snapshot.NativeRect.right, current.NativeRect.right);
        EXPECT_EQ(snapshot.NativeRect.bottom, current.NativeRect.bottom);
#endif
    }
};
}

TEST_F(RenderContextParentWindowTest, FullscreenRoundTripRestoresTheOwnedWindowsDesktopRectangle) {
    ASSERT_EQ(CK_OK, CreateWindowed());
    ParentWindowSnapshot before;
    Capture(before);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(CK_OK, View->GoFullScreen(80, 60, 32, 1, 60));
    ASSERT_EQ(static_cast<WIN_HANDLE>(Parent), View->m_AppHandle);
    EXPECT_EQ(Parent, SDL_GetWindowParent(Window));
    EXPECT_EQ(1, View->GetDriverIndex());
    EXPECT_EQ(before.X, View->m_WinRect.left);
    EXPECT_EQ(before.Y, View->m_WinRect.top);
    ParentWindowSnapshot fullscreen;
    Capture(fullscreen);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(80, fullscreen.Width);
    EXPECT_EQ(60, fullscreen.Height);
    EXPECT_EQ(before.Flags, fullscreen.Flags);
#if defined(_WIN32)
    EXPECT_EQ(before.Native, fullscreen.Native);
    EXPECT_EQ(before.NativeOwner, fullscreen.NativeOwner);
    EXPECT_EQ(before.Style, fullscreen.Style);
    EXPECT_EQ(before.ExtendedStyle, fullscreen.ExtendedStyle);
#endif
    OtherParent = SDL_CreateWindow("Different live owner", 500, 400, SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
    ASSERT_NE(nullptr, OtherParent) << SDL_GetError();
    ASSERT_TRUE(SDL_SetWindowParent(Window, OtherParent)) << SDL_GetError();
    ASSERT_EQ(OtherParent, SDL_GetWindowParent(Window));
    ASSERT_EQ(static_cast<WIN_HANDLE>(OtherParent), VxGetParent(Window));
#if defined(_WIN32)
    HWND temporaryOwner = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(OtherParent), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    ASSERT_NE(nullptr, temporaryOwner);
    ASSERT_EQ(temporaryOwner, GetWindow(before.Native, GW_OWNER));
#endif
    ASSERT_EQ(CK_OK, View->StopFullScreen());
    EXPECT_FALSE(View->IsFullScreen());
    EXPECT_EQ(0, View->GetDriverIndex());
    ExpectUnchangedWindow(before);

    delete View;
    View = nullptr;
    // Both SDL windows are borrowed: deleting the Context must not destroy them.
    EXPECT_NE(0u, SDL_GetWindowID(Parent));
    EXPECT_NE(0u, SDL_GetWindowID(Window));
    EXPECT_EQ(Parent, SDL_GetWindowParent(Window));
}

TEST_F(RenderContextParentWindowTest, FailedFullscreenReplacementRestoresTheOwnedWindowAndWindowedDevice) {
    ASSERT_EQ(CK_OK, CreateWindowed());
    ParentWindowSnapshot before;
    Capture(before);
    ASSERT_FALSE(HasFatalFailure());
    Driver(1)->Calls.CreateResult = FALSE;
    EXPECT_EQ(CKERR_CANCREATERENDERCONTEXT, View->GoFullScreen(80, 60, 32, 1, 60));
    EXPECT_EQ(static_cast<WIN_HANDLE>(Parent), View->m_AppHandle);
    EXPECT_FALSE(View->IsFullScreen());
    EXPECT_EQ(0, View->GetDriverIndex());
    EXPECT_EQ(Driver(0), View->m_RasterizerDriver);
    ASSERT_NE(nullptr, View->m_RasterizerContext);
    EXPECT_EQ(2, Driver(0)->Calls.Creates);
    EXPECT_EQ(1, Driver(1)->Calls.Creates);
    ExpectUnchangedWindow(before);
}

TEST_F(RenderContextParentWindowTest, FailedDirectFullscreenCreateRestoresTheRealOwnerStyleAndDesktopRectangle) {
    ParentWindowSnapshot before;
    Capture(before);
    ASSERT_FALSE(HasFatalFailure());
    Driver(1)->Calls.CreateResult = FALSE;
    CKRECT rect = {0, 0, 80, 60};
    EXPECT_EQ(CKERR_CANCREATERENDERCONTEXT, View->Create(Window, 1, &rect, TRUE, 32, 24, 8, 60));
    EXPECT_EQ(static_cast<WIN_HANDLE>(Parent), View->m_AppHandle);
    EXPECT_EQ(nullptr, View->m_RasterizerContext);
    EXPECT_FALSE(View->IsFullScreen());
    ExpectUnchangedWindow(before);
}

TEST_F(RenderContextParentWindowTest, RefusedShutdownKeepsTheRealOwnerWindowAndDeviceUnchanged) {
    ASSERT_EQ(CK_OK, CreateWindowed());
    ParentWindowSnapshot before;
    Capture(before);
    ASSERT_FALSE(HasFatalFailure());
    CKRasterizerContext *owner = View->m_RasterizerContext;
    Driver(0)->Calls.ShutdownResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, View->GoFullScreen(80, 60, 32, 1, 60));
    EXPECT_EQ(static_cast<WIN_HANDLE>(Parent), View->m_AppHandle);
    EXPECT_EQ(owner, View->m_RasterizerContext);
    EXPECT_EQ(0, Driver(1)->Calls.Creates);
    ExpectUnchangedWindow(before);
}

TEST_F(RenderContextParentWindowTest, DriverReplacementAndFallbackLeaveTheRealOwnedWindowUntouched) {
    ASSERT_EQ(CK_OK, CreateWindowed());
    ParentWindowSnapshot before;
    Capture(before);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(View->ChangeDriver(1));
    EXPECT_EQ(1, View->GetDriverIndex());
    ExpectUnchangedWindow(before);
    Driver(0)->Calls.CreateResult = FALSE;
    EXPECT_FALSE(View->ChangeDriver(0));
    EXPECT_EQ(1, View->GetDriverIndex());
    EXPECT_EQ(Driver(1), View->m_RasterizerDriver);
    ASSERT_NE(nullptr, View->m_RasterizerContext);
    ExpectUnchangedWindow(before);
}
