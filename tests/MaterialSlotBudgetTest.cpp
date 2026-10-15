#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SlotBudgetObserver : public MaterialRenderObserver {
public:
    int ResetCalls = 0, TextureWrites = 0, StageWrites = 0, MatrixWrites = 0;
    int TextureReads[CKRST_MAX_TEXTURE_STAGES] = {};
    int RejectReadStage = -1, Rejections = 0, RejectBorderStage = -1;
    CKBOOL ResetTextureStages(int first, int count) override {
        ++ResetCalls;
        const CKBOOL result = MaterialRenderObserver::ResetTextureStages(first, count);
        for (int stage = first; stage < first + count; ++stage) StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_DISABLE;
        return result;
    }
    CKBOOL SetTexture(CKDWORD texture, int stage) override {
        ++TextureWrites; return MaterialRenderObserver::SetTexture(texture, stage);
    }
    CKBOOL GetTexture(int stage, CKDWORD *texture) override {
        ++TextureReads[stage];
        if (!Rejections && stage == RejectReadStage) { ++Rejections; return FALSE; }
        return MaterialRenderObserver::GetTexture(stage, texture);
    }
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        ++StageWrites;
        if (!Rejections && stage == RejectBorderStage && state == CKRST_TSS_BORDERCOLOR) {
            ++Rejections; return FALSE;
        }
        return MaterialRenderObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        ++MatrixWrites; return MaterialRenderObserver::SetTransformMatrix(type, matrix);
    }
    void ClearActivity() { ResetCalls = TextureWrites = StageWrites = MatrixWrites = 0; }
};
class SlotBudgetTexture : public RCKTexture {
public:
    CKDWORD Handle; int Calls = 0, Stage = -1;
    SlotBudgetTexture(CKContext *context, CKDWORD handle) : RCKTexture(context), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        ++Calls; Stage = stage;
        return static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(Handle, stage);
    }
};
class MaterialSlotBudgetTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver; SlotBudgetObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Manager->m_DriverCount = 1;
        Manager->m_Drivers = new VxDriverDescEx[1](); Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0)); Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0;
        Caps(3, 8);
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Caps(CKDWORD textures, CKDWORD blends) {
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = textures;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = blends;
    }
    void Seed(int stage, CKDWORD handle) {
        State.SetTexture(handle, stage);
        State.SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_ADD);
        State.SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX, static_cast<CKDWORD>(stage));
        State.SetTextureStageState(stage, CKRST_TSS_CONSTANT, 0xFF000000u + stage);
        VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = stage + .25f;
        State.SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), matrix);
    }
    void Retained(int stage, CKDWORD handle) {
        EXPECT_EQ(handle, State.Textures[stage]); EXPECT_EQ(CKRST_TOP_ADD, State.StageStates[stage][CKRST_TSS_OP]);
        EXPECT_EQ(static_cast<CKDWORD>(stage), State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX]);
        EXPECT_EQ(0xFF000000u + stage, State.StageStates[stage][CKRST_TSS_CONSTANT]);
        EXPECT_FLOAT_EQ(stage + .25f, State.Matrices[VXMATRIX_TEXTURE0 + stage][3][0]);
    }
    void NoWrites() {
        EXPECT_EQ(0, State.ResetCalls); EXPECT_EQ(0, State.TextureWrites);
        EXPECT_EQ(0, State.StageWrites); EXPECT_EQ(0, State.MatrixWrites);
    }
};
}

