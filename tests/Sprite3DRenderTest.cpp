#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCKSprite3D.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "CKSceneGraph.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct CallbackObservation {
    RCKRenderContext *Device;
    RCKSprite3D *Sprite;
    RCKMaterial *Replacement;
    int Count = 0;
    int Order[4] = {};
    int VertexCounts[4] = {};
    bool Clear = false;
};
struct CallbackArgument { CallbackObservation *Trace; int Marker; };
CKBOOL ObserveSpriteCallback(CKRenderContext *device, CKRenderObject *object, void *argument) {
    CallbackArgument *entry = static_cast<CallbackArgument *>(argument);
    CallbackObservation &trace = *entry->Trace;
    EXPECT_EQ(trace.Device, device); EXPECT_EQ(reinterpret_cast<CKRenderObject *>(trace.Sprite), object);
    const int index = trace.Count++;
    if (index >= 4) { ADD_FAILURE() << "Unexpected callback count"; return FALSE; }
    trace.Order[index] = entry->Marker;
    RCKMaterial *material = static_cast<RCKMaterial *>(trace.Sprite->GetMaterial());
    trace.VertexCounts[index] = material && material->GetSprite3DBatch() ? material->GetSprite3DBatch()->m_Vertices.Size() : 0;
    if (trace.Clear) trace.Sprite->SetMaterial(nullptr);
    if (trace.Replacement) trace.Sprite->SetMaterial(trace.Replacement);
    return FALSE;
}
class Sprite3DRenderTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    RCKSprite3D *Sprite = nullptr;
    RCKMaterial *Material = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context); Device = new RCKRenderContext(Context); Device->DetachAll();
        VxRect viewport(0, 0, 200, 200); Device->SetViewRect(viewport);
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        VxMatrix projection; projection.Perspective(1.5707963267948966f, 1, 1, 20);
        Device->SetProjectionTransformationMatrix(projection);
        Device->SetWorldTransformationMatrix(VxMatrix::Identity()); Device->m_Flags = 0;
        Sprite = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
        Material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        ASSERT_NE(nullptr, Sprite); ASSERT_NE(nullptr, Material);
        Sprite->SetMaterial(Material); Sprite->SetMode(VXSPRITE3D_ORIENTABLE); Sprite->SetPosition3f(0, 0, 5);
    }
    void TearDown() override { delete Device; if (Context) CKCloseContext(Context); CKShutdown(); }
    void Cached() { Sprite->m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE; }
    static void Point(const CKVertex &vertex, float x, float y, float z) {
        EXPECT_NEAR(x, vertex.V.x, .00001f); EXPECT_NEAR(y, vertex.V.y, .00001f);
        EXPECT_NEAR(z, vertex.V.z, .00001f); EXPECT_FLOAT_EQ(1, vertex.V.w);
    }
};
}

