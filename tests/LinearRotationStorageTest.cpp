#include <gtest/gtest.h>
#include <climits>
#include <cmath>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKObjectAnimation.h"
#include "RCKKeyframeData.h"

extern void SetProcessorSpecific_FunctionsPtr();

// Fail the next array allocation made by the statically linked controller.
static thread_local bool FailNextRotationArray = false;

void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextRotationArray) {
        FailNextRotationArray = false;
        return nullptr;
    }
    try {
        return ::operator new[](bytes);
    } catch (...) {
        return nullptr;
    }
}

namespace {

// Virtools 2.1 wire: count DWORD, then five DWORDs per key: time, XYZW.
// These literals are independent of the implementation's dump and save paths.
const CKDWORD RotationWire[] = {
    2,
    0x40000000, 0, 0, 0, 0x3f800000,
    0x41200000, 0, 0, 0x3f800000, 0
};

CKRotationKey MakeKey(float time, float x, float y, float z, float w) {
    CKRotationKey key;
    key.TimeStep = time;
    key.Rot = VxQuaternion(x, y, z, w);
    return key;
}

CKRotationKey *Key(RCKLinearRotationController &controller, int index) {
    return static_cast<CKRotationKey *>(controller.GetKey(index));
}

void QuaternionEquals(const VxQuaternion &q, float x, float y, float z, float w) {
    EXPECT_FLOAT_EQ(x, q.x);
    EXPECT_FLOAT_EQ(y, q.y);
    EXPECT_FLOAT_EQ(z, q.z);
    EXPECT_FLOAT_EQ(w, q.w);
}

void QuarterTurnEquals(const VxQuaternion &q) {
    EXPECT_NEAR(0, q.x, 1e-6);
    EXPECT_NEAR(0, q.y, 1e-6);
    EXPECT_NEAR(0.7071067811865475, q.z, 1e-6);
    EXPECT_NEAR(0.7071067811865475, q.w, 1e-6);
}

class LinearRotationStorageTest : public ::testing::Test {
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
        FailNextRotationArray = false;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static RCKLinearRotationController *Controller(RCKObjectAnimation &animation) {
        return static_cast<RCKLinearRotationController *>(animation.CreateController(CKANIMATION_LINROT_CONTROL));
    }

    static void Fill(RCKLinearRotationController &controller) {
        CKRotationKey first = MakeKey(2, 0, 0, 0, 1);
        CKRotationKey last = MakeKey(10, 0, 0, 1, 0);
        ASSERT_EQ(0, controller.AddKey(&first));
        ASSERT_EQ(1, controller.AddKey(&last));
    }

