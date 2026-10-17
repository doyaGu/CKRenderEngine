#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct Payload {
    VxVector Position;
    VxVector Normal;
    Vx2DVector UV;
    CKDWORD Diffuse;
    CKDWORD Specular;
};

Payload InitialPayload(int index) {
    Payload result;
    result.Position = VxVector(float(10 + index), float(20 + index * 2), float(30 - index));
    result.Normal = VxVector(float(2 + index), float(3 - index), float(4 + index * 2));
    result.UV = Vx2DVector(.125f + index, .375f - index);
    result.Diffuse = 0xFF102030u + static_cast<CKDWORD>(index * 0x010101u);
    result.Specular = 0x80405060u + static_cast<CKDWORD>(index * 0x000102u);
    return result;
}

void ExpectVector(const VxVector &actual, const VxVector &expected) {
    EXPECT_FLOAT_EQ(expected.x, actual.x);
    EXPECT_FLOAT_EQ(expected.y, actual.y);
    EXPECT_FLOAT_EQ(expected.z, actual.z);
}

void SetPayload(RCKMesh &mesh, int index, Payload payload) {
    mesh.SetVertexPosition(index, &payload.Position);
    mesh.SetVertexNormal(index, &payload.Normal);
    mesh.SetVertexTextureCoordinates(index, payload.UV.x, payload.UV.y, -1);
    mesh.SetVertexColor(index, payload.Diffuse);
    mesh.SetVertexSpecularColor(index, payload.Specular);
}

void ExpectPayload(RCKMesh &mesh, int index, const Payload &expected) {
    SCOPED_TRACE(index);
    VxVector position, normal;
    float u = 0, v = 0;
    mesh.GetVertexPosition(index, &position);
    mesh.GetVertexNormal(index, &normal);
    mesh.GetVertexTextureCoordinates(index, &u, &v, -1);
    ExpectVector(position, expected.Position);
    ExpectVector(normal, expected.Normal);
    EXPECT_FLOAT_EQ(expected.UV.x, u);
    EXPECT_FLOAT_EQ(expected.UV.y, v);
    EXPECT_EQ(expected.Diffuse, mesh.GetVertexColor(index));
    EXPECT_EQ(expected.Specular, mesh.GetVertexSpecularColor(index));
}

void ExpectPayloads(RCKMesh &mesh, const Payload *expected, int count) {
    ASSERT_EQ(count, mesh.GetVertexCount());
    for (int index = 0; index < count; ++index)
        ExpectPayload(mesh, index, expected[index]);
}

void Fill(RCKMesh &mesh, Payload *expected, int count) {
    ASSERT_TRUE(mesh.SetVertexCount(count));
    for (int index = 0; index < count; ++index) {
        expected[index] = InitialPayload(index);
        SetPayload(mesh, index, expected[index]);
    }
}

void MakeTwoTriangles(RCKMesh &mesh, Payload *expected) {
    Fill(mesh, expected, 4);
    const VxVector positions[] = {
        VxVector(0, 0, 0), VxVector(2, 0, 0),
        VxVector(0, 3, 0), VxVector(0, 0, 4)
    };
    for (int index = 0; index < 4; ++index) {
        expected[index].Position = positions[index];
        mesh.SetVertexPosition(index, &expected[index].Position);
    }
    ASSERT_TRUE(mesh.SetFaceCount(2));
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.SetFaceVertexIndex(1, 0, 3, 1);
}

void ExpectTriangle(RCKMesh &mesh, int face, int a, int b, int c) {
    int actualA = -1, actualB = -1, actualC = -1;
    mesh.GetFaceVertexIndex(face, actualA, actualB, actualC);
    EXPECT_EQ(a, actualA);
    EXPECT_EQ(b, actualB);
    EXPECT_EQ(c, actualC);
}

template<class T>
void WriteStream(void *pointer, CKDWORD stride, int index, const T &value) {
    ASSERT_NE(nullptr, pointer);
    ASSERT_GE(stride, sizeof(T));
    std::memcpy(static_cast<CKBYTE *>(pointer) + index * stride, &value, sizeof(T));
}

class MeshVertexStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    RCKMaterial *Material() {
        return static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    }
};

