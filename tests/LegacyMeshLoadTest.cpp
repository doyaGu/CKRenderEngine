#include <gtest/gtest.h>

// Test the actual legacy parser and public Load path, including private helper
// dispatch, without an exported test hook or a second parser implementation.
#include "../src/CKMesh.cpp"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

class LegacyMeshProbe : public RCKMesh {
public:
    explicit LegacyMeshProbe(CKContext *context) : RCKMesh(context) {}
    int NormalBuilds = 0, FaceNormalBuilds = 0, Invalidations = 0, BatchCalls = 0;
    int BatchCounts[16] = {}, BatchFirst[16] = {};
    CKMaterial *BatchMaterials[16] = {};
    bool InvalidatedBeforeNormals = false, RebuiltBeforeLines = false;

    void ResetTrace() {
        NormalBuilds = FaceNormalBuilds = Invalidations = BatchCalls = 0;
        InvalidatedBeforeNormals = RebuiltBeforeLines = false;
    }
    void UnOptimize() override {
        ++Invalidations;
        RCKMesh::UnOptimize();
    }
    void BuildNormals() override {
        ++NormalBuilds;
        InvalidatedBeforeNormals = !(m_Flags & (VXMESH_OPTIMIZED | VXMESH_TRANSPARENCYUPTODATE));
        RCKMesh::BuildNormals();
    }
    void BuildFaceNormals() override {
        ++FaceNormalBuilds;
        InvalidatedBeforeNormals = !(m_Flags & (VXMESH_OPTIMIZED | VXMESH_TRANSPARENCYUPTODATE));
        RCKMesh::BuildFaceNormals();
    }
    CKBOOL SetLineCount(int count) override {
        RebuiltBeforeLines = NormalBuilds + FaceNormalBuilds != 0;
        return RCKMesh::SetLineCount(count);
    }
    void SetFaceMaterialEx(int *indices, int count, CKMaterial *material) override {
        if (BatchCalls < 16) {
            BatchCounts[BatchCalls] = count;
            BatchFirst[BatchCalls] = count ? indices[0] : -1;
            BatchMaterials[BatchCalls] = material;
        }
        ++BatchCalls;
        RCKMesh::SetFaceMaterialEx(indices, count, material);
    }
    void CacheGeometry() { m_Flags |= VXMESH_OPTIMIZED | VXMESH_TRANSPARENCYUPTODATE; }
    bool HasRawMaterial(CKMaterial *material) const {
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i]->m_Material == material) return true;
        return false;
    }
};

class LegacyMeshLoadTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Vector(CKStateChunk &chunk, float x, float y, float z) {
        chunk.WriteFloat(x); chunk.WriteFloat(y); chunk.WriteFloat(z);
    }
    static void Square(LegacyMeshProbe &mesh) {
        ASSERT_TRUE(mesh.SetVertexCount(4));
        const VxVector positions[] = {VxVector(100, 200, 10), VxVector(102, 200, 10),
            VxVector(102, 203, 10), VxVector(100, 203, 10)};
        for (int i = 0; i < 4; ++i) {
            VxVector position = positions[i], normal(20.0f + i, 30.0f + i, 40.0f + i);
            mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexColor(i, 0xc0c00000u + i); mesh.SetVertexSpecularColor(i, 0xd0d00000u + i);
        }
        mesh.ResetTrace();
    }
    static void Face(CKStateChunk &chunk, int a, int b, int c, CKWORD unused = 0) {
        chunk.WriteDwordAsWords(static_cast<CKDWORD>(a) | (static_cast<CKDWORD>(b) << 16));
        chunk.WriteDwordAsWords(static_cast<CKDWORD>(c) | (static_cast<CKDWORD>(unused) << 16));
    }
    static void ExpectFace(RCKMesh &mesh, int face, int a, int b, int c, CKMaterial *material) {
        int actualA = -1, actualB = -1, actualC = -1;
        mesh.GetFaceVertexIndex(face, actualA, actualB, actualC);
        EXPECT_EQ(a, actualA); EXPECT_EQ(b, actualB); EXPECT_EQ(c, actualC);
        EXPECT_EQ(material, mesh.GetFaceMaterial(face));
    }
    static CKERROR Load(LegacyMeshProbe &mesh, CKStateChunk &chunk) {
        chunk.CloseChunk();
        return mesh.Load(&chunk, nullptr);
    }
    static void QuadFaces(CKStateChunk &chunk) {
        chunk.WriteIdentifier(0x10000); chunk.WriteInt(2); chunk.WriteInt(1);
        chunk.WriteObject(nullptr); chunk.WriteInt(2);
        Face(chunk, 0, 1, 2, 0xabcd); Face(chunk, 0, 2, 3, 0xfedc);
    }
    static void Lines(CKStateChunk &chunk) {
        chunk.WriteIdentifier(0x40000); chunk.WriteInt(2); chunk.WriteInt(8);
        chunk.WriteDwordAsWords(0x00010003); chunk.WriteDwordAsWords(0x00020000);
    }
    void CompressionMatrix(CKBOOL prelit) {
        const int versions[] = {5, 8};
        for (int version : versions) {
            for (CKDWORD flags = 0; flags < 32; ++flags) {
                SCOPED_TRACE(version);
                SCOPED_TRACE(flags);
                SCOPED_TRACE(prelit);
                LegacyMeshProbe mesh(Context); Square(mesh);
                CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
                chunk.WriteIdentifier(0x2000); chunk.WriteDword(VXMESH_VISIBLE | (prelit ? VXMESH_PRELITMODE : 0));
                chunk.WriteIdentifier(0x20000); chunk.WriteInt(4); chunk.WriteDword(flags);
                // Independent pre-v9 grammar: no inclusive buffer-size word.
                if (!(flags & 16)) {
                    Vector(chunk, 0, 0, 0); Vector(chunk, 2, 0, 0);
                    Vector(chunk, 2, 3, 0); Vector(chunk, 0, 3, 0);
                }
                if (prelit) {
                    for (int i = 0; i < ((flags & 1) ? 1 : 4); ++i) chunk.WriteDword(0x10203040u + 0x01010101u * i);
                    for (int i = 0; i < ((flags & 2) ? 1 : 4); ++i) chunk.WriteDword(0x50607080u + 0x00010101u * i);
                } else if (!(flags & 4)) {
                    for (int i = 0; i < 4; ++i) Vector(chunk, 4.0f + i, -2.0f - i, 8.0f + i);
                }
                for (int i = 0; i < ((flags & 8) ? 1 : 4); ++i) {
                    chunk.WriteFloat(.125f + .25f * i); chunk.WriteFloat(.875f - .125f * i);
                }
                QuadFaces(chunk); Lines(chunk);
                ASSERT_EQ(CK_OK, Load(mesh, chunk));
                ASSERT_EQ(4, mesh.GetVertexCount()); ASSERT_EQ(2, mesh.GetFaceCount());
                EXPECT_EQ((flags & 4) ? 1 : 0, mesh.NormalBuilds);
                EXPECT_EQ((flags & 4) ? 0 : 1, mesh.FaceNormalBuilds);
                EXPECT_TRUE(mesh.InvalidatedBeforeNormals); EXPECT_TRUE(mesh.RebuiltBeforeLines);
                const float xs[] = {0, 2, 2, 0}, ys[] = {0, 0, 3, 3};
                for (int i = 0; i < 4; ++i) {
                    VxVector position, normal;
                    mesh.GetVertexPosition(i, &position); mesh.GetVertexNormal(i, &normal);
                    EXPECT_FLOAT_EQ(xs[i] + ((flags & 16) ? 100.0f : 0.0f), position.x);
                    EXPECT_FLOAT_EQ(ys[i] + ((flags & 16) ? 200.0f : 0.0f), position.y);
                    EXPECT_FLOAT_EQ((flags & 16) ? 10.0f : 0.0f, position.z);
                    if (flags & 4) {
                        EXPECT_FLOAT_EQ(0, normal.x); EXPECT_FLOAT_EQ(0, normal.y); EXPECT_FLOAT_EQ(1, normal.z);
                    } else {
                        EXPECT_FLOAT_EQ((prelit ? 20.0f : 4.0f) + i, normal.x);
                        EXPECT_FLOAT_EQ(prelit ? 30.0f + i : -2.0f - i, normal.y);
                        EXPECT_FLOAT_EQ((prelit ? 40.0f : 8.0f) + i, normal.z);
                    }
                    EXPECT_EQ(prelit ? 0x10203040u + 0x01010101u * ((flags & 1) ? 0 : i) : 0xc0c00000u + i, mesh.GetVertexColor(i));
                    EXPECT_EQ(prelit ? 0x50607080u + 0x00010101u * ((flags & 2) ? 0 : i) : 0xd0d00000u + i, mesh.GetVertexSpecularColor(i));
                    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(i, &u, &v, -1);
                    EXPECT_FLOAT_EQ(.125f + .25f * ((flags & 8) ? 0 : i), u);
                    EXPECT_FLOAT_EQ(.875f - .125f * ((flags & 8) ? 0 : i), v);
                }
                CKDWORD stride = 0; CKBYTE *faceNormals = mesh.GetFaceNormalsPtr(&stride);
                ASSERT_NE(nullptr, faceNormals);
                for (int i = 0; i < 2; ++i) {
                    VxVector *normal = reinterpret_cast<VxVector *>(faceNormals + i * stride);
                    EXPECT_FLOAT_EQ(0, normal->x); EXPECT_FLOAT_EQ(0, normal->y); EXPECT_FLOAT_EQ(1, normal->z);
                }
                ExpectFace(mesh, 0, 0, 1, 2, nullptr); ExpectFace(mesh, 1, 0, 2, 3, nullptr);
                int a = -1, b = -1; mesh.GetLine(0, &a, &b); EXPECT_EQ(3, a); EXPECT_EQ(1, b);
                mesh.GetLine(1, &a, &b); EXPECT_EQ(0, a); EXPECT_EQ(2, b);
            }
        }
    }
};

