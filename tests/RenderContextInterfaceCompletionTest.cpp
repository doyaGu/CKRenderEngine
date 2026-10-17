#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include <SDL3/SDL.h>

namespace {
class InterfaceBoundary : public TextureTestObserver {
public:
    CKBOOL ResizeResult = TRUE;
    int ResizeCalls = 0;
    CKBOOL Resize(int, int, int, int, CKDWORD) override {
        ++ResizeCalls;
        return ResizeResult;
    }
};

class ResizeDispatchContext : public RCKRenderContext {
public:
    explicit ResizeDispatchContext(CKContext *context) : RCKRenderContext(context) {}
    int X = 0, Y = 0, Width = 0, Height = 0;
    CKDWORD Flags = 0;
    int Calls = 0;
    CKERROR Resize(int x, int y, int width, int height, CKDWORD flags) override {
        ++Calls;
        X = x; Y = y; Width = width; Height = height; Flags = flags;
        return CKERR_INVALIDOPERATION;
    }
};

class RenderContextInterfaceCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    InterfaceBoundary Boundary;
    CKRasterizerDriver Driver;
    SDL_Window *Window = nullptr;
    int OriginX = 0, OriginY = 0;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
        Device->m_RasterizerDriver = &Driver;
        Device->m_Settings.m_Rect = CKRECT{7, 11, 320, 200};
        Device->m_WindowSettings = Device->m_Settings;
    }

    void TearDown() override {
        if (Device) {
            Device->m_RasterizerContext = nullptr;
            Device->m_RasterizerDriver = nullptr;
            Device->m_WinHandle = nullptr;
            delete Device;
        }
        if (Context) CKCloseContext(Context);
        if (Window) {
            SDL_DestroyWindow(Window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        CKShutdown();
    }

    void CreateHiddenWindow() {
        ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
        Window = SDL_CreateWindow("Render context interface coordinates", 320, 240,
                                  SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
        ASSERT_NE(nullptr, Window) << SDL_GetError();
        ASSERT_TRUE(SDL_SetWindowPosition(Window, 137, 151)) << SDL_GetError();
        ASSERT_TRUE(SDL_GetWindowPosition(Window, &OriginX, &OriginY));
        Device->m_WinHandle = Window;
    }
};
}

TEST_F(RenderContextInterfaceCompletionTest, ClientToScreenPreservesFractionsAgainstTheActualWindowOrigin) {
    CreateHiddenWindow(); ASSERT_NE(nullptr, Window);
    Vx2DVector point(-12.75f, 19.375f);
    Device->ClientToScreen(&point);
    EXPECT_FLOAT_EQ(float(OriginX) - 12.75f, point.x);
    EXPECT_FLOAT_EQ(float(OriginY) + 19.375f, point.y);
    point.Set(28.5f, -31.625f);
    Device->ClientToScreen(&point);
    EXPECT_FLOAT_EQ(float(OriginX) + 28.5f, point.x);
    EXPECT_FLOAT_EQ(float(OriginY) - 31.625f, point.y);
    EXPECT_EQ(Window, Device->GetWindowHandle());
}

TEST_F(RenderContextInterfaceCompletionTest, ScreenToClientPreservesFractionsAgainstTheActualWindowOrigin) {
    CreateHiddenWindow(); ASSERT_NE(nullptr, Window);
    Vx2DVector point(float(OriginX) - 17.875f, float(OriginY) + 23.25f);
    Device->ScreenToClient(&point);
    EXPECT_FLOAT_EQ(-17.875f, point.x);
    EXPECT_FLOAT_EQ(23.25f, point.y);
    point.Set(float(OriginX) + 29.625f, float(OriginY) - 41.5f);
    Device->ScreenToClient(&point);
    EXPECT_FLOAT_EQ(29.625f, point.x);
    EXPECT_FLOAT_EQ(-41.5f, point.y);
}

TEST_F(RenderContextInterfaceCompletionTest, MissingWindowLeavesTheOriginalCoordinatesUnchanged) {
    Device->m_WinHandle = nullptr;
    Vx2DVector point(-12.75f, 19.375f);
    Device->ClientToScreen(&point);
    EXPECT_FLOAT_EQ(-12.75f, point.x); EXPECT_FLOAT_EQ(19.375f, point.y);
    Device->ScreenToClient(&point);
    EXPECT_FLOAT_EQ(-12.75f, point.x); EXPECT_FLOAT_EQ(19.375f, point.y);
    Device->ClientToScreen(nullptr);
    Device->ScreenToClient(nullptr);
}

TEST_F(RenderContextInterfaceCompletionTest, WindowRectangleUsesVirtualResizeAndPropagatesItsResult) {
    ResizeDispatchContext dispatch(Context);
    VxRect rect(-7.75f, 9.5f, 314.875f, 218.25f);
    CKRenderContext *publicContext = &dispatch;
    EXPECT_EQ(CKERR_INVALIDOPERATION,
              publicContext->SetWindowRect(rect, VX_RESIZE_NOMOVE | VX_RESIZE_NOSIZE));
    EXPECT_EQ(1, dispatch.Calls);
    EXPECT_EQ(-7, dispatch.X); EXPECT_EQ(9, dispatch.Y);
    EXPECT_EQ(322, dispatch.Width); EXPECT_EQ(208, dispatch.Height);
    EXPECT_EQ(CKDWORD(VX_RESIZE_NOMOVE | VX_RESIZE_NOSIZE), dispatch.Flags);
    EXPECT_FLOAT_EQ(-7.75f, rect.left); EXPECT_FLOAT_EQ(218.25f, rect.bottom);
}

TEST_F(RenderContextInterfaceCompletionTest, RejectedAndPartialWindowResizesKeepTheAdvertisedRectangleConsistent) {
    VxRect rect(17.0f, 23.0f, 417.0f, 323.0f), current;
    Boundary.ResizeResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->SetWindowRect(rect));
    Device->GetWindowRect(current);
    EXPECT_FLOAT_EQ(7.0f, current.left); EXPECT_FLOAT_EQ(11.0f, current.top);
    EXPECT_EQ(320, Device->GetWidth()); EXPECT_EQ(200, Device->GetHeight());
    Boundary.ResizeResult = TRUE;
    ASSERT_EQ(CK_OK, Device->SetWindowRect(rect, VX_RESIZE_NOSIZE));
    Device->GetWindowRect(current);
    EXPECT_FLOAT_EQ(17.0f, current.left); EXPECT_FLOAT_EQ(23.0f, current.top);
    EXPECT_FLOAT_EQ(337.0f, current.right); EXPECT_FLOAT_EQ(223.0f, current.bottom);
    ASSERT_EQ(CK_OK, Device->SetWindowRect(rect, VX_RESIZE_NOMOVE));
    EXPECT_EQ(400, Device->GetWidth()); EXPECT_EQ(300, Device->GetHeight());
    EXPECT_EQ(3, Boundary.ResizeCalls);
}

TEST_F(RenderContextInterfaceCompletionTest, ScreenRelativeWindowRectangleIncludesTheContextOffset) {
    CreateHiddenWindow(); ASSERT_NE(nullptr, Window);
    VxRect rect;
    Device->GetWindowRect(rect, TRUE);
    EXPECT_FLOAT_EQ(float(OriginX + 7), rect.left);
    EXPECT_FLOAT_EQ(float(OriginY + 11), rect.top);
    EXPECT_FLOAT_EQ(float(OriginX + 327), rect.right);
    EXPECT_FLOAT_EQ(float(OriginY + 211), rect.bottom);
    EXPECT_EQ(&Boundary, Device->GetRasterizerContext());
}
