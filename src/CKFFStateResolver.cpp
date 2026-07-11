#include "CKFFStateResolver.h"

#include "CKFFShaderABI.h"
#include "CKFFStageState.h"
#include "CKFFUniformState.h"
#include "CKVertexLayoutCache.h"

#include <string.h>

static CKBOOL CKFFShaderKeyLightingEnabled(const CKFFShaderKeyVS &vs)
{
    return (vs.Bits & (1ull << 13)) != 0 ? TRUE : FALSE;
}

static void CKFFShaderKeyMaterialSources(const CKFFShaderKeyVS &vs, float materialSource[4])
{
    materialSource[0] = (float)((vs.Bits >> 25) & 3u);
    materialSource[1] = (float)((vs.Bits >> 27) & 3u);
    materialSource[2] = (float)((vs.Bits >> 29) & 3u);
    materialSource[3] = (float)((vs.Bits >> 31) & 3u);
}

static CKBYTE CKFFResolverTexcoordComponentCount(CKDWORD count)
{
    if (count < 1 || count > 4)
        return 2;
    return (CKBYTE)count;
}

static CKDWORD CKFFResolverSamplerTypeFromTextureFlags(CKDWORD textureFlags)
{
    if ((textureFlags & CKRST_TEXTURE_CUBEMAP) != 0)
        return CKFF_SAMPLER_CUBE;
    if ((textureFlags & CKRST_TEXTURE_VOLUMEMAP) != 0)
        return CKFF_SAMPLER_VOLUME;
    if ((textureFlags & CKRST_TEXTURE_DEPTHSTENCIL) != 0)
        return CKFF_SAMPLER_DEPTH;
    return CKFF_SAMPLER_2D;
}

static bool CKFFTextureArgUsesTexFactor(CKDWORD arg)
{
    return (arg & ~(0x10u | 0x20u)) == CKRST_TA_TFACTOR;
}

static bool CKFFShaderStageUsesTexFactor(const CKFFShaderKeyFSStage &stage)
{
    if (stage.ColorOp == CKRST_TOP_BLENDFACTORALPHA || stage.AlphaOp == CKRST_TOP_BLENDFACTORALPHA)
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.ColorArg0))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.ColorArg1))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.ColorArg2))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.AlphaArg0))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.AlphaArg1))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.AlphaArg2))
        return true;
    return false;
}

static bool CKFFProgramUsesTexFactor(const CKFFShaderKey &shaderKey)
{
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = shaderKey.FS.Stages[stage];
        if (CKFFShaderStageUsesTexFactor(s))
            return true;
    }
    return false;
}

static bool CKFFProgramUsesMaterialUniform(const CKFFShaderKey &shaderKey,
                                           CKBOOL fullSpecialized)
{
    if (shaderKey.VS.GetHasPositionT())
        return false;

    if (!fullSpecialized)
        return true;

    const uint64_t bits = shaderKey.VS.Bits;
    const CKDWORD diffuseSource = (CKDWORD)((bits >> 25) & 3u);
    if (diffuseSource == CKFF_MS_MATERIAL)
        return true;

    const bool lightingEnabled = (bits & (1ull << 13)) != 0;
    if (!lightingEnabled)
        return false;

    const CKDWORD ambientSource = (CKDWORD)((bits >> 27) & 3u);
    const CKDWORD specularSource = (CKDWORD)((bits >> 29) & 3u);
    const CKDWORD emissiveSource = (CKDWORD)((bits >> 31) & 3u);
    if (ambientSource == CKFF_MS_MATERIAL ||
        specularSource == CKFF_MS_MATERIAL ||
        emissiveSource == CKFF_MS_MATERIAL) {
        return true;
    }

    return true; // Lighting still reads u_ffDrawParams[4].x for specular power.
}

