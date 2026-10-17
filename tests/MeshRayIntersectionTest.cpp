#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCK3dEntity.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "RCKTexture.h"
#include "RCKRenderContext.h"
#include "CKRenderedScene.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Observe the existing rasterizer contract; no drawing or backend implementation.
class RayProjectionObserver : public MaterialRenderObserver {
public:
    int ProjectionReads = 0;
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix) override {
        if (type == VXMATRIX_PROJECTION) ++ProjectionReads;
        return MaterialRenderObserver::GetTransformMatrix(type, matrix);
    }
};

class MeshRayIntersectionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    RayProjectionObserver Projection;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }

    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCKMesh *Mesh(int faceCount = 1) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_NE(nullptr, mesh);
        if (!mesh) return nullptr;
        EXPECT_TRUE(mesh->SetVertexCount(3 * faceCount));
        EXPECT_TRUE(mesh->SetFaceCount(faceCount));
        for (int face = 0; face < faceCount; ++face) {
            Triangle(*mesh, face, VxVector(0, 0, 0), VxVector(4, 0, 0), VxVector(0, 4, 0));
        }
        mesh->BuildFaceNormals();
        return mesh;
    }

    static void Triangle(RCKMesh &mesh, int face, VxVector a, VxVector b, VxVector c) {
        const int base = 3 * face;
        mesh.SetVertexPosition(base, &a);
        mesh.SetVertexPosition(base + 1, &b);
        mesh.SetVertexPosition(base + 2, &c);
        mesh.SetFaceVertexIndex(face, base, base + 1, base + 2);
        VxVector normal(0, 0, 1);
        for (int i = 0; i < 3; ++i) mesh.SetVertexNormal(base + i, &normal);
    }

    static int Intersect(RCKMesh &mesh, VxVector origin, VxVector direction,
                         VxIntersectionDesc *description, CK_RAYINTERSECTION mode = CKRAYINTERSECTION_DEFAULT) {
        return RayIntersectionGenericFunc(&mesh, origin, direction, description, mode, VxMatrix::Identity());
    }

    static CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }

    RCKMaterial *AlphaMaterial() {
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
        EXPECT_NE(nullptr, material); EXPECT_NE(nullptr, texture);
        if (!material || !texture) return nullptr;
        EXPECT_TRUE(texture->CreateImage(2, 2, 32));
        texture->SetPickThreshold(128);
        for (int y = 0; y < 2; ++y) {
            EXPECT_TRUE(texture->SetPixel(0, y, 0x00ffffff));
            EXPECT_TRUE(texture->SetPixel(1, y, 0xffffffff));
        }
        material->SetTexture0(texture);
        material->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
        material->EnablePerspectiveCorrection(TRUE);
        return material;
    }

    static void Vector(const VxVector &actual, float x, float y, float z) {
        EXPECT_NEAR(x, actual.x, 0.00001f);
        EXPECT_NEAR(y, actual.y, 0.00001f);
        EXPECT_NEAR(z, actual.z, 0.00001f);
    }
};
}

TEST_F(MeshRayIntersectionTest, CountsGeometricHitsAndKeepsTheFirstNearestTie) {
    RCKMesh *mesh = Mesh(3); ASSERT_NE(nullptr, mesh);
    Triangle(*mesh, 1, VxVector(0, 0, 2), VxVector(4, 0, 2), VxVector(0, 4, 2));
    Triangle(*mesh, 2, VxVector(0, 0, 2), VxVector(4, 0, 2), VxVector(0, 4, 2));
    mesh->BuildFaceNormals();
    VxIntersectionDesc hit = {};
    ASSERT_EQ(3, Intersect(*mesh, VxVector(1, 1, 5), VxVector(0, 0, -10), &hit));
    EXPECT_EQ(1, hit.FaceIndex);
    EXPECT_NEAR(0.3f, hit.Distance, 0.00001f);
    Vector(hit.IntersectionPoint, 1, 1, 2);
}

