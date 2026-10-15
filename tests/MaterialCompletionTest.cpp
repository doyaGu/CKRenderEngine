#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKRenderedScene.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
const CKDWORD EffectFailed = 0x80000000u;
class CompletionObserver : public MaterialRenderObserver {
public:
    int TextureWrites = 0, StageWrites = 0, MatrixWrites = 0, Draws = 0;
    int RejectRead = -1, Refusals = 0;
    CKDWORD SubmittedTextures[CKRST_MAX_TEXTURE_STAGES] = {};
    CKDWORD SubmittedCoordinates[CKRST_MAX_TEXTURE_STAGES] = {};
    CKDWORD SubmittedOperations[CKRST_MAX_TEXTURE_STAGES] = {};
    VxMatrix SubmittedMatrices[CKRST_MAX_TEXTURE_STAGES];
    VxVector SubmittedPositions[3];
    CKBOOL GetTexture(int stage, CKDWORD *value) override {
        if (stage == RejectRead && !Refusals) { ++Refusals; return FALSE; }
        return MaterialRenderObserver::GetTexture(stage, value);
    }
    CKBOOL SetTexture(CKDWORD texture, int stage) override {
        ++TextureWrites; return MaterialRenderObserver::SetTexture(texture, stage);
    }
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        ++StageWrites; return MaterialRenderObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &value) override {
        ++MatrixWrites; return MaterialRenderObserver::SetTransformMatrix(type, value);
    }
    CKBOOL ResetTextureStages(int first, int count) override {
        const CKBOOL result = MaterialRenderObserver::ResetTextureStages(first, count);
        for (int stage = first; stage < first + count; ++stage) {
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_DISABLE;
            StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = stage;
        }
        return result;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        if (!indices || !data || count != 3 || !data->PositionPtr) return FALSE;
        for (int i = 0; i < 3; ++i)
            memcpy(&SubmittedPositions[i], static_cast<CKBYTE *>(data->PositionPtr) + indices[i] * data->PositionStride, sizeof(VxVector));
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            SubmittedTextures[stage] = Textures[stage];
            SubmittedCoordinates[stage] = StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
            SubmittedOperations[stage] = StageStates[stage][CKRST_TSS_OP];
            SubmittedMatrices[stage] = Matrices[VXMATRIX_TEXTURE0 + stage];
        }
        ++Draws; return TRUE;
    }
    void ClearActivity() { TextureWrites = StageWrites = MatrixWrites = StateWrites = Resets = 0; }
};
class CompletionTexture : public RCKTexture {
public:
    CKDWORD Handle;
    int Calls = 0;
    CKBOOL DirtyFailure = FALSE;
    CompletionTexture(CKContext *context, CKDWORD handle) : RCKTexture(context), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        ++Calls;
        CKRasterizerContext *state = context->GetRasterizerContext();
        const CKBOOL accepted = state->SetTexture(Handle, stage);
        if (DirtyFailure) {
            VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 9.0f;
            state->SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), matrix);
            state->SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX,
                CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_CAMERASPACENORMAL));
            state->SetTextureStageState(stage, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT3);
            return FALSE;
        }
        return accepted;
    }
};
int CompletionMaterialOwner(CKRenderContext *context, CKMaterial *, void *argument) {
    ++*static_cast<int *>(argument); context->GetRasterizerContext()->SetTexture(901, 0); return TRUE;
}
VX_EFFECTCALLBACK_RETVAL CompletionEffectOwner(CKRenderContext *context, CKMaterial *, int stage, void *argument) {
    ++*static_cast<int *>(argument); context->GetRasterizerContext()->SetTexture(902, stage); return VXEFFECTRETVAL_SKIPALLTEX;
}
class MaterialCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver; CompletionObserver State;
    VxVertex Vertices[3]; VxDrawPrimitiveData Data = {}; CKWORD Indices[3] = {0, 1, 2};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Manager->m_DriverCount = 1;
        Manager->m_Drivers = new VxDriverDescEx[1](); Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0;
        Device->SetWorldTransformationMatrix(VxMatrix::Identity());
        reinterpret_cast<RCK3dEntity *>(Device->m_RenderedScene->GetRootEntity())->SetWorldMatrix(VxMatrix::Identity());
        for (int i = 0; i < 3; ++i) {
            Vertices[i].m_Position = VxVector(i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f, 1.0f);
            Vertices[i].m_Normal = VxVector(1.0f, 0.0f, 0.0f); Vertices[i].m_UV = Vx2DVector(.125f + i * .125f, .25f);
        }
        Data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0; Data.VertexCount = 3;
        Data.PositionPtr = &Vertices[0].m_Position; Data.PositionStride = sizeof(VxVertex);
        Data.NormalPtr = &Vertices[0].m_Normal; Data.NormalStride = sizeof(VxVertex);
        Data.TexCoordPtr = &Vertices[0].m_UV; Data.TexCoordStride = sizeof(VxVertex);
        Caps(0); State.ClearActivity();
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Caps(CKDWORD textures) {
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = textures;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
    }
    void NoWrites() {
        EXPECT_EQ(0, State.TextureWrites); EXPECT_EQ(0, State.StageWrites);
        EXPECT_EQ(0, State.MatrixWrites); EXPECT_EQ(0, State.StateWrites); EXPECT_EQ(0, State.Resets);
    }
    void Draw() {
        ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, Indices, 3, &Data));
        EXPECT_FLOAT_EQ(1.0f, State.SubmittedPositions[1].x); EXPECT_FLOAT_EQ(1.0f, State.SubmittedPositions[2].y);
    }
    void Chain(RCKMaterial &material, CompletionTexture &color, CompletionTexture &first,
               CompletionTexture *second, VX_EFFECT effect) {
        material.SetTexture(0, &color); material.SetTexture(1, &first);
        if (second) material.SetTexture(2, second);
        material.SetEffect(effect);
    }
    void Pole(VX_EFFECTTEXGEN mode, const VxVector &position, float expectedZToU) {
        RCKMaterial material(Context); RCK3dEntity reference(Context);
        VxMatrix world = VxMatrix::Identity(); world[3].Set(position.x, position.y, position.z, 1.0f);
        Device->SetWorldTransformationMatrix(world); State.ClearActivity();
        ASSERT_EQ(1u, material.TexGenEffect(Device, mode, &reference, 2)); Draw();
        EXPECT_EQ(CKRSTPackTexcoordIndex(2, mode == VXEFFECT_TGREFLECT ? CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR : CKRST_TEXGEN_CAMERASPACENORMAL), State.SubmittedCoordinates[2]);
        const VxMatrix &matrix = State.SubmittedMatrices[2];
        for (int row = 0; row < 3; ++row) {
            float squared = 0;
            for (int column = 0; column < 3; ++column) {
                EXPECT_TRUE(std::isfinite(matrix[row][column])); squared += matrix[row][column] * matrix[row][column];
            }
            EXPECT_NEAR(.16f, squared, 1e-5f);
        }
        EXPECT_NEAR(expectedZToU, matrix[2][0] + matrix[3][0], 1e-5f);
        EXPECT_FLOAT_EQ(.5f, matrix[3][0]); EXPECT_FLOAT_EQ(.5f, matrix[3][1]);
    }
};
}

