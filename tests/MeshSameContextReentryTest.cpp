#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "TextureTestObserver.h"

namespace {
class SameContextMesh : public RCKMesh {
public:
    explicit SameContextMesh(CKContext *context) : RCKMesh(context) {}
    CKMaterialGroup *Group(CKMaterial *material) {
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i] && m_MaterialGroups[i]->m_Material == material &&
                m_MaterialGroups[i]->m_HasValidPrimitives)
                return m_MaterialGroups[i];
        return nullptr;
    }
};

// Each live public buffer handle has distinct byte storage. A nested upload
// cannot accidentally appear correct by overwriting one spy-wide byte array.
class SameContextObserver : public TextureTestObserver {
public:
    struct Resource {
        CKBOOL Live = FALSE;
        CKRST_OBJECTTYPE Type = CKRST_OBJ_VERTEXBUFFER;
        CKVertexBufferDesc Vertex;
        CKIndexBufferDesc Index;
        CKBYTE Bytes[8192] = {};
        CKWORD Words[512] = {};
        int Deletes = 0;
    };
    struct Draw {
        CKBOOL Hardware = FALSE;
        CKDWORD VB = 0, IB = 0, Flags = 0, Textures[2] = {};
        CKDWORD Lighting = 0, Cull = 0, ViewWidth = 0;
        int UVCount = 0;
        VxVector Positions[3];
        Vx2DVector UV[2][3];
        CKBOOL HasUV[2] = {};
        VxMatrix World, View, Projection;
        float MaterialR = 0;
    };
    Resource Resources[16];
    int ResourceCount = 0, Count = 0, Attempts = 0;
    CKBOOL ThrowNextDraw = FALSE;
    Draw Draws[32];
    Resource *Buffer(CKDWORD handle) {
        return handle >= 100 && handle < 100u + ResourceCount ? &Resources[handle - 100] : nullptr;
    }
    const Resource *Buffer(CKDWORD handle) const {
        return handle >= 100 && handle < 100u + ResourceCount ? &Resources[handle - 100] : nullptr;
    }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *bytes, CKDWORD *handle) override {
        EXPECT_LT(ResourceCount, 16);
        if (ResourceCount >= 16) return FALSE;
        Resource &resource = Resources[ResourceCount];
        *handle = 100 + ResourceCount++;
        resource.Live = TRUE; resource.Type = CKRST_OBJ_VERTEXBUFFER; resource.Vertex = *desc;
        const size_t size = static_cast<size_t>(desc->m_MaxVertexCount) * desc->m_VertexSize;
        EXPECT_LE(size, sizeof(resource.Bytes));
        if (size > sizeof(resource.Bytes)) return FALSE;
        if (bytes) memcpy(resource.Bytes, bytes, static_cast<size_t>(desc->m_CurrentVCount) * desc->m_VertexSize);
        return TRUE;
    }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *desc, const void *words, CKDWORD *handle) override {
        EXPECT_LT(ResourceCount, 16);
        if (ResourceCount >= 16) return FALSE;
        Resource &resource = Resources[ResourceCount];
        *handle = 100 + ResourceCount++;
        resource.Live = TRUE; resource.Type = CKRST_OBJ_INDEXBUFFER; resource.Index = *desc;
        EXPECT_LE(desc->m_MaxIndexCount, 512u);
        if (desc->m_MaxIndexCount > 512) return FALSE;
        if (words) memcpy(resource.Words, words, desc->m_CurrentICount * sizeof(CKWORD));
        return TRUE;
    }
    CKBOOL GetVertexBufferDesc(CKDWORD handle, CKVertexBufferDesc *desc) const override {
        const Resource *resource = Buffer(handle);
        if (!resource || !resource->Live || resource->Type != CKRST_OBJ_VERTEXBUFFER) return FALSE;
        *desc = resource->Vertex; return TRUE;
    }
    CKBOOL GetIndexBufferDesc(CKDWORD handle, CKIndexBufferDesc *desc) const override {
        const Resource *resource = Buffer(handle);
        if (!resource || !resource->Live || resource->Type != CKRST_OBJ_INDEXBUFFER) return FALSE;
        *desc = resource->Index; return TRUE;
    }
    void *LockVertexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count, CKRST_LOCKFLAGS) override {
        Resource *resource = Buffer(handle);
        if (!resource || !resource->Live || resource->Type != CKRST_OBJ_VERTEXBUFFER) return nullptr;
        EXPECT_LE(start + count, resource->Vertex.m_MaxVertexCount);
        if (start + count > resource->Vertex.m_MaxVertexCount) return nullptr;
        return resource->Bytes + start * resource->Vertex.m_VertexSize;
    }
    void *LockIndexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count, CKRST_LOCKFLAGS) override {
        Resource *resource = Buffer(handle);
        if (!resource || !resource->Live || resource->Type != CKRST_OBJ_INDEXBUFFER) return nullptr;
        EXPECT_LE(start + count, resource->Index.m_MaxIndexCount);
        if (start + count > resource->Index.m_MaxIndexCount) return nullptr;
        return resource->Words + start;
    }
    CKBOOL UnlockVertexBuffer(CKDWORD handle) override {
        Resource *resource = Buffer(handle); return resource && resource->Live;
    }
    CKBOOL UnlockIndexBuffer(CKDWORD handle) override {
        Resource *resource = Buffer(handle); return resource && resource->Live;
    }
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override {
        if (type == CKRST_OBJ_TEXTURE) return TextureTestObserver::DeleteObject(handle, type);
        Resource *resource = Buffer(handle);
        if (!resource || !resource->Live || resource->Type != type) return FALSE;
        ++resource->Deletes; resource->Live = FALSE; return TRUE;
    }
    Draw *Begin(CKBOOL hardware) {
        ++Attempts;
        if (ThrowNextDraw) { ThrowNextDraw = FALSE; throw 101; }
        EXPECT_LT(Count, 32); if (Count >= 32) return nullptr;
        Draw &draw = Draws[Count++]; draw.Hardware = hardware;
        draw.World = Matrices[VXMATRIX_WORLD]; draw.View = Matrices[VXMATRIX_VIEW];
        draw.Projection = Matrices[VXMATRIX_PROJECTION];
        draw.MaterialR = Material.Diffuse.r;
        draw.Lighting = States[VXRENDERSTATE_LIGHTING]; draw.Cull = States[VXRENDERSTATE_CULLMODE];
        draw.ViewWidth = Viewport.ViewWidth;
        for (int i = 0; i < 2; ++i) draw.Textures[i] = Textures[i];
        return &draw;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        if (!indices || !data || count != 3) return FALSE;
        Draw *draw = Begin(FALSE); if (!draw) return FALSE;
        draw->Flags = data->Flags;
        for (int i = 0; i < 3; ++i) {
            const int vertex = indices[i]; EXPECT_LT(vertex, data->VertexCount);
            if (vertex >= data->VertexCount) return FALSE;
            memcpy(&draw->Positions[i], static_cast<CKBYTE *>(data->PositionPtr) + vertex * data->PositionStride, sizeof(VxVector));
            for (int stage = 0; stage < 2; ++stage) {
                if ((data->Flags & CKRST_DP_STAGESMASK) < CKRST_DP_STAGE(stage)) continue;
                void *uv = stage ? data->TexCoordPtrs[stage - 1] : data->TexCoordPtr;
                CKDWORD stride = stage ? data->TexCoordStrides[stage - 1] : data->TexCoordStride;
                if (uv) {
                    draw->HasUV[stage] = TRUE; draw->UVCount = stage + 1;
                    memcpy(&draw->UV[stage][i], static_cast<CKBYTE *>(uv) + vertex * stride, sizeof(Vx2DVector));
                }
            }
        }
        return TRUE;
    }
    CKBOOL Hardware(CKDWORD vb, CKDWORD ib, CKDWORD base, CKWORD *indices, int count) {
        Resource *vertices = Buffer(vb);
        EXPECT_NE(nullptr, vertices);
        if (!vertices) return FALSE;
        EXPECT_TRUE(vertices->Live); EXPECT_EQ(CKRST_OBJ_VERTEXBUFFER, vertices->Type);
        if (!vertices->Live) return FALSE;
        CKRSTVertexLayout layout;
        CKRSTGetVertexLayout(vertices->Vertex.m_VertexFormat, vertices->Vertex.m_TexcoordDims, &layout);
        EXPECT_EQ(vertices->Vertex.m_VertexSize, layout.Stride);
        EXPECT_EQ(3, count); if (count != 3) return FALSE;
        Draw *draw = Begin(TRUE); if (!draw) return FALSE;
        draw->VB = vb; draw->IB = ib; draw->Flags = vertices->Vertex.m_VertexFormat;
        draw->UVCount = layout.TexcoordCount;
        for (int i = 0; i < 3; ++i) {
            const CKDWORD vertex = base + indices[i];
            EXPECT_LT(vertex, vertices->Vertex.m_MaxVertexCount);
            if (vertex >= vertices->Vertex.m_MaxVertexCount) return FALSE;
            const CKBYTE *bytes = vertices->Bytes + vertex * layout.Stride;
            memcpy(&draw->Positions[i], bytes + layout.PositionOffset, sizeof(VxVector));
            for (int stage = 0; stage < 2 && stage < layout.TexcoordCount; ++stage) {
                draw->HasUV[stage] = TRUE;
                memcpy(&draw->UV[stage][i], bytes + layout.TexcoordOffset[stage], sizeof(Vx2DVector));
            }
        }
        return TRUE;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD base, CKDWORD,
                           CKWORD *indices, int count) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); return Hardware(vb, 0, base, indices, count);
    }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib, CKDWORD,
                             CKDWORD, CKDWORD first, int count) override {
        EXPECT_EQ(VX_TRIANGLELIST, type);
        Resource *indices = Buffer(ib); EXPECT_NE(nullptr, indices);
        if (!indices) return FALSE;
        EXPECT_TRUE(indices->Live); EXPECT_EQ(CKRST_OBJ_INDEXBUFFER, indices->Type);
        if (!indices->Live) return FALSE;
        EXPECT_LE(first + count, indices->Index.m_MaxIndexCount);
        if (first + count > indices->Index.m_MaxIndexCount) return FALSE;
        return Hardware(vb, ib, 0, indices->Words + first, count);
    }
    void ResetDraws() { Count = Attempts = 0; }
};

