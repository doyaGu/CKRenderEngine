#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"
#include "../src/CKMeshDrawScope.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SubmissionBoundary : public MaterialRenderObserver {
public:
    CKBOOL LightingAck = TRUE, ViewportAck = TRUE, DrawAck = TRUE;
    int LightingCalls = 0, ViewportCalls = 0, Draws = 0;
    CKDWORD SeenFlags = 0;
    void *SeenPositions = nullptr;
    int SeenCount = 0;
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override {
        if (state == VXRENDERSTATE_LIGHTING) {
            ++LightingCalls;
            if (!LightingAck) return FALSE;
        }
        return MaterialRenderObserver::SetRenderState(state, value);
    }
    CKBOOL SetViewport(const CKViewportData *value) override {
        ++ViewportCalls;
        return ViewportAck && MaterialRenderObserver::SetViewport(value);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int count, VxDrawPrimitiveData *data) override {
        ++Draws; SeenFlags = data->Flags; SeenPositions = data->PositionPtr; SeenCount = count;
        return DrawAck;
    }
};

class RenderContextSubmissionCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    SubmissionBoundary State;
    VxVector Positions[6], Normals[6];
    CKDWORD Colors[6] = {}, Specular[6] = {};
    VxDrawPrimitiveData Data = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State;
        Device->m_ViewportData.ViewX = 7; Device->m_ViewportData.ViewY = 9;
        Device->m_ViewportData.ViewWidth = 80; Device->m_ViewportData.ViewHeight = 60;
        Device->BeginFrameErrorTracking();
        memset(&Device->m_Stats, 0, sizeof(Device->m_Stats));
        Data.VertexCount = 6; Data.Flags = CKRST_DP_TRANSFORM;
        Data.PositionPtr = Positions; Data.PositionStride = sizeof(VxVector);
        Data.ColorPtr = Colors; Data.ColorStride = sizeof(CKDWORD);
        Data.SpecularColorPtr = Specular; Data.SpecularColorStride = sizeof(CKDWORD);
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(RenderContextSubmissionCompletionTest, SuccessfulTopologyCountsUseAcceptedGeometryAndPreserveTheCallerPacket) {
    const VXPRIMITIVETYPE types[] = {VX_POINTLIST, VX_LINELIST, VX_LINESTRIP, VX_TRIANGLELIST, VX_TRIANGLESTRIP, VX_TRIANGLEFAN};
    const int points[] = {6, 0, 0, 0, 0, 0}, lines[] = {0, 3, 5, 0, 0, 0}, triangles[] = {0, 0, 0, 2, 4, 4};
    Data.Flags = (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_VBUFFER);
    const CKDWORD callerFlags = Data.Flags;
    for (int i = 0; i < 6; ++i) {
        memset(&Device->m_Stats, 0, sizeof(Device->m_Stats));
        ASSERT_TRUE(Device->DrawPrimitive(types[i], nullptr, 99, &Data));
        EXPECT_EQ(points[i], Device->m_Stats.NbPointsDrawn);
        EXPECT_EQ(lines[i], Device->m_Stats.NbLinesDrawn);
        EXPECT_EQ(triangles[i], Device->m_Stats.NbTrianglesDrawn);
        EXPECT_EQ(6, Device->m_Stats.NbVerticesProcessed);
        EXPECT_EQ(6, State.SeenCount); EXPECT_EQ(Positions, State.SeenPositions);
        EXPECT_EQ(callerFlags, Data.Flags);
        EXPECT_EQ(0, State.SeenFlags & CKRST_DP_VBUFFER);
        EXPECT_NE(0, State.SeenFlags & CKRST_DP_DIFFUSE);
        EXPECT_NE(0, State.SeenFlags & CKRST_DP_SPECULAR);
    }
    EXPECT_EQ(7u, State.Viewport.ViewX); EXPECT_EQ(60u, State.Viewport.ViewHeight);
}

TEST_F(RenderContextSubmissionCompletionTest, RejectedLightingStopsBeforeViewportAndGeometryWithoutCountingAFalseDraw) {
    Data.Flags = (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT);
    Data.NormalPtr = Normals; Data.NormalStride = sizeof(VxVector);
    State.LightingAck = FALSE;
    EXPECT_FALSE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_EQ(1, State.LightingCalls); EXPECT_EQ(0, State.ViewportCalls); EXPECT_EQ(0, State.Draws);
    EXPECT_EQ(0, Device->m_Stats.NbVerticesProcessed);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
    State.LightingAck = TRUE; Device->BeginFrameErrorTracking();
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_EQ(1, State.Draws); EXPECT_EQ(2, Device->m_Stats.NbTrianglesDrawn);
}

