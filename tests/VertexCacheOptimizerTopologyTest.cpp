#include <gtest/gtest.h>
#include "VertexCacheOptimizer.h"

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <vector>

namespace {
typedef std::array<CKWORD, 3> Triangle;
typedef std::map<Triangle, int> TriangleCounts;

XArray<CKWORD> Indices(const CKWORD *values, int count) {
    XArray<CKWORD> result;
    for (int i = 0; i < count; ++i)
        result.PushBack(values[i]);
    return result;
}

TriangleCounts CountTriangles(const XArray<CKWORD> &indices) {
    TriangleCounts result;
    for (int i = 0; i < indices.Size(); i += 3) {
        const Triangle triangle = {{indices[i], indices[i + 1], indices[i + 2]}};
        ++result[triangle];
    }
    return result;
}

void ExpectInput(const XArray<CKWORD> &input, const std::vector<CKWORD> &snapshot) {
    ASSERT_EQ(static_cast<int>(snapshot.size()), input.Size());
    for (int i = 0; i < input.Size(); ++i)
        EXPECT_EQ(snapshot[static_cast<std::size_t>(i)], input[i]);
}

void ExpectCompleted(const VertexCacheOptimizer &optimizer,
                     const XArray<CKWORD> &input, int vertexCount, int cacheSize) {
    const XArray<CKWORD> &output = optimizer.m_OutputIndices;
    ASSERT_EQ(input.Size(), output.Size());
    EXPECT_EQ(CountTriangles(input), CountTriangles(output));
    EXPECT_EQ(input.Size() / 3, optimizer.m_ProcessedCount);
    EXPECT_EQ(input.Size() / 3, optimizer.m_FaceCount);
    EXPECT_EQ(cacheSize, optimizer.m_CacheSize);
    ASSERT_EQ(input.Size() / 3, optimizer.m_VertexFaceMap.Size());
    for (int face = 0; face < input.Size() / 3; ++face) {
        SCOPED_TRACE(face);
        EXPECT_TRUE(optimizer.m_ProcessedFaces.IsSet(face));
        EXPECT_EQ(nullptr, optimizer.m_VertexFaceMap[face]);
    }

    // Model chronological FIFO from the actual emitted stream, rather than
    // repeating the optimizer's active-face selection or scoring algorithm.
    std::deque<int> fifo;
    if (vertexCount > 0)
        fifo.push_back(0); // The public processing operation seeds vertex0.
    for (int i = 0; i < output.Size(); ++i) {
        const int vertex = output[i];
        if (std::find(fifo.begin(), fifo.end(), vertex) != fifo.end())
            continue;
        if (static_cast<int>(fifo.size()) == cacheSize)
            fifo.pop_front();
        fifo.push_back(vertex);
    }
    ASSERT_EQ(static_cast<int>(fifo.size()), optimizer.m_CacheEntries.Size());
    for (int i = 0; i < optimizer.m_CacheEntries.Size(); ++i)
        EXPECT_EQ(fifo[static_cast<std::size_t>(i)], optimizer.m_CacheEntries[i]);
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        const bool present = std::find(fifo.begin(), fifo.end(), vertex) != fifo.end();
        EXPECT_EQ(present, optimizer.m_CacheSet.IsSet(vertex) != 0);
    }
    for (int i = 0; i < optimizer.m_ActiveFaces.Size(); ++i) {
        const VertexCacheOptimizer::CacheFaces &active = optimizer.m_ActiveFaces[i];
        ASSERT_GE(active.FaceIndex, 0);
        ASSERT_LT(active.FaceIndex, input.Size() / 3);
        EXPECT_GE(active.HitCount, 1);
        EXPECT_LE(active.HitCount, 3);
        const int start = active.FaceIndex * 3;
        EXPECT_TRUE(active.MinVertexIndex == input[start] ||
                    active.MinVertexIndex == input[start + 1] ||
                    active.MinVertexIndex == input[start + 2]);
    }
}

