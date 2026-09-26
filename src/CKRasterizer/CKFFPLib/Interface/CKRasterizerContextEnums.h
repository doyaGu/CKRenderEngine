#ifndef CKRASTERIZERCONTEXTENUMS_H
#define CKRASTERIZERCONTEXTENUMS_H

#include <stdint.h>

#include "VxDefines.h"

// Internal rasterizer enumerations used by CKFFPLib and both Concrete
// Rasterizer Contexts. Object kinds, clear flags, cube faces, texture / buffer /
// lock flags and debug flags shared with callers come from the public header.
#include "CKRasterizerEnums.h"

// ===========================================================================
// Constants
// ===========================================================================

#define CKRST_MAX_VERTEX_STREAMS   4
#define CKRST_MAX_PASSES     256


// ===========================================================================
// Object Kinds (internal objects; public kinds are in CKRasterizerEnums.h)
// ===========================================================================

#define CKRST_OBJ_SHADER          0x00000010
#define CKRST_OBJ_PROGRAM         0x00000020
#define CKRST_OBJ_FRAMEBUFFER     0x00000080
#define CKRST_OBJ_VERTEXLAYOUT    0x00000100

// ---------------------------------------------------------------------------
// Shader Stage and Format
// ---------------------------------------------------------------------------

#define CKRST_MAKEFOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

typedef enum CK_SHADER_STAGE {
    CKRST_SHADER_VERTEX  = 0,
    CKRST_SHADER_PIXEL   = 1,
} CK_SHADER_STAGE;

typedef uint32_t CK_SHADER_FORMAT;
typedef uint32_t CK_SHADER_PROFILE;

#define CKRST_SHADER_FORMAT_UNKNOWN  0u
#define CKRST_SHADER_FORMAT_BGFX     CKRST_MAKEFOURCC('B', 'G', 'F', 'X')
#define CKRST_SHADER_FORMAT_SPIRV    CKRST_MAKEFOURCC('S', 'P', 'V', 'B')
#define CKRST_SHADER_FORMAT_DXBC     CKRST_MAKEFOURCC('D', 'X', 'B', 'C')
#define CKRST_SHADER_FORMAT_DXIL     CKRST_MAKEFOURCC('D', 'X', 'I', 'L')
#define CKRST_SHADER_FORMAT_MSL      CKRST_MAKEFOURCC('M', 'S', 'L', 'S')
#define CKRST_SHADER_FORMAT_METALLIB CKRST_MAKEFOURCC('M', 'T', 'L', 'B')

#define CKRST_SHADER_PROFILE_UNKNOWN 0u
#define CKRST_SHADER_PROFILE_DX11    CKRST_MAKEFOURCC('D', 'X', '1', '1')
#define CKRST_SHADER_PROFILE_DX12    CKRST_MAKEFOURCC('D', 'X', '1', '2')
#define CKRST_SHADER_PROFILE_SPIRV   CKRST_MAKEFOURCC('S', 'P', 'V', ' ')
#define CKRST_SHADER_PROFILE_GLSL    CKRST_MAKEFOURCC('G', 'L', 'S', 'L')
#define CKRST_SHADER_PROFILE_ESSL    CKRST_MAKEFOURCC('E', 'S', 'S', 'L')
#define CKRST_SHADER_PROFILE_MSL     CKRST_MAKEFOURCC('M', 'S', 'L', ' ')

#define CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE 0x00000001u
#define CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT    0x00000002u
#define CKRST_SHADER_TARGET_BORDER_COLOR_UNIFORM 0x00000004u
#define CKRST_SHADER_TARGET_SAMPLER_ORDINAL      0x00000008u

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
// Internal texture flags (extend the public CKRST_TEXTUREFLAGS)
// ---------------------------------------------------------------------------

#define CKRST_TEXTURE_MSAA_X2         0x00800000
#define CKRST_TEXTURE_MSAA_X4         0x01000000
#define CKRST_TEXTURE_BLIT_DST        0x02000000
#define CKRST_TEXTURE_COMPUTE_WRITE   0x04000000
#define CKRST_TEXTURE_MSAA_X8         0x08000000
#define CKRST_TEXTURE_DEPTHSTENCIL    0x20000000
#define CKRST_TEXTURE_READBACK        0x40000000
#define CKRST_TEXTURE_MSAA_X16        0x80000000
#define CKRST_TEXTURE_MSAA_MASK       (CKRST_TEXTURE_MSAA_X2 | CKRST_TEXTURE_MSAA_X4 | \
                                       CKRST_TEXTURE_MSAA_X8 | CKRST_TEXTURE_MSAA_X16)

