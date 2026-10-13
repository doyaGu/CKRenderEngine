#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include "CKAll.h"
#include "RCKAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class AnimationFieldProbe : public RCKAnimation {
public:
    explicit AnimationFieldProbe(CKContext *context) : RCKAnimation(context) {}

    enum Field { Character, Length, Step, Root, Flags, FrameRate, FieldCount };

    std::size_t Offset(Field field) const {
        const void *addresses[] = {&m_Character, &m_Length, &m_Step,
                                   &m_RootEntity, &m_Flags, &m_FrameRate};
        return static_cast<const unsigned char *>(addresses[field]) -
               reinterpret_cast<const unsigned char *>(this);
    }

    template <class T>
    T Stored(Field field) const {
        T value;
        std::memcpy(&value, reinterpret_cast<const unsigned char *>(this) + Offset(field), sizeof(value));
        return value;
    }
};

// Independent chunk-version 7 bytes. Payload links are DWORD offsets; the ID
// sequence is independent of Animation::Save and StateChunk::WriteObject.
void LoadFieldWire(CKStateChunk &chunk, const CKDWORD *payload, int count,
                   const int *ids = nullptr, int idCount = 0) {
    CKDWORD wire[48] = {};
    wire[0] = (idCount ? 0x01000000u : 0u) | 0x0007100au;
    wire[1] = count;
    std::memcpy(wire + 2, payload, count * sizeof(CKDWORD));
    int position = count + 2;
    if (idCount)
        wire[position++] = idCount;
    for (int i = 0; i < idCount; ++i)
        wire[position++] = ids[i];
    ASSERT_TRUE(chunk.ConvertFromBuffer(wire));
}

class AnimationFieldsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(AnimationFieldsTest, ConstructorInitializesSixInstanceSlotsAndPublicClock) {
    AnimationFieldProbe animation(Context);
    EXPECT_EQ(nullptr, animation.Stored<RCKCharacter *>(AnimationFieldProbe::Character));
    EXPECT_EQ(nullptr, animation.Stored<RCK3dEntity *>(AnimationFieldProbe::Root));
    EXPECT_FLOAT_EQ(100.0f, animation.Stored<float>(AnimationFieldProbe::Length));
    EXPECT_FLOAT_EQ(0.0f, animation.Stored<float>(AnimationFieldProbe::Step));
    EXPECT_EQ(5u, animation.Stored<CKDWORD>(AnimationFieldProbe::Flags));
    EXPECT_FLOAT_EQ(30.0f, animation.Stored<float>(AnimationFieldProbe::FrameRate));
    EXPECT_FLOAT_EQ(100.0f, animation.GetLength());
    EXPECT_FLOAT_EQ(0.0f, animation.GetFrame());
    EXPECT_FLOAT_EQ(15.0f, animation.GetNextFrame(500.0f));
}

TEST_F(AnimationFieldsTest, ClockMutatorsWriteIndependentLengthStepAndFrameRateSlots) {
    AnimationFieldProbe animation(Context);
    CKAnimation *api = &animation;
    api->SetFlags(0xA5120515u);
    api->SetLength(64.0f);
    api->SetFrame(24.0f);
    api->LinkToFrameRate(FALSE, -12.0f);
    EXPECT_FLOAT_EQ(64.0f, animation.Stored<float>(AnimationFieldProbe::Length));
    EXPECT_FLOAT_EQ(0.375f, animation.Stored<float>(AnimationFieldProbe::Step));
    EXPECT_FLOAT_EQ(-12.0f, animation.Stored<float>(AnimationFieldProbe::FrameRate));
    EXPECT_EQ(0xA5120514u, animation.Stored<CKDWORD>(AnimationFieldProbe::Flags));
    EXPECT_FLOAT_EQ(25.0f, api->GetNextFrame(500.0f));
    api->LinkToFrameRate(TRUE, -12.0f);
    EXPECT_FLOAT_EQ(18.0f, api->GetNextFrame(500.0f));
    api->SetCurrentStep(-0.5f);
    EXPECT_FLOAT_EQ(-0.5f, animation.Stored<float>(AnimationFieldProbe::Step));
    EXPECT_FLOAT_EQ(-32.0f, api->GetFrame());
    EXPECT_FLOAT_EQ(64.0f, animation.Stored<float>(AnimationFieldProbe::Length));
    EXPECT_FLOAT_EQ(-12.0f, animation.Stored<float>(AnimationFieldProbe::FrameRate));
}