void OptimizeAndCheck(VertexCacheOptimizer &optimizer, const XArray<CKWORD> &input,
                      int vertexCount, int cacheSize) {
    std::vector<CKWORD> snapshot;
    for (int i = 0; i < input.Size(); ++i)
        snapshot.push_back(input[i]);
    optimizer.Initialize(vertexCount, input.Size() / 3, cacheSize);
    EXPECT_EQ(0, optimizer.m_ProcessedCount);
    EXPECT_EQ(0, optimizer.m_CacheEntries.Size());
    EXPECT_EQ(0, optimizer.m_OutputIndices.Size());
    ASSERT_EQ(input.Size() / 3, optimizer.m_VertexFaceMap.Size());
    for (int face = 0; face < input.Size() / 3; ++face) {
        EXPECT_EQ(nullptr, optimizer.m_VertexFaceMap[face]);
        EXPECT_FALSE(optimizer.m_ProcessedFaces.IsSet(face));
    }
    optimizer.BuildVertexFaceLists(input);
    optimizer.ProcessFaces(input);
    ExpectCompleted(optimizer, input, vertexCount, cacheSize);
    ExpectInput(input, snapshot);
}

TEST(VertexCacheOptimizerTopologyTest, VertexIncidenceListsRetainRepeatedCornersAndUnusedVertices) {
    const CKWORD data[] = {0, 0, 0, 0, 1, 0, 2, 3, 1};
    XArray<CKWORD> input = Indices(data, 9);
    VertexCacheOptimizer optimizer;
    optimizer.Initialize(6, 3, 3);
    optimizer.BuildVertexFaceLists(input);
    const int counts[] = {5, 2, 1, 1, 0, 0};
    const int expected[][5] = {{0, 0, 0, 1, 1}, {1, 2}, {2}, {2}, {}, {}};
    ASSERT_EQ(6, optimizer.m_VertexFaceLists.Size());
    for (int vertex = 0; vertex < 6; ++vertex) {
        SCOPED_TRACE(vertex);
        ASSERT_EQ(counts[vertex], optimizer.m_VertexFaceLists[vertex].Size());
        for (int i = 0; i < counts[vertex]; ++i)
            EXPECT_EQ(expected[vertex][i], optimizer.m_VertexFaceLists[vertex][i]);
    }
    EXPECT_GE(optimizer.m_ActiveFaces.Allocated(), 3 * 5);
    optimizer.ProcessFaces(input);
    ExpectCompleted(optimizer, input, 6, 3);
}

TEST(VertexCacheOptimizerTopologyTest, ConnectedAndDisconnectedFacesRetainOrientationAndMultiplicity) {
    const CKWORD data[] = {0, 1, 2, 2, 1, 3, 3, 1, 4, 4, 1, 5,
                           2, 1, 0, 0, 1, 2, 8, 9, 10, 10, 9, 11};
    const XArray<CKWORD> input = Indices(data, 24);
    const int capacities[] = {1, 2, 3, 6, 16};
    for (int i = 0; i < 5; ++i) {
        SCOPED_TRACE(capacities[i]);
        VertexCacheOptimizer optimizer;
        OptimizeAndCheck(optimizer, input, 13, capacities[i]);
    }
}

TEST(VertexCacheOptimizerTopologyTest, FullyCachedDuplicateAndReverseFacesPrecedeTheCostlierCandidate) {
    const CKWORD data[] = {0, 1, 2, 0, 3, 4, 0, 1, 2, 2, 1, 0};
    const XArray<CKWORD> input = Indices(data, 12);
    VertexCacheOptimizer optimizer;
    OptimizeAndCheck(optimizer, input, 5, 3);
    const XArray<CKWORD> &output = optimizer.GetOutputIndices();
    ASSERT_EQ(12, output.Size());
    for (int i = 0; i < 9; ++i)
        EXPECT_LE(output[i], 2);
    EXPECT_EQ(0, output[9]);
    EXPECT_EQ(3, output[10]);
    EXPECT_EQ(4, output[11]);
}

