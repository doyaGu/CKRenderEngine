#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializedMesh : public RCKMesh {
public:
    explicit SerializedMesh(CKContext *context) : RCKMesh(context) {}
    void PM() {
        DestroyPM(); m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = 2; m_ProgressiveMesh->m_MorphEnabled = TRUE;
        m_ProgressiveMesh->m_MorphStep = 7; m_ProgressiveMesh->m_Data.Resize(3);
        m_ProgressiveMesh->m_Data[0] = 11; m_ProgressiveMesh->m_Data[1] = 17; m_ProgressiveMesh->m_Data[2] = 23;
    }
    CKProgressiveMesh *RawPM() { return m_ProgressiveMesh; }
    CKWORD Mask(int i) const { return m_Faces[i].m_ChannelMask; }
    void Mask(int i, CKWORD value) { m_Faces[i].m_ChannelMask = value; }
    Vx2DVector *ChannelUV(int i) { return m_MaterialChannels[i].m_UVs; }
};
class MeshDependencyMap : public CKDependenciesContext {
public:
    explicit MeshDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject &from, CKObject &to) { m_MapID.Insert(from.GetID(), to.GetID()); }
};
class MeshSerializationAlignmentTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Begin(CKStateChunk &chunk, int version = 9) { chunk.StartWrite(); chunk.SetDataVersion(version); }
    static CKERROR Load(RCKMesh &mesh, CKStateChunk &chunk) { chunk.CloseChunk(); return mesh.Load(&chunk, nullptr); }
    static void Triangle(RCKMesh &mesh, int faces = 1) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        VxVector points[] = {VxVector(0, 0, 0), VxVector(2, 0, 0), VxVector(0, 3, 0)};
        for (int i = 0; i < 3; ++i) mesh.SetVertexPosition(i, &points[i]);
        for (int i = 0; i < faces; ++i) mesh.SetFaceVertexIndex(i, 0, 1, 2);
    }
    static void Options(CKDependencies &options, CKDWORD own) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_MESH] = own;
    }
    static void Vector(CKStateChunk &chunk, float x, float y, float z) {
        chunk.WriteFloat(x); chunk.WriteFloat(y); chunk.WriteFloat(z);
    }
    static void ModernVertices(CKStateChunk &chunk, CKDWORD flags = 0) {
        // Independent 2.1 version-9 planar wire: two vertices, inclusive word count.
        chunk.WriteIdentifier(0x20000); chunk.WriteInt(2); chunk.WriteDword(flags);
        int words = 1 + ((flags & 16) ? 0 : 6) + ((flags & 1) ? 1 : 2) + ((flags & 2) ? 1 : 2)
            + ((flags & 4) ? 0 : 6) + ((flags & 8) ? 2 : 4);
        chunk.WriteInt(words);
        if (!(flags & 16)) { Vector(chunk, 2, 3, 4); Vector(chunk, 5, 6, 7); }
        chunk.WriteDword(0x12345678); if (!(flags & 1)) chunk.WriteDword(0x87654321);
        chunk.WriteDword(0xabcdef01); if (!(flags & 2)) chunk.WriteDword(0x10fedcba);
        if (!(flags & 4)) { Vector(chunk, 0, 0, 2); Vector(chunk, 0, 3, 0); }
        chunk.WriteFloat(.25f); chunk.WriteFloat(.5f);
        if (!(flags & 8)) { chunk.WriteFloat(.75f); chunk.WriteFloat(1); }
    }
};
}