struct SameContextCall {
    SameContextMesh *Nested = nullptr;
    RCKRenderContext *Device = nullptr;
    RCK3dEntity *Entity = nullptr;
    CKMaterialGroup *Group = nullptr;
    VxDrawPrimitiveData *Data = nullptr;
    int Stage = 0, TriggerStage = 1, TriggerCall = 1, Calls = 0, Entries = 0, Result = 0;
    CKBOOL Fired = FALSE, Direct = FALSE, CatchThrow = FALSE, Caught = FALSE;
    void Enter() {
        if (Fired || Stage != TriggerStage || ++Calls != TriggerCall) return;
        Fired = TRUE; ++Entries;
        try {
            Result = Direct ? Nested->RenderGroup(Device, Group, nullptr, Data)
                            : Nested->DefaultRender(Device, Entity);
        } catch (int value) {
            if (!CatchThrow) throw;
            EXPECT_EQ(101, value); Caught = TRUE;
        }
    }
};

int SameContextCallback(CKRenderContext *, CKMaterial *, void *argument) {
    static_cast<SameContextCall *>(argument)->Enter(); return FALSE;
}
void SameContextPostCallback(CKRenderContext *, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    static_cast<SameContextCall *>(argument)->Enter();
}
int PaintNestedContext(CKRenderContext *context, CKMaterial *, void *) {
    RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
    VxMatrix world = VxMatrix::Identity(), view = VxMatrix::Identity(), projection = VxMatrix::Identity();
    world[3][0] = 99; view[3][1] = 77; projection[3][2] = -88;
    device->SetWorldTransformationMatrix(world); device->SetViewTransformationMatrix(view);
    device->SetProjectionTransformationMatrix(projection); device->m_ProjectionUpdated = FALSE;
    device->m_ViewportData.ViewWidth = 7;
    device->m_RasterizerContext->SetViewport(&device->m_ViewportData);
    device->m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    return FALSE;
}
void OwningSameContextPre(CKRenderContext *context, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
    VxMatrix world = VxMatrix::Identity(); world[3][0] = 9;
    device->SetWorldTransformationMatrix(world);
    device->m_RasterizerContext->SetTexture(901, 0);
    device->m_RasterizerContext->SetTexture(902, 1);
    device->m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    device->m_RasterizerContext->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
    device->m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
    device->m_RasterizerContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CW);
    static_cast<SameContextCall *>(argument)->Enter();
}
class SameContextMaterial : public RCKMaterial {
public:
    SameContextCall *Call = nullptr;
    explicit SameContextMaterial(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        if (!Call) return RCKMaterial::SetAsCurrent(context, lit, stage);
        struct RestoreStage {
            SameContextCall &Call;
            int Old;
            RestoreStage(SameContextCall &call, int stage) : Call(call), Old(call.Stage) { Call.Stage = stage; }
            ~RestoreStage() { Call.Stage = Old; }
        } restore(*Call, stage);
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
};

class MeshSameContextReentryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    SameContextObserver State;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
        VxMatrix world = VxMatrix::Identity(), view = VxMatrix::Identity(), projection = VxMatrix::Identity();
        world[3][0] = 1.25f; view[3][1] = 2.5f; projection[3][2] = -4.25f;
        Device->SetWorldTransformationMatrix(world); Device->SetViewTransformationMatrix(view);
        Device->SetProjectionTransformationMatrix(projection); Device->m_ProjectionUpdated = TRUE;
        Device->m_ViewportData.ViewWidth = 64; Device->m_ViewportData.ViewHeight = 48;
        State.SetViewport(&Device->m_ViewportData);
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Pixel(RCKTexture &texture, CKDWORD value) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *bytes = texture.LockSurfacePtr(0);
        ASSERT_NE(nullptr, bytes); memcpy(bytes, &value, sizeof(value));
        texture.ReleaseSurfacePtr(0); texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static void Geometry(SameContextMesh &mesh, CKMaterial &material, float x = 0, CKMaterial *second = nullptr) {
        const int faces = second ? 2 : 1;
        ASSERT_TRUE(mesh.SetVertexCount(3 * faces)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int f = 0; f < faces; ++f) {
            mesh.SetFaceVertexIndex(f, f * 3, f * 3 + 1, f * 3 + 2);
            mesh.SetFaceMaterial(f, f ? second : &material);
            for (int v = 0; v < 3; ++v) {
                VxVector point(x + 10.0f * f + (v == 1 ? 1.0f : 0), v == 2 ? 1.0f : 0, 1);
                VxVector normal(.25f, .5f, .75f);
                mesh.SetVertexPosition(3 * f + v, &point); mesh.SetVertexNormal(3 * f + v, &normal);
                mesh.SetVertexTextureCoordinates(3 * f + v, .125f + v * .125f, .25f, -1);
            }
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static void Channel(SameContextMesh &mesh, SameContextMaterial &material, RCKTexture &texture) {
        Pixel(texture, 0xFF445566); material.SetTexture(0, &texture);
        const int c = mesh.AddChannel(&material, FALSE); ASSERT_GE(c, 0); mesh.ActivateChannel(c, TRUE);
        mesh.SetChannelSourceBlend(c, VXBLEND_ZERO); mesh.SetChannelDestBlend(c, VXBLEND_SRCCOLOR);
        for (int v = 0; v < mesh.GetVertexCount(); ++v)
            mesh.SetVertexTextureCoordinates(v, .625f + (v % 3) * .0625f, .875f, c);
    }
    void Callback(SameContextMaterial &material, SameContextCall &call, SameContextMesh &nested) {
        call.Nested = &nested; call.Device = Device;
        material.Call = &call; material.SetCallback(SameContextCallback, &call);
    }
    static VxDrawPrimitiveData Packet(SameContextMesh &mesh) {
        VxDrawPrimitiveData data = {}; data.VertexCount = mesh.GetVertexCount();
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
        data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride);
        data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        return data;
    }
    void Warm(SameContextMesh &mesh) {
        for (int i = 0; i < 4; ++i) ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
        RCKMeshBufferState buffers = {}; mesh.GetRasterizerBufferState(buffers);
        ASSERT_NE(0u, buffers.VertexBuffer); ASSERT_TRUE(buffers.VertexBufferReady);
        ASSERT_NE(nullptr, State.Buffer(buffers.VertexBuffer));
        ASSERT_TRUE(State.Buffer(buffers.VertexBuffer)->Live);
        State.ResetDraws();
    }
    static void GeometryDraw(const SameContextObserver::Draw &draw, float x, CKBOOL channel = FALSE) {
        EXPECT_FLOAT_EQ(x, draw.Positions[0].x); EXPECT_FLOAT_EQ(x + 1, draw.Positions[1].x);
        EXPECT_FLOAT_EQ(1, draw.Positions[2].y); ASSERT_TRUE(draw.HasUV[0]);
        EXPECT_FLOAT_EQ(.125f, draw.UV[0][0].x); EXPECT_FLOAT_EQ(.25f, draw.UV[0][1].x);
        if (channel) {
            ASSERT_TRUE(draw.HasUV[1]); EXPECT_GE(draw.UVCount, 2);
            EXPECT_FLOAT_EQ(.625f, draw.UV[1][0].x); EXPECT_FLOAT_EQ(.6875f, draw.UV[1][1].x);
        }
    }
    static void OuterTransforms(const SameContextObserver::Draw &draw) {
        EXPECT_FLOAT_EQ(1.25f, draw.World[3][0]); EXPECT_FLOAT_EQ(2.5f, draw.View[3][1]);
        EXPECT_FLOAT_EQ(-4.25f, draw.Projection[3][2]); EXPECT_EQ(64u, draw.ViewWidth);
    }
    void CachedTransforms() {
        EXPECT_FLOAT_EQ(1.25f, Device->GetWorldTransformationMatrix()[3][0]);
        EXPECT_FLOAT_EQ(2.5f, Device->GetViewTransformationMatrix()[3][1]);
        EXPECT_FLOAT_EQ(-4.25f, Device->GetProjectionTransformationMatrix()[3][2]);
        EXPECT_TRUE(Device->m_ProjectionUpdated); EXPECT_EQ(64u, Device->m_ViewportData.ViewWidth);
    }
    void BuffersUnchanged(SameContextMesh &mesh, const RCKMeshBufferState &before) {
        RCKMeshBufferState after = {}; mesh.GetRasterizerBufferState(after);
        EXPECT_EQ(before.VertexBuffer, after.VertexBuffer); EXPECT_EQ(before.IndexBuffer, after.IndexBuffer);
        EXPECT_EQ(before.VertexBufferVertexFormat, after.VertexBufferVertexFormat);
        EXPECT_EQ(before.VertexBufferStride, after.VertexBufferStride);
        const SameContextObserver::Resource *vertex = State.Buffer(before.VertexBuffer);
        ASSERT_NE(nullptr, vertex); EXPECT_TRUE(vertex->Live); EXPECT_EQ(0, vertex->Deletes);
        if (before.IndexBuffer) {
            const SameContextObserver::Resource *index = State.Buffer(before.IndexBuffer);
            ASSERT_NE(nullptr, index); EXPECT_TRUE(index->Live); EXPECT_EQ(0, index->Deletes);
        }
    }
};
}

