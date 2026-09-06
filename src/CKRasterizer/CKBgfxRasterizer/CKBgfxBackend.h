#ifndef CKBGFXBACKEND_H
#define CKBGFXBACKEND_H

// bgfx implementation of CKRasterizerBackend (spec 5.10). One backend per
// bgfx instance: passes are sequential bgfx views, draws carry the sticky
// pipeline state and slot bindings. Program metadata owns named uniforms;
// submission maps to bgfx::frame(). Everything runs on the API thread.

#include "CKRasterizerBackend.h"
#include "CKBackendProgram.h"
#include "CKBgfxBorderPalette.h"

#include <atomic>
#include <map>
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
class CKBgfxResources;
struct CKBgfxBackendTestAccess;
struct CKBgfxShaderRecord;
struct CKBgfxProgramRecord;
struct CKBgfxVertexLayoutRecord;
struct CKBgfxVertexBufferRecord;
struct CKBgfxIndexBufferRecord;
struct CKBgfxTextureRecord;
struct CKBgfxFrameBufferRecord;
enum CKBgfxTextureOrientation : int;

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
// CKBgfxBackend
// ===========================================================================

class CKBgfxBackend : public CKRasterizerBackend {
    friend class CKBgfxCallback;
public:
    CKBgfxBackend();
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
    CKERROR UpdateBuffer(CKBackendBufferKind Kind, CKDWORD Buffer, CKDWORD Offset,
                         CKDWORD Size, const void *Data) override;
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateProgram(const CKBackendProgramDesc *Desc, CKDWORD *Out) override;
    CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const override;
    CKERROR DestroyObject(CKDWORD Object, CKDWORD Type) override;
    void SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name) override;

    // --- Frame
    CKERROR BeginPass(const CKBackendPassDesc *Desc) override;
    CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out) override;
    CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out) override;
    CKERROR Draw(const CKBackendDraw *Draw) override;
    CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                 CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect) override;
    CKERROR Submit(const CKBackendSubmitDesc &Desc, CKDWORD *FrameNumber) override;

    // --- Readback
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKBackendReadbackTicket *Ticket) override;

    // --- Misc
    uint64_t GetDrawApproximationMask() const override { return m_DrawApproximations; }
    const CKBackendStats &GetStats() const override { return m_Stats; }
    const CKBackendDeviceLimits &GetDeviceLimits() const { return m_CapsDesc; }
    const char *GetRendererName() const { return m_RendererName; }
    CKERROR GetTextureFormatCaps(VX_PIXELFORMAT Format, CKTextureFormatCaps *Caps) const;

    CKBgfxShaderRecord *GetShader(CKDWORD Handle);
    CKBgfxProgramRecord *GetProgram(CKDWORD Handle);
    CKBgfxVertexLayoutRecord *GetVertexLayout(CKDWORD Handle);
    CKBgfxVertexBufferRecord *GetVertexBuffer(CKDWORD Handle);
    CKBgfxIndexBufferRecord *GetIndexBuffer(CKDWORD Handle);
    CKBgfxTextureRecord *GetTexture(CKDWORD Handle);
    CKBgfxFrameBufferRecord *GetFrameBuffer(CKDWORD Handle);

