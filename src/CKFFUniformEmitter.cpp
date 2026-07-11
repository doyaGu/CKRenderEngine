#include "CKFFUniformEmitter.h"

#include "CKDrawStateCache.h"
#include "CKFFShaderABI.h"
#include "CKFFStageState.h"
#include "CKFFStateResolver.h"
#include "CKFFUniformState.h"
#include "CKRasterizer.h"

#include <string.h>

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

static bool CKFFTextureArgUsesStageConstant(CKDWORD arg)
{
    return CKFFBaseTextureArg(arg) == CKRST_TA_CONSTANT;
}

static bool CKFFShaderStageUsesStageConstant(const CKFFShaderKeyFSStage &stage)
{
    if (CKFFTextureArgUsesStageConstant(stage.ColorArg0))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.ColorArg1))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.ColorArg2))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.AlphaArg0))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.AlphaArg1))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.AlphaArg2))
        return true;
    return false;
}

static bool CKFFProgramUsesStageConstant(const CKFFShaderKey &shaderKey)
{
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = shaderKey.FS.Stages[stage];
        if (CKFFShaderStageUsesStageConstant(s))
            return true;
    }
    return false;
}

static bool CKFFProgramUsesRenderTargetFlip(const CKFFStateStore &state,
                                            CKDWORD shaderTargetFlags,
                                            CKDWORD activeTextureCount)
{
    if ((shaderTargetFlags & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) == 0)
        return false;
    if (activeTextureCount > CKFF_MAX_TEXTURE_STAGES)
        activeTextureCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < activeTextureCount; ++stage) {
        if (state.TextureHandles[stage] != 0 &&
            (state.TextureFlags[stage] & CKRST_TEXTURE_RENDERTARGET) != 0 &&
            (state.TextureFlags[stage] & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) == 0) {
            return true;
        }
    }
    return false;
}

static bool CKFFProgramUsesViewSpaceUniforms(const CKFFShaderKey &shaderKey,
                                             CKBOOL fullSpecialized)
{
    if (shaderKey.VS.GetHasPositionT())
        return false;

    if (!fullSpecialized)
        return true;

    const uint64_t bits = shaderKey.VS.Bits;
    if (CKFFShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return true;
    if ((bits & (1ull << 13)) != 0)
        return true;
    if ((bits & (1ull << 24)) != 0)
        return true;

    if (shaderKey.FS.VertexFogMode != 0)
        return true;

    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if ((shaderKey.VS.TexGen[stage] & 7u) != 0)
            return true;
    }

    return false;
}

static void CKFFInitUniformSink(CKFFUniformSink *sink,
                                CKRasterizerEncoder *encoder,
                                CKFFRenderPacketUniformPayload *staticPayload,
                                CKFFRenderPacketUniformPayload *objectPayload,
                                CKBOOL emitStatic,
                                CKBOOL emitObject)
{
    if (!sink)
        return;
    memset(sink, 0, sizeof(CKFFUniformSink));
    sink->Encoder = encoder;
    sink->StaticPayload = staticPayload;
    sink->ObjectPayload = objectPayload;
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
    context->FullSpecialized = programContext->FullSpecialized;
    context->PositionT = context->ShaderKey.VS.GetHasPositionT() ? TRUE : FALSE;
    context->LightingEnabled = (!context->PositionT &&
        (!context->FullSpecialized || ((context->ShaderKey.VS.Bits & (1ull << 13)) != 0)))
        ? TRUE : FALSE;
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

CKBOOL CKFFUniformEmitter::Emit(CKFFUniformSink *sink, CKDWORD uniform,
                                const void *data, CKDWORD count,
                                CKDWORD vec4Count, CKBOOL objectUniform)
{
    if (!sink || !data || count == 0)
        return TRUE;
    if (sink->Failed)
        return FALSE;
    if (objectUniform && !sink->EmitObject)
        return TRUE;
    if (!objectUniform && !sink->EmitStatic)
        return TRUE;
    if (sink->Encoder) {
        if (sink->Encoder->GetStatus() != CK_OK) {
            sink->Failed = TRUE;
            return FALSE;
        }
        UploadUniform(sink->Encoder, uniform, data, count);
        if (sink->Encoder->GetStatus() != CK_OK) {
            sink->Failed = TRUE;
            return FALSE;
        }
    }
    CKFFRenderPacketUniformPayload *payload = objectUniform ? sink->ObjectPayload : sink->StaticPayload;
    if (payload && !CKFFRenderPacketAddUniform(payload, uniform, data, count, vec4Count)) {
        sink->Failed = TRUE;
        return FALSE;
    }
    return TRUE;
}

void CKFFUniformEmitter::EmitObjectMatrixUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms || context->PositionT)
        return;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    CKFFUniformSink *sink = context->Uniforms;
    const bool viewSpaceUniforms = CKFFProgramUsesViewSpaceUniforms(context->ShaderKey,
                                                                    context->FullSpecialized);
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
        Emit(sink, u.u_vertexBlendMatrices, palette,
             CKFF_VERTEX_BLEND_MATRIX_COUNT,
             CKFF_VERTEX_BLEND_MATRIX_COUNT * 4, TRUE);
    }
    const CKDWORD matrixCount = viewSpaceUniforms ? 4 : 2;
    Emit(sink, u.u_ffMatrices, matrices, matrixCount, matrixCount * 4, TRUE);
}

