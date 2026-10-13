#include <gtest/gtest.h>

#include "CKAll.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

// The child seam executes the same virtual Save call as a real controller
// animation. Its independently specified payload makes the parent writer
// observable without calling KeyedAnimation::Load.
class SnapshotAnimation : public RCKObjectAnimation {
public:
    SnapshotAnimation(CKContext *context, CKDWORD payload)
        : RCKObjectAnimation(context, nullptr), Payload(payload) {}

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override {
        ++Calls;
        LastFile = file;
        LastFlags = flags;
        if (Fail)
            return nullptr;
        CKStateChunk *chunk = CreateCKStateChunk(CKCID_OBJECTANIMATION, file);
        chunk->StartWrite();
        chunk->SetDataVersion(CHUNK_DEV_2_1);
        chunk->WriteIdentifier(0x6B51);
        chunk->WriteDword(Payload);
        chunk->CloseChunk();
        return chunk;
    }

    CKDWORD Payload;
    int Calls = 0;
    CKFile *LastFile = nullptr;
    CKDWORD LastFlags = 0;
    bool Fail = false;
};

class KeyedSaveProbe : public RCKKeyedAnimation {
public:
    explicit KeyedSaveProbe(CKContext *context) : RCKKeyedAnimation(context, nullptr) {}
    void SetMergeFields(CKBOOL merged, float factor) {
        m_Merged = merged;
        m_MergeFactor = factor;
    }
    void AppendEmptySlot() { m_Animations.PushBack(nullptr); }
};

class DerivedKeyedSaveProbe : public KeyedSaveProbe {
public:
    explicit DerivedKeyedSaveProbe(CKContext *context) : KeyedSaveProbe(context) {}
    CK_CLASSID GetClassID() override { return CKCID_ANIMATION; }
};

class KeyedAnimationSaveTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    void PrepareFile(CKFile &file) {
        // CKFile::StartSave initializes this table before dependency collection.
        // Avoid producing a disk file: the actual SaveObject/PreSave/hash-table
        // path and the state's file-index writer are the code under test.
        file.m_IndexByClassId.Resize(80);
    }

    void ExpectSnapshot(CKStateChunk *parent, CKObject *object, CKDWORD payload) {
        EXPECT_EQ(object->GetID(), parent->ReadObjectID());
        CKStateChunk *sub = parent->ReadSubChunk();
        ASSERT_NE(nullptr, sub);
        EXPECT_EQ(CKCID_OBJECTANIMATION, sub->GetChunkClassID());
        EXPECT_EQ(CHUNK_DEV_2_1, sub->GetDataVersion());
        EXPECT_EQ(12, sub->GetDataSize());
        sub->StartRead();
        // Literal independent wire: identifier, terminating link, payload.
        EXPECT_EQ(0x6B51u, sub->ReadDword());
        EXPECT_EQ(0u, sub->ReadDword());
        EXPECT_EQ(payload, sub->ReadDword());
        DeleteCKStateChunk(sub);
    }
};

TEST_F(KeyedAnimationSaveTest, MemoryFlagCombinationsSelectExactFieldsAndForwardChildFlags) {
    for (unsigned mask = 0; mask < 8; ++mask) {
        SCOPED_TRACE(mask);
        KeyedSaveProbe keyed(Context);
        SnapshotAnimation child(Context, 0x1234ABCDu);
        keyed.SetMergeFields(TRUE, 0.375f);
        ASSERT_EQ(CK_OK, keyed.AddAnimation(&child));
        const CKDWORD flags = ((mask & 1) ? 0x1000u : 0u)
                            | ((mask & 2) ? 0x100000u : 0u)
                            | ((mask & 4) ? 0x200000u : 0u);
        CKStateChunk *chunk = keyed.Save(nullptr, flags);
        ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_KEYEDANIMATION, chunk->GetChunkClassID());
        EXPECT_EQ(CHUNK_DEV_2_1, chunk->GetDataVersion());
        EXPECT_EQ(-1, chunk->GetCurrentPos());
        chunk->StartRead();
        EXPECT_EQ((mask & 1) != 0, chunk->SeekIdentifier(0x1000u) != FALSE);
        if (mask & 1) {
            EXPECT_EQ(1, chunk->StartReadSequence());
            EXPECT_EQ(child.GetID(), chunk->ReadObjectID());
        }
        EXPECT_EQ((mask & 2) != 0, chunk->SeekIdentifier(0x100000u) != FALSE);
        if (mask & 2) {
            EXPECT_EQ(TRUE, chunk->ReadInt());
            EXPECT_FLOAT_EQ(0.375f, chunk->ReadFloat());
        }
        EXPECT_EQ((mask & 4) != 0, chunk->SeekIdentifier(0x200000u) != FALSE);
        if (mask & 4) {
            EXPECT_EQ(1u, chunk->ReadDword());
            ExpectSnapshot(chunk, &child, 0x1234ABCDu);
            EXPECT_EQ(1, child.Calls);
            EXPECT_EQ(nullptr, child.LastFile);
            EXPECT_EQ(flags, child.LastFlags);
        } else {
            EXPECT_EQ(0, child.Calls);
        }
        DeleteCKStateChunk(chunk);
    }
}