TEST_F(Sprite3DRenderTest, RenderEligibilityRequiresOwnVisibleBitAndMaterial) {
    Sprite->SetMaterial(nullptr); EXPECT_FALSE(Sprite->IsToBeRendered());
    Sprite->SetMaterial(Material); EXPECT_TRUE(Sprite->IsToBeRendered());
    Sprite->m_MoveableFlags &= ~VX_MOVEABLE_VISIBLE; EXPECT_FALSE(Sprite->IsToBeRendered());
    Sprite->m_MoveableFlags |= VX_MOVEABLE_VISIBLE; EXPECT_TRUE(Sprite->IsToBeRendered());
}
TEST_F(Sprite3DRenderTest, TransparentOrderingUsesMaterialUnlessRenderFirstIsSet) {
    Material->EnableAlphaBlend(TRUE); Material->SetDestBlend(VXBLEND_INVSRCALPHA); EXPECT_TRUE(Sprite->IsToBeRenderedLast());
    Sprite->m_MoveableFlags |= VX_MOVEABLE_RENDERFIRST; EXPECT_FALSE(Sprite->IsToBeRenderedLast());
    Sprite->m_MoveableFlags &= ~VX_MOVEABLE_RENDERFIRST; Material->EnableAlphaBlend(FALSE);
    EXPECT_FALSE(Sprite->IsToBeRenderedLast()); Sprite->SetMaterial(nullptr); EXPECT_FALSE(Sprite->IsToBeRenderedLast());
}
TEST_F(Sprite3DRenderTest, FillBatchCreatesFourIndependentWorldVerticesAndMappedUVs) {
    Vx2DVector size(4, 6), offset(1, -1); Sprite->SetSize(size); Sprite->SetOffset(offset);
    VxRect uv(.2f, .3f, .7f, .9f); Sprite->SetUVMapping(uv);
    VxMatrix matrix = VxMatrix::Identity(); matrix[0].Set(0, 2, 0); matrix[1].Set(-3, 0, 0); matrix[3].Set(10, 20, 30);
    Sprite->SetWorldMatrix(matrix); Sprite->GetWorldMatrix();
    CKSprite3DBatch batch; Sprite->FillBatch(&batch); ASSERT_EQ(4, batch.m_Vertices.Size());
    Point(batch.m_Vertices[0], 28, 20, 30); Point(batch.m_Vertices[1], 10, 20, 30);
    Point(batch.m_Vertices[2], 10, 28, 30); Point(batch.m_Vertices[3], 28, 28, 30);
    const float u[4] = {.2f, .2f, .7f, .7f}, v[4] = {.9f, .3f, .3f, .9f};
    for (int i = 0; i < 4; ++i) { EXPECT_FLOAT_EQ(u[i], batch.m_Vertices[i].tu); EXPECT_FLOAT_EQ(v[i], batch.m_Vertices[i].tv); }
    EXPECT_EQ(0, batch.m_Indices.Size()); EXPECT_EQ(0u, batch.m_VertexCount); EXPECT_EQ(0u, batch.m_IndexCount);
}
TEST_F(Sprite3DRenderTest, FillBatchAppendsWithoutReplacingEarlierVerticesOrIndices) {
    CKSprite3DBatch batch; batch.m_Vertices.Resize(1); batch.m_Vertices[0].V.Set(7, 8, 9, 1);
    batch.m_Indices.PushBack(11); batch.m_Flags = 0x40; Sprite->FillBatch(&batch);
    ASSERT_EQ(5, batch.m_Vertices.Size()); Point(batch.m_Vertices[0], 7, 8, 9);
    ASSERT_EQ(1, batch.m_Indices.Size()); EXPECT_EQ(11, batch.m_Indices[0]); EXPECT_EQ(0x40u, batch.m_Flags & 0x40);
    Point(batch.m_Vertices[1], -1, -1, 5); Point(batch.m_Vertices[4], 1, -1, 5);
}
TEST_F(Sprite3DRenderTest, FillBatchClippingBitIsAccumulatedFromRealSceneNode) {
    ASSERT_NE(nullptr, Sprite->m_SceneGraphNode); CKSprite3DBatch batch;
    Sprite->m_SceneGraphNode->SetAsInsideFrustum(); Sprite->FillBatch(&batch); EXPECT_EQ(0u, batch.m_Flags & 1);
    Sprite->m_SceneGraphNode->SetAsPotentiallyVisible();
    Sprite->m_SceneGraphNode->SetAsOutsideFrustum(); Sprite->FillBatch(&batch); EXPECT_EQ(1u, batch.m_Flags & 1);
    Sprite->m_SceneGraphNode->SetAsInsideFrustum(); Sprite->FillBatch(&batch); EXPECT_EQ(1u, batch.m_Flags & 1);
}
TEST_F(Sprite3DRenderTest, MaterialBatchQueuesWorldQuadBeforeDrawStageIndices) {
    Cached(); ASSERT_TRUE(Sprite->Render(Device, 0)); ASSERT_EQ(1, Device->m_Sprite3DBatches.Size());
    EXPECT_EQ(Material, Device->m_Sprite3DBatches[0]); CKSprite3DBatch *batch = Material->GetSprite3DBatch();
    ASSERT_NE(nullptr, batch); ASSERT_EQ(4, batch->m_Vertices.Size()); EXPECT_EQ(0, batch->m_Indices.Size());
    Point(batch->m_Vertices[0], -1, -1, 5); Point(batch->m_Vertices[2], 1, 1, 5);
    EXPECT_EQ(0, Device->m_ObjectExtents.Size());
}
TEST_F(Sprite3DRenderTest, RepeatedSameMaterialRenderAppendsQuadsToOnePendingBatch) {
    Cached(); ASSERT_TRUE(Sprite->Render(Device, 0)); ASSERT_TRUE(Sprite->Render(Device, 0));
    ASSERT_EQ(1, Device->m_Sprite3DBatches.Size()); ASSERT_NE(nullptr, Material->GetSprite3DBatch());
    EXPECT_EQ(8, Material->GetSprite3DBatch()->m_Vertices.Size()); EXPECT_EQ(0, Material->GetSprite3DBatch()->m_Indices.Size());
}
TEST_F(Sprite3DRenderTest, NoMaterialStillReturnsTrueWithoutQueueing) {
    Sprite->SetMaterial(nullptr); Cached(); EXPECT_TRUE(Sprite->Render(Device, 0));
    EXPECT_EQ(0, Device->m_Sprite3DBatches.Size());
}
TEST_F(Sprite3DRenderTest, PreThenPostCallbacksSurroundRealBatchAppendAndIgnoreFalseReturn) {
    Cached(); CallbackObservation trace = {Device, Sprite, nullptr};
    CallbackArgument pre = {&trace, 10}, post = {&trace, 30}, replacement = {&trace, 20};
    ASSERT_TRUE(Sprite->AddPreRenderCallBack(ObserveSpriteCallback, &pre, FALSE));
    ASSERT_TRUE(Sprite->AddPostRenderCallBack(ObserveSpriteCallback, &post, FALSE));
    Sprite->SetRenderCallBack(ObserveSpriteCallback, &replacement);
    ASSERT_TRUE(Sprite->Render(Device, 0)); ASSERT_EQ(2, trace.Count);
    EXPECT_EQ(10, trace.Order[0]); EXPECT_EQ(30, trace.Order[1]); EXPECT_EQ(0, trace.VertexCounts[0]); EXPECT_EQ(4, trace.VertexCounts[1]);
}
TEST_F(Sprite3DRenderTest, PreCallbackMaterialReplacementControlsQueuedGeometry) {
    Cached(); RCKMaterial *replacement = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr, replacement);
    CallbackObservation trace = {Device, Sprite, replacement}; CallbackArgument pre = {&trace, 10};
    ASSERT_TRUE(Sprite->AddPreRenderCallBack(ObserveSpriteCallback, &pre, FALSE)); ASSERT_TRUE(Sprite->Render(Device, 0));
    ASSERT_EQ(1, trace.Count); ASSERT_EQ(1, Device->m_Sprite3DBatches.Size()); EXPECT_EQ(replacement, Device->m_Sprite3DBatches[0]);
    EXPECT_EQ(nullptr, Material->GetSprite3DBatch());
}
TEST_F(Sprite3DRenderTest, PreCallbackCanRemoveMaterialAndPostStillRuns) {
    Cached(); CallbackObservation trace = {Device, Sprite, nullptr}; trace.Clear = true;
    CallbackArgument pre = {&trace, 10}, post = {&trace, 30};
    ASSERT_TRUE(Sprite->AddPreRenderCallBack(ObserveSpriteCallback, &pre, FALSE));
    ASSERT_TRUE(Sprite->AddPostRenderCallBack(ObserveSpriteCallback, &post, FALSE));
    ASSERT_TRUE(Sprite->Render(Device, 0)); ASSERT_EQ(2, trace.Count); EXPECT_EQ(0, Device->m_Sprite3DBatches.Size());
}
TEST_F(Sprite3DRenderTest, CachedCallbackWorldSynchronizationRespectsClearViewportFlag) {
    Cached(); CallbackObservation trace = {Device, Sprite, nullptr}; CallbackArgument pre = {&trace, 10};
    ASSERT_TRUE(Sprite->AddPreRenderCallBack(ObserveSpriteCallback, &pre, FALSE));
    VxMatrix caller = VxMatrix::Identity(); caller[3].Set(9, 8, 7); Device->SetWorldTransformationMatrix(caller);
    ASSERT_TRUE(Sprite->Render(Device, CK_RENDER_CLEARVIEWPORT)); EXPECT_FLOAT_EQ(7, Device->GetWorldTransformationMatrix()[3].z);
    ASSERT_TRUE(Sprite->Render(Device, 0)); EXPECT_FLOAT_EQ(5, Device->GetWorldTransformationMatrix()[3].z);
}
TEST_F(Sprite3DRenderTest, OutsideFrustumReturnsTrueButSkipsCallbacksBatchAndExtents) {
    Sprite->SetPosition3f(100, 0, 5); Sprite->m_MoveableFlags &= ~VX_MOVEABLE_EXTENTSUPTODATE;
    CallbackObservation trace = {Device, Sprite, nullptr}; CallbackArgument pre = {&trace, 10};
    ASSERT_TRUE(Sprite->AddPreRenderCallBack(ObserveSpriteCallback, &pre, FALSE));
    ASSERT_TRUE(Sprite->Render(Device, CKRENDER_UPDATEEXTENTS)); EXPECT_EQ(0, trace.Count);
    EXPECT_EQ(0, Device->m_Sprite3DBatches.Size()); EXPECT_EQ(0, Device->m_ObjectExtents.Size());
}
TEST_F(Sprite3DRenderTest, VisibleFrustumUpdatesAndAppendsActualScreenExtents) {
    Sprite->m_MoveableFlags &= ~VX_MOVEABLE_EXTENTSUPTODATE; ASSERT_TRUE(Sprite->Render(Device, CKRENDER_UPDATEEXTENTS));
    ASSERT_EQ(1, Device->m_ObjectExtents.Size()); const VxRect &rect = Device->m_ObjectExtents[0].m_Rect;
    EXPECT_LT(rect.left, rect.right); EXPECT_LT(rect.top, rect.bottom); EXPECT_NEAR(80, rect.left, .01f); EXPECT_NEAR(120, rect.right, .01f);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(Sprite), Device->m_ObjectExtents[0].m_Entity);
}
