#ifndef CKRASTERIZERENUMS_H
#define CKRASTERIZERENUMS_H

// CKRasterizer v3 contract: enumerations and constants.
//
// The v3 contract is D3D7 shaped: the engine sets fixed-function state
// (render states, texture stage states, transforms, lights, material,
// viewport, clip planes) and issues draws; all translation to a modern GPU
// API happens below this boundary (spec: docs/spec/2026-09-01-render-engine-
// redesign-v3.md, section 4).
//
// The backend interface of the translation core
// (src/CKRasterizer/CKRasterizerBackend/CKRasterizerBackend.h) includes this header for the
// enumerations both layers share.

#include <stdint.h>

#include "VxDefines.h"
#include "CKTypes.h"  // CKDWORD

// ===========================================================================
// Contract revision (spec 4.1)
// ===========================================================================

// Revision 4 adds semantic stage-range reset. Rebuild every rasterizer plugin;
// there is no v3 adapter or fallback to repeated state setters.
#define CKRST_INTERFACE_REVISION 0x00040000u

// ===========================================================================
// Limits
// ===========================================================================

#define CKRST_MAX_TEXTURE_STAGES        8
#define CKRST_MAX_LIGHTS                8
#define CKRST_MAX_USER_CLIP_PLANES      6
#define CKRST_MAX_WORLD_MATRICES        4   // VXMATRIX_WORLDMATRIX(0..3)
#define CKRST_MAX_TEXCOORD_DIMS         4

// ===========================================================================
// Object kinds (spec 4.5)
// ===========================================================================
// Only three kinds of handles cross the contract. Framebuffers, depth
// buffers, shaders, programs, uniforms, vertex layouts and samplers are
// rasterizer-internal objects.

#define CKRST_OBJ_TEXTURE         0x00000001u
#define CKRST_OBJ_VERTEXBUFFER    0x00000004u
#define CKRST_OBJ_INDEXBUFFER     0x00000008u
#define CKRST_OBJ_ALL             0xFFFFFFFFu

// ===========================================================================
// Clear flags (spec 4.3, v1 section 15)
// ===========================================================================

typedef enum CKRST_CTXCLEAR_FLAGS {
    CKRST_CTXCLEAR_DEPTH    = 0x00000010,
    CKRST_CTXCLEAR_COLOR    = 0x00000020,
    CKRST_CTXCLEAR_STENCIL  = 0x00000040,
    CKRST_CTXCLEAR_VIEWPORT = 0x00000100,
    CKRST_CTXCLEAR_ALL      = 0xFFFFFFFF,
} CKRST_CTXCLEAR_FLAGS;

// ===========================================================================
// Cube map faces
// ===========================================================================

typedef enum CKRST_CUBEFACE {
    CKRST_CUBEFACE_XPOS = 0,
    CKRST_CUBEFACE_XNEG = 1,
    CKRST_CUBEFACE_YPOS = 2,
    CKRST_CUBEFACE_YNEG = 3,
    CKRST_CUBEFACE_ZPOS = 4,
    CKRST_CUBEFACE_ZNEG = 5
} CKRST_CUBEFACE;

#define CKRST_CUBEFACE_COUNT 6

// ===========================================================================
// Texture flags (CKTextureDesc::Flags)
// ===========================================================================

typedef enum CKRST_TEXTUREFLAGS {
    CKRST_TEXTURE_VALID              = 0x00000001,
    CKRST_TEXTURE_COMPRESSION        = 0x00000004,
    CKRST_TEXTURE_MANAGED            = 0x00000080,
    CKRST_TEXTURE_HINTPROCEDURAL     = 0x00000100,
    CKRST_TEXTURE_HINTSTATIC         = 0x00000200,
    CKRST_TEXTURE_SPRITE             = 0x00000400,

    CKRST_TEXTURE_RGB                = 0x00000800,
    CKRST_TEXTURE_ALPHA              = 0x00001000,

    CKRST_TEXTURE_CUBEMAP            = 0x00002000,
    CKRST_TEXTURE_BUMPDUDV           = 0x00004000,
    CKRST_TEXTURE_FORCEPOW2          = 0x00008000,
    CKRST_TEXTURE_HINTCOLORKEY       = 0x00010000,
    CKRST_TEXTURE_HINTALPHAONE       = 0x00020000,
    CKRST_TEXTURE_RLESPRITE          = 0x00040000,
    CKRST_TEXTURE_SURFATTACHED       = 0x00080000,

    CKRST_TEXTURE_VOLUMEMAP          = 0x00100000,
    CKRST_TEXTURE_CONDITIONALNONPOW2 = 0x00200000,
    CKRST_TEXTURE_BUMPLUMINANCE      = 0x00400000,

    CKRST_TEXTURE_RENDERTARGET       = 0x10000000
} CKRST_TEXTUREFLAGS;

