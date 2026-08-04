#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKFFSamplerLayout.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfStats.h"
#include "CKRenderFrameCostStats.h"

#include <math.h>
#include <string.h>


CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr),
#if CKRE_ENABLE_FFP_DIAGNOSTICS
      m_DrawPreparer(m_State, m_DrawStateCache, m_ShaderCache, m_Probes),
      m_TextureBinder(m_State, m_ShaderCache, m_Probes),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache, m_Probes),
#else
      m_DrawPreparer(m_State, m_DrawStateCache, m_ShaderCache),
      m_TextureBinder(m_State, m_ShaderCache),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache),
#endif
      m_OpaquePackets(), m_LastDrawRejectReason(CKFF_DRAW_REJECT_NONE),
      m_FrameDrawRejected(FALSE),
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
    m_FrameDrawRejected = FALSE;
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
        if (shaderBackend &&
            caps.MaxTextureBindings < CKFF_MAX_PROGRAM_SAMPLER_BINDINGS) {
            Shutdown();
            return false;
        }
    }
    if (shaderBackend && !m_ShaderCache.Init(ctx)) {
        Shutdown();
        return false;
    }
    m_DrawStateCache.Reset();
    m_VertexLayoutCache.Init(ctx);
    m_TextureBinder.ResetProgramBindings();
    m_OpaquePackets.SetInstanceLayout(m_VertexLayoutCache.GetLayout(CKFF_VF_TEXCOORD0 |
                                                                    CKFF_VF_TEXCOORD1 |
                                                                    CKFF_VF_TEXCOORD2 |
                                                                    CKFF_VF_TEXCOORD3));
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_State.MarkViewProjectionDirty();
    MarkPreparedProgramDirty();
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
    m_TextureBinder.ResetProgramBindings();
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
    case CKFF_DRAW_REJECT_VERTEX_BLEND_PALETTE: return "vertex-blend-palette";
    case CKFF_DRAW_REJECT_AFFINE_TEXCOORD: return "affine-texture-coordinates";
    case CKFF_DRAW_REJECT_TEXTURE_OP: return "texture-operation";
    case CKFF_DRAW_REJECT_RENDER_TARGET_TYPE: return "render-target-type";
    case CKFF_DRAW_REJECT_BORDER_PALETTE: return "border-palette";
    case CKFF_DRAW_REJECT_DEPTH_COMPARE_FILTER: return "filtered-depth-compare";
    case CKFF_DRAW_REJECT_DITHER: return "dither";
    case CKFF_DRAW_REJECT_ZBIAS: return "z-bias";
    case CKFF_DRAW_REJECT_LINE_PATTERN: return "line-pattern";
    case CKFF_DRAW_REJECT_EDGE_ANTIALIAS: return "edge-antialias";
    case CKFF_DRAW_REJECT_CLIPPING_DISABLED: return "clipping-disabled";
    case CKFF_DRAW_REJECT_STAGE_BLEND: return "texture-stage-blend";
    case CKFF_DRAW_REJECT_SAMPLER_LOD_CONTROL: return "sampler-lod-control";
    case CKFF_DRAW_REJECT_SAMPLER_ANISOTROPY_LIMIT: return "sampler-anisotropy-limit";
    case CKFF_DRAW_REJECT_SAMPLER_LAYOUT: return "sampler-layout";
    case CKFF_DRAW_REJECT_STATE_VALUE: return "state-value";
    case CKFF_DRAW_REJECT_ENCODER_ERROR: return "encoder-error";
    case CKFF_DRAW_REJECT_POINT_VERTEX_BUFFER: return "point-vertex-buffer";
    default: return "none";
    }
}

static CKFFDrawRejectReason CKFFProgramPrepareRejectReason(
    CKFFProgramPrepareStatus status)
{
    switch (status) {
    case CKFF_PROGRAM_PREPARE_SAMPLER_LAYOUT:
        return CKFF_DRAW_REJECT_SAMPLER_LAYOUT;
    case CKFF_PROGRAM_PREPARE_PROGRAM_MISSING:
        return CKFF_DRAW_REJECT_PROGRAM_MISSING;
    case CKFF_PROGRAM_PREPARE_INVALID_INPUT:
        return CKFF_DRAW_REJECT_INVALID_INPUT;
    default:
        return CKFF_DRAW_REJECT_NONE;
    }
}

