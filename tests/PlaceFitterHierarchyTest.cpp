#include <gtest/gtest.h>
#include <cmath>

#include "CKAll.h"
#include "PlaceFitter.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKPatchMesh.h"
#include "RCKPlace.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class PlaceFitterHierarchyTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        ASSERT_NE(nullptr, Context->GetRenderManager());
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    static CK3dEntity *Api(RCK3dEntity *entity) {
        return reinterpret_cast<CK3dEntity *>(entity);
    }

    RCK3dEntity *Entity(RCK3dEntity *parent = nullptr, bool place = false) {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(
            place ? CKCID_PLACE : CKCID_3DENTITY));
        EXPECT_NE(nullptr, entity);
        if (!entity)
            return nullptr;
        entity->SetMoveableFlags(entity->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        if (parent)
            EXPECT_TRUE(entity->SetParent(Api(parent), FALSE));
        return entity;
    }

    RCKMesh *AttachPoints(RCK3dEntity *entity, const VxVector *points, int count, RCKMesh *mesh = nullptr) {
        if (!mesh)
            mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_NE(nullptr, mesh);
        if (!mesh || !mesh->SetVertexCount(count))
            return nullptr;
        for (int i = 0; i < count; ++i) {
            VxVector point = points[i];
            mesh->SetVertexPosition(i, &point);
        }
        mesh->VertexMove();
        entity->SetCurrentMesh(mesh);
        entity->UpdateBox(TRUE);
        return mesh;
    }

    static void ExpectContains(const VxMatrix &box, const VxVector &point) {
        for (int axis = 0; axis < 3; ++axis) {
            const VxVector &basis = box[axis];
            const float size2 = basis.x * basis.x + basis.y * basis.y + basis.z * basis.z;
            ASSERT_GT(size2, 0.0f);
            const float coordinate = ((point.x - box[3].x) * basis.x +
                                      (point.y - box[3].y) * basis.y +
                                      (point.z - box[3].z) * basis.z) / size2;
            EXPECT_LE(std::fabs(coordinate), 1.0001f);
        }
    }

    static void ExpectCenter(const VxMatrix &box, const VxVector &point) {
        EXPECT_NEAR(point.x, box[3].x, .0001f);
        EXPECT_NEAR(point.y, box[3].y, .0001f);
        EXPECT_NEAR(point.z, box[3].z, .0001f);
    }

    static void Quad(RCKPatchMesh *mesh) {
        const float xy[16][2] = {{0,0},{2,0},{2,3},{0,3},{2.0f/3,0},{4.0f/3,0},
            {2,1},{2,2},{4.0f/3,3},{2.0f/3,3},{0,2},{0,1},
            {2.0f/3,1},{4.0f/3,1},{4.0f/3,2},{2.0f/3,2}};
        mesh->SetVertVecCount(4, 12);
        for (int i = 0; i < 16; ++i) {
            VxVector point(xy[i][0], xy[i][1], 1);
            if (i < 4) mesh->SetVert(i, &point);
            else mesh->SetVec(i - 4, &point);
        }
        mesh->SetPatchCount(1); mesh->SetEdgeCount(4);
        CKPatch patch; patch.type = CK_PATCH_QUAD; patch.Material = 0;
        for (int i = 0; i < 4; ++i) {
            patch.v[i] = static_cast<short>(i);
            patch.edge[i] = static_cast<short>(i);
            patch.interior[i] = static_cast<short>(8 + i);
        }
        for (int i = 0; i < 8; ++i) patch.vec[i] = static_cast<short>(i);
        mesh->SetPatch(0, &patch);
        for (int i = 0; i < 4; ++i) {
            CKPatchEdge edge; edge.v1 = static_cast<short>(i); edge.v2 = static_cast<short>((i + 1) % 4);
            edge.vec12 = static_cast<short>(2 * i); edge.vec21 = static_cast<short>(2 * i + 1);
            edge.patch1 = 0; edge.patch2 = -1; mesh->SetEdge(i, &edge);
        }
        mesh->SetIterationCount(1);
        mesh->BuildRenderMesh();
    }
};

} // namespace

