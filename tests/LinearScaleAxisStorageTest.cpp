#include <gtest/gtest.h>
#include <climits>
#include <cmath>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKObjectAnimation.h"
#include "RCKKeyframeData.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

// Fail the next array allocation made by the statically linked controller.
static thread_local bool FailNextScaleAxisArray = false;

void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextScaleAxisArray) {
        FailNextScaleAxisArray = false;
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
const CKDWORD ScaleAxisWire[] = {
    2,
    0x40000000, 0, 0, 0, 0x3f800000,
    0x41200000, 0, 0, 0x3f800000, 0
};

CKScaleAxisKey MakeKey(float time, float x, float y, float z, float w) {
    CKScaleAxisKey key;
    key.TimeStep = time;
    key.Rot = VxQuaternion(x, y, z, w);
    return key;
}

CKScaleAxisKey *Key(RCKLinearScaleAxisController &controller, int index) {
    return static_cast<CKScaleAxisKey *>(controller.GetKey(index));
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

class LinearScaleAxisStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        FailNextScaleAxisArray = false;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static RCKLinearScaleAxisController *Controller(RCKObjectAnimation &animation) {
        return static_cast<RCKLinearScaleAxisController *>(animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL));
    }

    static void Fill(RCKLinearScaleAxisController &controller) {
        CKScaleAxisKey first = MakeKey(2, 0, 0, 0, 1);
        CKScaleAxisKey last = MakeKey(10, 0, 0, 1, 0);
        ASSERT_EQ(0, controller.AddKey(&first));
        ASSERT_EQ(1, controller.AddKey(&last));
    }

    static void DumpEquals(RCKLinearScaleAxisController &controller, const CKDWORD *expected, int words) {
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
            EXPECT_EQ(CKANIMATION_LINSCLAXIS_CONTROL, saved->ReadDword());
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

    static void WriteScaleAxis(CKStateChunk &chunk, const CKDWORD *words, int count) {
        chunk.WriteDword(CKANIMATION_LINSCLAXIS_CONTROL);
        chunk.WriteDword(count);
        if (count) chunk.WriteBufferNoSize_LEndian(count * 4, const_cast<CKDWORD *>(words));
    }

    static void EndAnimation(CKStateChunk &chunk) {
        chunk.WriteDword(0);
        chunk.CloseChunk();
        chunk.StartRead();
    }
};

TEST_F(LinearScaleAxisStorageTest, InsertsBeforeBetweenAndAfterWithoutNormalizingQuaternions) {
    RCKLinearScaleAxisController controller;
    CKScaleAxisKey last = MakeKey(10, 0, 0, 1, 0);
    CKScaleAxisKey first = MakeKey(2, 0, 0, 0, 1);
    CKScaleAxisKey middle = MakeKey(6, 1, 2, 3, 4);
    ASSERT_EQ(0, controller.AddKey(&last));
    ASSERT_EQ(0, controller.AddKey(&first));
    ASSERT_EQ(1, controller.AddKey(&middle));
    const CKDWORD expected[] = { 3,
        0x40000000, 0, 0, 0, 0x3f800000,
        0x40c00000, 0x3f800000, 0x40000000, 0x40400000, 0x40800000,
        0x41200000, 0, 0, 0x3f800000, 0 };
    DumpEquals(controller, expected, 16);
}

TEST_F(LinearScaleAxisStorageTest, InsertOwnsCallerKeyAfterMutationAndDestruction) {
    RCKObjectAnimation animation(Context);
    RCKLinearScaleAxisController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    {
        CKScaleAxisKey source = MakeKey(2, 0, 0, 0, -1);
        ASSERT_EQ(0, controller->AddKey(&source));
        EXPECT_NE(&source, controller->GetKey(0));
        source = MakeKey(99, 1, 2, 3, 4);
    }
    const CKDWORD expected[] = { 1, 0x40000000, 0, 0, 0, 0xbf800000 };
    SaveEquals(animation, expected, 6);
    VxQuaternion output;
    ASSERT_TRUE(animation.EvaluateScaleAxis(2, output));
    QuaternionEquals(output, 0, 0, 0, -1);
}

TEST_F(LinearScaleAxisStorageTest, DuplicateSignedZeroRetainsTimeBitsAndQuaternionSign) {
    RCKLinearScaleAxisController controller;
    const CKDWORD original[] = { 1, 0x80000000, 0, 0, 0, 0x3f800000 };
    ASSERT_EQ(24, controller.ReadKeysFrom(const_cast<CKDWORD *>(original)));
    CKKey *first = controller.GetKey(0);
    CKScaleAxisKey replacement = MakeKey(0, 0, 0, 0, -1);
    FailNextScaleAxisArray = true;
    ASSERT_EQ(0, controller.AddKey(&replacement));
    EXPECT_TRUE(FailNextScaleAxisArray);
    FailNextScaleAxisArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    const CKDWORD expected[] = { 1, 0x80000000, 0, 0, 0, 0xbf800000 };
    DumpEquals(controller, expected, 6);
}

TEST_F(LinearScaleAxisStorageTest, DuplicateLoadedTimesUpdateOnlyFirstEqualRecord) {
    RCKLinearScaleAxisController controller;
    const CKDWORD original[] = { 3,
        0x40000000, 0, 0, 0, 0x3f800000,
        0x40000000, 0, 0, 0, 0xbf800000,
        0x41200000, 0, 0, 0x3f800000, 0 };
    ASSERT_EQ(64, controller.ReadKeysFrom(const_cast<CKDWORD *>(original)));
    CKScaleAxisKey replacement = MakeKey(2, 1, 0, 0, 0);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    const CKDWORD expected[] = { 3,
        0x40000000, 0x3f800000, 0, 0, 0,
        0x40000000, 0, 0, 0, 0xbf800000,
        0x41200000, 0, 0, 0x3f800000, 0 };
    DumpEquals(controller, expected, 16);
}

TEST_F(LinearScaleAxisStorageTest, AddingOwnedKeyAgainKeepsRecordAndCount) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    CKKey *owned = controller.GetKey(1);
    ASSERT_EQ(1, controller.AddKey(owned));
    EXPECT_EQ(owned, controller.GetKey(1));
    DumpEquals(controller, ScaleAxisWire, 11);
}

