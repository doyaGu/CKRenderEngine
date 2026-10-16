#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class CompletionKeyed : public RCKKeyedAnimation {
public:
    explicit CompletionKeyed(CKContext *context) : RCKKeyedAnimation(context) {}
    void Character(RCKCharacter *character) { m_Character = character; }
    void Root(RCK3dEntity *root) { m_RootEntity = root; }
    static float Contribution(CKObjectAnimation *animation) {
        return ComputeAnimationContribution(static_cast<RCKObjectAnimation *>(animation));
    }
};

class CompletionRemap : public CKDependenciesContext {
public:
    explicit CompletionRemap(CKContext *context) : CKDependenciesContext(context) {}
    void Forget(CKObject &object) { m_MapID.Insert(object.GetID(), 0); }
};

class KeyedAnimationCompositionCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    RCKCharacter *Character() {
        return static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER));
    }
    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }
    RCKObjectAnimation *Channel(RCK3dEntity *entity, const VxVector &first,
                                const VxVector &last, float length = 10) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(
            Context->CreateObject(CKCID_OBJECTANIMATION));
        if (!animation)
            return nullptr;
        animation->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
        VxVector a = first, b = last;
        animation->AddPositionKey(0, &a);
        animation->AddPositionKey(length, &b);
        animation->SetLength(length);
        return animation;
    }
    static float X(RCK3dEntity *entity) {
        VxVector position;
        entity->GetPosition(&position);
        return position.x;
    }
    static void ChildOf(RCK3dEntity *entity, RCKCharacter *character) {
        entity->SetParent(reinterpret_cast<CK3dEntity *>(character));
    }
    static void Vec(const VxVector &actual, const VxVector &expected) {
        EXPECT_FLOAT_EQ(expected.x, actual.x);
        EXPECT_FLOAT_EQ(expected.y, actual.y);
        EXPECT_FLOAT_EQ(expected.z, actual.z);
    }

    int TransitionAfterNullRemap() {
        RCK3dEntity entity(Context);
        RCKKeyedAnimation input(Context), output(Context), result(Context);
        RCKObjectAnimation *a = Channel(&entity, VxVector(0, 0, 0), VxVector(10, 0, 0));
        RCKObjectAnimation *b = Channel(&entity, VxVector(20, 0, 0), VxVector(30, 0, 0));
        RCKObjectAnimation *removed = Channel(&entity, VxVector(99, 0, 0), VxVector(99, 0, 0));
        RCKObjectAnimation *reuse = Channel(&entity, VxVector(88, 0, 0), VxVector(88, 0, 0));
        if (!a || !b || !removed || !reuse)
            return 11;
        if (input.AddAnimation(a) != CK_OK || output.AddAnimation(b) != CK_OK ||
            result.AddAnimation(removed) != CK_OK || result.AddAnimation(reuse) != CK_OK)
            return 12;
        input.SetLength(10); output.SetLength(10); result.SetLength(10);
        input.SetCurrentStep(.25f); output.UpdateRootEntity(); result.UpdateRootEntity();
        if (result.GetAnimation(reinterpret_cast<CK3dEntity *>(&entity)) != removed)
            return 13;
        CompletionRemap remap(Context);
        remap.Forget(*removed);
        if (remap.Remap(removed) != nullptr || result.RemapDependencies(remap) != CK_OK ||
            result.GetAnimationCount() != 2 || result.GetAnimation(0) != nullptr || result.GetAnimation(1) != reuse)
            return 14;
        CKAnimation *api = &result;
        if (api->CreateTransition(&input, &output, 0, 2, 5) != 5 || result.GetLength() != 2 ||
            result.GetAnimationCount() != 1 || result.GetAnimation(0) != reuse)
            return 15;
        if (Context->GetObject(removed->GetID()) != removed ||
            reuse->Get3dEntity() != reinterpret_cast<CK3dEntity *>(&entity) ||
            (reuse->GetFlags() & CK_OBJECTANIMATION_WARPER) != 0)
            return 16;
        VxVector sample;
        if (!reuse->EvaluatePosition(0, sample) || sample.x != 2.5f)
            return 17;
        if (!reuse->EvaluatePosition(2, sample) || sample.x != 25)
            return 18;
        api->SetFrame(1);
        if (api->GetStep() != .5f || X(&entity) != 13.75f)
            return 19;
        return 0;
    }
};

