#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKSceneGraph.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Entity3dHierFrustumTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_ViewportData.ViewX = 10;
        Device->m_ViewportData.ViewY = 20;
        Device->m_ViewportData.ViewWidth = 640;
        Device->m_ViewportData.ViewHeight = 640;
        Device->m_ViewportData.ViewZMin = 0;
        Device->m_ViewportData.ViewZMax = 1;
        VxMatrix projection;
        projection.Perspective(1.57079632679489661923f, 1, 1, 9);
        Device->SetProjectionTransformationMatrix(projection);
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        // No GPU: the engine's real CPU box projection must classify the
        // same valid view/projection as its non-null-device path. No visibility
        // implementation or manually synchronised private frustum lives here.
        ASSERT_EQ(nullptr, Device->m_RasterizerContext);
    }

    void TearDown() override {
        delete Device;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    static CK3dEntity *API(RCK3dEntity *entity) {
        return reinterpret_cast<CK3dEntity *>(entity);
    }

    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }

    RCKMesh *BoxMesh(float halfX = 0.25f, float halfY = 0.25f, float halfZ = 0.25f) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_TRUE(mesh->SetVertexCount(8));
        for (int i = 0; i < 8; ++i) {
            VxVector point((i & 1) ? halfX : -halfX,
                           (i & 2) ? halfY : -halfY,
                           (i & 4) ? halfZ : -halfZ);
            mesh->SetVertexPosition(i, &point);
        }
        mesh->VertexMove();
        return mesh;
    }

    RCK3dEntity *BoxEntity(float x, float y, float z) {
        RCK3dEntity *entity = Entity();
        entity->SetCurrentMesh(BoxMesh());
        entity->SetPosition3f(x, y, z);
        return entity;
    }

    static void ExpectBox(const VxBbox &box, float minX, float minY, float minZ,
                          float maxX, float maxY, float maxZ) {
        EXPECT_FLOAT_EQ(minX, box.Min.x); EXPECT_FLOAT_EQ(minY, box.Min.y); EXPECT_FLOAT_EQ(minZ, box.Min.z);
        EXPECT_FLOAT_EQ(maxX, box.Max.x); EXPECT_FLOAT_EQ(maxY, box.Max.y); EXPECT_FLOAT_EQ(maxZ, box.Max.z);
    }

    static void ExpectState(RCK3dEntity *entity, CKDWORD state) {
        ASSERT_NE(nullptr, entity->m_SceneGraphNode);
        EXPECT_EQ(state, entity->m_SceneGraphNode->GetFlags() & CKSGN_FRUSTUM_MASK);
    }
};

TEST_F(Entity3dHierFrustumTest, EntireMeshHierarchyInsideSetsTraversalFlagAndCachesActualWorldUnion) {
    RCK3dEntity *parent = BoxEntity(0, 0, 5), *child = BoxEntity(1, 0, 5);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    CKSceneGraphNode *node = parent->m_SceneGraphNode;
    ASSERT_NE(nullptr, node);
    node->SetFlags(CKSGN_OUTSIDEFRUSTUM | CKSGN_INTRANSPARENTLIST);
    parent->m_RenderExtents = VxRect(11, 22, 33, 44);
    ASSERT_TRUE(parent->IsInViewFrustrumHierarchic(Device));
    ExpectState(parent, CKSGN_INSIDEFRUSTUM);
    EXPECT_TRUE(node->CheckHierarchyFrustum()); // Actual scene traversal consumes this flag.
    EXPECT_FALSE(node->IsAllOutsideFrustum());
    EXPECT_TRUE(node->IsHierarchyBoxComputed()); EXPECT_TRUE(node->IsHierarchyBoxValid());
    EXPECT_TRUE(node->IsInTransparentList());
    ExpectBox(node->m_Bbox, -0.25f, -0.25f, 4.75f, 1.25f, 0.25f, 5.25f);
    EXPECT_FLOAT_EQ(11, parent->m_RenderExtents.left); EXPECT_FLOAT_EQ(22, parent->m_RenderExtents.top);
    EXPECT_FLOAT_EQ(33, parent->m_RenderExtents.right); EXPECT_FLOAT_EQ(44, parent->m_RenderExtents.bottom);
    EXPECT_TRUE(parent->IsInViewFrustrumHierarchic(Device));
    ExpectState(parent, CKSGN_INSIDEFRUSTUM);
}

