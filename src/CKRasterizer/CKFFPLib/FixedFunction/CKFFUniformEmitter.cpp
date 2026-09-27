#include "CKFFUniformEmitter.h"

#include "CKDrawStateCache.h"
#include "CKFFShaderABI.h"
#include "CKFFStageState.h"
#include "CKFFStateResolver.h"
#include "CKFFUniformState.h"

#include <string.h>

CKBOOL CKFFUniformEmitter::RenderTargetOriginFlip() const
{
    return m_State.RenderTargetActive &&
           (m_ShaderTargetFlags & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) != 0;
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

static void CKFFInitUniformSink(CKFFUniformSink *sink,
                                CKFFConstantSet *constants,
                                CKBOOL emitStatic,
                                CKBOOL emitObject)
{
    if (!sink)
        return;
    memset(sink, 0, sizeof(CKFFUniformSink));
    sink->Constants = constants;
    sink->EmitStatic = emitStatic;
    sink->EmitObject = emitObject;
}

static void CKFFInitUniformEmissionContext(CKFFUniformEmissionContext *context,
                                           CKFFUniformSink *sink,
                                           const CKFFProgramContext *programContext,
                                           const CKFFTextureBindingSet *textures,
                                           CKDWORD activeTextureCount,
                                           CKBOOL polygonDepthBias,
                                           CKBOOL patternedLines)
{
    if (!context)
        return;
    memset(context, 0, sizeof(CKFFUniformEmissionContext));
    context->Uniforms = sink;
    context->ProgramContext = programContext;
    context->Textures = textures;
    context->ShaderKey = programContext->ShaderKey;
    context->FragmentProgram = programContext->FragmentProgram;
    context->ActiveTextureCount = activeTextureCount;
    context->PositionT = context->ShaderKey.VS.GetHasPositionT() ? TRUE : FALSE;
    context->LightingEnabled = context->PositionT ? FALSE : TRUE;
    context->FogEnabled = context->ShaderKey.FS.FogEnable ? TRUE : FALSE;
    context->VertexFogMode = context->FogEnabled ? context->ShaderKey.FS.VertexFogMode : 0;
    context->PixelFogMode = context->FogEnabled ? context->ShaderKey.FS.PixelFogMode : 0;
    context->PolygonDepthBias = polygonDepthBias;
    context->PatternedLines = patternedLines;
}

CKFFUniformEmitter::CKFFUniformEmitter(CKFFStateStore &state,
                                       const CKDrawStateCache &drawState,
                                       const CKDWORD &shaderTargetFlags,
                                       CKFFDrawProbes &probes)
    : m_State(state),
      m_DrawState(drawState),
      m_ShaderTargetFlags(shaderTargetFlags),
      m_Probes(probes),
      m_StaticUniformCacheValid(FALSE),
      m_LastStaticConstantsIdentity(0),
      m_LastStaticUniformRevision(0),
      m_LastStaticTextureBindingHash(0),
      m_LastStaticActiveTextureCount(0),
      m_LastStaticPolygonDepthBias(FALSE),
      m_LastStaticPatternedLines(FALSE)
{
}

void CKFFUniformEmitter::ResetCache()
{
    m_StaticUniformCacheValid = FALSE;
    m_LastStaticConstantsIdentity = 0;
    m_LastStaticUniformRevision = 0;
    m_LastStaticTextureBindingHash = 0;
    m_LastStaticActiveTextureCount = 0;
    m_LastStaticPolygonDepthBias = FALSE;
    m_LastStaticPatternedLines = FALSE;
    m_LastStaticShaderKey = CKFFShaderKey();
    m_LastStaticFragmentProgram = CKFFFragmentProgram();
}

CKBOOL CKFFUniformEmitter::Emit(CKFFUniformSink *sink, CKFFConstantBlock block,
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
    if (sink->Constants && !UploadUniform(sink->Constants, block, data, vec4Count)) {
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
    m_State.EnsureViewProjection();
    viewProj = m_State.ViewProjection();
    {
        // D3D8 samples at integer pixel centers. Match the half-pixel shift
        // already applied to POSITIONT vertices, then map the viewport into
        // the target. Applying it in clip space preserves perspective w and
        // keeps transformed meshes aligned with screen-space geometry.
        VxMatrix remap;
        Vx3DMatrixIdentity(remap);
        remap[0][0] = m_State.ViewportRemap[0];
        remap[1][1] = m_State.ViewportRemap[1];
        remap[3][0] = m_State.ViewportRemap[2] +
            0.5f * m_State.Viewport[0] * m_State.ViewportRemap[0];
        remap[3][1] = m_State.ViewportRemap[3] +
            0.5f * m_State.Viewport[1] * m_State.ViewportRemap[1];
        VxMatrix remapped;
        Vx3DMultiplyMatrix4(remapped, remap, viewProj);
        viewProj = remapped;
    }
    if (RenderTargetOriginFlip()) {
        // Render upside down into the target so its memory matches the D3D
        // layout on bottom-left-origin rasterizers.
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
        identity.SetIdentity();
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

void CKFFUniformEmitter::EmitStageAndFragmentProgramUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms || !context->Textures)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    // Every texture lookup reads this block for its LOD bias. Upload zeros
    // when a bias is reset so cached uniforms cannot retain the previous draw.
    float bumpEnv[CKFF_MAX_TEXTURE_STAGES * 2][4] = {};
    float borderColors[CKFF_MAX_TEXTURE_STAGES][4] = {};
    CKFFPackBumpEnvUniforms(m_State.StageStates, bumpEnv);
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFTextureBinding &binding = context->Textures->Bindings[stage];
        const CKSamplerDesc &sampler = binding.Sampler;
        if (binding.Texture != 0) {
            const CKDWORD transformFlags =
                m_State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] |
                CKFFResolveMirrorOnceAddressMask(m_State.StageStates[stage]);
            const CKFFSamplerShaderState samplerState =
                CKFFBuildSamplerShaderState(sampler, binding.TextureFlags,
                                            transformFlags,
                                            m_ShaderTargetFlags);
            bumpEnv[stage * 2 + 1][3] = float(samplerState.Bits);
        }
        CKFFPackColorARGB(sampler.BorderColor, borderColors[stage]);
    }
    Emit(sink, CKRST_BLOCK_BUMP_ENV, bumpEnv,
         CKFF_MAX_TEXTURE_STAGES * 2, CKFF_MAX_TEXTURE_STAGES * 2, FALSE);
    Emit(sink, CKRST_BLOCK_BORDER_COLORS, borderColors,
         CKFF_MAX_TEXTURE_STAGES, CKFF_MAX_TEXTURE_STAGES, FALSE);

    // POSITIONT maps absolute pixels through this vector. The 3D shader also
    // consumes its scale for the internal point-fill corner offset, so keep it
    // initialized for every vertex variant.
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
        // Mirror both pre-transformed positions and point-fill pixel offsets.
        viewport[1] = -viewport[1];
        viewport[3] = -viewport[3];
    }
    Emit(sink, CKRST_BLOCK_VIEWPORT, viewport, 1, 1, FALSE);

    CKFFStageParamsUniform stageParams;
    CKFFPackStageParams(m_State.StageStates, m_State.TextureHandles, m_State.TextureFlags,
                        context->ActiveTextureCount, stageParams,
                        m_State.StageStateSetMasks);
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

    CKFFFragmentProgramUniform ffProgram;
    CKFFPackFragmentProgram(context->FragmentProgram, ffProgram);
    Emit(sink, CKRST_BLOCK_FRAGMENT_PROGRAM, ffProgram.Values,
         CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT, CKFF_FRAGMENT_PROGRAM_UNIFORM_VEC4_COUNT, FALSE);
}

