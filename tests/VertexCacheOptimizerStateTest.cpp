#include <gtest/gtest.h>
#include <climits>
#include <type_traits>

#include "VertexCacheOptimizer.h"

static_assert(!std::is_copy_constructible<VertexCacheOptimizer>::value,
              "Optimizer scratch and nested arrays must not gain a shallow owner");
static_assert(!std::is_copy_assignable<VertexCacheOptimizer>::value,
              "Optimizer scratch and nested arrays must not gain a shallow assignment owner");

namespace {

void SetIndices(XArray<CKWORD> &target, const CKWORD *values, int count) {
    target.Resize(0);
    for (int i = 0; i < count; ++i) target.PushBack(values[i]);
}

struct FifoObservation {
    int Misses;
    int Count;
    int Resident[32];
};

// Each occupied slot records the time of its insertion. Hits do not update that
// time; a miss replaces the oldest insertion. This oracle uses neither the
// optimizer's bit arrays nor its shifting cache-entry representation.
FifoObservation ObserveFifo(const XArray<CKWORD> &indices, int capacity) {
    FifoObservation result = {};
    int labels[32] = {}, insertionTime[32] = {};
    int occupied = 0, clock = 0;
    for (int i = 0; i < indices.Size(); ++i) {
        const int label = indices[i];
        bool hit = false;
        for (int slot = 0; slot < occupied; ++slot)
            if (labels[slot] == label) hit = true;
        if (hit) continue;
        ++result.Misses;
        if (capacity == 0) continue;
        int slot = occupied;
        if (occupied < capacity) {
            ++occupied;
        } else {
            slot = 0;
            for (int candidate = 1; candidate < occupied; ++candidate)
                if (insertionTime[candidate] < insertionTime[slot]) slot = candidate;
        }
        labels[slot] = label;
        insertionTime[slot] = ++clock;
    }
    result.Count = occupied;
    for (int rank = 0; rank < occupied; ++rank) {
        int oldest = -1;
        for (int slot = 0; slot < occupied; ++slot)
            if (insertionTime[slot] != INT_MAX &&
                (oldest < 0 || insertionTime[slot] < insertionTime[oldest])) oldest = slot;
        result.Resident[rank] = labels[oldest];
        insertionTime[oldest] = INT_MAX;
    }
    return result;
}

::testing::AssertionResult SameOrientedFaces(const XArray<CKWORD> &expected,
                                             const XArray<CKWORD> &actual) {
    if (expected.Size() != actual.Size() || expected.Size() % 3 != 0 || expected.Size() > 48)
        return ::testing::AssertionFailure() << "Output has an invalid triangle count";
    bool used[16] = {};
    for (int outputFace = 0; outputFace < actual.Size() / 3; ++outputFace) {
        bool matched = false;
        for (int inputFace = 0; inputFace < expected.Size() / 3 && !matched; ++inputFace) {
            if (used[inputFace]) continue;
            for (int rotation = 0; rotation < 3 && !matched; ++rotation) {
                bool equal = true;
                for (int corner = 0; corner < 3; ++corner)
                    if (actual[outputFace * 3 + corner] !=
                        expected[inputFace * 3 + (corner + rotation) % 3]) equal = false;
                if (equal) { used[inputFace] = true; matched = true; }
            }
        }
        if (!matched)
            return ::testing::AssertionFailure() << "Output face " << outputFace
                                                  << " has changed orientation or multiplicity";
    }
    return ::testing::AssertionSuccess();
}

void Optimize(VertexCacheOptimizer &optimizer, XArray<CKWORD> &indices,
              int vertexCount, int cacheSize) {
    optimizer.Initialize(vertexCount, indices.Size() / 3, cacheSize);
    optimizer.BuildVertexFaceLists(indices);
    optimizer.ProcessFaces(indices);
}

} // namespace

TEST(VertexCacheOptimizerStateTest, FreshOwnersHaveIndependentEmptyOutputAndScalarState) {
    VertexCacheOptimizer first, second;
    EXPECT_EQ(0, first.GetOutputIndices().Size());
    EXPECT_EQ(0, second.GetOutputIndices().Size());
    EXPECT_EQ(0, first.m_ProcessedCount);
    EXPECT_EQ(0, first.m_FaceCount);
    EXPECT_EQ(0, first.m_CacheSize);
    EXPECT_EQ(0, first.m_CacheEntries.Size());
    EXPECT_EQ(0, first.m_VertexFaceLists.Size());
    EXPECT_EQ(0, first.m_VertexFaceMap.Size());
    first.GetOutputIndices().PushBack(42);
    EXPECT_EQ(1, first.GetOutputIndices().Size());
    EXPECT_EQ(0, second.GetOutputIndices().Size());
}

