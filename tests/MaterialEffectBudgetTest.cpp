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
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
const CKDWORD EffectFailed = 0x80000000u;
class BudgetObserver : public MaterialRenderObserver {
public:
    struct Draw {
        CKDWORD Textures[3] = {}, Operations[3] = {}, Coordinates[3] = {};
        float FirstX = 0;
    } Draws[16];
    int Limit = CKRST_MAX_TEXTURE_STAGES;
    int TextureWrites = 0, StageWrites = 0, OutsideWrites = 0, DrawCount = 0;
    CKBOOL SetTexture(CKDWORD texture, int stage) override {
        ++TextureWrites;
        if (stage < 0 || stage >= Limit) { ++OutsideWrites; return FALSE; }
        return MaterialRenderObserver::SetTexture(texture, stage);
    }
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        ++StageWrites;
        if (stage < 0 || stage >= Limit) { ++OutsideWrites; return FALSE; }
        return MaterialRenderObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data); EXPECT_LT(DrawCount, 16);
        if (!indices || !data || DrawCount >= 16) return FALSE;
        Draw &draw = Draws[DrawCount++];
        for (int stage = 0; stage < 3; ++stage) {
            draw.Textures[stage] = Textures[stage];
            draw.Operations[stage] = StageStates[stage][CKRST_TSS_OP];
            draw.Coordinates[stage] = StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        }
        if (data->PositionPtr)
            std::memcpy(&draw.FirstX, static_cast<CKBYTE *>(data->PositionPtr) +
                indices[0] * data->PositionStride, sizeof(draw.FirstX));
        return TRUE;
    }
};
// Material code uses the real SDK virtual texture boundary; storage/residency
// is outside these tests. The rasterizer records the submitted texture handles.
class BudgetTexture : public RCKTexture {
public:
    CKDWORD Handle;
    int Calls = 0;
    BudgetTexture(CKContext *context, CKDWORD handle) : RCKTexture(context), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        ++Calls;
        return static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(Handle, stage);
    }
};
struct BudgetOwner {
    int Calls = 0;
    VX_EFFECTCALLBACK_RETVAL Result = VXEFFECTRETVAL_SKIPALL;
};
int OwnMaterial(CKRenderContext *context, CKMaterial *, void *argument) {
    ++static_cast<BudgetOwner *>(argument)->Calls;
    static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(901, 0);
    return TRUE;
}
VX_EFFECTCALLBACK_RETVAL OwnEffect(CKRenderContext *context, CKMaterial *, int stage, void *argument) {
    BudgetOwner *owner = static_cast<BudgetOwner *>(argument); ++owner->Calls;
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    if (owner->Result == VXEFFECTRETVAL_SKIPTEXMAT) {
        VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = .375f;
        state->SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), matrix);
        state->SetTextureStageState(stage, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
        state->SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX,
            CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_CAMERASPACENORMAL));
    } else {
        state->SetTexture(902, 0);
    }
    return owner->Result;
}
class MaterialEffectBudgetTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver;
    BudgetObserver State;
    VxVertex Vertices[3];
    VxDrawPrimitiveData Data = {};
    CKWORD Indices[3] = {0, 1, 2};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
        for (int i = 0; i < 3; ++i) {
            Vertices[i].m_Position = VxVector(i == 1 ? 1 : 0, i == 2 ? 1 : 0, 1);
            Vertices[i].m_Normal = VxVector(0, 0, 1); Vertices[i].m_UV = Vx2DVector(.125f, .25f);
        }
        Data.VertexCount = 3; Data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0;
        Data.PositionPtr = &Vertices[0].m_Position; Data.PositionStride = sizeof(VxVertex);
        Data.NormalPtr = &Vertices[0].m_Normal; Data.NormalStride = sizeof(VxVertex);
        Data.TexCoordPtr = &Vertices[0].m_UV; Data.TexCoordStride = sizeof(VxVertex);
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Caps(CKDWORD textures, CKDWORD blends) {
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = textures;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = blends;
        State.Limit = CKRST_MAX_TEXTURE_STAGES;
        if (blends && blends < (CKDWORD)State.Limit) State.Limit = (int)blends;
    }
    void Untouched(int resets, int textures, int stages) {
        EXPECT_EQ(resets, State.Resets); EXPECT_EQ(textures, State.TextureWrites);
        EXPECT_EQ(stages, State.StageWrites); EXPECT_EQ(0, State.OutsideWrites);
    }
    void Draw() { ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, Indices, 3, &Data)); }
    void EffectOwner(VX_EFFECT effect, BudgetOwner &owner) {
        VxEffectDescription &description = const_cast<VxEffectDescription &>(Manager->GetEffectDescription(effect));
        description.SetCallback = OwnEffect; description.CallbackArg = &owner;
    }
    static void Geometry(RCKMesh &mesh, RCKMaterial &first, RCKMaterial *second = nullptr) {
        const int faces = second ? 2 : 1;
        ASSERT_TRUE(mesh.SetVertexCount(3 * faces)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int face = 0; face < faces; ++face) {
            mesh.SetFaceVertexIndex(face, face * 3, face * 3 + 1, face * 3 + 2);
            mesh.SetFaceMaterial(face, face ? second : &first);
            for (int i = 0; i < 3; ++i) {
                VxVector point(face * 10.0f + (i == 1 ? 1 : 0), i == 2 ? 1 : 0, 1);
                mesh.SetVertexPosition(face * 3 + i, &point);
                mesh.SetVertexTextureCoordinates(face * 3 + i, .125f, .25f, -1);
            }
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static void Channel(RCKMesh &mesh, RCKMaterial &material) {
        const int channel = mesh.AddChannel(&material, FALSE); ASSERT_GE(channel, 0);
        mesh.ActivateChannel(channel, TRUE); mesh.SetChannelSourceBlend(channel, VXBLEND_ZERO);
        mesh.SetChannelDestBlend(channel, VXBLEND_SRCCOLOR);
        for (int i = 0; i < mesh.GetVertexCount(); ++i) mesh.SetVertexTextureCoordinates(i, .375f, .625f, channel);
    }
};
}

