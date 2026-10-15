#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include "CKAll.h"
#include "RCKObjectAnimation.h"
#include "RCKKeyframeData.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

// Native 2.1 wire: key count, vertex count, one normal-presence DWORD, then
// each key's time, all XYZ positions and optional packed normals. These words
// are independent of DumpKeysTo and of the ObjectAnimation Save implementation.
const CKDWORD NormalWire[] = {
    2, 2, 1,
    0x40000000, 0x3f800000, 0x40000000, 0x40400000,
    0xc0800000, 0x40a00000, 0x40c00000, 0x00100020, 0x00300040,
    0x41200000, 0x41100000, 0x41200000, 0x41300000,
    0x40800000, 0x41500000, 0x41600000, 0x00500060, 0x00700080
};
const CKDWORD PositionWire[] = {
    2, 2, 0,
    0x40000000, 0x3f800000, 0x40000000, 0x40400000,
    0xc0800000, 0x40a00000, 0x40c00000,
    0x41200000, 0x41100000, 0x41200000, 0x41300000,
    0x40800000, 0x41500000, 0x41600000
};

void VectorEquals(const VxVector &actual, float x, float y, float z) {
    EXPECT_FLOAT_EQ(x, actual.x);
    EXPECT_FLOAT_EQ(y, actual.y);
    EXPECT_FLOAT_EQ(z, actual.z);
}

CKMorphKey *Key(RCKMorphController &controller, int index) {
    return static_cast<CKMorphKey *>(controller.GetKey(index));
}

class MorphStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static RCKMorphController *Controller(RCKObjectAnimation &animation) {
        return static_cast<RCKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
    }

    static void Fill(RCKMorphController &controller, CKBOOL normals) {
        controller.SetMorphVertexCount(2);
        for (int i = 0; i < 2; ++i) {
            const int index = controller.AddKey(i ? 10.0f : 2.0f, normals);
            ASSERT_EQ(i, index);
            CKMorphKey *key = Key(controller, index);
            ASSERT_NE(nullptr, key);
            ASSERT_NE(nullptr, key->PosArray);
            key->PosArray[0] = VxVector(i ? 9.0f : 1.0f, i ? 10.0f : 2.0f, i ? 11.0f : 3.0f);
            key->PosArray[1] = VxVector(i ? 4.0f : -4.0f, i ? 13.0f : 5.0f, i ? 14.0f : 6.0f);
            if (normals) {
                ASSERT_NE(nullptr, key->NormArray);
                const CKDWORD packed[2] = { i ? 0x00500060u : 0x00100020u, i ? 0x00700080u : 0x00300040u };
                std::memcpy(key->NormArray, packed, sizeof(packed));
            }
        }
    }

    static void DumpEquals(RCKMorphController &controller, const CKDWORD *expected, int words) {
        ASSERT_LE(words, 64);
        CKDWORD result[64];
        for (int i = 0; i < 64; ++i) result[i] = 0xcccccccc;
        ASSERT_EQ(words * 4, controller.DumpKeysTo(nullptr));
        ASSERT_EQ(words * 4, controller.DumpKeysTo(result));
        EXPECT_EQ(0, std::memcmp(result, expected, words * 4));
        EXPECT_EQ(0xccccccccu, result[words]);
    }

    static void SaveEquals(RCKObjectAnimation &animation, const CKDWORD *expected, int words) {
        CKStateChunk *saved = animation.Save(nullptr, CK_STATESAVE_OBJANIMALL);
        ASSERT_NE(nullptr, saved);
        saved->StartRead();
        const CKBOOL found = saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
        EXPECT_TRUE(found);
        if (found) {
            saved->Skip(10); // root/reserved, flags, entity, animation length
            EXPECT_EQ(CKANIMATION_MORPH_CONTROL, saved->ReadDword());
            EXPECT_EQ(static_cast<CKDWORD>(words), saved->ReadDword());
            EXPECT_EQ(0, std::memcmp(saved->LockReadBuffer(), expected, words * 4));
            saved->Skip(words);
            EXPECT_EQ(0u, saved->ReadDword());
        }
        DeleteCKStateChunk(saved);
    }

    static void BeginAnimation(CKStateChunk &chunk) {
        chunk.StartWrite();
        chunk.SetDataVersion(CHUNKDATA_CURRENTVERSION);
        chunk.WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
        for (int i = 0; i < 7; ++i) chunk.WriteFloat(0.0f);
        chunk.WriteDword(0);
        chunk.WriteObject(nullptr);
        chunk.WriteFloat(20.0f);
    }

    static void WriteMorph(CKStateChunk &chunk, const CKDWORD *words, int count) {
        chunk.WriteDword(CKANIMATION_MORPH_CONTROL);
        chunk.WriteDword(count);
        chunk.WriteBufferNoSize_LEndian(count * 4, const_cast<CKDWORD *>(words));
    }

    static void EndAnimation(CKStateChunk &chunk) {
        chunk.WriteDword(0);
        chunk.CloseChunk();
        chunk.StartRead();
    }
};

