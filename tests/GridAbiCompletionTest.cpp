#include <gtest/gtest.h>
#include <cstddef>
#include <type_traits>
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
class GridProbe : public RCKGrid {
public:
    explicit GridProbe(CKContext *context, CKSTRING name = nullptr) : RCKGrid(context, name) {}
    RCKMesh *Display() const { return m_Mesh; }
    CKDWORD RawPriority() const { return m_Priority; }
    CK_ID LayerID(int index) const { return m_Layers[index]; }
    std::size_t Offset(int index) const {
        const void *fields[] = {&m_Layers, &m_Width, &m_Length, &m_Priority, &m_OrientationMode, &m_Mesh};
        return static_cast<const unsigned char *>(fields[index]) - reinterpret_cast<const unsigned char *>(this);
    }
};

class GridAbiCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    SerializationGridRegistry *Registry = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT);
        CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Registry = new SerializationGridRegistry(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    CKGrid *Grid(const char *name) {
        return CKGrid::Cast(Context->CreateObject(CKCID_GRID, const_cast<char *>(name)));
    }
};

TEST_F(GridAbiCompletionTest, RegisteredFactorySdkBridgeCombinesDefaultSlotsWithInheritedTransform) {
    CKGrid *grid = Grid("Factory grid"); ASSERT_NE(nullptr, grid);
    EXPECT_EQ(CKCID_GRID, grid->GetClassID()); EXPECT_EQ(50, RCKGrid::m_ClassID);
    EXPECT_EQ(0, grid->GetWidth()); EXPECT_EQ(0, grid->GetLength()); EXPECT_EQ(0, grid->GetGridPriority());
    EXPECT_EQ(CKGRID_FREE, grid->GetOrientationMode()); EXPECT_EQ(0, grid->GetLayerCount());
    EXPECT_FLOAT_EQ(10, grid->GetHeightValidity());
    CKObject *registered = Context->GetObject(grid->GetID()); ASSERT_NE(nullptr, registered);
    RCKGrid *implementation = static_cast<RCKGrid *>(registered);
    EXPECT_EQ(static_cast<void *>(grid), static_cast<void *>(registered));
    EXPECT_EQ(static_cast<void *>(implementation), static_cast<void *>(static_cast<CKObject *>(implementation)));
    EXPECT_STREQ("Factory grid", registered->GetName());
    grid->SetDimensions(3, 2, 6, 4); grid->SetHeightValidity(8);
    grid->SetGridPriority(-7); grid->SetOrientationMode(CKGRID_YZ);
    VxVector center; grid->Get3dPosFrom2dCoords(&center, 2, 1);
    EXPECT_FLOAT_EQ(5, center.x); EXPECT_FLOAT_EQ(0, center.y); EXPECT_FLOAT_EQ(3, center.z);
    VxVector point(5, 4, 3); int x = -1, z = -1;
    EXPECT_FLOAT_EQ(.5f, grid->Get2dCoordsFrom3dPos(&point, &x, &z)); EXPECT_EQ(2, x); EXPECT_EQ(1, z);
    EXPECT_EQ(3, grid->GetWidth()); EXPECT_EQ(2, grid->GetLength()); EXPECT_EQ(-7, grid->GetGridPriority());
    EXPECT_EQ(CKGRID_YZ, grid->GetOrientationMode());
}