TEST_F(KeyedAnimationCompositionCompletionTest, UnionRetimesAllFiveChannelsWithoutOverflowingTheDurationRatio) {
    for (int side = 0; side < 2; ++side) {
        SCOPED_TRACE(side);
        RCKCharacter *character = Character();
        RCK3dEntity *entity = Entity();
        ASSERT_NE(nullptr, character);
        ASSERT_NE(nullptr, entity);
        ChildOf(entity, character);
        CompletionKeyed first(Context), second(Context);
        first.Character(character);
        second.Character(character);
        const float shortLength = std::ldexp(1.0f, -120);
        const float longLength = std::ldexp(1.0f, 120);
        RCKObjectAnimation *source = Channel(entity, VxVector(0, 0, 0), VxVector(8, 4, -2), shortLength);
        ASSERT_NE(nullptr, source);
        VxVector unit(1, 1, 1), large(3, 5, 7);
        VxQuaternion identity(0, 0, 0, 1), turn(0, .70710678118f, 0, .70710678118f);
        source->AddScaleKey(0, &unit); source->AddScaleKey(shortLength, &large);
        source->AddRotationKey(0, &identity); source->AddRotationKey(shortLength, &turn);
        source->AddScaleAxisKey(0, &identity); source->AddScaleAxisKey(shortLength, &identity);
        CKMorphController *morph = static_cast<CKMorphController *>(source->CreateController(CKANIMATION_CONTROLLER_MORPH));
        ASSERT_NE(nullptr, morph);
        morph->SetMorphVertexCount(1);
        ASSERT_EQ(0, morph->AddKey(0.0f, FALSE));
        ASSERT_EQ(1, morph->AddKey(shortLength, FALSE));
        CKMorphKey *m0 = static_cast<CKMorphKey *>(morph->GetKey(0));
        CKMorphKey *m1 = static_cast<CKMorphKey *>(morph->GetKey(1));
        ASSERT_NE(nullptr, m0->PosArray); ASSERT_NE(nullptr, m1->PosArray);
        m0->PosArray[0].Set(0, 0, 0); m1->PosArray[0].Set(8, 4, -2);
        CompletionKeyed &shortGroup = side ? second : first;
        CompletionKeyed &longGroup = side ? first : second;
        ASSERT_EQ(CK_OK, shortGroup.AddAnimation(source));
        shortGroup.SetLength(shortLength);
        longGroup.SetLength(longLength);
        CKAnimation *api = &first;
        CKKeyedAnimation *merged = CKKeyedAnimation::Cast(api->CreateMergedAnimation(&second, FALSE));
        ASSERT_NE(nullptr, merged);
        ASSERT_EQ(1, merged->GetAnimationCount());
        CKObjectAnimation *copy = merged->GetAnimation(0);
        ASSERT_NE(nullptr, copy);
        EXPECT_NE(source, copy);
        CKAnimController *controllers[] = {copy->GetPositionController(), copy->GetScaleController(),
            copy->GetRotationController(), copy->GetScaleAxisController(), copy->GetMorphController()};
        for (int i = 0; i < 5; ++i) {
            SCOPED_TRACE(i);
            ASSERT_NE(nullptr, controllers[i]);
            ASSERT_EQ(2, controllers[i]->GetKeyCount());
            // The ratio is 2^240; the authored key times are 0 and2^-120.
            // Their mapped times are exactly0 and2^120, both finite floats.
            EXPECT_EQ(0.0f, controllers[i]->GetKey(0)->GetTime());
            EXPECT_EQ(longLength, controllers[i]->GetKey(1)->GetTime());
            EXPECT_EQ(longLength, controllers[i]->GetLength());
        }
        VxVector value;
        ASSERT_TRUE(copy->EvaluatePosition(longLength * .5f, value));
        Vec(value, VxVector(4, 2, -1));
        ASSERT_TRUE(copy->EvaluateScale(longLength * .5f, value));
        Vec(value, VxVector(2, 3, 4));
        merged->SetStep(.5f);
        entity->GetPosition(&value);
        Vec(value, VxVector(4, 2, -1));
        EXPECT_EQ(shortLength, source->GetPositionController()->GetKey(1)->GetTime());
        EXPECT_EQ(shortLength, source->GetLength());
    }
}

