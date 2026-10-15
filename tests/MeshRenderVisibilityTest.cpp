#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKRenderObject.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct VisibilityTrace {
    int Count = 0;
    int Values[32] = {};
    CK3dEntity *Entities[32] = {};
    CKMesh *Meshes[32] = {};
    VxRect Extents[32];
    VxVector Worlds[32];
};
struct VisibilityCallback {
    VisibilityTrace *Trace;
    int Value;
};
void ObserveMesh(CKRenderContext *device, CK3dEntity *entity, CKMesh *mesh, void *argument) {
    VisibilityCallback *entry = static_cast<VisibilityCallback *>(argument);
    ASSERT_LT(entry->Trace->Count, 32);
    const int i = entry->Trace->Count++;
    entry->Trace->Values[i] = entry->Value;
    entry->Trace->Entities[i] = entity;
    entry->Trace->Meshes[i] = mesh;
    device->GetCurrentExtents(entry->Trace->Extents[i]);
    entry->Trace->Worlds[i] = device->GetWorldTransformationMatrix()[3];
}
void ObserveSubMesh(CKRenderContext *device, CK3dEntity *entity, CKMesh *mesh, CKMaterial *, void *argument) {
    ObserveMesh(device, entity, mesh, argument);
}
class VisibilityMesh : public RCKMesh {
public:
    int BoxReads = 0;
    explicit VisibilityMesh(CKContext *context) : RCKMesh(context) {}
    const VxBbox &GetLocalBox() override {
        ++BoxReads;
        return RCKMesh::GetLocalBox();
    }
};
class VisibilityDrawObserver : public MaterialRenderObserver {
public:
    VisibilityTrace *Trace = nullptr;
    int Draws = 0;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type);
        EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices);
        EXPECT_NE(nullptr, data);
        ++Draws;
        if (Trace) {
            EXPECT_LT(Trace->Count, 32);
            if (Trace->Count < 32) Trace->Values[Trace->Count++] = 50;
        }
        return TRUE;
    }
};
class MeshRenderVisibilityTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    RCK3dEntity *Entity = nullptr;
    RCK3dEntity *Other = nullptr;
    VisibilityDrawObserver State;
    VisibilityTrace Trace;
    VisibilityCallback Pre = {&Trace, 10}, Main = {&Trace, 20}, Post = {&Trace, 30};
    VisibilityCallback TemporaryPre = {&Trace, 11}, TemporaryPost = {&Trace, 31};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->DetachAll();
        Device->m_RasterizerContext = &State;
        VxRect viewport(20, 30, 220, 130);
        Device->SetViewRect(viewport);
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        Device->SetProjectionTransformationMatrix(VxMatrix::Identity());
        VxMatrix world = VxMatrix::Identity();
        world[3] = VxVector(0.25f, -0.2f, 0);
        Device->SetWorldTransformationMatrix(world);
        Device->m_Current3dEntity = nullptr;
        Entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        Other = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        ASSERT_NE(nullptr, Entity);
        ASSERT_NE(nullptr, Other);
        Entity->SetPosition3f(1000, 1000, 1000);
        SeedExtents(1000000, 1000000, -1000000, -1000000);
    }
    void TearDown() override {
        if (Device) {
            Device->m_RasterizerContext = nullptr;
            delete Device;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    void SeedExtents(float left, float top, float right, float bottom) {
        VxRect rect(left, top, right, bottom);
        Device->SetCurrentExtents(rect);
    }
    void Box(VisibilityMesh &mesh) {
        ASSERT_TRUE(mesh.SetVertexCount(2));
        VxVector minimum(-0.5f, -0.4f, 0.25f), maximum(0.5f, 0.4f, 0.75f);
        mesh.SetVertexPosition(0, &minimum);
        mesh.SetVertexPosition(1, &maximum);
        mesh.SetRenderCallBack(ObserveMesh, &Main);
    }
    void Triangle(VisibilityMesh &mesh) {
        ASSERT_TRUE(mesh.SetVertexCount(3));
        ASSERT_TRUE(mesh.SetFaceCount(1));
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
        VxVector a(0, 0, 0.5f), b(0.5f, 0, 0.5f), c(0, 0.5f, 0.5f);
        mesh.SetVertexPosition(0, &a); mesh.SetVertexPosition(1, &b); mesh.SetVertexPosition(2, &c);
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
    // RCPSS relative error <= 1.5 * 2^-12; half viewport width 100 gives
    // <= 0.036622 pixels for visible NDC, plus float arithmetic margin.
    // Projected bounds use 0.04; unchanged extents retain the 0.001 default.
    static void Rect(const VxRect &rect, float left, float top, float right, float bottom, float tolerance = 0.001f) {
        EXPECT_NEAR(left, rect.left, tolerance);
        EXPECT_NEAR(top, rect.top, tolerance);
        EXPECT_NEAR(right, rect.right, tolerance);
        EXPECT_NEAR(bottom, rect.bottom, tolerance);
    }
    void CurrentRect(float left, float top, float right, float bottom, float tolerance = 0.001f) {
        VxRect rect;
        Device->GetCurrentExtents(rect);
        Rect(rect, left, top, right, bottom, tolerance);
    }
    void Order(const int *expected, int count) {
        ASSERT_EQ(count, Trace.Count);
        for (int i = 0; i < count; ++i) EXPECT_EQ(expected[i], Trace.Values[i]);
    }
};
}

TEST_F(MeshRenderVisibilityTest, DistinctEntityProjectsLocalBoxUsingCurrentWorldAndOffsetViewport) {
    VisibilityMesh mesh(Context); Box(mesh);
    ASSERT_EQ(CK_OK, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity)));
    EXPECT_EQ(1, mesh.BoxReads);
    CurrentRect(95, 70, 195, 110, 0.04f);
    ASSERT_EQ(1, Trace.Count);
    Rect(Trace.Extents[0], 95, 70, 195, 110, 0.04f);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(Entity), Trace.Entities[0]); EXPECT_EQ(&mesh, Trace.Meshes[0]);
    EXPECT_FLOAT_EQ(0.25f, Trace.Worlds[0].x); EXPECT_FLOAT_EQ(-0.2f, Trace.Worlds[0].y);
    EXPECT_EQ(nullptr, Device->m_Current3dEntity);
    EXPECT_EQ(0, Device->m_ObjectExtents.Size());
}
TEST_F(MeshRenderVisibilityTest, ExtentsUnionPreservesAlreadyRenderedArea) {
    VisibilityMesh mesh(Context); Box(mesh); SeedExtents(40, 50, 120, 90);
    ASSERT_EQ(CK_OK, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity)));
    CurrentRect(40, 50, 195, 110, 0.04f);
}
TEST_F(MeshRenderVisibilityTest, SameCurrentEntityDoesNotRecomputeTheBoxOrExtents) {
    VisibilityMesh mesh(Context); Box(mesh); Device->m_Current3dEntity = Entity;
    SeedExtents(11, 12, 13, 14);
    ASSERT_EQ(CK_OK, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity)));
    EXPECT_EQ(0, mesh.BoxReads); CurrentRect(11, 12, 13, 14);
    EXPECT_EQ(1, Trace.Count); EXPECT_EQ(Entity, Device->m_Current3dEntity);
}
TEST_F(MeshRenderVisibilityTest, TwoNullEntityPointersPreserveExtentsWithoutBoxRead) {
    VisibilityMesh mesh(Context); Box(mesh); SeedExtents(11, 12, 13, 14);
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    EXPECT_EQ(0, mesh.BoxReads); CurrentRect(11, 12, 13, 14);
    ASSERT_EQ(1, Trace.Count); EXPECT_EQ(nullptr, Trace.Entities[0]);
}
TEST_F(MeshRenderVisibilityTest, NullEntityStillComputesExtentsWhenCurrentEntityIsNonNull) {
    VisibilityMesh mesh(Context); Box(mesh); Device->m_Current3dEntity = Other;
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    EXPECT_EQ(1, mesh.BoxReads); CurrentRect(95, 70, 195, 110, 0.04f);
    EXPECT_EQ(Other, Device->m_Current3dEntity);
}
TEST_F(MeshRenderVisibilityTest, FullyOffscreenBoxDoesNotSuppressCallbacksOrPolluteExtents) {
    VisibilityMesh mesh(Context); Box(mesh); SeedExtents(11, 12, 13, 14);
    VxMatrix world = VxMatrix::Identity(); world[3] = VxVector(10, 0, 0);
    Device->SetWorldTransformationMatrix(world);
    ASSERT_EQ(CK_OK, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity)));
    EXPECT_EQ(1, mesh.BoxReads); EXPECT_EQ(1, Trace.Count); CurrentRect(11, 12, 13, 14);
}
TEST_F(MeshRenderVisibilityTest, ChangedWorldMatrixReprojectsWithoutUsingEntityTransform) {
    VisibilityMesh mesh(Context); Box(mesh);
    ASSERT_EQ(CK_OK, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity))); CurrentRect(95, 70, 195, 110, 0.04f);
    SeedExtents(1000000, 1000000, -1000000, -1000000);
    VxMatrix world = VxMatrix::Identity(); world[3] = VxVector(-0.25f, 0.2f, 0);
    Device->SetWorldTransformationMatrix(world);
    ASSERT_EQ(CK_OK, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity)));
    EXPECT_EQ(2, mesh.BoxReads); CurrentRect(45, 50, 145, 90, 0.04f);
}
TEST_F(MeshRenderVisibilityTest, InvalidRasterizerReturnsBeforeBoxOrCallbacks) {
    VisibilityMesh mesh(Context); Box(mesh); SeedExtents(11, 12, 13, 14);
    Device->m_RasterizerContext = nullptr;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity)));
    EXPECT_EQ(0, mesh.BoxReads); EXPECT_EQ(0, Trace.Count); CurrentRect(11, 12, 13, 14);
}
TEST_F(MeshRenderVisibilityTest, PreAndPostCallbacksObserveAlreadyAccumulatedExtents) {
    VisibilityMesh mesh(Context); Box(mesh);
    ASSERT_TRUE(mesh.AddPreRenderCallBack(ObserveMesh, &Pre, FALSE));
    ASSERT_TRUE(mesh.AddPostRenderCallBack(ObserveMesh, &Post, FALSE));
    ASSERT_EQ(CK_OK, mesh.Render(Device, reinterpret_cast<CK3dEntity *>(Entity)));
    const int expected[] = {10, 20, 30}; Order(expected, 3);
    for (int i = 0; i < 3; ++i) Rect(Trace.Extents[i], 95, 70, 195, 110, 0.04f);
}
TEST_F(MeshRenderVisibilityTest, ManagerFrameCleanupRemovesOnlyTemporaryMeshCallbacks) {
    VisibilityMesh mesh(Context); Box(mesh);
    ASSERT_TRUE(mesh.AddPreRenderCallBack(ObserveMesh, &Pre, FALSE));
    ASSERT_TRUE(mesh.AddPreRenderCallBack(ObserveMesh, &TemporaryPre, TRUE));
    ASSERT_TRUE(mesh.AddPostRenderCallBack(ObserveMesh, &Post, FALSE));
    ASSERT_TRUE(mesh.AddPostRenderCallBack(ObserveMesh, &TemporaryPost, TRUE));
    EXPECT_FALSE(mesh.AddPreRenderCallBack(ObserveMesh, &TemporaryPre, FALSE));
    EXPECT_FALSE(mesh.AddPostRenderCallBack(ObserveMesh, &TemporaryPost, FALSE));
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    const int first[] = {10, 11, 20, 30, 31}; Order(first, 5);
    Manager->RemoveAllTemporaryCallbacks(); Trace.Count = 0;
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    const int second[] = {10, 20, 30}; Order(second, 3);
    Manager->RemoveAllTemporaryCallbacks();
}
TEST_F(MeshRenderVisibilityTest, ExplicitRemovalOfTemporaryMeshPairAllowsDeferredManagerCleanup) {
    VisibilityMesh mesh(Context); Box(mesh);
    ASSERT_TRUE(mesh.AddPreRenderCallBack(ObserveMesh, &TemporaryPre, TRUE));
    ASSERT_TRUE(mesh.AddPostRenderCallBack(ObserveMesh, &TemporaryPost, TRUE));
    ASSERT_TRUE(mesh.AddPreRenderCallBack(ObserveMesh, &Pre, FALSE));
    ASSERT_TRUE(mesh.RemovePreRenderCallBack(ObserveMesh, &TemporaryPre));
    ASSERT_TRUE(mesh.RemovePostRenderCallBack(ObserveMesh, &TemporaryPost));
    Manager->RemoveAllTemporaryCallbacks();
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    const int expected[] = {10, 20}; Order(expected, 2);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
}
TEST_F(MeshRenderVisibilityTest, ManagerFrameCleanupRemovesTemporarySubMeshCallbacksAroundRealDraw) {
    VisibilityMesh mesh(Context); Triangle(mesh); State.Trace = &Trace;
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(ObserveSubMesh, &Pre, FALSE));
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(ObserveSubMesh, &TemporaryPre, TRUE));
    ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(ObserveSubMesh, &Post, FALSE));
    ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(ObserveSubMesh, &TemporaryPost, TRUE));
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    const int first[] = {10, 11, 50, 30, 31}; Order(first, 5);
    EXPECT_EQ(1, State.Draws);
    Manager->RemoveAllTemporaryCallbacks(); Trace.Count = 0;
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    const int second[] = {10, 50, 30}; Order(second, 3);
    EXPECT_EQ(2, State.Draws);
    EXPECT_EQ(0, Manager->m_TemporaryPreRenderCallbacks.Size());
    EXPECT_EQ(0, Manager->m_TemporaryPostRenderCallbacks.Size());
}
TEST_F(MeshRenderVisibilityTest, ExplicitSubMeshRemovalLeavesPermanentCallbacksAfterFrameCleanup) {
    VisibilityMesh mesh(Context); Triangle(mesh); State.Trace = &Trace;
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(ObserveSubMesh, &TemporaryPre, TRUE));
    ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(ObserveSubMesh, &TemporaryPost, TRUE));
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(ObserveSubMesh, &Pre, FALSE));
    ASSERT_TRUE(mesh.RemoveSubMeshPreRenderCallBack(ObserveSubMesh, &TemporaryPre));
    ASSERT_TRUE(mesh.RemoveSubMeshPostRenderCallBack(ObserveSubMesh, &TemporaryPost));
    Manager->RemoveAllTemporaryCallbacks();
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
    const int expected[] = {10, 50}; Order(expected, 2);
}
TEST_F(MeshRenderVisibilityTest, RemoveAllCallbacksUnregistersOnlyOwnedMeshAndSubMeshContainers) {
    VisibilityMesh first(Context), second(Context); Box(first); Box(second);
    ASSERT_TRUE(first.AddPreRenderCallBack(ObserveMesh, &TemporaryPre, TRUE));
    ASSERT_TRUE(first.AddPostRenderCallBack(ObserveMesh, &TemporaryPost, TRUE));
    ASSERT_TRUE(first.AddSubMeshPreRenderCallBack(ObserveSubMesh, &TemporaryPre, TRUE));
    ASSERT_TRUE(first.AddSubMeshPostRenderCallBack(ObserveSubMesh, &TemporaryPost, TRUE));
    ASSERT_TRUE(second.AddPreRenderCallBack(ObserveMesh, &Pre, TRUE));
    ASSERT_TRUE(second.AddPostRenderCallBack(ObserveMesh, &Post, TRUE));
    ASSERT_EQ(3, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(3, Manager->m_TemporaryPostRenderCallbacks.Size());
    first.RemoveAllCallbacks();
    ASSERT_EQ(1, Manager->m_TemporaryPreRenderCallbacks.Size());
    ASSERT_EQ(1, Manager->m_TemporaryPostRenderCallbacks.Size());
    ASSERT_EQ(CK_OK, second.Render(Device, nullptr));
    const int before[] = {10, 20, 30}; Order(before, 3);
    Manager->RemoveAllTemporaryCallbacks(); Trace.Count = 0;
    ASSERT_EQ(CK_OK, second.Render(Device, nullptr));
    const int after[] = {20}; Order(after, 1);
}