TEST_F(PlaceFitterHierarchyTest, CollectorIncludesRootDescendantsAndSiblingsButNotAnOutsideSubtree) {
    RCK3dEntity *first = Entity(nullptr, true), *second = Entity(nullptr, true);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    RCK3dEntity *child = Entity(first), *grandchild = Entity(child), *sibling = Entity(first);
    RCK3dEntity *outside = Entity();
    ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, grandchild);
    ASSERT_NE(nullptr, sibling); ASSERT_NE(nullptr, outside);
    const VxVector points[5] = {VxVector(-4,-3,-2), VxVector(4,-3,-2), VxVector(-4,3,-2),
                                 VxVector(-4,-3,2), VxVector(1000,1000,1000)};
    ASSERT_NE(nullptr, AttachPoints(first, &points[0], 1));
    ASSERT_NE(nullptr, AttachPoints(child, &points[1], 1));
    ASSERT_NE(nullptr, AttachPoints(grandchild, &points[2], 1));
    ASSERT_NE(nullptr, AttachPoints(sibling, &points[3], 1));
    ASSERT_NE(nullptr, AttachPoints(outside, &points[4], 1));
    ASSERT_NE(nullptr, AttachPoints(second, points, 5));
    PlaceFitter fitter; VxMatrix fitted;
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    for (int i = 0; i < 4; ++i) ExpectContains(fitted, points[i]);
    for (int axis = 0; axis < 3; ++axis) EXPECT_LT(fitted[axis].Magnitude(), 20.0f);
}

TEST_F(PlaceFitterHierarchyTest, NestedLocalRotationAndScaleAreAppliedBeforeMatchingWorldPoints) {
    RCK3dEntity *first = Entity(nullptr, true), *second = Entity(nullptr, true);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    RCK3dEntity *branch = Entity(first), *leaf = Entity(branch);
    ASSERT_NE(nullptr, branch); ASSERT_NE(nullptr, leaf);
    first->SetPosition3f(10, 20, 30);
    VxMatrix local = VxMatrix::Identity();
    local[0].Set(0, 2, 0); local[1].Set(-3, 0, 0); local[2].Set(0, 0, 1); local[3].Set(1, 2, 3);
    branch->SetLocalMatrix(local);
    VxMatrix leafLocal = VxMatrix::Identity(); leafLocal[3].Set(4, 5, 6); leaf->SetLocalMatrix(leafLocal);
    const VxVector localPoints[4] = {VxVector(0,0,0),VxVector(1,0,0),VxVector(0,1,0),VxVector(0,0,1)};
    const VxVector worldPoints[4] = {VxVector(-4,30,39),VxVector(-4,32,39),VxVector(-7,30,39),VxVector(-4,30,40)};
    ASSERT_NE(nullptr, AttachPoints(leaf, localPoints, 4));
    ASSERT_NE(nullptr, AttachPoints(second, worldPoints, 4));
    const VxBbox box = first->GetHierarchicalBox(FALSE);
    ASSERT_NEAR(-7, box.Min.x, .0001f); ASSERT_NEAR(-4, box.Max.x, .0001f);
    ASSERT_NEAR(30, box.Min.y, .0001f); ASSERT_NEAR(32, box.Max.y, .0001f);
    ASSERT_NEAR(39, box.Min.z, .0001f); ASSERT_NEAR(40, box.Max.z, .0001f);
    PlaceFitter fitter; VxMatrix fitted;
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    for (int i = 0; i < 4; ++i) ExpectContains(fitted, worldPoints[i]);
}

TEST_F(PlaceFitterHierarchyTest, MeshlessAndHiddenBranchesStillContributeTheirDescendantGeometry) {
    RCK3dEntity *first = Entity(nullptr, true), *second = Entity(nullptr, true);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    RCK3dEntity *branch = Entity(first), *leaf = Entity(branch);
    ASSERT_NE(nullptr, branch); ASSERT_NE(nullptr, leaf);
    const VxVector point(3, 5, 7);
    ASSERT_NE(nullptr, AttachPoints(leaf, &point, 1));
    ASSERT_NE(nullptr, AttachPoints(second, &point, 1));
    branch->Show(CKHIDE); leaf->Show(CKHIDE);
    PlaceFitter fitter; VxMatrix fitted;
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    ExpectCenter(fitted, point); ExpectContains(fitted, point);
}

