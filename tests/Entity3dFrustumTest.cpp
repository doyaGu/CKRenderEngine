#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKSceneGraph.h"
#include "CKRasterizer.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Consumer seam only: every operation other than matrix delivery is rejected.
// Culling and projection execute the actual engine and VxMath code.
class FrustumWorldSink final : public CKRasterizerContext {
public:
    int WorldCalls = 0;
    VxMatrix LastWorld = VxMatrix::Identity();
    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetOptions(const CKRasterizerOptions *Options) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKERROR GetDeviceStatus() const override { ADD_FAILURE() << "Unexpected rasterizer operation"; return CKERR_NOTINITIALIZED; }
    CKBOOL BeginShutdown() override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL IsIdle() const override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL BeginScene() override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL EndScene() override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL BeginOverlayPhase() override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL BackToFront(CKBOOL VSync) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override { if (Type == VXMATRIX_WORLD) { ++WorldCalls; LastWorld = Mat; } return TRUE; }
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetMaterial(const CKMaterialData *Data) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetViewport(const CKViewportData *Data) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    void InitDefaultRenderStatesValue() override { ADD_FAILURE() << "Unexpected rasterizer operation"; }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex, CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel, CKRST_CUBEFACE Face, const CKRECT *Region) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return nullptr; }
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return nullptr; }
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL DeleteObject(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return 0; }
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return 0; }
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback, void *User) override { ADD_FAILURE() << "Unexpected rasterizer operation"; return FALSE; }
};

class Entity3dFrustumTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *RenderContext = nullptr;
    FrustumWorldSink Sink;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        RenderContext = new RCKRenderContext(Context);
        RenderContext->m_ViewportData.ViewX = 10;
        RenderContext->m_ViewportData.ViewY = 20;
        RenderContext->m_ViewportData.ViewWidth = 200;
        RenderContext->m_ViewportData.ViewHeight = 200;
        RenderContext->m_ViewportData.ViewZMin = 0;
        RenderContext->m_ViewportData.ViewZMax = 1;
        VxMatrix projection;
        projection.Perspective(PI / 2, 1, 1, 10);
        RenderContext->SetProjectionTransformationMatrix(projection);
        RenderContext->SetViewTransformationMatrix(VxMatrix::Identity());
        RenderContext->SetWorldTransformationMatrix(VxMatrix::Identity());
    }

    void TearDown() override {
        if (RenderContext) {
            RenderContext->m_RasterizerContext = nullptr;
            delete RenderContext;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCK3dEntity *Entity() {
        return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    }

    RCKMesh *Mesh(const VxBbox &box) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        EXPECT_TRUE(mesh->SetVertexCount(2));
        VxVector min = box.Min, max = box.Max;
        mesh->SetVertexPosition(0, &min);
        mesh->SetVertexPosition(1, &max);
        mesh->VertexMove();
        return mesh;
    }

    static VxBbox Box(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
        VxBbox box;
        box.Min.Set(minX, minY, minZ);
        box.Max.Set(maxX, maxY, maxZ);
        return box;
    }

    RCK3dEntity *UserBox(const VxBbox &box) {
        RCK3dEntity *entity = Entity();
        EXPECT_TRUE(entity->SetBoundingBox(&box, TRUE));
        entity->UpdateBox(TRUE);
        EXPECT_NE(nullptr, entity->m_SceneGraphNode);
        entity->m_SceneGraphNode->SetAsPotentiallyVisible();
        return entity;
    }

    static void Rect(const VxRect &rect, float left, float top, float right, float bottom) {
        // The scalar/SIMD reciprocal implementations need not round identically.
        EXPECT_NEAR(left, rect.left, .06f);
        EXPECT_NEAR(top, rect.top, .06f);
        EXPECT_NEAR(right, rect.right, .06f);
        EXPECT_NEAR(bottom, rect.bottom, .06f);
    }

    static void NodeFlags(RCK3dEntity *entity, CKDWORD flags) {
        ASSERT_NE(nullptr, entity->m_SceneGraphNode);
        EXPECT_EQ(flags, entity->m_SceneGraphNode->GetFlags() & CKSGN_FRUSTUM_MASK);
    }
};

