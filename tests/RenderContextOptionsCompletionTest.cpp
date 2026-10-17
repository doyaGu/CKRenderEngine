#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKMaterial.h"
#include "../src/CKRasterizer/CKRasterizerLib/CKRenderSettings.h"

namespace {
class OptionsBoundary : public TextureTestObserver {
public:
    CKRasterizerOptions Options;
    int OptionWrites = 0, ViewportWrites = 0, ProjectionWrites = 0;
    CKBOOL LastVSync = FALSE;
    CKBOOL SetOptions(const CKRasterizerOptions *options) override {
        ++OptionWrites;
        Options = *options;
        return TRUE;
    }
    CKBOOL SetViewport(const CKViewportData *viewport) override {
        ++ViewportWrites;
        return TextureTestObserver::SetViewport(viewport);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        if (type == VXMATRIX_PROJECTION)
            ++ProjectionWrites;
        return TextureTestObserver::SetTransformMatrix(type, matrix);
    }
    CKBOOL BackToFront(CKBOOL vsync) override {
        ++PresentCount;
        LastVSync = vsync;
        return TRUE;
    }
    CKBOOL SetTargetTexture(CKDWORD, int, int, CKRST_CUBEFACE) override { return TRUE; }
};

class LayoutObservationChild : public RCK2dEntity {
public:
    int RectWrites = 0;
    explicit LayoutObservationChild(CKContext *context) : RCK2dEntity(context) {}
    void SetRect(const VxRect &rect, CKBOOL keepChildren = FALSE) override {
        ++RectWrites;
        RCK2dEntity::SetRect(rect, keepChildren);
    }
};

class RenderContextOptionsCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    OptionsBoundary State;
    CKRasterizerDriver Driver;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        CKRenderSettingsClearOverridesForTests();
        Setting("FXAA", "false");
        Setting("ForceAnisotropicFiltering", "false");
        Setting("RenderScale", "1");
        Setting("Sharpness", "0");
        const char *drawMapKeys[] = {"Enabled", "Submits", "Resources", "Passes", "Markers", "Frame", "Summary"};
        for (const char *key : drawMapKeys)
            CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugDrawMap, key, "false");
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        ASSERT_NE(nullptr, Manager->m_2DRootFore);
        ASSERT_NE(nullptr, Manager->m_2DRootBack);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        VxImageDescEx format;
        VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State;
        Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0;
        Device->m_Settings.m_Rect = CKRECT{0, 0, 640, 360};
        Device->m_WindowSettings = Device->m_Settings;
        Device->m_ViewportData.ViewWidth = 640;
        Device->m_ViewportData.ViewHeight = 360;
        Device->m_RenderFlags = CK_RENDER_DOBACKTOFRONT;
        Device->BeginFrameErrorTracking();
    }

    void TearDown() override {
        if (Device) {
            Device->m_TargetTexture = nullptr;
            Device->m_RasterizerContext = nullptr;
            Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (Context)
            CKCloseContext(Context);
        CKRenderSettingsClearOverridesForTests();
        CKShutdown();
    }

    static void Setting(const char *key, const char *value) {
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, key, value);
    }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_NEAR(expected[row][column], actual[row][column], 1e-5f);
    }
    static void Rect(CK2dEntity *entity, float width, float height) {
        VxRect rect;
        entity->GetRect(rect);
        EXPECT_FLOAT_EQ(0, rect.left);
        EXPECT_FLOAT_EQ(0, rect.top);
        EXPECT_FLOAT_EQ(width, rect.right);
        EXPECT_FLOAT_EQ(height, rect.bottom);
    }
    RCKCamera *Camera() {
        RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
        if (!camera)
            return nullptr;
        camera->SetProjectionType(CK_PERSPECTIVEPROJECTION);
        camera->SetFov(PI / 2);
        camera->SetFrontPlane(2);
        camera->SetBackPlane(20);
        camera->SetAspectRatio(4, 3);
        Device->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(camera));
        return camera;
    }
};
}

