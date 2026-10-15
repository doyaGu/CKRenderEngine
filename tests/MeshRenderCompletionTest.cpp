#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "RCKRenderObject.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class CompletionObserver : public MaterialRenderObserver {
public:
    struct Draw {
        VXPRIMITIVETYPE Type;
        int Count;
        CKWORD First;
        CKDWORD ZWrite, ZFunc, Stencil, StencilPass, ColorWrite, Lighting, Wrap;
        CKDWORD Flags;
        VxVector Position;
    };
    Draw Draws[12] = {};
    int Count = 0;
    CKBOOL DrawResult = TRUE;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_LT(Count, 12);
        EXPECT_NE(nullptr, indices);
        EXPECT_NE(nullptr, data);
        if (Count >= 12 || !indices || !data) return FALSE;
        Draw &draw = Draws[Count++];
        draw.Type = type; draw.Count = count; draw.First = indices[0];
        draw.ZWrite = States[VXRENDERSTATE_ZWRITEENABLE];
        draw.ZFunc = States[VXRENDERSTATE_ZFUNC];
        draw.Stencil = States[VXRENDERSTATE_STENCILENABLE];
        draw.StencilPass = States[VXRENDERSTATE_STENCILPASS];
        draw.ColorWrite = States[VXRENDERSTATE_COLORWRITEENABLE];
        draw.Lighting = States[VXRENDERSTATE_LIGHTING];
        draw.Wrap = States[VXRENDERSTATE_WRAP0];
        draw.Flags = data->Flags;
        if (data->PositionPtr)
            draw.Position = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + indices[0] * data->PositionStride);
        return DrawResult;
    }
};

class MeshRenderCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CompletionObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State;
        Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Geometry(RCKMesh &mesh, int faces = 1) {
        ASSERT_TRUE(mesh.SetVertexCount(6));
        ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int i = 0; i < 6; ++i) {
            VxVector position(static_cast<float>(10 + i), static_cast<float>(i % 3), 0);
            mesh.SetVertexPosition(i, &position);
        }
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
        if (faces > 1) mesh.SetFaceVertexIndex(1, 3, 4, 5);
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
    void SeedState() {
        State.States[VXRENDERSTATE_ZWRITEENABLE] = 7;
        State.States[VXRENDERSTATE_ZFUNC] = VXCMP_GREATER;
        State.States[VXRENDERSTATE_STENCILENABLE] = 0;
        State.States[VXRENDERSTATE_STENCILPASS] = VXSTENCILOP_INVERT;
        State.States[VXRENDERSTATE_COLORWRITEENABLE] = 15;
        State.States[VXRENDERSTATE_LIGHTING] = 9;
    }
    void RestoredState() {
        EXPECT_EQ(7u, State.States[VXRENDERSTATE_ZWRITEENABLE]);
        EXPECT_EQ(VXCMP_GREATER, State.States[VXRENDERSTATE_ZFUNC]);
        EXPECT_EQ(0u, State.States[VXRENDERSTATE_STENCILENABLE]);
        EXPECT_EQ(VXSTENCILOP_INVERT, State.States[VXRENDERSTATE_STENCILPASS]);
        EXPECT_EQ(15u, State.States[VXRENDERSTATE_COLORWRITEENABLE]);
        EXPECT_EQ(9u, State.States[VXRENDERSTATE_LIGHTING]);
    }
};
}

TEST_F(MeshRenderCompletionTest, EmptyAndVertexOnlyMeshesDoNotSubmitOrIncrementDrawStatistics) {
    RCKMesh mesh(Context);
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr));
    ASSERT_TRUE(mesh.SetVertexCount(3));
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr));
    EXPECT_EQ(0, State.Count);
    EXPECT_EQ(0, Device->m_Stats.NbTrianglesDrawn);
    EXPECT_EQ(0, Device->m_Stats.NbLinesDrawn);
}

TEST_F(MeshRenderCompletionTest, DepthOnlyUsesColorMaskAndRestoresCallerState) {
    RCKMesh mesh(Context); RCK3dEntity entity(Context); Geometry(mesh); SeedState();
    entity.m_MoveableFlags |= VX_MOVEABLE_ZBUFONLY;
    EXPECT_EQ(1, mesh.DefaultRender(Device, &entity));
    ASSERT_EQ(1, State.Count);
    EXPECT_EQ(1u, State.Draws[0].ZWrite);
    EXPECT_EQ(VXCMP_LESSEQUAL, State.Draws[0].ZFunc);
    EXPECT_EQ(0u, State.Draws[0].Stencil);
    EXPECT_EQ(0u, State.Draws[0].ColorWrite);
    EXPECT_EQ(0u, State.Draws[0].Lighting);
    EXPECT_EQ(3, State.Draws[0].Count);
    RestoredState();
}

