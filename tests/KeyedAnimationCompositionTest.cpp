#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class CompositionProbe : public RCKKeyedAnimation {
public:
    explicit CompositionProbe(CKContext *context) : RCKKeyedAnimation(context, "First") {}
    void Character(RCKCharacter *character) { m_Character = character; }
    void Root(RCK3dEntity *root) { m_RootEntity = root; }
    void EmptySlot() { m_Animations.PushBack(nullptr); }
};

class FailingMergeChild : public RCKObjectAnimation {
public:
    explicit FailingMergeChild(CKContext *context) : RCKObjectAnimation(context) {}
    CKObjectAnimation *CreateMergedAnimation(CKObjectAnimation *, CKBOOL = FALSE) override { return nullptr; }
};

class KeyedAnimationCompositionTest : public ::testing::Test {
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
    int LiveObjects() {
        int count = 0;
        for (CK_ID id = 1; id <= static_cast<CK_ID>(Context->GetObjectCount()); ++id)
            if (Context->GetObject(id)) ++count;
        return count;
    }
    static void Curve(RCKObjectAnimation &animation, RCK3dEntity &entity, float duration, float end) {
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
        VxVector first(0, 0, 0), last(end, 0, 0);
        animation.AddPositionKey(0, &first); animation.AddPositionKey(duration, &last);
        animation.SetLength(duration);
    }
    static void Sample(CKObjectAnimation *animation, float frame, float expected) {
        ASSERT_NE(nullptr, animation); VxVector position(999, 999, 999);
        ASSERT_TRUE(animation->EvaluatePosition(frame, position)); EXPECT_FLOAT_EQ(expected, position.x);
    }
    static RCKKeyedAnimation *Merge(CompositionProbe &first, CompositionProbe &second, CKBOOL intersection = FALSE) {
        return static_cast<RCKKeyedAnimation *>(first.CreateMergedAnimation(&second, intersection));
    }
};

TEST_F(KeyedAnimationCompositionTest, DurationDispatchUpdatesChildrenAndControllersWithoutMovingKeys) {
    RCK3dEntity entity(Context); RCKObjectAnimation child(Context); CompositionProbe keyed(Context);
    Curve(child, entity, 10, 10);
    VxVector scale(2, 3, 4); VxQuaternion rotation(0, 0, 0, 1);
    child.AddScaleKey(7, &scale); child.AddRotationKey(7, &rotation); child.AddScaleAxisKey(7, &rotation);
    CKMorphController *morph = static_cast<CKMorphController *>(child.CreateController(CKANIMATION_CONTROLLER_MORPH));
    ASSERT_NE(nullptr, morph); morph->SetMorphVertexCount(1); ASSERT_GE(morph->AddKey(7, FALSE), 0);
    keyed.EmptySlot(); keyed.AddAnimation(&child); keyed.AddAnimation(&child);
    CKAnimation *api = &keyed; api->SetLength(20);
    EXPECT_FLOAT_EQ(20, api->GetLength()); EXPECT_FLOAT_EQ(20, child.GetLength());
    CKAnimController *controllers[] = {child.GetPositionController(), child.GetScaleController(),
        child.GetRotationController(), child.GetScaleAxisController(), child.GetMorphController()};
    for (int i = 0; i < 5; ++i) { ASSERT_NE(nullptr, controllers[i]); EXPECT_FLOAT_EQ(20, controllers[i]->GetLength()); }
    EXPECT_FLOAT_EQ(10, child.GetPositionController()->GetKey(1)->GetTime());
    Sample(&child, 5, 5);
    api->SetLength(0); EXPECT_FLOAT_EQ(0, child.GetLength());
    EXPECT_FLOAT_EQ(10, child.GetPositionController()->GetKey(1)->GetTime());
}

