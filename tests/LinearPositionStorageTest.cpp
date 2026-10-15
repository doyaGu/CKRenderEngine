#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKObjectAnimation.h"
#include "RCKKeyframeData.h"

extern void SetProcessorSpecific_FunctionsPtr();

// This executable links the controller implementation statically. Fail only
// its next nothrow array allocation, without changing CK2.dll allocation.
static thread_local bool FailNextLinearArray = false;

void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextLinearArray) {
        FailNextLinearArray = false;
        return nullptr;
    }
    try {
        return ::operator new[](bytes);
    } catch (...) {
        return nullptr;
    }
}

namespace {

// Virtools 2.1: one count DWORD followed by count records, each containing
// float time and XYZ. This fixture is independent of our Dump/Save routines.
const CKDWORD PositionWire[] = {
    2,
    0x40000000, 0x3f800000, 0x40000000, 0x40400000,
    0x41200000, 0x41100000, 0x41200000, 0x41300000
};

void VectorEquals(const VxVector &value, float x, float y, float z) {
    EXPECT_FLOAT_EQ(x, value.x);
    EXPECT_FLOAT_EQ(y, value.y);
    EXPECT_FLOAT_EQ(z, value.z);
}

CKPositionKey *Key(RCKLinearPositionController &controller, int index) {
    return static_cast<CKPositionKey *>(controller.GetKey(index));
}

CKPositionKey MakeKey(float time, float x, float y, float z) {
    CKPositionKey key;
    key.TimeStep = time;
    key.Pos = VxVector(x, y, z);
    return key;
}

class LinearPositionStorageTest : public ::testing::Test {
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
        FailNextLinearArray = false;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static RCKLinearPositionController *Controller(RCKObjectAnimation &animation) {
        return static_cast<RCKLinearPositionController *>(animation.CreateController(CKANIMATION_LINPOS_CONTROL));
    }

    static void Fill(RCKLinearPositionController &controller) {
        CKPositionKey first = MakeKey(2, 1, 2, 3);
        CKPositionKey last = MakeKey(10, 9, 10, 11);
        ASSERT_EQ(0, controller.AddKey(&first));
        ASSERT_EQ(1, controller.AddKey(&last));
    }

    static void DumpEquals(RCKLinearPositionController &controller, const CKDWORD *expected, int words) {
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
            saved->Skip(10); // Root/reserved, flags, entity, length.
            EXPECT_EQ(CKANIMATION_LINPOS_CONTROL, saved->ReadDword());
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
        for (int i = 0; i < 7; ++i) chunk.WriteFloat(0);
        chunk.WriteDword(0);
        chunk.WriteObject(nullptr);
        chunk.WriteFloat(20);
    }

    static void WritePosition(CKStateChunk &chunk, const CKDWORD *words, int count) {
        chunk.WriteDword(CKANIMATION_LINPOS_CONTROL);
        chunk.WriteDword(count);
        if (count)
            chunk.WriteBufferNoSize_LEndian(count * 4, const_cast<CKDWORD *>(words));
    }

