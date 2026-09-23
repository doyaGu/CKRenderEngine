#include "CKRenderProfile.h"
#include "CKFixedFunctionPipeline.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfClock.h"
#include "CKRenderFrameCostStats.h"
#include "CKFFStateResolver.h"
#include "CKVertexLayoutCache.h"

#include <math.h>
#include <string.h>


CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Features(0),
      m_MaxTextureBindings(0),
      m_FrameNumber(0),
      m_ShaderTargetFlags(0),
      m_TextureBinder(m_State, m_Probes),
      m_UniformEmitter(m_State, m_State.DrawState, m_ShaderTargetFlags, m_Probes),
      m_StaticUniformRevision(1),
      m_DrawValidationCacheValid(FALSE),
      m_DrawValidationCacheFormatFlags(0),
      m_DrawValidationCacheActiveTextureCount(0),
      m_DrawValidationCacheApproximationMask(0),
      m_VertexBufferProgramCacheValid(FALSE),
      m_VertexBufferProgramCacheDPFlags(0),
      m_VertexBufferProgramCacheFormatFlags(0),
      m_VertexBufferProgramCacheActiveTextureCount(0),
      m_SoftwareProgramCacheValid(FALSE),
      m_SoftwareProgramCacheDPFlags(0),
      m_SoftwareProgramCacheFormatFlags(0),
      m_SoftwareProgramCacheActiveTextureCount(0),
      m_SoftwareProgramCachePointSprite(FALSE),
      m_LastDrawRejectReason(CKFF_DRAW_REJECT_NONE),
      m_LastDrawApproximationMask(0),
      m_FrameDrawRejected(FALSE) {
    memset(m_DrawRejectCounts, 0, sizeof(m_DrawRejectCounts));
    memset(m_DrawApproximationCounts, 0, sizeof(m_DrawApproximationCounts));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_Probes.Config.StatsEnabled = settings.Enabled;
    m_Probes.Config.UniformHistEnabled = settings.UniformHistogram;
    m_Probes.Config.StatsInterval = settings.Interval;
#endif
    m_State.Reset();
    memset(&m_Probes.Stats, 0, sizeof(m_Probes.Stats));
    CKFFInitPreparedState(&m_VertexBufferProgramCache.PreparedState);
    CKFFInitProgramContext(&m_VertexBufferProgramCache.ProgramContext,
                           CKFFShaderKey(), CKFFSpecializationInfo());
    memset(m_SoftwareProgramCacheTexcoordComponentCounts, 0,
           sizeof(m_SoftwareProgramCacheTexcoordComponentCounts));
    CKFFInitPreparedState(&m_SoftwareProgramCache.PreparedState);
    CKFFInitProgramContext(&m_SoftwareProgramCache.ProgramContext,
                           CKFFShaderKey(), CKFFSpecializationInfo());
}

CKFixedFunctionPipeline::~CKFixedFunctionPipeline() {
    Shutdown();
}

bool CKFixedFunctionPipeline::Init(uint64_t features,
                                   CKDWORD maxTextureBindings,
                                   CKDWORD shaderTargetFlags) {
    if (Shutdown() != CK_OK)
        return false;
    m_Features = features;
    m_MaxTextureBindings = maxTextureBindings;
    m_ShaderTargetFlags = shaderTargetFlags;
    m_LastDrawRejectReason = CKFF_DRAW_REJECT_NONE;
    m_LastDrawApproximationMask = 0;
    memset(m_DrawApproximationCounts, 0, sizeof(m_DrawApproximationCounts));
    m_FrameDrawRejected = FALSE;
    memset(m_DrawRejectCounts, 0, sizeof(m_DrawRejectCounts));
    m_StaticUniformRevision = 1;
    m_UniformEmitter.ResetCache();
    m_DrawValidationCacheValid = FALSE;
    if ((features & (CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER)) &&
        maxTextureBindings < CKFF_SAMPLER_SLOT_COUNT) {
        Shutdown();
        return false;
    }
    m_State.DrawState.Reset();
    m_TransientGeometry.Clear();
    m_FrameNumber = 0;
    m_State.MarkViewProjectionDirty();
    MarkPreparedProgramDirty();
    return true;
}

CKERROR CKFixedFunctionPipeline::Shutdown() {
    m_TransientGeometry.Clear();
    m_ShaderTargetFlags = 0;
    m_Features = 0;
    m_MaxTextureBindings = 0;
    return CK_OK;
}

const char *CKFFDrawRejectReasonName(CKFFDrawRejectReason reason)
{
    switch (reason) {
    case CKFF_DRAW_REJECT_INVALID_INPUT: return "invalid-input";
    case CKFF_DRAW_REJECT_PREPARE_FAILED: return "prepare-failed";
    case CKFF_DRAW_REJECT_PROGRAM_MISSING: return "program-missing";
    case CKFF_DRAW_REJECT_TEXTURE_OP: return "texture-operation";
    case CKFF_DRAW_REJECT_STATE_VALUE: return "state-value";
    case CKFF_DRAW_REJECT_BACKEND_ERROR: return "backend-error";
    default: return "none";
    }
}

