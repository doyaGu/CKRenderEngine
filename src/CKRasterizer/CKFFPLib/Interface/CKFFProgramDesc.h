#ifndef CKFFPROGRAMDESC_H
#define CKFFPROGRAMDESC_H

#include "CKFFConstantSet.h"
#include "CKRasterizerContextTypes.h"
#include "XClassArray.h"
#include "XString.h"
#include <stdint.h>

constexpr CKDWORD CKFF_TEXTURE_SLOT_COUNT = 32;
constexpr CKDWORD CKFF_UNIFORM_BUFFER_COUNT = 4;

enum CKFFUniformType { CKFF_UNIFORM_VEC4, CKFF_UNIFORM_MAT4 };
enum CKFFTextureDimension { CKFF_TEXTURE_2D, CKFF_TEXTURE_CUBE, CKFF_TEXTURE_3D };

struct CKFFUniformBufferBinding {
    CK_SHADER_STAGE Stage = CKRST_SHADER_VERTEX;
    CKDWORD Slot = 0;
    CKDWORD Size = 0;
    // Equal non-sentinel ids explicitly share a packed snapshot across stages.
    // This avoids copying identical vertex/fragment data twice per draw.
    CKDWORD SharedData = UINT32_MAX;
};

struct CKFFUniformBinding {
    CKDWORD Slot = 0; // caller's logical byte-data slot
    XString Name; // named-uniform APIs; native buffer APIs use the range below
    CKFFUniformType Type = CKFF_UNIFORM_VEC4;
    CKDWORD Count = 1;
    CK_SHADER_STAGE Stage = CKRST_SHADER_VERTEX;
    CKDWORD BufferSlot = 0;
    CKDWORD Offset = 0;

    CKDWORD Size() const { return Count * (Type == CKFF_UNIFORM_MAT4 ? 64u : 16u); }
};

struct CKFFSamplerBinding {
    CKDWORD Slot = 0; // caller's logical texture slot
    CK_SHADER_STAGE Stage = CKRST_SHADER_PIXEL;
    CKDWORD NativeSlot = 0;
    CKFFTextureDimension Dimension = CKFF_TEXTURE_2D;
    XString Name;
    CKDWORD DefaultColor = UINT32_MAX; // texture used for an explicit zero binding
    // Optional shader-assisted border sampling ABI. These offsets refer to
    // the declared uniform buffer, never to a semantic FFP constant block.
    CKDWORD MetadataBufferSlot = UINT32_MAX;
    CKDWORD BorderColorOffset = 0;
    CKDWORD SamplerStateOffset = 0;
};

struct CKFFVertexInput {
    CK_VERTEX_ATTRIB Attribute = CKRST_ATTRIB_POSITION;
    CKDWORD Location = 0;
    CKBOOL Integer = FALSE;
    CKDWORD DefaultValue[4] = {}; // raw float/uint bits for a missing stream attribute
};

// CPU-owned declarations. Native programs copy this value, so caller arrays
// and strings may be temporary. No shader role is inferred from a count or slot.
struct CKFFProgramDesc {
    CKDWORD VertexShader = 0;
    CKDWORD PixelShader = 0;
    XClassArray<CKFFUniformBufferBinding> UniformBuffers;
    XClassArray<CKFFUniformBinding> Uniforms;
    XClassArray<CKFFSamplerBinding> Samplers;
    XClassArray<CKFFVertexInput> VertexInputs;
};

CKERROR CKFFValidateProgram(const CKFFProgramDesc &desc,
                            const CKShaderDesc &vertex, const CKShaderDesc &pixel);

#endif // CKFFPROGRAMDESC_H
