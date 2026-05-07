#include "RCKSprite.h"

#include "CKPathManager.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKBitmapData.h"
#include "CKBitmapReader.h"
#include "CKGlobals.h"
#include "CKRasterizer.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"

CK_CLASSID RCKSprite::m_ClassID = CKCID_SPRITE;

static void FindNearestFormatWithAlpha(CKRasterizerDriver *driver, VxImageDescEx *desc) {
    VxImageDescEx *best = nullptr;
    int minDiff = 64;
    for (auto it = driver->m_TextureFormats.Begin(); it != driver->m_TextureFormats.End(); ++it) {
        if (it->Format.AlphaMask) {
            int diff = abs(it->Format.BitsPerPixel - desc->BitsPerPixel);
            if (diff < minDiff) {
                best = &it->Format;
                minDiff = diff;
            }
        }
    }
    if (best) {
        desc->BitsPerPixel = best->BitsPerPixel;
        desc->RedMask = best->RedMask;
        desc->GreenMask = best->GreenMask;
        desc->BlueMask = best->BlueMask;
        desc->AlphaMask = best->AlphaMask;
    }
}

static CKBOOL SamePixelFormat(const VxImageDescEx &a, const VxImageDescEx &b) {
    return a.BitsPerPixel == b.BitsPerPixel &&
           a.RedMask == b.RedMask &&
           a.GreenMask == b.GreenMask &&
           a.BlueMask == b.BlueMask &&
           a.AlphaMask == b.AlphaMask;
}

static CKBYTE *ConvertSpriteImage(const VxImageDescEx &src, const VxImageDescEx &videoFormat,
                                  VxImageDescEx &uploadDesc) {
    if (src.Width <= 0 || src.Height <= 0 || src.BitsPerPixel <= 0 || videoFormat.BitsPerPixel <= 0)
        return nullptr;

    uploadDesc = videoFormat;
    uploadDesc.Width = src.Width;
    uploadDesc.Height = src.Height;
    uploadDesc.BytesPerLine = src.Width * uploadDesc.BitsPerPixel / 8;
    uploadDesc.TotalImageSize = uploadDesc.BytesPerLine * uploadDesc.Height;

    CKBYTE *converted = new CKBYTE[uploadDesc.TotalImageSize];
    uploadDesc.Image = converted;
    VxDoBlit(src, uploadDesc);
    return converted;
}

RCKSprite::RCKSprite(CKContext *Context, CKSTRING name) : RCK2dEntity(Context, name) {
    // CKBitmapData is initialized by its default constructor
    // Re-initialize m_SourceRect to (0,0,0,0) as per IDA decompilation
    m_SourceRect = VxRect(0.0f, 0.0f, 0.0f, 0.0f);

    RCKRenderManager *rm = (RCKRenderManager *) Context->GetRenderManager();
    m_VideoFormat = (VX_PIXELFORMAT) rm->m_SpriteVideoFormat.Value;
    m_RasterizerContext = nullptr;
    m_ObjectIndex = rm->CreateObjectIndex(CKRST_OBJ_TEXTURE);
    m_InVideoMemory = FALSE;
}

RCKSprite::~RCKSprite() {
    RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
    if (rm && m_ObjectIndex) {
        rm->ReleaseObjectIndex(m_ObjectIndex, CKRST_OBJ_TEXTURE);
    }
}

CK_CLASSID RCKSprite::GetClassID() {
    return m_ClassID;
}

CKSTRING RCKSprite::GetClassName() {
    return "Sprite";
}

int RCKSprite::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKSprite::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKSprite::Register() {
    // Based on IDA decompilation
    CKPARAMETERFROMCLASS(RCKSprite, CKPGUID_SPRITE);
    CKCLASSDEFAULTOPTIONS(RCKSprite, CK_GENERALOPTIONS_CANUSECURRENTOBJECT);
}

CKSprite *RCKSprite::CreateInstance(CKContext *Context) {
    return (CKSprite *) (CKRenderObject *) new RCKSprite(Context);
}

// ==================== CKSprite methods ====================

