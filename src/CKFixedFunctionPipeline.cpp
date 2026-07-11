#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKFFStateResolver.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfStats.h"
#include "CKRenderFrameCostStats.h"

#include <math.h>
#include <string.h>


CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr),
      m_DisableTextureFiltering(FALSE), m_DisableMipmaps(FALSE),
      m_ForceAnisotropicFiltering(FALSE),
#if CKRE_ENABLE_FFP_DIAGNOSTICS
      m_TextureBinder(m_State, m_ShaderCache, m_Probes),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache, m_Probes),
#else
      m_TextureBinder(m_State, m_ShaderCache),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache),
#endif
      m_OpaquePackets(), m_LastDrawRejectReason(CKFF_DRAW_REJECT_NONE),
      m_BorderPaletteCount(0), m_BorderPaletteFrameSerial((CKDWORD)-1) {
    memset(m_DrawRejectCounts, 0, sizeof(m_DrawRejectCounts));
    memset(m_BorderPaletteColors, 0, sizeof(m_BorderPaletteColors));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_Probes.Config.StatsEnabled = settings.Enabled;
    m_Probes.Config.UniformHistEnabled = settings.UniformHistogram;
    m_Probes.Config.StatsInterval = settings.Interval;
#endif
    const bool batchOpaque = CKRenderFFPSettings().GetBool("BatchOpaqueObjects", false) ||
                             CKRenderFFPSettings().GetBool("SortOpaqueObjects", false);
    m_OpaquePackets.SetSortingEnabled(batchOpaque ? TRUE : FALSE);
    m_OpaquePackets.SetInstancingEnabled(CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true) ? TRUE : FALSE);

    m_State.Reset();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    memset(&m_Probes.Stats, 0, sizeof(m_Probes.Stats));
#endif
}

#if !CKRE_ENABLE_FFP_DIAGNOSTICS
const CKFFFrameStats &CKFixedFunctionPipeline::GetFrameStats() const {
    static const CKFFFrameStats s_EmptyStats = {};
    return s_EmptyStats;
}
#endif

CKFixedFunctionPipeline::~CKFixedFunctionPipeline() {
    Shutdown();
}

bool CKFixedFunctionPipeline::Init(CKRasterizerContext *ctx) {
    if (Shutdown() != CK_OK)
        return false;
    m_Context = ctx;
    m_LastDrawRejectReason = CKFF_DRAW_REJECT_NONE;
    memset(m_DrawRejectCounts, 0, sizeof(m_DrawRejectCounts));
    m_BorderPaletteCount = 0;
    m_BorderPaletteFrameSerial = (CKDWORD)-1;
    memset(m_BorderPaletteColors, 0, sizeof(m_BorderPaletteColors));
    if (!ctx)
        return false;
    CKBOOL shaderBackend = TRUE;
    CKRasterizerCapsDesc caps;
    if (ctx->GetCaps(&caps) == CK_OK) {
        shaderBackend =
            (caps.Features & (CKRST_CAPS_VERTEX_SHADER | CKRST_CAPS_PIXEL_SHADER)) ==
                (CKRST_CAPS_VERTEX_SHADER | CKRST_CAPS_PIXEL_SHADER)
            ? TRUE : FALSE;
    }
    if (shaderBackend && !m_ShaderCache.Init(ctx)) {
        Shutdown();
        return false;
    }
    m_DrawStateCache.Reset();
    m_VertexLayoutCache.Init(ctx);
    m_OpaquePackets.SetInstanceLayout(m_VertexLayoutCache.GetLayout(CKFF_VF_TEXCOORD0 |
                                                                    CKFF_VF_TEXCOORD1 |
                                                                    CKFF_VF_TEXCOORD2 |
                                                                    CKFF_VF_TEXCOORD3));
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_State.MarkViewProjectionDirty();
    MarkPacketProgramDirty();
    m_OpaquePackets.ClearRenderPackets();
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
    return true;
}

CKERROR CKFixedFunctionPipeline::Shutdown() {
    const CKERROR status = m_RenderPipeline.Shutdown();
    if (status != CK_OK)
        return status;
    m_OpaquePackets.ClearRenderPackets();
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_OpaquePackets.SetInstanceLayout(0);
    m_ShaderCache.Shutdown();
    m_Context = nullptr;
    return CK_OK;
}