    static void EndAnimation(CKStateChunk &chunk) {
        chunk.WriteDword(0);
        chunk.CloseChunk();
        chunk.StartRead();
    }
};

TEST_F(LinearPositionStorageTest, InsertingBeforeBetweenAndAfterKeysRetainsAscendingWire) {
    RCKLinearPositionController controller;
    CKPositionKey last = MakeKey(10, 9, 10, 11);
    CKPositionKey first = MakeKey(2, 1, 2, 3);
    CKPositionKey middle = MakeKey(6, 5, 6, 7);
    ASSERT_EQ(0, controller.AddKey(&last));
    ASSERT_EQ(0, controller.AddKey(&first));
    ASSERT_EQ(1, controller.AddKey(&middle));
    const CKDWORD expected[] = { 3,
        0x40000000, 0x3f800000, 0x40000000, 0x40400000,
        0x40c00000, 0x40a00000, 0x40c00000, 0x40e00000,
        0x41200000, 0x41100000, 0x41200000, 0x41300000 };
    DumpEquals(controller, expected, 13);
}

TEST_F(LinearPositionStorageTest, InsertCopiesCallerKeyBeforeItsMutationAndDestruction) {
    RCKObjectAnimation animation(Context);
    RCKLinearPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    {
        CKPositionKey source = MakeKey(2, 1, 2, 3);
        ASSERT_EQ(0, controller->AddKey(&source));
        EXPECT_NE(&source, controller->GetKey(0));
        source = MakeKey(99, -1000, -1000, -1000);
    }
    const CKDWORD expected[] = { 1, 0x40000000, 0x3f800000, 0x40000000, 0x40400000 };
    SaveEquals(animation, expected, 5);
    VxVector output;
    ASSERT_TRUE(animation.EvaluatePosition(2, output));
    VectorEquals(output, 1, 2, 3);
}

TEST_F(LinearPositionStorageTest, DuplicateSignedZeroReplacesPositionAndPreservesOriginalTimeBits) {
    RCKLinearPositionController controller;
    const CKDWORD initial[] = { 1, 0x80000000, 0x3f800000, 0x40000000, 0x40400000 };
    ASSERT_EQ(20, controller.ReadKeysFrom(const_cast<CKDWORD *>(initial)));
    CKPositionKey replacement = MakeKey(0, 4, 5, 6);
    CKKey *original = controller.GetKey(0);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    EXPECT_EQ(original, controller.GetKey(0));
    const CKDWORD expected[] = { 1, 0x80000000, 0x40800000, 0x40a00000, 0x40c00000 };
    DumpEquals(controller, expected, 5);
}

TEST_F(LinearPositionStorageTest, DuplicateLoadedTimesReplaceOnlyTheFirstMatchingRecord) {
    RCKLinearPositionController controller;
    const CKDWORD initial[] = { 3,
        0x40000000, 0x3f800000, 0x40000000, 0x40400000,
        0x40000000, 0x40800000, 0x40a00000, 0x40c00000,
        0x41200000, 0x41100000, 0x41200000, 0x41300000 };
    ASSERT_EQ(52, controller.ReadKeysFrom(const_cast<CKDWORD *>(initial)));
    CKPositionKey replacement = MakeKey(2, 7, 8, 9);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    const CKDWORD expected[] = { 3,
        0x40000000, 0x40e00000, 0x41000000, 0x41100000,
        0x40000000, 0x40800000, 0x40a00000, 0x40c00000,
        0x41200000, 0x41100000, 0x41200000, 0x41300000 };
    DumpEquals(controller, expected, 13);
}

TEST_F(LinearPositionStorageTest, NullInsertionKeepsKeysAndConfiguredLength) {
    RCKLinearPositionController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    EXPECT_EQ(-1, controller.AddKey(nullptr));
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 9);
}

TEST_F(LinearPositionStorageTest, RemovingInteriorKeyChangesActualAnimationInterpolation) {
    RCKObjectAnimation animation(Context);
    RCKLinearPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    CKPositionKey middle = MakeKey(6, 100, 100, 100);
    ASSERT_EQ(1, controller->AddKey(&middle));
    VxVector before, after;
    ASSERT_TRUE(animation.EvaluatePosition(6, before));
    VectorEquals(before, 100, 100, 100);
    controller->RemoveKey(1);
    ASSERT_TRUE(animation.EvaluatePosition(6, after));
    VectorEquals(after, 5, 6, 7);
    SaveEquals(animation, PositionWire, 9);
}

