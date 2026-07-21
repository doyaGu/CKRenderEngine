#include "RCKTexture.h"

#include "CKBitmapReader.h"
#include "CKPathManager.h"
#include "CKStateChunk.h"
#include "CKRasterizer.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

CK_CLASSID RCKTexture::m_ClassID = CKCID_TEXTURE;

static CKBOOL HasAlphaFormat(const VxImageDescEx &desc) {
    return desc.AlphaMask != 0 || desc.Flags >= 0x13;
}

static CKBOOL IsSupportedObjectVideoFormat(VX_PIXELFORMAT format) {
    return format > UNKNOWN_PF && format <= _32_X8L8V8U8;
}

static VX_PIXELFORMAT ResolveObjectVideoFormat(VX_PIXELFORMAT requested, VX_PIXELFORMAT fallback) {
    if (IsSupportedObjectVideoFormat(requested))
        return requested;

    if (IsSupportedObjectVideoFormat(fallback))
        return fallback;

    return _32_ARGB8888;
}

static CKBOOL IsPowerOfTwo(CKDWORD x) {
    return x && !(x & (x - 1));
}

static void FindNearestFormatWithAlpha(CKRasterizerDriver *driver, VxImageDescEx &desc) {
    VxImageDescEx *bestFormat = nullptr;
    int bestDiff = 64;

    for (auto it = driver->m_TextureFormats.Begin(); it != driver->m_TextureFormats.End(); ++it) {
        if (it->Format.AlphaMask) {
            int diff = abs((int) it->Format.BitsPerPixel - (int) desc.BitsPerPixel);
            if (diff < bestDiff) {
                bestFormat = &it->Format;
                bestDiff = diff;
            }
        }
    }

    if (bestFormat) {
        desc.BitsPerPixel = bestFormat->BitsPerPixel;
        desc.RedMask = bestFormat->RedMask;
        desc.GreenMask = bestFormat->GreenMask;
        desc.BlueMask = bestFormat->BlueMask;
        desc.AlphaMask = bestFormat->AlphaMask;
    }
}

static CKBOOL SamePixelFormat(const VxImageDescEx &a, const VxImageDescEx &b) {
    return a.BitsPerPixel == b.BitsPerPixel &&
           a.RedMask == b.RedMask &&
           a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask &&
           a.AlphaMask == b.AlphaMask;
}

static void ReleaseBitmapProperties(CKBitmapProperties *&properties) {
    delete[] reinterpret_cast<CKBYTE *>(properties);
    properties = nullptr;
}

static CKBOOL BuildCopyUploadRegion(const VxRect *dest, int targetWidth, int targetHeight,
                                    VxImageDescEx &uploadDesc, CKRECT &region, CKRECT *&regionPtr) {
    regionPtr = nullptr;
    if (!dest)
        return TRUE;
    if (targetWidth <= 0 || targetHeight <= 0 || uploadDesc.Width <= 0 || uploadDesc.Height <= 0)
        return FALSE;

    int left = (int)dest->left;
    int top = (int)dest->top;
    int right = (int)dest->right;
    int bottom = (int)dest->bottom;
    if (right <= left || bottom <= top)
        return FALSE;

    int skipX = 0;
    int skipY = 0;
    if (left < 0) {
        skipX = -left;
        left = 0;
    }
    if (top < 0) {
        skipY = -top;
        top = 0;
    }
    if (left >= targetWidth || top >= targetHeight)
        return FALSE;
    if (right > targetWidth)
        right = targetWidth;
    if (bottom > targetHeight)
        bottom = targetHeight;

    int width = right - left;
    int height = bottom - top;
    if (width <= 0 || height <= 0)
        return FALSE;
    if (skipX >= uploadDesc.Width || skipY >= uploadDesc.Height)
        return FALSE;
    if (width > uploadDesc.Width - skipX)
        width = uploadDesc.Width - skipX;
    if (height > uploadDesc.Height - skipY)
        height = uploadDesc.Height - skipY;
    if (width <= 0 || height <= 0)
        return FALSE;

    const int bytesPerPixel = uploadDesc.BitsPerPixel > 0 ? uploadDesc.BitsPerPixel / 8 : 4;
    CKBYTE *image = (CKBYTE *)uploadDesc.Image;
    image += skipY * uploadDesc.BytesPerLine + skipX * bytesPerPixel;
    uploadDesc.Image = image;
    uploadDesc.Width = width;
    uploadDesc.Height = height;

    region.left = left;
    region.top = top;
    region.right = left + width;
    region.bottom = top + height;
    regionPtr = &region;
    return TRUE;
}

static CKBYTE *ConvertTextureImage(const VxImageDescEx &src, const VxImageDescEx &videoFormat,
                                   VxImageDescEx &uploadDesc) {
    if (src.Width <= 0 || src.Height <= 0 || src.BitsPerPixel <= 0 || videoFormat.BitsPerPixel <= 0)
        return nullptr;

    uploadDesc = videoFormat;
    uploadDesc.Width = src.Width;
    uploadDesc.Height = src.Height;
    uploadDesc.BytesPerLine = src.Width * uploadDesc.BitsPerPixel / 8;
    if (uploadDesc.BytesPerLine <= 0)
        return nullptr;
    const int imageSize = uploadDesc.BytesPerLine * uploadDesc.Height;
    if (imageSize <= 0)
        return nullptr;

    CKBYTE *converted = new CKBYTE[imageSize];
    uploadDesc.Image = converted;
    VxDoBlit(src, uploadDesc);
    return converted;
}

