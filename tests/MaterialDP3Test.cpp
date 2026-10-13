#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKRenderedScene.h"
#include "RCKLight.h"
#include "RCKTexture.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Stores only material calls at the public rasterizer boundary; no GPU or shading implementation.
class MaterialDP3TestSpy : public CKRasterizerContext {
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
    MaterialDP3TestSpy() {
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
    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override { for(int stage=FirstStage; stage<FirstStage+StageCount; ++stage) { Textures[stage]=0; for(int state=0; state<CKRST_TSS_MAXSTATE; ++state) StageStates[stage][state]=0; Matrices[VXMATRIX_TEXTURE0+stage].SetIdentity(); } return TRUE; } // Observe the reset boundary only.
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
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data) override { ADD_FAILURE() << "Unexpected draw boundary"; return FALSE; }
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

struct DP3TextureCalls { int Count=0; int Stages[8]={}; int Handles[8]={}; CKBOOL Clamps[8]={}; };
class DP3ObservedTexture : public RCKTexture {
public:
    MaterialDP3TestSpy *State;
    DP3TextureCalls *Calls;
    int Handle;
    CKBOOL Succeed=TRUE;
    DP3ObservedTexture(CKContext *context, MaterialDP3TestSpy *state, DP3TextureCalls *calls, int handle)
        : RCKTexture(context), State(state), Calls(calls), Handle(handle) {}
    CKBOOL SetAsCurrent(CKRenderContext *device, CKBOOL clamp, int stage) override {
        EXPECT_NE(nullptr,device); EXPECT_LT(Calls->Count,8);
        if (Calls->Count<8) { const int index=Calls->Count++; Calls->Stages[index]=stage; Calls->Handles[index]=Handle; Calls->Clamps[index]=clamp; }
        // Observe the texture's public virtual handoff only; no image upload/GPU implementation.
        if (Succeed) State->SetTexture(Handle,stage);
        return Succeed;
    }
};
class MaterialDP3Test : public ::testing::Test {
protected:
    CKContext *Context=nullptr;
    RCKRenderContext *Device=nullptr;
    RCK3dEntity *Root=nullptr;
    MaterialDP3TestSpy State;
    DP3TextureCalls Calls;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK,CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight,CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial,CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture,CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK,CKCreateContext(&Context,nullptr,0,0));
        new RCKRenderManager(Context); Device=new RCKRenderContext(Context); Device->m_RasterizerContext=&State;
        Root=reinterpret_cast<RCK3dEntity *>(Device->m_RenderedScene->GetRootEntity()); ASSERT_NE(nullptr,Root);
        Root->SetWorldMatrix(VxMatrix::Identity()); Device->SetWorldTransformationMatrix(VxMatrix::Identity());
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext=nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Textures(RCKMaterial &material, DP3ObservedTexture &color, DP3ObservedTexture &normal) {
        material.SetTexture(0,&color); material.SetTexture(1,&normal);
    }
    void Reference(RCKMaterial &material, CKObject *object) {
        material.SetEffect(VXEFFECT_DP3); CKParameter *parameter=material.GetEffectParameter(); ASSERT_NE(nullptr,parameter);
        ASSERT_EQ(static_cast<int>(sizeof(CK_ID)),parameter->GetDataSize()); const CK_ID id=object?object->GetID():0;
        ASSERT_EQ(CK_OK,parameter->SetValue(&id));
    }
    RCKLight *Light(VXLIGHT_TYPE type, const VxMatrix &world) {
        RCKLight *light=static_cast<RCKLight *>(Context->CreateObject(CKCID_LIGHT)); EXPECT_NE(nullptr,light);
        if (light) { light->SetType(type); light->SetWorldMatrix(world); } return light;
    }
    void IdentityTexture(int stage) {
        const VxMatrix &matrix=State.Matrices[VXMATRIX_TEXTURE0+stage];
        for(int row=0; row<4; ++row) for(int column=0; column<4; ++column) EXPECT_FLOAT_EQ(row==column?1:0,matrix[row][column]);
    }
};
}

