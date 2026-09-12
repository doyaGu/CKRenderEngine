// CKBgfxBackend texture creation, update and resource lifetime.

// CKBgfxBackend resource creation, upload, destruction and readback.

#include "CKBgfxBackend.h"
#include "CKBgfxResources.h"
#include "CKBgfxInternal.h"
#include "CKBgfxDrawMapTrace.h"
#include "CKRasterizerValidation.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <functional>

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
    // All color textures may receive an ordered copy, including an upload
    // occurring between draws. This flag does not change their logical usage.
    uint64_t texFlags = BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_NONE;
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
        rec->Format, BGFX_TEXTURE_BLIT_DST, NULL);
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
                                           const VxImageDescEx *data,
    const std::function<CKERROR(CKDWORD, CKDWORD, CKDWORD, const bgfx::Memory *)> &upload)
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
        if (upload(level, mipW, mipH, mipMem) != CK_OK) {
            complete = FALSE;
            break;
        }
        previous = mipDesc;
    }
    delete[] previousGenerated;
    return complete;
}

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
                        CKBgfxDestroyRecord(rec);
                        return CKERR_OUTOFMEMORY;
                    }
                    if (!CKBgfxConvertBumpLuminancePixels(
                            pf, Data->Image, sourcePitch, w, h,
                            converted, (CKDWORD)w * 4u)) {
                        VxFree(converted);
                        CKBgfxDestroyRecord(rec);
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

    const CKDWORD texture = m_Resources->Textures.Insert(
        rec, m_CapsDesc.MaxTextures);
    if (texture == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
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
CKBOOL CKBgfxBackend::IsObjectAlive(CKDWORD Object, CKDWORD Type) const
{
    if (!m_BgfxInitialized || !m_Created || Object == 0)
        return FALSE;
    return m_Resources->IsAlive(Object, Type);
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
        CKBgfxTextureRecord *rec = m_Resources->Textures.Remove(Object);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceTextureMap((CKSTRING)"delete", Object, rec);
        CKBgfxDestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_VERTEXBUFFER: {
        CKBgfxVertexBufferRecord *rec = m_Resources->VertexBuffers.Remove(Object);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceBufferMap((CKSTRING)"delete", (CKSTRING)"vb", Object, rec->Handle.idx, rec->Layout, rec->VertexSize, 0, 0, 0);
        CKBgfxDestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_INDEXBUFFER: {
        CKBgfxIndexBufferRecord *rec = m_Resources->IndexBuffers.Remove(Object);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceBufferMap((CKSTRING)"delete", (CKSTRING)"ib", Object, rec->Handle.idx, 0, rec->Index32 ? 4u : 2u, 0, rec->Index32 ? 1u : 0u, 0);
        CKBgfxDestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_SHADER: {
        CKBgfxShaderRecord *rec = m_Resources->Shaders.Remove(Object);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        CKBgfxDestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_PROGRAM: {
        CKBgfxProgramRecord *rec = m_Resources->Programs.Remove(Object);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        TraceProgramMap((CKSTRING)"delete", Object, rec);
        CKBgfxDestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_VERTEXLAYOUT: {
        CKBgfxVertexLayoutRecord *rec = m_Resources->VertexLayouts.Remove(Object);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        CKBgfxDestroyRecord(rec);
        return CK_OK;
    }
    case CKRST_OBJ_FRAMEBUFFER: {
        CKBgfxFrameBufferRecord *rec = m_Resources->FrameBuffers.Remove(Object);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        CKBgfxDestroyRecord(rec);
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


CKERROR CKBgfxBackend::UploadTextureOrdered(bgfx::TextureHandle texture, bgfx::TextureFormat::Enum format,
    CKDWORD mip, CKDWORD x, CKDWORD y, CKDWORD layer, CKDWORD width, CKDWORD height,
    const bgfx::Memory *data, CKBOOL cube, CKBOOL volume)
{
    if (!m_FrameInProgress) {
        if (cube) bgfx::updateTextureCube(texture, 0, (uint8_t)layer, (uint8_t)mip,
                                         (uint16_t)x, (uint16_t)y, (uint16_t)width, (uint16_t)height, data);
        else if (volume) bgfx::updateTexture3D(texture, (uint8_t)mip, (uint16_t)x, (uint16_t)y,
                                              (uint16_t)layer, (uint16_t)width, (uint16_t)height, 1, data);
        else bgfx::updateTexture2D(texture, 0, (uint8_t)mip, (uint16_t)x, (uint16_t)y,
                                   (uint16_t)width, (uint16_t)height, data);
        return CK_OK;
    }
    // bgfx uploads run before all views. Upload into a unique staging texture,
    // then copy in its own sequential view at this call's position.
    bgfx::TextureHandle staging;
    if (cube) {
        staging = bgfx::createTextureCube((uint16_t)XMax(width, height), false, 1, format);
        if (!bgfx::isValid(staging)) {
            // bgfx owns upload memory after an update call, including a
            // zero-sized update, which consumes it without touching a handle.
            bgfx::updateTextureCube(texture, 0, 0, 0, 0, 0, 0, 0, data);
            return CKERR_OUTOFMEMORY;
        }
        bgfx::updateTextureCube(staging, 0, 0, 0, 0, 0, (uint16_t)width, (uint16_t)height, data);
    } else if (volume) {
        staging = bgfx::createTexture3D((uint16_t)width, (uint16_t)height, 1, false, format, 0, data);
    } else {
        staging = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, format, 0, data);
    }
    if (!bgfx::isValid(staging)) return CKERR_OUTOFMEMORY;
    if (m_NextView >= m_CapsDesc.MaxRenderViews || !(m_Caps.Features & CKRST_DEVCAPS_BLIT)) {
        bgfx::destroy(staging);
        return CKERR_OUTOFMEMORY;
    }
    const bgfx::ViewId view = (bgfx::ViewId)m_NextView++;
    bgfx::resetView(view);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewName(view, "ordered-upload");
    bgfx::blit(view, texture, (uint8_t)mip, (uint16_t)x, (uint16_t)y, (uint16_t)layer,
               staging, 0, 0, 0, 0, (uint16_t)width, (uint16_t)height, 1);
    bgfx::destroy(staging);
    m_DrawPassNeedsResume = m_PassOpen != FALSE;
    ++m_FrameBlits;
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
    const CKERROR upload = UploadTextureOrdered(rec->Handle, rec->Format, Mip, x, y, Face, w, h, mem, isCube, isVolume);

    if (Face == 0 && !isCube && !isVolume)
        RecordTextureWrite(rec, Mip, CKBGFX_ORIENTATION_TOP_LEFT,
                           fullMipUpdate ? TRUE : FALSE);

    if (samplerBaseMem) {
        const CKERROR baseUpload = UploadTextureOrdered(rec->SamplerBaseHandle, rec->Format, 0, x, y, 0, w, h, samplerBaseMem);
        if (baseUpload != CK_OK) return baseUpload;
        if (fullMipUpdate)
            rec->SamplerBaseValid = TRUE;
    }

    if (upload != CK_OK) return upload;

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
            if (!CKBgfxUpdateGeneratedMipMaps(rec, &rec->AutoMipBaseDesc,
                [&](CKDWORD level, CKDWORD mw, CKDWORD mh, const bgfx::Memory *bytes) {
                    return UploadTextureOrdered(rec->Handle, rec->Format, level, 0, 0, 0, mw, mh, bytes);
                })) {
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
