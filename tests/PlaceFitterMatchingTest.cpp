#include <gtest/gtest.h>
#include <cmath>

#include "CKAll.h"
#include "PlaceFitter.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCK3dEntity.h"
#include "RCKPlace.h"
#include "RCKMesh.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class PlaceFitterMatchingTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        ASSERT_NE(nullptr, Context->GetRenderManager());
    }

    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCKPlace *Place() {
        RCKPlace *place = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE));
        EXPECT_NE(nullptr, place);
        if (!place) return nullptr;
        EXPECT_NE(nullptr, place->m_SceneGraphNode);
        place->SetMoveableFlags(place->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        return place;
    }

    RCKMesh *Mesh(RCKPlace *place, const VxVector *points, int count) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_NE(nullptr, mesh);
        if (!mesh) return nullptr;
        EXPECT_TRUE(mesh->SetVertexCount(count));
        for (int i = 0; i < count; ++i) {
            VxVector point = points[i];
            mesh->SetVertexPosition(i, &point);
        }
        mesh->VertexMove();
        place->SetCurrentMesh(mesh);
        EXPECT_EQ(reinterpret_cast<CKMesh *>(mesh), place->GetCurrentMesh());
        return mesh;
    }

    static CK3dEntity *Entity(RCKPlace *place) {
        return reinterpret_cast<CK3dEntity *>(place);
    }

    static VxMatrix Sentinel() {
        VxMatrix matrix = VxMatrix::Identity();
        matrix[0].x = 3.0f;
        matrix[1].y = 5.0f;
        matrix[2].z = 9.0f;
        matrix[3].Set(7.0f, 11.0f, 13.0f);
        return matrix;
    }

    static void SameMatrix(const VxMatrix &expected, const VxMatrix &actual) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_FLOAT_EQ(expected[row][column], actual[row][column]);
    }

    static void PointBox(const VxMatrix &box, const VxVector &point) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_TRUE(std::isfinite(box[row][column]));
        EXPECT_NEAR(point.x, box[3].x, 0.000001f);
        EXPECT_NEAR(point.y, box[3].y, 0.000001f);
        EXPECT_NEAR(point.z, box[3].z, 0.000001f);
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(0.1f, box[axis].Magnitude(), 0.000002f);
        EXPECT_FLOAT_EQ(1.0f, box[3][3]);
    }

    static void ActualXBounds(RCKPlace *place, float minimum, float maximum) {
        const VxBbox box = place->GetHierarchicalBox(FALSE);
        ASSERT_FLOAT_EQ(minimum, box.Min.x);
        ASSERT_FLOAT_EQ(maximum, box.Max.x);
    }
};

TEST_F(PlaceFitterMatchingTest, MillionUnitBoundsCannotTurnASeparatedWorldVertexIntoACommonPoint) {
    RCKPlace *first = Place();
    RCKPlace *second = Place();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    const VxVector source[] = {VxVector(-1048576.0f, 0, 0), VxVector(1048576.0f, 0, 0), VxVector(0, 0, 0)};
    const VxVector query(0.03125f, 0, 0); // Exact normal floats; distance exceeds the native0.01 world tolerance.
    ASSERT_NE(nullptr, Mesh(first, source, 3));
    ASSERT_NE(nullptr, Mesh(second, &query, 1));
    ActualXBounds(first, -1048576.0f, 1048576.0f);
    ActualXBounds(second, query.x, query.x);
    PlaceFitter fitter;
    VxMatrix box = Sentinel();
    const VxMatrix unchanged = box;
    EXPECT_FALSE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    SameMatrix(unchanged, box);
}

TEST_F(PlaceFitterMatchingTest, MillionUnitBoundsStillAcceptAnActuallyNearWorldPoint) {
    RCKPlace *first = Place();
    RCKPlace *second = Place();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    const VxVector source[] = {VxVector(-1048576.0f, 0, 0), VxVector(1048576.0f, 0, 0), VxVector(0, 0, 0)};
    const VxVector query(0.00390625f, 0, 0);
    ASSERT_NE(nullptr, Mesh(first, source, 3));
    ASSERT_NE(nullptr, Mesh(second, &query, 1));
    ActualXBounds(first, -1048576.0f, 1048576.0f);
    PlaceFitter fitter;
    VxMatrix box = Sentinel();
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    PointBox(box, query);
}

