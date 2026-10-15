#include "TextureTestObserver.h"
#include "MeshHardwareObserver.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKVertexBuffer.h"

namespace {
// One mesh or public vertex buffer is resident in each case. Handles and bytes
// belong to the target interface, never to the observation adapter.
class IdentityObserver : public TextureTestObserver {
public:
    MeshHardwareObserver Buffers;
    int SoftwareDraws = 0, HardwareDraws = 0;
    VxVector Positions[3];
    Vx2DVector UV[3];
    CKDWORD LastVB = 0, LastIB = 0;
    IdentityObserver() {
        SetContextDesc(nullptr, 3, 4, 320, 200, 32, TRUE, 60, 24, 8);
        m_FrameNumber = 37;
    }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *data, CKDWORD *handle) override {
        return Buffers.CreateVertexBuffer(desc, data, handle);
    }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *desc, const void *data, CKDWORD *handle) override {
        return Buffers.CreateIndexBuffer(desc, data, handle);
    }
    CKBOOL GetVertexBufferDesc(CKDWORD handle, CKVertexBufferDesc *desc) const override {
        return Buffers.GetVertexBufferDesc(handle, desc);
    }
    CKBOOL GetIndexBufferDesc(CKDWORD handle, CKIndexBufferDesc *desc) const override {
        return Buffers.GetIndexBufferDesc(handle, desc);
    }
    void *LockVertexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count, CKRST_LOCKFLAGS) override {
        ++Buffers.VertexLocks;
        EXPECT_EQ(Buffers.VertexHandle, handle);
        EXPECT_LE(start + count, Buffers.VertexDesc.m_MaxVertexCount);
        const size_t end = static_cast<size_t>(start + count) * Buffers.VertexDesc.m_VertexSize;
        EXPECT_LE(end, sizeof(Buffers.VertexBytes));
        if (handle != Buffers.VertexHandle || end > sizeof(Buffers.VertexBytes)) return nullptr;
        return Buffers.VertexBytes + start * Buffers.VertexDesc.m_VertexSize;
    }
    void *LockIndexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count, CKRST_LOCKFLAGS flags) override {
        return Buffers.LockIndexBuffer(handle, start, count, flags);
    }
    CKBOOL UnlockVertexBuffer(CKDWORD handle) override { return Buffers.UnlockVertexBuffer(handle); }
    CKBOOL UnlockIndexBuffer(CKDWORD handle) override { return Buffers.UnlockIndexBuffer(handle); }
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override {
        return type == CKRST_OBJ_TEXTURE ? TextureTestObserver::DeleteObject(handle, type)
                                       : Buffers.DeleteObject(handle, type);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        if (!data || count != 3) return FALSE;
        ++SoftwareDraws;
        for (int i = 0; i < 3; ++i) {
            const CKDWORD vertex = indices ? indices[i] : i;
            memcpy(&Positions[i], static_cast<CKBYTE *>(data->PositionPtr) + vertex * data->PositionStride, sizeof(VxVector));
            if (data->TexCoordPtr)
                memcpy(&UV[i], static_cast<CKBYTE *>(data->TexCoordPtr) + vertex * data->TexCoordStride, sizeof(Vx2DVector));
        }
        return TRUE;
    }
    CKBOOL Decode(CKDWORD vb, CKDWORD ib, CKDWORD start, CKDWORD vertexCount, CKWORD *indices, int count) {
        EXPECT_EQ(Buffers.VertexHandle, vb); EXPECT_NE(0u, vb);
        CKRSTVertexLayout layout;
        CKRSTGetVertexLayout(Buffers.VertexDesc.m_VertexFormat, Buffers.VertexDesc.m_TexcoordDims, &layout);
        EXPECT_EQ(layout.Stride, Buffers.VertexDesc.m_VertexSize);
        EXPECT_GE(layout.TexcoordCount, 1);
        const int delivered = indices ? count : static_cast<int>(vertexCount);
        EXPECT_EQ(3, delivered);
        if (vb != Buffers.VertexHandle || !vb || delivered != 3 || layout.TexcoordCount < 1) return FALSE;
        ++HardwareDraws; LastVB = vb; LastIB = ib;
        for (int i = 0; i < 3; ++i) {
            const CKDWORD vertex = start + (indices ? indices[i] : i);
            EXPECT_LT(vertex, Buffers.VertexDesc.m_MaxVertexCount);
            if (vertex >= Buffers.VertexDesc.m_MaxVertexCount) return FALSE;
            const CKBYTE *bytes = Buffers.VertexBytes + vertex * layout.Stride;
            memcpy(&Positions[i], bytes + layout.PositionOffset, sizeof(VxVector));
            memcpy(&UV[i], bytes + layout.TexcoordOffset[0], sizeof(Vx2DVector));
        }
        return TRUE;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD start, CKDWORD vertices, CKWORD *indices, int count) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); return Decode(vb, 0, start, vertices, indices, count);
    }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib, CKDWORD, CKDWORD vertices, CKDWORD first, int count) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(Buffers.IndexHandle, ib);
        EXPECT_LE(first + count, Buffers.IndexDesc.m_MaxIndexCount);
        if (ib != Buffers.IndexHandle || first + count > Buffers.IndexDesc.m_MaxIndexCount) return FALSE;
        return Decode(vb, ib, 0, vertices, Buffers.IndexWords + first, count);
    }
};

