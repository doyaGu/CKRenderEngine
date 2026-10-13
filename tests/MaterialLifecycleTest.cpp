#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKSprite3D.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class MaterialLayoutProbe : public RCKMaterial {
public:
    static void EffectParameter(RCKMaterial &material, CKParameter *parameter) { material.*(&MaterialLayoutProbe::m_EffectParameter) = parameter; }
    static void RecordLayout(RCKMaterial &material) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&material);
        std::size_t previous = sizeof(CKMaterial);
#define FIELD(field) { auto member = &MaterialLayoutProbe::field; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(material.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(material.*member), sizeof(RCKMaterial)); \
        EXPECT_EQ(0u, address % alignof(decltype(material.*member))); \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(material.*member))); \
        previous = offset + sizeof(material.*member); }
        FIELD(m_Textures); FIELD(m_MaterialData); FIELD(m_SpecularColor);
        FIELD(m_TextureBlendMode); FIELD(m_TextureMinMode); FIELD(m_TextureMagMode);
        FIELD(m_SourceBlend); FIELD(m_DestBlend); FIELD(m_ShadeMode); FIELD(m_FillMode);
        FIELD(m_TextureAddressMode); FIELD(m_TextureBorderColor); FIELD(m_Flags); FIELD(m_AlphaRef);
        FIELD(m_Sprite3DBatch); FIELD(m_Callback); FIELD(m_CallbackArgument); FIELD(m_EffectParameter);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKMaterial)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKMaterial)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(CKMaterial)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class MaterialLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Color(const VxColor &actual, float r, float g, float b, float a) {
        EXPECT_FLOAT_EQ(r, actual.r); EXPECT_FLOAT_EQ(g, actual.g); EXPECT_FLOAT_EQ(b, actual.b); EXPECT_FLOAT_EQ(a, actual.a);
    }
};
}

