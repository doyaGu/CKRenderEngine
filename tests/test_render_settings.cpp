#include "CKRenderSettings.h"
#include "TestTriangleMultiset.h"
#include "VxMath.h"

#include <stdio.h>

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

int main() {
    TestFramework tests;
    tests.Run("CK2_3D root settings parse legacy options", &OverridesReadEveryLegacyRootOption);
    return tests.ExitCode();
}
