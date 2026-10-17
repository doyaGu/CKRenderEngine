#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "RCKSprite3D.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "TextureTestObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
using Clock = std::chrono::steady_clock;
float Milliseconds(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

struct TimedCall {
    int DelayMs = 0;
    int Calls = 0;
    float Elapsed = 0;
    float ObjectsAtEntry = 0, SpritesAtEntry = 0, SkinAtEntry = 0;
    RCK3dEntity *Bone = nullptr;
    float BoneX = 0;
    void Run(CKRenderContext *context) {
        RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
        ObjectsAtEntry = device->m_Stats.ObjectsCallbacksTime;
        SpritesAtEntry = device->m_Stats.SpriteCallbacksTime;
        SkinAtEntry = device->m_Stats.SkinTime;
        const auto start = Clock::now();
        ++Calls;
        if (Bone) Bone->SetPosition3f(BoneX, 0, 0);
        if (DelayMs) std::this_thread::sleep_for(std::chrono::milliseconds(DelayMs));
        Elapsed += Milliseconds(start);
    }
};
CKBOOL ObjectCall(CKRenderContext *context, CKRenderObject *, void *argument) {
    static_cast<TimedCall *>(argument)->Run(context);
    return FALSE; // The legacy pre/post consumers ignore this result.
}
void MeshCall(CKRenderContext *context, CK3dEntity *, CKMesh *, void *argument) {
    static_cast<TimedCall *>(argument)->Run(context);
}
void SubmeshCall(CKRenderContext *context, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    static_cast<TimedCall *>(argument)->Run(context);
}

class TimingBoundary : public TextureTestObserver {
public:
    int Draws = 0, LastCount = 0, LastVertices = 0, DrawDelayMs = 0;
    float DrawElapsed = 0;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int count, VxDrawPrimitiveData *data) override {
        const auto start = Clock::now();
        ++Draws; LastCount = count; LastVertices = data->VertexCount;
        EXPECT_NE(nullptr, data->PositionPtr);
        if (DrawDelayMs) std::this_thread::sleep_for(std::chrono::milliseconds(DrawDelayMs));
        DrawElapsed += Milliseconds(start);
        return TRUE;
    }
};

// The virtual boundary observes elapsed CPU work and delegates the entire
// deformation to production UpdateSkin. It injects no delay or replacement.
class MeasuredSkinEntity : public RCK3dEntity {
public:
    int Updates = 0;
    float UpdateElapsed = 0, LastUpdateElapsed = 0;
    CKBOOL UpdateResult = FALSE;
    explicit MeasuredSkinEntity(CKContext *context) : RCK3dEntity(context) {}
    CKBOOL UpdateSkin() override {
        const auto start = Clock::now();
        UpdateResult = RCK3dEntity::UpdateSkin();
        LastUpdateElapsed = Milliseconds(start);
        UpdateElapsed += LastUpdateElapsed; ++Updates;
        return UpdateResult;
    }
};
class SkinMeshBoundary : public RCKMesh {
public:
    int Draws = 0, DelayMs = 0;
    float DrawElapsed = 0;
    VxVector Sample;
    explicit SkinMeshBoundary(CKContext *context) : RCKMesh(context) {}
    CKERROR Render(CKRenderContext *, CK3dEntity *) override {
        const auto start = Clock::now();
        ++Draws; GetVertexPosition(1, &Sample);
        if (DelayMs) std::this_thread::sleep_for(std::chrono::milliseconds(DelayMs));
        DrawElapsed += Milliseconds(start);
        return CK_OK;
    }
};

class RenderContextCallbackTimingTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    MeasuredSkinEntity *SkinEntity = nullptr;
    SkinMeshBoundary *SkinMesh = nullptr;
    TimingBoundary Boundary;
    XClassArray<CKRasterizerInfo> SavedRasterizers;
    static constexpr float ObjectsSeed = 101, SpritesSeed = 203, SkinSeed = 307;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        SavedRasterizers = g_RasterizersInfo; g_RasterizersInfo.Clear();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &Boundary;
        Device->m_Settings.m_Rect = {0,0,200,200};
        Device->SetViewRect(VxRect(0,0,200,200));
        Device->SetWorldTransformationMatrix(VxMatrix::Identity());
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        VxMatrix projection; projection.Perspective(PI/2,1,1,20);
        Device->SetProjectionTransformationMatrix(projection);
        Device->m_Flags = 0;
        Device->m_Stats.ObjectsCallbacksTime = ObjectsSeed;
        Device->m_Stats.SpriteCallbacksTime = SpritesSeed;
        Device->m_Stats.SkinTime = SkinSeed;
        Context->m_BehaviorContext.CurrentRenderContext = Device;
    }
    void TearDown() override {
        // Release manager borrows before deleting the fixture-owned skin probes.
        if (Manager) Manager->DetachAllObjects();
        delete SkinEntity; delete SkinMesh;
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        g_RasterizersInfo = SavedRasterizers; CKShutdown();
    }
    static CK3dEntity *Api(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    RCK3dEntity *Entity() {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        if (entity) { entity->SetPosition3f(0,0,5); entity->m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE; }
        return entity;
    }
    RCKMesh *Mesh() {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        if (mesh) Fill(mesh,3);
        return mesh;
    }
    static void Fill(RCKMesh *mesh, int count) {
        ASSERT_TRUE(mesh->SetVertexCount(count)); ASSERT_TRUE(mesh->SetFaceCount(1));
        const VxVector normal(0,0,-1);
        for (int i=0; i<count; ++i) {
            VxVector point(float(i%3)-1, float((i+1)%3)-1, 0), n=normal;
            mesh->SetVertexPosition(i,&point); mesh->SetVertexNormal(i,&n);
        }
        mesh->SetFaceVertexIndex(0,0,1,2); mesh->BuildFaceNormals(); mesh->VertexMove();
    }
    static void Interval(float delta, float callbackElapsed, float wall, float excluded=0) {
        EXPECT_GE(delta,callbackElapsed-.1f);
        EXPECT_LE(delta,wall-excluded+1.0f);
        EXPECT_GT(delta,0);
    }
    void OtherCategoriesUnchanged(CKBOOL objectTimer) {
        if (objectTimer) EXPECT_FLOAT_EQ(SpritesSeed,Device->m_Stats.SpriteCallbacksTime);
        else EXPECT_FLOAT_EQ(ObjectsSeed,Device->m_Stats.ObjectsCallbacksTime);
        EXPECT_FLOAT_EQ(SkinSeed,Device->m_Stats.SkinTime);
    }
    void PrepareSkin(CKBOOL progressive, int count=4096) {
        SkinEntity = new MeasuredSkinEntity(Context); SkinMesh = new SkinMeshBoundary(Context);
        Fill(SkinMesh,count);
        if (progressive) { ASSERT_EQ(CK_OK,SkinMesh->CreatePM()); ASSERT_TRUE(SkinMesh->IsPM()); }
        SkinEntity->SetCurrentMesh(SkinMesh);
        CKSkin *skin = SkinEntity->CreateSkin(); ASSERT_NE(nullptr,skin);
        skin->SetObjectInitMatrix(VxMatrix::Identity()); skin->SetBoneCount(1); skin->SetVertexCount(count);
        RCK3dEntity *bone = Entity(); ASSERT_NE(nullptr,bone); bone->SetPosition3f(3,0,0);
        skin->GetBoneData(0)->SetBone(Api(bone)); skin->GetBoneData(0)->SetBoneInitialInverseMatrix(VxMatrix::Identity());
        for (int i=0; i<count; ++i) {
            VxVector point; SkinMesh->GetVertexPosition(i,&point);
            CKSkinVertexData *vertex = skin->GetVertexData(i);
            vertex->SetInitialPos(point); vertex->SetBoneCount(1); vertex->SetBone(0,0); vertex->SetWeight(0,1);
        }
        SkinEntity->SetPosition3f(0,0,0);
        SkinEntity->m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE;
    }
};

TEST_F(RenderContextCallbackTimingTest, EntityPreAndPostAccumulateWhileTheMainCallbackRemainsOutsideTheirTimer) {
    RCK3dEntity *entity=Entity(); ASSERT_NE(nullptr,entity);
    TimedCall pre,main,post; pre.DelayMs=9; main.DelayMs=27; post.DelayMs=13;
    ASSERT_TRUE(entity->AddPreRenderCallBack(ObjectCall,&pre,FALSE));
    entity->SetRenderCallBack(ObjectCall,&main);
    ASSERT_TRUE(entity->AddPostRenderCallBack(ObjectCall,&post,FALSE));
    const auto start=Clock::now(); ASSERT_TRUE(entity->Render(Device,0)); const float wall=Milliseconds(start);
    EXPECT_EQ(1,pre.Calls); EXPECT_EQ(1,main.Calls); EXPECT_EQ(1,post.Calls);
    EXPECT_FLOAT_EQ(ObjectsSeed,pre.ObjectsAtEntry);
    EXPECT_GE(main.ObjectsAtEntry-ObjectsSeed,pre.Elapsed-.1f);
    EXPECT_FLOAT_EQ(main.ObjectsAtEntry,post.ObjectsAtEntry);
    Interval(Device->m_Stats.ObjectsCallbacksTime-ObjectsSeed,pre.Elapsed+post.Elapsed,wall,main.Elapsed);
    OtherCategoriesUnchanged(TRUE);
}

