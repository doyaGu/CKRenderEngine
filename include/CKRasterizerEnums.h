#ifndef CKRASTERIZERENUMS_H
#define CKRASTERIZERENUMS_H

#include <stdint.h>

#include "VxDefines.h"

// ===========================================================================
// Shared Enums (from v1, still used by v2 API)
// ===========================================================================

/******************************************************************************
// CKRasterizerContext::Clear Flags
*******************************************************************************/
typedef enum CKRST_CTXCLEAR_FLAGS
{
    CKRST_CTXCLEAR_DEPTH	= 0x00000010,
    CKRST_CTXCLEAR_COLOR	= 0x00000020,
    CKRST_CTXCLEAR_STENCIL	= 0x00000040,
    CKRST_CTXCLEAR_VIEWPORT	= 0x00000100,
    CKRST_CTXCLEAR_ALL		= 0xFFFFFFFF,
} CKRST_CTXCLEAR_FLAGS;

/******************************************************************************
// CKRasterizerContext::Frame sync handling
*******************************************************************************/
typedef enum CKRST_FRAME_SYNC_MODE
{
    CKRST_FRAME_SYNC_IMMEDIATE        = 0,
    CKRST_FRAME_SYNC_VSYNC            = 1,
    CKRST_FRAME_SYNC_PRESERVE_PRESENT = 2,
} CKRST_FRAME_SYNC_MODE;

typedef enum CKRST_FRAME_FLAGS
{
    CKRST_FRAME_NONE    = 0x00,
    CKRST_FRAME_CAPTURE = 0x01,
    CKRST_FRAME_DISCARD = 0x02,
    CKRST_FRAME_FLUSH   = 0x04,
} CKRST_FRAME_FLAGS;

/****************************************************************
// Cube Map Face Index
*****************************************************************/
typedef enum CKRST_CUBEFACE
{
    CKRST_CUBEFACE_XPOS = 0,
    CKRST_CUBEFACE_XNEG = 1,
    CKRST_CUBEFACE_YPOS = 2,
    CKRST_CUBEFACE_YNEG = 3,
    CKRST_CUBEFACE_ZPOS = 4,
    CKRST_CUBEFACE_ZNEG = 5
} CKRST_CUBEFACE;

/*************************************************************************************
// Texture Flags
*************************************************************************************/
typedef enum CKRST_TEXTUREFLAGS
{
    CKRST_TEXTURE_VALID				 = 0x00000001,
    CKRST_TEXTURE_COMPRESSION		 = 0x00000004,
    CKRST_TEXTURE_MANAGED			 = 0x00000080,
    CKRST_TEXTURE_HINTPROCEDURAL	 = 0x00000100,
    CKRST_TEXTURE_HINTSTATIC		 = 0x00000200,
    CKRST_TEXTURE_SPRITE			 = 0x00000400,

    CKRST_TEXTURE_RGB				 = 0x00000800,
    CKRST_TEXTURE_ALPHA				 = 0x00001000,

    CKRST_TEXTURE_CUBEMAP			 = 0x00002000,
    CKRST_TEXTURE_BUMPDUDV			 = 0x00004000,
    CKRST_TEXTURE_FORCEPOW2			 = 0x00008000,
    CKRST_TEXTURE_HINTCOLORKEY		 = 0x00010000,
    CKRST_TEXTURE_HINTALPHAONE		 = 0x00020000,
    CKRST_TEXTURE_RLESPRITE			 = 0x00040000,
    CKRST_TEXTURE_SURFATTACHED		 = 0x00080000,

    CKRST_TEXTURE_VOLUMEMAP			 = 0x00100000,
    CKRST_TEXTURE_CONDITIONALNONPOW2 = 0x00200000,

    CKRST_TEXTURE_RENDERTARGET		 = 0x10000000
} CKRST_TEXTUREFLAGS;

/******************************************************************
// Vertex or Index Buffer flags
*******************************************************************/
typedef enum CKRST_VBFLAGS
{
    CKRST_VB_VALID	    = 0x00000001,
    CKRST_VB_WRITEONLY  = 0x00000004,
    CKRST_VB_DYNAMIC    = 0x00000008,
    CKRST_VB_SHARED	    = 0x00000010,
} CKRST_VBFLAGS;

