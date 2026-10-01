#ifndef CKSDLGPU_UNIFORMBATCH_H
#define CKSDLGPU_UNIFORMBATCH_H

#include "CKFFProgramLayout.h"
#include <string.h>

// Batch byte arenas grow geometrically and keep their storage from one batch
// to the next, so steady-state frames neither reallocate nor recopy earlier
// draws. An arena that grew past the retention cap is released at reset.
inline void CKSdlGpuGrowBytes(XArray<CKBYTE> &bytes, int size) {
    if (size > bytes.Allocated()) {
        int capacity = bytes.Allocated() ? bytes.Allocated() : 4096;
        while (capacity < size)
            capacity = capacity > 0x3fffffff ? size : capacity * 2;
        bytes.Reserve(capacity);
    }
    bytes.Resize(size);
}

inline void CKSdlGpuResetBytes(XArray<CKBYTE> &bytes) {
    static const int kRetainedBytes = 4 * 1024 * 1024;
    if (bytes.Allocated() > kRetainedBytes) bytes.Clear();
    else bytes.Resize(0);
}

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
                const int oldSize = Data.Size();
                offsets[i] = unsigned(oldSize);
                CKSdlGpuGrowBytes(Data, oldSize + (int)buffer.Size);
                memcpy(Data.Begin() + oldSize, source, buffer.Size);
            }
        }
        cursor.Batch = Serial;
        memcpy(cursor.Offsets, offsets, sizeof(cursor.Offsets));
        for (int i = 0; i < layout.Buffers.Size(); ++i)
            cursor.Changes[i] = layout.Buffers[i].Change;
    }

    // Starts the next batch. Offsets from the previous batch become stale.
    void Reset() {
        CKSdlGpuResetBytes(Data);
        if (!++Serial) Serial = 1;
    }

    void Clear() {
        Data.Clear();
        if (!++Serial) Serial = 1;
    }

private:
    uint64_t Serial = 1;
};

// Native bindings belong to one render pass. SDL applies pushed uniform data
// to every later draw in the command buffer, across pipeline binds, so a draw
// whose slot would receive the bytes it already holds skips the push.
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
