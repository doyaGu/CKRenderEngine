#include "CKFixedFunctionPipeline.h"
#include "CKRasterizerBackend.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfClock.h"
#include "CKRenderFrameCostStats.h"

#include <math.h>
#include <string.h>


CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Backend(nullptr),
#if CKRE_ENABLE_FFP_DIAGNOSTICS
      m_DrawPreparer(m_State, m_DrawStateCache, m_ShaderCache, m_Probes),
      m_TextureBinder(m_State, m_ShaderCache, m_Probes),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache, m_Probes),
#else
      m_DrawPreparer(m_State, m_DrawStateCache, m_ShaderCache),
      m_TextureBinder(m_State, m_ShaderCache),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache),
#endif
      m_FrameNumber(0), m_LastDrawRejectReason(CKFF_DRAW_REJECT_NONE),
      m_LastDrawApproximationMask(0),
      m_FrameDrawRejected(FALSE),
      m_BorderPaletteCount(0), m_BorderPaletteFrameSerial((CKDWORD)-1) {
    memset(m_DrawRejectCounts, 0, sizeof(m_DrawRejectCounts));
    memset(m_DrawApproximationCounts, 0, sizeof(m_DrawApproximationCounts));
    memset(m_BorderPaletteColors, 0, sizeof(m_BorderPaletteColors));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_Probes.Config.StatsEnabled = settings.Enabled;
    m_Probes.Config.UniformHistEnabled = settings.UniformHistogram;
    m_Probes.Config.StatsInterval = settings.Interval;
#endif
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

bool CKFixedFunctionPipeline::Init(CKRasterizerBackend *backend) {
    if (Shutdown() != CK_OK)
        return false;
    m_Backend = backend;
    m_LastDrawRejectReason = CKFF_DRAW_REJECT_NONE;
    m_LastDrawApproximationMask = 0;
    memset(m_DrawApproximationCounts, 0, sizeof(m_DrawApproximationCounts));
    m_FrameDrawRejected = FALSE;
    memset(m_DrawRejectCounts, 0, sizeof(m_DrawRejectCounts));
    m_BorderPaletteCount = 0;
    m_BorderPaletteFrameSerial = (CKDWORD)-1;
    memset(m_BorderPaletteColors, 0, sizeof(m_BorderPaletteColors));
    if (!backend)
        return false;
    const CKBackendCaps &caps = backend->GetCaps();
    // A backend without programmable shaders (the NULL backend) records the
    // fixed-function state but has no programs to build.
    const CKBOOL shaderBackend =
        (caps.Features & (CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER)) ==
            (CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER)
        ? TRUE : FALSE;
    if (shaderBackend && caps.MaxTextureBindings < CKFF_SAMPLER_SLOT_COUNT) {
        Shutdown();
        return false;
    }
    if (shaderBackend && !m_ShaderCache.Init(backend)) {
        Shutdown();
        return false;
    }
    m_DrawStateCache.Reset();
    m_VertexLayoutCache.Init(backend);
    m_TextureBinder.ResetProgramBindings();
    m_TransientGeometry.Init(backend, &m_VertexLayoutCache);
    m_FrameNumber = 0;
    m_State.MarkViewProjectionDirty();
    MarkPreparedProgramDirty();
    return true;
}

CKERROR CKFixedFunctionPipeline::Shutdown() {
    const CKERROR status = PrepareShutdown();
    if (status != CK_OK)
        return status;
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_TextureBinder.ResetProgramBindings();
    m_ShaderCache.Shutdown();
    m_Backend = nullptr;
    return CK_OK;
}

CKERROR CKFixedFunctionPipeline::PrepareShutdown() {
    // The frame flow (translated context) must have ended its frame.
    return m_Backend && !m_Backend->IsIdle() ? CKERR_INVALIDOPERATION : CK_OK;
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
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK) & 0xffu;
    *forceKeepOps = FALSE;
    // The backend only knows "write nothing" or "write every bit".
    *effectiveWriteMask = writeMask == 0x00u ? 0x00u : 0xffu;
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILENABLE))
        return FALSE;
    const CKBOOL stencilWrites =
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILFAIL) != VXSTENCILOP_KEEP ||
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILZFAIL) != VXSTENCILOP_KEEP ||
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILPASS) != VXSTENCILOP_KEEP;
    if (!stencilWrites || writeMask == 0xffu)
        return FALSE;
    if (writeMask == 0x00u)
        *forceKeepOps = TRUE;
    return TRUE;
}

