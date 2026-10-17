#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern void (*g_BuildFaceNormalsFunc)(CKFace *, CKWORD *, int, VxVertex *, int);
extern void (*g_BuildNormalsFunc)(CKFace *, CKWORD *, int, VxVertex *, int);
extern void (*g_NormalizeFunc)(VxVertex *, int);
extern int (*g_RayIntersection)(RCKMesh *, VxVector &, VxVector &, VxIntersectionDesc *, CK_RAYINTERSECTION, const VxMatrix &);

namespace {
void ExpectNormal(const VxVector &actual, float x, float y, float z) {
    EXPECT_NEAR(x, actual.x, 0.00001f);
    EXPECT_NEAR(y, actual.y, 0.00001f);
    EXPECT_NEAR(z, actual.z, 0.00001f);
}

void InitializeVertices(VxVertex *vertices, int count) {
    for (int i = 0; i < count; ++i) {
        vertices[i].m_Position.Set(static_cast<float>(i), 7.0f, 11.0f);
        vertices[i].m_Normal.Set(9.0f, -8.0f, 7.0f);
        vertices[i].m_UV.Set(i + 0.25f, i + 0.75f);
    }
}

void ExpectUV(const VxVertex *vertices, int count) {
    for (int i = 0; i < count; ++i) {
        EXPECT_FLOAT_EQ(i + 0.25f, vertices[i].m_UV.x);
        EXPECT_FLOAT_EQ(i + 0.75f, vertices[i].m_UV.y);
    }
}

class MeshNormalGenerationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};
}

TEST_F(MeshNormalGenerationTest, FaceWindingUsesPositionPayloadAndPreservesOtherFields) {
    VxVertex vertices[3]; InitializeVertices(vertices, 3);
    vertices[0].m_Position.Set(5, 7, 11);
    vertices[1].m_Position.Set(8, 5, 11);
    vertices[2].m_Position.Set(11, 7, 9);
    CKFace faces[2] = {};
    faces[0].m_MatIndex = 13; faces[0].m_ChannelMask = 0x0123;
    faces[1].m_MatIndex = 29; faces[1].m_ChannelMask = 0x4567;
    CKWORD indices[] = {0, 1, 2, 0, 2, 1};
    BuildFaceNormalsGenericFunc(faces, indices, 2, vertices, 3);
    // The translated triangle's oriented area vector is (4, 6, 12).
    ExpectNormal(faces[0].m_Normal, 2.0f / 7.0f, 3.0f / 7.0f, 6.0f / 7.0f);
    ExpectNormal(faces[1].m_Normal, -2.0f / 7.0f, -3.0f / 7.0f, -6.0f / 7.0f);
    EXPECT_EQ(13, faces[0].m_MatIndex); EXPECT_EQ(0x0123, faces[0].m_ChannelMask);
    EXPECT_EQ(29, faces[1].m_MatIndex); EXPECT_EQ(0x4567, faces[1].m_ChannelMask);
    for (int i = 0; i < 3; ++i) ExpectNormal(vertices[i].m_Normal, 9, -8, 7);
    ExpectNormal(vertices[0].m_Position, 5, 7, 11);
    ExpectNormal(vertices[1].m_Position, 8, 5, 11);
    ExpectNormal(vertices[2].m_Position, 11, 7, 9);
    ExpectUV(vertices, 3);
}

TEST_F(MeshNormalGenerationTest, SmallNonzeroTriangleStillProducesAUnitFaceNormal) {
    VxVertex vertices[3]; InitializeVertices(vertices, 3);
    vertices[0].m_Position.Set(0, 0, 0);
    vertices[1].m_Position.Set(0.001f, 0, 0);
    vertices[2].m_Position.Set(0, 0.002f, 0.003f);
    CKFace face = {}; CKWORD indices[] = {0, 1, 2};
    BuildFaceNormalsGenericFunc(&face, indices, 1, vertices, 3);
    const float root13 = std::sqrt(13.0f);
    ExpectNormal(face.m_Normal, 0, -3.0f / root13, 2.0f / root13);
    EXPECT_NEAR(1.0f, face.m_Normal.SquareMagnitude(), 0.00001f);
    ExpectUV(vertices, 3);
}