CKERROR CKFixedFunctionPipeline::PrepareShutdown() {
    return m_RenderPipeline.PrepareShutdown();
}

void CKFixedFunctionPipeline::SetOpaqueSortingEnabled(CKBOOL enabled)
{
    m_OpaquePackets.SetSortingEnabled(enabled);
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
}

static const char *CKFFDrawRejectReasonName(CKFFDrawRejectReason reason)
{
    switch (reason) {
    case CKFF_DRAW_REJECT_INVALID_INPUT: return "invalid-input";
    case CKFF_DRAW_REJECT_PREPARE_FAILED: return "prepare-failed";
    case CKFF_DRAW_REJECT_PROGRAM_MISSING: return "program-missing";
    case CKFF_DRAW_REJECT_STENCIL_WRITE_MASK: return "partial-stencil-write-mask";
    case CKFF_DRAW_REJECT_VERTEX_TWEEN: return "vertex-tween";
    case CKFF_DRAW_REJECT_VERTEX_BLEND_INPUT: return "vertex-blend-input";
    case CKFF_DRAW_REJECT_AFFINE_TEXCOORD: return "affine-texture-coordinates";
    case CKFF_DRAW_REJECT_TEXTURE_OP: return "texture-operation";
    case CKFF_DRAW_REJECT_RENDER_TARGET_TYPE: return "render-target-type";
    case CKFF_DRAW_REJECT_BORDER_PALETTE: return "border-palette";
    default: return "none";
    }
}

CKBOOL CKFixedFunctionPipeline::RecordDrawReject(CKFFDrawRejectReason reason)
{
    m_LastDrawRejectReason = reason;
    if (reason > CKFF_DRAW_REJECT_NONE && reason < CKFF_DRAW_REJECT_COUNT) {
        CKDWORD &count = m_DrawRejectCounts[reason];
        ++count;
        if (count == 1) {
            CK_LOG_FMT("FFPReject", "draw rejected: reason=%s code=%u",
                       CKFFDrawRejectReasonName(reason), (unsigned)reason);
        }
    }
    return FALSE;
}

