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
class PMDrawObserver : public MaterialRenderObserver {
public:
    struct Draw {
        VXPRIMITIVETYPE Type = VX_TRIANGLELIST;
        int VertexCount = 0, IndexCount = 0;
        CKWORD Indices[48] = {};
        VxVector Positions[8], Normals[8];
        Vx2DVector UVs[8];
        CKDWORD Colors[8] = {}, Speculars[8] = {};
    };
    Draw Draws[24];
    int Count = 0;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_LT(Count, 24); EXPECT_NE(nullptr, data);
        if (Count >= 24 || !data) return FALSE;
        Draw &draw = Draws[Count++];
        draw.Type = type; draw.VertexCount = data->VertexCount; draw.IndexCount = count;
        EXPECT_LE(count, 48); EXPECT_LE(data->VertexCount, 8);
        for (int i = 0; i < count && i < 48; ++i) {
            draw.Indices[i] = indices[i];
            EXPECT_LT(indices[i], data->VertexCount) << "Submitted index must address submitted vertex stream";
        }
        for (int i = 0; i < data->VertexCount && i < 8; ++i) {
            if (data->PositionPtr) draw.Positions[i] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride);
            if (data->NormalPtr) draw.Normals[i] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->NormalPtr) + i * data->NormalStride);
            if (data->TexCoordPtr) draw.UVs[i] = *reinterpret_cast<Vx2DVector *>(static_cast<CKBYTE *>(data->TexCoordPtr) + i * data->TexCoordStride);
            if (data->ColorPtr) draw.Colors[i] = *reinterpret_cast<CKDWORD *>(static_cast<CKBYTE *>(data->ColorPtr) + i * data->ColorStride);
            if (data->SpecularColorPtr) draw.Speculars[i] = *reinterpret_cast<CKDWORD *>(static_cast<CKBYTE *>(data->SpecularColorPtr) + i * data->SpecularColorStride);
        }
        return TRUE;
    }
};
class PMMeshProbe : public RCKMesh {
public:
    explicit PMMeshProbe(CKContext *context) : RCKMesh(context) {}
    void ConfigurePM(int count, CKBOOL morph, int step = 2) {
        if (!m_ProgressiveMesh) m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = count;
        m_ProgressiveMesh->m_MorphEnabled = morph;
        m_ProgressiveMesh->m_MorphStep = step;
        const CKDWORD parents[] = {0,0,0,2,3,4};
        m_ProgressiveMesh->m_Data.Resize(6);
        for (int i = 0; i < 6; ++i) m_ProgressiveMesh->m_Data[i] = parents[i];
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
    CKProgressiveMesh *PM() { return m_ProgressiveMesh; }
    VxMaterialChannel &Channel(int index) { return m_MaterialChannels[index]; }
    CKMaterialGroup *Group(int index) { return m_MaterialGroups[index]; }
    void SeedFaceNormal(int index, const VxVector &normal) { m_Faces[index].m_Normal = normal; }
    int GroupCount() const { return m_MaterialGroups.Size(); }
    void Cold() { m_Valid = 0; }
};
class MeshPMConsumersTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    PMDrawObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State; Device->m_Current3dEntity = nullptr;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Geometry(PMMeshProbe &mesh, int faces = 1) {
        mesh.SetVertexCount(6); mesh.SetFaceCount(faces);
        const VxVector positions[] = {VxVector(0,0,0),VxVector(2,0,0),VxVector(0,0,0),VxVector(0,8,0),VxVector(12,0,0),VxVector(100,4,0)};
        for (int i = 0; i < 6; ++i) {
            VxVector normal(0, i == 3 ? 1.0f : 0.0f, i == 3 ? 0.0f : 1.0f);
            mesh.SetVertexPosition(i, const_cast<VxVector *>(&positions[i])); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, i * .125f, i == 3 ? 1.0f : 0.0f, -1);
            mesh.SetVertexColor(i, 0xff102030u + i); mesh.SetVertexSpecularColor(i, 0xff304050u + i);
        }
        mesh.SetFaceVertexIndex(0,0,1,3);
        if (faces > 1) mesh.SetFaceVertexIndex(1,0,1,2);
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
    void Prepare(PMMeshProbe &mesh, int count = 3, CKBOOL morph = TRUE) {
        mesh.CreateRenderGroups(); mesh.ConfigurePM(count, morph); mesh.BuildRenderMesh(); mesh.Cold();
    }
    int Channel(PMMeshProbe &mesh, RCKMaterial &material, CKBOOL sameUV = TRUE) {
        int index = mesh.AddChannel(&material, FALSE);
        EXPECT_GE(index, 0);
        mesh.SetChannelFlags(index, VXCHANNEL_ACTIVE | (sameUV ? VXCHANNEL_SAMEUV : 0));
        mesh.SetFlags(mesh.GetFlags() | VXMESH_RENDERCHANNELS);
        return index;
    }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, 1e-5f); EXPECT_NEAR(y, v.y, 1e-5f); EXPECT_NEAR(z, v.z, 1e-5f);
    }
    void ExactIndices(int draw, const CKWORD *expected, int count) {
        ASSERT_LT(draw, State.Count); ASSERT_EQ(count, State.Draws[draw].IndexCount);
        for (int i = 0; i < count; ++i) EXPECT_EQ(expected[i], State.Draws[draw].Indices[i]);
    }
};

