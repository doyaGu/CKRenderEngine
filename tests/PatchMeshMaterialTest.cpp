#include "TextureTestObserver.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"
#include "CKDependencies.h"

namespace {
class MaterialPatch : public RCKPatchMesh {
public:
    explicit MaterialPatch(CKContext *context) : RCKPatchMesh(context) {}
    void Channels(int count) { m_TexturePatches.Resize(count); m_TextureChannelCount=count; }
    CKPatchChannel &Channel(int index) { return m_TexturePatches[index]; }
};
class PatchMaterialObserver : public TextureTestObserver {
public:
    int Count=0;
    float Tags[32]={};
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE,CKWORD *indices,int count,VxDrawPrimitiveData *data) override {
        EXPECT_NE(nullptr,indices); EXPECT_NE(nullptr,data); EXPECT_GT(count,0); EXPECT_LT(Count,32);
        if(!indices||!data||count<=0||Count>=32) return FALSE;
        EXPECT_NE(nullptr,data->PositionPtr);
        for(int i=0;i<count;++i) EXPECT_LT(indices[i],data->VertexCount);
        Tags[Count++]=Material.Diffuse.r; return TRUE;
    }
};
class PatchMeshMaterialTest : public TextureTestWorld {
protected:
    PatchMaterialObserver Target;
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMesh,CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKPatchMesh,CKCID_MESH);
        CKCLASSREGISTERCID(RCKMaterial,CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        Device->DetachAll(); Device->m_RasterizerContext=&Target;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Target,0)); Manager->m_VertexCache.Value=0;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage=1; Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage=1;
        Device->m_DisplayWireframe=FALSE;
    }
    static void Quads(RCKPatchMesh &mesh,CKMaterial *first,CKMaterial *second=nullptr,int count=1) {
        mesh.SetVertVecCount(4*count,12*count); mesh.SetPatchCount(count); mesh.SetEdgeCount(4*count);
        for(int p=0;p<count;++p){
            const int vb=p*4,cb=p*12;
            VxVector points[]={VxVector(4.0f*p,0,1),VxVector(4.0f*p+3,0,1),VxVector(4.0f*p+3,3,1),VxVector(4.0f*p,3,1)};
            CKPatch patch; patch.type=CK_PATCH_QUAD; CKMaterial *material=p==0?first:second; patch.Material=material?material->GetID():0;
            for(int i=0;i<4;++i){
                mesh.SetVert(vb+i,&points[i]); patch.v[i]=static_cast<short>(vb+i); patch.edge[i]=static_cast<short>(vb+i); patch.interior[i]=static_cast<short>(cb+8+i);
                VxVector a=points[i]+(points[(i+1)%4]-points[i])*(1.0f/3.0f),b=points[i]+(points[(i+1)%4]-points[i])*(2.0f/3.0f);
                mesh.SetVec(cb+2*i,&a); mesh.SetVec(cb+2*i+1,&b); patch.vec[2*i]=static_cast<short>(cb+2*i); patch.vec[2*i+1]=static_cast<short>(cb+2*i+1);
                CKPatchEdge edge={static_cast<short>(vb+i),static_cast<short>(cb+2*i),static_cast<short>(cb+2*i+1),static_cast<short>(vb+(i+1)%4),static_cast<short>(p),-1};
                mesh.SetEdge(vb+i,&edge);
            }
            mesh.SetPatch(p,&patch);
        }
        mesh.SetFlags(mesh.GetFlags()&~(VXMESH_STRIPIFY|VXMESH_RENDERCHANNELS));
    }
    static void Extra(MaterialPatch &mesh,CKMaterial *material) {
        mesh.Channels(2); mesh.Channel(1).Material=material?material->GetID():0;
        mesh.Channel(1).Flags=VXBLEND_SRCALPHA; mesh.Channel(1).Type=VXBLEND_INVSRCALPHA;
        mesh.SetTVCount(4,0); mesh.SetTVPatchCount(1,0); CKTVPatch tv={{0,1,2,3}}; mesh.SetTVPatch(0,&tv,0);
        for(int i=0;i<4;++i) mesh.SetTV(i,.25f+i,.75f-i,0);
    }
    void Draw(RCKPatchMesh &mesh,float tag) {
        mesh.BuildRenderMesh(); Target.Count=0; EXPECT_EQ(1,mesh.DefaultRender(Device,nullptr)); ASSERT_GT(Target.Count,0);
        for(int i=0;i<Target.Count;++i) EXPECT_FLOAT_EQ(tag,Target.Tags[i]);
    }
    static void Faces(RCKPatchMesh &mesh,CKMaterial *material) {
        ASSERT_GT(mesh.GetFaceCount(),0);
        for(int i=0;i<mesh.GetFaceCount();++i) EXPECT_EQ(material,mesh.GetFaceMaterial(i));
    }
};

