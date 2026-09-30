#ifndef CKSDLGPURASTERIZERCONTEXT_H
#define CKSDLGPURASTERIZERCONTEXT_H

// Concrete SDL_GPU CKRasterizerContext. Fixed-function CPU state and native
// SDL_GPU resource/submission state have one owner.

#include "CKBuiltinShaders.h"
#include "CKRasterizer.h"
#include "CKRasterizerContextData.h"
#include "CKFixedFunctionPipeline.h"
#include "CKFFBufferUseTracker.h"
#include "CKFFBufferData.h"
#include "CKFFContextState.h"
#include "CKFFResourceStore.h"
#include "CKFFShaderCache.h"
#include "CKSdlGpuFFJitManifest.h"
#include "CKSdlGpuPresentStage.h"
#include "CKSdlGpuShaders.h"
#include "XSHashTable.h"

#include <memory>

#include "../Native/CKSdlGpuInternal.h"
#include "../Native/CKSdlGpuWorker.h"

class CKSdlGpuRasterizerContext;
struct CKSdlGpuReadback;
struct CKSdlGpuSubmissionStats;
typedef std::shared_ptr<CKSdlGpuReadback> CKSdlGpuReadbackTicket;

typedef void (*CKSdlGpuContextReadyFunction)(
    void *User, CKSdlGpuRasterizerContext &Context);

// ===========================================================================
// CKSdlGpuRasterizerContext
// ===========================================================================

class CKSdlGpuRasterizerContext final : public CKRasterizerContext {
public:
    using CKRasterizerContext::BackToFront;

    CKSdlGpuRasterizerContext();
    CKSdlGpuRasterizerContext(CKRasterizerDriver *Driver,
                              const CKFFShaderLibrary *Shaders,
                              CKSdlGpuContextReadyFunction Ready = NULL,
                              void *ReadyUser = NULL);
    ~CKSdlGpuRasterizerContext() override;

    // Native SDL_GPU operations remain methods of the concrete Context.
    CKERROR Init(const CKRasterizerInitParameters *Desc);
    void Shutdown();
    CKERROR Resize(int PosX, int PosY, int Width, int Height);
    const CKRasterizerDeviceCaps &GetCaps() const;
    CKBOOL IsNativeIdle() const;
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
    CKBOOL SupportsTexture2D(VX_PIXELFORMAT Format) const;
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
                           CKPresentSync Sync);
    CKERROR Submit(CKPresentSync Sync, CKBOOL PresentWindow, CKDWORD *SubmissionNumber);
    CKQWORD GetLastSubmitId() const;
    CKQWORD GetCompletedSubmitId();
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip,
                        CKReadbackDesc *Readback,
                        CKSdlGpuReadbackTicket *Ticket);
    CKReadbackState PollReadback(
        const CKSdlGpuReadbackTicket &Ticket, CKBOOL Wait);
    const XArray<CKBYTE> &GetReadbackData(
        const CKSdlGpuReadbackTicket &Ticket) const;
    uint64_t GetDrawApproximationMask() const;
    const CKSdlGpuSubmissionStats &GetSubmissionStats() const;

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
    // --- Test access ---
    CKFixedFunctionPipeline *GetFFPipelineForTests() { return &m_FFP; }
    CKBOOL RegisterTextureForTests(CKDWORD Handle, const CKTextureDesc &Desc) {
        return m_PublicResources.InsertTexture(Handle, Desc);
    }
    CKDWORD GetTargetForTests() const { return m_TargetState.Texture; }
    CKBOOL IsInSceneForTests() const { return m_FrameState.IsSceneActive(); }
    CKBOOL IsOverlayPhaseForTests() const { return m_FrameState.IsOverlayActive(); }
    CKDWORD GetPassCountForTests() const { return m_FramePasses; }
    int GetLiveResourceCountForTests(CKDWORD TypeMask) const;
    const CKLightData &GetLightForTests(CKDWORD Index) const { return m_FFP.GetLight((int)Index); }
    CKBOOL IsLightEnabledForTests(CKDWORD Index) const { return m_FFP.IsLightEnabled((int)Index); }
    const CKMaterialData &GetMaterialForTests() const { return m_FFP.GetMaterial(); }
    const CKViewportData &GetViewportForTests() const { return m_FFP.GetViewport(); }
    const CKRasterizerOptions &GetOptionsForTests() const { return m_Options; }
    CKERROR FlushPendingCommandsForTests();
    CKERROR AcquireSwapchainForTests();
    void CollectForTests();
    CKBOOL CompleteEmptySubmissionsForTests();
    // Waits for background compilation, then completes it as a frame
    // boundary would, until completing it queues no more. Call between
    // frames.
    CKBOOL FinishBackgroundWorkForTests(Sint32 TimeoutMs);
    struct FFJitCounts {
        CKDWORD Queued = 0, Ready = 0, Rejected = 0, Programs = 0, Pipelines = 0;
    };
    FFJitCounts CountFFJitProgramsForTests() const;
