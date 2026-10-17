#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKTargetCamera.h"
#include "RCKLight.h"
#include "RCKTargetLight.h"
#include "RCKMaterial.h"
#include "../src/CKRasterizer/CKRasterizerLib/CKRenderSettings.h"

namespace {
class CameraSubmissionBoundary : public MaterialRenderObserver {
public:
    int ViewportWrites = 0;
    int ProjectionWrites = 0;
    CKBOOL SetOptions(const CKRasterizerOptions *) override { return TRUE; }
    CKBOOL SetViewport(const CKViewportData *viewport) override {
        ++ViewportWrites;
        return MaterialRenderObserver::SetViewport(viewport);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        if (type == VXMATRIX_PROJECTION)
            ++ProjectionWrites;
        return MaterialRenderObserver::SetTransformMatrix(type, matrix);
    }
};

class RenderedSceneCameraTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CameraSubmissionBoundary Boundary;
    CKRasterizerDriver Driver;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetCamera, CKCID_CAMERA);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetLight, CKCID_LIGHT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        CKRenderSettingsClearOverridesForTests();
        const char *keys[] = {"Enabled", "Submits", "Resources", "Passes", "Markers", "Frame", "Summary"};
        for (const char *key : keys)
            CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugDrawMap, key, "false");
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary, 0));
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
        Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0;
        Device->m_Settings.m_Rect = CKRECT{0, 0, 640, 360};
        Device->m_WindowSettings = Device->m_Settings;
        Device->m_RenderFlags = CK_RENDER_USECAMERARATIO;
        SetViewport(0, 0, 640, 360);
    }

    void TearDown() override {
        if (Device) {
            Device->m_RasterizerContext = nullptr;
            Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (Context)
            CKCloseContext(Context);
        CKRenderSettingsClearOverridesForTests();
        CKShutdown();
    }

    CKRenderedScene &Scene() { return *Device->m_RenderedScene; }
    static CKCamera *API(RCKCamera *camera) { return reinterpret_cast<CKCamera *>(camera); }
    RCKCamera *Camera(CKBOOL attach = TRUE) {
        RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
        EXPECT_NE(nullptr, camera);
        if (!camera)
            return nullptr;
        camera->SetProjectionType(CK_PERSPECTIVEPROJECTION);
        camera->SetFov(PI / 2);
        camera->SetFrontPlane(2);
        camera->SetBackPlane(20);
        camera->SetAspectRatio(4, 3);
        if (attach)
            Device->AttachViewpointToCamera(API(camera));
        return camera;
    }
    void SetViewport(int x, int y, int width, int height) {
        Device->m_ViewportData.ViewX = x;
        Device->m_ViewportData.ViewY = y;
        Device->m_ViewportData.ViewWidth = width;
        Device->m_ViewportData.ViewHeight = height;
        Device->m_ViewportData.ViewZMin = .125f;
        Device->m_ViewportData.ViewZMax = .875f;
        ASSERT_TRUE(Boundary.SetViewport(&Device->m_ViewportData));
    }
    void Viewport(int x, int y, int width, int height) {
        VxRect rect;
        Device->GetViewRect(rect);
        EXPECT_FLOAT_EQ(static_cast<float>(x), rect.left);
        EXPECT_FLOAT_EQ(static_cast<float>(y), rect.top);
        EXPECT_FLOAT_EQ(static_cast<float>(x + width), rect.right);
        EXPECT_FLOAT_EQ(static_cast<float>(y + height), rect.bottom);
        EXPECT_EQ(static_cast<CKDWORD>(x), Boundary.Viewport.ViewX);
        EXPECT_EQ(static_cast<CKDWORD>(y), Boundary.Viewport.ViewY);
        EXPECT_EQ(static_cast<CKDWORD>(width), Boundary.Viewport.ViewWidth);
        EXPECT_EQ(static_cast<CKDWORD>(height), Boundary.Viewport.ViewHeight);
    }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_NEAR(expected[row][column], actual[row][column], 1e-5f)
                    << "row " << row << " column " << column;
    }
    static void Direction(RCK3dEntity *entity, float x, float y, float z) {
        VxVector direction;
        entity->GetOrientation(&direction, nullptr, nullptr);
        EXPECT_NEAR(x, direction.x, 1e-5f);
        EXPECT_NEAR(y, direction.y, 1e-5f);
        EXPECT_NEAR(z, direction.z, 1e-5f);
    }
};
}