TEST_F(MeshRayIntersectionTest, DescriptionInterpolatesAuthoredNormalAndBaseUVWithoutNormalizing) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    VxVector normals[] = {VxVector(1, 0, 0), VxVector(0, 2, 0), VxVector(0, 0, 3)};
    for (int i = 0; i < 3; ++i) mesh->SetVertexNormal(i, &normals[i]);
    mesh->SetVertexTextureCoordinates(0, 0.1f, 0.2f, -1);
    mesh->SetVertexTextureCoordinates(1, 0.9f, 0.2f, -1);
    mesh->SetVertexTextureCoordinates(2, 0.1f, 1.0f, -1);
    VxIntersectionDesc hit = {};
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 2, 3), VxVector(0, 0, -6), &hit, CKRAYINTERSECTION_SEGMENT));
    Vector(hit.IntersectionPoint, 1, 2, 0);
    Vector(hit.IntersectionNormal, 0.25f, 0.5f, 1.5f);
    EXPECT_NEAR(0.3f, hit.TexU, 0.00001f);
    EXPECT_NEAR(0.6f, hit.TexV, 0.00001f);
    EXPECT_NEAR(0.5f, hit.Distance, 0.00001f);
}

TEST_F(MeshRayIntersectionTest, MaterialTwoSidedFlagControlsBackfaceHits) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    VxIntersectionDesc hit = {};
    EXPECT_EQ(1, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, -4), &hit));
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, -2), VxVector(0, 0, 4), &hit));
    RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    ASSERT_NE(nullptr, material);
    mesh->SetFaceMaterial(0, reinterpret_cast<CKMaterial *>(material));
    material->SetTwoSided(FALSE);
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, -2), VxVector(0, 0, 4), &hit));
    material->SetTwoSided(TRUE);
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, -2), VxVector(0, 0, 4), &hit));
    Vector(hit.IntersectionNormal, 0, 0, 1);
}

TEST_F(MeshRayIntersectionTest, RayExtendsPastItsSecondPointWhileSegmentDoesNot) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    VxIntersectionDesc hit = {};
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 4), VxVector(0, 0, -2), &hit));
    EXPECT_FLOAT_EQ(2.0f, hit.Distance);
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, 4), VxVector(0, 0, -2), &hit, CKRAYINTERSECTION_SEGMENT));
}

TEST_F(MeshRayIntersectionTest, ParallelOutsideAndBehindRaysDoNotHit) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    VxIntersectionDesc hit = {};
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, 2), VxVector(2, 0, 0), &hit));
    EXPECT_EQ(0, Intersect(*mesh, VxVector(3, 3, 2), VxVector(0, 0, -4), &hit));
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, 4), &hit));
}

TEST_F(MeshRayIntersectionTest, SegmentIncludesItsStartAndEndPlaneContacts) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    VxIntersectionDesc hit = {};
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, -2), &hit, CKRAYINTERSECTION_SEGMENT));
    EXPECT_FLOAT_EQ(1.0f, hit.Distance);
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 0), VxVector(0, 0, -2), &hit, CKRAYINTERSECTION_SEGMENT));
    EXPECT_FLOAT_EQ(0.0f, hit.Distance);
}

TEST_F(MeshRayIntersectionTest, EmptyMeshDoesNotProduceAHit) {
    RCKMesh *mesh = Mesh(0); ASSERT_NE(nullptr, mesh);
    VxIntersectionDesc hit = {};
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, -4), &hit));
}

