#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class PatchMeshSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKPatchMesh *Mesh = nullptr;
    RCKMaterial *Material = nullptr;
    RCKMaterial *ChannelMaterial = nullptr;
    RCKMaterial *UnusedDefaultMaterial = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Mesh = static_cast<RCKPatchMesh *>(Context->CreateObject(CKCID_PATCHMESH));
        Material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        ChannelMaterial = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        UnusedDefaultMaterial = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        ASSERT_NE(nullptr, Mesh);
        ASSERT_NE(nullptr, Material);
        ASSERT_NE(nullptr, ChannelMaterial);
        ASSERT_NE(nullptr, UnusedDefaultMaterial);
    }

    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    // These are literal native records, independent of the PatchMesh Save path.
    // A planar bicubic quad spans x=0..2, y=0..3 and z=1.
    static void Points(VxVector (&points)[16], float z = 1.0f) {
        const float xy[16][2] = {
            {0, 0}, {2, 0}, {2, 3}, {0, 3},
            {2.0f / 3, 0}, {4.0f / 3, 0}, {2, 1}, {2, 2},
            {4.0f / 3, 3}, {2.0f / 3, 3}, {0, 2}, {0, 1},
            {2.0f / 3, 1}, {4.0f / 3, 1}, {4.0f / 3, 2}, {2.0f / 3, 2}
        };
        for (int i = 0; i < 16; ++i) points[i] = VxVector(xy[i][0], xy[i][1], z);
    }

    void Header(CKStateChunk &chunk, bool legacy, int iterations = 1, float z = 1.0f) {
        chunk.StartWrite();
        chunk.SetDataVersion(8);
        chunk.WriteIdentifier(legacy ? CK_STATESAVE_PATCHMESHDATA2 : CK_STATESAVE_PATCHMESHDATA3);
        chunk.WriteDword(CK_PATCHMESH_UPTODATE | CK_PATCHMESH_MATERIALSUPTODATE);
        if (legacy) chunk.WriteObject(Material);
        chunk.WriteInt(iterations);
        chunk.WriteInt(12);
        VxVector points[16]; Points(points, z);
        chunk.WriteDword(192); chunk.WriteDword(16);
        chunk.WriteBufferNoSize_LEndian(192, points);
    }

    static void IndexWords(short (&indices)[20]) {
        for (int i = 0; i < 4; ++i) indices[i] = static_cast<short>(i);
        for (int i = 0; i < 8; ++i) indices[4 + i] = static_cast<short>(i);
        for (int i = 0; i < 4; ++i) {
            indices[12 + i] = static_cast<short>(8 + i);
            indices[16 + i] = static_cast<short>(i);
        }
    }

    void Current(CKStateChunk &chunk, int iterations = 1, float z = 1.0f) {
        Header(chunk, false, iterations, z);
        chunk.StartObjectIDSequence(1); chunk.WriteObjectSequence(Material);
        chunk.WriteDword(4); chunk.WriteDword(0x10204080);
        short indices[20]; IndexWords(indices);
        chunk.WriteBufferNoSize_LEndian16(40, indices);
        const short edges[24] = {
            0, 0, 1, 1, 0, -1, 1, 2, 3, 2, 0, -1,
            2, 4, 5, 3, 0, -1, 3, 6, 7, 0, 0, -1
        };
        chunk.WriteDword(48); chunk.WriteDword(4);
        chunk.WriteBufferNoSize_LEndian16(48, const_cast<short *>(edges));
        chunk.StartObjectIDSequence(2);
        chunk.WriteObjectSequence(UnusedDefaultMaterial); chunk.WriteObjectSequence(ChannelMaterial);
        for (int channel = 0; channel < 2; ++channel) {
            chunk.WriteDword(channel ? VXBLEND_SRCALPHA : 0);
            chunk.WriteDword(channel ? VXBLEND_INVSRCCOLOR : 0);
            chunk.WriteDword(channel ? VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT : 0);
            short tv[4] = {0, 1, 2, 3};
            chunk.WriteDword(8); chunk.WriteDword(1);
            chunk.WriteBufferNoSize_LEndian16(8, tv);
            const float scale = channel ? 2.0f : 1.0f;
            float uv[8] = {0, 0, scale, 0, scale, scale, 0, scale};
            chunk.WriteDword(32); chunk.WriteDword(4);
            chunk.WriteBufferNoSize_LEndian(32, uv);
        }
        chunk.CloseChunk();
    }

    void Legacy(CKStateChunk &chunk, bool optional = false) {
        Header(chunk, true);
        CKDWORD patch[22] = {};
        patch[0] = 4;
        short indices[20]; IndexWords(indices);
        for (int i = 0; i < 16; ++i) patch[1 + i] = 0x12340000u | static_cast<CKWORD>(indices[i]);
        patch[17] = 0x76543210; // Unused legacy DWORD, before the four edge indices.
        for (int i = 0; i < 4; ++i) patch[18 + i] = 0x43210000u | static_cast<CKDWORD>(i);
        chunk.WriteDword(88); chunk.WriteDword(1); chunk.WriteBufferNoSize_LEndian(88, patch);
        CKDWORD edges[24] = {
            0, 0, 1, 1, 0, 0xFFFFFFFF, 1, 2, 3, 2, 0, 0xFFFFFFFF,
            2, 4, 5, 3, 0, 0xFFFFFFFF, 3, 6, 7, 0, 0, 0xFFFFFFFF
        };
        chunk.WriteDword(96); chunk.WriteDword(4); chunk.WriteBufferNoSize_LEndian(96, edges);
        CKDWORD tv[4] = {0x45670000, 0x45670001, 0x45670002, 0x45670003};
        chunk.WriteDword(16); chunk.WriteDword(1); chunk.WriteBufferNoSize_LEndian(16, tv);
        float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        chunk.WriteDword(32); chunk.WriteDword(4); chunk.WriteBufferNoSize_LEndian(32, uv);
        if (optional) {
            chunk.WriteIdentifier(CK_STATESAVE_PATCHMESHSMOOTH);
            chunk.WriteDword(4); chunk.WriteDword(1); chunk.WriteDword(0x81);
            chunk.WriteIdentifier(CK_STATESAVE_PATCHMESHMATERIALS);
            chunk.StartObjectIDSequence(1); chunk.WriteObjectSequence(ChannelMaterial);
        }
        chunk.CloseChunk();
    }

    void Consumer(float z = 1.0f, bool extraChannel = true) {
        ASSERT_EQ(9, Mesh->GetVertexCount()); ASSERT_EQ(8, Mesh->GetFaceCount());
        bool center = false;
        for (int i = 0; i < Mesh->GetVertexCount(); ++i) {
            VxVector point; Mesh->GetVertexPosition(i, &point);
            EXPECT_NEAR(z, point.z, 1e-5f);
            if (fabsf(point.x - 1) < 1e-5f && fabsf(point.y - 1.5f) < 1e-5f) center = true;
        }
        EXPECT_TRUE(center);
        for (int face = 0; face < Mesh->GetFaceCount(); ++face) {
            int a, b, c; Mesh->GetFaceVertexIndex(face, a, b, c);
            EXPECT_GE(a, 0); EXPECT_LT(a, 9); EXPECT_GE(b, 0); EXPECT_LT(b, 9); EXPECT_GE(c, 0); EXPECT_LT(c, 9);
        }
        float u = 0, v = 0; Mesh->GetVertexTextureCoordinates(2, &u, &v, -1);
        EXPECT_FLOAT_EQ(1, u); EXPECT_FLOAT_EQ(1, v);
        if (extraChannel) {
            ASSERT_EQ(1, Mesh->GetChannelCount()); EXPECT_EQ(ChannelMaterial, Mesh->GetChannelMaterial(0));
            EXPECT_EQ(VXBLEND_SRCALPHA, Mesh->GetChannelSourceBlend(0));
            EXPECT_EQ(VXBLEND_INVSRCCOLOR, Mesh->GetChannelDestBlend(0));
            EXPECT_EQ(VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT, Mesh->GetChannelFlags(0));
            Mesh->GetVertexTextureCoordinates(2, &u, &v, 0); EXPECT_FLOAT_EQ(2, u); EXPECT_FLOAT_EQ(2, v);
        }
    }
};

