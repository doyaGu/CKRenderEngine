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
struct ManagerContextCalls {
    int Creates = 0, Destroys = 0, OptionWrites = 0;
    CKBOOL CreateResult = TRUE;
};

// Observes only the engine/driver interface. No GPU implementation runs.
class ManagerContextBoundary : public MaterialRenderObserver {
public:
    ManagerContextCalls *Calls;
    ManagerContextBoundary(CKRasterizerDriver *driver, ManagerContextCalls *calls) : Calls(calls) {
        m_Driver = driver;
    }
    ~ManagerContextBoundary() override { ++Calls->Destroys; }
    CKBOOL Create(WIN_HANDLE window, int x, int y, int width, int height, int bpp,
                  CKBOOL fullscreen, int refresh, int zbpp, int stencil) override {
        ++Calls->Creates;
        SetContextDesc(window, x, y, width, height, bpp, fullscreen, refresh, zbpp, stencil);
        return Calls->CreateResult;
    }
    CKBOOL SetOptions(const CKRasterizerOptions *options) override {
        ++Calls->OptionWrites;
        SetContextOptions(*options);
        return TRUE;
    }
    void Fullscreen(CKBOOL value) {
        CKRasterizerContextDesc desc = {};
        GetDesc(&desc);
        SetContextDesc(desc.Window, desc.PosX, desc.PosY, desc.Width, desc.Height,
                       desc.Bpp, value, desc.RefreshRate, desc.ZBpp, desc.StencilBpp);
    }
};

class ManagerContextDriver : public CKRasterizerDriver {
public:
    ManagerContextCalls Calls;
    ManagerContextDriver(CKRasterizer *owner, CKDWORD index, CKSTRING name, CKBOOL hardware)
        : CKRasterizerDriver(owner, index, name, hardware) {
        m_CapsFinal = TRUE;
        VxDisplayMode mode = {800, 600, 32, 75};
        m_DisplayModes.PushBack(mode);
        const VX_PIXELFORMAT formats[] = {_24_RGB888, _16_ARGB1555, _32_ARGB8888};
        for (int i = 0; i < 3; ++i) {
            CKTextureDesc desc;
            VxPixelFormat2ImageDesc(formats[i], desc.Format);
            m_TextureFormats.PushBack(desc);
        }
    }
    ~ManagerContextDriver() override { DestroyContexts(); }
    CKBOOL GetNativeCaps(CKRasterizerNativeCapsDesc *) const override { return FALSE; }
    CKRasterizerContext *CreateContext() override {
        ManagerContextBoundary *context = new ManagerContextBoundary(this, &Calls);
        AddContext(context);
        return context;
    }
    void Description(const char *value) { m_Description = value; }
    void PublishCaps(CKBOOL final, int width) { m_CapsFinal = final; m_DisplayModes[0].Width = width; }
};

class ManagerContextRasterizer : public CKRasterizer {
public:
    ManagerContextRasterizer() {
        AddDriver(new ManagerContextDriver(this, 0, "Hardware observer", TRUE));
        AddDriver(new ManagerContextDriver(this, 1, "Software observer", FALSE));
        AddDriver(new ManagerContextDriver(this, 2, "Second software observer", FALSE));
    }
};

static CKRasterizer *StartManagerContextRasterizer(WIN_HANDLE) { return new ManagerContextRasterizer; }
static void CloseManagerContextRasterizer(CKRasterizer *rasterizer) { delete rasterizer; }

class RenderManagerContextTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr, *OtherContext = nullptr;
    RCKRenderManager *Manager = nullptr;
    XClassArray<CKRasterizerInfo> SavedRasterizers;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        SavedRasterizers = g_RasterizersInfo;
        g_RasterizersInfo.Clear();
        CKRasterizerInfo info;
        info.StartFct = StartManagerContextRasterizer;
        info.CloseFct = CloseManagerContextRasterizer;
        info.InterfaceRevision = CKRST_INTERFACE_REVISION;
        g_RasterizersInfo.PushBack(info);
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_EQ(3, Manager->GetRenderDriverCount());
    }
    void TearDown() override {
        if (OtherContext) CKCloseContext(OtherContext);
        if (Context) CKCloseContext(Context);
        g_RasterizersInfo = SavedRasterizers;
        CKShutdown();
    }
    ManagerContextDriver *Driver(int index = 0) {
        return static_cast<ManagerContextDriver *>(Manager->GetDriver(index));
    }
    CKRenderContext *Create(int driver = 0) {
        CKRECT rect = {3, 5, 323, 245};
        return Manager->CreateRenderContext(nullptr, driver, &rect, FALSE, 32, 24, 8, 75);
    }
};