CKDWORD CKFixedFunctionPipeline::NearestBorderPaletteSlot(CKDWORD argb) const
{
    CKDWORD best = 0;
    CKDWORD bestDistance = 0xFFFFFFFFu;
    for (CKDWORD slot = 0; slot < m_BorderPaletteCount; ++slot) {
        const CKDWORD other = m_BorderPaletteColors[slot];
        CKDWORD distance = 0;
        for (CKDWORD shift = 0; shift < 32; shift += 8) {
            const int a = (int)((argb >> shift) & 0xffu);
            const int b = (int)((other >> shift) & 0xffu);
            distance += (CKDWORD)((a - b) * (a - b));
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            best = slot;
        }
    }
    return best;
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
    if (!CKFFValidDrawStateValues(m_DrawStateCache) ||
        (m_DrawStateCache.GetColorWriteMask() & ~CKRST_STATE_WRITE_RGBA) != 0) {
        return RecordDrawReject(CKFF_DRAW_REJECT_STATE_VALUE);
    }

    // Render states the backends cannot express are ignored or approximated
    // (spec appendix C) and reported once per draw.
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_DITHERENABLE))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_DITHER);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZBIAS) != 0)
        RecordDrawApproximation(CKRST_DIAG_APPROX_ZBIAS);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_LINEPATTERN) != 0)
        RecordDrawApproximation(CKRST_DIAG_IGNORE_LINEPATTERN);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_EDGEANTIALIAS))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_ANTIALIAS);
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPING))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_CLIPPING_OFF);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_SOFTWAREVPROCESSING))
        RecordDrawApproximation(CKRST_DIAG_IGNORE_SOFTWAREVPROCESSING);
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_FILLMODE) == VXFILL_POINT)
        RecordDrawApproximation(CKRST_DIAG_APPROX_FILLMODE_POINT);

    CKBOOL forceKeepStencilOps = FALSE;
    CKDWORD effectiveStencilWriteMask = 0;
    if (ResolveStencilWrite(&forceKeepStencilOps, &effectiveStencilWriteMask))
        RecordDrawApproximation(CKRST_DIAG_APPROX_STENCIL_WRITE_MASK);

    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
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
    const CKBOOL perspectiveTexture =
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE) != 0;
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
            CKBOOL exact = TRUE;
            CKFFStageBlendToTextureOps(
                m_State.StageStates[stage][CKRST_TSS_STAGEBLEND],
                ignoredOp, ignoredArg1, ignoredArg2,
                ignoredAlphaOp, ignoredAlphaArg1, ignoredAlphaArg2, &exact);
            if (!exact)
                RecordDrawApproximation(CKRST_DIAG_APPROX_STAGEBLEND);
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
            if (!perspectiveTexture)
                RecordDrawApproximation(CKRST_DIAG_IGNORE_TEXTUREPERSPECTIVE_OFF);
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
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);

    const CKDWORD weightCount = CKVertexLayoutCache::DPFlagsToBlendWeightCount(data->Flags);
    if (weightCount < vertexBlend.Count) {
        // Missing weights read as zero; the last weight takes the remainder.
        RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_WEIGHTS);
    }
    const CKDWORD indexOffset = CKVertexLayoutCache::DPFlagsToBlendIndexOffset(data->Flags);
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
    m_DrawPreparer.InvalidateVertexBufferProgramCache();
}