TEST_F(KeyedAnimationCompositionCompletionTest, UnionRetimingPreservesNativeFiniteRatioAtAStepBoundary) {
    RCKCharacter *character = Character();
    RCK3dEntity *entity = Entity();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, entity);
    ChildOf(entity, character);
    CompletionKeyed first(Context), second(Context);
    first.Character(character);
    RCKObjectAnimation *source = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    ASSERT_NE(nullptr, source);
    source->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
    CKAnimController *controller = source->CreateController(CKANIMATION_BEZIERPOS_CONTROL);
    ASSERT_NE(nullptr, controller);
    CKBezierKeyFlags flags;
    flags.SetInTangentMode(BEZIER_KEY_STEP); flags.SetOutTangentMode(BEZIER_KEY_STEP);
    VxVector zero(0, 0, 0), end(8, 0, 0);
    CKBezierPositionKey a(0, zero, flags), b(.6f, end, flags);
    ASSERT_EQ(0, controller->AddKey(&a)); ASSERT_EQ(1, controller->AddKey(&b));
    ASSERT_EQ(CK_OK, first.AddAnimation(source)); first.SetLength(3); second.SetLength(7);
    CKAnimation *api = &first;
    CKKeyedAnimation *merged = CKKeyedAnimation::Cast(api->CreateMergedAnimation(&second, FALSE));
    ASSERT_NE(nullptr, merged); ASSERT_EQ(1, merged->GetAnimationCount());
    CKObjectAnimation *copy = merged->GetAnimation(0);
    ASSERT_NE(nullptr, copy);
    // Native f32(7/3)*f32(.6) rounds to1.4f; an unconditionally double
    // ratio produces the next float above1.4f and delays the STEP transition.
    EXPECT_EQ(1.4f, copy->GetPositionController()->GetKey(1)->GetTime());
    merged->SetFrame(1.4f);
    EXPECT_EQ(8.0f, X(entity));
}

TEST_F(KeyedAnimationCompositionCompletionTest, LodTruncatesTheNativeWideProductBeforeSelectingAdditionalChannels) {
    for (int floorPresent = 0; floorPresent < 2; ++floorPresent) {
        SCOPED_TRACE(floorPresent);
        RCKCharacter *character = Character();
        RCK3dEntity *root = Entity();
        ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, root);
        ChildOf(root, character);
        CompletionKeyed group(Context); group.Character(character); group.Root(root);
        RCKObjectAnimation *rootCurve = Channel(root, VxVector(0, 0, 0), VxVector(1, 0, 0));
        ASSERT_NE(nullptr, rootCurve); ASSERT_EQ(CK_OK, group.AddAnimation(rootCurve));
        RCK3dEntity *floor = nullptr;
        if (floorPresent) {
            floor = Entity(); ASSERT_NE(nullptr, floor);
            ChildOf(floor, character);
            character->SetFloorReferenceObject(reinterpret_cast<CK3dEntity *>(floor));
            RCKObjectAnimation *floorCurve = Channel(floor, VxVector(0, 0, 0), VxVector(2, 0, 0));
            ASSERT_NE(nullptr, floorCurve); ASSERT_EQ(CK_OK, group.AddAnimation(floorCurve));
        }
        RCK3dEntity *limbs[25];
        for (int i = 0; i < 25; ++i) {
            limbs[i] = Entity(); ASSERT_NE(nullptr, limbs[i]);
            ChildOf(limbs[i], character);
            RCKObjectAnimation *curve = Channel(limbs[i], VxVector(0, 0, 0), VxVector(100.0f - i, 0, 0));
            ASSERT_NE(nullptr, curve); ASSERT_EQ(CK_OK, group.AddAnimation(curve));
        }
        character->SetMoveableFlags(character->GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        character->SetAnimationLevelOfDetail(.04f);
        CKAnimation *api = &group; api->SetStep(1);
        EXPECT_EQ(1.0f, X(root));
        if (floor) EXPECT_EQ(2.0f, X(floor));
        // Stored .04f is slightly below1/25. The original wide product is
        // .9999999776, so integer truncation selects zero additional limbs.
        for (int i = 0; i < 25; ++i) EXPECT_EQ(0.0f, X(limbs[i])) << i;
        character->SetAnimationLevelOfDetail(std::nextafter(.04f, 1.0f));
        api->SetStep(1);
        EXPECT_EQ(100.0f, X(limbs[0]));
        for (int i = 1; i < 25; ++i) EXPECT_EQ(0.0f, X(limbs[i])) << i;
    }
}