TEST_F(Entity3dFrustumTest, UserBoxInsideProjectsActualViewportAndSetsInsideAndExtentFlags) {
    RCK3dEntity *entity = UserBox(Box(-.5f, -.5f, 2.5f, .5f, .5f, 3.5f));
    entity->m_RenderExtents = VxRect(1, 2, 3, 4);
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    Rect(entity->m_RenderExtents, 90, 100, 130, 140);
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
    EXPECT_TRUE(entity->GetMoveableFlags() & VX_MOVEABLE_EXTENTSUPTODATE);
}

TEST_F(Entity3dFrustumTest, UserBoxesOutsideEachOfSixPlanesAreRejectedWithOutsideCache) {
    const VxBbox boxes[6] = {
        Box(-8, -.2f, 2.5f, -7, .2f, 3),
        Box(7, -.2f, 2.5f, 8, .2f, 3),
        Box(-.2f, -8, 2.5f, .2f, -7, 3),
        Box(-.2f, 7, 2.5f, .2f, 8, 3),
        Box(-.2f, -.2f, .2f, .2f, .2f, .4f),
        Box(-.2f, -.2f, 11, .2f, .2f, 12)
    };
    for (int i = 0; i < 6; ++i) {
        SCOPED_TRACE(i);
        RCK3dEntity *entity = UserBox(boxes[i]);
        EXPECT_FALSE(entity->IsInViewFrustrum(RenderContext, 1));
        NodeFlags(entity, CKSGN_OUTSIDEFRUSTUM);
        EXPECT_TRUE(entity->GetMoveableFlags() & VX_MOVEABLE_EXTENTSUPTODATE);
    }
}

TEST_F(Entity3dFrustumTest, SidePlaneIntersectionIsVisibleWithoutWholeInsideCache) {
    RCK3dEntity *entity = UserBox(Box(2, -.5f, 2.5f, 4, .5f, 3.5f));
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    NodeFlags(entity, 0);
    EXPECT_NEAR(10 + 100 * (1 + 2.0f / 3.5f), entity->m_RenderExtents.left, .06f);
    EXPECT_NEAR(270, entity->m_RenderExtents.right, .06f);
}

TEST_F(Entity3dFrustumTest, CrossingNearPlaneIsVisibleAndProjectsRealBoxCorners) {
    RCK3dEntity *entity = UserBox(Box(-.2f, -.2f, .5f, .2f, .2f, 1.5f));
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    NodeFlags(entity, 0);
    Rect(entity->m_RenderExtents, 70, 80, 150, 160);
}

TEST_F(Entity3dFrustumTest, CrossingFarPlaneIsVisibleWithoutWholeInsideCache) {
    RCK3dEntity *entity = UserBox(Box(-.2f, -.2f, 9, .2f, .2f, 11));
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    NodeFlags(entity, 0);
    Rect(entity->m_RenderExtents, 110 - 20.0f / 9, 120 - 20.0f / 9,
         110 + 20.0f / 9, 120 + 20.0f / 9);
}

TEST_F(Entity3dFrustumTest, LowByteZeroKeepsUserBoxExtentsButMarksTheirCacheAsUpdated) {
    RCK3dEntity *entity = UserBox(Box(-.5f, -.5f, 2.5f, .5f, .5f, 3.5f));
    entity->m_RenderExtents = VxRect(1, 2, 3, 4);
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 0x10000));
    Rect(entity->m_RenderExtents, 1, 2, 3, 4);
    EXPECT_TRUE(entity->GetMoveableFlags() & VX_MOVEABLE_EXTENTSUPTODATE);
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
}

TEST_F(Entity3dFrustumTest, NonIdentityViewAndWorldComposeBeforeClipAndScreenProjection) {
    RCK3dEntity *entity = UserBox(Box(-.5f, -.5f, 2.5f, .5f, .5f, 3.5f));
    entity->SetPosition3f(5, 0, 0);
    VxMatrix view = VxMatrix::Identity();
    view[3].Set(-5, 0, 0);
    RenderContext->SetViewTransformationMatrix(view);
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    Rect(entity->m_RenderExtents, 90, 100, 130, 140);
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
    entity->SetPosition3f(12, 0, 0);
    entity->m_SceneGraphNode->SetAsPotentiallyVisible();
    EXPECT_FALSE(entity->IsInViewFrustrum(RenderContext, 0));
    NodeFlags(entity, CKSGN_OUTSIDEFRUSTUM);
}