// Multisampled render targets (color textures created with RENDERTARGET and
// depth textures): one of the MSAA_Xn flags selects the sample count. The
// samples are resolved when the texture is sampled; multisampled
// textures cannot be read back or blitted directly.
inline CKDWORD CKRSTTextureMSAAFlag(CKDWORD samples)
{
    if (samples >= 16) return CKRST_TEXTURE_MSAA_X16;
    if (samples >= 8)  return CKRST_TEXTURE_MSAA_X8;
    if (samples >= 4)  return CKRST_TEXTURE_MSAA_X4;
    if (samples >= 2)  return CKRST_TEXTURE_MSAA_X2;
    return 0;
}

inline CKDWORD CKRSTTextureMSAASamples(CKDWORD flags)
{
    if (flags & CKRST_TEXTURE_MSAA_X16) return 16;
    if (flags & CKRST_TEXTURE_MSAA_X8)  return 8;
    if (flags & CKRST_TEXTURE_MSAA_X4)  return 4;
    if (flags & CKRST_TEXTURE_MSAA_X2)  return 2;
    return 0;
}

// Context capabilities consumed by CKFFPLib. These values must not alias or
// expose a native graphics API capability mask.
typedef uint64_t CKRST_DEVCAPS;

#define CKRST_DEVCAPS_VERTEX_SHADER       UINT64_C(0x0000000000000001)
#define CKRST_DEVCAPS_PIXEL_SHADER        UINT64_C(0x0000000000000002)
#define CKRST_DEVCAPS_PASSES              UINT64_C(0x0000000000000004)
#define CKRST_DEVCAPS_FRAMEBUFFER         UINT64_C(0x0000000000000008)
#define CKRST_DEVCAPS_TRANSIENT_BUFFERS   UINT64_C(0x0000000000000010)
#define CKRST_DEVCAPS_SCISSOR             UINT64_C(0x0000000000000020)
#define CKRST_DEVCAPS_TEXTURE_READBACK    UINT64_C(0x0000000000000080)
#define CKRST_DEVCAPS_BUFFER_UPDATE       UINT64_C(0x0000000000000100)
#define CKRST_DEVCAPS_TEXTURE_UPDATE      UINT64_C(0x0000000000000200)
#define CKRST_DEVCAPS_DEPTH_TEXTURE       UINT64_C(0x0000000000000400)
#define CKRST_DEVCAPS_BLEND_EQUATION      UINT64_C(0x0000000000000800)
#define CKRST_DEVCAPS_BLIT                UINT64_C(0x0000000000001000)
#define CKRST_DEVCAPS_INDEX32             UINT64_C(0x0000000000004000)
#define CKRST_DEVCAPS_TEXTURE_COMPARISON  UINT64_C(0x0000000000008000)
#define CKRST_DEVCAPS_TEXTURE_CUBE        UINT64_C(0x0000000000080000)
#define CKRST_DEVCAPS_TEXTURE_3D          UINT64_C(0x0000000000100000)
#define CKRST_DEVCAPS_IMAGE_RW            UINT64_C(0x0000000000200000)
#define CKRST_DEVCAPS_VERTEX_ATTRIB_HALF  UINT64_C(0x0000000000400000)
#define CKRST_DEVCAPS_VERTEX_ATTRIB_UINT10 UINT64_C(0x0000000000800000)
#define CKRST_DEVCAPS_STENCIL_WRITE_MASK  UINT64_C(0x0000000001000000)

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

// ---------------------------------------------------------------------------
// Debug Flags (internal bits; caller-visible bits are in CKRasterizerEnums.h)
// ---------------------------------------------------------------------------

#define CKRST_DEBUG_TEXT       0x00000008
#define CKRST_DEBUG_PROFILER   0x00000010

// ===========================================================================
// CKDrawStateBuilder
// ===========================================================================

// VX enum values are reused only as compact numeric encodings of resolved
// pipeline state. The rasterizer resolves fixed-function combinations (for
// example BOTH source-alpha modes) before constructing the draw state.
class CKDrawStateBuilder {
public:
    CKDrawStateBuilder()
    {
        m_State.Lo = CKRST_STATE_WRITE_RGBA
                   | CKRST_STATE_DEPTH_TEST
                   | CKRST_STATE_DEPTH_WRITE
                   | CKRST_STATE_DEPTH_FUNC(VXCMP_LESSEQUAL)
                   | CKRST_STATE_CULL(VXCULL_CCW - 1);
        m_State.Mid = CKRST_STATE_PT(VX_TRIANGLELIST);
        m_State.Hi = 0;
    }

    CKDrawStateBuilder &WriteRGBA(CKBOOL R, CKBOOL G, CKBOOL B, CKBOOL A)
    {
        m_State.Lo &= ~0xFUL;
        if (R) m_State.Lo |= CKRST_STATE_WRITE_R;
        if (G) m_State.Lo |= CKRST_STATE_WRITE_G;
        if (B) m_State.Lo |= CKRST_STATE_WRITE_B;
        if (A) m_State.Lo |= CKRST_STATE_WRITE_A;
        return *this;
    }

