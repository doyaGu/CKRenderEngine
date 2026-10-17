#include <gtest/gtest.h>
#include <array>
#include <map>
#include <set>
#include <vector>
#include <type_traits>

#include "NvStripifier.h"

static_assert(!std::is_copy_constructible<NvStripifier>::value,
              "A producer must not acquire shallow ownership of scratch storage");
static_assert(!std::is_copy_assignable<NvStripifier>::value,
              "A producer must not acquire shallow assignment ownership of scratch storage");

namespace {

typedef std::array<int, 3> Triangle;
typedef std::map<Triangle, int> Triangles;

Triangle CyclicTriangle(int a, int b, int c) {
    Triangle result = {{a, b, c}};
    const Triangle alternatives[] = {{{b, c, a}}, {{c, a, b}}};
    for (const Triangle &other : alternatives)
        if (other < result) result = other;
    return result;
}

Triangles ListTriangles(const XArray<CKWORD> &input) {
    Triangles result;
    for (int i = 0; i + 2 < input.Size(); i += 3) {
        const int a = input[i], b = input[i + 1], c = input[i + 2];
        if (a != b && b != c && a != c) ++result[CyclicTriangle(a, b, c)];
    }
    return result;
}

Triangles StreamTriangles(const XArray<CKWORD> &input, bool restartSeparators) {
    Triangles result;
    int first = 0, second = 0, references = 0;
    for (int i = 0; i < input.Size(); ++i) {
        const int vertex = input[i];
        if (restartSeparators && vertex == 65535) { references = 0; continue; }
        if (references >= 2 && first != second && first != vertex && second != vertex) {
            if ((references & 1) == 0) ++result[CyclicTriangle(first, second, vertex)];
            else ++result[CyclicTriangle(second, first, vertex)];
        }
        first = second;
        second = vertex;
        ++references;
    }
    return result;
}

void SetIndices(XArray<CKWORD> &target, const CKWORD *values, int count) {
    target.Resize(0);
    for (int i = 0; i < count; ++i) target.PushBack(values[i]);
}

std::vector<CKWORD> Snapshot(const XArray<CKWORD> &input) {
    return std::vector<CKWORD>(input.Begin(), input.End());
}

struct OwnedStrips {
    XArray<NvStripInfo *> Values;
    ~OwnedStrips() { NvStripifier::DestroyStrips(Values); }
};

::testing::AssertionResult LiveFacePartition(const XArray<NvStripInfo *> &strips,
                                            const Triangles &expected) {
    std::set<const NvStripInfo *> uniqueStrips;
    std::set<const NvFaceInfo *> faces;
    Triangles actual;
    for (int i = 0; i < strips.Size(); ++i) {
        const NvStripInfo *strip = strips[i];
        if (!strip || !uniqueStrips.insert(strip).second || strip->Faces.Size() == 0)
            return ::testing::AssertionFailure() << "Invalid or repeated output strip owner";
        for (int j = 0; j < strip->Faces.Size(); ++j) {
            const NvFaceInfo *face = strip->Faces[j];
            if (!face || !faces.insert(face).second)
                return ::testing::AssertionFailure() << "A face is not uniquely partitioned";
            ++actual[CyclicTriangle(face->V[0], face->V[1], face->V[2])];
        }
    }
    if (actual != expected)
        return ::testing::AssertionFailure() << "Returned face values differ from the input topology";
    for (int i = 0; i < strips.Size(); ++i) {
        const NvStripInfo *strip = strips[i];
        if (strip->StartFace && faces.find(strip->StartFace) == faces.end())
            return ::testing::AssertionFailure() << "StartFace is outside the live face set";
        for (int j = 0; j < strip->Faces.Size(); ++j) {
            const NvFaceInfo *face = strip->Faces[j];
            for (int edge = 0; edge < 3; ++edge) {
                const NvFaceInfo *neighbor = face->Neighbor[edge];
                if (!neighbor) continue;
                // Establish live ownership before dereferencing any borrowed neighbor.
                if (faces.find(neighbor) == faces.end())
                    return ::testing::AssertionFailure() << "Neighbor is outside the live face set";
                int shared = 0;
                for (int corner = 0; corner < 3; ++corner)
                    if (face->V[corner] == neighbor->V[0] || face->V[corner] == neighbor->V[1] ||
                        face->V[corner] == neighbor->V[2]) ++shared;
                if (shared != 2)
                    return ::testing::AssertionFailure() << "Neighbor does not share a topological edge";
            }
        }
    }
    return ::testing::AssertionSuccess();
}

} // namespace