TEST_F(Entity3dFrustumTest, CurrentMeshBoundsDriveVisibleInsideAndTranslatedOutsideTests) {
    RCK3dEntity *entity = Entity();
    entity->SetCurrentMesh(Mesh(Box(-.5f, -.5f, 2.5f, .5f, .5f, 3.5f)));
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    Rect(entity->m_RenderExtents, 90, 100, 130, 140);
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
    entity->SetPosition3f(8, 0, 0);
    entity->m_SceneGraphNode->SetAsPotentiallyVisible();
    EXPECT_FALSE(entity->IsInViewFrustrum(RenderContext, 0));
    NodeFlags(entity, CKSGN_OUTSIDEFRUSTUM);
}

TEST_F(Entity3dFrustumTest, EmptyCurrentMeshRejectsEarlyAndRetainsNodeVisibilityFlags) {
    RCK3dEntity *entity = Entity();
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
    entity->SetCurrentMesh(mesh);
    entity->m_SceneGraphNode->SetAsInsideFrustum();
    EXPECT_FALSE(entity->IsInViewFrustrum(RenderContext, 1));
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
    EXPECT_FLOAT_EQ(100000000, entity->m_RenderExtents.left);
    EXPECT_FLOAT_EQ(-100000000, entity->m_RenderExtents.right);
    EXPECT_TRUE(entity->GetMoveableFlags() & VX_MOVEABLE_EXTENTSUPTODATE);
}

TEST_F(Entity3dFrustumTest, VertexMutationInvalidatesEntityAndAncestorBoxesBeforeClip) {
    RCK3dEntity *parent = Entity(), *entity = Entity();
    ASSERT_TRUE(entity->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
    RCKMesh *mesh = Mesh(Box(-.5f, -.5f, 2.5f, .5f, .5f, 3.5f));
    entity->SetCurrentMesh(mesh);
    entity->UpdateBox(TRUE);
    parent->m_SceneGraphNode->ComputeHierarchicalBox();
    ASSERT_TRUE(entity->m_MoveableFlags & VX_MOVEABLE_UPTODATE);
    ASSERT_TRUE(parent->m_SceneGraphNode->IsHierarchyBoxValid());
    VxVector newPosition(-1, -.5f, 2.5f);
    mesh->SetVertexPosition(0, &newPosition);
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 0));
    EXPECT_FALSE(entity->m_MoveableFlags & VX_MOVEABLE_UPTODATE);
    EXPECT_FALSE(parent->m_SceneGraphNode->IsHierarchyBoxValid());
}

TEST_F(Entity3dFrustumTest, SkinUsesBoneExpandedEntityBoxInsteadOfOnlyCurrentMeshBox) {
    RCK3dEntity *entity = Entity(), *bone = Entity();
    entity->SetCurrentMesh(Mesh(Box(7, -.2f, 2.5f, 8, .2f, 3)));
    entity->CreateSkin();
    RCKSkin *skin = static_cast<RCKSkin *>(entity->GetSkin());
    skin->SetBoneCount(1);
    ASSERT_EQ(1, skin->GetBoneCount());
    bone->SetPosition3f(0, 0, 3);
    skin->GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(bone));
    entity->UpdateBox(TRUE);
    EXPECT_FALSE(entity->m_MoveableFlags & VX_MOVEABLE_USERBOX);
    EXPECT_LE(entity->GetBoundingBox(TRUE).Min.x, 0);
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    NodeFlags(entity, 0);
}

TEST_F(Entity3dFrustumTest, PointEntityAlwaysRefreshesItsOnePixelExtentEvenWithZeroFlags) {
    RCK3dEntity *entity = Entity();
    entity->SetPosition3f(0, 0, 3);
    entity->m_RenderExtents = VxRect(1, 2, 3, 4);
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 0));
    Rect(entity->m_RenderExtents, 110, 120, 111, 121);
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
    EXPECT_TRUE(entity->GetMoveableFlags() & VX_MOVEABLE_EXTENTSUPTODATE);
}