TEST_F(MorphStorageTest, TimeInsertionOrdersKeysAndPreservesExistingOwnedPayloads) {
    RCKObjectAnimation animation(Context);
    RCKMorphController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    controller->SetMorphVertexCount(1);
    ASSERT_EQ(0, controller->AddKey(8.0f, FALSE));
    CKMorphKey *last = Key(*controller, 0);
    ASSERT_NE(nullptr, last);
    last->PosArray[0] = VxVector(8, 16, 24);
    VxVector *lastPayload = last->PosArray;
    ASSERT_EQ(0, controller->AddKey(2.0f, FALSE));
    Key(*controller, 0)->PosArray[0] = VxVector(2, 4, 6);
    ASSERT_EQ(1, controller->AddKey(5.0f, FALSE));
    Key(*controller, 1)->PosArray[0] = VxVector(5, 10, 15);
    EXPECT_EQ(lastPayload, Key(*controller, 2)->PosArray);
    EXPECT_FLOAT_EQ(2, Key(*controller, 0)->TimeStep);
    EXPECT_FLOAT_EQ(5, Key(*controller, 1)->TimeStep);
    EXPECT_FLOAT_EQ(8, Key(*controller, 2)->TimeStep);
    VxVector output;
    ASSERT_TRUE(animation.EvaluateMorphTarget(6.5f, 1, &output, sizeof(output), nullptr));
    VectorEquals(output, 6.5f, 13, 19.5f);
}

TEST_F(MorphStorageTest, TypedInsertionCopiesBorrowedPositionsAndNormalsBeforeSourceDies) {
    RCKObjectAnimation animation(Context);
    RCKMorphController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    controller->SetMorphVertexCount(2);
    {
        VxVector positions[2] = { VxVector(1, 2, 3), VxVector(-4, 5, 6) };
        VxCompressedVector normals[2];
        const CKDWORD packed[2] = { 0x00100020, 0x00300040 };
        std::memcpy(normals, packed, sizeof(packed));
        CKMorphKey source;
        source.TimeStep = 2;
        source.PosArray = positions;
        source.NormArray = normals;
        ASSERT_EQ(0, controller->AddKey(&source, FALSE));
        EXPECT_NE(positions, Key(*controller, 0)->PosArray);
        EXPECT_NE(normals, Key(*controller, 0)->NormArray);
        positions[0] = VxVector(-1000);
        normals[0].xa = 0;
    }
    const CKDWORD expected[] = { 1, 2, 1, 0x40000000, 0x3f800000, 0x40000000, 0x40400000,
        0xc0800000, 0x40a00000, 0x40c00000, 0x00100020, 0x00300040 };
    SaveEquals(animation, expected, 12);
}

