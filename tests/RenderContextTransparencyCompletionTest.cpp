#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCK3dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct RenderOrder {
    int Count = 0;
    int Values[16] = {};
    CKDWORD Flags[16] = {};
    void Reset() { Count = 0; }
};

// Observe only the final virtual Render boundary. Real entity boxes, world
// transforms, projection and both engine sorting paths remain in use.
class TransparentEntity : public RCK3dEntity {
public:
    TransparentEntity(CKContext *context, RenderOrder &order, int value)
        : RCK3dEntity(context), Order(order), Value(value) {}
    RenderOrder &Order;
    int Value, BoxReads = 0;
    CKBOOL Render(CKRenderContext *, CKDWORD flags) override {
        EXPECT_LT(Order.Count, 16);
        if (Order.Count < 16) {
            Order.Values[Order.Count] = Value;
            Order.Flags[Order.Count++] = flags;
        }
        return TRUE;
    }
    const VxBbox &GetBoundingBox(CKBOOL local) override {
        ++BoxReads;
        return RCK3dEntity::GetBoundingBox(local);
    }
};

class RenderContextTransparencyCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    RenderOrder Order;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Manager->m_SortTransparentObjects.Value = TRUE;
        Device->m_ViewMatrix.SetIdentity();
        Device->m_ProjectionMatrix.SetIdentity();
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Place(TransparentEntity &entity, float x, float y, float z, float radius = .01f) {
        VxMatrix world = VxMatrix::Identity();
        world[3].Set(x, y, z, 1);
        entity.SetWorldMatrix(world);
        VxBbox box;
        box.Min.Set(-radius, -radius, -radius);
        box.Max.Set(radius, radius, radius);
        ASSERT_TRUE(entity.SetBoundingBox(&box, TRUE));
    }
    void Add(TransparentEntity &entity) { Device->m_TransparentObjects.PushBack(&entity); }
    void ExpectOrder(int first, int second, CKDWORD flags = 0x12345678u) {
        ASSERT_EQ(2, Order.Count);
        EXPECT_EQ(first, Order.Values[0]); EXPECT_EQ(second, Order.Values[1]);
        EXPECT_EQ(flags, Order.Flags[0]); EXPECT_EQ(flags, Order.Flags[1]);
    }
    void Sort() { Device->RenderTransparents(0x12345678u); }
    void SortScene(TransparentEntity &first, TransparentEntity &second) {
        CKSceneGraphRootNode &root = Manager->m_SceneGraphRootNode;
        for (int i = 0; i < root.m_TransparentObjects.Size(); ++i)
            root.m_TransparentObjects[i].m_Node->ClearInTransparentList();
        root.m_TransparentObjects.Resize(0);
        first.m_SceneGraphNode->m_RenderContextMask = Device->m_MaskFree;
        second.m_SceneGraphNode->m_RenderContextMask = Device->m_MaskFree;
        first.m_SceneGraphNode->m_TimeFpsCalc = Device->m_TimeFpsCalc;
        second.m_SceneGraphNode->m_TimeFpsCalc = Device->m_TimeFpsCalc;
        root.AddTransparentObject(first.m_SceneGraphNode);
        root.AddTransparentObject(second.m_SceneGraphNode);
        root.SortTransparentObjects(Device, 0x12345678u);
    }
    void PlanarPair(TransparentEntity &plane, TransparentEntity &box, int thinAxis, float translation) {
        const float s = .7071067811865475f;
        VxMatrix world = VxMatrix::Identity();
        VxBbox local;
        if (thinAxis == 2) {
            world[0].Set(s, 0, -s, 0); world[1].Set(0, 1, 0, 0); world[2].Set(s, 0, s, 0);
            local.Min.Set(-5, -1, 0); local.Max.Set(5, 1, 0);
        } else if (thinAxis == 0) {
            world[0].Set(s, 0, s, 0); world[1].Set(0, 1, 0, 0); world[2].Set(-s, 0, s, 0);
            local.Min.Set(0, -1, -5); local.Max.Set(0, 1, 5);
        } else {
            world[0].Set(-s, 0, s, 0); world[1].Set(s, 0, s, 0); world[2].Set(0, 1, 0, 0);
            local.Min.Set(-5, 0, -1); local.Max.Set(5, 0, 1);
        }
        world[3].Set(0, 0, 10 + translation, 1);
        plane.SetWorldMatrix(world);
        ASSERT_TRUE(plane.SetBoundingBox(&local, TRUE));
        Place(box, -8, 0, 10 + translation, .25f);
        // The box is entirely on the camera side of the slanted plane, while
        // their projected Z ranges overlap. The plane must be drawn first.
        Device->m_RenderedScene->GetRootEntity()->SetPosition3f(0, 0, translation);
        Device->m_ViewMatrix.SetIdentity(); Device->m_ViewMatrix[3][2] = -translation;
    }
};
}

