#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"

namespace {
class PlatformFormatBoundary : public TextureTestObserver {
public:
    CKBOOL TargetResult = TRUE;
    CKDWORD Target = 0;
    mutable int DescriptorQueries = 0;

    CKBOOL SetTargetTexture(CKDWORD target, int, int, CKRST_CUBEFACE) override {
        if (!TargetResult)
            return FALSE;
        Target = target;
        return TRUE;
    }

    CKBOOL GetTextureDesc(CKDWORD handle, CKTextureDesc *desc) const override {
        ++DescriptorQueries;
        return TextureTestObserver::GetTextureDesc(handle, desc);
    }
};

class RenderContextPlatformCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    RCKRenderContext *OtherDevice = nullptr;
    PlatformFormatBoundary State, OtherState;
    CKRasterizerDriver Driver;

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
        ASSERT_NE(nullptr, Manager->m_2DRootFore);
        ASSERT_NE(nullptr, Manager->m_2DRootBack);
        ASSERT_NE(nullptr, Manager->m_DefaultMat);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        VxImageDescEx format;
        VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&OtherState, 0));
        Device = new RCKRenderContext(Context);
        OtherDevice = new RCKRenderContext(Context);
        Configure(Device, &State);
        Configure(OtherDevice, &OtherState);
    }

    void Configure(RCKRenderContext *device, CKRasterizerContext *state) {
        device->m_RasterizerContext = state;
        device->m_RasterizerDriver = &Driver;
        device->m_DriverIndex = 0;
        device->m_Settings.m_Rect = CKRECT{0, 0, 320, 200};
        device->m_Settings.m_Bpp = 32;
        device->m_Settings.m_Zbpp = 24;
        device->m_Settings.m_StencilBpp = 8;
        device->m_WindowSettings = device->m_Settings;
    }

    void TearDown() override {
        if (Device) {
            Device->m_TargetTexture = nullptr;
            Device->m_RasterizerContext = nullptr;
            Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (OtherDevice) {
            OtherDevice->m_TargetTexture = nullptr;
            OtherDevice->m_RasterizerContext = nullptr;
            OtherDevice->m_RasterizerDriver = nullptr;
            delete OtherDevice;
        }
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    RCKTexture *Texture() {
        RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
        if (!texture || !texture->CreateImage(4, 2, 32, 0))
            return nullptr;
        return texture;
    }
};
}

TEST_F(RenderContextPlatformCompletionTest, WindowBitDepthDoesNotInventChannelMasks) {
    const int bitCounts[] = {16, 24, 32};
    for (int bits : bitCounts) {
        Device->m_Settings.m_Bpp = bits;
        int bpp = -1, depth = -1, stencil = -1;
        EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat(&bpp, &depth, &stencil));
        EXPECT_EQ(bits, bpp);
        EXPECT_EQ(24, depth);
        EXPECT_EQ(8, stencil);
    }
    EXPECT_EQ(0, State.DescriptorQueries);
    EXPECT_EQ(0, State.Readbacks);
}

TEST_F(RenderContextPlatformCompletionTest, OptionalBitOutputsRemainIndependentWhenTheFormatIsUnknown) {
    Device->m_Settings.m_Bpp = 16;
    Device->m_Settings.m_Zbpp = 32;
    Device->m_Settings.m_StencilBpp = 0;
    int output = -1;
    EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat(&output));
    EXPECT_EQ(16, output);
    EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat(nullptr, &output));
    EXPECT_EQ(32, output);
    EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat(nullptr, nullptr, &output));
    EXPECT_EQ(0, output);
    EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat());
}

TEST_F(RenderContextPlatformCompletionTest, TextureTargetReportsActualResidentFormatInsteadOfRequestedOrSystemFormat) {
    const VX_PIXELFORMAT formats[] = {_16_RGB565, _16_ARGB1555, _32_RGB888, _32_ARGB8888};
    for (VX_PIXELFORMAT format : formats) {
        RCKTexture *texture = Texture();
        ASSERT_NE(nullptr, texture);
        texture->SetDesiredVideoFormat(_32_ARGB8888);
        State.ActualFormat = format;
        ASSERT_TRUE(Device->SetRenderTarget(texture));
        int bpp = -1, depth = -1, stencil = -1;
        const int before = State.DescriptorQueries;
        EXPECT_EQ(format, Device->GetPixelFormat(&bpp, &depth, &stencil));
        EXPECT_EQ(State.Resource[State.Target].Format.BitsPerPixel, bpp);
        EXPECT_EQ(24, depth);
        EXPECT_EQ(8, stencil);
        EXPECT_GT(State.DescriptorQueries, before);
        EXPECT_EQ(32, Device->m_Settings.m_Bpp);
        EXPECT_EQ(0, State.Loads);
        EXPECT_EQ(0, State.Readbacks);
        ASSERT_TRUE(Device->SetRenderTarget(nullptr));
    }
}

