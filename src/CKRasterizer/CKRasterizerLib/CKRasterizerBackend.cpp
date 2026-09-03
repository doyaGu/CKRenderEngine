#include "CKRasterizerBackend.h"
#include "CKFFShaderABI.h"
#include "CKFFConstants.h"

#include <stdio.h>

namespace {

const CKBackendConstantBlockDesc g_BlockTable[CKRST_BLOCK_COUNT] = {
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

const CKBackendConstantBlockDesc g_InvalidBlock = {NULL, FALSE, 0};

} // namespace

const CKBackendConstantBlockDesc &CKBackendConstantBlockInfo(CKBackendConstantBlock Block)
{
    if ((int)Block < 0 || (int)Block >= CKRST_BLOCK_COUNT)
        return g_InvalidBlock;
    return g_BlockTable[Block];
}

const char *CKBackendSamplerSlotName(CKDWORD Slot)
{
    static const char *const k2D[CKFF_MAX_TEXTURE_STAGES] = {
        "s_texture0", "s_texture1", "s_texture2", "s_texture3",
        "s_texture4", "s_texture5", "s_texture6", "s_texture7"};
    static const char *const kCube[CKFF_CUBE_SAMPLER_COUNT] = {
        "s_textureCube0", "s_textureCube1", "s_textureCube2", "s_textureCube3"};
    static const char *const kVolume[CKFF_VOLUME_SAMPLER_COUNT] = {
        "s_textureVolume0", "s_textureVolume1", "s_textureVolume2", "s_textureVolume3"};
    if (Slot < CKFF_CUBE_SAMPLER_SLOT_BASE)
        return k2D[Slot];
    if (Slot < CKFF_VOLUME_SAMPLER_SLOT_BASE)
        return kCube[Slot - CKFF_CUBE_SAMPLER_SLOT_BASE];
    if (Slot < CKFF_SAMPLER_SLOT_COUNT)
        return kVolume[Slot - CKFF_VOLUME_SAMPLER_SLOT_BASE];
    if (Slot == CKRST_BACKEND_SLOT_PRESENT)
        return "s_sceneColor";
    return NULL;
}
