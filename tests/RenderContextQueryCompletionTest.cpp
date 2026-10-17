#include "TextureTestObserver.h"

namespace {
class QueryBoundary : public TextureTestObserver {
public:
    CKBOOL MatrixAck = TRUE, StateAck = TRUE;
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix) override {
        if (MatrixAck) return TextureTestObserver::GetTransformMatrix(type, matrix);
        matrix.SetIdentity(); matrix[3][0] = 999;
        return FALSE;
    }
    CKBOOL GetRenderState(VXRENDERSTATETYPE type, CKDWORD *value) override {
        if (StateAck) return TextureTestObserver::GetRenderState(type, value);
        *value = 0xdeadbeefu;
        return FALSE;
    }
};
class RenderContextQueryCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    QueryBoundary Boundary;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static VxMatrix Translation(float x, float y, float z) {
        VxMatrix result = VxMatrix::Identity(); result[3].Set(x, y, z, 1); return result;
    }
};
}

TEST_F(RenderContextQueryCompletionTest, QueriesObserveDirectBackendWritesWithoutReplacingCpuCameraMatrices) {
    Device->SetWorldTransformationMatrix(Translation(1, 2, 3));
    Device->SetViewTransformationMatrix(Translation(4, 5, 6));
    Device->SetProjectionTransformationMatrix(Translation(7, 8, 9));
    Boundary.SetTransformMatrix(VXMATRIX_WORLD, Translation(11, 12, 13));
    Boundary.SetTransformMatrix(VXMATRIX_VIEW, Translation(14, 15, 16));
    Boundary.SetTransformMatrix(VXMATRIX_PROJECTION, Translation(17, 18, 19));
    const VxMatrix &world = Device->GetWorldTransformationMatrix();
    const VxMatrix &view = Device->GetViewTransformationMatrix();
    const VxMatrix &projection = Device->GetProjectionTransformationMatrix();
    EXPECT_FLOAT_EQ(11, world[3][0]); EXPECT_FLOAT_EQ(15, view[3][1]); EXPECT_FLOAT_EQ(19, projection[3][2]);
    EXPECT_FLOAT_EQ(1, Device->m_WorldMatrix[3][0]);
    EXPECT_FLOAT_EQ(5, Device->m_ViewMatrix[3][1]);
    EXPECT_FLOAT_EQ(9, Device->m_ProjectionMatrix[3][2]);
    Boundary.SetTransformMatrix(VXMATRIX_VIEW, Translation(24, 25, 26));
    EXPECT_FLOAT_EQ(25, Device->GetViewTransformationMatrix()[3][1]);
    EXPECT_FLOAT_EQ(11, world[3][0]); EXPECT_FLOAT_EQ(19, projection[3][2]);
}

TEST_F(RenderContextQueryCompletionTest, ProjectionQueryReportsTheTemporaryCropWhileTheFullCameraProjectionRemainsIntact) {
    Device->m_ViewportData.ViewX = 10; Device->m_ViewportData.ViewY = 20;
    Device->m_ViewportData.ViewWidth = 200; Device->m_ViewportData.ViewHeight = 100;
    VxMatrix full; full.Perspective(PI / 2, 2, 1, 11);
    Device->SetProjectionTransformationMatrix(full);
    VxRect clip(60, 45, 160, 95); Device->SetClipRect(&clip);
    const VxMatrix &actual = Device->GetProjectionTransformationMatrix();
    EXPECT_NEAR(2, actual[0][0], 1e-5f); EXPECT_NEAR(4, actual[1][1], 1e-5f);
    EXPECT_FLOAT_EQ(full[0][0], Device->m_ProjectionMatrix[0][0]);
    EXPECT_FLOAT_EQ(full[1][1], Device->m_ProjectionMatrix[1][1]);
    EXPECT_EQ(200u, Device->m_ViewportData.ViewWidth); EXPECT_EQ(100u, Device->m_ViewportData.ViewHeight);
    EXPECT_EQ(100u, Boundary.Viewport.ViewWidth); EXPECT_EQ(50u, Boundary.Viewport.ViewHeight);
}

TEST_F(RenderContextQueryCompletionTest, RejectedAndHeadlessMatrixQueriesReturnTheSavedCpuValues) {
    Device->SetWorldTransformationMatrix(Translation(1, 2, 3));
    Device->SetViewTransformationMatrix(Translation(4, 5, 6));
    Device->SetProjectionTransformationMatrix(Translation(7, 8, 9));
    Boundary.MatrixAck = FALSE;
    EXPECT_FLOAT_EQ(1, Device->GetWorldTransformationMatrix()[3][0]);
    EXPECT_FLOAT_EQ(5, Device->GetViewTransformationMatrix()[3][1]);
    EXPECT_FLOAT_EQ(9, Device->GetProjectionTransformationMatrix()[3][2]);
    Device->m_RasterizerContext = nullptr;
    EXPECT_FLOAT_EQ(1, Device->GetWorldTransformationMatrix()[3][0]);
    EXPECT_FLOAT_EQ(5, Device->GetViewTransformationMatrix()[3][1]);
    EXPECT_FLOAT_EQ(9, Device->GetProjectionTransformationMatrix()[3][2]);
}

TEST_F(RenderContextQueryCompletionTest, RejectedRenderStateQueriesDoNotPublishTheFailedOutput) {
    Boundary.States[VXRENDERSTATE_ZENABLE] = 7;
    EXPECT_EQ(7u, Device->GetState(VXRENDERSTATE_ZENABLE));
    EXPECT_EQ(7u, Device->GetRasterizerRenderState(VXRENDERSTATE_ZENABLE));
    Boundary.StateAck = FALSE;
    EXPECT_EQ(0u, Device->GetState(VXRENDERSTATE_ZENABLE));
    EXPECT_EQ(0u, Device->GetRasterizerRenderState(VXRENDERSTATE_ZENABLE));
    Device->m_RasterizerContext = nullptr;
    EXPECT_EQ(0u, Device->GetState(VXRENDERSTATE_ZENABLE));
}
