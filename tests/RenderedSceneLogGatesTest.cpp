#include <gtest/gtest.h>
#include "../src/CKRenderedScene.cpp"
#include "RCKMaterial.h"
#include "MaterialRenderObserver.h"
#include <cstdio>
#include <cstdint>
#include <string>

#if !CKRE_ENABLE_DEBUG_LOGGER
#error Both gate variants require the real logger enabled
#endif

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class LogGatesBoundary : public MaterialRenderObserver {
public:
    int Begins = 0, Ends = 0, Overlays = 0;
    CKBOOL BeginScene() override { ++Begins; return TRUE; }
    CKBOOL EndScene() override { ++Ends; return TRUE; }
    CKBOOL BeginOverlayPhase() override { ++Overlays; return TRUE; }
};

class RenderedSceneLogGatesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    LogGatesBoundary Boundary;
    CKRasterizerDriver Driver;
    char Path[1024] = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        CKRenderSettingsClearOverridesForTests();
        const char *keys[] = {"Enabled", "CameraAttach", "PresentSync", "RenderedSceneCamera"};
        for (const char *key : keys) Setting(key, false);
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Debug, "Output", "false");
        CKDebugLogger::Instance().EnableOutput(false);
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary, 0));
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0;
        Device->m_Settings.m_Rect = CKRECT{0, 0, 320, 240};
        Device->m_ViewportData.ViewWidth = 320; Device->m_ViewportData.ViewHeight = 240;
        Device->m_NearPlane = 1; Device->m_FarPlane = 100; Device->m_Fov = PI / 2;
        std::snprintf(Path, sizeof(Path), "%sscene-gates-%d-%llu.txt", ::testing::TempDir().c_str(),
            CKRE_ENABLE_FRAME_DIAGNOSTICS,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
        std::remove(Path);
        CKDebugLogger &logger = CKDebugLogger::Instance();
        logger.EnableDebuggerOutput(false); logger.EnableFileOutput(true);
        logger.SetLogFilePath(Path); logger.EnableOutput(true);
    }
    void TearDown() override {
        CKDebugLogger::Instance().EnableOutput(false);
        CKDebugLogger::Instance().EnableFileOutput(false);
        if (Device) {
            Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown(); CKRenderSettingsClearOverridesForTests(); std::remove(Path);
    }
    static void Setting(const char *key, bool value) {
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameLog, key, value ? "true" : "false");
    }
    std::string Read() {
        CKDebugLogger::Instance().Flush();
        FILE *file = std::fopen(Path, "rb");
        if (!file) return std::string();
        std::string text; char bytes[4096]; size_t count;
        while ((count = std::fread(bytes, 1, sizeof(bytes), file)) != 0) text.append(bytes, count);
        std::fclose(file); return text;
    }
    CKERROR Draw(CKBOOL skip3d) {
        return Device->m_RenderedScene->Draw(static_cast<CK_RENDER_FLAGS>(
            CK_RENDER_DONOTUPDATEEXTENTS | (skip3d ? CK_RENDER_SKIP3D : 0)));
    }
};

TEST_F(RenderedSceneLogGatesTest, HelpersFollowAllFourIndependentSettingCombinations) {
    for (int frame = 0; frame < 2; ++frame) {
        for (int camera = 0; camera < 2; ++camera) {
            SCOPED_TRACE(frame);
            SCOPED_TRACE(camera);
            Setting("Enabled", frame != 0); Setting("RenderedSceneCamera", camera != 0);
            EXPECT_EQ(CKRE_ENABLE_FRAME_DIAGNOSTICS != 0 && frame != 0, RenderedSceneFrameLogEnabled());
            EXPECT_EQ(CKRE_ENABLE_FRAME_DIAGNOSTICS != 0 && camera != 0, RenderedSceneCameraLogEnabled());
        }
    }
    Setting("Enabled", false); Setting("RenderedSceneCamera", false);
    EXPECT_FALSE(RenderedSceneFrameLogEnabled()); EXPECT_FALSE(RenderedSceneCameraLogEnabled());
}

TEST_F(RenderedSceneLogGatesTest, UnrelatedSettingsAndLoggerOutputDoNotReplaceEitherGate) {
    Setting("CameraAttach", true); Setting("PresentSync", true);
    EXPECT_FALSE(RenderedSceneFrameLogEnabled()); EXPECT_FALSE(RenderedSceneCameraLogEnabled());
    Setting("Enabled", true); Setting("RenderedSceneCamera", true);
    CKDebugLogger::Instance().EnableOutput(false);
    EXPECT_EQ(CKRE_ENABLE_FRAME_DIAGNOSTICS != 0, RenderedSceneFrameLogEnabled());
    EXPECT_EQ(CKRE_ENABLE_FRAME_DIAGNOSTICS != 0, RenderedSceneCameraLogEnabled());
    Setting("Enabled", false);
    EXPECT_FALSE(RenderedSceneFrameLogEnabled());
    EXPECT_EQ(CKRE_ENABLE_FRAME_DIAGNOSTICS != 0, RenderedSceneCameraLogEnabled());
}

