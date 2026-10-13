#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKTargetCamera.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializationTargetFile : public CKFile {
public:
    explicit SerializationTargetFile(CKContext *context) : CKFile(context) {}
    using CKFile::SaveFindObjectIndex;
};

class SerializationTargetCamera : public RCKTargetCamera {
public:
    explicit SerializationTargetCamera(CKContext *context) : RCKTargetCamera(context) {}
    CK_ID RawTarget() const { return m_Target; }
    void SetRawTarget(CK_ID id) { m_Target = id; }
};

class DerivedSerializationTargetCamera : public SerializationTargetCamera {
public:
    explicit DerivedSerializationTargetCamera(CKContext *context) : SerializationTargetCamera(context) {}
    CK_CLASSID GetClassID() override { return CKCID_LIGHT; }
};

class TargetCameraSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetCamera, CKCID_CAMERA);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Prepare(CKFile &file) {
        // StartSave normally prepares these buckets. Exercise the real CKFile
        // dependency collector without creating an unrelated disk fixture.
        file.m_IndexByClassId.Resize(CKGetClassCount());
    }
    static void TargetWire(CKStateChunk &chunk, int value) {
        chunk.WriteIdentifier(0x10000000);
        chunk.WriteInt(value);
    }
    static CKERROR Load(SerializationTargetCamera &camera, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk();
        return camera.Load(&chunk, file);
    }
    static int SavedTarget(CKStateChunk &chunk) {
        chunk.StartRead();
        EXPECT_TRUE(chunk.SeekIdentifier(0x10000000));
        return chunk.ReadInt();
    }
};
}

TEST_F(TargetCameraSerializationTest, NullLoadPreservesTargetCameraValuesAndObjectFlags) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(&target)); camera.SetFov(.75f);
    camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    const CKDWORD targetFlags = target.GetFlags();
    EXPECT_EQ(CKERR_INVALIDPARAMETER, camera.Load(nullptr, nullptr));
    EXPECT_EQ(target.GetID(), camera.RawTarget()); EXPECT_FLOAT_EQ(.75f, camera.GetFov());
    EXPECT_TRUE(camera.GetObjectFlags() & CK_OBJECT_UPTODATE);
    EXPECT_EQ(targetFlags, target.GetFlags());
}

TEST_F(TargetCameraSerializationTest, MissingTargetIdentifierPreservesReferenceAndClearsAncestorModification) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(&target));
    CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    chunk.WriteIdentifier(0x76543210); chunk.WriteInt(73);
    camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    ASSERT_EQ(CK_OK, Load(camera, chunk));
    EXPECT_EQ(target.GetID(), camera.RawTarget());
    EXPECT_FALSE(camera.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(TargetCameraSerializationTest, IndependentTargetWireAssignsIDWithoutSetTargetFlagSideEffects) {
    SerializationTargetCamera camera(Context); RCK3dEntity oldTarget(Context), newTarget(Context);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(&oldTarget));
    oldTarget.SetFlags(CK_3DENTITY_TARGETCAMERA | 0x80000000u);
    newTarget.SetFlags(CK_3DENTITY_FRAME | 0x40000000u);
    CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite();
    TargetWire(chunk, newTarget.GetID()); ASSERT_EQ(CK_OK, Load(camera, chunk));
    EXPECT_EQ(newTarget.GetID(), camera.RawTarget());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&newTarget), camera.GetTarget());
    EXPECT_EQ(CK_3DENTITY_TARGETCAMERA | 0x80000000u, oldTarget.GetFlags());
    EXPECT_EQ(CK_3DENTITY_FRAME | 0x40000000u, newTarget.GetFlags());
}

TEST_F(TargetCameraSerializationTest, IndependentNullWireClearsIDWithoutRewritingOldTargetFlags) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context);
    camera.SetTarget(reinterpret_cast<CK3dEntity *>(&target)); const CKDWORD flags = target.GetFlags();
    CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite(); TargetWire(chunk, 0);
    ASSERT_EQ(CK_OK, Load(camera, chunk)); EXPECT_EQ(0u, camera.RawTarget());
    EXPECT_EQ(nullptr, camera.GetTarget()); EXPECT_EQ(flags, target.GetFlags());
}