void CKFFUniformEmitter::EmitClipPlaneUniforms(const CKFFUniformEmissionContext *context)
{
    if (!context || !context->Uniforms)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    CKFFClipPlaneUniform clip;
    const CKDWORD clipMask = m_DrawState.GetRenderState(VXRENDERSTATE_CLIPPING)
        ? m_DrawState.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE) : 0;
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
                                      const CKFFTextureBindingSet *textures,
                                      CKDWORD activeTextureCount,
                                      CKBOOL polygonDepthBias,
                                      CKBOOL patternedLines)
{
    if (!sink || !programContext)
        return;
    CKFFUniformEmissionContext context;
    CKFFInitUniformEmissionContext(&context, sink, programContext, textures,
                                   activeTextureCount,
                                   polygonDepthBias, patternedLines);
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
        packed = CKFFPackViewLights(m_State.LightConstants, m_State.LightEnabled, m_State.ActiveLightCount,
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

    EmitStageAndFragmentProgramUniforms(&context);
    EmitClipPlaneUniforms(&context);
}

CKBOOL CKFFUniformEmitter::UploadUniforms(CKFFConstantSet *constants,
                                          const CKFFProgramContext *programContext,
                                          const CKFFTextureBindingSet &textures,
                                          uint64_t staticUniformRevision,
                                          CKBOOL polygonDepthBias,
                                          CKBOOL patternedLines)
{
    if (!constants || !programContext)
        return FALSE;
    if (!UploadObjectUniforms(constants, programContext,
                              textures.ActiveStageCount))
        return FALSE;
    if (m_StaticUniformCacheValid &&
        m_LastStaticConstantsIdentity == constants->Identity() &&
        m_LastStaticUniformRevision == staticUniformRevision &&
        m_LastStaticTextureBindingHash == textures.Hash &&
        m_LastStaticActiveTextureCount == textures.ActiveStageCount &&
        m_LastStaticPolygonDepthBias == polygonDepthBias &&
        m_LastStaticPatternedLines == patternedLines &&
        m_LastStaticShaderKey == programContext->ShaderKey &&
        m_LastStaticFragmentProgram == programContext->FragmentProgram) {
        return TRUE;
    }
    if (!UploadStaticUniforms(constants, programContext, textures,
                              polygonDepthBias, patternedLines))
        return FALSE;
    m_StaticUniformCacheValid = TRUE;
    m_LastStaticConstantsIdentity = constants->Identity();
    m_LastStaticUniformRevision = staticUniformRevision;
    m_LastStaticTextureBindingHash = textures.Hash;
    m_LastStaticActiveTextureCount = textures.ActiveStageCount;
    m_LastStaticPolygonDepthBias = polygonDepthBias;
    m_LastStaticPatternedLines = patternedLines;
    m_LastStaticShaderKey = programContext->ShaderKey;
    m_LastStaticFragmentProgram = programContext->FragmentProgram;
    return TRUE;
}

CKBOOL CKFFUniformEmitter::UploadObjectUniforms(CKFFConstantSet *constants,
                                                const CKFFProgramContext *programContext,
                                                CKDWORD activeTextureCount)
{
    if (!constants || !programContext)
        return FALSE;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, constants, FALSE, TRUE);
    EmitPayloads(&sink, programContext, NULL, activeTextureCount,
                 FALSE, FALSE);
    return sink.Failed ? FALSE : TRUE;
}

CKBOOL CKFFUniformEmitter::UploadStaticUniforms(CKFFConstantSet *constants,
                                                const CKFFProgramContext *programContext,
                                                const CKFFTextureBindingSet &textures,
                                                CKBOOL polygonDepthBias,
                                                CKBOOL patternedLines)
{
    if (!constants || !programContext)
        return FALSE;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, constants, TRUE, FALSE);
    EmitPayloads(&sink, programContext, &textures,
                 textures.ActiveStageCount,
                 polygonDepthBias, patternedLines);
    return sink.Failed ? FALSE : TRUE;
}

CKBOOL CKFFUniformEmitter::UploadUniform(CKFFConstantSet *constants, CKFFConstantBlock block,
                                         const void *data, CKDWORD vec4Count)
{
    if (!constants)
        return FALSE;
    if (CKFFSetConstants(constants, block, data, vec4Count) != CK_OK)
        return FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_Probes.OnUniform(block, vec4Count);
#endif
    return TRUE;
}
