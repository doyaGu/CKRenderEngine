#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

// These frames are assembled independently of Animation::Save and the chunk
// writer. Identifier links are payload DWORD offsets, not byte offsets.
void ReadWire(CKStateChunk &chunk, const CKDWORD *payload, int words,
              CKBOOL legacy = FALSE, CKBOOL file = FALSE,
              const int *ids = nullptr, int idCount = 0) {
    CKDWORD wire[96] = {};
    int pos = 0;
    if (legacy) {
        wire[pos++] = 0x00040000u; // Chunk version 4, data version 0.
        wire[pos++] = CKCID_ANIMATION;
        wire[pos++] = words;
        wire[pos++] = 0;
        wire[pos++] = idCount;
        wire[pos++] = 0;
    } else {
        const CKDWORD options = (file ? 8u : 0u) | (idCount ? 1u : 0u);
        wire[pos++] = (options << 24) | 0x0007100au;
        wire[pos++] = words;
    }
    std::memcpy(wire + pos, payload, words * sizeof(CKDWORD));
    pos += words;
    if (!legacy && idCount)
        wire[pos++] = idCount;
    for (int i = 0; i < idCount; ++i)
        wire[pos++] = ids[i];
    ASSERT_TRUE(chunk.ConvertFromBuffer(wire));
}

class AnimationState : public RCKAnimation {
public:
    explicit AnimationState(CKContext *context) : RCKAnimation(context, "Animation") {}
    void References(RCK3dEntity *root, RCKCharacter *character) {
        m_RootEntity = root;
        m_Character = character;
    }
};

class DerivedAnimationState : public AnimationState {
public:
    explicit DerivedAnimationState(CKContext *context) : AnimationState(context) {}
    CK_CLASSID GetClassID() override { return CKCID_KEYEDANIMATION; }
};