TEST_F(RenderContextSubmissionCompletionTest, RejectedViewportDoesNotSubmitUsingThePreviousPhysicalViewport) {
    State.Viewport.ViewX = 200; State.ViewportAck = FALSE;
    EXPECT_FALSE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_EQ(1, State.ViewportCalls); EXPECT_EQ(0, State.Draws);
    EXPECT_EQ(200u, State.Viewport.ViewX); EXPECT_EQ(0, Device->m_Stats.NbTrianglesDrawn);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
}

TEST_F(RenderContextSubmissionCompletionTest, MeshOwnedLightingIsPreservedAndRejectedDrawDoesNotAddStatistics) {
    State.States[VXRENDERSTATE_LIGHTING] = 1;
    State.LightingAck = FALSE;
    {
        CKMeshDrawScope mesh(Device, &Data);
        ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    }
    EXPECT_EQ(0, State.LightingCalls); EXPECT_EQ(1u, State.States[VXRENDERSTATE_LIGHTING]);
    EXPECT_EQ(2, Device->m_Stats.NbTrianglesDrawn);
    State.LightingAck = TRUE; State.DrawAck = FALSE;
    EXPECT_FALSE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_EQ(2, Device->m_Stats.NbTrianglesDrawn); EXPECT_EQ(6, Device->m_Stats.NbVerticesProcessed);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
}

TEST_F(RenderContextSubmissionCompletionTest, CombinedStateGuardRestoresSelectedStateTexturesAndTransformsOnlyOnce) {
    State.States[VXRENDERSTATE_ZENABLE] = 1;
    State.Textures[0] = 17; State.StageStates[0][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
    VxMatrix world = VxMatrix::Identity(); world[3][0] = 12;
    State.SetTransformMatrix(VXMATRIX_WORLD, world);
    VxMatrix texture = VxMatrix::Identity(); texture[0][0] = 3;
    State.SetTransformMatrix(VXMATRIX_TEXTURE0, texture);
    CKRenderContextStateGuard guard(&State);
    State.SetRenderState(VXRENDERSTATE_ZENABLE, 0);
    State.SetTexture(42, 0); State.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_DISABLE);
    State.SetTransformMatrix(VXMATRIX_WORLD, VxMatrix::Identity());
    State.SetTransformMatrix(VXMATRIX_TEXTURE0, VxMatrix::Identity());
    guard.Restore();
    EXPECT_EQ(1u, State.States[VXRENDERSTATE_ZENABLE]); EXPECT_EQ(17u, State.Textures[0]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.StageStates[0][CKRST_TSS_OP]);
    EXPECT_FLOAT_EQ(12, State.World[3][0]);
    EXPECT_FLOAT_EQ(3, State.Matrices[VXMATRIX_TEXTURE0][0][0]);
    const int writes = State.WorldWrites;
    guard.Restore(); EXPECT_EQ(writes, State.WorldWrites);
}

TEST_F(RenderContextSubmissionCompletionTest, SingleStateGuardNestingAndDismissRespectTheActiveOwnersState) {
    State.States[VXRENDERSTATE_ZENABLE] = 1;
    {
        CKRenderStateGuard outer(&State, VXRENDERSTATE_ZENABLE);
        State.SetRenderState(VXRENDERSTATE_ZENABLE, 0);
        {
            CKRenderStateGuard inner(&State, VXRENDERSTATE_ZENABLE);
            State.SetRenderState(VXRENDERSTATE_ZENABLE, 2);
        }
        EXPECT_EQ(0u, State.States[VXRENDERSTATE_ZENABLE]);
    }
    EXPECT_EQ(1u, State.States[VXRENDERSTATE_ZENABLE]);
    {
        CKRenderStateGuard dismissed(&State, VXRENDERSTATE_ZENABLE);
        State.SetRenderState(VXRENDERSTATE_ZENABLE, 3); dismissed.Dismiss();
    }
    EXPECT_EQ(3u, State.States[VXRENDERSTATE_ZENABLE]);
}
} // namespace
