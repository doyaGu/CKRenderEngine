#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"

namespace {
class StateBoundary : public TextureTestObserver {
public:
    CKBOOL ResizeResult = TRUE, TargetResult = TRUE;
    int ResizeCalls = 0, NullMaterials = 0, TargetCalls = 0;
    CKDWORD LastTarget = 0;
    CKBOOL Resize(int, int, int, int, CKDWORD) override { ++ResizeCalls; return ResizeResult; }
    CKBOOL SetMaterial(const CKMaterialData *material) override {
        if (!material) { ++NullMaterials; return TRUE; }
        return TextureTestObserver::SetMaterial(material);
    }
    CKBOOL SetTargetTexture(CKDWORD target, int, int, CKRST_CUBEFACE) override {
        ++TargetCalls;
        if (!TargetResult) return FALSE;
        LastTarget = target;
        return TRUE;
    }
};

class RenderContextStateCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    StateBoundary State;
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
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State;
        Device->m_RasterizerDriver = &Driver;
        Device->m_Settings.m_Rect = CKRECT{7, 11, 320, 200};
        Device->m_Settings.m_Bpp = 32;
        Device->m_Settings.m_Zbpp = 24;
        Device->m_Settings.m_StencilBpp = 8;
        Device->m_WindowSettings = Device->m_Settings;
        Device->m_ViewportData.ViewX = Device->m_ViewportData.ViewY = 0;
        Device->m_ViewportData.ViewWidth = 320;
        Device->m_ViewportData.ViewHeight = 200;
        Device->m_ViewportData.ViewZMin = 0.0f;
        Device->m_ViewportData.ViewZMax = 1.0f;
        State.Viewport = Device->m_ViewportData;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
    }
    void TearDown() override {
        if (Device) {
            Device->m_RasterizerContext = nullptr;
            Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (Manager) Manager->ForgetRasterizerContext(&State);
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void ExpectRect(const VxRect &r, float left, float top, float right, float bottom) {
        EXPECT_FLOAT_EQ(left, r.left); EXPECT_FLOAT_EQ(top, r.top);
        EXPECT_FLOAT_EQ(right, r.right); EXPECT_FLOAT_EQ(bottom, r.bottom);
    }
};

TEST_F(RenderContextStateCompletionTest, OptionsMasksAndGlobalModeKeepIndependentSettings) {
    Device->SetCurrentRenderOptions(CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_DOBACKTOFRONT);
    Device->SetClearBackground(FALSE);
    EXPECT_FALSE(Device->GetClearBackground()); EXPECT_TRUE(Device->GetClearZBuffer());
    Device->SetClearZBuffer(FALSE); Device->SetClearBackground(TRUE);
    EXPECT_TRUE(Device->GetClearBackground()); EXPECT_FALSE(Device->GetClearZBuffer());
    Device->ChangeCurrentRenderOptions(CK_RENDER_CLEARZ, CK_RENDER_CLEARBACK);
    EXPECT_EQ(CKDWORD(CK_RENDER_CLEARZ | CK_RENDER_DOBACKTOFRONT), Device->GetCurrentRenderOptions());
    Device->SetGlobalRenderMode(FlatShading, FALSE, TRUE);
    VxShadeType shading = GouraudShading; CKBOOL texture = TRUE, wireframe = FALSE;
    Device->GetGlobalRenderMode(&shading, &texture, &wireframe);
    EXPECT_EQ(FlatShading, shading); EXPECT_FALSE(texture); EXPECT_TRUE(wireframe);
}

TEST_F(RenderContextStateCompletionTest, SceneAmbientFogAndStereoParametersRoundTripIndependently) {
    float defaultSeparation = 0, defaultFocal = 0;
    Device->GetStereoParameters(defaultSeparation, defaultFocal);
    EXPECT_FLOAT_EQ(.4f, defaultSeparation); EXPECT_FLOAT_EQ(100.0f, defaultFocal);
    Device->SetAmbientLight(.25f, .5f, .75f);
    EXPECT_EQ(0xff3f7fbfu, Device->GetAmbientLight());
    Device->SetAmbientLight(0xffa1b2c3u); EXPECT_EQ(0xffa1b2c3u, Device->GetAmbientLight());
    Device->SetFogMode(VXFOG_LINEAR); Device->SetFogStart(2.5f); Device->SetFogEnd(321.5f);
    Device->SetFogDensity(.125f); Device->SetFogColor(0x11223344u);
    EXPECT_EQ(VXFOG_LINEAR, Device->GetFogMode()); EXPECT_FLOAT_EQ(2.5f, Device->GetFogStart());
    EXPECT_FLOAT_EQ(321.5f, Device->GetFogEnd()); EXPECT_FLOAT_EQ(.125f, Device->GetFogDensity());
    EXPECT_EQ(0x11223344u, Device->GetFogColor());
    Device->SetStereoParameters(-.375f, 2.25f);
    float separation = 0, focal = 0; Device->GetStereoParameters(separation, focal);
    EXPECT_FLOAT_EQ(-.375f, separation); EXPECT_FLOAT_EQ(2.25f, focal);
}

TEST_F(RenderContextStateCompletionTest, StatesTexturesAndNullMaterialAreForwardedToTheActiveBoundary) {
    Device->SetState(VXRENDERSTATE_ZWRITEENABLE, 0x12345678u);
    State.States[VXRENDERSTATE_ZENABLE] = 7;
    EXPECT_EQ(0x12345678u, State.States[VXRENDERSTATE_ZWRITEENABLE]);
    EXPECT_EQ(7u, Device->GetState(VXRENDERSTATE_ZENABLE));
    EXPECT_EQ(&State, Device->GetRasterizerContext());
    State.Textures[2] = 19; State.StageStates[2][CKRST_TSS_OP] = 21;
    EXPECT_TRUE(Device->SetTexture(nullptr, FALSE, 2));
    EXPECT_EQ(0u, State.Textures[2]); EXPECT_EQ(0u, State.StageStates[2][CKRST_TSS_OP]);
    ASSERT_TRUE(Device->SetTextureStageState(CKRST_TSS_OP, CKRST_TOP_MODULATE, 1));
    EXPECT_EQ(CKDWORD(CKRST_TOP_MODULATE), State.StageStates[1][CKRST_TSS_OP]);
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = .375f;
    Device->SetTextureMatrix(matrix, 1);
    EXPECT_FLOAT_EQ(.375f, State.Matrices[VXMATRIX_TEXTURE0 + 1][3][0]);
    State.Textures[0] = 8; State.Textures[1] = 9;
    Device->SetCurrentMaterial(nullptr);
    EXPECT_EQ(1, State.NullMaterials); EXPECT_EQ(0u, State.Textures[0]); EXPECT_EQ(0u, State.Textures[1]);
}

TEST_F(RenderContextStateCompletionTest, StencilAllocatorReportsFirstUnusedBitThroughBit31) {
    EXPECT_EQ(0u, Device->GetStencilFreeMask()); EXPECT_EQ(0, Device->GetFirstFreeStencilBits());
    Device->UsedStencilBits(0x7fffffffu);
    EXPECT_EQ(31, Device->GetFirstFreeStencilBits());
    Device->UsedStencilBits(0x80000000u);
    EXPECT_EQ(0xffffffffu, Device->GetStencilFreeMask()); EXPECT_EQ(-1, Device->GetFirstFreeStencilBits());
}

TEST_F(RenderContextStateCompletionTest, ResizeRejectionPreservesTheActiveWindowAndViewport) {
    State.ResizeResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->Resize(17, 23, 640, 480));
    EXPECT_EQ(320, Device->GetWidth()); EXPECT_EQ(200, Device->GetHeight());
    EXPECT_EQ(320, Device->m_ViewportData.ViewWidth);
    VxRect rect; Device->GetWindowRect(rect); ExpectRect(rect, 7, 11, 327, 211);
}

