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

static thread_local bool FailNextTCBPositionArray = false;

void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextTCBPositionArray) {
        FailNextTCBPositionArray = false;
        return nullptr;
    }
    try {
        return ::operator new[](bytes);
    } catch (...) {
        return nullptr;
    }
}

namespace {

// Original 2.1 raw records are count + nine DWORDs per key:
// time, position XYZ, tension, continuity, bias, ease-to, ease-from.
// These literals do not use either controller serialization path.
const CKDWORD PositionWire[] = {
    2,
    0x40000000, 0x40000000, 0x40800000, 0x40c00000, 0, 0, 0, 0, 0,
    0x41200000, 0x41200000, 0x41a00000, 0x41f00000, 0, 0, 0, 0, 0
};

const CKDWORD ParameterWire[] = {
    1, 0x40000000, 0x40000000, 0x40800000, 0x40c00000,
    0x3e800000, 0xbf000000, 0x3f400000, 0x3e000000, 0x3ec00000
};

CKTCBPositionKey MakeKey(float time, float x, float y, float z,
                         float tension = 0, float continuity = 0, float bias = 0,
                         float easeTo = 0, float easeFrom = 0) {
    VxVector position(x, y, z);
    return CKTCBPositionKey(time, position, tension, continuity, bias, easeTo, easeFrom);
}

CKTCBPositionKey *Key(RCKTCBPositionController &controller, int index) {
    return static_cast<CKTCBPositionKey *>(controller.GetKey(index));
}

class InspectPositionController : public RCKTCBPositionController {
public:
    const VxVector *Tangents() const { return m_Tangents; }
};

void VectorEquals(const VxVector &vector, float x, float y, float z) {
    EXPECT_FLOAT_EQ(x, vector.x);
    EXPECT_FLOAT_EQ(y, vector.y);
    EXPECT_FLOAT_EQ(z, vector.z);
}

class TCBPositionStorageTest : public ::testing::Test {
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
        FailNextTCBPositionArray = false;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static RCKTCBPositionController *Controller(RCKObjectAnimation &animation) {
        return static_cast<RCKTCBPositionController *>(animation.CreateController(CKANIMATION_TCBPOS_CONTROL));
    }

    static void Fill(RCKTCBPositionController &controller) {
        CKTCBPositionKey first = MakeKey(2, 2, 4, 6);
        CKTCBPositionKey last = MakeKey(10, 10, 20, 30);
        ASSERT_EQ(0, controller.AddKey(&first));
        ASSERT_EQ(1, controller.AddKey(&last));
    }

    static void SampleMidpoint(RCKTCBPositionController &controller) {
        VxVector output;
        ASSERT_TRUE(controller.Evaluate(6, &output));
        VectorEquals(output, 6, 12, 18);
    }

    static void DumpEquals(RCKTCBPositionController &controller, const CKDWORD *expected, int words) {
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
            EXPECT_EQ(CKANIMATION_TCBPOS_CONTROL, saved->ReadDword());
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
        chunk.WriteDword(CKANIMATION_TCBPOS_CONTROL);
        chunk.WriteDword(count);
        if (count) chunk.WriteBufferNoSize_LEndian(count * 4, const_cast<CKDWORD *>(words));
    }

