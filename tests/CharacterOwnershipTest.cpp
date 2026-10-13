#include <gtest/gtest.h>
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
CK_CLASSID DerivedKeyedID = 0;

class OwnershipCharacter : public RCKCharacter {
public:
    explicit OwnershipCharacter(CKContext *context) : RCKCharacter(context) {}
    XSObjectPointerArray &Parts() { return m_BodyParts; }
    XSObjectPointerArray &Animations() { return m_Animations; }
    void Destination(RCKAnimation *animation) { m_AnimDest = animation; }
};

class OwnershipKeyed : public RCKKeyedAnimation {
public:
    explicit OwnershipKeyed(CKContext *context) : RCKKeyedAnimation(context) {}
    void Root(RCK3dEntity *root) { m_RootEntity = root; }
};

class DerivedOwnershipKeyed : public OwnershipKeyed {
public:
    explicit DerivedOwnershipKeyed(CKContext *context) : OwnershipKeyed(context) {}
    CK_CLASSID GetClassID() override { return DerivedKeyedID; }
};

class CharacterOwnershipTest : public ::testing::Test {
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
        DerivedKeyedID = CKClassGetNewIdentifier();
        CKClassRegister(DerivedKeyedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKBodyPart *PartAPI(RCKBodyPart *part) { return reinterpret_cast<CKBodyPart *>(part); }
    static CK3dEntity *EntityAPI(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    static CKKeyedAnimation *KeyedAPI(RCKKeyedAnimation *animation) { return reinterpret_cast<CKKeyedAnimation *>(animation); }
    void DestroyOnly(CKObject *object) {
        CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
        ASSERT_EQ(CK_OK, Context->DestroyObject(object, 0, &options));
    }
    static void Curve(RCKObjectAnimation &child, RCK3dEntity &entity, float first, float last) {
        child.Set3dEntity(EntityAPI(&entity));
        VxVector a(first, 0, 0), b(last, 0, 0);
        child.AddPositionKey(0, &a); child.AddPositionKey(10, &b); child.SetLength(10);
    }
    static void WorldX(RCK3dEntity &entity, float x) {
        entity.SetMoveableFlags(entity.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        VxVector position(x, 0, 0); entity.SetPosition(&position);
    }
    static float WorldX(RCK3dEntity &entity) {
        VxVector position; entity.GetPosition(&position); return position.x;
    }
    static void SnapshotParts(RCKCharacter &character, CKObject *part, int count) {
        CKStateChunk chunk(CKCID_CHARACTER, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
        chunk.WriteIdentifier(CK_STATESAVE_CHARACTERBODYPARTS); chunk.StartObjectIDSequence(count);
        for (int i = 0; i < count; ++i) chunk.WriteObjectSequence(part);
        chunk.CloseChunk(); ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr));
    }
    static void SnapshotAnimations(RCKCharacter &character, CKObject *animation, int count) {
        CKFile file(character.GetCKContext());
        CKStateChunk chunk(CKCID_CHARACTER, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
        chunk.WriteIdentifier(CK_STATESAVE_CHARACTERONLY); chunk.StartObjectIDSequence(count);
        for (int i = 0; i < count; ++i) chunk.WriteObjectSequence(animation);
        chunk.StartObjectIDSequence(4);
        for (int i = 0; i < 4; ++i) chunk.WriteObjectSequence(nullptr);
        chunk.CloseChunk(); ASSERT_EQ(CK_OK, character.Load(&chunk, &file));
    }
};

TEST_F(CharacterOwnershipTest, ConstructorCreatesPrivateWarperWithActualDynamicCreationLifetime) {
    const CK_OBJECTCREATION_OPTIONS options[] = {CK_OBJECTCREATION_NONAMECHECK, CK_OBJECTCREATION_DYNAMIC};
    for (CK_OBJECTCREATION_OPTIONS option : options) {
        RCKCharacter *character = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER, nullptr, option));
        ASSERT_NE(nullptr, character); CKAnimation *warper = character->GetWarper(); ASSERT_NE(nullptr, warper);
        EXPECT_EQ(CKCID_KEYEDANIMATION, warper->GetClassID());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(character), warper->GetCharacter());
        EXPECT_EQ(character->IsDynamic(), warper->IsDynamic());
        EXPECT_EQ(0, character->GetBodyPartCount()); EXPECT_EQ(0, character->GetAnimationCount());
        EXPECT_EQ(nullptr, character->GetRootBodyPart()); EXPECT_EQ(nullptr, character->GetActiveAnimation());
        EXPECT_EQ(nullptr, character->GetNextActiveAnimation()); EXPECT_EQ(nullptr, character->GetFloorReferenceObject());
        EXPECT_EQ(CK_OBJECT_NOTTOBELISTEDANDSAVED,
                  warper->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED);
        EXPECT_NE(0u, warper->GetFlags() & CKANIMATION_ALLOWTURN);
        EXPECT_EQ(nullptr, warper->GetName());
    }
}