TEST_F(KeyedAnimationCompositionCompletionTest, OpposingFiniteVectorComponentsKeepTheirZeroContributionAndLodPriority) {
    RCKCharacter *character = Character();
    RCK3dEntity *root = Entity(), *cancelled = Entity(), *plain = Entity();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, root); ASSERT_NE(nullptr, cancelled); ASSERT_NE(nullptr, plain);
    ChildOf(root, character); ChildOf(cancelled, character); ChildOf(plain, character);
    CompletionKeyed group(Context); group.Character(character); group.Root(root);
    RCKObjectAnimation *r = Channel(root, VxVector(0, 0, 0), VxVector(1, 0, 0));
    RCKObjectAnimation *c = Channel(cancelled, VxVector(FLT_MAX, -FLT_MAX, 0), VxVector(-FLT_MAX, FLT_MAX, 0));
    RCKObjectAnimation *p = Channel(plain, VxVector(0, 0, 0), VxVector(10, 0, 0));
    ASSERT_NE(nullptr, r); ASSERT_NE(nullptr, c); ASSERT_NE(nullptr, p);
    EXPECT_EQ(0.0f, CompletionKeyed::Contribution(c));
    ASSERT_EQ(CK_OK, group.AddAnimation(c)); ASSERT_EQ(CK_OK, group.AddAnimation(p)); ASSERT_EQ(CK_OK, group.AddAnimation(r));
    character->SetMoveableFlags(character->GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
    character->SetAnimationLevelOfDetail(.5f);
    CKAnimation *api = &group; api->SetStep(.5f);
    EXPECT_EQ(r, group.GetAnimation(0)); EXPECT_EQ(p, group.GetAnimation(1)); EXPECT_EQ(c, group.GetAnimation(2));
    EXPECT_FLOAT_EQ(5, X(plain)); EXPECT_FLOAT_EQ(.5f, X(root));
}

TEST_F(KeyedAnimationCompositionCompletionTest, MergedContributionKeepsTheRepresentableMeanOfLargeSourceScores) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    const float extent = std::ldexp(1.0f, 127);
    RCKObjectAnimation *a = Channel(entity, VxVector(0, 0, 0), VxVector(extent, 0, 0));
    RCKObjectAnimation *b = Channel(entity, VxVector(0, 0, 0), VxVector(extent, 0, 0));
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
    CKObjectAnimation *blend = a->CreateMergedAnimation(b);
    ASSERT_NE(nullptr, blend);
    // Mean(2^127,2^127)=2^127, although their unscaled sum exceeds FLT_MAX.
    EXPECT_EQ(extent, CompletionKeyed::Contribution(a));
    EXPECT_EQ(extent, CompletionKeyed::Contribution(b));
    const float score = CompletionKeyed::Contribution(blend);
    EXPECT_TRUE(std::isfinite(score)); EXPECT_EQ(extent, score);
    VxVector position;
    ASSERT_TRUE(blend->EvaluatePosition(5, position));
    EXPECT_FLOAT_EQ(std::ldexp(1.0f, 126), position.x);
}