/*****************************************************************
// Lock flags
******************************************************************/
typedef enum CKRST_LOCKFLAGS
{
    CKRST_LOCK_DEFAULT		= 0x00000000,
    CKRST_LOCK_NOOVERWRITE	= 0x00000001,
    CKRST_LOCK_DISCARD		= 0x00000002,
} CKRST_LOCKFLAGS;

// ===========================================================================
// Constants
// ===========================================================================

#define CKRST_MAX_VERTEX_STREAMS   4
#define CKRST_MAX_TEXTURE_STAGES   8
#define CKRST_MAX_RENDER_VIEWS     256
#define CKRST_MAX_ENCODERS         8
#define CKRST_MAX_TRANSFORMS       1024
#define CKRST_MAX_COMPUTE_BINDINGS 8
#define CKRST_INVALID_TRANSFORM    0xFFFFFFFF

#define CKRST_INTERFACE_REVISION 0x00020100u

// ===========================================================================
// Object Kinds
// ===========================================================================

#define CKRST_OBJ_TEXTURE         0x00000001
#define CKRST_OBJ_VERTEXBUFFER    0x00000004
#define CKRST_OBJ_INDEXBUFFER     0x00000008
#define CKRST_OBJ_SHADER          0x00000010
#define CKRST_OBJ_PROGRAM         0x00000020
#define CKRST_OBJ_UNIFORM         0x00000040
#define CKRST_OBJ_FRAMEBUFFER     0x00000080
#define CKRST_OBJ_VERTEXLAYOUT    0x00000100
#define CKRST_OBJ_OCCLUSIONQUERY  0x00000200
#define CKRST_OBJ_INDIRECTBUFFER  0x00000400
#define CKRST_OBJ_ALL             0xFFFFFFFF

// ---------------------------------------------------------------------------
// Shader Stage and Format
// ---------------------------------------------------------------------------

#define CKRST_MAKEFOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

typedef enum CK_SHADER_STAGE {
    CKRST_SHADER_VERTEX  = 0,
    CKRST_SHADER_PIXEL   = 1,
    CKRST_SHADER_COMPUTE = 2,
} CK_SHADER_STAGE;

typedef uint32_t CK_SHADER_FORMAT;
typedef uint32_t CK_SHADER_PROFILE;

#define CKRST_SHADER_FORMAT_UNKNOWN 0u
#define CKRST_SHADER_FORMAT_NATIVE  1u

#define CKRST_SHADER_PROFILE_UNKNOWN 0u
#define CKRST_SHADER_PROFILE_DX11    CKRST_MAKEFOURCC('D', 'X', '1', '1')
#define CKRST_SHADER_PROFILE_DX12    CKRST_MAKEFOURCC('D', 'X', '1', '2')
#define CKRST_SHADER_PROFILE_SPIRV   CKRST_MAKEFOURCC('S', 'P', 'V', ' ')
#define CKRST_SHADER_PROFILE_GLSL    CKRST_MAKEFOURCC('G', 'L', 'S', 'L')
#define CKRST_SHADER_PROFILE_MSL     CKRST_MAKEFOURCC('M', 'S', 'L', ' ')

#define CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE 0x00000001u
#define CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT    0x00000002u

// ---------------------------------------------------------------------------
// Uniform Type
// ---------------------------------------------------------------------------

typedef enum CK_UNIFORM_TYPE {
    CKRST_UNIFORM_SAMPLER = 0,
    CKRST_UNIFORM_VEC4    = 1,
    CKRST_UNIFORM_MAT3    = 2,
    CKRST_UNIFORM_MAT4    = 3,
} CK_UNIFORM_TYPE;

// ---------------------------------------------------------------------------
// Vertex Attributes
// ---------------------------------------------------------------------------

typedef enum CK_VERTEX_ATTRIB {
    CKRST_ATTRIB_POSITION  = 0,
    CKRST_ATTRIB_NORMAL    = 1,
    CKRST_ATTRIB_TANGENT   = 2,
    CKRST_ATTRIB_BITANGENT = 3,
    CKRST_ATTRIB_COLOR0    = 4,
    CKRST_ATTRIB_COLOR1    = 5,
    CKRST_ATTRIB_COLOR2    = 6,
    CKRST_ATTRIB_COLOR3    = 7,
    CKRST_ATTRIB_INDICES   = 8,
    CKRST_ATTRIB_WEIGHT    = 9,
    CKRST_ATTRIB_TEXCOORD0 = 10,
    CKRST_ATTRIB_TEXCOORD1 = 11,
    CKRST_ATTRIB_TEXCOORD2 = 12,
    CKRST_ATTRIB_TEXCOORD3 = 13,
    CKRST_ATTRIB_TEXCOORD4 = 14,
    CKRST_ATTRIB_TEXCOORD5 = 15,
    CKRST_ATTRIB_TEXCOORD6 = 16,
    CKRST_ATTRIB_TEXCOORD7 = 17,
    CKRST_ATTRIB_COUNT     = 18,
} CK_VERTEX_ATTRIB;