    static void EndAnimation(CKStateChunk &chunk) {
        chunk.WriteDword(0);
        chunk.CloseChunk();
        chunk.StartRead();
    }
};

TEST_F(TCBPositionStorageTest, InsertsBeforeBetweenAndAfterWithAllSplineParameters) {
    RCKTCBPositionController controller;
    CKTCBPositionKey middle = MakeKey(6, 6, 12, 18, .25f, -.5f, .75f, .125f, .375f);
    CKTCBPositionKey last = MakeKey(10, 10, 20, 30);
    CKTCBPositionKey first = MakeKey(2, 2, 4, 6);
    ASSERT_EQ(0, controller.AddKey(&middle));
    ASSERT_EQ(1, controller.AddKey(&last));
    ASSERT_EQ(0, controller.AddKey(&first));
    EXPECT_FLOAT_EQ(2, Key(controller, 0)->TimeStep);
    EXPECT_EQ(0, std::memcmp(&middle, Key(controller, 1), sizeof(middle)));
    EXPECT_FLOAT_EQ(10, Key(controller, 2)->TimeStep);
}

TEST_F(TCBPositionStorageTest, InsertOwnsCallerKeyAfterMutationAndDestruction) {
    RCKTCBPositionController controller;
    {
        CKTCBPositionKey key = MakeKey(2, 2, 4, 6, .25f, -.5f, .75f, .125f, .375f);
        ASSERT_EQ(0, controller.AddKey(&key));
        std::memset(&key, 0, sizeof(key));
    }
    DumpEquals(controller, ParameterWire, 10);
}

TEST_F(TCBPositionStorageTest, DuplicateRetainsSignedTimeAndAllFiveSplineParameters) {
    RCKTCBPositionController controller;
    CKDWORD wire[10];
    std::memcpy(wire, ParameterWire, sizeof(wire));
    wire[1] = 0x80000000;
    ASSERT_EQ(40, controller.ReadKeysFrom(wire));
    CKKey *owned = controller.GetKey(0);
    CKTCBPositionKey update = MakeKey(0, -2, -4, -6, 9, 8, 7, 6, 5);
    FailNextTCBPositionArray = true;
    EXPECT_EQ(0, controller.AddKey(&update));
    EXPECT_TRUE(FailNextTCBPositionArray);
    FailNextTCBPositionArray = false;
    EXPECT_EQ(owned, controller.GetKey(0));
    wire[2] = 0xc0000000; wire[3] = 0xc0800000; wire[4] = 0xc0c00000;
    DumpEquals(controller, wire, 10);
}

TEST_F(TCBPositionStorageTest, DuplicateLoadedTimesUpdateOnlyFirstEqualRecord) {
    RCKTCBPositionController controller;
    CKDWORD wire[19];
    std::memcpy(wire, PositionWire, sizeof(wire));
    wire[10] = wire[1];
    ASSERT_EQ(76, controller.ReadKeysFrom(wire));
    CKTCBPositionKey update = MakeKey(2, -2, -4, -6, 1, 2, 3, 4, 5);
    ASSERT_EQ(0, controller.AddKey(&update));
    wire[2] = 0xc0000000; wire[3] = 0xc0800000; wire[4] = 0xc0c00000;
    DumpEquals(controller, wire, 19);
}

TEST_F(TCBPositionStorageTest, SelfAliasedDuplicateRetainsParametersAndInvalidatesTangents) {
    InspectPositionController controller;
    Fill(controller);
    SampleMidpoint(controller);
    ASSERT_NE(nullptr, controller.Tangents());
    CKTCBPositionKey *owned = Key(controller, 0);
    owned->Pos = VxVector(4, 8, 12);
    owned->tension = .5f;
    FailNextTCBPositionArray = true;
    ASSERT_EQ(0, controller.AddKey(owned));
    EXPECT_TRUE(FailNextTCBPositionArray);
    FailNextTCBPositionArray = false;
    EXPECT_EQ(owned, controller.GetKey(0));
    EXPECT_EQ(nullptr, controller.Tangents());
    EXPECT_FLOAT_EQ(.5f, owned->tension);
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(2, &output));
    VectorEquals(output, 4, 8, 12);
}

TEST_F(TCBPositionStorageTest, NullInsertAndInvalidRemovalPreserveKeysAndComputedCache) {
    InspectPositionController controller;
    Fill(controller); SampleMidpoint(controller);
    const VxVector *cache = controller.Tangents();
    CKKey *first = controller.GetKey(0);
    controller.SetLength(37);
    EXPECT_EQ(-1, controller.AddKey(nullptr));
    controller.RemoveKey(-1); controller.RemoveKey(2); controller.RemoveKey(INT_MAX);
    EXPECT_EQ(first, controller.GetKey(0));
    EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 19); SampleMidpoint(controller);
}

TEST_F(TCBPositionStorageTest, SuccessfulDuplicateInvalidatesCacheAndChangesSampling) {
    InspectPositionController controller;
    Fill(controller); SampleMidpoint(controller);
    ASSERT_NE(nullptr, controller.Tangents());
    CKTCBPositionKey update = MakeKey(2, 4, 8, 12);
    ASSERT_EQ(0, controller.AddKey(&update));
    EXPECT_EQ(nullptr, controller.Tangents());
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    VectorEquals(output, 7, 14, 21);
}

TEST_F(TCBPositionStorageTest, SuccessfulInsertInvalidatesCacheAndSamplesInsertedPosition) {
    InspectPositionController controller;
    Fill(controller); SampleMidpoint(controller);
    CKTCBPositionKey middle = MakeKey(6, -6, -12, -18);
    ASSERT_EQ(1, controller.AddKey(&middle));
    EXPECT_EQ(nullptr, controller.Tangents());
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(6, &output));
    VectorEquals(output, -6, -12, -18);
}

