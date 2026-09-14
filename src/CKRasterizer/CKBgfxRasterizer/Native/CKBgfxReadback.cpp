// CKBgfxRasterizerContext ordered texture readback.

#include "CKBgfxRasterizerContext.h"
#include "CKBgfxResources.h"

#include <memory>
#include <stdint.h>

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
                                              NativeReadbackTicket *Ticket)
{
    if (!m_BgfxInitialized || !m_BgfxCreated ||
        VxThread::GetCurrentVxThreadId() != m_ApiThreadId)
        return CKERR_INVALIDOPERATION;
    if (!Readback)
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
    if (!Ticket)
        return CK_OK;
    if (!(m_Caps.Features & CKRST_DEVCAPS_BLIT)) return CKERR_NOTIMPLEMENTED;
    if (m_NextView >= m_CapsDesc.MaxRenderViews) return CKERR_OUTOFMEMORY;
    auto pending = std::make_shared<CKBgfxRasterizerContext::NativeReadback>();
    pending->Data.Resize((int)requiredSize);
    // bgfx reads textures at the end of a submission. Preserve this call's
    // contents in a unique texture so later blits cannot change the snapshot.
    pending->Snapshot = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1,
        rec->Format, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(pending->Snapshot)) return CKERR_OUTOFMEMORY;
    const bgfx::ViewId view = (bgfx::ViewId)m_NextView++;
    bgfx::resetView(view);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewName(view, "readback-snapshot");
    bgfx::blit(view, pending->Snapshot, 0, 0, 0, 0, rec->Handle, (uint8_t)Mip, 0, 0, 0,
               (uint16_t)width, (uint16_t)height, 1);
    m_DrawPassNeedsResume = m_PassOpen != FALSE;
    m_FrameInProgress = TRUE;
    ++m_FrameBlits;
    pending->AvailableFrame = bgfx::readTexture(pending->Snapshot, pending->Data.Begin());
    m_BgfxReadbacks.PushBack(pending);
    *Ticket = pending;
    return CK_OK;
}

