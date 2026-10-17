#include <gtest/gtest.h>
#include "RadixSort.h"

#include <cstddef>
#include <cstdint>

namespace {
std::int64_t IntegerKey(CKDWORD value, bool signedValues) {
    // Interpret the32-bit signed domain without implementation-defined narrowing
    // or reproducing radix bytes, histograms, sign offsets, or bucket passes.
    const std::int64_t positive = static_cast<std::int64_t>(value);
    return signedValues && (value & 0x80000000u) ? positive - 0x100000000LL : positive;
}

template<std::size_t N>
void ExpectIntegerSort(RadixSorter &sorter, CKDWORD (&input)[N], CKDWORD count, bool signedValues) {
    ASSERT_GT(count, 0u);
    ASSERT_LE(count, static_cast<CKDWORD>(N));
    CKDWORD unchanged[N];
    std::int64_t expected[N];
    for (std::size_t i = 0; i < N; ++i) unchanged[i] = input[i];
    for (CKDWORD i = 0; i < count; ++i) expected[i] = IntegerKey(input[i], signedValues);
    // Independent comparison-sort oracle, with no radix implementation details.
    for (CKDWORD i = 1; i < count; ++i) {
        const std::int64_t key = expected[i];
        CKDWORD position = i;
        while (position && expected[position - 1] > key) {
            expected[position] = expected[position - 1];
            --position;
        }
        expected[position] = key;
    }
    EXPECT_EQ(&sorter, &sorter.Sort(input, count, signedValues));
    for (std::size_t i = 0; i < N; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(unchanged[i], input[i]);
    }
    CKDWORD *indices = sorter.GetIndices();
    ASSERT_NE(nullptr, indices);
    bool seen[N] = {};
    for (CKDWORD i = 0; i < count; ++i) {
        SCOPED_TRACE(i);
        const CKDWORD index = indices[i];
        // Check the logical domain before using an index as a key subscript.
        ASSERT_LT(index, count);
        EXPECT_FALSE(seen[index]);
        seen[index] = true;
        EXPECT_EQ(expected[i], IntegerKey(input[index], signedValues));
    }
    for (CKDWORD i = 0; i < count; ++i) EXPECT_TRUE(seen[i]);
}

template<std::size_t N>
void ExpectStableRefinement(RadixSorter &sorter, CKDWORD (&input)[N], CKDWORD (&priorOrder)[N], bool signedValues) {
    CKDWORD unchanged[N];
    for (std::size_t i = 0; i < N; ++i) unchanged[i] = input[i];
    // Stable comparison refinement of the preceding key order, as required by
    // real adjacency owner->vertex0->vertex1 and degree-seed consumers.
    for (std::size_t i = 1; i < N; ++i) {
        const CKDWORD id = priorOrder[i];
        const std::int64_t key = IntegerKey(input[id], signedValues);
        std::size_t position = i;
        while (position && IntegerKey(input[priorOrder[position - 1]], signedValues) > key) {
            priorOrder[position] = priorOrder[position - 1];
            --position;
        }
        priorOrder[position] = id;
    }
    EXPECT_EQ(&sorter, &sorter.Sort(input, static_cast<CKDWORD>(N), signedValues));
    CKDWORD *indices = sorter.GetIndices();
    ASSERT_NE(nullptr, indices);
    for (std::size_t i = 0; i < N; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(unchanged[i], input[i]);
        EXPECT_EQ(priorOrder[i], indices[i]);
    }
}

TEST(RadixSorterIntegerTest, SignedExtremesDuplicatesAndEveryByteProduceACompletePermutation) {
    CKDWORD input[] = {0x80000000u, 0xffffffffu, 0u, 0x7fffffffu, 1u, 0x80000001u,
                       0x7ffffffeu, 0xfffffffeu, 0x01000000u, 0xff000000u, 0x00010000u,
                       0x00000100u, 0x00ff0000u, 0x00000100u, 0u, 0x80000000u};
    RadixSorter sorter;
    ExpectIntegerSort(sorter, input, 16, true);
}

TEST(RadixSorterIntegerTest, UnsignedHighBitValuesUseTheFullThirtyTwoBitOrder) {
    CKDWORD input[] = {0x80000000u, 0xffffffffu, 0u, 0x7fffffffu, 1u, 0x80000001u,
                       0x7ffffffeu, 0xfffffffeu, 0x01000000u, 0xff000000u, 0x00010000u,
                       0x00000100u, 0x00ff0000u, 0x00000100u, 0u, 0x80000000u};
    RadixSorter sorter;
    ExpectIntegerSort(sorter, input, 16, false);
}

TEST(RadixSorterIntegerTest, SwitchingSignedInterpretationAtTheSameCountReordersTheSameUnchangedBits) {
    CKDWORD input[] = {0xffffffffu, 0u, 0x80000000u, 0x7fffffffu,
                       0x80000001u, 1u, 0xfffffffeu, 0x7ffffffeu};
    RadixSorter sorter;
    ExpectIntegerSort(sorter, input, 8, true);
    ExpectIntegerSort(sorter, input, 8, false);
    ExpectIntegerSort(sorter, input, 8, true);
    ExpectIntegerSort(sorter, input, 8, false);
}

TEST(RadixSorterIntegerTest, LargeThenSmallThenLargerLogicalPrefixesStayWithinEachRequestedDomain) {
    for (int signedMode = 0; signedMode < 2; ++signedMode) {
        SCOPED_TRACE(signedMode);
        CKDWORD input[64];
        for (CKDWORD i = 0; i < 64; ++i)
            input[i] = (signedMode ? 0x80000000u : 0u) + 64u - i;
        RadixSorter sorter;
        ExpectIntegerSort(sorter, input, 32, signedMode != 0);
        // The entire backing buffer remains valid. Old sorted indices31..0
        // are safe to read physically but invalid for the new logical prefix.
        ExpectIntegerSort(sorter, input, 4, signedMode != 0);
        ExpectIntegerSort(sorter, input, 12, signedMode != 0);
        ExpectIntegerSort(sorter, input, 32, signedMode != 0);
        ExpectIntegerSort(sorter, input, 48, signedMode != 0);
    }
}

TEST(RadixSorterIntegerTest, ShrinkingToOneElementReturnsOnlyIndexZeroWithoutReadingInvalidMemory) {
    for (int signedMode = 0; signedMode < 2; ++signedMode) {
        SCOPED_TRACE(signedMode);
        CKDWORD input[32];
        for (CKDWORD i = 0; i < 32; ++i) input[i] = 32u - i;
        RadixSorter sorter;
        ExpectIntegerSort(sorter, input, 32, signedMode != 0);
        ExpectIntegerSort(sorter, input, 1, signedMode != 0);
        ExpectIntegerSort(sorter, input, 3, signedMode != 0);
    }
}

TEST(RadixSorterIntegerTest, EachIndividualByteCanDetermineOrderWhileOtherBytesAndDuplicatesAreShared) {
    for (int byte = 0; byte < 4; ++byte) {
        SCOPED_TRACE(byte);
        for (int signedMode = 0; signedMode < 2; ++signedMode) {
            SCOPED_TRACE(signedMode);
            CKDWORD input[24];
            const CKDWORD shift = static_cast<CKDWORD>(8 * byte);
            for (CKDWORD i = 0; i < 24; ++i) {
                const CKDWORD varying = ((i * 37u) % 11u) * 23u;
                input[i] = (0xa5a5a5a5u & ~(0xffu << shift)) | (varying << shift);
            }
            RadixSorter sorter;
            ExpectIntegerSort(sorter, input, 24, signedMode != 0);
        }
    }
}

TEST(RadixSorterIntegerTest, RepeatedAndChangedKeysAtTheSameCountPublishFreshCompleteResults) {
    CKDWORD input[16];
    for (CKDWORD i = 0; i < 16; ++i) input[i] = 16u - i;
    RadixSorter sorter;
    ExpectIntegerSort(sorter, input, 16, false);
    ExpectIntegerSort(sorter, input, 16, false);
    for (CKDWORD i = 0; i < 16; ++i) input[i] = i;
    ExpectIntegerSort(sorter, input, 16, false);
    for (CKDWORD i = 0; i < 16; ++i) input[i] = 7u;
    ExpectIntegerSort(sorter, input, 16, false);
}

TEST(RadixSorterIntegerTest, SameSizeChainedKeysStablyRefineThePriorIndexOrdering) {
    const CKDWORD owners[] = {3, 1, 2, 1, 3, 0, 2, 0, 3, 1, 0, 2, 0, 1, 2, 3};
    const CKDWORD vertex0[] = {5, 1, 5, 1, 2, 2, 1, 2, 5, 2, 5, 1, 2, 5, 2, 1};
    const CKDWORD vertex1[] = {9, 9, 7, 9, 7, 9, 9, 7, 7, 9, 7, 9, 9, 7, 9, 7};
    for (int signedMode = 0; signedMode < 2; ++signedMode) {
        SCOPED_TRACE(signedMode);
        CKDWORD keys[16];
        CKDWORD expected[16];
        for (CKDWORD i = 0; i < 16; ++i) {
            expected[i] = i;
            keys[i] = owners[i] | ((i & 1u) ? 0x80000000u : 0u);
        }
        RadixSorter sorter;
        ExpectStableRefinement(sorter, keys, expected, signedMode != 0);
        for (CKDWORD i = 0; i < 16; ++i) keys[i] = vertex0[i];
        ExpectStableRefinement(sorter, keys, expected, signedMode != 0);
        for (CKDWORD i = 0; i < 16; ++i) keys[i] = vertex1[i];
        ExpectStableRefinement(sorter, keys, expected, signedMode != 0);
        ExpectStableRefinement(sorter, keys, expected, signedMode != 0);
        for (CKDWORD i = 0; i < 16; ++i) keys[i] = 42u;
        ExpectStableRefinement(sorter, keys, expected, signedMode != 0);
    }
}

TEST(RadixSorterIntegerTest, FreshOwnerResetAndEmptySortsKeepANullBorrowedView) {
    RadixSorter sorter;
    EXPECT_EQ(nullptr, sorter.GetIndices());
    EXPECT_EQ(&sorter, &sorter.ResetIndices());
    EXPECT_EQ(nullptr, sorter.GetIndices());
    CKDWORD input[] = {3, 1, 2};
    float floats[] = {3.0f, 1.0f, 2.0f};
    EXPECT_EQ(&sorter, &sorter.Sort(static_cast<CKDWORD *>(nullptr), 0, false));
    EXPECT_EQ(&sorter, &sorter.Sort(static_cast<float *>(nullptr), 0));
    EXPECT_EQ(&sorter, &sorter.Sort(input, 0, true));
    EXPECT_EQ(&sorter, &sorter.Sort(floats, 0));
    EXPECT_EQ(nullptr, sorter.GetIndices());
    EXPECT_EQ(3u, input[0]); EXPECT_EQ(1u, input[1]); EXPECT_EQ(2u, input[2]);
    EXPECT_FLOAT_EQ(3.0f, floats[0]); EXPECT_FLOAT_EQ(1.0f, floats[1]); EXPECT_FLOAT_EQ(2.0f, floats[2]);
}

TEST(RadixSorterIntegerTest, ResetUpdatesTheExistingBorrowedViewToIdentityWithoutChangingInput) {
    CKDWORD input[] = {8, 7, 6, 5, 4, 3, 2, 1};
    RadixSorter sorter;
    ExpectIntegerSort(sorter, input, 8, false);
    CKDWORD *borrowed = sorter.GetIndices();
    ASSERT_NE(nullptr, borrowed);
    ASSERT_EQ(7u, borrowed[0]);
    EXPECT_EQ(&sorter, &sorter.ResetIndices());
    EXPECT_EQ(borrowed, sorter.GetIndices());
    for (CKDWORD i = 0; i < 8; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(i, borrowed[i]);
        EXPECT_EQ(8u - i, input[i]);
        input[i] = (i * 7u) % 8u;
    }
    ExpectIntegerSort(sorter, input, 8, false);
}

TEST(RadixSorterIntegerTest, NestedIndependentOwnerDestructionLeavesTheSurvivorsResultsAndInputIntact) {
    CKDWORD input[16];
    for (CKDWORD i = 0; i < 16; ++i) input[i] = 16u - i;
    RadixSorter survivor;
    ExpectIntegerSort(survivor, input, 16, false);
    CKDWORD *borrowed = survivor.GetIndices();
    ASSERT_NE(nullptr, borrowed);
    CKDWORD expected[16];
    for (CKDWORD i = 0; i < 16; ++i) expected[i] = borrowed[i];
    {
        CKDWORD otherInput[8];
        for (CKDWORD i = 0; i < 8; ++i) otherInput[i] = 0x01000000u + 8u - i;
        RadixSorter other;
        ExpectIntegerSort(other, otherInput, 8, false);
        EXPECT_NE(borrowed, other.GetIndices());
        ASSERT_EQ(7u, other.GetIndices()[0]);
    }
    EXPECT_EQ(borrowed, survivor.GetIndices());
    for (CKDWORD i = 0; i < 16; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(expected[i], borrowed[i]);
        EXPECT_EQ(16u - i, input[i]);
    }
    ExpectIntegerSort(survivor, input, 16, false);
}
}
