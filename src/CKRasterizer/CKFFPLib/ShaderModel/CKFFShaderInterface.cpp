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

enum NativeGroup { VERTEX, FRAGMENT, PRESENT };
struct NativeBlock { NativeGroup Group; CKFFConstantBlock Block; };
const NativeBlock NativeBlocks[] = {
#define CKFF_NATIVE_BLOCK(Stage, Block) {Stage, CKRST_BLOCK_##Block},
#define CKFF_NATIVE_METADATA(Stage, Count)
#include "CKFFNativeLayout.def"
#undef CKFF_NATIVE_BLOCK
#undef CKFF_NATIVE_METADATA
};
struct NativeMetadata { NativeGroup Group; CKDWORD Count; };
const NativeMetadata NativeMetadataCounts[] = {
#define CKFF_NATIVE_BLOCK(Stage, Block)
#define CKFF_NATIVE_METADATA(Stage, Count) {Stage, Count},
#include "CKFFNativeLayout.def"
#undef CKFF_NATIVE_BLOCK
#undef CKFF_NATIVE_METADATA
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

    CKDWORD metadataOffset = 0, metadataCount = 0;
    for (CKDWORD stageIndex = 0; stageIndex < stageCount; ++stageIndex) {
        const CK_SHADER_STAGE stage = present ? CKRST_SHADER_PIXEL : static_cast<CK_SHADER_STAGE>(stageIndex);
        CKDWORD offset = 0;
        auto addUniform = [&](CKFFConstantBlock slot) {
            const auto &block = BlockTable[slot];
            CKBackendUniformBinding uniform;
            uniform.Slot = slot;
            uniform.Name = block.Name;
            uniform.Type = block.Mat4 ? CKBACKEND_UNIFORM_MAT4 : CKBACKEND_UNIFORM_VEC4;
            uniform.Count = block.Count;
            uniform.Stage = stage;
            uniform.Offset = offset;
            offset += uniform.Size();
            result.Uniforms.push_back(uniform);
        };
        if (packed) {
            const NativeGroup group = present ? PRESENT : stage == CKRST_SHADER_VERTEX ? VERTEX : FRAGMENT;
            for (const auto &entry : NativeBlocks)
                if (entry.Group == group) addUniform(entry.Block);
            for (const auto &entry : NativeMetadataCounts) {
                if (entry.Group != group) continue;
                metadataOffset = offset; metadataCount = entry.Count;
                offset += entry.Count * 32u; // one border color and sampler-state vector per native slot
            }
            CKBackendUniformBufferBinding buffer;
            buffer.Stage = stage; buffer.Size = offset;
            result.UniformBuffers.push_back(buffer);
        } else {
            // Named uniforms retain their logical identity and do not acquire
            // a synthetic native buffer layout.
            for (CKDWORD slot = 0; slot < CKRST_BLOCK_COUNT; ++slot)
                if (!present || slot == CKRST_BLOCK_PRESENT_PARAMS)
                    addUniform(static_cast<CKFFConstantBlock>(slot));
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
            sampler.BorderColorOffset = metadataOffset + slot * 16u;
            sampler.SamplerStateOffset = metadataOffset + metadataCount * 16u + slot * 16u;
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