void CKFixedFunctionPipeline::OnFixedFunctionStateChanged(CKDWORD changeMask)
{
    if (changeMask & CKFF_CHANGE_PROGRAM)
        MarkPreparedProgramDirty();
}

CKBOOL CKFixedFunctionPipeline::BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                                               CKDWORD activeTextureCount,
                                                               const CKFFShaderKey &shaderKey)
{
    if (!bindingSet || !m_Backend)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    const CKDWORD frameSerial = m_FrameNumber;
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
    if (shaderKey.FS.SamplerSlotOverflowMask != 0)
        RecordDrawApproximation(CKRST_DIAG_APPROX_SAMPLER_SLOTS);
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
            if (m_BorderPaletteCount >= 16) {
                // bgfx has 16 palette entries per frame: reuse the nearest
                // colour (spec appendix D).
                slot = NearestBorderPaletteSlot(argb);
                RecordDrawApproximation(CKRST_DIAG_APPROX_BORDER_COLOR);
            } else {
                m_BorderPaletteColors[slot] = argb;
                ++m_BorderPaletteCount;
                const CKDWORD rgba = ((argb >> 16) & 0xffu) << 24 |
                                     ((argb >> 8) & 0xffu) << 16 |
                                     (argb & 0xffu) << 8 |
                                     ((argb >> 24) & 0xffu);
                m_Backend->SetPaletteColor(slot, rgba);
            }
        }
        sampler.BorderColor = slot;
    }
    bindingSet->Hash = CKFFHashTextureBindingSet(
        bindingSet->ActiveTextureCount, bindingSet->Bindings);
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

CKBOOL CKFixedFunctionPipeline::DrawPrimitive(
    VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
    VxDrawPrimitiveData *data)
{
    BeginDrawDiagnostics();
    if (!m_Backend || !data || data->VertexCount == 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    CKFF_PROBE(m_Probes, OnSoftwareDraw());

    const CKDWORD formatFlags =
        CKVertexLayoutCache::DrawPrimitiveDataToFormatFlags(data);
    const CKBOOL pointSprites = type == VX_POINTLIST &&
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE) != 0;
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
    submission.Source = CKFF_SUBMIT_PRIMITIVE;
    return SubmitPrepared(submission);
}

CKBOOL CKFixedFunctionPipeline::DrawVertexBuffer(
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    BeginDrawDiagnostics();
    if (!m_Backend || !vb || vertexCount == 0)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureStageCount(
        m_State.TextureHandles, m_State.StageStates);
    if (!ValidateDrawState(formatFlags, activeTextureCount))
        return FALSE;
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) &&
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND) != VXVBLEND_DISABLE) {
        // Indices inside a backend buffer cannot be validated; the shader clamps
        // them to the palette (spec appendix C).
        RecordDrawApproximation(CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE);
    }
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if ((m_DrawStateCache.GetRenderState(
                 (VXRENDERSTATETYPE)(VXRENDERSTATE_WRAP0 + stage)) & VXWRAP_MASK) != 0) {
            // Texture wrap only applies to CPU-interleaved primitives.
            RecordDrawApproximation(CKRST_DIAG_IGNORE_WRAP);
            break;
        }
    }
    if (type == VX_POINTLIST) {
        // Device-buffer points render as plain points of the clamped constant
        // size; sprites, scaling and per-vertex sizes are not applied.
        const float pointSize = CKFFResolveConstantPointSize(m_DrawStateCache);
        if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE) ||
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE) ||
            (dpFlags & CKRST_DP_PSIZE) != 0 ||
            CKFFClampVertexBufferPointSize(pointSize) != pointSize) {
            RecordDrawApproximation(CKRST_DIAG_APPROX_POINT_SIZE);
        }
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
    return SubmitVertexBufferImmediate(preparation, type, vb, ib,
                                       baseVertex, vertexCount, startIndex, indexCount,
                                       dpFlags, formatFlags, vertexLayout);
}

// ============================================================================
// Internal methods
// ============================================================================

