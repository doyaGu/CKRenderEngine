#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCurve.h"
#include "RCKCurvePoint.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializedCurvePoint : public RCKCurvePoint {
public:
    explicit SerializedCurvePoint(CKContext *context) : RCKCurvePoint(context) {}
    void Seed(CKCurve *owner = nullptr) {
        m_Curve = owner; m_UseTCB = 7; m_Linear = -3;
        m_Tension = .125f; m_Continuity = -.25f; m_Bias = .375f; m_Length = 97;
        m_ReservedVector = VxVector(41, 43, 47); m_TangentIn = VxVector(2, 3, 5);
        m_m_TangentOut = VxVector(7, 11, 13); m_NotUsedVector = VxVector(53, 59, 61);
    }
    CKBOOL RawTCB() const { return m_UseTCB; }
    CKBOOL RawLinear() const { return m_Linear; }
};
class DerivedSerializedCurvePoint : public SerializedCurvePoint {
public:
    explicit DerivedSerializedCurvePoint(CKContext *context) : SerializedCurvePoint(context) {}
    CK_CLASSID GetClassID() override { return CKCID_CAMERA; }
};
class CurvePointSerializationTest : public ::testing::Test {
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
    static CKCurve *Curve(RCKCurve &curve) { return reinterpret_cast<CKCurve *>(&curve); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static void Vector(const VxVector &value, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x, value.x); EXPECT_FLOAT_EQ(y, value.y); EXPECT_FLOAT_EQ(z, value.z);
    }
    static void Begin(CKStateChunk &chunk, int version) { chunk.StartWrite(); chunk.SetDataVersion(version); }
    static void Main(CKStateChunk &chunk, int owner, int rawTCB = 0, int linear = -5) {
        chunk.WriteIdentifier(0x10000000u); chunk.WriteInt(owner); chunk.WriteInt(rawTCB); chunk.WriteInt(linear);
        chunk.WriteFloat(.625f); chunk.WriteFloat(-.75f); chunk.WriteFloat(.875f);
        chunk.WriteVector(VxVector(17, 19, 23)); chunk.WriteVector(VxVector(29, 31, 37));
    }
    static CKERROR Load(SerializedCurvePoint &point, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return point.Load(&chunk, file);
    }
    static void SeedTail(SerializedCurvePoint &point) {
        EXPECT_FLOAT_EQ(.125f, point.GetTension());
        EXPECT_FLOAT_EQ(-.25f, point.GetContinuity());
        EXPECT_FLOAT_EQ(.375f, point.GetBias());
    }
    static void Cache(SerializedCurvePoint &point) {
        VxVector value; EXPECT_FLOAT_EQ(97, point.GetLength()); point.GetReservedVector(&value); Vector(value, 41, 43, 47);
        point.GetFittedVector(&value); Vector(value, 53, 59, 61);
    }
};
}