class AnimationSerializationTest : public ::testing::Test {
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

TEST_F(AnimationSerializationTest, ModernIndependentFrameRestoresStateAndDiscardsOldBodyList) {
    RCK3dEntity oldFirst(Context), oldSecond(Context), root(Context);
    RCKCharacter character(Context);
    AnimationState animation(Context);
    CKDWORD payload[] = {
        0x10, 4, 0x00120510, 0x41c00000, // Flags, 24 fps.
        0x40, 7, 0x42140000,             // 37 frames.
        0x80, 13, 2, oldFirst.GetID(), oldSecond.GetID(), root.GetID(),
        0x100, 16, character.GetID(),
        0x200, 0, 0x3ec00000            // Step 0.375.
    };
    const int ids[] = {-1, 9, 12, 15};
    CKStateChunk chunk(CKCID_ANIMATION, nullptr);
    ReadWire(chunk, payload, 19, FALSE, FALSE, ids, 4);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_EQ(0x00120510u, animation.GetFlags());
    EXPECT_FLOAT_EQ(24.0f, animation.GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(37.0f, animation.GetLength());
    EXPECT_FLOAT_EQ(0.375f, animation.GetStep());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
}

TEST_F(AnimationSerializationTest, LegacyTwelveByteFlagsChangeOnlyLinkAndInterruptBits) {
    const CKDWORD unrelated = 0x00120510u;
    AnimationState animation(Context);
    for (int bits = 0; bits < 4; ++bits) {
        SCOPED_TRACE(bits);
        animation.SetFlags(unrelated | CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK);
        CKDWORD payload[] = {0x10, 5, (bits & 2) ? 7u : 0u,
                             (bits & 1) ? 3u : 0u, 0x41b00000,
                             0x40, 0, 0x42040000}; // 22 fps, 33 frames.
        CKStateChunk chunk(CKCID_ANIMATION, nullptr);
        ReadWire(chunk, payload, 8, TRUE);
        ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
        EXPECT_EQ(unrelated | ((bits & 1) ? 1u : 0u) | ((bits & 2) ? 4u : 0u), animation.GetFlags());
        EXPECT_FLOAT_EQ(22.0f, animation.GetLinkedFrameRate());
        EXPECT_FLOAT_EQ(33.0f, animation.GetLength());
    }
}

TEST_F(AnimationSerializationTest, UnknownSettingsPayloadLeavesSettingsAndReadsOtherIdentifiers) {
    AnimationState animation(Context);
    animation.SetFlags(0x00120515);
    animation.LinkToFrameRate(TRUE, 17.0f);
    CKDWORD payload[] = {0x10, 6, 0, 0, 0, 0, 0x40, 9, 0x42280000,
                         0x200, 0, 0x3f200000}; // Unknown 16-byte settings; 42, 0.625.
    CKStateChunk chunk(CKCID_ANIMATION, nullptr);
    ReadWire(chunk, payload, 12);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_EQ(0x00120515u, animation.GetFlags());
    EXPECT_FLOAT_EQ(17.0f, animation.GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(42.0f, animation.GetLength());
    EXPECT_FLOAT_EQ(0.625f, animation.GetStep());
}

TEST_F(AnimationSerializationTest, PartialFramePreservesAbsentFieldsAndLoadsBaseVisibility) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationState animation(Context);
    animation.References(&root, &character);
    animation.SetFlags(0x00120515);
    animation.LinkToFrameRate(TRUE, 19.0f);
    animation.SetStep(0.75f);
    CKDWORD payload[] = {CK_STATESAVE_OBJECTHIDDEN, 2, 0x40, 0, 0x42a20000}; // 81.
    CKStateChunk chunk(CKCID_ANIMATION, nullptr);
    ReadWire(chunk, payload, 5);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_FLOAT_EQ(81.0f, animation.GetLength());
    EXPECT_EQ(0x00120515u, animation.GetFlags());
    EXPECT_FLOAT_EQ(19.0f, animation.GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(0.75f, animation.GetStep());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
    EXPECT_FALSE(animation.IsVisible());
}

TEST_F(AnimationSerializationTest, FileRelativeIndependentFrameResolvesCreatedObjectIds) {
    RCK3dEntity oldBody(Context), root(Context);
    RCKCharacter character(Context);
    AnimationState animation(Context);
    CKFile file(Context);
    file.m_FileObjects.Resize(3);
    file.m_FileObjects[0].CreatedObject = oldBody.GetID();
    file.m_FileObjects[1].CreatedObject = character.GetID();
    file.m_FileObjects[2].CreatedObject = root.GetID();
    CKDWORD payload[] = {0x80, 5, 1, 0, 2, 0x100, 0, 1};
    CKStateChunk chunk(CKCID_ANIMATION, &file);
    ReadWire(chunk, payload, 8, FALSE, TRUE);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, &file));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
}

TEST_F(AnimationSerializationTest, SelectiveSaveIncludesExactlyRequestedAnimationFields) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationState animation(Context);
    animation.References(&root, &character);
    animation.SetFlags(0x00120515);
    animation.LinkToFrameRate(TRUE, 23.0f);
    animation.SetLength(51.0f);
    animation.SetStep(0.25f);
    const CKDWORD ids[] = {0x10, 0x40, 0x80, 0x100, 0x200};
    for (int mask = 0; mask < 32; ++mask) {
        SCOPED_TRACE(mask);
        CKDWORD flags = 0;
        for (int i = 0; i < 5; ++i)
            if (mask & (1 << i))
                flags |= ids[i];
        CKStateChunk *chunk = animation.Save(nullptr, flags);
        ASSERT_NE(nullptr, chunk);
        chunk->StartRead();
        for (int i = 0; i < 5; ++i)
            EXPECT_EQ((mask & (1 << i)) != 0, chunk->SeekIdentifier(ids[i]) != FALSE);
        if (mask & 1) {
            ASSERT_EQ(8, chunk->SeekIdentifierAndReturnSize(0x10));
            EXPECT_EQ(0x00120515u, chunk->ReadDword());
            EXPECT_FLOAT_EQ(23.0f, chunk->ReadFloat());
        }
        if (mask & 2) {
            ASSERT_EQ(4, chunk->SeekIdentifierAndReturnSize(0x40));
            EXPECT_FLOAT_EQ(51.0f, chunk->ReadFloat());
        }
        if (mask & 4) {
            ASSERT_EQ(8, chunk->SeekIdentifierAndReturnSize(0x80));
            EXPECT_EQ(0, chunk->ReadInt());
            EXPECT_EQ(root.GetID(), chunk->ReadObjectID());
        }
        if (mask & 8) {
            ASSERT_EQ(4, chunk->SeekIdentifierAndReturnSize(0x100));
            EXPECT_EQ(character.GetID(), chunk->ReadObjectID());
        }
        if (mask & 16) {
            ASSERT_EQ(4, chunk->SeekIdentifierAndReturnSize(0x200));
            EXPECT_FLOAT_EQ(0.25f, chunk->ReadFloat());
        }
        EXPECT_EQ(CKCID_ANIMATION, chunk->GetChunkClassID());
        DeleteCKStateChunk(chunk);
    }
}

