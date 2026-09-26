#include "CKFFProgramDesc.h"
#include "CKError.h"

#include <string.h>

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

bool ValidName(const XString &name)
{
    if (name.IsEmpty())
        return false;
    for (int i = 0; i < name.Length(); ++i) {
        const char c = name.CStr()[i];
        if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(i != 0 && c >= '0' && c <= '9'))
            return false;
    }
    return true;
}

struct BufferRanges {
    const CKFFUniformBufferBinding *Binding = nullptr;
    bool Occupied[CKFF_MAX_CONSTANT_BYTES / 16] = {};

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
    CKFFUniformType UniformType;
    CKDWORD Count;
    CKDWORD Offset;
    CKDWORD Size;
};

struct SharedBufferRanges {
    CKDWORD Id = UINT32_MAX;
    CKDWORD Size = 0;
    XClassArray<SharedWrite> Writes;

    bool Reserve(const SharedWrite &write)
    {
        for (int i = 0; i < Writes.Size(); ++i) {
            const SharedWrite &previous = Writes[i];
            if (write.Offset >= previous.Offset + previous.Size ||
                previous.Offset >= write.Offset + write.Size)
                continue;
            return write.Type == previous.Type && write.LogicalSlot == previous.LogicalSlot &&
                   write.UniformType == previous.UniformType && write.Count == previous.Count &&
                   write.Offset == previous.Offset && write.Size == previous.Size;
        }
        Writes.PushBack(write);
        return true;
    }
};

bool ReserveShared(XClassArray<SharedBufferRanges> &sharedBuffers,
                   const BufferRanges &buffer, const SharedWrite &write)
{
    if (buffer.Binding->SharedData == UINT32_MAX)
        return true;
    for (int i = 0; i < sharedBuffers.Size(); ++i) {
        if (sharedBuffers[i].Id == buffer.Binding->SharedData)
            return sharedBuffers[i].Reserve(write);
    }
    SharedBufferRanges ranges;
    ranges.Id = buffer.Binding->SharedData;
    ranges.Size = buffer.Binding->Size;
    sharedBuffers.PushBack(ranges);
    return sharedBuffers.Back().Reserve(write);
}

// bgfx uniform names are shared between shader stages, including samplers.
// A name cannot designate different native types in the same program.
struct NameSignature {
    XString Name;
    int Type = 0;
    CKDWORD Count = 0;
    int MetadataKind = 0;
    CKDWORD LogicalSlot = UINT32_MAX;
};

bool RegisterName(XClassArray<NameSignature> &names,
                  const XString &name, int type, CKDWORD count,
                  int metadataKind = 0, CKDWORD logicalSlot = UINT32_MAX)
{
    if (!ValidName(name))
        return false;
    for (int i = 0; i < names.Size(); ++i) {
        if (strcmp(names[i].Name.CStr(), name.CStr()) == 0)
            return names[i].Type == type && names[i].Count == count &&
                   names[i].MetadataKind == metadataKind &&
                   (metadataKind == 0 || names[i].LogicalSlot == logicalSlot);
    }
    NameSignature signature;
    signature.Name = name;
    signature.Type = type;
    signature.Count = count;
    signature.MetadataKind = metadataKind;
    signature.LogicalSlot = logicalSlot;
    names.PushBack(signature);
    return true;
}

} // namespace

