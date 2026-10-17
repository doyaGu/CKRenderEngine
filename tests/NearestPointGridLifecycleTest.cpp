#include <gtest/gtest.h>
#include <cmath>

#include "NearestPointGrid.h"
#include "PlaceFitter.h"
#include "CKAll.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKPlace.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class NearestPointGridLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    bool Started = false;

    void StartEngine() {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        Started = true;
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
        if (Context)
            CKCloseContext(Context);
        if (Started)
            CKShutdown();
    }

    RCKMesh *AddCube(RCKPlace *place, float minX, float maxX) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_NE(nullptr, mesh);
        if (!mesh || !mesh->SetVertexCount(8))
            return nullptr;
        int index = 0;
        for (int x = 0; x < 2; ++x) {
            for (int y = -1; y <= 1; y += 2) {
                for (int z = -1; z <= 1; z += 2) {
                    VxVector point(x ? maxX : minX, static_cast<float>(y), static_cast<float>(z));
                    mesh->SetVertexPosition(index++, &point);
                }
            }
        }
        mesh->VertexMove();
        place->SetCurrentMesh(mesh);
        return mesh;
    }

    static void ExpectContains(const VxMatrix &box, const VxVector &point) {
        for (int axis = 0; axis < 3; ++axis) {
            const VxVector &basis = box[axis];
            const float size2 = basis.x * basis.x + basis.y * basis.y + basis.z * basis.z;
            ASSERT_GT(size2, 0.0f);
            const float local = ((point.x - box[3].x) * basis.x +
                                 (point.y - box[3].y) * basis.y +
                                 (point.z - box[3].z) * basis.z) / size2;
            EXPECT_LE(std::fabs(local), 1.0001f);
        }
    }
};

} // namespace

TEST_F(NearestPointGridLifecycleTest, DefaultEmptyGridCanBeUsedWithItsDefaultThreshold) {
    NearestPointGrid grid;
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(0, 0, 0)));
    grid.SetGridDimensions(2, 2, 2);
    grid.AddPoint(VxVector(1, 1, 1), 51);
    EXPECT_EQ(51, grid.FindNearPoint(VxVector(1.0625f, 1, 1)));
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(1.125f, 1, 1)));
}

TEST_F(NearestPointGridLifecycleTest, AddedPointsCopyPositionsAndOpaqueLabelsAcrossCellGrowth) {
    NearestPointGrid grid;
    grid.SetGridDimensions(4, 4, 4);
    grid.SetThreshold(0);
    VxVector source(2.5f, 1.25f, .25f);
    grid.AddPoint(source, 0);
    source.Set(3.5f, 3.5f, 3.5f);
    EXPECT_EQ(0, grid.FindNearPoint(VxVector(2.5f, 1.25f, .25f)));
    EXPECT_EQ(-1, grid.FindNearPoint(source));
    for (int i = 0; i < 64; ++i) {
        source.Set(1.0f + static_cast<float>(i + 1) / 128.0f, 2.25f, 3.25f);
        grid.AddPoint(source, 2000000000 + i);
        source.Set(0, 0, 0);
    }
    for (int i = 0; i < 64; ++i) {
        const VxVector query(1.0f + static_cast<float>(i + 1) / 128.0f, 2.25f, 3.25f);
        EXPECT_EQ(2000000000 + i, grid.FindNearPoint(query));
    }
    EXPECT_EQ(0, grid.FindNearPoint(VxVector(2.5f, 1.25f, .25f)));
}

TEST_F(NearestPointGridLifecycleTest, NonCubicUnitCellsKeepAxisAndBoundaryPointsDistinct) {
    NearestPointGrid grid;
    grid.SetGridDimensions(2, 3, 4);
    grid.SetThreshold(0);
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 4; ++z)
                grid.AddPoint(VxVector(x + .125f, y + .125f, z + .125f), 100 * x + 10 * y + z);
    grid.AddPoint(VxVector(0, 0, 0), 901);
    grid.AddPoint(VxVector(1, 1, 1), 902);
    grid.AddPoint(VxVector(1.875f, 2.875f, 3.875f), 903);
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 4; ++z)
                EXPECT_EQ(100 * x + 10 * y + z,
                          grid.FindNearPoint(VxVector(x + .125f, y + .125f, z + .125f)));
    EXPECT_EQ(901, grid.FindNearPoint(VxVector(0, 0, 0)));
    EXPECT_EQ(902, grid.FindNearPoint(VxVector(1, 1, 1)));
    EXPECT_EQ(903, grid.FindNearPoint(VxVector(1.875f, 2.875f, 3.875f)));
}

TEST_F(NearestPointGridLifecycleTest, SameDimensionsResetAllPointsWithoutResettingThreshold) {
    NearestPointGrid grid;
    grid.SetGridDimensions(3, 3, 3);
    grid.SetThreshold(.25f);
    grid.AddPoint(VxVector(1, 1, 1), 73);
    const VxVector nearby(1.1875f, 1, 1);
    EXPECT_EQ(73, grid.FindNearPoint(nearby));
    grid.SetGridDimensions(3, 3, 3);
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(1, 1, 1)));
    EXPECT_EQ(-1, grid.FindNearPoint(nearby));
    grid.AddPoint(VxVector(1, 1, 1), 74);
    EXPECT_EQ(74, grid.FindNearPoint(nearby));
}

