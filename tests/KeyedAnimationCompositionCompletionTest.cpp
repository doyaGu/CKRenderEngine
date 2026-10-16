#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include "CKAll.h"
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


};

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


} // namespace
