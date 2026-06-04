#include <stdio.h>

#include "CKDrawStateCache.h"
#include "CKFFShaderKey.h"
#include "CKFFStageState.h"
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

void DeclaredTextureStagesIgnoreStaleHigherStageState()
{
    CKFFStateStore state;
    state.Reset();
    state.TextureHandles[0] = 101;
    state.TextureFlags[0] = CKRST_TEXTURE_VALID;
    state.TextureHandles[1] = 202;
    state.TextureFlags[1] = CKRST_TEXTURE_VALID;
    state.StageStates[1][CKRST_TSS_OP] = CKRST_TOP_ADD;

    CKDrawStateCache drawState;
    drawState.Reset();

    const CKDWORD dpFlags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0;
    const CKDWORD activeCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, state.TextureHandles, state.StageStates);
    Check(activeCount == 1, "declared stage flags must cap stale texture state");

    CKFFPreparedState prepared;
    CKFFStateResolver::BuildPreparedState(state, drawState, &prepared,
                                          dpFlags, activeCount,
                                          CKFF_VF_POSITION | CKFF_VF_TEXCOORD0,
                                          nullptr);
    Check(prepared.ActiveTextureCount == 1, "prepared state must keep only declared stage 0 active");
    Check(prepared.TextureBoundMask == 0x1u, "shader texture mask must not include stale stage 1 texture");
    Check(prepared.StateDesc.FS.GetStageColorOp(1) == CKRST_TOP_DISABLE,
          "stage 1 shader op must be disabled for a stage-0 draw");
}

int main()
{
    TransformedNormalInputKeepsLighting();
    PositionTForcesNoLighting();
    DeclaredTextureStagesIgnoreStaleHigherStageState();

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }

    printf("all passed\n");
    return 0;
}
