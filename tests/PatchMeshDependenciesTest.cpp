#include "TextureTestObserver.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"
#include "CKDependencies.h"

namespace {
class DependencyPatch : public RCKPatchMesh {
public:
    explicit DependencyPatch(CKContext *context) : RCKPatchMesh(context) {}
    void TextureChannels(int count) { m_TexturePatches.Resize(count); m_TextureChannelCount = count; }
    CKPatchChannel &Channel(int index) { return m_TexturePatches[index]; }
    int DerivedChannelCount() { return RCKMesh::GetChannelCount(); }
    int AddDerivedChannel(CKMaterial *material) { return RCKMesh::AddChannel(material,FALSE); }
};
class PatchDependencyMap : public CKDependenciesContext {
public:
    explicit PatchDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class PreparedPatchMaterial : public RCKMaterial {
public:
    explicit PreparedPatchMaterial(CKContext *context) : RCKMaterial(context) {}
    int Calls = 0;
    CKERROR Result = CK_OK;
    CKERROR PrepareDependencies(CKDependenciesContext &context) override {
        ++Calls; const CKERROR result = RCKMaterial::PrepareDependencies(context);
        return result == CK_OK ? Result : result;
    }
};
class VirtualMaterialPatch : public DependencyPatch {
public:
    explicit VirtualMaterialPatch(CKContext *context) : DependencyPatch(context) {}
    CKMaterial *Material = nullptr;
    int CountQueries = 0, MaterialQueries = 0;
    int GetMaterialCount() override { ++CountQueries; return 2; }
    CKMaterial *GetMaterial(int index) override { ++MaterialQueries; return index == 0 ? Material : nullptr; }
};

class PatchMeshDependenciesTest : public TextureTestWorld {
protected:
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
    }
    static void Options(CKDependencies &options, CKDWORD meshBits = 1, CKDWORD patchBits = 0) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM;
        options[CKCID_MESH] = meshBits; options[CKCID_PATCHMESH] = patchBits;
    }
    static void Quad(RCKPatchMesh &mesh, CKMaterial *material) {
        VxVector points[] = {VxVector(0,0,1), VxVector(3,0,1), VxVector(3,3,1), VxVector(0,3,1)};
        mesh.SetVertVecCount(4,12); mesh.SetPatchCount(1); mesh.SetEdgeCount(4);
        CKPatch patch; patch.type = CK_PATCH_QUAD; patch.Material = material ? material->GetID() : 0;
        for (int i = 0; i < 4; ++i) {
            mesh.SetVert(i,&points[i]); patch.v[i] = static_cast<short>(i); patch.edge[i] = static_cast<short>(i);
            patch.interior[i] = static_cast<short>(8+i);
            VxVector first = points[i] + (points[(i+1)%4] - points[i]) * (1.0f/3.0f);
            VxVector second = points[i] + (points[(i+1)%4] - points[i]) * (2.0f/3.0f);
            mesh.SetVec(2*i,&first); mesh.SetVec(2*i+1,&second);
            patch.vec[2*i] = static_cast<short>(2*i); patch.vec[2*i+1] = static_cast<short>(2*i+1);
            CKPatchEdge edge = {static_cast<short>(i), static_cast<short>(2*i), static_cast<short>(2*i+1), static_cast<short>((i+1)%4), 0, -1};
            mesh.SetEdge(i,&edge);
        }
        mesh.SetPatch(0,&patch);
        mesh.SetTVCount(4,-1); mesh.SetTVPatchCount(1,-1);
        CKTVPatch tv={{0,1,2,3}}; mesh.SetTVPatch(0,&tv,-1);
        for(int i=0;i<4;++i) mesh.SetTV(i,points[i].x/3,points[i].y/3,-1);
    }
    static void ExtraChannel(DependencyPatch &mesh, int index, CKMaterial *material) {
        mesh.Channel(index+1).Material = material ? material->GetID() : 0;
        mesh.Channel(index+1).Flags = VXBLEND_SRCALPHA;
        mesh.Channel(index+1).Type = VXBLEND_INVSRCALPHA;
        mesh.Channel(index+1).SubType = 0x120;
        mesh.SetTVCount(4,index); mesh.SetTVPatchCount(1,index);
        CKTVPatch tv={{0,1,2,3}}; mesh.SetTVPatch(0,&tv,index);
        for(int i=0;i<4;++i) mesh.SetTV(i,.25f+i,.75f-i,index);
    }
    static void BaseTriangle(RCKMesh &mesh, CKMaterial *material) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        VxVector points[]={VxVector(0,0,0),VxVector(1,0,0),VxVector(0,1,0)};
        for(int i=0;i<3;++i) mesh.SetVertexPosition(i,&points[i]);
        mesh.SetFaceVertexIndex(0,0,1,2); mesh.SetFaceMaterial(0,material);
    }
    static void DerivedMaterials(RCKPatchMesh &mesh, CKMaterial *patchMaterial, CKMaterial *extraMaterial) {
        ASSERT_EQ(2,mesh.GetFaceCount());
        for(int face=0;face<mesh.GetFaceCount();++face) EXPECT_EQ(patchMaterial,mesh.GetFaceMaterial(face));
        ASSERT_EQ(1,mesh.GetChannelCount()); EXPECT_EQ(extraMaterial,mesh.GetChannelMaterial(0));
        EXPECT_EQ(VXBLEND_SRCALPHA,mesh.GetChannelSourceBlend(0)); EXPECT_EQ(VXBLEND_INVSRCALPHA,mesh.GetChannelDestBlend(0));
        EXPECT_EQ(0x120u,mesh.GetChannelFlags(0));
        VxVector point; mesh.GetVertexPosition(0,&point); EXPECT_NEAR(1,point.z,.0001f);
    }
};