TEST_F(MaterialSlotBudgetTest, TargetZeroCannotIgnoreARetainedHighStageTexture) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(7, 77); Caps(1, 8); State.ClearActivity();
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 0)); NoWrites();
    EXPECT_EQ(0, selected.Calls); Retained(7, 77); EXPECT_EQ(0u, State.Textures[0]);
}
TEST_F(MaterialSlotBudgetTest, PrefixAndSuffixMustBothFitTheConfiguredBindingBudget) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(0, 70); Seed(7, 77); Caps(2, 8); State.ClearActivity();
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 2)); NoWrites();
    EXPECT_EQ(0, selected.Calls); Retained(0, 70); Retained(7, 77);
}
TEST_F(MaterialSlotBudgetTest, ExactCapacityReplacesOnlyTheSelectedDestinationAndPreservesTheSuffix) {
    RCKMaterial material(Context); SlotBudgetTexture unused(Context, 40), selected(Context, 42);
    material.SetTexture(0, &unused); material.SetTexture(3, &selected); Seed(0, 70); Seed(7, 77); Caps(3, 8);
    ASSERT_TRUE(material.BindTextureSlotToStage(Device, 3, 2));
    EXPECT_EQ(0, unused.Calls); EXPECT_EQ(1, selected.Calls); EXPECT_EQ(2, selected.Stage);
    EXPECT_EQ(42u, State.Textures[2]); EXPECT_EQ(2u, State.StageStates[2][CKRST_TSS_TEXCOORDINDEX]);
    Retained(0, 70); Retained(7, 77);
}
TEST_F(MaterialSlotBudgetTest, ReplacedDestinationIsNotCountedAsAnAdditionalRetainedBinding) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(0, 70); Seed(2, 98); Seed(7, 77); Caps(3, 8);
    ASSERT_TRUE(material.BindTextureSlotToStage(Device, 1, 2));
    EXPECT_EQ(42u, State.Textures[2]); EXPECT_EQ(1, selected.Calls); Retained(0, 70); Retained(7, 77);
}
TEST_F(MaterialSlotBudgetTest, ReplacingAnExistingDestinationCannotHideAnOverCapacitySuffix) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(2, 98); Seed(7, 77); Caps(1, 8); State.ClearActivity();
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 2)); NoWrites();
    EXPECT_EQ(0, selected.Calls); Retained(2, 98); Retained(7, 77);
}
TEST_F(MaterialSlotBudgetTest, UnknownCapabilityLimitsDoNotRequireTextureReadback) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    for (int stage = 0; stage < 7; ++stage) Seed(stage, 70 + stage);
    Caps(0, 0); State.RejectReadStage = 0;
    ASSERT_TRUE(material.BindTextureSlotToStage(Device, 1, 7));
    EXPECT_EQ(0, State.Rejections); EXPECT_EQ(42u, State.Textures[7]);
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) EXPECT_EQ(0, State.TextureReads[stage]);
    for (int stage = 0; stage < 7; ++stage) Retained(stage, 70 + stage);
}
TEST_F(MaterialSlotBudgetTest, PhysicalBlendStageLimitAppliesToTheNonNullDestinationOrdinal) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(0, 70); Seed(7, 77); Caps(8, 3); State.ClearActivity();
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 3)); NoWrites();
    EXPECT_EQ(0, selected.Calls); Retained(0, 70); Retained(7, 77);
}
TEST_F(MaterialSlotBudgetTest, ASparseHighDestinationConsumesOneTextureBinding) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Caps(1, 8); ASSERT_TRUE(material.BindTextureSlotToStage(Device, 1, 5));
    EXPECT_EQ(1, selected.Calls); EXPECT_EQ(5, selected.Stage); EXPECT_EQ(42u, State.Textures[5]);
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
        if (stage != 5) EXPECT_EQ(0u, State.Textures[stage]);
}
TEST_F(MaterialSlotBudgetTest, EmptySlotCanRemoveAnOldOverBlendBindingAndRepairTextureCapacity) {
    RCKMaterial material(Context); Seed(5, 98); Seed(7, 77); Caps(1, 1);
    ASSERT_TRUE(material.BindTextureSlotToStage(Device, 3, 5));
    EXPECT_EQ(0u, State.Textures[5]); EXPECT_EQ(CKRST_TOP_DISABLE, State.StageStates[5][CKRST_TSS_OP]);
    Retained(7, 77);
}
TEST_F(MaterialSlotBudgetTest, EmptySlotDoesNotMutateAChainThatWouldRemainOverTextureCapacity) {
    RCKMaterial material(Context); Seed(0, 70); Seed(2, 98); Seed(7, 77); Caps(1, 8); State.ClearActivity();
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 3, 2)); NoWrites();
    Retained(0, 70); Retained(2, 98); Retained(7, 77);
}
TEST_F(MaterialSlotBudgetTest, UnreadableRetainedSuffixRejectsBeforeMutationAndCanRetry) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(6, 76); Caps(2, 8); State.RejectReadStage = 6; State.ClearActivity();
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 0)); EXPECT_EQ(1, State.Rejections); NoWrites();
    EXPECT_EQ(0, selected.Calls); Retained(6, 76);
    ASSERT_TRUE(material.BindTextureSlotToStage(Device, 1, 0));
    EXPECT_EQ(1, selected.Calls); EXPECT_EQ(42u, State.Textures[0]); Retained(6, 76);
}
TEST_F(MaterialSlotBudgetTest, DestinationReadbackIsNotNeededWhenItsBindingIsBeingReplaced) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(2, 98); Caps(1, 8); State.RejectReadStage = 2;
    ASSERT_TRUE(material.BindTextureSlotToStage(Device, 1, 2));
    EXPECT_EQ(0, State.Rejections); EXPECT_EQ(0, State.TextureReads[2]); EXPECT_EQ(42u, State.Textures[2]);
}
TEST_F(MaterialSlotBudgetTest, ADisabledSuffixStillCountsAsARetainedConfiguredBinding) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(7, 77); State.SetTextureStageState(7, CKRST_TSS_OP, CKRST_TOP_DISABLE); Caps(1, 8); State.ClearActivity();
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 0)); NoWrites();
    EXPECT_EQ(0, selected.Calls); EXPECT_EQ(77u, State.Textures[7]);
    EXPECT_EQ(CKRST_TOP_DISABLE, State.StageStates[7][CKRST_TSS_OP]);
}
TEST_F(MaterialSlotBudgetTest, AcceptedBudgetThenSamplerRefusalCleansOnlyTheTargetAndPreservesItsSuffix) {
    RCKMaterial material(Context); SlotBudgetTexture selected(Context, 42); material.SetTexture(1, &selected);
    Seed(0, 70); Seed(7, 77); Caps(3, 8); State.RejectBorderStage = 2;
    EXPECT_FALSE(material.BindTextureSlotToStage(Device, 1, 2)); EXPECT_EQ(1, State.Rejections);
    EXPECT_EQ(1, selected.Calls); EXPECT_EQ(0u, State.Textures[2]); Retained(0, 70); Retained(7, 77);
}