TEST_F(LegacyMeshLoadTest, GroupedFacesUseConsecutiveRangesAndKeepDuplicateAndNullMaterials) {
    const int versions[] = {1, 4, 5, 8};
    for (int version : versions) {
        SCOPED_TRACE(version);
        LegacyMeshProbe mesh(Context); Square(mesh); RCKMaterial first(Context), second(Context);
        CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        chunk.WriteIdentifier(0x10000); chunk.WriteInt(5); chunk.WriteInt(4);
        chunk.WriteObject(&first); chunk.WriteInt(2); Face(chunk, 0, 1, 2, 0x1234); Face(chunk, 0, 2, 3, 0xabcd);
        chunk.WriteObject(&second); chunk.WriteInt(1); Face(chunk, 1, 2, 3, 0xffff);
        chunk.WriteObject(&first); chunk.WriteInt(1); Face(chunk, 3, 2, 0, 0x5678);
        chunk.WriteObject(nullptr); chunk.WriteInt(1); Face(chunk, 2, 1, 0, 0x9012);
        Lines(chunk); ASSERT_EQ(CK_OK, Load(mesh, chunk)); ASSERT_EQ(5, mesh.GetFaceCount());
        ExpectFace(mesh, 0, 0, 1, 2, &first); ExpectFace(mesh, 1, 0, 2, 3, &first);
        ExpectFace(mesh, 2, 1, 2, 3, &second); ExpectFace(mesh, 3, 3, 2, 0, &first); ExpectFace(mesh, 4, 2, 1, 0, nullptr);
        ASSERT_EQ(4, mesh.BatchCalls); const int counts[] = {2, 1, 1, 1}, starts[] = {0, 2, 3, 4};
        for (int i = 0; i < 4; ++i) { EXPECT_EQ(counts[i], mesh.BatchCounts[i]); EXPECT_EQ(starts[i], mesh.BatchFirst[i]); }
        EXPECT_TRUE(mesh.RebuiltBeforeLines);
    }
}

TEST_F(LegacyMeshLoadTest, EmptyGroupsRegisterTheirMaterialWithoutAdvancingTheFaceOrdinal) {
    LegacyMeshProbe mesh(Context); Square(mesh); RCKMaterial empty(Context), used(Context);
    CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(8);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(2); chunk.WriteInt(4);
    chunk.WriteObject(&empty); chunk.WriteInt(0);
    chunk.WriteObject(&used); chunk.WriteInt(1); Face(chunk, 0, 1, 2);
    chunk.WriteObject(nullptr); chunk.WriteInt(0);
    chunk.WriteObject(&used); chunk.WriteInt(1); Face(chunk, 0, 2, 3);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); ASSERT_EQ(4, mesh.BatchCalls);
    EXPECT_EQ(0, mesh.BatchCounts[0]); EXPECT_EQ(0, mesh.BatchFirst[1]);
    EXPECT_EQ(0, mesh.BatchCounts[2]); EXPECT_EQ(1, mesh.BatchFirst[3]);
    EXPECT_TRUE(mesh.HasRawMaterial(&empty)); EXPECT_TRUE(mesh.HasRawMaterial(&used));
    ExpectFace(mesh, 0, 0, 1, 2, &used); ExpectFace(mesh, 1, 0, 2, 3, &used);
}

