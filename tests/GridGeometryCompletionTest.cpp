#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKGrid.h"
#include "RCKLayer.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class GridGeometryCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    static CKGrid *API(RCKGrid &grid) {
        return reinterpret_cast<CKGrid *>(&grid);
    }
    static void Vec(const VxVector &actual, float x, float y, float z) {
        EXPECT_EQ(x, actual.x);
        EXPECT_EQ(y, actual.y);
        EXPECT_EQ(z, actual.z);
    }
};

TEST_F(GridGeometryCompletionTest, LargeCellCountsKeepTheExactIntegerDivisorInWorldQueries) {
    for (int longAxis = 0; longAxis < 2; ++longAxis) {
        SCOPED_TRACE(longAxis);
        RCKGrid grid(Context);
        CKGrid *api = API(grid);
        const int count = (1 << 24) + 1;
        const float extent = std::ldexp(1.0f, 24);
        api->SetDimensions(longAxis ? 1 : count, longAxis ? count : 1,
            longAxis ? 1.0f : extent, longAxis ? extent : 1.0f);
        // 2^24/(2^24+1) rounds to the float immediately below1.0.
        // Rounding the integer divisor to float first instead produces1.0.
        const float belowOne = std::nextafter(1.0f, 0.0f);
        const float belowHalf = std::nextafter(.5f, 0.0f);
        VxVector scale;
        api->GetScale(&scale, TRUE);
        EXPECT_EQ(longAxis ? 1.0f : belowOne, scale.x);
        EXPECT_EQ(longAxis ? belowOne : 1.0f, scale.z);
        EXPECT_EQ(10.0f, scale.y);
        VxVector center;
        api->Get3dPosFrom2dCoords(&center, 0, 0);
        Vec(center, longAxis ? .5f : belowHalf, 0, longAxis ? belowHalf : .5f);
        // This world point is exactly the second cell boundary at the
        // representable cell scale. The former unit scale picked cell1.
        VxVector boundary(longAxis ? .5f : 2.0f * belowOne, 5,
            longAxis ? 2.0f * belowOne : .5f);
        int x = -1, z = -1;
        EXPECT_FLOAT_EQ(.5f, api->Get2dCoordsFrom3dPos(&boundary, &x, &z));
        EXPECT_EQ(longAxis ? 0 : 2, x);
        EXPECT_EQ(longAxis ? 2 : 0, z);
        EXPECT_EQ(longAxis ? 1 : count, api->GetWidth());
        EXPECT_EQ(longAxis ? count : 1, api->GetLength());
        EXPECT_EQ(0, api->GetLayerCount());
    }
}

TEST_F(GridGeometryCompletionTest, LargeOrdinalCentersRoundOnlyAfterAddingTheHalfCell) {
    for (int longAxis = 0; longAxis < 2; ++longAxis) {
        SCOPED_TRACE(longAxis);
        RCKGrid grid(Context);
        CKGrid *api = API(grid);
        const int count = (1 << 24) + 8;
        api->SetDimensions(longAxis ? 1 : count, longAxis ? count : 1,
            longAxis ? 1.0f : static_cast<float>(count), longAxis ? static_cast<float>(count) : 1.0f);
        const int cells[] = {(1 << 24) + 1, (1 << 24) + 5};
        const float centers[] = {16777218.0f, 16777222.0f};
        for (int i = 0; i < 2; ++i) {
            VxVector center;
            api->Get3dPosFrom2dCoords(&center, longAxis ? 0 : cells[i], longAxis ? cells[i] : 0);
            // Exact centers are16777217.5 and16777221.5; these are their
            // nearest representable floats, rather than the earlier cells.
            Vec(center, longAxis ? .5f : centers[i], 0, longAxis ? centers[i] : .5f);
        }
        EXPECT_EQ(0, api->GetLayerCount());
    }
}

TEST_F(GridGeometryCompletionTest, OrdinaryRotatedCoordinatesStillSelectTheActualOwnedLayerCellAndBounds) {
    RCKGrid grid(Context);
    CKGrid *api = API(grid);
    api->SetDimensions(4, 3, 8, 12);
    api->SetHeightValidity(6);
    VxMatrix pose = VxMatrix::Identity();
    pose[0].Set(0, 0, -2);
    pose[1].Set(0, 6, 0);
    pose[2].Set(4, 0, 0);
    pose[3].Set(10, 20, 30);
    api->SetWorldMatrix(pose);
    RCKLayer layer(Context, "SpatialCells", grid.GetID());
    ASSERT_EQ(grid.GetID(), layer.GetOwner());
    ASSERT_NE(nullptr, layer.GetSquareArray());
    CKDWORD centerValue = 0x12345678, negativeValue = 0x9abcdef0;
    ASSERT_TRUE(layer.SetValue2(1, 2, &centerValue));
    ASSERT_TRUE(layer.SetValue2(0, 1, &negativeValue));
    VxVector center;
    api->Get3dPosFrom2dCoords(&center, 1, 2);
    Vec(center, 20, 20, 27);
    VxVector point(20, 23, 27);
    int x = -1, z = -1;
    EXPECT_FLOAT_EQ(.5f, api->Get2dCoordsFrom3dPos(&point, &x, &z));
    ASSERT_EQ(1, x); ASSERT_EQ(2, z);
    CKDWORD value = 0;
    ASSERT_TRUE(layer.GetValue2(x, z, &value));
    EXPECT_EQ(centerValue, value);
    // Negative local x is truncated toward zero by the native query contract.
    point.Set(16, 23, 31.5f);
    EXPECT_FLOAT_EQ(.5f, api->Get2dCoordsFrom3dPos(&point, &x, &z));
    ASSERT_EQ(0, x); ASSERT_EQ(1, z);
    ASSERT_TRUE(layer.GetValue2(x, z, &value));
    EXPECT_EQ(negativeValue, value);
    api->SetOrientationMode(CKGRID_YZ);
    api->SetGridPriority(-17);
    EXPECT_EQ(CKGRID_YZ, api->GetOrientationMode());
    EXPECT_EQ(-17, api->GetGridPriority());
    api->Get3dPosFrom2dCoords(&center, 1, 2);
    Vec(center, 20, 20, 27);
    const VxBbox &local = api->GetBoundingBox(TRUE);
    Vec(local.Min, 0, 0, 0); Vec(local.Max, 4, 1, 3);
    const VxBbox &world = api->GetBoundingBox(FALSE);
    Vec(world.Min, 10, 20, 22); Vec(world.Max, 22, 26, 30);
}

} // namespace
