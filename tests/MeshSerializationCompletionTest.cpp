#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "MeshHardwareObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CompletionMesh : public RCKMesh {
public:
    CK_CLASSID ReportedClass = CKCID_MESH;
    int FullLoads = 0;
    CKFile *LastFile = nullptr;
    explicit CompletionMesh(CKContext *context) : RCKMesh(context) {}
    CK_CLASSID GetClassID() override { return ReportedClass; }
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override {
        ++FullLoads; LastFile = file; return RCKMesh::Load(chunk, file);
    }
    CKWORD Mask(int face) const { return m_Faces[face].m_ChannelMask; }
    Vx2DVector *ChannelData(int channel) { return m_MaterialChannels[channel].m_UVs; }
    CKProgressiveMesh *PMData() { return m_ProgressiveMesh; }
    CKMaterialGroup *UploadGroup() { return m_MaterialGroups[0]; }
    void PM() {
        DestroyPM(); m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = 2; m_ProgressiveMesh->m_MorphEnabled = FALSE;
        m_ProgressiveMesh->m_MorphStep = 7; m_ProgressiveMesh->m_Data.Resize(2);
        m_ProgressiveMesh->m_Data[0] = 17; m_ProgressiveMesh->m_Data[1] = 29;
    }
};

class MeshSerializationCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Begin(CKStateChunk &chunk, int version = 9) {
        chunk.StartWrite(); chunk.SetDataVersion(version);
    }
    static int Words(int count, CKDWORD flags) {
        return 1 + ((flags & 16) ? 0 : 3 * count) + ((flags & 1) ? 1 : count) +
            ((flags & 2) ? 1 : count) + ((flags & 4) ? 0 : 3 * count) + ((flags & 8) ? 2 : 2 * count);
    }
    static void Vector(CKStateChunk &chunk, float x, float y, float z) {
        chunk.WriteFloat(x); chunk.WriteFloat(y); chunk.WriteFloat(z);
    }
    static void LiteralVertices(CKStateChunk &chunk, CKDWORD flags, int count = 3) {
        chunk.WriteIdentifier(0x20000); chunk.WriteInt(count);
        if (!count) return;
        chunk.WriteDword(flags); chunk.WriteInt(Words(count, flags));
        if (!(flags & 16))
            for (int i = 0; i < count; ++i) Vector(chunk, 10.0f + i, 20.0f + i, 30.0f + i);
        for (int i = 0; i < ((flags & 1) ? 1 : count); ++i) chunk.WriteDword(0xFF001100u + i);
        for (int i = 0; i < ((flags & 2) ? 1 : count); ++i) chunk.WriteDword(0x22003300u + i);
        if (!(flags & 4))
            for (int i = 0; i < count; ++i) Vector(chunk, 2.0f + i, 3.0f + i, 4.0f + i);
        for (int i = 0; i < ((flags & 8) ? 1 : count); ++i) {
            chunk.WriteFloat(.125f + i); chunk.WriteFloat(.375f + i);
        }
    }
    static void Seed(CompletionMesh &mesh, int count = 3) {
        ASSERT_TRUE(mesh.SetVertexCount(count));
        for (int i = 0; i < count; ++i) {
            VxVector p(-10.0f - i, -20.0f - i, -30.0f - i), n(-2.0f - i, -3.0f - i, -4.0f - i);
            mesh.SetVertexPosition(i, &p); mesh.SetVertexNormal(i, &n);
        }
    }
    static void Triangle(CompletionMesh &mesh) {
        Seed(mesh); ASSERT_TRUE(mesh.SetFaceCount(1)); mesh.SetFaceVertexIndex(0, 0, 1, 2);
        VxVector points[3] = {VxVector(0.0f, 0.0f, 0.0f), VxVector(2.0f, 0.0f, 0.0f), VxVector(0.0f, 3.0f, 0.0f)};
        for (int i = 0; i < 3; ++i) mesh.SetVertexPosition(i, &points[i]);
    }
    static void Channel(CKStateChunk &chunk, CKMaterial *material, CKDWORD flags, float u) {
        chunk.WriteObject(material); chunk.WriteDword(flags);
        chunk.WriteDword(VXBLEND_SRCALPHA); chunk.WriteDword(VXBLEND_INVSRCALPHA);
        chunk.WriteInt(3);
        for (int i = 0; i < 3; ++i) { chunk.WriteFloat(u + i); chunk.WriteFloat(.875f); }
    }
    static void ExpectLiteral(CompletionMesh &mesh, CKDWORD flags) {
        ASSERT_EQ(3, mesh.GetVertexCount());
        for (int i = 0; i < 3; ++i) {
            VxVector p, n; mesh.GetVertexPosition(i, &p); mesh.GetVertexNormal(i, &n);
            EXPECT_FLOAT_EQ((flags & 16) ? -10.0f - i : 10.0f + i, p.x);
            EXPECT_FLOAT_EQ((flags & 16) ? -20.0f - i : 20.0f + i, p.y);
            EXPECT_FLOAT_EQ((flags & 16) ? -30.0f - i : 30.0f + i, p.z);
            EXPECT_FLOAT_EQ((flags & 4) ? -2.0f - i : 2.0f + i, n.x);
            EXPECT_FLOAT_EQ((flags & 4) ? -3.0f - i : 3.0f + i, n.y);
            EXPECT_FLOAT_EQ((flags & 4) ? -4.0f - i : 4.0f + i, n.z);
            EXPECT_EQ(0xFF001100u + ((flags & 1) ? 0 : i), mesh.GetVertexColor(i));
            EXPECT_EQ(0x22003300u + ((flags & 2) ? 0 : i), mesh.GetVertexSpecularColor(i));
            float u = 0, v = 0; mesh.GetVertexTextureCoordinates(i, &u, &v, -1);
            EXPECT_FLOAT_EQ(.125f + ((flags & 8) ? 0 : i), u);
            EXPECT_FLOAT_EQ(.375f + ((flags & 8) ? 0 : i), v);
        }
    }
};

