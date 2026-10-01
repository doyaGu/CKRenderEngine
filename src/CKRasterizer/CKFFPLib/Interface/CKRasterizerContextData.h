#ifndef CKRASTERIZERCONTEXTDATA_H
#define CKRASTERIZERCONTEXTDATA_H

// CPU-side data shared by CKFFPLib and the two Concrete Rasterizer Contexts.
// This header defines no GPU interface; native execution belongs to each
// concrete Context.

#include "VxMath.h"
#include "CKTypes.h"
#include "CKError.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerResourceTypes.h"
#include "CKRasterizerContextEnums.h"   // CKDrawState, CK_DEPTH_FORMAT, CK_SHADER_PROFILE, CKRST_DEVCAPS_*, CK_FILTER_MODE...
#include "CKFFProgramDesc.h"
#include "CKFFTextureBindings.h"
#include "CKFFPipelineState.h"
#include "CKBuiltinShaders.h"
#include "CKRasterizerContextTypes.h"   // CKSamplerDesc, CKVertexLayoutDesc, CKShaderDesc, CKReadbackDesc

// Internal object types (the public rasterizer interface defines TEXTURE / VERTEXBUFFER /
// INDEXBUFFER).
#define CKRST_OBJ_RENDERTARGET    CKRST_OBJ_FRAMEBUFFER

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

struct CKRasterizerInitParameters {
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
    XClassArray<CKFFShaderTarget> ShaderTargets;

    CKRasterizerInitParameters()
        : Window(NULL), PosX(0), PosY(0), Width(0), Height(0), Bpp(-1), ZBpp(-1), StencilBpp(-1),
          Fullscreen(FALSE), RefreshRate(0), DebugFlags(0) {}
};

// What CKFFPLib needs to know about its containing Context. Format lists and
// display modes stay on the concrete rasterizer driver; this is the small
// post-Init view consumed by shared fixed-function code.
struct CKRasterizerDeviceCaps {
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

    CKRasterizerDeviceCaps()
        : Features(0), MaxTextureSize(0), MaxTextureBindings(0), MaxPasses(0), MaxMSAASamples(0),
          ShaderFormat(CKRST_SHADER_FORMAT_UNKNOWN), ShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN),
          OriginBottomLeft(FALSE), RequiresIntermediateTarget(FALSE), HomogeneousDepth(FALSE) {}
};

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

// Textures use the public rasterizer's CKTextureDesc (flags: CKRST_TEXTURE_CUBEMAP /
// VOLUMEMAP / RENDERTARGET / MIPMAP..., plus the internal
// CKRST_TEXTURE_MSAA_Xn / READBACK / BLIT_DST from CKRasterizerContextEnums.h).

struct CKDepthTextureDesc {
    CKDWORD Width, Height;
    CK_DEPTH_FORMAT Format;
    CKDWORD Samples;               // 0 / 1 = single sampled; must match the colour texture

    CKDepthTextureDesc() : Width(0), Height(0), Format(CKRST_DEPTHFMT_D24S8), Samples(0) {}
};

struct CKRenderTargetDesc {
    CKDWORD ColorTexture;          // RENDERTARGET texture (2D or cube)
    CKDWORD ColorMip;
    CKDWORD ColorLayer;            // cube face for cube textures
    CKDWORD DepthTexture;          // 0 = no depth-stencil

    CKRenderTargetDesc() : ColorTexture(0), ColorMip(0), ColorLayer(0), DepthTexture(0) {}
};

typedef enum CKBufferKind {
    CKRST_BUFFER_VERTEX = 0,
    CKRST_BUFFER_INDEX = 1,
} CKBufferKind;

typedef enum CKBufferUpdateMode {
    CKRST_BUFFER_UPDATE_PRESERVE = 0,
    CKRST_BUFFER_UPDATE_DISCARD,
    CKRST_BUFFER_UPDATE_NOOVERWRITE,
} CKBufferUpdateMode;

struct CKBufferDesc {
    CKBufferKind Kind;
    CKDWORD Size;                  // bytes
    CKDWORD Stride;                // vertex buffers: bytes per vertex (canonical layout)
    CKDWORD Layout;                // vertex buffers: vertex layout handle
    CKBOOL Index32;                // index buffers: 32-bit indices
    CKBOOL Dynamic;                // updated after creation (UpdateBuffer)
    const void *InitialData;       // NULL = uninitialised

    CKBufferDesc()
        : Kind(CKRST_BUFFER_VERTEX), Size(0), Stride(0), Layout(0), Index32(FALSE), Dynamic(FALSE),
          InitialData(NULL) {}
};

