#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class AnimationFlagsProbe : public RCKAnimation {
public:
    explicit AnimationFlagsProbe(CKContext *context) : RCKAnimation(context) {}
    void SeedFlags(CKDWORD word) { m_Flags = word; }
    CKDWORD RawFlags() const { return m_Flags; }
    void Associate(RCKCharacter *character) { m_Character = character; }
    void SeedPlayback() { m_Length = 41.0f; m_Step = .375f; m_FrameRate = 27.0f; }
    void ExpectPlayback() const {
        EXPECT_FLOAT_EQ(41.0f, m_Length);
        EXPECT_FLOAT_EQ(.375f, m_Step);
        EXPECT_FLOAT_EQ(27.0f, m_FrameRate);
    }
};

class KeyedFlagsProbe : public RCKKeyedAnimation {
public:
    explicit KeyedFlagsProbe(CKContext *context) : RCKKeyedAnimation(context) {}
    void SeedFlags(CKDWORD word) { m_Flags = word; }
    CKDWORD RawFlags() const { return m_Flags; }
    void Associate(RCKCharacter *character) { m_Character = character; }
};

class AnimationFlagsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(AnimationFlagsTest, SetFlagsReplacesCompleteWordWithoutChangingPlaybackOrCharacter) {
    RCKCharacter character(Context);
    AnimationFlagsProbe animation(Context);
    animation.SeedPlayback();
    animation.Associate(&character);
    CKAnimation *api = &animation;
    api->SetFlags(0xFFFFFFFFu);
    EXPECT_EQ(0xFFFFFFFFu, animation.RawFlags());
    api->SetFlags(0x80010010u);
    EXPECT_EQ(0x80010010u, animation.RawFlags());
    api->SetFlags(0);
    EXPECT_EQ(0u, animation.RawFlags());
    animation.ExpectPlayback();
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), api->GetCharacter());
}

TEST_F(AnimationFlagsTest, GetFlagsReadsIndependentStorageIncludingUnknownBits) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0);
    EXPECT_EQ(0u, api->GetFlags());
    for (unsigned bit = 0; bit < 32; ++bit) {
        SCOPED_TRACE(bit);
        animation.SeedFlags(CKDWORD(1u) << bit);
        EXPECT_EQ(CKDWORD(1u) << bit, api->GetFlags());
    }
    animation.SeedFlags(0xA55AA55Au);
    EXPECT_EQ(0xA55AA55Au, api->GetFlags());
}

TEST_F(AnimationFlagsTest, TransitionWriterReplacesModeButPreservesSecondaryAndPresetFlags) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFFFFFFFFu);
    api->SetTransitionMode(CK_TRANSITION_WARPSAMEPOS);
    EXPECT_EQ(0xFFFF02FFu, animation.RawFlags());
    api->SetTransitionMode(static_cast<CK_ANIMATION_TRANSITION_MODE>(
        CK_TRANSITION_WARPBEST | CK_TRANSITION_USEVELOCITY | CK_TRANSITION_LOOPIFEQUAL));
    EXPECT_EQ(0xFFFEE2FFu, animation.RawFlags());
    api->SetTransitionMode(static_cast<CK_ANIMATION_TRANSITION_MODE>(0));
    EXPECT_EQ(0xFFFE00FFu, animation.RawFlags());
    animation.SeedFlags(0);
    api->SetTransitionMode(CK_TRANSITION_WARPSTART);
    EXPECT_EQ(0x00001200u, animation.RawFlags());
}

TEST_F(AnimationFlagsTest, TransitionReaderExtractsNineBitsWithoutIncludingPresetOrAdjacentFlags) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFFFE00FFu);
    EXPECT_EQ(0, static_cast<int>(api->GetTransitionMode()));
    animation.SeedFlags(0xFFFFFFFFu);
    EXPECT_EQ(0x1FF, static_cast<int>(api->GetTransitionMode()));
    animation.SeedFlags(0x81010211u);
    EXPECT_EQ(0x102, static_cast<int>(api->GetTransitionMode()));
}

TEST_F(AnimationFlagsTest, SecondaryWriterReplacesSparseBitsButPreservesReservedHolesAndPreset) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFFFFFFFFu);
    api->SetSecondaryAnimationMode(CKSECONDARYANIMATION_LOOP);
    EXPECT_EQ(0xFF1BFFFFu, animation.RawFlags());
    api->SetSecondaryAnimationMode(static_cast<CK_SECONDARYANIMATION_FLAGS>(
        CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_DOWARP));
    EXPECT_EQ(0xFFD3FFFFu, animation.RawFlags());
    api->SetSecondaryAnimationMode(static_cast<CK_SECONDARYANIMATION_FLAGS>(0));
    EXPECT_EQ(0xFF13FFFFu, animation.RawFlags());
    animation.SeedFlags(0);
    api->SetSecondaryAnimationMode(CKSECONDARYANIMATION_ONESHOT);
    EXPECT_EQ(0x00040000u, animation.RawFlags());
}