TEST_F(MeshVertexStorageTest, PublicSettersChangeOnlyTheSelectedVertexPayloadField) {
    RCKMesh mesh(Context);
    Payload expected[4];
    Fill(mesh, expected, 4);

    expected[1].Position = VxVector(-9, 7, 11);
    mesh.SetVertexPosition(1, &expected[1].Position);
    ExpectPayloads(mesh, expected, 4);

    expected[2].Normal = VxVector(0, -1, 0);
    mesh.SetVertexNormal(2, &expected[2].Normal);
    ExpectPayloads(mesh, expected, 4);

    expected[0].UV = Vx2DVector(-.75f, 8.25f);
    mesh.SetVertexTextureCoordinates(0, expected[0].UV.x, expected[0].UV.y, -1);
    ExpectPayloads(mesh, expected, 4);

    expected[3].Diffuse = 0x12345678u;
    mesh.SetVertexColor(3, expected[3].Diffuse);
    ExpectPayloads(mesh, expected, 4);

    expected[1].Specular = 0xFEDCBA98u;
    mesh.SetVertexSpecularColor(1, expected[1].Specular);
    ExpectPayloads(mesh, expected, 4);
}

TEST_F(MeshVertexStorageTest, WritablePublicStreamViewsKeepAllOtherFieldsAndVerticesIndependent) {
    RCKMesh mesh(Context);
    Payload expected[4];
    Fill(mesh, expected, 4);
    CKDWORD stride = 0;

    expected[2].Position = VxVector(9, -8, 7);
    void *pointer = mesh.GetPositionsPtr(&stride);
    WriteStream(pointer, stride, 2, expected[2].Position);
    ExpectPayloads(mesh, expected, 4);

    expected[0].Normal = VxVector(0, 0, -1);
    pointer = mesh.GetNormalsPtr(&stride);
    WriteStream(pointer, stride, 0, expected[0].Normal);
    ExpectPayloads(mesh, expected, 4);

    expected[3].UV = Vx2DVector(5.5f, -6.25f);
    pointer = mesh.GetTextureCoordinatesPtr(&stride, -1);
    WriteStream(pointer, stride, 3, expected[3].UV);
    ExpectPayloads(mesh, expected, 4);

    expected[1].Diffuse = 0x01020304u;
    pointer = mesh.GetColorsPtr(&stride);
    WriteStream(pointer, stride, 1, expected[1].Diffuse);
    ExpectPayloads(mesh, expected, 4);

    expected[2].Specular = 0xE1D2C3B4u;
    pointer = mesh.GetSpecularColorsPtr(&stride);
    WriteStream(pointer, stride, 2, expected[2].Specular);
    ExpectPayloads(mesh, expected, 4);
}

TEST_F(MeshVertexStorageTest, FullWidthFaceMasksAndMaterialChangesDoNotOverwriteEachOtherOrTheNormal) {
    RCKMesh mesh(Context);
    Payload expected[4];
    MakeTwoTriangles(mesh, expected);
    RCKMaterial *materials[16];
    for (int channel = 0; channel < 16; ++channel) {
        materials[channel] = Material();
        ASSERT_NE(nullptr, materials[channel]);
        ASSERT_EQ(channel, mesh.AddChannel(materials[channel], FALSE));
    }
    const VxVector normals[] = {VxVector(2, 3, 4), VxVector(-5, 6, -7)};
    CKDWORD stride = 0;
    CKBYTE *normalStream = mesh.GetFaceNormalsPtr(&stride);
    WriteStream(normalStream, stride, 0, normals[0]);
    WriteStream(normalStream, stride, 1, normals[1]);
    mesh.SetFaceMaterial(0, materials[2]);
    mesh.SetFaceMaterial(1, materials[9]);
    mesh.SetFaceChannelMask(0, 0x8001u);
    mesh.SetFaceChannelMask(1, 0x5A5Au);
    EXPECT_EQ(materials[2], mesh.GetFaceMaterial(0));
    EXPECT_EQ(materials[9], mesh.GetFaceMaterial(1));
    EXPECT_EQ(0x8001u, mesh.GetFaceChannelMask(0));
    EXPECT_EQ(0x5A5Au, mesh.GetFaceChannelMask(1));

    mesh.SetFaceMaterial(0, materials[15]);
    int face = 1;
    mesh.SetFaceMaterialEx(&face, 1, materials[4]);
    EXPECT_EQ(0x8001u, mesh.GetFaceChannelMask(0));
    EXPECT_EQ(0x5A5Au, mesh.GetFaceChannelMask(1));
    mesh.ChangeFaceChannelMask(0, 0x4000u, 0x0001u);
    EXPECT_EQ(0xC000u, mesh.GetFaceChannelMask(0));
    mesh.SetFaceChannelMask(1, 0xFFFFu);
    EXPECT_EQ(materials[15], mesh.GetFaceMaterial(0));
    EXPECT_EQ(materials[4], mesh.GetFaceMaterial(1));
    ExpectVector(mesh.GetFaceNormal(0), normals[0]);
    ExpectVector(mesh.GetFaceNormal(1), normals[1]);
    ExpectTriangle(mesh, 0, 0, 1, 2);
    ExpectTriangle(mesh, 1, 0, 3, 1);
    ExpectPayloads(mesh, expected, 4);
}