static CKFFDrawRejectReason CKFFProgramPrepareRejectReason(
    CKFFProgramPrepareStatus status)
{
    switch (status) {
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

const char *CKFFDrawApproximationName(CKRST_DIAGNOSTIC code)
{
    switch (code) {
    case CKRST_DIAG_APPROX_FILLMODE_POINT: return "fillmode-point";
    case CKRST_DIAG_APPROX_STENCIL_WRITE_MASK: return "stencil-write-mask";
    case CKRST_DIAG_IGNORE_WRAP: return "texture-wrap";
    case CKRST_DIAG_IGNORE_CLIPPING_OFF: return "clipping-off";
    case CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE: return "vertex-blend-palette";
    case CKRST_DIAG_APPROX_VERTEX_BLEND_WEIGHTS: return "vertex-blend-weights";
    case CKRST_DIAG_APPROX_VERTEX_BLEND_TWEEN: return "vertex-tween-streams";
    case CKRST_DIAG_APPROX_POINT_SIZE: return "point-size";
    case CKRST_DIAG_APPROX_ZBIAS: return "z-bias";
    case CKRST_DIAG_IGNORE_ANTIALIAS: return "edge-antialias";
    case CKRST_DIAG_IGNORE_DITHER: return "dither";
    case CKRST_DIAG_IGNORE_LINEPATTERN: return "line-pattern";
    case CKRST_DIAG_IGNORE_TEXTUREPERSPECTIVE_OFF: return "texture-perspective-off";
    case CKRST_DIAG_IGNORE_SOFTWAREVPROCESSING: return "software-vertex-processing";
    case CKRST_DIAG_APPROX_TEXTURE_OP: return "texture-operation";
    case CKRST_DIAG_APPROX_ALPHA_BUMP_OP: return "alpha-bump-op";
    case CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS: return "bump-texture-format";
    case CKRST_DIAG_APPROX_MIRROR_ONCE: return "mirror-once";
    case CKRST_DIAG_APPROX_BORDER_COLOR: return "border-color-palette";
    case CKRST_DIAG_IGNORE_SAMPLER_LOD: return "sampler-lod-control";
    case CKRST_DIAG_APPROX_ANISOTROPY: return "anisotropy-level";
    case CKRST_DIAG_IGNORE_COMPAREFUNC: return "depth-compare";
    case CKRST_DIAG_APPROX_STAGEBLEND: return "stage-blend";
    case CKRST_DIAG_APPROX_COMPAREFUNC_FILTER: return "filtered-depth-compare";
    case CKRST_DIAG_APPROX_SAMPLER_SLOTS: return "sampler-slots";
    default: return "none";
    }
}

void CKFixedFunctionPipeline::RecordDrawApproximation(CKRST_DIAGNOSTIC code)
{
    if ((CKDWORD)code >= CKRST_DIAG_COUNT)
        return;
    const uint64_t bit = 1ull << (CKDWORD)code;
    if ((m_LastDrawApproximationMask & bit) != 0)
        return; // counted once per draw
    m_LastDrawApproximationMask |= bit;
    CKDWORD &count = m_DrawApproximationCounts[code];
    ++count;
    if (count == 1) {
        CK_LOG_FMT("FFPApprox", "draw approximated: %s code=%u",
                   CKFFDrawApproximationName(code), (unsigned)code);
    }
}

// bgfx has no stencil write mask. A write mask of 0 with writing operations
// becomes KEEP operations; a partial mask writes every bit (spec appendix C).
CKBOOL CKFixedFunctionPipeline::ResolveStencilWrite(CKBOOL *forceKeepOps,
                                                    CKDWORD *effectiveWriteMask) const
{
    const CKDWORD writeMask =
        m_State.DrawState.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK) & 0xffu;
    *forceKeepOps = FALSE;
    if (m_Features & CKRST_DEVCAPS_STENCIL_WRITE_MASK) {
        *effectiveWriteMask = writeMask;
        return FALSE;
    }
    // The backend only knows "write nothing" or "write every bit".
    *effectiveWriteMask = writeMask == 0x00u ? 0x00u : 0xffu;
    if (!m_State.DrawState.GetRenderState(VXRENDERSTATE_STENCILENABLE))
        return FALSE;
    const CKBOOL stencilWrites =
        m_State.DrawState.GetRenderState(VXRENDERSTATE_STENCILFAIL) != VXSTENCILOP_KEEP ||
        m_State.DrawState.GetRenderState(VXRENDERSTATE_STENCILZFAIL) != VXSTENCILOP_KEEP ||
        m_State.DrawState.GetRenderState(VXRENDERSTATE_STENCILPASS) != VXSTENCILOP_KEEP;
    if (!stencilWrites || writeMask == 0xffu)
        return FALSE;
    if (writeMask == 0x00u)
        *forceKeepOps = TRUE;
    return TRUE;
}

static float CKFFClampVertexBufferPointSize(float size)
{
    // bgfx point size is an integer in 1..15.
    const float rounded = floorf(size + 0.5f);
    if (rounded < 1.0f)
        return 1.0f;
    if (rounded > 15.0f)
        return 15.0f;
    return rounded;
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

CKBOOL CKFixedFunctionPipeline::ValidateDrawState(CKDWORD formatFlags,
                                                   CKDWORD activeTextureCount)
{
    if (m_DrawValidationCacheValid &&
        m_DrawValidationCacheFormatFlags == formatFlags &&
        m_DrawValidationCacheActiveTextureCount == activeTextureCount) {
        for (unsigned i = 0; i < CKRST_DIAG_COUNT; ++i) {
            if ((m_DrawValidationCacheApproximationMask & (1ull << i)) != 0)
                RecordDrawApproximation((CKRST_DIAGNOSTIC)i);
        }
        return TRUE;
    }

    if (!CKFFValidDrawStateValues(m_State.DrawState) ||
        (m_State.DrawState.GetColorWriteMask() & ~CKRST_STATE_WRITE_RGBA) != 0) {
        return RecordDrawReject(CKFF_DRAW_REJECT_STATE_VALUE);
    }

    // Render states the backends cannot express are ignored or approximated
    // (spec appendix C) and reported once per draw.
    if (m_State.DrawState.GetRenderState(VXRENDERSTATE_DITHERENABLE))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_DITHER);
    if (m_State.DrawState.GetRenderState(VXRENDERSTATE_ZBIAS) != 0)
        RecordDrawApproximation(CKRST_DIAG_APPROX_ZBIAS);
    if (m_State.DrawState.GetRenderState(VXRENDERSTATE_LINEPATTERN) != 0)
        RecordDrawApproximation(CKRST_DIAG_IGNORE_LINEPATTERN);
    if (m_State.DrawState.GetRenderState(VXRENDERSTATE_EDGEANTIALIAS))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_ANTIALIAS);
    if (!m_State.DrawState.GetRenderState(VXRENDERSTATE_CLIPPING))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_CLIPPING_OFF);
    if (m_State.DrawState.GetRenderState(VXRENDERSTATE_SOFTWAREVPROCESSING))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_SOFTWAREVPROCESSING);
    if (m_State.DrawState.GetRenderState(VXRENDERSTATE_FILLMODE) == VXFILL_POINT)
        RecordDrawApproximation(CKRST_DIAG_APPROX_FILLMODE_POINT);

    CKBOOL forceKeepStencilOps = FALSE;
    CKDWORD effectiveStencilWriteMask = 0;
    if (ResolveStencilWrite(&forceKeepStencilOps, &effectiveStencilWriteMask))
        RecordDrawApproximation(CKRST_DIAG_APPROX_STENCIL_WRITE_MASK);

    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        m_State.DrawState.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        m_State.DrawState.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
        formatFlags);
    if (!vertexBlend.Supported) {
        switch (vertexBlend.UnsupportedReason) {
        case CKFF_VERTEX_BLEND_UNSUPPORTED_INVALID_MODE:
            return RecordDrawReject(CKFF_DRAW_REJECT_STATE_VALUE);
        case CKFF_VERTEX_BLEND_UNSUPPORTED_POSITIONT:
            break; // D3D ignores vertex blending for pre-transformed vertices
        case CKFF_VERTEX_BLEND_UNSUPPORTED_MISSING_TWEEN_POSITION:
        case CKFF_VERTEX_BLEND_UNSUPPORTED_MISSING_TWEEN_NORMAL:
        case CKFF_VERTEX_BLEND_UNSUPPORTED_INDEXED_TWEEN:
            // Renders without tweening (the key resolved the blend mode off).
            RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_TWEEN);
            break;
        default:
            // Missing weights / indices read as zero in the shader.
            RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_WEIGHTS);
            break;
        }
    }
    if (vertexBlend.Indexed && m_State.VertexBlendPaletteOverflow)
        RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE);

    if (activeTextureCount > CKFF_MAX_TEXTURE_STAGES)
        activeTextureCount = CKFF_MAX_TEXTURE_STAGES;
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
            CKDWORD ignoredOp = 0;
            CKDWORD ignoredArg1 = 0;
            CKDWORD ignoredArg2 = 0;
            CKDWORD ignoredAlphaOp = 0;
            CKDWORD ignoredAlphaArg1 = 0;
            CKDWORD ignoredAlphaArg2 = 0;
            if (!CKFFStageBlendToTextureOps(
                m_State.StageStates[stage][CKRST_TSS_STAGEBLEND],
                ignoredOp, ignoredArg1, ignoredArg2,
                ignoredAlphaOp, ignoredAlphaArg1, ignoredAlphaArg2))
                return RecordDrawReject(CKFF_DRAW_REJECT_STATE_VALUE);
        }
        // Unknown operation values are invalid parameters, not approximations.
        if (CKFFClassifyTextureOpCoverage(colorOp) != CKFF_COVERAGE_EXACT)
            return RecordDrawReject(CKFF_DRAW_REJECT_TEXTURE_OP);
        const CKBOOL alphaBumpOp = alphaOp == CKRST_TOP_BUMPENVMAP ||
                                   alphaOp == CKRST_TOP_BUMPENVMAPLUMINANCE;
        if (alphaBumpOp) {
            // Invalid in D3D; the resolver substitutes SELECTARG1 (spec appendix D).
            RecordDrawApproximation(CKRST_DIAG_APPROX_ALPHA_BUMP_OP);
        } else if (CKFFClassifyTextureOpCoverage(alphaOp) != CKFF_COVERAGE_EXACT) {
            return RecordDrawReject(CKFF_DRAW_REJECT_TEXTURE_OP);
        }
        if ((colorOp == CKRST_TOP_BUMPENVMAP ||
             colorOp == CKRST_TOP_BUMPENVMAPLUMINANCE) &&
            (m_State.TextureFlags[stage] & CKRST_TEXTURE_BUMPDUDV) == 0) {
            // Sampled as an ordinary texture (spec appendix D).
            RecordDrawApproximation(CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS);
        }
        if (colorOp == CKRST_TOP_BUMPENVMAPLUMINANCE &&
            (m_State.TextureFlags[stage] & CKRST_TEXTURE_BUMPLUMINANCE) == 0) {
            RecordDrawApproximation(CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS);
        }
        CKFFShaderKeyFSStage shaderStage = {};
        shaderStage.ColorOp = colorOp;
        shaderStage.ColorArg0 = CKFFResolveStageColorArg0(
            m_State.StageStates[stage], stateSetMask);
        shaderStage.ColorArg1 = CKFFResolveStageColorArg1(
            m_State.StageStates[stage], textureBound, stateSetMask);
        shaderStage.ColorArg2 = CKFFResolveStageColorArg2(
            m_State.StageStates[stage], stateSetMask);
        shaderStage.AlphaOp = alphaBumpOp ? (CKDWORD)CKRST_TOP_SELECTARG1 : alphaOp;
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
                // bgfx samplers have no LOD controls (spec appendix D).
                RecordDrawApproximation(CKRST_DIAG_IGNORE_SAMPLER_LOD);
            }
            const CKBOOL anisotropic =
                sampler.MinFilter == CKRST_FILTER_ANISOTROPIC ||
                sampler.MagFilter == CKRST_FILTER_ANISOTROPIC ||
                sampler.MipFilter == CKRST_FILTER_ANISOTROPIC;
            if (anisotropic &&
                m_State.StageStates[stage][CKRST_TSS_MAXANISOTROPY] > 1) {
                // bgfx anisotropy is a switch: any level above one is "on".
                RecordDrawApproximation(CKRST_DIAG_APPROX_ANISOTROPY);
            }
            if (CKFFResolveMirrorOnceAddressMask(m_State.StageStates[stage]) != 0)
                RecordDrawApproximation(CKRST_DIAG_APPROX_MIRROR_ONCE);
            if ((m_State.TextureFlags[stage] & CKRST_TEXTURE_DEPTHSTENCIL) != 0 &&
                m_State.StageStates[stage][CKRST_TSS_COMPAREFUNC] != CKRST_COMPARE_NONE &&
                (sampler.MinFilter != CKRST_FILTER_NEAREST ||
                 sampler.MagFilter != CKRST_FILTER_NEAREST ||
                 (sampler.MipFilter != CKRST_FILTER_NONE &&
                  sampler.MipFilter != CKRST_FILTER_NEAREST &&
                  sampler.MipFilter != CKRST_FILTER_MIPNEAREST))) {
                // The shader compares one filtered depth sample instead of PCF.
                RecordDrawApproximation(CKRST_DIAG_APPROX_COMPAREFUNC_FILTER);
            }
        }
        previousColorOp = colorOp;
        previousAlphaOp = shaderStage.AlphaOp;
    }
    m_DrawValidationCacheFormatFlags = formatFlags;
    m_DrawValidationCacheActiveTextureCount = activeTextureCount;
    m_DrawValidationCacheApproximationMask = m_LastDrawApproximationMask;
    m_DrawValidationCacheValid = TRUE;
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::ValidateVertexBlendIndices(
    const VxDrawPrimitiveData *data,
    CKDWORD formatFlags)
{
    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        m_State.DrawState.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        m_State.DrawState.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
        formatFlags);
    if (!vertexBlend.Supported || !vertexBlend.Indexed ||
        vertexBlend.Mode != CKFF_VERTEX_BLEND_NORMAL) {
        return TRUE;
    }
    if (!data || !data->PositionPtr || data->VertexCount <= 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);

    const CKDWORD weightCount = CKFFVertexLayout::DPFlagsToBlendWeightCount(data->Flags);
    if (weightCount < vertexBlend.Count) {
        // Missing weights read as zero; the last weight takes the remainder.
        RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_WEIGHTS);
    }
    const CKDWORD indexOffset = CKFFVertexLayout::DPFlagsToBlendIndexOffset(data->Flags);
    if (data->PositionStride < indexOffset + sizeof(CKDWORD))
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);

    // Indices beyond the palette clamp to the last matrix in the shader
    // (spec appendix C); scan only to report the approximation.
    const CKDWORD usedIndexCount = vertexBlend.Count + 1;
    for (int vertex = 0; vertex < data->VertexCount; ++vertex) {
        CKDWORD packedIndices = 0;
        const CKBYTE *src = (const CKBYTE *)data->PositionPtr +
                            vertex * data->PositionStride + indexOffset;
        memcpy(&packedIndices, src, sizeof(packedIndices));
        for (CKDWORD slot = 0; slot < usedIndexCount; ++slot) {
            if (((packedIndices >> (slot * 8)) & 0xffu) >=
                CKFF_VERTEX_BLEND_MATRIX_COUNT) {
                RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE);
                return TRUE;
            }
        }
    }
    return TRUE;
}

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::MarkPreparedProgramDirty()
{
    m_VertexBufferProgramCacheValid = FALSE;
    m_SoftwareProgramCacheValid = FALSE;
}