TEST_F(CharacterOwnershipTest, BodyListPublicOperationsKeepOrderIdentityAndDuplicateContract) {
    OwnershipCharacter character(Context); RCKBodyPart a(Context), b(Context);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.AddBodyPart(nullptr));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.RemoveBodyPart(nullptr));
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&a))); ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&b)));
    EXPECT_EQ(CKERR_ALREADYPRESENT, character.AddBodyPart(PartAPI(&a)));
    ASSERT_EQ(2, character.GetBodyPartCount()); EXPECT_EQ(PartAPI(&a), character.GetBodyPart(0));
    EXPECT_EQ(PartAPI(&b), character.GetBodyPart(1)); EXPECT_EQ(nullptr, character.GetBodyPart(-1));
    EXPECT_EQ(nullptr, character.GetBodyPart(2)); EXPECT_EQ(PartAPI(&b), character.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), a.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), b.GetCharacter());
    a.SetParent(nullptr); b.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, TransferringDirectRootMovesHierarchyWithPreservedWorldPose) {
    OwnershipCharacter oldOwner(Context), newOwner(Context); RCKBodyPart root(Context);
    WorldX(oldOwner, 10); WorldX(newOwner, 100); WorldX(root, 15);
    ASSERT_EQ(CK_OK, oldOwner.AddBodyPart(PartAPI(&root)));
    ASSERT_EQ(CK_OK, newOwner.AddBodyPart(PartAPI(&root)));
    EXPECT_EQ(0, oldOwner.GetBodyPartCount()); EXPECT_EQ(nullptr, oldOwner.GetRootBodyPart());
    EXPECT_EQ(1, newOwner.GetBodyPartCount()); EXPECT_EQ(PartAPI(&root), newOwner.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&newOwner), root.GetCharacter());
    EXPECT_EQ(EntityAPI(&newOwner), root.GetParent()); EXPECT_FLOAT_EQ(15, WorldX(root));
    WorldX(oldOwner, 50); EXPECT_FLOAT_EQ(15, WorldX(root));
    WorldX(newOwner, 110); EXPECT_FLOAT_EQ(25, WorldX(root));
    root.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, TransferringJointPreservesItsExistingExternalParent) {
    OwnershipCharacter oldOwner(Context), newOwner(Context); RCK3dEntity parent(Context); RCKBodyPart part(Context);
    WorldX(parent, 10); WorldX(part, 17); part.SetParent(EntityAPI(&parent));
    ASSERT_EQ(CK_OK, oldOwner.AddBodyPart(PartAPI(&part)));
    ASSERT_EQ(CK_OK, newOwner.AddBodyPart(PartAPI(&part)));
    EXPECT_EQ(EntityAPI(&parent), part.GetParent()); EXPECT_FLOAT_EQ(17, WorldX(part));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&newOwner), part.GetCharacter());
    EXPECT_EQ(0, oldOwner.GetBodyPartCount()); EXPECT_EQ(nullptr, newOwner.GetRootBodyPart());
    part.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, AddBodyPartReportsHierarchyCycleWithoutClaimingMembership) {
    RCKBodyPart ancestor(Context); OwnershipCharacter character(Context);
    ASSERT_TRUE(character.SetParent(EntityAPI(&ancestor)));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.AddBodyPart(PartAPI(&ancestor)));
    EXPECT_EQ(0, character.GetBodyPartCount()); EXPECT_EQ(nullptr, character.GetRootBodyPart());
    EXPECT_EQ(nullptr, ancestor.GetCharacter()); EXPECT_EQ(nullptr, ancestor.GetParent());
    character.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, RemovingFinalSnapshotAliasClearsRootOwnerAndFloorWithoutDestroyingPart) {
    OwnershipCharacter character(Context); RCKBodyPart part(Context);
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&part)));
    SnapshotParts(character, &part, 2); character.SetFloorReferenceObject(EntityAPI(&part));
    ASSERT_EQ(CK_OK, character.RemoveBodyPart(PartAPI(&part)));
    EXPECT_EQ(1, character.GetBodyPartCount()); EXPECT_EQ(PartAPI(&part), character.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), part.GetCharacter());
    ASSERT_EQ(CK_OK, character.RemoveBodyPart(PartAPI(&part)));
    EXPECT_EQ(0, character.GetBodyPartCount()); EXPECT_EQ(nullptr, character.GetRootBodyPart());
    EXPECT_EQ(nullptr, part.GetCharacter()); EXPECT_EQ(nullptr, character.GetFloorReferenceObject());
    EXPECT_EQ(&part, Context->GetObject(part.GetID()));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.RemoveBodyPart(PartAPI(&part)));
    part.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, RemovingBorrowedBodySlotPreservesActualForeignOwner) {
    OwnershipCharacter owner(Context), borrower(Context); RCKBodyPart part(Context);
    ASSERT_EQ(CK_OK, owner.AddBodyPart(PartAPI(&part))); SnapshotParts(borrower, &part, 1);
    ASSERT_EQ(CK_OK, borrower.RemoveBodyPart(PartAPI(&part)));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&owner), part.GetCharacter());
    EXPECT_EQ(1, owner.GetBodyPartCount()); EXPECT_EQ(PartAPI(&part), owner.GetRootBodyPart());
    part.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, SelectingAnotherRootDoesNotReleaseStillRegisteredBodyMembers) {
    OwnershipCharacter character(Context); RCKBodyPart a(Context), b(Context);
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&a))); ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&b)));
    ASSERT_EQ(CK_OK, character.SetRootBodyPart(PartAPI(&a)));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), b.GetCharacter());
    EXPECT_EQ(PartAPI(&a), character.GetRootBodyPart()); EXPECT_EQ(2, character.GetBodyPartCount());
    ASSERT_EQ(CK_OK, character.SetRootBodyPart(nullptr));
    EXPECT_EQ(nullptr, character.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), a.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), b.GetCharacter());
    a.SetParent(nullptr); b.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, ForcedRootTransfersPreviousStandaloneSelectionWithoutAddingMembership) {
    OwnershipCharacter oldOwner(Context), newOwner(Context); RCKBodyPart part(Context);
    ASSERT_EQ(CK_OK, oldOwner.SetRootBodyPart(PartAPI(&part)));
    ASSERT_EQ(CK_OK, newOwner.SetRootBodyPart(PartAPI(&part)));
    EXPECT_EQ(nullptr, oldOwner.GetRootBodyPart()); EXPECT_EQ(PartAPI(&part), newOwner.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&newOwner), part.GetCharacter());
    EXPECT_EQ(0, oldOwner.GetBodyPartCount()); EXPECT_EQ(0, newOwner.GetBodyPartCount());
    ASSERT_EQ(CK_OK, newOwner.SetRootBodyPart(nullptr)); EXPECT_EQ(nullptr, part.GetCharacter());
}