TEST_F(PatchMeshSerializationTest, CurrentLiteralRebuildsGeometryMaterialsAndIndependentChannels) {
    CKStateChunk chunk(CKCID_PATCHMESH, nullptr); Current(chunk);
    ASSERT_EQ(CK_OK, Mesh->Load(&chunk, nullptr));
    EXPECT_EQ(4, Mesh->GetVertCount()); EXPECT_EQ(12, Mesh->GetVecCount());
    EXPECT_EQ(0x10204080u, Mesh->GetPatchSM(0)); EXPECT_EQ(Material, Mesh->GetPatchMaterial(0));
    Consumer();
}

TEST_F(PatchMeshSerializationTest, LegacyDwordRecordsKeepLowWordsAndDefaultMaterialForActualTessellation) {
    CKStateChunk chunk(CKCID_PATCHMESH, nullptr); Legacy(chunk);
    ASSERT_EQ(CK_OK, Mesh->Load(&chunk, nullptr));
    EXPECT_EQ(0xFFFFFFFFu, Mesh->GetPatchSM(0)); EXPECT_EQ(Material, Mesh->GetPatchMaterial(0));
    CKPatch patch; Mesh->GetPatch(0, &patch);
    for (int i = 0; i < 4; ++i) { EXPECT_EQ(i, patch.edge[i]); EXPECT_EQ(i, patch.v[i]); }
    CKPatchEdge edge; Mesh->GetEdge(3, &edge);
    EXPECT_EQ(3, edge.v1); EXPECT_EQ(6, edge.vec12); EXPECT_EQ(7, edge.vec21); EXPECT_EQ(0, edge.v2);
    EXPECT_EQ(-1, edge.patch2); Consumer(1.0f, false);
}