TEST_F(MeshSameContextReentryTest, PositiveStageOtherMeshDefaultRestoresMaterialTransformsAndTexturePrefix) {
    SameContextMesh outer(Context), inner(Context); SameContextMaterial base(Context), layer(Context); RCKMaterial innerMaterial(Context);
    RCKTexture primary(Context), texture(Context); Pixel(primary, 0xFF112233); base.SetTexture(0, &primary);
    base.SetDiffuse(VxColor(.2f, .3f, .4f, 1.0f)); innerMaterial.SetDiffuse(VxColor(.8f, .7f, .6f, 1.0f));
    innerMaterial.SetCallback(PaintNestedContext, nullptr);
    Geometry(outer, base); Geometry(inner, innerMaterial, 20); Channel(outer, layer, texture);
    SameContextCall call; Callback(layer, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); EXPECT_EQ(1, call.Entries); EXPECT_EQ(1, call.Result);
    ASSERT_EQ(2, State.Count); GeometryDraw(State.Draws[0], 20); GeometryDraw(State.Draws[1], 0, TRUE);
    EXPECT_FLOAT_EQ(99, State.Draws[0].World[3][0]); EXPECT_FLOAT_EQ(.8f, State.Draws[0].MaterialR);
    EXPECT_FLOAT_EQ(.2f, State.Draws[1].MaterialR); EXPECT_TRUE(State.Draws[1].Lighting);
    EXPECT_NE(0u, State.Draws[1].Textures[0]); EXPECT_NE(0u, State.Draws[1].Textures[1]);
    OuterTransforms(State.Draws[1]); CachedTransforms();
}