TEST_F(MeshNormalGenerationTest, SharedVertexReceivesEqualUnitFaceWeightsDespiteDifferentAreas) {
    VxVertex vertices[6]; InitializeVertices(vertices, 6);
    vertices[0].m_Position.Set(0, 0, 0);
    vertices[1].m_Position.Set(100, 0, 0);
    vertices[2].m_Position.Set(0, 100, 0);
    vertices[3].m_Position.Set(0, 0, 1);
    vertices[4].m_Position.Set(1, 0, 0);
    vertices[5].m_Position.Set(27, 31, 37);
    CKFace faces[2] = {}; CKWORD indices[] = {0, 1, 2, 0, 3, 4};
    BuildNormalsGenericFunc(faces, indices, 2, vertices, 6);
    const float diagonal = 1.0f / std::sqrt(2.0f);
    ExpectNormal(vertices[0].m_Normal, 0, diagonal, diagonal);
    ExpectNormal(vertices[1].m_Normal, 0, 0, 1);
    ExpectNormal(vertices[2].m_Normal, 0, 0, 1);
    ExpectNormal(vertices[3].m_Normal, 0, 1, 0);
    ExpectNormal(vertices[4].m_Normal, 0, 1, 0);
    ExpectNormal(vertices[5].m_Normal, 0, 0, 0);
    ExpectNormal(faces[0].m_Normal, 0, 0, 1);
    ExpectNormal(faces[1].m_Normal, 0, 1, 0);
    ExpectUV(vertices, 6);
}

TEST_F(MeshNormalGenerationTest, OpposingAndDegenerateFacesLeaveFiniteZeroNormalsAndIsolatedVertices) {
    VxVertex vertices[4]; InitializeVertices(vertices, 4);
    vertices[0].m_Position.Set(0, 0, 0);
    vertices[1].m_Position.Set(2, 0, 0);
    vertices[2].m_Position.Set(0, 3, 0);
    CKFace faces[3] = {}; CKWORD indices[] = {0, 1, 2, 0, 2, 1, 0, 1, 1};
    BuildNormalsGenericFunc(faces, indices, 3, vertices, 4);
    ExpectNormal(faces[0].m_Normal, 0, 0, 1);
    ExpectNormal(faces[1].m_Normal, 0, 0, -1);
    ExpectNormal(faces[2].m_Normal, 0, 0, 0);
    for (int i = 0; i < 4; ++i) {
        ExpectNormal(vertices[i].m_Normal, 0, 0, 0);
        EXPECT_TRUE(std::isfinite(vertices[i].m_Normal.x));
        EXPECT_TRUE(std::isfinite(vertices[i].m_Normal.y));
        EXPECT_TRUE(std::isfinite(vertices[i].m_Normal.z));
    }
    ExpectUV(vertices, 4);
}

TEST_F(MeshNormalGenerationTest, NearlyOpposingFacesNormalizeTheirNonzeroSharedVertexSum) {
    VxVertex vertices[4]; InitializeVertices(vertices, 4);
    vertices[0].m_Position.Set(0, 0, 0);
    vertices[1].m_Position.Set(1, 0, 0);
    vertices[2].m_Position.Set(0, 1, 0);
    vertices[3].m_Position.Set(0, -1, 0.0001f);
    CKFace faces[2] = {}; CKWORD indices[] = {0, 1, 2, 0, 1, 3};
    BuildNormalsGenericFunc(faces, indices, 2, vertices, 4);
    // The two unit face normals are (0,0,1) and approximately
    // (0,-0.0001,-1). Their sum has a well-defined -Y direction.
    ExpectNormal(faces[0].m_Normal, 0, 0, 1);
    ExpectNormal(faces[1].m_Normal, 0, -0.0001f, -1);
    for (int i = 0; i < 2; ++i) {
        EXPECT_NEAR(0.0f, vertices[i].m_Normal.x, 0.00001f);
        EXPECT_LT(vertices[i].m_Normal.y, -0.9999f);
        EXPECT_NEAR(0.0f, vertices[i].m_Normal.z, 0.001f);
        EXPECT_NEAR(1.0f, vertices[i].m_Normal.SquareMagnitude(), 0.00001f);
    }
    ExpectNormal(vertices[2].m_Normal, 0, 0, 1);
    ExpectNormal(vertices[3].m_Normal, 0, -0.0001f, -1);
    ExpectUV(vertices, 4);
}