TEST_F(CharacterOwnershipTest, AnimationListPublicOperationsKeepBoundsOrderAndBorrowedPlaybackReference) {
    OwnershipCharacter character(Context); RCKAnimation a(Context), b(Context);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.AddAnimation(nullptr));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.RemoveAnimation(nullptr));
    ASSERT_EQ(CK_OK, character.AddAnimation(&a)); ASSERT_EQ(CK_OK, character.AddAnimation(&b));
    ASSERT_EQ(CK_OK, character.AddAnimation(&a)); EXPECT_EQ(2, character.GetAnimationCount());
    EXPECT_EQ(&a, character.GetAnimation(0)); EXPECT_EQ(&b, character.GetAnimation(1));
    EXPECT_EQ(nullptr, character.GetAnimation(-1)); EXPECT_EQ(nullptr, character.GetAnimation(2));
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(&a)); ASSERT_EQ(CK_OK, character.RemoveAnimation(&a));
    EXPECT_EQ(nullptr, a.GetCharacter()); EXPECT_EQ(&a, character.GetActiveAnimation());
    EXPECT_EQ(1, character.GetAnimationCount()); EXPECT_EQ(&b, character.GetAnimation(0));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.RemoveAnimation(&a));
}

TEST_F(CharacterOwnershipTest, AnimationTransferRemovesAllOldSnapshotAliases) {
    OwnershipCharacter oldOwner(Context), newOwner(Context); RCKAnimation animation(Context);
    ASSERT_EQ(CK_OK, oldOwner.AddAnimation(&animation)); SnapshotAnimations(oldOwner, &animation, 2);
    ASSERT_EQ(CK_OK, newOwner.AddAnimation(&animation));
    EXPECT_EQ(0, oldOwner.GetAnimationCount()); EXPECT_EQ(1, newOwner.GetAnimationCount());
    EXPECT_EQ(&animation, newOwner.GetAnimation(0));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&newOwner), animation.GetCharacter());
}