typedef enum CK_VERTEX_ATTRIB_TYPE {
    CKRST_ATTRIBTYPE_INT8   = 0,
    CKRST_ATTRIBTYPE_UINT8  = 1,
    CKRST_ATTRIBTYPE_UINT10 = 2,
    CKRST_ATTRIBTYPE_INT16  = 3,
    CKRST_ATTRIBTYPE_UINT16 = 4,
    CKRST_ATTRIBTYPE_HALF   = 5,
    CKRST_ATTRIBTYPE_FLOAT  = 6,
} CK_VERTEX_ATTRIB_TYPE;

// ---------------------------------------------------------------------------
// Render View
// ---------------------------------------------------------------------------

typedef uint16_t CKRenderView;

typedef enum CK_VIEW_MODE {
    CKRST_VIEWMODE_DEFAULT    = 0,
    CKRST_VIEWMODE_SEQUENTIAL = 1,
    CKRST_VIEWMODE_DEPTH_ASC  = 2,
    CKRST_VIEWMODE_DEPTH_DESC = 3,
} CK_VIEW_MODE;

// ---------------------------------------------------------------------------
// Depth Format (for depth/stencil textures)
// ---------------------------------------------------------------------------

typedef enum CK_DEPTH_FORMAT {
    CKRST_DEPTHFMT_D16    = 0,
    CKRST_DEPTHFMT_D24    = 1,
    CKRST_DEPTHFMT_D24S8  = 2,
    CKRST_DEPTHFMT_D32F   = 3,
} CK_DEPTH_FORMAT;

// ---------------------------------------------------------------------------
// Sampler Enums
// ---------------------------------------------------------------------------

typedef enum CK_FILTER_MODE {
    CKRST_FILTER_NONE             = 0,
    CKRST_FILTER_NEAREST          = 1,
    CKRST_FILTER_LINEAR           = 2,
    CKRST_FILTER_MIPNEAREST       = 3,
    CKRST_FILTER_MIPLINEAR        = 4,
    CKRST_FILTER_LINEARMIPNEAREST = 5,
    CKRST_FILTER_LINEARMIPLINEAR  = 6,
    CKRST_FILTER_ANISOTROPIC      = 7,
} CK_FILTER_MODE;

typedef enum CK_ADDRESS_MODE {
    CKRST_ADDRESS_WRAP       = 1,
    CKRST_ADDRESS_MIRROR     = 2,
    CKRST_ADDRESS_CLAMP      = 3,
    CKRST_ADDRESS_BORDER     = 4,
} CK_ADDRESS_MODE;

typedef enum CK_COMPARE_MODE {
    CKRST_COMPARE_NONE     = 0,
    CKRST_COMPARE_LESS     = 1,
    CKRST_COMPARE_LEQUAL   = 2,
    CKRST_COMPARE_EQUAL    = 3,
    CKRST_COMPARE_GEQUAL   = 4,
    CKRST_COMPARE_GREATER  = 5,
    CKRST_COMPARE_NOTEQUAL = 6,
    CKRST_COMPARE_NEVER    = 7,
    CKRST_COMPARE_ALWAYS   = 8,
} CK_COMPARE_MODE;

// ---------------------------------------------------------------------------
// Discard Flags (encoder Submit)
// ---------------------------------------------------------------------------

typedef enum CK_DISCARD_FLAGS {
    CKRST_DISCARD_NONE           = 0x00,
    CKRST_DISCARD_BINDINGS       = 0x01,
    CKRST_DISCARD_INDEX_BUFFER   = 0x02,
    CKRST_DISCARD_INSTANCE_DATA  = 0x04,
    CKRST_DISCARD_STATE          = 0x08,
    CKRST_DISCARD_TRANSFORM      = 0x10,
    CKRST_DISCARD_VERTEX_STREAMS = 0x20,
    CKRST_DISCARD_ALL            = 0xFF,
} CK_DISCARD_FLAGS;