void CKFFUniformEmitter::EmitTextureMatrixUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms)
        return;
    CKFFUniformSink *sink = context->Uniforms;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    const CKDWORD texMatrixCount = CKFFCurrentTextureMatrixUploadCount(context, m_State.StageStates);
    if (texMatrixCount > 0)
        Emit(sink, u.u_texMatrix, m_State.TexMatrix,
             texMatrixCount, texMatrixCount * 4, FALSE);
}

void CKFFUniformEmitter::EmitStageAndSpecUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    if (CKFFProgramUsesBumpEnv(context->ShaderKey)) {
        float bumpEnv[CKFF_MAX_TEXTURE_STAGES * 2][4] = {};
        CKFFPackBumpEnvUniforms(m_State.StageStates, bumpEnv);
        Emit(sink, u.u_bumpEnv, bumpEnv,
             CKFF_MAX_TEXTURE_STAGES * 2, CKFF_MAX_TEXTURE_STAGES * 2, FALSE);
    }

    if (context->PositionT)
        Emit(sink, u.u_viewport, m_State.Viewport, 1, 1, FALSE);

    const CKDWORD targetFlags = m_ShaderCache.GetTargetFlags();
    if (!context->FullSpecialized || CKFFProgramUsesStageConstant(context->ShaderKey) ||
        CKFFProgramUsesRenderTargetFlip(m_State, targetFlags, context->ActiveTextureCount)) {
        CKFFStageParamsUniform stageParams;
        CKFFPackStageParams(m_State.StageStates, m_State.TextureHandles, m_State.TextureFlags,
                            context->ActiveTextureCount, targetFlags, stageParams,
                            m_State.StageStateSetMasks);
        if ((context->ShaderKey.VS.Bits & (1ull << 40)) != 0) {
            for (CKDWORD stage = 0;
                 stage < context->ActiveTextureCount &&
                 stage < CKFF_MAX_TEXTURE_STAGES;
                 ++stage) {
                float *colorExtra = stageParams.Values[
                    CKFFStageParamIndex(stage, CKFF_STAGE_PARAM_COLOR_EXTRA)];
                colorExtra[1] = 0.0f;
                const CKDWORD samplingFlags =
                    (CKDWORD)colorExtra[2] &
                    (CKFF_TTF_MIRRORONCE_MASK |
                     CKFF_TTF_RENDER_TARGET_FLIP_V);
                colorExtra[2] = (float)samplingFlags;
            }
        }
        Emit(sink, u.u_stageParams, stageParams.Values,
             CKFF_STAGE_PARAM_VEC4_COUNT, CKFF_STAGE_PARAM_VEC4_COUNT, FALSE);
    }

    if (!context->FullSpecialized) {
        CKFFSpecUniform ffSpec;
        CKFFPackSpecializationDwords(context->Specialization, ffSpec);
        Emit(sink, u.u_ffSpec, ffSpec.Values,
             CKFFSpecializationInfo::MaxSpecDwords,
             CKFFSpecializationInfo::MaxSpecDwords, FALSE);
    }
}

void CKFFUniformEmitter::EmitClipPlaneUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    CKFFClipPlaneUniform clip;
    const CKDWORD clipMask = m_DrawState.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE);
    if (!context->FullSpecialized || ((context->ShaderKey.VS.Bits & (1ull << 34)) != 0)) {
        if (clipMask != 0) {
            CKFFPackClipPlaneUniforms(m_State.UserClipPlanes, clipMask, clip);
            Emit(sink, u.u_clipPlanes, clip.Planes, 6, 6, FALSE);
        } else {
            memset(&clip, 0, sizeof(clip));
        }
        Emit(sink, u.u_clipParams, clip.Params, 1, 1, FALSE);
    }
}