class IdentityTask {
public:
    virtual ~IdentityTask() {}
    virtual void Run(RCKRenderContext *device) = 0;
    CKRasterizerContext *Seen = nullptr;
    int Entries = 0;
};
CKBOOL IdentityCallback(CKRenderContext *context, CKMaterial *, void *argument) {
    IdentityTask *task = static_cast<IdentityTask *>(argument);
    RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
    task->Seen = device->m_RasterizerContext; ++task->Entries;
    task->Run(device); return TRUE;
}

class RasterizerCallbackIdentityTest : public TextureTestWorld {
protected:
    IdentityObserver Target;
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        Device->DetachAll(); Device->m_RasterizerContext = &Target;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Target, 0));
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 2;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 2;
    }
    void Execute(IdentityTask &task) {
        RCKMaterial material(Context); material.SetCallback(IdentityCallback, &task);
        ASSERT_TRUE(material.SetAsCurrent(Device, TRUE, 0));
        EXPECT_EQ(&Target, Device->m_RasterizerContext);
    }
    void SubmittedTriangle() {
        EXPECT_FLOAT_EQ(2.0f, Target.Positions[0].x);
        EXPECT_FLOAT_EQ(6.0f, Target.Positions[1].y);
        EXPECT_FLOAT_EQ(10.0f, Target.Positions[2].z);
        EXPECT_FLOAT_EQ(.125f, Target.UV[0].x);
        EXPECT_FLOAT_EQ(.375f, Target.UV[2].x);
        EXPECT_FLOAT_EQ(.75f, Target.UV[1].y);
    }
    static void Geometry(RCKMesh &mesh, CKMaterial &material) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceMaterial(0, &material);
        for (int i = 0; i < 3; ++i) {
            VxVector point(2.0f + i * 3, 3.0f + i * 3, 4.0f + i * 3);
            VxVector normal(0.0f, 0.0f, 1.0f);
            mesh.SetVertexPosition(i, &point); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, .125f + i * .125f, .75f, -1);
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
};
}

