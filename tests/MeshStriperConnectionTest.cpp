#include <gtest/gtest.h>

#include "MeshStriper.h"

namespace {

const CKWORD MixedTriangles[30] = {
    60000, 60001, 60002,
    50000, 50001, 50002, 50002, 50001, 50003,
    40000, 40001, 40002, 40002, 40001, 40003, 40002, 40003, 40004,
    30000, 30001, 30002, 30002, 30001, 30003, 30002, 30003, 30004, 30004, 30003, 30005};

const CKWORD LeadingDuplicateTriangles[27] = {
    60000, 60001, 60002,
    1, 3, 0, 3, 1, 4, 1, 2, 4, 4, 2, 5,
    3, 4, 6, 6, 4, 7, 4, 5, 7, 7, 5, 8};

CKDWORD ReadIndex(const MeshStriper::Result &result, CKDWORD index, bool sixteenBits) {
    return sixteenBits ? static_cast<const CKWORD *>(result.StripIndices)[index]
                       : static_cast<const CKDWORD *>(result.StripIndices)[index];
}

void CanonicalCycle(CKDWORD (&triangle)[3]) {
    // Cyclic rotation keeps winding. Sorting all three indices would hide a reversed face.
    if (triangle[1] < triangle[0] && triangle[1] < triangle[2]) {
        const CKDWORD first = triangle[0];
        triangle[0] = triangle[1]; triangle[1] = triangle[2]; triangle[2] = first;
    } else if (triangle[2] < triangle[0] && triangle[2] < triangle[1]) {
        const CKDWORD last = triangle[2];
        triangle[2] = triangle[1]; triangle[1] = triangle[0]; triangle[0] = last;
    }
}

void ExpectOrientedTriangles(const MeshStriper::Result &result, const CKWORD *input,
                             int faceCount, bool sixteenBits) {
    ASSERT_GT(result.NbStrips, 0u);
    ASSERT_NE(nullptr, result.StripLengths);
    ASSERT_NE(nullptr, result.StripIndices);
    ASSERT_LE(faceCount, 32);
    bool consumed[32] = {};
    int triangles = 0;
    CKDWORD offset = 0;
    for (CKDWORD strip = 0; strip < result.NbStrips; ++strip) {
        const CKDWORD length = result.StripLengths[strip];
        ASSERT_GE(length, 3u);
        for (CKDWORD i = 2; i < length; ++i) {
            CKDWORD actual[3] = {ReadIndex(result, offset + i - 2, sixteenBits),
                                 ReadIndex(result, offset + i - 1, sixteenBits),
                                 ReadIndex(result, offset + i, sixteenBits)};
            if (actual[0] == actual[1] || actual[0] == actual[2] || actual[1] == actual[2])
                continue;
            if ((i & 1u) != 0) {
                const CKDWORD first = actual[0]; actual[0] = actual[1]; actual[1] = first;
            }
            CanonicalCycle(actual);
            int matched = -1;
            for (int face = 0; face < faceCount; ++face) {
                CKDWORD expected[3] = {input[face * 3], input[face * 3 + 1], input[face * 3 + 2]};
                CanonicalCycle(expected);
                if (!consumed[face] && actual[0] == expected[0] && actual[1] == expected[1] &&
                    actual[2] == expected[2]) {
                    matched = face;
                    break;
                }
            }
            ASSERT_GE(matched, 0) << "Unexpected, duplicated, or reversed triangle at strip "
                                  << strip << ", index " << i << ": "
                                  << actual[0] << "," << actual[1] << "," << actual[2];
            consumed[matched] = true;
            ++triangles;
        }
        offset += length;
    }
    EXPECT_EQ(faceCount, triangles);
    for (int face = 0; face < faceCount; ++face)
        EXPECT_TRUE(consumed[face]) << "Missing input face " << face;
}

template <int Count>
void CopyInput(CKWORD (&output)[Count], const CKWORD (&input)[Count]) {
    for (int i = 0; i < Count; ++i)
        output[i] = input[i];
}

void RunMixedJoins(bool sixteenBits, bool sortSeeds) {
    CKWORD input[30]; CopyInput(input, MixedTriangles);
    const CKDWORD flags = CKMESHSTRIPER_PARITYFIX | (sixteenBits ? CKMESHSTRIPER_INDEX16 : 0) |
                          (sortSeeds ? CKMESHSTRIPER_SORTSEEDS : 0);
    MeshStriper separate;
    ASSERT_TRUE(separate.Init(input, 10, flags));
    MeshStriper::Result raw = {};
    ASSERT_TRUE(separate.Compute(&raw));
    ASSERT_EQ(4u, raw.NbStrips);
    ExpectOrientedTriangles(raw, MixedTriangles, 10, sixteenBits);
    if (!sortSeeds) {
        ASSERT_NE(nullptr, raw.StripLengths);
        EXPECT_EQ(3u, raw.StripLengths[0]); EXPECT_EQ(4u, raw.StripLengths[1]);
        EXPECT_EQ(5u, raw.StripLengths[2]); EXPECT_EQ(6u, raw.StripLengths[3]);
    }
    MeshStriper connected;
    ASSERT_TRUE(connected.Init(input, 10, flags | CKMESHSTRIPER_CONNECTALL));
    MeshStriper::Result result = {};
    ASSERT_TRUE(connected.Compute(&result));
    ASSERT_EQ(1u, result.NbStrips);
    ASSERT_NE(nullptr, result.StripLengths);
    if (!sortSeeds)
        EXPECT_EQ(26u, result.StripLengths[0]);
    ExpectOrientedTriangles(result, MixedTriangles, 10, sixteenBits);
    ASSERT_TRUE(connected.Compute(&result));
    ASSERT_EQ(1u, result.NbStrips);
    ExpectOrientedTriangles(result, MixedTriangles, 10, sixteenBits);
}

void RunLeadingDuplicateJoin(bool sixteenBits) {
    CKWORD input[27]; CopyInput(input, LeadingDuplicateTriangles);
    const CKDWORD flags = CKMESHSTRIPER_PARITYFIX | (sixteenBits ? CKMESHSTRIPER_INDEX16 : 0);
    MeshStriper separate;
    ASSERT_TRUE(separate.Init(input, 9, flags));
    MeshStriper::Result raw = {};
    ASSERT_TRUE(separate.Compute(&raw));
    ASSERT_GE(raw.NbStrips, 3u);
    ASSERT_NE(nullptr, raw.StripLengths);
    ASSERT_NE(nullptr, raw.StripIndices);
    ASSERT_EQ(3u, raw.StripLengths[0]);
    ASSERT_EQ(7u, raw.StripLengths[1]);
    ASSERT_EQ(ReadIndex(raw, 3, sixteenBits), ReadIndex(raw, 4, sixteenBits));
    ExpectOrientedTriangles(raw, LeadingDuplicateTriangles, 9, sixteenBits);

    MeshStriper connected;
    ASSERT_TRUE(connected.Init(input, 9, flags | CKMESHSTRIPER_CONNECTALL));
    MeshStriper::Result result = {};
    ASSERT_TRUE(connected.Compute(&result));
    ASSERT_EQ(1u, result.NbStrips);
    ASSERT_NE(nullptr, result.StripLengths);
    ASSERT_NE(nullptr, result.StripIndices);
    ASSERT_GE(result.StripLengths[0], 7u);
    EXPECT_EQ(ReadIndex(raw, 2, sixteenBits), ReadIndex(result, 3, sixteenBits));
    EXPECT_EQ(ReadIndex(raw, 3, sixteenBits), ReadIndex(result, 4, sixteenBits));
    EXPECT_EQ(ReadIndex(raw, 4, sixteenBits), ReadIndex(result, 5, sixteenBits));
    EXPECT_EQ(ReadIndex(raw, 5, sixteenBits), ReadIndex(result, 6, sixteenBits));
    ExpectOrientedTriangles(result, LeadingDuplicateTriangles, 9, sixteenBits);
}

} // namespace

