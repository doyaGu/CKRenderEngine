#ifndef CKRASTERIZERBACKEND_H
#define CKRASTERIZERBACKEND_H

// ===========================================================================
// CKRasterizerBackend -- the thin GPU interface under the translation core
// (spec 5.10). Everything above it (CKTranslatedContext, the fixed-function
// pipeline, CKPresentStage) speaks D3D7 semantics translated into this small
// set of operations; everything below it (bgfx, NULL, later SDL_GPU) only has
// to implement these ~30 methods.
//
// Handles are opaque non-zero CKDWORDs; 0 is never a valid object. Object
// types are the CKRST_OBJ_* ids shared with the contract (CKRasterizerEnums.h)
// plus the backend-only ones below.
// ===========================================================================

#include "VxMath.h"
#include "CKTypes.h"
#include "CKError.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "CKRasterizerBackendEnums.h"   // CKDrawState, CK_DEPTH_FORMAT, CK_SHADER_PROFILE, CKRST_DEVCAPS_*, CK_FILTER_MODE...
#include "CKRasterizerBackendTypes.h"   // CKSamplerDesc, CKVertexLayoutDesc, CKShaderDesc, CKReadbackDesc

// Backend-only object types (the contract defines TEXTURE / VERTEXBUFFER /
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

    CKBackendInitDesc()
        : Window(NULL), PosX(0), PosY(0), Width(0), Height(0), Bpp(-1), ZBpp(-1), StencilBpp(-1),
          Fullscreen(FALSE), RefreshRate(0), DebugFlags(0) {}
};

// What the translation core needs to know about the device. Format lists and
// display modes stay on the driver (CKTranslatedDriver reads the device
// driver's tables); this is the per-context, post-Init view.
struct CKBackendCaps {
    uint64_t Features;             // CKRST_DEVCAPS_* (RENDER_VIEWS / FRAMEBUFFER / TEXTURE_READBACK / BLIT / DEPTH_TEXTURE / TEXTURE_CUBE / ...)
    CKDWORD MaxTextureSize;
    CKDWORD MaxTextureBindings;    // sampler slots a draw may use (>= CKFF_SAMPLER_SLOT_COUNT + 1 for the present sampler)
    CKDWORD MaxPasses;             // passes per frame (bgfx: views)
    CKDWORD MaxMSAASamples;        // 0 / 1 = no multisampled targets
    CK_SHADER_PROFILE ShaderProfile;
    CKBOOL OriginBottomLeft;       // framebuffer writes start at the bottom row (OpenGL)
    CKBOOL HomogeneousDepth;       // clip z in [-1, 1] (OpenGL) instead of [0, 1]

    CKBackendCaps()
        : Features(0), MaxTextureSize(0), MaxTextureBindings(0), MaxPasses(0), MaxMSAASamples(0),
          ShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN), OriginBottomLeft(FALSE), HomogeneousDepth(FALSE) {}
};

