#include "RCKTexture.h"

#include "CKPathManager.h"
#include "CKRasterizer.h"
#include "CKStateChunk.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "VxMath.h"

CK_CLASSID RCKTexture::m_ClassID = CKCID_TEXTURE;

static CKBOOL HasAlphaFormat(const VxImageDescEx &desc) {
    return desc.AlphaMask != 0 || desc.Flags >= 0x13;
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

CKBOOL RCKTexture::Create(int Width, int Height, int BPP, int Slot) {
    int oldWidth = GetWidth();
    int oldHeight = GetHeight();

    CKBOOL result = CKBitmapData::CreateImage(Width, Height, BPP, Slot);

    if (oldWidth != GetWidth() || oldHeight != GetHeight())
        FreeVideoMemory();

    return result;
}

CKBOOL RCKTexture::LoadImage(CKSTRING Name, int Slot) {
    XString path(Name);
    m_Context->GetPathManager()->ResolveFileName(path, BITMAP_PATH_IDX, -1);

    if (!CKBitmapData::LoadSlotImage(path, Slot))
        return FALSE;

    FreeVideoMemory();
    return TRUE;
}

CKBOOL RCKTexture::LoadMovie(CKSTRING Name) {
    XString path(Name);
    m_Context->GetPathManager()->ResolveFileName(path, BITMAP_PATH_IDX, -1);

    if (!CKBitmapData::LoadMovieFile(path))
        return FALSE;

    FreeVideoMemory();
    return TRUE;
}

CKBOOL RCKTexture::SetAsCurrent(CKRenderContext *Dev, CKBOOL Clamping, int TextureStage) {
    RCKRenderContext *dev = static_cast<RCKRenderContext *>(Dev);
    CKRasterizerContext *rstCtx = dev->m_RasterizerContext;

    // Check if texture is invalid
    if ((m_BitmapFlags & 1) != 0) {
        rstCtx->SetTexture(0, TextureStage);
        return FALSE;
    }

    // Check driver support for clamping
    if ((rstCtx->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_CLAMPEDGEALPHA) == 0)
        Clamping = FALSE;

    CKBOOL needsAlpha = (m_BitmapFlags & 2) != 0 || Clamping;
    CKBOOL needsCreate = FALSE;
    CKBOOL needsRestore = FALSE;
    CKBOOL isRenderTarget = FALSE;
    int result = 1;

    CKTextureDesc *texDesc = rstCtx->GetTextureData(m_ObjectIndex);
    if (texDesc) {
        isRenderTarget = (texDesc->Flags & CKRST_TEXTURE_RENDERTARGET) != 0;

        // Check if texture needs to be recreated
        if (!isRenderTarget &&
            ((!HasAlphaFormat(texDesc->Format) && needsAlpha) ||
                texDesc->MipMapCount != m_MipMapLevel)) {
            rstCtx->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE);
            needsCreate = TRUE;
        }
    } else {
        needsCreate = TRUE;
    }

    if (needsCreate) {
        SystemToVideoMemory(Dev, Clamping);
    } else {
        // Check if texture needs to be restored
        if (!isRenderTarget) {
            needsRestore = ((m_BitmapFlags & 4) != 0 || (Clamping && (m_BitmapFlags & 8) == 0));
        }
        m_RasterizerContext = rstCtx;
    }

    if (needsRestore)
        Restore(Clamping);

    if (!isRenderTarget) {
        if ((m_BitmapFlags & 2) != 0 || Clamping) {
            rstCtx->SetRenderState(VXRENDERSTATE_ALPHAREF, 0);
            rstCtx->SetRenderState(VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER);
            rstCtx->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
            result = 2;
        } else {
            rstCtx->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
        }
    }

    rstCtx->SetTexture(m_ObjectIndex, TextureStage);
    return result;
}