TEST_F(Entity3dFrustumTest, OutsidePointStillRefreshesExtentAndMarksNodeOutside) {
    RCK3dEntity *entity = Entity();
    entity->SetPosition3f(6, 0, 3);
    entity->m_RenderExtents = VxRect(1, 2, 3, 4);
    EXPECT_FALSE(entity->IsInViewFrustrum(RenderContext, 0));
    Rect(entity->m_RenderExtents, 310, 120, 311, 121);
    NodeFlags(entity, CKSGN_OUTSIDEFRUSTUM);
}

TEST_F(Entity3dFrustumTest, ClearViewportUsesExistingContextWorldAndSuppressesOnlyBoxWorldDelivery) {
    RCK3dEntity *entity = UserBox(Box(-.5f, -.5f, 2.5f, .5f, .5f, 3.5f));
    entity->SetPosition3f(8, 0, 0);
    RenderContext->m_RasterizerContext = &Sink;
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, CK_RENDER_CLEARVIEWPORT | 1));
    EXPECT_EQ(0, Sink.WorldCalls);
    Rect(entity->m_RenderExtents, 90, 100, 130, 140);
    EXPECT_FALSE(entity->IsInViewFrustrum(RenderContext, 1));
    EXPECT_EQ(1, Sink.WorldCalls);
    EXPECT_FLOAT_EQ(8, Sink.LastWorld[3].x);
    EXPECT_FLOAT_EQ(8, RenderContext->GetWorldTransformationMatrix()[3].x);
}

TEST_F(Entity3dFrustumTest, PointEntityAlwaysDeliversItsWorldEvenDuringClearViewport) {
    RCK3dEntity *entity = Entity();
    entity->SetPosition3f(.3f, 0, 3);
    VxMatrix unrelated = VxMatrix::Identity();
    unrelated[3].Set(40, 0, 3);
    RenderContext->SetWorldTransformationMatrix(unrelated);
    RenderContext->m_RasterizerContext = &Sink;
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, CK_RENDER_CLEARVIEWPORT));
    EXPECT_EQ(1, Sink.WorldCalls);
    EXPECT_FLOAT_EQ(.3f, Sink.LastWorld[3].x);
    EXPECT_FLOAT_EQ(3, Sink.LastWorld[3].z);
    EXPECT_FLOAT_EQ(.3f, RenderContext->GetWorldTransformationMatrix()[3].x);
    Rect(entity->m_RenderExtents, 120, 120, 121, 121);
}

TEST_F(Entity3dFrustumTest, ClipBoundaryPointIsInsideRatherThanDiscarded) {
    RCK3dEntity *entity = Entity();
    entity->SetPosition3f(0, 0, 1);
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 1));
    Rect(entity->m_RenderExtents, 110, 120, 111, 121);
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
}

TEST_F(Entity3dFrustumTest, RotatedViewMapsWorldPointToHandCalculatedPerspectiveScreenPosition) {
    RCK3dEntity *entity = Entity();
    entity->SetPosition3f(-3, 0, .3f);
    VxMatrix view = VxMatrix::Identity();
    view[0].Set(0, 0, -1);
    view[2].Set(1, 0, 0);
    RenderContext->SetViewTransformationMatrix(view);
    // Row-vector world (-3, 0, .3) becomes camera (.3, 0, 3).
    // 90-degree perspective then gives x/w=.1 at viewport center (110,120).
    EXPECT_TRUE(entity->IsInViewFrustrum(RenderContext, 0));
    Rect(entity->m_RenderExtents, 120, 120, 121, 121);
    NodeFlags(entity, CKSGN_INSIDEFRUSTUM);
}

