#include <gtest/gtest.h>
#include <cstdlib>
#include <new>
#include "CKAll.h"
#include "RCKMesh.h"

namespace {
struct ReleaseWatch { void *Pointer; int Releases; };
ReleaseWatch Watches[8];
int WatchCount = 0;
int Watch(void *pointer) {
    const int index = WatchCount++;
    Watches[index] = {pointer, 0};
    return index;
}
void Release(void *pointer) {
    for (int i = 0; i < WatchCount; ++i) {
        if (pointer && Watches[i].Pointer == pointer) {
            ++Watches[i].Releases;
            return; // Quarantine after the real destructor, preventing address reuse.
        }
    }
    std::free(pointer);
}
}

void *operator new(std::size_t bytes) {
    void *result = std::malloc(bytes ? bytes : 1);
    if (!result) throw std::bad_alloc();
    return result;
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *pointer) noexcept { Release(pointer); }
void operator delete[](void *pointer) noexcept { Release(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { Release(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { Release(pointer); }

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
class ProgressiveMeshOwnershipTest : public ::testing::Test {
protected:
    void SetUp() override { WatchCount = 0; }
    void TearDown() override {
        for (int i = 0; i < WatchCount; ++i) {
            EXPECT_EQ(1, Watches[i].Releases);
            if (Watches[i].Releases) std::free(Watches[i].Pointer);
        }
        WatchCount = 0;
    }
    static void Snapshot(CKProgressiveMesh &pm, int seed) {
        pm.m_VertexCount = 3;
        pm.m_MorphEnabled = TRUE;
        pm.m_MorphStep = seed;
        pm.m_Reserved = 4;
        for (int i = 0; i < 5; ++i) {
            pm.m_Data.PushBack((CKDWORD)(seed + i));
            pm.m_EdgeCollapseData.PushBack((CKDWORD)(seed + 10 + i));
        }
    }
    static void ExpectSnapshot(const CKProgressiveMesh &pm, int seed) {
        EXPECT_EQ(3, pm.m_VertexCount);
        EXPECT_EQ(TRUE, pm.m_MorphEnabled);
        EXPECT_EQ(seed, pm.m_MorphStep);
        EXPECT_EQ(4, pm.m_Reserved);
        ASSERT_EQ(5, pm.m_Data.Size());
        ASSERT_EQ(5, pm.m_EdgeCollapseData.Size());
        for (int i = 0; i < 5; ++i) {
            EXPECT_EQ((CKDWORD)(seed + i), pm.m_Data[i]);
            EXPECT_EQ((CKDWORD)(seed + 10 + i), pm.m_EdgeCollapseData[i]);
        }
    }
};
class ProgressiveMeshProbe : public RCKMesh {
public:
    explicit ProgressiveMeshProbe(CKContext *context) : RCKMesh(context) {}
    CKProgressiveMesh *Data() const { return m_ProgressiveMesh; }
};
}

TEST_F(ProgressiveMeshOwnershipTest, DefaultOwnersHaveIndependentEmptyState) {
    CKProgressiveMesh a, b;
    EXPECT_EQ(-1, a.m_VertexCount);
    EXPECT_EQ(0, a.m_MorphEnabled);
    EXPECT_EQ(0, a.m_MorphStep);
    EXPECT_EQ(0, a.m_Reserved);
    EXPECT_EQ(nullptr, a.m_EdgeCollapseStruct);
    EXPECT_EQ(0, a.m_Data.Size());
    EXPECT_EQ(0, a.m_EdgeCollapseData.Size());
    a.m_Data.PushBack(42);
    EXPECT_EQ(0, b.m_Data.Size());
}

TEST_F(ProgressiveMeshOwnershipTest, CopyConstructedSnapshotsOutliveSourceAndDoNotShareRuntimeGraph) {
    CKProgressiveMesh *source = new CKProgressiveMesh;
    Snapshot(*source, 12);
    source->m_EdgeCollapseStruct = new PMEdgeCollapseData;
    const int sourceGraph = Watch(source->m_EdgeCollapseStruct);
    CKProgressiveMesh copy(*source);
    ExpectSnapshot(copy, 12);
    EXPECT_EQ(nullptr, copy.m_EdgeCollapseStruct);
    EXPECT_NE(source->m_Data.Begin(), copy.m_Data.Begin());
    EXPECT_NE(source->m_EdgeCollapseData.Begin(), copy.m_EdgeCollapseData.Begin());
    source->m_Data[0] = 999;
    EXPECT_EQ(12u, copy.m_Data[0]);
    delete source;
    EXPECT_EQ(1, Watches[sourceGraph].Releases);
    ExpectSnapshot(copy, 12);
}

TEST_F(ProgressiveMeshOwnershipTest, AssignmentReplacesSnapshotAndReleasesDestinationConstructionGraph) {
    CKProgressiveMesh source, destination;
    Snapshot(source, 20);
    Snapshot(destination, 40);
    source.m_EdgeCollapseStruct = new PMEdgeCollapseData;
    destination.m_EdgeCollapseStruct = new PMEdgeCollapseData;
    const int sourceGraph = Watch(source.m_EdgeCollapseStruct);
    const int destinationGraph = Watch(destination.m_EdgeCollapseStruct);
    EXPECT_EQ(&destination, &(destination = source));
    ExpectSnapshot(destination, 20);
    EXPECT_EQ(nullptr, destination.m_EdgeCollapseStruct);
    EXPECT_EQ(1, Watches[destinationGraph].Releases);
    EXPECT_EQ(0, Watches[sourceGraph].Releases);
    EXPECT_NE(source.m_Data.Begin(), destination.m_Data.Begin());
    EXPECT_NE(source.m_EdgeCollapseData.Begin(), destination.m_EdgeCollapseData.Begin());
    destination.m_Data[0] = 77;
    EXPECT_EQ(20u, source.m_Data[0]);
}

TEST_F(ProgressiveMeshOwnershipTest, SelfAssignmentRetainsLiveGraphAndSnapshotStorage) {
    CKProgressiveMesh pm;
    Snapshot(pm, 30);
    pm.m_EdgeCollapseStruct = new PMEdgeCollapseData;
    const int graph = Watch(pm.m_EdgeCollapseStruct);
    PMEdgeCollapseData *identity = pm.m_EdgeCollapseStruct;
    CKDWORD *parents = pm.m_Data.Begin();
    EXPECT_EQ(&pm, &(pm = pm));
    EXPECT_EQ(identity, pm.m_EdgeCollapseStruct);
    EXPECT_EQ(parents, pm.m_Data.Begin());
    EXPECT_EQ(0, Watches[graph].Releases);
    ExpectSnapshot(pm, 30);
}

TEST_F(ProgressiveMeshOwnershipTest, AssigningAnEmptySnapshotClearsArraysAndOwnedRuntime) {
    CKProgressiveMesh source, destination;
    Snapshot(destination, 50);
    destination.m_EdgeCollapseStruct = new PMEdgeCollapseData;
    const int graph = Watch(destination.m_EdgeCollapseStruct);
    destination = source;
    EXPECT_EQ(-1, destination.m_VertexCount);
    EXPECT_EQ(0, destination.m_Data.Size());
    EXPECT_EQ(0, destination.m_EdgeCollapseData.Size());
    EXPECT_EQ(nullptr, destination.m_EdgeCollapseStruct);
    EXPECT_EQ(1, Watches[graph].Releases);
    destination = source;
    EXPECT_EQ(1, Watches[graph].Releases);
}

TEST_F(ProgressiveMeshOwnershipTest, MeshCopyUsesAnIndependentSnapshotAndDestroyPmKeepsSourceAlive) {
    SetProcessorSpecific_FunctionsPtr();
    ASSERT_EQ(CK_OK, CKStartUp());
    CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
    CKBuildClassHierarchyTable();
    CKContext *context = nullptr;
    ASSERT_EQ(CK_OK, CKCreateContext(&context, nullptr, 0, 0));
    {
        ProgressiveMeshProbe source(context), destination(context);
        ASSERT_TRUE(source.SetVertexCount(4));
        VxVector positions[4] = {VxVector(0, 0, 0), VxVector(1, 0, 0),
                                       VxVector(0, 1, 0), VxVector(1, 1, 0)};
        for (int i = 0; i < 4; ++i) source.SetVertexPosition(i, &positions[i]);
        source.SetFaceCount(2);
        source.SetFaceVertexIndex(0, 0, 1, 2);
        source.SetFaceVertexIndex(1, 2, 1, 3);
        ASSERT_EQ(CK_OK, source.CreatePM());
        CKProgressiveMesh *pm = source.Data();
        ASSERT_NE(nullptr, pm);
        ASSERT_NE(nullptr, pm->m_EdgeCollapseStruct);
        const int sourceGraph = Watch(pm->m_EdgeCollapseStruct);
        CKDependenciesContext dependencies(context);
        ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
        ASSERT_NE(nullptr, destination.Data());
        ASSERT_EQ(pm->m_Data.Size(), destination.Data()->m_Data.Size());
        for (int i = 0; i < pm->m_Data.Size(); ++i)
            EXPECT_EQ(pm->m_Data[i], destination.Data()->m_Data[i]);
        EXPECT_EQ(nullptr, destination.Data()->m_EdgeCollapseStruct);
        EXPECT_NE(pm->m_Data.Begin(), destination.Data()->m_Data.Begin());
        destination.DestroyPM();
        EXPECT_EQ(nullptr, destination.Data());
        EXPECT_EQ(0, Watches[sourceGraph].Releases);
        EXPECT_EQ(pm, source.Data());
        EXPECT_EQ(4, source.GetVertexCount());
    }
    CKCloseContext(context);
    CKShutdown();
}
