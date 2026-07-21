#ifndef CKBGFXRASTERIZER_H
#define CKBGFXRASTERIZER_H

#include "CKRasterizer.h"

#define CKBGFX_DRAWMAP_SOURCE_COUNT 6

#include <atomic>
#include <string.h>
#include <bgfx/bgfx.h>

#define CKBGFX_DRAWMAP_HASH_INIT 2166136261u

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

class CKBgfxRasterizerDriver;
class CKBgfxRasterizerContext;
class CKBgfxEncoder;

// ===========================================================================
// bgfx Callback Implementation
// ===========================================================================

class CKBgfxCallback : public bgfx::CallbackI {
public:
    CKBgfxCallback() : m_Context(NULL) {}

    void SetContext(CKBgfxRasterizerContext *ctx) { m_Context = ctx; }

    void fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char *_str) override;
    void traceVargs(const char *_filePath, uint16_t _line, const char *_format, va_list _argList) override;
    void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t _id) override;
    bool cacheRead(uint64_t _id, void *_data, uint32_t _size) override;
    void cacheWrite(uint64_t _id, const void *_data, uint32_t _size) override;
    void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                    uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                    const void *_data, uint32_t _size, bool _yflip) override;
    void captureBegin(uint32_t _width, uint32_t _height, uint32_t _pitch,
                      bgfx::TextureFormat::Enum _format, bool _yflip) override;
    void captureEnd() override;
    void captureFrame(const void *_data, uint32_t _size) override;

private:
    CKBgfxRasterizerContext *m_Context;
    VxMutex m_CacheMutex;
};

// ===========================================================================
// Internal resource records
// ===========================================================================

struct CKBgfxShaderRecord {
    bgfx::ShaderHandle Handle;
    CK_SHADER_STAGE Stage;
};

struct CKBgfxProgramRecord {
    bgfx::ProgramHandle Handle;
    CKDWORD VertexShader;
    CKDWORD PixelShader;
};

struct CKBgfxUniformRecord {
    bgfx::UniformHandle Handle;
    CK_UNIFORM_TYPE Type;
    CKDWORD Count;
    char Name[64];
};

struct CKBgfxVertexLayoutRecord {
    bgfx::VertexLayoutHandle Handle;
    bgfx::VertexLayout Layout;
};

struct CKBgfxVertexBufferRecord {
    bgfx::DynamicVertexBufferHandle Handle;
    CKDWORD Flags;
    CKDWORD Layout;
    CKDWORD VertexSize;
    CKDWORD VertexCount;
    CKDWORD Size;
};

struct CKBgfxIndexBufferRecord {
    bgfx::DynamicIndexBufferHandle Handle;
    CKBOOL Index32;
    CKDWORD IndexCount;
    CKDWORD Size;
};

enum CKBgfxTextureOrientation {
    CKBGFX_ORIENTATION_UNKNOWN = 0,
    CKBGFX_ORIENTATION_TOP_LEFT,
    CKBGFX_ORIENTATION_BOTTOM_LEFT,
    CKBGFX_ORIENTATION_MIXED,
};

static const CKDWORD CKBGFX_MAX_TRACKED_MIPS = 32;
static const CKDWORD CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS = 16;

struct CKBgfxTextureRecord {
    CKBgfxTextureRecord()
        : Handle(BGFX_INVALID_HANDLE), SamplerBaseHandle(BGFX_INVALID_HANDLE),
          Flags(0), Width(0), Height(0), Depth(1),
          IsDepth(FALSE), RequestedAutoMips(FALSE), MipCount(1),
          Format(bgfx::TextureFormat::Count), PixelFormat(UNKNOWN_PF), BitsPerPixel(0),
          SamplerBaseValid(FALSE),
          AutoMipBaseValid(FALSE)
    {
        memset(ReadbackOrientation, CKBGFX_ORIENTATION_UNKNOWN,
               sizeof(ReadbackOrientation));
    }

    ~CKBgfxTextureRecord()
    {
        if (bgfx::isValid(SamplerBaseHandle)) {
            bgfx::destroy(SamplerBaseHandle);
            SamplerBaseHandle = BGFX_INVALID_HANDLE;
        }
        delete[] AutoMipBaseDesc.Image;
        AutoMipBaseDesc.Image = NULL;
    }

