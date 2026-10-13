#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "CKAll.h"
#include "CKDependencies.h"
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

class RegistrationCharacterProbe : public RCKCharacter {
public:
    explicit RegistrationCharacterProbe(CKContext *context) : RCKCharacter(context) {}
    static CKSecondaryAnimation *Slots(RCKCharacter &character) {
        auto member = &RegistrationCharacterProbe::m_SecondaryAnimations;
        return character.*member;
    }
    static CKWORD Capacity(RCKCharacter &character) {
        auto member = &RegistrationCharacterProbe::m_SecondaryAnimationsAllocated;
        return character.*member;
    }
    static CKDWORD Reserved(RCKCharacter &character) {
        auto member = &RegistrationCharacterProbe::field_1D4;
        return character.*member;
    }
    void Reserved(CKDWORD bits) { field_1D4 = bits; }

    // Member pointers identify actual storage in any real RCKCharacter, with
    // no downcast to the test probe and no native offset imposed on SDL.
    static void RecordLayout(RCKCharacter &character) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&character);
        std::size_t previousEnd = 0;
#define RECORD_FIELD(field) \
        { \
            auto member = &RegistrationCharacterProbe::field; \
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(character.*member)); \
            const std::size_t offset = static_cast<std::size_t>(address - begin); \
            EXPECT_GE(offset, previousEnd) << #field; \
            EXPECT_EQ(0u, address % alignof(decltype(character.*member))) << #field; \
            EXPECT_LE(offset + sizeof(character.*member), sizeof(RCKCharacter)) << #field; \
            ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
            ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(character.*member))); \
            previousEnd = offset + sizeof(character.*member); \
        }
        RECORD_FIELD(m_BodyParts);
        RECORD_FIELD(m_Animations);
        RECORD_FIELD(m_SecondaryAnimations);
        RECORD_FIELD(m_SecondaryAnimationsCount);
        RECORD_FIELD(m_SecondaryAnimationsAllocated);
        RECORD_FIELD(m_RootBodyPart);
        RECORD_FIELD(m_ActiveAnimation);
        RECORD_FIELD(m_AnimDest);
        RECORD_FIELD(m_Warper);
        RECORD_FIELD(m_FrameDest);
        RECORD_FIELD(field_1D4);
        RECORD_FIELD(m_FloorRef);
        RECORD_FIELD(m_AnimationLevelOfDetail);
        RECORD_FIELD(m_FrameSrc);
        RECORD_FIELD(m_AnimSrc);
        RECORD_FIELD(m_TransitionMode);
#undef RECORD_FIELD
        ::testing::Test::RecordProperty("SDLCharacterSize", static_cast<int>(sizeof(RCKCharacter)));
        ::testing::Test::RecordProperty("SDLCharacterAlignment", static_cast<int>(alignof(RCKCharacter)));
        ::testing::Test::RecordProperty("SDL3dEntitySize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("ClassIDStaticValue", static_cast<int>(m_ClassID));
    }
};

class CharacterRegistrationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCKCharacter *Character() { return static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER)); }
    CKBodyPart *Part() { return CKBodyPart::Cast(Context->CreateObject(CKCID_BODYPART)); }
    CKKeyedAnimation *Animation() {
        CKKeyedAnimation *animation = CKKeyedAnimation::Cast(Context->CreateObject(CKCID_KEYEDANIMATION));
        if (animation) animation->SetLength(10);
        return animation;
    }
    static CKCharacter *SDK(RCKCharacter *character) { return reinterpret_cast<CKCharacter *>(character); }
    static int OwnedStorage(RCKCharacter &character) {
        return character.GetMemoryOccupation() - character.RCK3dEntity::GetMemoryOccupation();
    }
    static int StaticStorage() { return static_cast<int>(sizeof(RCKCharacter) - sizeof(RCK3dEntity)); }
    static void DestroyOnly(CKContext &context, CKObject *object) {
        CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, context.DestroyObject(object, 0, &dependencies));
    }
};

