#ifndef CKNULLRASTERIZER_H
#define CKNULLRASTERIZER_H

// NULL implementation of the CKRasterizer v3 contract.
//
// It stores every piece of fixed-function state, owns real CPU memory for
// vertex and index buffers, validates handles and parameters exactly as the
// contract prescribes, and records an event log that the contract tests
// inspect (draw order, pass splitting, target changes). It draws nothing.
//
// Phase 1 replaces this stand-alone stub with "translation core + NULL
// device"; the contract tests keep passing unchanged.

#include "CKRasterizer.h"

// ===========================================================================
// Event log
// ===========================================================================

enum CKNullEventKind {
    CKNULL_EVENT_CREATE = 0,
    CKNULL_EVENT_RESIZE,
    CKNULL_EVENT_PASS_BEGIN,      // A = pass index, B = target handle, C = face, Name = reason
    CKNULL_EVENT_PASS_END,        // A = pass index
    CKNULL_EVENT_CLEAR,           // A = flags, B = color, C = stencil, F = z, Rect = area (or empty)
    CKNULL_EVENT_BEGIN_SCENE,
    CKNULL_EVENT_END_SCENE,
    CKNULL_EVENT_BEGIN_OVERLAY,
    CKNULL_EVENT_PRESENT,         // A = vsync, B = frame number
    CKNULL_EVENT_DRAW,            // A = primitive type, B = primitive count, C = VB, D = IB
    CKNULL_EVENT_SET_TARGET,      // A = texture handle, B = width, C = height, D = face
    CKNULL_EVENT_COPY_TO_MEMORY,  // A = buffer type, B = bytes
    CKNULL_EVENT_COPY_FROM_MEMORY,
    CKNULL_EVENT_COPY_TO_TEXTURE, // A = texture handle, D = face
    CKNULL_EVENT_MARKER,          // Name = marker
    CKNULL_EVENT_OPTIONS,
};

struct CKNullEvent {
    CKNullEventKind Kind;
    CKDWORD Pass;                 // Pass index the event belongs to
    CKDWORD A, B, C, D;
    float F;
    CKRECT Rect;
    XString Name;
    // Draw snapshot (only for CKNULL_EVENT_DRAW)
    CKDWORD Textures[CKRST_MAX_TEXTURE_STAGES];
    CKDWORD RenderStateHash;      // FNV-1a of the render state array at draw time
    CKDWORD StageStateHash;       // FNV-1a of the stage state array at draw time
    CKDWORD Target;               // Target handle at draw time

    CKNullEvent()
        : Kind(CKNULL_EVENT_CREATE), Pass(0), A(0), B(0), C(0), D(0), F(0.0f),
          RenderStateHash(0), StageStateHash(0), Target(0)
    {
        Rect.left = Rect.top = Rect.right = Rect.bottom = 0;
        for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
            Textures[i] = 0;
    }
};

// ===========================================================================
// Resources
// ===========================================================================

struct CKNullResource {
    CKDWORD Type;                 // CKRST_OBJ_* or 0 when the slot is free
    CKDWORD Generation;
    XString Name;
    // Texture
    CKTextureDesc Texture;
    CKDWORD LoadedLevelMask;      // bit per mip level uploaded (face 0 for cubes)
    // Buffers
    CKVertexBufferDesc VertexBuffer;
    CKIndexBufferDesc IndexBuffer;
    XArray<CKBYTE> Storage;
    CKBOOL Locked;
    CKDWORD LockStart;
    CKDWORD LockCount;

    CKNullResource()
        : Type(0), Generation(0), LoadedLevelMask(0), Locked(FALSE), LockStart(0), LockCount(0) {}
};

// ===========================================================================
// Classes
// ===========================================================================

class CKNullRasterizerContext;

class CKNullRasterizer : public CKRasterizer {
public:
    CKNullRasterizer();
    ~CKNullRasterizer() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
};

class CKNullRasterizerDriver : public CKRasterizerDriver {
public:
    CKNullRasterizerDriver();
    ~CKNullRasterizerDriver() override;

    CKRasterizerContext *CreateContext() override;
    CKBOOL DestroyContext(CKRasterizerContext *Context) override;

    void InitCaps(CKRasterizer *Owner, CKDWORD Index);
};

class CKNullRasterizerContext : public CKRasterizerContext {
public:
    CKNullRasterizerContext();
    ~CKNullRasterizerContext() override;