private:
    friend struct CKBgfxBackendTestAccess;


    static const int MAX_TRANSIENT_VB = 256;
    static const int MAX_TRANSIENT_IB = 256;

    CKBOOL IsApiThread() const
    {
        return VxThread::GetCurrentVxThreadId() == m_ApiThreadId ? TRUE : FALSE;
    }
    CKBOOL IsReady() const { return m_BgfxInitialized && m_Created && IsApiThread() ? TRUE : FALSE; }
    void LatchFatalError(CKERROR Error);
    void ReleaseBgfx();
    CKERROR PrepareRectClear();
    void EncodeRectClear(bgfx::ViewId View, const CKBackendPassDesc &Desc);
    void ReleaseRectClear();
    bgfx::ProgramHandle m_RectClearProgram = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_RectClearVertices = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_RectClearColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_RectClearDepth = BGFX_INVALID_HANDLE;
    CKRECT WindowPixelRect(const CKRECT &rect) const;
    CKERROR BuildFrameBufferAttachments(const CKBackendRenderTargetDesc *Desc, bgfx::Attachment *Attachments,
                                        CKDWORD Capacity, CKDWORD &AttachmentCount);
    CKERROR CreateVertexBufferRecord(CKDWORD VertexSize, CKDWORD VertexCount, CKDWORD Layout,
                                     const void *Data, CKDWORD *OutBuffer);
    CKERROR CreateIndexBufferRecord(CKDWORD IndexCount, CKBOOL Index32, const void *Data, CKDWORD *OutBuffer);
    CKERROR UploadTextureOrdered(bgfx::TextureHandle Texture, bgfx::TextureFormat::Enum Format,
                                 CKDWORD Mip, CKDWORD X, CKDWORD Y, CKDWORD Layer, CKDWORD Width, CKDWORD Height,
                                 const bgfx::Memory *Data, CKBOOL Cube = FALSE, CKBOOL Volume = FALSE);
    CKERROR UpdateVertexBufferRecord(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data);
    CKERROR UpdateIndexBufferRecord(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data);

    // Draw submission
    CKERROR DrawFailed(CKERROR Error, const char *Operation);
    CKERROR ApplyPipelineState(const CKBackendPipelineState &state);
    CKERROR BindGeometry(const CKBackendDraw *Draw);
    std::shared_ptr<bgfx::TextureHandle> GetDefaultTexture(const CKBackendSamplerBinding &Binding);
    CKERROR BindTextureSlot(const CKBackendSamplerBinding &Binding, bgfx::UniformHandle Uniform,
                            bgfx::TextureHandle DefaultTexture, CKDWORD Texture, const CKSamplerDesc *Sampler);
    void ResetDebugBindings();
    void TraceSubmit(CKDWORD Program, bgfx::ProgramHandle ProgramHandle, CKDWORD Depth, const CKBackendPipelineState &state);

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

    CKBOOL m_BgfxInitialized;
    CKBOOL m_Created;
    WIN_HANDLE m_Window;
    int m_PosX;
    int m_PosY;
    CKDWORD m_Width;
    CKDWORD m_Height;
    CKDWORD m_DrawableWidth;
    CKDWORD m_DrawableHeight;
    CKBOOL m_Fullscreen;
    const char *m_RendererName;
    bgfx::RendererType::Enum m_RendererType;
    struct Readback : CKBackendReadback {
        CKDWORD AvailableFrame = 0;
        bgfx::TextureHandle Snapshot = BGFX_INVALID_HANDLE;
    };
    std::vector<std::shared_ptr<Readback>> m_Readbacks;
    CKBgfxBorderPalette m_BorderPalette;
    uint64_t m_DrawApproximations = 0;
    CKBackendCaps m_Caps;
    CKBackendStats m_Stats;
    CKBackendDeviceLimits m_CapsDesc;   // bgfx limits and CKRST_DEVCAPS_* features
    uint64_t m_NativeSupported;
    uint32_t m_NativeFormatCaps[bgfx::TextureFormat::Count];
    std::vector<CKBYTE> m_ConstantData[CKBACKEND_MAX_CONSTANT_SLOTS];
    std::map<uint64_t, std::weak_ptr<bgfx::TextureHandle>> m_DefaultTextures;
    CKBOOL m_VSync;
    uint32_t m_ResetFlags;
    CKBgfxCallback m_BgfxCallback;
    XUINTPTR m_ApiThreadId;
    std::atomic<CKBOOL> m_ShuttingDown;
    std::atomic<CKERROR> m_FatalError;

    // Frame
    CKBOOL m_FrameInProgress;        // a pass was begun since the last Present
    CKBOOL m_PassOpen;
    CKBackendPassDesc m_LogicalPass;
    bool m_DrawPassNeedsResume = false;
    bgfx::ViewId m_CurrentView;
    CKDWORD m_NextView;
    CKDWORD m_LastFrameViewCount;
    CKDWORD m_FramePasses, m_FrameDraws, m_FrameBlits, m_FrameTextureUploads, m_FrameBufferUploads;
    CKDWORD m_ViewFrameBuffer[CKRST_MAX_PASSES];
    CKRECT m_ViewRect[CKRST_MAX_PASSES];
    CKDWORD m_ViewClearFlags[CKRST_MAX_PASSES];
    CKBOOL m_ViewClearRecorded[CKRST_MAX_PASSES];

    // Draw state
    CKDrawState m_CachedDrawState;
    uint64_t m_CachedBgfxState;
    CKDWORD m_PointSize;
    CKDWORD m_CurrentLayout;
    char m_LastMarker[512];
    CKBgfxDrawMapVertexBinding m_DebugVertexBindings[CKRST_MAX_VERTEX_STREAMS];
    CKBgfxDrawMapTextureBinding m_DebugTextureBindings[CKBACKEND_MAX_TEXTURE_SLOTS];
    CKDWORD m_DebugVertexBindingMask;
    CKDWORD m_DebugTextureBindingMask;
    CKDWORD m_DebugIndexBuffer;
    CKDWORD m_DebugIndexStart;
    CKDWORD m_DebugIndexCount;
    CKDWORD m_DebugIndexHandle;
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
    CKBgfxResources *m_Resources;
    VxMutex m_ResourceStateMutex;

    // Transient geometry of the current frame
    bgfx::TransientVertexBuffer m_TransientVBPool[MAX_TRANSIENT_VB];
    CKDWORD m_TransientVBCount;
    bgfx::TransientIndexBuffer m_TransientIBPool[MAX_TRANSIENT_IB];
    CKDWORD m_TransientIBCount;
};

#endif // CKBGFXBACKEND_H
