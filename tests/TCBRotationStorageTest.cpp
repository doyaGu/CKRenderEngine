#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKObjectAnimation.h"
#include "RCKKeyframeData.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();

static thread_local bool FailNextTCBRotationArray = false;

void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextTCBRotationArray) {
        FailNextTCBRotationArray = false;
        return nullptr;
    }
    try {
        return ::operator new[](bytes);
    } catch (...) {
        return nullptr;
    }
}

namespace {

// Original 2.1 wire is count + ten DWORDs per 40-byte key:
// time, quaternion XYZW, tension, continuity, bias, ease-to, ease-from.
// Literal records are independent of either serialization path.
const CKDWORD RotationWire[] = {
    2,
    0x40000000, 0, 0, 0, 0x3f800000, 0, 0, 0, 0, 0,
    0x41200000, 0, 0, 0x3f800000, 0, 0, 0, 0, 0, 0
};

const CKDWORD ParameterWire[] = {
    1, 0x40000000, 0x40000000, 0x40800000, 0x40c00000, 0x41000000,
    0x3e800000, 0xbf000000, 0x3f400000, 0x3e000000, 0x3ec00000
};

CKTCBRotationKey MakeKey(float time, float x, float y, float z, float w,
                         float tension = 0, float continuity = 0, float bias = 0,
                         float easeTo = 0, float easeFrom = 0) {
    VxQuaternion rotation(x, y, z, w);
    return CKTCBRotationKey(time, rotation, tension, continuity, bias, easeTo, easeFrom);
}

CKTCBRotationKey *Key(RCKTCBRotationController &controller, int index) {
    return static_cast<CKTCBRotationKey *>(controller.GetKey(index));
}

class InspectRotationController : public RCKTCBRotationController {
public:
    const VxQuaternion *Tangents() const { return m_Tangents; }
};

void QuaternionEquals(const VxQuaternion &q, float x, float y, float z, float w) {
    EXPECT_FLOAT_EQ(x, q.x); EXPECT_FLOAT_EQ(y, q.y);
    EXPECT_FLOAT_EQ(z, q.z); EXPECT_FLOAT_EQ(w, q.w);
}

void QuarterTurnEquals(const VxQuaternion &q) {
    EXPECT_NEAR(0, q.x, 1e-6); EXPECT_NEAR(0, q.y, 1e-6);
    EXPECT_NEAR(0.7071067811865475, q.z, 1e-6);
    EXPECT_NEAR(0.7071067811865475, q.w, 1e-6);
}

class TCBRotationStorageTest : public ::testing::Test {
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
        FailNextTCBRotationArray = false;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static RCKTCBRotationController *Controller(RCKObjectAnimation &animation) {
        return static_cast<RCKTCBRotationController *>(animation.CreateController(CKANIMATION_TCBROT_CONTROL));
    }

    static void Fill(RCKTCBRotationController &controller) {
        CKTCBRotationKey first = MakeKey(2, 0, 0, 0, 1);
        CKTCBRotationKey last = MakeKey(10, 0, 0, 1, 0);
        ASSERT_EQ(0, controller.AddKey(&first));
        ASSERT_EQ(1, controller.AddKey(&last));
    }

    static void SampleMidpoint(RCKTCBRotationController &controller) {
        VxQuaternion output;
        ASSERT_TRUE(controller.Evaluate(6, &output));
        QuarterTurnEquals(output);
    }

    static void DumpEquals(RCKTCBRotationController &controller, const CKDWORD *expected, int words) {
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
            EXPECT_EQ(CKANIMATION_TCBROT_CONTROL, saved->ReadDword());
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
        chunk.WriteDword(CKANIMATION_TCBROT_CONTROL);
        chunk.WriteDword(count);
        if (count) chunk.WriteBufferNoSize_LEndian(count * 4, const_cast<CKDWORD *>(words));
    }