void CKFixedFunctionPipeline::OnFixedFunctionStateChanged(CKDWORD changeMask)
{
    if (changeMask & CKFF_CHANGE_DRAW_VALIDATION)
        m_DrawValidationCacheValid = FALSE;
    if (changeMask & CKFF_CHANGE_STATIC_UNIFORM) {
        ++m_StaticUniformRevision;
        if (m_StaticUniformRevision == 0) {
            m_StaticUniformRevision = 1;
            m_UniformEmitter.ResetCache();
        }
    }
    if (changeMask & CKFF_CHANGE_PROGRAM)
        MarkPreparedProgramDirty();
}

void CKFixedFunctionPipeline::RestoreState(const CKFFStateStore &state)
{
    m_State = state;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

uint64_t CKFixedFunctionPipeline::GetConstantRevision(CKDWORD block) const
{
    return block < CKFF_CONSTANT_SLOT_COUNT ? m_Constants[block].Change : 0;
}

CKBOOL CKFixedFunctionPipeline::BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                                               CKDWORD activeTextureCount,
                                                               const CKFFShaderKey &shaderKey)
{
    if (!bindingSet)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
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
    if (shaderKey.FS.SamplerSlotOverflowMask != 0)
        RecordDrawApproximation(CKRST_DIAG_APPROX_SAMPLER_SLOTS);
    return TRUE;
}

