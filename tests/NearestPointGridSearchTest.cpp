#include <gtest/gtest.h>
#include "NearestPointGrid.h"

#include <cmath>

namespace {
struct SearchPoint {
    VxVector position;
    int label;
};

double DistanceSquared(const VxVector &a, const VxVector &b) {
    const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
    const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
    const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
    return dx * dx + dy * dy + dz * dz;
}

// Independent oracle: examine every supplied point, without reproducing cell
// bounds or traversal order. Binary-fraction fixtures make the finite double
// distances exact. Equal minimum distances may return either associated label.
void ExpectNearestByFullScan(NearestPointGrid &grid, const SearchPoint *points, int count,
                             const VxVector &query, float radius) {
    const double radiusSquared = static_cast<double>(radius) * static_cast<double>(radius);
    double minimumSquared = radiusSquared;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        const double distance = DistanceSquared(points[i].position, query);
        if (distance <= radiusSquared && (!found || distance < minimumSquared)) {
            minimumSquared = distance;
            found = true;
        }
    }
    const int actual = grid.FindNearPoint(query);
    if (!found) {
        EXPECT_EQ(-1, actual);
        return;
    }
    int selected = -1;
    for (int i = 0; i < count; ++i)
        if (points[i].label == actual) selected = i;
    ASSERT_GE(selected, 0) << "Missing or unknown point label " << actual;
    EXPECT_DOUBLE_EQ(minimumSquared, DistanceSquared(points[selected].position, query));
}

TEST(NearestPointGridSearchTest, NearestPointInOneCellDoesNotDependOnInsertionOrderOrLabelMagnitude) {
    const SearchPoint points[] = {
        {VxVector(1.625f, 1.25f, 1.25f), 1},
        {VxVector(1.3125f, 1.25f, 1.25f), 0x12345678},
        {VxVector(1.5f, 1.25f, 1.25f), 7},
    };
    for (int reverse = 0; reverse < 2; ++reverse) {
        SCOPED_TRACE(reverse);
        NearestPointGrid grid;
        grid.SetGridDimensions(4, 4, 4);
        grid.SetThreshold(0.5f);
        for (int i = 0; i < 3; ++i) {
            const int point = reverse ? 2 - i : i;
            grid.AddPoint(points[point].position, points[point].label);
        }
        EXPECT_EQ(0x12345678, grid.FindNearPoint(VxVector(1.25f, 1.25f, 1.25f)));
    }
}

TEST(NearestPointGridSearchTest, NonCubicMultiCellSearchAgreesWithIndependentFullScan) {
    SearchPoint points[24];
    int count = 0;
    for (int z = 0; z < 7; z += 2)
        for (int y = 0; y < 3; y += 2)
            for (int x = 0; x < 5; x += 2) {
                points[count].position = VxVector(x + 0.25f, y + 0.5f, z + 0.75f);
                points[count].label = 1000 + 3 * count;
                ++count;
            }
    ASSERT_EQ(24, count);
    const VxVector queries[] = {
        VxVector(-0.25f, 0.5f, 0.75f), VxVector(5.125f, 2.5f, 6.75f),
        VxVector(1.875f, 1.25f, 3.875f), VxVector(2.25f, 2.5f, 4.75f),
        VxVector(0.25f, -0.25f, 2.75f), VxVector(4.25f, 3.25f, 6.75f),
        VxVector(2.25f, 0.5f, -0.25f), VxVector(4.25f, 2.5f, 7.25f),
        VxVector(0.875f, 0.875f, 0.875f), VxVector(3.5f, 1.5f, 5.5f),
        VxVector(-2.0f, -2.0f, -2.0f), VxVector(7.0f, 5.0f, 9.0f),
    };
    const float radii[] = {0.0f, 0.125f, 0.5f, 1.0f, 2.0f, 8.0f};
    for (int reverse = 0; reverse < 2; ++reverse) {
        SCOPED_TRACE(reverse);
        NearestPointGrid grid;
        grid.SetGridDimensions(5, 3, 7);
        for (int i = 0; i < count; ++i) {
            const int point = reverse ? count - 1 - i : i;
            grid.AddPoint(points[point].position, points[point].label);
        }
        for (int radius = 0; radius < 6; ++radius) {
            SCOPED_TRACE(radius);
            grid.SetThreshold(radii[radius]);
            for (int query = 0; query < 12; ++query) {
                SCOPED_TRACE(query);
                ExpectNearestByFullScan(grid, points, count, queries[query], radii[radius]);
            }
        }
    }
}