TEST_F(MeshSameContextReentryTest, PostSubmeshOtherMeshDefaultDoesNotAlterTheNextGroupsTransforms) {
    SameContextMesh outer(Context), inner(Context); SameContextMaterial base(Context); RCKMaterial second(Context), innerMaterial(Context);
    innerMaterial.SetCallback(PaintNestedContext, nullptr); Geometry(outer, base, 0, &second); Geometry(inner, innerMaterial, 20);
    SameContextCall call; call.Nested = &inner; call.Device = Device; call.TriggerStage = 0;
    ASSERT_TRUE(outer.AddSubMeshPostRenderCallBack(SameContextPostCallback, &call, FALSE));
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); EXPECT_EQ(1, call.Entries);
    ASSERT_EQ(3, State.Count); GeometryDraw(State.Draws[0], 0); GeometryDraw(State.Draws[1], 20); GeometryDraw(State.Draws[2], 10);
    EXPECT_FLOAT_EQ(99, State.Draws[1].World[3][0]); OuterTransforms(State.Draws[2]); CachedTransforms();
}

TEST_F(MeshSameContextReentryTest, PositiveStageDirectOtherGroupRestoresTheSameContextCallerState) {
    SameContextMesh outer(Context), inner(Context); SameContextMaterial base(Context), layer(Context); RCKMaterial innerMaterial(Context);
    RCKTexture primary(Context), texture(Context); Pixel(primary, 0xFF112233); base.SetTexture(0, &primary);
    base.SetDiffuse(VxColor(.2f, .3f, .4f, 1.0f)); innerMaterial.SetDiffuse(VxColor(.8f, .7f, .6f, 1.0f));
    innerMaterial.SetCallback(PaintNestedContext, nullptr);
    Geometry(outer, base); Geometry(inner, innerMaterial, 20); Channel(outer, layer, texture);
    ASSERT_EQ(1, inner.CreateRenderGroups()); VxDrawPrimitiveData packet = Packet(inner);
    SameContextCall call; call.Direct = TRUE; call.Group = inner.Group(&innerMaterial); call.Data = &packet;
    ASSERT_NE(nullptr, call.Group); Callback(layer, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); EXPECT_EQ(1, call.Entries);
    ASSERT_EQ(2, State.Count); GeometryDraw(State.Draws[0], 20); GeometryDraw(State.Draws[1], 0, TRUE);
    EXPECT_FLOAT_EQ(.2f, State.Draws[1].MaterialR); OuterTransforms(State.Draws[1]); CachedTransforms();
}