TEST_F(CharacterOwnershipTest, RemovingAnimationAliasOrBorrowedSlotDoesNotReleaseAnotherAssociation) {
    OwnershipCharacter owner(Context), borrower(Context); RCKAnimation animation(Context);
    ASSERT_EQ(CK_OK, owner.AddAnimation(&animation)); SnapshotAnimations(owner, &animation, 2);
    ASSERT_EQ(CK_OK, owner.RemoveAnimation(&animation));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&owner), animation.GetCharacter());
    SnapshotAnimations(borrower, &animation, 1); ASSERT_EQ(CK_OK, borrower.RemoveAnimation(&animation));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&owner), animation.GetCharacter());
    ASSERT_EQ(CK_OK, owner.RemoveAnimation(&animation)); EXPECT_EQ(nullptr, animation.GetCharacter());
}

TEST_F(CharacterOwnershipTest, RepeatedAddKeyedAnimationRecentersActualCurveAtCurrentFrame) {
    OwnershipCharacter character(Context); RCKBodyPart root(Context); RCKObjectAnimation child(Context); OwnershipKeyed group(Context);
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&root))); Curve(child, root, 0, 10);
    ASSERT_EQ(CK_OK, group.AddAnimation(&child)); group.SetLength(10); group.SetCurrentStep(.5f);
    ASSERT_EQ(CK_OK, character.AddAnimation(&group)); group.SetStep(.5f); EXPECT_FLOAT_EQ(0, root.GetLocalMatrix()[3][0]);
    group.SetCurrentStep(.8f); ASSERT_EQ(CK_OK, character.AddAnimation(&group)); group.SetStep(.8f);
    EXPECT_EQ(1, character.GetAnimationCount()); EXPECT_FLOAT_EQ(0, root.GetLocalMatrix()[3][0]);
    VxVector raw; ASSERT_TRUE(child.EvaluatePosition(8, raw)); EXPECT_FLOAT_EQ(8, raw.x);
    root.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, ExactKeyedAddRefreshesStaleHierarchyRootBeforeCentering) {
    OwnershipCharacter character(Context); RCKBodyPart actual(Context), stale(Context);
    RCKObjectAnimation child(Context); OwnershipKeyed group(Context);
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&actual))); Curve(child, actual, 0, 10);
    ASSERT_EQ(CK_OK, group.AddAnimation(&child)); group.SetLength(10); group.SetCurrentStep(.4f); group.Root(&stale);
    ASSERT_EQ(CK_OK, character.AddAnimation(&group)); group.SetStep(.4f);
    EXPECT_EQ(EntityAPI(&actual), group.GetRootEntity()); EXPECT_FLOAT_EQ(0, actual.GetLocalMatrix()[3][0]);
    actual.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, DerivedKeyedAddRetainsItsExplicitRootPolicyWhileCentering) {
    OwnershipCharacter character(Context); RCKBodyPart a(Context), b(Context);
    RCKObjectAnimation x(Context), y(Context); DerivedOwnershipKeyed group(Context);
    character.SetMoveableFlags(character.GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
    character.SetAnimationLevelOfDetail(1.0f);
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&a))); ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&b)));
    Curve(x, a, 0, 10); Curve(y, b, 0, 40);
    ASSERT_EQ(CK_OK, group.AddAnimation(&x)); ASSERT_EQ(CK_OK, group.AddAnimation(&y));
    group.SetLength(10); group.SetCurrentStep(.4f); group.Root(&b);
    ASSERT_EQ(CK_OK, character.AddAnimation(&group)); group.SetStep(.4f);
    EXPECT_EQ(EntityAPI(&b), group.GetRootEntity()); EXPECT_FLOAT_EQ(4, a.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(0, b.GetLocalMatrix()[3][0]);
    a.SetParent(nullptr); b.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, CharacterDestructionDetachesOwnLiveReferencesAndPreservesForeignOwners) {
    RCKCharacter *dead = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER));
    ASSERT_NE(nullptr, dead); CKAnimation *warper = dead->GetWarper(); ASSERT_NE(nullptr, warper);
    OwnershipCharacter foreign(Context); RCKBodyPart ownPart(Context), borrowedPart(Context);
    RCKAnimation ownAnimation(Context), borrowedAnimation(Context);
    ASSERT_EQ(CK_OK, dead->AddBodyPart(PartAPI(&ownPart))); ASSERT_EQ(CK_OK, foreign.AddBodyPart(PartAPI(&borrowedPart)));
    ASSERT_EQ(CK_OK, dead->AddAnimation(&ownAnimation)); ASSERT_EQ(CK_OK, foreign.AddAnimation(&borrowedAnimation));
    // Shared snapshots are a real producer of borrowed list references.
    SnapshotParts(*dead, &borrowedPart, 1); SnapshotAnimations(*dead, &borrowedAnimation, 1);
    const CK_ID id = dead->GetID(), warperID = warper->GetID();
    // Exercise the destructor independently of the character dependency policy.
    ownPart.SetParent(nullptr);
    delete dead; EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(nullptr, ownPart.GetCharacter()); EXPECT_EQ(nullptr, ownAnimation.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&foreign), borrowedPart.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&foreign), borrowedAnimation.GetCharacter());
    ASSERT_EQ(warper, Context->GetObject(warperID)); EXPECT_EQ(nullptr, warper->GetCharacter());
    borrowedPart.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, PreDeletionClearsDeletedWarperAndLeavesOtherAnimationPlayable) {
    RCKCharacter *character = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER));
    RCKAnimation *live = static_cast<RCKAnimation *>(Context->CreateObject(CKCID_ANIMATION));
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, live); ASSERT_EQ(CK_OK, character->AddAnimation(live));
    CKAnimation *warper = character->GetWarper(); ASSERT_NE(nullptr, warper); DestroyOnly(warper);
    EXPECT_EQ(nullptr, character->GetWarper()); EXPECT_EQ(1, character->GetAnimationCount());
    EXPECT_EQ(live, character->GetAnimation(0)); live->SetLength(10); live->SetFrame(3);
    EXPECT_FLOAT_EQ(3, live->GetFrame());
}