TEST_F(PlaceFitterMatchingTest, CollapsedGridCandidatesKeepAWorldValidMatchRegardlessOfSourceOrder) {
    for (int reverse = 0; reverse < 2; ++reverse) {
        SCOPED_TRACE(reverse);
        RCKPlace *first = Place();
        RCKPlace *second = Place();
        ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
        VxVector source[] = {VxVector(-1048576.0f, 0, 0), VxVector(1048576.0f, 0, 0),
                             VxVector(0.03125f, 0, 0), VxVector(0, 0, 0)};
        if (reverse) { const VxVector swap = source[2]; source[2] = source[3]; source[3] = swap; }
        const VxVector query(0.0390625f, 0, 0);
        ASSERT_NE(nullptr, Mesh(first, source, 4));
        ASSERT_NE(nullptr, Mesh(second, &query, 1));
        ActualXBounds(first, -1048576.0f, 1048576.0f);
        // World distance to.03125 is.0078125; distance to0 is.0390625.
        // A postcheck of just one rounded-grid winner must not hide the valid candidate.
        PlaceFitter fitter;
        VxMatrix box = Sentinel();
        ASSERT_TRUE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
        PointBox(box, query);
    }
}

TEST_F(PlaceFitterMatchingTest, MillionUnitNormalizationCannotEraseOneComponentOfASphericalMiss) {
    RCKPlace *first = Place();
    RCKPlace *second = Place();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    const VxVector source[] = {VxVector(-1048576.0f, 0, 0), VxVector(1048576.0f, 0, 0), VxVector(0, 0, 0)};
    const VxVector query(0.0078125f, 0.0078125f, 0);
    ASSERT_NE(nullptr, Mesh(first, source, 3));
    ASSERT_NE(nullptr, Mesh(second, &query, 1));
    ActualXBounds(first, -1048576.0f, 1048576.0f);
    const double squaredDistance = 2.0 * 0.0078125 * 0.0078125;
    ASSERT_GT(squaredDistance, 0.01 * 0.01);
    PlaceFitter fitter;
    VxMatrix box = Sentinel();
    const VxMatrix unchanged = box;
    EXPECT_FALSE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    SameMatrix(unchanged, box);
}

TEST_F(PlaceFitterMatchingTest, WorldToleranceFollowsTheNativeSmallExtentScaleAndThePointZeroOneCap) {
    const float extents[] = {0.5f, 2.0f, 16.0f, 1048576.0f};
    for (int extent = 0; extent < 4; ++extent) {
        SCOPED_TRACE(extent);
        RCKPlace *first = Place();
        RCKPlace *second = Place();
        ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
        const VxVector source[] = {VxVector(-extents[extent], 0, 0), VxVector(extents[extent], 0, 0), VxVector(0, 0, 0)};
        VxVector query(extent == 0 ? 0.00390625f : 0.0078125f, 0, 0);
        ASSERT_NE(nullptr, Mesh(first, source, 3));
        ASSERT_NE(nullptr, Mesh(second, &query, 1));
        ActualXBounds(first, -extents[extent], extents[extent]);
        PlaceFitter fitter;
        VxMatrix box = Sentinel();
        ASSERT_TRUE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
        PointBox(box, query);
        query.x = extent == 0 ? 0.0078125f : 0.015625f;
        // Publish replacement geometry through SetCurrentMesh so the entity's
        // hierarchical box observes the new public mesh attachment.
        ASSERT_NE(nullptr, Mesh(second, &query, 1));
        ActualXBounds(second, query.x, query.x);
        const VxMatrix unchanged = box;
        EXPECT_FALSE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
        SameMatrix(unchanged, box);
    }
}

TEST_F(PlaceFitterMatchingTest, ASourceVertexIsNotReusedForSeveralNearbyQueries) {
    for (int reverse = 0; reverse < 2; ++reverse) {
        SCOPED_TRACE(reverse);
        RCKPlace *first = Place();
        RCKPlace *second = Place();
        ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
        const VxVector source[] = {VxVector(-16, 0, 0), VxVector(16, 0, 0), VxVector(0, 0, 0)};
        VxVector query[] = {VxVector(0.00390625f, 0, 0), VxVector(-0.00390625f, 0, 0),
                            VxVector(0, 0, 0), VxVector(0.0078125f, 0, 0)};
        if (reverse) { const VxVector swap = query[0]; query[0] = query[1]; query[1] = swap; }
        ASSERT_NE(nullptr, Mesh(first, source, 3));
        ASSERT_NE(nullptr, Mesh(second, query, 4));
        ActualXBounds(first, -16, 16);
        PlaceFitter fitter;
        VxMatrix box = Sentinel();
        ASSERT_TRUE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
        PointBox(box, query[0]);
    }
}

