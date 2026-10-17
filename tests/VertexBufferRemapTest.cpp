#include <gtest/gtest.h>
#include <cstring>
#include <map>
#include <vector>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "RCKRenderObject.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
const CKDWORD DirtyStreams = 0x3C000u;

VxVector Position(int i, int epoch) { return VxVector(float(100 + i + 10 * epoch), float(i * i + epoch), float(epoch)); }
VxVector Normal(int i, int epoch) { return VxVector(float(i + epoch), float(2 + epoch), float(3 + i)); }
Vx2DVector MainUV(int i, int epoch) { return Vx2DVector(.125f * i + epoch, .25f * i + 2 * epoch); }
Vx2DVector PrivateUV(int i, int epoch) { return Vx2DVector(float(20 + i + epoch), float(40 + 2 * i + epoch)); }
CKDWORD Diffuse(int i, int epoch) { return 0xFF102030u + static_cast<CKDWORD>(i + 0x100 * epoch); }
CKDWORD Specular(int i, int epoch) { return 0xFF405060u + static_cast<CKDWORD>(i + 0x100 * epoch); }

void ExpectVector(const VxVector &actual, const VxVector &expected) {
    EXPECT_FLOAT_EQ(expected.x, actual.x); EXPECT_FLOAT_EQ(expected.y, actual.y); EXPECT_FLOAT_EQ(expected.z, actual.z);
}
void ExpectUV(const Vx2DVector &actual, const Vx2DVector &expected) {
    EXPECT_FLOAT_EQ(expected.x, actual.x); EXPECT_FLOAT_EQ(expected.y, actual.y);
}
void Fill(RCKMesh &mesh, int epoch) {
    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        VxVector position = Position(i, epoch), normal = Normal(i, epoch);
        mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
        mesh.SetVertexColor(i, Diffuse(i, epoch)); mesh.SetVertexSpecularColor(i, Specular(i, epoch));
        Vx2DVector uv = MainUV(i, epoch);
        mesh.SetVertexTextureCoordinates(i, uv.x, uv.y, -1);
    }
}
void Clean(RCKMesh &mesh) { mesh.SetFlags(mesh.GetFlags() & ~DirtyStreams); }
void Remap(CKVBuffer &buffer) {
    const int indices[] = {3, 1, 3, 0};
    ASSERT_EQ(4, buffer.m_VertexRemap.Size());
    for (int i = 0; i < 4; ++i) buffer.m_VertexRemap[i] = indices[i];
}
void ExpectPayload(const CKVBuffer &buffer, int positions, int colors, int uv) {
    ASSERT_EQ(4, buffer.m_VertexRemap.Size());
    ASSERT_EQ(4, buffer.m_Vertices.Size()); ASSERT_EQ(4, buffer.m_Colors.Size());
    for (int local = 0; local < 4; ++local) {
        const int global = buffer.m_VertexRemap[local];
        ExpectVector(buffer.m_Vertices[local].m_Position, Position(global, positions));
        ExpectVector(buffer.m_Vertices[local].m_Normal, Normal(global, positions));
        EXPECT_EQ(Diffuse(global, colors), buffer.m_Colors[local].Color);
        EXPECT_EQ(Specular(global, colors), buffer.m_Colors[local].Specular);
        ExpectUV(buffer.m_Vertices[local].m_UV, MainUV(global, uv));
    }
}
void SetPrivate(RCKMesh &mesh, int channel, int epoch) {
    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        const Vx2DVector uv = PrivateUV(i, epoch);
        mesh.SetVertexTextureCoordinates(i, uv.x, uv.y, channel);
    }
}
void ExpectPrivate(const CKVBuffer &buffer, int channel, int epoch) {
    ASSERT_LT(channel, buffer.m_UVs.Size());
    ASSERT_EQ(buffer.m_VertexRemap.Size(), buffer.m_UVs[channel].Size());
    for (int i = 0; i < buffer.m_VertexRemap.Size(); ++i)
        ExpectUV(buffer.m_UVs[channel][i], PrivateUV(buffer.m_VertexRemap[i], epoch));
}

