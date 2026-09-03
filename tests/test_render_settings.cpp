#include "CKRenderSettings.h"
#include "CKFixedFunctionPipeline.h"
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
        {"ForceAnisotropicFiltering", 1},
        {"FXAA", 1},
        {"DisableSpecular", 1},
        {"EnableScreenDump", 1},
        {"EnableDebugMode", 1},
        {"VertexCache", 24},
        {"SortTransparentObjects", 0},
        {"TextureCacheManagement", 0},
    };

    for (int i = 0; i < (int)(sizeof(numericCases) / sizeof(numericCases[0])); ++i) {
        char value[16];
        snprintf(value, sizeof(value), "%lu", (unsigned long)numericCases[i].value);
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
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "RenderScale", "1.5");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "Sharpness", "0.25");
    XString renderScale;
    TestCheck(CKRenderSettingsGetString(CKRenderSettingsSection::Root, "RenderScale", renderScale) &&
                  strcmp(renderScale.CStr(), "1.5") == 0,
              "RenderScale should be readable as a root string setting");
    XString sharpness;
    TestCheck(CKRenderSettingsGetString(CKRenderSettingsSection::Root, "Sharpness", sharpness) &&
                  strcmp(sharpness.CStr(), "0.25") == 0,
              "Sharpness should be readable as a root string setting");

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

static void ForceAnisotropicFilteringUpdatesFfpSamplers() {
    CKRenderSettingsClearOverridesForTests();

    CKFixedFunctionPipeline ffp;
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEARMIPLINEAR);
    ffp.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);

    ffp.SetRenderOptions(FALSE, FALSE, FALSE);
    CKSamplerDesc classic = ffp.BuildSamplerDesc(0);
    TestCheck(classic.MinFilter == CKRST_FILTER_LINEAR &&
                  classic.MagFilter == CKRST_FILTER_LINEAR &&
                  classic.MipFilter == CKRST_FILTER_LINEAR,
              "classic sampler settings should preserve stage filter state");

    ffp.SetRenderOptions(FALSE, FALSE, TRUE);
    CKSamplerDesc forced = ffp.BuildSamplerDesc(0);
    TestCheck(forced.MinFilter == CKRST_FILTER_ANISOTROPIC &&
                  forced.MagFilter == CKRST_FILTER_ANISOTROPIC &&
                  forced.MipFilter == CKRST_FILTER_ANISOTROPIC,
              "forced anisotropic filtering should update min/mag/mip filters");

    ffp.SetRenderOptions(TRUE, FALSE, TRUE);
    CKSamplerDesc filterDisabled = ffp.BuildSamplerDesc(0);
    TestCheck(filterDisabled.MinFilter == CKRST_FILTER_NEAREST &&
                  filterDisabled.MagFilter == CKRST_FILTER_NEAREST,
              "DisableFilter must take priority over forced anisotropic filtering");

    ffp.SetRenderOptions(FALSE, TRUE, TRUE);
    CKSamplerDesc mipDisabled = ffp.BuildSamplerDesc(0);
    TestCheck(mipDisabled.MipFilter == CKRST_FILTER_NONE,
              "DisableMipmap must prevent mip sampling even when anisotropic filtering is forced");
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
    tests.Run("forced anisotropic filtering updates FFP samplers", &ForceAnisotropicFilteringUpdatesFfpSamplers);
    tests.Run("FrameCostStats defaults and fallbacks", &FrameCostStatsDefaultsAndFallbacks);
    return tests.ExitCode();
}