TEST_F(Entity3dFrustumTest, TransformUsesWorldThenRotatedViewThenPerspectiveAndHonorsReferenceOverride) {
    RCK3dEntity *reference = Entity();
    reference->SetPosition3f(-4, .5f, .5f);
    VxMatrix view = VxMatrix::Identity();
    view[0].Set(0, 0, -1);
    view[2].Set(1, 0, 0);
    RenderContext->SetViewTransformationMatrix(view);
    RenderContext->SetWorldTransformationMatrix(reference->GetWorldMatrix());
    VxVector source(0, .5f, .5f), screen;
    // world(-4,1,1) -> camera(1,1,4) -> clip(1,1,10/3,4).
    // Viewport (10,20,200,200) maps this to (135,95,5/6).
    RenderContext->Transform(&screen, &source);
    EXPECT_NEAR(135, screen.x, .0001f);
    EXPECT_NEAR(95, screen.y, .0001f);
    EXPECT_NEAR(5.0f / 6, screen.z, .0001f);
    VxMatrix unrelated = VxMatrix::Identity();
    unrelated[3].Set(100, 0, 3);
    RenderContext->SetWorldTransformationMatrix(unrelated);
    RenderContext->Transform(&source, &source, reinterpret_cast<CK3dEntity *>(reference));
    EXPECT_NEAR(135, source.x, .0001f);
    EXPECT_NEAR(95, source.y, .0001f);
    EXPECT_NEAR(5.0f / 6, source.z, .0001f);
    EXPECT_FLOAT_EQ(-4, RenderContext->GetWorldTransformationMatrix()[3].x);
    VxVector original(0, .5f, .5f);
    RenderContext->Transform(&screen, &original);
    EXPECT_NEAR(135, screen.x, .0001f);
    EXPECT_NEAR(95, screen.y, .0001f);
    EXPECT_NEAR(5.0f / 6, screen.z, .0001f);
}

TEST_F(Entity3dFrustumTest, TransformVerticesHonorsIndependentStridesClipAggregationAndOutputCanaries) {
    struct Input { VxVector Position; CKDWORD Canary; };
    struct Homogeneous { VxVector4 Position; CKDWORD Canary; };
    struct Screen { VxVector4 Position; CKDWORD Canary[2]; };
    Input input[2];
    Homogeneous homogeneous[2];
    Screen screen[2];
    const CKDWORD canary = 0xA1B2C3D4;
    for (int i = 0; i < 2; ++i) {
        input[i].Canary = canary;
        homogeneous[i].Canary = canary;
        screen[i].Canary[0] = screen[i].Canary[1] = canary;
    }
    input[0].Position.Set(0, .5f, .5f);
    input[1].Position.Set(0, .5f, 5.5f);
    CKDWORD clip[2] = {0xFFFFFFFF, 0xFFFFFFFF};
    VxTransformData data = {};
    data.InVertices = &input[0].Position;
    data.InStride = sizeof(Input);
    data.OutVertices = &homogeneous[0].Position;
    data.OutStride = sizeof(Homogeneous);
    data.ScreenVertices = &screen[0].Position;
    data.ScreenStride = sizeof(Screen);
    data.ClipFlags = clip;
    VxMatrix world = VxMatrix::Identity();
    world[3].Set(-4, .5f, .5f);
    VxMatrix view = VxMatrix::Identity();
    view[0].Set(0, 0, -1);
    view[2].Set(1, 0, 0);
    RenderContext->SetWorldTransformationMatrix(world);
    RenderContext->SetViewTransformationMatrix(view);
    RenderContext->TransformVertices(2, &data);
    // Camera points (1,1,4) and (6,1,4), derived independently from inputs.
    EXPECT_NEAR(1, homogeneous[0].Position.x, .0001f);
    EXPECT_NEAR(6, homogeneous[1].Position.x, .0001f);
    for (int i = 0; i < 2; ++i) {
        EXPECT_NEAR(1, homogeneous[i].Position.y, .0001f);
        EXPECT_NEAR(10.0f / 3, homogeneous[i].Position.z, .0001f);
        EXPECT_NEAR(4, homogeneous[i].Position.w, .0001f);
        EXPECT_NEAR(95, screen[i].Position.y, .0001f);
        EXPECT_NEAR(5.0f / 6, screen[i].Position.z, .0001f);
        EXPECT_NEAR(.25f, screen[i].Position.w, .0001f);
        EXPECT_EQ(canary, input[i].Canary);
        EXPECT_EQ(canary, homogeneous[i].Canary);
        EXPECT_EQ(canary, screen[i].Canary[0]);
        EXPECT_EQ(canary, screen[i].Canary[1]);
    }
    EXPECT_NEAR(135, screen[0].Position.x, .0001f);
    EXPECT_NEAR(260, screen[1].Position.x, .0001f);
    EXPECT_EQ(0u, clip[0]);
    EXPECT_EQ(static_cast<CKDWORD>(VXCLIP_RIGHT), clip[1]);
    EXPECT_EQ(0u, data.m_Offscreen);
    EXPECT_EQ(135, data.m_2dExtents.left);
    EXPECT_EQ(95, data.m_2dExtents.top);
    EXPECT_EQ(260, data.m_2dExtents.right);
    EXPECT_EQ(95, data.m_2dExtents.bottom);

    input[0].Position.z = 9.5f; // camera(10,1,4), now both are right of x=w.
    RenderContext->TransformVertices(2, &data);
    EXPECT_EQ(static_cast<CKDWORD>(VXCLIP_RIGHT), clip[0]);
    EXPECT_EQ(static_cast<CKDWORD>(VXCLIP_RIGHT), clip[1]);
    EXPECT_EQ(static_cast<CKDWORD>(VXCLIP_RIGHT), data.m_Offscreen);
    EXPECT_NEAR(360, screen[0].Position.x, .0001f);
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(canary, input[i].Canary);
        EXPECT_EQ(canary, homogeneous[i].Canary);
        EXPECT_EQ(canary, screen[i].Canary[0]);
        EXPECT_EQ(canary, screen[i].Canary[1]);
    }
}

