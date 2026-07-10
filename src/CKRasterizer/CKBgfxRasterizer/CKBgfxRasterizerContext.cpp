#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"
#include "CKBgfxDrawMapTrace.h"
#include "CKRasterizerValidation.h"
#include "../../CKDrawAnnotation.h"

#include <bgfx/platform.h>
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

static_assert(BGFX_API_VERSION == 143, "Review CKBgfxRasterizer mappings before updating bgfx");
static_assert(CKRST_DISCARD_BINDINGS == BGFX_DISCARD_BINDINGS, "discard ABI mismatch");
static_assert(CKRST_DISCARD_INDEX_BUFFER == BGFX_DISCARD_INDEX_BUFFER, "discard ABI mismatch");
static_assert(CKRST_DISCARD_INSTANCE_DATA == BGFX_DISCARD_INSTANCE_DATA, "discard ABI mismatch");
static_assert(CKRST_DISCARD_STATE == BGFX_DISCARD_STATE, "discard ABI mismatch");
static_assert(CKRST_DISCARD_TRANSFORM == BGFX_DISCARD_TRANSFORM, "discard ABI mismatch");
static_assert(CKRST_DISCARD_VERTEX_STREAMS == BGFX_DISCARD_VERTEX_STREAMS, "discard ABI mismatch");
static_assert(CKRST_DISCARD_ALL == BGFX_DISCARD_ALL, "discard ABI mismatch");
static_assert(CKRST_FRAME_CAPTURE == BGFX_FRAME_DEBUG_CAPTURE, "frame ABI mismatch");
static_assert(CKRST_FRAME_DISCARD == BGFX_FRAME_DISCARD, "frame ABI mismatch");
static_assert(CKRST_FRAME_FLUSH == BGFX_FRAME_FLUSH, "frame ABI mismatch");

static VxMutex g_BgfxContextMutex;
static CKBgfxRasterizerContext *g_BgfxActiveContext = NULL;

static bool CKBgfxClaimActiveContext(CKBgfxRasterizerContext *Context)
{
    VxMutexLock lock(g_BgfxContextMutex);
    if (g_BgfxActiveContext && g_BgfxActiveContext != Context)
        return false;
    g_BgfxActiveContext = Context;
    return true;
}

static void CKBgfxReleaseActiveContext(CKBgfxRasterizerContext *Context)
{
    VxMutexLock lock(g_BgfxContextMutex);
    if (g_BgfxActiveContext == Context)
        g_BgfxActiveContext = NULL;
}

static uint32_t SampleBytesChecksum(const void *data, uint32_t size)
{
    if (!data || size == 0)
        return 0;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t hash = 2166136261u;
    uint32_t step = size > 4096 ? (size / 4096) : 1;
    for (uint32_t i = 0; i < size; i += step) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t FirstDword(const void *data, uint32_t size)
{
    uint32_t value = 0;
    if (data && size >= sizeof(value))
        memcpy(&value, data, sizeof(value));
    return value;
}

static bool CKBgfxIsOpenGLRenderer()
{
    bgfx::RendererType::Enum type = bgfx::getRendererType();
    return type == bgfx::RendererType::OpenGL ||
           type == bgfx::RendererType::OpenGLES;
}

static CKDWORD CKBgfxMapFormatCaps(CKDWORD NativeCaps, CKBOOL AllowReadback,
                                   CKBOOL AllowComparison);

static const char *CKBgfxViewModeName(CK_VIEW_MODE Mode)
{
    switch (Mode) {
    case CKRST_VIEWMODE_DEFAULT:    return "Default";
    case CKRST_VIEWMODE_SEQUENTIAL: return "Sequential";
    case CKRST_VIEWMODE_DEPTH_ASC:  return "DepthAscending";
    case CKRST_VIEWMODE_DEPTH_DESC: return "DepthDescending";
    default:                        return "Unknown";
    }
}

static void CKBgfxCopyDebugText(char *Dst, CKDWORD DstSize, CKSTRING Src)
{
    if (!Dst || DstSize == 0)
        return;
    if (!Src)
        Src = (CKSTRING)"";
    strncpy(Dst, Src, DstSize - 1);
    Dst[DstSize - 1] = '\0';
}

static CKBOOL CKBgfxDrawMapChannelEnabled(CKDWORD Flags, CKDWORD Channel)
{
    if ((Flags & CKRST_DEBUG_DRAWMAP) == 0)
        return FALSE;
    return (Flags & Channel) != 0 ? TRUE : FALSE;
}

static CKDWORD CKBgfxHashDword(CKDWORD Hash, CKDWORD Value)
{
    Hash ^= Value;
    Hash *= 16777619u;
    return Hash;
}

static CKDWORD CKBgfxHashDrawState(CKDrawState State)
{
    CKDWORD hash = CKBGFX_DRAWMAP_HASH_INIT;
    hash = CKBgfxHashDword(hash, State.Lo);
    hash = CKBgfxHashDword(hash, State.Mid);
    hash = CKBgfxHashDword(hash, State.Hi);
    return hash;
}

static CKDWORD CKBgfxHashStencil(CKDWORD Ref, CKDWORD ReadMask, CKDWORD WriteMask)
{
    CKDWORD hash = CKBGFX_DRAWMAP_HASH_INIT;
    hash = CKBgfxHashDword(hash, Ref & 0xFF);
    hash = CKBgfxHashDword(hash, ReadMask & 0xFF);
    hash = CKBgfxHashDword(hash, WriteMask & 0xFF);
    return hash;
}

static CKDWORD CKBgfxHashProgram(const CKBgfxProgramRecord *Record)
{
    CKDWORD hash = CKBGFX_DRAWMAP_HASH_INIT;
    if (!Record)
        return 0;
    hash = CKBgfxHashDword(hash, Record->Handle.idx);
    hash = CKBgfxHashDword(hash, Record->VertexShader);
    hash = CKBgfxHashDword(hash, Record->PixelShader);
    return hash;
}

static const char *CKBgfxTextureKindName(const CKBgfxTextureRecord *Record)
{
    if (!Record)
        return "none";
    if (Record->IsDepth)
        return "depth";
    if (Record->Flags & CKRST_TEXTURE_CUBEMAP)
        return "cube";
    if ((Record->Flags & CKRST_TEXTURE_VOLUMEMAP) && Record->Depth > 1)
        return "volume";
    return "2d";
}

static bool CKBgfxCanGenerateMipMaps(const VxImageDescEx *desc)
{
    return desc &&
           desc->Image &&
           desc->Width > 0 &&
           desc->Height > 0 &&
           desc->BitsPerPixel > 0 &&
           (desc->BitsPerPixel % 8) == 0;
}

static CKBYTE *CKBgfxCreateGeneratedMipMap(const VxImageDescEx &src, VxImageDescEx &dst)
{
    if (!CKBgfxCanGenerateMipMaps(&src))
        return NULL;

    dst = src;
    dst.Width = (src.Width > 1) ? (src.Width >> 1) : 1;
    dst.Height = (src.Height > 1) ? (src.Height >> 1) : 1;
    dst.BytesPerLine = dst.Width * dst.BitsPerPixel / 8;
    const CKDWORD imageSize = (CKDWORD)dst.BytesPerLine * (CKDWORD)dst.Height;
    dst.Image = new CKBYTE[imageSize];
    VxDoBlit(src, dst);
    return (CKBYTE *)dst.Image;
}

static uint64_t CKBgfxTextureFlagsFromDescFlags(CKDWORD flags)
{
    uint64_t texFlags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE;
    if (flags & CKRST_TEXTURE_RENDERTARGET)
        texFlags |= BGFX_TEXTURE_RT;
    if (flags & CKRST_TEXTURE_READBACK)
        texFlags |= BGFX_TEXTURE_READ_BACK;
    if (flags & CKRST_TEXTURE_BLIT_DST)
        texFlags |= BGFX_TEXTURE_BLIT_DST;
    if (flags & CKRST_TEXTURE_COMPUTE_WRITE)
        texFlags |= BGFX_TEXTURE_COMPUTE_WRITE;
    return texFlags;
}

static bool CKBgfxIsCompressedTextureFormat(bgfx::TextureFormat::Enum fmt)
{
    return fmt == bgfx::TextureFormat::BC1 ||
           fmt == bgfx::TextureFormat::BC2 ||
           fmt == bgfx::TextureFormat::BC3;
}

static bool CKBgfxCanExposeReadback(VX_PIXELFORMAT pf,
                                    bgfx::TextureFormat::Enum fmt)
{
    return (pf == _32_ARGB8888 && fmt == bgfx::TextureFormat::BGRA8) ||
           (pf == _32_ABGR8888 && fmt == bgfx::TextureFormat::RGBA8);
}

static bool CKBgfxCanUseGeneratedMipChain(CKDWORD flags,
                                           bgfx::TextureFormat::Enum fmt,
                                           CKDWORD depth,
                                           const VxImageDescEx *data)
{
    const bool isCube = (flags & CKRST_TEXTURE_CUBEMAP) != 0;
    const bool isVolume = (flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && depth > 1;
    if (isCube || isVolume)
        return false;
    if (flags & (CKRST_TEXTURE_RENDERTARGET | CKRST_TEXTURE_DEPTHSTENCIL))
        return false;
    if (CKBgfxIsCompressedTextureFormat(fmt))
        return false;
    return CKBgfxCanGenerateMipMaps(data);
}

static void CKBgfxDestroySamplerBaseHandle(CKBgfxTextureRecord *rec)
{
    if (!rec)
        return;
    if (bgfx::isValid(rec->SamplerBaseHandle)) {
        bgfx::destroy(rec->SamplerBaseHandle);
        rec->SamplerBaseHandle = BGFX_INVALID_HANDLE;
    }
    rec->SamplerBaseValid = FALSE;
}

static bool CKBgfxCanUseSamplerBaseHandle(const CKBgfxTextureRecord *rec)
{
    if (!rec)
        return false;
    if (rec->MipCount <= 1 || rec->IsDepth)
        return false;
    if ((rec->Flags & CKRST_TEXTURE_CUBEMAP) != 0)
        return false;
    if ((rec->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && rec->Depth > 1)
        return false;
    if (rec->Flags & (CKRST_TEXTURE_RENDERTARGET |
                      CKRST_TEXTURE_BLIT_DST |
                      CKRST_TEXTURE_COMPUTE_WRITE))
        return false;
    return true;
}

static bool CKBgfxEnsureSamplerBaseHandle(CKBgfxTextureRecord *rec)
{
    if (!CKBgfxCanUseSamplerBaseHandle(rec))
        return false;
    if (bgfx::isValid(rec->SamplerBaseHandle))
        return true;

    bgfx::TextureHandle handle = bgfx::createTexture2D(
        (uint16_t)rec->Width, (uint16_t)rec->Height, false, 1,
        rec->Format, BGFX_TEXTURE_NONE, NULL);
    if (!bgfx::isValid(handle))
        return false;

    rec->SamplerBaseHandle = handle;
    rec->SamplerBaseValid = FALSE;
    return true;
}

static void CKBgfxResetAutoMipBaseCache(CKBgfxTextureRecord *rec)
{
    if (!rec)
        return;
    delete[] rec->AutoMipBaseDesc.Image;
    rec->AutoMipBaseDesc = VxImageDescEx();
    rec->AutoMipBaseValid = FALSE;
}

static bool CKBgfxEnsureAutoMipBaseCache(CKBgfxTextureRecord *rec,
                                          const VxImageDescEx *formatDesc)
{
    if (!rec || !formatDesc)
        return false;

    const CKDWORD bpp = rec->BitsPerPixel > 0 ? rec->BitsPerPixel : (CKDWORD)formatDesc->BitsPerPixel;
    const CKDWORD pitch = CKBgfxImageRowBytes(rec->Width, bpp);
    if (pitch == 0 || rec->Height == 0)
        return false;

    const CKDWORD imageSize = pitch * rec->Height;
    if (rec->AutoMipBaseDesc.Image &&
        rec->AutoMipBaseDesc.Width == (int)rec->Width &&
        rec->AutoMipBaseDesc.Height == (int)rec->Height &&
        rec->AutoMipBaseDesc.BitsPerPixel == (int)bpp &&
        rec->AutoMipBaseDesc.BytesPerLine == (int)pitch) {
        return true;
    }

    CKBYTE *image = new CKBYTE[imageSize];
    memset(image, 0, imageSize);

    delete[] rec->AutoMipBaseDesc.Image;
    rec->AutoMipBaseDesc.Set(*formatDesc);
    rec->AutoMipBaseDesc.Width = (int)rec->Width;
    rec->AutoMipBaseDesc.Height = (int)rec->Height;
    rec->AutoMipBaseDesc.BitsPerPixel = (int)bpp;
    rec->AutoMipBaseDesc.BytesPerLine = (int)pitch;
    rec->AutoMipBaseDesc.ColorMap = NULL;
    rec->AutoMipBaseDesc.Image = image;
    rec->AutoMipBaseValid = FALSE;
    return true;
}

static bool CKBgfxUpdateAutoMipBaseCache(CKBgfxTextureRecord *rec,
                                          const VxImageDescEx *data,
                                          uint16_t x, uint16_t y,
                                          uint16_t width, uint16_t height)
{
    if (!rec || !data || !data->Image)
        return false;
    if (!CKBgfxEnsureAutoMipBaseCache(rec, data))
        return false;

    const CKDWORD bpp = rec->AutoMipBaseDesc.BitsPerPixel;
    const CKDWORD bytesPerPixel = bpp / 8;
    if (bytesPerPixel == 0)
        return false;
    if ((CKDWORD)x + width > rec->Width || (CKDWORD)y + height > rec->Height)
        return false;

    const CKDWORD dstPitch = (CKDWORD)rec->AutoMipBaseDesc.BytesPerLine;
    const CKDWORD rowBytes = (CKDWORD)width * bytesPerPixel;
    const CKDWORD srcPitch = CKBgfxResolveImagePitch(
        width, height, bpp,
        data->BytesPerLine > 0 ? (CKDWORD)data->BytesPerLine : 0);
    if (srcPitch == 0)
        return false;
    CKBYTE *dst = rec->AutoMipBaseDesc.Image + y * dstPitch + x * bytesPerPixel;
    const CKBYTE *src = (const CKBYTE *)data->Image;
    for (uint16_t row = 0; row < height; ++row)
        memcpy(dst + row * dstPitch, src + row * srcPitch, rowBytes);

    if (x == 0 && y == 0 && width == rec->Width && height == rec->Height)
        rec->AutoMipBaseValid = TRUE;

    return rec->AutoMipBaseValid != FALSE;
}

static bool CKBgfxRecreateTexture2D(CKBgfxTextureRecord *rec, bool hasMips)
{
    if (!rec)
        return false;
    if ((rec->Flags & CKRST_TEXTURE_CUBEMAP) != 0 ||
        ((rec->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && rec->Depth > 1))
        return false;

    bgfx::TextureHandle handle = bgfx::createTexture2D(
        (uint16_t)rec->Width, (uint16_t)rec->Height, hasMips, 1,
        rec->Format, CKBgfxTextureFlagsFromDescFlags(rec->Flags), NULL);
    if (!bgfx::isValid(handle))
        return false;

    if (bgfx::isValid(rec->Handle))
        bgfx::destroy(rec->Handle);
    rec->Handle = handle;
    rec->MipCount = hasMips ? CKBgfxTextureMipCount(rec->Width, rec->Height, 1) : 1;
    if (!hasMips)
        CKBgfxDestroySamplerBaseHandle(rec);
    return true;
}

static CKBOOL CKBgfxUpdateGeneratedMipMaps(CKBgfxTextureRecord *rec,
                                           const VxImageDescEx *data)
{
    if (!rec || rec->MipCount <= 1)
        return TRUE;
    if (!CKBgfxCanGenerateMipMaps(data))
        return FALSE;

    VxImageDescEx previous = *data;
    CKBYTE *previousGenerated = NULL;
    CKBOOL complete = TRUE;
    for (CKDWORD level = 1; level < rec->MipCount; ++level) {
        VxImageDescEx mipDesc;
        CKBYTE *generated = CKBgfxCreateGeneratedMipMap(previous, mipDesc);
        delete[] previousGenerated;
        previousGenerated = generated;
        if (!generated) {
            complete = FALSE;
            break;
        }

        uint16_t mipW = (uint16_t)mipDesc.Width;
        uint16_t mipH = (uint16_t)mipDesc.Height;
        CKDWORD mipBpp = rec->BitsPerPixel > 0 ? rec->BitsPerPixel : (CKDWORD)mipDesc.BitsPerPixel;
        CKDWORD mipRowBytes = mipDesc.BytesPerLine > 0
            ? (CKDWORD)mipDesc.BytesPerLine
            : (CKDWORD)mipW * mipBpp / 8;
        const bgfx::Memory *mipMem = bgfx::copy(mipDesc.Image, mipRowBytes * mipH);
        bgfx::updateTexture2D(rec->Handle, 0, (uint8_t)level, 0, 0, mipW, mipH, mipMem);
        previous = mipDesc;
    }
    delete[] previousGenerated;
    return complete;
}

static void *CKBgfxSdlPointerProperty(SDL_PropertiesID props, const char *name)
{
    return SDL_GetPointerProperty(props, name, NULL);
}

static bool CKBgfxFillSDLPlatformData(WIN_HANDLE Window, bgfx::PlatformData &platformData)
{
    platformData = bgfx::PlatformData();

    SDL_Window *window = static_cast<SDL_Window *>(Window);
    if (!window)
        return false;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props)
        return false;

#if defined(_WIN32)
    platformData.nwh = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER);
    return platformData.nwh != NULL;
#elif defined(__APPLE__)
    platformData.nwh = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER);
    return platformData.nwh != NULL;
#elif defined(__linux__)
    void *waylandSurface = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER);
    if (waylandSurface) {
        platformData.ndt = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER);
        platformData.nwh = waylandSurface;
        platformData.type = bgfx::NativeWindowHandleType::Wayland;
        return platformData.ndt != NULL;
    }

    const Sint64 x11Window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (x11Window != 0) {
        platformData.ndt = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER);
        platformData.nwh = reinterpret_cast<void *>(static_cast<uintptr_t>(x11Window));
        return platformData.ndt != NULL;
    }

    return false;
#else
    return false;
#endif
}

static void CKBgfxResolveFullscreenWindowSize(WIN_HANDLE Window, CKBOOL Fullscreen,
                                              int &width, int &height)
{
    if (!Fullscreen || !Window)
        return;

    SDL_Window *window = static_cast<SDL_Window *>(Window);
    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(window, &windowW, &windowH);
    if (windowW > 0 && windowH > 0) {
        width = windowW;
        height = windowH;
    }
}

// ===========================================================================
// Helper: allocate a context-local wrapper slot. Slot zero stays invalid.
// ===========================================================================

template <typename T>
static CKDWORD AllocateSlot(XArray<T *> &arr, CKDWORD maxHandles,
                            VxMutex &tableMutex)
{
    VxMutexLock lock(tableMutex);
    if (arr.Size() == 0)
        arr.PushBack(NULL);
    for (int i = 1; i < arr.Size(); ++i) {
        if (!arr[i])
            return (CKDWORD)i;
    }
    if (maxHandles != 0 && (CKDWORD)(arr.Size() - 1) >= maxHandles)
        return 0;
    arr.PushBack(NULL);
    return (CKDWORD)(arr.Size() - 1);
}

template <typename T>
static T *GetSlot(XArray<T *> &arr, CKDWORD index,
                  VxMutex &tableMutex)
{
    VxMutexLock lock(tableMutex);
    if (index == 0 || (int)index >= arr.Size())
        return NULL;
    return arr[index];
}

template <typename RecordT>
static void DestroyRecord(RecordT *rec)
{
    if (rec)
    {
        if (bgfx::isValid(rec->Handle))
            bgfx::destroy(rec->Handle);
        delete rec;
    }
}

template <typename RecordT>
static void DestroyAllRecords(XArray<RecordT *> &arr)
{
    for (int i = 0; i < arr.Size(); ++i)
        DestroyRecord(arr[i]);
    arr.Clear();
}

// ===========================================================================
// Helper: CK_DISCARD_FLAGS -> bgfx discard flags
// ===========================================================================

static bool CKBgfxTryDiscardFlags(CKDWORD flags, uint8_t &result)
{
    const CKDWORD individualMask = CKRST_DISCARD_BINDINGS |
                                   CKRST_DISCARD_INDEX_BUFFER |
                                   CKRST_DISCARD_INSTANCE_DATA |
                                   CKRST_DISCARD_STATE |
                                   CKRST_DISCARD_TRANSFORM |
                                   CKRST_DISCARD_VERTEX_STREAMS;
    if (flags != CKRST_DISCARD_ALL && (flags & ~individualMask) != 0)
        return false;
    result = (uint8_t)flags;
    return true;
}

static uint8_t ToBgfxDiscardFlags(CKDWORD flags)
{
    uint8_t result = BGFX_DISCARD_NONE;
    CKBgfxTryDiscardFlags(flags, result);
    return result;
}

// ===========================================================================
// CKBgfxEncoder
// ===========================================================================

CKBgfxEncoder::CKBgfxEncoder()
    : m_Active{FALSE}, m_Context(NULL), m_Encoder(NULL),
      m_OwnsNativeEncoder(FALSE), m_Status(CK_OK),
      m_StencilRef(0), m_StencilReadMask(0xFF), m_StencilWriteMask(0xFF),
      m_CurrentLayout(0), m_PointSize(0),
      m_CachedDrawState(), m_CachedBgfxState(0),
      m_DebugVertexBindingMask(0), m_DebugTextureBindingMask(0),
      m_DebugIndexBuffer(0), m_DebugIndexStart(0), m_DebugIndexCount(0),
      m_DebugIndexHandle(0), m_DebugSpecializationHash(0),
      m_DebugSpecializationValid(FALSE) {
    m_LastMarker[0] = '\0';
    memset(m_DebugVertexBindings, 0, sizeof(m_DebugVertexBindings));
    memset(m_DebugTextureBindings, 0, sizeof(m_DebugTextureBindings));
}

CKBgfxEncoder::~CKBgfxEncoder() {}

CKERROR CKBgfxEncoder::GetStatus() const
{
    return m_Status;
}

void CKBgfxEncoder::SetError(CKERROR Error)
{
    if (m_Status == CK_OK && Error != CK_OK)
        m_Status = Error;
}

CKBOOL CKBgfxEncoder::CanSubmit()
{
    if (!m_Active.load(std::memory_order_acquire) || !m_Context ||
        m_OwnerThread != VxThread::GetCurrentVxThreadId())
    {
        SetError(CKERR_INVALIDOPERATION);
        return FALSE;
    }
    return m_Status == CK_OK ? TRUE : FALSE;
}

static void ApplyBgfxState(bgfx::Encoder *encoder, uint64_t bgfxState)
{
    if (encoder)
        encoder->setState(bgfxState);
    else
        bgfx::setState(bgfxState);
}

static void ApplyStencil(bgfx::Encoder *encoder, CKDrawState state,
                         CKDWORD ref, CKDWORD readMask, CKDWORD writeMask)
{
    writeMask &= 0xFF;
    // bgfx has no stencil write-mask API; partial masks are only approximated
    // (see CKBgfxBuildFrontStencil). Surface them instead of failing silently.
    if ((state.Mid & CKRST_STENCIL_ENABLE) && writeMask != 0x00 && writeMask != 0xFF)
    {
        static int s_PartialWriteMaskLogCount = 0;
        if (s_PartialWriteMaskLogCount < 8)
        {
            CKBgfxLogf("Stencil",
                       "partial stencil write mask 0x%02X (read 0x%02X ref 0x%02X) is approximated: "
                       "bgfx stencil ops always write all 8 bits",
                       writeMask, readMask & 0xFF, ref & 0xFF);
            ++s_PartialWriteMaskLogCount;
        }
    }
    uint32_t fstencil = CKBgfxBuildFrontStencil(state, ref, readMask, writeMask);
    uint32_t bstencil = CKBgfxBuildBackStencil(state, ref, readMask, writeMask);
    if (encoder)
        encoder->setStencil(fstencil, bstencil);
    else
        bgfx::setStencil(fstencil, bstencil);
}

void CKBgfxEncoder::SetState(CKDrawState State)
{
    if (!CanSubmit())
        return;
    uint64_t bgfxState = 0;
    const CKERROR stateError = CKBgfxTryState(State, bgfxState);
    if (stateError != CK_OK) {
        SetError(stateError);
        return;
    }
    m_CachedDrawState = State;
    m_CachedBgfxState = bgfxState;

    uint64_t finalState = m_CachedBgfxState;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);
    ApplyBgfxState(m_Encoder, finalState);
    ApplyStencil(m_Encoder, State, m_StencilRef, m_StencilReadMask, m_StencilWriteMask);
}

void CKBgfxEncoder::SetStencilRef(CKDWORD Ref)
{
    if (!CanSubmit())
        return;
    if (Ref > 0xFF) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    m_StencilRef = Ref;
    if (m_CachedDrawState.Mid & CKRST_STENCIL_ENABLE)
        ApplyStencil(m_Encoder, m_CachedDrawState, m_StencilRef, m_StencilReadMask, m_StencilWriteMask);
}

void CKBgfxEncoder::SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask)
{
    if (!CanSubmit())
        return;
    if (ReadMask > 0xFF || WriteMask > 0xFF) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (WriteMask != 0x00 && WriteMask != 0xFF) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    m_StencilReadMask = ReadMask;
    m_StencilWriteMask = WriteMask;
    if (m_CachedDrawState.Mid & CKRST_STENCIL_ENABLE)
        ApplyStencil(m_Encoder, m_CachedDrawState, m_StencilRef, m_StencilReadMask, m_StencilWriteMask);
}