    static void DumpEquals(RCKLinearRotationController &controller, const CKDWORD *expected, int words) {
        ASSERT_LT(words, 64);
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
            saved->Skip(10);
            EXPECT_EQ(CKANIMATION_LINROT_CONTROL, saved->ReadDword());
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

    static void WriteRotation(CKStateChunk &chunk, const CKDWORD *words, int count) {
        chunk.WriteDword(CKANIMATION_LINROT_CONTROL);
        chunk.WriteDword(count);
        if (count) chunk.WriteBufferNoSize_LEndian(count * 4, const_cast<CKDWORD *>(words));
    }

    static void EndAnimation(CKStateChunk &chunk) {
        chunk.WriteDword(0);
        chunk.CloseChunk();
        chunk.StartRead();
    }
};

TEST_F(LinearRotationStorageTest, InsertsBeforeBetweenAndAfterWithoutNormalizingQuaternions) {
    RCKLinearRotationController controller;
    CKRotationKey last = MakeKey(10, 0, 0, 1, 0);
    CKRotationKey first = MakeKey(2, 0, 0, 0, 1);
    CKRotationKey middle = MakeKey(6, 1, 2, 3, 4);
    ASSERT_EQ(0, controller.AddKey(&last));
    ASSERT_EQ(0, controller.AddKey(&first));
    ASSERT_EQ(1, controller.AddKey(&middle));
    const CKDWORD expected[] = { 3,
        0x40000000, 0, 0, 0, 0x3f800000,
        0x40c00000, 0x3f800000, 0x40000000, 0x40400000, 0x40800000,
        0x41200000, 0, 0, 0x3f800000, 0 };
    DumpEquals(controller, expected, 16);
}

TEST_F(LinearRotationStorageTest, InsertOwnsCallerKeyAfterMutationAndDestruction) {
    RCKObjectAnimation animation(Context);
    RCKLinearRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    {
        CKRotationKey source = MakeKey(2, 0, 0, 0, -1);
        ASSERT_EQ(0, controller->AddKey(&source));
        EXPECT_NE(&source, controller->GetKey(0));
        source = MakeKey(99, 1, 2, 3, 4);
    }
    const CKDWORD expected[] = { 1, 0x40000000, 0, 0, 0, 0xbf800000 };
    SaveEquals(animation, expected, 6);
    VxQuaternion output;
    ASSERT_TRUE(animation.EvaluateRotation(2, output));
    QuaternionEquals(output, 0, 0, 0, -1);
}

TEST_F(LinearRotationStorageTest, DuplicateSignedZeroRetainsTimeBitsAndQuaternionSign) {
    RCKLinearRotationController controller;
    const CKDWORD original[] = { 1, 0x80000000, 0, 0, 0, 0x3f800000 };
    ASSERT_EQ(24, controller.ReadKeysFrom(const_cast<CKDWORD *>(original)));
    CKKey *first = controller.GetKey(0);
    CKRotationKey replacement = MakeKey(0, 0, 0, 0, -1);
    FailNextRotationArray = true;
    ASSERT_EQ(0, controller.AddKey(&replacement));
    EXPECT_TRUE(FailNextRotationArray);
    FailNextRotationArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    const CKDWORD expected[] = { 1, 0x80000000, 0, 0, 0, 0xbf800000 };
    DumpEquals(controller, expected, 6);
}

TEST_F(LinearRotationStorageTest, DuplicateLoadedTimesUpdateOnlyFirstEqualRecord) {
    RCKLinearRotationController controller;
    const CKDWORD original[] = { 3,
        0x40000000, 0, 0, 0, 0x3f800000,
        0x40000000, 0, 0, 0, 0xbf800000,
        0x41200000, 0, 0, 0x3f800000, 0 };
    ASSERT_EQ(64, controller.ReadKeysFrom(const_cast<CKDWORD *>(original)));
    CKRotationKey replacement = MakeKey(2, 1, 0, 0, 0);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    const CKDWORD expected[] = { 3,
        0x40000000, 0x3f800000, 0, 0, 0,
        0x40000000, 0, 0, 0, 0xbf800000,
        0x41200000, 0, 0, 0x3f800000, 0 };
    DumpEquals(controller, expected, 16);
}

TEST_F(LinearRotationStorageTest, AddingOwnedKeyAgainKeepsRecordAndCount) {
    RCKLinearRotationController controller;
    Fill(controller);
    CKKey *owned = controller.GetKey(1);
    ASSERT_EQ(1, controller.AddKey(owned));
    EXPECT_EQ(owned, controller.GetKey(1));
    DumpEquals(controller, RotationWire, 11);
}

TEST_F(LinearRotationStorageTest, NullInsertAndInvalidRemovalKeepOwnedState) {
    RCKLinearRotationController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    EXPECT_EQ(-1, controller.AddKey(nullptr));
    controller.RemoveKey(-1);
    controller.RemoveKey(2);
    controller.RemoveKey(INT_MAX);
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 11);
}

TEST_F(LinearRotationStorageTest, RemovingInteriorKeyChangesActualAnimationSampling) {
    RCKObjectAnimation animation(Context);
    RCKLinearRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    CKRotationKey middle = MakeKey(6, 0, 0, 0, -1);
    ASSERT_EQ(1, controller->AddKey(&middle));
    VxQuaternion before, after;
    ASSERT_TRUE(animation.EvaluateRotation(6, before));
    EXPECT_FLOAT_EQ(0, before.z);
    EXPECT_FLOAT_EQ(1, std::fabs(before.w));
    controller->RemoveKey(1);
    ASSERT_TRUE(animation.EvaluateRotation(6, after));
    QuarterTurnEquals(after);
    SaveEquals(animation, RotationWire, 11);
}

TEST_F(LinearRotationStorageTest, RemovingFirstThenLastLeavesReusableEmptyController) {
    RCKLinearRotationController controller;
    Fill(controller);
    controller.SetLength(37);
    controller.RemoveKey(0);
    ASSERT_EQ(1, controller.GetKeyCount());
    QuaternionEquals(Key(controller, 0)->Rot, 0, 0, 1, 0);
    FailNextRotationArray = true;
    controller.RemoveKey(0);
    EXPECT_TRUE(FailNextRotationArray);
    FailNextRotationArray = false;
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    const CKDWORD empty[] = { 0 };
    DumpEquals(controller, empty, 1);
    CKRotationKey replacement = MakeKey(3, 0, 0, 0, -1);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(3, &output));
    QuaternionEquals(output, 0, 0, 0, -1);
}

