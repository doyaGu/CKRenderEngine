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

class CharacterSaveProbe : public RCKCharacter {
public:
    explicit CharacterSaveProbe(CKContext *context) : RCKCharacter(context) {}
    void Body(CKObject *body) { m_BodyParts.PushBack(body); }
    void Animation(CKObject *animation) { m_Animations.PushBack(animation); }
    void References(RCKBodyPart *root, RCKKeyedAnimation *active, RCKAnimation *next, RCK3dEntity *floor) {
        m_RootBodyPart = root; m_ActiveAnimation = active; m_AnimDest = next; m_FloorRef = floor;
    }
};

class DerivedCharacterSave : public CharacterSaveProbe {
public:
    explicit DerivedCharacterSave(CKContext *context) : CharacterSaveProbe(context) {}
    CK_CLASSID GetClassID() override { return CKCID_3DENTITY; }
};

class CharacterSnapshotBody : public RCKBodyPart {
public:
    CharacterSnapshotBody(CKContext *context, CKDWORD payload) : RCKBodyPart(context), Payload(payload) {}
    void PreSave(CKFile *, CKDWORD flags) override { ++PreSaveCalls; PreSaveFlags = flags; }
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override {
        ++SaveCalls; LastFile = file; SaveFlags = flags;
        if (Fail) return nullptr;
        CKStateChunk *chunk = CreateCKStateChunk(CKCID_BODYPART, file);
        chunk->StartWrite(); chunk->SetDataVersion(CHUNK_DEV_2_1);
        chunk->WriteIdentifier(0xCA47); chunk->WriteDword(Payload);
        chunk->CloseChunk(); return chunk;
    }
    CKDWORD Payload, PreSaveFlags = 0, SaveFlags = 0;
    CKFile *LastFile = nullptr;
    int PreSaveCalls = 0, SaveCalls = 0;
    bool Fail = false;
};

class CharacterSaveAnimation : public RCKKeyedAnimation {
public:
    explicit CharacterSaveAnimation(CKContext *context) : RCKKeyedAnimation(context) {}
    void PreSave(CKFile *, CKDWORD flags) override { ++Calls; Flags = flags; }
    int Calls = 0; CKDWORD Flags = 0;
};

class CharacterBaseDependency : public RCKObjectAnimation {
public:
    explicit CharacterBaseDependency(CKContext *context) : RCKObjectAnimation(context) {}
    void PreSave(CKFile *, CKDWORD flags) override { ++Calls; Flags = flags; }
    int Calls = 0; CKDWORD Flags = 0;
};

class CharacterSaveFile : public CKFile {
public:
    explicit CharacterSaveFile(CKContext *context) : CKFile(context) {}
    using CKFile::SaveFindObjectIndex;
};

class CharacterSaveTest : public ::testing::Test {
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
    static void Prepare(CKFile &file) { file.m_IndexByClassId.Resize(80); }
    static void Snapshot(CKStateChunk &parent, CKDWORD expected) {
        CKStateChunk *sub = parent.ReadSubChunk(); ASSERT_NE(nullptr, sub);
        EXPECT_EQ(CKCID_BODYPART, sub->GetChunkClassID()); EXPECT_EQ(CHUNK_DEV_2_1, sub->GetDataVersion());
        sub->StartRead();
        // Independent child wire: identifier, terminal next identifier, payload.
        EXPECT_EQ(0xCA47u, sub->ReadDword()); EXPECT_EQ(0u, sub->ReadDword()); EXPECT_EQ(expected, sub->ReadDword());
        DeleteCKStateChunk(sub);
    }
    static void FourMemoryReferences(CKStateChunk &chunk, CK_ID active, CK_ID next, CK_ID root, CK_ID floor) {
        ASSERT_TRUE(chunk.SeekIdentifier(0xFFC00000)); EXPECT_EQ(4, chunk.StartReadSequence());
        EXPECT_EQ(active, chunk.ReadObjectID()); EXPECT_EQ(next, chunk.ReadObjectID());
        EXPECT_EQ(root, chunk.ReadObjectID()); EXPECT_EQ(floor, chunk.ReadObjectID());
    }
};

