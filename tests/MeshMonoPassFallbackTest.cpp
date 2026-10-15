#include <gtest/gtest.h>
#include <cstring>
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"
#include "MeshHardwareObserver.h"

namespace {
class MonoFallbackObserver : public TextureTestObserver {
public:
    struct Draw {
        CKBOOL Hardware = FALSE;
        int Count = 0;
        CKDWORD Textures[4] = {};
        VxVector Positions[12];
        Vx2DVector UVs[12];
    };
    Draw Draws[32]; int DrawCount = 0;
    CKDWORD TextureLabels[64] = {};
    MeshHardwareObserver Buffers;
    int BlendCalls = 0, RejectBlendCall = -1, RejectedBlends = 0;
    int RejectBindStage = -1, RejectSamplerStage = -1, RejectCoordinatesStage = -1;
    float RejectBaseDiffuse = -1.0f;
    CKBOOL Once = TRUE, CreateOnce = FALSE, UploadOnce = FALSE;
    int FailedCreates = 0, FailedUploads = 0, FailedBinds = 0, FailedSamplers = 0, FailedCoordinates = 0;
    CKBOOL RejectFallbackBind = FALSE;
    int FailedFallbackBinds = 0;
    CKBOOL MatchesBase() const {
        // DefaultRender first prepares a material while selecting channels.
        // Group-specific failures target actual group binds after that setup.
        return RejectBaseDiffuse < 0 || (MaterialWrites > 1 && Material.Diffuse.r == RejectBaseDiffuse);
    }
    CKBOOL CreateTexture(const CKTextureDesc *desc, CKDWORD *handle) override {
        if (CreateOnce && !FailedCreates) { ++FailedCreates; return FALSE; }
        return TextureTestObserver::CreateTexture(desc,handle);
    }
    CKBOOL LoadTexture(CKDWORD handle,const VxImageDescEx &image,int level,CKRST_CUBEFACE face,const CKRECT *region) override {
        if (UploadOnce && !FailedUploads) { ++FailedUploads; return FALSE; }
        if (handle < 64 && image.Image) std::memcpy(&TextureLabels[handle],image.Image,4);
        return TextureTestObserver::LoadTexture(handle,image,level,face,region);
    }
    CKBOOL SetTexture(CKDWORD handle,int stage) override {
        if (handle && stage == 0 && RejectFallbackBind) { ++FailedFallbackBinds; return FALSE; }
        if (handle && stage == RejectBindStage && MatchesBase() && (!Once || !FailedBinds)) { ++FailedBinds; return FALSE; }
        return TextureTestObserver::SetTexture(handle,stage);
    }
    CKBOOL SetTextureStageState(int stage,CKRST_TEXTURESTAGESTATETYPE state,CKDWORD value) override {
        if (state == CKRST_TSS_STAGEBLEND) {
            ++BlendCalls;
            if (BlendCalls == RejectBlendCall) { ++RejectedBlends; return FALSE; }
        }
        if (stage == RejectSamplerStage && state == CKRST_TSS_MINFILTER && MatchesBase() && (!Once || !FailedSamplers)) {
            ++FailedSamplers; return FALSE;
        }
        if (stage == RejectCoordinatesStage && state == CKRST_TSS_TEXCOORDINDEX && MatchesBase() && (!Once || !FailedCoordinates)) {
            ++FailedCoordinates; return FALSE;
        }
        return MaterialRenderObserver::SetTextureStageState(stage,state,value);
    }
    Draw *Begin(CKBOOL hardware,int count) {
        EXPECT_LT(DrawCount,32); if (DrawCount >= 32) return nullptr;
        Draw *draw = &Draws[DrawCount++]; draw->Hardware = hardware; draw->Count = count;
        EXPECT_LE(count,12);
        for (int i = 0; i < 4; ++i) draw->Textures[i] = Textures[i] < 64 ? TextureLabels[Textures[i]] : 0;
        return draw;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type,CKWORD *indices,int count,VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST,type); EXPECT_NE(nullptr,data); if (!data) return FALSE;
        Draw *draw = Begin(FALSE,count); if (!draw) return FALSE;
        for (int i = 0; i < count && i < 12; ++i) {
            EXPECT_LT(indices[i],data->VertexCount);
            draw->Positions[i] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + data->PositionStride*indices[i]);
            if (data->TexCoordPtr) draw->UVs[i] = *reinterpret_cast<Vx2DVector *>(static_cast<CKBYTE *>(data->TexCoordPtr) + data->TexCoordStride*indices[i]);
        }
        return TRUE;
    }
    CKBOOL HardwareDraw(CKDWORD first,CKWORD *indices,int count) {
        Draw *draw = Begin(TRUE,count); if (!draw) return FALSE;
        for (int i = 0; i < count && i < 12; ++i) {
            const CKDWORD vertex = first + indices[i]; EXPECT_LT(vertex,Buffers.VertexDesc.m_CurrentVCount);
            std::memcpy(&draw->Positions[i],Buffers.VertexBytes + vertex*Buffers.VertexDesc.m_VertexSize,sizeof(VxVector));
        }
        return TRUE;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type,CKDWORD vb,CKDWORD start,CKDWORD count,CKWORD *indices,int indexCount) override {
        EXPECT_EQ(VX_TRIANGLELIST,type); EXPECT_EQ(Buffers.VertexHandle,vb); return HardwareDraw(start,indices,indexCount);
    }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE type,CKDWORD vb,CKDWORD ib,CKDWORD minimum,CKDWORD count,CKDWORD start,int indexCount) override {
        EXPECT_EQ(VX_TRIANGLELIST,type); EXPECT_EQ(Buffers.VertexHandle,vb); EXPECT_EQ(Buffers.IndexHandle,ib);
        return HardwareDraw(0,Buffers.IndexWords+start,indexCount);
    }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *d,const void *p,CKDWORD *h) override { return Buffers.CreateVertexBuffer(d,p,h); }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *d,const void *p,CKDWORD *h) override { return Buffers.CreateIndexBuffer(d,p,h); }
    CKBOOL GetVertexBufferDesc(CKDWORD h,CKVertexBufferDesc *d) const override { return Buffers.GetVertexBufferDesc(h,d); }
    CKBOOL GetIndexBufferDesc(CKDWORD h,CKIndexBufferDesc *d) const override { return Buffers.GetIndexBufferDesc(h,d); }
    void *LockVertexBuffer(CKDWORD h,CKDWORD s,CKDWORD c,CKRST_LOCKFLAGS f) override { return Buffers.LockVertexBuffer(h,s,c,f); }
    void *LockIndexBuffer(CKDWORD h,CKDWORD s,CKDWORD c,CKRST_LOCKFLAGS f) override { return Buffers.LockIndexBuffer(h,s,c,f); }
    CKBOOL UnlockVertexBuffer(CKDWORD h) override { return Buffers.UnlockVertexBuffer(h); }
    CKBOOL UnlockIndexBuffer(CKDWORD h) override { return Buffers.UnlockIndexBuffer(h); }
    CKBOOL DeleteObject(CKRST_HANDLE h,CKRST_OBJECTTYPE t) override {
        return t == CKRST_OBJ_TEXTURE ? TextureTestObserver::DeleteObject(h,t) : Buffers.DeleteObject(h,t);
    }
};
class MonoFallbackMesh : public RCKMesh {
public:
    explicit MonoFallbackMesh(CKContext *context) : RCKMesh(context) {}
    void Cold() { m_Valid = 0; }
    void Hot() { m_Valid = 3; }
};
struct CallbackObservation { MonoFallbackObserver *State; int Calls = 0, DrawsAtLastCall = 0; };
void ObserveSubMeshPost(CKRenderContext *,CK3dEntity *,CKMesh *,CKMaterial *,void *argument) {
    CallbackObservation *record = static_cast<CallbackObservation *>(argument);
    ++record->Calls; record->DrawsAtLastCall = record->State->DrawCount;
}
int ObserveChannelMaterial(CKRenderContext *,CKMaterial *,void *argument) {
    int *calls = static_cast<int *>(argument); ++*calls;
    return FALSE; // Continue the material's ordinary texture/effect path.
}
class MeshMonoPassFallbackTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver; MonoFallbackObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK,CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh,CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial,CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKTexture,CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK,CKCreateContext(&Context,nullptr,0,0)); Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1](); Manager->m_VertexCache.Value = 0;
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888,format); Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State,0));
        Device = new RCKRenderContext(Context); Device->DetachAll(); Device->m_RasterizerContext = &State;
        Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0; Device->m_Current3dEntity = nullptr;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Geometry(MonoFallbackMesh &mesh,RCKMaterial &first,RCKMaterial *second = nullptr) {
        const int vertices = second ? 6 : 3;
        mesh.SetVertexCount(vertices); mesh.SetFaceCount(second ? 2 : 1);
        for (int i = 0; i < vertices; ++i) {
            const int group = i/3, corner = i%3;
            VxVector position(group*10.0f+(corner == 1 ? 2.0f : 0.0f),corner == 2 ? 2.0f : 0.0f,0),normal(0,0,1);
            mesh.SetVertexPosition(i,&position); mesh.SetVertexNormal(i,&normal);
            mesh.SetVertexTextureCoordinates(i,corner == 0 ? .9f : .1f,.25f,-1);
        }
        first.SetDiffuse(VxColor(.25f,1.0f,1.0f,1.0f)); mesh.SetFaceVertexIndex(0,0,1,2); mesh.SetFaceMaterial(0,&first);
        if (second) { second->SetDiffuse(VxColor(.5f,1.0f,1.0f,1.0f)); mesh.SetFaceVertexIndex(1,3,4,5); mesh.SetFaceMaterial(1,second); }
        mesh.SetLitMode(VX_LITMESH); mesh.SetFlags((mesh.GetFlags() | VXMESH_RENDERCHANNELS) & ~VXMESH_STRIPIFY);
    }
    int Channel(MonoFallbackMesh &mesh,RCKMaterial &material,RCKTexture &texture,CKDWORD label) {
        EXPECT_TRUE(texture.CreateImage(1,1,32,0)); CKBYTE *pixels = texture.LockSurfacePtr(0); EXPECT_NE(nullptr,pixels);
        if (pixels) std::memcpy(pixels,&label,4); texture.ReleaseSurfacePtr(0); material.SetTexture(0,&texture);
        const int channel = mesh.AddChannel(&material,FALSE);
        mesh.SetChannelSourceBlend(channel,VXBLEND_DESTCOLOR); mesh.SetChannelDestBlend(channel,VXBLEND_ZERO);
        mesh.SetChannelFlags(channel,VXCHANNEL_ACTIVE);
        for (int i = 0; i < mesh.GetVertexCount(); ++i) mesh.SetVertexTextureCoordinates(i,.375f+i*.0625f,.75f,channel);
        return channel;
    }
    void Draw(MonoFallbackMesh &mesh,CKBOOL hardware = FALSE) {
        ASSERT_EQ(1,mesh.CreateRenderGroups()); if (hardware) mesh.Hot(); else mesh.Cold();
        ASSERT_EQ(1,mesh.DefaultRender(Device,nullptr));
    }
    void Fallback(int draw,CKDWORD label,float groupX) {
        ASSERT_LT(draw,State.DrawCount); EXPECT_FALSE(State.Draws[draw].Hardware);
        EXPECT_EQ(label,State.Draws[draw].Textures[0]);
        for (int stage = 1; stage < 4; ++stage) EXPECT_EQ(0u,State.Draws[draw].Textures[stage]);
        EXPECT_FLOAT_EQ(groupX,State.Draws[draw].Positions[0].x); EXPECT_FLOAT_EQ(groupX+2,State.Draws[draw].Positions[1].x);
    }
};
const CKDWORD A = 0xff001001u,B = 0xff002002u,C = 0xff003003u;
}