    bgfx::TextureHandle Handle;
    bgfx::TextureHandle SamplerBaseHandle;
    CKDWORD Flags;
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD Depth;
    CKBOOL IsDepth;
    CKBOOL RequestedAutoMips;
    CKDWORD MipCount;
    bgfx::TextureFormat::Enum Format;
    VX_PIXELFORMAT PixelFormat;
    CKDWORD BitsPerPixel;
    CKBOOL SamplerBaseValid;
    VxImageDescEx AutoMipBaseDesc;
    CKBOOL AutoMipBaseValid;
    CKBYTE ReadbackOrientation[CKBGFX_MAX_TRACKED_MIPS];
};

struct CKBgfxFrameBufferRecord {
    bgfx::FrameBufferHandle Handle;
    CKDWORD ColorCount;
    CKFrameBufferAttachmentDesc Color[CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS];
};

struct CKBgfxOcclusionQueryRecord {
    bgfx::OcclusionQueryHandle Handle;
};

struct CKBgfxIndirectBufferRecord {
    bgfx::IndirectBufferHandle Handle;
    CKDWORD MaxCommands;
};

template <typename T>
struct CKBgfxResourceSlot {
    CKBgfxResourceSlot() : Record(NULL), Generation(0), Retired(FALSE) {}

    T *Record;
    CKWORD Generation;
    CKBOOL Retired;
};

// ===========================================================================
// CKBgfxRasterizer
// ===========================================================================

class CKBgfxRasterizer : public CKRasterizer {
public:
    CKBgfxRasterizer();
    ~CKBgfxRasterizer() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
};

// ===========================================================================
// CKBgfxRasterizerDriver
// ===========================================================================

class CKBgfxRasterizerDriver : public CKRasterizerDriver {
public:
    explicit CKBgfxRasterizerDriver(CKBgfxRasterizer *owner);
    ~CKBgfxRasterizerDriver() override;

    CKRasterizerContext *CreateContext() override;
    CKBOOL DestroyContext(CKRasterizerContext *Context) override;
};

struct CKBgfxScreenShotRequest {
    CKDWORD FrameBuffer;
    CKScreenShotCallback Callback;
    void *UserData;
    XString Path;
    CKBOOL UseCaptureFrame;
};

// ===========================================================================
// CKBgfxEncoder
// ===========================================================================

class CKBgfxEncoder : public CKRasterizerEncoder {
public:
    CKBgfxEncoder();
    ~CKBgfxEncoder() override;
    CKERROR GetStatus() const override;