TEST_F(CharacterSaveTest, MemoryWithoutCharacterMaskDoesNotWriteCharacterStateOrInvokeChildren) {
    CharacterSnapshotBody body(Context, 7); CharacterSaveProbe character(Context); character.Body(&body);
    const CKDWORD masks[] = {0, 0x100000u};
    for (unsigned int index = 0; index < 2; ++index) {
        CKStateChunk *chunk = character.Save(nullptr, masks[index]); ASSERT_NE(nullptr, chunk);
        EXPECT_NE(CKCID_CHARACTER, chunk->GetChunkClassID()); chunk->StartRead();
        EXPECT_FALSE(chunk->SeekIdentifier(0x400000)); EXPECT_FALSE(chunk->SeekIdentifier(0x10000000));
        EXPECT_FALSE(chunk->SeekIdentifier(0xFFC00000)); DeleteCKStateChunk(chunk);
    }
    EXPECT_EQ(0, body.SaveCalls);
}

TEST_F(CharacterSaveTest, EveryCharacterFlagBitWritesListAndFourReferencesOnlySnapshotBitCallsBodySave) {
    CharacterSnapshotBody body(Context, 0x12345678u); CharacterSaveProbe character(Context); character.Body(&body);
    character.References(&body, nullptr, nullptr, nullptr);
    int expectedCalls = 0;
    for (unsigned int bit = 22; bit < 32; ++bit) {
        SCOPED_TRACE(bit); const CKDWORD flags = CKDWORD(1) << bit;
        CKStateChunk *chunk = character.Save(nullptr, flags); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_CHARACTER, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
        chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x400000)); EXPECT_EQ(1, chunk->StartReadSequence());
        EXPECT_EQ(body.GetID(), chunk->ReadObjectID());
        FourMemoryReferences(*chunk, 0, 0, body.GetID(), 0);
        EXPECT_EQ(bit == 28, chunk->SeekIdentifier(0x10000000) != FALSE);
        if (bit == 28) { ++expectedCalls; EXPECT_EQ(1, chunk->StartReadSequence()); Snapshot(*chunk, 0x12345678u); }
        EXPECT_EQ(expectedCalls, body.SaveCalls); DeleteCKStateChunk(chunk);
    }
    EXPECT_EQ(0x7FFFFFFFu, body.SaveFlags); EXPECT_EQ(nullptr, body.LastFile);
}

TEST_F(CharacterSaveTest, MemoryListPreservesNullAndDuplicatesAndFourReferenceOrder) {
    CharacterSnapshotBody first(Context, 1), second(Context, 2); CharacterSaveAnimation active(Context), next(Context);
    RCK3dEntity floor(Context); CharacterSaveProbe character(Context);
    character.Body(&second); character.Body(nullptr); character.Body(&first); character.Body(&second);
    character.Animation(&next); character.Animation(&active); character.References(&first, &active, &next, &floor);
    CKStateChunk *chunk = character.Save(nullptr, 0x4000000); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x400000)); EXPECT_EQ(4, chunk->StartReadSequence());
    EXPECT_EQ(second.GetID(), chunk->ReadObjectID()); EXPECT_EQ(0u, chunk->ReadObjectID());
    EXPECT_EQ(first.GetID(), chunk->ReadObjectID()); EXPECT_EQ(second.GetID(), chunk->ReadObjectID());
    FourMemoryReferences(*chunk, active.GetID(), next.GetID(), first.GetID(), floor.GetID());
    EXPECT_EQ(0, first.SaveCalls); EXPECT_EQ(0, second.SaveCalls); DeleteCKStateChunk(chunk);
}