TEST_F(MeshMonoPassFallbackTest, SuccessfulSelectedChannelDrawsExactlyOnceInBaseStage) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    Draw(mesh); ASSERT_EQ(1,State.DrawCount); EXPECT_EQ(A,State.Draws[0].Textures[1]);
}
TEST_F(MeshMonoPassFallbackTest, RejectedStageBlendIsNotMarkedMonoAndUsesMultipass) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); int index = Channel(mesh,channel,texture,A);
    State.RejectBlendCall = 1; Draw(mesh); EXPECT_EQ(1,State.RejectedBlends); EXPECT_FALSE(mesh.GetChannelFlags(index) & VXCHANNEL_MONO);
    ASSERT_EQ(2,State.DrawCount); EXPECT_EQ(0u,State.Draws[0].Textures[1]); Fallback(1,A,0);
}
TEST_F(MeshMonoPassFallbackTest, TextureCreateFailureRetriesSelectedChannelAsGroupMultipass) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    State.CreateOnce = TRUE; Draw(mesh); EXPECT_EQ(1,State.FailedCreates); ASSERT_EQ(2,State.DrawCount); Fallback(1,A,0);
}
TEST_F(MeshMonoPassFallbackTest, TextureUploadFailureRetriesWithoutLosingSelectedChannel) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    State.UploadOnce = TRUE; Draw(mesh); EXPECT_EQ(1,State.FailedUploads); ASSERT_EQ(2,State.DrawCount); Fallback(1,A,0);
}
TEST_F(MeshMonoPassFallbackTest, TextureBindingFailureUsesSoftwareGroupPass) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    State.RejectBindStage = 1; Draw(mesh); EXPECT_EQ(1,State.FailedBinds); ASSERT_EQ(2,State.DrawCount); Fallback(1,A,0);
}
TEST_F(MeshMonoPassFallbackTest, RejectedSamplerAcknowledgementUsesGroupMultipass) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    State.RejectSamplerStage = 1; Draw(mesh); EXPECT_EQ(1,State.FailedSamplers); ASSERT_EQ(2,State.DrawCount); Fallback(1,A,0);
}
TEST_F(MeshMonoPassFallbackTest, RejectedCoordinateStateUsesGroupMultipass) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    State.RejectCoordinatesStage = 1; Draw(mesh); EXPECT_EQ(1,State.FailedCoordinates); ASSERT_EQ(2,State.DrawCount); Fallback(1,A,0);
}
TEST_F(MeshMonoPassFallbackTest, SecondStageFailureRetainsSuccessfulFirstStageWithoutApplyingItTwice) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),first(Context),second(Context); RCKTexture a(Context),b(Context); Geometry(mesh,base);
    Channel(mesh,first,a,A); Channel(mesh,second,b,B); State.RejectBindStage = 2; Draw(mesh);
    ASSERT_EQ(2,State.DrawCount); EXPECT_EQ(A,State.Draws[0].Textures[1]); EXPECT_EQ(0u,State.Draws[0].Textures[2]); Fallback(1,B,0);
}
TEST_F(MeshMonoPassFallbackTest, FailedMiddleStageDefersOnlyItsSuffixInAuthoredChannelOrder) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),first(Context),second(Context),third(Context); RCKTexture a(Context),b(Context),c(Context); Geometry(mesh,base);
    Channel(mesh,first,a,A); Channel(mesh,second,b,B); Channel(mesh,third,c,C); State.RejectBindStage = 2; Draw(mesh);
    ASSERT_EQ(3,State.DrawCount); EXPECT_EQ(A,State.Draws[0].Textures[1]); Fallback(1,B,0); Fallback(2,C,0);
}
TEST_F(MeshMonoPassFallbackTest, FailureInSecondMaterialGroupDoesNotRedrawSuccessfulFirstGroup) {
    MonoFallbackMesh mesh(Context); RCKMaterial first(Context),second(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,first,&second); Channel(mesh,channel,texture,A);
    State.RejectBindStage = 1; State.RejectBaseDiffuse = .5f; Draw(mesh);
    ASSERT_EQ(3,State.DrawCount); EXPECT_EQ(A,State.Draws[0].Textures[1]); EXPECT_FLOAT_EQ(0,State.Draws[0].Positions[0].x);
    EXPECT_EQ(0u,State.Draws[1].Textures[1]); EXPECT_FLOAT_EQ(10,State.Draws[1].Positions[0].x); Fallback(2,A,10);
}
TEST_F(MeshMonoPassFallbackTest, FailureInFirstGroupDoesNotPreventSecondGroupMonoPass) {
    MonoFallbackMesh mesh(Context); RCKMaterial first(Context),second(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,first,&second); Channel(mesh,channel,texture,A);
    State.RejectBindStage = 1; State.RejectBaseDiffuse = .25f; Draw(mesh);
    ASSERT_EQ(3,State.DrawCount); Fallback(1,A,0); EXPECT_FLOAT_EQ(10,State.Draws[2].Positions[0].x); EXPECT_EQ(A,State.Draws[2].Textures[1]);
}
TEST_F(MeshMonoPassFallbackTest, FallbackUsesRemappedPrivateChannelUVForFailedGroup) {
    MonoFallbackMesh mesh(Context); RCKMaterial first(Context),second(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,first,&second); Channel(mesh,channel,texture,A);
    State.RejectBindStage = 1; State.RejectBaseDiffuse = .5f; Draw(mesh); ASSERT_EQ(3,State.DrawCount);
    EXPECT_FLOAT_EQ(.5625f,State.Draws[2].UVs[0].x); EXPECT_FLOAT_EQ(.625f,State.Draws[2].UVs[1].x); EXPECT_FLOAT_EQ(.75f,State.Draws[2].UVs[0].y);
}
TEST_F(MeshMonoPassFallbackTest, GroupPostCallbackObservesCompletedFallbackWithoutBeingInvokedTwice) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    CallbackObservation observation = {&State}; mesh.AddSubMeshPostRenderCallBack(ObserveSubMeshPost,&observation,FALSE);
    State.RejectBindStage = 1; Draw(mesh); EXPECT_EQ(1,observation.Calls); EXPECT_EQ(2,observation.DrawsAtLastCall);
}
TEST_F(MeshMonoPassFallbackTest, RetryNextFrameReturnsToSingleMonoPass) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    State.RejectBindStage = 1; Draw(mesh); ASSERT_EQ(2,State.DrawCount); State.DrawCount = 0; mesh.Cold();
    ASSERT_EQ(1,mesh.DefaultRender(Device,nullptr)); ASSERT_EQ(1,State.DrawCount); EXPECT_EQ(A,State.Draws[0].Textures[1]);
}
TEST_F(MeshMonoPassFallbackTest, HardwareBaseDrawFallsBackToSoftwareOnlyForMissingChannel) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    State.RejectBindStage = 1; Draw(mesh,TRUE); ASSERT_EQ(2,State.DrawCount); EXPECT_TRUE(State.Draws[0].Hardware); Fallback(1,A,0);
}
TEST_F(MeshMonoPassFallbackTest, HardwareRemappedGroupFallbackUsesOriginalGroupGeometry) {
    MonoFallbackMesh mesh(Context); RCKMaterial first(Context),second(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,first,&second); Channel(mesh,channel,texture,A);
    State.RejectBindStage = 1; State.RejectBaseDiffuse = .5f; Draw(mesh,TRUE); ASSERT_EQ(3,State.DrawCount);
    EXPECT_TRUE(State.Draws[0].Hardware); EXPECT_TRUE(State.Draws[1].Hardware); Fallback(2,A,10);
}
TEST_F(MeshMonoPassFallbackTest, WrapAwareHardwareBaseRetainsCorrectSoftwareFallbackCoordinates) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    mesh.SetWrapMode(VXTEXTUREWRAP_U); State.RejectBindStage = 1; Draw(mesh,TRUE);
    ASSERT_EQ(2,State.DrawCount); EXPECT_TRUE(State.Draws[0].Hardware); Fallback(1,A,0);
    EXPECT_FLOAT_EQ(.375f,State.Draws[1].UVs[0].x); EXPECT_FLOAT_EQ(.4375f,State.Draws[1].UVs[1].x);
}
TEST_F(MeshMonoPassFallbackTest, SelectedMonoChannelRunsMaterialCallbackOnceBeforeItsActualDraw) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context),channel(Context); RCKTexture texture(Context); Geometry(mesh,base); Channel(mesh,channel,texture,A);
    int calls = 0; channel.SetCallback(ObserveChannelMaterial,&calls); Draw(mesh);
    EXPECT_EQ(1,calls); ASSERT_EQ(1,State.DrawCount); EXPECT_EQ(A,State.Draws[0].Textures[1]);
}

