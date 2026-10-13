#ifndef MATERIAL_RENDER_OBSERVER_H
#define MATERIAL_RENDER_OBSERVER_H
#include <gtest/gtest.h>
#include "CKRasterizer.h"
// Test-owned interface observer. Does not implement shading, uploads or drawing.
class MaterialRenderObserver : public CKRasterizerContext {
public:
    CKDWORD States[1024] = {};
    CKMaterialData Material = {};
    CKMaterialRenderState Applied = {};
    int MaterialWrites = 0, ApplyWrites = 0, Resets = 0;
    VxMatrix World = VxMatrix::Identity();
    CKViewportData Viewport = {};
    int WorldWrites = 0, StateWrites = 0, PresentCount = 0;
    CKDWORD Textures[CKRST_MAX_TEXTURE_STAGES] = {};
    CKDWORD StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE] = {};
    VxMatrix Matrices[32];
    MaterialRenderObserver() {
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
    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override { ++Resets; for (int stage = FirstStage; stage < FirstStage + StageCount; ++stage) { Textures[stage] = 0; for (int state = 0; state < CKRST_TSS_MAXSTATE; ++state) StageStates[stage][state] = 0; Matrices[VXMATRIX_TEXTURE0 + stage].SetIdentity(); } return TRUE; }
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override { Textures[Stage] = Texture; return TRUE; }
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override { *Texture = Textures[Stage]; return TRUE; }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override { Matrices[(unsigned int)Type] = Mat; if (Type == VXMATRIX_WORLD) { World = Mat; ++WorldWrites; } return TRUE; }
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override { Mat = Matrices[(unsigned int)Type]; return TRUE; }
    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override { ADD_FAILURE() << "Unexpected rasterizer call: SetLight"; return FALSE; }
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override { ADD_FAILURE() << "Unexpected rasterizer call: EnableLight"; return FALSE; }
    CKBOOL SetMaterial(const CKMaterialData *Data) override { Material = *Data; ++MaterialWrites; return TRUE; }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override { Applied = State; ++ApplyWrites; return TRUE; }
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

#endif