CKBOOL RCKTexture::Create(int Width, int Height, int BPP, int Slot) {
    int oldWidth = GetWidth();
    int oldHeight = GetHeight();

    CKBOOL result = CreateImage(Width, Height, BPP, Slot);
    if (!result)
        return FALSE;

    SetUserMipMapMode(FALSE);

    if (oldWidth != GetWidth() || oldHeight != GetHeight())
        FreeVideoMemory();

    return TRUE;
}

CKBOOL RCKTexture::LoadImage(CKSTRING Name, int Slot) {
    if (!Name)
        return FALSE;

    SetUserMipMapMode(0);

    int oldWidth = GetWidth();
    int oldHeight = GetHeight();

    XString path(Name);
    m_Context->GetPathManager()->ResolveFileName(path, BITMAP_PATH_IDX, -1);

    CKBOOL result = LoadSlotImage(path, Slot);
    if (!result)
        SetSlotFileName(Slot, Name);

    if (oldWidth != GetWidth() || oldHeight != GetHeight())
        FreeVideoMemory();

    return result;
}

CKBOOL RCKTexture::LoadMovie(CKSTRING Name) {
    SetUserMipMapMode(0);

    if (!Name)
        return FALSE;

    FreeVideoMemory();

    XString path(Name);
    m_Context->GetPathManager()->ResolveFileName(path, BITMAP_PATH_IDX, -1);

    CKBOOL result = LoadMovieFile(path);
    if (!result)
        m_Context->OutputToConsole("Movie can not be loaded...", TRUE);

    return result;
}

CKBOOL RCKTexture::SetAsCurrent(CKRenderContext *Dev, CKBOOL Clamping, int TextureStage) {
    RCKRenderContext *dev = static_cast<RCKRenderContext *>(Dev);
    if (!dev || !dev->m_RasterizerContext || !dev->m_RasterizerDriver)
        return FALSE;

    CKRasterizerContext *rstCtx = dev->m_RasterizerContext;
    if (!rstCtx->m_Driver)
        return FALSE;

    if ((m_BitmapFlags & CKBITMAPDATA_INVALID) != 0) {
        dev->m_FFPipeline.ResetTextureStage(TextureStage);
        return FALSE;
    }

    // Check driver support for clamping
    if (!(rstCtx->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_CLAMPEDGEALPHA))
        Clamping = FALSE;

    CKBOOL needsAlpha = (m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) != 0 || Clamping;
    CKBOOL needsCreate = FALSE;
    CKBOOL needsRestore = FALSE;
    CKBOOL isRenderTarget = FALSE;
    int result = 1;

    if (m_InVideoMemory) {
        if (m_RasterizerContext != rstCtx) {
            if (m_RasterizerContext)
                m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE);
            m_ObjectIndex = 0;
            m_InVideoMemory = FALSE;
            m_TextureFlags = 0;
            m_CachedMipMapCount = 0;
            memset(&m_VideoFormat, 0, sizeof(m_VideoFormat));
            needsCreate = TRUE;
        } else {
            isRenderTarget = (m_TextureFlags & CKRST_TEXTURE_RENDERTARGET) != 0;
        }
        if (m_InVideoMemory && !isRenderTarget &&
            ((!HasAlphaFormat(m_VideoFormat) && needsAlpha) ||
                m_CachedMipMapCount != m_MipMapLevel)) {
            rstCtx->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE);
            m_ObjectIndex = 0;
            m_InVideoMemory = FALSE;
            needsCreate = TRUE;
        }
    } else {
        needsCreate = TRUE;
    }

    if (needsCreate) {
        if (!SystemToVideoMemory(Dev, Clamping)) {
            dev->m_FFPipeline.ResetTextureStage(TextureStage);
            return FALSE;
        }
        isRenderTarget = (m_TextureFlags & CKRST_TEXTURE_RENDERTARGET) != 0;
    } else {
        // Check if texture needs to be restored
        if (!isRenderTarget) {
            needsRestore = ((m_BitmapFlags & CKBITMAPDATA_FORCERESTORE) != 0 || (Clamping && !(m_BitmapFlags & CKBITMAPDATA_CLAMPUPTODATE)));
        }
        m_RasterizerContext = rstCtx;
    }

    if (needsRestore && !Restore(Clamping)) {
        dev->m_FFPipeline.ResetTextureStage(TextureStage);
        return FALSE;
    }

    if (!isRenderTarget) {
        if ((m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) != 0 || Clamping) {
            dev->m_FFPipeline.SetRenderState(VXRENDERSTATE_ALPHAREF, 0);
            dev->m_FFPipeline.SetRenderState(VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER);
            dev->m_FFPipeline.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
            result = 2;
        } else {
            dev->m_FFPipeline.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
        }
    }
    dev->m_FFPipeline.SetTexture(TextureStage, m_ObjectIndex, m_TextureFlags);
    return result;
}