TEST(MeshStriperConnectionTest, OddAndEvenStripJoinsPreserveWindingIn16BitOutput) {
    RunMixedJoins(true, false);
}

TEST(MeshStriperConnectionTest, OddAndEvenStripJoinsPreserveWindingIn32BitOutput) {
    RunMixedJoins(false, false);
}

TEST(MeshStriperConnectionTest, JoinConsumesExistingLeadingParityDuplicateIn16BitOutput) {
    RunLeadingDuplicateJoin(true);
}

TEST(MeshStriperConnectionTest, JoinConsumesExistingLeadingParityDuplicateIn32BitOutput) {
    RunLeadingDuplicateJoin(false);
}

TEST(MeshStriperConnectionTest, DegreeSortedSeedSelectionPreservesConnectedOrientedFacesInBothWidths) {
    RunMixedJoins(true, true);
    RunMixedJoins(false, true);
}

TEST(MeshStriperConnectionTest, EvenLengthStripsJoinWithoutParityFlagInBothWidths) {
    CKWORD input[12] = {10, 11, 12, 12, 11, 13, 20, 21, 22, 22, 21, 23};
    for (int width = 0; width < 2; ++width) {
        const bool sixteenBits = width != 0;
        MeshStriper connected;
        ASSERT_TRUE(connected.Init(input, 4, CKMESHSTRIPER_CONNECTALL |
                                           (sixteenBits ? CKMESHSTRIPER_INDEX16 : 0)));
        MeshStriper::Result result = {};
        ASSERT_TRUE(connected.Compute(&result));
        ASSERT_EQ(1u, result.NbStrips);
        ASSERT_NE(nullptr, result.StripLengths);
        ASSERT_NE(nullptr, result.StripIndices);
        ASSERT_EQ(10u, result.StripLengths[0]);
        EXPECT_EQ(ReadIndex(result, 3, sixteenBits), ReadIndex(result, 4, sixteenBits));
        EXPECT_EQ(ReadIndex(result, 5, sixteenBits), ReadIndex(result, 6, sixteenBits));
        ExpectOrientedTriangles(result, input, 4, sixteenBits);
    }
}

