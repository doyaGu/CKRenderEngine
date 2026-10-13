#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class KeyedProbe : public RCKKeyedAnimation {
public:
    explicit KeyedProbe(CKContext *context) : RCKKeyedAnimation(context, "Keyed") {}
    void SetRoot(RCK3dEntity *root) { m_RootEntity = root; }
    void SetRootVector(const VxVector &value) { m_Vector = value; }
    void AppendEmptySlot() { m_Animations.PushBack(nullptr); }
};

class ObjectProbe : public RCKObjectAnimation {
public:
    explicit ObjectProbe(CKContext *context) : RCKObjectAnimation(context, "Child") {}
    RCKKeyedAnimation *Parent() const { return m_ParentKeyedAnimation; }
};

class KeyedAnimationLoadTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void List(CKStateChunk &chunk, CKObject *first, CKObject *second = nullptr) {
        chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST);
        chunk.StartObjectIDSequence(second ? 2 : 1);
        chunk.WriteObjectSequence(first);
        if (second) chunk.WriteObjectSequence(second);
    }
    static void PositionChunk(CKStateChunk &chunk, RCK3dEntity *entity, const VxVector &position,
                              const VxVector *root = nullptr) {
        chunk.StartWrite();
        chunk.SetDataVersion(0);
        chunk.WriteIdentifier(CK_STATESAVE_OBJANIMENTITY);
        chunk.WriteObject(entity);
        if (root) {
            chunk.WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
            chunk.WriteFloat(root->x); chunk.WriteFloat(root->y); chunk.WriteFloat(root->z);
        }
        chunk.WriteIdentifier(CK_STATESAVE_OBJANIMPOSKEYS);
        float key[4] = {5.0f, position.x, position.y, position.z};
        chunk.WriteDword(sizeof(key)); chunk.WriteDword(1);
        chunk.WriteBufferNoSize_LEndian(sizeof(key), key);
        chunk.CloseChunk();
    }
    static CKERROR Load(KeyedProbe &animation, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk();
        return animation.Load(&chunk, file);
    }
    static void ExpectPosition(ObjectProbe &animation, const VxVector &expected) {
        VxVector actual;
        ASSERT_TRUE(animation.EvaluatePosition(5.0f, actual));
        EXPECT_FLOAT_EQ(expected.x, actual.x);
        EXPECT_FLOAT_EQ(expected.y, actual.y);
        EXPECT_FLOAT_EQ(expected.z, actual.z);
    }
};

TEST_F(KeyedAnimationLoadTest, NullChunkPreservesListParentAndRootCache) {
    RCK3dEntity entity(Context, "Root"); ObjectProbe child(Context); KeyedProbe animation(Context);
    child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    animation.AddAnimation(&child); animation.SetRoot(&entity);
    ASSERT_EQ(&child, animation.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, animation.Load(nullptr, nullptr));
    EXPECT_EQ(1, animation.GetAnimationCount()); EXPECT_EQ(&animation, child.Parent());
    EXPECT_EQ(&child, animation.GetRootAnimationInternal());
}

TEST_F(KeyedAnimationLoadTest, FileListReplacementInvalidatesCachedRootAndDetachesRemovedChild) {
    RCK3dEntity entity(Context, "Root"); ObjectProbe oldChild(Context), newChild(Context); KeyedProbe animation(Context);
    oldChild.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    newChild.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    VxVector oldPosition(1, 2, 3), newPosition(7, 8, 9);
    oldChild.AddPositionKey(5, &oldPosition); newChild.AddPositionKey(5, &newPosition);
    animation.AddAnimation(&oldChild); animation.SetRoot(&entity); animation.SetRootVector(VxVector(4, -2, 1));
    ASSERT_EQ(&oldChild, animation.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr); chunk.StartWrite(); List(chunk, &newChild);
    CKFile file(Context); ASSERT_EQ(CK_OK, Load(animation, chunk, &file));
    ASSERT_EQ(1, animation.GetAnimationCount()); EXPECT_EQ(&newChild, animation.GetAnimation(0));
    EXPECT_EQ(nullptr, oldChild.Parent()); EXPECT_EQ(&animation, newChild.Parent());
    CKObjectAnimation *root = animation.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity));
    ASSERT_EQ(&newChild, root);
    VxVector actual; ASSERT_TRUE(root->EvaluatePosition(5, actual)); EXPECT_FLOAT_EQ(7, actual.x);
    ASSERT_EQ(CK_OK, root->SetFrame(5, reinterpret_cast<CKKeyedAnimation *>(&animation)));
    EXPECT_FLOAT_EQ(11, entity.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(6, entity.GetLocalMatrix()[3][1]);
    EXPECT_FLOAT_EQ(10, entity.GetLocalMatrix()[3][2]);
}