TEST_F(AnimationSerializationTest, FileSaveWritesAllFieldsAndUsesFileTableIndices) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationState animation(Context);
    animation.References(&root, &character);
    animation.SetFlags(0x00120515);
    animation.LinkToFrameRate(TRUE, 23.0f);
    animation.SetLength(51.0f);
    animation.SetStep(0.25f);
    CKFile file(Context);
    file.m_ObjectsHashTable.Insert(root.GetID(), 6);
    file.m_ObjectsHashTable.Insert(character.GetID(), 9);
    CKStateChunk *chunk = animation.Save(&file, 0);
    ASSERT_NE(nullptr, chunk);
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x10));
    EXPECT_EQ(0x00120515u, chunk->ReadDword());
    EXPECT_FLOAT_EQ(23.0f, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x40));
    EXPECT_FLOAT_EQ(51.0f, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x80));
    EXPECT_EQ(0, chunk->ReadInt());
    EXPECT_EQ(6, chunk->ReadInt());
    ASSERT_TRUE(chunk->SeekIdentifier(0x100));
    EXPECT_EQ(9, chunk->ReadInt());
    ASSERT_TRUE(chunk->SeekIdentifier(0x200));
    EXPECT_FLOAT_EQ(0.25f, chunk->ReadFloat());
    DeleteCKStateChunk(chunk);
}

TEST_F(AnimationSerializationTest, DerivedSaveRemainsWritableForFollowingSubclassPayload) {
    DerivedAnimationState animation(Context);
    animation.SetLength(45.0f);
    CKStateChunk *chunk = animation.Save(nullptr, CK_STATESAVE_ANIMATIONLENGTH);
    ASSERT_NE(nullptr, chunk);
    chunk->WriteIdentifier(0x400);
    chunk->WriteDword(0x12345678);
    chunk->CloseChunk();
    chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x40));
    EXPECT_FLOAT_EQ(45.0f, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x400));
    EXPECT_EQ(0x12345678u, chunk->ReadDword());
    DeleteCKStateChunk(chunk);
}

TEST_F(AnimationSerializationTest, NullLoadRejectsWithoutChangingAnimationState) {
    AnimationState animation(Context);
    animation.SetFlags(0x00120515);
    animation.LinkToFrameRate(TRUE, 13.0f);
    animation.SetLength(65.0f);
    animation.SetStep(0.875f);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, animation.Load(nullptr, nullptr));
    EXPECT_EQ(0x00120515u, animation.GetFlags());
    EXPECT_FLOAT_EQ(13.0f, animation.GetLinkedFrameRate());
    EXPECT_FLOAT_EQ(65.0f, animation.GetLength());
    EXPECT_FLOAT_EQ(0.875f, animation.GetStep());
}

} // namespace
