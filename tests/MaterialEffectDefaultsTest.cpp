#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKMesh.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKRenderedScene.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class EffectDefaultsObserver : public MaterialRenderObserver {
public:
    CKDWORD DrawTextures[3] = {}, DrawCoordinates[3] = {}, DrawOperations[3] = {};
    CKDWORD DrawTransformFlags[3] = {}, DrawBump00 = 0, DrawBump11 = 0, DrawFlags = 0;
    Vx2DVector DrawUV;
    VxVector DrawNormal;
    int DrawCount = 0;
    CKBOOL HasUV = FALSE, HasNormal = FALSE;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count); EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        if (!indices || !data) return FALSE;
        ++DrawCount; DrawFlags = data->Flags;
        for (int stage = 0; stage < 3; ++stage) {
            DrawTextures[stage] = Textures[stage];
            DrawCoordinates[stage] = StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
            DrawOperations[stage] = StageStates[stage][CKRST_TSS_OP];
            DrawTransformFlags[stage] = StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        }
        DrawBump00 = StageStates[1][CKRST_TSS_BUMPENVMAT00];
        DrawBump11 = StageStates[1][CKRST_TSS_BUMPENVMAT11];
        HasUV = data->TexCoordPtr != nullptr; HasNormal = data->NormalPtr != nullptr;
        if (HasUV) std::memcpy(&DrawUV, static_cast<CKBYTE *>(data->TexCoordPtr) + indices[0] * data->TexCoordStride, sizeof(DrawUV));
        if (HasNormal) std::memcpy(&DrawNormal, static_cast<CKBYTE *>(data->NormalPtr) + indices[0] * data->NormalStride, sizeof(DrawNormal));
        return TRUE;
    }
};
// Texture residency/storage is outside this fixture. The real material entry
// calls this SDK virtual and the observer records its actual stage binding.
class DefaultsTexture : public RCKTexture {
public:
    CKDWORD Handle;
    DefaultsTexture(CKContext *context, CKDWORD handle) : RCKTexture(context), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        return static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(Handle, stage);
    }
};
class MaterialEffectDefaultsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    EffectDefaultsObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context); Device->DetachAll(); Device->m_RasterizerContext = &State;
        ASSERT_NE(nullptr, Device->m_RenderedScene->GetRootEntity());
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Parameter(RCKMaterial &material, CKGUID type, int members, const char *name = "Params") {
        CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter);
        EXPECT_EQ(type, parameter->GetGUID()); EXPECT_STREQ(name, parameter->GetName());
        EXPECT_EQ(parameter, Context->GetObject(parameter->GetID()));
        CKStructHelper helper(parameter); EXPECT_EQ(members, helper.GetMemberCount());
    }
    void Dword(RCKMaterial &material, int index, CKGUID type, CKDWORD expected) {
        CKStructHelper helper(material.GetEffectParameter()); ASSERT_LT(index, helper.GetMemberCount());
        ASSERT_NE(nullptr, helper[index]); EXPECT_EQ(type, helper[index]->GetGUID());
        CKDWORD actual = 0xFFFFFFFFu; ASSERT_EQ(CK_OK, helper[index]->GetValue(&actual)); EXPECT_EQ(expected, actual);
    }
    void BumpMembers(RCKMaterial &material) {
        Parameter(material, CKPGUID_BUMPMAPPARAM, 4);
        CKStructHelper helper(material.GetEffectParameter()); ASSERT_EQ(4, helper.GetMemberCount());
        ASSERT_NE(nullptr, helper[0]); EXPECT_EQ(CKPGUID_FLOAT, helper[0]->GetGUID());
        float offset = -19; ASSERT_EQ(CK_OK, helper[0]->GetValue(&offset)); EXPECT_FLOAT_EQ(0, offset);
        Dword(material, 1, CKPGUID_TEXCOMBINE, CKRST_TOP_ADD);
        Dword(material, 2, CKPGUID_TEXGENEFFECT, VXEFFECT_TGREFLECT);
        Dword(material, 3, CKPGUID_3DENTITY, 0);
    }
    void ColorAndAlpha(int stage) {
        EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[stage][CKRST_TSS_OP]);
        EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[stage][CKRST_TSS_AOP]);
        EXPECT_EQ(CKRST_TA_TEXTURE, State.StageStates[stage][CKRST_TSS_ARG1]);
        EXPECT_EQ(CKRST_TA_CURRENT, State.StageStates[stage][CKRST_TSS_ARG2]);
        EXPECT_EQ(CKRST_TA_TEXTURE, State.StageStates[stage][CKRST_TSS_AARG1]);
        EXPECT_EQ(CKRST_TA_CURRENT, State.StageStates[stage][CKRST_TSS_AARG2]);
    }
    void PassThrough(int stage) {
        EXPECT_EQ(CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_PASSTHRU), State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX]);
        EXPECT_EQ(CKRST_TTF_NONE, State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            EXPECT_FLOAT_EQ(row == column ? 1 : 0, State.Matrices[VXMATRIX_TEXTURE0 + stage][row][column]);
    }
    void Reflection(int stage) {
        EXPECT_EQ(CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX]);
        EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
        const VxMatrix &matrix = State.Matrices[VXMATRIX_TEXTURE0 + stage];
        EXPECT_FLOAT_EQ(.4f, matrix[0][0]); EXPECT_FLOAT_EQ(-.4f, matrix[1][1]); EXPECT_FLOAT_EQ(.4f, matrix[2][2]);
        EXPECT_FLOAT_EQ(.5f, matrix[3][0]); EXPECT_FLOAT_EQ(.5f, matrix[3][1]); EXPECT_FLOAT_EQ(0, matrix[3][2]);
    }
    static CKDWORD Bits(float value) { CKDWORD bits; std::memcpy(&bits, &value, sizeof(bits)); return bits; }
};
}