TEST_F(MeshSerializationAlignmentTest, NullLoadPreservesGeometryChannelsAndPM) {
    SerializedMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh); ASSERT_EQ(0, mesh.AddChannel(&material)); mesh.PM();
    EXPECT_EQ(CKERR_INVALIDPARAMETER, mesh.Load(nullptr, nullptr)); EXPECT_EQ(3, mesh.GetVertexCount()); EXPECT_EQ(1, mesh.GetFaceCount());
    EXPECT_EQ(&material, mesh.GetChannelMaterial(0)); EXPECT_NE(nullptr, mesh.RawPM());
}
TEST_F(MeshSerializationAlignmentTest, VersionNineLiteralPlanarVerticesPreserveDistinctPositionsColorsNormalsAndUV) {
    SerializedMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); ModernVertices(chunk);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); ASSERT_EQ(2, mesh.GetVertexCount()); VxVector p, n;
    mesh.GetVertexPosition(1, &p); EXPECT_FLOAT_EQ(5, p.x); EXPECT_FLOAT_EQ(6, p.y); EXPECT_FLOAT_EQ(7, p.z);
    mesh.GetVertexNormal(1, &n); EXPECT_FLOAT_EQ(0, n.x); EXPECT_FLOAT_EQ(3, n.y); EXPECT_FLOAT_EQ(0, n.z);
    EXPECT_EQ(0x12345678u, mesh.GetVertexColor(0)); EXPECT_EQ(0x87654321u, mesh.GetVertexColor(1));
    EXPECT_EQ(0xabcdef01u, mesh.GetVertexSpecularColor(0)); EXPECT_EQ(0x10fedcbau, mesh.GetVertexSpecularColor(1));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(1, &u, &v, -1); EXPECT_FLOAT_EQ(.75f, u); EXPECT_FLOAT_EQ(1, v);
}
TEST_F(MeshSerializationAlignmentTest, VersionNineConstantColorAndUVFlagsReplicateIndependentWireValues) {
    SerializedMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); ModernVertices(chunk, 11);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(0x12345678u, mesh.GetVertexColor(1)); EXPECT_EQ(0xabcdef01u, mesh.GetVertexSpecularColor(1));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(1, &u, &v, -1); EXPECT_FLOAT_EQ(.25f, u); EXPECT_FLOAT_EQ(.5f, v);
}
TEST_F(MeshSerializationAlignmentTest, VersionTenUsesVersionNineVertexLayoutWithoutAssumingCorpusIsNativeTwoPointOne) {
    SerializedMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk, 10); ModernVertices(chunk);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(0x87654321u, mesh.GetVertexColor(1)); VxVector p; mesh.GetVertexPosition(1, &p); EXPECT_FLOAT_EQ(7, p.z);
}
TEST_F(MeshSerializationAlignmentTest, LegacyVersionEightPrelitPlanarLayoutHasNoInclusiveBufferSizeWord) {
    SerializedMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk, 8);
    chunk.WriteIdentifier(0x2000); chunk.WriteDword(VXMESH_VISIBLE | VXMESH_PRELITMODE);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(2); chunk.WriteDword(3 | 8);
    Vector(chunk, 2, 3, 4); Vector(chunk, 5, 6, 7); chunk.WriteDword(0x12345678); chunk.WriteDword(0xabcdef01);
    chunk.WriteFloat(.25f); chunk.WriteFloat(.5f); ASSERT_EQ(CK_OK, Load(mesh, chunk));
    VxVector p; mesh.GetVertexPosition(1, &p); EXPECT_FLOAT_EQ(5, p.x); EXPECT_FLOAT_EQ(7, p.z);
    EXPECT_EQ(0x12345678u, mesh.GetVertexColor(1)); EXPECT_EQ(0xabcdef01u, mesh.GetVertexSpecularColor(1));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(1, &u, &v, -1); EXPECT_FLOAT_EQ(.25f, u); EXPECT_FLOAT_EQ(.5f, v);
}
TEST_F(MeshSerializationAlignmentTest, LegacyVersionFourLitInterleavesPositionNormalAndUVPerVertex) {
    SerializedMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk, 4);
    chunk.WriteIdentifier(0x2000); chunk.WriteDword(VXMESH_VISIBLE);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(2);
    Vector(chunk, 2, 3, 4); Vector(chunk, 0, 0, 2); chunk.WriteFloat(.25f); chunk.WriteFloat(.5f);
    Vector(chunk, 5, 6, 7); Vector(chunk, 0, 3, 0); chunk.WriteFloat(.75f); chunk.WriteFloat(1);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); VxVector p, n; mesh.GetVertexPosition(1, &p); mesh.GetVertexNormal(1, &n);
    EXPECT_FLOAT_EQ(5, p.x); EXPECT_FLOAT_EQ(7, p.z); EXPECT_FLOAT_EQ(3, n.y);
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(1, &u, &v, -1); EXPECT_FLOAT_EQ(.75f, u); EXPECT_FLOAT_EQ(1, v);
}
TEST_F(MeshSerializationAlignmentTest, LegacyVersionZeroReadsSizePrefixedVectorsAndWideLineIndices) {
    SerializedMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk, 0);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(2);
    for (int i = 0; i < 2; ++i) {
        chunk.WriteInt(12); Vector(chunk, 2 + i, 3 + i, 4 + i); chunk.WriteInt(12); Vector(chunk, 0, 0, 2);
        chunk.WriteDword(0x12345678 + i); chunk.WriteDword(0xabcdef01 + i); chunk.WriteFloat(.25f * i); chunk.WriteFloat(.5f * i);
    }
    chunk.WriteIdentifier(0x40000); chunk.WriteInt(1); chunk.WriteInt(1); chunk.WriteInt(0);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); VxVector p; mesh.GetVertexPosition(1, &p); EXPECT_FLOAT_EQ(3, p.x); EXPECT_FLOAT_EQ(5, p.z);
    EXPECT_EQ(0x12345679u, mesh.GetVertexColor(1)); int a = -1, b = -1; mesh.GetLine(0, &a, &b); EXPECT_EQ(1, a); EXPECT_EQ(0, b);
}
TEST_F(MeshSerializationAlignmentTest, ModernLiteralFacesResolveMaterialGroupOrderAndPackedWordIndices) {
    SerializedMesh mesh(Context); RCKMaterial material(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); ModernVertices(chunk);
    chunk.WriteIdentifier(0x100000); chunk.WriteInt(2); chunk.WriteObject(nullptr); chunk.WriteInt(0); chunk.WriteObject(&material); chunk.WriteInt(0);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(1); chunk.WriteDwordAsWords(0x00010000); chunk.WriteDwordAsWords(0x00010000);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(&material, mesh.GetFaceMaterial(0)); int a = -1, b = -1, c = -1; mesh.GetFaceVertexIndex(0, a, b, c);
    EXPECT_EQ(0, a); EXPECT_EQ(1, b); EXPECT_EQ(0, c);
}
TEST_F(MeshSerializationAlignmentTest, LiteralConstantAndArrayWeightSectionsKeepIndependentValues) {
    SerializedMesh mesh(Context); CKStateChunk constant(CKCID_MESH, nullptr); Begin(constant);
    constant.WriteIdentifier(0x80000); constant.WriteInt(3); constant.WriteFloat(.375f); ASSERT_EQ(CK_OK, Load(mesh, constant));
    ASSERT_EQ(3, mesh.GetVertexWeightsCount()); for (int i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(.375f, mesh.GetVertexWeight(i));
    CKStateChunk varied(CKCID_MESH, nullptr); Begin(varied); varied.WriteIdentifier(0x80000); varied.WriteInt(3); varied.WriteInt(12);
    varied.WriteFloat(.125f); varied.WriteFloat(.5f); varied.WriteFloat(.875f); varied.WriteFloat(.125f); ASSERT_EQ(CK_OK, Load(mesh, varied));
    EXPECT_FLOAT_EQ(.125f, mesh.GetVertexWeight(0)); EXPECT_FLOAT_EQ(.5f, mesh.GetVertexWeight(1)); EXPECT_FLOAT_EQ(.875f, mesh.GetVertexWeight(2));
}
TEST_F(MeshSerializationAlignmentTest, OddTargetFaceMaskTruncationPreservesEveryAvailableFace) {
    SerializedMesh mesh(Context); Triangle(mesh, 3); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x8000); chunk.WriteInt(5); chunk.WriteDwordAsWords(0x22221111); chunk.WriteDwordAsWords(0x44443333); chunk.WriteWord(0x5555);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(0x1111, mesh.Mask(0)); EXPECT_EQ(0x2222, mesh.Mask(1)); EXPECT_EQ(0x3333, mesh.Mask(2));
}
TEST_F(MeshSerializationAlignmentTest, OddLiteralFaceMasksReadFinalWordAfterCompletePairs) {
    SerializedMesh mesh(Context); Triangle(mesh, 3); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x8000); chunk.WriteInt(3); chunk.WriteDwordAsWords(0x22221111); chunk.WriteWord(0x3333);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); EXPECT_EQ(0x1111, mesh.Mask(0)); EXPECT_EQ(0x2222, mesh.Mask(1)); EXPECT_EQ(0x3333, mesh.Mask(2));
}
TEST_F(MeshSerializationAlignmentTest, SaveWritesIndependentPackedFacesLinesWeightsAndOddMasks) {
    SerializedMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh, 3); mesh.SetLineCount(1); mesh.SetLine(0, 2, 1);
    ASSERT_EQ(0, mesh.AddChannel(&material)); mesh.Mask(0, 0x1234); mesh.Mask(1, 0x5678); mesh.Mask(2, 0x9abc);
    mesh.SetVertexWeightsCount(3); mesh.SetVertexWeight(0, .125f); mesh.SetVertexWeight(1, .5f); mesh.SetVertexWeight(2, .875f);
    CKStateChunk *chunk = mesh.Save(nullptr, CK_STATESAVE_MESHONLY); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x10000)); EXPECT_EQ(3, chunk->ReadInt()); EXPECT_EQ(0x00010000u, chunk->ReadDwordAsWords()); EXPECT_EQ(2u, chunk->ReadDwordAsWords());
    ASSERT_TRUE(chunk->SeekIdentifier(0x40000)); EXPECT_EQ(1, chunk->ReadInt()); EXPECT_EQ(4, chunk->ReadInt()); EXPECT_EQ(0x00010002u, chunk->ReadDwordAsWords());
    ASSERT_TRUE(chunk->SeekIdentifier(0x80000)); EXPECT_EQ(3, chunk->ReadInt()); EXPECT_EQ(12, chunk->ReadInt()); EXPECT_FLOAT_EQ(.125f, chunk->ReadFloat());
    EXPECT_FLOAT_EQ(.5f, chunk->ReadFloat()); EXPECT_FLOAT_EQ(.875f, chunk->ReadFloat()); EXPECT_FLOAT_EQ(.125f, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x8000)); EXPECT_EQ(3, chunk->ReadInt()); EXPECT_EQ(0x56781234u, chunk->ReadDwordAsWords()); EXPECT_EQ(0x9abc, chunk->ReadWord()); delete chunk;
}
TEST_F(MeshSerializationAlignmentTest, SaveWithoutFileOrMeshFlagsOnlyWritesInheritedState) {
    SerializedMesh mesh(Context); Triangle(mesh); CKStateChunk *chunk = mesh.Save(nullptr, 0); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    EXPECT_FALSE(chunk->SeekIdentifier(0x2000)); EXPECT_FALSE(chunk->SeekIdentifier(0x20000)); EXPECT_FALSE(chunk->SeekIdentifier(0x10000)); delete chunk;
}
TEST_F(MeshSerializationAlignmentTest, PreSaveSchedulesDistinctFaceAndChannelMaterialsWithRequestedFlags) {
    SerializedMesh mesh(Context); RCKMaterial face(Context), channel(Context); Triangle(mesh, 2);
    mesh.SetFaceMaterial(0, &face); mesh.SetFaceMaterial(1, &face); ASSERT_EQ(0, mesh.AddChannel(&face)); ASSERT_EQ(1, mesh.AddChannel(&channel));
    CKFile file(Context); file.m_IndexByClassId.Resize(CKGetClassCount()); mesh.PreSave(&file, 0x1234); mesh.PreSave(&file, 0x1234);
    ASSERT_EQ(2, file.m_FileObjects.Size()); EXPECT_EQ(face.GetID(), file.m_FileObjects[0].Object); EXPECT_EQ(channel.GetID(), file.m_FileObjects[1].Object);
    EXPECT_EQ(0x1234u, file.m_FileObjects[0].SaveFlags); EXPECT_EQ(0x1234u, file.m_FileObjects[1].SaveFlags);
}
TEST_F(MeshSerializationAlignmentTest, PreSaveSkipsNullDefaultGroupAndClearedChannelMaterial) {
    SerializedMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh); ASSERT_EQ(0, mesh.AddChannel(&material)); mesh.SetChannelMaterial(0, nullptr);
    CKFile file(Context); file.m_IndexByClassId.Resize(CKGetClassCount()); mesh.PreSave(&file, 0);
    EXPECT_EQ(0, file.m_FileObjects.Size()); EXPECT_EQ(1, mesh.GetChannelCount());
}
TEST_F(MeshSerializationAlignmentTest, LiteralProgressiveMeshHeaderAndDataReplaceOldAndAbsentSectionRemovesIt) {
    SerializedMesh mesh(Context); mesh.PM(); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); chunk.WriteIdentifier(0x800000);
    chunk.WriteInt(5); chunk.WriteInt(FALSE); chunk.WriteInt(9); chunk.WriteDword(31); chunk.WriteDword(37);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); ASSERT_NE(nullptr, mesh.RawPM()); EXPECT_EQ(5, mesh.RawPM()->m_VertexCount);
    EXPECT_FALSE(mesh.RawPM()->m_MorphEnabled); EXPECT_EQ(9, mesh.GetPMGeoMorphStep()); ASSERT_EQ(2, mesh.RawPM()->m_Data.Size()); EXPECT_EQ(31u, mesh.RawPM()->m_Data[0]); EXPECT_EQ(37u, mesh.RawPM()->m_Data[1]);
    CKStateChunk empty(CKCID_MESH, nullptr); Begin(empty); ASSERT_EQ(CK_OK, Load(mesh, empty)); EXPECT_EQ(nullptr, mesh.RawPM());
}
TEST_F(MeshSerializationAlignmentTest, CopyPreservesDisabledFaceChannelMembershipAndOwnsUVWeightsAndPM) {
    SerializedMesh source(Context), target(Context); RCKMaterial material(Context); Triangle(source); ASSERT_EQ(0, source.AddChannel(&material)); source.Mask(0, 0);
    source.SetVertexTextureCoordinates(1, .25f, .75f, 0); source.SetVertexWeightsCount(3); source.SetVertexWeight(1, .375f); source.PM();
    CKDependenciesContext dependencies(Context); ASSERT_EQ(CK_OK, target.Copy(source, dependencies));
    EXPECT_EQ(0, target.Mask(0)); ASSERT_EQ(1, target.GetChannelCount()); EXPECT_NE(source.ChannelUV(0), target.ChannelUV(0));
    EXPECT_NE(source.GetVertexWeightsPtr(), target.GetVertexWeightsPtr()); ASSERT_NE(nullptr, target.RawPM()); EXPECT_NE(source.RawPM(), target.RawPM());
    source.SetVertexTextureCoordinates(1, .5f, 1, 0); source.SetVertexWeight(1, .875f); source.RawPM()->m_Data[1] = 101;
    float u = 0, v = 0; target.GetVertexTextureCoordinates(1, &u, &v, 0); EXPECT_FLOAT_EQ(.25f, u); EXPECT_FLOAT_EQ(.75f, v);
    EXPECT_FLOAT_EQ(.375f, target.GetVertexWeight(1)); EXPECT_EQ(17u, target.RawPM()->m_Data[1]);
}
TEST_F(MeshSerializationAlignmentTest, CopyPreservesNullMaterialChannelOrdinalAndFollowingChannelData) {
    SerializedMesh source(Context), target(Context); RCKMaterial first(Context), second(Context); Triangle(source);
    ASSERT_EQ(0, source.AddChannel(&first)); ASSERT_EQ(1, source.AddChannel(&second)); source.SetChannelMaterial(0, nullptr);
    source.SetVertexTextureCoordinates(0, .125f, .375f, 0); source.SetVertexTextureCoordinates(0, .625f, .875f, 1);
    source.SetChannelSourceBlend(0, VXBLEND_SRCALPHA); source.SetChannelDestBlend(1, VXBLEND_INVSRCALPHA); source.Mask(0, 2);
    CKDependenciesContext dependencies(Context); ASSERT_EQ(CK_OK, target.Copy(source, dependencies)); ASSERT_EQ(2, target.GetChannelCount());
    EXPECT_EQ(nullptr, target.GetChannelMaterial(0)); EXPECT_EQ(&second, target.GetChannelMaterial(1)); EXPECT_EQ(2, target.Mask(0));
    EXPECT_EQ(VXBLEND_SRCALPHA, target.GetChannelSourceBlend(0)); EXPECT_EQ(VXBLEND_INVSRCALPHA, target.GetChannelDestBlend(1));
    float u = 0, v = 0; target.GetVertexTextureCoordinates(0, &u, &v, 0); EXPECT_FLOAT_EQ(.125f, u); EXPECT_FLOAT_EQ(.375f, v);
    target.GetVertexTextureCoordinates(0, &u, &v, 1); EXPECT_FLOAT_EQ(.625f, u); EXPECT_FLOAT_EQ(.875f, v);
}
TEST_F(MeshSerializationAlignmentTest, LiteralLoadPreservesNullFirstChannelAndItsFaceMaskOrdinal) {
    SerializedMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x4000); chunk.WriteInt(2);
    for (int i = 0; i < 2; ++i) {
        chunk.WriteObject(i ? &material : nullptr); chunk.WriteDword(VXCHANNEL_ACTIVE);
        chunk.WriteDword(i ? VXBLEND_ONE : VXBLEND_SRCALPHA); chunk.WriteDword(i ? VXBLEND_INVSRCALPHA : VXBLEND_ZERO);
        chunk.WriteInt(3); for (int vertex = 0; vertex < 3; ++vertex) { chunk.WriteFloat(.125f + .5f * i); chunk.WriteFloat(.375f + .5f * i); }
    }
    chunk.WriteIdentifier(0x8000); chunk.WriteInt(1); chunk.WriteWord(2);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); ASSERT_EQ(2, mesh.GetChannelCount()); EXPECT_EQ(nullptr, mesh.GetChannelMaterial(0)); EXPECT_EQ(&material, mesh.GetChannelMaterial(1));
    EXPECT_EQ(2, mesh.Mask(0)); EXPECT_EQ(VXBLEND_SRCALPHA, mesh.GetChannelSourceBlend(0)); EXPECT_EQ(VXBLEND_INVSRCALPHA, mesh.GetChannelDestBlend(1));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(0, &u, &v, 0); EXPECT_FLOAT_EQ(.125f, u); EXPECT_FLOAT_EQ(.375f, v);
    mesh.GetVertexTextureCoordinates(0, &u, &v, 1); EXPECT_FLOAT_EQ(.625f, u); EXPECT_FLOAT_EQ(.875f, v);
}
TEST_F(MeshSerializationAlignmentTest, LiteralLoadPreservesDuplicateMaterialChannelSlotsAndDistinctUVs) {
    SerializedMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x4000); chunk.WriteInt(2);
    for (int i = 0; i < 2; ++i) {
        chunk.WriteObject(&material); chunk.WriteDword(VXCHANNEL_ACTIVE | (i ? VXCHANNEL_NOTLIT : 0));
        chunk.WriteDword(i ? VXBLEND_ONE : VXBLEND_SRCALPHA); chunk.WriteDword(VXBLEND_ZERO);
        chunk.WriteInt(3); for (int vertex = 0; vertex < 3; ++vertex) { chunk.WriteFloat(.125f + .5f * i); chunk.WriteFloat(.375f + .5f * i); }
    }
    chunk.WriteIdentifier(0x8000); chunk.WriteInt(1); chunk.WriteWord(1);
    ASSERT_EQ(CK_OK, Load(mesh, chunk)); ASSERT_EQ(2, mesh.GetChannelCount()); EXPECT_EQ(&material, mesh.GetChannelMaterial(0)); EXPECT_EQ(&material, mesh.GetChannelMaterial(1));
    EXPECT_EQ(1, mesh.Mask(0)); EXPECT_TRUE(mesh.IsChannelLit(0)); EXPECT_FALSE(mesh.IsChannelLit(1)); EXPECT_EQ(VXBLEND_SRCALPHA, mesh.GetChannelSourceBlend(0)); EXPECT_EQ(VXBLEND_ONE, mesh.GetChannelSourceBlend(1));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(0, &u, &v, 0); EXPECT_FLOAT_EQ(.125f, u); EXPECT_FLOAT_EQ(.375f, v);
    mesh.GetVertexTextureCoordinates(0, &u, &v, 1); EXPECT_FLOAT_EQ(.625f, u); EXPECT_FLOAT_EQ(.875f, v);
}
TEST_F(MeshSerializationAlignmentTest, SelfCopyKeepsTopologyMaterialChannelWeightsAndPMStorage) {
    SerializedMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh); ASSERT_EQ(0, mesh.AddChannel(&material)); mesh.Mask(0, 0);
    mesh.SetFaceMaterial(0, &material); mesh.SetVertexWeightsCount(3); mesh.SetVertexWeight(1, .375f); mesh.PM();
    Vx2DVector *uv = mesh.ChannelUV(0); CKProgressiveMesh *pm = mesh.RawPM(); CKDependenciesContext dependencies(Context);
    ASSERT_EQ(CK_OK, mesh.Copy(mesh, dependencies)); EXPECT_EQ(3, mesh.GetVertexCount()); EXPECT_EQ(1, mesh.GetFaceCount());
    ASSERT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(&material, mesh.GetChannelMaterial(0)); EXPECT_EQ(&material, mesh.GetFaceMaterial(0));
    EXPECT_EQ(0, mesh.Mask(0)); EXPECT_EQ(uv, mesh.ChannelUV(0)); EXPECT_EQ(pm, mesh.RawPM()); EXPECT_FLOAT_EQ(.375f, mesh.GetVertexWeight(1));
}
TEST_F(MeshSerializationAlignmentTest, CopyEmptySourceClearsTargetOwnedGeometryLinesWeightsChannelsAndPM) {
    SerializedMesh source(Context), target(Context); RCKMaterial material(Context); Triangle(target); target.SetLineCount(1); target.SetLine(0, 0, 1);
    target.SetVertexWeightsCount(3); ASSERT_EQ(0, target.AddChannel(&material)); target.PM(); CKDependenciesContext dependencies(Context);
    ASSERT_EQ(CK_OK, target.Copy(source, dependencies)); EXPECT_EQ(0, target.GetVertexCount()); EXPECT_EQ(0, target.GetFaceCount()); EXPECT_EQ(0, target.GetLineCount());
    EXPECT_EQ(0, target.GetVertexWeightsCount()); EXPECT_EQ(0, target.GetChannelCount()); EXPECT_EQ(nullptr, target.RawPM()); EXPECT_EQ(&material, Context->GetObject(material.GetID()));
}
TEST_F(MeshSerializationAlignmentTest, PrepareMaterialBitIncludesFaceAndChannelReferencesAndNoBitSkipsBoth) {
    SerializedMesh mesh(Context); RCKMaterial face(Context), channel(Context); Triangle(mesh); mesh.SetFaceMaterial(0, &face); ASSERT_EQ(0, mesh.AddChannel(&channel));
    for (int bits = 0; bits <= 1; ++bits) {
        CKDependencies options; Options(options, bits); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, mesh.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(mesh.GetID()));
        EXPECT_EQ(bits != 0, !!dependencies.IsDependenciesHere(face.GetID())); EXPECT_EQ(bits != 0, !!dependencies.IsDependenciesHere(channel.GetID())); dependencies.StopDependencies();
    }
}
TEST_F(MeshSerializationAlignmentTest, RemapMaterialBitChangesGroupsAndChannelsWhileDisabledBitKeepsPointers) {
    SerializedMesh mesh(Context); RCKMaterial face(Context), channel(Context), replacement(Context); Triangle(mesh);
    mesh.SetFaceMaterial(0, &face); ASSERT_EQ(0, mesh.AddChannel(&channel));
    CKDependencies options; Options(options, 0); MeshDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(face, replacement); dependencies.Map(channel, replacement); ASSERT_EQ(CK_OK, mesh.RemapDependencies(dependencies));
    EXPECT_EQ(&face, mesh.GetFaceMaterial(0)); EXPECT_EQ(&channel, mesh.GetChannelMaterial(0)); options[CKCID_MESH] = 1;
    ASSERT_EQ(CK_OK, mesh.RemapDependencies(dependencies)); EXPECT_EQ(&replacement, mesh.GetFaceMaterial(0)); EXPECT_EQ(&replacement, mesh.GetChannelMaterial(0)); dependencies.StopDependencies();
}
TEST_F(MeshSerializationAlignmentTest, CheckPreDeletionClearsReferencedMaterialsAndPreservesChannelGeometry) {
    SerializedMesh mesh(Context); RCKMaterial doomed(Context), retained(Context); Triangle(mesh, 2);
    mesh.SetFaceMaterial(0, &doomed); mesh.SetFaceMaterial(1, &retained); ASSERT_EQ(0, mesh.AddChannel(&doomed)); mesh.SetVertexTextureCoordinates(0, .25f, .75f, 0);
    doomed.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); mesh.CheckPreDeletion(); EXPECT_EQ(nullptr, mesh.GetFaceMaterial(0)); EXPECT_EQ(&retained, mesh.GetFaceMaterial(1));
    ASSERT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(nullptr, mesh.GetChannelMaterial(0)); float u = 0, v = 0; mesh.GetVertexTextureCoordinates(0, &u, &v, 0);
    EXPECT_FLOAT_EQ(.25f, u); EXPECT_FLOAT_EQ(.75f, v); doomed.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(MeshSerializationAlignmentTest, IsObjectUsedAndSceneDependenciesFollowNativeFaceMaterialContract) {
    SerializedMesh mesh(Context); RCKMaterial face(Context), channel(Context); Triangle(mesh); mesh.SetFaceMaterial(0, &face); ASSERT_EQ(0, mesh.AddChannel(&channel));
    EXPECT_TRUE(mesh.IsObjectUsed(&face, CKCID_MATERIAL)); EXPECT_FALSE(mesh.IsObjectUsed(&channel, CKCID_MATERIAL));
    CKScene *scene = static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE)); ASSERT_NE(nullptr, scene);
    mesh.AddToScene(scene, FALSE); EXPECT_TRUE(scene->IsObjectHere(&mesh)); EXPECT_FALSE(scene->IsObjectHere(&face)); mesh.RemoveFromScene(scene, FALSE);
    mesh.AddToScene(scene, TRUE); EXPECT_TRUE(scene->IsObjectHere(&mesh)); EXPECT_TRUE(scene->IsObjectHere(&face)); EXPECT_FALSE(scene->IsObjectHere(&channel));
    mesh.RemoveFromScene(scene, TRUE); EXPECT_FALSE(scene->IsObjectHere(&mesh)); EXPECT_FALSE(scene->IsObjectHere(&face));
}
