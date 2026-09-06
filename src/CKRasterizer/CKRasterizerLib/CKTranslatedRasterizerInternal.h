#ifndef CKTRANSLATEDRASTERIZERINTERNAL_H
#define CKTRANSLATEDRASTERIZERINTERNAL_H

// Translation core of the CKRasterizer v3 contract (spec section 5).
//
// A CKTranslatedRasterizer composes a plugin's device factory and shader
// catalog and exposes them to the engine as the D3D7-shaped v3 contract. Every
// fixed-function state call is mirrored verbatim (so Get* returns exactly
// what was set) and forwarded to the fixed-function pipeline; draws go
// through CKFixedFunctionPipeline onto the CKRasterizerBackend; the frame
// flow selects logical targets; the backend owns native pass boundaries.

#include "CKTranslatedRasterizer.h"
#include "CKRasterizerPlugin.h"
#include "CKFixedFunctionPipeline.h"
#include "CKPresentStage.h"

#include <unordered_map>
#include <vector>

class CKTranslatedRasterizer;
class CKTranslatedDriver;
class CKTranslatedContext;

enum CKTranslatedFramePhase {
    CKTRANSLATED_FRAME_IDLE = 0,
    CKTRANSLATED_FRAME_SCENE,
    CKTRANSLATED_FRAME_OVERLAY
};

// All mutable frame-flow decisions live together. Phase is exclusive, so a
// context can no longer be both inside a scene and inside the overlay pass.
struct CKTranslatedFrameState {
    CKTranslatedFramePhase Phase;
    CKBOOL PassOpen;
    CKBOOL InternalTargets;
    CKBOOL Composited;
    CKBOOL TargetDecided;
    CKBOOL Open;
    CKBOOL NativePresented;

    CKTranslatedFrameState()
        : Phase(CKTRANSLATED_FRAME_IDLE), PassOpen(FALSE), InternalTargets(FALSE),
          Composited(FALSE), TargetDecided(FALSE), Open(FALSE), NativePresented(FALSE) {}

    CKBOOL IsSceneActive() const { return Phase == CKTRANSLATED_FRAME_SCENE ? TRUE : FALSE; }
    CKBOOL IsOverlayActive() const { return Phase == CKTRANSLATED_FRAME_OVERLAY ? TRUE : FALSE; }

    void FinishFrame()
    {
        Phase = CKTRANSLATED_FRAME_IDLE;
        PassOpen = FALSE;
        Composited = FALSE;
        TargetDecided = FALSE;
        Open = FALSE;
    }

    void Reset()
    {
        *this = CKTranslatedFrameState();
    }
};

// ===========================================================================
// CKTranslatedRasterizer
// ===========================================================================

class CKTranslatedRasterizer : public CKRasterizer {
public:
    // Takes ownership of `Library`. `CloseLibrary` (may be NULL) is called
    // with the library when the rasterizer is destroyed; NULL means `delete`.
    CKTranslatedRasterizer(CKRasterizerBackendLibrary *Library, CKTranslatedLibraryCloseFunction CloseLibrary);
    ~CKTranslatedRasterizer() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;

    CKRasterizerBackendLibrary *GetLibrary() const { return m_Library; }

private:
    CKRasterizerBackendLibrary *m_Library;
    CKTranslatedLibraryCloseFunction m_CloseLibrary;
};

// ===========================================================================
// CKTranslatedDriver
// ===========================================================================

class CKTranslatedDriver : public CKRasterizerDriver {
public:
    CKTranslatedDriver(CKTranslatedRasterizer *Owner, CKRasterizerBackendDriver *Backend, CKDWORD Index);
    ~CKTranslatedDriver() override;

    CKRasterizerContext *CreateContext() override;
    CKBOOL DestroyContext(CKRasterizerContext *Context) override;

    CKRasterizerBackendDriver *GetBackendDriver() const { return m_Backend; }
    // Copies caps, display modes and texture formats from the backend driver.
    // Backends refine their caps when a backend is created, so the context
    // calls this again after Create().
    void SyncCapsFromBackend();

private:
    CKRasterizerBackendDriver *m_Backend;
};

// ===========================================================================
// CKTranslatedContext
// ===========================================================================

class CKTranslatedContext : public CKRasterizerContext {
public:
    CKTranslatedContext(CKTranslatedDriver *Driver, CKRasterizerBackend *Backend);
    ~CKTranslatedContext() override;

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
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override;
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override;
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override;
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

    // --- Internal access ---
    CKRasterizerBackend *GetBackend() const { return m_Backend; }