TEST_F(AnimationFieldsTest, PackedModesShareFlagsSlotAndPreserveClockSlots) {
    AnimationFieldProbe animation(Context);
    animation.SetLength(31.0f);
    animation.SetStep(0.25f);
    animation.LinkToFrameRate(FALSE, 19.0f);
    animation.SetFlags(0xFFFFFFFFu);
    animation.SetTransitionMode(CK_TRANSITION_WARPSTART);
    EXPECT_EQ(0xFFFE12FFu, animation.Stored<CKDWORD>(AnimationFieldProbe::Flags));
    animation.SetSecondaryAnimationMode(CKSECONDARYANIMATION_ONESHOT);
    EXPECT_EQ(0xFF1612FFu, animation.Stored<CKDWORD>(AnimationFieldProbe::Flags));
    animation.SetCanBeInterrupt(FALSE);
    animation.SetCharacterOrientation(FALSE);
    animation.LinkToFrameRate(FALSE, 19.0f);
    EXPECT_EQ(0xFF1612EAu, animation.Stored<CKDWORD>(AnimationFieldProbe::Flags));
    EXPECT_FLOAT_EQ(31.0f, animation.Stored<float>(AnimationFieldProbe::Length));
    EXPECT_FLOAT_EQ(0.25f, animation.Stored<float>(AnimationFieldProbe::Step));
    EXPECT_FLOAT_EQ(19.0f, animation.Stored<float>(AnimationFieldProbe::FrameRate));
    EXPECT_EQ(CK_TRANSITION_WARPSTART, animation.GetTransitionMode());
    EXPECT_EQ(CKSECONDARYANIMATION_ONESHOT, animation.GetSecondaryAnimationMode());
}

TEST_F(AnimationFieldsTest, IndependentModernPayloadRestoresEveryInstanceSlot) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationFieldProbe animation(Context);
    const CKDWORD payload[] = {
        0x10, 4, 0x81420514u, 0x41B00000u, // flags; 22 fps
        0x40, 7, 0x42440000u,              // 49 frames
        0x80, 11, 0, root.GetID(),
        0x100, 14, character.GetID(),
        0x200, 0, 0x3F200000u              // step 0.625
    };
    const int ids[] = {-1, 10, 13};
    CKStateChunk chunk(CKCID_ANIMATION, nullptr);
    LoadFieldWire(chunk, payload, 17, ids, 3);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_EQ(&character, animation.Stored<RCKCharacter *>(AnimationFieldProbe::Character));
    EXPECT_EQ(&root, animation.Stored<RCK3dEntity *>(AnimationFieldProbe::Root));
    EXPECT_EQ(0x81420514u, animation.Stored<CKDWORD>(AnimationFieldProbe::Flags));
    EXPECT_FLOAT_EQ(22.0f, animation.Stored<float>(AnimationFieldProbe::FrameRate));
    EXPECT_FLOAT_EQ(49.0f, animation.Stored<float>(AnimationFieldProbe::Length));
    EXPECT_FLOAT_EQ(0.625f, animation.Stored<float>(AnimationFieldProbe::Step));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
    EXPECT_FLOAT_EQ(30.625f, animation.GetFrame());
}

TEST_F(AnimationFieldsTest, LegacySettingsModifyFlagSubfieldsWithoutDisturbingReferencesOrCursor) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationFieldProbe animation(Context);
    const CKDWORD references[] = {0x80, 4, 0, root.GetID(), 0x100, 0, character.GetID()};
    const int ids[] = {-1, 3, 6};
    CKStateChunk chunk(CKCID_ANIMATION, nullptr);
    LoadFieldWire(chunk, references, 7, ids, 3);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    animation.SetFlags(0x84220510u);
    animation.SetLength(37.0f);
    animation.SetCurrentStep(0.75f);
    // Settings size, rather than the product version, selects this legacy form.
    const CKDWORD legacy[] = {0x10, 0, 7, 0, 0x41880000u}; // interrupt, unlink, 17 fps
    LoadFieldWire(chunk, legacy, 5);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_EQ(0x84220514u, animation.Stored<CKDWORD>(AnimationFieldProbe::Flags));
    EXPECT_FLOAT_EQ(17.0f, animation.Stored<float>(AnimationFieldProbe::FrameRate));
    EXPECT_FLOAT_EQ(37.0f, animation.Stored<float>(AnimationFieldProbe::Length));
    EXPECT_FLOAT_EQ(0.75f, animation.Stored<float>(AnimationFieldProbe::Step));
    EXPECT_EQ(&root, animation.Stored<RCK3dEntity *>(AnimationFieldProbe::Root));
    EXPECT_EQ(&character, animation.Stored<RCKCharacter *>(AnimationFieldProbe::Character));
}