CKBOOL CKFixedFunctionPipeline::ValidateDrawState(CKDWORD formatFlags,
                                                   CKDWORD activeTextureCount)
{
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILENABLE)) {
        const CKDWORD writeMask =
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK) & 0xffu;
        const CKBOOL stencilWrites =
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILFAIL) != VXSTENCILOP_KEEP ||
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILZFAIL) != VXSTENCILOP_KEEP ||
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILPASS) != VXSTENCILOP_KEEP;
        if (stencilWrites && writeMask != 0x00u && writeMask != 0xffu)
            return RecordDrawReject(CKFF_DRAW_REJECT_STENCIL_WRITE_MASK);
    }

    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
        formatFlags);
    if (!vertexBlend.Supported) {
        if (vertexBlend.UnsupportedReason == CKFF_VERTEX_BLEND_UNSUPPORTED_TWEENING)
            return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_TWEEN);
        return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_INPUT);
    }

    if (activeTextureCount > CKFF_MAX_TEXTURE_STAGES)
        activeTextureCount = CKFF_MAX_TEXTURE_STAGES;
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE)) {
        for (CKDWORD stage = 0; stage < activeTextureCount; ++stage) {
            if (m_State.TextureHandles[stage] != 0)
                return RecordDrawReject(CKFF_DRAW_REJECT_AFFINE_TEXCOORD);
        }
    }
    const CKBOOL originBottomLeft =
        (m_ShaderCache.GetTargetFlags() & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) != 0;
    for (CKDWORD stage = 0; stage < activeTextureCount; ++stage) {
        const CKBOOL hasTexture = m_State.TextureHandles[stage] != 0;
        const CKDWORD colorOp = CKFFResolveStageColorOp(
            m_State.StageStates[stage], TRUE, hasTexture);
        const CKDWORD alphaOp = CKFFResolveStageAlphaOp(
            m_State.StageStates[stage], TRUE, hasTexture);
        if (CKFFClassifyTextureOpCoverage(colorOp) !=
                CKFF_COVERAGE_EXACT ||
            CKFFClassifyTextureOpCoverage(alphaOp) !=
                CKFF_COVERAGE_EXACT) {
            return RecordDrawReject(CKFF_DRAW_REJECT_TEXTURE_OP);
        }
        if (originBottomLeft && hasTexture &&
            (m_State.TextureFlags[stage] & CKRST_TEXTURE_RENDERTARGET) != 0 &&
            (m_State.TextureFlags[stage] &
                (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) != 0) {
            return RecordDrawReject(CKFF_DRAW_REJECT_RENDER_TARGET_TYPE);
        }
    }
    return TRUE;
}

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::MarkStaticUniformsDirty()
{
    m_OpaquePackets.MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::MarkPacketProgramDirty()
{
    m_OpaquePackets.MarkPacketProgramDirty();
}

void CKFixedFunctionPipeline::OnFixedFunctionStateChanged(CKDWORD changeMask)
{
    if (changeMask & CKFF_CHANGE_PROGRAM)
        MarkPacketProgramDirty();
    if (changeMask & CKFF_CHANGE_STATIC_UNIFORM)
        MarkStaticUniformsDirty();
}

CKBOOL CKFixedFunctionPipeline::BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                                               CKDWORD activeTextureCount)
{
    if (!bindingSet || !m_Context)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    const CKDWORD frameSerial = m_RenderPipeline.GetFrameNumber();
    if (m_BorderPaletteFrameSerial != frameSerial) {
        m_BorderPaletteFrameSerial = frameSerial;
        m_BorderPaletteCount = 0;
        memset(m_BorderPaletteColors, 0, sizeof(m_BorderPaletteColors));
    }
    m_TextureBinder.BuildBindingSet(bindingSet, activeTextureCount);
    for (CKDWORD i = 0; i < bindingSet->ActiveTextureCount; ++i) {
        CKSamplerDesc &sampler = bindingSet->Bindings[i].Sampler;
        if (sampler.AddressU != CKRST_ADDRESS_BORDER &&
            sampler.AddressV != CKRST_ADDRESS_BORDER &&
            sampler.AddressW != CKRST_ADDRESS_BORDER) {
            continue;
        }

        const CKDWORD argb = sampler.BorderColor;
        CKDWORD slot = 0;
        for (; slot < m_BorderPaletteCount; ++slot) {
            if (m_BorderPaletteColors[slot] == argb)
                break;
        }
        if (slot == m_BorderPaletteCount) {
            if (m_BorderPaletteCount >= 16)
                return RecordDrawReject(CKFF_DRAW_REJECT_BORDER_PALETTE);
            m_BorderPaletteColors[slot] = argb;
            ++m_BorderPaletteCount;
            const CKDWORD rgba = ((argb >> 16) & 0xffu) << 24 |
                                 ((argb >> 8) & 0xffu) << 16 |
                                 (argb & 0xffu) << 8 |
                                 ((argb >> 24) & 0xffu);
            m_Context->SetPaletteColor(slot, rgba);
        }
        sampler.BorderColor = slot;
    }
    bindingSet->Hash = CKFFHashRenderPacketTextureSet(
        bindingSet->ActiveTextureCount, bindingSet->Bindings);
    return TRUE;
}

void CKFixedFunctionPipeline::BeginDebugFrame() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_DebugState.BeginFrame();
    LogAndResetFrameStats();
#endif
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
}

// ============================================================================
// Drawing
// ============================================================================

CKBOOL CKFixedFunctionPipeline::DrawPrimitive(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
    VxDrawPrimitiveData *data)
{
    if (!encoder || !data || data->VertexCount == 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    if (HasOpaqueRenderPackets())
        FlushOpaqueRenderPackets(encoder, FALSE, FALSE);
    CKFF_PROBE(m_Probes, OnSoftwareDraw());

    bool hasNormal = (data->NormalPtr != nullptr);
    bool hasUV = (data->TexCoordPtr != nullptr);
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(data->Flags, hasNormal, hasUV, data->PositionStride);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial(view) : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.Indices = indices;
        debugInfo.IndexCount = indexCount;
        debugInfo.Data = data;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.Viewport = m_State.Viewport;
        m_DebugState.LogDrawPrimitiveHeader(debugInfo);
    }
#endif

    // Prepare transient geometry
    const CKDWORD wrapMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_WRAP0);
    CKFFPointSpriteParams pointParams;
    pointParams.Size = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE, 1.0f);
    pointParams.MinSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MIN, 1.0f);
    pointParams.MaxSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MAX, 64.0f);
    pointParams.ScaleEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE);
    pointParams.ScaleA = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_A, 1.0f);
    pointParams.ScaleB = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_B, 0.0f);
    pointParams.ScaleC = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_C, 0.0f);
    pointParams.World = m_State.World;
    pointParams.View = m_State.View;
    CKBOOL prepared = FALSE;
    {
        CKFF_SCOPE_TIME(m_Probes, PrepareUs);
        prepared = m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams,
            m_State.TexcoordComponentCounts);
    }
    if (!prepared) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitivePrepareFailed();
