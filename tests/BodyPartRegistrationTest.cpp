#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
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

class RegistrationBodyProbe : public RCKBodyPart {
public:
    explicit RegistrationBodyProbe(CKContext *context) : RCKBodyPart(context) {}
    static void RecordLayout(RCKBodyPart &body) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&body);
        std::size_t previousEnd = 0;
#define RECORD_FIELD(field) \
        { \
            auto member = &RegistrationBodyProbe::field; \
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(body.*member)); \
            const std::size_t offset = static_cast<std::size_t>(address - begin); \
            EXPECT_GE(offset, previousEnd) << #field; \
            EXPECT_EQ(0u, address % alignof(decltype(body.*member))) << #field; \
            EXPECT_LE(offset + sizeof(body.*member), sizeof(RCKBodyPart)) << #field; \
            ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
            ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(body.*member))); \
            previousEnd = offset + sizeof(body.*member); \
        }
        RECORD_FIELD(m_Character);
        RECORD_FIELD(m_ExclusiveAnimation);
        RECORD_FIELD(m_RotationJoint);
#undef RECORD_FIELD
        ::testing::Test::RecordProperty("SDLBodyPartSize", static_cast<int>(sizeof(RCKBodyPart)));
        ::testing::Test::RecordProperty("SDLBodyPartAlignment", static_cast<int>(alignof(RCKBodyPart)));
        ::testing::Test::RecordProperty("SDL3dObjectSize", static_cast<int>(sizeof(RCK3dObject)));
        ::testing::Test::RecordProperty("SDL3dEntitySize", static_cast<int>(sizeof(RCK3dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("ClassIDStaticValue", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("RotationJointSize", static_cast<int>(sizeof(CKIkJoint)));
        ::testing::Test::RecordProperty("JointFlagsOffset", static_cast<int>(offsetof(CKIkJoint, m_Flags)));
        ::testing::Test::RecordProperty("JointMinOffset", static_cast<int>(offsetof(CKIkJoint, m_Min)));
        ::testing::Test::RecordProperty("JointMaxOffset", static_cast<int>(offsetof(CKIkJoint, m_Max)));
        ::testing::Test::RecordProperty("JointDampingOffset", static_cast<int>(offsetof(CKIkJoint, m_Damping)));
    }
};

class BodyPartRegistrationTest : public ::testing::Test {
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
    CKBodyPart *Body() { return CKBodyPart::Cast(Context->CreateObject(CKCID_BODYPART)); }
    CKCharacter *Character() { return CKCharacter::Cast(Context->CreateObject(CKCID_CHARACTER)); }
    CKKeyedAnimation *Animation() {
        CKKeyedAnimation *animation = CKKeyedAnimation::Cast(Context->CreateObject(CKCID_KEYEDANIMATION));
        if (animation) animation->SetLength(10);
        return animation;
    }
    static RCKBodyPart *Implementation(CKBodyPart *body) { return reinterpret_cast<RCKBodyPart *>(body); }
    static int OwnBytes(CKBodyPart *body) {
        RCKBodyPart *implementation = Implementation(body);
        return implementation->GetMemoryOccupation() - implementation->RCK3dEntity::GetMemoryOccupation();
    }
    static void DestroyOnly(CKContext &context, CKObject *object) {
        CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, context.DestroyObject(object, 0, &dependencies));
    }
    RCKObjectAnimation *Curve(CKBodyPart *body) {
        RCKObjectAnimation *channel = static_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION));
        if (!channel) return nullptr;
        channel->Set3dEntity(body);
        VxVector first(0, 0, 0), last(10, 0, 0);
        channel->AddPositionKey(0, &first); channel->AddPositionKey(10, &last); channel->SetLength(10);
        return channel;
    }
    static CKIkJoint Joint() {
        CKIkJoint joint;
        joint.m_Flags = CK_IKJOINT_ACTIVE | CK_IKJOINT_LIMIT_Z | CK_IKJOINT_EASE_Y;
        joint.m_Min.Set(-1, -1, -.25f); joint.m_Max.Set(1, 1, .25f);
        joint.m_Damping.Set(.125f, .25f, .5f);
        return joint;
    }
    static void ExpectJoint(const CKIkJoint &actual, const CKIkJoint &expected) {
        EXPECT_EQ(expected.m_Flags, actual.m_Flags);
        for (int axis = 0; axis < 3; ++axis) {
            EXPECT_FLOAT_EQ((&expected.m_Min.x)[axis], (&actual.m_Min.x)[axis]);
            EXPECT_FLOAT_EQ((&expected.m_Max.x)[axis], (&actual.m_Max.x)[axis]);
            EXPECT_FLOAT_EQ((&expected.m_Damping.x)[axis], (&actual.m_Damping.x)[axis]);
        }
    }
};

