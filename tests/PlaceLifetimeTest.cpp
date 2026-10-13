#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKPlace.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class BatchLifetimePlace : public RCKPlace {
public:
    BatchLifetimePlace(CKContext *context, CK_ID parentID, bool *parentWasDeleted)
        : RCKPlace(context), ParentID(parentID), ParentWasDeleted(parentWasDeleted) {}
    ~BatchLifetimePlace() override { *ParentWasDeleted = m_Context->GetObject(ParentID) == nullptr; }
private:
    CK_ID ParentID; bool *ParentWasDeleted;
};
class PlaceLifetimeTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKPlace *Place(RCKPlace &place) { return reinterpret_cast<CKPlace *>(&place); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
};
}

TEST_F(PlaceLifetimeTest, DirectDeletionRemovesAllBidirectionalPortalGeometriesWithoutDeletingThem) {
    RCKPlace survivor(Context); RCK3dEntity first(Context), second(Context);
    first.SetFlags(CK_3DENTITY_PORTAL); second.SetFlags(CK_3DENTITY_PORTAL);
    RCKPlace *deleted = new RCKPlace(Context); const CK_ID deletedID = deleted->GetID();
    deleted->AddPortal(Place(survivor), Entity(first)); deleted->AddPortal(Place(survivor), Entity(second));
    ASSERT_EQ(2, survivor.GetPortalCount()); ASSERT_EQ(2, deleted->GetPortalCount()); delete deleted;
    EXPECT_EQ(nullptr, Context->GetObject(deletedID)); ASSERT_EQ(0, survivor.GetPortalCount());
    EXPECT_EQ(&first, Context->GetObject(first.GetID())); EXPECT_EQ(&second, Context->GetObject(second.GetID()));
}
TEST_F(PlaceLifetimeTest, DirectDeletionRemovesAsymmetricLiteralDuplicateIncomingEntriesAndKeepsUnrelatedEntry) {
    RCKPlace source(Context), unrelated(Context); RCKPlace *deleted = new RCKPlace(Context);
    CKFile file(Context); file.m_FileObjects.Resize(2);
    file.m_FileObjects[0].CreatedObject = deleted->GetID(); file.m_FileObjects[1].CreatedObject = unrelated.GetID();
    CKStateChunk chunk(CKCID_PLACE, &file); chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(0x1000); chunk.WriteInt(3);
    chunk.WriteInt(0); chunk.WriteInt(-1); chunk.WriteInt(0); chunk.WriteInt(-1);
    chunk.WriteInt(1); chunk.WriteInt(-1); chunk.CloseChunk(); ASSERT_EQ(CK_OK, source.Load(&chunk, &file));
    ASSERT_EQ(3, source.GetPortalCount()); ASSERT_EQ(0, deleted->GetPortalCount()); delete deleted;
    ASSERT_EQ(1, source.GetPortalCount()); CK3dEntity *geometry = Entity(source);
    EXPECT_EQ(Place(unrelated), source.GetPortal(0, &geometry)); EXPECT_EQ(nullptr, geometry);
}
TEST_F(PlaceLifetimeTest, DirectDeletionRemovesPlaceUsedAsPortalGeometryFromBothOtherPlaces) {
    RCKPlace first(Context), second(Context); RCKPlace *geometry = new RCKPlace(Context);
    const CK_ID geometryID = geometry->GetID(); geometry->SetFlags(CK_3DENTITY_PORTAL);
    first.AddPortal(Place(second), Entity(*geometry)); ASSERT_EQ(1, first.GetPortalCount()); ASSERT_EQ(1, second.GetPortalCount());
    delete geometry; EXPECT_EQ(nullptr, Context->GetObject(geometryID));
    ASSERT_EQ(0, first.GetPortalCount()); ASSERT_EQ(0, second.GetPortalCount());
    EXPECT_EQ(&first, Context->GetObject(first.GetID())); EXPECT_EQ(&second, Context->GetObject(second.GetID()));
}
TEST_F(PlaceLifetimeTest, ContextBatchDeletesOrdinaryParentBeforePlaceWithoutDereferencingDeadParent) {
    new RCKRenderManager(Context);
    RCK3dEntity *parent = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr, parent);
    bool parentWasDeleted = false; BatchLifetimePlace *place = new BatchLifetimePlace(Context, parent->GetID(), &parentWasDeleted);
    ASSERT_TRUE(place->SetParent(Entity(*parent), TRUE)); ASSERT_EQ(1, parent->GetChildrenCount());
    const CK_ID parentID = parent->GetID(), placeID = place->GetID(); CK_ID ids[2] = {parentID, placeID};
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK, Context->DestroyObjects(ids, 2, 0, &dependencies)); EXPECT_TRUE(parentWasDeleted);
    EXPECT_EQ(nullptr, Context->GetObject(parentID)); EXPECT_EQ(nullptr, Context->GetObject(placeID));
}
