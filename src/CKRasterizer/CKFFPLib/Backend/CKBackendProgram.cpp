#include "CKBackendProgram.h"
#include "CKError.h"

#include <map>
#include <utility>

namespace {

bool ValidStage(CK_SHADER_STAGE stage)
{
    return stage == CKRST_SHADER_VERTEX || stage == CKRST_SHADER_PIXEL;
}

bool ValidShaderTarget(CK_SHADER_FORMAT format, CK_SHADER_PROFILE profile)
{
    switch (format) {
    case CKRST_SHADER_FORMAT_BGFX:
        return profile == CKRST_SHADER_PROFILE_DX11 || profile == CKRST_SHADER_PROFILE_DX12 ||
               profile == CKRST_SHADER_PROFILE_SPIRV || profile == CKRST_SHADER_PROFILE_GLSL ||
               profile == CKRST_SHADER_PROFILE_ESSL || profile == CKRST_SHADER_PROFILE_MSL;
    case CKRST_SHADER_FORMAT_DXBC: return profile == CKRST_SHADER_PROFILE_DX11;
    case CKRST_SHADER_FORMAT_DXIL: return profile == CKRST_SHADER_PROFILE_DX12;
    case CKRST_SHADER_FORMAT_SPIRV: return profile == CKRST_SHADER_PROFILE_SPIRV;
    case CKRST_SHADER_FORMAT_MSL:
    case CKRST_SHADER_FORMAT_METALLIB: return profile == CKRST_SHADER_PROFILE_MSL;
    default: return false;
    }
}

bool ValidName(const std::string &name)
{
    if (name.empty())
        return false;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(i != 0 && c >= '0' && c <= '9'))
            return false;
    }
    return true;
}

struct BufferRanges {
    const CKBackendUniformBufferBinding *Binding = nullptr;
    std::array<bool, CKBACKEND_MAX_UNIFORM_BYTES / 16> Occupied = {};

    bool Reserve(CKDWORD offset, CKDWORD size)
    {
        if (!Binding || offset % 16 != 0 || size == 0 || size % 16 != 0 ||
            offset > Binding->Size || size > Binding->Size - offset)
            return false;
        for (CKDWORD i = offset / 16; i < (offset + size) / 16; ++i) {
            if (Occupied[i])
                return false;
        }
        for (CKDWORD i = offset / 16; i < (offset + size) / 16; ++i)
            Occupied[i] = true;
        return true;
    }
};

struct SharedWrite {
    enum Kind { Uniform, BorderColor, SamplerState } Type;
    CKDWORD LogicalSlot;
    CKBackendUniformType UniformType;
    CKDWORD Count;
    CKDWORD Offset;
    CKDWORD Size;
};

struct SharedBufferRanges {
    CKDWORD Size = 0;
    std::vector<SharedWrite> Writes;

    bool Reserve(const SharedWrite &write)
    {
        for (const auto &previous : Writes) {
            if (write.Offset >= previous.Offset + previous.Size ||
                previous.Offset >= write.Offset + write.Size)
                continue;
            return write.Type == previous.Type && write.LogicalSlot == previous.LogicalSlot &&
                   write.UniformType == previous.UniformType && write.Count == previous.Count &&
                   write.Offset == previous.Offset && write.Size == previous.Size;
        }
        Writes.push_back(write);
        return true;
    }
};

bool ReserveShared(std::map<CKDWORD, SharedBufferRanges> &sharedBuffers,
                   const BufferRanges &buffer, const SharedWrite &write)
{
    return buffer.Binding->SharedData == ~0u ||
           sharedBuffers[buffer.Binding->SharedData].Reserve(write);
}

// bgfx uniform names are shared between shader stages, including samplers.
// A name cannot designate different native types in the same program.
bool RegisterName(std::map<std::string, std::pair<int, CKDWORD>> &names,
                  const std::string &name, int type, CKDWORD count)
{
    if (!ValidName(name))
        return false;
    const auto signature = std::make_pair(type, count);
    const auto existing = names.emplace(name, signature);
    return existing.second || existing.first->second == signature;
}

} // namespace

