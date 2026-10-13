#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCKCamera.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CameraProjectionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCKCamera *Camera() { return static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA)); }
    static VxVector4 Clip(const VxMatrix &m, float x, float y, float z) {
        VxVector4 result; const VxVector point(x, y, z); Vx3DMultiplyMatrixVector4(&result, m, &point); return result;
    }
    static void NDC(const VxMatrix &m, float x, float y, float z, float nx, float ny, float nz) {
        const VxVector4 clip = Clip(m, x, y, z); ASSERT_NE(0, clip.w);
        EXPECT_NEAR(nx, clip.x / clip.w, 1e-5f); EXPECT_NEAR(ny, clip.y / clip.w, 1e-5f);
        EXPECT_NEAR(nz, clip.z / clip.w, 1e-5f);
    }
    static void Perspective(RCKCamera &c) {
        c.SetProjectionType(CK_PERSPECTIVEPROJECTION); c.SetFov(PI / 2);
        c.SetAspectRatio(2, 1); c.SetFrontPlane(2); c.SetBackPlane(10);
    }
    static void Orthographic(RCKCamera &c) {
        c.SetProjectionType(CK_ORTHOGRAPHICPROJECTION); c.SetOrthographicZoom(.25f);
        c.SetAspectRatio(2, 1); c.SetFrontPlane(2); c.SetBackPlane(10);
    }
};
}

