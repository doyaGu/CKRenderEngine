#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "CKMemoryPool.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "RCKRenderObject.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

// Observe actual scalar releases from CK2_3DStatic. VxMath DLL backing-array
// allocations are outside this observer; no array-lifetime claim is inferred.
struct GroupReleaseWatch { void *Pointer; int Releases; };
static GroupReleaseWatch s_GroupWatches[24];
static int s_GroupWatchCount = 0;
static int WatchGroupRelease(void *pointer) {
    const int index = s_GroupWatchCount++;
    s_GroupWatches[index] = {pointer,0};
    return index;
}
static void GroupObservedDelete(void *pointer) noexcept {
    for (int i = 0; i < s_GroupWatchCount; ++i) {
        if (pointer && pointer == s_GroupWatches[i].Pointer) {
            ++s_GroupWatches[i].Releases;
            return; // Hold watched storage to prevent allocator-reuse ambiguity.
        }
    }
    std::free(pointer);
}
void *operator new(std::size_t bytes) {
    void *pointer = std::malloc(bytes ? bytes : 1);
    if (!pointer) throw std::bad_alloc();
    return pointer;
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *pointer) noexcept { GroupObservedDelete(pointer); }
void operator delete[](void *pointer) noexcept { GroupObservedDelete(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { GroupObservedDelete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { GroupObservedDelete(pointer); }

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
class GroupMeshProbe : public RCKMesh {
public:
    explicit GroupMeshProbe(CKContext *context) : RCKMesh(context) {}
    int GroupCount() const { return m_MaterialGroups.Size(); }
    CKMaterialGroup *Group(int index) { return m_MaterialGroups[index]; }
    VxMaterialChannel &Channel(int index) { return m_MaterialChannels[index]; }
    CKDWORD FaceMaterialSlot(int face) { return m_Faces[face].m_MatIndex; }
    void AssignMaterialSlot(int face, CKWORD slot) { m_Faces[face].m_MatIndex = slot; }
    CKDWORD FaceMaskDirty() const { return m_FaceChannelMask; }
    CKDWORD Valid() const { return m_Valid; }
    void SetValid(CKDWORD value) { m_Valid = value; }
    void Cold() { m_Valid = 0; }
};
class GroupDrawObserver : public MaterialRenderObserver {
public:
    struct Triangle { VxVector P[3]; Vx2DVector UV[3]; CKDWORD Color[3]; };
    Triangle Triangles[1024]; int TriangleCount = 0, DrawCount = 0;
    int SubmittedVertexCount[64] = {};
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_NE(nullptr,data); if (!data) return FALSE;
        EXPECT_LT(DrawCount,64); if (DrawCount >= 64) return FALSE;
        SubmittedVertexCount[DrawCount++] = data->VertexCount;
        for (int i = 0; i < count; ++i) EXPECT_LT(indices[i],data->VertexCount);
        EXPECT_TRUE(type == VX_TRIANGLELIST || type == VX_TRIANGLESTRIP);
        const int triangles = type == VX_TRIANGLESTRIP ? count - 2 : count / 3;
        for (int t = 0; t < triangles; ++t) {
            CKWORD triangle[3];
            if (type == VX_TRIANGLESTRIP) {
                triangle[0] = indices[t]; triangle[1] = indices[t+1]; triangle[2] = indices[t+2];
                if (t & 1) { CKWORD swap = triangle[0]; triangle[0] = triangle[1]; triangle[1] = swap; }
            } else {
                triangle[0] = indices[3*t]; triangle[1] = indices[3*t+1]; triangle[2] = indices[3*t+2];
            }
            if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[2] == triangle[0]) continue;
            EXPECT_LT(TriangleCount,1024); if (TriangleCount >= 1024) return FALSE;
            Triangle &draw = Triangles[TriangleCount++];
            for (int v = 0; v < 3; ++v) {
                draw.P[v] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + triangle[v] * data->PositionStride);
                if (data->TexCoordPtr) draw.UV[v] = *reinterpret_cast<Vx2DVector *>(static_cast<CKBYTE *>(data->TexCoordPtr) + triangle[v] * data->TexCoordStride);
                if (data->ColorPtr) draw.Color[v] = *reinterpret_cast<CKDWORD *>(static_cast<CKBYTE *>(data->ColorPtr) + triangle[v] * data->ColorStride);
            }
        }
        return TRUE;
    }
};
class MeshRenderGroupsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    GroupDrawObserver State;
    void SetUp() override {
        s_GroupWatchCount = 0;
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK,CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK,CKCreateContext(&Context,nullptr,0,0));
        Manager = new RCKRenderManager(Context); Manager->m_VertexCache.Value = 0;
        Device = new RCKRenderContext(Context); Device->DetachAll(); Device->m_Current3dEntity = nullptr;
        Device->m_RasterizerContext = &State;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
        for (int i = 0; i < s_GroupWatchCount; ++i)
            if (s_GroupWatches[i].Releases) std::free(s_GroupWatches[i].Pointer);
        s_GroupWatchCount = 0;
    }
    void Geometry(GroupMeshProbe &mesh, int vertices = 6, int faces = 2) {
        mesh.SetVertexCount(vertices); mesh.SetFaceCount(faces);
        for (int i = 0; i < vertices; ++i) {
            VxVector position(static_cast<float>(i),static_cast<float>(i*i),0),normal(0,0,1);
            mesh.SetVertexPosition(i,&position); mesh.SetVertexNormal(i,&normal);
            mesh.SetVertexTextureCoordinates(i,i*.125f,i*.25f,-1); mesh.SetVertexColor(i,0xff000010u+i);
        }
        if (faces > 0) mesh.SetFaceVertexIndex(0,5,1,4);
        if (faces > 1) mesh.SetFaceVertexIndex(1,4,2,5);
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
    static void Vec(const VxVector &actual, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x,actual.x); EXPECT_FLOAT_EQ(y,actual.y); EXPECT_FLOAT_EQ(z,actual.z);
    }
    void CheckPayload(CKVBuffer *buffer, const int *globals, int count) {
        ASSERT_NE(nullptr,buffer); ASSERT_EQ(count,buffer->m_VertexRemap.Size());
        ASSERT_EQ(count,buffer->m_Vertices.Size()); ASSERT_EQ(count,buffer->m_Colors.Size());
        for (int i = 0; i < count; ++i) {
            EXPECT_EQ(globals[i],buffer->m_VertexRemap[i]);
            Vec(buffer->m_Vertices[i].m_Position,static_cast<float>(globals[i]),static_cast<float>(globals[i]*globals[i]),0);
            EXPECT_FLOAT_EQ(globals[i]*.125f,buffer->m_Vertices[i].m_UV.x);
            EXPECT_EQ(0xff000010u+globals[i],buffer->m_Colors[i].Color);
        }
    }
    void HasRenderedTriangle(int a,int b,int c) {
        int matches = 0;
        for (int t = 0; t < State.TriangleCount; ++t) {
            for (int offset = 0; offset < 3; ++offset) {
                const GroupDrawObserver::Triangle &triangle = State.Triangles[t];
                if (triangle.P[offset].x == a && triangle.P[(offset+1)%3].x == b && triangle.P[(offset+2)%3].x == c)
                    ++matches;
            }
        }
        EXPECT_EQ(1,matches) << "Expected exactly one oriented authored triangle";
    }
    void ManyMaterials(int materialCount) {
        GroupMeshProbe mesh(Context); Geometry(mesh,6,materialCount);
        RCKMaterial *materials[40] = {};
        ASSERT_LE(materialCount,40);
        for (int i = 0; i < materialCount; ++i) {
            materials[i] = new RCKMaterial(Context); mesh.SetFaceVertexIndex(i,5,1,4); mesh.SetFaceMaterial(i,materials[i]);
        }
        ASSERT_EQ(1,mesh.CreateRenderGroups()); ASSERT_EQ(materialCount+1,mesh.GroupCount());
        EXPECT_FALSE(mesh.Group(0)->m_HasValidPrimitives);
        const int globals[] = {5,1,4};
        for (int i = 0; i < materialCount; ++i) {
            CKMaterialGroup *group = mesh.Group(i+1); EXPECT_EQ(materials[i],group->m_Material);
            EXPECT_EQ(3u,group->m_VertexCount); EXPECT_EQ(static_cast<CKDWORD>(i*3),group->m_BaseVertex);
            ASSERT_EQ(1,group->m_FaceIndices.Size()); EXPECT_EQ(i,group->m_FaceIndices[0]);
            CheckPayload(mesh.GetVBuffer(group),globals,3);
        }
        mesh.Cold(); mesh.DefaultRender(Device,nullptr); EXPECT_EQ(materialCount,State.TriangleCount);
        for (int i = 0; i < materialCount; ++i) delete materials[i];
    }
};

