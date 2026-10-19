#include <gtest/gtest.h>
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
class CallbackMesh : public RCKMesh {
public:
    explicit CallbackMesh(CKContext *context) : RCKMesh(context) {}
    CKProgressiveMesh *PM() const { return m_ProgressiveMesh; }
    CKMaterialGroup *Group() { return m_MaterialGroups[0]; }
    CKCallbacksContainer *Callbacks() { return m_RenderCallbacks; }
    void AttachPM(int count, CKBOOL morph = FALSE) {
        m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = count;
        m_ProgressiveMesh->m_MorphEnabled = morph;
        m_ProgressiveMesh->m_MorphStep = 2;
        m_ProgressiveMesh->m_Reserved = -1;
        const CKDWORD parents[] = {0, 0, 0, 2, 3, 4};
        m_ProgressiveMesh->m_Data.Resize(6);
        for (int i = 0; i < 6; ++i) m_ProgressiveMesh->m_Data[i] = parents[i];
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
};

class CallbackDrawObserver : public MaterialRenderObserver {
public:
    int DrawCount = 0, VertexCount = 0, IndexCount = 0;
    CKWORD Indices[12] = {};
    VxVector Positions[8];
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type);
        EXPECT_LE(count, 12);
        EXPECT_LE(data->VertexCount, 8);
        ++DrawCount;
        VertexCount = data->VertexCount;
        IndexCount = count;
        for (int i = 0; i < count && i < 12; ++i) {
            Indices[i] = indices[i];
            EXPECT_LT(indices[i], data->VertexCount);
        }
        for (int i = 0; i < data->VertexCount && i < 8; ++i)
            memcpy(&Positions[i], static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride,
                   sizeof(VxVector));
        return TRUE;
    }
};

struct CallbackPostObservation {
    int Calls = 0;
    int PreparedCount = -1;
    CKMesh *SeenMesh = nullptr;
};

void ObserveAfterPM(CKRenderContext *, CK3dEntity *, CKMesh *mesh, void *argument) {
    CallbackPostObservation *state = static_cast<CallbackPostObservation *>(argument);
    ++state->Calls;
    state->SeenMesh = mesh;
    CKProgressiveMesh *pm = static_cast<CallbackMesh *>(mesh)->PM();
    state->PreparedCount = pm ? pm->m_Reserved : -1;
}

class ProgressiveMeshCallbackTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    CallbackDrawObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->DetachAll();
        Device->m_RasterizerContext = &State;
        Device->m_Current3dEntity = nullptr;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    void Geometry(CallbackMesh &mesh) {
        ASSERT_TRUE(mesh.SetVertexCount(6));
        ASSERT_TRUE(mesh.SetFaceCount(2));
        mesh.SetFaceVertexIndex(0, 0, 1, 5);
        mesh.SetFaceVertexIndex(1, 1, 4, 5);
        const VxVector points[] = {VxVector(0, 0, 0), VxVector(2, 0, 0), VxVector(0, 2, 0),
                                   VxVector(0, 8, 0), VxVector(12, 0, 0), VxVector(100, 4, 0)};
        for (int i = 0; i < 6; ++i) {
            VxVector position = points[i], normal(0, 0, 1);
            mesh.SetVertexPosition(i, &position);
            mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, i * 0.125f, i * 0.25f, -1);
        }
        mesh.SetFlags(mesh.GetFlags() & ~(VXMESH_STRIPIFY | VXMESH_PM_BUILDNORM));
        mesh.CreateRenderGroups();
    }
    static void ExpectIndices(CallbackMesh &mesh, int count, int a = 0, int b = 0, int c = 0) {
        ASSERT_EQ(1, mesh.Group()->m_Primitives.Size());
        const XArray<CKWORD> &indices = mesh.Group()->m_Primitives[0].m_Indices;
        ASSERT_EQ(count, indices.Size());
        if (count == 3) {
            EXPECT_EQ(a, indices[0]); EXPECT_EQ(b, indices[1]); EXPECT_EQ(c, indices[2]);
        }
    }
    static void ExpectPoint(const VxVector &actual, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x, actual.x); EXPECT_FLOAT_EQ(y, actual.y); EXPECT_FLOAT_EQ(z, actual.z);
    }
};