TEST_F(PatchMeshSerializationTest, LegacyOptionalSmoothingAndMaterialSequencesOverrideDefaults) {
    CKStateChunk chunk(CKCID_PATCHMESH, nullptr); Legacy(chunk, true);
    ASSERT_EQ(CK_OK, Mesh->Load(&chunk, nullptr));
    EXPECT_EQ(0x81u, Mesh->GetPatchSM(0)); EXPECT_EQ(ChannelMaterial, Mesh->GetPatchMaterial(0));
    ASSERT_EQ(8, Mesh->GetFaceCount());
    for (int i = 0; i < 8; ++i) EXPECT_EQ(ChannelMaterial, Mesh->GetFaceMaterial(i));
    Consumer(1.0f, false);
}

TEST_F(PatchMeshSerializationTest, IdentifierSelectsPatchLayoutIndependentlyOfOuterDataVersion) {
    CKStateChunk chunk(CKCID_PATCHMESH, nullptr); Current(chunk); chunk.SetDataVersion(3);
    ASSERT_EQ(CK_OK, Mesh->Load(&chunk, nullptr)); Consumer();
}

TEST_F(PatchMeshSerializationTest, SaveEmitsIndependentNativeArrayStridesSequencesAndChannelMetadata) {
    CKStateChunk literal(CKCID_PATCHMESH, nullptr); Current(literal);
    ASSERT_EQ(CK_OK, Mesh->Load(&literal, nullptr));
    CKStateChunk *saved = Mesh->Save(nullptr, CK_STATESAVE_PATCHMESHONLY); ASSERT_NE(nullptr, saved);
    saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_PATCHMESHDATA3));
    EXPECT_EQ(Mesh->GetPatchFlags(), saved->ReadDword()); EXPECT_EQ(1, saved->ReadInt()); EXPECT_EQ(12, saved->ReadInt());
    EXPECT_EQ(192u, saved->ReadDword()); EXPECT_EQ(16u, saved->ReadDword());
    VxVector expected[16]; Points(expected);
    for (int i = 0; i < 16; ++i) {
        VxVector actual; saved->ReadVector(&actual);
        EXPECT_FLOAT_EQ(expected[i].x, actual.x); EXPECT_FLOAT_EQ(expected[i].y, actual.y); EXPECT_FLOAT_EQ(expected[i].z, actual.z);
    }
    ASSERT_EQ(1, saved->StartReadSequence()); EXPECT_EQ(Material->GetID(), saved->ReadObjectID());
    EXPECT_EQ(4u, saved->ReadDword()); EXPECT_EQ(0x10204080u, saved->ReadDword());
    short actualIndices[20], expectedIndices[20]; IndexWords(expectedIndices);
    saved->ReadAndFillBuffer_LEndian16(40, actualIndices);
    for (int i = 0; i < 20; ++i) EXPECT_EQ(expectedIndices[i], actualIndices[i]);
    EXPECT_EQ(48u, saved->ReadDword()); EXPECT_EQ(4u, saved->ReadDword());
    short edges[24]; saved->ReadAndFillBuffer_LEndian16(48, edges);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(-1, edges[i * 6 + 5]);
    ASSERT_EQ(2, saved->StartReadSequence());
    EXPECT_EQ(UnusedDefaultMaterial->GetID(), saved->ReadObjectID()); EXPECT_EQ(ChannelMaterial->GetID(), saved->ReadObjectID());
    for (int channel = 0; channel < 2; ++channel) {
        EXPECT_EQ(channel ? VXBLEND_SRCALPHA : 0, saved->ReadDword());
        EXPECT_EQ(channel ? VXBLEND_INVSRCCOLOR : 0, saved->ReadDword());
        EXPECT_EQ(channel ? VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT : 0, saved->ReadDword());
        EXPECT_EQ(8u, saved->ReadDword()); EXPECT_EQ(1u, saved->ReadDword());
        short tv[4]; saved->ReadAndFillBuffer_LEndian16(8, tv);
        for (int i = 0; i < 4; ++i) EXPECT_EQ(i, tv[i]);
        EXPECT_EQ(32u, saved->ReadDword()); EXPECT_EQ(4u, saved->ReadDword());
        const float scale = channel ? 2.0f : 1.0f;
        const float uv[8] = {0, 0, scale, 0, scale, scale, 0, scale};
        for (int i = 0; i < 8; ++i) EXPECT_FLOAT_EQ(uv[i], saved->ReadFloat());
    }
    DeleteCKStateChunk(saved);
}