TEST_F(MeshSerializationCompletionTest, InternalReaderExecutesAllThirtyTwoNativePlanarFlagCombinations) {
    for (CKDWORD flags = 0; flags < 32; ++flags) {
        SCOPED_TRACE(flags); CompletionMesh mesh(Context); Seed(mesh);
        CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); LiteralVertices(chunk, flags);
        chunk.WriteIdentifier(0x123456); chunk.WriteDword(0xFEDCBA98); chunk.CloseChunk(); chunk.StartRead();
        CKDWORD actualFlags = 0xDEADBEEF; EXPECT_EQ(0, mesh.ILoadVertices(&chunk, &actualFlags));
        EXPECT_EQ(flags, actualFlags); ExpectLiteral(mesh, flags);
        // A second read sees the same wire. Endian conversion must be undone.
        EXPECT_EQ(0, mesh.ILoadVertices(&chunk, &actualFlags)); ExpectLiteral(mesh, flags);
        ASSERT_TRUE(chunk.SeekIdentifier(0x123456)); EXPECT_EQ(0xFEDCBA98u, chunk.ReadDword());
    }
}
TEST_F(MeshSerializationCompletionTest, InternalReaderLegacyAbsentAndZeroSectionsPreserveTheirDistinctContracts) {
    CompletionMesh mesh(Context); Seed(mesh); CKDWORD flags = 0x12345678;
    CKStateChunk legacy(CKCID_MESH, nullptr); Begin(legacy, 8); legacy.CloseChunk(); legacy.StartRead();
    EXPECT_EQ(-1, mesh.ILoadVertices(&legacy, &flags)); EXPECT_EQ(0x12345678u, flags); EXPECT_EQ(3, mesh.GetVertexCount());
    CKStateChunk absent(CKCID_MESH, nullptr); Begin(absent); absent.WriteIdentifier(0x7777); absent.WriteInt(5);
    absent.CloseChunk(); absent.StartRead(); EXPECT_EQ(0, mesh.ILoadVertices(&absent, &flags));
    EXPECT_EQ(0x12345678u, flags); EXPECT_EQ(3, mesh.GetVertexCount());
    CKStateChunk zero(CKCID_MESH, nullptr); Begin(zero); LiteralVertices(zero, 0, 0); zero.CloseChunk(); zero.StartRead();
    EXPECT_EQ(0, mesh.ILoadVertices(&zero, &flags)); EXPECT_EQ(0x12345678u, flags); EXPECT_EQ(0, mesh.GetVertexCount());
}
TEST_F(MeshSerializationCompletionTest, PublicModernVertexLoadKeepsChannelsPMAndOtherFullLoadState) {
    CompletionMesh mesh(Context); RCKMaterial material(Context); Seed(mesh); ASSERT_EQ(0, mesh.AddChannel(&material)); mesh.PM();
    CKProgressiveMesh *pm = mesh.PMData(); Vx2DVector *uv = mesh.ChannelData(0);
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); LiteralVertices(chunk, 0);
    chunk.CloseChunk(); chunk.StartRead(); mesh.LoadVertices(&chunk);
    EXPECT_EQ(0, mesh.FullLoads); EXPECT_EQ(pm, mesh.PMData()); EXPECT_EQ(uv, mesh.ChannelData(0));
    EXPECT_EQ(&material, mesh.GetChannelMaterial(0)); ExpectLiteral(mesh, 0);
}
TEST_F(MeshSerializationCompletionTest, PublicLegacyVertexLoadDispatchesVirtualFullLoadWithNullFile) {
    CompletionMesh mesh(Context); RCKMaterial material(Context); Seed(mesh); ASSERT_EQ(0, mesh.AddChannel(&material)); mesh.PM();
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk, 4);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(1); Vector(chunk, 7, 8, 9); Vector(chunk, 0, 0, 2);
    chunk.WriteFloat(.25f); chunk.WriteFloat(.75f); chunk.CloseChunk(); chunk.StartRead();
    mesh.LoadVertices(&chunk); EXPECT_EQ(1, mesh.FullLoads); EXPECT_EQ(nullptr, mesh.LastFile);
    EXPECT_EQ(0, mesh.GetChannelCount()); EXPECT_EQ(nullptr, mesh.PMData()); EXPECT_EQ(1, mesh.GetVertexCount());
    VxVector p; mesh.GetVertexPosition(0, &p); EXPECT_FLOAT_EQ(7, p.x); EXPECT_FLOAT_EQ(9, p.z);
}
TEST_F(MeshSerializationCompletionTest, ModernPublicAbsentSectionDoesNotFallBackToFullLoad) {
    CompletionMesh mesh(Context); RCKMaterial material(Context); Seed(mesh); ASSERT_EQ(0, mesh.AddChannel(&material));
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); chunk.WriteIdentifier(0x7777); chunk.WriteInt(3);
    chunk.CloseChunk(); chunk.StartRead(); mesh.LoadVertices(&chunk);
    EXPECT_EQ(0, mesh.FullLoads); EXPECT_EQ(3, mesh.GetVertexCount()); EXPECT_EQ(&material, mesh.GetChannelMaterial(0));
}
TEST_F(MeshSerializationCompletionTest, SameCountPublicVertexLoadInvalidatesPreviouslyComputedBounds) {
    CompletionMesh mesh(Context); Triangle(mesh); EXPECT_FLOAT_EQ(0, mesh.GetLocalBox().Min.x);
    EXPECT_FLOAT_EQ(2, mesh.GetLocalBox().Max.x); EXPECT_FLOAT_EQ(0, mesh.GetLocalBox().Max.z);
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); LiteralVertices(chunk, 0);
    chunk.CloseChunk(); chunk.StartRead(); mesh.LoadVertices(&chunk);
    EXPECT_FLOAT_EQ(10, mesh.GetLocalBox().Min.x); EXPECT_FLOAT_EQ(12, mesh.GetLocalBox().Max.x);
    EXPECT_FLOAT_EQ(30, mesh.GetLocalBox().Min.z); EXPECT_FLOAT_EQ(32, mesh.GetLocalBox().Max.z);
}
TEST_F(MeshSerializationCompletionTest, SameCountPublicVertexLoadRefreshesResidentVertexBufferPayload) {
    MeshHardwareObserver state;
    // Tests detach the observer before base destruction; it is stack owned.
    class DeviceScope : public RCKRenderContext {
    public:
        explicit DeviceScope(CKContext *context) : RCKRenderContext(context) {}
        ~DeviceScope() override { m_RasterizerContext = nullptr; m_RasterizerDriver = nullptr; }
    } device(Context);
    device.DetachAll(); device.m_RasterizerContext = &state;
    CompletionMesh mesh(Context); Triangle(mesh);
    CKMaterialGroup *group = mesh.UploadGroup(); ASSERT_NE(nullptr, group);
    group->m_HasValidPrimitives = TRUE; group->m_Primitives.Resize(1);
    CKPrimitiveEntry &primitive = group->m_Primitives[0]; primitive.m_Type = VX_TRIANGLELIST;
    primitive.m_IndexBufferOffset = -1; primitive.m_Indices.Resize(3);
    primitive.m_Indices[0] = 0; primitive.m_Indices[1] = 1; primitive.m_Indices[2] = 2;
    VxDrawPrimitiveData data = {}; data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_STAGES0;
    data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride); data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
    data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride); data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
    data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
    ASSERT_TRUE(mesh.CheckHWVertexBuffer(&device, &state, &data));
    RCKRenderManager *manager = static_cast<RCKRenderManager *>(Context->GetRenderManager());
    ASSERT_TRUE(manager->StoreMeshBuffers(&mesh));
    mesh.SetFlags(mesh.GetFlags() & ~(VXMESH_POS_CHANGED | VXMESH_UV_CHANGED | VXMESH_COLOR_CHANGED | VXMESH_NORMAL_CHANGED));
    ASSERT_EQ(1, state.VertexLocks);
    ASSERT_TRUE(mesh.CheckHWVertexBuffer(&device, &state, &data)); EXPECT_EQ(1, state.VertexLocks);
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); LiteralVertices(chunk, 0);
    chunk.CloseChunk(); chunk.StartRead(); mesh.LoadVertices(&chunk);
    ASSERT_TRUE(mesh.CheckHWVertexBuffer(&device, &state, &data)); EXPECT_EQ(2, state.VertexLocks);
    CKRSTVertexLayout layout; CKRSTGetVertexLayout(state.VertexDesc.m_VertexFormat, state.VertexDesc.m_TexcoordDims, &layout);
    float position = 0, normal = 0, uv = 0; CKDWORD color = 0, specular = 0;
    memcpy(&position, state.VertexBytes + layout.PositionOffset, 4);
    memcpy(&normal, state.VertexBytes + layout.NormalOffset, 4);
    memcpy(&uv, state.VertexBytes + layout.TexcoordOffset[0], 4);
    memcpy(&color, state.VertexBytes + layout.DiffuseOffset, 4);
    memcpy(&specular, state.VertexBytes + layout.SpecularOffset, 4);
    EXPECT_FLOAT_EQ(10, position); EXPECT_FLOAT_EQ(2, normal); EXPECT_FLOAT_EQ(.125f, uv);
    EXPECT_EQ(0xFF001100u, color); EXPECT_EQ(0x22003300u, specular);
}
TEST_F(MeshSerializationCompletionTest, SaveWritesIndependentPlanarWireForEveryReachableCompactFlagCombination) {
    for (int mode = 0; mode < 3; ++mode) for (int compact = 0; compact < 8; ++compact) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(compact);
        CompletionMesh mesh(Context); Seed(mesh);
        for (int i = 0; i < 3; ++i) {
            mesh.SetVertexColor(i, 0xFF001100u + ((compact & 1) ? 0 : i));
            mesh.SetVertexSpecularColor(i, 0x22003300u + ((compact & 2) ? 0 : i));
            mesh.SetVertexTextureCoordinates(i, .125f + ((compact & 4) ? 0 : i), .375f, -1);
        }
        const CKDWORD expected = (compact & 3) | ((compact & 4) ? 8 : 0) | (mode ? 4 : 0) | (mode == 2 ? 16 : 0);
        mesh.SetFlags(VXMESH_VISIBLE | (mode == 1 ? VXMESH_GENNORMALS : 0) | (mode == 2 ? VXMESH_PROCEDURALPOS : 0));
        CKStateChunk *chunk = mesh.Save(nullptr, CK_STATESAVE_MESHONLY); ASSERT_NE(nullptr, chunk);
        chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x20000));
        EXPECT_EQ(3, chunk->ReadInt()); EXPECT_EQ(expected, chunk->ReadDword()); EXPECT_EQ(Words(3, expected), chunk->ReadInt());
        if (!(expected & 16)) for (int i = 0; i < 3; ++i) {
            EXPECT_FLOAT_EQ(-10.0f - i, chunk->ReadFloat()); EXPECT_FLOAT_EQ(-20.0f - i, chunk->ReadFloat()); EXPECT_FLOAT_EQ(-30.0f - i, chunk->ReadFloat());
        }
        for (int i = 0; i < ((expected & 1) ? 1 : 3); ++i) EXPECT_EQ(0xFF001100u + i, chunk->ReadDword());
        for (int i = 0; i < ((expected & 2) ? 1 : 3); ++i) EXPECT_EQ(0x22003300u + i, chunk->ReadDword());
        if (!(expected & 4)) for (int i = 0; i < 3; ++i) {
            EXPECT_FLOAT_EQ(-2.0f - i, chunk->ReadFloat()); EXPECT_FLOAT_EQ(-3.0f - i, chunk->ReadFloat()); EXPECT_FLOAT_EQ(-4.0f - i, chunk->ReadFloat());
        }
        for (int i = 0; i < ((expected & 8) ? 1 : 3); ++i) {
            EXPECT_FLOAT_EQ(.125f + i, chunk->ReadFloat()); EXPECT_FLOAT_EQ(.375f, chunk->ReadFloat());
        }
        delete chunk;
    }
}
TEST_F(MeshSerializationCompletionTest, SaveLargeUncompressedVerticesRetainsInclusiveSizeAndLastUV) {
    CompletionMesh mesh(Context); Seed(mesh, 193);
    for (int i = 0; i < 193; ++i) {
        mesh.SetVertexColor(i, 0xFF000000u + i); mesh.SetVertexSpecularColor(i, 0x11000000u + i);
        mesh.SetVertexTextureCoordinates(i, .125f + i, .875f + i, -1);
    }
    CKStateChunk *chunk = mesh.Save(nullptr, CK_STATESAVE_MESHONLY); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x20000)); EXPECT_EQ(193, chunk->ReadInt()); EXPECT_EQ(0u, chunk->ReadDword());
    EXPECT_EQ(1 + 10 * 193, chunk->ReadInt()); chunk->Skip(10 * 193 - 2);
    EXPECT_FLOAT_EQ(192.125f, chunk->ReadFloat()); EXPECT_FLOAT_EQ(192.875f, chunk->ReadFloat()); delete chunk;
}
TEST_F(MeshSerializationCompletionTest, SaveProceduralUVUsesOnlyFirstPairAndConstantWeightsUseNoArraySize) {
    CompletionMesh mesh(Context); Seed(mesh); mesh.SetVertexWeightsCount(3);
    for (int i = 0; i < 3; ++i) { mesh.SetVertexTextureCoordinates(i, .125f + i, .375f + i, -1); mesh.SetVertexWeight(i, .625f); }
    mesh.SetFlags(VXMESH_VISIBLE | VXMESH_PROCEDURALUV);
    CKStateChunk *chunk = mesh.Save(nullptr, CK_STATESAVE_MESHONLY); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x20000)); EXPECT_EQ(3, chunk->ReadInt()); EXPECT_EQ(11u, chunk->ReadDword());
    EXPECT_EQ(Words(3, 11), chunk->ReadInt()); chunk->Skip(3 * 3 + 1 + 1 + 3 * 3);
    EXPECT_FLOAT_EQ(.125f, chunk->ReadFloat()); EXPECT_FLOAT_EQ(.375f, chunk->ReadFloat());
    EXPECT_EQ(8, chunk->SeekIdentifierAndReturnSize(0x80000)); EXPECT_EQ(3, chunk->ReadInt()); EXPECT_FLOAT_EQ(.625f, chunk->ReadFloat()); delete chunk;
}
TEST_F(MeshSerializationCompletionTest, SaveFileForcesMeshDataWithNoExplicitSaveFlags) {
    CompletionMesh mesh(Context); Seed(mesh); CKFile file(Context);
    file.m_IndexByClassId.Resize(CKGetClassCount()); CKStateChunk *chunk = mesh.Save(&file, 0); ASSERT_NE(nullptr, chunk);
    chunk->StartRead(); EXPECT_TRUE(chunk->SeekIdentifier(0x2000)); EXPECT_TRUE(chunk->SeekIdentifier(0x20000)); delete chunk;
}
TEST_F(MeshSerializationCompletionTest, PatchSaveOmitsGeometryAndChannelsButRetainsWeightAndProgressiveSections) {
    CompletionMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh); mesh.SetLineCount(1); mesh.SetLine(0, 0, 1);
    ASSERT_EQ(0, mesh.AddChannel(&material)); mesh.SetVertexWeightsCount(3); mesh.PM(); mesh.ReportedClass = CKCID_PATCHMESH;
    CKStateChunk *chunk = mesh.Save(nullptr, CK_STATESAVE_MESHONLY); ASSERT_NE(nullptr, chunk);
    // A derived class must be able to append before closing the parent chunk.
    chunk->WriteIdentifier(0x123456); chunk->WriteDword(0xABCDEF01); chunk->CloseChunk(); chunk->StartRead();
    EXPECT_TRUE(chunk->SeekIdentifier(0x2000)); EXPECT_FALSE(chunk->SeekIdentifier(0x100000));
    EXPECT_FALSE(chunk->SeekIdentifier(0x10000)); EXPECT_FALSE(chunk->SeekIdentifier(0x40000));
    EXPECT_FALSE(chunk->SeekIdentifier(0x20000)); EXPECT_FALSE(chunk->SeekIdentifier(0x4000));
    EXPECT_TRUE(chunk->SeekIdentifier(0x80000)); ASSERT_TRUE(chunk->SeekIdentifier(0x800000));
    EXPECT_EQ(2, chunk->ReadInt()); EXPECT_EQ(FALSE, chunk->ReadInt()); EXPECT_EQ(7, chunk->ReadInt());
    EXPECT_EQ(17u, chunk->ReadDword()); EXPECT_EQ(29u, chunk->ReadDword());
    ASSERT_TRUE(chunk->SeekIdentifier(0x123456)); EXPECT_EQ(0xABCDEF01u, chunk->ReadDword()); delete chunk;
}
TEST_F(MeshSerializationCompletionTest, FullLoadRebuildsOmittedNormalsFromLiteralFaces) {
    CompletionMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(3); chunk.WriteDword(4); chunk.WriteInt(Words(3, 4));
    Vector(chunk, 0, 0, 0); Vector(chunk, 2, 0, 0); Vector(chunk, 0, 3, 0);
    for (int i = 0; i < 6; ++i) chunk.WriteDword(0xFFFFFFFF);
    for (int i = 0; i < 6; ++i) chunk.WriteFloat(.5f);
    chunk.WriteIdentifier(0x100000); chunk.WriteInt(1); chunk.WriteObject(nullptr); chunk.WriteInt(0);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(1); chunk.WriteDwordAsWords(0x00010000); chunk.WriteDwordAsWords(2);
    chunk.CloseChunk(); ASSERT_EQ(CK_OK, mesh.Load(&chunk, nullptr));
    for (int i = 0; i < 3; ++i) { VxVector n; mesh.GetVertexNormal(i, &n); EXPECT_FLOAT_EQ(0, n.x); EXPECT_FLOAT_EQ(0, n.y); EXPECT_FLOAT_EQ(1, n.z); }
}
TEST_F(MeshSerializationCompletionTest, ModernMaterialTableMapsLaterNullSlotsBackToFirstStoredGroup) {
    CompletionMesh mesh(Context); RCKMaterial first(Context), second(Context); Seed(mesh);
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x100000); chunk.WriteInt(3);
    chunk.WriteObject(&first); chunk.WriteInt(0); chunk.WriteObject(nullptr); chunk.WriteInt(0);
    chunk.WriteObject(&second); chunk.WriteInt(0);
    chunk.WriteIdentifier(0x10000); chunk.WriteInt(2);
    chunk.WriteDwordAsWords(0x00010000); chunk.WriteDwordAsWords(0x00010002);
    chunk.WriteDwordAsWords(0x00020000); chunk.WriteDwordAsWords(0x00020001);
    chunk.CloseChunk(); ASSERT_EQ(CK_OK, mesh.Load(&chunk, nullptr));
    EXPECT_EQ(&first, mesh.GetFaceMaterial(0)); EXPECT_EQ(&second, mesh.GetFaceMaterial(1));
    int a = -1, b = -1, c = -1; mesh.GetFaceVertexIndex(1, a, b, c);
    EXPECT_EQ(0, a); EXPECT_EQ(2, b); EXPECT_EQ(1, c);
}
TEST_F(MeshSerializationCompletionTest, LegacyLitAndPrelitLayoutsKeepVersionOneFourFiveEightStreamBoundaries) {
    const int versions[4] = {1, 4, 5, 8};
    for (int version : versions) for (int prelit = 0; prelit < 2; ++prelit) {
        SCOPED_TRACE(version);
        SCOPED_TRACE(prelit);
        CompletionMesh mesh(Context);
        CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk, version);
        chunk.WriteIdentifier(0x2000); chunk.WriteDword(VXMESH_VISIBLE | (prelit ? VXMESH_PRELITMODE : 0));
        chunk.WriteIdentifier(0x20000); chunk.WriteInt(2);
        if (version >= 5) {
            chunk.WriteDword(0);
            Vector(chunk, 10, 20, 30); Vector(chunk, 11, 21, 31);
            if (prelit) {
                chunk.WriteDword(0xFF001100); chunk.WriteDword(0xFF001101);
                chunk.WriteDword(0x22003300); chunk.WriteDword(0x22003301);
            } else { Vector(chunk, 2, 3, 4); Vector(chunk, 3, 4, 5); }
            chunk.WriteFloat(.125f); chunk.WriteFloat(.375f); chunk.WriteFloat(1.125f); chunk.WriteFloat(1.375f);
        } else {
            for (int i = 0; i < 2; ++i) {
                Vector(chunk, 10.0f + i, 20.0f + i, 30.0f + i);
                if (prelit) { chunk.WriteDword(0xFF001100u + i); chunk.WriteDword(0x22003300u + i); }
                else Vector(chunk, 2.0f + i, 3.0f + i, 4.0f + i);
                chunk.WriteFloat(.125f + i); chunk.WriteFloat(.375f + i);
            }
        }
        chunk.WriteIdentifier(0x40000); chunk.WriteInt(1); chunk.WriteInt(4); chunk.WriteDwordAsWords(1);
        chunk.CloseChunk(); ASSERT_EQ(CK_OK, mesh.Load(&chunk, nullptr)); ASSERT_EQ(2, mesh.GetVertexCount());
        VxVector p, n; mesh.GetVertexPosition(1, &p); mesh.GetVertexNormal(1, &n);
        EXPECT_FLOAT_EQ(11, p.x); EXPECT_FLOAT_EQ(31, p.z);
        if (prelit) { EXPECT_EQ(0xFF001101u, mesh.GetVertexColor(1)); EXPECT_EQ(0x22003301u, mesh.GetVertexSpecularColor(1)); }
        else { EXPECT_FLOAT_EQ(3, n.x); EXPECT_FLOAT_EQ(5, n.z); }
        float u = 0, v = 0; mesh.GetVertexTextureCoordinates(1, &u, &v, -1); EXPECT_FLOAT_EQ(1.125f, u); EXPECT_FLOAT_EQ(1.375f, v);
        int a = -1, b = -1; mesh.GetLine(0, &a, &b); EXPECT_EQ(1, a); EXPECT_EQ(0, b);
    }
}
TEST_F(MeshSerializationCompletionTest, FileLoadMergesExistingMaterialChannelAndConsumesRejectedNullChannel) {
    CompletionMesh mesh(Context); RCKMaterial material(Context); Seed(mesh); ASSERT_EQ(0, mesh.AddChannel(&material));
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); chunk.WriteIdentifier(0x4000); chunk.WriteInt(2);
    Channel(chunk, nullptr, VXCHANNEL_ACTIVE, .125f); Channel(chunk, &material, VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT, .625f);
    chunk.CloseChunk(); CKFile file(Context); ASSERT_EQ(CK_OK, mesh.Load(&chunk, &file));
    ASSERT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(&material, mesh.GetChannelMaterial(0)); EXPECT_FALSE(mesh.IsChannelLit(0));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(2, &u, &v, 0); EXPECT_FLOAT_EQ(2.625f, u); EXPECT_FLOAT_EQ(.875f, v);
}
TEST_F(MeshSerializationCompletionTest, SameUVLiteralChannelDoesNotOverwriteMainUVAndFollowingChannelRemainsAligned) {
    CompletionMesh mesh(Context); RCKMaterial first(Context), second(Context); Seed(mesh);
    for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, .25f + i, .5f, -1);
    CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk); chunk.WriteIdentifier(0x4000); chunk.WriteInt(2);
    Channel(chunk, &first, VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV, 100.0f);
    Channel(chunk, &second, VXCHANNEL_ACTIVE, .625f); chunk.CloseChunk(); ASSERT_EQ(CK_OK, mesh.Load(&chunk, nullptr));
    ASSERT_EQ(2, mesh.GetChannelCount()); EXPECT_EQ(nullptr, mesh.ChannelData(0));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(1, &u, &v, -1); EXPECT_FLOAT_EQ(1.25f, u);
    mesh.GetVertexTextureCoordinates(1, &u, &v, 1); EXPECT_FLOAT_EQ(1.625f, u); EXPECT_FLOAT_EQ(.875f, v);
}
TEST_F(MeshSerializationCompletionTest, LoadPreservesAuthoredForcedTransparencyRatherThanNativeMaskLoss) {
    CompletionMesh mesh(Context); CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x2000); chunk.WriteDword(VXMESH_VISIBLE | VXMESH_FORCETRANSPARENCY | 0x80000000u);
    chunk.CloseChunk(); ASSERT_EQ(CK_OK, mesh.Load(&chunk, nullptr));
    EXPECT_TRUE(mesh.GetFlags() & VXMESH_FORCETRANSPARENCY); EXPECT_FALSE(mesh.GetFlags() & 0x80000000u); EXPECT_TRUE(mesh.IsTransparent());
}
TEST_F(MeshSerializationCompletionTest, PatchLoadSkipsVertexFaceAndChannelSectionsButReadsWeights) {
    CompletionMesh mesh(Context); RCKMaterial material(Context); Triangle(mesh); ASSERT_EQ(0, mesh.AddChannel(&material));
    mesh.ReportedClass = CKCID_PATCHMESH; CKStateChunk chunk(CKCID_MESH, nullptr); Begin(chunk);
    LiteralVertices(chunk, 0, 1); chunk.WriteIdentifier(0x4000); chunk.WriteInt(0);
    chunk.WriteIdentifier(0x80000); chunk.WriteInt(3); chunk.WriteFloat(.625f);
    chunk.CloseChunk(); CKFile file(Context); ASSERT_EQ(CK_OK, mesh.Load(&chunk, &file));
    EXPECT_EQ(3, mesh.GetVertexCount()); EXPECT_EQ(1, mesh.GetFaceCount()); EXPECT_EQ(&material, mesh.GetChannelMaterial(0));
    EXPECT_EQ(3, mesh.GetVertexWeightsCount()); EXPECT_FLOAT_EQ(.625f, mesh.GetVertexWeight(2));
}
} // namespace