TEST(NearestPointGridSearchTest, SphericalThresholdRejectsAnAxisAlignedCubeCorner) {
    NearestPointGrid grid;
    grid.SetGridDimensions(3, 3, 3);
    grid.SetThreshold(0.5f);
    grid.AddPoint(VxVector(1.375f, 1.375f, 1.375f), 11);
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(1.0f, 1.0f, 1.0f)));
    grid.AddPoint(VxVector(1.25f, 1.25f, 1.25f), 22);
    EXPECT_EQ(22, grid.FindNearPoint(VxVector(1.0f, 1.0f, 1.0f)));
}

TEST(NearestPointGridSearchTest, AxisRadiusBoundaryIsInclusiveAndTheNextSmallerRadiusRejectsIt) {
    const VxVector query(2.0f, 2.0f, 2.0f);
    for (int axis = 0; axis < 3; ++axis) {
        SCOPED_TRACE(axis);
        for (int sign = -1; sign <= 1; sign += 2) {
            SCOPED_TRACE(sign);
            VxVector point = query;
            if (axis == 0) point.x += sign * 0.5f;
            if (axis == 1) point.y += sign * 0.5f;
            if (axis == 2) point.z += sign * 0.5f;
            NearestPointGrid grid;
            grid.SetGridDimensions(5, 5, 5);
            grid.AddPoint(point, 20 + axis);
            grid.SetThreshold(0.5f);
            EXPECT_EQ(20 + axis, grid.FindNearPoint(query));
            grid.SetThreshold(std::nextafter(0.5f, 0.0f));
            EXPECT_EQ(-1, grid.FindNearPoint(query));
        }
    }
}

TEST(NearestPointGridSearchTest, ExactNonAxialSphereBoundaryUsesTheCombinedSquaredDistance) {
    NearestPointGrid grid;
    grid.SetGridDimensions(5, 5, 5);
    grid.AddPoint(VxVector(2.375f, 2.5f, 2.0f), 31);
    grid.SetThreshold(0.625f); // Exact scaled3-4-5 triangle: .375^2+.5^2=.625^2.
    EXPECT_EQ(31, grid.FindNearPoint(VxVector(2.0f, 2.0f, 2.0f)));
    grid.SetThreshold(std::nextafter(0.625f, 0.0f));
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(2.0f, 2.0f, 2.0f)));
}

TEST(NearestPointGridSearchTest, ChangingThresholdImmediatelyUpdatesSpatialAndSphericalAcceptance) {
    NearestPointGrid grid;
    grid.SetGridDimensions(4, 4, 4);
    grid.AddPoint(VxVector(1.375f, 1.375f, 1.0f), 44);
    const VxVector query(1.0f, 1.0f, 1.0f);
    grid.SetThreshold(0.5f);
    EXPECT_EQ(-1, grid.FindNearPoint(query));
    grid.SetThreshold(0.75f);
    EXPECT_EQ(44, grid.FindNearPoint(query));
    grid.SetThreshold(0.25f);
    EXPECT_EQ(-1, grid.FindNearPoint(query));
    grid.SetThreshold(0.75f);
    EXPECT_EQ(44, grid.FindNearPoint(query));
}

TEST(NearestPointGridSearchTest, ZeroRadiusMatchesOnlyAnExactlyStoredPosition) {
    NearestPointGrid grid;
    grid.SetGridDimensions(3, 3, 3);
    grid.AddPoint(VxVector(1.25f, 1.5f, 1.75f), 55);
    grid.SetThreshold(0.0f);
    EXPECT_EQ(55, grid.FindNearPoint(VxVector(1.25f, 1.5f, 1.75f)));
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(std::nextafter(1.25f, 2.0f), 1.5f, 1.75f)));
}

