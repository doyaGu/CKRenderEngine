#include <gtest/gtest.h>
#include "../src/CKMesh.cpp"
#include "TextureTestObserver.h"

namespace {
class AnnotationMesh : public RCKMesh {
public:
    explicit AnnotationMesh(CKContext *context) : RCKMesh(context) {}
    void Cold() { m_Valid = 0; }
};

class AnnotationObserver : public TextureTestObserver {
public:
    CKDrawAnnotationParsed LastMarker = {}, DrawMarkers[12] = {};
    VXPRIMITIVETYPE Types[12] = {};
    int Counts[12] = {}, Vertices[12] = {};
    VxVector FirstPosition[12];
    int MarkerCount = 0, DrawCount = 0;
    void SetDebugMarker(CKSTRING label) override {
        EXPECT_TRUE(CKDrawAnnotationParseLabel(label, &LastMarker));
        ++MarkerCount;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *, int count, VxDrawPrimitiveData *data) override {
        EXPECT_LT(DrawCount, 12); EXPECT_NE(nullptr, data);
        if (DrawCount >= 12 || !data) return FALSE;
        DrawMarkers[DrawCount] = LastMarker; Types[DrawCount] = type;
        Counts[DrawCount] = count; Vertices[DrawCount] = data->VertexCount;
        EXPECT_NE(nullptr, data->PositionPtr);
        if (data->PositionPtr) memcpy(&FirstPosition[DrawCount], data->PositionPtr, sizeof(VxVector));
        ++DrawCount; return TRUE;
    }
};

class NestedAnnotationMaterial : public RCKMaterial {
public:
    explicit NestedAnnotationMaterial(CKContext *context) : RCKMaterial(context) {}
    AnnotationMesh *Nested = nullptr;
    RCK3dEntity *Entity = nullptr;
    int NestedResult = 0;
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        if (Nested && stage == 0) {
            AnnotationMesh *mesh = Nested; Nested = nullptr;
            NestedResult = mesh->DefaultRender(static_cast<RCKRenderContext *>(context), Entity);
        }
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
};

class MeshDrawAnnotationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    RCKRenderManager *Manager = nullptr;
    AnnotationObserver State;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 4;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    void Enable() { Device->ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_DRAWMAP_MARKERS); ASSERT_NE(nullptr, Device->m_DrawAnnotationState); }
    static void Geometry(AnnotationMesh &mesh, RCKMaterial &material) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
        const VxVector positions[] = {VxVector(2, 3, 4), VxVector(3, 3, 4), VxVector(2, 4, 4)};
        for (int i = 0; i < 3; ++i) {
            VxVector position = positions[i]; mesh.SetVertexPosition(i, &position);
            mesh.SetVertexColor(i, 0xffffffffu); mesh.SetVertexTextureCoordinates(i, .25f, .5f, -1);
        }
        mesh.SetFaceMaterial(0, &material); mesh.SetLitMode(VX_PRELITMESH);
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
    static void Pixel(RCKTexture &texture) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0));
        CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        const CKDWORD color = 0xff123456u; memcpy(pixels, &color, sizeof(color));
        texture.ReleaseSurfacePtr(0); texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
};

TEST_F(MeshDrawAnnotationTest, DisabledTrackingDoesNotAllocateOrEmitAMarker) {
    ASSERT_EQ(nullptr, Device->m_DrawAnnotationState);
    CKMeshSetDrawAnnotation(nullptr, (CKSTRING)"SW", nullptr, nullptr, nullptr, 0, 0, VX_TRIANGLELIST, 3, 3);
    CKMeshSetDrawAnnotation(Device, (CKSTRING)"SW", nullptr, nullptr, nullptr, 0, 0, VX_TRIANGLELIST, 3, 3);
    EXPECT_EQ(nullptr, Device->m_DrawAnnotationState); EXPECT_EQ(0, State.MarkerCount);
}