TEST_F(PatchMeshDependenciesTest, PrepareFindsAuthoredPatchAndExtraMaterialsBeforeAnyBuild) {
    DependencyPatch mesh(Context); RCKMaterial patch(Context), extra(Context), defaultMetadata(Context);
    Quad(mesh,&patch); mesh.TextureChannels(2); ExtraChannel(mesh,0,&extra); mesh.Channel(0).Material=defaultMetadata.GetID();
    ASSERT_EQ(0,mesh.GetFaceCount()); ASSERT_EQ(0,mesh.DerivedChannelCount()); ASSERT_EQ(1,mesh.GetChannelCount());
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK,mesh.PrepareDependencies(dependencies));
    EXPECT_TRUE(dependencies.IsDependenciesHere(mesh.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(patch.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(extra.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(defaultMetadata.GetID()));
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, PrepareKeepsLiveBaseMeshReferencesAlongsideAuthoredState) {
    DependencyPatch mesh(Context); RCKMaterial patch(Context), extra(Context), base(Context), liveChannel(Context);
    Quad(mesh,&patch); mesh.TextureChannels(2); ExtraChannel(mesh,0,&extra);
    BaseTriangle(mesh,&base); ASSERT_EQ(0,mesh.AddDerivedChannel(&liveChannel)); ASSERT_EQ(1,mesh.DerivedChannelCount());
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK,mesh.PrepareDependencies(dependencies));
    EXPECT_TRUE(dependencies.IsDependenciesHere(base.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(liveChannel.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(patch.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(extra.GetID()));
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, PrepareUsesMeshMaterialBitRatherThanPatchClassBits) {
    DependencyPatch mesh(Context); RCKMaterial patch(Context), extra(Context); Quad(mesh,&patch); mesh.TextureChannels(2); ExtraChannel(mesh,0,&extra);
    CKDependencies options; Options(options,0,0xFFFF); PatchDependencyMap disabled(Context); disabled.StartDependencies(&options);
    EXPECT_EQ(CK_OK,mesh.PrepareDependencies(disabled)); EXPECT_FALSE(disabled.IsDependenciesHere(patch.GetID())); EXPECT_FALSE(disabled.IsDependenciesHere(extra.GetID()));
    EXPECT_TRUE(disabled.m_DynamicObjects.IsEmpty()); disabled.StopDependencies();
    Options(options,1,0); PatchDependencyMap enabled(Context); enabled.StartDependencies(&options);
    EXPECT_EQ(CK_OK,mesh.PrepareDependencies(enabled)); EXPECT_TRUE(enabled.IsDependenciesHere(patch.GetID())); EXPECT_TRUE(enabled.IsDependenciesHere(extra.GetID()));
    EXPECT_TRUE(enabled.m_DynamicObjects.IsEmpty()); enabled.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, PrepareHonorsVirtualMaterialEnumerationAndIgnoresChildErrors) {
    VirtualMaterialPatch mesh(Context); PreparedPatchMaterial material(Context); material.Result=CKERR_INVALIDPARAMETER; mesh.Material=&material;
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK,mesh.PrepareDependencies(dependencies)); EXPECT_GE(mesh.CountQueries,1); EXPECT_EQ(2,mesh.MaterialQueries); EXPECT_EQ(1,material.Calls);
    EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID())); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, PreparePropagatesBaseSaveFailureBeforeTraversingMaterials) {
    DependencyPatch mesh(Context); PreparedPatchMaterial material(Context); Quad(mesh,&material); mesh.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED,0);
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS,mesh.PrepareDependencies(dependencies)); EXPECT_EQ(0,material.Calls);
    EXPECT_FALSE(dependencies.IsDependenciesHere(mesh.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(material.GetID()));
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, PrepareFinishesPatchClassAndDoesNotTraverseTwice) {
    DependencyPatch mesh(Context); PreparedPatchMaterial material(Context); Quad(mesh,&material);
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK,mesh.PrepareDependencies(dependencies)); EXPECT_EQ(1,material.Calls); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
    EXPECT_EQ(CKERR_ALREADYPRESENT,mesh.PrepareDependencies(dependencies)); EXPECT_EQ(1,material.Calls); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
    dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, RemapBeforeBuildFeedsMappedPatchAndChannelMaterialsToFaces) {
    DependencyPatch mesh(Context); RCKMaterial patch(Context), extra(Context), patchCopy(Context), extraCopy(Context), metadata(Context), metadataCopy(Context);
    Quad(mesh,&patch); mesh.TextureChannels(2); ExtraChannel(mesh,0,&extra); mesh.Channel(0).Material=metadata.GetID();
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(patch.GetID(),patchCopy.GetID()); dependencies.Map(extra.GetID(),extraCopy.GetID()); dependencies.Map(metadata.GetID(),metadataCopy.GetID());
    EXPECT_EQ(CK_OK,mesh.RemapDependencies(dependencies)); EXPECT_EQ(&patchCopy,mesh.GetPatchMaterial(0));
    EXPECT_EQ(extraCopy.GetID(),mesh.Channel(1).Material); EXPECT_EQ(metadataCopy.GetID(),mesh.Channel(0).Material);
    mesh.BuildRenderMesh(); DerivedMaterials(mesh,&patchCopy,&extraCopy); dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, RemapKeepsBuiltAndAuthoredMaterialStateConsistentAcrossRebuild) {
    DependencyPatch mesh(Context); RCKMaterial patch(Context), extra(Context), patchCopy(Context), extraCopy(Context);
    Quad(mesh,&patch); mesh.TextureChannels(2); ExtraChannel(mesh,0,&extra); mesh.BuildRenderMesh(); DerivedMaterials(mesh,&patch,&extra);
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(patch.GetID(),patchCopy.GetID()); dependencies.Map(extra.GetID(),extraCopy.GetID());
    EXPECT_EQ(CK_OK,mesh.RemapDependencies(dependencies)); EXPECT_EQ(&patchCopy,mesh.GetFaceMaterial(0)); EXPECT_EQ(&extraCopy,mesh.GetChannelMaterial(0));
    EXPECT_EQ(&patchCopy,mesh.GetPatchMaterial(0)); EXPECT_EQ(extraCopy.GetID(),mesh.Channel(1).Material);
    EXPECT_EQ(0u,mesh.GetPatchFlags() & (CK_PATCHMESH_UPTODATE|CK_PATCHMESH_MATERIALSUPTODATE));
    mesh.BuildRenderMesh(); DerivedMaterials(mesh,&patchCopy,&extraCopy); mesh.CleanRenderMesh(); mesh.BuildRenderMesh(); DerivedMaterials(mesh,&patchCopy,&extraCopy);
    dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, RemapDisabledMeshBitPreservesAuthoredBaseAndFlagState) {
    DependencyPatch mesh(Context); RCKMaterial patch(Context), extra(Context), replacement(Context); Quad(mesh,&patch); mesh.TextureChannels(2); ExtraChannel(mesh,0,&extra); mesh.BuildRenderMesh();
    mesh.Channel(0).Material=patch.GetID(); mesh.SetPatchFlags(15);
    CKDependencies options; Options(options,0,0xFFFF); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(patch.GetID(),replacement.GetID()); dependencies.Map(extra.GetID(),0);
    EXPECT_EQ(CK_OK,mesh.RemapDependencies(dependencies)); EXPECT_EQ(&patch,mesh.GetPatchMaterial(0)); EXPECT_EQ(patch.GetID(),mesh.Channel(0).Material);
    EXPECT_EQ(extra.GetID(),mesh.Channel(1).Material); EXPECT_EQ(&patch,mesh.GetFaceMaterial(0)); EXPECT_EQ(&extra,mesh.GetChannelMaterial(0)); EXPECT_EQ(15u,mesh.GetPatchFlags());
    dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, RemapHandlesExplicitZeroAndUnmappedIdsWithoutChangingSourceRecords) {
    DependencyPatch source(Context), mesh(Context); RCKMaterial patch(Context), extra(Context), metadata(Context);
    Quad(source,&patch); source.TextureChannels(2); ExtraChannel(source,0,&extra); source.Channel(0).Material=metadata.GetID();
    Quad(mesh,&patch); mesh.TextureChannels(2); ExtraChannel(mesh,0,&extra); mesh.Channel(0).Material=metadata.GetID(); mesh.SetPatchFlags(15);
    CKDependencies options; Options(options); PatchDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(patch.GetID(),0); dependencies.Map(metadata.GetID(),0);
    EXPECT_EQ(CK_OK,mesh.RemapDependencies(dependencies)); EXPECT_EQ(nullptr,mesh.GetPatchMaterial(0)); EXPECT_EQ(0u,mesh.GetPatches()[0].Material);
    EXPECT_EQ(0u,mesh.Channel(0).Material); EXPECT_EQ(extra.GetID(),mesh.Channel(1).Material); EXPECT_EQ(10u,mesh.GetPatchFlags());
    mesh.BuildRenderMesh(); DerivedMaterials(mesh,nullptr,&extra);
    EXPECT_EQ(&patch,source.GetPatchMaterial(0)); EXPECT_EQ(metadata.GetID(),source.Channel(0).Material); EXPECT_EQ(extra.GetID(),source.Channel(1).Material);
    dependencies.StopDependencies();
}

