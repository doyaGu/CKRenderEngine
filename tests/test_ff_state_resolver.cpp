#include <stdio.h>

#include "CKDrawStateCache.h"
#include "CKFFShaderKey.h"
#include "CKFFStateResolver.h"
#include "CKFFStateStore.h"
#include "CKVertexLayoutCache.h"

static int g_failures = 0;

#define Check(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failures; } } while (0)

static CKFFShaderKey BuildKey(const CKFFPreparedState &prepared)
{
    return CKFFBuildShaderKey(prepared.StateDesc, prepared.TextureBoundMask);
}

void TransformedNormalInputKeepsLighting()
{
    CKFFStateStore state;
    state.Reset();
    CKDrawStateCache drawState;
    drawState.Reset();
    drawState.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);

    CKFFPreparedState first;
    CKFFStateResolver::BuildPreparedState(state, drawState, &first,
                                          CKRST_DP_TRANSFORM | CKRST_DP_LIGHT,
                                          0, 0, nullptr);
    Check(first.PositionT == FALSE, "transformed DP input must not be PositionT");
    Check(first.LightingEnabled == TRUE, "normal input with lighting enabled must light");

    CKFFPreparedState second;
    CKFFStateResolver::BuildPreparedState(state, drawState, &second,
                                          CKRST_DP_TRANSFORM | CKRST_DP_LIGHT,
                                          0, 0, nullptr);
    Check(BuildKey(first) == BuildKey(second), "identical transformed input must build a stable key");
}

void PositionTForcesNoLighting()
{
    CKFFStateStore state;
    state.Reset();
    CKDrawStateCache drawState;
    drawState.Reset();
    drawState.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);

    CKFFPreparedState first;
    CKFFStateResolver::BuildPreparedState(state, drawState, &first,
                                          0, 0,
                                          CKFF_VF_POSITIONT | CKFF_VF_NORMAL,
                                          nullptr);
    Check(first.PositionT == TRUE, "PositionT format must set PositionT");
    Check(first.LightingEnabled == FALSE, "PositionT must disable lighting");

    CKFFPreparedState second;
    CKFFStateResolver::BuildPreparedState(state, drawState, &second,
                                          0, 0,
                                          CKFF_VF_POSITIONT | CKFF_VF_NORMAL,
                                          nullptr);
    Check(BuildKey(first) == BuildKey(second), "identical PositionT input must build a stable key");
}

int main()
{
    TransformedNormalInputKeepsLighting();
    PositionTForcesNoLighting();

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }

    printf("all passed\n");
    return 0;
}