TEST_F(LinearScaleAxisStorageTest, NullInsertAndInvalidRemovalKeepOwnedState) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    EXPECT_EQ(-1, controller.AddKey(nullptr));
    controller.RemoveKey(-1);
    controller.RemoveKey(2);
    controller.RemoveKey(INT_MAX);
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, ScaleAxisWire, 11);
}

TEST_F(LinearScaleAxisStorageTest, RemovingInteriorKeyChangesActualAnimationSampling) {
    RCKObjectAnimation animation(Context);
    RCKLinearScaleAxisController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    CKScaleAxisKey middle = MakeKey(6, 0, 0, 0, -1);
    ASSERT_EQ(1, controller->AddKey(&middle));
    VxQuaternion before, after;
    ASSERT_TRUE(animation.EvaluateScaleAxis(6, before));
    EXPECT_FLOAT_EQ(0, before.z);
    EXPECT_FLOAT_EQ(1, std::fabs(before.w));
    controller->RemoveKey(1);
    ASSERT_TRUE(animation.EvaluateScaleAxis(6, after));
    QuarterTurnEquals(after);
    SaveEquals(animation, ScaleAxisWire, 11);
}

TEST_F(LinearScaleAxisStorageTest, RemovingFirstThenLastLeavesReusableEmptyController) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    controller.SetLength(37);
    controller.RemoveKey(0);
    ASSERT_EQ(1, controller.GetKeyCount());
    QuaternionEquals(Key(controller, 0)->Rot, 0, 0, 1, 0);
    FailNextScaleAxisArray = true;
    controller.RemoveKey(0);
    EXPECT_TRUE(FailNextScaleAxisArray);
    FailNextScaleAxisArray = false;
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    const CKDWORD empty[] = { 0 };
    DumpEquals(controller, empty, 1);
    CKScaleAxisKey replacement = MakeKey(3, 0, 0, 0, -1);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(3, &output));
    QuaternionEquals(output, 0, 0, 0, -1);
}

