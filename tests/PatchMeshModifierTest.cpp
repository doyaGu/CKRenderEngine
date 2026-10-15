#include "TextureTestObserver.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"

namespace {
class ModifierPatch : public RCKPatchMesh {
public:
    explicit ModifierPatch(CKContext *context) : RCKPatchMesh(context) {}
    void Extra(CKMaterial *material) {
        m_TexturePatches.Resize(2); m_TexturePatches[1].Material=material->GetID();
        m_TexturePatches[1].Flags=VXBLEND_ONE; m_TexturePatches[1].Type=VXBLEND_ZERO;
        m_TexturePatches[1].SubType=VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV;
    }
};
void IsVector(const VxVector &actual,const VxVector &expected) {
    EXPECT_NEAR(expected.x,actual.x,.0001f); EXPECT_NEAR(expected.y,actual.y,.0001f); EXPECT_NEAR(expected.z,actual.z,.0001f);
}
VxVector Position(RCKMesh &mesh,int index) { VxVector actual; mesh.GetVertexPosition(index,&actual); return actual; }
VxVector Normal(RCKMesh &mesh,int index) { VxVector actual; mesh.GetVertexNormal(index,&actual); return actual; }
int Find(RCKMesh &mesh,const VxVector &position) {
    for(int i=0;i<mesh.GetVertexCount();++i) {
        VxVector actual=Position(mesh,i);
        if(fabsf(actual.x-position.x)<.0001f && fabsf(actual.y-position.y)<.0001f && fabsf(actual.z-position.z)<.0001f) return i;
    }
    return -1;
}
void UV(RCKMesh &mesh,int index,float u,float v,int channel=-1) {
    float actualU,actualV; mesh.GetVertexTextureCoordinates(index,&actualU,&actualV,channel);
    EXPECT_NEAR(u,actualU,.0001f); EXPECT_NEAR(v,actualV,.0001f);
}
void Quad(ModifierPatch &mesh) {
    mesh.SetVertVecCount(4,12); mesh.SetPatchCount(1); mesh.SetEdgeCount(4);
    VxVector vertices[]={VxVector(0,0,0),VxVector(3,0,0),VxVector(3,3,0),VxVector(0,3,0)};
    CKPatch patch; patch.type=4; patch.SmoothingGroup=1;
    for(int i=0;i<4;++i) { mesh.SetVert(i,&vertices[i]); patch.v[i]=static_cast<short>(i); patch.edge[i]=static_cast<short>(i); patch.interior[i]=static_cast<short>(8+i); }
    for(int i=0;i<8;++i) patch.vec[i]=static_cast<short>(i);
    mesh.SetPatch(0,&patch);
    for(int edge=0;edge<4;++edge) {
        const VxVector &a=vertices[edge],&b=vertices[(edge+1)%4];
        VxVector first=a*(2.0f/3.0f)+b*(1.0f/3.0f),second=a*(1.0f/3.0f)+b*(2.0f/3.0f);
        mesh.SetVec(edge*2,&first); mesh.SetVec(edge*2+1,&second);
        CKPatchEdge item={static_cast<short>(edge),static_cast<short>(2*edge),static_cast<short>(2*edge+1),static_cast<short>((edge+1)%4),0,-1}; mesh.SetEdge(edge,&item);
    }
    mesh.ComputePatchInteriors(0); mesh.SetTVPatchCount(1,-1); mesh.SetTVCount(4,-1);
    CKTVPatch tv={{0,1,2,3}}; mesh.SetTVPatch(0,&tv,-1);
    for(int i=0;i<4;++i) mesh.SetTV(i,vertices[i].x/3,vertices[i].y/3,-1);
    mesh.SetIterationCount(1);
}
class PatchMeshModifierTest : public TextureTestWorld {};

TEST_F(PatchMeshModifierTest, EmptyModifierStorageHasItsAuthoredStrideAndZeroCount) {
    ModifierPatch mesh(Context); RCKMesh &api=mesh; CKDWORD stride=0;
    EXPECT_EQ(0,api.GetModifierVertexCount()); EXPECT_EQ(nullptr,api.GetModifierVertices(&stride)); EXPECT_EQ(12u,stride);
    EXPECT_EQ(0,api.GetModifierUVCount(-1)); EXPECT_EQ(nullptr,api.GetModifierUVs(&stride,-1)); EXPECT_EQ(8u,stride);
}

TEST_F(PatchMeshModifierTest, ModifierVerticesExposeTheCombinedAuthoredControlsBeforeAndAfterBuild) {
    ModifierPatch mesh(Context); Quad(mesh); RCKMesh &api=mesh; CKDWORD stride=0;
    ASSERT_EQ(16,api.GetModifierVertexCount()); ASSERT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetVerts()),api.GetModifierVertices(&stride));
    EXPECT_EQ(sizeof(VxVector),stride); EXPECT_EQ(mesh.GetVerts()+4,mesh.GetVecs());
    mesh.BuildRenderMesh(); ASSERT_EQ(9,mesh.GetVertexCount()); EXPECT_EQ(16,api.GetModifierVertexCount());
    EXPECT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetVerts()),api.GetModifierVertices(&stride));
    IsVector(*reinterpret_cast<VxVector *>(api.GetModifierVertices(&stride)+4*stride),VxVector(1,0,0));
}