TEST_F(CharacterSaveTest, EmbeddedSnapshotsUseBodyOrderAllBodyFlagsAndLegalEmptySlots) {
    CharacterSnapshotBody first(Context, 0x11223344u), second(Context, 0x55667788u); CharacterSaveProbe character(Context);
    character.Body(&second); character.Body(nullptr); character.Body(&first); character.Body(&second);
    CKStateChunk *chunk = character.Save(nullptr, 0x10000000); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x10000000)); EXPECT_EQ(4, chunk->StartReadSequence());
    Snapshot(*chunk, 0x55667788u); EXPECT_EQ(nullptr, chunk->ReadSubChunk());
    Snapshot(*chunk, 0x11223344u); Snapshot(*chunk, 0x55667788u);
    EXPECT_EQ(1, first.SaveCalls); EXPECT_EQ(2, second.SaveCalls);
    EXPECT_EQ(0x7FFFFFFFu, first.SaveFlags); EXPECT_EQ(0x7FFFFFFFu, second.SaveFlags);
    EXPECT_EQ(nullptr, first.LastFile); EXPECT_EQ(nullptr, second.LastFile); DeleteCKStateChunk(chunk);
}

TEST_F(CharacterSaveTest, EmptyBodySnapshotWritesZeroCountAndFourNullReferences) {
    CharacterSaveProbe character(Context); CKStateChunk *chunk = character.Save(nullptr, 0x10000000);
    ASSERT_NE(nullptr, chunk); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x400000));
    EXPECT_EQ(0, chunk->StartReadSequence()); ASSERT_TRUE(chunk->SeekIdentifier(0x10000000));
    EXPECT_EQ(0, chunk->StartReadSequence()); FourMemoryReferences(*chunk, 0, 0, 0, 0); DeleteCKStateChunk(chunk);
}

TEST_F(CharacterSaveTest, PresentBodySaveFailureRejectsSnapshotAndStopsBeforeLaterChildren) {
    for (int failIndex = 0; failIndex < 2; ++failIndex) {
        SCOPED_TRACE(failIndex);
        CharacterSnapshotBody first(Context, 1), second(Context, 2), later(Context, 3); CharacterSaveProbe character(Context);
        character.Body(nullptr); character.Body(&first); character.Body(&second); character.Body(&later);
        if (failIndex == 0) first.Fail = true; else second.Fail = true;
        CKStateChunk *failed = character.Save(nullptr, 0x10000000);
        EXPECT_EQ(nullptr, failed); DeleteCKStateChunk(failed);
        EXPECT_EQ(1, first.SaveCalls); EXPECT_EQ(failIndex == 0 ? 0 : 1, second.SaveCalls); EXPECT_EQ(0, later.SaveCalls);
        EXPECT_EQ(4, character.GetBodyPartCount()); EXPECT_EQ(&first, Context->GetObject(first.GetID()));
        EXPECT_EQ(&second, Context->GetObject(second.GetID()));
        first.Fail = false; second.Fail = false;
        CKStateChunk *retry = character.Save(nullptr, 0x10000000); ASSERT_NE(nullptr, retry);
        retry->StartRead(); ASSERT_TRUE(retry->SeekIdentifier(0x10000000)); EXPECT_EQ(4, retry->StartReadSequence());
        EXPECT_EQ(nullptr, retry->ReadSubChunk()); Snapshot(*retry, 1); Snapshot(*retry, 2); Snapshot(*retry, 3);
        DeleteCKStateChunk(retry);
    }
}