TEST_F(MeshSameContextReentryTest, SameMeshNestedSoftwareDeclarationCannotReplaceTheInFlightTwoUVHardwareBuffers) {
    SameContextMesh mesh(Context); SameContextMaterial base(Context), layer(Context); RCKTexture primary(Context), texture(Context);
    Pixel(primary, 0xFF112233); base.SetTexture(0, &primary); Geometry(mesh, base); Channel(mesh, layer, texture); Warm(mesh);
    RCKMeshBufferState before = {}; mesh.GetRasterizerBufferState(before);
    RCK3dEntity noChannels(Context); noChannels.m_MoveableFlags &= ~VX_MOVEABLE_RENDERCHANNELS;
    SameContextCall call; call.TriggerStage = 0; call.TriggerCall = 2; call.Entity = &noChannels; Callback(base, call, mesh);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(1, call.Entries); EXPECT_EQ(1, call.Result);
    ASSERT_EQ(2, State.Count); EXPECT_FALSE(State.Draws[0].Hardware); EXPECT_TRUE(State.Draws[1].Hardware);
    EXPECT_EQ(1, State.Draws[0].UVCount); EXPECT_EQ(before.VertexBuffer, State.Draws[1].VB);
    GeometryDraw(State.Draws[0], 0); GeometryDraw(State.Draws[1], 0, TRUE);
    BuffersUnchanged(mesh, before); OuterTransforms(State.Draws[1]);
}