TEST_F(Entity3dHierFrustumTest, EachCompletelyOutsideClipPlaneRejectsHierarchyAndClearsPreviousInsideBit) {
    const VxVector centers[6] = {VxVector(-20, 0, 5), VxVector(20, 0, 5),
                               VxVector(0, -20, 5), VxVector(0, 20, 5),
                               VxVector(0, 0, 0), VxVector(0, 0, 20)};
    RCK3dEntity *parent = BoxEntity(0, 0, 5), *child = BoxEntity(0, 0, 5);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    for (int i = 0; i < 6; ++i) {
        SCOPED_TRACE(i);
        parent->SetPosition(&centers[i], nullptr, TRUE);
        child->SetPosition(&centers[i]);
        parent->m_SceneGraphNode->SetFlags(CKSGN_INSIDEFRUSTUM);
        EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device));
        ExpectState(parent, CKSGN_OUTSIDEFRUSTUM);
        EXPECT_TRUE(parent->m_SceneGraphNode->IsAllOutsideFrustum());
        EXPECT_FALSE(parent->m_SceneGraphNode->CheckHierarchyFrustum());
    }
}

TEST_F(Entity3dHierFrustumTest, OutsideParentWithVisibleChildRetainsIntersectingUnionWithoutInsideShortcut) {
    RCK3dEntity *parent = BoxEntity(12, 0, 3), *child = BoxEntity(0, 0, 3);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    parent->m_SceneGraphNode->SetFlags(CKSGN_FRUSTUM_MASK);
    ASSERT_TRUE(parent->IsInViewFrustrumHierarchic(Device));
    ExpectBox(parent->m_SceneGraphNode->m_Bbox, -0.25f, -0.25f, 2.75f, 12.25f, 0.25f, 3.25f);
    ExpectState(parent, 0);
    EXPECT_FALSE(parent->m_SceneGraphNode->CheckHierarchyFrustum());
    EXPECT_FALSE(parent->m_SceneGraphNode->IsAllOutsideFrustum());
}

TEST_F(Entity3dHierFrustumTest, GrandchildContributesDespiteMeshlessIntermediateAndUsesWorldCoordinates) {
    RCK3dEntity *parent = BoxEntity(0, 0, 5), *middle = Entity(), *grandchild = BoxEntity(20, 0, 5);
    middle->SetPosition3f(100, 50, -10);
    ASSERT_TRUE(middle->SetParent(API(parent), TRUE));
    ASSERT_TRUE(grandchild->SetParent(API(middle), TRUE));
    ASSERT_TRUE(parent->IsInViewFrustrumHierarchic(Device));
    ExpectBox(parent->m_SceneGraphNode->m_Bbox, -0.25f, -0.25f, 4.75f, 20.25f, 0.25f, 5.25f);
    ExpectState(parent, 0);
    ExpectBox(middle->m_SceneGraphNode->m_Bbox, 19.75f, -0.25f, 4.75f, 20.25f, 0.25f, 5.25f);
    EXPECT_TRUE(middle->m_SceneGraphNode->IsHierarchyBoxValid());
}

TEST_F(Entity3dHierFrustumTest, MovingChildInvalidatesAncestorBoxAndReclassifiesOutsideThenInside) {
    RCK3dEntity *parent = Entity(), *child = BoxEntity(20, 0, 5);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_OUTSIDEFRUSTUM);
    child->SetPosition3f(0, 0, 5);
    EXPECT_FALSE(parent->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_TRUE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_INSIDEFRUSTUM);
    ExpectBox(parent->m_SceneGraphNode->m_Bbox, -0.25f, -0.25f, 4.75f, 0.25f, 0.25f, 5.25f);
    child->SetPosition3f(0, 0, 20);
    EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_OUTSIDEFRUSTUM);
}

TEST_F(Entity3dHierFrustumTest, ReplacingChildMeshInvalidatesUnionAndNewExtentPreventsFalseWholeHierarchyInside) {
    RCK3dEntity *parent = Entity(), *child = BoxEntity(0, 0, 5);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    ASSERT_TRUE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_INSIDEFRUSTUM);
    child->SetCurrentMesh(BoxMesh(12, 0.25f, 0.25f));
    EXPECT_FALSE(parent->m_SceneGraphNode->IsHierarchyBoxComputed());
    ASSERT_TRUE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, 0);
    ExpectBox(parent->m_SceneGraphNode->m_Bbox, -12, -0.25f, 4.75f, 12, 0.25f, 5.25f);
}

