#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKSprite3D.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Sprite3DRayTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *API(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, .0001f); EXPECT_NEAR(y, v.y, .0001f); EXPECT_NEAR(z, v.z, .0001f);
    }
    void Alpha(RCKSprite3D &sprite) {
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr, material);
        RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE)); ASSERT_NE(nullptr, texture);
        ASSERT_TRUE(texture->CreateImage(3, 3, 32)); texture->SetPickThreshold(128);
        for (int y = 0; y < 3; ++y) {
            ASSERT_TRUE(texture->SetPixel(0, y, 0x00ffffff)); ASSERT_TRUE(texture->SetPixel(1, y, 0xffffffff));
            ASSERT_TRUE(texture->SetPixel(2, y, 0xffffffff));
        }
        material->SetTexture0(texture); material->SetAlphaRef(128); material->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
        sprite.SetMaterial(reinterpret_cast<CKMaterial *>(material));
    }
};
}

TEST_F(Sprite3DRayTest, ActualHitReportsLocalPointNormalUVAndInputSpaceDistance) {
    RCKSprite3D sprite(Context); const VxVector from(.5f, -.5f, 6), to(.5f, -.5f, -2); VxIntersectionDesc hit = {};
    ASSERT_EQ(1, sprite.RayIntersection(&from, &to, &hit, API(sprite), CKRAYINTERSECTION_SEGMENT));
    Vec(hit.IntersectionPoint, .5f, -.5f, 0); Vec(hit.IntersectionNormal, 0, 0, -1);
    EXPECT_NEAR(6, hit.Distance, .0001f); EXPECT_EQ(0, hit.FaceIndex); EXPECT_FLOAT_EQ(.75f, hit.TexU); EXPECT_FLOAT_EQ(.75f, hit.TexV);
}
TEST_F(Sprite3DRayTest, QuadEdgesAndRaySourceAtPlaneAreIncluded) {
    RCKSprite3D sprite(Context); const VxVector starts[] = {VxVector(-1, -1, 2), VxVector(1, 1, 2), VxVector(0, 0, 0)};
    const VxVector ends[] = {VxVector(-1, -1, -2), VxVector(1, 1, -2), VxVector(0, 0, -2)};
    for (int i = 0; i < 3; ++i) { SCOPED_TRACE(i); EXPECT_EQ(1, sprite.RayIntersection(&starts[i], &ends[i], nullptr, API(sprite), CKRAYINTERSECTION_SEGMENT)); }
}
TEST_F(Sprite3DRayTest, ParallelAndOutsideQuadSegmentsDoNotIntersect) {
    RCKSprite3D sprite(Context); const VxVector a(0, 0, 2), b(1, 0, 2), c(1.01f, 0, 2), d(1.01f, 0, -2);
    EXPECT_EQ(0, sprite.RayIntersection(&a, &b, nullptr, API(sprite)));
    EXPECT_EQ(0, sprite.RayIntersection(&c, &d, nullptr, API(sprite), CKRAYINTERSECTION_SEGMENT));
}
TEST_F(Sprite3DRayTest, DefaultRayExtendsBeyondSecondPointInTheForwardDirection) {
    RCKSprite3D sprite(Context); const VxVector from(0, 0, 4), to(0, 0, 2); VxIntersectionDesc hit = {};
    ASSERT_EQ(1, sprite.RayIntersection(&from, &to, &hit, API(sprite), CKRAYINTERSECTION_DEFAULT)); EXPECT_FLOAT_EQ(4, hit.Distance);
}
TEST_F(Sprite3DRayTest, SegmentRejectsAPlaneBeyondTheEndingPoint) {
    RCKSprite3D sprite(Context); const VxVector from(0, 0, 4), to(0, 0, 2);
    EXPECT_EQ(0, sprite.RayIntersection(&from, &to, nullptr, API(sprite), CKRAYINTERSECTION_SEGMENT));
}
TEST_F(Sprite3DRayTest, BothRayAndSegmentRejectAPlaneBehindTheSource) {
    RCKSprite3D sprite(Context); const VxVector from(0, 0, 2), to(0, 0, 4); VxIntersectionDesc hit = {};
    EXPECT_EQ(0, sprite.RayIntersection(&from, &to, &hit, API(sprite), CKRAYINTERSECTION_DEFAULT));
    EXPECT_EQ(0, sprite.RayIntersection(&from, &to, nullptr, API(sprite), CKRAYINTERSECTION_SEGMENT));
}
TEST_F(Sprite3DRayTest, SegmentEndingExactlyOnThePlaneIsIncluded) {
    RCKSprite3D sprite(Context); const VxVector from(0, 0, 2), to(0, 0, 0); VxIntersectionDesc hit = {};
    ASSERT_EQ(1, sprite.RayIntersection(&from, &to, &hit, API(sprite), CKRAYINTERSECTION_SEGMENT)); EXPECT_FLOAT_EQ(2, hit.Distance);
}
TEST_F(Sprite3DRayTest, WorldRayUsesRotatedScaledSpriteAndReturnsSpriteLocalDescription) {
    RCKSprite3D sprite(Context); VxMatrix world = VxMatrix::Identity(); world[0].Set(0, 2, 0); world[1].Set(-3, 0, 0);
    world[2].Set(0, 0, 4); world[3].Set(10, 20, 30); sprite.SetWorldMatrix(world);
    const VxVector from(11.5f, 21, 38), to(11.5f, 21, 22); VxIntersectionDesc hit = {};
    ASSERT_EQ(1, sprite.RayIntersection(&from, &to, &hit, nullptr, CKRAYINTERSECTION_SEGMENT));
    Vec(hit.IntersectionPoint, .5f, -.5f, 0); Vec(hit.IntersectionNormal, 0, 0, -1); EXPECT_NEAR(8, hit.Distance, .0001f);
}
TEST_F(Sprite3DRayTest, ReferenceCoordinatesDetermineDistanceWhileHitPointRemainsSpriteLocal) {
    RCKSprite3D sprite(Context); sprite.SetPosition3f(10, 20, 30); RCK3dEntity reference(Context);
    VxMatrix world = VxMatrix::Identity(); world[2].Set(0, 0, 2); world[3].Set(10, 20, 30); reference.SetWorldMatrix(world);
    const VxVector from(.25f, -.5f, 3), to(.25f, -.5f, -3); VxIntersectionDesc hit = {};
    ASSERT_EQ(1, sprite.RayIntersection(&from, &to, &hit, API(reference), CKRAYINTERSECTION_SEGMENT));
    Vec(hit.IntersectionPoint, .25f, -.5f, 0); EXPECT_NEAR(3, hit.Distance, .0001f);
}
TEST_F(Sprite3DRayTest, PivotOffsetAndCustomUVRectangleControlActualHitTextureCoordinates) {
    RCKSprite3D sprite(Context); Vx2DVector size(4, 8), offset(1, -1); sprite.SetSize(size); sprite.SetOffset(offset);
    VxRect uv(.2f, .3f, .8f, .9f); sprite.SetUVMapping(uv); const VxVector from(1, -6, 2), to(1, -6, -2); VxIntersectionDesc hit = {};
    ASSERT_EQ(1, sprite.RayIntersection(&from, &to, &hit, API(sprite))); EXPECT_NEAR(.35f, hit.TexU, .0001f); EXPECT_NEAR(.75f, hit.TexV, .0001f);
}
TEST_F(Sprite3DRayTest, ActualTransparentBitmapPixelRejectsDescribedHit) {
    RCKSprite3D sprite(Context); Alpha(sprite); const VxVector from(-.8f, 0, 2), to(-.8f, 0, -2); VxIntersectionDesc hit = {};
    EXPECT_EQ(0, sprite.RayIntersection(&from, &to, &hit, API(sprite), CKRAYINTERSECTION_SEGMENT));
}
TEST_F(Sprite3DRayTest, IgnoreAlphaAcceptsTheSameActualTransparentBitmapPixel) {
    RCKSprite3D sprite(Context); Alpha(sprite); const VxVector from(-.8f, 0, 2), to(-.8f, 0, -2); VxIntersectionDesc hit = {};
    EXPECT_EQ(1, sprite.RayIntersection(&from, &to, &hit, API(sprite), static_cast<CK_RAYINTERSECTION>(CKRAYINTERSECTION_SEGMENT | CKRAYINTERSECTION_IGNOREALPHA)));
    Vec(hit.IntersectionPoint, -.8f, 0, 0);
}
TEST_F(Sprite3DRayTest, NullDescriptionUsesTheSameActualBitmapAlphaPolicy) {
    RCKSprite3D sprite(Context); Alpha(sprite); const VxVector from(-.8f, 0, 2), to(-.8f, 0, -2);
    EXPECT_EQ(0, sprite.RayIntersection(&from, &to, nullptr, API(sprite), CKRAYINTERSECTION_SEGMENT));
    EXPECT_EQ(1, sprite.RayIntersection(&from, &to, nullptr, API(sprite), static_cast<CK_RAYINTERSECTION>(CKRAYINTERSECTION_SEGMENT | CKRAYINTERSECTION_IGNOREALPHA)));
}
TEST_F(Sprite3DRayTest, OpaqueBitmapPixelIsAcceptedWithAndWithoutDescription) {
    RCKSprite3D sprite(Context); Alpha(sprite); const VxVector from(.5f, 0, 2), to(.5f, 0, -2); VxIntersectionDesc hit = {};
    EXPECT_EQ(1, sprite.RayIntersection(&from, &to, &hit, API(sprite), CKRAYINTERSECTION_SEGMENT));
    EXPECT_EQ(1, sprite.RayIntersection(&from, &to, nullptr, API(sprite), CKRAYINTERSECTION_SEGMENT));
}
