#include <gtest/gtest.h>
#include "XArray.h"
#include "CKTypes.h"
#include "VertexCache.h"
#include "NvStripifier.h"

#include <array>
#include <map>

namespace {
// Explicit template instantiation permits taking private member addresses.
// Keep the real declarations and their MSVC linkage unchanged in this test.
template<class Tag, typename Tag::Type Value> struct PrivateHelper {
    friend typename Tag::Type Resolve(Tag) { return Value; }
};
#define NV_HELPER(Name, Return, Args) \
    struct Name##Tag { typedef Return (*Type) Args; friend Type Resolve(Name##Tag); }; \
    template struct PrivateHelper<Name##Tag, &NvStripifier::Name>
NV_HELPER(GetUniqueVertexInB, int, (NvFaceInfo *, NvFaceInfo *));
NV_HELPER(GetSharedVertex, int, (NvFaceInfo *, NvFaceInfo *));
NV_HELPER(AlreadyExists, bool, (NvFaceInfo *, const XArray<NvFaceInfo *> &));
NV_HELPER(AvgStripSize, float, (const XArray<NvStripInfo *> &));
NV_HELPER(NextIsCW, bool, (int));
NV_HELPER(CalcNumHitsFace, int, (VertexCache *, NvFaceInfo *));
NV_HELPER(CalcNumHitsStrip, float, (VertexCache *, NvStripInfo *));
NV_HELPER(UpdateCacheFace, void, (VertexCache *, NvFaceInfo *));
NV_HELPER(UpdateCacheStrip, void, (VertexCache *, NvStripInfo *));
#undef NV_HELPER
typedef std::array<int, 3> Triangle;
typedef std::map<Triangle, int> TriangleCounts;

Triangle Canonical(int a, int b, int c) {
    const Triangle rotations[3] = {{{a, b, c}}, {{b, c, a}}, {{c, a, b}}};
    Triangle result = rotations[0];
    for (int i = 1; i < 3; ++i) {
        if (rotations[i] < result)
            result = rotations[i];
    }
    return result;
}

XArray<CKWORD> Chain(int faces) {
    XArray<CKWORD> indices;
    for (int i = 0; i < faces; ++i) {
        indices.PushBack((CKWORD)(i + (i % 2)));
        indices.PushBack((CKWORD)(i + 1 - (i % 2)));
        indices.PushBack((CKWORD)(i + 2));
    }
    return indices;
}

void ExpectFaces(const XArray<CKWORD> &input, const XArray<NvStripInfo *> &strips, int cap) {
    TriangleCounts expected, actual;
    for (int i = 0; i < input.Size(); i += 3)
        ++expected[Canonical(input[i], input[i + 1], input[i + 2])];
    for (int i = 0; i < strips.Size(); ++i) {
        const NvStripInfo *strip = strips[i];
        ASSERT_NE(nullptr, strip);
        EXPECT_GT(strip->Faces.Size(), 0);
        EXPECT_LE(strip->Faces.Size(), cap);
        EXPECT_EQ(-1, strip->ExperimentId);
        for (int j = 0; j < strip->Faces.Size(); ++j) {
            const NvFaceInfo *face = strip->Faces[j];
            ASSERT_NE(nullptr, face);
            EXPECT_EQ(strip->StripId, face->MarkA);
            EXPECT_EQ(-1, face->Experiment);
            ++actual[Canonical(face->V[0], face->V[1], face->V[2])];
        }
    }
    EXPECT_EQ(expected, actual);
}
}

TEST(NvStripifierOptimizationTest, HardwareCacheReservesSixEntriesBeforeSplitting) {
    const XArray<CKWORD> input = Chain(24);
    NvStripifier producer;
    XArray<NvStripInfo *> strips;
    producer.Stripify(input, 0, 10, 26, strips);
    ExpectFaces(input, strips, 4);
    EXPECT_GE(strips.Size(), 6);
    NvStripifier::DestroyStrips(strips);
}

TEST(NvStripifierOptimizationTest, CacheSizeChangesPartitionWithoutChangingGeometry) {
    const XArray<CKWORD> input = Chain(24);
    NvStripifier producer;
    XArray<NvStripInfo *> small, large;
    producer.Stripify(input, 0, 8, 26, small);
    producer.Stripify(input, 0, 64, 26, large);
    ExpectFaces(input, small, 2);
    ExpectFaces(input, large, 58);
    EXPECT_EQ(1, large.Size());
    EXPECT_GT(small.Size(), large.Size());
    NvStripifier::DestroyStrips(small);
    NvStripifier::DestroyStrips(large);
}

TEST(NvStripifierOptimizationTest, MinimumThresholdIsAppliedAfterCacheSplitting) {
    const XArray<CKWORD> input = Chain(9);
    NvStripifier producer;
    XArray<NvStripInfo *> strips;
    producer.Stripify(input, 4, 10, 11, strips);
    ExpectFaces(input, strips, 4);
    int full = 0, singleton = 0;
    for (int i = 0; i < strips.Size(); ++i) {
        full += strips[i]->Faces.Size() == 4;
        singleton += strips[i]->Faces.Size() == 1;
    }
    EXPECT_EQ(2, full);
    EXPECT_EQ(1, singleton);
    NvStripifier::DestroyStrips(strips);
}

