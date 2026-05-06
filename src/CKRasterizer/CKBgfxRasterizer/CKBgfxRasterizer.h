#ifndef CKBGFXRASTERIZER_H
#define CKBGFXRASTERIZER_H

#include "CKRasterizer.h"

#include <atomic>
#include <mutex>
#include <bgfx/bgfx.h>

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
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void *, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void *, uint32_t) override {}
    void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                    uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                    const void *_data, uint32_t _size, bool _yflip) override;
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void *, uint32_t) override {}

private:
    CKBgfxRasterizerContext *m_Context;
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
    CKDWORD Layout;
    CKDWORD VertexSize;
};

struct CKBgfxIndexBufferRecord {
    bgfx::DynamicIndexBufferHandle Handle;
    CKBOOL Index32;
};

struct CKBgfxTextureRecord {
    bgfx::TextureHandle Handle;
    CKDWORD Flags;
    CKDWORD Width;
    CKDWORD Height;
    CKBOOL IsDepth;
    bgfx::TextureFormat::Enum Format;
    CKDWORD BitsPerPixel;
};

struct CKBgfxFrameBufferRecord {
    bgfx::FrameBufferHandle Handle;
    CKDWORD FirstColorTexture;
};

struct CKBgfxOcclusionQueryRecord {
    bgfx::OcclusionQueryHandle Handle;
};

struct CKBgfxIndirectBufferRecord {
    bgfx::IndirectBufferHandle Handle;
    CKDWORD MaxCommands;
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

    CKERROR GetProgrammableCaps(VxProgCapsDesc &Caps) override;
};

// ===========================================================================
// CKBgfxEncoder
// ===========================================================================

class CKBgfxEncoder : public CKRasterizerEncoder {
public:
    CKBgfxEncoder();
    ~CKBgfxEncoder() override;

    void SetState(CKDrawState State) override;
    void SetStencilRef(CKDWORD Ref) override;
    void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask) override;
    void SetScissor(const CKRECT *Rect) override;
    void SetPointSize(float Size) override;

    void SetTransform(CKDWORD TransformIndex, CKDWORD Count) override;

    void SetVertexLayout(CKDWORD Layout) override;
    void SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                         CKDWORD StartVertex, CKDWORD VertexCount) override;
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

    void SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                          CK_ACCESS_MODE Access) override;
    void SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                         CKDWORD Mip, CK_ACCESS_MODE Access) override;

    void SetCondition(CKDWORD Query, CKBOOL Visible) override;

    void SetMarker(CKSTRING Name) override;

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

    CKDWORD m_StencilRef;
    CKDWORD m_StencilReadMask;
    CKDWORD m_StencilWriteMask;
    CKDWORD m_CurrentLayout;
    CKDWORD m_PointSize;
    CKDrawState m_CachedDrawState;
    uint64_t m_CachedBgfxState;
};

// ===========================================================================
// CKBgfxRasterizerContext
// ===========================================================================