    static void EndAnimation(CKStateChunk &chunk) {
        chunk.WriteDword(0);
        chunk.CloseChunk();
        chunk.StartRead();
    }
};

TEST_F(TCBRotationStorageTest, InsertsBeforeBetweenAndAfterWithAllSplineParameters) {
    RCKTCBRotationController controller;
    CKTCBRotationKey middle = MakeKey(6, 1, 2, 3, 4, .25f, -.5f, .75f, .125f, .375f);
    CKTCBRotationKey last = MakeKey(10, 0, 0, 1, 0);
    CKTCBRotationKey first = MakeKey(2, 0, 0, 0, 1);
    ASSERT_EQ(0, controller.AddKey(&middle));
    ASSERT_EQ(1, controller.AddKey(&last));
    ASSERT_EQ(0, controller.AddKey(&first));
    EXPECT_FLOAT_EQ(2, Key(controller, 0)->TimeStep);
    EXPECT_EQ(0, std::memcmp(&middle, Key(controller, 1), sizeof(middle)));
    EXPECT_FLOAT_EQ(10, Key(controller, 2)->TimeStep);
}

TEST_F(TCBRotationStorageTest, InsertOwnsCallerKeyAfterMutationAndDestruction) {
    RCKTCBRotationController controller;
    {
        CKTCBRotationKey key = MakeKey(2, 2, 4, 6, 8, .25f, -.5f, .75f, .125f, .375f);
        ASSERT_EQ(0, controller.AddKey(&key));
        std::memset(&key, 0, sizeof(key));
    }
    DumpEquals(controller, ParameterWire, 11);
}

TEST_F(TCBRotationStorageTest, DuplicateRetainsSignedTimeAndAllFiveSplineParameters) {
    RCKTCBRotationController controller;
    CKDWORD wire[11];
    std::memcpy(wire, ParameterWire, sizeof(wire));
    wire[1] = 0x80000000;
    ASSERT_EQ(44, controller.ReadKeysFrom(wire));
    CKKey *owned = controller.GetKey(0);
    CKTCBRotationKey update = MakeKey(0, -2, -4, -6, -8, 9, 8, 7, 6, 5);
    FailNextTCBRotationArray = true;
    EXPECT_EQ(0, controller.AddKey(&update));
    EXPECT_TRUE(FailNextTCBRotationArray);
    FailNextTCBRotationArray = false;
    EXPECT_EQ(owned, controller.GetKey(0));
    wire[2] = 0xc0000000; wire[3] = 0xc0800000; wire[4] = 0xc0c00000; wire[5] = 0xc1000000;
    DumpEquals(controller, wire, 11);
}

TEST_F(TCBRotationStorageTest, DuplicateLoadedTimesUpdateOnlyFirstEqualRecord) {
    RCKTCBRotationController controller;
    CKDWORD wire[21];
    std::memcpy(wire, RotationWire, sizeof(wire));
    wire[11] = wire[1];
    ASSERT_EQ(84, controller.ReadKeysFrom(wire));
    CKTCBRotationKey update = MakeKey(2, -2, -4, -6, -8, 1, 2, 3, 4, 5);
    ASSERT_EQ(0, controller.AddKey(&update));
    wire[2] = 0xc0000000; wire[3] = 0xc0800000; wire[4] = 0xc0c00000; wire[5] = 0xc1000000;
    DumpEquals(controller, wire, 21);
}

TEST_F(TCBRotationStorageTest, SelfAliasedDuplicateRetainsParametersAndInvalidatesTangents) {
    InspectRotationController controller;
    Fill(controller);
    SampleMidpoint(controller);
    ASSERT_NE(nullptr, controller.Tangents());
    CKTCBRotationKey *owned = Key(controller, 0);
    owned->Rot = VxQuaternion(0, 0, 0, -1);
    owned->tension = .5f;
    FailNextTCBRotationArray = true;
    ASSERT_EQ(0, controller.AddKey(owned));
    EXPECT_TRUE(FailNextTCBRotationArray);
    FailNextTCBRotationArray = false;
    EXPECT_EQ(owned, controller.GetKey(0));
    EXPECT_EQ(nullptr, controller.Tangents());
    EXPECT_FLOAT_EQ(.5f, owned->tension);
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(2, &output));
    QuaternionEquals(output, 0, 0, 0, -1);
}