#endif
        CKFF_PROBE(m_Probes, OnPrepareFailure());
        return RecordDrawReject(CKFF_DRAW_REJECT_PREPARE_FAILED);
    }
    CKFF_PROBE(m_Probes, OnTransientGeometry(m_TransientGeometry.GetLastVertexBytes(),
                                             m_TransientGeometry.GetLastIndexBytes()));

    // Build the fixed-function state description and select the matching program.
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        data->Flags, m_State.TextureHandles, m_State.StageStates);
    if (!ValidateDrawState(formatFlags, activeTextureCount))
        return FALSE;
    CKFFPreparedState preparedState;
    CKFFShaderKey shaderKey;
    {
        CKFF_SCOPE_TIME(m_Probes, StateUs);
        BuildCurrentPreparedState(&preparedState, data->Flags, activeTextureCount,
                                  formatFlags, m_State.TexcoordComponentCounts);
        shaderKey = CKFFBuildShaderKeyFromPreparedState(&preparedState);
    }
    CKFFProgramBinding programBinding;
    {
        CKFF_SCOPE_TIME(m_Probes, ProgramUs);
        programBinding = m_ShaderCache.GetProgram(shaderKey);
    }
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, shaderKey, programBinding);
    CKDWORD program = programContext.Program;
    if (program == 0) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitiveProgramMissing();