// Each public stream is valid, padded and deliberately has a different pitch.
// Even the old combined 24/8-byte reads remain within allocated packet storage.
class PitchedMesh : public RCKMesh {
public:
    struct PositionPacket { VxVector Value; float Padding[8]; } Positions[4];
    struct NormalPacket { VxVector Value; float Padding[4]; } Normals[4];
    struct ColorPacket { CKDWORD Value; CKDWORD Padding[3]; } Colors[4];
    struct SpecularPacket { CKDWORD Value; CKDWORD Padding[5]; } Speculars[4];
    struct UVPacket { Vx2DVector Value; float Padding[3]; } UVs[4];
    explicit PitchedMesh(CKContext *context) : RCKMesh(context) {
        std::memset(Positions, 0, sizeof(Positions)); std::memset(Normals, 0, sizeof(Normals));
        std::memset(Colors, 0, sizeof(Colors)); std::memset(Speculars, 0, sizeof(Speculars));
        std::memset(UVs, 0, sizeof(UVs));
        SetVertexCount(4);
        for (int i = 0; i < 4; ++i) {
            Positions[i].Value = Position(i, 2); Normals[i].Value = Normal(i, 2);
            Colors[i].Value = Diffuse(i, 2); Speculars[i].Value = Specular(i, 2); UVs[i].Value = MainUV(i, 2);
            for (float &padding : Positions[i].Padding) padding = -51.0f;
            for (CKDWORD &padding : Colors[i].Padding) padding = 0xABCDEF01u;
        }
    }
    void *GetPositionsPtr(CKDWORD *stride) override { *stride = sizeof(PositionPacket); return &Positions[0].Value; }
    void *GetNormalsPtr(CKDWORD *stride) override { *stride = sizeof(NormalPacket); return &Normals[0].Value; }
    void *GetColorsPtr(CKDWORD *stride) override { *stride = sizeof(ColorPacket); return &Colors[0].Value; }
    void *GetSpecularColorsPtr(CKDWORD *stride) override { *stride = sizeof(SpecularPacket); return &Speculars[0].Value; }
    void *GetTextureCoordinatesPtr(CKDWORD *stride, int channel) override {
        if (channel != -1) return RCKMesh::GetTextureCoordinatesPtr(stride, channel);
        *stride = sizeof(UVPacket); return &UVs[0].Value;
    }
};

class RemapDrawObserver : public MaterialRenderObserver {
public:
    struct Vertex { VxVector Position, Normal; Vx2DVector UV; CKDWORD Color = 0, Specular = 0; };
    struct Draw { bool HasUV = false, HasColor = false, HasSpecular = false; std::vector<Vertex> Vertices; };
    std::vector<Draw> Draws;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_NE(nullptr, data); EXPECT_NE(nullptr, indices);
        if (!data || !indices) return FALSE;
        Draw draw; draw.HasUV = data->TexCoordPtr != nullptr;
        draw.HasColor = data->ColorPtr != nullptr; draw.HasSpecular = data->SpecularColorPtr != nullptr;
        EXPECT_GT(count, 0); EXPECT_EQ(0, count % 3);
        for (int i = 0; i < count; ++i) {
            EXPECT_LT(indices[i], data->VertexCount);
            if (indices[i] >= data->VertexCount) return FALSE;
            Vertex vertex;
            if (data->PositionPtr) std::memcpy(&vertex.Position, static_cast<CKBYTE *>(data->PositionPtr) + indices[i] * data->PositionStride, sizeof(VxVector));
            if (data->NormalPtr) std::memcpy(&vertex.Normal, static_cast<CKBYTE *>(data->NormalPtr) + indices[i] * data->NormalStride, sizeof(VxVector));
            if (data->TexCoordPtr) std::memcpy(&vertex.UV, static_cast<CKBYTE *>(data->TexCoordPtr) + indices[i] * data->TexCoordStride, sizeof(Vx2DVector));
            if (data->ColorPtr) std::memcpy(&vertex.Color, static_cast<CKBYTE *>(data->ColorPtr) + indices[i] * data->ColorStride, sizeof(CKDWORD));
            if (data->SpecularColorPtr) std::memcpy(&vertex.Specular, static_cast<CKBYTE *>(data->SpecularColorPtr) + indices[i] * data->SpecularColorStride, sizeof(CKDWORD));
            draw.Vertices.push_back(vertex);
        }
        Draws.push_back(draw); return TRUE;
    }
};

class VertexBufferRemapTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    RemapDrawObserver Observer;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &Observer; Device->m_Current3dEntity = nullptr;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};
} // namespace

TEST_F(VertexBufferRemapTest, ConstructionResizeAndIndependentStoragePreserveExistingLocalEntries) {
    CKVBuffer empty, buffer(3), independent(3);
    EXPECT_EQ(0, empty.m_Vertices.Size()); EXPECT_EQ(0, empty.m_Colors.Size()); EXPECT_EQ(0, empty.m_VertexRemap.Size());
    EXPECT_EQ(3, buffer.m_Vertices.Size()); EXPECT_EQ(3, buffer.m_Colors.Size()); EXPECT_EQ(3, buffer.m_VertexRemap.Size());
    EXPECT_EQ(0, buffer.m_UVs.Size());
    buffer.m_VertexRemap[0] = 2; buffer.m_Vertices[0].m_Position = VxVector(4, 5, 6); buffer.m_Colors[0].Color = 0xFF123456;
    buffer.m_UVs.Resize(1); buffer.m_UVs[0].Resize(3); buffer.m_UVs[0][0] = Vx2DVector(.25f, .5f);
    EXPECT_NE(buffer.m_Vertices.Begin(), independent.m_Vertices.Begin());
    EXPECT_NE(buffer.m_Colors.Begin(), independent.m_Colors.Begin());
    EXPECT_NE(buffer.m_VertexRemap.Begin(), independent.m_VertexRemap.Begin());
    buffer.Resize(5); EXPECT_EQ(5, buffer.m_Vertices.Size()); EXPECT_EQ(5, buffer.m_Colors.Size()); EXPECT_EQ(5, buffer.m_VertexRemap.Size());
    EXPECT_EQ(2, buffer.m_VertexRemap[0]); ExpectVector(buffer.m_Vertices[0].m_Position, VxVector(4, 5, 6));
    EXPECT_EQ(0xFF123456u, buffer.m_Colors[0].Color); ExpectUV(buffer.m_UVs[0][0], Vx2DVector(.25f, .5f));
    buffer.Resize(1); EXPECT_EQ(2, buffer.m_VertexRemap[0]);
    buffer.Resize(0); EXPECT_EQ(0, buffer.m_Vertices.Size()); EXPECT_EQ(0, buffer.m_Colors.Size()); EXPECT_EQ(0, buffer.m_VertexRemap.Size());
    EXPECT_EQ(3, independent.m_Vertices.Size());
}

TEST_F(VertexBufferRemapTest, ForceCopiesEveryPayloadThroughLocalToGlobalRemapWithoutChangingTheSource) {
    RCKMesh mesh(Context); mesh.SetVertexCount(4); Fill(mesh, 0);
    CKVBuffer buffer(4); Remap(buffer); Clean(mesh); buffer.Update(&mesh, 1);
    ExpectPayload(buffer, 0, 0, 0);
    for (int i = 0; i < 4; ++i) { VxVector position; mesh.GetVertexPosition(i, &position); ExpectVector(position, Position(i, 0)); }
    buffer.m_Vertices[0].m_Position.x = -700;
    VxVector position; mesh.GetVertexPosition(3, &position); ExpectVector(position, Position(3, 0));
}

