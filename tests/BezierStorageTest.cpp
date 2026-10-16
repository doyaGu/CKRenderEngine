#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"

extern void SetProcessorSpecific_FunctionsPtr();
static thread_local bool FailBezierArray = false;
void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    if (FailBezierArray) { FailBezierArray = false; return nullptr; }
    try { return ::operator new[](size); } catch (const std::bad_alloc &) { return nullptr; }
}
namespace {
// Original2.1 wire: count; time,XYZ,flags; optional In then Out, each three
// DWORDs, present when the corresponding 16-bit mode contains bit0x20.
const CKDWORD LinearWire[] = {
    2, 0x40000000, 0x40000000, 0x40800000, 0x40c00000, 0x00020002,
       0x41200000, 0x41200000, 0x41a00000, 0x41f00000, 0x00020002
};
const CKDWORD ExplicitWire[] = {
    1, 0x40000000, 0x40000000, 0x40800000, 0x40c00000, 0x00200020,
       0xbf800000, 0xc0000000, 0xc0400000, 0x40800000, 0x40a00000, 0x40c00000
};
const CKDWORD MixedWire[] = {
    2, 0x40000000, 0x40000000, 0x40800000, 0x40c00000, 0x00200002,
       0x40400000, 0x40c00000, 0x41100000,
       0x41200000, 0x41200000, 0x41a00000, 0x41f00000, 0x00020020,
       0xc0400000, 0xc0c00000, 0xc1100000
};
class InspectPosition : public RCKBezierPositionController {
public:
    CKBOOL Computed() const { return m_TangentsComputed; }
};
class InspectScale : public RCKBezierScaleController {
public:
    CKBOOL Computed() const { return m_TangentsComputed; }
};
class BezierStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    InspectPosition Position;
    InspectScale Scale;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { FailBezierArray = false; if (Context) CKCloseContext(Context); CKShutdown(); }
    CKAnimController &Controller(int kind) { return kind == 0 ? static_cast<CKAnimController &>(Position) : static_cast<CKAnimController &>(Scale); }
    CKBOOL Computed(int kind) const { return kind == 0 ? Position.Computed() : Scale.Computed(); }
    static CKANIMATION_CONTROLLER Type(int kind) { return kind == 0 ? CKANIMATION_BEZIERPOS_CONTROL : CKANIMATION_BEZIERSCL_CONTROL; }
    static CKBezierPositionKey *Key(CKAnimController &controller, int index) { return static_cast<CKBezierPositionKey *>(controller.GetKey(index)); }
    static void Vec(const VxVector &actual, float x, float y, float z) {
        EXPECT_NEAR(x, actual.x, .00002f); EXPECT_NEAR(y, actual.y, .00002f); EXPECT_NEAR(z, actual.z, .00002f);
    }
    static CKBezierPositionKey Make(float time, float x, float y, float z) {
        CKBezierPositionKey key; key.TimeStep = time; key.Pos.Set(x, y, z);
        key.In.Set(-1, -2, -3); key.Out.Set(4, 5, 6);
        key.Flags.SetInTangentMode(BEZIER_KEY_TANGENTS); key.Flags.SetOutTangentMode(BEZIER_KEY_TANGENTS); return key;
    }
    static void Fill(CKAnimController &controller) { ASSERT_EQ(44, controller.ReadKeysFrom(const_cast<CKDWORD *>(LinearWire))); controller.SetLength(37); }
    static void Sample(CKAnimController &controller) { VxVector output; ASSERT_TRUE(controller.Evaluate(6, &output)); Vec(output, 6, 12, 18); }
    static void Dump(CKAnimController &controller, const CKDWORD *words, int count) {
        CKDWORD output[80]; for (int i = 0; i < 80; ++i) output[i] = 0xa5a5a5a5;
        ASSERT_LT(count, 80); ASSERT_EQ(count * 4, controller.DumpKeysTo(nullptr)); ASSERT_EQ(count * 4, controller.DumpKeysTo(output));
        EXPECT_EQ(0, std::memcmp(output, words, count * 4)); EXPECT_EQ(0xa5a5a5a5u, output[count]);
    }
    static void Begin(CKStateChunk &chunk) {
        chunk.StartWrite(); chunk.SetDataVersion(CHUNKDATA_CURRENTVERSION); chunk.WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
        for (int i = 0; i < 7; ++i) chunk.WriteFloat(0);
        chunk.WriteDword(0); chunk.WriteObject(nullptr); chunk.WriteFloat(20);
    }
    static void Write(CKStateChunk &chunk, CKANIMATION_CONTROLLER type, const CKDWORD *words, int count) {
        chunk.WriteDword(type); chunk.WriteDword(count);
        if (count) chunk.WriteBufferNoSize_LEndian(count * 4, const_cast<CKDWORD *>(words));
    }
    static void End(CKStateChunk &chunk) { chunk.WriteDword(0); chunk.CloseChunk(); chunk.StartRead(); }
    static void SaveMatches(RCKObjectAnimation &animation, CKANIMATION_CONTROLLER expectedType, const CKDWORD *words, int count) {
        CKStateChunk *saved = animation.Save(nullptr, CK_STATESAVE_OBJANIMALL); ASSERT_NE(nullptr, saved);
        saved->StartRead(); const CKBOOL found = saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS); EXPECT_TRUE(found);
        bool matched = false;
        if (found) {
            saved->Skip(10);
            for (int record = 0; record < 4; ++record) {
                const CKDWORD type = saved->ReadDword(); if (!type) break;
                const CKDWORD size = saved->ReadDword();
                if (type == static_cast<CKDWORD>(expectedType)) {
                    EXPECT_EQ(static_cast<CKDWORD>(count), size);
                    if (size == static_cast<CKDWORD>(count)) EXPECT_EQ(0, std::memcmp(saved->LockReadBuffer(), words, count * 4));
                    matched = true;
                }
                saved->Skip(size);
            }
        }
        EXPECT_TRUE(matched); DeleteCKStateChunk(saved);
    }
};