TEST_F(CharacterRegistrationTest, RegisteredFactoryDispatchesSDKIdentityAndClassMetadata) {
    const int initialCount = Context->GetObjectsCountByClassID(CKCID_CHARACTER);
    CKObject *object = Context->CreateObject(CKCID_CHARACTER, "Registered character");
    ASSERT_NE(nullptr, object); CKCharacter *character = CKCharacter::Cast(object); ASSERT_NE(nullptr, character);
    EXPECT_EQ(CKCID_CHARACTER, character->GetClassID());
    EXPECT_EQ(object, Context->GetObject(character->GetID()));
    EXPECT_EQ(initialCount + 1, Context->GetObjectsCountByClassID(CKCID_CHARACTER));
    EXPECT_TRUE(CKIsChildClassOf(object, CKCID_3DENTITY));
    EXPECT_STREQ("Character", RCKCharacter::GetClassName());
    CKClassDesc *description = CKGetClassDesc(CKCID_CHARACTER); ASSERT_NE(nullptr, description);
    EXPECT_STREQ("Character", description->NameFct());
    EXPECT_EQ(CKCID_3DENTITY, description->Parent);
    character->SetAnimationLevelOfDetail(.375f); EXPECT_FLOAT_EQ(.375f, character->GetAnimationLevelOfDetail());
    EXPECT_EQ(nullptr, character->GetRootBodyPart()); EXPECT_EQ(0, character->GetBodyPartCount());
    EXPECT_EQ(0, character->GetAnimationCount()); EXPECT_NE(nullptr, character->GetWarper());
}

TEST_F(CharacterRegistrationTest, DirectFactoryUsesContextIdentityAndIndependentPrivateWarper) {
    CKCharacter *first = RCKCharacter::CreateInstance(Context), *second = RCKCharacter::CreateInstance(Context);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    EXPECT_NE(first->GetID(), second->GetID());
    EXPECT_EQ(first, reinterpret_cast<CKCharacter *>(Context->GetObject(first->GetID())));
    EXPECT_EQ(second, reinterpret_cast<CKCharacter *>(Context->GetObject(second->GetID())));
    EXPECT_EQ(CKCID_CHARACTER, first->GetClassID()); EXPECT_EQ(nullptr, first->GetName());
    CKAnimation *firstWarp = first->GetWarper(), *secondWarp = second->GetWarper();
    ASSERT_NE(nullptr, firstWarp); ASSERT_NE(nullptr, secondWarp); EXPECT_NE(firstWarp, secondWarp);
    EXPECT_EQ(first, firstWarp->GetCharacter()); EXPECT_EQ(second, secondWarp->GetCharacter());
    const CK_ID firstID = first->GetID();
    // Direct factories register the object identity during construction; class
    // list finalization belongs to Context::CreateObject, so delete directly.
    delete reinterpret_cast<RCKCharacter *>(first);
    EXPECT_EQ(nullptr, Context->GetObject(firstID)); EXPECT_EQ(nullptr, firstWarp->GetCharacter());
    EXPECT_EQ(second, secondWarp->GetCharacter());
    delete reinterpret_cast<RCKCharacter *>(second);
}

TEST_F(CharacterRegistrationTest, RegisterPopulatesNotificationsParameterAndCopyDependencyMask) {
    CKClassDesc *description = CKGetClassDesc(CKCID_CHARACTER); ASSERT_NE(nullptr, description);
    description->ToBeNotify.Clear(); description->Parameter = CKGUID(); description->DefaultCopyDependencies = 0;
    RCKCharacter::Register();
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_ANIMATION));
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_KEYEDANIMATION));
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_BODYPART));
    EXPECT_EQ(CKPGUID_CHARACTER, description->Parameter);
    EXPECT_EQ(1u, description->DefaultCopyDependencies);
    CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
    EXPECT_EQ(1u, dependencies.GetClassDependencies(CKCID_CHARACTER));
}

