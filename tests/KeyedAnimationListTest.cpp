#include <gtest/gtest.h>
#include <climits>
#include <new>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class KeyedListProbe : public RCKKeyedAnimation {
public:
    explicit KeyedListProbe(CKContext *context) : RCKKeyedAnimation(context, "Keyed") {}
    void Root(RCK3dEntity *root) { m_RootEntity = root; }
    void RootVector(const VxVector &value) { m_Vector = value; }
    void EmptySlot() { m_Animations.PushBack(nullptr); }
};

class ListChildProbe : public RCKObjectAnimation {
public:
    explicit ListChildProbe(CKContext *context) : RCKObjectAnimation(context, "Child") {}
    RCKKeyedAnimation *Parent() const { return m_ParentKeyedAnimation; }
};

class KeyedAnimationListTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void BindPosition(ListChildProbe &child, RCK3dEntity &entity, float x) {
        child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
        VxVector position(x, 0, 0); child.AddPositionKey(0, &position);
    }
    static void Pose(ListChildProbe &child, RCK3dEntity &entity, float expected) {
        ASSERT_EQ(CK_OK, child.SetFrame(0)); EXPECT_FLOAT_EQ(expected, entity.GetLocalMatrix()[3][0]);
    }
};

TEST_F(KeyedAnimationListTest, AddPreservesOrderedDuplicatesAndInvalidatesSorting) {
    ListChildProbe first(Context), second(Context); KeyedListProbe keyed(Context);
    keyed.SetFlags(CKANIMATION_SUBANIMSSORTED | CKANIMATION_LINKTOFRAMERATE);
    keyed.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first)); ASSERT_EQ(CK_OK, keyed.AddAnimation(&second));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first));
    ASSERT_EQ(3, keyed.GetAnimationCount()); EXPECT_EQ(&first, keyed.GetAnimation(0));
    EXPECT_EQ(&second, keyed.GetAnimation(1)); EXPECT_EQ(&first, keyed.GetAnimation(2));
    EXPECT_EQ(&keyed, first.Parent()); EXPECT_EQ(&keyed, second.Parent());
    EXPECT_EQ(0u, keyed.GetFlags() & CKANIMATION_SUBANIMSSORTED);
    EXPECT_NE(0u, keyed.GetFlags() & CKANIMATION_LINKTOFRAMERATE);
    EXPECT_EQ(0u, keyed.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(KeyedAnimationListTest, NullAddAndRemovePreserveListParentFlagsAndCache) {
    RCK3dEntity entity(Context); ListChildProbe child(Context); KeyedListProbe keyed(Context);
    BindPosition(child, entity, 3); keyed.AddAnimation(&child); keyed.Root(&entity);
    ASSERT_EQ(&child, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    keyed.SetFlags(CKANIMATION_SUBANIMSSORTED); keyed.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, keyed.AddAnimation(nullptr));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, keyed.RemoveAnimation(nullptr));
    EXPECT_EQ(1, keyed.GetAnimationCount()); EXPECT_EQ(&keyed, child.Parent());
    EXPECT_EQ(&child, keyed.GetRootAnimationInternal());
    EXPECT_NE(0u, keyed.GetFlags() & CKANIMATION_SUBANIMSSORTED);
    EXPECT_NE(0u, keyed.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(KeyedAnimationListTest, RemovingOneDuplicateKeepsParentCacheAndCenteredPose) {
    RCK3dEntity entity(Context); ListChildProbe child(Context); KeyedListProbe keyed(Context);
    BindPosition(child, entity, 3); keyed.AddAnimation(&child); keyed.AddAnimation(&child);
    keyed.Root(&entity); keyed.RootVector(VxVector(4, 0, 0));
    ASSERT_EQ(&child, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    ASSERT_EQ(CK_OK, keyed.RemoveAnimation(&child)); EXPECT_EQ(1, keyed.GetAnimationCount());
    EXPECT_EQ(&keyed, child.Parent()); EXPECT_EQ(&child, keyed.GetRootAnimationInternal()); Pose(child, entity, 7);
}

TEST_F(KeyedAnimationListTest, RemovingLastCachedChildDetachesAndFindsNextRootChannel) {
    RCK3dEntity entity(Context); ListChildProbe oldChild(Context), next(Context); KeyedListProbe keyed(Context);
    BindPosition(oldChild, entity, 3); BindPosition(next, entity, 9);
    keyed.AddAnimation(&oldChild); keyed.AddAnimation(&next); keyed.Root(&entity); keyed.RootVector(VxVector(4, 0, 0));
    ASSERT_EQ(&oldChild, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    ASSERT_EQ(CK_OK, keyed.RemoveAnimation(&oldChild)); EXPECT_EQ(nullptr, oldChild.Parent());
    EXPECT_EQ(nullptr, keyed.GetRootAnimationInternal());
    ASSERT_EQ(&next, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    Pose(next, entity, 13); Pose(oldChild, entity, 3);
}

TEST_F(KeyedAnimationListTest, RemoveAbsentReturnsSuccessWithoutDetachingAnotherParent) {
    ListChildProbe retained(Context), absent(Context); KeyedListProbe keyed(Context), other(Context);
    keyed.AddAnimation(&retained); other.AddAnimation(&absent);
    keyed.SetFlags(CKANIMATION_SUBANIMSSORTED); keyed.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    EXPECT_EQ(CK_OK, keyed.RemoveAnimation(&absent)); EXPECT_EQ(&other, absent.Parent());
    EXPECT_EQ(1, keyed.GetAnimationCount()); EXPECT_EQ(&retained, keyed.GetAnimation(0));
    EXPECT_EQ(0u, keyed.GetFlags() & CKANIMATION_SUBANIMSSORTED);
    EXPECT_EQ(0u, keyed.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(KeyedAnimationListTest, RemovingMemberDoesNotDetachItsNewerParent) {
    ListChildProbe child(Context); KeyedListProbe keyed(Context), other(Context);
    keyed.AddAnimation(&child); other.AddAnimation(&child);
    ASSERT_EQ(CK_OK, keyed.RemoveAnimation(&child)); EXPECT_EQ(0, keyed.GetAnimationCount());
    EXPECT_EQ(&other, child.Parent()); EXPECT_EQ(1, other.GetAnimationCount());
}

TEST_F(KeyedAnimationListTest, RootLookupRejectsCacheWhoseChildWasReboundToAnotherEntity) {
    RCK3dEntity root(Context), other(Context); ListChildProbe cached(Context), current(Context); KeyedListProbe keyed(Context);
    BindPosition(cached, root, 3); BindPosition(current, root, 9);
    keyed.AddAnimation(&cached); keyed.AddAnimation(&current); keyed.Root(&root); keyed.RootVector(VxVector(4, 0, 0));
    ASSERT_EQ(&cached, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    cached.Set3dEntity(reinterpret_cast<CK3dEntity *>(&other));
    ASSERT_EQ(&current, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    EXPECT_EQ(&cached, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&other))); Pose(current, root, 13);
}

TEST_F(KeyedAnimationListTest, IndexedAndEntityLookupRespectOrderAndEmptySlots) {
    RCK3dEntity entity(Context), missing(Context); ListChildProbe first(Context), second(Context); KeyedListProbe keyed(Context);
    BindPosition(first, entity, 1); BindPosition(second, entity, 2);
    keyed.EmptySlot(); keyed.AddAnimation(&first); keyed.AddAnimation(&second);
    EXPECT_EQ(3, keyed.GetAnimationCount()); EXPECT_EQ(nullptr, keyed.GetAnimation(0));
    EXPECT_EQ(&first, keyed.GetAnimation(1)); EXPECT_EQ(&second, keyed.GetAnimation(2));
    EXPECT_EQ(&first, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    EXPECT_EQ(nullptr, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&missing)));
}

// The frozen-before runner excludes this one test: its unchecked index routine
// has undefined behavior. Final verification calls the actual API at boundaries.
TEST_F(KeyedAnimationListTest, InvalidIndicesReturnNull) {
    KeyedListProbe keyed(Context); ListChildProbe child(Context);
    EXPECT_EQ(nullptr, keyed.GetAnimation(0)); EXPECT_EQ(nullptr, keyed.GetAnimation(-1));
    keyed.AddAnimation(&child); EXPECT_EQ(&child, keyed.GetAnimation(0));
    EXPECT_EQ(nullptr, keyed.GetAnimation(-1)); EXPECT_EQ(nullptr, keyed.GetAnimation(1));
    EXPECT_EQ(nullptr, keyed.GetAnimation(INT_MIN)); EXPECT_EQ(nullptr, keyed.GetAnimation(INT_MAX));
}

TEST_F(KeyedAnimationListTest, ClearRemovesMembershipPreservesChildKeysAndResetsOldCentering) {
    RCK3dEntity entity(Context); ListChildProbe oldChild(Context), next(Context); KeyedListProbe keyed(Context);
    BindPosition(oldChild, entity, 3); BindPosition(next, entity, 9);
    keyed.AddAnimation(&oldChild); keyed.Root(&entity); keyed.RootVector(VxVector(-3, 0, 0));
    ASSERT_EQ(&oldChild, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity))); Pose(oldChild, entity, 0);
    keyed.Clear(); EXPECT_EQ(0, keyed.GetAnimationCount()); EXPECT_EQ(nullptr, oldChild.Parent());
    EXPECT_EQ(nullptr, keyed.GetRootAnimationInternal()); EXPECT_FLOAT_EQ(0, keyed.GetRootVectorInternal().x);
    EXPECT_EQ(nullptr, keyed.GetRootEntity());
    VxVector position; ASSERT_TRUE(oldChild.EvaluatePosition(0, position)); EXPECT_FLOAT_EQ(3, position.x);
    keyed.AddAnimation(&next); ASSERT_EQ(reinterpret_cast<CK3dEntity *>(&entity), keyed.GetRootEntity());
    ASSERT_EQ(&next, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity))); Pose(next, entity, 9);
}

TEST_F(KeyedAnimationListTest, ClearAcceptsEmptySlotsAndPreservesChildrenOwnedByAnotherParent) {
    ListChildProbe owned(Context), shared(Context); KeyedListProbe keyed(Context), other(Context);
    keyed.EmptySlot(); keyed.AddAnimation(&owned); keyed.AddAnimation(&shared); other.AddAnimation(&shared);
    keyed.Clear(); EXPECT_EQ(0, keyed.GetAnimationCount()); EXPECT_EQ(nullptr, owned.Parent());
    EXPECT_EQ(&other, shared.Parent()); EXPECT_EQ(1, other.GetAnimationCount());
}

TEST_F(KeyedAnimationListTest, KeyedHeapDestructionDetachesLiveChildWithoutClearingItsController) {
    RCK3dEntity entity(Context); ListChildProbe *child = new ListChildProbe(Context);
    KeyedListProbe *keyed = new KeyedListProbe(Context); BindPosition(*child, entity, 9);
    keyed->AddAnimation(child); keyed->Root(&entity); keyed->RootVector(VxVector(4, 0, 0));
    ASSERT_EQ(child, keyed->GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    delete keyed;
    ASSERT_EQ(nullptr, child->Parent()); // Do not dereference a dangling parent on the before runtime.
    Pose(*child, entity, 9); delete child;
}

TEST_F(KeyedAnimationListTest, ChildHeapDestructionBeforeKeyedOnlyDetachesLiveMembers) {
    KeyedListProbe *keyed = new KeyedListProbe(Context);
    ListChildProbe *dead = new ListChildProbe(Context); ListChildProbe live(Context);
    keyed->AddAnimation(dead); keyed->AddAnimation(&live); delete dead;
    keyed->Clear(); EXPECT_EQ(0, keyed->GetAnimationCount()); EXPECT_EQ(nullptr, live.Parent()); delete keyed;
}

#ifdef _WIN32
TEST_F(KeyedAnimationListTest, DestructorDoesNotReadAnUnregisteredChildInAnInaccessiblePage) {
    void *memory = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ASSERT_NE(nullptr, memory); KeyedListProbe *keyed = new KeyedListProbe(Context);
    ListChildProbe *child = new(memory) ListChildProbe(Context); keyed->AddAnimation(child);
    child->~ListChildProbe(); DWORD oldProtection = 0;
    ASSERT_TRUE(VirtualProtect(memory, 4096, PAGE_NOACCESS, &oldProtection));
    delete keyed; // An unguarded detach would read a page that cannot be accessed.
    EXPECT_TRUE(VirtualFree(memory, 0, MEM_RELEASE));
}
#endif

TEST_F(KeyedAnimationListTest, UpdateRootSkipsEmptyAndUnboundSlotsBeforeWalkingCharacterHierarchy) {
    RCKCharacter character(Context); RCK3dEntity root(Context), leaf(Context);
    ASSERT_TRUE(root.SetParent(reinterpret_cast<CK3dEntity *>(&character), FALSE));
    ASSERT_TRUE(leaf.SetParent(reinterpret_cast<CK3dEntity *>(&root), FALSE));
    ListChildProbe unbound(Context), bound(Context); KeyedListProbe keyed(Context);
    BindPosition(bound, leaf, 9); keyed.EmptySlot(); keyed.AddAnimation(&unbound); keyed.AddAnimation(&bound);
    keyed.UpdateRootEntity(); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), keyed.GetRootEntity());
    EXPECT_EQ(nullptr, keyed.GetRootAnimationInternal());
}

TEST_F(KeyedAnimationListTest, UpdateRootFindsStandaloneTopAncestorAndClearsStaleCache) {
    RCK3dEntity root(Context), leaf(Context); ListChildProbe child(Context); KeyedListProbe keyed(Context);
    ASSERT_TRUE(leaf.SetParent(reinterpret_cast<CK3dEntity *>(&root), FALSE));
    BindPosition(child, leaf, 3); keyed.AddAnimation(&child);
    keyed.Root(&leaf);
    ASSERT_EQ(&child, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&leaf)));
    keyed.UpdateRootEntity(); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), keyed.GetRootEntity());
    EXPECT_EQ(nullptr, keyed.GetRootAnimationInternal());
    EXPECT_NE(0u, keyed.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(KeyedAnimationListTest, SetParentHelperAssignsAndClearsRelationWithoutChangingMembership) {
    ListChildProbe child(Context); KeyedListProbe first(Context), second(Context);
    RCKKeyedAnimation::SetParentKeyedAnimation(&child, &first); EXPECT_EQ(&first, child.Parent());
    RCKKeyedAnimation::SetParentKeyedAnimation(&child, &second); EXPECT_EQ(&second, child.Parent());
    RCKKeyedAnimation::SetParentKeyedAnimation(&child, nullptr); EXPECT_EQ(nullptr, child.Parent());
    EXPECT_EQ(0, first.GetAnimationCount()); EXPECT_EQ(0, second.GetAnimationCount());
}

TEST_F(KeyedAnimationListTest, IndependentSnapshotLoadMarksRecomputedRootUpToDate) {
    RCK3dEntity entity(Context); ListChildProbe child(Context); KeyedListProbe keyed(Context);
    BindPosition(child, entity, 9);
    CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr); chunk.StartWrite();
    chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST);
    chunk.StartObjectIDSequence(1); chunk.WriteObjectSequence(&child); chunk.CloseChunk();
    ASSERT_EQ(CK_OK, keyed.Load(&chunk, nullptr));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&entity), keyed.GetRootEntity());
    EXPECT_TRUE(keyed.IsUpToDate());
    EXPECT_EQ(&keyed, child.Parent());
}

} // namespace