TEST_F(TCBRotationStorageTest, NullInsertAndInvalidRemovalPreserveKeysAndComputedCache) {
    InspectRotationController controller;
    Fill(controller); SampleMidpoint(controller);
    const VxQuaternion *cache = controller.Tangents();
    CKKey *first = controller.GetKey(0);
    controller.SetLength(37);
    EXPECT_EQ(-1, controller.AddKey(nullptr));
    controller.RemoveKey(-1); controller.RemoveKey(2); controller.RemoveKey(INT_MAX);
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 21); SampleMidpoint(controller);
}

TEST_F(TCBRotationStorageTest, SuccessfulDuplicateInvalidatesCacheAndChangesSampling) {
    InspectRotationController controller;
    Fill(controller); SampleMidpoint(controller);
    ASSERT_NE(nullptr, controller.Tangents());
    CKTCBRotationKey update = MakeKey(2, 0, 0, 1, 0);
    ASSERT_EQ(0, controller.AddKey(&update));
    EXPECT_EQ(nullptr, controller.Tangents());
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuaternionEquals(output, 0, 0, 1, 0);
}

TEST_F(TCBRotationStorageTest, SuccessfulInsertInvalidatesCacheAndSamplesInsertedQuaternion) {
    InspectRotationController controller;
    Fill(controller); SampleMidpoint(controller);
    CKTCBRotationKey middle = MakeKey(6, 1, 0, 0, 0);
    ASSERT_EQ(1, controller.AddKey(&middle));
    EXPECT_EQ(nullptr, controller.Tangents());
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    QuaternionEquals(output, 1, 0, 0, 0);
}

TEST_F(TCBRotationStorageTest, RemovingInteriorKeyChangesActualAnimationSampling) {
    RCKObjectAnimation animation(Context);
    RCKTCBRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller); Fill(*controller);
    CKTCBRotationKey middle = MakeKey(6, 1, 0, 0, 0);
    ASSERT_EQ(1, controller->AddKey(&middle));
    VxQuaternion output;
    ASSERT_TRUE(animation.EvaluateRotation(6, output));
    QuaternionEquals(output, 1, 0, 0, 0);
    controller->RemoveKey(1);
    ASSERT_TRUE(animation.EvaluateRotation(6, output));
    QuarterTurnEquals(output);
    SaveEquals(animation, RotationWire, 21);
}

TEST_F(TCBRotationStorageTest, RemovingFirstThenLastClearsCacheAndLeavesReusableController) {
    InspectRotationController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    controller.RemoveKey(0);
    EXPECT_EQ(nullptr, controller.Tangents());
    ASSERT_EQ(1, controller.GetKeyCount());
    QuaternionEquals(Key(controller, 0)->Rot, 0, 0, 1, 0);
    FailNextTCBRotationArray = true;
    controller.RemoveKey(0);
    EXPECT_TRUE(FailNextTCBRotationArray);
    FailNextTCBRotationArray = false;
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    const CKDWORD empty[] = { 0 }; DumpEquals(controller, empty, 1);
    CKTCBRotationKey replacement = MakeKey(3, 0, 1, 0, 0);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    VxQuaternion output;
    ASSERT_TRUE(controller.Evaluate(3, &output)); QuaternionEquals(output, 0, 1, 0, 0);
}

TEST_F(TCBRotationStorageTest, FailedInsertRetainsKeysLengthCacheAndSampling) {
    InspectRotationController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const VxQuaternion *cache = controller.Tangents();
    CKTCBRotationKey middle = MakeKey(6, 1, 0, 0, 0);
    FailNextTCBRotationArray = true;
    EXPECT_EQ(-1, controller.AddKey(&middle));
    EXPECT_FALSE(FailNextTCBRotationArray); FailNextTCBRotationArray = false;
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 21); SampleMidpoint(controller);
}