TEST_F(MeshDrawAnnotationTest, PendingMetadataOwnsPathAndObjectIdentitySnapshots) {
    Enable(); RCK3dEntity entity(Context); AnnotationMesh mesh(Context); RCKMaterial material(Context);
    entity.SetName("saved_entity"); mesh.SetName("saved_mesh"); material.SetName("saved_material");
    char path[] = "CUSTOM";
    CKMeshSetDrawAnnotation(Device, path, &entity, &mesh, &material, 7, 2, VX_LINELIST, 8, 6);
    path[0] = 'X'; entity.SetName("new_entity"); mesh.SetName("new_mesh"); material.SetName("new_material");
    CKDrawAnnotation value; ASSERT_TRUE(Device->ConsumeDrawAnnotation(&value, VX_POINTLIST, 1, 1));
    EXPECT_EQ(CKDRAW_SOURCE_MESH, value.Source); EXPECT_EQ(VX_LINELIST, value.PrimitiveType);
    EXPECT_EQ(8u, value.IndexCount); EXPECT_EQ(6u, value.VertexCount);
    EXPECT_EQ(7, value.GroupIndex); EXPECT_EQ(2, value.PrimitiveIndex); EXPECT_STREQ("CUSTOM", value.Path);
    EXPECT_EQ(entity.GetID(), value.Entity.Id); EXPECT_STREQ("saved_entity", value.Entity.Name);
    EXPECT_EQ(mesh.GetID(), value.Mesh.Id); EXPECT_STREQ("saved_mesh", value.Mesh.Name);
    EXPECT_EQ(material.GetID(), value.Material.Id); EXPECT_STREQ("saved_material", value.Material.Name);
    EXPECT_EQ(0u, value.Object.Id); EXPECT_STREQ("", value.Object.Name); EXPECT_NE(0u, value.Token);
    EXPECT_EQ(0, State.MarkerCount);
}

TEST_F(MeshDrawAnnotationTest, OptionalReferencesAndSignedOrdinalsRemainExplicitMeshMetadata) {
    Enable();
    CKMeshSetDrawAnnotation(Device, nullptr, nullptr, nullptr, nullptr, -7, -1, VX_POINTLIST, 0xffffffffu, 0x80000000u);
    CKDrawAnnotation value; ASSERT_TRUE(Device->ConsumeDrawAnnotation(&value, VX_TRIANGLELIST, 3, 3));
    EXPECT_EQ(CKDRAW_SOURCE_MESH, value.Source); EXPECT_STREQ("", value.Path);
    EXPECT_EQ(0u, value.Entity.Id); EXPECT_STREQ("", value.Entity.Name);
    EXPECT_EQ(0u, value.Mesh.Id); EXPECT_STREQ("", value.Mesh.Name);
    EXPECT_EQ(0u, value.Material.Id); EXPECT_STREQ("", value.Material.Name);
    EXPECT_EQ(-7, value.GroupIndex); EXPECT_EQ(-1, value.PrimitiveIndex);
    EXPECT_EQ(0xffffffffu, value.IndexCount); EXPECT_EQ(0x80000000u, value.VertexCount);
}

TEST_F(MeshDrawAnnotationTest, LongPathAndNamesAreTerminatedAtTheirIndependentCapacities) {
    Enable(); RCK3dEntity entity(Context); AnnotationMesh mesh(Context); RCKMaterial material(Context);
    char name[150], path[100]; memset(name, 'N', sizeof(name)); memset(path, 'P', sizeof(path));
    name[sizeof(name) - 1] = '\0'; path[sizeof(path) - 1] = '\0';
    entity.SetName(name); mesh.SetName(name); material.SetName(name);
    CKMeshSetDrawAnnotation(Device, path, &entity, &mesh, &material, 0, 0, VX_TRIANGLELIST, 3, 3);
    CKDrawAnnotation value; ASSERT_TRUE(Device->ConsumeDrawAnnotation(&value, VX_POINTLIST, 0, 0));
    EXPECT_EQ(31u, strlen(value.Path)); EXPECT_EQ(0, memcmp(value.Path, path, 31));
    EXPECT_EQ(95u, strlen(value.Entity.Name)); EXPECT_EQ(95u, strlen(value.Mesh.Name)); EXPECT_EQ(95u, strlen(value.Material.Name));
    EXPECT_EQ(0, memcmp(value.Entity.Name, name, 95)); EXPECT_EQ(0, memcmp(value.Mesh.Name, name, 95));
    EXPECT_EQ(0, memcmp(value.Material.Name, name, 95));
}

