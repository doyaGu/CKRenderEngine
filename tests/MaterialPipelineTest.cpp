#include <gtest/gtest.h>
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
class PipelineMaterial : public RCKMaterial {
public:
    explicit PipelineMaterial(CKContext *context) : RCKMaterial(context) {}
    void MissingParameter(VX_EFFECT effect) { m_Flags = (m_Flags & ~0x3f00u) | ((effect & 0x3f) << 8); m_EffectParameter = nullptr; }
};
struct PipelineTextureCalls { int Count = 0; int Handles[16] = {}; int Stages[16] = {}; CKBOOL Clamps[16] = {}; };
class PipelineTexture : public RCKTexture {
public:
    MaterialRenderObserver *State; PipelineTextureCalls *Trace; int Handle; CKBOOL Result = TRUE;
    PipelineTexture(CKContext *context, MaterialRenderObserver *state, PipelineTextureCalls *trace, int handle)
        : RCKTexture(context), State(state), Trace(trace), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *device, CKBOOL clamp, int stage) override {
        EXPECT_NE(nullptr, device); EXPECT_LT(Trace->Count, 16);
        if (Trace->Count < 16) { const int i = Trace->Count++; Trace->Handles[i] = Handle; Trace->Stages[i] = stage; Trace->Clamps[i] = clamp; }
        if (Result) State->SetTexture(Handle, stage); return Result;
    }
};
class MaterialPipelineTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderContext *Device = nullptr; MaterialRenderObserver State; PipelineTextureCalls Trace;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp()); CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context); Device->m_RasterizerContext = &State; ASSERT_NE(nullptr, Device->m_RenderedScene->GetRootEntity());
    }
    void TearDown() override { if (Device) { Device->m_RasterizerContext = nullptr; delete Device; } if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKTexture *Texture(RCKTexture &texture) { return reinterpret_cast<CKTexture *>(&texture); }
    static CKDWORD Bits(float value) { CKDWORD bits = 0; memcpy(&bits, &value, sizeof(value)); return bits; }
    static void Dword(CKStructHelper &helper, int index, CKDWORD value) { ASSERT_LT(index, helper.GetMemberCount()); ASSERT_NE(nullptr, helper[index]); ASSERT_EQ(CK_OK, helper[index]->SetValue(&value)); }
    static void Float(CKStructHelper &helper, int index, float value) { ASSERT_LT(index, helper.GetMemberCount()); ASSERT_NE(nullptr, helper[index]); ASSERT_EQ(CK_OK, helper[index]->SetValue(&value)); }
    void Combination(RCKMaterial &material, VX_EFFECT effect, CKDWORD first, CKDWORD firstTexGen = VXEFFECT_TGNONE, CK_ID firstRef = 0,
                     CKDWORD second = CKRST_TOP_MODULATE, CKDWORD secondTexGen = VXEFFECT_TGNONE, CK_ID secondRef = 0) {
        material.SetEffect(effect); ASSERT_NE(nullptr, material.GetEffectParameter()); CKStructHelper helper(material.GetEffectParameter());
        ASSERT_EQ(effect == VXEFFECT_3TEXTURES ? 6 : 3, helper.GetMemberCount()); Dword(helper, 0, first); Dword(helper, 1, firstTexGen); Dword(helper, 2, firstRef);
        if (effect == VXEFFECT_3TEXTURES) { Dword(helper, 3, second); Dword(helper, 4, secondTexGen); Dword(helper, 5, secondRef); }
    }
    void BumpParameters(RCKMaterial &material, float offset, CKDWORD combine = CKRST_TOP_ADDSIGNED, CKDWORD generation = VXEFFECT_TGNONE, CK_ID ref = 0) {
        material.SetEffect(VXEFFECT_BUMPENV); ASSERT_NE(nullptr, material.GetEffectParameter()); CKStructHelper helper(material.GetEffectParameter());
        ASSERT_EQ(4, helper.GetMemberCount()); Float(helper, 0, offset); Dword(helper, 1, combine); Dword(helper, 2, generation); Dword(helper, 3, ref);
    }
    void ColorAndAlpha(int stage, CKDWORD operation) {
        EXPECT_EQ(operation, State.StageStates[stage][CKRST_TSS_OP]); EXPECT_EQ(operation, State.StageStates[stage][CKRST_TSS_AOP]);
        EXPECT_EQ(CKRST_TA_TEXTURE, State.StageStates[stage][CKRST_TSS_ARG1]); EXPECT_EQ(CKRST_TA_CURRENT, State.StageStates[stage][CKRST_TSS_ARG2]);
        EXPECT_EQ(CKRST_TA_TEXTURE, State.StageStates[stage][CKRST_TSS_AARG1]); EXPECT_EQ(CKRST_TA_CURRENT, State.StageStates[stage][CKRST_TSS_AARG2]);
    }
    void Identity(int stage) { for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) EXPECT_FLOAT_EQ(r == c ? 1 : 0, State.Matrices[VXMATRIX_TEXTURE0 + stage][r][c]); }
};
}