TEST_F(TCBPositionStorageTest, RemovingInteriorKeyChangesActualAnimationSampling) {
    RCKObjectAnimation animation(Context);
    RCKTCBPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller); Fill(*controller);
    CKTCBPositionKey middle = MakeKey(6, -6, -12, -18);
    ASSERT_EQ(1, controller->AddKey(&middle));
    VxVector output;
    ASSERT_TRUE(animation.EvaluatePosition(6, output));
    VectorEquals(output, -6, -12, -18);
    controller->RemoveKey(1);
    ASSERT_TRUE(animation.EvaluatePosition(6, output));
    VectorEquals(output, 6, 12, 18);
    SaveEquals(animation, PositionWire, 19);
}

TEST_F(TCBPositionStorageTest, RemovingFirstThenLastClearsCacheAndLeavesReusableController) {
    InspectPositionController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    controller.RemoveKey(0);
    EXPECT_EQ(nullptr, controller.Tangents());
    ASSERT_EQ(1, controller.GetKeyCount());
    VectorEquals(Key(controller, 0)->Pos, 10, 20, 30);
    FailNextTCBPositionArray = true;
    controller.RemoveKey(0);
    EXPECT_TRUE(FailNextTCBPositionArray);
    FailNextTCBPositionArray = false;
    EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0));
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    const CKDWORD empty[] = { 0 }; DumpEquals(controller, empty, 1);
    CKTCBPositionKey replacement = MakeKey(3, 3, 6, 9);
    ASSERT_EQ(0, controller.AddKey(&replacement));
    VxVector output;
    ASSERT_TRUE(controller.Evaluate(3, &output)); VectorEquals(output, 3, 6, 9);
}

TEST_F(TCBPositionStorageTest, FailedInsertRetainsKeysLengthCacheAndSampling) {
    InspectPositionController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const VxVector *cache = controller.Tangents();
    CKTCBPositionKey middle = MakeKey(6, -6, -12, -18);
    FailNextTCBPositionArray = true;
    EXPECT_EQ(-1, controller.AddKey(&middle));
    EXPECT_FALSE(FailNextTCBPositionArray); FailNextTCBPositionArray = false;
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 19); SampleMidpoint(controller);
}

TEST_F(TCBPositionStorageTest, FailedRemoveRetainsKeysLengthCacheAndSampling) {
    InspectPositionController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const VxVector *cache = controller.Tangents();
    FailNextTCBPositionArray = true;
    controller.RemoveKey(0);
    EXPECT_FALSE(FailNextTCBPositionArray); FailNextTCBPositionArray = false;
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 19); SampleMidpoint(controller);
}

TEST_F(TCBPositionStorageTest, FailedReadRetainsKeysLengthCacheAndSampling) {
    InspectPositionController controller;
    Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    CKKey *first = controller.GetKey(0);
    const VxVector *cache = controller.Tangents();
    FailNextTCBPositionArray = true;
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(ParameterWire)));
    EXPECT_FALSE(FailNextTCBPositionArray); FailNextTCBPositionArray = false;
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, PositionWire, 19); SampleMidpoint(controller);
}

TEST_F(TCBPositionStorageTest, DumpAndActualAnimationSaveMatchLiteralThirtySixByteKeys) {
    RCKObjectAnimation animation(Context);
    RCKTCBPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller); Fill(*controller);
    DumpEquals(*controller, PositionWire, 19); SaveEquals(animation, PositionWire, 19);
}

TEST_F(TCBPositionStorageTest, AllFiveSplineParametersHaveLiteralWireOrder) {
    RCKObjectAnimation animation(Context);
    RCKTCBPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    CKTCBPositionKey key = MakeKey(2, 2, 4, 6, .25f, -.5f, .75f, .125f, .375f);
    ASSERT_EQ(0, controller->AddKey(&key));
    DumpEquals(*controller, ParameterWire, 10); SaveEquals(animation, ParameterWire, 10);
}

TEST_F(TCBPositionStorageTest, EmptyDumpHasOnlyCountAndRetainsLength) {
    RCKTCBPositionController controller; controller.SetLength(37);
    const CKDWORD empty[] = { 0 }; DumpEquals(controller, empty, 1);
    EXPECT_FLOAT_EQ(37, controller.GetLength());
}

TEST_F(TCBPositionStorageTest, UnalignedDumpWritesExactlyItsReportedBytes) {
    RCKTCBPositionController controller; Fill(controller);
    unsigned char bytes[sizeof(PositionWire) + 2]; std::memset(bytes, 0xa5, sizeof(bytes));
    ASSERT_EQ(76, controller.DumpKeysTo(bytes + 1));
    EXPECT_EQ(0xa5, bytes[0]); EXPECT_EQ(0xa5, bytes[sizeof(bytes) - 1]);
    EXPECT_EQ(0, std::memcmp(bytes + 1, PositionWire, sizeof(PositionWire)));
}