TEST_F(LegacyMeshLoadTest, ReloadRetainsExistingGroupsAndOverwritesTheLoadedFaceAssignments) {
    LegacyMeshProbe mesh(Context); Square(mesh); RCKMaterial previous(Context), replacement(Context);
    mesh.SetFaceCount(2); mesh.SetFaceMaterial(0, &previous); mesh.SetFaceMaterial(1, &previous); mesh.ResetTrace();
    CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(2); chunk.WriteInt(2);
    chunk.WriteObject(nullptr); chunk.WriteInt(1); Face(chunk, 3, 2, 1);
    chunk.WriteObject(&replacement); chunk.WriteInt(1); Face(chunk, 0, 2, 3);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_TRUE(mesh.HasRawMaterial(&previous));
    ExpectFace(mesh, 0, 3, 2, 1, nullptr); ExpectFace(mesh, 1, 0, 2, 3, &replacement);
}

TEST_F(LegacyMeshLoadTest, PackedIndicesAreUnsignedAndTheFourthWordIsNotAnIndexOrMaterial) {
    LegacyMeshProbe mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(65536));
    CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(1);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(1); chunk.WriteInt(1);
    chunk.WriteObject(nullptr); chunk.WriteInt(1); Face(chunk, 32768, 65535, 9, 0xbeef);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); ExpectFace(mesh, 0, 32768, 65535, 9, nullptr);
}

TEST_F(LegacyMeshLoadTest, ZeroFaceCountPreservesExistingFacesButStillInvalidatesAndBuildsNormals) {
    LegacyMeshProbe mesh(Context); Square(mesh); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.CacheGeometry(); mesh.ResetTrace();
    CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(8);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(0);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(1, mesh.GetFaceCount()); EXPECT_EQ(0, mesh.BatchCalls);
    EXPECT_GE(mesh.Invalidations, 1); EXPECT_TRUE(mesh.InvalidatedBeforeNormals); EXPECT_EQ(1, mesh.FaceNormalBuilds);
    EXPECT_EQ(0u, mesh.GetFlags() & (VXMESH_OPTIMIZED | VXMESH_TRANSPARENCYUPTODATE));
    ExpectFace(mesh, 0, 0, 1, 2, nullptr);
}

TEST_F(LegacyMeshLoadTest, MissingSectionsRetainTopologyAndLinesAndInvalidateExistingCaches) {
    LegacyMeshProbe mesh(Context); Square(mesh); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 2, 3);
    mesh.SetLineCount(1); mesh.SetLine(0, 3, 1); mesh.CacheGeometry(); mesh.ResetTrace();
    CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(4, mesh.GetVertexCount()); EXPECT_EQ(1, mesh.GetFaceCount());
    EXPECT_EQ(1, mesh.GetLineCount()); int a = -1, b = -1; mesh.GetLine(0, &a, &b); EXPECT_EQ(3, a); EXPECT_EQ(1, b);
    EXPECT_EQ(1, mesh.FaceNormalBuilds); EXPECT_EQ(0, mesh.NormalBuilds); EXPECT_TRUE(mesh.InvalidatedBeforeNormals);
    ExpectFace(mesh, 0, 0, 2, 3, nullptr);
}

TEST_F(LegacyMeshLoadTest, VersionZeroReadsWideFaceIndicesObsoleteDataAndPerFaceMaterials) {
    LegacyMeshProbe mesh(Context); Square(mesh); RCKMaterial material(Context);
    CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(0);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(2);
    chunk.WriteInt(0); chunk.WriteInt(1); chunk.WriteInt(2); chunk.WriteDword(0x89abcdef); chunk.WriteObject(&material);
    chunk.WriteInt(0); chunk.WriteInt(2); chunk.WriteInt(3); chunk.WriteDword(0x12345678); chunk.WriteObject(nullptr);
    chunk.WriteIdentifier(0x40000); chunk.WriteInt(1); chunk.WriteInt(3); chunk.WriteInt(1);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(0, mesh.BatchCalls);
    ExpectFace(mesh, 0, 0, 1, 2, &material); ExpectFace(mesh, 1, 0, 2, 3, nullptr);
    int a = -1, b = -1; mesh.GetLine(0, &a, &b); EXPECT_EQ(3, a); EXPECT_EQ(1, b);
    EXPECT_TRUE(mesh.RebuiltBeforeLines);
}

TEST_F(LegacyMeshLoadTest, LitVersionsFiveAndEightCoverEveryPackedAttributeFlagCombination) {
    CompressionMatrix(FALSE);
}