void CKFFStateResolver::BuildPreparedState(const CKFFStateStore &state,
                                           const CKDrawStateCache &drawState,
                                           CKFFPreparedState *out,
                                           CKDWORD dpFlags,
                                           CKDWORD activeTextureCount,
                                           CKDWORD formatFlags,
                                           const CKBYTE *texcoordComponentCounts)
{
    if (!out)
        return;
    CKFFInitPreparedState(out);
    CKFFStateDesc &stateDesc = out->StateDesc;
    out->ActiveTextureCount = activeTextureCount;
    if (out->ActiveTextureCount > CKFF_MAX_TEXTURE_STAGES)
        out->ActiveTextureCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < out->ActiveTextureCount; ++stage) {
        if (state.TextureHandles[stage] != 0)
            out->TextureBoundMask |= (1u << stage);
    }

    const bool hasFormat = formatFlags != 0;
    const bool positionT = hasFormat ? ((formatFlags & CKFF_VF_POSITIONT) != 0) : ((dpFlags & CKRST_DP_TRANSFORM) == 0);
    out->PositionT = positionT ? TRUE : FALSE;

    // Vertex state description
    stateDesc.VS.SetHasPosition(!positionT);
    stateDesc.VS.SetHasPositionT(positionT);
    stateDesc.VS.SetHasNormal(hasFormat ? ((formatFlags & CKFF_VF_NORMAL) != 0) : ((dpFlags & CKRST_DP_LIGHT) != 0));
    stateDesc.VS.SetHasColor0(hasFormat ? ((formatFlags & CKFF_VF_COLOR0) != 0) : ((dpFlags & CKRST_DP_DIFFUSE) != 0));
    stateDesc.VS.SetHasColor1(hasFormat ? ((formatFlags & CKFF_VF_COLOR1) != 0) : ((dpFlags & CKRST_DP_SPECULAR) != 0));
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        stateDesc.VS.SetHasTexCoord(
            stage,
            hasFormat ? ((formatFlags & CKFF_VF_TEXCOORD(stage)) != 0) : (out->ActiveTextureCount > (CKDWORD)stage));
        const CKDWORD packedTexcoord = state.StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        const CKDWORD transformFlags = state.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        stateDesc.VS.SetTexCoordIndex(stage, CKFFTexcoordIndex(packedTexcoord));
        stateDesc.VS.SetTextureTransformFlags(stage, transformFlags);
        const CKDWORD componentCount = texcoordComponentCounts ? texcoordComponentCounts[stage] : 2;
        stateDesc.VS.SetTexcoordComponentCount(stage, CKFFResolverTexcoordComponentCount(componentCount));
        if (!positionT) {
            const CKDWORD texgen = (packedTexcoord >> 16) & 0xFFFFu;
            const bool hasTransform = transformFlags != 0;
            stateDesc.VS.SetTexGen(stage, texgen, hasTransform);
        }
    }

    CKBOOL lighting = drawState.GetRenderState(VXRENDERSTATE_LIGHTING);
    CKBOOL specular = drawState.GetRenderState(VXRENDERSTATE_SPECULARENABLE);
    CKBOOL normalize = drawState.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS);

    stateDesc.VS.SetLightingEnabled(!positionT && lighting && stateDesc.VS.GetHasNormal());
    out->LightingEnabled = stateDesc.VS.GetLightingEnabled() ? TRUE : FALSE;
    stateDesc.VS.SetSpecularEnabled(specular != 0);
    stateDesc.VS.SetNormalizeNormals(normalize != 0);
    stateDesc.VS.SetLocalViewer(stateDesc.VS.GetLightingEnabled() &&
                                drawState.GetRenderState(VXRENDERSTATE_LOCALVIEWER) != 0);
    stateDesc.VS.SetLightCount(stateDesc.VS.GetLightingEnabled() ? state.ActiveLightCount : 0);

    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        drawState.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        drawState.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
        positionT ? 0 : formatFlags);
    stateDesc.VS.SetVertexBlendMode(vertexBlend.Mode);
    stateDesc.VS.SetVertexBlendIndexed(vertexBlend.Indexed != 0);
    stateDesc.VS.SetVertexBlendCount(vertexBlend.Count);

    const CKBOOL colorVertex = drawState.GetRenderState(VXRENDERSTATE_COLORVERTEX);
    const CKDWORD diffuseSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        drawState.GetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD ambientSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        drawState.GetRenderState(VXRENDERSTATE_AMBIENTFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD specularSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        drawState.GetRenderState(VXRENDERSTATE_SPECULARFROMVERTEX),
        dpFlags, CKRST_DP_SPECULAR, CKFF_MS_COLOR1);
    const CKDWORD emissiveSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        drawState.GetRenderState(VXRENDERSTATE_EMISSIVEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);

    stateDesc.VS.SetDiffuseSource(diffuseSource);
    stateDesc.VS.SetAmbientSource(ambientSource);
    stateDesc.VS.SetSpecularSource(specularSource);
    stateDesc.VS.SetEmissiveSource(emissiveSource);
    out->MaterialSource[0] = (float)diffuseSource;
    out->MaterialSource[1] = (float)ambientSource;
    out->MaterialSource[2] = (float)specularSource;
    out->MaterialSource[3] = (float)emissiveSource;

    // Fog
    CKBOOL fogEnable = drawState.GetRenderState(VXRENDERSTATE_FOGENABLE);
    if (fogEnable) {
        CKDWORD vertexFogMode = drawState.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE);
        CKDWORD pixelFogMode = drawState.GetRenderState(VXRENDERSTATE_FOGPIXELMODE);
        if (pixelFogMode != VXFOG_NONE)
            vertexFogMode = VXFOG_NONE;
        stateDesc.VS.SetFogMode(vertexFogMode);
        stateDesc.VS.SetRangeFog(drawState.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
        stateDesc.FS.SetVertexFogMode(vertexFogMode);
        stateDesc.FS.SetPixelFogMode(pixelFogMode);
        stateDesc.FS.SetRangeFog(drawState.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
    }

    // Fragment state description mirrors the active fixed-function texture-stage contract.
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const uint64_t stateSetMask = state.StageStateSetMasks[stage];
        const bool stageActive = (CKDWORD)stage < out->ActiveTextureCount;
        const bool hasTexture = stageActive && state.TextureHandles[stage] != 0;
        const CKDWORD colorOp = CKFFResolveStageColorOp(state.StageStates[stage], stageActive, hasTexture);
        const CKDWORD alphaOp = CKFFResolveStageAlphaOp(state.StageStates[stage], stageActive, hasTexture);
        stateDesc.FS.SetStageColorOp(stage, colorOp);
        stateDesc.FS.SetStageColorArg0(stage, CKFFResolveStageColorArg0(
            state.StageStates[stage], stateSetMask));
        stateDesc.FS.SetStageColorArg1(stage, CKFFResolveStageColorArg1(
            state.StageStates[stage], hasTexture, stateSetMask));
        stateDesc.FS.SetStageColorArg2(stage, CKFFResolveStageColorArg2(
            state.StageStates[stage], stateSetMask));
        stateDesc.FS.SetStageAlphaOp(stage, alphaOp);
        stateDesc.FS.SetStageAlphaArg0(stage, CKFFResolveStageAlphaArg0(
            state.StageStates[stage], stateSetMask));
        stateDesc.FS.SetStageAlphaArg1(stage, CKFFResolveStageAlphaArg1(
            state.StageStates[stage], hasTexture, stateSetMask));
        stateDesc.FS.SetStageAlphaArg2(stage, CKFFResolveStageAlphaArg2(
            state.StageStates[stage], stateSetMask));
        stateDesc.FS.SetStageResultIsTemp(stage, CKFFBaseTextureArg(
            CKFFResolveStageResultArg(state.StageStates[stage], stateSetMask)) == CKRST_TA_TEMP);
        stateDesc.FS.SetStageProjectedSampler(stage, (state.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] & CKRST_TTF_PROJECTED) != 0);
        stateDesc.FS.SetStageSamplerType(stage, CKFFResolverSamplerTypeFromTextureFlags(state.TextureFlags[stage]));
        stateDesc.FS.SetStageSamplerCompareFunc(stage, state.StageStates[stage][CKRST_TSS_COMPAREFUNC]);
        stateDesc.FS.SetStageMirrorOnceMask(stage, CKFFResolveMirrorOnceAddressMask(state.StageStates[stage]) >> 9);

        if (colorOp == CKRST_TOP_DISABLE)
            break;
    }

    stateDesc.FS.SetSpecularAdd(specular != 0);
    stateDesc.FS.SetFogEnabled(fogEnable != 0);
    stateDesc.FS.SetFlatShade(drawState.GetRenderState(VXRENDERSTATE_SHADEMODE) == VXSHADE_FLAT);

    CKBOOL alphaTest = drawState.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE);
    if (alphaTest) {
        stateDesc.FS.SetAlphaTestEnabled(true);
        stateDesc.FS.SetAlphaFunc(drawState.GetRenderState(VXRENDERSTATE_ALPHAFUNC));
    }
    stateDesc.VS.SetVertexClipping(drawState.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE) != 0);
}