CKERROR CKValidateBackendProgram(const CKBackendProgramDesc &desc,
                                const CKShaderDesc &vertex, const CKShaderDesc &pixel)
{
    // The backend must resolve both handles before calling this function;
    // resource lifetime and generation checks belong to its handle table.
    if (!desc.VertexShader || !desc.PixelShader || desc.VertexShader == desc.PixelShader ||
        vertex.Stage != CKRST_SHADER_VERTEX || pixel.Stage != CKRST_SHADER_PIXEL ||
        vertex.Format != pixel.Format || vertex.Profile != pixel.Profile ||
        !ValidShaderTarget(vertex.Format, vertex.Profile) ||
        vertex.StorageTextureCount || vertex.StorageBufferCount ||
        pixel.StorageTextureCount || pixel.StorageBufferCount)
        return CKERR_INVALIDPARAMETER;

    const bool namedUniforms = vertex.Format == CKRST_SHADER_FORMAT_BGFX;
    BufferRanges buffers[2][CKBACKEND_MAX_UNIFORM_BUFFERS];
    std::map<CKDWORD, SharedBufferRanges> sharedBuffers;
    CKDWORD bufferCounts[2] = {};
    for (const auto &buffer : desc.UniformBuffers) {
        if (!ValidStage(buffer.Stage) || buffer.Slot >= CKBACKEND_MAX_UNIFORM_BUFFERS ||
            !buffer.Size || buffer.Size > CKBACKEND_MAX_UNIFORM_BYTES || buffer.Size % 16 != 0)
            return CKERR_INVALIDPARAMETER;
        auto &ranges = buffers[buffer.Stage][buffer.Slot];
        if (ranges.Binding)
            return CKERR_INVALIDPARAMETER;
        ranges.Binding = &buffer;
        ++bufferCounts[buffer.Stage];
        if (buffer.SharedData != ~0u) {
            auto &shared = sharedBuffers[buffer.SharedData];
            if (shared.Size && shared.Size != buffer.Size)
                return CKERR_INVALIDPARAMETER;
            shared.Size = buffer.Size;
        }
    }
    for (CKDWORD stage = 0; stage < 2; ++stage) {
        for (CKDWORD slot = 0; slot < bufferCounts[stage]; ++slot) {
            if (!buffers[stage][slot].Binding)
                return CKERR_INVALIDPARAMETER;
        }
    }

    const CKBackendUniformBinding *uniforms[2][CKBACKEND_MAX_CONSTANT_SLOTS] = {};
    std::map<std::string, std::pair<int, CKDWORD>> names;
    for (const auto &uniform : desc.Uniforms) {
        if (!ValidStage(uniform.Stage) || uniform.Slot >= CKBACKEND_MAX_CONSTANT_SLOTS ||
            uniform.BufferSlot >= CKBACKEND_MAX_UNIFORM_BUFFERS || uniform.Offset % 16 != 0 ||
            (uniform.Type != CKBACKEND_UNIFORM_VEC4 && uniform.Type != CKBACKEND_UNIFORM_MAT4))
            return CKERR_INVALIDPARAMETER;
        const CKDWORD elementSize = uniform.Type == CKBACKEND_UNIFORM_MAT4 ? 64u : 16u;
        // Check the multiplication before calling Size(), including named uniforms.
        if (!uniform.Count || uniform.Count > CKBACKEND_MAX_UNIFORM_BYTES / elementSize)
            return CKERR_INVALIDPARAMETER;
        auto &logical = uniforms[uniform.Stage][uniform.Slot];
        const auto *otherStage = uniforms[1 - uniform.Stage][uniform.Slot];
        if (logical || (otherStage &&
                       (otherStage->Type != uniform.Type || otherStage->Count != uniform.Count)))
            return CKERR_INVALIDPARAMETER;
        logical = &uniform;
        // Pure named-uniform programs do not need invented native buffers.
        // Once a stage declares buffers, all of its ranges must be consistent.
        if (!namedUniforms || bufferCounts[uniform.Stage]) {
            auto &buffer = buffers[uniform.Stage][uniform.BufferSlot];
            if (!buffer.Reserve(uniform.Offset, uniform.Size()) ||
                !ReserveShared(sharedBuffers, buffer, {SharedWrite::Uniform, uniform.Slot,
                    uniform.Type, uniform.Count, uniform.Offset, uniform.Size()}))
                return CKERR_INVALIDPARAMETER;
        }
        if (namedUniforms && !RegisterName(names, uniform.Name, uniform.Type, uniform.Count))
            return CKERR_INVALIDPARAMETER;
    }

    const CKBackendSamplerBinding *logicalSamplers[2][CKBACKEND_MAX_TEXTURE_SLOTS] = {};
    bool nativeSamplers[2][CKBACKEND_MAX_TEXTURE_SLOTS] = {};
    CKDWORD samplerCounts[2] = {};
    for (const auto &sampler : desc.Samplers) {
        if (!ValidStage(sampler.Stage) || sampler.Slot >= CKBACKEND_MAX_TEXTURE_SLOTS ||
            sampler.NativeSlot >= CKBACKEND_MAX_TEXTURE_SLOTS ||
            (sampler.Dimension != CKBACKEND_TEXTURE_2D && sampler.Dimension != CKBACKEND_TEXTURE_CUBE &&
             sampler.Dimension != CKBACKEND_TEXTURE_3D) ||
            logicalSamplers[sampler.Stage][sampler.Slot] || nativeSamplers[sampler.Stage][sampler.NativeSlot])
            return CKERR_INVALIDPARAMETER;
        // BindTexture supplies one image per logical slot to both stages.
        // Native indices and zero-binding defaults may differ, but one image
        // cannot satisfy incompatible dimensional types at the same time.
        const auto *otherStage = logicalSamplers[1 - sampler.Stage][sampler.Slot];
        if (otherStage && otherStage->Dimension != sampler.Dimension)
            return CKERR_INVALIDPARAMETER;
        logicalSamplers[sampler.Stage][sampler.Slot] = &sampler;
        nativeSamplers[sampler.Stage][sampler.NativeSlot] = true;
        ++samplerCounts[sampler.Stage];
        if (namedUniforms && !RegisterName(names, sampler.Name, 2 + sampler.Dimension, 1))
            return CKERR_INVALIDPARAMETER;
        if (sampler.MetadataBufferSlot != ~0u) {
            if (sampler.MetadataBufferSlot >= CKBACKEND_MAX_UNIFORM_BUFFERS)
                return CKERR_INVALIDPARAMETER;
            auto &buffer = buffers[sampler.Stage][sampler.MetadataBufferSlot];
            if (!buffer.Reserve(sampler.BorderColorOffset, 16) ||
                !buffer.Reserve(sampler.SamplerStateOffset, 16) ||
                !ReserveShared(sharedBuffers, buffer, {SharedWrite::BorderColor, sampler.Slot,
                    CKBACKEND_UNIFORM_VEC4, 1, sampler.BorderColorOffset, 16}) ||
                !ReserveShared(sharedBuffers, buffer, {SharedWrite::SamplerState, sampler.Slot,
                    CKBACKEND_UNIFORM_VEC4, 1, sampler.SamplerStateOffset, 16}))
                return CKERR_INVALIDPARAMETER;
        }
    }
    const CKShaderDesc *shaders[2] = {&vertex, &pixel};
    for (CKDWORD stage = 0; stage < 2; ++stage) {
        for (CKDWORD slot = 0; slot < samplerCounts[stage]; ++slot) {
            if (!nativeSamplers[stage][slot])
                return CKERR_INVALIDPARAMETER;
        }
        if (!namedUniforms && (shaders[stage]->SamplerCount != samplerCounts[stage] ||
                              shaders[stage]->UniformBufferCount != bufferCounts[stage]))
            return CKERR_INVALIDPARAMETER;
    }

    bool attributes[CKRST_ATTRIB_COUNT] = {};
    bool locations[16] = {};
    for (const auto &input : desc.VertexInputs) {
        if (static_cast<CKDWORD>(input.Attribute) >= CKRST_ATTRIB_COUNT || input.Location >= 16 ||
            (input.Integer != FALSE && input.Integer != TRUE) ||
            attributes[input.Attribute] || locations[input.Location])
            return CKERR_INVALIDPARAMETER;
        attributes[input.Attribute] = true;
        locations[input.Location] = true;
    }
    return CK_OK;
}