TEST_F(ProgressiveMeshCallbackTest, DirectCallbackRebuildsDirtyNonMorphLodAndRejectsCollapsedTriangles) {
    CallbackMesh mesh(Context);
    Geometry(mesh);
    mesh.AttachPM(4);
    int opaqueCookie = 17;
    ProgressiveMeshPreRenderCallback(nullptr, nullptr, &mesh, &opaqueCookie);
    EXPECT_TRUE(mesh.IsUpToDate());
    EXPECT_EQ(4, mesh.PM()->m_Reserved);
    EXPECT_EQ(4u, mesh.Group()->m_VertexCount);
    ExpectIndices(mesh, 3, 0, 1, 3);
    mesh.SetVerticesRendered(6);
    EXPECT_FALSE(mesh.IsUpToDate());
    ProgressiveMeshPreRenderCallback(nullptr, nullptr, &mesh, &opaqueCookie);
    ExpectIndices(mesh, 6);
    EXPECT_EQ(6, mesh.PM()->m_Reserved);
    mesh.SetVerticesRendered(2);
    ProgressiveMeshPreRenderCallback(nullptr, nullptr, &mesh, &opaqueCookie);
    EXPECT_EQ(2, mesh.PM()->m_Reserved);
    ExpectIndices(mesh, 0);
    EXPECT_FALSE(mesh.Group()->m_HasValidPrimitives);
}

TEST_F(ProgressiveMeshCallbackTest, ExplicitMeshWinsOverADifferentBorrowedDataMesh) {
    CallbackMesh mesh(Context), dataMesh(Context);
    Geometry(mesh); Geometry(dataMesh);
    mesh.AttachPM(4); dataMesh.AttachPM(5, TRUE);
    ProgressiveMeshPreRenderCallback(Device, nullptr, &mesh, &dataMesh);
    EXPECT_TRUE(mesh.IsUpToDate());
    ExpectIndices(mesh, 3, 0, 1, 3);
    EXPECT_FALSE(dataMesh.IsUpToDate());
    EXPECT_EQ(-1, dataMesh.PM()->m_Reserved);
    EXPECT_EQ(0, dataMesh.PM()->m_EdgeCollapseData.Size());
    ExpectIndices(dataMesh, 6);
}

TEST_F(ProgressiveMeshCallbackTest, NullMeshDataFallbackPreparesMorphPayloadWithoutTakingOwnership) {
    CallbackMesh mesh(Context);
    Geometry(mesh);
    mesh.AttachPM(5, TRUE);
    CKProgressiveMesh *ownedPM = mesh.PM();
    ProgressiveMeshPreRenderCallback(nullptr, nullptr, nullptr, &mesh);
    EXPECT_EQ(ownedPM, mesh.PM());
    EXPECT_TRUE(mesh.IsUpToDate());
    EXPECT_EQ(5, mesh.GetVerticesRendered());
    EXPECT_EQ(6, mesh.PM()->m_Reserved);
    ASSERT_EQ(6 * sizeof(VxVertex) / sizeof(CKDWORD), static_cast<size_t>(mesh.PM()->m_EdgeCollapseData.Size()));
    const VxVertex *prepared = reinterpret_cast<const VxVertex *>(mesh.PM()->m_EdgeCollapseData.Begin());
    ExpectPoint(prepared[4].m_Position, 6, 4, 0);
    ExpectPoint(prepared[5].m_Position, 50, 6, 0);
    EXPECT_FLOAT_EQ(0.5f, prepared[5].m_UV.x);
    EXPECT_FLOAT_EQ(1.0f, prepared[5].m_UV.y);
    VxVector original;
    mesh.GetVertexPosition(5, &original);
    ExpectPoint(original, 100, 4, 0);
    ExpectIndices(mesh, 6);
    mesh.SetVerticesRendered(3); // The caller still owns the mesh; this prepares the four-vertex tier.
    ProgressiveMeshPreRenderCallback(nullptr, nullptr, nullptr, &mesh);
    EXPECT_EQ(ownedPM, mesh.PM());
    EXPECT_EQ(4, mesh.PM()->m_Reserved);
}

TEST_F(ProgressiveMeshCallbackTest, AlreadyPreparedCallbackKeepsTheLodPayloadAndStorageStable) {
    CallbackMesh mesh(Context);
    Geometry(mesh); mesh.AttachPM(5, TRUE);
    ProgressiveMeshPreRenderCallback(nullptr, nullptr, &mesh, nullptr);
    const CKDWORD *scratch = mesh.PM()->m_EdgeCollapseData.Begin();
    const CKWORD *indices = mesh.Group()->m_Primitives[0].m_Indices.Begin();
    for (int i = 0; i < 3; ++i) {
        ProgressiveMeshPreRenderCallback(nullptr, nullptr, &mesh, nullptr);
        EXPECT_TRUE(mesh.IsUpToDate());
        EXPECT_EQ(scratch, mesh.PM()->m_EdgeCollapseData.Begin());
        EXPECT_EQ(indices, mesh.Group()->m_Primitives[0].m_Indices.Begin());
        EXPECT_EQ(6, mesh.PM()->m_Reserved);
        ExpectPoint(reinterpret_cast<const VxVertex *>(scratch)[5].m_Position, 50, 6, 0);
        ExpectIndices(mesh, 6);
    }
}

