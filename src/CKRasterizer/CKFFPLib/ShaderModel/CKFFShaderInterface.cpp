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
    {"u_borderColor", FALSE, CKFF_MAX_TEXTURE_STAGES},
    {"u_borderSampler", FALSE, CKFF_SAMPLER_SLOT_COUNT},
    {"u_ffProgram", FALSE, CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT},
    {"u_clipPlanes", FALSE, CKFF_CLIP_PLANE_COUNT},
    {"u_clipParams", FALSE, 1},
    {"u_postParams", FALSE, 1},
};

enum NativeGroup { VERTEX, FRAGMENT, PRESENT };
struct NativeBlock {
    NativeGroup Group;
    CKDWORD BufferSlot;
    CKFFConstantBlock Block;
};
const NativeBlock NativeBlocks[] = {
#define CKFF_NATIVE_BLOCK(Stage, Slot, Block) {Stage, Slot, CKRST_BLOCK_##Block},
#define CKFF_NATIVE_METADATA(Stage, Slot, Count)
#include "CKFFNativeLayout.def"
#undef CKFF_NATIVE_BLOCK
#undef CKFF_NATIVE_METADATA
};
struct NativeMetadata {
    NativeGroup Group;
    CKDWORD BufferSlot;
    CKDWORD Count;
};
const NativeMetadata NativeMetadataCounts[] = {
#define CKFF_NATIVE_BLOCK(Stage, Slot, Block)
#define CKFF_NATIVE_METADATA(Stage, Slot, Count) {Stage, Slot, Count},
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

void AppendUniformBinding(CKFFProgramDesc &program,
                          CKFFConstantBlock blockSlot,
                          CK_SHADER_STAGE stage,
                          CKDWORD bufferSlot,
                          CKDWORD &bufferSize)
{
    const CKFFConstantBlockDesc &block = BlockTable[blockSlot];
    CKFFUniformBinding uniform;
    uniform.Slot = blockSlot;
    uniform.Name = block.Name;
    uniform.Type = block.Mat4 ? CKFF_UNIFORM_MAT4 : CKFF_UNIFORM_VEC4;
    uniform.Count = block.Count;
    uniform.Stage = stage;
    uniform.BufferSlot = bufferSlot;
    uniform.Offset = bufferSize;
    bufferSize += uniform.Size();
    program.Uniforms.PushBack(uniform);
}

// Whether a pixel buffer holds exactly the blocks of a vertex buffer, at the
// same offsets.
bool SameBlocks(const CKFFProgramDesc &program, const CKFFUniformBufferBinding &vertex,
                const CKFFUniformBufferBinding &pixel)
{
    if (vertex.Size != pixel.Size)
        return false;
    int blocks = 0;
    for (int i = 0; i < program.Uniforms.Size(); ++i) {
        const CKFFUniformBinding &uniform = program.Uniforms[i];
        if (uniform.Stage == CKRST_SHADER_PIXEL && uniform.BufferSlot == pixel.Slot)
            --blocks;
        if (uniform.Stage != CKRST_SHADER_VERTEX || uniform.BufferSlot != vertex.Slot)
            continue;
        ++blocks;
        bool matched = false;
        for (int j = 0; j < program.Uniforms.Size() && !matched; ++j) {
            const CKFFUniformBinding &other = program.Uniforms[j];
            matched = other.Stage == CKRST_SHADER_PIXEL && other.BufferSlot == pixel.Slot &&
                      other.Slot == uniform.Slot && other.Type == uniform.Type &&
                      other.Count == uniform.Count && other.Offset == uniform.Offset;
        }
        if (!matched)
            return false;
    }
    // Every vertex block is matched and the pixel buffer holds no other.
    return blocks == 0;
}

// A pixel buffer that is the image of a vertex buffer shares its data: a draw
// snapshots the bytes once for both stages.
void ShareStageBuffers(CKFFProgramDesc &program)
{
    for (int p = 0; p < program.UniformBuffers.Size(); ++p) {
        CKFFUniformBufferBinding &pixel = program.UniformBuffers[p];
        if (pixel.Stage != CKRST_SHADER_PIXEL)
            continue;
        for (int v = 0; v < program.UniformBuffers.Size(); ++v) {
            CKFFUniformBufferBinding &vertex = program.UniformBuffers[v];
            if (vertex.Stage == CKRST_SHADER_VERTEX && SameBlocks(program, vertex, pixel)) {
                vertex.SharedData = pixel.SharedData = (CKDWORD)v;
                break;
            }
        }
    }
}

} // namespace