TEST_F(MaterialEffectDefaultsTest, TexGenReferentialCreatesReflectAndNullMembersWithoutExplicitParameterWrites) {
    RCKMaterial material(Context); material.SetEffect(VXEFFECT_TEXGENREF);
    Parameter(material, CKPGUID_TEXGENREFEFFECT, 2, "TexGen Params");
    Dword(material, 0, CKPGUID_TEXGENEFFECT, VXEFFECT_TGREFLECT); Dword(material, 1, CKPGUID_3DENTITY, 0);
}
TEST_F(MaterialEffectDefaultsTest, CombineTwoCreatesModulateNoneAndNullMembersWithoutExplicitParameterWrites) {
    RCKMaterial material(Context); material.SetEffect(VXEFFECT_2TEXTURES);
    Parameter(material, CKPGUID_COMBINE2TEX, 3);
    Dword(material, 0, CKPGUID_TEXCOMBINE, CKRST_TOP_MODULATE);
    Dword(material, 1, CKPGUID_TEXGENEFFECT, VXEFFECT_TGNONE); Dword(material, 2, CKPGUID_3DENTITY, 0);
}
TEST_F(MaterialEffectDefaultsTest, CombineThreeCreatesBothIndependentDefaultTripletsWithoutExplicitParameterWrites) {
    RCKMaterial material(Context); material.SetEffect(VXEFFECT_3TEXTURES);
    Parameter(material, CKPGUID_COMBINE3TEX, 6);
    Dword(material, 0, CKPGUID_TEXCOMBINE, CKRST_TOP_MODULATE);
    Dword(material, 1, CKPGUID_TEXGENEFFECT, VXEFFECT_TGNONE); Dword(material, 2, CKPGUID_3DENTITY, 0);
    Dword(material, 3, CKPGUID_TEXCOMBINE, CKRST_TOP_MODULATE);
    Dword(material, 4, CKPGUID_TEXGENEFFECT, VXEFFECT_TGNONE); Dword(material, 5, CKPGUID_3DENTITY, 0);
}
TEST_F(MaterialEffectDefaultsTest, BumpCreatesZeroOffsetAddReflectAndNullMembersWithoutExplicitParameterWrites) {
    RCKMaterial material(Context); material.SetEffect(VXEFFECT_BUMPENV); BumpMembers(material);
}
TEST_F(MaterialEffectDefaultsTest, TexGenReferentialDefaultActuallyGeneratesReflectionAtRequestedStage) {
    RCKMaterial material(Context); DefaultsTexture texture(Context, 41); material.SetTexture(0, &texture);
    material.SetEffect(VXEFFECT_TEXGENREF); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 2));
    EXPECT_EQ(41u, State.Textures[2]); Reflection(2);
}
TEST_F(MaterialEffectDefaultsTest, CombineTwoAutomaticDefaultActuallyModulatesExtraTextureWithItsOwnPassThroughUV) {
    RCKMaterial material(Context); DefaultsTexture base(Context, 41), extra(Context, 42);
    material.SetTexture(0, &base); material.SetTexture(1, &extra); material.SetEffect(VXEFFECT_2TEXTURES);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]); ColorAndAlpha(1); PassThrough(1);
}
TEST_F(MaterialEffectDefaultsTest, CombineThreeAutomaticDefaultsActuallyModulateBothExtraTexturesWithIndependentUVs) {
    RCKMaterial material(Context); DefaultsTexture base(Context, 41), first(Context, 42), second(Context, 43);
    material.SetTexture(0, &base); material.SetTexture(1, &first); material.SetTexture(2, &second);
    material.SetEffect(VXEFFECT_3TEXTURES); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]); EXPECT_EQ(43u, State.Textures[2]);
    ColorAndAlpha(1); ColorAndAlpha(2); PassThrough(1); PassThrough(2);
}
TEST_F(MaterialEffectDefaultsTest, BumpAutomaticDefaultActuallyUsesAmplitudeTwoAndAddReflectionEnvironment) {
    RCKMaterial material(Context); DefaultsTexture base(Context, 41), bump(Context, 42), environment(Context, 43);
    material.SetTexture(0, &base); material.SetTexture(1, &bump); material.SetTexture(2, &environment);
    material.SetEffect(VXEFFECT_BUMPENV); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]); EXPECT_EQ(43u, State.Textures[2]);
    EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.StageStates[1][CKRST_TSS_OP]);
    EXPECT_EQ(Bits(2.0f), State.StageStates[1][CKRST_TSS_BUMPENVMAT00]); EXPECT_EQ(Bits(2.0f), State.StageStates[1][CKRST_TSS_BUMPENVMAT11]);
    EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_BUMPENVMAT01]); EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_BUMPENVMAT10]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_PASSTHRU), State.StageStates[1][CKRST_TSS_TEXCOORDINDEX]);
    EXPECT_EQ(CKRST_TOP_ADD, State.StageStates[2][CKRST_TSS_OP]); EXPECT_EQ(CKRST_TOP_SELECTARG2, State.StageStates[2][CKRST_TSS_AOP]);
    Reflection(2); BumpMembers(material);
}
TEST_F(MaterialEffectDefaultsTest, BumpWithoutOptionalBumpTextureStillUsesAutomaticAddReflectionAtStageOne) {
    RCKMaterial material(Context); DefaultsTexture base(Context, 41), environment(Context, 43);
    material.SetTexture(0, &base); material.SetTexture(2, &environment); material.SetEffect(VXEFFECT_BUMPENV);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(43u, State.Textures[1]); EXPECT_EQ(0u, State.Textures[2]);
    EXPECT_EQ(CKRST_TOP_ADD, State.StageStates[1][CKRST_TSS_OP]); Reflection(1);
}
TEST_F(MaterialEffectDefaultsTest, SwitchingRealParameterTypeFromCombineThreeToBumpAppliesAllNewDefaults) {
    RCKMaterial material(Context); material.SetEffect(VXEFFECT_3TEXTURES);
    CKParameter *parameter = material.GetEffectParameter(); ASSERT_NE(nullptr, parameter); const CK_ID id = parameter->GetID();
    material.SetEffect(VXEFFECT_BUMPENV); ASSERT_EQ(parameter, material.GetEffectParameter()); EXPECT_EQ(id, material.GetEffectParameter()->GetID());
    BumpMembers(material);
}
TEST_F(MaterialEffectDefaultsTest, RenderChannelsConsumesUneditedBumpDefaultsAtActualDrawWithPrivateUVAndNormals) {
    RCKMesh mesh(Context); RCKMaterial channel(Context); DefaultsTexture base(Context, 41), bump(Context, 42), environment(Context, 43);
    ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1)); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetLitMode(VX_LITMESH);
    for (int i = 0; i < 3; ++i) {
        VxVector position(static_cast<float>(i), 0, 1), normal(0, 0, 1);
        mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
        mesh.SetVertexTextureCoordinates(i, .125f, .25f, -1);
    }
    channel.SetTexture(0, &base); channel.SetTexture(1, &bump); channel.SetTexture(2, &environment); channel.SetEffect(VXEFFECT_BUMPENV);
    const int index = mesh.AddChannel(&channel, FALSE); ASSERT_GE(index, 0); mesh.ActivateChannel(index, TRUE);
    mesh.SetChannelSourceBlend(index, VXBLEND_ONE); mesh.SetChannelDestBlend(index, VXBLEND_ONE);
    for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, .375f, .625f, index);
    VxDrawPrimitiveData data = {}; data.VertexCount = 3; data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
    data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride); data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
    data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
    data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride); data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(41u, State.DrawTextures[0]); EXPECT_EQ(42u, State.DrawTextures[1]); EXPECT_EQ(43u, State.DrawTextures[2]);
    EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.DrawOperations[1]); EXPECT_EQ(CKRST_TOP_ADD, State.DrawOperations[2]);
    EXPECT_EQ(Bits(2.0f), State.DrawBump00); EXPECT_EQ(Bits(2.0f), State.DrawBump11);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_PASSTHRU), State.DrawCoordinates[1]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.DrawCoordinates[2]);
    EXPECT_EQ(CKRST_TTF_COUNT2, State.DrawTransformFlags[2]);
    ASSERT_TRUE(State.HasUV); EXPECT_FLOAT_EQ(.375f, State.DrawUV.x); EXPECT_FLOAT_EQ(.625f, State.DrawUV.y);
    ASSERT_TRUE(State.HasNormal); EXPECT_FLOAT_EQ(1, State.DrawNormal.z); EXPECT_NE(0u, State.DrawFlags & CKRST_DP_LIGHT);
    BumpMembers(channel);
}