static CKBOOL CKFFValueInRange(CKDWORD value, CKDWORD first, CKDWORD last)
{
    return value >= first && value <= last ? TRUE : FALSE;
}

static CKBOOL CKFFValidTextureArgument(CKDWORD value)
{
    if ((value & ~(CKRST_TA_COMPLEMENT | CKRST_TA_ALPHAREPLICATE | 0x0fu)) != 0)
        return FALSE;
    return (value & 0x0fu) <= CKRST_TA_CONSTANT ? TRUE : FALSE;
}

static CKBOOL CKFFValidDrawStateValues(const CKDrawStateCache &drawState)
{
    if (!CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_FILLMODE),
                          VXFILL_POINT, VXFILL_SOLID) ||
        !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_SHADEMODE),
                          VXSHADE_FLAT, VXSHADE_GOURAUD) ||
        !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_CULLMODE),
                          VXCULL_NONE, VXCULL_CCW)) {
        return FALSE;
    }
    if (drawState.GetRenderState(VXRENDERSTATE_ZENABLE) &&
        !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_ZFUNC),
                          VXCMP_NEVER, VXCMP_ALWAYS)) {
        return FALSE;
    }
    if (drawState.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE) &&
        !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_ALPHAFUNC),
                          VXCMP_NEVER, VXCMP_ALWAYS)) {
        return FALSE;
    }
    if (drawState.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE) &&
        (!CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_SRCBLEND),
                           VXBLEND_ZERO, VXBLEND_BOTHINVSRCALPHA) ||
         !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_DESTBLEND),
                           VXBLEND_ZERO, VXBLEND_SRCALPHASAT) ||
         !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_BLENDOP),
                           VXBLENDOP_ADD, VXBLENDOP_MAX))) {
        return FALSE;
    }
    if (drawState.GetRenderState(VXRENDERSTATE_FOGENABLE) &&
        (drawState.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) > VXFOG_LINEAR ||
         drawState.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE) > VXFOG_LINEAR)) {
        return FALSE;
    }
    if (drawState.GetRenderState(VXRENDERSTATE_STENCILENABLE)) {
        if (!CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_STENCILFUNC),
                              VXCMP_NEVER, VXCMP_ALWAYS) ||
            !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_STENCILFAIL),
                              VXSTENCILOP_KEEP, VXSTENCILOP_DECR) ||
            !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_STENCILZFAIL),
                              VXSTENCILOP_KEEP, VXSTENCILOP_DECR) ||
            !CKFFValueInRange(drawState.GetRenderState(VXRENDERSTATE_STENCILPASS),
                              VXSTENCILOP_KEEP, VXSTENCILOP_DECR)) {
            return FALSE;
        }
    }
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if ((drawState.GetRenderState(
                 (VXRENDERSTATETYPE)(VXRENDERSTATE_WRAP0 + stage)) & ~VXWRAP_MASK) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static CKBOOL CKFFValidTextureStageValues(const CKDWORD *stageState)
{
    if (!stageState)
        return FALSE;
    const CKDWORD transformFlags = stageState[CKRST_TSS_TEXTURETRANSFORMFLAGS];
    if ((transformFlags & ~(0xffu | CKRST_TTF_PROJECTED)) != 0 ||
        (transformFlags & 0xffu) > CKRST_TTF_COUNT4) {
        return FALSE;
    }
    const CKDWORD texcoordIndex = CKFFTexcoordIndex(stageState[CKRST_TSS_TEXCOORDINDEX]);
    const CKDWORD texgen = CKFFTexcoordGeneration(stageState[CKRST_TSS_TEXCOORDINDEX]);
    if (texcoordIndex >= CKFF_MAX_TEXTURE_STAGES || texgen > CKFF_TEXGEN_SPHEREMAP)
        return FALSE;

    const CKDWORD addressStates[4] = {
        stageState[CKRST_TSS_ADDRESS],
        stageState[CKRST_TSS_ADDRESSU],
        stageState[CKRST_TSS_ADDRESSV],
        stageState[CKRST_TSS_ADDRESW]
    };
    for (int i = 0; i < 4; ++i) {
        if (addressStates[i] != 0 &&
            !CKFFValueInRange(addressStates[i], VXTEXTURE_ADDRESSWRAP,
                              VXTEXTURE_ADDRESSMIRRORONCE)) {
            return FALSE;
        }
    }
    const CKDWORD filters[2] = {
        stageState[CKRST_TSS_MINFILTER],
        stageState[CKRST_TSS_MAGFILTER]
    };
    for (int i = 0; i < 2; ++i) {
        if (filters[i] != 0 &&
            !CKFFValueInRange(filters[i], VXTEXTUREFILTER_NEAREST,
                              VXTEXTUREFILTER_ANISOTROPIC)) {
            return FALSE;
        }
    }
    const CKDWORD compareFunc = stageState[CKRST_TSS_COMPAREFUNC];
    return compareFunc == CKRST_COMPARE_NONE ||
           CKFFValueInRange(compareFunc, VXCMP_NEVER, VXCMP_ALWAYS);
}

static float CKFFResolveConstantPointSize(const CKDrawStateCache &drawState)
{
    return CKTransientGeometry::ComputePointSpriteSizeForDistance(
        CKFFReadFloatRenderState(drawState, VXRENDERSTATE_POINTSIZE, 1.0f),
        CKFFReadFloatRenderState(drawState, VXRENDERSTATE_POINTSIZE_MIN, 1.0f),
        CKFFReadFloatRenderState(drawState, VXRENDERSTATE_POINTSIZE_MAX, 64.0f),
        FALSE, 1.0f, 0.0f, 0.0f, 0.0f);
}

CKBOOL CKFixedFunctionPipeline::RecordDrawReject(CKFFDrawRejectReason reason)
{
    m_LastDrawRejectReason = reason;
    m_FrameDrawRejected = TRUE;
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

CKBOOL CKFixedFunctionPipeline::RejectPendingSubmission(
    CKRasterizerEncoder *encoder, CKFFDrawRejectReason reason)
{
    if (encoder)
        encoder->Discard(CKRST_DISCARD_ALL);
    return RecordDrawReject(reason);
}

CKBOOL CKFixedFunctionPipeline::ValidateDrawState(CKDWORD formatFlags,
                                                   CKDWORD activeTextureCount)
{
    if (!CKFFValidDrawStateValues(m_DrawStateCache) ||
        (m_DrawStateCache.GetColorWriteMask() & ~CKRST_STATE_WRITE_RGBA) != 0) {
        return RecordDrawReject(CKFF_DRAW_REJECT_STATE_VALUE);
    }
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_DITHERENABLE))
        return RecordDrawReject(CKFF_DRAW_REJECT_DITHER);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZBIAS) != 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_ZBIAS);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_LINEPATTERN) != 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_LINE_PATTERN);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_EDGEANTIALIAS))
        return RecordDrawReject(CKFF_DRAW_REJECT_EDGE_ANTIALIAS);
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPING))
        return RecordDrawReject(CKFF_DRAW_REJECT_CLIPPING_DISABLED);

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
        if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND) ==
            VXVBLEND_TWEENING) {
            return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_TWEEN);
        }
        return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_INPUT);
    }
    if (vertexBlend.Indexed && m_State.VertexBlendPaletteOverflow)
        return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_PALETTE);

    if (activeTextureCount > CKFF_MAX_TEXTURE_STAGES)
        activeTextureCount = CKFF_MAX_TEXTURE_STAGES;
    const CKBOOL perspectiveTexture =
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE) != 0;
    const CKBOOL originBottomLeft =
        (m_ShaderCache.GetTargetFlags() & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) != 0;
    CKDWORD previousColorOp = 0;
    CKDWORD previousAlphaOp = 0;
    for (CKDWORD stage = 0; stage < activeTextureCount; ++stage) {
        const uint64_t stateSetMask = m_State.StageStateSetMasks[stage];
        const CKBOOL textureBound = m_State.TextureHandles[stage] != 0;
        const CKDWORD colorOp = CKFFResolveStageColorOp(
            m_State.StageStates[stage], TRUE, textureBound);
        if (colorOp == CKRST_TOP_DISABLE)
            break;
        const CKDWORD alphaOp = CKFFResolveStageAlphaOp(
            m_State.StageStates[stage], TRUE, textureBound);
        if ((stateSetMask & (1ull << CKRST_TSS_STAGEBLEND)) != 0) {
            CKDWORD ignoredColorOp = 0;
            CKDWORD ignoredColorArg1 = 0;
            CKDWORD ignoredColorArg2 = 0;
            CKDWORD ignoredAlphaOp = 0;
            CKDWORD ignoredAlphaArg1 = 0;
            CKDWORD ignoredAlphaArg2 = 0;
            if (!CKFFStageBlendToTextureOps(
                    m_State.StageStates[stage][CKRST_TSS_STAGEBLEND],
                    ignoredColorOp, ignoredColorArg1, ignoredColorArg2,
                    ignoredAlphaOp, ignoredAlphaArg1, ignoredAlphaArg2)) {
                return RecordDrawReject(CKFF_DRAW_REJECT_STAGE_BLEND);
            }
        }
        if (CKFFClassifyTextureOpCoverage(colorOp) !=
                CKFF_COVERAGE_EXACT ||
            CKFFClassifyTextureOpCoverage(alphaOp) !=
                CKFF_COVERAGE_EXACT) {
            return RecordDrawReject(CKFF_DRAW_REJECT_TEXTURE_OP);
        }
        if (alphaOp == CKRST_TOP_BUMPENVMAP ||
            alphaOp == CKRST_TOP_BUMPENVMAPLUMINANCE) {
            return RecordDrawReject(CKFF_DRAW_REJECT_TEXTURE_OP);
        }
        if ((colorOp == CKRST_TOP_BUMPENVMAP ||
             colorOp == CKRST_TOP_BUMPENVMAPLUMINANCE) &&
            (m_State.TextureFlags[stage] & CKRST_TEXTURE_BUMPDUDV) == 0) {
            return RecordDrawReject(CKFF_DRAW_REJECT_TEXTURE_OP);
        }
        if (colorOp == CKRST_TOP_BUMPENVMAPLUMINANCE &&
            (m_State.TextureFlags[stage] & CKRST_TEXTURE_BUMPLUMINANCE) == 0) {
            return RecordDrawReject(CKFF_DRAW_REJECT_TEXTURE_OP);
        }
        CKFFShaderKeyFSStage shaderStage = {};
        shaderStage.ColorOp = colorOp;
        shaderStage.ColorArg0 = CKFFResolveStageColorArg0(
            m_State.StageStates[stage], stateSetMask);
        shaderStage.ColorArg1 = CKFFResolveStageColorArg1(
            m_State.StageStates[stage], textureBound, stateSetMask);
        shaderStage.ColorArg2 = CKFFResolveStageColorArg2(
            m_State.StageStates[stage], stateSetMask);
        shaderStage.AlphaOp = alphaOp;
        shaderStage.AlphaArg0 = CKFFResolveStageAlphaArg0(
            m_State.StageStates[stage], stateSetMask);
        shaderStage.AlphaArg1 = CKFFResolveStageAlphaArg1(
            m_State.StageStates[stage], textureBound, stateSetMask);
        shaderStage.AlphaArg2 = CKFFResolveStageAlphaArg2(
            m_State.StageStates[stage], stateSetMask);
        const CKDWORD resultArg = CKFFResolveStageResultArg(
            m_State.StageStates[stage], stateSetMask);
        if (!CKFFValidTextureArgument(shaderStage.ColorArg0) ||
            !CKFFValidTextureArgument(shaderStage.ColorArg1) ||
            !CKFFValidTextureArgument(shaderStage.ColorArg2) ||
            !CKFFValidTextureArgument(shaderStage.AlphaArg0) ||
            !CKFFValidTextureArgument(shaderStage.AlphaArg1) ||
            !CKFFValidTextureArgument(shaderStage.AlphaArg2) ||
            (resultArg != CKRST_TA_CURRENT && resultArg != CKRST_TA_TEMP)) {
            return RecordDrawReject(CKFF_DRAW_REJECT_STATE_VALUE);
        }
        const CKBOOL samplesTexture = textureBound &&
            CKFFShaderKeyStageUsesTexture(
                shaderStage, previousColorOp, previousAlphaOp);
        if (samplesTexture) {
            if (!CKFFValidTextureStageValues(m_State.StageStates[stage]))
                return RecordDrawReject(CKFF_DRAW_REJECT_STATE_VALUE);
            const CKSamplerDesc sampler = BuildSamplerDesc((int)stage);
            const CKDWORD lodBiasBits =
                m_State.StageStates[stage][CKRST_TSS_MIPMAPLODBIAS];
            float lodBias = 0.0f;
            memcpy(&lodBias, &lodBiasBits, sizeof(lodBias));
            if (sampler.MipFilter != CKRST_FILTER_NONE &&
                (lodBias != 0.0f ||
                 m_State.StageStates[stage][CKRST_TSS_MAXMIPMLEVEL] != 0)) {
                return RecordDrawReject(CKFF_DRAW_REJECT_SAMPLER_LOD_CONTROL);
            }
            const CKBOOL anisotropic =
                sampler.MinFilter == CKRST_FILTER_ANISOTROPIC ||
                sampler.MagFilter == CKRST_FILTER_ANISOTROPIC ||
                sampler.MipFilter == CKRST_FILTER_ANISOTROPIC;
            if (anisotropic &&
                m_State.StageStates[stage][CKRST_TSS_MAXANISOTROPY] > 1) {
                return RecordDrawReject(CKFF_DRAW_REJECT_SAMPLER_ANISOTROPY_LIMIT);
            }
        }
        if (!perspectiveTexture && samplesTexture)
            return RecordDrawReject(CKFF_DRAW_REJECT_AFFINE_TEXCOORD);
        if (originBottomLeft && samplesTexture &&
            (m_State.TextureFlags[stage] & CKRST_TEXTURE_RENDERTARGET) != 0 &&
            (m_State.TextureFlags[stage] &
                (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) != 0) {
            return RecordDrawReject(CKFF_DRAW_REJECT_RENDER_TARGET_TYPE);
        }
        if (samplesTexture &&
            (m_State.TextureFlags[stage] & CKRST_TEXTURE_DEPTHSTENCIL) != 0 &&
            m_State.StageStates[stage][CKRST_TSS_COMPAREFUNC] != CKRST_COMPARE_NONE) {
            const CKSamplerDesc sampler = BuildSamplerDesc((int)stage);
            if (sampler.MinFilter != CKRST_FILTER_NEAREST ||
                sampler.MagFilter != CKRST_FILTER_NEAREST ||
                (sampler.MipFilter != CKRST_FILTER_NONE &&
                 sampler.MipFilter != CKRST_FILTER_NEAREST &&
                 sampler.MipFilter != CKRST_FILTER_MIPNEAREST)) {
                return RecordDrawReject(CKFF_DRAW_REJECT_DEPTH_COMPARE_FILTER);
            }
        }
        previousColorOp = colorOp;
        previousAlphaOp = alphaOp;
    }
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::ValidateVertexBlendIndices(
    const VxDrawPrimitiveData *data,
    CKDWORD formatFlags)
{
    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
        formatFlags);
    if (!vertexBlend.Supported || !vertexBlend.Indexed ||
        vertexBlend.Mode != CKFF_VERTEX_BLEND_NORMAL) {
        return TRUE;
    }
    if (!data || !data->PositionPtr || data->VertexCount <= 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_INPUT);

    const CKDWORD weightCount = CKVertexLayoutCache::DPFlagsToBlendWeightCount(data->Flags);
    if (weightCount < vertexBlend.Count)
        return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_INPUT);
    const CKDWORD indexOffset = CKVertexLayoutCache::DPFlagsToBlendIndexOffset(data->Flags);
    if (data->PositionStride < indexOffset + sizeof(CKDWORD))
        return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_INPUT);

    const CKDWORD usedIndexCount = vertexBlend.Count + 1;
    for (int vertex = 0; vertex < data->VertexCount; ++vertex) {
        CKDWORD packedIndices = 0;
        const CKBYTE *src = (const CKBYTE *)data->PositionPtr +
                            vertex * data->PositionStride + indexOffset;
        memcpy(&packedIndices, src, sizeof(packedIndices));
        for (CKDWORD slot = 0; slot < usedIndexCount; ++slot) {
            if (((packedIndices >> (slot * 8)) & 0xffu) >=
                CKFF_VERTEX_BLEND_MATRIX_COUNT) {
                return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_PALETTE);
            }
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

void CKFixedFunctionPipeline::MarkPreparedProgramDirty()
{
    m_DrawPreparer.InvalidateVertexBufferProgramCache();
}

void CKFixedFunctionPipeline::OnFixedFunctionStateChanged(CKDWORD changeMask)
{
    if (changeMask & CKFF_CHANGE_PROGRAM)
        MarkPreparedProgramDirty();
    if (changeMask & CKFF_CHANGE_STATIC_UNIFORM)
        MarkStaticUniformsDirty();
}

CKBOOL CKFixedFunctionPipeline::BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                                               CKDWORD activeTextureCount,
                                                               const CKFFShaderKey &shaderKey)
{
    if (!bindingSet || !m_Context)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    const CKDWORD frameSerial = m_RenderPipeline.GetFrameNumber();
    if (m_BorderPaletteFrameSerial != frameSerial) {
        m_BorderPaletteFrameSerial = frameSerial;
        m_BorderPaletteCount = 0;
        memset(m_BorderPaletteColors, 0, sizeof(m_BorderPaletteColors));
    }
    CKDWORD sampledTextureMask = 0;
    CKDWORD stageCount = activeTextureCount;
    if (stageCount > CKFF_MAX_TEXTURE_STAGES)
        stageCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < stageCount; ++stage) {
        if (shaderKey.FS.Stages[stage].HasTexture)
            sampledTextureMask |= 1u << stage;
    }
    m_TextureBinder.BuildBindingSet(bindingSet, activeTextureCount, sampledTextureMask);
    bindingSet->ActiveStageCount = stageCount;
    const CKFFSamplerLayoutKey samplerLayout = CKFFBuildSamplerLayoutKey(shaderKey.FS);
    if (CKFFSamplerLayoutSupportsGenericMixed(samplerLayout)) {
        const CKFFUniformHandles &uniforms = m_ShaderCache.GetUniforms();
        CKDWORD cubeIndex = 0;
        CKDWORD volumeIndex = 0;
        for (CKDWORD stage = 0; stage < bindingSet->ActiveTextureCount; ++stage) {
            CKFFRenderPacketTextureBinding &binding = bindingSet->Bindings[stage];
            if (shaderKey.FS.Stages[stage].SamplerType == CKFF_SAMPLER_VOLUME) {
                binding.Stage = CKFF_MAX_TEXTURE_STAGES + 4 + volumeIndex;
                binding.Uniform = uniforms.s_textureVolume[volumeIndex];
                ++volumeIndex;
            } else if (shaderKey.FS.Stages[stage].SamplerType == CKFF_SAMPLER_CUBE) {
                binding.Stage = CKFF_MAX_TEXTURE_STAGES + cubeIndex;
                binding.Uniform = uniforms.s_textureCube[cubeIndex];
                ++cubeIndex;
            }
        }
    }
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
    m_FrameDrawRejected = FALSE;
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

    const CKDWORD formatFlags =
        CKVertexLayoutCache::DrawPrimitiveDataToFormatFlags(data);
    const CKBOOL pointSprites = type == VX_POINTLIST &&
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE) != 0;
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

    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureStageCount(
        m_State.TextureHandles, m_State.StageStates);
    if (!ValidateDrawState(formatFlags, activeTextureCount))
        return FALSE;
    if (!ValidateVertexBlendIndices(data, formatFlags))
        return FALSE;

    // Prepare transient geometry
    CKDWORD wrapModes[CKFF_MAX_TEXTURE_STAGES];
    CKBOOL wrapsTexcoords = FALSE;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        wrapModes[stage] = m_DrawStateCache.GetRenderState(
            (VXRENDERSTATETYPE)(VXRENDERSTATE_WRAP0 + stage));
        const void *texcoord = stage == 0
            ? data->TexCoordPtr
            : data->TexCoordPtrs[stage - 1];
        if ((wrapModes[stage] & VXWRAP_MASK) != 0 &&
            (formatFlags & CKFF_VF_TEXCOORD(stage)) != 0 && texcoord)
            wrapsTexcoords = TRUE;
    }
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
    pointParams.Projection = m_State.Projection;
    pointParams.ViewportWidth = m_State.Viewport[0] != 0.0f
        ? fabsf(2.0f / m_State.Viewport[0]) : 1.0f;
    pointParams.ViewportHeight = m_State.Viewport[1] != 0.0f
        ? fabsf(2.0f / m_State.Viewport[1]) : 1.0f;
    CKFFProgramPreparation programPreparation;
    const CKFFProgramPrepareStatus prepareStatus = m_DrawPreparer.PrepareProgram(
        &programPreparation, data->Flags, activeTextureCount, formatFlags,
        m_State.TexcoordComponentCounts, pointSprites);
    if (prepareStatus != CKFF_PROGRAM_PREPARE_OK) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging &&
            prepareStatus == CKFF_PROGRAM_PREPARE_PROGRAM_MISSING) {
            m_DebugState.LogDrawPrimitiveProgramMissing();
        }
