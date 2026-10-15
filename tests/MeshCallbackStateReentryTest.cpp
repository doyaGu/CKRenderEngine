#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "TextureTestObserver.h"

namespace {
class CallbackStateMesh : public RCKMesh {
public:
    explicit CallbackStateMesh(CKContext *context) : RCKMesh(context) {}
    CKMaterialGroup *Group(CKMaterial *material) {
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i] && m_MaterialGroups[i]->m_Material == material &&
                m_MaterialGroups[i]->m_HasValidPrimitives) return m_MaterialGroups[i];
        return nullptr;
    }
};

// Each live VB/IB owns distinct bytes. Hardware observations decode actual
// submitted handles, indices and strides; this observer does not shade pixels.
class CallbackStateObserver : public TextureTestObserver {
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
        CKMaterialData MaterialPayload;
        CKViewportData ActualViewport = {};
    };
    Resource Resources[16];
    int ResourceCount = 0, Count = 0, Attempts = 0;
    CKBOOL ThrowNextDraw = FALSE;
    Draw Draws[32];
    CKBOOL RejectNextMaterial = FALSE, RejectNextViewport = FALSE;
    int MaterialRefusals = 0, ViewportRefusals = 0;
    CKBOOL SetMaterial(const CKMaterialData *material) override {
        if (RejectNextMaterial) { RejectNextMaterial = FALSE; ++MaterialRefusals; return FALSE; }
        return TextureTestObserver::SetMaterial(material);
    }
    CKBOOL SetViewport(const CKViewportData *viewport) override {
        if (RejectNextViewport) { RejectNextViewport = FALSE; ++ViewportRefusals; return FALSE; }
        return TextureTestObserver::SetViewport(viewport);
    }
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
        if (ThrowNextDraw) { ThrowNextDraw = FALSE; throw 102; }
        EXPECT_LT(Count, 32); if (Count >= 32) return nullptr;
        Draw &draw = Draws[Count++]; draw.Hardware = hardware;
        draw.World = Matrices[VXMATRIX_WORLD]; draw.View = Matrices[VXMATRIX_VIEW];
        draw.Projection = Matrices[VXMATRIX_PROJECTION];
        draw.MaterialR = Material.Diffuse.r;
        draw.MaterialPayload = Material; draw.ActualViewport = Viewport;
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