TEST_F(PatchMeshModifierTest, RawBoundaryControlEditAndVertexMoveChangeTheActualCubicMidpoint) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; CKDWORD stride;
    CKBYTE *points=api.GetModifierVertices(&stride); ASSERT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetVerts()),points); ASSERT_EQ(16,api.GetModifierVertexCount());
    reinterpret_cast<VxVector *>(points+4*stride)->z=2; reinterpret_cast<VxVector *>(points+5*stride)->z=2;
    api.ModifierVertexMove(TRUE,FALSE); mesh.BuildRenderMesh();
    const int midpoint=Find(mesh,VxVector(1.5f,0,1.5f)); ASSERT_GE(midpoint,0); UV(mesh,midpoint,.5f,0);
    EXPECT_NEAR(1.5f,mesh.GetLocalBox().Max.z,.0001f); EXPECT_LT(Normal(mesh,0).z,0); EXPECT_GT(fabsf(Normal(mesh,0).x),.1f);
}

TEST_F(PatchMeshModifierTest, VertexMoveFalsePreservesAuthoredNormalPolicyDespiteFaceOnlyArgument) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; CKDWORD stride;
    CKBYTE *points=api.GetModifierVertices(&stride); ASSERT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetVerts()),points);
    const VxVector manual(.25f,.5f,.75f); for(int i=0;i<mesh.GetVertexCount();++i) mesh.SetVertexNormal(i,const_cast<VxVector *>(&manual));
    reinterpret_cast<VxVector *>(points)->z=1; api.ModifierVertexMove(FALSE,TRUE); mesh.BuildRenderMesh();
    IsVector(Position(mesh,0),VxVector(0,0,1)); for(int i=0;i<mesh.GetVertexCount();++i) IsVector(Normal(mesh,i),manual);
}

TEST_F(PatchMeshModifierTest, VertexMoveTrueRebuildsSurfaceNormalsWhenFaceArgumentIsFalse) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; CKDWORD stride;
    CKBYTE *points=api.GetModifierVertices(&stride); ASSERT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetVerts()),points);
    for(int i=0;i<api.GetModifierVertexCount();++i) reinterpret_cast<VxVector *>(points+i*stride)->z=reinterpret_cast<VxVector *>(points+i*stride)->x;
    api.ModifierVertexMove(TRUE,FALSE); mesh.BuildRenderMesh();
    for(int i=0;i<mesh.GetVertexCount();++i) IsVector(Normal(mesh,i),VxVector(.70710678f,0,-.70710678f));
}