TEST_F(RenderContextOptionsCompletionTest, ExplicitOptionsPacketReflectsManagerDefaultsAndCurrentConfiguration) {
    Manager->m_Antialias.Value = 4;
    Manager->m_DisableFilter.Value = 1;
    Manager->m_DisableMipmap.Value = 1;
    Setting("FXAA", "true");
    Setting("ForceAnisotropicFiltering", "true");
    Setting("RenderScale", "1.5");
    Setting("Sharpness", "0.375");
    Device->m_RasterizerDebugFlags = CKRST_DEBUG_WIREFRAME | CKRST_DEBUG_IFH;
    Device->ApplyRenderOptions();
    EXPECT_EQ(1, State.OptionWrites);
    EXPECT_EQ(4u, State.Options.MSAASamples);
    EXPECT_TRUE(State.Options.DisableTextureFiltering);
    EXPECT_TRUE(State.Options.DisableMipmaps);
    EXPECT_TRUE(State.Options.FXAA);
    EXPECT_TRUE(State.Options.ForceAnisotropicFiltering);
    EXPECT_FLOAT_EQ(1.5f, State.Options.RenderScale);
    EXPECT_FLOAT_EQ(.375f, State.Options.Sharpness);
    EXPECT_EQ(CKRST_DEBUG_WIREFRAME | CKRST_DEBUG_IFH, State.Options.DebugFlags);
}

TEST_F(RenderContextOptionsCompletionTest, DecimalWhitespaceAndScientificNotationReachTheOptionsConsumer) {
    Setting("RenderScale", "  1.25  ");
    Setting("Sharpness", " 4.5e-1 ");
    Device->ApplyRenderOptions();
    EXPECT_FLOAT_EQ(1.25f, State.Options.RenderScale);
    EXPECT_FLOAT_EQ(.45f, State.Options.Sharpness);
    Setting("RenderScale", "2e0");
    Setting("Sharpness", ".125");
    Device->ApplyRenderOptions();
    EXPECT_FLOAT_EQ(2, State.Options.RenderScale);
    EXPECT_FLOAT_EQ(.125f, State.Options.Sharpness);
}

TEST_F(RenderContextOptionsCompletionTest, EmptyAndNonnumericSettingsUseTheirIndependentFallbacks) {
    const char *values[] = {"", "no-number", "   "};
    for (const char *value : values) {
        Setting("RenderScale", value);
        Setting("Sharpness", value);
        Device->ApplyRenderOptions();
        EXPECT_FLOAT_EQ(1, State.Options.RenderScale);
        EXPECT_FLOAT_EQ(0, State.Options.Sharpness);
    }
}

TEST_F(RenderContextOptionsCompletionTest, NumericPrefixesAndFiniteOutOfRangeValuesRemainTheConsumersContract) {
    Setting("RenderScale", "1.75 trailing");
    Setting("Sharpness", ".25suffix");
    Device->ApplyRenderOptions();
    EXPECT_FLOAT_EQ(1.75f, State.Options.RenderScale);
    EXPECT_FLOAT_EQ(.25f, State.Options.Sharpness);
    Setting("RenderScale", "0.125");
    Setting("Sharpness", "2.5");
    Device->ApplyRenderOptions();
    EXPECT_FLOAT_EQ(.125f, State.Options.RenderScale);
    EXPECT_FLOAT_EQ(2.5f, State.Options.Sharpness);
    // The Context transports these values; the public rasterizer contract
    // assigns clamping to its options consumer.
}

TEST_F(RenderContextOptionsCompletionTest, DrawMapMasterAndSummaryComposeThePacketWithoutDroppingOtherDebugFlags) {
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugDrawMap, "Enabled", "true");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugDrawMap, "Summary", "true");
    Device->m_RasterizerDebugFlags = CKRST_DEBUG_WIREFRAME;
    Device->ApplyRenderOptions();
    EXPECT_EQ(CKRST_DEBUG_WIREFRAME | CKRST_DEBUG_DRAWMAP | CKRST_DEBUG_DRAWMAP_FRAME |
                  CKRST_DEBUG_DRAWMAP_SUMMARY, State.Options.DebugFlags);
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugDrawMap, "Enabled", "false");
    Device->ApplyRenderOptions();
    EXPECT_EQ(CKRST_DEBUG_WIREFRAME, State.Options.DebugFlags);
}

TEST_F(RenderContextOptionsCompletionTest, TimerLimitDoesNotInventVerticalSyncAndPreservesAnExplicitRequest) {
    Context->GetTimeManager()->SetFrameRateLimit(143);
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_LIMIT);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_FALSE(State.LastVSync);
    ASSERT_EQ(CK_OK, Device->BackToFront(static_cast<CK_RENDER_FLAGS>(CK_RENDER_DOBACKTOFRONT | CK_RENDER_WAITVBL)));
    EXPECT_TRUE(State.LastVSync);
    EXPECT_EQ(2, State.PresentCount);
    EXPECT_EQ(CK_RENDER_DOBACKTOFRONT, Device->GetCurrentRenderOptions());
}