// ---------------------------------------------------------------------------
// Texture Flags (v2 extensions to CKRST_TEXTUREFLAGS)
// ---------------------------------------------------------------------------

#define CKRST_TEXTURE_BLIT_DST        0x02000000
#define CKRST_TEXTURE_COMPUTE_WRITE   0x04000000
#define CKRST_TEXTURE_DEPTHSTENCIL    0x20000000
#define CKRST_TEXTURE_READBACK        0x40000000

// ---------------------------------------------------------------------------
// Vertex Buffer Compute Flags (extensions to CKRST_VBFLAGS)
// ---------------------------------------------------------------------------

#define CKRST_VB_COMPUTE_READ       0x0100
#define CKRST_VB_COMPUTE_WRITE      0x0200
#define CKRST_VB_COMPUTE_READ_WRITE (CKRST_VB_COMPUTE_READ | CKRST_VB_COMPUTE_WRITE)

// Runtime context capabilities. These values are owned by CKRasterizer and
// must not alias or expose a native backend capability mask.
typedef uint64_t CKRST_CAPS;

#define CKRST_CAPS_VERTEX_SHADER       UINT64_C(0x0000000000000001)
#define CKRST_CAPS_PIXEL_SHADER        UINT64_C(0x0000000000000002)
#define CKRST_CAPS_RENDER_VIEWS        UINT64_C(0x0000000000000004)
#define CKRST_CAPS_FRAMEBUFFER         UINT64_C(0x0000000000000008)
#define CKRST_CAPS_TRANSIENT_BUFFERS   UINT64_C(0x0000000000000010)
#define CKRST_CAPS_SCISSOR             UINT64_C(0x0000000000000020)
#define CKRST_CAPS_INSTANCING          UINT64_C(0x0000000000000040)
#define CKRST_CAPS_TEXTURE_READBACK    UINT64_C(0x0000000000000080)
#define CKRST_CAPS_BUFFER_UPDATE       UINT64_C(0x0000000000000100)
#define CKRST_CAPS_TEXTURE_UPDATE      UINT64_C(0x0000000000000200)
#define CKRST_CAPS_DEPTH_TEXTURE       UINT64_C(0x0000000000000400)
#define CKRST_CAPS_BLEND_EQUATION      UINT64_C(0x0000000000000800)
#define CKRST_CAPS_BLIT                UINT64_C(0x0000000000001000)
#define CKRST_CAPS_TRANSFORM_CACHE     UINT64_C(0x0000000000002000)
#define CKRST_CAPS_INDEX32             UINT64_C(0x0000000000004000)
#define CKRST_CAPS_TEXTURE_COMPARISON  UINT64_C(0x0000000000008000)
#define CKRST_CAPS_COMPUTE             UINT64_C(0x0000000000010000)
#define CKRST_CAPS_OCCLUSION_QUERY     UINT64_C(0x0000000000020000)
#define CKRST_CAPS_DRAW_INDIRECT       UINT64_C(0x0000000000040000)
#define CKRST_CAPS_TEXTURE_CUBE        UINT64_C(0x0000000000080000)
#define CKRST_CAPS_TEXTURE_3D          UINT64_C(0x0000000000100000)
#define CKRST_CAPS_IMAGE_RW            UINT64_C(0x0000000000200000)
#define CKRST_CAPS_VERTEX_ATTRIB_HALF  UINT64_C(0x0000000000400000)
#define CKRST_CAPS_VERTEX_ATTRIB_UINT10 UINT64_C(0x0000000000800000)

typedef enum CKRST_FORMAT_CAPS {
    CKRST_FORMAT_CAPS_NONE             = 0x00000000,
    CKRST_FORMAT_CAPS_TEXTURE_2D       = 0x00000001,
    CKRST_FORMAT_CAPS_TEXTURE_3D       = 0x00000002,
    CKRST_FORMAT_CAPS_TEXTURE_CUBE     = 0x00000004,
    CKRST_FORMAT_CAPS_FRAMEBUFFER      = 0x00000008,
    CKRST_FORMAT_CAPS_FRAMEBUFFER_MSAA = 0x00000010,
    CKRST_FORMAT_CAPS_READBACK         = 0x00000020,
    CKRST_FORMAT_CAPS_IMAGE_READ       = 0x00000040,
    CKRST_FORMAT_CAPS_IMAGE_WRITE      = 0x00000080,
    CKRST_FORMAT_CAPS_MIP_AUTOGEN      = 0x00000100,
    CKRST_FORMAT_CAPS_SRGB             = 0x00000200,
    CKRST_FORMAT_CAPS_TEXTURE_COMPARE  = 0x00000400,
} CKRST_FORMAT_CAPS;