void CKBgfxEncoder::SetScissor(const CKRECT *Rect)
{
    if (!CanSubmit())
        return;
    if (!Rect)
    {
        if (m_Encoder)
            m_Encoder->setScissor();
        else
            bgfx::setScissor();
        return;
    }
    if (Rect->left < 0 || Rect->top < 0 ||
        Rect->right < Rect->left || Rect->bottom < Rect->top ||
        Rect->left > 0xffff || Rect->top > 0xffff ||
        Rect->right - Rect->left > 0xffff ||
        Rect->bottom - Rect->top > 0xffff) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    uint16_t x = (uint16_t)Rect->left;
    uint16_t y = (uint16_t)Rect->top;
    uint16_t w = (uint16_t)(Rect->right - Rect->left);
    uint16_t h = (uint16_t)(Rect->bottom - Rect->top);
    if (m_Encoder)
        m_Encoder->setScissor(x, y, w, h);
    else
        bgfx::setScissor(x, y, w, h);
}

void CKBgfxEncoder::SetPointSize(float Size)
{
    if (!CanSubmit())
        return;
    if (!(Size >= 0.0f && Size <= 15.0f)) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    m_PointSize = (CKDWORD)(Size + 0.5f);

    uint64_t finalState = m_CachedBgfxState;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);
    ApplyBgfxState(m_Encoder, finalState);
}

void CKBgfxEncoder::SetTransform(CKDWORD TransformIndex, CKDWORD Count)
{
    if (!CanSubmit())
        return;
    const CKDWORD allocatedTransforms = m_Context
        ? m_Context->m_TransformCount.load(std::memory_order_acquire) : 0;
    if (!m_Context || TransformIndex == CKRST_INVALID_TRANSFORM || Count == 0 ||
        TransformIndex + Count < TransformIndex ||
        TransformIndex + Count > allocatedTransforms) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }

    const VxMatrix *mtx = &m_Context->m_TransformCache[TransformIndex];
    if (m_Encoder)
        m_Encoder->setTransform((const void *)mtx, (uint16_t)Count);
    else
        bgfx::setTransform((const void *)mtx, (uint16_t)Count);
}

void CKBgfxEncoder::SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                                     CKDWORD StartVertex, CKDWORD VertexCount,
                                     CKDWORD Layout)
{
    if (!CanSubmit())
        return;
    if (!m_Context) {
        SetError(CKERR_INVALIDOPERATION);
        return;
    }
    CKBgfxVertexBufferRecord *rec = m_Context->GetVertexBuffer(Buffer);
    CKBgfxVertexLayoutRecord *layoutRec = m_Context->GetVertexLayout(Layout);
    if (!rec || !layoutRec || Stream >= m_Context->m_CapsDesc.MaxVertexStreams ||
        VertexCount == 0 || StartVertex > rec->VertexCount ||
        VertexCount > rec->VertexCount - StartVertex) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }

    const bgfx::VertexLayoutHandle layoutHandle = layoutRec->Handle;

    m_CurrentLayout = Layout;

    if (m_Encoder)
        m_Encoder->setVertexBuffer((uint8_t)Stream, rec->Handle, StartVertex, VertexCount, layoutHandle);
    else
        bgfx::setVertexBuffer((uint8_t)Stream, rec->Handle, StartVertex, VertexCount, layoutHandle);

    if (m_Context->m_DrawMapSubmitActive && Stream < CKRST_MAX_VERTEX_STREAMS) {
        m_DebugVertexBindings[Stream].Buffer = Buffer;
        m_DebugVertexBindings[Stream].Start = StartVertex;
        m_DebugVertexBindings[Stream].Count = VertexCount;
        m_DebugVertexBindings[Stream].BgfxHandle = rec->Handle.idx;
        m_DebugVertexBindings[Stream].LayoutHandle =
            bgfx::isValid(layoutHandle) ? layoutHandle.idx : 0xffff;
        m_DebugVertexBindingMask |= (1u << Stream);
    }
}

void CKBgfxEncoder::SetIndexBuffer(CKDWORD Buffer,
                                    CKDWORD StartIndex, CKDWORD IndexCount)
{
    if (!CanSubmit())
        return;
    if (!m_Context) {
        SetError(CKERR_INVALIDOPERATION);
        return;
    }
    CKBgfxIndexBufferRecord *rec = m_Context->GetIndexBuffer(Buffer);
    if (!rec || IndexCount == 0 || StartIndex > rec->IndexCount ||
        IndexCount > rec->IndexCount - StartIndex) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (m_Encoder)
        m_Encoder->setIndexBuffer(rec->Handle, StartIndex, IndexCount);
    else
        bgfx::setIndexBuffer(rec->Handle, StartIndex, IndexCount);
    if (m_Context->m_DrawMapSubmitActive) {
        m_DebugIndexBuffer = Buffer;
        m_DebugIndexStart = StartIndex;
        m_DebugIndexCount = IndexCount;
        m_DebugIndexHandle = rec->Handle.idx;
    }
}

void CKBgfxEncoder::SetInstanceBuffer(CKDWORD Stream, CKDWORD Buffer,
                                       CKDWORD StartInstance, CKDWORD InstanceCount)
{
    if (!CanSubmit())
        return;
    if ((m_Context->m_CapsDesc.Features & CKRST_CAPS_INSTANCING) == 0) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    if (!m_Context) {
        SetError(CKERR_INVALIDOPERATION);
        return;
    }
    CKBgfxVertexBufferRecord *rec = m_Context->GetVertexBuffer(Buffer);
    if (!rec || Stream != 0 ||
        InstanceCount == 0 || StartInstance > rec->VertexCount ||
        InstanceCount > rec->VertexCount - StartInstance) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (m_Encoder)
        m_Encoder->setInstanceDataBuffer(rec->Handle, StartInstance, InstanceCount);
    else
        bgfx::setInstanceDataBuffer(rec->Handle, StartInstance, InstanceCount);
}

void CKBgfxEncoder::SetTransientVertexBuffer(CKDWORD Stream,
                                              CKTransientVertexBuffer *Buffer)
{
    if (!CanSubmit())
        return;
    if (!Buffer || !Buffer->Data || !m_Context ||
        Stream >= m_Context->m_CapsDesc.MaxVertexStreams || Buffer->VertexCount == 0) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    VxMutexLock poolLock(m_Context->m_TransientPoolMutex);
    bgfx::TransientVertexBuffer *tvb = NULL;
    CKDWORD vbCount = m_Context->m_TransientVBCount.load(std::memory_order_acquire);
    for (CKDWORD i = 0; i < vbCount; ++i)
    {
        if (m_Context->m_TransientVBPool[i].data == (uint8_t *)Buffer->Data)
        {
            tvb = &m_Context->m_TransientVBPool[i];
            break;
        }
    }
    if (!tvb) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    CKBgfxVertexLayoutRecord *layout = m_Context->GetVertexLayout(Buffer->Layout);
    if (!layout || Buffer->Size != tvb->size ||
        Buffer->StartVertex != tvb->startVertex ||
        Buffer->Stride != tvb->stride ||
        layout->Handle.idx != tvb->layoutHandle.idx ||
        Buffer->VertexCount > tvb->size / tvb->stride) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (m_Encoder)
        m_Encoder->setVertexBuffer((uint8_t)Stream, tvb, 0, Buffer->VertexCount);
    else
        bgfx::setVertexBuffer((uint8_t)Stream, tvb, 0, Buffer->VertexCount);
}

void CKBgfxEncoder::SetTransientIndexBuffer(CKTransientIndexBuffer *Buffer)
{
    if (!CanSubmit())
        return;
    if (!Buffer || !Buffer->Data || !m_Context || Buffer->IndexCount == 0) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    VxMutexLock poolLock(m_Context->m_TransientPoolMutex);
    bgfx::TransientIndexBuffer *tib = NULL;
    CKDWORD ibCount = m_Context->m_TransientIBCount.load(std::memory_order_acquire);
    for (CKDWORD i = 0; i < ibCount; ++i)
    {
        if (m_Context->m_TransientIBPool[i].data == (uint8_t *)Buffer->Data)
        {
            tib = &m_Context->m_TransientIBPool[i];
            break;
        }
    }
    if (!tib) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    const CKDWORD indexSize = tib->isIndex16 ? 2u : 4u;
    if (Buffer->Size != tib->size || Buffer->StartIndex != tib->startIndex ||
        (Buffer->Index32 != FALSE) == tib->isIndex16 ||
        Buffer->IndexCount > tib->size / indexSize) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (m_Encoder)
        m_Encoder->setIndexBuffer(tib, 0, Buffer->IndexCount);
    else
        bgfx::setIndexBuffer(tib, 0, Buffer->IndexCount);
}

void CKBgfxEncoder::SetTransientInstanceBuffer(CKDWORD Stream,
                                                CKTransientInstanceBuffer *Buffer)
{
    if (!CanSubmit())
        return;
    if (!Buffer || !Buffer->Data || !m_Context ||
        Stream != 0 || Buffer->InstanceCount == 0) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    VxMutexLock poolLock(m_Context->m_TransientPoolMutex);
    bgfx::InstanceDataBuffer *idb = NULL;
    CKDWORD instCount = m_Context->m_TransientInstCount.load(std::memory_order_acquire);
    for (CKDWORD i = 0; i < instCount; ++i)
    {
        if (m_Context->m_TransientInstPool[i].data == (uint8_t *)Buffer->Data)
        {
            idb = &m_Context->m_TransientInstPool[i];
            break;
        }
    }
    if (!idb) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    CKBgfxVertexLayoutRecord *layout = m_Context->GetVertexLayout(Buffer->Layout);
    const CKDWORD layoutStride = layout
        ? (layout->Layout.m_stride + 15u) & ~15u
        : 0;
    if (!layout || Buffer->Size != idb->size || Buffer->Stride != idb->stride ||
        layoutStride != idb->stride ||
        Buffer->StartInstance > idb->num ||
        Buffer->InstanceCount > idb->num - Buffer->StartInstance) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (m_Encoder)
        m_Encoder->setInstanceDataBuffer(idb, Buffer->StartInstance, Buffer->InstanceCount);
    else
        bgfx::setInstanceDataBuffer(idb, Buffer->StartInstance, Buffer->InstanceCount);
}

void CKBgfxEncoder::SetTexture(CKDWORD Stage, CKDWORD Uniform,
                                CKDWORD Texture, CKSamplerDesc *Sampler)
{
    static int s_SetTextureLogCount = 0;
    if (!CanSubmit())
        return;
    if (!m_Context) {
        SetError(CKERR_INVALIDOPERATION);
        return;
    }
    CKBgfxUniformRecord *uniRec = m_Context->GetUniform(Uniform);
    CKBgfxTextureRecord *texRec = m_Context->GetTexture(Texture);
    if (Stage >= m_Context->m_CapsDesc.MaxTextureStages || !uniRec ||
        uniRec->Type != CKRST_UNIFORM_SAMPLER || (Texture != 0 && !texRec)) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (texRec && (texRec->Flags & CKRST_TEXTURE_READBACK) != 0) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    uint32_t flags = BGFX_SAMPLER_NONE;
    if (!CKBgfxTrySamplerFlags(Sampler, flags)) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (Sampler) {
        if (Sampler->CompareFunc != CKRST_COMPARE_NONE) {
            if (!texRec ||
                (m_Context->m_CapsDesc.Features & CKRST_CAPS_TEXTURE_COMPARISON) == 0 ||
                (CKBgfxMapFormatCaps(m_Context->m_NativeFormatCaps[texRec->Format],
                                     FALSE, TRUE) &
                 CKRST_FORMAT_CAPS_TEXTURE_COMPARE) == 0) {
                SetError(CKERR_NOTIMPLEMENTED);
                return;
            }
        }
    }
    bgfx::TextureHandle textureHandle = texRec ? texRec->Handle : m_Context->m_DefaultWhiteTexture;
    bool usingSamplerBase = false;
    if (texRec && !CKBgfxSamplerWantsMipMaps(Sampler) && texRec->MipCount > 1) {
        if (!texRec->SamplerBaseValid ||
            !bgfx::isValid(texRec->SamplerBaseHandle)) {
            SetError(CKERR_NOTIMPLEMENTED);
            return;
        }
        textureHandle = texRec->SamplerBaseHandle;
        usingSamplerBase = true;
    }
    if (m_Context->m_DebugLogTextureBindings &&
        s_SetTextureLogCount < 80) {
        CKBgfxLogf("SetTexture",
                 "stage=%u uniform=%u texture=%u uni=%p tex=%p texIdx=%u base=%u size=%ux%u fmt=%d sampler=%p",
                 Stage, Uniform, Texture, (void *)uniRec, (void *)texRec,
                 bgfx::isValid(textureHandle) ? textureHandle.idx : 0xffff,
                 usingSamplerBase ? 1u : 0u,
                 texRec ? texRec->Width : 0,
                 texRec ? texRec->Height : 0,
                 texRec ? (int)texRec->Format : -1,
                 (void *)Sampler);
        s_SetTextureLogCount++;
    }
    if (!bgfx::isValid(textureHandle)) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (m_Encoder)
        m_Encoder->setTexture((uint8_t)Stage, uniRec->Handle, textureHandle, flags);
    else
        bgfx::setTexture((uint8_t)Stage, uniRec->Handle, textureHandle, flags);

    if (m_Context->m_DrawMapSubmitActive && Stage < CKRST_MAX_TEXTURE_STAGES) {
        m_DebugTextureBindings[Stage].Texture = Texture;
        m_DebugTextureBindings[Stage].Uniform = Uniform;
        m_DebugTextureBindings[Stage].BgfxHandle =
            bgfx::isValid(textureHandle) ? textureHandle.idx : 0xffff;
        m_DebugTextureBindings[Stage].SamplerFlags = flags;
        m_DebugTextureBindingMask |= (1u << Stage);
    }
}

void CKBgfxEncoder::SetUniform(CKDWORD Uniform, const void *Data, CKDWORD Count)
{
    if (!CanSubmit())
        return;
    if (!m_Context) {
        SetError(CKERR_INVALIDOPERATION);
        return;
    }
    CKBgfxUniformRecord *rec = m_Context->GetUniform(Uniform);
    if (!rec || rec->Type == CKRST_UNIFORM_SAMPLER || !Data || Count == 0 ||
        Count > rec->Count) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    static int s_uniformLogCount = 0;
    if (m_Context->m_DebugLogUniforms && s_uniformLogCount < 256) {
        const float *f = static_cast<const float *>(Data);
        if (f && rec->Type == CKRST_UNIFORM_VEC4) {
            CKBgfxLogf("SetUniform",
                     "uniform=%u name=%s handle=%u type=%u count=%u recCount=%u first=(%.3f %.3f %.3f %.3f)",
                     Uniform, rec->Name, rec->Handle.idx, (unsigned)rec->Type, Count, rec->Count,
                     f[0], f[1], f[2], f[3]);
        } else {
            CKBgfxLogf("SetUniform",
                     "uniform=%u name=%s handle=%u type=%u count=%u recCount=%u data=%p",
                     Uniform, rec->Name, rec->Handle.idx, (unsigned)rec->Type, Count, rec->Count, Data);
        }
        ++s_uniformLogCount;
    }
    if (m_Encoder)
        m_Encoder->setUniform(rec->Handle, Data, (uint16_t)Count);
    else
        bgfx::setUniform(rec->Handle, Data, (uint16_t)Count);
}

void CKBgfxEncoder::Submit(CKRenderView View, CKDWORD Program,
                            CKDWORD Depth, CKDWORD Flags)
{
    if (!CanSubmit()) {
        m_DebugSpecializationValid = FALSE;
        return;
    }
    uint8_t discard = BGFX_DISCARD_NONE;
    if (View >= CKRST_MAX_RENDER_VIEWS ||
        !CKBgfxTryDiscardFlags(Flags, discard)) {
        SetError(CKERR_INVALIDPARAMETER);
        m_DebugSpecializationValid = FALSE;
        return;
    }
    CKBgfxProgramRecord *rec = m_Context->GetProgram(Program);
    if (!rec || !bgfx::isValid(rec->Handle) || rec->PixelShader == 0) {
        SetError(CKERR_INVALIDPARAMETER);
        m_Context->RecordInvalidSubmit((CKSTRING)"Submit", View, Program,
                                       (CKSTRING)"invalid_program");
        if (m_Context->m_DrawMapMarkerCaptureActive)
            m_LastMarker[0] = '\0';
        m_DebugSpecializationValid = FALSE;
        return;
    }
    if (m_Context->m_DrawMapSubmitActive)
        TraceSubmit((CKSTRING)"Submit", View, Program, rec->Handle, Depth, Flags, 0, 0, 0);
    m_Context->RecordViewColorWrite(View, TRUE);
    if (m_Encoder)
        m_Encoder->submit((bgfx::ViewId)View, rec->Handle, Depth, discard);
    else
        bgfx::submit((bgfx::ViewId)View, rec->Handle, Depth, discard);
    if (m_Context->m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    m_DebugSpecializationValid = FALSE;
}

void CKBgfxEncoder::TraceSubmit(CKSTRING Kind,
                                CKRenderView View,
                                CKDWORD Program,
                                bgfx::ProgramHandle ProgramHandle,
                                CKDWORD Depth,
                                CKDWORD Flags,
                                CKDWORD Extra0,
                                CKDWORD Extra1,
                                CKDWORD Extra2)
{
    if (!m_Context)
        return;
    if (!m_Context->m_DrawMapSubmitActive)
        return;

    const CKDWORD submitSerial = m_Context->m_DebugSubmitSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    CKDWORD viewSubmitSerial = 0;
    CKDrawAnnotationParsed parsed;
    CKBOOL parsedLabel = FALSE;
    CKDWORD sourceIndex = CKDRAW_SOURCE_NONE;
    CKBgfxProgramRecord *programRecord = m_Context->GetProgram(Program);
    CKDWORD stateHash = CKBgfxHashDrawState(m_CachedDrawState);
    CKDWORD stencilHash = CKBgfxHashStencil(m_StencilRef, m_StencilReadMask, m_StencilWriteMask);
    CKDWORD programHash = CKBgfxHashProgram(programRecord);
    uint64_t finalState = m_CachedBgfxState;
    char texFields[512];
    CKDWORD texOffset = 0;
    char vbFields[256];
    CKDWORD vbOffset = 0;
    CK_SHADER_PROFILE shaderProfile = CKRST_SHADER_PROFILE_UNKNOWN;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);
    CKRasterizerTargetDesc target;
    if (m_Context->GetTargetDesc(&target) == CK_OK)
        shaderProfile = target.ShaderProfile;

    texFields[0] = '\0';
    for (CKDWORD i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i) {
        if ((m_DebugTextureBindingMask & (1u << i)) == 0)
            continue;
        if (!CKBgfxDrawMapAppendTextureBinding(texFields, sizeof(texFields),
                                               &texOffset,
                                               i,
                                               m_DebugTextureBindings[i].Texture,
                                               m_DebugTextureBindings[i].Uniform,
                                               m_DebugTextureBindings[i].BgfxHandle,
                                               m_DebugTextureBindings[i].SamplerFlags))
            break;
    }
    texFields[sizeof(texFields) - 1] = '\0';

    vbFields[0] = '\0';
    for (CKDWORD i = 0; i < CKRST_MAX_VERTEX_STREAMS; ++i) {
        if ((m_DebugVertexBindingMask & (1u << i)) == 0)
            continue;
        if (!CKBgfxDrawMapAppendVertexBinding(vbFields, sizeof(vbFields),
                                              &vbOffset,
                                              i,
                                              m_DebugVertexBindings[i].Buffer,
                                              m_DebugVertexBindings[i].Start,
                                              m_DebugVertexBindings[i].Count,
                                              m_DebugVertexBindings[i].BgfxHandle,
                                              m_DebugVertexBindings[i].LayoutHandle))
            break;
    }
    vbFields[sizeof(vbFields) - 1] = '\0';

    if (View < CKRST_MAX_RENDER_VIEWS)
        viewSubmitSerial = m_Context->m_DebugViewSubmitSerial[View].fetch_add(1, std::memory_order_relaxed) + 1;

    if (m_LastMarker[0] == '\0') {
        m_Context->m_DebugMissingAnnotationCount.fetch_add(1, std::memory_order_relaxed);
        CKBgfxDrawMapTraceSubmitMiss((CKSTRING)"no_annotation",
                                      m_Context->m_DebugFrameId,
                                      submitSerial,
                                      (unsigned)View,
                                      Program,
                                      Kind,
                                      NULL);
    } else {
        parsedLabel = CKDrawAnnotationParseLabel(m_LastMarker, &parsed);
        if (parsedLabel) {
            m_Context->m_DebugParsedAnnotationCount.fetch_add(1, std::memory_order_relaxed);
            sourceIndex = (CKDWORD)parsed.Source;
            if (sourceIndex < CKDRAW_SOURCE_COUNT)
                m_Context->m_DebugSourceSubmitCount[sourceIndex].fetch_add(1, std::memory_order_relaxed);
            if (parsed.Source == CKDRAW_SOURCE_RAW_PRIMITIVE)
                m_Context->m_DebugRawPrimitiveCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_Context->m_DebugMissingAnnotationCount.fetch_add(1, std::memory_order_relaxed);
            CKBgfxDrawMapTraceSubmitMiss((CKSTRING)"malformed_annotation",
                                          m_Context->m_DebugFrameId,
                                          submitSerial,
                                          (unsigned)View,
                                          Program,
                                          Kind,
                                          m_LastMarker);
        }
    }

    {
        CKBgfxDrawMapStateTrace stateTrace;
        stateTrace.Frame = m_Context->m_DebugFrameId;
        stateTrace.Submit = submitSerial;
        stateTrace.StateHash = stateHash;
        stateTrace.Low = m_CachedDrawState.Lo;
        stateTrace.Mid = m_CachedDrawState.Mid;
        stateTrace.High = m_CachedDrawState.Hi;
        stateTrace.BgfxStateLo = (CKDWORD)(finalState & 0xffffffffu);
        stateTrace.BgfxStateHi = (CKDWORD)(finalState >> 32);
        stateTrace.StencilHash = stencilHash;
        stateTrace.StencilRef = m_StencilRef;
        stateTrace.StencilReadMask = m_StencilReadMask;
        stateTrace.StencilWriteMask = m_StencilWriteMask;
        stateTrace.PointSize = m_PointSize;
        CKBgfxDrawMapTraceState(&stateTrace);
    }

    {
        CKBgfxDrawMapSubmitTrace submitTrace;
        submitTrace.Frame = m_Context->m_DebugFrameId;
        submitTrace.Submit = submitSerial;
        submitTrace.View = (unsigned)View;
        submitTrace.ViewSubmit = viewSubmitSerial;
        submitTrace.Kind = Kind;
        submitTrace.ViewMode = View < CKRST_MAX_RENDER_VIEWS ? CKBgfxViewModeName(m_Context->m_DebugViewMode[View]) : "Invalid";
        submitTrace.OrderGeneration = m_Context->m_DebugViewOrderGeneration;
        submitTrace.OrderSequential = m_Context->m_DebugViewOrderSequential ? 1u : 0u;
        submitTrace.Depth = Depth;
        submitTrace.Program = Program;
        submitTrace.BgfxProgram = bgfx::isValid(ProgramHandle) ? ProgramHandle.idx : 0xffff;
        submitTrace.ProgramHash = programHash;
        submitTrace.SpecHash = 0;
        submitTrace.ShaderProfile = (CKSTRING)CKBgfxShaderProfileName(shaderProfile);
        submitTrace.StateHash = stateHash;
        submitTrace.BgfxStateLo = (CKDWORD)(finalState & 0xffffffffu);
        submitTrace.BgfxStateHi = (CKDWORD)(finalState >> 32);
        submitTrace.StencilHash = stencilHash;
        submitTrace.Layout = m_CurrentLayout;
        submitTrace.VertexBufferMask = m_DebugVertexBindingMask;
        submitTrace.IndexBuffer = m_DebugIndexBuffer;
        submitTrace.IndexStart = m_DebugIndexStart;
        submitTrace.IndexCount = m_DebugIndexCount;
        submitTrace.IndexBgfxHandle = m_DebugIndexHandle;
        submitTrace.TextureMask = m_DebugTextureBindingMask;
        submitTrace.Discard = (unsigned)ToBgfxDiscardFlags(Flags);
        submitTrace.Extra0 = Extra0;
        submitTrace.Extra1 = Extra1;
        submitTrace.Extra2 = Extra2;
        submitTrace.Parse = parsedLabel ? 1u : 0u;
        submitTrace.Token = parsedLabel ? parsed.Token : 0u;
        submitTrace.Source = parsedLabel ? parsed.SourceName : (CKSTRING)"";
        submitTrace.ObjectId = parsedLabel ? parsed.Object.Id : 0u;
        submitTrace.ObjectName = parsedLabel ? parsed.Object.Name : (CKSTRING)"";
        submitTrace.EntityId = parsedLabel ? parsed.Entity.Id : 0u;
        submitTrace.EntityName = parsedLabel ? parsed.Entity.Name : (CKSTRING)"";
        submitTrace.MeshId = parsedLabel ? parsed.Mesh.Id : 0u;
        submitTrace.MeshName = parsedLabel ? parsed.Mesh.Name : (CKSTRING)"";
        submitTrace.MaterialId = parsedLabel ? parsed.Material.Id : 0u;
        submitTrace.MaterialName = parsedLabel ? parsed.Material.Name : (CKSTRING)"";
        submitTrace.Path = parsedLabel ? parsed.Path : (CKSTRING)"";
        submitTrace.GroupIndex = parsedLabel ? parsed.GroupIndex : -1;
        submitTrace.PrimitiveIndex = parsedLabel ? parsed.PrimitiveIndex : -1;
        submitTrace.PrimitiveType = parsedLabel ? (int)parsed.PrimitiveType : 0;
        submitTrace.IndexTotal = parsedLabel ? parsed.IndexCount : 0u;
        submitTrace.VertexTotal = parsedLabel ? parsed.VertexCount : 0u;
        submitTrace.VertexFields = vbFields;
        submitTrace.TextureFields = texFields;
        submitTrace.Label = m_LastMarker;
        CKBgfxDrawMapTraceSubmit(&submitTrace);
    }
}

void CKBgfxEncoder::Touch(CKRenderView View)
{
    if (!CanSubmit() || View >= m_Context->m_CapsDesc.MaxRenderViews) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    m_Context->RecordViewColorWrite(View, FALSE);
    if (m_Encoder)
        m_Encoder->touch((bgfx::ViewId)View);
    else
        bgfx::touch((bgfx::ViewId)View);
}