TEST_F(LinearRotationStorageTest, FailedInsertRetainsKeysLengthAndSampling) {
    RCKLinearRotationController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    CKRotationKey middle = MakeKey(6, 1, 0, 0, 0);
    FailNextRotationArray = true;
    EXPECT_EQ(-1, controller.AddKey(&middle));
    EXPECT_FALSE(FailNextRotationArray);
    FailNextRotationArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearRotationStorageTest, FailedRemoveRetainsKeysLengthAndSampling) {
    RCKLinearRotationController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    FailNextRotationArray = true;
    controller.RemoveKey(0);
    EXPECT_FALSE(FailNextRotationArray);
    FailNextRotationArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearRotationStorageTest, FailedReadRetainsKeysLengthAndSampling) {
    RCKLinearRotationController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const CKDWORD replacement[] = { 1, 0x40c00000, 0x3f800000, 0, 0, 0 };
    FailNextRotationArray = true;
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(replacement)));
    EXPECT_FALSE(FailNextRotationArray);
    FailNextRotationArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearRotationStorageTest, DumpAndActualAnimationSaveMatchLiteralTwentyByteKeys) {
    RCKObjectAnimation animation(Context);
    RCKLinearRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    DumpEquals(*controller, RotationWire, 11);
    SaveEquals(animation, RotationWire, 11);
}

TEST_F(LinearRotationStorageTest, EmptyDumpHasOnlyCountAndRetainsLength) {
    RCKLinearRotationController controller;
    controller.SetLength(37);
    const CKDWORD empty[] = { 0 };
    DumpEquals(controller, empty, 1);
    EXPECT_FLOAT_EQ(37, controller.GetLength());
}

TEST_F(LinearRotationStorageTest, UnalignedDumpWritesExactlyItsReportedBytes) {
    RCKLinearRotationController controller;
    Fill(controller);
    unsigned char bytes[sizeof(RotationWire) + 2];
    std::memset(bytes, 0xa5, sizeof(bytes));
    ASSERT_EQ(static_cast<int>(sizeof(RotationWire)), controller.DumpKeysTo(bytes + 1));
    EXPECT_EQ(0xa5, bytes[0]);
    EXPECT_EQ(0xa5, bytes[sizeof(bytes) - 1]);
    EXPECT_EQ(0, std::memcmp(bytes + 1, RotationWire, sizeof(RotationWire)));
}

TEST_F(LinearRotationStorageTest, RawReadOwnsLiteralKeysAndRetainsLength) {
    RCKLinearRotationController controller;
    controller.SetLength(37);
    {
        CKDWORD source[11];
        std::memcpy(source, RotationWire, sizeof(source));
        ASSERT_EQ(44, controller.ReadKeysFrom(source));
        std::memset(source, 0, sizeof(source));
    }
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearRotationStorageTest, RawReadAllowsUnalignedKeysAndLeavesFollowingBytes) {
    RCKLinearRotationController controller;
    unsigned char source[sizeof(RotationWire) + 5];
    std::memset(source, 0x5a, sizeof(source));
    std::memcpy(source + 1, RotationWire, sizeof(RotationWire));
    ASSERT_EQ(44, controller.ReadKeysFrom(source + 1));
    DumpEquals(controller, RotationWire, 11);
    EXPECT_EQ(0x5a, source[sizeof(RotationWire) + 1]);
}

TEST_F(LinearRotationStorageTest, ReadingEmptyClearsOwnedKeysWithoutChangingLength) {
    RCKLinearRotationController controller;
    Fill(controller);
    controller.SetLength(37);
    const CKDWORD empty[] = { 0, 0xdeadbeef };
    ASSERT_EQ(4, controller.ReadKeysFrom(const_cast<CKDWORD *>(empty)));
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    VxQuaternion output(1, 2, 3, 4);
    EXPECT_FALSE(controller.Evaluate(2, &output));
    QuaternionEquals(output, 1, 2, 3, 4);
    DumpEquals(controller, empty, 1);
}

TEST_F(LinearRotationStorageTest, InvalidCountOrTimeRetainsPreviousKeys) {
    RCKLinearRotationController controller;
    Fill(controller);
    CKKey *first = controller.GetKey(0);
    EXPECT_EQ(0, controller.ReadKeysFrom(nullptr));
    const CKDWORD negative[] = { 0xffffffff };
    const CKDWORD overflow[] = { 0x7fffffff };
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(negative)));
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(overflow)));
    CKDWORD invalid[11];
    std::memcpy(invalid, RotationWire, sizeof(invalid));
    invalid[6] = 0x3f800000;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[6] = 0x7fc00001;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[6] = 0x7f800000;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    EXPECT_EQ(first, controller.GetKey(0));
    DumpEquals(controller, RotationWire, 11);
}

TEST_F(LinearRotationStorageTest, QuaternionPayloadBitsAreNotTreatedAsTimeOrNormalized) {
    RCKObjectAnimation animation(Context);
    RCKLinearRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    const CKDWORD wire[] = { 1, 0x40000000, 0x80000000, 0x7fc12345, 0x7f800000, 0xbf800000 };
    ASSERT_EQ(24, controller->ReadKeysFrom(const_cast<CKDWORD *>(wire)));
    DumpEquals(*controller, wire, 6);
    SaveEquals(animation, wire, 6);
}

