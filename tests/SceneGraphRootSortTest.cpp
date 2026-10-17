#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKSceneGraph.h"
#include "CKRenderedScene.h"
#include "RCK3dEntity.h"
#include "RCKCamera.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKSprite3D.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
CKBOOL RegisteredSortDraw(CKRenderContext *, CKRenderObject *, void *) { return TRUE; }

struct SortTrace {
    int Count = 0;
    int Values[32] = {};
    CKDWORD Flags[32] = {};
    CKRenderContext *Contexts[32] = {};
    void Add(int value, CKRenderContext *context, CKDWORD flags) {
        EXPECT_LT(Count, 32);
        if (Count < 32) { Values[Count] = value; Contexts[Count] = context; Flags[Count++] = flags; }
    }
    void Reset() { Count = 0; }
};

// Observe only the virtual submission boundary. World transforms, local/world
// boxes, graph membership, stamps, queues and the production sort remain real.
class SortEntity : public RCK3dEntity {
public:
    SortEntity(CKContext *context, SortTrace &trace, int value)
        : RCK3dEntity(context), Trace(trace), Value(value) {}
    ~SortEntity() override {
        for (int i = 0; i < ViewCount; ++i) Views[i]->RemoveObject(this);
    }
    SortTrace &Trace;
    int Value, BoxReads = 0;
    RCKRenderContext *Views[2] = {};
    int ViewCount = 0;
    void TrackView(RCKRenderContext *view) { ASSERT_LT(ViewCount, 2); Views[ViewCount++] = view; }
    CKBOOL LastLocal = FALSE;
    CKBOOL Render(CKRenderContext *context, CKDWORD flags) override {
        Trace.Add(Value, context, flags); return TRUE;
    }
    const VxBbox &GetBoundingBox(CKBOOL local) override {
        ++BoxReads; LastLocal = local;
        return RCK3dEntity::GetBoundingBox(local);
    }
};

class SortSprite : public RCKSprite3D {
public:
    SortSprite(CKContext *context, SortTrace &trace) : RCKSprite3D(context), Trace(trace) {}
    ~SortSprite() override {
        for (int i = 0; i < ViewCount; ++i) Views[i]->RemoveObject(this);
    }
    SortTrace &Trace;
    RCKRenderContext *Views[2] = {};
    int ViewCount = 0;
    void TrackView(RCKRenderContext *view) { ASSERT_LT(ViewCount, 2); Views[ViewCount++] = view; }
    CKBOOL Render(CKRenderContext *context, CKDWORD flags) override {
        Trace.Add(3, context, flags); return TRUE;
    }
};

class SortMatrixBoundary : public MaterialRenderObserver {
public:
    int ViewReads = 0, ProjectionReads = 0;
    CKBOOL RejectView = FALSE, RejectProjection = FALSE;
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix) override {
        if (type == VXMATRIX_VIEW) ++ViewReads;
        if (type == VXMATRIX_PROJECTION) ++ProjectionReads;
        if ((type == VXMATRIX_VIEW && RejectView) ||
            (type == VXMATRIX_PROJECTION && RejectProjection)) {
            matrix.SetIdentity(); matrix[3].Set(1000, 2000, 3000, 1);
            return FALSE; // A refused query does not provide usable output.
        }
        return MaterialRenderObserver::GetTransformMatrix(type, matrix);
    }
};

class SceneGraphRootSortTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr, *Other = nullptr;
    SortTrace Trace;
    SortMatrixBoundary Boundary;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Manager->m_SortTransparentObjects.Value = TRUE;
        Device->m_TimeFpsCalc = 11;
        Device->m_ViewMatrix.SetIdentity(); Device->m_ProjectionMatrix.SetIdentity();
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Other) { Other->m_RasterizerContext = nullptr; delete Other; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    CKSceneGraphRootNode &Root() { return Manager->m_SceneGraphRootNode; }
    RCKRenderContext *SecondView() {
        Other = new RCKRenderContext(Context); Other->m_TimeFpsCalc = 22;
        Other->m_ViewMatrix.SetIdentity(); Other->m_ProjectionMatrix.SetIdentity();
        EXPECT_NE(Device->m_MaskFree, Other->m_MaskFree); return Other;
    }
    static void Place(RCK3dEntity &entity, float x, float y, float z, float radius = .01f) {
        VxMatrix world = VxMatrix::Identity(); world[3].Set(x, y, z, 1);
        entity.SetWorldMatrix(world);
        VxBbox box; box.Min.Set(-radius, -radius, -radius); box.Max.Set(radius, radius, radius);
        ASSERT_TRUE(entity.SetBoundingBox(&box, TRUE)); entity.UpdateBox(TRUE);
    }
    template <class EntityType>
    void Attach(EntityType &entity, RCKRenderContext *view = nullptr) {
        if (!view) view = Device;
        ASSERT_TRUE(entity.SetRenderCallBack(RegisteredSortDraw, nullptr));
        entity.SetRenderAsTransparent(TRUE);
        entity.TrackView(view);
        view->AddObject(&entity); view->AddRemoveSequence(FALSE);
        ASSERT_NE(nullptr, entity.m_SceneGraphNode);
        ASSERT_TRUE((entity.m_SceneGraphNode->m_RenderContextMask & view->m_MaskFree) != 0);
        ASSERT_TRUE(entity.IsToBeRendered()); ASSERT_TRUE(entity.IsToBeRenderedLast());
    }
    void Enqueue(RCK3dEntity &entity, RCKRenderContext *view = nullptr) {
        if (!view) view = Device;
        ASSERT_NE(nullptr, entity.m_SceneGraphNode);
        entity.m_SceneGraphNode->NoTestsTraversal(view, 0);
        EXPECT_EQ(view->m_TimeFpsCalc, entity.m_SceneGraphNode->m_TimeFpsCalc);
        EXPECT_TRUE(entity.m_SceneGraphNode->IsInTransparentList());
    }
    void Sort(RCKRenderContext *view = nullptr) { Root().SortTransparentObjects(view ? view : Device, 0x12345678u); }
    void Order(int first, int second, RCKRenderContext *view = nullptr) {
        if (!view) view = Device;
        ASSERT_EQ(2, Trace.Count); EXPECT_EQ(first, Trace.Values[0]); EXPECT_EQ(second, Trace.Values[1]);
        for (int i = 0; i < 2; ++i) {
            EXPECT_EQ(reinterpret_cast<CKRenderContext *>(view), Trace.Contexts[i]);
            EXPECT_EQ(0x12345678u, Trace.Flags[i]);
        }
    }
    const CKTransparentObject *Entry(const RCK3dEntity &entity) {
        for (int i = 0; i < Root().m_TransparentObjects.Size(); ++i)
            if (Root().m_TransparentObjects[i].m_Node == entity.m_SceneGraphNode)
                return &Root().m_TransparentObjects[i];
        return nullptr;
    }
    void Depth(const RCK3dEntity &entity, float minimum, float maximum) {
        const CKTransparentObject *entry = Entry(entity); ASSERT_NE(nullptr, entry);
        EXPECT_NEAR(minimum, entry->m_ZhMin, .0001f); EXPECT_NEAR(maximum, entry->m_ZhMax, .0001f);
    }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
            EXPECT_NEAR(expected[r][c], actual[r][c], .0001f);
    }
};

TEST_F(SceneGraphRootSortTest, EmptySingleAndDisabledQueuesKeepOrderWithoutProjectingBoxes) {
    Device->m_RasterizerContext = &Boundary;
    Sort(); EXPECT_EQ(0, Trace.Count); EXPECT_EQ(0, Boundary.ViewReads);
    SortEntity first(Context, Trace, 1), second(Context, Trace, 2);
    Place(first, 0, 0, 2); Place(second, 0, 0, 9); Attach(first); Attach(second);
    Enqueue(first); Sort(); ASSERT_EQ(1, Trace.Count); EXPECT_EQ(1, Trace.Values[0]);
    EXPECT_EQ(0x12345678u, Trace.Flags[0]); EXPECT_EQ(0, first.BoxReads);
    Trace.Reset(); Enqueue(second); Manager->m_SortTransparentObjects.Value = FALSE;
    Sort(); Order(1, 2); EXPECT_EQ(0, first.BoxReads); EXPECT_EQ(0, second.BoxReads);
    EXPECT_EQ(0, Boundary.ViewReads); EXPECT_EQ(0, Boundary.ProjectionReads);
}

