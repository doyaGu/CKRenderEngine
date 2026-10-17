#include <gtest/gtest.h>
#include "CKRenderEngineTypes.h"

#include <cmath>
#include <initializer_list>
#include <memory>
#include <set>

namespace {
void ExpectVector(const VxVector &value, float x, float y, float z) {
    EXPECT_NEAR(x, value.x, 0.000001f);
    EXPECT_NEAR(y, value.y, 0.000001f);
    EXPECT_NEAR(z, value.z, 0.000001f);
}

void ExpectGraph(const PMVertexEx &vertex, std::initializer_list<PMVertexEx *> neighbors,
                 std::initializer_list<PMFace *> faces) {
    const std::set<PMVertexEx *> expectedNeighbors(neighbors);
    const std::set<PMFace *> expectedFaces(faces);
    std::set<PMVertexEx *> actualNeighbors;
    std::set<PMFace *> actualFaces;
    for (int i = 0; i < vertex.neighbors.Size(); ++i)
        actualNeighbors.insert(vertex.neighbors[i]);
    for (int i = 0; i < vertex.faces.Size(); ++i)
        actualFaces.insert(vertex.faces[i]);
    EXPECT_EQ(expectedNeighbors, actualNeighbors);
    EXPECT_EQ(expectedFaces, actualFaces);
    EXPECT_EQ(static_cast<int>(expectedNeighbors.size()), vertex.neighbors.Size());
    EXPECT_EQ(static_cast<int>(expectedFaces.size()), vertex.faces.Size());
}

TEST(ProgressiveMeshTopologyTest, VertexInitializationAndNeighborIdentityOperationsRemainIndependent) {
    PMVertexEx empty;
    ExpectVector(empty.position, 0, 0, 0);
    EXPECT_EQ(-1, empty.originalIndex);
    EXPECT_EQ(nullptr, empty.collapseTarget);
    EXPECT_FLOAT_EQ(0, empty.collapseCost);
    EXPECT_EQ(-1, empty.arrayIndex);
    EXPECT_EQ(-1, empty.heapIndex);
    ExpectGraph(empty, {}, {});

    PMVertexEx a(VxVector(2, -3, 5), 40000);
    PMVertexEx b(VxVector(2, -3, 5), 40001);
    ExpectVector(a.position, 2, -3, 5);
    EXPECT_EQ(40000, a.originalIndex);
    EXPECT_EQ(nullptr, a.collapseTarget);
    EXPECT_FLOAT_EQ(0, a.collapseCost);
    EXPECT_EQ(-1, a.arrayIndex);
    EXPECT_EQ(-1, a.heapIndex);
    a.AddNeighbor(&a);
    a.AddNeighbor(&b);
    a.AddNeighbor(&b);
    EXPECT_TRUE(a.HasNeighbor(&b));
    EXPECT_FALSE(a.HasNeighbor(&a));
    EXPECT_FALSE(b.HasNeighbor(&a)); // These list operations are one-sided.
    b.AddNeighbor(&a);
    ExpectGraph(a, {&b}, {});
    ExpectGraph(b, {&a}, {});
    a.RemoveNeighbor(&b);
    EXPECT_FALSE(a.HasNeighbor(&b));
    EXPECT_TRUE(b.HasNeighbor(&a));
    a.RemoveNeighbor(&b); // Already absent remains absent.
    b.RemoveNeighbor(&a);
    ExpectGraph(a, {}, {});
    ExpectGraph(b, {}, {});
}

TEST(ProgressiveMeshTopologyTest, FaceConstructionRegistersMutualTopologyAndDerivesTheGeometricNormal) {
    PMVertexEx a(VxVector(0, 0, 0), 0);
    PMVertexEx b(VxVector(2, 0, 0), 1);
    PMVertexEx c(VxVector(0, 3, 0), 2);
    int materialIdentity = 7;
    PMFace face(&a, &b, &c, VxVector(1, 0, 0), &materialIdentity);
    EXPECT_EQ(&a, face.vertices[0]);
    EXPECT_EQ(&b, face.vertices[1]);
    EXPECT_EQ(&c, face.vertices[2]);
    EXPECT_EQ(&materialIdentity, face.material);
    EXPECT_EQ(-1, face.arrayIndex);
    // The actual triangle is in the XY plane. A stale supplied X normal must
    // not become the progressive algorithm's curvature input.
    ExpectVector(face.normal, 0, 0, 1);
    ExpectGraph(a, {&b, &c}, {&face});
    ExpectGraph(b, {&a, &c}, {&face});
    ExpectGraph(c, {&a, &b}, {&face});
}

TEST(ProgressiveMeshTopologyTest, DeletingSharedFacesPreservesTheRemainingEdgeThenRemovesTheFinalAdjacency) {
    PMVertexEx a(VxVector(0, 0, 0), 0);
    PMVertexEx b(VxVector(1, 0, 0), 1);
    PMVertexEx c(VxVector(0, 1, 0), 2);
    PMVertexEx d(VxVector(0, 0, 1), 3);
    std::unique_ptr<PMFace> first(new PMFace(&a, &b, &c, VxVector(0, 0, 1), nullptr));
    std::unique_ptr<PMFace> second(new PMFace(&b, &a, &d, VxVector(0, 1, 0), nullptr));
    ExpectGraph(a, {&b, &c, &d}, {first.get(), second.get()});
    PMVertexEx::RemoveIfNonNeighbor(&a, &b);
    PMVertexEx::RemoveIfNonNeighbor(&b, &a);
    EXPECT_TRUE(a.HasNeighbor(&b));
    EXPECT_TRUE(b.HasNeighbor(&a));
    first.reset();
    ExpectGraph(a, {&b, &d}, {second.get()});
    ExpectGraph(b, {&a, &d}, {second.get()});
    ExpectGraph(c, {}, {});
    ExpectGraph(d, {&a, &b}, {second.get()});
    second.reset();
    ExpectGraph(a, {}, {});
    ExpectGraph(b, {}, {});
    ExpectGraph(c, {}, {});
    ExpectGraph(d, {}, {});
}

TEST(ProgressiveMeshTopologyTest, ContainsVertexDistinguishesCoincidentVertexObjects) {
    PMVertexEx a(VxVector(0, 0, 0), 0);
    PMVertexEx coincident(VxVector(0, 0, 0), 10);
    PMVertexEx b(VxVector(1, 0, 0), 1);
    PMVertexEx c(VxVector(0, 1, 0), 2);
    PMFace face(&a, &b, &c, VxVector(0, 0, 1), nullptr);
    EXPECT_TRUE(face.ContainsVertex(&a));
    EXPECT_TRUE(face.ContainsVertex(&b));
    EXPECT_TRUE(face.ContainsVertex(&c));
    EXPECT_FALSE(face.ContainsVertex(&coincident));
    ExpectGraph(coincident, {}, {});
}

TEST(ProgressiveMeshTopologyTest, ReplaceVertexTransfersIncidencePreservesSharedEdgesAndUpdatesNormal) {
    PMVertexEx a(VxVector(0, 0, 0), 0);
    PMVertexEx b(VxVector(1, 0, 0), 1);
    PMVertexEx c(VxVector(0, 1, 0), 2);
    PMVertexEx replacement(VxVector(0, 0, 1), 3);
    PMVertexEx support(VxVector(0, -1, 0), 4);
    int materialIdentity = 9;
    PMFace moving(&a, &b, &c, VxVector(0, 0, 1), &materialIdentity);
    PMFace remaining(&a, &b, &support, VxVector(0, 0, -1), nullptr);
    moving.arrayIndex = 12;
    moving.ReplaceVertex(&a, &replacement);
    EXPECT_FALSE(moving.ContainsVertex(&a));
    EXPECT_TRUE(moving.ContainsVertex(&replacement));
    EXPECT_EQ(&materialIdentity, moving.material);
    EXPECT_EQ(12, moving.arrayIndex);
    ExpectGraph(a, {&b, &support}, {&remaining});
    ExpectGraph(b, {&a, &c, &replacement, &support}, {&moving, &remaining});
    ExpectGraph(c, {&b, &replacement}, {&moving});
    ExpectGraph(replacement, {&b, &c}, {&moving});
    ExpectGraph(support, {&a, &b}, {&remaining});
    const float diagonal = 1.0f / std::sqrt(3.0f);
    ExpectVector(moving.normal, diagonal, diagonal, diagonal);
    ExpectVector(remaining.normal, 0, 0, -1);
}

TEST(ProgressiveMeshTopologyTest, ConsecutiveReplacementsRetireOldVerticesWithoutDuplicateIncidences) {
    PMVertexEx a(VxVector(0, 0, 0), 0);
    PMVertexEx b(VxVector(1, 0, 0), 1);
    PMVertexEx c(VxVector(0, 1, 0), 2);
    PMVertexEx d(VxVector(0, 0, 1), 3);
    PMVertexEx e(VxVector(1, 1, 1), 4);
    PMFace face(&a, &b, &c, VxVector(0, 0, 1), nullptr);
    face.ReplaceVertex(&a, &d);
    face.ReplaceVertex(&b, &e);
    EXPECT_EQ(&d, face.vertices[0]);
    EXPECT_EQ(&e, face.vertices[1]);
    EXPECT_EQ(&c, face.vertices[2]);
    ExpectGraph(a, {}, {});
    ExpectGraph(b, {}, {});
    ExpectGraph(c, {&d, &e}, {&face});
    ExpectGraph(d, {&c, &e}, {&face});
    ExpectGraph(e, {&c, &d}, {&face});
    const float diagonal = 1.0f / std::sqrt(3.0f);
    ExpectVector(face.normal, -diagonal, diagonal, diagonal);
}

TEST(ProgressiveMeshTopologyTest, ComputeNormalUsesCurrentPositionsAndAuthoredWinding) {
    PMVertexEx a(VxVector(0, 0, 0), 0);
    PMVertexEx b(VxVector(2, 0, 0), 1);
    PMVertexEx c(VxVector(0, 3, 0), 2);
    PMFace forward(&a, &b, &c, VxVector(0, 0, 1), nullptr);
    PMFace reverse(&a, &c, &b, VxVector(0, 0, -1), nullptr);
    forward.ComputeNormal();
    reverse.ComputeNormal();
    ExpectVector(forward.normal, 0, 0, 1);
    ExpectVector(reverse.normal, 0, 0, -1);
    c.position = VxVector(0, 3, 3);
    forward.ComputeNormal();
    reverse.ComputeNormal();
    const float diagonal = std::sqrt(0.5f);
    ExpectVector(forward.normal, 0, -diagonal, diagonal);
    ExpectVector(reverse.normal, 0, diagonal, -diagonal);
    ExpectGraph(a, {&b, &c}, {&forward, &reverse});
    // Uniform scaling keeps this plane's direction. Below the native length
    // gate, retain the existing unit normal rather than storing a tiny raw
    // cross product which would corrupt subsequent curvature dot products.
    b.position = VxVector(0.0002f, 0, 0);
    c.position = VxVector(0, 0.0003f, 0.0003f);
    forward.ComputeNormal();
    reverse.ComputeNormal();
    ExpectVector(forward.normal, 0, -diagonal, diagonal);
    ExpectVector(reverse.normal, 0, diagonal, -diagonal);
}

TEST(ProgressiveMeshTopologyTest, DefaultFaceStaysDetachedAndDoesNotChangeLiveVertexGraphs) {
    PMVertexEx a(VxVector(0, 0, 0), 0);
    PMVertexEx b(VxVector(1, 0, 0), 1);
    PMVertexEx c(VxVector(0, 1, 0), 2);
    PMFace live(&a, &b, &c, VxVector(0, 0, 1), nullptr);
    {
        PMFace detached;
        EXPECT_EQ(nullptr, detached.vertices[0]);
        EXPECT_EQ(nullptr, detached.vertices[1]);
        EXPECT_EQ(nullptr, detached.vertices[2]);
        EXPECT_EQ(nullptr, detached.material);
        EXPECT_EQ(-1, detached.arrayIndex);
        EXPECT_FALSE(detached.ContainsVertex(&a));
        ExpectVector(detached.normal, 0, 0, 0);
    }
    ExpectGraph(a, {&b, &c}, {&live});
    ExpectGraph(b, {&a, &c}, {&live});
    ExpectGraph(c, {&a, &b}, {&live});
}
}