TEST_F(MeshRenderCompletionTest, StencilOnlyReplacesReferenceWithoutDepthOrColorWrites) {
    RCKMesh mesh(Context); RCK3dEntity entity(Context); Geometry(mesh); SeedState();
    entity.m_MoveableFlags |= VX_MOVEABLE_STENCILONLY;
    EXPECT_EQ(1, mesh.DefaultRender(Device, &entity));
    ASSERT_EQ(1, State.Count);
    EXPECT_EQ(0u, State.Draws[0].ZWrite);
    EXPECT_EQ(1u, State.Draws[0].Stencil);
    EXPECT_EQ(VXSTENCILOP_REPLACE, State.Draws[0].StencilPass);
    EXPECT_EQ(0u, State.Draws[0].ColorWrite);
    EXPECT_EQ(0u, State.Draws[0].Lighting);
    RestoredState();
}

TEST_F(MeshRenderCompletionTest, OpaqueGroupsPrecedeTransparentGroupsAndAuthoredLinesFollowBoth) {
    RCKMesh mesh(Context); RCKMaterial alpha(Context), opaque(Context); Geometry(mesh, 2);
    alpha.EnableAlphaBlend(TRUE); alpha.SetSourceBlend(VXBLEND_SRCALPHA);
    alpha.SetDestBlend(VXBLEND_INVSRCALPHA);
    mesh.SetFaceMaterial(0, &alpha); mesh.SetFaceMaterial(1, &opaque);
    ASSERT_TRUE(mesh.SetLineCount(1)); mesh.SetLine(0, 5, 1);
    EXPECT_EQ(1, mesh.DefaultRender(Device, nullptr));
    ASSERT_EQ(3, State.Count);
    EXPECT_EQ(VX_TRIANGLELIST, State.Draws[0].Type);
    EXPECT_EQ(VX_TRIANGLELIST, State.Draws[1].Type);
    // Material-group remapping may change submitted indices; consumed positions
    // distinguish the authored faces independently of that implementation choice.
    EXPECT_FLOAT_EQ(13.0f, State.Draws[0].Position.x);
    EXPECT_FLOAT_EQ(10.0f, State.Draws[1].Position.x);
    EXPECT_EQ(VX_LINELIST, State.Draws[2].Type);
    EXPECT_EQ(2, State.Draws[2].Count);
    EXPECT_FLOAT_EQ(15.0f, State.Draws[2].Position.x);
    EXPECT_EQ(2, Device->m_Stats.NbTrianglesDrawn);
    EXPECT_EQ(1, Device->m_Stats.NbLinesDrawn);
}

TEST_F(MeshRenderCompletionTest, LineOnlyDrawRetainsAuthoredEndpointAndVertexColors) {
    RCKMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(2));
    VxVector a(2, 4, 8), b(16, 32, 64);
    mesh.SetVertexPosition(0, &a); mesh.SetVertexPosition(1, &b);
    ASSERT_TRUE(mesh.SetLineCount(1)); mesh.SetLine(0, 1, 0); SeedState();
    EXPECT_EQ(1, mesh.DefaultRender(Device, nullptr));
    ASSERT_EQ(1, State.Count);
    EXPECT_EQ(VX_LINELIST, State.Draws[0].Type);
    EXPECT_FLOAT_EQ(16.0f, State.Draws[0].Position.x);
    EXPECT_NE(0u, State.Draws[0].Flags & CKRST_DP_DIFFUSE);
    EXPECT_EQ(0u, State.Draws[0].Lighting);
    RestoredState();
}

TEST_F(MeshRenderCompletionTest, BackendDrawRefusalKeepsNativePublicRenderReturnContract) {
    RCKMesh mesh(Context); Geometry(mesh); State.DrawResult = FALSE;
    EXPECT_EQ(CK_OK, mesh.Render(Device, nullptr));
    ASSERT_EQ(1, State.Count);
    EXPECT_EQ(1, Device->m_Stats.NbTrianglesDrawn);
}
