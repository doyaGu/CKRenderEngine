#include <gtest/gtest.h>
#include "NvStripifier.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {
typedef std::array<CKWORD, 3> Triangle;
typedef std::map<Triangle, int> TriangleCounts;

Triangle OrientedKey(CKWORD a, CKWORD b, CKWORD c) {
    const Triangle first = {{a, b, c}};
    const Triangle second = {{b, c, a}};
    const Triangle third = {{c, a, b}};
    return std::min(first, std::min(second, third));
}

void AddTriangle(TriangleCounts &counts, CKWORD a, CKWORD b, CKWORD c) {
    if (a != b && b != c && a != c)
        ++counts[OrientedKey(a, b, c)];
}

TriangleCounts TriangleList(const XArray<CKWORD> &indices) {
    TriangleCounts counts;
    for (int i = 0; i + 2 < indices.Size(); i += 3)
        AddTriangle(counts, indices[i], indices[i + 1], indices[i + 2]);
    return counts;
}

TriangleCounts GpuStrip(const XArray<CKWORD> &indices, bool joined, CKDWORD &segments) {
    TriangleCounts counts;
    std::vector<CKWORD> window;
    segments = 0;
    for (int i = 0; i < indices.Size(); ++i) {
        const CKWORD value = indices[i];
        if (!joined && value == 0xffff) {
            window.clear();
            continue;
        }
        if (window.empty())
            ++segments;
        window.push_back(value);
        const std::size_t count = window.size();
        if (count < 3)
            continue;
        CKWORD a = window[count - 3];
        CKWORD b = window[count - 2];
        if ((count & 1) == 0)
            std::swap(a, b); // GPU alternates winding at every strip triangle.
        AddTriangle(counts, a, b, window[count - 1]);
    }
    return counts;
}

XArray<CKWORD> Indices(const CKWORD *values, int count) {
    XArray<CKWORD> indices;
    for (int i = 0; i < count; ++i)
        indices.PushBack(values[i]);
    return indices;
}

struct OwnedStrips {
    XArray<NvStripInfo *> Values;
    ~OwnedStrips() { NvStripifier::DestroyStrips(Values); }
};

void CheckPublicOutput(const XArray<CKWORD> &input, CKWORD vertexCount,
                       int minimum, int cacheSize, OwnedStrips &owned) {
    std::vector<CKWORD> snapshot;
    for (int i = 0; i < input.Size(); ++i)
        snapshot.push_back(input[i]);
    NvStripifier stripifier;
    stripifier.Stripify(input, minimum, cacheSize, vertexCount, owned.Values);
    const TriangleCounts expected = TriangleList(input);
    TriangleCounts storedFaces;
    std::set<const NvFaceInfo *> identities;
    for (int s = 0; s < owned.Values.Size(); ++s) {
        const NvStripInfo *strip = owned.Values[s];
        ASSERT_NE(nullptr, strip);
        EXPECT_EQ(-1, strip->ExperimentId);
        for (int f = 0; f < strip->Faces.Size(); ++f) {
            const NvFaceInfo *face = strip->Faces[f];
            ASSERT_NE(nullptr, face);
            EXPECT_TRUE(identities.insert(face).second);
            EXPECT_GE(face->MarkA, 0);
            EXPECT_EQ(-1, face->Experiment);
            AddTriangle(storedFaces, static_cast<CKWORD>(face->V[0]),
                        static_cast<CKWORD>(face->V[1]), static_cast<CKWORD>(face->V[2]));
        }
    }
    EXPECT_EQ(expected, storedFaces); // Verify selection separately from emission.
    for (int join = 0; join < 2; ++join) {
        SCOPED_TRACE(join);
        XArray<CKWORD> stream;
        CKDWORD reported = 99;
        NvStripifier::CreateStrips(owned.Values, stream, join != 0, reported);
        CKDWORD observed = 0;
        EXPECT_EQ(expected, GpuStrip(stream, join != 0, observed));
        EXPECT_EQ(observed, reported);
    }
    ASSERT_EQ(static_cast<int>(snapshot.size()), input.Size());
    for (int i = 0; i < input.Size(); ++i)
        EXPECT_EQ(snapshot[static_cast<std::size_t>(i)], input[i]);
}

// Real public NvStripInfo input graph: undirected edges are linked into both
// endpoint buckets, with their actual incident faces. No private helper access.
struct FaceGraph {
    XArray<NvFaceInfo *> Faces;
    XArray<NvEdgeInfo *> Buckets;
    std::map<std::pair<int, int>, NvEdgeInfo *> Edges;