TEST_F(MeshVertexStorageTest, FaceNormalConsumerPreservesMaterialsMasksIndicesAndEveryVertexPayload) {
    RCKMesh mesh(Context);
    Payload expected[4];
    MakeTwoTriangles(mesh, expected);
    RCKMaterial first(Context), second(Context);
    ASSERT_EQ(0, mesh.AddChannel(&first, FALSE));
    ASSERT_EQ(1, mesh.AddChannel(&second, FALSE));
    mesh.SetFaceMaterial(0, &first);
    mesh.SetFaceMaterial(1, &second);
    mesh.SetFaceChannelMask(0, 1);
    mesh.SetFaceChannelMask(1, 2);
    mesh.BuildFaceNormals();

    ExpectVector(mesh.GetFaceNormal(0), VxVector(0, 0, 1));
    ExpectVector(mesh.GetFaceNormal(1), VxVector(0, 1, 0));
    EXPECT_EQ(&first, mesh.GetFaceMaterial(0));
    EXPECT_EQ(&second, mesh.GetFaceMaterial(1));
    EXPECT_EQ(1u, mesh.GetFaceChannelMask(0));
    EXPECT_EQ(2u, mesh.GetFaceChannelMask(1));
    ExpectTriangle(mesh, 0, 0, 1, 2);
    ExpectTriangle(mesh, 1, 0, 3, 1);
    ExpectPayloads(mesh, expected, 4);
}

TEST_F(MeshVertexStorageTest, VertexResizePreservesWholePayloadPrefixesAndInitializesOnlyNewSlots) {
    RCKMesh mesh(Context);
    Payload expected[4];
    Fill(mesh, expected, 4);
    ASSERT_TRUE(mesh.SetVertexCount(7));
    for (int index = 0; index < 4; ++index)
        ExpectPayload(mesh, index, expected[index]);
    const Payload empty = {VxVector(0, 0, 0), VxVector(0, 0, 0), Vx2DVector(0, 0), 0xFFFFFFFFu, 0};
    for (int index = 4; index < 7; ++index)
        ExpectPayload(mesh, index, empty);

    ASSERT_TRUE(mesh.SetVertexCount(2));
    for (int index = 0; index < 2; ++index)
        ExpectPayload(mesh, index, expected[index]);
    ASSERT_TRUE(mesh.SetVertexCount(5));
    for (int index = 0; index < 2; ++index)
        ExpectPayload(mesh, index, expected[index]);
    for (int index = 2; index < 5; ++index)
        ExpectPayload(mesh, index, empty);
}