TEST_F(PlaceFitterMatchingTest, OneFitterCanSucceedFailWithoutChangingOutputThenSucceedOnChangedGeometry) {
    RCKPlace *first = Place();
    RCKPlace *second = Place();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    const VxVector source[] = {VxVector(-16, 0, 0), VxVector(16, 0, 0), VxVector(0, 0, 0)};
    VxVector query(0.00390625f, 0, 0);
    ASSERT_NE(nullptr, Mesh(first, source, 3));
    ASSERT_NE(nullptr, Mesh(second, &query, 1));
    PlaceFitter fitter;
    VxMatrix box = Sentinel();
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    PointBox(box, query);
    const VxMatrix unchanged = box;
    query.x = 0.5f;
    ASSERT_NE(nullptr, Mesh(second, &query, 1));
    ActualXBounds(second, 0.5f, 0.5f);
    EXPECT_FALSE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    SameMatrix(unchanged, box);
    query.x = -0.00390625f;
    ASSERT_NE(nullptr, Mesh(second, &query, 1));
    ActualXBounds(second, query.x, query.x);
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    PointBox(box, query);
}

TEST_F(PlaceFitterMatchingTest, SeveralCommonPointsFitTheSecondWorldPositionsWithNativePadding) {
    RCKPlace *first = Place();
    RCKPlace *second = Place();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    const VxVector source[] = {VxVector(-1048576.0f, 0, 0), VxVector(1048576.0f, 0, 0),
                               VxVector(-1, -1, 0), VxVector(-1, 1, 0), VxVector(1, -1, 0), VxVector(1, 1, 0)};
    const VxVector query[] = {VxVector(-1, -1, 0.0078125f), VxVector(-1, 1, 0.0078125f),
                              VxVector(1, -1, 0.0078125f), VxVector(1, 1, 0.0078125f)};
    ASSERT_NE(nullptr, Mesh(first, source, 6));
    ASSERT_NE(nullptr, Mesh(second, query, 4));
    ActualXBounds(first, -1048576.0f, 1048576.0f);
    PlaceFitter fitter;
    VxMatrix box = Sentinel();
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    EXPECT_NEAR(0, box[3].x, 0.00001f);
    EXPECT_NEAR(0, box[3].y, 0.00001f);
    EXPECT_NEAR(0.0078125f, box[3].z, 0.000001f);
    int thin = 0;
    for (int axis = 0; axis < 3; ++axis)
        if (std::fabs(box[axis].Magnitude() - 0.1f) < 0.00001f) ++thin;
    EXPECT_EQ(1, thin);
    for (int point = 0; point < 4; ++point)
        for (int axis = 0; axis < 3; ++axis) {
            const VxVector &direction = box[axis];
            const double sizeSquared = static_cast<double>(direction.x) * direction.x +
                                       static_cast<double>(direction.y) * direction.y +
                                       static_cast<double>(direction.z) * direction.z;
            ASSERT_GT(sizeSquared, 0.0);
            const double local = ((query[point].x - box[3].x) * direction.x +
                                  (query[point].y - box[3].y) * direction.y +
                                  (query[point].z - box[3].z) * direction.z) / sizeSquared;
            EXPECT_LE(std::fabs(local), 1.0001);
        }
}

TEST_F(PlaceFitterMatchingTest, DownstreamFitFailureDoesNotPublishAPartiallyWrittenMatrix) {
    RCKPlace *first = Place();
    RCKPlace *second = Place();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    const VxVector point(1048576.0f, 0, 0);
    ASSERT_NE(nullptr, Mesh(first, &point, 1));
    ASSERT_NE(nullptr, Mesh(second, &point, 1));
    ActualXBounds(first, point.x, point.x);
    ActualXBounds(second, point.x, point.x);
    // The fixed VxMath dependency rejects this fitted center after writing its
    // candidate matrix. PlaceFitter must commit output only on overall success.
    PlaceFitter fitter;
    VxMatrix box = Sentinel();
    const VxMatrix unchanged = box;
    EXPECT_FALSE(fitter.ComputeBestFitBBox(Entity(first), Entity(second), box));
    SameMatrix(unchanged, box);
}
}
