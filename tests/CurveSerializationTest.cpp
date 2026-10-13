#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializationCurve : public RCKCurve {
public:
    explicit SerializationCurve(CKContext *context) : RCKCurve(context) {}
    void Append(CKObject *point) { m_ControlPoints.PushBack(point); }
    void Seed() { m_Opened = -7; m_FittingCoeff = .625f; m_StepCount = -11; m_Length = 97; m_Color = 0x12345678; }
    CKBOOL Loading() const { return m_Loading; }
    CKBOOL OpenValue() const { return m_Opened; }
    float CachedLength() const { return m_Length; }
    CKDWORD PackedColor() const { return m_Color; }
};
class DerivedSerializationCurve : public SerializationCurve {
public:
    explicit DerivedSerializationCurve(CKContext *context) : SerializationCurve(context) {}
    CK_CLASSID GetClassID() override { return CKCID_CAMERA; }
};
class CurveSerializationPoint : public RCKCurvePoint {
public:
    explicit CurveSerializationPoint(CKContext *context, SerializationCurve *owner = nullptr)
        : RCKCurvePoint(context), Owner(owner), LoadCalls(0), SaveCalls(0), Value(0), Flags(0), File(nullptr), SawLoading(FALSE), SawNull(FALSE) {}
    SerializationCurve *Owner;
    int LoadCalls, SaveCalls, Value;
    CKDWORD Flags;
    CKFile *File;
    CKBOOL SawLoading, SawNull;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override {
        ++SaveCalls; Flags = flags; File = file;
        CKStateChunk *chunk = CreateCKStateChunk(CKCID_CURVEPOINT, nullptr);
        chunk->StartWrite(); chunk->WriteIdentifier(0x76543210); chunk->WriteInt(Value); chunk->CloseChunk();
        return chunk;
    }
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override {
        ++LoadCalls; File = file; SawNull = chunk == nullptr; SawLoading = Owner && Owner->Loading();
        if (chunk) { chunk->StartRead(); if (chunk->SeekIdentifier(0x76543210)) Value = chunk->ReadInt(); }
        if (Owner) Owner->Update(); // The real curve must suppress updates until all point payloads are read.
        return CKERR_INVALIDPARAMETER; // Native Curve intentionally ignores the child Load result.
    }
};
class CurveSerializationFile : public CKFile {
public:
    explicit CurveSerializationFile(CKContext *context) : CKFile(context) {}
    using CKFile::SaveFindObjectIndex;
};
class CurveSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCurvePoint, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCurve, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKERROR Load(SerializationCurve &curve, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return curve.Load(&chunk, file);
    }
    static void Begin(CKStateChunk &chunk, int version = 6) { chunk.StartWrite(); chunk.SetDataVersion(version); }
    static void MainWire(CKStateChunk &chunk, CK_ID a, CK_ID b, float fitting = .375f, int steps = 17, int opened = -3) {
        chunk.WriteIdentifier(0xffc00000u); chunk.WriteInt(2); chunk.WriteInt(a); chunk.WriteInt(b);
        chunk.WriteFloat(fitting); chunk.WriteInt(steps); chunk.WriteInt(opened);
    }
    static CKStateChunk *Payload(int value) {
        CKStateChunk *chunk = CreateCKStateChunk(CKCID_CURVEPOINT, nullptr);
        chunk->StartWrite(); chunk->WriteIdentifier(0x76543210); chunk->WriteInt(value); chunk->CloseChunk(); return chunk;
    }
    static void ChildWire(CKStateChunk &chunk, int wireID, int value) {
        chunk.WriteInt(wireID); CKStateChunk *child = Payload(value); chunk.WriteSubChunk(child); DeleteCKStateChunk(child);
    }
    static void Prepare(CKFile &file) { file.m_IndexByClassId.Resize(CKGetClassCount()); }
    static void ReadMain(CKStateChunk &chunk, int a, int b, float fitting, int steps, int opened) {
        chunk.StartRead(); ASSERT_TRUE(chunk.SeekIdentifier(0xffc00000u)); EXPECT_EQ(2, chunk.ReadInt());
        EXPECT_EQ(a, chunk.ReadInt()); EXPECT_EQ(b, chunk.ReadInt()); EXPECT_FLOAT_EQ(fitting, chunk.ReadFloat());
        EXPECT_EQ(steps, chunk.ReadInt()); EXPECT_EQ(opened, chunk.ReadInt());
    }
};
}