TEST_F(CharacterSaveTest, FilePreSaveOrdersRootListsAndBaseDependenciesWithTheirActualFlags) {
    CharacterSnapshotBody root(Context, 1), other(Context, 2); CharacterSaveAnimation first(Context), second(Context);
    CharacterBaseDependency baseDependency(Context); CharacterSaveProbe character(Context);
    baseDependency.Set3dEntity(reinterpret_cast<CK3dEntity *>(&character));
    character.Body(&other); character.Body(&root); character.Body(nullptr); character.Body(&other);
    character.Animation(&second); character.Animation(&first); character.Animation(&second);
    character.References(&root, nullptr, nullptr, nullptr);
    CharacterSaveFile file(Context); Prepare(file); const CKDWORD flags = 0x4000000;
    file.SaveObject(&character, flags); ASSERT_EQ(6, file.m_FileObjects.Size());
    CKObject *expected[] = {&baseDependency, &root, &other, &second, &first, &character};
    for (int index = 0; index < 6; ++index) {
        EXPECT_EQ(expected[index], file.m_FileObjects[index].ObjPtr);
        EXPECT_EQ(index == 0 || index == 5 ? flags : 0xFFFFFFFFu, file.m_FileObjects[index].SaveFlags);
        EXPECT_EQ(index, file.SaveFindObjectIndex(expected[index]->GetID()));
    }
    EXPECT_EQ(flags, baseDependency.Flags); EXPECT_EQ(0xFFFFFFFFu, root.PreSaveFlags);
    EXPECT_EQ(0xFFFFFFFFu, other.PreSaveFlags); EXPECT_EQ(0xFFFFFFFFu, first.Flags); EXPECT_EQ(0xFFFFFFFFu, second.Flags);
    EXPECT_EQ(1, root.PreSaveCalls); EXPECT_EQ(1, other.PreSaveCalls); EXPECT_EQ(1, first.Calls); EXPECT_EQ(1, second.Calls);
    character.PreSave(&file, 0x10000000); EXPECT_EQ(6, file.m_FileObjects.Size()); EXPECT_EQ(1, root.PreSaveCalls);
}

TEST_F(CharacterSaveTest, PreSaveUpgradesReferenceToSavedBodyWithoutDuplicatingItsFileIndex) {
    CharacterSnapshotBody root(Context, 1); CharacterSaveProbe character(Context); character.Body(&root);
    character.References(&root, nullptr, nullptr, nullptr);
    CharacterSaveFile file(Context); Prepare(file); file.SaveObjectAsReference(&root);
    ASSERT_EQ(1, file.m_FileObjects.Size()); EXPECT_EQ(0u, file.m_FileObjects[0].SaveFlags);
    file.SaveObject(&character, 0x4000000); ASSERT_EQ(2, file.m_FileObjects.Size());
    EXPECT_EQ(&root, file.m_FileObjects[0].ObjPtr); EXPECT_EQ(0xFFFFFFFFu, file.m_FileObjects[0].SaveFlags);
    EXPECT_EQ(0, file.SaveFindObjectIndex(root.GetID())); EXPECT_EQ(0, file.m_ReferencedObjects.Size());
    EXPECT_EQ(1, root.PreSaveCalls); EXPECT_EQ(0xFFFFFFFFu, root.PreSaveFlags);
}

TEST_F(CharacterSaveTest, PreSaveKeepsFirstSaveFlagsAndHonorsRealDynamicAndExcludedFilters) {
    CharacterSnapshotBody root(Context, 1), dynamic(Context, 2), excluded(Context, 3); CharacterSaveAnimation animation(Context);
    CharacterSaveProbe character(Context); character.Body(&root); character.Body(&dynamic); character.Body(&excluded);
    character.Animation(&animation); character.References(&root, nullptr, nullptr, nullptr);
    dynamic.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); excluded.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    CharacterSaveFile file(Context); Prepare(file); file.SaveObject(&root, 0x55u); file.SaveObject(&character, 0x4000000);
    ASSERT_EQ(3, file.m_FileObjects.Size()); EXPECT_EQ(&root, file.m_FileObjects[0].ObjPtr);
    EXPECT_EQ(0x55u, file.m_FileObjects[0].SaveFlags); EXPECT_EQ(&animation, file.m_FileObjects[1].ObjPtr);
    EXPECT_EQ(0xFFFFFFFFu, file.m_FileObjects[1].SaveFlags); EXPECT_EQ(&character, file.m_FileObjects[2].ObjPtr);
    EXPECT_EQ(0, dynamic.PreSaveCalls); EXPECT_EQ(0, excluded.PreSaveCalls);
    EXPECT_EQ(-1, file.SaveFindObjectIndex(dynamic.GetID())); EXPECT_EQ(-1, file.SaveFindObjectIndex(excluded.GetID()));
}