TEST_F(MeshRayIntersectionTest, SixteenFaceClassificationPreservesInteriorHitsForEachPrincipalAxis) {
    RCKMesh *mesh = Mesh(16); ASSERT_NE(nullptr, mesh);
    const VxVector origins[] = {VxVector(1, 1, 2), VxVector(2, 1, 1), VxVector(1, 2, 1)};
    const VxVector directions[] = {VxVector(0, 0, -4), VxVector(-4, 0, 0), VxVector(0, -4, 0)};
    for (int axis = 0; axis < 3; ++axis) {
        SCOPED_TRACE(axis);
        for (int face = 0; face < 16; ++face) {
            if (axis == 0) Triangle(*mesh, face, VxVector(0, 0, 0), VxVector(4, 0, 0), VxVector(0, 4, 0));
            if (axis == 1) Triangle(*mesh, face, VxVector(0, 0, 0), VxVector(0, 4, 0), VxVector(0, 0, 4));
            if (axis == 2) Triangle(*mesh, face, VxVector(0, 0, 0), VxVector(0, 0, 4), VxVector(4, 0, 0));
        }
        mesh->BuildFaceNormals();
        VxIntersectionDesc hit = {};
        ASSERT_EQ(16, Intersect(*mesh, origins[axis], directions[axis], &hit, CKRAYINTERSECTION_SEGMENT));
        EXPECT_EQ(0, hit.FaceIndex);
        EXPECT_FLOAT_EQ(0.5f, hit.Distance);
    }
}

TEST_F(MeshRayIntersectionTest, ClassificationThresholdDoesNotDiscardExactEdgeAndVertexContacts) {
    const VxVector origins[] = {VxVector(0, 1, -2), VxVector(0, 0, -2)};
    for (int faceCount = 15; faceCount <= 16; ++faceCount) {
        SCOPED_TRACE(faceCount);
        RCKMesh *mesh = Mesh(faceCount); ASSERT_NE(nullptr, mesh);
        for (int face = 0; face < faceCount; ++face)
            Triangle(*mesh, face, VxVector(0, 0, 0), VxVector(0, 4, 0), VxVector(4, 0, 0));
        mesh->BuildFaceNormals();
        for (int contact = 0; contact < 2; ++contact) {
            SCOPED_TRACE(contact);
            VxIntersectionDesc hit = {};
            ASSERT_EQ(faceCount, Intersect(*mesh, origins[contact], VxVector(0, 0, 4), &hit, CKRAYINTERSECTION_SEGMENT));
            EXPECT_EQ(0, hit.FaceIndex);
            EXPECT_FLOAT_EQ(0.5f, hit.Distance);
            Vector(hit.IntersectionPoint, origins[contact].x, origins[contact].y, 0);
        }
    }
}

TEST_F(MeshRayIntersectionTest, UnsignedWordTopologyReadsVerticesAboveTheSignedWordRange) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    ASSERT_TRUE(mesh->SetVertexCount(32771));
    VxVector a(0, 0, 0), b(4, 0, 0), c(0, 4, 0), normal(0, 0, 1);
    mesh->SetVertexPosition(32768, &a); mesh->SetVertexPosition(32769, &b); mesh->SetVertexPosition(32770, &c);
    for (int i = 32768; i <= 32770; ++i) mesh->SetVertexNormal(i, &normal);
    mesh->SetFaceVertexIndex(0, 32768, 32769, 32770);
    mesh->BuildFaceNormals();
    VxIntersectionDesc hit = {};
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, -4), &hit));
    Vector(hit.IntersectionPoint, 1, 1, 0);
    Vector(hit.IntersectionNormal, 0, 0, 1);
}

