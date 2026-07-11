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

void TextureStageChainIsIndependentOfTexcoordDeclarations()
{
    CKFFStateStore state;
    state.Reset();
    state.TextureHandles[0] = 101;
    state.TextureFlags[0] = CKRST_TEXTURE_VALID;
    state.StageStates[1][CKRST_TSS_OP] = CKRST_TOP_SELECTARG1;
    state.StageStates[1][CKRST_TSS_ARG1] = CKRST_TA_CONSTANT;

    CKDrawStateCache drawState;
    drawState.Reset();

    const CKDWORD dpFlags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0;
    const CKDWORD activeCount = (CKDWORD)CKFFResolveActiveTextureStageCount(
        state.TextureHandles, state.StageStates);
    Check(activeCount == 2,
          "an untextured stage must remain active when only texcoord 0 is declared");

    CKFFPreparedState prepared;
    CKFFStateResolver::BuildPreparedState(state, drawState, &prepared,
                                          dpFlags, activeCount,
                                          CKFF_VF_POSITION | CKFF_VF_TEXCOORD0,
                                          nullptr);
    Check(prepared.ActiveTextureCount == 2,
          "prepared state must use the fixed-function stage chain length");
    Check(prepared.TextureBoundMask == 0x1u,
          "the untextured stage must not add a texture binding");
    Check(prepared.StateDesc.FS.GetStageColorOp(1) == CKRST_TOP_SELECTARG1,
          "stage 1 shader op must remain active without texcoord 1");

    state.StageStates[0][CKRST_TSS_OP] = CKRST_TOP_DISABLE;
    Check(CKFFResolveActiveTextureStageCount(
              state.TextureHandles, state.StageStates) == 0,
          "COLOROP disable must terminate the complete stage chain");
}

void PixelFogMarksVertexEyeSpaceDependency()
{
    CKFFStateStore state;
    state.Reset();
    CKDrawStateCache drawState;
    drawState.Reset();
    drawState.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    drawState.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_NONE);
    drawState.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_LINEAR);

    CKFFPreparedState prepared;
    CKFFStateResolver::BuildPreparedState(state, drawState, &prepared,
                                          CKRST_DP_TRANSFORM, 0,
                                          CKFF_VF_POSITION, nullptr);
    const CKFFShaderKey key = BuildKey(prepared);
    Check(prepared.StateDesc.VS.GetPixelFog(),
          "pixel fog must mark the vertex eye-space dependency");
    Check((key.VS.Bits & (1ull << 24)) != 0,
          "pixel fog dependency must survive shader-key construction");
}

void PointSpriteBypassesVertexTexcoordProcessing()
{
    CKFFStateStore state;
    state.Reset();
    state.TextureHandles[0] = 101;
    state.TextureHandles[1] = 102;
    state.StageStates[0][CKRST_TSS_TEXCOORDINDEX] =
        CKFFPackTexcoordIndex(3, CKFF_TEXGEN_CAMERASPACEPOSITION);
    state.StageStates[0][CKRST_TSS_TEXTURETRANSFORMFLAGS] =
        CKRST_TTF_COUNT3 | CKRST_TTF_PROJECTED;
    state.StageStates[1][CKRST_TSS_OP] = CKRST_TOP_SELECTARG1;
    state.StageStates[1][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
    state.StageStates[1][CKRST_TSS_TEXCOORDINDEX] =
        CKFFPackTexcoordIndex(2, CKFF_TEXGEN_CAMERASPACENORMAL);
    state.StageStates[1][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_COUNT2;

    CKDrawStateCache drawState;
    drawState.Reset();

    CKFFPreparedState prepared;
    CKFFStateResolver::BuildPreparedState(
        state, drawState, &prepared,
        CKRST_DP_TRANSFORM | CKRST_DP_STAGES1, 2,
        CKFF_VF_POSITION | CKFF_VF_TEXCOORD0 | CKFF_VF_TEXCOORD1,
        nullptr, TRUE);

    Check(prepared.StateDesc.VS.GetPointSprite(),
          "point sprite mode must survive prepared-state construction");
    for (CKDWORD stage = 0; stage < 2; ++stage) {
        Check(prepared.StateDesc.VS.GetTexCoordIndex(stage) == 0,
              "all point sprite stages must consume generated texcoord zero");
        Check(prepared.StateDesc.VS.GetTexGenMode(stage) == 0,
              "point sprite coordinates must bypass texgen");
        Check(prepared.StateDesc.VS.GetTextureTransformFlags(stage) == 0,
              "point sprite coordinates must bypass texture matrices");
        Check(!prepared.StateDesc.FS.GetStageProjectedSampler(stage),
              "point sprite coordinates must bypass projected division");
    }
}

int main()
{
    TransformedNormalInputKeepsLighting();
    PositionTForcesNoLighting();
    TextureStageChainIsIndependentOfTexcoordDeclarations();
    PixelFogMarksVertexEyeSpaceDependency();
    PointSpriteBypassesVertexTexcoordProcessing();

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }

    printf("all passed\n");
    return 0;
}