TEST_F(LinearScaleAxisStorageTest, FailedInsertRetainsKeysLengthAndSampling) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    CKScaleAxisKey middle = MakeKey(6, 1, 0, 0, 0);
    FailNextScaleAxisArray = true;
    EXPECT_EQ(-1, controller.AddKey(&middle));
    EXPECT_FALSE(FailNextScaleAxisArray);
    FailNextScaleAxisArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, ScaleAxisWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearScaleAxisStorageTest, FailedRemoveRetainsKeysLengthAndSampling) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    FailNextScaleAxisArray = true;
    controller.RemoveKey(0);
    EXPECT_FALSE(FailNextScaleAxisArray);
    FailNextScaleAxisArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, ScaleAxisWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearScaleAxisStorageTest, FailedReadRetainsKeysLengthAndSampling) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const CKDWORD replacement[] = { 1, 0x40c00000, 0x3f800000, 0, 0, 0 };
    FailNextScaleAxisArray = true;
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(replacement)));
    EXPECT_FALSE(FailNextScaleAxisArray);
    FailNextScaleAxisArray = false;
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, ScaleAxisWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearScaleAxisStorageTest, DumpAndActualAnimationSaveMatchLiteralTwentyByteKeys) {
    RCKObjectAnimation animation(Context);
    RCKLinearScaleAxisController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    Fill(*controller);
    DumpEquals(*controller, ScaleAxisWire, 11);
    SaveEquals(animation, ScaleAxisWire, 11);
}

TEST_F(LinearScaleAxisStorageTest, EmptyDumpHasOnlyCountAndRetainsLength) {
    RCKLinearScaleAxisController controller;
    controller.SetLength(37);
    const CKDWORD empty[] = { 0 };
    DumpEquals(controller, empty, 1);
    EXPECT_FLOAT_EQ(37, controller.GetLength());
}

TEST_F(LinearScaleAxisStorageTest, UnalignedDumpWritesExactlyItsReportedBytes) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    unsigned char bytes[sizeof(ScaleAxisWire) + 2];
    std::memset(bytes, 0xa5, sizeof(bytes));
    ASSERT_EQ(static_cast<int>(sizeof(ScaleAxisWire)), controller.DumpKeysTo(bytes + 1));
    EXPECT_EQ(0xa5, bytes[0]);
    EXPECT_EQ(0xa5, bytes[sizeof(bytes) - 1]);
    EXPECT_EQ(0, std::memcmp(bytes + 1, ScaleAxisWire, sizeof(ScaleAxisWire)));
}

TEST_F(LinearScaleAxisStorageTest, RawReadOwnsLiteralKeysAndRetainsLength) {
    RCKLinearScaleAxisController controller;
    controller.SetLength(37);
    {
        CKDWORD source[11];
        std::memcpy(source, ScaleAxisWire, sizeof(source));
        ASSERT_EQ(44, controller.ReadKeysFrom(source));
        std::memset(source, 0, sizeof(source));
    }
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, ScaleAxisWire, 11);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuarterTurnEquals(output);
}

TEST_F(LinearScaleAxisStorageTest, RawReadAllowsUnalignedKeysAndLeavesFollowingBytes) {
    RCKLinearScaleAxisController controller;
    unsigned char source[sizeof(ScaleAxisWire) + 5];
    std::memset(source, 0x5a, sizeof(source));
    std::memcpy(source + 1, ScaleAxisWire, sizeof(ScaleAxisWire));
    ASSERT_EQ(44, controller.ReadKeysFrom(source + 1));
    DumpEquals(controller, ScaleAxisWire, 11);
    EXPECT_EQ(0x5a, source[sizeof(ScaleAxisWire) + 1]);
}

