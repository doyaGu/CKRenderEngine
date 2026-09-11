#ifndef CKRASTERIZERBACKEND_H
#define CKRASTERIZERBACKEND_H

// ===========================================================================
// CKRasterizerBackend -- the thin GPU interface under the translation core
// (spec 5.10). Everything above it (CKTranslatedContext, the fixed-function
// pipeline, CKPresentStage) speaks D3D7 semantics translated into this small
// set of operations; everything below it (bgfx, NULL) only has to implement
// resources, resolved pipeline states, commands and synchronization.
//
// Handles are opaque non-zero CKDWORDs; 0 is never a valid object. Object
// types are the CKRST_OBJ_* ids shared with the public rasterizer interface
// plus the backend-only ones below.
// ===========================================================================

#include "VxMath.h"
#include "CKTypes.h"
#include "CKError.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerResourceTypes.h"
#include "CKRasterizerBackendEnums.h"   // CKDrawState, CK_DEPTH_FORMAT, CK_SHADER_PROFILE, CKRST_DEVCAPS_*, CK_FILTER_MODE...
#include <memory>
#include <vector>
#include "CKBackendProgram.h"
#include "CKBackendDrawData.h"
#include "CKBuiltinShaders.h"
#include "CKRasterizerBackendTypes.h"   // CKSamplerDesc, CKVertexLayoutDesc, CKShaderDesc, CKReadbackDesc

// Backend-only object types (the public rasterizer interface defines TEXTURE / VERTEXBUFFER /
// INDEXBUFFER).
#define CKRST_OBJ_RENDERTARGET    CKRST_OBJ_FRAMEBUFFER

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

struct CKBackendInitDesc {
    WIN_HANDLE Window;
    int PosX, PosY;
    int Width, Height;
    int Bpp;                 // colour depth requested by the engine (-1 = default)
    int ZBpp;                // depth bits (-1 = default)
    int StencilBpp;          // stencil bits (-1 = default)
    CKBOOL Fullscreen;
    int RefreshRate;
    CKDWORD DebugFlags;      // CKRST_DEBUG_*
    // Empty permits every native target. A rasterizer supplies only targets
    // for which its own complete shader library is available.
    std::vector<CKBackendShaderTarget> ShaderTargets;

    CKBackendInitDesc()
        : Window(NULL), PosX(0), PosY(0), Width(0), Height(0), Bpp(-1), ZBpp(-1), StencilBpp(-1),
          Fullscreen(FALSE), RefreshRate(0), DebugFlags(0) {}
};

// What the fixed-function context needs to know about the backend. Format
// lists and display modes stay on the concrete rasterizer driver; this is the
// per-backend, post-Init view.
struct CKBackendCaps {
    uint64_t Features;             // CKRST_DEVCAPS_* (RENDER_VIEWS / FRAMEBUFFER / TEXTURE_READBACK / BLIT / DEPTH_TEXTURE / TEXTURE_CUBE / ...)
    CKDWORD MaxTextureSize;
    CKDWORD MaxTextureBindings;    // logical texture binding slots
    CKDWORD MaxPasses;             // passes per frame (bgfx: views)
    CKDWORD MaxMSAASamples;        // 0 / 1 = no multisampled targets
    CK_SHADER_FORMAT ShaderFormat; // exact payload accepted by CreateShader
    CK_SHADER_PROFILE ShaderProfile;
    CKBOOL OriginBottomLeft;       // framebuffer writes start at the bottom row (OpenGL)
    CKBOOL RequiresIntermediateTarget; // window frames need a sampleable image for copies/readback
    CKBOOL HomogeneousDepth;       // clip z in [-1, 1] (OpenGL) instead of [0, 1]

    CKBackendCaps()
        : Features(0), MaxTextureSize(0), MaxTextureBindings(0), MaxPasses(0), MaxMSAASamples(0),
          ShaderFormat(CKRST_SHADER_FORMAT_UNKNOWN), ShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN),
          OriginBottomLeft(FALSE), RequiresIntermediateTarget(FALSE), HomogeneousDepth(FALSE) {}
};