TEST(VertexCacheOptimizerTopologyTest, LowerMissCandidateWinsAfterTheInitialTriangleWithoutLeakingTemporaryEvictions) {
    const CKWORD data[] = {0, 1, 2, 0, 3, 4, 1, 2, 5};
    const XArray<CKWORD> input = Indices(data, 9);
    VertexCacheOptimizer optimizer;
    OptimizeAndCheck(optimizer, input, 6, 3);
    const XArray<CKWORD> &output = optimizer.GetOutputIndices();
    const CKWORD expected[] = {0, 1, 2, 1, 2, 5, 0, 3, 4};
    ASSERT_EQ(9, output.Size());
    for (int i = 0; i < 9; ++i)
        EXPECT_EQ(expected[i], output[i]);
}

TEST(VertexCacheOptimizerTopologyTest, HighValenceFrontiersPreserveMapStorageAndEveryFace) {
    XArray<CKWORD> input;
    for (int i = 1; i <= 180; ++i) {
        input.PushBack(0);
        input.PushBack(static_cast<CKWORD>(i));
        input.PushBack(static_cast<CKWORD>(i + 1));
    }
    const int capacities[] = {1, 2, 4, 16};
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(capacities[i]);
        VertexCacheOptimizer optimizer;
        optimizer.Initialize(182, 180, capacities[i]);
        optimizer.BuildVertexFaceLists(input);
        EXPECT_GE(optimizer.m_ActiveFaces.Allocated(), capacities[i] * 180);
        const VertexCacheOptimizer::CacheFaces *reservedStorage = optimizer.m_ActiveFaces.Begin();
        optimizer.ProcessFaces(input);
        EXPECT_EQ(reservedStorage, optimizer.m_ActiveFaces.Begin());
        ExpectCompleted(optimizer, input, 182, capacities[i]);
    }
}

TEST(VertexCacheOptimizerTopologyTest, DegenerateAndDuplicateTrianglesCompleteWithTheirOriginalCornerSequences) {
    const CKWORD data[] = {0, 0, 0, 0, 1, 0, 2, 2, 3, 3, 4, 4,
                           4, 4, 4, 5, 6, 7, 7, 7, 5, 0, 1, 0};
    const XArray<CKWORD> input = Indices(data, 24);
    const int capacities[] = {1, 2, 3, 8};
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(capacities[i]);
        VertexCacheOptimizer optimizer;
        OptimizeAndCheck(optimizer, input, 9, capacities[i]);
    }
}

TEST(VertexCacheOptimizerTopologyTest, ReinitializationAcrossMeshSizesClearsOutputAndProcessingDomains) {
    const CKWORD firstData[] = {0, 1, 2, 2, 1, 3, 3, 1, 4, 4, 1, 5};
    const CKWORD secondData[] = {1, 1, 0, 0, 1, 0};
    const CKWORD lastData[] = {8, 7, 6, 6, 7, 5, 3, 4, 2, 0, 1, 2, 8, 8, 8};
    const XArray<CKWORD> first = Indices(firstData, 12);
    const XArray<CKWORD> second = Indices(secondData, 6);
    const XArray<CKWORD> empty;
    const XArray<CKWORD> last = Indices(lastData, 15);
    VertexCacheOptimizer optimizer;
    OptimizeAndCheck(optimizer, first, 6, 8);
    OptimizeAndCheck(optimizer, second, 2, 1);
    optimizer.Initialize(0, 0, 3);
    optimizer.BuildVertexFaceLists(empty);
    optimizer.ProcessFaces(empty);
    EXPECT_EQ(0, optimizer.m_ProcessedCount);
    EXPECT_EQ(0, optimizer.m_FaceCount);
    EXPECT_EQ(0, optimizer.m_OutputIndices.Size());
    EXPECT_EQ(0, optimizer.m_CacheEntries.Size());
    EXPECT_EQ(0, optimizer.m_VertexFaceLists.Size());
    EXPECT_EQ(0, optimizer.m_VertexFaceMap.Size());
    OptimizeAndCheck(optimizer, last, 9, 2);
}

TEST(VertexCacheOptimizerTopologyTest, WordBoundaryLabelsRemainDistinctInIncidenceMapsAndOutput) {
    const CKWORD data[] = {0, 32768, 65535, 65535, 32768, 65534,
                           32767, 65534, 32768, 0, 0, 65535};
    const XArray<CKWORD> input = Indices(data, 12);
    VertexCacheOptimizer optimizer;
    OptimizeAndCheck(optimizer, input, 65536, 3);
}
}
