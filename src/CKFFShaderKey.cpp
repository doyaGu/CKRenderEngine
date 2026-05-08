#include "CKFFShaderKey.h"

#include "CKRasterizerEnums.h"

#include <cstring>

namespace {

CKDWORD BaseTextureArg(CKDWORD arg) {
    return arg & ~(CKRST_TA_COMPLEMENT | CKRST_TA_ALPHAREPLICATE);
}

std::size_t HashCombine(std::size_t seed, std::size_t value) {
    return seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2));
}

bool StageOpUsesTexture(const CKFFShaderKeyFSStage &stage, bool color) {
    const CKDWORD op = color ? stage.ColorOp : stage.AlphaOp;
    const CKDWORD mask = CKFFShaderKeyArgsMask(op);
    const CKDWORD arg0 = color ? stage.ColorArg0 : stage.AlphaArg0;
    const CKDWORD arg1 = color ? stage.ColorArg1 : stage.AlphaArg1;
    const CKDWORD arg2 = color ? stage.ColorArg2 : stage.AlphaArg2;

    return ((mask & 0b001u) && CKFFShaderKeyArgUsesTexture(arg0)) ||
           ((mask & 0b010u) && CKFFShaderKeyArgUsesTexture(arg1)) ||
           ((mask & 0b100u) && CKFFShaderKeyArgUsesTexture(arg2));
}

} // namespace

CKFFShaderKeyVS::CKFFShaderKeyVS() : Bits(0), TexGen{} {}

CKFFShaderKeyVS::CKFFShaderKeyVS(const CKFFVSStateDesc &desc)
    : Bits(desc.bits), TexGen{} {
    memcpy(TexGen, desc.TexGen, sizeof(TexGen));
}

bool CKFFShaderKeyVS::operator==(const CKFFShaderKeyVS &other) const {
    return Bits == other.Bits && memcmp(TexGen, other.TexGen, sizeof(TexGen)) == 0;
}

CKFFShaderKeyFS::CKFFShaderKeyFS()
    : Stages{}, LastActiveTextureStage(0), GlobalSpecularEnable(false) {}

bool CKFFShaderKeyFS::operator==(const CKFFShaderKeyFS &other) const {
    if (LastActiveTextureStage != other.LastActiveTextureStage ||
        GlobalSpecularEnable != other.GlobalSpecularEnable)
        return false;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &a = Stages[stage];
        const CKFFShaderKeyFSStage &b = other.Stages[stage];
        if (a.ColorOp != b.ColorOp ||
            a.ColorArg0 != b.ColorArg0 ||
            a.ColorArg1 != b.ColorArg1 ||
            a.ColorArg2 != b.ColorArg2 ||
            a.AlphaOp != b.AlphaOp ||
            a.AlphaArg0 != b.AlphaArg0 ||
            a.AlphaArg1 != b.AlphaArg1 ||
            a.AlphaArg2 != b.AlphaArg2 ||
            a.ResultIsTemp != b.ResultIsTemp)
            return false;
    }
    return true;
}

std::size_t CKFFShaderKeyHash::operator()(const CKFFShaderKey &key) const {
    std::size_t seed = (std::size_t)key.VS.Bits;
    for (uint8_t texGen : key.VS.TexGen)
        seed = HashCombine(seed, texGen);
    seed = HashCombine(seed, key.FS.LastActiveTextureStage);
    seed = HashCombine(seed, key.FS.GlobalSpecularEnable ? 1u : 0u);
    for (const CKFFShaderKeyFSStage &stage : key.FS.Stages) {
        seed = HashCombine(seed, stage.ColorOp);
        seed = HashCombine(seed, stage.ColorArg0);
        seed = HashCombine(seed, stage.ColorArg1);
        seed = HashCombine(seed, stage.ColorArg2);
        seed = HashCombine(seed, stage.AlphaOp);
        seed = HashCombine(seed, stage.AlphaArg0);
        seed = HashCombine(seed, stage.AlphaArg1);
        seed = HashCombine(seed, stage.AlphaArg2);
        seed = HashCombine(seed, stage.ResultIsTemp ? 1u : 0u);
    }
    return seed;
}

CKDWORD CKFFShaderKeyArgsMask(CKDWORD op) {
    switch (op) {
    case CKRST_TOP_DISABLE:
        return 0b000u;
    case CKRST_TOP_SELECTARG1:
    case CKRST_TOP_PREMODULATE:
        return 0b010u;
    case CKRST_TOP_SELECTARG2:
        return 0b100u;
    case CKRST_TOP_MULTIPLYADD:
    case CKRST_TOP_LERP:
        return 0b111u;
    default:
        return 0b110u;
    }
}