TEST_F(MaterialLifecycleTest, RegisteredDefaultsAndActualPlatformLayout) {
    RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr, material);
    EXPECT_EQ(30, material->GetClassID()); EXPECT_EQ(30, RCKMaterial::m_ClassID);
    EXPECT_TRUE(CKIsChildClassOf(material, CKCID_BEOBJECT)); EXPECT_EQ(material, Context->GetObject(material->GetID()));
    for (int i = 0; i < 4; ++i) EXPECT_EQ(nullptr, material->GetTexture(i));
    Color(material->GetDiffuse(), .7f, .7f, .7f, 1); Color(material->GetAmbient(), .3f, .3f, .3f, 1);
    Color(material->GetSpecular(), .5f, .5f, .5f, 1); Color(material->GetEmissive(), 0, 0, 0, 1); EXPECT_FLOAT_EQ(0, material->GetPower());
    EXPECT_EQ(VXTEXTUREBLEND_MODULATEALPHA, material->GetTextureBlendMode()); EXPECT_EQ(VXTEXTUREFILTER_LINEAR, material->GetTextureMinMode());
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, material->GetTextureMagMode()); EXPECT_EQ(VXBLEND_ONE, material->GetSourceBlend()); EXPECT_EQ(VXBLEND_ZERO, material->GetDestBlend());
    EXPECT_EQ(VXSHADE_GOURAUD, material->GetShadeMode()); EXPECT_EQ(VXFILL_SOLID, material->GetFillMode());
    EXPECT_EQ(VXTEXTURE_ADDRESSWRAP, material->GetTextureAddressMode()); EXPECT_EQ(0u, material->GetTextureBorderColor());
    EXPECT_FALSE(material->IsTwoSided()); EXPECT_TRUE(material->ZWriteEnabled()); EXPECT_TRUE(material->PerspectiveCorrectionEnabled());
    EXPECT_FALSE(material->AlphaBlendEnabled()); EXPECT_FALSE(material->AlphaTestEnabled()); EXPECT_EQ(VXCMP_LESSEQUAL, material->GetZFunc());
    EXPECT_EQ(VXCMP_ALWAYS, material->GetAlphaFunc()); EXPECT_EQ(0, material->GetAlphaRef()); EXPECT_EQ(VXEFFECT_NONE, material->GetEffect());
    EXPECT_EQ(nullptr, material->GetEffectParameter()); EXPECT_EQ(nullptr, material->GetSprite3DBatch());
    void *argument = material; EXPECT_EQ(nullptr, material->GetCallback(&argument)); EXPECT_EQ(nullptr, argument);
    MaterialLayoutProbe::RecordLayout(*material);
}
TEST_F(MaterialLifecycleTest, NamedConstructionCopiesNameAndKeepsIndependentDefaults) {
    char name[] = "Named material"; RCKMaterial first(Context, name), second(Context); name[0] = 'X';
    EXPECT_STREQ("Named material", first.GetName()); EXPECT_EQ(nullptr, second.GetName());
    first.SetDiffuse(VxColor(.1f, .2f, .3f, .4f)); Color(second.GetDiffuse(), .7f, .7f, .7f, 1);
    first.SetPriority(17); EXPECT_EQ(17, first.GetPriority()); EXPECT_EQ(0, second.GetPriority());
}
TEST_F(MaterialLifecycleTest, ClassRegistrationIncludesTextureAndEffectParameterDeletionNotifications) {
    EXPECT_STREQ("Material", RCKMaterial::GetClassName()); CKClassDesc *desc = CKGetClassDesc(CKCID_MATERIAL); ASSERT_NE(nullptr, desc);
    desc->ToBeNotify.Clear(); desc->Parameter = CKGUID(); desc->DefaultOptions = 0; RCKMaterial::Register();
    EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_TEXTURE)); EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_PARAMETER));
    EXPECT_EQ(CKPGUID_MATERIAL, desc->Parameter); EXPECT_EQ(1437274829u, desc->Parameter.d1); EXPECT_EQ(581855883u, desc->Parameter.d2);
    EXPECT_EQ(CK_GENERALOPTIONS_CANUSECURRENTOBJECT, desc->DefaultOptions); EXPECT_STREQ("Material", desc->NameFct());
}
TEST_F(MaterialLifecycleTest, DependencyMetadataUsesNativeModeTableAndExactTextureLabel) {
    const int modes[] = {-1, 0, 1, 2, 3, 4, 5, 100}; CKClassDesc *desc = CKGetClassDesc(CKCID_MATERIAL); ASSERT_NE(nullptr, desc);
    for (int mode : modes) { const int count = mode == 1 || mode == 2 || mode == 4 ? 1 : 0;
        EXPECT_EQ(count, RCKMaterial::GetDependenciesCount(mode)); EXPECT_EQ(count, desc->DependsCountFct(mode));
        EXPECT_STREQ("Texture", RCKMaterial::GetDependencies(0, mode)); EXPECT_STREQ("Texture", desc->DependsFct(0, mode));
        EXPECT_EQ(nullptr, RCKMaterial::GetDependencies(-1, mode)); EXPECT_EQ(nullptr, RCKMaterial::GetDependencies(1, mode)); }
}
TEST_F(MaterialLifecycleTest, DirectFactorySDKDispatchAndVirtualDestructionKeepBorrowedResources) {
    RCKTexture texture(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject; RCKSprite3D sprite(Context);
    RCKMaterial *material = RCKMaterial::CreateInstance(Context); ASSERT_NE(nullptr, material); const CK_ID id = material->GetID();
    CKMaterial *api = reinterpret_cast<CKMaterial *>(material); EXPECT_EQ(30, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    api->SetTexture(3, reinterpret_cast<CKTexture *>(&texture)); EXPECT_EQ(reinterpret_cast<CKTexture *>(&texture), api->GetTexture(3));
    MaterialLayoutProbe::EffectParameter(*material, &parameter); ASSERT_TRUE(material->AddSprite3DBatch(&sprite)); ASSERT_NE(nullptr, material->GetSprite3DBatch());
    delete static_cast<CKObject *>(material); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&texture, Context->GetObject(texture.GetID())); EXPECT_EQ(&parameter, Context->GetObject(parameter.GetID())); EXPECT_EQ(&sprite, Context->GetObject(sprite.GetID()));
}
TEST_F(MaterialLifecycleTest, MemoryOccupationUsesPortableTailAndDoesNotCountBorrowedResources) {
    RCKMaterial material(Context); RCKTexture texture(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject;
    EXPECT_EQ(material.CKBeObject::GetMemoryOccupation() + static_cast<int>(sizeof(RCKMaterial) - sizeof(CKBeObject)), material.GetMemoryOccupation());
    const int before = material.GetMemoryOccupation(); material.SetTexture(2, reinterpret_cast<CKTexture *>(&texture));
    MaterialLayoutProbe::EffectParameter(material, &parameter); texture.SetName("Borrowed texture name"); EXPECT_EQ(before, material.GetMemoryOccupation());
    material.SetName("Owned material name"); EXPECT_EQ(material.CKBeObject::GetMemoryOccupation() + static_cast<int>(sizeof(RCKMaterial) - sizeof(CKBeObject)), material.GetMemoryOccupation());
}
TEST_F(MaterialLifecycleTest, CheckPreDeletionClearsAllMatchingTextureSlotsAndPreservesOtherSlots) {
    RCKMaterial material(Context); RCKTexture doomed(Context), survivor(Context); material.SetTexture(0, reinterpret_cast<CKTexture *>(&doomed));
    material.SetTexture(1, reinterpret_cast<CKTexture *>(&survivor)); material.SetTexture(3, reinterpret_cast<CKTexture *>(&doomed));
    material.CheckPreDeletion(); EXPECT_EQ(reinterpret_cast<CKTexture *>(&doomed), material.GetTexture(0));
    doomed.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); material.CheckPreDeletion(); EXPECT_EQ(nullptr, material.GetTexture(0)); EXPECT_EQ(nullptr, material.GetTexture(3));
    EXPECT_EQ(reinterpret_cast<CKTexture *>(&survivor), material.GetTexture(1)); EXPECT_EQ(nullptr, material.GetTexture(2)); doomed.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(MaterialLifecycleTest, ContextTextureDeletionNotifiesMultipleMaterialsAndKeepsOtherResource) {
    RCKMaterial *first = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); RCKMaterial *second = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    RCKTexture *doomed = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE)); RCKTexture *survivor = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, doomed); ASSERT_NE(nullptr, survivor);
    first->SetTexture(0, reinterpret_cast<CKTexture *>(doomed)); first->SetTexture(3, reinterpret_cast<CKTexture *>(doomed)); second->SetTexture(1, reinterpret_cast<CKTexture *>(doomed));
    second->SetTexture(2, reinterpret_cast<CKTexture *>(survivor)); const CK_ID id = doomed->GetID(); CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(doomed, 0, &options)); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(nullptr, first->GetTexture(0)); EXPECT_EQ(nullptr, first->GetTexture(3)); EXPECT_EQ(nullptr, second->GetTexture(1)); EXPECT_EQ(reinterpret_cast<CKTexture *>(survivor), second->GetTexture(2));
}
TEST_F(MaterialLifecycleTest, CheckPreDeletionClearsFlaggedEffectParameterAndPreservesLiveParameter) {
    RCKMaterial material(Context); CKParameter *doomedObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, doomedObject); CKParameter &doomed = *doomedObject; CKParameter *survivorObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, survivorObject); CKParameter &survivor = *survivorObject; MaterialLayoutProbe::EffectParameter(material, &doomed);
    material.CheckPreDeletion(); EXPECT_EQ(&doomed, material.GetEffectParameter()); doomed.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    material.CheckPreDeletion(); EXPECT_EQ(nullptr, material.GetEffectParameter()); doomed.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    MaterialLayoutProbe::EffectParameter(material, &survivor); material.CheckPreDeletion(); EXPECT_EQ(&survivor, material.GetEffectParameter());
}
TEST_F(MaterialLifecycleTest, ContextParameterDeletionClearsSharedReferenceWithoutChangingEffectOrTextures) {
    RCKMaterial *first = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); RCKMaterial *second = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    CKParameter *parameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); RCKTexture texture(Context);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, parameter);
    MaterialLayoutProbe::EffectParameter(*first, parameter); MaterialLayoutProbe::EffectParameter(*second, parameter);
    first->SetTexture(0, reinterpret_cast<CKTexture *>(&texture)); const VX_EFFECT effect = first->GetEffect(); const CK_ID id = parameter->GetID();
    CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE; ASSERT_EQ(CK_OK, Context->DestroyObject(parameter, 0, &options));
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(nullptr, first->GetEffectParameter()); EXPECT_EQ(nullptr, second->GetEffectParameter());
    EXPECT_EQ(effect, first->GetEffect()); EXPECT_EQ(reinterpret_cast<CKTexture *>(&texture), first->GetTexture(0));
    // Also make teardown safe when this test is run against the immutable old library.
    MaterialLayoutProbe::EffectParameter(*first, nullptr); MaterialLayoutProbe::EffectParameter(*second, nullptr);
}
TEST_F(MaterialLifecycleTest, IsObjectUsedRequiresExactReferencedTextureAndMatchingClass) {
    RCKMaterial material(Context); RCKTexture first(Context), second(Context); CKObject unrelated(Context); material.SetTexture(3, reinterpret_cast<CKTexture *>(&first));
    EXPECT_TRUE(material.IsObjectUsed(&first, CKCID_TEXTURE)); EXPECT_FALSE(material.IsObjectUsed(&second, CKCID_TEXTURE));
    EXPECT_FALSE(material.IsObjectUsed(&first, CKCID_MATERIAL)); EXPECT_FALSE(material.IsObjectUsed(&unrelated, CKCID_TEXTURE));
    material.SetTexture(3, nullptr); EXPECT_FALSE(material.IsObjectUsed(&first, CKCID_TEXTURE));
}
TEST_F(MaterialLifecycleTest, NullIsNotAReferencedTextureWithEmptyOrPartiallyFilledSlots) {
    RCKMaterial material(Context); RCKTexture texture(Context); EXPECT_FALSE(material.IsObjectUsed(nullptr, CKCID_TEXTURE));
    EXPECT_FALSE(material.IsObjectUsed(nullptr, CKCID_OBJECT)); material.SetTexture(0, reinterpret_cast<CKTexture *>(&texture));
    EXPECT_FALSE(material.IsObjectUsed(nullptr, CKCID_TEXTURE)); material.SetTexture(0, nullptr); EXPECT_FALSE(material.IsObjectUsed(nullptr, CKCID_TEXTURE));
}
TEST_F(MaterialLifecycleTest, NullSceneOperationsPreserveExistingMaterialAndTextureMembership) {
    RCKMaterial material(Context); RCKTexture texture(Context); CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); ASSERT_NE(nullptr, scene);
    material.SetTexture(0, reinterpret_cast<CKTexture *>(&texture)); scene->AddObject(&material); scene->AddObject(&texture);
    material.AddToScene(nullptr, TRUE); material.RemoveFromScene(nullptr, TRUE); EXPECT_TRUE(material.IsInScene(scene)); EXPECT_TRUE(texture.IsInScene(scene));
}
TEST_F(MaterialLifecycleTest, AddWithoutOwnDependenciesStillAddsInheritedScriptButNotTexture) {
    RCKMaterial material(Context); RCKTexture texture(Context); CKBehavior *behaviorObject = static_cast<CKBehavior *>(Context->CreateObject(CKCID_BEHAVIOR)); ASSERT_NE(nullptr, behaviorObject); CKBehavior &behavior = *behaviorObject; behavior.SetType(CKBEHAVIORTYPE_SCRIPT);
    ASSERT_EQ(CK_OK, material.AddScript(&behavior)); material.SetTexture(0, reinterpret_cast<CKTexture *>(&texture));
    CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); ASSERT_NE(nullptr, scene); material.AddToScene(scene, FALSE);
    EXPECT_TRUE(material.IsInScene(scene)); EXPECT_TRUE(behavior.IsInScene(scene)); EXPECT_FALSE(texture.IsInScene(scene)); material.RemoveAllScripts();
}
TEST_F(MaterialLifecycleTest, AddWithDependenciesAddsOnlyPrimaryTextureAndDoesNotDuplicateMembership) {
    RCKMaterial material(Context); RCKTexture primary(Context), auxiliary(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject;
    material.SetTexture(0, reinterpret_cast<CKTexture *>(&primary)); material.SetTexture(1, reinterpret_cast<CKTexture *>(&auxiliary));
    MaterialLayoutProbe::EffectParameter(material, &parameter); CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); ASSERT_NE(nullptr, scene);
    material.AddToScene(scene, TRUE); material.AddToScene(scene, TRUE); EXPECT_TRUE(material.IsInScene(scene)); EXPECT_TRUE(primary.IsInScene(scene));
    EXPECT_FALSE(auxiliary.IsInScene(scene)); EXPECT_EQ(1, material.GetSceneInCount()); EXPECT_EQ(1, primary.GetSceneInCount());
}
TEST_F(MaterialLifecycleTest, RemoveWithoutDependenciesLeavesPrimaryTextureAndInheritedScriptInScene) {
    RCKMaterial material(Context); RCKTexture primary(Context); CKBehavior *behaviorObject = static_cast<CKBehavior *>(Context->CreateObject(CKCID_BEHAVIOR)); ASSERT_NE(nullptr, behaviorObject); CKBehavior &behavior = *behaviorObject; behavior.SetType(CKBEHAVIORTYPE_SCRIPT);
    ASSERT_EQ(CK_OK, material.AddScript(&behavior)); material.SetTexture(0, reinterpret_cast<CKTexture *>(&primary));
    CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); ASSERT_NE(nullptr, scene);
    material.AddToScene(scene, TRUE); ASSERT_TRUE(material.IsInScene(scene)); ASSERT_TRUE(primary.IsInScene(scene)); ASSERT_TRUE(behavior.IsInScene(scene));
    material.RemoveFromScene(scene, FALSE); EXPECT_FALSE(material.IsInScene(scene)); EXPECT_TRUE(primary.IsInScene(scene)); EXPECT_TRUE(behavior.IsInScene(scene)); material.RemoveAllScripts();
}
TEST_F(MaterialLifecycleTest, RemoveWithDependenciesRemovesPrimaryTextureAndScriptsButKeepsAuxiliaryTexture) {
    RCKMaterial material(Context); RCKTexture primary(Context), auxiliary(Context); CKBehavior *behaviorObject = static_cast<CKBehavior *>(Context->CreateObject(CKCID_BEHAVIOR)); ASSERT_NE(nullptr, behaviorObject); CKBehavior &behavior = *behaviorObject; behavior.SetType(CKBEHAVIORTYPE_SCRIPT);
    ASSERT_EQ(CK_OK, material.AddScript(&behavior)); material.SetTexture(0, reinterpret_cast<CKTexture *>(&primary)); material.SetTexture(2, reinterpret_cast<CKTexture *>(&auxiliary));
    CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); ASSERT_NE(nullptr, scene);
    // Independently add resources so the missing old Add override cannot hide the Remove defect.
    scene->AddObject(&material); scene->AddObject(&primary); scene->AddObject(&auxiliary); scene->AddObject(&behavior);
    material.RemoveFromScene(scene, TRUE); EXPECT_FALSE(material.IsInScene(scene)); EXPECT_FALSE(primary.IsInScene(scene));
    EXPECT_FALSE(behavior.IsInScene(scene)); EXPECT_TRUE(auxiliary.IsInScene(scene)); material.RemoveAllScripts();
}
TEST_F(MaterialLifecycleTest, RemovingFromOneSceneKeepsOtherMaterialAndPrimaryTextureMembership) {
    RCKMaterial material(Context); RCKTexture texture(Context); material.SetTexture(0, reinterpret_cast<CKTexture *>(&texture));
    CKScene *first = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); CKScene *second = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE));
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); first->AddObject(&material); first->AddObject(&texture); second->AddObject(&material); second->AddObject(&texture);
    material.RemoveFromScene(first, TRUE); EXPECT_FALSE(material.IsInScene(first)); EXPECT_FALSE(texture.IsInScene(first));
    EXPECT_TRUE(material.IsInScene(second)); EXPECT_TRUE(texture.IsInScene(second));
}