CKBOOL RCKTexture::Restore(CKBOOL Clamp) {
    if (!m_RasterizerContext)
        return FALSE;

    if ((m_BitmapFlags & 1) != 0)
        return FALSE;

    // Clear clamping-related flags
    m_BitmapFlags &= ~0x0C;

    CKBOOL result = FALSE;

    // Check for cube map case
    if ((m_BitmapFlags & 0x10) != 0 &&
        GetSlotCount() == 6 &&
        GetWidth() == GetHeight()) {
        CKTextureDesc *texDesc = m_RasterizerContext->GetTextureData(m_ObjectIndex);
        if (texDesc && (texDesc->Flags & CKRST_TEXTURE_CUBEMAP) != 0) {
            VxImageDescEx desc;
            GetImageDesc(desc);

            for (int face = CKRST_CUBEFACE_XPOS; face < 6; ++face) {
                CKBYTE *imageData = LockSurfacePtr(face);
                if (imageData) {
                    desc.Image = imageData;
                    result |= m_RasterizerContext->LoadCubeMapTexture(
                        m_ObjectIndex, desc, static_cast<CKRST_CUBEFACE>(face), -1);
                    ReleaseSurfacePtr(face);
                }
            }
            return result;
        }
    }

    // Standard texture case
    CKBYTE *imageData = LockSurfacePtr(GetCurrentSlot());
    if (imageData) {
        VxImageDescEx desc;
        GetImageDesc(desc);
        desc.Image = imageData;

        // Handle transparency
        if ((m_BitmapFlags & 2) != 0)
            SetAlphaForTransparentColor(desc);

        // Handle clamping
        if ((m_RasterizerContext->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_CLAMPEDGEALPHA) !=
            0) {
            if (Clamp)
                SetBorderColorForClamp(desc);
        } else {
            m_BitmapFlags |= 8;
        }

        // Load mipmaps if present
        if (m_MipMaps && m_MipMapLevel) {
            result = m_RasterizerContext->LoadTexture(m_ObjectIndex, desc, 0);
            int mipCount = m_MipMaps->Size();
            for (int i = 0; i < mipCount; ++i) {
                VxImageDescEx *mipmap = m_MipMaps->At(i);
                result = m_RasterizerContext->LoadTexture(m_ObjectIndex, *mipmap, i + 1);
            }
        } else {
            result = m_RasterizerContext->LoadTexture(m_ObjectIndex, desc, -1);
        }

        ReleaseSurfacePtr(GetCurrentSlot());
    }

    return result;
}

CKBOOL RCKTexture::SystemToVideoMemory(CKRenderContext *Dev, CKBOOL Clamping) {
    RCKRenderContext *dev = static_cast<RCKRenderContext *>(Dev);

    if ((m_BitmapFlags & 1) != 0)
        return FALSE;

    if (!dev->m_RasterizerContext)
        return FALSE;

    if (!dev->m_RasterizerDriver)
        return FALSE;

    m_RasterizerContext = dev->m_RasterizerContext;

    CKTextureDesc desc;
    desc.Format.Width = GetWidth();
    desc.Format.Height = GetHeight();
    desc.MipMapCount = m_MipMapLevel;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;

    // Check texture cache management
    RCKRenderManager *rm = static_cast<RCKRenderManager *>(m_Context->GetRenderManager());
    if (rm->m_TextureCacheManagement.Value)
        desc.Flags |= CKRST_TEXTURE_MANAGED;

    // Check for cube map
    if ((m_BitmapFlags & 0x10) != 0 &&
        GetSlotCount() == 6 &&
        GetWidth() == GetHeight()) {
        desc.Flags |= CKRST_TEXTURE_CUBEMAP;
    }

    // Check for bump map format
    if (m_DesiredVideoFormat >= _16_V8U8 && m_DesiredVideoFormat <= _32_X8L8V8U8)
        desc.Flags |= CKRST_TEXTURE_BUMPDUDV;

    // Set format based on desired video format
    if (m_DesiredVideoFormat != UNKNOWN_PF) {
        VxPixelFormat2ImageDesc(m_DesiredVideoFormat, desc.Format);

        // If no alpha format and we need alpha, find nearest format with alpha
        if (!HasAlphaFormat(desc.Format)) {
            if ((m_BitmapFlags & 2) != 0 ||
                (Clamping && (m_RasterizerContext->m_Driver->m_3DCaps.CKRasterizerSpecificCaps &
                    CKRST_SPECIFICCAPS_CLAMPEDGEALPHA) != 0)) {
                FindNearestFormatWithAlpha(dev->m_RasterizerDriver, desc.Format);
            }
        }

        if (HasAlphaFormat(desc.Format))
            desc.Flags |= CKRST_TEXTURE_ALPHA;
    } else {
        // Default format: 16-bit ARGB1555
        desc.Format.BitsPerPixel = 16;
        desc.Format.AlphaMask = 0x8000;
        desc.Format.RedMask = 0x7C00;
        desc.Format.GreenMask = 0x03E0;
        desc.Format.BlueMask = 0x001F;
        desc.Flags |= CKRST_TEXTURE_ALPHA;
    }

    if (m_RasterizerContext->CreateObject(m_ObjectIndex, CKRST_OBJ_TEXTURE, &desc)) {
        m_MipMapLevel = desc.MipMapCount;
        return Restore(Clamping);
    }

    return FALSE;
}

CKBOOL RCKTexture::FreeVideoMemory() {
    if (!m_RasterizerContext)
        return FALSE;

    return m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE);
}

CKBOOL RCKTexture::IsInVideoMemory() {
    if (!m_RasterizerContext)
        return FALSE;

    return m_RasterizerContext->GetTextureData(m_ObjectIndex) != nullptr;
}

