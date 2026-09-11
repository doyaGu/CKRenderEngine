#ifndef CKRE_CK_BENCHMARK_BACKEND_H
#define CKRE_CK_BENCHMARK_BACKEND_H

#include "CKRecordingRasterizer.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Allocation-free command sink for CKFFPLib CPU benchmarks. Resource and
// shader setup still use CKRecordingBackend, but timed frame operations do not
// append command records or copy complete payloads.
class CKBenchmarkBackend : public CKRecordingBackend {
public:
    explicit CKBenchmarkBackend(const CKBackendCaps &conventions = CKBackendCaps());

    CKBOOL IsIdle() const override { return TRUE; }
    CKERROR BeginPass(const CKBackendPassDesc *desc) override;
    CKBOOL AllocTransientVertices(CKDWORD count, CKDWORD layout,
                                  CKBackendTransientVertices *out) override;
    CKBOOL AllocTransientIndices(CKDWORD count, CKBOOL index32,
                                 CKBackendTransientIndices *out) override;
    CKERROR Draw(const CKBackendDraw *draw) override;
    CKERROR PresentTexture(CKDWORD texture, CKDWORD width, CKDWORD height,
                           CKBackendPresentSync sync) override;
    CKERROR Submit(const CKBackendSubmitDesc &desc,
                   CKDWORD *frameNumber) override;

    void ResetMeasurements();
    uint64_t Checksum() const { return m_Checksum; }
    uint64_t DrawCount() const { return m_DrawCount; }
    uint64_t TransientBytes() const { return m_TransientBytes; }
    uint64_t PassCount() const { return m_PassCount; }
    uint64_t SubmitCount() const { return m_SubmitCount; }
    uint64_t PresentCount() const { return m_PresentCount; }

private:
    static constexpr size_t ScratchCapacity = 256u * 1024u;

    static uint64_t Mix(uint64_t hash, uint64_t value);
    static uint64_t SampleBytes(uint64_t hash, const void *data, size_t size);
    uint64_t HashDraw(uint64_t hash, const CKBackendDraw &draw);

    std::array<CKBYTE, ScratchCapacity> m_VertexScratch{};
    std::array<CKBYTE, ScratchCapacity> m_IndexScratch{};
    CKDWORD m_NextTransientToken = 1;
    uint64_t m_Checksum = 1469598103934665603ull;
    uint64_t m_DrawCount = 0;
    uint64_t m_TransientBytes = 0;
    uint64_t m_PassCount = 0;
    uint64_t m_SubmitCount = 0;
    uint64_t m_PresentCount = 0;
    std::array<uint64_t, CKBACKEND_MAX_CONSTANT_SLOTS>
        m_LastConstantRevisions{};
};

class CKBenchmarkRasterizerDriver : public CKRecordingRasterizerDriver {
protected:
    CKRecordingBackend *NewBackend() override;
};

class CKBenchmarkRasterizer : public CKRecordingRasterizer {
protected:
    CKRecordingRasterizerDriver *NewDriver() override;
};

#endif // CKRE_CK_BENCHMARK_BACKEND_H