TEST_F(VertexBufferRemapTest, DirtyMaskRefreshesThePositionNormalColorAndUVGroupsIndependently) {
    RCKMesh mesh(Context); mesh.SetVertexCount(4); Fill(mesh, 0); CKVBuffer buffer(4); Remap(buffer); buffer.Update(&mesh, 1);
    Fill(mesh, 1); Clean(mesh); mesh.VertexMove(); buffer.Update(&mesh, 0); ExpectPayload(buffer, 1, 0, 0);
    Fill(mesh, 2); Clean(mesh); mesh.ColorChanged(); buffer.Update(&mesh, 0); ExpectPayload(buffer, 1, 2, 0);
    Clean(mesh); mesh.UVChanged(); buffer.Update(&mesh, 0); ExpectPayload(buffer, 1, 2, 2);
}

TEST_F(VertexBufferRemapTest, IndependentPaddedStreamsHonorEachVirtualPointerAndPitch) {
    PitchedMesh mesh(Context); CKVBuffer buffer(4); Remap(buffer); Clean(mesh);
    buffer.Update(&mesh, 1); ExpectPayload(buffer, 2, 2, 2);
}

TEST_F(VertexBufferRemapTest, CleanUnforcedUpdatePreservesTheSnapshotUntilForceRequestsARefresh) {
    RCKMesh mesh(Context); mesh.SetVertexCount(4); Fill(mesh, 0); CKVBuffer buffer(4); Remap(buffer); buffer.Update(&mesh, 1);
    Fill(mesh, 1); Clean(mesh); buffer.Update(&mesh, 0); ExpectPayload(buffer, 0, 0, 0);
    buffer.Update(&mesh, 1); ExpectPayload(buffer, 1, 1, 1);
}

TEST_F(VertexBufferRemapTest, PrivateUVStorageSurvivesAValidTemporarilyUnboundChannelMaterial) {
    RCKMesh mesh(Context); RCKMaterial material(Context); mesh.SetVertexCount(4); Fill(mesh, 0);
    const int channel = mesh.AddChannel(&material, FALSE); ASSERT_EQ(0, channel); SetPrivate(mesh, channel, 3);
    mesh.SetChannelMaterial(channel, nullptr);
    CKVBuffer buffer(4); Remap(buffer); buffer.Update(&mesh, 1); ExpectPrivate(buffer, channel, 3);
    Clean(mesh); mesh.SetChannelMaterial(channel, &material); buffer.Update(&mesh, 0); ExpectPrivate(buffer, channel, 3);
}

TEST_F(VertexBufferRemapTest, AddedRemovedAndSharedUVChannelsRefreshTheirCurrentLogicalSlots) {
    RCKMesh mesh(Context); RCKMaterial first(Context), second(Context), shared(Context), added(Context);
    mesh.SetVertexCount(4); Fill(mesh, 0); const int a = mesh.AddChannel(&first, FALSE), b = mesh.AddChannel(&second, FALSE);
    ASSERT_EQ(0, a); ASSERT_EQ(1, b); SetPrivate(mesh, a, 1); SetPrivate(mesh, b, 2);
    const int same = mesh.AddChannel(&shared, FALSE); ASSERT_EQ(2, same);
    mesh.SetChannelFlags(same, mesh.GetChannelFlags(same) | VXCHANNEL_SAMEUV); mesh.UVChanged();
    CKVBuffer buffer(4); Remap(buffer); buffer.Update(&mesh, 1); ExpectPrivate(buffer, a, 1); ExpectPrivate(buffer, b, 2);
    ASSERT_EQ(4, buffer.m_UVs[same].Size());
    for (int i = 0; i < 4; ++i) ExpectUV(buffer.m_UVs[same][i], MainUV(buffer.m_VertexRemap[i], 0));
    Clean(mesh); mesh.RemoveChannel(0); buffer.Update(&mesh, 0); ASSERT_EQ(2, buffer.m_UVs.Size()); ExpectPrivate(buffer, 0, 2);
    const int fresh = mesh.AddChannel(&added, FALSE); ASSERT_EQ(2, fresh); SetPrivate(mesh, fresh, 4); buffer.Update(&mesh, 0); ExpectPrivate(buffer, fresh, 4);
    while (mesh.GetChannelCount()) mesh.RemoveChannel(0);
    buffer.Update(&mesh, 0); EXPECT_EQ(0, buffer.m_UVs.Size());
}

