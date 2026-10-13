#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKTargetLight.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TargetLightFile : public CKFile {
public:
    explicit TargetLightFile(CKContext *context) : CKFile(context) {}
    using CKFile::SaveFindObjectIndex;
};

class SerializationTargetLight : public RCKTargetLight {
public:
    explicit SerializationTargetLight(CKContext *context) : RCKTargetLight(context) {}
    CK_ID RawTarget() const { return m_Target; }
    void SetRawTarget(CK_ID id) { m_Target = id; }
    CKDWORD RawLightFlags() const { return m_Flags; }
    void SetRawLightFlags(CKDWORD value) { m_Flags = value; }
};

class DerivedSerializationTargetLight : public SerializationTargetLight {
public:
    explicit DerivedSerializationTargetLight(CKContext *context) : SerializationTargetLight(context) {}
    CK_CLASSID GetClassID() override { return CKCID_CAMERA; }
};

class TargetLightSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetLight, CKCID_LIGHT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Prepare(CKFile &file) {
        // StartSave normally allocates these buckets. The collector below is the real CKFile.
        file.m_IndexByClassId.Resize(CKGetClassCount());
    }
    static void TargetWire(CKStateChunk &chunk, int value) {
        chunk.WriteIdentifier(0x80000000u); chunk.WriteInt(value);
    }
    static CKERROR Load(SerializationTargetLight &light, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return light.Load(&chunk, file);
    }
    static int SavedTarget(CKStateChunk &chunk) {
        chunk.StartRead(); EXPECT_TRUE(chunk.SeekIdentifier(0x80000000u)); return chunk.ReadInt();
    }
    static void Seed(SerializationTargetLight &light) {
        light.SetType(VX_LIGHTSPOT); light.SetRawLightFlags(0x40000300);
        light.SetColor(VxColor(.25f, .5f, .75f, .125f));
        light.SetConstantAttenuation(2); light.SetLinearAttenuation(3);
        light.SetQuadraticAttenuation(5); light.SetRange(257);
        light.SetFallOff(1.25f); light.SetHotSpot(.625f); light.SetFallOffShape(7);
        light.SetLightPower(-3);
    }
    static void ReadSavedLight(CKStateChunk &chunk) {
        chunk.StartRead(); ASSERT_TRUE(chunk.SeekIdentifier(0x00400000));
        EXPECT_EQ(0x40000302u, chunk.ReadDword()); EXPECT_EQ(0xff3f7fbfu, chunk.ReadDword());
        EXPECT_FLOAT_EQ(2, chunk.ReadFloat()); EXPECT_FLOAT_EQ(3, chunk.ReadFloat());
        EXPECT_FLOAT_EQ(5, chunk.ReadFloat()); EXPECT_FLOAT_EQ(257, chunk.ReadFloat());
        EXPECT_FLOAT_EQ(1.25f, chunk.ReadFloat()); EXPECT_FLOAT_EQ(.625f, chunk.ReadFloat());
        EXPECT_FLOAT_EQ(7, chunk.ReadFloat()); ASSERT_TRUE(chunk.SeekIdentifier(0x00800000));
        EXPECT_FLOAT_EQ(-3, chunk.ReadFloat());
    }
};
}

TEST_F(TargetLightSerializationTest, NullLoadPreservesTargetLightValuesAndObjectFlags) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); Seed(light);
    light.SetTarget(reinterpret_cast<CK3dEntity *>(&target)); light.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    const CKDWORD targetFlags = target.GetFlags();
    EXPECT_EQ(CKERR_INVALIDPARAMETER, light.Load(nullptr, nullptr));
    EXPECT_EQ(target.GetID(), light.RawTarget()); EXPECT_FLOAT_EQ(-3, light.GetLightPower());
    EXPECT_FLOAT_EQ(.125f, light.GetColor().a); EXPECT_TRUE(light.GetObjectFlags() & CK_OBJECT_UPTODATE);
    EXPECT_EQ(targetFlags, target.GetFlags());
}