TEST_F(CurveSerializationTest, NullLoadPreservesFieldsControlPointsAndValidity) {
    SerializationCurve curve(Context); RCKCurvePoint point(Context); curve.Seed(); curve.Append(&point);
    curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, curve.Load(nullptr, nullptr)); EXPECT_EQ(1, curve.GetControlPointCount());
    EXPECT_FLOAT_EQ(.625f, curve.GetFittingCoeff()); EXPECT_EQ(-11, curve.GetStepCount()); EXPECT_EQ(-7, curve.OpenValue());
    EXPECT_TRUE(curve.GetObjectFlags() & CK_OBJECT_UPTODATE); EXPECT_FALSE(curve.Loading());
}
TEST_F(CurveSerializationTest, MissingIdentifiersPreserveCurveTailAndInvalidateCachedGeometry) {
    SerializationCurve curve(Context); RCKCurvePoint point(Context); curve.Seed(); curve.Append(&point);
    const int versions[] = {0, 4, 5, 6, 8};
    for (int version : versions) {
        curve.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk, version);
        chunk.WriteIdentifier(0x12345678); chunk.WriteInt(41); ASSERT_EQ(CK_OK, Load(curve, chunk));
        EXPECT_EQ(1, curve.GetControlPointCount()); EXPECT_FLOAT_EQ(.625f, curve.GetFittingCoeff()); EXPECT_EQ(-11, curve.GetStepCount());
        EXPECT_EQ(-7, curve.OpenValue()); EXPECT_FLOAT_EQ(97, curve.CachedLength()); EXPECT_EQ(0x12345678u, curve.PackedColor());
        EXPECT_FALSE(curve.Loading()); EXPECT_FALSE(curve.GetObjectFlags() & CK_OBJECT_UPTODATE);
    }
}
TEST_F(CurveSerializationTest, CurrentLiteralMainWireReplacesArrayAndReadsOrderedTailAcrossVersions) {
    SerializationCurve curve(Context); RCKCurvePoint old(Context), a(Context), b(Context); const int versions[] = {5, 6, 8};
    for (int version : versions) {
        curve.RemoveAllControlPoints(); curve.Append(&old); curve.Seed(); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk, version);
        MainWire(chunk, a.GetID(), b.GetID()); ASSERT_EQ(CK_OK, Load(curve, chunk));
        ASSERT_EQ(2, curve.GetControlPointCount()); EXPECT_EQ(&a, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(0)));
        EXPECT_EQ(&b, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(1))); EXPECT_FLOAT_EQ(.375f, curve.GetFittingCoeff());
        EXPECT_EQ(17, curve.GetStepCount()); EXPECT_EQ(-3, curve.OpenValue()); EXPECT_FLOAT_EQ(97, curve.CachedLength());
        EXPECT_EQ(0x12345678u, curve.PackedColor()); EXPECT_EQ(nullptr, a.GetCurve()); EXPECT_EQ(nullptr, a.GetParent());
    }
}
TEST_F(CurveSerializationTest, CurrentLiteralMainFiltersUnresolvedAndZeroIDsWithoutShiftingTail) {
    SerializationCurve curve(Context); RCKCurvePoint a(Context); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0xffc00000u); chunk.WriteInt(4); chunk.WriteInt(0); chunk.WriteInt(0x71234567);
    chunk.WriteInt(a.GetID()); chunk.WriteInt(0); chunk.WriteFloat(-.25f); chunk.WriteInt(-19); chunk.WriteInt(0);
    ASSERT_EQ(CK_OK, Load(curve, chunk)); ASSERT_EQ(1, curve.GetControlPointCount()); EXPECT_EQ(&a, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(0)));
    EXPECT_FLOAT_EQ(-.25f, curve.GetFittingCoeff()); EXPECT_EQ(-19, curve.GetStepCount()); EXPECT_FALSE(curve.IsOpen());
}
TEST_F(CurveSerializationTest, CurrentZeroCountMainReplacesExistingControlPoints) {
    SerializationCurve curve(Context); RCKCurvePoint a(Context); curve.Append(&a); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0xffc00000u); chunk.WriteInt(0); chunk.WriteFloat(.25f); chunk.WriteInt(23); chunk.WriteInt(1);
    ASSERT_EQ(CK_OK, Load(curve, chunk)); EXPECT_EQ(0, curve.GetControlPointCount()); EXPECT_FLOAT_EQ(.25f, curve.GetFittingCoeff());
    EXPECT_EQ(23, curve.GetStepCount()); EXPECT_TRUE(curve.IsOpen());
}
TEST_F(CurveSerializationTest, LegacySeparateIdentifiersReadIndependentlyOfPhysicalOrder) {
    SerializationCurve curve(Context); RCKCurvePoint a(Context), b(Context); const int versions[] = {0, 4};
    for (int version : versions) {
        curve.Seed(); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk, version);
        chunk.WriteIdentifier(0x02000000); chunk.WriteInt(0); chunk.WriteIdentifier(0x01000000); chunk.WriteInt(31);
        chunk.WriteIdentifier(0x00400000); chunk.WriteFloat(-.5f); chunk.WriteIdentifier(0x00800000);
        chunk.WriteInt(2); chunk.WriteInt(b.GetID()); chunk.WriteInt(a.GetID());
        ASSERT_EQ(CK_OK, Load(curve, chunk)); EXPECT_EQ(2, curve.GetControlPointCount());
        EXPECT_EQ(&b, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(0))); EXPECT_EQ(&a, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(1)));
        EXPECT_FLOAT_EQ(-.5f, curve.GetFittingCoeff()); EXPECT_EQ(31, curve.GetStepCount()); EXPECT_EQ(0, curve.OpenValue());
    }
}
TEST_F(CurveSerializationTest, LegacySparsePropertiesPreserveMissingFieldsAndIgnoreCurrentMain) {
    SerializationCurve curve(Context); RCKCurvePoint a(Context), b(Context); curve.Seed(); curve.Append(&a);
    CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk, 4); MainWire(chunk, b.GetID(), b.GetID());
    chunk.WriteIdentifier(0x01000000); chunk.WriteInt(53); ASSERT_EQ(CK_OK, Load(curve, chunk));
    EXPECT_EQ(1, curve.GetControlPointCount()); EXPECT_EQ(&a, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(0)));
    EXPECT_EQ(53, curve.GetStepCount()); EXPECT_FLOAT_EQ(.625f, curve.GetFittingCoeff()); EXPECT_EQ(-7, curve.OpenValue());
}
TEST_F(CurveSerializationTest, CurrentIgnoresLegacyPropertyIdentifiers) {
    SerializationCurve curve(Context); curve.Seed(); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk, 5);
    chunk.WriteIdentifier(0x00400000); chunk.WriteFloat(.25f); chunk.WriteIdentifier(0x01000000); chunk.WriteInt(53);
    chunk.WriteIdentifier(0x02000000); chunk.WriteInt(0); ASSERT_EQ(CK_OK, Load(curve, chunk));
    EXPECT_FLOAT_EQ(.625f, curve.GetFittingCoeff()); EXPECT_EQ(-11, curve.GetStepCount()); EXPECT_EQ(-7, curve.OpenValue());
}
TEST_F(CurveSerializationTest, LiteralSubchunksDispatchInOrderWhileLoadingAndIgnoreChildErrors) {
    SerializationCurve curve(Context); curve.Seed(); CurveSerializationPoint a(Context, &curve), b(Context, &curve);
    CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk); MainWire(chunk, a.GetID(), b.GetID());
    chunk.WriteIdentifier(0xff000000u); chunk.WriteInt(3); ChildWire(chunk, a.GetID(), 71);
    ChildWire(chunk, 0x71234567, 73); ChildWire(chunk, b.GetID(), 79);
    ASSERT_EQ(CK_OK, Load(curve, chunk)); EXPECT_EQ(1, a.LoadCalls); EXPECT_EQ(1, b.LoadCalls);
    EXPECT_EQ(71, a.Value); EXPECT_EQ(79, b.Value); EXPECT_TRUE(a.SawLoading); EXPECT_TRUE(b.SawLoading);
    EXPECT_EQ(nullptr, a.File); EXPECT_EQ(nullptr, b.File); EXPECT_FALSE(curve.Loading()); EXPECT_FLOAT_EQ(97, curve.CachedLength());
}
TEST_F(CurveSerializationTest, NullSubchunkIsStillDispatchedToExistingPoint) {
    SerializationCurve curve(Context); CurveSerializationPoint a(Context, &curve); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0xff000000u); chunk.WriteInt(1); chunk.WriteInt(a.GetID()); chunk.WriteSubChunk(nullptr);
    ASSERT_EQ(CK_OK, Load(curve, chunk)); EXPECT_EQ(1, a.LoadCalls); EXPECT_TRUE(a.SawNull); EXPECT_TRUE(a.SawLoading);
}
TEST_F(CurveSerializationTest, CurrentFileContextSkipsEmbeddedPointPayloads) {
    SerializationCurve curve(Context); CurveSerializationPoint a(Context, &curve); CKFile file(Context);
    CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk, 6); chunk.WriteIdentifier(0xff000000u); chunk.WriteInt(1);
    ChildWire(chunk, a.GetID(), 83); ASSERT_EQ(CK_OK, Load(curve, chunk, &file)); EXPECT_EQ(0, a.LoadCalls);
}
TEST_F(CurveSerializationTest, LegacyFileContextStillDispatchesEmbeddedPointPayloads) {
    SerializationCurve curve(Context); CurveSerializationPoint a(Context, &curve); CKFile file(Context);
    CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk, 4); chunk.WriteIdentifier(0xff000000u); chunk.WriteInt(1);
    ChildWire(chunk, a.GetID(), 89); ASSERT_EQ(CK_OK, Load(curve, chunk, &file)); EXPECT_EQ(1, a.LoadCalls);
    EXPECT_EQ(89, a.Value); EXPECT_EQ(nullptr, a.File); EXPECT_TRUE(a.SawLoading);
}
TEST_F(CurveSerializationTest, CurrentFileMainUsesCreatedObjectIndicesAndDropsMissingEntries) {
    SerializationCurve curve(Context); RCKCurvePoint a(Context), b(Context); CKFile file(Context); file.m_FileObjects.Resize(2);
    file.m_FileObjects[0].CreatedObject = a.GetID(); file.m_FileObjects[1].CreatedObject = b.GetID();
    CKStateChunk chunk(CKCID_CURVE, &file); Begin(chunk); chunk.WriteIdentifier(0xffc00000u);
    chunk.WriteInt(4); chunk.WriteInt(1); chunk.WriteInt(-1); chunk.WriteInt(0); chunk.WriteInt(99);
    chunk.WriteFloat(.875f); chunk.WriteInt(101); chunk.WriteInt(1); ASSERT_EQ(CK_OK, Load(curve, chunk, &file));
    ASSERT_EQ(2, curve.GetControlPointCount()); EXPECT_EQ(&b, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(0)));
    EXPECT_EQ(&a, reinterpret_cast<RCKCurvePoint *>(curve.GetControlPoint(1))); EXPECT_FLOAT_EQ(.875f, curve.GetFittingCoeff()); EXPECT_EQ(101, curve.GetStepCount());
}
TEST_F(CurveSerializationTest, AncestorWorldMatrixAndCurveMainCoexistInIndependentWire) {
    SerializationCurve curve(Context); RCKCurvePoint a(Context), b(Context); CKStateChunk chunk(CKCID_CURVE, nullptr); Begin(chunk);
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(0); chunk.WriteDword(VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0)); chunk.WriteVector(VxVector(0, 0, 1));
    chunk.WriteVector(VxVector(7, 11, 13)); MainWire(chunk, a.GetID(), b.GetID());
    // Native Entity Load preserves the initial WORLDALIGNED transform for fileless state loads.
    ASSERT_TRUE(curve.GetMoveableFlags() & VX_MOVEABLE_WORLDALIGNED); ASSERT_EQ(CK_OK, Load(curve, chunk));
    EXPECT_FLOAT_EQ(0, curve.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(0, curve.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(0, curve.GetWorldMatrix()[3][2]); EXPECT_EQ(2, curve.GetControlPointCount());
    EXPECT_FLOAT_EQ(.375f, curve.GetFittingCoeff()); EXPECT_EQ(17, curve.GetStepCount());
    CKFile file(Context); ASSERT_EQ(CK_OK, curve.Load(&chunk, &file));
    EXPECT_FLOAT_EQ(7, curve.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(11, curve.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(13, curve.GetWorldMatrix()[3][2]); EXPECT_EQ(2, curve.GetControlPointCount());
}
TEST_F(CurveSerializationTest, FilelessZeroMaskReturnsAncestorWithoutCurveSections) {
    SerializationCurve curve(Context); CKStateChunk *chunk = curve.Save(nullptr, 0); ASSERT_NE(nullptr, chunk);
    EXPECT_NE(CKCID_CURVE, chunk->GetChunkClassID()); chunk->StartRead(); EXPECT_FALSE(chunk->SeekIdentifier(0xffc00000u));
    EXPECT_FALSE(chunk->SeekIdentifier(0xff000000u)); DeleteCKStateChunk(chunk);
}
TEST_F(CurveSerializationTest, EveryCurveMaskBitProducesOrderedMainAndPointPayloads) {
    SerializationCurve curve(Context); CurveSerializationPoint a(Context), b(Context); curve.Seed(); curve.Append(&a); curve.Append(&b);
    a.Value = 103; b.Value = 107;
    for (int bit = 22; bit < 32; ++bit) {
        CKDWORD flags = CKDWORD(1) << bit; CKStateChunk *chunk = curve.Save(nullptr, flags); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_CURVE, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
        ReadMain(*chunk, a.GetID(), b.GetID(), .625f, -11, -7); ASSERT_TRUE(chunk->SeekIdentifier(0xff000000u));
        EXPECT_EQ(2, chunk->ReadInt()); EXPECT_EQ(a.GetID(), chunk->ReadObjectID()); CKStateChunk *first = chunk->ReadSubChunk();
        ASSERT_NE(nullptr, first); first->StartRead(); ASSERT_TRUE(first->SeekIdentifier(0x76543210)); EXPECT_EQ(103, first->ReadInt()); DeleteCKStateChunk(first);
        EXPECT_EQ(b.GetID(), chunk->ReadObjectID()); CKStateChunk *second = chunk->ReadSubChunk(); ASSERT_NE(nullptr, second);
        second->StartRead(); ASSERT_TRUE(second->SeekIdentifier(0x76543210)); EXPECT_EQ(107, second->ReadInt()); DeleteCKStateChunk(second);
        EXPECT_EQ(flags, a.Flags); EXPECT_EQ(flags, b.Flags); EXPECT_EQ(nullptr, a.File); DeleteCKStateChunk(chunk);
    }
    EXPECT_EQ(10, a.SaveCalls); EXPECT_EQ(10, b.SaveCalls);
}
TEST_F(CurveSerializationTest, FilelessNullAndDynamicPointReferencesFollowChunkObjectPolicy) {
    SerializationCurve curve(Context); CurveSerializationPoint dynamic(Context); dynamic.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    curve.Append(nullptr); curve.Append(&dynamic); CKStateChunk *chunk = curve.Save(nullptr, CK_STATESAVE_CURVEONLY); ASSERT_NE(nullptr, chunk);
    ReadMain(*chunk, 0, 0, 0, 100, TRUE); ASSERT_TRUE(chunk->SeekIdentifier(0xff000000u)); EXPECT_EQ(2, chunk->ReadInt());
    EXPECT_EQ(0u, chunk->ReadObjectID()); EXPECT_EQ(nullptr, chunk->ReadSubChunk()); EXPECT_EQ(0u, chunk->ReadObjectID());
    CKStateChunk *child = chunk->ReadSubChunk(); ASSERT_NE(nullptr, child); DeleteCKStateChunk(child); EXPECT_EQ(1, dynamic.SaveCalls); DeleteCKStateChunk(chunk);
}
TEST_F(CurveSerializationTest, FileSaveZeroMaskWritesIndicesAndOmitsEmbeddedPointSaves) {
    SerializationCurve curve(Context); CurveSerializationPoint a(Context), b(Context); curve.Seed(); curve.Append(&a); curve.Append(&b);
    CurveSerializationFile file(Context); Prepare(file); file.SaveObject(&b, 0); file.SaveObject(&a, 0);
    CKStateChunk *chunk = curve.Save(&file, 0); ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_CURVE, chunk->GetChunkClassID());
    ReadMain(*chunk, 1, 0, .625f, -11, -7); EXPECT_FALSE(chunk->SeekIdentifier(0xff000000u));
    EXPECT_EQ(0, a.SaveCalls); EXPECT_EQ(0, b.SaveCalls); EXPECT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA)); DeleteCKStateChunk(chunk);
}
TEST_F(CurveSerializationTest, FileSaveUnregisteredPointReferencesAreMinusOne) {
    SerializationCurve curve(Context); CurveSerializationPoint a(Context), b(Context); curve.Append(&a); curve.Append(&b); CKFile file(Context);
    CKStateChunk *chunk = curve.Save(&file, 0); ASSERT_NE(nullptr, chunk); ReadMain(*chunk, -1, -1, 0, 100, TRUE); DeleteCKStateChunk(chunk);
}
TEST_F(CurveSerializationTest, DerivedSaveAllowsAppendingAfterCurveMainAndPointSections) {
    DerivedSerializationCurve curve(Context); CurveSerializationPoint a(Context), b(Context); curve.Append(&a); curve.Append(&b);
    CKStateChunk *chunk = curve.Save(nullptr, CK_STATESAVE_CURVEONLY); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_CURVE, chunk->GetChunkClassID()); EXPECT_GE(chunk->GetCurrentPos(), 0);
    chunk->WriteIdentifier(0x12345678); chunk->WriteInt(109); chunk->CloseChunk(); ReadMain(*chunk, a.GetID(), b.GetID(), 0, 100, TRUE);
    ASSERT_TRUE(chunk->SeekIdentifier(0x12345678)); EXPECT_EQ(109, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(CurveSerializationTest, PreSaveAlwaysRegistersAllPointsWithAllFlagsAndDeduplicates) {
    SerializationCurve curve(Context); RCKCurvePoint a(Context), b(Context); curve.Append(&a); curve.Append(&b); CKFile file(Context); Prepare(file);
    curve.PreSave(&file, 0); curve.PreSave(&file, CK_STATESAVE_CURVEFITCOEFF); ASSERT_EQ(2, file.m_FileObjects.Size());
    EXPECT_EQ(a.GetID(), file.m_FileObjects[0].Object); EXPECT_EQ(b.GetID(), file.m_FileObjects[1].Object);
    EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[0].SaveFlags); EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[1].SaveFlags);
}
TEST_F(CurveSerializationTest, PreSaveUsesRealFileFilteringForNullDynamicAndExcludedPoints) {
    SerializationCurve curve(Context); RCKCurvePoint ordinary(Context), dynamic(Context), excluded(Context);
    dynamic.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); excluded.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    curve.Append(nullptr); curve.Append(&dynamic); curve.Append(&ordinary); curve.Append(&excluded); CKFile file(Context); Prepare(file);
    curve.PreSave(&file, 0); ASSERT_EQ(1, file.m_FileObjects.Size()); EXPECT_EQ(ordinary.GetID(), file.m_FileObjects[0].Object);
    EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[0].SaveFlags);
}
TEST_F(CurveSerializationTest, PreSaveRegistersInheritedHierarchyBeforeControlPoints) {
    SerializationCurve curve(Context); RCK3dEntity child(Context); RCKCurvePoint a(Context);
    child.SetParent(reinterpret_cast<CK3dEntity *>(&curve), FALSE); curve.Append(&a); CKFile file(Context); Prepare(file);
    curve.PreSave(&file, CK_STATESAVE_3DENTITYHIERARCHY); ASSERT_EQ(2, file.m_FileObjects.Size());
    EXPECT_EQ(child.GetID(), file.m_FileObjects[0].Object); EXPECT_EQ(a.GetID(), file.m_FileObjects[1].Object);
    // Entity PreSave itself schedules hierarchy children with ALL, then Curve schedules points with ALL.
    EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[0].SaveFlags); EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[1].SaveFlags);
    child.SetParent(nullptr, FALSE);
}