TEST_F(RenderedSceneLogGatesTest, FrameOutputTracksLiveSettingsAndLoggerWithoutChangingDrawSuccess) {
    const bool enabled[] = {false, true, false};
    for (int i = 0; i < 3; ++i) {
        Setting("Enabled", enabled[i]); const std::string before = Read();
        ASSERT_EQ(CK_OK, Draw(TRUE));
        const std::string added = Read().substr(before.size());
        const bool expected = CKRE_ENABLE_FRAME_DIAGNOSTICS != 0 && enabled[i];
        EXPECT_EQ(expected, added.find("[RenderedScene] Draw enter") != std::string::npos);
        EXPECT_EQ(expected, added.find("[RenderedScene] Draw - calling BeginScene") != std::string::npos);
        EXPECT_EQ(std::string::npos, added.find("Attached camera #"));
        if (!expected) EXPECT_TRUE(added.empty());
    }
    Setting("Enabled", true); CKDebugLogger::Instance().EnableOutput(false);
    const std::string beforeMuted = Read();
    ASSERT_EQ(CK_OK, Draw(TRUE)); EXPECT_EQ(beforeMuted, Read());
    EXPECT_EQ(4, Boundary.Begins); EXPECT_EQ(4, Boundary.Ends); EXPECT_EQ(4, Boundary.Overlays);
}

TEST_F(RenderedSceneLogGatesTest, CameraOutputIsIndependentAndContainsTheActualAttachedPose) {
    RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA, "gate camera"));
    ASSERT_NE(nullptr, camera);
    camera->SetPosition3f(3, 4, 5); camera->SetFrontPlane(1); camera->SetBackPlane(100);
    camera->SetFov(PI / 2); camera->SetAspectRatio(4, 3);
    Device->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(camera));
    const bool enabled[] = {false, true, false};
    for (int i = 0; i < 3; ++i) {
        Setting("RenderedSceneCamera", enabled[i]); const std::string before = Read();
        ASSERT_EQ(CK_OK, Draw(FALSE));
        const std::string added = Read().substr(before.size());
        const bool expected = CKRE_ENABLE_FRAME_DIAGNOSTICS != 0 && enabled[i];
        EXPECT_EQ(expected, added.find("Attached camera #") != std::string::npos);
        EXPECT_EQ(std::string::npos, added.find("Draw enter"));
        if (expected) {
            EXPECT_NE(std::string::npos, added.find("name=gate camera"));
            EXPECT_NE(std::string::npos, added.find("root row3=(3.000 4.000 5.000)"));
            EXPECT_NE(std::string::npos, added.find("cam row3=(3.000 4.000 5.000)"));
        } else EXPECT_TRUE(added.empty());
        EXPECT_FLOAT_EQ(3, Boundary.Matrices[VXMATRIX_VIEW][3].x * -1);
        EXPECT_FLOAT_EQ(4, Boundary.Matrices[VXMATRIX_VIEW][3].y * -1);
        EXPECT_FLOAT_EQ(5, Boundary.Matrices[VXMATRIX_VIEW][3].z * -1);
    }
    EXPECT_EQ(3, Boundary.Begins); EXPECT_EQ(3, Boundary.Ends); EXPECT_EQ(3, Boundary.Overlays);
}

TEST_F(RenderedSceneLogGatesTest, Skip3dSuppressesCameraOutputWhileLeavingFrameLoggingAvailable) {
    Setting("Enabled", true); Setting("RenderedSceneCamera", true);
    ASSERT_EQ(CK_OK, Draw(TRUE)); const std::string text = Read();
    EXPECT_EQ(CKRE_ENABLE_FRAME_DIAGNOSTICS != 0, text.find("Draw enter") != std::string::npos);
    EXPECT_EQ(std::string::npos, text.find("camera="));
    EXPECT_EQ(std::string::npos, text.find("Attached camera #"));
    EXPECT_EQ(1, Boundary.Begins); EXPECT_EQ(1, Boundary.Ends); EXPECT_EQ(1, Boundary.Overlays);
}
} // namespace
