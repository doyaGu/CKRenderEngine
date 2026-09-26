#ifndef CKBGFXRASTERIZERCONTEXT_H
#define CKBGFXRASTERIZERCONTEXT_H

// Concrete bgfx CKRasterizerContext. The public Context, fixed-function state,
// and bgfx implementation share one owner.

#include "CKFFProgramDesc.h"
#include "CKBgfxBorderPalette.h"
#include "CKBuiltinShaders.h"
#include "CKRasterizer.h"
#include "CKFixedFunctionPipeline.h"
#include "CKFFBufferUseTracker.h"
#include "CKFFBufferData.h"
#include "CKFFContextState.h"
#include "CKFFResourceStore.h"
#include "CKFFShaderCache.h"
#include "CKBgfxPresentStage.h"
#include "XSHashTable.h"
#include "XClassArray.h"

#include <atomic>
#include <memory>
#include <string.h>
#include <bgfx/bgfx.h>

class CKBgfxRasterizerDriver;
class CKBgfxRasterizerContext;
class CKBgfxResources;
struct CKBgfxShaderRecord;
struct CKBgfxProgramRecord;
struct CKBgfxVertexLayoutRecord;
struct CKBgfxVertexBufferRecord;
struct CKBgfxIndexBufferRecord;
struct CKBgfxTextureRecord;
struct CKBgfxFrameBufferRecord;
enum CKBgfxTextureOrientation : int;

struct CKBgfxDeviceLimits {
    CKRST_DEVCAPS Features;
    CKDWORD MaxDrawCalls;
    CKDWORD MaxBlits;
    CKDWORD MaxTextureSize;
    CKDWORD MaxTextureLayers;
    CKDWORD MaxRenderViews;
    CKDWORD MaxFrameBuffers;
    CKDWORD MaxColorAttachments;
    CKDWORD MaxPrograms;
    CKDWORD MaxShaders;
    CKDWORD MaxTextures;
    CKDWORD MaxTextureBindings;
    CKDWORD MaxVertexLayouts;
    CKDWORD MaxVertexStreams;
    CKDWORD MaxIndexBuffers;
    CKDWORD MaxVertexBuffers;
    CKDWORD MaxDynamicIndexBuffers;
    CKDWORD MaxDynamicVertexBuffers;
    CKDWORD MaxUniforms;
    CKDWORD MaxTransientVertexBufferSize;
    CKDWORD MaxTransientIndexBufferSize;

    CKBgfxDeviceLimits()
        : Features(0), MaxDrawCalls(0), MaxBlits(0), MaxTextureSize(0),
          MaxTextureLayers(0), MaxRenderViews(0), MaxFrameBuffers(0),
          MaxColorAttachments(0), MaxPrograms(0), MaxShaders(0),
          MaxTextures(0), MaxTextureBindings(0), MaxVertexLayouts(0),
          MaxVertexStreams(0), MaxIndexBuffers(0), MaxVertexBuffers(0),
          MaxDynamicIndexBuffers(0), MaxDynamicVertexBuffers(0),
          MaxUniforms(0), MaxTransientVertexBufferSize(0),
          MaxTransientIndexBufferSize(0) {}
};

struct CKBgfxTextureFormatCaps {
    VX_PIXELFORMAT Format;
    CKDWORD Caps;

    CKBgfxTextureFormatCaps() : Format(UNKNOWN_PF), Caps(0) {}
};

#define CKBGFX_DRAWMAP_SOURCE_COUNT 6
#define CKBGFX_DRAWMAP_HASH_INIT 2166136261u
#define CKBGFX_MAX_FRAME_LATENCY 2u

struct CKBgfxDrawMapVertexBinding {
    CKDWORD Buffer;
    CKDWORD Start;
    CKDWORD Count;
    CKDWORD BgfxHandle;
    CKDWORD LayoutHandle;
};

struct CKBgfxDrawMapTextureBinding {
    CKDWORD Texture;
    CKDWORD Uniform;
    CKDWORD BgfxHandle;
    CKDWORD SamplerFlags;
};