    // --- Test access (the translated tests read the pipeline and the mirror) ---
    CKFixedFunctionPipeline *GetFFPipelineForTests() { return &m_FFP; }
    CKDWORD GetTargetForTests() const { return m_Target; }
    CKBOOL IsInSceneForTests() const { return m_Frame.IsSceneActive(); }
    CKBOOL IsOverlayPhaseForTests() const { return m_Frame.IsOverlayActive(); }
    CKDWORD GetPassCountForTests() const { return m_FramePasses; }
    int GetLiveResourceCountForTests(CKDWORD TypeMask) const;
    const CKLightData &GetLightForTests(CKDWORD Index) const { return m_Lights[Index]; }
    CKBOOL IsLightEnabledForTests(CKDWORD Index) const { return m_LightEnabled[Index]; }
    const CKMaterialData &GetMaterialForTests() const { return m_Material; }
    const CKViewportData &GetViewportForTests() const { return m_Viewport; }
    const CKRasterizerOptions &GetOptionsForTests() const { return m_Options; }
    CKBOOL GetVertexBufferDescForTests(CKDWORD VB, CKVertexBufferDesc *Desc) const;

private:
    struct Resource {
        CKDWORD Type;                      // CKRST_OBJ_*
        CKDWORD Handle;                    // backend handle (== contract handle)
        CKTextureDesc Texture;
        CKVertexBufferDesc VertexBuffer;
        CKIndexBufferDesc IndexBuffer;
        CKRSTVertexLayout Layout;          // canonical layout the engine writes
        CKDWORD FormatFlags;               // fixed-function layout the backend sees
        CKDWORD DeviceStride;
        CKDWORD DeviceLayout;
        std::vector<CKBYTE> Shadow;        // Lock storage (canonical layout / indices)
        std::vector<CKBYTE> Scratch;       // interleave target for Unlock
        CKBOOL Locked;
        CKDWORD LockStart;
        CKDWORD LockCount;

        Resource()
            : Type(0), Handle(0), FormatFlags(0), DeviceStride(0), DeviceLayout(0),
              Locked(FALSE), LockStart(0), LockCount(0) {
            memset(&Layout, 0, sizeof(Layout));
        }
    };

    // Snapshot of the current target at RequestReadback, completed by an
    // owned backend ticket and delivered at a frame boundary.
    struct PendingReadback {
        CKReadbackCallback Callback;
        void *User;
        CKRECT Rect;
        CKBOOL HasRect;
        VXBUFFER_TYPE Buffer;
        CKBOOL Done;
        CKBOOL Success;
        CKDWORD Width;
        CKDWORD Height;
        CKDWORD Pitch;
        VX_PIXELFORMAT Format;
        CKBOOL YFlip;
        std::vector<CKBYTE> Data;
        CKBackendReadbackTicket Ticket;

        PendingReadback()
            : Callback(NULL), User(NULL), HasRect(FALSE), Buffer(VXBUFFER_BACKBUFFER), Done(FALSE),
              Success(FALSE), Width(0), Height(0), Pitch(0), Format(UNKNOWN_PF), YFlip(FALSE),
              Ticket() {
            Rect.left = Rect.top = Rect.right = Rect.bottom = 0;
        }
    };

    static uint64_t ResourceKey(CKDWORD Type, CKDWORD Handle) { return ((uint64_t)Type << 32) | Handle; }
    Resource *FindResource(CKDWORD Type, CKDWORD Handle);
    const Resource *FindResource(CKDWORD Type, CKDWORD Handle) const;
    void Diag(CKRST_DIAGNOSTIC Kind) { ++m_Stats.Diagnostics[Kind]; }
    CKRST_DIAGNOSTIC DrawRejectDiagnostic() const;
    void RecordDrawApproximations();

    // Frame flow
    CKBOOL PrepareFrameTarget();
    CKBOOL OpenPass(CKDWORD RenderTarget, const CKRECT &Rect, CKDWORD ClearFlags, CKDWORD Color,
                    float Z, CKDWORD Stencil, const char *Name);
    CKBOOL EnsureDrawPass();
    CKRECT CurrentTargetRect() const;
    CKRECT WindowRect() const;
    // Engine coordinates (spec 4.4): the logical target is the window or the
    // target texture; the physical pass rect may be scaled by RenderScale.
    CKRECT LogicalTargetRect() const;
    CKRECT ScaleToPhysical(const CKRECT &Rect) const;
    void UpdateTargetExtents();
    CKDWORD CurrentSceneFrameBuffer() const;
    CKDWORD OverlayFrameBuffer() const;
    CKDWORD CurrentPassFrameBuffer() const;
    CKRECT CurrentPassRect() const;
    CKBOOL CompositeScene();
    CKBOOL PresentInternalTarget();
    void FinishFrame();
    void ApplyOptions();
    void ResetStateMirror();
    void ReleaseFrameScratch();
    void ReleaseTarget();
    void UpdateAlphaTestPrecision();