    ~FaceGraph() {
        for (std::map<std::pair<int, int>, NvEdgeInfo *>::iterator it = Edges.begin(); it != Edges.end(); ++it)
            delete it->second;
        for (int i = 0; i < Faces.Size(); ++i)
            delete Faces[i];
    }

    void Initialize(const XArray<CKWORD> &input, int vertexCount) {
        Buckets.Resize(vertexCount);
        for (int i = 0; i < vertexCount; ++i)
            Buckets[i] = nullptr;
        for (int i = 0; i < input.Size(); i += 3) {
            NvFaceInfo *face = new NvFaceInfo();
            for (int j = 0; j < 3; ++j)
                face->V[j] = input[i + j];
            Faces.PushBack(face);
            const int slots[3][2] = {{0, 1}, {0, 2}, {1, 2}};
            for (int j = 0; j < 3; ++j) {
                int a = face->V[slots[j][0]];
                int b = face->V[slots[j][1]];
                if (a > b)
                    std::swap(a, b);
                const std::pair<int, int> key(a, b);
                NvEdgeInfo *edge = nullptr;
                if (Edges.find(key) == Edges.end()) {
                    edge = new NvEdgeInfo();
                    edge->Vertex0 = a;
                    edge->Vertex1 = b;
                    edge->Face0 = face;
                    edge->NextV0 = Buckets[a];
                    edge->NextV1 = Buckets[b];
                    Buckets[a] = edge;
                    Buckets[b] = edge;
                    Edges[key] = edge;
                } else {
                    edge = Edges[key];
                    ASSERT_EQ(nullptr, edge->Face1);
                    edge->Face1 = face;
                }
            }
        }
        for (int i = 0; i < Faces.Size(); ++i) {
            NvFaceInfo *face = Faces[i];
            const int slots[3][2] = {{0, 1}, {0, 2}, {1, 2}};
            for (int j = 0; j < 3; ++j) {
                int a = face->V[slots[j][0]];
                int b = face->V[slots[j][1]];
                if (a > b)
                    std::swap(a, b);
                NvEdgeInfo *edge = Edges[std::make_pair(a, b)];
                face->Neighbor[j] = edge->Face0 == face ? edge->Face1 : edge->Face0;
            }
        }
    }
};

TEST(NvStripifierTopologyTest, RotatedConsistentlyOrientedPairsRetainGpuWinding) {
    const CKWORD a[] = {0, 1, 2};
    const CKWORD b[] = {2, 1, 3};
    for (int ra = 0; ra < 3; ++ra) {
        for (int rb = 0; rb < 3; ++rb) {
            SCOPED_TRACE(ra);
            SCOPED_TRACE(rb);
            XArray<CKWORD> input;
            for (int i = 0; i < 3; ++i) input.PushBack(a[(ra + i) % 3]);
            for (int i = 0; i < 3; ++i) input.PushBack(b[(rb + i) % 3]);
            OwnedStrips strips;
            CheckPublicOutput(input, 4, 0, 16, strips);
        }
    }
}

TEST(NvStripifierTopologyTest, SameDirectedSharedEdgesKeepBothOriginalFaceOrientations) {
    const CKWORD a[] = {0, 1, 2};
    const CKWORD b[] = {1, 2, 3};
    for (int rotation = 0; rotation < 3; ++rotation) {
        SCOPED_TRACE(rotation);
        XArray<CKWORD> input;
        for (int i = 0; i < 3; ++i) input.PushBack(a[(rotation + i) % 3]);
        for (int i = 0; i < 3; ++i) input.PushBack(b[(rotation + i) % 3]);
        OwnedStrips strips;
        CheckPublicOutput(input, 4, 0, 16, strips);
    }
}

TEST(NvStripifierTopologyTest, TenSamplesDoNotLimitTheNumberOfConnectedComponentsOptimized) {
    XArray<CKWORD> input;
    for (int component = 0; component < 16; ++component) {
        const CKWORD base = static_cast<CKWORD>(4 * component);
        const CKWORD pair[] = {base, static_cast<CKWORD>(base + 1), static_cast<CKWORD>(base + 2),
                               static_cast<CKWORD>(base + 2), static_cast<CKWORD>(base + 1), static_cast<CKWORD>(base + 3)};
        for (int i = 0; i < 6; ++i) input.PushBack(pair[i]);
    }
    OwnedStrips strips;
    CheckPublicOutput(input, 64, 0, 16, strips);
    ASSERT_EQ(16, strips.Values.Size());
    for (int i = 0; i < strips.Values.Size(); ++i)
        EXPECT_EQ(2, strips.Values[i]->Faces.Size());
}