TEST_F(RenderContextCallbackTimingTest, MeshPreAndPostAccumulateWhileTheMainCallbackRemainsOutsideTheirTimer) {
    RCKMesh *mesh=Mesh(); RCK3dEntity *entity=Entity(); ASSERT_NE(nullptr,mesh); ASSERT_NE(nullptr,entity);
    TimedCall pre,main,post; pre.DelayMs=8; main.DelayMs=29; post.DelayMs=14;
    ASSERT_TRUE(mesh->AddPreRenderCallBack(MeshCall,&pre,FALSE)); mesh->SetRenderCallBack(MeshCall,&main);
    ASSERT_TRUE(mesh->AddPostRenderCallBack(MeshCall,&post,FALSE));
    Device->m_Current3dEntity=entity;
    const auto start=Clock::now(); ASSERT_EQ(CK_OK,mesh->Render(Device,Api(entity))); const float wall=Milliseconds(start);
    Device->m_Current3dEntity=nullptr;
    EXPECT_EQ(1,pre.Calls); EXPECT_EQ(1,main.Calls); EXPECT_EQ(1,post.Calls);
    EXPECT_FLOAT_EQ(ObjectsSeed,pre.ObjectsAtEntry);
    EXPECT_GE(main.ObjectsAtEntry-ObjectsSeed,pre.Elapsed-.1f); EXPECT_FLOAT_EQ(main.ObjectsAtEntry,post.ObjectsAtEntry);
    Interval(Device->m_Stats.ObjectsCallbacksTime-ObjectsSeed,pre.Elapsed+post.Elapsed,wall,main.Elapsed);
    OtherCategoriesUnchanged(TRUE);
}

TEST_F(RenderContextCallbackTimingTest, SubmeshCallbacksExcludeTheActualSubmittedDrawFromCallbackTime) {
    RCKMesh *mesh=Mesh(); ASSERT_NE(nullptr,mesh);
    RCKMaterial *material=static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr,material);
    TimedCall pre,post; pre.DelayMs=8; post.DelayMs=14;
    ASSERT_TRUE(mesh->AddSubMeshPreRenderCallBack(SubmeshCall,&pre,FALSE));
    ASSERT_TRUE(mesh->AddSubMeshPostRenderCallBack(SubmeshCall,&post,FALSE));
    CKMaterialGroup group; group.m_Material=material; group.m_VertexCount=3; group.m_Primitives.Resize(1);
    group.m_Primitives[0].m_Type=VX_TRIANGLELIST; group.m_Primitives[0].m_IndexBufferOffset=-1;
    group.m_Primitives[0].m_Indices.PushBack(0); group.m_Primitives[0].m_Indices.PushBack(1); group.m_Primitives[0].m_Indices.PushBack(2);
    VxVector positions[3]={VxVector(-1,-1,5),VxVector(0,1,5),VxVector(1,-1,5)};
    CKDWORD colors[3]={0xffffffffu,0xffffffffu,0xffffffffu};
    VxDrawPrimitiveData data={}; data.Flags=static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM|CKRST_DP_DIFFUSE);
    data.VertexCount=3; data.PositionPtr=positions; data.PositionStride=sizeof(VxVector);
    data.ColorPtr=colors; data.ColorStride=sizeof(CKDWORD);
    Boundary.DrawDelayMs=31;
    const auto start=Clock::now(); ASSERT_EQ(1,mesh->RenderGroup(Device,&group,nullptr,&data)); const float wall=Milliseconds(start);
    EXPECT_EQ(1,pre.Calls); EXPECT_EQ(1,post.Calls); ASSERT_EQ(1,Boundary.Draws);
    EXPECT_EQ(3,Boundary.LastCount); EXPECT_EQ(3,Boundary.LastVertices);
    Interval(Device->m_Stats.ObjectsCallbacksTime-ObjectsSeed,pre.Elapsed+post.Elapsed,wall,Boundary.DrawElapsed);
    OtherCategoriesUnchanged(TRUE);
}