TEST_F(RenderManagerContextTest, FailedCreationDoesNotPublishContextAndRestoresItsMask) {
    const CKDWORD freeMask = Manager->m_RenderContextMaskFree;
    Driver()->Calls.CreateResult = FALSE;
    EXPECT_EQ(nullptr, Create());
    EXPECT_EQ(1, Driver()->Calls.Creates);
    EXPECT_EQ(1, Driver()->Calls.Destroys);
    EXPECT_EQ(0, Manager->GetRenderContextCount());
    EXPECT_EQ(0, Context->GetObjectsCountByClassID(CKCID_RENDERCONTEXT));
    EXPECT_EQ(freeMask, Manager->m_RenderContextMaskFree);
    EXPECT_EQ(nullptr, Manager->GetFullscreenContext());
}

TEST_F(RenderManagerContextTest, ContextListOwnsPublishedEntriesAndRemoveDoesNotDestroyObjects) {
    CKRenderContext *first = Create(), *second = Create(1);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    ASSERT_EQ(0, static_cast<RCKRenderContext *>(first)->m_ObjectExtents.Size());
    ASSERT_EQ(0, static_cast<RCKRenderContext *>(second)->m_ObjectExtents.Size());
    EXPECT_GE(static_cast<RCKRenderContext *>(first)->m_ObjectExtents.Allocated(), 500);
    EXPECT_GE(static_cast<RCKRenderContext *>(second)->m_ObjectExtents.Allocated(), 500);
    const CK_ID firstId = first->GetID(), secondId = second->GetID();
    EXPECT_EQ(2, Manager->GetRenderContextCount());
    EXPECT_EQ(first, Manager->GetRenderContext(0));
    EXPECT_EQ(second, Manager->GetRenderContext(1));
    EXPECT_EQ(nullptr, Manager->GetRenderContext(-1));
    EXPECT_EQ(nullptr, Manager->GetRenderContext(2));
    CKPOINT point = {11, 13};
    EXPECT_EQ(nullptr, Manager->GetRenderContextFromPoint(point));

    Manager->RemoveRenderContext(first);
    EXPECT_EQ(1, Manager->GetRenderContextCount());
    EXPECT_EQ(first, Context->GetObject(firstId));
    EXPECT_EQ(second, Manager->GetRenderContext(0));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Manager->DestroyRenderContext(first));
    EXPECT_EQ(first, Context->GetObject(firstId));
    EXPECT_EQ(CK_OK, Manager->DestroyRenderContext(second));
    EXPECT_EQ(nullptr, Context->GetObject(secondId));
    EXPECT_EQ(0, Manager->GetRenderContextCount());
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Manager->DestroyRenderContext(nullptr));
    Context->DestroyObject(first);
    EXPECT_EQ(1, Driver()->Calls.Destroys);
    EXPECT_EQ(1, Driver(1)->Calls.Destroys);
}