#endif
        CKFF_PROBE(m_Probes, OnProgramMiss());
        return RecordDrawReject(CKFF_DRAW_REJECT_PROGRAM_MISSING);
    }
    CKFF_PROBE(m_Probes, OnProgram(program));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.Indices = indices;
        debugInfo.IndexCount = indexCount;
        debugInfo.Data = data;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.Viewport = m_State.Viewport;
        debugInfo.Program = program;
        debugInfo.ActiveTextureCount = (int)preparedState.ActiveTextureCount;
        debugInfo.ActiveLightCount = m_State.ActiveLightCount;
        debugInfo.StateDesc = &preparedState.StateDesc;
        debugInfo.DrawState = &m_DrawStateCache;
        debugInfo.Stage0.ColorOp = preparedState.StateDesc.FS.GetStageColorOp(0);
        debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_State.StageStates[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_State.StageStates[0], preparedState.ActiveTextureCount > 0, m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_State.StageStates[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawPrimitiveDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    if (!BuildCurrentTextureBindingSet(&textureBindingSet, preparedState.ActiveTextureCount))
        return FALSE;

    VXPRIMITIVETYPE drawStateType =
        (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
         (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
            ? VX_TRIANGLELIST : type;
    CKFFDrawSubmission submission = {};
    submission.View = view;
    submission.DrawStateType = drawStateType;
    submission.ProgramContext = &programContext;
    submission.Textures = &textureBindingSet;
    submission.Source = CKFF_SUBMIT_PRIMITIVE;
    return SubmitPrepared(encoder, submission);
}

CKBOOL CKFixedFunctionPipeline::DrawVertexBuffer(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!encoder || !vb || vertexCount == 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, m_State.TextureHandles, m_State.StageStates);
    if (!ValidateDrawState(formatFlags, activeTextureCount))
        return FALSE;
    const CKBOOL drawn = m_OpaquePackets.DrawVertexBuffer(
        *this, encoder, view, type, vb, ib,
        baseVertex, vertexCount, startIndex, indexCount,
        dpFlags, formatFlags, vertexLayout);
    if (drawn)
        m_LastDrawRejectReason = CKFF_DRAW_REJECT_NONE;
    return drawn;
}

// ============================================================================
// Internal methods
// ============================================================================

CKBOOL CKFixedFunctionPipeline::SubmitPrepared(
    CKRasterizerEncoder *encoder,
    const CKFFDrawSubmission &submission)
{
    const CKFFProgramContext *programContext = submission.ProgramContext;
    const CKFFTextureBindingSet *textures = submission.Textures;
    if (!encoder || !programContext || !textures)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);

    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        m_UniformEmitter.UploadUniforms(encoder, programContext, textures->ActiveTextureCount);
    }

    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    {
        CKFF_SCOPE_TIME(m_Probes, TransformUs);
        encoder->SetTransform(transformIdx, 1);
    }
    CKFF_PROBE(m_Probes, OnTransformSet());

    CKDrawState drawState;
    {
        CKFF_SCOPE_TIME(m_Probes, DrawStateBuildUs);
        drawState = m_DrawStateCache.BuildDrawState(submission.DrawStateType);
    }
    CKFF_PROBE(m_Probes, OnDrawState(drawState));
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    {
        CKFF_SCOPE_TIME(m_Probes, EncoderStateUs);
        encoder->SetState(drawState);
    }
    {
        CKFF_SCOPE_TIME(m_Probes, StencilUs);
        encoder->SetStencilRef(stencilRef);
        encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
    }

    if (submission.VertexLayout)
        CKFF_PROBE(m_Probes, OnVertexLayoutSet());

    if (submission.VertexBuffer) {
        CKFF_PROBE(m_Probes, OnVertexBuffers(submission.VertexBuffer, submission.IndexBuffer, submission.VertexLayout));
        {
            CKFF_SCOPE_TIME(m_Probes, BufferBindUs);
            encoder->SetVertexBuffer(0, submission.VertexBuffer, submission.BaseVertex,
                                     submission.VertexCount, submission.VertexLayout);
            if (submission.IndexBuffer)
                encoder->SetIndexBuffer(submission.IndexBuffer, submission.StartIndex, submission.IndexCount);
        }
        CKFF_PROBE(m_Probes, OnVertexBufferSet());
        if (submission.IndexBuffer)
            CKFF_PROBE(m_Probes, OnIndexBufferSet());
    }

    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        BindTextures(encoder, textures);
    }

    float depth = ComputeDepthKey();
    {
        CKFF_SCOPE_TIME(m_Probes, SubmitUs);
        encoder->Submit(submission.View, programContext->Program, *(CKDWORD *)&depth, SubmitDiscardFlags());
        if (submission.Source == CKFF_SUBMIT_PRIMITIVE) {
            CK_FRAME_COST_ADD_PRIMITIVE_SUBMIT();
        } else {
            CK_FRAME_COST_ADD_MESH_SUBMIT();
        }
        CK_FRAME_COST_ADD_SUBMITTED_DRAW();
    }
    CKFF_PROBE(m_Probes, OnSubmittedDraw());
    m_LastDrawRejectReason = CKFF_DRAW_REJECT_NONE;
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::SubmitVertexBufferImmediate(
    CKRasterizerEncoder *encoder,
    CKRenderView view,
    VXPRIMITIVETYPE type,
    CKDWORD vb,
    CKDWORD ib,
    CKDWORD baseVertex,
    CKDWORD vertexCount,
    CKDWORD startIndex,
    CKDWORD indexCount,
    CKDWORD dpFlags,
    CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!encoder || !vb)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    CKFF_PROBE(m_Probes, OnHardwareDraw());
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial(view) : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.VertexBuffer = vb;
        debugInfo.IndexBuffer = ib;
        debugInfo.BaseVertex = baseVertex;
        debugInfo.VertexCount = vertexCount;
        debugInfo.StartIndex = startIndex;
        debugInfo.PersistentIndexCount = indexCount;
        debugInfo.DPFlags = dpFlags;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.VertexLayout = vertexLayout;
        debugInfo.DrawSerial = debugDrawSerial;
        m_DebugState.LogDrawVertexBufferHeader(debugInfo);
    }
