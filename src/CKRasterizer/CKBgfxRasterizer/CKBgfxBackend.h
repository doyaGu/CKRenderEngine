#ifndef CKBGFXBACKEND_H
#define CKBGFXBACKEND_H

// bgfx implementation of CKRasterizerBackend (spec 5.10). One backend per
// bgfx instance: passes are sequential bgfx views, draws carry the sticky
// pipeline state and slot bindings, constant blocks are one bgfx uniform
// each, Present() is bgfx::frame(). Everything runs on the API thread.

#include "CKRasterizerBackend.h"

#include <atomic>
#include <string.h>
#include <bgfx/bgfx.h>

#define CKBGFX_DRAWMAP_SOURCE_COUNT 6
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

class CKBgfxBackend;
class CKBgfxBackendDriver;
class CKBgfxBackendLibrary;

// ===========================================================================
// bgfx Callback Implementation
// ===========================================================================

class CKBgfxCallback : public bgfx::CallbackI {
public:
    CKBgfxCallback() : m_Context(NULL) {}

    void SetContext(CKBgfxBackend *ctx) { m_Context = ctx; }

    void fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char *_str) override;
    void traceVargs(const char *_filePath, uint16_t _line, const char *_format, va_list _argList) override;
    void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t _id) override;
    bool cacheRead(uint64_t _id, void *_data, uint32_t _size) override;
    void cacheWrite(uint64_t _id, const void *_data, uint32_t _size) override;
    // bgfx debug captures (BGFX_DEBUG / screenshot requests from bgfx itself)
    // are written as BMP files; the backend reads pixels through ReadTexture.
    void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                    uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                    const void *_data, uint32_t _size, bool _yflip) override;
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void *, uint32_t) override {}

private:
    CKBgfxBackend *m_Context;
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

struct CKBgfxVertexLayoutRecord {
    bgfx::VertexLayoutHandle Handle;
    bgfx::VertexLayout Layout;
};

struct CKBgfxVertexBufferRecord {
    bgfx::DynamicVertexBufferHandle Handle;
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
static const CKDWORD CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS = 2;

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

// A render target: one colour attachment (2D or a cube face / volume slice)
// plus an optional depth-stencil texture.
struct CKBgfxFrameBufferRecord {
    bgfx::FrameBufferHandle Handle;
    CKBackendRenderTargetDesc Desc;
};

template <typename T>
struct CKBgfxResourceSlot {
    CKBgfxResourceSlot() : Record(NULL), Generation(0), Retired(FALSE) {}

    T *Record;
    CKWORD Generation;
    CKBOOL Retired;
};

// ===========================================================================
// CKBgfxBackendLibrary / CKBgfxBackendDriver
// ===========================================================================

class CKBgfxBackendLibrary : public CKRasterizerBackendLibrary {
public:
    CKBgfxBackendLibrary();
    ~CKBgfxBackendLibrary() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
    int GetDriverCount() const override { return m_Drivers.Size(); }
    CKRasterizerBackendDriver *GetDriver(CKDWORD Index) const override;
    WIN_HANDLE GetMainWindow() const override { return m_MainWindow; }

private:
    WIN_HANDLE m_MainWindow;
    XArray<CKBgfxBackendDriver *> m_Drivers;
};

// The single bgfx adapter: display modes from SDL, caps from the baseline
// (spec 4.9.2) lowered to the bgfx limits once a backend is initialised.
class CKBgfxBackendDriver : public CKRasterizerBackendDriver {
public:
    explicit CKBgfxBackendDriver(CKBgfxBackendLibrary *owner);
    ~CKBgfxBackendDriver() override;

    CKRasterizerBackend *CreateBackend() override;
    CKBOOL DestroyBackend(CKRasterizerBackend *Backend) override;

    CKBgfxBackendLibrary *GetOwner() const { return m_Owner; }
    int GetBackendCount() const { return m_Backends.Size(); }

private:
    CKBgfxBackendLibrary *m_Owner;
    XArray<CKBgfxBackend *> m_Backends;
};

// ===========================================================================
// CKBgfxBackend
// ===========================================================================

class CKBgfxBackend : public CKRasterizerBackend {
    friend class CKBgfxCallback;
    friend class CKBgfxBackendDriver;
public:
    explicit CKBgfxBackend(CKBgfxBackendDriver *driver);
    ~CKBgfxBackend() override;

    // --- Device
    CKERROR Init(const CKBackendInitDesc *Desc) override;
    void Shutdown() override;
    CKERROR Resize(int PosX, int PosY, int Width, int Height) override;
    CKERROR GetDeviceStatus() const override;
    const CKBackendCaps &GetCaps() const override { return m_Caps; }
    CKBOOL IsIdle() const override;
    void SetDebugFlags(CKDWORD Flags) override;

