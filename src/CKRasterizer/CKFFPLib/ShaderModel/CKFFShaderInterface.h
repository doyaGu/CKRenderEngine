#ifndef CKFFSHADERINTERFACE_H
#define CKFFSHADERINTERFACE_H

#include "CKRasterizerContextData.h"
#include "CKFFProgramDesc.h"
#include "CKFFShaderABI.h"

// Logical fixed-function data belongs to the rasterizer. A backend only sees
// the byte slots and explicit native resource mappings in a program descriptor.
enum CKFFConstantBlock {
    CKRST_BLOCK_MATRICES = 0,
    CKRST_BLOCK_VERTEX_BLEND_MATRICES,
    CKRST_BLOCK_DRAW_PARAMS,
    CKRST_BLOCK_TEX_MATRICES,
    CKRST_BLOCK_LIGHTS,
    CKRST_BLOCK_BUMP_ENV,
    CKRST_BLOCK_VIEWPORT,
    CKRST_BLOCK_STAGE_PARAMS,
    CKRST_BLOCK_BORDER_COLORS,
    CKRST_BLOCK_BORDER_SAMPLERS,
    CKRST_BLOCK_SPEC,
    CKRST_BLOCK_CLIP_PLANES,
    CKRST_BLOCK_CLIP_PARAMS,
    CKRST_BLOCK_PRESENT_PARAMS,
    CKRST_BLOCK_COUNT
};

struct CKFFConstantBlockDesc {
    const char *Name;
    CKBOOL Mat4;
    CKDWORD Count;
};

enum {
    CKFF_SLOT_PRESENT = CKFF_SAMPLER_SLOT_COUNT,
    CKFF_SLOT_COUNT = CKFF_SLOT_PRESENT + 1,
};

const CKFFConstantBlockDesc &CKFFConstantBlockInfo(CKFFConstantBlock block);
const char *CKFFSamplerSlotName(
    CKDWORD slot,
    CKFFSamplerLayout layout = CKFF_SAMPLER_LAYOUT_WIDE_2D);

// Called only when a cached program is first created. The resulting owned
// descriptor is compiled by the backend; draw submission does not rebuild it.
CKFFProgramDesc CKFFBuildProgramInterface(CKDWORD vertexShader, CKDWORD pixelShader,
                                              CK_SHADER_FORMAT format, CKBOOL present = FALSE,
                                              CKBOOL positionT = FALSE,
                                              CKFFSamplerLayout samplerLayout =
                                                  CKFF_SAMPLER_LAYOUT_WIDE_2D);

inline CKERROR CKFFSetConstants(CKFFConstantSet *constants, CKFFConstantBlock block,
                                const void *data, CKDWORD vec4Count)
{
    return constants->Set(static_cast<CKDWORD>(block), data, vec4Count * 16u);
}

#endif
