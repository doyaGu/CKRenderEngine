#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class StorageCharacter : public RCKCharacter {
public:
    explicit StorageCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};

static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }

// Real animation curves and public ownership APIs exercise the storage consumer.
class StorageAnimationRig {
public:
    StorageCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation MainRoot, MainLimb, SecondaryLimb;
    RCKKeyedAnimation Main, Secondary;

    explicit StorageAnimationRig(CKContext *context)
        : Character(context), Root(context), Limb(context), MainRoot(context), MainLimb(context),
          SecondaryLimb(context), Main(context), Secondary(context) {
        Character.SetMoveableFlags(Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Limb.SetMoveableFlags(Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        EXPECT_TRUE(Limb.SetParent(Entity(Root), TRUE));
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Limb)));
        Curve(Main, MainRoot, Root, 0, 0);
        Curve(Main, MainLimb, Limb, 10, 20);
        Curve(Secondary, SecondaryLimb, Limb, 100, 180);
        Main.SetLength(10); Secondary.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Main));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Secondary));
        Main.LinkToFrameRate(TRUE, 0);
        Main.SetCanBeInterrupt(FALSE);
        Secondary.LinkToFrameRate(TRUE, 1000);
        EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Main));
        Main.SetFrame(1.25f);
    }

    ~StorageAnimationRig() {
        Character.FlushSecondaryAnimations();
        Limb.SetParent(nullptr, TRUE);
        Root.SetParent(nullptr, TRUE);
    }

private:
    static void Curve(RCKKeyedAnimation &animation, RCKObjectAnimation &channel, RCKBodyPart &body,
                      float first, float last) {
        channel.Set3dEntity(Entity(body));
        VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(10, &b);
        EXPECT_EQ(CK_OK, animation.AddAnimation(&channel));
    }
};

class SecondaryAnimationStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
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

    static CKDWORD Bits(float frame) {
        CKDWORD result;
        static_assert(sizeof(result) == sizeof(frame), "Frame storage requires a 32-bit float word");
        std::memcpy(&result, &frame, sizeof(result));
        return result;
    }

    static CKSecondaryAnimation Record(RCKKeyedAnimation &source, RCKKeyedAnimation &warper) {
        CKSecondaryAnimation slot = {};
        slot.AnimID = source.GetID();
        slot.Animation = &warper;
        slot.Flags = CKSECONDARYANIMATION_LOOPNTIMES | CKSECONDARYANIMATION_DOWARP;
        slot.WarpLength = 3.125f;
        slot.Padding = source.GetID();
        slot.StartingFrameBits = 0x3fa00000u; // 1.25, independently initialized storage.
        slot.Mode = CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP;
        slot.LoopCountRemaining = 7;
        return slot;
    }

    static void SameFields(const CKSecondaryAnimation &expected, const CKSecondaryAnimation &actual) {
        EXPECT_EQ(expected.AnimID, actual.AnimID);
        EXPECT_EQ(expected.Animation, actual.Animation);
        EXPECT_EQ(expected.Flags, actual.Flags);
        EXPECT_EQ(Bits(expected.WarpLength), Bits(actual.WarpLength));
        EXPECT_EQ(expected.Padding, actual.Padding);
        EXPECT_EQ(expected.StartingFrameBits, actual.StartingFrameBits);
        EXPECT_EQ(expected.Mode, actual.Mode);
        EXPECT_EQ(expected.LoopCountRemaining, actual.LoopCountRemaining);
    }

    static void LimbPose(StorageAnimationRig &rig, float expected) {
        EXPECT_NEAR(expected, rig.Limb.GetLocalMatrix()[3][0], 0.0001f);
        VxVector position;
        rig.Limb.GetPosition(&position, Entity(rig.Root));
        EXPECT_NEAR(expected, position.x, 0.0001f);
        EXPECT_FLOAT_EQ(0, position.y);
        EXPECT_FLOAT_EQ(0, position.z);
    }
};
}

TEST_F(SecondaryAnimationStorageTest, SetterStoresIndependentKnownIEEEFloatWords) {
    const float frames[] = {0.0f, -0.0f, 0.1f, 1.25f, 7.5f, -1.0f};
    const CKDWORD words[] = {0x00000000u, 0x80000000u, 0x3dcccccdu, 0x3fa00000u, 0x40f00000u, 0xbf800000u};
    CKSecondaryAnimation slot = {};
    for (int i = 0; i < 6; ++i) {
        SCOPED_TRACE(i);
        slot.SetStartingFrame(frames[i]);
        EXPECT_EQ(words[i], slot.StartingFrameBits);
    }
}