TEST_F(SceneGraphRootSortTest, EnabledMultiSortRemovesForeignAndStaleEntriesAndAllowsOtherViewToRequeue) {
    RCKRenderContext *other = SecondView();
    SortEntity own(Context, Trace, 1), stale(Context, Trace, 2), foreign(Context, Trace, 4);
    Place(own, 0, 0, 2); Place(stale, 0, 0, 9); Place(foreign, 0, 0, 5);
    Attach(own); Attach(stale); Attach(foreign, other);
    --Device->m_TimeFpsCalc; Enqueue(stale); ++Device->m_TimeFpsCalc;
    Enqueue(own); Enqueue(foreign, other); ASSERT_EQ(3, Root().m_TransparentObjects.Size());
    Sort(); ASSERT_EQ(1, Trace.Count); EXPECT_EQ(1, Trace.Values[0]);
    ASSERT_EQ(1, Root().m_TransparentObjects.Size()); EXPECT_NE(nullptr, Entry(own));
    EXPECT_FALSE(stale.m_SceneGraphNode->IsInTransparentList());
    EXPECT_FALSE(foreign.m_SceneGraphNode->IsInTransparentList());
    EXPECT_EQ(0, stale.BoxReads); EXPECT_EQ(0, foreign.BoxReads); EXPECT_EQ(1, own.BoxReads);
    Trace.Reset(); Enqueue(foreign, other); ASSERT_EQ(2, Root().m_TransparentObjects.Size());
    Sort(other); ASSERT_EQ(1, Trace.Count); EXPECT_EQ(4, Trace.Values[0]);
    EXPECT_EQ(reinterpret_cast<CKRenderContext *>(other), Trace.Contexts[0]);
    EXPECT_FALSE(own.m_SceneGraphNode->IsInTransparentList());
    ASSERT_EQ(1, Root().m_TransparentObjects.Size()); EXPECT_NE(nullptr, Entry(foreign));
}

TEST_F(SceneGraphRootSortTest, DisabledSortPreservesForeignQueueButRemovesOnlyMatchingStaleEntries) {
    RCKRenderContext *other = SecondView(); Manager->m_SortTransparentObjects.Value = FALSE;
    SortEntity own(Context, Trace, 1), stale(Context, Trace, 2), foreign(Context, Trace, 4);
    Place(own, 0, 0, 2); Place(stale, 0, 0, 9); Place(foreign, 0, 0, 5);
    Attach(own); Attach(stale); Attach(foreign, other);
    --Device->m_TimeFpsCalc; Enqueue(stale); ++Device->m_TimeFpsCalc;
    Enqueue(foreign, other); Enqueue(own); Sort();
    ASSERT_EQ(1, Trace.Count); EXPECT_EQ(1, Trace.Values[0]);
    ASSERT_EQ(2, Root().m_TransparentObjects.Size()); EXPECT_NE(nullptr, Entry(foreign));
    EXPECT_TRUE(foreign.m_SceneGraphNode->IsInTransparentList()); EXPECT_FALSE(stale.m_SceneGraphNode->IsInTransparentList());
    Trace.Reset(); Sort(other); ASSERT_EQ(1, Trace.Count); EXPECT_EQ(4, Trace.Values[0]);
    EXPECT_EQ(2, Root().m_TransparentObjects.Size()); EXPECT_TRUE(own.m_SceneGraphNode->IsInTransparentList());
    EXPECT_EQ(0, own.BoxReads); EXPECT_EQ(0, stale.BoxReads); EXPECT_EQ(0, foreign.BoxReads);
}

