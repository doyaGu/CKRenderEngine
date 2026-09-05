#include "CKFFShaderInterface.h"

namespace {

const CKFFConstantBlockDesc BlockTable[CKRST_BLOCK_COUNT] = {
    {"u_ffMatrices", TRUE, CKFF_MATRIX_VEC4_COUNT},
    {"u_vertexBlendMatrices", TRUE, CKFF_VERTEX_BLEND_MATRIX_COUNT},
    {"u_ffDrawParams", FALSE, CKFF_DRAW_PARAM_VEC4_COUNT},
    {"u_texMatrix", TRUE, CKFF_MAX_TEXTURE_STAGES},
    {"u_lights", FALSE, CKFF_MAX_LIGHTS * 7},
    {"u_bumpEnv", FALSE, CKFF_MAX_TEXTURE_STAGES * 2},
    {"u_viewport", FALSE, 1},
    {"u_stageParams", FALSE, CKFF_STAGE_PARAM_VEC4_COUNT},
    {"u_ffSpec", FALSE, CKFF_SPEC_UNIFORM_VEC4_COUNT},
    {"u_clipPlanes", FALSE, CKFF_CLIP_PLANE_COUNT},
    {"u_clipParams", FALSE, 1},
    {"u_postParams", FALSE, 1},
};

const CKFFConstantBlockDesc InvalidBlock = {NULL, FALSE, 0};

const CK_VERTEX_ATTRIB VertexAttributes[16] = {
    CKRST_ATTRIB_POSITION, CKRST_ATTRIB_NORMAL, CKRST_ATTRIB_TANGENT, CKRST_ATTRIB_BITANGENT,
    CKRST_ATTRIB_COLOR0, CKRST_ATTRIB_COLOR1, CKRST_ATTRIB_INDICES, CKRST_ATTRIB_WEIGHT,
    CKRST_ATTRIB_TEXCOORD0, CKRST_ATTRIB_TEXCOORD1, CKRST_ATTRIB_TEXCOORD2, CKRST_ATTRIB_TEXCOORD3,
    CKRST_ATTRIB_TEXCOORD4, CKRST_ATTRIB_TEXCOORD5, CKRST_ATTRIB_TEXCOORD6, CKRST_ATTRIB_TEXCOORD7,
};

} // namespace

const CKFFConstantBlockDesc &CKFFConstantBlockInfo(CKFFConstantBlock block)
{
    return static_cast<CKDWORD>(block) < CKRST_BLOCK_COUNT ? BlockTable[block] : InvalidBlock;
}

const char *CKFFSamplerSlotName(CKDWORD slot)
{
    static const char *const names[CKFF_SLOT_COUNT] = {
        "s_texture0", "s_texture1", "s_texture2", "s_texture3",
        "s_texture4", "s_texture5", "s_texture6", "s_texture7",
        "s_textureCube0", "s_textureCube1", "s_textureCube2", "s_textureCube3",
        "s_textureVolume0", "s_textureVolume1", "s_textureVolume2", "s_textureVolume3",
        "s_sceneColor",
    };
    return slot < CKFF_SLOT_COUNT ? names[slot] : NULL;
}

CKBackendProgramDesc CKFFBuildProgramInterface(CKDWORD vertexShader, CKDWORD pixelShader,
                                              CK_SHADER_FORMAT format, CKBOOL present)
{
    CKBackendProgramDesc result;
    result.VertexShader = vertexShader;
    result.PixelShader = pixelShader;
    const bool packed = format != CKRST_SHADER_FORMAT_BGFX;
    const CKDWORD stageCount = present ? 1u : 2u;
    result.UniformBuffers.reserve(packed ? stageCount : 0u);
    result.Uniforms.reserve(present ? 1u : CKRST_BLOCK_COUNT * stageCount);

    CKDWORD constantBytes = 0;
    for (const auto &block : BlockTable)
        constantBytes += block.Count * (block.Mat4 ? 64u : 16u);
    const CKDWORD borderBytes = CKFF_SAMPLER_SLOT_COUNT * 16u;
    const CKDWORD uniformBytes = constantBytes + borderBytes * 2u;

    for (CKDWORD stageIndex = 0; stageIndex < stageCount; ++stageIndex) {
        const CK_SHADER_STAGE stage = present ? CKRST_SHADER_PIXEL : static_cast<CK_SHADER_STAGE>(stageIndex);
        if (packed) {
            CKBackendUniformBufferBinding buffer;
            buffer.Stage = stage;
            buffer.Size = uniformBytes;
            // FFP vertex and fragment shaders consume the same immutable data.
            // The backend snapshots it once per draw and binds both stages.
            buffer.SharedData = 0;
            result.UniformBuffers.push_back(buffer);
        }
        CKDWORD offset = 0;
        for (CKDWORD slot = 0; slot < CKRST_BLOCK_COUNT; ++slot) {
            const CKFFConstantBlockDesc &block = BlockTable[slot];
            CKBackendUniformBinding uniform;
            uniform.Slot = slot;
            uniform.Name = block.Name;
            uniform.Type = block.Mat4 ? CKBACKEND_UNIFORM_MAT4 : CKBACKEND_UNIFORM_VEC4;
            uniform.Count = block.Count;
            uniform.Stage = stage;
            uniform.Offset = offset;
            offset += uniform.Size();
            if (!present || slot == CKRST_BLOCK_PRESENT_PARAMS)
                result.Uniforms.push_back(uniform);
        }
    }

    const CKDWORD samplerCount = present ? 1u : CKFF_SAMPLER_SLOT_COUNT;
    result.Samplers.reserve(samplerCount);
    for (CKDWORD slot = 0; slot < samplerCount; ++slot) {
        CKBackendSamplerBinding sampler;
        sampler.Slot = present ? CKFF_SLOT_PRESENT : slot;
        sampler.NativeSlot = slot;
        sampler.Name = CKFFSamplerSlotName(sampler.Slot);
        sampler.Dimension = present || slot < CKFF_CUBE_SAMPLER_SLOT_BASE ? CKBACKEND_TEXTURE_2D :
            slot < CKFF_VOLUME_SAMPLER_SLOT_BASE ? CKBACKEND_TEXTURE_CUBE : CKBACKEND_TEXTURE_3D;
        if (packed) {
            sampler.MetadataBufferSlot = 0;
            sampler.BorderColorOffset = constantBytes + slot * 16u;
            sampler.SamplerStateOffset = constantBytes + borderBytes + slot * 16u;
        }
        result.Samplers.push_back(sampler);
    }

    result.VertexInputs.reserve(present ? 2u : 16u);
    for (CKDWORD location = 0; location < 16; ++location) {
        if (present && location != 0 && location != 8)
            continue;
        CKBackendVertexInput input;
        input.Attribute = VertexAttributes[location];
        input.Location = location;
        input.Integer = location == 6;
        // The legacy defaults are part of this shader family, not an API rule.
        // Integer indices are zero; other attributes have w=1; diffuse is white.
        if (!input.Integer)
            input.DefaultValue[3] = 0x3f800000u;
        if (location == 4)
            input.DefaultValue.fill(0x3f800000u);
        result.VertexInputs.push_back(input);
    }
    return result;
}