TEST_F(LegacyMeshLoadTest, PrelitVersionsFiveAndEightCoverEveryPackedAttributeFlagCombination) {
    CompressionMatrix(TRUE);
}

TEST_F(LegacyMeshLoadTest, VersionsOneAndFourReadPrelitInterleavedFieldsWithoutAFlagWord) {
    const int versions[] = {1, 4};
    for (int version : versions) {
        SCOPED_TRACE(version); LegacyMeshProbe mesh(Context); Square(mesh);
        CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        chunk.WriteIdentifier(0x2000); chunk.WriteDword(VXMESH_VISIBLE | VXMESH_PRELITMODE);
        chunk.WriteIdentifier(0x20000); chunk.WriteInt(4);
        for (int i = 0; i < 4; ++i) {
            Vector(chunk, 2.0f + i, 7.0f - i, 11.0f + i);
            chunk.WriteDword(0x01020304 + i); chunk.WriteDword(0x50607080 + i);
            chunk.WriteFloat(.125f * i); chunk.WriteFloat(.25f * i);
        }
        QuadFaces(chunk); ASSERT_EQ(CK_OK, Load(mesh, chunk));
        for (int i = 0; i < 4; ++i) {
            VxVector position, normal; mesh.GetVertexPosition(i, &position); mesh.GetVertexNormal(i, &normal);
            EXPECT_FLOAT_EQ(2.0f + i, position.x); EXPECT_FLOAT_EQ(7.0f - i, position.y); EXPECT_FLOAT_EQ(11.0f + i, position.z);
            EXPECT_FLOAT_EQ(20.0f + i, normal.x); EXPECT_EQ(0x01020304u + i, mesh.GetVertexColor(i));
            EXPECT_EQ(0x50607080u + i, mesh.GetVertexSpecularColor(i));
            float u = 0, v = 0; mesh.GetVertexTextureCoordinates(i, &u, &v, -1);
            EXPECT_FLOAT_EQ(.125f * i, u); EXPECT_FLOAT_EQ(.25f * i, v);
        }
    }
}

TEST_F(LegacyMeshLoadTest, SkippedPositionsRetainOldVerticesAndUseInitializationForNewVertices) {
    LegacyMeshProbe mesh(Context); Square(mesh); mesh.SetVertexCount(2); mesh.ResetTrace();
    CKStateChunk chunk(CKCID_MESH, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(4); chunk.WriteDword(16 | 8);
    for (int i = 0; i < 4; ++i) Vector(chunk, 4.0f + i, 5.0f + i, 6.0f + i);
    chunk.WriteFloat(.25f); chunk.WriteFloat(.75f); QuadFaces(chunk);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); VxVector position;
    mesh.GetVertexPosition(0, &position); EXPECT_FLOAT_EQ(100, position.x); EXPECT_FLOAT_EQ(200, position.y);
    mesh.GetVertexPosition(1, &position); EXPECT_FLOAT_EQ(102, position.x);
    for (int i = 2; i < 4; ++i) {
        mesh.GetVertexPosition(i, &position); EXPECT_FLOAT_EQ(0, position.x); EXPECT_FLOAT_EQ(0, position.y); EXPECT_FLOAT_EQ(0, position.z);
        VxVector normal; mesh.GetVertexNormal(i, &normal); EXPECT_FLOAT_EQ(4.0f + i, normal.x);
    }
}

TEST_F(LegacyMeshLoadTest, ALoadWithoutVerticesDoesNotReuseThePreviousNormalGenerationFlag) {
    LegacyMeshProbe mesh(Context); Square(mesh);
    CKStateChunk first(CKCID_MESH, nullptr); first.StartWrite(); first.SetDataVersion(8);
    first.WriteIdentifier(0x20000); first.WriteInt(4); first.WriteDword(16 | 4 | 8);
    first.WriteFloat(.25f); first.WriteFloat(.75f); QuadFaces(first);
    ASSERT_EQ(CK_OK, Load(mesh, first)); EXPECT_EQ(1, mesh.NormalBuilds);
    mesh.ResetTrace(); CKStateChunk second(CKCID_MESH, nullptr); second.StartWrite(); second.SetDataVersion(8);
    QuadFaces(second); ASSERT_EQ(CK_OK, Load(mesh, second));
    EXPECT_EQ(0, mesh.NormalBuilds); EXPECT_EQ(1, mesh.FaceNormalBuilds);
}