TEST_F(RenderContextTransparencyCompletionTest, EmptySingletonAndDisabledSortDoNotQueryBoxesOrLoseFlags) {
    Sort(); EXPECT_EQ(0, Order.Count);
    TransparentEntity nearEntity(Context, Order, 1), farEntity(Context, Order, 2);
    Place(nearEntity, 0, 0, 1); Place(farEntity, 0, 0, 10);
    Add(nearEntity); Sort(); ASSERT_EQ(1, Order.Count); EXPECT_EQ(1, Order.Values[0]);
    EXPECT_EQ(0x12345678u, Order.Flags[0]); EXPECT_EQ(0, nearEntity.BoxReads);
    Order.Reset(); Add(farEntity); Manager->m_SortTransparentObjects.Value = FALSE;
    Sort(); ExpectOrder(1, 2); EXPECT_EQ(0, nearEntity.BoxReads); EXPECT_EQ(0, farEntity.BoxReads);
}

TEST_F(RenderContextTransparencyCompletionTest, PriorityPrecedesDepthAndEqualDepthOverlapsKeepSubmissionOrder) {
    TransparentEntity nearEntity(Context, Order, 1), farEntity(Context, Order, 2);
    Place(nearEntity, 0, 0, 1); Place(farEntity, 0, 0, 10);
    nearEntity.m_SceneGraphNode->m_MaxPriority = -1;
    farEntity.m_SceneGraphNode->m_MaxPriority = 10000;
    Add(farEntity); Add(nearEntity); Sort(); ExpectOrder(1, 2);
    Order.Reset(); nearEntity.m_SceneGraphNode->m_MaxPriority = 10000;
    // Equal depth does not imply zero classifier: identical boxes satisfy
    // inclusive containment. Partial overlap avoids either box containing the
    // other, so the actual tie must preserve the submitted far/near order.
    Place(farEntity, .01f, 0, 1); Sort(); ExpectOrder(2, 1);
}

TEST_F(RenderContextTransparencyCompletionTest, NoncommutingViewAndProjectionSortByTheActualCameraDepth) {
    TransparentEntity first(Context, Order, 1), second(Context, Order, 2);
    Place(first, 2, 0, 0); Place(second, 0, 0, 3);
    const float s = .7071067811865475f;
    Device->m_ViewMatrix[0].Set(s, 0, s, 0);
    Device->m_ViewMatrix[2].Set(-s, 0, s, 0);
    Device->m_ViewMatrix[3][2] = 10;
    Device->m_ProjectionMatrix[0][0] = 10;
    // Camera depths are 10+2/sqrt(2) and 10+3/sqrt(2).
    // Projection changes only X, so entity 2 remains farther away.
    Add(first); Add(second); Sort(); ExpectOrder(2, 1);
}

TEST_F(RenderContextTransparencyCompletionTest, ProjectiveWIsPreservedWhenComparingNormalizedDepth) {
    TransparentEntity first(Context, Order, 1), second(Context, Order, 2);
    Place(first, 2, 0, 3); Place(second, 0, 0, 2);
    Device->m_ProjectionMatrix[0][3] = 1;
    // A custom projective transform gives z/w = 3/(2+1) and 2/(0+1).
    // Treating this projection as affine incorrectly places entity 1 first.
    Add(first); Add(second); Sort(); ExpectOrder(2, 1);
}