TEST_F(SceneGraphRootSortTest, RealPriorityKeysTakePrecedenceOverTheOppositeDepthOrder) {
    SortEntity nearEntity(Context, Trace, 1), farEntity(Context, Trace, 2);
    Place(nearEntity, 0, 0, 2); Place(farEntity, 0, 0, 20); Attach(nearEntity); Attach(farEntity);
    nearEntity.m_SceneGraphNode->SetPriority(20, TRUE); farEntity.m_SceneGraphNode->SetPriority(-20, TRUE);
    Enqueue(farEntity); Enqueue(nearEntity); Sort(); Order(1, 2);
    Depth(nearEntity, 1.99f, 2.01f); Depth(farEntity, 19.99f, 20.01f);
}

TEST_F(SceneGraphRootSortTest, ProjectionUsesTheLocalBoxAndAppliesTheWorldTransformExactlyOnce) {
    SortEntity wide(Context, Trace, 1), farther(Context, Trace, 2);
    Place(wide, 0, 0, 5, 1); Place(farther, 0, 0, 8, .25f);
    VxMatrix world = wide.GetWorldMatrix(); world[2][2] = 2; wide.SetWorldMatrix(world); wide.UpdateBox(TRUE);
    Attach(wide); Attach(farther); Enqueue(wide); Enqueue(farther); Sort(); Order(2, 1);
    Depth(wide, 3, 7); Depth(farther, 7.75f, 8.25f);
    EXPECT_TRUE(wide.LastLocal); EXPECT_TRUE(farther.LastLocal);
    EXPECT_EQ(1, wide.BoxReads); EXPECT_EQ(1, farther.BoxReads);
    EXPECT_FLOAT_EQ(-1, wide.m_LocalBoundingBox.Min.z); EXPECT_FLOAT_EQ(1, wide.m_LocalBoundingBox.Max.z);
}

TEST_F(SceneGraphRootSortTest, DirectBackendViewWritesControlDepthWithoutChangingTheCpuCache) {
    SortEntity first(Context, Trace, 1), second(Context, Trace, 2);
    Place(first, 1, 0, 3); Place(second, 4, 0, 1); Attach(first); Attach(second);
    Device->m_RasterizerContext = &Boundary;
    Boundary.Matrices[VXMATRIX_VIEW][0].Set(0, 0, 1, 0);
    Boundary.Matrices[VXMATRIX_VIEW][2].Set(-1, 0, 0, 0);
    Enqueue(first); Enqueue(second); Sort(); Order(2, 1);
    Depth(first, .99f, 1.01f); Depth(second, 3.99f, 4.01f);
    Matrix(Device->m_ViewMatrix, VxMatrix::Identity()); EXPECT_GT(Boundary.ViewReads, 0);
}

TEST_F(SceneGraphRootSortTest, DirectBackendProjectiveWControlsNormalizedDepthWithoutChangingTheCpuCache) {
    SortEntity first(Context, Trace, 1), second(Context, Trace, 2);
    Place(first, 2, 0, 3); Place(second, 0, 0, 2); Attach(first); Attach(second);
    Device->m_RasterizerContext = &Boundary; Boundary.Matrices[VXMATRIX_PROJECTION][0][3] = 1;
    Enqueue(first); Enqueue(second); Sort(); Order(2, 1);
    Depth(first, 2.99f / 3.01f, 3.01f / 2.99f);
    Depth(second, 1.99f / 1.01f, 2.01f / .99f);
    Matrix(Device->m_ProjectionMatrix, VxMatrix::Identity()); EXPECT_GT(Boundary.ProjectionReads, 0);
}

TEST_F(SceneGraphRootSortTest, RefusedMatrixQueriesRetainKnownCpuMatricesAndAcceptTheOtherIndependentQuery) {
    SortEntity first(Context, Trace, 1), second(Context, Trace, 2);
    Place(first, 2, 0, 3); Place(second, 0, 0, 2); Attach(first); Attach(second);
    Device->m_RasterizerContext = &Boundary;
    Boundary.RejectView = Boundary.RejectProjection = TRUE;
    Enqueue(first); Enqueue(second); Sort(); Order(1, 2);
    Depth(first, 2.99f, 3.01f); Depth(second, 1.99f, 2.01f);
    Trace.Reset(); Boundary.RejectProjection = FALSE;
    Boundary.Matrices[VXMATRIX_PROJECTION][0][3] = 1;
    Sort(); Order(2, 1); Depth(first, 2.99f / 3.01f, 3.01f / 2.99f);
    EXPECT_GE(Boundary.ViewReads, 2); EXPECT_GE(Boundary.ProjectionReads, 2);
    Matrix(Device->m_ViewMatrix, VxMatrix::Identity()); Matrix(Device->m_ProjectionMatrix, VxMatrix::Identity());
}

