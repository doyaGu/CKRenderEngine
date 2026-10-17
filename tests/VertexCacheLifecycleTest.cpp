#include <gtest/gtest.h>
#include <climits>
#include <type_traits>

#include "VertexCache.h"

static_assert(!std::is_copy_constructible<VertexCache>::value,
              "The entry allocation must not acquire a shallow-copy owner");
static_assert(!std::is_copy_assignable<VertexCache>::value,
              "The entry allocation must not acquire a shallow-assignment owner");

TEST(VertexCacheLifecycleTest, PositiveCapacitiesStartWithOnlyTheEmptySentinel) {
    const int sizes[] = {1, 2, 8, 16, 24, 31};
    for (int sample = 0; sample < 6; ++sample) {
        VertexCache cache(sizes[sample]);
        const VertexCache &readOnly = cache;
        EXPECT_EQ(sizes[sample], readOnly.Size());
        EXPECT_TRUE(readOnly.InCache(-1));
        EXPECT_FALSE(readOnly.InCache(0));
        EXPECT_FALSE(readOnly.InCache(1));
        EXPECT_FALSE(readOnly.InCache(65535));
        EXPECT_FALSE(readOnly.InCache(INT_MAX));
    }
}

TEST(VertexCacheLifecycleTest, ClearRemovesEveryEntryAndRetainsConfiguredCapacity) {
    const int sizes[] = {1, 3, 16};
    for (int sample = 0; sample < 3; ++sample) {
        VertexCache cache(sizes[sample]);
        for (int i = 0; i < sizes[sample]; ++i)
            cache.AddEntry(100 + i);
        for (int i = 0; i < sizes[sample]; ++i)
            ASSERT_TRUE(cache.InCache(100 + i));
        EXPECT_FALSE(cache.InCache(-1));

        cache.Clear();
        EXPECT_EQ(sizes[sample], cache.Size());
        EXPECT_TRUE(cache.InCache(-1));
        for (int i = 0; i < sizes[sample]; ++i)
            EXPECT_FALSE(cache.InCache(100 + i));
    }
}

TEST(VertexCacheLifecycleTest, ClearingAndRefillingOneOwnerDoesNotChangeAnother) {
    VertexCache first(3), second(7);
    for (int i = 0; i < 3; ++i) first.AddEntry(10 + i);
    for (int i = 0; i < 7; ++i) second.AddEntry(20 + i);
    first.Clear();
    for (int i = 0; i < 3; ++i) EXPECT_FALSE(first.InCache(10 + i));
    for (int i = 0; i < 7; ++i) EXPECT_TRUE(second.InCache(20 + i));

    for (int i = 0; i < 3; ++i) first.AddEntry(30 + i);
    first.Clear();
    first.Clear();
    EXPECT_TRUE(first.InCache(-1));
    for (int i = 0; i < 3; ++i) EXPECT_FALSE(first.InCache(30 + i));
    for (int i = 0; i < 7; ++i) EXPECT_TRUE(second.InCache(20 + i));
    EXPECT_EQ(3, first.Size());
    EXPECT_EQ(7, second.Size());
}

TEST(VertexCacheLifecycleTest, NestedStackOwnerDestructionPreservesOuterContents) {
    VertexCache outer(4);
    outer.AddEntry(101); outer.AddEntry(102);
    {
        VertexCache inner(7);
        inner.AddEntry(201); inner.AddEntry(202);
        EXPECT_TRUE(inner.InCache(201));
        EXPECT_FALSE(inner.InCache(101));
        outer.AddEntry(103);
        EXPECT_FALSE(inner.InCache(103));
    }
    EXPECT_EQ(4, outer.Size());
    EXPECT_TRUE(outer.InCache(101));
    EXPECT_TRUE(outer.InCache(102));
    EXPECT_TRUE(outer.InCache(103));
    EXPECT_FALSE(outer.InCache(201));
    outer.AddEntry(104);
    EXPECT_TRUE(outer.InCache(104));
}

TEST(VertexCacheLifecycleTest, DynamicOwnersRemainIndependentAcrossDestructionOrder) {
    VertexCache *first = new VertexCache(2);
    VertexCache *survivor = new VertexCache(5);
    first->AddEntry(1001);
    survivor->AddEntry(2001); survivor->AddEntry(2002);
    delete first;
    EXPECT_EQ(5, survivor->Size());
    EXPECT_TRUE(survivor->InCache(2001));
    EXPECT_TRUE(survivor->InCache(2002));
    EXPECT_FALSE(survivor->InCache(1001));

    VertexCache *replacement = new VertexCache(3);
    replacement->AddEntry(3001);
    delete survivor;
    EXPECT_EQ(3, replacement->Size());
    EXPECT_TRUE(replacement->InCache(3001));
    EXPECT_FALSE(replacement->InCache(2001));
    replacement->Clear();
    EXPECT_FALSE(replacement->InCache(3001));
    EXPECT_TRUE(replacement->InCache(-1));
    delete replacement;
}

TEST(VertexCacheLifecycleTest, ZeroCapacitySupportsLookupAndClearWithoutInsertion) {
    VertexCache cache(0);
    const VertexCache &readOnly = cache;
    EXPECT_EQ(0, readOnly.Size());
    EXPECT_FALSE(readOnly.InCache(-1));
    EXPECT_FALSE(readOnly.InCache(0));
    EXPECT_FALSE(readOnly.InCache(INT_MAX));
    cache.Clear();
    cache.Clear();
    EXPECT_EQ(0, readOnly.Size());
    EXPECT_FALSE(readOnly.InCache(-1));
    EXPECT_FALSE(readOnly.InCache(0));
}