TEST_F(MeshRayIntersectionTest, PublicEntityConsumerPreservesLocalDescriptionAndInputReferentialDistance) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    RCK3dEntity *reference = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, reference);
    entity->SetCurrentMesh(mesh);
    VxMatrix world = VxMatrix::Identity();
    world[0].Set(0, 2, 0); world[1].Set(-3, 0, 0); world[2].Set(0, 0, 4); world[3].Set(10, 20, 30);
    entity->SetWorldMatrix(world);
    const VxVector from(7, 22, 38), to(7, 22, 22);
    VxIntersectionDesc hit = {};
    ASSERT_EQ(1, entity->RayIntersection(&from, &to, &hit, nullptr, CKRAYINTERSECTION_SEGMENT));
    EXPECT_EQ(entity, hit.Object); Vector(hit.IntersectionPoint, 1, 1, 0); Vector(hit.IntersectionNormal, 0, 0, 1);
    EXPECT_NEAR(8, hit.Distance, 0.00001f);

    reference->SetWorldMatrix(world);
    const VxVector localFrom(1, 1, 2), localTo(1, 1, -2);
    ASSERT_EQ(1, entity->RayIntersection(&localFrom, &localTo, &hit, API(reference), CKRAYINTERSECTION_SEGMENT));
    EXPECT_EQ(entity, hit.Object); Vector(hit.IntersectionPoint, 1, 1, 0); EXPECT_NEAR(2, hit.Distance, 0.00001f);
    ASSERT_EQ(1, entity->RayIntersection(&localFrom, &localTo, &hit, API(entity), CKRAYINTERSECTION_SEGMENT));
    Vector(hit.IntersectionPoint, 1, 1, 0); EXPECT_NEAR(2, hit.Distance, 0.00001f);
}

TEST_F(MeshRayIntersectionTest, FirstContactChoosesFaceOrderInsteadOfNearestAndReturnsOne) {
    RCKMesh *mesh = Mesh(3); ASSERT_NE(nullptr, mesh);
    Triangle(*mesh, 1, VxVector(0, 0, 2), VxVector(4, 0, 2), VxVector(0, 4, 2));
    mesh->BuildFaceNormals();
    VxIntersectionDesc hit = {};
    const CK_RAYINTERSECTION mode = static_cast<CK_RAYINTERSECTION>(CKRAYINTERSECTION_SEGMENT | CKRAYINTERSECTION_FIRSTCONTACT);
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 5), VxVector(0, 0, -10), &hit, mode));
    EXPECT_EQ(0, hit.FaceIndex);
    EXPECT_FLOAT_EQ(0.5f, hit.Distance);
    Vector(hit.IntersectionPoint, 1, 1, 0);
}

TEST_F(MeshRayIntersectionTest, NonSegmentOptionBitsDoNotShortenAnInfiniteRay) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    VxIntersectionDesc hit = {};
    const CK_RAYINTERSECTION options[] = {
        CKRAYINTERSECTION_IGNOREALPHA, CKRAYINTERSECTION_FIRSTCONTACT,
        static_cast<CK_RAYINTERSECTION>(CKRAYINTERSECTION_IGNOREALPHA | CKRAYINTERSECTION_FIRSTCONTACT)
    };
    for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(i);
        ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 4), VxVector(0, 0, -2), &hit, options[i]));
        EXPECT_FLOAT_EQ(2.0f, hit.Distance);
        const CK_RAYINTERSECTION segment = static_cast<CK_RAYINTERSECTION>(options[i] | CKRAYINTERSECTION_SEGMENT);
        EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, 4), VxVector(0, 0, -2), &hit, segment));
    }
}

TEST_F(MeshRayIntersectionTest, NullDescriptionReportsGeometricOcclusionOnceWithoutTextureSampling) {
    RCKMesh *mesh = Mesh(3); ASSERT_NE(nullptr, mesh);
    RCKMaterial *material = AlphaMaterial(); ASSERT_NE(nullptr, material);
    for (int face = 0; face < 3; ++face) mesh->SetFaceMaterial(face, reinterpret_cast<CKMaterial *>(material));
    for (int i = 0; i < 9; ++i) mesh->SetVertexTextureCoordinates(i, 0, 0, -1);
    EXPECT_EQ(1, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, -4), nullptr, CKRAYINTERSECTION_SEGMENT));
    EXPECT_EQ(0, Intersect(*mesh, VxVector(3, 3, 2), VxVector(0, 0, -4), nullptr, CKRAYINTERSECTION_SEGMENT));
}

