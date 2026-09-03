#include "CKFFUniformEmitter.h"

#include "CKDrawStateCache.h"
#include "CKFFShaderABI.h"
#include "CKFFStageState.h"
#include "CKFFStateResolver.h"
#include "CKFFUniformState.h"
#include "CKRasterizerBackend.h"

#include <string.h>

CKBOOL CKFFUniformEmitter::RenderTargetOriginFlip() const
{
    return m_State.RenderTargetActive &&
           (m_ShaderCache.GetTargetFlags() & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) != 0;
}

static CKDWORD CKFFShaderKeyVertexBlendMode(const CKFFShaderKeyVS &vs)
{
    return (CKDWORD)((vs.Bits >> 35) & 3u);
}

static CKBOOL CKFFShaderKeyLightingEnabled(const CKFFShaderKeyVS &vs)
{
    return (vs.Bits & (1ull << 13)) != 0 ? TRUE : FALSE;
}

static CKDWORD CKFFCurrentTextureMatrixUploadCount(
    const CKFFUniformEmissionContext *context,
    const CKDWORD stageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES])
{
    if (!context || context->PositionT || !stageStates)
        return 0;
    CKDWORD activeTextureCount = context->ActiveTextureCount;
    if (activeTextureCount > CKFF_MAX_TEXTURE_STAGES)
        activeTextureCount = CKFF_MAX_TEXTURE_STAGES;
    CKDWORD count = 0;
    for (CKDWORD stage = 0; stage < activeTextureCount; ++stage) {
        const CKDWORD flags = stageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        const CKDWORD componentCount = flags & 0xFFu;
        if (componentCount >= 1 && componentCount <= 4)
            count = stage + 1;
    }
    return count;
}

static bool CKFFProgramUsesBumpEnv(const CKFFShaderKey &shaderKey)
{
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = shaderKey.FS.Stages[stage].ColorOp;
        if (op == CKRST_TOP_BUMPENVMAP || op == CKRST_TOP_BUMPENVMAPLUMINANCE)
            return true;
    }
    return false;
}

static void CKFFInitUniformSink(CKFFUniformSink *sink,
                                CKRasterizerBackend *backend,
                                CKBOOL emitStatic,
                                CKBOOL emitObject)
{
    if (!sink)
        return;
    memset(sink, 0, sizeof(CKFFUniformSink));
    sink->Backend = backend;
    sink->EmitStatic = emitStatic;
    sink->EmitObject = emitObject;
}

static void CKFFInitUniformEmissionContext(CKFFUniformEmissionContext *context,
                                           CKFFUniformSink *sink,
                                           const CKFFProgramContext *programContext,
                                           CKDWORD activeTextureCount)
{
    if (!context)
        return;
    memset(context, 0, sizeof(CKFFUniformEmissionContext));
    context->Uniforms = sink;
    context->ProgramContext = programContext;
    context->ShaderKey = programContext->ShaderKey;
    context->Specialization = programContext->Specialization;
    context->ActiveTextureCount = activeTextureCount;
    context->PositionT = context->ShaderKey.VS.GetHasPositionT() ? TRUE : FALSE;
    context->LightingEnabled = context->PositionT ? FALSE : TRUE;
    context->FogEnabled = context->ShaderKey.FS.FogEnable ? TRUE : FALSE;
    context->VertexFogMode = context->FogEnabled ? context->ShaderKey.FS.VertexFogMode : 0;
    context->PixelFogMode = context->FogEnabled ? context->ShaderKey.FS.PixelFogMode : 0;
}

#if CKRE_ENABLE_FFP_DIAGNOSTICS
CKFFUniformEmitter::CKFFUniformEmitter(CKFFStateStore &state,
                                       const CKDrawStateCache &drawState,
                                       CKFFShaderCache &shaderCache,
                                       CKFFDrawProbes &probes)
#else
CKFFUniformEmitter::CKFFUniformEmitter(CKFFStateStore &state,
                                       const CKDrawStateCache &drawState,
                                       CKFFShaderCache &shaderCache)
