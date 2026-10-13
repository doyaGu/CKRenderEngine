#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Entity3dGeometryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    RCKMesh *Mesh() { return static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH)); }
    static CK3dEntity *API(RCK3dEntity *object) { return reinterpret_cast<CK3dEntity *>(object); }
    static void Expect(const VxVector &v, float x, float y, float z) { EXPECT_NEAR(x, v.x, 1e-5f); EXPECT_NEAR(y, v.y, 1e-5f); EXPECT_NEAR(z, v.z, 1e-5f); }
    static void Box(const VxBbox &box, float minX, float minY, float minZ, float maxX, float maxY, float maxZ) { Expect(box.Min, minX, minY, minZ); Expect(box.Max, maxX, maxY, maxZ); }
    static void Triangle(RCKMesh &mesh) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        VxVector vertices[3] = {VxVector(0, 0, 0), VxVector(2, 0, 0), VxVector(0, 2, 0)};
        VxVector normal(0, 0, 1);
        for (int i = 0; i < 3; ++i) { mesh.SetVertexPosition(i, &vertices[i]); mesh.SetVertexNormal(i, &normal); }
        mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.BuildFaceNormals(); mesh.VertexMove();
    }
};

TEST_F(Entity3dGeometryTest, ChangeReferentialRebasesEveryMeshAndNormalsWithoutReparentingOrMovingChildren) {
    RCK3dEntity *object = Entity(), *parent = Entity(), *child = Entity(), *reference = Entity();
    RCKMesh *first = Mesh(), *second = Mesh(); Triangle(*first); Triangle(*second);
    ASSERT_EQ(CK_OK, object->AddMesh(first)); ASSERT_EQ(CK_OK, object->AddMesh(second)); object->SetCurrentMesh(first);
    parent->SetPosition3f(100, 0, 0); object->SetPosition3f(10, 20, 30); child->SetPosition3f(50, 60, 70);
    ASSERT_TRUE(object->SetParent(API(parent), TRUE)); ASSERT_TRUE(child->SetParent(API(object), TRUE));
    VxMatrix ref = VxMatrix::Identity(); ref[0].Set(0, 0, -1); ref[2].Set(1, 0, 0); ref[3].Set(4, 5, 6); reference->SetWorldMatrix(ref);
    object->ChangeReferential(API(reference));
    EXPECT_EQ(API(parent), object->GetParent()); Expect(object->GetWorldMatrix()[3], 4, 5, 6); Expect(child->GetWorldMatrix()[3], 50, 60, 70);
    EXPECT_FALSE(object->GetMoveableFlags() & VX_MOVEABLE_WORLDALIGNED);
    RCKMesh *meshes[2] = {first, second};
    for (int m = 0; m < 2; ++m) {
        VxVector position, normal, world; meshes[m]->GetVertexPosition(0, &position); Expect(position, -24, 15, 6);
        object->Transform(&world, &position); Expect(world, 10, 20, 30);
        meshes[m]->GetVertexPosition(1, &position); Expect(position, -24, 15, 8); object->Transform(&world, &position); Expect(world, 12, 20, 30);
        meshes[m]->GetVertexNormal(0, &normal); Expect(normal, -1, 0, 0);
    }
    Box(object->GetBoundingBox(FALSE), 10, 20, 30, 12, 22, 30);
}

TEST_F(Entity3dGeometryTest, ChangeReferentialToWorldBakesVertexPositionsAndMakesRepeatedWorldCallANoOp) {
    RCK3dEntity *object = Entity(), *parent = Entity(); RCKMesh *mesh = Mesh(); Triangle(*mesh); object->SetCurrentMesh(mesh);
    ASSERT_TRUE(object->SetParent(API(parent), TRUE)); object->SetPosition3f(10, 20, 30);
    object->ChangeReferential(); EXPECT_EQ(API(parent), object->GetParent()); EXPECT_TRUE(object->GetMoveableFlags() & VX_MOVEABLE_WORLDALIGNED);
    Expect(object->GetWorldMatrix()[3], 0, 0, 0); VxVector position; mesh->GetVertexPosition(1, &position); Expect(position, 12, 20, 30);
    object->ChangeReferential(); mesh->GetVertexPosition(1, &position); Expect(position, 12, 20, 30);
    Box(object->GetBoundingBox(FALSE), 10, 20, 30, 12, 22, 30);
}

