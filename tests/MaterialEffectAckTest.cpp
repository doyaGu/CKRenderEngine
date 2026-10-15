#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKMesh.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
const CKDWORD EffectFailed = 0x80000000u;
class EffectAckObserver : public MaterialRenderObserver {
public:
    int RejectStage = -1, RejectMatrix = -1, Rejections = 0, DrawCount = 0;
    int SettersAfterRejection = 0;
    CKRST_TEXTURESTAGESTATETYPE RejectState = CKRST_TSS_OP;
    CKDWORD RejectValue = CKRST_TOP_MODULATE;
    CKDWORD DrawTextures[8][3] = {};
    float FirstX[8] = {};
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        if (Rejections) ++SettersAfterRejection;
        if (!Rejections && stage == RejectStage && state == RejectState && value == RejectValue) {
            ++Rejections; return FALSE;
        }
        if (state == CKRST_TSS_TEXTUREMAPBLEND && value == VXTEXTUREBLEND_MODULATEALPHA)
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
        return MaterialRenderObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        if (Rejections) ++SettersAfterRejection;
        if (!Rejections && static_cast<int>(type) == RejectMatrix) { ++Rejections; return FALSE; }
        return MaterialRenderObserver::SetTransformMatrix(type, matrix);
    }
    CKBOOL ResetTextureStages(int firstStage, int stageCount) override {
        const CKBOOL result = MaterialRenderObserver::ResetTextureStages(firstStage, stageCount);
        // ResetTextureStages disables the specified chain. The general test
        // observer zeroes its raw storage; expose the documented disabled OP.
        for (int stage = firstStage; stage < firstStage + stageCount; ++stage)
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_DISABLE;
        return result;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count); EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        EXPECT_LT(DrawCount, 8); if (!indices || !data || DrawCount >= 8) return FALSE;
        for (int stage = 0; stage < 3; ++stage) DrawTextures[DrawCount][stage] = Textures[stage];
        if (data->PositionPtr)
            FirstX[DrawCount] = reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + indices[0] * data->PositionStride)->x;
        ++DrawCount; return TRUE;
    }
};
class EffectAckTexture : public RCKTexture {
public:
    CKDWORD Handle;
    int Calls = 0;
    EffectAckTexture(CKContext *context, CKDWORD handle) : RCKTexture(context), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        ++Calls;
        return static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(Handle, stage);
    }
};
int OwnAckMaterial(CKRenderContext *context, CKMaterial *, void *argument) {
    ++*static_cast<int *>(argument);
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    state->SetTexture(901, 0); state->SetTexture(902, 1);
    state->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    state->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
    return TRUE;
}
class MaterialEffectAckTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver;
    EffectAckObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 3;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Reject(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) {
        State.RejectStage = stage; State.RejectState = state; State.RejectValue = value;
    }
    static void Bind(RCKMaterial &material, EffectAckTexture &primary, EffectAckTexture &first,
                     EffectAckTexture *second = nullptr, VX_EFFECT effect = VXEFFECT_3TEXTURES) {
        material.SetTexture(0, &primary); material.SetTexture(1, &first);
        if (second) material.SetTexture(2, second);
        material.SetEffect(effect);
    }
    void Cleared(int stage) {
        EXPECT_EQ(1, State.Rejections); EXPECT_EQ(0u, State.Textures[stage]);
        EXPECT_EQ(0, State.SettersAfterRejection);
        EXPECT_EQ(CKRST_TOP_DISABLE, State.StageStates[stage][CKRST_TSS_OP]);
        EXPECT_EQ(CKRST_TTF_NONE, State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    }
    static void Geometry(RCKMesh &mesh, RCKMaterial &plain, RCKMaterial &effect) {
        ASSERT_TRUE(mesh.SetVertexCount(6)); ASSERT_TRUE(mesh.SetFaceCount(2));
        for (int face = 0; face < 2; ++face) {
            mesh.SetFaceVertexIndex(face, face * 3, face * 3 + 1, face * 3 + 2);
            mesh.SetFaceMaterial(face, face ? &effect : &plain);
            for (int i = 0; i < 3; ++i) {
                VxVector position(face * 10.0f + (i == 1 ? 1.0f : 0.0f), i == 2 ? 1.0f : 0.0f, 1.0f);
                mesh.SetVertexPosition(face * 3 + i, &position);
            }
        }
        mesh.SetFlags(mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY));
    }
};
}

