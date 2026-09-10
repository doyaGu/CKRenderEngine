#ifndef CKBACKENDPROGRAM_H
#define CKBACKENDPROGRAM_H

#include "CKRasterizerBackendTypes.h"
#include <array>
#include <string>
#include <vector>

// Device binding limits, independent of any particular shader family.
constexpr CKDWORD CKBACKEND_MAX_CONSTANT_SLOTS = 32;
constexpr CKDWORD CKBACKEND_MAX_TEXTURE_SLOTS = 32;
constexpr CKDWORD CKBACKEND_MAX_UNIFORM_BUFFERS = 4;
constexpr CKDWORD CKBACKEND_MAX_UNIFORM_BYTES = 16384;

enum CKBackendUniformType { CKBACKEND_UNIFORM_VEC4, CKBACKEND_UNIFORM_MAT4 };
enum CKBackendTextureDimension { CKBACKEND_TEXTURE_2D, CKBACKEND_TEXTURE_CUBE, CKBACKEND_TEXTURE_3D };

struct CKBackendUniformBufferBinding {
    CK_SHADER_STAGE Stage = CKRST_SHADER_VERTEX;
    CKDWORD Slot = 0;
    CKDWORD Size = 0;
    // Equal non-sentinel ids explicitly share a packed snapshot across stages.
    // This avoids copying identical vertex/fragment data twice per draw.
    CKDWORD SharedData = ~0u;
};

struct CKBackendUniformBinding {
    CKDWORD Slot = 0; // caller's logical byte-data slot
    std::string Name; // named-uniform APIs; native buffer APIs use the range below
    CKBackendUniformType Type = CKBACKEND_UNIFORM_VEC4;
    CKDWORD Count = 1;
    CK_SHADER_STAGE Stage = CKRST_SHADER_VERTEX;
    CKDWORD BufferSlot = 0;
    CKDWORD Offset = 0;

    CKDWORD Size() const { return Count * (Type == CKBACKEND_UNIFORM_MAT4 ? 64u : 16u); }
};

struct CKBackendSamplerBinding {
    CKDWORD Slot = 0; // caller's logical texture slot
    CK_SHADER_STAGE Stage = CKRST_SHADER_PIXEL;
    CKDWORD NativeSlot = 0;
    CKBackendTextureDimension Dimension = CKBACKEND_TEXTURE_2D;
    std::string Name;
    CKDWORD DefaultColor = 0xffffffffu; // texture used for an explicit zero binding
    // Optional shader-assisted border sampling ABI. These offsets refer to
    // the declared uniform buffer, never to a semantic FFP constant block.
    CKDWORD MetadataBufferSlot = ~0u;
    CKDWORD BorderColorOffset = 0;
    CKDWORD SamplerStateOffset = 0;
};

struct CKBackendVertexInput {
    CK_VERTEX_ATTRIB Attribute = CKRST_ATTRIB_POSITION;
    CKDWORD Location = 0;
    CKBOOL Integer = FALSE;
    std::array<CKDWORD, 4> DefaultValue = {}; // raw float/uint bits for a missing stream attribute
};

// Owned declarations: programs copy this value, so caller arrays and strings
// can be temporary. No shader role is inferred from a resource count or slot.
struct CKBackendProgramDesc {
    CKDWORD VertexShader = 0;
    CKDWORD PixelShader = 0;
    std::vector<CKBackendUniformBufferBinding> UniformBuffers;
    std::vector<CKBackendUniformBinding> Uniforms;
    std::vector<CKBackendSamplerBinding> Samplers;
    std::vector<CKBackendVertexInput> VertexInputs;
};

CKERROR CKValidateBackendProgram(const CKBackendProgramDesc &desc,
                                const CKShaderDesc &vertex, const CKShaderDesc &pixel);

#endif