TEST_F(PatchMeshSerializationTest, SaveWithoutPatchBitsRetainsOnlyBaseChunk) {
    CKStateChunk literal(CKCID_PATCHMESH, nullptr); Current(literal);
    ASSERT_EQ(CK_OK, Mesh->Load(&literal, nullptr));
    CKStateChunk *saved = Mesh->Save(nullptr, CK_STATESAVE_NAME); ASSERT_NE(nullptr, saved);
    saved->StartRead(); EXPECT_FALSE(saved->SeekIdentifier(CK_STATESAVE_PATCHMESHDATA3)); DeleteCKStateChunk(saved);
}

TEST_F(PatchMeshSerializationTest, FileSaveIncludesPatchPayloadEvenWithoutPatchSelectionBits) {
    CKStateChunk literal(CKCID_PATCHMESH, nullptr); Current(literal);
    ASSERT_EQ(CK_OK, Mesh->Load(&literal, nullptr)); CKFile file(Context);
    CKStateChunk *saved = Mesh->Save(&file, 0); ASSERT_NE(nullptr, saved);
    saved->StartRead(); EXPECT_TRUE(saved->SeekIdentifier(CK_STATESAVE_PATCHMESHDATA3)); DeleteCKStateChunk(saved);
}

TEST_F(PatchMeshSerializationTest, CurrentLoadVerticesMovesSurfaceWithoutReplacingTopologyOrIteration) {
    CKStateChunk initial(CKCID_PATCHMESH, nullptr); Current(initial);
    ASSERT_EQ(CK_OK, Mesh->Load(&initial, nullptr));
    CKStateChunk moved(CKCID_PATCHMESH, nullptr); Header(moved, false, 7, 5); moved.CloseChunk(); moved.StartRead();
    Mesh->LoadVertices(&moved);
    EXPECT_EQ(1, Mesh->GetIterationCount()); EXPECT_EQ(1, Mesh->GetPatchCount()); EXPECT_EQ(4, Mesh->GetEdgeCount());
    EXPECT_FALSE(Mesh->GetPatchFlags() & CK_PATCHMESH_UPTODATE);
    EXPECT_TRUE(Mesh->GetPatchFlags() & CK_PATCHMESH_BUILDNORMALS);
    EXPECT_EQ(Material, Mesh->GetPatchMaterial(0)); Mesh->BuildRenderMesh(); Consumer(5);
}