TEST_F(MaterialPipelineTest, CombineTwoRoutesExtraSlotOneToRequestedFirstExtraStageAndCombinesBothChannels) {
    RCKMaterial material(Context); PipelineTexture primary(Context, &State, &Trace, 41), extra(Context, &State, &Trace, 42);
    material.SetTexture(0, Texture(primary)); material.SetTexture(1, Texture(extra)); Combination(material, VXEFFECT_2TEXTURES, CKRST_TOP_ADD);
    State.Textures[0] = 91; ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 3)); ASSERT_EQ(1, Trace.Count);
    EXPECT_EQ(42, Trace.Handles[0]); EXPECT_EQ(3, Trace.Stages[0]); EXPECT_EQ(42u, State.Textures[3]); EXPECT_EQ(91u, State.Textures[0]); ColorAndAlpha(3, CKRST_TOP_ADD);
}
TEST_F(MaterialPipelineTest, CombineThreeReadsSixRealStructMembersAndUsesIndependentExtraReferences) {
    RCKMaterial material(Context); PipelineTexture first(Context, &State, &Trace, 42), second(Context, &State, &Trace, 43); RCK3dEntity reference(Context);
    VxMatrix world = VxMatrix::Identity(); world[3].Set(7, 11, 13, 1); reference.SetWorldMatrix(world);
    material.SetTexture(1, Texture(first)); material.SetTexture(2, Texture(second));
    Combination(material, VXEFFECT_3TEXTURES, CKRST_TOP_ADDSIGNED, VXEFFECT_TGNONE, 0, CKRST_TOP_SUBTRACT, VXEFFECT_TGTRANSFORM, reference.GetID());
    ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 3)); ASSERT_EQ(2, Trace.Count); EXPECT_EQ(42, Trace.Handles[0]); EXPECT_EQ(3, Trace.Stages[0]); EXPECT_EQ(43, Trace.Handles[1]); EXPECT_EQ(4, Trace.Stages[1]);
    ColorAndAlpha(3, CKRST_TOP_ADDSIGNED); ColorAndAlpha(4, CKRST_TOP_SUBTRACT); Identity(3); EXPECT_FLOAT_EQ(7, State.Matrices[VXMATRIX_TEXTURE0 + 4][3][0]); EXPECT_FLOAT_EQ(13, State.Matrices[VXMATRIX_TEXTURE0 + 4][3][2]);
}
TEST_F(MaterialPipelineTest, CombineTwoTexGenReferenceUsesActualEntityTransformAtDestinationStage) {
    RCKMaterial material(Context); PipelineTexture extra(Context, &State, &Trace, 42); RCK3dEntity reference(Context); VxMatrix world = VxMatrix::Identity(); world[3].Set(17, 19, 23, 1); reference.SetWorldMatrix(world);
    material.SetTexture(1, Texture(extra)); Combination(material, VXEFFECT_2TEXTURES, CKRST_TOP_MODULATE, VXEFFECT_TGTRANSFORM, reference.GetID());
    ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 2)); EXPECT_FLOAT_EQ(17, State.Matrices[VXMATRIX_TEXTURE0 + 2][3][0]); EXPECT_FLOAT_EQ(23, State.Matrices[VXMATRIX_TEXTURE0 + 2][3][2]);
}
TEST_F(MaterialPipelineTest, MissingCombinationParametersUseModulatePassThroughForBothExtraTextures) {
    PipelineMaterial material(Context); PipelineTexture first(Context, &State, &Trace, 42), second(Context, &State, &Trace, 43); material.SetTexture(1, Texture(first)); material.SetTexture(2, Texture(second)); material.MissingParameter(VXEFFECT_3TEXTURES);
    ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 2)); ColorAndAlpha(2, CKRST_TOP_MODULATE); ColorAndAlpha(3, CKRST_TOP_MODULATE);
    EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_PASSTHRU), State.StageStates[2][CKRST_TSS_TEXCOORDINDEX]); EXPECT_EQ(CKRSTPackTexcoordIndex(3, CKRST_TEXGEN_PASSTHRU), State.StageStates[3][CKRST_TSS_TEXCOORDINDEX]); Identity(2); Identity(3);
}
TEST_F(MaterialPipelineTest, CombineOperationZeroDisablesBothChannelsAndInvalidEnumUsesModulateFallback) {
    RCKMaterial material(Context); PipelineTexture extra(Context, &State, &Trace, 42); material.SetTexture(1, Texture(extra));
    const CKDWORD values[] = {0, CKRST_TOP_SUBTRACT, 999}; const CKDWORD expected[] = {CKRST_TOP_DISABLE, CKRST_TOP_SUBTRACT, CKRST_TOP_MODULATE};
    for (int i = 0; i < 3; ++i) { Combination(material, VXEFFECT_2TEXTURES, values[i]); ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 2)); ColorAndAlpha(2, expected[i]); }
}
TEST_F(MaterialPipelineTest, CombineTextureBindingFailureClearsFailedStageAndReturnsFailureMarker) {
    RCKMaterial material(Context); PipelineTexture first(Context, &State, &Trace, 42), second(Context, &State, &Trace, 43); first.Result = FALSE;
    material.SetTexture(1, Texture(first)); material.SetTexture(2, Texture(second)); Combination(material, VXEFFECT_3TEXTURES, CKRST_TOP_ADD);
    State.Textures[3] = 91; State.Textures[0] = 92; EXPECT_EQ(0x80000000u, material.BlendTexturesEffect(Device, 3));
    ASSERT_EQ(1, Trace.Count); EXPECT_EQ(42, Trace.Handles[0]); EXPECT_EQ(0u, State.Textures[3]); EXPECT_EQ(92u, State.Textures[0]);
}
TEST_F(MaterialPipelineTest, FailedSecondExtraTexturePreservesSuccessfulFirstExtraAndResetsFailureStage) {
    RCKMaterial material(Context); PipelineTexture first(Context, &State, &Trace, 42), second(Context, &State, &Trace, 43); second.Result = FALSE;
    material.SetTexture(1, Texture(first)); material.SetTexture(2, Texture(second)); Combination(material, VXEFFECT_3TEXTURES, CKRST_TOP_ADD);
    State.Textures[4] = 91; EXPECT_EQ(0x80000000u, material.BlendTexturesEffect(Device, 3)); ASSERT_EQ(2, Trace.Count); EXPECT_EQ(42u, State.Textures[3]); EXPECT_EQ(0u, State.Textures[4]);
}
TEST_F(MaterialPipelineTest, MaterialApplicationPropagatesExtraTextureFailureInsteadOfReportingSuccess) {
    RCKMaterial material(Context); PipelineTexture primary(Context, &State, &Trace, 41), extra(Context, &State, &Trace, 42); extra.Result = FALSE;
    material.SetTexture(0, Texture(primary)); material.SetTexture(1, Texture(extra)); Combination(material, VXEFFECT_2TEXTURES, CKRST_TOP_ADD);
    EXPECT_FALSE(material.SetAsCurrent(Device, FALSE, 2)); EXPECT_EQ(0u, State.Textures[3]);
}
TEST_F(MaterialPipelineTest, BumpDefaultsBindBaseBumpAndEnvironmentToThreeDistinctStages) {
    PipelineMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43);
    material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env)); material.MissingParameter(VXEFFECT_BUMPENV);
    State.Matrices[VXMATRIX_TEXTURE0][3][0] = 9; State.Matrices[VXMATRIX_TEXTURE0 + 1][3][0] = 10;
    ASSERT_EQ(2u, material.BumpMapEnvEffect(Device)); ASSERT_EQ(3, Trace.Count);
    for (int i = 0; i < 3; ++i) { EXPECT_EQ(41 + i, Trace.Handles[i]); EXPECT_EQ(i, Trace.Stages[i]); }
    EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.StageStates[1][CKRST_TSS_OP]); EXPECT_EQ(Bits(2.0f), State.StageStates[1][CKRST_TSS_BUMPENVMAT00]); EXPECT_EQ(Bits(2.0f), State.StageStates[1][CKRST_TSS_BUMPENVMAT11]);
    EXPECT_EQ(CKRST_TOP_ADDSIGNED, State.StageStates[2][CKRST_TSS_OP]); EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.StageStates[2][CKRST_TSS_TEXCOORDINDEX]); Identity(0); Identity(1);
}
TEST_F(MaterialPipelineTest, TypedBumpParametersAddTwoToAmplitudeAndReadCombineTexGenAndEntityReference) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43); RCK3dEntity reference(Context);
    VxMatrix world = VxMatrix::Identity(); world[3].Set(7, 11, 13, 1); reference.SetWorldMatrix(world); material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env));
    BumpParameters(material, .25f, CKRST_TOP_SUBTRACT, VXEFFECT_TGTRANSFORM, reference.GetID()); ASSERT_EQ(2u, material.BumpMapEnvEffect(Device));
    EXPECT_EQ(Bits(2.25f), State.StageStates[1][CKRST_TSS_BUMPENVMAT00]); EXPECT_EQ(Bits(2.25f), State.StageStates[1][CKRST_TSS_BUMPENVMAT11]);
    EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_BUMPENVMAT01]); EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_BUMPENVMAT10]); EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_BUMPENVLSCALE]); EXPECT_EQ(0u, State.StageStates[1][CKRST_TSS_BUMPENVLOFFSET]);
    EXPECT_EQ(CKRST_TOP_SUBTRACT, State.StageStates[2][CKRST_TSS_OP]); EXPECT_FLOAT_EQ(7, State.Matrices[VXMATRIX_TEXTURE0 + 2][3][0]); EXPECT_FLOAT_EQ(13, State.Matrices[VXMATRIX_TEXTURE0 + 2][3][2]);
}
TEST_F(MaterialPipelineTest, BumpFloatStateStoresExactZeroAndNegativeAmplitudeWithoutIntegerConversion) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42); material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump));
    const float offsets[] = {-2.0f, -4.0f}; const CKDWORD expected[] = {0x00000000u, 0xC0000000u};
    for (int i = 0; i < 2; ++i) { BumpParameters(material, offsets[i]); ASSERT_EQ(2u, material.BumpMapEnvEffect(Device)); EXPECT_EQ(expected[i], State.StageStates[1][CKRST_TSS_BUMPENVMAT00]); EXPECT_EQ(expected[i], State.StageStates[1][CKRST_TSS_BUMPENVMAT11]); }
}
TEST_F(MaterialPipelineTest, MissingBumpTextureRoutesEnvironmentSlotTwoToStageOneWithBaseAndEnvironmentFiltering) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), env(Context, &State, &Trace, 43); material.SetTexture(0, Texture(base)); material.SetTexture(2, Texture(env));
    material.SetTextureMinMode(VXTEXTUREFILTER_NEAREST); material.SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP); material.SetTextureBorderColor(0x12345678); BumpParameters(material, 0);
    ASSERT_EQ(2u, material.BumpMapEnvEffect(Device)); ASSERT_EQ(2, Trace.Count); EXPECT_EQ(0, Trace.Stages[0]); EXPECT_EQ(1, Trace.Stages[1]); EXPECT_EQ(43, Trace.Handles[1]); EXPECT_TRUE(Trace.Clamps[0]); EXPECT_FALSE(Trace.Clamps[1]);
    for (int stage = 0; stage < 2; ++stage) { EXPECT_EQ(VXTEXTUREFILTER_NEAREST, State.StageStates[stage][CKRST_TSS_MINFILTER]); EXPECT_EQ(0x12345678u, State.StageStates[stage][CKRST_TSS_BORDERCOLOR]); EXPECT_EQ(VXTEXTURE_ADDRESSCLAMP, State.StageStates[stage][CKRST_TSS_ADDRESSU]); EXPECT_EQ(VXTEXTURE_ADDRESSCLAMP, State.StageStates[stage][CKRST_TSS_ADDRESW]); }
}
TEST_F(MaterialPipelineTest, OptionalBumpUsesLinearFilterWhileEnvironmentUsesMaterialFilter) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43); material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env));
    material.SetTextureMinMode(VXTEXTUREFILTER_NEAREST); BumpParameters(material, 0); ASSERT_EQ(2u, material.BumpMapEnvEffect(Device));
    EXPECT_EQ(VXTEXTUREFILTER_NEAREST, State.StageStates[0][CKRST_TSS_MINFILTER]); EXPECT_EQ(VXTEXTUREFILTER_LINEAR, State.StageStates[1][CKRST_TSS_MINFILTER]); EXPECT_EQ(VXTEXTUREFILTER_NEAREST, State.StageStates[2][CKRST_TSS_MINFILTER]);
}
TEST_F(MaterialPipelineTest, BumpBaseFailureStopsOptionalTextureTraversalAndResetsBaseStage) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43); base.Result = FALSE;
    material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env)); BumpParameters(material, 0); State.Textures[0] = 91;
    EXPECT_EQ(0x80000000u, material.BumpMapEnvEffect(Device)); EXPECT_EQ(1, Trace.Count); EXPECT_EQ(0u, State.Textures[0]);
}
TEST_F(MaterialPipelineTest, BumpOptionalFailureStopsEnvironmentBindingAndResetsItsStage) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43); bump.Result = FALSE;
    material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env)); BumpParameters(material, 0); State.Textures[1] = 91;
    EXPECT_EQ(0x80000000u, material.BumpMapEnvEffect(Device)); EXPECT_EQ(2, Trace.Count); EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(0u, State.Textures[1]);
}
TEST_F(MaterialPipelineTest, BumpEnvironmentFailurePreservesBaseAndBumpBindingsAndResetsEnvironmentStage) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43); env.Result = FALSE;
    material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env)); BumpParameters(material, 0); State.Textures[2] = 91;
    EXPECT_EQ(0x80000000u, material.BumpMapEnvEffect(Device)); EXPECT_EQ(3, Trace.Count); EXPECT_EQ(41u, State.Textures[0]); EXPECT_EQ(42u, State.Textures[1]); EXPECT_EQ(0u, State.Textures[2]);
}
TEST_F(MaterialPipelineTest, MissingBumpBaseReturnsWithoutBindingOptionalTextures) {
    RCKMaterial material(Context); PipelineTexture bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env));
    EXPECT_EQ(0u, material.BumpMapEnvEffect(Device)); EXPECT_EQ(0, Trace.Count);
}
TEST_F(MaterialPipelineTest, MissingEnvironmentDisablesTheUnusedEnvironmentStageAfterBump) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42); material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); BumpParameters(material, 0);
    State.StageStates[2][CKRST_TSS_OP] = CKRST_TOP_ADD; ASSERT_EQ(2u, material.BumpMapEnvEffect(Device)); EXPECT_EQ(CKRST_TOP_DISABLE, State.StageStates[2][CKRST_TSS_OP]);
}
TEST_F(MaterialPipelineTest, UnreadableRealStructMembersRetainIndependentBumpAmplitudeAndCombineDefaults) {
    RCKMaterial material(Context); PipelineTexture base(Context, &State, &Trace, 41), bump(Context, &State, &Trace, 42), env(Context, &State, &Trace, 43);
    material.SetTexture(0, Texture(base)); material.SetTexture(1, Texture(bump)); material.SetTexture(2, Texture(env)); BumpParameters(material, .25f, CKRST_TOP_SUBTRACT);
    CKParameterTypeDesc empty; empty.Guid = CKGUID(0x7E44B088, 0x20000001); empty.TypeName = "Test empty effect member"; empty.DefaultSize = 0;
    ASSERT_EQ(CK_OK, Context->GetParameterManager()->RegisterParameterType(&empty)); CKStructHelper helper(material.GetEffectParameter());
    ASSERT_NE(nullptr, helper[0]); ASSERT_NE(nullptr, helper[1]); helper[0]->SetGUID(empty.Guid); helper[1]->SetGUID(empty.Guid);
    ASSERT_EQ(0, helper[0]->GetDataSize()); ASSERT_EQ(0, helper[1]->GetDataSize());
    float unreadableFloat = 0; CKDWORD unreadableDword = 0; ASSERT_NE(CK_OK, helper[0]->GetValue(&unreadableFloat)); ASSERT_NE(CK_OK, helper[1]->GetValue(&unreadableDword));
    ASSERT_EQ(2u, material.BumpMapEnvEffect(Device)); EXPECT_EQ(Bits(2.0f), State.StageStates[1][CKRST_TSS_BUMPENVMAT00]); EXPECT_EQ(CKRST_TOP_ADDSIGNED, State.StageStates[2][CKRST_TSS_OP]);
}