TEST_F(PlaceFitterHierarchyTest, RepeatedFitUsesOnlyCurrentMeshesAndDoesNotRetainEarlierPoints) {
    RCK3dEntity *first = Entity(nullptr, true), *second = Entity(nullptr, true);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    const VxVector oldPoint(1, 2, 3), newPoint(9, 10, 11);
    ASSERT_NE(nullptr, AttachPoints(first, &oldPoint, 1));
    ASSERT_NE(nullptr, AttachPoints(second, &oldPoint, 1));
    PlaceFitter fitter; VxMatrix fitted;
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    ExpectCenter(fitted, oldPoint);
    ASSERT_NE(nullptr, AttachPoints(first, &newPoint, 1));
    VxMatrix unchanged = VxMatrix::Identity(); unchanged[3].Set(17, 19, 23); fitted = unchanged;
    EXPECT_FALSE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_FLOAT_EQ(unchanged[row][column], fitted[row][column]);
    ASSERT_NE(nullptr, AttachPoints(second, &newPoint, 1));
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    ExpectCenter(fitted, newPoint);
}

TEST_F(PlaceFitterHierarchyTest, EachDominantAxisAndIndependentUnionExtremaKeepAllCommonVertices) {
    for (int dominant = 0; dominant < 3; ++dominant) {
        RCK3dEntity *first = Entity(nullptr, true), *second = Entity(nullptr, true);
        ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
        VxVector common[8]; int index = 0;
        for (int x = -1; x <= 1; x += 2)
            for (int y = -1; y <= 1; y += 2)
                for (int z = -1; z <= 1; z += 2) {
                    common[index].Set(static_cast<float>(x), 2.0f * y, 3.0f * z);
                    common[index][dominant] *= 16.0f; ++index;
                }
        VxVector left[9], right[9];
        for (int i = 0; i < 8; ++i) left[i] = right[i] = common[i];
        left[8].Set(-7, 4, -3); right[8].Set(8, -5, 4);
        ASSERT_NE(nullptr, AttachPoints(first, left, 9));
        ASSERT_NE(nullptr, AttachPoints(second, right, 9));
        PlaceFitter fitter; VxMatrix fitted;
        ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
        ExpectCenter(fitted, VxVector(0, 0, 0));
        for (int i = 0; i < 8; ++i) ExpectContains(fitted, common[i]);
    }
}

TEST_F(PlaceFitterHierarchyTest, AuthoredPatchControlPointsRemainThePortablePreferredSource) {
    RCK3dEntity *first = Entity(nullptr, true), *second = Entity(nullptr, true);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    RCKPatchMesh *patch = static_cast<RCKPatchMesh *>(Context->CreateObject(CKCID_PATCHMESH));
    ASSERT_NE(nullptr, patch); Quad(patch);
    ASSERT_EQ(16, patch->GetModifierVertexCount()); ASSERT_EQ(9, patch->GetVertexCount());
    const VxVector control(2.0f / 3.0f, 1, 1);
    for (int i = 0; i < patch->GetVertexCount(); ++i) {
        VxVector raw; patch->GetVertexPosition(i, &raw);
        const float dx = raw.x - control.x, dy = raw.y - control.y, dz = raw.z - control.z;
        ASSERT_GT(dx * dx + dy * dy + dz * dz, .0001f);
    }
    first->SetCurrentMesh(patch); first->UpdateBox(TRUE);
    ASSERT_NE(nullptr, AttachPoints(second, &control, 1));
    PlaceFitter fitter; VxMatrix fitted;
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    ExpectCenter(fitted, control); ExpectContains(fitted, control);
}

TEST_F(PlaceFitterHierarchyTest, PatchWithOnlyRawMeshVerticesUsesTheExistingFallback) {
    RCK3dEntity *first = Entity(nullptr, true), *second = Entity(nullptr, true);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    RCKPatchMesh *patch = static_cast<RCKPatchMesh *>(Context->CreateObject(CKCID_PATCHMESH));
    ASSERT_NE(nullptr, patch);
    const VxVector points[4] = {VxVector(0,0,0),VxVector(2,0,0),VxVector(0,3,0),VxVector(0,0,4)};
    ASSERT_NE(nullptr, AttachPoints(first, points, 4, patch));
    CKDWORD modifierStride = 0;
    ASSERT_EQ(nullptr, patch->GetModifierVertices(&modifierStride));
    ASSERT_EQ(0, patch->GetModifierVertexCount()); ASSERT_EQ(4, patch->GetVertexCount());
    ASSERT_NE(nullptr, AttachPoints(second, points, 4));
    PlaceFitter fitter; VxMatrix fitted;
    ASSERT_TRUE(fitter.ComputeBestFitBBox(Api(first), Api(second), fitted));
    for (int i = 0; i < 4; ++i) ExpectContains(fitted, points[i]);
}