void CKFFUniformEmitter::EmitPayloads(CKFFUniformSink *sink,
                                      const CKFFProgramContext *programContext,
                                      CKDWORD activeTextureCount)
{
    if (!sink || !programContext)
        return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    CKFFUniformEmissionContext context;
    CKFFInitUniformEmissionContext(&context, sink, programContext, activeTextureCount);
    const bool emitStatic = sink->EmitStatic;
    const bool emitObject = sink->EmitObject;

    if (emitObject)
        EmitObjectMatrixUniforms(&context);
    if (!emitStatic)
        return;

    EmitTextureMatrixUniforms(&context);

    // bgfx uniform bindings are draw state. Packet replay can retain static
    // draw constants across sorted opaque packets, but immediate draws still
    // upload all constants before each submit.
    int packed = 0;
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    if (context.LightingEnabled) {
        packed = CKFFPackViewLights(m_State.Lights, m_State.LightEnabled, m_State.ActiveLightCount,
                                    CKFFShaderKeyLightingEnabled(context.ShaderKey.VS),
                                    m_State.View, viewLights);

        if (packed > 1)
            Emit(sink, u.u_lights, viewLights, packed * 7, packed * 7, FALSE);
    }

    float drawParams[CKFF_DRAW_PARAM_VEC4_COUNT][4];
    CKDWORD drawParamCount = CKFFStateResolver::BuildDrawParams(m_State, m_DrawState,
                                                                 drawParams, viewLights,
                                                                 packed, &context);
    if (drawParamCount > 0)
        Emit(sink, u.u_ffDrawParams, drawParams, drawParamCount, drawParamCount, FALSE);

    EmitStageAndSpecUniforms(&context);
    EmitClipPlaneUniforms(&context);
}

void CKFFUniformEmitter::UploadUniforms(CKRasterizerEncoder *encoder,
                                        const CKFFProgramContext *programContext,
                                        CKDWORD activeTextureCount)
{
    if (!encoder || !programContext)
        return;
    UploadObjectUniforms(encoder, programContext, activeTextureCount);
    if (encoder->GetStatus() == CK_OK)
        UploadStaticUniforms(encoder, programContext, activeTextureCount);
}

void CKFFUniformEmitter::UploadObjectUniforms(CKRasterizerEncoder *encoder,
                                              const CKFFProgramContext *programContext,
                                              CKDWORD activeTextureCount)
{
    if (!encoder || !programContext)
        return;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, encoder, nullptr, nullptr, FALSE, TRUE);
    EmitPayloads(&sink, programContext, activeTextureCount);
}

void CKFFUniformEmitter::UploadStaticUniforms(CKRasterizerEncoder *encoder,
                                              const CKFFProgramContext *programContext,
                                              CKDWORD activeTextureCount)
{
    if (!encoder || !programContext)
        return;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, encoder, nullptr, nullptr, TRUE, FALSE);
    EmitPayloads(&sink, programContext, activeTextureCount);
}

CKBOOL CKFFUniformEmitter::BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                                     const CKFFProgramContext *programContext,
                                                     CKDWORD activeTextureCount)
{
    if (!payload)
        return FALSE;
    memset(payload, 0, sizeof(CKFFRenderPacketUniformPayload));

    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, nullptr, payload, nullptr, TRUE, FALSE);
    EmitPayloads(&sink, programContext, activeTextureCount);
    if (sink.Failed)
        return FALSE;

    payload->Hash = CKFFHashRenderPacketUniformPayload(*payload);
    return TRUE;
}

CKBOOL CKFFUniformEmitter::BuildObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                               const CKFFProgramContext *programContext)
{
    if (!uniforms)
        return FALSE;
    memset(uniforms, 0, sizeof(CKRenderPacketObjectUniforms));

    if (!programContext)
        return FALSE;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return TRUE;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    const bool viewSpaceUniforms = CKFFProgramUsesViewSpaceUniforms(shaderKey,
                                                                    programContext->FullSpecialized);

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix modelViewProj;
    if (viewSpaceUniforms) {
        Vx3DMultiplyMatrix4(modelView, m_State.View, m_State.World);
        Vx3DInverseMatrix(normalMatrix, modelView);
        Vx3DTransposeMatrix(normalMatrix, normalMatrix);
    }
    Vx3DMultiplyMatrix4(modelViewProj, m_State.ViewProjection(), m_State.World);

    uniforms->MatrixUniform = u.u_ffMatrices;
    uniforms->MatrixCount = viewSpaceUniforms ? 4 : 2;
    uniforms->Matrices[0] = modelViewProj;
    uniforms->Matrices[1] = m_State.World;
    if (viewSpaceUniforms) {
        uniforms->Matrices[2] = modelView;
        uniforms->Matrices[3] = normalMatrix;
    }
    return TRUE;
}

void CKFFUniformEmitter::UploadUniform(CKRasterizerEncoder *encoder, CKDWORD uniform,
                                       const void *data, CKDWORD count)
{
    if (!encoder)
        return;
    encoder->SetUniform(uniform, data, count);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (encoder->GetStatus() == CK_OK)
        m_Probes.OnUniform(m_ShaderCache.GetUniforms(), uniform, count);
#endif
}