TEST_F(LinearPositionStorageTest, RemovingFirstThenLastLeavesEmptyControllerReusable) {
    RCKLinearPositionController controller;
    Fill(controller);
    controller.SetLength(37);
    controller.RemoveKey(0);
    ASSERT_EQ(1, controller.GetKeyCount());
    VectorEquals(Key(controller, 0)->Pos, 9, 10, 11);
    controller.RemoveKey(0);
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    const CKDWORD empty[] = { 0 };
    DumpEquals(controller, empty, 1);
    CKPositionKey replacement = MakeKey(3, 4, 5, 6);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(3, &output));
    VectorEquals(output, 4, 5, 6);
}

TEST_F(LinearPositionStorageTest, OutOfRangeRemovalDoesNotChangeOwnedKeys) {
    RCKLinearPositionController controller;
    Fill(controller);
    CKKey *first = controller.GetKey(0);
    controller.RemoveKey(-1);
    controller.RemoveKey(2);
    controller.RemoveKey(INT_MAX);
    EXPECT_EQ(first, controller.GetKey(0));
    DumpEquals(controller, PositionWire, 9);
}

TEST_F(LinearPositionStorageTest, FailedInsertRetainsOldKeysLengthAndSampling) {
    RCKLinearPositionController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    CKPositionKey middle = MakeKey(6, 100, 100, 100);
    FailNextLinearArray = true;
    const int result = controller.AddKey(&middle);
    const bool allocationWasAttempted = !FailNextLinearArray;
    FailNextLinearArray = false;
    EXPECT_TRUE(allocationWasAttempted);
    EXPECT_EQ(-1, result);
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 9);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    VectorEquals(output, 5, 6, 7);
}

TEST_F(LinearPositionStorageTest, DuplicateReplacementRequiresNoArrayAllocation) {
    RCKLinearPositionController controller;
    Fill(controller);
    CKPositionKey replacement = MakeKey(2, 4, 5, 6);
    FailNextLinearArray = true;
    const int result = controller.AddKey(&replacement);
    const bool noAllocation = FailNextLinearArray;
    FailNextLinearArray = false;
    EXPECT_TRUE(noAllocation);
    ASSERT_EQ(0, result);
    EXPECT_EQ(2, controller.GetKeyCount());
    VectorEquals(Key(controller, 0)->Pos, 4, 5, 6);
}

TEST_F(LinearPositionStorageTest, FailedRemoveRetainsOldKeysLengthAndSampling) {
    RCKLinearPositionController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    FailNextLinearArray = true;
    controller.RemoveKey(0);
    const bool allocationWasAttempted = !FailNextLinearArray;
    FailNextLinearArray = false;
    EXPECT_TRUE(allocationWasAttempted);
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 9);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    VectorEquals(output, 5, 6, 7);
}

TEST_F(LinearPositionStorageTest, FailedReadRetainsOldKeysLengthAndSampling) {
    RCKLinearPositionController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const CKDWORD replacement[] = { 1, 0x40c00000, 0x42c80000, 0x42c80000, 0x42c80000 };
    FailNextLinearArray = true;
    const int result = controller.ReadKeysFrom(const_cast<CKDWORD *>(replacement));
    const bool allocationWasAttempted = !FailNextLinearArray;
    FailNextLinearArray = false;
    EXPECT_TRUE(allocationWasAttempted);
    EXPECT_EQ(0, result);
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 9);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    VectorEquals(output, 5, 6, 7);
}

TEST_F(LinearPositionStorageTest, DumpAndActualAnimationSaveMatchLiteralTwoKeyWire) {
    RCKObjectAnimation animation(Context);
    RCKLinearPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    DumpEquals(*controller, PositionWire, 9);
    SaveEquals(animation, PositionWire, 9);
}

TEST_F(LinearPositionStorageTest, EmptyDumpIncludesOnlyItsCountAndPreservesLength) {
    RCKLinearPositionController controller;
    controller.SetLength(37);
    const CKDWORD empty[] = { 0 };
    DumpEquals(controller, empty, 1);
    EXPECT_FLOAT_EQ(37, controller.GetLength());
}