TEST_F(RenderContextCallbackTimingTest, Sprite3dCallbacksAccumulateWhileQueueingTheRealMaterialBatch) {
    RCKSprite3D *sprite=static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    RCKMaterial *material=static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    ASSERT_NE(nullptr,sprite); ASSERT_NE(nullptr,material);
    sprite->SetMaterial(material); sprite->SetMode(VXSPRITE3D_ORIENTABLE); sprite->SetPosition3f(0,0,5);
    sprite->m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE;
    TimedCall pre,post; pre.DelayMs=9; post.DelayMs=15;
    ASSERT_TRUE(sprite->AddPreRenderCallBack(ObjectCall,&pre,FALSE));
    ASSERT_TRUE(sprite->AddPostRenderCallBack(ObjectCall,&post,FALSE));
    const auto start=Clock::now(); ASSERT_TRUE(sprite->Render(Device,0)); const float wall=Milliseconds(start);
    EXPECT_EQ(1,pre.Calls); EXPECT_EQ(1,post.Calls); ASSERT_EQ(1,Device->m_Sprite3DBatches.Size());
    ASSERT_NE(nullptr,material->GetSprite3DBatch()); EXPECT_EQ(4,material->GetSprite3DBatch()->m_Vertices.Size());
    EXPECT_EQ(0,Boundary.Draws);
    Interval(Device->m_Stats.ObjectsCallbacksTime-ObjectsSeed,pre.Elapsed+post.Elapsed,wall);
    OtherCategoriesUnchanged(TRUE);
}

TEST_F(RenderContextCallbackTimingTest, TwoDimensionalCallbacksAccumulateAcrossDrawsAndExcludeActualGeometryTime) {
    RCK2dEntity *entity=static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
    RCKMaterial *material=static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    ASSERT_NE(nullptr,entity); ASSERT_NE(nullptr,material);
    entity->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ,0); entity->EnableRatioOffset(FALSE);
    entity->SetRect(VxRect(10,20,80,70)); entity->SetMaterial(material);
    TimedCall pre,post; pre.DelayMs=10; post.DelayMs=16;
    ASSERT_TRUE(entity->AddPreRenderCallBack(ObjectCall,&pre,FALSE));
    ASSERT_TRUE(entity->AddPostRenderCallBack(ObjectCall,&post,FALSE));
    Boundary.DrawDelayMs=33;
    const auto start=Clock::now();
    ASSERT_EQ(CK_OK,entity->Render(Device)); ASSERT_EQ(CK_OK,entity->Render(Device));
    const float wall=Milliseconds(start);
    EXPECT_EQ(2,pre.Calls); EXPECT_EQ(2,post.Calls); ASSERT_EQ(2,Boundary.Draws); EXPECT_EQ(4,Boundary.LastVertices);
    Interval(Device->m_Stats.SpriteCallbacksTime-SpritesSeed,pre.Elapsed+post.Elapsed,wall,Boundary.DrawElapsed);
    OtherCategoriesUnchanged(FALSE);
}

TEST_F(RenderContextCallbackTimingTest, NonProgressiveSkinMeasuresRealDeformationBeforeObjectCallbacks) {
    PrepareSkin(FALSE); ASSERT_NE(nullptr,SkinEntity); ASSERT_NE(nullptr,SkinMesh);
    CKSkin *skin=SkinEntity->GetSkin(); RCK3dEntity *bone=reinterpret_cast<RCK3dEntity *>(skin->GetBoneData(0)->GetBone());
    VxVector initial; SkinMesh->GetVertexPosition(1,&initial);
    TimedCall pre,post; pre.DelayMs=18; post.DelayMs=12; pre.Bone=bone; pre.BoneX=7;
    ASSERT_TRUE(SkinEntity->AddPreRenderCallBack(ObjectCall,&pre,FALSE));
    ASSERT_TRUE(SkinEntity->AddPostRenderCallBack(ObjectCall,&post,FALSE)); SkinMesh->DelayMs=25;
    const auto start=Clock::now(); ASSERT_TRUE(SkinEntity->Render(Device,0)); const float wall=Milliseconds(start);
    EXPECT_EQ(1,SkinEntity->Updates); EXPECT_TRUE(SkinEntity->UpdateResult); EXPECT_EQ(1,SkinMesh->Draws);
    EXPECT_NEAR(initial.x+3,SkinMesh->Sample.x,1e-4f); EXPECT_NEAR(7,bone->GetWorldMatrix()[3].x,1e-4f);
    EXPECT_GT(pre.SkinAtEntry,SkinSeed);
    Interval(Device->m_Stats.SkinTime-SkinSeed,SkinEntity->UpdateElapsed,wall,pre.Elapsed+post.Elapsed+SkinMesh->DrawElapsed);
    Interval(Device->m_Stats.ObjectsCallbacksTime-ObjectsSeed,pre.Elapsed+post.Elapsed,wall,SkinEntity->UpdateElapsed+SkinMesh->DrawElapsed);
    EXPECT_FLOAT_EQ(SpritesSeed,Device->m_Stats.SpriteCallbacksTime);
}

