#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKPlace.h"
#include "RCKMesh.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class PlaceGeometryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKPlace *API(RCKPlace &place) { return reinterpret_cast<CKPlace *>(&place); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    RCKMesh *Mesh(RCK3dEntity &entity, const VxVector *points, int count) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_NE(nullptr, mesh); if (!mesh) return nullptr;
        EXPECT_TRUE(mesh->SetVertexCount(count));
        for (int i = 0; i < count; ++i) {
            VxVector position = points[i]; mesh->SetVertexPosition(i, &position);
        }
        mesh->VertexMove(); entity.SetCurrentMesh(mesh); return mesh;
    }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, 0.0001f); EXPECT_NEAR(y, v.y, 0.0001f); EXPECT_NEAR(z, v.z, 0.0001f);
    }
    static void Same(const VxMatrix &a, const VxMatrix &b) {
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            EXPECT_FLOAT_EQ(a[row][column], b[row][column]);
    }
    static void Contains(const VxMatrix &matrix, const VxVector &world) {
        for (int axis = 0; axis < 3; ++axis) {
            const VxVector &v = matrix[axis]; const float size2 = v.x*v.x + v.y*v.y + v.z*v.z;
            ASSERT_GT(size2, 0.0f);
            const float local = ((world.x-matrix[3].x)*v.x + (world.y-matrix[3].y)*v.y +
                                 (world.z-matrix[3].z)*v.z)/size2;
            EXPECT_LE(std::fabs(local), 1.0001f);
        }
    }
    static void FiniteBox(const VxMatrix &matrix) {
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            EXPECT_TRUE(std::isfinite(matrix[row][column]));
        for (int axis = 0; axis < 3; ++axis) EXPECT_GE(matrix[axis].Magnitude(), .0999f);
        EXPECT_FLOAT_EQ(1, matrix[3][3]);
    }
    static void Cube(VxVector *points, float minX, float maxX, float size) {
        int index = 0;
        for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < 2; ++z)
            points[index++].Set(x ? maxX : minX, y ? size : -size, z ? size : -size);
    }
};
}