TEST_F(MeshMonoPassFallbackTest, MixedMultipassChannelDoesNotReplayAlreadyFoldedMonoPrefix) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context), modulate(Context), additive(Context);
    RCKTexture a(Context), b(Context); Geometry(mesh, base);
    Channel(mesh, modulate, a, A); const int second = Channel(mesh, additive, b, B);
    mesh.SetChannelSourceBlend(second, VXBLEND_ONE); mesh.SetChannelDestBlend(second, VXBLEND_ONE);
    Draw(mesh); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(A, State.Draws[0].Textures[1]);
    EXPECT_EQ(B, State.Draws[1].Textures[0]);
    for (int stage = 1; stage < 4; ++stage) EXPECT_EQ(0u, State.Draws[1].Textures[stage]);
}

TEST_F(MeshMonoPassFallbackTest, FailedFallbackMaterialPropagatesToDefaultRenderAndCanRetryNextFrame) {
    MonoFallbackMesh mesh(Context); RCKMaterial base(Context), channel(Context);
    RCKTexture texture(Context); Geometry(mesh, base); Channel(mesh, channel, texture, A);
    CallbackObservation observation = {&State};
    mesh.AddSubMeshPostRenderCallBack(ObserveSubMeshPost, &observation, FALSE);
    ASSERT_EQ(1, mesh.CreateRenderGroups()); mesh.Cold();
    State.RejectBindStage = 1; State.RejectFallbackBind = TRUE;
    EXPECT_EQ(0, mesh.DefaultRender(Device, nullptr));
    EXPECT_EQ(1, State.FailedBinds); EXPECT_EQ(1, State.FailedFallbackBinds);
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(0u, State.Draws[0].Textures[1]);
    EXPECT_EQ(1, observation.Calls);
    State.RejectBindStage = -1; State.RejectFallbackBind = FALSE;
    State.DrawCount = 0; mesh.Cold();
    EXPECT_EQ(1, mesh.DefaultRender(Device, nullptr));
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(A, State.Draws[0].Textures[1]);
    EXPECT_EQ(2, observation.Calls);
}