TEST_F(MorphStorageTest, GenericInsertionThroughBaseRequestsNormalsAndCopiesPositionData) {
    RCKMorphController controller;
    controller.SetMorphVertexCount(1);
    VxVector position(3, 6, 9);
    CKMorphKey source;
    source.TimeStep = 4;
    source.PosArray = &position;
    source.NormArray = nullptr;
    CKAnimController *base = &controller;
    ASSERT_EQ(0, base->AddKey(&source));
    CKMorphKey *key = Key(controller, 0);
    ASSERT_NE(nullptr, key);
    ASSERT_NE(nullptr, key->NormArray);
    key->NormArray[0].xa = 0x1234;
    key->NormArray[0].ya = 0x5678;
    position = VxVector(-1);
    const CKDWORD expected[] = { 1, 1, 1, 0x40800000, 0x40400000, 0x40c00000, 0x41100000, 0x56781234 };
    DumpEquals(controller, expected, 8);
}

TEST_F(MorphStorageTest, InsertCanBorrowAnExistingPayloadWithoutTransferringItsOwnership) {
    RCKMorphController controller;
    Fill(controller, TRUE);
    CKMorphKey source = *Key(controller, 1);
    VxVector *oldPositions = source.PosArray;
    VxCompressedVector *oldNormals = source.NormArray;
    source.TimeStep = 6;
    ASSERT_EQ(1, controller.AddKey(&source, FALSE));
    EXPECT_EQ(oldPositions, Key(controller, 2)->PosArray);
    EXPECT_EQ(oldNormals, Key(controller, 2)->NormArray);
    EXPECT_NE(oldPositions, Key(controller, 1)->PosArray);
    EXPECT_NE(oldNormals, Key(controller, 1)->NormArray);
    oldPositions[0].x = 99;
    oldNormals[0].xa = 99;
    VectorEquals(Key(controller, 1)->PosArray[0], 9, 10, 11);
    EXPECT_EQ(0x60, Key(controller, 1)->NormArray[0].xa);
    controller.RemoveKey(2);
    VxVector output[2];
    ASSERT_TRUE(controller.Evaluate(6, 2, output, sizeof(VxVector), nullptr));
    VectorEquals(output[0], 9, 10, 11);
}

TEST_F(MorphStorageTest, DuplicateAndNullInsertionsKeepExistingDataAndNormalPresence) {
    RCKMorphController controller;
    Fill(controller, FALSE);
    VxVector replacement[2] = { VxVector(77), VxVector(88) };
    CKMorphKey source;
    source.TimeStep = 2;
    source.PosArray = replacement;
    source.NormArray = nullptr;
    VxVector *first = Key(controller, 0)->PosArray;
    EXPECT_EQ(0, controller.AddKey(&source, TRUE));
    EXPECT_EQ(1, controller.AddKey(10, TRUE));
    EXPECT_EQ(-1, controller.AddKey(static_cast<CKKey *>(nullptr), FALSE));
    EXPECT_EQ(-1, controller.AddKey(static_cast<CKKey *>(nullptr)));
    EXPECT_EQ(first, Key(controller, 0)->PosArray);
    EXPECT_EQ(nullptr, Key(controller, 0)->NormArray);
    EXPECT_EQ(nullptr, Key(controller, 1)->NormArray);
    DumpEquals(controller, PositionWire, 17);
}

TEST_F(MorphStorageTest, RemovingMiddleRetainsSurvivorArraysAndChangesActualInterpolation) {
    RCKObjectAnimation animation(Context);
    RCKMorphController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller, TRUE);
    CKMorphKey source = *Key(*controller, 1);
    source.TimeStep = 6;
    ASSERT_EQ(1, controller->AddKey(&source, FALSE));
    Key(*controller, 1)->PosArray[0] = VxVector(100);
    VxVector before[2], after[2];
    ASSERT_TRUE(animation.EvaluateMorphTarget(6, 2, before, sizeof(VxVector), nullptr));
    VectorEquals(before[0], 100, 100, 100);
    VxVector *last = Key(*controller, 2)->PosArray;
    VxCompressedVector *lastNormals = Key(*controller, 2)->NormArray;
    controller->RemoveKey(1);
    EXPECT_EQ(last, Key(*controller, 1)->PosArray);
    EXPECT_EQ(lastNormals, Key(*controller, 1)->NormArray);
    ASSERT_TRUE(animation.EvaluateMorphTarget(6, 2, after, sizeof(VxVector), nullptr));
    VectorEquals(after[0], 5, 6, 7);
    VectorEquals(after[1], 0, 9, 10);
    SaveEquals(animation, NormalWire, 21);
}

