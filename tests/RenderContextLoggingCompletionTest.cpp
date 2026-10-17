#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKMaterial.h"
#include "MaterialRenderObserver.h"
#include "../src/CKRasterizer/CKRasterizerLib/CKDebugLogger.h"
#include "../src/CKRasterizer/CKRasterizerLib/CKRenderSettings.h"
#include <cstdio>
#include <cstdint>
#include <string>

#if !CKRE_ENABLE_DEBUG_LOGGER || !CKRE_ENABLE_FRAME_DIAGNOSTICS
#error RenderContextLoggingCompletionTest requires the actual enabled Context and logger variant
#endif

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class LoggingBoundary : public MaterialRenderObserver {
public:
    int Clears = 0;
    CKBOOL ClearAccepted = TRUE, LastVSync = FALSE;
    CKBOOL Clear(CKDWORD, CKDWORD, float, CKDWORD, int, CKRECT *) override {
        ++Clears;
        return ClearAccepted;
    }
    CKBOOL BackToFront(CKBOOL vsync) override {
        ++PresentCount;
        LastVSync = vsync;
        return TRUE;
    }
};

size_t Occurrences(const std::string &text, const char *needle) {
    size_t count = 0, offset = 0;
    const size_t length = std::char_traits<char>::length(needle);
    while ((offset = text.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += length;
    }
    return count;
}

class RenderContextLoggingCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    LoggingBoundary Boundary;
    char Path[1024] = {};

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        CKRenderSettingsClearOverridesForTests();
        const char *keys[] = {"Enabled", "CameraAttach", "PresentSync", "RenderedSceneCamera"};
        for (const char *key : keys)
            CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameLog, key, "false");
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Debug, "Output", "false");
        CKDebugLogger::Instance().EnableOutput(false);
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
        Device->m_Settings.m_Rect = CKRECT{0, 0, 320, 240};
        Device->m_ViewportData.ViewWidth = 320;
        Device->m_ViewportData.ViewHeight = 240;
        Device->m_Active = TRUE;
        Device->BeginFrameErrorTracking();
        std::snprintf(Path, sizeof(Path), "%scontext-enabled-log-%llu.txt", ::testing::TempDir().c_str(),
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
        std::remove(Path);
        CKDebugLogger &logger = CKDebugLogger::Instance();
        logger.EnableDebuggerOutput(false);
        logger.EnableFileOutput(true);
        logger.SetLogFilePath(Path);
        logger.EnableOutput(true);
    }
    void TearDown() override {
        CKDebugLogger::Instance().EnableOutput(false);
        CKDebugLogger::Instance().EnableFileOutput(false);
        Device->m_RasterizerContext = nullptr;
        delete Device;
        CKCloseContext(Context);
        CKShutdown();
        CKRenderSettingsClearOverridesForTests();
        std::remove(Path);
    }
    void Setting(const char *name, bool value) {
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameLog, name, value ? "true" : "false");
    }
    std::string Read() {
        CKDebugLogger::Instance().Flush();
        FILE *file = std::fopen(Path, "r");
        if (!file) return std::string();
        std::string text;
        char bytes[4096];
        size_t count;
        while ((count = std::fread(bytes, 1, sizeof(bytes), file)) != 0)
            text.append(bytes, count);
        std::fclose(file);
        return text;
    }
};

TEST_F(RenderContextLoggingCompletionTest, FrameToggleReportsActualClearRenderSchedulingAndRejectedClear) {
    ASSERT_EQ(CK_OK, Device->Clear(CK_RENDER_CLEARBACK));
    EXPECT_TRUE(Read().empty());
    Setting("Enabled", true);
    ASSERT_EQ(CK_OK, Device->Clear(CK_RENDER_CLEARBACK));
    const std::string clear = Read();
    EXPECT_NE(std::string::npos, clear.find("[Clear] enter\n"));
    EXPECT_NE(std::string::npos, clear.find("[Clear] getting background material\n"));
    EXPECT_NE(std::string::npos, clear.find("[Clear] rasterizer Clear\n"));
    EXPECT_NE(std::string::npos, clear.find("[Clear] done\n"));
    ASSERT_EQ(CK_OK, Device->Render(static_cast<CK_RENDER_FLAGS>(
        CK_RENDER_CLEARBACK | CK_RENDER_SKIPDRAWSCENE | CK_RENDER_DOBACKTOFRONT)));
    const std::string render = Read().substr(clear.size());
    EXPECT_NE(std::string::npos, render.find("[Render] about to Clear\n"));
    EXPECT_NE(std::string::npos, render.find("[Render] about to DrawScene\n"));
    EXPECT_NE(std::string::npos, render.find("[DrawScene] enter\n"));
    EXPECT_NE(std::string::npos, render.find("[Render] DrawScene done\n"));
    EXPECT_EQ(1, Boundary.PresentCount);
    const size_t successfulLength = Read().size();
    Boundary.ClearAccepted = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->Clear(CK_RENDER_CLEARBACK));
    const std::string rejected = Read().substr(successfulLength);
    EXPECT_NE(std::string::npos, rejected.find("[Clear] rasterizer Clear\n"));
    EXPECT_EQ(std::string::npos, rejected.find("[Clear] done\n"));
    Setting("Enabled", false);
    Boundary.ClearAccepted = TRUE;
    const std::string beforeDisabled = Read();
    ASSERT_EQ(CK_OK, Device->Clear(CK_RENDER_CLEARBACK));
    EXPECT_EQ(beforeDisabled, Read());
    EXPECT_EQ(5, Boundary.Clears);
}

