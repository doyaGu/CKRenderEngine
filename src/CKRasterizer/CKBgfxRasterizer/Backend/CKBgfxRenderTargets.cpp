// CKBgfxBackend render-target and depth-texture creation.

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

    auto *rec = new CKBgfxFrameBufferRecord();
    rec->Handle = handle;
    rec->Desc = *Desc;

    const CKDWORD frameBuffer = m_Resources->FrameBuffers.Insert(
        rec, m_CapsDesc.MaxFrameBuffers);
    if (frameBuffer == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
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

    const CKDWORD texture = m_Resources->Textures.Insert(
        rec, m_CapsDesc.MaxTextures);
    if (texture == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
    }
    *Out = texture;
    TraceTextureMap((CKSTRING)"create", texture, rec);
    return CK_OK;
}