TEST_F(BezierStorageTest, SortedInsertionOwnsCompleteKeysAndDuplicateUpdatesOnlyTheValue) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); CKBezierPositionKey b = Make(6, 6, 12, 18), a = Make(2, 2, 4, 6), c = Make(10, 10, 20, 30);
        ASSERT_EQ(0, controller.AddKey(&b)); ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(2, controller.AddKey(&c));
        EXPECT_EQ(0, std::memcmp(&a, Key(controller, 0), sizeof(a))); EXPECT_EQ(0, std::memcmp(&c, Key(controller, 2), sizeof(c)));
        CKBezierPositionKey expected = *Key(controller, 1), update = Make(6, -6, -12, -18);
        update.Flags.SetInTangentMode(BEZIER_KEY_STEP); update.In.Set(90, 80, 70); update.Out.Set(60, 50, 40);
        FailBezierArray = true; EXPECT_EQ(1, controller.AddKey(&update)); EXPECT_TRUE(FailBezierArray); FailBezierArray = false;
        expected.Pos = update.Pos; EXPECT_EQ(0, std::memcmp(&expected, Key(controller, 1), sizeof(expected)));
        std::memset(&a, 0, sizeof(a)); Vec(Key(controller, 0)->Pos, 2, 4, 6); EXPECT_EQ(3, controller.GetKeyCount());
    }
}

TEST_F(BezierStorageTest, SignedZeroAndSelfAliasedDuplicatePreserveTimeFlagsAndDerivatives) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); CKDWORD wire[12]; std::memcpy(wire, ExplicitWire, sizeof(wire)); wire[1] = 0x80000000;
        ASSERT_EQ(48, controller.ReadKeysFrom(wire)); VxVector output; ASSERT_TRUE(controller.Evaluate(0, &output));
        CKBezierPositionKey *owned = Key(controller, 0); owned->Pos.Set(-2, -4, -6);
        FailBezierArray = true; EXPECT_EQ(0, controller.AddKey(owned)); EXPECT_TRUE(FailBezierArray); FailBezierArray = false;
        EXPECT_EQ(owned, controller.GetKey(0)); EXPECT_FALSE(Computed(kind));
        wire[2] = 0xc0000000; wire[3] = 0xc0800000; wire[4] = 0xc0c00000; Dump(controller, wire, 12);
    }
}