bool CKFFShaderKeyArgUsesTexture(CKDWORD arg) {
    return BaseTextureArg(arg) == CKRST_TA_TEXTURE;
}

CKFFShaderKeyFS CKFFBuildShaderKeyFS(const CKFFFSStateDesc &desc, CKDWORD textureBoundMask) {
    CKFFShaderKeyFS key;
    key.GlobalSpecularEnable = desc.GetSpecularAdd();

    CKDWORD activeCount = 0;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        CKFFShaderKeyFSStage &dst = key.Stages[stage];
        dst.ColorOp = desc.GetStageColorOp(stage);
        dst.ColorArg0 = desc.GetStageColorArg0(stage);
        dst.ColorArg1 = desc.GetStageColorArg1(stage);
        dst.ColorArg2 = desc.GetStageColorArg2(stage);
        dst.AlphaOp = desc.GetStageAlphaOp(stage);
        dst.AlphaArg0 = desc.GetStageAlphaArg0(stage);
        dst.AlphaArg1 = desc.GetStageAlphaArg1(stage);
        dst.AlphaArg2 = desc.GetStageAlphaArg2(stage);
        dst.ResultIsTemp = desc.GetStageResultIsTemp(stage);

        if (dst.ColorOp == 0 || dst.ColorOp == CKRST_TOP_DISABLE)
            break;

        const bool hasTexture = (textureBoundMask & (1u << stage)) != 0;
        if (!hasTexture && (StageOpUsesTexture(dst, true) || StageOpUsesTexture(dst, false))) {
            dst.ColorOp = CKRST_TOP_DISABLE;
            dst.AlphaOp = CKRST_TOP_DISABLE;
            dst.ResultIsTemp = false;
            break;
        }

        if (stage == 0 &&
            dst.ResultIsTemp &&
            dst.ColorOp != CKRST_TOP_DISABLE &&
            dst.AlphaOp == CKRST_TOP_DISABLE) {
            dst.AlphaOp = CKRST_TOP_SELECTARG1;
            dst.AlphaArg1 = CKRST_TA_DIFFUSE;
        }

        activeCount = stage + 1;
    }

    if (activeCount > 0) {
        key.LastActiveTextureStage = activeCount - 1;
        key.Stages[activeCount - 1].ResultIsTemp = false;
    }

    return key;
}

CKFFShaderKey CKFFBuildShaderKey(const CKFFStateDesc &desc, CKDWORD textureBoundMask) {
    CKFFShaderKey key;
    key.VS = CKFFShaderKeyVS(desc.VS);
    key.FS = CKFFBuildShaderKeyFS(desc.FS, textureBoundMask);
    return key;
}

CKFFSpecializationInfo CKFFBuildSpecializationInfo(const CKFFShaderKeyFS &key) {
    CKFFSpecializationInfo info;
    info.SetOptimized(true);
    info.Set(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE, key.LastActiveTextureStage);
    info.Set(CKFF_SPEC_GLOBAL_SPECULAR_ENABLED, key.GlobalSpecularEnable ? 1u : 0u);

    for (CKDWORD stage = 0; stage < 4; ++stage) {
        const CKFFShaderKeyFSStage &src = key.Stages[stage];
        const CKDWORD base = (CKDWORD)CKFF_SPEC_STAGE0_COLOR_OP + stage * 7;
        info.Set((CKFFSpecConstantId)(base + 0), src.ColorOp);
        info.Set((CKFFSpecConstantId)(base + 1), CKFFSpecializationInfo::RepackArg(src.ColorArg1));
        info.Set((CKFFSpecConstantId)(base + 2), CKFFSpecializationInfo::RepackArg(src.ColorArg2));
        info.Set((CKFFSpecConstantId)(base + 3), src.AlphaOp);
        info.Set((CKFFSpecConstantId)(base + 4), CKFFSpecializationInfo::RepackArg(src.AlphaArg1));
        info.Set((CKFFSpecConstantId)(base + 5), CKFFSpecializationInfo::RepackArg(src.AlphaArg2));
        info.Set((CKFFSpecConstantId)(base + 6), src.ResultIsTemp ? 1u : 0u);
    }

    return info;
}
