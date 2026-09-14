#ifndef CKRE_FFP_RECORDING_CONTEXT_H
#define CKRE_FFP_RECORDING_CONTEXT_H

// Test-owned CKRasterizerContext used by white-box and fault-injection tests.
// It is not linked into a production rasterizer or installed.

#include "CKRecordingBackend.h"
#include "CKFFBufferData.h"
#include "CKFFBufferUseTracker.h"
#include "CKFFContextState.h"
#include "CKFFResourceStore.h"
#include "CKFFTestPipeline.h"
#include "CKRasterizer.h"
#include "CKFixedFunctionPipeline.h"
#include "FFPRecordingPresentStage.h"

#include <vector>

class FFPRecordingContext;

typedef void (*FFPRecordingReadyFunction)(void *user, CKRecordingBackend *backend);

struct FFPRecordingContextDesc {
    CKRasterizerDriver *Driver;
    CKRecordingBackend *Backend;
    const CKFFShaderLibrary *Shaders;
    FFPRecordingReadyFunction BackendReady;
    void *BackendReadyUser;

    FFPRecordingContextDesc()
        : Driver(NULL), Backend(NULL), Shaders(NULL), BackendReady(NULL),
          BackendReadyUser(NULL) {}
};

// ===========================================================================
// FFPRecordingContext
// ===========================================================================

class FFPRecordingContext final : public CKRasterizerContext {
public:
    explicit FFPRecordingContext(const FFPRecordingContextDesc &Desc);
    ~FFPRecordingContext() override;

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
    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override;
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override;
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override;
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override;
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override;
    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override;
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override;
    CKBOOL SetMaterial(const CKMaterialData *Data) override;
    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override;
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
    CKBOOL GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const override;
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override;
    CKBOOL GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const override;
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override;
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override;
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override;
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override;
    CKBOOL DeleteObject(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type) override;
    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override;
    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override;

    // --- Targets, readback, copies ---
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override;
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override;
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override;
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override;
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer,
                           CKReadbackCallback Callback, void *User) override;

    // --- Internal access ---
    CKRecordingBackend *GetBackend() const { return m_Backend; }

    // --- Test access ---
    CKFixedFunctionPipeline *GetFFPipelineForTests() { return &m_FFP; }
    CKDWORD GetTargetForTests() const { return m_Target.Texture; }
    CKBOOL IsInSceneForTests() const { return m_Frame.IsSceneActive(); }
    CKBOOL IsOverlayPhaseForTests() const { return m_Frame.IsOverlayActive(); }
    CKDWORD GetPassCountForTests() const { return m_FramePasses; }
    int GetLiveResourceCountForTests(CKDWORD TypeMask) const;
    const CKLightData &GetLightForTests(CKDWORD Index) const { return m_FFP.GetLight((int)Index); }
    CKBOOL IsLightEnabledForTests(CKDWORD Index) const { return m_FFP.IsLightEnabled((int)Index); }
    const CKMaterialData &GetMaterialForTests() const { return m_FFP.GetMaterial(); }
    const CKViewportData &GetViewportForTests() const { return m_FFP.GetViewport(); }
    const CKRasterizerOptions &GetOptionsForTests() const { return m_Options; }

private:
    typedef CKFFVertexBufferData VertexBufferData;
    typedef CKFFIndexBufferData IndexBufferData;

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
        CKRecordingReadbackTicket Ticket;

        PendingReadback()
            : Callback(NULL), User(NULL), HasRect(FALSE), Buffer(VXBUFFER_BACKBUFFER), Done(FALSE),
              Success(FALSE), Width(0), Height(0), Pitch(0), Format(UNKNOWN_PF), YFlip(FALSE),
              Ticket() {
            Rect.left = Rect.top = Rect.right = Rect.bottom = 0;
        }
    };

    CKDWORD GetNativeVertexLayout(CKDWORD FormatFlags);
    void Diag(CKRST_DIAGNOSTIC Kind) { ++m_Stats.Diagnostics[Kind]; }
    CKRST_DIAGNOSTIC DrawRejectDiagnostic() const;
    void RecordDrawApproximations();

    // Frame flow
    CKBOOL PrepareFrameTarget();
    CKBOOL OpenPass(CKDWORD RenderTarget, const CKRECT &Rect, CKDWORD ClearFlags, CKDWORD Color,
                    float Z, CKDWORD Stencil, const char *Name);
    CKBOOL CanContinuePass(CKDWORD RenderTarget, const CKRECT &Rect) const;
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
    CKBOOL PresentInternalTarget(CKPresentSync Sync);
    void FinishFrame();
    void ApplyOptions();
    void ReleaseFrameScratch();
    CKBOOL ReleaseTarget();
    void UpdateAlphaTestPrecision();

    // Draw helpers
    CKBOOL ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL CheckDeviceForDraw();
    void CountDraw(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL SubmitVertexBuffer(VXPRIMITIVETYPE Type, CKDWORD VBHandle,
                              const CKFFVertexBufferData &VB,
                              CKDWORD IBHandle, CKDWORD BaseVertex,
                              CKDWORD VertexCount, CKDWORD StartIndex, CKDWORD IndexCount,
                              const CKWORD *Indices = NULL);

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

    CKFFShaderLibrary m_ShaderLibrary;
    FFPRecordingReadyFunction m_BackendReady;
    void *m_BackendReadyUser;
    CKRecordingBackend *m_Backend;    // owned by the concrete rasterizer driver
    CKFFTestPipeline m_FFP;
    FFPRecordingPresentStage m_Present;
    // Verbatim state mirror (spec 4.10)

    // Frame
    CKFFFrameState m_Frame;
    CKDWORD m_PassTarget;
    CKDWORD m_LastDeviceFrame;     // backend frame counter after the last Present()

    // Target
    CKFFRenderTargetState m_Target;
    CKDWORD m_TargetFrameBuffer;
    CKDWORD m_TargetDepthTexture;

    // Resources
    CKFFResourceStore m_PublicResources;
    CKFFBufferUseTracker m_BufferUses;
    CKDWORD m_CopyTexture;
    CKDWORD m_CopyWidth;
    CKDWORD m_CopyHeight;

    // Readback
    std::vector<PendingReadback *> m_Readbacks;

    // Stats
    CKDWORD m_FrameDrawCalls;
    CKDWORD m_FramePrimitives;
    CKDWORD m_FramePasses;
    CKDWORD m_FrameClears;
    CKDWORD m_FrameTextureUploads;
    CKDWORD m_FrameBufferUploads;
    CKBOOL m_LayoutMismatchLogged;
};

#endif // CKRE_FFP_RECORDING_CONTEXT_H