TEST_F(TargetLightSerializationTest, MissingTargetIdentifierPreservesReferenceAndLightAncestorModificationFlag) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context);
    light.SetTarget(reinterpret_cast<CK3dEntity *>(&target)); light.SetLightPower(-3);
    CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    chunk.WriteIdentifier(0x76543210); chunk.WriteInt(73); light.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(target.GetID(), light.RawTarget());
    EXPECT_FLOAT_EQ(1, light.GetLightPower()); EXPECT_TRUE(light.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(TargetLightSerializationTest, IndependentTargetWireAssignsIDWithoutSetTargetFlagSideEffects) {
    SerializationTargetLight light(Context); RCK3dEntity oldTarget(Context), newTarget(Context);
    light.SetTarget(reinterpret_cast<CK3dEntity *>(&oldTarget));
    oldTarget.SetFlags(CK_3DENTITY_TARGETLIGHT | 0x80000000u);
    newTarget.SetFlags(CK_3DENTITY_FRAME | 0x40000000u);
    CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr); chunk.StartWrite(); TargetWire(chunk, newTarget.GetID());
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(newTarget.GetID(), light.RawTarget());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&newTarget), light.GetTarget());
    EXPECT_EQ(CK_3DENTITY_TARGETLIGHT | 0x80000000u, oldTarget.GetFlags());
    EXPECT_EQ(CK_3DENTITY_FRAME | 0x40000000u, newTarget.GetFlags());
}

TEST_F(TargetLightSerializationTest, IndependentNullWireClearsIDWithoutRewritingOldTargetFlags) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context);
    light.SetTarget(reinterpret_cast<CK3dEntity *>(&target)); const CKDWORD flags = target.GetFlags();
    CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr); chunk.StartWrite(); TargetWire(chunk, 0);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(0u, light.RawTarget());
    EXPECT_EQ(nullptr, light.GetTarget()); EXPECT_EQ(flags, target.GetFlags());
}

TEST_F(TargetLightSerializationTest, UnknownFilelessObjectIDIsRetainedForDeferredResolution) {
    SerializationTargetLight light(Context); CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr);
    chunk.StartWrite(); TargetWire(chunk, 0x71234567); ASSERT_EQ(CK_OK, Load(light, chunk));
    EXPECT_EQ(0x71234567u, light.RawTarget()); EXPECT_EQ(nullptr, light.GetTarget());
}

TEST_F(TargetLightSerializationTest, LoadedReferenceCanResolveAfterTheEntityIsCreated) {
    SerializationTargetLight light(Context); const CK_ID next = light.GetID() + 1;
    ASSERT_EQ(nullptr, Context->GetObject(next)); CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr);
    chunk.StartWrite(); TargetWire(chunk, next); ASSERT_EQ(CK_OK, Load(light, chunk));
    EXPECT_EQ(nullptr, light.GetTarget()); RCK3dEntity target(Context); ASSERT_EQ(next, target.GetID());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&target), light.GetTarget());
}

TEST_F(TargetLightSerializationTest, TargetWireIsIndependentOfLightDataVersionBranch) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); const int versions[] = {0, 4, 5, 6, 8};
    for (int version : versions) {
        light.SetRawTarget(0); CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr); chunk.StartWrite();
        chunk.SetDataVersion(version); TargetWire(chunk, target.GetID()); ASSERT_EQ(CK_OK, Load(light, chunk));
        EXPECT_EQ(target.GetID(), light.RawTarget());
    }
}

TEST_F(TargetLightSerializationTest, FileWireIndexResolvesThroughCreatedObjectTable) {
    SerializationTargetLight light(Context); RCK3dEntity first(Context), selected(Context); CKFile file(Context);
    file.m_FileObjects.Resize(2); file.m_FileObjects[0].CreatedObject = first.GetID();
    file.m_FileObjects[1].CreatedObject = selected.GetID(); CKStateChunk chunk(CKCID_TARGETLIGHT, &file);
    chunk.StartWrite(); TargetWire(chunk, 1); ASSERT_EQ(CK_OK, Load(light, chunk, &file));
    EXPECT_EQ(selected.GetID(), light.RawTarget()); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&selected), light.GetTarget());
}

TEST_F(TargetLightSerializationTest, FileWireZeroIndexMeansFirstObjectRatherThanNull) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); CKFile file(Context);
    file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = target.GetID();
    CKStateChunk chunk(CKCID_TARGETLIGHT, &file); chunk.StartWrite(); TargetWire(chunk, 0);
    ASSERT_EQ(CK_OK, Load(light, chunk, &file)); EXPECT_EQ(target.GetID(), light.RawTarget());
}

