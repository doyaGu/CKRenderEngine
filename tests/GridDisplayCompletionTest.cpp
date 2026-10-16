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
class GridDisplayCompletionTest : public ::testing::Test {
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

    RCKGrid *Grid(int width) {
        RCKGrid *grid = static_cast<RCKGrid *>(Context->CreateObject(CKCID_GRID, "Display sum"));
        if (grid)
            grid->SetDimensions(width, 1, static_cast<float>(width), 1);
        return grid;
    }

    CKLayer *Layer(RCKGrid &grid, const char *name, VxColor color) {
        const int type = Registry->RegisterType(const_cast<char *>(name));
        Registry->SetAssociatedColor(type, &color);
        return grid.AddLayer(type);
    }

    static void Pixels(RCKGrid &grid, const CKDWORD *expected, int cells) {
        grid.ConstructMeshTexture(.5f);
        CKMesh *mesh = grid.GetCurrentMesh();
        ASSERT_NE(nullptr, mesh);
        CKMaterial *material = mesh->GetFaceMaterial(0);
        ASSERT_NE(nullptr, material);
        CKTexture *texture = material->GetTexture();
        ASSERT_NE(nullptr, texture);
        ASSERT_EQ(16, texture->GetWidth());
        ASSERT_EQ(16, texture->GetHeight());
        const CKDWORD *pixels = reinterpret_cast<const CKDWORD *>(texture->LockSurfacePtr());
        ASSERT_NE(nullptr, pixels);
        for (int cell = 0; cell < cells; ++cell) {
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x)
                    EXPECT_EQ(expected[cell], pixels[y * 16 + cell * 2 + x]) << cell;
            }
        }
        texture->ReleaseSurfacePtr();
    }
};

TEST_F(GridDisplayCompletionTest, LargePositiveLayerSumSaturatesTheFinalRgbChannels) {
    RCKGrid *grid = Grid(1);
    ASSERT_NE(nullptr, grid);
    CKLayer *first = Layer(*grid, "First", VxColor(1.0f, 1.0f, 1.0f));
    CKLayer *second = Layer(*grid, "Second", VxColor(1.0f, 1.0f, 1.0f));
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    int value = 1 << 30;
    first->SetValue(0, 0, &value);
    second->SetValue(0, 0, &value);
    // Two legal positive int cells sum to 2^31. Saturation yields white;
    // an overflowing signed32 accumulator incorrectly paints the cell black.
    const CKDWORD expected[] = {0xffffffffu};
    Pixels(*grid, expected, 1);
}

TEST_F(GridDisplayCompletionTest, LayerSumsCancelBeforeSaturationAndKeepPerLayerTruncation) {
    RCKGrid *grid = Grid(3);
    ASSERT_NE(nullptr, grid);
    const char *names[] = {"Positive A", "Positive B", "Negative A", "Negative B", "Residual"};
    const int firstCell[] = {1 << 30, 1 << 30, -(1 << 30), -(1 << 30), 129};
    for (int layer = 0; layer < 5; ++layer) {
        CKLayer *cells = Layer(*grid, names[layer], VxColor(1.0f, .5f, .25f));
        ASSERT_NE(nullptr, cells);
        int value = firstCell[layer];
        cells->SetValue(0, 0, &value);
        value = 1;
        cells->SetValue(1, 0, &value);
        value = -1;
        cells->SetValue(2, 0, &value);
    }
    // Large contributions cancel to129,64,32. Fractions truncate per layer,
    // and the native signed-channel packing remains defined modulo2^32.
    const CKDWORD expected[] = {0xff814020u, 0xff050000u, 0xfffb0000u};
    Pixels(*grid, expected, 3);
}
} // namespace
