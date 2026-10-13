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
class BodyPartProbe : public RCKBodyPart {
public:
    explicit BodyPartProbe(CKContext *context) : RCKBodyPart(context) {}
    void Character(RCKCharacter *character) { m_Character = character; }
    CKIkJoint &Joint() { return m_RotationJoint; }
};

class DerivedBodyPartProbe : public BodyPartProbe {
public:
    explicit DerivedBodyPartProbe(CKContext *context) : BodyPartProbe(context) {}
    CK_CLASSID GetClassID() override { return CKCID_CHARACTER; }
};

class BodyPartSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Seed(BodyPartProbe &part, RCKCharacter &character) {
        part.Character(&character); part.Joint().m_Flags = 0x111;
        part.Joint().m_Min.Set(-1, -2, -3); part.Joint().m_Max.Set(1, 2, 3);
        part.Joint().m_Damping.Set(.1f, .2f, .3f);
    }
    static void ExpectSeed(BodyPartProbe &part, RCKCharacter &character) {
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), part.GetCharacter());
        EXPECT_EQ(0x111u, part.Joint().m_Flags); EXPECT_FLOAT_EQ(-2, part.Joint().m_Min.y);
        EXPECT_FLOAT_EQ(3, part.Joint().m_Max.z); EXPECT_FLOAT_EQ(.1f, part.Joint().m_Damping.x);
    }
    static void EntityPose(CKStateChunk &chunk, CKDWORD flags, const VxVector &position) {
        chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(flags); chunk.WriteDword(0);
        chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0));
        chunk.WriteVector(VxVector(0, 0, 1)); chunk.WriteVector(position);
    }
    static void CurrentJoint(CKStateChunk &chunk) {
        chunk.WriteDword(0x321); chunk.WriteVector(VxVector(-4, -5, -6));
        chunk.WriteVector(VxVector(4, 5, 6)); chunk.WriteVector(VxVector(.4f, .5f, .6f));
    }
    static void LegacyJoint(CKStateChunk &chunk, int size = 72) {
        chunk.WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT); chunk.WriteInt(size);
        for (int i = 0; i < 9; ++i) chunk.WriteDword(1);
        if (size > 36) {
            chunk.WriteVector(VxVector(-7, -8, -9)); chunk.WriteVector(VxVector(7, 8, 9));
            chunk.WriteVector(VxVector(.7f, .8f, .9f));
        }
    }
    static CKERROR Load(BodyPartProbe &part, CKStateChunk &chunk) {
        chunk.CloseChunk(); return part.Load(&chunk, nullptr);
    }
};

TEST_F(BodyPartSerializationTest, MemorySaveWithoutBodyBitsReturns3dEntityBaseChunk) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    part.m_LocalMatrix[3][0] = 12; part.LocalMatrixChanged(FALSE, TRUE);
    CKStateChunk *saved = part.Save(nullptr, CK_STATESAVE_3DENTITYMATRIX); ASSERT_NE(nullptr, saved);
    EXPECT_EQ(CKCID_3DENTITY, saved->GetChunkClassID()); saved->StartRead();
    EXPECT_TRUE(saved->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA));
    EXPECT_FALSE(saved->SeekIdentifier(CK_STATESAVE_BODYPARTCHARACTER)); DeleteCKStateChunk(saved);
}

TEST_F(BodyPartSerializationTest, EveryPartialBodySaveFlagSelectsCharacterBlockWithoutJointWhenInvalid) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    part.SetFlags(part.GetFlags() & ~CK_3DENTITY_IKJOINTVALID);
    const CKDWORD bits[] = {0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000, 0x20000000, 0x40000000};
    for (int i = 0; i < 7; ++i) {
        CKStateChunk *saved = part.Save(nullptr, bits[i]); ASSERT_NE(nullptr, saved);
        EXPECT_EQ(CKCID_BODYPART, saved->GetChunkClassID()); EXPECT_EQ(-1, saved->GetCurrentPos());
        saved->StartRead(); EXPECT_EQ(4, saved->SeekIdentifierAndReturnSize(CK_STATESAVE_BODYPARTCHARACTER));
        EXPECT_EQ(character.GetID(), saved->ReadObjectID());
        EXPECT_FALSE(saved->SeekIdentifier(CK_STATESAVE_BODYPARTROTJOINT)); DeleteCKStateChunk(saved);
    }
}