TEST_F(BezierStorageTest, ValidRemovePreservesSurvivorFieldsAndLastRemovalNeedsNoAllocation) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); Fill(controller); Sample(controller);
        CKBezierPositionKey middle = Make(6, -6, -12, -18); ASSERT_EQ(1, controller.AddKey(&middle));
        VxVector output; ASSERT_TRUE(controller.Evaluate(6, &output)); Vec(output, -6, -12, -18);
        controller.RemoveKey(1); EXPECT_FALSE(Computed(kind)); Sample(controller); controller.RemoveKey(0);
        Vec(Key(controller, 0)->Pos, 10, 20, 30); FailBezierArray = true; controller.RemoveKey(0);
        EXPECT_TRUE(FailBezierArray); FailBezierArray = false; EXPECT_EQ(0, controller.GetKeyCount()); EXPECT_EQ(nullptr, controller.GetKey(0));
        EXPECT_FLOAT_EQ(37, controller.GetLength()); const CKDWORD empty[] = { 0 }; Dump(controller, empty, 1);
    }
}

TEST_F(BezierStorageTest, InvalidEditsAndAllocationFailuresPreserveKeysLengthCacheAndSampling) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); Fill(controller); Sample(controller); CKKey *first = controller.GetKey(0);
        EXPECT_EQ(-1, controller.AddKey(nullptr)); controller.RemoveKey(-1); controller.RemoveKey(INT_MAX);
        CKBezierPositionKey key = Make(6, -6, -12, -18);
        FailBezierArray = true; EXPECT_EQ(-1, controller.AddKey(&key)); EXPECT_FALSE(FailBezierArray);
        FailBezierArray = true; controller.RemoveKey(0); EXPECT_FALSE(FailBezierArray);
        FailBezierArray = true; EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(ExplicitWire))); EXPECT_FALSE(FailBezierArray); FailBezierArray = false;
        EXPECT_EQ(first, controller.GetKey(0)); EXPECT_TRUE(Computed(kind)); EXPECT_FLOAT_EQ(37, controller.GetLength()); Dump(controller, LinearWire, 11); Sample(controller);
    }
}

TEST_F(BezierStorageTest, LiteralCompactAndOptionalWireLayoutsRoundTripWithoutComputingTangents) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind);
        ASSERT_EQ(44, controller.ReadKeysFrom(const_cast<CKDWORD *>(LinearWire))); EXPECT_FALSE(Computed(kind)); Dump(controller, LinearWire, 11);
        Vec(Key(controller, 0)->In, 0, 0, 0); Vec(Key(controller, 0)->Out, 0, 0, 0);
        ASSERT_EQ(68, controller.ReadKeysFrom(const_cast<CKDWORD *>(MixedWire))); EXPECT_FALSE(Computed(kind)); Dump(controller, MixedWire, 17);
        Vec(Key(controller, 0)->Out, 3, 6, 9); Vec(Key(controller, 1)->In, -3, -6, -9);
        ASSERT_EQ(48, controller.ReadKeysFrom(const_cast<CKDWORD *>(ExplicitWire))); Dump(controller, ExplicitWire, 12);
        Vec(Key(controller, 0)->In, -1, -2, -3); Vec(Key(controller, 0)->Out, 4, 5, 6);
    }
}

TEST_F(BezierStorageTest, OptionalWirePresenceUsesTheTangentBitEvenInCombinedModes) {
    CKDWORD combined[12]; std::memcpy(combined, ExplicitWire, sizeof(combined)); combined[5] = 0x00220022;
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); ASSERT_EQ(48, controller.ReadKeysFrom(combined)); Dump(controller, combined, 12);
        Vec(Key(controller, 0)->In, -1, -2, -3); Vec(Key(controller, 0)->Out, 4, 5, 6);
    }
}

TEST_F(BezierStorageTest, RawReadOwnsUnalignedVariableRecordsAndLeavesTheFollowingBytes) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); Fill(controller); Sample(controller);
        unsigned char source[sizeof(MixedWire) + 2]; std::memset(source, 0xa5, sizeof(source)); std::memcpy(source + 1, MixedWire, sizeof(MixedWire));
        ASSERT_EQ(68, controller.ReadKeysFrom(source + 1)); EXPECT_FALSE(Computed(kind)); EXPECT_FLOAT_EQ(37, controller.GetLength());
        EXPECT_EQ(0xa5, source[sizeof(MixedWire) + 1]); std::memset(source, 0, sizeof(source)); Dump(controller, MixedWire, 17);
    }
}