TEST_F(KeyedAnimationLoadTest, ReplacingListPreservesRemovedChildsOtherParent) {
    ObjectProbe removed(Context), retained(Context); KeyedProbe animation(Context), other(Context);
    animation.AddAnimation(&removed); animation.AddAnimation(&retained); other.AddAnimation(&removed);
    CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr); chunk.StartWrite(); List(chunk, &retained);
    CKFile file(Context); ASSERT_EQ(CK_OK, Load(animation, chunk, &file));
    EXPECT_EQ(&other, removed.Parent()); EXPECT_EQ(&animation, retained.Parent());
    EXPECT_EQ(1, other.GetAnimationCount());
}

TEST_F(KeyedAnimationLoadTest, ListReplacementAcceptsExistingEmptySnapshotSlot) {
    ObjectProbe oldChild(Context), newChild(Context); KeyedProbe animation(Context);
    animation.AddAnimation(&oldChild); animation.AppendEmptySlot();
    CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr); chunk.StartWrite(); List(chunk, &newChild);
    CKFile file(Context); ASSERT_EQ(CK_OK, Load(animation, chunk, &file));
    EXPECT_EQ(1, animation.GetAnimationCount()); EXPECT_EQ(&newChild, animation.GetAnimation(0));
    EXPECT_EQ(nullptr, oldChild.Parent()); EXPECT_EQ(&animation, newChild.Parent());
}

TEST_F(KeyedAnimationLoadTest, FileRootReplacementInvalidatesCacheWithoutReplacingChildren) {
    RCK3dEntity first(Context, "First"), second(Context, "Second");
    ObjectProbe firstChild(Context), secondChild(Context); KeyedProbe animation(Context);
    firstChild.Set3dEntity(reinterpret_cast<CK3dEntity *>(&first));
    secondChild.Set3dEntity(reinterpret_cast<CK3dEntity *>(&second));
    animation.AddAnimation(&firstChild); animation.AddAnimation(&secondChild); animation.SetRoot(&first);
    ASSERT_EQ(&firstChild, animation.GetAnimation(reinterpret_cast<CK3dEntity *>(&first)));
    CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr); chunk.StartWrite();
    chunk.WriteIdentifier(CK_STATESAVE_ANIMATIONBODYPARTS);
    chunk.WriteDword(0); chunk.WriteObject(&second);
    CKFile file(Context); ASSERT_EQ(CK_OK, Load(animation, chunk, &file));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&second), animation.GetRootEntity());
    EXPECT_EQ(&secondChild, animation.GetAnimation(reinterpret_cast<CK3dEntity *>(&second)));
    EXPECT_EQ(2, animation.GetAnimationCount());
}

TEST_F(KeyedAnimationLoadTest, IndependentEmbeddedStateRestoresPoseParentAndLegacyRootTransfer) {
    RCK3dEntity entity(Context, "Root"); ObjectProbe child(Context); KeyedProbe animation(Context);
    CKStateChunk sub(CKCID_OBJECTANIMATION, nullptr), chunk(CKCID_KEYEDANIMATION, nullptr);
    const VxVector position(7, 8, 9), root(2, -3, 4);
    PositionChunk(sub, &entity, position, &root); chunk.StartWrite(); List(chunk, &child);
    chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS);
    chunk.WriteDword(1); chunk.WriteObject(&child); chunk.WriteSubChunk(&sub);
    animation.SetFlags(CKANIMATION_SUBANIMSSORTED);
    ASSERT_EQ(CK_OK, Load(animation, chunk)); ExpectPosition(child, position);
    EXPECT_EQ(&animation, child.Parent()); EXPECT_EQ(nullptr, child.GetAppData());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&entity), animation.GetRootEntity());
    EXPECT_FLOAT_EQ(2, animation.GetRootVectorInternal().x);
    EXPECT_FLOAT_EQ(-3, animation.GetRootVectorInternal().y);
    EXPECT_FLOAT_EQ(4, animation.GetRootVectorInternal().z);
    EXPECT_EQ(0u, animation.GetFlags() & CKANIMATION_SUBANIMSSORTED);
}

TEST_F(KeyedAnimationLoadTest, MissingObjectConsumesEmbeddedFrameAndRestoresFollowingChild) {
    RCK3dEntity entity(Context, "Root"); ObjectProbe child(Context); KeyedProbe animation(Context);
    CKStateChunk sub(CKCID_OBJECTANIMATION, nullptr), chunk(CKCID_KEYEDANIMATION, nullptr);
    PositionChunk(sub, &entity, VxVector(11, 12, 13)); chunk.StartWrite(); List(chunk, &child);
    chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS); chunk.WriteDword(2);
    chunk.WriteObject(nullptr); chunk.WriteSubChunk(&sub);
    chunk.WriteObject(&child); chunk.WriteSubChunk(&sub);
    ASSERT_EQ(CK_OK, Load(animation, chunk)); ExpectPosition(child, VxVector(11, 12, 13));
}

