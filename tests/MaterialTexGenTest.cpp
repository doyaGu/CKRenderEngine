#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKRenderedScene.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Stores only material calls at the public rasterizer boundary; no GPU or shading implementation.
class MaterialTexGenTestSpy : public CKRasterizerContext {
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
    MaterialTexGenTestSpy() {
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

class MaterialTexGenTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    RCK3dEntity *Root = nullptr;
    MaterialTexGenTestSpy State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context); Device = new RCKRenderContext(Context); Device->m_RasterizerContext = &State;
        ASSERT_NE(nullptr, Device->m_RenderedScene);
        Root = reinterpret_cast<RCK3dEntity *>(Device->m_RenderedScene->GetRootEntity()); ASSERT_NE(nullptr, Root);
        Root->SetWorldMatrix(VxMatrix::Identity()); Device->SetWorldTransformationMatrix(VxMatrix::Identity());
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    RCK3dEntity *Entity(const VxMatrix &world) {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        EXPECT_NE(nullptr, entity); if (entity) entity->SetWorldMatrix(world); return entity;
    }
    static VxMatrix RotationZ() {
        VxMatrix result = VxMatrix::Identity(); result[0].Set(0, 1, 0, 0); result[1].Set(-1, 0, 0, 0); return result;
    }
    static VxMatrix RotationX() {
        VxMatrix result = VxMatrix::Identity(); result[1].Set(0, 0, 1, 0); result[2].Set(0, -1, 0, 0); return result;
    }
    void Coordinates(int stage, CKDWORD mode, CKDWORD count) {
        EXPECT_EQ(CKRSTPackTexcoordIndex(stage, mode), State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX]);
        EXPECT_EQ(count, State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
    }
    void XYZ(int stage, const VxMatrix &expected) {
        const VxMatrix &actual = State.Matrices[VXMATRIX_TEXTURE0 + stage];
        // COUNT2/COUNT3 consume xyz; their unused homogeneous w is not a bitwise compatibility requirement.
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 3; ++column)
            EXPECT_NEAR(expected[row][column], actual[row][column], 1e-5f) << row << "," << column;
    }
    void UV(int stage, float x, float y, float z, float u, float v) {
        const VxMatrix &m = State.Matrices[VXMATRIX_TEXTURE0 + stage];
        EXPECT_NEAR(u, x*m[0][0]+y*m[1][0]+z*m[2][0]+m[3][0], 1e-5f);
        EXPECT_NEAR(v, x*m[0][1]+y*m[1][1]+z*m[2][1]+m[3][1], 1e-5f);
    }
};
}