TEST_F(LinearScaleAxisStorageTest, ReadingEmptyClearsOwnedKeysWithoutChangingLength) {
    RCKLinearScaleAxisController controller;
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

TEST_F(LinearScaleAxisStorageTest, InvalidCountOrTimeRetainsPreviousKeys) {
    RCKLinearScaleAxisController controller;
    Fill(controller);
    CKKey *first = controller.GetKey(0);
    EXPECT_EQ(0, controller.ReadKeysFrom(nullptr));
    const CKDWORD negative[] = { 0xffffffff };
    const CKDWORD overflow[] = { 0x7fffffff };
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(negative)));
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(overflow)));
    CKDWORD invalid[11];
    std::memcpy(invalid, ScaleAxisWire, sizeof(invalid));
    invalid[6] = 0x3f800000;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[6] = 0x7fc00001;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[6] = 0x7f800000;
    EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    EXPECT_EQ(first, controller.GetKey(0));
    DumpEquals(controller, ScaleAxisWire, 11);
}

TEST_F(LinearScaleAxisStorageTest, QuaternionPayloadBitsAreNotTreatedAsTimeOrNormalized) {
    RCKObjectAnimation animation(Context);
    RCKLinearScaleAxisController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    const CKDWORD wire[] = { 1, 0x40000000, 0x80000000, 0x7fc12345, 0x7f800000, 0xbf800000 };
    ASSERT_EQ(24, controller->ReadKeysFrom(const_cast<CKDWORD *>(wire)));
    DumpEquals(*controller, wire, 6);
    SaveEquals(animation, wire, 6);
}

TEST_F(LinearScaleAxisStorageTest, InvalidDumpTimeWritesNothingAndSavePropagatesFailure) {
    RCKObjectAnimation animation(Context);
    RCKLinearScaleAxisController *controller = Controller(animation);
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

TEST_F(LinearScaleAxisStorageTest, BoundedAnimationLoadSamplesAndSavesLiteralScaleAxis) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteScaleAxis(chunk, ScaleAxisWire, 11);
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetScaleAxisController());
    VxQuaternion output;
    ASSERT_TRUE(animation.EvaluateScaleAxis(6, output));
    QuarterTurnEquals(output);
    SaveEquals(animation, ScaleAxisWire, 11);
}

TEST_F(LinearScaleAxisStorageTest, BoundedAnimationLoadRetainsNegativeQuaternionSign) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    const CKDWORD negative[] = { 1, 0x40000000, 0x80000000, 0, 0, 0xbf800000 };
    BeginAnimation(chunk);
    WriteScaleAxis(chunk, negative, 6);
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    VxQuaternion output;
    ASSERT_TRUE(animation.EvaluateScaleAxis(2, output));
    QuaternionEquals(output, 0, 0, 0, -1);
    SaveEquals(animation, negative, 6);
}

TEST_F(LinearScaleAxisStorageTest, BoundedAnimationLoadCannotReadTruncatedKeyFromNextIdentifier) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteScaleAxis(chunk, ScaleAxisWire, 6);
    chunk.WriteDword(0);
    chunk.WriteIdentifier(0x76543210);
    for (int i = 0; i < 32; ++i) chunk.WriteDword(0x42424242);
    chunk.CloseChunk();
    chunk.StartRead();
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetScaleAxisController());
    chunk.StartRead();
    ASSERT_TRUE(chunk.SeekIdentifier(0x76543210));
    EXPECT_EQ(0x42424242u, chunk.ReadDword());
}

TEST_F(LinearScaleAxisStorageTest, BoundedAnimationLoadRejectsAbsentCount) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteScaleAxis(chunk, nullptr, 0);
    EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetScaleAxisController());
}