#endif
    : m_State(state),
      m_DrawState(drawState),
      m_ShaderCache(shaderCache)
#if CKRE_ENABLE_FFP_DIAGNOSTICS
      , m_Probes(probes)
#endif
{
}

CKBOOL CKFFUniformEmitter::Emit(CKFFUniformSink *sink, CKBackendConstantBlock block,
                                const void *data, CKDWORD count,
                                CKDWORD vec4Count, CKBOOL objectUniform)
{
    (void)count;
    if (!sink || !data || vec4Count == 0)
        return TRUE;
    if (sink->Failed)
        return FALSE;
    if (objectUniform && !sink->EmitObject)
        return TRUE;
    if (!objectUniform && !sink->EmitStatic)
        return TRUE;
    if (sink->Backend && !UploadUniform(sink->Backend, block, data, vec4Count)) {
        sink->Failed = TRUE;
        return FALSE;
    }
    return TRUE;
}

void CKFFUniformEmitter::EmitObjectMatrixUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms || context->PositionT)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    const bool viewSpaceUniforms = true;
    const bool vertexBlend = CKFFShaderKeyVertexBlendMode(context->ShaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL;
    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix viewNormalMatrix;
    VxMatrix viewProj;
    VxMatrix modelViewProj;
    if (viewSpaceUniforms) {
        Vx3DMultiplyMatrix4(modelView, m_State.View, m_State.World);
        Vx3DInverseMatrix(normalMatrix, modelView);
        Vx3DTransposeMatrix(normalMatrix, normalMatrix);
        if (vertexBlend) {
            Vx3DInverseMatrix(viewNormalMatrix, m_State.View);
            Vx3DTransposeMatrix(viewNormalMatrix, viewNormalMatrix);
        }
    }
    Vx3DMultiplyMatrix4(viewProj, m_State.Projection, m_State.View);
    if (!m_State.ViewportRemapIdentity) {
        // Viewport emulation (spec 4.4): viewport-relative clip -> target clip.
        VxMatrix remap;
        Vx3DMatrixIdentity(remap);
        remap[0][0] = m_State.ViewportRemap[0];
        remap[1][1] = m_State.ViewportRemap[1];
        remap[3][0] = m_State.ViewportRemap[2];
        remap[3][1] = m_State.ViewportRemap[3];
        VxMatrix remapped;
        Vx3DMultiplyMatrix4(remapped, remap, viewProj);
        viewProj = remapped;
    }
    if (RenderTargetOriginFlip()) {
        // Render upside down into the target so its memory matches the D3D
        // layout on bottom-left-origin backends (spec 5.9, RTT origin).
        VxMatrix flip;
        Vx3DMatrixIdentity(flip);
        flip[1][1] = -1.0f;
        VxMatrix flipped;
        Vx3DMultiplyMatrix4(flipped, flip, viewProj);
        viewProj = flipped;
    }
    Vx3DMultiplyMatrix4(modelViewProj, viewProj, m_State.World);
    VxMatrix matrices[4];
    matrices[0] = vertexBlend ? viewProj : modelViewProj;
    matrices[1] = m_State.World;
    if (viewSpaceUniforms) {
        matrices[2] = vertexBlend ? m_State.View : modelView;
        matrices[3] = vertexBlend ? viewNormalMatrix : normalMatrix;
    }
    if (vertexBlend) {
        VxMatrix identity;
        identity.Identity();
        VxMatrix palette[CKFF_VERTEX_BLEND_MATRIX_COUNT];
        for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
            if (m_State.VertexBlendMatrixSet[i])
                palette[i] = m_State.VertexBlendMatrices[i];
            else
                palette[i] = (i == 0) ? m_State.World : identity;
        }
        Emit(sink, CKRST_BLOCK_VERTEX_BLEND_MATRICES, palette,
             CKFF_VERTEX_BLEND_MATRIX_COUNT,
             CKFF_VERTEX_BLEND_MATRIX_COUNT * 4, TRUE);
    }
    const CKDWORD matrixCount = viewSpaceUniforms ? 4 : 2;
    Emit(sink, CKRST_BLOCK_MATRICES, matrices, matrixCount, matrixCount * 4, TRUE);
}

