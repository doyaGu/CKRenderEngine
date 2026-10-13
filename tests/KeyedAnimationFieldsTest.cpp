#include <gtest/gtest.h>
#include <cstdio>
#include <cstddef>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class KeyedFieldsProbe : public RCKKeyedAnimation {
public:
    explicit KeyedFieldsProbe(CKContext *context) : RCKKeyedAnimation(context) {}
    XSObjectPointerArray &List() { return m_Animations; }
    void Character(RCKCharacter *character) { m_Character = character; }
    void Root(RCK3dEntity *root) { m_RootEntity = root; }
    CKBOOL &Merged() { return m_Merged; }
    float &Factor() { return m_MergeFactor; }
    RCKObjectAnimation *&Cache() { return m_RootAnimation; }
    VxVector &Vector() { return m_Vector; }
    void PrintLayout() {
        const char *base = reinterpret_cast<const char *>(static_cast<RCKKeyedAnimation *>(this));
        std::printf("KEYED_LAYOUT pointer=%zu size=%zu align=%zu array_size=%zu array_align=%zu "
                    "m_Animations=%td m_Merged=%td m_MergeFactor=%td m_RootAnimation=%td m_Vector=%td "
                    "class_id_size=%zu class_id_align=%zu class_id_static=1\n",
                    sizeof(void *), sizeof(RCKKeyedAnimation), alignof(RCKKeyedAnimation),
                    sizeof(XSObjectPointerArray), alignof(XSObjectPointerArray),
                    reinterpret_cast<const char *>(&m_Animations) - base,
                    reinterpret_cast<const char *>(&m_Merged) - base,
                    reinterpret_cast<const char *>(&m_MergeFactor) - base,
                    reinterpret_cast<const char *>(&m_RootAnimation) - base,
                    reinterpret_cast<const char *>(&m_Vector) - base,
                    sizeof(m_ClassID), alignof(CK_CLASSID));
    }
};

class ChildFieldsProbe : public RCKObjectAnimation {
public:
    explicit ChildFieldsProbe(CKContext *context) : RCKObjectAnimation(context) {}
    RCKKeyedAnimation *Parent() { return m_ParentKeyedAnimation; }
};

class KeyedAnimationFieldsTest : public ::testing::Test {
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
    static void Curve(RCKObjectAnimation &child, RCK3dEntity &entity, float start, float end) {
        child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
        VxVector a(start, 0, 0), b(end, 0, 0);
        child.AddPositionKey(0, &a); child.AddPositionKey(10, &b); child.SetLength(10);
    }
    static void WriteList(CKStateChunk &chunk, CKObject *first, CKObject *second = nullptr) {
        chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST);
        chunk.StartObjectIDSequence(second ? 2 : 1);
        chunk.WriteObjectSequence(first); if (second) chunk.WriteObjectSequence(second);
    }
};

TEST_F(KeyedAnimationFieldsTest, LayoutReportsActualPortableStorageAndConstructorDefaults) {
    KeyedFieldsProbe group(Context); group.PrintLayout();
    EXPECT_EQ(2 * sizeof(void *), sizeof(XSObjectPointerArray));
    EXPECT_EQ(alignof(void *), alignof(XSObjectPointerArray));
    EXPECT_EQ(0, group.List().Size()); EXPECT_EQ(FALSE, group.Merged());
    EXPECT_FLOAT_EQ(.5f, group.Factor()); EXPECT_EQ(nullptr, group.Cache());
    EXPECT_FLOAT_EQ(0, group.Vector().x); EXPECT_FLOAT_EQ(0, group.Vector().y); EXPECT_FLOAT_EQ(0, group.Vector().z);
}

TEST_F(KeyedAnimationFieldsTest, RawListOrderControlsSavedReferencesAndDurationConsumers) {
    RCK3dEntity a(Context), b(Context); ChildFieldsProbe x(Context), y(Context); KeyedFieldsProbe group(Context);
    Curve(x, a, 0, 10); Curve(y, b, 0, 20); group.AddAnimation(&x); group.AddAnimation(&y);
    group.List()[0] = &y; group.List()[1] = &x;
    CKStateChunk *saved = group.Save(nullptr, CK_STATESAVE_KEYEDANIMANIMLIST); ASSERT_NE(nullptr, saved);
    saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST));
    EXPECT_EQ(2, saved->StartReadSequence()); EXPECT_EQ(&y, saved->ReadObject(Context));
    EXPECT_EQ(&x, saved->ReadObject(Context)); DeleteCKStateChunk(saved);
    group.SetLength(30); EXPECT_FLOAT_EQ(30, x.GetLength()); EXPECT_FLOAT_EQ(30, y.GetLength());
    EXPECT_EQ(&group, x.Parent()); EXPECT_EQ(&group, y.Parent());
    VxVector value; ASSERT_TRUE(x.EvaluatePosition(5, value)); EXPECT_FLOAT_EQ(5, value.x);
}