TEST_F(MeshSameContextReentryTest, OtherHotMeshKeepsDistinctLiveBuffersAndRestoresOuterHardwareTransforms) {
    SameContextMesh outer(Context), inner(Context); SameContextMaterial base(Context), layer(Context); RCKMaterial innerMaterial(Context);
    RCKTexture primary(Context), texture(Context); Pixel(primary, 0xFF112233); base.SetTexture(0, &primary);
    Geometry(outer, base); Channel(outer, layer, texture); Geometry(inner, innerMaterial, 20);
    Warm(outer); Warm(inner);
    RCKMeshBufferState before = {}, innerBefore = {}; outer.GetRasterizerBufferState(before); inner.GetRasterizerBufferState(innerBefore);
    ASSERT_NE(before.VertexBuffer, innerBefore.VertexBuffer);
    innerMaterial.SetCallback(PaintNestedContext, nullptr);
    SameContextCall call; call.TriggerStage = 0; call.TriggerCall = 2; Callback(base, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); EXPECT_EQ(1, call.Entries);
    ASSERT_EQ(2, State.Count); GeometryDraw(State.Draws[0], 20); GeometryDraw(State.Draws[1], 0, TRUE);
    EXPECT_TRUE(State.Draws[1].Hardware); EXPECT_EQ(before.VertexBuffer, State.Draws[1].VB);
    if (State.Draws[0].Hardware) EXPECT_EQ(innerBefore.VertexBuffer, State.Draws[0].VB);
    BuffersUnchanged(outer, before); BuffersUnchanged(inner, innerBefore);
    OuterTransforms(State.Draws[1]); CachedTransforms();
}