void CKFFUniformEmitter::EmitTextureMatrixUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms)
        return;
    CKFFUniformSink *sink = context->Uniforms;
    const CKDWORD texMatrixCount = CKFFCurrentTextureMatrixUploadCount(context, m_State.StageStates);
    if (texMatrixCount > 0)
        Emit(sink, CKRST_BLOCK_TEX_MATRICES, m_State.TexMatrix,
             texMatrixCount, texMatrixCount * 4, FALSE);
}

void CKFFUniformEmitter::EmitStageAndSpecUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    if (CKFFProgramUsesBumpEnv(context->ShaderKey)) {
        float bumpEnv[CKFF_MAX_TEXTURE_STAGES * 2][4] = {};
        CKFFPackBumpEnvUniforms(m_State.StageStates, bumpEnv);
        Emit(sink, CKRST_BLOCK_BUMP_ENV, bumpEnv,
             CKFF_MAX_TEXTURE_STAGES * 2, CKFF_MAX_TEXTURE_STAGES * 2, FALSE);
    }

    if (context->PositionT) {
        float viewport[4];
        memcpy(viewport, m_State.Viewport, sizeof(viewport));
        if (!m_State.ViewportRemapIdentity) {
            // Same remap as the projection: scale, then offset (times w in the shader).
            viewport[0] *= m_State.ViewportRemap[0];
            viewport[2] = viewport[2] * m_State.ViewportRemap[0] + m_State.ViewportRemap[2];
            viewport[1] *= m_State.ViewportRemap[1];
            viewport[3] = viewport[3] * m_State.ViewportRemap[1] + m_State.ViewportRemap[3];
        }
        if (RenderTargetOriginFlip()) {
            // Pre-transformed vertices: mirror the screen-to-clip Y mapping.
            viewport[1] = -viewport[1];
            viewport[3] = -viewport[3];
        }
        Emit(sink, CKRST_BLOCK_VIEWPORT, viewport, 1, 1, FALSE);
    }

    CKFFStageParamsUniform stageParams;
    CKFFPackStageParams(m_State.StageStates, m_State.TextureHandles, m_State.TextureFlags,
                        context->ActiveTextureCount, stageParams,
                        m_State.StageStateSetMasks,
                        context->ShaderKey.FS.SamplerSlotOverflowMask);
    if (context->ShaderKey.VS.GetPointSprite()) {
        // Expanded point sprites carry their own texcoords: bypass texgen,
        // texture matrices and projection, keep only the sampling flags.
        for (CKDWORD stage = 0;
             stage < context->ActiveTextureCount &&
             stage < CKFF_MAX_TEXTURE_STAGES;
             ++stage) {
            float *coord = stageParams.Values[
                CKFFStageParamIndex(stage, CKFF_STAGE_PARAM_COORD)];
            coord[0] = 0.0f;
            const CKDWORD samplingFlags =
                (CKDWORD)coord[1] &
                (CKFF_TTF_MIRRORONCE_MASK |
                 CKFF_TTF_BUMP_UNORM);
            coord[1] = (float)samplingFlags;
        }
    }
    Emit(sink, CKRST_BLOCK_STAGE_PARAMS, stageParams.Values,
         CKFF_STAGE_PARAM_VEC4_COUNT, CKFF_STAGE_PARAM_VEC4_COUNT, FALSE);

    CKFFSpecUniform ffSpec;
    CKFFPackSpecialization(context->Specialization, ffSpec);
    Emit(sink, CKRST_BLOCK_SPEC, ffSpec.Values,
         CKFF_SPEC_UNIFORM_VEC4_COUNT, CKFF_SPEC_UNIFORM_VEC4_COUNT, FALSE);
}