TEST_F(PatchMeshSerializationTest, LegacyLoadVerticesSkipsLegacyMaterialAndPreservesExistingPatchState) {
    CKStateChunk initial(CKCID_PATCHMESH, nullptr); Current(initial);
    ASSERT_EQ(CK_OK, Mesh->Load(&initial, nullptr));
    CKStateChunk moved(CKCID_PATCHMESH, nullptr); Header(moved, true, 6, -2); moved.CloseChunk(); moved.StartRead();
    Mesh->LoadVertices(&moved); EXPECT_EQ(1, Mesh->GetIterationCount()); EXPECT_EQ(0x10204080u, Mesh->GetPatchSM(0));
    EXPECT_EQ(Material, Mesh->GetPatchMaterial(0)); Mesh->BuildRenderMesh(); Consumer(-2);
}

TEST_F(PatchMeshSerializationTest, LoadVerticesResizesCombinedControlPointStorageForStandaloneUse) {
    Mesh->SetVertVecCount(1, 1);
    CKStateChunk moved(CKCID_PATCHMESH, nullptr); Header(moved, false, 7, 5); moved.CloseChunk(); moved.StartRead();
    Mesh->LoadVertices(&moved); ASSERT_EQ(4, Mesh->GetVertCount()); ASSERT_EQ(12, Mesh->GetVecCount());
    ASSERT_NE(nullptr, Mesh->GetVerts()); EXPECT_EQ(Mesh->GetVerts() + 4, Mesh->GetVecs());
    VxVector p; Mesh->GetVert(2, &p); EXPECT_FLOAT_EQ(2, p.x); EXPECT_FLOAT_EQ(3, p.y); EXPECT_FLOAT_EQ(5, p.z);
    Mesh->GetVec(11, &p); EXPECT_FLOAT_EQ(2.0f / 3, p.x); EXPECT_FLOAT_EQ(2, p.y); EXPECT_FLOAT_EQ(5, p.z);
}

TEST_F(PatchMeshSerializationTest, PreSaveCollectsPatchAndExtraChannelMaterialsWithRequestedFlags) {
    CKStateChunk literal(CKCID_PATCHMESH, nullptr); Current(literal);
    ASSERT_EQ(CK_OK, Mesh->Load(&literal, nullptr)); CKFile file(Context); file.m_IndexByClassId.Resize(128);
    Mesh->PreSave(&file, CK_STATESAVE_NAME);
    bool patch = false, channel = false;
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        const CKFileObject &object = file.m_FileObjects[i];
        if (object.Object == Material->GetID()) { patch = true; EXPECT_EQ(CK_STATESAVE_NAME, object.SaveFlags); }
        if (object.Object == ChannelMaterial->GetID()) { channel = true; EXPECT_EQ(CK_STATESAVE_NAME, object.SaveFlags); }
        EXPECT_NE(UnusedDefaultMaterial->GetID(), object.Object);
    }
    EXPECT_TRUE(patch); EXPECT_TRUE(channel);
}

TEST_F(PatchMeshSerializationTest, NullLoadReturnsInvalidParameterWithoutChangingExistingSurface) {
    CKStateChunk initial(CKCID_PATCHMESH, nullptr); Current(initial); ASSERT_EQ(CK_OK, Mesh->Load(&initial, nullptr));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Mesh->Load(nullptr, nullptr)); Consumer();
}
} // namespace