TEST(NvStripifierLifetimeTest, DefaultValuesHaveIndependentEmptySeedsAndNoBorrowedOwners) {
    NvFaceInfo face, otherFace;
    NvEdgeInfo edge, otherEdge;
    NvStripInfo strip, otherStrip;
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(-1, face.V[i]); EXPECT_EQ(nullptr, face.Neighbor[i]);
    }
    EXPECT_EQ(-1, face.MarkA); EXPECT_EQ(-1, face.MarkB); EXPECT_EQ(-1, face.Experiment);
    EXPECT_EQ(2, edge.RefCount);
    EXPECT_EQ(nullptr, edge.Face0); EXPECT_EQ(nullptr, edge.Face1);
    EXPECT_EQ(-1, edge.Vertex0); EXPECT_EQ(-1, edge.Vertex1);
    EXPECT_EQ(nullptr, edge.NextV0); EXPECT_EQ(nullptr, edge.NextV1);
    EXPECT_EQ(nullptr, strip.StartFace); EXPECT_EQ(nullptr, strip.StartEdge);
    EXPECT_EQ(0, strip.StartCW); EXPECT_EQ(0, strip.Faces.Size());
    EXPECT_EQ(0, strip.StripId); EXPECT_EQ(-1, strip.ExperimentId); EXPECT_EQ(0, strip.field_20);
    face.V[0] = 7; edge.Vertex0 = 7; strip.StripId = 9;
    EXPECT_EQ(-1, otherFace.V[0]); EXPECT_EQ(-1, otherEdge.Vertex0); EXPECT_EQ(0, otherStrip.StripId);
}

TEST(NvStripifierLifetimeTest, ReturnedFacesAndNeighborsOutliveTheInputAndProducer) {
    const CKWORD square[] = {0, 1, 2, 2, 1, 3};
    XArray<CKWORD> expectedInput;
    SetIndices(expectedInput, square, 6);
    const Triangles expected = ListTriangles(expectedInput);
    OwnedStrips strips;
    {
        NvStripifier producer;
        XArray<CKWORD> source;
        SetIndices(source, square, 6);
        producer.Stripify(source, 0, 16, 4, strips.Values);
        ASSERT_TRUE(LiveFacePartition(strips.Values, expected));
        for (int i = 0; i < source.Size(); ++i) source[i] = 99;
    }
    ASSERT_TRUE(LiveFacePartition(strips.Values, expected));
    XArray<CKWORD> stream;
    CKDWORD count = 0;
    NvStripifier::CreateStrips(strips.Values, stream, true, count);
    EXPECT_EQ(1u, count);
    EXPECT_EQ(expected, StreamTriangles(stream, false));
}

TEST(NvStripifierLifetimeTest, ReturnedStripsDoNotExposeAnExpiredConstructionEdge) {
    const CKWORD square[] = {0, 1, 2, 2, 1, 3};
    XArray<CKWORD> source;
    SetIndices(source, square, 6);
    NvStripifier producer;
    OwnedStrips strips;
    producer.Stripify(source, 0, 16, 4, strips.Values);
    ASSERT_GT(strips.Values.Size(), 0);
    for (int i = 0; i < strips.Values.Size(); ++i) {
        ASSERT_NE(nullptr, strips.Values[i]);
        // Construction edges have already been released. Never dereference
        // an expired seed merely to make a use-after-free regression observable.
        EXPECT_EQ(nullptr, strips.Values[i]->StartEdge);
    }
}

TEST(NvStripifierLifetimeTest, RepeatedEmissionPreservesOwnersAndStreamsSurviveDestroy) {
    const CKWORD separate[] = {0, 1, 2, 3, 4, 5};
    XArray<CKWORD> source, stream, joined;
    SetIndices(source, separate, 6);
    const Triangles expected = ListTriangles(source);
    NvStripifier producer;
    OwnedStrips strips;
    producer.Stripify(source, 64, 16, 6, strips.Values);
    ASSERT_TRUE(LiveFacePartition(strips.Values, expected));
    std::vector<const NvStripInfo *> owners;
    for (int i = 0; i < strips.Values.Size(); ++i) owners.push_back(strips.Values[i]);
    CKDWORD count = 999;
    stream.PushBack(88);
    NvStripifier::CreateStrips(strips.Values, stream, false, count);
    EXPECT_EQ(2u, count);
    ASSERT_EQ(expected, StreamTriangles(stream, true));
    const std::vector<CKWORD> firstStream = Snapshot(stream);
    CKDWORD joinedCount = 0;
    NvStripifier::CreateStrips(strips.Values, joined, true, joinedCount);
    EXPECT_EQ(1u, joinedCount);
    EXPECT_EQ(expected, StreamTriangles(joined, false));
    NvStripifier::CreateStrips(strips.Values, stream, false, count);
    EXPECT_EQ(firstStream, Snapshot(stream));
    ASSERT_EQ(owners.size(), static_cast<std::size_t>(strips.Values.Size()));
    for (int i = 0; i < strips.Values.Size(); ++i) EXPECT_EQ(owners[i], strips.Values[i]);
    ASSERT_TRUE(LiveFacePartition(strips.Values, expected));
    NvStripifier::DestroyStrips(strips.Values);
    EXPECT_EQ(0, strips.Values.Size());
    NvStripifier::DestroyStrips(strips.Values);
    EXPECT_EQ(firstStream, Snapshot(stream));
    EXPECT_EQ(expected, StreamTriangles(joined, false));
}