void CKFixedFunctionPipeline::BeginDebugFrame() {
    m_FrameDrawRejected = FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_DebugState.BeginFrame();
    LogAndResetFrameStats();
#endif
}

// ============================================================================
// Drawing
// ============================================================================

CKFFProgramPrepareStatus CKFixedFunctionPipeline::PrepareProgram(
    CKFFProgramPreparation *preparation,
    CKDWORD dpFlags,
    CKDWORD activeTextureCount,
    CKDWORD formatFlags,
    const CKBYTE *texcoordComponentCounts,
    CKBOOL pointSprite)
{
    CKRE_PROFILE_SCOPE("CKRE.FFP.PrepareProgram");
    if (!preparation)
        return CKFF_PROGRAM_PREPARE_INVALID_INPUT;

    {
        CKFF_SCOPE_TIME(m_Probes, StateUs);
        CKFFStateResolver::BuildPreparedState(
            m_State, m_State.DrawState, &preparation->PreparedState,
            dpFlags, activeTextureCount, formatFlags,
            texcoordComponentCounts, pointSprite);
    }

    const CKFFShaderKey shaderKey =
        CKFFBuildShaderKeyFromPreparedState(&preparation->PreparedState);
    CKFFInitProgramContext(
        &preparation->ProgramContext, shaderKey,
        CKFFBuildSpecializationInfo(shaderKey.FS));
    return CKFF_PROGRAM_PREPARE_OK;
}

