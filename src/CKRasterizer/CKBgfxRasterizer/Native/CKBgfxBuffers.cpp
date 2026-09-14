// CKBgfxRasterizerContext vertex and index buffer storage.

#include "CKBgfxRasterizerContext.h"
#include "CKBgfxResources.h"

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::CreateVertexBufferRecord(CKDWORD VertexSize, CKDWORD VertexCount, CKDWORD Layout,
                                                const void *Data, CKDWORD *OutBuffer)
{
    if (VertexSize == 0 || VertexSize > UINT16_MAX || VertexCount == 0)
        return CKERR_INVALIDPARAMETER;
    uint64_t totalSize64 = (uint64_t)VertexCount * VertexSize;
    if (totalSize64 > 0x7fffffffu)
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

    auto *rec = new CKBgfxVertexBufferRecord();
    rec->Handle = handle;
    rec->Layout = Layout;
    rec->NativeLayout = layout;
    rec->Shadow.Resize((int)totalSize);
    if (Data)
        memcpy(rec->Shadow.Begin(), Data, totalSize);
    else
        memset(rec->Shadow.Begin(), 0, totalSize);
    rec->VertexSize = VertexSize;
    rec->VertexCount = VertexCount;
    rec->Size = totalSize;

    const CKDWORD buffer = m_Resources->VertexBuffers.Insert(
        rec, m_CapsDesc.MaxDynamicVertexBuffers);
    if (buffer == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
    }
    *OutBuffer = buffer;
    TraceBufferMap((CKSTRING)"create", (CKSTRING)"vb", buffer, rec->Handle.idx,
                   rec->Layout, rec->VertexSize, VertexCount, 0, 0);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateIndexBufferRecord(CKDWORD IndexCount, CKBOOL Index32, const void *Data,
                                               CKDWORD *OutBuffer)
{
    if (IndexCount == 0)
        return CKERR_INVALIDPARAMETER;
    if (Index32 && (m_CapsDesc.Features & CKRST_DEVCAPS_INDEX32) == 0)
        return CKERR_NOTIMPLEMENTED;

    CKDWORD indexSize = Index32 ? 4 : 2;
    uint64_t totalSize64 = (uint64_t)IndexCount * indexSize;
    if (totalSize64 > 0x7fffffffu)
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

    auto *rec = new CKBgfxIndexBufferRecord();
    rec->Handle = handle;
    rec->Index32 = Index32;
    rec->Shadow.Resize((int)totalSize);
    if (Data)
        memcpy(rec->Shadow.Begin(), Data, totalSize);
    else
        memset(rec->Shadow.Begin(), 0, totalSize);
    rec->IndexCount = IndexCount;
    rec->Size = totalSize;

    const CKDWORD buffer = m_Resources->IndexBuffers.Insert(
        rec, m_CapsDesc.MaxDynamicIndexBuffers);
    if (buffer == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
    }
    *OutBuffer = buffer;
    TraceBufferMap((CKSTRING)"create", (CKSTRING)"ib", buffer, rec->Handle.idx,
                   0, indexSize, IndexCount, Index32 ? 1u : 0u, 0);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateBuffer(const CKBufferDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Size == 0)
        return CKERR_INVALIDPARAMETER;
    CKERROR err;
    if (Desc->Kind == CKRST_BUFFER_VERTEX) {
        if (Desc->Stride == 0 || (Desc->Size % Desc->Stride) != 0)
            return CKERR_INVALIDPARAMETER;
        err = CreateVertexBufferRecord(Desc->Stride, Desc->Size / Desc->Stride, Desc->Layout, Desc->InitialData, Out);
    } else if (Desc->Kind == CKRST_BUFFER_INDEX) {
        const CKDWORD indexSize = Desc->Index32 ? 4 : 2;
        if ((Desc->Size % indexSize) != 0)
            return CKERR_INVALIDPARAMETER;
        err = CreateIndexBufferRecord(Desc->Size / indexSize, Desc->Index32, Desc->InitialData, Out);
    } else {
        return CKERR_INVALIDPARAMETER;
    }
    if (err == CK_OK && Desc->InitialData)
        ++m_BgfxFrameBufferUploads;
    return err;
}

CKERROR CKBgfxRasterizerContext::UpdateBuffer(const CKBufferUpdateDesc *Desc)
{
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (!Desc ||
        (unsigned)Desc->Mode > CKRST_BUFFER_UPDATE_NOOVERWRITE ||
        (Desc->Mode == CKRST_BUFFER_UPDATE_NOOVERWRITE &&
         Desc->Rename))
        return CKERR_INVALIDPARAMETER;
    CKERROR err;
    if (Desc->Kind == CKRST_BUFFER_VERTEX)
        err = UpdateVertexBufferRecord(*Desc);
    else if (Desc->Kind == CKRST_BUFFER_INDEX)
        err = UpdateIndexBufferRecord(*Desc);
    else
        return CKERR_INVALIDPARAMETER;
    if (err == CK_OK)
        ++m_BgfxFrameBufferUploads;
    return err;
}

CKERROR CKBgfxRasterizerContext::UpdateVertexBufferRecord(const CKBufferUpdateDesc &Desc)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc.Data || Desc.Buffer == 0 || Desc.Size == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Desc.Buffer);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

    if (rec->VertexSize == 0)
        return CKERR_INVALIDPARAMETER;
    if (Desc.Offset % rec->VertexSize != 0 || Desc.Size % rec->VertexSize != 0 ||
        Desc.Offset > rec->Size || Desc.Size > rec->Size - Desc.Offset)
        return CKERR_INVALIDPARAMETER;

    const CKBOOL rename = Desc.Mode == CKRST_BUFFER_UPDATE_DISCARD ||
                          Desc.Rename;
    if (rename) {
        bgfx::DynamicVertexBufferHandle replacement;
        if (Desc.Mode == CKRST_BUFFER_UPDATE_DISCARD) {
            replacement = bgfx::createDynamicVertexBuffer(
                rec->VertexCount, rec->NativeLayout, BGFX_BUFFER_ALLOW_RESIZE);
        } else {
            XArray<CKBYTE> contents(rec->Size);
            contents.Resize((int)rec->Size);
            memcpy(contents.Begin(), rec->Shadow.Begin(), rec->Size);
            memcpy(contents.Begin() + Desc.Offset, Desc.Data, Desc.Size);
            replacement = bgfx::createDynamicVertexBuffer(
                bgfx::copy(contents.Begin(), rec->Size),
                rec->NativeLayout, BGFX_BUFFER_ALLOW_RESIZE);
        }
        if (!bgfx::isValid(replacement))
            return CKERR_OUTOFMEMORY;
        if (Desc.Mode == CKRST_BUFFER_UPDATE_DISCARD) {
            bgfx::update(replacement, Desc.Offset / rec->VertexSize,
                         bgfx::copy(Desc.Data, Desc.Size));
        }
        bgfx::destroy(rec->Handle);
        rec->Handle = replacement;
    } else {
        bgfx::update(rec->Handle, Desc.Offset / rec->VertexSize,
                     bgfx::copy(Desc.Data, Desc.Size));
    }
    memcpy(rec->Shadow.Begin() + Desc.Offset, Desc.Data, Desc.Size);

    return CK_OK;
}
CKERROR CKBgfxRasterizerContext::UpdateIndexBufferRecord(const CKBufferUpdateDesc &Desc)
{
    if (!m_BgfxInitialized || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc.Data || Desc.Buffer == 0 || Desc.Size == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxIndexBufferRecord *rec = GetIndexBuffer(Desc.Buffer);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

    CKDWORD indexSize = rec->Index32 ? 4 : 2;
    if (Desc.Offset % indexSize != 0 || Desc.Size % indexSize != 0 ||
        Desc.Offset > rec->Size || Desc.Size > rec->Size - Desc.Offset)
        return CKERR_INVALIDPARAMETER;

    const CKBOOL rename = Desc.Mode == CKRST_BUFFER_UPDATE_DISCARD ||
                          Desc.Rename;
    const uint16_t flags = BGFX_BUFFER_ALLOW_RESIZE |
        (rec->Index32 ? BGFX_BUFFER_INDEX32 : 0);
    if (rename) {
        bgfx::DynamicIndexBufferHandle replacement;
        if (Desc.Mode == CKRST_BUFFER_UPDATE_DISCARD) {
            replacement = bgfx::createDynamicIndexBuffer(
                rec->IndexCount, flags);
        } else {
            XArray<CKBYTE> contents(rec->Size);
            contents.Resize((int)rec->Size);
            memcpy(contents.Begin(), rec->Shadow.Begin(), rec->Size);
            memcpy(contents.Begin() + Desc.Offset, Desc.Data, Desc.Size);
            replacement = bgfx::createDynamicIndexBuffer(
                bgfx::copy(contents.Begin(), rec->Size), flags);
        }
        if (!bgfx::isValid(replacement))
            return CKERR_OUTOFMEMORY;
        if (Desc.Mode == CKRST_BUFFER_UPDATE_DISCARD) {
            bgfx::update(replacement, Desc.Offset / indexSize,
                         bgfx::copy(Desc.Data, Desc.Size));
        }
        bgfx::destroy(rec->Handle);
        rec->Handle = replacement;
    } else {
        bgfx::update(rec->Handle, Desc.Offset / indexSize,
                     bgfx::copy(Desc.Data, Desc.Size));
    }
    memcpy(rec->Shadow.Begin() + Desc.Offset, Desc.Data, Desc.Size);

    return CK_OK;
}
