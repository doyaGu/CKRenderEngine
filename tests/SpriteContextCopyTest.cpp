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

class SpriteContextCopyTest : public SpriteTransferTest {};
TEST_F(SpriteContextCopyTest, CopyRejectsUnresidentAndForeignContextWithoutReadback) {
    EXPECT_FALSE(Sprite->CopyContext(View[0],nullptr,nullptr)); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0]));
    EXPECT_FALSE(Sprite->CopyContext(View[1],nullptr,nullptr)); EXPECT_EQ(0,Boundary[0].Requests); EXPECT_EQ(0,Boundary[1].Requests);
}
TEST_F(SpriteContextCopyTest, CopyQueuesBackbufferReadAndUploadStartsOnlyOnCompletion) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); ASSERT_TRUE(Sprite->CopyContext(View[0],nullptr,nullptr));
    EXPECT_EQ(1,Boundary[0].Uploads); EXPECT_FALSE(Boundary[0].HasReadRegion);
    CKDWORD pixels[16]; for(int i=0;i<16;++i) pixels[i]=0xffa00000u+i;
    VxImageDescEx image=Image(pixels); Boundary[0].Complete(&image);
    EXPECT_EQ(2,Boundary[0].Uploads); EXPECT_EQ(0xffa00000u,Pixel(Boundary[0],0)); EXPECT_EQ(0xffa0000fu,Pixel(Boundary[0],15));
    EXPECT_EQ(0xff010101u,Sprite->GetPixel(0,0)); // CopyContext updates video, not the authoritative system bitmap.
}
TEST_F(SpriteContextCopyTest, DeferredCopySnapshotsSourceAndDestinationRectangles) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); VxRect source(7,9,9,11),dest(1,2,3,4);
    ASSERT_TRUE(Sprite->CopyContext(View[0],&source,&dest)); source=VxRect(0,0,1,1); dest=VxRect(0,0,1,1);
    EXPECT_TRUE(Boundary[0].HasReadRegion); EXPECT_EQ(7,Boundary[0].ReadRegion.left); EXPECT_EQ(9,Boundary[0].ReadRegion.top);
    CKDWORD pixels[]={0xff112233,0xff445566,0xff778899,0xffabcdef}; VxImageDescEx image=Image(pixels,2,2); Boundary[0].Complete(&image);
    EXPECT_TRUE(Boundary[0].HasUploadRegion); EXPECT_EQ(1,Boundary[0].UploadRegion.left); EXPECT_EQ(2,Boundary[0].UploadRegion.top);
    EXPECT_EQ(3,Boundary[0].UploadRegion.right); EXPECT_EQ(4,Boundary[0].UploadRegion.bottom); EXPECT_EQ(0xff112233u,Pixel(Boundary[0],0));
}
TEST_F(SpriteContextCopyTest, ReadbackQueueRejectionDoesNotUpload) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); Boundary[0].FailRequest=TRUE;
    EXPECT_FALSE(Sprite->CopyContext(View[0],nullptr,nullptr)); EXPECT_EQ(nullptr,Boundary[0].PendingCallback); EXPECT_EQ(1,Boundary[0].Uploads);
}
TEST_F(SpriteContextCopyTest, ReadbackFailureDoesNotUploadOrMutateSystemPixels) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); ASSERT_TRUE(Sprite->CopyContext(View[0],nullptr,nullptr));
    Boundary[0].Complete(nullptr,FALSE); EXPECT_EQ(1,Boundary[0].Uploads); EXPECT_EQ(0xff010101u,Sprite->GetPixel(0,0));
}
TEST_F(SpriteContextCopyTest, ReadbackAfterVideoFreeDoesNotReallocateOrUpload) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); ASSERT_TRUE(Sprite->CopyContext(View[0],nullptr,nullptr)); ASSERT_TRUE(Sprite->FreeVideoMemory());
    CKDWORD pixels[16]={}; VxImageDescEx image=Image(pixels); Boundary[0].Complete(&image);
    EXPECT_EQ(1,Boundary[0].Creates); EXPECT_EQ(1,Boundary[0].Uploads); EXPECT_FALSE(Sprite->IsInVideoMemory());
}
TEST_F(SpriteContextCopyTest, ContextSwitchBeforeCompletionUploadsToRequestingCopy) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD first=Sprite->Handle(); ASSERT_TRUE(Sprite->CopyContext(View[0],nullptr,nullptr));
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[1])); CKDWORD pixels[16]={0xff123456}; VxImageDescEx image=Image(pixels); Boundary[0].Complete(&image);
    EXPECT_EQ(2,Boundary[0].Uploads); EXPECT_EQ(first,Boundary[0].UploadedHandle); EXPECT_EQ(1,Boundary[1].Uploads); EXPECT_EQ(0xff123456u,Pixel(Boundary[0],0));
}
TEST_F(SpriteContextCopyTest, ApplyClipsLeadingDestinationAndSkipsCorrespondingSourcePixels) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD pixels[16]; for(int i=0;i<16;++i) pixels[i]=0xffa00000u+i;
    VxImageDescEx image=Image(pixels); VxRect dest(-1,-1,3,3); ASSERT_TRUE(Sprite->Apply(View[0],image,&dest));
    EXPECT_EQ(0,Boundary[0].UploadRegion.left); EXPECT_EQ(0,Boundary[0].UploadRegion.top); EXPECT_EQ(3,Boundary[0].UploadRegion.right);
    EXPECT_EQ(3,Boundary[0].Uploaded.Width); EXPECT_EQ(3,Boundary[0].Uploaded.Height); EXPECT_EQ(16,Boundary[0].Uploaded.BytesPerLine);
    EXPECT_EQ(0xffa00005u,Pixel(Boundary[0],0)); EXPECT_EQ(0xffa0000fu,Pixel(Boundary[0],8));
}
TEST_F(SpriteContextCopyTest, ApplyClipsTrailingDestinationToTextureBounds) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD pixels[16]; for(int i=0;i<16;++i) pixels[i]=0xffb00000u+i;
    VxImageDescEx image=Image(pixels); VxRect dest(2,3,6,7); ASSERT_TRUE(Sprite->Apply(View[0],image,&dest));
    EXPECT_EQ(2,Boundary[0].UploadRegion.left); EXPECT_EQ(3,Boundary[0].UploadRegion.top); EXPECT_EQ(4,Boundary[0].UploadRegion.right); EXPECT_EQ(4,Boundary[0].UploadRegion.bottom);
    EXPECT_EQ(2,Boundary[0].Uploaded.Width); EXPECT_EQ(1,Boundary[0].Uploaded.Height); EXPECT_EQ(0xffb00000u,Pixel(Boundary[0],0)); EXPECT_EQ(0xffb00001u,Pixel(Boundary[0],1));
}
TEST_F(SpriteContextCopyTest, ApplyLimitsDestinationExtentToSuppliedSourceImage) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD pixels[]={0xff123456,0xffabcdef}; VxImageDescEx image=Image(pixels,2,1); VxRect dest(1,1,4,4);
    ASSERT_TRUE(Sprite->Apply(View[0],image,&dest)); EXPECT_EQ(3,Boundary[0].UploadRegion.right); EXPECT_EQ(2,Boundary[0].UploadRegion.bottom);
    EXPECT_EQ(2,Boundary[0].Uploaded.Width); EXPECT_EQ(1,Boundary[0].Uploaded.Height); EXPECT_EQ(0xffabcdefu,Pixel(Boundary[0],1));
}
TEST_F(SpriteContextCopyTest, FullyOutsideAndEmptyDestinationDoNotUpload) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD pixels[16]={}; VxImageDescEx image=Image(pixels);
    VxRect outside(5,0,9,4),empty(1,1,1,3); EXPECT_FALSE(Sprite->Apply(View[0],image,&outside)); EXPECT_FALSE(Sprite->Apply(View[0],image,&empty)); EXPECT_EQ(1,Boundary[0].Uploads);
}
TEST_F(SpriteContextCopyTest, ApplyRGB565ConversionKeepsClippedSourceOffset) {
    Sprite->SetDesiredVideoFormat(_16_RGB565); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0]));
    CKDWORD pixels[]={0xff000000,0xffff0000,0xff00ff00,0xff0000ff}; VxImageDescEx image=Image(pixels,4,1); VxRect dest(-1,0,3,1);
    ASSERT_TRUE(Sprite->Apply(View[0],image,&dest)); EXPECT_EQ(16,Boundary[0].Uploaded.BitsPerPixel); EXPECT_EQ(3,Boundary[0].Uploaded.Width);
    CKWORD colors[3]={}; memcpy(colors,Boundary[0].Bytes,6); EXPECT_EQ(0xf800,colors[0]); EXPECT_EQ(0x07e0,colors[1]); EXPECT_EQ(0x001f,colors[2]);
}
TEST_F(SpriteContextCopyTest, CompressedCopyTargetKeepsReadableARGBSourceAndPixelRegion) {
    Sprite->SetDesiredVideoFormat(_DXT5); ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0]));
    CKDWORD pixels[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff}; VxImageDescEx image=Image(pixels,2,2); VxRect dest(1,1,3,3);
    ASSERT_TRUE(Sprite->Apply(View[0],image,&dest)); EXPECT_EQ(32,Boundary[0].Uploaded.BitsPerPixel); EXPECT_EQ(0u,Boundary[0].Uploaded.Flags);
    EXPECT_EQ(0xffff0000u,Pixel(Boundary[0],0)); EXPECT_EQ(0xffffffffu,Pixel(Boundary[0],3)); EXPECT_EQ(1,Boundary[0].UploadRegion.left); EXPECT_EQ(3,Boundary[0].UploadRegion.right);
}
TEST_F(SpriteContextCopyTest, ApplyUploadFailureLeavesVideoAndBitmapStateIntact) {
    ASSERT_TRUE(Sprite->SystemToVideoMemory(View[0])); CKDWORD handle=Sprite->Handle(),pixels[16]={}; VxImageDescEx image=Image(pixels);
    Boundary[0].FailUpload=TRUE; EXPECT_FALSE(Sprite->Apply(View[0],image)); EXPECT_TRUE(Sprite->IsInVideoMemory()); EXPECT_EQ(handle,Sprite->Handle());
    EXPECT_FALSE(Sprite->ToRestore()); EXPECT_EQ(0xff010101u,Sprite->GetPixel(0,0)); EXPECT_EQ(0,Boundary[0].Deletes);
}

}