TEST_F(MeshNormalGenerationTest, RebuildingReplacesOldVertexNormalsAndRecomputesTheFaceDirection) {
    VxVertex vertices[3]; InitializeVertices(vertices, 3);
    vertices[0].m_Position.Set(0, 0, 0);
    vertices[1].m_Position.Set(3, 0, 0);
    vertices[2].m_Position.Set(0, 4, 0);
    CKFace face = {}; face.m_MatIndex = 11; face.m_ChannelMask = 0x89ab;
    CKWORD indices[] = {0, 1, 2};
    BuildNormalsGenericFunc(&face, indices, 1, vertices, 3);
    for (int i = 0; i < 3; ++i) ExpectNormal(vertices[i].m_Normal, 0, 0, 1);
    vertices[1].m_Position.Set(0, 3, 0);
    vertices[2].m_Position.Set(0, 0, 4);
    BuildNormalsGenericFunc(&face, indices, 1, vertices, 3);
    for (int i = 0; i < 3; ++i) ExpectNormal(vertices[i].m_Normal, 1, 0, 0);
    ExpectNormal(face.m_Normal, 1, 0, 0);
    EXPECT_EQ(11, face.m_MatIndex); EXPECT_EQ(0x89ab, face.m_ChannelMask);
    ExpectUV(vertices, 3);
}

TEST_F(MeshNormalGenerationTest, NormalizeTouchesOnlyRequestedNormalsAndPreservesZeroAndOtherPayload) {
    VxVertex vertices[5]; InitializeVertices(vertices, 5);
    vertices[0].m_Normal.Set(3, 4, 0);
    vertices[1].m_Normal.Set(0, 0, 0);
    vertices[2].m_Normal.Set(0, 0, -12);
    vertices[3].m_Normal.Set(0, 0, 0.0001f);
    vertices[4].m_Normal.Set(9, 12, 0);
    NormalizeGenericFunc(vertices, 4);
    ExpectNormal(vertices[0].m_Normal, 0.6f, 0.8f, 0);
    ExpectNormal(vertices[1].m_Normal, 0, 0, 0);
    ExpectNormal(vertices[2].m_Normal, 0, 0, -1);
    ExpectNormal(vertices[3].m_Normal, 0, 0, 1);
    ExpectNormal(vertices[4].m_Normal, 9, 12, 0);
    NormalizeGenericFunc(vertices, 0);
    ExpectNormal(vertices[0].m_Normal, 0.6f, 0.8f, 0);
    ExpectNormal(vertices[4].m_Normal, 9, 12, 0);
    for (int i = 0; i < 5; ++i) ExpectNormal(vertices[i].m_Position, static_cast<float>(i), 7, 11);
    ExpectUV(vertices, 5);
}

