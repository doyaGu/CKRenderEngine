#include <gtest/gtest.h>
#include <cstring>
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
struct MeshTrace { int Count = 0; int Order[32] = {}; };
struct MeshCallbackArgument { MeshTrace *Trace; int Value; };
void MeshCallback(CKRenderContext *, CK3dEntity *, CKMesh *, void *argument) {
    MeshCallbackArgument *entry = static_cast<MeshCallbackArgument *>(argument);
    ASSERT_LT(entry->Trace->Count, 32);
    entry->Trace->Order[entry->Trace->Count++] = entry->Value;
}
void SubMeshCallback(CKRenderContext *, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    MeshCallback(nullptr, nullptr, nullptr, argument);
}
class MeshDrawObserver : public MaterialRenderObserver {
public:
    struct Draw {
        VXPRIMITIVETYPE Type = VX_TRIANGLELIST;
        int Count = 0, VertexCount = 0;
        CKWORD Indices[24] = {};
        VxVector Positions[8], Normals[8];
        Vx2DVector UVs[8];
    };
    Draw Draws[16]; int DrawCount = 0;
    MeshTrace *Trace = nullptr;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_LT(DrawCount, 16); if (DrawCount >= 16 || !data) return FALSE;
        Draw &draw = Draws[DrawCount++]; draw.Type = type; draw.Count = count; draw.VertexCount = data->VertexCount;
        EXPECT_LE(count, 24); EXPECT_LE(data->VertexCount, 8);
        for (int i = 0; i < count && i < 24; ++i) draw.Indices[i] = indices[i];
        for (int i = 0; i < data->VertexCount && i < 8; ++i) {
            if (data->PositionPtr) draw.Positions[i] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + data->PositionStride * i);
            if (data->NormalPtr) draw.Normals[i] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->NormalPtr) + data->NormalStride * i);
            if (data->TexCoordPtr) draw.UVs[i] = *reinterpret_cast<Vx2DVector *>(static_cast<CKBYTE *>(data->TexCoordPtr) + data->TexCoordStride * i);
        }
        if (Trace) { EXPECT_LT(Trace->Count, 32); Trace->Order[Trace->Count++] = 50; }
        return TRUE;
    }
};
class MeshRenderProbe : public RCKMesh {
public:
    explicit MeshRenderProbe(CKContext *context) : RCKMesh(context) {}
    int Groups() const { return m_MaterialGroups.Size(); }
    CKMaterialGroup *Group(int i) { return m_MaterialGroups[i]; }
    CKCallbacksContainer *Callbacks() { return m_RenderCallbacks; }
    CKCallbacksContainer *SubCallbacks() { return m_SubMeshCallbacks; }
    void PM(int count, CKBOOL morph, int step, const CKDWORD *parents, int parentCount) {
        m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = count; m_ProgressiveMesh->m_MorphEnabled = morph;
        m_ProgressiveMesh->m_MorphStep = step; m_ProgressiveMesh->m_Data.Resize(parentCount);
        for (int i = 0; i < parentCount; ++i) m_ProgressiveMesh->m_Data[i] = parents[i];
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
    CKProgressiveMesh *PM() { return m_ProgressiveMesh; }
    void Cold() { m_Valid = 0; }
};
// Only projection is substituted: Pick2D itself executes the engine's segment,
// distance and perspective interpolation code against independent coordinates.
class MeshProjectionContext : public RCKRenderContext {
public:
    VxVector4 Screen[8]; int TransformCount = 0;
    explicit MeshProjectionContext(CKContext *context) : RCKRenderContext(context) {}
    void TransformVertices(int count, VxTransformData *data, CK3dEntity *) override {
        ++TransformCount; EXPECT_LE(count, 8); EXPECT_NE(nullptr, data->ScreenVertices);
        for (int i = 0; i < count && i < 8; ++i)
            *reinterpret_cast<VxVector4 *>(static_cast<CKBYTE *>(data->ScreenVertices) + i * data->ScreenStride) = Screen[i];
    }
};
class MeshRenderAlignmentTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr;
    MeshProjectionContext *Device = nullptr; MeshDrawObserver State;
    MeshTrace Trace; MeshCallbackArgument Pre = {&Trace, 10}, Main = {&Trace, 20}, Post = {&Trace, 30};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new MeshProjectionContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State; Device->m_Current3dEntity = nullptr;
        State.Trace = &Trace;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Triangle(MeshRenderProbe &mesh) {
        mesh.SetVertexCount(3); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2);
        const VxVector p[3] = {VxVector(0,0,0), VxVector(2,0,0), VxVector(0,2,0)};
        for (int i = 0; i < 3; ++i) mesh.SetVertexPosition(i, const_cast<VxVector *>(&p[i]));
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
    void MorphTriangle(MeshRenderProbe &mesh) {
        mesh.SetVertexCount(4); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 3);
        const VxVector p[4] = {VxVector(0,0,0), VxVector(2,0,0), VxVector(0,0,0), VxVector(0,8,0)};
        const VxVector n[4] = {VxVector(0,0,1), VxVector(0,0,1), VxVector(0,0,1), VxVector(0,1,0)};
        for (int i = 0; i < 4; ++i) {
            mesh.SetVertexPosition(i, const_cast<VxVector *>(&p[i])); mesh.SetVertexNormal(i, const_cast<VxVector *>(&n[i]));
            mesh.SetVertexTextureCoordinates(i, 0.0f, i == 3 ? 1.0f : 0.0f, -1);
        }
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY); mesh.CreateRenderGroups();
        const CKDWORD parents[] = {0,0,0,2}; mesh.PM(3, TRUE, 2, parents, 4); mesh.BuildRenderMesh();
    }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, 1e-5f); EXPECT_NEAR(y, v.y, 1e-5f); EXPECT_NEAR(z, v.z, 1e-5f);
    }
    void Order(const int *values, int count) {
        ASSERT_EQ(count, Trace.Count); for (int i = 0; i < count; ++i) EXPECT_EQ(values[i], Trace.Order[i]);
    }
    void Line(MeshRenderProbe &mesh) {
        mesh.SetVertexCount(2); mesh.SetLineCount(1); mesh.SetLine(0, 0, 1);
        VxVector a(2,4,6), b(8,10,12); mesh.SetVertexPosition(0, &a); mesh.SetVertexPosition(1, &b);
        Device->Screen[0] = VxVector4(0,0,.5f,1); Device->Screen[1] = VxVector4(20,0,.5f,1);
    }
};
}