TEST_F(BodyPartRegistrationTest, RegisteredFactoryDispatchesSdkClassIdentityAndNativeClassName) {
    const int before = Context->GetObjectsCountByClassID(CKCID_BODYPART);
    CKObject *object = Context->CreateObject(CKCID_BODYPART, "Registered body");
    ASSERT_NE(nullptr, object); CKBodyPart *body = CKBodyPart::Cast(object); ASSERT_NE(nullptr, body);
    EXPECT_EQ(CKCID_BODYPART, body->GetClassID()); EXPECT_EQ(object, Context->GetObject(body->GetID()));
    EXPECT_EQ(before + 1, Context->GetObjectsCountByClassID(CKCID_BODYPART));
    EXPECT_TRUE(CKIsChildClassOf(body, CKCID_3DOBJECT)); EXPECT_TRUE(CKIsChildClassOf(body, CKCID_3DENTITY));
    EXPECT_STREQ("Body Part", RCKBodyPart::GetClassName());
    CKClassDesc *description = CKGetClassDesc(CKCID_BODYPART); ASSERT_NE(nullptr, description);
    EXPECT_STREQ("Body Part", description->NameFct()); EXPECT_EQ(CKCID_3DOBJECT, description->Parent);
    VxVector expected(3, 5, 7), actual; body->SetPosition(&expected); body->GetPosition(&actual);
    EXPECT_FLOAT_EQ(3, actual.x); EXPECT_FLOAT_EQ(5, actual.y); EXPECT_FLOAT_EQ(7, actual.z);
}

TEST_F(BodyPartRegistrationTest, DirectFactoryConstructsDistinctContextIdentitiesAndUnregistersOnDelete) {
    CKBodyPart *first = RCKBodyPart::CreateInstance(Context), *second = RCKBodyPart::CreateInstance(Context);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); EXPECT_NE(first->GetID(), second->GetID());
    EXPECT_EQ(first, reinterpret_cast<CKBodyPart *>(Context->GetObject(first->GetID())));
    EXPECT_EQ(second, reinterpret_cast<CKBodyPart *>(Context->GetObject(second->GetID())));
    EXPECT_EQ(CKCID_BODYPART, first->GetClassID()); EXPECT_EQ(nullptr, first->GetName());
    EXPECT_EQ(nullptr, first->GetCharacter()); EXPECT_EQ(nullptr, first->GetExclusiveAnimation());
    const CK_ID id = first->GetID();
    // A direct factory registers identity in construction; the context factory
    // separately finalizes its class list. Destroy a direct factory directly.
    delete Implementation(first); EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(second, reinterpret_cast<CKBodyPart *>(Context->GetObject(second->GetID())));
    delete Implementation(second);
}

TEST_F(BodyPartRegistrationTest, RegisterPublishesCharacterNotificationAndSdkParameterWithoutCopyOption) {
    CKClassDesc *description = CKGetClassDesc(CKCID_BODYPART); ASSERT_NE(nullptr, description);
    description->ToBeNotify.Clear(); description->Parameter = CKGUID(); description->DefaultCopyDependencies = 0x55;
    RCKBodyPart::Register();
    EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_CHARACTER));
    EXPECT_EQ(CKPGUID_BODYPART, description->Parameter); EXPECT_EQ(0x55u, description->DefaultCopyDependencies);
    EXPECT_EQ(0x4BA1010Eu, description->Parameter.d1); EXPECT_EQ(0x7ABD6D02u, description->Parameter.d2);
}

TEST_F(BodyPartRegistrationTest, DependencyMetadataAdvertisesNoOwnOptionsInEveryMode) {
    CKClassDesc *description = CKGetClassDesc(CKCID_BODYPART); ASSERT_NE(nullptr, description);
    const int modes[] = {CK_DEPENDENCIES_COPY, CK_DEPENDENCIES_DELETE, CK_DEPENDENCIES_REPLACE,
                         CK_DEPENDENCIES_SAVE, CK_DEPENDENCIES_BUILD, 0, -1, 99};
    for (int mode : modes) {
        EXPECT_EQ(0, RCKBodyPart::GetDependenciesCount(mode)); EXPECT_EQ(0, description->DependsCountFct(mode));
        for (int index = -1; index < 3; ++index) {
            EXPECT_EQ(nullptr, RCKBodyPart::GetDependencies(index, mode)); EXPECT_EQ(nullptr, description->DependsFct(index, mode));
        }
    }
}