TEST_F(BezierStorageTest, UnalignedDumpWritesExactlyItsQueriedVariableSize) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); ASSERT_EQ(68, controller.ReadKeysFrom(const_cast<CKDWORD *>(MixedWire)));
        unsigned char bytes[sizeof(MixedWire) + 2]; std::memset(bytes, 0xa5, sizeof(bytes));
        ASSERT_EQ(68, controller.DumpKeysTo(bytes + 1)); EXPECT_EQ(0xa5, bytes[0]); EXPECT_EQ(0xa5, bytes[sizeof(bytes) - 1]);
        EXPECT_EQ(0, std::memcmp(bytes + 1, MixedWire, sizeof(MixedWire)));
    }
}

TEST_F(BezierStorageTest, EmptyReadReleasesKeysAndInvalidatesComputedStateWithoutChangingLength) {
    const CKDWORD empty[] = { 0, 0xdeadbeef };
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); Fill(controller); Sample(controller);
        ASSERT_EQ(4, controller.ReadKeysFrom(const_cast<CKDWORD *>(empty))); EXPECT_FALSE(Computed(kind)); EXPECT_EQ(0, controller.GetKeyCount());
        EXPECT_EQ(nullptr, controller.GetKey(0)); EXPECT_FLOAT_EQ(37, controller.GetLength()); Dump(controller, empty, 1);
    }
}

TEST_F(BezierStorageTest, RejectedCountOrTimeRetainsTheAlreadyComputedController) {
    for (int kind = 0; kind < 2; ++kind) {
        CKAnimController &controller = Controller(kind); Fill(controller); Sample(controller); CKKey *first = controller.GetKey(0);
        const CKDWORD negative[] = { 0xffffffff }, overflow[] = { 0x7fffffff };
        EXPECT_EQ(0, controller.ReadKeysFrom(nullptr)); EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(negative)));
        EXPECT_EQ(0, controller.ReadKeysFrom(const_cast<CKDWORD *>(overflow)));
        CKDWORD bad[11]; std::memcpy(bad, LinearWire, sizeof(bad)); bad[6] = 0x3f800000; EXPECT_EQ(0, controller.ReadKeysFrom(bad));
        bad[6] = 0x7fc00001; EXPECT_EQ(0, controller.ReadKeysFrom(bad)); EXPECT_EQ(first, controller.GetKey(0)); EXPECT_TRUE(Computed(kind));
        Dump(controller, LinearWire, 11); Sample(controller);
    }
}

TEST_F(BezierStorageTest, NonTimePayloadBitsArePreservedAndInvalidDumpIsAtomic) {
    for (int kind = 0; kind < 2; ++kind) {
        RCKObjectAnimation animation(Context); CKAnimController *controller = animation.CreateController(Type(kind)); ASSERT_NE(nullptr, controller);
        CKDWORD payload[12]; std::memcpy(payload, ExplicitWire, sizeof(payload)); payload[2] = 0x80000000; payload[3] = 0x7fc12345; payload[6] = 0x7f800000;
        ASSERT_EQ(48, controller->ReadKeysFrom(payload)); Dump(*controller, payload, 12); SaveMatches(animation, Type(kind), payload, 12);
        const CKDWORD invalidTime = 0x7fc00001; std::memcpy(&Key(*controller, 0)->TimeStep, &invalidTime, 4);
        CKDWORD output[12]; for (int i = 0; i < 12; ++i) output[i] = 0xa5a5a5a5;
        EXPECT_EQ(0, controller->DumpKeysTo(nullptr)); EXPECT_EQ(0, controller->DumpKeysTo(output));
        for (int i = 0; i < 12; ++i) EXPECT_EQ(0xa5a5a5a5u, output[i]);
        CKStateChunk *saved = animation.Save(nullptr, CK_STATESAVE_OBJANIMALL); EXPECT_EQ(nullptr, saved); if (saved) DeleteCKStateChunk(saved);
    }
}

TEST_F(BezierStorageTest, BoundedAnimationLoadAndSavePreserveLiteralOptionalRecords) {
    for (int kind = 0; kind < 2; ++kind) {
        RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
        Begin(chunk); Write(chunk, Type(kind), MixedWire, 17); End(chunk); ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr));
        CKAnimController *controller = kind == 0 ? animation.GetPositionController() : animation.GetScaleController(); ASSERT_NE(nullptr, controller);
        Dump(*controller, MixedWire, 17); SaveMatches(animation, Type(kind), MixedWire, 17);
    }
}

