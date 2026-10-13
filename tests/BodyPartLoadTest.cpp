#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class BodyPartLoadTest : public ::testing::Test {
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
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCKBodyPart *Body() { return static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART)); }
    RCKCharacter *Character() { return static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER)); }
    static void Legacy(CKStateChunk &chunk, int version, const CKDWORD (&booleans)[9], CKObject *character,
                       int size = 72) {
        chunk.StartWrite(); chunk.SetDataVersion(version);
        chunk.WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT); chunk.WriteInt(size);
        for (int i = 0; i < 9; ++i) chunk.WriteDword(booleans[i]);
        if (size == 72) {
            chunk.WriteVector(VxVector(-.2f, -.3f, -.4f));
            chunk.WriteVector(VxVector(.2f, .3f, .4f));
            chunk.WriteVector(VxVector(.1f, .2f, .3f));
        }
        chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(character);
        chunk.CloseChunk();
    }
    static void Pose(RCK3dEntity &body, float x, float y, float z) {
        VxMatrix matrix; Vx3DMatrixFromEulerAngles(matrix, x, y, z);
        body.SetMoveableFlags(body.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        body.SetLocalMatrix(matrix);
    }
    static void Angles(RCK3dEntity &body, float x, float y, float z) {
        float actualX, actualY, actualZ;
        Vx3DMatrixToEulerAngles(body.GetLocalMatrix(), &actualX, &actualY, &actualZ);
        EXPECT_NEAR(x, actualX, .00001f); EXPECT_NEAR(y, actualY, .00001f); EXPECT_NEAR(z, actualZ, .00001f);
    }
    static void SingleAxisMatrix(const VxMatrix &matrix, int axis, float angle) {
        float expected[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        const float cosine = std::cos(angle), sine = std::sin(angle);
        if (axis == 0) {
            expected[1][1] = cosine; expected[1][2] = sine;
            expected[2][1] = -sine; expected[2][2] = cosine;
        } else if (axis == 1) {
            expected[0][0] = cosine; expected[0][2] = -sine;
            expected[2][0] = sine; expected[2][2] = cosine;
        } else {
            expected[0][0] = cosine; expected[0][1] = sine;
            expected[1][0] = -sine; expected[1][1] = cosine;
        }
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_NEAR(expected[row][column], matrix[row][column], .00001f);
    }
    static void Vectors(const CKIkJoint &joint) {
        EXPECT_FLOAT_EQ(-.2f, joint.m_Min.x); EXPECT_FLOAT_EQ(-.3f, joint.m_Min.y); EXPECT_FLOAT_EQ(-.4f, joint.m_Min.z);
        EXPECT_FLOAT_EQ(.2f, joint.m_Max.x); EXPECT_FLOAT_EQ(.3f, joint.m_Max.y); EXPECT_FLOAT_EQ(.4f, joint.m_Max.z);
        EXPECT_FLOAT_EQ(.1f, joint.m_Damping.x); EXPECT_FLOAT_EQ(.2f, joint.m_Damping.y); EXPECT_FLOAT_EQ(.3f, joint.m_Damping.z);
    }
};

TEST_F(BodyPartLoadTest, LegacyFirstLimitColumnRestoresNamedXAndClampsBothXBounds) {
    const int versions[] = {0, 1, 4};
    for (int version : versions) {
        SCOPED_TRACE(version);
        RCKBodyPart *body = Body(); RCKCharacter *character = Character();
        ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, character);
        const CKDWORD words[9] = {0, 0, 0, 1, 0, 0, 0, 0, 0};
        CKStateChunk chunk(CKCID_BODYPART, nullptr); Legacy(chunk, version, words, character);
        ASSERT_EQ(CK_OK, body->Load(&chunk, nullptr));
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(character), body->GetCharacter());
        CKIkJoint joint; body->GetRotationJoint(&joint);
        EXPECT_EQ(CKDWORD(CK_IKJOINT_LIMIT_X), joint.m_Flags); Vectors(joint);
        Pose(*body, .8f, 0, 0); ASSERT_EQ(CK_OK, body->FitToJoint());
        SingleAxisMatrix(body->GetLocalMatrix(), 0, .2f);
        Pose(*body, -.8f, 0, 0); ASSERT_EQ(CK_OK, body->FitToJoint());
        SingleAxisMatrix(body->GetLocalMatrix(), 0, -.2f);
        Pose(*body, .8f, .6f, 0); ASSERT_EQ(CK_OK, body->FitToJoint()); Angles(*body, .2f, .6f, 0);
    }
}