TEST_F(TCBPositionStorageTest, RawReadOwnsLiteralKeysRetainsLengthAndInvalidatesCache) {
    InspectPositionController controller; Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    {
        CKDWORD source[10]; std::memcpy(source, ParameterWire, sizeof(source));
        ASSERT_EQ(40, controller.ReadKeysFrom(source)); std::memset(source, 0, sizeof(source));
    }
    EXPECT_EQ(nullptr, controller.Tangents()); EXPECT_FLOAT_EQ(37, controller.GetLength());
    DumpEquals(controller, ParameterWire, 10);
    EXPECT_FLOAT_EQ(.25f, Key(controller, 0)->tension);
    EXPECT_FLOAT_EQ(-.5f, Key(controller, 0)->continuity);
    EXPECT_FLOAT_EQ(.75f, Key(controller, 0)->bias);
    EXPECT_FLOAT_EQ(.125f, Key(controller, 0)->easeto);
    EXPECT_FLOAT_EQ(.375f, Key(controller, 0)->easefrom);
}

TEST_F(TCBPositionStorageTest, RawReadAllowsUnalignedKeysAndLeavesFollowingBytes) {
    RCKTCBPositionController controller;
    unsigned char source[sizeof(PositionWire) + 5]; std::memset(source, 0x5a, sizeof(source));
    std::memcpy(source + 1, PositionWire, sizeof(PositionWire));
    ASSERT_EQ(76, controller.ReadKeysFrom(source + 1));
    DumpEquals(controller, PositionWire, 19); SampleMidpoint(controller);
    EXPECT_EQ(0x5a, source[sizeof(PositionWire) + 1]);
}

TEST_F(TCBPositionStorageTest, ReadingEmptyClearsOwnedKeysAndCacheWithoutChangingLength) {
    InspectPositionController controller; Fill(controller); SampleMidpoint(controller); controller.SetLength(37);
    const CKDWORD empty[] = { 0, 0xdeadbeef };
    ASSERT_EQ(4, controller.ReadKeysFrom(const_cast<CKDWORD *>(empty)));
    EXPECT_EQ(nullptr, controller.Tangents()); EXPECT_EQ(0, controller.GetKeyCount());
    EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_FLOAT_EQ(37, controller.GetLength());
    VxVector output(1, 2, 3); EXPECT_FALSE(controller.Evaluate(2, &output)); VectorEquals(output, 1, 2, 3);
    DumpEquals(controller, empty, 1);
}

TEST_F(TCBPositionStorageTest, InvalidCountOrTimeRetainsKeysAndComputedCache) {
    InspectPositionController controller; Fill(controller); SampleMidpoint(controller);
    CKKey *first = controller.GetKey(0); const VxVector *cache = controller.Tangents();
    EXPECT_EQ(0, controller.ReadKeysFrom(nullptr));
    const CKDWORD negative[] = { 0xffffffff }, overflow[] = { 0x7fffffff };
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(negative)));
    EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(overflow)));
    CKDWORD invalid[19]; std::memcpy(invalid, PositionWire, sizeof(invalid));
    invalid[10] = 0x3f800000; EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[10] = 0x7fc00001; EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    invalid[10] = 0x7f800000; EXPECT_EQ(0, controller.ReadKeysFrom(invalid));
    EXPECT_EQ(first, controller.GetKey(0)); EXPECT_EQ(cache, controller.Tangents());
    DumpEquals(controller, PositionWire, 19); SampleMidpoint(controller);
}

TEST_F(TCBPositionStorageTest, OnlyTimeIsValidatedAndOtherPayloadBitsArePreserved) {
    RCKObjectAnimation animation(Context); RCKTCBPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller);
    const CKDWORD wire[] = { 1, 0x40000000, 0x80000000, 0x7fc12345, 0x7f800000,
                            0xff800000, 0x7fc54321, 0x80000000, 0x3f800000, 0xbf800000 };
    ASSERT_EQ(40, controller->ReadKeysFrom(const_cast<CKDWORD *>(wire)));
    DumpEquals(*controller, wire, 10); SaveEquals(animation, wire, 10);
}