CKBOOL RCKSprite::Create(int Width, int Height, int BPP, int Slot) {
    int oldWidth = m_BitmapData.m_Width;
    int oldHeight = m_BitmapData.m_Height;

    if (m_BitmapData.CreateImage(Width, Height, BPP, Slot)) {
        SetSize(Vx2DVector((float) GetWidth(), (float) GetHeight()));
        m_SourceRect.left = 0.0f;
        m_SourceRect.top = 0.0f;
        m_SourceRect.right = (float) GetWidth();
        m_SourceRect.bottom = (float) GetHeight();

        if (oldWidth != m_BitmapData.m_Width || oldHeight != m_BitmapData.m_Height) {
            FreeVideoMemory();
        }
        return TRUE;
    } else {
        if (oldWidth != m_BitmapData.m_Width || oldHeight != m_BitmapData.m_Height) {
            FreeVideoMemory();
        }
        return FALSE;
    }
}

CKBOOL RCKSprite::LoadImage(CKSTRING Name, int Slot) {
    if (!Name) return FALSE;

    XString filename(Name);
    CKPathManager *pm = m_Context->GetPathManager();
    pm->ResolveFileName(filename, BITMAP_PATH_IDX, -1);

    int oldWidth = m_BitmapData.m_Width;
    int oldHeight = m_BitmapData.m_Height;

    CKBOOL result = m_BitmapData.LoadSlotImage(filename.Str(), Slot);
    if (result) {
        SetSize(Vx2DVector((float) GetWidth(), (float) GetHeight()));

        if (Slot == 0) {
            m_SourceRect.left = 0.0f;
            m_SourceRect.top = 0.0f;
            m_SourceRect.right = (float) GetWidth();
            m_SourceRect.bottom = (float) GetHeight();
        }
    } else {
        m_BitmapData.SetSlotFileName(Slot, Name);
    }

    if (oldWidth != m_BitmapData.m_Width || oldHeight != m_BitmapData.m_Height) {
        FreeVideoMemory();
    }

    return result;
}

CKBOOL RCKSprite::SaveImage(CKSTRING Name, int Slot, CKBOOL CKUseFormat) {
    return m_BitmapData.SaveImage(Name, Slot, CKUseFormat);
}

