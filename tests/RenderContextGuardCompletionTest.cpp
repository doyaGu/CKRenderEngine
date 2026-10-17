#include <gtest/gtest.h>
#include <new>
#include <type_traits>
#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKSprite3D.h"

namespace {
// Keep the same behavioral fixture runnable against the previous void Restore
// interface. A void implementation cannot report a rejected restoration.
template <class Guard> CKBOOL RestoreResult(Guard &guard) {
    if constexpr (std::is_same<decltype(guard.Restore()), void>::value) {
        guard.Restore();
        return TRUE;
    } else {
        return guard.Restore();
    }
}

class GuardBoundary : public TextureTestObserver {
public:
    CKBOOL MissingState = FALSE, MissingTexture = FALSE, MissingStage = FALSE, MissingMatrix = FALSE;
    CKBOOL QueriesFail = FALSE;
    CKBOOL RejectRestore = FALSE, RejectWorld = FALSE;
    CKBOOL RejectTexture = FALSE, RejectStage = FALSE, RejectView = FALSE;
    int RejectLightingCall = 0, LightingCalls = 0;
    int StateTargetWrites = 0, TextureTargetWrites = 0, StageTargetWrites = 0, MatrixTargetWrites = 0;
    int Draws = 0, Begins = 0, Ends = 0;
    CKBOOL GetRenderState(VXRENDERSTATETYPE state, CKDWORD *value) override {
        if (QueriesFail || (MissingState && state == VXRENDERSTATE_ZENABLE)) return FALSE;
        return TextureTestObserver::GetRenderState(state, value);
    }
    CKBOOL GetTexture(int stage, CKDWORD *texture) override {
        if (QueriesFail || (MissingTexture && stage == 2)) return FALSE;
        return TextureTestObserver::GetTexture(stage, texture);
    }
    CKBOOL GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD *value) override {
        if (QueriesFail || (MissingStage && stage == 2 && state == CKRST_TSS_MINFILTER)) return FALSE;
        return TextureTestObserver::GetTextureStageState(stage, state, value);
    }
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix) override {
        if (QueriesFail || (MissingMatrix && type == VXMATRIX_VIEW)) return FALSE;
        return TextureTestObserver::GetTransformMatrix(type, matrix);
    }
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override {
        if (state == VXRENDERSTATE_ZENABLE) ++StateTargetWrites;
        if (state == VXRENDERSTATE_LIGHTING && ++LightingCalls == RejectLightingCall) return FALSE;
        if (RejectRestore && state == VXRENDERSTATE_LIGHTING && value == 1) return FALSE;
        return TextureTestObserver::SetRenderState(state, value);
    }
    CKBOOL SetTexture(CKDWORD texture, int stage) override {
        if (stage == 2) ++TextureTargetWrites;
        if (RejectTexture && stage == 2) return FALSE;
        return TextureTestObserver::SetTexture(texture, stage);
    }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override {
        if (type == VXMATRIX_VIEW) ++MatrixTargetWrites;
        if (RejectWorld && type == VXMATRIX_WORLD) return FALSE;
        if (RejectView && type == VXMATRIX_VIEW) return FALSE;
        return TextureTestObserver::SetTransformMatrix(type, matrix);
    }
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        if (stage == 2 && state == CKRST_TSS_MINFILTER) ++StageTargetWrites;
        if (RejectStage && stage == 2 && state == CKRST_TSS_MINFILTER) return FALSE;
        // These are state-interface semantics, not a shading implementation.
        // They expose order-dependent side effects of the two legacy setters.
        if (state == CKRST_TSS_TEXTUREMAPBLEND || (state == CKRST_TSS_STAGEBLEND && value)) {
            StageStates[stage][CKRST_TSS_OP] = value ? CKRST_TOP_MODULATE : 0;
            StageStates[stage][CKRST_TSS_ARG1] = value ? CKRST_TA_TEXTURE : 0;
            StageStates[stage][CKRST_TSS_AOP] = value ? CKRST_TOP_MODULATE : 0;
        }
        if (state == CKRST_TSS_ADDRESS) {
            StageStates[stage][CKRST_TSS_ADDRESSU] = value;
            StageStates[stage][CKRST_TSS_ADDRESSV] = value;
            StageStates[stage][CKRST_TSS_ADDRESW] = value;
        }
        return TextureTestObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL BeginScene() override { ++Begins; return TRUE; }
    CKBOOL EndScene() override { ++Ends; return TRUE; }
    CKBOOL Clear(CKDWORD, CKDWORD, float, CKDWORD, int, CKRECT *) override { return TRUE; }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int, VxDrawPrimitiveData *) override { ++Draws; return TRUE; }
};