TEST_F(MeshPMConsumersTest, ReducedPMKeepsAuthoredLinesWithinFullSourceStream) {
    PMMeshProbe mesh(Context); Geometry(mesh); mesh.SetLineCount(1); mesh.SetLine(0,4,5); Prepare(mesh,3,FALSE);
    ASSERT_EQ(1,mesh.DefaultRender(Device,nullptr)); ASSERT_EQ(2,State.Count);
    EXPECT_EQ(VX_LINELIST,State.Draws[1].Type); EXPECT_EQ(6,State.Draws[1].VertexCount);
    const CKWORD indices[] = {4,5}; ExactIndices(1,indices,2); Vec(State.Draws[1].Positions[5],100,4,0);
}
TEST_F(MeshPMConsumersTest, GeomorphLinesRetainAuthoredSourcePositions) {
    PMMeshProbe mesh(Context); Geometry(mesh); mesh.SetLineCount(1); mesh.SetLine(0,3,5); Prepare(mesh);
    ASSERT_EQ(1,mesh.DefaultRender(Device,nullptr)); ASSERT_EQ(2,State.Count);
    Vec(State.Draws[0].Positions[3],0,4,0); Vec(State.Draws[1].Positions[3],0,8,0);
    EXPECT_EQ(6,State.Draws[1].VertexCount); Vec(State.Draws[1].Positions[5],100,4,0);
}
TEST_F(MeshPMConsumersTest, LineOnlyMeshRendersAuthoredLinesAtZeroTriangleLOD) {
    PMMeshProbe mesh(Context); Geometry(mesh); mesh.SetFaceCount(0); mesh.SetLineCount(1); mesh.SetLine(0,4,5); Prepare(mesh,0,FALSE);
    ASSERT_EQ(1,mesh.DefaultRender(Device,nullptr)); ASSERT_EQ(1,State.Count);
    EXPECT_EQ(VX_LINELIST,State.Draws[0].Type); EXPECT_EQ(6,State.Draws[0].VertexCount);
}
TEST_F(MeshPMConsumersTest, LineDiffuseAndSpecularUseSourceVertexIndices) {
    PMMeshProbe mesh(Context); Geometry(mesh); mesh.SetLineCount(1); mesh.SetLine(0,4,5); Prepare(mesh);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.Count);
    EXPECT_EQ(0xff102035u,State.Draws[1].Colors[5]); EXPECT_EQ(0xff304055u,State.Draws[1].Speculars[5]);
}
TEST_F(MeshPMConsumersTest, SparseMorphScratchPreservesUnreferencedSubmittedPosition) {
    PMMeshProbe mesh(Context); Geometry(mesh); VxVector unused(3,5,7); mesh.SetVertexPosition(2,&unused); Prepare(mesh);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(1,State.Count); EXPECT_EQ(4,State.Draws[0].VertexCount);
    Vec(State.Draws[0].Positions[2],3,5,7);
}
TEST_F(MeshPMConsumersTest, SparseMorphScratchPreservesUnreferencedNormalAndUV) {
    PMMeshProbe mesh(Context); Geometry(mesh); VxVector unused(1,2,3); mesh.SetVertexNormal(2,&unused);
    mesh.SetVertexTextureCoordinates(2,.75f,.875f,-1); Prepare(mesh); mesh.DefaultRender(Device,nullptr);
    ASSERT_EQ(1,State.Count); Vec(State.Draws[0].Normals[2],1,2,3);
    EXPECT_FLOAT_EQ(.75f,State.Draws[0].UVs[2].x); EXPECT_FLOAT_EQ(.875f,State.Draws[0].UVs[2].y);
}
TEST_F(MeshPMConsumersTest, MorphSameUVChannelSamplesInterpolatedMainUV) {
    PMMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); Channel(mesh,material); Prepare(mesh);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.Count);
    EXPECT_FLOAT_EQ(.5f,State.Draws[0].UVs[3].y); EXPECT_FLOAT_EQ(.5f,State.Draws[1].UVs[3].y);
    // Lower LOD is 2: follow parents 3 -> 2 -> 0, then interpolate at t = 0.5.
    EXPECT_FLOAT_EQ(.1875f,State.Draws[1].UVs[3].x);
}
TEST_F(MeshPMConsumersTest, PrivateUVChannelRetainsItsOwnCoordinates) {
    PMMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); int channel = Channel(mesh,material,FALSE);
    mesh.SetVertexTextureCoordinates(3,.875f,.9375f,channel); Prepare(mesh); mesh.DefaultRender(Device,nullptr);
    ASSERT_EQ(2,State.Count); EXPECT_FLOAT_EQ(.875f,State.Draws[1].UVs[3].x); EXPECT_FLOAT_EQ(.9375f,State.Draws[1].UVs[3].y);
}
TEST_F(MeshPMConsumersTest, ChannelMasksSelectOnlyMatchingSurvivingTriangles) {
    PMMeshProbe mesh(Context); RCKMaterial first(Context),second(Context); Geometry(mesh,2);
    Channel(mesh,first); Channel(mesh,second); mesh.SetFaceChannelMask(0,1); mesh.SetFaceChannelMask(1,2); Prepare(mesh);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(3,State.Count);
    const CKWORD a[] = {0,1,3}, b[] = {0,1,2}; ExactIndices(1,a,3); ExactIndices(2,b,3);
}
TEST_F(MeshPMConsumersTest, ChannelMaskCanExcludeAllTrianglesAtCurrentLOD) {
    PMMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); int channel = Channel(mesh,material);
    mesh.SetFaceChannelMask(0,0); Prepare(mesh); mesh.DefaultRender(Device,nullptr);
    ASSERT_EQ(1,State.Count); ASSERT_NE(nullptr,mesh.Channel(channel).m_FaceIndices); EXPECT_EQ(0,mesh.Channel(channel).m_FaceIndices->Size());
}
TEST_F(MeshPMConsumersTest, ChangingMaskAfterBuildRetainsRemappedLODIndices) {
    PMMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); mesh.SetFaceVertexIndex(0,0,1,5);
    Channel(mesh,material); Prepare(mesh,4,FALSE); mesh.SetFaceChannelMask(0,0); mesh.SetFaceChannelMask(0,1);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.Count); const CKWORD indices[] = {0,1,3}; ExactIndices(1,indices,3);
}
TEST_F(MeshPMConsumersTest, DegenerateFaceBeforeValidFaceDoesNotPopulateChannelPass) {
    PMMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh,2);
    mesh.SetFaceVertexIndex(0,0,4,5); mesh.SetFaceVertexIndex(1,0,1,5); Channel(mesh,material); Prepare(mesh,4,FALSE);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.Count); const CKWORD indices[] = {0,1,3}; ExactIndices(0,indices,3); ExactIndices(1,indices,3);
}
TEST_F(MeshPMConsumersTest, MultiMaterialPMChannelsRemainInGlobalVertexSpace) {
    PMMeshProbe mesh(Context); RCKMaterial base(Context),channelMat(Context); Geometry(mesh,2);
    mesh.SetFaceMaterial(1,&base); Channel(mesh,channelMat); Prepare(mesh); mesh.DefaultRender(Device,nullptr);
    ASSERT_EQ(3,State.Count); const CKWORD indices[] = {0,1,3,0,1,2}; ExactIndices(2,indices,6);
}
TEST_F(MeshPMConsumersTest, SameUVChannelRestoresCallerProvidedPointerAndStride) {
    PMMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh); Channel(mesh,material); Prepare(mesh);
    Vx2DVector supplied[4] = {Vx2DVector(.1f,.2f),Vx2DVector(.3f,.4f),Vx2DVector(.5f,.6f),Vx2DVector(.7f,.8f)};
    VxDrawPrimitiveData data; memset(&data,0,sizeof(data)); data.VertexCount = 4;
    data.PositionPtr = &reinterpret_cast<VxVertex *>(mesh.PM()->m_EdgeCollapseData.Begin())[0].m_Position;
    data.PositionStride = sizeof(VxVertex); data.TexCoordPtr = supplied; data.TexCoordStride = sizeof(Vx2DVector);
    ASSERT_EQ(1,mesh.RenderChannels(Device,nullptr,&data,0)); ASSERT_EQ(1,State.Count);
    EXPECT_FLOAT_EQ(.7f,State.Draws[0].UVs[3].x); EXPECT_FLOAT_EQ(.8f,State.Draws[0].UVs[3].y);
    EXPECT_EQ(supplied,data.TexCoordPtr); EXPECT_EQ(sizeof(Vx2DVector),data.TexCoordStride);
}
TEST_F(MeshPMConsumersTest, SourceBoundsConservativelyEncloseMorphAndAuthoredLines) {
    PMMeshProbe mesh(Context); Geometry(mesh); mesh.SetLineCount(1); mesh.SetLine(0,4,5); Prepare(mesh);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.Count);
    const VxBbox &box = mesh.GetLocalBox();
    EXPECT_FLOAT_EQ(0,box.Min.x); EXPECT_FLOAT_EQ(100,box.Max.x); EXPECT_FLOAT_EQ(8,box.Max.y);
    for (int d = 0; d < State.Count; ++d) for (int i = 0; i < State.Draws[d].VertexCount; ++i) {
        const VxVector &p = State.Draws[d].Positions[i];
        EXPECT_GE(p.x,box.Min.x); EXPECT_LE(p.x,box.Max.x); EXPECT_GE(p.y,box.Min.y); EXPECT_LE(p.y,box.Max.y);
    }
}
TEST_F(MeshPMConsumersTest, EditingSourceRebuildsScratchAndInvalidatesConservativeBounds) {
    PMMeshProbe mesh(Context); Geometry(mesh); Prepare(mesh); EXPECT_FLOAT_EQ(100,mesh.GetLocalBox().Max.x);
    VxVector changed(120,9,0); mesh.SetVertexPosition(5,&changed); mesh.SetVerticesRendered(4); mesh.BuildRenderMesh(); mesh.Cold();
    EXPECT_FLOAT_EQ(120,mesh.GetLocalBox().Max.x); EXPECT_FLOAT_EQ(9,mesh.GetLocalBox().Max.y);
    VxVertex *scratch = reinterpret_cast<VxVertex *>(mesh.PM()->m_EdgeCollapseData.Begin()); Vec(scratch[5].m_Position,120,9,0);
}
TEST_F(MeshPMConsumersTest, DepthOnlyDrawUsesCurrentLODIndices) {
    PMMeshProbe mesh(Context); RCK3dEntity entity(Context); Geometry(mesh); mesh.SetFaceVertexIndex(0,0,1,5); Prepare(mesh,4,FALSE);
    entity.m_MoveableFlags |= VX_MOVEABLE_ZBUFONLY; mesh.DefaultRender(Device,&entity);
    ASSERT_EQ(1,State.Count); const CKWORD indices[] = {0,1,3}; ExactIndices(0,indices,3);
}
TEST_F(MeshPMConsumersTest, StencilOnlyDrawUsesCurrentLODIndices) {
    PMMeshProbe mesh(Context); RCK3dEntity entity(Context); Geometry(mesh); mesh.SetFaceVertexIndex(0,0,1,5); Prepare(mesh,4,FALSE);
    entity.m_MoveableFlags |= VX_MOVEABLE_STENCILONLY; mesh.DefaultRender(Device,&entity);
    ASSERT_EQ(1,State.Count); const CKWORD indices[] = {0,1,3}; ExactIndices(0,indices,3);
}
TEST_F(MeshPMConsumersTest, WireframeOverlayUsesCurrentLODIndices) {
    PMMeshProbe mesh(Context); Geometry(mesh); mesh.SetFaceVertexIndex(0,0,1,5); Prepare(mesh,4,FALSE); Device->m_DisplayWireframe = TRUE;
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(2,State.Count); const CKWORD indices[] = {0,1,3}; ExactIndices(1,indices,3);
}
TEST_F(MeshPMConsumersTest, RebuiltNormalsRemainAssociatedWithSurvivingAuthoredFace) {
    PMMeshProbe mesh(Context); Geometry(mesh,2);
    mesh.SetFaceVertexIndex(0,0,4,5); mesh.SetFaceVertexIndex(1,0,1,5);
    VxVector omitted(1,2,3), surviving(4,5,6); mesh.SeedFaceNormal(0,omitted); mesh.SeedFaceNormal(1,surviving);
    mesh.SetFlags(mesh.GetFlags() | VXMESH_PM_BUILDNORM); Prepare(mesh,4,FALSE);
    Vec(mesh.GetFaceNormal(0),1,2,3); Vec(mesh.GetFaceNormal(1),0,0,1);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(1,State.Count);
    const CKWORD indices[] = {0,1,3}; ExactIndices(0,indices,3);
    Vec(State.Draws[0].Normals[0],0,0,1); Vec(State.Draws[0].Normals[1],0,0,1); Vec(State.Draws[0].Normals[3],0,0,1);
}
TEST_F(MeshPMConsumersTest, GeomorphNormalRebuildMapsCompactFacesWithoutOverwritingOmittedFace) {
    PMMeshProbe mesh(Context); Geometry(mesh,2);
    mesh.SetFaceVertexIndex(0,0,4,5); mesh.SetFaceVertexIndex(1,0,1,5);
    VxVector omitted(1,2,3), surviving(4,5,6); mesh.SeedFaceNormal(0,omitted); mesh.SeedFaceNormal(1,surviving);
    mesh.SetFlags(mesh.GetFlags() | VXMESH_PM_BUILDNORM); Prepare(mesh);
    Vec(mesh.GetFaceNormal(0),1,2,3); Vec(mesh.GetFaceNormal(1),0,0,1);
    VxVector sourceNormal; mesh.GetVertexNormal(3,&sourceNormal); Vec(sourceNormal,0,1,0);
    mesh.DefaultRender(Device,nullptr); ASSERT_EQ(1,State.Count);
    Vec(State.Draws[0].Positions[3],0,4,0); Vec(State.Draws[0].Normals[3],0,0,1);
}
}