#endif
        if (prepareStatus == CKFF_PROGRAM_PREPARE_PROGRAM_MISSING)
            CKFF_PROBE(m_Probes, OnProgramMiss());
        return RecordDrawReject(CKFFProgramPrepareRejectReason(prepareStatus));
    }
    const CKFFPreparedState &preparedState = programPreparation.PreparedState;
    const CKFFProgramContext &programContext = programPreparation.ProgramContext;
    const CKFFShaderKey &shaderKey = programContext.ShaderKey;
    const CKDWORD program = programContext.Program;
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
        debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0,
            m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(
            m_State.StageStates[0], m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_State.StageStates[0], preparedState.ActiveTextureCount > 0, m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0,
            m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(
            m_State.StageStates[0], m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawPrimitiveDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    if (!BuildCurrentTextureBindingSet(
            &textureBindingSet, preparedState.ActiveTextureCount, shaderKey))
        return FALSE;

    CKBOOL prepared = FALSE;
    {
        CKFF_SCOPE_TIME(m_Probes, PrepareUs);
        prepared = m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapModes[0],
            pointSprites, &pointParams,
            m_State.TexcoordComponentCounts, wrapModes);
    }
    if (!prepared) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitivePrepareFailed();
#endif
        encoder->Discard(CKRST_DISCARD_ALL);
        CKFF_PROBE(m_Probes, OnPrepareFailure());
        return RecordDrawReject(CKFF_DRAW_REJECT_PREPARE_FAILED);
    }
    CKFF_PROBE(m_Probes, OnTransientGeometry(m_TransientGeometry.GetLastVertexBytes(),
                                             m_TransientGeometry.GetLastIndexBytes()));

    VXPRIMITIVETYPE drawStateType = type;
    if (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
        type == VX_POINTLIST) {
        drawStateType = VX_TRIANGLELIST;
    } else if (type == VX_LINESTRIP && wrapsTexcoords) {
        drawStateType = VX_LINELIST;
    }
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
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureStageCount(
        m_State.TextureHandles, m_State.StageStates);
    if (!ValidateDrawState(formatFlags, activeTextureCount))
        return FALSE;
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) &&
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND) != VXVBLEND_DISABLE) {
        return RecordDrawReject(CKFF_DRAW_REJECT_VERTEX_BLEND_PALETTE);
    }
    if (type == VX_POINTLIST) {
        if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE) ||
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE) ||
            (dpFlags & CKRST_DP_PSIZE) != 0) {
            return RecordDrawReject(CKFF_DRAW_REJECT_POINT_VERTEX_BUFFER);
        }
        const float pointSize = CKFFResolveConstantPointSize(m_DrawStateCache);
        if (pointSize > 15.0f || floorf(pointSize) != pointSize)
            return RecordDrawReject(CKFF_DRAW_REJECT_POINT_VERTEX_BUFFER);
    }
    CKFFProgramPreparation preparation;
    const CKFFProgramPrepareStatus prepareStatus =
        m_DrawPreparer.PrepareVertexBufferProgram(
            &preparation, dpFlags, formatFlags);
    if (prepareStatus != CKFF_PROGRAM_PREPARE_OK) {
        if (prepareStatus == CKFF_PROGRAM_PREPARE_PROGRAM_MISSING)
            CKFF_PROBE(m_Probes, OnProgramMiss());
        return RecordDrawReject(CKFFProgramPrepareRejectReason(prepareStatus));
    }
    const CKBOOL drawn = m_OpaquePackets.DrawVertexBuffer(
        *this, preparation, encoder, view, type, vb, ib,
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
    if (encoder->GetStatus() != CK_OK)
        return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);

    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        m_UniformEmitter.UploadUniforms(encoder, programContext, textures->ActiveStageCount);
    }
    if (encoder->GetStatus() != CK_OK)
        return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);

    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    {
        CKFF_SCOPE_TIME(m_Probes, TransformUs);
        encoder->SetTransform(transformIdx, 1);
    }
    if (encoder->GetStatus() != CK_OK)
        return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);
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
        if (submission.DrawStateType == VX_POINTLIST)
            encoder->SetPointSize(CKFFResolveConstantPointSize(m_DrawStateCache));
        encoder->SetState(drawState);
    }
    if (encoder->GetStatus() != CK_OK)
        return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);
    {
        CKFF_SCOPE_TIME(m_Probes, StencilUs);
        encoder->SetStencilRef(stencilRef);
        if (encoder->GetStatus() != CK_OK)
            return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);
        encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
    }
    if (encoder->GetStatus() != CK_OK)
        return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);

    if (submission.VertexLayout)
        CKFF_PROBE(m_Probes, OnVertexLayoutSet());

    if (submission.VertexBuffer) {
        CKFF_PROBE(m_Probes, OnVertexBuffers(submission.VertexBuffer, submission.IndexBuffer, submission.VertexLayout));
        {
            CKFF_SCOPE_TIME(m_Probes, BufferBindUs);
            encoder->SetVertexBuffer(0, submission.VertexBuffer, submission.BaseVertex,
                                     submission.VertexCount, submission.VertexLayout);
            if (encoder->GetStatus() != CK_OK)
                return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);
            if (submission.IndexBuffer)
                encoder->SetIndexBuffer(submission.IndexBuffer, submission.StartIndex, submission.IndexCount);
        }
        if (encoder->GetStatus() != CK_OK)
            return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);
        CKFF_PROBE(m_Probes, OnVertexBufferSet());
        if (submission.IndexBuffer)
            CKFF_PROBE(m_Probes, OnIndexBufferSet());
    }

    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        BindTextures(encoder, programContext->Program, textures);
    }
    if (encoder->GetStatus() != CK_OK)
        return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);

    const CKDWORD depth = CKFFEncodeDepthKey(ComputeDepthKey());
    {
        CKFF_SCOPE_TIME(m_Probes, SubmitUs);
        encoder->Submit(submission.View, programContext->Program, depth, SubmitDiscardFlags());
        if (encoder->GetStatus() != CK_OK)
            return RejectPendingSubmission(encoder, CKFF_DRAW_REJECT_ENCODER_ERROR);
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
    const CKFFProgramPreparation &preparation,
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

    const CKFFPreparedState &preparedState = preparation.PreparedState;
    const CKFFProgramContext &programContext = preparation.ProgramContext;
    const CKFFShaderKey &shaderKey = programContext.ShaderKey;
    const CKDWORD program = programContext.Program;
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
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0,
            m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(
            m_State.StageStates[0], m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0,
            m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(
            m_State.StageStates[0],
            preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0,
            m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(
            m_State.StageStates[0], m_State.StageStateSetMasks[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawVertexBufferDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    if (!BuildCurrentTextureBindingSet(
            &textureBindingSet, preparedState.ActiveTextureCount, shaderKey))
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

void CKFixedFunctionPipeline::BindTextures(
    CKRasterizerEncoder *encoder, CKDWORD program,
    const CKFFTextureBindingSet *bindingSet) {
    m_TextureBinder.Bind(encoder, program, bindingSet);
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKFFSubmitDiscardFlags(m_State, m_DrawStateCache);
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    CKFF_PROBE(m_Probes, LogAndReset(m_DrawStateCache, m_ShaderCache));
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