TEST_F(KeyedAnimationFieldsTest, DuplicateMembershipDetachesOnlyAfterFinalRemovalAndPreservesCurve) {
    RCK3dEntity entity(Context); ChildFieldsProbe child(Context); KeyedFieldsProbe group(Context);
    Curve(child, entity, 0, 10); group.AddAnimation(&child); group.AddAnimation(&child);
    group.Root(&entity); group.CenterAnimation(3); ASSERT_EQ(&child, group.Cache());
    group.RemoveAnimation(&child); EXPECT_EQ(1, group.List().Size()); EXPECT_EQ(&group, child.Parent());
    EXPECT_EQ(&child, group.Cache()); ASSERT_EQ(CK_OK, child.SetFrame(5));
    EXPECT_FLOAT_EQ(2, entity.GetLocalMatrix()[3][0]);
    group.RemoveAnimation(&child); EXPECT_EQ(0, group.List().Size()); EXPECT_EQ(nullptr, child.Parent());
    EXPECT_EQ(nullptr, group.Cache()); ASSERT_EQ(CK_OK, child.SetFrame(5));
    EXPECT_FLOAT_EQ(5, entity.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationFieldsTest, RawMergedFlagAndFactorAreSerializedAsSeparateValues) {
    KeyedFieldsProbe group(Context); group.Merged() = 7; group.Factor() = .375f;
    CKStateChunk *saved = group.Save(nullptr, CK_STATESAVE_KEYEDANIMMERGE); ASSERT_NE(nullptr, saved);
    saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_KEYEDANIMMERGE));
    EXPECT_EQ(7, saved->ReadInt()); EXPECT_FLOAT_EQ(.375f, saved->ReadFloat()); DeleteCKStateChunk(saved);
    // A snapshot's nonzero CKBOOL is retained; it is not encoded into animation flags.
    EXPECT_EQ(7, group.IsMerged()); EXPECT_EQ(0u, group.GetFlags() & CKANIMATION_SUBANIMSSORTED);
}

TEST_F(KeyedAnimationFieldsTest, IndependentMergePayloadUpdatesRawFieldsAndAbsentPayloadPreservesThem) {
    KeyedFieldsProbe group(Context); CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr);
    chunk.StartWrite(); chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMMERGE);
    chunk.WriteInt(3); chunk.WriteFloat(.625f); chunk.CloseChunk();
    CKFile file(Context); ASSERT_EQ(CK_OK, group.Load(&chunk, &file));
    EXPECT_EQ(3, group.Merged()); EXPECT_FLOAT_EQ(.625f, group.Factor());
    CKStateChunk omitted(CKCID_KEYEDANIMATION, nullptr); omitted.StartWrite(); omitted.CloseChunk();
    ASSERT_EQ(CK_OK, group.Load(&omitted, &file));
    EXPECT_EQ(3, group.Merged()); EXPECT_FLOAT_EQ(.625f, group.Factor());
}