void CKBgfxEncoder::Blit(CKRenderView View,
                          CKDWORD DstTexture, CKDWORD DstMip,
                          CKDWORD DstX, CKDWORD DstY,
                          CKDWORD SrcTexture, CKDWORD SrcMip,
                          const CKRECT *SrcRect)
{
    if (!CanSubmit())
        return;
    CKBgfxTextureRecord *dst = m_Context->GetTexture(DstTexture);
    CKBgfxTextureRecord *src = m_Context->GetTexture(SrcTexture);
    if (!dst || !src || View >= m_Context->m_CapsDesc.MaxRenderViews)
    {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (DstMip >= dst->MipCount || SrcMip >= src->MipCount)
    {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    const CKDWORD dstWidth = XMax((CKDWORD)1, dst->Width >> DstMip);
    const CKDWORD dstHeight = XMax((CKDWORD)1, dst->Height >> DstMip);
    const CKDWORD srcWidth = XMax((CKDWORD)1, src->Width >> SrcMip);
    const CKDWORD srcHeight = XMax((CKDWORD)1, src->Height >> SrcMip);
    if (DstX >= dstWidth || DstY >= dstHeight ||
        (SrcRect && (SrcRect->left < 0 || SrcRect->top < 0 ||
                     SrcRect->right <= SrcRect->left ||
                     SrcRect->bottom <= SrcRect->top ||
                     (CKDWORD)SrcRect->right > srcWidth ||
                     (CKDWORD)SrcRect->bottom > srcHeight))) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if ((m_Context->m_CapsDesc.Features & CKRST_CAPS_BLIT) == 0 ||
        (dst->Flags & CKRST_TEXTURE_BLIT_DST) == 0 ||
        dst->Format != src->Format) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    const int srcRectWidth = SrcRect ? SrcRect->right - SrcRect->left : 0;
    const int srcRectHeight = SrcRect ? SrcRect->bottom - SrcRect->top : 0;
    const CKDWORD copiedWidth = SrcRect
        ? (srcRectWidth > 0 ? (CKDWORD)srcRectWidth : 0)
        : XMax((CKDWORD)1, src->Width >> SrcMip);
    const CKDWORD copiedHeight = SrcRect
        ? (srcRectHeight > 0 ? (CKDWORD)srcRectHeight : 0)
        : XMax((CKDWORD)1, src->Height >> SrcMip);
    const CKDWORD actualCopiedWidth =
        XMin(copiedWidth, dstWidth - DstX);
    const CKDWORD actualCopiedHeight =
        XMin(copiedHeight, dstHeight - DstY);
    const CKBOOL fullDestination =
        DstX == 0 && DstY == 0 &&
        actualCopiedWidth == dstWidth && actualCopiedHeight == dstHeight;
    m_Context->RecordViewColorWrite(View, FALSE);
    m_Context->RecordTextureBlit(dst, DstMip, src, SrcMip, fullDestination);
    if (SrcRect)
    {
        if (m_Encoder)
            m_Encoder->blit((bgfx::ViewId)View,
                            dst->Handle, (uint8_t)DstMip,
                            (uint16_t)DstX, (uint16_t)DstY, 0,
                            src->Handle, (uint8_t)SrcMip,
                            (uint16_t)SrcRect->left, (uint16_t)SrcRect->top, 0,
                            (uint16_t)(SrcRect->right - SrcRect->left),
                            (uint16_t)(SrcRect->bottom - SrcRect->top), 1);
        else
            bgfx::blit((bgfx::ViewId)View,
                        dst->Handle, (uint8_t)DstMip,
                        (uint16_t)DstX, (uint16_t)DstY, 0,
                        src->Handle, (uint8_t)SrcMip,
                        (uint16_t)SrcRect->left, (uint16_t)SrcRect->top, 0,
                        (uint16_t)(SrcRect->right - SrcRect->left),
                        (uint16_t)(SrcRect->bottom - SrcRect->top), 1);
    }
    else
    {
        if (m_Encoder)
            m_Encoder->blit((bgfx::ViewId)View,
                            dst->Handle, (uint8_t)DstMip,
                            (uint16_t)DstX, (uint16_t)DstY, 0,
                            src->Handle, (uint8_t)SrcMip);
        else
            bgfx::blit((bgfx::ViewId)View,
                        dst->Handle, (uint8_t)DstMip,
                        (uint16_t)DstX, (uint16_t)DstY, 0,
                        src->Handle, (uint8_t)SrcMip);
    }
}

void CKBgfxEncoder::SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                                     CK_ACCESS_MODE Access)
{
    if (!CanSubmit())
        return;
    if (!m_Context || Stage >= m_Context->m_CapsDesc.MaxComputeBindings)
    {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if ((m_Context->m_CapsDesc.Features & CKRST_CAPS_COMPUTE) == 0) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    CKBgfxVertexBufferRecord *vb = m_Context->GetVertexBuffer(Buffer);
    if (!vb || (Access != CKRST_ACCESS_READ && Access != CKRST_ACCESS_WRITE &&
                Access != CKRST_ACCESS_READWRITE))
    {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    const CKBOOL allowRead = (vb->Flags & CKRST_VB_COMPUTE_READ) != 0;
    const CKBOOL allowWrite = (vb->Flags & CKRST_VB_COMPUTE_WRITE) != 0;
    if ((Access == CKRST_ACCESS_READ && !allowRead) ||
        (Access == CKRST_ACCESS_WRITE && !allowWrite) ||
        (Access == CKRST_ACCESS_READWRITE && (!allowRead || !allowWrite))) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    bgfx::Access::Enum bgfxAccess = bgfx::Access::Read;
    if (Access == CKRST_ACCESS_WRITE) bgfxAccess = bgfx::Access::Write;
    else if (Access == CKRST_ACCESS_READWRITE) bgfxAccess = bgfx::Access::ReadWrite;
    if (m_Encoder)
        m_Encoder->setBuffer((uint8_t)Stage, vb->Handle, bgfxAccess);
    else
        bgfx::setBuffer((uint8_t)Stage, vb->Handle, bgfxAccess);
}

void CKBgfxEncoder::SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                                     CKDWORD Mip, CK_ACCESS_MODE Access)
{
    if (!CanSubmit())
        return;
    if (!m_Context || Stage >= m_Context->m_CapsDesc.MaxComputeBindings)
    {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    CKBgfxTextureRecord *tex = m_Context->GetTexture(Texture);
    if (!tex || Mip >= tex->MipCount ||
        (Access != CKRST_ACCESS_READ && Access != CKRST_ACCESS_WRITE &&
         Access != CKRST_ACCESS_READWRITE))
    {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if ((tex->Flags & CKRST_TEXTURE_READBACK) != 0) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    if ((m_Context->m_CapsDesc.Features & CKRST_CAPS_COMPUTE) == 0 ||
        (m_Context->m_CapsDesc.Features & CKRST_CAPS_IMAGE_RW) == 0) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    const CKDWORD formatCaps = CKBgfxMapFormatCaps(
        m_Context->m_NativeFormatCaps[tex->Format], FALSE, FALSE);
    if ((Access == CKRST_ACCESS_READ &&
         (formatCaps & CKRST_FORMAT_CAPS_IMAGE_READ) == 0) ||
        ((Access == CKRST_ACCESS_WRITE || Access == CKRST_ACCESS_READWRITE) &&
         ((tex->Flags & CKRST_TEXTURE_COMPUTE_WRITE) == 0 ||
          (formatCaps & CKRST_FORMAT_CAPS_IMAGE_WRITE) == 0)) ||
        (Access == CKRST_ACCESS_READWRITE &&
         (formatCaps & CKRST_FORMAT_CAPS_IMAGE_READ) == 0)) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    bgfx::Access::Enum bgfxAccess = bgfx::Access::Read;
    if (Access == CKRST_ACCESS_WRITE) bgfxAccess = bgfx::Access::Write;
    else if (Access == CKRST_ACCESS_READWRITE) bgfxAccess = bgfx::Access::ReadWrite;
    if (Access == CKRST_ACCESS_WRITE || Access == CKRST_ACCESS_READWRITE)
        m_Context->RecordTextureWrite(tex, Mip, CKBGFX_ORIENTATION_UNKNOWN, TRUE);
    if (m_Encoder)
        m_Encoder->setImage((uint8_t)Stage, tex->Handle, (uint8_t)Mip, bgfxAccess);
    else
        bgfx::setImage((uint8_t)Stage, tex->Handle, (uint8_t)Mip, bgfxAccess);
}

void CKBgfxEncoder::SetCondition(CKDWORD Query, CKBOOL Visible)
{
    if (!CanSubmit())
        return;
    if (!m_Context)
    {
        SetError(CKERR_INVALIDOPERATION);
        return;
    }
    CKBgfxOcclusionQueryRecord *oq = m_Context->GetOcclusionQuery(Query);
    if (!oq)
    {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    if (m_Encoder)
        m_Encoder->setCondition(oq->Handle, Visible != FALSE);
    else
        bgfx::setCondition(oq->Handle, Visible != FALSE);
}

void CKBgfxEncoder::SetMarker(CKSTRING Name)
{
    if (!CanSubmit())
        return;
    if (!m_Context)
        return;
    if (Name) {
        if (m_Context->m_DrawMapMarkerCaptureActive) {
            if (m_LastMarker[0] != '\0') {
                m_Context->m_DebugMarkerOverwriteCount.fetch_add(1, std::memory_order_relaxed);
                if (CKBgfxDrawMapChannelEnabled(m_Context->m_DrawMapFlags,
                                                CKRST_DEBUG_DRAWMAP_MARKERS))
                    CKBgfxLogf("MarkerOverwrite",
                               "frame=%u old=\"%s\" new=\"%s\"",
                               m_Context->m_DebugFrameId, m_LastMarker, Name);
            }
            strncpy(m_LastMarker, Name, sizeof(m_LastMarker) - 1);
            m_LastMarker[sizeof(m_LastMarker) - 1] = '\0';
        }
        if (m_Encoder)
            m_Encoder->setMarker(Name);
        else
            bgfx::setMarker(Name);
        if (CKBgfxDrawMapChannelEnabled(m_Context->m_DrawMapFlags,
                                        CKRST_DEBUG_DRAWMAP_MARKERS))
            CKBgfxLogf("Marker", "%s", Name);
    } else {
        m_LastMarker[0] = '\0';
    }
}

CKBOOL CKBgfxEncoder::ConsumeMarker(char *Buffer, CKDWORD BufferSize)
{
    if (!CanSubmit())
        return FALSE;
    if (!m_Context || !m_Context->m_DrawMapMarkerCaptureActive)
        return FALSE;
    if (!Buffer || BufferSize == 0)
        return FALSE;
    Buffer[0] = '\0';
    if (m_LastMarker[0] == '\0')
        return FALSE;
    strncpy(Buffer, m_LastMarker, BufferSize - 1);
    Buffer[BufferSize - 1] = '\0';
    m_LastMarker[0] = '\0';
    return TRUE;
}

void CKBgfxEncoder::SubmitOcclusionQuery(CKRenderView View, CKDWORD Program,
                                          CKDWORD Query, CKDWORD Depth,
                                          CKDWORD Flags)
{
    if (!CanSubmit()) {
        m_DebugSpecializationValid = FALSE;
        return;
    }
    uint8_t discard = BGFX_DISCARD_NONE;
    if (View >= m_Context->m_CapsDesc.MaxRenderViews ||
        !CKBgfxTryDiscardFlags(Flags, discard)) {
        SetError(CKERR_INVALIDPARAMETER);
        m_DebugSpecializationValid = FALSE;
        return;
    }
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    CKBgfxOcclusionQueryRecord *oq = m_Context->GetOcclusionQuery(Query);
    if (!prog || !bgfx::isValid(prog->Handle) || prog->PixelShader == 0 || !oq) {
        SetError(CKERR_INVALIDPARAMETER);
        m_Context->RecordInvalidSubmit((CKSTRING)"Occlusion", View, Program,
                                       !prog || !bgfx::isValid(prog->Handle)
                                           ? (CKSTRING)"invalid_program"
                                           : (CKSTRING)"invalid_occlusion_query");
        if (m_Context->m_DrawMapMarkerCaptureActive)
            m_LastMarker[0] = '\0';
        m_DebugSpecializationValid = FALSE;
        return;
    }
    if (m_Context->m_DrawMapSubmitActive)
        TraceSubmit((CKSTRING)"Occlusion", View, Program, prog->Handle, Depth, Flags, Query, 0, 0);
    m_Context->RecordViewColorWrite(View, TRUE);
    if (m_Encoder)
        m_Encoder->submit((bgfx::ViewId)View, prog->Handle, oq->Handle, Depth, discard);
    else
        bgfx::submit((bgfx::ViewId)View, prog->Handle, oq->Handle, Depth, discard);
    if (m_Context->m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    m_DebugSpecializationValid = FALSE;
}

void CKBgfxEncoder::SubmitIndirect(CKRenderView View, CKDWORD Program,
                                    CKDWORD IndirectBuffer,
                                    CKDWORD Start, CKDWORD Count,
                                    CKDWORD Depth, CKDWORD Flags)
{
    if (!CanSubmit()) {
        m_DebugSpecializationValid = FALSE;
        return;
    }
    uint8_t discard = BGFX_DISCARD_NONE;
    if (View >= m_Context->m_CapsDesc.MaxRenderViews || Count == 0 ||
        !CKBgfxTryDiscardFlags(Flags, discard)) {
        SetError(CKERR_INVALIDPARAMETER);
        m_DebugSpecializationValid = FALSE;
        return;
    }
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    CKBgfxIndirectBufferRecord *ib = m_Context->GetIndirectBuffer(IndirectBuffer);
    if (!prog || !bgfx::isValid(prog->Handle) || prog->PixelShader == 0 || !ib ||
        Start > ib->MaxCommands || Count > ib->MaxCommands - Start) {
        SetError(CKERR_INVALIDPARAMETER);
        m_Context->RecordInvalidSubmit((CKSTRING)"Indirect", View, Program,
                                       !prog || !bgfx::isValid(prog->Handle)
                                           ? (CKSTRING)"invalid_program"
                                           : (CKSTRING)"invalid_indirect_buffer");
        if (m_Context->m_DrawMapMarkerCaptureActive)
            m_LastMarker[0] = '\0';
        m_DebugSpecializationValid = FALSE;
        return;
    }
    if (m_Context->m_DrawMapSubmitActive)
        TraceSubmit((CKSTRING)"Indirect", View, Program, prog->Handle, Depth, Flags, IndirectBuffer, Start, Count);
    m_Context->RecordViewColorWrite(View, TRUE);
    if (m_Encoder)
        m_Encoder->submit((bgfx::ViewId)View, prog->Handle, ib->Handle,
                          Start, Count, Depth, discard);
    else
        bgfx::submit((bgfx::ViewId)View, prog->Handle, ib->Handle,
                     Start, Count, Depth, discard);
    if (m_Context->m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    m_DebugSpecializationValid = FALSE;
}

void CKBgfxEncoder::Dispatch(CKRenderView View, CKDWORD Program,
                              CKDWORD NumX, CKDWORD NumY, CKDWORD NumZ,
                              CKDWORD Flags)
{
    m_DebugSpecializationValid = FALSE;
    if (!CanSubmit())
        return;
    if ((m_Context->m_CapsDesc.Features & CKRST_CAPS_COMPUTE) == 0) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    uint8_t discard = BGFX_DISCARD_NONE;
    if (View >= m_Context->m_CapsDesc.MaxRenderViews ||
        !CKBgfxTryDiscardFlags(Flags, discard)) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    if (!prog || !bgfx::isValid(prog->Handle) || prog->PixelShader != 0) {
        SetError(CKERR_INVALIDPARAMETER);
        m_Context->RecordInvalidSubmit((CKSTRING)"Dispatch", View, Program,
                                       (CKSTRING)"invalid_program");
        if (m_Context->m_DrawMapMarkerCaptureActive)
            m_LastMarker[0] = '\0';
        return;
    }
    m_Context->RecordViewColorWrite(View, FALSE);
    if (m_Encoder)
        m_Encoder->dispatch((bgfx::ViewId)View, prog->Handle, NumX, NumY, NumZ, discard);
    else
        bgfx::dispatch((bgfx::ViewId)View, prog->Handle, NumX, NumY, NumZ, discard);
}

void CKBgfxEncoder::DispatchIndirect(CKRenderView View, CKDWORD Program,
                                      CKDWORD IndirectBuffer,
                                      CKDWORD Start, CKDWORD Count,
                                      CKDWORD Flags)
{
    if (!CanSubmit())
        return;
    if ((m_Context->m_CapsDesc.Features & CKRST_CAPS_COMPUTE) == 0 ||
        (m_Context->m_CapsDesc.Features & CKRST_CAPS_DRAW_INDIRECT) == 0) {
        SetError(CKERR_NOTIMPLEMENTED);
        return;
    }
    uint8_t discard = BGFX_DISCARD_NONE;
    if (View >= m_Context->m_CapsDesc.MaxRenderViews || Count == 0 ||
        !CKBgfxTryDiscardFlags(Flags, discard)) {
        SetError(CKERR_INVALIDPARAMETER);
        return;
    }
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    CKBgfxIndirectBufferRecord *ib = m_Context->GetIndirectBuffer(IndirectBuffer);
    if (!prog || !bgfx::isValid(prog->Handle) || prog->PixelShader != 0 || !ib ||
        Start > ib->MaxCommands || Count > ib->MaxCommands - Start) {
        SetError(CKERR_INVALIDPARAMETER);
        m_Context->RecordInvalidSubmit((CKSTRING)"DispatchIndirect", View, Program,
                                       !prog || !bgfx::isValid(prog->Handle)
                                           ? (CKSTRING)"invalid_program"
                                           : (CKSTRING)"invalid_indirect_buffer");
        if (m_Context->m_DrawMapMarkerCaptureActive)
            m_LastMarker[0] = '\0';
        return;
    }
    m_Context->RecordViewColorWrite(View, FALSE);
    if (m_Encoder)
        m_Encoder->dispatch((bgfx::ViewId)View, prog->Handle, ib->Handle,
                            Start, Count, discard);
    else
        bgfx::dispatch((bgfx::ViewId)View, prog->Handle, ib->Handle,
                       Start, Count, discard);
}

// ===========================================================================
// CKBgfxRasterizerContext
// ===========================================================================

CKBgfxRasterizerContext::CKBgfxRasterizerContext(CKBgfxRasterizerDriver *driver)
    : m_BgfxInitialized(FALSE), m_RendererName("Unknown"),
      m_RendererType(bgfx::RendererType::Count),
      m_NativeSupported(0),
      m_DefaultWhiteTexture(BGFX_INVALID_HANDLE),
      m_VSync(FALSE), m_ResetFlags(BGFX_RESET_NONE), m_AntialiasSamples(0),
      m_NextScreenShotToken(1),
      m_DebugFrameId(0), m_DebugSubmitSerial{0}, m_DebugMissingAnnotationCount{0},
      m_DebugMarkerOverwriteCount{0}, m_DebugMarkerStaleCount{0},
      m_DebugInvalidSubmitCount{0}, m_DebugFatalCount{0},
      m_DebugParsedAnnotationCount{0},
      m_DebugRawPrimitiveCount{0},
      m_DebugEncoderLeakCount{0}, m_DebugTransientAllocMissCount{0},
      m_DebugViewOrderGeneration(0), m_DebugViewOrderSequential(TRUE),
      m_DebugFlags(0), m_DrawMapFlags(0), m_DrawMapActive(FALSE),
      m_DrawMapSubmitActive(FALSE), m_DrawMapMarkerCaptureActive(FALSE),
      m_DebugBgfxFlags(0), m_DebugOverlay(FALSE), m_DebugLogPresentSync(FALSE),
      m_DebugLogTextureBindings(FALSE), m_DebugLogTextures(FALSE), m_DebugLogUniforms(FALSE),
      m_TransformCount{0},
      m_TransientVBCount{0}, m_TransientIBCount{0}, m_TransientInstCount{0}
{
    m_Driver = driver;
    memset(m_NativeFormatCaps, 0, sizeof(m_NativeFormatCaps));
    m_BgfxCallback.SetContext(this);
    for (int i = 0; i < CKRST_MAX_RENDER_VIEWS; ++i) {
        m_DebugViewSubmitSerial[i].store(0, std::memory_order_relaxed);
        m_DebugViewMode[i] = CKRST_VIEWMODE_DEFAULT;
        m_DebugViewName[i][0] = '\0';
        m_ViewFrameBuffer[i] = 0;
        m_ViewRect[i].left = 0;
        m_ViewRect[i].top = 0;
        m_ViewRect[i].right = 0;
        m_ViewRect[i].bottom = 0;
        m_ViewClearFlags[i] = 0;
        m_ViewClearRecorded[i] = FALSE;
    }
    for (int i = 0; i < CKDRAW_SOURCE_COUNT; ++i)
        m_DebugSourceSubmitCount[i].store(0, std::memory_order_relaxed);
}

static CKBgfxTextureOrientation CKBgfxMergeOrientation(
    CKBgfxTextureOrientation Current,
    CKBgfxTextureOrientation Incoming,
    CKBOOL FullOverwrite)
{
    if (FullOverwrite)
        return Incoming;
    if (Incoming == CKBGFX_ORIENTATION_UNKNOWN)
        return CKBGFX_ORIENTATION_UNKNOWN;
    if (Incoming == CKBGFX_ORIENTATION_MIXED ||
        Current == CKBGFX_ORIENTATION_MIXED)
        return CKBGFX_ORIENTATION_MIXED;
    if (Current == CKBGFX_ORIENTATION_UNKNOWN || Current == Incoming)
        return Incoming;
    return CKBGFX_ORIENTATION_MIXED;
}

void CKBgfxRasterizerContext::RecordTextureWrite(
    CKBgfxTextureRecord *Texture, CKDWORD Mip,
    CKBgfxTextureOrientation Orientation, CKBOOL FullOverwrite)
{
    if (!Texture || Mip >= Texture->MipCount || Mip >= CKBGFX_MAX_TRACKED_MIPS)
        return;
    VxMutexLock lock(m_ResourceStateMutex);
    const CKBgfxTextureOrientation current =
        (CKBgfxTextureOrientation)Texture->ReadbackOrientation[Mip];
    Texture->ReadbackOrientation[Mip] = (CKBYTE)CKBgfxMergeOrientation(
        current, Orientation, FullOverwrite);
}

void CKBgfxRasterizerContext::RecordTextureBlit(
    CKBgfxTextureRecord *Destination, CKDWORD DestinationMip,
    const CKBgfxTextureRecord *Source, CKDWORD SourceMip,
    CKBOOL FullOverwrite)
{
    if (!Destination || !Source ||
        DestinationMip >= Destination->MipCount || SourceMip >= Source->MipCount ||
        DestinationMip >= CKBGFX_MAX_TRACKED_MIPS || SourceMip >= CKBGFX_MAX_TRACKED_MIPS)
        return;
    VxMutexLock lock(m_ResourceStateMutex);
    const CKBgfxTextureOrientation source =
        (CKBgfxTextureOrientation)Source->ReadbackOrientation[SourceMip];
    const CKBgfxTextureOrientation current =
        (CKBgfxTextureOrientation)Destination->ReadbackOrientation[DestinationMip];
    Destination->ReadbackOrientation[DestinationMip] =
        (CKBYTE)CKBgfxMergeOrientation(current, source, FullOverwrite);
}

void CKBgfxRasterizerContext::RecordViewColorWrite(CKRenderView View,
                                                    CKBOOL HasDraw)
{
    if (View >= CKRST_MAX_RENDER_VIEWS)
        return;

    VxMutexLock lock(m_ResourceStateMutex);
    CKBgfxFrameBufferRecord *frameBuffer = GetFrameBuffer(m_ViewFrameBuffer[View]);
    if (!frameBuffer)
        return;

    const CKBgfxTextureOrientation targetOrientation =
        m_TargetDesc.OriginBottomLeft
            ? CKBGFX_ORIENTATION_BOTTOM_LEFT
            : CKBGFX_ORIENTATION_TOP_LEFT;
    const CKBOOL executeColorClear =
        !m_ViewClearRecorded[View] &&
        (m_ViewClearFlags[View] & CKRST_CTXCLEAR_COLOR) != 0;

    for (CKDWORD i = 0; i < frameBuffer->ColorCount; ++i) {
        const CKFrameBufferAttachmentDesc &attachment = frameBuffer->Color[i];
        CKBgfxTextureRecord *texture = GetTexture(attachment.Texture);
        if (!texture || attachment.Mip >= texture->MipCount ||
            attachment.Mip >= CKBGFX_MAX_TRACKED_MIPS)
            continue;

        CKBgfxTextureOrientation current =
            (CKBgfxTextureOrientation)texture->ReadbackOrientation[attachment.Mip];
        if (executeColorClear) {
            const CKDWORD width = XMax((CKDWORD)1, texture->Width >> attachment.Mip);
            const CKDWORD height = XMax((CKDWORD)1, texture->Height >> attachment.Mip);
            const CKBOOL fullClear =
                m_ViewRect[View].left == 0 && m_ViewRect[View].top == 0 &&
                m_ViewRect[View].right == (int)width &&
                m_ViewRect[View].bottom == (int)height;
            current = CKBgfxMergeOrientation(current, targetOrientation, fullClear);
        }
        if (HasDraw)
            current = CKBgfxMergeOrientation(current, targetOrientation, FALSE);
        texture->ReadbackOrientation[attachment.Mip] = (CKBYTE)current;
    }
    if (executeColorClear)
        m_ViewClearRecorded[View] = TRUE;
}

CKBgfxRasterizerContext::~CKBgfxRasterizerContext()
{
    XArray<CKBgfxScreenShotRequest> cancelledScreenShots;
    {
        VxMutexLock lock(m_ScreenShotMutex);
        cancelledScreenShots.Swap(m_PendingScreenShots);
    }
    for (int i = 0; i < cancelledScreenShots.Size(); ++i) {
        CKBgfxScreenShotRequest &request = cancelledScreenShots[i];
        if (request.Callback) {
            request.Callback(request.UserData, request.FrameBuffer, 0, 0, 0,
                         UNKNOWN_PF, NULL, 0, FALSE);
        }
    }
    if (m_BgfxInitialized)
    {
        if (m_DefaultEncoder.m_Active.load(std::memory_order_acquire))
        {
            m_DefaultEncoder.m_Context = NULL;
            m_DefaultEncoder.m_Active.store(FALSE, std::memory_order_relaxed);
        }
        for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
        {
            if (m_Encoders[i].m_Active.load(std::memory_order_acquire))
            {
                CKBgfxLogf("Encoder",
                           "active pool encoder during context destruction slot=%d",
                           i);
            }
        }

        if (bgfx::isValid(m_DefaultWhiteTexture)) {
            bgfx::destroy(m_DefaultWhiteTexture);
            m_DefaultWhiteTexture = BGFX_INVALID_HANDLE;
        }

        DestroyAllRecords(m_FrameBuffers);
        DestroyAllRecords(m_Textures);
        DestroyAllRecords(m_Programs);
        DestroyAllRecords(m_Shaders);
        DestroyAllRecords(m_Uniforms);
        DestroyAllRecords(m_VertexLayouts);
        DestroyAllRecords(m_VertexBuffers);
        DestroyAllRecords(m_IndexBuffers);
        DestroyAllRecords(m_OcclusionQueries);
        DestroyAllRecords(m_IndirectBuffers);

        bgfx::shutdown();
        m_BgfxInitialized = FALSE;
        m_Created = FALSE;
        m_RendererType = bgfx::RendererType::Count;
    }

    CKBgfxReleaseActiveContext(this);
    CKBgfxCloseLogFile();
}

CKERROR CKBgfxRasterizerContext::Create(WIN_HANDLE Window, int PosX, int PosY,
                                        int Width, int Height, int Bpp,
                                        CKBOOL Fullscreen, int RefreshRate,
                                        int Zbpp, int StencilBpp)
{
    if (m_BgfxInitialized || m_Created)
        return CKERR_INVALIDOPERATION;
    if (!CKBgfxClaimActiveContext(this)) {
        CKBgfxLogf("Init", "another CKBgfxRasterizerContext is already active");
        return CKERR_INVALIDOPERATION;
    }

    m_Window = Window;
    m_PosX = PosX;
    m_PosY = PosY;

    if (Width <= 0 || Height <= 0)
    {
        CKRECT rc = {0, 0,
                     CKBgfxConfigPositiveInt("Renderer", "FallbackWidth", 640),
                     CKBgfxConfigPositiveInt("Renderer", "FallbackHeight", 480)};
        if (Window)
            VxGetClientRect(Window, &rc);
        Width = rc.right - rc.left;
        Height = rc.bottom - rc.top;
    }

    if (Width <= 0)
        Width = CKBgfxConfigPositiveInt("Renderer", "FallbackWidth", 640);
    if (Height <= 0)
        Height = CKBgfxConfigPositiveInt("Renderer", "FallbackHeight", 480);

    if (Width > 0xffff || Height > 0xffff) {
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }

    CKBgfxResolveFullscreenWindowSize(Window, Fullscreen, Width, Height);

    if (Width <= 0 || Height <= 0 || Width > 0xffff || Height > 0xffff) {
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }

    m_Width = Width;
    m_Height = Height;
    m_Bpp = (Bpp > 0) ? Bpp : 32;
    m_ZBpp = (Zbpp > 0) ? Zbpp : 24;
    m_StencilBpp = (StencilBpp > 0) ? StencilBpp : 8;
    m_Fullscreen = Fullscreen;
    m_RefreshRate = RefreshRate;

    const bgfx::RendererType::Enum requestedRenderer = CKBgfxParseRequestedRenderer();

    bgfx::Init init;
    init.type = requestedRenderer;
    if (!CKBgfxFillSDLPlatformData(Window, init.platformData)) {
        CKBgfxLogf("Init", "failed to extract SDL native window data window=%p", Window);
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }
    CKBgfxLogf("Init",
               "platformData ndt=%p nwh=%p type=%s",
               init.platformData.ndt,
               init.platformData.nwh,
               CKBgfxNativeWindowHandleTypeName(init.platformData.type));
    init.resolution.width = Width;
    init.resolution.height = Height;
    init.resolution.reset = m_ResetFlags;
    init.callback = &m_BgfxCallback;

    if (!bgfx::init(init)) {
        CKBgfxLogf("Init", "bgfx init failed for requested renderer '%s' window=%p size=%dx%d",
                   CKBgfxRendererTypeName(requestedRenderer), Window, Width, Height);
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDOPERATION;
    }

    m_BgfxInitialized = TRUE;
    m_ApiThreadId = VxThread::GetCurrentVxThreadId();
    const bgfx::RendererType::Enum actualRenderer = bgfx::getRendererType();
    m_RendererType = actualRenderer;
    m_RendererName = CKBgfxRendererTypeName(actualRenderer);
    if (CKBgfxShaderProfile(actualRenderer) == CKRST_SHADER_PROFILE_UNKNOWN) {
        CKBgfxLogf("Init", "renderer %s is unsupported: no shader profile is built",
                   CKBgfxRendererTypeName(actualRenderer));
        bgfx::shutdown();
        m_BgfxInitialized = FALSE;
        m_RendererType = bgfx::RendererType::Count;
        CKBgfxReleaseActiveContext(this);
        return CKERR_NOTIMPLEMENTED;
    }
    const bgfx::Caps *caps = bgfx::getCaps();
    m_TargetDesc = CKRasterizerTargetDesc();
    m_TargetDesc.ShaderProfile = CKBgfxShaderProfile(actualRenderer);
    m_TargetDesc.HomogeneousDepth = caps && caps->homogeneousDepth ? TRUE : FALSE;
    m_TargetDesc.OriginBottomLeft = caps && caps->originBottomLeft ? TRUE : FALSE;
    m_CapsDesc = CKRasterizerCapsDesc();
    memset(m_NativeFormatCaps, 0, sizeof(m_NativeFormatCaps));
    if (caps) {
        m_NativeSupported = caps->supported;
        memcpy(m_NativeFormatCaps, caps->formats, sizeof(m_NativeFormatCaps));

        const bgfx::Caps::Limits &limits = caps->limits;
        m_CapsDesc.MaxDrawCalls = limits.maxDrawCalls;
        m_CapsDesc.MaxBlits = limits.maxBlits;
        m_CapsDesc.MaxTextureSize = limits.maxTextureSize;
        m_CapsDesc.MaxTextureLayers = limits.maxTextureLayers;
        m_CapsDesc.MaxRenderViews = XMin((CKDWORD)limits.maxViews,
                                         (CKDWORD)CKRST_MAX_RENDER_VIEWS);
        m_CapsDesc.MaxFrameBuffers = limits.maxFrameBuffers;
        m_CapsDesc.MaxColorAttachments = limits.maxFBAttachments;
        m_CapsDesc.MaxPrograms = limits.maxPrograms;
        m_CapsDesc.MaxShaders = limits.maxShaders;
        m_CapsDesc.MaxTextures = limits.maxTextures;
        m_CapsDesc.MaxTextureStages = XMin((CKDWORD)limits.maxTextureSamplers,
                                           (CKDWORD)CKRST_MAX_TEXTURE_STAGES);
        m_CapsDesc.MaxComputeBindings = limits.maxComputeBindings;
        m_CapsDesc.MaxVertexLayouts = limits.maxVertexLayouts;
        m_CapsDesc.MaxVertexStreams = XMin((CKDWORD)limits.maxVertexStreams,
                                           (CKDWORD)CKRST_MAX_VERTEX_STREAMS);
        m_CapsDesc.MaxIndexBuffers = limits.maxIndexBuffers;
        m_CapsDesc.MaxVertexBuffers = limits.maxVertexBuffers;
        m_CapsDesc.MaxDynamicIndexBuffers = limits.maxDynamicIndexBuffers;
        m_CapsDesc.MaxDynamicVertexBuffers = limits.maxDynamicVertexBuffers;
        m_CapsDesc.MaxUniforms = limits.maxUniforms;
        m_CapsDesc.MaxOcclusionQueries = limits.maxOcclusionQueries;
#if BGFX_CONFIG_MULTITHREADED
        m_CapsDesc.MaxEncoders = XMin((CKDWORD)limits.maxEncoders,
                                      (CKDWORD)CKRST_MAX_ENCODERS);
#else
        m_CapsDesc.MaxEncoders = 0;
#endif
        m_CapsDesc.MinResourceCommandBufferSize = limits.minResourceCbSize;
        m_CapsDesc.MaxTransientVertexBufferSize = limits.maxTransientVbSize;
        m_CapsDesc.MaxTransientIndexBufferSize = limits.maxTransientIbSize;
        m_CapsDesc.MinUniformBufferSize = limits.minUniformBufferSize;
        m_CapsDesc.MaxTransforms = CKRST_MAX_TRANSFORMS;

        m_CapsDesc.Features = CKRST_CAPS_VERTEX_SHADER |
                              CKRST_CAPS_PIXEL_SHADER |
                              CKRST_CAPS_RENDER_VIEWS |
                              CKRST_CAPS_FRAMEBUFFER |
                              CKRST_CAPS_TRANSIENT_BUFFERS |
                              CKRST_CAPS_SCISSOR |
                              CKRST_CAPS_BUFFER_UPDATE |
                              CKRST_CAPS_TEXTURE_UPDATE |
                              CKRST_CAPS_BLEND_EQUATION |
                              CKRST_CAPS_TRANSFORM_CACHE |
                              CKRST_CAPS_TEXTURE_CUBE;
        if (caps->supported & BGFX_CAPS_INSTANCING)
            m_CapsDesc.Features |= CKRST_CAPS_INSTANCING;
        if (caps->supported & BGFX_CAPS_TEXTURE_READ_BACK)
            m_CapsDesc.Features |= CKRST_CAPS_TEXTURE_READBACK;
        if (caps->supported & BGFX_CAPS_TEXTURE_BLIT)
            m_CapsDesc.Features |= CKRST_CAPS_BLIT;
        if (caps->supported & BGFX_CAPS_INDEX32)
            m_CapsDesc.Features |= CKRST_CAPS_INDEX32;
        if (caps->supported & BGFX_CAPS_TEXTURE_COMPARE_ALL)
            m_CapsDesc.Features |= CKRST_CAPS_TEXTURE_COMPARISON;
        if (caps->supported & BGFX_CAPS_COMPUTE)
            m_CapsDesc.Features |= CKRST_CAPS_COMPUTE;
        if (caps->supported & BGFX_CAPS_OCCLUSION_QUERY)
            m_CapsDesc.Features |= CKRST_CAPS_OCCLUSION_QUERY;
        if (caps->supported & BGFX_CAPS_DRAW_INDIRECT)
            m_CapsDesc.Features |= CKRST_CAPS_DRAW_INDIRECT;
        if (caps->supported & BGFX_CAPS_TEXTURE_3D)
            m_CapsDesc.Features |= CKRST_CAPS_TEXTURE_3D;
        if (caps->supported & BGFX_CAPS_IMAGE_RW)
            m_CapsDesc.Features |= CKRST_CAPS_IMAGE_RW;
        if (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_HALF)
            m_CapsDesc.Features |= CKRST_CAPS_VERTEX_ATTRIB_HALF;
        if (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_UINT10)
            m_CapsDesc.Features |= CKRST_CAPS_VERTEX_ATTRIB_UINT10;

        const bgfx::TextureFormat::Enum depthFormats[] = {
            bgfx::TextureFormat::D16,
            bgfx::TextureFormat::D24,
            bgfx::TextureFormat::D24S8,
            bgfx::TextureFormat::D32F,
        };
        for (CKDWORD i = 0; i < sizeof(depthFormats) / sizeof(depthFormats[0]); ++i) {
            if (m_NativeFormatCaps[depthFormats[i]] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) {
                m_CapsDesc.Features |= CKRST_CAPS_DEPTH_TEXTURE;
                break;
            }
        }
    }
    if (m_Driver) {
        m_Driver->m_Desc.Format("bgfx %s Driver", m_RendererName);
    }

    CKBgfxLogf("Init", "renderer requested=%s actual=%s",
               CKBgfxRendererTypeName(requestedRenderer), m_RendererName);
    {
        bgfx::RendererType::Enum supported[bgfx::RendererType::Count];
        const uint8_t count = bgfx::getSupportedRenderers((uint8_t)bgfx::RendererType::Count, supported);
        char names[256];
        CKDWORD offset = 0;
        names[0] = '\0';
        for (uint8_t i = 0; i < count && offset + 1 < sizeof(names); ++i) {
            int written = snprintf(names + offset, sizeof(names) - offset,
                                   i == 0 ? "%s" : ",%s",
                                   CKBgfxRendererTypeName(supported[i]));
            if (written <= 0)
                break;
            if ((CKDWORD)written >= sizeof(names) - offset) {
                offset = sizeof(names) - 1;
                break;
            }
            offset += (CKDWORD)written;
        }
        names[sizeof(names) - 1] = '\0';
        CKBgfxLogf("Init", "supported renderers=%s", names);
    }
    const CK_SHADER_PROFILE shaderProfile = CKBgfxShaderProfile(actualRenderer);
    if ((m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 || CKBgfxLogEnabled("Config", false)) {
        CKBgfxLogf("DrawMap",
                   "enabled=%u submits=%u resources=%u views=%u markers=%u frame=%u summary=%u renderer=%s profile=%s sequentialViews=clear,background2d,renderfirst3d,opaque3d,stencil-clear,transparent3d,foreground2d",
                   (m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 ? 1u : 0u,
                   CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_SUBMITS),
                   CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES),
                   CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_VIEWS),
                   CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS),
                   CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME),
                   CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_SUMMARY),
                   m_RendererName,
                   CKBgfxShaderProfileName(shaderProfile));
    }
    if (caps) {
        CKBgfxLogf("Init",
                   "backend conventions profile=%s(0x%08X) homogeneousDepth=%u originBottomLeft=%u maxTextureSamplers=%u maxUniforms=%u textureCompareAll=%u texture3D=%u rendererMultithreaded=%u",
                   CKBgfxShaderProfileName(shaderProfile),
                   shaderProfile,
                   caps->homogeneousDepth ? 1u : 0u,
                   caps->originBottomLeft ? 1u : 0u,
                   (unsigned)caps->limits.maxTextureSamplers,
                   (unsigned)caps->limits.maxUniforms,
                   (caps->supported & BGFX_CAPS_TEXTURE_COMPARE_ALL) ? 1u : 0u,
                   (caps->supported & BGFX_CAPS_TEXTURE_3D) ? 1u : 0u,
                   (caps->supported & BGFX_CAPS_RENDERER_MULTITHREADED) ? 1u : 0u);
    }
    CKBgfxLogf("Init",
               "texture policy openGLAutoMipWorkaround=%u shaderDepthCompare=%u samplerBaseLevelAlias=%u",
               CKBgfxIsOpenGLRenderer() ? 1u : 0u,
               1u,
               CKBgfxIsOpenGLRenderer() ? 1u : 0u);

    const uint32_t whitePixel = 0xffffffffu;
    const bgfx::Memory *whiteMem = bgfx::copy(&whitePixel, sizeof(whitePixel));
    m_DefaultWhiteTexture = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::BGRA8, 0, whiteMem);
    if (!bgfx::isValid(m_DefaultWhiteTexture)) {
        CKBgfxLogf("Init", "failed to create default white texture");
        bgfx::shutdown();
        m_BgfxInitialized = FALSE;
        m_RendererType = bgfx::RendererType::Count;
        CKBgfxReleaseActiveContext(this);
        return CKERR_OUTOFMEMORY;
    }

    bgfx::setViewRect(0, 0, 0, (uint16_t)Width, (uint16_t)Height);
    bgfx::setViewClear(0,
                        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                        0x000000ff, 1.0f, 0);
    bgfx::touch(0);
    bgfx::frame();
    ConfigureDebug();

    m_Created = TRUE;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::Resize(int PosX, int PosY,
                                        int Width, int Height, CKDWORD Flags)
{
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (Width <= 0 || Height <= 0 || Width > 0xffff || Height > 0xffff ||
        Flags != 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxResolveFullscreenWindowSize(m_Window, m_Fullscreen, Width, Height);
    if (Width <= 0 || Height <= 0 || Width > 0xffff || Height > 0xffff)
        return CKERR_INVALIDPARAMETER;

    m_PosX = PosX;
    m_PosY = PosY;
    m_Width = Width;
    m_Height = Height;

    bgfx::reset((uint32_t)Width, (uint32_t)Height, m_ResetFlags);
    bgfx::setViewRect(0, 0, 0, (uint16_t)Width, (uint16_t)Height);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::GetTargetDesc(CKRasterizerTargetDesc *Target) const
{
    if (!Target || Target->Size < sizeof(CKRasterizerTargetDesc))
        return CKERR_INVALIDPARAMETER;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    *Target = m_TargetDesc;
    return CK_OK;
}

static CKDWORD CKBgfxMapFormatCaps(CKDWORD NativeCaps, CKBOOL AllowReadback,
                                    CKBOOL AllowComparison)
{
    CKDWORD result = CKRST_FORMAT_CAPS_NONE;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_2D)
        result |= CKRST_FORMAT_CAPS_TEXTURE_2D;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_3D)
        result |= CKRST_FORMAT_CAPS_TEXTURE_3D;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_CUBE)
        result |= CKRST_FORMAT_CAPS_TEXTURE_CUBE;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
        result |= CKRST_FORMAT_CAPS_FRAMEBUFFER;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA)
        result |= CKRST_FORMAT_CAPS_FRAMEBUFFER_MSAA;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ)
        result |= CKRST_FORMAT_CAPS_IMAGE_READ;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE)
        result |= CKRST_FORMAT_CAPS_IMAGE_WRITE;
    if (NativeCaps & BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN)
        result |= CKRST_FORMAT_CAPS_MIP_AUTOGEN;
    if (NativeCaps & (BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB |
                      BGFX_CAPS_FORMAT_TEXTURE_3D_SRGB |
                      BGFX_CAPS_FORMAT_TEXTURE_CUBE_SRGB))
        result |= CKRST_FORMAT_CAPS_SRGB;
    if (AllowReadback && (result & CKRST_FORMAT_CAPS_TEXTURE_2D))
        result |= CKRST_FORMAT_CAPS_READBACK;
    if (AllowComparison && (result & CKRST_FORMAT_CAPS_TEXTURE_2D))
        result |= CKRST_FORMAT_CAPS_TEXTURE_COMPARE;
    return result;
}