CKERROR RCKSprite::Draw(CKRenderContext *dev) {
    if (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_INVALID)
        return CKERR_INVALIDPARAMETER;

    RCKRenderContext *rctx = (RCKRenderContext *) dev;
    if (!rctx || !rctx->m_RasterizerContext)
        return CKERR_INVALIDRENDERCONTEXT;

    CKRasterizerContext *rstCtx = rctx->m_RasterizerContext;
    m_RasterizerContext = rstCtx;

    CKBOOL reload = FALSE;

    if (m_InVideoMemory) {
        if (!m_VideoFormatDesc.AlphaMask && m_VideoFormatDesc.Flags < _DXT1 && (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_TRANSPARENT)) {
            rstCtx->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE);
            m_InVideoMemory = FALSE;
            reload = TRUE;
        }
    } else {
        reload = TRUE;
    }

    if (reload) {
        SystemToVideoMemory(dev, FALSE);
    } else {
        m_RasterizerContext = rstCtx;
    }

    if (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_FORCERESTORE) {
        Restore(FALSE);
    }

    CKFixedFunctionPipeline &ffp = rctx->m_FFPipeline;

    // Set render states for 2D sprite rendering
    ffp.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_ALWAYS);

    if (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) {
        ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
        ffp.SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
        ffp.SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA);
        ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
        ffp.SetRenderState(VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER);
        ffp.SetRenderState(VXRENDERSTATE_ALPHAREF, 0);
    } else {
        ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
        ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    }

    // Bind sprite texture
    ffp.DisableTextureStagesFrom(0);
    ffp.SetTexture(0, m_ObjectIndex);
    ffp.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);

    // Build quad vertices
    VxDrawPrimitiveData *data = rctx->GetDrawPrimitiveStructure(CKRST_DP_CL_VCT, 4);
    float *positionPtr = (float *) data->PositionPtr;
    CKDWORD *texCoordPtr = (CKDWORD *) data->TexCoordPtr;
    void *colorPtr = data->ColorPtr;
    int posStride = data->PositionStride;

    // White color (texture provides all color)
    CKDWORD whiteColor = 0xFFFFFFFF;
    VxFillStructure(4, colorPtr, data->ColorStride, 4, &whiteColor);

    // Sprite source rectangles are stored in bitmap pixels; bgfx samplers expect normalized UVs.
    const float invBitmapWidth = m_BitmapData.m_Width > 0 ? 1.0f / (float)m_BitmapData.m_Width : 0.0f;
    const float invBitmapHeight = m_BitmapData.m_Height > 0 ? 1.0f / (float)m_BitmapData.m_Height : 0.0f;
    float u0 = m_SrcRect.left * invBitmapWidth;
    float v0 = m_SrcRect.top * invBitmapHeight;
    float u1 = m_SrcRect.right * invBitmapWidth;
    float v1 = m_SrcRect.bottom * invBitmapHeight;

    texCoordPtr[0] = *(CKDWORD *) &u0;
    texCoordPtr[1] = *(CKDWORD *) &v0;
    texCoordPtr[2] = *(CKDWORD *) &u1;
    texCoordPtr[3] = *(CKDWORD *) &v0;
    texCoordPtr[4] = *(CKDWORD *) &u1;
    texCoordPtr[5] = *(CKDWORD *) &v1;
    texCoordPtr[6] = *(CKDWORD *) &u0;
    texCoordPtr[7] = *(CKDWORD *) &v1;

    // Vertex positions (screen-space quad)
    positionPtr[0] = (float)(int)(m_VtxPos.left + 0.5f);
    positionPtr[1] = (float)(int)(m_VtxPos.top + 0.5f);
    positionPtr[2] = 0.0f;
    positionPtr[3] = 1.0f;
    positionPtr = (float *) ((char *) positionPtr + posStride);

    positionPtr[0] = (float)(int)(m_VtxPos.right + 0.5f);
    positionPtr[1] = (float)(int)(m_VtxPos.top + 0.5f);
    positionPtr[2] = 0.0f;
    positionPtr[3] = 1.0f;
    positionPtr = (float *) ((char *) positionPtr + posStride);

    positionPtr[0] = (float)(int)(m_VtxPos.right + 0.5f);
    positionPtr[1] = (float)(int)(m_VtxPos.bottom + 0.5f);
    positionPtr[2] = 0.0f;
    positionPtr[3] = 1.0f;
    positionPtr = (float *) ((char *) positionPtr + posStride);

    positionPtr[0] = (float)(int)(m_VtxPos.left + 0.5f);
    positionPtr[1] = (float)(int)(m_VtxPos.bottom + 0.5f);
    positionPtr[2] = 0.0f;
    positionPtr[3] = 1.0f;

    rctx->DrawPrimitive(VX_TRIANGLEFAN, NULL, 4, data);

    return CK_OK;
}

CKBOOL RCKSprite::SystemToVideoMemory(CKRenderContext *dev, CKBOOL Clamping) {
    if (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_INVALID) return FALSE;

    RCKRenderContext *rctx = (RCKRenderContext *) dev;
    if (!rctx->m_RasterizerContext) return FALSE;

    m_RasterizerContext = rctx->m_RasterizerContext;

    CKTextureDesc spriteDesc;
    spriteDesc.Format.Width = m_BitmapData.m_Width;
    spriteDesc.Format.Height = m_BitmapData.m_Height;
    spriteDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_MANAGED | CKRST_TEXTURE_SPRITE | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    spriteDesc.MipMapCount = 0;

    VxPixelFormat2ImageDesc(m_VideoFormat, spriteDesc.Format);

    if (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) {
        FindNearestFormatWithAlpha(rctx->m_RasterizerDriver, &spriteDesc.Format);
    }

    // Handle UNKNOWN_PF - use ARGB1555 format
    if (m_VideoFormat == UNKNOWN_PF) {
        spriteDesc.Format.BitsPerPixel = 16;
        spriteDesc.Format.AlphaMask = 0x8000;
        spriteDesc.Format.RedMask = 0x7C00;
        spriteDesc.Format.GreenMask = 0x3E0;
        spriteDesc.Format.BlueMask = 0x1F;
    }

    if (m_RasterizerContext->CreateTexture(m_ObjectIndex, &spriteDesc, nullptr) == CK_OK) {
        m_InVideoMemory = TRUE;
        m_VideoFormatDesc = spriteDesc.Format;
        return Restore(Clamping);
    }
    return FALSE;
}