CKFFProgramPrepareStatus CKFixedFunctionPipeline::PrepareVertexBufferProgram(
    CKFFProgramPreparation *preparation,
    CKDWORD dpFlags,
    CKDWORD formatFlags)
{
    if (!preparation)
        return CKFF_PROGRAM_PREPARE_INVALID_INPUT;

    const CKDWORD activeTextureCount =
        (CKDWORD)CKFFResolveActiveTextureStageCount(
            m_State.TextureHandles, m_State.StageStates);
    if (m_VertexBufferProgramCacheValid &&
        m_VertexBufferProgramCacheDPFlags == dpFlags &&
        m_VertexBufferProgramCacheFormatFlags == formatFlags &&
        m_VertexBufferProgramCacheActiveTextureCount == activeTextureCount) {
        *preparation = m_VertexBufferProgramCache;
        return CKFF_PROGRAM_PREPARE_OK;
    }

    const CKFFProgramPrepareStatus status = PrepareProgram(
        preparation, dpFlags, activeTextureCount, formatFlags);
    if (status == CKFF_PROGRAM_PREPARE_OK) {
        m_VertexBufferProgramCacheDPFlags = dpFlags;
        m_VertexBufferProgramCacheFormatFlags = formatFlags;
        m_VertexBufferProgramCacheActiveTextureCount = activeTextureCount;
        m_VertexBufferProgramCache = *preparation;
        m_VertexBufferProgramCacheValid = TRUE;
    }
    return status;
}

CKFFProgramPrepareStatus CKFixedFunctionPipeline::PrepareSoftwareProgram(
    CKFFProgramPreparation *preparation,
    CKDWORD dpFlags,
    CKDWORD activeTextureCount,
    CKDWORD formatFlags,
    const CKBYTE *texcoordComponentCounts,
    CKBOOL pointSprite)
{
    if (!preparation || !texcoordComponentCounts)
        return CKFF_PROGRAM_PREPARE_INVALID_INPUT;

    if (m_SoftwareProgramCacheValid &&
        m_SoftwareProgramCacheDPFlags == dpFlags &&
        m_SoftwareProgramCacheFormatFlags == formatFlags &&
        m_SoftwareProgramCacheActiveTextureCount == activeTextureCount &&
        m_SoftwareProgramCachePointSprite == pointSprite &&
        memcmp(m_SoftwareProgramCacheTexcoordComponentCounts,
               texcoordComponentCounts,
               sizeof(m_SoftwareProgramCacheTexcoordComponentCounts)) == 0) {
        *preparation = m_SoftwareProgramCache;
        return CKFF_PROGRAM_PREPARE_OK;
    }

    const CKFFProgramPrepareStatus status = PrepareProgram(
        preparation, dpFlags, activeTextureCount, formatFlags,
        texcoordComponentCounts, pointSprite);
    if (status == CKFF_PROGRAM_PREPARE_OK) {
        m_SoftwareProgramCacheDPFlags = dpFlags;
        m_SoftwareProgramCacheFormatFlags = formatFlags;
        m_SoftwareProgramCacheActiveTextureCount = activeTextureCount;
        m_SoftwareProgramCachePointSprite = pointSprite;
        memcpy(m_SoftwareProgramCacheTexcoordComponentCounts,
               texcoordComponentCounts,
               sizeof(m_SoftwareProgramCacheTexcoordComponentCounts));
        m_SoftwareProgramCache = *preparation;
        m_SoftwareProgramCacheValid = TRUE;
    }
    return status;
}

