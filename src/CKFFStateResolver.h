#ifndef CKFFSTATERESOLVER_H
#define CKFFSTATERESOLVER_H

#include "CKDrawStateCache.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateStore.h"

struct CKFFStateResolver {
    static void BuildPreparedState(const CKFFStateStore &state,
                                   const CKDrawStateCache &drawState,
                                   CKFFPreparedState *out,
                                   CKDWORD dpFlags,
                                   CKDWORD activeTextureCount,
                                   CKDWORD formatFlags,
                                   const CKBYTE *texcoordComponentCounts);

    static CKDWORD BuildDrawParams(const CKFFStateStore &state,
                                   const CKDrawStateCache &drawState,
                                   float (*drawParams)[4],
                                   const CKFFLightData *viewLights,
                                   int packedLightCount,
                                   const CKFFUniformEmissionContext *context);
};

#endif // CKFFSTATERESOLVER_H
