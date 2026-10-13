#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKSprite.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
extern void SetProcessorSpecific_FunctionsPtr();
namespace {
// Resource and readback acknowledgement only. Production bitmap conversion,
// per-context resource bookkeeping and deferred-copy dispatch execute normally.
class SpriteTransferBoundary : public CKRasterizerContext {
public:
    int Creates=0,Uploads=0,Deletes=0,Requests=0;
    CKDWORD NextHandle=1,UploadedHandle=0,DeletedHandle=0;
    CKBOOL FailCreate=FALSE,FailUpload=FALSE,FailDelete=FALSE,FailRequest=FALSE;
    CKTextureDesc Created;
    VxImageDescEx Uploaded;
    CKBYTE Bytes[2048]={}; int ByteCount=0;
    CKBOOL HasUploadRegion=FALSE,HasReadRegion=FALSE;
    CKRECT UploadRegion={},ReadRegion={};
    CKReadbackCallback PendingCallback=nullptr; void *PendingUser=nullptr;
    void Complete(const VxImageDescEx *image,CKBOOL success=TRUE) {
        CKReadbackCallback callback=PendingCallback; void *user=PendingUser;
        PendingCallback=nullptr; PendingUser=nullptr;
        ASSERT_NE(nullptr,callback); callback(user,HasReadRegion?&ReadRegion:nullptr,VXBUFFER_BACKBUFFER,image,success);
    }
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
    SpriteTransferBoundary() {
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
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int, VxDrawPrimitiveData *) override {
        ADD_FAILURE()<<"Unexpected GPU submission"; return FALSE;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVB"; return FALSE; }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex, CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVBIB"; return FALSE; }
    CKBOOL CreateTexture(const CKTextureDesc *desc, CKDWORD *handle) override {
        ++Creates; Created = *desc;
        if (FailCreate) return FALSE;
        *handle = NextHandle++; return TRUE;
    }
    CKBOOL LoadTexture(CKDWORD handle, const VxImageDescEx &image, int level, CKRST_CUBEFACE face, const CKRECT *region) override {
        ++Uploads; UploadedHandle=handle; Uploaded=image; Uploaded.Image=nullptr;
        EXPECT_EQ(0,level); EXPECT_EQ(CKRST_CUBEFACE_XPOS,face);
        HasUploadRegion=region!=nullptr; if(region) UploadRegion=*region;
        memset(Bytes,0,sizeof(Bytes)); ByteCount=0;
        const int row=image.Width*(image.BitsPerPixel/8);
        if(image.Image && row>0 && row*image.Height<=sizeof(Bytes)) {
            for(int y=0;y<image.Height;++y)
                memcpy(Bytes+y*row,image.Image+y*image.BytesPerLine,row);
            ByteCount=row*image.Height;
        }
        return !FailUpload;
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
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override {
        ++Deletes; DeletedHandle=handle; EXPECT_EQ(CKRST_OBJ_TEXTURE,type);
        return !FailDelete;
    }
    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override { ADD_FAILURE() << "Unexpected rasterizer call: FlushObjects"; return FALSE; }
    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override { ADD_FAILURE() << "Unexpected rasterizer call: SetResourceName"; return FALSE; }
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: SetTargetTexture"; return FALSE; }
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToTexture"; return FALSE; }
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToMemoryBuffer"; return FALSE; }
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyFromMemoryBuffer"; return FALSE; }
    CKBOOL RequestReadback(const CKRECT *rect, VXBUFFER_TYPE buffer, CKReadbackCallback callback, void *user) override {
        ++Requests; EXPECT_EQ(VXBUFFER_BACKBUFFER,buffer);
        HasReadRegion=rect!=nullptr; if(rect) ReadRegion=*rect;
        if(FailRequest) return FALSE;
        EXPECT_EQ(nullptr,PendingCallback); PendingCallback=callback; PendingUser=user;
        return TRUE;
    }
};


class TransferSprite : public RCKSprite {
public:
    explicit TransferSprite(CKContext *c):RCKSprite(c) {}
    CKDWORD Handle() const { return m_ObjectIndex; }
    CKBOOL Apply(RCKRenderContext *c,const VxImageDescEx &s,const VxRect *d=nullptr) { return ApplyContextCopy(c,s,d); }
};
class SpriteTransferTest : public ::testing::Test {
protected:
    CKContext *Context=nullptr; RCKRenderManager *Manager=nullptr;
    RCKRenderContext *View[2]={}; SpriteTransferBoundary Boundary[2]; TransferSprite *Sprite=nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK,CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity,CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite,CKCID_2DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK,CKCreateContext(&Context,nullptr,0,0));
        Manager=new RCKRenderManager(Context); Manager->m_SpriteVideoFormat.Value=_32_ARGB8888;
        Boundary[1].NextHandle=100;
        for(int i=0;i<2;++i) { View[i]=new RCKRenderContext(Context); View[i]->m_RasterizerContext=&Boundary[i]; }
        Sprite=new TransferSprite(Context); ASSERT_TRUE(Sprite->Create(4,4,32));
        for(int y=0;y<4;++y) for(int x=0;x<4;++x) ASSERT_TRUE(Sprite->SetPixel(x,y,0xff000000u|((y*4+x+1)*0x010101u)));
    }
    void TearDown() override {
        for(int i=0;i<2;++i) {
            if(Boundary[i].PendingCallback) Boundary[i].Complete(nullptr,FALSE);
            Boundary[i].FailDelete=FALSE;
        }
        delete Sprite;
        for(int i=0;i<2;++i) { View[i]->m_RasterizerContext=nullptr; delete View[i]; }
        CKCloseContext(Context); CKShutdown();
    }
    CKDWORD Pixel(const SpriteTransferBoundary &b,int index) {
        CKDWORD pixel=0; memcpy(&pixel,b.Bytes+index*4,4); return pixel;
    }
    static VxImageDescEx Image(CKDWORD *pixels,int w=4,int h=4) {
        VxImageDescEx image; VxPixelFormat2ImageDesc(_32_ARGB8888,image);
        image.Width=w; image.Height=h; image.BytesPerLine=w*4; image.Image=reinterpret_cast<CKBYTE *>(pixels); return image;
    }
};

