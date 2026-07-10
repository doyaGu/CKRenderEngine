#include "CKBgfxInternal.h"
#include "CKRasterizerValidation.h"
#include "CKBgfxConfig.h"
#include "VxWindowFunctions.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#include <strings.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
static int fopen_s(FILE **file, const char *path, const char *mode)
{
    *file = fopen(path, mode);
    return *file ? 0 : 1;
}

static int _vsnprintf_s(char *buffer, size_t size, size_t, const char *format, va_list args)
{
    return vsnprintf(buffer, size, format, args);
}

static int _snprintf_s(char *buffer, size_t size, size_t truncate, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = _vsnprintf_s(buffer, size, truncate, format, args);
    va_end(args);
    return result;
}
#endif

static FILE *g_BgfxLogFile = nullptr;

static XString CKBgfxSiblingFile(const char *path, const char *file)
{
    if (!path || !file)
        return "";

    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *last = slash;
    if (!last || (backslash && backslash > last))
        last = backslash;
    if (!last)
        return file;

    XString sibling(path, (int)(last - path + 1));
    sibling << file;
    return sibling;
}

#ifdef _WIN32
XString CKBgfxModuleSiblingFile(const void *address, const char *file)
{
    HMODULE hMod = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)address, &hMod))
        return "";

    XString modulePath = VxGetModuleFileName((INSTANCE_HANDLE)hMod);
    return CKBgfxSiblingFile(modulePath.CStr(), file);
}
#else
XString CKBgfxModuleSiblingFile(const void *address, const char *file)
{
    Dl_info info;
    if (dladdr(address, &info) && info.dli_fname)
        return CKBgfxSiblingFile(info.dli_fname, file);
    return "";
}
#endif

static bool CKBgfxLogNameEquals(const char *lhs, const char *rhs)
{
#ifdef _WIN32
    return lhs && rhs && _stricmp(lhs, rhs) == 0;
#else
    return lhs && rhs && strcasecmp(lhs, rhs) == 0;
#endif
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
        XString path = CKBgfxModuleSiblingFile((const void *)&CKBgfxGetLogFile, "CKBgfx_Trace.log");
        if (path.Length() == 0)
            path = "CKBgfx_Trace.log";
        fopen_s(&g_BgfxLogFile, path.CStr(), "w");
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

#ifdef _WIN32
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
#else
    fputs(msg, stderr);
    fputc('\n', stderr);
#endif

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
    case bgfx::RendererType::Metal:      return "Metal";
    case bgfx::RendererType::Noop:       return "Noop";
    default:                             return "Auto";
    }
}

const char *CKBgfxShaderProfileName(CK_SHADER_PROFILE profile)
{
    switch (profile) {
    case CKRST_SHADER_PROFILE_DX11:  return "dx11";
    case CKRST_SHADER_PROFILE_DX12:  return "dx12";
    case CKRST_SHADER_PROFILE_SPIRV: return "spirv";
    case CKRST_SHADER_PROFILE_GLSL:  return "glsl";
    case CKRST_SHADER_PROFILE_MSL:   return "metal";
    default:                         return "unknown";
    }
}

const char *CKBgfxNativeWindowHandleTypeName(bgfx::NativeWindowHandleType::Enum type)
{
    switch (type) {
    case bgfx::NativeWindowHandleType::Default: return "Default";
    case bgfx::NativeWindowHandleType::Wayland: return "Wayland";
    default:                                    return "Unknown";
    }
}

const char *CKBgfxDebugViewLine0()
{
    return "views: 0 clear 1 bg2d 2 first3d 3 opaque";
}

const char *CKBgfxDebugViewLine1()
{
    return "       4 stencil 5 trans 6 post 7 fg2d";
}