CKBOOL RCKSprite::Restore(CKBOOL Clamp) {
    if (!m_RasterizerContext) return FALSE;
    if (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_INVALID) return FALSE;

    m_BitmapData.m_BitmapFlags &= ~CKBITMAPDATA_FORCERESTORE;

    CKBYTE *ptr = m_BitmapData.LockSurfacePtr(-1);
    if (ptr) {
        VxImageDescEx desc;
        m_BitmapData.GetImageDesc(desc);
        desc.Image = ptr;

        if (m_BitmapData.m_BitmapFlags & CKBITMAPDATA_TRANSPARENT) {
            m_BitmapData.SetAlphaForTransparentColor(desc);
        }

        VxImageDescEx uploadDesc = desc;
        CKBYTE *converted = nullptr;
        if (!SamePixelFormat(desc, m_VideoFormatDesc)) {
            converted = ConvertSpriteImage(desc, m_VideoFormatDesc, uploadDesc);
            if (!converted)
                return FALSE;
        }

        const CKBOOL result = (m_RasterizerContext->UpdateTexture(m_ObjectIndex, 0, 0, nullptr, &uploadDesc) == CK_OK);
        delete[] converted;
        return result;
    }
    return FALSE;
}

CKBOOL RCKSprite::FreeVideoMemory() {
    if (m_RasterizerContext && m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_TEXTURE) == CK_OK) {
        m_InVideoMemory = FALSE;
        return TRUE;
    }
    return FALSE;
}

CKBOOL RCKSprite::IsInVideoMemory() {
    return m_RasterizerContext && m_InVideoMemory;
}

CKBOOL RCKSprite::CopyContext(CKRenderContext *ctx, VxRect *Src, VxRect *Dest) {
    if (!ctx || !m_RasterizerContext || !m_InVideoMemory)
        return FALSE;

    RCKRenderContext *rctx = static_cast<RCKRenderContext *>(ctx);

    VxImageDescEx srcDesc;
    const int requiredBytes = rctx->DumpToMemory(Src, VXBUFFER_BACKBUFFER, srcDesc);
    if (!requiredBytes)
        return FALSE;

    CKBYTE *pixels = new CKBYTE[(size_t)requiredBytes];
    srcDesc.Image = pixels;
    if (!rctx->DumpToMemory(Src, VXBUFFER_BACKBUFFER, srcDesc)) {
        delete[] pixels;
        return FALSE;
    }

    VxImageDescEx uploadDesc = srcDesc;
    CKBYTE *converted = nullptr;
    if (!SamePixelFormat(srcDesc, m_VideoFormatDesc)) {
        converted = ConvertSpriteImage(srcDesc, m_VideoFormatDesc, uploadDesc);
        if (!converted) {
            delete[] pixels;
            return FALSE;
        }
    }

    CKRECT region;
    CKRECT *regionPtr = nullptr;
    if (Dest) {
        region.left = (int)Dest->left;
        region.top = (int)Dest->top;
        region.right = (int)Dest->right;
        region.bottom = (int)Dest->bottom;
        regionPtr = &region;
    }

    const CKBOOL result =
        (m_RasterizerContext->UpdateTexture(m_ObjectIndex, 0, 0, regionPtr, &uploadDesc) == CK_OK);
    delete[] converted;
    delete[] pixels;
    return result;
}

CKBOOL RCKSprite::GetVideoTextureDesc(VxImageDescEx &desc) {
    if (!m_RasterizerContext || !m_InVideoMemory) return FALSE;
    desc = m_VideoFormatDesc;
    return TRUE;
}

VX_PIXELFORMAT RCKSprite::GetVideoPixelFormat() {
    if (!m_RasterizerContext || !m_InVideoMemory) return UNKNOWN_PF;
    return VxImageDesc2PixelFormat(m_VideoFormatDesc);
}

CKBOOL RCKSprite::GetSystemTextureDesc(VxImageDescEx &desc) {
    return m_BitmapData.GetImageDesc(desc);
}

