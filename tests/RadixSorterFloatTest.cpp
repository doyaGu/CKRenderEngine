#include <gtest/gtest.h>
#include <cfloat>
#include <cstring>

#include "RadixSort.h"

namespace {

::testing::AssertionResult CheckFloatSort(RadixSorter &sorter, float *input, CKDWORD count) {
    if (count == 0 || count > 64)
        return ::testing::AssertionFailure() << "Fixture count outside its finite oracle capacity";
    float expected[64]; CKDWORD originalBits[64]; bool seen[64] = {};
    std::memcpy(expected, input, count * sizeof(float));
    std::memcpy(originalBits, input, count * sizeof(float));
    // Independent numeric insertion sort; no radix bytes, sign masks or production sorting helper.
    for (CKDWORD i = 1; i < count; ++i) {
        const float value = expected[i];
        CKDWORD position = i;
        while (position != 0 && value < expected[position - 1]) {
            expected[position] = expected[position - 1];
            --position;
        }
        expected[position] = value;
    }
    if (&sorter.Sort(input, count) != &sorter)
        return ::testing::AssertionFailure() << "Sort did not return its owner";
    const CKDWORD *indices = sorter.GetIndices();
    if (!indices)
        return ::testing::AssertionFailure() << "Missing output permutation";
    // Validate every index before using any index to access the active input.
    for (CKDWORD i = 0; i < count; ++i) {
        if (indices[i] >= count)
            return ::testing::AssertionFailure() << "Index " << indices[i] << " at position " << i
                                                  << " is outside active count " << count;
        if (seen[indices[i]])
            return ::testing::AssertionFailure() << "Repeated index " << indices[i];
        seen[indices[i]] = true;
    }
    if (std::memcmp(originalBits, input, count * sizeof(float)) != 0)
        return ::testing::AssertionFailure() << "Sort modified original float bits";
    for (CKDWORD i = 0; i < count; ++i) {
        if (input[indices[i]] != expected[i])
            return ::testing::AssertionFailure() << "Numeric rank " << i << " is " << input[indices[i]]
                                                  << ", expected " << expected[i];
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult CheckIntegerWarmup(RadixSorter &sorter, CKDWORD *input,
                                            CKDWORD count, bool signedValues) {
    sorter.Sort(input, count, signedValues);
    const CKDWORD *indices = sorter.GetIndices();
    if (!indices)
        return ::testing::AssertionFailure() << "Integer warmup returned no permutation";
    bool seen[16] = {};
    if (count > 16)
        return ::testing::AssertionFailure() << "Integer warmup fixture too large";
    for (CKDWORD i = 0; i < count; ++i) {
        if (indices[i] >= count || seen[indices[i]])
            return ::testing::AssertionFailure() << "Invalid integer warmup permutation";
        seen[indices[i]] = true;
    }
    for (CKDWORD i = 1; i < count; ++i) {
        const CKDWORD left = input[indices[i - 1]], right = input[indices[i]];
        if (signedValues ? static_cast<int>(left) > static_cast<int>(right) : left > right)
            return ::testing::AssertionFailure() << "Integer warmup is not ordered";
    }
    return ::testing::AssertionSuccess();
}

float FloatBits(CKDWORD bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

TEST(RadixSorterFloatTest, MixedFiniteSignsAndMagnitudesMatchIndependentNumericOracle) {
    float input[] = {3, -.125f, 0, -9, 12345, -32768, 1e-10f, -1e-5f,
                     FLT_MAX, -FLT_MAX, FLT_MIN, -FLT_MIN, .5f, -.5f};
    RadixSorter sorter;
    ASSERT_TRUE(CheckFloatSort(sorter, input, sizeof(input) / sizeof(input[0])));
}

TEST(RadixSorterFloatTest, UniformNegativeMostSignificantByteStillSortsAscendingNumerically) {
    const float cases[3][5] = {{-1.125f, -1.75f, -1.25f, -1.5f, -1},
                              {-2.25f, -3.5f, -2.5f, -3, -2},
                              {-144, -224, -160, -192, -128}};
    for (int sample = 0; sample < 3; ++sample) {
        float input[5]; std::memcpy(input, cases[sample], sizeof(input));
        RadixSorter sorter;
        ASSERT_TRUE(CheckFloatSort(sorter, input, 5)) << "Negative range " << sample;
    }
}

TEST(RadixSorterFloatTest, PositiveAndNegativeZeroAreOneNumericEquivalenceClass) {
    float input[] = {FloatBits(0x80000000u), 3, 0, -2, FloatBits(0x80000000u), 0, 1, -1};
    RadixSorter sorter;
    ASSERT_TRUE(CheckFloatSort(sorter, input, 8));
    CKDWORD first[8]; std::memcpy(first, sorter.GetIndices(), sizeof(first));
    ASSERT_TRUE(CheckFloatSort(sorter, input, 8));
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(first[i], sorter.GetIndices()[i]);
}

TEST(RadixSorterFloatTest, RepeatedNegativeAndPositiveValuesNeedPermutationButNotTieStability) {
    float input[] = {-1.5f, 2.5f, -2.5f, -1.5f, 0, 2.5f, -2.5f, 1, 0, -1.5f};
    RadixSorter sorter;
    ASSERT_TRUE(CheckFloatSort(sorter, input, 10));
    ASSERT_TRUE(CheckFloatSort(sorter, input, 10));
}

TEST(RadixSorterFloatTest, ShrinkAndRegrowUseOnlyTheNewActiveCount) {
    // Full physical backing keeps an old implementation's stale reads in mapped fixture storage.
    // The semantic failure is the out-of-range output permutation, not a fixture-side bad read.
    float backing[16];
    for (int i = 0; i < 16; ++i) backing[i] = static_cast<float>(16 - i);
    RadixSorter sorter;
    ASSERT_TRUE(CheckFloatSort(sorter, backing, 16));
    backing[0] = .5f; backing[1] = -1.5f; backing[2] = 2; backing[3] = -1.25f; backing[4] = 0;
    ASSERT_TRUE(CheckFloatSort(sorter, backing, 5));
    for (int i = 0; i < 12; ++i) backing[i] = static_cast<float>((i * 7) % 13 - 6);
    ASSERT_TRUE(CheckFloatSort(sorter, backing, 12));
    backing[0] = -9;
    ASSERT_TRUE(CheckFloatSort(sorter, backing, 1));
}

TEST(RadixSorterFloatTest, FloatSortReusesUnsignedAndSignedModesAcrossDifferentCounts) {
    CKDWORD unsignedKeys[8] = {80, 40, 70, 30, 60, 20, 50, 10};
    RadixSorter sorter;
    ASSERT_TRUE(CheckIntegerWarmup(sorter, unsignedKeys, 8, false));
    // The old cached prefix is 7,5,3. Its backing values0,1,2 are already ordered,
    // so before-fix execution returns invalid active indices without corrupting a radix bucket.
    float backing[8] = {9, -5, 4, 2, 99, 1, 55, 0};
    ASSERT_TRUE(CheckFloatSort(sorter, backing, 3));
    CKDWORD signedKeys[3] = {static_cast<CKDWORD>(-3), 1, static_cast<CKDWORD>(-1)};
    ASSERT_TRUE(CheckIntegerWarmup(sorter, signedKeys, 3, true));
    ASSERT_TRUE(CheckFloatSort(sorter, backing, 8));
}

TEST(RadixSorterFloatTest, EqualValuesAndSignedZerosKeepAnAlreadyOrderedCachedPermutation) {
    CKDWORD keys[8] = {80, 40, 70, 30, 60, 20, 50, 10};
    RadixSorter sorter;
    ASSERT_TRUE(CheckIntegerWarmup(sorter, keys, 8, false));
    CKDWORD prior[8]; std::memcpy(prior, sorter.GetIndices(), sizeof(prior));
    float equal[8] = {-2, -2, -2, -2, -2, -2, -2, -2};
    ASSERT_TRUE(CheckFloatSort(sorter, equal, 8));
    for (int i = 0; i < 8; ++i) EXPECT_EQ(prior[i], sorter.GetIndices()[i]);
    float zeros[8] = {0, FloatBits(0x80000000u), 0, FloatBits(0x80000000u),
                      FloatBits(0x80000000u), 0, FloatBits(0x80000000u), 0};
    ASSERT_TRUE(CheckFloatSort(sorter, zeros, 8));
    for (int i = 0; i < 8; ++i) EXPECT_EQ(prior[i], sorter.GetIndices()[i]);
}

TEST(RadixSorterFloatTest, SameCountInputChangesInvalidateTheNumericCoherenceShortcut) {
    float input[4] = {-8, -4, -2, -1};
    RadixSorter sorter;
    ASSERT_TRUE(CheckFloatSort(sorter, input, 4));
    input[0] = -1; input[1] = -1.5f; input[2] = -1.25f; input[3] = -1.75f;
    ASSERT_TRUE(CheckFloatSort(sorter, input, 4));
}

TEST(RadixSorterFloatTest, FiniteSubnormalValuesRetainTheirNumericOrderAndInputBits) {
    float input[] = {FloatBits(0x00000001u), FloatBits(0x80000002u), FloatBits(0x007fffffu),
                     FloatBits(0x80000001u), FloatBits(0x00000002u), FloatBits(0x807fffffu)};
    RadixSorter sorter;
    ASSERT_TRUE(CheckFloatSort(sorter, input, 6));
}