TEST_F(RenderContextStateCompletionTest, ResizeAndViewRectUseNewDimensionsAndHonorIndependentMoveSizeFlags) {
    ASSERT_EQ(CK_OK, Device->Resize(17, 23, 640, 480));
    EXPECT_EQ(640, Device->GetWidth()); EXPECT_EQ(480, Device->GetHeight());
    VxRect rect; Device->GetViewRect(rect); ExpectRect(rect, 0, 0, 640, 480);
    ASSERT_EQ(CK_OK, Device->Resize(99, 98, 300, 180, VX_RESIZE_NOMOVE));
    Device->GetWindowRect(rect); ExpectRect(rect, 17, 23, 317, 203);
    ASSERT_EQ(CK_OK, Device->Resize(4, 6, 0, 0, VX_RESIZE_NOSIZE));
    Device->GetWindowRect(rect); ExpectRect(rect, 4, 6, 304, 186);
    VxRect viewport(3, 5, 103, 65); Device->SetViewRect(viewport);
    Device->GetViewRect(rect); ExpectRect(rect, 3, 5, 103, 65);
    EXPECT_EQ(3, State.Viewport.ViewX); EXPECT_EQ(100, State.Viewport.ViewWidth);
}

TEST_F(RenderContextStateCompletionTest, RenderTargetFailureKeepsWindowSettingsAndRestorePublishesTheWindowViewport) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.CreateImage(4, 2, 32));
    State.TargetResult = FALSE;
    EXPECT_FALSE(Device->SetRenderTarget(&texture));
    EXPECT_EQ(nullptr, Device->m_TargetTexture); EXPECT_EQ(320, Device->GetWidth());
    State.TargetResult = TRUE;
    ASSERT_TRUE(Device->SetRenderTarget(&texture));
    EXPECT_EQ(&texture, Device->m_TargetTexture); EXPECT_EQ(4, Device->GetWidth()); EXPECT_EQ(2, Device->GetHeight());
    EXPECT_EQ(4, State.Viewport.ViewWidth); EXPECT_EQ(2, State.Viewport.ViewHeight);
    ASSERT_EQ(CK_OK, Device->Resize(8, 10, 800, 600));
    EXPECT_EQ(4, Device->GetWidth()); EXPECT_EQ(2, Device->GetHeight());
    EXPECT_EQ(800, Device->m_WindowSettings.m_Rect.right);
    EXPECT_FALSE(Device->SetRenderTarget(&texture));
    State.TargetResult = FALSE;
    EXPECT_FALSE(Device->SetRenderTarget(nullptr)); EXPECT_EQ(&texture, Device->m_TargetTexture);
    State.TargetResult = TRUE;
    ASSERT_TRUE(Device->SetRenderTarget(nullptr));
    EXPECT_EQ(nullptr, Device->m_TargetTexture); EXPECT_EQ(800, Device->GetWidth()); EXPECT_EQ(600, Device->GetHeight());
    EXPECT_EQ(800, State.Viewport.ViewWidth); EXPECT_EQ(600, State.Viewport.ViewHeight);
}

TEST_F(RenderContextStateCompletionTest, ExtentsAndStatsReturnCurrentValuesWithoutAliasingCallerBuffers) {
    VxRect rect(1, 2, 3, 4); Device->SetCurrentExtents(rect); rect.left = 99;
    Device->GetCurrentExtents(rect); ExpectRect(rect, 1, 2, 3, 4);
    Device->m_Stats.NbTrianglesDrawn = 31;
    VxStats stats = {}; Device->GetStats(&stats); EXPECT_EQ(31, stats.NbTrianglesDrawn);
    stats.NbTrianglesDrawn = 0; EXPECT_EQ(31, Device->GetStats().NbTrianglesDrawn);
    int bpp = 0, depth = 0, stencil = 0;
    EXPECT_EQ(UNKNOWN_PF, Device->GetPixelFormat(&bpp, &depth, &stencil));
    EXPECT_EQ(32, bpp); EXPECT_EQ(24, depth); EXPECT_EQ(8, stencil);
}
} // namespace