TEST_F(BodyPartSerializationTest, CurrentSaveWritesExactlyFortyJointBytesWithoutLengthPrefix) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    part.SetFlags(part.GetFlags() | CK_3DENTITY_IKJOINTVALID);
    ASSERT_EQ(40u, sizeof(CKIkJoint)); CKStateChunk *saved = part.Save(nullptr, CK_STATESAVE_BODYPARTROTJOINT);
    ASSERT_NE(nullptr, saved); saved->StartRead();
    EXPECT_EQ(44, saved->SeekIdentifierAndReturnSize(CK_STATESAVE_BODYPARTCHARACTER));
    EXPECT_EQ(character.GetID(), saved->ReadObjectID()); EXPECT_EQ(0x111u, saved->ReadDword());
    const float expected[] = {-1, -2, -3, 1, 2, 3, .1f, .2f, .3f};
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(expected[i], saved->ReadFloat());
    DeleteCKStateChunk(saved);
}

TEST_F(BodyPartSerializationTest, FileSaveIgnoresZeroSaveFlagsAndIncludesBaseEntityAndBodyData) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character); CKFile file(Context);
    CKStateChunk *saved = part.Save(&file, 0); ASSERT_NE(nullptr, saved);
    saved->StartRead(); EXPECT_TRUE(saved->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA));
    EXPECT_EQ(4, saved->SeekIdentifierAndReturnSize(CK_STATESAVE_BODYPARTCHARACTER));
    // CKFile writes its object index rather than a context ID; length/order remain independent of that mapping.
    saved->ReadObjectID(); DeleteCKStateChunk(saved);
}

TEST_F(BodyPartSerializationTest, DerivedBodySaveKeepsWriterOpenForSubclassPayload) {
    DerivedBodyPartProbe part(Context); CKStateChunk *saved = part.Save(nullptr, CK_STATESAVE_BODYPARTCHARACTER);
    ASSERT_NE(nullptr, saved); EXPECT_EQ(CKCID_BODYPART, saved->GetChunkClassID());
    EXPECT_GE(saved->GetCurrentPos(), 0); saved->WriteIdentifier(0x12345678); saved->WriteDword(99); saved->CloseChunk();
    saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(0x12345678)); EXPECT_EQ(99u, saved->ReadDword());
    DeleteCKStateChunk(saved);
}

TEST_F(BodyPartSerializationTest, NullLoadAndOmittedIdentifiersPreserveBodyState) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, part.Load(nullptr, nullptr)); ExpectSeed(part, character);
    CKStateChunk omitted(CKCID_BODYPART, nullptr); omitted.StartWrite(); omitted.SetDataVersion(5);
    ASSERT_EQ(CK_OK, Load(part, omitted)); ExpectSeed(part, character);
}