struct CKBackendStats {
    CKDWORD Frames;                // Submit() calls
    CKDWORD Passes;                // BeginPass() calls in the last frame
    CKDWORD Draws;                 // Draw() calls in the last frame
    CKDWORD Blits;                 // Blit() calls in the last frame
    CKDWORD TextureUploads;        // CreateTexture with data + UpdateTexture in the last frame
    CKDWORD BufferUploads;         // CreateBuffer with data + UpdateBuffer in the last frame
    // Timings of the last frame (0 when the backend has no timer)
    int64_t CpuTimeFrame;
    int64_t CpuTimerFreq;
    int64_t GpuTimeFrame;
    int64_t GpuTimerFreq;
    CKDWORD GpuMemoryMax;
    CKDWORD GpuMemoryUsed;

    CKBackendStats()
        : Frames(0), Passes(0), Draws(0), Blits(0), TextureUploads(0), BufferUploads(0), CpuTimeFrame(0),
          CpuTimerFreq(0), GpuTimeFrame(0), GpuTimerFreq(0), GpuMemoryMax(0), GpuMemoryUsed(0) {}
};

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

// Textures use the public rasterizer's CKTextureDesc (flags: CKRST_TEXTURE_CUBEMAP /
// VOLUMEMAP / RENDERTARGET / MIPMAP..., plus the backend-only
// CKRST_TEXTURE_MSAA_Xn / READBACK / BLIT_DST from CKRasterizerBackendEnums.h).

struct CKBackendDepthDesc {
    CKDWORD Width, Height;
    CK_DEPTH_FORMAT Format;
    CKDWORD Samples;               // 0 / 1 = single sampled; must match the colour texture

    CKBackendDepthDesc() : Width(0), Height(0), Format(CKRST_DEPTHFMT_D24S8), Samples(0) {}
};

struct CKBackendRenderTargetDesc {
    CKDWORD ColorTexture;          // RENDERTARGET texture (2D or cube)
    CKDWORD ColorMip;
    CKDWORD ColorLayer;            // cube face for cube textures
    CKDWORD DepthTexture;          // 0 = no depth-stencil

    CKBackendRenderTargetDesc() : ColorTexture(0), ColorMip(0), ColorLayer(0), DepthTexture(0) {}
};

typedef enum CKBackendBufferKind {
    CKRST_BACKEND_BUFFER_VERTEX = 0,
    CKRST_BACKEND_BUFFER_INDEX = 1,
} CKBackendBufferKind;

struct CKBackendBufferDesc {
    CKBackendBufferKind Kind;
    CKDWORD Size;                  // bytes
    CKDWORD Stride;                // vertex buffers: bytes per vertex (canonical layout)
    CKDWORD Layout;                // vertex buffers: vertex layout handle
    CKBOOL Index32;                // index buffers: 32-bit indices
    CKBOOL Dynamic;                // updated after creation (UpdateBuffer)
    const void *InitialData;       // NULL = uninitialised

    CKBackendBufferDesc()
        : Kind(CKRST_BACKEND_BUFFER_VERTEX), Size(0), Stride(0), Layout(0), Index32(FALSE), Dynamic(FALSE),
          InitialData(NULL) {}
};

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

struct CKBackendPassDesc {
    CKDWORD RenderTarget;          // 0 = swap chain
    CKRECT Rect;                   // pass rectangle in target pixels (viewport + scissor of the pass)
    CKDWORD ClearFlags;            // CKRST_CTXCLEAR_*; 0 = load
    CKDWORD ClearColor;            // ARGB
    float ClearZ;
    CKDWORD ClearStencil;
    const char *Name;              // debug name, may be NULL

    CKBackendPassDesc()
        : RenderTarget(0), ClearFlags(0), ClearColor(0), ClearZ(1.0f), ClearStencil(0), Name(NULL) {
        Rect.left = Rect.top = Rect.right = Rect.bottom = 0;
    }
};

// Everything the rasterizer state of one draw needs (spec 5.10 ResolvedState).
struct CKBackendPipelineState {
    CKDrawState State;             // Lo / Mid / Hi words (blend, depth, cull, fill, topology, stencil ops)
    CKDWORD StencilRef;            // 0..255
    CKDWORD StencilReadMask;       // 0..255
    CKDWORD StencilWriteMask;      // 0..255 (backends without a write mask report CKRST_DEVCAPS_STENCIL_WRITE_MASK unset)
    CKBOOL ScissorEnabled;
    CKRECT Scissor;                // target pixels
    float PointSize;

