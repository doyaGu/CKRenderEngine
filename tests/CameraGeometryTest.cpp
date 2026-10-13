#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCKCamera.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CameraGeometryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCKCamera *Camera() { return static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA)); }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    static CK3dEntity *API(RCK3dEntity *e) { return reinterpret_cast<CK3dEntity *>(e); }
    static void Expect(const VxVector &v, float x, float y, float z, float tolerance = 1e-5f) {
        EXPECT_NEAR(x, v.x, tolerance); EXPECT_NEAR(y, v.y, tolerance); EXPECT_NEAR(z, v.z, tolerance);
    }
    static void Axes(RCKCamera &c, const VxVector &direction, const VxVector &up, const VxVector &right) {
        VxVector d, u, r; c.GetOrientation(&d, &u, &r);
        Expect(d, direction.x, direction.y, direction.z);
        Expect(u, up.x, up.y, up.z); Expect(r, right.x, right.y, right.z);
        EXPECT_NEAR(0, DotProduct(d, u), 1e-5f); EXPECT_NEAR(0, DotProduct(d, r), 1e-5f);
        EXPECT_NEAR(0, DotProduct(u, r), 1e-5f);
        EXPECT_NEAR(1, d.Magnitude(), 1e-5f); EXPECT_NEAR(1, u.Magnitude(), 1e-5f);
        EXPECT_NEAR(1, r.Magnitude(), 1e-5f);
    }
};
}

