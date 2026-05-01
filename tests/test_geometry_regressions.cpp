#include "MeshAdjacency.h"
#include "NearestPointGrid.h"
#include "NvStripifier.h"
#include "RadixSort.h"
#include "TestTriangleMultiset.h"
#include "VertexCacheOptimizer.h"

namespace {

void BuildTriangleMultisetFromJoinedStrip(const XArray<CKWORD> &stripIndices, XArray<TestTriCount> &tris) {
    tris.Resize(0);
    for (int i = 2; i < stripIndices.Size(); ++i) {
        const int a = (int)stripIndices[i - 2];
        const int b = (int)stripIndices[i - 1];
        const int c = (int)stripIndices[i];
        if (a == b || b == c || a == c)
            continue;
        TestAddTriangle(tris, TestMakeTriKey(a, b, c));
    }
}

void Test_RadixSorter_EmptyInput_IsSafe() {
    RadixSorter sorter;
    sorter.Sort((CKDWORD *)nullptr, 0, false);
    sorter.Sort((float *)nullptr, 0);
    TestCheck(true, "Zero-length sorts should be no-ops");
}

void Test_MeshAdjacency_CompactsInvalidEdges() {
    CKWORD indices[] = {0, 1, 2, 2, 1, 3};

    MeshAdjacency adjacency;
    adjacency.Init(indices, 2);
    TestCheck(adjacency.Compute(true, true), "Adjacency computation failed");
    TestCheck(adjacency.GetEdges().Size() == 5, "Shared edge compaction should leave 5 unique edges");

    const XArray<MeshAdjacency::Edge> &edges = adjacency.GetEdges();
    for (int i = 0; i < edges.Size(); ++i) {
        TestCheck(edges[i].faces[0] != 0xffffffff, "Invalid duplicate edge leaked through compaction");
    }
}

void Test_NearestPointGrid_ChoosesNearestPoint() {
    NearestPointGrid grid;
    grid.SetGridDimensions(4, 4, 4);
    grid.SetThreshold(0.3f);
    grid.AddPoint(VxVector(1.25f, 1.0f, 1.0f), 10);
    grid.AddPoint(VxVector(1.10f, 1.0f, 1.0f), 20);

    TestCheck(grid.FindNearPoint(VxVector(1.0f, 1.0f, 1.0f)) == 20, "Grid should return the nearest match, not the first one");
}

void Test_NearestPointGrid_RejectsOutsideEuclideanThreshold() {
    NearestPointGrid grid;
    grid.SetGridDimensions(4, 4, 4);
    grid.SetThreshold(0.3f);
    grid.AddPoint(VxVector(1.25f, 1.25f, 1.25f), 10);

    TestCheck(grid.FindNearPoint(VxVector(1.0f, 1.0f, 1.0f)) == -1, "Grid should reject points outside the spherical threshold");
}

void Test_NvStripifier_EmptyInput_ReturnsEmptyOutput() {
    NvStripifier stripifier;
    XArray<CKWORD> inIndices;
    XArray<NvStripInfo *> strips;
    stripifier.Stripify(inIndices, 0, 16, 0, strips);
    TestCheck(strips.Size() == 0, "Empty input should produce no strips");

    XArray<CKWORD> outIndices;
    CKDWORD outStripCount = 123;
    NvStripifier::CreateStrips(strips, outIndices, false, outStripCount);
    TestCheck(outIndices.Size() == 0, "Empty strip set should produce no indices");
    TestCheck(outStripCount == 0, "Empty strip set should report zero strips");
}

void Test_NvStripifier_HighIndexFallback_UsesJoinedStream() {
    XArray<CKWORD> inIndices;
    inIndices.PushBack(0);
    inIndices.PushBack(1);
    inIndices.PushBack((CKWORD)0xFFFF);
    inIndices.PushBack(10);
    inIndices.PushBack(11);
    inIndices.PushBack(12);

    NvStripifier stripifier;
    XArray<NvStripInfo *> strips;
    stripifier.Stripify(inIndices, 64, 16, (CKWORD)0xFFFF, strips);

    XArray<CKWORD> outIndices;
    CKDWORD outStripCount = 0;
    NvStripifier::CreateStrips(strips, outIndices, false, outStripCount);

    TestCheck(outStripCount == 1, "High-index fallback should emit a single joined stream");
    TestCheck(outIndices.Size() > 0, "Joined fallback should still emit indices");

    bool sawHighIndex = false;
    for (int i = 0; i < outIndices.Size(); ++i) {
        if (outIndices[i] == (CKWORD)0xFFFF) {
            sawHighIndex = true;
            break;
        }
    }
    TestCheck(sawHighIndex, "Output should preserve the 0xFFFF vertex index as real data");

    XArray<TestTriCount> expected;
    XArray<TestTriCount> actual;
    TestBuildTriangleMultisetFromTriList(inIndices, expected);
    BuildTriangleMultisetFromJoinedStrip(outIndices, actual);
    TestCheck(TestSameTriangleMultiset(expected, actual), "Joined fallback should preserve triangle coverage");

    NvStripifier::DestroyStrips(strips);
}

void Test_VertexCacheOptimizer_OutOfRangeIndex_DoesNotCrash() {
    XArray<CKWORD> indices;
    indices.PushBack(0);
    indices.PushBack(1);
    indices.PushBack(7);
    indices.PushBack(7);
    indices.PushBack(2);
    indices.PushBack(3);

    VertexCacheOptimizer optimizer;
    optimizer.Initialize(/*vertexCount=*/4, /*faceCount=*/2, /*cacheSize=*/16);
    optimizer.BuildVertexFaceLists(indices);
    optimizer.ProcessFaces(indices);

    XArray<CKWORD> &out = optimizer.GetOutputIndices();
    TestCheck(out.Size() == indices.Size(), "Optimizer should preserve all triangle indices");

    XArray<TestTriCount> expected;
    XArray<TestTriCount> actual;
    TestBuildTriangleMultisetFromTriList(indices, expected);
    TestBuildTriangleMultisetFromTriList(out, actual);
    TestCheck(TestSameTriangleMultiset(expected, actual), "Optimizer should preserve triangle coverage with out-of-range indices");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Radix sorter empty input", &Test_RadixSorter_EmptyInput_IsSafe);
    tests.Run("Mesh adjacency compaction", &Test_MeshAdjacency_CompactsInvalidEdges);
    tests.Run("Nearest point chooses nearest", &Test_NearestPointGrid_ChoosesNearestPoint);
    tests.Run("Nearest point rejects spherical miss", &Test_NearestPointGrid_RejectsOutsideEuclideanThreshold);
    tests.Run("Stripifier empty input", &Test_NvStripifier_EmptyInput_ReturnsEmptyOutput);
    tests.Run("Stripifier high-index fallback", &Test_NvStripifier_HighIndexFallback_UsesJoinedStream);
    tests.Run("Vertex cache optimizer out-of-range index", &Test_VertexCacheOptimizer_OutOfRangeIndex_DoesNotCrash);
    return tests.ExitCode();
}