CKBOOL CKFixedFunctionPipeline::SubmitPrepared(const CKFFDrawSubmission &submission)
{
    const CKFFProgramContext *programContext = submission.ProgramContext;
    const CKFFTextureBindingSet *textures = submission.Textures;
    if (!m_Backend || !programContext || !textures || !programContext->Program)
        return RecordDrawReject(CKFF_DRAW_REJECT_INVALID_INPUT);

    // Constant blocks. A refused upload aborts the draw (the backend drops
    // its pending state).
    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        if (!m_UniformEmitter.UploadUniforms(m_Backend, programContext, textures->ActiveStageCount))
            return RecordDrawReject(CKFF_DRAW_REJECT_BACKEND_ERROR);
    }
    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));

    // Pipeline state.
    CKBackendPipelineState pipeline;
    {
        CKFF_SCOPE_TIME(m_Probes, DrawStateBuildUs);
        pipeline.State = m_DrawStateCache.BuildDrawState(submission.DrawStateType);
    }
    CKFF_PROBE(m_Probes, OnDrawState(pipeline.State));
    // Stencil reference and masks are DWORD render states of which the 8-bit
    // stencil buffer uses the low byte (D3D7 semantics).
    pipeline.StencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF) & 0xffu;
    pipeline.StencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK) & 0xffu;
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
        ? CKFFClampVertexBufferPointSize(CKFFResolveConstantPointSize(m_DrawStateCache))
        : 1.0f;
    {
        CKFF_SCOPE_TIME(m_Probes, PipelineStateUs);
        m_Backend->SetPipelineState(&pipeline);
    }

    // Geometry.
    CKBackendDraw draw;
    draw.Program = programContext->Program;
    if (submission.VertexLayout)
        CKFF_PROBE(m_Probes, OnVertexLayoutSet());
    if (submission.VertexBuffer) {
        CKFF_PROBE(m_Probes, OnVertexBuffers(submission.VertexBuffer, submission.IndexBuffer, submission.VertexLayout));
        draw.Layout = submission.VertexLayout;
        draw.VertexBuffer = submission.VertexBuffer;
        draw.StartVertex = submission.BaseVertex;
        draw.VertexCount = submission.VertexCount;
        draw.IndexBuffer = submission.IndexBuffer;
        draw.StartIndex = submission.StartIndex;
        draw.IndexCount = submission.IndexBuffer ? submission.IndexCount : 0;
        CKFF_PROBE(m_Probes, OnVertexBufferSet());
        if (submission.IndexBuffer)
            CKFF_PROBE(m_Probes, OnIndexBufferSet());
    } else {
        const CKBackendTransientVertices *vertices = m_TransientGeometry.GetVertices();
        const CKBackendTransientIndices *indices = m_TransientGeometry.GetIndices();
        draw.Layout = m_TransientGeometry.GetLayoutHandle();
        draw.TransientVertices = vertices;
        draw.VertexCount = vertices->Count;
        draw.TransientIndices = indices;
        draw.IndexCount = indices ? indices->Count : 0;
    }

    // Textures.
    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        BindTextures(programContext->Program, textures);
    }

    draw.SortKey = CKFFEncodeDepthKey(ComputeDepthKey());
    {
        CKFF_SCOPE_TIME(m_Probes, SubmitUs);
        if (m_Backend->Draw(&draw) != CK_OK)
            return RecordDrawReject(CKFF_DRAW_REJECT_BACKEND_ERROR);
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
    CKDWORD vertexLayout)
{
    if (!m_Backend || !vb)
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
    const CKDWORD program = programContext.Program;
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
    return SubmitPrepared(submission);
}

void CKFixedFunctionPipeline::BindTextures(CKDWORD program, const CKFFTextureBindingSet *bindingSet) {
    m_TextureBinder.Bind(m_Backend, program, bindingSet);
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    CKFF_PROBE(m_Probes, LogAndReset(m_DrawStateCache));
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    return m_TextureBinder.BuildSamplerDesc(stage);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    return CKFFComputeDepthKey(m_State, m_DrawStateCache);
}