TEST_F(KeyedAnimationCompositionTest, CenterResolvesLazyRootAndAppliesOffsetAtRequestedFrame) {
    RCKCharacter character(Context); RCK3dEntity root(Context); RCKObjectAnimation child(Context); CompositionProbe keyed(Context);
    root.SetParent(reinterpret_cast<CK3dEntity *>(&character)); keyed.Character(&character);
    Curve(child, root, 10, 10); keyed.AddAnimation(&child);
    keyed.CenterAnimation(4);
    EXPECT_EQ(&child, keyed.GetRootAnimationInternal()); EXPECT_FLOAT_EQ(-4, keyed.GetRootVectorInternal().x);
    ASSERT_EQ(CK_OK, child.SetFrame(4)); EXPECT_FLOAT_EQ(0, root.GetLocalMatrix()[3][0]);
    ASSERT_EQ(CK_OK, child.SetFrame(7)); EXPECT_FLOAT_EQ(3, root.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationCompositionTest, CenterRejectsReboundCacheAndUsesCurrentRootChannel) {
    RCK3dEntity root(Context), other(Context); RCKObjectAnimation stale(Context), current(Context); CompositionProbe keyed(Context);
    Curve(stale, root, 10, 10); Curve(current, root, 10, 30);
    keyed.AddAnimation(&stale); keyed.AddAnimation(&current); keyed.Root(&root); keyed.CenterAnimation(2);
    stale.Set3dEntity(reinterpret_cast<CK3dEntity *>(&other)); keyed.CenterAnimation(2);
    EXPECT_EQ(&current, keyed.GetRootAnimationInternal()); EXPECT_FLOAT_EQ(-6, keyed.GetRootVectorInternal().x);
    ASSERT_EQ(CK_OK, current.SetFrame(2)); EXPECT_FLOAT_EQ(0, root.GetLocalMatrix()[3][0]);
    Sample(&stale, 2, 2);
}

TEST_F(KeyedAnimationCompositionTest, CenterWithoutPositionResetsOffsetToZero) {
    RCK3dEntity root(Context); RCKObjectAnimation child(Context); CompositionProbe keyed(Context);
    Curve(child, root, 10, 10); keyed.AddAnimation(&child); keyed.Root(&root); keyed.CenterAnimation(4);
    ASSERT_TRUE(child.DeleteController(CKANIMATION_CONTROLLER_POS)); keyed.CenterAnimation(4);
    EXPECT_FLOAT_EQ(0, keyed.GetRootVectorInternal().x); EXPECT_FLOAT_EQ(0, keyed.GetRootVectorInternal().y);
    EXPECT_FLOAT_EQ(0, keyed.GetRootVectorInternal().z);
}

TEST_F(KeyedAnimationCompositionTest, UnionCopiesBothUnmatchedCurvesAndRescalesTheirTimes) {
    RCKCharacter character(Context); RCK3dEntity leftEntity(Context), rightEntity(Context);
    RCKObjectAnimation left(Context), right(Context); CompositionProbe first(Context), second(Context);
    first.Character(&character); second.Character(&character);
    Curve(left, leftEntity, 10, 10); Curve(right, rightEntity, 20, 40);
    first.AddAnimation(&left); second.AddAnimation(&right); first.SetLength(10); second.SetLength(20);
    RCKKeyedAnimation *merged = Merge(first, second); ASSERT_NE(nullptr, merged);
    ASSERT_EQ(2, merged->GetAnimationCount()); EXPECT_FLOAT_EQ(20, merged->GetLength()); EXPECT_TRUE(merged->IsMerged());
    CKObjectAnimation *leftCopy = merged->GetAnimation(0), *rightCopy = merged->GetAnimation(1);
    EXPECT_NE(&left, leftCopy); EXPECT_NE(&right, rightCopy);
    EXPECT_EQ(left.Get3dEntity(), leftCopy->Get3dEntity()); EXPECT_EQ(right.Get3dEntity(), rightCopy->Get3dEntity());
    EXPECT_FLOAT_EQ(20, leftCopy->GetLength()); EXPECT_FLOAT_EQ(20, rightCopy->GetLength());
    EXPECT_FLOAT_EQ(20, leftCopy->GetPositionController()->GetKey(1)->GetTime());
    Sample(leftCopy, 10, 5); Sample(rightCopy, 10, 20);
    EXPECT_FLOAT_EQ(10, left.GetPositionController()->GetKey(1)->GetTime()); EXPECT_FLOAT_EQ(10, left.GetLength());
    VxVector changed(100, 0, 0); left.AddPositionKey(10, &changed); Sample(leftCopy, 10, 5);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), merged->GetCharacter());
}

