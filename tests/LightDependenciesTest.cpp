#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKLight.h"
#include "RCKSkin.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DependencyLight : public RCKLight {
public:
    explicit DependencyLight(CKContext *context) : RCKLight(context) {}
    CKLightData &Data() { return m_LightData; }
    CKDWORD LightFlags() const { return m_Flags; }
    void LightFlags(CKDWORD flags) { m_Flags = flags; }
};
class LightDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Options(CKDependencies &options, CKDWORD entity = 0, CKDWORD light = 0) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM;
        options[CKCID_3DENTITY] = entity; options[CKCID_LIGHT] = light;
    }
    CKERROR Copy(RCKLight &destination, CKObject &source) {
        CKDependencies options; Options(options); CKDependenciesContext dependencies(Context);
        dependencies.StartDependencies(&options); CKERROR result = destination.Copy(source, dependencies);
        dependencies.StopDependencies(); return result;
    }
    static void Fill(DependencyLight &light) {
        CKLightData &data = light.Data(); data.Type = VX_LIGHTSPOT;
        data.Diffuse = VxColor(.125f, .25f, .375f, .5f);
        data.Specular = VxColor(.625f, .75f, .875f, 1.0f);
        data.Ambient = VxColor(-.125f, -.25f, -.375f, -.5f);
        data.Position = VxVector(11, 13, 17); data.Direction = VxVector(19, 23, 29);
        data.Range = 307; data.Falloff = .3125f;
        data.Attenuation0 = 3; data.Attenuation1 = 5; data.Attenuation2 = 7;
        data.InnerSpotCone = .4375f; data.OuterSpotCone = .9375f;
        light.LightFlags(0x52100300); light.SetLightPower(-2.75f);
    }
    static void Color(const VxColor &expected, const VxColor &actual) {
        EXPECT_EQ(expected.r, actual.r); EXPECT_EQ(expected.g, actual.g);
        EXPECT_EQ(expected.b, actual.b); EXPECT_EQ(expected.a, actual.a);
    }
    static void Vector(const VxVector &expected, const VxVector &actual) {
        EXPECT_EQ(expected.x, actual.x); EXPECT_EQ(expected.y, actual.y); EXPECT_EQ(expected.z, actual.z);
    }
    static void Data(const CKLightData &expected, const CKLightData &actual) {
        EXPECT_EQ(expected.Type, actual.Type); Color(expected.Diffuse, actual.Diffuse);
        Color(expected.Specular, actual.Specular); Color(expected.Ambient, actual.Ambient);
        Vector(expected.Position, actual.Position); Vector(expected.Direction, actual.Direction);
        EXPECT_EQ(expected.Range, actual.Range); EXPECT_EQ(expected.Falloff, actual.Falloff);
        EXPECT_EQ(expected.Attenuation0, actual.Attenuation0); EXPECT_EQ(expected.Attenuation1, actual.Attenuation1);
        EXPECT_EQ(expected.Attenuation2, actual.Attenuation2); EXPECT_EQ(expected.InnerSpotCone, actual.InnerSpotCone);
        EXPECT_EQ(expected.OuterSpotCone, actual.OuterSpotCone);
    }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
};
}