TEST_F(MeshSameContextReentryTest, CaughtNestedSameMeshDrawExceptionRetainsTheOuterHardwareLayoutAndGeometry) {
    SameContextMesh mesh(Context); SameContextMaterial base(Context), layer(Context); RCKTexture primary(Context), texture(Context);
    Pixel(primary, 0xFF112233); base.SetTexture(0, &primary); Geometry(mesh, base); Channel(mesh, layer, texture); Warm(mesh);
    RCKMeshBufferState before = {}; mesh.GetRasterizerBufferState(before);
    RCK3dEntity noChannels(Context); noChannels.m_MoveableFlags &= ~VX_MOVEABLE_RENDERCHANNELS;
    SameContextCall call; call.TriggerStage = 0; call.TriggerCall = 2; call.Entity = &noChannels; call.CatchThrow = TRUE;
    Callback(base, call, mesh); State.ThrowNextDraw = TRUE;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(1, call.Entries); EXPECT_TRUE(call.Caught);
    EXPECT_EQ(2, State.Attempts); ASSERT_EQ(1, State.Count);
    EXPECT_TRUE(State.Draws[0].Hardware); EXPECT_EQ(before.VertexBuffer, State.Draws[0].VB);
    GeometryDraw(State.Draws[0], 0, TRUE); BuffersUnchanged(mesh, before); OuterTransforms(State.Draws[0]);
}

TEST_F(MeshSameContextReentryTest, OwningPreCallbackRetainsItsOwnEntryStateAfterOtherMeshDraw) {
    SameContextMesh outer(Context), inner(Context); SameContextMaterial base(Context); RCKMaterial innerMaterial(Context);
    Geometry(outer, base); Geometry(inner, innerMaterial, 20); innerMaterial.SetCallback(PaintNestedContext, nullptr);
    SameContextCall call; call.Nested = &inner; call.Device = Device; call.TriggerStage = 0;
    ASSERT_TRUE(outer.AddSubMeshPreRenderCallBack(OwningSameContextPre, &call, FALSE));
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); EXPECT_EQ(1, call.Entries);
    ASSERT_EQ(2, State.Count); GeometryDraw(State.Draws[0], 20); GeometryDraw(State.Draws[1], 0);
    EXPECT_FLOAT_EQ(99, State.Draws[0].World[3][0]); EXPECT_FLOAT_EQ(9, State.Draws[1].World[3][0]);
    EXPECT_FLOAT_EQ(9, Device->GetWorldTransformationMatrix()[3][0]);
    EXPECT_EQ(901u, State.Draws[1].Textures[0]); EXPECT_EQ(902u, State.Draws[1].Textures[1]);
    EXPECT_TRUE(State.Draws[1].Lighting); EXPECT_EQ(VXCULL_CW, State.Draws[1].Cull);
    EXPECT_FLOAT_EQ(2.5f, State.Draws[1].View[3][1]); EXPECT_FLOAT_EQ(-4.25f, State.Draws[1].Projection[3][2]);
    EXPECT_EQ(64u, State.Draws[1].ViewWidth);
}
