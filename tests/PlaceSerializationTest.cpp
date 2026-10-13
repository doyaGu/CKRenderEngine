#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKPlace.h"
#include "RCKCamera.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializedPlace : public RCKPlace {
public:
    explicit SerializedPlace(CKContext *context) : RCKPlace(context) {}
    void References(CK_ID level, CK_ID camera) { m_Level = level; m_Camera = camera; }
    CK_ID LevelID() const { return m_Level; }
    CK_ID CameraID() const { return m_Camera; }
    void Append(RCKPlace *place, RCK3dEntity *portal) {
        CKPortalEntry entry; entry.place = reinterpret_cast<CKPlace *>(place);
        entry.portal = reinterpret_cast<CK3dEntity *>(portal); m_Portals.PushBack(entry);
    }
};
class PlaceSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKPlace *Place(RCKPlace &place) { return reinterpret_cast<CKPlace *>(&place); }
    static void Prepare(CKFile &file) { file.m_IndexByClassId.Resize(CKGetClassCount()); }
    static void Begin(CKStateChunk &chunk, int version = 6) { chunk.StartWrite(); chunk.SetDataVersion(version); }
    static CKERROR Load(SerializedPlace &place, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return place.Load(&chunk, file);
    }
    static void FileObjects(CKFile &file, CKObject &a, CKObject &b) {
        file.m_FileObjects.Resize(2); file.m_FileObjects[0].CreatedObject = a.GetID(); file.m_FileObjects[1].CreatedObject = b.GetID();
    }
};
}