TEST_F(CharacterRegistrationTest, DependencyOptionsExposeOnlyTheAdvertisedCopyEntry) {
    CKClassDesc *description = CKGetClassDesc(CKCID_CHARACTER); ASSERT_NE(nullptr, description);
    const int modes[] = {CK_DEPENDENCIES_COPY, CK_DEPENDENCIES_DELETE, CK_DEPENDENCIES_REPLACE,
                         CK_DEPENDENCIES_SAVE, CK_DEPENDENCIES_BUILD, 0, -1, 99};
    for (int mode : modes) {
        const int count = mode == CK_DEPENDENCIES_COPY ? 1 : 0;
        EXPECT_EQ(count, RCKCharacter::GetDependenciesCount(mode)); EXPECT_EQ(count, description->DependsCountFct(mode));
        if (count) {
            EXPECT_STREQ("Share Animations", RCKCharacter::GetDependencies(0, mode));
            EXPECT_STREQ("Share Animations", description->DependsFct(0, mode));
        } else {
            EXPECT_EQ(nullptr, RCKCharacter::GetDependencies(0, mode));
        }
        EXPECT_EQ(nullptr, RCKCharacter::GetDependencies(-1, mode));
        EXPECT_EQ(nullptr, RCKCharacter::GetDependencies(1, mode));
    }
}

TEST_F(CharacterRegistrationTest, MemoryCountsPointerStorageWithoutRecursivelyCountingReferencedObjects) {
    RCKCharacter *character = Character(); ASSERT_NE(nullptr, character);
    CKBodyPart *first = Part(), *second = Part(); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    CKKeyedAnimation *a = Animation(), *b = Animation(), *c = Animation();
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b); ASSERT_NE(nullptr, c);
    EXPECT_EQ(StaticStorage(), OwnedStorage(*character));
    ASSERT_EQ(CK_OK, character->AddBodyPart(first)); ASSERT_EQ(CK_OK, character->AddBodyPart(second));
    ASSERT_EQ(CK_OK, character->AddAnimation(a)); ASSERT_EQ(CK_OK, character->AddAnimation(b));
    ASSERT_EQ(CK_OK, character->AddAnimation(c));
    EXPECT_EQ(StaticStorage() + 5 * static_cast<int>(sizeof(CKObject *)), OwnedStorage(*character));
    // Growing a referenced object's independent allocation must not grow the
    // character's own storage budget, unlike changing its pointer-list count.
    const int before = character->GetMemoryOccupation();
    first->SetName("A body name whose storage belongs to the referenced object");
    a->SetName("An animation name whose storage belongs to the referenced object");
    EXPECT_EQ(before, character->GetMemoryOccupation());
    ASSERT_EQ(CK_OK, character->RemoveBodyPart(second)); ASSERT_EQ(CK_OK, character->RemoveAnimation(c));
    EXPECT_EQ(StaticStorage() + 3 * static_cast<int>(sizeof(CKObject *)), OwnedStorage(*character));
}

TEST_F(CharacterRegistrationTest, MemoryIncludesAllocatedSecondaryStorageAfterFlushRetainsItsCapacity) {
    RCKCharacter *character = Character(); ASSERT_NE(nullptr, character);
    CKKeyedAnimation *animations[] = {Animation(), Animation(), Animation()};
    for (CKKeyedAnimation *animation : animations) {
        ASSERT_NE(nullptr, animation); ASSERT_EQ(CK_OK, character->AddAnimation(animation));
        ASSERT_EQ(CK_OK, character->PlaySecondaryAnimation(animation, 0, CKSECONDARYANIMATION_ONESHOT, 1));
    }
    EXPECT_EQ(3, character->GetSecondaryAnimationsCount());
    ASSERT_NE(nullptr, RegistrationCharacterProbe::Slots(*character));
    EXPECT_EQ(4, RegistrationCharacterProbe::Capacity(*character));
    const int budget = StaticStorage() + 3 * static_cast<int>(sizeof(CKObject *)) +
                       4 * static_cast<int>(sizeof(CKSecondaryAnimation));
    EXPECT_EQ(budget, OwnedStorage(*character));
    CKSecondaryAnimation *storage = RegistrationCharacterProbe::Slots(*character);
    character->FlushSecondaryAnimations(); EXPECT_EQ(0, character->GetSecondaryAnimationsCount());
    EXPECT_EQ(storage, RegistrationCharacterProbe::Slots(*character));
    EXPECT_EQ(4, RegistrationCharacterProbe::Capacity(*character)); EXPECT_EQ(budget, OwnedStorage(*character));
}