TEST_F(BodyPartLoadTest, LegacySecondLimitColumnRestoresYForMemoryAndFileLoad) {
    for (int fileLoad = 0; fileLoad < 2; ++fileLoad) {
        SCOPED_TRACE(fileLoad);
        RCKBodyPart *body = Body(); RCKCharacter *character = Character();
        ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, character);
        const CKDWORD words[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
        CKStateChunk chunk(CKCID_BODYPART, nullptr); Legacy(chunk, 4, words, character);
        CKFile file(Context);
        ASSERT_EQ(CK_OK, body->Load(&chunk, fileLoad ? &file : nullptr));
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(character), body->GetCharacter());
        CKIkJoint joint; body->GetRotationJoint(&joint); EXPECT_EQ(CKDWORD(CK_IKJOINT_LIMIT_Y), joint.m_Flags);
        Pose(*body, 0, .8f, 0); ASSERT_EQ(CK_OK, body->FitToJoint()); SingleAxisMatrix(body->GetLocalMatrix(), 1, .3f);
        Pose(*body, 0, -.8f, 0); ASSERT_EQ(CK_OK, body->FitToJoint()); SingleAxisMatrix(body->GetLocalMatrix(), 1, -.3f);
        Pose(*body, .7f, .8f, .6f); ASSERT_EQ(CK_OK, body->FitToJoint()); Angles(*body, .7f, .3f, .6f);
    }
}

TEST_F(BodyPartLoadTest, LegacyThirdLimitColumnRestoresZAndNotifiesActualDescendantPose) {
    RCKBodyPart *body = Body(); ASSERT_NE(nullptr, body);
    RCK3dEntity *child = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(reinterpret_cast<CK3dEntity *>(body)));
    VxMatrix childLocal; childLocal.SetIdentity(); childLocal[3] = VxVector(2, 0, 0); child->SetLocalMatrix(childLocal);
    const CKDWORD words[9] = {0, 0, 0, 0, 0, 1, 0, 0, 0};
    CKStateChunk chunk(CKCID_BODYPART, nullptr); Legacy(chunk, 4, words, nullptr);
    ASSERT_EQ(CK_OK, body->Load(&chunk, nullptr));
    CKIkJoint joint; body->GetRotationJoint(&joint); EXPECT_EQ(CKDWORD(CK_IKJOINT_LIMIT_Z), joint.m_Flags);
    Pose(*body, 0, 0, .8f); ASSERT_EQ(CK_OK, body->FitToJoint()); SingleAxisMatrix(body->GetLocalMatrix(), 2, .4f);
    EXPECT_NEAR(2 * std::cos(.4f), child->GetWorldMatrix()[3][0], .00001f);
    EXPECT_NEAR(2 * std::sin(.4f), child->GetWorldMatrix()[3][1], .00001f);
    EXPECT_NEAR(0, child->GetWorldMatrix()[3][2], .00001f);
    Pose(*body, 0, 0, -.8f); ASSERT_EQ(CK_OK, body->FitToJoint()); SingleAxisMatrix(body->GetLocalMatrix(), 2, -.4f);
    EXPECT_NEAR(-2 * std::sin(.4f), child->GetWorldMatrix()[3][1], .00001f);
    Pose(*body, .7f, .6f, .8f); ASSERT_EQ(CK_OK, body->FitToJoint()); Angles(*body, .7f, .6f, .4f);
}

TEST_F(BodyPartLoadTest, LegacyActiveAndEaseColumnsUseCorrespondingAxesWithoutEnablingLimits) {
    const CKDWORD expected[] = {CK_IKJOINT_ACTIVE_X | CK_IKJOINT_EASE_X,
                               CK_IKJOINT_ACTIVE_Y | CK_IKJOINT_EASE_Y,
                               CK_IKJOINT_ACTIVE_Z | CK_IKJOINT_EASE_Z};
    for (int axis = 0; axis < 3; ++axis) {
        SCOPED_TRACE(axis);
        RCKBodyPart *body = Body(); ASSERT_NE(nullptr, body);
        CKDWORD words[9] = {}; words[axis] = 0x80000000u; words[axis + 6] = 17;
        CKStateChunk chunk(CKCID_BODYPART, nullptr); Legacy(chunk, 4, words, nullptr);
        ASSERT_EQ(CK_OK, body->Load(&chunk, nullptr));
        CKIkJoint joint; body->GetRotationJoint(&joint); EXPECT_EQ(expected[axis], joint.m_Flags); Vectors(joint);
        Pose(*body, .7f, -.6f, .8f); ASSERT_EQ(CK_OK, body->FitToJoint()); Angles(*body, .7f, -.6f, .8f);
    }
}