TEST_F(RenderContextOptionsCompletionTest, SyncForcesPresentationWaitAndFreeModeRetainsThePortableOverride) {
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_SYNC);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_TRUE(State.LastVSync);
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
    ASSERT_EQ(CK_OK, Device->BackToFront(static_cast<CK_RENDER_FLAGS>(CK_RENDER_DOBACKTOFRONT | CK_RENDER_WAITVBL)));
    EXPECT_FALSE(State.LastVSync);
    EXPECT_EQ(2, State.PresentCount);
}

TEST_F(RenderContextOptionsCompletionTest, LimitUsesStoredFlagsWhenOnlyHighOptionsAreProvided) {
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_LIMIT);
    Device->SetCurrentRenderOptions(CK_RENDER_DOBACKTOFRONT | CK_RENDER_WAITVBL);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_PLAYERCONTEXT));
    EXPECT_TRUE(State.LastVSync);
    Device->SetCurrentRenderOptions(CK_RENDER_DOBACKTOFRONT);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_PLAYERCONTEXT));
    EXPECT_FALSE(State.LastVSync);
}

TEST_F(RenderContextOptionsCompletionTest, FullViewportPublishesActiveDimensionsAndPreservesTheDepthRange) {
    Device->m_ViewportData.ViewX = 17;
    Device->m_ViewportData.ViewY = 23;
    Device->m_ViewportData.ViewZMin = .25f;
    Device->m_ViewportData.ViewZMax = .75f;
    Device->SetFullViewport(&Device->m_ViewportData, 193, 107);
    EXPECT_EQ(1, State.ViewportWrites);
    EXPECT_EQ(0u, State.Viewport.ViewX);
    EXPECT_EQ(0u, State.Viewport.ViewY);
    EXPECT_EQ(193u, State.Viewport.ViewWidth);
    EXPECT_EQ(107u, State.Viewport.ViewHeight);
    EXPECT_FLOAT_EQ(.25f, State.Viewport.ViewZMin);
    EXPECT_FLOAT_EQ(.75f, State.Viewport.ViewZMax);
}

TEST_F(RenderContextOptionsCompletionTest, FullViewportOnAnIndependentPacketDoesNotReplaceTheActiveDeviceViewport) {
    CKViewportData local;
    local.ViewZMin = .125f;
    local.ViewZMax = .875f;
    Device->SetFullViewport(&local, 41, 29);
    EXPECT_EQ(0u, local.ViewX);
    EXPECT_EQ(0u, local.ViewY);
    EXPECT_EQ(41u, local.ViewWidth);
    EXPECT_EQ(29u, local.ViewHeight);
    EXPECT_FLOAT_EQ(.125f, local.ViewZMin);
    EXPECT_FLOAT_EQ(.875f, local.ViewZMax);
    EXPECT_EQ(0, State.ViewportWrites);
    EXPECT_EQ(640u, Device->m_ViewportData.ViewWidth);
    EXPECT_EQ(360u, Device->m_ViewportData.ViewHeight);
}

TEST_F(RenderContextOptionsCompletionTest, PrepareCamerasResolvesStoredAspectFlagsAndCopiesTheActualCameraPose) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(0, 0, -1);
    pose[2].Set(1, 0, 0);
    pose[3].Set(3, 5, 7);
    camera->SetWorldMatrix(pose);
    Device->SetCurrentRenderOptions(CK_RENDER_USECAMERARATIO);
    Device->PrepareCameras(CK_RENDER_PLAYERCONTEXT);
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), pose);
    EXPECT_EQ(80u, State.Viewport.ViewX);
    EXPECT_EQ(0u, State.Viewport.ViewY);
    EXPECT_EQ(480u, State.Viewport.ViewWidth);
    EXPECT_EQ(360u, State.Viewport.ViewHeight);
    VxMatrix projection;
    projection.Perspective(PI / 2, 4.0f / 3.0f, 2, 20);
    Matrix(State.Matrices[VXMATRIX_PROJECTION], projection);
    EXPECT_TRUE(camera->IsUpToDate());
}