TEST_F(MeshRenderGroupsTest, EmptyMeshProducesNoValidPrimitive) {
    GroupMeshProbe mesh(Context); EXPECT_EQ(0,mesh.CreateRenderGroups()); ASSERT_EQ(1,mesh.GroupCount());
    EXPECT_FALSE(mesh.Group(0)->m_HasValidPrimitives); EXPECT_TRUE(mesh.GetFlags() & VXMESH_OPTIMIZED);
}
TEST_F(MeshRenderGroupsTest, MonoMaterialKeepsAuthoredGlobalIndicesAndFullVertexStream) {
    GroupMeshProbe mesh(Context); Geometry(mesh); ASSERT_EQ(1,mesh.CreateRenderGroups());
    ASSERT_EQ(1,mesh.GroupCount()); CKMaterialGroup *group = mesh.Group(0);
    EXPECT_EQ(nullptr,mesh.GetVBuffer(group)); EXPECT_EQ(6u,group->m_VertexCount); EXPECT_EQ(6u,group->m_MaxVertexIndex);
    const CKWORD expected[] = {5,1,4,4,2,5}; ASSERT_EQ(6,group->m_Primitives[0].m_Indices.Size());
    for (int i = 0; i < 6; ++i) EXPECT_EQ(expected[i],group->m_Primitives[0].m_Indices[i]);
    EXPECT_TRUE(mesh.GetFlags() & VXMESH_MONOMATERIAL);
    mesh.Cold(); mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.TriangleCount);
    HasRenderedTriangle(5,1,4); HasRenderedTriangle(4,2,5); EXPECT_EQ(6,State.SubmittedVertexCount[0]);
}
TEST_F(MeshRenderGroupsTest, MonoMaterialInSecondarySlotRetainsFaceMaterialIdentityAfterPruning) {
    GroupMeshProbe mesh(Context); RCKMaterial unused(Context),used(Context); Geometry(mesh);
    mesh.CreateNewMaterialGroup(&unused); mesh.SetFaceMaterial(0,&used); mesh.SetFaceMaterial(1,&used);
    ASSERT_EQ(1,mesh.CreateRenderGroups()); ASSERT_EQ(2,mesh.GroupCount());
    EXPECT_EQ(&used,mesh.GetFaceMaterial(0)); EXPECT_EQ(&used,mesh.GetFaceMaterial(1));
    EXPECT_EQ(1u,mesh.FaceMaterialSlot(0)); EXPECT_EQ(nullptr,mesh.GetVBuffer(mesh.Group(1)));
    EXPECT_TRUE(mesh.GetFlags() & VXMESH_MONOMATERIAL);
}
TEST_F(MeshRenderGroupsTest, TwoMaterialsRemapPositionsUVColorsAndIndicesByActualFaceMembership) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); mesh.SetFaceMaterial(1,&material);
    ASSERT_EQ(1,mesh.CreateRenderGroups()); ASSERT_EQ(2,mesh.GroupCount());
    const int first[] = {5,1,4},second[] = {4,2,5};
    CheckPayload(mesh.GetVBuffer(mesh.Group(0)),first,3); CheckPayload(mesh.GetVBuffer(mesh.Group(1)),second,3);
    EXPECT_EQ(0u,mesh.Group(0)->m_BaseVertex); EXPECT_EQ(3u,mesh.Group(1)->m_BaseVertex);
    EXPECT_FALSE(mesh.GetFlags() & VXMESH_MONOMATERIAL);
    mesh.Cold(); mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.TriangleCount);
    HasRenderedTriangle(5,1,4); HasRenderedTriangle(4,2,5);
    for (int t = 0; t < State.TriangleCount; ++t) for (int v = 0; v < 3; ++v) {
        const int global = static_cast<int>(State.Triangles[t].P[v].x);
        EXPECT_FLOAT_EQ(global*.125f,State.Triangles[t].UV[v].x); EXPECT_EQ(0xff000010u+global,State.Triangles[t].Color[v]);
    }
}
TEST_F(MeshRenderGroupsTest, SharedVerticesHaveIndependentPerMaterialLocalSlots) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); mesh.SetFaceMaterial(1,&material);
    mesh.CreateRenderGroups(); CKVBuffer *first = mesh.GetVBuffer(mesh.Group(0)),*second = mesh.GetVBuffer(mesh.Group(1));
    ASSERT_NE(nullptr,first); ASSERT_NE(nullptr,second); EXPECT_NE(first,second);
    EXPECT_EQ(5,first->m_VertexRemap[0]); EXPECT_EQ(5,second->m_VertexRemap[2]);
    EXPECT_EQ(4,first->m_VertexRemap[2]); EXPECT_EQ(4,second->m_VertexRemap[0]);
}
TEST_F(MeshRenderGroupsTest, EmptyMaterialSlotsArePrunedWithoutChangingSurvivingFaceBindings) {
    GroupMeshProbe mesh(Context); RCKMaterial unused1(Context),unused2(Context),used(Context); Geometry(mesh);
    mesh.CreateNewMaterialGroup(&unused1); mesh.CreateNewMaterialGroup(&unused2); mesh.SetFaceMaterial(1,&used);
    ASSERT_EQ(1,mesh.CreateRenderGroups()); ASSERT_EQ(2,mesh.GroupCount());
    EXPECT_EQ(nullptr,mesh.GetFaceMaterial(0)); EXPECT_EQ(&used,mesh.GetFaceMaterial(1)); EXPECT_EQ(1u,mesh.FaceMaterialSlot(1));
}
TEST_F(MeshRenderGroupsTest, TwentyNineMaterialsUseCorrectLowBitTrackerPayload) { ManyMaterials(29); }
TEST_F(MeshRenderGroupsTest, ThirtyMaterialsUseCorrectBitArrayTrackerPayload) { ManyMaterials(30); }
TEST_F(MeshRenderGroupsTest, ThirtyThreeMaterialsPreserveMembershipAboveThirtyTwoBits) { ManyMaterials(33); }
TEST_F(MeshRenderGroupsTest, LargeMaterialRemapDoesNotTruncateLocalIndicesAboveByteRange) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh,600,200);
    for (int f = 0; f < 200; ++f) {
        mesh.SetFaceVertexIndex(f,3*f,3*f+1,3*f+2); if (f >= 100) mesh.SetFaceMaterial(f,&material);
    }
    ASSERT_EQ(1,mesh.CreateRenderGroups()); ASSERT_EQ(2,mesh.GroupCount());
    ASSERT_EQ(300u,mesh.Group(0)->m_VertexCount); ASSERT_EQ(300u,mesh.Group(1)->m_VertexCount);
    EXPECT_EQ(299,mesh.Group(0)->m_Primitives[0].m_Indices[299]); EXPECT_EQ(299,mesh.Group(1)->m_Primitives[0].m_Indices[299]);
    mesh.Cold(); mesh.DefaultRender(Device,nullptr); ASSERT_EQ(200,State.TriangleCount);
    HasRenderedTriangle(297,298,299); HasRenderedTriangle(597,598,599);
}
TEST_F(MeshRenderGroupsTest, ContextScratchAllocationUsesDWordUnitsAndPreservesRetainedTail) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh,600,2); mesh.SetFaceMaterial(1,&material);
    int poolIndex = -1;
    CKDWORD *buffer = static_cast<CKDWORD *>(Context->AllocateMemoryPool(600+8,poolIndex));
    ASSERT_NE(nullptr,buffer); ASSERT_GE(poolIndex,0); EXPECT_GE(Context->m_MemoryPools[poolIndex]->AllocatedSize(),608u);
    for (int i = 600; i < 608; ++i) buffer[i] = 0x74ab9812u;
    Context->ReleaseMemoryPool(poolIndex); ASSERT_EQ(1,mesh.CreateRenderGroups());
    int reused = -1; CKDWORD *after = static_cast<CKDWORD *>(Context->AllocateMemoryPool(600,reused));
    EXPECT_EQ(poolIndex,reused); EXPECT_EQ(buffer,after);
    for (int i = 600; i < 608; ++i) EXPECT_EQ(0x74ab9812u,after[i]);
    Context->ReleaseMemoryPool(reused);
}
TEST_F(MeshRenderGroupsTest, StripOptimizationPreservesOrientedAuthoredTriangles) {
    GroupMeshProbe mesh(Context); Geometry(mesh,6,2); mesh.SetFaceVertexIndex(0,0,1,2); mesh.SetFaceVertexIndex(1,2,1,3);
    mesh.SetFlags(mesh.GetFlags() | VXMESH_STRIPIFY); ASSERT_EQ(1,mesh.CreateRenderGroups());
    ASSERT_EQ(VX_TRIANGLESTRIP,mesh.Group(0)->m_Primitives[0].m_Type);
    mesh.Cold(); mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.TriangleCount);
    HasRenderedTriangle(0,1,2); HasRenderedTriangle(2,1,3);
}
TEST_F(MeshRenderGroupsTest, VertexCacheOptimizationPreservesOrientedAuthoredTriangles) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); mesh.SetFaceMaterial(1,&material);
    Manager->m_VertexCache.Value = 16; ASSERT_EQ(1,mesh.CreateRenderGroups());
    mesh.Cold(); mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.TriangleCount);
    HasRenderedTriangle(5,1,4); HasRenderedTriangle(4,2,5);
}
TEST_F(MeshRenderGroupsTest, RebuildReleasesOldRemappedBuffersExactlyOnce) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); mesh.SetFaceMaterial(1,&material);
    mesh.CreateRenderGroups(); const int first = WatchGroupRelease(mesh.GetVBuffer(mesh.Group(0))),second = WatchGroupRelease(mesh.GetVBuffer(mesh.Group(1)));
    ASSERT_EQ(1,mesh.CreateRenderGroups()); EXPECT_EQ(1,s_GroupWatches[first].Releases); EXPECT_EQ(1,s_GroupWatches[second].Releases);
    const int globals[] = {5,1,4}; CheckPayload(mesh.GetVBuffer(mesh.Group(0)),globals,3);
}
TEST_F(MeshRenderGroupsTest, RemovingAllFacesClearsStalePrimitivesAndRemappedBuffers) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); mesh.SetFaceMaterial(1,&material); mesh.CreateRenderGroups();
    const int first = WatchGroupRelease(mesh.GetVBuffer(mesh.Group(0))),second = WatchGroupRelease(mesh.GetVBuffer(mesh.Group(1)));
    mesh.SetFaceCount(0); EXPECT_EQ(0,mesh.CreateRenderGroups());
    for (int i = 0; i < mesh.GroupCount(); ++i) {
        EXPECT_FALSE(mesh.Group(i)->m_HasValidPrimitives); EXPECT_EQ(0,mesh.Group(i)->m_FaceIndices.Size());
        EXPECT_EQ(0,mesh.Group(i)->m_Primitives[0].m_Indices.Size()); EXPECT_EQ(nullptr,mesh.GetVBuffer(mesh.Group(i)));
    }
    EXPECT_EQ(1,s_GroupWatches[first].Releases); EXPECT_EQ(1,s_GroupWatches[second].Releases);
}
TEST_F(MeshRenderGroupsTest, RemovingAllVerticesClearsStalePrimitives) {
    GroupMeshProbe mesh(Context); Geometry(mesh); mesh.CreateRenderGroups(); mesh.SetVertexCount(0);
    EXPECT_EQ(0,mesh.CreateRenderGroups()); EXPECT_FALSE(mesh.Group(0)->m_HasValidPrimitives);
    EXPECT_EQ(0,mesh.Group(0)->m_Primitives[0].m_Indices.Size()); EXPECT_EQ(0u,mesh.Group(0)->m_VertexCount);
}
TEST_F(MeshRenderGroupsTest, RebuildDropsCachedChannelIndicesAndMarksMasksDirty) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); int channel = mesh.AddChannel(&material,FALSE);
    mesh.SetFaceChannelMask(0,1); mesh.SetFaceChannelMask(1,0); mesh.CreateRenderGroups(); mesh.UpdateChannelIndices();
    ASSERT_NE(nullptr,mesh.Channel(channel).m_FaceIndices); ASSERT_EQ(3,mesh.Channel(channel).m_FaceIndices->Size());
    ASSERT_EQ(1,mesh.CreateRenderGroups()); EXPECT_EQ(nullptr,mesh.Channel(channel).m_FaceIndices); EXPECT_EQ(0xffffu,mesh.FaceMaskDirty());
}
TEST_F(MeshRenderGroupsTest, EmptyGeometryRebuildDropsStaleChannelFaceIndices) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); int channel = mesh.AddChannel(&material,FALSE);
    mesh.SetFaceChannelMask(0,1); mesh.SetFaceChannelMask(1,0); mesh.CreateRenderGroups(); mesh.UpdateChannelIndices();
    ASSERT_NE(nullptr,mesh.Channel(channel).m_FaceIndices); mesh.SetFaceCount(0); EXPECT_EQ(0,mesh.CreateRenderGroups());
    EXPECT_EQ(nullptr,mesh.Channel(channel).m_FaceIndices); EXPECT_EQ(0xffffu,mesh.FaceMaskDirty());
}
TEST_F(MeshRenderGroupsTest, PrimitiveValidityIsRecomputedFromAnyNonemptyEntry) {
    GroupMeshProbe mesh(Context); CKMaterialGroup group; group.m_Primitives.Resize(3); group.m_HasValidPrimitives = 9;
    mesh.UpdateHasValidPrimitives(&group); EXPECT_EQ(0u,group.m_HasValidPrimitives);
    group.m_Primitives[2].m_Indices.PushBack(4); mesh.UpdateHasValidPrimitives(&group); EXPECT_EQ(1u,group.m_HasValidPrimitives);
    group.m_Primitives[2].m_Indices.Clear(); mesh.UpdateHasValidPrimitives(&group); EXPECT_EQ(0u,group.m_HasValidPrimitives);
}
TEST_F(MeshRenderGroupsTest, GetVBufferReturnsExactOwnedPointerAndHandlesEmptyGroups) {
    GroupMeshProbe mesh(Context); CKMaterialGroup group; EXPECT_EQ(nullptr,mesh.GetVBuffer(nullptr)); EXPECT_EQ(nullptr,mesh.GetVBuffer(&group));
    CKVBuffer *buffer = new CKVBuffer(7); group.m_RemapData = reinterpret_cast<CKUINTPTR>(buffer);
    EXPECT_EQ(buffer,mesh.GetVBuffer(&group)); EXPECT_EQ(7,mesh.GetVBuffer(&group)->m_VertexRemap.Size());
    mesh.DeleteVBuffer(&group); EXPECT_EQ(nullptr,mesh.GetVBuffer(&group));
}
TEST_F(MeshRenderGroupsTest, DeleteVBufferReleasesOwnershipExactlyOnceWithoutChangingMaterialOrPrimitive) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); CKMaterialGroup group(&material); group.m_Primitives.Resize(1);
    group.m_Primitives[0].m_Indices.PushBack(2); CKVBuffer *buffer = new CKVBuffer(4); group.m_RemapData = reinterpret_cast<CKUINTPTR>(buffer);
    const int watch = WatchGroupRelease(buffer); mesh.DeleteVBuffer(&group); mesh.DeleteVBuffer(&group); mesh.DeleteVBuffer(nullptr);
    EXPECT_EQ(1,s_GroupWatches[watch].Releases); EXPECT_EQ(0u,group.m_RemapData); EXPECT_EQ(&material,group.m_Material);
    EXPECT_EQ(1,group.m_Primitives[0].m_Indices.Size());
}
TEST_F(MeshRenderGroupsTest, DeleteRenderGroupReleasesGroupsAndRemapsButKeepsReferencedMaterialsAlive) {
    GroupMeshProbe mesh(Context); RCKMaterial *material = new RCKMaterial(Context); Geometry(mesh); mesh.SetFaceMaterial(1,material); mesh.CreateRenderGroups();
    const int group0 = WatchGroupRelease(mesh.Group(0)),group1 = WatchGroupRelease(mesh.Group(1));
    const int remap0 = WatchGroupRelease(mesh.GetVBuffer(mesh.Group(0))),remap1 = WatchGroupRelease(mesh.GetVBuffer(mesh.Group(1)));
    const int materialWatch = WatchGroupRelease(material); mesh.SetValid(6); mesh.DeleteRenderGroup(); mesh.DeleteRenderGroup();
    EXPECT_EQ(0,mesh.GroupCount()); EXPECT_EQ(0u,mesh.Valid());
    EXPECT_EQ(1,s_GroupWatches[group0].Releases); EXPECT_EQ(1,s_GroupWatches[group1].Releases);
    EXPECT_EQ(1,s_GroupWatches[remap0].Releases); EXPECT_EQ(1,s_GroupWatches[remap1].Releases);
    EXPECT_EQ(0,s_GroupWatches[materialWatch].Releases); EXPECT_EQ(CKCID_MATERIAL,material->GetClassID());
    delete material; EXPECT_EQ(1,s_GroupWatches[materialWatch].Releases);
}
TEST_F(MeshRenderGroupsTest, HighMaterialSlotSingleFaceRemainsMonoMaterialAfterUnusedSlotsArePruned) {
    GroupMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh,6,1);
    // Material slots loaded from files use CKFace's WORD ordinal. Seed a valid
    // sparse slot table independently of material lookup/grouping algorithms.
    for (int i = 1; i < 2050; ++i) mesh.CreateNewMaterialGroup(nullptr);
    const int slot = mesh.CreateNewMaterialGroup(&material); ASSERT_EQ(2050,slot);
    mesh.AssignMaterialSlot(0,static_cast<CKWORD>(slot)); ASSERT_EQ(&material,mesh.GetFaceMaterial(0));
    ASSERT_EQ(1,mesh.CreateRenderGroups()); ASSERT_EQ(2,mesh.GroupCount());
    EXPECT_TRUE(mesh.GetFlags() & VXMESH_MONOMATERIAL); EXPECT_EQ(&material,mesh.GetFaceMaterial(0));
    EXPECT_EQ(nullptr,mesh.GetVBuffer(mesh.Group(1))); EXPECT_EQ(6u,mesh.Group(1)->m_VertexCount);
    mesh.Cold(); mesh.DefaultRender(Device,nullptr); ASSERT_EQ(1,State.TriangleCount); HasRenderedTriangle(5,1,4);
}
}