CKDWORD CKFFStateResolver::BuildDrawParams(const CKFFStateStore &state,
                                           const CKDrawStateCache &drawState,
                                           float (*drawParams)[4],
                                           const CKFFLightData *viewLights,
                                           int packedLightCount,
                                           const CKFFUniformEmissionContext *context)
{
    if (!context)
        return 0;
    const CKFFShaderKey &shaderKey = context->ShaderKey;
    memset(drawParams, 0, sizeof(float) * CKFF_DRAW_PARAM_VEC4_COUNT * 4);
    memcpy(drawParams[0], state.Material.Diffuse, sizeof(drawParams[0]));
    memcpy(drawParams[1], state.Material.Ambient, sizeof(drawParams[1]));
    memcpy(drawParams[2], state.Material.Specular, sizeof(drawParams[2]));
    memcpy(drawParams[3], state.Material.Emissive, sizeof(drawParams[3]));
    drawParams[CKFF_DRAW_PARAM_MATERIAL_POWER][0] = state.Material.Power;
    float materialSource[4];
    CKFFShaderKeyMaterialSources(shaderKey.VS, materialSource);
    memcpy(drawParams[CKFF_DRAW_PARAM_MATERIAL_SOURCES], materialSource,
           sizeof(drawParams[CKFF_DRAW_PARAM_MATERIAL_SOURCES]));
    drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][2] = drawState.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) ? 1.0f : 0.0f;
    if (context->LightingEnabled) {
        CKDWORD ambientColor = drawState.GetRenderState(VXRENDERSTATE_AMBIENT);
        float ambientColorF[4];
        CKFFPackColorARGB(ambientColor, ambientColorF);
        const CKBOOL lightingEnabled = CKFFShaderKeyLightingEnabled(shaderKey.VS);
        drawParams[CKFF_DRAW_PARAM_LIGHTING][0] = lightingEnabled ? (float)packedLightCount : -1.0f;
        drawParams[CKFF_DRAW_PARAM_LIGHTING][1] = ambientColorF[0];
        drawParams[CKFF_DRAW_PARAM_LIGHTING][2] = ambientColorF[1];
        drawParams[CKFF_DRAW_PARAM_LIGHTING][3] = ambientColorF[2];
        drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][0] = lightingEnabled &&
                            drawState.GetRenderState(VXRENDERSTATE_LOCALVIEWER) ? 1.0f : 0.0f;
        drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][1] = drawState.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS) ? 1.0f : 0.0f;
        if (packedLightCount == 1 && viewLights) {
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 0], viewLights[0].Position, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 1], viewLights[0].Direction, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 2], viewLights[0].Diffuse, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 3], viewLights[0].Specular, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 4], viewLights[0].Ambient, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 5], viewLights[0].Attenuation, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 6], viewLights[0].SpotParams, sizeof(drawParams[0]));
            drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][3] = 1.0f;
        }
    }
    const bool shaderUsesVertexParams = !context->PositionT &&
        (!context->FullSpecialized ||
         context->LightingEnabled ||
         CKFFProgramUsesMaterialUniform(context->ShaderKey, context->FullSpecialized));
    CKDWORD drawParamCount = shaderUsesVertexParams
        ? (context->LightingEnabled ? (packedLightCount == 1 ? 19 : 8) : 6)
        : 0;

    drawParams[CKFF_DRAW_PARAM_ALPHA][0] = (float)CKFFAlphaRefByte(drawState.GetRenderState(VXRENDERSTATE_ALPHAREF));
    drawParams[CKFF_DRAW_PARAM_ALPHA][1] = drawState.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE)
        ? CKFFPackAlphaFuncPrecision(drawState.GetRenderState(VXRENDERSTATE_ALPHAFUNC), state.AlphaTestPrecision)
        : 0.0f;
    drawParams[CKFF_DRAW_PARAM_ALPHA][2] = drawState.GetRenderState(VXRENDERSTATE_SPECULARENABLE) ? 1.0f : 0.0f;
    drawParams[CKFF_DRAW_PARAM_ALPHA][3] = (float)context->PixelFogMode;

    CKDWORD tf = drawState.GetRenderState(VXRENDERSTATE_TEXTUREFACTOR);
    CKFFPackColorARGB(tf, drawParams[CKFF_DRAW_PARAM_TEXTURE_FACTOR]);

    drawParams[CKFF_DRAW_PARAM_FOG][0] = CKFFReadFloatRenderState(drawState, VXRENDERSTATE_FOGSTART, 0.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][1] = CKFFReadFloatRenderState(drawState, VXRENDERSTATE_FOGEND, 1.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][2] = CKFFReadFloatRenderState(drawState, VXRENDERSTATE_FOGDENSITY, 1.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][3] = (float)context->VertexFogMode;

    CKDWORD fogColor = drawState.GetRenderState(VXRENDERSTATE_FOGCOLOR);
    CKFFPackColorARGB(fogColor, drawParams[CKFF_DRAW_PARAM_FOG_COLOR]);

    CKDWORD fragmentParamCount = 0;
    if (!context->FullSpecialized) {
        fragmentParamCount = 4;
    } else if (context->FogEnabled) {
        fragmentParamCount = 4;
    } else if (CKFFProgramUsesTexFactor(context->ShaderKey)) {
        fragmentParamCount = 2;
    } else if (shaderKey.FS.AlphaTestEnable) {
        fragmentParamCount = 1;
    }
    if (fragmentParamCount > 0 && drawParamCount < 8 + fragmentParamCount)
        drawParamCount = 8 + fragmentParamCount;
    if (!context->FullSpecialized && !context->PositionT && context->FogEnabled && drawParamCount < 8)
        drawParamCount = 8;
    return drawParamCount;
}