#endif

    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, m_State.TextureHandles, m_State.StageStates);
    CKFFPreparedState preparedState;
    CKFFShaderKey shaderKey;
    {
        CKFF_SCOPE_TIME(m_Probes, StateUs);
        BuildCurrentPreparedState(&preparedState, dpFlags, activeTextureCount, formatFlags);
        shaderKey = CKFFBuildShaderKeyFromPreparedState(&preparedState);
    }
    CKFFProgramBinding programBinding;
    {
        CKFF_SCOPE_TIME(m_Probes, ProgramUs);
        programBinding = m_ShaderCache.GetProgram(shaderKey);
    }
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, shaderKey, programBinding);
    CKDWORD program = programContext.Program;
    if (program == 0) {
        CKFF_PROBE(m_Probes, OnProgramMiss());
        return RecordDrawReject(CKFF_DRAW_REJECT_PROGRAM_MISSING);
    }
    CKFF_PROBE(m_Probes, OnProgram(program));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.VertexBuffer = vb;
        debugInfo.IndexBuffer = ib;
        debugInfo.BaseVertex = baseVertex;
        debugInfo.VertexCount = vertexCount;
        debugInfo.StartIndex = startIndex;
        debugInfo.PersistentIndexCount = indexCount;
        debugInfo.DPFlags = dpFlags;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.VertexLayout = vertexLayout;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.Program = program;
        debugInfo.ActiveTextureCount = (int)preparedState.ActiveTextureCount;
        debugInfo.ActiveLightCount = m_State.ActiveLightCount;
        debugInfo.StateDesc = &preparedState.StateDesc;
        debugInfo.DrawState = &m_DrawStateCache;
        debugInfo.Stage0.ColorOp = preparedState.StateDesc.FS.GetStageColorOp(0);
        debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_State.StageStates[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0,
            m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_State.StageStates[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawVertexBufferDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    if (!BuildCurrentTextureBindingSet(&textureBindingSet, preparedState.ActiveTextureCount))
        return FALSE;

    CKFFDrawSubmission submission = {};
    submission.View = view;
    submission.DrawStateType = type;
    submission.ProgramContext = &programContext;
    submission.Textures = &textureBindingSet;
    submission.VertexBuffer = vb;
    submission.IndexBuffer = ib;
    submission.BaseVertex = baseVertex;
    submission.VertexCount = vertexCount;
    submission.StartIndex = startIndex;
    submission.IndexCount = indexCount;
    submission.VertexLayout = vertexLayout;
    submission.Source = CKFF_SUBMIT_VERTEX_BUFFER;
    return SubmitPrepared(encoder, submission);
}

void CKFixedFunctionPipeline::BuildCurrentPreparedState(
    CKFFPreparedState *prepared, CKDWORD dpFlags, CKDWORD activeTextureCount,
    CKDWORD formatFlags, const CKBYTE *texcoordComponentCounts) {
    CKFFStateResolver::BuildPreparedState(m_State, m_DrawStateCache, prepared, dpFlags,
                                          activeTextureCount, formatFlags, texcoordComponentCounts);
}

CKBOOL CKFixedFunctionPipeline::BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                                          const CKFFProgramContext *programContext,
                                                          CKDWORD activeTextureCount)
{
    return m_UniformEmitter.BuildStaticUniformPayload(payload, programContext, activeTextureCount);
}

void CKFixedFunctionPipeline::UpdateViewProjectionCache()
{
    if (!m_State.EnsureViewProjection())
        return;
    CKFF_PROBE(m_Probes, OnViewProjectionRebuild());
}

CKBOOL CKFixedFunctionPipeline::BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                                          const CKFFProgramContext *programContext)
{
    UpdateViewProjectionCache();
    return m_UniformEmitter.BuildObjectUniforms(uniforms, programContext);
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder,
                                           const CKFFTextureBindingSet *bindingSet) {
    m_TextureBinder.Bind(encoder, bindingSet);
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKFFSubmitDiscardFlags(m_State, m_DrawStateCache);
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    CKFF_PROBE(m_Probes, LogAndReset(m_DrawStateCache, m_ShaderCache.GetUniforms()));
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    return m_TextureBinder.BuildSamplerDesc(stage);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    return CKFFComputeDepthKey(m_State, m_DrawStateCache);
}

void CKFixedFunctionPipeline::FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder,
                                                       CKBOOL forceDirectReplay,
                                                       CKBOOL allowAdaptiveLearning)
{
    m_OpaquePackets.FlushRenderPackets(*this, encoder, forceDirectReplay, allowAdaptiveLearning);
}