TEST_F(MorphStorageTest, RemovingFirstThenLastLeavesConfiguredEmptyControllerReusable) {
    RCKMorphController controller;
    Fill(controller, TRUE);
    controller.SetLength(27);
    VxVector *last = Key(controller, 1)->PosArray;
    controller.RemoveKey(0);
    ASSERT_EQ(1, controller.GetKeyCount());
    EXPECT_EQ(last, Key(controller, 0)->PosArray);
    EXPECT_FLOAT_EQ(10, Key(controller, 0)->TimeStep);
    controller.RemoveKey(0);
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(2, controller.GetMorphVertexCount());
    EXPECT_FLOAT_EQ(27, controller.GetLength());
    const CKDWORD empty[] = { 0, 2, 0 };
    DumpEquals(controller, empty, 3);
    ASSERT_EQ(0, controller.AddKey(7, FALSE));
    CKMorphKey *key = Key(controller, 0);
    ASSERT_NE(nullptr, key);
    key->PosArray[0] = VxVector(3, 4, 5);
    key->PosArray[1] = VxVector(6, 7, 8);
    VxVector output[2];
    ASSERT_TRUE(controller.Evaluate(7, 2, output, sizeof(VxVector), nullptr));
    VectorEquals(output[1], 6, 7, 8);
}

TEST_F(MorphStorageTest, InvalidRemovalDoesNotReleaseOrChangeSurvivingPayloads) {
    RCKMorphController controller;
    Fill(controller, TRUE);
    VxVector *first = Key(controller, 0)->PosArray;
    VxVector *last = Key(controller, 1)->PosArray;
    controller.RemoveKey(-1);
    controller.RemoveKey(2);
    controller.RemoveKey(INT_MAX);
    EXPECT_EQ(first, Key(controller, 0)->PosArray);
    EXPECT_EQ(last, Key(controller, 1)->PosArray);
    DumpEquals(controller, NormalWire, 21);
}

TEST_F(MorphStorageTest, UniformNormalDumpMatchesLiteralWireAndActualAnimationSave) {
    RCKObjectAnimation animation(Context);
    RCKMorphController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller, TRUE);
    DumpEquals(*controller, NormalWire, 21);
    SaveEquals(animation, NormalWire, 21);
}

TEST_F(MorphStorageTest, PositionOnlyDumpMatchesLiteralWireAndActualAnimationSave) {
    RCKObjectAnimation animation(Context);
    RCKMorphController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller, FALSE);
    DumpEquals(*controller, PositionWire, 17);
    SaveEquals(animation, PositionWire, 17);
}

TEST_F(MorphStorageTest, MixedNormalKeysSavePositionsForEitherPresenceOrder) {
    for (int firstHasNormals = 0; firstHasNormals < 2; ++firstHasNormals) {
        RCKObjectAnimation animation(Context);
        RCKMorphController *controller = Controller(animation);
        ASSERT_NE(nullptr, controller);
        Fill(*controller, firstHasNormals != 0);
        CKMorphKey *key = Key(*controller, 1);
        if (firstHasNormals) {
            delete[] key->NormArray;
            key->NormArray = nullptr;
        } else {
            key->NormArray = new VxCompressedVector[2];
            key->NormArray[0].xa = 1; key->NormArray[0].ya = 2;
            key->NormArray[1].xa = 3; key->NormArray[1].ya = 4;
        }
        SaveEquals(animation, PositionWire, 17);
        EXPECT_EQ(firstHasNormals != 0, Key(*controller, 0)->NormArray != nullptr);
        EXPECT_EQ(firstHasNormals == 0, Key(*controller, 1)->NormArray != nullptr);
    }
}

