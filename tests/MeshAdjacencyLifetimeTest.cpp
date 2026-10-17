#include <gtest/gtest.h>

#include "MeshAdjacency.h"
#include "MeshStriper.h"

namespace {

const CKDWORD BoundaryFace = 0xffffffffu;

void ExpectFace(const MeshAdjacency::Face &face, CKDWORD a, CKDWORD b, CKDWORD c) {
    EXPECT_EQ(a, face.vertices[0]);
    EXPECT_EQ(b, face.vertices[1]);
    EXPECT_EQ(c, face.vertices[2]);
}

void ExpectBoundaryLinks(const MeshAdjacency::Face &face) {
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(BoundaryFace, face.faces[i]);
}

void ExpectRawEdge(const MeshAdjacency::Edge &edge, CKDWORD a, CKDWORD b, CKDWORD owner) {
    EXPECT_EQ(a, edge.vertices[0]);
    EXPECT_EQ(b, edge.vertices[1]);
    EXPECT_EQ(owner, edge.faces[0]);
    EXPECT_EQ(BoundaryFace, edge.faces[1]);
}

void SortTriple(CKDWORD (&vertices)[3]) {
    for (int i = 0; i < 2; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            if (vertices[j] < vertices[i]) {
                const CKDWORD saved = vertices[i];
                vertices[i] = vertices[j];
                vertices[j] = saved;
            }
        }
    }
}

CKDWORD StripIndex(const MeshStriper::Result &result, CKDWORD index, bool sixteenBits) {
    return sixteenBits ? static_cast<const CKWORD *>(result.StripIndices)[index]
                       : static_cast<const CKDWORD *>(result.StripIndices)[index];
}

void ExpectStripTriangles(const MeshStriper::Result &result, const CKWORD *expected,
                         int triangleCount, bool sixteenBits) {
    ASSERT_GT(result.NbStrips, 0u);
    ASSERT_NE(nullptr, result.StripLengths);
    ASSERT_NE(nullptr, result.StripIndices);
    ASSERT_LE(triangleCount, 4);
    bool consumed[4] = {};
    int triangles = 0;
    CKDWORD offset = 0;
    for (CKDWORD strip = 0; strip < result.NbStrips; ++strip) {
        const CKDWORD length = result.StripLengths[strip];
        ASSERT_GE(length, 3u);
        for (CKDWORD i = 2; i < length; ++i) {
            CKDWORD actual[3] = {StripIndex(result, offset + i - 2, sixteenBits),
                                 StripIndex(result, offset + i - 1, sixteenBits),
                                 StripIndex(result, offset + i, sixteenBits)};
            if (actual[0] == actual[1] || actual[0] == actual[2] || actual[1] == actual[2])
                continue;
            SortTriple(actual);
            int match = -1;
            for (int face = 0; face < triangleCount; ++face) {
                CKDWORD wanted[3] = {expected[face * 3], expected[face * 3 + 1], expected[face * 3 + 2]};
                SortTriple(wanted);
                if (!consumed[face] && actual[0] == wanted[0] && actual[1] == wanted[1] &&
                    actual[2] == wanted[2]) {
                    match = face;
                    break;
                }
            }
            ASSERT_GE(match, 0) << "Unexpected or duplicated triangle in real strip output";
            consumed[match] = true;
            ++triangles;
        }
        offset += length;
    }
    EXPECT_EQ(triangleCount, triangles);
    for (int i = 0; i < triangleCount; ++i)
        EXPECT_TRUE(consumed[i]);
}

} // namespace

TEST(MeshAdjacencyLifetimeTest, FaceQueriesUseUnorientedSlotsAndLeaveTopologyUntouched) {
    MeshAdjacency::Face face = {{17, 29, 41}, {0x80000003u, 0x40000005u, BoundaryFace}};
    const MeshAdjacency::Face &readOnly = face;
    const CKDWORD pairs[6][2] = {{17, 29}, {29, 17}, {17, 41}, {41, 17}, {29, 41}, {41, 29}};
    const CKBYTE slots[6] = {0, 0, 1, 1, 2, 2};
    const CKDWORD opposite[6] = {41, 41, 29, 29, 17, 17};
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(slots[i], readOnly.FindEdge(pairs[i][0], pairs[i][1]));
        EXPECT_EQ(opposite[i], readOnly.OppositeVertex(pairs[i][0], pairs[i][1]));
    }
    EXPECT_EQ(CKBYTE(0xff), readOnly.FindEdge(17, 99));
    EXPECT_EQ(CKBYTE(0xff), readOnly.FindEdge(99, 17));
    EXPECT_EQ(CKBYTE(0xff), readOnly.FindEdge(17, 17));
    EXPECT_EQ(BoundaryFace, readOnly.OppositeVertex(17, 99));
    EXPECT_EQ(BoundaryFace, readOnly.OppositeVertex(99, 17));
    EXPECT_EQ(BoundaryFace, readOnly.OppositeVertex(17, 17));
    ExpectFace(readOnly, 17, 29, 41);
    EXPECT_EQ(0x80000003u, readOnly.faces[0]);
    EXPECT_EQ(0x40000005u, readOnly.faces[1]);
    EXPECT_EQ(BoundaryFace, readOnly.faces[2]);
}