CKBOOL RCKTexture::Restore(CKBOOL Clamp) {
    if (!m_RasterizerContext)
        return FALSE;

    if (!m_InVideoMemory)
        return FALSE;

    if ((m_BitmapFlags & CKBITMAPDATA_INVALID) != 0)
        return FALSE;

    if (!m_RasterizerContext->m_Driver)
        return FALSE;

    m_BitmapFlags &= ~CKBITMAPDATA_CLAMPUPTODATE;

    CKBOOL result = FALSE;

    // Check for cube map case
    if (IsCubeMap() && GetSlotCount() == 6 && GetWidth() == GetHeight()) {
        if (m_InVideoMemory && (m_TextureFlags & CKRST_TEXTURE_CUBEMAP) != 0) {
            VxImageDescEx desc;
            GetImageDesc(desc);

            result = TRUE;
            for (int face = CKRST_CUBEFACE_XPOS; face < 6; ++face) {
                if (!m_Slots[face]) {
                    result = FALSE;
                    break;
                }
                CKBYTE *imageData = (CKBYTE *)m_Slots[face]->m_DataBuffer;
                if (!imageData) {
                    result = FALSE;
                    break;
                }

                desc.Image = imageData;
                if ((m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) != 0)
                    SetAlphaForTransparentColor(desc);

                VxImageDescEx uploadDesc = desc;
                CKBYTE *converted = nullptr;
                if (!SamePixelFormat(desc, m_VideoFormat)) {
                    converted = ConvertTextureImage(desc, m_VideoFormat, uploadDesc);
                    if (!converted) {
                        result = FALSE;
                        break;
                    }
                }

                if (m_RasterizerContext->UpdateTexture(m_ObjectIndex, 0, face, nullptr, &uploadDesc) != CK_OK) {
                    result = FALSE;
                }
                delete[] converted;
                if (!result)
                    break;
            }
            if (result)
                m_BitmapFlags &= ~CKBITMAPDATA_FORCERESTORE;
            return result;
        }
    }

    // Standard texture case
    if (m_CurrentSlot < 0 || m_CurrentSlot >= m_Slots.Size())
        return FALSE;

    if (!m_Slots[m_CurrentSlot])
        return FALSE;

    CKBYTE *imageData = (CKBYTE *)m_Slots[m_CurrentSlot]->m_DataBuffer;
    if (imageData) {
        VxImageDescEx desc;
        GetImageDesc(desc);
        desc.Image = imageData;

        // Handle transparency
        if ((m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) != 0)
            SetAlphaForTransparentColor(desc);

        // Handle clamping
        if ((m_RasterizerContext->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_CLAMPEDGEALPHA) != 0) {
            if (Clamp)
                SetBorderColorForClamp(desc);
        } else {
            m_BitmapFlags |= CKBITMAPDATA_CLAMPUPTODATE;
        }

        VxImageDescEx uploadDesc = desc;
        CKBYTE *converted = nullptr;
        if (!SamePixelFormat(desc, m_VideoFormat)) {
            converted = ConvertTextureImage(desc, m_VideoFormat, uploadDesc);
            if (!converted)
                return FALSE;
        }
        // Upload texture data via v2 API
        if (m_MipMaps && m_MipMapLevel) {
            if (m_RasterizerContext->UpdateTexture(m_ObjectIndex, 0, 0, nullptr, &uploadDesc) != CK_OK) {
                delete[] converted;
                return FALSE;
            }
            int mipCount = m_MipMaps->Size();
            for (int i = 0; i < mipCount; ++i) {
                VxImageDescEx *mipmap = m_MipMaps->At(i);
                if (!mipmap || !mipmap->Image) {
                    delete[] converted;
                    return FALSE;
                }

                VxImageDescEx uploadMipDesc = *mipmap;
                CKBYTE *convertedMip = nullptr;
                if (!SamePixelFormat(*mipmap, m_VideoFormat)) {
                    convertedMip = ConvertTextureImage(*mipmap, m_VideoFormat, uploadMipDesc);
                    if (!convertedMip) {
                        delete[] converted;
                        return FALSE;
                    }
                }

                CKERROR mipErr = m_RasterizerContext->UpdateTexture(m_ObjectIndex, i + 1, 0, nullptr, &uploadMipDesc);
                delete[] convertedMip;
                if (mipErr != CK_OK) {
                    delete[] converted;
                    return FALSE;
                }
            }
            result = TRUE;
        } else {
            result = (m_RasterizerContext->UpdateTexture(m_ObjectIndex, 0, 0, nullptr, &uploadDesc) == CK_OK);
            delete[] converted;
            if (result)
                m_BitmapFlags &= ~CKBITMAPDATA_FORCERESTORE;
            return result;
        }
        delete[] converted;
    }

    if (result)
        m_BitmapFlags &= ~CKBITMAPDATA_FORCERESTORE;
    return result;
}