CKERROR CKBgfxRasterizerContext::GetCaps(CKRasterizerCapsDesc *Caps) const
{
    if (!Caps || Caps->Size < sizeof(CKRasterizerCapsDesc))
        return CKERR_INVALIDPARAMETER;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    *Caps = m_CapsDesc;
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::GetTextureFormatCaps(VX_PIXELFORMAT Format,
                                                       CKTextureFormatCaps *Caps) const
{
    if (!Caps || Caps->Size < sizeof(CKTextureFormatCaps))
        return CKERR_INVALIDPARAMETER;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    bgfx::TextureFormat::Enum nativeFormat;
    if (!CKBgfxTryTextureFormat(Format, nativeFormat))
        return CKERR_INVALIDPARAMETER;
    *Caps = CKTextureFormatCaps();
    Caps->Format = Format;
    const CKBOOL allowReadback =
        (m_CapsDesc.Features & CKRST_CAPS_TEXTURE_READBACK) != 0 &&
        CKBgfxCanExposeReadback(Format, nativeFormat)
            ? TRUE : FALSE;
    Caps->Caps = CKBgfxMapFormatCaps(
        m_NativeFormatCaps[nativeFormat], allowReadback, FALSE);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::GetDepthFormatCaps(CK_DEPTH_FORMAT Format,
                                                     CKDepthFormatCaps *Caps) const
{
    if (!Caps || Caps->Size < sizeof(CKDepthFormatCaps))
        return CKERR_INVALIDPARAMETER;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    bgfx::TextureFormat::Enum nativeFormat;
    if (!CKBgfxTryDepthFormat(Format, nativeFormat))
        return CKERR_INVALIDPARAMETER;
    *Caps = CKDepthFormatCaps();
    Caps->Format = Format;
    Caps->Caps = CKBgfxMapFormatCaps(
        m_NativeFormatCaps[nativeFormat], FALSE,
        (m_CapsDesc.Features & CKRST_CAPS_TEXTURE_COMPARISON) != 0 ? TRUE : FALSE);
    return CK_OK;
}

void CKBgfxRasterizerContext::ConfigureDebug()
{
    const CKBgfxDebugConfig &debug = CKBgfxDebugSettings();
    m_DebugOverlay = debug.Overlay ? TRUE : FALSE;
    m_DebugLogPresentSync = debug.Log.PresentSync ? TRUE : FALSE;
    m_DebugLogTextureBindings = debug.Log.TextureBindings ? TRUE : FALSE;
    m_DebugLogTextures = debug.Log.Textures ? TRUE : FALSE;
    m_DebugLogUniforms = debug.Log.Uniforms ? TRUE : FALSE;
    SetDebug(m_DebugFlags);

    if (debug.Log.Config ||
        m_DebugBgfxFlags != BGFX_DEBUG_NONE ||
        m_DebugOverlay) {
        CKBgfxLogf("Debug", "configured bgfxFlags=0x%X overlay=%d",
                 m_DebugBgfxFlags, m_DebugOverlay ? 1 : 0);
    }
}

void CKBgfxRasterizerContext::DrawDebugOverlay()
{
    if (!m_DebugOverlay)
        return;

    const bgfx::Stats *s = bgfx::getStats();
    bgfx::dbgTextClear(0, false);
    bgfx::dbgTextPrintf(0, 0, 0x4f, "CKBgfx frame=%u renderer=%s size=%ux%u",
                        m_DebugFrameId, m_RendererName,
                        (unsigned)m_Width, (unsigned)m_Height);
    if (s) {
        bgfx::dbgTextPrintf(0, 1, 0x2f, "bgfx gpuFrame=%u draws=%u blits=%u computes=%u views=%u",
                            s->gpuFrameNum, s->numDraw, s->numBlit, s->numCompute, s->numViews);
        bgfx::dbgTextPrintf(0, 2, 0x2f, "transient vb=%u ib=%u waitSubmit=%lld waitRender=%lld",
                            s->transientVbUsed, s->transientIbUsed,
                            (long long)s->waitSubmit, (long long)s->waitRender);
    }
    bgfx::dbgTextPrintf(0, 3, 0x1f, "%s", CKBgfxDebugViewLine0());
    bgfx::dbgTextPrintf(0, 4, 0x1f, "%s", CKBgfxDebugViewLine1());
}

// ---------------------------------------------------------------------------
// Accessor helpers
// ---------------------------------------------------------------------------

CKBgfxShaderRecord *CKBgfxRasterizerContext::GetShader(CKDWORD Handle)
{
    return GetSlot(m_Shaders, Handle, m_ResourceTableMutex);
}

CKBgfxProgramRecord *CKBgfxRasterizerContext::GetProgram(CKDWORD Handle)
{
    return GetSlot(m_Programs, Handle, m_ResourceTableMutex);
}

CKBgfxUniformRecord *CKBgfxRasterizerContext::GetUniform(CKDWORD Handle)
{
    return GetSlot(m_Uniforms, Handle, m_ResourceTableMutex);
}

CKBgfxVertexLayoutRecord *CKBgfxRasterizerContext::GetVertexLayout(CKDWORD Handle)
{
    return GetSlot(m_VertexLayouts, Handle, m_ResourceTableMutex);
}

CKBgfxVertexBufferRecord *CKBgfxRasterizerContext::GetVertexBuffer(CKDWORD Handle)
{
    return GetSlot(m_VertexBuffers, Handle, m_ResourceTableMutex);
}

CKBgfxIndexBufferRecord *CKBgfxRasterizerContext::GetIndexBuffer(CKDWORD Handle)
{
    return GetSlot(m_IndexBuffers, Handle, m_ResourceTableMutex);
}

CKBgfxTextureRecord *CKBgfxRasterizerContext::GetTexture(CKDWORD Handle)
{
    return GetSlot(m_Textures, Handle, m_ResourceTableMutex);
}

CKBgfxFrameBufferRecord *CKBgfxRasterizerContext::GetFrameBuffer(CKDWORD Handle)
{
    return GetSlot(m_FrameBuffers, Handle, m_ResourceTableMutex);
}

void CKBgfxRasterizerContext::TraceTextureMap(CKSTRING Event, CKDWORD Texture,
                                              const CKBgfxTextureRecord *Record)
{
    if (!m_DrawMapActive ||
        !CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES))
        return;
    CKBgfxDrawMapTextureTrace trace;
    trace.Event = Event;
    trace.Frame = m_DebugFrameId;
    trace.Texture = Texture;
    trace.Bgfx = Record && bgfx::isValid(Record->Handle) ? Record->Handle.idx : 0xffff;
    trace.SamplerBase = Record && bgfx::isValid(Record->SamplerBaseHandle) ? Record->SamplerBaseHandle.idx : 0xffff;
    trace.Kind = (CKSTRING)CKBgfxTextureKindName(Record);
    trace.Width = Record ? Record->Width : 0u;
    trace.Height = Record ? Record->Height : 0u;
    trace.Depth = Record ? Record->Depth : 0u;
    trace.Format = Record ? (int)Record->Format : -1;
    trace.Mips = Record ? Record->MipCount : 0u;
    trace.Flags = Record ? Record->Flags : 0u;
    trace.AutoMip = Record && Record->RequestedAutoMips ? 1u : 0u;
    trace.BaseSampler = Record && Record->SamplerBaseValid ? 1u : 0u;
    trace.IsDepth = Record && Record->IsDepth ? 1u : 0u;
    trace.BitsPerPixel = Record ? Record->BitsPerPixel : 0u;
    CKBgfxDrawMapTraceTexture(&trace);
}

void CKBgfxRasterizerContext::TraceProgramMap(CKSTRING Event, CKDWORD Program,
                                              const CKBgfxProgramRecord *Record)
{
    char spec[160];
    CK_SHADER_PROFILE profile = CKRST_SHADER_PROFILE_UNKNOWN;
    if (!m_DrawMapActive ||
        !CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES))
        return;
    CKBgfxDrawMapProgramTrace trace;
    CKRasterizerTargetDesc target;
    if (GetTargetDesc(&target) == CK_OK)
        profile = target.ShaderProfile;
    spec[0] = '\0';
    trace.Event = Event;
    trace.Frame = m_DebugFrameId;
    trace.Program = Program;
    trace.Bgfx = Record && bgfx::isValid(Record->Handle) ? Record->Handle.idx : 0xffff;
    trace.VertexShader = Record ? Record->VertexShader : 0u;
    trace.PixelShader = Record ? Record->PixelShader : 0u;
    trace.Profile = (CKSTRING)CKBgfxShaderProfileName(profile);
    trace.SpecCount = 0;
    trace.SpecHash = 0;
    trace.Spec = spec;
    CKBgfxDrawMapTraceProgram(&trace);
}

