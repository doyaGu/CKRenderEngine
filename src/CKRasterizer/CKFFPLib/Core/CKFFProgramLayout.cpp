#include "CKFFProgramLayout.h"
#include <cstring>

CKERROR CKFFConstantSet::Set(CKDWORD slot, const void *data, CKDWORD byteSize)
{
    if (slot >= CKFF_CONSTANT_SLOT_COUNT || !data || !byteSize ||
        byteSize > CKFF_MAX_CONSTANT_BYTES)
        return CKERR_INVALIDPARAMETER;
    auto &value = m_Values[slot];
    if ((CKDWORD)value.Bytes.Size() >= byteSize &&
        std::memcmp(value.Bytes.Begin(), data, byteSize) == 0)
        return CK_OK;
    if ((CKDWORD)value.Bytes.Size() < byteSize)
        value.Bytes.Resize((int)byteSize);
    std::memcpy(value.Bytes.Begin(), data, byteSize);
    ++value.Change;
    if (value.Change == 0)
        value.Change = 1;
    return CK_OK;
}

void CKFFProgramLayout::Init(const CKFFProgramDesc &desc)
{
    Buffers.Clear();
    Copies.Clear();
    Data.Clear();
    SourceIdentity = 0;
    for (int i = 0; i < desc.UniformBuffers.Size(); ++i) {
        const auto &source = desc.UniformBuffers[i];
        CKDWORD offset = CKDWORD(Data.Size());
        bool shared = false;
        if (source.SharedData != UINT32_MAX) {
            for (int j = 0; j < i; ++j) {
                if (desc.UniformBuffers[j].SharedData == source.SharedData) {
                    offset = Buffers[j].Offset; shared = true; break;
                }
            }
        }
        if (!shared) {
            const int oldSize = Data.Size();
            Data.Resize(oldSize + (int)source.Size);
            memset(Data.Begin() + oldSize, 0, source.Size);
        }
        Buffer buffer;
        buffer.Stage = source.Stage;
        buffer.Slot = source.Slot;
        buffer.Offset = offset;
        buffer.Size = source.Size;
        Buffers.PushBack(buffer);
    }
    for (int i = 0; i < desc.Uniforms.Size(); ++i) {
        const CKFFUniformBinding &uniform = desc.Uniforms[i];
        const CKDWORD offset = BufferOffset(uniform.Stage, uniform.BufferSlot) + uniform.Offset;
        bool duplicate = false;
        for (int copyIndex = 0; copyIndex < Copies.Size(); ++copyIndex) {
            const Copy &copy = Copies[copyIndex];
            if (copy.Slot == uniform.Slot && copy.Offset == offset &&
                copy.Size == uniform.Size()) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            Copy copy;
            copy.Slot = uniform.Slot;
            copy.Offset = offset;
            copy.Size = uniform.Size();
            Copies.PushBack(copy);
        }
    }
}

void CKFFProgramLayout::Update(const CKFFConstantSet &values)
{
    const bool sourceChanged = SourceIdentity != values.Identity();
    SourceIdentity = values.Identity();
    for (int i = 0; i < Copies.Size(); ++i) {
        Copy &copy = Copies[i];
        const auto &source = values[copy.Slot];
        if (!sourceChanged && copy.Change == source.Change) continue;
        const CKDWORD size = XMin(copy.Size, (CKDWORD)source.Bytes.Size());
        if (size) std::memcpy(Data.Begin() + copy.Offset, source.Bytes.Begin(), size);
        if (size < copy.Size) std::memset(Data.Begin() + copy.Offset + size, 0, copy.Size - size);
        MarkDataChanged(copy.Offset, copy.Size);
        copy.Change = source.Change;
    }
}

bool CKFFProgramLayout::MarkDataChanged(CKDWORD offset, CKDWORD size)
{
    if (!size) return true;
    if (offset > (CKDWORD)Data.Size() || size > (CKDWORD)Data.Size() - offset) return false;
    const uint64_t end = uint64_t(offset) + size;
    for (int i = 0; i < Buffers.Size(); ++i) {
        const auto &buffer = Buffers[i];
        if (uint64_t(buffer.Offset) >= end ||
            uint64_t(offset) >= uint64_t(buffer.Offset) + buffer.Size) continue;
        bool sharedAlreadyAdvanced = false;
        for (int j = 0; j < i; ++j) {
            if (Buffers[j].Offset == buffer.Offset) {
                sharedAlreadyAdvanced = true;
                break;
            }
        }
        if (sharedAlreadyAdvanced) continue;
        CKQWORD change = buffer.Change + 1;
        if (!change) change = 1;
        for (int aliasIndex = 0; aliasIndex < Buffers.Size(); ++aliasIndex)
            if (Buffers[aliasIndex].Offset == buffer.Offset)
                Buffers[aliasIndex].Change = change;
    }
    return true;
}

CKDWORD CKFFProgramLayout::BufferOffset(CK_SHADER_STAGE stage, CKDWORD slot) const
{
    for (int i = 0; i < Buffers.Size(); ++i)
        if (Buffers[i].Stage == stage && Buffers[i].Slot == slot)
            return Buffers[i].Offset;
    return UINT32_MAX;
}