class CKBgfxRasterizerContext : public CKRasterizerContext {
    friend class CKBgfxEncoder;
public:
    explicit CKBgfxRasterizerContext(CKBgfxRasterizerDriver *driver);
    ~CKBgfxRasterizerContext() override;

    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY,
                  int Width, int Height, int Bpp,
                  CKBOOL Fullscreen, int RefreshRate,
                  int Zbpp, int StencilBpp) override;
    CKBOOL Resize(int PosX, int PosY, int Width, int Height,
                  CKDWORD Flags) override;

    // Resource creation
    CKERROR CreateVertexBuffer(CKDWORD Buffer, CKVertexBufferDesc *Desc,
                               const void *Data) override;
    CKERROR CreateIndexBuffer(CKDWORD Buffer, CKIndexBufferDesc *Desc,
                              CKBOOL Index32, const void *Data) override;
    CKERROR CreateTexture(CKDWORD Texture, CKTextureDesc *Desc,
                          const VxImageDescEx *Data) override;
    CKERROR CreateShader(CKDWORD Shader, CKShaderDesc *Desc) override;
    CKERROR CreateProgram(CKDWORD Program, CKProgramDesc *Desc) override;
    CKERROR CreateUniform(CKDWORD Uniform, CKUniformDesc *Desc) override;
    CKERROR CreateVertexLayout(CKDWORD Layout, CKVertexLayoutDesc *Desc) override;
    CKERROR CreateFrameBuffer(CKDWORD FrameBuffer, CKFrameBufferDesc *Desc) override;
    CKERROR CreateDepthTexture(CKDWORD Texture, CKDepthTextureDesc *Desc) override;
    CKERROR CreateOcclusionQuery(CKDWORD Query, CKOcclusionQueryDesc *Desc) override;
    CKERROR CreateIndirectBuffer(CKDWORD Buffer, CKIndirectBufferDesc *Desc) override;
    CKERROR DeleteObject(CKDWORD Object, CKDWORD Type) override;
    void FlushObjects(CKDWORD TypeMask) override;

    // Resource update
    CKERROR UpdateVertexBuffer(CKDWORD Buffer, CKDWORD Offset,
                               CKDWORD Size, const void *Data) override;
    CKERROR UpdateIndexBuffer(CKDWORD Buffer, CKDWORD Offset,
                              CKDWORD Size, const void *Data) override;
    CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face,
                          const CKRECT *Region, const VxImageDescEx *Data) override;

    // Readback
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip,
                        VxImageDescEx *Data) override;
    CKERROR ReadFrameBuffer(CKDWORD FrameBuffer,
                            VxImageDescEx *Data) override;

    // Occlusion query results
    CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD Query,
                                            CKDWORD *PixelCount) override;

    // Palette
    void SetPaletteColor(CKDWORD Index, CKDWORD RGBA) override;

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
    void RequestScreenShot(CKDWORD FrameBuffer,
                           CKScreenShotCallback Callback) override;

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
    CKRasterizerEncoder *BeginEncoder() override;
    void EndEncoder(CKRasterizerEncoder *Encoder) override;
    CKERROR Frame(CKBOOL VSync) override;

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

    CKBOOL m_BgfxInitialized;
    CKBOOL m_VSync;
    uint32_t m_ResetFlags;
    CKBgfxCallback m_BgfxCallback;
    CKBgfxEncoder m_Encoders[CKRST_MAX_ENCODERS];

    std::mutex m_ScreenShotMutex;
    CKScreenShotCallback m_PendingScreenShotCallback;
    CKDWORD m_PendingScreenShotFB;
    VxImageDescEx *m_BackbufferReadTarget;
    std::atomic<bool> m_BackbufferReadReady;

    CKDWORD m_DebugFrameId;
    CKDWORD m_DebugCaptureFirstFrames;
    CKDWORD m_DebugCaptureInterval;
    CKDWORD m_DebugCaptureLimit;
    CKDWORD m_DebugCaptureSaved;
    CKDWORD m_DebugBgfxFlags;
    CKBOOL m_DebugOverlay;
    char m_DebugCaptureDir[260];

    void ConfigureDebugCapture();
    void RequestDebugFrameCapture();
    void DrawDebugOverlay();

    XArray<CKBgfxShaderRecord *> m_Shaders;
    XArray<CKBgfxProgramRecord *> m_Programs;
    XArray<CKBgfxUniformRecord *> m_Uniforms;
    XArray<CKBgfxVertexLayoutRecord *> m_VertexLayouts;
    XArray<CKBgfxVertexBufferRecord *> m_VertexBuffers;
    XArray<CKBgfxIndexBufferRecord *> m_IndexBuffers;
    XArray<CKBgfxTextureRecord *> m_Textures;
    XArray<CKBgfxFrameBufferRecord *> m_FrameBuffers;
    XArray<CKBgfxOcclusionQueryRecord *> m_OcclusionQueries;
    XArray<CKBgfxIndirectBufferRecord *> m_IndirectBuffers;

    CKRenderStats m_Stats{};
    XArray<CKRenderViewStats> m_ViewStatsCache;

    VxMatrix m_TransformCache[CKRST_MAX_TRANSFORMS];
    std::atomic<CKDWORD> m_TransformCount;

    static const int MAX_TRANSIENT_VB = 256;
    static const int MAX_TRANSIENT_IB = 256;
    static const int MAX_TRANSIENT_INST = 256;
    bgfx::TransientVertexBuffer m_TransientVBPool[MAX_TRANSIENT_VB];
    std::atomic<CKDWORD> m_TransientVBCount;
    bgfx::TransientIndexBuffer m_TransientIBPool[MAX_TRANSIENT_IB];
    std::atomic<CKDWORD> m_TransientIBCount;
    bgfx::InstanceDataBuffer m_TransientInstPool[MAX_TRANSIENT_INST];
    std::atomic<CKDWORD> m_TransientInstCount;
};

#endif // CKBGFXRASTERIZER_H