TEST_F(MaterialEffectAckTest, CombineFirstOperationFailureStopsTheLaterExtraAndClearsOnlyItsStage) {
    RCKMaterial material(Context); EffectAckTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    Bind(material, primary, first, &second); State.SetTexture(99, 0);
    Reject(1, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    EXPECT_EQ(EffectFailed, material.BlendTexturesEffect(Device, 1)); Cleared(1);
    EXPECT_EQ(99u, State.Textures[0]); EXPECT_EQ(1, first.Calls); EXPECT_EQ(0, second.Calls);
}

TEST_F(MaterialEffectAckTest, CombineSecondCoordinatesFailurePreservesTheAcceptedPrefixAndReturnsFalse) {
    RCKMaterial material(Context); EffectAckTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    Bind(material, primary, first, &second); Reject(2, CKRST_TSS_TEXCOORDINDEX, 2);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); Cleared(2);
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[1][CKRST_TSS_OP]);
}

TEST_F(MaterialEffectAckTest, CombineSecondMatrixFailureReportsHelperFailureAndPreservesTheFirstExtra) {
    RCKMaterial material(Context); EffectAckTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    Bind(material, primary, first, &second); State.SetTexture(99, 0); State.RejectMatrix = VXMATRIX_TEXTURE0 + 2;
    EXPECT_EQ(EffectFailed, material.BlendTexturesEffect(Device, 1)); Cleared(2);
    EXPECT_EQ(99u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[1][CKRST_TSS_OP]);
}

TEST_F(MaterialEffectAckTest, DotProductFirstOperationFailureClearsTheRejectedNormalStage) {
    RCKMaterial material(Context); EffectAckTexture color(Context, 41), normal(Context, 42);
    Bind(material, color, normal, nullptr, VXEFFECT_DP3); Reject(0, CKRST_TSS_OP, CKRST_TOP_DOTPRODUCT3);
    EXPECT_EQ(EffectFailed, material.DP3Effect(Device, 0)); Cleared(0);
    // DP3 binds both textures before uploading its stage semantics. Preserve
    // that ordering rather than requiring rollback of an earlier binding.
    EXPECT_EQ(1, normal.Calls); EXPECT_EQ(1, color.Calls);
}

TEST_F(MaterialEffectAckTest, DotProductSecondArgumentFailurePreservesTheNormalStage) {
    RCKMaterial material(Context); EffectAckTexture color(Context, 41), normal(Context, 42);
    Bind(material, color, normal, nullptr, VXEFFECT_DP3); Reject(1, CKRST_TSS_ARG2, CKRST_TA_CURRENT);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); Cleared(1);
    EXPECT_EQ(42u, State.Textures[0]); EXPECT_EQ(CKRST_TOP_DOTPRODUCT3, State.StageStates[0][CKRST_TSS_OP]);
    EXPECT_EQ(CKRST_TA_TFACTOR, State.StageStates[0][CKRST_TSS_ARG2]);
}

TEST_F(MaterialEffectAckTest, BumpOperationFailureStopsTheEnvironmentAndPreservesTheColor) {
    RCKMaterial material(Context); EffectAckTexture color(Context, 41), bump(Context, 42), environment(Context, 43);
    Bind(material, color, bump, &environment, VXEFFECT_BUMPENV); Reject(1, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
    EXPECT_EQ(EffectFailed, material.BumpMapEnvEffect(Device)); Cleared(1);
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(0, environment.Calls);
}

TEST_F(MaterialEffectAckTest, BumpGeneratedEnvironmentMatrixFailurePreservesBothAcceptedStages) {
    RCKMaterial material(Context); EffectAckTexture color(Context, 41), bump(Context, 42), environment(Context, 43);
    Bind(material, color, bump, &environment, VXEFFECT_BUMPENV); State.RejectMatrix = VXMATRIX_TEXTURE0 + 2;
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); Cleared(2);
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]);
    EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.StageStates[1][CKRST_TSS_OP]);
}

