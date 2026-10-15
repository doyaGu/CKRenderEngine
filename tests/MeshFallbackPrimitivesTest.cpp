#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MeshHardwareObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
class FallbackPrimitiveObserver : public MeshHardwareObserver {
public:
    struct Draw {
        int Path = 0, Count = 0;
        VXPRIMITIVETYPE Type = VX_TRIANGLELIST;
        CKDWORD Cull = 0, Fill = 0, Source = 0, Dest = 0, Texture = 0;
        CKMaterialData MaterialAtDraw = {};
        CKWORD Indices[12] = {};
        VxVector Positions[12]; Vx2DVector UVs[12];
    };
    Draw Draws[32]; int DrawCount = 0, FailDraw = -1, FailedStageBindings = 0;
    CKBOOL RejectStage1 = FALSE;
    CKBOOL SetTexture(CKDWORD texture, int stage) override {
        if (texture && stage == 1 && RejectStage1 && !FailedStageBindings) {
            ++FailedStageBindings; return FALSE;
        }
        return MeshHardwareObserver::SetTexture(texture, stage);
    }
    Draw &Capture(int path, VXPRIMITIVETYPE type, CKWORD *indices, int count) {
        EXPECT_LT(DrawCount, 32); EXPECT_LE(count, 12);
        Draw &draw = Draws[DrawCount++]; draw.Path = path; draw.Type = type; draw.Count = count;
        draw.Cull = States[VXRENDERSTATE_CULLMODE]; draw.Fill = States[VXRENDERSTATE_FILLMODE];
        draw.Source = States[VXRENDERSTATE_SRCBLEND]; draw.Dest = States[VXRENDERSTATE_DESTBLEND]; draw.Texture = Textures[0];
        draw.MaterialAtDraw = Material;
        for (int i = 0; i < count && i < 12; ++i) draw.Indices[i] = indices[i];
        return draw;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        const int ordinal = DrawCount; Draw &draw = Capture(0, type, indices, count);
        EXPECT_NE(nullptr, data); if (!data) return FALSE;
        for (int i = 0; i < count && i < 12; ++i) {
            EXPECT_LT(indices[i], data->VertexCount);
            if (data->PositionPtr) std::memcpy(&draw.Positions[i], static_cast<CKBYTE *>(data->PositionPtr) + indices[i] * data->PositionStride, sizeof(VxVector));
            if (data->TexCoordPtr) std::memcpy(&draw.UVs[i], static_cast<CKBYTE *>(data->TexCoordPtr) + indices[i] * data->TexCoordStride, sizeof(Vx2DVector));
        }
        return ordinal != FailDraw;
    }
    CKBOOL Hardware(int path, VXPRIMITIVETYPE type, CKDWORD base, CKWORD *indices, int count) {
        const int ordinal = DrawCount; Draw &draw = Capture(path, type, indices, count);
        for (int i = 0; i < count && i < 12; ++i) {
            const CKDWORD vertex = base + indices[i]; EXPECT_LT(vertex, VertexDesc.m_CurrentVCount);
            if (vertex >= VertexDesc.m_CurrentVCount) continue;
            const CKBYTE *bytes = VertexBytes + vertex * VertexDesc.m_VertexSize;
            std::memcpy(&draw.Positions[i], bytes, sizeof(VxVector));
            std::memcpy(&draw.UVs[i], bytes + VertexDesc.m_VertexSize - sizeof(Vx2DVector), sizeof(Vx2DVector));
        }
        return ordinal != FailDraw;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD base, CKDWORD, CKWORD *indices, int count) override {
        EXPECT_EQ(VertexHandle, vb); return Hardware(1, type, base, indices, count);
    }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib, CKDWORD, CKDWORD, CKDWORD offset, int count) override {
        EXPECT_EQ(VertexHandle, vb); EXPECT_EQ(IndexHandle, ib); return Hardware(2, type, 0, IndexWords + offset, count);
    }
};
class PrimitiveTexture : public RCKTexture {
public:
    CKDWORD Marker;
    PrimitiveTexture(CKContext *context, CKDWORD marker) : RCKTexture(context), Marker(marker) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        return static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(Marker, stage);
    }
};
class FallbackPrimitiveMesh : public RCKMesh {
public:
    explicit FallbackPrimitiveMesh(CKContext *context) : RCKMesh(context) {}
    CKMaterialGroup *Group() { return m_MaterialGroups[0]; }
    void Select(int index) { m_ActiveTextureChannels.PushBack(index); m_MaterialChannels[index].m_Flags |= VXCHANNEL_MONO; }
};
struct PostObservation { FallbackPrimitiveObserver *State; int Calls = 0, DrawsSeen = 0; };
void ObserveFinishedGroup(CKRenderContext *, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    PostObservation *record = static_cast<PostObservation *>(argument);
    ++record->Calls; record->DrawsSeen = record->State->DrawCount;
}
class MeshFallbackPrimitivesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    FallbackPrimitiveObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State; Device->m_Current3dEntity = nullptr;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    VxDrawPrimitiveData Geometry(FallbackPrimitiveMesh &mesh, RCKMaterial &base, int count = 6) {
        mesh.SetVertexCount(count);
        for (int i = 0; i < count; ++i) {
            VxVector position(10.0f * (i + 1), static_cast<float>(i & 1), .5f), normal(0, 0, 1);
            mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, 1.0f + .1f * i, 2.0f + .2f * i, -1);
        }
        mesh.Group()->m_Material = &base; mesh.Group()->m_HasValidPrimitives = TRUE; mesh.Group()->m_VertexCount = count;
        VxDrawPrimitiveData data = {}; data.VertexCount = count; data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride); data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride); data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        return data;
    }
    void Primitive(FallbackPrimitiveMesh &mesh, int slot, VXPRIMITIVETYPE type, const CKWORD *indices, int count) {
        CKMaterialGroup *group = mesh.Group(); group->m_Primitives.Resize(slot + 1);
        CKPrimitiveEntry &primitive = group->m_Primitives[slot]; primitive.m_Type = type; primitive.m_IndexBufferOffset = -1;
        primitive.m_Indices.Resize(count); for (int i = 0; i < count; ++i) primitive.m_Indices[i] = indices[i];
    }
    int MissingChannel(FallbackPrimitiveMesh &mesh, RCKMaterial &channel, PrimitiveTexture &texture) {
        channel.SetTexture(0, &texture); const int index = mesh.AddChannel(&channel, FALSE);
        mesh.SetChannelSourceBlend(index, VXBLEND_DESTCOLOR); mesh.SetChannelDestBlend(index, VXBLEND_ZERO);
        for (int i = 0; i < mesh.GetVertexCount(); ++i) mesh.SetVertexTextureCoordinates(i, 3.0f + .1f * i, 4.0f + .2f * i, index);
        mesh.Select(index); State.RejectStage1 = TRUE; return index;
    }
    void Transparent(RCKMaterial &material) {
        material.SetTwoSided(TRUE); material.SetFillMode(VXFILL_SOLID); material.EnableAlphaBlend(TRUE);
        material.SetSourceBlend(VXBLEND_SRCALPHA); material.SetDestBlend(VXBLEND_INVSRCALPHA); material.EnableZWrite(FALSE);
    }
    void CheckIndices(int draw, VXPRIMITIVETYPE type, const CKWORD *indices, int count) {
        ASSERT_LT(draw, State.DrawCount); EXPECT_EQ(type, State.Draws[draw].Type); ASSERT_EQ(count, State.Draws[draw].Count);
        for (int i = 0; i < count; ++i) EXPECT_EQ(indices[i], State.Draws[draw].Indices[i]);
    }
    void Upload(FallbackPrimitiveMesh &mesh, VxDrawPrimitiveData &data) {
        ASSERT_TRUE(mesh.CheckHWVertexBuffer(Device, &State, &data)); ASSERT_TRUE(Manager->StoreMeshBuffers(&mesh));
    }
};
}