struct CKBgfxSubmittedFrame {
    CKDWORD FrameNumber;
    CKQWORD SubmitId;

    CKBgfxSubmittedFrame(CKDWORD Frame = 0, CKQWORD Submit = 0)
        : FrameNumber(Frame), SubmitId(Submit) {}
};

class CKBgfxCallback : public bgfx::CallbackI {
public:
    CKBgfxCallback() : m_Context(NULL) {}

    void SetContext(CKBgfxRasterizerContext *Context) { m_Context = Context; }

    void fatal(const char *FilePath, uint16_t Line, bgfx::Fatal::Enum Code,
               const char *Text) override;
    void traceVargs(const char *FilePath, uint16_t Line, const char *Format,
                    va_list Arguments) override;
    void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t Id) override;
    bool cacheRead(uint64_t Id, void *Data, uint32_t Size) override;
    void cacheWrite(uint64_t Id, const void *Data, uint32_t Size) override;
    void screenShot(const char *FilePath, uint32_t Width, uint32_t Height,
                    uint32_t Pitch, bgfx::TextureFormat::Enum Format,
                    const void *Data, uint32_t Size, bool YFlip) override;
    void captureBegin(uint32_t, uint32_t, uint32_t,
                      bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void *, uint32_t) override {}

private:
    CKBgfxRasterizerContext *m_Context;
    VxMutex m_CacheMutex;
};

// ===========================================================================
// CKBgfxRasterizerContext
// ===========================================================================

class CKBgfxRasterizerContext final : public CKRasterizerContext {
    struct NativeReadback;
    typedef std::shared_ptr<NativeReadback> NativeReadbackTicket;
    struct DefaultTextureHash {
        int operator()(const uint64_t &Key) const
        {
            return (int)((uint32_t)Key ^ (uint32_t)(Key >> 32));
        }
    };
    typedef XSHashTable<std::weak_ptr<bgfx::TextureHandle>, uint64_t,
                        DefaultTextureHash> DefaultTextureTable;

public:
    using CKRasterizerContext::BackToFront;

    CKBgfxRasterizerContext();
    CKBgfxRasterizerContext(CKBgfxRasterizerDriver *Driver,
                            const CKFFShaderLibrary *Shaders);
    ~CKBgfxRasterizerContext() override;

    // Native bgfx operations. They are methods of the concrete Context so
    // resource ownership, synchronization, and submission stay local.
    CKERROR Init(const CKRasterizerInitParameters *Desc);
    void Shutdown();
    CKERROR Resize(int PosX, int PosY, int Width, int Height);
    const CKRasterizerDeviceCaps &GetCaps() const { return m_Caps; }
    void SetDebugFlags(CKDWORD Flags);

