#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKPlace.h"
#include "RCKMesh.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class PlaceFitToleranceTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKPlace, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        // Construct the manager before every entity so hierarchy queries use real scene nodes.
        new RCKRenderManager(Context); ASSERT_NE(nullptr, Context->GetRenderManager());
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *Entity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
    void Cube(RCK3dEntity &entity) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH)); ASSERT_NE(nullptr, mesh);
        ASSERT_TRUE(mesh->SetVertexCount(8)); int index = 0;
        for (int x = -1; x <= 1; x += 2) for (int y = -1; y <= 1; y += 2) for (int z = -1; z <= 1; z += 2) {
            VxVector vertex(500.0f * x, 500.0f * y, 500.0f * z); mesh->SetVertexPosition(index++, &vertex);
        }
        mesh->VertexMove(); entity.SetCurrentMesh(mesh);
        ASSERT_EQ(reinterpret_cast<CKMesh *>(mesh), entity.GetCurrentMesh());
    }
};
}

TEST_F(PlaceFitToleranceTest, LargeActualHierarchicalBoundsDoNotExpandWorldMatchingTolerance) {
    RCKPlace *first = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE));
    RCKPlace *second = static_cast<RCKPlace *>(Context->CreateObject(CKCID_PLACE));
    RCK3dEntity *firstChild = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    RCK3dEntity *secondChild = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, firstChild); ASSERT_NE(nullptr, secondChild);
    ASSERT_NE(nullptr, first->m_SceneGraphNode); ASSERT_NE(nullptr, second->m_SceneGraphNode);
    ASSERT_NE(nullptr, firstChild->m_SceneGraphNode); ASSERT_NE(nullptr, secondChild->m_SceneGraphNode);
    first->SetMoveableFlags(first->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    second->SetMoveableFlags(second->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    firstChild->SetMoveableFlags(firstChild->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    secondChild->SetMoveableFlags(secondChild->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    ASSERT_TRUE(firstChild->SetParent(Entity(*first), FALSE)); ASSERT_TRUE(secondChild->SetParent(Entity(*second), FALSE));
    Cube(*firstChild); Cube(*secondChild); second->SetPosition3f(0, 0, .04f);
    const VxBbox firstBox = first->GetHierarchicalBox(FALSE), secondBox = second->GetHierarchicalBox(FALSE);
    // These independent bounds checks exclude the headless point-box fallback as a reason for rejecting the fit.
    ASSERT_NEAR(-500.0f, firstBox.Min.x, .0001f); ASSERT_NEAR(500.0f, firstBox.Max.x, .0001f);
    ASSERT_NEAR(-500.0f, firstBox.Min.y, .0001f); ASSERT_NEAR(500.0f, firstBox.Max.y, .0001f);
    ASSERT_NEAR(-500.0f, firstBox.Min.z, .0001f); ASSERT_NEAR(500.0f, firstBox.Max.z, .0001f);
    ASSERT_NEAR(-500.0f, secondBox.Min.x, .0001f); ASSERT_NEAR(500.0f, secondBox.Max.x, .0001f);
    ASSERT_NEAR(-500.0f, secondBox.Min.y, .0001f); ASSERT_NEAR(500.0f, secondBox.Max.y, .0001f);
    ASSERT_NEAR(-499.96f, secondBox.Min.z, .0001f); ASSERT_NEAR(500.04f, secondBox.Max.z, .0001f);
    VxMatrix fitted = VxMatrix::Identity(); fitted[3].Set(7, 11, 13); const VxMatrix unchanged = fitted;
    EXPECT_FALSE(first->ComputeBestFitBBox(reinterpret_cast<CKPlace *>(second), fitted));
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        EXPECT_FLOAT_EQ(unchanged[row][column], fitted[row][column]);
}