void CKFFUniformEmitter::EmitClipPlaneUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    CKFFClipPlaneUniform clip;
    const CKDWORD clipMask = m_DrawState.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE);
    if (clipMask != 0) {
        CKFFPackClipPlaneUniforms(m_State.UserClipPlanes, clipMask, clip);
        Emit(sink, CKRST_BLOCK_CLIP_PLANES, clip.Planes, 6, 6, FALSE);
    } else {
        memset(&clip, 0, sizeof(clip));
    }
    Emit(sink, CKRST_BLOCK_CLIP_PARAMS, clip.Params, 1, 1, FALSE);
}

void CKFFUniformEmitter::EmitPayloads(CKFFUniformSink *sink,
                                      const CKFFProgramContext *programContext,
                                      CKDWORD activeTextureCount)
{
    if (!sink || !programContext)
        return;
    CKFFUniformEmissionContext context;
    CKFFInitUniformEmissionContext(&context, sink, programContext, activeTextureCount);
    const bool emitStatic = sink->EmitStatic;
    const bool emitObject = sink->EmitObject;

    if (emitObject)
        EmitObjectMatrixUniforms(&context);
    if (!emitStatic)
        return;

    EmitTextureMatrixUniforms(&context);

    // bgfx uniform bindings are draw state: every draw uploads all of its
    // constants before the submit.
    int packed = 0;
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    if (context.LightingEnabled) {
        packed = CKFFPackViewLights(m_State.Lights, m_State.LightEnabled, m_State.ActiveLightCount,
                                    CKFFShaderKeyLightingEnabled(context.ShaderKey.VS),
                                    m_State.View, viewLights);

        if (packed > 1)
            Emit(sink, CKRST_BLOCK_LIGHTS, viewLights, packed * 7, packed * 7, FALSE);
    }

    float drawParams[CKFF_DRAW_PARAM_VEC4_COUNT][4];
    CKDWORD drawParamCount = CKFFStateResolver::BuildDrawParams(m_State, m_DrawState,
                                                                 drawParams, viewLights,
                                                                 packed, &context);
    if (drawParamCount > 0)
        Emit(sink, CKRST_BLOCK_DRAW_PARAMS, drawParams, drawParamCount, drawParamCount, FALSE);

    EmitStageAndSpecUniforms(&context);
    EmitClipPlaneUniforms(&context);
}

CKBOOL CKFFUniformEmitter::UploadUniforms(CKRasterizerBackend *backend,
                                          const CKFFProgramContext *programContext,
                                          CKDWORD activeTextureCount)
{
    if (!backend || !programContext)
        return FALSE;
    if (!UploadObjectUniforms(backend, programContext, activeTextureCount))
        return FALSE;
    return UploadStaticUniforms(backend, programContext, activeTextureCount);
}

CKBOOL CKFFUniformEmitter::UploadObjectUniforms(CKRasterizerBackend *backend,
                                                const CKFFProgramContext *programContext,
                                                CKDWORD activeTextureCount)
{
    if (!backend || !programContext)
        return FALSE;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, backend, FALSE, TRUE);
    EmitPayloads(&sink, programContext, activeTextureCount);
    return sink.Failed ? FALSE : TRUE;
}

CKBOOL CKFFUniformEmitter::UploadStaticUniforms(CKRasterizerBackend *backend,
                                                const CKFFProgramContext *programContext,
                                                CKDWORD activeTextureCount)
{
    if (!backend || !programContext)
        return FALSE;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, backend, TRUE, FALSE);
    EmitPayloads(&sink, programContext, activeTextureCount);
    return sink.Failed ? FALSE : TRUE;
}

CKBOOL CKFFUniformEmitter::UploadUniform(CKRasterizerBackend *backend, CKBackendConstantBlock block,
                                         const void *data, CKDWORD vec4Count)
{
    if (!backend)
        return FALSE;
    if (backend->PushConstants(block, data, vec4Count) != CK_OK)
        return FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_Probes.OnUniform(block, vec4Count);
#endif
    return TRUE;
}
