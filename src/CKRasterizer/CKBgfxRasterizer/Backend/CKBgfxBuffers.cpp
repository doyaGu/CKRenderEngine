// CKBgfxBackend vertex and index buffer storage.

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

    auto *rec = new CKBgfxVertexBufferRecord();
    rec->Handle = handle;
    rec->Layout = Layout;
    rec->NativeLayout = layout;
    rec->Shadow.resize(totalSize);
    if (Data) memcpy(rec->Shadow.data(), Data, totalSize);
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

    auto *rec = new CKBgfxIndexBufferRecord();
    rec->Handle = handle;
    rec->Index32 = Index32;
    rec->Shadow.resize(totalSize);
    if (Data) memcpy(rec->Shadow.data(), Data, totalSize);
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
    } else if (Desc->Kind == CKRST_BACKEND_BUFFER_INDEX) {
        const CKDWORD indexSize = Desc->Index32 ? 4 : 2;
        if ((Desc->Size % indexSize) != 0)
            return CKERR_INVALIDPARAMETER;
        err = CreateIndexBufferRecord(Desc->Size / indexSize, Desc->Index32, Desc->InitialData, Out);
    } else {
        return CKERR_INVALIDPARAMETER;
    }
    if (err == CK_OK && Desc->InitialData)
        ++m_FrameBufferUploads;
    return err;
}

CKERROR CKBgfxBackend::UpdateBuffer(CKBackendBufferKind Kind, CKDWORD Buffer, CKDWORD Offset,
                                    CKDWORD Size, const void *Data)
{
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    CKERROR err;
    if (Kind == CKRST_BACKEND_BUFFER_VERTEX)
        err = UpdateVertexBufferRecord(Buffer, Offset, Size, Data);
    else if (Kind == CKRST_BACKEND_BUFFER_INDEX)
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
    memcpy(rec->Shadow.data() + Offset, Data, Size);
    if (m_FrameInProgress) {
        const auto replacement = bgfx::createDynamicVertexBuffer(
            bgfx::copy(rec->Shadow.data(), rec->Size), rec->NativeLayout, BGFX_BUFFER_ALLOW_RESIZE);
        if (!bgfx::isValid(replacement)) return CKERR_OUTOFMEMORY;
        // bgfx retains the old native resource until encoded draws have finished.
        bgfx::destroy(rec->Handle);
        rec->Handle = replacement;
    } else {
        bgfx::update(rec->Handle, Offset / rec->VertexSize, bgfx::copy(Data, Size));
    }

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
    memcpy(rec->Shadow.data() + Offset, Data, Size);
    if (m_FrameInProgress) {
        const auto replacement = bgfx::createDynamicIndexBuffer(bgfx::copy(rec->Shadow.data(), rec->Size),
            BGFX_BUFFER_ALLOW_RESIZE | (rec->Index32 ? BGFX_BUFFER_INDEX32 : 0));
        if (!bgfx::isValid(replacement)) return CKERR_OUTOFMEMORY;
        bgfx::destroy(rec->Handle);
        rec->Handle = replacement;
    } else {
        bgfx::update(rec->Handle, Offset / indexSize, bgfx::copy(Data, Size));
    }

    return CK_OK;
}