TEST_F(GridAbiCompletionTest, IndependentGridWireRestoresScalarSlotsAndOrderedLayerIDsWithoutOwningObjects) {
    GridProbe grid(Context, "Wire grid"); grid.SetDimensions(2, 2, 4, 4); grid.SetHeightValidity(6);
    ASSERT_FLOAT_EQ(6, grid.GetHeightValidity());
    const int firstType = Registry->RegisterType("Field A"), secondType = Registry->RegisterType("Field B");
    CKLayer *first = grid.AddLayer(firstType), *second = grid.AddLayer(secondType);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    first->GetSquareArray()[0].ival = -7; second->GetSquareArray()[0].ival = 13;
    CKStateChunk chunk(CKCID_GRID, nullptr);
    chunk.StartWrite(); chunk.WriteIdentifier(CK_STATESAVE_GRIDDATA);
    chunk.WriteInt(4); chunk.WriteInt(3); chunk.WriteInt(0x11223344); chunk.WriteInt(-9); chunk.WriteDword(CKGRID_XY);
    chunk.WriteInt(1); chunk.StartObjectIDSequence(2); chunk.WriteObjectSequence(second); chunk.WriteObjectSequence(first);
    chunk.CloseChunk(); CKFile file(Context);
    CKObject *object = &grid; ASSERT_EQ(CK_OK, object->Load(&chunk, &file));
    EXPECT_EQ(4, grid.GetWidth()); EXPECT_EQ(3, grid.GetLength()); EXPECT_EQ(-9, grid.GetGridPriority());
    EXPECT_EQ(0xfffffff7u, grid.RawPriority()); EXPECT_EQ(CKGRID_XY, grid.GetOrientationMode());
    ASSERT_EQ(2, grid.GetLayerCount()); EXPECT_EQ(second->GetID(), grid.LayerID(0)); EXPECT_EQ(first->GetID(), grid.LayerID(1));
    EXPECT_EQ(second, grid.GetLayerByIndex(0)); EXPECT_EQ(first, grid.GetLayerByIndex(1));
    EXPECT_EQ(first, grid.GetLayer(firstType)); EXPECT_EQ(second, grid.GetLayerByName("Field B"));
    EXPECT_EQ(-7, first->GetSquareArray()[0].ival); EXPECT_EQ(13, second->GetSquareArray()[0].ival);
    EXPECT_EQ(grid.GetID(), first->GetOwner()); EXPECT_EQ(grid.GetID(), second->GetOwner());
    // The independent file chunk omits an entity matrix. Native base Load
    // applies its identity fallback, so inherited height becomes one.
    EXPECT_STREQ("Wire grid", object->GetName()); EXPECT_FLOAT_EQ(1, grid.GetHeightValidity());
    EXPECT_EQ(nullptr, grid.Display());
}

TEST_F(GridAbiCompletionTest, GeneratedDisplayHandleReusesItsMeshAndExplicitDestroyLeavesLayerDataUsable) {
    GridProbe grid(Context, "Display grid"); grid.SetDimensions(2, 1, 2, 1);
    const int type = Registry->RegisterType("Display cells");
    CKLayer *layer = grid.AddLayer(type); ASSERT_NE(nullptr, layer);
    layer->GetSquareArray()[0].ival = 17; layer->GetSquareArray()[1].ival = 29;
    grid.ConstructMeshTexture(.25f);
    RCKMesh *display = grid.Display(); ASSERT_NE(nullptr, display);
    const CK_ID meshId = display->GetID(), layerId = layer->GetID();
    EXPECT_EQ(display, Context->GetObject(meshId));
    EXPECT_EQ(static_cast<void *>(display), static_cast<void *>(grid.GetCurrentMesh()));
    grid.ConstructMeshTexture(.75f); EXPECT_EQ(display, grid.Display());
    grid.DestroyMeshTexture(); EXPECT_EQ(nullptr, grid.Display()); EXPECT_EQ(nullptr, Context->GetObject(meshId));
    EXPECT_EQ(layer, Context->GetObject(layerId)); EXPECT_EQ(1, grid.GetLayerCount());
    EXPECT_EQ(17, layer->GetSquareArray()[0].ival); EXPECT_EQ(29, layer->GetSquareArray()[1].ival);
    grid.ConstructMeshTexture(.5f); ASSERT_NE(nullptr, grid.Display());
    EXPECT_EQ(17, layer->GetSquareArray()[0].ival); grid.DestroyMeshTexture();
}

TEST_F(GridAbiCompletionTest, VirtualGridDestructionReleasesIDStorageAndKeepsContextOwnedLayersAndMeshAlive) {
    GridProbe *grid = new GridProbe(Context, "Borrowed resources"); grid->SetDimensions(2, 1, 2, 1);
    const int type = Registry->RegisterType("Surviving cells");
    CKLayer *layer = grid->AddLayer(type); ASSERT_NE(nullptr, layer); layer->GetSquareArray()[1].ival = 41;
    grid->ConstructMeshTexture(.5f); RCKMesh *mesh = grid->Display(); ASSERT_NE(nullptr, mesh);
    const CK_ID gridId = grid->GetID(), layerId = layer->GetID(), meshId = mesh->GetID();
    CKObject *object = grid; delete object;
    EXPECT_EQ(nullptr, Context->GetObject(gridId)); EXPECT_EQ(layer, Context->GetObject(layerId));
    EXPECT_EQ(mesh, Context->GetObject(meshId)); EXPECT_EQ(41, layer->GetSquareArray()[1].ival);
    EXPECT_GT(mesh->GetVertexCount(), 0); EXPECT_GT(mesh->GetFaceCount(), 0);
}