TEST_F(PatchMeshDependenciesTest, ActualContextCopyClonesAuthoredMaterialsBeforeFirstTessellation) {
    DependencyPatch source(Context); RCKMaterial patch(Context), extra(Context); Quad(source,&patch); source.TextureChannels(2); ExtraChannel(source,0,&extra);
    ASSERT_EQ(0,source.GetFaceCount()); CKDependencies options; Options(options);
    RCKPatchMesh *copy=static_cast<RCKPatchMesh *>(Context->CopyObject(&source,&options)); ASSERT_NE(nullptr,copy); EXPECT_NE(&source,copy);
    CKMaterial *copiedPatch=copy->GetPatchMaterial(0); ASSERT_NE(nullptr,copiedPatch); EXPECT_NE(&patch,copiedPatch);
    copy->BuildRenderMesh(); CKMaterial *copiedExtra=copy->GetChannelMaterial(0); ASSERT_NE(nullptr,copiedExtra); EXPECT_NE(&extra,copiedExtra);
    DerivedMaterials(*copy,copiedPatch,copiedExtra);
    EXPECT_EQ(&patch,source.GetPatchMaterial(0)); EXPECT_EQ(extra.GetID(),source.Channel(1).Material); EXPECT_EQ(0,source.GetFaceCount());
}
}