TEST_F(TargetCameraSerializationTest, UnknownFilelessObjectIDIsRetainedForDeferredResolution) {
    SerializationTargetCamera camera(Context);
    CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite(); TargetWire(chunk, 0x71234567);
    ASSERT_EQ(CK_OK, Load(camera, chunk)); EXPECT_EQ(0x71234567u, camera.RawTarget());
    EXPECT_EQ(nullptr, camera.GetTarget());
}

TEST_F(TargetCameraSerializationTest, LoadedReferenceCanResolveAfterTheEntityIsCreated) {
    SerializationTargetCamera camera(Context);
    const CK_ID next = camera.GetID() + 1;
    ASSERT_EQ(nullptr, Context->GetObject(next));
    CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite(); TargetWire(chunk, next);
    ASSERT_EQ(CK_OK, Load(camera, chunk)); EXPECT_EQ(nullptr, camera.GetTarget());
    RCK3dEntity target(Context);
    ASSERT_EQ(next, target.GetID());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&target), camera.GetTarget());
}

TEST_F(TargetCameraSerializationTest, TargetWireIsIndependentOfCameraDataVersionBranch) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context);
    const int versions[] = {0, 4, 5, 6, 8};
    for (int version : versions) {
        camera.SetRawTarget(0);
        CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        TargetWire(chunk, target.GetID()); ASSERT_EQ(CK_OK, Load(camera, chunk));
        EXPECT_EQ(target.GetID(), camera.RawTarget());
    }
}

TEST_F(TargetCameraSerializationTest, FileWireIndexResolvesThroughCreatedObjectTable) {
    SerializationTargetCamera camera(Context); RCK3dEntity first(Context), selected(Context); CKFile file(Context);
    file.m_FileObjects.Resize(2); file.m_FileObjects[0].CreatedObject = first.GetID();
    file.m_FileObjects[1].CreatedObject = selected.GetID();
    CKStateChunk chunk(CKCID_TARGETCAMERA, &file); chunk.StartWrite(); TargetWire(chunk, 1);
    ASSERT_EQ(CK_OK, Load(camera, chunk, &file)); EXPECT_EQ(selected.GetID(), camera.RawTarget());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&selected), camera.GetTarget());
}

TEST_F(TargetCameraSerializationTest, FileWireZeroIndexMeansTheFirstObjectRatherThanNull) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); CKFile file(Context);
    file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = target.GetID();
    CKStateChunk chunk(CKCID_TARGETCAMERA, &file); chunk.StartWrite(); TargetWire(chunk, 0);
    ASSERT_EQ(CK_OK, Load(camera, chunk, &file)); EXPECT_EQ(target.GetID(), camera.RawTarget());
}

TEST_F(TargetCameraSerializationTest, MissingOrNegativeFileIndexClearsTheReference) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); CKFile file(Context);
    file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = target.GetID();
    const int indices[] = {-1, 1, 17};
    for (int index : indices) {
        camera.SetRawTarget(target.GetID());
        CKStateChunk chunk(CKCID_TARGETCAMERA, &file); chunk.StartWrite(); TargetWire(chunk, index);
        ASSERT_EQ(CK_OK, Load(camera, chunk, &file)); EXPECT_EQ(0u, camera.RawTarget());
    }
}