TEST_F(NearestPointGridLifecycleTest, ShrinkEmptyAndGrowReplaceOldCellsBeforeReuse) {
    NearestPointGrid grid;
    grid.SetThreshold(0);
    grid.SetGridDimensions(4, 3, 5);
    grid.AddPoint(VxVector(.125f, .125f, .125f), 81);
    grid.AddPoint(VxVector(3.5f, 2.5f, 4.5f), 82);
    EXPECT_EQ(82, grid.FindNearPoint(VxVector(3.5f, 2.5f, 4.5f)));
    grid.SetGridDimensions(1, 1, 1);
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(.125f, .125f, .125f)));
    grid.AddPoint(VxVector(.5f, .5f, .5f), 83);
    EXPECT_EQ(83, grid.FindNearPoint(VxVector(.5f, .5f, .5f)));
    grid.SetGridDimensions(0, 2, 2);
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(.5f, .5f, .5f)));
    grid.SetGridDimensions(2, 3, 1);
    EXPECT_EQ(-1, grid.FindNearPoint(VxVector(.5f, .5f, .5f)));
    grid.AddPoint(VxVector(1.5f, 2.5f, .5f), 84);
    EXPECT_EQ(84, grid.FindNearPoint(VxVector(1.5f, 2.5f, .5f)));
}

TEST_F(NearestPointGridLifecycleTest, IndependentOwnersAndScopedDestructionDoNotShareCells) {
    VxVector callerPoint(1.25f, 1.5f, 1.75f);
    NearestPointGrid first;
    first.SetGridDimensions(2, 2, 2);
    first.SetThreshold(0);
    first.AddPoint(callerPoint, 101);
    {
        NearestPointGrid second;
        second.SetGridDimensions(2, 2, 2);
        second.SetThreshold(0);
        second.AddPoint(callerPoint, 202);
        EXPECT_EQ(101, first.FindNearPoint(callerPoint));
        EXPECT_EQ(202, second.FindNearPoint(callerPoint));
        second.SetGridDimensions(1, 1, 1);
        EXPECT_EQ(101, first.FindNearPoint(callerPoint));
    }
    EXPECT_EQ(101, first.FindNearPoint(callerPoint));
    EXPECT_FLOAT_EQ(1.25f, callerPoint.x);
    EXPECT_FLOAT_EQ(1.5f, callerPoint.y);
    EXPECT_FLOAT_EQ(1.75f, callerPoint.z);
}

TEST_F(NearestPointGridLifecycleTest, ActualPlaceFitterNormalizesTranslatedWorldBoundsToFindSharedPlane) {
    StartEngine();
    ASSERT_NE(nullptr, Context);
    RCKPlace *first = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE));
    RCKPlace *second = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE));
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    first->SetMoveableFlags(first->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    second->SetMoveableFlags(second->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    ASSERT_NE(nullptr, AddCube(first, -2, 0));
    ASSERT_NE(nullptr, AddCube(second, 0, 2));
    first->SetPosition3f(-10, 20, -30);
    second->SetPosition3f(-10, 20, -30);
    const VxBbox firstBox = first->GetHierarchicalBox(FALSE);
    const VxBbox secondBox = second->GetHierarchicalBox(FALSE);
    ASSERT_NEAR(-12, firstBox.Min.x, .0001f); ASSERT_NEAR(-10, firstBox.Max.x, .0001f);
    ASSERT_NEAR(-10, secondBox.Min.x, .0001f); ASSERT_NEAR(-8, secondBox.Max.x, .0001f);
    ASSERT_NEAR(19, firstBox.Min.y, .0001f); ASSERT_NEAR(21, firstBox.Max.y, .0001f);
    ASSERT_NEAR(-31, secondBox.Min.z, .0001f); ASSERT_NEAR(-29, secondBox.Max.z, .0001f);

    PlaceFitter fitter;
    VxMatrix fitted;
    ASSERT_TRUE(fitter.ComputeBestFitBBox(reinterpret_cast<CK3dEntity *>(first),
                                         reinterpret_cast<CK3dEntity *>(second), fitted));
    EXPECT_NEAR(-10, fitted[3].x, .0001f);
    EXPECT_NEAR(20, fitted[3].y, .0001f);
    EXPECT_NEAR(-30, fitted[3].z, .0001f);
    int thinAxes = 0;
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_TRUE(std::isfinite(fitted[axis].Magnitude()));
        if (std::fabs(fitted[axis].Magnitude() - .1f) < .0001f)
            ++thinAxes;
    }
    EXPECT_EQ(1, thinAxes);
    for (int y = -1; y <= 1; y += 2)
        for (int z = -1; z <= 1; z += 2)
            ExpectContains(fitted, VxVector(-10, 20.0f + y, -30.0f + z));
}
