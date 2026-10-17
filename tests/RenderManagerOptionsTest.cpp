#include "MaterialRenderObserver.h"
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKLight.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKSprite.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class ManagerOptionsBoundary : public MaterialRenderObserver {
public:
    int OptionWrites = 0;
    CKRasterizerOptions LastOptions;
    CKBOOL SetOptions(const CKRasterizerOptions *options) override {
        ++OptionWrites;
        LastOptions = *options;
        return TRUE;
    }
};

struct ManagerEffectCalls {
    int Count = 0, Stage = -1;
    CKMaterial *Material = nullptr;
};

static VX_EFFECTCALLBACK_RETVAL ObserveManagerEffect(CKRenderContext *, CKMaterial *material, int stage, void *argument) {
    ManagerEffectCalls *calls = static_cast<ManagerEffectCalls *>(argument);
    ++calls->Count;
    calls->Stage = stage;
    calls->Material = material;
    return VXEFFECTRETVAL_SKIPALL;
}

class RenderManagerOptionsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_TextureVideoFormat.Value = _32_ARGB8888;
        Manager->m_SpriteVideoFormat.Value = _32_ARGB8888;
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(RenderManagerOptionsTest, DesiredTextureFormatChangesFutureDefaultsWithoutReplacingExistingOverrides) {
    RCKTexture *existing = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    ASSERT_NE(nullptr, existing);
    existing->SetDesiredVideoFormat(_16_ARGB1555);
    VxImageDescEx requested;
    VxPixelFormat2ImageDesc(_16_RGB565, requested);
    Manager->SetDesiredTexturesVideoFormat(requested);
    EXPECT_EQ(_16_ARGB1555, existing->GetDesiredVideoFormat());
    RCKTexture *created = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    ASSERT_NE(nullptr, created);
    EXPECT_EQ(_16_RGB565, created->GetDesiredVideoFormat());
    VxImageDescEx actual;
    Manager->GetDesiredTexturesVideoFormat(actual);
    EXPECT_EQ(_16_RGB565, VxImageDesc2PixelFormat(actual));
    EXPECT_EQ(16, actual.BitsPerPixel);
    EXPECT_EQ(0xf800u, actual.RedMask);
    EXPECT_EQ(0x07e0u, actual.GreenMask);
    EXPECT_EQ(0x001fu, actual.BlueMask);
    EXPECT_EQ(0u, actual.AlphaMask);
}

TEST_F(RenderManagerOptionsTest, FormatOptionsAreCaseInsensitiveCreationDefaultsForTexturesAndSprites) {
    RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    RCKSprite *sprite = static_cast<RCKSprite *>(Context->CreateObject(CKCID_SPRITE));
    ASSERT_NE(nullptr, texture);
    ASSERT_NE(nullptr, sprite);
    texture->SetDesiredVideoFormat(_16_ARGB4444);
    sprite->SetDesiredVideoFormat(_16_ARGB1555);
    Manager->SetRenderOptions("tExTuReViDeOfOrMaT", _16_RGB565);
    Manager->SetRenderOptions("sPrItEvIdEoFoRmAt", _32_ARGB8888);
    EXPECT_EQ(_16_ARGB4444, texture->GetDesiredVideoFormat());
    EXPECT_EQ(_16_ARGB1555, sprite->GetDesiredVideoFormat());
    RCKTexture *newTexture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    RCKSprite *newSprite = static_cast<RCKSprite *>(Context->CreateObject(CKCID_SPRITE));
    ASSERT_NE(nullptr, newTexture);
    ASSERT_NE(nullptr, newSprite);
    EXPECT_EQ(_16_RGB565, newTexture->GetDesiredVideoFormat());
    EXPECT_EQ(_32_ARGB8888, newSprite->GetDesiredVideoFormat());
    Manager->SetRenderOptions("NoSuchOption", 0x12345678u);
    EXPECT_EQ(static_cast<CKDWORD>(_16_RGB565), Manager->m_TextureVideoFormat.Value);
    EXPECT_EQ(static_cast<CKDWORD>(_32_ARGB8888), Manager->m_SpriteVideoFormat.Value);
}

TEST_F(RenderManagerOptionsTest, ChangingOptionsDoesNotReconfigureExistingContexts) {
    ManagerOptionsBoundary boundary;
    RCKRenderContext view(Context);
    view.m_RasterizerContext = &boundary;
    Manager->m_RenderContexts.PushBack(view.GetID());
    Manager->SetRenderOptions("DisableFilter", 1);
    Manager->SetRenderOptions("DisableMipmap", 1);
    Manager->SetRenderOptions("Antialias", 4);
    EXPECT_EQ(1u, Manager->m_DisableFilter.Value);
    EXPECT_EQ(1u, Manager->m_DisableMipmap.Value);
    EXPECT_EQ(4u, Manager->m_Antialias.Value);
    EXPECT_EQ(0, boundary.OptionWrites);
    // Context creation (and explicit refresh) consumes the current defaults.
    view.ApplyRenderOptions();
    EXPECT_EQ(1, boundary.OptionWrites);
    EXPECT_TRUE(boundary.LastOptions.DisableTextureFiltering);
    EXPECT_TRUE(boundary.LastOptions.DisableMipmaps);
    EXPECT_EQ(4u, boundary.LastOptions.MSAASamples);
    Manager->RemoveRenderContext(&view);
}

