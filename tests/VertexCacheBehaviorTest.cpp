#include <gtest/gtest.h>
#include "VertexCache.h"

#include <algorithm>
#include <deque>

namespace {
// Independent chronological model: oldest slot is at the deque front. The
// production cache stores newest first and shifts its raw array instead.
void CheckMembership(const VertexCache &cache, const std::deque<int> &chronological) {
    for (int label = -1; label <= 9; ++label) {
        SCOPED_TRACE(label);
        const bool expected = std::find(chronological.begin(), chronological.end(), label) != chronological.end();
        EXPECT_EQ(expected, cache.InCache(label));
    }
}

int AppendChronologically(std::deque<int> &chronological, int label) {
    const int evicted = chronological.front();
    chronological.pop_front();
    chronological.push_back(label);
    return evicted;
}

TEST(VertexCacheBehaviorTest, InitialSentinelsAreReplacedBeforeTheOldestInsertedEntryIsEvicted) {
    VertexCache cache(4);
    EXPECT_TRUE(cache.InCache(-1));
    for (int label = 10; label < 14; ++label) {
        SCOPED_TRACE(label);
        EXPECT_EQ(-1, cache.AddEntry(label));
        EXPECT_TRUE(cache.InCache(label));
        EXPECT_EQ(label != 13, cache.InCache(-1));
    }
    EXPECT_EQ(10, cache.AddEntry(20));
    EXPECT_FALSE(cache.InCache(10));
    EXPECT_TRUE(cache.InCache(11));
    EXPECT_TRUE(cache.InCache(12));
    EXPECT_TRUE(cache.InCache(13));
    EXPECT_TRUE(cache.InCache(20));
}

TEST(VertexCacheBehaviorTest, MembershipHitsAndMissesDoNotPromoteOrOtherwiseMutateTheFifoOrder) {
    VertexCache cache(3);
    EXPECT_EQ(-1, cache.AddEntry(10));
    EXPECT_EQ(-1, cache.AddEntry(20));
    EXPECT_EQ(-1, cache.AddEntry(30));
    const VertexCache &lookup = cache;
    for (int repeat = 0; repeat < 5; ++repeat) {
        EXPECT_TRUE(lookup.InCache(10));
        EXPECT_FALSE(lookup.InCache(99));
    }
    EXPECT_EQ(10, cache.AddEntry(40));
    EXPECT_FALSE(cache.InCache(10));
    EXPECT_EQ(20, cache.AddEntry(50));
    EXPECT_TRUE(cache.InCache(30));
    EXPECT_TRUE(cache.InCache(40));
    EXPECT_TRUE(cache.InCache(50));
}

TEST(VertexCacheBehaviorTest, DirectDuplicateInsertionsOccupyDistinctSlotsAndEvictEachOccurrence) {
    VertexCache cache(3);
    EXPECT_EQ(-1, cache.AddEntry(1));
    EXPECT_EQ(-1, cache.AddEntry(2));
    EXPECT_EQ(-1, cache.AddEntry(3));
    EXPECT_EQ(1, cache.AddEntry(2)); // Duplicate2 is inserted, not merely touched.
    EXPECT_FALSE(cache.InCache(1));
    EXPECT_EQ(2, cache.AddEntry(4)); // The older occurrence is evicted first.
    EXPECT_TRUE(cache.InCache(2));
    EXPECT_EQ(3, cache.AddEntry(5));
    EXPECT_TRUE(cache.InCache(2));
    EXPECT_EQ(2, cache.AddEntry(6));
    EXPECT_FALSE(cache.InCache(2));
    EXPECT_TRUE(cache.InCache(4));
    EXPECT_TRUE(cache.InCache(5));
    EXPECT_TRUE(cache.InCache(6));
}

TEST(VertexCacheBehaviorTest, RepeatedInsertionStreamsAgreeWithAnIndependentChronologicalQueue) {
    const int capacities[] = {1, 2, 4, 16};
    const int labels[] = {1, 2, 3, 1, 4, 3, 5, 1, 0, 9, 9, 2, 6, 7, 1, 8,
                          3, 4, 3, 5, 2, 0, 8, 7, 6, 9, 1, 2, 3, 4, 5, 6};
    for (int capacityIndex = 0; capacityIndex < 4; ++capacityIndex) {
        SCOPED_TRACE(capacityIndex);
        const int capacity = capacities[capacityIndex];
        VertexCache cache(capacity);
        std::deque<int> chronological(static_cast<std::size_t>(capacity), -1);
        CheckMembership(cache, chronological);
        for (int step = 0; step < 32; ++step) {
            SCOPED_TRACE(step);
            const int evicted = AppendChronologically(chronological, labels[step]);
            EXPECT_EQ(evicted, cache.AddEntry(labels[step]));
            CheckMembership(cache, chronological);
            // Repeated observations must not alter the next eviction.
            EXPECT_TRUE(cache.InCache(labels[step]));
            CheckMembership(cache, chronological);
        }
    }
}

TEST(VertexCacheBehaviorTest, MissOnlyPublicAccessSequenceKeepsHitsStationaryAndAddsOnlyMissingFaceLabels) {
    VertexCache cache(3);
    std::deque<int> chronological(3, -1);
    const int faces[][3] = {{0, 1, 2}, {0, 1, 2}, {0, 3, 4}, {2, 2, 5}, {4, 5, 6}};
    const int expectedMisses[] = {3, 0, 2, 1, 1};
    for (int face = 0; face < 5; ++face) {
        SCOPED_TRACE(face);
        int misses = 0;
        for (int vertex = 0; vertex < 3; ++vertex) {
            SCOPED_TRACE(vertex);
            const int label = faces[face][vertex];
            const bool expectedHit = std::find(chronological.begin(), chronological.end(), label) != chronological.end();
            ASSERT_EQ(expectedHit, cache.InCache(label));
            if (!expectedHit) {
                ++misses;
                const int evicted = AppendChronologically(chronological, label);
                EXPECT_EQ(evicted, cache.AddEntry(label));
            }
            CheckMembership(cache, chronological);
        }
        EXPECT_EQ(expectedMisses[face], misses);
    }
    EXPECT_FALSE(cache.InCache(2));
    EXPECT_TRUE(cache.InCache(4));
    EXPECT_TRUE(cache.InCache(5));
    EXPECT_TRUE(cache.InCache(6));
}

TEST(VertexCacheBehaviorTest, IntegerLabelsKeepAllThirtyTwoBitsWithoutWordTruncation) {
    VertexCache cache(3);
    EXPECT_EQ(-1, cache.AddEntry(65535));
    EXPECT_EQ(-1, cache.AddEntry(65536));
    EXPECT_EQ(-1, cache.AddEntry(0x7fffffff));
    EXPECT_TRUE(cache.InCache(65535));
    EXPECT_TRUE(cache.InCache(65536));
    EXPECT_TRUE(cache.InCache(0x7fffffff));
    EXPECT_FALSE(cache.InCache(0));
    EXPECT_FALSE(cache.InCache(32767));
    EXPECT_EQ(65535, cache.AddEntry(0));
    EXPECT_FALSE(cache.InCache(65535));
    EXPECT_TRUE(cache.InCache(65536));
    EXPECT_TRUE(cache.InCache(0x7fffffff));
    EXPECT_TRUE(cache.InCache(0));
}
}