TEST(RenderContextGuardCompletionTest, UnknownCaptureValuesNeverBecomeRestoreWrites) {
    GuardBoundary state;
    state.MissingState = state.MissingTexture = state.MissingStage = state.MissingMatrix = TRUE;
    alignas(CKRenderContextStateGuard) unsigned char storage[sizeof(CKRenderContextStateGuard)];
    memset(storage, 0xCD, sizeof(storage));
    CKRenderContextStateGuard *guard = new (storage) CKRenderContextStateGuard(&state);
    state.MissingState = state.MissingTexture = state.MissingStage = state.MissingMatrix = FALSE;
    state.States[VXRENDERSTATE_ZENABLE] = 1;
    state.Textures[2] = 42;
    state.StageStates[2][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_LINEAR;
    state.Matrices[VXMATRIX_VIEW][3].x = 17;
    EXPECT_TRUE(RestoreResult(*guard));
    EXPECT_EQ(0, state.StateTargetWrites); EXPECT_EQ(0, state.TextureTargetWrites);
    EXPECT_EQ(0, state.StageTargetWrites); EXPECT_EQ(0, state.MatrixTargetWrites);
    EXPECT_EQ(1u, state.States[VXRENDERSTATE_ZENABLE]); EXPECT_EQ(42u, state.Textures[2]);
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, state.StageStates[2][CKRST_TSS_MINFILTER]);
    EXPECT_FLOAT_EQ(17, state.Matrices[VXMATRIX_VIEW][3].x);
    guard->~CKRenderContextStateGuard();
}

TEST(RenderContextGuardCompletionTest, CapturedValuesRestoreWhenLaterQueriesAreUnavailable) {
    GuardBoundary state;
    state.States[VXRENDERSTATE_ZENABLE] = 1; state.Textures[2] = 42;
    state.StageStates[2][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_LINEAR;
    state.Matrices[VXMATRIX_VIEW][3].x = 17;
    CKRenderContextStateGuard guard(&state);
    state.States[VXRENDERSTATE_ZENABLE] = 0; state.Textures[2] = 0;
    state.StageStates[2][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_NEAREST;
    state.Matrices[VXMATRIX_VIEW][3].x = 9; state.QueriesFail = TRUE;
    EXPECT_TRUE(RestoreResult(guard));
    EXPECT_EQ(1u, state.States[VXRENDERSTATE_ZENABLE]); EXPECT_EQ(42u, state.Textures[2]);
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, state.StageStates[2][CKRST_TSS_MINFILTER]);
    EXPECT_FLOAT_EQ(17, state.Matrices[VXMATRIX_VIEW][3].x);
}

TEST(RenderContextGuardCompletionTest, RejectedRestoreContinuesIndependentStatesAndIsConsumedOnce) {
    GuardBoundary state;
    state.States[VXRENDERSTATE_LIGHTING] = 1; state.States[VXRENDERSTATE_ZENABLE] = 1;
    {
        CKRenderContextStateGuard guard(&state);
        state.States[VXRENDERSTATE_LIGHTING] = 0; state.States[VXRENDERSTATE_ZENABLE] = 0;
        state.RejectRestore = TRUE;
        EXPECT_FALSE(RestoreResult(guard));
        EXPECT_EQ(0u, state.States[VXRENDERSTATE_LIGHTING]); EXPECT_EQ(1u, state.States[VXRENDERSTATE_ZENABLE]);
        EXPECT_TRUE(RestoreResult(guard));
        state.RejectRestore = FALSE; state.States[VXRENDERSTATE_ZENABLE] = 0;
    }
    EXPECT_EQ(0u, state.States[VXRENDERSTATE_ZENABLE]);
    EXPECT_EQ(0u, state.States[VXRENDERSTATE_LIGHTING]);
}

TEST(RenderContextGuardCompletionTest, LegacyBlendRestorePreservesExplicitCombineOverrides) {
    GuardBoundary state;
    state.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATE);
    state.SetTextureStageState(0, CKRST_TSS_STAGEBLEND, STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR));
    state.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    state.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    state.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_DISABLE);
    CKRenderContextStateGuard guard(&state);
    state.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, 0);
    state.SetTextureStageState(0, CKRST_TSS_STAGEBLEND, 0);
    EXPECT_TRUE(RestoreResult(guard));
    EXPECT_EQ(CKRST_TOP_SELECTARG1, state.StageStates[0][CKRST_TSS_OP]);
    EXPECT_EQ(CKRST_TA_DIFFUSE, state.StageStates[0][CKRST_TSS_ARG1]);
    EXPECT_EQ(CKRST_TOP_DISABLE, state.StageStates[0][CKRST_TSS_AOP]);
    EXPECT_EQ(STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR), state.StageStates[0][CKRST_TSS_STAGEBLEND]);
}