    void SetState(CKDrawState State) override;
    void SetStencilRef(CKDWORD Ref) override;
    void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask) override;
    void SetScissor(const CKRECT *Rect) override;
    void SetPointSize(float Size) override;

    void SetTransform(CKDWORD TransformIndex, CKDWORD Count) override;

    void SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                         CKDWORD StartVertex, CKDWORD VertexCount,
                         CKDWORD Layout) override;
    void SetIndexBuffer(CKDWORD Buffer,
                        CKDWORD StartIndex, CKDWORD IndexCount) override;
    void SetInstanceBuffer(CKDWORD Stream, CKDWORD Buffer,
                           CKDWORD StartInstance, CKDWORD InstanceCount) override;
    void SetTransientVertexBuffer(CKDWORD Stream,
                                  CKTransientVertexBuffer *Buffer) override;
    void SetTransientIndexBuffer(CKTransientIndexBuffer *Buffer) override;
    void SetTransientInstanceBuffer(CKDWORD Stream,
                                    CKTransientInstanceBuffer *Buffer) override;

    void SetTexture(CKDWORD Stage, CKDWORD Uniform,
                    CKDWORD Texture, CKSamplerDesc *Sampler) override;
    void SetUniform(CKDWORD Uniform, const void *Data, CKDWORD Count) override;
    void Discard(CKDWORD Flags) override;
    void SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                          CK_ACCESS_MODE Access) override;
    void SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                         CKDWORD Mip, CK_ACCESS_MODE Access) override;

    void SetCondition(CKDWORD Query, CKBOOL Visible) override;

    void SetMarker(CKSTRING Name) override;
    CKBOOL ConsumeMarker(char *Buffer, CKDWORD BufferSize) override;

    void Submit(CKRenderView View, CKDWORD Program,
                CKDWORD Depth, CKDWORD Flags) override;
    void SubmitOcclusionQuery(CKRenderView View, CKDWORD Program,
                              CKDWORD Query, CKDWORD Depth,
                              CKDWORD Flags) override;
    void SubmitIndirect(CKRenderView View, CKDWORD Program,
                        CKDWORD IndirectBuffer,
                        CKDWORD Start, CKDWORD Count,
                        CKDWORD Depth, CKDWORD Flags) override;

    void Dispatch(CKRenderView View, CKDWORD Program,
                  CKDWORD NumX, CKDWORD NumY, CKDWORD NumZ,
                  CKDWORD Flags) override;
    void DispatchIndirect(CKRenderView View, CKDWORD Program,
                          CKDWORD IndirectBuffer,
                          CKDWORD Start, CKDWORD Count,
                          CKDWORD Flags) override;

    void Touch(CKRenderView View) override;
    void Blit(CKRenderView View,
              CKDWORD DstTexture, CKDWORD DstMip,
              CKDWORD DstX, CKDWORD DstY,
              CKDWORD SrcTexture, CKDWORD SrcMip,
              const CKRECT *SrcRect) override;

    std::atomic<CKBOOL> m_Active;
    CKBgfxRasterizerContext *m_Context;
    bgfx::Encoder *m_Encoder;
    CKBOOL m_OwnsNativeEncoder;
    CKERROR m_Status;
    CKERROR m_FrameStatus;
    XUINTPTR m_OwnerThread;

    CKDWORD m_StencilRef;
    CKDWORD m_StencilReadMask;
    CKDWORD m_StencilWriteMask;
    CKDWORD m_CurrentLayout;
    CKDWORD m_PointSize;
    CKDrawState m_CachedDrawState;
    uint64_t m_CachedBgfxState;
    char m_LastMarker[512];
    CKBgfxDrawMapVertexBinding m_DebugVertexBindings[CKRST_MAX_VERTEX_STREAMS];
    CKBgfxDrawMapTextureBinding m_DebugTextureBindings[CKRST_MAX_TEXTURE_STAGES];
    CKDWORD m_DebugVertexBindingMask;
    CKDWORD m_DebugTextureBindingMask;
    CKDWORD m_DebugIndexBuffer;
    CKDWORD m_DebugIndexStart;
    CKDWORD m_DebugIndexCount;
    CKDWORD m_DebugIndexHandle;
    CKDWORD m_DebugSpecializationHash;
    CKBOOL m_DebugSpecializationValid;

    void TraceSubmit(CKSTRING Kind,
                     CKRenderView View,
                     CKDWORD Program,
                     bgfx::ProgramHandle ProgramHandle,
                     CKDWORD Depth,
                     CKDWORD Flags,
                     CKDWORD Extra0,
                     CKDWORD Extra1,
                     CKDWORD Extra2);
    void ResetDebugBindings(CKDWORD Flags);
    void SetError(CKERROR Error, CKSTRING Operation = NULL);
    CKBOOL CanSubmit();
};

// ===========================================================================
// CKBgfxRasterizerContext
// ===========================================================================

class CKBgfxRasterizerContext : public CKRasterizerContext {
    friend class CKBgfxEncoder;
    friend class CKBgfxCallback;
    friend class CKBgfxRasterizerDriver;
public:
    explicit CKBgfxRasterizerContext(CKBgfxRasterizerDriver *driver);
    ~CKBgfxRasterizerContext() override;

    CKERROR Create(WIN_HANDLE Window, int PosX, int PosY,
                   int Width, int Height, int Bpp,
                   CKBOOL Fullscreen, int RefreshRate,
                   int Zbpp, int StencilBpp) override;
    CKERROR Resize(int PosX, int PosY, int Width, int Height,
                   CKDWORD Flags) override;
    CKERROR GetTargetDesc(CKRasterizerTargetDesc *Target) const override;
    CKBOOL IsIdle() const override;
    CKERROR BeginShutdown() override;
    CKERROR GetDeviceStatus() const override;
    CKERROR GetCaps(CKRasterizerCapsDesc *Caps) const override;
    CKERROR GetTextureFormatCaps(VX_PIXELFORMAT Format,
                                 CKTextureFormatCaps *Caps) const override;
    CKERROR GetDepthFormatCaps(CK_DEPTH_FORMAT Format,
                               CKDepthFormatCaps *Caps) const override;