TEST_F(LinearPositionStorageTest, DumpWritesExactlyItsReportedBytesIntoUnalignedStorage) {
    RCKLinearPositionController controller;
    Fill(controller);
    unsigned char bytes[sizeof(PositionWire) + 2];
    std::memset(bytes, 0xa5, sizeof(bytes));
    ASSERT_EQ(static_cast<int>(sizeof(PositionWire)), controller.DumpKeysTo(bytes + 1));
    EXPECT_EQ(0xa5, bytes[0]);
    EXPECT_EQ(0xa5, bytes[sizeof(bytes) - 1]);
    EXPECT_EQ(0, std::memcmp(bytes + 1, PositionWire, sizeof(PositionWire)));
}

TEST_F(LinearPositionStorageTest, RawReadOwnsLiteralPayloadAndRetainsControllerLength) {
    RCKLinearPositionController controller;
    controller.SetLength(37);
    {
        CKDWORD source[9];
        std::memcpy(source, PositionWire, sizeof(source));
        ASSERT_EQ(36, controller.ReadKeysFrom(source));
        std::memset(source, 0, sizeof(source));
    }
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 9);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    VectorEquals(output, 5, 6, 7);
}

TEST_F(LinearPositionStorageTest, RawReadAcceptsUnalignedPayloadAndIgnoresFollowingBytes) {
    RCKLinearPositionController controller;
    unsigned char source[sizeof(PositionWire) + 5];
    std::memset(source, 0x5a, sizeof(source));
    std::memcpy(source + 1, PositionWire, sizeof(PositionWire));
    ASSERT_EQ(36, controller.ReadKeysFrom(source + 1));
    DumpEquals(controller, PositionWire, 9);
    EXPECT_EQ(0x5a, source[sizeof(PositionWire) + 1]);
}

TEST_F(LinearPositionStorageTest, RawReadEmptyReleasesPreviousKeysWithoutChangingLength) {
    RCKLinearPositionController controller;
    Fill(controller);
    controller.SetLength(37);
    const CKDWORD empty[] = { 0, 0xdeadbeef };
    ASSERT_EQ(4, controller.ReadKeysFrom(const_cast<CKDWORD *>(empty)));
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    VxVector output(9, 8, 7);
    EXPECT_FALSE(controller.Evaluate(2, &output));
    VectorEquals(output, 9, 8, 7);
    DumpEquals(controller, empty, 1);
}

TEST_F(LinearPositionStorageTest, RawReadRejectsInvalidCountsAndTimesWithoutReplacingKeys) {
    RCKLinearPositionController controller;
    Fill(controller);
    CKKey *first = controller.GetKey(0);
    EXPECT_EQ(0, controller.ReadKeysFrom(nullptr));
    const CKDWORD negative[] = { 0xffffffff };
    const CKDWORD overflow[] = { 0x7fffffff };
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(negative)));
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(overflow)));
    CKDWORD invalid[9];
    std::memcpy(invalid, PositionWire, sizeof(invalid));
    invalid[5] = 0x3f800000; // Time 1 follows time 2.
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[5] = 0x7fc00001;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[5] = 0x7f800000;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    EXPECT_EQ(first, controller.GetKey(0));
    DumpEquals(controller, PositionWire, 9);
}

TEST_F(LinearPositionStorageTest, PositionPayloadBitsArePreservedWithoutTreatingThemAsTime) {
    RCKLinearPositionController controller;
    const CKDWORD wire[] = { 1, 0x40000000, 0x80000000, 0x7fc12345, 0x7f800000 };
    ASSERT_EQ(20, controller.ReadKeysFrom(const_cast<CKDWORD *>(wire)));
    DumpEquals(controller, wire, 5);
}

