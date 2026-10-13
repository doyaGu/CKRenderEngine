#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKSprite3D.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "CKSceneGraph.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Sprite3DGeometryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Box(const VxBbox &box, float x0, float y0, float z0, float x1, float y1, float z1) {
        EXPECT_NEAR(x0, box.Min.x, .0001f); EXPECT_NEAR(y0, box.Min.y, .0001f); EXPECT_NEAR(z0, box.Min.z, .0001f);
        EXPECT_NEAR(x1, box.Max.x, .0001f); EXPECT_NEAR(y1, box.Max.y, .0001f); EXPECT_NEAR(z1, box.Max.z, .0001f);
    }
    static void SameBox(const VxBbox &expected, const VxBbox &actual) {
        Box(actual, expected.Min.x, expected.Min.y, expected.Min.z, expected.Max.x, expected.Max.y, expected.Max.z);
    }
};
}

TEST_F(Sprite3DGeometryTest, DefaultSizeAndOffsetDescribeTheActualCenteredQuad) {
    RCKSprite3D sprite(Context); Vx2DVector size, offset; sprite.GetSize(size); sprite.GetOffset(offset);
    EXPECT_FLOAT_EQ(2, size.x); EXPECT_FLOAT_EQ(2, size.y); EXPECT_FLOAT_EQ(0, offset.x); EXPECT_FLOAT_EQ(0, offset.y);
    Box(sprite.GetBoundingBox(TRUE), -1, -1, 0, 1, 1, 0);
}
TEST_F(Sprite3DGeometryTest, ResizeUsesFullDimensionsAndTheExistingPivotOffset) {
    RCKSprite3D sprite(Context); Vx2DVector offset(.5f, -1.0f), size(8, 6); sprite.SetOffset(offset); sprite.SetSize(size);
    Box(sprite.GetBoundingBox(TRUE), -2, -6, 0, 6, 0, 0); Vx2DVector actual; sprite.GetSize(actual);
    EXPECT_FLOAT_EQ(8, actual.x); EXPECT_FLOAT_EQ(6, actual.y); sprite.GetOffset(actual);
    EXPECT_FLOAT_EQ(.5f, actual.x); EXPECT_FLOAT_EQ(-1, actual.y);
}
TEST_F(Sprite3DGeometryTest, OffsetMovesPivotWithoutChangingDimensions) {
    RCKSprite3D sprite(Context); Vx2DVector size(4, 10), offset(1, 1); sprite.SetSize(size); sprite.SetOffset(offset);
    Box(sprite.GetBoundingBox(TRUE), 0, 0, 0, 4, 10, 0); sprite.GetSize(size);
    EXPECT_FLOAT_EQ(4, size.x); EXPECT_FLOAT_EQ(10, size.y);
    offset.Set(-1, -1); sprite.SetOffset(offset); Box(sprite.GetBoundingBox(TRUE), -4, -10, 0, 0, 0, 0);
}
TEST_F(Sprite3DGeometryTest, ZeroDimensionsRemainZeroAndOffsetValuesAreNotClamped) {
    RCKSprite3D sprite(Context); Vx2DVector size(0, 4), offset(4, -3); sprite.SetSize(size); sprite.SetOffset(offset);
    Box(sprite.GetBoundingBox(TRUE), 0, -8, 0, 0, -4, 0); sprite.GetOffset(offset);
    EXPECT_FLOAT_EQ(4, offset.x); EXPECT_FLOAT_EQ(-3, offset.y); sprite.GetSize(size);
    EXPECT_FLOAT_EQ(0, size.x); EXPECT_FLOAT_EQ(4, size.y);
}
TEST_F(Sprite3DGeometryTest, UVMappingStoresExactReversedAndOutOfRangeRectangle) {
    RCKSprite3D sprite(Context); VxRect rect(2, -3, -.25f, 4), result; sprite.SetUVMapping(rect); sprite.GetUVMapping(result);
    EXPECT_FLOAT_EQ(2, result.left); EXPECT_FLOAT_EQ(-3, result.top); EXPECT_FLOAT_EQ(-.25f, result.right); EXPECT_FLOAT_EQ(4, result.bottom);
    rect = VxRect(7, 8, 9, 10); sprite.GetUVMapping(result); EXPECT_FLOAT_EQ(2, result.left); EXPECT_FLOAT_EQ(4, result.bottom);
}
TEST_F(Sprite3DGeometryTest, WorldBoxTransformsActualQuadAfterLocalOnlyUpdate) {
    RCKSprite3D sprite(Context); Vx2DVector size(4, 6); sprite.SetSize(size);
    VxMatrix world = VxMatrix::Identity(); world[0].Set(0, 2, 0); world[1].Set(-3, 0, 0); world[3].Set(10, 20, 30);
    sprite.SetWorldMatrix(world); sprite.UpdateBox(FALSE); Box(sprite.GetBoundingBox(TRUE), -2, -3, 0, 2, 3, 0);
    sprite.UpdateBox(TRUE); Box(sprite.GetBoundingBox(FALSE), 1, 16, 30, 19, 24, 30);
    EXPECT_TRUE(sprite.GetMoveableFlags() & VX_MOVEABLE_UPTODATE); EXPECT_TRUE(sprite.GetMoveableFlags() & VX_MOVEABLE_BOXVALID);
}
TEST_F(Sprite3DGeometryTest, ResizingAfterCachedWorldBoxUpdatesWorldAndHierarchyBounds) {
    RCKSprite3D sprite(Context); sprite.SetPosition3f(10, 20, 30); ASSERT_NE(nullptr, sprite.m_SceneGraphNode);
    Box(sprite.GetBoundingBox(FALSE), 9, 19, 30, 11, 21, 30); sprite.GetHierarchicalBox(FALSE);
    Vx2DVector size(8, 6); sprite.SetSize(size); Box(sprite.GetBoundingBox(FALSE), 6, 17, 30, 14, 23, 30);
    Box(sprite.GetHierarchicalBox(FALSE), 6, 17, 30, 14, 23, 30);
}
TEST_F(Sprite3DGeometryTest, ChangingOffsetAfterCachedWorldBoxUpdatesWorldAndHierarchyBounds) {
    RCKSprite3D sprite(Context); Vx2DVector size(4, 6); sprite.SetSize(size); sprite.SetPosition3f(10, 20, 30);
    Box(sprite.GetBoundingBox(FALSE), 8, 17, 30, 12, 23, 30); sprite.GetHierarchicalBox(FALSE);
    Vx2DVector offset(1, -1); sprite.SetOffset(offset); Box(sprite.GetBoundingBox(FALSE), 10, 14, 30, 14, 20, 30);
    Box(sprite.GetHierarchicalBox(FALSE), 10, 14, 30, 14, 20, 30);
}
TEST_F(Sprite3DGeometryTest, CustomLocalBoundingBoxIsAnAcceptedNoOpForSpriteShape) {
    RCKSprite3D sprite(Context); sprite.UpdateBox(TRUE); const VxBbox local = sprite.GetBoundingBox(TRUE), world = sprite.GetBoundingBox(FALSE);
    const CKDWORD flags = sprite.GetMoveableFlags(); VxBbox custom; custom.Min.Set(2, 3, 4); custom.Max.Set(8, 13, 24);
    ASSERT_TRUE(sprite.SetBoundingBox(&custom, TRUE)); SameBox(local, sprite.GetBoundingBox(TRUE)); SameBox(world, sprite.GetBoundingBox(FALSE));
    EXPECT_EQ(flags, sprite.GetMoveableFlags()); Vx2DVector offset; sprite.GetOffset(offset); EXPECT_FLOAT_EQ(0, offset.x); EXPECT_FLOAT_EQ(0, offset.y);
}
TEST_F(Sprite3DGeometryTest, CustomWorldBoundingBoxAndNullResetAreAcceptedNoOps) {
    RCKSprite3D sprite(Context); sprite.SetPosition3f(10, 20, 30); sprite.UpdateBox(TRUE);
    const VxBbox local = sprite.GetBoundingBox(TRUE), world = sprite.GetBoundingBox(FALSE); const CKDWORD flags = sprite.GetMoveableFlags();
    VxBbox custom; custom.Min.Set(-20, -30, -40); custom.Max.Set(60, 70, 80);
    ASSERT_TRUE(sprite.SetBoundingBox(&custom, FALSE)); ASSERT_TRUE(sprite.SetBoundingBox(nullptr, FALSE));
    SameBox(local, sprite.GetBoundingBox(TRUE)); SameBox(world, sprite.GetBoundingBox(FALSE)); EXPECT_EQ(flags, sprite.GetMoveableFlags());
}