    // Resource creation
    CKERROR CreateVertexBuffer(const CKVertexBufferDesc *Desc,
                               const void *Data, CKDWORD *OutBuffer) override;
    CKERROR CreateIndexBuffer(const CKIndexBufferDesc *Desc,
                              CKBOOL Index32, const void *Data,
                              CKDWORD *OutBuffer) override;
    CKERROR CreateTexture(const CKTextureDesc *Desc,
                          const VxImageDescEx *Data,
                          CKDWORD *OutTexture) override;
    CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *OutShader) override;
    CKERROR CreateProgram(const CKProgramDesc *Desc, CKDWORD *OutProgram) override;
    CKERROR CreateUniform(const CKUniformDesc *Desc, CKDWORD *OutUniform) override;
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc,
                               CKDWORD *OutLayout) override;
    CKERROR CreateFrameBuffer(const CKFrameBufferDesc *Desc,
                              CKDWORD *OutFrameBuffer) override;
    CKERROR CreateDepthTexture(const CKDepthTextureDesc *Desc,
                               CKDWORD *OutTexture) override;
    CKERROR CreateOcclusionQuery(const CKOcclusionQueryDesc *Desc,
                                 CKDWORD *OutQuery) override;
    CKERROR CreateIndirectBuffer(const CKIndirectBufferDesc *Desc,
                                 CKDWORD *OutBuffer) override;
    CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const override;
    CKERROR DeleteObject(CKDWORD Object, CKDWORD Type) override;
    CKERROR FlushObjects(CKDWORD TypeMask) override;

    // Resource update
    CKERROR UpdateVertexBuffer(CKDWORD Buffer, CKDWORD Offset,
                               CKDWORD Size, const void *Data) override;
    CKERROR UpdateIndexBuffer(CKDWORD Buffer, CKDWORD Offset,
                              CKDWORD Size, const void *Data) override;
    CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face,
                          const CKRECT *Region, const VxImageDescEx *Data) override;

    // Readback
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip,
                        CKReadbackDesc *Readback,
                        CKDWORD *AvailableFrame) override;

    // Occlusion query results
    CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD Query,
                                            CKDWORD *PixelCount) override;

    // Palette
    CKERROR SetPaletteColor(CKDWORD Index, CKDWORD RGBA) override;

    // Debug text overlay
    void DbgTextClear(CKDWORD Color, CKBOOL Small) override;
    void DbgTextPrintf(CKWORD X, CKWORD Y, CKDWORD Attr,
                       CKSTRING Format, ...) override;
    void DbgTextImage(CKWORD X, CKWORD Y, CKWORD Width, CKWORD Height,
                      const void *Data, CKWORD Pitch) override;

    // Debug flags
    void SetDebug(CKDWORD Flags) override;

    // Statistics
    const CKRenderStats *GetStats() override;
#ifdef CKRE_ENABLE_TEST_ACCESS
    CKDWORD GetFatalCountForTests() const { return m_DebugFatalCount.load(std::memory_order_relaxed); }
    void InjectFatalForTests() { LatchFatalError(CKERR_INVALIDRENDERCONTEXT); }
    CKDWORD GetInvalidSubmitCountForTests() const { return m_DebugInvalidSubmitCount.load(std::memory_order_relaxed); }
    CKDWORD GetEncoderLeakCountForTests() const { return m_DebugEncoderLeakCount.load(std::memory_order_relaxed); }
    CKDWORD GetTransientAllocMissCountForTests() const { return m_DebugTransientAllocMissCount.load(std::memory_order_relaxed); }
    CKDWORD FindUniformSlotByHandleForTests(uint16_t BgfxIdx) { return FindUniformSlotByHandle(BgfxIdx); }
    void InjectUniformRecordForTests(CKDWORD Slot, uint16_t BgfxIdx, CK_UNIFORM_TYPE Type,
                                     CKDWORD Count, const char *Name)
    {
        VxMutexLock lock(m_ResourceTableMutex);
        auto *rec = new CKBgfxUniformRecord();
        rec->Handle.idx = BgfxIdx;
        rec->Type = Type;
        rec->Count = Count;
        strncpy(rec->Name, Name ? Name : "", sizeof(rec->Name) - 1);
        rec->Name[sizeof(rec->Name) - 1] = '\0';
        while (m_Uniforms.Size() <= (int)Slot)
            m_Uniforms.PushBack(CKBgfxResourceSlot<CKBgfxUniformRecord>());
        delete m_Uniforms[Slot].Record;
        m_Uniforms[Slot].Record = rec;
        m_Uniforms[Slot].Generation = 1;
    }
