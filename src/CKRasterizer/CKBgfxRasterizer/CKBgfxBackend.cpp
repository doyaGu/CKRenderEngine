#include "CKBgfxBackend.h"
#include "CKBgfxInternal.h"
#include "CKBgfxDrawMapTrace.h"
#include "CKRasterizerValidation.h"
#include "CKRasterizerCapsBaseline.h"
#include "CKDrawAnnotation.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

static_assert(BGFX_API_VERSION == 153, "Review CKBgfxRasterizer mappings before updating bgfx");

static VxMutex g_BgfxContextMutex;
static CKBgfxBackend *g_BgfxActiveContext = NULL;

static bool CKBgfxClaimActiveContext(CKBgfxBackend *Context)
{
    VxMutexLock lock(g_BgfxContextMutex);
    if (g_BgfxActiveContext && g_BgfxActiveContext != Context)
        return false;
    g_BgfxActiveContext = Context;
    return true;
}

static void CKBgfxReleaseActiveContext(CKBgfxBackend *Context)
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

static uint64_t CKBgfxTextureMSAAFlags(CKDWORD flags)
{
    switch (CKRSTTextureMSAASamples(flags)) {
    case 16: return BGFX_TEXTURE_RT_MSAA_X16;
    case 8:  return BGFX_TEXTURE_RT_MSAA_X8;
    case 4:  return BGFX_TEXTURE_RT_MSAA_X4;
    case 2:  return BGFX_TEXTURE_RT_MSAA_X2;
    default: return 0;
    }
}