// ---------------------------------------------------------------------------
// Draw State
// ---------------------------------------------------------------------------

struct CKDrawState {
    uint32_t Lo;
    uint32_t Mid;
    uint32_t Hi;
};

// ---------------------------------------------------------------------------
// Draw State Helper Macros - Lo word
// ---------------------------------------------------------------------------

#define CKRST_STATE_WRITE_R        0x00000001UL
#define CKRST_STATE_WRITE_G        0x00000002UL
#define CKRST_STATE_WRITE_B        0x00000004UL
#define CKRST_STATE_WRITE_A        0x00000008UL
#define CKRST_STATE_WRITE_RGB      (CKRST_STATE_WRITE_R | CKRST_STATE_WRITE_G | CKRST_STATE_WRITE_B)
#define CKRST_STATE_WRITE_RGBA     (CKRST_STATE_WRITE_RGB | CKRST_STATE_WRITE_A)
#define CKRST_STATE_DEPTH_TEST     0x00000010UL
#define CKRST_STATE_DEPTH_WRITE    0x00000020UL
#define CKRST_STATE_MSAA           0x00004000UL
#define CKRST_STATE_ALPHA_COVERAGE 0x00008000UL

#define CKRST_STATE_DEPTH_FUNC(f)      (((uint32_t)(f) & 0xF) << 6)
#define CKRST_STATE_CULL(c)            (((uint32_t)(c) & 0x3) << 10)
#define CKRST_STATE_FILLMODE(m)        (((uint32_t)(m) & 0x3) << 12)
#define CKRST_STATE_BLEND_SRC(b)       (((uint32_t)(b) & 0xF) << 16)
#define CKRST_STATE_BLEND_DST(b)       (((uint32_t)(b) & 0xF) << 20)
#define CKRST_STATE_BLEND_SRC_ALPHA(b) (((uint32_t)(b) & 0xF) << 24)
#define CKRST_STATE_BLEND_DST_ALPHA(b) (((uint32_t)(b) & 0xF) << 28)

#define CKRST_STATE_BLEND(src, dst) \
    (CKRST_STATE_BLEND_SRC(src) | CKRST_STATE_BLEND_DST(dst))

#define CKRST_STATE_BLEND_SEPARATE(src_c, dst_c, src_a, dst_a) \
    (CKRST_STATE_BLEND_SRC(src_c) | CKRST_STATE_BLEND_DST(dst_c) | \
     CKRST_STATE_BLEND_SRC_ALPHA(src_a) | CKRST_STATE_BLEND_DST_ALPHA(dst_a))

// ---------------------------------------------------------------------------
// Draw State Helper Macros - Mid word
// ---------------------------------------------------------------------------

#define CKRST_STATE_BLEND_EQ(op)       (((uint32_t)(op) & 0x7) << 0)
#define CKRST_STATE_BLEND_EQ_ALPHA(op) (((uint32_t)(op) & 0x7) << 3)
#define CKRST_STATE_BLEND_EQ_SEPARATE(c, a) \
    (CKRST_STATE_BLEND_EQ(c) | CKRST_STATE_BLEND_EQ_ALPHA(a))
#define CKRST_STATE_PT(pt)             (((uint32_t)(pt) & 0x7) << 6)

#define CKRST_STENCIL_ENABLE           (1UL << 9)
#define CKRST_STENCIL_FUNC(f)         (((uint32_t)(f) & 0xF) << 10)
#define CKRST_STENCIL_FAIL(o)         (((uint32_t)(o) & 0xF) << 14)
#define CKRST_STENCIL_ZFAIL(o)        (((uint32_t)(o) & 0xF) << 18)
#define CKRST_STENCIL_PASS(o)         (((uint32_t)(o) & 0xF) << 22)

#define CKRST_STENCIL_OPS(func, fail, zfail, pass) \
    (CKRST_STENCIL_ENABLE | CKRST_STENCIL_FUNC(func) | \
     CKRST_STENCIL_FAIL(fail) | CKRST_STENCIL_ZFAIL(zfail) | CKRST_STENCIL_PASS(pass))