CKBOOL RCKTexture::SystemToVideoMemory(CKRenderContext *Dev, CKBOOL Clamping) {
    RCKRenderContext *dev = static_cast<RCKRenderContext *>(Dev);
    if (!dev)
        return FALSE;

    if ((m_BitmapFlags & CKBITMAPDATA_INVALID) != 0)
        return FALSE;

    if (GetWidth() <= 0 || GetHeight() <= 0)
        return FALSE;

    if (!dev->m_RasterizerContext)
        return FALSE;

    if (!dev->m_RasterizerDriver)
        return FALSE;

    m_RasterizerContext = dev->m_RasterizerContext;
    if (!m_RasterizerContext->m_Driver)
        return FALSE;

    RCKRenderManager *rm = static_cast<RCKRenderManager *>(m_Context->GetRenderManager());
    const VX_PIXELFORMAT fallbackFormat =
        rm ? static_cast<VX_PIXELFORMAT>(rm->m_TextureVideoFormat.Value) : UNKNOWN_PF;

    CKTextureDesc desc;
    VxImageDescEx systemDesc;
    GetImageDesc(systemDesc);
    desc.Format = systemDesc;

    const VX_PIXELFORMAT videoFormat = ResolveObjectVideoFormat(
        m_DesiredVideoFormat,
        fallbackFormat);
    if (desc.Format.BitsPerPixel <= 0) {
        VxPixelFormat2ImageDesc(videoFormat, desc.Format);
    }
    desc.Format.Width = GetWidth();
    desc.Format.Height = GetHeight();
    desc.MipMapCount = 0;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;

    // Check texture cache management
    if (rm && rm->m_TextureCacheManagement.Value)
        desc.Flags |= CKRST_TEXTURE_MANAGED;
    if (!(rm && rm->m_DisableMipmap.Value) && m_MipMapLevel != 0) {
        if (m_MipMaps)
            desc.MipMapCount = (CKDWORD)m_MipMaps->Size() + 1;
        else
            desc.MipMapCount = (CKDWORD)-1;
    }

    // Check for cube map
    if ((m_BitmapFlags & CKBITMAPDATA_CUBEMAP) != 0 && GetSlotCount() == 6 && GetWidth() == GetHeight()) {
        desc.Flags |= CKRST_TEXTURE_CUBEMAP;
    }

    const VX_PIXELFORMAT actualVideoFormat = VxImageDesc2PixelFormat(desc.Format);
    if (actualVideoFormat >= _16_V8U8 && actualVideoFormat <= _32_X8L8V8U8)
        desc.Flags |= CKRST_TEXTURE_BUMPDUDV;
    if (actualVideoFormat == _16_L6V5U5 || actualVideoFormat == _32_X8L8V8U8)
        desc.Flags |= CKRST_TEXTURE_BUMPLUMINANCE;

    // If no alpha format and we need alpha, find nearest format with alpha
    if (!HasAlphaFormat(desc.Format)) {
        if ((m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) != 0 ||
            (Clamping && (m_RasterizerContext->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_CLAMPEDGEALPHA) != 0)) {
            FindNearestFormatWithAlpha(dev->m_RasterizerDriver, desc.Format);
        }
    }

    if (HasAlphaFormat(desc.Format))
        desc.Flags |= CKRST_TEXTURE_ALPHA;

    if (m_RasterizerContext->CreateTexture(&desc, nullptr, &m_ObjectIndex) == CK_OK) {
        m_InVideoMemory = TRUE;
        m_TextureFlags = desc.Flags;
        m_CachedMipMapCount = m_MipMapLevel;
        m_VideoFormat = desc.Format;
        if (Restore(Clamping))
            return TRUE;

        m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE);
        m_ObjectIndex = 0;
        m_InVideoMemory = FALSE;
        m_TextureFlags = 0;
        m_CachedMipMapCount = 0;
        memset(&m_VideoFormat, 0, sizeof(m_VideoFormat));
        m_ObjectIndex = 0;
        return FALSE;
    }

    return FALSE;
}

CKBOOL RCKTexture::FreeVideoMemory() {
    if (!m_RasterizerContext) {
        m_InVideoMemory = FALSE;
        m_ObjectIndex = 0;
        m_TextureFlags = 0;
        m_CachedMipMapCount = 0;
        memset(&m_VideoFormat, 0, sizeof(m_VideoFormat));
        return FALSE;
    }

    CKBOOL result = FALSE;
    if (m_InVideoMemory) {
        result = m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE) == CK_OK;
        m_ObjectIndex = 0;
    }
    m_InVideoMemory = FALSE;
    m_TextureFlags = 0;
    m_CachedMipMapCount = 0;
    memset(&m_VideoFormat, 0, sizeof(m_VideoFormat));
    return result;
}

CKBOOL RCKTexture::IsInVideoMemory() {
    if (!m_RasterizerContext)
        return FALSE;

    return m_InVideoMemory;
}

CKBOOL RCKTexture::CopyContext(CKRenderContext *ctx, VxRect *Src, VxRect *Dest, int CubeMapFace) {
    if (!ctx)
        return FALSE;

    RCKRenderContext *rctx = static_cast<RCKRenderContext *>(ctx);
    if (!rctx->m_RasterizerContext || rctx->m_RasterizerContext != m_RasterizerContext)
        return FALSE;
    if (!m_RasterizerContext || !m_InVideoMemory)
        return FALSE;
    if (CubeMapFace < CKRST_CUBEFACE_XPOS || CubeMapFace > CKRST_CUBEFACE_ZNEG)
        return FALSE;
    if (!IsCubeMap() && CubeMapFace != CKRST_CUBEFACE_XPOS)
        return FALSE;
    if (IsCubeMap() && (GetSlotCount() != 6 || GetWidth() != GetHeight()))
        return FALSE;

    return rctx->QueueTextureCopy(this, Src, Dest, CubeMapFace);
}