TEST_F(RasterizerCallbackIdentityTest, TexturePixelsAndResidencySurviveNestedCallbacksWithoutDuplicateResources) {
    TextureTestProbe texture(Context);
    const CKDWORD pixels[] = {0xFF123456, 0xFF789ABC}; Pixels(texture, pixels, 2, 1);
    texture.SetDesiredVideoFormat(_32_ARGB8888);
    struct TextureTask : IdentityTask {
        TextureTestProbe *Texture = nullptr;
        IdentityObserver *Target = nullptr;
        RCKRenderManager *Manager = nullptr;
        CKContext *Context = nullptr;
        CKBOOL Delete = FALSE, Nested = FALSE;
        CKRasterizerContext *ExpectedObserved = nullptr;
        void Run(RCKRenderContext *device) override {
            if (ExpectedObserved) EXPECT_EQ(ExpectedObserved, device->m_RasterizerContext);
            if (Delete) {
                ASSERT_TRUE(Manager->DeleteTextureObject(Texture, device->m_RasterizerContext));
                EXPECT_EQ(Target, Texture->SelectedContext()); EXPECT_EQ(0, Texture->GetRstTextureIndex());
                return;
            }
            ASSERT_TRUE(Texture->SetAsCurrent(device, FALSE, Nested ? 1 : 0));
            EXPECT_EQ(Target, Texture->SelectedContext());
            if (!Nested) {
                TextureTask inner; inner.Texture = Texture; inner.Target = Target; inner.Nested = TRUE;
                inner.ExpectedObserved = device->m_RasterizerContext;
                RCKMaterial material(Context); material.SetCallback(IdentityCallback, &inner);
                ASSERT_TRUE(material.SetAsCurrent(device, TRUE, 0)); EXPECT_EQ(1, inner.Entries);
                EXPECT_EQ(Texture->GetRstTextureIndex(), static_cast<int>(Target->Textures[1]));
            }
        }
    } task;
    task.Texture = &texture; task.Target = &Target; task.Manager = Manager; task.Context = Context;
    Execute(task);
    const CKDWORD handle = texture.GetRstTextureIndex(); ASSERT_NE(0u, handle);
    EXPECT_TRUE(Target.Live[handle]); EXPECT_EQ(&Target, texture.SelectedContext());
    ASSERT_EQ(1, Target.Loads); EXPECT_EQ(0, memcmp(pixels, Target.Uploads[0].Bytes, sizeof(pixels)));
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(handle, Target.Textures[0]); EXPECT_EQ(1, Target.Creates); EXPECT_EQ(1, Target.Loads);
    Execute(task); EXPECT_EQ(1, Target.Creates); EXPECT_EQ(1, Target.Loads);
    task.Delete = TRUE; Execute(task);
    EXPECT_FALSE(Target.Live[handle]); EXPECT_EQ(1, Target.Deletes);
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0));
    EXPECT_EQ(2, Target.Creates); EXPECT_EQ(2, Target.Loads);
    EXPECT_NE(handle, static_cast<CKDWORD>(texture.GetRstTextureIndex()));
    EXPECT_EQ(0, memcmp(pixels, Target.Uploads[1].Bytes, sizeof(pixels)));
}

TEST_F(RasterizerCallbackIdentityTest, PublicVertexBufferUploadsAndDrawsThroughCanonicalResidentIdentity) {
    RCKVertexBuffer buffer(Context);
    const CKRST_DPFLAGS flags = static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_STAGES0);
    EXPECT_EQ(CK_VB_LOST, buffer.Check(Device, 3, flags, FALSE));
    struct BufferTask : IdentityTask {
        RCKVertexBuffer *Buffer = nullptr;
        IdentityObserver *Target = nullptr;
        CKBOOL Upload = TRUE;
        void Run(RCKRenderContext *device) override {
            if (Upload) {
                VxDrawPrimitiveData *data = Buffer->Lock(device, 0, 3, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
                for (int i = 0; i < 3; ++i) {
                    VxVector point(2.0f + i * 3, 3.0f + i * 3, 4.0f + i * 3);
                    Vx2DVector uv(.125f + i * .125f, .75f);
                    memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride, &point, sizeof(point));
                    memcpy(static_cast<CKBYTE *>(data->TexCoordPtr) + i * data->TexCoordStride, &uv, sizeof(uv));
                }
                Buffer->Unlock(device);
            }
            RCKVertexBufferObjectState state = {}; Buffer->GetRasterizerObjectState(state);
            ASSERT_TRUE(state.Valid); EXPECT_EQ(Target, state.Context); ASSERT_NE(0u, state.ObjectIndex);
            ASSERT_TRUE(Buffer->Draw(device, VX_TRIANGLELIST, nullptr, 0, 0, 3));
        }
    } task;
    task.Buffer = &buffer; task.Target = &Target;
    Execute(task); EXPECT_EQ(1, Target.HardwareDraws); EXPECT_EQ(0, Target.SoftwareDraws); SubmittedTriangle();
    const CKDWORD handle = Target.LastVB;
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 0, 3));
    EXPECT_EQ(2, Target.HardwareDraws); EXPECT_EQ(handle, Target.LastVB); SubmittedTriangle();
    task.Upload = FALSE; Execute(task);
    EXPECT_EQ(3, Target.HardwareDraws); EXPECT_EQ(handle, Target.LastVB); SubmittedTriangle();
    EXPECT_EQ(1, Target.Buffers.VertexCreates); EXPECT_EQ(1, Target.Buffers.VertexLocks);
    EXPECT_EQ(0, Target.Buffers.VertexDeletes);
    ASSERT_TRUE(Manager->DeleteVertexBufferObjects(&buffer));
    EXPECT_EQ(1, Target.Buffers.VertexDeletes);
}