    // Draw helpers
    CKBOOL ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL CheckDeviceForDraw();
    void CountDraw(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL SubmitVertexBuffer(VXPRIMITIVETYPE Type, const Resource &VB, CKDWORD IBHandle, CKDWORD BaseVertex,
                              CKDWORD VertexCount, CKDWORD StartIndex, CKDWORD IndexCount);

    // Readback helpers
    CKBOOL BuildReadbackImage(const PendingReadback &Readback, VxImageDescEx &Desc, std::vector<CKBYTE> &Pixels) const;
    // The swapchain is never read. Capture the current scene/overlay or bound
    // 2D target at the call position; backend tickets retain their own storage.
    CKBOOL CanReadNativeTarget();
    CKBOOL CanReadTargetTexture();
    CKBOOL CanReadCurrentTarget();
    CKBOOL ResolveCopySource();
    CKBOOL BlitForReadback();
    CKBOOL IssueTextureReadback(PendingReadback &Readback);
    CKBOOL CaptureReadback(PendingReadback &Readback);
    // Frame without scene content: re-presents the native target so the window
    // keeps its image while waiting, and/or blits the readback source.
    CKBOOL SubmitReadbackFrame(CKBOOL Present, CKBOOL Blit, CKDWORD *FrameNumber);
    void DeliverReadbacks();
    void CancelReadbacks();
    CKBOOL CompleteReadback(PendingReadback &Readback, CKBOOL Wait);
    CKBOOL ValidateRect(const CKRECT *Rect, CKDWORD Width, CKDWORD Height) const;

    CKTranslatedDriver *m_TranslatedDriver;
    CKRasterizerBackend *m_Backend;    // owned by the backend driver
    CKFixedFunctionPipeline m_FFP;
    CKPresentStage m_Present;
    CKRasterizerOptions m_Options;

    // Verbatim state mirror (spec 4.10)
    CKDWORD m_RenderStates[VXRENDERSTATE_MAXSTATE];
    CKDWORD m_StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE];
    // Defaults in the public mirror can differ from the FFP defaults until
    // explicitly applied. Only synchronized entries can skip repeated writes.
    uint64_t m_AppliedStageStates[CKRST_MAX_TEXTURE_STAGES]{};
    CKDWORD m_Textures[CKRST_MAX_TEXTURE_STAGES];
    VxMatrix m_Matrices[CKRST_MATRIX_SLOT_COUNT];
    CKLightData m_Lights[CKRST_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKRST_MAX_LIGHTS];
    CKMaterialData m_Material;
    CKViewportData m_Viewport;
    VxPlane m_ClipPlanes[CKRST_MAX_USER_CLIP_PLANES];

    // Frame
    CKBOOL m_Created;
    CKBOOL m_ShuttingDown;
    CKTranslatedFrameState m_Frame;
    CKDWORD m_FrameNumber;
    CKDWORD m_LastDeviceFrame;     // backend frame counter after the last Present()
    XString m_Marker;

    // Target
    CKDWORD m_Target;
    CKRST_CUBEFACE m_TargetFace;
    CKDWORD m_TargetWidth;
    CKDWORD m_TargetHeight;
    CKDWORD m_TargetFrameBuffer;
    CKDWORD m_TargetDepthTexture;

    // Resources
    std::unordered_map<uint64_t, Resource> m_Resources;
    std::vector<CKDWORD> m_FrameIndexBuffers;   // per-draw index uploads, deleted after Frame()
    CKDWORD m_CopyTexture;
    CKDWORD m_CopyWidth;
    CKDWORD m_CopyHeight;

    // Readback
    std::vector<PendingReadback *> m_Readbacks;

    // Stats
    CKRenderStats m_Stats;
    CKDWORD m_FrameDrawCalls;
    CKDWORD m_FramePrimitives;
    CKDWORD m_FramePasses;
    CKDWORD m_FrameClears;
    CKDWORD m_FrameTextureUploads;
    CKDWORD m_FrameBufferUploads;
    CKBOOL m_LayoutMismatchLogged;
};

#endif // CKTRANSLATEDRASTERIZERINTERNAL_H