    // --- Resources
    CKERROR CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out) override;
    CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                          const VxImageDescEx *Data) override;
    CKERROR CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out) override;
    CKERROR UpdateBuffer(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data) override;
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateProgram(CKDWORD VertexShader, CKDWORD PixelShader, CKDWORD *Out) override;
    CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const override;
    CKERROR DestroyObject(CKDWORD Object, CKDWORD Type) override;
    void SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name) override;

    // --- Frame
    CKERROR BeginPass(const CKBackendPassDesc *Desc) override;
    void SetPipelineState(const CKBackendPipelineState *State) override;
    void BindTexture(CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler) override;
    CKERROR PushConstants(CKBackendConstantBlock Block, const void *Data, CKDWORD Vec4Count) override;
    void SetMarker(const char *Name) override;
    CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out) override;
    CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out) override;
    CKERROR Draw(const CKBackendDraw *Draw) override;
    CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                 CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect) override;
    CKERROR Present(CKBackendPresentMode Mode, CKDWORD *FrameNumber) override;

    // --- Readback
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKDWORD *AvailableFrame) override;

    // --- Misc
    CKERROR SetPaletteColor(CKDWORD Index, CKDWORD RGBA) override;
    const CKBackendStats &GetStats() const override { return m_Stats; }

#ifdef CKRE_ENABLE_TEST_ACCESS
    CKDWORD GetFatalCountForTests() const { return m_DebugFatalCount.load(std::memory_order_relaxed); }
    void InjectFatalForTests() { LatchFatalError(CKERR_INVALIDRENDERCONTEXT); }
    CKDWORD GetInvalidSubmitCountForTests() const { return m_DebugInvalidSubmitCount.load(std::memory_order_relaxed); }
    CKDWORD GetTransientAllocMissCountForTests() const { return m_DebugTransientAllocMissCount.load(std::memory_order_relaxed); }
#endif

    CKBgfxShaderRecord *GetShader(CKDWORD Handle);
    CKBgfxProgramRecord *GetProgram(CKDWORD Handle);
    CKBgfxVertexLayoutRecord *GetVertexLayout(CKDWORD Handle);
    CKBgfxVertexBufferRecord *GetVertexBuffer(CKDWORD Handle);
    CKBgfxIndexBufferRecord *GetIndexBuffer(CKDWORD Handle);
    CKBgfxTextureRecord *GetTexture(CKDWORD Handle);
    CKBgfxFrameBufferRecord *GetFrameBuffer(CKDWORD Handle);