void RCKSprite::SetDesiredVideoFormat(VX_PIXELFORMAT pf) {
    if (m_VideoFormat != pf) {
        m_VideoFormat = pf;
        FreeVideoMemory();
    }
}

VX_PIXELFORMAT RCKSprite::GetDesiredVideoFormat() {
    return m_VideoFormat;
}

void RCKSprite::CopySpriteData(RCKSprite *src) {
    if (!src) return;

    if (src->GetMovieFileName()) {
        m_BitmapData.LoadMovieFile(src->GetMovieFileName());
    }

    if (src->m_BitmapData.m_SaveProperties) {
        m_BitmapData.m_SaveProperties = CKCopyBitmapProperties(src->m_BitmapData.m_SaveProperties);
    }

    m_BitmapData.m_Width = src->m_BitmapData.m_Width;
    m_BitmapData.m_Height = src->m_BitmapData.m_Height;
    m_BitmapData.m_CurrentSlot = src->m_BitmapData.m_CurrentSlot;
    m_BitmapData.m_BitmapFlags = src->m_BitmapData.m_BitmapFlags;
    m_BitmapData.m_TransColor = src->m_BitmapData.m_TransColor;
    m_BitmapData.m_SaveOptions = src->m_BitmapData.m_SaveOptions;
    m_BitmapData.m_PickThreshold = src->m_BitmapData.m_PickThreshold;
    m_VideoFormat = src->m_VideoFormat;

    const int slotCount = src->GetSlotCount();
    SetSlotCount(slotCount);

    const size_t byteCount = (size_t) (4 * m_BitmapData.m_Height * m_BitmapData.m_Width);
    for (int i = 0; i < slotCount; ++i) {
        CKBYTE *srcPtr = src->LockSurfacePtr(i);
        SetSlotFileName(i, src->GetSlotFileName(i));
        if (srcPtr) {
            // Original allocates per-slot storage then memcpy 32bpp (4*W*H).
            m_BitmapData.CreateImage(m_BitmapData.m_Width, m_BitmapData.m_Height, 32, i);
            CKBYTE *dstPtr = m_BitmapData.LockSurfacePtr(i);
            if (dstPtr) {
                memcpy(dstPtr, srcPtr, byteCount);
            }
        }
    }
}

CKERROR RCKSprite::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = RCK2dEntity::Copy(o, context);
    if (err != CK_OK) return err;

    context.GetClassDependencies(CKCID_SPRITE);
    CopySpriteData((RCKSprite *) &o);
    return CK_OK;
}

void RCKSprite::PreSave(CKFile *file, CKDWORD flags) {
    RCK2dEntity::PreSave(file, flags);
}

CKStateChunk *RCKSprite::Save(CKFile *file, CKDWORD flags) {
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_SPRITE, file);
    CKStateChunk *baseChunk = RCK2dEntity::Save(file, flags);
    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    if (file) {
        CKDWORD identifiers[] = {0x200000, 0x10000000, 0x800000, 0x400000};
        m_BitmapData.DumpToChunk(chunk, m_Context, file, identifiers);

        chunk->WriteIdentifier(CK_STATESAVE_SPRITETRANSPARENT);
        chunk->WriteDword(GetTransparentColor());
        chunk->WriteDword(IsTransparent());

        chunk->WriteIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE);
        chunk->WriteInt(GetCurrentSlot());

        chunk->WriteIdentifier(CK_STATESAVE_SPRITEFORMAT);
        chunk->WriteDword(m_BitmapData.m_SaveOptions);

        if (m_BitmapData.m_SaveProperties) {
            chunk->WriteBuffer(m_BitmapData.m_SaveProperties->m_Size, m_BitmapData.m_SaveProperties);
        } else {
            chunk->WriteBuffer(0, nullptr);
        }
    } else {
        if (flags & CK_STATESAVE_SPRITETRANSPARENT) {
            chunk->WriteIdentifier(CK_STATESAVE_SPRITETRANSPARENT);
            chunk->WriteDword(GetTransparentColor());
            chunk->WriteDword(IsTransparent());
        }
        if (flags & CK_STATESAVE_SPRITECURRENTIMAGE) {
            chunk->WriteIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE);
            chunk->WriteInt(GetCurrentSlot());
        }
    }

    if (GetClassID() == CKCID_SPRITE) {
        chunk->CloseChunk();
    } else {
        chunk->UpdateDataSize();
    }
    return chunk;
}