TEST_F(CharacterSaveTest, FileWriterForcesListsUsesFileIndicesAndSuppressesInlineBodySave) {
    CharacterSnapshotBody root(Context, 1), other(Context, 2); CharacterSaveAnimation active(Context), next(Context);
    RCK3dEntity floor(Context); CharacterSaveProbe character(Context);
    character.Body(&other); character.Body(nullptr); character.Body(&root); character.Body(&other);
    character.Animation(&next); character.Animation(nullptr); character.Animation(&active); character.Animation(&next);
    character.References(&root, &active, &next, &floor);
    CharacterSaveFile file(Context); Prepare(file); file.SaveObject(&character, 0); file.SaveObject(&floor, 0);
    ASSERT_EQ(6, file.m_FileObjects.Size());
    EXPECT_EQ(0, file.SaveFindObjectIndex(root.GetID())); EXPECT_EQ(1, file.SaveFindObjectIndex(other.GetID()));
    EXPECT_EQ(2, file.SaveFindObjectIndex(next.GetID())); EXPECT_EQ(3, file.SaveFindObjectIndex(active.GetID()));
    EXPECT_EQ(4, file.SaveFindObjectIndex(character.GetID())); EXPECT_EQ(5, file.SaveFindObjectIndex(floor.GetID()));
    CKStateChunk *chunk = character.Save(&file, 0x10000000); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x400000)); EXPECT_EQ(4, chunk->ReadInt());
    EXPECT_EQ(1, chunk->ReadInt()); EXPECT_EQ(-1, chunk->ReadInt()); EXPECT_EQ(0, chunk->ReadInt()); EXPECT_EQ(1, chunk->ReadInt());
    ASSERT_TRUE(chunk->SeekIdentifier(0xFFC00000)); EXPECT_EQ(4, chunk->ReadInt());
    EXPECT_EQ(2, chunk->ReadInt()); EXPECT_EQ(-1, chunk->ReadInt()); EXPECT_EQ(3, chunk->ReadInt()); EXPECT_EQ(2, chunk->ReadInt());
    EXPECT_EQ(4, chunk->ReadInt()); EXPECT_EQ(3, chunk->ReadInt()); EXPECT_EQ(2, chunk->ReadInt());
    EXPECT_EQ(0, chunk->ReadInt()); EXPECT_EQ(5, chunk->ReadInt());
    EXPECT_FALSE(chunk->SeekIdentifier(0x10000000)); EXPECT_EQ(0, root.SaveCalls); EXPECT_EQ(0, other.SaveCalls);
    DeleteCKStateChunk(chunk);
}

TEST_F(CharacterSaveTest, DerivedCharacterWriterStaysOpenForAnIndependentAdditionalSection) {
    DerivedCharacterSave character(Context); CKStateChunk *chunk = character.Save(nullptr, 0x4000000);
    ASSERT_NE(nullptr, chunk); EXPECT_GE(chunk->GetCurrentPos(), 0);
    chunk->WriteIdentifier(0xC047); chunk->WriteDword(0xCAFEBABEu); chunk->CloseChunk(); chunk->StartRead();
    FourMemoryReferences(*chunk, 0, 0, 0, 0); ASSERT_TRUE(chunk->SeekIdentifier(0xC047));
    EXPECT_EQ(0xCAFEBABEu, chunk->ReadDword()); DeleteCKStateChunk(chunk);
}

} // namespace