TEST_F(CurvePointSerializationTest, NullLoadPreservesTailTransformOwnerAndValidity) {
    RCKCurve owner(Context); SerializedCurvePoint point(Context); point.Seed(Curve(owner));
    VxVector pos(7, 11, 13); point.SetPosition(&pos); owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, point.Load(nullptr, nullptr)); EXPECT_EQ(Curve(owner), point.GetCurve());
    EXPECT_EQ(7, point.RawTCB()); EXPECT_EQ(-3, point.RawLinear()); SeedTail(point); Cache(point);
    point.GetPosition(&pos); Vector(pos, 7, 11, 13); EXPECT_TRUE(owner.GetObjectFlags() & CK_OBJECT_UPTODATE);
}
TEST_F(CurvePointSerializationTest, MissingIdentifiersPreserveAllOwnFieldsAcrossVersionBoundary) {
    RCKCurve owner(Context); SerializedCurvePoint point(Context); point.Seed(Curve(owner));
    for (int version : {0, 4, 5, 6, 8}) {
        CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, version); chunk.WriteIdentifier(0x12345678); chunk.WriteInt(41);
        owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); ASSERT_EQ(CK_OK, Load(point, chunk));
        EXPECT_EQ(Curve(owner), point.GetCurve()); EXPECT_EQ(7, point.RawTCB()); EXPECT_EQ(-3, point.RawLinear());
        SeedTail(point); Cache(point); EXPECT_TRUE(owner.GetObjectFlags() & CK_OBJECT_UPTODATE);
    }
}
TEST_F(CurvePointSerializationTest, CurrentLiteralWireLoadsOrderedTailAndInverseTCBFlag) {
    RCKCurve owner(Context); SerializedCurvePoint point(Context); point.Seed();
    for (int version : {5, 6, 8}) {
        CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, version); Main(chunk, owner.GetID());
        ASSERT_EQ(CK_OK, Load(point, chunk)); EXPECT_EQ(Curve(owner), point.GetCurve()); EXPECT_TRUE(point.IsTCB());
        EXPECT_EQ(0, point.RawTCB()); EXPECT_EQ(-5, point.IsLinear()); EXPECT_FLOAT_EQ(.625f, point.GetTension());
        EXPECT_FLOAT_EQ(-.75f, point.GetContinuity()); EXPECT_FLOAT_EQ(.875f, point.GetBias());
        VxVector in, out; point.GetTangents(&in, &out); Vector(in, 17, 19, 23); Vector(out, 29, 31, 37); Cache(point);
    }
}
TEST_F(CurvePointSerializationTest, CurrentNonzeroRawTCBMeansExplicitTangentsWithoutNormalizingWire) {
    SerializedCurvePoint point(Context);
    for (int raw : {1, -3, 7}) {
        CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, 6); Main(chunk, 0, raw);
        ASSERT_EQ(CK_OK, Load(point, chunk)); EXPECT_FALSE(point.IsTCB()); EXPECT_EQ(raw, point.RawTCB());
    }
}
TEST_F(CurvePointSerializationTest, CurrentLoadAssignsOwnerWithoutReparentingOrChangingWorldPosition) {
    RCKCurve owner(Context); RCK3dEntity parent(Context); SerializedCurvePoint point(Context); point.Seed();
    ASSERT_TRUE(point.SetParent(Entity(parent), TRUE)); VxVector pos(7, 11, 13); point.SetPosition(&pos);
    owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, 6);
    // Native Entity Load requires an explicit parent and world matrix payload.
    chunk.WriteIdentifier(0x00100000u); chunk.WriteDword(0x00020000u); chunk.WriteDword(VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0)); chunk.WriteVector(VxVector(0, 0, 1));
    chunk.WriteVector(VxVector(7, 11, 13)); chunk.WriteInt(parent.GetID()); Main(chunk, owner.GetID());
    ASSERT_EQ(CK_OK, Load(point, chunk)); EXPECT_EQ(Curve(owner), point.GetCurve()); EXPECT_EQ(Entity(parent), point.GetParent());
    point.GetPosition(&pos); Vector(pos, 7, 11, 13);
    point.SetCurve(nullptr); point.SetParent(nullptr, TRUE);
}
TEST_F(CurvePointSerializationTest, CurrentStandaloneLoadRefreshesActualOwningCurveLengthAndHermiteSample) {
    RCKCurve owner(Context); SerializedCurvePoint point(Context); RCKCurvePoint end(Context);
    VxVector startPos(0, 0, 0), endPos(10, 0, 0), zero(0, 0, 0);
    point.SetPosition(&startPos); end.SetPosition(&endPos); point.SetLinear(TRUE); point.UseTCB(FALSE); end.UseTCB(FALSE);
    point.SetTangents(&zero, &zero); end.SetTangents(&zero, &zero);
    ASSERT_EQ(CK_OK, owner.AddControlPoint(reinterpret_cast<CKCurvePoint *>(&point)));
    ASSERT_EQ(CK_OK, owner.AddControlPoint(reinterpret_cast<CKCurvePoint *>(&end)));
    point.SetCurve(nullptr); point.ModifyMoveableFlags(VX_MOVEABLE_WORLDALIGNED, 0);
    EXPECT_FLOAT_EQ(10, owner.GetLength()); VxVector sample; ASSERT_EQ(CK_OK, owner.GetLocalPos(.5f, &sample)); Vector(sample, 5, 0, 0);
    CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, 6); chunk.WriteIdentifier(0x10000000u);
    chunk.WriteInt(owner.GetID()); chunk.WriteInt(1); chunk.WriteInt(0);
    chunk.WriteFloat(0); chunk.WriteFloat(0); chunk.WriteFloat(0); chunk.WriteVector(zero); chunk.WriteVector(VxVector(0, 20, 0));
    ASSERT_EQ(CK_OK, Load(point, chunk)); EXPECT_EQ(Curve(owner), point.GetCurve()); EXPECT_GT(owner.GetLength(), 10);
    ASSERT_EQ(CK_OK, owner.GetLocalPos(.5f, &sample)); Vector(sample, 5, 2.5f, 0);
    owner.RemoveAllControlPoints(); point.SetCurve(nullptr); end.SetCurve(nullptr); point.SetParent(nullptr, TRUE); end.SetParent(nullptr, TRUE);
}
TEST_F(CurvePointSerializationTest, CurrentIgnoresLegacySeparateSections) {
    SerializedCurvePoint point(Context); point.Seed(); CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, 5);
    chunk.WriteIdentifier(0x20000000u); chunk.WriteFloat(1); chunk.WriteFloat(2); chunk.WriteFloat(3);
    chunk.WriteIdentifier(0x80000000u); chunk.WriteVector(VxVector(71, 73, 79));
    chunk.WriteIdentifier(0x40000000u); chunk.WriteVector(VxVector(83, 89, 97)); chunk.WriteVector(VxVector(101, 103, 107));
    ASSERT_EQ(CK_OK, Load(point, chunk)); SeedTail(point); Cache(point); VxVector in, out;
    point.GetTangents(&in, &out); Vector(in, 2, 3, 5); Vector(out, 7, 11, 13);
}
TEST_F(CurvePointSerializationTest, LegacyLiteralDefaultSetsWorldPositionAndInverseFlagWithExistingParent) {
    RCKCurve owner(Context); RCK3dEntity parent(Context); SerializedCurvePoint point(Context); point.Seed();
    VxVector parentPos(100, 200, 300); parent.SetPosition(&parentPos); point.SetParent(Entity(parent), TRUE);
    for (int version : {0, 4}) {
        CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, version);
        // Without this ancestor section Entity Load intentionally detaches the parent.
        chunk.WriteIdentifier(0x00008000u); chunk.WriteInt(parent.GetID());
        chunk.WriteIdentifier(0x10000000u); chunk.WriteInt(owner.GetID()); chunk.WriteInt(0); chunk.WriteInt(-5);
        chunk.WriteFloat(7); chunk.WriteFloat(11); chunk.WriteFloat(13); owner.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
        ASSERT_EQ(CK_OK, Load(point, chunk)); EXPECT_EQ(Curve(owner), point.GetCurve()); EXPECT_TRUE(point.IsTCB());
        EXPECT_EQ(-5, point.IsLinear()); EXPECT_EQ(Entity(parent), point.GetParent()); VxVector pos;
        point.GetPosition(&pos); Vector(pos, 7, 11, 13); point.GetPosition(&pos, Entity(parent)); Vector(pos, -93, -189, -287);
        SeedTail(point); Cache(point); EXPECT_FALSE(owner.GetObjectFlags() & CK_OBJECT_UPTODATE);
    }
    point.SetCurve(nullptr); point.SetParent(nullptr, TRUE);
}
TEST_F(CurvePointSerializationTest, LegacySeparateSectionsAreIndependentAndPhysicalOrderDoesNotMatter) {
    SerializedCurvePoint point(Context); point.Seed(); CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, 4);
    chunk.WriteIdentifier(0x40000000u); chunk.WriteVector(VxVector(17, 19, 23)); chunk.WriteVector(VxVector(29, 31, 37));
    chunk.WriteIdentifier(0x80000000u); chunk.WriteVector(VxVector(71, 73, 79));
    chunk.WriteIdentifier(0x20000000u); chunk.WriteFloat(.625f); chunk.WriteFloat(-.75f); chunk.WriteFloat(.875f);
    ASSERT_EQ(CK_OK, Load(point, chunk)); EXPECT_EQ(7, point.RawTCB()); EXPECT_EQ(-3, point.RawLinear());
    EXPECT_FLOAT_EQ(.625f, point.GetTension()); EXPECT_FLOAT_EQ(-.75f, point.GetContinuity()); EXPECT_FLOAT_EQ(.875f, point.GetBias());
    VxVector in, out, value; point.GetTangents(&in, &out); Vector(in, 17, 19, 23); Vector(out, 29, 31, 37);
    point.GetReservedVector(&value); Vector(value, 71, 73, 79); point.GetFittedVector(&value); Vector(value, 53, 59, 61);
    EXPECT_FLOAT_EQ(97, point.GetLength());
}
TEST_F(CurvePointSerializationTest, LegacySparseTCBSectionPreservesOtherFieldsAndTransform) {
    SerializedCurvePoint point(Context); point.Seed(); VxVector pos(7, 11, 13); point.SetPosition(&pos);
    CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, 4);
    // SetPosition clears WORLDALIGNED, so a missing ancestor matrix would load Identity.
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 7; matrix[3][1] = 11; matrix[3][2] = 13;
    chunk.WriteIdentifier(0x00020000u); chunk.WriteInt(64); chunk.WriteMatrix(matrix);
    chunk.WriteIdentifier(0x20000000u);
    chunk.WriteFloat(.625f); chunk.WriteFloat(-.75f); chunk.WriteFloat(.875f); ASSERT_EQ(CK_OK, Load(point, chunk));
    EXPECT_EQ(7, point.RawTCB()); EXPECT_EQ(-3, point.RawLinear()); Cache(point); point.GetPosition(&pos); Vector(pos, 7, 11, 13);
    VxVector in, out; point.GetTangents(&in, &out); Vector(in, 2, 3, 5); Vector(out, 7, 11, 13);
}
TEST_F(CurvePointSerializationTest, FileLiteralUsesCreatedObjectIndexZeroAndPreservesTailPosition) {
    RCKCurve a(Context), b(Context); SerializedCurvePoint point(Context); point.Seed(); CKFile file(Context); file.m_FileObjects.Resize(2);
    file.m_FileObjects[0].CreatedObject = a.GetID(); file.m_FileObjects[1].CreatedObject = b.GetID();
    for (int index : {0, 1, -1, 99}) {
        CKStateChunk chunk(CKCID_CURVEPOINT, &file); Begin(chunk, 6); Main(chunk, index);
        ASSERT_EQ(CK_OK, Load(point, chunk, &file)); EXPECT_EQ(index == 0 ? Curve(a) : index == 1 ? Curve(b) : nullptr, point.GetCurve());
        EXPECT_FLOAT_EQ(.625f, point.GetTension()); VxVector in; point.GetTangents(&in, nullptr); Vector(in, 17, 19, 23);
    }
}
TEST_F(CurvePointSerializationTest, LegacyFileLiteralUsesCreatedObjectIndexAndWorldPosition) {
    RCKCurve owner(Context); SerializedCurvePoint point(Context); CKFile file(Context); file.m_FileObjects.Resize(1);
    file.m_FileObjects[0].CreatedObject = owner.GetID(); CKStateChunk chunk(CKCID_CURVEPOINT, &file); Begin(chunk, 4);
    chunk.WriteIdentifier(0x10000000u); chunk.WriteInt(0); chunk.WriteInt(1); chunk.WriteInt(0);
    chunk.WriteFloat(7); chunk.WriteFloat(11); chunk.WriteFloat(13); ASSERT_EQ(CK_OK, Load(point, chunk, &file));
    EXPECT_EQ(Curve(owner), point.GetCurve()); EXPECT_FALSE(point.IsTCB()); VxVector pos; point.GetPosition(&pos); Vector(pos, 7, 11, 13);
}
TEST_F(CurvePointSerializationTest, FilelessNullAndUnresolvedOwnerIDsAreNullWithoutShiftingTail) {
    SerializedCurvePoint point(Context);
    for (int id : {0, 0x71234567}) {
        CKStateChunk chunk(CKCID_CURVEPOINT, nullptr); Begin(chunk, 6); Main(chunk, id);
        ASSERT_EQ(CK_OK, Load(point, chunk)); EXPECT_EQ(nullptr, point.GetCurve()); EXPECT_FLOAT_EQ(.875f, point.GetBias());
        VxVector out; point.GetTangents(nullptr, &out); Vector(out, 29, 31, 37);
    }
}
TEST_F(CurvePointSerializationTest, FilelessZeroAndUnrelatedMaskReturnOnlyAncestorChunk) {
    SerializedCurvePoint point(Context);
    for (CKDWORD flags : {0u, 1u, 0x00200000u}) {
        CKStateChunk *chunk = point.Save(nullptr, flags); ASSERT_NE(nullptr, chunk); EXPECT_NE(CKCID_CURVEPOINT, chunk->GetChunkClassID());
        chunk->StartRead(); EXPECT_FALSE(chunk->SeekIdentifier(0x10000000u)); DeleteCKStateChunk(chunk);
    }
}
TEST_F(CurvePointSerializationTest, EveryCurveMaskBitWritesLiteralOrderedMainAndOmitsCachedFields) {
    RCKCurve owner(Context); SerializedCurvePoint point(Context); point.Seed(Curve(owner));
    for (int bit = 22; bit < 32; ++bit) {
        CKStateChunk *chunk = point.Save(nullptr, CKDWORD(1) << bit); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_CURVEPOINT, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos()); chunk->StartRead();
        ASSERT_TRUE(chunk->SeekIdentifier(0x10000000u)); EXPECT_EQ(owner.GetID(), chunk->ReadInt()); EXPECT_EQ(7, chunk->ReadInt());
        EXPECT_EQ(-3, chunk->ReadInt()); EXPECT_FLOAT_EQ(.125f, chunk->ReadFloat()); EXPECT_FLOAT_EQ(-.25f, chunk->ReadFloat());
        EXPECT_FLOAT_EQ(.375f, chunk->ReadFloat()); VxVector in, out; chunk->ReadVector(&in); chunk->ReadVector(&out);
        Vector(in, 2, 3, 5); Vector(out, 7, 11, 13); EXPECT_EQ(chunk->GetDataSize(), 4 * chunk->GetCurrentPos());
        EXPECT_FALSE(chunk->SeekIdentifier(0x20000000u)); EXPECT_FALSE(chunk->SeekIdentifier(0x40000000u));
        EXPECT_FALSE(chunk->SeekIdentifier(0x80000000u)); DeleteCKStateChunk(chunk);
    }
}
TEST_F(CurvePointSerializationTest, PublicTCBModesSaveNativeInverseWireFlag) {
    SerializedCurvePoint point(Context);
    for (CKBOOL use : {TRUE, FALSE, -3}) {
        point.UseTCB(use); CKStateChunk *chunk = point.Save(nullptr, CK_STATESAVE_CURVEONLY); ASSERT_NE(nullptr, chunk);
        chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x10000000u)); EXPECT_EQ(0, chunk->ReadInt());
        EXPECT_EQ(use == 0 ? 1 : 0, chunk->ReadInt()); DeleteCKStateChunk(chunk);
    }
}
TEST_F(CurvePointSerializationTest, FileSaveZeroMaskWritesRegisteredOwnerIndexAndInheritedSection) {
    RCKCurve first(Context), owner(Context); SerializedCurvePoint point(Context); point.Seed(Curve(owner));
    CKFile file(Context); file.m_IndexByClassId.Resize(CKGetClassCount()); file.SaveObject(&first, 0); file.SaveObject(&owner, 0);
    CKStateChunk *chunk = point.Save(&file, 0); ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_CURVEPOINT, chunk->GetChunkClassID());
    chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x10000000u)); EXPECT_EQ(1, chunk->ReadInt()); EXPECT_EQ(7, chunk->ReadInt());
    EXPECT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA)); DeleteCKStateChunk(chunk);
}
TEST_F(CurvePointSerializationTest, FileSaveUnregisteredOwnerWritesMinusOne) {
    RCKCurve owner(Context); SerializedCurvePoint point(Context); point.Seed(Curve(owner)); CKFile file(Context);
    CKStateChunk *chunk = point.Save(&file, 0); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x10000000u)); EXPECT_EQ(-1, chunk->ReadInt()); EXPECT_EQ(7, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(CurvePointSerializationTest, FilelessDynamicOwnerUsesZeroObjectReferencePolicy) {
    RCKCurve owner(Context); owner.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); SerializedCurvePoint point(Context); point.Seed(Curve(owner));
    CKStateChunk *chunk = point.Save(nullptr, CK_STATESAVE_CURVEONLY); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x10000000u)); EXPECT_EQ(0, chunk->ReadInt()); EXPECT_EQ(7, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(CurvePointSerializationTest, DerivedSaveAllowsAppendingAfterCompleteMainSection) {
    DerivedSerializedCurvePoint point(Context); point.Seed(); CKStateChunk *chunk = point.Save(nullptr, CK_STATESAVE_CURVEONLY); ASSERT_NE(nullptr, chunk);
    EXPECT_GE(chunk->GetCurrentPos(), 0); chunk->WriteIdentifier(0x12345678); chunk->WriteInt(109); chunk->CloseChunk(); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x10000000u)); EXPECT_EQ(0, chunk->ReadInt()); EXPECT_EQ(7, chunk->ReadInt());
    ASSERT_TRUE(chunk->SeekIdentifier(0x12345678)); EXPECT_EQ(109, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