    CKBackendPipelineState()
        : StencilRef(0), StencilReadMask(0xFF), StencilWriteMask(0xFF), ScissorEnabled(FALSE), PointSize(1.0f) {
        State.Lo = CKRST_STATE_DEFAULT_LO;
        State.Mid = CKRST_STATE_DEFAULT_MID;
        State.Hi = 0;
        Scissor.left = Scissor.top = Scissor.right = Scissor.bottom = 0;
    }
};

// Per-frame scratch geometry the core fills right before a draw.
struct CKBackendTransientVertices {
    void *Data;                    // write here, Count * Stride bytes
    CKDWORD Count;
    CKDWORD Stride;
    CKDWORD Layout;
    CKDWORD Token;                 // backend cookie identifying the allocation in Draw()

    CKBackendTransientVertices() : Data(NULL), Count(0), Stride(0), Layout(0), Token(0) {}
};

struct CKBackendTransientIndices {
    void *Data;
    CKDWORD Count;
    CKBOOL Index32;
    CKDWORD Token;

    CKBackendTransientIndices() : Data(NULL), Count(0), Index32(FALSE), Token(0) {}
};

// One draw. Geometry comes either from buffers (handles) or from transient
// allocations of this frame (pointers); indices are optional. Each of the two
// vertex streams has its own layout, source and starting vertex.
// Complete draw packet: no pipeline, binding, constant or marker state is
// inherited from a previous call. Null data pointers select empty/zero data.
// All borrowed data is consumed/snapshotted before Draw returns.
struct CKBackendDraw {
    CKBackendPipelineState Pipeline;
    const CKBackendTextureBindings *Textures = nullptr;
    const CKBackendConstants *Constants = nullptr;
    const char *Marker = nullptr;
    CKDWORD Program;
    CKDWORD Layout;                            // vertex layout of stream 0
    CKDWORD VertexBuffer;                      // 0 when TransientVertices is used
    const CKBackendTransientVertices *TransientVertices;
    CKDWORD StartVertex;
    CKDWORD VertexCount;
    CKDWORD IndexBuffer;                       // 0 when TransientIndices / non-indexed
    const CKBackendTransientIndices *TransientIndices;
    CKDWORD StartIndex;
    CKDWORD IndexCount;                        // 0 = non-indexed
    CKDWORD Stream1Layout;
    CKDWORD Stream1VertexBuffer;
    const CKBackendTransientVertices *Stream1Transient;
    CKDWORD Stream1StartVertex;
    CKDWORD SortKey;                           // draw order key inside the pass (0 = submission order)

    CKBackendDraw()
        : Program(0), Layout(0), VertexBuffer(0), TransientVertices(NULL), StartVertex(0), VertexCount(0),
          IndexBuffer(0), TransientIndices(NULL), StartIndex(0), IndexCount(0), Stream1Layout(0),
          Stream1VertexBuffer(0), Stream1Transient(NULL), Stream1StartVertex(0), SortKey(0) {}
};

enum CKBackendPresentSync {
    CKRST_BACKEND_SYNC_IMMEDIATE,
    CKRST_BACKEND_SYNC_VSYNC,
    CKRST_BACKEND_SYNC_UNCHANGED
};
struct CKBackendSubmitDesc {
    CKBackendPresentSync Sync;
    CKBOOL PresentWindow;
    CKBackendSubmitDesc(CKBackendPresentSync sync, CKBOOL presentWindow)
        : Sync(sync), PresentWindow(presentWindow) {}
};

// A scheduled readback owns its CPU storage until the GPU has finished.
// Dropping the consumer ticket cancels delivery, never the in-flight write.
struct CKBackendReadback {
    virtual ~CKBackendReadback() = default;
    std::vector<CKBYTE> Data;
    CKBOOL Complete = FALSE;
    CKERROR Error = CK_OK;
};
using CKBackendReadbackTicket = std::shared_ptr<CKBackendReadback>;
enum CKBackendReadbackState {
    CKRST_READBACK_NEEDS_SUBMIT,
    CKRST_READBACK_PENDING,
    CKRST_READBACK_READY,
    CKRST_READBACK_FAILED
};

// ---------------------------------------------------------------------------
// The interface
// ---------------------------------------------------------------------------

class CKRasterizerBackend {
public:
    virtual ~CKRasterizerBackend() {}