TEST_F(RasterizerCallbackIdentityTest, NaturalMeshHardwareWarmupKeepsRealCacheIdentityAcrossCallbackLifetimes) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Geometry(mesh, material);
    struct MeshTask : IdentityTask {
        RCKMesh *Mesh = nullptr;
        IdentityObserver *Target = nullptr;
        RCKRenderManager *Manager = nullptr;
        int Draws = 4;
        void Run(RCKRenderContext *device) override {
            for (int i = 0; i < Draws; ++i) ASSERT_EQ(1, Mesh->DefaultRender(device, nullptr));
            EXPECT_TRUE(Manager->SelectMeshBuffers(Mesh, device->m_RasterizerContext));
            RCKMeshBufferState state = {}; Mesh->GetRasterizerBufferState(state);
            ASSERT_NE(0u, state.VertexBuffer); ASSERT_TRUE(state.VertexBufferReady);
            EXPECT_EQ(Target, state.Context); EXPECT_EQ(state.VertexBuffer, Target->LastVB);
        }
    } task;
    task.Mesh = &mesh; task.Target = &Target; task.Manager = Manager;
    Execute(task); ASSERT_GT(Target.HardwareDraws, 0); SubmittedTriangle();
    RCKMeshBufferState before = {}; mesh.GetRasterizerBufferState(before);
    const int creates = Target.Buffers.VertexCreates, indexCreates = Target.Buffers.IndexCreates;
    const int draws = Target.HardwareDraws;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    task.Draws = 1; Execute(task);
    RCKMeshBufferState after = {}; mesh.GetRasterizerBufferState(after);
    EXPECT_EQ(&Target, after.Context); EXPECT_EQ(before.VertexBuffer, after.VertexBuffer);
    EXPECT_EQ(before.IndexBuffer, after.IndexBuffer); EXPECT_TRUE(after.VertexBufferReady);
    EXPECT_EQ(draws + 2, Target.HardwareDraws); EXPECT_EQ(creates, Target.Buffers.VertexCreates);
    EXPECT_EQ(indexCreates, Target.Buffers.IndexCreates); EXPECT_EQ(0, Target.Buffers.VertexDeletes);
    EXPECT_EQ(before.VertexBuffer, Target.LastVB); SubmittedTriangle();
    ASSERT_TRUE(Manager->DeleteMeshBuffers(&mesh)); EXPECT_EQ(1, Target.Buffers.VertexDeletes);
}

TEST_F(RasterizerCallbackIdentityTest, RegistryDescriptorAndDiagnosticsForwardToTheRegisteredBackend) {
    struct RegistryTask : IdentityTask {
        RCKRenderManager *Manager = nullptr;
        IdentityObserver *Target = nullptr;
        void Run(RCKRenderContext *device) override {
            CKRasterizerContext *observed = device->m_RasterizerContext;
            EXPECT_TRUE(Manager->RegisterRasterizerContext(observed, 0));
            EXPECT_EQ(Manager->GetDriverDescription(Target), Manager->GetDriverDescription(observed));
            EXPECT_EQ(Target, Manager->GetFullscreenContext());
            CKRasterizerContextDesc desc = {}; ASSERT_TRUE(observed->GetDesc(&desc));
            EXPECT_EQ(320, desc.Width); EXPECT_EQ(200, desc.Height); EXPECT_EQ(3, desc.PosX); EXPECT_TRUE(desc.Fullscreen);
            CKRenderStats stats = {}; observed->GetStats(stats);
            EXPECT_EQ(37u, stats.FrameNumber); EXPECT_EQ(320, stats.Width); EXPECT_EQ(200, stats.Height);
            Manager->ForgetRasterizerContext(observed);
            EXPECT_EQ(nullptr, Manager->GetDriverDescription(Target));
            EXPECT_EQ(nullptr, Manager->GetDriverDescription(observed));
            EXPECT_EQ(nullptr, Manager->GetFullscreenContext());
        }
    } task;
    task.Manager = Manager; task.Target = &Target; Execute(task);
    EXPECT_TRUE(Manager->RegisterRasterizerContext(&Target, 0));
}
