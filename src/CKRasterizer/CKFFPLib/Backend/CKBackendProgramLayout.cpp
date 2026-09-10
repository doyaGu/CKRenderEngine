#include "CKBackendProgramLayout.h"
#include <algorithm>
#include <cstring>
#include <atomic>

CKBackendConstants::CKBackendConstants()
{
    static std::atomic<uint64_t> nextIdentity{1};
    m_Identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
}

CKERROR CKBackendConstants::Set(CKDWORD slot, const void *data, CKDWORD byteSize)
{
    if (slot >= m_Values.size() || !data || !byteSize || byteSize > CKBACKEND_MAX_UNIFORM_BYTES)
        return CKERR_INVALIDPARAMETER;
    auto &value = m_Values[slot];
    if (value.Bytes.size() >= byteSize && std::memcmp(value.Bytes.data(), data, byteSize) == 0)
        return CK_OK;
    if (value.Bytes.size() < byteSize) value.Bytes.resize(byteSize);
    std::memcpy(value.Bytes.data(), data, byteSize);
    ++value.Revision;
    return CK_OK;
}

void CKBackendProgramLayout::Init(const CKBackendProgramDesc &desc)
{
    Buffers.clear(); Copies.clear(); Data.clear(); SourceIdentity = 0;
    for (size_t i = 0; i < desc.UniformBuffers.size(); ++i) {
        const auto &source = desc.UniformBuffers[i];
        CKDWORD offset = CKDWORD(Data.size());
        bool shared = false;
        if (source.SharedData != ~0u) {
            for (size_t j = 0; j < i; ++j) {
                if (desc.UniformBuffers[j].SharedData == source.SharedData) {
                    offset = Buffers[j].Offset; shared = true; break;
                }
            }
        }
        if (!shared) Data.resize(Data.size() + source.Size, 0);
        Buffers.push_back({source.Stage, source.Slot, offset, source.Size});
    }
    for (const auto &uniform : desc.Uniforms) {
        const CKDWORD offset = BufferOffset(uniform.Stage, uniform.BufferSlot) + uniform.Offset;
        const auto found = std::find_if(Copies.begin(), Copies.end(), [&](const Copy &copy) {
            return copy.Slot == uniform.Slot && copy.Offset == offset && copy.Size == uniform.Size();
        });
        if (found == Copies.end()) Copies.push_back({uniform.Slot, offset, uniform.Size()});
    }
}

void CKBackendProgramLayout::Update(const CKBackendConstants &values)
{
    const bool sourceChanged = SourceIdentity != values.Identity();
    SourceIdentity = values.Identity();
    for (auto &copy : Copies) {
        const auto &source = values[copy.Slot];
        if (!sourceChanged && copy.Revision == source.Revision) continue;
        const size_t size = (std::min)(size_t(copy.Size), source.Bytes.size());
        if (size) std::memcpy(Data.data() + copy.Offset, source.Bytes.data(), size);
        if (size < copy.Size) std::memset(Data.data() + copy.Offset + size, 0, copy.Size - size);
        copy.Revision = source.Revision;
    }
}

CKDWORD CKBackendProgramLayout::BufferOffset(CK_SHADER_STAGE stage, CKDWORD slot) const
{
    for (const auto &buffer : Buffers)
        if (buffer.Stage == stage && buffer.Slot == slot) return buffer.Offset;
    return ~0u;
}