    CKDrawStateBuilder &Depth(CKBOOL Test, CKBOOL Write, VXCMPFUNC Func)
    {
        m_State.Lo &= ~(CKRST_STATE_DEPTH_TEST | CKRST_STATE_DEPTH_WRITE | (0xFUL << 6));
        if (Test)  m_State.Lo |= CKRST_STATE_DEPTH_TEST;
        if (Write) m_State.Lo |= CKRST_STATE_DEPTH_WRITE;
        m_State.Lo |= CKRST_STATE_DEPTH_FUNC(Func);
        return *this;
    }

    CKDrawStateBuilder &Cull(VXCULL Mode)
    {
        m_State.Lo &= ~(0x3UL << 10);
        CKDWORD remapped = (Mode >= 1) ? (Mode - 1) : 0;
        m_State.Lo |= CKRST_STATE_CULL(remapped);
        return *this;
    }

    CKDrawStateBuilder &Fill(VXFILL_MODE Mode)
    {
        m_State.Lo &= ~(0x3UL << 12);
        CKDWORD remapped;
        switch (Mode) {
        case VXFILL_SOLID:     remapped = 0; break;
        case VXFILL_WIREFRAME: remapped = 1; break;
        case VXFILL_POINT:     remapped = 2; break;
        default:               remapped = 0; break;
        }
        m_State.Lo |= CKRST_STATE_FILLMODE(remapped);
        return *this;
    }

    CKDrawStateBuilder &MSAA(CKBOOL Enable)
    {
        if (Enable) m_State.Lo |= CKRST_STATE_MSAA;
        else        m_State.Lo &= ~CKRST_STATE_MSAA;
        return *this;
    }

    CKDrawStateBuilder &AlphaCoverage(CKBOOL Enable)
    {
        if (Enable) m_State.Lo |= CKRST_STATE_ALPHA_COVERAGE;
        else        m_State.Lo &= ~CKRST_STATE_ALPHA_COVERAGE;
        return *this;
    }

    CKDrawStateBuilder &Blend(VXBLEND_MODE SrcColor, VXBLEND_MODE DstColor)
    {
        m_State.Lo &= ~(0xFFFFUL << 16);
        m_State.Lo |= CKRST_STATE_BLEND(SrcColor, DstColor);
        return *this;
    }

    CKDrawStateBuilder &BlendSeparate(VXBLEND_MODE SrcColor, VXBLEND_MODE DstColor,
                                       VXBLEND_MODE SrcAlpha, VXBLEND_MODE DstAlpha)
    {
        m_State.Lo &= ~(0xFFFFUL << 16);
        m_State.Lo |= CKRST_STATE_BLEND_SEPARATE(SrcColor, DstColor, SrcAlpha, DstAlpha);
        return *this;
    }

    CKDrawStateBuilder &NoBlend()
    {
        m_State.Lo &= ~(0xFFFFUL << 16);
        return *this;
    }

    CKDrawStateBuilder &BlendEquation(VXBLENDOP Color)
    {
        m_State.Mid &= ~0x3FUL;
        m_State.Mid |= CKRST_STATE_BLEND_EQ(Color);
        return *this;
    }

    CKDrawStateBuilder &BlendEquationSeparate(VXBLENDOP Color, VXBLENDOP Alpha)
    {
        m_State.Mid &= ~0x3FUL;
        m_State.Mid |= CKRST_STATE_BLEND_EQ_SEPARATE(Color, Alpha);
        return *this;
    }

    CKDrawStateBuilder &Topology(VXPRIMITIVETYPE PT)
    {
        m_State.Mid &= ~(0x7UL << 6);
        m_State.Mid |= CKRST_STATE_PT(PT);
        return *this;
    }

    CKDrawStateBuilder &Stencil(CKBOOL Enable, VXCMPFUNC Func,
                                 VXSTENCILOP Fail, VXSTENCILOP ZFail, VXSTENCILOP Pass)
    {
        m_State.Mid &= ~(0x7FFFFFUL << 9);
        if (Enable)
            m_State.Mid |= CKRST_STENCIL_OPS(Func, Fail, ZFail, Pass);
        return *this;
    }

    CKDrawStateBuilder &StencilBack(VXCMPFUNC Func,
                                     VXSTENCILOP Fail, VXSTENCILOP ZFail, VXSTENCILOP Pass)
    {
        m_State.Hi &= ~0xFFFFUL;
        m_State.Hi |= CKRST_STENCIL_BACK_OPS(Func, Fail, ZFail, Pass);
        return *this;
    }

    CKDrawStateBuilder &FrontFaceCCW(CKBOOL CCW)
    {
        if (CCW) m_State.Hi |= CKRST_STATE_FRONT_CCW;
        else     m_State.Hi &= ~CKRST_STATE_FRONT_CCW;
        return *this;
    }

    CKDrawState Build() const { return m_State; }

private:
    CKDrawState m_State;
};

#endif // CKRASTERIZERCONTEXTENUMS_H