static uint64_t CKBgfxTextureFlagsFromDescFlags(CKDWORD flags)
{
    uint64_t texFlags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE;
    if (flags & CKRST_TEXTURE_RENDERTARGET) {
        texFlags |= BGFX_TEXTURE_RT;
        const uint64_t msaa = CKBgfxTextureMSAAFlags(flags);
        if (msaa)
            texFlags = (texFlags & ~(uint64_t)BGFX_TEXTURE_RT) | msaa;
    }
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

static bool CKBgfxIsBumpLuminanceFormat(VX_PIXELFORMAT format)
{
    return format == _16_L6V5U5 || format == _32_X8L8V8U8;
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

static const CKDWORD CKBGFX_RESOURCE_SLOT_MASK = 0xffffu;

static CKDWORD EncodeResourceHandle(CKDWORD slot, CKWORD generation)
{
    return ((CKDWORD)generation << 16) | slot;
}

static CKDWORD ResourceHandleSlot(CKDWORD handle)
{
    return handle & CKBGFX_RESOURCE_SLOT_MASK;
}

static CKWORD ResourceHandleGeneration(CKDWORD handle)
{
    return (CKWORD)(handle >> 16);
}

template <typename T>
static CKDWORD AllocateSlot(XArray<CKBgfxResourceSlot<T> > &arr,
                            CKDWORD maxHandles,
                            VxMutex &tableMutex)
{
    VxMutexLock lock(tableMutex);
    if (arr.Size() == 0)
        arr.PushBack(CKBgfxResourceSlot<T>());
    for (int i = 1; i < arr.Size(); ++i) {
        CKBgfxResourceSlot<T> &slot = arr[i];
        if (!slot.Record && !slot.Retired) {
            if (slot.Generation == 0xffffu) {
                slot.Retired = TRUE;
                continue;
            }
            ++slot.Generation;
            return EncodeResourceHandle((CKDWORD)i, slot.Generation);
        }
    }
    const CKDWORD handleLimit = maxHandles != 0
        ? XMin(maxHandles, CKBGFX_RESOURCE_SLOT_MASK)
        : CKBGFX_RESOURCE_SLOT_MASK;
    if ((CKDWORD)(arr.Size() - 1) >= handleLimit)
        return 0;
    CKBgfxResourceSlot<T> slot;
    slot.Generation = 1;
    arr.PushBack(slot);
    return EncodeResourceHandle((CKDWORD)(arr.Size() - 1), slot.Generation);
}

template <typename T>
static T *GetSlot(XArray<CKBgfxResourceSlot<T> > &arr, CKDWORD handle,
                  VxMutex &tableMutex)
{
    VxMutexLock lock(tableMutex);
    const CKDWORD index = ResourceHandleSlot(handle);
    if (index == 0 || (int)index >= arr.Size())
        return NULL;
    CKBgfxResourceSlot<T> &slot = arr[index];
    return slot.Generation == ResourceHandleGeneration(handle)
        ? slot.Record : NULL;
}

template <typename T>
static CKBOOL StoreSlot(XArray<CKBgfxResourceSlot<T> > &arr,
                        CKDWORD handle, T *record,
                        VxMutex &tableMutex)
{
    VxMutexLock lock(tableMutex);
    const CKDWORD index = ResourceHandleSlot(handle);
    if (!record || index == 0 || (int)index >= arr.Size())
        return FALSE;
    CKBgfxResourceSlot<T> &slot = arr[index];
    if (slot.Record || slot.Generation != ResourceHandleGeneration(handle))
        return FALSE;
    slot.Record = record;
    return TRUE;
}

template <typename T>
static T *TakeSlot(XArray<CKBgfxResourceSlot<T> > &arr, CKDWORD handle,
                   VxMutex &tableMutex)
{
    VxMutexLock lock(tableMutex);
    const CKDWORD index = ResourceHandleSlot(handle);
    if (index == 0 || (int)index >= arr.Size())
        return NULL;
    CKBgfxResourceSlot<T> &slot = arr[index];
    if (slot.Generation != ResourceHandleGeneration(handle))
        return NULL;
    T *record = slot.Record;
    slot.Record = NULL;
    return record;
}

template <typename T>
static CKBOOL IsSlotAlive(const XArray<CKBgfxResourceSlot<T> > &arr,
                          CKDWORD handle, VxMutex &tableMutex)
{
    VxMutexLock lock(tableMutex);
    const CKDWORD index = ResourceHandleSlot(handle);
    if (index == 0 || (int)index >= arr.Size())
        return FALSE;
    const CKBgfxResourceSlot<T> &slot = arr[index];
    return slot.Record && slot.Generation == ResourceHandleGeneration(handle)
        ? TRUE : FALSE;
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
static void DestroyAllRecords(XArray<CKBgfxResourceSlot<RecordT> > &arr)
{
    for (int i = 0; i < arr.Size(); ++i) {
        DestroyRecord(arr[i].Record);
        arr[i].Record = NULL;
    }
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

// ===========================================================================
// CKBgfxBackend
// ===========================================================================

CKBgfxBackend::CKBgfxBackend(CKBgfxBackendDriver *driver)
    : m_Driver(driver), m_BgfxInitialized(FALSE), m_Created(FALSE), m_Window(NULL), m_PosX(0), m_PosY(0),
      m_Width(0), m_Height(0), m_Fullscreen(FALSE), m_RendererName("Unknown"),
      m_RendererType(bgfx::RendererType::Count), m_NativeSupported(0),
      m_DefaultWhiteTexture(BGFX_INVALID_HANDLE),
      m_VSync(FALSE), m_ResetFlags(BGFX_RESET_NONE), m_ApiThreadId(0),
      m_ShuttingDown{FALSE}, m_FatalError{CK_OK},
      m_FrameInProgress(FALSE), m_PassOpen(FALSE), m_CurrentView(0), m_NextView(0), m_LastFrameViewCount(0),
      m_FramePasses(0), m_FrameDraws(0), m_FrameBlits(0), m_FrameTextureUploads(0), m_FrameBufferUploads(0),
      m_CachedDrawState(), m_CachedBgfxState(0), m_PointSize(0), m_CurrentLayout(0),
      m_DebugVertexBindingMask(0), m_DebugTextureBindingMask(0),
      m_DebugIndexBuffer(0), m_DebugIndexStart(0), m_DebugIndexCount(0), m_DebugIndexHandle(0),
      m_DebugSpecializationHash(0), m_DebugSpecializationValid(FALSE), m_DrawErrorLogCount(0),
      m_DebugFrameId(0), m_DebugSubmitSerial{0}, m_DebugMissingAnnotationCount{0},
      m_DebugMarkerOverwriteCount{0}, m_DebugMarkerStaleCount{0},
      m_DebugInvalidSubmitCount{0}, m_DebugFatalCount{0},
      m_DebugParsedAnnotationCount{0}, m_DebugRawPrimitiveCount{0},
      m_DebugTransientAllocMissCount{0},
      m_DebugFlags(0), m_DrawMapFlags(0), m_DrawMapActive(FALSE),
      m_DrawMapSubmitActive(FALSE), m_DrawMapMarkerCaptureActive(FALSE),
      m_DebugBgfxFlags(0), m_DebugOverlay(FALSE), m_DebugLogPresentSync(FALSE),
      m_DebugLogTextureBindings(FALSE), m_DebugLogTextures(FALSE), m_DebugLogUniforms(FALSE),
      m_TransientVBCount(0), m_TransientIBCount(0)
{
    memset(m_NativeFormatCaps, 0, sizeof(m_NativeFormatCaps));
    memset(m_Slots, 0, sizeof(m_Slots));
    memset(m_DebugVertexBindings, 0, sizeof(m_DebugVertexBindings));
    memset(m_DebugTextureBindings, 0, sizeof(m_DebugTextureBindings));
    m_LastMarker[0] = '\0';
    for (int i = 0; i < CKRST_BLOCK_COUNT; ++i)
        m_BlockUniforms[i] = BGFX_INVALID_HANDLE;
    for (int i = 0; i < CKRST_BACKEND_SLOT_COUNT; ++i)
        m_SamplerUniforms[i] = BGFX_INVALID_HANDLE;
    m_BgfxCallback.SetContext(this);
    for (int i = 0; i < CKRST_MAX_RENDER_VIEWS; ++i) {
        m_DebugViewSubmitSerial[i].store(0, std::memory_order_relaxed);
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

CKBgfxBackend::~CKBgfxBackend()
{
    Shutdown();
}

void CKBgfxBackend::RecordTextureWrite(
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
void CKBgfxBackend::RecordTextureBlit(
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

void CKBgfxBackend::RecordViewColorWrite(CKRenderView View, CKBOOL HasDraw)
{
    if (View >= CKRST_MAX_RENDER_VIEWS)
        return;

    VxMutexLock lock(m_ResourceStateMutex);
    CKBgfxFrameBufferRecord *frameBuffer = GetFrameBuffer(m_ViewFrameBuffer[View]);
    if (!frameBuffer)
        return;

    const CKBgfxTextureOrientation targetOrientation =
        m_Caps.OriginBottomLeft
            ? CKBGFX_ORIENTATION_BOTTOM_LEFT
            : CKBGFX_ORIENTATION_TOP_LEFT;
    const CKBOOL executeColorClear =
        !m_ViewClearRecorded[View] &&
        (m_ViewClearFlags[View] & CKRST_CTXCLEAR_COLOR) != 0;

    CKBgfxTextureRecord *texture = GetTexture(frameBuffer->Desc.ColorTexture);
    const CKDWORD mip = frameBuffer->Desc.ColorMip;
    if (texture && mip < texture->MipCount && mip < CKBGFX_MAX_TRACKED_MIPS) {
        CKBgfxTextureOrientation current =
            (CKBgfxTextureOrientation)texture->ReadbackOrientation[mip];
        if (executeColorClear) {
            const CKDWORD width = XMax((CKDWORD)1, texture->Width >> mip);
            const CKDWORD height = XMax((CKDWORD)1, texture->Height >> mip);
            const CKBOOL fullClear =
                m_ViewRect[View].left == 0 && m_ViewRect[View].top == 0 &&
                m_ViewRect[View].right == (int)width &&
                m_ViewRect[View].bottom == (int)height;
            current = CKBgfxMergeOrientation(current, targetOrientation, fullClear);
        }
        if (HasDraw)
            current = CKBgfxMergeOrientation(current, targetOrientation, FALSE);
        texture->ReadbackOrientation[mip] = (CKBYTE)current;
    }
    if (executeColorClear)
        m_ViewClearRecorded[View] = TRUE;
}


// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

CKERROR CKBgfxBackend::Init(const CKBackendInitDesc *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (m_BgfxInitialized || m_Created)
        return CKERR_INVALIDOPERATION;
    m_FatalError.store(CK_OK, std::memory_order_release);
    m_ShuttingDown.store(FALSE, std::memory_order_release);
    if (!CKBgfxClaimActiveContext(this)) {
        CKBgfxLogf("Init", "another CKBgfxBackend is already active");
        return CKERR_INVALIDOPERATION;
    }

    WIN_HANDLE Window = Desc->Window;
    int Width = Desc->Width;
    int Height = Desc->Height;
    m_Window = Window;
    m_PosX = Desc->PosX;
    m_PosY = Desc->PosY;
    m_Fullscreen = Desc->Fullscreen;
    m_DebugFlags = Desc->DebugFlags;

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

    CKBgfxResolveFullscreenWindowSize(Window, m_Fullscreen, Width, Height);

    if (Width <= 0 || Height <= 0 || Width > 0xffff || Height > 0xffff) {
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }

    m_Width = (CKDWORD)Width;
    m_Height = (CKDWORD)Height;

    bgfx::RendererType::Enum requestedRenderer = bgfx::RendererType::Count;
    if (!CKBgfxParseRequestedRenderer(requestedRenderer)) {
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }

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
    if (requestedRenderer != bgfx::RendererType::Count &&
        actualRenderer != requestedRenderer) {
        CKBgfxLogf("Init",
                   "requested renderer %s is unavailable; bgfx selected %s",
                   CKBgfxRendererTypeName(requestedRenderer),
                   CKBgfxRendererTypeName(actualRenderer));
        bgfx::shutdown();
        m_BgfxInitialized = FALSE;
        CKBgfxReleaseActiveContext(this);
        return CKERR_NOTIMPLEMENTED;
    }
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
    m_Caps = CKBackendCaps();
    m_Caps.ShaderProfile = CKBgfxShaderProfile(actualRenderer);
    m_Caps.HomogeneousDepth = caps && caps->homogeneousDepth ? TRUE : FALSE;
    m_Caps.OriginBottomLeft = caps && caps->originBottomLeft ? TRUE : FALSE;
    m_CapsDesc = CKBackendDeviceLimits();
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
        if (actualRenderer == bgfx::RendererType::Vulkan &&
            m_CapsDesc.MaxRenderViews != 0)
            --m_CapsDesc.MaxRenderViews;
        m_CapsDesc.MaxFrameBuffers = limits.maxFrameBuffers;
        m_CapsDesc.MaxColorAttachments = limits.maxFBAttachments;
        m_CapsDesc.MaxPrograms = limits.maxPrograms;
        m_CapsDesc.MaxShaders = limits.maxShaders;
        m_CapsDesc.MaxTextures = limits.maxTextures;
        m_CapsDesc.MaxTextureStages = XMin((CKDWORD)limits.maxTextureSamplers,
                                           (CKDWORD)CKRST_MAX_TEXTURE_STAGES);
        m_CapsDesc.MaxTextureBindings = limits.maxTextureSamplers;
        m_CapsDesc.MaxVertexLayouts = limits.maxVertexLayouts;
        m_CapsDesc.MaxVertexStreams = XMin((CKDWORD)limits.maxVertexStreams,
                                           (CKDWORD)CKRST_MAX_VERTEX_STREAMS);
        m_CapsDesc.MaxIndexBuffers = limits.maxIndexBuffers;
        m_CapsDesc.MaxVertexBuffers = limits.maxVertexBuffers;
        m_CapsDesc.MaxDynamicIndexBuffers = limits.maxDynamicIndexBuffers;
        m_CapsDesc.MaxDynamicVertexBuffers = limits.maxDynamicVertexBuffers;
        m_CapsDesc.MaxUniforms = limits.maxUniforms;
        m_CapsDesc.MaxTransientVertexBufferSize = limits.maxTransientVbSize;
        m_CapsDesc.MaxTransientIndexBufferSize = limits.maxTransientIbSize;

        m_CapsDesc.Features = CKRST_DEVCAPS_VERTEX_SHADER |
                              CKRST_DEVCAPS_PIXEL_SHADER |
                              CKRST_DEVCAPS_RENDER_VIEWS |
                              CKRST_DEVCAPS_FRAMEBUFFER |
                              CKRST_DEVCAPS_TRANSIENT_BUFFERS |
                              CKRST_DEVCAPS_SCISSOR |
                              CKRST_DEVCAPS_BUFFER_UPDATE |
                              CKRST_DEVCAPS_TEXTURE_UPDATE |
                              CKRST_DEVCAPS_BLEND_EQUATION |
                              CKRST_DEVCAPS_TEXTURE_CUBE;
        if (caps->supported & BGFX_CAPS_TEXTURE_READ_BACK)
            m_CapsDesc.Features |= CKRST_DEVCAPS_TEXTURE_READBACK;
        if (caps->supported & BGFX_CAPS_TEXTURE_BLIT)
            m_CapsDesc.Features |= CKRST_DEVCAPS_BLIT;
        if (caps->supported & BGFX_CAPS_INDEX32)
            m_CapsDesc.Features |= CKRST_DEVCAPS_INDEX32;
        if (caps->supported & BGFX_CAPS_TEXTURE_COMPARE_ALL)
            m_CapsDesc.Features |= CKRST_DEVCAPS_TEXTURE_COMPARISON;
        if (caps->supported & BGFX_CAPS_TEXTURE_3D)
            m_CapsDesc.Features |= CKRST_DEVCAPS_TEXTURE_3D;
        if (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_HALF)
            m_CapsDesc.Features |= CKRST_DEVCAPS_VERTEX_ATTRIB_HALF;
        if (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_UINT10)
            m_CapsDesc.Features |= CKRST_DEVCAPS_VERTEX_ATTRIB_UINT10;

        const bgfx::TextureFormat::Enum depthFormats[] = {
            bgfx::TextureFormat::D16,
            bgfx::TextureFormat::D24,
            bgfx::TextureFormat::D24S8,
            bgfx::TextureFormat::D32F,
        };
        const uint64_t depthTextureCaps =
            BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER |
            BGFX_CAPS_FORMAT_TEXTURE_2D;
        for (CKDWORD i = 0; i < sizeof(depthFormats) / sizeof(depthFormats[0]); ++i) {
            if ((m_NativeFormatCaps[depthFormats[i]] & depthTextureCaps) == depthTextureCaps) {
                m_CapsDesc.Features |= CKRST_DEVCAPS_DEPTH_TEXTURE;
                break;
            }
        }
    }
    m_Caps.Features = m_CapsDesc.Features;
    m_Caps.MaxTextureSize = m_CapsDesc.MaxTextureSize;
    m_Caps.MaxTextureBindings = m_CapsDesc.MaxTextureBindings;
    m_Caps.MaxPasses = m_CapsDesc.MaxRenderViews;
    m_Caps.MaxMSAASamples = 16;   // MSAA targets are created on demand (CKRST_TEXTURE_MSAA_Xn)
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
    const CK_SHADER_PROFILE shaderProfile = m_Caps.ShaderProfile;
    if ((m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 || CKBgfxLogEnabled("Config", false)) {
        CKBgfxLogf("DrawMap",
                   "enabled=%u submits=%u resources=%u views=%u markers=%u frame=%u summary=%u renderer=%s profile=%s",
                   (m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 ? 1u : 0u,
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_SUBMITS),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_RESOURCES),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_VIEWS),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_MARKERS),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_FRAME),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_SUMMARY),
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
    if (!bgfx::isValid(m_DefaultWhiteTexture) || !CreateUniforms()) {
        CKBgfxLogf("Init", "failed to create the default texture or the constant block uniforms");
        ReleaseBgfx();
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

    if (m_Driver) {
        // Spec 4.9.2: the baseline bit fields stay as reported by the driver;
        // only numeric limits may be lowered to the real backend limits.
        Vx3DCapsDesc limits;
        memset(&limits, 0, sizeof(limits));
        limits.MaxTextureWidth = m_CapsDesc.MaxTextureSize;
        limits.MaxTextureHeight = m_CapsDesc.MaxTextureSize;
        limits.MaxTextureRatio = m_CapsDesc.MaxTextureSize;
        limits.MaxNumberTextureStage = m_CapsDesc.MaxTextureStages;
        limits.MaxNumberBlendStage = m_CapsDesc.MaxTextureStages;
        CKRSTLowerCapsToLimits(&m_Driver->m_3DCaps, &limits);

        m_Driver->m_TextureFormats.Clear();
        for (int format = _32_ARGB8888; format <= _32_X8L8V8U8; ++format) {
            CKTextureFormatCaps formatCaps;
            if (GetTextureFormatCaps((VX_PIXELFORMAT)format, &formatCaps) != CK_OK ||
                (formatCaps.Caps & CKRST_FORMAT_CAPS_TEXTURE_2D) == 0)
                continue;
            CKTextureDesc textureDesc;
            textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
            switch ((VX_PIXELFORMAT)format) {
            case _32_ARGB8888:
            case _16_ARGB1555:
            case _16_ARGB4444:
            case _32_ABGR8888:
            case _32_RGBA8888:
            case _32_BGRA8888:
            case _16_ABGR1555:
            case _16_ABGR4444:
            case _DXT1:
            case _DXT3:
            case _DXT5:
                textureDesc.Flags |= CKRST_TEXTURE_ALPHA;
                break;
            default:
                break;
            }
            VxPixelFormat2ImageDesc((VX_PIXELFORMAT)format, textureDesc.Format);
            m_Driver->m_TextureFormats.PushBack(textureDesc);
        }
        m_Driver->m_CapsUpToDate = TRUE;
    }

    return CK_OK;
}

CKBOOL CKBgfxBackend::CreateUniforms()
{
    for (int block = 0; block < CKRST_BLOCK_COUNT; ++block) {
        const CKBackendConstantBlockDesc &info = CKBackendConstantBlockInfo((CKBackendConstantBlock)block);
        m_BlockUniforms[block] = bgfx::createUniform(
            info.Name, info.Mat4 ? bgfx::UniformType::Mat4 : bgfx::UniformType::Vec4,
            (uint16_t)(info.Count > 0 ? info.Count : 1));
        if (!bgfx::isValid(m_BlockUniforms[block]))
            return FALSE;
    }
    for (CKDWORD slot = 0; slot < CKRST_BACKEND_SLOT_COUNT; ++slot) {
        m_SamplerUniforms[slot] = bgfx::createUniform(CKBackendSamplerSlotName(slot), bgfx::UniformType::Sampler, 1);
        if (!bgfx::isValid(m_SamplerUniforms[slot]))
            return FALSE;
    }
    return TRUE;
}

void CKBgfxBackend::DestroyUniforms()
{
    for (int block = 0; block < CKRST_BLOCK_COUNT; ++block) {
        if (bgfx::isValid(m_BlockUniforms[block]))
            bgfx::destroy(m_BlockUniforms[block]);
        m_BlockUniforms[block] = BGFX_INVALID_HANDLE;
    }
    for (CKDWORD slot = 0; slot < CKRST_BACKEND_SLOT_COUNT; ++slot) {
        if (bgfx::isValid(m_SamplerUniforms[slot]))
            bgfx::destroy(m_SamplerUniforms[slot]);
        m_SamplerUniforms[slot] = BGFX_INVALID_HANDLE;
    }
}

// Releases every bgfx object and shuts bgfx down.
void CKBgfxBackend::ReleaseBgfx()
{
    if (!m_BgfxInitialized)
        return;
    if (bgfx::isValid(m_DefaultWhiteTexture)) {
        bgfx::destroy(m_DefaultWhiteTexture);
        m_DefaultWhiteTexture = BGFX_INVALID_HANDLE;
    }
    DestroyUniforms();
    DestroyAllRecords(m_FrameBuffers);
    DestroyAllRecords(m_Textures);
    DestroyAllRecords(m_Programs);
    DestroyAllRecords(m_Shaders);
    DestroyAllRecords(m_VertexLayouts);
    DestroyAllRecords(m_VertexBuffers);
    DestroyAllRecords(m_IndexBuffers);
    bgfx::shutdown();
    m_BgfxInitialized = FALSE;
    m_Created = FALSE;
    m_RendererType = bgfx::RendererType::Count;
    CKBgfxReleaseActiveContext(this);
}

void CKBgfxBackend::Shutdown()
{
    if (!m_BgfxInitialized)
        return;
    m_ShuttingDown.store(TRUE, std::memory_order_release);
    m_FrameInProgress = FALSE;
    m_PassOpen = FALSE;
    m_NextView = 0;
    m_LastFrameViewCount = 0;
    m_TransientVBCount = 0;
    m_TransientIBCount = 0;
    ReleaseBgfx();
    CKBgfxCloseLogFile();
}

CKERROR CKBgfxBackend::Resize(int PosX, int PosY, int Width, int Height)
{
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (Width <= 0 || Height <= 0 || Width > 0xffff || Height > 0xffff)
        return CKERR_INVALIDPARAMETER;
    if (m_FrameInProgress)
        return CKERR_INVALIDOPERATION;
    CKBgfxResolveFullscreenWindowSize(m_Window, m_Fullscreen, Width, Height);
    if (Width <= 0 || Height <= 0 || Width > 0xffff || Height > 0xffff)
        return CKERR_INVALIDPARAMETER;

    m_PosX = PosX;
    m_PosY = PosY;
    m_Width = (CKDWORD)Width;
    m_Height = (CKDWORD)Height;

    bgfx::reset((uint32_t)Width, (uint32_t)Height, m_ResetFlags);
    bgfx::setViewRect(0, 0, 0, (uint16_t)Width, (uint16_t)Height);

    return CK_OK;
}

CKBOOL CKBgfxBackend::IsIdle() const
{
    return !m_FrameInProgress ? TRUE : FALSE;
}

CKERROR CKBgfxBackend::GetDeviceStatus() const
{
    if (!m_BgfxInitialized || !m_Created)
        return CKERR_INVALIDRENDERCONTEXT;
    if (m_ShuttingDown.load(std::memory_order_acquire))
        return CKERR_INVALIDOPERATION;
    return m_FatalError.load(std::memory_order_acquire);
}

void CKBgfxBackend::LatchFatalError(CKERROR Error)
{
    if (Error == CK_OK)
        return;
    CKERROR expected = CK_OK;
    m_FatalError.compare_exchange_strong(
        expected, Error, std::memory_order_acq_rel,
        std::memory_order_relaxed);
}

CKERROR CKBgfxBackend::GetTextureFormatCaps(VX_PIXELFORMAT Format,
                                                       CKTextureFormatCaps *Caps) const
{
    if (!Caps || Caps->Size < sizeof(CKTextureFormatCaps))
        return CKERR_INVALIDPARAMETER;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    bgfx::TextureFormat::Enum nativeFormat;
    if (!CKBgfxTryTextureStorageFormat(Format, nativeFormat))
        return CKERR_INVALIDPARAMETER;
    *Caps = CKTextureFormatCaps();
    Caps->Format = Format;
    const CKBOOL allowReadback =
        (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_READBACK) != 0 &&
        CKBgfxCanExposeReadback(Format, nativeFormat)
            ? TRUE : FALSE;
    Caps->Caps = CKBgfxMapFormatCaps(
        m_NativeFormatCaps[nativeFormat], allowReadback, FALSE);
    if (CKBgfxIsBumpLuminanceFormat(Format))
        Caps->Caps &= CKRST_FORMAT_CAPS_TEXTURE_2D;
    return CK_OK;
}
void CKBgfxBackend::ConfigureDebug()
{
    const CKBgfxDebugConfig &debug = CKBgfxDebugSettings();
    m_DebugOverlay = debug.Overlay ? TRUE : FALSE;
    m_DebugLogPresentSync = debug.Log.PresentSync ? TRUE : FALSE;
    m_DebugLogTextureBindings = debug.Log.TextureBindings ? TRUE : FALSE;
    m_DebugLogTextures = debug.Log.Textures ? TRUE : FALSE;
    m_DebugLogUniforms = debug.Log.Uniforms ? TRUE : FALSE;
    SetDebugFlags(m_DebugFlags);

    if (debug.Log.Config ||
        m_DebugBgfxFlags != BGFX_DEBUG_NONE ||
        m_DebugOverlay) {
        CKBgfxLogf("Debug", "configured bgfxFlags=0x%X overlay=%d",
                 m_DebugBgfxFlags, m_DebugOverlay ? 1 : 0);
    }
}
void CKBgfxBackend::DrawDebugOverlay()
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

CKBgfxShaderRecord *CKBgfxBackend::GetShader(CKDWORD Handle)
{
    return GetSlot(m_Shaders, Handle, m_ResourceTableMutex);
}
CKBgfxProgramRecord *CKBgfxBackend::GetProgram(CKDWORD Handle)
{
    return GetSlot(m_Programs, Handle, m_ResourceTableMutex);
}
CKBgfxVertexLayoutRecord *CKBgfxBackend::GetVertexLayout(CKDWORD Handle)
{
    return GetSlot(m_VertexLayouts, Handle, m_ResourceTableMutex);
}
CKBgfxVertexBufferRecord *CKBgfxBackend::GetVertexBuffer(CKDWORD Handle)
{
    return GetSlot(m_VertexBuffers, Handle, m_ResourceTableMutex);
}
CKBgfxIndexBufferRecord *CKBgfxBackend::GetIndexBuffer(CKDWORD Handle)
{
    return GetSlot(m_IndexBuffers, Handle, m_ResourceTableMutex);
}
CKBgfxTextureRecord *CKBgfxBackend::GetTexture(CKDWORD Handle)
{
    return GetSlot(m_Textures, Handle, m_ResourceTableMutex);
}
CKBgfxFrameBufferRecord *CKBgfxBackend::GetFrameBuffer(CKDWORD Handle)
{
    return GetSlot(m_FrameBuffers, Handle, m_ResourceTableMutex);
}
void CKBgfxBackend::TraceTextureMap(CKSTRING Event, CKDWORD Texture,
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
void CKBgfxBackend::TraceProgramMap(CKSTRING Event, CKDWORD Program,
                                              const CKBgfxProgramRecord *Record)
{
    char spec[160];
    const CK_SHADER_PROFILE profile = m_Caps.ShaderProfile;
    if (!m_DrawMapActive ||
        !CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_RESOURCES))
        return;
    CKBgfxDrawMapProgramTrace trace;
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
void CKBgfxBackend::TraceBufferMap(CKSTRING Event, CKSTRING Kind,
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
void CKBgfxBackend::RecordInvalidSubmit(CKSTRING Kind, CKRenderView View,
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
void CKBgfxBackend::RecordTransientAllocMiss(const char *Kind,
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
// Resources
// ---------------------------------------------------------------------------

CKERROR CKBgfxBackend::CreateTexture(const CKTextureDesc *Desc,
                                                const VxImageDescEx *Data,
                                                CKDWORD *OutTexture)
{
    static int s_CreateTextureLogCount = 0;
    if (!OutTexture)
        return CKERR_INVALIDPARAMETER;
    *OutTexture = 0;
    if (!IsReady())
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
    if (!CKBgfxTryTextureStorageFormat(pf, fmt))
        return CKERR_INVALIDPARAMETER;
    const CKBOOL bumpLuminance = CKBgfxIsBumpLuminanceFormat(pf) ? TRUE : FALSE;
    if (bumpLuminance &&
        (cubeRequested || volumeRequested ||
         (Desc->Flags & (CKRST_TEXTURE_RENDERTARGET |
                         CKRST_TEXTURE_READBACK |
                         CKRST_TEXTURE_BLIT_DST |
                         CKRST_TEXTURE_COMPUTE_WRITE)) != 0)) {
        return CKERR_NOTIMPLEMENTED;
    }
    const CKBOOL allowReadback =
        (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_READBACK) != 0 &&
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
    if (cube && (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_CUBE) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (volume && (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_3D) == 0)
        return CKERR_NOTIMPLEMENTED;
    if ((Desc->Flags & CKRST_TEXTURE_RENDERTARGET) &&
        (formatCaps & CKRST_FORMAT_CAPS_FRAMEBUFFER) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Flags & CKRST_TEXTURE_READBACK) {
        if (cube || volume ||
            (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_READBACK) == 0 ||
            (formatCaps & CKRST_FORMAT_CAPS_READBACK) == 0)
            return CKERR_NOTIMPLEMENTED;
    }
    if ((Desc->Flags & CKRST_TEXTURE_BLIT_DST) &&
        (m_CapsDesc.Features & CKRST_DEVCAPS_BLIT) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Flags & CKRST_TEXTURE_COMPUTE_WRITE) {
        if ((m_CapsDesc.Features & CKRST_DEVCAPS_IMAGE_RW) == 0 ||
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
        if (bumpLuminance) {
            const CKDWORD sourceBytes = pf == _16_L6V5U5 ? 2u : 4u;
            if (hasMips) {
                if (Data->TotalImageSize <= 0) {
                    return CKERR_INVALIDPARAMETER;
                }
                bgfx::TextureInfo textureInfo;
                bgfx::calcTextureSize(textureInfo, w, h, 1, false, true, 1, fmt);
                copiedBytes = textureInfo.storageSize;
                if (copiedBytes == 0)
                    return CKERR_INVALIDPARAMETER;
                CKBYTE *converted = (CKBYTE *)VxMalloc(copiedBytes);
                if (!converted)
                    return CKERR_OUTOFMEMORY;
                if (!CKBgfxConvertBumpLuminanceMipChain(
                        pf, Data->Image, (CKDWORD)Data->TotalImageSize,
                        w, h, fullMipCount, converted, copiedBytes)) {
                    VxFree(converted);
                    return CKERR_INVALIDPARAMETER;
                }
                mem = bgfx::copy(converted, copiedBytes);
                VxFree(converted);
            } else {
                const CKDWORD sourcePitch = Data->BytesPerLine > 0
                    ? (CKDWORD)Data->BytesPerLine : (CKDWORD)w * sourceBytes;
                if (sourcePitch < (CKDWORD)w * sourceBytes)
                    return CKERR_INVALIDPARAMETER;
                copiedBytes = (CKDWORD)w * (CKDWORD)h * 4u;
                CKBYTE *converted = (CKBYTE *)VxMalloc(copiedBytes);
                if (!converted)
                    return CKERR_OUTOFMEMORY;
                if (!CKBgfxConvertBumpLuminancePixels(
                        pf, Data->Image, sourcePitch, w, h,
                        converted, (CKDWORD)w * 4u)) {
                    VxFree(converted);
                    return CKERR_INVALIDPARAMETER;
                }
                mem = bgfx::copy(converted, copiedBytes);
                VxFree(converted);
            }
        } else {
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
                const bgfx::Memory *baseMem = NULL;
                if (bumpLuminance) {
                    const CKDWORD sourceBytes = pf == _16_L6V5U5 ? 2u : 4u;
                    const CKDWORD sourcePitch = hasMips
                        ? (CKDWORD)w * sourceBytes
                        : (Data->BytesPerLine > 0
                               ? (CKDWORD)Data->BytesPerLine
                               : (CKDWORD)w * sourceBytes);
                    CKBYTE *converted = (CKBYTE *)VxMalloc(baseInfo.storageSize);
                    if (!converted) {
                        DestroyRecord(rec);
                        return CKERR_OUTOFMEMORY;
                    }
                    if (!CKBgfxConvertBumpLuminancePixels(
                            pf, Data->Image, sourcePitch, w, h,
                            converted, (CKDWORD)w * 4u)) {
                        VxFree(converted);
                        DestroyRecord(rec);
                        return CKERR_INVALIDPARAMETER;
                    }
                    baseMem = bgfx::copy(converted, baseInfo.storageSize);
                    VxFree(converted);
                } else {
                    baseMem = bgfx::copy(Data->Image, baseInfo.storageSize);
                }
                if (baseMem) {
                    bgfx::updateTexture2D(rec->SamplerBaseHandle, 0, 0,
                                          0, 0, w, h, baseMem);
                    rec->SamplerBaseValid = TRUE;
                }
            }
        }
    }

    if (!StoreSlot(m_Textures, texture, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    TraceTextureMap((CKSTRING)"create", texture, rec);

    if (m_DebugLogTextures &&
        s_CreateTextureLogCount < 80) {
        const CKDWORD sourceLogBytes = bumpLuminance && Data && Data->Image
            ? (CKDWORD)w * (CKDWORD)h *
                  (pf == _16_L6V5U5 ? 2u : 4u)
            : copiedBytes;
        CKBgfxLogf("CreateTexture",
                 "id=%u handle=%u size=%ux%u flags=0x%X pf=%d bgfxFmt=%d bpp=%u requestedMips=%u actualMips=%u autoMips=%u initBytes=%u initFirst=0x%08X initHash=0x%08X",
                 texture, rec->Handle.idx, rec->Width, rec->Height, Desc->Flags,
                 (int)pf, (int)fmt, rec->BitsPerPixel, requestedMipCount,
                 rec->MipCount, requestedAutoMips, copiedBytes,
                 FirstDword(Data ? Data->Image : nullptr, sourceLogBytes),
                 SampleBytesChecksum(Data ? Data->Image : nullptr, sourceLogBytes));
        s_CreateTextureLogCount++;
    }

    if (uploadInitialAfterCreate) {
        CKERROR uploadResult = UpdateTexture(texture, 0, 0, NULL, Data);
        if (uploadResult != CK_OK) {
            DestroyObject(texture, CKRST_OBJ_TEXTURE);
            return uploadResult;
        }
    }

    *OutTexture = texture;
    if (Data && Data->Image)
        ++m_FrameTextureUploads;
    return CK_OK;
}
CKERROR CKBgfxBackend::CreateShader(const CKShaderDesc *Desc,
                                               CKDWORD *OutShader)
{
    if (!OutShader)
        return CKERR_INVALIDPARAMETER;
    *OutShader = 0;
    if (!m_BgfxInitialized || !m_Created || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (Desc->Stage != CKRST_SHADER_VERTEX && Desc->Stage != CKRST_SHADER_PIXEL)
        return CKERR_INVALIDPARAMETER;
    if (!Desc->Code || Desc->CodeSize == 0) {
        CKBgfxLogf("CreateShader",
                   "invalid shader blob shader=%u stage=%u code=%p size=%u",
                   0u,
                   Desc ? (unsigned)Desc->Stage : 0u,
                   Desc ? Desc->Code : NULL,
                   Desc ? Desc->CodeSize : 0u);
        return CKERR_INVALIDPARAMETER;
    }
    if (Desc->Format != CKRST_SHADER_FORMAT_NATIVE ||
        Desc->Profile != m_Caps.ShaderProfile) {
        CKBgfxLogf("CreateShader",
                   "shader target mismatch shader=%u stage=%u descFormat=0x%08X descProfile=%s(0x%08X) targetFormat=0x%08X targetProfile=%s(0x%08X)",
                   0u,
                   (unsigned)Desc->Stage,
                   Desc->Format,
                   CKBgfxShaderProfileName(Desc->Profile),
                   Desc->Profile,
                    CKRST_SHADER_FORMAT_NATIVE,
                    CKBgfxShaderProfileName(m_Caps.ShaderProfile),
                    m_Caps.ShaderProfile);
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
    if (!StoreSlot(m_Shaders, shader, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    *OutShader = shader;

    return CK_OK;
}
CKERROR CKBgfxBackend::CreateVertexLayout(const CKVertexLayoutDesc *Desc,
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
            (m_CapsDesc.Features & CKRST_DEVCAPS_VERTEX_ATTRIB_HALF) == 0)
            return CKERR_NOTIMPLEMENTED;
        if (elem.Type == CKRST_ATTRIBTYPE_UINT10 &&
            (m_CapsDesc.Features & CKRST_DEVCAPS_VERTEX_ATTRIB_UINT10) == 0)
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

    if (!StoreSlot(m_VertexLayouts, layout, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    *OutLayout = layout;

    return CK_OK;
}

CKERROR CKBgfxBackend::CreateProgram(CKDWORD VertexShader, CKDWORD PixelShader, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;

    CKBgfxShaderRecord *vs = GetShader(VertexShader);
    CKBgfxShaderRecord *ps = GetShader(PixelShader);
    if (!vs || !ps || vs->Stage != CKRST_SHADER_VERTEX || ps->Stage != CKRST_SHADER_PIXEL) {
        CKBgfxLogf("CreateProgram", "missing shaders vs=%p(h=%u) ps=%p(h=%u) shadersSize=%d",
                   vs, VertexShader, ps, PixelShader, m_Shaders.Size());
        return CKERR_INVALIDPARAMETER;
    }

    const CKDWORD program = AllocateSlot(m_Programs, m_CapsDesc.MaxPrograms, m_ResourceTableMutex);
    if (program == 0)
        return CKERR_OUTOFMEMORY;

    bgfx::ProgramHandle handle = bgfx::createProgram(vs->Handle, ps->Handle, false);
    if (!bgfx::isValid(handle)) {
        CKBgfxLogf("CreateProgram", "bgfx::createProgram failed vs.idx=%u ps.idx=%u",
                   vs->Handle.idx, ps->Handle.idx);
        return CKERR_INVALIDPARAMETER;
    }

    auto *rec = new CKBgfxProgramRecord();
    rec->Handle = handle;
    rec->VertexShader = VertexShader;
    rec->PixelShader = PixelShader;

    if (!StoreSlot(m_Programs, program, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    *Out = program;
    TraceProgramMap((CKSTRING)"create", program, rec);
    return CK_OK;
}

CKERROR CKBgfxBackend::BuildFrameBufferAttachments(const CKBackendRenderTargetDesc *Desc,
                                                   bgfx::Attachment *Attachments,
                                                   CKDWORD Capacity,
                                                   CKDWORD &AttachmentCount)
{
    AttachmentCount = 0;
    if (!Desc || !Attachments || Capacity < 2)
        return CKERR_INVALIDPARAMETER;

    CKBgfxTextureRecord *tex = GetTexture(Desc->ColorTexture);
    const CKBOOL cube = tex && (tex->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    const CKBOOL volume = tex &&
        (tex->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && tex->Depth > 1;
    if (!tex || tex->IsDepth ||
        (tex->Flags & CKRST_TEXTURE_RENDERTARGET) == 0 ||
        Desc->ColorMip >= tex->MipCount ||
        (!cube && !volume && Desc->ColorLayer != 0) ||
        (cube && Desc->ColorLayer >= 6) ||
        (volume && Desc->ColorLayer >=
            XMax((CKDWORD)1, tex->Depth >> Desc->ColorMip)))
    {
        return CKERR_INVALIDPARAMETER;
    }
    const uint8_t resolve = tex->RequestedAutoMips && tex->MipCount > 1
        ? BGFX_RESOLVE_AUTO_GEN_MIPS : BGFX_RESOLVE_NONE;
    Attachments[AttachmentCount].init(
        tex->Handle, bgfx::Access::Write,
        (uint16_t)Desc->ColorLayer, 1,
        (uint16_t)Desc->ColorMip, resolve);
    ++AttachmentCount;

    if (Desc->DepthTexture != 0)
    {
        CKBgfxTextureRecord *depthTex = GetTexture(Desc->DepthTexture);
        if (!depthTex || !depthTex->IsDepth)
            return CKERR_INVALIDPARAMETER;
        Attachments[AttachmentCount].init(
            depthTex->Handle, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
        ++AttachmentCount;
    }
    return CK_OK;
}

CKERROR CKBgfxBackend::CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_DEVCAPS_FRAMEBUFFER) == 0)
        return CKERR_NOTIMPLEMENTED;

    bgfx::Attachment attachments[CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS];
    CKDWORD totalAttachments = 0;
    const CKERROR attachmentError = BuildFrameBufferAttachments(
        Desc, attachments, CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS, totalAttachments);
    if (attachmentError != CK_OK)
        return attachmentError;

    if (!bgfx::isFrameBufferValid((uint8_t)totalAttachments, attachments))
        return CKERR_NOTIMPLEMENTED;

    bgfx::FrameBufferHandle handle = bgfx::createFrameBuffer(
        (uint8_t)totalAttachments, attachments, false);
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
    rec->Desc = *Desc;

    if (!StoreSlot(m_FrameBuffers, frameBuffer, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    *Out = frameBuffer;
    return CK_OK;
}

CKERROR CKBgfxBackend::CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_DEVCAPS_DEPTH_TEXTURE) == 0)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Width == 0 || Desc->Height == 0 ||
        Desc->Width > m_CapsDesc.MaxTextureSize ||
        Desc->Height > m_CapsDesc.MaxTextureSize)
        return CKERR_INVALIDPARAMETER;

    uint16_t w = (uint16_t)Desc->Width;
    uint16_t h = (uint16_t)Desc->Height;
    bgfx::TextureFormat::Enum fmt;
    if (!CKBgfxTryDepthFormat(Desc->Format, fmt))
        return CKERR_INVALIDPARAMETER;
    const CKDWORD formatCaps = CKBgfxMapFormatCaps(
        m_NativeFormatCaps[fmt], FALSE,
        (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_COMPARISON) != 0);
    const CKDWORD requiredCaps =
        CKRST_FORMAT_CAPS_FRAMEBUFFER | CKRST_FORMAT_CAPS_TEXTURE_2D;
    if ((formatCaps & requiredCaps) != requiredCaps)
        return CKERR_NOTIMPLEMENTED;

    const CKDWORD msaaDescFlag = CKRSTTextureMSAAFlag(Desc->Samples);
    uint64_t texFlags = BGFX_TEXTURE_RT;
    const uint64_t msaa = CKBgfxTextureMSAAFlags(msaaDescFlag);
    if (msaa) {
        // Multisampled depth is only ever written by the pass that owns it.
        texFlags = msaa | BGFX_TEXTURE_RT_WRITE_ONLY;
    }
    if (!bgfx::isTextureValid(0, false, 1, fmt, texFlags))
        return CKERR_NOTIMPLEMENTED;
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        w, h, false, 1, fmt, texFlags, NULL);
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
    rec->Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL | msaaDescFlag;
    rec->Width = w;
    rec->Height = h;
    rec->Depth = 1;
    rec->IsDepth = TRUE;
    rec->RequestedAutoMips = FALSE;
    rec->MipCount = 1;
    rec->Format = fmt;
    rec->BitsPerPixel = 0;

    if (!StoreSlot(m_Textures, texture, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    *Out = texture;
    TraceTextureMap((CKSTRING)"create", texture, rec);
    return CK_OK;
}

CKBOOL CKBgfxBackend::IsObjectAlive(CKDWORD Object, CKDWORD Type) const
{
    if (!m_BgfxInitialized || !m_Created || Object == 0)
        return FALSE;
    switch (Type) {
    case CKRST_OBJ_TEXTURE:        return IsSlotAlive(m_Textures, Object, m_ResourceTableMutex);
    case CKRST_OBJ_VERTEXBUFFER:   return IsSlotAlive(m_VertexBuffers, Object, m_ResourceTableMutex);
    case CKRST_OBJ_INDEXBUFFER:    return IsSlotAlive(m_IndexBuffers, Object, m_ResourceTableMutex);
    case CKRST_OBJ_SHADER:         return IsSlotAlive(m_Shaders, Object, m_ResourceTableMutex);
    case CKRST_OBJ_PROGRAM:        return IsSlotAlive(m_Programs, Object, m_ResourceTableMutex);
    case CKRST_OBJ_VERTEXLAYOUT:   return IsSlotAlive(m_VertexLayouts, Object, m_ResourceTableMutex);
    case CKRST_OBJ_FRAMEBUFFER:    return IsSlotAlive(m_FrameBuffers, Object, m_ResourceTableMutex);
    default:                       return FALSE;
    }
}

// bgfx defers the destruction to the end of the frame, so objects may go
// away in the middle of a pass.
CKERROR CKBgfxBackend::DestroyObject(CKDWORD Object, CKDWORD Type)
{
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (Object == 0)
        return CKERR_INVALIDPARAMETER;
    switch (Type) {
    case CKRST_OBJ_TEXTURE: {
        CKBgfxTextureRecord *rec = TakeSlot(m_Textures, Object, m_ResourceTableMutex);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceTextureMap((CKSTRING)"delete", Object, rec);
        DestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_VERTEXBUFFER: {
        CKBgfxVertexBufferRecord *rec = TakeSlot(m_VertexBuffers, Object, m_ResourceTableMutex);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceBufferMap((CKSTRING)"delete", (CKSTRING)"vb", Object, rec->Handle.idx, rec->Layout, rec->VertexSize, 0, 0, 0);
        DestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_INDEXBUFFER: {
        CKBgfxIndexBufferRecord *rec = TakeSlot(m_IndexBuffers, Object, m_ResourceTableMutex);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceBufferMap((CKSTRING)"delete", (CKSTRING)"ib", Object, rec->Handle.idx, 0, rec->Index32 ? 4u : 2u, 0, rec->Index32 ? 1u : 0u, 0);
        DestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_SHADER: {
        CKBgfxShaderRecord *rec = TakeSlot(m_Shaders, Object, m_ResourceTableMutex);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        DestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_PROGRAM: {
        CKBgfxProgramRecord *rec = TakeSlot(m_Programs, Object, m_ResourceTableMutex);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceProgramMap((CKSTRING)"delete", Object, rec);
        DestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_VERTEXLAYOUT: {
        CKBgfxVertexLayoutRecord *rec = TakeSlot(m_VertexLayouts, Object, m_ResourceTableMutex);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        DestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_FRAMEBUFFER: {
        CKBgfxFrameBufferRecord *rec = TakeSlot(m_FrameBuffers, Object, m_ResourceTableMutex);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        DestroyRecord(rec);
        return CK_OK;
    }
    default:
        return CKERR_INVALIDPARAMETER;
    }
}

void CKBgfxBackend::SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name)
{
    if (!IsReady() || !Name)
        return;
    const int32_t len = (int32_t)strlen(Name);
    switch (Type)
    {
    case CKRST_OBJ_SHADER:
        if (CKBgfxShaderRecord *r = GetShader(Object))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_TEXTURE:
        if (CKBgfxTextureRecord *r = GetTexture(Object))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_FRAMEBUFFER:
        if (CKBgfxFrameBufferRecord *r = GetFrameBuffer(Object))
            bgfx::setName(r->Handle, Name, len);
        break;
    default:
        break;
    }
}


// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

CKERROR CKBgfxBackend::CreateVertexBufferRecord(CKDWORD VertexSize, CKDWORD VertexCount, CKDWORD Layout,
                                                const void *Data, CKDWORD *OutBuffer)
{
    if (VertexSize == 0 || VertexSize > UINT16_MAX || VertexCount == 0)
        return CKERR_INVALIDPARAMETER;
    uint64_t totalSize64 = (uint64_t)VertexCount * VertexSize;
    if (totalSize64 > UINT32_MAX)
        return CKERR_OUTOFMEMORY;
    CKDWORD totalSize = (CKDWORD)totalSize64;

    // The stride is all bgfx needs of the buffer's layout; draws bind the
    // real vertex layout handle.
    bgfx::VertexLayout layout;
    layout.begin(m_RendererType);
    CKDWORD remainingStride = VertexSize;
    while (remainingStride > 0) {
        const uint8_t chunk = static_cast<uint8_t>(
            XMin(remainingStride, (CKDWORD)UINT8_MAX));
        layout.skip(chunk);
        remainingStride -= chunk;
    }
    layout.end();

    const uint16_t flags = BGFX_BUFFER_ALLOW_RESIZE;
    bgfx::DynamicVertexBufferHandle handle;
    if (Data)
    {
        const bgfx::Memory *mem = bgfx::copy(Data, totalSize);
        handle = bgfx::createDynamicVertexBuffer(mem, layout, flags);
    }
    else
    {
        handle = bgfx::createDynamicVertexBuffer(VertexCount, layout, flags);
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
    rec->Layout = Layout;
    rec->VertexSize = VertexSize;
    rec->VertexCount = VertexCount;
    rec->Size = totalSize;

    if (!StoreSlot(m_VertexBuffers, buffer, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    *OutBuffer = buffer;
    TraceBufferMap((CKSTRING)"create", (CKSTRING)"vb", buffer, rec->Handle.idx,
                   rec->Layout, rec->VertexSize, VertexCount, 0, 0);
    return CK_OK;
}

CKERROR CKBgfxBackend::CreateIndexBufferRecord(CKDWORD IndexCount, CKBOOL Index32, const void *Data,
                                               CKDWORD *OutBuffer)
{
    if (IndexCount == 0)
        return CKERR_INVALIDPARAMETER;
    if (Index32 && (m_CapsDesc.Features & CKRST_DEVCAPS_INDEX32) == 0)
        return CKERR_NOTIMPLEMENTED;

    CKDWORD indexSize = Index32 ? 4 : 2;
    uint64_t totalSize64 = (uint64_t)IndexCount * indexSize;
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
        handle = bgfx::createDynamicIndexBuffer(IndexCount, flags);
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
    rec->IndexCount = IndexCount;
    rec->Size = totalSize;

    if (!StoreSlot(m_IndexBuffers, buffer, rec, m_ResourceTableMutex)) {
        DestroyRecord(rec);
        return CKERR_INVALIDOPERATION;
    }
    *OutBuffer = buffer;
    TraceBufferMap((CKSTRING)"create", (CKSTRING)"ib", buffer, rec->Handle.idx,
                   0, indexSize, IndexCount, Index32 ? 1u : 0u, 0);
    return CK_OK;
}

CKERROR CKBgfxBackend::CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Size == 0)
        return CKERR_INVALIDPARAMETER;
    CKERROR err;
    if (Desc->Kind == CKRST_BACKEND_BUFFER_VERTEX) {
        if (Desc->Stride == 0 || (Desc->Size % Desc->Stride) != 0)
            return CKERR_INVALIDPARAMETER;
        err = CreateVertexBufferRecord(Desc->Stride, Desc->Size / Desc->Stride, Desc->Layout, Desc->InitialData, Out);
    } else {
        const CKDWORD indexSize = Desc->Index32 ? 4 : 2;
        if ((Desc->Size % indexSize) != 0)
            return CKERR_INVALIDPARAMETER;
        err = CreateIndexBufferRecord(Desc->Size / indexSize, Desc->Index32, Desc->InitialData, Out);
    }
    if (err == CK_OK && Desc->InitialData)
        ++m_FrameBufferUploads;
    return err;
}

CKERROR CKBgfxBackend::UpdateBuffer(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data)
{
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    CKERROR err;
    if (IsSlotAlive(m_VertexBuffers, Buffer, m_ResourceTableMutex))
        err = UpdateVertexBufferRecord(Buffer, Offset, Size, Data);
    else if (IsSlotAlive(m_IndexBuffers, Buffer, m_ResourceTableMutex))
        err = UpdateIndexBufferRecord(Buffer, Offset, Size, Data);
    else
        return CKERR_INVALIDPARAMETER;
    if (err == CK_OK)
        ++m_FrameBufferUploads;
    return err;
}

CKERROR CKBgfxBackend::UpdateVertexBufferRecord(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Data || Buffer == 0 || Size == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Buffer);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

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
CKERROR CKBgfxBackend::UpdateIndexBufferRecord(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data)
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
CKERROR CKBgfxBackend::UpdateTexture(CKDWORD Texture, CKDWORD Mip,
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
    const bool bumpLuminance = CKBgfxIsBumpLuminanceFormat(rec->PixelFormat);
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
    else if (bumpLuminance)
    {
        const CKDWORD sourceBytes = rec->PixelFormat == _16_L6V5U5 ? 2u : 4u;
        const CKDWORD sourcePitch = Data->BytesPerLine > 0
            ? (CKDWORD)Data->BytesPerLine : (CKDWORD)w * sourceBytes;
        if (sourcePitch < (CKDWORD)w * sourceBytes)
            return CKERR_INVALIDPARAMETER;
        const CKDWORD convertedSize = (CKDWORD)w * (CKDWORD)h * 4u;
        CKBYTE *converted = (CKBYTE *)VxMalloc(convertedSize);
        if (!converted)
            return CKERR_OUTOFMEMORY;
        if (!CKBgfxConvertBumpLuminancePixels(
                rec->PixelFormat, Data->Image, sourcePitch, w, h,
                converted, (CKDWORD)w * 4u)) {
            VxFree(converted);
            return CKERR_INVALIDPARAMETER;
        }
        mem = bgfx::copy(converted, convertedSize);
        if (updateSamplerBase)
            samplerBaseMem = bgfx::copy(converted, convertedSize);
        VxFree(converted);
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

    ++m_FrameTextureUploads;
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
CKERROR CKBgfxBackend::ReadTexture(CKDWORD Texture, CKDWORD Mip,
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

    *AvailableFrame = bgfx::readTexture(rec->Handle, data, 0, (uint8_t)Mip);
    return CK_OK;
}
CKERROR CKBgfxBackend::SetPaletteColor(CKDWORD Index, CKDWORD RGBA)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (Index >= 16)
        return CKERR_INVALIDPARAMETER;
    bgfx::setPaletteColor((uint8_t)Index, RGBA);
    return CK_OK;
}
void CKBgfxBackend::SetDebugFlags(CKDWORD Flags)
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
    if (!m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    if (Flags & CKRST_DEBUG_WIREFRAME) bgfxFlags |= BGFX_DEBUG_WIREFRAME;
    if (Flags & CKRST_DEBUG_IFH)       bgfxFlags |= BGFX_DEBUG_IFH;
    if (Flags & CKRST_DEBUG_STATS)     bgfxFlags |= BGFX_DEBUG_STATS;
    if (Flags & CKRST_DEBUG_TEXT)      bgfxFlags |= BGFX_DEBUG_TEXT;
    if (Flags & CKRST_DEBUG_PROFILER)  bgfxFlags |= BGFX_DEBUG_PROFILER;
    m_DebugBgfxFlags = bgfxFlags;
    if (m_BgfxInitialized)
        bgfx::setDebug(bgfxFlags);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

CKERROR CKBgfxBackend::BeginPass(const CKBackendPassDesc *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    const CKERROR status = GetDeviceStatus();
    if (status != CK_OK)
        return status;
    const CKRECT &rect = Desc->Rect;
    if (rect.left < 0 || rect.top < 0 || rect.right <= rect.left || rect.bottom <= rect.top ||
        rect.right > UINT16_MAX || rect.bottom > UINT16_MAX)
        return CKERR_INVALIDPARAMETER;
    if ((Desc->ClearFlags & ~(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL)) != 0 ||
        Desc->ClearStencil > 0xff || !(Desc->ClearZ >= 0.0f && Desc->ClearZ <= 1.0f))
        return CKERR_INVALIDPARAMETER;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    if (Desc->RenderTarget != 0) {
        CKBgfxFrameBufferRecord *rec = GetFrameBuffer(Desc->RenderTarget);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        frameBuffer = rec->Handle;
    }

    m_FrameInProgress = TRUE;
    CKRenderView view;
    CKDWORD clearFlags = Desc->ClearFlags;
    if (m_NextView < m_CapsDesc.MaxRenderViews) {
        view = (CKRenderView)m_NextView++;
    } else {
        // Out of views: keep drawing into the last pass; a clear would apply
        // to the whole pass, so it is dropped.
        view = m_CurrentView;
        clearFlags = 0;
    }

    bgfx::setViewMode((bgfx::ViewId)view, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer((bgfx::ViewId)view, frameBuffer);
    bgfx::setViewRect((bgfx::ViewId)view,
                      (uint16_t)rect.left, (uint16_t)rect.top,
                      (uint16_t)(rect.right - rect.left),
                      (uint16_t)(rect.bottom - rect.top));
    uint16_t bgfxClearFlags = 0;
    if (clearFlags & CKRST_CTXCLEAR_COLOR)   bgfxClearFlags |= BGFX_CLEAR_COLOR;
    if (clearFlags & CKRST_CTXCLEAR_DEPTH)   bgfxClearFlags |= BGFX_CLEAR_DEPTH;
    if (clearFlags & CKRST_CTXCLEAR_STENCIL) bgfxClearFlags |= BGFX_CLEAR_STENCIL;
    const CKDWORD a = (Desc->ClearColor >> 24) & 0xFF;
    const CKDWORD r = (Desc->ClearColor >> 16) & 0xFF;
    const CKDWORD g = (Desc->ClearColor >> 8) & 0xFF;
    const CKDWORD b = (Desc->ClearColor >> 0) & 0xFF;
    bgfx::setViewClear((bgfx::ViewId)view, bgfxClearFlags, (r << 24) | (g << 16) | (b << 8) | a,
                       Desc->ClearZ, (uint8_t)Desc->ClearStencil);
    if ((m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 && Desc->Name) {
        bgfx::setViewName((bgfx::ViewId)view, Desc->Name);
        CKBgfxCopyDebugText(m_DebugViewName[view], sizeof(m_DebugViewName[view]), (CKSTRING)Desc->Name);
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_VIEWS))
            CKBgfxLogf("ViewMap", "frame=%u view=%u name=%s target=%u rect=%d,%d-%d,%d clear=0x%X",
                       m_DebugFrameId, (unsigned)view, m_DebugViewName[view], Desc->RenderTarget,
                       rect.left, rect.top, rect.right, rect.bottom, clearFlags);
    }
    {
        VxMutexLock lock(m_ResourceStateMutex);
        m_ViewFrameBuffer[view] = Desc->RenderTarget;
        m_ViewRect[view] = rect;
        m_ViewClearFlags[view] = clearFlags;
        m_ViewClearRecorded[view] = FALSE;
    }
    RecordViewColorWrite(view, FALSE);
    bgfx::touch((bgfx::ViewId)view);

    m_CurrentView = view;
    m_PassOpen = TRUE;
    ++m_FramePasses;
    return CK_OK;
}

void CKBgfxBackend::SetPipelineState(const CKBackendPipelineState *State)
{
    if (State)
        m_State = *State;
}

void CKBgfxBackend::BindTexture(CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler)
{
    if (Slot >= CKRST_BACKEND_SLOT_COUNT)
        return;
    SlotBinding &slot = m_Slots[Slot];
    if (Texture == 0) {
        // bgfx forgets its bindings after every submit, so clearing a bound
        // slot costs nothing; an explicit zero binding on an empty slot is the
        // sampler-unit assignment GLSL programs need.
        slot.PendingZero = slot.Texture == 0 ? TRUE : FALSE;
        slot.Texture = 0;
        slot.HasSampler = FALSE;
        return;
    }
    slot.Texture = Texture;
    slot.PendingZero = FALSE;
    slot.HasSampler = Sampler != NULL;
    if (Sampler)
        slot.Sampler = *Sampler;
}

CKERROR CKBgfxBackend::PushConstants(CKBackendConstantBlock Block, const void *Data, CKDWORD Vec4Count)
{
    if ((int)Block < 0 || (int)Block >= CKRST_BLOCK_COUNT || !Data || Vec4Count == 0)
        return CKERR_INVALIDPARAMETER;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    const CKBackendConstantBlockDesc &info = CKBackendConstantBlockInfo(Block);
    const CKDWORD count = info.Mat4 ? Vec4Count / 4 : Vec4Count;
    if (count == 0 || count > info.Count || !bgfx::isValid(m_BlockUniforms[Block]))
        return DrawFailed(CKERR_INVALIDPARAMETER, "PushConstants");
    static int s_uniformLogCount = 0;
    if (m_DebugLogUniforms && s_uniformLogCount < 256) {
        const float *f = static_cast<const float *>(Data);
        CKBgfxLogf("PushConstants",
                   "block=%s handle=%u count=%u first=(%.3f %.3f %.3f %.3f)",
                   info.Name, m_BlockUniforms[Block].idx, count, f[0], f[1], f[2], f[3]);
        ++s_uniformLogCount;
    }
    bgfx::setUniform(m_BlockUniforms[Block], Data, (uint16_t)count);
    if (m_DrawMapSubmitActive && Block == CKRST_BLOCK_SPEC) {
        m_DebugSpecializationHash = SampleBytesChecksum(Data, Vec4Count * 4u * (CKDWORD)sizeof(float));
        m_DebugSpecializationValid = TRUE;
    }
    return CK_OK;
}

void CKBgfxBackend::SetMarker(const char *Name)
{
    if (!IsReady())
        return;
    if (!Name) {
        m_LastMarker[0] = '\0';
        return;
    }
    if (m_DrawMapMarkerCaptureActive) {
        if (m_LastMarker[0] != '\0') {
            m_DebugMarkerOverwriteCount.fetch_add(1, std::memory_order_relaxed);
            if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS))
                CKBgfxLogf("MarkerOverwrite",
                           "frame=%u old=\"%s\" new=\"%s\"",
                           m_DebugFrameId, m_LastMarker, Name);
        }
        strncpy(m_LastMarker, Name, sizeof(m_LastMarker) - 1);
        m_LastMarker[sizeof(m_LastMarker) - 1] = '\0';
    }
    bgfx::setMarker(Name);
    if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS))
        CKBgfxLogf("Marker", "%s", Name);
}

CKBOOL CKBgfxBackend::AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out)
{
    if (!Out || Count == 0 || !m_BgfxInitialized || !m_Created)
        return FALSE;
    *Out = CKBackendTransientVertices();
    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return FALSE;

    const CKDWORD available = bgfx::getAvailTransientVertexBuffer(Count, layoutRec->Layout);
    if (available < Count) {
        RecordTransientAllocMiss("vertex", Count, available);
        return FALSE;
    }
    if (m_TransientVBCount >= MAX_TRANSIENT_VB) {
        RecordTransientAllocMiss("vertex-pool", Count, MAX_TRANSIENT_VB);
        return FALSE;
    }
    bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[m_TransientVBCount];
    bgfx::allocTransientVertexBuffer(tvb, Count, layoutRec->Layout);
    if (tvb->data == NULL) {
        RecordTransientAllocMiss("vertex-alloc", Count, available);
        return FALSE;
    }
    ++m_TransientVBCount;
    Out->Data = tvb->data;
    Out->Count = Count;
    Out->Stride = layoutRec->Layout.m_stride;
    Out->Layout = Layout;
    Out->Token = m_TransientVBCount;
    return TRUE;
}

CKBOOL CKBgfxBackend::AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out)
{
    if (!Out || Count == 0 || !m_BgfxInitialized || !m_Created ||
        (Index32 && (m_CapsDesc.Features & CKRST_DEVCAPS_INDEX32) == 0))
        return FALSE;
    *Out = CKBackendTransientIndices();

    const CKDWORD available = bgfx::getAvailTransientIndexBuffer(Count, Index32 ? true : false);
    if (available < Count) {
        RecordTransientAllocMiss("index", Count, available);
        return FALSE;
    }
    if (m_TransientIBCount >= MAX_TRANSIENT_IB) {
        RecordTransientAllocMiss("index-pool", Count, MAX_TRANSIENT_IB);
        return FALSE;
    }
    bgfx::TransientIndexBuffer *tib = &m_TransientIBPool[m_TransientIBCount];
    bgfx::allocTransientIndexBuffer(tib, Count, Index32 ? true : false);
    if (tib->data == NULL) {
        RecordTransientAllocMiss("index-alloc", Count, available);
        return FALSE;
    }
    ++m_TransientIBCount;
    Out->Data = tib->data;
    Out->Count = Count;
    Out->Index32 = Index32;
    Out->Token = m_TransientIBCount;
    return TRUE;
}

// A draw that cannot be submitted: the pending bgfx state is dropped so the
// next draw starts clean.
CKERROR CKBgfxBackend::DrawFailed(CKERROR Error, const char *Operation)
{
    bgfx::discard(BGFX_DISCARD_ALL);
    ResetDebugBindings();
    if (m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    if (m_DrawErrorLogCount < 16) {
        ++m_DrawErrorLogCount;
        CKBgfxLogf("DrawError", "frame=%u operation=%s error=0x%08X",
                   m_DebugFrameId, Operation ? Operation : "unknown", (unsigned)Error);
    }
    return Error;
}

static void ApplyStencil(CKDrawState state, CKDWORD ref, CKDWORD readMask, CKDWORD writeMask)
{
    const uint32_t fstencil = CKBgfxBuildFrontStencil(state, ref, readMask, writeMask);
    const uint32_t bstencil = CKBgfxBuildBackStencil(state, ref, readMask, writeMask);
    bgfx::setStencil(fstencil, bstencil);
}

CKERROR CKBgfxBackend::ApplyPipelineState()
{
    uint64_t bgfxState = 0;
    const CKERROR stateError = CKBgfxTryState(m_State.State, bgfxState);
    if (stateError != CK_OK)
        return stateError;
    if (!(m_State.PointSize >= 0.0f && m_State.PointSize <= 15.0f))
        return CKERR_INVALIDPARAMETER;
    const CKDWORD stencilRef = m_State.StencilRef & 0xFF;
    const CKDWORD stencilReadMask = m_State.StencilReadMask & 0xFF;
    const CKDWORD stencilWriteMask = m_State.StencilWriteMask & 0xFF;
    // bgfx has no stencil write mask (appendix C): the pipeline approximates
    // partial masks before the draw reaches the backend.
    if ((m_State.State.Mid & CKRST_STENCIL_ENABLE) && stencilWriteMask != 0x00 && stencilWriteMask != 0xFF)
        return CKERR_NOTIMPLEMENTED;
    m_PointSize = (CKDWORD)(m_State.PointSize + 0.5f);
    m_CachedDrawState = m_State.State;
    m_CachedBgfxState = bgfxState;
    uint64_t finalState = bgfxState;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);
    bgfx::setState(finalState);
    ApplyStencil(m_State.State, stencilRef, stencilReadMask, stencilWriteMask);

    if (m_State.ScissorEnabled) {
        const CKRECT &rect = m_State.Scissor;
        if (rect.left < 0 || rect.top < 0 ||
            rect.right < rect.left || rect.bottom < rect.top ||
            rect.left > 0xffff || rect.top > 0xffff ||
            rect.right - rect.left > 0xffff ||
            rect.bottom - rect.top > 0xffff)
            return CKERR_INVALIDPARAMETER;
        bgfx::setScissor((uint16_t)rect.left, (uint16_t)rect.top,
                         (uint16_t)(rect.right - rect.left), (uint16_t)(rect.bottom - rect.top));
    } else {
        bgfx::setScissor();
    }
    return CK_OK;
}

CKERROR CKBgfxBackend::BindGeometry(const CKBackendDraw *Draw)
{
    // Stream 0
    if (Draw->TransientVertices) {
        const CKBackendTransientVertices *tv = Draw->TransientVertices;
        if (tv->Token == 0 || tv->Token > m_TransientVBCount || tv->Count == 0)
            return CKERR_INVALIDPARAMETER;
        bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[tv->Token - 1];
        if (tvb->data != tv->Data || tv->Count > tvb->size / XMax((CKDWORD)1, (CKDWORD)tvb->stride))
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(0, tvb, 0, tv->Count);
        m_CurrentLayout = tv->Layout;
        if (m_DrawMapSubmitActive) {
            CKBgfxDrawMapVertexBinding &binding = m_DebugVertexBindings[0];
            binding.Buffer = 0;
            binding.Start = tvb->startVertex;
            binding.Count = tv->Count;
            binding.BgfxHandle = tvb->handle.idx;
            binding.LayoutHandle = tvb->layoutHandle.idx;
            m_DebugVertexBindingMask |= 1u;
        }
    } else if (Draw->VertexBuffer) {
        CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Draw->VertexBuffer);
        CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Draw->Layout);
        if (!rec || !layoutRec || Draw->VertexCount == 0 || Draw->StartVertex > rec->VertexCount ||
            Draw->VertexCount > rec->VertexCount - Draw->StartVertex)
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(0, rec->Handle, Draw->StartVertex, Draw->VertexCount, layoutRec->Handle);
        m_CurrentLayout = Draw->Layout;
        if (m_DrawMapSubmitActive) {
            m_DebugVertexBindings[0].Buffer = Draw->VertexBuffer;
            m_DebugVertexBindings[0].Start = Draw->StartVertex;
            m_DebugVertexBindings[0].Count = Draw->VertexCount;
            m_DebugVertexBindings[0].BgfxHandle = rec->Handle.idx;
            m_DebugVertexBindings[0].LayoutHandle = layoutRec->Handle.idx;
            m_DebugVertexBindingMask |= 1u;
        }
    } else {
        return CKERR_INVALIDPARAMETER;
    }

    // Stream 1 (vertex tweening)
    if (Draw->Stream1Transient) {
        const CKBackendTransientVertices *tv = Draw->Stream1Transient;
        if (tv->Token == 0 || tv->Token > m_TransientVBCount || tv->Count == 0 || m_CapsDesc.MaxVertexStreams < 2)
            return CKERR_INVALIDPARAMETER;
        bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[tv->Token - 1];
        if (tvb->data != tv->Data)
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(1, tvb, 0, tv->Count);
    } else if (Draw->Stream1VertexBuffer) {
        CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Draw->Stream1VertexBuffer);
        CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Draw->Stream1Layout);
        if (!rec || !layoutRec || m_CapsDesc.MaxVertexStreams < 2 || Draw->Stream1StartVertex > rec->VertexCount ||
            Draw->VertexCount > rec->VertexCount - Draw->Stream1StartVertex)
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(1, rec->Handle, Draw->Stream1StartVertex, Draw->VertexCount, layoutRec->Handle);
    }

    // Indices
    if (Draw->TransientIndices) {
        const CKBackendTransientIndices *ti = Draw->TransientIndices;
        if (ti->Token == 0 || ti->Token > m_TransientIBCount || ti->Count == 0)
            return CKERR_INVALIDPARAMETER;
        bgfx::TransientIndexBuffer *tib = &m_TransientIBPool[ti->Token - 1];
        const CKDWORD indexSize = tib->isIndex16 ? 2u : 4u;
        if (tib->data != ti->Data || ti->Count > tib->size / indexSize)
            return CKERR_INVALIDPARAMETER;
        bgfx::setIndexBuffer(tib, 0, ti->Count);
        if (m_DrawMapSubmitActive) {
            m_DebugIndexBuffer = 0;
            m_DebugIndexStart = tib->startIndex;
            m_DebugIndexCount = ti->Count;
            m_DebugIndexHandle = tib->handle.idx;
        }
    } else if (Draw->IndexBuffer) {
        CKBgfxIndexBufferRecord *rec = GetIndexBuffer(Draw->IndexBuffer);
        if (!rec || Draw->IndexCount == 0 || Draw->StartIndex > rec->IndexCount ||
            Draw->IndexCount > rec->IndexCount - Draw->StartIndex)
            return CKERR_INVALIDPARAMETER;
        bgfx::setIndexBuffer(rec->Handle, Draw->StartIndex, Draw->IndexCount);
        if (m_DrawMapSubmitActive) {
            m_DebugIndexBuffer = Draw->IndexBuffer;
            m_DebugIndexStart = Draw->StartIndex;
            m_DebugIndexCount = Draw->IndexCount;
            m_DebugIndexHandle = rec->Handle.idx;
        }
    }
    return CK_OK;
}

CKERROR CKBgfxBackend::BindTextureSlot(CKDWORD Stage, CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler)
{
    static int s_SetTextureLogCount = 0;
    if (Stage >= m_CapsDesc.MaxTextureBindings)
        return CKERR_INVALIDPARAMETER;
    CKBgfxTextureRecord *texRec = GetTexture(Texture);
    if (Texture != 0 && !texRec)
        return CKERR_INVALIDPARAMETER;
    if (texRec && (texRec->Flags & CKRST_TEXTURE_READBACK) != 0)
        return CKERR_NOTIMPLEMENTED;
    uint32_t flags = BGFX_SAMPLER_NONE;
    if (!CKBgfxTrySamplerFlags(Sampler, flags))
        return CKERR_INVALIDPARAMETER;
    if (Sampler && Sampler->CompareFunc != CKRST_COMPARE_NONE) {
        if (!texRec ||
            (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_COMPARISON) == 0 ||
            (CKBgfxMapFormatCaps(m_NativeFormatCaps[texRec->Format], FALSE, TRUE) &
             CKRST_FORMAT_CAPS_TEXTURE_COMPARE) == 0)
            return CKERR_NOTIMPLEMENTED;
    }
    bgfx::TextureHandle textureHandle = texRec ? texRec->Handle : m_DefaultWhiteTexture;
    bool usingSamplerBase = false;
    if (texRec && !CKBgfxSamplerWantsMipMaps(Sampler) && texRec->MipCount > 1) {
        if (!texRec->SamplerBaseValid || !bgfx::isValid(texRec->SamplerBaseHandle))
            return CKERR_NOTIMPLEMENTED;
        textureHandle = texRec->SamplerBaseHandle;
        usingSamplerBase = true;
    }
    if (m_DebugLogTextureBindings && s_SetTextureLogCount < 80) {
        CKBgfxLogf("BindTexture",
                   "stage=%u slot=%u texture=%u tex=%p texIdx=%u base=%u size=%ux%u fmt=%d sampler=%p",
                   Stage, Slot, Texture, (void *)texRec,
                   bgfx::isValid(textureHandle) ? textureHandle.idx : 0xffff,
                   usingSamplerBase ? 1u : 0u,
                   texRec ? texRec->Width : 0,
                   texRec ? texRec->Height : 0,
                   texRec ? (int)texRec->Format : -1,
                   (const void *)Sampler);
        s_SetTextureLogCount++;
    }
    if (!bgfx::isValid(textureHandle))
        return CKERR_INVALIDPARAMETER;
    bgfx::setTexture((uint8_t)Stage, m_SamplerUniforms[Slot], textureHandle, flags);

    if (m_DrawMapSubmitActive && Stage < CKRST_MAX_TEXTURE_STAGES) {
        m_DebugTextureBindings[Stage].Texture = Texture;
        m_DebugTextureBindings[Stage].Uniform = Slot;
        m_DebugTextureBindings[Stage].BgfxHandle = textureHandle.idx;
        m_DebugTextureBindings[Stage].SamplerFlags = flags;
        m_DebugTextureBindingMask |= (1u << Stage);
    }
    return CK_OK;
}

void CKBgfxBackend::ResetDebugBindings()
{
    memset(m_DebugVertexBindings, 0, sizeof(m_DebugVertexBindings));
    m_DebugVertexBindingMask = 0;
    m_DebugIndexBuffer = 0;
    m_DebugIndexStart = 0;
    m_DebugIndexCount = 0;
    m_DebugIndexHandle = 0;
    memset(m_DebugTextureBindings, 0, sizeof(m_DebugTextureBindings));
    m_DebugTextureBindingMask = 0;
    m_DebugSpecializationHash = 0;
    m_DebugSpecializationValid = FALSE;
}

CKERROR CKBgfxBackend::Draw(const CKBackendDraw *Draw)
{
    if (!Draw || !Draw->Program)
        return CKERR_INVALIDPARAMETER;
    if (!IsReady() || !m_PassOpen)
        return CKERR_INVALIDOPERATION;
    const CKERROR status = GetDeviceStatus();
    if (status != CK_OK)
        return DrawFailed(status, "Draw");

    CKBgfxProgramRecord *rec = GetProgram(Draw->Program);
    if (!rec || !bgfx::isValid(rec->Handle) || rec->PixelShader == 0) {
        RecordInvalidSubmit((CKSTRING)"Draw", m_CurrentView, Draw->Program, (CKSTRING)"invalid_program");
        return DrawFailed(CKERR_INVALIDPARAMETER, "Draw.program");
    }

    CKERROR err = ApplyPipelineState();
    if (err != CK_OK)
        return DrawFailed(err, "Draw.state");
    err = BindGeometry(Draw);
    if (err != CK_OK)
        return DrawFailed(err, "Draw.geometry");

    // The device has CKFF_SAMPLER_SLOT_COUNT texture stages; the present
    // sampler (slot 16) shares stage 0 with fixed-function slot 0, which a
    // present draw never samples.
    const CKBOOL presentBound = m_Slots[CKRST_BACKEND_SLOT_PRESENT].Texture != 0;
    for (CKDWORD slot = 0; slot < CKRST_BACKEND_SLOT_COUNT; ++slot) {
        SlotBinding &binding = m_Slots[slot];
        if (slot == 0 && presentBound)
            continue;
        const CKDWORD stage = slot == CKRST_BACKEND_SLOT_PRESENT ? 0 : slot;
        if (binding.PendingZero) {
            binding.PendingZero = FALSE;
            err = BindTextureSlot(stage, slot, 0, NULL);
        } else if (binding.Texture) {
            err = BindTextureSlot(stage, slot, binding.Texture, binding.HasSampler ? &binding.Sampler : NULL);
        } else {
            continue;
        }
        if (err != CK_OK)
            return DrawFailed(err, "Draw.texture");
    }

    if (m_DrawMapSubmitActive)
        TraceSubmit(Draw->Program, rec->Handle, Draw->SortKey);
    RecordViewColorWrite(m_CurrentView, TRUE);
    bgfx::submit((bgfx::ViewId)m_CurrentView, rec->Handle, Draw->SortKey, BGFX_DISCARD_ALL);
    if (m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    ResetDebugBindings();
    ++m_FrameDraws;
    return CK_OK;
}

CKERROR CKBgfxBackend::Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                            CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect)
{
    if (!IsReady() || !m_PassOpen)
        return CKERR_INVALIDOPERATION;
    const CKERROR status = GetDeviceStatus();
    if (status != CK_OK)
        return status;
    CKBgfxTextureRecord *dst = GetTexture(DstTexture);
    CKBgfxTextureRecord *src = GetTexture(SrcTexture);
    if (!dst || !src)
        return CKERR_INVALIDPARAMETER;
    if (DstMip >= dst->MipCount || SrcMip >= src->MipCount)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD dstLayers = (dst->Flags & CKRST_TEXTURE_CUBEMAP) ? 6u : XMax((CKDWORD)1, dst->Depth >> DstMip);
    const CKDWORD srcLayers = (src->Flags & CKRST_TEXTURE_CUBEMAP) ? 6u : XMax((CKDWORD)1, src->Depth >> SrcMip);
    if (DstLayer >= dstLayers || SrcLayer >= srcLayers)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD dstWidth = XMax((CKDWORD)1, dst->Width >> DstMip);
    const CKDWORD dstHeight = XMax((CKDWORD)1, dst->Height >> DstMip);
    const CKDWORD srcWidth = XMax((CKDWORD)1, src->Width >> SrcMip);
    const CKDWORD srcHeight = XMax((CKDWORD)1, src->Height >> SrcMip);
    if (DstX >= dstWidth || DstY >= dstHeight ||
        (SrcRect && (SrcRect->left < 0 || SrcRect->top < 0 ||
                     SrcRect->right <= SrcRect->left ||
                     SrcRect->bottom <= SrcRect->top ||
                     (CKDWORD)SrcRect->right > srcWidth ||
                     (CKDWORD)SrcRect->bottom > srcHeight)))
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_DEVCAPS_BLIT) == 0 ||
        (dst->Flags & CKRST_TEXTURE_BLIT_DST) == 0 ||
        dst->Format != src->Format)
        return CKERR_NOTIMPLEMENTED;

    const CKDWORD srcX = SrcRect ? (CKDWORD)SrcRect->left : 0;
    const CKDWORD srcY = SrcRect ? (CKDWORD)SrcRect->top : 0;
    const CKDWORD copiedWidth = SrcRect ? (CKDWORD)(SrcRect->right - SrcRect->left) : srcWidth;
    const CKDWORD copiedHeight = SrcRect ? (CKDWORD)(SrcRect->bottom - SrcRect->top) : srcHeight;
    const CKDWORD actualCopiedWidth = XMin(copiedWidth, dstWidth - DstX);
    const CKDWORD actualCopiedHeight = XMin(copiedHeight, dstHeight - DstY);
    const CKBOOL fullDestination =
        DstX == 0 && DstY == 0 &&
        actualCopiedWidth == dstWidth && actualCopiedHeight == dstHeight;
    RecordViewColorWrite(m_CurrentView, FALSE);
    RecordTextureBlit(dst, DstMip, src, SrcMip, fullDestination);
    bgfx::blit((bgfx::ViewId)m_CurrentView,
               dst->Handle, (uint8_t)DstMip, (uint16_t)DstX, (uint16_t)DstY, (uint16_t)DstLayer,
               src->Handle, (uint8_t)SrcMip, (uint16_t)srcX, (uint16_t)srcY, (uint16_t)SrcLayer,
               (uint16_t)copiedWidth, (uint16_t)copiedHeight, 1);
    ++m_FrameBlits;
    return CK_OK;
}

CKERROR CKBgfxBackend::Present(CKBackendPresentMode Mode, CKDWORD *FrameNumber)
{
    if (FrameNumber)
        *FrameNumber = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    const CKERROR fatalError = GetDeviceStatus();
    if (fatalError != CK_OK)
        return fatalError;
    if (Mode != CKRST_BACKEND_PRESENT_IMMEDIATE && Mode != CKRST_BACKEND_PRESENT_VSYNC &&
        Mode != CKRST_BACKEND_PRESENT_PRESERVE)
        return CKERR_INVALIDPARAMETER;

    // A present-sync change needs bgfx::reset. Applying it to the frame being
    // submitted loses that frame's rendering (the reset recreates the swap
    // chain and the frame buffers while the frame renders), so the frame is
    // rendered with the old sync mode and the reset gets an empty frame of its
    // own right after it.
    const CKBOOL updatePresentSync = Mode != CKRST_BACKEND_PRESENT_PRESERVE;
    const CKBOOL vsync = Mode == CKRST_BACKEND_PRESENT_VSYNC;
    const CKBOOL resetAfterFrame = updatePresentSync && vsync != m_VSync;

    static int s_PresentSyncLogCount = 0;
    if (m_DebugLogPresentSync && s_PresentSyncLogCount < 64) {
        CKBgfxLogf("PresentSync",
                 "frame=%u mode=%d currentVSync=%d resetFlags=0x%X",
                 m_DebugFrameId, (int)Mode,
                 m_VSync ? 1 : 0, m_ResetFlags);
        ++s_PresentSyncLogCount;
    }

    DrawDebugOverlay();

    if (m_DrawMapActive &&
        CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME)) {
        CKBgfxLogf("FrameMap",
                   "End frame=%u passes=%u submits=%u parsed=%u missingAnnotations=%u rawPrimitive=%u markerOverwrite=%u markerStale=%u invalidSubmit=%u",
                   m_DebugFrameId,
                   m_FramePasses,
                   m_DebugSubmitSerial.load(std::memory_order_relaxed),
                   m_DebugParsedAnnotationCount.load(std::memory_order_relaxed),
                   m_DebugMissingAnnotationCount.load(std::memory_order_relaxed),
                   m_DebugRawPrimitiveCount.load(std::memory_order_relaxed),
                   m_DebugMarkerOverwriteCount.load(std::memory_order_relaxed),
                   m_DebugMarkerStaleCount.load(std::memory_order_relaxed),
                   m_DebugInvalidSubmitCount.load(std::memory_order_relaxed));
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
                               "frame=%u view=%u submits=%u name=%s",
                               m_DebugFrameId,
                               (unsigned)i,
                               viewSubmits,
                               m_DebugViewName[i]);
                }
            }
        }
    }
    if (m_DrawMapMarkerCaptureActive && m_LastMarker[0] != '\0') {
        m_DebugMarkerStaleCount.fetch_add(1, std::memory_order_relaxed);
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_MARKERS))
            CKBgfxLogf("MarkerStale", "frame=%u reason=present label=\"%s\"", m_DebugFrameId, m_LastMarker);
        m_LastMarker[0] = '\0';
    }

    CKDWORD submittedFrame = bgfx::frame();
    if (resetAfterFrame)
    {
        m_VSync = vsync;
        m_ResetFlags = CKBgfxBuildResetFlags(vsync, 0);
        bgfx::reset((uint32_t)m_Width, (uint32_t)m_Height, m_ResetFlags);
        submittedFrame = bgfx::frame();
    }
    if (FrameNumber)
        *FrameNumber = submittedFrame;

    // Views the previous frame used and this one did not keep their
    // configuration in bgfx; reset them so a later frame starts clean.
    for (CKDWORD view = m_NextView; view < m_LastFrameViewCount; ++view)
        bgfx::resetView((bgfx::ViewId)view);
    {
        VxMutexLock lock(m_ResourceStateMutex);
        for (CKDWORD view = m_NextView; view < m_LastFrameViewCount; ++view) {
            m_ViewFrameBuffer[view] = 0;
            m_ViewRect[view].left = m_ViewRect[view].top = m_ViewRect[view].right = m_ViewRect[view].bottom = 0;
            m_ViewClearFlags[view] = 0;
        }
        for (int i = 0; i < CKRST_MAX_RENDER_VIEWS; ++i)
            m_ViewClearRecorded[i] = FALSE;
    }
    m_LastFrameViewCount = m_NextView;
    m_NextView = 0;
    m_PassOpen = FALSE;
    m_FrameInProgress = FALSE;
    m_TransientVBCount = 0;
    m_TransientIBCount = 0;
    m_DrawErrorLogCount = 0;

    ++m_Stats.Frames;
    m_Stats.Passes = m_FramePasses;
    m_Stats.Draws = m_FrameDraws;
    m_Stats.Blits = m_FrameBlits;
    m_Stats.TextureUploads = m_FrameTextureUploads;
    m_Stats.BufferUploads = m_FrameBufferUploads;
    m_FramePasses = m_FrameDraws = m_FrameBlits = m_FrameTextureUploads = m_FrameBufferUploads = 0;
    if (const bgfx::Stats *s = bgfx::getStats()) {
        m_Stats.CpuTimeFrame = s->cpuTimeFrame;
        m_Stats.CpuTimerFreq = s->cpuTimerFreq;
        m_Stats.GpuTimeFrame = s->gpuTimeEnd - s->gpuTimeBegin;
        m_Stats.GpuTimerFreq = s->gpuTimerFreq;
        m_Stats.GpuMemoryMax = (CKDWORD)(s->gpuMemoryMax >> 10);
        m_Stats.GpuMemoryUsed = (CKDWORD)(s->gpuMemoryUsed >> 10);
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
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_FRAME))
            CKBgfxLogf("FrameMap", "Begin frame=%u", m_DebugFrameId);
    }
    return CK_OK;
}


void CKBgfxBackend::TraceSubmit(CKDWORD Program, bgfx::ProgramHandle ProgramHandle, CKDWORD Depth)
{
    if (!m_DrawMapSubmitActive)
        return;
    const CKRenderView View = m_CurrentView;
    const CKDWORD submitSerial = m_DebugSubmitSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    CKDWORD viewSubmitSerial = 0;
    CKDrawAnnotationParsed parsed;
    CKBOOL parsedLabel = FALSE;
    CKDWORD sourceIndex = CKDRAW_SOURCE_NONE;
    CKBgfxProgramRecord *programRecord = GetProgram(Program);
    CKDWORD stateHash = CKBgfxHashDrawState(m_CachedDrawState);
    CKDWORD stencilHash = CKBgfxHashStencil(m_State.StencilRef, m_State.StencilReadMask, m_State.StencilWriteMask);
    CKDWORD programHash = CKBgfxHashProgram(programRecord);
    uint64_t finalState = m_CachedBgfxState;
    char texFields[512];
    CKDWORD texOffset = 0;
    char vbFields[256];
    CKDWORD vbOffset = 0;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);

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
        viewSubmitSerial = m_DebugViewSubmitSerial[View].fetch_add(1, std::memory_order_relaxed) + 1;

    if (m_LastMarker[0] == '\0') {
        m_DebugMissingAnnotationCount.fetch_add(1, std::memory_order_relaxed);
        CKBgfxDrawMapTraceSubmitMiss((CKSTRING)"no_annotation",
                                      m_DebugFrameId,
                                      submitSerial,
                                      (unsigned)View,
                                      Program,
                                      (CKSTRING)"Submit",
                                      NULL);
    } else {
        parsedLabel = CKDrawAnnotationParseLabel(m_LastMarker, &parsed);
        if (parsedLabel) {
            m_DebugParsedAnnotationCount.fetch_add(1, std::memory_order_relaxed);
            sourceIndex = (CKDWORD)parsed.Source;
            if (sourceIndex < CKDRAW_SOURCE_COUNT)
                m_DebugSourceSubmitCount[sourceIndex].fetch_add(1, std::memory_order_relaxed);
            if (parsed.Source == CKDRAW_SOURCE_RAW_PRIMITIVE)
                m_DebugRawPrimitiveCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_DebugMissingAnnotationCount.fetch_add(1, std::memory_order_relaxed);
            CKBgfxDrawMapTraceSubmitMiss((CKSTRING)"malformed_annotation",
                                          m_DebugFrameId,
                                          submitSerial,
                                          (unsigned)View,
                                          Program,
                                          (CKSTRING)"Submit",
                                          m_LastMarker);
        }
    }

    {
        CKBgfxDrawMapStateTrace stateTrace;
        stateTrace.Frame = m_DebugFrameId;
        stateTrace.Submit = submitSerial;
        stateTrace.StateHash = stateHash;
        stateTrace.Low = m_CachedDrawState.Lo;
        stateTrace.Mid = m_CachedDrawState.Mid;
        stateTrace.High = m_CachedDrawState.Hi;
        stateTrace.BgfxStateLo = (CKDWORD)(finalState & 0xffffffffu);
        stateTrace.BgfxStateHi = (CKDWORD)(finalState >> 32);
        stateTrace.StencilHash = stencilHash;
        stateTrace.StencilRef = m_State.StencilRef;
        stateTrace.StencilReadMask = m_State.StencilReadMask;
        stateTrace.StencilWriteMask = m_State.StencilWriteMask;
        stateTrace.PointSize = m_PointSize;
        CKBgfxDrawMapTraceState(&stateTrace);
    }

    {
        CKBgfxDrawMapSubmitTrace submitTrace;
        submitTrace.Frame = m_DebugFrameId;
        submitTrace.Submit = submitSerial;
        submitTrace.View = (unsigned)View;
        submitTrace.ViewSubmit = viewSubmitSerial;
        submitTrace.Kind = (CKSTRING)"Submit";
        submitTrace.ViewMode = (CKSTRING)"Sequential";
        submitTrace.OrderGeneration = 0;
        submitTrace.OrderSequential = 1u;
        submitTrace.Depth = Depth;
        submitTrace.Program = Program;
        submitTrace.BgfxProgram = bgfx::isValid(ProgramHandle) ? ProgramHandle.idx : 0xffff;
        submitTrace.ProgramHash = programHash;
        submitTrace.SpecHash = m_DebugSpecializationValid
            ? m_DebugSpecializationHash : 0;
        submitTrace.ShaderProfile = (CKSTRING)CKBgfxShaderProfileName(m_Caps.ShaderProfile);
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
        submitTrace.Discard = (unsigned)BGFX_DISCARD_ALL;
        submitTrace.Extra0 = 0;
        submitTrace.Extra1 = 0;
        submitTrace.Extra2 = 0;
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