TEST_F(PlaceGeometryTest, NullTargetLeavesOutputMatrixUnchanged) {
    RCKPlace place(Context); VxMatrix matrix = VxMatrix::Identity(); matrix[3].Set(7,11,13); const VxMatrix before = matrix;
    EXPECT_FALSE(API(place)->ComputeBestFitBBox(nullptr, matrix)); Same(before, matrix);
}
TEST_F(PlaceGeometryTest, MissingGeometryLeavesOutputMatrixUnchanged) {
    RCKPlace a(Context), b(Context); VxMatrix matrix = VxMatrix::Identity(); matrix[3].Set(7,11,13); const VxMatrix before = matrix;
    EXPECT_FALSE(a.ComputeBestFitBBox(API(b), matrix)); Same(before, matrix);
}
TEST_F(PlaceGeometryTest, DisjointVerticesCannotProduceAPortalBox) {
    RCKPlace a(Context), b(Context); const VxVector points[] = {VxVector(0,0,0),VxVector(2,0,0),VxVector(0,2,0)};
    Mesh(a, points, 3); Mesh(b, points, 3); b.SetPosition3f(100,100,100);
    VxMatrix matrix = VxMatrix::Identity(); const VxMatrix before = matrix;
    EXPECT_FALSE(a.ComputeBestFitBBox(API(b), matrix)); Same(before, matrix);
}
TEST_F(PlaceGeometryTest, SharedCubeProducesFiniteBoxContainingAllWorldVertices) {
    RCKPlace a(Context), b(Context); VxVector points[8]; Cube(points,-1,1,1); Mesh(a,points,8); Mesh(b,points,8);
    VxMatrix matrix; ASSERT_TRUE(a.ComputeBestFitBBox(API(b), matrix)); FiniteBox(matrix); Vec(matrix[3],0,0,0);
    for (int i=0;i<8;++i) Contains(matrix,points[i]);
}
TEST_F(PlaceGeometryTest, SharedPlanarFaceHasNonzeroThicknessAndWorldCenter) {
    RCKPlace a(Context), b(Context); VxVector first[8], second[8]; Cube(first,-1,1,1); Cube(second,1,3,1);
    Mesh(a,first,8); Mesh(b,second,8); VxMatrix matrix;
    ASSERT_TRUE(a.ComputeBestFitBBox(API(b),matrix)); FiniteBox(matrix); Vec(matrix[3],1,0,0);
    int thinAxes=0;
    for(int i=0;i<3;++i) if(std::fabs(matrix[i].Magnitude()-.1f)<.0001f) ++thinAxes;
    EXPECT_EQ(1,thinAxes);
    for(int y=-1;y<=1;y+=2) for(int z=-1;z<=1;z+=2) Contains(matrix,VxVector(1,static_cast<float>(y),static_cast<float>(z)));
}
TEST_F(PlaceGeometryTest, OneSharedVertexProducesFinitePaddedBox) {
    RCKPlace a(Context), b(Context); const VxVector point(7,11,13); Mesh(a,&point,1); Mesh(b,&point,1); VxMatrix matrix;
    ASSERT_TRUE(a.ComputeBestFitBBox(API(b),matrix)); FiniteBox(matrix); Vec(matrix[3],7,11,13);
    for(int i=0;i<3;++i) EXPECT_NEAR(.1f,matrix[i].Magnitude(),.0001f);
    Contains(matrix,point);
}
TEST_F(PlaceGeometryTest, SharedEdgeProducesBoxWithoutRequiringThreeCommonVertices) {
    RCKPlace a(Context), b(Context); const VxVector first[] = {VxVector(0,0,0),VxVector(1,0,0),VxVector(0,2,0)};
    const VxVector second[] = {VxVector(0,0,0),VxVector(1,0,0),VxVector(0,-2,0)};
    Mesh(a,first,3); Mesh(b,second,3); VxMatrix matrix;
    ASSERT_TRUE(a.ComputeBestFitBBox(API(b),matrix)); FiniteBox(matrix); Vec(matrix[3],.5f,0,0);
    Contains(matrix,first[0]); Contains(matrix,first[1]);
}
TEST_F(PlaceGeometryTest, DescendantMeshUsesWorldTransformWhenComparedWithRootMesh) {
    RCKPlace a(Context), b(Context); RCK3dEntity child(Context); a.SetPosition3f(10,0,0);
    ASSERT_TRUE(child.SetParent(Entity(a),FALSE));
    const VxVector first[] = {VxVector(0,0,0),VxVector(2,0,0),VxVector(0,2,0)};
    const VxVector world[] = {VxVector(10,0,0),VxVector(12,0,0),VxVector(10,2,0)};
    Mesh(child,first,3); Mesh(b,world,3); VxMatrix matrix;
    ASSERT_TRUE(a.ComputeBestFitBBox(API(b),matrix)); FiniteBox(matrix);
    for(int i=0;i<3;++i) Contains(matrix,world[i]);
}
TEST_F(PlaceGeometryTest, LargeExtentDoesNotTreatSeparatedVerticesAsCommon) {
    RCKPlace a(Context), b(Context); RCK3dEntity first(Context), second(Context);
    ASSERT_TRUE(first.SetParent(Entity(a),FALSE)); ASSERT_TRUE(second.SetParent(Entity(b),FALSE));
    VxVector points[8]; Cube(points,-500,500,500); Mesh(first,points,8); Mesh(second,points,8);
    b.SetPosition3f(0,0,.04f); VxMatrix matrix = VxMatrix::Identity(); const VxMatrix before = matrix;
    EXPECT_FALSE(a.ComputeBestFitBBox(API(b),matrix)); Same(before,matrix);
}
TEST_F(PlaceGeometryTest, NearbyVerticesFitSecondPlaceWorldCoordinates) {
    RCKPlace a(Context), b(Context); const VxVector points[] = {VxVector(0,0,0),VxVector(10,0,0),VxVector(0,10,0)};
    Mesh(a,points,3); Mesh(b,points,3); b.SetPosition3f(0,0,.005f); VxMatrix matrix;
    ASSERT_TRUE(a.ComputeBestFitBBox(API(b),matrix)); FiniteBox(matrix); EXPECT_NEAR(.005f,matrix[3].z,.0001f);
    for(int i=0;i<3;++i) Contains(matrix,VxVector(points[i].x,points[i].y,.005f));
}
TEST_F(PlaceGeometryTest, DuplicateQueryVerticesStillPermitOneSharedPoint) {
    RCKPlace a(Context), b(Context); const VxVector point(7,11,13); VxVector duplicates[4]={point,point,point,point};
    Mesh(a,&point,1); Mesh(b,duplicates,4); VxMatrix matrix;
    ASSERT_TRUE(a.ComputeBestFitBBox(API(b),matrix)); FiniteBox(matrix); Vec(matrix[3],7,11,13);
}