TEST_F(SecondaryAnimationStorageTest, GetterReadsIndependentKnownWordsIncludingSignedZero) {
    const CKDWORD words[] = {0x00000000u, 0x80000000u, 0x3e000000u, 0x3fa00000u, 0xc0200000u, 0xbf800000u};
    const float frames[] = {0.0f, -0.0f, 0.125f, 1.25f, -2.5f, -1.0f};
    CKSecondaryAnimation slot = {};
    for (int i = 0; i < 6; ++i) {
        SCOPED_TRACE(i);
        slot.StartingFrameBits = words[i];
        const float frame = slot.GetStartingFrame();
        EXPECT_FLOAT_EQ(frames[i], frame);
        EXPECT_EQ(words[i], Bits(frame));
    }
}

TEST_F(SecondaryAnimationStorageTest, SettingFramePreservesEveryOtherLiveField) {
    RCKKeyedAnimation source(Context), warper(Context);
    CKSecondaryAnimation slot = Record(source, warper);
    const CKSecondaryAnimation original = slot;
    slot.SetStartingFrame(-1.0f);
    CKSecondaryAnimation expected = original;
    expected.StartingFrameBits = 0xbf800000u;
    SameFields(expected, slot);
    slot.SetStartingFrame(-0.0f);
    expected.StartingFrameBits = 0x80000000u;
    SameFields(expected, slot);
}

TEST_F(SecondaryAnimationStorageTest, ValueCopiesKeepAllFieldsWithoutSharingSubsequentStorage) {
    RCKKeyedAnimation source(Context), warper(Context), other(Context);
    const CKSecondaryAnimation initial = Record(source, warper);
    CKSecondaryAnimation original = initial;
    CKSecondaryAnimation copied(original);
    CKSecondaryAnimation assigned = {};
    assigned = copied;
    SameFields(initial, copied);
    SameFields(initial, assigned);

    original.AnimID = other.GetID(); original.Animation = &other;
    original.Flags = CKSECONDARYANIMATION_ONESHOT;
    original.WarpLength = 0.5f; original.Padding = warper.GetID();
    original.SetStartingFrame(7.5f); original.Mode = CKSECONDARYANIMATIONRUNTIME_PLAYING;
    original.LoopCountRemaining = 2;
    SameFields(initial, copied);
    SameFields(initial, assigned);
    copied.SetStartingFrame(0.125f);
    EXPECT_EQ(0x40f00000u, original.StartingFrameBits);
    EXPECT_EQ(0x3e000000u, copied.StartingFrameBits);
    SameFields(initial, assigned);
}

TEST_F(SecondaryAnimationStorageTest, ArrayElementsUseSDLRecordStrideAndIndependentFrameWords) {
    RCKKeyedAnimation first(Context), second(Context), warper(Context);
    CKSecondaryAnimation slots[3] = {Record(first, warper), Record(second, first), Record(warper, second)};
    const CKSecondaryAnimation oldFirst = slots[0], oldThird = slots[2];
    const unsigned char *base = reinterpret_cast<const unsigned char *>(&slots[0]);
    EXPECT_EQ(sizeof(CKSecondaryAnimation), static_cast<std::size_t>(reinterpret_cast<const unsigned char *>(&slots[1]) - base));
    EXPECT_EQ(2 * sizeof(CKSecondaryAnimation), static_cast<std::size_t>(reinterpret_cast<const unsigned char *>(&slots[2]) - base));
    slots[1].SetStartingFrame(-1.0f);
    slots[1].Animation = nullptr;
    slots[1].Padding = 0;
    slots[1].Mode = CKSECONDARYANIMATIONRUNTIME_STARTINGWARP;
    EXPECT_EQ(0xbf800000u, slots[1].StartingFrameBits);
    SameFields(oldFirst, slots[0]);
    SameFields(oldThird, slots[2]);
    slots[0].LoopCountRemaining = 4;
    EXPECT_EQ(7u, slots[1].LoopCountRemaining);
    EXPECT_EQ(7u, slots[2].LoopCountRemaining);
}

