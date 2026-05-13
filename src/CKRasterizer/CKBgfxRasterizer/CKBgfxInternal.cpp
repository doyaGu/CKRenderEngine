#include "CKBgfxInternal.h"
#include "CKBgfxConfig.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

static FILE *g_BgfxLogFile = nullptr;

static bool CKBgfxLogNameEquals(const char *lhs, const char *rhs)
{
    return lhs && rhs && _stricmp(lhs, rhs) == 0;
}

static CKBgfxDebugConfig CKBgfxReadDebugSettings()
{
    CKBgfxDebugConfig config = {};

    if (CKBgfxConfigBool("Debug.Bgfx", "Wireframe", false)) config.BgfxFlags |= BGFX_DEBUG_WIREFRAME;
    if (CKBgfxConfigBool("Debug.Bgfx", "IFH", false))       config.BgfxFlags |= BGFX_DEBUG_IFH;
    if (CKBgfxConfigBool("Debug.Bgfx", "Stats", false))     config.BgfxFlags |= BGFX_DEBUG_STATS;
    if (CKBgfxConfigBool("Debug.Bgfx", "Text", false))      config.BgfxFlags |= BGFX_DEBUG_TEXT;
    if (CKBgfxConfigBool("Debug.Bgfx", "Profiler", false))  config.BgfxFlags |= BGFX_DEBUG_PROFILER;
    if (CKBgfxConfigBool("Debug.Bgfx", "All", false))
        config.BgfxFlags |= BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS | BGFX_DEBUG_PROFILER;

    config.Overlay = CKBgfxConfigBool("Debug", "Overlay", (config.BgfxFlags & BGFX_DEBUG_TEXT) != 0);
    if (config.Overlay)
        config.BgfxFlags |= BGFX_DEBUG_TEXT;

    config.Log.File = CKBgfxConfigBool("Debug.Log", "File", false);
    config.Log.Trace = CKBgfxConfigBool("Debug.Log", "Trace", false);
    config.Log.Config = CKBgfxConfigBool("Debug.Log", "Config", false);
    config.Log.Textures = CKBgfxConfigBool("Debug.Log", "Textures", false);
    config.Log.TextureBindings = CKBgfxConfigBool("Debug.Log", "TextureBindings", false);
    config.Log.Uniforms = CKBgfxConfigBool("Debug.Log", "Uniforms", false);
    config.Log.PresentSync = CKBgfxConfigBool("Debug.Log", "PresentSync", false);

    return config;
}

const CKBgfxDebugConfig &CKBgfxDebugSettings()
{
    static const CKBgfxDebugConfig s_Config = CKBgfxReadDebugSettings();
    return s_Config;
}

bool CKBgfxLogEnabled(const char *name, bool fallback)
{
    const CKBgfxLogConfig &log = CKBgfxDebugSettings().Log;
    if (CKBgfxLogNameEquals(name, "File")) return log.File;
    if (CKBgfxLogNameEquals(name, "Trace")) return log.Trace;
    if (CKBgfxLogNameEquals(name, "Config")) return log.Config;
    if (CKBgfxLogNameEquals(name, "Textures")) return log.Textures;
    if (CKBgfxLogNameEquals(name, "TextureBindings")) return log.TextureBindings;
    if (CKBgfxLogNameEquals(name, "Uniforms")) return log.Uniforms;
    if (CKBgfxLogNameEquals(name, "PresentSync")) return log.PresentSync;
    return fallback;
}

int CKBgfxConfigPositiveInt(const char *section, const char *name, int fallback)
{
    const int value = CKBgfxConfigInt(section, name, fallback);
    return value > 0 ? value : fallback;
}

static uint32_t CKBgfxMsaaResetFlags(CKDWORD samples)
{
    if (samples >= 16) return BGFX_RESET_MSAA_X16;
    if (samples >= 8)  return BGFX_RESET_MSAA_X8;
    if (samples >= 4)  return BGFX_RESET_MSAA_X4;
    if (samples >= 2)  return BGFX_RESET_MSAA_X2;
    return BGFX_RESET_NONE;
}

