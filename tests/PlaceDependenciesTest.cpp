#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKPlace.h"
#include "RCKCamera.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DependencyPlace : public RCKPlace {
public:
    explicit DependencyPlace(CKContext *context) : RCKPlace(context) {}
    void References(CK_ID level, CK_ID camera) { m_Level = level; m_Camera = camera; }
    CK_ID LevelID() const { return m_Level; }
    CK_ID CameraID() const { return m_Camera; }
    void Append(RCKPlace *place, RCK3dEntity *portal) { CKPortalEntry e; e.place = reinterpret_cast<CKPlace *>(place); e.portal = reinterpret_cast<CK3dEntity *>(portal); m_Portals.PushBack(e); }
};
class PlaceDependencyMap : public CKDependenciesContext {
public:
    explicit PlaceDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class PlaceDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    static CKPlace *Place(RCKPlace &place) { return reinterpret_cast<CKPlace *>(&place); }
    static void Options(CKDependencies &options, CKDWORD own = 0, CKDWORD entity = 0) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM; options[CKCID_PLACE] = own; options[CKCID_3DENTITY] = entity;
    }
};
}

TEST_F(PlaceDependenciesTest, PreparePlaceBitCollectsPortalGeometryButNotDestinationCameraOrLevel) {
    DependencyPlace place(Context), other(Context); RCK3dEntity geometry(Context); RCKCamera camera(Context); CKObject level(Context);
    place.References(level.GetID(), camera.GetID()); place.Append(&other, &geometry); place.Append(&other, nullptr); place.Append(&other, &geometry);
    CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, place.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(place.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(geometry.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(other.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(camera.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(level.GetID()));
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); EXPECT_EQ(CKERR_ALREADYPRESENT, place.PrepareDependencies(dependencies)); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, PrepareZeroOwnBitKeepsPortalGeometryOutOfDependencies) {
    DependencyPlace place(Context), other(Context); RCK3dEntity geometry(Context); place.Append(&other, &geometry);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, place.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(place.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(geometry.GetID())); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, PreparePreservesAncestorChildrenAndOwnPortalDependenciesTogether) {
    DependencyPlace place(Context), other(Context); RCK3dEntity geometry(Context), child(Context); place.Append(&other, &geometry); child.SetParent(Entity(place), TRUE);
    CKDependencies options; Options(options, 1, 2); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, place.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(child.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(geometry.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(other.GetID()));
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies(); child.SetParent(nullptr, TRUE);
}
TEST_F(PlaceDependenciesTest, PreparePropagatesRealSaveExclusionFailureBeforePortalTraversal) {
    DependencyPlace place(Context), other(Context); RCK3dEntity geometry(Context); place.Append(&other, &geometry);
    place.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0); CKDependencies options; Options(options, 1);
    CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE); dependencies.StartDependencies(&options);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, place.PrepareDependencies(dependencies)); EXPECT_FALSE(dependencies.IsDependenciesHere(geometry.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(place.GetID())); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, RemapAllOwnReferencesWithZeroOwnBitWithoutCreatingReciprocalEntries) {
    DependencyPlace place(Context), oldDestination(Context), newDestination(Context); RCK3dEntity oldGeometry(Context), newGeometry(Context);
    RCKCamera oldCamera(Context), newCamera(Context); CKObject oldLevel(Context), newLevel(Context);
    place.References(oldLevel.GetID(), oldCamera.GetID()); place.Append(&oldDestination, &oldGeometry); place.Append(&oldDestination, &oldGeometry);
    CKDependencies options; Options(options); PlaceDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(oldDestination.GetID(), newDestination.GetID()); dependencies.Map(oldGeometry.GetID(), newGeometry.GetID());
    dependencies.Map(oldCamera.GetID(), newCamera.GetID()); dependencies.Map(oldLevel.GetID(), newLevel.GetID());
    ASSERT_EQ(CK_OK, place.RemapDependencies(dependencies)); EXPECT_EQ(newLevel.GetID(), place.LevelID()); EXPECT_EQ(newCamera.GetID(), place.CameraID());
    ASSERT_EQ(2, place.GetPortalCount()); for (int i = 0; i < 2; ++i) { CK3dEntity *geometry = nullptr;
        EXPECT_EQ(Place(newDestination), place.GetPortal(i, &geometry)); EXPECT_EQ(Entity(newGeometry), geometry); }
    EXPECT_EQ(0, newDestination.GetPortalCount()); EXPECT_EQ(0, oldDestination.GetPortalCount()); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, RemapExplicitZeroClearsRawIDsAndRetainsNullPortalSlots) {
    DependencyPlace place(Context), other(Context); RCK3dEntity geometry(Context); CKObject level(Context); RCKCamera camera(Context);
    place.References(level.GetID(), camera.GetID()); place.Append(&other, &geometry); CKDependencies options; Options(options);
    PlaceDependencyMap dependencies(Context); dependencies.StartDependencies(&options); dependencies.Map(level.GetID(), 0); dependencies.Map(camera.GetID(), 0);
    dependencies.Map(other.GetID(), 0); dependencies.Map(geometry.GetID(), 0); ASSERT_EQ(CK_OK, place.RemapDependencies(dependencies));
    EXPECT_EQ(0u, place.LevelID()); EXPECT_EQ(0u, place.CameraID()); ASSERT_EQ(1, place.GetPortalCount()); CK3dEntity *portal = Entity(geometry);
    EXPECT_EQ(nullptr, place.GetPortal(0, &portal)); EXPECT_EQ(nullptr, portal); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, RemapUnmappedReferencesRetainIdentityAndClip) {
    DependencyPlace place(Context), other(Context); RCK3dEntity geometry(Context); CKObject level(Context); RCKCamera camera(Context);
    place.References(level.GetID(), camera.GetID()); place.Append(&other, &geometry); place.ViewportClip() = VxRect(2, 3, 5, 7);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, place.RemapDependencies(dependencies)); EXPECT_EQ(level.GetID(), place.LevelID()); EXPECT_EQ(camera.GetID(), place.CameraID());
    CK3dEntity *portal = nullptr; EXPECT_EQ(Place(other), place.GetPortal(0, &portal)); EXPECT_EQ(Entity(geometry), portal);
    EXPECT_FLOAT_EQ(2, place.ViewportClip().left); EXPECT_FLOAT_EQ(7, place.ViewportClip().bottom); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, CopyWithOwnBitTransfersOrderedPortalsAndRawReferencesWithoutRemappingOrClip) {
    DependencyPlace source(Context), destination(Context), other(Context), mapped(Context); RCK3dEntity geometry(Context); RCKCamera camera(Context); CKObject level(Context);
    source.References(level.GetID(), camera.GetID()); source.Append(&other, &geometry); source.Append(&other, nullptr); source.Append(&other, &geometry);
    source.ViewportClip() = VxRect(2, 3, 5, 7); destination.ViewportClip() = VxRect(11, 13, 17, 19);
    CKDependencies options; Options(options, 1); PlaceDependencyMap dependencies(Context); dependencies.StartDependencies(&options); dependencies.Map(other.GetID(), mapped.GetID());
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(level.GetID(), destination.LevelID()); EXPECT_EQ(camera.GetID(), destination.CameraID());
    ASSERT_EQ(3, destination.GetPortalCount()); CK3dEntity *portal = nullptr; EXPECT_EQ(Place(other), destination.GetPortal(0, &portal)); EXPECT_EQ(Entity(geometry), portal);
    EXPECT_EQ(Place(other), destination.GetPortal(1, &portal)); EXPECT_EQ(nullptr, portal); EXPECT_EQ(Place(other), destination.GetPortal(2, &portal));
    EXPECT_EQ(Entity(geometry), portal); EXPECT_FLOAT_EQ(11, destination.ViewportClip().left); EXPECT_FLOAT_EQ(19, destination.ViewportClip().bottom);
    EXPECT_EQ(0, other.GetPortalCount()); ASSERT_EQ(CK_OK, destination.RemapDependencies(dependencies)); EXPECT_EQ(Place(mapped), destination.GetPortal(0, nullptr));
    EXPECT_EQ(Place(other), source.GetPortal(0, nullptr)); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, CopyWithoutOwnBitKeepsDestinationPortalsButAlwaysCopiesIDs) {
    DependencyPlace source(Context), destination(Context), other(Context), retained(Context); RCKCamera camera(Context); CKObject level(Context);
    source.References(level.GetID(), camera.GetID()); source.Append(&other, nullptr); destination.Append(&retained, nullptr);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(level.GetID(), destination.LevelID()); EXPECT_EQ(camera.GetID(), destination.CameraID());
    ASSERT_EQ(1, destination.GetPortalCount()); EXPECT_EQ(Place(retained), destination.GetPortal(0, nullptr)); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, CopyOwnBitWithEmptySourceClearsDestinationPortals) {
    DependencyPlace source(Context), destination(Context), other(Context); destination.Append(&other, nullptr); CKDependencies options; Options(options, 1);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(0, destination.GetPortalCount()); EXPECT_EQ(0u, destination.LevelID()); EXPECT_EQ(0u, destination.CameraID()); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, SelfCopyPreservesActualSkinBoneVertexMeshAndPortalStorage) {
    DependencyPlace place(Context), other(Context); RCK3dEntity bone(Context); RCKMesh mesh(Context); RCKCamera camera(Context); CKObject level(Context);
    place.References(level.GetID(), camera.GetID()); place.Append(&other, nullptr); place.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh), TRUE);
    CKSkin *skin = place.CreateSkin(); ASSERT_NE(nullptr, skin); skin->SetBoneCount(1); skin->GetBoneData(0)->SetBone(Entity(bone));
    skin->SetVertexCount(1); CKSkinVertexData *vertex = skin->GetVertexData(0); vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, .75f);
    VxVector initialPos(7, 11, 13); vertex->SetInitialPos(initialPos); CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, place.Copy(place, dependencies)); EXPECT_EQ(skin, place.GetSkin()); ASSERT_EQ(1, skin->GetBoneCount()); EXPECT_EQ(Entity(bone), skin->GetBoneData(0)->GetBone());
    ASSERT_EQ(1, skin->GetVertexCount()); EXPECT_EQ(vertex, skin->GetVertexData(0)); ASSERT_EQ(1, vertex->GetBoneCount()); EXPECT_FLOAT_EQ(.75f, vertex->GetWeight(0));
    EXPECT_EQ(reinterpret_cast<CKMesh *>(&mesh), place.GetCurrentMesh()); EXPECT_EQ(1, place.GetMeshCount()); EXPECT_EQ(Place(other), place.GetPortal(0, nullptr));
    EXPECT_EQ(camera.GetID(), place.CameraID()); EXPECT_EQ(level.GetID(), place.LevelID()); dependencies.StopDependencies();
}
TEST_F(PlaceDependenciesTest, ContextCopyClonesSelectedPortalGeometryAndRemapsOnlyTheNewPlace) {
    CKObject *sourceObject = Context->CreateObject(CKCID_PLACE); CKObject *otherObject = Context->CreateObject(CKCID_PLACE);
    CKObject *geometryObject = Context->CreateObject(CKCID_3DENTITY); ASSERT_NE(nullptr, sourceObject); ASSERT_NE(nullptr, otherObject); ASSERT_NE(nullptr, geometryObject);
    RCKPlace *source = static_cast<RCKPlace *>(sourceObject); RCKPlace *other = static_cast<RCKPlace *>(otherObject); RCK3dEntity *geometry = static_cast<RCK3dEntity *>(geometryObject);
    geometry->SetFlags(geometry->GetFlags() | CK_3DENTITY_PORTAL); source->AddPortal(Place(*other), Entity(*geometry)); CKDependencies options; Options(options, 1);
    CKObject *copiedObject = Context->CopyObject(sourceObject, &options); ASSERT_NE(nullptr, copiedObject); EXPECT_NE(sourceObject, copiedObject);
    RCKPlace *copy = static_cast<RCKPlace *>(copiedObject); ASSERT_EQ(1, copy->GetPortalCount()); CK3dEntity *copiedGeometry = nullptr;
    EXPECT_EQ(Place(*other), copy->GetPortal(0, &copiedGeometry)); ASSERT_NE(nullptr, copiedGeometry); EXPECT_NE(Entity(*geometry), copiedGeometry);
    CK3dEntity *originalGeometry = nullptr; EXPECT_EQ(Place(*other), source->GetPortal(0, &originalGeometry)); EXPECT_EQ(Entity(*geometry), originalGeometry);
    EXPECT_EQ(1, other->GetPortalCount()); EXPECT_EQ(Place(*source), other->GetPortal(0, nullptr));
}