void CKBgfxRasterizerContext::TraceBufferMap(CKSTRING Event, CKSTRING Kind,
                                             CKDWORD Buffer,
                                             CKDWORD BgfxHandle,
                                             CKDWORD Layout,
                                             CKDWORD Stride,
                                             CKDWORD Count,
                                             CKDWORD Index32,
                                             CKDWORD Flags)
{
    if (!m_DrawMapActive ||
        !CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES))
        return;
    CKBgfxDrawMapBufferTrace trace;
    trace.Event = Event;
    trace.Frame = m_DebugFrameId;
    trace.Kind = Kind;
    trace.Buffer = Buffer;
    trace.Bgfx = BgfxHandle;
    trace.Layout = Layout;
    trace.Stride = Stride;
    trace.Count = Count;
    trace.Index32 = Index32;
    trace.Flags = Flags;
    CKBgfxDrawMapTraceBuffer(&trace);
}

void CKBgfxRasterizerContext::RecordInvalidSubmit(CKSTRING Kind, CKRenderView View,
                                                  CKDWORD Program, CKSTRING Reason)
{
    m_DebugInvalidSubmitCount.fetch_add(1, std::memory_order_relaxed);
    if (m_DrawMapSubmitActive) {
        CKBgfxDrawMapTraceSubmitMiss(Reason,
                                      m_DebugFrameId,
                                      0,
                                      (unsigned)View,
                                      Program,
                                      Kind,
                                      NULL);
    }
}

