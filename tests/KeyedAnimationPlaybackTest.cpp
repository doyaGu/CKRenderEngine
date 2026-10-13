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
class PlaybackProbe : public RCKKeyedAnimation {
public:
    explicit PlaybackProbe(CKContext *context) : RCKKeyedAnimation(context) {}
    void Character(RCKCharacter *character) { m_Character = character; }
    void Root(RCK3dEntity *root) { m_RootEntity = root; }
    void EmptySlot() { m_Animations.PushBack(nullptr); }
};

class KeyedAnimationPlaybackTest : public ::testing::Test {
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
    static void Curve(RCKObjectAnimation &animation, RCK3dEntity &entity, float duration, float start, float end) {
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
        VxVector first(start, 0, 0), last(end, 0, 0);
        animation.AddPositionKey(0, &first); animation.AddPositionKey(duration, &last);
        animation.SetLength(duration);
    }
    static void Sample(CKObjectAnimation *animation, float frame, float expected) {
        ASSERT_NE(nullptr, animation); VxVector position;
        ASSERT_TRUE(animation->EvaluatePosition(frame, position)); EXPECT_NEAR(expected, position.x, 1e-5f);
    }
    static void ChildOf(RCK3dEntity &entity, RCKCharacter &character) {
        entity.SetParent(reinterpret_cast<CK3dEntity *>(&character));
    }
};

