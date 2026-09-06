#ifndef CKSDLGPU_UNIFORMBATCH_H
#define CKSDLGPU_UNIFORMBATCH_H

#include "CKBackendProgramLayout.h"
#include <array>
#include <cstring>

// Each program remembers its latest immutable buffer versions in this batch.
// Byte comparison includes backend-generated sampler metadata. Offsets remain
// valid when the arena grows; no caller or packing-cache pointer is retained.
struct CKSdlGpuUniformCursor {
    uint64_t Batch = 0;
    std::array<unsigned, 2 * CKBACKEND_MAX_UNIFORM_BUFFERS> Offsets = {};
};

class CKSdlGpuUniformBatch {
public:
    std::vector<CKBYTE> Data;

    void Snapshot(const CKBackendProgramLayout &layout, CKSdlGpuUniformCursor &cursor,
                  std::array<unsigned, 2 * CKBACKEND_MAX_UNIFORM_BUFFERS> &offsets) {
        const bool sameBatch = cursor.Batch == Serial;
        for (size_t i = 0; i < layout.Buffers.size(); ++i) {
            const auto &buffer = layout.Buffers[i];
            bool shared = false;
            for (size_t j = 0; j < i; ++j) {
                if (layout.Buffers[j].Offset == buffer.Offset) {
                    offsets[i] = offsets[j]; shared = true; break;
                }
            }
            if (shared) continue;
            const CKBYTE *source = layout.Data.data() + buffer.Offset;
            if (sameBatch && std::memcmp(Data.data() + cursor.Offsets[i], source, buffer.Size) == 0) {
                offsets[i] = cursor.Offsets[i];
            } else {
                offsets[i] = unsigned(Data.size());
                Data.insert(Data.end(), source, source + buffer.Size);
            }
        }
        cursor.Batch = Serial;
        cursor.Offsets = offsets;
    }

    void Clear() { Data.clear(); ++Serial; }

private:
    uint64_t Serial = 1;
};

// Native bindings belong to one render pass. Pipeline changes invalidate this
// view, while repeated draws with the same pipeline can reuse pushed bytes.
class CKSdlGpuUniformBindings {
public:
    bool NeedsPush(const CKBackendProgramLayout::Buffer &buffer, unsigned offset,
                   const std::vector<CKBYTE> &data) {
        auto &bound = Buffers[buffer.Stage == CKRST_SHADER_VERTEX ? 0 : 1][buffer.Slot];
        if (bound.Size == buffer.Size &&
            (bound.Offset == offset || std::memcmp(data.data() + bound.Offset,
                                                   data.data() + offset, buffer.Size) == 0)) return false;
        bound = {offset, buffer.Size};
        return true;
    }
    void Invalidate() { Buffers = {}; }
private:
    struct Binding { unsigned Offset = 0, Size = 0; };
    std::array<std::array<Binding, CKBACKEND_MAX_UNIFORM_BUFFERS>, 2> Buffers = {};
};

#endif