TEST_F(BodyPartRegistrationTest, MemoryReportsActualInlineStorageWithoutRecursivelyCountingBorrowedObjects) {
    CKBodyPart *body = Body(); CKCharacter *character = Character(); CKKeyedAnimation *animation = Animation();
    ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, animation);
    const int inlineBytes = static_cast<int>(sizeof(RCKBodyPart) - sizeof(RCK3dEntity));
    EXPECT_EQ(inlineBytes, OwnBytes(body)); ASSERT_EQ(CK_OK, character->AddBodyPart(body));
    body->SetExclusiveAnimation(animation); const int before = body->GetMemoryOccupation();
    character->SetName("A longer character name allocated by the character rather than the body");
    animation->SetName("Animation name storage does not belong to its borrowed body reference");
    EXPECT_EQ(before, body->GetMemoryOccupation()); EXPECT_EQ(inlineBytes, OwnBytes(body));
    CKIkJoint joint = Joint(); body->SetRotationJoint(&joint); EXPECT_EQ(before, body->GetMemoryOccupation());
    body->SetName("Body-owned name allocation is included by the base memory reporter");
    EXPECT_EQ(inlineBytes, OwnBytes(body)); body->SetExclusiveAnimation(nullptr);
    ASSERT_EQ(CK_OK, character->RemoveBodyPart(body)); EXPECT_EQ(inlineBytes, OwnBytes(body));
}

TEST_F(BodyPartRegistrationTest, ActualFieldLayoutAndConstructorDefaultsSupportTheCurrentArchitecture) {
    CKBodyPart *body = Body(); ASSERT_NE(nullptr, body); RegistrationBodyProbe::RecordLayout(*Implementation(body));
    EXPECT_EQ(nullptr, body->GetCharacter()); EXPECT_EQ(nullptr, body->GetExclusiveAnimation());
    EXPECT_EQ(CKCID_BODYPART, RCKBodyPart::m_ClassID);
    CKIkJoint actual; body->GetRotationJoint(&actual); EXPECT_EQ(CK_IKJOINT_ACTIVE, actual.m_Flags);
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_FLOAT_EQ(0, (&actual.m_Min.x)[axis]); EXPECT_FLOAT_EQ(0, (&actual.m_Max.x)[axis]);
        EXPECT_FLOAT_EQ(0, (&actual.m_Damping.x)[axis]);
    }
}

TEST_F(BodyPartRegistrationTest, PublicOwnerTransferAndRegisteredDeletionFollowCharacterOwnedDependencies) {
    CKBodyPart *body = Body(); CKCharacter *first = Character(), *second = Character();
    ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    ASSERT_EQ(CK_OK, first->AddBodyPart(body)); EXPECT_EQ(first, body->GetCharacter());
    ASSERT_EQ(CK_OK, second->AddBodyPart(body)); EXPECT_EQ(second, body->GetCharacter());
    EXPECT_EQ(0, first->GetBodyPartCount()); EXPECT_EQ(1, second->GetBodyPartCount());
    const CK_ID bodyID = body->GetID(), firstID = first->GetID(), secondID = second->GetID();
    // Character always prepares its body list for DELETE, independently of
    // the optional class-dependency mask. Both identities are gone afterward;
    // the body's borrowed owner pointer does not make the body own its owner.
    DestroyOnly(*Context, second); EXPECT_EQ(nullptr, Context->GetObject(secondID));
    EXPECT_EQ(nullptr, Context->GetObject(bodyID));
    EXPECT_EQ(first, reinterpret_cast<CKCharacter *>(Context->GetObject(firstID)));
}

TEST_F(BodyPartRegistrationTest, BorrowedExclusiveIdentityControlsRealChannelPlaybackAndExplicitRelease) {
    CKBodyPart *body = Body(); CKKeyedAnimation *allowed = Animation(), *blocked = Animation();
    ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, allowed); ASSERT_NE(nullptr, blocked);
    RCKObjectAnimation *channel = Curve(body); ASSERT_NE(nullptr, channel);
    body->SetExclusiveAnimation(allowed); EXPECT_EQ(allowed, body->GetExclusiveAnimation());
    ASSERT_EQ(CK_OK, channel->SetFrame(6, allowed)); EXPECT_FLOAT_EQ(6, body->GetLocalMatrix()[3][0]);
    ASSERT_EQ(CK_OK, channel->SetFrame(3, blocked)); EXPECT_FLOAT_EQ(6, body->GetLocalMatrix()[3][0]);
    body->SetExclusiveAnimation(nullptr); ASSERT_EQ(CK_OK, channel->SetFrame(3, blocked));
    EXPECT_FLOAT_EQ(3, body->GetLocalMatrix()[3][0]); const CK_ID id = allowed->GetID();
    DestroyOnly(*Context, allowed); EXPECT_EQ(nullptr, Context->GetObject(id));
    ASSERT_EQ(CK_OK, channel->SetFrame(7, blocked)); EXPECT_FLOAT_EQ(7, body->GetLocalMatrix()[3][0]);
}