TEST_F(SceneGraphRootSortTest, EqualDepthPartialOverlapIsStableWhileUnequalOverlapUsesTheDepthFallback) {
    SortEntity first(Context, Trace, 1), second(Context, Trace, 2);
    Place(first, 0, 0, 10, 2); Place(second, .1f, 0, 10, 2); Attach(first); Attach(second);
    Enqueue(first); Enqueue(second); Sort(); Order(1, 2);
    Depth(first, 8, 12); Depth(second, 8, 12);
    Trace.Reset(); Place(second, 0, 0, 11, 2); Sort(); Order(2, 1);
    Depth(second, 9, 13);
}

TEST_F(SceneGraphRootSortTest, OverlapClassifierUsesTheActualSceneRootEyePosition) {
    SortEntity first(Context, Trace, 1), second(Context, Trace, 2);
    Place(first, 0, 0, 10, 1); Place(second, 5, 0, 10, 1); Attach(first); Attach(second);
    Device->m_RenderedScene->GetRootEntity()->SetPosition3f(0, 0, 10);
    Enqueue(first); Enqueue(second); Sort(); Order(2, 1);
    Depth(first, 9, 11); Depth(second, 9, 11);
    Matrix(Device->m_ViewMatrix, VxMatrix::Identity());
}

TEST_F(SceneGraphRootSortTest, SortRefreshesBillboardDepthAfterCameraChangeAndPreservesOrientableMode) {
    SortSprite sprite(Context, Trace); SortEntity peer(Context, Trace, 2);
    RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    ASSERT_NE(nullptr, material);
    material->SetSourceBlend(VXBLEND_SRCALPHA); material->SetDestBlend(VXBLEND_INVSRCALPHA);
    material->EnableAlphaBlend(TRUE); material->EnableAlphaTest(FALSE);
    ASSERT_TRUE(material->IsAlphaTransparent());
    sprite.SetMaterial(reinterpret_cast<CKMaterial *>(material));
    sprite.ModifyMoveableFlags(0, VX_MOVEABLE_RENDERFIRST);
    ASSERT_EQ(0u, sprite.GetMoveableFlags() & VX_MOVEABLE_RENDERFIRST);
    ASSERT_TRUE(sprite.IsToBeRendered()); ASSERT_TRUE(sprite.IsToBeRenderedLast());
    Vx2DVector size(4, 2); sprite.SetSize(size); sprite.SetPosition3f(0, 0, 5);
    Place(peer, 0, 0, 20); Attach(sprite); Attach(peer); Enqueue(sprite); Enqueue(peer);
    VxMatrix camera = VxMatrix::Identity();
    camera[0].Set(0, 0, 1, 0); camera[2].Set(-1, 0, 0, 0);
    Device->m_RenderedScene->GetRootEntity()->SetWorldMatrix(camera);
    Sort(); Order(2, 3); Depth(sprite, 3, 7);
    const VxMatrix &facing = sprite.GetWorldMatrix();
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
        EXPECT_FLOAT_EQ(camera[r][c], facing[r][c]);
    EXPECT_FLOAT_EQ(0, facing[3][0]); EXPECT_FLOAT_EQ(0, facing[3][1]); EXPECT_FLOAT_EQ(5, facing[3][2]);
    VxMatrix fixed = VxMatrix::Identity(); fixed[0][0] = 2; fixed[1][1] = 3; fixed[3].Set(0, 0, 8, 1);
    sprite.SetMode(VXSPRITE3D_ORIENTABLE); sprite.SetWorldMatrix(fixed); Trace.Reset();
    Sort(); Order(2, 3); Matrix(sprite.GetWorldMatrix(), fixed); Depth(sprite, 8, 8);
}
}