TEST_F(BodyPartLoadTest, AllLegacyAxesExportNamedModernJointFlagsAndIndependentWireValues) {
    RCKBodyPart *body = Body(); RCKCharacter *character = Character(); ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, character);
    const CKDWORD words[9] = {1, 2, 0xffffffffu, 17, 1, 1, 0x80000000u, 3, 1};
    CKStateChunk chunk(CKCID_BODYPART, nullptr); Legacy(chunk, 4, words, character);
    ASSERT_EQ(CK_OK, body->Load(&chunk, nullptr));
    CKIkJoint joint; body->GetRotationJoint(&joint); EXPECT_EQ(0x777u, joint.m_Flags); Vectors(joint);
    Pose(*body, .8f, -.7f, .9f); ASSERT_EQ(CK_OK, body->FitToJoint()); Angles(*body, .2f, -.3f, .4f);
    body->SetFlags(body->GetFlags() | CK_3DENTITY_IKJOINTVALID);
    CKStateChunk *saved = body->Save(nullptr, CK_STATESAVE_BODYPARTCHARACTER); ASSERT_NE(nullptr, saved);
    saved->StartRead(); EXPECT_GE(saved->GetDataVersion(), 5);
    EXPECT_EQ(44, saved->SeekIdentifierAndReturnSize(CK_STATESAVE_BODYPARTCHARACTER));
    EXPECT_EQ(character->GetID(), saved->ReadObjectID()); EXPECT_EQ(0x777u, saved->ReadDword());
    const float expected[] = {-.2f, -.3f, -.4f, .2f, .3f, .4f, .1f, .2f, .3f};
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(expected[i], saved->ReadFloat());
    EXPECT_FALSE(saved->SeekIdentifier(CK_STATESAVE_BODYPARTROTJOINT)); DeleteCKStateChunk(saved);
}

TEST_F(BodyPartLoadTest, BooleanOnlyLegacyPrefixRetainsDeterministicZeroLimitsForAllNamedAxes) {
    RCKBodyPart *body = Body(); ASSERT_NE(nullptr, body);
    const CKDWORD words[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    CKStateChunk chunk(CKCID_BODYPART, nullptr); Legacy(chunk, 0, words, nullptr, 36);
    ASSERT_EQ(CK_OK, body->Load(&chunk, nullptr));
    CKIkJoint joint; body->GetRotationJoint(&joint); EXPECT_EQ(0x777u, joint.m_Flags);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(0, (&joint.m_Min.x)[i]); EXPECT_FLOAT_EQ(0, (&joint.m_Max.x)[i]); EXPECT_FLOAT_EQ(0, (&joint.m_Damping.x)[i]);
    }
    Pose(*body, .7f, -.6f, .8f); ASSERT_EQ(CK_OK, body->FitToJoint()); SingleAxisMatrix(body->GetLocalMatrix(), 0, 0);
}

TEST_F(BodyPartLoadTest, CurrentDataVersionsFiveAndSixKeepNamedJointPayloadAndFitContract) {
    for (int version = 5; version <= 6; ++version) {
        SCOPED_TRACE(version);
        RCKBodyPart *body = Body(); RCKCharacter *character = Character(); ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, character);
        CKStateChunk chunk(CKCID_BODYPART, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(CK_3DENTITY_IKJOINTVALID); chunk.WriteDword(0);
        chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0));
        chunk.WriteVector(VxVector(0, 0, 1)); chunk.WriteVector(VxVector(0, 0, 0));
        chunk.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); chunk.WriteObject(character);
        chunk.WriteDword(CK_IKJOINT_LIMIT_Z | CK_IKJOINT_ACTIVE_Z | CK_IKJOINT_EASE_Z);
        chunk.WriteVector(VxVector(-.2f, -.3f, -.4f)); chunk.WriteVector(VxVector(.2f, .3f, .4f));
        chunk.WriteVector(VxVector(.1f, .2f, .3f)); chunk.CloseChunk();
        ASSERT_EQ(CK_OK, body->Load(&chunk, nullptr));
        CKIkJoint joint; body->GetRotationJoint(&joint); EXPECT_EQ(0x444u, joint.m_Flags); Vectors(joint);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(character), body->GetCharacter());
        Pose(*body, 0, 0, .8f); ASSERT_EQ(CK_OK, body->FitToJoint()); SingleAxisMatrix(body->GetLocalMatrix(), 2, .4f);
    }
}

TEST_F(BodyPartLoadTest, PartialCurrentCharacterReferenceRetainsPreviouslyDecodedLegacyJoint) {
    RCKBodyPart *body = Body(); RCKCharacter *first = Character(), *next = Character();
    ASSERT_NE(nullptr, body); ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, next);
    const CKDWORD words[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    CKStateChunk legacy(CKCID_BODYPART, nullptr); Legacy(legacy, 4, words, first);
    ASSERT_EQ(CK_OK, body->Load(&legacy, nullptr));
    CKStateChunk partial(CKCID_BODYPART, nullptr); partial.StartWrite(); partial.SetDataVersion(5);
    partial.WriteIdentifier(CK_STATESAVE_BODYPARTCHARACTER); partial.WriteObject(next); partial.CloseChunk();
    ASSERT_EQ(CK_OK, body->Load(&partial, nullptr));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(next), body->GetCharacter());
    CKIkJoint joint; body->GetRotationJoint(&joint); EXPECT_EQ(CKDWORD(CK_IKJOINT_LIMIT_Y), joint.m_Flags); Vectors(joint);
    Pose(*body, .7f, .8f, 0); ASSERT_EQ(CK_OK, body->FitToJoint()); Angles(*body, .7f, .3f, 0);
}
} // namespace