CKBOOL CKFixedFunctionPipeline::PreparePrimitive(
    VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
    VxDrawPrimitiveData *data)
{
    CKRE_PROFILE_SCOPE("CKRE.FFP.DrawPrimitive");
    BeginDrawDiagnostics();
    if (!data || data->VertexCount <= 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    CKFF_PROBE(m_Probes, OnSoftwareDraw());

    const CKDWORD formatFlags =
        CKFFVertexLayout::DrawPrimitiveDataToFormatFlags(data);
    const CKBOOL pointSprites = type == VX_POINTLIST &&
        m_State.DrawState.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE) != 0;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial() : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
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
        wrapModes[stage] = m_State.DrawState.GetRenderState(
            (VXRENDERSTATETYPE)(VXRENDERSTATE_WRAP0 + stage));
        if ((formatFlags & CKFF_VF_TEXCOORD(stage)) == 0)
            continue;
        const void *texcoord = stage == 0
            ? data->TexCoordPtr
            : data->TexCoordPtrs[stage - 1];
        if ((wrapModes[stage] & VXWRAP_MASK) != 0 && texcoord)
            wrapsTexcoords = TRUE;
    }
    CKFFPointSpriteParams pointParams;
    pointParams.Size = CKFFReadFloatRenderState(m_State.DrawState, VXRENDERSTATE_POINTSIZE, 1.0f);
    pointParams.MinSize = CKFFReadFloatRenderState(m_State.DrawState, VXRENDERSTATE_POINTSIZE_MIN, 1.0f);
    pointParams.MaxSize = CKFFReadFloatRenderState(m_State.DrawState, VXRENDERSTATE_POINTSIZE_MAX, 64.0f);
    pointParams.ScaleEnable = m_State.DrawState.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE);
    pointParams.ScaleA = CKFFReadFloatRenderState(m_State.DrawState, VXRENDERSTATE_POINTSCALE_A, 1.0f);
    pointParams.ScaleB = CKFFReadFloatRenderState(m_State.DrawState, VXRENDERSTATE_POINTSCALE_B, 0.0f);
    pointParams.ScaleC = CKFFReadFloatRenderState(m_State.DrawState, VXRENDERSTATE_POINTSCALE_C, 0.0f);
    pointParams.World = m_State.World;
    pointParams.View = m_State.View;
    pointParams.Projection = m_State.Projection;
    pointParams.ViewportWidth = m_State.Viewport[0] != 0.0f
        ? fabsf(2.0f / m_State.Viewport[0]) : 1.0f;
    pointParams.ViewportHeight = m_State.Viewport[1] != 0.0f
        ? fabsf(2.0f / m_State.Viewport[1]) : 1.0f;
    CKFFProgramPreparation programPreparation;
    const CKFFProgramPrepareStatus prepareStatus = PrepareSoftwareProgram(
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
    const CKDWORD program = 0;
    CKFF_PROBE(m_Probes, OnProgram(program));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
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
        debugInfo.DrawState = &m_State.DrawState;
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
            type, indices, indexCount, data, wrapModes[0],
            pointSprites, &pointParams,
            m_State.TexcoordComponentCounts, wrapModes);
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

    VXPRIMITIVETYPE drawStateType = type;
    if (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
        type == VX_POINTLIST) {
        drawStateType = VX_TRIANGLELIST;
    } else if (type == VX_LINESTRIP && wrapsTexcoords) {
        drawStateType = VX_LINELIST;
    }
    CKFFDrawSubmission submission = {};
    submission.DrawStateType = drawStateType;
    submission.ProgramContext = &programContext;
    submission.Textures = &textureBindingSet;
    submission.VertexFormat = formatFlags;
    submission.Source = CKFF_DRAW_PRIMITIVE;
    return PrepareDraw(submission);
}