CKBOOL RCKTexture::ApplyContextCopy(RCKRenderContext *context,
                                    const VxImageDescEx &source,
                                    const VxRect *destination,
                                    int cubeMapFace) {
    if (!context || !context->m_RasterizerContext ||
        context->m_RasterizerContext != m_RasterizerContext ||
        !m_InVideoMemory)
        return FALSE;
    if (cubeMapFace < CKRST_CUBEFACE_XPOS || cubeMapFace > CKRST_CUBEFACE_ZNEG)
        return FALSE;
    if (!IsCubeMap() && cubeMapFace != CKRST_CUBEFACE_XPOS)
        return FALSE;

    VxImageDescEx uploadDesc = source;
    CKBYTE *converted = nullptr;
    if (!SamePixelFormat(source, m_VideoFormat)) {
        converted = ConvertTextureImage(source, m_VideoFormat, uploadDesc);
        if (!converted)
            return FALSE;
    }

    CKRECT region;
    CKRECT *regionPtr = nullptr;
    if (!BuildCopyUploadRegion(destination, GetWidth(), GetHeight(),
                               uploadDesc, region, regionPtr)) {
        delete[] converted;
        return FALSE;
    }

    CKERROR err = m_RasterizerContext->UpdateTexture(
        m_ObjectIndex, 0, (CKDWORD)cubeMapFace, regionPtr, &uploadDesc);
    delete[] converted;
    return err == CK_OK;
}

CKBOOL RCKTexture::UseMipmap(int UseMipMap) {
    CKDWORD mipMapLevel = 0;
    if (UseMipMap == 1 || UseMipMap < 0) {
        mipMapLevel = (CKDWORD)-1;
    } else if (UseMipMap > 1) {
        mipMapLevel = (CKDWORD)UseMipMap;
    }

    if (m_MipMapLevel != mipMapLevel)
        FreeVideoMemory();

    m_MipMapLevel = mipMapLevel;

    return TRUE;
}

int RCKTexture::GetMipmapCount() {
    return m_MipMapLevel;
}

CKBOOL RCKTexture::GetVideoTextureDesc(VxImageDescEx &desc) {
    if (!m_RasterizerContext || !m_InVideoMemory)
        return FALSE;

    desc = m_VideoFormat;
    return TRUE;
}

VX_PIXELFORMAT RCKTexture::GetVideoPixelFormat() {
    if (!m_RasterizerContext || !m_InVideoMemory)
        return UNKNOWN_PF;

    return VxImageDesc2PixelFormat(m_VideoFormat);
}

CKBOOL RCKTexture::GetSystemTextureDesc(VxImageDescEx &desc) {
    return GetImageDesc(desc);
}

void RCKTexture::SetDesiredVideoFormat(VX_PIXELFORMAT Format) {
    if (m_DesiredVideoFormat != Format) {
        m_DesiredVideoFormat = Format;
        FreeVideoMemory();
    }
}

VX_PIXELFORMAT RCKTexture::GetDesiredVideoFormat() {
    return m_DesiredVideoFormat;
}

CKBOOL RCKTexture::SetUserMipMapMode(CKBOOL UserMipmap) {
    if (UserMipmap) {
        if (!m_MipMaps) {
            // User mipmap mode requires single slot and power-of-2 dimensions
            if (GetSlotCount() != 1)
                return FALSE;
            if (!IsPowerOfTwo(GetWidth()))
                return FALSE;
            if (!IsPowerOfTwo(GetHeight()))
                return FALSE;

            m_MipMaps = new XClassArray<VxImageDescEx>();

            // Calculate mipmap count
            CKDWORD maxDim = (GetWidth() > GetHeight()) ? GetWidth() : GetHeight();
            int mipCount = 0;
            while (maxDim > 1) {
                maxDim >>= 1;
                ++mipCount;
            }

            m_MipMaps->Resize(mipCount);

            VxImageDescEx mipDesc;
            mipDesc.AlphaMask = A_MASK;
            mipDesc.RedMask = R_MASK;
            mipDesc.GreenMask = G_MASK;
            mipDesc.BlueMask = B_MASK;
            mipDesc.BitsPerPixel = 32;
            mipDesc.Width = GetWidth();
            mipDesc.Height = GetHeight();
            mipDesc.BytesPerLine = GetWidth() * 4;

            for (int i = 0; i < mipCount; ++i) {
                mipDesc.Width = mipDesc.Width > 1 ? (mipDesc.Width >> 1) : 1;
                mipDesc.Height = mipDesc.Height > 1 ? (mipDesc.Height >> 1) : 1;
                mipDesc.BytesPerLine = mipDesc.Width * 4;
                mipDesc.Image = new CKBYTE[mipDesc.Width * mipDesc.Height * 4];
                memset(mipDesc.Image, 0, mipDesc.Width * mipDesc.Height * 4);

                VxImageDescEx *mipmap = m_MipMaps->At(i);
                *mipmap = mipDesc;
            }
            FreeVideoMemory();
        }
        UseMipmap(TRUE);
        return TRUE;
    } else {
        // Clean up user mipmaps
        if (m_MipMaps) {
            int count = m_MipMaps->Size();
            for (int i = 0; i < count; ++i) {
                VxImageDescEx *mipmap = m_MipMaps->At(i);
                delete[] mipmap->Image;
                mipmap->Image = nullptr;
            }
            delete m_MipMaps;
            m_MipMaps = nullptr;
            FreeVideoMemory();
        }
        return TRUE;
    }
}

CKBOOL RCKTexture::GetUserMipMapLevel(int Level, VxImageDescEx &ResultImage) {
    if (!m_MipMaps)
        return FALSE;

    if (Level < 0 || Level >= m_MipMaps->Size())
        return FALSE;

    ResultImage = *m_MipMaps->At(Level);
    return TRUE;
}

