#include <gtest/gtest.h>

#include "MeshStriper.h"

namespace {

CKDWORD IndexAt(const MeshStriper::Result &result, CKDWORD index, bool narrow) {
    return narrow ? static_cast<const CKWORD *>(result.StripIndices)[index]
                  : static_cast<const CKDWORD *>(result.StripIndices)[index];
}

void ExpectSingleTriangle(const MeshStriper::Result &result, bool narrow,
                          CKDWORD a, CKDWORD b, CKDWORD c) {
    ASSERT_EQ(1u, result.NbStrips);
    ASSERT_NE(nullptr, result.StripLengths);
    ASSERT_NE(nullptr, result.StripIndices);
    ASSERT_GE(result.StripLengths[0], 3u);
    ASSERT_LE(result.StripLengths[0], 8u);
    int triangles = 0;
    for (CKDWORD i = 2; i < result.StripLengths[0]; ++i) {
        CKDWORD x = IndexAt(result, i - 2, narrow);
        CKDWORD y = IndexAt(result, i - 1, narrow);
        const CKDWORD z = IndexAt(result, i, narrow);
        if (x == y || y == z || z == x)
            continue;
        if ((i & 1u) != 0) {
            const CKDWORD saved = x;
            x = y;
            y = saved;
        }
        EXPECT_TRUE((x == a && y == b && z == c) ||
                    (x == b && y == c && z == a) ||
                    (x == c && y == a && z == b));
        ++triangles;
    }
    EXPECT_EQ(1, triangles);
}

void ExpectUntouched(const MeshStriper::Result &result, const CKDWORD *length,
                     const CKWORD *indices) {
    EXPECT_EQ(91u, result.NbStrips);
    EXPECT_EQ(length, result.StripLengths);
    EXPECT_EQ(indices, result.StripIndices);
}

TEST(MeshStriperLifecycleTest, FreshObjectRejectsComputeWithoutPublishingAnOutput) {
    MeshStriper striper;
    CKDWORD length = 73;
    CKWORD indices[3] = {7, 11, 13};
    MeshStriper::Result result = {91, &length, indices};
    EXPECT_FALSE(striper.Compute(&result));
    ExpectUntouched(result, &length, indices);
    EXPECT_FALSE(striper.Compute(nullptr));
    EXPECT_EQ(73u, length);
    EXPECT_EQ(CKWORD(7), indices[0]);
}

TEST(MeshStriperLifecycleTest, InitCopiesCallerInputAndRepeatedComputationsReturnTheSameGeometry) {
    for (int narrow = 0; narrow < 2; ++narrow) {
        MeshStriper striper;
        CKWORD indices[3] = {65535, 40001, 2};
        const CKDWORD flags = CKMESHSTRIPER_PARITYFIX |
                              (narrow ? CKMESHSTRIPER_INDEX16 : 0);
        ASSERT_TRUE(striper.Init(indices, 1, flags));
        indices[0] = indices[1] = indices[2] = 99;
        for (int repeat = 0; repeat < 3; ++repeat) {
            MeshStriper::Result result = {};
            ASSERT_TRUE(striper.Compute(&result));
            ExpectSingleTriangle(result, narrow != 0, 65535, 40001, 2);
        }
    }
}

TEST(MeshStriperLifecycleTest, NullOutputFailurePreservesTheLastSuccessfulBorrowedView) {
    MeshStriper striper;
    CKWORD input[3] = {31, 37, 41};
    ASSERT_TRUE(striper.Init(input, 1, CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_PARITYFIX));
    MeshStriper::Result result = {};
    ASSERT_TRUE(striper.Compute(&result));
    const CKDWORD length = result.StripLengths[0];
    EXPECT_FALSE(striper.Compute(nullptr));
    EXPECT_EQ(length, result.StripLengths[0]);
    ExpectSingleTriangle(result, true, 31, 37, 41);
    MeshStriper::Result repeated = {};
    ASSERT_TRUE(striper.Compute(&repeated));
    ExpectSingleTriangle(repeated, true, 31, 37, 41);
}

TEST(MeshStriperLifecycleTest, ReinitReplacesOldGeometryAndSelectsTheRequestedOutputWidth) {
    MeshStriper striper;
    CKWORD first[3] = {1, 2, 3};
    ASSERT_TRUE(striper.Init(first, 1, CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_PARITYFIX));
    MeshStriper::Result narrow = {};
    ASSERT_TRUE(striper.Compute(&narrow));
    ExpectSingleTriangle(narrow, true, 1, 2, 3);

    CKWORD second[3] = {60001, 60002, 60003};
    ASSERT_TRUE(striper.Init(second, 1, CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_CONNECTALL));
    MeshStriper::Result wide = {};
    ASSERT_TRUE(striper.Compute(&wide));
    ExpectSingleTriangle(wide, false, 60001, 60002, 60003);

    CKWORD third[3] = {101, 103, 107};
    ASSERT_TRUE(striper.Init(third, 1, CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_PARITYFIX |
                                          CKMESHSTRIPER_CONNECTALL | CKMESHSTRIPER_SORTSEEDS));
    MeshStriper::Result connectedNarrow = {};
    ASSERT_TRUE(striper.Compute(&connectedNarrow));
    ExpectSingleTriangle(connectedNarrow, true, 101, 103, 107);
}

TEST(MeshStriperLifecycleTest, NonmanifoldInitInvalidatesComputeAndAFreshInitRecovers) {
    MeshStriper striper;
    CKWORD valid[3] = {2, 3, 5};
    ASSERT_TRUE(striper.Init(valid, 1, CKMESHSTRIPER_PARITYFIX));
    MeshStriper::Result success = {};
    ASSERT_TRUE(striper.Compute(&success));
    CKWORD nonmanifold[9] = {0, 1, 2, 1, 0, 3, 0, 1, 4};
    EXPECT_FALSE(striper.Init(nonmanifold, 3, CKMESHSTRIPER_INDEX16));
    CKDWORD length = 73;
    CKWORD indices[3] = {7, 11, 13};
    MeshStriper::Result failed = {91, &length, indices};
    EXPECT_FALSE(striper.Compute(&failed));
    ExpectUntouched(failed, &length, indices);
    CKWORD replacement[3] = {17, 19, 23};
    ASSERT_TRUE(striper.Init(replacement, 1, CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16));
    MeshStriper::Result recovered = {};
    ASSERT_TRUE(striper.Compute(&recovered));
    ExpectSingleTriangle(recovered, true, 17, 19, 23);
}

TEST(MeshStriperLifecycleTest, InvalidInitDoesNotExposeThePreviousComputedMesh) {
    for (int mode = 0; mode < 3; ++mode) {
        MeshStriper striper;
        CKWORD input[3] = {43, 47, 53};
        ASSERT_TRUE(striper.Init(input, 1, CKMESHSTRIPER_PARITYFIX));
        MeshStriper::Result first = {};
        ASSERT_TRUE(striper.Compute(&first));
        EXPECT_FALSE(striper.Init(mode == 0 ? nullptr : input,
                                  mode == 0 ? 1 : (mode == 1 ? 0 : -1), 0));
        CKDWORD length = 73;
        CKWORD indices[3] = {7, 11, 13};
        MeshStriper::Result failed = {91, &length, indices};
        EXPECT_FALSE(striper.Compute(&failed));
        ExpectUntouched(failed, &length, indices);
        ASSERT_TRUE(striper.Init(input, 1, CKMESHSTRIPER_PARITYFIX));
        MeshStriper::Result recovered = {};
        ASSERT_TRUE(striper.Compute(&recovered));
        ExpectSingleTriangle(recovered, false, 43, 47, 53);
    }
}

TEST(MeshStriperLifecycleTest, SeparateInstancesKeepIndependentBorrowedResultStorage) {
    MeshStriper first;
    MeshStriper second;
    CKWORD one[3] = {61, 67, 71};
    CKWORD two[3] = {73, 79, 83};
    ASSERT_TRUE(first.Init(one, 1, CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_PARITYFIX));
    MeshStriper::Result firstResult = {};
    ASSERT_TRUE(first.Compute(&firstResult));
    ASSERT_TRUE(second.Init(two, 1, CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_CONNECTALL));
    MeshStriper::Result secondResult = {};
    ASSERT_TRUE(second.Compute(&secondResult));
    ExpectSingleTriangle(firstResult, true, 61, 67, 71);
    ExpectSingleTriangle(secondResult, false, 73, 79, 83);
    two[0] = 89;
    two[1] = 97;
    two[2] = 101;
    ASSERT_TRUE(second.Init(two, 1, CKMESHSTRIPER_PARITYFIX));
    ASSERT_TRUE(second.Compute(&secondResult));
    EXPECT_NE(firstResult.StripLengths, secondResult.StripLengths);
    EXPECT_NE(firstResult.StripIndices, secondResult.StripIndices);
    ExpectSingleTriangle(firstResult, true, 61, 67, 71);
}

TEST(MeshStriperLifecycleTest, DestructionLeavesTheCopiedInputOwnedByItsCaller) {
    CKWORD *input = new CKWORD[3]{97, 101, 103};
    {
        MeshStriper striper;
        ASSERT_TRUE(striper.Init(input, 1, CKMESHSTRIPER_PARITYFIX));
        MeshStriper::Result result = {};
        ASSERT_TRUE(striper.Compute(&result));
        ExpectSingleTriangle(result, false, 97, 101, 103);
    }
    EXPECT_EQ(CKWORD(97), input[0]);
    EXPECT_EQ(CKWORD(101), input[1]);
    EXPECT_EQ(CKWORD(103), input[2]);
    delete[] input;
}

} // namespace
