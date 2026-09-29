#ifndef CKFFTEXTUREBINDINGS_H
#define CKFFTEXTUREBINDINGS_H

#include "CKFFProgramDesc.h"
#include "CKRasterizerContextTypes.h"

struct CKFFTextureSlot {
    CKDWORD Texture = 0;
    CKDWORD FixedStage = UINT32_MAX;
    CKDWORD ShaderState = 0;
    CKSamplerDesc Sampler = {CKRST_FILTER_LINEAR, CKRST_FILTER_LINEAR, CKRST_FILTER_NONE,
        CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, 0, CKRST_COMPARE_NONE};
};

struct CKFFTextureBindings {
    CKFFTextureSlot &operator[](CKDWORD slot) { return Slots[slot]; }
    const CKFFTextureSlot &operator[](CKDWORD slot) const { return Slots[slot]; }

    CKFFTextureSlot Slots[CKFF_TEXTURE_SLOT_COUNT];
};

#endif // CKFFTEXTUREBINDINGS_H