struct CKBackendStats {
    CKDWORD Frames;                // Present() calls
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

// Textures use the contract's CKTextureDesc (flags: CKRST_TEXTURE_CUBEMAP /
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

// Constant blocks: the translation core pushes whole blocks; the layout of
// each block is the shader ABI (CKFFShaderABI.h). Backends map a block to
// whatever their shader system needs (bgfx: one uniform per block).
typedef enum CKBackendConstantBlock {
    CKRST_BLOCK_MATRICES = 0,          // mat4[CKFF_MATRIX_VEC4_COUNT]        u_ffMatrices
    CKRST_BLOCK_VERTEX_BLEND_MATRICES, // mat4[CKFF_VERTEX_BLEND_MATRIX_COUNT] u_vertexBlendMatrices
    CKRST_BLOCK_DRAW_PARAMS,           // vec4[CKFF_DRAW_PARAM_VEC4_COUNT]    u_ffDrawParams
    CKRST_BLOCK_TEX_MATRICES,          // mat4[CKFF_MAX_TEXTURE_STAGES]       u_texMatrix
    CKRST_BLOCK_LIGHTS,                // vec4[CKFF_MAX_LIGHTS * 7]           u_lights
    CKRST_BLOCK_BUMP_ENV,              // vec4[CKFF_MAX_TEXTURE_STAGES * 2]   u_bumpEnv
    CKRST_BLOCK_VIEWPORT,              // vec4                                u_viewport
    CKRST_BLOCK_STAGE_PARAMS,          // vec4[CKFF_STAGE_PARAM_VEC4_COUNT]   u_stageParams
    CKRST_BLOCK_SPEC,                  // vec4[CKFF_SPEC_UNIFORM_VEC4_COUNT]  u_ffSpec
    CKRST_BLOCK_CLIP_PLANES,           // vec4[CKFF_CLIP_PLANE_COUNT]         u_clipPlanes
    CKRST_BLOCK_CLIP_PARAMS,           // vec4                                u_clipParams
    CKRST_BLOCK_PRESENT_PARAMS,        // vec4                                u_postParams (present stage)
    CKRST_BLOCK_COUNT
} CKBackendConstantBlock;

struct CKBackendConstantBlockDesc {
    const char *Name;              // uniform name in the shaders
    CKBOOL Mat4;                   // elements are mat4 (4 vec4 each) instead of vec4
    CKDWORD Count;                 // element count
};

// Sampler slots: 0..CKFF_SAMPLER_SLOT_COUNT-1 are the fixed-function layout
// (CKFFShaderABI.h: s_texture0..7, s_textureCube0..3, s_textureVolume0..3);
// the present stage samples its source through the slot after them.
enum {
    CKRST_BACKEND_SLOT_PRESENT = 16,
    CKRST_BACKEND_SLOT_COUNT = 17,
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
// allocations of this frame (pointers); indices are optional. Stream 1 is the
// second position/normal stream of vertex tweening.
struct CKBackendDraw {
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

typedef enum CKBackendPresentMode {
    CKRST_BACKEND_PRESENT_IMMEDIATE = 0,   // present, no vsync
    CKRST_BACKEND_PRESENT_VSYNC = 1,       // present, vsync
    CKRST_BACKEND_PRESENT_PRESERVE = 2,    // end the frame without changing the present-sync mode (target frames, readback waits)
} CKBackendPresentMode;

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
    virtual CKERROR UpdateBuffer(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data) = 0;
    virtual CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out) = 0;
    virtual CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out) = 0;
    virtual CKERROR CreateProgram(CKDWORD VertexShader, CKDWORD PixelShader, CKDWORD *Out) = 0;
    virtual CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const = 0;
    virtual CKERROR DestroyObject(CKDWORD Object, CKDWORD Type) = 0;
    virtual void SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name) = 0;

    // --- Frame ------------------------------------------------------------
    // Passes are sequential: draws and blits inside a pass execute in call
    // order, passes in BeginPass order. A pass stays open until the next
    // BeginPass or Present. Blits submitted in a pass run before its draws.
    virtual CKERROR BeginPass(const CKBackendPassDesc *Desc) = 0;
    virtual void SetPipelineState(const CKBackendPipelineState *State) = 0;   // sticky until changed
    // Texture bindings are sticky until changed; Texture 0 clears the slot
    // (backends whose shaders need every sampler assigned, like GLSL, get the
    // explicit zero binding once per program from the pipeline).
    virtual void BindTexture(CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler) = 0;
    // Uploads a constant block for the next Draw. A failure aborts the draw
    // being prepared (the backend drops its pending state).
    virtual CKERROR PushConstants(CKBackendConstantBlock Block, const void *Data, CKDWORD Vec4Count) = 0;
    virtual void SetMarker(const char *Name) = 0;          // consumed by the next Draw (drawmap)
    virtual CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out) = 0;
    virtual CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out) = 0;
    virtual CKERROR Draw(const CKBackendDraw *Draw) = 0;
    virtual CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                         CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect) = 0;
    // Ends the frame: executes every pass, presents the swap chain (unless
    // PRESERVE) and returns the device frame number.
    virtual CKERROR Present(CKBackendPresentMode Mode, CKDWORD *FrameNumber) = 0;

    // --- Readback ---------------------------------------------------------
    // Texture created with CKRST_TEXTURE_READBACK. With Readback->Data NULL
    // only the layout is filled; otherwise the copy is scheduled and
    // *AvailableFrame is the frame number after which Data is valid.
    virtual CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKDWORD *AvailableFrame) = 0;

    // --- Misc -------------------------------------------------------------
    virtual CKERROR SetPaletteColor(CKDWORD Index, CKDWORD RGBA) = 0;   // border colours (bgfx: 16 entries)
    virtual const CKBackendStats &GetStats() const = 0;
};

// Uniform-name table of the constant blocks (bgfx-style backends create one
// uniform per block; block layouts are the shader ABI).
const CKBackendConstantBlockDesc &CKBackendConstantBlockInfo(CKBackendConstantBlock Block);
// Sampler uniform name of a slot (NULL for an invalid slot).
const char *CKBackendSamplerSlotName(CKDWORD Slot);

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

// One adapter of a backend library: what the engine sees through the v3
// driver (caps, display modes, texture formats) and the factory of backends.
// Caps follow the baseline (spec 4.9.2); RefreshCaps() lets a driver lower the
// numeric limits once a backend exists.
class CKRasterizerBackendDriver {
public:
    CKRasterizerBackendDriver();
    virtual ~CKRasterizerBackendDriver() {}

    virtual CKRasterizerBackend *CreateBackend() = 0;
    // Shuts the backend down and frees it; FALSE when it cannot go away yet.
    virtual CKBOOL DestroyBackend(CKRasterizerBackend *Backend) = 0;
    virtual void RefreshCaps() {}

    CKBOOL m_Hardware;
    CKBOOL m_CapsUpToDate;
    CKDWORD m_DriverIndex;
    XArray<VxDisplayMode> m_DisplayModes;
    XClassArray<CKTextureDesc> m_TextureFormats;
    Vx3DCapsDesc m_3DCaps;
    Vx2DCapsDesc m_2DCaps;
    XString m_Desc;
};

class CKRasterizerBackendLibrary {
public:
    virtual ~CKRasterizerBackendLibrary() {}

    virtual CKBOOL Start(WIN_HANDLE AppWnd) = 0;
    virtual void Close() = 0;
    virtual int GetDriverCount() const = 0;
    virtual CKRasterizerBackendDriver *GetDriver(CKDWORD Index) const = 0;
    virtual WIN_HANDLE GetMainWindow() const = 0;
};

#endif // CKRASTERIZERBACKEND_H