TEST_F(TargetLightSerializationTest, MissingOrNegativeFileIndexClearsReference) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); CKFile file(Context);
    file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = target.GetID(); const int indices[] = {-1, 1, 17};
    for (int index : indices) {
        light.SetRawTarget(target.GetID()); CKStateChunk chunk(CKCID_TARGETLIGHT, &file);
        chunk.StartWrite(); TargetWire(chunk, index); ASSERT_EQ(CK_OK, Load(light, chunk, &file));
        EXPECT_EQ(0u, light.RawTarget());
    }
}

TEST_F(TargetLightSerializationTest, LiteralCurrentLightAndEntityAncestorPayloadsLoadAlongsideTarget) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); CKFile file(Context);
    CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(0); chunk.WriteDword(VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0));
    chunk.WriteVector(VxVector(0, 0, 1)); chunk.WriteVector(VxVector(7, 11, 13));
    chunk.WriteIdentifier(0x00400000); chunk.WriteDword(0x80000202); chunk.WriteDword(0x80402010);
    chunk.WriteFloat(2); chunk.WriteFloat(3); chunk.WriteFloat(5); chunk.WriteFloat(257);
    chunk.WriteFloat(1.25f); chunk.WriteFloat(.625f); chunk.WriteFloat(7);
    chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(-2.5f); TargetWire(chunk, target.GetID());
    ASSERT_EQ(CK_OK, Load(light, chunk, &file)); EXPECT_EQ(target.GetID(), light.RawTarget());
    EXPECT_EQ(VX_LIGHTSPOT, light.GetType()); EXPECT_EQ(0x80000200u, light.RawLightFlags());
    EXPECT_FLOAT_EQ(64 / 255.f, light.GetColor().r); EXPECT_FLOAT_EQ(128 / 255.f, light.GetColor().a);
    EXPECT_FLOAT_EQ(2, light.GetConstantAttenuation()); EXPECT_FLOAT_EQ(3, light.GetLinearAttenuation());
    EXPECT_FLOAT_EQ(5, light.GetQuadraticAttenuation()); EXPECT_FLOAT_EQ(257, light.GetRange());
    EXPECT_FLOAT_EQ(1.25f, light.GetFallOff()); EXPECT_FLOAT_EQ(.625f, light.GetHotSpot());
    EXPECT_FLOAT_EQ(7, light.GetFallOffShape()); EXPECT_FLOAT_EQ(-2.5f, light.GetLightPower());
    EXPECT_FLOAT_EQ(7, light.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(11, light.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(13, light.GetWorldMatrix()[3][2]);
}

TEST_F(TargetLightSerializationTest, LiteralLegacyLightPayloadPreservesAlphaAndReadsConesBeforeTarget) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); const int versions[] = {0, 4};
    for (int version : versions) {
        Seed(light); CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        chunk.WriteIdentifier(0x00400000); chunk.WriteDword(1);
        chunk.WriteFloat(.125f); chunk.WriteFloat(.375f); chunk.WriteFloat(.625f); chunk.WriteFloat(.875f);
        chunk.WriteInt(0); chunk.WriteInt(-7); chunk.WriteFloat(11); chunk.WriteFloat(13); chunk.WriteFloat(17);
        chunk.WriteFloat(509); chunk.WriteFloat(1.75f); chunk.WriteFloat(.875f); chunk.WriteFloat(19);
        chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(29); TargetWire(chunk, target.GetID());
        ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(target.GetID(), light.RawTarget());
        EXPECT_EQ(VX_LIGHTPOINT, light.GetType()); EXPECT_EQ(0x40000200u, light.RawLightFlags());
        EXPECT_FLOAT_EQ(.375f, light.GetColor().g); EXPECT_FLOAT_EQ(.125f, light.GetColor().a);
        EXPECT_FLOAT_EQ(11, light.GetConstantAttenuation()); EXPECT_FLOAT_EQ(509, light.GetRange());
        EXPECT_FLOAT_EQ(1.75f, light.GetFallOff()); EXPECT_FLOAT_EQ(.875f, light.GetHotSpot());
        EXPECT_FLOAT_EQ(19, light.GetFallOffShape()); EXPECT_FLOAT_EQ(1, light.GetLightPower());
    }
}