class SpriteVideoMemoryTest : public SpriteTransferTest {};
TEST_F(SpriteVideoMemoryTest, UnresidentQueriesLeaveDescriptorAndDirtyPixelsIntact) {
    VxImageDescEx desc; desc.Width=123;
    EXPECT_FALSE(Sprite->IsInVideoMemory()); EXPECT_FALSE(Sprite->GetVideoTextureDesc(desc)); EXPECT_EQ(123,desc.Width);
    EXPECT_EQ(UNKNOWN_PF,Sprite->GetVideoPixelFormat()); EXPECT_TRUE(Sprite->ToRestore()); EXPECT_FALSE(Sprite->Restore());
}
TEST_F(SpriteVideoMemoryTest, UploadCreatesSpriteTextureWithOriginalPixelsAndClearsDirtyState) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); ASSERT_EQ(1,Boundary[0].Creates); ASSERT_EQ(1,Boundary[0].Uploads);
    EXPECT_EQ(4,Boundary[0].Created.Format.Width); EXPECT_EQ(4,Boundary[0].Created.Format.Height);
    EXPECT_NE(0u,Boundary[0].Created.Flags&CKRST_TEXTURE_SPRITE); EXPECT_NE(0u,Boundary[0].Created.Flags&CKRST_TEXTURE_MANAGED);
    EXPECT_EQ(0,Boundary[0].Created.MipMapCount); EXPECT_EQ(Sprite->Handle(),Boundary[0].UploadedHandle);
    EXPECT_EQ(0xff010101u,Pixel(Boundary[0],0)); EXPECT_EQ(0xff101010u,Pixel(Boundary[0],15));
    EXPECT_TRUE(Sprite->IsInVideoMemory()); EXPECT_FALSE(Sprite->ToRestore()); EXPECT_FALSE(Manager->SpriteObjectNeedsUpdate(Sprite));
    VxImageDescEx desc; ASSERT_TRUE(Sprite->GetVideoTextureDesc(desc)); EXPECT_EQ(4,desc.Width); EXPECT_EQ(4,desc.Height);
    EXPECT_EQ(_32_ARGB8888,Sprite->GetVideoPixelFormat());
}
TEST_F(SpriteVideoMemoryTest, RestoreUsesCurrentSlotAndWritesChangedSurfaceBytes) {
    ASSERT_TRUE(Sprite->SetSlotCount(2)); ASSERT_TRUE(Sprite->Create(4,4,32,1)); ASSERT_TRUE(Sprite->SetPixel(0,0,0xffe02040,1));
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); ASSERT_TRUE(Sprite->SetCurrentSlot(1)); ASSERT_TRUE(Sprite->Restore(TRUE));
    EXPECT_EQ(0xffe02040u,Pixel(Boundary[0],0)); EXPECT_FALSE(Sprite->ToRestore());
}
TEST_F(SpriteVideoMemoryTest, TransparencyUpdatesActualAlphaBeforeUpload) {
    ASSERT_TRUE(Sprite->SetPixel(0,0,0xff123456)); ASSERT_TRUE(Sprite->SetPixel(1,0,0xffabcdef));
    Sprite->SetTransparentColor(0x00123456); Sprite->SetTransparent(TRUE);
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); EXPECT_EQ(0x00123456u,Pixel(Boundary[0],0)); EXPECT_EQ(0xffaacceeu,Pixel(Boundary[0],1));
}
TEST_F(SpriteVideoMemoryTest, DesiredRGB565UsesRealBlitterConversion) {
    ASSERT_TRUE(Sprite->SetPixel(0,0,0xffff0000)); ASSERT_TRUE(Sprite->SetPixel(1,0,0xff00ff00)); ASSERT_TRUE(Sprite->SetPixel(2,0,0xff0000ff));
    Sprite->SetDesiredVideoFormat(_16_RGB565); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0]));
    EXPECT_EQ(_16_RGB565,Sprite->GetDesiredVideoFormat()); EXPECT_EQ(_16_RGB565,Sprite->GetVideoPixelFormat()); EXPECT_EQ(16,Boundary[0].Uploaded.BitsPerPixel);
    CKWORD colors[3]={}; memcpy(colors,Boundary[0].Bytes,6); EXPECT_EQ(0xf800,colors[0]); EXPECT_EQ(0x07e0,colors[1]); EXPECT_EQ(0x001f,colors[2]);
}
TEST_F(SpriteVideoMemoryTest, UnsupportedRequestedFormatUsesManagerFallbackAndPreservesRequest) {
    Manager->m_SpriteVideoFormat.Value=_16_RGB565; Sprite->SetDesiredVideoFormat(static_cast<VX_PIXELFORMAT>(999));
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); EXPECT_EQ(_16_RGB565,Sprite->GetVideoPixelFormat()); EXPECT_EQ(999,Sprite->GetDesiredVideoFormat());
}
TEST_F(SpriteVideoMemoryTest, UnknownRequestedAndManagerFormatsFallBackToARGB) {
    Manager->m_SpriteVideoFormat.Value=UNKNOWN_PF; Sprite->SetDesiredVideoFormat(UNKNOWN_PF);
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); EXPECT_EQ(_32_ARGB8888,Sprite->GetVideoPixelFormat());
}
TEST_F(SpriteVideoMemoryTest, CompressedTargetReceivesSystemPixelsWithoutUnsupportedCPUCompression) {
    Sprite->SetDesiredVideoFormat(_DXT1); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0]));
    EXPECT_EQ(_DXT1,Sprite->GetVideoPixelFormat()); EXPECT_EQ(32,Boundary[0].Uploaded.BitsPerPixel);
    EXPECT_EQ(0u,Boundary[0].Uploaded.Flags); EXPECT_EQ(16,Boundary[0].Uploaded.BytesPerLine);
    EXPECT_EQ(0xff010101u,Pixel(Boundary[0],0)); EXPECT_EQ(0xff101010u,Pixel(Boundary[0],15));
}
TEST_F(SpriteVideoMemoryTest, AllocationFailureCreatesNoResidentOrUpload) {
    Boundary[0].FailCreate=TRUE; EXPECT_FALSE(Sprite->SystemToVideoMemory(View[0])); EXPECT_FALSE(Sprite->IsInVideoMemory());
    EXPECT_EQ(0,Boundary[0].Uploads); EXPECT_EQ(0,Boundary[0].Deletes); EXPECT_TRUE(Sprite->ToRestore());
}
TEST_F(SpriteVideoMemoryTest, InitialUploadFailureDeletesAllocationAndAllowsRetry) {
    Boundary[0].FailUpload=TRUE; EXPECT_FALSE(Sprite->SystemToVideoMemory(View[0])); EXPECT_FALSE(Sprite->IsInVideoMemory());
    EXPECT_EQ(1,Boundary[0].Deletes); EXPECT_TRUE(Sprite->ToRestore()); Boundary[0].FailUpload=FALSE;
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); EXPECT_EQ(2,Boundary[0].Creates); EXPECT_FALSE(Sprite->ToRestore());
}
TEST_F(SpriteVideoMemoryTest, RestoreFailureKeepsResidentHandleAndDirtyRetry) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD handle=Sprite->Handle(); ASSERT_TRUE(Sprite->SetPixel(0,0,0xff654321));
    ASSERT_TRUE(Sprite->ReleaseSurfacePtr());
    Boundary[0].FailUpload=TRUE; EXPECT_FALSE(Sprite->Restore()); EXPECT_EQ(handle,Sprite->Handle()); EXPECT_TRUE(Sprite->IsInVideoMemory());
    EXPECT_TRUE(Sprite->ToRestore()); EXPECT_TRUE(Manager->SpriteObjectNeedsUpdate(Sprite)); Boundary[0].FailUpload=FALSE;
    ASSERT_TRUE(Sprite->Restore()); EXPECT_EQ(0xff654321u,Pixel(Boundary[0],0)); EXPECT_FALSE(Sprite->ToRestore());
}
TEST_F(SpriteVideoMemoryTest, ReallocationDeletionFailurePreservesOldHandle) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD handle=Sprite->Handle(); Boundary[0].FailDelete=TRUE;
    EXPECT_FALSE(Sprite->SystemToVideoMemory(View[0])); EXPECT_EQ(handle,Sprite->Handle()); EXPECT_TRUE(Sprite->IsInVideoMemory()); EXPECT_EQ(1,Boundary[0].Creates);
}
TEST_F(SpriteVideoMemoryTest, SameDesiredFormatDoesNotDeleteResidentObject) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD handle=Sprite->Handle(); Sprite->SetDesiredVideoFormat(_32_ARGB8888);
    EXPECT_EQ(0,Boundary[0].Deletes); EXPECT_EQ(handle,Sprite->Handle());
}
TEST_F(SpriteVideoMemoryTest, DesiredFormatDeletionFailurePreservesRequestAndResidentFormat) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); Boundary[0].FailDelete=TRUE; Sprite->SetDesiredVideoFormat(_16_RGB565);
    EXPECT_EQ(_32_ARGB8888,Sprite->GetDesiredVideoFormat()); EXPECT_EQ(_32_ARGB8888,Sprite->GetVideoPixelFormat()); EXPECT_TRUE(Sprite->IsInVideoMemory());
}
TEST_F(SpriteVideoMemoryTest, DesiredFormatChangeReleasesAllContextCopiesBeforeNextUpload) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[1])); Sprite->SetDesiredVideoFormat(_16_RGB565);
    EXPECT_EQ(1,Boundary[0].Deletes); EXPECT_EQ(1,Boundary[1].Deletes); EXPECT_FALSE(Sprite->IsInVideoMemory());
    EXPECT_EQ(_16_RGB565,Sprite->GetDesiredVideoFormat()); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); EXPECT_EQ(_16_RGB565,Sprite->GetVideoPixelFormat());
}
TEST_F(SpriteVideoMemoryTest, ContextSelectionReusesHandleAndDirtyStateRemainsIndependent) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD first=Sprite->Handle(); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[1])); CKDWORD second=Sprite->Handle();
    EXPECT_NE(first,second); EXPECT_EQ(0,Boundary[0].Deletes); ASSERT_TRUE(Manager->SelectSpriteObject(Sprite,&Boundary[0])); EXPECT_EQ(first,Sprite->Handle());
    CKDWORD *surface=reinterpret_cast<CKDWORD *>(Sprite->LockSurfacePtr()); ASSERT_NE(nullptr,surface);
    surface[0]=0xff112233; ASSERT_TRUE(Sprite->ReleaseSurfacePtr()); ASSERT_TRUE(Sprite->Restore());
    EXPECT_FALSE(Manager->SpriteObjectNeedsUpdate(Sprite)); ASSERT_TRUE(Manager->SelectSpriteObject(Sprite,&Boundary[1]));
    EXPECT_EQ(second,Sprite->Handle()); EXPECT_TRUE(Manager->SpriteObjectNeedsUpdate(Sprite)); ASSERT_TRUE(Sprite->Restore());
    EXPECT_EQ(0xff112233u,Pixel(Boundary[1],0)); EXPECT_EQ(1,Boundary[0].Creates); EXPECT_EQ(1,Boundary[1].Creates);
}
TEST_F(SpriteVideoMemoryTest, FreePartialFailureKeepsOnlyFailedContextUntilRetry) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD first=Sprite->Handle(); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[1]));
    Boundary[0].FailDelete=TRUE; EXPECT_FALSE(Sprite->FreeVideoMemory()); EXPECT_TRUE(Sprite->IsInVideoMemory()); EXPECT_EQ(first,Sprite->Handle());
    EXPECT_FALSE(Manager->SelectSpriteObject(Sprite,&Boundary[1])); ASSERT_TRUE(Manager->SelectSpriteObject(Sprite,&Boundary[0]));
    Boundary[0].FailDelete=FALSE; EXPECT_TRUE(Sprite->FreeVideoMemory()); EXPECT_FALSE(Sprite->IsInVideoMemory()); EXPECT_EQ(2,Boundary[0].Deletes); EXPECT_EQ(1,Boundary[1].Deletes);
}
TEST_F(SpriteVideoMemoryTest, FreeUnresidentIsIdempotentAndDetachedQueriesReportUnknown) {
    EXPECT_TRUE(Sprite->FreeVideoMemory()); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); EXPECT_TRUE(Sprite->FreeVideoMemory());
    EXPECT_TRUE(Sprite->FreeVideoMemory()); EXPECT_FALSE(Sprite->IsInVideoMemory()); EXPECT_EQ(UNKNOWN_PF,Sprite->GetVideoPixelFormat()); EXPECT_EQ(1,Boundary[0].Deletes);
}
TEST_F(SpriteVideoMemoryTest, ForgettingContextRetainsOtherLiveCopyThenDetachesLast) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD first=Sprite->Handle(); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[1]));
    Manager->ForgetRasterizerContext(&Boundary[1]); EXPECT_TRUE(Sprite->IsInVideoMemory()); EXPECT_EQ(first,Sprite->Handle());
    Manager->ForgetRasterizerContext(&Boundary[0]); EXPECT_FALSE(Sprite->IsInVideoMemory()); EXPECT_EQ(UNKNOWN_PF,Sprite->GetVideoPixelFormat());
    EXPECT_EQ(0,Boundary[0].Deletes); EXPECT_EQ(0,Boundary[1].Deletes);
}

}
