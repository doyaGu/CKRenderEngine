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
class OwnerAckObserver : public MaterialRenderObserver {
public:
    int Rejections = 0, PayloadCalls = 0, ResetCalls = 0, MatrixGetCalls = 0, CoordinateGetCalls = 0;
    int MatrixWrites[CKRST_MAX_TEXTURE_STAGES] = {}, CullGetCalls = 0, DrawCount = 0;
    CKBOOL RejectPayload = FALSE, RejectReset = FALSE, RejectClear = FALSE;
    CKBOOL RejectMatrixGet = FALSE, RejectCullGet = FALSE, RejectTextureGet = FALSE;
    CKBOOL RejectRender = FALSE, RejectRestoreCull = FALSE, SawRestoreRefusal = FALSE;
    int TargetStage = -1;
    CKRST_TEXTURESTAGESTATETYPE TargetStageState = CKRST_TSS_OP;
    CKDWORD StageValue = 0, RenderValue = 0, WrapHandle = 0;
    VXRENDERSTATETYPE TargetRenderState = VXRENDERSTATE_CULLMODE;
    CKDWORD DrawTexture[8] = {}, DrawCull[8] = {};
    float DrawX[8] = {};
    CKBOOL SetMaterial(const CKMaterialData *data) override {
        ++PayloadCalls;
        if (!Rejections && RejectPayload) { ++Rejections; return FALSE; }
        return MaterialRenderObserver::SetMaterial(data);
    }
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override {
        const CKBOOL restoredCull = RejectRestoreCull && state == VXRENDERSTATE_CULLMODE &&
            value == VXCULL_CCW && States[VXRENDERSTATE_CULLMODE] == VXCULL_NONE && Textures[1] == 42;
        if (!Rejections && ((RejectRender && state == TargetRenderState && value == RenderValue) ||
            (WrapHandle && state == VXRENDERSTATE_WRAP0 && Textures[0] == WrapHandle) || restoredCull)) {
            SawRestoreRefusal = restoredCull; ++Rejections; return FALSE;
        }
        return MaterialRenderObserver::SetRenderState(state, value);
    }
    CKBOOL GetRenderState(VXRENDERSTATETYPE state, CKDWORD *value) override {
        if (state == VXRENDERSTATE_CULLMODE) ++CullGetCalls;
        if (!Rejections && RejectCullGet && state == VXRENDERSTATE_CULLMODE) { ++Rejections; return FALSE; }
        return MaterialRenderObserver::GetRenderState(state, value);
    }
    CKBOOL ResetTextureStages(int first, int count) override {
        ++ResetCalls;
        if (!Rejections && RejectReset && first == TargetStage) { ++Rejections; return FALSE; }
        const CKBOOL result = MaterialRenderObserver::ResetTextureStages(first, count);
        for (int stage = first; stage < first + count; ++stage) StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_DISABLE;
        return result;
    }
    CKBOOL SetTexture(CKDWORD texture, int stage) override {
        if (!Rejections && RejectClear && !texture && stage == TargetStage) { ++Rejections; return FALSE; }
        return MaterialRenderObserver::SetTexture(texture, stage);
    }
    CKBOOL GetTexture(int stage, CKDWORD *texture) override {
        if (!Rejections && RejectTextureGet && stage == TargetStage) { ++Rejections; return FALSE; }
        return MaterialRenderObserver::GetTexture(stage, texture);
    }
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        if (!Rejections && stage == TargetStage && state == TargetStageState && value == StageValue) { ++Rejections; return FALSE; }
        if (state == CKRST_TSS_TEXTUREMAPBLEND && value == VXTEXTUREBLEND_MODULATEALPHA)
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
        return MaterialRenderObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD *value) override {
        if (state == CKRST_TSS_TEXCOORDINDEX) ++CoordinateGetCalls;
        return MaterialRenderObserver::GetTextureStageState(stage, state, value);
    }
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix) override {
        if (type >= VXMATRIX_TEXTURE0 && type < VXMATRIX_TEXTURE0 + CKRST_MAX_TEXTURE_STAGES) {
            ++MatrixGetCalls;
            if (!Rejections && RejectMatrixGet) { ++Rejections; return FALSE; }
        }
        return MaterialRenderObserver::GetTransformMatrix(type, matrix);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        if (type >= VXMATRIX_TEXTURE0 && type < VXMATRIX_TEXTURE0 + CKRST_MAX_TEXTURE_STAGES)
            ++MatrixWrites[type - VXMATRIX_TEXTURE0];
        return MaterialRenderObserver::SetTransformMatrix(type, matrix);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count); EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        EXPECT_LT(DrawCount, 8); if (!indices || !data || DrawCount >= 8) return FALSE;
        DrawTexture[DrawCount] = Textures[0]; DrawCull[DrawCount] = States[VXRENDERSTATE_CULLMODE];
        if (data->PositionPtr)
            DrawX[DrawCount] = reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + indices[0] * data->PositionStride)->x;
        ++DrawCount; return TRUE;
    }
};
class OwnerAckTexture : public RCKTexture {
public:
    CKDWORD Handle; int Calls = 0, LastStage = -1;
    OwnerAckTexture(CKContext *context, CKDWORD handle) : RCKTexture(context), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        ++Calls; LastStage = stage;
        return static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(Handle, stage);
    }
};
struct OwnerAckCall { int Calls = 0; VX_EFFECTCALLBACK_RETVAL Result = VXEFFECTRETVAL_SKIPALLTEX; };
void WriteOwnedAckChain(CKRenderContext *context, int stage) {
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    state->SetTexture(901, stage); state->SetTexture(902, stage + 1);
    state->SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    state->SetTextureStageState(stage + 1, CKRST_TSS_OP, CKRST_TOP_ADD);
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = .375f;
    state->SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), matrix);
    state->SetTextureStageState(stage, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
    state->SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX,
        CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_CAMERASPACENORMAL));
}
VX_EFFECTCALLBACK_RETVAL OwnAckEffect(CKRenderContext *context, CKMaterial *, int stage, void *argument) {
    OwnerAckCall *call = static_cast<OwnerAckCall *>(argument); ++call->Calls;
    WriteOwnedAckChain(context, stage); return call->Result;
}
int OwnAckAll(CKRenderContext *context, CKMaterial *, void *argument) {
    ++static_cast<OwnerAckCall *>(argument)->Calls; WriteOwnedAckChain(context, 0); return TRUE;
}
struct DecliningPrefixCall {
    int Stage = 0, PositiveCalls = 0, PrimaryCalls = 0;
    OwnerAckObserver *Observer = nullptr;
};
int DeclineAfterCullChange(CKRenderContext *context, CKMaterial *, void *argument) {
    DecliningPrefixCall *call = static_cast<DecliningPrefixCall *>(argument);
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    state->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    if (call->Stage > 0) {
        ++call->PositiveCalls;
        call->Observer->RejectTextureGet = TRUE; call->Observer->TargetStage = 0;
    } else {
        ++call->PrimaryCalls;
    }
    return FALSE;
}
class DecliningPrefixMaterial : public RCKMaterial {
public:
    DecliningPrefixCall &Call;
    DecliningPrefixMaterial(CKContext *context, DecliningPrefixCall &call) : RCKMaterial(context), Call(call) {
        SetCallback(DeclineAfterCullChange, &Call);
    }
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        const int previousStage = Call.Stage; Call.Stage = stage;
        const CKBOOL result = RCKMaterial::SetAsCurrent(context, lit, stage);
        Call.Stage = previousStage; return result;
    }
};
class MaterialOwnerAckTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver; OwnerAckObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3; Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 3;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0)); Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0;
        Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Reject(VXRENDERSTATETYPE state, CKDWORD value) { State.RejectRender = TRUE; State.TargetRenderState = state; State.RenderValue = value; }
    void EffectOwner(RCKMaterial &material, OwnerAckCall &call) {
        material.SetEffect(VXEFFECT_2TEXTURES);
        VxEffectDescription &description = const_cast<VxEffectDescription &>(Manager->GetEffectDescription(VXEFFECT_2TEXTURES));
        description.SetCallback = OwnAckEffect; description.CallbackArg = &call;
    }
    void Owned() {
        EXPECT_EQ(901u, State.Textures[0]); EXPECT_EQ(902u, State.Textures[1]);
        EXPECT_EQ(CKRST_TOP_ADD, State.StageStates[1][CKRST_TSS_OP]);
        EXPECT_FLOAT_EQ(.375f, State.Matrices[VXMATRIX_TEXTURE0][3][0]);
        EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[0][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    }
    static void Geometry(RCKMesh &mesh, RCKMaterial &first, RCKMaterial *second = nullptr) {
        const int faces = second ? 2 : 1;
        ASSERT_TRUE(mesh.SetVertexCount(faces * 3)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int face = 0; face < faces; ++face) {
            mesh.SetFaceVertexIndex(face, face * 3, face * 3 + 1, face * 3 + 2);
            mesh.SetFaceMaterial(face, face ? second : &first);
            for (int i = 0; i < 3; ++i) {
                VxVector point(face * 10.0f + (i == 1 ? 1.0f : 0.0f), i == 2 ? 1.0f : 0.0f, 1.0f);
                mesh.SetVertexPosition(face * 3 + i, &point); mesh.SetVertexTextureCoordinates(face * 3 + i, .125f, .25f, -1);
            }
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static void Channel(RCKMesh &mesh, RCKMaterial &material) {
        const int index = mesh.AddChannel(&material, FALSE); ASSERT_GE(index, 0); mesh.ActivateChannel(index, TRUE);
        mesh.SetChannelSourceBlend(index, VXBLEND_ZERO); mesh.SetChannelDestBlend(index, VXBLEND_SRCCOLOR);
        for (int i = 0; i < mesh.GetVertexCount(); ++i) mesh.SetVertexTextureCoordinates(i, .375f, .625f, index);
    }
};
}

TEST_F(MaterialOwnerAckTest, LitPayloadRefusalStopsTextureBindingAndCanRecover) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    State.RejectPayload = TRUE; EXPECT_FALSE(material.SetAsCurrent(Device, TRUE, 0));
    EXPECT_EQ(1, State.Rejections); EXPECT_EQ(0, texture.Calls);
    ASSERT_TRUE(material.SetAsCurrent(Device, TRUE, 0)); EXPECT_EQ(41u, State.Textures[0]);
}
TEST_F(MaterialOwnerAckTest, AlphaSourceBlendRefusalStopsBeforeTheTexture) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    material.EnableAlphaBlend(TRUE); material.SetSourceBlend(VXBLEND_SRCALPHA);
    Reject(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA); EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(1, State.Rejections); EXPECT_EQ(0, texture.Calls);
}
TEST_F(MaterialOwnerAckTest, PerspectiveRefusalAtAPositiveStageRetainsTheEarlierBinding) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    State.SetTexture(99, 0); material.EnablePerspectiveCorrection(TRUE);
    Reject(VXRENDERSTATE_TEXTUREPERSPECTIVE, TRUE); EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 2));
    EXPECT_EQ(1, State.Rejections); EXPECT_EQ(1, texture.Calls); EXPECT_EQ(99u, State.Textures[0]);
    EXPECT_EQ(0, State.MatrixWrites[2]);
}
TEST_F(MaterialOwnerAckTest, AlphaReferenceRefusalIsNotReportedAsASuccessfulMaterial) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    material.EnableAlphaTest(TRUE); material.SetAlphaRef(113); Reject(VXRENDERSTATE_ALPHAREF, 113);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(1, State.Rejections);
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(113u, State.States[VXRENDERSTATE_ALPHAREF]);
}
TEST_F(MaterialOwnerAckTest, TextureOwnerKeepsItsChainWhenAnOrdinaryCullWriteFails) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    OwnerAckCall call; EffectOwner(material, call); Reject(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0, texture.Calls); EXPECT_EQ(0, State.ResetCalls); Owned();
}
TEST_F(MaterialOwnerAckTest, CombinedTextureSkipFlagsNeverReadOrReapplyTheOwnedMatrix) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    OwnerAckCall call; call.Result = static_cast<VX_EFFECTCALLBACK_RETVAL>(VXEFFECTRETVAL_SKIPALLTEX | VXEFFECTRETVAL_SKIPTEXMAT);
    EffectOwner(material, call); State.RejectMatrixGet = TRUE;
    ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(0, State.Rejections);
    EXPECT_EQ(0, State.MatrixGetCalls); EXPECT_EQ(0, State.CoordinateGetCalls); EXPECT_EQ(0, State.ResetCalls);
    EXPECT_EQ(1, State.MatrixWrites[0]); EXPECT_EQ(0, texture.Calls); Owned();
}
TEST_F(MaterialOwnerAckTest, MatrixOnlyOwnerReadFailureStopsBeforeResetAndTextureBinding) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    OwnerAckCall call; call.Result = VXEFFECTRETVAL_SKIPTEXMAT; EffectOwner(material, call); State.RejectMatrixGet = TRUE;
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0, State.ResetCalls); EXPECT_EQ(0, texture.Calls); Owned();
}
TEST_F(MaterialOwnerAckTest, ExplicitSlotResetRefusalDoesNotBindOrModifyTheRetainedPrefix) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(1, &texture);
    State.SetTexture(99, 0); State.SetTexture(98, 2); State.RejectReset = TRUE; State.TargetStage = 2;
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 2)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0, texture.Calls); EXPECT_EQ(99u, State.Textures[0]); EXPECT_EQ(98u, State.Textures[2]);
}
TEST_F(MaterialOwnerAckTest, ExplicitEmptySlotReportsClearRefusalAndRetriesCleanupOnlyForItsStage) {
    RCKMaterial material(Context); State.SetTexture(99, 0); State.SetTexture(98, 2);
    State.RejectClear = TRUE; State.TargetStage = 2;
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 3, 2)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(99u, State.Textures[0]); EXPECT_EQ(0u, State.Textures[2]);
}
TEST_F(MaterialOwnerAckTest, ExplicitSlotSamplerRefusalClearsOnlyItsDestination) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(1, &texture);
    material.SetTextureBorderColor(0x12345678); State.SetTexture(99, 0);
    State.TargetStage = 2; State.TargetStageState = CKRST_TSS_BORDERCOLOR; State.StageValue = 0x12345678;
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 2)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(1, texture.Calls); EXPECT_EQ(0u, State.Textures[2]); EXPECT_EQ(99u, State.Textures[0]);
}
TEST_F(MaterialOwnerAckTest, ExplicitSlotCoordinateRefusalCannotReturnSuccess) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(1, &texture);
    State.TargetStage = 2; State.TargetStageState = CKRST_TSS_TEXCOORDINDEX; State.StageValue = 2;
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 2)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0u, State.Textures[2]); EXPECT_EQ(1, texture.Calls);
}
TEST_F(MaterialOwnerAckTest, UnreadableRetainedTextureCannotBeCountedAsAnEmptyPrefix) {
    RCKMaterial material(Context); OwnerAckTexture texture(Context, 41); material.SetTexture(0, &texture);
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 1;
    State.SetTexture(99, 0); State.RejectTextureGet = TRUE; State.TargetStage = 0;
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 2)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(0, texture.Calls); EXPECT_EQ(99u, State.Textures[0]);
    State.SetTexture(0, 0); ASSERT_TRUE(material.SetAsCurrent(Device, FALSE, 2)); EXPECT_EQ(41u, State.Textures[2]);
}
TEST_F(MaterialOwnerAckTest, FullMaterialOwnerDoesNotProbeOrdinaryPayloadOrCullReadback) {
    RCKMesh mesh(Context); RCKMaterial material(Context); OwnerAckCall call;
    material.SetCallback(OwnAckAll, &call); Geometry(mesh, material);
    State.RejectPayload = TRUE; State.RejectCullGet = TRUE;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(0, State.Rejections); EXPECT_EQ(0, State.PayloadCalls); EXPECT_EQ(1, call.Calls);
    EXPECT_EQ(901u, State.DrawTexture[0]); Owned();
}
TEST_F(MaterialOwnerAckTest, FailedOrdinaryMaterialIsSkippedWhileAnotherGroupStillDraws) {
    RCKMesh mesh(Context); RCKMaterial good(Context), bad(Context); OwnerAckTexture first(Context, 40), second(Context, 41);
    good.SetTexture(0, &first); bad.SetTexture(0, &second); bad.EnableAlphaBlend(TRUE); bad.SetSourceBlend(VXBLEND_SRCALPHA);
    Geometry(mesh, good, &bad); Reject(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(1, State.Rejections); EXPECT_EQ(40u, State.DrawTexture[0]); EXPECT_FLOAT_EQ(0.0f, State.DrawX[0]);
}
TEST_F(MaterialOwnerAckTest, FailedGroupWrapDoesNotSuppressTheLaterValidGroup) {
    RCKMesh mesh(Context); RCKMaterial bad(Context), good(Context); OwnerAckTexture first(Context, 41), second(Context, 40);
    bad.SetTexture(0, &first); good.SetTexture(0, &second); Geometry(mesh, bad, &good); State.WrapHandle = 41;
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(1, State.Rejections); EXPECT_EQ(40u, State.DrawTexture[0]); EXPECT_FLOAT_EQ(10.0f, State.DrawX[0]);
}
TEST_F(MaterialOwnerAckTest, FullMaterialOwnerStillRequiresTheMeshOwnedWrapWriteToSucceed) {
    RCKMesh mesh(Context); RCKMaterial material(Context); OwnerAckCall call;
    material.SetCallback(OwnAckAll, &call); Geometry(mesh, material);
    Reject(VXRENDERSTATE_CULLMODE, VXCULL_CCW); State.WrapHandle = 901;
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(0, State.DrawCount);
    EXPECT_EQ(1, State.Rejections); EXPECT_GE(call.Calls, 1); Owned();
}
TEST_F(MaterialOwnerAckTest, NeutralPrimaryStageRefusalStopsBeforeBindingTheFoldedChannel) {
    RCKMesh mesh(Context); RCKMaterial base(Context), channel(Context); OwnerAckTexture texture(Context, 42);
    channel.SetTexture(0, &texture); Geometry(mesh, base); Channel(mesh, channel);
    State.TargetStage = 0; State.TargetStageState = CKRST_TSS_OP; State.StageValue = CKRST_TOP_SELECTARG1;
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(0, State.DrawCount);
    EXPECT_EQ(1, State.Rejections); EXPECT_EQ(0, texture.Calls);
}
TEST_F(MaterialOwnerAckTest, RefusedBaseCullRestorationIsFatalRatherThanAChannelReplay) {
    RCKMesh mesh(Context); RCKMaterial base(Context), channel(Context); OwnerAckTexture texture(Context, 42);
    channel.SetTexture(0, &texture); channel.SetTwoSided(TRUE); Geometry(mesh, base); Channel(mesh, channel);
    State.RejectRestoreCull = TRUE;
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(0, State.DrawCount);
    EXPECT_EQ(1, State.Rejections); EXPECT_TRUE(State.SawRestoreRefusal);
    EXPECT_EQ(1, texture.Calls); EXPECT_EQ(1, texture.LastStage);
}
TEST_F(MaterialOwnerAckTest, UnreadableBaseCullSnapshotIsFatalAfterAnOrdinaryChannelBind) {
    RCKMesh mesh(Context); RCKMaterial base(Context), channel(Context); OwnerAckTexture texture(Context, 42);
    channel.SetTexture(0, &texture); channel.SetTwoSided(TRUE); Geometry(mesh, base); Channel(mesh, channel);
    State.RejectCullGet = TRUE;
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(0, State.DrawCount);
    EXPECT_EQ(1, State.Rejections); EXPECT_GE(State.CullGetCalls, 1);
    EXPECT_EQ(1, texture.Calls); EXPECT_EQ(1, texture.LastStage);
}
TEST_F(MaterialOwnerAckTest, DecliningCallbackEarlyPrefixFailureRestoresBaseStateBeforeLegalChannelRecovery) {
    RCKMesh mesh(Context); RCKMaterial base(Context); DecliningPrefixCall call; call.Observer = &State;
    DecliningPrefixMaterial channel(Context, call); OwnerAckTexture primary(Context, 40), layer(Context, 42);
    base.SetTexture(0, &primary); channel.SetTexture(0, &layer); Geometry(mesh, base); Channel(mesh, channel);
    // The positive-stage callback changes global state, then declines. Its
    // retained-prefix query fails before ordinary sampler/material writes.
    // The base must retain CCW coverage; the stage0 retry is a valid layer.
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(1, State.Rejections); EXPECT_EQ(1, call.PositiveCalls); EXPECT_EQ(1, call.PrimaryCalls);
    EXPECT_EQ(40u, State.DrawTexture[0]); EXPECT_EQ(42u, State.DrawTexture[1]);
    EXPECT_EQ(VXCULL_CCW, State.DrawCull[0]); EXPECT_EQ(VXCULL_CCW, State.DrawCull[1]);
    EXPECT_EQ(1, layer.Calls); EXPECT_EQ(0, layer.LastStage);
}
