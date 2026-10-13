#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
extern void SetProcessorSpecific_FunctionsPtr();
namespace {
// Only the external submission boundary is observed. Production clipping,
// callbacks, bitmap alpha picking and vertex generation run unchanged.
class Entity2dDrawSpy : public CKRasterizerContext {
public:
    CKDWORD States[1024] = {};
    CKMaterialData MaterialPayload = {};
    int MaterialWrites = 0;
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
    Entity2dDrawSpy() {
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
    CKBOOL SetMaterial(const CKMaterialData *Data) override { MaterialPayload = *Data; ++MaterialWrites; return TRUE; }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override { return TRUE; } // No shading or material behavior is tested.
    CKBOOL SetViewport(const CKViewportData *Data) override { Viewport = *Data; return TRUE; }
    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer call: SetUserClipPlane"; return FALSE; }
    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer call: GetUserClipPlane"; return FALSE; }
    void InitDefaultRenderStatesValue() override { ADD_FAILURE() << "Unexpected rasterizer call: InitDefaultRenderStatesValue"; }
    struct Batch {
        VXPRIMITIVETYPE Type; int Count; VxVector4 Points[4]; CKDWORD Colors[4];
        CKWORD Indices[5]; CKDWORD ZFunc, ZWrite; CKViewportData View;
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
        b.ZFunc=States[VXRENDERSTATE_ZFUNC]; b.ZWrite=States[VXRENDERSTATE_ZWRITEENABLE]; b.View=Viewport;
        return RejectDraw != DrawCount;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVB"; return FALSE; }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex, CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVBIB"; return FALSE; }
    CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateTexture"; return FALSE; }
    CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel, CKRST_CUBEFACE Face, const CKRECT *Region) override { ADD_FAILURE() << "Unexpected rasterizer call: LoadTexture"; return FALSE; }
    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetTextureDesc"; return FALSE; }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateVertexBuffer"; return FALSE; }
    CKBOOL GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetVertexBufferDesc"; return FALSE; }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateIndexBuffer"; return FALSE; }
    CKBOOL GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetIndexBufferDesc"; return FALSE; }
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: LockVertexBuffer"; return nullptr; }
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override { ADD_FAILURE() << "Unexpected rasterizer call: UnlockVertexBuffer"; return FALSE; }
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: LockIndexBuffer"; return nullptr; }
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override { ADD_FAILURE() << "Unexpected rasterizer call: UnlockIndexBuffer"; return FALSE; }
    CKBOOL DeleteObject(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type) override { ADD_FAILURE() << "Unexpected rasterizer call: DeleteObject"; return FALSE; }
    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override { ADD_FAILURE() << "Unexpected rasterizer call: FlushObjects"; return FALSE; }
    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override { ADD_FAILURE() << "Unexpected rasterizer call: SetResourceName"; return FALSE; }
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: SetTargetTexture"; return FALSE; }
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToTexture"; return FALSE; }
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToMemoryBuffer"; return FALSE; }
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyFromMemoryBuffer"; return FALSE; }
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback, void *User) override { ADD_FAILURE() << "Unexpected rasterizer call: RequestReadback"; return FALSE; }
};