// MipMapCount convention (spec 4.5): 0 or 1 = no mips; N = the engine
// uploads N levels itself; CKRST_MIPMAP_GENERATE = upload level 0 only and
// let the rasterizer build the full filtered chain.
#define CKRST_MIPMAP_GENERATE ((CKDWORD)-1)

// ===========================================================================
// Vertex / index buffer flags
// ===========================================================================

typedef enum CKRST_VBFLAGS {
    CKRST_VB_VALID     = 0x00000001,
    CKRST_VB_WRITEONLY = 0x00000004,
    CKRST_VB_DYNAMIC   = 0x00000008,
    CKRST_VB_SHARED    = 0x00000010,
} CKRST_VBFLAGS;

typedef enum CKRST_LOCKFLAGS {
    CKRST_LOCK_DEFAULT     = 0x00000000,
    CKRST_LOCK_NOOVERWRITE = 0x00000001,
    CKRST_LOCK_DISCARD     = 0x00000002,
} CKRST_LOCKFLAGS;

// ===========================================================================
// Vertex format (spec 4.5)
// ===========================================================================
// The vertex format of a vertex buffer is the vertex-data subset of
// CKRST_DPFLAGS. CKRST_DP_TRANSFORM missing means pre-transformed vertices
// (x, y, z, rhw). CKRST_DP_LIGHT set means a normal is present. The
// interleaved memory layout is fixed by CKRSTGetVertexLayout() in
// CKRasterizer.h (D3D FVF order).

#define CKRST_VF_MASK (CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | \
                       CKRST_DP_SPECULAR | CKRST_DP_STAGESMASK | CKRST_DP_WEIGHTMASK | \
                       CKRST_DP_MATRIXPAL | CKRST_DP_PSIZE | CKRST_DP_TWEEN)

// ===========================================================================
// Render state extensions (spec 4.6)
// ===========================================================================
// VXRENDERSTATE_COLORWRITEENABLE = 168 has the D3D8 value; VxMath's
// VXRENDERSTATETYPE enumeration leaves it free and does not declare it.

#define VXRENDERSTATE_COLORWRITEENABLE ((VXRENDERSTATETYPE)168)

#define CKRST_COLORWRITE_RED   0x00000001u
#define CKRST_COLORWRITE_GREEN 0x00000002u
#define CKRST_COLORWRITE_BLUE  0x00000004u
#define CKRST_COLORWRITE_ALPHA 0x00000008u
#define CKRST_COLORWRITE_ALL   0x0000000Fu

// ===========================================================================
// Transform matrices (spec 4.6): v1 / D3D values
// ===========================================================================
// VXMATRIX_WORLD is an alias of VXMATRIX_WORLDMATRIX(0): setting either
// updates the same matrix. This is the only definition of VXMATRIX_TYPE; the
// engine and the translation core both use it.

typedef enum VXMATRIX_TYPE {
    VXMATRIX_WORLD      = 1,
    VXMATRIX_VIEW       = 2,
    VXMATRIX_PROJECTION = 3,
    VXMATRIX_TEXTURE0   = 16,
    VXMATRIX_TEXTURE1   = 17,
    VXMATRIX_TEXTURE2   = 18,
    VXMATRIX_TEXTURE3   = 19,
    VXMATRIX_TEXTURE4   = 20,
    VXMATRIX_TEXTURE5   = 21,
    VXMATRIX_TEXTURE6   = 22,
    VXMATRIX_TEXTURE7   = 23,
    VXMATRIX_WMAT       = 256,
} VXMATRIX_TYPE;

#define VXMATRIX_TEXTURE(stage)     ((VXMATRIX_TYPE)(VXMATRIX_TEXTURE0 + (stage)))
#define VXMATRIX_WORLDMATRIX(index) ((VXMATRIX_TYPE)(VXMATRIX_WMAT + (index)))