TEST(RenderContextGuardCompletionTest, TextureStageAndMatrixRestoreRejectionsEachReportFailureAndContinue) {
    for (int refusal = 0; refusal < 3; ++refusal) {
        SCOPED_TRACE(refusal);
        GuardBoundary state;
        state.States[VXRENDERSTATE_ZENABLE] = 1; state.Textures[2] = 42;
        state.StageStates[2][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_LINEAR;
        state.Matrices[VXMATRIX_VIEW][3].x = 17;
        CKRenderContextStateGuard guard(&state);
        state.States[VXRENDERSTATE_ZENABLE] = 0; state.Textures[2] = 0;
        state.StageStates[2][CKRST_TSS_MINFILTER] = VXTEXTUREFILTER_NEAREST;
        state.Matrices[VXMATRIX_VIEW][3].x = 9;
        state.RejectTexture = refusal == 0; state.RejectStage = refusal == 1; state.RejectView = refusal == 2;
        EXPECT_FALSE(RestoreResult(guard));
        EXPECT_EQ(1u, state.States[VXRENDERSTATE_ZENABLE]);
        EXPECT_EQ(refusal == 0 ? 0u : 42u, state.Textures[2]);
        EXPECT_EQ(refusal == 1 ? VXTEXTUREFILTER_NEAREST : VXTEXTUREFILTER_LINEAR,
                  state.StageStates[2][CKRST_TSS_MINFILTER]);
        EXPECT_FLOAT_EQ(refusal == 2 ? 9 : 17, state.Matrices[VXMATRIX_VIEW][3].x);
    }
}

TEST(RenderContextGuardCompletionTest, AddressAliasRestoresBeforeIndependentAxes) {
    GuardBoundary state;
    state.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSWRAP);
    state.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSCLAMP);
    state.SetTextureStageState(0, CKRST_TSS_ADDRESSV, VXTEXTURE_ADDRESSMIRROR);
    state.SetTextureStageState(0, CKRST_TSS_ADDRESW, VXTEXTURE_ADDRESSBORDER);
    CKRenderContextStateGuard guard(&state);
    state.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    EXPECT_TRUE(RestoreResult(guard));
    EXPECT_EQ(VXTEXTURE_ADDRESSWRAP, state.StageStates[0][CKRST_TSS_ADDRESS]);
    EXPECT_EQ(VXTEXTURE_ADDRESSCLAMP, state.StageStates[0][CKRST_TSS_ADDRESSU]);
    EXPECT_EQ(VXTEXTURE_ADDRESSMIRROR, state.StageStates[0][CKRST_TSS_ADDRESSV]);
    EXPECT_EQ(VXTEXTURE_ADDRESSBORDER, state.StageStates[0][CKRST_TSS_ADDRESW]);
}

