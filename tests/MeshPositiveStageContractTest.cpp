#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
const VXMATRIX_TYPE PositiveTextureMatrix = static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + 1);
class PositiveStageObserver : public TextureTestObserver {
public:
    int FailStage = -1, Rejections = 0;
    CKRST_TEXTURESTAGESTATETYPE FailState = CKRST_TSS_STAGEBLEND;
    CKBOOL FailMatrix = FALSE;
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        if (stage == FailStage && state == FailState) { ++Rejections; return FALSE; }
        return TextureTestObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        if (FailMatrix && type == PositiveTextureMatrix) { ++Rejections; return FALSE; }
        return TextureTestObserver::SetTransformMatrix(type, matrix);
    }
};
class PositiveStageMesh : public RCKMesh {
public:
    explicit PositiveStageMesh(CKContext *context) : RCKMesh(context) {}
    void Select(int index) { m_ActiveTextureChannels.PushBack(index); }
};
struct PositiveOwner { int Calls = 0; VX_EFFECTCALLBACK_RETVAL EffectResult = VXEFFECTRETVAL_SKIPTEXMAT; };
void WritePositiveOwnership(CKRenderContext *context) {
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    state->SetTexture(47, 1);
    state->SetTextureStageState(1, CKRST_TSS_TEXCOORDINDEX, 3);
    state->SetTextureStageState(1, CKRST_TSS_STAGEBLEND, 0x65);
    state->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CW);
}
int PositiveMaterialOwner(CKRenderContext *context, CKMaterial *, void *argument) {
    ++static_cast<PositiveOwner *>(argument)->Calls; WritePositiveOwnership(context); return TRUE;
}
VX_EFFECTCALLBACK_RETVAL PositiveEffectOwner(CKRenderContext *context, CKMaterial *, int stage, void *argument) {
    PositiveOwner *owner = static_cast<PositiveOwner *>(argument); ++owner->Calls;
    WritePositiveOwnership(context);
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    state->SetRenderState(VXRENDERSTATE_TEXTUREFACTOR, 0xFFAABBCCu);
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 6.25f;
    state->SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), matrix);
    state->SetTextureStageState(stage, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
    state->SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX,
        CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_CAMERASPACENORMAL));
    return owner->EffectResult;
}
class PositiveVirtualOwner : public RCKMaterial {
public:
    int Calls = 0;
    explicit PositiveVirtualOwner(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        EXPECT_EQ(1, stage); ++Calls; WritePositiveOwnership(context); return TRUE;
    }
};
class MeshPositiveStageContractTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    PositiveStageObserver State; CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
        VxImageDescEx alpha; VxPixelFormat2ImageDesc(_32_ARGB8888, alpha); Manager->m_Drivers[0].TextureFormats.PushBack(alpha);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0)); Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0;
        State.MaterialWrites = 0;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Pixel(RCKTexture &texture) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        CKDWORD value = 0xFF2468AC; memcpy(pixels, &value, 4); texture.ReleaseSurfacePtr(0); texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    void Select(PositiveStageMesh &mesh, RCKMaterial &material, RCKTexture &texture) {
        Pixel(texture); material.SetTexture(0, &texture); int channel = mesh.AddChannel(&material, FALSE); ASSERT_GE(channel, 0);
        mesh.SetChannelSourceBlend(channel, VXBLEND_ZERO); mesh.SetChannelDestBlend(channel, VXBLEND_SRCCOLOR);
        mesh.ActivateChannel(channel, TRUE); mesh.Select(channel);
    }
    static void Configure(RCKMaterial &material) {
        material.SetTwoSided(TRUE); material.SetFillMode(VXFILL_WIREFRAME); material.SetShadeMode(VXSHADE_FLAT);
        material.EnableZWrite(FALSE); material.SetZFunc(VXCMP_GREATER); material.EnableAlphaBlend(TRUE);
        material.EnablePerspectiveCorrection(FALSE); material.EnableAlphaTest(FALSE);
        material.SetTextureMinMode(VXTEXTUREFILTER_LINEAR); material.SetTextureMagMode(VXTEXTUREFILTER_NEAREST);
        material.SetTextureBorderColor(0xFFABCDEF); material.SetTextureAddressMode(VXTEXTURE_ADDRESSWRAP);
        material.SetTextureBlendMode(VXTEXTUREBLEND_MODULATE);
    }
    void SeedBaseStates() {
        State.States[VXRENDERSTATE_CULLMODE] = VXCULL_CCW;
        State.States[VXRENDERSTATE_FILLMODE] = VXFILL_SOLID;
        State.States[VXRENDERSTATE_SHADEMODE] = VXSHADE_GOURAUD;
        State.States[VXRENDERSTATE_ZWRITEENABLE] = TRUE;
        State.States[VXRENDERSTATE_ZFUNC] = VXCMP_LESSEQUAL;
        State.States[VXRENDERSTATE_ALPHABLENDENABLE] = FALSE;
        State.States[VXRENDERSTATE_TEXTUREPERSPECTIVE] = TRUE;
        State.States[VXRENDERSTATE_ALPHATESTENABLE] = TRUE;
        State.States[VXRENDERSTATE_ALPHAFUNC] = VXCMP_LESS;
        State.States[VXRENDERSTATE_ALPHAREF] = 73;
        State.Textures[0] = 29; State.Textures[2] = 57;
    }
    void BaseStates() {
        EXPECT_EQ(VXCULL_CCW, State.States[VXRENDERSTATE_CULLMODE]); EXPECT_EQ(VXFILL_SOLID, State.States[VXRENDERSTATE_FILLMODE]);
        EXPECT_EQ(VXSHADE_GOURAUD, State.States[VXRENDERSTATE_SHADEMODE]); EXPECT_EQ(TRUE, State.States[VXRENDERSTATE_ZWRITEENABLE]);
        EXPECT_EQ(VXCMP_LESSEQUAL, State.States[VXRENDERSTATE_ZFUNC]); EXPECT_EQ(FALSE, State.States[VXRENDERSTATE_ALPHABLENDENABLE]);
        EXPECT_EQ(TRUE, State.States[VXRENDERSTATE_TEXTUREPERSPECTIVE]); EXPECT_EQ(TRUE, State.States[VXRENDERSTATE_ALPHATESTENABLE]);
        EXPECT_EQ(VXCMP_LESS, State.States[VXRENDERSTATE_ALPHAFUNC]); EXPECT_EQ(73u, State.States[VXRENDERSTATE_ALPHAREF]);
    }
    void Effect(RCKMaterial &material, PositiveOwner &owner) {
        VxEffectDescription description; description.Summary = "Positive stage ownership";
        description.SetCallback = PositiveEffectOwner; description.CallbackArg = &owner;
        material.SetEffect(static_cast<VX_EFFECT>(Manager->AddEffect(description)));
    }
};
}

