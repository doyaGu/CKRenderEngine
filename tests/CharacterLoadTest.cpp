#include <gtest/gtest.h>
#include <cstring>
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

// Independently authored DWORD payloads. No Character/BodyPart Save or chunk
// writer participates in the oracle. Identifier links are payload offsets.
struct CharacterFrame {
    CKDWORD Data[256] = {};
    int Count = 0;
    int Previous = -1;
    void Word(CKDWORD value) { Data[Count++] = value; }
    void Float(float value) {
        CKDWORD word;
        std::memcpy(&word, &value, sizeof(word));
        Word(word);
    }
    void Identifier(CKDWORD identifier) {
        if (Previous >= 0)
            Data[Previous + 1] = Count;
        Previous = Count;
        Word(identifier); Word(0);
    }
    void Object(CKObject *object) { Word(object ? object->GetID() : 0); }
    void SubChunk(const CharacterFrame &child) {
        Word(7 + child.Count);
        Word(CKCID_BODYPART); Word(0x00070005u); Word(child.Count);
        Word(0); Word(0); Word(0); Word(0);
        for (int i = 0; i < child.Count; ++i)
            Word(child.Data[i]);
    }
    CKBOOL Read(CKStateChunk &chunk, int version = 5, CKBOOL file = FALSE) const {
        CKDWORD wire[258] = {};
        wire[0] = (file ? 0x08000000u : 0u) | 0x00070000u |
                  (CKDWORD(CKCID_CHARACTER) << 8) | CKDWORD(version);
        wire[1] = Count;
        std::memcpy(wire + 2, Data, Count * sizeof(CKDWORD));
        return chunk.ConvertFromBuffer(wire);
    }
};

class CharacterLoadProbe : public RCKCharacter {
public:
    explicit CharacterLoadProbe(CKContext *context) : RCKCharacter(context) {}
    XSObjectPointerArray &Parts() { return m_BodyParts; }
    XSObjectPointerArray &Animations() { return m_Animations; }
    void References(RCKKeyedAnimation *active, RCKAnimation *destination,
                    RCKBodyPart *root, RCK3dEntity *floor) {
        m_ActiveAnimation = active; m_AnimDest = destination;
        m_RootBodyPart = root; m_FloorRef = floor;
    }
};

class FailingCharacterPart : public RCKBodyPart {
public:
    explicit FailingCharacterPart(CKContext *context) : RCKBodyPart(context) {}
    int Calls = 0;
    CKFile *LastFile = reinterpret_cast<CKFile *>(1);
    CK_CLASSID LastClass = 0;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override {
        ++Calls; LastFile = file;
        LastClass = chunk ? chunk->GetChunkClassID() : 0;
        return CKERR_INVALIDFILE;
    }
};

class CharacterLoadTest : public ::testing::Test {
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
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CharacterFrame Pose(float x, float y, float z, RCKCharacter *character = nullptr) {
        CharacterFrame frame;
        frame.Identifier(0x00100000u);
        frame.Word(character ? CK_3DENTITY_PARENTVALID : 0); frame.Word(VX_MOVEABLE_VISIBLE);
        const float values[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, x, y, z};
        for (float value : values)
            frame.Float(value);
        if (character)
            frame.Object(character);
        frame.Identifier(0x04000000u); frame.Object(character);
        return frame;
    }
    static void ExpectPose(RCKBodyPart &part, float x, float y, float z) {
        const VxMatrix &matrix = part.GetLocalMatrix();
        EXPECT_FLOAT_EQ(x, matrix[3][0]); EXPECT_FLOAT_EQ(y, matrix[3][1]); EXPECT_FLOAT_EQ(z, matrix[3][2]);
    }
    static void References(CharacterFrame &frame, CKObject *active, CKObject *destination,
                           CKObject *root, CKObject *floor) {
        frame.Identifier(0xFFC00000u); frame.Word(4);
        frame.Object(active); frame.Object(destination); frame.Object(root); frame.Object(floor);
    }
    static void ExpectReferences(CharacterLoadProbe &character, RCKKeyedAnimation *active,
                                 RCKAnimation *destination, RCKBodyPart *root, RCK3dEntity *floor) {
        EXPECT_EQ(static_cast<CKAnimation *>(active), character.GetActiveAnimation());
        EXPECT_EQ(static_cast<CKAnimation *>(destination), character.GetNextActiveAnimation());
        EXPECT_EQ(reinterpret_cast<CKBodyPart *>(root), character.GetRootBodyPart());
        EXPECT_EQ(reinterpret_cast<CK3dEntity *>(floor), character.GetFloorReferenceObject());
    }
};