TEST(MeshAdjacencyLifetimeTest, RepeatedVertexQueriesKeepNativeFirstMatchingSlot) {
    const MeshAdjacency::Face repeated = {{7, 7, 11}, {BoundaryFace, BoundaryFace, BoundaryFace}};
    EXPECT_EQ(CKBYTE(0), repeated.FindEdge(7, 7));
    EXPECT_EQ(11u, repeated.OppositeVertex(7, 7));
    EXPECT_EQ(CKBYTE(1), repeated.FindEdge(7, 11));
    EXPECT_EQ(CKBYTE(1), repeated.FindEdge(11, 7));
    EXPECT_EQ(7u, repeated.OppositeVertex(7, 11));
    EXPECT_EQ(7u, repeated.OppositeVertex(11, 7));
    EXPECT_EQ(CKBYTE(0xff), repeated.FindEdge(11, 11));
    EXPECT_EQ(BoundaryFace, repeated.OppositeVertex(11, 11));
    const MeshAdjacency::Face collapsed = {{4, 4, 4}, {BoundaryFace, BoundaryFace, BoundaryFace}};
    EXPECT_EQ(CKBYTE(0), collapsed.FindEdge(4, 4));
    EXPECT_EQ(4u, collapsed.OppositeVertex(4, 4));
}

TEST(MeshAdjacencyLifetimeTest, Init16CopiesInputAndBothGetterOverloadsExposeOwnedTopology) {
    MeshAdjacency adjacency;
    const MeshAdjacency &readOnly = adjacency;
    EXPECT_EQ(0, adjacency.GetEdges().Size());
    EXPECT_EQ(0, adjacency.GetFaces().Size());
    EXPECT_EQ(&adjacency.GetEdges(), &readOnly.GetEdges());
    EXPECT_EQ(&adjacency.GetFaces(), &readOnly.GetFaces());
    CKWORD indices[3] = {65535, 0, 512};
    adjacency.Init(indices, 1);
    indices[0] = indices[1] = indices[2] = 37;
    ASSERT_EQ(1, readOnly.GetFaces().Size());
    ASSERT_EQ(3, readOnly.GetEdges().Size());
    ExpectFace(readOnly.GetFaces()[0], 65535, 0, 512);
    ExpectBoundaryLinks(readOnly.GetFaces()[0]);
    ExpectRawEdge(readOnly.GetEdges()[0], 0, 65535, 0);
    ExpectRawEdge(readOnly.GetEdges()[1], 512, 65535, 0);
    ExpectRawEdge(readOnly.GetEdges()[2], 0, 512, 0);
    EXPECT_EQ(adjacency.GetEdges().Begin(), readOnly.GetEdges().Begin());
    EXPECT_EQ(adjacency.GetFaces().Begin(), readOnly.GetFaces().Begin());
}

TEST(MeshAdjacencyLifetimeTest, Reinit32ReplacesComputed16LinksWithoutTruncatingVertexIds) {
    MeshAdjacency adjacency;
    CKWORD connected[6] = {0, 1, 2, 2, 1, 3};
    adjacency.Init(connected, 2);
    ASSERT_TRUE(adjacency.Compute(false, true));
    ASSERT_EQ(2, adjacency.GetFaces().Size());
    EXPECT_EQ(0, adjacency.GetEdges().Size());
    EXPECT_EQ(1u, adjacency.GetFaces()[0].faces[2]);
    EXPECT_EQ(0x80000000u, adjacency.GetFaces()[1].faces[0]);

    CKDWORD replacement[3] = {0x80000012u, 70001, 0x7fffabcdu};
    adjacency.Init(replacement, 1);
    replacement[0] = replacement[1] = replacement[2] = 9;
    ASSERT_EQ(1, adjacency.GetFaces().Size());
    ASSERT_EQ(3, adjacency.GetEdges().Size());
    ExpectFace(adjacency.GetFaces()[0], 0x80000012u, 70001, 0x7fffabcdu);
    ExpectBoundaryLinks(adjacency.GetFaces()[0]);
    ExpectRawEdge(adjacency.GetEdges()[0], 70001, 0x80000012u, 0);
    ExpectRawEdge(adjacency.GetEdges()[1], 0x7fffabcdu, 0x80000012u, 0);
    ExpectRawEdge(adjacency.GetEdges()[2], 70001, 0x7fffabcdu, 0);
    ASSERT_TRUE(adjacency.Compute(true, false));
    ASSERT_EQ(3, adjacency.GetEdges().Size());
    ASSERT_EQ(1, adjacency.GetFaces().Size());
    ExpectFace(adjacency.GetFaces()[0], 0x80000012u, 70001, 0x7fffabcdu);
    ExpectBoundaryLinks(adjacency.GetFaces()[0]);
}