CKBOOL RCKTexture::CopyContext(CKRenderContext *ctx, VxRect *Src, VxRect *Dest, int CubeMapFace) {
    if (!ctx)
        return FALSE;

    RCKRenderContext *dev = static_cast<RCKRenderContext *>(ctx);
    return dev->m_RasterizerContext->CopyToTexture(
        m_ObjectIndex, Src, Dest, static_cast<CKRST_CUBEFACE>(CubeMapFace));
}

CKBOOL RCKTexture::UseMipmap(int UseMipMap) {
    if (m_MipMapLevel != (CKDWORD) UseMipMap)
        FreeVideoMemory();

    if (UseMipMap)
        m_MipMapLevel = (CKDWORD) -1;
    else
        m_MipMapLevel = 0;

    return TRUE;
}

int RCKTexture::GetMipmapCount() {
    return m_MipMapLevel;
}

CKBOOL RCKTexture::GetVideoTextureDesc(VxImageDescEx &desc) {
    if (!m_RasterizerContext)
        return FALSE;

    CKTextureDesc *texDesc = m_RasterizerContext->GetTextureData(m_ObjectIndex);
    if (!texDesc)
        return FALSE;

    desc = texDesc->Format;
    return TRUE;
}

VX_PIXELFORMAT RCKTexture::GetVideoPixelFormat() {
    if (!m_RasterizerContext)
        return UNKNOWN_PF;

    CKTextureDesc *texDesc = m_RasterizerContext->GetTextureData(m_ObjectIndex);
    if (!texDesc)
        return UNKNOWN_PF;

    return VxImageDesc2PixelFormat(texDesc->Format);
}

CKBOOL RCKTexture::GetSystemTextureDesc(VxImageDescEx &desc) {
    return GetImageDesc(desc);
}

void RCKTexture::SetDesiredVideoFormat(VX_PIXELFORMAT Format) {
    m_DesiredVideoFormat = Format;
}

VX_PIXELFORMAT RCKTexture::GetDesiredVideoFormat() {
    return m_DesiredVideoFormat;
}

CKBOOL RCKTexture::SetUserMipMapMode(CKBOOL UserMipmap) {
    if (UserMipmap) {
        UseMipmap(TRUE);

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
            mipDesc.AlphaMask = 0xFF000000;
            mipDesc.RedMask = 0x00FF0000;
            mipDesc.GreenMask = 0x0000FF00;
            mipDesc.BlueMask = 0x000000FF;
            mipDesc.BitsPerPixel = 32;
            mipDesc.Width = GetWidth();
            mipDesc.Height = GetHeight();
            mipDesc.BytesPerLine = GetWidth() * 4;

            for (int i = 0; i < mipCount; ++i) {
                mipDesc.Width >>= 1;
                mipDesc.Height >>= 1;
                mipDesc.BytesPerLine >>= 1;
                mipDesc.Image = new CKBYTE[mipDesc.Width * mipDesc.Height * 4];

                VxImageDescEx *mipmap = m_MipMaps->At(i);
                *mipmap = mipDesc;
            }
        }
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

RCKTexture::RCKTexture(CKContext *Context, CKSTRING name) : CKTexture(Context, name) {
    RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
    m_DesiredVideoFormat = static_cast<VX_PIXELFORMAT>(rm->m_TextureVideoFormat.Value);
    m_MipMapLevel = 0;
    m_RasterizerContext = nullptr;
    m_MipMaps = nullptr;
    m_ObjectIndex = rm->CreateObjectIndex(CKRST_OBJ_TEXTURE);
}

RCKTexture::~RCKTexture() {
    RCKTexture::SetUserMipMapMode(FALSE);
    if (m_ObjectIndex != 0) {
        RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
        rm->ReleaseObjectIndex(m_ObjectIndex, CKRST_OBJ_TEXTURE);
    }
}

CK_CLASSID RCKTexture::GetClassID() {
    return m_ClassID;
}

CKStateChunk *RCKTexture::Save(CKFile *file, CKDWORD flags) {
    return CKBeObject::Save(file, flags);
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
            m_MipMapLevel = dword & 0xFF;
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

        if (m_DesiredVideoFormat > _32_X8L8V8U8) {
            m_DesiredVideoFormat = _16_ARGB1555;
        }
    }

    return CK_OK;
}

int RCKTexture::GetMemoryOccupation() {
    return CKBeObject::GetMemoryOccupation() + 20 + GetWidth() * GetHeight() * GetSlotCount() * 4;
}

CKERROR RCKTexture::Copy(CKObject &o, CKDependenciesContext &context) {
    return CKBeObject::Copy(o, context);
}

CKSTRING RCKTexture::GetClassName() {
    return (CKSTRING) "Texture";
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
