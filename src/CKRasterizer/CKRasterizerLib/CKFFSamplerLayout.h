#ifndef CKFFSAMPLERLAYOUT_H
#define CKFFSAMPLERLAYOUT_H

#include "CKFFShaderKey.h"

#include <stddef.h>
#include <stdio.h>

struct CKFFSamplerLayoutKey {
    CKDWORD Bits;

    CKFFSamplerLayoutKey() : Bits(0) {}

    bool operator==(const CKFFSamplerLayoutKey &other) const { return Bits == other.Bits; }
    bool operator!=(const CKFFSamplerLayoutKey &other) const { return !(*this == other); }
};

inline CKDWORD CKFFNormalizeSamplerLayoutType(const CKFFShaderKeyFSStage &stage) {
    if (!stage.HasTexture)
        return CKFF_SAMPLER_2D;
    if (stage.SamplerType == CKFF_SAMPLER_CUBE || stage.SamplerType == CKFF_SAMPLER_VOLUME)
        return stage.SamplerType;
    return CKFF_SAMPLER_2D;
}

inline CKFFSamplerLayoutKey CKFFBuildSamplerLayoutKey(const CKFFShaderKeyFS &key) {
    CKFFSamplerLayoutKey layout;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage)
        layout.Bits |= (CKFFNormalizeSamplerLayoutType(key.Stages[stage]) & 3u) << (stage * 2);
    return layout;
}

inline CKDWORD CKFFSamplerLayoutStageType(const CKFFSamplerLayoutKey &layout, CKDWORD stage) {
    if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES)
        return CKFF_SAMPLER_2D;
    return (layout.Bits >> (stage * 2)) & 3u;
}

inline bool CKFFSamplerLayoutNeedsCubeSampler(const CKFFSamplerLayoutKey &layout) {
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        if (CKFFSamplerLayoutStageType(layout, stage) == CKFF_SAMPLER_CUBE)
            return true;
    }
    return false;
}

inline bool CKFFSamplerLayoutNeedsVolumeSampler(const CKFFSamplerLayoutKey &layout) {
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        if (CKFFSamplerLayoutStageType(layout, stage) == CKFF_SAMPLER_VOLUME)
            return true;
    }
    return false;
}

inline bool CKFFSamplerLayoutNeedsMixedCubeVolume(const CKFFSamplerLayoutKey &layout) {
    return CKFFSamplerLayoutNeedsCubeSampler(layout) && CKFFSamplerLayoutNeedsVolumeSampler(layout);
}

inline CKDWORD CKFFSamplerLayoutTypeCount(const CKFFSamplerLayoutKey &layout,
                                          CKDWORD samplerType) {
    CKDWORD count = 0;
    for (CKDWORD stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
        if (CKFFSamplerLayoutStageType(layout, stage) == samplerType)
            ++count;
    }
    return count;
}

inline bool CKFFSamplerLayoutSupportsGenericMixed(const CKFFSamplerLayoutKey &layout) {
    const CKDWORD cubeCount = CKFFSamplerLayoutTypeCount(layout, CKFF_SAMPLER_CUBE);
    const CKDWORD volumeCount = CKFFSamplerLayoutTypeCount(layout, CKFF_SAMPLER_VOLUME);
    return cubeCount > 0 && cubeCount <= 4 &&
           volumeCount > 0 && volumeCount <= 4;
}

inline CKFFSamplerLayoutKey CKFFCanonicalSamplerLayoutKey(
    const CKFFSamplerLayoutKey &layout) {
    if (!CKFFSamplerLayoutSupportsGenericMixed(layout))
        return layout;
    CKFFSamplerLayoutKey canonical;
    canonical.Bits = CKFF_SAMPLER_VOLUME | (CKFF_SAMPLER_CUBE << 2);
    return canonical;
}

inline void CKFFFormatSamplerLayoutStageTypes(const CKFFSamplerLayoutKey &layout,
                                              char *buffer,
                                              size_t bufferSize) {
    if (!buffer || bufferSize == 0)
        return;
    snprintf(buffer, bufferSize, "[%u,%u,%u,%u,%u,%u,%u,%u]",
                  CKFFSamplerLayoutStageType(layout, 0),
                  CKFFSamplerLayoutStageType(layout, 1),
                  CKFFSamplerLayoutStageType(layout, 2),
                  CKFFSamplerLayoutStageType(layout, 3),
                  CKFFSamplerLayoutStageType(layout, 4),
                  CKFFSamplerLayoutStageType(layout, 5),
                  CKFFSamplerLayoutStageType(layout, 6),
                  CKFFSamplerLayoutStageType(layout, 7));
}

inline void CKFFFormatSamplerLayoutManifestEntry(const CKFFSamplerLayoutKey &layout,
                                                 const char *backend,
                                                 char *buffer,
                                                 size_t bufferSize) {
    if (!buffer || bufferSize == 0)
        return;

    char stageTypes[32];
    CKFFFormatSamplerLayoutStageTypes(layout, stageTypes, sizeof(stageTypes));
    snprintf(buffer, bufferSize,
                  "{\"backends\":[\"%s\"],\"stageTypes\":%s}",
                  backend ? backend : "unknown",
                  stageTypes);
}

#endif // CKFFSAMPLERLAYOUT_H