TEST_F(TCBRotationStorageTest, FailedRemoveRetainsKeysLengthCacheAndSampling) {
    InspectRotationController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const VxQuaternion *cache = controller.Tangents();
    FailNextTCBRotationArray = true;
    controller.RemoveKey(0);
    EXPECT_FALSE(FailNextTCBRotationArray); FailNextTCBRotationArray = false;
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 21); SampleMidpoint(controller);
}

TEST_F(TCBRotationStorageTest, FailedReadRetainsKeysLengthCacheAndSampling) {
    InspectRotationController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const VxQuaternion *cache = controller.Tangents();
    FailNextTCBRotationArray = true;
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(ParameterWire)));
    EXPECT_FALSE(FailNextTCBRotationArray); FailNextTCBRotationArray = false;
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, RotationWire, 21); SampleMidpoint(controller);
}

TEST_F(TCBRotationStorageTest, DumpAndActualAnimationSaveMatchLiteralFortyByteKeys) {
    RCKObjectAnimation animation(Context);
    RCKTCBRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller); Fill(*controller);
    DumpEquals(*controller, RotationWire, 21); SaveEquals(animation, RotationWire, 21);
}

TEST_F(TCBRotationStorageTest, AllFiveSplineParametersHaveLiteralWireOrder) {
    RCKObjectAnimation animation(Context);
    RCKTCBRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    CKTCBRotationKey key = MakeKey(2, 2, 4, 6, 8, .25f, -.5f, .75f, .125f, .375f);
    ASSERT_EQ(0, controller->AddKey(&key));
    DumpEquals(*controller, ParameterWire, 11); SaveEquals(animation, ParameterWire, 11);
}

TEST_F(TCBRotationStorageTest, EmptyDumpHasOnlyCountAndRetainsLength) {
    RCKTCBRotationController controller; controller.SetLength(37);
    const CKDWORD empty[] = { 0 }; DumpEquals(controller, empty, 1);
    EXPECT_FLOAT_EQ(37, controller.GetLength());
}

TEST_F(TCBRotationStorageTest, UnalignedDumpWritesExactlyItsReportedBytes) {
    RCKTCBRotationController controller; Fill(controller);
    unsigned char bytes[sizeof(RotationWire) + 2]; std::memset(bytes, 0xa5, sizeof(bytes));
    ASSERT_EQ(84, controller.DumpKeysTo(bytes + 1));
    EXPECT_EQ(0xa5, bytes[0]); EXPECT_EQ(0xa5, bytes[sizeof(bytes) - 1]);
    EXPECT_EQ(0, std::memcmp(bytes + 1, RotationWire, sizeof(RotationWire)));
}

TEST_F(TCBRotationStorageTest, RawReadOwnsLiteralKeysRetainsLengthAndInvalidatesCache) {
    InspectRotationController controller; Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    {
        CKDWORD source[11]; std::memcpy(source, ParameterWire, sizeof(source));
        ASSERT_EQ(44, controller.ReadKeysFrom(source)); std::memset(source, 0, sizeof(source));
    }
    EXPECT_EQ(nullptr, controller.Tangents()); EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, ParameterWire, 11);
    EXPECT_FLOAT_EQ(.25f, Key(controller, 0)->tension);
    EXPECT_FLOAT_EQ(-.5f, Key(controller, 0)->continuity);
    EXPECT_FLOAT_EQ(.75f, Key(controller, 0)->bias);
    EXPECT_FLOAT_EQ(.125f, Key(controller, 0)->easeto);
    EXPECT_FLOAT_EQ(.375f, Key(controller, 0)->easefrom);
}