TEST_F(Entity3dGeometryTest, MeshBoundsInvalidateAfterTranslationAndScalingAndReportWorldBarycenter) {
    RCK3dEntity *object = Entity(); RCKMesh *mesh = Mesh(); Triangle(*mesh); object->SetCurrentMesh(mesh);
    object->SetPosition3f(10, 20, 30); Box(object->GetBoundingBox(TRUE), 0, 0, 0, 2, 2, 0); Box(object->GetBoundingBox(FALSE), 10, 20, 30, 12, 22, 30);
    object->SetScale3f(2, 3, 4); object->Translate3f(-2, -3, -4); Box(object->GetBoundingBox(FALSE), 8, 17, 26, 12, 23, 26);
    VxVector barycenter; ASSERT_TRUE(object->GetBaryCenter(&barycenter)); Expect(barycenter, 8 + 4.0f / 3, 19, 26);
    EXPECT_NEAR(mesh->GetRadius() * 4, object->GetRadius(), 1e-5f);
}

TEST_F(Entity3dGeometryTest, LocalAndWorldUserBoundsOverrideMeshUntilCleared) {
    RCK3dEntity *object = Entity(); RCKMesh *mesh = Mesh(); Triangle(*mesh); object->SetCurrentMesh(mesh); object->SetPosition3f(10, 20, 30);
    VxBbox local; local.Min.Set(-1, -2, -3); local.Max.Set(1, 2, 3); ASSERT_TRUE(object->SetBoundingBox(&local, TRUE));
    Box(object->GetBoundingBox(FALSE), 9, 18, 27, 11, 22, 33);
    VxBbox world; world.Min.Set(0, 10, 20); world.Max.Set(6, 18, 30); ASSERT_TRUE(object->SetBoundingBox(&world, FALSE));
    Box(object->GetBoundingBox(TRUE), -10, -10, -10, -4, -2, 0); object->SetBoundingBox(nullptr);
    Box(object->GetBoundingBox(TRUE), 0, 0, 0, 2, 2, 0); EXPECT_FALSE(object->GetMoveableFlags() & VX_MOVEABLE_USERBOX);
}

TEST_F(Entity3dGeometryTest, NoMeshHasPointBoundsAndFalseBarycenterWhileUserBoxProvidesRadius) {
    RCK3dEntity *object = Entity(); object->SetPosition3f(3, 4, 5); object->UpdateBox(TRUE);
    Box(object->GetBoundingBox(FALSE), 3, 4, 5, 3, 4, 5); Box(object->GetBoundingBox(TRUE), 0, 0, 0, 0, 0, 0);
    VxVector center; EXPECT_FALSE(object->GetBaryCenter(&center)); Expect(center, 3, 4, 5); EXPECT_FLOAT_EQ(0, object->GetRadius());
    VxBbox box; box.Min.Set(-1, -2, -3); box.Max.Set(1, 2, 3); ASSERT_TRUE(object->SetBoundingBox(&box, TRUE)); EXPECT_FLOAT_EQ(3, object->GetRadius());
    object->SetBoundingBox(nullptr); EXPECT_FLOAT_EQ(0, object->GetRadius());
}

TEST_F(Entity3dGeometryTest, HierarchicalBoundsMergeLiveChildBoxesAndConvertToParentLocalCoordinates) {
    new RCKRenderManager(Context); RCK3dEntity *parent = Entity(), *child = Entity(); parent->SetPosition3f(10, 0, 0); child->SetPosition3f(20, 0, 0);
    VxBbox box; box.Min.Set(-1, -1, -1); box.Max.Set(1, 1, 1); parent->SetBoundingBox(&box, TRUE); child->SetBoundingBox(&box, TRUE);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE)); ASSERT_NE(nullptr, parent->m_SceneGraphNode); ASSERT_NE(nullptr, child->m_SceneGraphNode);
    Box(parent->GetHierarchicalBox(FALSE), 9, -1, -1, 21, 1, 1); Box(parent->GetHierarchicalBox(TRUE), -1, -1, -1, 11, 1, 1);
    child->SetPosition3f(30, 0, 0); Box(parent->GetHierarchicalBox(FALSE), 9, -1, -1, 31, 1, 1);
}

TEST_F(Entity3dGeometryTest, HeadlessHierarchicalBoxReturnsActualEntityBounds) {
    RCK3dEntity *object = Entity(); object->SetPosition3f(1, 2, 3); VxBbox box; box.Min.Set(-1, -1, -1); box.Max.Set(1, 1, 1); object->SetBoundingBox(&box, TRUE);
    ASSERT_EQ(nullptr, object->m_SceneGraphNode); Box(object->GetHierarchicalBox(FALSE), 0, 1, 2, 2, 3, 4); Box(object->GetHierarchicalBox(TRUE), -1, -1, -1, 1, 1, 1);
}