TEST_F(AnimationFlagsTest, SecondaryReaderOmitsReservedHolesAndPreset) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFF13FFFFu);
    EXPECT_EQ(0, static_cast<int>(api->GetSecondaryAnimationMode()));
    animation.SeedFlags(0xFFFFFFFFu);
    EXPECT_EQ(0xEC, static_cast<int>(api->GetSecondaryAnimationMode()));
    animation.SeedFlags(0x02341215u);
    EXPECT_EQ(0x24, static_cast<int>(api->GetSecondaryAnimationMode()));
}

TEST_F(AnimationFlagsTest, InterruptionWriterChangesOnlyBitTwoForAnyNonzeroInput) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFFFFFFFFu);
    api->SetCanBeInterrupt(FALSE);
    EXPECT_EQ(0xFFFFFFFBu, animation.RawFlags());
    api->SetCanBeInterrupt(-7);
    EXPECT_EQ(0xFFFFFFFFu, animation.RawFlags());
    animation.SeedFlags(0xA55AA551u);
    api->SetCanBeInterrupt(16);
    EXPECT_EQ(0xA55AA555u, animation.RawFlags());
    api->SetCanBeInterrupt(FALSE);
    EXPECT_EQ(0xA55AA551u, animation.RawFlags());
}

TEST_F(AnimationFlagsTest, InterruptionReaderReturnsRawMaskRatherThanNormalizedTrue) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFFFFFFFBu);
    EXPECT_EQ(0, api->CanBeInterrupt());
    animation.SeedFlags(4);
    EXPECT_EQ(4, api->CanBeInterrupt());
    animation.SeedFlags(0xFFFFFFFFu);
    EXPECT_EQ(4, api->CanBeInterrupt());
}

TEST_F(AnimationFlagsTest, OrientationWriterChangesOnlyBitFourForAnyNonzeroInput) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFFFFFFFFu);
    api->SetCharacterOrientation(FALSE);
    EXPECT_EQ(0xFFFFFFEFu, animation.RawFlags());
    api->SetCharacterOrientation(-2);
    EXPECT_EQ(0xFFFFFFFFu, animation.RawFlags());
    animation.SeedFlags(0xA55AA505u);
    api->SetCharacterOrientation(4);
    EXPECT_EQ(0xA55AA515u, animation.RawFlags());
    api->SetCharacterOrientation(FALSE);
    EXPECT_EQ(0xA55AA505u, animation.RawFlags());
}

TEST_F(AnimationFlagsTest, OrientationReaderReturnsRawMaskRatherThanNormalizedTrue) {
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    animation.SeedFlags(0xFFFFFFEFu);
    EXPECT_EQ(0, api->DoesCharacterTakeOrientation());
    animation.SeedFlags(0x10);
    EXPECT_EQ(0x10, api->DoesCharacterTakeOrientation());
    animation.SeedFlags(0xFFFFFFFFu);
    EXPECT_EQ(0x10, api->DoesCharacterTakeOrientation());
}

TEST_F(AnimationFlagsTest, CharacterReaderReturnsExactAssociatedCharacterOrNull) {
    RCKCharacter first(Context), second(Context);
    AnimationFlagsProbe animation(Context);
    CKAnimation *api = &animation;
    EXPECT_EQ(nullptr, api->GetCharacter());
    animation.Associate(&first);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&first), api->GetCharacter());
    animation.Associate(&second);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&second), api->GetCharacter());
    animation.Associate(nullptr);
    EXPECT_EQ(nullptr, api->GetCharacter());
}

TEST_F(AnimationFlagsTest, KeyedAnimationInheritsIndependentPackedStateOperations) {
    RCKCharacter character(Context);
    KeyedFlagsProbe animation(Context);
    animation.Associate(&character);
    animation.SeedFlags(0x83100041u);
    CKAnimation *api = &animation;
    api->SetTransitionMode(CK_TRANSITION_WARPBEST);
    api->SetSecondaryAnimationMode(static_cast<CK_SECONDARYANIMATION_FLAGS>(
        CKSECONDARYANIMATION_LOOP | CKSECONDARYANIMATION_DOWARP));
    api->SetCanBeInterrupt(TRUE);
    api->SetCharacterOrientation(TRUE);
    EXPECT_EQ(0x83982255u, animation.RawFlags());
    EXPECT_EQ(0x83982255u, api->GetFlags());
    EXPECT_EQ(0x22, static_cast<int>(api->GetTransitionMode()));
    EXPECT_EQ(0x88, static_cast<int>(api->GetSecondaryAnimationMode()));
    EXPECT_EQ(4, api->CanBeInterrupt());
    EXPECT_EQ(0x10, api->DoesCharacterTakeOrientation());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), api->GetCharacter());
    EXPECT_EQ(0, animation.GetAnimationCount());
    EXPECT_FLOAT_EQ(.5f, animation.GetMergeFactor());
}

} // namespace