TEST_F(PatchMeshModifierTest, ModifierUVsExposeAuthoredPayloadCountRatherThanGeneratedVertexCount) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.SetTVCount(6,-1); RCKMesh &api=mesh; CKDWORD stride;
    EXPECT_EQ(6,api.GetModifierUVCount(-1)); EXPECT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetTVs(-1)),api.GetModifierUVs(&stride,-1)); EXPECT_EQ(8u,stride);
    mesh.BuildRenderMesh(); EXPECT_EQ(9,mesh.GetVertexCount()); EXPECT_EQ(6,api.GetModifierUVCount(-1));
    EXPECT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetTVs(-1)),api.GetModifierUVs(&stride,-1));
}

TEST_F(PatchMeshModifierTest, RawBaseUVWriteAndMoveReachCornerAndInterpolatedEdgeCoordinates) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; CKDWORD stride;
    CKBYTE *uvs=api.GetModifierUVs(&stride,-1); ASSERT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetTVs(-1)),uvs); ASSERT_EQ(4,api.GetModifierUVCount(-1));
    VxUV *first=reinterpret_cast<VxUV *>(uvs); first->u=.5f; first->v=.25f; api.ModifierUVMove(); mesh.BuildRenderMesh();
    UV(mesh,0,.5f,.25f); int midpoint=Find(mesh,VxVector(1.5f,0,0)); ASSERT_GE(midpoint,0); UV(mesh,midpoint,.75f,.125f);
}

TEST_F(PatchMeshModifierTest, SameUVRenderingDoesNotRedirectTheAuthoredExtraModifierBuffer) {
    RCKMaterial material(Context); ModifierPatch mesh(Context); Quad(mesh); mesh.Extra(reinterpret_cast<CKMaterial *>(&material));
    mesh.SetTVPatchCount(1,0); mesh.SetTVCount(4,0); CKTVPatch tv={{0,1,2,3}}; mesh.SetTVPatch(0,&tv,0);
    for(int i=0;i<4;++i) mesh.SetTV(i,float(i+10),float(i+20),0); mesh.BuildRenderMesh(); RCKMesh &api=mesh; CKDWORD stride;
    CKBYTE *uvs=api.GetModifierUVs(&stride,0); ASSERT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetTVs(0)),uvs); ASSERT_EQ(4,api.GetModifierUVCount(0));
    reinterpret_cast<VxUV *>(uvs)->u=30; reinterpret_cast<VxUV *>(uvs)->v=40; api.ModifierUVMove(); mesh.BuildRenderMesh();
    UV(mesh,0,0,0,-1); UV(mesh,0,30,40,0); CKDWORD baseStride,channelStride;
    EXPECT_EQ(mesh.GetTextureCoordinatesPtr(&baseStride,-1),mesh.GetTextureCoordinatesPtr(&channelStride,0));
}

TEST_F(PatchMeshModifierTest, RemovingAuthoredUVStorageReportsEmptyInsteadOfBorrowingGeneratedData) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; CKDWORD stride;
    mesh.SetTVCount(0,-1); EXPECT_EQ(0,api.GetModifierUVCount(-1)); EXPECT_EQ(nullptr,api.GetModifierUVs(&stride,-1)); EXPECT_EQ(8u,stride);
    mesh.BuildRenderMesh(); UV(mesh,2,0,0,-1); EXPECT_EQ(9,mesh.GetVertexCount());
}