TEST_F(PatchMeshMaterialTest, ReplaceBeforeFirstBuildUpdatesAuthoredMaterialAndActualDraw) {
    MaterialPatch mesh(Context); RCKMaterial old(Context),replacement(Context); replacement.SetDiffuse(VxColor(.375f,.2f,.7f,1.0f));
    Quads(mesh,&old); ASSERT_EQ(0,mesh.GetFaceCount()); mesh.ReplaceMaterial(&old,&replacement);
    EXPECT_EQ(&replacement,mesh.GetPatchMaterial(0)); Draw(mesh,.375f); Faces(mesh,&replacement);
}

TEST_F(PatchMeshMaterialTest, ReplaceKeepsImmediateFacesAndRepeatedBuildsInAgreement) {
    MaterialPatch mesh(Context); RCKMaterial old(Context),replacement(Context); old.SetDiffuse(VxColor(.125f,0.0f,0.0f,1.0f)); replacement.SetDiffuse(VxColor(.625f,0.0f,0.0f,1.0f));
    Quads(mesh,&old); Draw(mesh,.125f); mesh.ReplaceMaterial(&old,&replacement); Faces(mesh,&replacement);
    EXPECT_EQ(&replacement,mesh.GetPatchMaterial(0)); EXPECT_EQ(0u,mesh.GetPatchFlags()&5); Draw(mesh,.625f);
    mesh.CleanRenderMesh(); Draw(mesh,.625f); Faces(mesh,&replacement);
}

TEST_F(PatchMeshMaterialTest, ReplaceChangesOnlyMatchingPatchesAndPreservesExtraChannels) {
    MaterialPatch mesh(Context); RCKMaterial old(Context),other(Context),replacement(Context); Quads(mesh,&old,&other,2); mesh.BuildRenderMesh();
    mesh.ReplaceMaterial(&old,&replacement); EXPECT_EQ(&replacement,mesh.GetPatchMaterial(0)); EXPECT_EQ(&other,mesh.GetPatchMaterial(1));
    mesh.BuildRenderMesh(); EXPECT_EQ(&replacement,mesh.GetFaceMaterial(0)); EXPECT_EQ(&replacement,mesh.GetFaceMaterial(1));
    EXPECT_EQ(&other,mesh.GetFaceMaterial(2)); EXPECT_EQ(&other,mesh.GetFaceMaterial(3));
    Quads(mesh,&old); Extra(mesh,&old); mesh.BuildRenderMesh(); ASSERT_EQ(1,mesh.GetChannelCount());
    mesh.ReplaceMaterial(&old,&replacement); mesh.BuildRenderMesh(); Faces(mesh,&replacement);
    EXPECT_EQ(old.GetID(),mesh.Channel(1).Material); EXPECT_EQ(&old,mesh.GetChannelMaterial(0));
}

TEST_F(PatchMeshMaterialTest, ReplaceSupportsNullAssignmentsAndSamePointerIsAnExactNoOp) {
    MaterialPatch mesh(Context); RCKMaterial material(Context); material.SetDiffuse(VxColor(.4375f,0.0f,0.0f,1.0f)); Quads(mesh,nullptr); mesh.BuildRenderMesh();
    mesh.SetPatchFlags(7); mesh.ReplaceMaterial(nullptr,nullptr); EXPECT_EQ(7u,mesh.GetPatchFlags());
    mesh.ReplaceMaterial(nullptr,&material); EXPECT_EQ(&material,mesh.GetPatchMaterial(0)); Faces(mesh,&material); Draw(mesh,.4375f);
    const CKDWORD flags=mesh.GetPatchFlags(); mesh.ReplaceMaterial(&material,&material); EXPECT_EQ(flags,mesh.GetPatchFlags());
    mesh.ReplaceMaterial(&material,nullptr); EXPECT_EQ(nullptr,mesh.GetPatchMaterial(0)); Faces(mesh,nullptr); mesh.BuildRenderMesh(); Faces(mesh,nullptr);
}

TEST_F(PatchMeshMaterialTest, ApplyGlobalBeforeBuildAssignsEveryAuthoredPatchAndDrawsOneMaterial) {
    MaterialPatch mesh(Context); RCKMaterial first(Context),second(Context),global(Context); global.SetDiffuse(VxColor(.8125f,0.0f,0.0f,1.0f)); Quads(mesh,&first,&second,2);
    mesh.ApplyGlobalMaterial(&global); EXPECT_EQ(&global,mesh.GetPatchMaterial(0)); EXPECT_EQ(&global,mesh.GetPatchMaterial(1));
    Draw(mesh,.8125f); Faces(mesh,&global); EXPECT_EQ(4,mesh.GetFaceCount());
}