TEST_F(CameraProjectionTest, PerspectiveMapsNearFarAndInteriorDepthToZeroOneClipRange) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Perspective(*c); VxMatrix m; c->ComputeProjectionMatrix(m);
    NDC(m, 0, 0, 2, 0, 0, 0); NDC(m, 0, 0, 10, 0, 0, 1); NDC(m, 0, 0, 4, 0, 0, .625f);
    EXPECT_LT(Clip(m, 0, 0, 1).z, 0); EXPECT_GT(Clip(m, 0, 0, 11).z, Clip(m, 0, 0, 11).w);
}
TEST_F(CameraProjectionTest, PerspectiveFovIsHorizontalAndAspectSetsVerticalFrustumExtent) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Perspective(*c); VxMatrix m; c->ComputeProjectionMatrix(m);
    NDC(m, 4, 0, 4, 1, 0, .625f); NDC(m, -4, 0, 4, -1, 0, .625f);
    NDC(m, 0, 2, 4, 0, 1, .625f); NDC(m, 0, -2, 4, 0, -1, .625f);
}
TEST_F(CameraProjectionTest, PerspectiveSixtyDegreeConeProjectsAnalyticEdgeRays) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Perspective(*c); c->SetFov(PI / 3); c->SetAspectRatio(3, 2);
    VxMatrix m; c->ComputeProjectionMatrix(m); const float horizontal = 4 / std::sqrt(3.0f);
    NDC(m, horizontal, 0, 4, 1, 0, .625f); NDC(m, 0, horizontal * 2 / 3, 4, 0, 1, .625f);
}
TEST_F(CameraProjectionTest, PerspectiveForeshortensAndKeepsHomogeneousWEqualToViewDepth) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Perspective(*c); VxMatrix m; c->ComputeProjectionMatrix(m);
    const VxVector4 nearPoint = Clip(m, 1, .5f, 2), farPoint = Clip(m, 1, .5f, 4), behind = Clip(m, 0, 0, -2);
    EXPECT_FLOAT_EQ(2, nearPoint.w); EXPECT_FLOAT_EQ(4, farPoint.w); EXPECT_FLOAT_EQ(-2, behind.w);
    EXPECT_NEAR(nearPoint.x / nearPoint.w / 2, farPoint.x / farPoint.w, 1e-5f);
    EXPECT_NEAR(nearPoint.y / nearPoint.w / 2, farPoint.y / farPoint.w, 1e-5f);
}
TEST_F(CameraProjectionTest, OrthographicMapsNearFarAndMidpointWithLinearDepth) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Orthographic(*c); VxMatrix m; c->ComputeProjectionMatrix(m);
    NDC(m, 0, 0, 2, 0, 0, 0); NDC(m, 0, 0, 10, 0, 0, 1); NDC(m, 0, 0, 6, 0, 0, .5f);
    EXPECT_FLOAT_EQ(1, Clip(m, 2, 3, 4).w);
}
TEST_F(CameraProjectionTest, OrthographicZoomAndAspectDetermineIndependentScreenEdges) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Orthographic(*c); VxMatrix m; c->ComputeProjectionMatrix(m);
    NDC(m, 4, 0, 6, 1, 0, .5f); NDC(m, -4, 0, 6, -1, 0, .5f);
    NDC(m, 0, 2, 6, 0, 1, .5f); NDC(m, 0, -2, 6, 0, -1, .5f);
}
TEST_F(CameraProjectionTest, OrthographicApparentSizeIgnoresDepthAndIncreasesWithZoom) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Orthographic(*c); VxMatrix m; c->ComputeProjectionMatrix(m);
    const VxVector4 a = Clip(m, 2, 1, 3), b = Clip(m, 2, 1, 9);
    EXPECT_FLOAT_EQ(a.x, b.x); EXPECT_FLOAT_EQ(a.y, b.y); c->SetOrthographicZoom(.5f); c->ComputeProjectionMatrix(m);
    const VxVector4 zoomed = Clip(m, 2, 1, 3); EXPECT_FLOAT_EQ(a.x * 2, zoomed.x); EXPECT_FLOAT_EQ(a.y * 2, zoomed.y);
}
TEST_F(CameraProjectionTest, ProjectionTypeUsesTheLowBitAndReplacesPreviousMatrixContents) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Perspective(*c); c->SetProjectionType(3); VxMatrix m;
    for (int r = 0; r < 4; ++r) for (int col = 0; col < 4; ++col) m[r][col] = 77;
    c->ComputeProjectionMatrix(m); NDC(m, 0, 0, 2, 0, 0, 0); NDC(m, 4, 0, 4, 1, 0, .625f);
    c->SetProjectionType(2); c->SetOrthographicZoom(.25f); c->ComputeProjectionMatrix(m);
    NDC(m, 0, 0, 2, 0, 0, 0); NDC(m, 4, 2, 6, 1, 1, .5f);
}
TEST_F(CameraProjectionTest, InvalidViewportDimensionsKeepTheExistingSquareAspectFallback) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Perspective(*c); VxMatrix m;
    const int dimensions[4][2] = {{0, 3}, {4, 0}, {-4, 3}, {4, -3}};
    for (int i = 0; i < 4; ++i) {
        c->SetAspectRatio(dimensions[i][0], dimensions[i][1]); c->ComputeProjectionMatrix(m);
        NDC(m, 4, 4, 4, 1, 1, .625f);
        int width, height; c->GetAspectRatio(width, height);
        EXPECT_EQ(dimensions[i][0], width); EXPECT_EQ(dimensions[i][1], height);
    }
}
TEST_F(CameraProjectionTest, ProjectionMatrixIsIndependentOfWorldPoseAndCallerDoesNotChangeCameraSettings) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); Perspective(*c); VxMatrix before, after; c->ComputeProjectionMatrix(before);
    c->SetPosition3f(30, -17, 9); c->Roll(.7f); c->ComputeProjectionMatrix(after);
    for (int r = 0; r < 4; ++r) for (int col = 0; col < 4; ++col) EXPECT_FLOAT_EQ(before[r][col], after[r][col]);
    EXPECT_FLOAT_EQ(PI / 2, c->GetFov()); EXPECT_FLOAT_EQ(2, c->GetFrontPlane()); EXPECT_FLOAT_EQ(10, c->GetBackPlane());
    EXPECT_EQ(CK_PERSPECTIVEPROJECTION, c->GetProjectionType());
}