CKERROR RCKSprite::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk) return CKERR_INVALIDPARAMETER;

    RCK2dEntity::Load(chunk, file);

    if (file) {
        if (chunk->SeekIdentifier(CK_STATESAVE_SPRITESHARED)) {
            RCKSprite *src = (RCKSprite *) chunk->ReadObject(m_Context);
            CopySpriteData(src);
        } else {
            VxRect savedSrcRect = m_SourceRect;
            CKDWORD identifiers[] = {0x200000, 0x10000000, 0x800000, 0x400000, 0x40000};
            m_BitmapData.ReadFromChunk(chunk, m_Context, file, identifiers);
            m_SourceRect = savedSrcRect;

            if (chunk->SeekIdentifier(CK_STATESAVE_SPRITETRANSPARENT)) {
                SetTransparentColor(chunk->ReadDword());
                SetTransparent(chunk->ReadDword());
            }
            if (chunk->SeekIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE)) {
                SetCurrentSlot(chunk->ReadInt());
            }
            if (chunk->SeekIdentifier(CK_STATESAVE_SPRITEFORMAT)) {
                m_BitmapData.m_SaveOptions = (CK_BITMAP_SAVEOPTIONS) chunk->ReadDword();
                void *buf = nullptr;
                int size = chunk->ReadBuffer(&buf);
                if (buf) {
                    if (chunk->GetDataVersion() > 6) {
                        SetSaveFormat((CKBitmapProperties *) buf);
                    }
                    CKDeletePointer(buf);
                }
            }
        }
    } else {
        if (chunk->SeekIdentifier(CK_STATESAVE_SPRITETRANSPARENT)) {
            SetTransparentColor(chunk->ReadDword());
            SetTransparent(chunk->ReadDword());
        }
        if (chunk->SeekIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE)) {
            SetCurrentSlot(chunk->ReadInt());
        }
        if (chunk->SeekIdentifier(CK_STATESAVE_SPRITESHARED)) {
            RCKSprite *src = (RCKSprite *) chunk->ReadObject(m_Context);
            CopySpriteData(src);
        }
    }
    return CK_OK;
}

void RCKSprite::RestoreInitialSize() {
    Vx2DVector size((float) GetWidth(), (float) GetHeight());
    m_Rect.SetSize(size);
}

int RCKSprite::GetMemoryOccupation() {
    return RCK2dEntity::GetMemoryOccupation() + (sizeof(RCKSprite) - sizeof(RCK2dEntity));
}

CKERROR RCKSprite::PrepareDependencies(CKDependenciesContext &context) {
    CKERROR err = RCK2dEntity::PrepareDependencies(context);
    if (err != CK_OK)
        return err;
    return context.FinishPrepareDependencies(this, m_ClassID);
}

CKERROR RCKSprite::RemapDependencies(CKDependenciesContext &context) {
    return RCK2dEntity::RemapDependencies(context);
}

CKBOOL RCKSprite::LoadMovie(CKSTRING Name, int width, int height, int Bpp) {
    if (!Name) return FALSE;

    XString filename(Name);
    CKPathManager *pm = m_Context->GetPathManager();
    pm->ResolveFileName(filename, BITMAP_PATH_IDX, -1);

    if (m_BitmapData.LoadMovieFile(filename.Str())) {
        SetSize(Vx2DVector((float) GetWidth(), (float) GetHeight()));
        m_SourceRect.left = 0.0f;
        m_SourceRect.top = 0.0f;
        m_SourceRect.right = (float) GetWidth();
        m_SourceRect.bottom = (float) GetHeight();
        return TRUE;
    }

    return FALSE;
}