TEST_F(KeyedAnimationCompositionCompletionTest, SmallQuaternionMotionRetainsItsNativeContributionAndActualLodPose) {
    RCKCharacter *character = Character();
    RCK3dEntity *root = Entity(), *rotating = Entity(), *plain = Entity();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, root); ASSERT_NE(nullptr, rotating); ASSERT_NE(nullptr, plain);
    ChildOf(root, character); ChildOf(rotating, character); ChildOf(plain, character);
    CompletionKeyed group(Context); group.Character(character); group.Root(root);
    RCKObjectAnimation *r = Channel(root, VxVector(0, 0, 0), VxVector(1, 0, 0));
    RCKObjectAnimation *p = Channel(plain, VxVector(0, 0, 0), VxVector(std::ldexp(1.0f, -18), 0, 0));
    RCKObjectAnimation *q = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    ASSERT_NE(nullptr, r); ASSERT_NE(nullptr, p); ASSERT_NE(nullptr, q);
    q->Set3dEntity(reinterpret_cast<CK3dEntity *>(rotating));
    const float small = std::ldexp(1.0f, -13);
    VxQuaternion first(small, 0, 0, 1), last(-small, 0, 0, 1);
    q->AddRotationKey(0, &first); q->AddRotationKey(10, &last); q->SetLength(10);
    // The stored float quaternions are nearest representations of the small
    // rotations. Their raw native dot is1-2^-26, so score=125*2^-23.
    EXPECT_FLOAT_EQ(std::ldexp(125.0f, -23), CompletionKeyed::Contribution(q));
    ASSERT_EQ(CK_OK, group.AddAnimation(p)); ASSERT_EQ(CK_OK, group.AddAnimation(q)); ASSERT_EQ(CK_OK, group.AddAnimation(r));
    character->SetMoveableFlags(character->GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
    character->SetAnimationLevelOfDetail(.5f);
    CKAnimation *api = &group; api->SetStep(1);
    EXPECT_EQ(r, group.GetAnimation(0)); EXPECT_EQ(q, group.GetAnimation(1)); EXPECT_EQ(p, group.GetAnimation(2));
    VxQuaternion actual; rotating->GetQuaternion(&actual);
    EXPECT_NEAR(small, std::fabs(actual.x), 0.0000001f);
    EXPECT_EQ(0.0f, X(plain));
}

TEST_F(KeyedAnimationCompositionCompletionTest, BestTransitionDistinguishesSmallRotationScoresAndSelectsTheActualPose) {
    RCK3dEntity *entity = Entity(); ASSERT_NE(nullptr, entity);
    RCKKeyedAnimation input(Context), output(Context), result(Context);
    RCKObjectAnimation *a = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    RCKObjectAnimation *b = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
    a->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
    b->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
    const float small = std::ldexp(1.0f, -13);
    VxQuaternion positive(small, 0, 0, 1), negative(-small, 0, 0, 1);
    a->AddRotationKey(0, &positive); a->AddRotationKey(4, &positive);
    b->AddRotationKey(0, &negative); b->AddRotationKey(1, &positive); b->AddRotationKey(4, &positive);
    ASSERT_EQ(CK_OK, input.AddAnimation(a)); ASSERT_EQ(CK_OK, output.AddAnimation(b));
    input.SetLength(4); output.SetLength(4); input.SetCurrentStep(.25f);
    CKAnimation *api = &result;
    // The two native candidate errors are +10*2^-26 and -10*2^-26.
    // Rounding both dots to float first incorrectly ties them at zero.
    EXPECT_EQ(1.0f, api->CreateTransition(&input, &output, CK_TRANSITION_WARPTOBEST, 2, 0));
    ASSERT_EQ(1, result.GetAnimationCount());
    VxQuaternion endpoint;
    ASSERT_TRUE(result.GetAnimation(0)->EvaluateRotation(2, endpoint));
    EXPECT_NEAR(small, endpoint.x * endpoint.w, 0.0000001f);
    api->SetFrame(2);
    VxQuaternion actual; entity->GetQuaternion(&actual);
    EXPECT_NEAR(small, actual.x * actual.w, 0.0000001f);
}

TEST_F(KeyedAnimationCompositionCompletionTest, TransitionPrunesAnActualNullRemapBeforeReusingTheLiveChannelAndPose) {
    // The old SDK deletion check faults on the legitimately remapped null.
    // A subprocess makes that before failure reportable without replacing
    // the real transition, its reuse/lifetime checks, or its pose consumer.
    ASSERT_EXIT({
        const int result = TransitionAfterNullRemap();
        if (result)
            std::fprintf(stderr, "transition scenario failed at %d\n", result);
        std::exit(result);
    }, ::testing::ExitedWithCode(0), "");
}

} // namespace