uint32_t CKBgfxBuildResetFlags(CKBOOL vsync, CKDWORD samples)
{
    uint32_t flags = CKBgfxMsaaResetFlags(samples);
    if (vsync)
        flags |= BGFX_RESET_VSYNC;
    return flags;
}

static bool CKBgfxFileLogEnabled()
{
    return CKBgfxLogEnabled("File", false);
}

static FILE *CKBgfxGetLogFile()
{
    if (!g_BgfxLogFile) {
        char path[MAX_PATH] = {0};
        HMODULE hMod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&CKBgfxGetLogFile, &hMod)) {
            GetModuleFileNameA(hMod, path, MAX_PATH);
            char *last = strrchr(path, '\\');
            if (last)
                strcpy_s(last + 1, MAX_PATH - (last + 1 - path), "CKBgfx_Trace.log");
        }
        if (path[0] == '\0')
            strcpy_s(path, "CKBgfx_Trace.log");
        fopen_s(&g_BgfxLogFile, path, "w");
    }
    return g_BgfxLogFile;
}

void CKBgfxCloseLogFile()
{
    if (g_BgfxLogFile) {
        fclose(g_BgfxLogFile);
        g_BgfxLogFile = nullptr;
    }
}

void CKBgfxLogf(const char *tag, const char *fmt, ...)
{
    char msg[2048];
    int n = _snprintf_s(msg, sizeof(msg), _TRUNCATE, "[CKBgfx] [%s] ", tag ? tag : "?");
    if (n < 0)
        n = 0;

    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(msg + n, sizeof(msg) - n, _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringA(msg);
    OutputDebugStringA("\n");

    if (CKBgfxFileLogEnabled()) {
        FILE *f = CKBgfxGetLogFile();
        if (f) {
            fputs(msg, f);
            fputc('\n', f);
            fflush(f);
        }
    }
}

const char *CKBgfxRendererTypeName(bgfx::RendererType::Enum type)
{
    switch (type) {
    case bgfx::RendererType::Direct3D11: return "Direct3D11";
    case bgfx::RendererType::Direct3D12: return "Direct3D12";
    case bgfx::RendererType::Vulkan:     return "Vulkan";
    case bgfx::RendererType::OpenGL:     return "OpenGL";
    case bgfx::RendererType::OpenGLES:   return "OpenGLES";
    case bgfx::RendererType::Noop:       return "Noop";
    default:                             return "Auto";
    }
}

CK_SHADER_PROFILE CKBgfxShaderProfile(bgfx::RendererType::Enum type)
{
    switch (type) {
    case bgfx::RendererType::Direct3D11: return CKRST_SHADER_PROFILE_DX11;
    case bgfx::RendererType::Direct3D12: return CKRST_SHADER_PROFILE_DX12;
    case bgfx::RendererType::Vulkan:     return CKRST_SHADER_PROFILE_SPIRV;
    case bgfx::RendererType::OpenGL:
    case bgfx::RendererType::OpenGLES:   return CKRST_SHADER_PROFILE_GLSL;
    default:                             return CKRST_SHADER_PROFILE_UNKNOWN;
    }
}

bgfx::RendererType::Enum CKBgfxParseRequestedRenderer()
{
    char value[32] = {0};
    if (!CKBgfxConfigString("Renderer", "Backend", value, (CKDWORD)sizeof(value)) ||
        _stricmp(value, "auto") == 0)
        return bgfx::RendererType::Count;
    if (_stricmp(value, "d3d11") == 0 || _stricmp(value, "direct3d11") == 0)
        return bgfx::RendererType::Direct3D11;
    if (_stricmp(value, "d3d12") == 0 || _stricmp(value, "direct3d12") == 0)
        return bgfx::RendererType::Direct3D12;
    if (_stricmp(value, "vulkan") == 0)
        return bgfx::RendererType::Vulkan;
    if (_stricmp(value, "opengl") == 0 || _stricmp(value, "gl") == 0)
        return bgfx::RendererType::OpenGL;

    CKBgfxLogf("Init", "unknown Renderer/Backend='%s', falling back to auto", value);
    return bgfx::RendererType::Count;
}

bgfx::UniformType::Enum CKBgfxUniformType(CK_UNIFORM_TYPE type)
{
    switch (type)
    {
    case CKRST_UNIFORM_FLOAT1:
    case CKRST_UNIFORM_FLOAT2:
    case CKRST_UNIFORM_FLOAT3:
    case CKRST_UNIFORM_FLOAT4:  return bgfx::UniformType::Vec4;
    case CKRST_UNIFORM_MATRIX4: return bgfx::UniformType::Mat4;
    case CKRST_UNIFORM_SAMPLER: return bgfx::UniformType::Sampler;
    default:                    return bgfx::UniformType::Vec4;
    }
}

bgfx::Attrib::Enum CKBgfxAttrib(CK_VERTEX_ATTRIB attrib)
{
    switch (attrib)
    {
    case CKRST_ATTRIB_POSITION:  return bgfx::Attrib::Position;
    case CKRST_ATTRIB_NORMAL:    return bgfx::Attrib::Normal;
    case CKRST_ATTRIB_TANGENT:   return bgfx::Attrib::Tangent;
    case CKRST_ATTRIB_BITANGENT: return bgfx::Attrib::Bitangent;
    case CKRST_ATTRIB_COLOR0:    return bgfx::Attrib::Color0;
    case CKRST_ATTRIB_COLOR1:    return bgfx::Attrib::Color1;
    case CKRST_ATTRIB_COLOR2:    return bgfx::Attrib::Color2;
    case CKRST_ATTRIB_COLOR3:    return bgfx::Attrib::Color3;
    case CKRST_ATTRIB_INDICES:   return bgfx::Attrib::Indices;
    case CKRST_ATTRIB_WEIGHT:    return bgfx::Attrib::Weight;
    case CKRST_ATTRIB_TEXCOORD0: return bgfx::Attrib::TexCoord0;
    case CKRST_ATTRIB_TEXCOORD1: return bgfx::Attrib::TexCoord1;
    case CKRST_ATTRIB_TEXCOORD2: return bgfx::Attrib::TexCoord2;
    case CKRST_ATTRIB_TEXCOORD3: return bgfx::Attrib::TexCoord3;
    case CKRST_ATTRIB_TEXCOORD4: return bgfx::Attrib::TexCoord4;
    case CKRST_ATTRIB_TEXCOORD5: return bgfx::Attrib::TexCoord5;
    case CKRST_ATTRIB_TEXCOORD6: return bgfx::Attrib::TexCoord6;
    case CKRST_ATTRIB_TEXCOORD7: return bgfx::Attrib::TexCoord7;
    default:                     return bgfx::Attrib::Position;
    }
}

bgfx::AttribType::Enum CKBgfxAttribType(CK_VERTEX_ATTRIB_TYPE type)
{
    switch (type)
    {
    case CKRST_ATTRIBTYPE_FLOAT:  return bgfx::AttribType::Float;
    case CKRST_ATTRIBTYPE_UINT8:  return bgfx::AttribType::Uint8;
    case CKRST_ATTRIBTYPE_INT16:  return bgfx::AttribType::Int16;
    default:                      return bgfx::AttribType::Float;
    }
}

bgfx::TextureFormat::Enum CKBgfxTextureFormat(VX_PIXELFORMAT pf)
{
    switch (pf)
    {
    case _32_ARGB8888: return bgfx::TextureFormat::BGRA8;
    case _32_RGB888:   return bgfx::TextureFormat::BGRA8;
    case _32_BGRA8888: return bgfx::TextureFormat::BGRA8;
    case _32_ABGR8888: return bgfx::TextureFormat::RGBA8;
    case _24_RGB888:   return bgfx::TextureFormat::RGB8;
    case _24_BGR888:   return bgfx::TextureFormat::RGB8;
    case _16_RGB565:
    case _16_BGR565:   return bgfx::TextureFormat::R5G6B5;
    case _16_RGB555:
    case _16_BGR555:   return bgfx::TextureFormat::RGB5A1;
    case _16_ARGB1555:
    case _16_ABGR1555: return bgfx::TextureFormat::RGB5A1;
    case _16_ARGB4444:
    case _16_ABGR4444: return bgfx::TextureFormat::RGBA4;
    case _DXT1:        return bgfx::TextureFormat::BC1;
    case _DXT3:        return bgfx::TextureFormat::BC2;
    case _DXT5:        return bgfx::TextureFormat::BC3;
    default:           return bgfx::TextureFormat::BGRA8;
    }
}

bgfx::TextureFormat::Enum CKBgfxDepthFormat(CK_DEPTH_FORMAT fmt)
{
    switch (fmt)
    {
    case CKRST_DEPTHFMT_D16:    return bgfx::TextureFormat::D16;
    case CKRST_DEPTHFMT_D24:    return bgfx::TextureFormat::D24;
    case CKRST_DEPTHFMT_D24S8:  return bgfx::TextureFormat::D24S8;
    case CKRST_DEPTHFMT_D32F:   return bgfx::TextureFormat::D32F;
    default:                    return bgfx::TextureFormat::D24S8;
    }
}

uint32_t CKBgfxSamplerFlags(const CKSamplerDesc *s)
{
    if (!s)
        return BGFX_SAMPLER_NONE;

    uint32_t flags = 0;

    switch (s->MinFilter)
    {
    case CKRST_FILTER_NEAREST:     flags |= BGFX_SAMPLER_MIN_POINT; break;
    case CKRST_FILTER_ANISOTROPIC: flags |= BGFX_SAMPLER_MIN_ANISOTROPIC; break;
    default: break;
    }

    switch (s->MagFilter)
    {
    case CKRST_FILTER_NEAREST:     flags |= BGFX_SAMPLER_MAG_POINT; break;
    case CKRST_FILTER_ANISOTROPIC: flags |= BGFX_SAMPLER_MAG_ANISOTROPIC; break;
    default: break;
    }

    switch (s->MipFilter)
    {
    case CKRST_FILTER_NEAREST:
    case CKRST_FILTER_MIPNEAREST:  flags |= BGFX_SAMPLER_MIP_POINT; break;
    default: break;
    }

    switch (s->AddressU)
    {
    case CKRST_ADDRESS_MIRROR: flags |= BGFX_SAMPLER_U_MIRROR; break;
    case CKRST_ADDRESS_CLAMP:  flags |= BGFX_SAMPLER_U_CLAMP; break;
    case CKRST_ADDRESS_BORDER: flags |= BGFX_SAMPLER_U_BORDER; break;
    default: break;
    }

    switch (s->AddressV)
    {
    case CKRST_ADDRESS_MIRROR: flags |= BGFX_SAMPLER_V_MIRROR; break;
    case CKRST_ADDRESS_CLAMP:  flags |= BGFX_SAMPLER_V_CLAMP; break;
    case CKRST_ADDRESS_BORDER: flags |= BGFX_SAMPLER_V_BORDER; break;
    default: break;
    }

    switch (s->AddressW)
    {
    case CKRST_ADDRESS_MIRROR: flags |= BGFX_SAMPLER_W_MIRROR; break;
    case CKRST_ADDRESS_CLAMP:  flags |= BGFX_SAMPLER_W_CLAMP; break;
    case CKRST_ADDRESS_BORDER: flags |= BGFX_SAMPLER_W_BORDER; break;
    default: break;
    }

    if (s->CompareFunc != CKRST_COMPARE_NONE)
    {
        switch (s->CompareFunc)
        {
        case CKRST_COMPARE_LESS:     flags |= BGFX_SAMPLER_COMPARE_LESS; break;
        case CKRST_COMPARE_LEQUAL:   flags |= BGFX_SAMPLER_COMPARE_LEQUAL; break;
        case CKRST_COMPARE_EQUAL:    flags |= BGFX_SAMPLER_COMPARE_EQUAL; break;
        case CKRST_COMPARE_GEQUAL:   flags |= BGFX_SAMPLER_COMPARE_GEQUAL; break;
        case CKRST_COMPARE_GREATER:  flags |= BGFX_SAMPLER_COMPARE_GREATER; break;
        case CKRST_COMPARE_NOTEQUAL: flags |= BGFX_SAMPLER_COMPARE_NOTEQUAL; break;
        case CKRST_COMPARE_NEVER:    flags |= BGFX_SAMPLER_COMPARE_NEVER; break;
        case CKRST_COMPARE_ALWAYS:   flags |= BGFX_SAMPLER_COMPARE_ALWAYS; break;
        default: break;
        }
    }

    if (s->BorderColor != 0)
        flags |= BGFX_SAMPLER_BORDER_COLOR(s->BorderColor & 0xF);

    return flags;
}

static uint64_t CKBgfxBlendFactor(CKDWORD vx)
{
    switch (vx) {
    case VXBLEND_ZERO:        return BGFX_STATE_BLEND_ZERO;
    case VXBLEND_ONE:         return BGFX_STATE_BLEND_ONE;
    case VXBLEND_SRCCOLOR:    return BGFX_STATE_BLEND_SRC_COLOR;
    case VXBLEND_INVSRCCOLOR: return BGFX_STATE_BLEND_INV_SRC_COLOR;
    case VXBLEND_SRCALPHA:    return BGFX_STATE_BLEND_SRC_ALPHA;
    case VXBLEND_INVSRCALPHA: return BGFX_STATE_BLEND_INV_SRC_ALPHA;
    case VXBLEND_DESTALPHA:   return BGFX_STATE_BLEND_DST_ALPHA;
    case VXBLEND_INVDESTALPHA:return BGFX_STATE_BLEND_INV_DST_ALPHA;
    case VXBLEND_DESTCOLOR:   return BGFX_STATE_BLEND_DST_COLOR;
    case VXBLEND_INVDESTCOLOR:return BGFX_STATE_BLEND_INV_DST_COLOR;
    case VXBLEND_SRCALPHASAT: return BGFX_STATE_BLEND_SRC_ALPHA_SAT;
    default:                  return BGFX_STATE_BLEND_ONE;
    }
}

static uint64_t CKBgfxBlendEquation(CKDWORD op)
{
    switch (op) {
    case VXBLENDOP_ADD:         return BGFX_STATE_BLEND_EQUATION_ADD;
    case VXBLENDOP_SUBTRACT:    return BGFX_STATE_BLEND_EQUATION_SUB;
    case VXBLENDOP_REVSUBTRACT: return BGFX_STATE_BLEND_EQUATION_REVSUB;
    case VXBLENDOP_MIN:         return BGFX_STATE_BLEND_EQUATION_MIN;
    case VXBLENDOP_MAX:         return BGFX_STATE_BLEND_EQUATION_MAX;
    default:                    return BGFX_STATE_BLEND_EQUATION_ADD;
    }
}

uint64_t CKBgfxState(CKDrawState State)
{
    uint64_t bgfxState = 0;
    CKDWORD lo = State.Lo;

    if (lo & CKRST_STATE_WRITE_R) bgfxState |= BGFX_STATE_WRITE_R;
    if (lo & CKRST_STATE_WRITE_G) bgfxState |= BGFX_STATE_WRITE_G;
    if (lo & CKRST_STATE_WRITE_B) bgfxState |= BGFX_STATE_WRITE_B;
    if (lo & CKRST_STATE_WRITE_A) bgfxState |= BGFX_STATE_WRITE_A;

    if (lo & CKRST_STATE_DEPTH_TEST)
    {
        CKDWORD depthFunc = (lo >> 6) & 0xF;
        switch (depthFunc)
        {
        case VXCMP_LESS:         bgfxState |= BGFX_STATE_DEPTH_TEST_LESS; break;
        case VXCMP_LESSEQUAL:    bgfxState |= BGFX_STATE_DEPTH_TEST_LEQUAL; break;
        case VXCMP_EQUAL:        bgfxState |= BGFX_STATE_DEPTH_TEST_EQUAL; break;
        case VXCMP_GREATEREQUAL: bgfxState |= BGFX_STATE_DEPTH_TEST_GEQUAL; break;
        case VXCMP_GREATER:      bgfxState |= BGFX_STATE_DEPTH_TEST_GREATER; break;
        case VXCMP_NOTEQUAL:     bgfxState |= BGFX_STATE_DEPTH_TEST_NOTEQUAL; break;
        case VXCMP_NEVER:        bgfxState |= BGFX_STATE_DEPTH_TEST_NEVER; break;
        case VXCMP_ALWAYS:       bgfxState |= BGFX_STATE_DEPTH_TEST_ALWAYS; break;
        default:                 bgfxState |= BGFX_STATE_DEPTH_TEST_LESS; break;
        }
    }

    if (lo & CKRST_STATE_DEPTH_WRITE)
        bgfxState |= BGFX_STATE_WRITE_Z;

    CKDWORD cullMode = (lo >> 10) & 0x3;
    if (cullMode == 1) bgfxState |= BGFX_STATE_CULL_CW;
    else if (cullMode == 2) bgfxState |= BGFX_STATE_CULL_CCW;

    if (lo & CKRST_STATE_MSAA)
        bgfxState |= BGFX_STATE_MSAA;

    if (lo & CKRST_STATE_ALPHA_COVERAGE)
        bgfxState |= BGFX_STATE_BLEND_ALPHA_TO_COVERAGE;

    CKDWORD blendSrc = (lo >> 16) & 0xF;
    CKDWORD blendDst = (lo >> 20) & 0xF;
    CKDWORD blendSrcA = (lo >> 24) & 0xF;
    CKDWORD blendDstA = (lo >> 28) & 0xF;
    if (blendSrc != 0)
    {
        if (blendSrcA != 0)
        {
            bgfxState |= BGFX_STATE_BLEND_FUNC_SEPARATE(
                CKBgfxBlendFactor(blendSrc), CKBgfxBlendFactor(blendDst),
                CKBgfxBlendFactor(blendSrcA), CKBgfxBlendFactor(blendDstA));
        }
        else
        {
            bgfxState |= BGFX_STATE_BLEND_FUNC(
                CKBgfxBlendFactor(blendSrc), CKBgfxBlendFactor(blendDst));
        }
    }

    CKDWORD mid = State.Mid;

    CKDWORD blendEq = mid & 0x7;
    CKDWORD blendEqA = (mid >> 3) & 0x7;
    if (blendEq != 0)
    {
        if (blendEqA != 0)
            bgfxState |= BGFX_STATE_BLEND_EQUATION_SEPARATE(
                CKBgfxBlendEquation(blendEq), CKBgfxBlendEquation(blendEqA));
        else
            bgfxState |= BGFX_STATE_BLEND_EQUATION(CKBgfxBlendEquation(blendEq));
    }

    CKDWORD fillMode = (lo >> 12) & 0x3;
    if (fillMode == 1)
    {
        bgfxState |= BGFX_STATE_PT_LINES;
    }
    else if (fillMode == 2)
    {
        bgfxState |= BGFX_STATE_PT_POINTS;
    }
    else
    {
        CKDWORD pt = (mid >> 6) & 0x7;
        switch (pt)
        {
        case VX_POINTLIST:     bgfxState |= BGFX_STATE_PT_POINTS; break;
        case VX_LINELIST:      bgfxState |= BGFX_STATE_PT_LINES; break;
        case VX_LINESTRIP:     bgfxState |= BGFX_STATE_PT_LINESTRIP; break;
        case VX_TRIANGLESTRIP: bgfxState |= BGFX_STATE_PT_TRISTRIP; break;
        default: break;
        }
    }

    CKDWORD hi = State.Hi;
    if (hi & CKRST_STATE_FRONT_CCW)
        bgfxState |= BGFX_STATE_FRONT_CCW;

    return bgfxState;
}

static uint32_t CKBgfxStencilTest(CKDWORD func)
{
    switch (func)
    {
    case VXCMP_NEVER:        return BGFX_STENCIL_TEST_NEVER;
    case VXCMP_LESS:         return BGFX_STENCIL_TEST_LESS;
    case VXCMP_EQUAL:        return BGFX_STENCIL_TEST_EQUAL;
    case VXCMP_LESSEQUAL:    return BGFX_STENCIL_TEST_LEQUAL;
    case VXCMP_GREATER:      return BGFX_STENCIL_TEST_GREATER;
    case VXCMP_NOTEQUAL:     return BGFX_STENCIL_TEST_NOTEQUAL;
    case VXCMP_GREATEREQUAL: return BGFX_STENCIL_TEST_GEQUAL;
    case VXCMP_ALWAYS:       return BGFX_STENCIL_TEST_ALWAYS;
    default:                 return BGFX_STENCIL_TEST_ALWAYS;
    }
}

static uint32_t CKBgfxStencilFailS(CKDWORD op)
{
    switch (op)
    {
    case VXSTENCILOP_KEEP:    return BGFX_STENCIL_OP_FAIL_S_KEEP;
    case VXSTENCILOP_ZERO:    return BGFX_STENCIL_OP_FAIL_S_ZERO;
    case VXSTENCILOP_REPLACE: return BGFX_STENCIL_OP_FAIL_S_REPLACE;
    case VXSTENCILOP_INCRSAT: return BGFX_STENCIL_OP_FAIL_S_INCRSAT;
    case VXSTENCILOP_DECRSAT: return BGFX_STENCIL_OP_FAIL_S_DECRSAT;
    case VXSTENCILOP_INVERT:  return BGFX_STENCIL_OP_FAIL_S_INVERT;
    case VXSTENCILOP_INCR:    return BGFX_STENCIL_OP_FAIL_S_INCR;
    case VXSTENCILOP_DECR:    return BGFX_STENCIL_OP_FAIL_S_DECR;
    default:                  return BGFX_STENCIL_OP_FAIL_S_KEEP;
    }
}

static uint32_t CKBgfxStencilFailZ(CKDWORD op)
{
    switch (op)
    {
    case VXSTENCILOP_KEEP:    return BGFX_STENCIL_OP_FAIL_Z_KEEP;
    case VXSTENCILOP_ZERO:    return BGFX_STENCIL_OP_FAIL_Z_ZERO;
    case VXSTENCILOP_REPLACE: return BGFX_STENCIL_OP_FAIL_Z_REPLACE;
    case VXSTENCILOP_INCRSAT: return BGFX_STENCIL_OP_FAIL_Z_INCRSAT;
    case VXSTENCILOP_DECRSAT: return BGFX_STENCIL_OP_FAIL_Z_DECRSAT;
    case VXSTENCILOP_INVERT:  return BGFX_STENCIL_OP_FAIL_Z_INVERT;
    case VXSTENCILOP_INCR:    return BGFX_STENCIL_OP_FAIL_Z_INCR;
    case VXSTENCILOP_DECR:    return BGFX_STENCIL_OP_FAIL_Z_DECR;
    default:                  return BGFX_STENCIL_OP_FAIL_Z_KEEP;
    }
}

static uint32_t CKBgfxStencilPassZ(CKDWORD op)
{
    switch (op)
    {
    case VXSTENCILOP_KEEP:    return BGFX_STENCIL_OP_PASS_Z_KEEP;
    case VXSTENCILOP_ZERO:    return BGFX_STENCIL_OP_PASS_Z_ZERO;
    case VXSTENCILOP_REPLACE: return BGFX_STENCIL_OP_PASS_Z_REPLACE;
    case VXSTENCILOP_INCRSAT: return BGFX_STENCIL_OP_PASS_Z_INCRSAT;
    case VXSTENCILOP_DECRSAT: return BGFX_STENCIL_OP_PASS_Z_DECRSAT;
    case VXSTENCILOP_INVERT:  return BGFX_STENCIL_OP_PASS_Z_INVERT;
    case VXSTENCILOP_INCR:    return BGFX_STENCIL_OP_PASS_Z_INCR;
    case VXSTENCILOP_DECR:    return BGFX_STENCIL_OP_PASS_Z_DECR;
    default:                  return BGFX_STENCIL_OP_PASS_Z_KEEP;
    }
}

static CKDWORD CKBgfxAdjustStencilRefForWriteMask(CKDWORD ref, CKDWORD readMask, CKDWORD writeMask)
{
    readMask &= 0xFF;
    writeMask &= 0xFF;
    if (writeMask != 0xFF && (readMask == 0 || readMask == writeMask))
        return ref & writeMask;
    return ref & 0xFF;
}

// ===========================================================================
// Helper: build bgfx front stencil uint32_t from CKDrawState Mid word
// ===========================================================================

uint32_t CKBgfxBuildFrontStencil(CKDrawState State, CKDWORD ref, CKDWORD readMask, CKDWORD writeMask)
{
    CKDWORD mid = State.Mid;
    if (!(mid & CKRST_STENCIL_ENABLE))
        return BGFX_STENCIL_NONE;

    ref = CKBgfxAdjustStencilRefForWriteMask(ref, readMask, writeMask);
    CKDWORD func   = (mid >> 10) & 0xF;
    CKDWORD failOp = (mid >> 14) & 0xF;
    CKDWORD zfailOp = (mid >> 18) & 0xF;
    CKDWORD passOp = (mid >> 22) & 0xF;
    // bgfx exposes the compare read mask but not the D3D stencil write mask.
    // Preserve test semantics and approximate masked writes only where the
    // public stencil state can do so without changing the compare mask.
    if ((writeMask & 0xFF) == 0)
    {
        failOp = VXSTENCILOP_KEEP;
        zfailOp = VXSTENCILOP_KEEP;
        passOp = VXSTENCILOP_KEEP;
    }

    uint32_t stencil = 0;
    stencil |= CKBgfxStencilTest(func);
    stencil |= CKBgfxStencilFailS(failOp);
    stencil |= CKBgfxStencilFailZ(zfailOp);
    stencil |= CKBgfxStencilPassZ(passOp);
    stencil |= BGFX_STENCIL_FUNC_REF(ref);
    stencil |= BGFX_STENCIL_FUNC_RMASK(readMask);

    return stencil;
}

// ===========================================================================
// Helper: build bgfx back stencil uint32_t from CKDrawState Hi word
// ===========================================================================

uint32_t CKBgfxBuildBackStencil(CKDrawState State, CKDWORD ref, CKDWORD readMask, CKDWORD writeMask)
{
    if (!(State.Mid & CKRST_STENCIL_ENABLE))
        return BGFX_STENCIL_NONE;

    CKDWORD hi = State.Hi;
    ref = CKBgfxAdjustStencilRefForWriteMask(ref, readMask, writeMask);
    CKDWORD func    = hi & 0xF;
    CKDWORD failOp  = (hi >> 4) & 0xF;
    CKDWORD zfailOp = (hi >> 8) & 0xF;
    CKDWORD passOp  = (hi >> 12) & 0xF;

    if (func == 0 && failOp == 0 && zfailOp == 0 && passOp == 0)
        return BGFX_STENCIL_NONE;
    if ((writeMask & 0xFF) == 0)
    {
        failOp = VXSTENCILOP_KEEP;
        zfailOp = VXSTENCILOP_KEEP;
        passOp = VXSTENCILOP_KEEP;
    }

    uint32_t stencil = 0;
    stencil |= CKBgfxStencilTest(func);
    stencil |= CKBgfxStencilFailS(failOp);
    stencil |= CKBgfxStencilFailZ(zfailOp);
    stencil |= CKBgfxStencilPassZ(passOp);
    stencil |= BGFX_STENCIL_FUNC_REF(ref);
    stencil |= BGFX_STENCIL_FUNC_RMASK(readMask);

    return stencil;
}