CKMaterialData CallbackPayload(float red) {
    CKMaterialData value = {};
    value.Diffuse = VxColor(red, .3125f, .4375f, .875f);
    value.Ambient = VxColor(.125f, .25f, .375f, 1.0f);
    value.Specular = VxColor(.5f, .625f, .75f, 1.0f);
    value.Emissive = VxColor(.0625f, .1875f, .3125f, 1.0f);
    value.SpecularPower = 17.0f;
    return value;
}
CKViewportData CallbackViewport(CKDWORD width) {
    CKViewportData value;
    value.ViewX = 3; value.ViewY = 5; value.ViewWidth = width; value.ViewHeight = 72;
    value.ViewZMin = .125f; value.ViewZMax = .875f;
    return value;
}
struct CallbackStateCall {
    CallbackStateMesh *Inner = nullptr;
    RCKRenderContext *Device = nullptr;
    CallbackStateObserver *State = nullptr;
    CKMaterialGroup *Group = nullptr;
    VxDrawPrimitiveData *Data = nullptr;
    CKMaterialData Payload = CallbackPayload(.625f), AfterPayload;
    CKViewportData Viewport = CallbackViewport(96), AfterViewport;
    int Stage = 0, TriggerStage = 0, Entries = 0, Result = 0;
    CKDWORD CachedWidthAfter = 0;
    CKBOOL Fired = FALSE, Direct = FALSE, FullOwner = TRUE;
    CKBOOL CatchThrow = FALSE, Caught = FALSE, RefuseWrites = FALSE;
    void Enter() {
        if (Fired || Stage != TriggerStage) return;
        Fired = TRUE; ++Entries;
        // Always acquire the interface while inside the callback. A previously
        // cached raw backend pointer is deliberately outside this contract.
        CKRasterizerContext *rasterizer = Device->m_RasterizerContext;
        EXPECT_TRUE(rasterizer->SetMaterial(&Payload));
        EXPECT_TRUE(rasterizer->SetViewport(&Viewport));
        if (RefuseWrites) {
            CKMaterialData refused = CallbackPayload(.9375f);
            CKViewportData refusedView = CallbackViewport(123);
            State->RejectNextMaterial = TRUE; State->RejectNextViewport = TRUE;
            EXPECT_FALSE(rasterizer->SetMaterial(&refused));
            EXPECT_FALSE(rasterizer->SetViewport(&refusedView));
        }
        try {
            Result = Direct ? Inner->RenderGroup(Device, Group, nullptr, Data)
                            : Inner->DefaultRender(Device, nullptr);
        } catch (int value) {
            if (!CatchThrow) throw;
            EXPECT_EQ(102, value); Caught = TRUE;
        }
        AfterPayload = State->Material; AfterViewport = State->Viewport;
        CachedWidthAfter = Device->m_ViewportData.ViewWidth;
    }
};
int CallbackStateMaterial(CKRenderContext *, CKMaterial *, void *argument) {
    CallbackStateCall *call = static_cast<CallbackStateCall *>(argument); call->Enter();
    return call->FullOwner;
}
void CallbackStatePre(CKRenderContext *, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    static_cast<CallbackStateCall *>(argument)->Enter();
}
int CallbackInnerPaint(CKRenderContext *context, CKMaterial *, void *) {
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    CKMaterialData payload = CallbackPayload(.8125f); CKViewportData viewport = CallbackViewport(7);
    EXPECT_TRUE(state->SetMaterial(&payload)); EXPECT_TRUE(state->SetViewport(&viewport));
    return FALSE;
}
class CallbackStateMaterialClass : public RCKMaterial {
public:
    CallbackStateCall *Call = nullptr;
    explicit CallbackStateMaterialClass(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        if (!Call) return RCKMaterial::SetAsCurrent(context, lit, stage);
        struct RestoreStage {
            CallbackStateCall &Call; int Saved;
            RestoreStage(CallbackStateCall &call, int stage) : Call(call), Saved(call.Stage) { Call.Stage = stage; }
            ~RestoreStage() { Call.Stage = Saved; }
        } restore(*Call, stage);
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
};

class MeshCallbackStateReentryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    CallbackStateObserver State; CKRasterizerDriver Driver;
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
        Manager->m_Drivers[0].TextureFormats.PushBack(format); ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll(); Device->m_RasterizerContext = &State;
        Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
        Device->m_ViewportData.ViewWidth = 64; Device->m_ViewportData.ViewHeight = 48;
        ASSERT_TRUE(State.SetViewport(&Device->m_ViewportData));
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Geometry(CallbackStateMesh &mesh, CKMaterial &material, float x = 0.0f) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1)); mesh.SetFaceVertexIndex(0, 0, 1, 2);
        mesh.SetFaceMaterial(0, &material);
        for (int i = 0; i < 3; ++i) {
            VxVector point(x + (i == 1 ? 1.0f : 0.0f), i == 2 ? 1.0f : 0.0f, 1.0f), normal(.25f, .5f, .75f);
            mesh.SetVertexPosition(i, &point); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, .125f + i * .125f, .25f, -1);
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static void Channel(CallbackStateMesh &mesh, CallbackStateMaterialClass &material, RCKTexture &texture) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *bytes = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, bytes);
        CKDWORD color = 0xFF445566; memcpy(bytes, &color, 4); texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888); material.SetTexture(0, &texture);
        const int index = mesh.AddChannel(&material, FALSE); ASSERT_GE(index, 0); mesh.ActivateChannel(index, TRUE);
        mesh.SetChannelSourceBlend(index, VXBLEND_ZERO); mesh.SetChannelDestBlend(index, VXBLEND_SRCCOLOR);
        for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, .625f + i * .0625f, .875f, index);
    }
    static VxDrawPrimitiveData Packet(CallbackStateMesh &mesh) {
        VxDrawPrimitiveData data = {}; data.VertexCount = mesh.GetVertexCount();
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride); data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride); data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        return data;
    }
    void Configure(CallbackStateCall &call, CallbackStateMesh &inner) { call.Inner = &inner; call.Device = Device; call.State = &State; }
    void FullCallback(CallbackStateMaterialClass &material, CallbackStateCall &call, CallbackStateMesh &inner) {
        Configure(call, inner); material.Call = &call; material.SetCallback(CallbackStateMaterial, &call);
    }
    void Prime(CallbackStateMesh &mesh) { ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); State.ResetDraws(); }
    void Warm(CallbackStateMesh &mesh) {
        for (int i = 0; i < 4; ++i) ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
        RCKMeshBufferState state = {}; mesh.GetRasterizerBufferState(state);
        ASSERT_TRUE(state.VertexBufferReady); ASSERT_NE(0u, state.VertexBuffer);
        ASSERT_NE(nullptr, State.Buffer(state.VertexBuffer)); ASSERT_TRUE(State.Buffer(state.VertexBuffer)->Live);
        State.ResetDraws();
    }
    static void Color(const VxColor &expected, const VxColor &actual) {
        EXPECT_FLOAT_EQ(expected.r, actual.r); EXPECT_FLOAT_EQ(expected.g, actual.g);
        EXPECT_FLOAT_EQ(expected.b, actual.b); EXPECT_FLOAT_EQ(expected.a, actual.a);
    }
    static void Payload(const CKMaterialData &expected, const CKMaterialData &actual) {
        Color(expected.Diffuse, actual.Diffuse); Color(expected.Ambient, actual.Ambient);
        Color(expected.Specular, actual.Specular); Color(expected.Emissive, actual.Emissive);
        EXPECT_FLOAT_EQ(expected.SpecularPower, actual.SpecularPower);
    }
    static void Viewport(const CKViewportData &expected, const CKViewportData &actual) {
        EXPECT_EQ(expected.ViewX, actual.ViewX); EXPECT_EQ(expected.ViewY, actual.ViewY);
        EXPECT_EQ(expected.ViewWidth, actual.ViewWidth); EXPECT_EQ(expected.ViewHeight, actual.ViewHeight);
        EXPECT_FLOAT_EQ(expected.ViewZMin, actual.ViewZMin); EXPECT_FLOAT_EQ(expected.ViewZMax, actual.ViewZMax);
    }
    void Restored(const CallbackStateCall &call, const CallbackStateObserver::Draw &outer) {
        EXPECT_EQ(1, call.Entries); Payload(call.Payload, call.AfterPayload); Viewport(call.Viewport, call.AfterViewport);
        Payload(call.Payload, outer.MaterialPayload); Viewport(call.Viewport, outer.ActualViewport);
        EXPECT_EQ(64u, call.CachedWidthAfter); EXPECT_EQ(64u, Device->m_ViewportData.ViewWidth);
        EXPECT_EQ(48u, Device->m_ViewportData.ViewHeight);
        EXPECT_FLOAT_EQ(0.0f, outer.Positions[0].x); EXPECT_FLOAT_EQ(1.0f, outer.Positions[1].x);
        EXPECT_FLOAT_EQ(1.0f, outer.Positions[2].y);
    }
};
}

