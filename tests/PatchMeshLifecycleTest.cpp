#include "TextureTestObserver.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"
#include "CKDependencies.h"

namespace {
class LifecyclePatch : public RCKPatchMesh {
public:
    explicit LifecyclePatch(CKContext *context) : RCKPatchMesh(context) {}
    using EdgeRecord = EdgeTessInfo;
    CKPatchChannel &Channel(int index) { return m_TexturePatches[index]; }
    void AllocateChannels(int count) { m_TexturePatches.Resize(count); m_TextureChannelCount = count; }
    int CallbackCount() const { return m_RenderCallbacks ? m_RenderCallbacks->m_PreCallBacks.Size() : 0; }
    bool Dirty() const { return m_PatchChanged != FALSE; }
    int Builds = 0;
    void BuildRenderMesh() override { ++Builds; RCKPatchMesh::BuildRenderMesh(); }
    void RecordLayout() {
        EXPECT_EQ(0u,m_TessVertexBase);EXPECT_EQ(0u,m_TessFaceBase);EXPECT_EQ(0u,m_TessEdgeVertexCount);
        EXPECT_EQ(0u,m_TessInteriorVertexCount);EXPECT_EQ(0u,m_TessTotalVertices);
        EXPECT_EQ(0u,m_TessWorkData0);EXPECT_EQ(0u,m_TessWorkData1);EXPECT_EQ(0u,m_TessWorkData2);
        for (int i=0;i<3;++i) { EXPECT_FLOAT_EQ(0,m_TessWorkVectors[i].x);EXPECT_FLOAT_EQ(0,m_TessWorkVectors[i].y);EXPECT_FLOAT_EQ(0,m_TessWorkVectors[i].z); }
#define PATCH_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        PATCH_FIELD(m_Verts); PATCH_FIELD(m_Vecs); PATCH_FIELD(m_VertCount); PATCH_FIELD(m_VecCount);
        PATCH_FIELD(m_PatchEdges); PATCH_FIELD(m_Patches); PATCH_FIELD(m_TexturePatches);
        PATCH_FIELD(m_PatchFlags); PATCH_FIELD(m_TextureChannelCount); PATCH_FIELD(m_IterationCount);
        PATCH_FIELD(m_TessVertexBase); PATCH_FIELD(m_TessFaceBase); PATCH_FIELD(m_TessEdgeVertexCount);
        PATCH_FIELD(m_TessInteriorVertexCount); PATCH_FIELD(m_TessTotalVertices); PATCH_FIELD(m_TessWorkVectors);
        PATCH_FIELD(m_TessWorkData0); PATCH_FIELD(m_TessWorkData1); PATCH_FIELD(m_TessWorkData2);
        PATCH_FIELD(m_PatchChanged); PATCH_FIELD(m_CornerVertexMap); PATCH_FIELD(m_SharedVertexSources);
        PATCH_FIELD(m_SmoothEdgeFlags); PATCH_FIELD(m_HardEdgeFlags);
#undef PATCH_FIELD
        ::testing::Test::RecordProperty("patch_size", static_cast<int>(sizeof(RCKPatchMesh)));
        ::testing::Test::RecordProperty("patch_alignment", static_cast<int>(alignof(RCKPatchMesh)));
        ::testing::Test::RecordProperty("mesh_base_size", static_cast<int>(sizeof(RCKMesh)));
        ::testing::Test::RecordProperty("class_id", static_cast<int>(m_ClassID));
    }
};
class SaveFailurePatch : public LifecyclePatch {
public:
    explicit SaveFailurePatch(CKContext *context) : LifecyclePatch(context) {}
    bool Refuse = true;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override {
        return Refuse ? nullptr : RCKPatchMesh::Save(file, flags);
    }
};
class LoadFailurePatch : public LifecyclePatch {
public:
    explicit LoadFailurePatch(CKContext *context) : LifecyclePatch(context) {}
    int Loads = 0;
    bool SawPatchWire = false;
    CKERROR Load(CKStateChunk *chunk, CKFile *) override {
        ++Loads; chunk->StartRead(); SawPatchWire = chunk->SeekIdentifier(CK_STATESAVE_PATCHMESHDATA3) != FALSE;
        return CKERR_INVALIDPARAMETER;
    }
};
class PatchMeshLifecycleTest : public TextureTestWorld {
protected:
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
    }
    static void Quad(RCKPatchMesh &mesh, float z = 1) {
        const float xy[16][2] = {{0,0},{2,0},{2,3},{0,3},{2.0f/3,0},{4.0f/3,0},
            {2,1},{2,2},{4.0f/3,3},{2.0f/3,3},{0,2},{0,1},
            {2.0f/3,1},{4.0f/3,1},{4.0f/3,2},{2.0f/3,2}};
        mesh.SetVertVecCount(4,12);
        for (int i=0;i<16;++i) { VxVector p(xy[i][0],xy[i][1],z); if(i<4)mesh.SetVert(i,&p);else mesh.SetVec(i-4,&p); }
        mesh.SetPatchCount(1);mesh.SetEdgeCount(4);
        CKPatch patch;patch.type=CK_PATCH_QUAD;patch.Material=0;
        for(int i=0;i<4;++i){patch.v[i]=static_cast<short>(i);patch.edge[i]=static_cast<short>(i);patch.interior[i]=static_cast<short>(8+i);}
        for(int i=0;i<8;++i)patch.vec[i]=static_cast<short>(i);
        mesh.SetPatch(0,&patch);
        for(int i=0;i<4;++i){CKPatchEdge edge;edge.v1=static_cast<short>(i);edge.v2=static_cast<short>((i+1)%4);
            edge.vec12=static_cast<short>(2*i);edge.vec21=static_cast<short>(2*i+1);edge.patch1=0;edge.patch2=-1;mesh.SetEdge(i,&edge);}
        mesh.SetIterationCount(1);
    }
    static void Plane(RCKPatchMesh &mesh,float z) {
        mesh.BuildRenderMesh();ASSERT_EQ(9,mesh.GetVertexCount());ASSERT_EQ(8,mesh.GetFaceCount());
        for(int i=0;i<mesh.GetVertexCount();++i){VxVector p;mesh.GetVertexPosition(i,&p);EXPECT_NEAR(z,p.z,.0001f);EXPECT_GE(p.x,-.0001f);EXPECT_LE(p.x,2.0001f);EXPECT_GE(p.y,-.0001f);EXPECT_LE(p.y,3.0001f);}
    }
    static void Consume(CKRenderContext *,CK3dEntity *,CKMesh *mesh,void *data) {
        int *calls=static_cast<int *>(data);++*calls;EXPECT_EQ(9,mesh->GetVertexCount());EXPECT_EQ(8,mesh->GetFaceCount());
        VxVector p;mesh->GetVertexPosition(0,&p);EXPECT_NEAR(1,p.z,.0001f);
    }
};