TEST_F(LinearRotationStorageTest, InvalidDumpTimeWritesNothingAndSavePropagatesFailure) {
    RCKObjectAnimation animation(Context);
    RCKLinearRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    CKDWORD output[11];
    for (int i = 0; i < 11; ++i) output[i] = 0xa5a5a5a5;
    Key(*controller, 1)->TimeStep = 1;
    EXPECT_EQ(0, controller->DumpKeysTo(nullptr));
    EXPECT_EQ(0, controller->DumpKeysTo(output));
    CKStateChunk *saved = animation.Save(nullptr, CK_STATESAVE_OBJANIMALL);
    EXPECT_EQ(nullptr, saved);
    if (saved) DeleteCKStateChunk(saved);
    const CKDWORD invalidTime = 0x7fc00001;
    std::memcpy(&Key(*controller, 1)->TimeStep, &invalidTime, sizeof(invalidTime));
    EXPECT_EQ(0, controller->DumpKeysTo(output));
    for (int i = 0; i < 11; ++i) EXPECT_EQ(0xa5a5a5a5u, output[i]);
}

TEST_F(LinearRotationStorageTest, BoundedAnimationLoadSamplesAndSavesLiteralRotation) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteRotation(chunk, RotationWire, 11);
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetRotationController());
    VxQuaternion output;
    ASSERT_TRUE(animation.EvaluateRotation(6, output));
    QuarterTurnEquals(output);
    SaveEquals(animation, RotationWire, 11);
}

TEST_F(LinearRotationStorageTest, BoundedAnimationLoadRetainsNegativeQuaternionSign) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    const CKDWORD negative[] = { 1, 0x40000000, 0x80000000, 0, 0, 0xbf800000 };
    BeginAnimation(chunk);
    WriteRotation(chunk, negative, 6);
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    VxQuaternion output;
    ASSERT_TRUE(animation.EvaluateRotation(2, output));
    QuaternionEquals(output, 0, 0, 0, -1);
    SaveEquals(animation, negative, 6);
}

TEST_F(LinearRotationStorageTest, BoundedAnimationLoadCannotReadTruncatedKeyFromNextIdentifier) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteRotation(chunk, RotationWire, 6);
    chunk.WriteDword(0);
    chunk.WriteIdentifier(0x76543210);
    for (int i = 0; i < 32; ++i) chunk.WriteDword(0x42424242);
    chunk.CloseChunk();
    chunk.StartRead();
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetRotationController());
    chunk.StartRead();
    ASSERT_TRUE(chunk.SeekIdentifier(0x76543210));
    EXPECT_EQ(0x42424242u, chunk.ReadDword());
}

TEST_F(LinearRotationStorageTest, BoundedAnimationLoadRejectsAbsentCount) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteRotation(chunk, nullptr, 0);
    EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetRotationController());
}

TEST_F(LinearRotationStorageTest, BoundedAnimationLoadSkipsPaddingBeforeFollowingScaleRecord) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    CKDWORD padded[13];
    std::memcpy(padded, RotationWire, sizeof(RotationWire));
    padded[11] = 0xdeadbeef;
    padded[12] = 0xbaadf00d;
    WriteRotation(chunk, padded, 13);
    const CKDWORD scale[] = { 1, 0x40000000, 0x40000000, 0x40400000, 0x40800000 };
    chunk.WriteDword(CKANIMATION_LINSCL_CONTROL);
    chunk.WriteDword(5);
    chunk.WriteBufferNoSize_LEndian(sizeof(scale), const_cast<CKDWORD *>(scale));
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetRotationController());
    ASSERT_NE(nullptr, animation.GetScaleController());
    VxQuaternion rotation;
    VxVector scaling;
    ASSERT_TRUE(animation.EvaluateRotation(6, rotation));
    ASSERT_TRUE(animation.EvaluateScale(2, scaling));
    QuarterTurnEquals(rotation);
    EXPECT_FLOAT_EQ(2, scaling.x);
    EXPECT_FLOAT_EQ(3, scaling.y);
    EXPECT_FLOAT_EQ(4, scaling.z);
}

TEST_F(LinearRotationStorageTest, BoundedAnimationLoadRejectsUnsortedTimes) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    CKDWORD unsorted[11];
    std::memcpy(unsorted, RotationWire, sizeof(unsorted));
    unsorted[6] = 0x3f800000;
    BeginAnimation(chunk);
    WriteRotation(chunk, unsorted, 11);
    EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetRotationController());
}

} // namespace