    // --- Lifecycle ---
    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height,
                  int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) override;
    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) override;
    CKBOOL SetOptions(const CKRasterizerOptions *Options) override;
    CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const override;
    CKERROR GetDeviceStatus() const override;
    CKBOOL BeginShutdown() override;
    CKBOOL IsIdle() const override;

    // --- Frame ---
    CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil,
                 int RectCount, CKRECT *Rects) override;
    CKBOOL BeginScene() override;
    CKBOOL EndScene() override;
    CKBOOL BeginOverlayPhase() override;
    CKBOOL BackToFront(CKBOOL VSync) override;

    // --- State ---
    CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) override;
    CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) override;
    CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) override;
    CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) override;
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override;
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override;
    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override;
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override;
    CKBOOL SetMaterial(const CKMaterialData *Data) override;
    CKBOOL SetViewport(const CKViewportData *Data) override;
    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override;
    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override;
    void InitDefaultRenderStatesValue() override;

    // --- Draw ---
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount,
                         VxDrawPrimitiveData *Data) override;
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex,
                           CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override;
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB,
                             CKDWORD MinVertexIndex, CKDWORD VertexCount,
                             CKDWORD StartIndex, int IndexCount) override;

    // --- Resources ---
    CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) override;
    CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel,
                       CKRST_CUBEFACE Face, const CKRECT *Region) override;
    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override;
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override;
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override;
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override;
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override;
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override;
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override;
    CKBOOL DeleteObject(CKDWORD Handle, CKDWORD Type) override;
    CKBOOL FlushObjects(CKDWORD TypeMask) override;
    void SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name) override;

    // --- Targets, readback, copies ---
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override;
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override;
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override;
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override;
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer,
                           CKReadbackCallback Callback, void *User) override;

    // --- Diagnostics ---
    void SetDebugMarker(CKSTRING Name) override;
    const CKRenderStats *GetStats() override;

    // --- Test access ---
    const XClassArray<CKNullEvent> &GetEvents() const { return m_Events; }
    void ClearEvents() { m_Events.Clear(); }
    CKDWORD GetPassIndex() const { return m_PassIndex; }
    CKBOOL IsInScene() const { return m_InScene; }
    CKDWORD GetTarget() const { return m_Target; }
    const VxMatrix &GetMatrix(VXMATRIX_TYPE Type) const;
    const CKLightData &GetLight(CKDWORD Index) const { return m_Lights[Index]; }
    CKBOOL IsLightEnabled(CKDWORD Index) const { return m_LightEnabled[Index]; }
    const CKMaterialData &GetMaterial() const { return m_Material; }
    const CKViewportData &GetViewport() const { return m_Viewport; }
    CKDWORD GetBoundTexture(int Stage) const { return m_Textures[Stage]; }
    const CKRasterizerOptions &GetOptions() const { return m_Options; }
    const CKNullResource *GetResource(CKDWORD Handle) const;
    int GetLiveResourceCount(CKDWORD TypeMask) const;

private:
    CKNullResource *FindResource(CKDWORD Handle, CKDWORD Type);
    const CKNullResource *FindResource(CKDWORD Handle, CKDWORD Type) const;
    CKDWORD AllocateResource(CKDWORD Type);
    void ReleaseResource(CKDWORD Handle);
    void Diag(CKRST_DIAGNOSTIC Kind) { ++m_Stats.Diagnostics[Kind]; }
    CKNullEvent &PushEvent(CKNullEventKind Kind);
    void BeginPass(const char *Reason);
    void EndPass();
    CKBOOL RecordDraw(VXPRIMITIVETYPE Type, int ElementCount, CKDWORD VB, CKDWORD IB);
    CKBOOL ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount) const;
    CKDWORD HashRenderStates() const;
    CKDWORD HashStageStates() const;
    void ResetFrameStats();

    // Fixed-function state
    CKDWORD m_RenderStates[VXRENDERSTATE_MAXSTATE];
    CKDWORD m_StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE];
    CKDWORD m_Textures[CKRST_MAX_TEXTURE_STAGES];
    VxMatrix m_Matrices[CKRST_MATRIX_SLOT_COUNT];
    CKLightData m_Lights[CKRST_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKRST_MAX_LIGHTS];
    CKMaterialData m_Material;
    CKViewportData m_Viewport;
    VxPlane m_ClipPlanes[CKRST_MAX_USER_CLIP_PLANES];

    // Frame state
    CKBOOL m_Created;
    CKBOOL m_ShuttingDown;
    CKBOOL m_InScene;
    CKBOOL m_OverlayPhase;
    CKBOOL m_PassOpen;
    CKDWORD m_PassIndex;
    CKDWORD m_Target;
    CKRST_CUBEFACE m_TargetFace;
    CKDWORD m_TargetWidth;
    CKDWORD m_TargetHeight;
    CKRasterizerOptions m_Options;
    XString m_Marker;

    // Resources
    XClassArray<CKNullResource> m_Resources;
    XArray<CKDWORD> m_FreeSlots;

    // Diagnostics
    CKRenderStats m_Stats;
    CKDWORD m_FrameDrawCalls;
    CKDWORD m_FramePrimitives;
    CKDWORD m_FramePasses;
    CKDWORD m_FrameClears;
    CKDWORD m_FrameTextureUploads;
    CKDWORD m_FrameBufferUploads;
    XClassArray<CKNullEvent> m_Events;
    VxMatrix m_Identity;
};

// ===========================================================================
// Entry points (CK_LIB static builds link these directly)
// ===========================================================================

CKRasterizer *CKNullRasterizerStart(WIN_HANDLE AppWnd);
void CKNullRasterizerClose(CKRasterizer *Rasterizer);
void CKNullRasterizerGetInfo(CKRasterizerInfo *Info);

#endif // CKNULLRASTERIZER_H