TEST_F(CharacterOwnershipTest, PreDeletionRemovesMarkedMembersAndClearsPrimaryRootAndFloorReferences) {
    OwnershipCharacter character(Context); RCKBodyPart deadPart(Context), livePart(Context);
    RCKAnimation deadAnimation(Context), liveAnimation(Context);
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&livePart))); ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&deadPart)));
    ASSERT_EQ(CK_OK, character.AddAnimation(&deadAnimation)); ASSERT_EQ(CK_OK, character.AddAnimation(&liveAnimation));
    ASSERT_EQ(CK_OK, character.SetActiveAnimation(&deadAnimation)); character.SetFloorReferenceObject(EntityAPI(&deadPart));
    character.Destination(&deadAnimation);
    deadPart.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); deadAnimation.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    character.CheckPreDeletion(); EXPECT_EQ(1, character.GetBodyPartCount()); EXPECT_EQ(PartAPI(&livePart), character.GetBodyPart(0));
    EXPECT_EQ(1, character.GetAnimationCount()); EXPECT_EQ(&liveAnimation, character.GetAnimation(0));
    EXPECT_EQ(nullptr, character.GetActiveAnimation()); EXPECT_EQ(nullptr, character.GetRootBodyPart());
    EXPECT_EQ(nullptr, character.GetNextActiveAnimation());
    EXPECT_EQ(nullptr, character.GetFloorReferenceObject());
    deadPart.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); deadAnimation.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    livePart.SetParent(nullptr); deadPart.SetParent(nullptr);
}

