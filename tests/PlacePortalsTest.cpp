#include <gtest/gtest.h>
#include <cstddef>
#include <climits>
#include "CKAll.h"
#include "RCKPlace.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class PortalPlaceProbe : public RCKPlace {
public:
    explicit PortalPlaceProbe(CKContext *context) : RCKPlace(context) {}
    void Append(CKPlace *place, CK3dEntity *portal) {
        CKPortalEntry entry; entry.place = place; entry.portal = portal; m_Portals.PushBack(entry);
    }
};
class PlacePortalsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKPlace *Place(RCKPlace &place) { return reinterpret_cast<CKPlace *>(&place); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Enable(RCK3dEntity &entity) { entity.SetFlags(entity.GetFlags() | CK_3DENTITY_PORTAL); }
    static void Entry(RCKPlace &source, int index, RCKPlace &destination, CK3dEntity *geometry) {
        CK3dEntity *actual = nullptr; EXPECT_EQ(Place(destination), Place(source)->GetPortal(index, &actual));
        EXPECT_EQ(geometry, actual);
    }
};
}

TEST_F(PlacePortalsTest, SDKAddsOneSymmetricPairAndDoesNotReparentGeometry) {
    RCKPlace a(Context), b(Context); RCK3dEntity geometry(Context), parent(Context); Enable(geometry);
    ASSERT_TRUE(geometry.SetParent(Entity(parent), TRUE)); Place(a)->AddPortal(Place(b), Entity(geometry));
    EXPECT_EQ(1, Place(a)->GetPortalCount()); EXPECT_EQ(1, Place(b)->GetPortalCount());
    Entry(a, 0, b, Entity(geometry)); Entry(b, 0, a, Entity(geometry)); EXPECT_EQ(Entity(parent), geometry.GetParent());
}
TEST_F(PlacePortalsTest, GeometryRequiresPortalFlagButAdditionalFlagsArePreserved) {
    RCKPlace a(Context), b(Context); RCK3dEntity geometry(Context); geometry.SetFlags(CK_3DENTITY_FRAME);
    a.AddPortal(Place(b), Entity(geometry)); EXPECT_EQ(0, a.GetPortalCount()); EXPECT_EQ(0, b.GetPortalCount());
    Enable(geometry); const CKDWORD flags = geometry.GetFlags(); a.AddPortal(Place(b), Entity(geometry));
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(flags, geometry.GetFlags());
}
TEST_F(PlacePortalsTest, NullGeometryAddsVisibleEverywherePair) {
    RCKPlace a(Context), b(Context); a.AddPortal(Place(b), nullptr);
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount()); Entry(a, 0, b, nullptr); Entry(b, 0, a, nullptr);
}
TEST_F(PlacePortalsTest, ExactPairAddedTwiceAndThroughReverseEndpointStaysUnique) {
    RCKPlace a(Context), b(Context); RCK3dEntity geometry(Context); Enable(geometry);
    a.AddPortal(Place(b), Entity(geometry)); a.AddPortal(Place(b), Entity(geometry)); b.AddPortal(Place(a), Entity(geometry));
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount());
}
TEST_F(PlacePortalsTest, MultipleGeometriesRetainInsertionOrderForSameDestination) {
    RCKPlace a(Context), b(Context); RCK3dEntity first(Context), second(Context); Enable(first); Enable(second);
    a.AddPortal(Place(b), Entity(first)); a.AddPortal(Place(b), Entity(second)); EXPECT_EQ(2, a.GetPortalCount());
    Entry(a, 0, b, Entity(first)); Entry(a, 1, b, Entity(second)); Entry(b, 0, a, Entity(first)); Entry(b, 1, a, Entity(second));
}
TEST_F(PlacePortalsTest, SameGeometryCanConnectDistinctDestinations) {
    RCKPlace a(Context), b(Context), c(Context); RCK3dEntity geometry(Context); Enable(geometry);
    a.AddPortal(Place(b), Entity(geometry)); a.AddPortal(Place(c), Entity(geometry)); EXPECT_EQ(2, a.GetPortalCount());
    Entry(a, 0, b, Entity(geometry)); Entry(a, 1, c, Entity(geometry));
}
TEST_F(PlacePortalsTest, WildcardSuppressesLaterSpecificPairsOnlyForItsDestination) {
    RCKPlace a(Context), b(Context), c(Context); RCK3dEntity geometry(Context); Enable(geometry);
    a.AddPortal(Place(b), nullptr); a.AddPortal(Place(b), Entity(geometry)); a.AddPortal(Place(c), Entity(geometry));
    EXPECT_EQ(2, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount()); Entry(a, 0, b, nullptr); Entry(a, 1, c, Entity(geometry));
}
TEST_F(PlacePortalsTest, LaterWildcardCoexistsWithEarlierSpecificPairs) {
    RCKPlace a(Context), b(Context); RCK3dEntity first(Context), second(Context); Enable(first); Enable(second);
    a.AddPortal(Place(b), Entity(first)); a.AddPortal(Place(b), nullptr); a.AddPortal(Place(b), Entity(second));
    EXPECT_EQ(2, a.GetPortalCount()); EXPECT_EQ(2, b.GetPortalCount()); Entry(a, 0, b, Entity(first)); Entry(a, 1, b, nullptr);
}
TEST_F(PlacePortalsTest, NullAndSelfDestinationsLeaveOtherConnectionsUntouched) {
    RCKPlace a(Context), b(Context); a.AddPortal(Place(b), nullptr);
    a.AddPortal(nullptr, nullptr); a.AddPortal(Place(a), nullptr); a.RemovePortal(nullptr, nullptr); a.RemovePortal(Place(a), nullptr);
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount()); Entry(a, 0, b, nullptr);
}
TEST_F(PlacePortalsTest, RemoveExactPairRemovesBothDirectionsAndPreservesOtherGeometry) {
    RCKPlace a(Context), b(Context); RCK3dEntity first(Context), second(Context); Enable(first); Enable(second);
    a.AddPortal(Place(b), Entity(first)); a.AddPortal(Place(b), Entity(second)); Place(a)->RemovePortal(Place(b), Entity(first));
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount()); Entry(a, 0, b, Entity(second)); Entry(b, 0, a, Entity(second));
}
TEST_F(PlacePortalsTest, NullRemovalMeansExactWildcardAndRetainsSpecificGeometry) {
    RCKPlace a(Context), b(Context); RCK3dEntity geometry(Context); Enable(geometry);
    a.AddPortal(Place(b), Entity(geometry)); a.AddPortal(Place(b), nullptr); a.RemovePortal(Place(b), nullptr);
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount()); Entry(a, 0, b, Entity(geometry));
}
TEST_F(PlacePortalsTest, AbsentRemovalCannotRemoveAnotherDestinationOrGeometry) {
    RCKPlace a(Context), b(Context), c(Context); RCK3dEntity geometry(Context), absent(Context); Enable(geometry);
    a.AddPortal(Place(b), Entity(geometry)); a.RemovePortal(Place(c), Entity(geometry)); a.RemovePortal(Place(b), Entity(absent));
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount()); Entry(a, 0, b, Entity(geometry));
}
TEST_F(PlacePortalsTest, ExistingAsymmetricLocalPairDoesNotCreateReciprocalEntry) {
    PortalPlaceProbe a(Context); RCKPlace b(Context); RCK3dEntity geometry(Context); Enable(geometry);
    a.Append(Place(b), Entity(geometry)); a.AddPortal(Place(b), Entity(geometry));
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(0, b.GetPortalCount());
}
TEST_F(PlacePortalsTest, ExistingAsymmetricWildcardBlocksSpecificPairWithoutChangingOtherEndpoint) {
    PortalPlaceProbe a(Context); RCKPlace b(Context); RCK3dEntity geometry(Context); Enable(geometry);
    a.Append(Place(b), nullptr); a.AddPortal(Place(b), Entity(geometry)); EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(0, b.GetPortalCount());
}
TEST_F(PlacePortalsTest, ReverseExistingAsymmetricPairIsNotDuplicatedWhenRepairingMissingLocalDirection) {
    RCKPlace a(Context); PortalPlaceProbe b(Context); RCK3dEntity geometry(Context); Enable(geometry);
    b.Append(Place(a), Entity(geometry)); a.AddPortal(Place(b), Entity(geometry));
    EXPECT_EQ(1, a.GetPortalCount()); EXPECT_EQ(1, b.GetPortalCount()); Entry(a, 0, b, Entity(geometry));
}
TEST_F(PlacePortalsTest, RemoveDeletesOnlyFirstLiteralDuplicateInEachDirection) {
    PortalPlaceProbe a(Context), b(Context); RCK3dEntity geometry(Context);
    for (int i = 0; i < 3; ++i) { a.Append(Place(b), Entity(geometry)); b.Append(Place(a), Entity(geometry)); }
    a.RemovePortal(Place(b), Entity(geometry)); EXPECT_EQ(2, a.GetPortalCount()); EXPECT_EQ(2, b.GetPortalCount());
    a.RemovePortal(Place(b), Entity(geometry)); a.RemovePortal(Place(b), Entity(geometry));
    EXPECT_EQ(0, a.GetPortalCount()); EXPECT_EQ(0, b.GetPortalCount());
}
TEST_F(PlacePortalsTest, RemoveFromAbsentLocalDirectionStillRemovesReverseDirection) {
    RCKPlace a(Context); PortalPlaceProbe b(Context); b.Append(Place(a), nullptr);
    a.RemovePortal(Place(b), nullptr); EXPECT_EQ(0, a.GetPortalCount()); EXPECT_EQ(0, b.GetPortalCount());
}
TEST_F(PlacePortalsTest, ValidIndexOptionalOutputAndNullGeometryAreDistinctFromInvalidIndex) {
    RCKPlace a(Context), b(Context); RCK3dEntity sentinel(Context); a.AddPortal(Place(b), nullptr);
    CK3dEntity *value = Entity(sentinel); EXPECT_EQ(Place(b), a.GetPortal(0, &value)); EXPECT_EQ(nullptr, value);
    EXPECT_EQ(Place(b), a.GetPortal(0, nullptr));
}
TEST_F(PlacePortalsTest, InvalidIndicesOnEmptyArrayReturnNullAndPreserveOutput) {
    RCKPlace a(Context); RCK3dEntity sentinel(Context);
    const int indices[] = {-1, 0, 1, INT_MAX};
    for (int index : indices) { CK3dEntity *value = Entity(sentinel); EXPECT_EQ(nullptr, a.GetPortal(index, &value)); EXPECT_EQ(Entity(sentinel), value); }
}
TEST_F(PlacePortalsTest, InvalidIndicesOnPopulatedArrayReturnNullAndPreserveOutput) {
    RCKPlace a(Context), b(Context); RCK3dEntity sentinel(Context); a.AddPortal(Place(b), nullptr);
    const int indices[] = {-1, 1, 2, INT_MAX};
    for (int index : indices) { CK3dEntity *value = Entity(sentinel); EXPECT_EQ(nullptr, a.GetPortal(index, &value)); EXPECT_EQ(Entity(sentinel), value); }
    EXPECT_EQ(1, a.GetPortalCount()); Entry(a, 0, b, nullptr);
}
TEST_F(PlacePortalsTest, MutableSDKViewportReferenceBelongsToOnePlace) {
    RCKPlace a(Context), b(Context); VxRect &clip = Place(a)->ViewportClip();
    EXPECT_EQ(&clip, &a.ViewportClip()); EXPECT_NE(&clip, &b.ViewportClip());
    clip.left = -3; clip.top = 5; clip.right = 17; clip.bottom = 23;
    EXPECT_FLOAT_EQ(-3, a.ViewportClip().left); EXPECT_FLOAT_EQ(5, a.ViewportClip().top);
    EXPECT_FLOAT_EQ(17, a.ViewportClip().right); EXPECT_FLOAT_EQ(23, a.ViewportClip().bottom);
    EXPECT_FLOAT_EQ(0, b.ViewportClip().left); EXPECT_FLOAT_EQ(0, b.ViewportClip().bottom);
}
TEST_F(PlacePortalsTest, PortalEntryEqualityUsesBothExactPointersAndActualPlatformLayout) {
    RCKPlace a(Context), b(Context); RCK3dEntity first(Context), second(Context);
    CKPortalEntry entry = {Place(a), Entity(first)}, same = entry, differentPlace = {Place(b), Entity(first)};
    CKPortalEntry differentGeometry = {Place(a), Entity(second)}, wildcard = {Place(a), nullptr};
    EXPECT_TRUE(entry == same); EXPECT_FALSE(entry == differentPlace); EXPECT_FALSE(entry == differentGeometry); EXPECT_FALSE(entry == wildcard);
    CKPortalEntry nullPair = {nullptr, nullptr}; EXPECT_TRUE(nullPair == nullPair);
    EXPECT_EQ(2 * sizeof(void *), sizeof(CKPortalEntry)); EXPECT_EQ(alignof(void *), alignof(CKPortalEntry));
    EXPECT_EQ(0u, offsetof(CKPortalEntry, place)); EXPECT_EQ(sizeof(void *), offsetof(CKPortalEntry, portal));
    ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(CKPortalEntry)));
    ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(CKPortalEntry)));
    ::testing::Test::RecordProperty("place_offset", static_cast<int>(offsetof(CKPortalEntry, place)));
    ::testing::Test::RecordProperty("portal_offset", static_cast<int>(offsetof(CKPortalEntry, portal)));
    ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
}