// Number of distinct matrix slots a context stores: view, projection,
// 8 texture matrices, CKRST_MAX_WORLD_MATRICES world matrices.
#define CKRST_MATRIX_SLOT_COUNT (2 + CKRST_MAX_TEXTURE_STAGES + CKRST_MAX_WORLD_MATRICES)

// ===========================================================================
// Texture coordinate index generation (CKRST_TSS_TEXCOORDINDEX high bits)
// ===========================================================================
// Same encoding as D3D8 D3DTSS_TCI_*: the low 16 bits select the texture
// coordinate set, the high 16 bits select the generation mode.

#define CKRST_TCI_PASSTHRU                    0x00000000u
#define CKRST_TCI_CAMERASPACENORMAL           0x00010000u
#define CKRST_TCI_CAMERASPACEPOSITION         0x00020000u
#define CKRST_TCI_CAMERASPACEREFLECTIONVECTOR 0x00030000u
#define CKRST_TCI_SPHEREMAP                   0x00040000u
#define CKRST_TCI_MASK                        0xFFFF0000u

// Generation numbers (CKRST_TCI_* >> 16) for CKRSTPackTexcoordIndex.
#define CKRST_TEXGEN_PASSTHRU                    0u
#define CKRST_TEXGEN_CAMERASPACENORMAL           1u
#define CKRST_TEXGEN_CAMERASPACEPOSITION         2u
#define CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR 3u
#define CKRST_TEXGEN_SPHEREMAP                   4u

inline CKDWORD CKRSTPackTexcoordIndex(CKDWORD Index, CKDWORD Generation)
{
    return (Index & 0xFFFFu) | ((Generation << 16) & CKRST_TCI_MASK);
}

inline CKDWORD CKRSTTexcoordIndex(CKDWORD Packed)
{
    return Packed & 0xFFFFu;
}

// Returns CKRST_TCI_* >> 16, i.e. 0 = pass through, 1 = camera-space normal,
// 2 = camera-space position, 3 = camera-space reflection, 4 = sphere map.
inline CKDWORD CKRSTTexcoordGeneration(CKDWORD Packed)
{
    return (Packed & CKRST_TCI_MASK) >> 16;
}

// ===========================================================================
// Backend capability bits (spec 4.9.1) - tests and diagnostics only
// ===========================================================================
// CKRasterizerCapsDesc::Features describes what the backend below the
// translation core can do natively. The engine MUST NOT read it; the
// translation core approximates whatever is missing (appendix C / D).

typedef uint64_t CKRST_CAPS;

#define CKRST_CAPS_SYNC_READBACK        UINT64_C(0x0000000000000001)
#define CKRST_CAPS_MIDFRAME_READBACK    UINT64_C(0x0000000000000002)
#define CKRST_CAPS_POINT_SIZE           UINT64_C(0x0000000000000004)
#define CKRST_CAPS_DEPTH_BIAS           UINT64_C(0x0000000000000008)
#define CKRST_CAPS_STENCIL_WRITE_MASK   UINT64_C(0x0000000000000010)
#define CKRST_CAPS_SAMPLER_LOD_CONTROL  UINT64_C(0x0000000000000020)
#define CKRST_CAPS_ANISOTROPY_LEVEL     UINT64_C(0x0000000000000040)
#define CKRST_CAPS_MSAA                 UINT64_C(0x0000000000000080)
#define CKRST_CAPS_TEXTURE_CUBE         UINT64_C(0x0000000000000100)
#define CKRST_CAPS_TEXTURE_VOLUME       UINT64_C(0x0000000000000200)
#define CKRST_CAPS_BORDER_COLOR         UINT64_C(0x0000000000000400)
#define CKRST_CAPS_SEPARATE_ALPHA_BLEND UINT64_C(0x0000000000000800)
#define CKRST_CAPS_MIRROR_ONCE          UINT64_C(0x0000000000001000)
#define CKRST_CAPS_TEXTURE_DXT          UINT64_C(0x0000000000002000)

// ===========================================================================
// Debug flags (CKRasterizerOptions::DebugFlags)
// ===========================================================================

