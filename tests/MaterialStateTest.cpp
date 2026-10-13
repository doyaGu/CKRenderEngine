#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern CKBOOL g_UpdateTransparency;

namespace {
class MaterialStateProbe : public RCKMaterial {
public:
    static void Flags(RCKMaterial &material, CKDWORD value) { auto member = &MaterialStateProbe::m_Flags; material.*member = value; }
    static CKDWORD Flags(RCKMaterial &material) { auto member = &MaterialStateProbe::m_Flags; return material.*member; }
};
class MaterialStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); g_UpdateTransparency = FALSE;
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); g_UpdateTransparency = FALSE; }
};
int MaterialCallback(CKRenderContext *context, CKMaterial *material, void *argument) {
    int *calls = static_cast<int *>(argument); ++*calls;
    return context == nullptr && material != nullptr ? 73 : 0;
}
}

TEST_F(MaterialStateTest, TextureBlendValueIsRetainedWithoutChangingFilters) {
    RCKMaterial material(Context); material.SetTextureBlendMode(VXTEXTUREBLEND_ADD);
    EXPECT_EQ(VXTEXTUREBLEND_ADD, material.GetTextureBlendMode());
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, material.GetTextureMinMode()); EXPECT_EQ(VXTEXTUREFILTER_LINEAR, material.GetTextureMagMode());
    material.SetTextureBlendMode(static_cast<VXTEXTURE_BLENDMODE>(0x12345678));
    EXPECT_EQ(0x12345678, static_cast<int>(material.GetTextureBlendMode()));
}
TEST_F(MaterialStateTest, MinificationAndMagnificationModesRemainIndependent) {
    RCKMaterial material(Context); material.SetTextureMinMode(VXTEXTUREFILTER_NEAREST);
    EXPECT_EQ(VXTEXTUREFILTER_NEAREST, material.GetTextureMinMode()); EXPECT_EQ(VXTEXTUREFILTER_LINEAR, material.GetTextureMagMode());
    material.SetTextureMagMode(VXTEXTUREFILTER_NEAREST); material.SetTextureMinMode(VXTEXTUREFILTER_LINEAR);
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, material.GetTextureMinMode()); EXPECT_EQ(VXTEXTUREFILTER_NEAREST, material.GetTextureMagMode());
    material.SetTextureMinMode(static_cast<VXTEXTURE_FILTERMODE>(25)); material.SetTextureMagMode(static_cast<VXTEXTURE_FILTERMODE>(26));
    EXPECT_EQ(25, static_cast<int>(material.GetTextureMinMode())); EXPECT_EQ(26, static_cast<int>(material.GetTextureMagMode()));
}
TEST_F(MaterialStateTest, AddressAndBorderRetainExactValuesWithoutChangingBlend) {
    RCKMaterial material(Context); material.SetTextureAddressMode(VXTEXTURE_ADDRESSBORDER);
    material.SetTextureBorderColor(0x12345678); EXPECT_EQ(VXTEXTURE_ADDRESSBORDER, material.GetTextureAddressMode());
    EXPECT_EQ(0x12345678u, material.GetTextureBorderColor()); EXPECT_EQ(VXTEXTUREBLEND_MODULATEALPHA, material.GetTextureBlendMode());
    material.SetTextureAddressMode(static_cast<VXTEXTURE_ADDRESSMODE>(99));
    EXPECT_EQ(99, static_cast<int>(material.GetTextureAddressMode()));
}
TEST_F(MaterialStateTest, SourceAndDestinationBlendRetentionAndDirtyNotificationAreIndependent) {
    RCKMaterial material(Context); material.SetSourceBlend(VXBLEND_SRCALPHA);
    EXPECT_EQ(VXBLEND_SRCALPHA, material.GetSourceBlend()); EXPECT_EQ(VXBLEND_ZERO, material.GetDestBlend());
    EXPECT_FALSE(g_UpdateTransparency); material.SetDestBlend(VXBLEND_INVSRCALPHA);
    EXPECT_TRUE(g_UpdateTransparency); EXPECT_EQ(VXBLEND_INVSRCALPHA, material.GetDestBlend());
    g_UpdateTransparency = FALSE; material.SetDestBlend(VXBLEND_INVSRCALPHA); EXPECT_FALSE(g_UpdateTransparency);
    material.SetSourceBlend(static_cast<VXBLEND_MODE>(40)); EXPECT_EQ(40, static_cast<int>(material.GetSourceBlend()));
}
TEST_F(MaterialStateTest, TwoSidedUsesNonzeroTruthAndPreservesOtherLowAndHighFlags) {
    RCKMaterial material(Context); MaterialStateProbe::Flags(material, 0xA55A5AFE);
    material.SetTwoSided(-8); EXPECT_TRUE(material.IsTwoSided()); EXPECT_EQ(0xA55A5AFFu, MaterialStateProbe::Flags(material));
    material.SetTwoSided(0); EXPECT_FALSE(material.IsTwoSided()); EXPECT_EQ(0xA55A5AFEu, MaterialStateProbe::Flags(material));
    EXPECT_FALSE(g_UpdateTransparency);
}
TEST_F(MaterialStateTest, ZWritePreservesFlagsAndNotifiesOnlyActualClassificationChanges) {
    RCKMaterial material(Context); MaterialStateProbe::Flags(material, 0xABCD1230);
    material.EnableZWrite(7); EXPECT_TRUE(material.ZWriteEnabled()); EXPECT_EQ(0xABCD1232u, MaterialStateProbe::Flags(material));
    EXPECT_TRUE(g_UpdateTransparency); g_UpdateTransparency = FALSE; material.EnableZWrite(-1); EXPECT_FALSE(g_UpdateTransparency);
    material.EnableZWrite(FALSE); EXPECT_FALSE(material.ZWriteEnabled()); EXPECT_EQ(0xABCD1230u, MaterialStateProbe::Flags(material));
    EXPECT_TRUE(g_UpdateTransparency);
}
TEST_F(MaterialStateTest, AlphaBlendPreservesFlagsAndDoesNotNotifyRepeatedTruthyRequests) {
    RCKMaterial material(Context); MaterialStateProbe::Flags(material, 0xABCD1230);
    material.EnableAlphaBlend(-1); EXPECT_TRUE(material.AlphaBlendEnabled()); EXPECT_EQ(0xABCD1238u, MaterialStateProbe::Flags(material));
    EXPECT_TRUE(g_UpdateTransparency); g_UpdateTransparency = FALSE; material.EnableAlphaBlend(23); EXPECT_FALSE(g_UpdateTransparency);
    material.EnableAlphaBlend(FALSE); EXPECT_FALSE(material.AlphaBlendEnabled()); EXPECT_TRUE(g_UpdateTransparency);
    EXPECT_EQ(0xABCD1230u, MaterialStateProbe::Flags(material));
}
TEST_F(MaterialStateTest, AlphaTestPreservesFlagsAndNotifiesOnlyTransitions) {
    RCKMaterial material(Context); MaterialStateProbe::Flags(material, 0xABCD1208);
    material.EnableAlphaTest(19); EXPECT_TRUE(material.AlphaTestEnabled()); EXPECT_EQ(0xABCD1218u, MaterialStateProbe::Flags(material));
    EXPECT_TRUE(g_UpdateTransparency); g_UpdateTransparency = FALSE; material.EnableAlphaTest(TRUE); EXPECT_FALSE(g_UpdateTransparency);
    material.EnableAlphaTest(FALSE); EXPECT_FALSE(material.AlphaTestEnabled()); EXPECT_TRUE(g_UpdateTransparency);
    EXPECT_EQ(0xABCD1208u, MaterialStateProbe::Flags(material));
}
TEST_F(MaterialStateTest, PerspectiveCorrectionUsesNonzeroTruthAndPreservesClassificationBits) {
    RCKMaterial material(Context); MaterialStateProbe::Flags(material, 0xABCD123B);
    material.EnablePerspectiveCorrection(55); EXPECT_TRUE(material.PerspectiveCorrectionEnabled());
    EXPECT_EQ(0xABCD123Fu, MaterialStateProbe::Flags(material)); material.EnablePerspectiveCorrection(FALSE);
    EXPECT_FALSE(material.PerspectiveCorrectionEnabled()); EXPECT_EQ(0xABCD123Bu, MaterialStateProbe::Flags(material)); EXPECT_FALSE(g_UpdateTransparency);
}
TEST_F(MaterialStateTest, ComparisonFieldsMaskFiveBitsAndRemainIndependent) {
    RCKMaterial material(Context); MaterialStateProbe::Flags(material, 0xFEDCBA98);
    material.SetZFunc(VXCMP_GREATER); EXPECT_EQ(VXCMP_GREATER, material.GetZFunc());
    EXPECT_EQ((0xFEDCBA98u & ~0x7C000u) | (CKDWORD(VXCMP_GREATER) << 14), MaterialStateProbe::Flags(material));
    const CKDWORD afterZ = MaterialStateProbe::Flags(material); material.SetAlphaFunc(VXCMP_NOTEQUAL);
    EXPECT_EQ(VXCMP_NOTEQUAL, material.GetAlphaFunc()); EXPECT_EQ(VXCMP_GREATER, material.GetZFunc());
    EXPECT_EQ((afterZ & ~0xF80000u) | (CKDWORD(VXCMP_NOTEQUAL) << 19), MaterialStateProbe::Flags(material));
    material.SetZFunc(static_cast<VXCMPFUNC>(0x123)); material.SetAlphaFunc(static_cast<VXCMPFUNC>(0x127));
    EXPECT_EQ(3, static_cast<int>(material.GetZFunc())); EXPECT_EQ(7, static_cast<int>(material.GetAlphaFunc()));
    EXPECT_FALSE(g_UpdateTransparency);
}
TEST_F(MaterialStateTest, AlphaReferenceRetainsFullByteWithoutChangingComparisonOrFlags) {
    RCKMaterial material(Context); const CKDWORD flags = MaterialStateProbe::Flags(material);
    material.SetAlphaRef(255); EXPECT_EQ(255, material.GetAlphaRef());
    material.SetAlphaRef(0); EXPECT_EQ(0, material.GetAlphaRef()); EXPECT_EQ(flags, MaterialStateProbe::Flags(material));
    EXPECT_FALSE(g_UpdateTransparency);
}
TEST_F(MaterialStateTest, FillAndShadeValuesRemainIndependentAndDoNotDirtyTransparency) {
    RCKMaterial material(Context); material.SetFillMode(VXFILL_WIREFRAME); material.SetShadeMode(VXSHADE_FLAT);
    EXPECT_EQ(VXFILL_WIREFRAME, material.GetFillMode()); EXPECT_EQ(VXSHADE_FLAT, material.GetShadeMode());
    material.SetFillMode(static_cast<VXFILL_MODE>(99)); material.SetShadeMode(static_cast<VXSHADE_MODE>(98));
    EXPECT_EQ(99, static_cast<int>(material.GetFillMode())); EXPECT_EQ(98, static_cast<int>(material.GetShadeMode())); EXPECT_FALSE(g_UpdateTransparency);
}
TEST_F(MaterialStateTest, TransparencyClassificationSeparatesBlendsCutoutsAndNonDepthWritingAlphaTest) {
    RCKMaterial material(Context); EXPECT_FALSE(material.IsAlphaTransparent());
    material.EnableAlphaBlend(); EXPECT_FALSE(material.IsAlphaTransparent());
    material.SetDestBlend(VXBLEND_INVSRCALPHA); EXPECT_TRUE(material.IsAlphaTransparent());
    material.EnableAlphaTest(); EXPECT_FALSE(material.IsAlphaTransparent());
    material.EnableZWrite(FALSE); EXPECT_TRUE(material.IsAlphaTransparent());
    material.EnableAlphaBlend(FALSE); EXPECT_FALSE(material.IsAlphaTransparent());
    material.EnableAlphaBlend(); material.SetDestBlend(VXBLEND_ZERO); EXPECT_FALSE(material.IsAlphaTransparent());
}
TEST_F(MaterialStateTest, CallbackRetrievalPreservesCallableAndArgumentIncludingNullOutput) {
    RCKMaterial material(Context); void *argument = &material; EXPECT_EQ(nullptr, material.GetCallback(&argument)); EXPECT_EQ(nullptr, argument);
    int calls = 0; material.SetCallback(&MaterialCallback, &calls);
    CK_MATERIALCALLBACK callback = material.GetCallback(&argument); ASSERT_EQ(&MaterialCallback, callback); ASSERT_EQ(&calls, argument);
    EXPECT_EQ(callback, material.GetCallback(nullptr)); EXPECT_EQ(73, callback(nullptr, reinterpret_cast<CKMaterial *>(&material), argument)); EXPECT_EQ(1, calls);
    material.SetCallback(nullptr, &calls); EXPECT_EQ(nullptr, material.GetCallback(&argument)); EXPECT_EQ(&calls, argument);
}
TEST_F(MaterialStateTest, ChannelPatchSavesCompleteStatePreservesUnrelatedFlagsAndRestoresWithoutSortingSideEffects) {
    RCKMaterial material(Context); material.SetSourceBlend(VXBLEND_SRCALPHA); material.SetDestBlend(VXBLEND_ZERO);
    MaterialStateProbe::Flags(material, 0xD5ABCDB7); g_UpdateTransparency = FALSE;
    VXBLEND_MODE savedSource, savedDest; CKDWORD savedFlags;
    material.PatchForChannelRender(VXBLEND_ONE, VXBLEND_ONE, savedSource, savedDest, savedFlags);
    EXPECT_EQ(VXBLEND_SRCALPHA, savedSource); EXPECT_EQ(VXBLEND_ZERO, savedDest); EXPECT_EQ(0xD5ABCDB7u, savedFlags);
    EXPECT_EQ(VXBLEND_ONE, material.GetSourceBlend()); EXPECT_EQ(VXBLEND_ONE, material.GetDestBlend());
    EXPECT_TRUE(material.AlphaBlendEnabled()); EXPECT_FALSE(material.ZWriteEnabled());
    EXPECT_EQ(0xD5ABCDBDu, MaterialStateProbe::Flags(material)); EXPECT_FALSE(g_UpdateTransparency);
    material.RestoreAfterChannelRender(savedSource, savedDest, savedFlags);
    EXPECT_EQ(VXBLEND_SRCALPHA, material.GetSourceBlend()); EXPECT_EQ(VXBLEND_ZERO, material.GetDestBlend());
    EXPECT_EQ(0xD5ABCDB7u, MaterialStateProbe::Flags(material)); EXPECT_FALSE(g_UpdateTransparency);
}
TEST_F(MaterialStateTest, NestedChannelPatchesRestoreEachSavedStateInOrder) {
    RCKMaterial material(Context); const CKDWORD initial = MaterialStateProbe::Flags(material);
    VXBLEND_MODE firstSource, firstDest, secondSource, secondDest; CKDWORD firstFlags, secondFlags;
    material.PatchForChannelRender(VXBLEND_SRCALPHA, VXBLEND_ONE, firstSource, firstDest, firstFlags);
    material.PatchForChannelRender(VXBLEND_ONE, VXBLEND_INVSRCALPHA, secondSource, secondDest, secondFlags);
    material.RestoreAfterChannelRender(secondSource, secondDest, secondFlags);
    EXPECT_EQ(VXBLEND_SRCALPHA, material.GetSourceBlend()); EXPECT_EQ(VXBLEND_ONE, material.GetDestBlend());
    EXPECT_TRUE(material.AlphaBlendEnabled()); EXPECT_FALSE(material.ZWriteEnabled());
    material.RestoreAfterChannelRender(firstSource, firstDest, firstFlags);
    EXPECT_EQ(VXBLEND_ONE, material.GetSourceBlend()); EXPECT_EQ(VXBLEND_ZERO, material.GetDestBlend()); EXPECT_EQ(initial, MaterialStateProbe::Flags(material));
}