TEST_F(PatchMeshLifecycleTest, RegisteredFactoryAndClassHelpersUseActualContextIdentity) {
    CKObject *object=Context->CreateObject(CKCID_PATCHMESH,"Registered patch");ASSERT_NE(nullptr,object);
    EXPECT_EQ(CKCID_PATCHMESH,object->GetClassID());EXPECT_EQ(object,Context->GetObject(object->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(object,CKCID_MESH));EXPECT_STREQ("Patch Mesh",RCKPatchMesh::GetClassName());
    for(int mode=0;mode<4;++mode){EXPECT_EQ(0,RCKPatchMesh::GetDependenciesCount(mode));EXPECT_EQ(nullptr,RCKPatchMesh::GetDependencies(0,mode));}
    CKClassDesc *description=CKGetClassDesc(CKCID_PATCHMESH);ASSERT_NE(nullptr,description);
    EXPECT_TRUE((description->DefaultOptions&CK_GENERALOPTIONS_CANUSECURRENTOBJECT)!=0);
    EXPECT_EQ(CKPGUID_PATCHMESH,description->Parameter);EXPECT_TRUE(description->ToBeNotify.IsSet(CKCID_MATERIAL));
    CKPatchMesh *direct=RCKPatchMesh::CreateInstance(Context);ASSERT_NE(nullptr,direct);EXPECT_EQ(CKCID_PATCHMESH,direct->GetClassID());delete direct;
}

TEST_F(PatchMeshLifecycleTest, PortableLayoutsRecordEveryOwnedAndDerivedField) {
    LifecyclePatch mesh(Context);mesh.RecordLayout();EXPECT_EQ(0,mesh.GetVertCount());EXPECT_EQ(0,mesh.GetVecCount());
    EXPECT_EQ(0,mesh.GetPatchCount());EXPECT_EQ(0,mesh.GetIterationCount());EXPECT_TRUE(mesh.Dirty());EXPECT_EQ(1,mesh.CallbackCount());
    CKPatchChannel channel;
    EXPECT_EQ(0,channel.Patches.Size());EXPECT_EQ(0,channel.UVs.Size());EXPECT_EQ(0u,channel.Material);
    EXPECT_EQ(0u,channel.Flags);EXPECT_EQ(0u,channel.Type);EXPECT_EQ(0u,channel.SubType);
#define CHANNEL_FIELD(field) RecordProperty("channel_" #field "_offset",static_cast<int>(offsetof(CKPatchChannel,field)));RecordProperty("channel_" #field "_size",static_cast<int>(sizeof(channel.field)))
    CHANNEL_FIELD(Patches);CHANNEL_FIELD(UVs);CHANNEL_FIELD(Material);CHANNEL_FIELD(Flags);CHANNEL_FIELD(Type);CHANNEL_FIELD(SubType);
#undef CHANNEL_FIELD
    RecordProperty("channel_size",static_cast<int>(sizeof(CKPatchChannel)));RecordProperty("channel_alignment",static_cast<int>(alignof(CKPatchChannel)));
    RecordProperty("edge_record_size",static_cast<int>(sizeof(LifecyclePatch::EdgeRecord)));RecordProperty("edge_record_alignment",static_cast<int>(alignof(LifecyclePatch::EdgeRecord)));
    RecordProperty("edge_BasePatch1_offset",static_cast<int>(offsetof(LifecyclePatch::EdgeRecord,BasePatch1)));
    RecordProperty("edge_BasePatch2_offset",static_cast<int>(offsetof(LifecyclePatch::EdgeRecord,BasePatch2)));
    RecordProperty("edge_Hard_offset",static_cast<int>(offsetof(LifecyclePatch::EdgeRecord,Hard)));
    EXPECT_EQ(12u,sizeof(LifecyclePatch::EdgeRecord));EXPECT_EQ(sizeof(void*)==4?40u:64u,sizeof(CKPatchChannel));
}

TEST_F(PatchMeshLifecycleTest, ChannelCopiesOwnTheirArraysAfterSourceDestruction) {
    CKPatchChannel copy;
    {CKPatchChannel source;source.UVs.Resize(2);source.Patches.Resize(1);source.UVs[0].u=.125f;source.UVs[1].v=.875f;
        source.Patches[0].tv[0]=1;source.Material=123;source.Flags=4;source.Type=5;source.SubType=6;copy=source;
        EXPECT_NE(source.UVs.Begin(),copy.UVs.Begin());EXPECT_NE(source.Patches.Begin(),copy.Patches.Begin());source.UVs[0].u=9;}
    EXPECT_FLOAT_EQ(.125f,copy.UVs[0].u);EXPECT_FLOAT_EQ(.875f,copy.UVs[1].v);EXPECT_EQ(1,copy.Patches[0].tv[0]);
    EXPECT_EQ(123u,copy.Material);EXPECT_EQ(4u,copy.Flags);EXPECT_EQ(5u,copy.Type);EXPECT_EQ(6u,copy.SubType);
}

TEST_F(PatchMeshLifecycleTest, ClearKeepsNativeInactiveConfigurationAndCanBuildAgain) {
    LifecyclePatch mesh(Context);Quad(mesh);Plane(mesh,1);mesh.Channel(0).Material=77;mesh.Channel(0).Flags=9;
    mesh.Clear();EXPECT_EQ(nullptr,mesh.GetVerts());EXPECT_EQ(nullptr,mesh.GetVecs());EXPECT_EQ(0,mesh.GetPatchCount());
    EXPECT_EQ(4,mesh.GetEdgeCount());EXPECT_EQ(1,mesh.GetIterationCount());EXPECT_EQ(2u,mesh.GetPatchFlags());
    EXPECT_EQ(77u,mesh.Channel(0).Material);EXPECT_EQ(9u,mesh.Channel(0).Flags);EXPECT_EQ(1,mesh.CallbackCount());
    Quad(mesh,2);Plane(mesh,2);
}

TEST_F(PatchMeshLifecycleTest, RegisteredPreRenderCallbackBuildsTheActualMeshConsumerOnce) {
    LifecyclePatch mesh(Context);Quad(mesh);int consumed=0;mesh.SetRenderCallBack(Consume,&consumed);
    EXPECT_EQ(CK_OK,mesh.Render(Device,nullptr));EXPECT_EQ(1,mesh.Builds);EXPECT_EQ(1,consumed);EXPECT_EQ(1,mesh.CallbackCount());
    EXPECT_EQ(CK_OK,mesh.Render(Device,nullptr));EXPECT_EQ(2,mesh.Builds);EXPECT_EQ(2,consumed);EXPECT_EQ(1,mesh.CallbackCount());
}

TEST_F(PatchMeshLifecycleTest, CopyOwnsControlDataAfterSourceDestructionAndRebuildsIndependently) {
    LifecyclePatch target(Context);CKDependenciesContext dependencies(Context);
    {LifecyclePatch source(Context);Quad(source);Plane(source,1);ASSERT_EQ(CK_OK,target.Copy(source,dependencies));
        EXPECT_NE(source.GetVerts(),target.GetVerts());EXPECT_NE(source.GetVecs(),target.GetVecs());EXPECT_EQ(1,target.CallbackCount());
        VxVector changed(0,0,10);source.SetVert(0,&changed);}
    Plane(target,1);VxVector changed(0,0,4);target.SetVert(0,&changed);target.BuildRenderMesh();
    bool sawCorner=false;for(int i=0;i<target.GetVertexCount();++i){VxVector p;target.GetVertexPosition(i,&p);if(p.x==0&&p.y==0){EXPECT_FLOAT_EQ(4,p.z);sawCorner=true;}}EXPECT_TRUE(sawCorner);
}

TEST_F(PatchMeshLifecycleTest, SelfCopyDoesNotRequireSavingOrReplaceBorrowedControlStorage) {
    SaveFailurePatch mesh(Context);Quad(mesh);VxVector *points=mesh.GetVerts();CKDependenciesContext dependencies(Context);
    EXPECT_EQ(CK_OK,mesh.Copy(mesh,dependencies));EXPECT_EQ(points,mesh.GetVerts());EXPECT_EQ(1,mesh.CallbackCount());Plane(mesh,1);
}

TEST_F(PatchMeshLifecycleTest, CopyReturnsSourceSaveFailure) {
    SaveFailurePatch source(Context);Quad(source);LifecyclePatch target(Context);CKDependenciesContext dependencies(Context);
    EXPECT_EQ(CKERR_OUTOFMEMORY,target.Copy(source,dependencies));
}

TEST_F(PatchMeshLifecycleTest, CopyReturnsTheActualVirtualLoadFailure) {
    LifecyclePatch source(Context);Quad(source);LoadFailurePatch target(Context);CKDependenciesContext dependencies(Context);
    EXPECT_EQ(CKERR_INVALIDPARAMETER,target.Copy(source,dependencies));EXPECT_EQ(1,target.Loads);EXPECT_TRUE(target.SawPatchWire);
}

TEST_F(PatchMeshLifecycleTest, MemoryQueryCountsEachAllocatedChannelPayloadInBytesOnce) {
    LifecyclePatch mesh(Context);const int empty=mesh.GetMemoryOccupation();mesh.SetTVCount(4,-1);
    EXPECT_EQ(32,mesh.GetMemoryOccupation()-empty);mesh.SetTVPatchCount(2,-1);EXPECT_EQ(48,mesh.GetMemoryOccupation()-empty);
    const int primary=mesh.GetMemoryOccupation();mesh.AllocateChannels(2);mesh.SetTVCount(2,0);
    EXPECT_EQ(static_cast<int>(sizeof(CKPatchChannel))+16,mesh.GetMemoryOccupation()-primary);
}

TEST_F(PatchMeshLifecycleTest, InheritedSceneWrappersKeepNativeMeshAndMaterialMembership) {
    RCKPatchMesh *implementation=static_cast<RCKPatchMesh *>(Context->CreateObject(CKCID_PATCHMESH));
    CKPatchMesh *mesh=reinterpret_cast<CKPatchMesh *>(implementation);
    CKScene *scene=static_cast<CKScene *>(Context->CreateObject(CKCID_SCENE));
    CKMaterial *material=static_cast<CKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    ASSERT_NE(nullptr,mesh);ASSERT_NE(nullptr,scene);ASSERT_NE(nullptr,material);
    Quad(*implementation);
    mesh->SetPatchMaterial(0,material);mesh->BuildRenderMesh();
    mesh->AddToScene(scene,TRUE);EXPECT_TRUE(mesh->IsInScene(scene));EXPECT_TRUE(material->IsInScene(scene));
    mesh->RemoveFromScene(scene,TRUE);EXPECT_FALSE(mesh->IsInScene(scene));EXPECT_FALSE(material->IsInScene(scene));
    mesh->AddToScene(nullptr,TRUE);mesh->RemoveFromScene(nullptr,TRUE);EXPECT_FALSE(mesh->IsInScene(scene));
}
}