TEST_F(KeyedAnimationSaveTest, EmbeddedSnapshotsPreserveNullSlotOrderAndIndependentPayloads) {
    KeyedSaveProbe keyed(Context);
    SnapshotAnimation first(Context, 0x11223344u), second(Context, 0x55667788u);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first));
    keyed.AppendEmptySlot();
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&second));
    const CKDWORD flags = 0x200000u | 0x40u;
    CKStateChunk *chunk = keyed.Save(nullptr, flags);
    ASSERT_NE(nullptr, chunk);
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x200000u));
    EXPECT_EQ(3u, chunk->ReadDword());
    ExpectSnapshot(chunk, &first, 0x11223344u);
    EXPECT_EQ(0u, chunk->ReadObjectID());
    EXPECT_EQ(nullptr, chunk->ReadSubChunk());
    ExpectSnapshot(chunk, &second, 0x55667788u);
    EXPECT_EQ(flags, first.LastFlags);
    EXPECT_EQ(flags, second.LastFlags);
    DeleteCKStateChunk(chunk);
}

TEST_F(KeyedAnimationSaveTest, FileForcesListAndMergeAndSuppressesEmbeddedSave) {
    KeyedSaveProbe keyed(Context);
    SnapshotAnimation child(Context, 0xABCDEF01u);
    keyed.SetMergeFields(TRUE, 0.625f);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&child));
    CKFile file(Context);
    PrepareFile(file);
    file.SaveObject(&keyed, 0x200000u);
    ASSERT_EQ(2, file.m_FileObjects.Size());
    ASSERT_EQ(&child, file.m_FileObjects[0].ObjPtr);
    ASSERT_EQ(&keyed, file.m_FileObjects[1].ObjPtr);
    CKStateChunk *chunk = keyed.Save(&file, 0x200000u);
    ASSERT_NE(nullptr, chunk);
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x1000u));
    EXPECT_EQ(1, chunk->ReadInt());
    EXPECT_EQ(0, chunk->ReadInt()); // File index, independent of context ID.
    ASSERT_TRUE(chunk->SeekIdentifier(0x100000u));
    EXPECT_EQ(TRUE, chunk->ReadInt());
    EXPECT_FLOAT_EQ(0.625f, chunk->ReadFloat());
    EXPECT_FALSE(chunk->SeekIdentifier(0x200000u));
    EXPECT_EQ(0, child.Calls);
    DeleteCKStateChunk(chunk);
}

TEST_F(KeyedAnimationSaveTest, DerivedWriterRemainsOpenForAnAdditionalIdentifier) {
    DerivedKeyedSaveProbe keyed(Context);
    keyed.SetMergeFields(FALSE, 0.25f);
    CKStateChunk *chunk = keyed.Save(nullptr, 0x100000u);
    ASSERT_NE(nullptr, chunk);
    ASSERT_EQ(4, chunk->GetCurrentPos());
    EXPECT_EQ(16, chunk->GetDataSize());
    chunk->WriteIdentifier(0x7A51u);
    chunk->WriteDword(0xCAFEBABEu);
    chunk->CloseChunk();
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x100000u));
    EXPECT_EQ(FALSE, chunk->ReadInt());
    EXPECT_FLOAT_EQ(0.25f, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x7A51u));
    EXPECT_EQ(0xCAFEBABEu, chunk->ReadDword());
    DeleteCKStateChunk(chunk);
}