#endif

    // Resource naming
    void SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name) override;

    // Shader reflection
    CKDWORD GetShaderUniforms(CKDWORD Shader, CKDWORD *Uniforms,
                               CKDWORD MaxCount) override;
    void GetUniformInfo(CKDWORD Uniform, CKUniformInfo *Info) override;

    // Framebuffer queries
    CKDWORD GetFrameBufferTexture(CKDWORD FrameBuffer,
                                   CKDWORD Attachment) override;

    // Resource validation
    CKBOOL IsTextureValid(CKDWORD Depth, CKBOOL CubeMap, CKWORD NumLayers,
                          CKDWORD Format, CKDWORD Flags) override;
    CKBOOL IsFrameBufferValid(CKDWORD ColorCount,
                              const CKFrameBufferAttachmentDesc *Color,
                              const CKFrameBufferAttachmentDesc *DepthStencil) override;

    // Texture info
    void CalcTextureSize(CKTextureInfo *Info, CKWORD Width, CKWORD Height,
                         CKWORD Depth, CKBOOL CubeMap, CKBOOL HasMips,
                         CKWORD NumLayers, CKDWORD Format) override;

    // Screenshot capture
    CKERROR RequestScreenShot(CKDWORD FrameBuffer,
                              CKScreenShotCallback Callback,
                              void *UserData = NULL) override;
    CKERROR CancelScreenShots(void *UserData) override;

    // Render views
    CKERROR SetViewName(CKRenderView View, CKSTRING Name) override;
    CKERROR SetViewRect(CKRenderView View, const CKRECT &Rect) override;
    CKERROR SetViewScissor(CKRenderView View, const CKRECT *Rect) override;
    CKERROR SetViewClear(CKRenderView View, CKDWORD Flags,
                         CKDWORD Color, float Z, CKDWORD Stencil) override;
    CKERROR SetViewTransform(CKRenderView View,
                             const VxMatrix *ViewMatrix,
                             const VxMatrix *ProjMatrix) override;
    CKERROR SetViewFrameBuffer(CKRenderView View, CKDWORD FrameBuffer) override;
    CKERROR SetViewMode(CKRenderView View, CK_VIEW_MODE Mode) override;
    CKERROR SetViewOrder(CKRenderView Start, CKWORD Count,
                         const CKRenderView *Order) override;
    CKERROR ResetView(CKRenderView View) override;
    CKERROR TouchView(CKRenderView View) override;
    CKERROR SetAntialias(CKDWORD Samples) override;

    // Transform cache
    CKDWORD AllocTransform(VxMatrix *Transform, CKDWORD Count) override;

    // Transient buffers
    CKBOOL AllocTransientVertexBuffer(CKTransientVertexBuffer *Buffer,
                                      CKDWORD VertexCount, CKDWORD Layout) override;
    CKBOOL AllocTransientIndexBuffer(CKTransientIndexBuffer *Buffer,
                                     CKDWORD IndexCount, CKBOOL Index32) override;
    CKBOOL AllocTransientInstanceBuffer(CKTransientInstanceBuffer *Buffer,
                                        CKDWORD InstanceCount, CKDWORD Layout) override;
    CKDWORD GetAvailTransientVertexBuffer(CKDWORD VertexCount, CKDWORD Layout) override;
    CKDWORD GetAvailTransientIndexBuffer(CKDWORD IndexCount, CKBOOL Index32) override;
    CKDWORD GetAvailTransientInstanceBuffer(CKDWORD InstanceCount, CKDWORD Layout) override;

    // Encoder and frame
    CKRasterizerEncoder *BeginEncoder(CKBOOL ForceNewEncoder = FALSE) override;
    CKERROR EndEncoder(CKRasterizerEncoder *Encoder) override;
    CKERROR Frame(CKRST_FRAME_SYNC_MODE SyncMode,
                  CKDWORD Flags = CKRST_FRAME_NONE,
                  CKDWORD *FrameNumber = NULL) override;

    CKBgfxShaderRecord *GetShader(CKDWORD Handle);
    CKBgfxProgramRecord *GetProgram(CKDWORD Handle);
    CKBgfxUniformRecord *GetUniform(CKDWORD Handle);
    CKBgfxVertexLayoutRecord *GetVertexLayout(CKDWORD Handle);
    CKBgfxVertexBufferRecord *GetVertexBuffer(CKDWORD Handle);
    CKBgfxIndexBufferRecord *GetIndexBuffer(CKDWORD Handle);
    CKBgfxTextureRecord *GetTexture(CKDWORD Handle);
    CKBgfxFrameBufferRecord *GetFrameBuffer(CKDWORD Handle);
    CKBgfxOcclusionQueryRecord *GetOcclusionQuery(CKDWORD Handle);
    CKBgfxIndirectBufferRecord *GetIndirectBuffer(CKDWORD Handle);