TEST_F(CharacterLoadTest, NullChunkPreservesListsReferencesAndPose) {
    CharacterLoadProbe character(Context); RCKBodyPart part(Context); RCK3dEntity floor(Context);
    RCKKeyedAnimation active(Context); RCKAnimation destination(Context);
    character.Parts().PushBack(&part); character.Animations().PushBack(&active);
    character.References(&active, &destination, &part, &floor);
    VxVector position(4, 5, 6); character.SetPosition(&position);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, character.Load(nullptr, nullptr));
    EXPECT_EQ(1, character.GetBodyPartCount()); EXPECT_EQ(1, character.GetAnimationCount());
    ExpectReferences(character, &active, &destination, &part, &floor);
    EXPECT_FLOAT_EQ(4, character.GetLocalMatrix()[3][0]);
}

TEST_F(CharacterLoadTest, ModernMemoryReplacesBodyListAndFourReferencesButKeepsAnimationList) {
    CharacterLoadProbe character(Context); RCKBodyPart oldPart(Context), a(Context), b(Context);
    RCK3dEntity floor(Context); RCKKeyedAnimation oldAnimation(Context), active(Context);
    RCKAnimation destination(Context);
    character.Parts().PushBack(&oldPart); character.Animations().PushBack(&oldAnimation);
    CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(3);
    frame.Object(&b); frame.Object(&a); frame.Object(&b);
    References(frame, &active, &destination, &a, &floor);
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk));
    ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr));
    ASSERT_EQ(3, character.GetBodyPartCount());
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&b), character.GetBodyPart(0));
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&a), character.GetBodyPart(1));
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&b), character.GetBodyPart(2));
    ASSERT_EQ(1, character.GetAnimationCount()); EXPECT_EQ(&oldAnimation, character.GetAnimation(0));
    ExpectReferences(character, &active, &destination, &a, &floor);
    EXPECT_EQ(&oldPart, Context->GetObject(oldPart.GetID()));
}

TEST_F(CharacterLoadTest, ModernFileResolvesIndicesAndReplacesBothOrderedLists) {
    CharacterLoadProbe character(Context); RCKBodyPart oldPart(Context), a(Context), b(Context);
    RCK3dEntity floor(Context); RCKKeyedAnimation oldAnimation(Context), active(Context);
    RCKAnimation destination(Context);
    character.Parts().PushBack(&oldPart); character.Animations().PushBack(&oldAnimation);
    CKFile file(Context); file.m_FileObjects.Resize(5);
    CKObject *objects[] = {&b, &active, &a, &destination, &floor};
    for (int i = 0; i < 5; ++i) file.m_FileObjects[i].CreatedObject = objects[i]->GetID();
    CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(2); frame.Word(0); frame.Word(2);
    frame.Identifier(0xFFC00000u); frame.Word(3); frame.Word(3); frame.Word(1); frame.Word(1);
    frame.Word(4); frame.Word(1); frame.Word(3); frame.Word(2); frame.Word(4);
    CKStateChunk chunk(CKCID_CHARACTER, &file); ASSERT_TRUE(frame.Read(chunk, 5, TRUE));
    ASSERT_EQ(CK_OK, character.Load(&chunk, &file));
    ASSERT_EQ(2, character.GetBodyPartCount()); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&b), character.GetBodyPart(0));
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&a), character.GetBodyPart(1));
    ASSERT_EQ(3, character.GetAnimationCount()); EXPECT_EQ(&destination, character.GetAnimation(0));
    EXPECT_EQ(&active, character.GetAnimation(1)); EXPECT_EQ(&active, character.GetAnimation(2));
    ExpectReferences(character, &active, &destination, &a, &floor);
    EXPECT_EQ(&oldAnimation, Context->GetObject(oldAnimation.GetID()));
}