TEST_F(SecondaryAnimationStorageTest, SDLLayoutUsesNativePointersAndTheExplicitLiveReturnTargetWord) {
    const bool widePointer = sizeof(void *) == 8;
    EXPECT_EQ(widePointer ? 40u : 32u, sizeof(CKSecondaryAnimation));
    EXPECT_EQ(widePointer ? 8u : 4u, alignof(CKSecondaryAnimation));
    EXPECT_EQ(0u, offsetof(CKSecondaryAnimation, AnimID));
    EXPECT_EQ(widePointer ? 8u : 4u, offsetof(CKSecondaryAnimation, Animation));
    EXPECT_EQ(widePointer ? 16u : 8u, offsetof(CKSecondaryAnimation, Flags));
    EXPECT_EQ(widePointer ? 20u : 12u, offsetof(CKSecondaryAnimation, WarpLength));
    EXPECT_EQ(widePointer ? 24u : 16u, offsetof(CKSecondaryAnimation, Padding));
    EXPECT_EQ(widePointer ? 28u : 20u, offsetof(CKSecondaryAnimation, StartingFrameBits));
    EXPECT_EQ(widePointer ? 32u : 24u, offsetof(CKSecondaryAnimation, Mode));
    EXPECT_EQ(widePointer ? 36u : 28u, offsetof(CKSecondaryAnimation, LoopCountRemaining));
    // Original 2.1 has seven DWORD-width fields. This portable record is deliberately different.
    EXPECT_NE(28u, sizeof(CKSecondaryAnimation));
}

TEST_F(SecondaryAnimationStorageTest, PublicDirectPlaybackStoresAndSamplesTheFractionalStartingFrame) {
    StorageAnimationRig rig(Context);
    ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 2.375f, CKSECONDARYANIMATION_LOOPNTIMES, 1.5f, 7));
    ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
    const CKSecondaryAnimation &slot = rig.Character.Slot();
    EXPECT_EQ(rig.Secondary.GetID(), slot.AnimID);
    EXPECT_EQ(nullptr, slot.Animation);
    EXPECT_EQ(CKSECONDARYANIMATION_LOOPNTIMES, slot.Flags);
    EXPECT_EQ(0u, slot.Padding);
    EXPECT_EQ(0x40180000u, slot.StartingFrameBits); // 2.375, independent expected word.
    EXPECT_FLOAT_EQ(2.375f, slot.GetStartingFrame());
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, slot.Mode);
    EXPECT_EQ(7u, slot.LoopCountRemaining);
    EXPECT_FLOAT_EQ(2.375f, rig.Secondary.GetFrame());
    EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation());
    LimbPose(rig, 119);
}

TEST_F(SecondaryAnimationStorageTest, StartingWarpReadsTheStoredFractionWhenHandingOverToTheRealSource) {
    StorageAnimationRig rig(Context);
    const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
        CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
    ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, 2.375f, flags, 1.5f, 3));
    ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
    const CKSecondaryAnimation &slot = rig.Character.Slot();
    RCKKeyedAnimation *warper = slot.Animation;
    ASSERT_NE(nullptr, warper);
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, slot.Mode);
    EXPECT_EQ(rig.Secondary.GetID(), slot.AnimID);
    EXPECT_EQ(static_cast<CKDWORD>(flags), slot.Flags);
    EXPECT_FLOAT_EQ(1.5f, slot.WarpLength);
    EXPECT_EQ(0u, slot.Padding);
    EXPECT_EQ(0x40180000u, slot.StartingFrameBits);
    EXPECT_EQ(3u, slot.LoopCountRemaining);
    ASSERT_EQ(1, warper->GetAnimationCount());
    CKObjectAnimation *channel = warper->GetAnimation(0); ASSERT_NE(nullptr, channel);
    VxVector endpoint;
    ASSERT_TRUE(channel->EvaluatePosition(1.5f, endpoint));
    EXPECT_NEAR(119, endpoint.x, 0.0001f);
    EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());

    // 2 ms at 1000 fps crosses the 1.5-frame warp by 0.5 frame.
    rig.Character.Tick(2);
    EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, slot.Mode);
    EXPECT_EQ(warper, slot.Animation);
    EXPECT_EQ(0x40180000u, slot.StartingFrameBits);
    EXPECT_FLOAT_EQ(2.875f, rig.Secondary.GetFrame());
    EXPECT_EQ(&rig.Secondary, rig.Limb.GetExclusiveAnimation());
    LimbPose(rig, 123);
}
