#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKSprite.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
extern void SetProcessorSpecific_FunctionsPtr();
namespace {
// Acknowledges external resource and submit calls only. The real Sprite,
// BitmapData and RenderManager generate images, rectangles, uploads and vertices.
class SpriteSubmissionSpy : public CKRasterizerContext {
public:
    CKDWORD States[1024] = {};
    VxMatrix World = VxMatrix::Identity();
    CKViewportData Viewport = {};
    int WorldWrites = 0, StateWrites = 0, PresentCount = 0;
    CKDWORD Textures[CKRST_MAX_TEXTURE_STAGES] = {};
    CKDWORD StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE] = {};
    VxMatrix Matrices[32];
    int DrawCount = 0, BatchVertexCount = 0, BatchIndexCount = 0;
    CKWORD BatchIndices[6] = {};
    VxVector4 BatchPositions[4];
    VxVector BatchWorldPosition;
    SpriteSubmissionSpy() {
        for (int i = 0; i < 32; ++i) Matrices[i].SetIdentity();
    }
    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) override { ADD_FAILURE() << "Unexpected rasterizer call: Create"; return FALSE; }
    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: Resize"; return FALSE; }
    CKBOOL SetOptions(const CKRasterizerOptions *Options) override { ADD_FAILURE() << "Unexpected rasterizer call: SetOptions"; return FALSE; }
    CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const override { return FALSE; }
    CKERROR GetDeviceStatus() const override { return CK_OK; }
    CKBOOL BeginShutdown() override { return TRUE; }
    CKBOOL IsIdle() const override { return TRUE; }
    CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects) override { ADD_FAILURE() << "Unexpected rasterizer call: Clear"; return FALSE; }
    CKBOOL BeginScene() override { ADD_FAILURE() << "Unexpected rasterizer call: BeginScene"; return FALSE; }
    CKBOOL EndScene() override { ADD_FAILURE() << "Unexpected rasterizer call: EndScene"; return FALSE; }
    CKBOOL BeginOverlayPhase() override { ADD_FAILURE() << "Unexpected rasterizer call: BeginOverlayPhase"; return FALSE; }
    CKBOOL BackToFront(CKBOOL VSync) override { ++PresentCount; return TRUE; }
    CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) override { States[(unsigned int)State] = Value; ++StateWrites; return TRUE; }
    CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) override { *Value = States[(unsigned int)State]; return TRUE; }
    CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) override { StageStates[Stage][Tss] = Value; return TRUE; }
    CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) override { *Value = StageStates[Stage][Tss]; return TRUE; }
    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override { return TRUE; } // Boundary acknowledgement; no pipeline implementation.
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override { Textures[Stage] = Texture; return TRUE; }
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override { *Texture = Textures[Stage]; return TRUE; }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override { Matrices[(unsigned int)Type] = Mat; if (Type == VXMATRIX_WORLD) { World = Mat; ++WorldWrites; } return TRUE; }
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override { Mat = Matrices[(unsigned int)Type]; return TRUE; }
    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override { ADD_FAILURE() << "Unexpected rasterizer call: SetLight"; return FALSE; }
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override { ADD_FAILURE() << "Unexpected rasterizer call: EnableLight"; return FALSE; }
    CKBOOL SetMaterial(const CKMaterialData *Data) override { ADD_FAILURE() << "Unexpected rasterizer call: SetMaterial"; return FALSE; }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override { return TRUE; } // No shading or material behavior is tested.
    CKBOOL SetViewport(const CKViewportData *Data) override { Viewport = *Data; return TRUE; }
    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer call: SetUserClipPlane"; return FALSE; }
    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer call: GetUserClipPlane"; return FALSE; }
    void InitDefaultRenderStatesValue() override { ADD_FAILURE() << "Unexpected rasterizer call: InitDefaultRenderStatesValue"; }
    struct Batch {
        VXPRIMITIVETYPE Type; int Count; VxVector4 Points[4]; CKDWORD Colors[4];
        Vx2DVector UV[4]; CKDWORD Texture; CKDWORD Blend,AlphaTest,Lighting,MinFilter,MagFilter,Address; CKWORD Indices[5]; CKDWORD ZFunc, ZWrite; CKViewportData View;
    } Batches[8];
    int RejectDraw = 0;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_LT(DrawCount, 8); if (DrawCount >= 8 || !data) return FALSE;
        Batch &b = Batches[DrawCount++]; b.Type=type; b.Count=count;
        EXPECT_EQ(4, data->VertexCount); EXPECT_NE(nullptr, data->PositionPtr); EXPECT_NE(nullptr, data->ColorPtr);
        for (int i=0;i<4;++i) {
            b.Points[i]=*reinterpret_cast<VxVector4 *>(static_cast<char *>(data->PositionPtr)+i*data->PositionStride);
            b.Colors[i]=*reinterpret_cast<CKDWORD *>(static_cast<char *>(data->ColorPtr)+i*data->ColorStride);
        }
        for(int i=0;i<5;++i) b.Indices[i]=indices && i<count ? indices[i] : 0xffff;
        b.Texture=Textures[0]; b.Blend=States[VXRENDERSTATE_ALPHABLENDENABLE]; b.AlphaTest=States[VXRENDERSTATE_ALPHATESTENABLE]; b.Lighting=States[VXRENDERSTATE_LIGHTING]; b.MinFilter=StageStates[0][CKRST_TSS_MINFILTER]; b.MagFilter=StageStates[0][CKRST_TSS_MAGFILTER]; b.Address=StageStates[0][CKRST_TSS_ADDRESS];
        for(int i=0;i<4;++i) b.UV[i]=*reinterpret_cast<Vx2DVector *>(static_cast<char *>(data->TexCoordPtr)+i*data->TexCoordStride);
        b.ZFunc=States[VXRENDERSTATE_ZFUNC]; b.ZWrite=States[VXRENDERSTATE_ZWRITEENABLE]; b.View=Viewport;
        return RejectDraw != DrawCount;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVB"; return FALSE; }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex, CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVBIB"; return FALSE; }
    int Creates=0, Uploads=0, Deletes=0; bool RejectCreate=false,RejectUpload=false; CKDWORD NextHandle=100;
    CKTextureDesc Created; CKDWORD UploadedPixel=0;
    CKBOOL CreateTexture(const CKTextureDesc *desc, CKDWORD *out) override {
        ++Creates; Created=*desc; if(RejectCreate)return FALSE; *out=NextHandle++; return TRUE;
    }
    CKBOOL LoadTexture(CKDWORD texture,const VxImageDescEx &image,int mip,CKRST_CUBEFACE face,const CKRECT *region) override {
        ++Uploads; EXPECT_NE(0u,texture); EXPECT_EQ(0,mip); EXPECT_EQ(nullptr,region);
        if(image.Image && image.BitsPerPixel==32) UploadedPixel=*reinterpret_cast<CKDWORD *>(image.Image);
        return !RejectUpload;
    }
    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetTextureDesc"; return FALSE; }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateVertexBuffer"; return FALSE; }
    CKBOOL GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetVertexBufferDesc"; return FALSE; }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateIndexBuffer"; return FALSE; }
    CKBOOL GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetIndexBufferDesc"; return FALSE; }
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: LockVertexBuffer"; return nullptr; }
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override { ADD_FAILURE() << "Unexpected rasterizer call: UnlockVertexBuffer"; return FALSE; }
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: LockIndexBuffer"; return nullptr; }
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override { ADD_FAILURE() << "Unexpected rasterizer call: UnlockIndexBuffer"; return FALSE; }
    CKBOOL DeleteObject(CKRST_HANDLE handle,CKRST_OBJECTTYPE type) override { ++Deletes; EXPECT_NE(0u,handle); EXPECT_EQ(CKRST_OBJ_TEXTURE,type); return TRUE; }
    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override { ADD_FAILURE() << "Unexpected rasterizer call: FlushObjects"; return FALSE; }
    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override { ADD_FAILURE() << "Unexpected rasterizer call: SetResourceName"; return FALSE; }
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: SetTargetTexture"; return FALSE; }
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToTexture"; return FALSE; }
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToMemoryBuffer"; return FALSE; }
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyFromMemoryBuffer"; return FALSE; }
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback, void *User) override { ADD_FAILURE() << "Unexpected rasterizer call: RequestReadback"; return FALSE; }
};