TEST_F(MorphStorageTest, ZeroVertexNormalPresenceAndEmptyConfiguredHeaderAreObservable) {
    RCKMorphController controller;
    ASSERT_EQ(0, controller.AddKey(2, TRUE));
    ASSERT_EQ(1, controller.AddKey(10, TRUE));
    const CKDWORD zeroVertices[] = { 2, 0, 1, 0x40000000, 0x41200000 };
    DumpEquals(controller, zeroVertices, 5);
    RCKMorphController empty;
    empty.SetMorphVertexCount(7);
    const CKDWORD configured[] = { 0, 7, 0 };
    DumpEquals(empty, configured, 3);
}

TEST_F(MorphStorageTest, DumpWritesExactlyItsReportedBytesIntoUnalignedStorage) {
    RCKMorphController controller;
    Fill(controller, TRUE);
    unsigned char bytes[sizeof(NormalWire) + 2];
    std::memset(bytes, 0xa5, sizeof(bytes));
    ASSERT_EQ(static_cast<int>(sizeof(NormalWire)), controller.DumpKeysTo(bytes + 1));
    EXPECT_EQ(0xa5, bytes[0]);
    EXPECT_EQ(0xa5, bytes[sizeof(bytes) - 1]);
    EXPECT_EQ(0, std::memcmp(bytes + 1, NormalWire, sizeof(NormalWire)));
}

TEST_F(MorphStorageTest, RawReadCopiesLiteralPayloadAndRetainsControllerLength) {
    RCKMorphController controller;
    controller.SetLength(73);
    {
        CKDWORD words[21]; std::memcpy(words, NormalWire, sizeof(words));
        ASSERT_EQ(static_cast<int>(sizeof(words)), controller.ReadKeysFrom(words));
        words[4] = 0;
        words[10] = 0;
    }
    EXPECT_FLOAT_EQ(73, controller.GetLength());
    ASSERT_EQ(2, controller.GetKeyCount());
    EXPECT_EQ(2, controller.GetMorphVertexCount());
    VectorEquals(Key(controller, 0)->PosArray[0], 1, 2, 3);
    EXPECT_EQ(0x40, Key(controller, 0)->NormArray[1].xa);
    VxVector output[2];
    ASSERT_TRUE(controller.Evaluate(6, 2, output, sizeof(VxVector), nullptr));
    VectorEquals(output[0], 5, 6, 7);
    VectorEquals(output[1], 0, 9, 10);
    DumpEquals(controller, NormalWire, 21);
}

TEST_F(MorphStorageTest, RawReadTreatsAnyNonzeroNormalFlagAsPresent) {
    CKDWORD words[21]; std::memcpy(words, NormalWire, sizeof(words));
    words[2] = 0x80000000;
    RCKMorphController controller;
    ASSERT_EQ(static_cast<int>(sizeof(words)), controller.ReadKeysFrom(words));
    EXPECT_EQ(0x80, Key(controller, 1)->NormArray[1].xa);
    EXPECT_EQ(0x70, Key(controller, 1)->NormArray[1].ya);
    DumpEquals(controller, NormalWire, 21);
}

TEST_F(MorphStorageTest, RawReadAcceptsUnalignedLiteralAndPositionOnlyReplacement) {
    RCKMorphController controller;
    Fill(controller, TRUE);
    unsigned char bytes[sizeof(PositionWire) + 1];
    std::memcpy(bytes + 1, PositionWire, sizeof(PositionWire));
    ASSERT_EQ(static_cast<int>(sizeof(PositionWire)), controller.ReadKeysFrom(bytes + 1));
    EXPECT_EQ(nullptr, Key(controller, 0)->NormArray);
    EXPECT_EQ(nullptr, Key(controller, 1)->NormArray);
    DumpEquals(controller, PositionWire, 17);
}