TEST_F(BodyPartSerializationTest, IndependentCurrentWireRestoresJointCharacterAndActualEntityPose) {
    RCKCharacter oldCharacter(Context), character(Context); BodyPartProbe part(Context); Seed(part, oldCharacter);
    // Memory snapshots apply their matrix only when the pre-load preserved
    // WORLDALIGNED gate is clear. Default entities preserve their current pose.
    part.m_MoveableFlags &= ~VX_MOVEABLE_WORLDALIGNED;
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    EntityPose(chunk, CK_3DENTITY_IKJOINTVALID, VxVector(3, 4, 5));
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(&character); CurrentJoint(chunk);
    ASSERT_EQ(CK_OK, Load(part, chunk)); EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), part.GetCharacter());
    CKIkJoint joint; part.GetRotationJoint(&joint); EXPECT_EQ(0x321u, joint.m_Flags);
    EXPECT_FLOAT_EQ(-4, joint.m_Min.x); EXPECT_FLOAT_EQ(-5, joint.m_Min.y); EXPECT_FLOAT_EQ(-6, joint.m_Min.z);
    EXPECT_FLOAT_EQ(4, joint.m_Max.x); EXPECT_FLOAT_EQ(5, joint.m_Max.y); EXPECT_FLOAT_EQ(6, joint.m_Max.z);
    EXPECT_FLOAT_EQ(.4f, joint.m_Damping.x); EXPECT_FLOAT_EQ(.5f, joint.m_Damping.y); EXPECT_FLOAT_EQ(.6f, joint.m_Damping.z);
    EXPECT_FLOAT_EQ(3, part.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(4, part.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(5, part.GetWorldMatrix()[3][2]);
}

TEST_F(BodyPartSerializationTest, CharacterOnlyCurrentLoadNeedsNoJointAndPreservesExistingJoint) {
    RCKCharacter oldCharacter(Context), character(Context); BodyPartProbe part(Context); Seed(part, oldCharacter);
    part.SetFlags(part.GetFlags() & ~CK_3DENTITY_IKJOINTVALID);
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(&character);
    ASSERT_EQ(CK_OK, Load(part, chunk)); EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), part.GetCharacter());
    EXPECT_EQ(0x111u, part.Joint().m_Flags); EXPECT_FLOAT_EQ(-2, part.Joint().m_Min.y);
}

TEST_F(BodyPartSerializationTest, LegacyVersionFourDecodesNineBooleanWordsIntoNamedSdkAxes) {
    RCKCharacter character(Context); BodyPartProbe part(Context);
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4); LegacyJoint(chunk);
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(&character);
    ASSERT_EQ(CK_OK, Load(part, chunk)); EXPECT_EQ(0x777u, part.Joint().m_Flags);
    EXPECT_FLOAT_EQ(-7, part.Joint().m_Min.x); EXPECT_FLOAT_EQ(-8, part.Joint().m_Min.y); EXPECT_FLOAT_EQ(-9, part.Joint().m_Min.z);
    EXPECT_FLOAT_EQ(7, part.Joint().m_Max.x); EXPECT_FLOAT_EQ(8, part.Joint().m_Max.y); EXPECT_FLOAT_EQ(9, part.Joint().m_Max.z);
    EXPECT_FLOAT_EQ(.7f, part.Joint().m_Damping.x); EXPECT_FLOAT_EQ(.8f, part.Joint().m_Damping.y); EXPECT_FLOAT_EQ(.9f, part.Joint().m_Damping.z);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), part.GetCharacter());
}

TEST_F(BodyPartSerializationTest, LegacyBooleanOnlyPayloadUsesZeroInitializedVectors) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(0); LegacyJoint(chunk, 36);
    ASSERT_EQ(CK_OK, Load(part, chunk)); EXPECT_EQ(0x777u, part.Joint().m_Flags);
    EXPECT_FLOAT_EQ(0, part.Joint().m_Min.x); EXPECT_FLOAT_EQ(0, part.Joint().m_Max.y); EXPECT_FLOAT_EQ(0, part.Joint().m_Damping.z);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), part.GetCharacter());
}

TEST_F(BodyPartSerializationTest, CurrentVersionIgnoresLegacyJointAndLegacyVersionIgnoresCurrentJointTail) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    CKStateChunk current(CKCID_BODYPART, nullptr); current.StartWrite(); current.SetDataVersion(5); LegacyJoint(current);
    ASSERT_EQ(CK_OK, Load(part, current)); ExpectSeed(part, character);
    CKStateChunk legacy(CKCID_BODYPART, nullptr); legacy.StartWrite(); legacy.SetDataVersion(4);
    legacy.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); legacy.WriteObject(nullptr); CurrentJoint(legacy);
    ASSERT_EQ(CK_OK, Load(part, legacy)); EXPECT_EQ(nullptr, part.GetCharacter());
    EXPECT_EQ(0x111u, part.Joint().m_Flags); EXPECT_FLOAT_EQ(-2, part.Joint().m_Min.y);
}