TEST_F(RenderContextOptionsCompletionTest, PrepareCamerasHonorsExplicitOptionsAndReusesAnUnchangedCameraProjection) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    Device->SetCurrentRenderOptions(CK_RENDER_USECAMERARATIO);
    Device->m_ViewportData.ViewWidth = 320;
    Device->m_ViewportData.ViewHeight = 120;
    Device->PrepareCameras(CK_RENDER_CLEARZ);
    EXPECT_EQ(320u, State.Viewport.ViewWidth);
    EXPECT_EQ(120u, State.Viewport.ViewHeight);
    const int writes = State.ProjectionWrites;
    Device->PrepareCameras(CK_RENDER_CLEARZ);
    EXPECT_EQ(writes, State.ProjectionWrites);
    VxMatrix projection;
    projection.Perspective(PI / 2, 320.0f / 120.0f, 2, 20);
    Matrix(State.Matrices[VXMATRIX_PROJECTION], projection);
}

TEST_F(RenderContextOptionsCompletionTest, ForcedCameraSettingsCopyProjectionParametersAndResizeTheRealViewport) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    camera->SetProjectionType(CK_ORTHOGRAPHICPROJECTION);
    camera->SetOrthographicZoom(3);
    camera->SetFrontPlane(4);
    camera->SetBackPlane(40);
    camera->SetPosition3f(7, 11, 13);
    Device->SetCurrentRenderOptions(CK_RENDER_USECAMERARATIO);
    Device->ForceCameraSettingsUpdate();
    EXPECT_FALSE(Device->m_Perspective);
    EXPECT_FLOAT_EQ(3, Device->m_Zoom);
    EXPECT_FLOAT_EQ(4, Device->m_NearPlane);
    EXPECT_FLOAT_EQ(40, Device->m_FarPlane);
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), camera->GetWorldMatrix());
    EXPECT_EQ(480u, State.Viewport.ViewWidth);
    EXPECT_EQ(360u, State.Viewport.ViewHeight);
    VxMatrix projection;
    projection.Orthographic(3, 4.0f / 3.0f, 4, 40);
    Matrix(State.Matrices[VXMATRIX_PROJECTION], projection);
}

TEST_F(RenderContextOptionsCompletionTest, ForceWithoutAnAttachedCameraPreservesTheIndependentViewpointAndProjection) {
    Device->GetViewpoint()->SetPosition3f(5, 7, 11);
    Device->m_Fov = .8f;
    Device->m_NearPlane = 3;
    Device->m_FarPlane = 30;
    const VxMatrix before = Device->GetViewpoint()->GetWorldMatrix();
    Device->ForceCameraSettingsUpdate();
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), before);
    EXPECT_FLOAT_EQ(.8f, Device->m_Fov);
    EXPECT_FLOAT_EQ(3, Device->m_NearPlane);
    EXPECT_FLOAT_EQ(30, Device->m_FarPlane);
    EXPECT_EQ(0, State.ProjectionWrites);
    EXPECT_EQ(0, State.ViewportWrites);
}

TEST_F(RenderContextOptionsCompletionTest, CurrentTargetAndWindowDimensionsReachBothRealTwoDimensionalRoots) {
    RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    ASSERT_NE(nullptr, texture);
    ASSERT_TRUE(texture->CreateImage(32, 16, 32));
    ASSERT_TRUE(Device->SetRenderTarget(texture));
    Rect(Device->Get2dRoot(FALSE), 32, 16);
    Rect(Device->Get2dRoot(TRUE), 32, 16);
    ASSERT_TRUE(Device->SetRenderTarget(nullptr));
    Rect(Device->Get2dRoot(FALSE), 640, 360);
    Rect(Device->Get2dRoot(TRUE), 640, 360);
}

TEST_F(RenderContextOptionsCompletionTest, UnchangedRootDimensionsDoNotRelayoutDescendantsButARealResizeDoes) {
    LayoutObservationChild child(Context);
    child.SetParent(Device->Get2dRoot(FALSE));
    child.SetRect(VxRect(7, 11, 27, 31));
    Device->UpdateProjection(TRUE);
    child.RectWrites = 0;
    Device->UpdateProjection(TRUE);
    EXPECT_EQ(0, child.RectWrites);
    Device->m_Settings.m_Rect.right = 320;
    Device->m_Settings.m_Rect.bottom = 180;
    Device->m_ViewportData.ViewWidth = 320;
    Device->m_ViewportData.ViewHeight = 180;
    Device->UpdateProjection(TRUE);
    EXPECT_GT(child.RectWrites, 0);
    Rect(Device->Get2dRoot(FALSE), 320, 180);
    Rect(Device->Get2dRoot(TRUE), 320, 180);
    child.SetParent(nullptr);
}