TEST_F(KeyedAnimationPlaybackTest, VirtualStepAppliesAllChannelsAndClampsToEndpoints) {
    RCK3dEntity a(Context), b(Context); RCKObjectAnimation x(Context), y(Context); PlaybackProbe group(Context);
    Curve(x, a, 10, 2, 12); Curve(y, b, 20, 10, 50);
    group.AddAnimation(&x); group.EmptySlot(); group.AddAnimation(&y);
    CKAnimation *api = &group; api->SetStep(.25f);
    EXPECT_FLOAT_EQ(4.5f, a.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(20, b.GetLocalMatrix()[3][0]);
    api->SetStep(2); EXPECT_FLOAT_EQ(12, a.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(1, api->GetStep());
    api->SetStep(-2); EXPECT_FLOAT_EQ(2, a.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(0, api->GetStep());
}

TEST_F(KeyedAnimationPlaybackTest, VirtualFrameDrivesPoseWhileCurrentStepOnlyStoresCursor) {
    RCK3dEntity entity(Context); RCKObjectAnimation child(Context); PlaybackProbe group(Context);
    Curve(child, entity, 20, 10, 50); group.AddAnimation(&child); group.SetLength(20);
    CKAnimation *api = &group; api->SetFrame(5);
    EXPECT_FLOAT_EQ(20, entity.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(5, api->GetFrame());
    api->SetCurrentStep(.75f); EXPECT_FLOAT_EQ(.75f, api->GetStep());
    EXPECT_FLOAT_EQ(20, entity.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationPlaybackTest, ZeroDurationFrameAppliesConstantPose) {
    RCK3dEntity entity(Context); RCKObjectAnimation child(Context); PlaybackProbe group(Context);
    VxVector value(7, 0, 0); child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); child.AddPositionKey(0, &value);
    group.AddAnimation(&child); group.SetLength(0); CKAnimation *api = &group; api->SetFrame(3);
    EXPECT_FLOAT_EQ(7, entity.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(0, api->GetStep());
}

TEST_F(KeyedAnimationPlaybackTest, CenteredRootPlaybackUpdatesWorldHierarchy) {
    RCKCharacter character(Context); RCK3dEntity root(Context), descendant(Context);
    RCKObjectAnimation child(Context); PlaybackProbe group(Context);
    ChildOf(root, character); descendant.SetParent(reinterpret_cast<CK3dEntity *>(&root));
    descendant.m_LocalMatrix[3][0] = 2; descendant.LocalMatrixChanged(FALSE, FALSE);
    group.Character(&character); Curve(child, root, 10, 0, 10); group.AddAnimation(&child); group.CenterAnimation(3);
    group.SetStep(.5f); EXPECT_FLOAT_EQ(2, root.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(4, descendant.GetWorldMatrix()[3][0]); EXPECT_EQ(&child, group.GetRootAnimationInternal());
}

TEST_F(KeyedAnimationPlaybackTest, IgnoredEntityRetainsItsPose) {
    RCK3dEntity ignored(Context), active(Context); RCKObjectAnimation x(Context), y(Context); PlaybackProbe group(Context);
    Curve(x, ignored, 10, 0, 10); Curve(y, active, 10, 0, 20);
    ignored.m_MoveableFlags |= 0x400; group.AddAnimation(&x); group.AddAnimation(&y); group.SetStep(.5f);
    EXPECT_FLOAT_EQ(0, ignored.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(10, active.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationPlaybackTest, InvisibleCharacterUpdatesRootAndFloorOnly) {
    RCKCharacter character(Context); RCK3dEntity root(Context), floor(Context), moving(Context);
    RCKObjectAnimation r(Context), f(Context), m(Context); PlaybackProbe group(Context);
    ChildOf(root, character); ChildOf(floor, character); ChildOf(moving, character);
    group.Character(&character); character.SetFloorReferenceObject(reinterpret_cast<CK3dEntity *>(&floor));
    character.m_MoveableFlags &= ~VX_MOVEABLE_CHARACTERRENDERED; character.SetAnimationLevelOfDetail(1);
    Curve(r, root, 10, 0, 1); Curve(f, floor, 10, 0, 2); Curve(m, moving, 10, 0, 100);
    group.AddAnimation(&m); group.AddAnimation(&f); group.AddAnimation(&r); group.Root(&root); group.SetStep(1);
    EXPECT_FLOAT_EQ(1, root.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(2, floor.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(0, moving.GetLocalMatrix()[3][0]); EXPECT_EQ(&f, group.GetAnimation(0)); EXPECT_EQ(&r, group.GetAnimation(1));
}

TEST_F(KeyedAnimationPlaybackTest, LodChoosesHighestContributionAndFullLodUpdatesRemainder) {
    RCKCharacter character(Context); RCK3dEntity root(Context), low(Context), high(Context), medium(Context);
    RCKObjectAnimation r(Context), a(Context), b(Context), c(Context); PlaybackProbe group(Context);
    ChildOf(root, character); group.Character(&character); character.m_MoveableFlags |= VX_MOVEABLE_CHARACTERRENDERED;
    Curve(r, root, 10, 0, 1); Curve(a, low, 10, 0, 2); Curve(b, high, 10, 0, 20); Curve(c, medium, 10, 0, 10);
    group.AddAnimation(&a); group.AddAnimation(&c); group.AddAnimation(&r); group.AddAnimation(&b); group.Root(&root);
    character.SetAnimationLevelOfDetail(.5f); group.SetStep(.5f);
    EXPECT_FLOAT_EQ(.5f, root.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(10, high.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(0, low.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(0, medium.GetLocalMatrix()[3][0]);
    EXPECT_EQ(&r, group.GetAnimation(0)); EXPECT_EQ(&b, group.GetAnimation(1)); EXPECT_EQ(&c, group.GetAnimation(2));
    character.SetAnimationLevelOfDetail(1); group.SetStep(1);
    EXPECT_FLOAT_EQ(2, low.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(10, medium.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationPlaybackTest, RotationScaleAndMorphContributeToLodOrdering) {
    RCKCharacter character(Context); RCK3dEntity root(Context), position(Context), rotation(Context), scale(Context), morph(Context);
    RCKObjectAnimation r(Context), p(Context), q(Context), s(Context), m(Context); PlaybackProbe group(Context);
    ChildOf(root, character); group.Character(&character); character.m_MoveableFlags |= VX_MOVEABLE_CHARACTERRENDERED;
    character.SetAnimationLevelOfDetail(1); Curve(r, root, 10, 0, 0); Curve(p, position, 10, 0, 10);
    q.Set3dEntity(reinterpret_cast<CK3dEntity *>(&rotation)); VxQuaternion identity(0, 0, 0, 1), turn(0, 1, 0, 0);
    q.AddRotationKey(0, &identity); q.AddRotationKey(10, &turn); q.SetLength(10);
    s.Set3dEntity(reinterpret_cast<CK3dEntity *>(&scale)); VxVector one(1, 1, 1), large(21, 1, 1);
    s.AddScaleKey(0, &one); s.AddScaleKey(10, &large); s.SetLength(10);
    m.Set3dEntity(reinterpret_cast<CK3dEntity *>(&morph)); ASSERT_NE(nullptr, m.CreateController(CKANIMATION_CONTROLLER_MORPH));
    group.AddAnimation(&p); group.AddAnimation(&s); group.AddAnimation(&q); group.AddAnimation(&m); group.AddAnimation(&r);
    group.Root(&root); group.SetStep(1);
    EXPECT_EQ(&r, group.GetAnimation(0)); EXPECT_EQ(&q, group.GetAnimation(1)); EXPECT_EQ(&m, group.GetAnimation(2));
    EXPECT_EQ(&s, group.GetAnimation(3)); EXPECT_EQ(&p, group.GetAnimation(4)); EXPECT_NEAR(21, scale.GetLocalMatrix()[0][0], 1e-5f);
}

TEST_F(KeyedAnimationPlaybackTest, MergedContributionAveragesSourcesAndPreservesEqualScoreOrder) {
    RCKCharacter character(Context); RCK3dEntity root(Context), blended(Context), plain(Context);
    RCKObjectAnimation r(Context), left(Context), right(Context), p(Context); PlaybackProbe group(Context);
    ChildOf(root, character); group.Character(&character); character.m_MoveableFlags |= VX_MOVEABLE_CHARACTERRENDERED;
    character.SetAnimationLevelOfDetail(1); Curve(r, root, 10, 0, 0);
    Curve(left, blended, 10, 0, 2); Curve(right, blended, 10, 0, 18); Curve(p, plain, 10, 0, 10);
    CKObjectAnimation *merged = left.CreateMergedAnimation(&right); ASSERT_NE(nullptr, merged);
    group.AddAnimation(&p); group.AddAnimation(merged); group.AddAnimation(&r); group.Root(&root); group.SetStep(.5f);
    EXPECT_EQ(&r, group.GetAnimation(0)); EXPECT_EQ(&p, group.GetAnimation(1)); EXPECT_EQ(merged, group.GetAnimation(2));
    EXPECT_FLOAT_EQ(5, blended.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationPlaybackTest, EmptyAndUnboundChannelsSortAfterBoundChannels) {
    RCKCharacter character(Context); RCK3dEntity root(Context), other(Context);
    RCKObjectAnimation r(Context), p(Context), unbound(Context); PlaybackProbe group(Context);
    ChildOf(root, character); group.Character(&character); character.m_MoveableFlags |= VX_MOVEABLE_CHARACTERRENDERED;
    character.SetAnimationLevelOfDetail(1); Curve(r, root, 10, 0, 1); Curve(p, other, 10, 0, 2);
    group.EmptySlot(); group.AddAnimation(&unbound); group.AddAnimation(&p); group.AddAnimation(&r); group.Root(&root);
    group.SetStep(1); EXPECT_EQ(&r, group.GetAnimation(0)); EXPECT_EQ(&p, group.GetAnimation(1));
    EXPECT_EQ(&unbound, group.GetAnimation(2)); EXPECT_EQ(nullptr, group.GetAnimation(3));
}

TEST_F(KeyedAnimationPlaybackTest, NativeContributionUsesSignedAxisSumAndAdditionInvalidatesSort) {
    RCKCharacter character(Context); RCK3dEntity root(Context), cancelled(Context), plain(Context), added(Context);
    RCKObjectAnimation r(Context), c(Context), p(Context), a(Context); PlaybackProbe group(Context);
    ChildOf(root, character); group.Character(&character); character.m_MoveableFlags |= VX_MOVEABLE_CHARACTERRENDERED;
    character.SetAnimationLevelOfDetail(1); Curve(r, root, 10, 0, 0); Curve(p, plain, 10, 0, 2);
    c.Set3dEntity(reinterpret_cast<CK3dEntity *>(&cancelled)); VxVector zero(0, 0, 0), opposing(100, -100, 0);
    c.AddPositionKey(0, &zero); c.AddPositionKey(10, &opposing); c.SetLength(10);
    group.AddAnimation(&c); group.AddAnimation(&p); group.AddAnimation(&r); group.Root(&root); group.SetStep(1);
    EXPECT_EQ(&p, group.GetAnimation(1)); EXPECT_EQ(&c, group.GetAnimation(2));
    Curve(a, added, 10, 0, 20); group.AddAnimation(&a); group.SetStep(.5f);
    EXPECT_EQ(&a, group.GetAnimation(1)); EXPECT_EQ(&p, group.GetAnimation(2));
    EXPECT_FLOAT_EQ(10, added.GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationPlaybackTest, TransitionUsesInputCursorAndOutputFrameThenDrivesPose) {
    RCK3dEntity entity(Context); RCKObjectAnimation a(Context), b(Context); PlaybackProbe in(Context), out(Context), result(Context);
    Curve(a, entity, 10, 0, 10); Curve(b, entity, 20, 100, 140);
    in.AddAnimation(&a); out.AddAnimation(&b); in.SetLength(10); out.SetLength(20); in.SetCurrentStep(.3f);
    EXPECT_FLOAT_EQ(8, result.CreateTransition(&in, &out, 0, 4, 8)); EXPECT_FLOAT_EQ(4, result.GetLength());
    ASSERT_EQ(1, result.GetAnimationCount()); Sample(result.GetAnimation(0), 0, 3); Sample(result.GetAnimation(0), 4, 116);
    result.SetStep(0); EXPECT_NEAR(3, entity.GetLocalMatrix()[3][0], 1e-5f);
    result.SetStep(.5f); EXPECT_NEAR(59.5f, entity.GetLocalMatrix()[3][0], 1e-5f);
    result.SetStep(1); EXPECT_NEAR(116, entity.GetLocalMatrix()[3][0], 1e-5f);
}

TEST_F(KeyedAnimationPlaybackTest, SamePositionUsesNormalizedInputCursorAndMinimumDuration) {
    RCK3dEntity entity(Context); RCKObjectAnimation a(Context), b(Context); PlaybackProbe in(Context), out(Context), result(Context);
    Curve(a, entity, 10, 0, 10); Curve(b, entity, 20, 100, 140);
    in.AddAnimation(&a); out.AddAnimation(&b); in.SetLength(10); out.SetLength(20); in.SetCurrentStep(.25f);
    EXPECT_FLOAT_EQ(5, result.CreateTransition(&in, &out, CK_TRANSITION_WARPTOSAMEPOS, .2f, 19));
    EXPECT_FLOAT_EQ(1, result.GetLength()); Sample(result.GetAnimation(0), 0, 2.5f); Sample(result.GetAnimation(0), 1, 110);
}

TEST_F(KeyedAnimationPlaybackTest, TransitionCopiesOutputFrameRateAndAllowTurn) {
    RCK3dEntity entity(Context); RCKObjectAnimation a(Context), b(Context); PlaybackProbe in(Context), out(Context), result(Context);
    Curve(a, entity, 10, 0, 10); Curve(b, entity, 20, 20, 40); in.AddAnimation(&a); out.AddAnimation(&b);
    in.SetLength(10); out.SetLength(20); out.LinkToFrameRate(TRUE, 30); out.SetFlags(out.GetFlags() | CKANIMATION_ALLOWTURN);
    result.CreateTransition(&in, &out, 0, 2, 0);
    EXPECT_TRUE(result.IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(30, result.GetLinkedFrameRate());
    EXPECT_TRUE(result.GetFlags() & CKANIMATION_ALLOWTURN);
    out.LinkToFrameRate(FALSE, 24); out.SetFlags(out.GetFlags() & ~CKANIMATION_ALLOWTURN);
    result.CreateTransition(&in, &out, 0, 2, 0);
    EXPECT_FALSE(result.IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(24, result.GetLinkedFrameRate());
    EXPECT_FALSE(result.GetFlags() & CKANIMATION_ALLOWTURN);
}

TEST_F(KeyedAnimationPlaybackTest, BestTransitionSearchesAllMatchedPositionChannels) {
    RCK3dEntity x(Context), y(Context); RCKObjectAnimation a(Context), b(Context), c(Context), d(Context);
    PlaybackProbe in(Context), out(Context), result(Context);
    Curve(a, x, 10, 0, 60); Curve(b, y, 10, 0, 2); Curve(c, x, 10, 0, 100); Curve(d, y, 10, 0, 10);
    in.AddAnimation(&a); in.AddAnimation(&b); out.AddAnimation(&c); out.AddAnimation(&d);
    in.SetLength(10); out.SetLength(10); in.SetCurrentStep(.5f);
    // x prefers frame3 with weight10/frame; the last channel y prefers frame1.
    EXPECT_FLOAT_EQ(3, result.CreateTransition(&in, &out, CK_TRANSITION_WARPTOBEST, 2, 9));
    Sample(result.GetAnimation(0), 0, 30); Sample(result.GetAnimation(0), 2, 30);
    Sample(result.GetAnimation(1), 0, 1); Sample(result.GetAnimation(1), 2, 3);
}

TEST_F(KeyedAnimationPlaybackTest, BestTransitionIncludesScaleAndRotationAndKeepsEarliestTie) {
    RCK3dEntity entity(Context); RCKObjectAnimation a(Context), b(Context); PlaybackProbe in(Context), out(Context), result(Context);
    a.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); b.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    VxVector scale(3, 1, 1), one(1, 1, 1), end(11, 1, 1);
    VxQuaternion identity(0, 0, 0, 1); a.AddScaleKey(0, &scale); b.AddScaleKey(0, &one); b.AddScaleKey(10, &end);
    a.AddRotationKey(0, &identity); b.AddRotationKey(0, &identity);
    in.AddAnimation(&a); out.AddAnimation(&b); in.SetLength(10); out.SetLength(10);
    EXPECT_FLOAT_EQ(2, result.CreateTransition(&in, &out, CK_TRANSITION_WARPTOBEST, 2, 0));
    VxVector value; ASSERT_TRUE(result.GetAnimation(0)->EvaluateScale(2, value)); EXPECT_FLOAT_EQ(3, value.x);
    b.DeleteController(CKANIMATION_CONTROLLER_SCL);
    EXPECT_FLOAT_EQ(0, result.CreateTransition(&in, &out, CK_TRANSITION_WARPTOBEST, 2, 9));
}

TEST_F(KeyedAnimationPlaybackTest, BestTransitionUsesRotationScoreAndPreservesScaleAxisEndpoints) {
    RCK3dEntity entity(Context); RCKObjectAnimation a(Context), b(Context); PlaybackProbe in(Context), out(Context), result(Context);
    a.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); b.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    VxQuaternion identity(0, 0, 0, 1), halfTurn(0, 1, 0, 0);
    a.AddRotationKey(0, &halfTurn); b.AddRotationKey(0, &identity); b.AddRotationKey(2, &halfTurn); b.AddRotationKey(10, &halfTurn);
    a.AddScaleAxisKey(0, &identity); b.AddScaleAxisKey(0, &halfTurn);
    in.AddAnimation(&a); out.AddAnimation(&b); in.SetLength(10); out.SetLength(10);
    EXPECT_FLOAT_EQ(2, result.CreateTransition(&in, &out, CK_TRANSITION_WARPTOBEST, 2, 9));
    VxQuaternion axis; ASSERT_TRUE(result.GetAnimation(0)->EvaluateScaleAxis(0, axis)); EXPECT_NEAR(1, fabsf(axis.w), 1e-5f);
    ASSERT_TRUE(result.GetAnimation(0)->EvaluateScaleAxis(2, axis)); EXPECT_NEAR(1, fabsf(axis.y), 1e-5f);
}

TEST_F(KeyedAnimationPlaybackTest, TransitionLastMatchingInputWinsAndUnmatchedChannelsClear) {
    RCK3dEntity x(Context), y(Context); RCKObjectAnimation first(Context), last(Context), target(Context), unmatched(Context), stale(Context);
    PlaybackProbe in(Context), out(Context), result(Context);
    Curve(first, x, 10, 1, 1); Curve(last, x, 10, 9, 9); Curve(target, x, 10, 20, 20);
    Curve(unmatched, y, 10, 40, 40); Curve(stale, y, 10, 90, 90);
    in.AddAnimation(&first); in.AddAnimation(&last); out.AddAnimation(&target); out.AddAnimation(&unmatched);
    result.AddAnimation(&stale); in.SetLength(10); out.SetLength(10);
    result.CreateTransition(&in, &out, 0, 2, 0); Sample(result.GetAnimation(0), 0, 9); Sample(result.GetAnimation(0), 2, 20);
    ASSERT_EQ(2, result.GetAnimationCount()); EXPECT_FALSE(result.GetAnimation(1)->HasPositionInfo());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&x), stale.Get3dEntity());
    EXPECT_EQ(1, x.GetObjectAnimationCount() - 3); EXPECT_EQ(1, y.GetObjectAnimationCount());
}

TEST_F(KeyedAnimationPlaybackTest, InPlaceInputTransitionUsesEntityMappingInsteadOfArrayIndex) {
    RCK3dEntity x(Context), y(Context); RCKObjectAnimation a(Context), b(Context), c(Context), d(Context); PlaybackProbe in(Context), out(Context);
    Curve(a, x, 10, 1, 1); Curve(b, y, 10, 2, 2); Curve(c, x, 20, 10, 10); Curve(d, y, 20, 20, 20);
    in.AddAnimation(&a); in.AddAnimation(&b); out.AddAnimation(&d); out.AddAnimation(&c); in.SetLength(10); out.SetLength(20);
    EXPECT_FLOAT_EQ(5, in.CreateTransition(&in, &out, 0, 2, 5));
    Sample(&a, 0, 1); Sample(&a, 2, 10); Sample(&b, 0, 2); Sample(&b, 2, 20);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&x), a.Get3dEntity()); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&y), b.Get3dEntity());
}

TEST_F(KeyedAnimationPlaybackTest, InPlaceOutputTransitionReturnsOriginalDestinationFrame) {
    RCK3dEntity entity(Context); RCKObjectAnimation a(Context), b(Context); PlaybackProbe in(Context), out(Context);
    Curve(a, entity, 10, 1, 1); Curve(b, entity, 20, 10, 50); in.AddAnimation(&a); out.AddAnimation(&b);
    in.SetLength(10); out.SetLength(20);
    EXPECT_FLOAT_EQ(8, out.CreateTransition(&in, &out, 0, 4, 8));
    EXPECT_FLOAT_EQ(4, out.GetLength()); Sample(&b, 0, 1); Sample(&b, 4, 26);
}

TEST_F(KeyedAnimationPlaybackTest, CharacterMismatchAndMissingInputsPreserveResult) {
    RCKCharacter one(Context), two(Context); RCK3dEntity entity(Context); RCKObjectAnimation child(Context);
    PlaybackProbe in(Context), out(Context), result(Context); in.Character(&one); out.Character(&two);
    Curve(child, entity, 10, 3, 7); result.AddAnimation(&child); result.SetLength(10);
    EXPECT_FLOAT_EQ(-1, result.CreateTransition(&in, &out, 0, 2, 0)); EXPECT_FLOAT_EQ(10, result.GetLength());
    EXPECT_EQ(nullptr, result.GetCharacter()); Sample(&child, 10, 7);
    EXPECT_FLOAT_EQ(0, result.CreateTransition(nullptr, &out, 0, 2, 0));
    EXPECT_FLOAT_EQ(0, result.CreateTransition(&in, &in, 0, 2, 0)); EXPECT_EQ(1, result.GetAnimationCount());
}

TEST_F(KeyedAnimationPlaybackTest, DynamicTransitionCreatesDynamicChildAndRegistersEntity) {
    RCK3dEntity entity(Context); RCKObjectAnimation a(Context), b(Context); PlaybackProbe in(Context), out(Context), result(Context);
    Curve(a, entity, 10, 0, 10); Curve(b, entity, 10, 20, 30); in.AddAnimation(&a); out.AddAnimation(&b);
    in.SetLength(10); out.SetLength(10); result.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    result.CreateTransition(&in, &out, 0, 2, 0); ASSERT_EQ(1, result.GetAnimationCount());
    EXPECT_TRUE(result.GetAnimation(0)->IsDynamic()); EXPECT_EQ(3, entity.GetObjectAnimationCount());
    EXPECT_EQ(result.GetAnimation(0), entity.GetObjectAnimation(2));
}

TEST_F(KeyedAnimationPlaybackTest, VelocityModeAppliesOnlyToRoot) {
    RCK3dEntity root(Context), other(Context); RCKObjectAnimation a(Context), b(Context), c(Context), d(Context);
    PlaybackProbe in(Context), out(Context), result(Context);
    Curve(a, root, 10, 0, 10); Curve(b, other, 10, 0, 10); Curve(c, root, 20, 100, 160); Curve(d, other, 20, 100, 160);
    in.AddAnimation(&a); in.AddAnimation(&b); out.AddAnimation(&c); out.AddAnimation(&d);
    in.SetLength(10); out.SetLength(20); out.Root(&root); in.SetCurrentStep(.5f);
    EXPECT_FLOAT_EQ(10, result.CreateTransition(&in, &out, CK_TRANSITION_USEVELOCITY, 2, 10));
    Sample(result.GetAnimation(0), 0, 0); Sample(result.GetAnimation(0), 1, .4f); Sample(result.GetAnimation(0), 2, 1.2f);
    Sample(result.GetAnimation(1), 0, 5); Sample(result.GetAnimation(1), 2, 130);
}

TEST_F(KeyedAnimationPlaybackTest, AlignOrientationChangesOnlyRootStartingRotation) {
    RCK3dEntity root(Context), other(Context); RCKObjectAnimation a(Context), b(Context), c(Context), d(Context);
    PlaybackProbe in(Context), out(Context), result(Context);
    RCKObjectAnimation *sources[] = {&a, &b, &c, &d};
    VxQuaternion identity(0, 0, 0, 1), yaw(0, .70710678118f, 0, .70710678118f);
    for (int i = 0; i < 4; ++i) {
        sources[i]->Set3dEntity(reinterpret_cast<CK3dEntity *>(i % 2 ? &other : &root));
        sources[i]->AddRotationKey(0, &identity); sources[i]->AddRotationKey(10, &yaw);
    }
    in.AddAnimation(&a); in.AddAnimation(&b); out.AddAnimation(&c); out.AddAnimation(&d);
    in.SetLength(10); out.SetLength(10); out.Root(&root); in.SetCurrentStep(1);
    in.SetFlags(in.GetFlags() | CKANIMATION_ALIGNORIENTATION);
    result.CreateTransition(&in, &out, 0, 2, 0);
    VxQuaternion rotation;
    ASSERT_TRUE(result.GetAnimation(0)->EvaluateRotation(0, rotation));
    EXPECT_NEAR(-1, rotation.y * rotation.w * 2, 1e-5f);
    ASSERT_TRUE(result.GetAnimation(1)->EvaluateRotation(0, rotation));
    EXPECT_NEAR(1, rotation.y * rotation.w * 2, 1e-5f);
}
} // namespace