TEST_F(MeshRayIntersectionTest, NearestTransparentPixelRejectsTheQueryWithoutSearchingTheFartherOpaqueFace) {
    RCKMesh *mesh = Mesh(2); ASSERT_NE(nullptr, mesh);
    Triangle(*mesh, 1, VxVector(0, 0, 2), VxVector(4, 0, 2), VxVector(0, 4, 2));
    mesh->BuildFaceNormals();
    RCKMaterial *material = AlphaMaterial(); ASSERT_NE(nullptr, material);
    mesh->SetFaceMaterial(1, reinterpret_cast<CKMaterial *>(material));
    for (int i = 3; i < 6; ++i) mesh->SetVertexTextureCoordinates(i, 0, 0, -1);
    VxIntersectionDesc hit = {};
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, 4), VxVector(0, 0, -8), &hit, CKRAYINTERSECTION_SEGMENT));
    // The same bitmap has an opaque right column. Accepted queries keep the geometric count.
    for (int i = 3; i < 6; ++i) mesh->SetVertexTextureCoordinates(i, 1, 0, -1);
    ASSERT_EQ(2, Intersect(*mesh, VxVector(1, 1, 4), VxVector(0, 0, -8), &hit, CKRAYINTERSECTION_SEGMENT));
    EXPECT_EQ(1, hit.FaceIndex);
    EXPECT_FLOAT_EQ(0.25f, hit.Distance);
}

TEST_F(MeshRayIntersectionTest, IgnoreAlphaAcceptsTheSameActualTransparentNearestPixel) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    RCKMaterial *material = AlphaMaterial(); ASSERT_NE(nullptr, material);
    mesh->SetFaceMaterial(0, reinterpret_cast<CKMaterial *>(material));
    for (int i = 0; i < 3; ++i) mesh->SetVertexTextureCoordinates(i, 0, 0, -1);
    VxIntersectionDesc hit = {};
    EXPECT_EQ(0, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, -4), &hit, CKRAYINTERSECTION_SEGMENT));
    const CK_RAYINTERSECTION ignore = static_cast<CK_RAYINTERSECTION>(CKRAYINTERSECTION_SEGMENT | CKRAYINTERSECTION_IGNOREALPHA);
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 2), VxVector(0, 0, -4), &hit, ignore));
    Vector(hit.IntersectionPoint, 1, 1, 0);
    EXPECT_FLOAT_EQ(0, hit.TexU);
}

TEST_F(MeshRayIntersectionTest, FiniteRayHitIsNotLimitedByTheOldNearestDistanceSentinel) {
    RCKMesh *mesh = Mesh(); ASSERT_NE(nullptr, mesh);
    VxIntersectionDesc hit = {};
    ASSERT_EQ(1, Intersect(*mesh, VxVector(1, 1, 1.0e36f), VxVector(0, 0, -1), &hit));
    EXPECT_EQ(0, hit.FaceIndex);
    EXPECT_NEAR(1.0e36f, hit.Distance, 1.0e30f);
    Vector(hit.IntersectionPoint, 1, 1, 0);
}

TEST_F(MeshRayIntersectionTest, ClassificationPreservesTheSameRayAtOrdinaryAndLargeFiniteDirectionScales) {
    RCKMesh *mesh = Mesh(16); ASSERT_NE(nullptr, mesh);
    for (int face = 0; face < 16; ++face)
        Triangle(*mesh, face, VxVector(1, 1, 0), VxVector(5, 1, 0), VxVector(1, 5, 0));
    mesh->BuildFaceNormals();
    const float scales[] = {1, 1.0e14f};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(i);
        VxIntersectionDesc hit = {};
        ASSERT_EQ(16, Intersect(*mesh, VxVector(2, 2, 2), VxVector(0, 0, -4 * scales[i]), &hit));
        Vector(hit.IntersectionPoint, 2, 2, 0);
        EXPECT_NEAR(0.5f / scales[i], hit.Distance, 0.00001f / scales[i]);
    }
}