TEST_F(KeyedAnimationCompositionTest, CopiedChannelsAllRescaleKeyTimesAndKeepValues) {
    RCKCharacter character(Context); RCK3dEntity entity(Context); RCKObjectAnimation child(Context);
    CompositionProbe first(Context), second(Context); first.Character(&character); second.Character(&character);
    Curve(child, entity, 10, 10); VxVector scale(2, 3, 4); VxQuaternion identity(0, 0, 0, 1);
    child.AddScaleKey(7, &scale); child.AddRotationKey(7, &identity); child.AddScaleAxisKey(7, &identity);
    CKMorphController *morph = static_cast<CKMorphController *>(child.CreateController(CKANIMATION_CONTROLLER_MORPH));
    ASSERT_NE(nullptr, morph); morph->SetMorphVertexCount(1); ASSERT_GE(morph->AddKey(7, FALSE), 0);
    first.AddAnimation(&child); first.SetLength(10); second.SetLength(20);
    RCKKeyedAnimation *merged = Merge(first, second); ASSERT_NE(nullptr, merged); ASSERT_EQ(1, merged->GetAnimationCount());
    CKObjectAnimation *copy = merged->GetAnimation(0);
    CKAnimController *controllers[] = {copy->GetScaleController(), copy->GetRotationController(),
        copy->GetScaleAxisController(), copy->GetMorphController()};
    for (int i = 0; i < 4; ++i) {
        ASSERT_NE(nullptr, controllers[i]); EXPECT_FLOAT_EQ(14, controllers[i]->GetKey(0)->GetTime());
        EXPECT_FLOAT_EQ(20, controllers[i]->GetLength());
    }
    VxVector actual; ASSERT_TRUE(copy->EvaluateScale(14, actual)); EXPECT_FLOAT_EQ(3, actual.y);
    EXPECT_FLOAT_EQ(7, child.GetScaleController()->GetKey(0)->GetTime());
}

TEST_F(KeyedAnimationCompositionTest, IntersectionBlendsMatchedChannelAndOmitsUnmatchedOnes) {
    RCKCharacter character(Context); RCK3dEntity common(Context), unique(Context);
    RCKObjectAnimation left(Context), right(Context), unmatched(Context); CompositionProbe first(Context), second(Context);
    first.Character(&character); second.Character(&character);
    Curve(left, common, 10, 10); Curve(right, common, 20, 40); Curve(unmatched, unique, 10, 90);
    first.AddAnimation(&left); first.AddAnimation(&unmatched); second.AddAnimation(&right);
    first.SetLength(10); second.SetLength(20);
    RCKKeyedAnimation *merged = Merge(first, second, TRUE); ASSERT_NE(nullptr, merged); ASSERT_EQ(1, merged->GetAnimationCount());
    CKObjectAnimation *blend = merged->GetAnimation(0); EXPECT_TRUE(blend->IsMerged()); EXPECT_EQ(left.Get3dEntity(), blend->Get3dEntity());
    merged->SetMergeFactor(0); Sample(blend, 10, 5);
    merged->SetMergeFactor(0.25f); Sample(blend, 10, 8.75f);
    merged->SetMergeFactor(1); Sample(blend, 10, 20);
    EXPECT_FLOAT_EQ(10, left.GetLength()); EXPECT_FLOAT_EQ(20, right.GetLength());
}

TEST_F(KeyedAnimationCompositionTest, DynamicSourceCreatesDynamicGroupAndMatchedChildrenWithoutEditingSourceFlags) {
    RCKCharacter character(Context); RCK3dEntity entity(Context); RCKObjectAnimation left(Context), right(Context);
    CompositionProbe first(Context), second(Context); first.Character(&character); second.Character(&character);
    Curve(left, entity, 10, 10); Curve(right, entity, 20, 40);
    first.AddAnimation(&left); second.AddAnimation(&right); first.SetLength(10); second.SetLength(20);
    first.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); left.SetFlags(CK_OBJECTANIMATION_IGNOREMORPH);
    CKDWORD flags = left.GetFlags(); RCKKeyedAnimation *merged = Merge(first, second, TRUE);
    ASSERT_NE(nullptr, merged); EXPECT_TRUE(merged->IsDynamic()); ASSERT_EQ(1, merged->GetAnimationCount());
    EXPECT_TRUE(merged->GetAnimation(0)->IsDynamic()); EXPECT_EQ(flags, left.GetFlags());
    Sample(merged->GetAnimation(0), 10, 12.5f);
}