TEST_F(KeyedAnimationSaveTest, PreSaveCollectsOrderedUniqueChildrenBeforeParentAndKeepsFlags) {
    KeyedSaveProbe keyed(Context);
    SnapshotAnimation first(Context, 1), second(Context, 2);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&second));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first));
    keyed.AppendEmptySlot();
    CKFile file(Context);
    PrepareFile(file);
    const CKDWORD flags = 0x12304560u;
    file.SaveObject(&keyed, flags);
    ASSERT_EQ(3, file.m_FileObjects.Size());
    EXPECT_EQ(&first, file.m_FileObjects[0].ObjPtr);
    EXPECT_EQ(&second, file.m_FileObjects[1].ObjPtr);
    EXPECT_EQ(&keyed, file.m_FileObjects[2].ObjPtr);
    for (int index = 0; index < 3; ++index)
        EXPECT_EQ(flags, file.m_FileObjects[index].SaveFlags);
    keyed.PreSave(&file, 0xFFFFFFFFu);
    EXPECT_EQ(3, file.m_FileObjects.Size());
    EXPECT_EQ(flags, file.m_FileObjects[0].SaveFlags);
}

TEST_F(KeyedAnimationSaveTest, PreSaveUsesActualFileFiltersForDynamicAndExcludedChildren) {
    KeyedSaveProbe keyed(Context);
    SnapshotAnimation kept(Context, 1), dynamic(Context, 2), excluded(Context, 3);
    dynamic.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    excluded.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&dynamic));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&kept));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&excluded));
    CKFile file(Context);
    PrepareFile(file);
    file.SaveObject(&keyed, 0x1000u);
    ASSERT_EQ(2, file.m_FileObjects.Size());
    EXPECT_EQ(&kept, file.m_FileObjects[0].ObjPtr);
    EXPECT_EQ(&keyed, file.m_FileObjects[1].ObjPtr);
}

TEST_F(KeyedAnimationSaveTest, FailedPresentChildRejectsPartialSnapshotAndStopsLaterChildren) {
    KeyedSaveProbe keyed(Context);
    SnapshotAnimation first(Context, 0x11u), failed(Context, 0x22u), last(Context, 0x33u);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&failed));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&last));
    failed.Fail = true;
    CKStateChunk *chunk = keyed.Save(nullptr, 0x200000u);
    EXPECT_EQ(nullptr, chunk);
    DeleteCKStateChunk(chunk); // Also clean the successful baseline result.
    EXPECT_EQ(1, first.Calls);
    EXPECT_EQ(1, failed.Calls);
    EXPECT_EQ(0, last.Calls);
    // Failure leaves live child objects and the parent's list usable. A retry
    // executes complete snapshots rather than retaining a partial save.
    failed.Fail = false;
    chunk = keyed.Save(nullptr, 0x200000u);
    ASSERT_NE(nullptr, chunk);
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x200000u));
    EXPECT_EQ(3u, chunk->ReadDword());
    ExpectSnapshot(chunk, &first, 0x11u);
    ExpectSnapshot(chunk, &failed, 0x22u);
    ExpectSnapshot(chunk, &last, 0x33u);
    EXPECT_EQ(2, first.Calls);
    EXPECT_EQ(2, failed.Calls);
    EXPECT_EQ(1, last.Calls);
    DeleteCKStateChunk(chunk);
}

TEST_F(KeyedAnimationSaveTest, EmptyListSlotWritesSuccessfulNullObjectAndNullChunkPair) {
    KeyedSaveProbe keyed(Context);
    keyed.AppendEmptySlot();
    CKStateChunk *chunk = keyed.Save(nullptr, 0x200000u);
    ASSERT_NE(nullptr, chunk);
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x200000u));
    EXPECT_EQ(1u, chunk->ReadDword());
    EXPECT_EQ(0u, chunk->ReadObjectID());
    EXPECT_EQ(nullptr, chunk->ReadSubChunk());
    EXPECT_EQ(chunk->GetDataSize() / 4, chunk->GetCurrentPos());
    DeleteCKStateChunk(chunk);
}

} // namespace