TEST_F(TargetCameraSerializationTest, TargetLoadAlsoAppliesLiteralCameraAndEntityAncestorPayloads) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); CKFile file(Context);
    CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(0); chunk.WriteDword(VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0));
    chunk.WriteVector(VxVector(0, 0, 1)); chunk.WriteVector(VxVector(7, 11, 13));
    chunk.WriteIdentifier(0x0fc00000); chunk.WriteInt(-7); chunk.WriteFloat(.75f); chunk.WriteFloat(17.5f);
    chunk.WriteDword(0x04380780); chunk.WriteFloat(.125f); chunk.WriteFloat(8192);
    TargetWire(chunk, target.GetID()); ASSERT_EQ(CK_OK, Load(camera, chunk, &file));
    EXPECT_EQ(target.GetID(), camera.RawTarget()); EXPECT_EQ(-7, camera.GetProjectionType());
    EXPECT_FLOAT_EQ(.75f, camera.GetFov()); EXPECT_FLOAT_EQ(17.5f, camera.GetOrthographicZoom());
    int width = 0, height = 0; camera.GetAspectRatio(width, height);
    EXPECT_EQ(1920, width); EXPECT_EQ(1080, height);
    EXPECT_FLOAT_EQ(.125f, camera.GetFrontPlane()); EXPECT_FLOAT_EQ(8192, camera.GetBackPlane());
    EXPECT_FLOAT_EQ(7, camera.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(11, camera.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(13, camera.GetWorldMatrix()[3][2]);
}

TEST_F(TargetCameraSerializationTest, FilelessZeroMaskDoesNotEmitTargetData) {
    SerializationTargetCamera camera(Context); CKStateChunk *chunk = camera.Save(nullptr, 0);
    ASSERT_NE(nullptr, chunk); EXPECT_NE(CKCID_TARGETCAMERA, chunk->GetChunkClassID());
    chunk->StartRead(); EXPECT_FALSE(chunk->SeekIdentifier(0x10000000)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetCameraSerializationTest, FilelessCameraMaskDelegatesWithCameraPayloadOnly) {
    SerializationTargetCamera camera(Context); camera.SetFov(.75f);
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_CAMERAONLY); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_CAMERA, chunk->GetChunkClassID()); chunk->StartRead();
    EXPECT_TRUE(chunk->SeekIdentifier(0x0fc00000)); EXPECT_FALSE(chunk->SeekIdentifier(0x10000000));
    DeleteCKStateChunk(chunk);
}

TEST_F(TargetCameraSerializationTest, EveryTargetOnlyMaskBitEmitsTheTargetSectionAndClosesExactClass) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetRawTarget(target.GetID());
    const CKDWORD masks[] = {0x10000000, 0x20000000, 0x40000000};
    for (CKDWORD mask : masks) {
        CKStateChunk *chunk = camera.Save(nullptr, mask); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_TARGETCAMERA, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
        EXPECT_EQ(static_cast<int>(target.GetID()), SavedTarget(*chunk));
        EXPECT_FALSE(chunk->SeekIdentifier(0x0fc00000)); DeleteCKStateChunk(chunk);
    }
}

TEST_F(TargetCameraSerializationTest, FilelessTargetSaveWritesTheObjectIDAndLeavesTargetFlagsUntouched) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetRawTarget(target.GetID());
    target.SetFlags(CK_3DENTITY_FRAME | 0x80000000u);
    CKStateChunk *chunk = camera.Save(nullptr, 0x10000000); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(static_cast<int>(target.GetID()), SavedTarget(*chunk));
    EXPECT_EQ(CK_3DENTITY_FRAME | 0x80000000u, target.GetFlags()); DeleteCKStateChunk(chunk);
}

TEST_F(TargetCameraSerializationTest, NullOrUnresolvedFilelessTargetIsWrittenAsZero) {
    SerializationTargetCamera camera(Context);
    const CK_ID ids[] = {0, 0x71234567};
    for (CK_ID id : ids) {
        camera.SetRawTarget(id); CKStateChunk *chunk = camera.Save(nullptr, 0x10000000);
        ASSERT_NE(nullptr, chunk); EXPECT_EQ(0, SavedTarget(*chunk)); DeleteCKStateChunk(chunk);
    }
}

TEST_F(TargetCameraSerializationTest, FilelessDynamicTargetIsFilteredByChunkObjectContract) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context);
    target.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); camera.SetRawTarget(target.GetID());
    CKStateChunk *chunk = camera.Save(nullptr, 0x10000000); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(0, SavedTarget(*chunk)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetCameraSerializationTest, FileSaveZeroMaskWritesRegisteredIndexAndCombinedCameraPayload) {
    SerializationTargetCamera camera(Context); RCK3dEntity first(Context), target(Context); SerializationTargetFile file(Context);
    Prepare(file); file.SaveObject(&first, 0); file.SaveObject(&target, 0); camera.SetRawTarget(target.GetID());
    ASSERT_EQ(1, file.SaveFindObjectIndex(target.GetID()));
    CKStateChunk *chunk = camera.Save(&file, 0); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_TARGETCAMERA, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
    EXPECT_EQ(1, SavedTarget(*chunk)); EXPECT_TRUE(chunk->SeekIdentifier(0x0fc00000));
    EXPECT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetCameraSerializationTest, FileSaveUnregisteredTargetWritesMinusOneRatherThanRawID) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); CKFile file(Context);
    camera.SetRawTarget(target.GetID()); CKStateChunk *chunk = camera.Save(&file, 0);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(-1, SavedTarget(*chunk)); DeleteCKStateChunk(chunk);
}