TEST_F(LightDependenciesTest, OwnDependencyCountIsZeroForEveryMode) {
    for (int mode = -3; mode <= 16; ++mode) EXPECT_EQ(0, RCKLight::GetDependenciesCount(mode));
}
TEST_F(LightDependenciesTest, OwnDependencyLabelsAreNullForEveryIndexAndMode) {
    for (int mode = -3; mode <= 16; ++mode)
        for (int index = -2; index <= 8; ++index) EXPECT_EQ(nullptr, RCKLight::GetDependencies(index, mode));
}
TEST_F(LightDependenciesTest, CopyTransfersEveryLightDataFieldAndCompleteTail) {
    DependencyLight source(Context), destination(Context); Fill(source);
    ASSERT_EQ(CK_OK, Copy(destination, source)); Data(source.Data(), destination.Data());
    EXPECT_EQ(source.LightFlags(), destination.LightFlags()); EXPECT_EQ(-2.75f, destination.GetLightPower());
}
TEST_F(LightDependenciesTest, CopyResetsDestinationActivitySpecularAndPrivateFlags) {
    DependencyLight source(Context), destination(Context); Fill(destination);
    source.Active(FALSE); source.SetSpecularFlag(FALSE); source.LightFlags(0); source.SetLightPower(.0625f);
    ASSERT_EQ(CK_OK, Copy(destination, source)); EXPECT_FALSE(destination.GetActivity());
    EXPECT_FALSE(destination.GetSpecularFlag()); EXPECT_EQ(0u, destination.LightFlags()); EXPECT_EQ(.0625f, destination.GetLightPower());
}
TEST_F(LightDependenciesTest, CopyPreservesUnrestrictedTypeAndNegativePowerValues) {
    DependencyLight source(Context), destination(Context);
    const int types[] = {VX_LIGHTPOINT, VX_LIGHTSPOT, VX_LIGHTDIREC, 0, 99};
    for (int type : types) {
        source.SetType(static_cast<VXLIGHT_TYPE>(type)); source.SetLightPower(-static_cast<float>(type) - .5f);
        ASSERT_EQ(CK_OK, Copy(destination, source)); EXPECT_EQ(source.GetType(), destination.GetType());
        EXPECT_EQ(source.GetLightPower(), destination.GetLightPower());
    }
}
TEST_F(LightDependenciesTest, CopyKeepsSourceDataAndIdentityIndependent) {
    DependencyLight source(Context), destination(Context); Fill(source);
    const CK_ID sourceID = source.GetID(), destinationID = destination.GetID(); CKLightData original = source.Data();
    ASSERT_EQ(CK_OK, Copy(destination, source)); destination.SetColor(VxColor(9.0f, 8.0f, 7.0f, 6.0f));
    destination.Active(FALSE); destination.SetLightPower(17); Data(original, source.Data());
    EXPECT_EQ(0x52100300u, source.LightFlags()); EXPECT_EQ(-2.75f, source.GetLightPower());
    EXPECT_EQ(sourceID, source.GetID()); EXPECT_EQ(destinationID, destination.GetID());
}
TEST_F(LightDependenciesTest, CopyRetainsInheritedParentWorldTransformAndZOrder) {
    DependencyLight source(Context), destination(Context); RCK3dEntity parent(Context);
    source.SetParent(Entity(parent), TRUE); source.SetZOrder(37); VxVector position(13, 17, 19);
    source.SetPosition(&position, nullptr, FALSE); VxMatrix world = source.GetWorldMatrix();
    ASSERT_EQ(CK_OK, Copy(destination, source)); EXPECT_EQ(Entity(parent), destination.GetParent());
    EXPECT_EQ(37, destination.GetZOrder());
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        EXPECT_EQ(world[row][column], destination.GetWorldMatrix()[row][column]);
    source.SetParent(nullptr, TRUE); destination.SetParent(nullptr, TRUE);
}
TEST_F(LightDependenciesTest, CopyDeepCopiesOwnedSkinBonesAndVertexWeights) {
    DependencyLight source(Context), destination(Context); RCK3dEntity bone(Context);
    CKSkin *previous = destination.CreateSkin(); ASSERT_NE(nullptr, previous);
    previous->SetBoneCount(3); previous->SetVertexCount(1); previous->GetVertexData(0)->SetBoneCount(1);
    previous->GetVertexData(0)->SetBone(0, 2); previous->GetVertexData(0)->SetWeight(0, .125f);
    CKSkin *skin = source.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1); skin->SetVertexCount(1);
    skin->GetBoneData(0)->SetBone(Entity(bone)); CKSkinVertexData *vertex = skin->GetVertexData(0);
    vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, .75f); VxVector initial(2, 3, 5); vertex->SetInitialPos(initial);
    ASSERT_EQ(CK_OK, Copy(destination, source)); CKSkin *copied = destination.GetSkin();
    ASSERT_NE(nullptr, copied); EXPECT_NE(skin, copied); EXPECT_EQ(1, copied->GetBoneCount()); EXPECT_EQ(1, copied->GetVertexCount());
    EXPECT_EQ(Entity(bone), copied->GetBoneData(0)->GetBone()); EXPECT_EQ(1, copied->GetVertexData(0)->GetBoneCount());
    EXPECT_EQ(0, copied->GetVertexData(0)->GetBone(0)); EXPECT_EQ(.75f, copied->GetVertexData(0)->GetWeight(0));
    Vector(initial, copied->GetVertexData(0)->GetInitialPos()); copied->GetVertexData(0)->SetWeight(0, .125f);
    EXPECT_EQ(.75f, vertex->GetWeight(0));
}
TEST_F(LightDependenciesTest, SourceWithoutSkinRetainsExistingDestinationSkinContract) {
    DependencyLight source(Context), destination(Context); CKSkin *skin = destination.CreateSkin();
    ASSERT_NE(nullptr, skin); skin->SetBoneCount(2); ASSERT_EQ(CK_OK, Copy(destination, source));
    EXPECT_EQ(skin, destination.GetSkin()); EXPECT_EQ(2, skin->GetBoneCount());
}
TEST_F(LightDependenciesTest, SelfCopyKeepsOwnedSkinReadinessParentAndAllLightFields) {
    DependencyLight light(Context); RCK3dEntity parent(Context); Fill(light); light.SetParent(Entity(parent), TRUE);
    CKSkin *skin = light.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(2); CKLightData data = light.Data();
    light.ModifyObjectFlags(0, CK_OBJECT_UPTODATE); const CKDWORD objectFlags = light.GetObjectFlags();
    const CKDWORD moveableFlags = light.GetMoveableFlags(); const int children = parent.GetChildrenCount();
    ASSERT_EQ(CK_OK, Copy(light, light)); EXPECT_EQ(skin, light.GetSkin()); EXPECT_EQ(2, light.GetSkin()->GetBoneCount());
    EXPECT_EQ(objectFlags, light.GetObjectFlags()); EXPECT_EQ(moveableFlags, light.GetMoveableFlags());
    EXPECT_EQ(Entity(parent), light.GetParent()); EXPECT_EQ(children, parent.GetChildrenCount());
    Data(data, light.Data()); EXPECT_EQ(0x52100300u, light.LightFlags()); EXPECT_EQ(-2.75f, light.GetLightPower());
    light.SetParent(nullptr, TRUE);
}
TEST_F(LightDependenciesTest, ActualContextCopyCopiesLightTailAndOwnedSkinWithoutOwnDependencyBits) {
    RCKLight *source = static_cast<RCKLight *>(Context->CreateObject(CKCID_LIGHT)); ASSERT_NE(nullptr, source);
    source->Active(FALSE); source->SetSpecularFlag(TRUE); source->SetLightPower(-3.25f);
    source->SetColor(VxColor(.25f, .5f, .75f, 1.0f)); source->SetRange(731); source->SetType(VX_LIGHTSPOT);
    CKSkin *sourceSkin = source->CreateSkin(); ASSERT_NE(nullptr, sourceSkin); sourceSkin->SetBoneCount(2);
    for (int mask = 0; mask < 2; ++mask) {
        CKDependencies options; Options(options, 0, mask ? 0xffffffffu : 0);
        CKObject *object = Context->CopyObject(source, &options); ASSERT_NE(nullptr, object); EXPECT_NE(source, object);
        RCKLight *copy = static_cast<RCKLight *>(object); EXPECT_FALSE(copy->GetActivity()); EXPECT_TRUE(copy->GetSpecularFlag());
        EXPECT_EQ(-3.25f, copy->GetLightPower()); Color(source->GetColor(), copy->GetColor());
        EXPECT_EQ(731, copy->GetRange()); EXPECT_EQ(VX_LIGHTSPOT, copy->GetType());
        ASSERT_NE(nullptr, copy->GetSkin()); EXPECT_NE(sourceSkin, copy->GetSkin()); EXPECT_EQ(2, copy->GetSkin()->GetBoneCount());
    }
}