TEST_F(RenderedSceneCameraTest, DirtyPerspectiveCameraCopiesPoseLensAndClippingPlanes) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(0, 0, -1);
    pose[2].Set(1, 0, 0);
    pose[3].Set(3, 5, 7);
    camera->SetWorldMatrix(pose);
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    EXPECT_EQ(API(camera), Device->GetAttachedCamera());
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), pose);
    EXPECT_TRUE(Device->m_Perspective);
    EXPECT_FLOAT_EQ(PI / 2, Device->m_Fov);
    EXPECT_FLOAT_EQ(2, Device->m_NearPlane);
    EXPECT_FLOAT_EQ(20, Device->m_FarPlane);
    Viewport(80, 0, 480, 360);
    VxMatrix expected;
    expected.Perspective(PI / 2, 4.0f / 3.0f, 2, 20);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], expected);
    EXPECT_TRUE(camera->IsUpToDate());
}

TEST_F(RenderedSceneCameraTest, CleanAttachedCameraRefreshesPoseWithoutRebuildingProjection) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    const int writes = Boundary.ProjectionWrites;
    const VxMatrix projection = Boundary.Matrices[VXMATRIX_PROJECTION];
    camera->SetPosition3f(11, 13, 17);
    camera->ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), camera->GetWorldMatrix());
    EXPECT_EQ(writes, Boundary.ProjectionWrites);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], projection);
}

TEST_F(RenderedSceneCameraTest, DirtyOrthographicCameraUsesZoomAndViewportAspect) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    camera->SetProjectionType(CK_ORTHOGRAPHICPROJECTION);
    camera->SetOrthographicZoom(3);
    camera->SetFrontPlane(4);
    camera->SetBackPlane(40);
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    EXPECT_FALSE(Device->m_Perspective);
    EXPECT_FLOAT_EQ(3, Device->m_Zoom);
    VxMatrix expected;
    expected.Orthographic(3, 4.0f / 3.0f, 4, 40);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], expected);
    EXPECT_TRUE(camera->IsUpToDate());
}

TEST_F(RenderedSceneCameraTest, WithoutAttachedCameraKeepsFreeViewpointAndUpdatesItsOwnProjection) {
    Device->GetViewpoint()->SetPosition3f(5, 7, 11);
    const VxMatrix pose = Device->GetViewpoint()->GetWorldMatrix();
    Device->m_Fov = .8f;
    Device->m_NearPlane = 3;
    Device->m_FarPlane = 30;
    Device->m_ProjectionUpdated = FALSE;
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    EXPECT_EQ(nullptr, Device->GetAttachedCamera());
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), pose);
    VxMatrix expected;
    expected.Perspective(.8f, 640.0f / 360.0f, 3, 30);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], expected);
    const int writes = Boundary.ProjectionWrites;
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    EXPECT_EQ(writes, Boundary.ProjectionWrites);
}

TEST_F(RenderedSceneCameraTest, CameraRatioFitsWideAndTallWindowsWithCenteredIntegerViewport) {
    ASSERT_NE(nullptr, Camera());
    Scene().UpdateViewportSize(FALSE, CK_RENDER_USECAMERARATIO);
    Viewport(80, 0, 480, 360);
    Device->m_Settings.m_Rect = CKRECT{31, 47, 300, 500};
    Scene().UpdateViewportSize(FALSE, CK_RENDER_USECAMERARATIO);
    Viewport(0, 137, 300, 225);
    VxMatrix expected;
    expected.Perspective(Device->m_Fov, 4.0f / 3.0f, Device->m_NearPlane, Device->m_FarPlane);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], expected);
}