TEST_F(PlaceSerializationTest, NullLoadLeavesReferencesPortalsAndTransformIntact) {
    SerializedPlace place(Context), other(Context); RCKCamera camera(Context); CKObject level(Context); RCK3dEntity portal(Context);
    place.References(level.GetID(), camera.GetID()); place.Append(&other, &portal); VxVector pos(7, 11, 13); place.SetPosition(&pos);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, place.Load(nullptr, nullptr)); EXPECT_EQ(level.GetID(), place.LevelID());
    EXPECT_EQ(camera.GetID(), place.CameraID()); EXPECT_EQ(1, place.GetPortalCount()); place.GetPosition(&pos);
    EXPECT_FLOAT_EQ(7, pos.x); EXPECT_FLOAT_EQ(11, pos.y); EXPECT_FLOAT_EQ(13, pos.z);
}
TEST_F(PlaceSerializationTest, FilelessLoadIgnoresAllPlaceSectionsAndEntityTransformPayload) {
    SerializedPlace place(Context), other(Context); RCKCamera camera(Context); CKObject level(Context);
    place.References(level.GetID(), camera.GetID()); VxVector pos(7, 11, 13); place.SetPosition(&pos);
    CKStateChunk chunk(CKCID_PLACE, nullptr); Begin(chunk); chunk.WriteIdentifier(0x2000); chunk.WriteInt(0);
    chunk.WriteIdentifier(0x8000); chunk.WriteInt(0); chunk.WriteIdentifier(0x1000); chunk.WriteInt(1); chunk.WriteInt(other.GetID()); chunk.WriteInt(0);
    chunk.WriteIdentifier(0x00100000); chunk.WriteDword(0x00020000); chunk.WriteDword(0);
    for (int i = 0; i < 4; ++i) chunk.WriteVector(VxVector(100, 200, 300)); chunk.WriteInt(0);
    ASSERT_EQ(CK_OK, Load(place, chunk)); EXPECT_EQ(level.GetID(), place.LevelID()); EXPECT_EQ(camera.GetID(), place.CameraID());
    EXPECT_EQ(0, place.GetPortalCount()); place.GetPosition(&pos); EXPECT_FLOAT_EQ(7, pos.x); EXPECT_FLOAT_EQ(11, pos.y); EXPECT_FLOAT_EQ(13, pos.z);
}
TEST_F(PlaceSerializationTest, LoadMirrorsObjectVisibilityAndPreservesOtherMoveableFlags) {
    SerializedPlace place(Context);
    for (CKDWORD identifier : {CKDWORD(0), CKDWORD(4), CKDWORD(0x18)}) {
        place.SetMoveableFlags(VX_MOVEABLE_PICKABLE | VX_MOVEABLE_VISIBLE | VX_MOVEABLE_HIERARCHICALHIDE);
        CKStateChunk chunk(CKCID_PLACE, nullptr); Begin(chunk); chunk.WriteIdentifier(identifier ? identifier : 0x76543210);
        chunk.WriteInt(0); ASSERT_EQ(CK_OK, Load(place, chunk)); CKDWORD flags = place.GetMoveableFlags();
        EXPECT_EQ(identifier == 0, bool(flags & VX_MOVEABLE_VISIBLE));
        EXPECT_EQ(identifier == 0x18, bool(flags & VX_MOVEABLE_HIERARCHICALHIDE)); EXPECT_TRUE(flags & VX_MOVEABLE_PICKABLE);
    }
}
TEST_F(PlaceSerializationTest, FileLiteralReferencesResolveIndexZeroAcrossDataVersions) {
    SerializedPlace place(Context); RCKCamera camera(Context); CKObject level(Context); CKFile file(Context); FileObjects(file, camera, level);
    for (int version : {0, 4, 5, 6, 8}) {
        CKStateChunk chunk(CKCID_PLACE, &file); Begin(chunk, version);
        chunk.WriteIdentifier(0x8000); chunk.WriteInt(1); chunk.WriteIdentifier(0x2000); chunk.WriteInt(0);
        ASSERT_EQ(CK_OK, Load(place, chunk, &file)); EXPECT_EQ(level.GetID(), place.LevelID()); EXPECT_EQ(camera.GetID(), place.CameraID());
    }
}
TEST_F(PlaceSerializationTest, FileMissingSectionsRetainReferencesAndExistingPortalEntries) {
    SerializedPlace place(Context), other(Context); CKObject level(Context); RCKCamera camera(Context); place.Append(&other, nullptr);
    place.References(level.GetID(), camera.GetID()); CKFile file(Context); CKStateChunk chunk(CKCID_PLACE, &file);
    Begin(chunk); chunk.WriteIdentifier(0x76543210); chunk.WriteInt(13); ASSERT_EQ(CK_OK, Load(place, chunk, &file));
    EXPECT_EQ(level.GetID(), place.LevelID()); EXPECT_EQ(camera.GetID(), place.CameraID()); EXPECT_EQ(1, place.GetPortalCount());
}
TEST_F(PlaceSerializationTest, FileUnresolvedAndMinusOneReferencesClearIDs) {
    SerializedPlace place(Context); CKObject level(Context); RCKCamera camera(Context); CKFile file(Context); FileObjects(file, camera, level);
    for (int index : {-1, 2, 99}) {
        place.References(level.GetID(), camera.GetID()); CKStateChunk chunk(CKCID_PLACE, &file); Begin(chunk);
        chunk.WriteIdentifier(0x2000); chunk.WriteInt(index); chunk.WriteIdentifier(0x8000); chunk.WriteInt(index);
        ASSERT_EQ(CK_OK, Load(place, chunk, &file)); EXPECT_EQ(0u, place.LevelID()); EXPECT_EQ(0u, place.CameraID());
    }
}
TEST_F(PlaceSerializationTest, FilePortalsAppendDuplicatesAndNullGeometryWithoutReciprocalConnection) {
    SerializedPlace place(Context), other(Context); RCK3dEntity portal(Context); CKFile file(Context); FileObjects(file, other, portal);
    place.Append(&other, &portal); CKStateChunk chunk(CKCID_PLACE, &file); Begin(chunk);
    chunk.WriteIdentifier(0x1000); chunk.WriteInt(4);
    chunk.WriteInt(0); chunk.WriteInt(1); chunk.WriteInt(0); chunk.WriteInt(1);
    chunk.WriteInt(0); chunk.WriteInt(-1); chunk.WriteInt(-1); chunk.WriteInt(1);
    ASSERT_EQ(CK_OK, Load(place, chunk, &file)); ASSERT_EQ(4, place.GetPortalCount()); EXPECT_EQ(0, other.GetPortalCount());
    for (int i = 0; i < 3; ++i) { CK3dEntity *geometry = nullptr; EXPECT_EQ(Place(other), place.GetPortal(i, &geometry)); EXPECT_EQ(Entity(portal), geometry); }
    CK3dEntity *geometry = Entity(portal); EXPECT_EQ(Place(other), place.GetPortal(3, &geometry)); EXPECT_EQ(nullptr, geometry);
    ASSERT_EQ(CK_OK, place.Load(&chunk, &file)); EXPECT_EQ(7, place.GetPortalCount());
}
TEST_F(PlaceSerializationTest, FileZeroAndNegativePortalCountsPreserveExistingEntries) {
    SerializedPlace place(Context), other(Context); place.Append(&other, nullptr); CKFile file(Context);
    for (int count : {0, -3}) { CKStateChunk chunk(CKCID_PLACE, &file); Begin(chunk); chunk.WriteIdentifier(0x1000); chunk.WriteInt(count);
        ASSERT_EQ(CK_OK, Load(place, chunk, &file)); EXPECT_EQ(1, place.GetPortalCount()); }
}
TEST_F(PlaceSerializationTest, FileChildrenCollectOnlyRootsAndPreserveWorldPositions) {
    SerializedPlace place(Context); RCK3dEntity root(Context), child(Context), externalParent(Context), externalChild(Context);
    child.SetParent(Entity(root), TRUE); externalChild.SetParent(Entity(externalParent), TRUE);
    VxVector pos(7, 11, 13); root.SetPosition(&pos); pos = VxVector(17, 19, 23); externalChild.SetPosition(&pos);
    CKFile file(Context); file.m_FileObjects.Resize(4);
    file.m_FileObjects[0].CreatedObject = child.GetID(); file.m_FileObjects[1].CreatedObject = root.GetID();
    file.m_FileObjects[2].CreatedObject = externalChild.GetID(); file.m_FileObjects[3].CreatedObject = place.GetID();
    CKStateChunk chunk(CKCID_PLACE, &file); Begin(chunk); chunk.WriteIdentifier(0x4000); chunk.WriteInt(4);
    chunk.WriteInt(0); chunk.WriteInt(1); chunk.WriteInt(2); chunk.WriteInt(-1);
    ASSERT_EQ(CK_OK, Load(place, chunk, &file)); EXPECT_EQ(Entity(place), root.GetParent()); EXPECT_EQ(Entity(root), child.GetParent());
    EXPECT_EQ(Entity(place), externalChild.GetParent()); EXPECT_EQ(2, place.GetChildrenCount()); root.GetPosition(&pos); EXPECT_FLOAT_EQ(7, pos.x);
    externalChild.GetPosition(&pos); EXPECT_FLOAT_EQ(17, pos.x); EXPECT_FLOAT_EQ(19, pos.y); EXPECT_FLOAT_EQ(23, pos.z);
    child.SetParent(nullptr, TRUE); root.SetParent(nullptr, TRUE); externalChild.SetParent(nullptr, TRUE);
}
TEST_F(PlaceSerializationTest, FileChildAlreadyReferencingPlaceKeepsItsIntermediateParent) {
    SerializedPlace place(Context); RCK3dEntity middle(Context), child(Context); middle.SetParent(Entity(place), TRUE); child.SetParent(Entity(middle), TRUE);
    CKFile file(Context); file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = child.GetID();
    CKStateChunk chunk(CKCID_PLACE, &file); Begin(chunk); chunk.WriteIdentifier(0x4000); chunk.WriteInt(1); chunk.WriteInt(0);
    ASSERT_EQ(CK_OK, Load(place, chunk, &file)); EXPECT_EQ(Entity(middle), child.GetParent()); EXPECT_EQ(1, place.GetChildrenCount());
    child.SetParent(nullptr, TRUE); middle.SetParent(nullptr, TRUE);
}
TEST_F(PlaceSerializationTest, SaveAlwaysUsesPlaceClassAndNeverSerializesEntityTransformOrChildren) {
    SerializedPlace place(Context); RCK3dEntity child(Context); child.SetParent(Entity(place), TRUE); VxVector pos(7, 11, 13); place.SetPosition(&pos);
    CKFile file(Context);
    for (CKFile *target : {static_cast<CKFile *>(nullptr), &file}) {
        CKStateChunk *chunk = place.Save(target, CK_STATESAVE_ALL); ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_PLACE, chunk->GetChunkClassID());
        chunk->StartRead(); EXPECT_FALSE(chunk->SeekIdentifier(0x00100000)); EXPECT_FALSE(chunk->SeekIdentifier(0x4000)); DeleteCKStateChunk(chunk);
    }
    child.SetParent(nullptr, TRUE);
}
TEST_F(PlaceSerializationTest, FilelessCameraMaskWritesRawIDAndLevelIsAlwaysOmitted) {
    SerializedPlace place(Context); RCKCamera camera(Context); CKObject level(Context); place.References(level.GetID(), camera.GetID());
    CKStateChunk *chunk = place.Save(nullptr, CK_STATESAVE_PLACECAMERA | CK_STATESAVE_PLACELEVEL); ASSERT_NE(nullptr, chunk);
    chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x2000)); EXPECT_EQ(camera.GetID(), chunk->ReadObjectID());
    EXPECT_FALSE(chunk->SeekIdentifier(0x8000)); EXPECT_FALSE(chunk->SeekIdentifier(0x1000)); DeleteCKStateChunk(chunk);
}
TEST_F(PlaceSerializationTest, FileMasksWriteRegisteredIndicesAndPortalPairOrder) {
    SerializedPlace place(Context), other(Context); RCKCamera camera(Context); CKObject level(Context); RCK3dEntity portal(Context);
    place.References(level.GetID(), camera.GetID()); place.Append(&other, &portal); place.Append(&other, nullptr); CKFile file(Context); Prepare(file);
    file.SaveObject(&portal, 0); file.SaveObject(&other, 0); file.SaveObject(&level, 0); file.SaveObject(&camera, 0);
    CKStateChunk *chunk = place.Save(&file, 0xf000); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x2000)); EXPECT_EQ(3, chunk->ReadInt()); ASSERT_TRUE(chunk->SeekIdentifier(0x8000)); EXPECT_EQ(2, chunk->ReadInt());
    ASSERT_TRUE(chunk->SeekIdentifier(0x1000)); EXPECT_EQ(2, chunk->ReadInt()); EXPECT_EQ(1, chunk->ReadInt()); EXPECT_EQ(0, chunk->ReadInt());
    EXPECT_EQ(1, chunk->ReadInt()); EXPECT_EQ(-1, chunk->ReadInt()); EXPECT_FALSE(chunk->SeekIdentifier(0x4000)); DeleteCKStateChunk(chunk);
}
TEST_F(PlaceSerializationTest, ZeroAndUnrelatedMasksOmitEveryPlaceSectionEvenWithFile) {
    SerializedPlace place(Context), other(Context); RCKCamera camera(Context); CKObject level(Context); place.Append(&other, nullptr); place.References(level.GetID(), camera.GetID());
    CKFile file(Context);
    for (CKFile *target : {static_cast<CKFile *>(nullptr), &file}) for (CKDWORD mask : {CKDWORD(0), CKDWORD(0x00400000)}) {
        CKStateChunk *chunk = place.Save(target, mask); ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_PLACE, chunk->GetChunkClassID()); chunk->StartRead();
        for (CKDWORD identifier : {CKDWORD(0x1000), CKDWORD(0x2000), CKDWORD(0x4000), CKDWORD(0x8000)}) EXPECT_FALSE(chunk->SeekIdentifier(identifier));
        DeleteCKStateChunk(chunk);
    }
}
TEST_F(PlaceSerializationTest, FileUnregisteredCameraLevelAndPortalSerializeMinusOne) {
    SerializedPlace place(Context), other(Context); RCKCamera camera(Context); CKObject level(Context); place.References(level.GetID(), camera.GetID()); place.Append(&other, nullptr);
    CKFile file(Context); CKStateChunk *chunk = place.Save(&file, 0xb000); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x2000)); EXPECT_EQ(-1, chunk->ReadInt()); ASSERT_TRUE(chunk->SeekIdentifier(0x8000)); EXPECT_EQ(-1, chunk->ReadInt());
    ASSERT_TRUE(chunk->SeekIdentifier(0x1000)); EXPECT_EQ(1, chunk->ReadInt()); EXPECT_EQ(-1, chunk->ReadInt()); EXPECT_EQ(-1, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(PlaceSerializationTest, PreSaveRegistersInheritedChildrenBeforeCameraAndPreservesSaveMask) {
    SerializedPlace place(Context); RCK3dEntity child(Context); RCKCamera camera(Context); child.SetParent(Entity(place), TRUE);
    place.SetDefaultCamera(reinterpret_cast<CKCamera *>(&camera)); CKFile file(Context); Prepare(file);
    CKDWORD flags = CK_STATESAVE_3DENTITYHIERARCHY | CK_STATESAVE_PLACECAMERA; place.PreSave(&file, flags); place.PreSave(&file, flags);
    ASSERT_EQ(2, file.m_FileObjects.Size()); EXPECT_EQ(child.GetID(), file.m_FileObjects[0].Object); EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[0].SaveFlags);
    EXPECT_EQ(camera.GetID(), file.m_FileObjects[1].Object); EXPECT_EQ(flags, file.m_FileObjects[1].SaveFlags); child.SetParent(nullptr, TRUE);
}
TEST_F(PlaceSerializationTest, PreSaveIgnoresLevelAndPortalsAndFiltersDynamicCamera) {
    SerializedPlace place(Context), other(Context); RCKCamera camera(Context); CKObject level(Context); RCK3dEntity portal(Context);
    place.References(level.GetID(), camera.GetID()); place.Append(&other, &portal); camera.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    CKFile file(Context); Prepare(file); place.PreSave(&file, 0); EXPECT_EQ(0, file.m_FileObjects.Size());
    camera.ModifyObjectFlags(0, CK_OBJECT_DYNAMIC); CKFile ordinaryFile(Context); Prepare(ordinaryFile);
    place.PreSave(&ordinaryFile, 0); ASSERT_EQ(1, ordinaryFile.m_FileObjects.Size());
    EXPECT_EQ(camera.GetID(), ordinaryFile.m_FileObjects[0].Object); EXPECT_EQ(0u, ordinaryFile.m_FileObjects[0].SaveFlags);
}