TEST_F(TargetLightSerializationTest, ModernPowerOnlyAndTargetSectionsRemainIndependent) {
    SerializationTargetLight light(Context); Seed(light); RCK3dEntity target(Context);
    CKStateChunk chunk(CKCID_TARGETLIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(8);
    chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(0); TargetWire(chunk, target.GetID());
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_FLOAT_EQ(0, light.GetLightPower());
    EXPECT_EQ(VX_LIGHTSPOT, light.GetType()); EXPECT_EQ(0x40000300u, light.RawLightFlags());
    EXPECT_FLOAT_EQ(.125f, light.GetColor().a); EXPECT_EQ(target.GetID(), light.RawTarget());
}

TEST_F(TargetLightSerializationTest, FilelessZeroMaskDoesNotEmitTargetData) {
    SerializationTargetLight light(Context); CKStateChunk *chunk = light.Save(nullptr, 0); ASSERT_NE(nullptr, chunk);
    EXPECT_NE(CKCID_TARGETLIGHT, chunk->GetChunkClassID()); chunk->StartRead();
    EXPECT_FALSE(chunk->SeekIdentifier(0x80000000u)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetLightSerializationTest, FilelessLightMaskDelegatesWithLightPayloadOnly) {
    SerializationTargetLight light(Context); Seed(light); CKStateChunk *chunk = light.Save(nullptr, CK_STATESAVE_LIGHTONLY);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_LIGHT, chunk->GetChunkClassID()); ReadSavedLight(*chunk);
    EXPECT_FALSE(chunk->SeekIdentifier(0x80000000u)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetLightSerializationTest, EveryTargetOnlyMaskBitIncludingHighestEmitsTargetAndClosesExactClass) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); light.SetRawTarget(target.GetID());
    const CKDWORD masks[] = {0x10000000, 0x20000000, 0x40000000, 0x80000000u};
    for (CKDWORD mask : masks) {
        CKStateChunk *chunk = light.Save(nullptr, mask); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_TARGETLIGHT, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
        EXPECT_EQ(static_cast<int>(target.GetID()), SavedTarget(*chunk));
        EXPECT_FALSE(chunk->SeekIdentifier(0x00400000)); DeleteCKStateChunk(chunk);
    }
}

TEST_F(TargetLightSerializationTest, FilelessTargetSaveWritesObjectIDAndLeavesTargetFlagsUntouched) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); light.SetRawTarget(target.GetID());
    target.SetFlags(CK_3DENTITY_FRAME | 0x80000000u); CKStateChunk *chunk = light.Save(nullptr, 0x80000000u);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(static_cast<int>(target.GetID()), SavedTarget(*chunk));
    EXPECT_EQ(CK_3DENTITY_FRAME | 0x80000000u, target.GetFlags()); DeleteCKStateChunk(chunk);
}

TEST_F(TargetLightSerializationTest, NullOrUnresolvedFilelessTargetIsWrittenAsZero) {
    SerializationTargetLight light(Context); const CK_ID ids[] = {0, 0x71234567};
    for (CK_ID id : ids) {
        light.SetRawTarget(id); CKStateChunk *chunk = light.Save(nullptr, 0x80000000u);
        ASSERT_NE(nullptr, chunk); EXPECT_EQ(0, SavedTarget(*chunk)); DeleteCKStateChunk(chunk);
    }
}