TEST(NearestPointGridSearchTest, EqualMinimumDistancesAllowEitherAssociatedLabelButNeverAFartherPoint) {
    const SearchPoint points[] = {
        {VxVector(1.5f, 2.0f, 2.0f), 61},
        {VxVector(2.5f, 2.0f, 2.0f), 62},
        {VxVector(2.0f, 2.75f, 2.0f), 63},
    };
    for (int reverse = 0; reverse < 2; ++reverse) {
        SCOPED_TRACE(reverse);
        NearestPointGrid grid;
        grid.SetGridDimensions(5, 5, 5);
        grid.SetThreshold(1.0f);
        for (int i = 0; i < 3; ++i) {
            const int point = reverse ? 2 - i : i;
            grid.AddPoint(points[point].position, points[point].label);
        }
        ExpectNearestByFullScan(grid, points, 3, VxVector(2.0f, 2.0f, 2.0f), 1.0f);
    }
}

TEST(NearestPointGridSearchTest, QueriesOutsideEachGridFaceAreClampedWithoutDroppingNearbyStoredPoints) {
    const int sizes[] = {4, 5, 6};
    for (int axis = 0; axis < 3; ++axis) {
        SCOPED_TRACE(axis);
        for (int high = 0; high < 2; ++high) {
            SCOPED_TRACE(high);
            VxVector point(1.5f, 1.5f, 1.5f);
            VxVector nearQuery = point;
            VxVector farQuery = point;
            const float coordinate = high ? sizes[axis] - 0.125f : 0.125f;
            const float nearCoordinate = high ? sizes[axis] + 0.125f : -0.125f;
            const float farCoordinate = high ? sizes[axis] + 2.0f : -2.0f;
            if (axis == 0) { point.x = coordinate; nearQuery.x = nearCoordinate; farQuery.x = farCoordinate; }
            if (axis == 1) { point.y = coordinate; nearQuery.y = nearCoordinate; farQuery.y = farCoordinate; }
            if (axis == 2) { point.z = coordinate; nearQuery.z = nearCoordinate; farQuery.z = farCoordinate; }
            NearestPointGrid grid;
            grid.SetGridDimensions(sizes[0], sizes[1], sizes[2]);
            grid.AddPoint(point, 70 + axis);
            grid.SetThreshold(0.5f);
            EXPECT_EQ(70 + axis, grid.FindNearPoint(nearQuery));
            EXPECT_EQ(-1, grid.FindNearPoint(farQuery));
        }
    }
}

TEST(NearestPointGridSearchTest, SmallNormalCoordinatesKeepNearestAndSphereSemanticsWhenFloatSquaresUnderflow) {
    const float radius = std::ldexp(1.0f, -80);
    const float closer = radius * 0.25f;
    const float farther = radius * 0.75f;
    ASSERT_TRUE(std::isnormal(radius));
    ASSERT_TRUE(std::isnormal(closer));
    ASSERT_TRUE(std::isnormal(farther));
    const SearchPoint points[] = {
        {VxVector(closer, 0.0f, 0.0f), 11},
        {VxVector(farther, 0.0f, 0.0f), 22},
    };
    NearestPointGrid nearest;
    nearest.SetGridDimensions(1, 1, 1);
    nearest.SetThreshold(radius);
    nearest.AddPoint(points[0].position, points[0].label);
    nearest.AddPoint(points[1].position, points[1].label);
    ExpectNearestByFullScan(nearest, points, 2, VxVector(0.0f, 0.0f, 0.0f), radius);

    const SearchPoint corner = {VxVector(farther, farther, 0.0f), 33};
    NearestPointGrid sphere;
    sphere.SetGridDimensions(1, 1, 1);
    sphere.SetThreshold(radius);
    sphere.AddPoint(corner.position, corner.label);
    ExpectNearestByFullScan(sphere, &corner, 1, VxVector(0.0f, 0.0f, 0.0f), radius);
}
}