TEST_F(MaterialTexGenTest, NoneResetsPriorGeneratedMatrixAndReturnsCoordinatesSuccess) {
    RCKMaterial material(Context); VxMatrix stale = RotationZ(); stale[3].Set(17, 18, 19, 1);
    State.Matrices[VXMATRIX_TEXTURE0 + 2] = stale; State.StageStates[2][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_COUNT3;
    ASSERT_EQ(1u, material.TexGenEffect(Device, VXEFFECT_TGNONE, Root, 2));
    Coordinates(2, CKRST_TEXGEN_PASSTHRU, CKRST_TTF_NONE); XYZ(2, VxMatrix::Identity());
}
TEST_F(MaterialTexGenTest, RootTransformUsesIdentityUVBasisEvenWhenViewpointWorldIsMovedAndRotated) {
    RCKMaterial material(Context); VxMatrix moved = RotationZ(); moved[3].Set(7, 8, 9, 1); Root->SetWorldMatrix(moved);
    ASSERT_EQ(1u, material.TexGenEffect(Device, VXEFFECT_TGTRANSFORM, Root, 3));
    Coordinates(3, CKRST_TEXGEN_PASSTHRU, CKRST_TTF_COUNT2); XYZ(3, VxMatrix::Identity());
    EXPECT_FLOAT_EQ(1, State.Matrices[VXMATRIX_TEXTURE0 + 3][2][3]); UV(3, .25f, .75f, 0, .25f, .75f);
}
TEST_F(MaterialTexGenTest, ExplicitTransformUsesReferentialBasisAndItsTranslationInThirdRowWithoutMutatingEntity) {
    RCKMaterial material(Context); VxMatrix world = VxMatrix::Identity();
    world[0].Set(0, 2, 0, 0); world[1].Set(-3, 0, 0, 0); world[2].Set(0, 0, 5, 0); world[3].Set(7, 8, 9, 1);
    RCK3dEntity *reference = Entity(world); ASSERT_NE(nullptr, reference);
    ASSERT_EQ(1u, material.TexGenEffect(Device, VXEFFECT_TGTRANSFORM, reference, 1));
    VxMatrix expected = world; expected[2].Set(7, 8, 1, 1); XYZ(1, expected);
    EXPECT_FLOAT_EQ(1, State.Matrices[VXMATRIX_TEXTURE0 + 1][2][3]); Coordinates(1, CKRST_TEXGEN_PASSTHRU, CKRST_TTF_COUNT2);
    UV(1, .25f, .5f, 0, 5.5f, 8.5f);
    for (int row=0; row<4; ++row) for (int column=0; column<4; ++column) EXPECT_FLOAT_EQ(world[row][column], reference->GetWorldMatrix()[row][column]);
}
TEST_F(MaterialTexGenTest, RootPlanarProjectsUnitCoordinatesWithHalfScaleYFlipAndBias) {
    RCKMaterial material(Context); Root->SetWorldMatrix(RotationZ());
    ASSERT_EQ(1u, material.TexGenEffect(Device, VXEFFECT_TGPLANAR, Root, 2));
    Coordinates(2, CKRST_TEXGEN_CAMERASPACEPOSITION, CKRST_TTF_COUNT2);
    VxMatrix expected = VxMatrix::Identity(); expected[0][0] = .5f; expected[1][1] = -.5f; expected[2][2] = .5f; expected[3].Set(.5f,.5f,0,1);
    XYZ(2, expected); UV(2, 1, 1, 0, 1, 0); UV(2, -1, -1, 0, 0, 1);
}
TEST_F(MaterialTexGenTest, ExplicitPlanarComposesRootThenInverseReferentialBeforeProjection) {
    RCKMaterial material(Context); VxMatrix root = RotationZ(); root[3].Set(10,20,30,1); Root->SetWorldMatrix(root);
    VxMatrix ref = VxMatrix::Identity(); ref[0][0]=2; ref[1][1]=4; ref[2][2]=5; ref[3].Set(2,4,5,1);
    RCK3dEntity *reference = Entity(ref); ASSERT_NE(nullptr, reference);
    ASSERT_EQ(1u, material.TexGenEffect(Device, VXEFFECT_TGPLANAR, reference, 0));
    VxMatrix expected = VxMatrix::Identity(); expected[0].Set(0,-.125f,0,0); expected[1].Set(-.25f,0,0,0);
    expected[2].Set(0,0,.1f,0); expected[3].Set(2.5f,-1.5f,2.5f,1);
    XYZ(0, expected); UV(0,1,2,3,2,-1.625f); Coordinates(0, CKRST_TEXGEN_CAMERASPACEPOSITION, CKRST_TTF_COUNT2);
}
TEST_F(MaterialTexGenTest, RootReflectionUsesReflectionModeAndPointFourProjectionIndependentOfObjectAndViewMatrices) {
    RCKMaterial material(Context); Device->SetWorldTransformationMatrix(RotationZ()); Device->SetViewTransformationMatrix(RotationX());
    ASSERT_EQ(1u, material.TexGenEffect(Device, VXEFFECT_TGREFLECT, Root, 1));
    Coordinates(1, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR, CKRST_TTF_COUNT2);
    UV(1,1,0,0,.9f,.5f); UV(1,0,1,0,.5f,.1f); UV(1,0,0,1,.5f,.5f);
}
TEST_F(MaterialTexGenTest, RootChromeUsesNormalModeWithSameTwoDimensionalProjection) {
    RCKMaterial material(Context); ASSERT_EQ(1u, material.TexGenEffect(Device, VXEFFECT_TGCHROME, Root, 0));
    Coordinates(0, CKRST_TEXGEN_CAMERASPACENORMAL, CKRST_TTF_COUNT2); UV(0,1,0,0,.9f,.5f); UV(0,0,1,0,.5f,.1f);
}
TEST_F(MaterialTexGenTest, ExplicitReflectionBuildsLookBasisFromObjectPositionAndCombinesViewpointRotation) {
    RCKMaterial material(Context); Root->SetWorldMatrix(RotationZ()); Device->SetViewTransformationMatrix(RotationX());
    VxMatrix ref = VxMatrix::Identity(); ref[3].Set(0,0,-10,1); RCK3dEntity *reference = Entity(ref); ASSERT_NE(nullptr,reference);
    ASSERT_EQ(1u, material.TexGenEffect(Device,VXEFFECT_TGREFLECT,reference,2));
    VxMatrix expected=VxMatrix::Identity(); expected[0].Set(0,-.4f,0,0); expected[1].Set(-.4f,0,0,0);
    expected[2].Set(0,0,.4f,0); expected[3].Set(.5f,.5f,0,1); XYZ(2,expected);
    UV(2,1,0,0,.5f,.1f); UV(2,0,1,0,.1f,.5f); Coordinates(2,CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR,CKRST_TTF_COUNT2);
}
TEST_F(MaterialTexGenTest, ExplicitChromeLookDirectionUsesCurrentWorldPositionInsteadOfViewTranslation) {
    RCKMaterial material(Context); VxMatrix world=VxMatrix::Identity(); world[3].Set(10,0,0,1); Device->SetWorldTransformationMatrix(world);
    VxMatrix view=RotationZ(); view[3].Set(0,0,100,1); Device->SetViewTransformationMatrix(view);
    RCK3dEntity *reference=Entity(VxMatrix::Identity()); ASSERT_NE(nullptr,reference);
    ASSERT_EQ(1u,material.TexGenEffect(Device,VXEFFECT_TGCHROME,reference,0));
    VxMatrix expected=VxMatrix::Identity(); expected[0].Set(0,0,.4f,0); expected[1].Set(0,-.4f,0,0);
    expected[2].Set(-.4f,0,0,0); expected[3].Set(.5f,.5f,0,1); XYZ(0,expected); UV(0,0,0,1,.1f,.5f);
    Coordinates(0,CKRST_TEXGEN_CAMERASPACENORMAL,CKRST_TTF_COUNT2);
}
TEST_F(MaterialTexGenTest, CubeReflectionUsesRawReferentialRotationAndClearsTranslation) {
    RCKMaterial material(Context); VxMatrix ref=RotationZ(); ref[3].Set(7,8,9,1); RCK3dEntity *reference=Entity(ref); ASSERT_NE(nullptr,reference);
    Device->SetWorldTransformationMatrix(RotationX()); Device->SetViewTransformationMatrix(RotationX());
    ASSERT_EQ(1u,material.TexGenEffect(Device,VXEFFECT_TGCUBEMAP_REFLECT,reference,1));
    ref[3].Set(0,0,0,1); XYZ(1,ref); Coordinates(1,CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR,CKRST_TTF_COUNT3);
}
TEST_F(MaterialTexGenTest, CubeSkymapUsesRawReferentialRotationAndPositionGeneration) {
    RCKMaterial material(Context); VxMatrix ref=RotationZ(); ref[3].Set(7,8,9,1); RCK3dEntity *reference=Entity(ref); ASSERT_NE(nullptr,reference);
    ASSERT_EQ(1u,material.TexGenEffect(Device,VXEFFECT_TGCUBEMAP_SKYMAP,reference,2));
    ref[3].Set(0,0,0,1); XYZ(2,ref); Coordinates(2,CKRST_TEXGEN_CAMERASPACEPOSITION,CKRST_TTF_COUNT3);
}
TEST_F(MaterialTexGenTest, CubeNormalsWritesReferentialLinearTransformAndNormalGeneration) {
    RCKMaterial material(Context); VxMatrix ref=RotationZ(); ref[0][1]=2; ref[1][0]=-3; ref[2][2]=4; ref[3].Set(7,8,9,1);
    RCK3dEntity *reference=Entity(ref); ASSERT_NE(nullptr,reference);
    ASSERT_EQ(1u,material.TexGenEffect(Device,VXEFFECT_TGCUBEMAP_NORMALS,reference,3));
    ref[3].Set(0,0,0,1); XYZ(3,ref); Coordinates(3,CKRST_TEXGEN_CAMERASPACENORMAL,CKRST_TTF_COUNT3);
}
TEST_F(MaterialTexGenTest, CubePositionsComposesReferentialThenInverseObjectWorldAndIgnoresCameraView) {
    RCKMaterial material(Context); VxMatrix world=RotationZ(); world[3].Set(1,2,3,1); Device->SetWorldTransformationMatrix(world);
    Device->SetViewTransformationMatrix(RotationX()); VxMatrix ref=RotationX(); ref[3].Set(7,8,9,1);
    RCK3dEntity *reference=Entity(ref); ASSERT_NE(nullptr,reference);
    ASSERT_EQ(1u,material.TexGenEffect(Device,VXEFFECT_TGCUBEMAP_POSITIONS,reference,0));
    // Literal row-major product Rx * inverse(Rz), independent of engine matrix multiplication.
    VxMatrix expected=VxMatrix::Identity(); expected[0].Set(0,-1,0,0); expected[1].Set(0,0,1,0); expected[2].Set(-1,0,0,0);
    expected[3].Set(0,0,0,1); XYZ(0,expected); Coordinates(0,CKRST_TEXGEN_CAMERASPACEPOSITION,CKRST_TTF_COUNT3);
}
TEST_F(MaterialTexGenTest, GeneratedCoordinatesOnlyChangeSelectedStageAndLeaveLightingAndTextureBindingsUntouched) {
    RCKMaterial material(Context); State.Textures[0]=123; State.Textures[1]=124; State.States[VXRENDERSTATE_TEXTUREFACTOR]=0x12345678;
    State.StageStates[0][CKRST_TSS_TEXCOORDINDEX]=79; State.Matrices[VXMATRIX_TEXTURE0]=RotationX();
    ASSERT_EQ(1u,material.TexGenEffect(Device,VXEFFECT_TGREFLECT,Root,1));
    EXPECT_EQ(123u,State.Textures[0]); EXPECT_EQ(124u,State.Textures[1]); EXPECT_EQ(0x12345678u,State.States[VXRENDERSTATE_TEXTUREFACTOR]);
    EXPECT_EQ(79u,State.StageStates[0][CKRST_TSS_TEXCOORDINDEX]); XYZ(0,RotationX());
}