TEST_F(TargetLightSerializationTest, FilelessDynamicTargetIsFilteredByChunkObjectContract) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); target.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    light.SetRawTarget(target.GetID()); CKStateChunk *chunk = light.Save(nullptr, 0x80000000u);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(0, SavedTarget(*chunk)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetLightSerializationTest, FileSaveZeroMaskWritesRegisteredIndexAndCombinedLightPayload) {
    SerializationTargetLight light(Context); Seed(light); RCK3dEntity first(Context), target(Context); TargetLightFile file(Context);
    Prepare(file); file.SaveObject(&first, 0); file.SaveObject(&target, 0); light.SetRawTarget(target.GetID());
    ASSERT_EQ(1, file.SaveFindObjectIndex(target.GetID())); CKStateChunk *chunk = light.Save(&file, 0);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_TARGETLIGHT, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
    EXPECT_EQ(1, SavedTarget(*chunk)); ReadSavedLight(*chunk);
    EXPECT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetLightSerializationTest, FileSaveUnregisteredTargetWritesMinusOneRatherThanRawID) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); CKFile file(Context); light.SetRawTarget(target.GetID());
    CKStateChunk *chunk = light.Save(&file, 0); ASSERT_NE(nullptr, chunk); EXPECT_EQ(-1, SavedTarget(*chunk)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetLightSerializationTest, DerivedSaveLeavesTargetAndLightPayloadWritableForSubclassAppend) {
    DerivedSerializationTargetLight light(Context); Seed(light); RCK3dEntity target(Context); light.SetRawTarget(target.GetID());
    CKStateChunk *chunk = light.Save(nullptr, CK_STATESAVE_LIGHTONLY | CK_STATESAVE_TLIGHTONLY);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_TARGETLIGHT, chunk->GetChunkClassID()); EXPECT_GE(chunk->GetCurrentPos(), 0);
    chunk->WriteIdentifier(0x76543210); chunk->WriteInt(79); chunk->CloseChunk();
    EXPECT_EQ(static_cast<int>(target.GetID()), SavedTarget(*chunk)); ReadSavedLight(*chunk);
    ASSERT_TRUE(chunk->SeekIdentifier(0x76543210)); EXPECT_EQ(79, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}

TEST_F(TargetLightSerializationTest, PreSaveRegistersTargetThroughRealFileAndForwardsSaveMask) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); light.SetRawTarget(target.GetID());
    TargetLightFile file(Context); Prepare(file); light.PreSave(&file, CK_STATESAVE_LIGHTDATA);
    ASSERT_EQ(1, file.m_FileObjects.Size()); EXPECT_EQ(target.GetID(), file.m_FileObjects[0].Object);
    EXPECT_EQ(&target, file.m_FileObjects[0].ObjPtr); EXPECT_EQ(CK_STATESAVE_LIGHTDATA, file.m_FileObjects[0].SaveFlags);
    EXPECT_EQ(0, file.SaveFindObjectIndex(target.GetID()));
}

TEST_F(TargetLightSerializationTest, PreSaveZeroMaskStillRegistersTargetAndDeduplicatesRepeats) {
    SerializationTargetLight light(Context); RCK3dEntity target(Context); light.SetRawTarget(target.GetID());
    CKFile file(Context); Prepare(file); light.PreSave(&file, 0); light.PreSave(&file, 0);
    ASSERT_EQ(1, file.m_FileObjects.Size()); EXPECT_EQ(target.GetID(), file.m_FileObjects[0].Object);
    EXPECT_EQ(0u, file.m_FileObjects[0].SaveFlags);
}

TEST_F(TargetLightSerializationTest, PreSaveIgnoresNullUnresolvedDynamicAndExcludedTargetsUsingFilePolicy) {
    SerializationTargetLight light(Context); RCK3dEntity dynamic(Context), excluded(Context);
    dynamic.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); excluded.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    const CK_ID ids[] = {0, 0x71234567, dynamic.GetID(), excluded.GetID()};
    for (CK_ID id : ids) {
        CKFile file(Context); Prepare(file); light.SetRawTarget(id); light.PreSave(&file, CK_STATESAVE_ALL);
        EXPECT_EQ(0, file.m_FileObjects.Size());
    }
}

TEST_F(TargetLightSerializationTest, PreSaveRegistersAncestorHierarchyBeforeOwnTarget) {
    SerializationTargetLight light(Context); RCK3dEntity child(Context), target(Context);
    child.SetParent(reinterpret_cast<CK3dEntity *>(&light), FALSE); light.SetRawTarget(target.GetID());
    CKFile file(Context); Prepare(file); light.PreSave(&file, CK_STATESAVE_3DENTITYHIERARCHY);
    ASSERT_EQ(2, file.m_FileObjects.Size()); EXPECT_EQ(child.GetID(), file.m_FileObjects[0].Object);
    EXPECT_EQ(target.GetID(), file.m_FileObjects[1].Object);
    EXPECT_EQ(CK_STATESAVE_3DENTITYHIERARCHY, file.m_FileObjects[1].SaveFlags); child.SetParent(nullptr, FALSE);
}