TEST_F(MeshNormalGenerationTest, UnsignedWordIndicesReachVerticesAboveTheSignedWordRange) {
    XArray<VxVertex> vertices; vertices.Resize(32771);
    const int first = 32768;
    vertices[first].m_Position.Set(0, 0, 0);
    vertices[first + 1].m_Position.Set(2, 0, 0);
    vertices[first + 2].m_Position.Set(0, 3, 0);
    CKFace face = {}; CKWORD indices[] = {32768, 32769, 32770};
    BuildNormalsGenericFunc(&face, indices, 1, vertices.Begin(), vertices.Size());
    ExpectNormal(face.m_Normal, 0, 0, 1);
    for (int i = first; i < first + 3; ++i) ExpectNormal(vertices[i].m_Normal, 0, 0, 1);
    ExpectNormal(vertices[0].m_Normal, 0, 0, 0);
    ExpectNormal(vertices[first - 1].m_Normal, 0, 0, 0);
}

TEST_F(MeshNormalGenerationTest, RepeatedProcessorSetupProvidesCallableGeometryDispatchEntries) {
    for (int iteration = 0; iteration < 2; ++iteration) {
        g_BuildFaceNormalsFunc = nullptr; g_BuildNormalsFunc = nullptr;
        g_NormalizeFunc = nullptr; g_RayIntersection = nullptr;
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_NE(nullptr, g_BuildFaceNormalsFunc); ASSERT_NE(nullptr, g_BuildNormalsFunc);
        ASSERT_NE(nullptr, g_NormalizeFunc); ASSERT_NE(nullptr, g_RayIntersection);
        VxVertex vertices[4]; InitializeVertices(vertices, 4);
        vertices[0].m_Position.Set(0, 0, 0);
        vertices[1].m_Position.Set(0, 2, 0);
        vertices[2].m_Position.Set(0, 0, 3);
        CKFace face = {}; CKWORD indices[] = {0, 1, 2};
        g_BuildFaceNormalsFunc(&face, indices, 1, vertices, 4);
        ExpectNormal(face.m_Normal, 1, 0, 0);
        g_BuildNormalsFunc(&face, indices, 1, vertices, 4);
        for (int i = 0; i < 3; ++i) ExpectNormal(vertices[i].m_Normal, 1, 0, 0);
        ExpectNormal(vertices[3].m_Normal, 0, 0, 0);
        vertices[0].m_Normal.Set(0, 6, 8);
        g_NormalizeFunc(vertices, 1);
        ExpectNormal(vertices[0].m_Normal, 0, 0.6f, 0.8f);
        ExpectUV(vertices, 4);
    }
}

TEST_F(MeshNormalGenerationTest, PublicMeshRebuildAndNormalNotificationPreserveAuthoredPayload) {
    RCKMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(4)); mesh.SetFaceCount(1);
    const VxVector points[] = {VxVector(0, 0, 0), VxVector(3, 0, 0), VxVector(0, 4, 0), VxVector(11, 13, 17)};
    for (int i = 0; i < 4; ++i) {
        VxVector point = points[i], normal(9, -8, 7);
        mesh.SetVertexPosition(i, &point); mesh.SetVertexNormal(i, &normal);
        mesh.SetVertexTextureCoordinates(i, i + 0.25f, i + 0.75f, -1);
        mesh.SetVertexColor(i, 0xff123400u + i); mesh.SetVertexSpecularColor(i, 0x10203000u + i);
    }
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.BuildNormals(); ExpectNormal(mesh.GetFaceNormal(0), 0, 0, 1);
    EXPECT_NE(0u, mesh.GetFlags() & VXMESH_GENNORMALS);
    EXPECT_NE(0u, mesh.GetFlags() & VXMESH_NORMAL_CHANGED);
    for (int i = 0; i < 4; ++i) {
        VxVector actual; mesh.GetVertexNormal(i, &actual);
        ExpectNormal(actual, 0, 0, i == 3 ? 0.0f : 1.0f);
    }
    VxVector authored(0, -1, 0); mesh.SetVertexNormal(0, &authored); mesh.NormalChanged();
    EXPECT_EQ(0u, mesh.GetFlags() & VXMESH_GENNORMALS);
    mesh.SetFaceVertexIndex(0, 0, 2, 1); mesh.BuildFaceNormals();
    ExpectNormal(mesh.GetFaceNormal(0), 0, 0, -1);
    VxVector actual; mesh.GetVertexNormal(0, &actual); ExpectNormal(actual, 0, -1, 0);
    mesh.BuildNormals();
    for (int i = 0; i < 4; ++i) {
        mesh.GetVertexNormal(i, &actual); ExpectNormal(actual, 0, 0, i == 3 ? 0.0f : -1.0f);
        mesh.GetVertexPosition(i, &actual); ExpectNormal(actual, points[i].x, points[i].y, points[i].z);
        float u = 0, v = 0; mesh.GetVertexTextureCoordinates(i, &u, &v, -1);
        EXPECT_FLOAT_EQ(i + 0.25f, u); EXPECT_FLOAT_EQ(i + 0.75f, v);
        EXPECT_EQ(0xff123400u + i, mesh.GetVertexColor(i));
        EXPECT_EQ(0x10203000u + i, mesh.GetVertexSpecularColor(i));
    }
}