private:
    struct SlotBinding {
        CKDWORD Texture;
        CKSamplerDesc Sampler;
        CKBOOL HasSampler;
        CKBOOL PendingZero;   // explicit zero binding requested for the next draw
    };

    static const int MAX_TRANSIENT_VB = 256;
    static const int MAX_TRANSIENT_IB = 256;

    CKBOOL IsApiThread() const
    {
        return VxThread::GetCurrentVxThreadId() == m_ApiThreadId ? TRUE : FALSE;
    }
    CKBOOL IsReady() const { return m_BgfxInitialized && m_Created && IsApiThread() ? TRUE : FALSE; }
    void LatchFatalError(CKERROR Error);
    CKBOOL CreateUniforms();
    void DestroyUniforms();
    void ReleaseBgfx();
    CKERROR GetTextureFormatCaps(VX_PIXELFORMAT Format, CKTextureFormatCaps *Caps) const;
    CKERROR BuildFrameBufferAttachments(const CKBackendRenderTargetDesc *Desc, bgfx::Attachment *Attachments,
                                        CKDWORD Capacity, CKDWORD &AttachmentCount);
    CKERROR CreateVertexBufferRecord(CKDWORD VertexSize, CKDWORD VertexCount, CKDWORD Layout,
                                     const void *Data, CKDWORD *OutBuffer);
    CKERROR CreateIndexBufferRecord(CKDWORD IndexCount, CKBOOL Index32, const void *Data, CKDWORD *OutBuffer);
    CKERROR UpdateVertexBufferRecord(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data);
    CKERROR UpdateIndexBufferRecord(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data);

    // Draw submission
    CKERROR DrawFailed(CKERROR Error, const char *Operation);
    CKERROR ApplyPipelineState();
    CKERROR BindGeometry(const CKBackendDraw *Draw);
    CKERROR BindTextureSlot(CKDWORD Stage, CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler);
    void ResetDebugBindings();
    void TraceSubmit(CKDWORD Program, bgfx::ProgramHandle ProgramHandle, CKDWORD Depth);

    // Debug / diagnostics
    void ConfigureDebug();
    void DrawDebugOverlay();
    void TraceTextureMap(CKSTRING Event, CKDWORD Texture, const CKBgfxTextureRecord *Record);
    void TraceProgramMap(CKSTRING Event, CKDWORD Program, const CKBgfxProgramRecord *Record);
    void TraceBufferMap(CKSTRING Event, CKSTRING Kind, CKDWORD Buffer, CKDWORD BgfxHandle, CKDWORD Layout,
                        CKDWORD Stride, CKDWORD Count, CKDWORD Index32, CKDWORD Flags);
    void RecordInvalidSubmit(CKSTRING Kind, bgfx::ViewId View, CKDWORD Program, CKSTRING Reason);
    void RecordTransientAllocMiss(const char *Kind, CKDWORD Requested, CKDWORD Available);
    void RecordViewColorWrite(bgfx::ViewId View, CKBOOL HasDraw);
    void RecordTextureWrite(CKBgfxTextureRecord *Texture, CKDWORD Mip, CKBgfxTextureOrientation Orientation,
                            CKBOOL FullOverwrite);
    void RecordTextureBlit(CKBgfxTextureRecord *Destination, CKDWORD DestinationMip,
                           const CKBgfxTextureRecord *Source, CKDWORD SourceMip, CKBOOL FullOverwrite);

    CKBgfxBackendDriver *m_Driver;
    CKBOOL m_BgfxInitialized;
    CKBOOL m_Created;
    WIN_HANDLE m_Window;
    int m_PosX;
    int m_PosY;
    CKDWORD m_Width;
    CKDWORD m_Height;
    CKBOOL m_Fullscreen;
    const char *m_RendererName;
    bgfx::RendererType::Enum m_RendererType;
    CKBackendCaps m_Caps;
    CKBackendStats m_Stats;
    CKBackendDeviceLimits m_CapsDesc;   // bgfx limits and CKRST_DEVCAPS_* features
    uint64_t m_NativeSupported;
    uint32_t m_NativeFormatCaps[bgfx::TextureFormat::Count];
    bgfx::TextureHandle m_DefaultWhiteTexture;
    bgfx::UniformHandle m_BlockUniforms[CKRST_BLOCK_COUNT];
    bgfx::UniformHandle m_SamplerUniforms[CKRST_BACKEND_SLOT_COUNT];
    CKBOOL m_VSync;
    uint32_t m_ResetFlags;
    CKBgfxCallback m_BgfxCallback;
    XUINTPTR m_ApiThreadId;
    std::atomic<CKBOOL> m_ShuttingDown;
    std::atomic<CKERROR> m_FatalError;

    // Frame
    CKBOOL m_FrameInProgress;        // a pass was begun since the last Present
    CKBOOL m_PassOpen;
    bgfx::ViewId m_CurrentView;
    CKDWORD m_NextView;
    CKDWORD m_LastFrameViewCount;
    CKDWORD m_FramePasses, m_FrameDraws, m_FrameBlits, m_FrameTextureUploads, m_FrameBufferUploads;
    CKDWORD m_ViewFrameBuffer[CKRST_MAX_PASSES];
    CKRECT m_ViewRect[CKRST_MAX_PASSES];
    CKDWORD m_ViewClearFlags[CKRST_MAX_PASSES];
    CKBOOL m_ViewClearRecorded[CKRST_MAX_PASSES];

    // Draw state
    CKBackendPipelineState m_State;
    SlotBinding m_Slots[CKRST_BACKEND_SLOT_COUNT];
    CKDrawState m_CachedDrawState;
    uint64_t m_CachedBgfxState;
    CKDWORD m_PointSize;
    CKDWORD m_CurrentLayout;
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
    CKDWORD m_DrawErrorLogCount;

    // Debug
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

    // Resources
    XArray<CKBgfxResourceSlot<CKBgfxShaderRecord> > m_Shaders;
    XArray<CKBgfxResourceSlot<CKBgfxProgramRecord> > m_Programs;
    XArray<CKBgfxResourceSlot<CKBgfxVertexLayoutRecord> > m_VertexLayouts;
    XArray<CKBgfxResourceSlot<CKBgfxVertexBufferRecord> > m_VertexBuffers;
    XArray<CKBgfxResourceSlot<CKBgfxIndexBufferRecord> > m_IndexBuffers;
    XArray<CKBgfxResourceSlot<CKBgfxTextureRecord> > m_Textures;
    XArray<CKBgfxResourceSlot<CKBgfxFrameBufferRecord> > m_FrameBuffers;
    mutable VxMutex m_ResourceTableMutex;
    VxMutex m_ResourceStateMutex;

    // Transient geometry of the current frame
    bgfx::TransientVertexBuffer m_TransientVBPool[MAX_TRANSIENT_VB];
    CKDWORD m_TransientVBCount;
    bgfx::TransientIndexBuffer m_TransientIBPool[MAX_TRANSIENT_IB];
    CKDWORD m_TransientIBCount;
};

#endif // CKBGFXBACKEND_H
