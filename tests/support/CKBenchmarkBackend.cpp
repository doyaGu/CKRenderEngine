#include "CKBenchmarkBackend.h"

#include <cstring>
#include <new>

CKBenchmarkBackend::CKBenchmarkBackend(const CKBackendCaps &conventions)
    : CKRecordingBackend(conventions)
{
}

uint64_t CKBenchmarkBackend::Mix(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

uint64_t CKBenchmarkBackend::SampleBytes(uint64_t hash, const void *data,
                                         size_t size)
{
    if (!data || size == 0)
        return Mix(hash, 0);
    const CKBYTE *bytes = static_cast<const CKBYTE *>(data);
    hash = Mix(hash, size);
    const size_t samples[] = {0, size - 1};
    for (size_t sample : samples)
        hash = Mix(hash, bytes[sample]);
    return hash;
}

void CKBenchmarkBackend::ResetMeasurements()
{
    m_Checksum = 1469598103934665603ull;
    m_DrawCount = 0;
    m_TransientBytes = 0;
    m_PassCount = 0;
    m_SubmitCount = 0;
    m_PresentCount = 0;
    m_NextTransientToken = 1;
    m_LastConstantRevisions.fill(0);
}

CKERROR CKBenchmarkBackend::BeginPass(const CKBackendPassDesc *desc)
{
    if (!desc)
        return CKERR_INVALIDPARAMETER;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (desc->Rect.left < 0 || desc->Rect.top < 0 ||
        desc->Rect.right <= desc->Rect.left ||
        desc->Rect.bottom <= desc->Rect.top)
        return CKERR_INVALIDPARAMETER;
    m_PassOpen = TRUE;
    ++m_PassCount;
    m_Checksum = Mix(m_Checksum, desc->RenderTarget);
    m_Checksum = Mix(m_Checksum, desc->ClearFlags);
    m_Checksum = SampleBytes(m_Checksum, &desc->Rect, sizeof(desc->Rect));
    m_Checksum = Mix(m_Checksum, desc->ClearColor);
    m_Checksum = SampleBytes(m_Checksum, &desc->ClearZ,
                             sizeof(desc->ClearZ));
    m_Checksum = Mix(m_Checksum, desc->ClearStencil);
    return CK_OK;
}

CKBOOL CKBenchmarkBackend::AllocTransientVertices(
    CKDWORD count, CKDWORD layout, CKBackendTransientVertices *out)
{
    if (!out || count == 0 || !m_Initialized)
        return FALSE;
    const CKRecordingObject *object = FindObject(layout);
    if (!object || object->Type != CKRST_OBJ_VERTEXLAYOUT ||
        object->Stride == 0)
        return FALSE;
    const size_t byteCount = static_cast<size_t>(count) * object->Stride;
    if (byteCount > m_VertexScratch.size())
        return FALSE;
    *out = CKBackendTransientVertices();
    out->Data = m_VertexScratch.data();
    out->Count = count;
    out->Stride = object->Stride;
    out->Layout = layout;
    out->Token = m_NextTransientToken++;
    return TRUE;
}

CKBOOL CKBenchmarkBackend::AllocTransientIndices(
    CKDWORD count, CKBOOL index32, CKBackendTransientIndices *out)
{
    if (!out || count == 0 || !m_Initialized)
        return FALSE;
    const size_t byteCount = static_cast<size_t>(count) * (index32 ? 4u : 2u);
    if (byteCount > m_IndexScratch.size())
        return FALSE;
    *out = CKBackendTransientIndices();
    out->Data = m_IndexScratch.data();
    out->Count = count;
    out->Index32 = index32;
    out->Token = m_NextTransientToken++;
    return TRUE;
}

uint64_t CKBenchmarkBackend::HashDraw(uint64_t hash,
                                      const CKBackendDraw &draw)
{
    hash = Mix(hash, draw.Program);
    hash = Mix(hash, draw.Pipeline.State.Lo);
    hash = Mix(hash, draw.Pipeline.State.Mid);
    hash = Mix(hash, draw.Pipeline.State.Hi);
    hash = Mix(hash, draw.Pipeline.StencilRef);
    hash = Mix(hash, draw.Pipeline.StencilReadMask);
    hash = Mix(hash, draw.Pipeline.StencilWriteMask);
    hash = Mix(hash, draw.Pipeline.ScissorEnabled);
    hash = SampleBytes(hash, &draw.Pipeline.Scissor,
                       sizeof(draw.Pipeline.Scissor));
    hash = SampleBytes(hash, &draw.Pipeline.PointSize,
                       sizeof(draw.Pipeline.PointSize));
    hash = Mix(hash, draw.Layout);
    hash = Mix(hash, draw.VertexBuffer);
    hash = Mix(hash, draw.StartVertex);
    hash = Mix(hash, draw.VertexCount);
    hash = Mix(hash, draw.IndexBuffer);
    hash = Mix(hash, draw.StartIndex);
    hash = Mix(hash, draw.IndexCount);
    hash = Mix(hash, draw.Stream1Layout);
    hash = Mix(hash, draw.Stream1VertexBuffer);
    hash = Mix(hash, draw.Stream1StartVertex);

    if (draw.Textures) {
        for (const CKBackendTextureBinding &binding : *draw.Textures) {
            hash = Mix(hash, binding.Texture);
            hash = Mix(hash, binding.Sampler.MinFilter);
            hash = Mix(hash, binding.Sampler.MagFilter);
            hash = Mix(hash, binding.Sampler.MipFilter);
            hash = Mix(hash, binding.Sampler.AddressU);
            hash = Mix(hash, binding.Sampler.AddressV);
            hash = Mix(hash, binding.Sampler.AddressW);
            hash = Mix(hash, binding.Sampler.BorderColor);
            hash = Mix(hash, binding.Sampler.CompareFunc);
        }
    }
    if (draw.Constants) {
        hash = Mix(hash, draw.Constants->Identity());
        for (CKDWORD slot = 0; slot < CKBACKEND_MAX_CONSTANT_SLOTS; ++slot) {
            const CKBackendConstantValue &value = (*draw.Constants)[slot];
            hash = Mix(hash,
                       value.Revision != m_LastConstantRevisions[slot]);
            hash = Mix(hash, value.Bytes.size());
            m_LastConstantRevisions[slot] = value.Revision;
        }
    }
    if (draw.TransientVertices && draw.TransientVertices->Data) {
        const size_t size = static_cast<size_t>(draw.TransientVertices->Count) *
                            draw.TransientVertices->Stride;
        hash = SampleBytes(hash, draw.TransientVertices->Data, size);
    }
    if (draw.TransientIndices && draw.TransientIndices->Data) {
        const size_t size = static_cast<size_t>(draw.TransientIndices->Count) *
                            (draw.TransientIndices->Index32 ? 4u : 2u);
        hash = SampleBytes(hash, draw.TransientIndices->Data, size);
    }
    return hash;
}

CKERROR CKBenchmarkBackend::Draw(const CKBackendDraw *draw)
{
    if (!draw || !draw->Program)
        return CKERR_INVALIDPARAMETER;
    if (!m_Initialized || !m_PassOpen)
        return CKERR_INVALIDOPERATION;
    m_Checksum = HashDraw(m_Checksum, *draw);
    ++m_DrawCount;
    if (draw->TransientVertices)
        m_TransientBytes += static_cast<uint64_t>(draw->TransientVertices->Count) *
                            draw->TransientVertices->Stride;
    if (draw->TransientIndices)
        m_TransientBytes += static_cast<uint64_t>(draw->TransientIndices->Count) *
                            (draw->TransientIndices->Index32 ? 4u : 2u);
    return CK_OK;
}

CKERROR CKBenchmarkBackend::PresentTexture(CKDWORD texture, CKDWORD width,
                                            CKDWORD height,
                                            CKBackendPresentSync sync)
{
    if (!m_Initialized || texture == 0 || width == 0 || height == 0)
        return CKERR_INVALIDPARAMETER;
    m_Checksum = Mix(m_Checksum, texture);
    m_Checksum = Mix(m_Checksum, width);
    m_Checksum = Mix(m_Checksum, height);
    m_Checksum = Mix(m_Checksum, sync);
    ++m_PresentCount;
    m_PassOpen = FALSE;
    return CK_OK;
}

CKERROR CKBenchmarkBackend::Submit(const CKBackendSubmitDesc &desc,
                                    CKDWORD *frameNumber)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (desc.Sync != CKRST_BACKEND_SYNC_IMMEDIATE &&
        desc.Sync != CKRST_BACKEND_SYNC_VSYNC &&
        desc.Sync != CKRST_BACKEND_SYNC_UNCHANGED)
        return CKERR_INVALIDPARAMETER;
    ++m_FrameNumber;
    ++m_SubmitCount;
    m_PassOpen = FALSE;
    if (frameNumber)
        *frameNumber = m_FrameNumber;
    m_Checksum = Mix(m_Checksum, desc.Sync);
    m_Checksum = Mix(m_Checksum, desc.PresentWindow);
    return CK_OK;
}

CKRecordingBackend *CKBenchmarkBackendDriver::NewBackend()
{
    return new (std::nothrow) CKBenchmarkBackend(GetBackendConventions());
}

CKRecordingBackendDriver *CKBenchmarkBackendLibrary::NewDriver()
{
    return new (std::nothrow) CKBenchmarkBackendDriver();
}