TEST_F(CharacterRegistrationTest, ActualFieldStorageAndDefaultsAreValidForTheCurrentArchitecture) {
    RCKCharacter *character = Character(); ASSERT_NE(nullptr, character);
    RegistrationCharacterProbe::RecordLayout(*character);
    EXPECT_EQ(nullptr, RegistrationCharacterProbe::Slots(*character));
    EXPECT_EQ(0, RegistrationCharacterProbe::Capacity(*character));
    EXPECT_EQ(0u, RegistrationCharacterProbe::Reserved(*character));
    EXPECT_EQ(nullptr, character->GetActiveAnimation()); EXPECT_EQ(nullptr, character->GetNextActiveAnimation());
    EXPECT_EQ(nullptr, character->GetFloorReferenceObject()); EXPECT_FLOAT_EQ(1, character->GetAnimationLevelOfDetail());
    CKDWORD mode = 99; CKAnimation *source = character->GetWarper(), *target = source; float from = -1, to = -1;
    character->GetWarperParameters(&mode, &source, &from, &target, &to);
    EXPECT_EQ(0u, mode); EXPECT_EQ(nullptr, source); EXPECT_EQ(nullptr, target); EXPECT_FLOAT_EQ(0, from); EXPECT_FLOAT_EQ(0, to);
}

TEST_F(CharacterRegistrationTest, PublicPlaybackSelectionUsesTheObservedTransitionCacheAndRelations) {
    RCKCharacter *character = Character(); CKBodyPart *root = Part();
    CKKeyedAnimation *source = Animation(), *target = Animation();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, root); ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, target);
    ASSERT_EQ(CK_OK, character->AddBodyPart(root));
    ASSERT_EQ(CK_OK, character->AddAnimation(source)); ASSERT_EQ(CK_OK, character->AddAnimation(target));
    source->SetCanBeInterrupt(TRUE); source->SetFrame(3); ASSERT_EQ(CK_OK, character->SetActiveAnimation(source));
    ASSERT_EQ(CK_OK, character->SetNextActiveAnimation(target, CK_TRANSITION_WARPSTART, 2));
    CKDWORD mode = 0; CKAnimation *actualSource = nullptr, *actualTarget = nullptr; float from = -1, to = -1;
    character->GetWarperParameters(&mode, &actualSource, &from, &actualTarget, &to);
    EXPECT_EQ(static_cast<CKDWORD>(CK_TRANSITION_WARPSTART), mode); EXPECT_EQ(source, actualSource);
    EXPECT_EQ(target, actualTarget); EXPECT_FLOAT_EQ(3, from); EXPECT_FLOAT_EQ(0, to);
    EXPECT_EQ(character->GetWarper(), character->GetActiveAnimation()); EXPECT_EQ(target, character->GetNextActiveAnimation());
    EXPECT_FLOAT_EQ(2, character->GetWarper()->GetLength());
    character->SetFloorReferenceObject(reinterpret_cast<CK3dEntity *>(root));
    EXPECT_EQ(root, character->GetRootBodyPart()); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(root), character->GetFloorReferenceObject());
    character->SetAnimationLevelOfDetail(.5f); EXPECT_FLOAT_EQ(.5f, character->GetAnimationLevelOfDetail());
}

TEST_F(CharacterRegistrationTest, OpaqueReservedWordInitializesToZeroAndCopyPreservesItsBits) {
    RegistrationCharacterProbe source(Context), target(Context);
    EXPECT_EQ(0u, RegistrationCharacterProbe::Reserved(source)); EXPECT_EQ(0u, RegistrationCharacterProbe::Reserved(target));
    source.Reserved(0xC1234567u);
    CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
    ASSERT_EQ(CK_OK, target.Copy(source, dependencies));
    EXPECT_EQ(0xC1234567u, RegistrationCharacterProbe::Reserved(source));
    EXPECT_EQ(0xC1234567u, RegistrationCharacterProbe::Reserved(target));
    EXPECT_NE(source.GetID(), target.GetID()); EXPECT_NE(source.GetWarper(), target.GetWarper());
}