TEST(VertexCacheOptimizerStateTest, MetricMatchesAnIndependentFifoOracleAndLeavesItsScratchObservable) {
    const CKWORD values[] = {1, 2, 1, 3, 1, 2, 65535, 2, 1, 4, 1, 3, 65535, 0, 4, 2, 1, 0};
    const int capacities[] = {1, 2, 3, 4, 16};
    const int modes[] = {0, 1, -1};
    XArray<CKWORD> indices;
    SetIndices(indices, values, 18);
    VertexCacheOptimizer optimizer;
    for (int c = 0; c < 5; ++c) {
        const FifoObservation expected = ObserveFifo(indices, capacities[c]);
        for (int m = 0; m < 3; ++m) {
            const int divisor = modes[m] ? 16 : 6;
            EXPECT_FLOAT_EQ(static_cast<float>(expected.Misses) / divisor,
                            optimizer.ComputeCacheMetric(indices, capacities[c], modes[m]));
            EXPECT_EQ(capacities[c], optimizer.m_CacheSize);
            ASSERT_EQ(expected.Count, optimizer.m_CacheEntries.Size());
            for (int i = 0; i < expected.Count; ++i)
                EXPECT_EQ(expected.Resident[i], optimizer.m_CacheEntries[i]);
            for (int labelIndex = 0; labelIndex < 18; ++labelIndex) {
                bool resident = false;
                for (int slot = 0; slot < expected.Count; ++slot)
                    if (expected.Resident[slot] == values[labelIndex]) resident = true;
                EXPECT_EQ(resident, optimizer.m_CacheSet.IsSet(values[labelIndex]) != 0);
            }
            for (int i = 0; i < 18; ++i) EXPECT_EQ(values[i], indices[i]);
        }
    }
}

TEST(VertexCacheOptimizerStateTest, ListAndStripDenominatorsIncludeAllStreamReferences) {
    const CKWORD values[] = {0, 1, 2, 2, 3, 3, 1, 0, 4};
    XArray<CKWORD> indices;
    SetIndices(indices, values, 9);
    const FifoObservation expected = ObserveFifo(indices, 4);
    VertexCacheOptimizer optimizer;
    EXPECT_FLOAT_EQ(static_cast<float>(expected.Misses) / 3,
                    optimizer.ComputeCacheMetric(indices, 4, 0));
    EXPECT_FLOAT_EQ(static_cast<float>(expected.Misses) / 7,
                    optimizer.ComputeCacheMetric(indices, 4, 1));
    EXPECT_FLOAT_EQ(static_cast<float>(expected.Misses) / 7,
                    optimizer.ComputeCacheMetric(indices, 4, 8));
}

TEST(VertexCacheOptimizerStateTest, DisabledAndNontriangleMetricInputsReturnZeroAndKeepOutput) {
    const CKWORD triangle[] = {0, 2, 1};
    XArray<CKWORD> indices, empty, shortStream;
    SetIndices(indices, triangle, 3);
    SetIndices(shortStream, triangle, 2);
    VertexCacheOptimizer optimizer;
    Optimize(optimizer, indices, 3, 4);
    ASSERT_TRUE(SameOrientedFaces(indices, optimizer.GetOutputIndices()));
    EXPECT_FLOAT_EQ(0, optimizer.ComputeCacheMetric(indices, 0, 0));
    EXPECT_FLOAT_EQ(0, optimizer.ComputeCacheMetric(indices, -1, 1));
    EXPECT_FLOAT_EQ(0, optimizer.ComputeCacheMetric(empty, 4, 0));
    EXPECT_FLOAT_EQ(0, optimizer.ComputeCacheMetric(empty, 4, 1));
    EXPECT_FLOAT_EQ(0, optimizer.ComputeCacheMetric(shortStream, 4, 0));
    EXPECT_FLOAT_EQ(0, optimizer.ComputeCacheMetric(shortStream, 4, 1));
    EXPECT_TRUE(SameOrientedFaces(indices, optimizer.GetOutputIndices()));
}

TEST(VertexCacheOptimizerStateTest, InitializeAfterMetricRestoresTheFaceAndVertexDomains) {
    const CKWORD metricStream[] = {65535, 64000, 65535, 1, 64000, 2};
    const CKWORD triangles[] = {0, 1, 2, 3, 4, 5};
    XArray<CKWORD> metricIndices, indices;
    SetIndices(metricIndices, metricStream, 6);
    SetIndices(indices, triangles, 6);
    VertexCacheOptimizer optimizer;
    const FifoObservation metric = ObserveFifo(metricIndices, 2);
    ASSERT_FLOAT_EQ(static_cast<float>(metric.Misses) / 4,
                    optimizer.ComputeCacheMetric(metricIndices, 2, 1));
    optimizer.Initialize(6, 2, 4);
    EXPECT_EQ(0, optimizer.GetOutputIndices().Size());
    EXPECT_EQ(0, optimizer.m_ProcessedCount);
    EXPECT_EQ(2, optimizer.m_FaceCount);
    EXPECT_EQ(0, optimizer.m_CacheEntries.Size());
    for (int i = 0; i < 6; ++i) EXPECT_FALSE(optimizer.m_CacheSet.IsSet(i));
    for (int i = 0; i < 2; ++i) EXPECT_FALSE(optimizer.m_ProcessedFaces.IsSet(i));
    optimizer.BuildVertexFaceLists(indices);
    optimizer.ProcessFaces(indices);
    EXPECT_TRUE(SameOrientedFaces(indices, optimizer.GetOutputIndices()));
    EXPECT_EQ(2, optimizer.m_ProcessedCount);
}