int RCKTexture::GetRstTextureIndex() {
    return m_ObjectIndex;
}

CKBOOL RCKTexture::EnsureRenderTarget(CKRenderContext *Dev, CKBOOL ReadBack) {
    RCKRenderContext *dev = static_cast<RCKRenderContext *>(Dev);
    if (!dev || !dev->m_RasterizerContext || !dev->m_RasterizerDriver)
        return FALSE;
    if (!dev->m_RasterizerContext->m_Driver)
        return FALSE;
    if ((m_BitmapFlags & CKBITMAPDATA_INVALID) != 0)
        return FALSE;
    if (GetWidth() <= 0 || GetHeight() <= 0)
        return FALSE;
    const CKBOOL isCubeTarget = IsCubeMap() && GetSlotCount() == 6 && GetWidth() == GetHeight();

    const CKDWORD requiredFlags = CKRST_TEXTURE_RENDERTARGET |
                                  (isCubeTarget ? CKRST_TEXTURE_CUBEMAP : 0) |
                                  (ReadBack ? CKRST_TEXTURE_READBACK : 0);
    if (m_InVideoMemory &&
        m_RasterizerContext == dev->m_RasterizerContext &&
        (m_TextureFlags & requiredFlags) == requiredFlags) {
        return TRUE;
    }

    if (m_InVideoMemory && m_RasterizerContext)
        m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE);
    m_ObjectIndex = 0;

    m_RasterizerContext = dev->m_RasterizerContext;
    m_InVideoMemory = FALSE;
    m_TextureFlags = 0;
    m_CachedMipMapCount = 0;
    memset(&m_VideoFormat, 0, sizeof(m_VideoFormat));

    CKTextureDesc desc;
    RCKRenderManager *rm = static_cast<RCKRenderManager *>(m_Context->GetRenderManager());
    const VX_PIXELFORMAT fallbackFormat =
        rm ? static_cast<VX_PIXELFORMAT>(rm->m_TextureVideoFormat.Value) : UNKNOWN_PF;
    const VX_PIXELFORMAT videoFormat = ResolveObjectVideoFormat(
        m_DesiredVideoFormat,
        fallbackFormat);
    VxPixelFormat2ImageDesc(videoFormat, desc.Format);
    desc.Format.Width = GetWidth();
    desc.Format.Height = GetHeight();
    desc.MipMapCount = 1;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | requiredFlags;

    if (HasAlphaFormat(desc.Format))
        desc.Flags |= CKRST_TEXTURE_ALPHA;

    if (m_RasterizerContext->CreateTexture(&desc, nullptr, &m_ObjectIndex) != CK_OK)
        return FALSE;

    m_InVideoMemory = TRUE;
    m_TextureFlags = desc.Flags;
    m_CachedMipMapCount = desc.MipMapCount;
    m_VideoFormat = desc.Format;
    return TRUE;
}

RCKTexture::RCKTexture(CKContext *Context, CKSTRING name) : CKTexture(Context, name) {
    RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
    m_DesiredVideoFormat = static_cast<VX_PIXELFORMAT>(rm->m_TextureVideoFormat.Value);
    m_MipMapLevel = 0;
    m_RasterizerContext = nullptr;
    m_MipMaps = nullptr;
    m_ObjectIndex = 0;
    m_InVideoMemory = FALSE;
    m_TextureFlags = 0;
    m_CachedMipMapCount = 0;
    memset(&m_VideoFormat, 0, sizeof(m_VideoFormat));
}

RCKTexture::~RCKTexture() {
    FreeVideoMemory();
    RCKTexture::SetUserMipMapMode(FALSE);
}

CK_CLASSID RCKTexture::GetClassID() {
    return m_ClassID;
}