TEST_F(MaterialPipelineTest, CombineThreeUsesTwoDistinctLiveReferencesWithoutCrossingParameterTriplets) {
    RCKMaterial material(Context);
    PipelineTexture first(Context, &State, &Trace, 42), second(Context, &State, &Trace, 43);
    RCK3dEntity firstReference(Context), secondReference(Context);
    VxMatrix a = VxMatrix::Identity(), b = VxMatrix::Identity();
    a[3].Set(3, 5, 7, 1); b[3].Set(11, 13, 17, 1);
    firstReference.SetWorldMatrix(a); secondReference.SetWorldMatrix(b);
    material.SetTexture(1, Texture(first)); material.SetTexture(2, Texture(second));
    Combination(material, VXEFFECT_3TEXTURES,
                CKRST_TOP_ADDSIGNED, VXEFFECT_TGTRANSFORM, firstReference.GetID(),
                CKRST_TOP_SUBTRACT, VXEFFECT_TGTRANSFORM, secondReference.GetID());

    ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 3));
    ColorAndAlpha(3, CKRST_TOP_ADDSIGNED); ColorAndAlpha(4, CKRST_TOP_SUBTRACT);
    EXPECT_EQ(42u, State.Textures[3]); EXPECT_EQ(43u, State.Textures[4]);
    const float firstXYZ[3] = {3, 5, 7}, secondXYZ[3] = {11, 13, 17};
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(firstXYZ[i], State.Matrices[VXMATRIX_TEXTURE0 + 3][3][i]);
        EXPECT_FLOAT_EQ(secondXYZ[i], State.Matrices[VXMATRIX_TEXTURE0 + 4][3][i]);
    }
    EXPECT_FLOAT_EQ(3, State.Matrices[VXMATRIX_TEXTURE0 + 3][2][0]);
    EXPECT_FLOAT_EQ(5, State.Matrices[VXMATRIX_TEXTURE0 + 3][2][1]);
    EXPECT_FLOAT_EQ(11, State.Matrices[VXMATRIX_TEXTURE0 + 4][2][0]);
    EXPECT_FLOAT_EQ(13, State.Matrices[VXMATRIX_TEXTURE0 + 4][2][1]);
    for (int stage = 3; stage < 5; ++stage) {
        EXPECT_EQ(CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_PASSTHRU), State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX]);
        EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    }
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
        EXPECT_FLOAT_EQ(a[row][column], firstReference.GetWorldMatrix()[row][column]);
        EXPECT_FLOAT_EQ(b[row][column], secondReference.GetWorldMatrix()[row][column]);
    }
}

