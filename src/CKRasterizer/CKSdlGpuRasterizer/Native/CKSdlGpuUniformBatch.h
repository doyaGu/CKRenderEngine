#ifndef CKSDLGPU_UNIFORMBATCH_H
#define CKSDLGPU_UNIFORMBATCH_H

#include "CKFFProgramLayout.h"
#include <string.h>

// Each program remembers which immutable buffer data was last copied in this batch.
// Offsets remain valid when the arena grows; no caller or packing-cache pointer
// is retained.
struct CKSdlGpuUniformCursor {
    uint64_t Batch = 0;
    unsigned Offsets[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
    CKQWORD Changes[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
};

class CKSdlGpuUniformBatch {
public:
    XArray<CKBYTE> Data;

    void Snapshot(const CKFFProgramLayout &layout, CKSdlGpuUniformCursor &cursor,
                  unsigned *offsets) {
        const bool sameBatch = cursor.Batch == Serial;
        for (int i = 0; i < layout.Buffers.Size(); ++i) {
            const auto &buffer = layout.Buffers[i];
            bool shared = false;
            for (int j = 0; j < i; ++j) {
                if (layout.Buffers[j].Offset == buffer.Offset) {
                    offsets[i] = offsets[j]; shared = true; break;
                }
            }
            if (shared) continue;
            const CKBYTE *source = layout.Data.Begin() + buffer.Offset;
            if (sameBatch && cursor.Changes[i] == buffer.Change) {
                offsets[i] = cursor.Offsets[i];
            } else {
                offsets[i] = unsigned(Data.Size());
                const int oldSize = Data.Size();
                Data.Resize(oldSize + (int)buffer.Size);
                memcpy(Data.Begin() + oldSize, source, buffer.Size);
            }
        }
        cursor.Batch = Serial;
        memcpy(cursor.Offsets, offsets, sizeof(cursor.Offsets));
        for (int i = 0; i < layout.Buffers.Size(); ++i)
            cursor.Changes[i] = layout.Buffers[i].Change;
    }

    void Clear() {
        Data.Clear();
        if (!++Serial) Serial = 1;
    }

private:
    uint64_t Serial = 1;
};

// Native bindings belong to one render pass. Pipeline changes invalidate this
// view, while repeated draws with the same pipeline can reuse pushed bytes.
class CKSdlGpuUniformBindings {
public:
    bool NeedsPush(const CKFFProgramLayout::Buffer &buffer, unsigned offset,
                   const XArray<CKBYTE> &data) {
        auto &bound = Buffers[buffer.Stage == CKRST_SHADER_VERTEX ? 0 : 1][buffer.Slot];
        if (bound.Size == buffer.Size &&
            (bound.Offset == offset || memcmp(data.Begin() + bound.Offset,
                                              data.Begin() + offset, buffer.Size) == 0)) return false;
        bound = {offset, buffer.Size};
        return true;
    }
    void Invalidate() { memset(Buffers, 0, sizeof(Buffers)); }
private:
    struct Binding { unsigned Offset = 0, Size = 0; };
    Binding Buffers[2][CKFF_UNIFORM_BUFFER_COUNT] = {};
};

#endif