TEST_F(VertexBufferRemapTest, ActualRemappedRenderingPreservesPayloadAndWholeMeshChannelCoverageOnLateBinding) {
    RCKMesh mesh(Context); RCKMaterial first(Context), second(Context), channelMaterial(Context);
    mesh.SetVertexCount(4); Fill(mesh, 0); mesh.SetFaceCount(2);
    mesh.SetFaceVertexIndex(0, 2, 3, 0); mesh.SetFaceVertexIndex(1, 1, 2, 3);
    mesh.SetFaceMaterial(0, &first); mesh.SetFaceMaterial(1, &second); mesh.SetLitMode(VX_PRELITMESH);
    const int channel = mesh.AddChannel(&channelMaterial, FALSE); ASSERT_EQ(0, channel); SetPrivate(mesh, channel, 3);
    mesh.SetChannelSourceBlend(channel, VXBLEND_SRCALPHA); mesh.SetChannelDestBlend(channel, VXBLEND_INVSRCALPHA);
    mesh.SetChannelMaterial(channel, nullptr); mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    ASSERT_EQ(1, mesh.CreateRenderGroups());
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_GE(Observer.Draws.size(), 2u);
    Observer.Draws.clear();
    mesh.SetChannelMaterial(channel, &channelMaterial);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_GE(Observer.Draws.size(), 3u);
    std::map<int, int> mainFaces, privateFaces;
    for (const RemapDrawObserver::Draw &draw : Observer.Draws) {
        ASSERT_GT(draw.Vertices.size(), 0u); ASSERT_EQ(0u, draw.Vertices.size() % 3);
        EXPECT_TRUE(draw.HasUV);
        const bool privateStream = draw.HasUV && draw.Vertices[0].UV.x >= 20.0f;
        if (!privateStream) { EXPECT_TRUE(draw.HasColor); EXPECT_TRUE(draw.HasSpecular); }
        for (const RemapDrawObserver::Vertex &vertex : draw.Vertices) {
            const int global = static_cast<int>(vertex.Position.x) - 100;
            ASSERT_GE(global, 0); ASSERT_LT(global, 4);
            ExpectVector(vertex.Position, Position(global, 0)); ExpectVector(vertex.Normal, Normal(global, 0));
            if (draw.HasColor) EXPECT_EQ(Diffuse(global, 0), vertex.Color);
            if (draw.HasSpecular) EXPECT_EQ(Specular(global, 0), vertex.Specular);
            if (!draw.HasUV) continue;
            if (privateStream) ExpectUV(vertex.UV, PrivateUV(global, 3));
            else ExpectUV(vertex.UV, MainUV(global, 0));
        }
        for (size_t i = 0; i < draw.Vertices.size(); i += 3) {
            int labels[3];
            for (int j = 0; j < 3; ++j) labels[j] = static_cast<int>(draw.Vertices[i + j].Position.x) - 100;
            const int a = labels[0] * 16 + labels[1] * 4 + labels[2];
            const int b = labels[1] * 16 + labels[2] * 4 + labels[0];
            const int c = labels[2] * 16 + labels[0] * 4 + labels[1];
            const int key = a < b ? (a < c ? a : c) : (b < c ? b : c);
            ++(privateStream ? privateFaces : mainFaces)[key];
        }
    }
    // Preserve both oriented faces once in the base and once in the channel.
    // A whole-mesh channel may legally submit both triangles in a single draw.
    std::map<int, int> expected;
    expected[2 * 4 + 3] = 1; // cyclic minimum of (2,3,0) is (0,2,3).
    expected[1 * 16 + 2 * 4 + 3] = 1;
    EXPECT_EQ(expected, mainFaces); EXPECT_EQ(expected, privateFaces);
}
