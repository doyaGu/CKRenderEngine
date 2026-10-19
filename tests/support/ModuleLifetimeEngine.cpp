// Build the real module entry points as a DLL against the fixed SDK.
#define CKRE_DYNAMIC_RENDER_ENGINE 1
#include "../../src/CK2_3D.cpp"
#include "CKRenderSettings.h"

extern "C" INSTANCE_HANDLE CKRETestModuleHandle() { return g_DllHandle; }
extern "C" int CKRETestProviderCount() { return g_RasterizersInfo.Size(); }
extern "C" const CKRasterizerInfo *CKRETestProvider(int index) { return &g_RasterizersInfo[index]; }
extern "C" void CKRETestRegisterProvider(const char *path) { RegisterRasterizer(path); }
extern "C" void CKRETestRegisterStaticProvider() {
    RegisterStaticRasterizer(CKNullRasterizerGetInfo, "test-static");
}
extern "C" void CKRETestReleaseProviders() { ReleaseRasterizers(); }
extern "C" void CKRETestEnumerateProviders() { EnumerateRasterizers(); }
extern "C" CKBOOL CKRETestEnumerationDone() { return g_EnumerationDone; }
extern "C" void CKRETestRegisterStaticInfo(CKRST_GETINFO getInfo, const char *name) {
    RegisterStaticRasterizer(getInfo, name);
}
extern "C" void CKRETestRegisterNullProvider() { RegisterNullRasterizer(); }
extern "C" void CKRETestModulePath(char *path, int size) {
    XString name = CKRenderEngineModulePath();
    snprintf(path, size, "%s", name.CStr());
}
extern "C" const char *CKRETestProviderMask(const char *name) {
    return CKRenderEngineRasterizerMask(name);
}
extern "C" CKERROR CKRETestExitInstance(CKContext *context) { return ExitInstanceFct(context); }
extern "C" void CKRETestTimingConfigure(int enabled, int warmup, int samples) {
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Enabled", enabled ? "1" : "0");
    char value[32];
    snprintf(value, sizeof(value), "%d", warmup);
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "WarmupFrames", value);
    snprintf(value, sizeof(value), "%d", samples);
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "SampleFrames", value);
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Output", "none");
    CKRenderFrameCostStatsResetForTests();
}
extern "C" void CKRETestTimingBeginFrame() { CKRenderFrameCostStatsBeginRenderFrame(0, 0, 0, 0); }
extern "C" void CKRETestTimingEndFrame() { CKRenderFrameCostStatsEndRenderFrame(); }
extern "C" CKBOOL CKRETestTimingInsideFrame() { return CKRenderFrameCostStatsIsCollecting(); }
extern "C" void CKRETestTimingSnapshot(CKRenderFrameCostStatsSnapshot *snapshot) {
    CKRenderFrameCostStatsCopySnapshot(snapshot);
}