TEST_F(LinearScaleAxisStorageTest, BoundedAnimationLoadSkipsPaddingBeforeFollowingScaleRecord) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    CKDWORD padded[13];
    std::memcpy(padded, ScaleAxisWire, sizeof(ScaleAxisWire));
    padded[11] = 0xdeadbeef;
    padded[12] = 0xbaadf00d;
    WriteScaleAxis(chunk, padded, 13);
    const CKDWORD scale[] = { 1, 0x40000000, 0x40000000, 0x40400000, 0x40800000 };
    chunk.WriteDword(CKANIMATION_LINSCL_CONTROL);
    chunk.WriteDword(5);
    chunk.WriteBufferNoSize_LEndian(sizeof(scale), const_cast<CKDWORD *>(scale));
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetScaleAxisController());
    ASSERT_NE(nullptr, animation.GetScaleController());
    VxQuaternion rotation;
    VxVector scaling;
    ASSERT_TRUE(animation.EvaluateScaleAxis(6, rotation));
    ASSERT_TRUE(animation.EvaluateScale(2, scaling));
    QuarterTurnEquals(rotation);
    EXPECT_FLOAT_EQ(2, scaling.x);
    EXPECT_FLOAT_EQ(3, scaling.y);
    EXPECT_FLOAT_EQ(4, scaling.z);
}

TEST_F(LinearScaleAxisStorageTest, BoundedAnimationLoadRejectsUnsortedTimes) {
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    CKDWORD unsorted[11];
    std::memcpy(unsorted, ScaleAxisWire, sizeof(unsorted));
    unsorted[6] = 0x3f800000;
    BeginAnimation(chunk);
    WriteScaleAxis(chunk, unsorted, 11);
    EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetScaleAxisController());
}

TEST_F(LinearScaleAxisStorageTest, LoadedScaleAxisChangesSymmetricStretchRatherThanObjectRotation) {
    RCK3dEntity entity(Context);
    RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    WriteScaleAxis(chunk, ScaleAxisWire, 11);
    EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetRotationController());
    ASSERT_NE(nullptr, animation.GetScaleAxisController());
    VxVector position(5, 6, 7), scale(2, 3, 4);
    VxQuaternion rotation(0, 0, 0, 1);
    animation.AddPositionKey(0, &position);
    animation.AddRotationKey(0, &rotation);
    animation.AddScaleKey(0, &scale);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    animation.SetLength(8);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    // Time 4 is one quarter of [2,10]: the scale axis is 45 degrees.
    // Independently, U*diag(2,3,4)*U^T has XY diagonal 2.5 and XY
    // off-diagonal -.5. This is a symmetric stretch, not a 45-degree pose.
    const float expected[4][4] = {
        { 2.5f, -.5f, 0, 0 }, { -.5f, 2.5f, 0, 0 },
        { 0, 0, 4, 0 }, { 5, 6, 7, 1 }
    };
    const VxMatrix &matrix = entity.GetLocalMatrix();
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_NEAR(expected[row][column], matrix[row][column], 1e-5f);
    animation.Set3dEntity(nullptr);
}

TEST_F(LinearScaleAxisStorageTest, SelfAliasedDuplicateEditPreservesSignedTimeAndAllQuaternionBits) {
    RCKLinearScaleAxisController controller;
    const CKDWORD wire[] = { 1, 0x80000000, 0, 0, 0, 0x3f800000 };
    ASSERT_EQ(24, controller.ReadKeysFrom(const_cast<CKDWORD *>(wire)));
    CKScaleAxisKey *owned = Key(controller, 0);
    owned->Rot = VxQuaternion(1, 2, 3, -4);
    FailNextScaleAxisArray = true;
    const int index = controller.AddKey(owned);
    const bool noAllocation = FailNextScaleAxisArray;
    FailNextScaleAxisArray = false;
    EXPECT_EQ(0, index);
    EXPECT_TRUE(noAllocation);
    EXPECT_EQ(owned, controller.GetKey(0));
    const CKDWORD expected[] = { 1, 0x80000000, 0x3f800000, 0x40000000, 0x40400000, 0xc0800000 };
    DumpEquals(controller, expected, 6);
}

} // namespace