TEST_F(RenderManagerOptionsTest, BuiltinEffectsPublishNativeIndicesBudgetsAndParameterSchemas) {
    const char *names[] = {"None", "TexGen", "TexGen with referential", "Environment Bump Map",
                           "Floor DotProduct3 Lighting", "Combine 2 Textures", "Combine 3 Textures"};
    const int textures[] = {0, 0, 0, 2, 1, 1, 2};
    const int coordinates[] = {0, 0, 0, 2, 1, 0, 0};
    EXPECT_EQ(7, Manager->GetEffectCount());
    for (int i = 0; i < 7; ++i) {
        const VxEffectDescription &effect = Manager->GetEffectDescription(i);
        EXPECT_EQ(i, static_cast<int>(effect.EffectIndex));
        EXPECT_STREQ(names[i], effect.Summary.CStr());
        EXPECT_EQ(textures[i], effect.MaxTextureCount);
        EXPECT_EQ(coordinates[i], effect.NeededTextureCoordsCount);
    }
    CKParameterManager *parameters = Context->GetParameterManager();
    CKParameterTypeDesc *type = parameters->GetParameterTypeDescription(CKPGUID_TEXGENREFEFFECT);
    ASSERT_NE(nullptr, type);
    EXPECT_STREQ("Tex coords gen+Referential", type->TypeName.CStr());
    CKStructStruct *structure = parameters->GetStructDescByType(type->Index);
    ASSERT_NE(nullptr, structure);
    ASSERT_EQ(2, structure->GetNumSubParam());
    EXPECT_EQ(CKPGUID_TEXGENEFFECT, structure->GetSubParamGuid(0));
    EXPECT_EQ(CKPGUID_3DENTITY, structure->GetSubParamGuid(1));
    EXPECT_STREQ("TexGen", structure->GetSubParamDescription(0));
    EXPECT_STREQ("Referential", structure->GetSubParamDescription(1));
    type = parameters->GetParameterTypeDescription(CKPGUID_MATERIALEFFECT);
    ASSERT_NE(nullptr, type);
    EXPECT_TRUE(type->dwFlags & CKPARAMETERTYPE_HIDDEN);
    CKEnumStruct *enumeration = parameters->GetEnumDescByType(type->Index);
    ASSERT_NE(nullptr, enumeration);
    ASSERT_EQ(7, enumeration->GetNumEnums());
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(i, enumeration->GetEnumValue(i));
        EXPECT_STREQ(names[i], enumeration->GetEnumDescription(i));
    }
}

TEST_F(RenderManagerOptionsTest, DotProductLightingCreatesALightTypedParameter) {
    CKParameterTypeDesc *lightType = Context->GetParameterManager()->GetParameterTypeDescription(CKPGUID_LIGHT);
    ASSERT_NE(nullptr, lightType);
    EXPECT_EQ(CKCID_LIGHT, lightType->Cid);
    const VxEffectDescription &effect = Manager->GetEffectDescription(VXEFFECT_DP3);
    EXPECT_EQ(CKPGUID_LIGHT, effect.ParameterType);
    RCKMaterial material(Context);
    material.SetEffect(VXEFFECT_DP3);
    CKParameter *parameter = material.GetEffectParameter();
    ASSERT_NE(nullptr, parameter);
    EXPECT_EQ(CKPGUID_LIGHT, parameter->GetGUID());
    EXPECT_STREQ("Light", parameter->GetName());
    CK_ID light = 0xffffffffu;
    ASSERT_EQ(CK_OK, parameter->GetValue(&light));
    EXPECT_EQ(0u, light);
}

TEST_F(RenderManagerOptionsTest, AddedEffectCopiesDescriptionAssignsIndexAndRunsItsCallback) {
    ManagerEffectCalls calls;
    VxEffectDescription description;
    description.EffectIndex = static_cast<VX_EFFECT>(99);
    description.Summary = "Custom effect";
    description.Description = "Independent description";
    description.NeededTextureCoordsCount = 0;
    description.SetCallback = ObserveManagerEffect;
    description.CallbackArg = &calls;
    const int index = Manager->AddEffect(description);
    EXPECT_EQ(7, index);
    EXPECT_EQ(8, Manager->GetEffectCount());
    EXPECT_EQ(99, static_cast<int>(description.EffectIndex));
    description.Summary = "Changed source";
    const VxEffectDescription &stored = Manager->GetEffectDescription(index);
    EXPECT_EQ(index, static_cast<int>(stored.EffectIndex));
    EXPECT_STREQ("Custom effect", stored.Summary.CStr());
    EXPECT_STREQ("Independent description", stored.Description.CStr());
    EXPECT_EQ(ObserveManagerEffect, stored.SetCallback);
    EXPECT_EQ(&calls, stored.CallbackArg);

    ManagerOptionsBoundary boundary;
    RCKRenderContext view(Context);
    view.m_RasterizerContext = &boundary;
    RCKMaterial material(Context);
    material.SetEffect(static_cast<VX_EFFECT>(index));
    ASSERT_TRUE(material.SetAsCurrent(&view, FALSE, 2));
    EXPECT_EQ(1, calls.Count);
    EXPECT_EQ(2, calls.Stage);
    EXPECT_EQ(reinterpret_cast<CKMaterial *>(&material), calls.Material);
}
} // namespace