class Inspect2d : public RCK2dEntity {
public:
    explicit Inspect2d(CKContext *context):RCK2dEntity(context) {}
    const VxRect &Pixels() const { return m_VtxPos; }
    const VxRect &UV() const { return m_SrcRect; }
};
struct CallbackProbe { Entity2dDrawSpy *Spy; int Count=0; int At[4]={}; };
CKBOOL Callback(CKRenderContext *, CKRenderObject *, void *argument) {
    CallbackProbe *p=static_cast<CallbackProbe *>(argument); p->At[p->Count++]=p->Spy->DrawCount; return FALSE;
}
class Entity2dRenderTest : public ::testing::Test {
protected:
    CKContext *Context=nullptr; RCKRenderContext *View=nullptr;
    Entity2dDrawSpy Spy; Inspect2d *Objects[8]={}; int ObjectCount=0;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context,nullptr,0,0));
        new RCKRenderManager(Context); View=new RCKRenderContext(Context);
        View->m_RasterizerContext=&Spy; Context->m_BehaviorContext.CurrentRenderContext=View;
        View->m_Settings.m_Rect.right=400; View->m_Settings.m_Rect.bottom=300;
        View->SetViewRect(VxRect(0,0,200,100));
    }
    void TearDown() override {
        Context->m_BehaviorContext.CurrentRenderContext=nullptr;
        for(int i=ObjectCount-1;i>=0;--i) delete Objects[i];
        View->m_RasterizerContext=nullptr; delete View; CKCloseContext(Context); CKShutdown();
    }
    Inspect2d *Entity(const VxRect &rect=VxRect(10,20,60,70)) {
        Inspect2d *e=new Inspect2d(Context); Objects[ObjectCount++]=e;
        e->EnableRatioOffset(FALSE); e->SetRect(rect); return e;
    }
    void Material(Inspect2d *e) {
        RCKMaterial *m=static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        m->SetDiffuse(VxColor(1.0f,0.0f,0.0f,1.0f)); e->SetMaterial(m);
    }
    static CK2dEntity *API(RCK2dEntity *e) { return reinterpret_cast<CK2dEntity *>(e); }
    static void Rect(const VxRect &r,float l,float t,float right,float b) {
        EXPECT_NEAR(l,r.left,1e-6f); EXPECT_NEAR(t,r.top,1e-6f);
        EXPECT_NEAR(right,r.right,1e-6f); EXPECT_NEAR(b,r.bottom,1e-6f);
    }
    void Visible(Inspect2d *e) { e->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ,0); }
};
TEST_F(Entity2dRenderTest, FourEdgeClippingUsesOriginalUVOriginForBothTrailingEdges) {
    Inspect2d *e=Entity(VxRect(-100,-50,300,150)); ASSERT_TRUE(e->UpdateExtents(View));
    Rect(e->Pixels(),0,0,200,100); Rect(e->UV(),.25f,.25f,.75f,.75f);
}
TEST_F(Entity2dRenderTest, FourEdgeClippingPreservesExplicitSourceSubrectangle) {
    Inspect2d *e=Entity(VxRect(-100,-50,300,150)); e->UseSourceRect(TRUE);
    e->SetSourceRect(VxRect(.2f,.3f,.8f,.9f)); ASSERT_TRUE(e->UpdateExtents(View));
    Rect(e->UV(),.35f,.45f,.65f,.75f);
}
TEST_F(Entity2dRenderTest, LeadingEdgeClippingAndOutsideCacheAreIndependent) {
    Inspect2d *e=Entity(VxRect(-100,-50,100,50)); ASSERT_TRUE(e->UpdateExtents(View));
    Rect(e->Pixels(),0,0,100,50); Rect(e->UV(),.5f,.5f,1,1);
    e->SetRect(VxRect(210,110,230,130)); EXPECT_FALSE(e->UpdateExtents(View));
    Rect(e->Pixels(),0,0,0,0); Rect(e->UV(),0,0,0,0);
}
TEST_F(Entity2dRenderTest, RatioOffsetUsesViewportOriginAndWindowClipUsesWindowSize) {
    View->SetViewRect(VxRect(20,30,220,130)); Inspect2d *e=Entity(VxRect(10,10,60,60));
    e->EnableRatioOffset(TRUE); ASSERT_TRUE(e->UpdateExtents(View)); Rect(e->Pixels(),30,40,80,90);
    e->EnableRatioOffset(FALSE); e->EnableClipToCamera(FALSE); e->SetRect(VxRect(250,150,350,250));
    ASSERT_TRUE(e->UpdateExtents(View)); Rect(e->Pixels(),250,150,350,250);
}
TEST_F(Entity2dRenderTest, ParentClippingIntersectsActualParentCache) {
    Inspect2d *p=Entity(VxRect(40,20,140,80)), *e=Entity(VxRect(0,0,200,100));
    e->SetParent(API(p)); e->SetClipToParent(TRUE); ASSERT_TRUE(p->UpdateExtents(View));
    ASSERT_TRUE(e->UpdateExtents(View)); Rect(e->Pixels(),40,20,140,80); Rect(e->UV(),.2f,.2f,.7f,.8f);
}
TEST_F(Entity2dRenderTest, PickUsesClippedInclusiveBoundsAndRejectsInterfaceObjects) {
    Inspect2d *e=Entity(VxRect(-20,20,60,70)); ASSERT_TRUE(e->UpdateExtents(View));
    EXPECT_EQ(API(e),e->Pick(Vx2DVector(0,20),FALSE)); EXPECT_EQ(API(e),e->Pick(Vx2DVector(60,70),FALSE));
    EXPECT_EQ(nullptr,e->Pick(Vx2DVector(-1,30),FALSE)); Visible(e); EXPECT_EQ(nullptr,e->Pick(Vx2DVector(20,30),FALSE));
}
TEST_F(Entity2dRenderTest, PickTraversesChildrenBackToFrontAndHonorsIgnoreUnpickable) {
    Inspect2d *p=Entity(), *a=Entity(), *b=Entity(); a->SetParent(API(p)); b->SetParent(API(p));
    a->SetZOrder(1); b->SetZOrder(2); ASSERT_TRUE(p->UpdateExtents(View)); ASSERT_TRUE(a->UpdateExtents(View)); ASSERT_TRUE(b->UpdateExtents(View));
    EXPECT_EQ(API(b),p->Pick(Vx2DVector(30,40),FALSE)); b->SetPickable(FALSE);
    EXPECT_EQ(API(a),p->Pick(Vx2DVector(30,40),FALSE)); EXPECT_EQ(API(b),p->Pick(Vx2DVector(30,40),TRUE));
}
TEST_F(Entity2dRenderTest, PickAllowsChildrenOfOrdinaryHiddenParentButPrunesHierarchyHide) {
    Inspect2d *p=Entity(), *e=Entity(); e->SetParent(API(p)); ASSERT_TRUE(p->UpdateExtents(View)); ASSERT_TRUE(e->UpdateExtents(View));
    p->Show(CKHIDE); EXPECT_EQ(API(e),p->Pick(Vx2DVector(30,40),FALSE));
    p->Show(CKHIERARCHICALHIDE); EXPECT_EQ(nullptr,p->Pick(Vx2DVector(30,40),FALSE));
}
TEST_F(Entity2dRenderTest, PrecisePickReadsActualBitmapAlpha) {
    Inspect2d *e=Entity(VxRect(0,0,100,100)); Material(e);
    RCKTexture *t=static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE)); ASSERT_TRUE(t->CreateImage(3,3,32));
    for(int y=0;y<3;++y) { ASSERT_TRUE(t->SetPixel(0,y,0x00ffffff)); ASSERT_TRUE(t->SetPixel(1,y,0xffffffff)); ASSERT_TRUE(t->SetPixel(2,y,0xffffffff)); }
    t->SetPickThreshold(128);
    e->GetMaterial()->SetTexture0(t); e->GetMaterial()->SetAlphaRef(128); e->GetMaterial()->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
    ASSERT_TRUE(e->UpdateExtents(View)); EXPECT_EQ(nullptr,e->Pick(Vx2DVector(10,50),FALSE)); EXPECT_EQ(API(e),e->Pick(Vx2DVector(60,50),FALSE));
}
TEST_F(Entity2dRenderTest, MaterialDrawGeneratesRoundedQuadAndRestoresStates) {
    Inspect2d *e=Entity(VxRect(10.2f,20.6f,60.8f,70.1f)); Material(e); ASSERT_TRUE(e->UpdateExtents(View));
    Spy.States[VXRENDERSTATE_ZFUNC]=VXCMP_LESS; Spy.States[VXRENDERSTATE_ZWRITEENABLE]=TRUE;
    ASSERT_EQ(CK_OK,e->Draw(View)); ASSERT_EQ(1,Spy.DrawCount); EXPECT_EQ(VX_TRIANGLEFAN,Spy.Batches[0].Type);
    EXPECT_EQ(4,Spy.Batches[0].Count); EXPECT_FLOAT_EQ(10,Spy.Batches[0].Points[0].x); EXPECT_FLOAT_EQ(21,Spy.Batches[0].Points[0].y);
    EXPECT_FLOAT_EQ(61,Spy.Batches[0].Points[2].x); EXPECT_FLOAT_EQ(70,Spy.Batches[0].Points[2].y);
    for(int i=0;i<4;++i) { EXPECT_EQ(0xffff0000u,Spy.Batches[0].Colors[i]); EXPECT_FLOAT_EQ(0,Spy.Batches[0].Points[i].z); EXPECT_FLOAT_EQ(1,Spy.Batches[0].Points[i].w); }
    EXPECT_EQ(VXCMP_LESS,Spy.States[VXRENDERSTATE_ZFUNC]); EXPECT_TRUE(Spy.States[VXRENDERSTATE_ZWRITEENABLE]);
    EXPECT_EQ(1, Spy.MaterialWrites);
    EXPECT_FLOAT_EQ(1.0f, Spy.MaterialPayload.Diffuse.r); EXPECT_FLOAT_EQ(0.0f, Spy.MaterialPayload.Diffuse.g);
    EXPECT_FLOAT_EQ(0.0f, Spy.MaterialPayload.Diffuse.b); EXPECT_FLOAT_EQ(1.0f, Spy.MaterialPayload.Diffuse.a);
}
TEST_F(Entity2dRenderTest, BackgroundDrawTemporarilyDisablesDepthAndRestoresViewport) {
    Inspect2d *e=Entity(); Material(e); e->SetBackground(TRUE); e->EnableClipToCamera(FALSE); ASSERT_TRUE(e->UpdateExtents(View));
    Spy.States[VXRENDERSTATE_ZFUNC]=VXCMP_LESS; Spy.States[VXRENDERSTATE_ZWRITEENABLE]=TRUE;
    ASSERT_EQ(CK_OK,e->Draw(View)); EXPECT_EQ(VXCMP_ALWAYS,Spy.Batches[0].ZFunc); EXPECT_FALSE(Spy.Batches[0].ZWrite);
    EXPECT_EQ(400,Spy.Batches[0].View.ViewWidth); EXPECT_EQ(300,Spy.Batches[0].View.ViewHeight);
    EXPECT_EQ(200,View->m_ViewportData.ViewWidth); EXPECT_EQ(100,View->m_ViewportData.ViewHeight);
    EXPECT_EQ(VXCMP_LESS,Spy.States[VXRENDERSTATE_ZFUNC]); EXPECT_TRUE(Spy.States[VXRENDERSTATE_ZWRITEENABLE]);
}
TEST_F(Entity2dRenderTest, EditorPlaceholderGeneratesTransparentFillAndClosedWhiteOutline) {
    Inspect2d *e=Entity(); ASSERT_TRUE(e->UpdateExtents(View)); ASSERT_EQ(CK_OK,e->Draw(View)); ASSERT_EQ(2,Spy.DrawCount);
    EXPECT_EQ(VX_TRIANGLEFAN,Spy.Batches[0].Type); EXPECT_EQ(0x66000000u,Spy.Batches[0].Colors[0]);
    EXPECT_EQ(VX_LINESTRIP,Spy.Batches[1].Type); EXPECT_EQ(5,Spy.Batches[1].Count);
    for(int i=0;i<5;++i) EXPECT_EQ(i%4,Spy.Batches[1].Indices[i]);
    EXPECT_EQ(0xffffffffu,Spy.Batches[1].Colors[2]); EXPECT_FLOAT_EQ(59,Spy.Batches[1].Points[2].x); EXPECT_FLOAT_EQ(69,Spy.Batches[1].Points[2].y);
    EXPECT_FLOAT_EQ(60,Spy.Batches[0].Points[2].x); EXPECT_FLOAT_EQ(70,Spy.Batches[0].Points[2].y);
}
TEST_F(Entity2dRenderTest, FailedDrawPropagatesErrorAndRestoresStates) {
    Inspect2d *e=Entity(); Material(e); ASSERT_TRUE(e->UpdateExtents(View)); Spy.RejectDraw=1;
    Spy.States[VXRENDERSTATE_FOGENABLE]=TRUE; EXPECT_EQ(CKERR_INVALIDOPERATION,e->Draw(View));
    EXPECT_TRUE(Spy.States[VXRENDERSTATE_FOGENABLE]); EXPECT_EQ(1,Spy.DrawCount);
}
TEST_F(Entity2dRenderTest, FailedPlaceholderOutlinePropagatesErrorAndRestoresStates) {
    Inspect2d *e=Entity(); ASSERT_TRUE(e->UpdateExtents(View)); Spy.RejectDraw=2;
    Spy.States[VXRENDERSTATE_ZWRITEENABLE]=TRUE; EXPECT_EQ(CKERR_INVALIDOPERATION,e->Draw(View));
    EXPECT_TRUE(Spy.States[VXRENDERSTATE_ZWRITEENABLE]); EXPECT_EQ(2,Spy.DrawCount);
}
TEST_F(Entity2dRenderTest, RenderCallsPreDrawChildrenAndPostInOrderIgnoringCallbackFalse) {
    Inspect2d *p=Entity(), *e=Entity(); e->SetParent(API(p)); Visible(p); Visible(e); Material(p); Material(e);
    CallbackProbe probe{&Spy}; ASSERT_TRUE(p->AddPreRenderCallBack(Callback,&probe,FALSE)); ASSERT_TRUE(p->AddPostRenderCallBack(Callback,&probe,FALSE));
    ASSERT_EQ(CK_OK,p->Render(View)); EXPECT_EQ(2,Spy.DrawCount); ASSERT_EQ(2,probe.Count); EXPECT_EQ(0,probe.At[0]); EXPECT_EQ(2,probe.At[1]);
}
TEST_F(Entity2dRenderTest, ClippedRenderStillCallsCallbacksAndUnclippedChildren) {
    Inspect2d *p=Entity(VxRect(300,200,350,250)), *a=Entity(), *b=Entity(); a->SetParent(API(p)); b->SetParent(API(p));
    b->SetClipToParent(TRUE); Visible(p); Visible(a); Visible(b); Material(p); Material(a); Material(b);
    CallbackProbe probe{&Spy}; p->AddPreRenderCallBack(Callback,&probe,FALSE); p->AddPostRenderCallBack(Callback,&probe,FALSE);
    ASSERT_EQ(CK_OK,p->Render(View)); EXPECT_EQ(1,Spy.DrawCount); ASSERT_EQ(2,probe.Count); EXPECT_EQ(0,probe.At[0]); EXPECT_EQ(1,probe.At[1]);
}
TEST_F(Entity2dRenderTest, HiddenRenderAllowsChildrenAndHierarchyHidePrunesAll) {
    Inspect2d *p=Entity(), *e=Entity(); e->SetParent(API(p)); Visible(p); Visible(e); Material(p); Material(e);
    p->Show(CKHIDE); ASSERT_EQ(CK_OK,p->Render(View)); EXPECT_EQ(1,Spy.DrawCount);
    Spy.DrawCount=0; p->Show(CKHIERARCHICALHIDE); ASSERT_EQ(CK_OK,p->Render(View)); EXPECT_EQ(0,Spy.DrawCount);
}
TEST_F(Entity2dRenderTest, RenderRetainsFirstFailureWhileFinishingChildrenAndPostCallback) {
    Inspect2d *p=Entity(), *e=Entity(); e->SetParent(API(p)); Visible(p); Visible(e); Material(p); Material(e); Spy.RejectDraw=1;
    CallbackProbe probe{&Spy}; p->AddPostRenderCallBack(Callback,&probe,FALSE);
    EXPECT_EQ(CKERR_INVALIDOPERATION,p->Render(View)); EXPECT_EQ(2,Spy.DrawCount); ASSERT_EQ(1,probe.Count); EXPECT_EQ(2,probe.At[0]);
}
} // namespace