private:
    friend class CKBgfxCallback;

    CKBOOL IsApiThread() const
    {
        return VxThread::GetCurrentVxThreadId() == m_ApiThreadId ? TRUE : FALSE;
    }
    void LatchFatalError(CKERROR Error);
    CKBOOL TryBeginShutdown();
    void BeginForcedShutdown();
    void EndCurrentThreadEncoders();
    CKDWORD FindUniformSlotByHandle(uint16_t BgfxIdx);
    CKBOOL HasActiveEncoders() const;
    CKBOOL HasCaptureFrameScreenShots();
    CKERROR BuildFrameBufferAttachments(
        CKDWORD ColorCount,
        const CKFrameBufferAttachmentDesc *Color,
        const CKFrameBufferAttachmentDesc *DepthStencil,
        bgfx::Attachment *Attachments,
        CKDWORD Capacity,
        CKDWORD &AttachmentCount);

    CKBOOL m_BgfxInitialized;
    const char *m_RendererName;
    bgfx::RendererType::Enum m_RendererType;
    CKRasterizerTargetDesc m_TargetDesc;
    CKRasterizerCapsDesc m_CapsDesc;
    uint64_t m_NativeSupported;
    uint32_t m_NativeFormatCaps[bgfx::TextureFormat::Count];
    bgfx::TextureHandle m_DefaultWhiteTexture;
    CKBOOL m_VSync;
    uint32_t m_ResetFlags;
    CKDWORD m_AntialiasSamples;
    CKBgfxCallback m_BgfxCallback;
    CKBgfxEncoder m_DefaultEncoder;
    CKBgfxEncoder m_Encoders[CKRST_MAX_ENCODERS];
    XUINTPTR m_ApiThreadId;
    mutable VxMutex m_EncoderLifecycleMutex;
    CKBOOL m_FrameInProgress;
    std::atomic<CKBOOL> m_ShuttingDown;

    VxMutex m_ScreenShotMutex;
    XArray<CKBgfxScreenShotRequest> m_PendingScreenShots;
    uint64_t m_NextScreenShotToken;
    CKDWORD m_CaptureWidth;
    CKDWORD m_CaptureHeight;
    CKDWORD m_CapturePitch;
    bgfx::TextureFormat::Enum m_CaptureFormat;
    CKBOOL m_CaptureYFlip;

    CKDWORD m_DebugFrameId;
    std::atomic<CKDWORD> m_DebugSubmitSerial;
    std::atomic<CKDWORD> m_DebugViewSubmitSerial[CKRST_MAX_RENDER_VIEWS];
    std::atomic<CKDWORD> m_DebugMissingAnnotationCount;
    std::atomic<CKDWORD> m_DebugMarkerOverwriteCount;
    std::atomic<CKDWORD> m_DebugMarkerStaleCount;
    std::atomic<CKDWORD> m_DebugInvalidSubmitCount;
    std::atomic<CKDWORD> m_DebugFatalCount;
    std::atomic<CKERROR> m_FatalError;
    std::atomic<CKDWORD> m_DebugParsedAnnotationCount;
    std::atomic<CKDWORD> m_DebugRawPrimitiveCount;
    std::atomic<CKDWORD> m_DebugSourceSubmitCount[CKBGFX_DRAWMAP_SOURCE_COUNT];
    std::atomic<CKDWORD> m_DebugEncoderLeakCount;
    std::atomic<CKDWORD> m_DebugTransientAllocMissCount;
    CK_VIEW_MODE m_DebugViewMode[CKRST_MAX_RENDER_VIEWS];
    char m_DebugViewName[CKRST_MAX_RENDER_VIEWS][64];
    CKDWORD m_DebugViewOrderGeneration;
    CKBOOL m_DebugViewOrderSequential;
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

    void ConfigureDebug();
    void DrawDebugOverlay();
    void TraceTextureMap(CKSTRING Event, CKDWORD Texture,
                         const CKBgfxTextureRecord *Record);
    void TraceProgramMap(CKSTRING Event, CKDWORD Program,
                         const CKBgfxProgramRecord *Record);
    void TraceBufferMap(CKSTRING Event, CKSTRING Kind, CKDWORD Buffer,
                        CKDWORD BgfxHandle, CKDWORD Layout,
                        CKDWORD Stride, CKDWORD Count,
                        CKDWORD Index32, CKDWORD Flags);
    void RecordInvalidSubmit(CKSTRING Kind, CKRenderView View, CKDWORD Program, CKSTRING Reason);
    void RecordTransientAllocMiss(const char *Kind, CKDWORD Requested, CKDWORD Available);
    void RecordViewColorWrite(CKRenderView View, CKBOOL ExecuteClear);
    void RecordTextureWrite(CKBgfxTextureRecord *Texture, CKDWORD Mip,
                            CKBgfxTextureOrientation Orientation,
                            CKBOOL FullOverwrite);
    void RecordTextureBlit(CKBgfxTextureRecord *Destination, CKDWORD DestinationMip,
                           const CKBgfxTextureRecord *Source, CKDWORD SourceMip,
                           CKBOOL FullOverwrite);

    XArray<CKBgfxResourceSlot<CKBgfxShaderRecord> > m_Shaders;
    XArray<CKBgfxResourceSlot<CKBgfxProgramRecord> > m_Programs;
    XArray<CKBgfxResourceSlot<CKBgfxUniformRecord> > m_Uniforms;
    XArray<CKBgfxResourceSlot<CKBgfxVertexLayoutRecord> > m_VertexLayouts;
    XArray<CKBgfxResourceSlot<CKBgfxVertexBufferRecord> > m_VertexBuffers;
    XArray<CKBgfxResourceSlot<CKBgfxIndexBufferRecord> > m_IndexBuffers;
    XArray<CKBgfxResourceSlot<CKBgfxTextureRecord> > m_Textures;
    XArray<CKBgfxResourceSlot<CKBgfxFrameBufferRecord> > m_FrameBuffers;
    XArray<CKBgfxResourceSlot<CKBgfxOcclusionQueryRecord> > m_OcclusionQueries;
    XArray<CKBgfxResourceSlot<CKBgfxIndirectBufferRecord> > m_IndirectBuffers;

    mutable VxMutex m_ResourceTableMutex;
    VxMutex m_ResourceStateMutex;
    CKDWORD m_ViewFrameBuffer[CKRST_MAX_RENDER_VIEWS];
    CKRECT m_ViewRect[CKRST_MAX_RENDER_VIEWS];
    CKDWORD m_ViewClearFlags[CKRST_MAX_RENDER_VIEWS];
    CKBOOL m_ViewClearRecorded[CKRST_MAX_RENDER_VIEWS];

    CKRenderStats m_Stats{};
    XArray<CKRenderViewStats> m_ViewStatsCache;

    VxMatrix m_TransformCache[CKRST_MAX_TRANSFORMS];
    std::atomic<CKDWORD> m_TransformCount;

    static const int MAX_TRANSIENT_VB = 256;
    static const int MAX_TRANSIENT_IB = 256;
    static const int MAX_TRANSIENT_INST = 256;
    VxMutex m_TransientPoolMutex;
    bgfx::TransientVertexBuffer m_TransientVBPool[MAX_TRANSIENT_VB];
    std::atomic<CKDWORD> m_TransientVBCount;
    bgfx::TransientIndexBuffer m_TransientIBPool[MAX_TRANSIENT_IB];
    std::atomic<CKDWORD> m_TransientIBCount;
    bgfx::InstanceDataBuffer m_TransientInstPool[MAX_TRANSIENT_INST];
    std::atomic<CKDWORD> m_TransientInstCount;
};

#endif // CKBGFXRASTERIZER_H
