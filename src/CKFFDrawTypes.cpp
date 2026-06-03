#include "CKFFDrawTypes.h"
#include "CKDrawStateCache.h"
#include "CKFFStateStore.h"
#include "CKRasterizerEnums.h"

float CKFFComputeDepthKey(const CKFFStateStore &state, const CKDrawStateCache &drawState)
{
    (void)drawState;

    return state.World[3][0] * state.View[0][2] +
           state.World[3][1] * state.View[1][2] +
           state.World[3][2] * state.View[2][2] +
           state.View[3][2];
}

CKDWORD CKFFSubmitDiscardFlags(const CKFFStateStore &state, const CKDrawStateCache &drawState)
{
    (void)state;
    (void)drawState;

    return CKRST_DISCARD_ALL;
}