TEST_F(MaterialEffectBudgetTest, CompleteCombineThreeRejectsBeforeBindingAndRecoversAtExactCapacity) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    material.SetTexture(0, &primary); material.SetTexture(1, &first); material.SetTexture(2, &second);
    material.SetEffect(VXEFFECT_3TEXTURES); Caps(2, 8);
    const int resets = State.Resets, textures = State.TextureWrites, stages = State.StageWrites;
    ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); Untouched(resets, textures, stages);
    EXPECT_EQ(0, primary.Calls); EXPECT_EQ(0, first.Calls); EXPECT_EQ(0, second.Calls);
    Caps(3, 3); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); Draw();
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(41u, State.Draws[0].Textures[0]);
    EXPECT_EQ(42u, State.Draws[0].Textures[1]); EXPECT_EQ(43u, State.Draws[0].Textures[2]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.Draws[0].Operations[2]); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, BlendStageCapacityIndependentlyLimitsTheCompleteEffect) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), extra(Context, 42);
    material.SetTexture(0, &primary); material.SetTexture(1, &extra); material.SetEffect(VXEFFECT_2TEXTURES);
    Caps(8, 1); ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(0, primary.Calls);
    Caps(1, 8); ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(0, extra.Calls);
    Caps(8, 2); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); Draw();
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(42u, State.Draws[0].Textures[1]); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, PositiveStartingStageAccountsForPrimaryAndAllExtras) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), extra(Context, 42);
    material.SetTexture(0, &primary); material.SetTexture(1, &extra); material.SetEffect(VXEFFECT_2TEXTURES);
    Caps(3, 3); ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 2)); EXPECT_EQ(0, primary.Calls);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 1));
    EXPECT_EQ(41u, State.Textures[1]); EXPECT_EQ(42u, State.Textures[2]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[2][CKRST_TSS_OP]); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, MissingFirstExtraDoesNotReserveASecondExtraStage) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), orphan(Context, 43);
    material.SetTexture(0, &primary); material.SetTexture(2, &orphan); material.SetEffect(VXEFFECT_3TEXTURES);
    Caps(1, 1); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(0, orphan.Calls); EXPECT_EQ(0, State.OutsideWrites);
    material.SetTexture(1, &orphan); material.SetTexture(2, nullptr); Caps(8, 8);
    EXPECT_EQ(2u, material.BlendTexturesEffect(Device, 7)); EXPECT_EQ(43u, State.Textures[7]);
}
TEST_F(MaterialEffectBudgetTest, InvalidCombineStillRequiresTheCompleteConfiguredRange) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    material.SetTexture(0, &primary); material.SetTexture(1, &first); material.SetTexture(2, &second);
    material.SetEffect(VXEFFECT_3TEXTURES); CKStructHelper parameters(material.GetEffectParameter());
    ASSERT_NE(nullptr, parameters[0]); CKDWORD disabled = CKRST_TOP_DISABLE;
    ASSERT_EQ(CK_OK, parameters[0]->SetValue(&disabled)); Caps(2, 8);
    ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(0, primary.Calls);
    Caps(3, 3); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[1][CKRST_TSS_OP]);
    EXPECT_EQ(43u, State.Textures[2]); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, DirectEffectHelpersReportFailureRatherThanNoEffect) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    material.SetTexture(0, &primary); material.SetTexture(1, &first); material.SetTexture(2, &second);
    Caps(1, 1); EXPECT_EQ(EffectFailed, material.DP3Effect(Device, 0));
    EXPECT_EQ(EffectFailed, material.BumpMapEnvEffect(Device));
    material.SetEffect(VXEFFECT_3TEXTURES); EXPECT_EQ(EffectFailed, material.BlendTexturesEffect(Device, 1));
    EXPECT_EQ(0, primary.Calls); EXPECT_EQ(0, first.Calls); EXPECT_EQ(0, second.Calls);
    Caps(0, 0); EXPECT_EQ(EffectFailed, material.DP3Effect(Device, 7));
    EXPECT_EQ(EffectFailed, material.BlendTexturesEffect(Device, 7));
    material.SetTexture(0, nullptr);
    EXPECT_EQ(0u, material.DP3Effect(Device, 0)); EXPECT_EQ(0u, material.BumpMapEnvEffect(Device));
    EXPECT_EQ(2u, material.BlendTexturesEffect(Device, 1));
    EXPECT_EQ(42u, State.Textures[1]); EXPECT_EQ(43u, State.Textures[2]);
    material.SetTexture(1, nullptr);
    EXPECT_EQ(0u, material.BlendTexturesEffect(Device, 1)); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, DotProductNeedsTwoPhysicalStagesDespiteSharedUVAndRecovers) {
    RCKMaterial material(Context); BudgetTexture color(Context, 41), normal(Context, 42);
    material.SetTexture(0, &color); material.SetTexture(1, &normal); material.SetEffect(VXEFFECT_DP3);
    Caps(1, 8); ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(0, normal.Calls);
    Caps(2, 2); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); Draw();
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(42u, State.Draws[0].Textures[0]); EXPECT_EQ(41u, State.Draws[0].Textures[1]);
    EXPECT_EQ(CKRST_TOP_DOTPRODUCT3, State.Draws[0].Operations[0]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.Draws[0].Operations[1]);
    EXPECT_EQ(0u, State.Draws[0].Coordinates[0]); EXPECT_EQ(0u, State.Draws[0].Coordinates[1]);
}
TEST_F(MaterialEffectBudgetTest, BumpCompactionAndTerminalDisableRespectExactAvailableStages) {
    RCKMaterial material(Context); BudgetTexture color(Context, 41), bump(Context, 42), environment(Context, 43);
    material.SetTexture(0, &color); material.SetEffect(VXEFFECT_BUMPENV);
    Caps(1, 1); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(41u, State.Textures[0]);
    material.SetTexture(1, &bump); Caps(2, 2); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(42u, State.Textures[1]); EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.StageStates[1][CKRST_TSS_OP]);
    material.SetTexture(1, nullptr); material.SetTexture(2, &environment);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(43u, State.Textures[1]);
    material.SetTexture(1, &bump); ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0));
    Caps(3, 3); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); Draw();
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(41u, State.Draws[0].Textures[0]);
    EXPECT_EQ(42u, State.Draws[0].Textures[1]); EXPECT_EQ(43u, State.Draws[0].Textures[2]);
    EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, MaterialAndEffectOwnersBypassOrdinaryEffectFootprintChecks) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    material.SetTexture(0, &primary); material.SetTexture(1, &first); material.SetTexture(2, &second);
    material.SetEffect(VXEFFECT_3TEXTURES); Caps(1, 1); BudgetOwner owner;
    material.SetCallback(OwnMaterial, &owner); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(901u, State.Textures[0]); EXPECT_EQ(0, primary.Calls); EXPECT_EQ(1, owner.Calls);
    material.SetCallback(nullptr, nullptr); EffectOwner(VXEFFECT_3TEXTURES, owner);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(902u, State.Textures[0]);
    owner.Result = VXEFFECTRETVAL_SKIPALLTEX;
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(902u, State.Textures[0]);
    EXPECT_EQ(0, primary.Calls); EXPECT_EQ(0, first.Calls); EXPECT_EQ(0, second.Calls);
    EXPECT_EQ(3, owner.Calls); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, SkipMatrixChecksOnlyPrimaryAndZeroCapabilitiesRetainTheApiLimit) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    material.SetTexture(0, &primary); material.SetTexture(1, &first); material.SetTexture(2, &second);
    material.SetEffect(VXEFFECT_3TEXTURES); BudgetOwner owner; owner.Result = VXEFFECTRETVAL_SKIPTEXMAT;
    EffectOwner(VXEFFECT_3TEXTURES, owner); Caps(1, 1);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(41u, State.Textures[0]);
    EXPECT_EQ(0, first.Calls); EXPECT_EQ(0, second.Calls);
    EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[0][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    EXPECT_FLOAT_EQ(.375f, State.Matrices[VXMATRIX_TEXTURE0][3][0]);
    VxEffectDescription &description = const_cast<VxEffectDescription &>(Manager->GetEffectDescription(VXEFFECT_3TEXTURES));
    description.SetCallback = nullptr; description.CallbackArg = nullptr; Caps(0, 0);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 5)); EXPECT_EQ(43u, State.Textures[7]);
    const int calls = primary.Calls; ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 6));
    EXPECT_EQ(calls, primary.Calls); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, FailedBaseEffectDoesNotDrawItsGroupOrReplayProviderAsAnIndependentLayer) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context);
    BudgetTexture primary(Context, 41), extra(Context, 42), layer(Context, 43);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra); base.SetEffect(VXEFFECT_2TEXTURES);
    provider.SetTexture(0, &layer); Geometry(mesh, base); Channel(mesh, provider); Caps(1, 1);
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(0, State.DrawCount);
    EXPECT_EQ(0, primary.Calls); EXPECT_EQ(0, extra.Calls); EXPECT_EQ(0, layer.Calls);
    EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, FailedEffectGroupKeepsThePlainGroupAndItsIndependentLayer) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), effect(Context), channel(Context);
    BudgetTexture plainTexture(Context, 40), primary(Context, 41), extra(Context, 42), layer(Context, 43);
    plain.SetTexture(0, &plainTexture); effect.SetTexture(0, &primary); effect.SetTexture(1, &extra);
    effect.SetEffect(VXEFFECT_2TEXTURES); channel.SetTexture(0, &layer);
    Geometry(mesh, plain, &effect); Channel(mesh, channel); Caps(1, 1);
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(40u, State.Draws[0].Textures[0]); EXPECT_EQ(43u, State.Draws[1].Textures[0]);
    EXPECT_FLOAT_EQ(0, State.Draws[0].FirstX); EXPECT_FLOAT_EQ(0, State.Draws[1].FirstX);
    EXPECT_EQ(0, primary.Calls); EXPECT_EQ(0, extra.Calls); EXPECT_EQ(1, layer.Calls);
    EXPECT_EQ(0, State.OutsideWrites);
}

