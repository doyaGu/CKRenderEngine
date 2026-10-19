#include <gtest/gtest.h>

// Compile the real implementation in this test TU so its private helpers can
// be exercised without exports, test-only runtime hooks or algorithm copies.
#include "../src/CKMesh.cpp"

namespace {
PMVertexEx *AddCollapseVertex(PMEdgeCollapseData &graph, float x, float y, float z) {
    PMVertexEx *v = new PMVertexEx(VxVector(x, y, z), graph.vertices.Size());
    v->arrayIndex = graph.vertices.Size();
    graph.vertices.PushBack(v);
    return v;
}

PMFace *AddCollapseFace(PMEdgeCollapseData &graph, PMVertexEx *a, PMVertexEx *b,
                        PMVertexEx *c, void *material = nullptr) {
    PMFace *face = new PMFace(a, b, c, VxVector(0, 0, 0), material);
    face->arrayIndex = graph.faces.Size();
    graph.faces.PushBack(face);
    return face;
}

void InitCollapseHeap(PMEdgeCollapseData &graph, PMVertexEx *first = nullptr) {
    for (int i = 0; i < graph.vertices.Size(); ++i) {
        PMVertexEx *v = graph.vertices[i];
        CalculateMinCollapseCost(v);
        // Select a particular valid contraction for the topology tests.
        if (v == first) v->collapseCost = -1.0f;
        graph.AddToHeap(v);
    }
}

void ExpectCollapseGraph(const PMEdgeCollapseData &graph) {
    for (int fi = 0; fi < graph.faces.Size(); ++fi) {
        const PMFace *face = graph.faces[fi];
        ASSERT_NE(nullptr, face);
        EXPECT_EQ(fi, face->arrayIndex);
        for (int corner = 0; corner < 3; ++corner) {
            PMVertexEx *v = face->vertices[corner];
            ASSERT_NE(nullptr, v);
            ASSERT_GE(v->arrayIndex, 0);
            ASSERT_LT(v->arrayIndex, graph.vertices.Size());
            EXPECT_EQ(v, graph.vertices[v->arrayIndex]);
            EXPECT_NE(v, face->vertices[(corner + 1) % 3]);
        }
    }
    for (int vi = 0; vi < graph.vertices.Size(); ++vi) {
        const PMVertexEx *v = graph.vertices[vi];
        EXPECT_EQ(vi, v->arrayIndex);
        // Derive incidence and adjacency from the surviving triangle list,
        // independently of the lists updated by the collapse implementation.
        int incident = 0, adjacent = 0;
        for (int fi = 0; fi < graph.faces.Size(); ++fi) {
            PMFace *face = graph.faces[fi];
            const bool contains = face->vertices[0] == v || face->vertices[1] == v || face->vertices[2] == v;
            int occurrences = 0;
            for (int i = 0; i < v->faces.Size(); ++i) occurrences += v->faces[i] == face;
            EXPECT_EQ(contains ? 1 : 0, occurrences);
            incident += contains;
        }
        EXPECT_EQ(incident, v->faces.Size());
        for (int other = 0; other < graph.vertices.Size(); ++other) {
            PMVertexEx *n = graph.vertices[other];
            bool joined = false;
            for (int fi = 0; fi < graph.faces.Size(); ++fi) {
                const PMFace *face = graph.faces[fi];
                bool hasV = false, hasN = false;
                for (int corner = 0; corner < 3; ++corner) {
                    hasV |= face->vertices[corner] == v;
                    hasN |= face->vertices[corner] == n;
                }
                joined |= v != n && hasV && hasN;
            }
            int occurrences = 0;
            for (int i = 0; i < v->neighbors.Size(); ++i) occurrences += v->neighbors[i] == n;
            EXPECT_EQ(joined ? 1 : 0, occurrences);
            adjacent += joined;
        }
        EXPECT_EQ(adjacent, v->neighbors.Size());
    }
}

void ExpectCollapseHeap(const PMEdgeCollapseData &graph) {
    int active = 0;
    for (int i = 0; i < graph.heap.Size(); ++i) {
        PMVertexEx *v = graph.heap[i];
        if (v) {
            ++active;
            EXPECT_EQ(i, v->heapIndex);
            ASSERT_GE(v->arrayIndex, 0);
            ASSERT_LT(v->arrayIndex, graph.vertices.Size());
            EXPECT_EQ(v, graph.vertices[v->arrayIndex]);
        }
        for (int child = 2 * i + 1; child <= 2 * i + 2 && child < graph.heap.Size(); ++child) {
            if (graph.heap[child]) {
                ASSERT_NE(nullptr, v) << "A hole must sort after every live entry";
                EXPECT_LE(v->collapseCost, graph.heap[child]->collapseCost);
            }
        }
    }
    EXPECT_EQ(graph.vertices.Size(), active);
}

TEST(ProgressiveMeshCollapseTest, BoundaryElsewhereOverridesTheCostOfAnInteriorCandidateEdge) {
    PMEdgeCollapseData g;
    PMVertexEx *o = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *b = AddCollapseVertex(g, 3, 0, 0);
    PMVertexEx *c = AddCollapseVertex(g, 0, 4, 0);
    PMVertexEx *d = AddCollapseVertex(g, -3, 0, 0);
    PMVertexEx *e = AddCollapseVertex(g, 0, -4, 0);
    AddCollapseFace(g, o, b, c);
    PMFace *opening = AddCollapseFace(g, o, c, d);
    AddCollapseFace(g, o, d, e);
    AddCollapseFace(g, o, e, b);
    EXPECT_NEAR(0.003f, CalculateEdgeCollapseCost(o, b), 0.000001f);
    g.DeleteFace(opening); // Edge o-b still has two faces; o-c/o-d are boundaries.
    EXPECT_FLOAT_EQ(3.0f, CalculateEdgeCollapseCost(o, b));
    EXPECT_FLOAT_EQ(4.0f, CalculateEdgeCollapseCost(o, c));
    EXPECT_EQ(nullptr, o->collapseTarget); // Cost evaluation has no selection side effect.
    ExpectCollapseGraph(g);
}

TEST(ProgressiveMeshCollapseTest, FoldCostUsesTheWorstFaceAfterChoosingItsBestSharedNormal) {
    PMEdgeCollapseData g;
    PMVertexEx *o = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *b = AddCollapseVertex(g, 2, 0, 0);
    PMVertexEx *c = AddCollapseVertex(g, 0, 2, 0);
    PMVertexEx *d = AddCollapseVertex(g, 0, 0, 2);
    AddCollapseFace(g, o, b, c); // +Z
    AddCollapseFace(g, o, c, d); // +X
    AddCollapseFace(g, o, d, b); // +Y
    // Each edge at o has two faces. The +X face is perpendicular to both
    // shared faces of o-b: length 2 times (1.002 - 0) / 2 = 1.002.
    EXPECT_NEAR(1.002f, CalculateEdgeCollapseCost(o, b), 0.000001f);
    EXPECT_NEAR(1.002f, CalculateEdgeCollapseCost(o, c), 0.000001f);
    EXPECT_NEAR(1.002f, CalculateEdgeCollapseCost(o, d), 0.000001f);
}

TEST(ProgressiveMeshCollapseTest, CoincidentEndpointObjectsHaveZeroLengthCost) {
    PMEdgeCollapseData g;
    PMVertexEx *a = AddCollapseVertex(g, 2, -3, 5);
    PMVertexEx *b = AddCollapseVertex(g, 2, -3, 5);
    PMVertexEx *c = AddCollapseVertex(g, 7, -3, 5);
    AddCollapseFace(g, a, b, c);
    EXPECT_FLOAT_EQ(0.0f, CalculateEdgeCollapseCost(a, b));
    CalculateMinCollapseCost(a);
    EXPECT_EQ(b, a->collapseTarget);
    EXPECT_FLOAT_EQ(0.0f, a->collapseCost);
}

TEST(ProgressiveMeshCollapseTest, IsolatedVertexClearsAnEarlierSelectionAndGetsTheNegativeCost) {
    PMVertexEx a, formerTarget;
    a.collapseTarget = &formerTarget;
    a.collapseCost = 4000000.0f;
    CalculateMinCollapseCost(&a);
    EXPECT_EQ(nullptr, a.collapseTarget);
    EXPECT_FLOAT_EQ(-0.01f, a.collapseCost);
}

TEST(ProgressiveMeshCollapseTest, MinimumSelectionCanChooseALaterShorterBoundaryNeighbor) {
    PMEdgeCollapseData g;
    PMVertexEx *a = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *b = AddCollapseVertex(g, 3, 0, 0);
    PMVertexEx *c = AddCollapseVertex(g, 0, 1, 0);
    AddCollapseFace(g, a, b, c);
    ASSERT_EQ(b, a->neighbors[0]);
    CalculateMinCollapseCost(a);
    EXPECT_EQ(c, a->collapseTarget);
    EXPECT_FLOAT_EQ(1.0f, a->collapseCost);
}

TEST(ProgressiveMeshCollapseTest, EqualCostsKeepTheFirstNeighborRatherThanTheLowestIdentity) {
    PMEdgeCollapseData g;
    PMVertexEx *a = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *b = AddCollapseVertex(g, 1, 0, 0);
    PMVertexEx *c = AddCollapseVertex(g, 0, 1, 0);
    AddCollapseFace(g, a, b, c);
    CalculateMinCollapseCost(a);
    EXPECT_EQ(b, a->collapseTarget);
    a->neighbors[0] = c;
    a->neighbors[1] = b;
    CalculateMinCollapseCost(a);
    EXPECT_EQ(c, a->collapseTarget);
    EXPECT_FLOAT_EQ(1.0f, a->collapseCost);
}

TEST(ProgressiveMeshCollapseTest, FirstFiniteCandidateCanExceedTheInitialMillionCost) {
    PMEdgeCollapseData g;
    PMVertexEx *a = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *b = AddCollapseVertex(g, 2000000, 0, 0);
    PMVertexEx *c = AddCollapseVertex(g, 0, 3000000, 0);
    AddCollapseFace(g, a, b, c);
    CalculateMinCollapseCost(a);
    EXPECT_EQ(b, a->collapseTarget);
    EXPECT_FLOAT_EQ(2000000.0f, a->collapseCost);
}

TEST(ProgressiveMeshCollapseTest, NullTargetRetiresThePoppedIsolatedVertexAndFixesTheSwappedIndex) {
    PMEdgeCollapseData g;
    PMVertexEx *old = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *kept = AddCollapseVertex(g, 7, 8, 9);
    InitCollapseHeap(g, old);
    ASSERT_EQ(old, g.PopMinFromHeap());
    ProgressiveMeshCollapseVertex(&g, old, nullptr);
    ASSERT_EQ(1, g.vertices.Size());
    EXPECT_EQ(kept, g.vertices[0]);
    EXPECT_EQ(0, kept->arrayIndex);
    EXPECT_EQ(2, g.heap.Size()); // Pop leaves a hole; collapse does not shrink it.
    EXPECT_EQ(nullptr, kept->collapseTarget);
    ExpectCollapseGraph(g);
    ExpectCollapseHeap(g);
}

TEST(ProgressiveMeshCollapseTest, TriangleContractionDeletesTheFaceAndRecomputesBothIsolatedNeighbors) {
    PMEdgeCollapseData g;
    PMVertexEx *old = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *target = AddCollapseVertex(g, 2, 0, 0);
    PMVertexEx *other = AddCollapseVertex(g, 0, 3, 0);
    AddCollapseFace(g, old, target, other);
    InitCollapseHeap(g, old);
    ASSERT_EQ(old, g.PopMinFromHeap());
    ProgressiveMeshCollapseVertex(&g, old, target);
    ASSERT_EQ(2, g.vertices.Size());
    EXPECT_EQ(0, g.faces.Size());
    EXPECT_EQ(3, g.heap.Size());
    EXPECT_EQ(nullptr, target->collapseTarget);
    EXPECT_EQ(nullptr, other->collapseTarget);
    EXPECT_FLOAT_EQ(-0.01f, target->collapseCost);
    EXPECT_FLOAT_EQ(-0.01f, other->collapseCost);
    ExpectCollapseGraph(g);
    ExpectCollapseHeap(g);
}

TEST(ProgressiveMeshCollapseTest, ContractionPreservesFacePayloadUpdatesNormalsAndMovesCostsBothWays) {
    PMEdgeCollapseData g;
    PMVertexEx *old = AddCollapseVertex(g, 0, 0, 0);
    PMVertexEx *target = AddCollapseVertex(g, 1, 0, 0);
    PMVertexEx *a = AddCollapseVertex(g, 0, 1, 0);
    PMVertexEx *b = AddCollapseVertex(g, 0, 0, 1);
    PMVertexEx *c = AddCollapseVertex(g, 0, 2, 2);
    PMVertexEx *d = AddCollapseVertex(g, -1, 2, 0);
    PMVertexEx *unrelated = AddCollapseVertex(g, 9, 9, 9);
    int firstMaterial = 21, secondMaterial = 22;
    AddCollapseFace(g, old, target, a);
    AddCollapseFace(g, target, old, b);
    PMFace *first = AddCollapseFace(g, old, a, c, &firstMaterial);
    PMFace *second = AddCollapseFace(g, old, c, d, &secondMaterial);
    InitCollapseHeap(g, old);
    const float targetBefore = target->collapseCost;
    const float bBefore = b->collapseCost;
    ASSERT_EQ(old, g.PopMinFromHeap());
    ProgressiveMeshCollapseVertex(&g, old, target);
    ASSERT_EQ(6, g.vertices.Size());
    ASSERT_EQ(2, g.faces.Size());
    EXPECT_EQ(target, first->vertices[0]);
    EXPECT_EQ(a, first->vertices[1]);
    EXPECT_EQ(c, first->vertices[2]);
    EXPECT_EQ(target, second->vertices[0]);
    EXPECT_EQ(c, second->vertices[1]);
    EXPECT_EQ(d, second->vertices[2]);
    EXPECT_EQ(&firstMaterial, first->material);
    EXPECT_EQ(&secondMaterial, second->material);
    EXPECT_NEAR(2.0f / 3, first->normal.x, 0.000001f);
    EXPECT_NEAR(2.0f / 3, first->normal.y, 0.000001f);
    EXPECT_NEAR(-1.0f / 3, first->normal.z, 0.000001f);
    EXPECT_NEAR(-2.0f / 3, second->normal.x, 0.000001f);
    EXPECT_NEAR(-2.0f / 3, second->normal.y, 0.000001f);
    EXPECT_NEAR(1.0f / 3, second->normal.z, 0.000001f);
    EXPECT_NEAR(sqrtf(2.0f), target->collapseCost, 0.000001f);
    EXPECT_GT(target->collapseCost, targetBefore);
    EXPECT_FLOAT_EQ(-0.01f, b->collapseCost);
    EXPECT_LT(b->collapseCost, bBefore);
    EXPECT_EQ(nullptr, b->collapseTarget);
    EXPECT_EQ(nullptr, unrelated->collapseTarget);
    EXPECT_FLOAT_EQ(-0.01f, unrelated->collapseCost);
    EXPECT_NEAR(sqrtf(5.0f), c->collapseCost, 0.000001f);
    EXPECT_NEAR(sqrtf(5.0f), d->collapseCost, 0.000001f);
    ExpectCollapseGraph(g);
    ExpectCollapseHeap(g);
}

TEST(ProgressiveMeshCollapseTest, CompleteOctahedronReductionMaintainsTopologyAndAlwaysPopsTheTrueMinimum) {
    PMEdgeCollapseData g;
    PMVertexEx *top = AddCollapseVertex(g, 0, 0, 2);
    PMVertexEx *bottom = AddCollapseVertex(g, 0, 0, -2);
    PMVertexEx *ring[4] = {AddCollapseVertex(g, 3, 0, 0), AddCollapseVertex(g, 0, 4, 0),
                           AddCollapseVertex(g, -3, 0, 0), AddCollapseVertex(g, 0, -4, 0)};
    AddCollapseVertex(g, 20, 20, 20); // The null-target path interleaves with face collapses.
    for (int i = 0; i < 4; ++i) {
        AddCollapseFace(g, top, ring[i], ring[(i + 1) % 4]);
        AddCollapseFace(g, bottom, ring[(i + 1) % 4], ring[i]);
    }
    InitCollapseHeap(g);
    bool retired[7] = {};
    int contractions = 0;
    while (g.vertices.Size()) {
        ExpectCollapseGraph(g);
        ExpectCollapseHeap(g);
        float minimum = g.vertices[0]->collapseCost;
        for (int i = 1; i < g.vertices.Size(); ++i)
            if (g.vertices[i]->collapseCost < minimum) minimum = g.vertices[i]->collapseCost;
        PMVertexEx *old = g.PopMinFromHeap();
        ASSERT_NE(nullptr, old);
        EXPECT_FLOAT_EQ(minimum, old->collapseCost);
        ASSERT_GE(old->originalIndex, 0);
        ASSERT_LT(old->originalIndex, 7);
        EXPECT_FALSE(retired[old->originalIndex]);
        retired[old->originalIndex] = true;
        PMVertexEx *target = old->collapseTarget;
        if (target) {
            ASSERT_TRUE(old->HasNeighbor(target));
            ASSERT_EQ(target, g.vertices[target->arrayIndex]);
        } else {
            EXPECT_EQ(0, old->faces.Size());
            EXPECT_EQ(0, old->neighbors.Size());
        }
        ProgressiveMeshCollapseVertex(&g, old, target);
        ++contractions;
    }
    EXPECT_EQ(7, contractions);
    EXPECT_EQ(0, g.faces.Size());
    EXPECT_EQ(7, g.heap.Size());
    EXPECT_EQ(nullptr, g.PopMinFromHeap());
    for (int i = 0; i < 7; ++i) EXPECT_TRUE(retired[i]);
}
} // namespace