TEST_F(CharacterLoadTest, LegacyFileReadsAnimationTailAndSeparateRootFloorIdentifiers) {
    CharacterLoadProbe character(Context); RCKBodyPart part(Context); RCK3dEntity floor(Context);
    RCKKeyedAnimation active(Context); RCKAnimation destination(Context);
    CKFile file(Context); file.m_FileObjects.Resize(4);
    CKObject *objects[] = {&part, &active, &destination, &floor};
    for (int i = 0; i < 4; ++i) file.m_FileObjects[i].CreatedObject = objects[i]->GetID();
    CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(1); frame.Word(0);
    frame.Identifier(0x01000000u); frame.Word(2); frame.Word(2); frame.Word(1); frame.Word(1); frame.Word(2);
    frame.Identifier(0x02000000u); frame.Word(0); frame.Identifier(0x20000000u); frame.Word(3);
    CKStateChunk chunk(CKCID_CHARACTER, &file); ASSERT_TRUE(frame.Read(chunk, 4, TRUE));
    ASSERT_EQ(CK_OK, character.Load(&chunk, &file));
    ASSERT_EQ(1, character.GetBodyPartCount()); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&part), character.GetBodyPart(0));
    ASSERT_EQ(2, character.GetAnimationCount()); EXPECT_EQ(&destination, character.GetAnimation(0));
    EXPECT_EQ(&active, character.GetAnimation(1));
    ExpectReferences(character, &active, &destination, &part, &floor);
}

TEST_F(CharacterLoadTest, LegacyMemoryConsumesMissingObjectSnapshotAndLoadsFollowingBodyPose) {
    CharacterLoadProbe character(Context); RCKBodyPart oldPart(Context), loaded(Context);
    loaded.SetMoveableFlags(loaded.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    RCK3dEntity floor(Context); RCKKeyedAnimation active(Context); RCKAnimation destination(Context);
    character.Parts().PushBack(&oldPart); character.Animations().PushBack(&active);
    CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(1); frame.Object(&loaded);
    frame.Identifier(0x04000000u); frame.Word(0x12345678u); frame.Object(&active); frame.Object(&destination);
    frame.Identifier(0x10000000u); frame.Word(2); frame.Word(0); frame.SubChunk(Pose(1, 2, 3));
    frame.Object(&loaded); frame.SubChunk(Pose(7, 8, 9, &character));
    frame.Identifier(0x02000000u); frame.Object(&loaded); frame.Identifier(0x20000000u); frame.Object(&floor);
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk, 4));
    ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr));
    ASSERT_EQ(1, character.GetBodyPartCount()); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&oldPart), character.GetBodyPart(0));
    ASSERT_EQ(1, character.GetAnimationCount()); EXPECT_EQ(&active, character.GetAnimation(0));
    ExpectReferences(character, &active, &destination, &loaded, &floor);
    ExpectPose(loaded, 7, 8, 9);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), loaded.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&character), loaded.GetParent());
    loaded.SetParent(nullptr);
}

TEST_F(CharacterLoadTest, ModernMatchingSnapshotsRestoreBodyPosesAndChildCharacterHierarchy) {
    CharacterLoadProbe character(Context); RCKBodyPart a(Context), b(Context);
    a.SetMoveableFlags(a.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    b.SetMoveableFlags(b.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(2); frame.Object(&b); frame.Object(&a);
    frame.Identifier(0x10000000u); frame.Word(2);
    frame.SubChunk(Pose(11, 12, 13, &character)); frame.SubChunk(Pose(21, 22, 23, &character));
    References(frame, nullptr, nullptr, &a, &b);
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk));
    ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr));
    ExpectPose(b, 11, 12, 13); ExpectPose(a, 21, 22, 23);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), a.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), b.GetCharacter());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&character), a.GetParent());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&character), b.GetParent());
    ExpectReferences(character, nullptr, nullptr, &a, &b);
    a.SetParent(nullptr); b.SetParent(nullptr);
}

TEST_F(CharacterLoadTest, ModernMismatchedCountSkipsEntireSequenceAndStillReadsReferences) {
    CharacterLoadProbe character(Context); FailingCharacterPart part(Context);
    RCK3dEntity floor(Context); RCKKeyedAnimation active(Context); RCKAnimation destination(Context);
    CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(1); frame.Object(&part);
    frame.Identifier(0x10000000u); frame.Word(2); frame.SubChunk(Pose(1, 2, 3)); frame.SubChunk(Pose(4, 5, 6));
    References(frame, &active, &destination, &part, &floor);
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk));
    ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr)); EXPECT_EQ(0, part.Calls);
    ExpectReferences(character, &active, &destination, &part, &floor);
}

TEST_F(CharacterLoadTest, ModernSnapshotsUseRetainedListAndConsumeNullTargetAndNullSnapshot) {
    CharacterLoadProbe character(Context); RCKBodyPart unchanged(Context), loaded(Context);
    loaded.SetMoveableFlags(loaded.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    character.Parts().PushBack(nullptr); character.Parts().PushBack(&unchanged); character.Parts().PushBack(&loaded);
    VxVector position(31, 32, 33); unchanged.SetPosition(&position);
    CharacterFrame frame; frame.Identifier(0x10000000u); frame.Word(3);
    frame.SubChunk(Pose(1, 2, 3)); frame.Word(0); frame.SubChunk(Pose(41, 42, 43));
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk));
    ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr)); EXPECT_EQ(3, character.GetBodyPartCount());
    ExpectPose(unchanged, 31, 32, 33); ExpectPose(loaded, 41, 42, 43);
}