TEST_F(MaterialCompletionTest, PositiveOrdinaryBindingCountsRetainedSuffixAndRetriesAtExactCapacity) {
    RCKMaterial material(Context); CompletionTexture color(Context, 41); material.SetTexture(0, &color);
    State.Textures[7] = 77; Caps(1);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 2)); NoWrites(); EXPECT_EQ(0, color.Calls); EXPECT_EQ(77u, State.Textures[7]);
    Caps(2); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 2)); Draw();
    EXPECT_EQ(41u, State.SubmittedTextures[2]); EXPECT_EQ(77u, State.SubmittedTextures[7]); EXPECT_EQ(1, color.Calls);
}
TEST_F(MaterialCompletionTest, PrimaryEntryDiscardsOldSuffixBeforeApplyingTheTextureBudget) {
    RCKMaterial material(Context); CompletionTexture color(Context, 41); material.SetTexture(0, &color);
    State.Textures[0] = 70; State.Textures[7] = 77; Caps(1);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); Draw();
    EXPECT_EQ(41u, State.SubmittedTextures[0]); EXPECT_EQ(0u, State.SubmittedTextures[7]); EXPECT_EQ(1, color.Calls);
}
TEST_F(MaterialCompletionTest, DirectDotProductCountsBothRetainedSidesAndKeepsItsSharedUVContract) {
    RCKMaterial material(Context); CompletionTexture color(Context, 41), normal(Context, 42);
    Chain(material, color, normal, nullptr, VXEFFECT_DP3);
    State.Textures[0] = 70; State.Textures[7] = 77; Caps(3);
    EXPECT_NE(0u, material.DP3Effect(Device, 2) & EffectFailed); NoWrites(); EXPECT_EQ(0, color.Calls); EXPECT_EQ(0, normal.Calls);
    Caps(4); ASSERT_EQ(2u, material.DP3Effect(Device, 2)); Draw();
    EXPECT_EQ(70u, State.SubmittedTextures[0]); EXPECT_EQ(42u, State.SubmittedTextures[2]);
    EXPECT_EQ(41u, State.SubmittedTextures[3]); EXPECT_EQ(77u, State.SubmittedTextures[7]);
    EXPECT_EQ(2u, State.SubmittedCoordinates[2]); EXPECT_EQ(2u, State.SubmittedCoordinates[3]);
    EXPECT_EQ(CKRST_TOP_DOTPRODUCT3, State.SubmittedOperations[2]); EXPECT_EQ(CKRST_TOP_MODULATE, State.SubmittedOperations[3]);
}
TEST_F(MaterialCompletionTest, DirectCombineCountsPrimaryAndRetainedSuffixBeforeAnyExtraUpload) {
    RCKMaterial material(Context); CompletionTexture color(Context, 41), first(Context, 42), second(Context, 43);
    Chain(material, color, first, &second, VXEFFECT_3TEXTURES);
    State.Textures[0] = 41; State.Textures[7] = 77; Caps(3);
    EXPECT_NE(0u, material.BlendTexturesEffect(Device, 1) & EffectFailed); NoWrites(); EXPECT_EQ(0, first.Calls); EXPECT_EQ(0, second.Calls);
    Caps(4); ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 1)); Draw();
    EXPECT_EQ(41u, State.SubmittedTextures[0]); EXPECT_EQ(42u, State.SubmittedTextures[1]);
    EXPECT_EQ(43u, State.SubmittedTextures[2]); EXPECT_EQ(77u, State.SubmittedTextures[7]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.SubmittedOperations[1]); EXPECT_EQ(CKRST_TOP_MODULATE, State.SubmittedOperations[2]);
    EXPECT_EQ(1u, State.SubmittedCoordinates[1]); EXPECT_EQ(2u, State.SubmittedCoordinates[2]);
}
TEST_F(MaterialCompletionTest, PositiveCombinePreflightIncludesItsWholeChainAndBothRetainedSides) {
    RCKMaterial material(Context); CompletionTexture color(Context, 41), first(Context, 42), second(Context, 43);
    Chain(material, color, first, &second, VXEFFECT_3TEXTURES);
    State.Textures[0] = 70; State.Textures[7] = 77; Caps(4);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 1)); NoWrites(); EXPECT_EQ(0, color.Calls);
    Caps(5); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 1)); Draw();
    EXPECT_EQ(70u, State.SubmittedTextures[0]); EXPECT_EQ(41u, State.SubmittedTextures[1]);
    EXPECT_EQ(42u, State.SubmittedTextures[2]); EXPECT_EQ(43u, State.SubmittedTextures[3]); EXPECT_EQ(77u, State.SubmittedTextures[7]);
}
TEST_F(MaterialCompletionTest, DirectBumpCountsSurvivingSuffixBeyondItsAbsoluteCompactedChain) {
    RCKMaterial material(Context); CompletionTexture color(Context, 41), bump(Context, 42), environment(Context, 43);
    Chain(material, color, bump, &environment, VXEFFECT_BUMPENV);
    State.Textures[7] = 77; Caps(3);
    EXPECT_NE(0u, material.BumpMapEnvEffect(Device) & EffectFailed); NoWrites(); EXPECT_EQ(0, color.Calls);
    Caps(4); ASSERT_EQ(2u, material.BumpMapEnvEffect(Device)); Draw();
    EXPECT_EQ(41u, State.SubmittedTextures[0]); EXPECT_EQ(42u, State.SubmittedTextures[1]);
    EXPECT_EQ(43u, State.SubmittedTextures[2]); EXPECT_EQ(77u, State.SubmittedTextures[7]);
    EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.SubmittedOperations[1]); EXPECT_EQ(CKRST_TOP_ADD, State.SubmittedOperations[2]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.SubmittedCoordinates[2]);
}
TEST_F(MaterialCompletionTest, UnreadableRetainedSuffixFailsBeforeMutationAndNextCallCanRecover) {
    RCKMaterial material(Context); CompletionTexture color(Context, 41); material.SetTexture(0, &color);
    State.Textures[7] = 77; State.RejectRead = 7; Caps(2);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 2)); NoWrites(); EXPECT_EQ(1, State.Refusals); EXPECT_EQ(0, color.Calls);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 2)); Draw();
    EXPECT_EQ(41u, State.SubmittedTextures[2]); EXPECT_EQ(77u, State.SubmittedTextures[7]);
}