TEST_F(RenderContextPlatformCompletionTest, RejectedTargetDescriptorDoesNotAdvertiseAStalePixelFormat) {
    RCKTexture *texture = Texture();
    ASSERT_NE(nullptr, texture);
    State.ActualFormat = _16_RGB565;
    ASSERT_TRUE(Device->SetRenderTarget(texture));
    EXPECT_EQ(_16_RGB565, Device->GetPixelFormat());
    State.QueryResult = FALSE;
    int bpp = -1, depth = -1, stencil = -1;
    EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat(&bpp, &depth, &stencil));
    EXPECT_EQ(0, bpp);
    EXPECT_EQ(24, depth);
    EXPECT_EQ(8, stencil);
    EXPECT_EQ(texture, Device->m_TargetTexture);
    State.QueryResult = TRUE;
    EXPECT_EQ(_16_RGB565, Device->GetPixelFormat(&bpp));
    EXPECT_EQ(16, bpp);
}

TEST_F(RenderContextPlatformCompletionTest, ReturningToWindowDropsTheTextureFormatAndRestoresWindowBitDepth) {
    RCKTexture *texture = Texture();
    ASSERT_NE(nullptr, texture);
    State.ActualFormat = _16_ARGB1555;
    ASSERT_TRUE(Device->SetRenderTarget(texture));
    int bpp = -1;
    EXPECT_EQ(_16_ARGB1555, Device->GetPixelFormat(&bpp));
    EXPECT_EQ(16, bpp);
    ASSERT_TRUE(Device->SetRenderTarget(nullptr));
    const int before = State.DescriptorQueries;
    EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat(&bpp));
    EXPECT_EQ(32, bpp);
    EXPECT_EQ(before, State.DescriptorQueries);
}

TEST_F(RenderContextPlatformCompletionTest, SharedTargetQueriesTheRequestingContextAfterAnotherOwnerWasSelected) {
    RCKTexture *texture = Texture();
    ASSERT_NE(nullptr, texture);
    State.ActualFormat = _16_RGB565;
    OtherState.ActualFormat = _32_ARGB8888;
    OtherState.NextHandle = 9;
    ASSERT_TRUE(Device->SetRenderTarget(texture));
    const CKDWORD firstHandle = State.Target;
    ASSERT_TRUE(OtherDevice->SetRenderTarget(texture));
    const CKDWORD secondHandle = OtherState.Target;
    ASSERT_NE(firstHandle, secondHandle);
    ASSERT_EQ(secondHandle, CKDWORD(texture->GetRstTextureIndex()));
    int bpp = -1;
    EXPECT_EQ(_16_RGB565, Device->GetPixelFormat(&bpp));
    EXPECT_EQ(16, bpp);
    EXPECT_EQ(_32_ARGB8888, OtherDevice->GetPixelFormat(&bpp));
    EXPECT_EQ(32, bpp);
    EXPECT_EQ(firstHandle, State.Target);
    EXPECT_EQ(secondHandle, OtherState.Target);
    EXPECT_EQ(1, State.Creates);
    EXPECT_EQ(1, OtherState.Creates);
}

TEST_F(RenderContextPlatformCompletionTest, TargetQueryObservesTheCurrentDescriptorWithoutReallocationOrUpload) {
    RCKTexture *texture = Texture();
    ASSERT_NE(nullptr, texture);
    ASSERT_TRUE(Device->SetRenderTarget(texture));
    EXPECT_EQ(_32_ARGB8888, Device->GetPixelFormat());
    VxImageDescEx changed;
    VxPixelFormat2ImageDesc(_16_ARGB4444, changed);
    changed.Width = 4;
    changed.Height = 2;
    State.Resource[State.Target].Format = changed;
    int bpp = -1;
    EXPECT_EQ(_16_ARGB4444, Device->GetPixelFormat(&bpp));
    EXPECT_EQ(16, bpp);
    EXPECT_EQ(1, State.Creates);
    EXPECT_EQ(0, State.Loads);
    EXPECT_EQ(0, State.Readbacks);
}