TEST_F(CharacterOwnershipTest, GenericKeyedRootCanRemainLiveAfterCharacterDestruction) {
    RCKCharacter *character = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER)); ASSERT_NE(nullptr, character);
    RCK3dEntity root(Context); RCKObjectAnimation child(Context); OwnershipKeyed group(Context);
    Curve(child, root, 7, 17); ASSERT_EQ(CK_OK, group.AddAnimation(&child)); group.SetLength(10);
    ASSERT_EQ(CK_OK, character->AddAnimation(&group));
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&root), character->GetRootBodyPart());
    delete character; ASSERT_EQ(nullptr, group.GetCharacter());
    EXPECT_EQ(&root, Context->GetObject(root.GetID()));
    group.SetStep(.5f); EXPECT_FLOAT_EQ(5, root.GetLocalMatrix()[3][0]);
}

TEST_F(CharacterOwnershipTest, GenericKeyedRootSwitchesToBodyPartWithoutTouchingEntityStorage) {
    OwnershipCharacter character(Context); RCK3dEntity generic(Context); RCKBodyPart realPart(Context);
    RCKObjectAnimation child(Context); OwnershipKeyed group(Context);
    Curve(child, generic, 7, 17); ASSERT_EQ(CK_OK, group.AddAnimation(&child)); group.SetLength(10);
    ASSERT_EQ(CK_OK, character.AddAnimation(&group));
    const CK_ID id = generic.GetID(); generic.SetAppData(&child);
    VxMatrix before = generic.GetLocalMatrix();
    ASSERT_EQ(CK_OK, character.SetRootBodyPart(PartAPI(&realPart)));
    EXPECT_EQ(PartAPI(&realPart), character.GetRootBodyPart());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), realPart.GetCharacter());
    EXPECT_EQ(&generic, Context->GetObject(id)); EXPECT_EQ(&child, generic.GetAppData());
    EXPECT_EQ(CKCID_3DENTITY, generic.GetClassID());
    EXPECT_FLOAT_EQ(before[3][0], generic.GetLocalMatrix()[3][0]);
}

TEST_F(CharacterOwnershipTest, ContextClearAllSafelyDeletesWarperBeforeItsCharacter) {
    RCKCharacter *character = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER));
    RCKBodyPart *part = static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
    RCKAnimation *animation = static_cast<RCKAnimation *>(Context->CreateObject(CKCID_ANIMATION));
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, part); ASSERT_NE(nullptr, animation);
    ASSERT_EQ(CK_OK, character->AddBodyPart(PartAPI(part))); ASSERT_EQ(CK_OK, character->AddAnimation(animation));
    ASSERT_NE(nullptr, character->GetWarper());
    ASSERT_EQ(CK_OK, Context->ClearAll());
    RCKCharacter *replacement = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER));
    ASSERT_NE(nullptr, replacement); ASSERT_NE(nullptr, replacement->GetWarper());
    EXPECT_EQ(0, replacement->GetBodyPartCount());
}

TEST_F(CharacterOwnershipTest, ContextCloseSafelyDeletesEarlierMembersBeforeTheirCharacter) {
    RCKBodyPart *part = static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART));
    RCKAnimation *animation = static_cast<RCKAnimation *>(Context->CreateObject(CKCID_ANIMATION));
    RCKCharacter *character = static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER));
    ASSERT_NE(nullptr, part); ASSERT_NE(nullptr, animation); ASSERT_NE(nullptr, character);
    ASSERT_EQ(CK_OK, character->AddBodyPart(PartAPI(part))); ASSERT_EQ(CK_OK, character->AddAnimation(animation));
    ASSERT_EQ(CK_OK, CKCloseContext(Context)); Context = nullptr;
    ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    EXPECT_NE(nullptr, Context->CreateObject(CKCID_CHARACTER));
}

} // namespace