TEST(MeshAdjacencyLifetimeTest, EmptyReinitClearsOldOutputsAndFreshInitCanBeComputedAgain) {
    MeshAdjacency adjacency;
    CKWORD first[6] = {0, 1, 2, 2, 1, 3};
    adjacency.Init(first, 2);
    ASSERT_TRUE(adjacency.Compute(true, true));
    ASSERT_EQ(5, adjacency.GetEdges().Size());
    adjacency.Init(static_cast<CKWORD *>(nullptr), 1);
    EXPECT_EQ(0, adjacency.GetEdges().Size());
    EXPECT_EQ(0, adjacency.GetFaces().Size());

    CKDWORD second[3] = {80000, 80001, 80002};
    adjacency.Init(second, 1);
    adjacency.Init(second, 0);
    EXPECT_EQ(0, adjacency.GetEdges().Size());
    EXPECT_EQ(0, adjacency.GetFaces().Size());

    CKWORD disconnected[6] = {10, 11, 12, 20, 21, 22};
    adjacency.Init(disconnected, 2);
    ASSERT_TRUE(adjacency.Compute(true, true));
    ASSERT_EQ(6, adjacency.GetEdges().Size());
    ASSERT_EQ(2, adjacency.GetFaces().Size());
    ExpectFace(adjacency.GetFaces()[0], 10, 11, 12);
    ExpectFace(adjacency.GetFaces()[1], 20, 21, 22);
    ExpectBoundaryLinks(adjacency.GetFaces()[0]);
    ExpectBoundaryLinks(adjacency.GetFaces()[1]);
}

TEST(MeshAdjacencyLifetimeTest, DestructionLeavesCallerBuffersOwnedByCaller) {
    CKWORD *indices16 = new CKWORD[3]{51, 53, 59};
    CKDWORD *indices32 = new CKDWORD[3]{90001, 90003, 90009};
    {
        MeshAdjacency adjacency;
        adjacency.Init(indices16, 1);
        ASSERT_TRUE(adjacency.Compute(false, true));
        adjacency.Init(indices32, 1);
        ASSERT_TRUE(adjacency.Compute(true, true));
    }
    EXPECT_EQ(CKWORD(51), indices16[0]);
    EXPECT_EQ(CKWORD(53), indices16[1]);
    EXPECT_EQ(CKWORD(59), indices16[2]);
    EXPECT_EQ(90001u, indices32[0]);
    EXPECT_EQ(90003u, indices32[1]);
    EXPECT_EQ(90009u, indices32[2]);
    delete[] indices16;
    delete[] indices32;
}

TEST(MeshAdjacencyLifetimeTest, RealStriperConsumerRepeatsFaceTraversalAndReinitializesItsTopology) {
    MeshStriper striper;
    CKWORD square[6] = {0, 1, 2, 2, 1, 3};
    const CKWORD expectedSquare[6] = {0, 1, 2, 2, 1, 3};
    ASSERT_TRUE(striper.Init(square, 2, CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_PARITYFIX));
    for (int i = 0; i < 6; ++i)
        square[i] = 47;
    MeshStriper::Result result = {};
    ASSERT_TRUE(striper.Compute(&result));
    EXPECT_EQ(1u, result.NbStrips);
    ExpectStripTriangles(result, expectedSquare, 2, true);
    ASSERT_TRUE(striper.Compute(&result));
    EXPECT_EQ(1u, result.NbStrips);
    ExpectStripTriangles(result, expectedSquare, 2, true);

    CKWORD disconnected[6] = {10, 11, 12, 20, 21, 22};
    ASSERT_TRUE(striper.Init(disconnected, 2, CKMESHSTRIPER_SORTSEEDS | CKMESHSTRIPER_PARITYFIX));
    ASSERT_TRUE(striper.Compute(&result));
    EXPECT_EQ(2u, result.NbStrips);
    ExpectStripTriangles(result, disconnected, 2, false);
    ASSERT_TRUE(striper.Compute(&result));
    ExpectStripTriangles(result, disconnected, 2, false);
}