TEST_F(MeshFallbackPrimitivesTest, OpaqueTwoSidedFallbackKeepsBothSidesVisible) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); base.SetTwoSided(TRUE); const CKWORD indices[] = {2, 0, 1};
    Primitive(mesh, 0, VX_TRIANGLELIST, indices, 3); MissingChannel(mesh, channel, texture);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(VXCULL_NONE, State.Draws[0].Cull); EXPECT_EQ(VXCULL_NONE, State.Draws[1].Cull);
    EXPECT_EQ(22u, State.Draws[1].Texture); EXPECT_EQ(VXBLEND_DESTCOLOR, State.Draws[1].Source); EXPECT_EQ(VXBLEND_ZERO, State.Draws[1].Dest);
}
TEST_F(MeshFallbackPrimitivesTest, TransparentFallbackCompletesBackSideBeforeFrontSideAndPostCallbackOnce) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); Transparent(base); const CKWORD indices[] = {2, 0, 1};
    Primitive(mesh, 0, VX_TRIANGLELIST, indices, 3); MissingChannel(mesh, channel, texture);
    PostObservation post = {&State}; mesh.AddSubMeshPostRenderCallBack(ObserveFinishedGroup, &post, FALSE);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(4, State.DrawCount);
    EXPECT_EQ(VXCULL_CW, State.Draws[0].Cull); EXPECT_EQ(VXCULL_CW, State.Draws[1].Cull);
    EXPECT_EQ(VXCULL_CCW, State.Draws[2].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[3].Cull);
    EXPECT_EQ(22u, State.Draws[1].Texture); EXPECT_EQ(22u, State.Draws[3].Texture);
    EXPECT_EQ(VXBLEND_SRCALPHA, State.Draws[2].Source); EXPECT_EQ(VXBLEND_INVSRCALPHA, State.Draws[2].Dest);
    EXPECT_EQ(1, post.Calls); EXPECT_EQ(4, post.DrawsSeen);
}
TEST_F(MeshFallbackPrimitivesTest, TransparentFailedBackFallbackStillCompletesFrontAndPostCallback) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); Transparent(base); const CKWORD indices[] = {0, 1, 2};
    Primitive(mesh, 0, VX_TRIANGLELIST, indices, 3); MissingChannel(mesh, channel, texture); State.FailDraw = 1;
    PostObservation post = {&State}; mesh.AddSubMeshPostRenderCallBack(ObserveFinishedGroup, &post, FALSE);
    EXPECT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(4, State.DrawCount);
    EXPECT_EQ(VXCULL_CCW, State.Draws[2].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[3].Cull);
    EXPECT_EQ(22u, State.Draws[3].Texture); EXPECT_EQ(1, post.Calls); EXPECT_EQ(4, post.DrawsSeen);
}
TEST_F(MeshFallbackPrimitivesTest, TwoSidedWireframeFallbackKeepsBaseCoverageAndFillMode) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); base.SetTwoSided(TRUE); base.SetFillMode(VXFILL_WIREFRAME);
    const CKWORD indices[] = {2, 0, 1}; Primitive(mesh, 0, VX_TRIANGLELIST, indices, 3); MissingChannel(mesh, channel, texture);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(VXCULL_NONE, State.Draws[1].Cull); EXPECT_EQ(VXFILL_WIREFRAME, State.Draws[1].Fill);
}
TEST_F(MeshFallbackPrimitivesTest, TransparentFallbackSuffixKeepsAuthoredChannelOrderOnEachSide) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), first(Context), second(Context);
    PrimitiveTexture a(Context, 22), b(Context, 33); auto data = Geometry(mesh, base); Transparent(base);
    const CKWORD indices[] = {2, 0, 1}; Primitive(mesh, 0, VX_TRIANGLELIST, indices, 3);
    MissingChannel(mesh, first, a); MissingChannel(mesh, second, b);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(6, State.DrawCount);
    EXPECT_EQ(22u, State.Draws[1].Texture); EXPECT_EQ(33u, State.Draws[2].Texture);
    EXPECT_EQ(22u, State.Draws[4].Texture); EXPECT_EQ(33u, State.Draws[5].Texture);
    EXPECT_EQ(VXCULL_CW, State.Draws[2].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[4].Cull);
}
TEST_F(MeshFallbackPrimitivesTest, StripFallbackPreservesAlternatingWindingAndOriginalIndexSequence) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); const CKWORD strip[] = {2, 0, 3, 1};
    Primitive(mesh, 0, VX_TRIANGLESTRIP, strip, 4); MissingChannel(mesh, channel, texture);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(2, State.DrawCount);
    CheckIndices(1, VX_TRIANGLESTRIP, strip, 4);
    EXPECT_FLOAT_EQ(30, State.Draws[1].Positions[0].x); EXPECT_FLOAT_EQ(40, State.Draws[1].Positions[2].x);
    EXPECT_FLOAT_EQ(3.2f, State.Draws[1].UVs[0].x); EXPECT_FLOAT_EQ(3.3f, State.Draws[1].UVs[2].x);
}
TEST_F(MeshFallbackPrimitivesTest, StripFallbackRetainsDegenerateBridgesAndFollowingPrimitiveOrder) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); const CKWORD strip[] = {0, 1, 2, 2, 3, 3, 4, 5}, triangle[] = {5, 3, 1};
    Primitive(mesh, 0, VX_TRIANGLESTRIP, strip, 8); Primitive(mesh, 1, VX_TRIANGLELIST, triangle, 3); MissingChannel(mesh, channel, texture);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(4, State.DrawCount);
    CheckIndices(2, VX_TRIANGLESTRIP, strip, 8); CheckIndices(3, VX_TRIANGLELIST, triangle, 3);
    EXPECT_FLOAT_EQ(60, State.Draws[2].Positions[7].x); EXPECT_FLOAT_EQ(3.5f, State.Draws[2].UVs[7].x);
}
TEST_F(MeshFallbackPrimitivesTest, TransparentStripFallbackKeepsEachSidePrimitiveOrder) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); Transparent(base); const CKWORD strip[] = {0, 1, 2, 3}, triangle[] = {4, 3, 5};
    Primitive(mesh, 0, VX_TRIANGLESTRIP, strip, 4); Primitive(mesh, 1, VX_TRIANGLELIST, triangle, 3); MissingChannel(mesh, channel, texture);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data)); ASSERT_EQ(8, State.DrawCount);
    CheckIndices(2, VX_TRIANGLESTRIP, strip, 4); CheckIndices(3, VX_TRIANGLELIST, triangle, 3);
    CheckIndices(6, VX_TRIANGLESTRIP, strip, 4); CheckIndices(7, VX_TRIANGLELIST, triangle, 3);
    EXPECT_EQ(VXCULL_CW, State.Draws[2].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[6].Cull);
}
TEST_F(MeshFallbackPrimitivesTest, HardwareStripUsesSoftwarePrimitiveDomainForFallback) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); const CKWORD strip[] = {2, 0, 3, 1};
    Primitive(mesh, 0, VX_TRIANGLESTRIP, strip, 4); MissingChannel(mesh, channel, texture); Upload(mesh, data);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, nullptr)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(2, State.Draws[0].Path); EXPECT_EQ(0, State.Draws[1].Path); CheckIndices(1, VX_TRIANGLESTRIP, strip, 4);
    EXPECT_FLOAT_EQ(30, State.Draws[1].Positions[0].x); EXPECT_FLOAT_EQ(3.2f, State.Draws[1].UVs[0].x);
}
TEST_F(MeshFallbackPrimitivesTest, WrappedHardwareBackAndFrontUseExpandedIndexDomainForAllTriangles) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context); auto data = Geometry(mesh, base, 4); Transparent(base);
    const CKWORD triangles[] = {0, 1, 2, 0, 2, 3}; Primitive(mesh, 0, VX_TRIANGLELIST, triangles, 6);
    mesh.SetWrapMode(VXTEXTUREWRAP_U); Upload(mesh, data);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, nullptr)); ASSERT_EQ(2, State.DrawCount);
    // The second triangle's last corner must be global vertex3, not expanded
    // vertex3 (which is the duplicated global vertex0).
    EXPECT_FLOAT_EQ(40, State.Draws[0].Positions[5].x); EXPECT_FLOAT_EQ(40, State.Draws[1].Positions[5].x);
    EXPECT_EQ(2, State.Draws[0].Path); EXPECT_EQ(2, State.Draws[1].Path);
}
TEST_F(MeshFallbackPrimitivesTest, WrappedHardwareFallbackRecoversOriginalSoftwareTrianglesOnBothSides) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base, 4); Transparent(base); const CKWORD triangles[] = {0, 1, 2, 0, 2, 3};
    Primitive(mesh, 0, VX_TRIANGLELIST, triangles, 6); MissingChannel(mesh, channel, texture);
    mesh.SetWrapMode(VXTEXTUREWRAP_U); Upload(mesh, data);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, nullptr)); ASSERT_EQ(4, State.DrawCount);
    EXPECT_EQ(0, State.Draws[1].Path); EXPECT_EQ(0, State.Draws[3].Path);
    CheckIndices(1, VX_TRIANGLELIST, triangles, 6); CheckIndices(3, VX_TRIANGLELIST, triangles, 6);
    EXPECT_FLOAT_EQ(40, State.Draws[1].Positions[5].x); EXPECT_FLOAT_EQ(3.3f, State.Draws[3].UVs[5].x);
}
TEST_F(MeshFallbackPrimitivesTest, HardwareRemappedStripFallbackUsesLocalIndicesWithGlobalMappedCoordinates) {
    FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context); PrimitiveTexture texture(Context, 22);
    auto data = Geometry(mesh, base); const CKWORD strip[] = {0, 2, 1};
    Primitive(mesh, 0, VX_TRIANGLESTRIP, strip, 3); MissingChannel(mesh, channel, texture);
    CKVBuffer *remap = new CKVBuffer(3); remap->m_VertexRemap[0] = 4; remap->m_VertexRemap[1] = 0; remap->m_VertexRemap[2] = 2;
    mesh.Group()->m_RemapData = reinterpret_cast<CKUINTPTR>(remap); mesh.Group()->m_VertexCount = 3; Upload(mesh, data);
    ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, nullptr)); ASSERT_EQ(2, State.DrawCount);
    CheckIndices(1, VX_TRIANGLESTRIP, strip, 3); EXPECT_FLOAT_EQ(50, State.Draws[1].Positions[0].x);
    EXPECT_FLOAT_EQ(30, State.Draws[1].Positions[1].x); EXPECT_FLOAT_EQ(10, State.Draws[1].Positions[2].x);
    EXPECT_FLOAT_EQ(3.4f, State.Draws[1].UVs[0].x); EXPECT_FLOAT_EQ(3.0f, State.Draws[1].UVs[2].x);
}