TEST_F(RenderContextTransparencyCompletionTest, DirectBackendMatrixWritesControlSortEvenWhenContextCachesDiffer) {
    TransparentEntity first(Context, Order, 1), second(Context, Order, 2);
    Place(first, 1, 0, 3); Place(second, 4, 0, 1);
    MaterialRenderObserver backend;
    backend.Matrices[VXMATRIX_VIEW][0].Set(0, 0, 1, 0);
    backend.Matrices[VXMATRIX_VIEW][2].Set(-1, 0, 0, 0);
    Device->m_RasterizerContext = &backend;
    Add(first); Add(second); Sort(); ExpectOrder(2, 1);
    EXPECT_FLOAT_EQ(1, Device->m_ViewMatrix[0][0]);
    Device->m_RasterizerContext = nullptr;
}

TEST_F(RenderContextTransparencyCompletionTest, PlanarOverlapOrderIsUnchangedByWorldTranslationForAllThinAxes) {
    TransparentEntity plane(Context, Order, 1), box(Context, Order, 2);
    Add(plane); Add(box);
    for (int axis = 0; axis != 3; ++axis) {
        SCOPED_TRACE(axis);
        PlanarPair(plane, box, axis, 0); Order.Reset(); Sort(); ExpectOrder(1, 2);
        PlanarPair(plane, box, axis, 100); Order.Reset(); Sort(); ExpectOrder(1, 2);
        Place(box, 8, 0, 110, .25f); Order.Reset(); Sort(); ExpectOrder(2, 1);
        Place(box, 0, 0, 110, .25f); Order.Reset(); Sort(); ExpectOrder(2, 1);
    }
}

TEST_F(RenderContextTransparencyCompletionTest, SceneGraphPlanarOverlapUsesTheSameTranslatedPlaneContractForAllThinAxes) {
    TransparentEntity plane(Context, Order, 1), box(Context, Order, 2);
    for (int axis = 0; axis != 3; ++axis) {
        SCOPED_TRACE(axis);
        PlanarPair(plane, box, axis, 0); Order.Reset(); SortScene(plane, box); ExpectOrder(1, 2);
        PlanarPair(plane, box, axis, 100); Order.Reset(); SortScene(plane, box); ExpectOrder(1, 2);
        Place(box, 8, 0, 110, .25f); Order.Reset(); SortScene(plane, box); ExpectOrder(2, 1);
        Place(box, 0, 0, 110, .25f); Order.Reset(); SortScene(plane, box); ExpectOrder(2, 1);
    }
}

TEST_F(RenderContextTransparencyCompletionTest, OverlappingSolidBoxesUseBoundingSeparationAndDepthFallback) {
    TransparentEntity nearEntity(Context, Order, 1), farEntity(Context, Order, 2);
    Place(nearEntity, 0, 0, 10, 2); Place(farEntity, 0, 0, 11, 2);
    Add(nearEntity); Add(farEntity);
    { SCOPED_TRACE("Context overlapping boxes"); Sort(); ExpectOrder(2, 1); }
    Order.Reset();
    { SCOPED_TRACE("SceneGraph overlapping boxes"); SortScene(nearEntity, farEntity); ExpectOrder(2, 1); }
    Order.Reset();
    Place(nearEntity, 0, 0, 10, 1); Place(farEntity, 5, 0, 10, 1);
    // A camera inside the near box and outside the separate peer is the
    // VxBbox classifier's definite -1 case; an external camera is inconclusive.
    Device->m_RenderedScene->GetRootEntity()->SetPosition3f(0, 0, 10);
    { SCOPED_TRACE("Context camera inside near box"); Sort(); ExpectOrder(2, 1); }
    Order.Reset();
    { SCOPED_TRACE("SceneGraph camera inside near box"); SortScene(nearEntity, farEntity); ExpectOrder(2, 1); }
}