TEST_F(CharacterRegistrationTest, RegisteredIdentityRejectsUnsafeCppCopyAndMove) {
    EXPECT_FALSE(std::is_copy_constructible<RCKCharacter>::value);
    EXPECT_FALSE(std::is_copy_assignable<RCKCharacter>::value);
    EXPECT_FALSE(std::is_move_constructible<RCKCharacter>::value);
    EXPECT_FALSE(std::is_move_assignable<RCKCharacter>::value);
}

TEST_F(CharacterRegistrationTest, ContextCopyCreatesIndependentIdentityWarperAndSecondaryStorage) {
    RCKCharacter *source = Character(); ASSERT_NE(nullptr, source);
    CKBodyPart *body = Part(); CKKeyedAnimation *primary = Animation(), *secondary = Animation();
    ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, primary); ASSERT_NE(nullptr, secondary);
    ASSERT_EQ(CK_OK, source->AddBodyPart(body)); ASSERT_EQ(CK_OK, source->AddAnimation(primary));
    ASSERT_EQ(CK_OK, source->AddAnimation(secondary)); ASSERT_EQ(CK_OK, source->SetActiveAnimation(primary));
    ASSERT_EQ(CK_OK, source->PlaySecondaryAnimation(secondary, 0, CKSECONDARYANIMATION_ONESHOT, 1));
    CKSecondaryAnimation *sourceStorage = RegistrationCharacterProbe::Slots(*source); ASSERT_NE(nullptr, sourceStorage);
    CKCharacter *copy = CKCharacter::Cast(Context->CopyObject(source)); ASSERT_NE(nullptr, copy);
    RCKCharacter *implementation = reinterpret_cast<RCKCharacter *>(copy);
    EXPECT_NE(source->GetID(), copy->GetID()); EXPECT_EQ(copy, reinterpret_cast<CKCharacter *>(Context->GetObject(copy->GetID())));
    EXPECT_NE(source->GetWarper(), copy->GetWarper()); EXPECT_EQ(copy, copy->GetWarper()->GetCharacter());
    ASSERT_EQ(1, copy->GetBodyPartCount()); EXPECT_NE(body, copy->GetBodyPart(0));
    ASSERT_EQ(2, copy->GetAnimationCount()); EXPECT_NE(primary, copy->GetAnimation(0)); EXPECT_NE(secondary, copy->GetAnimation(1));
    EXPECT_EQ(0, copy->GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, RegistrationCharacterProbe::Slots(*implementation));
    ASSERT_EQ(CK_OK, copy->PlaySecondaryAnimation(copy->GetAnimation(1), 0, CKSECONDARYANIMATION_ONESHOT, 1));
    ASSERT_NE(nullptr, RegistrationCharacterProbe::Slots(*implementation));
    EXPECT_NE(sourceStorage, RegistrationCharacterProbe::Slots(*implementation));
    copy->FlushSecondaryAnimations(); EXPECT_EQ(0, copy->GetSecondaryAnimationsCount()); EXPECT_EQ(1, source->GetSecondaryAnimationsCount());
    EXPECT_EQ(sourceStorage, RegistrationCharacterProbe::Slots(*source));
}

TEST_F(CharacterRegistrationTest, RegisteredDeletionNotificationsReleaseBodyAndAnimationReferences) {
    RCKCharacter *character = Character(); CKBodyPart *body = Part(); CKKeyedAnimation *animation = Animation();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, animation);
    ASSERT_EQ(CK_OK, character->AddBodyPart(body)); ASSERT_EQ(CK_OK, character->AddAnimation(animation));
    ASSERT_EQ(CK_OK, character->SetActiveAnimation(animation));
    DestroyOnly(*Context, animation);
    EXPECT_EQ(0, character->GetAnimationCount()); EXPECT_EQ(nullptr, character->GetActiveAnimation());
    DestroyOnly(*Context, body);
    EXPECT_EQ(0, character->GetBodyPartCount()); EXPECT_EQ(nullptr, character->GetRootBodyPart());
}

} // namespace