TEST_F(TCBRotationStorageTest, RawReadAllowsUnalignedKeysAndLeavesFollowingBytes) {
    RCKTCBRotationController controller;
    unsigned char source[sizeof(RotationWire) + 5]; std::memset(source, 0x5a, sizeof(source));
    std::memcpy(source + 1, RotationWire, sizeof(RotationWire));
    ASSERT_EQ(84, controller.ReadKeysFrom(source + 1));
    DumpEquals(controller, RotationWire, 21); SampleMidpoint(controller);
    EXPECT_EQ(0x5a, source[sizeof(RotationWire) + 1]);
}

TEST_F(TCBRotationStorageTest, ReadingEmptyClearsOwnedKeysAndCacheWithoutChangingLength) {
    InspectRotationController controller; Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    const CKDWORD empty[] = { 0, 0xdeadbeef };
    ASSERT_EQ(4, controller.ReadKeysFrom(const_cast<CKDWORD *>(empty)));
    EXPECT_EQ(nullptr, controller.Tangents()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_FLOAT_EQ(37, controller.GetLength());
    VxQuaternion output(1, 2, 3, 4); EXPECT_FALSE(controller.Evaluate(2, &output)); QuaternionEquals(output, 1, 2, 3, 4);
    DumpEquals(controller, empty, 1);
}

TEST_F(TCBRotationStorageTest, InvalidCountOrTimeRetainsKeysAndComputedCache) {
    InspectRotationController controller; Fill(controller); SampleMidpoint(controller);
    CKKey *first = controller.GetKey(0); const VxQuaternion *cache = controller.Tangents();
    EXPECT_EQ(0, controller.ReadKeysFrom(nullptr));
    const CKDWORD negative[] = { 0xffffffff }, overflow[] = { 0x7fffffff };
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(negative)));
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(overflow)));
    CKDWORD invalid[21]; std::memcpy(invalid, RotationWire, sizeof(invalid));
    invalid[11] = 0x3f800000; EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[11] = 0x7fc00001; EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[11] = 0x7f800000; EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    DumpEquals(controller, RotationWire, 21); SampleMidpoint(controller);
}

TEST_F(TCBRotationStorageTest, OnlyTimeIsValidatedAndOtherPayloadBitsArePreserved) {
    RCKObjectAnimation animation(Context); RCKTCBRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    const CKDWORD wire[] = { 1, 0x40000000, 0x80000000, 0x7fc12345, 0x7f800000, 0xbf800000,
                            0xff800000, 0x7fc54321, 0x80000000, 0x3f800000, 0xbf800000 };
    ASSERT_EQ(44, controller->ReadKeysFrom(const_cast<CKDWORD *>(wire)));
    DumpEquals(*controller, wire, 11); SaveEquals(animation, wire, 11);
}

TEST_F(TCBRotationStorageTest, InvalidDumpTimeWritesNothingAndSavePropagatesFailure) {
    RCKObjectAnimation animation(Context); RCKTCBRotationController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller); Fill(*controller);
    CKDWORD output[21]; for (int i = 0; i < 21; ++i) output[i] = 0xa5a5a5a5;
    Key(*controller, 1)->TimeStep = 1;
    EXPECT_EQ(0, controller->DumpKeysTo(nullptr)); EXPECT_EQ(0, controller->DumpKeysTo(output));
    CKStateChunk *saved = animation.Save(nullptr, CK_STATESAVE_OBJANIMALL);
    EXPECT_EQ(nullptr, saved); if (saved) DeleteCKStateChunk(saved);
    const CKDWORD invalidTime = 0x7fc00001;
    std::memcpy(&Key(*controller, 1)->TimeStep, &invalidTime, sizeof(invalidTime));
    EXPECT_EQ(0, controller->DumpKeysTo(output));
    for (int i = 0; i < 21; ++i) EXPECT_EQ(0xa5a5a5a5u, output[i]);
}