TEST_F(PatchMeshMaterialTest, ApplyGlobalAfterBuildSurvivesControlEditsAndCleanRebuild) {
    MaterialPatch mesh(Context); RCKMaterial first(Context),global(Context); first.SetDiffuse(VxColor(.0625f,0.0f,0.0f,1.0f)); global.SetDiffuse(VxColor(.9375f,0.0f,0.0f,1.0f));
    Quads(mesh,&first); Draw(mesh,.0625f); mesh.ApplyGlobalMaterial(&global); Faces(mesh,&global); EXPECT_EQ(&global,mesh.GetPatchMaterial(0));
    EXPECT_EQ(0u,mesh.GetPatchFlags()&5); Draw(mesh,.9375f);
    VxVector point(0,3,2); mesh.SetVert(3,&point); Draw(mesh,.9375f); mesh.CleanRenderMesh(); Draw(mesh,.9375f); Faces(mesh,&global);
}

TEST_F(PatchMeshMaterialTest, ApplyGlobalNullClearsBasePatchesWithoutReplacingExtraMaterial) {
    MaterialPatch mesh(Context); RCKMaterial first(Context),extra(Context); Quads(mesh,&first); Extra(mesh,&extra); mesh.BuildRenderMesh(); ASSERT_EQ(1,mesh.GetChannelCount());
    mesh.ApplyGlobalMaterial(nullptr); EXPECT_EQ(nullptr,mesh.GetPatchMaterial(0)); Faces(mesh,nullptr); mesh.BuildRenderMesh(); Faces(mesh,nullptr);
    EXPECT_EQ(extra.GetID(),mesh.Channel(1).Material); EXPECT_EQ(&extra,mesh.GetChannelMaterial(0));
}

TEST_F(PatchMeshMaterialTest, CheckPostDeletionClearsMissingAuthoredIdsAndKeepsDefaultMetadata) {
    MaterialPatch mesh(Context); RCKMaterial live(Context); Quads(mesh,&live,&live,2); mesh.Channels(3);
    mesh.GetPatches()[0].Material=0xFFFFFFFDu; mesh.Channel(0).Material=0xFFFFFFFCu; mesh.Channel(1).Material=0xFFFFFFFBu; mesh.Channel(2).Material=live.GetID();
    mesh.SetPatchFlags(15); mesh.CheckPostDeletion(); EXPECT_EQ(0u,mesh.GetPatches()[0].Material); EXPECT_EQ(live.GetID(),mesh.GetPatches()[1].Material);
    EXPECT_EQ(0xFFFFFFFCu,mesh.Channel(0).Material); EXPECT_EQ(0u,mesh.Channel(1).Material); EXPECT_EQ(live.GetID(),mesh.Channel(2).Material); EXPECT_EQ(10u,mesh.GetPatchFlags());
}

TEST_F(PatchMeshMaterialTest, CheckPostDeletionPreservesLiveMaterialIdsAndCurrentFlags) {
    MaterialPatch mesh(Context); RCKMaterial material(Context),extra(Context); Quads(mesh,&material); Extra(mesh,&extra); mesh.BuildRenderMesh();
    const CKDWORD flags=mesh.GetPatchFlags(); mesh.CheckPostDeletion(); EXPECT_EQ(flags,mesh.GetPatchFlags());
    EXPECT_EQ(&material,mesh.GetPatchMaterial(0)); EXPECT_EQ(extra.GetID(),mesh.Channel(1).Material); Faces(mesh,&material); EXPECT_EQ(&extra,mesh.GetChannelMaterial(0));
}

TEST_F(PatchMeshMaterialTest, ActualContextDeletionClearsAuthoredMaterialBeforeAnyTessellation) {
    RCKPatchMesh *mesh=static_cast<RCKPatchMesh *>(Context->CreateObject(CKCID_PATCHMESH));
    CKMaterial *material=static_cast<CKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr,mesh); ASSERT_NE(nullptr,material);
    Quads(*mesh,material); const CK_ID id=material->GetID(); ASSERT_EQ(0,mesh->GetFaceCount()); CKDependencies options; options.m_Flags=CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK,Context->DestroyObject(material,0,&options)); EXPECT_EQ(nullptr,Context->GetObject(id)); EXPECT_EQ(0u,mesh->GetPatches()[0].Material);
    mesh->BuildRenderMesh(); Faces(*mesh,nullptr);
}

TEST_F(PatchMeshMaterialTest, ActualContextDeletionKeepsLiveInheritedFacesAndAuthoringConsistent) {
    RCKPatchMesh *mesh=static_cast<RCKPatchMesh *>(Context->CreateObject(CKCID_PATCHMESH));
    CKMaterial *material=static_cast<CKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr,mesh); ASSERT_NE(nullptr,material);
    Quads(*mesh,material); mesh->BuildRenderMesh(); Faces(*mesh,material); CKDependencies options; options.m_Flags=CK_DEPENDENCIES_NONE;
    ASSERT_EQ(CK_OK,Context->DestroyObject(material,0,&options)); EXPECT_EQ(0u,mesh->GetPatches()[0].Material); Faces(*mesh,nullptr);
    mesh->BuildRenderMesh(); Faces(*mesh,nullptr); mesh->CleanRenderMesh(); mesh->BuildRenderMesh(); Faces(*mesh,nullptr);
}
}
