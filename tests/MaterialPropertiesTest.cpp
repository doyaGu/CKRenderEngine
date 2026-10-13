#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class MaterialProbe : public RCKMaterial {
public:
    static const VxColor &ActiveSpecular(RCKMaterial &material) {
        auto member = &MaterialProbe::m_MaterialData;
        return (material.*member).Specular;
    }
    static CKSprite3DBatch &SeedBatch(RCKMaterial &material) {
        auto member = &MaterialProbe::m_Sprite3DBatch;
        if (!(material.*member)) material.*member = new CKSprite3DBatch();
        CKSprite3DBatch &batch = *(material.*member);
        batch.m_Vertices.Resize(4); batch.m_Indices.Resize(6);
        batch.m_VertexCount = 4; batch.m_IndexCount = 6; batch.m_Flags = 9;
        return batch;
    }
};
class MaterialPropertiesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Color(const VxColor &actual, const VxColor &expected) {
        EXPECT_FLOAT_EQ(expected.r, actual.r); EXPECT_FLOAT_EQ(expected.g, actual.g);
        EXPECT_FLOAT_EQ(expected.b, actual.b); EXPECT_FLOAT_EQ(expected.a, actual.a);
    }
};
}

TEST_F(MaterialPropertiesTest, AmbientCopiesAllChannelsWithoutClampingOrChangingDiffuse) {
    RCKMaterial material(Context); const VxColor diffuse = material.GetDiffuse();
    VxColor value(-1.5f, 2.25f, 0.125f, 0.375f); material.SetAmbient(value);
    value.r = 99; Color(material.GetAmbient(), VxColor(-1.5f, 2.25f, 0.125f, 0.375f));
    Color(material.GetDiffuse(), diffuse);
}
TEST_F(MaterialPropertiesTest, DiffuseReferenceRemainsLiveAndAssignmentCopiesInput) {
    RCKMaterial material(Context); const VxColor &reference = material.GetDiffuse();
    VxColor value(2.0f, -3.0f, 4.0f, .25f); material.SetDiffuse(value); value.a = 1;
    EXPECT_EQ(&reference, &material.GetDiffuse()); Color(reference, VxColor(2.0f, -3.0f, 4.0f, .25f));
}
TEST_F(MaterialPropertiesTest, EmissiveRetainsAlphaAndDoesNotChangeAmbientOrPower) {
    RCKMaterial material(Context); const VxColor ambient = material.GetAmbient();
    material.SetPower(4); material.SetEmissive(VxColor(.1f, .2f, .3f, .4f));
    Color(material.GetEmissive(), VxColor(.1f, .2f, .3f, .4f));
    Color(material.GetAmbient(), ambient); EXPECT_FLOAT_EQ(4, material.GetPower());
}
TEST_F(MaterialPropertiesTest, DisabledSpecularRetainsUserColorButHasZeroActiveRGBA) {
    RCKMaterial material(Context); material.SetSpecular(VxColor(.2f, .4f, .6f, .8f));
    Color(material.GetSpecular(), VxColor(.2f, .4f, .6f, .8f));
    Color(MaterialProbe::ActiveSpecular(material), VxColor(0.0f, 0.0f, 0.0f, 0.0f));
}
TEST_F(MaterialPropertiesTest, PowerThresholdEqualityDisablesActiveSpecular) {
    RCKMaterial material(Context); material.SetSpecular(VxColor(.2f, .4f, .6f, .8f));
    material.SetPower(.05f); EXPECT_FLOAT_EQ(.05f, material.GetPower());
    Color(MaterialProbe::ActiveSpecular(material), VxColor(0.0f, 0.0f, 0.0f, 0.0f));
}
TEST_F(MaterialPropertiesTest, AdjacentPowerValuesOnEitherSideOfThresholdSwitchOnlyActiveColor) {
    RCKMaterial material(Context); const VxColor cached(.2f, .4f, .6f, .8f);
    material.SetSpecular(cached); material.SetPower(std::nextafter(.05f, 1.0f));
    Color(MaterialProbe::ActiveSpecular(material), cached);
    material.SetPower(std::nextafter(.05f, 0.0f));
    Color(MaterialProbe::ActiveSpecular(material), VxColor(0.0f, 0.0f, 0.0f, 0.0f)); Color(material.GetSpecular(), cached);
}
TEST_F(MaterialPropertiesTest, SettingSpecularAtExactThresholdKeepsCacheAndDisablesActiveRGBA) {
    RCKMaterial material(Context); material.SetPower(.05f);
    material.SetSpecular(VxColor(.75f, .5f, .25f, .125f));
    Color(material.GetSpecular(), VxColor(.75f, .5f, .25f, .125f));
    Color(MaterialProbe::ActiveSpecular(material), VxColor(0.0f, 0.0f, 0.0f, 0.0f));
}
TEST_F(MaterialPropertiesTest, ChangingActiveSpecularTransfersAllFourChannels) {
    RCKMaterial material(Context); material.SetPower(10);
    material.SetSpecular(VxColor(2.0f, -1.0f, .25f, .375f));
    Color(MaterialProbe::ActiveSpecular(material), VxColor(2.0f, -1.0f, .25f, .375f));
    EXPECT_FLOAT_EQ(10, material.GetPower());
}
TEST_F(MaterialPropertiesTest, NegativeAndUnorderedPowerDisableWithoutClampingStoredValue) {
    RCKMaterial material(Context); material.SetPower(-7);
    EXPECT_FLOAT_EQ(-7, material.GetPower()); Color(MaterialProbe::ActiveSpecular(material), VxColor(0.0f, 0.0f, 0.0f, 0.0f));
    material.SetPower(std::numeric_limits<float>::quiet_NaN()); EXPECT_TRUE(std::isnan(material.GetPower()));
    material.SetSpecular(VxColor(1.0f, 2.0f, 3.0f, 4.0f)); Color(MaterialProbe::ActiveSpecular(material), VxColor(0.0f, 0.0f, 0.0f, 0.0f));
}
TEST_F(MaterialPropertiesTest, DiffuseChangeInvalidatesBatchVerticesWithoutDiscardingAllocationOrIndices) {
    RCKMaterial material(Context); CKSprite3DBatch &batch = MaterialProbe::SeedBatch(material);
    material.SetDiffuse(VxColor(.1f, .2f, .3f, .4f)); EXPECT_EQ(0u, batch.m_VertexCount);
    EXPECT_EQ(4, batch.m_Vertices.Size()); EXPECT_EQ(6, batch.m_Indices.Size());
    EXPECT_EQ(6u, batch.m_IndexCount); EXPECT_EQ(9u, batch.m_Flags);
}
TEST_F(MaterialPropertiesTest, SpecularChangeInvalidatesBatchWhilePowerAndAmbientDoNot) {
    RCKMaterial material(Context); CKSprite3DBatch &batch = MaterialProbe::SeedBatch(material);
    material.SetPower(3); material.SetAmbient(VxColor(1.0f, 0.0f, 0.0f, 1.0f)); material.SetEmissive(VxColor(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(4u, batch.m_VertexCount); material.SetSpecular(VxColor(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_EQ(0u, batch.m_VertexCount); EXPECT_EQ(6, batch.m_Indices.Size());
}
TEST_F(MaterialPropertiesTest, FourTextureSlotsRetainBorrowedPointersIndependentlyAndPrimarySetterAliasesSlotZero) {
    RCKMaterial material(Context); RCKTexture first(Context), second(Context);
    for (int slot = 0; slot < 4; ++slot) material.SetTexture(slot, &first);
    material.SetTexture(2, &second); material.SetTexture0(&second);
    EXPECT_EQ(&second, material.GetTexture(0)); EXPECT_EQ(&first, material.GetTexture(1));
    EXPECT_EQ(&second, material.GetTexture(2)); EXPECT_EQ(&first, material.GetTexture(3));
    material.SetTexture(2, nullptr); EXPECT_EQ(nullptr, material.GetTexture(2));
    EXPECT_EQ(&first, Context->GetObject(first.GetID())); EXPECT_EQ(&second, Context->GetObject(second.GetID()));
}
TEST_F(MaterialPropertiesTest, InvalidTextureIndicesHaveNoEffectOnMaterialState) {
    RCKMaterial material(Context); RCKTexture texture(Context); material.SetTexture0(&texture);
    material.SetTexture(-1, &texture); material.SetTexture(4, &texture);
    EXPECT_EQ(nullptr, material.GetTexture(-1)); EXPECT_EQ(nullptr, material.GetTexture(4));
    EXPECT_EQ(&texture, material.GetTexture(0)); for (int slot = 1; slot < 4; ++slot) EXPECT_EQ(nullptr, material.GetTexture(slot));
    EXPECT_FLOAT_EQ(0, material.GetPower());
}