TEST_F(MaterialPipelineTest, CombineThreeRereadsEditedTripletsAndLiveTransformsWithoutKeepingPriorReferences) {
    RCKMaterial material(Context);
    PipelineTexture first(Context, &State, &Trace, 42), second(Context, &State, &Trace, 43);
    RCK3dEntity a(Context), b(Context), replacement(Context);
    VxMatrix world = VxMatrix::Identity();
    world[3].Set(3, 5, 7, 1); a.SetWorldMatrix(world);
    world[3].Set(11, 13, 17, 1); b.SetWorldMatrix(world);
    world[3].Set(23, 29, 31, 1); replacement.SetWorldMatrix(world);
    material.SetTexture(1, Texture(first)); material.SetTexture(2, Texture(second));
    Combination(material, VXEFFECT_3TEXTURES,
                CKRST_TOP_ADDSIGNED, VXEFFECT_TGTRANSFORM, a.GetID(),
                CKRST_TOP_SUBTRACT, VXEFFECT_TGTRANSFORM, b.GetID());
    ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 3));

    CKStructHelper parameters(material.GetEffectParameter());
    Dword(parameters, 0, CKRST_TOP_SUBTRACT); Dword(parameters, 1, VXEFFECT_TGNONE); Dword(parameters, 2, 0);
    Dword(parameters, 3, CKRST_TOP_ADD); Dword(parameters, 5, replacement.GetID());
    ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 3));
    Identity(3); ColorAndAlpha(3, CKRST_TOP_SUBTRACT); ColorAndAlpha(4, CKRST_TOP_ADD);
    EXPECT_FLOAT_EQ(23, State.Matrices[VXMATRIX_TEXTURE0 + 4][3][0]);
    EXPECT_FLOAT_EQ(29, State.Matrices[VXMATRIX_TEXTURE0 + 4][3][1]);
    EXPECT_FLOAT_EQ(31, State.Matrices[VXMATRIX_TEXTURE0 + 4][3][2]);
    EXPECT_EQ(CKRST_TTF_NONE, State.StageStates[3][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[4][CKRST_TSS_TEXTURETRANSFORMFLAGS]);

    world[3].Set(37, 41, 43, 1); a.SetWorldMatrix(world);
    Dword(parameters, 1, VXEFFECT_TGTRANSFORM); Dword(parameters, 2, a.GetID());
    Dword(parameters, 4, VXEFFECT_TGNONE); Dword(parameters, 5, 0);
    ASSERT_EQ(2u, material.BlendTexturesEffect(Device, 3));
    ColorAndAlpha(3, CKRST_TOP_SUBTRACT); ColorAndAlpha(4, CKRST_TOP_ADD); Identity(4);
    EXPECT_FLOAT_EQ(37, State.Matrices[VXMATRIX_TEXTURE0 + 3][3][0]);
    EXPECT_FLOAT_EQ(41, State.Matrices[VXMATRIX_TEXTURE0 + 3][3][1]);
    EXPECT_FLOAT_EQ(43, State.Matrices[VXMATRIX_TEXTURE0 + 3][3][2]);
    EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[3][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    EXPECT_EQ(CKRST_TTF_NONE, State.StageStates[4][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
}