namespace {
struct PrimitiveMaterialCallback {
    int Calls = 0;
    CKMaterialData OwnedMaterial = {};
};
int CountOrdinaryPrimitiveMaterial(CKRenderContext *, CKMaterial *, void *argument) {
    ++*static_cast<int *>(argument);
    return FALSE;
}
int OwnFallbackPrimitiveMaterial(CKRenderContext *context, CKMaterial *, void *argument) {
    PrimitiveMaterialCallback *callback = static_cast<PrimitiveMaterialCallback *>(argument);
    ++callback->Calls;
    // First call is the positive-stage attempt: use ordinary texture binding,
    // whose real SetTexture rejection selects group-local software recovery.
    if (callback->Calls == 1)
        return FALSE;
    CKRasterizerContext *rst = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    rst->SetMaterial(&callback->OwnedMaterial);
    rst->SetTexture(99, 0);
    rst->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    rst->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_DESTCOLOR);
    rst->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_ZERO);
    return TRUE;
}
void CheckPrimitiveMaterial(const CKMaterialData &expected, const CKMaterialData &actual) {
    EXPECT_EQ(expected.Diffuse, actual.Diffuse);
    EXPECT_EQ(expected.Ambient, actual.Ambient);
    EXPECT_EQ(expected.Specular, actual.Specular);
    EXPECT_EQ(expected.Emissive, actual.Emissive);
    EXPECT_FLOAT_EQ(expected.SpecularPower, actual.SpecularPower);
}
}

