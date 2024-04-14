#include "RCKTexture.h"

#include "CKStateChunk.h"
#include "RCKRenderManager.h"

CK_CLASSID RCKTexture::m_ClassID = CKCID_TEXTURE;

CKBOOL RCKTexture::Create(int Width, int Height, int BPP, int Slot) {
    return 0;
}

CKBOOL RCKTexture::LoadImage(CKSTRING Name, int Slot) {
    return 0;
}

CKBOOL RCKTexture::LoadMovie(CKSTRING Name) {
    return 0;
}

CKBOOL RCKTexture::SetAsCurrent(CKRenderContext *Dev, CKBOOL Clamping, int TextureStage) {
    return 0;
}

CKBOOL RCKTexture::Restore(CKBOOL Clamp) {
    return 0;
}

CKBOOL RCKTexture::SystemToVideoMemory(CKRenderContext *Dev, CKBOOL Clamping) {
    return 0;
}

CKBOOL RCKTexture::FreeVideoMemory() {
    return 0;
}

CKBOOL RCKTexture::IsInVideoMemory() {
    return 0;
}

CKBOOL RCKTexture::CopyContext(CKRenderContext *ctx, VxRect *Src, VxRect *Dest, int CubeMapFace) {
    return 0;
}

CKBOOL RCKTexture::UseMipmap(int UseMipMap) {
    return 0;
}

int RCKTexture::GetMipmapCount() {
    return 0;
}

CKBOOL RCKTexture::GetVideoTextureDesc(VxImageDescEx &desc) {
    return 0;
}

VX_PIXELFORMAT RCKTexture::GetVideoPixelFormat() {
    return _16_RGB555;
}

CKBOOL RCKTexture::GetSystemTextureDesc(VxImageDescEx &desc) {
    return 0;
}

void RCKTexture::SetDesiredVideoFormat(VX_PIXELFORMAT Format) {

}

VX_PIXELFORMAT RCKTexture::GetDesiredVideoFormat() {
    return _16_RGB555;
}

CKBOOL RCKTexture::SetUserMipMapMode(CKBOOL UserMipmap) {
    return 0;
}

CKBOOL RCKTexture::GetUserMipMapLevel(int Level, VxImageDescEx &ResultImage) {
    return 0;
}

int RCKTexture::GetRstTextureIndex() {
    return 0;
}

RCKTexture::RCKTexture(CKContext *Context, CKSTRING name) : CKTexture(Context, name) {
    RCKRenderManager *rm = (RCKRenderManager *)m_Context->GetRenderManager();
    m_DesiredVideoFormat = static_cast<VX_PIXELFORMAT>(rm->m_TextureVideoFormat.Value);
    m_MipMapLevel = 0;
    m_RasterizerContext = nullptr;
    m_MipMaps = nullptr;
    m_ObjectIndex = rm->CreateObjectIndex(CKRST_OBJ_TEXTURE);
}

RCKTexture::~RCKTexture() {
    RCKTexture::SetUserMipMapMode(FALSE);
    if (m_ObjectIndex != 0) {
        RCKRenderManager *rm = (RCKRenderManager *)m_Context->GetRenderManager();
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
            CKBOOL transparency = (CKBOOL)chunk->ReadDword();
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
            chunk->ReadBuffer((void **)&format);
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
            chunk->ReadBuffer((void **)&format);
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

RCKTexture *RCKTexture::CreateInstance(CKContext *Context) {
    return new RCKTexture(Context);
}