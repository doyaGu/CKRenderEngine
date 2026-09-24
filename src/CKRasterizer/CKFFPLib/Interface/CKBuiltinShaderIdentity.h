#ifndef CKBUILTINSHADERIDENTITY_H
#define CKBUILTINSHADERIDENTITY_H

#include "CKRasterizerContextEnums.h"
#include "CKTypes.h"

// Identifies the fixed-function shader data consumed by CKFFPLib. Concrete
// rasterizer adapters use these values to reject stale generated artifacts.
static const CKDWORD CKFF_SHADER_ABI_VERSION = 8u;
static const CKDWORD CKFF_SHADER_INTERFACE_HASH = 0x61c4f0a9u;
static const CKDWORD CKFF_SHADER_SAMPLER_SLOT_COUNT = 16u;

constexpr CKDWORD CKFFNativeInterfaceHash()
{
    const char *layout =
#define CKFF_NATIVE_BLOCK(Stage, Slot, Block) "block:" #Stage ":" #Slot ":" #Block ";"
#define CKFF_NATIVE_METADATA(Stage, Slot, Count) "metadata:" #Stage ":" #Slot ":" #Count ";"
#include "CKFFNativeLayout.def"
#undef CKFF_NATIVE_BLOCK
#undef CKFF_NATIVE_METADATA
        ;

    CKDWORD hash = CKFF_SHADER_INTERFACE_HASH;
    while (*layout)
        hash = (hash ^ static_cast<CKBYTE>(*layout++)) * 16777619u;
    return hash;
}

static constexpr CKDWORD CKFF_SHADER_NATIVE_INTERFACE_HASH =
    CKFFNativeInterfaceHash();

inline CKDWORD CKFFShaderInterfaceHash(CK_SHADER_FORMAT format)
{
    return format == CKRST_SHADER_FORMAT_BGFX
        ? CKFF_SHADER_INTERFACE_HASH
        : CKFF_SHADER_NATIVE_INTERFACE_HASH;
}

#endif
