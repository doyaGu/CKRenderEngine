#include "CKBackendProgramLayout.h"
#include <algorithm>
#include <cstring>

void CKBackendProgramLayout::Init(const CKBackendProgramDesc &desc)
{
    Buffers.clear(); Copies.clear(); Data.clear();
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

void CKBackendProgramLayout::Update(const CKBackendConstantValues &values)
{
    for (auto &copy : Copies) {
        const auto &source = values[copy.Slot];
        if (copy.Revision == source.Revision) continue;
        const size_t size = (std::min)(size_t(copy.Size), source.Bytes.size());
        if (size) std::memcpy(Data.data() + copy.Offset, source.Bytes.data(), size);
        copy.Revision = source.Revision;
    }
}

CKDWORD CKBackendProgramLayout::BufferOffset(CK_SHADER_STAGE stage, CKDWORD slot) const
{
    for (const auto &buffer : Buffers)
        if (buffer.Stage == stage && buffer.Slot == slot) return buffer.Offset;
    return ~0u;
}