CKBOOL CKFixedFunctionPipeline::PrepareVertexBuffer(
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout, const CKWORD *indices)
{
    CKRE_PROFILE_SCOPE("CKRE.FFP.DrawVertexBuffer");
    BeginDrawDiagnostics();
    if (!vb || vertexCount == 0 ||
        (ib && indices) || (indices && indexCount == 0))
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureStageCount(
        m_State.TextureHandles, m_State.StageStates);
    if (!ValidateDrawState(formatFlags, activeTextureCount))
        return FALSE;
    if (m_State.DrawState.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) &&
        m_State.DrawState.GetRenderState(VXRENDERSTATE_VERTEXBLEND) != VXVBLEND_DISABLE) {
        // Indices inside a backend buffer cannot be validated; the shader clamps
        // them to the palette (spec appendix C).
        RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE);
    }
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if ((m_State.DrawState.GetRenderState(
                 (VXRENDERSTATETYPE)(VXRENDERSTATE_WRAP0 + stage)) & VXWRAP_MASK) != 0) {
            // Texture wrap only applies to CPU-interleaved primitives.
            RecordDrawApproximation(CKRST_DIAG_IGNORE_WRAP);
            break;
        }
    }
    if (type == VX_POINTLIST) {
        // Device-buffer points render as plain points of the clamped constant
        // size; sprites, scaling and per-vertex sizes are not applied.
        const float pointSize = CKFFResolveConstantPointSize(m_State.DrawState);
        if (m_State.DrawState.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE) ||
            m_State.DrawState.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE) ||
            (dpFlags & CKRST_DP_PSIZE) != 0 ||
            CKFFClampVertexBufferPointSize(pointSize) != pointSize) {
            RecordDrawApproximation(CKRST_DIAG_APPROX_POINT_SIZE);
        }
    }
    CKFFProgramPreparation preparation;
    const CKFFProgramPrepareStatus prepareStatus =
        PrepareVertexBufferProgram(
            &preparation, dpFlags, formatFlags);
    if (prepareStatus != CKFF_PROGRAM_PREPARE_OK) {
        if (prepareStatus == CKFF_PROGRAM_PREPARE_PROGRAM_MISSING)
            CKFF_PROBE(m_Probes, OnProgramMiss());
        return RecordDrawReject(CKFFProgramPrepareRejectReason(prepareStatus));
    }
    return PrepareVertexBufferImmediate(preparation, type, vb, ib,
                                        baseVertex, vertexCount, startIndex, indexCount,
                                        dpFlags, formatFlags, vertexLayout, indices);
}

// ============================================================================
// Internal methods
// ============================================================================