TEST_F(MorphStorageTest, RawReadNullAndUnrepresentableHeaderPreserveExistingController) {
    RCKMorphController controller;
    Fill(controller, TRUE);
    controller.SetLength(19);
    VxVector *positions = Key(controller, 0)->PosArray;
    CKDWORD overflow[] = { 1, INT_MAX, 1 };
    EXPECT_EQ(0, controller.ReadKeysFrom(nullptr));
    EXPECT_EQ(0, controller.ReadKeysFrom(overflow));
    EXPECT_EQ(positions, Key(controller, 0)->PosArray);
    EXPECT_FLOAT_EQ(19, controller.GetLength());
    DumpEquals(controller, NormalWire, 21);
}

TEST_F(MorphStorageTest, RawReadEmptyConfiguredControllerConsumesOnlyItsCompleteHeader) {
    RCKMorphController controller;
    Fill(controller, TRUE);
    const CKDWORD words[] = { 0, 17, 0, 0xdeadbeef };
    ASSERT_EQ(12, controller.ReadKeysFrom(const_cast<CKDWORD *>(words)));
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(17, controller.GetMorphVertexCount());
    const CKDWORD expected[] = { 0, 17, 0 };
    DumpEquals(controller, expected, 3);
}

TEST_F(MorphStorageTest, BoundedAnimationLoadReadsLiteralMorphForActualSamplingAndSave) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteMorph(chunk, NormalWire, 21);
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetMorphController());
    EXPECT_EQ(2, animation.GetMorphVertexCount());
    VxVector output[2];
    ASSERT_TRUE(animation.EvaluateMorphTarget(6, 2, output, sizeof(VxVector), nullptr));
    VectorEquals(output[0], 5, 6, 7);
    VectorEquals(output[1], 0, 9, 10);
    SaveEquals(animation, NormalWire, 21);
}

TEST_F(MorphStorageTest, BoundedAnimationLoadRejectsTruncatedPayloadBeforeFollowingIdentifier) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteMorph(chunk, NormalWire, 8); // Header claims 21 words; record ends at 8.
    chunk.WriteDword(0);
    chunk.WriteIdentifier(0x76543210);
    for (int i = 0; i < 32; ++i) chunk.WriteDword(0x42424242);
    chunk.CloseChunk(); chunk.StartRead();
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetMorphController());
    chunk.StartRead();
    ASSERT_TRUE(chunk.SeekIdentifier(0x76543210));
    EXPECT_EQ(0x42424242u, chunk.ReadDword());
}

TEST_F(MorphStorageTest, BoundedAnimationLoadSkipsDeclaredPaddingAndReadsTheNextController) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    CKDWORD padded[23];
    std::memcpy(padded, NormalWire, sizeof(NormalWire));
    padded[21] = 0xdeadbeef; padded[22] = 0xbaadf00d;
    WriteMorph(chunk, padded, 23);
    const CKDWORD position[] = { 1, 0x40000000, 0x40800000, 0x40a00000, 0x40c00000 };
    chunk.WriteDword(CKANIMATION_LINPOS_CONTROL);
    chunk.WriteDword(5);
    chunk.WriteBufferNoSize_LEndian(sizeof(position), const_cast<CKDWORD *>(position));
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetPositionController());
    ASSERT_NE(nullptr, animation.GetMorphController());
    VxVector positionOutput, morphOutput[2];
    ASSERT_TRUE(animation.EvaluatePosition(2, positionOutput));
    VectorEquals(positionOutput, 4, 5, 6);
    ASSERT_TRUE(animation.EvaluateMorphTarget(6, 2, morphOutput, sizeof(VxVector), nullptr));
    VectorEquals(morphOutput[1], 0, 9, 10);
}

} // namespace