TEST_F(CameraGeometryTest, PositiveQuarterRollRotatesAxesWithoutMovingCameraOrChangingLook) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); c->SetPosition3f(7, 11, -3);
    c->Roll(PI / 2); Axes(*c, VxVector(0, 0, 1), VxVector(1, 0, 0), VxVector(0, -1, 0));
    VxVector position; c->GetPosition(&position); Expect(position, 7, 11, -3);
}
TEST_F(CameraGeometryTest, RollUsesTheExistingLocalLookAxisOfAParentedCamera) {
    RCKCamera *c = Camera(); RCK3dEntity *p = Entity(); ASSERT_NE(nullptr, c); ASSERT_NE(nullptr, p);
    VxMatrix parent = VxMatrix::Identity(); parent[0].Set(0, 1, 0); parent[1].Set(-1, 0, 0); parent[3].Set(20, 30, 40);
    p->SetWorldMatrix(parent); ASSERT_TRUE(c->SetParent(API(p), FALSE));
    VxMatrix local = VxMatrix::Identity(); local[0].Set(0, 0, -1); local[2].Set(1, 0, 0); local[3].Set(3, 4, 5);
    c->SetLocalMatrix(local); c->Roll(PI / 2);
    Axes(*c, VxVector(0, 1, 0), VxVector(0, 0, -1), VxVector(1, 0, 0));
    VxVector position; c->GetPosition(&position); Expect(position, 16, 33, 45);
    Expect(c->GetLocalMatrix()[3], 3, 4, 5);
}
TEST_F(CameraGeometryTest, OppositeRollsRestoreThePoseAndZeroRollKeepsTranslation) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c);
    VxMatrix pose = VxMatrix::Identity(); pose[0].Set(0, 0, -1); pose[2].Set(1, 0, 0); pose[3].Set(8, -2, 6);
    c->SetLocalMatrix(pose); c->Roll(.71f); c->Roll(-.71f); c->Roll(0);
    Axes(*c, VxVector(1, 0, 0), VxVector(0, 1, 0), VxVector(0, 0, -1));
    VxVector position; c->GetPosition(&position); Expect(position, 8, -2, 6);
}
TEST_F(CameraGeometryTest, RollPropagatesTheNewOrientationToChildren) {
    RCKCamera *c = Camera(); RCK3dEntity *child = Entity(); ASSERT_NE(nullptr, c); ASSERT_NE(nullptr, child);
    c->SetPosition3f(10, 20, 30); ASSERT_TRUE(child->SetParent(API(c), FALSE)); child->SetPosition3f(2, 0, 0, API(c));
    c->Roll(PI / 2); VxVector position; child->GetPosition(&position); Expect(position, 10, 18, 30);
}
TEST_F(CameraGeometryTest, NonuniformScaleFollowsNativeLocalMatrixCompositionIncludingShear) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c);
    VxMatrix pose = VxMatrix::Identity(); pose[0].Set(2, 0, 0); pose[1].Set(0, 3, 0);
    pose[2].Set(0, 0, 4); pose[3].Set(7, 11, -3); c->SetLocalMatrix(pose); c->Roll(PI / 4);
    const float halfRootTwo = std::sqrt(.5f);
    const VxMatrix &local = c->GetLocalMatrix();
    Expect(local[0], 2 * halfRootTwo, -3 * halfRootTwo, 0);
    Expect(local[1], 2 * halfRootTwo, 3 * halfRootTwo, 0);
    Expect(local[2], 0, 0, 4); Expect(local[3], 7, 11, -3);
    EXPECT_NEAR(-2.5f, DotProduct(local[0], local[1]), 1e-5f);
    const VxVector x(1, 0, 0), y(0, 1, 0); VxVector point;
    c->Transform(&point, &x); Expect(point, 7 + 2 * halfRootTwo, 11 - 3 * halfRootTwo, -3);
    c->Transform(&point, &y); Expect(point, 7 + 2 * halfRootTwo, 11 + 3 * halfRootTwo, -3);
}
TEST_F(CameraGeometryTest, ResetRollRestoresWorldUpWhenLookingForward) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c); c->SetPosition3f(5, 6, 7); c->Roll(.9f); c->ResetRoll();
    Axes(*c, VxVector(0, 0, 1), VxVector(0, 1, 0), VxVector(1, 0, 0));
    VxVector position; c->GetPosition(&position); Expect(position, 5, 6, 7);
}
TEST_F(CameraGeometryTest, ResetRollPreservesPitchedLookWhileRemovingBank) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c);
    VxMatrix pose = VxMatrix::Identity(); pose[0].Set(1, 0, 0); pose[1].Set(0, .8f, -.6f); pose[2].Set(0, .6f, .8f);
    c->SetWorldMatrix(pose); c->Roll(.65f); c->ResetRoll();
    Axes(*c, VxVector(0, .6f, .8f), VxVector(0, .8f, -.6f), VxVector(1, 0, 0));
}
TEST_F(CameraGeometryTest, ResetRollUsesWorldUpRatherThanTheRotatedParentUp) {
    RCKCamera *c = Camera(); RCK3dEntity *p = Entity(); ASSERT_NE(nullptr, c); ASSERT_NE(nullptr, p);
    VxMatrix parent = VxMatrix::Identity(); parent[0].Set(0, 1, 0); parent[1].Set(-1, 0, 0); parent[3].Set(20, 30, 40);
    p->SetWorldMatrix(parent); ASSERT_TRUE(c->SetParent(API(p), FALSE));
    VxMatrix world = VxMatrix::Identity(); world[0].Set(0, 0, -2); world[1].Set(0, 3, 0); world[2].Set(4, 0, 0); world[3].Set(7, 8, 9);
    c->SetWorldMatrix(world); c->ResetRoll();
    Axes(*c, VxVector(1, 0, 0), VxVector(0, 1, 0), VxVector(0, 0, -1));
    VxVector position, scale; c->GetPosition(&position); Expect(position, 7, 8, 9);
    c->GetScale(&scale, FALSE); Expect(scale, 2, 3, 4);
    EXPECT_EQ(API(p), c->GetParent());
}
TEST_F(CameraGeometryTest, ResetRollLookingStraightUpUsesAStableOrthonormalFallback) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c);
    VxMatrix pose = VxMatrix::Identity(); pose[0].Set(-1, 0, 0); pose[1].Set(0, 0, 1); pose[2].Set(0, 1, 0);
    c->SetWorldMatrix(pose); c->Roll(.4f); c->ResetRoll();
    Axes(*c, VxVector(0, 1, 0), VxVector(0, 0, 1), VxVector(-1, 0, 0));
}
TEST_F(CameraGeometryTest, ResetRollLookingStraightDownUsesAStableOrthonormalFallback) {
    RCKCamera *c = Camera(); ASSERT_NE(nullptr, c);
    VxMatrix pose = VxMatrix::Identity(); pose[0].Set(1, 0, 0); pose[1].Set(0, 0, 1); pose[2].Set(0, -1, 0);
    c->SetWorldMatrix(pose); c->Roll(-.4f); c->ResetRoll();
    Axes(*c, VxVector(0, -1, 0), VxVector(0, 0, 1), VxVector(1, 0, 0));
}
TEST_F(CameraGeometryTest, ResetRollIsIdempotentAndPropagatesTheUprightPoseToChildren) {
    RCKCamera *c = Camera(); RCK3dEntity *child = Entity(); ASSERT_NE(nullptr, c); ASSERT_NE(nullptr, child);
    c->SetPosition3f(10, 20, 30); c->Roll(PI / 2); ASSERT_TRUE(child->SetParent(API(c), FALSE));
    child->SetPosition3f(2, 0, 0, API(c)); c->ResetRoll(); c->ResetRoll();
    Axes(*c, VxVector(0, 0, 1), VxVector(0, 1, 0), VxVector(1, 0, 0));
    VxVector position; child->GetPosition(&position); Expect(position, 12, 20, 30);
}
