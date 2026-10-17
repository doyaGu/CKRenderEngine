#include <gtest/gtest.h>
#include "MeshStriper.h"

namespace {
bool SameCyclicOrientation(CKDWORD a, CKDWORD b, CKDWORD c, const CKWORD *triangle) {
    return (a == triangle[0] && b == triangle[1] && c == triangle[2]) ||
           (a == triangle[1] && b == triangle[2] && c == triangle[0]) ||
           (a == triangle[2] && b == triangle[0] && c == triangle[1]);
}

CKDWORD ReadIndex(const MeshStriper::Result &result, CKDWORD index, bool narrow) {
    return narrow ? static_cast<const CKWORD *>(result.StripIndices)[index]
                  : static_cast<const CKDWORD *>(result.StripIndices)[index];
}

// Independent primitive-consumer oracle: decode each segment using GPU strip
// parity, keep parity advancing across degenerate triangles, and match a
// multiplicity-aware input triangle list. Only cyclic rotations preserve a
// face's orientation; reversing its winding must fail a PARITYFIX result.
void ExpectCoverage(CKWORD *input, int faceCount, CKDWORD flags, CKDWORD expectedStrips = 0) {
    ASSERT_GT(faceCount, 0); ASSERT_LE(faceCount, 64);
    MeshStriper stripifier;
    ASSERT_TRUE(stripifier.Init(input, faceCount, flags));
    MeshStriper::Result result = {};
    ASSERT_TRUE(stripifier.Compute(&result));
    ASSERT_GT(result.NbStrips, 0u); ASSERT_LE(result.NbStrips, static_cast<CKDWORD>(faceCount));
    ASSERT_NE(nullptr, result.StripLengths); ASSERT_NE(nullptr, result.StripIndices);
    if (expectedStrips) EXPECT_EQ(expectedStrips, result.NbStrips);
    const bool narrow = (flags & CKMESHSTRIPER_INDEX16) != 0;
    const bool oriented = (flags & CKMESHSTRIPER_PARITYFIX) != 0;
    bool consumed[64] = {};
    int emitted = 0;
    CKDWORD base = 0;
    for (CKDWORD strip = 0; strip < result.NbStrips; ++strip) {
        SCOPED_TRACE(strip);
        const CKDWORD length = result.StripLengths[strip];
        ASSERT_GE(length, 3u);
        ASSERT_LE(length, static_cast<CKDWORD>(4 * faceCount + 8));
        for (CKDWORD index = 2; index < length; ++index) {
            SCOPED_TRACE(index);
            CKDWORD a = ReadIndex(result, base + index - 2, narrow);
            CKDWORD b = ReadIndex(result, base + index - 1, narrow);
            const CKDWORD c = ReadIndex(result, base + index, narrow);
            if (index & 1) { const CKDWORD swap = a; a = b; b = swap; }
            if (a == b || b == c || a == c) continue;
            ++emitted;
            int match = -1;
            for (int face = 0; face < faceCount; ++face) {
                if (consumed[face]) continue;
                if (SameCyclicOrientation(a,b,c,input + 3 * face) ||
                    (!oriented && SameCyclicOrientation(a,c,b,input + 3 * face))) {
                    match = face; break;
                }
            }
            EXPECT_GE(match, 0) << "Unexpected or reversed triangle " << a << ',' << b << ',' << c;
            if (match >= 0) consumed[match] = true;
        }
        base += length;
    }
    EXPECT_EQ(faceCount, emitted);
    for (int face = 0; face < faceCount; ++face) {
        SCOPED_TRACE(face);
        EXPECT_TRUE(consumed[face]) << "Missing input face " << input[3*face] << ',' << input[3*face+1] << ',' << input[3*face+2];
    }
}

void RotateTriangle(CKWORD *destination, const CKWORD *source, int rotation, bool reverse = false) {
    CKWORD values[3] = {source[0], source[1], source[2]};
    if (reverse) { const CKWORD swap = values[1]; values[1] = values[2]; values[2] = swap; }
    for (int vertex = 0; vertex < 3; ++vertex)
        destination[vertex] = values[(vertex + rotation) % 3];
}

void MakeChain(CKWORD *out, int faceCount, int rotation, int firstFace, CKWORD labelBase = 0,
               CKDWORD reversedFaces = 0) {
    int write = 0;
    for (int order = 0; order < faceCount; ++order) {
        const int face = order == 0 ? firstFace : (order <= firstFace ? order - 1 : order);
        CKWORD triangle[3] = {static_cast<CKWORD>(labelBase + face),
                              static_cast<CKWORD>(labelBase + face + 1),
                              static_cast<CKWORD>(labelBase + face + 2)};
        if (face & 1) { const CKWORD swap = triangle[0]; triangle[0] = triangle[1]; triangle[1] = swap; }
        RotateTriangle(out + 3 * write++, triangle, (rotation + face) % 3,
                       (reversedFaces & (1u << face)) != 0);
    }
}

TEST(MeshStriperTraversalTest, SingleFacesKeepTheirOrientationForAllCyclicSeedRotationsAndBothWidths) {
    const CKWORD source[] = {50000,50001,50002};
    for (int rotation = 0; rotation < 3; ++rotation) {
        SCOPED_TRACE(rotation);
        CKWORD input[3]; RotateTriangle(input,source,rotation);
        for (int narrow = 0; narrow < 2; ++narrow) {
            SCOPED_TRACE(narrow);
            ExpectCoverage(input,1,CKMESHSTRIPER_PARITYFIX | (narrow ? CKMESHSTRIPER_INDEX16 : 0),1);
        }
    }
}

TEST(MeshStriperTraversalTest, AdjacentTwoFaceStripsKeepWindingForEverySeedSlotAndStartingFace) {
    for (int seed = 0; seed < 2; ++seed) {
        SCOPED_TRACE(seed);
        for (int rotation = 0; rotation < 3; ++rotation) {
            SCOPED_TRACE(rotation);
            CKWORD input[6]; MakeChain(input,2,rotation,seed);
            ExpectCoverage(input,2,CKMESHSTRIPER_PARITYFIX,1);
            ExpectCoverage(input,2,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16,1);
        }
    }
}

TEST(MeshStriperTraversalTest, LongEvenAndOddChainsPreserveTheOrientedTriangleStreamInBothWidths) {
    for (int faces = 3; faces <= 9; ++faces) {
        SCOPED_TRACE(faces);
        for (int rotation = 0; rotation < 3; ++rotation) {
            SCOPED_TRACE(rotation);
            CKWORD input[27]; MakeChain(input,faces,rotation,0,40000);
            ExpectCoverage(input,faces,CKMESHSTRIPER_PARITYFIX,1);
            ExpectCoverage(input,faces,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16,1);
        }
    }
}

TEST(MeshStriperTraversalTest, BidirectionalGrowthFromAMiddleSeedKeepsEveryFaceAndItsWinding) {
    for (int faces = 4; faces <= 9; ++faces) {
        SCOPED_TRACE(faces);
        for (int rotation = 0; rotation < 3; ++rotation) {
            SCOPED_TRACE(rotation);
            CKWORD input[27]; MakeChain(input,faces,rotation,faces/2);
            ExpectCoverage(input,faces,CKMESHSTRIPER_PARITYFIX,1);
            ExpectCoverage(input,faces,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16,1);
        }
    }
}

TEST(MeshStriperTraversalTest, ClosedTetrahedronCyclesTerminateWithoutRepeatingFacesOrLosingCounterpartTags) {
    const CKWORD source[] = {0,1,2, 0,3,1, 1,3,2, 2,3,0};
    for (int seed = 0; seed < 4; ++seed) {
        SCOPED_TRACE(seed);
        for (int rotation = 0; rotation < 3; ++rotation) {
            SCOPED_TRACE(rotation);
            CKWORD input[12];
            for (int face = 0; face < 4; ++face)
                RotateTriangle(input+3*face,source+3*((face+seed)%4),(face+rotation)%3);
            ExpectCoverage(input,4,CKMESHSTRIPER_PARITYFIX,1);
            ExpectCoverage(input,4,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_SORTSEEDS,1);
        }
    }
}

TEST(MeshStriperTraversalTest, AcceptedSameDirectedSharedEdgesPreserveBothInputFaceOrientationsWithParityFix) {
    const CKWORD source[] = {0,1,2, 1,2,3};
    for (int rotation = 0; rotation < 3; ++rotation) {
        SCOPED_TRACE(rotation);
        CKWORD input[6]; RotateTriangle(input,source,rotation); RotateTriangle(input+3,source+3,(rotation+1)%3);
        ExpectCoverage(input,2,CKMESHSTRIPER_PARITYFIX);
        ExpectCoverage(input,2,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16);
    }
}

TEST(MeshStriperTraversalTest, SeveralWindingDiscontinuitiesInOneAcceptedChainNeverReverseAnInputFace) {
    for (int rotation = 0; rotation < 3; ++rotation) {
        SCOPED_TRACE(rotation);
        CKWORD input[24]; MakeChain(input,8,rotation,3,200,0x5au);
        ExpectCoverage(input,8,CKMESHSTRIPER_PARITYFIX);
        ExpectCoverage(input,8,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16);
        ExpectCoverage(input,8,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_SORTSEEDS);
    }
}

TEST(MeshStriperTraversalTest, CyclicTopologyWithAReversedFaceKeepsMultiplicityAndStopsAtWindingCuts) {
    const CKWORD source[] = {0,1,2, 0,3,1, 1,3,2, 2,3,0};
    for (int rotation = 0; rotation < 3; ++rotation) {
        SCOPED_TRACE(rotation);
        CKWORD input[12];
        for (int face = 0; face < 4; ++face)
            RotateTriangle(input+3*face,source+3*face,(rotation+face)%3,face==1);
        ExpectCoverage(input,4,CKMESHSTRIPER_PARITYFIX);
        ExpectCoverage(input,4,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_SORTSEEDS);
    }
}

TEST(MeshStriperTraversalTest, FlagOffRetainsTheNativeUnorientedCoverageContractForAcceptedMixedWinding) {
    const CKWORD source[] = {0,1,2, 1,2,3};
    CKWORD pair[6]; RotateTriangle(pair,source,0); RotateTriangle(pair+3,source+3,1);
    ExpectCoverage(pair,2,0,1); ExpectCoverage(pair,2,CKMESHSTRIPER_INDEX16,1);
    for (int rotation = 0; rotation < 3; ++rotation) {
        SCOPED_TRACE(rotation);
        CKWORD input[24]; MakeChain(input,8,rotation,3,200,0x5au);
        ExpectCoverage(input,8,0);
        ExpectCoverage(input,8,CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_SORTSEEDS);
    }
}

TEST(MeshStriperTraversalTest, AcceptedDuplicateFacesKeepTheirMultiplicityAndOrientation) {
    const CKWORD source[] = {4,5,6};
    for (int rotation = 0; rotation < 3; ++rotation) {
        SCOPED_TRACE(rotation);
        CKWORD input[6]; RotateTriangle(input,source,rotation); RotateTriangle(input+3,source,(rotation+1)%3);
        ExpectCoverage(input,2,CKMESHSTRIPER_PARITYFIX);
        ExpectCoverage(input,2,CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_INDEX16);
    }
}
}