TEST_F(KeyedAnimationLoadTest, NestedLoadFailureIsReportedAfterConsumingLaterFramesAndRepairingParents) {
    RCK3dEntity entity(Context, "Root"); ObjectProbe first(Context), second(Context); KeyedProbe animation(Context);
    CKStateChunk sub(CKCID_OBJECTANIMATION, nullptr), chunk(CKCID_KEYEDANIMATION, nullptr);
    PositionChunk(sub, &entity, VxVector(14, 15, 16)); chunk.StartWrite(); List(chunk, &first, &second);
    chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS); chunk.WriteDword(2);
    chunk.WriteObject(&first); chunk.WriteSubChunk(nullptr);
    chunk.WriteObject(&second); chunk.WriteSubChunk(&sub);
    animation.SetFlags(CKANIMATION_SUBANIMSSORTED);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Load(animation, chunk));
    ExpectPosition(second, VxVector(14, 15, 16));
    EXPECT_EQ(&animation, first.Parent()); EXPECT_EQ(&animation, second.Parent());
    EXPECT_EQ(0u, animation.GetFlags() & CKANIMATION_SUBANIMSSORTED);
}

TEST_F(KeyedAnimationLoadTest, FileLoadSkipsEmbeddedStateAndKeepsDeclaredRoot) {
    RCK3dEntity entity(Context, "Root"), other(Context, "Other"); ObjectProbe child(Context); KeyedProbe animation(Context);
    VxVector oldPosition(21, 22, 23); child.AddPositionKey(5, &oldPosition);
    CKStateChunk sub(CKCID_OBJECTANIMATION, nullptr), chunk(CKCID_KEYEDANIMATION, nullptr);
    PositionChunk(sub, &other, VxVector(1, 2, 3)); chunk.StartWrite(); List(chunk, &child);
    chunk.WriteIdentifier(CK_STATESAVE_ANIMATIONBODYPARTS); chunk.WriteDword(0); chunk.WriteObject(&entity);
    chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS); chunk.WriteDword(1);
    chunk.WriteObject(&child); chunk.WriteSubChunk(&sub);
    CKFile file(Context); ASSERT_EQ(CK_OK, Load(animation, chunk, &file));
    ExpectPosition(child, oldPosition); EXPECT_EQ(nullptr, child.Get3dEntity());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&entity), animation.GetRootEntity()); EXPECT_EQ(&animation, child.Parent());
}

TEST_F(KeyedAnimationLoadTest, SelectiveMergeRetainsChildrenAndConsumesTransfersInListOrder) {
    RCK3dEntity entity(Context, "Root"); ObjectProbe first(Context), second(Context); KeyedProbe animation(Context);
    first.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    VxVector position(1, 2, 3); first.AddPositionKey(5, &position);
    animation.AddAnimation(&first); animation.AddAnimation(&second);
    animation.SetRoot(&entity);
    ASSERT_EQ(&first, animation.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)));
    first.SetAppData(new VxVector(1, 2, 3)); second.SetAppData(new VxVector(4, 5, 6));
    CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr); chunk.StartWrite();
    chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMMERGE); chunk.WriteInt(TRUE); chunk.WriteFloat(0.25f);
    CKFile file(Context); ASSERT_EQ(CK_OK, Load(animation, chunk, &file));
    EXPECT_EQ(2, animation.GetAnimationCount()); EXPECT_EQ(&animation, first.Parent()); EXPECT_EQ(&animation, second.Parent());
    EXPECT_TRUE(animation.IsMerged()); EXPECT_FLOAT_EQ(0.25f, animation.GetMergeFactor());
    EXPECT_FLOAT_EQ(0.5f, first.GetMergeFactor()); EXPECT_FLOAT_EQ(0.5f, second.GetMergeFactor());
    EXPECT_EQ(nullptr, first.GetAppData()); EXPECT_EQ(nullptr, second.GetAppData());
    EXPECT_FLOAT_EQ(4, animation.GetRootVectorInternal().x); EXPECT_FLOAT_EQ(5, animation.GetRootVectorInternal().y);
    EXPECT_FLOAT_EQ(6, animation.GetRootVectorInternal().z);
    EXPECT_EQ(&first, animation.GetRootAnimationInternal());
    ASSERT_EQ(CK_OK, first.SetFrame(5, reinterpret_cast<CKKeyedAnimation *>(&animation)));
    EXPECT_FLOAT_EQ(5, entity.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(7, entity.GetLocalMatrix()[3][1]);
    EXPECT_FLOAT_EQ(9, entity.GetLocalMatrix()[3][2]);
}

} // namespace