TEST_F(PatchMeshModifierTest, RawUVMoveRebuildsSharedEdgeConnectivityWhenValuesCreateASeam) {
    ModifierPatch mesh(Context); mesh.SetVertVecCount(6,24); mesh.SetPatchCount(2); mesh.SetEdgeCount(7);
    VxVector vertices[]={VxVector(0,0,0),VxVector(1,0,0),VxVector(1,1,0),VxVector(0,1,0),VxVector(2,0,0),VxVector(2,1,0)};
    for(int i=0;i<6;++i) mesh.SetVert(i,&vertices[i]);
    const short corners[2][4]={{0,1,2,3},{1,4,5,2}},edgeIds[2][4]={{0,1,2,3},{4,5,6,1}};
    for(int p=0;p<2;++p) {
        CKPatch patch; patch.type=4; patch.SmoothingGroup=1;
        for(int c=0;c<4;++c) { patch.v[c]=corners[p][c]; patch.edge[c]=edgeIds[p][c]; patch.interior[c]=static_cast<short>(p*12+8+c); }
        for(int c=0;c<8;++c) patch.vec[c]=static_cast<short>(p*12+c); mesh.SetPatch(p,&patch);
        for(int edge=0;edge<4;++edge) {
            VxVector first=vertices[corners[p][edge]],second=vertices[corners[p][(edge+1)%4]];
            VxVector a=first*(2.0f/3.0f)+second*(1.0f/3.0f),b=first*(1.0f/3.0f)+second*(2.0f/3.0f);
            mesh.SetVec(p*12+2*edge,&a); mesh.SetVec(p*12+2*edge+1,&b);
        }
    }
    CKPatchEdge edges[]={{0,0,1,1,0,-1},{1,2,3,2,0,1},{2,4,5,3,0,-1},{3,6,7,0,0,-1},{1,12,13,4,1,-1},{4,14,15,5,1,-1},{5,16,17,2,1,-1}};
    for(int e=0;e<7;++e) mesh.SetEdge(e,&edges[e]);
    mesh.SetTVPatchCount(2,-1); mesh.SetTVCount(8,-1); CKTVPatch firstTV={{0,1,2,3}},secondTV={{4,5,6,7}};
    mesh.SetTVPatch(0,&firstTV,-1); mesh.SetTVPatch(1,&secondTV,-1);
    for(int p=0;p<2;++p) for(int c=0;c<4;++c) { VxVector point=vertices[corners[p][c]]; mesh.SetTV(p*4+c,point.x,point.y,-1); }
    mesh.SetIterationCount(1); mesh.BuildRenderMesh(); ASSERT_EQ(15,mesh.GetVertexCount());
    RCKMesh &api=mesh; CKDWORD stride; CKBYTE *uvs=api.GetModifierUVs(&stride,-1);
    ASSERT_EQ(reinterpret_cast<CKBYTE *>(mesh.GetTVs(-1)),uvs); ASSERT_EQ(8,api.GetModifierUVCount(-1));
    reinterpret_cast<VxUV *>(uvs+4*stride)->u+=10; reinterpret_cast<VxUV *>(uvs+7*stride)->u+=10;
    api.ModifierUVMove(); mesh.BuildRenderMesh(); ASSERT_EQ(18,mesh.GetVertexCount()); int matches[2],count=0;
    for(int i=0;i<mesh.GetVertexCount();++i) {
        VxVector point=Position(mesh,i);
        if(fabsf(point.x-1)<.0001f && fabsf(point.y-.5f)<.0001f && fabsf(point.z)<.0001f) { ASSERT_LT(count,2); matches[count++]=i; }
    }
    ASSERT_EQ(2,count); float u0,v0,u1,v1; mesh.GetVertexTextureCoordinates(matches[0],&u0,&v0,-1); mesh.GetVertexTextureCoordinates(matches[1],&u1,&v1,-1);
    EXPECT_TRUE((u0==1 && u1==11) || (u0==11 && u1==1)); EXPECT_FLOAT_EQ(.5f,v0); EXPECT_FLOAT_EQ(.5f,v1);
    IsVector(Normal(mesh,matches[0]),VxVector(0,0,-1)); IsVector(Normal(mesh,matches[1]),VxVector(0,0,-1));
}

TEST_F(PatchMeshModifierTest, TranslateBeforeFirstBuildMovesCornersAndEveryControlVector) {
    ModifierPatch mesh(Context); Quad(mesh); VxVector original[16]; for(int i=0;i<16;++i) original[i]=mesh.GetVerts()[i];
    RCKMesh &api=mesh; VxVector shift(4,-2,7); api.TranslateVertices(&shift);
    for(int i=0;i<16;++i) IsVector(mesh.GetVerts()[i],original[i]+shift);
    mesh.BuildRenderMesh(); IsVector(Position(mesh,0),shift); IsVector(Position(mesh,2),VxVector(7,1,7));
    int middle=Find(mesh,VxVector(5.5f,-.5f,7)); ASSERT_GE(middle,0); UV(mesh,middle,.5f,.5f);
}

