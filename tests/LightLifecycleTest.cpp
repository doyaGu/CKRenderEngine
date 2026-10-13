#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKLight.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class LightLifecycleProbe : public RCKLight {
public:
    explicit LightLifecycleProbe(CKContext *context) : RCKLight(context) {}
    const CKLightData &Data() const { return m_LightData; }
    CKDWORD Flags() const { return m_Flags; }
    static void RecordLayout(RCKLight &light) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&light);
        std::size_t previous = sizeof(RCK3dEntity);
#define FIELD(field) { auto member = &LightLifecycleProbe::field; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(light.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(light.*member), sizeof(RCKLight)); \
        EXPECT_EQ(0u, address % alignof(decltype(light.*member))); \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(light.*member))); \
        previous = offset + sizeof(light.*member); }
        FIELD(m_LightData); FIELD(m_Flags); FIELD(m_LightPower);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKLight)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKLight)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class LightLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
}

TEST_F(LightLifecycleTest, RegisteredIdentityDefaultsAndActualPlatformLayout) {
    RCKLight *light = static_cast<RCKLight *>(Context->CreateObject(CKCID_LIGHT)); ASSERT_NE(nullptr, light);
    EXPECT_EQ(38, light->GetClassID()); EXPECT_EQ(light, Context->GetObject(light->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(light, CKCID_3DENTITY)); EXPECT_TRUE(CKIsChildClassOf(light, CKCID_RENDEROBJECT));
    EXPECT_EQ(VX_LIGHTPOINT, light->GetType()); EXPECT_TRUE(light->GetActivity());
    EXPECT_FALSE(light->GetSpecularFlag()); EXPECT_EQ(1.0f, light->GetLightPower()); EXPECT_EQ(nullptr, light->GetSkin());
    LightLifecycleProbe::RecordLayout(*light);
}

TEST_F(LightLifecycleTest, ConstructorInitializesNativeLightDataColorsAndAttenuation) {
    LightLifecycleProbe light(Context); const CKLightData &data = light.Data();
    EXPECT_EQ(0x100u, light.Flags()); EXPECT_EQ(VX_LIGHTPOINT, data.Type);
    EXPECT_EQ(1.0f, data.Diffuse.r); EXPECT_EQ(1.0f, data.Diffuse.g);
    EXPECT_EQ(1.0f, data.Diffuse.b); EXPECT_EQ(1.0f, data.Diffuse.a);
    EXPECT_EQ(0.0f, data.Specular.r); EXPECT_EQ(0.0f, data.Specular.g);
    EXPECT_EQ(0.0f, data.Specular.b); EXPECT_EQ(0.0f, data.Specular.a);
    EXPECT_EQ(0.0f, data.Ambient.r); EXPECT_EQ(0.0f, data.Ambient.g);
    EXPECT_EQ(0.0f, data.Ambient.b); EXPECT_EQ(1.0f, data.Ambient.a);
    EXPECT_EQ(0.0f, data.Position.x); EXPECT_EQ(0.0f, data.Position.y); EXPECT_EQ(0.0f, data.Position.z);
    EXPECT_EQ(0.0f, data.Direction.x); EXPECT_EQ(0.0f, data.Direction.y); EXPECT_EQ(0.0f, data.Direction.z);
    EXPECT_EQ(5000.0f, data.Range); EXPECT_EQ(1.0f, data.Falloff);
    EXPECT_EQ(1.0f, data.Attenuation0); EXPECT_EQ(0.0f, data.Attenuation1); EXPECT_EQ(0.0f, data.Attenuation2);
    EXPECT_EQ(0.69813174f, data.InnerSpotCone); EXPECT_EQ(0.78539819f, data.OuterSpotCone);
}

TEST_F(LightLifecycleTest, NamedConstructorPreservesContextIdentityAndInheritedState) {
    char name[] = "Native light lifecycle"; RCKLight light(Context, name);
    EXPECT_STREQ(name, light.GetName()); EXPECT_EQ(&light, Context->GetObject(light.GetID()));
    EXPECT_EQ(CKCID_LIGHT, light.GetClassID()); EXPECT_TRUE(light.IsVisible());
    EXPECT_EQ(nullptr, light.GetParent()); EXPECT_EQ(nullptr, light.GetSkin());
}

TEST_F(LightLifecycleTest, ClassNameMatchesNativeDisplayName) {
    EXPECT_STREQ("Light", RCKLight::GetClassName());
}

TEST_F(LightLifecycleTest, RegistrationPublishesNativeLightParameter) {
    CKClassDesc *description = CKGetClassDesc(CKCID_LIGHT); ASSERT_NE(nullptr, description);
    description->Parameter = CKGUID(); RCKLight::Register();
    EXPECT_EQ(CKPGUID_LIGHT, description->Parameter);
    EXPECT_EQ(0x4b6d412fu, description->Parameter.d1); EXPECT_EQ(0x5d1e1416u, description->Parameter.d2);
}

TEST_F(LightLifecycleTest, DirectFactoryVirtualDestructorReleasesOwnedSkinAndRegistration) {
    CKLight *api = RCKLight::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKLight *light = reinterpret_cast<RCKLight *>(api);
    EXPECT_EQ(CKCID_LIGHT, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    EXPECT_EQ(1.0f, api->GetLightPower()); const CK_ID id = light->GetID();
    ASSERT_NE(nullptr, light->CreateSkin()); light->GetSkin()->SetBoneCount(2); light->GetSkin()->SetVertexCount(3);
    delete static_cast<CKObject *>(light); EXPECT_EQ(nullptr, Context->GetObject(id));
}

TEST_F(LightLifecycleTest, ContextDeletionUnlinksLightFromSurvivingParent) {
    new RCKRenderManager(Context); RCK3dEntity parent(Context);
    RCKLight *light = static_cast<RCKLight *>(Context->CreateObject(CKCID_LIGHT)); ASSERT_NE(nullptr, light);
    light->SetParent(reinterpret_cast<CK3dEntity *>(&parent), FALSE); ASSERT_EQ(1, parent.GetChildrenCount());
    const CK_ID id = light->GetID(); CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObject(light, 0, &dependencies));
    EXPECT_EQ(0, parent.GetChildrenCount()); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&parent, Context->GetObject(parent.GetID()));
}

TEST_F(LightLifecycleTest, MemoryOccupationAddsActualPlatformTailToInheritedStorage) {
    RCKLight light(Context); const int tail = static_cast<int>(sizeof(RCKLight) - sizeof(RCK3dEntity));
    EXPECT_GE(tail, 112); EXPECT_EQ(light.RCK3dEntity::GetMemoryOccupation() + tail, light.GetMemoryOccupation());
    char name[] = "Light memory"; light.SetName(name); light.SetRange(123.0f); light.SetLightPower(-2.0f);
    EXPECT_EQ(light.RCK3dEntity::GetMemoryOccupation() + tail, light.GetMemoryOccupation());
    CKSkin *skin = light.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(2); skin->SetVertexCount(3);
    EXPECT_EQ(light.RCK3dEntity::GetMemoryOccupation() + tail, light.GetMemoryOccupation());
}