TEST(NvStripifierLifetimeTest, ExplicitDestroyAllowsProducerReuseWithoutRetainingEarlierFaces) {
    const CKWORD firstFace[] = {0, 1, 2};
    const CKWORD nextFaces[] = {10, 11, 12, 20, 21, 22};
    XArray<CKWORD> source, stream;
    NvStripifier producer;
    OwnedStrips strips;
    SetIndices(source, firstFace, 3);
    producer.Stripify(source, 0, 16, 3, strips.Values);
    CKDWORD count = 0;
    NvStripifier::CreateStrips(strips.Values, stream, true, count);
    const std::vector<CKWORD> priorStream = Snapshot(stream);
    NvStripifier::DestroyStrips(strips.Values);
    SetIndices(source, nextFaces, 6);
    producer.Stripify(source, 64, 16, 23, strips.Values);
    ASSERT_TRUE(LiveFacePartition(strips.Values, ListTriangles(source)));
    XArray<CKWORD> nextStream;
    NvStripifier::CreateStrips(strips.Values, nextStream, false, count);
    EXPECT_EQ(2u, count);
    EXPECT_EQ(ListTriangles(source), StreamTriangles(nextStream, true));
    EXPECT_EQ(priorStream, Snapshot(stream));
}

TEST(NvStripifierLifetimeTest, OneShotAndTwoStagePathsReturnIndependentEquivalentGeometry) {
    const CKWORD faces[] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
    XArray<CKWORD> source, firstStream, secondStream;
    SetIndices(source, faces, 9);
    NvStripifier first, second;
    OwnedStrips strips;
    first.Stripify(source, 64, 16, 11, strips.Values);
    CKDWORD firstCount = 0, secondCount = 0;
    NvStripifier::CreateStrips(strips.Values, firstStream, true, firstCount);
    second.Stripify(source, 64, 16, 11, true, secondStream, secondCount);
    EXPECT_EQ(1u, firstCount); EXPECT_EQ(1u, secondCount);
    EXPECT_EQ(ListTriangles(source), StreamTriangles(firstStream, false));
    EXPECT_EQ(ListTriangles(source), StreamTriangles(secondStream, false));
    NvStripifier::DestroyStrips(strips.Values);
    EXPECT_EQ(ListTriangles(source), StreamTriangles(secondStream, false));
    for (int i = 0; i < source.Size(); ++i) EXPECT_EQ(faces[i], source[i]);
}

TEST(NvStripifierLifetimeTest, EmptyAndShortInputsResetFreshResultContainers) {
    NvStripifier producer;
    OwnedStrips strips;
    XArray<CKWORD> empty, output, shortInput;
    output.PushBack(17); output.PushBack(18); output.PushBack(19);
    CKDWORD count = 999;
    NvStripifier::CreateStrips(strips.Values, output, false, count);
    EXPECT_EQ(0, output.Size()); EXPECT_EQ(0u, count);
    producer.Stripify(empty, 0, 16, 0, true, output, count);
    EXPECT_EQ(0, output.Size()); EXPECT_EQ(0u, count);
    shortInput.PushBack(0); shortInput.PushBack(1);
    producer.Stripify(shortInput, 0, 16, 3, strips.Values);
    EXPECT_EQ(0, strips.Values.Size());
    producer.Stripify(shortInput, 0, 16, 3, false, output, count);
    EXPECT_EQ(0, output.Size()); EXPECT_EQ(0u, count);
}

TEST(NvStripifierLifetimeTest, HighVertexLabelRetainsDataThroughJoinedFallback) {
    OwnedStrips strips;
    const int values[2][3] = {{65535, 1, 2}, {3, 4, 5}};
    Triangles expected;
    for (int i = 0; i < 2; ++i) {
        NvFaceInfo *face = new NvFaceInfo;
        for (int j = 0; j < 3; ++j) face->V[j] = values[i][j];
        NvStripInfo *strip = new NvStripInfo;
        strip->StartFace = face;
        strip->Faces.PushBack(face);
        strips.Values.PushBack(strip);
        ++expected[CyclicTriangle(values[i][0], values[i][1], values[i][2])];
    }
    XArray<CKWORD> output;
    CKDWORD count = 999;
    NvStripifier::CreateStrips(strips.Values, output, false, count);
    EXPECT_EQ(1u, count); // 0xFFFF is vertex data, so restart encoding would lose it.
    EXPECT_EQ(expected, StreamTriangles(output, false));
    bool foundHighLabel = false;
    for (int i = 0; i < output.Size(); ++i) if (output[i] == 65535) foundHighLabel = true;
    EXPECT_TRUE(foundHighLabel);
    NvStripifier::DestroyStrips(strips.Values);
    EXPECT_EQ(expected, StreamTriangles(output, false));
}