TEST_F(MeshRenderAlignmentTest, InvalidRenderContextDoesNotInvokeCallbacks) {
    MeshRenderProbe mesh(Context); mesh.SetRenderCallBack(MeshCallback, &Main);
    Device->m_RasterizerContext = nullptr; EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, mesh.Render(Device, nullptr)); EXPECT_EQ(0, Trace.Count);
}
TEST_F(MeshRenderAlignmentTest, MeshCallbacksExecutePreReplacementAndPostInRegistrationOrder) {
    MeshRenderProbe mesh(Context); ASSERT_TRUE(mesh.AddPreRenderCallBack(MeshCallback, &Pre, FALSE));
    mesh.SetRenderCallBack(MeshCallback, &Main); ASSERT_TRUE(mesh.AddPostRenderCallBack(MeshCallback, &Post, FALSE));
    EXPECT_EQ(CK_OK, mesh.Render(Device, nullptr)); const int expected[] = {10,20,30}; Order(expected, 3); EXPECT_EQ(0, State.DrawCount);
}
TEST_F(MeshRenderAlignmentTest, RemovingCallbackRequiresTheSameFunctionAndArgument) {
    MeshRenderProbe mesh(Context); mesh.SetRenderCallBack(MeshCallback, &Main);
    ASSERT_TRUE(mesh.AddPreRenderCallBack(MeshCallback, &Pre, FALSE)); EXPECT_FALSE(mesh.RemovePreRenderCallBack(MeshCallback, &Post));
    ASSERT_TRUE(mesh.RemovePreRenderCallBack(MeshCallback, &Pre)); EXPECT_FALSE(mesh.RemovePreRenderCallBack(MeshCallback, &Pre));
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr)); const int expected[] = {20}; Order(expected, 1);
}
TEST_F(MeshRenderAlignmentTest, DefaultCallbackRestoresRealDrawBetweenPreAndPost) {
    MeshRenderProbe mesh(Context); Triangle(mesh); mesh.SetRenderCallBack(MeshCallback, &Main);
    mesh.AddPreRenderCallBack(MeshCallback, &Pre, FALSE); mesh.AddPostRenderCallBack(MeshCallback, &Post, FALSE); mesh.SetDefaultRenderCallBack();
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr)); const int expected[] = {10,50,30}; Order(expected, 3); EXPECT_EQ(1, State.DrawCount);
}
TEST_F(MeshRenderAlignmentTest, RemoveAllCallbacksClearsBothContainersAndRestoresDrawing) {
    MeshRenderProbe mesh(Context); Triangle(mesh); mesh.SetRenderCallBack(MeshCallback, &Main);
    mesh.AddPreRenderCallBack(MeshCallback, &Pre, FALSE); mesh.AddPostRenderCallBack(MeshCallback, &Post, FALSE);
    mesh.AddSubMeshPreRenderCallBack(SubMeshCallback, &Pre, FALSE); mesh.AddSubMeshPostRenderCallBack(SubMeshCallback, &Post, FALSE);
    mesh.RemoveAllCallbacks(); EXPECT_EQ(nullptr, mesh.Callbacks()); EXPECT_EQ(nullptr, mesh.SubCallbacks());
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr)); const int expected[] = {50}; Order(expected, 1);
}
TEST_F(MeshRenderAlignmentTest, SubMeshCallbacksSurroundActualPrimitiveDraw) {
    MeshRenderProbe mesh(Context); Triangle(mesh);
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(SubMeshCallback, &Pre, FALSE)); ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(SubMeshCallback, &Post, FALSE));
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr)); const int expected[] = {10,50,30}; Order(expected, 3);
}
TEST_F(MeshRenderAlignmentTest, SubMeshCallbackRemovalMatchesFunctionAndArgument) {
    MeshRenderProbe mesh(Context); Triangle(mesh); mesh.AddSubMeshPreRenderCallBack(SubMeshCallback, &Pre, FALSE);
    mesh.AddSubMeshPostRenderCallBack(SubMeshCallback, &Post, FALSE);
    EXPECT_FALSE(mesh.RemoveSubMeshPreRenderCallBack(SubMeshCallback, &Post)); EXPECT_TRUE(mesh.RemoveSubMeshPreRenderCallBack(SubMeshCallback, &Pre));
    EXPECT_TRUE(mesh.RemoveSubMeshPostRenderCallBack(SubMeshCallback, &Post)); EXPECT_FALSE(mesh.RemoveSubMeshPostRenderCallBack(SubMeshCallback, &Post));
    ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr)); const int expected[] = {50}; Order(expected, 1);
}
TEST_F(MeshRenderAlignmentTest, GeoMorphDrawUsesInterpolatedPositionNormalAndUV) {
    MeshRenderProbe mesh(Context); MorphTriangle(mesh); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(4, State.Draws[0].VertexCount); EXPECT_EQ(3, State.Draws[0].Indices[2]);
    Vec(State.Draws[0].Positions[3], 0,4,0); Vec(State.Draws[0].Normals[3], 0,.5f,.5f); EXPECT_FLOAT_EQ(.5f, State.Draws[0].UVs[3].y);
    VxVector original; mesh.GetVertexPosition(3, &original); Vec(original, 0,8,0);
}
TEST_F(MeshRenderAlignmentTest, GeoMorphIndicesRemainWithinSubmittedTargetLODVertexCount) {
    MeshRenderProbe mesh(Context); MorphTriangle(mesh); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    for (int i = 0; i < State.Draws[0].Count; ++i) EXPECT_LT(State.Draws[0].Indices[i], State.Draws[0].VertexCount);
}
TEST_F(MeshRenderAlignmentTest, ChangingMorphLODRecomputesFromSourceWithoutCumulativeDrift) {
    MeshRenderProbe mesh(Context); MorphTriangle(mesh); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    mesh.SetVerticesRendered(4); mesh.BuildRenderMesh(); mesh.Cold(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    ASSERT_EQ(2, State.DrawCount); Vec(State.Draws[1].Positions[3], 0,8,0);
    mesh.SetVerticesRendered(3); mesh.BuildRenderMesh(); mesh.Cold(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    ASSERT_EQ(3, State.DrawCount); Vec(State.Draws[2].Positions[3], 0,4,0);
}
TEST_F(MeshRenderAlignmentTest, PMBuildDiscardsGroupLocalRemapBeforeUsingGlobalCollapseIndices) {
    MeshRenderProbe mesh(Context); mesh.SetVertexCount(4); mesh.SetFaceCount(2);
    const VxVector points[] = {VxVector(10,0,0),VxVector(20,0,0),VxVector(30,0,0),VxVector(40,0,0)};
    for (int i = 0; i < 4; ++i) mesh.SetVertexPosition(i, const_cast<VxVector *>(&points[i]));
    mesh.SetFaceVertexIndex(0, 2,3,0); mesh.SetFaceVertexIndex(1, 1,2,3);
    RCKMaterial material(Context); mesh.SetFaceMaterial(1, &material); mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    ASSERT_EQ(1, mesh.CreateRenderGroups()); ASSERT_EQ(2, mesh.Groups()); ASSERT_NE(0u, mesh.Group(0)->m_RemapData);
    const CKDWORD parents[] = {0,0,1,2}; mesh.PM(4, FALSE, 0, parents, 4); mesh.BuildRenderMesh();
    EXPECT_EQ(0u, mesh.Group(0)->m_RemapData); EXPECT_EQ(0u, mesh.Group(1)->m_RemapData);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(2, State.Draws[0].Indices[0]); Vec(State.Draws[0].Positions[2],30,0,0);
}
TEST_F(MeshRenderAlignmentTest, PMZeroRenderedVerticesSubmitsNoTriangle) {
    MeshRenderProbe mesh(Context); Triangle(mesh); mesh.CreateRenderGroups(); const CKDWORD parents[] = {0,0,1};
    mesh.PM(0, FALSE, 0, parents, 3); mesh.BuildRenderMesh(); mesh.DefaultRender(Device, nullptr); EXPECT_EQ(0, State.DrawCount);
}
TEST_F(MeshRenderAlignmentTest, NonMorphLODOneAndTwoDoNotInventExtraRenderedVertices) {
    for (int count = 1; count <= 2; ++count) {
        MeshRenderProbe mesh(Context); Triangle(mesh); mesh.CreateRenderGroups(); const CKDWORD parents[] = {0,0,1};
        mesh.PM(count, FALSE, 0, parents, 3); mesh.BuildRenderMesh(); EXPECT_EQ(count, mesh.PM()->m_Reserved);
        mesh.DefaultRender(Device, nullptr); EXPECT_EQ(0, State.DrawCount);
    }
}
TEST_F(MeshRenderAlignmentTest, PMSetterClampsToPhysicalVertexCountAndInvalidatesOnlyOnChange) {
    MeshRenderProbe mesh(Context); Triangle(mesh); const CKDWORD parents[] = {0,0,1}; mesh.PM(3, FALSE, 0, parents, 3);
    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); mesh.SetVerticesRendered(99); EXPECT_EQ(3, mesh.GetVerticesRendered()); EXPECT_TRUE(mesh.IsUpToDate());
    mesh.SetVerticesRendered(-1); EXPECT_EQ(0, mesh.GetVerticesRendered()); EXPECT_FALSE(mesh.IsUpToDate());
    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); mesh.EnablePMGeoMorph(TRUE); EXPECT_TRUE(mesh.IsPMGeoMorphEnabled()); EXPECT_FALSE(mesh.IsUpToDate());
    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0); mesh.SetPMGeoMorphStep(2); EXPECT_EQ(2, mesh.GetPMGeoMorphStep()); EXPECT_FALSE(mesh.IsUpToDate());
}
TEST_F(MeshRenderAlignmentTest, MissingPMGettersReturnZeroAndSettersDoNotCreatePM) {
    MeshRenderProbe mesh(Context); EXPECT_EQ(0, mesh.GetVerticesRendered()); EXPECT_FALSE(mesh.IsPMGeoMorphEnabled()); EXPECT_EQ(0, mesh.GetPMGeoMorphStep());
    mesh.SetVerticesRendered(3); mesh.EnablePMGeoMorph(TRUE); mesh.SetPMGeoMorphStep(2); EXPECT_EQ(nullptr, mesh.PM());
}
TEST_F(MeshRenderAlignmentTest, NonMorphDrawAnnouncesExactRequestedLODWithoutSnappingUp) {
    MeshRenderProbe mesh(Context); mesh.SetVertexCount(6); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0,0,1,2);
    for (int i = 0; i < 6; ++i) { VxVector p((float)i,0,0); mesh.SetVertexPosition(i,&p); }
    mesh.CreateRenderGroups(); const CKDWORD parents[] = {0,0,1,2,3,4}; mesh.PM(4,FALSE,0,parents,6); mesh.BuildRenderMesh();
    ASSERT_EQ(1,mesh.DefaultRender(Device,nullptr)); ASSERT_EQ(1,State.DrawCount); EXPECT_EQ(4,State.Draws[0].VertexCount);
}
TEST_F(MeshRenderAlignmentTest, DestroyPMKeepsUserCallbackAndRebuildsFullResolutionIndices) {
    MeshRenderProbe mesh(Context); Triangle(mesh); mesh.CreateRenderGroups(); const CKDWORD parents[] = {0,0,1};
    mesh.PM(2,FALSE,0,parents,3); mesh.AddPreRenderCallBack(ProgressiveMeshPreRenderCallback,&mesh, FALSE);
    mesh.AddPreRenderCallBack(MeshCallback,&Pre, FALSE); mesh.BuildRenderMesh(); mesh.DestroyPM();
    EXPECT_EQ(nullptr,mesh.PM()); ASSERT_NE(nullptr,mesh.Callbacks()); EXPECT_EQ(1,mesh.Callbacks()->m_PreCallBacks.Size());
    ASSERT_EQ(CK_OK,mesh.Render(Device,nullptr)); const int expected[] = {10,50}; Order(expected,2);
    ASSERT_EQ(1,State.DrawCount); EXPECT_EQ(3,State.Draws[0].Count); EXPECT_EQ(2,State.Draws[0].Indices[2]);
    mesh.DestroyPM(); EXPECT_EQ(1,mesh.Callbacks()->m_PreCallBacks.Size());
}
TEST_F(MeshRenderAlignmentTest, Pick2DReportsObjectSpaceIntersectionAndClearsOnlyDefinedFields) {
    MeshRenderProbe mesh(Context); Line(mesh); VxIntersectionDesc desc; std::memset(&desc, 0x55, sizeof(desc)); desc.Object = reinterpret_cast<CKRenderObject *>(Device);
    ASSERT_TRUE(mesh.Pick2D(Vx2DVector(10,0), &desc, Device, nullptr)); Vec(desc.IntersectionPoint,5,7,9); Vec(desc.IntersectionNormal,0,0,0);
    EXPECT_FLOAT_EQ(-1, desc.Distance); EXPECT_FLOAT_EQ(0, desc.TexU); EXPECT_FLOAT_EQ(0, desc.TexV); EXPECT_EQ(0, desc.FaceIndex);
    EXPECT_EQ(reinterpret_cast<CKRenderObject *>(Device), desc.Object); EXPECT_EQ(1, Device->TransformCount);
}
TEST_F(MeshRenderAlignmentTest, Pick2DPerspectiveCorrectionUsesEndpointReciprocalW) {
    MeshRenderProbe mesh(Context); Line(mesh); Device->Screen[0].w = 2; Device->Screen[1].w = 4; VxIntersectionDesc desc;
    ASSERT_TRUE(mesh.Pick2D(Vx2DVector(10,0), &desc, Device, nullptr)); Vec(desc.IntersectionPoint,6,8,10);
}
TEST_F(MeshRenderAlignmentTest, Pick2DThresholdIncludesTenPixelsAndExcludesLargerDistance) {
    MeshRenderProbe mesh(Context); Line(mesh); VxIntersectionDesc desc;
    EXPECT_TRUE(mesh.Pick2D(Vx2DVector(10,10), &desc, Device, nullptr)); EXPECT_FALSE(mesh.Pick2D(Vx2DVector(10.0f,10.01f), &desc, Device, nullptr));
}
TEST_F(MeshRenderAlignmentTest, Pick2DProjectionExcludesEndpointAndOutsideSegment) {
    MeshRenderProbe mesh(Context); Line(mesh); VxIntersectionDesc desc;
    EXPECT_FALSE(mesh.Pick2D(Vx2DVector(20,0), &desc, Device, nullptr)); EXPECT_FALSE(mesh.Pick2D(Vx2DVector(-1,0), &desc, Device, nullptr));
    EXPECT_TRUE(mesh.Pick2D(Vx2DVector(.01f,0.0f), &desc, Device, nullptr));
}
TEST_F(MeshRenderAlignmentTest, Pick2DFirstMatchingLineWinsRatherThanNearestLine) {
    MeshRenderProbe mesh(Context); mesh.SetVertexCount(4); mesh.SetLineCount(2); mesh.SetLine(0,0,1); mesh.SetLine(1,2,3);
    for (int i = 0; i < 4; ++i) { VxVector p((i & 1) ? 20.0f : 0.0f, i < 2 ? 3.0f : 0.0f, 0); mesh.SetVertexPosition(i,&p); Device->Screen[i] = VxVector4(p.x,p.y,.5f,1); }
    VxIntersectionDesc desc; ASSERT_TRUE(mesh.Pick2D(Vx2DVector(10,0),&desc,Device,nullptr)); EXPECT_FLOAT_EQ(3, desc.IntersectionPoint.y);
}
TEST_F(MeshRenderAlignmentTest, Pick2DEmptyGeometryDoesNotTransformOrChangeOutput) {
    MeshRenderProbe mesh(Context); VxIntersectionDesc desc; desc.Distance = 57;
    EXPECT_FALSE(mesh.Pick2D(Vx2DVector(0,0),&desc,Device,nullptr)); EXPECT_EQ(0, Device->TransformCount); EXPECT_FLOAT_EQ(57,desc.Distance);
}
TEST_F(MeshRenderAlignmentTest, Pick2DAllowsBooleanHitQueryWithoutOutputDescriptor) {
    MeshRenderProbe mesh(Context); Line(mesh); EXPECT_TRUE(mesh.Pick2D(Vx2DVector(10,0),nullptr,Device,nullptr));
}