TEST_F(PatchMeshModifierTest, TranslateAfterBuildSurvivesASeparateTessellationLevelChange) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; VxVector shift(-3,5,2); api.TranslateVertices(&shift);
    mesh.SetIterationCount(2); mesh.BuildRenderMesh(); EXPECT_EQ(16,mesh.GetVertexCount()); IsVector(mesh.GetVerts()[0],shift);
    IsVector(Position(mesh,2),VxVector(0,8,2)); const VxBbox &box=mesh.GetLocalBox(); IsVector(box.Min,shift); IsVector(box.Max,VxVector(0,8,2));
}

TEST_F(PatchMeshModifierTest, PivotScaleBeforeBuildTransformsAuthorControlsAndTheResultingSurface) {
    ModifierPatch mesh(Context); Quad(mesh); VxVector original[16]; for(int i=0;i<16;++i) original[i]=mesh.GetVerts()[i];
    RCKMesh &api=mesh; VxVector scale(2,3,4),pivot(1,1,1); api.ScaleVertices(&scale,&pivot);
    for(int i=0;i<16;++i) IsVector(mesh.GetVerts()[i],VxVector((original[i].x-1)*2+1,(original[i].y-1)*3+1,(original[i].z-1)*4+1));
    mesh.BuildRenderMesh(); IsVector(Position(mesh,0),VxVector(-1,-2,-3)); IsVector(Position(mesh,2),VxVector(5,7,-3));
}

TEST_F(PatchMeshModifierTest, ScaleVertices3fAfterBuildPersistsAcrossAnotherBuildAndRecomputesNormals) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; api.ScaleVertices3f(-2,3,1,nullptr);
    mesh.SetIterationCount(2); mesh.BuildRenderMesh(); IsVector(mesh.GetVerts()[2],VxVector(-6,9,0)); IsVector(Position(mesh,2),VxVector(-6,9,0));
    for(int i=0;i<mesh.GetVertexCount();++i) IsVector(Normal(mesh,i),VxVector(0,0,1));
}

TEST_F(PatchMeshModifierTest, RotateBeforeBuildUsesTheCombinedAuthorStreamAndIndependentQuarterTurn) {
    ModifierPatch mesh(Context); Quad(mesh); VxVector original[16]; for(int i=0;i<16;++i) original[i]=mesh.GetVerts()[i];
    RCKMesh &api=mesh; VxVector axis(0,0,1); api.RotateVertices(&axis,1.5707963267948966f);
    // Native row-vector axis rotation maps +X toward -Y for positive Z angles.
    for(int i=0;i<16;++i) IsVector(mesh.GetVerts()[i],VxVector(original[i].y,-original[i].x,original[i].z));
    mesh.BuildRenderMesh(); IsVector(Position(mesh,1),VxVector(0,-3,0)); IsVector(Position(mesh,2),VxVector(3,-3,0));
}

TEST_F(PatchMeshModifierTest, RotateAfterBuildPersistsAcrossRetessellationWithCorrectFacingNormals) {
    ModifierPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); RCKMesh &api=mesh; VxVector axis(1,0,0); api.RotateVertices(&axis,1.5707963267948966f);
    // The same native convention maps +Y toward -Z for positive X angles.
    mesh.SetIterationCount(2); mesh.BuildRenderMesh(); IsVector(mesh.GetVerts()[2],VxVector(3,0,-3)); IsVector(Position(mesh,2),VxVector(3,0,-3));
    for(int i=0;i<mesh.GetVertexCount();++i) IsVector(Normal(mesh,i),VxVector(0,-1,0));
}
} // namespace