TEST_F(MaterialEffectBudgetTest, SparsePositiveStageCountsTexturesRatherThanStageOrdinalAndIncludesRetainedPrefix) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41);
    material.SetTexture(0, &primary); Caps(1, 8);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 5));
    EXPECT_EQ(41u, State.Textures[5]); EXPECT_EQ(0, State.OutsideWrites);
    ASSERT_TRUE(State.SetTexture(99, 0));
    const int calls = primary.Calls, writes = State.TextureWrites;
    ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 5));
    EXPECT_EQ(calls, primary.Calls); EXPECT_EQ(writes, State.TextureWrites);
    EXPECT_EQ(99u, State.Textures[0]); EXPECT_EQ(41u, State.Textures[5]);
    ASSERT_TRUE(State.SetTexture(0, 0));
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 5)); EXPECT_EQ(0, State.OutsideWrites);
}
TEST_F(MaterialEffectBudgetTest, DotProductWithoutANormalTextureUsesTwoStagesButOnlyOneTextureBinding) {
    RCKMaterial material(Context); BudgetTexture primary(Context, 41);
    material.SetTexture(0, &primary); material.SetEffect(VXEFFECT_DP3); Caps(1, 2);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); Draw();
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(0u, State.Draws[0].Textures[0]);
    EXPECT_EQ(41u, State.Draws[0].Textures[1]);
    EXPECT_EQ(CKRST_TOP_DOTPRODUCT3, State.Draws[0].Operations[0]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.Draws[0].Operations[1]);
    EXPECT_EQ(0, State.OutsideWrites);
    const int calls = primary.Calls; Caps(1, 1);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(calls, primary.Calls);
}