void CKBgfxRasterizerContext::RecordTransientAllocMiss(const char *Kind,
                                                       CKDWORD Requested,
                                                       CKDWORD Available)
{
    const CKDWORD miss = m_DebugTransientAllocMissCount.fetch_add(1, std::memory_order_relaxed);
    if (miss < 16 || CKBgfxLogEnabled("Config", false)) {
        CKBgfxLogf("Transient",
                   "%s allocation rejected requested=%u available=%u",
                   Kind ? Kind : "unknown",
                   (unsigned)Requested,
                   (unsigned)Available);
    }
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::CreateVertexBuffer(const CKVertexBufferDesc *Desc,
                                                     const void *Data,
                                                     CKDWORD *OutBuffer)
{
    if (!OutBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutBuffer = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if ((Desc->m_Flags & (CKRST_VB_COMPUTE_READ | CKRST_VB_COMPUTE_WRITE)) &&
        (m_CapsDesc.Features & CKRST_CAPS_COMPUTE) == 0)
        return CKERR_NOTIMPLEMENTED;
    if ((Desc->m_Flags & CKRST_VB_COMPUTE_WRITE) && Data)
        return CKERR_INVALIDPARAMETER;

    CKDWORD vertexSize = Desc->m_VertexSize;
    if (vertexSize == 0 || vertexSize > UINT16_MAX ||
        Desc->m_MaxVertexCount == 0)
        return CKERR_INVALIDPARAMETER;
    uint64_t totalSize64 = (uint64_t)Desc->m_MaxVertexCount * vertexSize;
    if (totalSize64 > UINT32_MAX)
        return CKERR_OUTOFMEMORY;
    CKDWORD totalSize = (CKDWORD)totalSize64;

    bgfx::VertexLayout layout;
    layout.begin(m_RendererType);
    CKDWORD remainingStride = vertexSize;
    while (remainingStride > 0) {
        const uint8_t chunk = static_cast<uint8_t>(
            XMin(remainingStride, (CKDWORD)UINT8_MAX));
        layout.skip(chunk);
        remainingStride -= chunk;
    }
    layout.end();

    uint16_t flags = (Desc->m_Flags & CKRST_VB_COMPUTE_WRITE)
        ? 0 : BGFX_BUFFER_ALLOW_RESIZE;
    if (Desc->m_Flags & CKRST_VB_COMPUTE_READ)
        flags |= BGFX_BUFFER_COMPUTE_READ;
    if (Desc->m_Flags & CKRST_VB_COMPUTE_WRITE)
        flags |= BGFX_BUFFER_COMPUTE_WRITE;

    bgfx::DynamicVertexBufferHandle handle;
    if (Data)
    {
        const bgfx::Memory *mem = bgfx::copy(Data, totalSize);
        handle = bgfx::createDynamicVertexBuffer(mem, layout, flags);
    }
    else
    {
        handle = bgfx::createDynamicVertexBuffer(Desc->m_MaxVertexCount, layout, flags);
    }

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD buffer = AllocateSlot(m_VertexBuffers,
                                         m_CapsDesc.MaxDynamicVertexBuffers,
                                         m_ResourceTableMutex);
    if (buffer == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxVertexBufferRecord();
    rec->Handle = handle;
    rec->Flags = Desc->m_Flags;
    rec->Layout = 0;
    rec->VertexSize = vertexSize;
    rec->VertexCount = Desc->m_MaxVertexCount;
    rec->Size = totalSize;

    m_VertexBuffers[buffer] = rec;
    *OutBuffer = buffer;
    TraceBufferMap((CKSTRING)"create", (CKSTRING)"vb", buffer, rec->Handle.idx,
                   rec->Layout, rec->VertexSize, Desc->m_MaxVertexCount,
                   0, Desc->m_Flags);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateIndexBuffer(const CKIndexBufferDesc *Desc,
                                                    CKBOOL Index32,
                                                    const void *Data,
                                                    CKDWORD *OutBuffer)
{
    if (!OutBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutBuffer = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (Desc->m_MaxIndexCount == 0)
        return CKERR_INVALIDPARAMETER;
    if (Index32 && (m_CapsDesc.Features & CKRST_CAPS_INDEX32) == 0)
        return CKERR_NOTIMPLEMENTED;

    CKDWORD indexSize = Index32 ? 4 : 2;
    uint64_t totalSize64 = (uint64_t)Desc->m_MaxIndexCount * indexSize;
    if (totalSize64 > UINT32_MAX)
        return CKERR_OUTOFMEMORY;
    CKDWORD totalSize = (CKDWORD)totalSize64;

    uint16_t flags = BGFX_BUFFER_ALLOW_RESIZE;
    if (Index32) flags |= BGFX_BUFFER_INDEX32;

    bgfx::DynamicIndexBufferHandle handle;
    if (Data)
    {
        const bgfx::Memory *mem = bgfx::copy(Data, totalSize);
        handle = bgfx::createDynamicIndexBuffer(mem, flags);
    }
    else
    {
        handle = bgfx::createDynamicIndexBuffer(Desc->m_MaxIndexCount, flags);
    }

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD buffer = AllocateSlot(m_IndexBuffers,
                                         m_CapsDesc.MaxDynamicIndexBuffers,
                                         m_ResourceTableMutex);
    if (buffer == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxIndexBufferRecord();
    rec->Handle = handle;
    rec->Index32 = Index32;
    rec->IndexCount = Desc->m_MaxIndexCount;
    rec->Size = totalSize;

    m_IndexBuffers[buffer] = rec;
    *OutBuffer = buffer;
    TraceBufferMap((CKSTRING)"create", (CKSTRING)"ib", buffer, rec->Handle.idx,
                   0, indexSize, Desc->m_MaxIndexCount,
                   Index32 ? 1u : 0u, Desc->m_Flags);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateTexture(const CKTextureDesc *Desc,
                                                const VxImageDescEx *Data,
                                                CKDWORD *OutTexture)
{
    static int s_CreateTextureLogCount = 0;
    if (!OutTexture)
        return CKERR_INVALIDPARAMETER;
    *OutTexture = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (Desc->Flags & CKRST_TEXTURE_DEPTHSTENCIL)
        return CKERR_INVALIDPARAMETER;

    const CKBOOL cubeRequested = (Desc->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    const CKBOOL volumeRequested = (Desc->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0;
    if (Desc->Format.Width <= 0 || Desc->Format.Height <= 0 || Desc->Depth == 0 ||
        (CKDWORD)Desc->Format.Width > m_CapsDesc.MaxTextureSize ||
        (CKDWORD)Desc->Format.Height > m_CapsDesc.MaxTextureSize ||
        Desc->Depth > m_CapsDesc.MaxTextureSize ||
        (cubeRequested && volumeRequested) ||
        (cubeRequested && (Desc->Format.Width != Desc->Format.Height || Desc->Depth != 1)) ||
        (volumeRequested ? Desc->Depth <= 1 : Desc->Depth != 1))
        return CKERR_INVALIDPARAMETER;
    uint16_t w = (uint16_t)Desc->Format.Width;
    uint16_t h = (uint16_t)Desc->Format.Height;
    uint16_t d = (uint16_t)XMax((CKDWORD)1, Desc->Depth);

    VX_PIXELFORMAT pf = VxImageDesc2PixelFormat(Desc->Format);
    bgfx::TextureFormat::Enum fmt;
    if (!CKBgfxTryTextureFormat(pf, fmt))
        return CKERR_INVALIDPARAMETER;
    const CKBOOL allowReadback =
        (m_CapsDesc.Features & CKRST_CAPS_TEXTURE_READBACK) != 0 &&
        CKBgfxCanExposeReadback(pf, fmt)
            ? TRUE : FALSE;
    const CKDWORD formatCaps = CKBgfxMapFormatCaps(
        m_NativeFormatCaps[fmt], allowReadback, FALSE);
    const CKBOOL cube = cubeRequested;
    const CKBOOL volume = volumeRequested;
    if (Data && Data->Image && VxImageDesc2PixelFormat(*Data) != pf)
        return CKERR_INVALIDPARAMETER;
    CKDWORD requiredFormatCaps = cube ? CKRST_FORMAT_CAPS_TEXTURE_CUBE
                                     : (volume ? CKRST_FORMAT_CAPS_TEXTURE_3D
                                               : CKRST_FORMAT_CAPS_TEXTURE_2D);
    if ((formatCaps & requiredFormatCaps) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (cube && (m_CapsDesc.Features & CKRST_CAPS_TEXTURE_CUBE) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (volume && (m_CapsDesc.Features & CKRST_CAPS_TEXTURE_3D) == 0)
        return CKERR_NOTIMPLEMENTED;
    if ((Desc->Flags & CKRST_TEXTURE_RENDERTARGET) &&
        (formatCaps & CKRST_FORMAT_CAPS_FRAMEBUFFER) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Flags & CKRST_TEXTURE_READBACK) {
        if (cube || volume ||
            (m_CapsDesc.Features & CKRST_CAPS_TEXTURE_READBACK) == 0 ||
            (formatCaps & CKRST_FORMAT_CAPS_READBACK) == 0)
            return CKERR_NOTIMPLEMENTED;
    }
    if ((Desc->Flags & CKRST_TEXTURE_BLIT_DST) &&
        (m_CapsDesc.Features & CKRST_CAPS_BLIT) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Flags & CKRST_TEXTURE_COMPUTE_WRITE) {
        if ((m_CapsDesc.Features & CKRST_CAPS_IMAGE_RW) == 0 ||
            (formatCaps & CKRST_FORMAT_CAPS_IMAGE_WRITE) == 0)
            return CKERR_NOTIMPLEMENTED;
    }
    CKDWORD fullMipCount = CKBgfxTextureMipCount(w, h, d);
    CKDWORD requestedMipCount = Desc->MipMapCount;
    CKBOOL openGL = CKBgfxIsOpenGLRenderer() ? TRUE : FALSE;
    CKBOOL requestedAutoMips = CKBgfxIsAutoMipRequest(
        requestedMipCount, fullMipCount);
    CKBOOL autoMipDataAvailable = (requestedAutoMips &&
                                   CKBgfxCanUseGeneratedMipChain(Desc->Flags, fmt, d, Data)) ? TRUE : FALSE;
    bool hasMips = CKBgfxShouldCreateTextureMipChain(
        requestedMipCount, fullMipCount, openGL, autoMipDataAvailable) == TRUE;
    bool uploadInitialAfterCreate = hasMips && autoMipDataAvailable && Data && Data->Image;

    uint64_t texFlags = CKBgfxTextureFlagsFromDescFlags(Desc->Flags);
    if (!bgfx::isTextureValid(d, cube != FALSE, 1, fmt, texFlags))
        return CKERR_NOTIMPLEMENTED;

    const bgfx::Memory *mem = NULL;
    CKDWORD copiedBytes = 0;
    if (Data && Data->Image && !uploadInitialAfterCreate)
    {
        CKDWORD bpp = (Data->BitsPerPixel > 0)
            ? Data->BitsPerPixel
            : (Desc->Format.BitsPerPixel > 0 ? Desc->Format.BitsPerPixel : 32);
        const CKBOOL compressed = pf == _DXT1 || pf == _DXT3 || pf == _DXT5;
        if (Data->Width != w || Data->Height != h)
            return CKERR_INVALIDPARAMETER;
        const CKBOOL packedResource = hasMips || compressed || cube || volume;
        if (packedResource) {
            bgfx::TextureInfo textureInfo;
            bgfx::calcTextureSize(textureInfo, w, h, d, cube != FALSE,
                                  hasMips, 1, fmt);
            if (textureInfo.storageSize == 0 || Data->TotalImageSize <= 0 ||
                static_cast<CKDWORD>(Data->TotalImageSize) < textureInfo.storageSize)
                return CKERR_INVALIDPARAMETER;
            if (!compressed) {
                if (bpp == 0 || (bpp % 8) != 0)
                    return CKERR_INVALIDPARAMETER;
                const CKDWORD rowBytes = static_cast<CKDWORD>(w) * (bpp / 8u);
                if (Data->BytesPerLine > 0 &&
                    static_cast<CKDWORD>(Data->BytesPerLine) != rowBytes)
                    return CKERR_INVALIDPARAMETER;
            }
            copiedBytes = textureInfo.storageSize;
            mem = bgfx::copy(Data->Image, copiedBytes);
        } else {
            if (bpp == 0 || (bpp % 8) != 0)
                return CKERR_INVALIDPARAMETER;
            const CKDWORD rowBytes = static_cast<CKDWORD>(w) * (bpp / 8u);
            const CKDWORD pitch = CKBgfxResolveImagePitch(
                w, h, bpp,
                Data->BytesPerLine > 0 ? static_cast<CKDWORD>(Data->BytesPerLine) : 0);
            if (rowBytes == 0 || pitch == 0)
                return CKERR_INVALIDPARAMETER;
            copiedBytes = rowBytes * h;
            if (pitch == rowBytes) {
                mem = bgfx::copy(Data->Image, copiedBytes);
            } else {
                mem = bgfx::alloc(copiedBytes);
                for (uint16_t row = 0; row < h; ++row)
                    memcpy(mem->data + row * rowBytes,
                           static_cast<const CKBYTE *>(Data->Image) + row * pitch,
                           rowBytes);
            }
        }
    }

    bgfx::TextureHandle handle;
    if (Desc->Flags & CKRST_TEXTURE_CUBEMAP)
    {
        handle = bgfx::createTextureCube(w, hasMips, 1, fmt, texFlags, mem);
    }
    else if ((Desc->Flags & CKRST_TEXTURE_VOLUMEMAP) && d > 1)
    {
        handle = bgfx::createTexture3D(w, h, d, hasMips, fmt, texFlags, mem);
    }
    else
    {
        handle = bgfx::createTexture2D(w, h, hasMips, 1, fmt, texFlags, mem);
    }

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD texture = AllocateSlot(m_Textures, m_CapsDesc.MaxTextures,
                                          m_ResourceTableMutex);
    if (texture == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxTextureRecord();
    rec->Handle = handle;
    rec->Flags = Desc->Flags;
    rec->Width = w;
    rec->Height = h;
    rec->Depth = d;
    rec->IsDepth = FALSE;
    rec->RequestedAutoMips = requestedAutoMips;
    rec->MipCount = hasMips ? fullMipCount : 1;
    rec->Format = fmt;
    rec->PixelFormat = pf;
    rec->BitsPerPixel = (Desc->Format.BitsPerPixel > 0) ? Desc->Format.BitsPerPixel :
                         ((Data && Data->BitsPerPixel > 0) ? Data->BitsPerPixel : 32);
    if (Data && Data->Image && !uploadInitialAfterCreate &&
        !(Desc->Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) &&
        Data->Width == (int)w && Data->Height == (int)h) {
        const CKDWORD initializedMips = hasMips ? rec->MipCount : 1;
        for (CKDWORD mip = 0; mip < initializedMips; ++mip)
            rec->ReadbackOrientation[mip] = CKBGFX_ORIENTATION_TOP_LEFT;
        if (hasMips && CKBgfxEnsureSamplerBaseHandle(rec)) {
            bgfx::TextureInfo baseInfo;
            bgfx::calcTextureSize(baseInfo, w, h, 1, false, false, 1, fmt);
            if (baseInfo.storageSize > 0 && baseInfo.storageSize <= copiedBytes) {
                const bgfx::Memory *baseMem =
                    bgfx::copy(Data->Image, baseInfo.storageSize);
                bgfx::updateTexture2D(rec->SamplerBaseHandle, 0, 0,
                                      0, 0, w, h, baseMem);
                rec->SamplerBaseValid = TRUE;
            }
        }
    }

    m_Textures[texture] = rec;
    TraceTextureMap((CKSTRING)"create", texture, rec);

    if (m_DebugLogTextures &&
        s_CreateTextureLogCount < 80) {
        CKBgfxLogf("CreateTexture",
                 "id=%u handle=%u size=%ux%u flags=0x%X pf=%d bgfxFmt=%d bpp=%u requestedMips=%u actualMips=%u autoMips=%u initBytes=%u initFirst=0x%08X initHash=0x%08X",
                 texture, rec->Handle.idx, rec->Width, rec->Height, Desc->Flags,
                 (int)pf, (int)fmt, rec->BitsPerPixel, requestedMipCount,
                 rec->MipCount, requestedAutoMips, copiedBytes,
                 FirstDword(Data ? Data->Image : nullptr, copiedBytes),
                 SampleBytesChecksum(Data ? Data->Image : nullptr, copiedBytes));
        s_CreateTextureLogCount++;
    }

    if (uploadInitialAfterCreate) {
        CKERROR uploadResult = UpdateTexture(texture, 0, 0, NULL, Data);
        if (uploadResult != CK_OK) {
            DeleteObject(texture, CKRST_OBJ_TEXTURE);
            return uploadResult;
        }
    }

    *OutTexture = texture;
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateShader(const CKShaderDesc *Desc,
                                               CKDWORD *OutShader)
{
    if (!OutShader)
        return CKERR_INVALIDPARAMETER;
    *OutShader = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (Desc->Stage != CKRST_SHADER_VERTEX && Desc->Stage != CKRST_SHADER_PIXEL &&
        Desc->Stage != CKRST_SHADER_COMPUTE)
        return CKERR_INVALIDPARAMETER;
    if (Desc->Stage == CKRST_SHADER_COMPUTE &&
        (m_CapsDesc.Features & CKRST_CAPS_COMPUTE) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (!Desc->Code || Desc->CodeSize == 0) {
        CKBgfxLogf("CreateShader",
                   "invalid shader blob shader=%u stage=%u code=%p size=%u",
                   0u,
                   Desc ? (unsigned)Desc->Stage : 0u,
                   Desc ? Desc->Code : NULL,
                   Desc ? Desc->CodeSize : 0u);
        return CKERR_INVALIDPARAMETER;
    }
    CKRasterizerTargetDesc target;
    if (GetTargetDesc(&target) != CK_OK ||
        Desc->Format != CKRST_SHADER_FORMAT_NATIVE ||
        Desc->Profile != target.ShaderProfile) {
        CKBgfxLogf("CreateShader",
                   "shader target mismatch shader=%u stage=%u descFormat=0x%08X descProfile=%s(0x%08X) targetFormat=0x%08X targetProfile=%s(0x%08X)",
                   0u,
                   (unsigned)Desc->Stage,
                   Desc->Format,
                   CKBgfxShaderProfileName(Desc->Profile),
                   Desc->Profile,
                    CKRST_SHADER_FORMAT_NATIVE,
                    CKBgfxShaderProfileName(target.ShaderProfile),
                    target.ShaderProfile);
        return CKERR_INVALIDPARAMETER;
    }

    const bgfx::Memory *mem = bgfx::copy(Desc->Code, Desc->CodeSize);
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    if (!bgfx::isValid(handle)) {
        CKBgfxLogf("CreateShader",
                   "bgfx::createShader failed shader=%u stage=%u profile=%s(0x%08X) size=%u",
                   0u,
                   (unsigned)Desc->Stage,
                   CKBgfxShaderProfileName(Desc->Profile),
                   Desc->Profile,
                   Desc->CodeSize);
        return CKERR_INVALIDPARAMETER;
    }

    auto *rec = new CKBgfxShaderRecord();
    rec->Handle = handle;
    rec->Stage = Desc->Stage;

    const CKDWORD shader = AllocateSlot(m_Shaders, m_CapsDesc.MaxShaders,
                                         m_ResourceTableMutex);
    if (shader == 0) {
        bgfx::destroy(handle);
        delete rec;
        return CKERR_OUTOFMEMORY;
    }
    m_Shaders[shader] = rec;
    *OutShader = shader;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateProgram(const CKProgramDesc *Desc,
                                                CKDWORD *OutProgram)
{
    if (!OutProgram)
        return CKERR_INVALIDPARAMETER;
    *OutProgram = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;

    const CKDWORD program = AllocateSlot(m_Programs, m_CapsDesc.MaxPrograms,
                                          m_ResourceTableMutex);
    if (program == 0)
        return CKERR_OUTOFMEMORY;

    bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;

    CKBgfxShaderRecord *vs = GetShader(Desc->VertexShader);
    if (vs && vs->Stage == CKRST_SHADER_COMPUTE)
    {
        if (Desc->PixelShader != 0 ||
            (m_CapsDesc.Features & CKRST_CAPS_COMPUTE) == 0)
            return CKERR_INVALIDPARAMETER;
        handle = bgfx::createProgram(vs->Handle, false);
        if (!bgfx::isValid(handle))
            return CKERR_INVALIDPARAMETER;

        if (Desc->ConsumeShaders)
        {
            bgfx::destroy(vs->Handle);
            delete vs;
            m_Shaders[Desc->VertexShader] = NULL;
        }
    }
    else
    {
        CKBgfxShaderRecord *ps = GetShader(Desc->PixelShader);
        if (!vs || !ps || vs->Stage != CKRST_SHADER_VERTEX ||
            ps->Stage != CKRST_SHADER_PIXEL) {
            CKBgfxLogf("CreateProgram", "missing shaders vs=%p(h=%u) ps=%p(h=%u) shadersSize=%d",
                       vs, Desc->VertexShader, ps, Desc->PixelShader, m_Shaders.Size());
            return CKERR_INVALIDPARAMETER;
        }

        handle = bgfx::createProgram(vs->Handle, ps->Handle, false);
        if (!bgfx::isValid(handle)) {
            CKBgfxLogf("CreateProgram", "bgfx::createProgram failed vs.idx=%u ps.idx=%u",
                       vs->Handle.idx, ps->Handle.idx);
            return CKERR_INVALIDPARAMETER;
        }

        if (Desc->ConsumeShaders)
        {
            bgfx::destroy(vs->Handle);
            delete vs;
            m_Shaders[Desc->VertexShader] = NULL;
            bgfx::destroy(ps->Handle);
            delete ps;
            m_Shaders[Desc->PixelShader] = NULL;
        }
    }

    auto *rec = new CKBgfxProgramRecord();
    rec->Handle = handle;
    rec->VertexShader = Desc->VertexShader;
    rec->PixelShader = Desc->PixelShader;

    m_Programs[program] = rec;
    *OutProgram = program;
    TraceProgramMap((CKSTRING)"create", program, rec);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateUniform(const CKUniformDesc *Desc,
                                                CKDWORD *OutUniform)
{
    if (!OutUniform)
        return CKERR_INVALIDPARAMETER;
    *OutUniform = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc || !Desc->Name || Desc->Count == 0)
        return CKERR_INVALIDPARAMETER;

    bgfx::UniformType::Enum bgfxType;
    if (!CKBgfxTryUniformType(Desc->Type, bgfxType))
        return CKERR_INVALIDPARAMETER;
    if (Desc->Count > UINT16_MAX)
        return CKERR_INVALIDPARAMETER;
    uint16_t num = (uint16_t)(Desc->Count > 0 ? Desc->Count : 1);
    bgfx::UniformHandle handle = bgfx::createUniform(Desc->Name, bgfxType, num);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD uniform = AllocateSlot(m_Uniforms, m_CapsDesc.MaxUniforms,
                                          m_ResourceTableMutex);
    if (uniform == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxUniformRecord();
    rec->Handle = handle;
    rec->Type = Desc->Type;
    rec->Count = Desc->Count;
    strncpy(rec->Name, Desc->Name, sizeof(rec->Name) - 1);
    rec->Name[sizeof(rec->Name) - 1] = '\0';

    m_Uniforms[uniform] = rec;
    *OutUniform = uniform;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateVertexLayout(const CKVertexLayoutDesc *Desc,
                                                     CKDWORD *OutLayout)
{
    if (!OutLayout)
        return CKERR_INVALIDPARAMETER;
    *OutLayout = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (CKRasterizerValidateVertexLayout(Desc) != CK_OK ||
        m_RendererType == bgfx::RendererType::Count)
        return CKERR_INVALIDPARAMETER;

    CKDWORD order[CKRST_ATTRIB_COUNT];
    bgfx::Attrib::Enum nativeAttribs[CKRST_ATTRIB_COUNT];
    bgfx::AttribType::Enum nativeTypes[CKRST_ATTRIB_COUNT];
    CKBOOL usedAttribs[CKRST_ATTRIB_COUNT] = {};
    for (CKDWORD i = 0; i < Desc->ElementCount; ++i)
    {
        const CKVertexElementDesc &elem = Desc->Elements[i];
        if ((CKDWORD)elem.Attrib >= CKRST_ATTRIB_COUNT ||
            usedAttribs[elem.Attrib] || elem.Count < 1 || elem.Count > 4 ||
            elem.Offset >= Desc->Stride ||
            !CKBgfxTryAttrib(elem.Attrib, nativeAttribs[i]) ||
            !CKBgfxTryAttribType(elem.Type, nativeTypes[i]))
            return CKERR_INVALIDPARAMETER;
        if (elem.Type == CKRST_ATTRIBTYPE_HALF &&
            (m_CapsDesc.Features & CKRST_CAPS_VERTEX_ATTRIB_HALF) == 0)
            return CKERR_NOTIMPLEMENTED;
        if (elem.Type == CKRST_ATTRIBTYPE_UINT10 &&
            (m_CapsDesc.Features & CKRST_CAPS_VERTEX_ATTRIB_UINT10) == 0)
            return CKERR_NOTIMPLEMENTED;
        usedAttribs[elem.Attrib] = TRUE;
        order[i] = i;
    }

    for (CKDWORD i = 1; i < Desc->ElementCount; ++i)
    {
        const CKDWORD value = order[i];
        CKDWORD j = i;
        while (j > 0 &&
               Desc->Elements[order[j - 1]].Offset > Desc->Elements[value].Offset)
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = value;
    }

    bgfx::VertexLayout bgfxLayout;
    bgfxLayout.begin(m_RendererType);
    CKDWORD cursor = 0;
    for (CKDWORD sortedIndex = 0; sortedIndex < Desc->ElementCount; ++sortedIndex)
    {
        const CKDWORD sourceIndex = order[sortedIndex];
        const CKVertexElementDesc &elem = Desc->Elements[sourceIndex];
        if (elem.Offset < cursor)
            return CKERR_INVALIDPARAMETER;
        CKDWORD gap = elem.Offset - cursor;
        while (gap > 0)
        {
            const uint8_t chunk = (uint8_t)XMin(gap, (CKDWORD)UINT8_MAX);
            bgfxLayout.skip(chunk);
            gap -= chunk;
        }
        bgfxLayout.add(nativeAttribs[sourceIndex], elem.Count,
                       nativeTypes[sourceIndex], elem.Normalized != FALSE,
                       elem.AsInt != FALSE);
        cursor = bgfxLayout.getStride();
        if (cursor > Desc->Stride)
            return CKERR_INVALIDPARAMETER;
    }

    CKDWORD tail = Desc->Stride - cursor;
    while (tail > 0)
    {
        const uint8_t chunk = (uint8_t)XMin(tail, (CKDWORD)UINT8_MAX);
        bgfxLayout.skip(chunk);
        tail -= chunk;
    }
    bgfxLayout.end();

    if (bgfxLayout.getStride() != Desc->Stride)
        return CKERR_INVALIDPARAMETER;
    for (CKDWORD i = 0; i < Desc->ElementCount; ++i)
    {
        const CKVertexElementDesc &elem = Desc->Elements[i];
        uint8_t count = 0;
        bgfx::AttribType::Enum type = bgfx::AttribType::Count;
        bool normalized = false;
        bool asInt = false;
        bgfxLayout.decode(nativeAttribs[i], count, type, normalized, asInt);
        if (bgfxLayout.getOffset(nativeAttribs[i]) != elem.Offset ||
            count != elem.Count || type != nativeTypes[i] ||
            normalized != (elem.Normalized != FALSE) ||
            asInt != (elem.AsInt != FALSE))
            return CKERR_INVALIDPARAMETER;
    }

    bgfx::VertexLayoutHandle handle = bgfx::createVertexLayout(bgfxLayout);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD layout = AllocateSlot(m_VertexLayouts,
                                         m_CapsDesc.MaxVertexLayouts,
                                         m_ResourceTableMutex);
    if (layout == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxVertexLayoutRecord();
    rec->Handle = handle;
    rec->Layout = bgfxLayout;

    m_VertexLayouts[layout] = rec;
    *OutLayout = layout;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateFrameBuffer(const CKFrameBufferDesc *Desc,
                                                    CKDWORD *OutFrameBuffer)
{
    if (!OutFrameBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutFrameBuffer = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_CAPS_FRAMEBUFFER) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->ColorCount > m_CapsDesc.MaxColorAttachments ||
        Desc->ColorCount > CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS ||
        (Desc->ColorCount != 0 && !Desc->Color))
        return CKERR_INVALIDPARAMETER;

    CKDWORD totalAttachments = Desc->ColorCount;
    CKBOOL hasDepth = (Desc->DepthStencil.Texture != 0);
    if (hasDepth) totalAttachments++;

    if (totalAttachments == 0 ||
        totalAttachments > CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS)
        return CKERR_INVALIDPARAMETER;

    bgfx::Attachment *attachments = new bgfx::Attachment[totalAttachments];
    CKDWORD idx = 0;

    for (CKDWORD i = 0; i < Desc->ColorCount; ++i)
    {
        CKBgfxTextureRecord *tex = GetTexture(Desc->Color[i].Texture);
        const CKBOOL cube = tex && (tex->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
        const CKBOOL volume = tex &&
            (tex->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && tex->Depth > 1;
        if (!tex || tex->IsDepth ||
            (tex->Flags & CKRST_TEXTURE_RENDERTARGET) == 0 ||
            Desc->Color[i].Mip >= tex->MipCount ||
            (!cube && !volume && Desc->Color[i].Layer != 0) ||
            (cube && Desc->Color[i].Layer >= 6) ||
            (volume && Desc->Color[i].Layer >=
                XMax((CKDWORD)1, tex->Depth >> Desc->Color[i].Mip)))
        {
            delete[] attachments;
            return CKERR_INVALIDPARAMETER;
        }
        attachments[idx].init(tex->Handle, bgfx::Access::Write,
                              (uint16_t)Desc->Color[i].Layer, 1,
                              (uint16_t)Desc->Color[i].Mip);
        idx++;
    }

    if (hasDepth)
    {
        CKBgfxTextureRecord *depthTex = GetTexture(Desc->DepthStencil.Texture);
        if (!depthTex || !depthTex->IsDepth ||
            Desc->DepthStencil.Mip >= depthTex->MipCount ||
            Desc->DepthStencil.Layer != 0)
        {
            delete[] attachments;
            return CKERR_INVALIDPARAMETER;
        }
        attachments[idx].init(depthTex->Handle, bgfx::Access::Write,
                              (uint16_t)Desc->DepthStencil.Layer, 1,
                              (uint16_t)Desc->DepthStencil.Mip);
        idx++;
    }

    if (!bgfx::isFrameBufferValid((uint8_t)totalAttachments, attachments)) {
        delete[] attachments;
        return CKERR_NOTIMPLEMENTED;
    }

    bgfx::FrameBufferHandle handle = bgfx::createFrameBuffer(
        (uint8_t)totalAttachments, attachments, false);
    delete[] attachments;

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD frameBuffer = AllocateSlot(m_FrameBuffers,
                                              m_CapsDesc.MaxFrameBuffers,
                                              m_ResourceTableMutex);
    if (frameBuffer == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxFrameBufferRecord();
    rec->Handle = handle;
    rec->ColorCount = Desc->ColorCount;
    for (CKDWORD i = 0; i < Desc->ColorCount; ++i)
        rec->Color[i] = Desc->Color[i];

    m_FrameBuffers[frameBuffer] = rec;
    *OutFrameBuffer = frameBuffer;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateDepthTexture(const CKDepthTextureDesc *Desc,
                                                     CKDWORD *OutTexture)
{
    if (!OutTexture)
        return CKERR_INVALIDPARAMETER;
    *OutTexture = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_CAPS_DEPTH_TEXTURE) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Width == 0 || Desc->Height == 0 ||
        Desc->Width > m_CapsDesc.MaxTextureSize ||
        Desc->Height > m_CapsDesc.MaxTextureSize)
        return CKERR_INVALIDPARAMETER;

    uint16_t w = (uint16_t)Desc->Width;
    uint16_t h = (uint16_t)Desc->Height;
    bgfx::TextureFormat::Enum fmt;
    if (!CKBgfxTryDepthFormat(Desc->DepthFormat, fmt))
        return CKERR_INVALIDPARAMETER;
    const CKDWORD formatCaps = CKBgfxMapFormatCaps(
        m_NativeFormatCaps[fmt], FALSE,
        (m_CapsDesc.Features & CKRST_CAPS_TEXTURE_COMPARISON) != 0);
    if ((formatCaps & CKRST_FORMAT_CAPS_FRAMEBUFFER) == 0)
        return CKERR_NOTIMPLEMENTED;
    const CKDWORD fullMipCount = CKBgfxTextureMipCount(w, h, 1);
    if (Desc->MipMapCount > fullMipCount)
        return CKERR_INVALIDPARAMETER;
    bool hasMips = Desc->MipMapCount > 1;

    uint64_t texFlags = BGFX_TEXTURE_RT_WRITE_ONLY;
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        w, h, hasMips, 1, fmt, texFlags, NULL);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD texture = AllocateSlot(m_Textures, m_CapsDesc.MaxTextures,
                                          m_ResourceTableMutex);
    if (texture == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxTextureRecord();
    rec->Handle = handle;
    rec->Flags = Desc->Flags | CKRST_TEXTURE_DEPTHSTENCIL;
    rec->Width = w;
    rec->Height = h;
    rec->Depth = 1;
    rec->IsDepth = TRUE;
    rec->RequestedAutoMips = FALSE;
    rec->MipCount = hasMips ? CKBgfxTextureMipCount(w, h, 1) : 1;
    rec->Format = fmt;
    rec->BitsPerPixel = 0;

    m_Textures[texture] = rec;
    *OutTexture = texture;
    TraceTextureMap((CKSTRING)"create", texture, rec);

    return CK_OK;
}

CKBOOL CKBgfxRasterizerContext::IsObjectAlive(CKDWORD Object, CKDWORD Type) const
{
    if (!m_BgfxInitialized || !m_Created || !IsApiThread() || Object == 0)
        return FALSE;
#define CKBGFX_SLOT_ALIVE(Array) \
    ((int)Object < (Array).Size() && (Array)[Object] != NULL ? TRUE : FALSE)
    switch (Type) {
    case CKRST_OBJ_TEXTURE:        return CKBGFX_SLOT_ALIVE(m_Textures);
    case CKRST_OBJ_VERTEXBUFFER:   return CKBGFX_SLOT_ALIVE(m_VertexBuffers);
    case CKRST_OBJ_INDEXBUFFER:    return CKBGFX_SLOT_ALIVE(m_IndexBuffers);
    case CKRST_OBJ_SHADER:         return CKBGFX_SLOT_ALIVE(m_Shaders);
    case CKRST_OBJ_PROGRAM:        return CKBGFX_SLOT_ALIVE(m_Programs);
    case CKRST_OBJ_UNIFORM:        return CKBGFX_SLOT_ALIVE(m_Uniforms);
    case CKRST_OBJ_VERTEXLAYOUT:   return CKBGFX_SLOT_ALIVE(m_VertexLayouts);
    case CKRST_OBJ_FRAMEBUFFER:    return CKBGFX_SLOT_ALIVE(m_FrameBuffers);
    case CKRST_OBJ_OCCLUSIONQUERY: return CKBGFX_SLOT_ALIVE(m_OcclusionQueries);
    case CKRST_OBJ_INDIRECTBUFFER: return CKBGFX_SLOT_ALIVE(m_IndirectBuffers);
    default:                       return FALSE;
    }
#undef CKBGFX_SLOT_ALIVE
}

CKERROR CKBgfxRasterizerContext::DeleteObject(CKDWORD Object, CKDWORD Type)
{
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (Object == 0)
        return CKERR_INVALIDPARAMETER;
    switch (Type) {
    case CKRST_OBJ_TEXTURE:
    case CKRST_OBJ_VERTEXBUFFER:
    case CKRST_OBJ_INDEXBUFFER:
    case CKRST_OBJ_SHADER:
    case CKRST_OBJ_PROGRAM:
    case CKRST_OBJ_UNIFORM:
    case CKRST_OBJ_VERTEXLAYOUT:
    case CKRST_OBJ_FRAMEBUFFER:
    case CKRST_OBJ_OCCLUSIONQUERY:
    case CKRST_OBJ_INDIRECTBUFFER:
        break;
    default:
        return CKERR_INVALIDPARAMETER;
    }

    if (Type & CKRST_OBJ_TEXTURE)
    {
        CKBgfxTextureRecord *rec = GetTexture(Object);
        if (rec) { TraceTextureMap((CKSTRING)"delete", Object, rec); DestroyRecord(rec); m_Textures[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_VERTEXBUFFER)
    {
        CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Object);
        if (rec) { TraceBufferMap((CKSTRING)"delete", (CKSTRING)"vb", Object, rec->Handle.idx, rec->Layout, rec->VertexSize, 0, 0, 0); DestroyRecord(rec); m_VertexBuffers[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_INDEXBUFFER)
    {
        CKBgfxIndexBufferRecord *rec = GetIndexBuffer(Object);
        if (rec) { TraceBufferMap((CKSTRING)"delete", (CKSTRING)"ib", Object, rec->Handle.idx, 0, rec->Index32 ? 4u : 2u, 0, rec->Index32 ? 1u : 0u, 0); DestroyRecord(rec); m_IndexBuffers[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_SHADER)
    {
        CKBgfxShaderRecord *rec = GetShader(Object);
        if (rec) { DestroyRecord(rec); m_Shaders[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_PROGRAM)
    {
        CKBgfxProgramRecord *rec = GetProgram(Object);
        if (rec) { TraceProgramMap((CKSTRING)"delete", Object, rec); DestroyRecord(rec); m_Programs[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_UNIFORM)
    {
        CKBgfxUniformRecord *rec = GetUniform(Object);
        if (rec) { DestroyRecord(rec); m_Uniforms[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_VERTEXLAYOUT)
    {
        CKBgfxVertexLayoutRecord *rec = GetVertexLayout(Object);
        if (rec) { DestroyRecord(rec); m_VertexLayouts[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_FRAMEBUFFER)
    {
        CKBgfxFrameBufferRecord *rec = GetFrameBuffer(Object);
        if (rec) { DestroyRecord(rec); m_FrameBuffers[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_OCCLUSIONQUERY)
    {
        CKBgfxOcclusionQueryRecord *rec = GetOcclusionQuery(Object);
        if (rec) { DestroyRecord(rec); m_OcclusionQueries[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_INDIRECTBUFFER)
    {
        CKBgfxIndirectBufferRecord *rec = GetIndirectBuffer(Object);
        if (rec) { DestroyRecord(rec); m_IndirectBuffers[Object] = NULL; return CK_OK; }
    }

    return CKERR_INVALIDPARAMETER;
}

CKERROR CKBgfxRasterizerContext::FlushObjects(CKDWORD TypeMask)
{
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (CKRasterizerValidateObjectMask(TypeMask) != CK_OK)
        return CKERR_INVALIDPARAMETER;
    for (CKDWORD i = 0; i < m_CapsDesc.MaxEncoders; ++i) {
        if (m_Encoders[i].m_Active.load(std::memory_order_acquire))
            return CKERR_INVALIDOPERATION;
    }
    if (TypeMask & CKRST_OBJ_FRAMEBUFFER)
        DestroyAllRecords(m_FrameBuffers);
    if (TypeMask & CKRST_OBJ_PROGRAM)
        DestroyAllRecords(m_Programs);
    if (TypeMask & CKRST_OBJ_TEXTURE)
        DestroyAllRecords(m_Textures);
    if (TypeMask & CKRST_OBJ_VERTEXBUFFER)
        DestroyAllRecords(m_VertexBuffers);
    if (TypeMask & CKRST_OBJ_INDEXBUFFER)
        DestroyAllRecords(m_IndexBuffers);
    if (TypeMask & CKRST_OBJ_SHADER)
        DestroyAllRecords(m_Shaders);
    if (TypeMask & CKRST_OBJ_UNIFORM)
        DestroyAllRecords(m_Uniforms);
    if (TypeMask & CKRST_OBJ_VERTEXLAYOUT)
        DestroyAllRecords(m_VertexLayouts);
    if (TypeMask & CKRST_OBJ_OCCLUSIONQUERY)
        DestroyAllRecords(m_OcclusionQueries);
    if (TypeMask & CKRST_OBJ_INDIRECTBUFFER)
        DestroyAllRecords(m_IndirectBuffers);
    return CK_OK;
}

// ---------------------------------------------------------------------------
// Resource update
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::UpdateVertexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                                     CKDWORD Size, const void *Data)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Data || Buffer == 0 || Size == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Buffer);
    if (!rec)
        return CKERR_INVALIDPARAMETER;
    if (rec->Flags & CKRST_VB_COMPUTE_WRITE)
        return CKERR_INVALIDOPERATION;

    if (rec->VertexSize == 0)
        return CKERR_INVALIDPARAMETER;
    if (Offset % rec->VertexSize != 0 || Size % rec->VertexSize != 0 ||
        Offset > rec->Size || Size > rec->Size - Offset)
        return CKERR_INVALIDPARAMETER;
    CKDWORD startVertex = Offset / rec->VertexSize;
    const bgfx::Memory *mem = bgfx::copy(Data, Size);
    bgfx::update(rec->Handle, startVertex, mem);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::UpdateIndexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                                    CKDWORD Size, const void *Data)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Data || Buffer == 0 || Size == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxIndexBufferRecord *rec = GetIndexBuffer(Buffer);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

    CKDWORD indexSize = rec->Index32 ? 4 : 2;
    if (Offset % indexSize != 0 || Size % indexSize != 0 ||
        Offset > rec->Size || Size > rec->Size - Offset)
        return CKERR_INVALIDPARAMETER;
    CKDWORD startIndex = (indexSize > 0) ? Offset / indexSize : 0;
    const bgfx::Memory *mem = bgfx::copy(Data, Size);
    bgfx::update(rec->Handle, startIndex, mem);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::UpdateTexture(CKDWORD Texture, CKDWORD Mip,
                                                CKDWORD Face, const CKRECT *Region,
                                                const VxImageDescEx *Data)
{
    static int s_UpdateTextureLogCount = 0;
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Data || !Data->Image)
        return CKERR_INVALIDPARAMETER;

    CKBgfxTextureRecord *rec = GetTexture(Texture);
    if (!rec)
        return CKERR_INVALIDPARAMETER;
    if (Mip >= rec->MipCount)
        return CKERR_INVALIDPARAMETER;
    if ((rec->Flags & CKRST_TEXTURE_READBACK) != 0)
        return CKERR_NOTIMPLEMENTED;

    bool isCube = (rec->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    bool isVolume = (rec->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && rec->Depth > 1;
    if (!isCube && !isVolume && Face != 0)
        return CKERR_INVALIDPARAMETER;
    if (isCube && Face >= 6)
        return CKERR_INVALIDPARAMETER;
    if (isVolume && Face >= XMax((CKDWORD)1, rec->Depth >> Mip))
        return CKERR_INVALIDPARAMETER;

    uint16_t x = 0, y = 0;
    const uint16_t mipWidth = (uint16_t)XMax((CKDWORD)1, rec->Width >> Mip);
    const uint16_t mipHeight = (uint16_t)XMax((CKDWORD)1, rec->Height >> Mip);
    uint16_t w = mipWidth;
    uint16_t h = mipHeight;
    if (Region)
    {
        if (Region->left < 0 || Region->top < 0 ||
            Region->right <= Region->left || Region->bottom <= Region->top ||
            Region->right > mipWidth || Region->bottom > mipHeight)
            return CKERR_INVALIDPARAMETER;
        x = (uint16_t)Region->left;
        y = (uint16_t)Region->top;
        w = (uint16_t)(Region->right - Region->left);
        h = (uint16_t)(Region->bottom - Region->top);
    }
    else if (Data->Width > 0 && Data->Height > 0)
    {
        if (Data->Width > mipWidth || Data->Height > mipHeight)
            return CKERR_INVALIDPARAMETER;
        w = (uint16_t)Data->Width;
        h = (uint16_t)Data->Height;
    }
    if (Data->Width != w || Data->Height != h ||
        VxImageDesc2PixelFormat(*Data) != rec->PixelFormat)
        return CKERR_INVALIDPARAMETER;
    const bool fullMipUpdate = x == 0 && y == 0 && w == mipWidth && h == mipHeight;

    bool compressed = CKBgfxIsCompressedTextureFormat(rec->Format);
    if (compressed &&
        ((x % 4) != 0 || (y % 4) != 0 ||
         ((w % 4) != 0 && x + w != mipWidth) ||
         ((h % 4) != 0 && y + h != mipHeight)))
        return CKERR_INVALIDPARAMETER;
    bool fullBase2DUpdate = Mip == 0 &&
                            Face == 0 &&
                            fullMipUpdate &&
                            !compressed &&
                            !isCube &&
                            !isVolume;
    bool canGenerateAutoMips = rec->RequestedAutoMips &&
                               fullBase2DUpdate &&
                               CKBgfxCanUseGeneratedMipChain(rec->Flags, rec->Format, rec->Depth, Data);

    CKBgfxAutoMipUpdateAction autoMipAction = CKBgfxResolveAutoMipUpdateAction(
        rec->RequestedAutoMips, rec->MipCount,
        fullBase2DUpdate ? TRUE : FALSE,
        canGenerateAutoMips ? TRUE : FALSE);
    if (Mip == 0 && Face == 0) {
        if (autoMipAction == CKBGFX_AUTOMIP_UPDATE_PROMOTE) {
            if (!CKBgfxRecreateTexture2D(rec, true)) {
                if (m_DebugLogTextures)
                    CKBgfxLogf("UpdateTexture",
                               "id=%u failed to recreate auto-mip texture with mips",
                               Texture);
                return CKERR_OUTOFMEMORY;
            }
        } else if (autoMipAction == CKBGFX_AUTOMIP_UPDATE_DEMOTE) {
            if (!CKBgfxRecreateTexture2D(rec, false)) {
                if (m_DebugLogTextures)
                    CKBgfxLogf("UpdateTexture",
                               "id=%u failed to recreate auto-mip texture without mips",
                               Texture);
                return CKERR_OUTOFMEMORY;
            }
            CKBgfxResetAutoMipBaseCache(rec);
        }
    }

    const bgfx::Memory *mem = NULL;
    const bool updateSamplerBase =
        Mip == 0 &&
        Face == 0 &&
        !isCube &&
        !isVolume &&
        (fullMipUpdate || rec->SamplerBaseValid) &&
        CKBgfxEnsureSamplerBaseHandle(rec);
    const bgfx::Memory *samplerBaseMem = NULL;
    if (compressed)
    {
        const CKDWORD blockSize = (rec->Format == bgfx::TextureFormat::BC1) ? 8 : 16;
        const CKDWORD blocksW = (w + 3) / 4;
        const CKDWORD blocksH = (h + 3) / 4;
        const CKDWORD imgSize = blocksW * blocksH * blockSize;
        if (Data->TotalImageSize > 0 &&
            static_cast<CKDWORD>(Data->TotalImageSize) < imgSize)
            return CKERR_INVALIDPARAMETER;
        mem = bgfx::copy(Data->Image, imgSize);
        if (updateSamplerBase)
            samplerBaseMem = bgfx::copy(Data->Image, imgSize);
    }
    else
    {
        CKDWORD bpp = rec->BitsPerPixel > 0 ? rec->BitsPerPixel : 32;
        CKDWORD rowBytes = CKBgfxImageRowBytes(w, bpp);
        CKDWORD pitch = CKBgfxResolveImagePitch(
            w, h, bpp,
            Data->BytesPerLine > 0 ? (CKDWORD)Data->BytesPerLine : 0);
        if (rowBytes == 0 || pitch == 0)
            return CKERR_INVALIDPARAMETER;
        if (m_DebugLogTextures &&
            s_UpdateTextureLogCount < 120) {
            uint32_t sampleSize = pitch * h;
            CKBgfxLogf("UpdateTexture",
                     "id=%u handle=%u mip=%u face=%u region=%ux%u+%u,%u rec=%ux%u recBpp=%u data=%dx%d dataBpp=%d pitch=%u rowBytes=%u first=0x%08X hash=0x%08X",
                     Texture, rec->Handle.idx, Mip, Face, w, h, x, y,
                     rec->Width, rec->Height, rec->BitsPerPixel,
                     Data->Width, Data->Height, Data->BitsPerPixel,
                     pitch, rowBytes,
                     FirstDword(Data->Image, sampleSize),
                     SampleBytesChecksum(Data->Image, sampleSize));
            s_UpdateTextureLogCount++;
        }
        if (pitch == rowBytes)
        {
            mem = bgfx::copy(Data->Image, rowBytes * h);
            if (updateSamplerBase)
                samplerBaseMem = bgfx::copy(Data->Image, rowBytes * h);
        }
        else
        {
            mem = bgfx::alloc(rowBytes * h);
            for (uint16_t row = 0; row < h; ++row)
                memcpy(mem->data + row * rowBytes, (CKBYTE *)Data->Image + row * pitch, rowBytes);
            if (updateSamplerBase) {
                samplerBaseMem = bgfx::alloc(rowBytes * h);
                for (uint16_t row = 0; row < h; ++row)
                    memcpy(samplerBaseMem->data + row * rowBytes, (CKBYTE *)Data->Image + row * pitch, rowBytes);
            }
        }
    }

    if (isCube)
        bgfx::updateTextureCube(rec->Handle, 0, (uint8_t)Face, (uint8_t)Mip, x, y, w, h, mem);
    else if (isVolume)
        bgfx::updateTexture3D(rec->Handle, (uint8_t)Mip, x, y, (uint16_t)Face, w, h, 1, mem);
    else
        bgfx::updateTexture2D(rec->Handle, (uint16_t)Face, (uint8_t)Mip, x, y, w, h, mem);

    if (Face == 0 && !isCube && !isVolume)
        RecordTextureWrite(rec, Mip, CKBGFX_ORIENTATION_TOP_LEFT,
                           fullMipUpdate ? TRUE : FALSE);

    if (samplerBaseMem) {
        bgfx::updateTexture2D(rec->SamplerBaseHandle, 0, 0, x, y, w, h, samplerBaseMem);
        if (fullMipUpdate)
            rec->SamplerBaseValid = TRUE;
    }

    if (rec->RequestedAutoMips &&
        Mip == 0 &&
        Face == 0 &&
        !compressed &&
        !isCube &&
        !isVolume &&
        rec->MipCount > 1) {
        const bool cacheValid = CKBgfxUpdateAutoMipBaseCache(rec, Data, x, y, w, h);
        if (fullBase2DUpdate && !cacheValid)
            return CKERR_OUTOFMEMORY;
        if (cacheValid) {
            if (!CKBgfxUpdateGeneratedMipMaps(rec, &rec->AutoMipBaseDesc)) {
                if (m_DebugLogTextures)
                    CKBgfxLogf("UpdateTexture",
                               "id=%u failed to generate complete auto-mip chain",
                               Texture);
                return CKERR_OUTOFMEMORY;
            }
            for (CKDWORD generatedMip = 1;
                 generatedMip < rec->MipCount &&
                 generatedMip < CKBGFX_MAX_TRACKED_MIPS;
                 ++generatedMip) {
                RecordTextureWrite(rec, generatedMip,
                                   CKBGFX_ORIENTATION_TOP_LEFT, TRUE);
            }
        }
    }

    return CK_OK;
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

static bool CKBgfxGetReadbackLayout(const CKBgfxTextureRecord *Record,
                                    CKDWORD Width, CKDWORD Height,
                                    CKDWORD &RowPitch, CKDWORD &RequiredSize)
{
    if (!Record || Width == 0 || Height == 0)
        return false;
    CKDWORD rows = Height;
    if (Record->Format == bgfx::TextureFormat::BC1 ||
        Record->Format == bgfx::TextureFormat::BC2 ||
        Record->Format == bgfx::TextureFormat::BC3) {
        const CKDWORD blockSize = Record->Format == bgfx::TextureFormat::BC1 ? 8u : 16u;
        RowPitch = XMax((CKDWORD)1, (Width + 3u) / 4u) * blockSize;
        rows = XMax((CKDWORD)1, (Height + 3u) / 4u);
    } else {
        if (Record->BitsPerPixel == 0 || (Record->BitsPerPixel % 8) != 0)
            return false;
        const uint64_t pitch = (uint64_t)Width * (Record->BitsPerPixel / 8u);
        if (pitch > UINT32_MAX)
            return false;
        RowPitch = (CKDWORD)pitch;
    }
    const uint64_t size = (uint64_t)RowPitch * rows;
    if (size > UINT32_MAX)
        return false;
    RequiredSize = (CKDWORD)size;
    return true;
}

CKERROR CKBgfxRasterizerContext::ReadTexture(CKDWORD Texture, CKDWORD Mip,
                                              CKReadbackDesc *Readback,
                                              CKDWORD *AvailableFrame)
{
    if (!m_BgfxInitialized || !m_Created ||
        VxThread::GetCurrentVxThreadId() != m_ApiThreadId)
        return CKERR_INVALIDOPERATION;
    if (!Readback || Readback->Size < sizeof(CKReadbackDesc))
        return CKERR_INVALIDPARAMETER;
    if (Readback->Data && !AvailableFrame)
        return CKERR_INVALIDPARAMETER;
    CKBgfxTextureRecord *rec = GetTexture(Texture);
    if (!rec)
        return CKERR_INVALIDPARAMETER;
    if (Mip >= rec->MipCount || Mip >= CKBGFX_MAX_TRACKED_MIPS)
        return CKERR_INVALIDPARAMETER;
    if (rec->IsDepth)
        return CKERR_NOTIMPLEMENTED;
    if (rec->PixelFormat == UNKNOWN_PF)
        return CKERR_INVALIDPARAMETER;
    if (!(rec->Flags & CKRST_TEXTURE_READBACK))
        return CKERR_INVALIDOPERATION;
    if ((rec->Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) != 0)
        return CKERR_NOTIMPLEMENTED;

    const CKDWORD width = XMax((CKDWORD)1, rec->Width >> Mip);
    const CKDWORD height = XMax((CKDWORD)1, rec->Height >> Mip);
    CKDWORD rowPitch = 0;
    CKDWORD requiredSize = 0;
    if (!CKBgfxGetReadbackLayout(rec, width, height, rowPitch, requiredSize))
        return CKERR_NOTIMPLEMENTED;

    CKBgfxTextureOrientation orientation;
    {
        VxMutexLock lock(m_ResourceStateMutex);
        orientation = (CKBgfxTextureOrientation)rec->ReadbackOrientation[Mip];
    }
    if (orientation == CKBGFX_ORIENTATION_UNKNOWN ||
        orientation == CKBGFX_ORIENTATION_MIXED)
        return CKERR_NOTIMPLEMENTED;
    const CKBOOL flipRows = orientation == CKBGFX_ORIENTATION_BOTTOM_LEFT;

    void *data = Readback->Data;
    const CKDWORD capacity = Readback->Capacity;
    *Readback = CKReadbackDesc();
    Readback->Data = data;
    Readback->Capacity = capacity;
    Readback->RequiredSize = requiredSize;
    Readback->RowPitch = rowPitch;
    Readback->Width = width;
    Readback->Height = height;
    Readback->Format = rec->PixelFormat;
    Readback->YFlip = flipRows;
    if (!data)
        return CK_OK;
    if (capacity < requiredSize)
        return CKERR_INVALIDPARAMETER;

    *AvailableFrame = bgfx::readTexture(rec->Handle, data, (uint8_t)Mip);
    return CK_OK;
}

// ---------------------------------------------------------------------------
// Render views
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::SetViewName(CKRenderView View, CKSTRING Name)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews || !Name)
        return CKERR_INVALIDPARAMETER;
    bgfx::setViewName((bgfx::ViewId)View, Name);
    if (m_DrawMapActive &&
        CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_VIEWS) &&
        View < CKRST_MAX_RENDER_VIEWS) {
        CKBgfxCopyDebugText(m_DebugViewName[View], sizeof(m_DebugViewName[View]), Name);
        CKBgfxLogf("ViewMap", "frame=%u view=%u name=%s",
                   m_DebugFrameId, (unsigned)View, m_DebugViewName[View]);
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewRect(CKRenderView View, const CKRECT &Rect)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews || Rect.left < 0 || Rect.top < 0 ||
        Rect.right <= Rect.left || Rect.bottom <= Rect.top ||
        Rect.right > UINT16_MAX || Rect.bottom > UINT16_MAX)
        return CKERR_INVALIDPARAMETER;
    bgfx::setViewRect((bgfx::ViewId)View,
                       (uint16_t)Rect.left, (uint16_t)Rect.top,
                       (uint16_t)(Rect.right - Rect.left),
                       (uint16_t)(Rect.bottom - Rect.top));
    {
        VxMutexLock lock(m_ResourceStateMutex);
        m_ViewRect[View] = Rect;
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewScissor(CKRenderView View, const CKRECT *Rect)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews)
        return CKERR_INVALIDPARAMETER;
    if (Rect) {
        if (Rect->left < 0 || Rect->top < 0 || Rect->right <= Rect->left ||
            Rect->bottom <= Rect->top || Rect->right > UINT16_MAX ||
            Rect->bottom > UINT16_MAX)
            return CKERR_INVALIDPARAMETER;
        bgfx::setViewScissor((bgfx::ViewId)View,
                              (uint16_t)Rect->left, (uint16_t)Rect->top,
                              (uint16_t)(Rect->right - Rect->left),
                              (uint16_t)(Rect->bottom - Rect->top));
    } else {
        bgfx::setViewScissor((bgfx::ViewId)View);
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewClear(CKRenderView View, CKDWORD Flags,
                                               CKDWORD Color, float Z,
                                               CKDWORD Stencil)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews ||
        (Flags & ~(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH |
                   CKRST_CTXCLEAR_STENCIL)) != 0 || Stencil > 0xff ||
        !(Z >= 0.0f && Z <= 1.0f))
        return CKERR_INVALIDPARAMETER;

    uint16_t bgfxClearFlags = 0;
    if (Flags & CKRST_CTXCLEAR_COLOR)   bgfxClearFlags |= BGFX_CLEAR_COLOR;
    if (Flags & CKRST_CTXCLEAR_DEPTH)   bgfxClearFlags |= BGFX_CLEAR_DEPTH;
    if (Flags & CKRST_CTXCLEAR_STENCIL) bgfxClearFlags |= BGFX_CLEAR_STENCIL;

    CKDWORD a = (Color >> 24) & 0xFF;
    CKDWORD r = (Color >> 16) & 0xFF;
    CKDWORD g = (Color >> 8) & 0xFF;
    CKDWORD b = (Color >> 0) & 0xFF;
    CKDWORD bgfxColor = (r << 24) | (g << 16) | (b << 8) | a;

    bgfx::setViewClear((bgfx::ViewId)View, bgfxClearFlags, bgfxColor, Z,
                        (uint8_t)Stencil);
    {
        VxMutexLock lock(m_ResourceStateMutex);
        m_ViewClearFlags[View] = Flags;
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewTransform(CKRenderView View,
                                                   const VxMatrix *ViewMatrix,
                                                   const VxMatrix *ProjMatrix)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews)
        return CKERR_INVALIDPARAMETER;
    bgfx::setViewTransform((bgfx::ViewId)View,
                            ViewMatrix ? (const void *)ViewMatrix : NULL,
                            ProjMatrix ? (const void *)ProjMatrix : NULL);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewFrameBuffer(CKRenderView View,
                                                     CKDWORD FrameBuffer)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews)
        return CKERR_INVALIDPARAMETER;
    if (FrameBuffer == 0)
    {
        bgfx::FrameBufferHandle invalid = BGFX_INVALID_HANDLE;
        bgfx::setViewFrameBuffer((bgfx::ViewId)View, invalid);
    }
    else
    {
        CKBgfxFrameBufferRecord *rec = GetFrameBuffer(FrameBuffer);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        bgfx::setViewFrameBuffer((bgfx::ViewId)View, rec->Handle);
    }
    {
        VxMutexLock lock(m_ResourceStateMutex);
        m_ViewFrameBuffer[View] = FrameBuffer;
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewMode(CKRenderView View, CK_VIEW_MODE Mode)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews)
        return CKERR_INVALIDPARAMETER;
    bgfx::ViewMode::Enum bgfxMode;
    switch (Mode) {
    case CKRST_VIEWMODE_DEFAULT:    bgfxMode = bgfx::ViewMode::Default; break;
    case CKRST_VIEWMODE_SEQUENTIAL: bgfxMode = bgfx::ViewMode::Sequential; break;
    case CKRST_VIEWMODE_DEPTH_ASC:  bgfxMode = bgfx::ViewMode::DepthAscending; break;
    case CKRST_VIEWMODE_DEPTH_DESC: bgfxMode = bgfx::ViewMode::DepthDescending; break;
    default:                        return CKERR_INVALIDPARAMETER;
    }
    bgfx::setViewMode((bgfx::ViewId)View, bgfxMode);
    const CKBOOL traceViews = m_DrawMapActive
        ? CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_VIEWS)
        : FALSE;
    if ((m_DrawMapSubmitActive || traceViews) && View < CKRST_MAX_RENDER_VIEWS) {
        m_DebugViewMode[View] = Mode;
        if (traceViews)
            CKBgfxLogf("ViewMap", "frame=%u view=%u mode=%s",
                       m_DebugFrameId, (unsigned)View, CKBgfxViewModeName(Mode));
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewOrder(CKRenderView Start, CKWORD Count,
                                               const CKRenderView *Order)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (Count == 0 || Start >= m_CapsDesc.MaxRenderViews ||
        (CKDWORD)Start + Count > m_CapsDesc.MaxRenderViews)
        return CKERR_INVALIDPARAMETER;
    if (Order) {
        for (CKWORD i = 0; i < Count; ++i) {
            if (Order[i] >= m_CapsDesc.MaxRenderViews)
                return CKERR_INVALIDPARAMETER;
        }
    }
    bgfx::setViewOrder((bgfx::ViewId)Start, Count, (const bgfx::ViewId *)Order);
    const CKBOOL traceViews = m_DrawMapActive
        ? CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_VIEWS)
        : FALSE;
    const CKBOOL traceFrame = m_DrawMapActive
        ? CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME)
        : FALSE;
    const CKBOOL traceSummary = m_DrawMapActive
        ? CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_SUMMARY)
        : FALSE;
    if (m_DrawMapSubmitActive || traceViews || traceFrame || traceSummary) {
        ++m_DebugViewOrderGeneration;
        m_DebugViewOrderSequential = TRUE;
        if (Order) {
            for (CKWORD i = 0; i < Count; ++i) {
                if (Order[i] != (CKRenderView)(Start + i)) {
                    m_DebugViewOrderSequential = FALSE;
                    break;
                }
            }
        }
        if (traceViews)
            CKBgfxLogf("ViewMap",
                       "frame=%u orderGen=%u start=%u count=%u sequential=%u orderPtr=%p",
                       m_DebugFrameId,
                       m_DebugViewOrderGeneration,
                       (unsigned)Start,
                       (unsigned)Count,
                       m_DebugViewOrderSequential ? 1u : 0u,
                       Order);
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::ResetView(CKRenderView View)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews)
        return CKERR_INVALIDPARAMETER;
    bgfx::resetView((bgfx::ViewId)View);
    {
        VxMutexLock lock(m_ResourceStateMutex);
        m_ViewFrameBuffer[View] = 0;
        m_ViewRect[View].left = 0;
        m_ViewRect[View].top = 0;
        m_ViewRect[View].right = 0;
        m_ViewRect[View].bottom = 0;
        m_ViewClearFlags[View] = 0;
        m_ViewClearRecorded[View] = FALSE;
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::TouchView(CKRenderView View)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (View >= m_CapsDesc.MaxRenderViews)
        return CKERR_INVALIDPARAMETER;
    RecordViewColorWrite(View, FALSE);
    bgfx::touch((bgfx::ViewId)View);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetAntialias(CKDWORD Samples)
{
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (Samples != 0 && Samples != 2 && Samples != 4 &&
        Samples != 8 && Samples != 16)
        return CKERR_INVALIDPARAMETER;
    if (m_AntialiasSamples == Samples)
        return CK_OK;

    m_AntialiasSamples = Samples;
    m_ResetFlags = CKBgfxBuildResetFlags(m_VSync, m_AntialiasSamples);
    bgfx::reset((uint32_t)m_Width, (uint32_t)m_Height, m_ResetFlags);
    return CK_OK;
}

// ---------------------------------------------------------------------------
// Transform cache
// ---------------------------------------------------------------------------

CKDWORD CKBgfxRasterizerContext::AllocTransform(VxMatrix *Transform, CKDWORD Count)
{
    if (!m_BgfxInitialized || !m_Created || !Transform || Count == 0)
        return CKRST_INVALID_TRANSFORM;

    CKDWORD expected = m_TransformCount.load(std::memory_order_relaxed);
    CKDWORD desired;
    do {
        if (expected + Count > CKRST_MAX_TRANSFORMS)
            return CKRST_INVALID_TRANSFORM;
        desired = expected + Count;
    } while (!m_TransformCount.compare_exchange_weak(
        expected, desired,
        std::memory_order_acq_rel, std::memory_order_relaxed));

    memcpy(&m_TransformCache[expected], Transform, Count * sizeof(VxMatrix));
    return expected;
}

// ---------------------------------------------------------------------------
// Transient buffers
// ---------------------------------------------------------------------------

CKBOOL CKBgfxRasterizerContext::AllocTransientVertexBuffer(
    CKTransientVertexBuffer *Buffer, CKDWORD VertexCount, CKDWORD Layout)
{
    if (!m_BgfxInitialized || !m_Created || !Buffer || VertexCount == 0)
        return FALSE;

    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return FALSE;

    VxMutexLock poolLock(m_TransientPoolMutex);

    const CKDWORD available = bgfx::getAvailTransientVertexBuffer(VertexCount, layoutRec->Layout);
    if (available < VertexCount) {
        RecordTransientAllocMiss("vertex", VertexCount, available);
        return FALSE;
    }

    CKDWORD slot = m_TransientVBCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= MAX_TRANSIENT_VB)
    {
        m_TransientVBCount.fetch_sub(1, std::memory_order_relaxed);
        RecordTransientAllocMiss("vertex-pool", VertexCount, MAX_TRANSIENT_VB);
        return FALSE;
    }

    bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[slot];
    bgfx::allocTransientVertexBuffer(tvb, VertexCount, layoutRec->Layout);
    if (tvb->data == NULL)
    {
        RecordTransientAllocMiss("vertex-alloc", VertexCount, available);
        return FALSE;
    }

    Buffer->Data = tvb->data;
    Buffer->Size = tvb->size;
    Buffer->StartVertex = tvb->startVertex;
    Buffer->VertexCount = VertexCount;
    Buffer->Stride = layoutRec->Layout.m_stride;
    Buffer->Layout = Layout;

    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::AllocTransientIndexBuffer(
    CKTransientIndexBuffer *Buffer, CKDWORD IndexCount, CKBOOL Index32)
{
    if (!m_BgfxInitialized || !m_Created || !Buffer || IndexCount == 0 ||
        (Index32 && (m_CapsDesc.Features & CKRST_CAPS_INDEX32) == 0))
        return FALSE;

    VxMutexLock poolLock(m_TransientPoolMutex);

    const CKDWORD available = bgfx::getAvailTransientIndexBuffer(IndexCount, Index32 ? true : false);
    if (available < IndexCount) {
        RecordTransientAllocMiss("index", IndexCount, available);
        return FALSE;
    }

    CKDWORD slot = m_TransientIBCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= MAX_TRANSIENT_IB)
    {
        m_TransientIBCount.fetch_sub(1, std::memory_order_relaxed);
        RecordTransientAllocMiss("index-pool", IndexCount, MAX_TRANSIENT_IB);
        return FALSE;
    }

    bgfx::TransientIndexBuffer *tib = &m_TransientIBPool[slot];
    bgfx::allocTransientIndexBuffer(tib, IndexCount, Index32 ? true : false);
    if (tib->data == NULL)
    {
        RecordTransientAllocMiss("index-alloc", IndexCount, available);
        return FALSE;
    }

    Buffer->Data = tib->data;
    Buffer->Size = tib->size;
    Buffer->StartIndex = tib->startIndex;
    Buffer->IndexCount = IndexCount;
    Buffer->Index32 = Index32;

    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::AllocTransientInstanceBuffer(
    CKTransientInstanceBuffer *Buffer, CKDWORD InstanceCount, CKDWORD Layout)
{
    if (!m_BgfxInitialized || !m_Created || !Buffer || InstanceCount == 0 ||
        (m_CapsDesc.Features & CKRST_CAPS_INSTANCING) == 0)
        return FALSE;

    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return FALSE;

    VxMutexLock poolLock(m_TransientPoolMutex);

    uint16_t stride = layoutRec->Layout.m_stride;
    if (stride % 16 != 0)
        stride = (uint16_t)((stride + 15) & ~15);

    const CKDWORD available = bgfx::getAvailInstanceDataBuffer(InstanceCount, stride);
    if (available < InstanceCount) {
        RecordTransientAllocMiss("instance", InstanceCount, available);
        return FALSE;
    }

    CKDWORD slot = m_TransientInstCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= MAX_TRANSIENT_INST)
    {
        m_TransientInstCount.fetch_sub(1, std::memory_order_relaxed);
        RecordTransientAllocMiss("instance-pool", InstanceCount, MAX_TRANSIENT_INST);
        return FALSE;
    }

    bgfx::InstanceDataBuffer *idb = &m_TransientInstPool[slot];
    bgfx::allocInstanceDataBuffer(idb, InstanceCount, stride);
    if (idb->data == NULL)
    {
        RecordTransientAllocMiss("instance-alloc", InstanceCount, available);
        return FALSE;
    }

    Buffer->Data = idb->data;
    Buffer->Size = idb->size;
    Buffer->StartInstance = 0;
    Buffer->InstanceCount = InstanceCount;
    Buffer->Stride = stride;
    Buffer->Layout = Layout;

    return TRUE;
}

CKDWORD CKBgfxRasterizerContext::GetAvailTransientVertexBuffer(
    CKDWORD VertexCount, CKDWORD Layout)
{
    if (!m_BgfxInitialized || !m_Created || VertexCount == 0)
        return 0;
    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return 0;
    return bgfx::getAvailTransientVertexBuffer(VertexCount, layoutRec->Layout);
}

CKDWORD CKBgfxRasterizerContext::GetAvailTransientIndexBuffer(
    CKDWORD IndexCount, CKBOOL Index32)
{
    if (!m_BgfxInitialized || !m_Created || IndexCount == 0 ||
        (Index32 && (m_CapsDesc.Features & CKRST_CAPS_INDEX32) == 0))
        return 0;
    return bgfx::getAvailTransientIndexBuffer(IndexCount, Index32 ? true : false);
}

CKDWORD CKBgfxRasterizerContext::GetAvailTransientInstanceBuffer(
    CKDWORD InstanceCount, CKDWORD Layout)
{
    if (!m_BgfxInitialized || !m_Created || InstanceCount == 0 ||
        (m_CapsDesc.Features & CKRST_CAPS_INSTANCING) == 0)
        return 0;
    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return 0;
    uint16_t stride = layoutRec->Layout.m_stride;
    if (stride % 16 != 0)
        stride = (uint16_t)((stride + 15) & ~15);
    return bgfx::getAvailInstanceDataBuffer(InstanceCount, stride);
}

// ---------------------------------------------------------------------------
// Encoder and frame
// ---------------------------------------------------------------------------

static void CKBgfxResetEncoderWrapper(CKBgfxEncoder &Encoder,
                                      CKBgfxRasterizerContext *Context,
                                      bgfx::Encoder *NativeEncoder,
                                      CKBOOL OwnsNativeEncoder)
{
    Encoder.m_Context = Context;
    Encoder.m_Encoder = NativeEncoder;
    Encoder.m_OwnsNativeEncoder = OwnsNativeEncoder;
    Encoder.m_Status = CK_OK;
    Encoder.m_OwnerThread = VxThread::GetCurrentVxThreadId();
    Encoder.m_StencilRef = 0;
    Encoder.m_StencilReadMask = 0xFF;
    Encoder.m_StencilWriteMask = 0xFF;
    Encoder.m_CurrentLayout = 0;
    Encoder.m_PointSize = 0;
    memset(&Encoder.m_CachedDrawState, 0, sizeof(CKDrawState));
    Encoder.m_CachedBgfxState = 0;
    Encoder.m_LastMarker[0] = '\0';
    memset(Encoder.m_DebugVertexBindings, 0, sizeof(Encoder.m_DebugVertexBindings));
    memset(Encoder.m_DebugTextureBindings, 0, sizeof(Encoder.m_DebugTextureBindings));
    Encoder.m_DebugVertexBindingMask = 0;
    Encoder.m_DebugTextureBindingMask = 0;
    Encoder.m_DebugIndexBuffer = 0;
    Encoder.m_DebugIndexStart = 0;
    Encoder.m_DebugIndexCount = 0;
    Encoder.m_DebugIndexHandle = 0;
    Encoder.m_DebugSpecializationHash = 0;
    Encoder.m_DebugSpecializationValid = FALSE;
}

CKRasterizerEncoder *CKBgfxRasterizerContext::BeginEncoder(CKBOOL ForceNewEncoder)
{
    if (!m_BgfxInitialized || !m_Created)
        return NULL;

    const CKBOOL useDefault = !ForceNewEncoder &&
                              VxThread::GetCurrentVxThreadId() == m_ApiThreadId;
    if (useDefault)
    {
        CKBOOL expected = FALSE;
        if (!m_DefaultEncoder.m_Active.compare_exchange_strong(
                expected, TRUE, std::memory_order_acq_rel,
                std::memory_order_relaxed))
            return NULL;
        CKBgfxResetEncoderWrapper(m_DefaultEncoder, this, NULL, FALSE);
        return &m_DefaultEncoder;
    }

#if !BGFX_CONFIG_MULTITHREADED
    return NULL;
#else
    for (CKDWORD i = 0; i < m_CapsDesc.MaxEncoders; ++i)
    {
        CKBOOL expected = FALSE;
        if (m_Encoders[i].m_Active.compare_exchange_strong(
                expected, TRUE, std::memory_order_acq_rel, std::memory_order_relaxed))
        {
            bgfx::Encoder *nativeEncoder = bgfx::begin(true);
            if (!nativeEncoder) {
                m_Encoders[i].m_Active.store(FALSE, std::memory_order_release);
                return NULL;
            }
            CKBgfxResetEncoderWrapper(m_Encoders[i], this, nativeEncoder, TRUE);
            return &m_Encoders[i];
        }
    }
    return NULL;
#endif
}

CKERROR CKBgfxRasterizerContext::EndEncoder(CKRasterizerEncoder *Encoder)
{
    if (!Encoder)
        return CKERR_INVALIDPARAMETER;

    CKBgfxEncoder *enc = NULL;
    if (Encoder == &m_DefaultEncoder) {
        enc = &m_DefaultEncoder;
    } else {
        for (int i = 0; i < CKRST_MAX_ENCODERS; ++i) {
            if (Encoder == &m_Encoders[i]) {
                enc = &m_Encoders[i];
                break;
            }
        }
    }

    if (!enc || !enc->m_Active.load(std::memory_order_acquire))
        return CKERR_INVALIDPARAMETER;
    if (enc->m_OwnerThread != VxThread::GetCurrentVxThreadId()) {
        enc->SetError(CKERR_INVALIDOPERATION);
        return enc->GetStatus();
    }

    if (m_DrawMapMarkerCaptureActive && enc->m_LastMarker[0] != '\0') {
        m_DebugMarkerStaleCount.fetch_add(1, std::memory_order_relaxed);
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS))
            CKBgfxLogf("MarkerStale",
                       "frame=%u reason=end_encoder label=\"%s\"",
                       m_DebugFrameId, enc->m_LastMarker);
        enc->m_LastMarker[0] = '\0';
    }

    if (enc->m_OwnsNativeEncoder && enc->m_Encoder) {
        bgfx::end(enc->m_Encoder);
        enc->m_Encoder = NULL;
    }

    const CKERROR status = enc->GetStatus();
    enc->m_Context = NULL;
    enc->m_OwnsNativeEncoder = FALSE;
    enc->m_Active.store(FALSE, std::memory_order_release);
    return status;
}

CKERROR CKBgfxRasterizerContext::Frame(CKRST_FRAME_SYNC_MODE SyncMode,
                                       CKDWORD Flags, CKDWORD *FrameNumber)
{
    if (!m_BgfxInitialized || !m_Created ||
        VxThread::GetCurrentVxThreadId() != m_ApiThreadId)
        return CKERR_INVALIDOPERATION;

    if (SyncMode != CKRST_FRAME_SYNC_IMMEDIATE &&
        SyncMode != CKRST_FRAME_SYNC_VSYNC &&
        SyncMode != CKRST_FRAME_SYNC_PRESERVE_PRESENT)
        return CKERR_INVALIDPARAMETER;
    if ((Flags & ~(CKRST_FRAME_CAPTURE | CKRST_FRAME_DISCARD |
                   CKRST_FRAME_FLUSH)) != 0)
        return CKERR_INVALIDPARAMETER;

    if (m_DefaultEncoder.m_Active.load(std::memory_order_acquire))
        return CKERR_INVALIDOPERATION;

    for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
    {
        if (m_Encoders[i].m_Active.load(std::memory_order_acquire))
        {
            const CKDWORD leak = m_DebugEncoderLeakCount.fetch_add(1, std::memory_order_relaxed);
            if (leak < 16 || CKBgfxLogEnabled("Config", false)) {
                CKBgfxLogf("Encoder",
                           "active encoder before frame frame=%u slot=%d",
                           m_DebugFrameId,
                           i);
            }
        }
    }

    const CKBOOL updatePresentSync = SyncMode != CKRST_FRAME_SYNC_PRESERVE_PRESENT;
    const CKBOOL vsync = SyncMode == CKRST_FRAME_SYNC_VSYNC;
    if (updatePresentSync && vsync != m_VSync)
    {
        m_VSync = vsync;
        m_ResetFlags = CKBgfxBuildResetFlags(vsync, m_AntialiasSamples);
        bgfx::reset((uint32_t)m_Width, (uint32_t)m_Height, m_ResetFlags);
    }

    static int s_PresentSyncLogCount = 0;
    if (m_DebugLogPresentSync && s_PresentSyncLogCount < 64) {
        CKBgfxLogf("PresentSync",
                 "frame=%u syncMode=%d currentVSync=%d resetFlags=0x%X",
                 m_DebugFrameId, SyncMode,
                 m_VSync ? 1 : 0, m_ResetFlags);
        ++s_PresentSyncLogCount;
    }

    DrawDebugOverlay();

    if (m_DrawMapActive &&
        CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME)) {
        CKBgfxLogf("FrameMap",
                   "End frame=%u submits=%u parsed=%u missingAnnotations=%u rawPrimitive=%u markerOverwrite=%u markerStale=%u invalidSubmit=%u orderSequential=%u",
                   m_DebugFrameId,
                   m_DebugSubmitSerial.load(std::memory_order_relaxed),
                   m_DebugParsedAnnotationCount.load(std::memory_order_relaxed),
                   m_DebugMissingAnnotationCount.load(std::memory_order_relaxed),
                   m_DebugRawPrimitiveCount.load(std::memory_order_relaxed),
                   m_DebugMarkerOverwriteCount.load(std::memory_order_relaxed),
                   m_DebugMarkerStaleCount.load(std::memory_order_relaxed),
                   m_DebugInvalidSubmitCount.load(std::memory_order_relaxed),
                   m_DebugViewOrderSequential ? 1u : 0u);
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_SUMMARY)) {
            CKBgfxLogf("FrameMap",
                       "Sources frame=%u Mesh=%u 2D=%u Sprite=%u Callback=%u RawPrimitive=%u",
                       m_DebugFrameId,
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_MESH].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_2D_ENTITY].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_SPRITE].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_CALLBACK].load(std::memory_order_relaxed),
                       m_DebugSourceSubmitCount[CKDRAW_SOURCE_RAW_PRIMITIVE].load(std::memory_order_relaxed));
            for (int i = 0; i < CKRST_MAX_RENDER_VIEWS; ++i) {
                CKDWORD viewSubmits = m_DebugViewSubmitSerial[i].load(std::memory_order_relaxed);
                if (viewSubmits != 0) {
                    CKBgfxLogf("ViewMap",
                               "frame=%u view=%u submits=%u mode=%s name=%s",
                               m_DebugFrameId,
                               (unsigned)i,
                               viewSubmits,
                               CKBgfxViewModeName(m_DebugViewMode[i]),
                               m_DebugViewName[i]);
                }
            }
        }
    }

    const CKDWORD submittedFrame = bgfx::frame((uint8_t)Flags);
    if (FrameNumber)
        *FrameNumber = submittedFrame;
    {
        VxMutexLock lock(m_ResourceStateMutex);
        for (int i = 0; i < CKRST_MAX_RENDER_VIEWS; ++i)
            m_ViewClearRecorded[i] = FALSE;
    }
    ++m_DebugFrameId;
    if (m_DrawMapActive) {
        m_DebugSubmitSerial.store(0, std::memory_order_relaxed);
        m_DebugMissingAnnotationCount.store(0, std::memory_order_relaxed);
        m_DebugMarkerOverwriteCount.store(0, std::memory_order_relaxed);
        m_DebugMarkerStaleCount.store(0, std::memory_order_relaxed);
        m_DebugInvalidSubmitCount.store(0, std::memory_order_relaxed);
        m_DebugParsedAnnotationCount.store(0, std::memory_order_relaxed);
        m_DebugRawPrimitiveCount.store(0, std::memory_order_relaxed);
        for (int i = 0; i < CKDRAW_SOURCE_COUNT; ++i)
            m_DebugSourceSubmitCount[i].store(0, std::memory_order_relaxed);
        for (int i = 0; i < CKRST_MAX_RENDER_VIEWS; ++i)
            m_DebugViewSubmitSerial[i].store(0, std::memory_order_relaxed);
    }

    if (m_DrawMapActive &&
        CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME))
        CKBgfxLogf("FrameMap", "Begin frame=%u", m_DebugFrameId);

    m_TransformCount.store(0, std::memory_order_relaxed);
    {
        VxMutexLock poolLock(m_TransientPoolMutex);
        m_TransientVBCount.store(0, std::memory_order_relaxed);
        m_TransientIBCount.store(0, std::memory_order_relaxed);
        m_TransientInstCount.store(0, std::memory_order_relaxed);
    }

    return CK_OK;
}

// ===========================================================================
// Resource creation - occlusion queries, indirect buffers
// ===========================================================================

CKERROR CKBgfxRasterizerContext::CreateOcclusionQuery(const CKOcclusionQueryDesc *Desc,
                                                       CKDWORD *OutQuery)
{
    if (!OutQuery)
        return CKERR_INVALIDPARAMETER;
    *OutQuery = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_CAPS_OCCLUSION_QUERY) == 0)
        return CKERR_NOTIMPLEMENTED;

    bgfx::OcclusionQueryHandle handle = bgfx::createOcclusionQuery();
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD query = AllocateSlot(m_OcclusionQueries,
                                        m_CapsDesc.MaxOcclusionQueries,
                                        m_ResourceTableMutex);
    if (query == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxOcclusionQueryRecord();
    rec->Handle = handle;

    m_OcclusionQueries[query] = rec;
    *OutQuery = query;
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateIndirectBuffer(const CKIndirectBufferDesc *Desc,
                                                       CKDWORD *OutBuffer)
{
    if (!OutBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutBuffer = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->MaxCommands == 0)
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_CAPS_DRAW_INDIRECT) == 0)
        return CKERR_NOTIMPLEMENTED;

    bgfx::IndirectBufferHandle handle = bgfx::createIndirectBuffer(Desc->MaxCommands);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    const CKDWORD buffer = AllocateSlot(m_IndirectBuffers, 0,
                                         m_ResourceTableMutex);
    if (buffer == 0) {
        bgfx::destroy(handle);
        return CKERR_OUTOFMEMORY;
    }

    auto *rec = new CKBgfxIndirectBufferRecord();
    rec->Handle = handle;
    rec->MaxCommands = Desc->MaxCommands;

    m_IndirectBuffers[buffer] = rec;
    *OutBuffer = buffer;
    return CK_OK;
}

// ===========================================================================
// Occlusion query results
// ===========================================================================

CK_OCCLUSION_RESULT CKBgfxRasterizerContext::GetOcclusionResult(CKDWORD Query,
                                                                 CKDWORD *PixelCount)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKRST_OCCLUSION_NORESULT;
    CKBgfxOcclusionQueryRecord *oq = GetOcclusionQuery(Query);
    if (!oq)
        return CKRST_OCCLUSION_NORESULT;

    int32_t numPixels = 0;
    bgfx::OcclusionQueryResult::Enum result = bgfx::getResult(oq->Handle, &numPixels);
    if (PixelCount)
        *PixelCount = (CKDWORD)numPixels;

    switch (result)
    {
    case bgfx::OcclusionQueryResult::Invisible: return CKRST_OCCLUSION_INVISIBLE;
    case bgfx::OcclusionQueryResult::Visible:   return CKRST_OCCLUSION_VISIBLE;
    default:                                     return CKRST_OCCLUSION_NORESULT;
    }
}

// ===========================================================================
// Palette, debug text, debug flags
// ===========================================================================

CKERROR CKBgfxRasterizerContext::SetPaletteColor(CKDWORD Index, CKDWORD RGBA)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (Index >= 16)
        return CKERR_INVALIDPARAMETER;
    bgfx::setPaletteColor((uint8_t)Index, RGBA);
    return CK_OK;
}

void CKBgfxRasterizerContext::DbgTextClear(CKDWORD Color, CKBOOL Small)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return;
    bgfx::dbgTextClear((uint8_t)Color, Small != FALSE);
}

void CKBgfxRasterizerContext::DbgTextPrintf(CKWORD X, CKWORD Y, CKDWORD Attr,
                                             CKSTRING Format, ...)
{
    if (!m_BgfxInitialized || !IsApiThread() || !Format)
        return;
    char buf[4096];
    va_list args;
    va_start(args, Format);
    vsnprintf(buf, sizeof(buf), Format, args);
    va_end(args);
    bgfx::dbgTextPrintf(X, Y, (uint8_t)Attr, "%s", buf);
}

void CKBgfxRasterizerContext::DbgTextImage(CKWORD X, CKWORD Y,
                                            CKWORD Width, CKWORD Height,
                                            const void *Data, CKWORD Pitch)
{
    if (!m_BgfxInitialized || !IsApiThread() || !Data)
        return;
    bgfx::dbgTextImage(X, Y, Width, Height, Data, Pitch);
}

void CKBgfxRasterizerContext::SetDebug(CKDWORD Flags)
{
    const CKBgfxDebugConfig &debug = CKBgfxDebugSettings();
    uint32_t bgfxFlags = debug.BgfxFlags;
    m_DebugFlags = Flags;
    m_DrawMapFlags = ((Flags & CKRST_DEBUG_DRAWMAP) != 0) ? Flags : 0;
    m_DrawMapActive = (m_DrawMapFlags & CKRST_DEBUG_DRAWMAP) != 0 ? TRUE : FALSE;
    m_DrawMapSubmitActive =
        CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_SUBMITS);
    m_DrawMapMarkerCaptureActive =
        (m_DrawMapSubmitActive ||
         CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS))
            ? TRUE : FALSE;
    if (!m_DrawMapMarkerCaptureActive) {
        for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
            m_Encoders[i].m_LastMarker[0] = '\0';
    }
    if (Flags & CKRST_DEBUG_WIREFRAME) bgfxFlags |= BGFX_DEBUG_WIREFRAME;
    if (Flags & CKRST_DEBUG_IFH)       bgfxFlags |= BGFX_DEBUG_IFH;
    if (Flags & CKRST_DEBUG_STATS)     bgfxFlags |= BGFX_DEBUG_STATS;
    if (Flags & CKRST_DEBUG_TEXT)      bgfxFlags |= BGFX_DEBUG_TEXT;
    if (Flags & CKRST_DEBUG_PROFILER)  bgfxFlags |= BGFX_DEBUG_PROFILER;
    m_DebugBgfxFlags = bgfxFlags;
    if (m_BgfxInitialized)
        bgfx::setDebug(bgfxFlags);
}

// ===========================================================================
// Statistics
// ===========================================================================

const CKRenderStats *CKBgfxRasterizerContext::GetStats()
{
    if (!m_BgfxInitialized || !IsApiThread())
        return &m_Stats;
    const bgfx::Stats *s = bgfx::getStats();
    if (!s)
        return &m_Stats;

    m_Stats.CpuTimeFrame = s->cpuTimeFrame;
    m_Stats.CpuTimerFreq = s->cpuTimerFreq;
    m_Stats.GpuTimeBegin = s->gpuTimeBegin;
    m_Stats.GpuTimeEnd = s->gpuTimeEnd;
    m_Stats.GpuTimerFreq = s->gpuTimerFreq;
    m_Stats.WaitRender = s->waitRender;
    m_Stats.WaitSubmit = s->waitSubmit;
    m_Stats.DrawCalls = (CKDWORD)s->numDraw;
    m_Stats.BlitCalls = (CKDWORD)s->numBlit;
    m_Stats.ComputeCalls = (CKDWORD)s->numCompute;
    m_Stats.MaxGpuLatency = (CKDWORD)s->maxGpuLatency;
    m_Stats.NumUpdatedVertexBuffers = (CKDWORD)s->numDynamicVertexBuffers;
    m_Stats.NumUpdatedIndexBuffers = (CKDWORD)s->numDynamicIndexBuffers;
    m_Stats.NumTransientVertexBuffers = (CKDWORD)s->transientVbUsed;
    m_Stats.NumTransientIndexBuffers = (CKDWORD)s->transientIbUsed;
    m_Stats.NumTransientInstanceBuffers = m_TransientInstCount.load(std::memory_order_relaxed);
    m_Stats.NumViews = (CKWORD)s->numViews;
    if (s->numViews > 0 && s->viewStats)
    {
        m_ViewStatsCache.Resize(s->numViews);
        for (uint16_t i = 0; i < s->numViews; ++i)
        {
            m_ViewStatsCache[i].Name = s->viewStats[i].name;
            m_ViewStatsCache[i].View = s->viewStats[i].view;
            m_ViewStatsCache[i].DrawCalls = 0;
            m_ViewStatsCache[i].CpuTimeBegin = s->viewStats[i].cpuTimeBegin;
            m_ViewStatsCache[i].CpuTimeEnd = s->viewStats[i].cpuTimeEnd;
            m_ViewStatsCache[i].GpuTimeBegin = s->viewStats[i].gpuTimeBegin;
            m_ViewStatsCache[i].GpuTimeEnd = s->viewStats[i].gpuTimeEnd;
        }
        m_Stats.ViewStats = &m_ViewStatsCache[0];
    }
    else
    {
        m_Stats.ViewStats = NULL;
    }
    m_Stats.GpuMemoryMax = (CKDWORD)(s->gpuMemoryMax >> 10);
    m_Stats.GpuMemoryUsed = (CKDWORD)(s->gpuMemoryUsed >> 10);
    m_Stats.Width = (CKDWORD)s->width;
    m_Stats.Height = (CKDWORD)s->height;
    m_Stats.TextWidth = (CKDWORD)s->textWidth;
    m_Stats.TextHeight = (CKDWORD)s->textHeight;

    return &m_Stats;
}

// ===========================================================================
// Resource naming
// ===========================================================================

void CKBgfxRasterizerContext::SetResourceName(CKDWORD Handle, CKDWORD Type,
                                               CKSTRING Name)
{
    if (!m_BgfxInitialized || !IsApiThread() || !Name)
        return;

    int32_t len = (int32_t)strlen(Name);
    switch (Type)
    {
    case CKRST_OBJ_SHADER:
        if (CKBgfxShaderRecord *r = GetShader(Handle))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_TEXTURE:
        if (CKBgfxTextureRecord *r = GetTexture(Handle))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_FRAMEBUFFER:
        if (CKBgfxFrameBufferRecord *r = GetFrameBuffer(Handle))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_VERTEXBUFFER:
    case CKRST_OBJ_INDEXBUFFER:
    case CKRST_OBJ_PROGRAM:
    case CKRST_OBJ_UNIFORM:
    case CKRST_OBJ_VERTEXLAYOUT:
    case CKRST_OBJ_OCCLUSIONQUERY:
    case CKRST_OBJ_INDIRECTBUFFER:
        break;
    default:
        break;
    }
}

// ===========================================================================
// Shader reflection
// ===========================================================================

CKDWORD CKBgfxRasterizerContext::FindUniformSlotByHandle(uint16_t BgfxIdx)
{
    for (int i = 0; i < m_Uniforms.Size(); ++i)
    {
        if (m_Uniforms[i] && m_Uniforms[i]->Handle.idx == BgfxIdx)
            return (CKDWORD)i;
    }
    return 0;
}

CKDWORD CKBgfxRasterizerContext::GetShaderUniforms(CKDWORD Shader,
                                                    CKDWORD *Uniforms,
                                                    CKDWORD MaxCount)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return 0;
    CKBgfxShaderRecord *rec = GetShader(Shader);
    if (!rec)
        return 0;

    uint16_t count = bgfx::getShaderUniforms(rec->Handle);

    if (Uniforms && MaxCount > 0)
    {
        uint16_t toQuery = (count < (uint16_t)MaxCount) ? count : (uint16_t)MaxCount;
        bgfx::UniformHandle *buf = new bgfx::UniformHandle[toQuery];
        bgfx::getShaderUniforms(rec->Handle, buf, toQuery);
        // Reflected uniforms are reported as CK slot handles, like every other
        // handle in this API. Uniforms with no CK slot (e.g. bgfx predefined
        // uniforms) map to the invalid handle 0.
        for (uint16_t i = 0; i < toQuery; ++i)
            Uniforms[i] = FindUniformSlotByHandle(buf[i].idx);
        delete[] buf;
    }
    return (CKDWORD)count;
}

void CKBgfxRasterizerContext::GetUniformInfo(CKDWORD Uniform, CKUniformInfo *Info)
{
    if (!Info)
        return;

    Info->Name[0] = '\0';
    Info->Type = CKRST_UNIFORM_VEC4;
    Info->Count = 0;

    CKBgfxUniformRecord *rec = GetUniform(Uniform);
    if (!rec)
        return;

    strncpy(Info->Name, rec->Name, sizeof(Info->Name) - 1);
    Info->Name[sizeof(Info->Name) - 1] = '\0';
    Info->Type = rec->Type;
    Info->Count = rec->Count > 0 ? rec->Count : 1;
}

// ===========================================================================
// Framebuffer queries
// ===========================================================================

CKDWORD CKBgfxRasterizerContext::GetFrameBufferTexture(CKDWORD FrameBuffer,
                                                        CKDWORD Attachment)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return 0;
    CKBgfxFrameBufferRecord *fb = GetFrameBuffer(FrameBuffer);
    if (!fb || Attachment >= fb->ColorCount)
        return 0;
    return IsObjectAlive(fb->Color[Attachment].Texture, CKRST_OBJ_TEXTURE)
        ? fb->Color[Attachment].Texture : 0;
}

// ===========================================================================
// Resource validation
// ===========================================================================

CKBOOL CKBgfxRasterizerContext::IsTextureValid(CKDWORD Depth, CKBOOL CubeMap,
                                                CKWORD NumLayers, CKDWORD Format,
                                                CKDWORD Flags)
{
    if (!m_BgfxInitialized || !IsApiThread() || Depth == 0 || NumLayers == 0)
        return FALSE;
    bgfx::TextureFormat::Enum nativeFormat;
    if (!CKBgfxTryTextureFormat(static_cast<VX_PIXELFORMAT>(Format), nativeFormat))
        return FALSE;
    uint64_t bgfxFlags = 0;
    if (Flags & CKRST_TEXTURE_RENDERTARGET)  bgfxFlags |= BGFX_TEXTURE_RT;
    if (Flags & CKRST_TEXTURE_BLIT_DST)      bgfxFlags |= BGFX_TEXTURE_BLIT_DST;
    if (Flags & CKRST_TEXTURE_COMPUTE_WRITE) bgfxFlags |= BGFX_TEXTURE_COMPUTE_WRITE;
    if (Flags & CKRST_TEXTURE_READBACK)      bgfxFlags |= BGFX_TEXTURE_READ_BACK;

    return bgfx::isTextureValid((uint16_t)Depth, CubeMap != FALSE, NumLayers,
                                nativeFormat, bgfxFlags)
           ? TRUE : FALSE;
}

CKBOOL CKBgfxRasterizerContext::IsFrameBufferValid(CKDWORD ColorCount,
                                                    const CKFrameBufferAttachmentDesc *Color,
                                                    const CKFrameBufferAttachmentDesc *DepthStencil)
{
    static const CKDWORD MAX_ATTACHMENTS = 16;
    if (!m_BgfxInitialized || !IsApiThread() || ColorCount > MAX_ATTACHMENTS ||
        (ColorCount != 0 && !Color))
        return FALSE;
    bgfx::Attachment attachments[MAX_ATTACHMENTS];
    CKDWORD count = 0;

    for (CKDWORD i = 0; i < ColorCount && count < MAX_ATTACHMENTS; ++i)
    {
        CKBgfxTextureRecord *tex = GetTexture(Color[i].Texture);
        if (!tex || tex->IsDepth || Color[i].Mip >= tex->MipCount ||
            Color[i].Layer != 0)
            return FALSE;
        attachments[count].init(tex->Handle, bgfx::Access::Write,
                                (uint16_t)Color[i].Layer, 1, (uint16_t)Color[i].Mip);
        ++count;
    }

    if (DepthStencil && DepthStencil->Texture != 0)
    {
        CKBgfxTextureRecord *tex = GetTexture(DepthStencil->Texture);
        if (!tex || !tex->IsDepth || DepthStencil->Mip >= tex->MipCount ||
            DepthStencil->Layer != 0)
            return FALSE;
        if (count < MAX_ATTACHMENTS)
        {
            attachments[count].init(tex->Handle, bgfx::Access::Write,
                                    (uint16_t)DepthStencil->Layer, 1,
                                    (uint16_t)DepthStencil->Mip);
            ++count;
        }
    }

    return bgfx::isFrameBufferValid((uint8_t)count, attachments) ? TRUE : FALSE;
}

// ===========================================================================
// Texture info
// ===========================================================================

void CKBgfxRasterizerContext::CalcTextureSize(CKTextureInfo *Info,
                                               CKWORD Width, CKWORD Height,
                                               CKWORD Depth, CKBOOL CubeMap,
                                               CKBOOL HasMips, CKWORD NumLayers,
                                               CKDWORD Format)
{
    if (!Info)
        return;

    memset(Info, 0, sizeof(*Info));
    if (!m_BgfxInitialized || !IsApiThread() || Width == 0 || Height == 0 ||
        Depth == 0 || NumLayers == 0)
        return;
    bgfx::TextureFormat::Enum nativeFormat;
    if (!CKBgfxTryTextureFormat(static_cast<VX_PIXELFORMAT>(Format), nativeFormat))
        return;

    bgfx::TextureInfo bgfxInfo;
    bgfx::calcTextureSize(bgfxInfo, Width, Height, Depth, CubeMap != FALSE,
                          HasMips != FALSE, NumLayers,
                          nativeFormat);

    Info->Format = Format;
    Info->StorageSize = bgfxInfo.storageSize;
    Info->Width = bgfxInfo.width;
    Info->Height = bgfxInfo.height;
    Info->Depth = bgfxInfo.depth;
    Info->NumMips = bgfxInfo.numMips;
    Info->BitsPerPixel = bgfxInfo.bitsPerPixel;
    Info->CubeMap = bgfxInfo.cubeMap ? TRUE : FALSE;
}

// ===========================================================================
// Screenshot capture
// ===========================================================================

CKERROR CKBgfxRasterizerContext::RequestScreenShot(CKDWORD FrameBuffer,
                                                    CKScreenShotCallback Callback,
                                                    void *UserData)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Callback || FrameBuffer != 0)
        return CKERR_INVALIDPARAMETER;

    XString requestPath;
    {
        VxMutexLock lock(m_ScreenShotMutex);
        CKBgfxScreenShotRequest pending = {};
        pending.FrameBuffer = FrameBuffer;
        pending.Callback = Callback;
        pending.UserData = UserData;
        char path[64];
        snprintf(path, sizeof(path), "ckrst-screenshot-%llu",
                 (unsigned long long)m_NextScreenShotToken++);
        pending.Path = path;
        requestPath = pending.Path;
        m_PendingScreenShots.PushBack(pending);
    }

    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, requestPath.CStr());
    return CK_OK;
}

// ===========================================================================
// Resource accessors
// ===========================================================================

CKBgfxOcclusionQueryRecord *CKBgfxRasterizerContext::GetOcclusionQuery(CKDWORD Handle)
{
    return GetSlot(m_OcclusionQueries, Handle, m_ResourceTableMutex);
}

CKBgfxIndirectBufferRecord *CKBgfxRasterizerContext::GetIndirectBuffer(CKDWORD Handle)
{
    return GetSlot(m_IndirectBuffers, Handle, m_ResourceTableMutex);
}