class SpriteDrawTest:public ::testing::Test {
protected:
    CKContext *Context=nullptr; RCKRenderManager *Manager=nullptr;
    RCKRenderContext *View=nullptr; RCKSprite *Sprite=nullptr; SpriteSubmissionSpy Spy;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK,CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity,CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite,CKCID_2DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK,CKCreateContext(&Context,nullptr,0,0));
        Manager=new RCKRenderManager(Context); View=new RCKRenderContext(Context);
        View->m_RasterizerContext=&Spy; Context->m_BehaviorContext.CurrentRenderContext=View;
        View->m_Settings.m_Rect.right=400; View->m_Settings.m_Rect.bottom=300;
        View->SetViewRect(VxRect(0,0,200,100));
        Sprite=static_cast<RCKSprite *>(Context->CreateObject(CKCID_SPRITE)); ASSERT_NE(nullptr,Sprite);
        Sprite->SetDesiredVideoFormat(_32_ARGB8888); Sprite->EnableRatioOffset(FALSE);
    }
    void TearDown() override {
        if(Sprite) EXPECT_TRUE(Sprite->FreeVideoMemory());
        Context->m_BehaviorContext.CurrentRenderContext=nullptr;
        View->m_RasterizerContext=nullptr; delete View; CKCloseContext(Context); CKShutdown();
    }
    void Image() { ASSERT_TRUE(Sprite->Create(4,2,32)); Sprite->SetRect(VxRect(10,20,50,40)); ASSERT_TRUE(Sprite->UpdateExtents(View)); }
};
TEST_F(SpriteDrawTest, InvalidBitmapRejectsWithoutAnyResourceOrSubmitCall) {
    EXPECT_EQ(CKERR_INVALIDPARAMETER,Sprite->Draw(View)); EXPECT_EQ(0,Spy.Creates); EXPECT_EQ(0,Spy.DrawCount);
}
TEST_F(SpriteDrawTest, ValidBitmapRejectsMissingOrHeadlessRenderContext) {
    Image(); EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT,Sprite->Draw(nullptr));
    View->m_RasterizerContext=nullptr; EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT,Sprite->Draw(View));
    View->m_RasterizerContext=&Spy; EXPECT_EQ(0,Spy.Creates);
}
TEST_F(SpriteDrawTest, FirstDrawUploadsRealBitmapAndBuildsWhiteNormalizedQuad) {
    Image(); ASSERT_TRUE(Sprite->SetPixel(0,0,0xff123456));
    ASSERT_EQ(CK_OK,Sprite->Draw(View)); ASSERT_EQ(1,Spy.DrawCount); EXPECT_EQ(1,Spy.Creates); EXPECT_EQ(1,Spy.Uploads);
    EXPECT_EQ(0xff123456u,Spy.UploadedPixel); EXPECT_EQ(4,Spy.Created.Format.Width); EXPECT_EQ(2,Spy.Created.Format.Height);
    const auto &b=Spy.Batches[0]; EXPECT_EQ(VX_TRIANGLEFAN,b.Type); EXPECT_EQ(4,b.Count); EXPECT_EQ(100u,b.Texture);
    EXPECT_FLOAT_EQ(9.5f,b.Points[0].x); EXPECT_FLOAT_EQ(19.5f,b.Points[0].y); EXPECT_FLOAT_EQ(49.5f,b.Points[2].x); EXPECT_FLOAT_EQ(39.5f,b.Points[2].y);
    const float u[]={0,1,1,0},v[]={0,0,1,1};
    for(int i=0;i<4;++i) { EXPECT_EQ(0xffffffffu,b.Colors[i]); EXPECT_FLOAT_EQ(u[i],b.UV[i].x); EXPECT_FLOAT_EQ(v[i],b.UV[i].y); EXPECT_FLOAT_EQ(0,b.Points[i].z); EXPECT_FLOAT_EQ(1,b.Points[i].w); }
}
TEST_F(SpriteDrawTest, BothSideClippingUsesPixelSourceCacheBeforeNormalizedSubmission) {
    Image(); Sprite->SetRect(VxRect(-100,-50,300,150)); ASSERT_TRUE(Sprite->UpdateExtents(View));
    ASSERT_EQ(CK_OK,Sprite->Draw(View)); const auto &b=Spy.Batches[0];
    EXPECT_FLOAT_EQ(.25f,b.UV[0].x); EXPECT_FLOAT_EQ(.25f,b.UV[0].y); EXPECT_FLOAT_EQ(.75f,b.UV[2].x); EXPECT_FLOAT_EQ(.75f,b.UV[2].y);
    EXPECT_FLOAT_EQ(-.5f,b.Points[0].x); EXPECT_FLOAT_EQ(199.5f,b.Points[2].x);
}
TEST_F(SpriteDrawTest, ExplicitPixelSourceAndRatioOffsetFeedActualQuad) {
    Image(); View->SetViewRect(VxRect(20,30,220,130)); Sprite->EnableRatioOffset(TRUE);
    Sprite->UseSourceRect(TRUE); Sprite->SetSourceRect(VxRect(1,0,3,1)); ASSERT_TRUE(Sprite->UpdateExtents(View));
    ASSERT_EQ(CK_OK,Sprite->Draw(View)); const auto &b=Spy.Batches[0];
    EXPECT_FLOAT_EQ(.25f,b.UV[0].x); EXPECT_FLOAT_EQ(0,b.UV[0].y); EXPECT_FLOAT_EQ(.75f,b.UV[2].x); EXPECT_FLOAT_EQ(.5f,b.UV[2].y);
    EXPECT_FLOAT_EQ(29.5f,b.Points[0].x); EXPECT_FLOAT_EQ(49.5f,b.Points[0].y);
}
TEST_F(SpriteDrawTest, OpaqueSubmissionSelectsPixelSamplingAndRestoresCallerStates) {
    Image(); Spy.States[VXRENDERSTATE_LIGHTING]=TRUE; Spy.States[VXRENDERSTATE_ZWRITEENABLE]=TRUE;
    Spy.States[VXRENDERSTATE_ALPHABLENDENABLE]=TRUE; Spy.Textures[0]=77;
    Spy.StageStates[0][CKRST_TSS_MINFILTER]=VXTEXTUREFILTER_LINEAR; Spy.StageStates[0][CKRST_TSS_MAGFILTER]=VXTEXTUREFILTER_LINEAR;
    ASSERT_EQ(CK_OK,Sprite->Draw(View)); const auto &b=Spy.Batches[0];
    EXPECT_FALSE(b.Lighting); EXPECT_FALSE(b.ZWrite); EXPECT_EQ(VXCMP_ALWAYS,b.ZFunc); EXPECT_FALSE(b.Blend); EXPECT_FALSE(b.AlphaTest);
    EXPECT_EQ(VXTEXTUREFILTER_NEAREST,b.MinFilter); EXPECT_EQ(VXTEXTUREFILTER_NEAREST,b.MagFilter); EXPECT_EQ(VXTEXTURE_ADDRESSCLAMP,b.Address);
    EXPECT_TRUE(Spy.States[VXRENDERSTATE_LIGHTING]); EXPECT_TRUE(Spy.States[VXRENDERSTATE_ZWRITEENABLE]); EXPECT_TRUE(Spy.States[VXRENDERSTATE_ALPHABLENDENABLE]);
    EXPECT_EQ(77u,Spy.Textures[0]); EXPECT_EQ(VXTEXTUREFILTER_LINEAR,Spy.StageStates[0][CKRST_TSS_MINFILTER]);
}
TEST_F(SpriteDrawTest, TransparentSubmissionSetsBlendAndAlphaTest) {
    Image(); Sprite->SetTransparent(TRUE); Sprite->SetTransparentColor(0x00123456); Sprite->SetPixel(0,0,0xff123456);
    ASSERT_EQ(CK_OK,Sprite->Draw(View)); EXPECT_TRUE(Spy.Batches[0].Blend); EXPECT_TRUE(Spy.Batches[0].AlphaTest);
    EXPECT_EQ(0x00123456u,Spy.UploadedPixel);
}
TEST_F(SpriteDrawTest, ResidentCleanSecondDrawReusesResourceWithoutUpload) {
    Image(); ASSERT_EQ(CK_OK,Sprite->Draw(View)); ASSERT_EQ(CK_OK,Sprite->Draw(View));
    EXPECT_EQ(1,Spy.Creates); EXPECT_EQ(1,Spy.Uploads); EXPECT_EQ(2,Spy.DrawCount);
}
TEST_F(SpriteDrawTest, ReleasedSurfaceChangesCauseFreshUploadBeforeNextDraw) {
    Image(); ASSERT_EQ(CK_OK,Sprite->Draw(View)); CKBYTE *p=Sprite->LockSurfacePtr(); ASSERT_NE(nullptr,p);
    *reinterpret_cast<CKDWORD *>(p)=0xffabcdef; ASSERT_TRUE(Sprite->ReleaseSurfacePtr());
    ASSERT_EQ(CK_OK,Sprite->Draw(View)); EXPECT_EQ(1,Spy.Creates); EXPECT_EQ(2,Spy.Uploads); EXPECT_EQ(0xffabcdefu,Spy.UploadedPixel);
}
TEST_F(SpriteDrawTest, CreationFailureStopsDrawAndCanRetry) {
    Image(); Spy.RejectCreate=true; EXPECT_EQ(CKERR_INVALIDOPERATION,Sprite->Draw(View)); EXPECT_EQ(0,Spy.DrawCount);
    Spy.RejectCreate=false; EXPECT_EQ(CK_OK,Sprite->Draw(View)); EXPECT_EQ(1,Spy.DrawCount);
}
TEST_F(SpriteDrawTest, UploadFailureDeletesPartialResourceAndCanRetry) {
    Image(); Spy.RejectUpload=true; EXPECT_EQ(CKERR_INVALIDOPERATION,Sprite->Draw(View)); EXPECT_EQ(0,Spy.DrawCount);
    EXPECT_EQ(1,Spy.Deletes); EXPECT_FALSE(Sprite->IsInVideoMemory()); Spy.RejectUpload=false;
    EXPECT_EQ(CK_OK,Sprite->Draw(View)); EXPECT_EQ(1,Spy.DrawCount);
}
TEST_F(SpriteDrawTest, DirtyUploadFailureSkipsSubmitAndRetainsRetry) {
    Image(); ASSERT_EQ(CK_OK,Sprite->Draw(View)); CKBYTE *p=Sprite->LockSurfacePtr(); ASSERT_NE(nullptr,p); *reinterpret_cast<CKDWORD *>(p)=0xff102030; ASSERT_TRUE(Sprite->ReleaseSurfacePtr());
    Spy.RejectUpload=true; EXPECT_EQ(CKERR_INVALIDOPERATION,Sprite->Draw(View)); EXPECT_EQ(1,Spy.DrawCount);
    Spy.RejectUpload=false; EXPECT_EQ(CK_OK,Sprite->Draw(View)); EXPECT_EQ(2,Spy.DrawCount); EXPECT_EQ(0xff102030u,Spy.UploadedPixel);
}
TEST_F(SpriteDrawTest, SubmissionFailurePropagatesAndRestoresCallerState) {
    Image(); Spy.RejectDraw=1; Spy.States[VXRENDERSTATE_FOGENABLE]=TRUE;
    EXPECT_EQ(CKERR_INVALIDOPERATION,Sprite->Draw(View)); EXPECT_EQ(1,Spy.DrawCount); EXPECT_TRUE(Spy.States[VXRENDERSTATE_FOGENABLE]);
    EXPECT_TRUE(Sprite->IsInVideoMemory());
}
} // namespace
