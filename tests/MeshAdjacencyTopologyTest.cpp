#include <gtest/gtest.h>
#include "MeshAdjacency.h"

namespace {
const CKDWORD Boundary = 0xffffffffu;

struct ExpectedEdge { CKDWORD A, B, FirstFace, SecondFace; };

void ExpectFace(const MeshAdjacency &adjacency, int index,
                CKDWORD a, CKDWORD b, CKDWORD c,
                CKDWORD ab, CKDWORD ac, CKDWORD bc) {
    const XArray<MeshAdjacency::Face> &faces = adjacency.GetFaces();
    ASSERT_GE(index, 0); ASSERT_LT(index, faces.Size());
    const MeshAdjacency::Face &face = faces[index];
    EXPECT_EQ(a, face.vertices[0]); EXPECT_EQ(b, face.vertices[1]); EXPECT_EQ(c, face.vertices[2]);
    EXPECT_EQ(ab, face.faces[0]); EXPECT_EQ(ac, face.faces[1]); EXPECT_EQ(bc, face.faces[2]);
}

void ExpectEdges(const MeshAdjacency &adjacency, const ExpectedEdge *expected, int count) {
    const XArray<MeshAdjacency::Edge> &edges = adjacency.GetEdges();
    ASSERT_EQ(count, edges.Size());
    for (int i = 0; i < count; ++i) {
        SCOPED_TRACE(i);
        int matches = 0;
        for (int j = 0; j < edges.Size(); ++j) {
            if (edges[j].vertices[0] != expected[i].A || edges[j].vertices[1] != expected[i].B)
                continue;
            ++matches;
            EXPECT_EQ(expected[i].FirstFace, edges[j].faces[0]);
            EXPECT_EQ(expected[i].SecondFace, edges[j].faces[1]);
        }
        EXPECT_EQ(1, matches); // Each undirected edge is one complete owner tuple.
    }
}

void ExpectReciprocalLinks(const MeshAdjacency &adjacency) {
    const XArray<MeshAdjacency::Face> &faces = adjacency.GetFaces();
    const int endpoints[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (int face = 0; face < faces.Size(); ++face) {
        for (int edge = 0; edge < 3; ++edge) {
            SCOPED_TRACE(face);
            SCOPED_TRACE(edge);
            const CKDWORD link = faces[face].faces[edge];
            if (link == Boundary) continue;
            const CKDWORD peer = link & 0x3fffffffu;
            const CKDWORD peerEdge = link >> 30;
            ASSERT_LT(peer, static_cast<CKDWORD>(faces.Size())); ASSERT_LT(peerEdge, 3u);
            const CKDWORD back = faces[static_cast<int>(peer)].faces[peerEdge];
            EXPECT_NE(Boundary, back);
            EXPECT_EQ(static_cast<CKDWORD>(face), back & 0x3fffffffu);
            EXPECT_EQ(static_cast<CKDWORD>(edge), back >> 30);
            const CKDWORD a = faces[face].vertices[endpoints[edge][0]];
            const CKDWORD b = faces[face].vertices[endpoints[edge][1]];
            const CKDWORD p = faces[static_cast<int>(peer)].vertices[endpoints[peerEdge][0]];
            const CKDWORD q = faces[static_cast<int>(peer)].vertices[endpoints[peerEdge][1]];
            EXPECT_TRUE((a == p && b == q) || (a == q && b == p));
        }
    }
}

TEST(MeshAdjacencyTopologyTest, Init16CopiesTheInputAndPublishesCanonicalHalfEdgesBeforeLinking) {
    CKWORD indices[] = {9, 3, 7, 7, 3, 11};
    MeshAdjacency adjacency; adjacency.Init(indices, 2);
    indices[0] = 99; indices[5] = 99;
    ExpectFace(adjacency, 0, 9, 3, 7, Boundary, Boundary, Boundary);
    ExpectFace(adjacency, 1, 7, 3, 11, Boundary, Boundary, Boundary);
    const CKDWORD halves[6][4] = {
        {3, 9, 0, Boundary}, {7, 9, 0, Boundary}, {3, 7, 0, Boundary},
        {3, 7, 1, Boundary}, {7, 11, 1, Boundary}, {3, 11, 1, Boundary}
    };
    ASSERT_EQ(6, adjacency.GetEdges().Size());
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(halves[i][0], adjacency.GetEdges()[i].vertices[0]);
        EXPECT_EQ(halves[i][1], adjacency.GetEdges()[i].vertices[1]);
        EXPECT_EQ(halves[i][2], adjacency.GetEdges()[i].faces[0]);
        EXPECT_EQ(halves[i][3], adjacency.GetEdges()[i].faces[1]);
    }
    ASSERT_TRUE(adjacency.Compute(true, true));
    ExpectFace(adjacency, 0, 9, 3, 7, Boundary, Boundary, 1);
    ExpectFace(adjacency, 1, 7, 3, 11, 0x80000000u, Boundary, Boundary);
    const ExpectedEdge edges[] = {{3,9,0,Boundary},{7,9,0,Boundary},{3,7,0,1},{7,11,1,Boundary},{3,11,1,Boundary}};
    ExpectEdges(adjacency, edges, 5); ExpectReciprocalLinks(adjacency);
}

TEST(MeshAdjacencyTopologyTest, Init32RetainsFullWidthVertexLabelsAndReciprocalCounterpartSlots) {
    CKDWORD indices[] = {0x80000001u, 70001, 0xfffffffeu, 70002, 0xfffffffeu, 70001};
    MeshAdjacency adjacency; adjacency.Init(indices, 2);
    indices[0] = 0; indices[4] = 0;
    ASSERT_TRUE(adjacency.Compute(true, true));
    ExpectFace(adjacency, 0, 0x80000001u, 70001, 0xfffffffeu, Boundary, Boundary, 0x80000001u);
    ExpectFace(adjacency, 1, 70002, 0xfffffffeu, 70001, Boundary, Boundary, 0x80000000u);
    const ExpectedEdge edges[] = {
        {70001,0x80000001u,0,Boundary}, {0x80000001u,0xfffffffeu,0,Boundary}, {70001,0xfffffffeu,0,1},
        {70002,0xfffffffeu,1,Boundary}, {70001,70002,1,Boundary}
    };
    ExpectEdges(adjacency, edges, 5); ExpectReciprocalLinks(adjacency);
}

TEST(MeshAdjacencyTopologyTest, HighestSortedSharedEdgeIsOneRecordWithBothOwnersAndBothFaceLinks) {
    // (8,9) is the final group after the native face/V0/V1 radix passes.
    CKWORD indices[] = {0, 8, 9, 1, 9, 8};
    MeshAdjacency adjacency; adjacency.Init(indices, 2); ASSERT_TRUE(adjacency.Compute(true, true));
    ExpectFace(adjacency, 0, 0, 8, 9, Boundary, Boundary, 0x80000001u);
    ExpectFace(adjacency, 1, 1, 9, 8, Boundary, Boundary, 0x80000000u);
    const ExpectedEdge edges[] = {{0,8,0,Boundary},{0,9,0,Boundary},{8,9,0,1},{1,9,1,Boundary},{1,8,1,Boundary}};
    ExpectEdges(adjacency, edges, 5); ExpectReciprocalLinks(adjacency);
}

TEST(MeshAdjacencyTopologyTest, EveryFlagPairPublishesFacesWhileOnlyIEdgesControlsEdgeRetention) {
    for (int keepEdges = 0; keepEdges != 2; ++keepEdges) {
        for (int requestFaces = 0; requestFaces != 2; ++requestFaces) {
            SCOPED_TRACE(keepEdges);
            SCOPED_TRACE(requestFaces);
            CKWORD indices[] = {0, 1, 2, 2, 1, 3};
            MeshAdjacency adjacency; adjacency.Init(indices, 2);
            ASSERT_TRUE(adjacency.Compute(keepEdges != 0, requestFaces != 0));
            ASSERT_EQ(2, adjacency.GetFaces().Size());
            ExpectFace(adjacency, 0, 0, 1, 2, Boundary, Boundary, 1);
            ExpectFace(adjacency, 1, 2, 1, 3, 0x80000000u, Boundary, Boundary);
            EXPECT_EQ(keepEdges ? 5 : 0, adjacency.GetEdges().Size());
            ExpectReciprocalLinks(adjacency);
        }
    }
}

TEST(MeshAdjacencyTopologyTest, DifferentSharedSlotsRemainIndependentAcrossAChainAndDisconnectedComponent) {
    CKWORD indices[] = {0,1,2, 2,1,3, 2,3,4, 8,9,10};
    MeshAdjacency adjacency; adjacency.Init(indices, 4); ASSERT_TRUE(adjacency.Compute(true, true));
    ExpectFace(adjacency, 0, 0,1,2, Boundary,Boundary,1);
    ExpectFace(adjacency, 1, 2,1,3, 0x80000000u,2,Boundary);
    ExpectFace(adjacency, 2, 2,3,4, 0x40000001u,Boundary,Boundary);
    ExpectFace(adjacency, 3, 8,9,10, Boundary,Boundary,Boundary);
    const ExpectedEdge edges[] = {
        {0,1,0,Boundary},{0,2,0,Boundary},{1,2,0,1},{1,3,1,Boundary},{2,3,1,2},
        {2,4,2,Boundary},{3,4,2,Boundary},{8,9,3,Boundary},{8,10,3,Boundary},{9,10,3,Boundary}
    };
    ExpectEdges(adjacency, edges, 10); ExpectReciprocalLinks(adjacency);
}

TEST(MeshAdjacencyTopologyTest, ClosedTetrahedronHasSixDualOwnerEdgesAndReciprocalTagsInEverySlot) {
    CKWORD indices[] = {0,1,2, 0,3,1, 1,3,2, 2,3,0};
    MeshAdjacency adjacency; adjacency.Init(indices, 4); ASSERT_TRUE(adjacency.Compute(true, true));
    ExpectFace(adjacency,0,0,1,2,0x40000001u,0x40000003u,0x40000002u);
    ExpectFace(adjacency,1,0,3,1,0x80000003u,0,2);
    ExpectFace(adjacency,2,1,3,2,0x80000001u,0x80000000u,3);
    ExpectFace(adjacency,3,2,3,0,0x80000002u,0x40000000u,1);
    const ExpectedEdge edges[] = {{0,1,0,1},{0,2,0,3},{1,2,0,2},{0,3,1,3},{1,3,1,2},{2,3,2,3}};
    ExpectEdges(adjacency,edges,6); ExpectReciprocalLinks(adjacency);
}

TEST(MeshAdjacencyTopologyTest, ThreeOwnersAreRejectedForInteriorAndFinalGroupsWithEitherEdgeOutputPolicy) {
    CKWORD interior[] = {0,1,2, 1,0,3, 0,1,4};
    CKWORD finalGroup[] = {0,8,9, 1,9,8, 2,8,9};
    for (int final = 0; final != 2; ++final) {
        for (int keepEdges = 0; keepEdges != 2; ++keepEdges) {
            SCOPED_TRACE(final);
            SCOPED_TRACE(keepEdges);
            MeshAdjacency adjacency; adjacency.Init(final ? finalGroup : interior, 3);
            EXPECT_FALSE(adjacency.Compute(keepEdges != 0, true));
            // No transactional topology guarantee is imposed after failure.
        }
    }
}

TEST(MeshAdjacencyTopologyTest, FreshInitAfterConsumedEdgesResetsLinksAndChangesIndexWidthWithoutOldTopology) {
    MeshAdjacency adjacency; CKWORD oldMesh[] = {0,1,2, 2,1,3};
    adjacency.Init(oldMesh,2); ASSERT_TRUE(adjacency.Compute(false,true));
    ASSERT_EQ(0,adjacency.GetEdges().Size());
    CKDWORD nextMesh[] = {70000,70001,70002, 80000,80001,80002};
    adjacency.Init(nextMesh,2); ASSERT_EQ(6,adjacency.GetEdges().Size());
    ExpectFace(adjacency,0,70000,70001,70002,Boundary,Boundary,Boundary);
    ExpectFace(adjacency,1,80000,80001,80002,Boundary,Boundary,Boundary);
    ASSERT_TRUE(adjacency.Compute(true,false));
    const ExpectedEdge edges[] = {
        {70000,70001,0,Boundary},{70000,70002,0,Boundary},{70001,70002,0,Boundary},
        {80000,80001,1,Boundary},{80000,80002,1,Boundary},{80001,80002,1,Boundary}
    };
    ExpectEdges(adjacency,edges,6); ExpectReciprocalLinks(adjacency);
    CKWORD lastMesh[] = {4,5,6}; adjacency.Init(lastMesh,1);
    ASSERT_TRUE(adjacency.Compute(true,true)); ASSERT_EQ(1,adjacency.GetFaces().Size());
    ExpectFace(adjacency,0,4,5,6,Boundary,Boundary,Boundary); EXPECT_EQ(3,adjacency.GetEdges().Size());
}

TEST(MeshAdjacencyTopologyTest, FreshInitAfterANonmanifoldFailureProducesOnlyTheNextValidMesh) {
    // A valid shared pair precedes a later three-owner run. Failure may have
    // published partial links, but a new Init must not retain that old mesh.
    CKWORD invalid[] = {0,1,2, 1,0,3, 4,8,9, 5,9,8, 6,8,9};
    MeshAdjacency adjacency; adjacency.Init(invalid,5); ASSERT_FALSE(adjacency.Compute(true,true));
    CKWORD valid[] = {10,11,12, 12,11,13}; adjacency.Init(valid,2);
    ASSERT_EQ(2,adjacency.GetFaces().Size()); ASSERT_EQ(6,adjacency.GetEdges().Size());
    ExpectFace(adjacency,0,10,11,12,Boundary,Boundary,Boundary);
    ExpectFace(adjacency,1,12,11,13,Boundary,Boundary,Boundary);
    ASSERT_TRUE(adjacency.Compute(true,true));
    ExpectFace(adjacency,0,10,11,12,Boundary,Boundary,1);
    ExpectFace(adjacency,1,12,11,13,0x80000000u,Boundary,Boundary);
    const ExpectedEdge edges[] = {{10,11,0,Boundary},{10,12,0,Boundary},{11,12,0,1},{11,13,1,Boundary},{12,13,1,Boundary}};
    ExpectEdges(adjacency,edges,5); ExpectReciprocalLinks(adjacency);
}

TEST(MeshAdjacencyTopologyTest, EmptyTopologyComputesSuccessfullyAndClearedInputsStayReusable) {
    for (int flags = 0; flags != 4; ++flags) {
        SCOPED_TRACE(flags);
        const bool keepEdges = (flags & 1) != 0;
        const bool requestFaces = (flags & 2) != 0;
        MeshAdjacency adjacency;
        ASSERT_TRUE(adjacency.Compute(keepEdges, requestFaces));
        EXPECT_EQ(0, adjacency.GetFaces().Size()); EXPECT_EQ(0, adjacency.GetEdges().Size());
        adjacency.Init(static_cast<CKWORD *>(nullptr), 1);
        ASSERT_TRUE(adjacency.Compute(keepEdges, requestFaces));
        EXPECT_EQ(0, adjacency.GetFaces().Size()); EXPECT_EQ(0, adjacency.GetEdges().Size());
        adjacency.Init(static_cast<CKDWORD *>(nullptr), 1);
        ASSERT_TRUE(adjacency.Compute(keepEdges, requestFaces));
        EXPECT_EQ(0, adjacency.GetFaces().Size()); EXPECT_EQ(0, adjacency.GetEdges().Size());
        CKWORD words[] = {4,5,6}; adjacency.Init(words, 0);
        ASSERT_TRUE(adjacency.Compute(keepEdges, requestFaces));
        EXPECT_EQ(0, adjacency.GetFaces().Size()); EXPECT_EQ(0, adjacency.GetEdges().Size());
        CKDWORD dwords[] = {70000,70001,70002}; adjacency.Init(dwords, 0);
        ASSERT_TRUE(adjacency.Compute(keepEdges, requestFaces));
        EXPECT_EQ(0, adjacency.GetFaces().Size()); EXPECT_EQ(0, adjacency.GetEdges().Size());
        adjacency.Init(words, 1); ASSERT_TRUE(adjacency.Compute(true, true));
        ASSERT_EQ(1, adjacency.GetFaces().Size());
        ExpectFace(adjacency,0,4,5,6,Boundary,Boundary,Boundary);
        const ExpectedEdge edges[] = {{4,5,0,Boundary},{4,6,0,Boundary},{5,6,0,Boundary}};
        ExpectEdges(adjacency,edges,3); ExpectReciprocalLinks(adjacency);
    }
}
}