TEST_F(MaterialDP3Test, BindsNormalSlotOneThenColorSlotZeroToRequestedConsecutiveStagesWithSharedUVIndex) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    State.Matrices[VXMATRIX_TEXTURE0+2][3][0]=10; State.Matrices[VXMATRIX_TEXTURE0+3][0][0]=9;
    ASSERT_EQ(2u,material.DP3Effect(Device,2)); ASSERT_EQ(2,Calls.Count);
    EXPECT_EQ(42,Calls.Handles[0]); EXPECT_EQ(2,Calls.Stages[0]); EXPECT_FALSE(Calls.Clamps[0]);
    EXPECT_EQ(41,Calls.Handles[1]); EXPECT_EQ(3,Calls.Stages[1]); EXPECT_FALSE(Calls.Clamps[1]);
    EXPECT_EQ(42u,State.Textures[2]); EXPECT_EQ(41u,State.Textures[3]); IdentityTexture(2); IdentityTexture(3);
    EXPECT_EQ(2u,State.StageStates[2][CKRST_TSS_TEXCOORDINDEX]); EXPECT_EQ(2u,State.StageStates[3][CKRST_TSS_TEXCOORDINDEX]);
    EXPECT_EQ(CKRST_TTF_NONE,State.StageStates[2][CKRST_TSS_TEXTURETRANSFORMFLAGS]); EXPECT_EQ(CKRST_TTF_NONE,State.StageStates[3][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
}
TEST_F(MaterialDP3Test, DotProductAndModulateStagesUseDistinctColorInputsAndMaterialFiltering) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    material.SetTextureMinMode(VXTEXTUREFILTER_NEAREST); material.SetTextureMagMode(VXTEXTUREFILTER_LINEAR); material.SetTextureAddressMode(VXTEXTURE_ADDRESSMIRROR);
    ASSERT_EQ(2u,material.DP3Effect(Device,0));
    EXPECT_EQ(CKRST_TOP_DOTPRODUCT3,State.StageStates[0][CKRST_TSS_OP]); EXPECT_EQ(CKRST_TA_TEXTURE,State.StageStates[0][CKRST_TSS_ARG1]); EXPECT_EQ(CKRST_TA_TFACTOR,State.StageStates[0][CKRST_TSS_ARG2]);
    EXPECT_EQ(CKRST_TOP_MODULATE,State.StageStates[1][CKRST_TSS_OP]); EXPECT_EQ(CKRST_TA_TEXTURE,State.StageStates[1][CKRST_TSS_ARG1]); EXPECT_EQ(CKRST_TA_CURRENT,State.StageStates[1][CKRST_TSS_ARG2]);
    for(int stage=0; stage<2; ++stage) {
        EXPECT_EQ(VXTEXTUREFILTER_NEAREST,State.StageStates[stage][CKRST_TSS_MINFILTER]); EXPECT_EQ(VXTEXTUREFILTER_LINEAR,State.StageStates[stage][CKRST_TSS_MAGFILTER]);
        EXPECT_EQ(VXTEXTURE_ADDRESSMIRROR,State.StageStates[stage][CKRST_TSS_ADDRESS]);
        EXPECT_EQ(VXTEXTURE_ADDRESSMIRROR,State.StageStates[stage][CKRST_TSS_ADDRESSU]); EXPECT_EQ(VXTEXTURE_ADDRESSMIRROR,State.StageStates[stage][CKRST_TSS_ADDRESSV]);
    }
}
TEST_F(MaterialDP3Test, DefaultReferentialIsViewpointPositionAndUsesCurrentObjectWorldDisplacement) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    Root->SetPosition3f(4,2,3); VxMatrix world=VxMatrix::Identity(); world[3].Set(1,2,3,1); Device->SetWorldTransformationMatrix(world);
    ASSERT_EQ(2u,material.DP3Effect(Device,0)); EXPECT_EQ(0xFF018080u,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
}
TEST_F(MaterialDP3Test, DirectionalLightUsesWorldDirectionAndIgnoresItsPosition) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    VxMatrix world=VxMatrix::Identity(); world[3].Set(100,200,300,1); RCKLight *light=Light(VX_LIGHTDIREC,world); ASSERT_NE(nullptr,light); Reference(material,light);
    ASSERT_EQ(2u,material.DP3Effect(Device,0)); EXPECT_EQ(0xFF800180u,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
}
TEST_F(MaterialDP3Test, DirectionalXAxisUsesInverseObjectRotationAndDoesNotUseCameraView) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    VxMatrix lightWorld=VxMatrix::Identity(); lightWorld[0].Set(0,0,-1,0); lightWorld[2].Set(1,0,0,0); RCKLight *light=Light(VX_LIGHTDIREC,lightWorld); ASSERT_NE(nullptr,light); Reference(material,light);
    VxMatrix world=VxMatrix::Identity(); world[0].Set(0,1,0,0); world[1].Set(-1,0,0,0); world[3].Set(20,30,40,1); Device->SetWorldTransformationMatrix(world);
    Device->SetViewTransformationMatrix(VxMatrix::Identity()); ASSERT_EQ(2u,material.DP3Effect(Device,0)); EXPECT_EQ(0xFF8080FFu,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
}
TEST_F(MaterialDP3Test, PointLightUsesRelativePositionInsteadOfOrientation) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    VxMatrix world=VxMatrix::Identity(); world[3].Set(4,2,3,1); RCKLight *light=Light(VX_LIGHTPOINT,world); ASSERT_NE(nullptr,light); Reference(material,light);
    VxMatrix object=VxMatrix::Identity(); object[3].Set(1,2,3,1); Device->SetWorldTransformationMatrix(object);
    ASSERT_EQ(2u,material.DP3Effect(Device,1)); EXPECT_EQ(0xFF018080u,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
}
TEST_F(MaterialDP3Test, SpotLightUsesPositionAndNormalizesBeforeSignedDirectionEncoding) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    VxMatrix world=VxMatrix::Identity(); world[3].Set(-3,-4,0,1); RCKLight *light=Light(VX_LIGHTSPOT,world); ASSERT_NE(nullptr,light); Reference(material,light);
    ASSERT_EQ(2u,material.DP3Effect(Device,0)); EXPECT_EQ(0xFFCC801Au,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
}
TEST_F(MaterialDP3Test, EntityReferentialUsesPositionLikeNondirectionalLight) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    RCK3dEntity *entity=static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); ASSERT_NE(nullptr,entity); entity->SetPosition3f(0,-7,0); Reference(material,entity);
    ASSERT_EQ(2u,material.DP3Effect(Device,0)); EXPECT_EQ(0xFF808001u,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
}
TEST_F(MaterialDP3Test, NullParameterValueFallsBackToViewpointInsteadOfZeroDirection) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    Root->SetPosition3f(-3,0,0); Reference(material,nullptr);
    ASSERT_EQ(2u,material.DP3Effect(Device,0)); EXPECT_EQ(0xFFFF8080u,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
}
TEST_F(MaterialDP3Test, OptionalNormalTextureLeavesEmptyFirstStageAndStillBindsColorOnSecondStage) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41); material.SetTexture0(&color);
    State.Textures[0]=999; ASSERT_EQ(2u,material.DP3Effect(Device,0)); ASSERT_EQ(1,Calls.Count);
    EXPECT_EQ(1,Calls.Stages[0]); EXPECT_EQ(41,Calls.Handles[0]); EXPECT_EQ(0u,State.Textures[0]); EXPECT_EQ(41u,State.Textures[1]);
    EXPECT_EQ(CKRST_TOP_DOTPRODUCT3,State.StageStates[0][CKRST_TSS_OP]);
}
TEST_F(MaterialDP3Test, NormalTextureBindingFailureReturnsFailureBitAndDoesNotBindPrimary) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal); normal.Succeed=FALSE;
    State.Textures[2]=999; EXPECT_NE(0u,material.DP3Effect(Device,2)&0x80000000u); ASSERT_EQ(1,Calls.Count);
    EXPECT_EQ(42,Calls.Handles[0]); EXPECT_EQ(2,Calls.Stages[0]); EXPECT_EQ(0u,State.Textures[2]);
}
TEST_F(MaterialDP3Test, PrimaryTextureBindingFailureReturnsFailureBitInsteadOfReportingCompleteEffect) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal); color.Succeed=FALSE;
    State.Textures[3]=999; EXPECT_NE(0u,material.DP3Effect(Device,2)&0x80000000u); ASSERT_EQ(2,Calls.Count);
    EXPECT_EQ(42,Calls.Handles[0]); EXPECT_EQ(41,Calls.Handles[1]); EXPECT_EQ(0u,State.Textures[3]);
}
TEST_F(MaterialDP3Test, EffectPreservesOtherStagesAndMaterialLogicalProperties) {
    RCKMaterial material(Context); DP3ObservedTexture color(Context,&State,&Calls,41), normal(Context,&State,&Calls,42); Textures(material,color,normal);
    material.SetDiffuse(VxColor(.1f,.2f,.3f,.4f)); material.EnableAlphaBlend(TRUE); material.SetZFunc(VXCMP_GREATER);
    State.Textures[0]=777; State.StageStates[0][CKRST_TSS_OP]=CKRST_TOP_ADDSIGNED; State.Matrices[VXMATRIX_TEXTURE0][3][0]=19;
    ASSERT_EQ(2u,material.DP3Effect(Device,2)); EXPECT_EQ(777u,State.Textures[0]); EXPECT_EQ(CKRST_TOP_ADDSIGNED,State.StageStates[0][CKRST_TSS_OP]);
    EXPECT_FLOAT_EQ(19,State.Matrices[VXMATRIX_TEXTURE0][3][0]); EXPECT_FLOAT_EQ(.4f,material.GetDiffuse().a);
    EXPECT_TRUE(material.AlphaBlendEnabled()); EXPECT_EQ(VXCMP_GREATER,material.GetZFunc()); EXPECT_EQ(&color,material.GetTexture(0)); EXPECT_EQ(&normal,material.GetTexture(1));
}