TEST_F(LinearPositionStorageTest, InvalidDumpTimeFailsBeforeWritingAndAnimationSavePropagatesFailure) {
    RCKObjectAnimation animation(Context);
    RCKLinearPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    CKDWORD output[9];
    for (int i = 0; i < 9; ++i) output[i] = 0xa5a5a5a5;
    Key(*controller, 1)->TimeStep = 1;
    EXPECT_EQ(0, controller->DumpKeysTo(nullptr));
    EXPECT_EQ(0, controller->DumpKeysTo(output));
    CKStateChunk *saved = animation.Save(nullptr, CK_STATESAVE_OBJANIMALL);
    EXPECT_EQ(nullptr, saved);
    if (saved) DeleteCKStateChunk(saved);
    for (int i = 0; i < 9; ++i) EXPECT_EQ(0xa5a5a5a5u, output[i]);
    const CKDWORD invalidTime = 0x7fc00001;
    std::memcpy(&Key(*controller, 1)->TimeStep, &invalidTime, sizeof(invalidTime));
    EXPECT_EQ(0, controller->DumpKeysTo(output));
    for (int i = 0; i < 9; ++i) EXPECT_EQ(0xa5a5a5a5u, output[i]);
}

TEST_F(LinearPositionStorageTest, BoundedAnimationLoadReadsLiteralPositionForSamplingAndSave) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WritePosition(chunk, PositionWire, 9);
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetPositionController());
    VxVector output;
    ASSERT_TRUE(animation.EvaluatePosition(6, output));
    VectorEquals(output, 5, 6, 7);
    SaveEquals(animation, PositionWire, 9);
}

TEST_F(LinearPositionStorageTest, BoundedAnimationLoadRejectsTruncationBeforeNextIdentifier) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WritePosition(chunk, PositionWire, 5); // Two keys declared, one key available.
    chunk.WriteDword(0);
    chunk.WriteIdentifier(0x76543210);
    for (int i = 0; i < 32; ++i) chunk.WriteDword(0x42424242);
    chunk.CloseChunk();
    chunk.StartRead();
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetPositionController());
    chunk.StartRead();
    ASSERT_TRUE(chunk.SeekIdentifier(0x76543210));
    EXPECT_EQ(0x42424242u, chunk.ReadDword());
}

TEST_F(LinearPositionStorageTest, BoundedAnimationLoadRejectsAnAbsentCountBeforeTheNextRecord) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WritePosition(chunk, nullptr, 0);
    EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetPositionController());
}

TEST_F(LinearPositionStorageTest, BoundedAnimationLoadSkipsDeclaredPaddingBeforeRotationRecord) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    CKDWORD padded[11];
    std::memcpy(padded, PositionWire, sizeof(PositionWire));
    padded[9] = 0xdeadbeef;
    padded[10] = 0xbaadf00d;
    WritePosition(chunk, padded, 11);
    const CKDWORD rotation[] = { 1, 0x40000000, 0, 0, 0, 0x3f800000 };
    chunk.WriteDword(CKANIMATION_LINROT_CONTROL);
    chunk.WriteDword(6);
    chunk.WriteBufferNoSize_LEndian(sizeof(rotation), const_cast<CKDWORD *>(rotation));
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetPositionController());
    ASSERT_NE(nullptr, animation.GetRotationController());
    VxVector position;
    VxQuaternion orientation;
    ASSERT_TRUE(animation.EvaluatePosition(6, position));
    ASSERT_TRUE(animation.EvaluateRotation(2, orientation));
    VectorEquals(position, 5, 6, 7);
    EXPECT_FLOAT_EQ(0, orientation.x);
    EXPECT_FLOAT_EQ(0, orientation.y);
    EXPECT_FLOAT_EQ(0, orientation.z);
    EXPECT_FLOAT_EQ(1, orientation.w);
}

TEST_F(LinearPositionStorageTest, BoundedAnimationLoadRejectsUnsortedTimeRecord) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    CKDWORD unsorted[9];
    std::memcpy(unsorted, PositionWire, sizeof(unsorted));
    unsorted[5] = 0x3f800000;
    BeginAnimation(chunk);
    WritePosition(chunk, unsorted, 9);
    EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetPositionController());
}

} // namespace