TEST_F(MeshCallbackStateReentryTest, FullMaterialOwnerRetainsItsActualPayloadAndViewportAfterInnerDefault) {
    CallbackStateMesh outer(Context), inner(Context); CallbackStateMaterialClass material(Context); RCKMaterial innerMaterial(Context);
    material.SetDiffuse(VxColor(.25f, .25f, .25f, 1.0f)); innerMaterial.SetDiffuse(VxColor(.8125f, .5f, .5f, 1.0f));
    Geometry(outer, material); Geometry(inner, innerMaterial, 20.0f); Prime(outer);
    innerMaterial.SetCallback(CallbackInnerPaint, nullptr); CallbackStateCall call; FullCallback(material, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.Count);
    EXPECT_FLOAT_EQ(20.0f, State.Draws[0].Positions[0].x); EXPECT_EQ(7u, State.Draws[0].ActualViewport.ViewWidth);
    Restored(call, State.Draws[1]);
}
TEST_F(MeshCallbackStateReentryTest, PreSubmeshOwnerRetainsItsActualStateAfterInnerDirectGroup) {
    CallbackStateMesh outer(Context), inner(Context); RCKMaterial material(Context), innerMaterial(Context);
    Geometry(outer, material); Geometry(inner, innerMaterial, 20.0f); Prime(outer);
    innerMaterial.SetCallback(CallbackInnerPaint, nullptr); ASSERT_EQ(1, inner.CreateRenderGroups());
    VxDrawPrimitiveData data = Packet(inner); CallbackStateCall call; Configure(call, inner);
    call.Direct = TRUE; call.Group = inner.Group(&innerMaterial); call.Data = &data; ASSERT_NE(nullptr, call.Group);
    ASSERT_TRUE(outer.AddSubMeshPreRenderCallBack(CallbackStatePre, &call, FALSE));
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.Count);
    EXPECT_EQ(1, call.Result); Restored(call, State.Draws[1]);
}
TEST_F(MeshCallbackStateReentryTest, DecliningPositiveStageCallbackKeepsItsDirectPayloadAndViewport) {
    CallbackStateMesh outer(Context), inner(Context); RCKMaterial base(Context), innerMaterial(Context);
    CallbackStateMaterialClass layer(Context); RCKTexture texture(Context);
    base.SetDiffuse(VxColor(.25f, .25f, .25f, 1.0f)); Geometry(outer, base); Geometry(inner, innerMaterial, 20.0f);
    Channel(outer, layer, texture); innerMaterial.SetCallback(CallbackInnerPaint, nullptr);
    CallbackStateCall call; call.TriggerStage = 1; call.FullOwner = FALSE; FullCallback(layer, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.Count);
    EXPECT_TRUE(State.Draws[1].HasUV[1]); EXPECT_FLOAT_EQ(.625f, State.Draws[1].UV[1][0].x);
    Restored(call, State.Draws[1]);
}
TEST_F(MeshCallbackStateReentryTest, CaughtInnerDrawExceptionRestoresTheDirectCallbackPayloadAndViewport) {
    CallbackStateMesh outer(Context), inner(Context); CallbackStateMaterialClass material(Context); RCKMaterial innerMaterial(Context);
    Geometry(outer, material); Geometry(inner, innerMaterial, 20.0f); Prime(outer);
    innerMaterial.SetCallback(CallbackInnerPaint, nullptr); CallbackStateCall call; call.CatchThrow = TRUE; FullCallback(material, call, inner);
    State.ThrowNextDraw = TRUE; ASSERT_EQ(1, outer.DefaultRender(Device, nullptr));
    EXPECT_TRUE(call.Caught); EXPECT_EQ(2, State.Attempts); ASSERT_EQ(1, State.Count); Restored(call, State.Draws[0]);
}
TEST_F(MeshCallbackStateReentryTest, RejectedDirectWritesDoNotReplaceTheLastAcceptedCallbackState) {
    CallbackStateMesh outer(Context), inner(Context); CallbackStateMaterialClass material(Context); RCKMaterial innerMaterial(Context);
    Geometry(outer, material); Geometry(inner, innerMaterial, 20.0f); Prime(outer);
    innerMaterial.SetCallback(CallbackInnerPaint, nullptr); CallbackStateCall call; call.RefuseWrites = TRUE; FullCallback(material, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.Count);
    EXPECT_EQ(1, State.MaterialRefusals); EXPECT_EQ(1, State.ViewportRefusals); Restored(call, State.Draws[1]);
}
TEST_F(MeshCallbackStateReentryTest, SuccessiveCallbackEntriesUseTheirOwnDistinctAcceptedValues) {
    CallbackStateMesh outer(Context), inner(Context); CallbackStateMaterialClass material(Context); RCKMaterial innerMaterial(Context);
    Geometry(outer, material); Geometry(inner, innerMaterial, 20.0f); Prime(outer);
    innerMaterial.SetCallback(CallbackInnerPaint, nullptr); CallbackStateCall call; FullCallback(material, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.Count); Restored(call, State.Draws[1]);
    call.Fired = FALSE; call.Entries = 0; call.Payload = CallbackPayload(.75f); call.Viewport = CallbackViewport(128); State.ResetDraws();
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.Count); Restored(call, State.Draws[1]);
}
TEST_F(MeshCallbackStateReentryTest, CallbackStateSurvivesDistinctHotMeshHardwareDrawsAndLaterReuse) {
    CallbackStateMesh outer(Context), inner(Context); CallbackStateMaterialClass material(Context); RCKMaterial innerMaterial(Context);
    Geometry(outer, material); Geometry(inner, innerMaterial, 20.0f); Warm(outer); Warm(inner);
    RCKMeshBufferState outerBefore = {}, innerBefore = {}; outer.GetRasterizerBufferState(outerBefore); inner.GetRasterizerBufferState(innerBefore);
    ASSERT_NE(outerBefore.VertexBuffer, innerBefore.VertexBuffer); const int resources = State.ResourceCount;
    innerMaterial.SetCallback(CallbackInnerPaint, nullptr); CallbackStateCall call; FullCallback(material, call, inner);
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.Count);
    EXPECT_TRUE(State.Draws[0].Hardware); EXPECT_TRUE(State.Draws[1].Hardware);
    EXPECT_EQ(innerBefore.VertexBuffer, State.Draws[0].VB); EXPECT_EQ(outerBefore.VertexBuffer, State.Draws[1].VB);
    EXPECT_FLOAT_EQ(20.0f, State.Draws[0].Positions[0].x); Restored(call, State.Draws[1]);
    RCKMeshBufferState outerAfter = {}, innerAfter = {}; outer.GetRasterizerBufferState(outerAfter); inner.GetRasterizerBufferState(innerAfter);
    EXPECT_EQ(&State, outerAfter.Context); EXPECT_EQ(&State, innerAfter.Context);
    EXPECT_EQ(outerBefore.VertexBuffer, outerAfter.VertexBuffer); EXPECT_EQ(innerBefore.VertexBuffer, innerAfter.VertexBuffer);
    EXPECT_EQ(outerBefore.IndexBuffer, outerAfter.IndexBuffer); EXPECT_EQ(innerBefore.IndexBuffer, innerAfter.IndexBuffer);
    EXPECT_EQ(resources, State.ResourceCount);
    EXPECT_TRUE(State.Buffer(outerBefore.VertexBuffer)->Live); EXPECT_EQ(0, State.Buffer(outerBefore.VertexBuffer)->Deletes);
    EXPECT_TRUE(State.Buffer(innerBefore.VertexBuffer)->Live); EXPECT_EQ(0, State.Buffer(innerBefore.VertexBuffer)->Deletes);
    material.SetCallback(nullptr, nullptr); State.ResetDraws();
    ASSERT_EQ(1, outer.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.Count);
    EXPECT_TRUE(State.Draws[0].Hardware); EXPECT_EQ(outerBefore.VertexBuffer, State.Draws[0].VB);
    EXPECT_FLOAT_EQ(0.0f, State.Draws[0].Positions[0].x); EXPECT_EQ(resources, State.ResourceCount);
}