TEST_F(GridAbiCompletionTest, RegisteredIdentityRejectsCppCopyAndContextCloneKeepsIndependentNameAndState) {
    EXPECT_FALSE(std::is_copy_constructible<RCKGrid>::value); EXPECT_FALSE(std::is_copy_assignable<RCKGrid>::value);
    EXPECT_FALSE(std::is_move_constructible<RCKGrid>::value); EXPECT_FALSE(std::is_move_assignable<RCKGrid>::value);
    CKGrid *source = Grid("Grid source"); ASSERT_NE(nullptr, source);
    source->SetDimensions(3, 2, 6, 4); source->SetGridPriority(-7); source->SetOrientationMode(CKGRID_YZ);
    source->SetHeightValidity(8);
    CKObject *sourceObject = Context->GetObject(source->GetID()); ASSERT_NE(nullptr, sourceObject);
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_NONE;
    CKObject *copyObject = Context->CopyObject(sourceObject, &dependencies);
    ASSERT_NE(nullptr, copyObject); CKGrid *copy = CKGrid::Cast(copyObject); ASSERT_NE(nullptr, copy);
    EXPECT_NE(source->GetID(), copy->GetID()); EXPECT_NE(sourceObject, copyObject);
    EXPECT_EQ(sourceObject, Context->GetObject(source->GetID())); EXPECT_EQ(copyObject, Context->GetObject(copy->GetID()));
    EXPECT_EQ(3, copy->GetWidth()); EXPECT_EQ(2, copy->GetLength()); EXPECT_EQ(-7, copy->GetGridPriority());
    EXPECT_EQ(CKGRID_YZ, copy->GetOrientationMode()); EXPECT_FLOAT_EQ(8, copy->GetHeightValidity());
    copy->SetName("Grid clone"); copy->SetDimensions(5, 4, 10, 8); copy->SetGridPriority(12);
    EXPECT_STREQ("Grid source", source->GetName()); EXPECT_EQ(3, source->GetWidth()); EXPECT_EQ(2, source->GetLength());
    EXPECT_EQ(-7, source->GetGridPriority());
    const CK_ID copyId = copy->GetID(); delete copyObject;
    EXPECT_EQ(nullptr, Context->GetObject(copyId)); EXPECT_EQ(sourceObject, Context->GetObject(source->GetID()));
    EXPECT_STREQ("Grid source", source->GetName()); EXPECT_EQ(3, source->GetWidth());
}

TEST_F(GridAbiCompletionTest, CurrentCompilerLayoutRecordsAllFieldsAndTheExistingSdkBridgeBoundary) {
    GridProbe grid(Context, "Grid layout"); const bool wide = sizeof(void *) == 8;
    const std::size_t expected[] = {wide ? 552u : 424u, wide ? 576u : 436u, wide ? 580u : 440u,
                                    wide ? 584u : 444u, wide ? 588u : 448u, wide ? 592u : 452u};
    const char *names[] = {"m_Layers", "m_Width", "m_Length", "m_Priority", "m_OrientationMode", "m_Mesh"};
    RecordProperty("RCKGrid_size", static_cast<int>(sizeof(RCKGrid)));
    RecordProperty("RCKGrid_alignment", static_cast<int>(alignof(RCKGrid)));
    RecordProperty("RCK3dEntity_size", static_cast<int>(sizeof(RCK3dEntity)));
    RecordProperty("XObjectArray_size", static_cast<int>(sizeof(XObjectArray)));
    RecordProperty("XObjectArray_alignment", static_cast<int>(alignof(XObjectArray)));
    RecordProperty("pointer_size", static_cast<int>(sizeof(void *)));
    EXPECT_EQ(wide ? 600u : 456u, sizeof(RCKGrid)); EXPECT_EQ(wide ? 8u : 4u, alignof(RCKGrid));
    EXPECT_EQ(wide ? 552u : 424u, sizeof(RCK3dEntity)); EXPECT_EQ(wide ? 24u : 12u, sizeof(XObjectArray));
    EXPECT_EQ(alignof(void *), alignof(XObjectArray)); EXPECT_EQ(sizeof(RCKGrid), sizeof(GridProbe));
    for (int i = 0; i < 6; ++i) { EXPECT_EQ(expected[i], grid.Offset(i)) << names[i]; RecordProperty(names[i], static_cast<int>(grid.Offset(i))); }
    RecordProperty("m_ClassID_size", static_cast<int>(sizeof(RCKGrid::m_ClassID)));
    RecordProperty("m_ClassID_value", static_cast<int>(RCKGrid::m_ClassID));
    EXPECT_EQ(4u, sizeof(RCKGrid::m_ClassID)); EXPECT_EQ(50, RCKGrid::m_ClassID);
    EXPECT_FALSE((std::is_base_of<CKGrid, RCKGrid>::value));
    EXPECT_EQ(static_cast<void *>(&grid), static_cast<void *>(static_cast<CKObject *>(&grid)));
}
} // namespace