TEST_F(MeshVertexStorageTest, FaceResizePreservesActiveMetadataAndKeepsTopologyInItsOwnArray) {
    RCKMesh mesh(Context);
    Payload expected[4];
    MakeTwoTriangles(mesh, expected);
    RCKMaterial first(Context), second(Context);
    ASSERT_EQ(0, mesh.AddChannel(&first, FALSE));
    ASSERT_EQ(1, mesh.AddChannel(&second, FALSE));
    mesh.SetFaceMaterial(0, &first);
    mesh.SetFaceMaterial(1, &second);
    mesh.SetFaceChannelMask(0, 1);
    mesh.SetFaceChannelMask(1, 2);
    mesh.BuildFaceNormals();
    ASSERT_TRUE(mesh.SetFaceCount(5));
    ExpectTriangle(mesh, 0, 0, 1, 2);
    ExpectTriangle(mesh, 1, 0, 3, 1);
    for (int face = 2; face < 5; ++face) {
        EXPECT_EQ(0xFFFFu, mesh.GetFaceChannelMask(face));
        ExpectTriangle(mesh, face, 0, 0, 0);
    }
    EXPECT_EQ(&second, mesh.GetFaceMaterial(1));
    EXPECT_EQ(2u, mesh.GetFaceChannelMask(1));
    ExpectVector(mesh.GetFaceNormal(1), VxVector(0, 1, 0));

    ASSERT_TRUE(mesh.SetFaceCount(1));
    ASSERT_TRUE(mesh.SetFaceCount(3));
    EXPECT_EQ(&first, mesh.GetFaceMaterial(0));
    EXPECT_EQ(1u, mesh.GetFaceChannelMask(0));
    ExpectVector(mesh.GetFaceNormal(0), VxVector(0, 0, 1));
    ExpectTriangle(mesh, 0, 0, 1, 2);
    for (int face = 1; face < 3; ++face) {
        EXPECT_EQ(0xFFFFu, mesh.GetFaceChannelMask(face));
        ExpectTriangle(mesh, face, 0, 0, 0);
    }
    ExpectPayloads(mesh, expected, 4);
}

TEST_F(MeshVertexStorageTest, MeshCopyKeepsSemanticFaceAndVertexStorageIndependentInBothDirections) {
    RCKMesh source(Context), copy(Context);
    Payload original[4];
    MakeTwoTriangles(source, original);
    RCKMaterial first(Context), second(Context);
    ASSERT_EQ(0, source.AddChannel(&first, FALSE));
    ASSERT_EQ(1, source.AddChannel(&second, FALSE));
    source.SetFaceMaterial(0, &first);
    source.SetFaceMaterial(1, &second);
    source.SetFaceChannelMask(0, 1);
    source.SetFaceChannelMask(1, 2);
    source.BuildFaceNormals();
    CKDependenciesContext dependencies(Context);
    ASSERT_EQ(CK_OK, copy.Copy(source, dependencies));
    ExpectPayloads(copy, original, 4);
    EXPECT_EQ(&first, copy.GetFaceMaterial(0));
    EXPECT_EQ(&second, copy.GetFaceMaterial(1));
    EXPECT_EQ(1u, copy.GetFaceChannelMask(0));
    EXPECT_EQ(2u, copy.GetFaceChannelMask(1));
    ExpectVector(copy.GetFaceNormal(0), VxVector(0, 0, 1));
    ExpectVector(copy.GetFaceNormal(1), VxVector(0, 1, 0));

    Payload changed = InitialPayload(9);
    SetPayload(source, 1, changed);
    source.SetFaceVertexIndex(0, 3, 2, 0);
    source.SetFaceMaterial(0, &second);
    source.SetFaceChannelMask(0, 3);
    CKDWORD stride = 0;
    void *normalStream = source.GetFaceNormalsPtr(&stride);
    WriteStream(normalStream, stride, 0, VxVector(7, 8, 9));
    ExpectPayloads(copy, original, 4);
    ExpectTriangle(copy, 0, 0, 1, 2);
    EXPECT_EQ(&first, copy.GetFaceMaterial(0));
    EXPECT_EQ(1u, copy.GetFaceChannelMask(0));
    ExpectVector(copy.GetFaceNormal(0), VxVector(0, 0, 1));

    Payload secondChange = InitialPayload(12);
    SetPayload(copy, 3, secondChange);
    copy.SetFaceVertexIndex(1, 2, 1, 0);
    copy.SetFaceMaterial(1, &first);
    copy.SetFaceChannelMask(1, 1);
    normalStream = copy.GetFaceNormalsPtr(&stride);
    WriteStream(normalStream, stride, 1, VxVector(-3, -4, -5));
    ExpectPayload(source, 3, original[3]);
    ExpectPayload(source, 1, changed);
    ExpectTriangle(source, 1, 0, 3, 1);
    EXPECT_EQ(&second, source.GetFaceMaterial(1));
    EXPECT_EQ(2u, source.GetFaceChannelMask(1));
    ExpectVector(source.GetFaceNormal(1), VxVector(0, 1, 0));
}
} // namespace