TEST_F(Entity3dGeometryTest, SkinUpdateMovesActualModifierVerticesFromBoneAndRejectsInsufficientVertexData) {
    RCK3dEntity *object = Entity(), *bone = Entity(); RCKMesh *mesh = Mesh(); Triangle(*mesh); object->SetCurrentMesh(mesh);
    EXPECT_FALSE(object->UpdateSkin()); CKSkin *skin = object->CreateSkin(); ASSERT_NE(nullptr, skin); EXPECT_FALSE(object->UpdateSkin());
    skin->SetObjectInitMatrix(VxMatrix::Identity()); skin->SetBoneCount(1); skin->SetVertexCount(3);
    skin->GetBoneData(0)->SetBone(API(bone)); skin->GetBoneData(0)->SetBoneInitialInverseMatrix(VxMatrix::Identity());
    for (int i = 0; i < 3; ++i) {
        CKSkinVertexData *data = skin->GetVertexData(i); data->SetBoneCount(1); data->SetBone(0, 0); data->SetWeight(0, 1);
        VxVector initial; mesh->GetVertexPosition(i, &initial); data->SetInitialPos(initial);
    }
    bone->SetPosition3f(4, 5, 6); ASSERT_TRUE(object->UpdateSkin()); VxVector result; mesh->GetVertexPosition(1, &result); Expect(result, 6, 5, 6);
    EXPECT_TRUE(mesh->GetFlags() & VXMESH_HINTDYNAMIC);
    skin->SetNormalCount(3); for (int i = 0; i < 3; ++i) skin->SetNormal(i, VxVector(0, 0, 1));
    bone->SetPosition3f(7, 8, 9); ASSERT_TRUE(object->UpdateSkin()); mesh->GetVertexPosition(1, &result); Expect(result, 9, 8, 9); mesh->GetVertexNormal(1, &result); Expect(result, 0, 0, 1);
    skin->SetNormal(1, VxVector(0, 1, 0)); ASSERT_TRUE(object->UpdateSkin()); mesh->GetVertexNormal(1, &result); Expect(result, 0, 1, 0);
}

TEST_F(Entity3dGeometryTest, HeadlessPointProjectionAndRenderExtentAccessorExposeCurrentState) {
    new RCKRenderManager(Context); RCKRenderContext renderContext(Context); RCK3dEntity *object = Entity();
    object->m_RenderExtents = VxRect(1, 2, 3, 4); EXPECT_FALSE(object->IsInViewFrustrum(nullptr)); EXPECT_FALSE(object->IsInViewFrustrumHierarchic(nullptr));
    VxRect output; object->GetRenderExtents(output); EXPECT_FLOAT_EQ(1, output.left); EXPECT_FLOAT_EQ(2, output.top); EXPECT_FLOAT_EQ(3, output.right); EXPECT_FLOAT_EQ(4, output.bottom);
    // The default identity camera and zero-sized viewport map the origin to (0,0).
    EXPECT_TRUE(object->IsInViewFrustrum(&renderContext, 1)); EXPECT_TRUE(object->IsInViewFrustrumHierarchic(&renderContext));
    object->GetRenderExtents(output); EXPECT_FLOAT_EQ(0, output.left); EXPECT_FLOAT_EQ(0, output.top); EXPECT_FLOAT_EQ(1, output.right); EXPECT_FLOAT_EQ(1, output.bottom);
}

TEST_F(Entity3dGeometryTest, RayIntersectionTransformsWorldAndReferenceSegmentsAndMeasuresInputSpaceDistance) {
    RCK3dEntity *object = Entity(), *reference = Entity(); RCKMesh *mesh = Mesh(); Triangle(*mesh); object->SetCurrentMesh(mesh);
    object->SetPosition3f(10, 20, 30); VxMatrix ref = VxMatrix::Identity(); ref[2].Set(0, 0, 2); ref[3].Set(10, 20, 30); reference->SetWorldMatrix(ref);
    const VxVector from(10.5f, 20.5f, 35), to(10.5f, 20.5f, 25); VxIntersectionDesc hit;
    ASSERT_NE(0, object->RayIntersection(&from, &to, &hit, nullptr, CKRAYINTERSECTION_SEGMENT)); EXPECT_EQ(object, hit.Object); EXPECT_NEAR(5, hit.Distance, 1e-5f);
    const VxVector refFrom(.5f, .5f, 2.5f), refTo(.5f, .5f, -2.5f);
    ASSERT_NE(0, object->RayIntersection(&refFrom, &refTo, &hit, API(reference), CKRAYINTERSECTION_SEGMENT)); EXPECT_NEAR(2.5f, hit.Distance, 1e-5f);
    const VxVector localFrom(.5f, .5f, 5), localTo(.5f, .5f, -5);
    EXPECT_NE(0, object->RayIntersection(&localFrom, &localTo, nullptr, API(object), CKRAYINTERSECTION_SEGMENT));
    const VxVector missFrom(20, 30, 35), missTo(20, 30, 25); EXPECT_EQ(0, object->RayIntersection(&missFrom, &missTo, &hit, nullptr, CKRAYINTERSECTION_SEGMENT)); EXPECT_EQ(nullptr, hit.Object);
}
} // namespace