CKBOOL CKFixedFunctionPipeline::PrepareDraw(const CKFFDrawSubmission &submission)
{
    CKRE_PROFILE_SCOPE("CKRE.FFP.PrepareDraw");
    const CKFFProgramContext *programContext = submission.ProgramContext;
    const CKFFTextureBindingSet *textures = submission.Textures;
    if (!programContext || !textures)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);

    // Constant blocks. A refused upload aborts the draw (the backend drops
    // its pending state).
    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        if (!m_UniformEmitter.UploadUniforms(
                &m_Constants, programContext, textures->ActiveStageCount,
                m_StaticUniformRevision))
            return RecordDrawReject(CKFF_DRAW_REJECT_BACKEND_ERROR);
    }
    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));

    // Pipeline state.
    CKFFPipelineState pipeline;
    {
        CKFF_SCOPE_TIME(m_Probes, DrawStateBuildUs);
        pipeline.State = m_State.DrawState.BuildDrawState(submission.DrawStateType);
    }
    CKFF_PROBE(m_Probes, OnDrawState(pipeline.State));
    // Stencil reference and masks are DWORD render states of which the 8-bit
    // stencil buffer uses the low byte (D3D7 semantics).
    pipeline.StencilRef = m_State.DrawState.GetRenderState(VXRENDERSTATE_STENCILREF) & 0xffu;
    pipeline.StencilReadMask = m_State.DrawState.GetRenderState(VXRENDERSTATE_STENCILMASK) & 0xffu;
    CKBOOL forceKeepStencilOps = FALSE;
    CKDWORD stencilWriteMask = 0xffu;
    ResolveStencilWrite(&forceKeepStencilOps, &stencilWriteMask);
    pipeline.StencilWriteMask = stencilWriteMask;
    if (forceKeepStencilOps) {
        pipeline.State.Mid &= ~(CKRST_STENCIL_FAIL(0xF) | CKRST_STENCIL_ZFAIL(0xF) | CKRST_STENCIL_PASS(0xF));
        pipeline.State.Mid |= CKRST_STENCIL_FAIL(VXSTENCILOP_KEEP) |
                              CKRST_STENCIL_ZFAIL(VXSTENCILOP_KEEP) |
                              CKRST_STENCIL_PASS(VXSTENCILOP_KEEP);
    }
    pipeline.ScissorEnabled = m_State.ScissorEnabled;
    pipeline.Scissor = m_State.Scissor;
    pipeline.PointSize = submission.DrawStateType == VX_POINTLIST
        ? CKFFClampVertexBufferPointSize(CKFFResolveConstantPointSize(m_State.DrawState))
        : 1.0f;
    m_Draw = CKFFDraw();
    m_Draw.Pipeline = pipeline;
    m_Draw.Constants = &m_Constants;
    m_Draw.Marker = m_DrawMarker;
    m_Draw.ShaderKey = programContext->ShaderKey;
    m_Draw.Specialization = programContext->Specialization;
    m_Draw.Source = submission.Source;
    if (submission.VertexLayout)
        CKFF_PROBE(m_Probes, OnVertexLayoutSet());
    if (submission.VertexBuffer) {
        CKFF_PROBE(m_Probes, OnVertexBuffers(submission.VertexBuffer, submission.IndexBuffer, submission.VertexLayout));
        m_Draw.VertexFormat = submission.VertexFormat;
        m_Draw.VertexBuffer = submission.VertexBuffer;
        m_Draw.StartVertex = submission.BaseVertex;
        m_Draw.VertexCount = submission.VertexCount;
        m_Draw.IndexBuffer = submission.IndexBuffer;
        m_Draw.Indices = submission.Indices;
        m_Draw.Index32 = submission.Index32;
        m_Draw.StartIndex = submission.StartIndex;
        m_Draw.IndexCount = (submission.IndexBuffer || submission.Indices)
            ? submission.IndexCount : 0;
        CKFF_PROBE(m_Probes, OnVertexBufferSet());
        if (submission.IndexBuffer || submission.Indices)
            CKFF_PROBE(m_Probes, OnIndexBufferSet());
    } else {
        const CKDWORD formatFlags = m_TransientGeometry.GetFormatFlags();
        m_Draw.VertexFormat = formatFlags;
        m_Draw.Vertices = m_TransientGeometry.GetVertices();
        m_Draw.VertexStride = m_TransientGeometry.GetVertexStride();
        m_Draw.VertexCount = m_TransientGeometry.GetVertexCount();
        m_Draw.Indices = m_TransientGeometry.GetIndices();
        m_Draw.Index32 = m_TransientGeometry.IsIndex32();
        m_Draw.IndexCount = m_TransientGeometry.GetIndexCount();
    }

    // Textures.
    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        m_Draw.Textures = *textures;
        CKDWORD desiredTextures[CKFF_MAX_TEXTURE_STAGES] = {};
        for (CKDWORD i = 0; i < textures->ActiveTextureCount; ++i) {
            desiredTextures[i] = textures->Bindings[i].Texture;
            if (textures->Bindings[i].Texture)
                CKFF_PROBE(m_Probes, OnTextureBind());
        }
        CKFF_PROBE(m_Probes, OnTextureSet(textures->ActiveTextureCount, desiredTextures));
    }

    m_Draw.SortKey = CKFFEncodeDepthKey(ComputeDepthKey());
    m_LastDrawRejectReason = CKFF_DRAW_REJECT_NONE;
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::FinishDraw(CKERROR error, uint64_t approximationMask)
{
    if (error != CK_OK)
        return RecordDrawReject(CKFF_DRAW_REJECT_BACKEND_ERROR);
    for (unsigned i = 0; i < CKRST_DIAG_COUNT; ++i) {
        if ((approximationMask & (1ull << i)) != 0)
            RecordDrawApproximation((CKRST_DIAGNOSTIC)i);
    }
    if (m_Draw.Source == CKFF_DRAW_PRIMITIVE)
        CK_FRAME_COST_ADD_PRIMITIVE_SUBMIT();
    else
        CK_FRAME_COST_ADD_MESH_SUBMIT();
    CK_FRAME_COST_ADD_SUBMITTED_DRAW();
    CKFF_PROBE(m_Probes, OnSubmittedDraw());
    m_LastDrawRejectReason = CKFF_DRAW_REJECT_NONE;
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::PrepareVertexBufferImmediate(
    const CKFFProgramPreparation &preparation,
    VXPRIMITIVETYPE type,
    CKDWORD vb,
    CKDWORD ib,
    CKDWORD baseVertex,
    CKDWORD vertexCount,
    CKDWORD startIndex,
    CKDWORD indexCount,
    CKDWORD dpFlags,
    CKDWORD formatFlags,
    CKDWORD vertexLayout,
    const CKWORD *indices)
{
    CKRE_PROFILE_SCOPE("CKRE.FFP.SubmitVertexBuffer");
    if (!vb)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    CKFF_PROBE(m_Probes, OnHardwareDraw());
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial() : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
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
    const CKDWORD program = 0;
    CKFF_PROBE(m_Probes, OnProgram(program));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
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
        debugInfo.DrawState = &m_State.DrawState;
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

    if (indices) {
        m_ImmediateIndices.Resize(indexCount);
        memcpy(m_ImmediateIndices.Begin(), indices,
               (size_t)indexCount * sizeof(CKWORD));
        CKFF_PROBE(m_Probes, OnTransientGeometry(
            0, indexCount * (CKDWORD)sizeof(CKWORD)));
    } else {
        m_ImmediateIndices.Clear();
    }

    CKFFDrawSubmission submission = {};
    submission.DrawStateType = type;
    submission.ProgramContext = &programContext;
    submission.Textures = &textureBindingSet;
    submission.VertexBuffer = vb;
    submission.IndexBuffer = ib;
    submission.Indices = indices ? (const CKBYTE *)m_ImmediateIndices.Begin() : NULL;
    submission.Index32 = FALSE;
    submission.BaseVertex = baseVertex;
    submission.VertexCount = vertexCount;
    submission.StartIndex = startIndex;
    submission.IndexCount = indexCount;
    submission.VertexLayout = vertexLayout;
    submission.VertexFormat = formatFlags;
    submission.Source = CKFF_DRAW_VERTEX_BUFFER;
    return PrepareDraw(submission);
}



void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    CKFF_PROBE(m_Probes, LogAndReset(m_State.DrawState));
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    return m_TextureBinder.BuildSamplerDesc(stage);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    return CKFFComputeDepthKey(m_State, m_State.DrawState);
}