TEST_F(Entity3dHierFrustumTest, RemovingVisibleChildChangesIntersectingHierarchyIntoOutsideParent) {
    RCK3dEntity *parent = BoxEntity(20, 0, 5), *child = BoxEntity(0, 0, 5);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    ASSERT_TRUE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, 0);
    ASSERT_TRUE(child->SetParent(nullptr, TRUE));
    EXPECT_FALSE(parent->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_OUTSIDEFRUSTUM);
    ExpectBox(parent->m_SceneGraphNode->m_Bbox, 19.75f, -0.25f, 4.75f, 20.25f, 0.25f, 5.25f);
    EXPECT_TRUE(child->IsInViewFrustrumHierarchic(Device)); ExpectState(child, CKSGN_INSIDEFRUSTUM);
}

TEST_F(Entity3dHierFrustumTest, NearAndFarPlaneTouchRemainVisibleWithClosedClipVolumeConvention) {
    RCK3dEntity *entity = Entity(); entity->SetCurrentMesh(BoxMesh(0.25f, 0.25f, 0));
    entity->SetPosition3f(0, 0, 1);
    EXPECT_TRUE(entity->IsInViewFrustrumHierarchic(Device)); ExpectState(entity, CKSGN_INSIDEFRUSTUM);
    entity->SetPosition3f(0, 0, 9);
    EXPECT_TRUE(entity->IsInViewFrustrumHierarchic(Device)); ExpectState(entity, CKSGN_INSIDEFRUSTUM);
}

TEST_F(Entity3dHierFrustumTest, PublicCameraMatricesReclassifyCachedWorldBoxWithoutRebuildingGeometry) {
    RCK3dEntity *parent = BoxEntity(30, 0, 5), *child = BoxEntity(31, 0, 5);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    ASSERT_FALSE(parent->IsInViewFrustrumHierarchic(Device));
    const VxBbox cached = parent->m_SceneGraphNode->m_Bbox;
    VxMatrix view = VxMatrix::Identity(); view[3].Set(-30, 0, 0);
    Device->SetViewTransformationMatrix(view);
    ASSERT_TRUE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_INSIDEFRUSTUM);
    ExpectBox(parent->m_SceneGraphNode->m_Bbox, cached.Min.x, cached.Min.y, cached.Min.z,
              cached.Max.x, cached.Max.y, cached.Max.z);
    VxMatrix projection; projection.Perspective(1.57079632679489661923f, 1, 6, 9);
    Device->SetProjectionTransformationMatrix(projection);
    EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_OUTSIDEFRUSTUM);
    Device->SetViewTransformationMatrix(VxMatrix::Identity());
    projection.Perspective(1.57079632679489661923f, 1, 1, 9);
    Device->SetProjectionTransformationMatrix(projection);
    EXPECT_FALSE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, CKSGN_OUTSIDEFRUSTUM);
}

TEST_F(Entity3dHierFrustumTest, EntirelyMeshlessHierarchyIsConservativelyVisibleAndClearsStaleClipFlags) {
    RCK3dEntity *parent = Entity(), *child = Entity();
    parent->SetPosition3f(100, 100, -100); child->SetPosition3f(200, 200, -200);
    ASSERT_TRUE(child->SetParent(API(parent), TRUE));
    parent->m_SceneGraphNode->SetFlags(CKSGN_FRUSTUM_MASK | CKSGN_INTRANSPARENTLIST);
    EXPECT_TRUE(parent->IsInViewFrustrumHierarchic(Device)); ExpectState(parent, 0);
    EXPECT_TRUE(parent->m_SceneGraphNode->IsHierarchyBoxComputed());
    EXPECT_FALSE(parent->m_SceneGraphNode->IsHierarchyBoxValid());
    EXPECT_TRUE(parent->m_SceneGraphNode->IsInTransparentList());
    EXPECT_FALSE(parent->m_SceneGraphNode->CheckHierarchyFrustum());
}
} // namespace