TEST(NvStripifierTopologyTest, ClosedSurfaceWithoutBoundaryResetRetainsEveryOrientedFace) {
    const CKWORD values[] = {0, 2, 1, 0, 1, 3, 1, 2, 3, 2, 0, 3};
    const XArray<CKWORD> input = Indices(values, 12);
    OwnedStrips strips;
    CheckPublicOutput(input, 4, 0, 16, strips);
}

TEST(NvStripifierTopologyTest, BranchedGridRetainsOrientedCoverageWithSmallAndNormalCaches) {
    XArray<CKWORD> input;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const CKWORD a = static_cast<CKWORD>(y * 5 + x);
            const CKWORD b = static_cast<CKWORD>(a + 1);
            const CKWORD c = static_cast<CKWORD>(a + 5);
            const CKWORD d = static_cast<CKWORD>(a + 6);
            const CKWORD pair[] = {a, b, c, c, b, d};
            for (int i = 0; i < 6; ++i) input.PushBack(pair[i]);
        }
    }
    const int sizes[] = {4, 16};
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(sizes[i]);
        OwnedStrips strips;
        CheckPublicOutput(input, 20, 0, sizes[i], strips);
    }
}

TEST(NvStripifierTopologyTest, MinimumStripPolicyKeepsShortFacesAvailableForEmission) {
    const CKWORD values[] = {0, 1, 2, 2, 1, 3, 4, 5, 6};
    const XArray<CKWORD> input = Indices(values, 9);
    OwnedStrips strips;
    CheckPublicOutput(input, 7, 64, 16, strips);
}

TEST(NvStripifierTopologyTest, MiddleSeedBuildCrossesItsStartEdgeToExtendBothEnds) {
    const CKWORD values[] = {0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5, 4, 5, 6};
    const XArray<CKWORD> input = Indices(values, 15);
    FaceGraph graph;
    graph.Initialize(input, 7);
    ASSERT_EQ(5, graph.Faces.Size());
    NvStripInfo strip;
    strip.StartFace = graph.Faces[2];
    strip.StartEdge = graph.Edges[std::make_pair(2, 3)];
    strip.StartCW = 1;
    strip.StripId = 77;
    strip.ExperimentId = 42;
    strip.Build(graph.Buckets, graph.Faces);
    ASSERT_EQ(5, strip.Faces.Size());
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(graph.Faces[i], strip.Faces[i]);
        EXPECT_EQ(42, graph.Faces[i]->Experiment);
        EXPECT_EQ(77, graph.Faces[i]->MarkB);
        EXPECT_EQ(-1, graph.Faces[i]->MarkA);
    }
    strip.ExperimentId = 43;
    strip.StripId = 78;
    strip.Build(graph.Buckets, graph.Faces);
    ASSERT_EQ(5, strip.Faces.Size());
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(graph.Faces[i], strip.Faces[i]);
        EXPECT_EQ(43, graph.Faces[i]->Experiment);
        EXPECT_EQ(78, graph.Faces[i]->MarkB);
    }
}

TEST(NvStripifierTopologyTest, CombineReversesBackwardFacesAndRetainsForwardOrderWithoutTakingOwnership) {
    NvFaceInfo faces[5];
    XArray<NvFaceInfo *> forward;
    XArray<NvFaceInfo *> backward;
    forward.PushBack(&faces[2]);
    forward.PushBack(&faces[3]);
    forward.PushBack(&faces[4]);
    backward.PushBack(&faces[1]);
    backward.PushBack(&faces[0]);
    NvStripInfo strip;
    strip.Faces.PushBack(&faces[4]);
    strip.Combine(forward, backward);
    ASSERT_EQ(5, strip.Faces.Size());
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(&faces[i], strip.Faces[i]);
        EXPECT_EQ(-1, faces[i].MarkA);
        EXPECT_EQ(-1, faces[i].Experiment);
    }
    ASSERT_EQ(3, forward.Size());
    ASSERT_EQ(2, backward.Size());
    EXPECT_EQ(&faces[2], forward[0]);
    EXPECT_EQ(&faces[0], backward[1]);
}
}