TEST_F(ProgressiveMeshCallbackTest, PlainMeshCallPreservesAuthoredGeometryAndDoesNotCreatePm) {
    CallbackMesh mesh(Context);
    Geometry(mesh);
    const CKWORD *indices = mesh.Group()->m_Primitives[0].m_Indices.Begin();
    const CKDWORD flags = mesh.GetFlags();
    ProgressiveMeshPreRenderCallback(nullptr, nullptr, &mesh, nullptr);
    EXPECT_EQ(nullptr, mesh.PM());
    EXPECT_EQ(flags, mesh.GetFlags());
    EXPECT_EQ(indices, mesh.Group()->m_Primitives[0].m_Indices.Begin());
    ExpectIndices(mesh, 6);
    VxVector original;
    mesh.GetVertexPosition(5, &original);
    ExpectPoint(original, 100, 4, 0);
}

TEST_F(ProgressiveMeshCallbackTest, RegisteredCallbackUpdatesLodBeforeRealDrawingAndPostCallbacks) {
    CallbackMesh mesh(Context), dataMesh(Context);
    Geometry(mesh); Geometry(dataMesh);
    mesh.AttachPM(4); dataMesh.AttachPM(5, TRUE);
    ASSERT_TRUE(mesh.AddPreRenderCallBack(ProgressiveMeshPreRenderCallback, &dataMesh, FALSE));
    CallbackPostObservation post;
    ASSERT_TRUE(mesh.AddPostRenderCallBack(ObserveAfterPM, &post, FALSE));
    EXPECT_EQ(CK_OK, mesh.Render(Device, nullptr));
    ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(4, State.VertexCount);
    EXPECT_EQ(3, State.IndexCount);
    EXPECT_EQ(0, State.Indices[0]); EXPECT_EQ(1, State.Indices[1]); EXPECT_EQ(3, State.Indices[2]);
    ExpectPoint(State.Positions[3], 0, 8, 0);
    EXPECT_EQ(1, post.Calls); EXPECT_EQ(&mesh, post.SeenMesh); EXPECT_EQ(4, post.PreparedCount);
    mesh.SetVerticesRendered(6);
    EXPECT_EQ(CK_OK, mesh.Render(Device, nullptr));
    EXPECT_EQ(2, State.DrawCount); EXPECT_EQ(6, State.VertexCount); EXPECT_EQ(6, State.IndexCount);
    EXPECT_EQ(2, post.Calls); EXPECT_EQ(6, post.PreparedCount);
    mesh.SetVerticesRendered(2);
    EXPECT_EQ(CK_OK, mesh.Render(Device, nullptr));
    EXPECT_EQ(2, State.DrawCount); // The callback prepared an empty lower LOD.
    EXPECT_EQ(3, post.Calls); EXPECT_EQ(2, post.PreparedCount);
    EXPECT_EQ(-1, dataMesh.PM()->m_Reserved);
    EXPECT_FALSE(dataMesh.IsUpToDate());
}

TEST_F(ProgressiveMeshCallbackTest, CreatePmRegistersTheCallbackAndDestroyPmKeepsUserCallbacks) {
    CallbackMesh mesh(Context);
    Geometry(mesh);
    ASSERT_EQ(CK_OK, mesh.CreatePM());
    ASSERT_NE(nullptr, mesh.Callbacks());
    ASSERT_EQ(1, mesh.Callbacks()->m_PreCallBacks.Size());
    const VxCallBack &entry = mesh.Callbacks()->m_PreCallBacks[0];
    EXPECT_EQ(reinterpret_cast<void *>(ProgressiveMeshPreRenderCallback), entry.callback);
    EXPECT_EQ(&mesh, entry.argument);
    CallbackPostObservation post;
    ASSERT_TRUE(mesh.AddPostRenderCallBack(ObserveAfterPM, &post, FALSE));
    mesh.SetVerticesRendered(4);
    EXPECT_EQ(CK_OK, mesh.Render(Device, nullptr));
    EXPECT_TRUE(mesh.IsUpToDate());
    EXPECT_EQ(4, mesh.PM()->m_Reserved);
    EXPECT_EQ(1, post.Calls); EXPECT_EQ(4, post.PreparedCount);
    mesh.DestroyPM();
    EXPECT_EQ(nullptr, mesh.PM());
    EXPECT_EQ(0, mesh.Callbacks()->m_PreCallBacks.Size());
    EXPECT_EQ(1, mesh.Callbacks()->m_PostCallBacks.Size());
    const int oldDraws = State.DrawCount;
    EXPECT_EQ(CK_OK, mesh.Render(Device, nullptr));
    EXPECT_GT(State.DrawCount, oldDraws);
    EXPECT_EQ(2, post.Calls); EXPECT_EQ(-1, post.PreparedCount);
}
} // namespace