TEST_F(TCBPositionStorageTest, InvalidDumpTimeWritesNothingAndSavePropagatesFailure) {
    RCKObjectAnimation animation(Context); RCKTCBPositionController *controller = Controller(animation);
    ASSERT_NE(nullptr, controller); Fill(*controller);
    CKDWORD output[19]; for (int i = 0; i < 19; ++i) output[i] = 0xa5a5a5a5;
    Key(*controller, 1)->TimeStep = 1;
    EXPECT_EQ(0, controller->DumpKeysTo(nullptr)); EXPECT_EQ(0, controller->DumpKeysTo(output));
    CKStateChunk *saved = animation.Save(nullptr, CK_STATESAVE_OBJANIMALL);
    EXPECT_EQ(nullptr, saved); if (saved) DeleteCKStateChunk(saved);
    const CKDWORD invalidTime = 0x7fc00001;
    std::memcpy(&Key(*controller, 1)->TimeStep, &invalidTime, sizeof(invalidTime));
    EXPECT_EQ(0, controller->DumpKeysTo(output));
    for (int i = 0; i < 19; ++i) EXPECT_EQ(0xa5a5a5a5u, output[i]);
}

TEST_F(TCBPositionStorageTest, BoundedAnimationLoadSamplesAndSavesLiteralPosition) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WritePosition(chunk, PositionWire, 19); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetPositionController());
    VxVector output; ASSERT_TRUE(animation.EvaluatePosition(6, output)); VectorEquals(output, 6, 12, 18);
    SaveEquals(animation, PositionWire, 19);
}

TEST_F(TCBPositionStorageTest, BoundedAnimationLoadRetainsSplineParameters) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WritePosition(chunk, ParameterWire, 10); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetPositionController());
    SaveEquals(animation, ParameterWire, 10);
    CKTCBPositionKey *key = static_cast<CKTCBPositionKey *>(animation.GetPositionController()->GetKey(0));
    EXPECT_FLOAT_EQ(.125f, key->easeto); EXPECT_FLOAT_EQ(.375f, key->easefrom);
}

TEST_F(TCBPositionStorageTest, BoundedAnimationLoadCannotReadTruncatedKeyFromNextIdentifier) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WritePosition(chunk, PositionWire, 10); chunk.WriteDword(0);
    chunk.WriteIdentifier(0x76543210);
    for (int i = 0; i < 32; ++i) chunk.WriteDword(0x42424242);
    chunk.CloseChunk(); chunk.StartRead();
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetPositionController());
    chunk.StartRead(); ASSERT_TRUE(chunk.SeekIdentifier(0x76543210)); EXPECT_EQ(0x42424242u, chunk.ReadDword());
}

TEST_F(TCBPositionStorageTest, BoundedAnimationLoadRejectsAbsentCount) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WritePosition(chunk, nullptr, 0); EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetPositionController());
}

TEST_F(TCBPositionStorageTest, BoundedAnimationLoadSkipsPaddingBeforeFollowingScaleRecord) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk);
    CKDWORD padded[21]; std::memcpy(padded, PositionWire, sizeof(PositionWire));
    padded[19] = 0xdeadbeef; padded[20] = 0xbaadf00d; WritePosition(chunk, padded, 21);
    const CKDWORD scale[] = { 1, 0x40000000, 0x40000000, 0x40400000, 0x40800000 };
    chunk.WriteDword(CKANIMATION_LINSCL_CONTROL); chunk.WriteDword(5);
    chunk.WriteBufferNoSize_LEndian(sizeof(scale), const_cast<CKDWORD *>(scale)); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    ASSERT_NE(nullptr, animation.GetPositionController()); ASSERT_NE(nullptr, animation.GetScaleController());
    VxVector position, scaling;
    ASSERT_TRUE(animation.EvaluatePosition(6, position)); ASSERT_TRUE(animation.EvaluateScale(2, scaling));
    VectorEquals(position, 6, 12, 18); VectorEquals(scaling, 2, 3, 4);
}

TEST_F(TCBPositionStorageTest, BoundedAnimationLoadRejectsUnsortedTimes) {
    RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    CKDWORD unsorted[19]; std::memcpy(unsorted, PositionWire, sizeof(unsorted)); unsorted[10] = 0x3f800000;
    BeginAnimation(chunk); WritePosition(chunk, unsorted, 19); EndAnimation(chunk);
    EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr)); EXPECT_EQ(nullptr, animation.GetPositionController());
}

TEST_F(TCBPositionStorageTest, LoadedTCBPositionDrivesActualEntityTranslationAtInteriorTime) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    BeginAnimation(chunk); WritePosition(chunk, PositionWire, 19); EndAnimation(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
    EXPECT_EQ(nullptr, animation.GetScaleController()); EXPECT_EQ(nullptr, animation.GetRotationController());
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f));
    const VxMatrix &matrix = entity.GetLocalMatrix();
    VectorEquals(matrix[3], 6, 12, 18);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            EXPECT_FLOAT_EQ(row == column ? 1.0f : 0.0f, matrix[row][column]);
    animation.Set3dEntity(nullptr);
}

} // namespace