TEST(VertexCacheOptimizerStateTest, CompletedOptimizationMetricAndSmallerInitializationCanShareOneOwner) {
    const CKWORD firstFaces[] = {0, 1, 2, 3, 4, 5};
    const CKWORD secondFace[] = {2, 0, 1};
    const CKWORD metricStream[] = {10, 11, 10, 12, 10, 11};
    XArray<CKWORD> first, second, empty, metricIndices;
    SetIndices(first, firstFaces, 6);
    SetIndices(second, secondFace, 3);
    SetIndices(metricIndices, metricStream, 6);
    VertexCacheOptimizer optimizer;
    Optimize(optimizer, first, 6, 4);
    ASSERT_TRUE(SameOrientedFaces(first, optimizer.GetOutputIndices()));
    ASSERT_TRUE(optimizer.m_ProcessedFaces.IsSet(0));
    ASSERT_TRUE(optimizer.m_ProcessedFaces.IsSet(1));
    const FifoObservation metric = ObserveFifo(metricIndices, 2);
    ASSERT_FLOAT_EQ(static_cast<float>(metric.Misses) / 2,
                    optimizer.ComputeCacheMetric(metricIndices, 2, 0));
    EXPECT_TRUE(optimizer.m_ProcessedFaces.IsSet(0));
    EXPECT_TRUE(optimizer.m_ProcessedFaces.IsSet(1));
    EXPECT_EQ(2, optimizer.m_ProcessedCount);
    ASSERT_EQ(metric.Count, optimizer.m_CacheEntries.Size());
    for (int i = 0; i < metric.Count; ++i) {
        EXPECT_EQ(metric.Resident[i], optimizer.m_CacheEntries[i]);
        EXPECT_TRUE(optimizer.m_CacheSet.IsSet(metric.Resident[i]));
    }
    for (int i = 0; i < 6; ++i) EXPECT_FALSE(optimizer.m_CacheSet.IsSet(i));
    Optimize(optimizer, second, 3, 1);
    EXPECT_TRUE(SameOrientedFaces(second, optimizer.GetOutputIndices()));
    EXPECT_EQ(1, optimizer.m_ProcessedCount);
    EXPECT_EQ(1, optimizer.m_FaceCount);
    EXPECT_EQ(3, optimizer.m_VertexFaceLists.Size());
    EXPECT_EQ(1, optimizer.m_VertexFaceMap.Size());
    Optimize(optimizer, empty, 0, 4);
    EXPECT_EQ(0, optimizer.GetOutputIndices().Size());
    EXPECT_EQ(0, optimizer.m_ProcessedCount);
    EXPECT_EQ(0, optimizer.m_FaceCount);
    EXPECT_EQ(0, optimizer.m_VertexFaceMap.Size());
}

TEST(VertexCacheOptimizerStateTest, BorrowedOutputAndSwapTransferBothOwnedArrays) {
    const CKWORD triangle[] = {0, 2, 1};
    XArray<CKWORD> indices, target;
    SetIndices(indices, triangle, 3);
    target.PushBack(17); target.PushBack(18);
    {
        VertexCacheOptimizer optimizer;
        Optimize(optimizer, indices, 3, 4);
        XArray<CKWORD> &borrowed = optimizer.GetOutputIndices();
        EXPECT_EQ(&borrowed, &optimizer.GetOutputIndices());
        ASSERT_TRUE(SameOrientedFaces(indices, borrowed));
        const CKWORD *originalOutput = borrowed.Begin(), *originalTarget = target.Begin();
        optimizer.SwapOutput(target);
        EXPECT_EQ(originalOutput, target.Begin());
        EXPECT_EQ(originalTarget, borrowed.Begin());
        EXPECT_TRUE(SameOrientedFaces(indices, target));
        ASSERT_EQ(2, borrowed.Size());
        EXPECT_EQ(17, borrowed[0]); EXPECT_EQ(18, borrowed[1]);
        borrowed.PushBack(19);
        EXPECT_EQ(3, borrowed.Size());
        EXPECT_TRUE(SameOrientedFaces(indices, target));
    }
    EXPECT_TRUE(SameOrientedFaces(indices, target));
}

TEST(VertexCacheOptimizerStateTest, CacheFacesConstructorInitializesIndependentScalarSentinels) {
    VertexCacheOptimizer::CacheFaces first, second;
    EXPECT_EQ(0, first.FaceIndex);
    EXPECT_EQ(0, first.HitCount);
    EXPECT_EQ(INT_MAX, first.MinVertexIndex);
    first.FaceIndex = 5; first.HitCount = 2; first.MinVertexIndex = 7;
    EXPECT_EQ(0, second.FaceIndex);
    EXPECT_EQ(0, second.HitCount);
    EXPECT_EQ(INT_MAX, second.MinVertexIndex);
}