TEST_F(TCBRotationStorageTest, BoundedAnimationLoadSamplesAndSavesLiteralRotation) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WriteRotation(chunk, RotationWire, 21); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetRotationController());
    VxQuaternion output; ASSERT_TRUE(animation.EvaluateRotation(6, output)); QuarterTurnEquals(output);
    SaveEquals(animation, RotationWire, 21);
}

TEST_F(TCBRotationStorageTest, BoundedAnimationLoadRetainsSplineParameters) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WriteRotation(chunk, ParameterWire, 11); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetRotationController());
    SaveEquals(animation, ParameterWire, 11);
    CKTCBRotationKey *key = static_cast<CKTCBRotationKey *>(animation.GetRotationController()->GetKey(0));
    EXPECT_FLOAT_EQ(.125f, key->easeto); EXPECT_FLOAT_EQ(.375f, key->easefrom);
}

TEST_F(TCBRotationStorageTest, BoundedAnimationLoadCannotReadTruncatedKeyFromNextIdentifier) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WriteRotation(chunk, RotationWire, 11); chunk.WriteDword(0);
    chunk.WriteIdentifier(0x76543210);
    for (int i = 0; i < 32; ++i) chunk.WriteDword(0x42424242);
    chunk.CloseChunk(); chunk.StartRead();
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetRotationController());
    chunk.StartRead(); ASSERT_TRUE(chunk.SeekIdentifier(0x76543210)); EXPECT_EQ(0x42424242u, chunk.ReadDword());
}

TEST_F(TCBRotationStorageTest, BoundedAnimationLoadRejectsAbsentCount) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WriteRotation(chunk, nullptr, 0); EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetRotationController());
}

TEST_F(TCBRotationStorageTest, BoundedAnimationLoadSkipsPaddingBeforeFollowingScaleRecord) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    CKDWORD padded[23]; std::memcpy(padded, RotationWire, sizeof(RotationWire));
    padded[21] = 0xdeadbeef; padded[22] = 0xbaadf00d; WriteRotation(chunk, padded, 23);
    const CKDWORD scale[] = { 1, 0x40000000, 0x40000000, 0x40400000, 0x40800000 };
    chunk.WriteDword(CKANIMATION_LINSCL_CONTROL); chunk.WriteDword(5);
    chunk.WriteBufferNoSize_LEndian(sizeof(scale), const_cast<CKDWORD *>(scale)); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetRotationController()); ASSERT_NE(nullptr, animation.GetScaleController());
    VxQuaternion rotation; VxVector scaling;
    ASSERT_TRUE(animation.EvaluateRotation(6, rotation)); ASSERT_TRUE(animation.EvaluateScale(2, scaling));
    QuarterTurnEquals(rotation);
    EXPECT_FLOAT_EQ(2, scaling.x); EXPECT_FLOAT_EQ(3, scaling.y); EXPECT_FLOAT_EQ(4, scaling.z);
}

TEST_F(TCBRotationStorageTest, BoundedAnimationLoadRejectsUnsortedTimes) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    CKDWORD unsorted[21]; std::memcpy(unsorted, RotationWire, sizeof(unsorted)); unsorted[11] = 0x3f800000;
    BeginAnimation(chunk); WriteRotation(chunk, unsorted, 21); EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr)); EXPECT_EQ(nullptr, animation.GetRotationController());
}

TEST_F(TCBRotationStorageTest, LoadedTCBRotationDrivesActualEntityOrientationAtInteriorTime) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WriteRotation(chunk, RotationWire, 21); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetScaleController()); EXPECT_EQ(nullptr, animation.GetPositionController());
    const VxVector translation(3, 4, 5);
    entity.SetPosition(&translation, nullptr, FALSE);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &matrix = entity.GetLocalMatrix();
    const float expected[4][3] = { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 3, 4, 5 } };
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 3; ++column)
            EXPECT_NEAR(expected[row][column], matrix[row][column], 2e-6);
    animation.Set3dEntity(nullptr);
}

} // namespace