TEST_F(TargetCameraSerializationTest, DerivedSaveLeavesTargetAndCameraPayloadWritableForSubclass) {
    DerivedSerializationTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetRawTarget(target.GetID());
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_CAMERAONLY | CK_STATESAVE_TCAMERAONLY);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_TARGETCAMERA, chunk->GetChunkClassID());
    EXPECT_GE(chunk->GetCurrentPos(), 0); chunk->WriteIdentifier(0x76543210); chunk->WriteInt(79);
    chunk->CloseChunk(); EXPECT_EQ(static_cast<int>(target.GetID()), SavedTarget(*chunk));
    EXPECT_TRUE(chunk->SeekIdentifier(0x0fc00000));
    ASSERT_TRUE(chunk->SeekIdentifier(0x76543210)); EXPECT_EQ(79, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}

TEST_F(TargetCameraSerializationTest, PreSaveRegistersTargetThroughRealFileAndForwardsTheSaveMask) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetRawTarget(target.GetID());
    SerializationTargetFile file(Context); Prepare(file); camera.PreSave(&file, CK_STATESAVE_CAMERAFOV);
    ASSERT_EQ(1, file.m_FileObjects.Size()); EXPECT_EQ(target.GetID(), file.m_FileObjects[0].Object);
    EXPECT_EQ(&target, file.m_FileObjects[0].ObjPtr); EXPECT_EQ(CK_STATESAVE_CAMERAFOV, file.m_FileObjects[0].SaveFlags);
    EXPECT_EQ(0, file.SaveFindObjectIndex(target.GetID()));
}

TEST_F(TargetCameraSerializationTest, PreSaveZeroMaskStillRegistersTheTargetAndDeduplicatesRepeats) {
    SerializationTargetCamera camera(Context); RCK3dEntity target(Context); camera.SetRawTarget(target.GetID());
    CKFile file(Context); Prepare(file); camera.PreSave(&file, 0); camera.PreSave(&file, 0);
    ASSERT_EQ(1, file.m_FileObjects.Size()); EXPECT_EQ(target.GetID(), file.m_FileObjects[0].Object);
    EXPECT_EQ(0u, file.m_FileObjects[0].SaveFlags);
}

TEST_F(TargetCameraSerializationTest, PreSaveIgnoresNullUnresolvedDynamicAndExcludedTargetsUsingFilePolicy) {
    SerializationTargetCamera camera(Context); RCK3dEntity dynamic(Context), excluded(Context);
    dynamic.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); excluded.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    const CK_ID ids[] = {0, 0x71234567, dynamic.GetID(), excluded.GetID()};
    for (CK_ID id : ids) {
        CKFile file(Context); Prepare(file); camera.SetRawTarget(id); camera.PreSave(&file, CK_STATESAVE_ALL);
        EXPECT_EQ(0, file.m_FileObjects.Size());
    }
}

TEST_F(TargetCameraSerializationTest, PreSaveRegistersAncestorHierarchyBeforeItsOwnTarget) {
    SerializationTargetCamera camera(Context); RCK3dEntity child(Context), target(Context);
    child.SetParent(reinterpret_cast<CK3dEntity *>(&camera), FALSE); camera.SetRawTarget(target.GetID());
    CKFile file(Context); Prepare(file); camera.PreSave(&file, CK_STATESAVE_3DENTITYHIERARCHY);
    ASSERT_EQ(2, file.m_FileObjects.Size()); EXPECT_EQ(child.GetID(), file.m_FileObjects[0].Object);
    EXPECT_EQ(target.GetID(), file.m_FileObjects[1].Object);
    EXPECT_EQ(CK_STATESAVE_3DENTITYHIERARCHY, file.m_FileObjects[1].SaveFlags);
    child.SetParent(nullptr, FALSE);
}