CKERROR CKFFValidateProgram(const CKFFProgramDesc &desc,
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
    BufferRanges buffers[2][CKFF_UNIFORM_BUFFER_COUNT];
    XClassArray<SharedBufferRanges> sharedBuffers;
    CKDWORD bufferCounts[2] = {};
    for (int i = 0; i < desc.UniformBuffers.Size(); ++i) {
        const CKFFUniformBufferBinding &buffer = desc.UniformBuffers[i];
        if (!ValidStage(buffer.Stage) || buffer.Slot >= CKFF_UNIFORM_BUFFER_COUNT ||
            !buffer.Size || buffer.Size > CKFF_MAX_CONSTANT_BYTES || buffer.Size % 16 != 0)
            return CKERR_INVALIDPARAMETER;
        auto &ranges = buffers[buffer.Stage][buffer.Slot];
        if (ranges.Binding)
            return CKERR_INVALIDPARAMETER;
        ranges.Binding = &buffer;
        ++bufferCounts[buffer.Stage];
        if (buffer.SharedData != UINT32_MAX) {
            bool found = false;
            for (int sharedIndex = 0; sharedIndex < sharedBuffers.Size(); ++sharedIndex) {
                SharedBufferRanges &shared = sharedBuffers[sharedIndex];
                if (shared.Id == buffer.SharedData) {
                    if (shared.Size != buffer.Size)
                        return CKERR_INVALIDPARAMETER;
                    found = true;
                    break;
                }
            }
            if (!found) {
                SharedBufferRanges shared;
                shared.Id = buffer.SharedData;
                shared.Size = buffer.Size;
                sharedBuffers.PushBack(shared);
            }
        }
    }
    for (CKDWORD stage = 0; stage < 2; ++stage) {
        for (CKDWORD slot = 0; slot < bufferCounts[stage]; ++slot) {
            if (!buffers[stage][slot].Binding)
                return CKERR_INVALIDPARAMETER;
        }
    }

    const CKFFUniformBinding *uniforms[2][CKFF_CONSTANT_SLOT_COUNT] = {};
    XClassArray<NameSignature> names;
    for (int i = 0; i < desc.Uniforms.Size(); ++i) {
        const CKFFUniformBinding &uniform = desc.Uniforms[i];
        if (!ValidStage(uniform.Stage) || uniform.Slot >= CKFF_CONSTANT_SLOT_COUNT ||
            uniform.BufferSlot >= CKFF_UNIFORM_BUFFER_COUNT || uniform.Offset % 16 != 0 ||
            (uniform.Type != CKFF_UNIFORM_VEC4 && uniform.Type != CKFF_UNIFORM_MAT4))
            return CKERR_INVALIDPARAMETER;
        const CKDWORD elementSize = uniform.Type == CKFF_UNIFORM_MAT4 ? 64u : 16u;
        // Check the multiplication before calling Size(), including named uniforms.
        if (!uniform.Count || uniform.Count > CKFF_MAX_CONSTANT_BYTES / elementSize)
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

    const CKFFSamplerBinding *logicalSamplers[2][CKFF_TEXTURE_SLOT_COUNT] = {};
    bool nativeSamplers[2][CKFF_TEXTURE_SLOT_COUNT] = {};
    CKDWORD samplerCounts[2] = {};
    for (int i = 0; i < desc.Samplers.Size(); ++i) {
        const CKFFSamplerBinding &sampler = desc.Samplers[i];
        if (!ValidStage(sampler.Stage) || sampler.Slot >= CKFF_TEXTURE_SLOT_COUNT ||
            sampler.NativeSlot >= CKFF_TEXTURE_SLOT_COUNT ||
            (sampler.Dimension != CKFF_TEXTURE_2D && sampler.Dimension != CKFF_TEXTURE_CUBE &&
             sampler.Dimension != CKFF_TEXTURE_3D) ||
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
        const bool hasBorderColorName = !sampler.BorderColorName.IsEmpty();
        const bool hasSamplerStateName = !sampler.SamplerStateName.IsEmpty();
        if (hasBorderColorName != hasSamplerStateName)
            return CKERR_INVALIDPARAMETER;
        if (hasBorderColorName) {
            if (!namedUniforms || sampler.MetadataBufferSlot != UINT32_MAX ||
                !RegisterName(names, sampler.BorderColorName, CKFF_UNIFORM_VEC4,
                              1, 1, sampler.Slot) ||
                !RegisterName(names, sampler.SamplerStateName, CKFF_UNIFORM_VEC4,
                              1, 2, sampler.Slot))
                return CKERR_INVALIDPARAMETER;
        } else if (sampler.MetadataBufferSlot != UINT32_MAX) {
            if (sampler.MetadataBufferSlot >= CKFF_UNIFORM_BUFFER_COUNT)
                return CKERR_INVALIDPARAMETER;
            auto &buffer = buffers[sampler.Stage][sampler.MetadataBufferSlot];
            if (!buffer.Reserve(sampler.BorderColorOffset, 16) ||
                !buffer.Reserve(sampler.SamplerStateOffset, 16) ||
                !ReserveShared(sharedBuffers, buffer, {SharedWrite::BorderColor, sampler.Slot,
                    CKFF_UNIFORM_VEC4, 1, sampler.BorderColorOffset, 16}) ||
                !ReserveShared(sharedBuffers, buffer, {SharedWrite::SamplerState, sampler.Slot,
                    CKFF_UNIFORM_VEC4, 1, sampler.SamplerStateOffset, 16}))
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
    for (int i = 0; i < desc.VertexInputs.Size(); ++i) {
        const CKFFVertexInput &input = desc.VertexInputs[i];
        if (static_cast<CKDWORD>(input.Attribute) >= CKRST_ATTRIB_COUNT || input.Location >= 16 ||
            (input.Integer != FALSE && input.Integer != TRUE) ||
            attributes[input.Attribute] || locations[input.Location])
            return CKERR_INVALIDPARAMETER;
        attributes[input.Attribute] = true;
        locations[input.Location] = true;
    }
    return CK_OK;
}
