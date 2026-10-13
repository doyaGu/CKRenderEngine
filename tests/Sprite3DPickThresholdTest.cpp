#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"

extern CKBOOL PreciseTexturePick(CKMaterial *material, float u, float v);
extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Sprite3DPickThresholdTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp()); CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCKMaterial *Material() { return static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); }
    RCKTexture *Texture(RCKMaterial &material, int width, int height, int threshold) {
        RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
        EXPECT_NE(nullptr, texture); if (!texture) return nullptr;
        EXPECT_TRUE(texture->CreateImage(width, height, 32)); texture->SetPickThreshold(threshold);
        material.SetTexture0(texture); return texture;
    }
    static CKMaterial *API(RCKMaterial &material) { return reinterpret_cast<CKMaterial *>(&material); }
};
}

TEST_F(Sprite3DPickThresholdTest, ZeroTextureThresholdIgnoresMaterialRenderAlphaReference) {
    RCKMaterial *material = Material(); ASSERT_NE(nullptr, material); RCKTexture *texture = Texture(*material, 1, 1, 0); ASSERT_NE(nullptr, texture);
    ASSERT_TRUE(texture->SetPixel(0, 0, 0x00ffffff)); EXPECT_EQ(0, texture->GetPickThreshold());
    material->SetAlphaRef(128); EXPECT_TRUE(PreciseTexturePick(API(*material), .5f, .5f));
    material->SetAlphaRef(255); EXPECT_TRUE(PreciseTexturePick(API(*material), .5f, .5f));
}
TEST_F(Sprite3DPickThresholdTest, NonzeroTextureThresholdWorksWithZeroMaterialAlphaReference) {
    RCKMaterial *material = Material(); ASSERT_NE(nullptr, material); RCKTexture *texture = Texture(*material, 3, 1, 128); ASSERT_NE(nullptr, texture);
    ASSERT_TRUE(texture->SetPixel(0, 0, 0x00ffffff)); ASSERT_TRUE(texture->SetPixel(1, 0, 0xffffffff)); ASSERT_TRUE(texture->SetPixel(2, 0, 0xffffffff));
    material->SetAlphaRef(0); material->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
    EXPECT_FALSE(PreciseTexturePick(API(*material), .1f, .5f)); EXPECT_TRUE(PreciseTexturePick(API(*material), .75f, .5f));
}
TEST_F(Sprite3DPickThresholdTest, BitmapAlphaAtThresholdIsAcceptedAndOneBelowIsRejected) {
    RCKMaterial *material = Material(); ASSERT_NE(nullptr, material); RCKTexture *texture = Texture(*material, 2, 1, 128); ASSERT_NE(nullptr, texture);
    ASSERT_TRUE(texture->SetPixel(0, 0, 0x7fffffff)); ASSERT_TRUE(texture->SetPixel(1, 0, 0x80ffffff));
    material->SetAlphaRef(0); material->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
    EXPECT_FALSE(PreciseTexturePick(API(*material), 0, .5f)); EXPECT_TRUE(PreciseTexturePick(API(*material), 1, .5f));
}
TEST_F(Sprite3DPickThresholdTest, MissingMaterialTextureAndZeroThresholdEmptyTextureAcceptPicking) {
    EXPECT_TRUE(PreciseTexturePick(nullptr, .5f, .5f)); RCKMaterial *material = Material(); ASSERT_NE(nullptr, material);
    material->SetAlphaRef(255); EXPECT_TRUE(PreciseTexturePick(API(*material), .5f, .5f));
    RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE)); ASSERT_NE(nullptr, texture);
    EXPECT_EQ(-1, texture->GetWidth()); EXPECT_EQ(-1, texture->GetHeight()); texture->SetPickThreshold(0); material->SetTexture0(texture);
    EXPECT_TRUE(PreciseTexturePick(API(*material), .5f, .5f));
}
TEST_F(Sprite3DPickThresholdTest, WrapSamplesActualBitmapForPositiveAndNegativeUVCoordinates) {
    RCKMaterial *material = Material(); ASSERT_NE(nullptr, material); RCKTexture *texture = Texture(*material, 4, 4, 128); ASSERT_NE(nullptr, texture);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) ASSERT_TRUE(texture->SetPixel(x, y, x && y ? 0xffffffff : 0x00ffffff));
    material->SetAlphaRef(0); material->SetTextureAddressMode(VXTEXTURE_ADDRESSWRAP);
    EXPECT_FALSE(PreciseTexturePick(API(*material), 1.1f, 1.1f)); EXPECT_TRUE(PreciseTexturePick(API(*material), -.1f, -.1f));
    EXPECT_FALSE(PreciseTexturePick(API(*material), 2.1f, -.1f)); EXPECT_FALSE(PreciseTexturePick(API(*material), -.1f, 2.1f));
}
TEST_F(Sprite3DPickThresholdTest, MirrorUsesNativeTruncationAndOddTileParityOnActualPixels) {
    RCKMaterial *material = Material(); ASSERT_NE(nullptr, material); RCKTexture *texture = Texture(*material, 5, 1, 128); ASSERT_NE(nullptr, texture);
    for (int x = 0; x < 5; ++x) ASSERT_TRUE(texture->SetPixel(x, 0, x >= 2 ? 0xffffffff : 0x00ffffff));
    material->SetAlphaRef(0); material->SetTextureAddressMode(VXTEXTURE_ADDRESSMIRROR);
    EXPECT_FALSE(PreciseTexturePick(API(*material), 1.75f, .5f)); EXPECT_TRUE(PreciseTexturePick(API(*material), 1.25f, .5f));
    EXPECT_FALSE(PreciseTexturePick(API(*material), -1.25f, .5f)); EXPECT_TRUE(PreciseTexturePick(API(*material), -.25f, .5f));
}
TEST_F(Sprite3DPickThresholdTest, ClampAndBorderSampleRealEdgePixelsForBothAxes) {
    RCKMaterial *material = Material(); ASSERT_NE(nullptr, material); RCKTexture *texture = Texture(*material, 5, 5, 128); ASSERT_NE(nullptr, texture);
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) ASSERT_TRUE(texture->SetPixel(x, y, x >= 2 && y >= 2 ? 0xffffffff : 0x00ffffff));
    material->SetAlphaRef(0); const VXTEXTURE_ADDRESSMODE modes[] = {VXTEXTURE_ADDRESSCLAMP, VXTEXTURE_ADDRESSBORDER};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(i); material->SetTextureAddressMode(modes[i]);
        EXPECT_TRUE(PreciseTexturePick(API(*material), 2, 2)); EXPECT_FALSE(PreciseTexturePick(API(*material), -2, 2));
        EXPECT_FALSE(PreciseTexturePick(API(*material), 2, -2));
    }
}
