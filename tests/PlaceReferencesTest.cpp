#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKPlace.h"
#include "RCKCamera.h"
#include "RCKMesh.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class ReferencePlaceProbe : public RCKPlace {
public:
    explicit ReferencePlaceProbe(CKContext *context) : RCKPlace(context) {}
    void Append(CKPlace *place, CK3dEntity *portal) {
        CKPortalEntry entry; entry.place = place; entry.portal = portal; m_Portals.PushBack(entry);
    }
    void SetRawReferences(CK_ID level, CK_ID camera) { m_Level = level; m_Camera = camera; }
    CK_ID LevelID() const { return m_Level; }
    CK_ID CameraID() const { return m_Camera; }
};
class PlaceReferencesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_OBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKPlace *Place(RCKPlace &place) { return reinterpret_cast<CKPlace *>(&place); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKCamera *Camera(RCKCamera &camera) { return reinterpret_cast<CKCamera *>(&camera); }
};
}

TEST_F(PlaceReferencesTest, DefaultCameraSDKStoresIdentityAndNullClearsWithoutChangingHierarchy) {
    ReferencePlaceProbe place(Context); RCKCamera camera(Context); RCK3dEntity parent(Context);
    ASSERT_TRUE(camera.SetParent(Entity(parent), TRUE)); EXPECT_EQ(nullptr, Place(place)->GetDefaultCamera());
    Place(place)->SetDefaultCamera(Camera(camera)); EXPECT_EQ(camera.GetID(), place.CameraID());
    EXPECT_EQ(Camera(camera), Place(place)->GetDefaultCamera()); EXPECT_EQ(Entity(parent), camera.GetParent());
    Place(place)->SetDefaultCamera(nullptr); EXPECT_EQ(0u, place.CameraID()); EXPECT_EQ(nullptr, place.GetDefaultCamera());
}
TEST_F(PlaceReferencesTest, SharedDefaultCameraIsIndependentPerPlace) {
    ReferencePlaceProbe a(Context), b(Context); RCKCamera camera(Context);
    a.SetDefaultCamera(Camera(camera)); b.SetDefaultCamera(Camera(camera)); a.SetDefaultCamera(nullptr);
    EXPECT_EQ(nullptr, a.GetDefaultCamera()); EXPECT_EQ(Camera(camera), b.GetDefaultCamera()); EXPECT_EQ(camera.GetID(), b.CameraID());
}
TEST_F(PlaceReferencesTest, DefaultCameraGetterResolvesCurrentContextIDWithoutCreatingObjects) {
    ReferencePlaceProbe place(Context); RCKCamera camera(Context); const int count = Context->GetObjectCount();
    place.SetRawReferences(0, camera.GetID()); EXPECT_EQ(Camera(camera), place.GetDefaultCamera());
    place.SetRawReferences(0, static_cast<CK_ID>(count + 100)); EXPECT_EQ(nullptr, place.GetDefaultCamera());
    EXPECT_EQ(count, Context->GetObjectCount()); EXPECT_EQ(static_cast<CK_ID>(count + 100), place.CameraID());
}
TEST_F(PlaceReferencesTest, PostDeletionClearsMissingReferencesAndPreservesLiveOnesIndependently) {
    ReferencePlaceProbe place(Context); RCKCamera camera(Context); CKObject *level = Context->CreateObject(CKCID_OBJECT);
    ASSERT_NE(nullptr, level); place.SetRawReferences(level->GetID(), camera.GetID()); place.CheckPostDeletion();
    EXPECT_EQ(level->GetID(), place.LevelID()); EXPECT_EQ(camera.GetID(), place.CameraID());
    place.SetRawReferences(static_cast<CK_ID>(Context->GetObjectCount() + 100), camera.GetID()); place.CheckPostDeletion();
    EXPECT_EQ(0u, place.LevelID()); EXPECT_EQ(camera.GetID(), place.CameraID());
    place.SetRawReferences(level->GetID(), static_cast<CK_ID>(Context->GetObjectCount() + 100)); place.CheckPostDeletion();
    EXPECT_EQ(level->GetID(), place.LevelID()); EXPECT_EQ(0u, place.CameraID());
}
TEST_F(PlaceReferencesTest, PostDeletionClearsDirectlyDeletedCameraBeforeItsFreeIDIsReused) {
    ReferencePlaceProbe place(Context); RCKCamera *camera = new RCKCamera(Context); const CK_ID id = camera->GetID();
    camera->ModifyObjectFlags(CK_OBJECT_FREEID, 0); place.SetDefaultCamera(Camera(*camera)); delete camera;
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(nullptr, place.GetDefaultCamera()); EXPECT_EQ(id, place.CameraID());
    place.CheckPostDeletion(); EXPECT_EQ(0u, place.CameraID());
    RCKCamera replacement(Context); EXPECT_EQ(id, replacement.GetID()); EXPECT_EQ(nullptr, place.GetDefaultCamera());
    place.SetDefaultCamera(Camera(replacement)); EXPECT_EQ(Camera(replacement), place.GetDefaultCamera());
}
TEST_F(PlaceReferencesTest, PreDeletionRemovesEveryAdjacentEntryReferencingMarkedDestination) {
    ReferencePlaceProbe source(Context); RCKPlace doomed(Context), survivor(Context); RCK3dEntity geometry(Context);
    source.Append(Place(doomed), nullptr); source.Append(Place(doomed), Entity(geometry)); source.Append(Place(survivor), Entity(geometry));
    doomed.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); source.CheckPreDeletion();
    ASSERT_EQ(1, source.GetPortalCount()); CK3dEntity *out = nullptr;
    EXPECT_EQ(Place(survivor), source.GetPortal(0, &out)); EXPECT_EQ(Entity(geometry), out);
    doomed.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(PlaceReferencesTest, PreDeletionRemovesMarkedGeometryAcrossDifferentDestinations) {
    ReferencePlaceProbe source(Context); RCKPlace a(Context), b(Context); RCK3dEntity doomed(Context), survivor(Context);
    source.Append(Place(a), Entity(doomed)); source.Append(Place(b), Entity(doomed)); source.Append(Place(a), Entity(survivor));
    doomed.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); source.CheckPreDeletion();
    ASSERT_EQ(1, source.GetPortalCount()); CK3dEntity *out = nullptr;
    EXPECT_EQ(Place(a), source.GetPortal(0, &out)); EXPECT_EQ(Entity(survivor), out); doomed.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(PlaceReferencesTest, PreDeletionPreservesUnmarkedWildcardAndDoesNotMutateReverseEndpoint) {
    ReferencePlaceProbe source(Context), destination(Context); source.Append(Place(destination), nullptr);
    destination.Append(Place(source), nullptr); source.CheckPreDeletion();
    EXPECT_EQ(1, source.GetPortalCount()); EXPECT_EQ(1, destination.GetPortalCount());
    destination.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); source.CheckPreDeletion();
    EXPECT_EQ(0, source.GetPortalCount()); EXPECT_EQ(1, destination.GetPortalCount());
    destination.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(PlaceReferencesTest, PreDeletionDoesNotClearDefaultCameraUntilObjectActuallyDisappears) {
    ReferencePlaceProbe place(Context); RCKCamera camera(Context); place.SetDefaultCamera(Camera(camera));
    camera.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); place.CheckPreDeletion(); place.CheckPostDeletion();
    EXPECT_EQ(camera.GetID(), place.CameraID()); EXPECT_EQ(Camera(camera), place.GetDefaultCamera());
    camera.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(PlaceReferencesTest, ObjectUsageFindsDestinationAndGeometryOnlyForEntityClassFilter) {
    ReferencePlaceProbe source(Context); RCKPlace destination(Context); RCK3dEntity geometry(Context), unrelated(Context);
    source.Append(Place(destination), Entity(geometry));
    EXPECT_TRUE(source.IsObjectUsed(&destination, CKCID_PLACE)); EXPECT_TRUE(source.IsObjectUsed(&geometry, CKCID_3DENTITY));
    EXPECT_FALSE(source.IsObjectUsed(&unrelated, CKCID_3DENTITY));
    EXPECT_FALSE(source.IsObjectUsed(&destination, CKCID_OBJECT)); EXPECT_FALSE(source.IsObjectUsed(&geometry, CKCID_OBJECT));
}
TEST_F(PlaceReferencesTest, DefaultCameraReferenceAloneIsNotPortalObjectUsage) {
    RCKPlace place(Context); RCKCamera camera(Context); place.SetDefaultCamera(Camera(camera));
    EXPECT_FALSE(place.IsObjectUsed(&camera, CKCID_CAMERA)); EXPECT_EQ(Camera(camera), place.GetDefaultCamera());
}
TEST_F(PlaceReferencesTest, ObjectUsageFallsBackToInheritedCurrentMeshRelationship) {
    RCKPlace place(Context); CKMesh *mesh = static_cast<CKMesh *>(Context->CreateObject(CKCID_MESH));
    ASSERT_NE(nullptr, mesh); place.SetCurrentMesh(mesh); EXPECT_TRUE(place.IsObjectUsed(mesh, CKCID_MESH));
    place.SetCurrentMesh(nullptr); EXPECT_TRUE(place.IsObjectUsed(mesh, CKCID_MESH));
    ASSERT_EQ(CK_OK, place.RemoveMesh(mesh)); EXPECT_FALSE(place.IsObjectUsed(mesh, CKCID_MESH));
}
TEST_F(PlaceReferencesTest, PortalRemovalImmediatelyUpdatesObjectUsageWithoutDeletingObjects) {
    RCKPlace source(Context), destination(Context); RCK3dEntity geometry(Context); geometry.SetFlags(CK_3DENTITY_PORTAL);
    source.AddPortal(Place(destination), Entity(geometry)); ASSERT_TRUE(source.IsObjectUsed(&geometry, CKCID_3DENTITY));
    source.RemovePortal(Place(destination), Entity(geometry)); EXPECT_FALSE(source.IsObjectUsed(&geometry, CKCID_3DENTITY));
    EXPECT_FALSE(source.IsObjectUsed(&destination, CKCID_PLACE)); EXPECT_EQ(&geometry, Context->GetObject(geometry.GetID()));
    EXPECT_EQ(&destination, Context->GetObject(destination.GetID()));
}