// Proxy methods for CKBitmapData
CKBYTE *RCKSprite::LockSurfacePtr(int Slot) { return m_BitmapData.LockSurfacePtr(Slot); }
CKBOOL RCKSprite::ReleaseSurfacePtr(int Slot) { return m_BitmapData.ReleaseSurfacePtr(Slot); }
CKBOOL RCKSprite::SetPixel(int x, int y, CKDWORD Color, int Slot) { return m_BitmapData.SetPixel(x, y, Color, Slot); }
CKDWORD RCKSprite::GetPixel(int x, int y, int Slot) { return m_BitmapData.GetPixel(x, y, Slot); }
void RCKSprite::SetTransparent(CKBOOL Trans) { m_BitmapData.SetTransparent(Trans); }
CKBOOL RCKSprite::IsTransparent() { return m_BitmapData.IsTransparent(); }
void RCKSprite::SetTransparentColor(CKDWORD Color) { m_BitmapData.SetTransparentColor(Color); }
CKDWORD RCKSprite::GetTransparentColor() { return m_BitmapData.GetTransparentColor(); }
int RCKSprite::GetWidth() { return m_BitmapData.GetWidth(); }
int RCKSprite::GetHeight() { return m_BitmapData.GetHeight(); }
CKBOOL RCKSprite::SetSlotFileName(int Slot, CKSTRING Filename) { return m_BitmapData.SetSlotFileName(Slot, Filename); }
CKSTRING RCKSprite::GetSlotFileName(int Slot) { return m_BitmapData.GetSlotFileName(Slot); }
int RCKSprite::GetSlotCount() { return m_BitmapData.GetSlotCount(); }
CKBOOL RCKSprite::SetSlotCount(int Count) { return m_BitmapData.SetSlotCount(Count); }
int RCKSprite::GetCurrentSlot() { return m_BitmapData.GetCurrentSlot(); }
CKBOOL RCKSprite::SetCurrentSlot(int Slot) { return m_BitmapData.SetCurrentSlot(Slot); }
CKSTRING RCKSprite::GetMovieFileName() { return m_BitmapData.GetMovieFileName(); }
CKMovieReader *RCKSprite::GetMovieReader() { return m_BitmapData.GetMovieReader(); }
void RCKSprite::SetPickThreshold(int Threshold) { m_BitmapData.SetPickThreshold(Threshold); }
int RCKSprite::GetPickThreshold() { return m_BitmapData.GetPickThreshold(); }
void RCKSprite::SetSaveOptions(CK_BITMAP_SAVEOPTIONS Options) { m_BitmapData.SetSaveOptions(Options); }
CK_BITMAP_SAVEOPTIONS RCKSprite::GetSaveOptions() { return m_BitmapData.GetSaveOptions(); }
void RCKSprite::SetSaveFormat(CKBitmapProperties *Format) { m_BitmapData.SetSaveFormat(Format); }
CKBitmapProperties *RCKSprite::GetSaveFormat() { return m_BitmapData.GetSaveFormat(); }

// Pixel format information
int RCKSprite::GetBitsPerPixel() {
    VxImageDescEx desc;
    m_BitmapData.GetImageDesc(desc);
    return desc.BitsPerPixel;
}

int RCKSprite::GetBytesPerLine() {
    VxImageDescEx desc;
    m_BitmapData.GetImageDesc(desc);
    return desc.BytesPerLine;
}

int RCKSprite::GetRedMask() {
    VxImageDescEx desc;
    m_BitmapData.GetImageDesc(desc);
    return desc.RedMask;
}

int RCKSprite::GetGreenMask() {
    VxImageDescEx desc;
    m_BitmapData.GetImageDesc(desc);
    return desc.GreenMask;
}

int RCKSprite::GetBlueMask() {
    VxImageDescEx desc;
    m_BitmapData.GetImageDesc(desc);
    return desc.BlueMask;
}

int RCKSprite::GetAlphaMask() {
    VxImageDescEx desc;
    m_BitmapData.GetImageDesc(desc);
    return desc.AlphaMask;
}

CKBOOL RCKSprite::ReleaseSlot(int Slot) {
    return m_BitmapData.ReleaseSlot(Slot);
}

CKBOOL RCKSprite::ReleaseAllSlots() {
    return m_BitmapData.ReleaseAllSlots();
}

CKBOOL RCKSprite::ToRestore() {
    return m_BitmapData.ToRestore();
}