TEST_F(MeshPositiveStageContractTest, PublicPositiveSetterKeepsOtherStagesAndActuallyAppliesGlobalMaterialState) {
    RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture); material.SetTexture(0, &texture); Configure(material); SeedBaseStates();
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 1));
    EXPECT_EQ(29u, State.Textures[0]); EXPECT_EQ(57u, State.Textures[2]); EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[1]);
    EXPECT_EQ(VXCULL_NONE, State.States[VXRENDERSTATE_CULLMODE]); EXPECT_EQ(VXFILL_WIREFRAME, State.States[VXRENDERSTATE_FILLMODE]);
    EXPECT_EQ(VXSHADE_FLAT, State.States[VXRENDERSTATE_SHADEMODE]); EXPECT_EQ(FALSE, State.States[VXRENDERSTATE_ZWRITEENABLE]);
    EXPECT_EQ(VXCMP_GREATER, State.States[VXRENDERSTATE_ZFUNC]); EXPECT_EQ(TRUE, State.States[VXRENDERSTATE_ALPHABLENDENABLE]);
    EXPECT_EQ(0, State.MaterialWrites); EXPECT_EQ(1, State.Loads);
    CKDWORD value; memcpy(&value, State.Uploads[0].Bytes, 4); EXPECT_EQ(0xFF2468ACu, value);
}
TEST_F(MeshPositiveStageContractTest, PublicPositiveDefaultTextureSendsActualSamplerMapBlendAndIdentityMatrix) {
    RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture); material.SetTexture(0, &texture); Configure(material);
    State.Matrices[PositiveTextureMatrix][3][0] = 9.5f;
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 1));
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, State.StageStates[1][CKRST_TSS_MINFILTER]);
    EXPECT_EQ(VXTEXTUREFILTER_NEAREST, State.StageStates[1][CKRST_TSS_MAGFILTER]);
    EXPECT_EQ(VXTEXTURE_ADDRESSWRAP, State.StageStates[1][CKRST_TSS_ADDRESSU]);
    EXPECT_EQ(0xFFABCDEFu, State.StageStates[1][CKRST_TSS_BORDERCOLOR]);
    EXPECT_EQ(VXTEXTUREBLEND_MODULATE, State.StageStates[1][CKRST_TSS_TEXTUREMAPBLEND]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(1, CKRST_TEXGEN_PASSTHRU), State.StageStates[1][CKRST_TSS_TEXCOORDINDEX]);
    EXPECT_EQ(CKRST_TTF_NONE, State.StageStates[1][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        EXPECT_FLOAT_EQ(row == column ? 1.0f : 0.0f, State.Matrices[PositiveTextureMatrix][row][column]);
}
TEST_F(MeshPositiveStageContractTest, PublicPositiveSkipTextureMatrixKeepsActualCallbackMatrixCoordinatesAndLaterStage) {
    RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture); material.SetTexture(0, &texture); SeedBaseStates();
    PositiveOwner owner; Effect(material, owner); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 1));
    EXPECT_EQ(1, owner.Calls); EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[1]); EXPECT_EQ(57u, State.Textures[2]);
    EXPECT_FLOAT_EQ(6.25f, State.Matrices[PositiveTextureMatrix][3][0]);
    EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[1][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(1, CKRST_TEXGEN_CAMERASPACENORMAL), State.StageStates[1][CKRST_TSS_TEXCOORDINDEX]);
}
TEST_F(MeshPositiveStageContractTest, PublicPositiveRejectedSamplerReturnsFailureAndClearsOnlySelectedStage) {
    RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture); material.SetTexture(0, &texture); SeedBaseStates();
    State.FailStage = 1; State.FailState = CKRST_TSS_MINFILTER;
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 1)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0u, State.Textures[1]); EXPECT_EQ(29u, State.Textures[0]); EXPECT_EQ(57u, State.Textures[2]);
}
TEST_F(MeshPositiveStageContractTest, PublicPositiveRejectedIdentityMatrixReturnsFailure) {
    RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture); material.SetTexture(0, &texture); SeedBaseStates(); State.FailMatrix = TRUE;
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 1)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0u, State.Textures[1]); EXPECT_EQ(29u, State.Textures[0]); EXPECT_EQ(57u, State.Textures[2]);
}
TEST_F(MeshPositiveStageContractTest, OrdinaryMonoConsumerKeepsBaseGlobalStateButBindsFullMaterialTextureState) {
    PositiveStageMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Select(mesh, material, texture); Configure(material); SeedBaseStates();
    mesh.BindMonoPassTextureChannels(Device); BaseStates();
    EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[1]); EXPECT_EQ(29u, State.Textures[0]); EXPECT_EQ(57u, State.Textures[2]);
    EXPECT_EQ(VXTEXTUREBLEND_MODULATE, State.StageStates[1][CKRST_TSS_TEXTUREMAPBLEND]);
    EXPECT_EQ(STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR), State.StageStates[1][CKRST_TSS_STAGEBLEND]);
}
TEST_F(MeshPositiveStageContractTest, MonoConsumerDoesNotOverrideEffectOwnedTextureMatrixAndCoordinates) {
    PositiveStageMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Select(mesh, material, texture); SeedBaseStates();
    PositiveOwner owner; Effect(material, owner); mesh.BindMonoPassTextureChannels(Device); BaseStates();
    EXPECT_EQ(0xFFAABBCCu, State.States[VXRENDERSTATE_TEXTUREFACTOR]);
    EXPECT_EQ(1, owner.Calls); EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[1]);
    EXPECT_FLOAT_EQ(6.25f, State.Matrices[PositiveTextureMatrix][3][0]); EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[1][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(1, CKRST_TEXGEN_CAMERASPACENORMAL), State.StageStates[1][CKRST_TSS_TEXCOORDINDEX]);
    EXPECT_EQ(STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR), State.StageStates[1][CKRST_TSS_STAGEBLEND]);
}
TEST_F(MeshPositiveStageContractTest, MonoMaterialCallbackOwnsStageBlendAndGlobalState) {
    PositiveStageMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Select(mesh, material, texture); SeedBaseStates();
    PositiveOwner owner; material.SetCallback(PositiveMaterialOwner, &owner); mesh.BindMonoPassTextureChannels(Device);
    EXPECT_EQ(1, owner.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(47u, State.Textures[1]);
    EXPECT_EQ(3u, State.StageStates[1][CKRST_TSS_TEXCOORDINDEX]); EXPECT_EQ(0x65u, State.StageStates[1][CKRST_TSS_STAGEBLEND]);
    EXPECT_EQ(VXCULL_CW, State.States[VXRENDERSTATE_CULLMODE]); EXPECT_EQ(29u, State.Textures[0]);
}
TEST_F(MeshPositiveStageContractTest, MonoSkipAllTexturesPreservesStageBlendButRestoresNormalGlobalState) {
    PositiveStageMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Select(mesh, material, texture); SeedBaseStates();
    PositiveOwner owner; owner.EffectResult = VXEFFECTRETVAL_SKIPALLTEX; Effect(material, owner);
    mesh.BindMonoPassTextureChannels(Device); BaseStates(); EXPECT_EQ(1, owner.Calls); EXPECT_EQ(0, State.Loads);
    EXPECT_EQ(47u, State.Textures[1]); EXPECT_EQ(0x65u, State.StageStates[1][CKRST_TSS_STAGEBLEND]);
}
TEST_F(MeshPositiveStageContractTest, MonoSkipAllPreservesStageBlendAndGlobalState) {
    PositiveStageMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Select(mesh, material, texture); SeedBaseStates();
    PositiveOwner owner; owner.EffectResult = VXEFFECTRETVAL_SKIPALL; Effect(material, owner); mesh.BindMonoPassTextureChannels(Device);
    EXPECT_EQ(1, owner.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(47u, State.Textures[1]);
    EXPECT_EQ(0x65u, State.StageStates[1][CKRST_TSS_STAGEBLEND]); EXPECT_EQ(VXCULL_CW, State.States[VXRENDERSTATE_CULLMODE]);
}
TEST_F(MeshPositiveStageContractTest, MonoUnknownVirtualOverrideKeepsItsActualStageAndGlobalState) {
    PositiveStageMesh mesh(Context); PositiveVirtualOwner material(Context); RCKTexture texture(Context); Select(mesh, material, texture); SeedBaseStates();
    mesh.BindMonoPassTextureChannels(Device); EXPECT_EQ(1, material.Calls); EXPECT_EQ(0, State.Loads);
    EXPECT_EQ(47u, State.Textures[1]); EXPECT_EQ(0x65u, State.StageStates[1][CKRST_TSS_STAGEBLEND]);
    EXPECT_EQ(VXCULL_CW, State.States[VXRENDERSTATE_CULLMODE]);
}
TEST_F(MeshPositiveStageContractTest, RejectedMonoBlendRestoresBaseGlobalStateAndDisablesFailedStage) {
    PositiveStageMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Select(mesh, material, texture); Configure(material); SeedBaseStates();
    State.FailStage = 1; State.FailState = CKRST_TSS_STAGEBLEND;
    mesh.BindMonoPassTextureChannels(Device); BaseStates(); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0u, State.Textures[1]); EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_STAGEBLEND]); EXPECT_EQ(29u, State.Textures[0]);
}
TEST_F(MeshPositiveStageContractTest, TextureUploadFailureDoesNotLeaveAnEnabledMonoBlend) {
    PositiveStageMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Select(mesh, material, texture); SeedBaseStates(); State.FailLoad = 0;
    mesh.BindMonoPassTextureChannels(Device); BaseStates(); EXPECT_EQ(1, State.Loads);
    EXPECT_EQ(0u, State.Textures[1]); EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_STAGEBLEND]); EXPECT_EQ(29u, State.Textures[0]);
}