CK_SHADER_PROFILE CKBgfxShaderProfile(bgfx::RendererType::Enum type)
{
    switch (type) {
    case bgfx::RendererType::Direct3D11: return CKRST_SHADER_PROFILE_DX11;
    case bgfx::RendererType::Direct3D12: return CKRST_SHADER_PROFILE_DX12;
    case bgfx::RendererType::Vulkan:     return CKRST_SHADER_PROFILE_SPIRV;
    case bgfx::RendererType::OpenGL:     return CKRST_SHADER_PROFILE_GLSL;
    case bgfx::RendererType::OpenGLES:   return CKRST_SHADER_PROFILE_UNKNOWN;
    case bgfx::RendererType::Metal:      return CKRST_SHADER_PROFILE_MSL;
    default:                             return CKRST_SHADER_PROFILE_UNKNOWN;
    }
}

bgfx::RendererType::Enum CKBgfxParseRequestedRenderer()
{
    char value[32] = {0};
    const char *envBackend = getenv("CKBGFX_RENDERER_BACKEND");
    if (envBackend && envBackend[0] != '\0') {
        strncpy(value, envBackend, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
    } else if (!CKBgfxConfigString("Renderer", "Backend", value, (CKDWORD)sizeof(value)) ||
               CKBgfxLogNameEquals(value, "auto")) {
        return bgfx::RendererType::Count;
    }

    if (CKBgfxLogNameEquals(value, "d3d11") || CKBgfxLogNameEquals(value, "direct3d11"))
        return bgfx::RendererType::Direct3D11;
    if (CKBgfxLogNameEquals(value, "d3d12") || CKBgfxLogNameEquals(value, "direct3d12"))
        return bgfx::RendererType::Direct3D12;
    if (CKBgfxLogNameEquals(value, "vulkan"))
        return bgfx::RendererType::Vulkan;
    if (CKBgfxLogNameEquals(value, "opengl") || CKBgfxLogNameEquals(value, "gl"))
        return bgfx::RendererType::OpenGL;
    if (CKBgfxLogNameEquals(value, "metal") || CKBgfxLogNameEquals(value, "msl"))
        return bgfx::RendererType::Metal;

    CKBgfxLogf("Init", "unknown Renderer/Backend='%s', falling back to auto", value);
    return bgfx::RendererType::Count;
}

bool CKBgfxTryUniformType(CK_UNIFORM_TYPE type, bgfx::UniformType::Enum &result)
{
    switch (type)
    {
    case CKRST_UNIFORM_SAMPLER: result = bgfx::UniformType::Sampler; break;
    case CKRST_UNIFORM_VEC4:    result = bgfx::UniformType::Vec4; break;
    case CKRST_UNIFORM_MAT3:    result = bgfx::UniformType::Mat3; break;
    case CKRST_UNIFORM_MAT4:    result = bgfx::UniformType::Mat4; break;
    default:                    return false;
    }
    return true;
}

bool CKBgfxTryAttrib(CK_VERTEX_ATTRIB attrib, bgfx::Attrib::Enum &result)
{
    switch (attrib)
    {
    case CKRST_ATTRIB_POSITION:  result = bgfx::Attrib::Position; break;
    case CKRST_ATTRIB_NORMAL:    result = bgfx::Attrib::Normal; break;
    case CKRST_ATTRIB_TANGENT:   result = bgfx::Attrib::Tangent; break;
    case CKRST_ATTRIB_BITANGENT: result = bgfx::Attrib::Bitangent; break;
    case CKRST_ATTRIB_COLOR0:    result = bgfx::Attrib::Color0; break;
    case CKRST_ATTRIB_COLOR1:    result = bgfx::Attrib::Color1; break;
    case CKRST_ATTRIB_COLOR2:    result = bgfx::Attrib::Color2; break;
    case CKRST_ATTRIB_COLOR3:    result = bgfx::Attrib::Color3; break;
    case CKRST_ATTRIB_INDICES:   result = bgfx::Attrib::Indices; break;
    case CKRST_ATTRIB_WEIGHT:    result = bgfx::Attrib::Weight; break;
    case CKRST_ATTRIB_TEXCOORD0: result = bgfx::Attrib::TexCoord0; break;
    case CKRST_ATTRIB_TEXCOORD1: result = bgfx::Attrib::TexCoord1; break;
    case CKRST_ATTRIB_TEXCOORD2: result = bgfx::Attrib::TexCoord2; break;
    case CKRST_ATTRIB_TEXCOORD3: result = bgfx::Attrib::TexCoord3; break;
    case CKRST_ATTRIB_TEXCOORD4: result = bgfx::Attrib::TexCoord4; break;
    case CKRST_ATTRIB_TEXCOORD5: result = bgfx::Attrib::TexCoord5; break;
    case CKRST_ATTRIB_TEXCOORD6: result = bgfx::Attrib::TexCoord6; break;
    case CKRST_ATTRIB_TEXCOORD7: result = bgfx::Attrib::TexCoord7; break;
    default:                     return false;
    }
    return true;
}

bool CKBgfxTryAttribType(CK_VERTEX_ATTRIB_TYPE type, bgfx::AttribType::Enum &result)
{
    switch (type)
    {
    case CKRST_ATTRIBTYPE_INT8:   result = bgfx::AttribType::Int8; break;
    case CKRST_ATTRIBTYPE_UINT8:  result = bgfx::AttribType::Uint8; break;
    case CKRST_ATTRIBTYPE_UINT10: result = bgfx::AttribType::Uint10; break;
    case CKRST_ATTRIBTYPE_INT16:  result = bgfx::AttribType::Int16; break;
    case CKRST_ATTRIBTYPE_UINT16: result = bgfx::AttribType::Uint16; break;
    case CKRST_ATTRIBTYPE_HALF:   result = bgfx::AttribType::Half; break;
    case CKRST_ATTRIBTYPE_FLOAT:  result = bgfx::AttribType::Float; break;
    default:                      return false;
    }
    return true;
}

bool CKBgfxTryTextureFormat(VX_PIXELFORMAT pf, bgfx::TextureFormat::Enum &result)
{
    switch (pf)
    {
    case _32_ARGB8888: result = bgfx::TextureFormat::BGRA8; break;
    case _32_ABGR8888: result = bgfx::TextureFormat::RGBA8; break;
    case _24_BGR888:   result = bgfx::TextureFormat::RGB8; break;
    case _16_RGB565:   result = bgfx::TextureFormat::B5G6R5; break;
    case _16_BGR565:   result = bgfx::TextureFormat::R5G6B5; break;
    case _16_ARGB1555: result = bgfx::TextureFormat::BGR5A1; break;
    case _16_ABGR1555: result = bgfx::TextureFormat::RGB5A1; break;
    case _16_ARGB4444: result = bgfx::TextureFormat::BGRA4; break;
    case _16_ABGR4444: result = bgfx::TextureFormat::RGBA4; break;
    case _DXT1:        result = bgfx::TextureFormat::BC1; break;
    case _DXT3:        result = bgfx::TextureFormat::BC2; break;
    case _DXT5:        result = bgfx::TextureFormat::BC3; break;
    default:           return false;
    }
    return true;
}

bool CKBgfxTryPixelFormat(bgfx::TextureFormat::Enum format, VX_PIXELFORMAT &result)
{
    switch (format)
    {
    case bgfx::TextureFormat::BGRA8:  result = _32_ARGB8888; break;
    case bgfx::TextureFormat::RGBA8:  result = _32_ABGR8888; break;
    case bgfx::TextureFormat::RGB8:   result = _24_BGR888; break;
    case bgfx::TextureFormat::B5G6R5: result = _16_RGB565; break;
    case bgfx::TextureFormat::R5G6B5: result = _16_BGR565; break;
    case bgfx::TextureFormat::BGR5A1: result = _16_ARGB1555; break;
    case bgfx::TextureFormat::RGB5A1: result = _16_ABGR1555; break;
    case bgfx::TextureFormat::BGRA4:  result = _16_ARGB4444; break;
    case bgfx::TextureFormat::RGBA4:  result = _16_ABGR4444; break;
    case bgfx::TextureFormat::BC1:    result = _DXT1; break;
    case bgfx::TextureFormat::BC2:    result = _DXT3; break;
    case bgfx::TextureFormat::BC3:    result = _DXT5; break;
    default:                          return false;
    }
    return true;
}

bool CKBgfxTryDepthFormat(CK_DEPTH_FORMAT fmt, bgfx::TextureFormat::Enum &result)
{
    switch (fmt)
    {
    case CKRST_DEPTHFMT_D16:    result = bgfx::TextureFormat::D16; break;
    case CKRST_DEPTHFMT_D24:    result = bgfx::TextureFormat::D24; break;
    case CKRST_DEPTHFMT_D24S8:  result = bgfx::TextureFormat::D24S8; break;
    case CKRST_DEPTHFMT_D32F:   result = bgfx::TextureFormat::D32F; break;
    default:                    return false;
    }
    return true;
}

CKDWORD CKBgfxImageRowBytes(CKDWORD width, CKDWORD bitsPerPixel)
{
    if (bitsPerPixel == 0 || (bitsPerPixel % 8) != 0)
        return 0;
    uint64_t rowBytes = (uint64_t)width * (uint64_t)bitsPerPixel / 8;
    return rowBytes > 0xffffffffu ? 0 : (CKDWORD)rowBytes;
}

CKDWORD CKBgfxResolveImagePitch(CKDWORD width, CKDWORD height,
                                CKDWORD bitsPerPixel, CKDWORD pitchOrImageSize)
{
    const CKDWORD rowBytes = CKBgfxImageRowBytes(width, bitsPerPixel);
    if (rowBytes == 0)
        return 0;
    if (pitchOrImageSize == 0)
        return rowBytes;
    if (pitchOrImageSize < rowBytes)
        return rowBytes;

    // VxImageDescEx aliases BytesPerLine and TotalImageSize. Some legacy
    // upload paths fill the uncompressed total size into the same field.
    const uint64_t tightImageSize = (uint64_t)rowBytes * (uint64_t)height;
    if (height > 1 &&
        (uint64_t)pitchOrImageSize >= tightImageSize &&
        (pitchOrImageSize % height) == 0) {
        const CKDWORD candidatePitch = pitchOrImageSize / height;
        if (candidatePitch >= rowBytes)
            return candidatePitch;
    }

    return pitchOrImageSize;
}

CKDWORD CKBgfxTextureMipCount(CKDWORD width, CKDWORD height, CKDWORD depth)
{
    CKDWORD count = 1;
    while (width > 1 || height > 1 || depth > 1) {
        width = (width > 1) ? (width >> 1) : 1;
        height = (height > 1) ? (height >> 1) : 1;
        depth = (depth > 1) ? (depth >> 1) : 1;
        ++count;
    }
    return count;
}

CKBOOL CKBgfxIsAutoMipRequest(CKDWORD requestedMipCount, CKDWORD fullMipCount)
{
    return (requestedMipCount == (CKDWORD)-1 ||
            requestedMipCount > fullMipCount) ? TRUE : FALSE;
}

CKBOOL CKBgfxShouldCreateTextureMipChain(CKDWORD requestedMipCount,
                                          CKDWORD fullMipCount,
                                          CKBOOL openGL,
                                          CKBOOL autoMipDataAvailable)
{
    if (openGL && CKBgfxIsAutoMipRequest(requestedMipCount, fullMipCount))
        return autoMipDataAvailable ? TRUE : FALSE;

    return requestedMipCount > 1 ? TRUE : FALSE;
}

CKBgfxAutoMipUpdateAction CKBgfxResolveAutoMipUpdateAction(CKBOOL requestedAutoMips,
                                                           CKDWORD currentMipCount,
                                                           CKBOOL fullBaseUpdate,
                                                           CKBOOL canGenerateFullMipChain)
{
    if (!requestedAutoMips)
        return CKBGFX_AUTOMIP_UPDATE_NONE;

    if (!fullBaseUpdate)
        return CKBGFX_AUTOMIP_UPDATE_KEEP;

    if (canGenerateFullMipChain && currentMipCount <= 1)
        return CKBGFX_AUTOMIP_UPDATE_PROMOTE;

    if (!canGenerateFullMipChain && currentMipCount > 1)
        return CKBGFX_AUTOMIP_UPDATE_DEMOTE;

    return CKBGFX_AUTOMIP_UPDATE_KEEP;
}

CKBOOL CKBgfxSamplerWantsMipMaps(const CKSamplerDesc *s)
{
    if (!s)
        return TRUE;

    switch (s->MipFilter)
    {
    case CKRST_FILTER_NONE:
        return FALSE;
    case CKRST_FILTER_NEAREST:
    case CKRST_FILTER_MIPNEAREST:
    case CKRST_FILTER_MIPLINEAR:
    case CKRST_FILTER_LINEARMIPNEAREST:
    case CKRST_FILTER_LINEARMIPLINEAR:
    case CKRST_FILTER_ANISOTROPIC:
        return TRUE;
    default:
        return TRUE;
    }
}

CKBOOL CKBgfxTrySamplerFlags(const CKSamplerDesc *s, uint32_t &flags)
{
    flags = BGFX_SAMPLER_NONE;
    if (CKRasterizerValidateSampler(s) != CK_OK)
        return FALSE;
    if (!s)
        return TRUE;

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
    case CKRST_FILTER_MIPNEAREST:
    case CKRST_FILTER_LINEARMIPNEAREST:
        flags |= BGFX_SAMPLER_MIP_POINT;
        break;
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

    return TRUE;
}

uint32_t CKBgfxSamplerFlags(const CKSamplerDesc *s)
{
    uint32_t flags = BGFX_SAMPLER_NONE;
    CKBgfxTrySamplerFlags(s, flags);
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

CKERROR CKBgfxTryState(CKDrawState State, uint64_t &bgfxState)
{
    bgfxState = 0;
    const CKERROR validation = CKRasterizerValidateDrawState(State);
    if (validation != CK_OK)
        return validation;

    CKDWORD lo = State.Lo;

    const CKDWORD depthFunc = (lo >> 6) & 0xF;
    const CKDWORD cullMode = (lo >> 10) & 0x3;
    const CKDWORD fillMode = (lo >> 12) & 0x3;
    const CKDWORD blendSrc = (lo >> 16) & 0xF;
    const CKDWORD blendDst = (lo >> 20) & 0xF;
    const CKDWORD blendSrcA = (lo >> 24) & 0xF;
    const CKDWORD blendDstA = (lo >> 28) & 0xF;
    const CKDWORD mid = State.Mid;
    const CKDWORD blendEq = mid & 0x7;
    const CKDWORD blendEqA = (mid >> 3) & 0x7;
    const CKDWORD primitive = (mid >> 6) & 0x7;

    if (lo & CKRST_STATE_WRITE_R) bgfxState |= BGFX_STATE_WRITE_R;
    if (lo & CKRST_STATE_WRITE_G) bgfxState |= BGFX_STATE_WRITE_G;
    if (lo & CKRST_STATE_WRITE_B) bgfxState |= BGFX_STATE_WRITE_B;
    if (lo & CKRST_STATE_WRITE_A) bgfxState |= BGFX_STATE_WRITE_A;

    if (lo & CKRST_STATE_DEPTH_TEST)
    {
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
        default: break;
        }
    }

    if (lo & CKRST_STATE_DEPTH_WRITE)
        bgfxState |= BGFX_STATE_WRITE_Z;

    if (cullMode == 1) bgfxState |= BGFX_STATE_CULL_CW;
    else if (cullMode == 2) bgfxState |= BGFX_STATE_CULL_CCW;

    if (lo & CKRST_STATE_MSAA)
        bgfxState |= BGFX_STATE_MSAA;

    if (lo & CKRST_STATE_ALPHA_COVERAGE)
        bgfxState |= BGFX_STATE_BLEND_ALPHA_TO_COVERAGE;

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

    if (blendEq != 0)
    {
        if (blendEqA != 0)
            bgfxState |= BGFX_STATE_BLEND_EQUATION_SEPARATE(
                CKBgfxBlendEquation(blendEq), CKBgfxBlendEquation(blendEqA));
        else
            bgfxState |= BGFX_STATE_BLEND_EQUATION(CKBgfxBlendEquation(blendEq));
    }

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
        switch (primitive)
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

    return CK_OK;
}

uint64_t CKBgfxState(CKDrawState State)
{
    uint64_t bgfxState = 0;
    CKBgfxTryState(State, bgfxState);
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
