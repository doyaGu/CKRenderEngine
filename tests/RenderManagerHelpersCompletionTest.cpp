#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "CKAll.h"
#include "CKRasterizer.h"
#include "../src/CKRasterizer/CKRasterizerLib/CKRenderSettings.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKSprite.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
class ClassificationDriver : public CKRasterizerDriver {
public:
    CKBOOL DescriptionAvailable;
    ClassificationDriver(CKRasterizer *owner, CKDWORD index, CKBOOL hardware, CKBOOL available)
        : CKRasterizerDriver(owner, index, "classification observer", hardware), DescriptionAvailable(available) {}
    CKBOOL GetDesc(CKRasterizerDriverDesc *desc) const override {
        return DescriptionAvailable ? CKRasterizerDriver::GetDesc(desc) : FALSE;
    }
};
class ClassificationRasterizer : public CKRasterizer {
public:
    ClassificationRasterizer() {
        AddDriver(new ClassificationDriver(this, 0, FALSE, TRUE));
        AddDriver(new ClassificationDriver(this, 1, TRUE, TRUE));
        AddDriver(new ClassificationDriver(this, 2, TRUE, FALSE));
    }
};
CKRasterizer *StartClassification(WIN_HANDLE) { return new ClassificationRasterizer; }
void CloseClassification(CKRasterizer *rasterizer) { delete rasterizer; }

class RenderManagerHelpersCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr, *Other = nullptr;
    XClassArray<CKRasterizerInfo> Saved;
    static CKDWORD Option(const RCKRenderManager *manager, const char *key) {
        for (int i = 0; i < manager->m_Options.Size(); ++i)
            if (strcmp(manager->m_Options[i]->Key.CStr(), key) == 0)
                return manager->m_Options[i]->Value;
        ADD_FAILURE() << "Missing option " << key;
        return 0xffffffffu;
    }
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
        CKBuildClassHierarchyTable();
        CKRenderSettingsClearOverridesForTests();
        Saved = g_RasterizersInfo;
        g_RasterizersInfo.Clear();
        CKRasterizerInfo factory;
        factory.StartFct = StartClassification;
        factory.CloseFct = CloseClassification;
        factory.InterfaceRevision = CKRST_INTERFACE_REVISION;
        g_RasterizersInfo.PushBack(factory);
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Other) CKCloseContext(Other);
        if (Context) CKCloseContext(Context);
        g_RasterizersInfo = Saved;
        CKRenderSettingsClearOverridesForTests();
        CKShutdown();
    }
};

TEST_F(RenderManagerHelpersCompletionTest, RealIniValuesReachManagerOptionsAndFutureResourceDefaults) {
    RCKRenderManager *manager = new RCKRenderManager(Context);
    const char *keys[] = {"ForceLinearFog", "ForceSoftware", "DisableFilter", "Antialias",
                          "DisableMipmap", "DisableSpecular", "EnableScreenDump", "TextureCacheManagement"};
    const CKDWORD values[] = {1, 1, 1, 4, 1, 1, 1, 0};
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(values[i], Option(manager, keys[i])) << keys[i];
    RCKTexture texture(Context);
    RCKSprite sprite(Context);
    EXPECT_EQ(_16_RGB565, texture.GetDesiredVideoFormat());
    EXPECT_EQ(_DXT5, sprite.GetDesiredVideoFormat());
}

TEST_F(RenderManagerHelpersCompletionTest, MissingIniEntriesKeepTheConstructorsDefaults) {
    RCKRenderManager *manager = new RCKRenderManager(Context);
    EXPECT_EQ(1u, Option(manager, "UseIndexBuffers"));
    EXPECT_EQ(16u, Option(manager, "VertexCache"));
    EXPECT_EQ(1u, Option(manager, "SortTransparentObjects"));
}

TEST_F(RenderManagerHelpersCompletionTest, InvalidFormatsKeepDefaultsAndLaterSettingsDoNotMutateAnExistingManager) {
    RCKRenderManager *first = new RCKRenderManager(Context);
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "TextureVideoFormat", "invalid-format");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "SpriteVideoFormat", "invalid-format");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::Root, "Antialias", "2");
    ASSERT_EQ(CK_OK, CKCreateContext(&Other, nullptr, 0, 0));
    RCKRenderManager *second = new RCKRenderManager(Other);
    EXPECT_EQ(_32_ARGB8888, second->m_TextureVideoFormat.Value);
    EXPECT_EQ(_32_ARGB8888, second->m_SpriteVideoFormat.Value);
    EXPECT_EQ(2u, Option(second, "Antialias"));
    EXPECT_EQ(4u, Option(first, "Antialias"));
    EXPECT_EQ(_16_RGB565, first->m_TextureVideoFormat.Value);
}

TEST_F(RenderManagerHelpersCompletionTest, HardwareClassificationOrdersDriversAndTreatsUnavailableDescriptionsAsNonHardware) {
    RCKRenderManager *manager = new RCKRenderManager(Context);
    ASSERT_EQ(3, manager->GetRenderDriverCount());
    CKRasterizer *factory = manager->m_Rasterizers[0];
    EXPECT_EQ(factory->GetDriver(1), manager->GetDriver(0));
    EXPECT_EQ(factory->GetDriver(0), manager->GetDriver(1));
    EXPECT_EQ(factory->GetDriver(2), manager->GetDriver(2));
    EXPECT_TRUE(manager->m_Drivers[0].Hardware);
    EXPECT_FALSE(manager->m_Drivers[1].Hardware);
    EXPECT_FALSE(manager->m_Drivers[2].Hardware);
    EXPECT_EQ(1, manager->GetPreferredSoftwareDriver());
    EXPECT_EQ(1, manager->m_Rasterizers.Size());
}
} // namespace

int main(int argc, char **argv) {
    // This executable owns its input file and environment. Settings are read
    // through the production parser before any manager is constructed.
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("ballanced-render-settings-" + std::to_string(sizeof(void *)) + "-" + std::to_string(nonce) + ".ini");
    FILE *file = fopen(path.string().c_str(), "wb");
    if (!file) return 2;
    fputs("<CK2_3D>\nForceLinearFog=1\nForceSoftware=1\nDisableFilter=1\nAntialias=4\n"
          "DisableMipmap=1\nDisableSpecular=1\nEnableScreenDump=1\nTextureCacheManagement=0\n"
          "TextureVideoFormat=_16_RGB565\nSpriteVideoFormat=_DXT5\n</CK2_3D>\n", file);
    fclose(file);
    const char *existing = std::getenv("CKRE_SETTINGS_FILE");
    const std::string previous = existing ? existing : "";
#ifdef _WIN32
    const int environmentResult = _putenv_s("CKRE_SETTINGS_FILE", path.string().c_str());
#else
    const int environmentResult = setenv("CKRE_SETTINGS_FILE", path.string().c_str(), 1);
#endif
    if (environmentResult != 0) {
        std::filesystem::remove(path);
        return 2;
    }
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
#ifdef _WIN32
    _putenv_s("CKRE_SETTINGS_FILE", previous.c_str());
#else
    if (existing) setenv("CKRE_SETTINGS_FILE", previous.c_str(), 1);
    else unsetenv("CKRE_SETTINGS_FILE");
#endif
    std::filesystem::remove(path);
    return result;
}
