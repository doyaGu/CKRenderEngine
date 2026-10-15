#include <gtest/gtest.h>
#include <cstdlib>
#include <new>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"

// CK2_3DStatic owns these scalar allocations. VxMath.dll array allocations
// intentionally are not claimed to be visible to this executable observer.
struct MeshReleaseWatch {
    void *Pointer;
    int Releases;
    int Order;
    bool Hold;
};
static MeshReleaseWatch s_Watches[32];
static int s_WatchCount = 0;
static int s_ReleaseOrder = 0;
static int WatchMeshRelease(void *pointer, bool hold = true) {
    const int index = s_WatchCount++;
    s_Watches[index] = {pointer, 0, 0, hold};
    return index;
}
static void MeshObservedDelete(void *pointer) noexcept {
    for (int i = 0; i < s_WatchCount; ++i) {
        if (pointer && pointer == s_Watches[i].Pointer) {
            ++s_Watches[i].Releases;
            s_Watches[i].Order = ++s_ReleaseOrder;
            if (s_Watches[i].Hold)
                return;
            break;
        }
    }
    std::free(pointer);
}
void *operator new(std::size_t bytes) {
    void *pointer = std::malloc(bytes ? bytes : 1);
    if (!pointer)
        throw std::bad_alloc();
    return pointer;
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *pointer) noexcept { MeshObservedDelete(pointer); }
void operator delete[](void *pointer) noexcept { MeshObservedDelete(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { MeshObservedDelete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { MeshObservedDelete(pointer); }

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
class MeshMemoryProbe : public RCKMesh {
public:
    explicit MeshMemoryProbe(CKContext *context) : RCKMesh(context) {}
    void AttachPM(CKProgressiveMesh *pm) { m_ProgressiveMesh = pm; }
    CKProgressiveMesh *PM() { return m_ProgressiveMesh; }
    XArray<VxVertex> &Vertices() { return m_Vertices; }
    XArray<CKWORD> &FaceIndices() { return m_FaceVertexIndices; }
    XArray<CKWORD> &Lines() { return m_LineIndices; }
    XArray<int> &ActiveChannels() { return m_ActiveTextureChannels; }
    XClassArray<VxMaterialChannel> &Channels() { return m_MaterialChannels; }
    XArray<float> *Weights() { return m_VertexWeights; }
    CKMaterialGroup *Group() { return m_MaterialGroups[0]; }
    CKMaterialGroup *GroupAt(int index) { return m_MaterialGroups[index]; }
    void AttachSubCallbacks(CKCallbacksContainer *callbacks) { m_SubMeshCallbacks = callbacks; }
};
class MeshLifetimeMemoryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        s_WatchCount = 0; s_ReleaseOrder = 0;
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        // Held frees make an erroneous vertex-before-face teardown observable
        // without letting the regression oracle depend on allocator reuse.
        for (int i = 0; i < s_WatchCount; ++i)
            if (s_Watches[i].Hold && s_Watches[i].Releases)
                std::free(s_Watches[i].Pointer);
        s_WatchCount = 0;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};
static CKProgressiveMesh *MakeOwnedPM() {
    CKProgressiveMesh *pm = new CKProgressiveMesh;
    pm->m_Data.Resize(7); pm->m_EdgeCollapseData.Resize(5);
    pm->m_EdgeCollapseStruct = new PMEdgeCollapseData;
    return pm;
}
}

TEST_F(MeshLifetimeMemoryTest, DestructorReleasesOwnedProgressiveMeshAndRuntimeGraph) {
    MeshMemoryProbe *mesh = new MeshMemoryProbe(Context);
    CKProgressiveMesh *pm = MakeOwnedPM(); mesh->AttachPM(pm);
    const int pmWatch = WatchMeshRelease(pm);
    const int edgeWatch = WatchMeshRelease(pm->m_EdgeCollapseStruct);
    delete mesh;
    EXPECT_EQ(1, s_Watches[pmWatch].Releases);
    EXPECT_EQ(1, s_Watches[edgeWatch].Releases);
    if (!s_Watches[pmWatch].Releases) delete pm;
}
TEST_F(MeshLifetimeMemoryTest, DestroyPMThenDestructorDoesNotReleaseProgressiveDataTwice) {
    MeshMemoryProbe *mesh = new MeshMemoryProbe(Context);
    CKProgressiveMesh *pm = MakeOwnedPM(); mesh->AttachPM(pm);
    const int watch = WatchMeshRelease(pm, true);
    mesh->DestroyPM(); EXPECT_FALSE(mesh->IsPM());
    EXPECT_EQ(nullptr, mesh->PM()); EXPECT_EQ(1, s_Watches[watch].Releases);
    delete mesh; EXPECT_EQ(1, s_Watches[watch].Releases);
}
TEST_F(MeshLifetimeMemoryTest, RemovingMaterialGroupReleasesItsOwnedRemapBuffer) {
    MeshMemoryProbe mesh(Context); RCKMaterial material(Context);
    const int index = mesh.CreateNewMaterialGroup(&material);
    CKVBuffer *buffer = new CKVBuffer(3);
    mesh.GroupAt(index)->m_RemapData = reinterpret_cast<CKUINTPTR>(buffer);
    const int watch = WatchMeshRelease(buffer, true);
    mesh.ReplaceMaterial(&material, nullptr);
    EXPECT_EQ(-1, mesh.GetMaterialGroupIndex(&material, FALSE));
    EXPECT_EQ(1, s_Watches[watch].Releases);
    if (!s_Watches[watch].Releases) delete buffer;
}
TEST_F(MeshLifetimeMemoryTest, RuntimeFacesAreDestroyedBeforeTheirReferencedVertices) {
    PMEdgeCollapseData *edge = new PMEdgeCollapseData;
    PMVertexEx *vertices[3] = {new PMVertexEx, new PMVertexEx, new PMVertexEx};
    PMFace *face = new PMFace(vertices[0], vertices[1], vertices[2], VxVector(0, 0, 1), nullptr);
    int vertexWatches[3];
    for (int i = 0; i < 3; ++i) {
        edge->vertices.PushBack(vertices[i]);
        vertexWatches[i] = WatchMeshRelease(vertices[i], true);
    }
    edge->faces.PushBack(face); edge->heap.PushBack(vertices[0]);
    const int faceWatch = WatchMeshRelease(face, true);
    delete edge;
    EXPECT_EQ(1, s_Watches[faceWatch].Releases);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(1, s_Watches[vertexWatches[i]].Releases);
        EXPECT_LT(s_Watches[faceWatch].Order, s_Watches[vertexWatches[i]].Order);
    }
}
TEST_F(MeshLifetimeMemoryTest, RetainedVertexCapacityIsCountedUntilStorageIsCleared) {
    MeshMemoryProbe mesh(Context); const int before = mesh.GetMemoryOccupation();
    mesh.Vertices().Reserve(17); mesh.Vertices().Resize(9);
    EXPECT_EQ(17 * static_cast<int>(sizeof(VxVertex)), mesh.GetMemoryOccupation() - before);
    mesh.Vertices().Resize(0);
    EXPECT_EQ(17 * static_cast<int>(sizeof(VxVertex)), mesh.GetMemoryOccupation() - before);
    mesh.Vertices().Clear(); EXPECT_EQ(before, mesh.GetMemoryOccupation());
}
TEST_F(MeshLifetimeMemoryTest, TopologyAndActiveChannelStorageAreOwnedEvenWhenEmpty) {
    MeshMemoryProbe mesh(Context); const int before = mesh.GetMemoryOccupation();
    mesh.FaceIndices().Reserve(13); mesh.Lines().Reserve(11); mesh.ActiveChannels().Reserve(7);
    const int expected = 24 * static_cast<int>(sizeof(CKWORD)) + 7 * static_cast<int>(sizeof(int));
    EXPECT_EQ(expected, mesh.GetMemoryOccupation() - before);
    mesh.FaceIndices().Clear(); mesh.Lines().Clear(); mesh.ActiveChannels().Clear();
    EXPECT_EQ(before, mesh.GetMemoryOccupation());
}
TEST_F(MeshLifetimeMemoryTest, VertexWeightObjectAndRetainedWeightBufferAreCounted) {
    MeshMemoryProbe mesh(Context); const int before = mesh.GetMemoryOccupation();
    mesh.SetVertexWeightsCount(6); ASSERT_NE(nullptr, mesh.Weights());
    EXPECT_EQ(static_cast<int>(sizeof(XArray<float>) + 6 * sizeof(float)), mesh.GetMemoryOccupation() - before);
    mesh.Weights()->Resize(0);
    EXPECT_EQ(static_cast<int>(sizeof(XArray<float>) + 6 * sizeof(float)), mesh.GetMemoryOccupation() - before);
}
TEST_F(MeshLifetimeMemoryTest, ChannelUVsUseUVElementSizeAndReferencedMaterialsAreNotOwned) {
    MeshMemoryProbe mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(4));
    mesh.Vertices().Reserve(19); const int before = mesh.GetMemoryOccupation();
    mesh.Channels().Reserve(3); mesh.Channels().Resize(1);
    mesh.Channels()[0].m_UVs = new Vx2DVector[4];
    EXPECT_EQ(static_cast<int>(3 * sizeof(VxMaterialChannel) + 4 * sizeof(Vx2DVector)), mesh.GetMemoryOccupation() - before);
    RCKMaterial material(Context); const int withUV = mesh.GetMemoryOccupation();
    mesh.Channels()[0].m_Material = &material; EXPECT_EQ(withUV, mesh.GetMemoryOccupation());
    mesh.Channels()[0].m_FaceIndices = new XArray<CKWORD>;
    mesh.Channels()[0].m_FaceIndices->Reserve(9);
    EXPECT_EQ(static_cast<int>(sizeof(XArray<CKWORD>) + 9 * sizeof(CKWORD)), mesh.GetMemoryOccupation() - withUV);
}
TEST_F(MeshLifetimeMemoryTest, GroupPrimitiveBackingCapacityAndNestedIndexBuffersAreCounted) {
    MeshMemoryProbe mesh(Context); CKMaterialGroup *group = mesh.Group();
    const int before = mesh.GetMemoryOccupation(); group->m_Primitives.Reserve(5);
    EXPECT_EQ(5 * static_cast<int>(sizeof(CKPrimitiveEntry)), mesh.GetMemoryOccupation() - before);
    group->m_Primitives.Resize(1); group->m_Primitives[0].m_Indices.Reserve(12);
    EXPECT_EQ(static_cast<int>(5 * sizeof(CKPrimitiveEntry) + 12 * sizeof(CKWORD)), mesh.GetMemoryOccupation() - before);
    group->m_Primitives[0].m_Indices.Resize(0);
    EXPECT_EQ(static_cast<int>(5 * sizeof(CKPrimitiveEntry) + 12 * sizeof(CKWORD)), mesh.GetMemoryOccupation() - before);
}
TEST_F(MeshLifetimeMemoryTest, RemappedBuffersCountGeometryColorsRemapAndEachUVChannel) {
    MeshMemoryProbe mesh(Context); CKMaterialGroup *group = mesh.Group();
    const int before = mesh.GetMemoryOccupation(); CKVBuffer *buffer = new CKVBuffer;
    buffer->m_Vertices.Reserve(7); buffer->m_Colors.Reserve(11); buffer->m_VertexRemap.Reserve(13);
    buffer->m_UVs.Reserve(3); buffer->m_UVs.Resize(1); buffer->m_UVs[0].Reserve(17);
    group->m_RemapData = reinterpret_cast<CKUINTPTR>(buffer);
    const int expected = static_cast<int>(sizeof(CKVBuffer) + 7 * sizeof(VxVertex) + 11 * sizeof(VxColors)
        + 13 * sizeof(int) + 3 * sizeof(XArray<Vx2DVector>) + 17 * sizeof(Vx2DVector));
    EXPECT_EQ(expected, mesh.GetMemoryOccupation() - before);
    mesh.DeleteVBuffer(group); EXPECT_EQ(nullptr, mesh.GetVBuffer(group));
    EXPECT_EQ(before, mesh.GetMemoryOccupation());
}
TEST_F(MeshLifetimeMemoryTest, SecondaryCallbackContainerAndBothCallbackArraysAreCounted) {
    MeshMemoryProbe mesh(Context); const int before = mesh.GetMemoryOccupation();
    CKCallbacksContainer *callbacks = new CKCallbacksContainer;
    callbacks->m_PreCallBacks.Reserve(3); callbacks->m_PostCallBacks.Reserve(5);
    mesh.AttachSubCallbacks(callbacks);
    EXPECT_EQ(static_cast<int>(sizeof(CKCallbacksContainer) + 8 * sizeof(VxCallBack)), mesh.GetMemoryOccupation() - before);
}
TEST_F(MeshLifetimeMemoryTest, ProgressiveRuntimeGraphCountsOwnedVerticesFacesAndAliasesOnce) {
    MeshMemoryProbe mesh(Context);
    // DestroyPM rebuilds ordinary render groups; compare with the same group state.
    mesh.CreateRenderGroups();
    const int before = mesh.GetMemoryOccupation();
    CKProgressiveMesh *pm = MakeOwnedPM(); mesh.AttachPM(pm);
    PMEdgeCollapseData *edge = pm->m_EdgeCollapseStruct;
    PMVertexEx *vertex = new PMVertexEx; PMFace *face = new PMFace;
    edge->vertices.Reserve(3); edge->vertices.PushBack(vertex);
    edge->heap.Reserve(5); edge->heap.PushBack(vertex);
    edge->faces.Reserve(7); edge->faces.PushBack(face);
    vertex->neighbors.Reserve(11); vertex->neighbors.PushBack(vertex);
    vertex->faces.Reserve(13); vertex->faces.PushBack(face);
    const int expected = static_cast<int>(sizeof(CKProgressiveMesh) + 12 * sizeof(CKDWORD)
        + sizeof(PMEdgeCollapseData) + (3 + 5 + 7 + 11 + 13) * sizeof(void *)
        + sizeof(PMVertexEx) + sizeof(PMFace));
    EXPECT_EQ(expected, mesh.GetMemoryOccupation() - before);
    mesh.DestroyPM(); EXPECT_EQ(before, mesh.GetMemoryOccupation());
}