CKStateChunk *RCKTexture::Save(CKFile *file, CKDWORD flags) {
    CKStateChunk *baseChunk = CKBeObject::Save(file, flags);
    if (!file && !(flags & CK_STATESAVE_OLDTEXONLY))
        return baseChunk;

    CKStateChunk *chunk = CreateCKStateChunk(CKCID_TEXTURE, file);
    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    CKDWORD identifiers[4] = {
        CK_STATESAVE_TEXAVIFILENAME, // 0x1000
        CK_STATESAVE_TEXREADER,      // 0x100000
        CK_STATESAVE_TEXCOMPRESSED,  // 0x20000
        CK_STATESAVE_TEXFILENAMES    // 0x10000
    };
    DumpToChunk(chunk, m_Context, file, identifiers);

    if (m_PickThreshold) {
        chunk->WriteIdentifier(CK_STATESAVE_PICKTHRESHOLD);
        chunk->WriteInt(m_PickThreshold);
    }

    chunk->WriteIdentifier(CK_STATESAVE_OLDTEXONLY);
    CKDWORD dword = (CKBYTE) m_MipMapLevel;
    dword |= (m_SaveOptions << 16);
    if (IsTransparent())
        dword |= 0x100;
    if (m_BitmapFlags & CKBITMAPDATA_CUBEMAP)
        dword |= 0x400;
    if (m_DesiredVideoFormat != UNKNOWN_PF)
        dword |= 0x200;

    chunk->WriteDword(dword);
    chunk->WriteDword(GetTransparentColor());

    if (GetSlotCount() > 1)
        chunk->WriteInt(GetCurrentSlot());

    if (m_DesiredVideoFormat != UNKNOWN_PF)
        chunk->WriteDword(m_DesiredVideoFormat);

    if (m_SaveProperties) {
        chunk->WriteIdentifier(CK_STATESAVE_TEXSAVEFORMAT);
        chunk->WriteBuffer(m_SaveProperties->m_Size, m_SaveProperties);
    }

    if (m_MipMaps) {
        chunk->WriteIdentifier(CK_STATESAVE_USERMIPMAP);
        chunk->WriteInt(m_MipMaps->Size());
        int count = m_MipMaps->Size();
        for (int i = 0; i < count; ++i) {
            VxImageDescEx *mipmap = m_MipMaps->At(i);
            chunk->WriteRawBitmap(*mipmap);
        }
    }

    if (GetClassID() == CKCID_TEXTURE)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

CKERROR RCKTexture::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    CKBeObject::Load(chunk, file);

    CKDWORD identifiers[5] = {
        CK_STATESAVE_TEXAVIFILENAME,
        CK_STATESAVE_TEXREADER,
        CK_STATESAVE_TEXCOMPRESSED,
        CK_STATESAVE_TEXFILENAMES,
        CK_STATESAVE_TEXBITMAPS
    };
    ReadFromChunk(chunk, m_Context, file, identifiers);

    if (chunk->GetDataVersion() < CHUNK_VERSION2) {
        if (chunk->SeekIdentifier(CK_STATESAVE_TEXTRANSPARENT)) {
            CKDWORD color = chunk->ReadDword();
            SetTransparentColor(color);
            CKBOOL transparency = (CKBOOL) chunk->ReadDword();
            SetTransparent(transparency);
        }
        if (chunk->SeekIdentifier(CK_STATESAVE_TEXCURRENTIMAGE)) {
            int slot = chunk->ReadInt();
            SetCurrentSlot(slot);
        }
        int size = chunk->SeekIdentifierAndReturnSize(CK_STATESAVE_USERMIPMAP);
        if (size > 0) {
            CKBOOL useMipMap = chunk->ReadInt();
            UseMipmap(useMipMap);
            if (size > sizeof(CKDWORD)) {
                VxImageDescEx desc;
                chunk->ReadAndFillBuffer(&desc.Width);
                m_DesiredVideoFormat = VxImageDesc2PixelFormat(desc);
            }
        }
        if (chunk->SeekIdentifier(CK_STATESAVE_TEXSYSTEMCACHING)) {
            m_SaveOptions = static_cast<CK_BITMAP_SAVEOPTIONS>(chunk->ReadDword());
            CKBitmapProperties *format = nullptr;
            chunk->ReadBuffer((void **) &format);
            if (format) {
                if (chunk->GetDataVersion() > CHUNK_VERSION3) {
                    SetSaveFormat(format);
                }
                CKDeletePointer(format);
            }
        }
    } else {
        int size = chunk->SeekIdentifierAndReturnSize(CK_STATESAVE_OLDTEXONLY);
        if (size > 0) {
            CKDWORD dword = chunk->ReadDword();
            CKDWORD mipMapLevel = dword & 0xFF;
            m_MipMapLevel = (mipMapLevel == 0xFF) ? (CKDWORD)-1 : mipMapLevel;
            m_SaveOptions = static_cast<CK_TEXTURE_SAVEOPTIONS>((dword & 0xFF0000) >> 16);
            SetCubeMap((dword & 0x400) != 0);
            SetTransparent((dword & 0x100) != 0);
            int slot = 0;
            size -= 4;
            if (size == 3 * sizeof(CKDWORD)) {
                CKDWORD color = chunk->ReadDword();
                SetTransparentColor(color);
                slot = chunk->ReadInt();
                m_DesiredVideoFormat = static_cast<VX_PIXELFORMAT>(chunk->ReadDword());
            } else if (size == 2 * sizeof(CKDWORD)) {
                if (GetSlotCount() <= 1 || (dword & 0x200) == 0) {
                    CKDWORD color = chunk->ReadDword();
                    SetTransparentColor(color);
                }
                if (GetSlotCount() > 1) {
                    slot = chunk->ReadInt();
                }
                if ((dword & 0x200) != 0) {
                    m_DesiredVideoFormat = static_cast<VX_PIXELFORMAT>(chunk->ReadDword());
                }
            } else if (size == sizeof(CKDWORD)) {
                if ((dword & 0x200) != 0) {
                    m_DesiredVideoFormat = static_cast<VX_PIXELFORMAT>(chunk->ReadDword());
                } else if (GetSlotCount() <= 1) {
                    CKDWORD color = chunk->ReadDword();
                    SetTransparentColor(color);
                } else {
                    slot = chunk->ReadInt();
                }
            }
            SetCurrentSlot(slot);
        }

        if (chunk->SeekIdentifier(CK_STATESAVE_USERMIPMAP)) {
            SetUserMipMapMode(TRUE);
            if (m_MipMaps) {
                int count = m_MipMaps->Size();
                if (count == chunk->ReadInt()) {
                    for (int i = 0; i < count; ++i) {
                        VxImageDescEx desc;
                        CKBYTE *data = chunk->ReadRawBitmap(desc);
                        if (data) {
                            desc.Image = data;
                            VxImageDescEx *mipmap = m_MipMaps->At(i);
                            mipmap->Set(desc);
                            mipmap->Image = new CKBYTE[desc.BytesPerLine * desc.Height];
                            VxDoBlitUpsideDown(desc, *mipmap);
                            CKDeletePointer(data);
                        }
                    }
                }
            }
        }

        if (chunk->SeekIdentifier(CK_STATESAVE_PICKTHRESHOLD)) {
            m_PickThreshold = chunk->ReadInt();
        }

        if (chunk->SeekIdentifier(CK_STATESAVE_TEXSAVEFORMAT)) {
            CKBitmapProperties *format = nullptr;
            chunk->ReadBuffer((void **) &format);
            if (format) {
                if (chunk->GetDataVersion() > CHUNK_VERSION3) {
                    SetSaveFormat(format);
                }
                CKDeletePointer(format);
            }
        }

        if (!IsSupportedObjectVideoFormat(m_DesiredVideoFormat)) {
            RCKRenderManager *rm = static_cast<RCKRenderManager *>(m_Context->GetRenderManager());
            m_DesiredVideoFormat = ResolveObjectVideoFormat(
                m_DesiredVideoFormat,
                rm ? static_cast<VX_PIXELFORMAT>(rm->m_TextureVideoFormat.Value) : UNKNOWN_PF);
        }
    }

    return CK_OK;
}