TEST_F(MeshDrawAnnotationTest, ReplacingPendingMetadataConsumesOnlyTheLatestEntryThenFallsBack) {
    Enable(); AnnotationMesh first(Context), second(Context);
    CKMeshSetDrawAnnotation(Device, (CKSTRING)"FIRST", nullptr, &first, nullptr, 1, 0, VX_TRIANGLELIST, 3, 3);
    const CKDWORD firstToken = Device->m_DrawAnnotationState->Pending.Token;
    CKMeshSetDrawAnnotation(Device, (CKSTRING)"SECOND", nullptr, &second, nullptr, 2, 3, VX_LINELIST, 4, 5);
    CKDrawAnnotation value; ASSERT_TRUE(Device->ConsumeDrawAnnotation(&value, VX_POINTLIST, 0, 0));
    EXPECT_EQ(second.GetID(), value.Mesh.Id); EXPECT_STREQ("SECOND", value.Path); EXPECT_NE(firstToken, value.Token);
    EXPECT_FALSE(Device->ConsumeDrawAnnotation(&value, VX_POINTLIST, 9, 10));
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, value.Source); EXPECT_EQ(0u, value.Mesh.Id);
    EXPECT_EQ(9u, value.IndexCount); EXPECT_EQ(10u, value.VertexCount);
    CKDrawAnnotationCounters counters; CKDrawAnnotationStateGetCounters(Device->m_DrawAnnotationState, &counters);
    EXPECT_EQ(2u, counters.PendingSetCount); EXPECT_EQ(1u, counters.PendingOverwriteCount);
    EXPECT_EQ(1u, counters.ExplicitCount); EXPECT_EQ(1u, counters.RawFallbackCount);
}

TEST_F(MeshDrawAnnotationTest, ActualSoftwareDrawAddsMatchingMetadataWithoutChangingThePayload) {
    AnnotationMesh mesh(Context); RCKMaterial material(Context); RCK3dEntity entity(Context); Geometry(mesh, material);
    ASSERT_EQ(1, mesh.DefaultRender(Device, &entity)); EXPECT_EQ(0, State.MarkerCount);
    Enable(); mesh.Cold(); ASSERT_EQ(1, mesh.DefaultRender(Device, &entity));
    ASSERT_EQ(2, State.DrawCount); ASSERT_EQ(1, State.MarkerCount);
    const CKDrawAnnotationParsed &value = State.DrawMarkers[1];
    EXPECT_TRUE(value.Valid); EXPECT_EQ(CKDRAW_SOURCE_MESH, value.Source); EXPECT_STREQ("SW", value.Path);
    EXPECT_EQ(entity.GetID(), value.Entity.Id); EXPECT_EQ(mesh.GetID(), value.Mesh.Id); EXPECT_EQ(material.GetID(), value.Material.Id);
    EXPECT_GE(value.GroupIndex, 0); EXPECT_EQ(0, value.PrimitiveIndex);
    EXPECT_EQ(State.Types[1], value.PrimitiveType); EXPECT_EQ(static_cast<CKDWORD>(State.Counts[1]), value.IndexCount);
    EXPECT_EQ(static_cast<CKDWORD>(State.Vertices[1]), value.VertexCount);
    EXPECT_EQ(3, State.Counts[0]); EXPECT_EQ(3, State.Counts[1]); EXPECT_EQ(3, State.Vertices[0]); EXPECT_EQ(3, State.Vertices[1]);
    for (int i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(2, State.FirstPosition[i].x); EXPECT_FLOAT_EQ(3, State.FirstPosition[i].y); EXPECT_FLOAT_EQ(4, State.FirstPosition[i].z);
    }
    EXPECT_FALSE(CKDrawAnnotationStateHasPending(Device->m_DrawAnnotationState));
}