    // --- Device -----------------------------------------------------------
    virtual CKERROR Init(const CKBackendInitDesc *Desc) = 0;
    virtual void Shutdown() = 0;
    virtual CKERROR Resize(int PosX, int PosY, int Width, int Height) = 0;
    virtual CKERROR GetDeviceStatus() const = 0;           // CK_OK or a fatal device error
    virtual const CKBackendCaps &GetCaps() const = 0;      // valid after Init
    virtual CKBOOL IsIdle() const = 0;                     // no frame in progress
    virtual void SetDebugFlags(CKDWORD Flags) = 0;         // CKRST_DEBUG_*

    // --- Resources --------------------------------------------------------
    // Data (optional) fills mip 0 (face 0); further levels go through
    // UpdateTexture. Returns CKERR_NOTIMPLEMENTED for unsupported formats /
    // flag combinations (the core then approximates or rejects).
    virtual CKERROR CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out) = 0;
    virtual CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                                  const VxImageDescEx *Data) = 0;
    virtual CKERROR CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out) = 0;
    virtual CKERROR CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out) = 0;
    virtual CKERROR CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out) = 0;
    // Buffer handles are scoped by kind: a vertex buffer and an index buffer
    // may legally have the same numeric handle.
    virtual CKERROR UpdateBuffer(CKBackendBufferKind Kind, CKDWORD Buffer, CKDWORD Offset,
                                 CKDWORD Size, const void *Data) = 0;
    virtual CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out) = 0;
    virtual CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out) = 0;
    virtual CKERROR CreateProgram(const CKBackendProgramDesc *Desc, CKDWORD *Out) = 0;
    virtual CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const = 0;
    virtual CKERROR DestroyObject(CKDWORD Object, CKDWORD Type) = 0;
    virtual void SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name) = 0;

    // --- Frame ------------------------------------------------------------
    // Draws and copies execute in call order. BeginPass selects a logical
    // target; a backend may split it into native passes around transfers.
    // A Blit does not require or change the logical drawing target.
    virtual CKERROR BeginPass(const CKBackendPassDesc *Desc) = 0;
    virtual CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out) = 0;
    virtual CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out) = 0;
    virtual CKERROR Draw(const CKBackendDraw *Draw) = 0;
    virtual CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                         CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect) = 0;
    // Optional direct presentation path for a complete window-sized texture.
    // CKERR_NOTIMPLEMENTED asks the translation layer to draw its portable
    // fullscreen present pass instead. Sync is applied before acquiring the
    // window image; the caller still submits with the same mode (or UNCHANGED).
    virtual CKERROR PresentTexture(CKDWORD Texture, CKDWORD Width, CKDWORD Height,
                                   CKBackendPresentSync Sync) {
        (void)Texture; (void)Width; (void)Height; (void)Sync;
        return CKERR_NOTIMPLEMENTED;
    }
    // Submit recorded work. PresentWindow requests the recorded window image;
    // Sync independently controls presentation timing. The returned serial is
    // for statistics, never a GPU-completion guarantee. An adapter may keep
    // showing the last image while advancing resource-only submissions.
    virtual CKERROR Submit(const CKBackendSubmitDesc &Desc, CKDWORD *FrameNumber) = 0;

    // --- Readback ---------------------------------------------------------
    // Ticket == NULL queries the layout only. Otherwise enqueue a download
    // of the contents at this command position into backend-owned storage.
    // Subsequent writes cannot change it. The caller never lends memory to the GPU.
    virtual CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback,
                               CKBackendReadbackTicket *Ticket) = 0;
    // Wait may wait for an already submitted GPU operation. NEEDS_SUBMIT asks
    // the context to submit work (bgfx may need to re-present the last image).
    virtual CKBackendReadbackState PollReadback(const CKBackendReadbackTicket &Ticket, CKBOOL Wait) {
        if (!Ticket || Ticket->Error != CK_OK) return CKRST_READBACK_FAILED;
        return Ticket->Complete ? CKRST_READBACK_READY : CKRST_READBACK_NEEDS_SUBMIT;
    }

    // --- Misc -------------------------------------------------------------
    virtual uint64_t GetDrawApproximationMask() const { return 0; }
    virtual const CKBackendStats &GetStats() const = 0;
};

#endif // CKRASTERIZERBACKEND_H