TEST_F(RenderContextCallbackTimingTest, ProgressiveSkinMeasuresRealDeformationAfterThePreCallback) {
    PrepareSkin(TRUE,3); ASSERT_NE(nullptr,SkinEntity); ASSERT_NE(nullptr,SkinMesh);
    CKSkin *skin=SkinEntity->GetSkin(); RCK3dEntity *bone=reinterpret_cast<RCK3dEntity *>(skin->GetBoneData(0)->GetBone());
    VxVector initial; SkinMesh->GetVertexPosition(1,&initial);
    TimedCall pre,post; pre.DelayMs=19; post.DelayMs=11; pre.Bone=bone; pre.BoneX=7;
    ASSERT_TRUE(SkinEntity->AddPreRenderCallBack(ObjectCall,&pre,FALSE));
    ASSERT_TRUE(SkinEntity->AddPostRenderCallBack(ObjectCall,&post,FALSE)); SkinMesh->DelayMs=25;
    const auto start=Clock::now(); ASSERT_TRUE(SkinEntity->Render(Device,0)); const float wall=Milliseconds(start);
    EXPECT_EQ(1,SkinEntity->Updates); EXPECT_TRUE(SkinEntity->UpdateResult); EXPECT_EQ(1,SkinMesh->Draws);
    EXPECT_FLOAT_EQ(SkinSeed,pre.SkinAtEntry); EXPECT_GT(post.SkinAtEntry,SkinSeed);
    EXPECT_NEAR(initial.x+7,SkinMesh->Sample.x,1e-4f);
    Interval(Device->m_Stats.SkinTime-SkinSeed,SkinEntity->UpdateElapsed,wall,pre.Elapsed+post.Elapsed+SkinMesh->DrawElapsed);
    Interval(Device->m_Stats.ObjectsCallbacksTime-ObjectsSeed,pre.Elapsed+post.Elapsed,wall,SkinEntity->UpdateElapsed+SkinMesh->DrawElapsed);
    EXPECT_FLOAT_EQ(SpritesSeed,Device->m_Stats.SpriteCallbacksTime);
}

TEST_F(RenderContextCallbackTimingTest, RepeatedActualSkinUpdatesAccumulateWithoutInventingCallbackTime) {
    PrepareSkin(FALSE,32768); ASSERT_NE(nullptr,SkinEntity); ASSERT_NE(nullptr,SkinMesh);
    CKSkin *skin=SkinEntity->GetSkin(); RCK3dEntity *bone=reinterpret_cast<RCK3dEntity *>(skin->GetBoneData(0)->GetBone());
    VxVector initial; SkinMesh->GetVertexPosition(1,&initial);
    const auto start=Clock::now(); ASSERT_TRUE(SkinEntity->Render(Device,0));
    const float first=Device->m_Stats.SkinTime;
    EXPECT_NEAR(initial.x+3,SkinMesh->Sample.x,1e-4f);
    bone->SetPosition3f(8,0,0); SkinEntity->m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE;
    ASSERT_TRUE(SkinEntity->Render(Device,0)); const float wall=Milliseconds(start);
    EXPECT_EQ(2,SkinEntity->Updates); EXPECT_TRUE(SkinEntity->UpdateResult); EXPECT_EQ(2,SkinMesh->Draws);
    EXPECT_NEAR(initial.x+8,SkinMesh->Sample.x,1e-4f); EXPECT_GT(Device->m_Stats.SkinTime,first);
    Interval(Device->m_Stats.SkinTime-SkinSeed,SkinEntity->UpdateElapsed,wall);
    EXPECT_FLOAT_EQ(ObjectsSeed,Device->m_Stats.ObjectsCallbacksTime);
    EXPECT_FLOAT_EQ(SpritesSeed,Device->m_Stats.SpriteCallbacksTime);
}
} // namespace