TEST_F(RenderContextLoggingCompletionTest, PresentDiagnosticsReportActualPolicyFlagsAndStopAtSixtyFourPerSite) {
    CKTimeManager *time = Context->GetTimeManager();
    time->SetFrameRateLimit(30);
    time->ChangeLimitOptions(CK_FRAMERATE_LIMIT);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_TRUE(Read().empty());
    Setting("PresentSync", true);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_FALSE(Boundary.LastVSync);
    std::string text = Read();
    EXPECT_NE(std::string::npos, text.find("frameRateMode=0x40 frameRateLimit=30.000"));
    EXPECT_NE(std::string::npos, text.find("waitVbl=0"));
    EXPECT_NE(std::string::npos, text.find("[PresentSync] BackToFront "));
    time->ChangeLimitOptions(CK_FRAMERATE_SYNC);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_TRUE(Boundary.LastVSync);
    text = Read().substr(text.size());
    EXPECT_NE(std::string::npos, text.find("frameRateMode=0x10 frameRateLimit=30.000"));
    EXPECT_NE(std::string::npos, text.find("waitVbl=1"));
    time->ChangeLimitOptions(CK_FRAMERATE_FREE);
    for (int i = 0; i < 73; ++i)
        ASSERT_EQ(CK_OK, Device->BackToFront(static_cast<CK_RENDER_FLAGS>(CK_RENDER_DOBACKTOFRONT | CK_RENDER_WAITVBL)));
    EXPECT_FALSE(Boundary.LastVSync);
    text = Read();
    EXPECT_EQ(64u, Occurrences(text, "[PresentSync] BackToFront "));
    EXPECT_EQ(64u, Occurrences(text, "[PresentSync] limitOptions="));
    EXPECT_EQ(76, Boundary.PresentCount);
    Setting("PresentSync", false);
    ASSERT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_EQ(text, Read());
    EXPECT_EQ(77, Boundary.PresentCount);
}

TEST_F(RenderContextLoggingCompletionTest, CameraDiagnosticsUseRealCameraFieldsAndStopAtSixteenAttachments) {
    RCKCamera *camera = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA, (CKSTRING)"logging camera"));
    ASSERT_NE(nullptr, camera);
    camera->SetFov(.75f); camera->SetFrontPlane(2.5f); camera->SetBackPlane(250);
    camera->SetAspectRatio(16, 9); camera->SetPosition3f(3, 5, 7);
    Device->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(camera));
    EXPECT_TRUE(Read().empty());
    Setting("CameraAttach", true);
    for (int i = 0; i < 20; ++i)
        Device->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(camera));
    const std::string text = Read();
    EXPECT_EQ(16u, Occurrences(text, "AttachViewpointToCamera #"));
    EXPECT_NE(std::string::npos, text.find("AttachViewpointToCamera #0:"));
    EXPECT_NE(std::string::npos, text.find("AttachViewpointToCamera #15:"));
    EXPECT_EQ(std::string::npos, text.find("AttachViewpointToCamera #16:"));
    EXPECT_NE(std::string::npos, text.find("name=logging camera"));
    EXPECT_NE(std::string::npos, text.find("fov=0.750 near=2.500 far=250.000 aspect=16:9"));
    EXPECT_NE(std::string::npos, text.find("pos=(3.000 5.000 7.000)"));
    EXPECT_EQ(reinterpret_cast<CKCamera *>(camera), Device->GetAttachedCamera());
    EXPECT_FLOAT_EQ(7, Device->GetViewpoint()->GetWorldMatrix()[3][2]);
    Device->AttachViewpointToCamera(nullptr);
    EXPECT_EQ(text, Read());
}

TEST_F(RenderContextLoggingCompletionTest, GlobalLoggerOutputGateSuppressesEnabledContextLogsWithoutSuppressingClear) {
    Setting("Enabled", true);
    CKDebugLogger::Instance().EnableOutput(false);
    ASSERT_EQ(CK_OK, Device->Clear(CK_RENDER_CLEARBACK));
    EXPECT_TRUE(Read().empty());
    EXPECT_EQ(1, Boundary.Clears);
    CKDebugLogger::Instance().EnableOutput(true);
    ASSERT_EQ(CK_OK, Device->Clear(CK_RENDER_CLEARBACK));
    EXPECT_NE(std::string::npos, Read().find("[Clear] done\n"));
    EXPECT_EQ(2, Boundary.Clears);
}
} // namespace