TEST_F(MeshLifetimeMemoryTest, ResetMaterialGroupRetainsNestedPrimitiveStorageUntilCleared) {
    MeshMemoryProbe mesh(Context); CKMaterialGroup *group = mesh.Group();
    const int before = mesh.GetMemoryOccupation();
    group->m_Primitives.Reserve(2); group->m_Primitives.Resize(2);
    group->m_Primitives[0].m_Indices.Reserve(3);
    group->m_Primitives[1].m_Indices.Reserve(9);
    const int owned = static_cast<int>(2 * sizeof(CKPrimitiveEntry) + 12 * sizeof(CKWORD));
    EXPECT_EQ(owned, mesh.GetMemoryOccupation() - before);
    mesh.ResetMaterialGroup(group, 0);
    EXPECT_EQ(1, group->m_Primitives.Size());
    // Resetting drawable entries does not free either reserved index stream.
    EXPECT_EQ(owned, mesh.GetMemoryOccupation() - before);
    group->m_Primitives.Clear(); EXPECT_EQ(before, mesh.GetMemoryOccupation());
}
TEST_F(MeshLifetimeMemoryTest, ShrinkingRemapUVChannelsRetainsInactiveChannelBuffersUntilCleared) {
    MeshMemoryProbe mesh(Context); CKMaterialGroup *group = mesh.Group();
    const int before = mesh.GetMemoryOccupation(); CKVBuffer *buffer = new CKVBuffer;
    group->m_RemapData = reinterpret_cast<CKUINTPTR>(buffer);
    buffer->m_UVs.Reserve(2); buffer->m_UVs.Resize(2);
    buffer->m_UVs[0].Reserve(3); buffer->m_UVs[1].Reserve(11);
    const int bufferOnly = static_cast<int>(sizeof(CKVBuffer));
    const int owned = static_cast<int>(sizeof(CKVBuffer) + 2 * sizeof(XArray<Vx2DVector>)
        + 14 * sizeof(Vx2DVector));
    EXPECT_EQ(owned, mesh.GetMemoryOccupation() - before);
    buffer->m_UVs.Resize(1);
    // The inactive second channel retains its allocation until XClassArray::Clear.
    EXPECT_EQ(owned, mesh.GetMemoryOccupation() - before);
    buffer->m_UVs.Clear(); EXPECT_EQ(bufferOnly, mesh.GetMemoryOccupation() - before);
    mesh.DeleteVBuffer(group); EXPECT_EQ(before, mesh.GetMemoryOccupation());
}