// ---------------------------------------------------------------------------
// Draw State Helper Macros - Hi word
// ---------------------------------------------------------------------------

#define CKRST_STENCIL_BACK_FUNC(f)    (((uint32_t)(f) & 0xF) << 0)
#define CKRST_STENCIL_BACK_FAIL(o)    (((uint32_t)(o) & 0xF) << 4)
#define CKRST_STENCIL_BACK_ZFAIL(o)   (((uint32_t)(o) & 0xF) << 8)
#define CKRST_STENCIL_BACK_PASS(o)    (((uint32_t)(o) & 0xF) << 12)

#define CKRST_STENCIL_BACK_OPS(func, fail, zfail, pass) \
    (CKRST_STENCIL_BACK_FUNC(func) | CKRST_STENCIL_BACK_FAIL(fail) | \
     CKRST_STENCIL_BACK_ZFAIL(zfail) | CKRST_STENCIL_BACK_PASS(pass))

#define CKRST_STATE_FRONT_CCW          (1UL << 16)

// ---------------------------------------------------------------------------
// Common State Presets
// ---------------------------------------------------------------------------

#define CKRST_STATE_DEFAULT_LO \
    (CKRST_STATE_WRITE_RGBA | CKRST_STATE_DEPTH_TEST | CKRST_STATE_DEPTH_WRITE | \
     CKRST_STATE_DEPTH_FUNC(VXCMP_LESSEQUAL) | CKRST_STATE_CULL(2))

#define CKRST_STATE_DEFAULT_MID \
    (CKRST_STATE_PT(VX_TRIANGLELIST))

#define CKRST_STATE_BLEND_ALPHA_LO \
    (CKRST_STATE_WRITE_RGBA | CKRST_STATE_DEPTH_TEST | \
     CKRST_STATE_DEPTH_FUNC(VXCMP_LESSEQUAL) | CKRST_STATE_CULL(2) | \
     CKRST_STATE_BLEND(VXBLEND_SRCALPHA, VXBLEND_INVSRCALPHA))

#define CKRST_STATE_BLEND_ADD_LO \
    (CKRST_STATE_WRITE_RGBA | CKRST_STATE_DEPTH_TEST | \
     CKRST_STATE_DEPTH_FUNC(VXCMP_LESSEQUAL) | \
     CKRST_STATE_BLEND(VXBLEND_ONE, VXBLEND_ONE))

// ---------------------------------------------------------------------------
// Compute Access Mode
// ---------------------------------------------------------------------------

typedef enum CK_ACCESS_MODE {
    CKRST_ACCESS_READ      = 0,
    CKRST_ACCESS_WRITE     = 1,
    CKRST_ACCESS_READWRITE = 2,
} CK_ACCESS_MODE;

// ---------------------------------------------------------------------------
// Occlusion Query Result
// ---------------------------------------------------------------------------

typedef enum CK_OCCLUSION_RESULT {
    CKRST_OCCLUSION_INVISIBLE = 0,
    CKRST_OCCLUSION_VISIBLE   = 1,
    CKRST_OCCLUSION_NORESULT  = 2,
} CK_OCCLUSION_RESULT;

// ---------------------------------------------------------------------------
// Debug Flags
// ---------------------------------------------------------------------------

#define CKRST_DEBUG_NONE       0x00000000
#define CKRST_DEBUG_WIREFRAME  0x00000001
#define CKRST_DEBUG_IFH        0x00000002
#define CKRST_DEBUG_STATS      0x00000004
#define CKRST_DEBUG_TEXT       0x00000008
#define CKRST_DEBUG_PROFILER   0x00000010
#define CKRST_DEBUG_DRAWMAP           0x00000020
#define CKRST_DEBUG_DRAWMAP_SUBMITS   0x00000040
#define CKRST_DEBUG_DRAWMAP_RESOURCES 0x00000080
#define CKRST_DEBUG_DRAWMAP_VIEWS     0x00000100
#define CKRST_DEBUG_DRAWMAP_MARKERS   0x00000200
#define CKRST_DEBUG_DRAWMAP_FRAME     0x00000400
#define CKRST_DEBUG_DRAWMAP_SUMMARY   0x00000800

#endif // CKRASTERIZERENUMS_H