TEST_F(KeyedAnimationCompositionTest, OrientationAndTransitionFlagsDoNotCreateDynamicObjects) {
    RCKCharacter character(Context); CompositionProbe first(Context), second(Context);
    first.Character(&character); first.SetFlags(CKANIMATION_ALLOWTURN | CKANIMATION_TRANSITION_WARPTOSTART);
    RCKKeyedAnimation *merged = Merge(first, second); ASSERT_NE(nullptr, merged); EXPECT_FALSE(merged->IsDynamic());
    EXPECT_EQ(0, merged->GetAnimationCount());
}

TEST_F(KeyedAnimationCompositionTest, StaticMergeRestoresInternalSourceCreationBit) {
    RCKCharacter character(Context); RCK3dEntity entity(Context); RCKObjectAnimation left(Context), right(Context);
    CompositionProbe first(Context), second(Context); first.Character(&character);
    Curve(left, entity, 10, 10); Curve(right, entity, 10, 30);
    first.AddAnimation(&left); second.AddAnimation(&right); first.SetLength(10); second.SetLength(10);
    left.SetFlags(0x20000000u | CK_OBJECTANIMATION_IGNOREMORPH); CKDWORD flags = left.GetFlags();
    RCKKeyedAnimation *merged = Merge(first, second); ASSERT_NE(nullptr, merged); ASSERT_EQ(1, merged->GetAnimationCount());
    EXPECT_FALSE(merged->GetAnimation(0)->IsDynamic()); EXPECT_EQ(flags, left.GetFlags());
}

TEST_F(KeyedAnimationCompositionTest, FailedChildMergeRemovesPartialResultAndPreservesInputs) {
    RCKCharacter character(Context); RCK3dEntity common(Context), failingEntity(Context);
    RCKObjectAnimation left(Context), right(Context), failingMatch(Context); FailingMergeChild failure(Context);
    CompositionProbe first(Context), second(Context); first.Character(&character);
    Curve(left, common, 10, 10); Curve(right, common, 10, 30);
    Curve(failure, failingEntity, 10, 20); Curve(failingMatch, failingEntity, 10, 40);
    first.AddAnimation(&left); first.AddAnimation(&failure); second.AddAnimation(&right); second.AddAnimation(&failingMatch);
    int before = LiveObjects(); EXPECT_EQ(nullptr, Merge(first, second)); EXPECT_EQ(before, LiveObjects());
    EXPECT_EQ(2, first.GetAnimationCount()); EXPECT_EQ(2, second.GetAnimationCount()); Sample(&left, 5, 5); Sample(&right, 5, 15);
}

TEST_F(KeyedAnimationCompositionTest, UnionSkipsEmptyUnboundSlotsAndKeepsZeroDurationConstant) {
    RCKCharacter character(Context); RCK3dEntity entity(Context); RCKObjectAnimation constant(Context), unbound(Context);
    CompositionProbe first(Context), second(Context); first.Character(&character); second.SetLength(20);
    constant.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); VxVector position(7, 0, 0); constant.AddPositionKey(0, &position); constant.SetLength(0);
    first.EmptySlot(); first.AddAnimation(&unbound); first.AddAnimation(&constant); first.RCKAnimation::SetLength(0);
    RCKKeyedAnimation *merged = Merge(first, second); ASSERT_NE(nullptr, merged); ASSERT_EQ(1, merged->GetAnimationCount());
    EXPECT_FLOAT_EQ(20, merged->GetAnimation(0)->GetLength()); Sample(merged->GetAnimation(0), 10, 7);
}

TEST_F(KeyedAnimationCompositionTest, MissingArgumentOrCharacterDoesNotCreateObjects) {
    CompositionProbe first(Context), second(Context); int before = Context->GetObjectCount();
    EXPECT_EQ(nullptr, first.CreateMergedAnimation(nullptr)); EXPECT_EQ(nullptr, Merge(first, second));
    EXPECT_EQ(before, Context->GetObjectCount());
}

} // namespace