TEST_F(BezierStorageTest, BoundedAnimationLoadCannotReadOptionalTangentsFromTheNextIdentifier) {
    for (int kind = 0; kind < 2; ++kind) {
        RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
        Begin(chunk); Write(chunk, Type(kind), ExplicitWire, 9); chunk.WriteDword(0); chunk.WriteIdentifier(0x76543210);
        for (int i = 0; i < 32; ++i) chunk.WriteDword(0x42424242); chunk.CloseChunk(); chunk.StartRead();
        EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
        EXPECT_EQ(nullptr, kind == 0 ? animation.GetPositionController() : animation.GetScaleController());
        chunk.StartRead(); ASSERT_TRUE(chunk.SeekIdentifier(0x76543210)); EXPECT_EQ(0x42424242u, chunk.ReadDword());
    }
}

TEST_F(BezierStorageTest, BoundedAnimationLoadRejectsAbsentCountsAndTruncatedPrefixes) {
    for (int kind = 0; kind < 2; ++kind) {
        for (int words = 0; words < 6; words += 5) {
            RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
            Begin(chunk); Write(chunk, Type(kind), ExplicitWire, words); End(chunk);
            EXPECT_EQ(CKERR_INVALIDFILE, animation.Load(&chunk, nullptr));
            EXPECT_EQ(nullptr, kind == 0 ? animation.GetPositionController() : animation.GetScaleController());
        }
    }
}

TEST_F(BezierStorageTest, BoundedAnimationLoadSkipsDeclaredPaddingBeforeAnotherController) {
    for (int kind = 0; kind < 2; ++kind) {
        RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
        CKDWORD padded[13]; std::memcpy(padded, LinearWire, sizeof(LinearWire)); padded[11] = 0xdeadbeef; padded[12] = 0xbaadf00d;
        const CKDWORD rotation[] = { 1, 0x40000000, 0, 0, 0, 0x3f800000 };
        Begin(chunk); Write(chunk, Type(kind), padded, 13); Write(chunk, CKANIMATION_LINROT_CONTROL, rotation, 6); End(chunk);
        ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr)); ASSERT_NE(nullptr, animation.GetRotationController());
        CKAnimController *controller = kind == 0 ? animation.GetPositionController() : animation.GetScaleController(); ASSERT_NE(nullptr, controller); Sample(*controller);
        VxQuaternion orientation; ASSERT_TRUE(animation.EvaluateRotation(2, orientation)); EXPECT_FLOAT_EQ(1, orientation.w);
        SaveMatches(animation, Type(kind), LinearWire, 11);
    }
}

TEST_F(BezierStorageTest, LiteralPositionAndSignedScaleReachTheActualEntityMatrixTogether) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context); CKStateChunk chunk(CKCID_OBJECTANIMATION, nullptr);
    const CKDWORD scale[] = { 2, 0x40000000, 0xc0000000, 0, 0x40400000, 0x00020002,
                                0x41200000, 0xc1200000, 0, 0x41300000, 0x00020002 };
    Begin(chunk); Write(chunk, CKANIMATION_BEZIERPOS_CONTROL, LinearWire, 11); Write(chunk, CKANIMATION_BEZIERSCL_CONTROL, scale, 11); End(chunk);
    ASSERT_EQ(CK_OK, animation.Load(&chunk, nullptr)); animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(12);
    ASSERT_EQ(CK_OK, animation.SetStep(.5f)); const VxMatrix &matrix = entity.GetLocalMatrix();
    Vec(matrix[3], 6, 12, 18); Vec(VxVector(matrix[0][0], matrix[1][1], matrix[2][2]), -6, 0, 7);
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) if (row != col) EXPECT_FLOAT_EQ(0, matrix[row][col]);
    SaveMatches(animation, CKANIMATION_BEZIERPOS_CONTROL, LinearWire, 11); SaveMatches(animation, CKANIMATION_BEZIERSCL_CONTROL, scale, 11);
    animation.Set3dEntity(nullptr);
}
} // namespace