TEST_F(MeshFallbackPrimitivesTest, TransparentFrontRestoresBaseMaterialAfterOwnedBackFallbackWithoutReplayingCallback) {
    // Exercise both enabled specular and cached-but-disabled specular. Expected
    // payloads are authored independently, rather than read back from getters.
    for (int enabledSpecular = 0; enabledSpecular < 2; ++enabledSpecular) {
        SCOPED_TRACE(enabledSpecular);
        State.DrawCount = 0; State.MaterialWrites = 0; State.FailedStageBindings = 0;
        FallbackPrimitiveMesh mesh(Context); RCKMaterial base(Context), channel(Context);
        PrimitiveTexture texture(Context, 22); auto data = Geometry(mesh, base);
        mesh.SetLitMode(VX_LITMESH); data.Flags |= CKRST_DP_LIGHT; Transparent(base);
        const CKWORD indices[] = {2, 0, 1}; Primitive(mesh, 0, VX_TRIANGLELIST, indices, 3);
        MissingChannel(mesh, channel, texture);
        CKMaterialData expected = {};
        expected.Diffuse = VxColor(.11f, .21f, .31f, .41f);
        expected.Ambient = VxColor(.12f, .22f, .32f, .42f);
        expected.Emissive = VxColor(.13f, .23f, .33f, .43f);
        const VxColor cachedSpecular(.14f, .24f, .34f, .44f);
        expected.SpecularPower = enabledSpecular ? 7.0f : .025f;
        expected.Specular = enabledSpecular ? cachedSpecular : VxColor(0, 0, 0, 0);
        base.SetDiffuse(expected.Diffuse); base.SetAmbient(expected.Ambient);
        base.SetEmissive(expected.Emissive); base.SetPower(expected.SpecularPower);
        base.SetSpecular(cachedSpecular);
        int baseCalls = 0; base.SetCallback(CountOrdinaryPrimitiveMaterial, &baseCalls);
        PrimitiveMaterialCallback callback;
        callback.OwnedMaterial.Diffuse = VxColor(.91f, .81f, .71f, .61f);
        callback.OwnedMaterial.Ambient = VxColor(.92f, .82f, .72f, .62f);
        callback.OwnedMaterial.Specular = VxColor(.93f, .83f, .73f, .63f);
        callback.OwnedMaterial.Emissive = VxColor(.94f, .84f, .74f, .64f);
        callback.OwnedMaterial.SpecularPower = 19.0f;
        channel.SetCallback(OwnFallbackPrimitiveMaterial, &callback);
        ASSERT_EQ(1, mesh.RenderGroup(Device, mesh.Group(), nullptr, &data));
        ASSERT_EQ(4, State.DrawCount);
        CheckPrimitiveMaterial(expected, State.Draws[0].MaterialAtDraw);
        CheckPrimitiveMaterial(callback.OwnedMaterial, State.Draws[1].MaterialAtDraw);
        CheckPrimitiveMaterial(expected, State.Draws[2].MaterialAtDraw);
        CheckPrimitiveMaterial(callback.OwnedMaterial, State.Draws[3].MaterialAtDraw);
        EXPECT_EQ(1, baseCalls); EXPECT_EQ(3, callback.Calls);
        EXPECT_EQ(4, State.MaterialWrites);
        EXPECT_EQ(VXCULL_CW, State.Draws[1].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[2].Cull);
        EXPECT_EQ(99u, State.Draws[1].Texture); EXPECT_EQ(99u, State.Draws[3].Texture);
    }
}
