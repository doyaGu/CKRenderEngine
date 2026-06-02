#include "CKRenderSettings.h"
#include "TestTriangleMultiset.h"
#include "VxMath.h"

#include <stdio.h>
#include <string.h>

static void OverridesReadEveryLegacyRootOption() {
    CKRenderSettingsClearOverridesForTests();

    struct NumericCase {
        const char *name;
        CKDWORD value;
    };

    const NumericCase numericCases[] = {
        {"DisablePerspectiveCorrection", 1},
        {"ForceLinearFog", 1},
        {"ForceSoftware", 1},
        {"DisableFilter", 1},
        {"EnsureVertexShader", 1},
        {"UseIndexBuffers", 1},
        {"DisableDithering", 1},
        {"Antialias", 4},
        {"DisableMipmap", 1},
        {"DisableSpecular", 1},
        {"EnableScreenDump", 1},
        {"EnableDebugMode", 1},
        {"VertexCache", 24},
        {"SortTransparentObjects", 0},
        {"TextureCacheManagement", 0},
    };

    for (int i = 0; i < (int)(sizeof(numericCases) / sizeof(numericCases[0])); ++i) {
        char value[16];
        sprintf(value, "%lu", (unsigned long)numericCases[i].value);
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, numericCases[i].name, value);
        TestCheck(CKRenderSettingsGetDword(CKRenderSettingsSection::Root, numericCases[i].name, 999) == numericCases[i].value,
                  "numeric CK2_3D root option should round-trip through settings");
    }

    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "TextureVideoFormat", "_32_ARGB8888");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "SpriteVideoFormat", "_DXT5");

    TestCheck(CKRenderSettingsGetPixelFormat(CKRenderSettingsSection::Root, "TextureVideoFormat", UNKNOWN_PF) == _32_ARGB8888,
              "TextureVideoFormat should parse VX pixel format tokens");
    TestCheck(CKRenderSettingsGetPixelFormat(CKRenderSettingsSection::Root, "SpriteVideoFormat", UNKNOWN_PF) == _DXT5,
              "SpriteVideoFormat should parse VX pixel format tokens");

    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "TextureVideoFormat", "not-a-format");
    TestCheck(CKRenderSettingsGetPixelFormat(CKRenderSettingsSection::Root, "TextureVideoFormat", _16_ARGB1555) == _16_ARGB1555,
              "invalid pixel format settings should keep the fallback value");

    CKRenderSettingsClearOverridesForTests();
}

static void ModernDefaultsPreferFullQualityRenderPath() {
    CKRenderSettingsClearOverridesForTests();

    TestCheck(CKRenderSettingsGetDword(CKRenderSettingsSection::Root, "UseIndexBuffers", 0) == 1,
              "default CK2_3D settings should enable index buffers");
    TestCheck(CKRenderSettingsGetPixelFormat(CKRenderSettingsSection::Root, "TextureVideoFormat", UNKNOWN_PF) == _32_ARGB8888,
              "default CK2_3D settings should use 32-bit texture video format");
    TestCheck(CKRenderSettingsGetPixelFormat(CKRenderSettingsSection::Root, "SpriteVideoFormat", UNKNOWN_PF) == _32_ARGB8888,
              "default CK2_3D settings should use 32-bit sprite video format");
}

static void FfpRuntimeOptionsDoNotLiveUnderDebugStats() {
    CKRenderSettingsClearOverridesForTests();

    TestCheck(!CKRenderFFPSettings().GetBool("SortOpaqueObjects", true),
              "SortOpaqueObjects must default off until packet path is proven profitable");
    TestCheck(CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", false),
              "InstanceOpaqueObjects must default on for explicit opaque packet sorting");

    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFFPStats, "SortOpaqueObjects", "0");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFFPStats, "InstanceOpaqueObjects", "0");
    const CKRenderDiagnosticsConfig &diagnostics = CKRenderDiagnosticsSettings();

    TestCheck(!diagnostics.FFPStats.Any(),
              "Debug.FFPStats must remain pure diagnostics even if FFP runtime options are present");
    TestCheck(CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true),
              "Debug.FFPStats must not override FFP.InstanceOpaqueObjects");

    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "SortOpaqueObjects", "0");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "InstanceOpaqueObjects", "0");
    TestCheck(!CKRenderFFPSettings().GetBool("SortOpaqueObjects", true),
              "SortOpaqueObjects must be read from the FFP runtime section");
    TestCheck(!CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true),
              "InstanceOpaqueObjects must be read from the FFP runtime section");

    CKRenderSettingsClearOverridesForTests();
}

static void FrameCostStatsDefaultsAndFallbacks() {
    CKRenderSettingsClearOverridesForTests();

    const CKRenderDiagnosticsConfig &defaults = CKRenderDiagnosticsSettings();
    TestCheck(!defaults.FrameCostStats.Enabled,
              "Debug.FrameCostStats must default disabled");
    TestCheck(defaults.FrameCostStats.WarmupFrames == 120,
              "Debug.FrameCostStats WarmupFrames default must be 120");
    TestCheck(defaults.FrameCostStats.SampleFrames == 600,
              "Debug.FrameCostStats SampleFrames default must be 600");

    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Enabled", "1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "WarmupFrames", "-1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "SampleFrames", "0");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Output", "none");

    const CKRenderDiagnosticsConfig &diagnostics = CKRenderDiagnosticsSettings();
    TestCheck(diagnostics.FrameCostStats.Enabled,
              "FrameCostStats Enabled must be read from Debug.FrameCostStats");
    TestCheck(diagnostics.FrameCostStats.WarmupFrames == 120,
              "invalid FrameCostStats warmup must fall back to 120");
    TestCheck(diagnostics.FrameCostStats.SampleFrames == 600,
              "invalid FrameCostStats sample must fall back to 600");
    TestCheck(strcmp(diagnostics.FrameCostStats.Output, "none") == 0,
              "FrameCostStats Output must be read from Debug.FrameCostStats");

    CKRenderSettingsClearOverridesForTests();
}

int main() {
    TestFramework tests;
    tests.Run("CK2_3D root settings parse legacy options", &OverridesReadEveryLegacyRootOption);
    tests.Run("CK2_3D defaults prefer the full quality render path", &ModernDefaultsPreferFullQualityRenderPath);
    tests.Run("FFP runtime options do not live under Debug.FFPStats", &FfpRuntimeOptionsDoNotLiveUnderDebugStats);
    tests.Run("FrameCostStats defaults and fallbacks", &FrameCostStatsDefaultsAndFallbacks);
    return tests.ExitCode();
}