private:
    typedef CKFFVertexBufferData VertexBufferData;
    typedef CKFFIndexBufferData IndexBufferData;

    // SDL_GPU command and resource helpers. They are private methods of the
    // concrete Context so native failure, submission and lifetime policy stay
    // local to the one owner.
    bool Ready() const {
        return Device && SDL_GetCurrentThreadID() == Thread && Error == CK_OK;
    }
    CKERROR Fail(const char *Operation);
    // The SDL payload format for a shader target, or INVALID when this
    // device cannot create it.
    SDL_GPUShaderFormat NativeShaderFormat(CK_SHADER_FORMAT Format,
                                           CK_SHADER_PROFILE Profile) const;
    bool EnsureCommands();
    CKERROR AcquireSwapchain();
    CKERROR Flush(bool PresentWindow = true);
    CKSdlGpuGeometryUpload UploadGeometry(const XArray<CKBYTE> &Vertices,
                                          const XArray<CKBYTE> &Indices);
    CKERROR UploadBuffer(SDL_GPUBuffer *Buffer, unsigned Offset,
                         const void *Data, unsigned Size, bool Cycle);
    CKERROR UploadTexture(CKSdlGpuTexture &Texture, unsigned Mip,
                          unsigned Layer, const CKRECT *Region,
                          const VxImageDescEx &Data);
    CKERROR UploadTexturePixels(CKSdlGpuTexture &Texture, unsigned Mip,
                                unsigned Layer, unsigned X, unsigned Y,
                                unsigned Width, unsigned Height,
                                const XArray<CKBYTE> &Pixels);
    CKERROR GenerateUploadMips(CKSdlGpuTexture &Texture, unsigned Layer);
    CKERROR GenerateGpuMips(CKSdlGpuTexture &Texture, unsigned Layer);
    CKERROR GenerateVolumeMips(CKSdlGpuTexture &Texture);
    CKERROR CopyVolumeSlice(CKSdlGpuTexture &Texture, unsigned Mip,
                            unsigned Layer, SDL_GPUTexture *Slice,
                            bool ToVolume);
    CKERROR PreserveTexture(CKSdlGpuTexture &Texture);
    CKERROR PrepareDepthPad(
        const std::shared_ptr<CKSdlGpuTexture> &Source,
        const CKSamplerDesc &Sampler,
        std::shared_ptr<CKSdlGpuTexture> &Padded,
        float Transform[4]);
    CKERROR ClearRect(SDL_GPURenderPass *Pass,
                      const CKRenderPassDesc &Desc,
                      SDL_GPUTextureFormat ColorFormat,
                      SDL_GPUTextureFormat DepthFormat,
                      SDL_GPUSampleCount Samples);
    CKERROR ConvertColorTexture(SDL_GPUTexture *Source,
                                SDL_GPUTexture *Destination,
                                SDL_GPUTextureFormat DestinationFormat,
                                unsigned Width, unsigned Height);
    CKERROR DrawColorTexture(SDL_GPURenderPass *Pass,
                             SDL_GPUTexture *Source,
                             SDL_GPUTextureFormat ColorFormat,
                             SDL_GPUTextureFormat DepthFormat,
                             SDL_GPUSampleCount Samples,
                             unsigned Width, unsigned Height,
                             const SDL_Rect &Scissor,
                             CKDWORD ColorTargetFormat,
                             CKBOOL Dither,
                             CKBOOL ExplicitQuantize);
    CKERROR EnsureDitherTargets(unsigned Width, unsigned Height,
                                unsigned Samples,
                                SDL_GPUTextureFormat SourceFormat);
    CKERROR SnapshotDitherSource(SDL_GPUTexture *Source,
                                SDL_GPUTextureType SourceType,
                                unsigned SourceMip, unsigned SourceLayer,
                                unsigned Width, unsigned Height,
                                SDL_GPUTexture **Snapshot);
    SDL_GPUGraphicsPipeline *Pipeline(
        const CKSdlGpuDraw &Draw, SDL_GPUTextureFormat Color,
        SDL_GPUTextureFormat Depth, SDL_GPUSampleCount Samples);
    // Queues the worker's creation of the pipeline of a specialized program
    // for a draw, unless the program has or awaits it already.
    void QueuePipeline(const CKSdlGpuDraw &Draw, SDL_GPUTextureFormat Color,
                       SDL_GPUTextureFormat Depth, SDL_GPUSampleCount Samples,
                       CKSdlGpuJobPriority Priority);
    std::shared_ptr<SDL_GPUSampler> Sampler(const CKSamplerDesc &Desc);
    void PruneProgramCaches();
    void Collect();
    // Takes ownership; the worker starts with its first job. A job
    // submitted after another runs once that one has run.
    bool SubmitJob(CKSdlGpuJob *Job,
                   CKSdlGpuJobPriority Priority = CKSDLGPU_JOB_NORMAL,
                   const CKSdlGpuJob *After = nullptr);
    // Completes finished jobs. Call only at a frame boundary.
    void CollectJobs();

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
        CKSdlGpuReadbackTicket Ticket;

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
    CKDWORD ResolveNativeFFProgram(
        const CKFFProgramContext &ProgramContext,
        const CKFFTextureBindingSet &Textures,
        const CKFFConstantSet *Constants,
        CKBOOL PositionTDepthPad);
    // The precompiled program of an artifact, created on first use.
    CKDWORD NativeFFProgram(CKFFProgramVariant Variant,
                            const CKSdlGpuFFFragmentArtifactKey &Artifact,
                            CKBOOL PositionTDepthPad);
    void ClearNativeFFPrograms();

    // Fragment programs compiled at runtime (CKSdlGpuRasterizerFFJit.cpp).
    // A draw names the key of its fragment program and of the draw state the
    // shader of its artifact branches on. The draws of keys that compile
    // alike share one entry, whose shader the worker compiles and creates
    // once. They bind the entry's programs at once and draw with the
    // precompiled pipelines until the worker has created the programs' own,
    // right after the shader. The manifest of the device queues the
    // programs and pipelines of earlier runs at idle priority before any
    // draw asks for them.
    class FFJitJob;
    // The lanes and switches of a CKFFNativeFragmentKey, then the sampler
    // layout.
    enum {
        FF_JIT_KEY_LAYOUT = CKFF_FRAGMENT_PROGRAM_LANE_COUNT +
                            CKFF_NATIVE_FRAGMENT_SWITCH_WORD_COUNT,
    };
    typedef CKSdlGpuFixedKey<FF_JIT_KEY_LAYOUT + 1> FFJitKey;
    typedef CKSdlGpuFixedKeyHash<FF_JIT_KEY_LAYOUT + 1> FFJitKeyHash;
    static FFJitKey MakeFFJitKey(const CKFFNativeFragmentKey &Fragment,
                                 CKFFSamplerLayout Layout);
    // The artifact a key names: its layout, and the shader sampling and
    // comparison samplers of its switches.
    static CKSdlGpuFFFragmentArtifactKey FFJitArtifact(const FFJitKey &Key);
    struct FFJitProgram {
        enum Status { QUEUED, READY, REJECTED };
        // Canonical.
        FFJitKey Key;
        Status State = QUEUED;
        // Registered when the entry is queued, before the worker creates it.
        CKDWORD PixelShader = 0;
        // A program of the shader for the draws of each precompiled program
        // they replace, whose interface and pipelines it takes.
        struct Binding {
            CKDWORD Precompiled;
            CKFFProgramVariant Variant;
            CKDWORD Program;
        };
        XArray<Binding> Programs;
        // Orders the manifest: the programs draws used, by first use, then
        // the loaded ones no draw used, in their earlier order.
        CKDWORD Rank = 0;
        // A loaded program's compilation, promoted when a draw uses it.
        CKSdlGpuJob *IdleJob = nullptr;
        // The loaded pipelines to queue once the program is compiled.
        XArray<CKSdlGpuFFJitRecord> Prewarm;
    };
    void InitFFJit();
    void LoadFFJitManifest();
    void PrewarmFFJitProgram(FFJitProgram &Entry);
    // Records the compiled programs and their pipelines for the next run.
    void SaveFFJitManifest();
    CKDWORD ResolveFFJitProgram(const CKFFFragmentProgram &FragmentProgram,
                                const CKFFConstantSet *Constants,
                                const CKSdlGpuFFFragmentArtifactKey &Artifact,
                                CKFFProgramVariant Variant,
                                CKDWORD Precompiled);
    // The entry of a draw key no draw had, -1 past the entry limit.
    int AddFFJitDrawKey(const FFJitKey &DrawKey, CKFFNativeFragmentKey Fragment,
                        CKFFSamplerLayout Layout);
    int AddFFJitProgram(const FFJitKey &Key, CKDWORD Rank);
    // Queues the compilation of a new entry and registers its shader. False
    // when the entry is rejected.
    bool SubmitFFJitProgram(FFJitProgram &Entry, const CKFFNativeFragmentKey &Fragment,
                            CKFFSamplerLayout Layout, CKSdlGpuJobPriority Priority);
    // The program of an entry for the draws of a precompiled program,
    // created on first use. When it cannot be, the entry is rejected.
    CKDWORD BindFFJitProgram(FFJitProgram &Entry, CKFFProgramVariant Variant,
                             CKDWORD Precompiled);
    CKDWORD CreateFFJitProgram(CKDWORD PixelShader,
                               CKFFProgramVariant Variant,
                               CKDWORD Precompiled);
    // The shader holds no SDL shader when the worker could not compile or
    // create it.
    void CompleteFFJitProgram(const FFJitKey &Key,
                              const std::shared_ptr<CKSdlGpuShader> &Shader);
    void ClearFFJitPrograms();

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
    CKSdlGpuContextReadyFunction m_Ready;
    void *m_ReadyUser;
    CKFixedFunctionPipeline m_FFP;
    CKFFShaderCache m_ShaderCache;
    CKDWORD m_NativeFFPrograms[CKFF_PROGRAM_VARIANT_COUNT]
                                [CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT]
                                [2] = {};
    CKDWORD m_NativeFFVertexShaders[CKFF_PROGRAM_VARIANT_COUNT] = {};
    CKDWORD m_NativeFFDepthPadVertexShaders[2] = {};
    CKDWORD m_NativeFFPixelShaders[
        CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT] = {};
    // INVALID when every program draws with the precompiled shaders.
    SDL_GPUShaderFormat m_FFJitFormat = SDL_GPU_SHADERFORMAT_INVALID;
    // Entries by creation, and the index of each canonical key's entry.
    XClassArray<FFJitProgram> m_FFJitPrograms;
    XSHashTable<int, FFJitKey, FFJitKeyHash> m_FFJitKeys;
    // The entry index of every draw key drawn, so that a draw canonicalizes
    // only a key it has not drawn before.
    XSHashTable<int, FFJitKey, FFJitKeyHash> m_FFJitDrawKeys;
    // DXBC programs cannot use the DXIL vertex shaders.
    CKDWORD m_FFJitVertexShaders[CKFF_PROGRAM_VARIANT_COUNT] = {};
    CKDWORD m_FFJitDepthPadVertexShaders[2] = {};
    // Empty when the manifest is disabled.
    XString m_FFJitManifest;
    uint64_t m_FFJitIdentity = 0;
    CKDWORD m_FFJitUses = 0;
    XSHashTable<CKDWORD, CKDWORD> m_NativeVertexLayouts;
    CKSdlGpuPresentStage m_Present;
    // Verbatim fixed-function state mirror.

    // Frame
    CKFFFrameState m_FrameState;
    CKDWORD m_PassTarget;
    CKDWORD m_LastDeviceFrame;     // backend frame counter after the last Present()

    // Target
    CKFFRenderTargetState m_TargetState;
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

    // Native SDL_GPU state is owned directly by this concrete Context.
    SDL_GPUDevice *Device = nullptr;
    SDL_Window *Window = nullptr; // borrowed from Player
    bool WindowClaimed = false;
    SDL_ThreadID Thread = 0;
    SDL_GPUShaderFormat ShaderFormat = 0;
    CKRasterizerDeviceCaps Caps;
    CKSdlGpuSubmissionStats Stats;
    CKSdlGpuSubmissionStats FrameStats;
    CKERROR Error = CK_OK;
    CKDWORD DebugFlags = 0;
    CKDWORD Submission = 0;
    CKQWORD LastSubmitId = 0;
    CKQWORD CompletedSubmitId = 0;
    uint64_t DrawApproximations = 0;
    unsigned Width = 0;
    unsigned Height = 0;
    SDL_GPUPresentMode PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    SDL_GPUCommandBuffer *Commands = nullptr;
    SDL_GPUTexture *Swapchain = nullptr;
    unsigned SwapWidth = 0;
    unsigned SwapHeight = 0;
    bool PresentCopySupported = false;
    bool PresentCopyLogged = false;
    bool PassOpen = false;
    CKRenderPassDesc Pass;
    std::shared_ptr<CKSdlGpuTarget> Target;
    XClassArray<CKSdlGpuDraw> Draws;
    CKSdlGpuDrawResourceBatch DrawResources;
    CKSdlGpuBinding SamplerBindings[CKFF_TEXTURE_SLOT_COUNT];
    CKSdlGpuUniformBatch Uniforms;
    CKSdlGpuBindingBatch Bindings;
    CKSdlGpuTransientStorage TransientVertices;
    CKSdlGpuTransientStorage TransientIndices;
    CKDWORD NextTransientToken = 1;
    XArray<CKSdlGpuTransientVertexInfo> TransientVertexInfo;
    XArray<CKSdlGpuTransientIndexInfo> TransientIndexInfo;
    CKSdlGpuDefaultVertexTable DefaultVertexBuffers;
    std::shared_ptr<CKSdlGpuProgram> ClearProgram;
    std::shared_ptr<CKSdlGpuProgram> VolumeMipProgram;
    std::shared_ptr<CKSdlGpuProgram> DitherProgram;
    std::shared_ptr<SDL_GPUSampler> DitherSampler;
    std::shared_ptr<SDL_GPUTexture> DitherScratch;
    std::shared_ptr<SDL_GPUTexture> DitherMultisample;
    std::shared_ptr<SDL_GPUTexture> DitherResolved;
    std::shared_ptr<SDL_GPUTexture> DitherSource;
    SDL_GPUTextureFormat DitherSourceFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    unsigned DitherScratchWidth = 0;
    unsigned DitherScratchHeight = 0;
    unsigned DitherScratchSamples = 1;
    CKSdlGpuDefaultTextureTable DefaultTextures;
    XClassArray<std::shared_ptr<CKSdlGpuReadback>> Readbacks;
    XClassArray<CKSdlGpuSubmission> Submissions;
    XClassArray<std::shared_ptr<CKSdlGpuGeometryBuffer>> PendingGeometry;
    XClassArray<std::shared_ptr<CKSdlGpuGeometryBuffer>> FreeGeometry;
    size_t FreeGeometryBytes = 0;
    XClassArray<std::shared_ptr<CKSdlGpuBufferUploadPage>> PendingBufferUploads;
    XClassArray<std::shared_ptr<CKSdlGpuBufferUploadPage>> FreeBufferUploads;
    size_t FreeBufferUploadBytes = 0;
    XArray<CKBYTE> BatchVertices;
    XArray<CKBYTE> BatchIndices;
    CKSdlGpuSamplerTable Samplers;
    XClassArray<CKSdlGpuDepthPad> DepthPads;
    CKSdlGpuTable<CKSdlGpuTexture> Textures;
    CKSdlGpuTable<CKSdlGpuBuffer> VertexBuffers;
    CKSdlGpuTable<CKSdlGpuBuffer> IndexBuffers;
    CKSdlGpuTable<CKSdlGpuLayout> Layouts;
    CKSdlGpuTable<CKSdlGpuShader> ShaderObjects;
    CKSdlGpuTable<CKSdlGpuProgram> Programs;
    CKSdlGpuTable<CKSdlGpuTarget> Targets;
    // Last, so it stops before anything its jobs reference is destroyed.
    CKSdlGpuWorker Worker;
};

#endif // CKSDLGPURASTERIZERCONTEXT_H