TEST_F(RenderManagerContextTest, ContextDriverRegistrationRetainsIdentityAndForgetRemovesFullscreenLookup) {
    CKRenderContext *created = Create();
    ASSERT_NE(nullptr, created);
    RCKRenderContext *view = static_cast<RCKRenderContext *>(created);
    ManagerContextBoundary *boundary = static_cast<ManagerContextBoundary *>(view->m_RasterizerContext);
    VxDriverDescEx *driver = Manager->GetDriverDescription(0);
    EXPECT_EQ(driver, Manager->GetDriverDescription(boundary));
    EXPECT_TRUE(Manager->RegisterRasterizerContext(boundary, 0));
    EXPECT_FALSE(Manager->RegisterRasterizerContext(boundary, 1));
    EXPECT_FALSE(Manager->RegisterRasterizerContext(nullptr, 0));
    EXPECT_FALSE(Manager->RegisterRasterizerContext(boundary, 3));
    EXPECT_EQ(nullptr, Manager->GetFullscreenContext());
    boundary->Fullscreen(TRUE);
    EXPECT_EQ(boundary, Manager->GetFullscreenContext());
    Manager->ForgetRasterizerContext(boundary);
    EXPECT_EQ(nullptr, Manager->GetDriverDescription(boundary));
    EXPECT_EQ(nullptr, Manager->GetFullscreenContext());
    EXPECT_TRUE(Manager->RegisterRasterizerContext(boundary, 1));
    EXPECT_EQ(Manager->GetDriverDescription(1), Manager->GetDriverDescription(boundary));
    Manager->ForgetRasterizerContext(boundary);
    EXPECT_EQ(CK_OK, Manager->DestroyRenderContext(created));
}

TEST_F(RenderManagerContextTest, PublicDriverDescriptorsBelongToTheirManagerAndRefreshInPlace) {
    VxDriverDesc *first = Manager->GetRenderDriverDescription(0);
    ASSERT_NE(nullptr, first);
    EXPECT_STREQ("Hardware observer", first->DriverDesc);
    EXPECT_TRUE(first->IsHardware);
    ASSERT_EQ(1, first->DisplayModeCount);
    EXPECT_EQ(800, first->DisplayModes[0].Width);
    EXPECT_EQ(3, first->TextureFormats.Size());
    Driver()->Description("Unpublished capability update");
    Driver()->PublishCaps(FALSE, 1024);
    Manager->m_Drivers[0].CapsUpToDate = FALSE;
    const VxDisplayMode *initialModes = first->DisplayModes;
    EXPECT_EQ(&Manager->m_Drivers[0], Manager->GetDriverDescription(0));
    EXPECT_EQ(first, Manager->GetRenderDriverDescription(0));
    EXPECT_STREQ("Hardware observer", first->DriverDesc);
    EXPECT_EQ(initialModes, first->DisplayModes);
    EXPECT_EQ(800, first->DisplayModes[0].Width);
    ASSERT_EQ(CK_OK, CKCreateContext(&OtherContext, nullptr, 0, 0));
    RCKRenderManager *other = new RCKRenderManager(OtherContext);
    static_cast<ManagerContextDriver *>(other->GetDriver(0))->Description("Other context driver");
    other->RefreshDriverCaps(0);
    VxDriverDesc *second = other->GetRenderDriverDescription(0);
    ASSERT_NE(nullptr, second);
    EXPECT_NE(first, second);
    EXPECT_STREQ("Other context driver", second->DriverDesc);
    EXPECT_STREQ("Hardware observer", first->DriverDesc);

    Driver()->Description("Updated first context");
    Driver()->PublishCaps(TRUE, 1024);
    Manager->RefreshDriverCaps(0);
    EXPECT_STREQ("Updated first context", first->DriverDesc);
    EXPECT_EQ(1024, first->DisplayModes[0].Width);
    EXPECT_STREQ("Other context driver", second->DriverDesc);
    EXPECT_EQ(first, Manager->GetRenderDriverDescription(0));
    EXPECT_EQ(nullptr, Manager->GetRenderDriverDescription(-1));
    EXPECT_EQ(nullptr, Manager->GetRenderDriverDescription(3));
    EXPECT_EQ(nullptr, Manager->GetDriver(-1));
    EXPECT_EQ(nullptr, Manager->GetDriverDescription(3));
}