TEST_F(RenderedSceneCameraTest, StoredRatioDisabledPreventsExplicitRatioFromChangingTheCurrentViewport) {
    ASSERT_NE(nullptr, Camera());
    Device->m_RenderFlags = CK_RENDER_CLEARZ;
    SetViewport(7, 9, 320, 200);
    Device->m_ProjectionUpdated = TRUE;
    const int writes = Boundary.ViewportWrites;
    Scene().UpdateViewportSize(FALSE, CK_RENDER_USECAMERARATIO);
    Viewport(7, 9, 320, 200);
    EXPECT_EQ(writes, Boundary.ViewportWrites);
}

TEST_F(RenderedSceneCameraTest, StoredRatioEnabledAllowsExplicitFullViewportToRestoreTheWindow) {
    ASSERT_NE(nullptr, Camera());
    SetViewport(80, 0, 480, 360);
    Scene().UpdateViewportSize(FALSE, CK_RENDER_CLEARZ);
    Viewport(0, 0, 640, 360);
}

TEST_F(RenderedSceneCameraTest, HighBitOnlyFlagsResolveTheStoredCameraRatio) {
    ASSERT_NE(nullptr, Camera());
    Scene().UpdateViewportSize(FALSE, CK_RENDER_PLAYERCONTEXT);
    Viewport(80, 0, 480, 360);
}

TEST_F(RenderedSceneCameraTest, IgnoreAspectPreservesManualViewportAndForcedProjection) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    camera->SetFlags(camera->GetFlags() | CK_3DENTITY_CAMERAIGNOREASPECT);
    SetViewport(17, 19, 450, 150);
    const int writes = Boundary.ProjectionWrites;
    Scene().UpdateViewportSize(FALSE, CK_RENDER_USECAMERARATIO);
    Viewport(17, 19, 450, 150);
    EXPECT_EQ(writes, Boundary.ProjectionWrites);
    Scene().UpdateViewportSize(TRUE, CK_RENDER_USECAMERARATIO);
    Viewport(17, 19, 450, 150);
    EXPECT_EQ(writes + 1, Boundary.ProjectionWrites);
}

TEST_F(RenderedSceneCameraTest, NoAttachedCameraForceRefreshesWithoutApplyingTheDefaultRatio) {
    SetViewport(13, 17, 310, 190);
    const int writes = Boundary.ProjectionWrites;
    Scene().UpdateViewportSize(FALSE, CK_RENDER_USECAMERARATIO);
    EXPECT_EQ(writes, Boundary.ProjectionWrites);
    Scene().UpdateViewportSize(TRUE, CK_RENDER_USECAMERARATIO);
    EXPECT_EQ(writes + 1, Boundary.ProjectionWrites);
    Viewport(13, 17, 310, 190);
}

TEST_F(RenderedSceneCameraTest, ForcedCameraSettingsRefreshChangedLensImmediatelyAtUnchangedViewport) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    const int writes = Boundary.ProjectionWrites;
    camera->SetProjectionType(CK_ORTHOGRAPHICPROJECTION);
    camera->SetOrthographicZoom(7);
    camera->SetFrontPlane(4);
    camera->SetBackPlane(90);
    camera->SetPosition3f(13, 17, 19);
    Scene().ForceCameraSettingsUpdate();
    EXPECT_EQ(writes + 1, Boundary.ProjectionWrites);
    Viewport(80, 0, 480, 360);
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), camera->GetWorldMatrix());
    VxMatrix expected;
    expected.Orthographic(7, 4.0f / 3.0f, 4, 90);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], expected);
}

TEST_F(RenderedSceneCameraTest, ForceWithoutAttachedCameraLeavesViewpointAndProjectionUntouched) {
    Device->GetViewpoint()->SetPosition3f(17, 19, 23);
    const VxMatrix pose = Device->GetViewpoint()->GetWorldMatrix();
    const VxMatrix projection = Boundary.Matrices[VXMATRIX_PROJECTION];
    const int writes = Boundary.ProjectionWrites;
    Scene().ForceCameraSettingsUpdate();
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), pose);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], projection);
    EXPECT_EQ(writes, Boundary.ProjectionWrites);
}