const CKFFConstantBlockDesc &CKFFConstantBlockInfo(CKFFConstantBlock block)
{
    return static_cast<CKDWORD>(block) < CKRST_BLOCK_COUNT ? BlockTable[block] : InvalidBlock;
}

const char *CKFFSamplerSlotName(CKDWORD slot, CKFFSamplerLayout layout)
{
    static const char *const twoDNames[CKFF_WIDE_SAMPLER_COUNT] = {
        "s_texture0", "s_texture1", "s_texture2", "s_texture3",
        "s_texture4", "s_texture5", "s_texture6", "s_texture7",
    };
    static const char *const cubeNames[CKFF_WIDE_SAMPLER_COUNT] = {
        "s_textureCube0", "s_textureCube1", "s_textureCube2", "s_textureCube3",
        "s_textureCube4", "s_textureCube5", "s_textureCube6", "s_textureCube7",
    };
    static const char *const volumeNames[CKFF_WIDE_SAMPLER_COUNT] = {
        "s_textureVolume0", "s_textureVolume1", "s_textureVolume2", "s_textureVolume3",
        "s_textureVolume4", "s_textureVolume5", "s_textureVolume6", "s_textureVolume7",
    };
    if (slot == CKFF_SLOT_PRESENT)
        return "s_sceneColor";
    if (slot >= CKFF_SAMPLER_SLOT_COUNT)
        return NULL;
    const CKDWORD cubeBase = CKFFSamplerTypeSlotBase(CKFF_SAMPLER_CUBE, layout);
    const CKDWORD volumeBase = CKFFSamplerTypeSlotBase(CKFF_SAMPLER_VOLUME, layout);
    if (slot < cubeBase)
        return twoDNames[slot];
    if (slot < volumeBase)
        return cubeNames[slot - cubeBase];
    return volumeNames[slot - volumeBase];
}