TEST_F(CharacterLoadTest, ModernChildLoadFailureReturnsRealErrorAndStopsLaterSnapshots) {
    CharacterLoadProbe character(Context); FailingCharacterPart bad(Context); RCKBodyPart later(Context), oldRoot(Context);
    later.SetMoveableFlags(later.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    character.References(nullptr, nullptr, &oldRoot, nullptr);
    VxVector position(51, 52, 53); later.SetPosition(&position);
    CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(2); frame.Object(&bad); frame.Object(&later);
    frame.Identifier(0x10000000u); frame.Word(2); frame.SubChunk(Pose(1, 2, 3)); frame.SubChunk(Pose(61, 62, 63));
    References(frame, nullptr, nullptr, &later, nullptr);
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk));
    EXPECT_EQ(CKERR_INVALIDFILE, character.Load(&chunk, nullptr));
    EXPECT_EQ(1, bad.Calls); EXPECT_EQ(nullptr, bad.LastFile); EXPECT_EQ(CKCID_BODYPART, bad.LastClass);
    ExpectPose(later, 51, 52, 53);
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&oldRoot), character.GetRootBodyPart());
    EXPECT_EQ(2, character.GetBodyPartCount()); // Incremental load, no rollback contract.
}

TEST_F(CharacterLoadTest, LegacyChildLoadFailureReturnsRealErrorAndStopsLaterPairs) {
    CharacterLoadProbe character(Context); FailingCharacterPart bad(Context); RCKBodyPart later(Context);
    later.SetMoveableFlags(later.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    VxVector position(71, 72, 73); later.SetPosition(&position);
    CharacterFrame frame; frame.Identifier(0x10000000u); frame.Word(2);
    frame.Object(&bad); frame.SubChunk(Pose(1, 2, 3)); frame.Object(&later); frame.SubChunk(Pose(81, 82, 83));
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk, 4));
    EXPECT_EQ(CKERR_INVALIDFILE, character.Load(&chunk, nullptr));
    EXPECT_EQ(1, bad.Calls); EXPECT_EQ(nullptr, bad.LastFile); EXPECT_EQ(CKCID_BODYPART, bad.LastClass);
    ExpectPose(later, 71, 72, 73);
}

TEST_F(CharacterLoadTest, RootFallbackRequiresModernReferenceSectionAndChoosesFirstHierarchyChild) {
    CharacterLoadProbe character(Context); RCKBodyPart first(Context), second(Context);
    first.SetParent(reinterpret_cast<CK3dEntity *>(&character)); second.SetParent(reinterpret_cast<CK3dEntity *>(&character));
    CharacterFrame absent; absent.Identifier(0x00400000u); absent.Word(0);
    CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(absent.Read(chunk));
    ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr)); EXPECT_EQ(nullptr, character.GetRootBodyPart());
    CharacterFrame references; References(references, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(references.Read(chunk, 4)); ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, character.GetRootBodyPart());
    ASSERT_TRUE(references.Read(chunk, 5)); ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr));
    EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&first), character.GetRootBodyPart());
    first.SetParent(nullptr); second.SetParent(nullptr);
}

TEST_F(CharacterLoadTest, ModernAbsentReferenceFieldsPreservePointersAcrossVersionFiveAndLater) {
    CharacterLoadProbe character(Context); RCKBodyPart root(Context), listed(Context); RCK3dEntity floor(Context);
    RCKKeyedAnimation active(Context); RCKAnimation destination(Context);
    character.References(&active, &destination, &root, &floor);
    const int versions[] = {5, 6, 10};
    for (int version : versions) {
        SCOPED_TRACE(version);
        CharacterFrame frame; frame.Identifier(0x00400000u); frame.Word(1); frame.Object(&listed);
        CKStateChunk chunk(CKCID_CHARACTER, nullptr); ASSERT_TRUE(frame.Read(chunk, version));
        ASSERT_EQ(CK_OK, character.Load(&chunk, nullptr));
        ExpectReferences(character, &active, &destination, &root, &floor);
        ASSERT_EQ(1, character.GetBodyPartCount()); EXPECT_EQ(reinterpret_cast<CKBodyPart *>(&listed), character.GetBodyPart(0));
    }
}

} // namespace