TEST_F(RenderedSceneCameraTest, RestoringCameraViewportKeepsDepthRangeAndDoesNotRebuildProjection) {
    ASSERT_NE(nullptr, Camera());
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    const VxMatrix projection = Boundary.Matrices[VXMATRIX_PROJECTION];
    const int writes = Boundary.ProjectionWrites;
    SetViewport(0, 0, 640, 360);
    Scene().ResizeViewport(VxRect(80.75f, .75f, 560.75f, 360.75f));
    Viewport(80, 0, 480, 360);
    EXPECT_FLOAT_EQ(.125f, Boundary.Viewport.ViewZMin);
    EXPECT_FLOAT_EQ(.875f, Boundary.Viewport.ViewZMax);
    EXPECT_EQ(writes, Boundary.ProjectionWrites);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], projection);
}

TEST_F(RenderedSceneCameraTest, TargetCamerasAndNonPointLightsTrackTargetsBeforeCopyingTheViewpoint) {
    RCKTargetCamera *camera = static_cast<RCKTargetCamera *>(Context->CreateObject(CKCID_TARGETCAMERA));
    RCKTargetLight *spot = static_cast<RCKTargetLight *>(Context->CreateObject(CKCID_TARGETLIGHT));
    RCKTargetLight *point = static_cast<RCKTargetLight *>(Context->CreateObject(CKCID_TARGETLIGHT));
    RCK3dEntity *target = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, camera); ASSERT_NE(nullptr, spot); ASSERT_NE(nullptr, point); ASSERT_NE(nullptr, target);
    target->SetPosition3f(10, 0, 0);
    camera->SetTarget(reinterpret_cast<CK3dEntity *>(target));
    spot->SetTarget(reinterpret_cast<CK3dEntity *>(target));
    point->SetTarget(reinterpret_cast<CK3dEntity *>(target));
    spot->SetType(VX_LIGHTSPOT);
    point->SetType(VX_LIGHTPOINT);
    Device->AddObject(reinterpret_cast<CKRenderObject *>(camera));
    Device->AddObject(reinterpret_cast<CKRenderObject *>(spot));
    Device->AddObject(reinterpret_cast<CKRenderObject *>(point));
    ASSERT_TRUE(Device->IsObjectAttached(reinterpret_cast<CKRenderObject *>(camera)));
    ASSERT_TRUE(Device->IsObjectAttached(reinterpret_cast<CKRenderObject *>(spot)));
    ASSERT_TRUE(Device->IsObjectAttached(reinterpret_cast<CKRenderObject *>(point)));
    Device->AttachViewpointToCamera(API(camera));
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    Direction(camera, 1, 0, 0);
    Direction(spot, 1, 0, 0);
    Direction(point, 0, 0, 1);
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), camera->GetWorldMatrix());
    target->SetPosition3f(0, 0, 10);
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    Direction(camera, 0, 0, 1);
    Direction(spot, 0, 0, 1);
    camera->SetTarget(nullptr);
    spot->SetTarget(nullptr);
    point->SetTarget(nullptr);
}

TEST_F(RenderedSceneCameraTest, ExplicitDetachRemainsFreeWhenTheSceneHasAStartingCamera) {
    RCKCamera *camera = Camera();
    ASSERT_NE(nullptr, camera);
    CKLevel *level = static_cast<CKLevel *>(Context->CreateObject(CKCID_LEVEL));
    ASSERT_NE(nullptr, level);
    CKScene *scene = level->GetLevelScene();
    ASSERT_NE(nullptr, scene);
    Context->SetCurrentLevel(level);
    scene->AddObjectToScene(reinterpret_cast<CKSceneObject *>(camera), FALSE);
    scene->SetStartingCamera(API(camera));
    ASSERT_EQ(CK_OK, level->LaunchScene(scene));
    ASSERT_EQ(scene, Context->GetCurrentScene());
    ASSERT_EQ(API(camera), scene->GetStartingCamera());
    Device->DetachViewpointFromCamera();
    Device->GetViewpoint()->SetPosition3f(23, 29, 31);
    const VxMatrix freePose = Device->GetViewpoint()->GetWorldMatrix();
    camera->SetPosition3f(37, 41, 43);
    Scene().PrepareCameras(CK_RENDER_USECAMERARATIO);
    EXPECT_EQ(nullptr, Device->GetAttachedCamera());
    Matrix(Device->GetViewpoint()->GetWorldMatrix(), freePose);
    Context->SetCurrentLevel(nullptr);
}
