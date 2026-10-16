#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKGrid.h"
#include "RCKLayer.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"
#include "SerializationGridRegistry.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class GridStorageCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    SerializationGridRegistry *Registry = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Registry = new SerializationGridRegistry(Context);
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    RCKGrid *Grid(const char *name, int width, int length) {
        RCKGrid *grid = static_cast<RCKGrid *>(Context->CreateObject(CKCID_GRID, const_cast<char *>(name)));
        if (grid)
            grid->SetDimensions(width, length, static_cast<float>(width), static_cast<float>(length));
        return grid;
    }

    void DisplayIDs(RCKGrid &grid, CK_ID (&ids)[4]) {
        grid.ConstructMeshTexture(0.5f);
        CKMesh *mesh = grid.GetCurrentMesh();
        ASSERT_NE(nullptr, mesh);
        CKMaterial *main = mesh->GetFaceMaterial(0), *border = mesh->GetFaceMaterial(2);
        ASSERT_NE(nullptr, main);
        ASSERT_NE(nullptr, border);
        CKTexture *texture = main->GetTexture(0);
        ASSERT_NE(nullptr, texture);
        ids[0] = mesh->GetID();
        ids[1] = main->GetID();
        ids[2] = border->GetID();
        ids[3] = texture->GetID();
    }
};

TEST_F(GridStorageCompletionTest, MissingLayerSnapshotRetainsNullFrameAndRestoresFollowingLiveCells) {
    RCKGrid *grid = Grid("FramedGrid", 1, 1);
    ASSERT_NE(nullptr, grid);
    grid->Show(CKHIDE);
    CKLayer *removed = grid->AddLayer(Registry->RegisterType("RemovedFrame"));
    CKLayer *survivor = grid->AddLayer(Registry->RegisterType("SurvivingFrame"));
    ASSERT_NE(nullptr, removed);
    ASSERT_NE(nullptr, survivor);
    CKDWORD original = 0x10203040u;
    ASSERT_TRUE(survivor->SetValue2(0, 0, &original));
    const CK_ID removedID = removed->GetID(), survivorID = survivor->GetID();
    ASSERT_EQ(CK_OK, Context->DestroyObject(removed, CK_DESTROY_NONOTIFY));
    ASSERT_EQ(nullptr, Context->GetObject(removedID));
    ASSERT_EQ(2, grid->GetLayerCount());
    ASSERT_EQ(nullptr, grid->GetLayerByIndex(0));

    CKStateChunk *saved = grid->Save(nullptr, CK_STATESAVE_GRIDONLY | CK_STATESAVE_LAYERDATA);
    ASSERT_NE(nullptr, saved);
    saved->StartRead();
    ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_GRIDDATA));
    EXPECT_EQ(1, saved->ReadInt());
    EXPECT_EQ(1, saved->ReadInt());
    EXPECT_EQ(0, saved->ReadInt());
    EXPECT_EQ(0, saved->ReadInt());
    EXPECT_EQ(CKGRID_FREE, saved->ReadDword());
    XObjectArray references = saved->ReadXObjectArray();
    ASSERT_EQ(2, references.Size());
    EXPECT_EQ(0u, references[0]);
    EXPECT_EQ(survivorID, references[1]);
    CKStateChunk *first = saved->ReadSubChunk();
    EXPECT_EQ(nullptr, first);
    DeleteCKStateChunk(first);
    CKStateChunk *second = saved->ReadSubChunk();
    EXPECT_NE(nullptr, second);
    if (second) {
        second->StartRead();
        EXPECT_TRUE(second->SeekIdentifier(CK_STATESAVE_LAYERDATA));
        EXPECT_EQ(grid->GetID(), second->ReadObjectID());
        EXPECT_EQ(survivor->GetType(), second->ReadInt());
    }
    DeleteCKStateChunk(second);

    CKDWORD changed = 0x55667788u;
    ASSERT_TRUE(survivor->SetValue2(0, 0, &changed));
    EXPECT_EQ(CK_OK, grid->Load(saved, nullptr));
    CKDWORD actual = 0;
    ASSERT_TRUE(survivor->GetValue2(0, 0, &actual));
    EXPECT_EQ(original, actual);
    EXPECT_EQ(1, grid->GetLayerCount());
    EXPECT_EQ(survivor, grid->GetLayerByIndex(0));
    EXPECT_EQ(grid->GetID(), survivor->GetOwner());
    DeleteCKStateChunk(saved);
}

} // namespace