TEST(RenderContextGuardCompletionTest, SingleUnknownCaptureDoesNotRestoreAnInventedZero) {
    GuardBoundary state; state.MissingState = TRUE;
    CKRenderStateGuard guard(&state, VXRENDERSTATE_ZENABLE);
    state.MissingState = FALSE; state.States[VXRENDERSTATE_ZENABLE] = 1;
    EXPECT_TRUE(RestoreResult(guard)); EXPECT_EQ(1u, state.States[VXRENDERSTATE_ZENABLE]);
    EXPECT_EQ(0, state.StateTargetWrites);
}

TEST(RenderContextGuardCompletionTest, SingleCapturedValueRestoresWithoutASecondSuccessfulQuery) {
    GuardBoundary state; state.States[VXRENDERSTATE_ZENABLE] = 1;
    CKRenderStateGuard guard(&state, VXRENDERSTATE_ZENABLE);
    state.States[VXRENDERSTATE_ZENABLE] = 0; state.QueriesFail = TRUE;
    EXPECT_TRUE(RestoreResult(guard)); EXPECT_EQ(1u, state.States[VXRENDERSTATE_ZENABLE]);
}

TEST(RenderContextGuardCompletionTest, SingleRejectedRestoreReportsFailureWithoutDestructorRetry) {
    GuardBoundary state; state.States[VXRENDERSTATE_LIGHTING] = 1;
    {
        CKRenderStateGuard guard(&state, VXRENDERSTATE_LIGHTING);
        state.States[VXRENDERSTATE_LIGHTING] = 0; state.RejectRestore = TRUE;
        EXPECT_FALSE(RestoreResult(guard)); state.RejectRestore = FALSE;
    }
    EXPECT_EQ(0u, state.States[VXRENDERSTATE_LIGHTING]);
}

TEST(RenderContextGuardCompletionTest, NestedGuardsRestoreTheirOwnCapturedStateAndDismissCancels) {
    GuardBoundary state; state.States[VXRENDERSTATE_ZENABLE] = 1;
    {
        CKRenderContextStateGuard outer(&state); state.States[VXRENDERSTATE_ZENABLE] = 2;
        {
            CKRenderStateGuard inner(&state, VXRENDERSTATE_ZENABLE);
            state.States[VXRENDERSTATE_ZENABLE] = 3;
        }
        EXPECT_EQ(2u, state.States[VXRENDERSTATE_ZENABLE]);
    }
    EXPECT_EQ(1u, state.States[VXRENDERSTATE_ZENABLE]);
    {
        CKRenderContextStateGuard outer(&state); CKRenderStateGuard single(&state, VXRENDERSTATE_ZENABLE);
        outer.Dismiss(); single.Dismiss(); state.States[VXRENDERSTATE_ZENABLE] = 7;
    }
    EXPECT_EQ(7u, state.States[VXRENDERSTATE_ZENABLE]);
    CKRenderContextStateGuard empty(nullptr); CKRenderStateGuard inactive(&state, VXRENDERSTATE_ZENABLE, FALSE);
    EXPECT_TRUE(RestoreResult(empty)); EXPECT_TRUE(RestoreResult(inactive));
    EXPECT_EQ(7u, state.States[VXRENDERSTATE_ZENABLE]);
}

class RenderContextGuardDrawCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver;
    GuardBoundary State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0); Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        Manager->m_Drivers[0].Caps3D.CKRasterizerSpecificCaps |= CKRST_SPECIFICCAPS_CLAMPEDGEALPHA;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->m_RasterizerContext = &State;
        Device->m_RasterizerDriver = &Driver; Device->m_Active = TRUE;
        Device->m_Settings.m_Rect.right = 320; Device->m_Settings.m_Rect.bottom = 200;
        Device->m_ViewportData.ViewWidth = 320; Device->m_ViewportData.ViewHeight = 200;
        State.Viewport = Device->m_ViewportData; Device->BeginFrameErrorTracking();
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Background() {
        RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
        ASSERT_NE(nullptr, texture); ASSERT_TRUE(texture->CreateImage(2, 2, 32, 0));
        CKBYTE *image = texture->LockSurfacePtr(0); ASSERT_NE(nullptr, image);
        memset(image, 255, 16); texture->ReleaseSurfacePtr(0);
        Device->GetBackgroundMaterial()->SetTexture(0, texture);
    }
    RCKMaterial *QueueSprite() {
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        RCKSprite3D *sprite = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
        if (!material || !sprite) return nullptr;
        sprite->SetMaterial(material); sprite->SetMode(VXSPRITE3D_ORIENTABLE); sprite->SetPosition3f(0, 0, 5);
        Device->AddSprite3DBatch(sprite); return material;
    }
    void ExpectConsumed(RCKMaterial *material, int draws) {
        EXPECT_EQ(draws, State.Draws); EXPECT_EQ(0, Device->m_Sprite3DBatches.Size());
        EXPECT_EQ(0, material->GetSprite3DBatch()->m_Vertices.Size());
        EXPECT_EQ(CKERR_INVALIDOPERATION, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    }
};

TEST_F(RenderContextGuardDrawCompletionTest, ClearReportsRestoreRefusalAndStillRestoresOtherStateAndEndsScene) {
    Background(); State.States[VXRENDERSTATE_LIGHTING] = 1; State.States[VXRENDERSTATE_ZENABLE] = 1;
    State.RejectRestore = TRUE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->Clear(CK_RENDER_CLEARBACK));
    EXPECT_EQ(1, State.Draws); EXPECT_EQ(1, State.Begins); EXPECT_EQ(1, State.Ends);
    EXPECT_EQ(1u, State.States[VXRENDERSTATE_ZENABLE]);
    EXPECT_EQ(0u, State.States[VXRENDERSTATE_LIGHTING]);
}

TEST_F(RenderContextGuardDrawCompletionTest, SpriteRestoreRefusalRecordsFrameFailureAndConsumesQueue) {
    RCKMaterial *material = QueueSprite(); ASSERT_NE(nullptr, material);
    State.States[VXRENDERSTATE_LIGHTING] = 1; State.RejectRestore = TRUE;
    Device->CallSprite3DBatches(); ExpectConsumed(material, 1);
}

TEST_F(RenderContextGuardDrawCompletionTest, SpriteInitialLightingRefusalSkipsDrawAndConsumesQueue) {
    RCKMaterial *material = QueueSprite(); ASSERT_NE(nullptr, material);
    State.RejectLightingCall = 1;
    Device->CallSprite3DBatches(); ExpectConsumed(material, 0);
    EXPECT_EQ(0, Device->m_Stats.NbObjectDrawn);
}

TEST_F(RenderContextGuardDrawCompletionTest, SpriteWorldRefusalSkipsDrawAndConsumesQueue) {
    RCKMaterial *material = QueueSprite(); ASSERT_NE(nullptr, material);
    State.RejectWorld = TRUE;
    Device->CallSprite3DBatches(); ExpectConsumed(material, 0);
}

TEST_F(RenderContextGuardDrawCompletionTest, SpriteMaterialLightingRefusalSkipsDrawAndConsumesQueue) {
    RCKMaterial *material = QueueSprite(); ASSERT_NE(nullptr, material);
    State.RejectLightingCall = 2;
    Device->CallSprite3DBatches(); ExpectConsumed(material, 0);
}
} // namespace