struct CKBufferUpdateDesc {
    CKBufferKind Kind;
    CKDWORD Buffer;
    CKBufferUpdateMode Mode;
    CKBOOL Rename;
    CKDWORD Offset;
    CKDWORD Size;
    const void *Data;

    CKBufferUpdateDesc()
        : Kind(CKRST_BUFFER_VERTEX), Buffer(0),
          Mode(CKRST_BUFFER_UPDATE_PRESERVE), Rename(FALSE),
          Offset(0), Size(0), Data(NULL) {}
};

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

struct CKRenderPassDesc {
    CKDWORD RenderTarget;          // 0 = swap chain
    CKRECT Rect;                   // pass rectangle in target pixels (viewport + scissor of the pass)
    CKDWORD ClearFlags;            // CKRST_CTXCLEAR_*; 0 = load
    CKDWORD ClearColor;            // ARGB
    float ClearZ;
    CKDWORD ClearStencil;
    CKDWORD ColorTargetFormat;     // CKFFColorTargetFormat used for clear/output conversion
    const char *Name;              // debug name, may be NULL

    CKRenderPassDesc()
        : RenderTarget(0), ClearFlags(0), ClearColor(0), ClearZ(1.0f),
          ClearStencil(0), ColorTargetFormat(0), Name(NULL) {
        Rect.left = Rect.top = Rect.right = Rect.bottom = 0;
    }
};

// Per-frame scratch geometry the core fills right before a draw.
struct CKTransientVertexData {
    void *Data;                    // write here, Count * Stride bytes
    CKDWORD Count;
    CKDWORD Stride;
    CKDWORD Layout;
    CKDWORD Token;                 // Context-local cookie identifying the allocation in Draw()

    CKTransientVertexData() : Data(NULL), Count(0), Stride(0), Layout(0), Token(0) {}
};

struct CKTransientIndexData {
    void *Data;
    CKDWORD Count;
    CKBOOL Index32;
    CKDWORD Token;

    CKTransientIndexData() : Data(NULL), Count(0), Index32(FALSE), Token(0) {}
};

// One draw. Geometry comes either from buffers (handles) or from transient
// allocations of this frame (pointers); indices are optional. Each of the two
// vertex streams has its own layout, source and starting vertex.
// Complete draw packet: no pipeline, binding, constant or marker state is
// inherited from a previous call. Null data pointers select empty/zero data.
// All borrowed data is consumed/snapshotted before Draw returns.
struct CKDrawCommand {
    CKFFPipelineState Pipeline;
    const CKFFTextureBindings *Textures = nullptr;
    // The slots of Textures that may hold other than CKFFTextureSlot(). A
    // backend need not read the others.
    CKDWORD TextureSlots = 0xFFFFFFFFu;
    const CKFFConstantSet *Constants = nullptr;
    const char *Marker = nullptr;
    CKDWORD Program;
    CKDWORD Layout;                            // vertex layout of stream 0
    CKDWORD VertexBuffer;                      // 0 when TransientVertices is used
    const CKTransientVertexData *TransientVertices;
    CKDWORD StartVertex;
    CKDWORD VertexCount;
    CKDWORD IndexBuffer;                       // 0 when TransientIndices / non-indexed
    const CKTransientIndexData *TransientIndices;
    CKDWORD StartIndex;
    CKDWORD IndexCount;                        // 0 = non-indexed
    CKDWORD Stream1Layout;
    CKDWORD Stream1VertexBuffer;
    const CKTransientVertexData *Stream1Transient;
    CKDWORD Stream1StartVertex;
    CKDWORD SortKey;                           // draw order key inside the pass (0 = submission order)
    CKBOOL DitherEnable;                       // legacy target-color dithering for this draw
    CKDWORD ColorTargetFormat;                 // CKFFColorTargetFormat

    CKDrawCommand()
        : Program(0), Layout(0), VertexBuffer(0),
          TransientVertices(NULL), StartVertex(0), VertexCount(0),
          IndexBuffer(0), TransientIndices(NULL),
          StartIndex(0), IndexCount(0), Stream1Layout(0),
          Stream1VertexBuffer(0),
          Stream1Transient(NULL), Stream1StartVertex(0), SortKey(0),
          DitherEnable(FALSE), ColorTargetFormat(0) {}
};

enum CKPresentSync {
    CKRST_PRESENT_IMMEDIATE,
    CKRST_PRESENT_VSYNC,
    CKRST_PRESENT_UNCHANGED
};

enum CKReadbackState {
    CKRST_READBACK_NEEDS_SUBMIT,
    CKRST_READBACK_PENDING,
    CKRST_READBACK_READY,
    CKRST_READBACK_FAILED
};

#endif // CKRASTERIZERCONTEXTDATA_H