TEST_F(MaterialEffectAckTest, DirectTexgenMatrixFailureClearsItsStageWithoutTouchingThePrefix) {
    RCKMaterial material(Context); State.SetTexture(99, 0); State.SetTexture(77, 2);
    State.RejectMatrix = VXMATRIX_TEXTURE0 + 2;
    EXPECT_EQ(EffectFailed, material.TexGenEffect(Device, VXEFFECT_TGREFLECT, nullptr, 2)); Cleared(2);
    EXPECT_EQ(99u, State.Textures[0]);
}

TEST_F(MaterialEffectAckTest, AcceptedCombineChainKeepsAllColorAlphaAndCoordinateSemantics) {
    RCKMaterial material(Context); EffectAckTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    Bind(material, primary, first, &second); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]); EXPECT_EQ(43u, State.Textures[2]);
    for (int stage = 1; stage < 3; ++stage) {
        EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[stage][CKRST_TSS_OP]);
        EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[stage][CKRST_TSS_AOP]);
        EXPECT_EQ(CKRST_TA_TEXTURE, State.StageStates[stage][CKRST_TSS_ARG1]);
        EXPECT_EQ(CKRST_TA_CURRENT, State.StageStates[stage][CKRST_TSS_ARG2]);
        EXPECT_EQ(static_cast<CKDWORD>(stage), State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX]);
        EXPECT_EQ(CKRST_TTF_NONE, State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
        EXPECT_FLOAT_EQ(1.0f, State.Matrices[VXMATRIX_TEXTURE0 + stage][0][0]);
    }
    EXPECT_EQ(0, State.Rejections);
}

TEST_F(MaterialEffectAckTest, RejectedAckThenTemporaryCapacityFailureDoNotPoisonTheNextSuccessfulBind) {
    RCKMaterial material(Context); EffectAckTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    Bind(material, primary, first, &second); Reject(1, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); Cleared(1); EXPECT_EQ(0, second.Calls);
    const int primaryCalls = primary.Calls;
    Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 1;
    ASSERT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(primaryCalls, primary.Calls);
    Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 3;
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]); EXPECT_EQ(43u, State.Textures[2]);
    EXPECT_EQ(1, second.Calls); EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[2][CKRST_TSS_OP]);
}

TEST_F(MaterialEffectAckTest, MeshDropsTheRejectedEffectGroupAndKeepsTheValidPlainGroup) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), effect(Context);
    EffectAckTexture plainTexture(Context, 40), primary(Context, 41), extra(Context, 42);
    plain.SetTexture(0, &plainTexture); Bind(effect, primary, extra, nullptr, VXEFFECT_2TEXTURES);
    Geometry(mesh, plain, effect); Reject(1, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(40u, State.DrawTextures[0][0]); EXPECT_FLOAT_EQ(0.0f, State.FirstX[0]); Cleared(1);
}

TEST_F(MaterialEffectAckTest, TrueCallbackOwnedChainDoesNotAttemptTheRejectedOrdinaryOperation) {
    RCKMaterial material(Context); EffectAckTexture primary(Context, 41), first(Context, 42), second(Context, 43);
    Bind(material, primary, first, &second); int calls = 0; material.SetCallback(OwnAckMaterial, &calls);
    Reject(1, CKRST_TSS_OP, CKRST_TOP_MODULATE); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(1, calls); EXPECT_EQ(0, State.Rejections); EXPECT_EQ(0, primary.Calls);
    EXPECT_EQ(0, first.Calls); EXPECT_EQ(0, second.Calls);
    EXPECT_EQ(901u, State.Textures[0]); EXPECT_EQ(902u, State.Textures[1]);
    EXPECT_EQ(CKRST_TOP_ADD, State.StageStates[1][CKRST_TSS_OP]);
}