TEST(MeshStriperConnectionTest, ResultViewsRemainBorrowedUntilOwnerRecomputesOrReinitializes) {
    CKWORD input[30]; CopyInput(input, MixedTriangles);
    MeshStriper firstOwner;
    ASSERT_TRUE(firstOwner.Init(input, 10, CKMESHSTRIPER_CONNECTALL | CKMESHSTRIPER_PARITYFIX |
                                         CKMESHSTRIPER_INDEX16));
    MeshStriper::Result borrowed = {};
    {
        MeshStriper::Result temporary = {};
        ASSERT_TRUE(firstOwner.Compute(&temporary));
        borrowed = temporary;
        EXPECT_EQ(temporary.StripLengths, borrowed.StripLengths);
        EXPECT_EQ(temporary.StripIndices, borrowed.StripIndices);
        EXPECT_EQ(1u, borrowed.NbStrips);
    }
    // Destroying the non-owning Result value did not destroy either owner allocation.
    ExpectOrientedTriangles(borrowed, MixedTriangles, 10, true);
    MeshStriper otherOwner;
    ASSERT_TRUE(otherOwner.Init(input, 10, CKMESHSTRIPER_CONNECTALL | CKMESHSTRIPER_PARITYFIX));
    MeshStriper::Result independent = {};
    ASSERT_TRUE(otherOwner.Compute(&independent));
    ExpectOrientedTriangles(independent, MixedTriangles, 10, false);
    ExpectOrientedTriangles(borrowed, MixedTriangles, 10, true);

    ASSERT_TRUE(firstOwner.Init(input, 10, CKMESHSTRIPER_CONNECTALL | CKMESHSTRIPER_PARITYFIX));
    // Init/Compute can invalidate previous views. Only consume newly returned pointers.
    ASSERT_TRUE(firstOwner.Compute(&borrowed));
    ExpectOrientedTriangles(borrowed, MixedTriangles, 10, false);
    ASSERT_TRUE(firstOwner.Compute(&borrowed));
    ExpectOrientedTriangles(borrowed, MixedTriangles, 10, false);
}

TEST(MeshStriperConnectionTest, ConnectedSameDirectedSharedEdgePreservesBothInputWindings) {
    CKWORD input[6] = {0, 1, 2, 1, 2, 3};
    for (int width = 0; width < 2; ++width) {
        const bool sixteenBits = width != 0;
        MeshStriper connected;
        ASSERT_TRUE(connected.Init(input, 2, CKMESHSTRIPER_CONNECTALL | CKMESHSTRIPER_PARITYFIX |
                                           (sixteenBits ? CKMESHSTRIPER_INDEX16 : 0)));
        MeshStriper::Result result = {};
        ASSERT_TRUE(connected.Compute(&result));
        ASSERT_EQ(1u, result.NbStrips);
        ExpectOrientedTriangles(result, input, 2, sixteenBits);
    }
}