    CKERROR CreateTexture(const CKTextureDesc *Desc,
                          const VxImageDescEx *Data, CKDWORD *Out);
    CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face,
                          const CKRECT *Region, const VxImageDescEx *Data);
    CKERROR CreateDepthTexture(const CKDepthTextureDesc *Desc, CKDWORD *Out);
    CKERROR CreateRenderTarget(const CKRenderTargetDesc *Desc,
                               CKDWORD *Out);
    CKERROR CreateBuffer(const CKBufferDesc *Desc, CKDWORD *Out);
    CKERROR UpdateBuffer(const CKBufferUpdateDesc *Desc);
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out);
    CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out);
    CKERROR CreateProgram(const CKFFProgramDesc *Desc, CKDWORD *Out);
    CKBOOL IsNativeObjectAlive(CKDWORD Object, CKDWORD Type) const;
    CKERROR DestroyObject(CKDWORD Object, CKDWORD Type);
    CKERROR SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name);

    CKERROR BeginPass(const CKRenderPassDesc *Desc);
    CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout,
                                  CKTransientVertexData *Out);
    CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32,
                                 CKTransientIndexData *Out);
    CKERROR Draw(const CKDrawCommand *Draw);
    CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer,
                 CKDWORD DstX, CKDWORD DstY, CKDWORD SrcTexture,
                 CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect);
    CKERROR PresentTexture(CKDWORD Texture, CKDWORD Width, CKDWORD Height,
                           CKPresentSync Sync)
    {
        (void)Texture;
        (void)Width;
        (void)Height;
        (void)Sync;
        return CKERR_NOTIMPLEMENTED;
    }
    CKERROR Submit(CKPresentSync Sync, CKBOOL PresentWindow, CKDWORD *FrameNumber);
    CKQWORD GetLastSubmitId() const { return m_LastSubmitId; }
    CKQWORD GetCompletedSubmitId();

    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip,
                        CKReadbackDesc *Readback,
                        NativeReadbackTicket *Ticket);
    CKReadbackState PollReadback(
        const NativeReadbackTicket &Ticket, CKBOOL Wait)
    {
        (void)Wait;
        if (!Ticket || Ticket->Error != CK_OK)
            return CKRST_READBACK_FAILED;
        return Ticket->Complete ? CKRST_READBACK_READY
                                : CKRST_READBACK_NEEDS_SUBMIT;
    }

    uint64_t GetDrawApproximationMask() const { return m_DrawApproximations; }
    const CKBgfxDeviceLimits &GetDeviceLimits() const { return m_CapsDesc; }
    const char *GetRendererName() const { return m_RendererName; }
    CKERROR GetTextureFormatCaps(VX_PIXELFORMAT Format,
                                 CKBgfxTextureFormatCaps *Caps) const;
    void RecordFatalError(CKERROR Error);

    CKBgfxShaderRecord *GetShader(CKDWORD Handle);
    CKBgfxProgramRecord *GetProgram(CKDWORD Handle);
    CKBgfxVertexLayoutRecord *GetVertexLayout(CKDWORD Handle);
    CKBgfxVertexBufferRecord *GetVertexBuffer(CKDWORD Handle);
    CKBgfxIndexBufferRecord *GetIndexBuffer(CKDWORD Handle);
    CKBgfxTextureRecord *GetTexture(CKDWORD Handle);
    CKBgfxFrameBufferRecord *GetFrameBuffer(CKDWORD Handle);

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

    // --- Diagnostics ---
    void GetStats(CKRenderStats &Stats) const override;

    // --- Test access ---
    CKFixedFunctionPipeline *GetFFPipelineForTests() { return &m_FFP; }
    CKBOOL RegisterTextureForTests(CKDWORD Handle, const CKTextureDesc &Desc) {
        return m_PublicResources.InsertTexture(Handle, Desc);
    }
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
    CKRECT GetWindowViewRectForTests() const;
    CKDWORD ExchangeNextViewForTests(CKDWORD Next = UINT32_MAX);
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
        XArray<CKBYTE> Data;
        NativeReadbackTicket Ticket;

        PendingReadback()
            : Callback(NULL), User(NULL), HasRect(FALSE), Buffer(VXBUFFER_BACKBUFFER), Done(FALSE),
              Success(FALSE), Width(0), Height(0), Pitch(0), Format(UNKNOWN_PF), YFlip(FALSE),
              Ticket() {
            Rect.left = Rect.top = Rect.right = Rect.bottom = 0;
        }
    };

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
    // Engine coordinates: the logical target is the window or the
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
    CKBOOL ReleaseTarget();
    void UpdateAlphaTestPrecision();

    // Draw helpers
    CKBOOL ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL CheckDeviceForDraw();
    void CountDraw(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL SubmitVertexBuffer(VXPRIMITIVETYPE Type, CKDWORD VBHandle, const VertexBufferData &VB,
                              CKDWORD IBHandle, CKDWORD BaseVertex,
                              CKDWORD VertexCount, CKDWORD StartIndex, CKDWORD IndexCount,
                              const CKWORD *Indices = NULL);
    CKBOOL SubmitPreparedDraw();
    CKDWORD GetNativeVertexLayout(CKDWORD FormatFlags);
    void ClearNativeVertexLayouts();
    CKFFProgramBinding ResolveNativeFFProgram(const CKFFShaderKey &Key);
    void ClearNativeFFPrograms();

    // Readback helpers
    CKBOOL BuildReadbackImage(const PendingReadback &Readback, VxImageDescEx &Desc,
                              XArray<CKBYTE> &Pixels) const;
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
    CKFixedFunctionPipeline m_FFP;
    CKFFShaderCache m_ShaderCache;
    CKDWORD m_NativeFFPrograms[CKFF_PROGRAM_VARIANT_COUNT] = {};
    CKDWORD m_NativeFFVertexShaders[CKFF_PROGRAM_VARIANT_COUNT] = {};
    CKDWORD m_NativeFFPixelShader = 0;
    XSHashTable<CKDWORD, CKDWORD> m_NativeVertexLayouts;
    CKBgfxPresentStage m_Present;
    // Verbatim fixed-function state mirror.

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
    XArray<PendingReadback *> m_Readbacks;

    // Stats
    CKDWORD m_FrameDrawCalls;
    CKDWORD m_FramePrimitives;
    CKDWORD m_FramePasses;
    CKDWORD m_FrameClears;
    CKDWORD m_FrameTextureUploads;
    CKDWORD m_FrameBufferUploads;
    CKBOOL m_LayoutMismatchLogged;

    static const int MAX_TRANSIENT_VB = 256;
    static const int MAX_TRANSIENT_IB = 256;

    CKBOOL IsApiThread() const
    {
        return VxThread::GetCurrentVxThreadId() == m_ApiThreadId ? TRUE : FALSE;
    }
    CKBOOL IsReady() const
    {
        return m_BgfxInitialized && m_BgfxCreated && IsApiThread() ? TRUE : FALSE;
    }
    void LatchFatalError(CKERROR Error);
    void ReleaseBgfx();
    CKERROR PrepareRectClear();
    void EncodeRectClear(bgfx::ViewId View, const CKRenderPassDesc &Desc);
    void ReleaseRectClear();
    CKRECT WindowPixelRect(const CKRECT &Rect) const;
    CKERROR BuildFrameBufferAttachments(
        const CKRenderTargetDesc *Desc, bgfx::Attachment *Attachments,
        CKDWORD Capacity, CKDWORD &AttachmentCount);
    CKERROR CreateVertexBufferRecord(CKDWORD VertexSize, CKDWORD VertexCount,
                                     CKDWORD Layout, const void *Data,
                                     CKDWORD *OutBuffer);
    CKERROR CreateIndexBufferRecord(CKDWORD IndexCount, CKBOOL Index32,
                                    const void *Data, CKDWORD *OutBuffer);
    CKERROR UploadTextureOrdered(bgfx::TextureHandle Texture,
                                 bgfx::TextureFormat::Enum Format,
                                 CKDWORD Mip, CKDWORD X, CKDWORD Y,
                                 CKDWORD Layer, CKDWORD Width, CKDWORD Height,
                                 const bgfx::Memory *Data,
                                 CKBOOL Cube = FALSE,
                                 CKBOOL Volume = FALSE);
    CKBOOL UpdateGeneratedMipMaps(CKBgfxTextureRecord *Texture,
                                  const VxImageDescEx *BaseLevel);
    CKERROR UpdateVertexBufferRecord(const CKBufferUpdateDesc &Desc);
    CKERROR UpdateIndexBufferRecord(const CKBufferUpdateDesc &Desc);

    CKERROR DrawFailed(CKERROR Error, const char *Operation);
    CKERROR ApplyPipelineState(const CKFFPipelineState &State);
    CKERROR BindGeometry(const CKDrawCommand *Draw);
    std::shared_ptr<bgfx::TextureHandle> GetDefaultTexture(
        const CKFFSamplerBinding &Binding);
    CKERROR BindTextureSlot(const CKFFSamplerBinding &Binding,
                            bgfx::UniformHandle Uniform,
                            bgfx::TextureHandle DefaultTexture,
                            CKDWORD Texture, const CKSamplerDesc *Sampler,
                            bool FixedFunctionBorderSampling,
                            bool ShaderBorderSampling);
    void ResetDebugBindings();
    void TraceSubmit(CKDWORD Program, bgfx::ProgramHandle ProgramHandle,
                     CKDWORD Depth, const CKFFPipelineState &State);

    void ConfigureDebug();
    void DrawDebugOverlay();
    void TraceTextureMap(CKSTRING Event, CKDWORD Texture,
                         const CKBgfxTextureRecord *Record);
    void TraceProgramMap(CKSTRING Event, CKDWORD Program,
                         const CKBgfxProgramRecord *Record);
    void TraceBufferMap(CKSTRING Event, CKSTRING Kind, CKDWORD Buffer,
                        CKDWORD BgfxHandle, CKDWORD Layout, CKDWORD Stride,
                        CKDWORD Count, CKDWORD Index32, CKDWORD Flags);
    void RecordInvalidSubmit(CKSTRING Kind, bgfx::ViewId View,
                             CKDWORD Program, CKSTRING Reason);
    void RecordTransientAllocMiss(const char *Kind, CKDWORD Requested,
                                  CKDWORD Available);
    void RecordViewColorWrite(bgfx::ViewId View, CKBOOL HasDraw);
    void RecordTextureWrite(CKBgfxTextureRecord *Texture, CKDWORD Mip,
                            CKBgfxTextureOrientation Orientation,
                            CKBOOL FullOverwrite);
    void RecordTextureBlit(CKBgfxTextureRecord *Destination,
                           CKDWORD DestinationMip,
                           const CKBgfxTextureRecord *Source,
                           CKDWORD SourceMip, CKBOOL FullOverwrite);
    CKBOOL GetTextureBottomLeft(CKDWORD Texture, CKBOOL &BottomLeft);

    bgfx::ProgramHandle m_RectClearProgram = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_RectClearVertices = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_RectClearColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_RectClearDepth = BGFX_INVALID_HANDLE;

    CKBOOL m_BgfxInitialized;
    CKBOOL m_BgfxCreated;
    WIN_HANDLE m_BgfxWindow;
    int m_BgfxPosX;
    int m_BgfxPosY;
    CKDWORD m_BgfxWidth;
    CKDWORD m_BgfxHeight;
    CKDWORD m_DrawableWidth;
    CKDWORD m_DrawableHeight;
    CKBOOL m_BgfxFullscreen;
    const char *m_RendererName;
    bgfx::RendererType::Enum m_RendererType;
    struct NativeReadback {
        XArray<CKBYTE> Data;
        CKBOOL Complete = FALSE;
        CKERROR Error = CK_OK;
        CKDWORD AvailableFrame = 0;
        bgfx::TextureHandle Snapshot = BGFX_INVALID_HANDLE;
    };
    XClassArray<std::shared_ptr<NativeReadback>> m_BgfxReadbacks;
    CKBgfxBorderPalette m_BorderPalette;
    uint64_t m_DrawApproximations = 0;
    CKRasterizerDeviceCaps m_Caps;
    int64_t m_BgfxCpuTimeFrame = 0;
    int64_t m_BgfxCpuTimerFreq = 0;
    int64_t m_BgfxGpuTimeFrame = 0;
    int64_t m_BgfxGpuTimerFreq = 0;
    CKDWORD m_BgfxGpuMemoryMax = 0;
    CKDWORD m_BgfxGpuMemoryUsed = 0;
    CKBgfxDeviceLimits m_CapsDesc;
    uint64_t m_NativeSupported;
    uint32_t m_NativeFormatCaps[bgfx::TextureFormat::Count];
    XArray<CKBYTE> m_ConstantData[CKFF_CONSTANT_SLOT_COUNT];
    DefaultTextureTable m_DefaultTextures;
    CKBOOL m_VSync;
    uint32_t m_ResetFlags;
    CKBgfxCallback m_BgfxCallback;
    XUINTPTR m_ApiThreadId;
    std::atomic<CKBOOL> m_BgfxShuttingDown;
    std::atomic<CKERROR> m_FatalError;

    CKBOOL m_FrameInProgress;
    CKBOOL m_PassOpen;
    CKRenderPassDesc m_LogicalPass;
    bool m_DrawPassNeedsResume = false;
    bgfx::ViewId m_CurrentView;
    CKDWORD m_NextView;
    CKDWORD m_LastFrameViewCount;
    CKDWORD m_BgfxFramePasses;
    CKDWORD m_FrameDraws;
    CKDWORD m_FrameBlits;
    CKDWORD m_BgfxFrameTextureUploads;
    CKDWORD m_BgfxFrameBufferUploads;
    XArray<CKBgfxSubmittedFrame> m_SubmittedFrames;
    CKQWORD m_LastSubmitId;
    CKQWORD m_CompletedSubmitId;
    CKDWORD m_ViewFrameBuffer[CKRST_MAX_PASSES];
    CKRECT m_ViewRect[CKRST_MAX_PASSES];
    CKDWORD m_ViewClearFlags[CKRST_MAX_PASSES];
    CKBOOL m_ViewClearRecorded[CKRST_MAX_PASSES];

    CKDrawState m_CachedDrawState;
    uint64_t m_CachedBgfxState;
    CKDWORD m_PointSize;
    CKDWORD m_CurrentLayout;
    char m_LastMarker[512];
    CKBgfxDrawMapVertexBinding m_DebugVertexBindings[CKRST_MAX_VERTEX_STREAMS];
    CKBgfxDrawMapTextureBinding m_DebugTextureBindings[CKFF_TEXTURE_SLOT_COUNT];
    CKDWORD m_DebugVertexBindingMask;
    CKDWORD m_DebugTextureBindingMask;
    CKDWORD m_DebugIndexBuffer;
    CKDWORD m_DebugIndexStart;
    CKDWORD m_DebugIndexCount;
    CKDWORD m_DebugIndexHandle;
    CKDWORD m_DrawErrorLogCount;

    CKDWORD m_DebugFrameId;
    std::atomic<CKDWORD> m_DebugSubmitSerial;
    std::atomic<CKDWORD> m_DebugViewSubmitSerial[CKRST_MAX_PASSES];
    std::atomic<CKDWORD> m_DebugMissingAnnotationCount;
    std::atomic<CKDWORD> m_DebugMarkerOverwriteCount;
    std::atomic<CKDWORD> m_DebugMarkerStaleCount;
    std::atomic<CKDWORD> m_DebugInvalidSubmitCount;
    std::atomic<CKDWORD> m_DebugFatalCount;
    std::atomic<CKDWORD> m_DebugParsedAnnotationCount;
    std::atomic<CKDWORD> m_DebugRawPrimitiveCount;
    std::atomic<CKDWORD> m_DebugSourceSubmitCount[CKBGFX_DRAWMAP_SOURCE_COUNT];
    std::atomic<CKDWORD> m_DebugTransientAllocMissCount;
    char m_DebugViewName[CKRST_MAX_PASSES][64];
    CKDWORD m_DebugFlags;
    CKDWORD m_DrawMapFlags;
    CKBOOL m_DrawMapActive;
    CKBOOL m_DrawMapSubmitActive;
    CKBOOL m_DrawMapMarkerCaptureActive;
    CKDWORD m_DebugBgfxFlags;
    CKBOOL m_DebugOverlay;
    CKBOOL m_DebugLogPresentSync;
    CKBOOL m_DebugLogTextureBindings;
    CKBOOL m_DebugLogTextures;
    CKBOOL m_DebugLogUniforms;

    CKBgfxResources *m_Resources;
    VxMutex m_ResourceStateMutex;

    bgfx::TransientVertexBuffer m_TransientVBPool[MAX_TRANSIENT_VB];
    CKDWORD m_TransientVBCount;
    bgfx::TransientIndexBuffer m_TransientIBPool[MAX_TRANSIENT_IB];
    CKDWORD m_TransientIBCount;
};

#endif // CKBGFXRASTERIZERCONTEXT_H