CKFFProgramDesc CKFFBuildProgramInterface(CKDWORD vertexShader, CKDWORD pixelShader,
                                              CK_SHADER_FORMAT format, CKBOOL present,
                                              CKBOOL positionT,
                                              CKFFSamplerLayout samplerLayout)
{
    CKFFProgramDesc result;
    result.VertexShader = vertexShader;
    result.PixelShader = pixelShader;
    const bool packed = format != CKRST_SHADER_FORMAT_BGFX;
    const CKDWORD stageCount = present ? 1u : 2u;
    result.UniformBuffers.Reserve((int)(packed ? stageCount * CKFF_UNIFORM_BUFFER_COUNT : 0u));
    result.Uniforms.Reserve((int)(present ? 1u : CKRST_BLOCK_COUNT * stageCount));

    CKDWORD metadataBufferSlot = UINT32_MAX;
    CKDWORD metadataOffset = 0;
    CKDWORD metadataCount = 0;
    for (CKDWORD stageIndex = 0; stageIndex < stageCount; ++stageIndex) {
        const CK_SHADER_STAGE stage = present ? CKRST_SHADER_PIXEL : static_cast<CK_SHADER_STAGE>(stageIndex);
        if (packed) {
            const NativeGroup group = present ? PRESENT : stage == CKRST_SHADER_VERTEX ? VERTEX : FRAGMENT;
            CKDWORD bufferSizes[CKFF_UNIFORM_BUFFER_COUNT] = {};
            for (const NativeBlock &entry : NativeBlocks) {
                if (entry.Group != group)
                    continue;
                if (group == VERTEX && positionT && entry.BufferSlot == 0)
                    continue;
                if (entry.BufferSlot >= CKFF_UNIFORM_BUFFER_COUNT)
                    return CKFFProgramDesc();
                const CKDWORD bufferSlot = group == VERTEX && positionT
                    ? entry.BufferSlot - 1u : entry.BufferSlot;
                AppendUniformBinding(result, entry.Block, stage, bufferSlot,
                                     bufferSizes[bufferSlot]);
            }
            for (const auto &entry : NativeMetadataCounts) {
                if (entry.Group != group)
                    continue;
                if (entry.BufferSlot >= CKFF_UNIFORM_BUFFER_COUNT)
                    return CKFFProgramDesc();
                metadataBufferSlot = entry.BufferSlot;
                metadataOffset = bufferSizes[entry.BufferSlot];
                metadataCount = entry.Count;
                // One border color and sampler-state vector per native slot.
                bufferSizes[entry.BufferSlot] += entry.Count * 32u;
            }
            CKDWORD bufferCount = 0;
            for (CKDWORD slot = 0; slot < CKFF_UNIFORM_BUFFER_COUNT; ++slot) {
                if (!bufferSizes[slot])
                    continue;
                if (slot != bufferCount)
                    return CKFFProgramDesc();
                CKFFUniformBufferBinding buffer;
                buffer.Stage = stage;
                buffer.Slot = slot;
                buffer.Size = bufferSizes[slot];
                result.UniformBuffers.PushBack(buffer);
                ++bufferCount;
            }
        } else {
            // Named uniforms retain their logical identity and do not acquire
            // a synthetic native buffer layout.
            CKDWORD offset = 0;
            for (CKDWORD slot = 0; slot < CKRST_BLOCK_COUNT; ++slot)
                if (!present || slot == CKRST_BLOCK_PRESENT_PARAMS)
                    AppendUniformBinding(result, static_cast<CKFFConstantBlock>(slot),
                                         stage, 0, offset);
        }
    }

    if (packed && !present)
        ShareStageBuffers(result);

    const CKDWORD samplerCount = present ? 1u : CKFF_SAMPLER_SLOT_COUNT;
    result.Samplers.Reserve((int)samplerCount);
    for (CKDWORD slot = 0; slot < samplerCount; ++slot) {
        CKFFSamplerBinding sampler;
        sampler.Slot = present ? CKFF_SLOT_PRESENT : slot;
        sampler.NativeSlot = slot;
        sampler.Name = CKFFSamplerSlotName(sampler.Slot, samplerLayout);
        const CKDWORD cubeBase = CKFFSamplerTypeSlotBase(
            CKFF_SAMPLER_CUBE, samplerLayout);
        const CKDWORD volumeBase = CKFFSamplerTypeSlotBase(
            CKFF_SAMPLER_VOLUME, samplerLayout);
        sampler.Dimension = present || slot < cubeBase ? CKFF_TEXTURE_2D :
            slot < volumeBase ? CKFF_TEXTURE_CUBE : CKFF_TEXTURE_3D;
        if (packed) {
            sampler.MetadataBufferSlot = metadataBufferSlot;
            sampler.BorderColorOffset = metadataOffset + slot * 16u;
            sampler.SamplerStateOffset = metadataOffset + metadataCount * 16u + slot * 16u;
        }
        result.Samplers.PushBack(sampler);
    }

    result.VertexInputs.Reserve((int)(present ? 2u : 16u));
    for (CKDWORD location = 0; location < 16; ++location) {
        if (present && location != 0 && location != 8)
            continue;
        CKFFVertexInput input;
        input.Attribute = VertexAttributes[location];
        input.Location = location;
        input.Integer = location == 6;
        // The legacy defaults are part of this shader family, not an API rule.
        // Integer indices are zero; other attributes have w=1; diffuse is white.
        if (!input.Integer)
            input.DefaultValue[3] = 0x3f800000u;
        if (location == 4) {
            for (int component = 0; component < 4; ++component)
                input.DefaultValue[component] = 0x3f800000u;
        }
        result.VertexInputs.PushBack(input);
    }
    return result;
}
