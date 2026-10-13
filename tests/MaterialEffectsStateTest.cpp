#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class MaterialEffectsStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        ASSERT_GE(Manager->GetEffectCount(), 7);
        ASSERT_GE(Context->GetParameterManager()->ParameterGuidToType(CKPGUID_INT), 0);
        ASSERT_GE(Context->GetParameterManager()->ParameterGuidToType(CKPGUID_FLOAT), 0);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    VX_EFFECT Add(CKGUID type, const char *name, const char *value) {
        VxEffectDescription description; description.Summary = name; description.ParameterType = type;
        description.ParameterDescription = name; description.ParameterDefaultValue = value;
        return static_cast<VX_EFFECT>(Manager->AddEffect(description));
    }
};
}

TEST_F(MaterialEffectsStateTest, NewParameterUsesRegisteredTypeDescriptorNameAndMeaningfulDefaultValue) {
    RCKMaterial material(Context); const VX_EFFECT effect = Add(CKPGUID_INT, "Integer effect argument", "17");
    material.SetEffect(effect); EXPECT_EQ(effect, material.GetEffect()); CKParameter *parameter = material.GetEffectParameter();
    ASSERT_NE(nullptr, parameter); EXPECT_EQ(CKPGUID_INT, parameter->GetGUID()); EXPECT_STREQ("Integer effect argument", parameter->GetName());
    int value = 0; ASSERT_EQ(CK_OK, parameter->GetValue(&value)); EXPECT_EQ(17, value);
    EXPECT_EQ(parameter, Context->GetObject(parameter->GetID())); EXPECT_EQ(nullptr, parameter->GetOwner()); EXPECT_FALSE(parameter->IsDynamic());
}
TEST_F(MaterialEffectsStateTest, ReapplyingSameEffectPreservesEditedParameterValueAndObjectIdentity) {
    RCKMaterial material(Context); const VX_EFFECT effect = Add(CKPGUID_INT, "Integer effect argument", "17");
    material.SetEffect(effect); CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter);
    const CK_ID id = parameter->GetID(); int value = 39; ASSERT_EQ(CK_OK, parameter->SetValue(&value));
    material.SetEffect(effect); EXPECT_EQ(parameter, material.GetEffectParameter()); EXPECT_EQ(parameter, Context->GetObject(id));
    value = 0; ASSERT_EQ(CK_OK, parameter->GetValue(&value)); EXPECT_EQ(39, value);
}
TEST_F(MaterialEffectsStateTest, DifferentEffectWithSameTypeRenamesWithoutResettingEditedValue) {
    RCKMaterial material(Context); const VX_EFFECT first = Add(CKPGUID_INT, "First integer", "17");
    const VX_EFFECT second = Add(CKPGUID_INT, "Second integer", "99"); material.SetEffect(first);
    CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter);
    int value = 39; ASSERT_EQ(CK_OK, parameter->SetValue(&value)); material.SetEffect(second);
    EXPECT_EQ(parameter, material.GetEffectParameter()); EXPECT_STREQ("Second integer", parameter->GetName());
    value = 0; ASSERT_EQ(CK_OK, parameter->GetValue(&value)); EXPECT_EQ(39, value); EXPECT_EQ(second, material.GetEffect());
}
TEST_F(MaterialEffectsStateTest, DifferentParameterTypeReusesObjectAndAppliesNewDefaultAfterTypeConversion) {
    RCKMaterial material(Context); const VX_EFFECT integer = Add(CKPGUID_INT, "Integer", "17");
    const VX_EFFECT floating = Add(CKPGUID_FLOAT, "Floating", "2.5"); material.SetEffect(integer);
    CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter); const CK_ID id = parameter->GetID();
    material.SetEffect(floating); ASSERT_EQ(parameter, material.GetEffectParameter()); EXPECT_EQ(id, parameter->GetID());
    EXPECT_EQ(CKPGUID_FLOAT, parameter->GetGUID()); EXPECT_STREQ("Floating", parameter->GetName());
    float value = 0; ASSERT_EQ(CK_OK, parameter->GetValue(&value)); EXPECT_FLOAT_EQ(2.5f, value);
}
TEST_F(MaterialEffectsStateTest, ClearingEffectDestroysParameterAndRepeatedClearIsHarmless) {
    RCKMaterial material(Context); material.SetEffect(Add(CKPGUID_INT, "Integer", "17"));
    CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter); const CK_ID id = parameter->GetID();
    material.SetEffect(VXEFFECT_NONE); EXPECT_EQ(VXEFFECT_NONE, material.GetEffect()); EXPECT_EQ(nullptr, material.GetEffectParameter());
    EXPECT_EQ(nullptr, Context->GetObject(id)); material.SetEffect(VXEFFECT_NONE); EXPECT_EQ(nullptr, material.GetEffectParameter());
}
TEST_F(MaterialEffectsStateTest, SwitchingToNonParameterizedEffectDestroysObsoleteParameter) {
    RCKMaterial material(Context); material.SetEffect(Add(CKPGUID_INT, "Integer", "17"));
    CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter); const CK_ID id = parameter->GetID();
    const VX_EFFECT effect = Add(CKGUID(), "No parameter", ""); material.SetEffect(effect);
    EXPECT_EQ(effect, material.GetEffect()); EXPECT_EQ(nullptr, material.GetEffectParameter()); EXPECT_EQ(nullptr, Context->GetObject(id));
}
TEST_F(MaterialEffectsStateTest, DynamicMaterialCreatesDynamicEffectParameterWithoutReusingStaticNamesake) {
    const VX_EFFECT effect = Add(CKPGUID_INT, "Shared effect name", "17"); RCKMaterial staticMaterial(Context);
    staticMaterial.SetEffect(effect); CKParameter *first = staticMaterial.GetEffectParameter(); ASSERT_NE(nullptr, first);
    RCKMaterial *dynamicMaterial = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL, nullptr, CK_OBJECTCREATION_DYNAMIC));
    ASSERT_NE(nullptr, dynamicMaterial); ASSERT_TRUE(dynamicMaterial->IsDynamic()); dynamicMaterial->SetEffect(effect);
    CKParameter *second = dynamicMaterial->GetEffectParameter(); ASSERT_NE(nullptr, second); EXPECT_NE(first, second);
    EXPECT_TRUE(second->IsDynamic()); EXPECT_FALSE(first->IsDynamic()); EXPECT_EQ(CKPGUID_INT, second->GetGUID());
}
TEST_F(MaterialEffectsStateTest, MaterialsWithSameEffectOwnDistinctParametersAndEditsDoNotLeak) {
    const VX_EFFECT effect = Add(CKPGUID_INT, "Repeated name", "17"); RCKMaterial first(Context), second(Context);
    first.SetEffect(effect); second.SetEffect(effect); CKParameter *a = first.GetEffectParameter(), *b = second.GetEffectParameter();
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b); EXPECT_NE(a, b); int value = 43; ASSERT_EQ(CK_OK, a->SetValue(&value));
    value = 0; ASSERT_EQ(CK_OK, b->GetValue(&value)); EXPECT_EQ(17, value);
    const CK_ID bId = b->GetID(); first.SetEffect(VXEFFECT_NONE); EXPECT_EQ(b, Context->GetObject(bId));
}
TEST_F(MaterialEffectsStateTest, MaskedEffectIndexUsesStoredDescriptorAndPreservesRenderState) {
    RCKMaterial material(Context); const VX_EFFECT effect = Add(CKPGUID_INT, "Masked integer", "17"); ASSERT_LT(static_cast<int>(effect), 32);
    material.SetZFunc(VXCMP_GREATER); material.SetAlphaFunc(VXCMP_LESS); material.SetTwoSided(TRUE); material.EnableAlphaBlend(TRUE);
    material.SetEffect(static_cast<VX_EFFECT>(static_cast<int>(effect) + 64)); EXPECT_EQ(effect, material.GetEffect());
    CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter); int value = 0;
    ASSERT_EQ(CK_OK, parameter->GetValue(&value)); EXPECT_EQ(17, value);
    EXPECT_EQ(VXCMP_GREATER, material.GetZFunc()); EXPECT_EQ(VXCMP_LESS, material.GetAlphaFunc());
    EXPECT_TRUE(material.IsTwoSided()); EXPECT_TRUE(material.AlphaBlendEnabled()); EXPECT_TRUE(material.ZWriteEnabled());
}
TEST_F(MaterialEffectsStateTest, UnavailableDescriptorClearsStaleParameterAndStoresMaskedEffectWithoutCrash) {
    RCKMaterial material(Context); material.SetEffect(Add(CKPGUID_INT, "Integer", "17"));
    CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter); const CK_ID id = parameter->GetID();
    ASSERT_LT(Manager->GetEffectCount(), 63); material.SetEffect(static_cast<VX_EFFECT>(63));
    EXPECT_EQ(63, static_cast<int>(material.GetEffect())); EXPECT_EQ(nullptr, material.GetEffectParameter()); EXPECT_EQ(nullptr, Context->GetObject(id));
}