TEST_F(MeshNormalGenerationTest, NormalizePreservesDirectionAcrossFiniteFloatScales) {
    const float scales[] = {1.0e-30f, 1.0e30f};
    for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex) {
        VxVertex vertices[2]; InitializeVertices(vertices, 2);
        vertices[0].m_Normal.Set(3.0f * scales[scaleIndex], 4.0f * scales[scaleIndex], 0);
        vertices[1].m_Normal.Set(-3.0f * scales[scaleIndex], 0, -4.0f * scales[scaleIndex]);
        NormalizeGenericFunc(vertices, 2);
        ExpectNormal(vertices[0].m_Normal, 0.6f, 0.8f, 0);
        ExpectNormal(vertices[1].m_Normal, -0.6f, 0, -0.8f);
        EXPECT_NEAR(1.0f, vertices[0].m_Normal.SquareMagnitude(), 0.00001f);
        EXPECT_NEAR(1.0f, vertices[1].m_Normal.SquareMagnitude(), 0.00001f);
        for (int i = 0; i < 2; ++i) ExpectNormal(vertices[i].m_Position, static_cast<float>(i), 7, 11);
        ExpectUV(vertices, 2);
    }
}

TEST_F(MeshNormalGenerationTest, FaceNormalsPreserveWindingAcrossFiniteFloatScales) {
    const float scales[] = {1.0e-30f, 1.0e30f};
    for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex) {
        VxVertex vertices[3]; InitializeVertices(vertices, 3);
        vertices[0].m_Position.Set(0, 0, 0);
        vertices[1].m_Position.Set(3.0f * scales[scaleIndex], 0, 0);
        vertices[2].m_Position.Set(0, 4.0f * scales[scaleIndex], 0);
        CKFace faces[2] = {};
        faces[0].m_MatIndex = 13; faces[0].m_ChannelMask = 0x0123;
        faces[1].m_MatIndex = 29; faces[1].m_ChannelMask = 0x4567;
        CKWORD indices[] = {0, 1, 2, 0, 2, 1};
        BuildFaceNormalsGenericFunc(faces, indices, 2, vertices, 3);
        ExpectNormal(faces[0].m_Normal, 0, 0, 1);
        ExpectNormal(faces[1].m_Normal, 0, 0, -1);
        EXPECT_EQ(13, faces[0].m_MatIndex); EXPECT_EQ(0x0123, faces[0].m_ChannelMask);
        EXPECT_EQ(29, faces[1].m_MatIndex); EXPECT_EQ(0x4567, faces[1].m_ChannelMask);
        for (int i = 0; i < 3; ++i) ExpectNormal(vertices[i].m_Normal, 9, -8, 7);
        ExpectNormal(vertices[0].m_Position, 0, 0, 0);
        ExpectNormal(vertices[1].m_Position, 3.0f * scales[scaleIndex], 0, 0);
        ExpectNormal(vertices[2].m_Position, 0, 4.0f * scales[scaleIndex], 0);
        ExpectUV(vertices, 3);
    }
}