TEST_F(AnimationFieldsTest, ReferenceSlotsAreNonOwningAndDeletionChecksClearOnlyMarkedTarget) {
    CKObject *root = Context->CreateObject(CKCID_3DENTITY, "Root");
    CKObject *character = Context->CreateObject(CKCID_CHARACTER, "Character");
    ASSERT_NE(nullptr, root);
    ASSERT_NE(nullptr, character);
    const CK_ID rootId = root->GetID(), characterId = character->GetID();
    {
        AnimationFieldProbe animation(Context);
        const CKDWORD references[] = {0x80, 4, 0, rootId, 0x100, 0, characterId};
        const int ids[] = {-1, 3, 6};
        CKStateChunk chunk(CKCID_ANIMATION, nullptr);
        LoadFieldWire(chunk, references, 7, ids, 3);
        ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
        root->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
        animation.CheckPreDeletion();
        EXPECT_EQ(nullptr, animation.Stored<RCK3dEntity *>(AnimationFieldProbe::Root));
        EXPECT_EQ(character, reinterpret_cast<CKObject *>(
            animation.Stored<RCKCharacter *>(AnimationFieldProbe::Character)));
        root->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
        character->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
        animation.CheckPreDeletion();
        EXPECT_EQ(nullptr, animation.Stored<RCKCharacter *>(AnimationFieldProbe::Character));
        character->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
        // Destroy the animation while it holds both live references again.
        ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
        EXPECT_EQ(root, reinterpret_cast<CKObject *>(
            animation.Stored<RCK3dEntity *>(AnimationFieldProbe::Root)));
        EXPECT_EQ(character, reinterpret_cast<CKObject *>(
            animation.Stored<RCKCharacter *>(AnimationFieldProbe::Character)));
    }
    EXPECT_EQ(root, Context->GetObject(rootId));
    EXPECT_EQ(character, Context->GetObject(characterId));
    ASSERT_EQ(CK_OK, Context->DestroyObject(root));
    ASSERT_EQ(CK_OK, Context->DestroyObject(character));
}

TEST_F(AnimationFieldsTest, ClassIdentifierIsSharedStaticStorageOutsideInstances) {
    AnimationFieldProbe first(Context), second(Context);
    unsigned char firstBytes[sizeof(RCKAnimation)], secondBytes[sizeof(RCKAnimation)];
    std::memcpy(firstBytes, &first, sizeof(firstBytes));
    std::memcpy(secondBytes, &second, sizeof(secondBytes));
    const CK_CLASSID original = RCKAnimation::m_ClassID;
    EXPECT_EQ(CKCID_ANIMATION, original);
    RCKAnimation::m_ClassID = CKCID_KEYEDANIMATION;
    const CK_CLASSID firstResult = first.GetClassID(), secondResult = second.GetClassID();
    RCKAnimation::m_ClassID = original;
    EXPECT_EQ(CKCID_KEYEDANIMATION, firstResult);
    EXPECT_EQ(CKCID_KEYEDANIMATION, secondResult);
    EXPECT_EQ(0, std::memcmp(firstBytes, &first, sizeof(firstBytes)));
    EXPECT_EQ(0, std::memcmp(secondBytes, &second, sizeof(secondBytes)));
    EXPECT_EQ(&first.m_ClassID, &second.m_ClassID);
    EXPECT_EQ(CKCID_ANIMATION, first.GetClassID());
}

TEST_F(AnimationFieldsTest, RecordsActualPlatformLayoutWithoutDemandingNativeOffsets) {
    AnimationFieldProbe animation(Context);
    const char *names[] = {"m_Character", "m_Length", "m_Step", "m_RootEntity", "m_Flags", "m_FrameRate"};
    const std::size_t sizes[] = {sizeof(RCKCharacter *), sizeof(float), sizeof(float),
                               sizeof(RCK3dEntity *), sizeof(CKDWORD), sizeof(float)};
    RecordProperty("pointer_size", static_cast<int>(sizeof(void *)));
    RecordProperty("sizeof_RCKAnimation", static_cast<int>(sizeof(RCKAnimation)));
    RecordProperty("alignof_RCKAnimation", static_cast<int>(alignof(RCKAnimation)));
    RecordProperty("sizeof_CKAnimation", static_cast<int>(sizeof(CKAnimation)));
    for (int i = 0; i < AnimationFieldProbe::FieldCount; ++i) {
        const std::size_t offset = animation.Offset(static_cast<AnimationFieldProbe::Field>(i));
        RecordProperty(names[i], static_cast<int>(offset));
        EXPECT_LE(offset + sizes[i], sizeof(RCKAnimation));
        EXPECT_EQ(0u, offset % (i == AnimationFieldProbe::Character || i == AnimationFieldProbe::Root
                                  ? alignof(void *) : alignof(float)));
        if (i)
            EXPECT_GE(offset, animation.Offset(static_cast<AnimationFieldProbe::Field>(i - 1)) + sizes[i - 1]);
    }
    EXPECT_GE(animation.Offset(AnimationFieldProbe::Character), sizeof(CKAnimation));
    EXPECT_EQ(0u, reinterpret_cast<std::size_t>(&animation) % alignof(RCKAnimation));
    EXPECT_EQ(sizeof(RCKAnimation), sizeof(AnimationFieldProbe));
}

} // namespace