TEST_F(KeyedAnimationFieldsTest, MergeFactorPropagatesIntoLiveBlendedCurveAndSourceEditsRemainVisible) {
    RCKCharacter character(Context); RCK3dEntity entity(Context); ChildFieldsProbe a(Context), b(Context);
    KeyedFieldsProbe first(Context), second(Context); first.Character(&character); second.Character(&character);
    Curve(a, entity, 0, 10); Curve(b, entity, 20, 40); first.AddAnimation(&a); second.AddAnimation(&b);
    first.SetLength(10); second.SetLength(10);
    RCKKeyedAnimation *blend = static_cast<RCKKeyedAnimation *>(first.CreateMergedAnimation(&second, TRUE));
    ASSERT_NE(nullptr, blend); ASSERT_TRUE(blend->IsMerged()); ASSERT_EQ(1, blend->GetAnimationCount());
    CKObjectAnimation *channel = blend->GetAnimation(0); ASSERT_TRUE(channel->IsMerged());
    blend->SetMergeFactor(.25f); VxVector position; ASSERT_TRUE(channel->EvaluatePosition(5, position));
    EXPECT_FLOAT_EQ(11.25f, position.x);
    VxVector changed(60, 0, 0); b.AddPositionKey(10, &changed);
    ASSERT_TRUE(channel->EvaluatePosition(5, position)); EXPECT_FLOAT_EQ(13.75f, position.x);
    blend->SetMergeFactor(.75f); ASSERT_TRUE(channel->EvaluatePosition(5, position));
    EXPECT_FLOAT_EQ(31.25f, position.x); blend->SetStep(.5f); EXPECT_FLOAT_EQ(31.25f, entity.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationFieldsTest, StaleRootCacheIsValidatedAgainstMembershipAndEntityBeforePlayback) {
    RCK3dEntity root(Context), other(Context); ChildFieldsProbe current(Context), outside(Context);
    KeyedFieldsProbe group(Context); Curve(current, root, 0, 10); Curve(outside, root, 100, 200);
    group.AddAnimation(&current); group.Root(&root); group.Cache() = &outside;
    EXPECT_EQ(&current, group.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    EXPECT_EQ(&current, group.Cache()); group.Vector().Set(-2, 0, 0);
    ASSERT_EQ(CK_OK, current.SetFrame(5)); EXPECT_FLOAT_EQ(3, root.GetLocalMatrix()[3][0]);
    current.Set3dEntity(reinterpret_cast<CK3dEntity *>(&other));
    EXPECT_EQ(nullptr, group.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    EXPECT_EQ(nullptr, group.Cache()); ASSERT_EQ(CK_OK, current.SetFrame(5));
    EXPECT_FLOAT_EQ(5, other.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationFieldsTest, RawCenteringVectorChangesRootPoseWithoutChangingKeysOrOtherChannels) {
    RCK3dEntity root(Context), other(Context); ChildFieldsProbe r(Context), o(Context); KeyedFieldsProbe group(Context);
    Curve(r, root, 0, 10); Curve(o, other, 10, 30); group.AddAnimation(&r); group.AddAnimation(&o);
    group.Root(&root); group.Cache() = &r; group.Vector().Set(-2, 3, -4);
    group.SetStep(.5f); EXPECT_FLOAT_EQ(3, root.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(3, root.GetLocalMatrix()[3][1]); EXPECT_FLOAT_EQ(-4, root.GetLocalMatrix()[3][2]);
    EXPECT_FLOAT_EQ(20, other.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(0, other.GetLocalMatrix()[3][1]);
    VxVector raw; ASSERT_TRUE(r.EvaluatePosition(5, raw)); EXPECT_FLOAT_EQ(5, raw.x); EXPECT_FLOAT_EQ(0, raw.y);
    group.Clear(); EXPECT_FLOAT_EQ(0, group.Vector().x); EXPECT_EQ(nullptr, group.Cache());
    ASSERT_EQ(CK_OK, r.SetFrame(5)); EXPECT_FLOAT_EQ(5, root.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationFieldsTest, IndependentLegacyChildPayloadTransfersVectorAndDrivesCenteredPose) {
    RCK3dEntity root(Context); ChildFieldsProbe child(Context); KeyedFieldsProbe group(Context);
    CKStateChunk sub(CKCID_OBJECTANIMATION, nullptr), chunk(CKCID_KEYEDANIMATION, nullptr);
    sub.StartWrite(); sub.SetDataVersion(0);
    sub.WriteIdentifier(CK_STATESAVE_OBJANIMENTITY); sub.WriteObject(&root);
    // Frame playback needs the independently restored child data length;
    // loading the legacy keys alone does not infer a duration from their time.
    sub.WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH); sub.WriteFloat(10);
    sub.WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA); sub.WriteFloat(-2); sub.WriteFloat(3); sub.WriteFloat(-4);
    sub.WriteIdentifier(CK_STATESAVE_OBJANIMPOSKEYS);
    const float key[4] = {5, 7, 8, 9}; sub.WriteDword(sizeof(key)); sub.WriteDword(1);
    sub.WriteBufferNoSize_LEndian(sizeof(key), const_cast<float *>(key)); sub.CloseChunk();
    chunk.StartWrite(); WriteList(chunk, &child);
    chunk.WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS);
    chunk.WriteDword(1); chunk.WriteObject(&child); chunk.WriteSubChunk(&sub); chunk.CloseChunk();
    ASSERT_EQ(CK_OK, group.Load(&chunk, nullptr)); EXPECT_EQ(&group, child.Parent());
    ASSERT_FLOAT_EQ(10, child.GetLength());
    EXPECT_EQ(nullptr, child.GetAppData()); EXPECT_FLOAT_EQ(-2, group.Vector().x);
    EXPECT_FLOAT_EQ(3, group.Vector().y); EXPECT_FLOAT_EQ(-4, group.Vector().z);
    EXPECT_EQ(&child, group.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    ASSERT_EQ(CK_OK, child.SetFrame(5)); EXPECT_FLOAT_EQ(5, root.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(11, root.GetLocalMatrix()[3][1]); EXPECT_FLOAT_EQ(5, root.GetLocalMatrix()[3][2]);
}

TEST_F(KeyedAnimationFieldsTest, ClassIDIsSharedStaticStorageConsumedByVirtualDispatch) {
    KeyedFieldsProbe a(Context), b(Context); EXPECT_EQ(CKCID_KEYEDANIMATION, RCKKeyedAnimation::m_ClassID);
    CKObject *first = &a, *second = &b; CK_CLASSID saved = RCKKeyedAnimation::m_ClassID;
    RCKKeyedAnimation::m_ClassID = CKCID_ANIMATION;
    CK_CLASSID actualFirst = first->GetClassID(), actualSecond = second->GetClassID();
    RCKKeyedAnimation::m_ClassID = saved;
    EXPECT_EQ(CKCID_ANIMATION, actualFirst); EXPECT_EQ(CKCID_ANIMATION, actualSecond);
    EXPECT_EQ(CKCID_KEYEDANIMATION, first->GetClassID()); EXPECT_EQ(CKCID_KEYEDANIMATION, second->GetClassID());
    // Restore before any object teardown or registry operation. There is no instance offset for this member.
}
} // namespace