TEST(NvStripifierOptimizationTest, ShortFacesRemainAvailableThroughPortableStripOnlyApi) {
    const XArray<CKWORD> input = Chain(8);
    NvStripifier producer;
    XArray<NvStripInfo *> strips;
    producer.Stripify(input, 20, 64, 10, strips);
    ExpectFaces(input, strips, 1);
    EXPECT_EQ(8, strips.Size());
    NvStripifier::DestroyStrips(strips);
}

TEST(NvStripifierOptimizationTest, CacheAtReservedBudgetStillMakesForwardProgress) {
    const XArray<CKWORD> input = Chain(7);
    NvStripifier producer;
    XArray<NvStripInfo *> strips;
    producer.Stripify(input, 0, 6, 9, strips);
    ExpectFaces(input, strips, 1);
    EXPECT_EQ(7, strips.Size());
    NvStripifier::DestroyStrips(strips);
}

TEST(NvStripifierOptimizationTest, ReusingProducerAppliesNewCacheAndMinimum) {
    const XArray<CKWORD> input = Chain(16);
    const XArray<CKWORD> snapshot = input;
    NvStripifier producer;
    XArray<NvStripInfo *> strips;
    producer.Stripify(input, 0, 64, 18, strips);
    ExpectFaces(input, strips, 58);
    EXPECT_EQ(1, strips.Size());
    NvStripifier::DestroyStrips(strips);
    producer.Stripify(input, 3, 9, 18, strips);
    ExpectFaces(input, strips, 3);
    EXPECT_GE(strips.Size(), 6);
    ASSERT_EQ(snapshot.Size(), input.Size());
    for (int i = 0; i < input.Size(); ++i)
        EXPECT_EQ(snapshot[i], input[i]);
    NvStripifier::DestroyStrips(strips);
}

TEST(NvStripifierOptimizationTest, FaceHelpersDistinguishSharedUniqueAndIdenticalCandidates) {
    NvFaceInfo a, b, copy;
    a.V[0] = 1; a.V[1] = 2; a.V[2] = 3;
    b.V[0] = 3; b.V[1] = 2; b.V[2] = 4;
    copy.V[0] = 1; copy.V[1] = 2; copy.V[2] = 3;
    EXPECT_EQ(4, Resolve(GetUniqueVertexInBTag())(&a, &b));
    EXPECT_EQ(-1, Resolve(GetUniqueVertexInBTag())(&a, &copy));
    EXPECT_EQ(3, Resolve(GetSharedVertexTag())(&a, &b));
    XArray<NvFaceInfo *> faces;
    faces.PushBack(&a);
    EXPECT_TRUE(Resolve(AlreadyExistsTag())(&copy, faces));
    EXPECT_FALSE(Resolve(AlreadyExistsTag())(&b, faces));
    EXPECT_FLOAT_EQ(0.0f, Resolve(AvgStripSizeTag())(XArray<NvStripInfo *>()));
    for (int count = 2; count < 12; ++count)
        EXPECT_EQ((count % 2) == 0, Resolve(NextIsCWTag())(count));
}

TEST(NvStripifierOptimizationTest, CacheScoringDoesNotTouchFifoAndUpdatesKeepCacheHitsInPlace) {
    NvFaceInfo a, b;
    a.V[0] = 1; a.V[1] = 2; a.V[2] = 3;
    b.V[0] = 2; b.V[1] = 4; b.V[2] = 5;
    NvStripInfo strip;
    strip.Faces.PushBack(&a);
    strip.Faces.PushBack(&b);
    VertexCache cache(3);
    cache.AddEntry(1);
    cache.AddEntry(2);
    EXPECT_EQ(2, Resolve(CalcNumHitsFaceTag())(&cache, &a));
    EXPECT_FLOAT_EQ(1.5f, Resolve(CalcNumHitsStripTag())(&cache, &strip));
    EXPECT_FALSE(cache.InCache(3));
    Resolve(UpdateCacheFaceTag())(&cache, &a);
    EXPECT_TRUE(cache.InCache(1));
    EXPECT_TRUE(cache.InCache(2));
    EXPECT_TRUE(cache.InCache(3));
    Resolve(UpdateCacheStripTag())(&cache, &strip);
    EXPECT_FALSE(cache.InCache(1));
    EXPECT_FALSE(cache.InCache(2));
    EXPECT_TRUE(cache.InCache(3));
    EXPECT_TRUE(cache.InCache(4));
    EXPECT_TRUE(cache.InCache(5));
    XArray<NvStripInfo *> strips;
    strips.PushBack(&strip);
    EXPECT_FLOAT_EQ(2.0f, Resolve(AvgStripSizeTag())(strips));
}