TEST_F(Entity3dFrustumTest, TransformVerticesReferencePersistsWorldForLaterNullReferenceAndEmptyJobsKeepIt) {
    RCK3dEntity *reference = Entity(), *unusedReference = Entity();
    reference->SetPosition3f(-3, 0, .3f);
    unusedReference->SetPosition3f(-7, 0, 0);
    VxMatrix view = VxMatrix::Identity();
    view[0].Set(0, 0, -1);
    view[2].Set(1, 0, 0);
    RenderContext->SetViewTransformationMatrix(view);
    VxMatrix unrelated = VxMatrix::Identity();
    unrelated[3].Set(100, 0, 3);
    RenderContext->SetWorldTransformationMatrix(unrelated);
    VxVector input[2] = {VxVector(0, 0, 0), VxVector(0, .3f, 0)};
    VxVector4 screen[2];
    CKDWORD clip[2] = {0xFFFFFFFF, 0xFFFFFFFF};
    VxTransformData data = {};
    data.InVertices = input;
    data.InStride = sizeof(VxVector);
    data.ScreenVertices = screen;
    data.ScreenStride = sizeof(VxVector4);
    data.ClipFlags = clip;
    RenderContext->TransformVertices(2, &data, reinterpret_cast<CK3dEntity *>(reference));
    EXPECT_FLOAT_EQ(-3, RenderContext->GetWorldTransformationMatrix()[3].x);
    EXPECT_FLOAT_EQ(.3f, RenderContext->GetWorldTransformationMatrix()[3].z);
    // Row-vector camera (.3,0,3), (.3,.3,3); 90-degree perspective.
    EXPECT_NEAR(120, screen[0].x, .0001f);
    EXPECT_NEAR(120, screen[0].y, .0001f);
    EXPECT_NEAR(120, screen[1].x, .0001f);
    EXPECT_NEAR(110, screen[1].y, .0001f);
    EXPECT_NEAR(20.0f / 27, screen[0].z, .0001f);
    EXPECT_NEAR(1.0f / 3, screen[0].w, .0001f);
    EXPECT_EQ(0u, clip[0]);
    EXPECT_EQ(0u, clip[1]);
    EXPECT_EQ(0u, data.m_Offscreen);
    screen[0].Set(-1, -1, -1, -1);
    screen[1].Set(-1, -1, -1, -1);
    RenderContext->TransformVertices(2, &data);
    EXPECT_NEAR(120, screen[0].x, .0001f);
    EXPECT_NEAR(120, screen[0].y, .0001f);
    EXPECT_NEAR(120, screen[1].x, .0001f);
    EXPECT_NEAR(110, screen[1].y, .0001f);
    RenderContext->TransformVertices(0, &data, reinterpret_cast<CK3dEntity *>(unusedReference));
    RenderContext->TransformVertices(0, nullptr, reinterpret_cast<CK3dEntity *>(unusedReference));
    EXPECT_FLOAT_EQ(-3, RenderContext->GetWorldTransformationMatrix()[3].x);
    EXPECT_FLOAT_EQ(.3f, RenderContext->GetWorldTransformationMatrix()[3].z);
}
} // namespace