TEST_F(RenderManagerContextTest, SoftwarePreferenceAndAlphaPromotionUseOrderedDriverFormats) {
    EXPECT_EQ(1, Manager->GetPreferredSoftwareDriver());
    Manager->m_Drivers[2].Caps2D.Family = CKRST_OPENGL;
    EXPECT_EQ(2, Manager->GetPreferredSoftwareDriver());
    VxImageDescEx format;
    VxPixelFormat2ImageDesc(_24_RGB888, format);
    CKBYTE pixel = 71;
    format.Width = 11;
    format.Height = 13;
    format.BytesPerLine = 71;
    format.Image = &pixel;
    const CKDWORD flags = format.Flags;
    Manager->FindNearestTextureFormatWithAlpha(0, format);
    // Both alpha formats are eight bits away: the first enumerated one wins.
    EXPECT_EQ(16, format.BitsPerPixel);
    EXPECT_EQ(0x8000u, format.AlphaMask);
    EXPECT_EQ(0x7c00u, format.RedMask);
    EXPECT_EQ(0x03e0u, format.GreenMask);
    EXPECT_EQ(0x001fu, format.BlueMask);
    EXPECT_EQ(11, format.Width);
    EXPECT_EQ(13, format.Height);
    EXPECT_EQ(71, format.BytesPerLine);
    EXPECT_EQ(&pixel, format.Image);
    EXPECT_EQ(flags, format.Flags);
    Manager->m_Drivers[0].TextureFormats.Resize(0);
    Manager->FindNearestTextureFormatWithAlpha(0, format);
    EXPECT_EQ(16, format.BitsPerPixel);
    EXPECT_EQ(0x8000u, format.AlphaMask);
}

TEST_F(RenderManagerContextTest, DefaultMaterialIsAStableExcludedContextObjectAndCanBeRecreated) {
    CKMaterial *material = Manager->GetDefaultMaterial();
    ASSERT_NE(nullptr, material);
    EXPECT_EQ(material, Manager->GetDefaultMaterial());
    EXPECT_EQ(material, Context->GetObject(material->GetID()));
    EXPECT_TRUE((material->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED) == CK_OBJECT_NOTTOBELISTEDANDSAVED);
    Manager->m_DefaultMat = nullptr;
    CKMaterial *replacement = Manager->GetDefaultMaterial();
    ASSERT_NE(nullptr, replacement);
    EXPECT_NE(material, replacement);
    EXPECT_NE(material->GetID(), replacement->GetID());
    EXPECT_EQ(replacement, Manager->GetDefaultMaterial());
    EXPECT_EQ(material, Context->GetObject(material->GetID()));
}

TEST_F(RenderManagerContextTest, ScreenPointLookupUsesARealHiddenWindowAndPublicationOrder) {
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
    struct WindowScope {
        SDL_Window *Window = nullptr;
        RCKRenderContext *Views[2] = {};
        ~WindowScope() {
            for (int i = 0; i < 2; ++i) if (Views[i]) Views[i]->m_WinHandle = nullptr;
            if (Window) SDL_DestroyWindow(Window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    } scope;
    scope.Window = SDL_CreateWindow("Render manager point lookup", 64, 48, SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
    ASSERT_NE(nullptr, scope.Window) << SDL_GetError();
    ASSERT_TRUE(SDL_SetWindowPosition(scope.Window, 100, 120));
    int x = 0, y = 0;
    ASSERT_TRUE(SDL_GetWindowPosition(scope.Window, &x, &y));
    for (int i = 0; i < 2; ++i) {
        scope.Views[i] = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        ASSERT_NE(nullptr, scope.Views[i]);
        scope.Views[i]->m_WinHandle = scope.Window;
        Manager->m_RenderContexts.PushBack(scope.Views[i]->GetID());
    }
    CKPOINT inside = {x + 31, y + 23}, firstCorner = {x, y}, lastCorner = {x + 64, y + 48};
    CKPOINT before = {x - 1, y + 1}, after = {x + 65, y + 49};
    EXPECT_EQ(scope.Views[0], Manager->GetRenderContextFromPoint(inside));
    EXPECT_EQ(scope.Views[0], Manager->GetRenderContextFromPoint(firstCorner));
    EXPECT_EQ(scope.Views[0], Manager->GetRenderContextFromPoint(lastCorner));
    EXPECT_EQ(nullptr, Manager->GetRenderContextFromPoint(before));
    EXPECT_EQ(nullptr, Manager->GetRenderContextFromPoint(after));
    Manager->RemoveRenderContext(scope.Views[0]);
    EXPECT_EQ(scope.Views[1], Manager->GetRenderContextFromPoint(inside));
}
} // namespace