int RCKTexture::GetMemoryOccupation() {
    int size = CKBeObject::GetMemoryOccupation() + (sizeof(RCKTexture) - sizeof(CKBeObject));
    size += GetWidth() * GetHeight() * GetSlotCount() * sizeof(CKDWORD);
    return size;
}

CKERROR RCKTexture::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = CKBeObject::Copy(o, context);
    if (err != CK_OK)
        return err;

    RCKTexture *src = static_cast<RCKTexture *>(&o);

    context.GetClassDependencies(CKCID_TEXTURE);

    // Copy movie if present
    FreeVideoMemory();
    SetUserMipMapMode(FALSE);
    SetMovieInfo(nullptr);
    ReleaseAllSlots();
    ReleaseBitmapProperties(m_SaveProperties);

    CKBOOL copiedMovie = FALSE;
    CKSTRING srcMovie = src->GetMovieFileName();
    if (srcMovie && *srcMovie)
        copiedMovie = LoadMovie(srcMovie);

    // Copy save properties if present
    if (src->m_SaveProperties)
        m_SaveProperties = CKCopyBitmapProperties(src->m_SaveProperties);

    // Copy bitmap data members
    m_Width = src->m_Width;
    m_Height = src->m_Height;
    m_CurrentSlot = src->m_CurrentSlot;
    m_BitmapFlags = src->m_BitmapFlags;
    m_TransColor = src->m_TransColor;
    m_SaveOptions = src->m_SaveOptions;
    m_PickThreshold = src->m_PickThreshold;

    // Copy texture-specific members
    m_DesiredVideoFormat = src->m_DesiredVideoFormat;
    m_MipMapLevel = src->m_MipMapLevel;

    // Copy slot count and contents
    if (copiedMovie) {
        SetCurrentSlot(src->GetCurrentSlot());
    } else {
        const int slotCount = src->m_MovieInfo ? src->m_Slots.Size() : src->GetSlotCount();
        SetSlotCount(slotCount);

        const size_t imageSize = (m_Width > 0 && m_Height > 0)
            ? (size_t)m_Width * (size_t)m_Height * 4u
            : 0u;
        for (int i = 0; i < GetSlotCount(); ++i) {
            CKBYTE *srcImage = src->LockSurfacePtr(i);
            SetSlotFileName(i, src->GetSlotFileName(i));

            if (srcImage) {
                if (imageSize > 0) {
                    CreateImage(m_Width, m_Height, 32, i);
                    CKBYTE *dstImage = LockSurfacePtr(i);
                    if (dstImage) {
                        memcpy(dstImage, srcImage, imageSize);
                        ReleaseSurfacePtr(i);
                    }
                }
                src->ReleaseSurfacePtr(i);
            }
        }
    }

    if (src->m_MipMaps) {
        m_MipMaps = new XClassArray<VxImageDescEx>();
        int mipCount = src->m_MipMaps->Size();
        m_MipMaps->Resize(mipCount);
        for (int i = 0; i < mipCount; ++i) {
            VxImageDescEx *srcMip = src->m_MipMaps->At(i);
            VxImageDescEx *dstMip = m_MipMaps->At(i);
            *dstMip = *srcMip;
            dstMip->Image = nullptr;
            if (srcMip->Image && srcMip->BytesPerLine > 0 && srcMip->Height > 0) {
                const int byteCount = srcMip->BytesPerLine * srcMip->Height;
                dstMip->Image = new CKBYTE[byteCount];
                memcpy(dstMip->Image, srcMip->Image, byteCount);
            }
        }
    }

    m_BitmapFlags |= CKBITMAPDATA_FORCERESTORE;
    return CK_OK;
}

CKSTRING RCKTexture::GetClassName() {
    return "Texture";
}

int RCKTexture::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKTexture::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKTexture::Register() {
    CKPARAMETERFROMCLASS(RCKTexture, CKPGUID_TEXTURE);
    CKCLASSDEFAULTOPTIONS(RCKTexture, CK_GENERALOPTIONS_CANUSECURRENTOBJECT);
}

CKTexture *RCKTexture::CreateInstance(CKContext *Context) {
    return new RCKTexture(Context);
}