TEST_F(BodyPartRegistrationTest, ContextCopyRemapsOwnerAndExclusiveWithinTheRegisteredCharacterGraph) {
    CKCharacter *source = Character(); CKBodyPart *body = Body(); CKKeyedAnimation *exclusive = Animation();
    ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, exclusive);
    ASSERT_EQ(CK_OK, source->AddBodyPart(body)); ASSERT_EQ(CK_OK, source->AddAnimation(exclusive));
    body->SetExclusiveAnimation(exclusive); CKIkJoint joint = Joint(); body->SetRotationJoint(&joint);
    CKCharacter *copy = CKCharacter::Cast(Context->CopyObject(source)); ASSERT_NE(nullptr, copy);
    ASSERT_EQ(1, copy->GetBodyPartCount()); ASSERT_EQ(1, copy->GetAnimationCount());
    CKBodyPart *copiedBody = copy->GetBodyPart(0); ASSERT_NE(nullptr, copiedBody);
    EXPECT_NE(body, copiedBody); EXPECT_NE(body->GetID(), copiedBody->GetID());
    EXPECT_EQ(copy, copiedBody->GetCharacter()); EXPECT_EQ(copy->GetAnimation(0), copiedBody->GetExclusiveAnimation());
    EXPECT_NE(exclusive, copiedBody->GetExclusiveAnimation());
    CKIkJoint actual; copiedBody->GetRotationJoint(&actual); ExpectJoint(actual, joint);
    joint.m_Max.z = .75f; body->SetRotationJoint(&joint); copiedBody->GetRotationJoint(&actual); EXPECT_FLOAT_EQ(.25f, actual.m_Max.z);
    const CK_ID sourceBodyID = body->GetID(), sourceAnimationID = exclusive->GetID();
    const CK_ID copiedBodyID = copiedBody->GetID(), copiedAnimationID = copy->GetAnimation(0)->GetID(), copyID = copy->GetID();
    DestroyOnly(*Context, copy); EXPECT_EQ(nullptr, Context->GetObject(copyID));
    EXPECT_EQ(nullptr, Context->GetObject(copiedBodyID)); EXPECT_EQ(nullptr, Context->GetObject(copiedAnimationID));
    EXPECT_EQ(source, body->GetCharacter()); EXPECT_EQ(body, reinterpret_cast<CKBodyPart *>(Context->GetObject(sourceBodyID)));
    EXPECT_EQ(exclusive, Context->GetObject(sourceAnimationID));
}

TEST_F(BodyPartRegistrationTest, ContextCopiedInlineJointHasIndependentStorageAndFeedsActualJointFit) {
    CKBodyPart *source = Body(); ASSERT_NE(nullptr, source);
    CKIkJoint joint = Joint(); source->SetRotationJoint(&joint);
    VxMatrix pose; Vx3DMatrixFromEulerAngles(pose, 0, 0, .75f); source->SetLocalMatrix(pose);
    CKBodyPart *copy = CKBodyPart::Cast(Context->CopyObject(source)); ASSERT_NE(nullptr, copy);
    EXPECT_NE(source->GetID(), copy->GetID()); EXPECT_EQ(copy, reinterpret_cast<CKBodyPart *>(Context->GetObject(copy->GetID())));
    joint.m_Max.z = .5f; source->SetRotationJoint(&joint);
    CKIkJoint copiedJoint; copy->GetRotationJoint(&copiedJoint); EXPECT_FLOAT_EQ(.25f, copiedJoint.m_Max.z);
    ASSERT_EQ(CK_OK, copy->FitToJoint()); float x, y, z; Vx3DMatrixToEulerAngles(copy->GetLocalMatrix(), &x, &y, &z);
    EXPECT_NEAR(.25f, z, 1e-5f); Vx3DMatrixToEulerAngles(source->GetLocalMatrix(), &x, &y, &z); EXPECT_NEAR(.75f, z, 1e-5f);
}

TEST_F(BodyPartRegistrationTest, DestroyingRegisteredBodyDoesNotDestroyItsBorrowedRelations) {
    CKCharacter *character = Character(); CKBodyPart *body = Body(); CKKeyedAnimation *animation = Animation();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, animation);
    ASSERT_EQ(CK_OK, character->AddBodyPart(body)); body->SetExclusiveAnimation(animation);
    const CK_ID bodyID = body->GetID(), characterID = character->GetID(), animationID = animation->GetID();
    DestroyOnly(*Context, body); EXPECT_EQ(nullptr, Context->GetObject(bodyID)); EXPECT_EQ(0, character->GetBodyPartCount());
    EXPECT_EQ(character, Context->GetObject(characterID)); EXPECT_EQ(animation, Context->GetObject(animationID));
    animation->SetLength(27); EXPECT_FLOAT_EQ(27, animation->GetLength());
}

} // namespace