TEST_F(MeshDrawAnnotationTest, SeparateChannelPassIdentifiesItsOwnMaterialAndChannelOrdinal) {
    Enable(); AnnotationMesh mesh(Context); RCKMaterial base(Context), channel(Context); RCK3dEntity entity(Context);
    RCKTexture primary(Context), extra(Context); Pixel(primary); Pixel(extra);
    base.SetTexture(0, &primary); channel.SetTexture(0, &extra); Geometry(mesh, base);
    const int index = mesh.AddChannel(&channel, FALSE); ASSERT_GE(index, 0);
    mesh.ActivateChannel(index, TRUE); mesh.SetChannelSourceBlend(index, VXBLEND_ONE); mesh.SetChannelDestBlend(index, VXBLEND_ONE);
    mesh.SetFlags(mesh.GetFlags() | VXMESH_RENDERCHANNELS);
    ASSERT_EQ(1, mesh.DefaultRender(Device, &entity)); ASSERT_EQ(2, State.DrawCount); ASSERT_EQ(2, State.MarkerCount);
    EXPECT_STREQ("SW", State.DrawMarkers[0].Path); EXPECT_EQ(base.GetID(), State.DrawMarkers[0].Material.Id);
    const CKDrawAnnotationParsed &value = State.DrawMarkers[1];
    EXPECT_STREQ("CHANNEL", value.Path); EXPECT_EQ(channel.GetID(), value.Material.Id);
    EXPECT_EQ(mesh.GetID(), value.Mesh.Id); EXPECT_EQ(entity.GetID(), value.Entity.Id);
    EXPECT_EQ(index, value.GroupIndex); EXPECT_EQ(0, value.PrimitiveIndex);
    EXPECT_EQ(3u, value.IndexCount); EXPECT_EQ(3u, value.VertexCount);
    EXPECT_NE(State.DrawMarkers[0].Token, value.Token);
}

TEST_F(MeshDrawAnnotationTest, NestedMaterialDrawKeepsInnerAndOuterMetadataSeparate) {
    Enable(); AnnotationMesh outer(Context), inner(Context); NestedAnnotationMaterial outerMaterial(Context);
    RCKMaterial innerMaterial(Context); RCK3dEntity outerEntity(Context), innerEntity(Context);
    Geometry(outer, outerMaterial); Geometry(inner, innerMaterial);
    outerMaterial.Nested = &inner; outerMaterial.Entity = &innerEntity;
    ASSERT_EQ(1, outer.DefaultRender(Device, &outerEntity)); EXPECT_EQ(1, outerMaterial.NestedResult);
    ASSERT_EQ(2, State.DrawCount); ASSERT_EQ(2, State.MarkerCount);
    EXPECT_EQ(inner.GetID(), State.DrawMarkers[0].Mesh.Id); EXPECT_EQ(innerMaterial.GetID(), State.DrawMarkers[0].Material.Id);
    EXPECT_EQ(innerEntity.GetID(), State.DrawMarkers[0].Entity.Id);
    EXPECT_EQ(outer.GetID(), State.DrawMarkers[1].Mesh.Id); EXPECT_EQ(outerMaterial.GetID(), State.DrawMarkers[1].Material.Id);
    EXPECT_EQ(outerEntity.GetID(), State.DrawMarkers[1].Entity.Id);
    EXPECT_NE(State.DrawMarkers[0].Token, State.DrawMarkers[1].Token);
    EXPECT_FALSE(CKDrawAnnotationStateHasPending(Device->m_DrawAnnotationState));
}
} // namespace