TEST_F(BodyPartSerializationTest, TruncatedCurrentJointCannotConsumeFollowingIdentifierAndPreservesBodyFields) {
    RCKCharacter oldCharacter(Context), character(Context); BodyPartProbe part(Context); Seed(part, oldCharacter);
    part.m_MoveableFlags &= ~VX_MOVEABLE_WORLDALIGNED;
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    EntityPose(chunk, CK_3DENTITY_IKJOINTVALID, VxVector(9, 8, 7));
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(&character); chunk.WriteDword(0x777);
    chunk.WriteIdentifier(0x12345678); for (int i = 0; i < 12; ++i) chunk.WriteFloat(100 + i);
    EXPECT_EQ(CKERR_INVALIDFILE, Load(part, chunk)); ExpectSeed(part, oldCharacter);
    // Base entity load already succeeded; body failure does not promise whole-object rollback.
    EXPECT_FLOAT_EQ(9, part.GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(8, part.GetWorldMatrix()[3][1]);
    ASSERT_TRUE(chunk.SeekIdentifier(0x12345678)); EXPECT_FLOAT_EQ(100, chunk.ReadFloat());
}

TEST_F(BodyPartSerializationTest, EmptyCurrentCharacterBlockIsAnErrorRatherThanImplicitNullReference) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteIdentifier(0x12345678); chunk.WriteDword(99);
    EXPECT_EQ(CKERR_INVALIDFILE, Load(part, chunk)); ExpectSeed(part, character);
}

TEST_F(BodyPartSerializationTest, TruncatedLegacySizeCannotReadFollowingCharacterBlock) {
    RCKCharacter oldCharacter(Context), character(Context); BodyPartProbe part(Context); Seed(part, oldCharacter);
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT); chunk.WriteInt(72); chunk.WriteDword(1);
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(&character);
    EXPECT_EQ(CKERR_INVALIDFILE, Load(part, chunk)); ExpectSeed(part, oldCharacter);
}

TEST_F(BodyPartSerializationTest, NegativeAndMissingLegacySizesFailWithoutPublishingJoint) {
    RCKCharacter character(Context); BodyPartProbe part(Context); Seed(part, character);
    CKStateChunk negative(CKCID_BODYPART, nullptr); negative.StartWrite(); negative.SetDataVersion(4);
    negative.WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT); negative.WriteInt(-1);
    for (int j = 0; j < 19; ++j) negative.WriteDword(1);
    EXPECT_EQ(CKERR_INVALIDFILE, Load(part, negative)); ExpectSeed(part, character);
    CKStateChunk missing(CKCID_BODYPART, nullptr); missing.StartWrite(); missing.SetDataVersion(4);
    missing.WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT);
    EXPECT_EQ(CKERR_INVALIDFILE, Load(part, missing)); ExpectSeed(part, character);
}

TEST_F(BodyPartSerializationTest, CompleteUnknownLegacyExtensionSkipsJointAndRestoresFollowingCharacter) {
    RCKCharacter oldCharacter(Context), character(Context); BodyPartProbe part(Context); Seed(part, oldCharacter);
    CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT); chunk.WriteInt(80);
    for (int i = 0; i < 20; ++i) chunk.WriteDword(0x12345678 + i);
    chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(&character);
    ASSERT_EQ(CK_OK, Load(part, chunk)); EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), part.GetCharacter());
    EXPECT_EQ(0x111u, part.Joint().m_Flags); EXPECT_FLOAT_EQ(-2, part.Joint().m_Min.y);
    EXPECT_FLOAT_EQ(3, part.Joint().m_Max.z); EXPECT_FLOAT_EQ(.1f, part.Joint().m_Damping.x);
}
} // namespace