#define CKRST_DEBUG_NONE              0x00000000u
#define CKRST_DEBUG_WIREFRAME         0x00000001u
#define CKRST_DEBUG_IFH               0x00000002u  // skip all draws ("infinitely fast hardware")
#define CKRST_DEBUG_STATS             0x00000004u
#define CKRST_DEBUG_DRAWMAP           0x00000020u
#define CKRST_DEBUG_DRAWMAP_SUBMITS   0x00000040u
#define CKRST_DEBUG_DRAWMAP_RESOURCES 0x00000080u
#define CKRST_DEBUG_DRAWMAP_PASSES    0x00000100u
#define CKRST_DEBUG_DRAWMAP_MARKERS   0x00000200u
#define CKRST_DEBUG_DRAWMAP_FRAME     0x00000400u
#define CKRST_DEBUG_DRAWMAP_SUMMARY   0x00000800u

// ===========================================================================
// Diagnostics (spec 4.10, appendix C / D)
// ===========================================================================
// Every counter is cumulative since context creation and exposed through
// CKRenderStats::Diagnostics[]. REJECT_* counters are the only cases where a
// draw returns FALSE. INVALID_* counters are Set* calls that returned FALSE
// and left state unchanged. APPROX_* / IGNORE_* counters are draws that were
// submitted with an approximated or ignored state.

typedef enum CKRST_DIAGNOSTIC {
    // Draw rejected (returned FALSE)
    CKRST_DIAG_REJECT_INVALID_HANDLE = 0,
    CKRST_DIAG_REJECT_INVALID_PARAMETER,
    CKRST_DIAG_REJECT_DEVICE_LOST,
    CKRST_DIAG_REJECT_SCENE_STATE,      // operation not allowed in the current scene state
    CKRST_DIAG_REJECT_UNSUPPORTED_STATE, // the rasterizer could not encode the draw (program creation
                                         // failed or the draw was refused); every fixed-function state
                                         // is approximated

    // State call rejected (returned FALSE, state unchanged)
    CKRST_DIAG_INVALID_RENDER_STATE,
    CKRST_DIAG_INVALID_STAGE_STATE,
    CKRST_DIAG_INVALID_STAGE_INDEX,
    CKRST_DIAG_INVALID_LIGHT_INDEX,
    CKRST_DIAG_INVALID_CLIP_PLANE_INDEX,
    CKRST_DIAG_INVALID_MATRIX_TYPE,
    CKRST_DIAG_INVALID_TARGET,          // SetTargetTexture inside a scene / bad face / bad size
    CKRST_DIAG_OVERLAY_ON_TARGET,       // BeginOverlayPhase while target != 0

    // Render state approximations (appendix C)
    CKRST_DIAG_APPROX_FILLMODE_POINT,
    CKRST_DIAG_APPROX_STENCIL_WRITE_MASK,
    CKRST_DIAG_IGNORE_WRAP,
    CKRST_DIAG_IGNORE_CLIPPING_OFF,
    CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE,
    CKRST_DIAG_APPROX_VERTEX_BLEND_WEIGHTS,
    CKRST_DIAG_APPROX_VERTEX_BLEND_TWEEN,
    CKRST_DIAG_APPROX_POINT_SIZE,
    CKRST_DIAG_APPROX_ZBIAS,
    CKRST_DIAG_IGNORE_ANTIALIAS,
    CKRST_DIAG_IGNORE_DITHER,
    CKRST_DIAG_IGNORE_LINEPATTERN,
    CKRST_DIAG_IGNORE_TEXTUREPERSPECTIVE_OFF,
    CKRST_DIAG_IGNORE_SOFTWAREVPROCESSING,

    // Texture stage approximations (appendix D)
    CKRST_DIAG_APPROX_TEXTURE_OP,
    CKRST_DIAG_APPROX_ALPHA_BUMP_OP,
    CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS,
    CKRST_DIAG_APPROX_MIRROR_ONCE,
    CKRST_DIAG_APPROX_BORDER_COLOR,
    CKRST_DIAG_IGNORE_SAMPLER_LOD,
    CKRST_DIAG_APPROX_ANISOTROPY,
    CKRST_DIAG_IGNORE_COMPAREFUNC,
    CKRST_DIAG_APPROX_STAGEBLEND,
    CKRST_DIAG_APPROX_COMPAREFUNC_FILTER,   // shader depth compare sampled with a filtering sampler
    CKRST_DIAG_APPROX_SAMPLER_SLOTS,        // more than four cube or volume stages: the extra stages sample as unbound

    // Presentation (spec 4.4)
    CKRST_DIAG_APPROX_MSAA,                 // no multisampled targets on this device: the scene rendered single sampled

    CKRST_DIAG_COUNT
} CKRST_DIAGNOSTIC;

#endif // CKRASTERIZERENUMS_H
