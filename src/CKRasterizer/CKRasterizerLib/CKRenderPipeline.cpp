#include "CKRenderPipeline.h"
#include "CKRasterizerDevice.h"
#include "CKDebugLogger.h"
#include "CKRenderPerfClock.h"
#include "CKRenderSettings.h"

#include <stdlib.h>
#include <string.h>

static float ParseRenderScale(const char *value, float fallback)
{
    if (!value || value[0] == '\0')
        return fallback;
    char *end = nullptr;
    const float parsed = (float)strtod(value, &end);
    if (end == value)
        return fallback;
    return parsed;
}

float CKRenderPipelineClampRenderScale(float scale)
{
    return CKPostprocessPass::ClampRenderScale(scale);
}

float CKRenderPipelineClampSharpness(float sharpness)
{
    return CKPostprocessPass::ClampSharpness(sharpness);
}

CKRenderPipelineConfig CKRenderPipelineConfigFromSettings()
{
    CKRenderPipelineConfig config;
    config.FXAA = CKRenderRootSettings().GetBool("FXAA", false) ? TRUE : FALSE;

    XString renderScale;
    if (CKRenderRootSettings().GetString("RenderScale", renderScale))
        config.RenderScale = CKRenderPipelineClampRenderScale(ParseRenderScale(renderScale.CStr(), 1.0f));
    else
        config.RenderScale = 1.0f;

    XString sharpness;
    if (CKRenderRootSettings().GetString("Sharpness", sharpness))
        config.Sharpness = CKRenderPipelineClampSharpness(ParseRenderScale(sharpness.CStr(), 0.0f));
    else
        config.Sharpness = 0.0f;
    return config;
}

CKRenderPipeline::CKRenderPipeline()
    : m_Context(nullptr), m_Encoder(nullptr), m_ExternalRenderTarget(FALSE),
      m_PostprocessSubmitted(FALSE), m_FrameNumber(0) {
    Vx3DMatrixIdentity(m_OrthoProj);
}

CKRenderPipeline::~CKRenderPipeline() {
    Shutdown();
}

void CKRenderPipeline::Init(CKRasterizerDevice *ctx) {
    m_Context = ctx;
    m_Encoder = nullptr;
    m_FrameNumber = 0;
    m_Postprocess.Init(ctx);
    if (m_Context) {
        m_Context->SetViewName(CKRP_VIEW_CLEAR, (CKSTRING)"clear");
        m_Context->SetViewName(CKRP_VIEW_BACKGROUND2D, (CKSTRING)"background2d");
        m_Context->SetViewName(CKRP_VIEW_RENDERFIRST3D, (CKSTRING)"renderfirst3d");
        m_Context->SetViewName(CKRP_VIEW_OPAQUE3D, (CKSTRING)"opaque3d");
        m_Context->SetViewName(CKRP_VIEW_STENCIL_CLEAR, (CKSTRING)"stencil-clear");
        m_Context->SetViewName(CKRP_VIEW_TRANSPARENT, (CKSTRING)"transparent3d");
        m_Context->SetViewName(CKRP_VIEW_POSTPROCESS, (CKSTRING)"postprocess");
        m_Context->SetViewName(CKRP_VIEW_FOREGROUND2D, (CKSTRING)"foreground2d");

        // Virtools fixed-function rendering is order-sensitive. Scene graph
        // traversal already handles render-first objects and transparent
        // sorting, so keep bgfx from reordering submissions within a view.
        m_Context->SetViewMode(CKRP_VIEW_CLEAR, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_BACKGROUND2D, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_RENDERFIRST3D, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_OPAQUE3D, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_STENCIL_CLEAR, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_TRANSPARENT, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_POSTPROCESS, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_FOREGROUND2D, CKRST_VIEWMODE_SEQUENTIAL);
    }
}

CKERROR CKRenderPipeline::PrepareShutdown() {
    CKERROR status = CK_OK;
    if (m_Context && m_Encoder) {
        status = m_Context->EndEncoder(m_Encoder);
        m_Encoder = nullptr;
    }
    if (status != CK_OK || (m_Context && !m_Context->IsIdle()))
        return status != CK_OK ? status : CKERR_INVALIDOPERATION;
    return CK_OK;
}

CKERROR CKRenderPipeline::Shutdown() {
    const CKERROR status = PrepareShutdown();
    if (status != CK_OK)
        return status;
    m_Postprocess.Shutdown();
    m_Encoder = nullptr;
    m_Context = nullptr;
    m_ExternalRenderTarget = FALSE;
    m_PostprocessSubmitted = FALSE;
    m_FrameNumber = 0;
    return CK_OK;
}

void CKRenderPipeline::SetResourceIds(const CKRenderPipelineResourceIds &ids) {
    // The engine only ever hands over an empty set to reset the pass.
    (void)ids;
    m_Postprocess.DestroySceneFrameBuffer();
    m_Postprocess.DestroyResources();
}

void CKRenderPipeline::SetExternalRenderTarget(CKBOOL enabled) {
    m_ExternalRenderTarget = enabled;
    if (enabled) {
        m_Postprocess.DestroySceneFrameBuffer();
        m_PostprocessSubmitted = FALSE;
    }
}

CKERROR CKRenderPipeline::BeginFrame(
    const CKRECT &viewport, CKDWORD clearFlags, CKDWORD clearColor, float clearZ,
    const VxMatrix &view, const VxMatrix &proj)
{
    if (!m_Context)
        return CKERR_INVALIDRENDERCONTEXT;
    const CKERROR deviceStatus = m_Context->GetDeviceStatus();
    if (deviceStatus != CK_OK)
        return deviceStatus;
    if (m_Encoder)
        return CKERR_INVALIDOPERATION;
    m_Config = CKRenderPipelineConfigFromSettings();
    m_PostprocessSubmitted = FALSE;

    // Build orthographic projection for 2D views
    float w = (float)(viewport.right - viewport.left);
    float h = (float)(viewport.bottom - viewport.top);
    if (w <= 0.0f) w = 1.0f;
    if (h <= 0.0f) h = 1.0f;

    Vx3DMatrixIdentity(m_OrthoProj);
    m_OrthoProj[0][0] = 2.0f / w;
    m_OrthoProj[1][1] = -2.0f / h;
    m_OrthoProj[2][2] = 1.0f;
    m_OrthoProj[3][0] = -1.0f;
    m_OrthoProj[3][1] = 1.0f;
    m_OrthoProj[3][3] = 1.0f;

    const CKBOOL wantsSceneFrameBuffer =
        !m_ExternalRenderTarget && m_Config.NeedsSceneFrameBuffer();
    CKBOOL useSceneFrameBuffer = FALSE;
    if (wantsSceneFrameBuffer) {
        useSceneFrameBuffer =
            EnsureSceneFrameBuffer(viewport) && m_Postprocess.EnsureResources();
        if (!useSceneFrameBuffer) {
            m_Postprocess.DestroySceneFrameBuffer();
            return CKERR_NOTIMPLEMENTED;
        }
    } else if (m_Postprocess.IsSceneFrameBufferActive()) {
        m_Postprocess.DestroySceneFrameBuffer();
    }

    if (!m_ExternalRenderTarget) {
        const CKDWORD sceneFb = useSceneFrameBuffer ? m_Postprocess.GetSceneFrameBuffer() : 0;
        CKERROR status = BindFrameBuffer(CKRP_VIEW_CLEAR, sceneFb);
        if (status != CK_OK) return status;
        status = BindFrameBuffer(CKRP_VIEW_BACKGROUND2D, sceneFb);
        if (status != CK_OK) return status;
        status = BindFrameBuffer(CKRP_VIEW_RENDERFIRST3D, sceneFb);
        if (status != CK_OK) return status;
        status = BindFrameBuffer(CKRP_VIEW_OPAQUE3D, sceneFb);
        if (status != CK_OK) return status;
        status = BindFrameBuffer(CKRP_VIEW_STENCIL_CLEAR, sceneFb);
        if (status != CK_OK) return status;
        status = BindFrameBuffer(CKRP_VIEW_TRANSPARENT, sceneFb);
        if (status != CK_OK) return status;
        status = BindFrameBuffer(CKRP_VIEW_POSTPROCESS, 0);
        if (status != CK_OK) return status;
        status = BindFrameBuffer(CKRP_VIEW_FOREGROUND2D, 0);
        if (status != CK_OK) return status;
    }

    CKRECT sceneRect = viewport;
    if (useSceneFrameBuffer) {
        sceneRect.left = 0;
        sceneRect.top = 0;
        sceneRect.right = (int)m_Postprocess.GetSceneWidth();
        sceneRect.bottom = (int)m_Postprocess.GetSceneHeight();
    }

    // View 0: Clear only
    CKERROR status = m_Context->SetViewRect(
        CKRP_VIEW_CLEAR, useSceneFrameBuffer ? sceneRect : viewport);
    if (status != CK_OK) return status;
    status = m_Context->SetViewClear(
        CKRP_VIEW_CLEAR, clearFlags, clearColor, clearZ, 0);
    if (status != CK_OK) return status;

    // View 1: Background 2D
    status = m_Context->SetViewRect(
        CKRP_VIEW_BACKGROUND2D, useSceneFrameBuffer ? sceneRect : viewport);
    if (status != CK_OK) return status;
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    status = m_Context->SetViewTransform(
        CKRP_VIEW_BACKGROUND2D, &identity, &m_OrthoProj);
    if (status != CK_OK) return status;

    // Log matrices on first few frames and then periodically
    const bool logFrameMatrices =
#if CKRE_ENABLE_FRAME_DIAGNOSTICS
        CKRenderDiagnosticsSettings().FrameLog.Enabled;
#else
        false;
#endif
    static int s_logCount = 0;
    if (logFrameMatrices && (s_logCount < 5 || (s_logCount >= 30 && s_logCount < 33))) {
        CK_LOG_FMT("RenderPipeline", "View matrix row0: %.3f %.3f %.3f %.3f",
                   view[0][0], view[0][1], view[0][2], view[0][3]);
        CK_LOG_FMT("RenderPipeline", "View matrix row1: %.3f %.3f %.3f %.3f",
                   view[1][0], view[1][1], view[1][2], view[1][3]);
        CK_LOG_FMT("RenderPipeline", "View matrix row2: %.3f %.3f %.3f %.3f",
                   view[2][0], view[2][1], view[2][2], view[2][3]);
        CK_LOG_FMT("RenderPipeline", "View matrix row3: %.3f %.3f %.3f %.3f",
                   view[3][0], view[3][1], view[3][2], view[3][3]);
        CK_LOG_FMT("RenderPipeline", "Proj matrix row0: %.3f %.3f %.3f %.3f",
                   proj[0][0], proj[0][1], proj[0][2], proj[0][3]);
        CK_LOG_FMT("RenderPipeline", "Proj matrix row1: %.3f %.3f %.3f %.3f",
                   proj[1][0], proj[1][1], proj[1][2], proj[1][3]);
        CK_LOG_FMT("RenderPipeline", "Proj matrix row2: %.3f %.3f %.3f %.3f",
                   proj[2][0], proj[2][1], proj[2][2], proj[2][3]);
        CK_LOG_FMT("RenderPipeline", "Proj matrix row3: %.3f %.3f %.3f %.3f",
                   proj[3][0], proj[3][1], proj[3][2], proj[3][3]);
        s_logCount++;
    }

    // View 2: render-first 3D backgrounds/sky objects
    status = m_Context->SetViewRect(
        CKRP_VIEW_RENDERFIRST3D, useSceneFrameBuffer ? sceneRect : viewport);
    if (status != CK_OK) return status;
    status = m_Context->SetViewTransform(CKRP_VIEW_RENDERFIRST3D, &view, &proj);
    if (status != CK_OK) return status;

    // View 3: Opaque 3D
    status = m_Context->SetViewRect(
        CKRP_VIEW_OPAQUE3D, useSceneFrameBuffer ? sceneRect : viewport);
    if (status != CK_OK) return status;
    status = m_Context->SetViewTransform(CKRP_VIEW_OPAQUE3D, &view, &proj);
    if (status != CK_OK) return status;

    // View 4: optional mid-frame stencil clear before transparent draws
    status = m_Context->SetViewRect(
        CKRP_VIEW_STENCIL_CLEAR, useSceneFrameBuffer ? sceneRect : viewport);
    if (status != CK_OK) return status;
    status = m_Context->SetViewClear(CKRP_VIEW_STENCIL_CLEAR, 0, 0, 1.0f, 0);
    if (status != CK_OK) return status;

    // View 5: Transparent 3D
    status = m_Context->SetViewRect(
        CKRP_VIEW_TRANSPARENT, useSceneFrameBuffer ? sceneRect : viewport);
    if (status != CK_OK) return status;
    status = m_Context->SetViewTransform(CKRP_VIEW_TRANSPARENT, &view, &proj);
    if (status != CK_OK) return status;

    // View 6: Optional postprocess scene composite
    status = ConfigurePostprocessView(viewport);
    if (status != CK_OK) return status;

    // View 7: Foreground 2D
    status = m_Context->SetViewRect(CKRP_VIEW_FOREGROUND2D, viewport);
    if (status != CK_OK) return status;
    status = m_Context->SetViewTransform(
        CKRP_VIEW_FOREGROUND2D, &identity, &m_OrthoProj);
    if (status != CK_OK) return status;

    // Acquire encoder
    m_Encoder = m_Context->BeginEncoder();
    if (!m_Encoder)
        return CKERR_INVALIDOPERATION;

    // Touch clear view to ensure it's processed even with no draws
    m_Encoder->Touch(CKRP_VIEW_CLEAR);
    const CKERROR encoderStatus = m_Encoder->GetStatus();
    if (encoderStatus != CK_OK) {
        m_Context->EndEncoder(m_Encoder);
        m_Encoder = nullptr;
        return encoderStatus;
    }
    return CK_OK;
}

CKERROR CKRenderPipeline::CompositeScene()
{
    if (!m_Context)
        return CKERR_INVALIDRENDERCONTEXT;
    if (!m_Encoder)
        return CKERR_INVALIDOPERATION;
    if (!m_Postprocess.IsSceneFrameBufferActive() || m_PostprocessSubmitted)
        return CK_OK;

    const CKERROR status = m_Postprocess.Submit(
        m_Encoder, CKRP_VIEW_POSTPROCESS, m_Config.FXAA, m_Config.Sharpness);
    if (status == CK_OK)
        m_PostprocessSubmitted = TRUE;
    return status;
}

CKERROR CKRenderPipeline::QueueStencilClearBeforeTransparent(const CKRECT &viewport, CKDWORD stencil)
{
    if (!m_Context || !m_Encoder)
        return CKERR_INVALIDOPERATION;

    CKERROR status = m_Context->SetViewRect(CKRP_VIEW_STENCIL_CLEAR, viewport);
    if (status != CK_OK)
        return status;
    status = m_Context->SetViewClear(
        CKRP_VIEW_STENCIL_CLEAR, CKRST_CTXCLEAR_STENCIL, 0, 1.0f, stencil);
    if (status != CK_OK)
        return status;
    m_Encoder->Touch(CKRP_VIEW_STENCIL_CLEAR);
    return m_Encoder->GetStatus();
}

CKERROR CKRenderPipeline::EndFrame(CKRST_FRAME_SYNC_MODE syncMode) {
    if (!m_Context)
        return CKERR_INVALIDRENDERCONTEXT;

    const bool logPresentSync =
#if CKRE_ENABLE_FRAME_DIAGNOSTICS
        CKRenderDiagnosticsSettings().FrameLog.PresentSync;
#else
        false;
#endif
    const double frameStart = logPresentSync ? CKRenderPerfNow() : 0.0;

    CKERROR encoderStatus = CK_OK;
    if (m_Encoder) {
        encoderStatus = m_Context->EndEncoder(m_Encoder);
        m_Encoder = nullptr;
    }

    CKDWORD frameNumber = 0;
    const CKERROR frameStatus = m_Context->Frame(
        syncMode, CKRST_FRAME_NONE, &frameNumber);
    if (logPresentSync) {
        const CKRasterizerDeviceStats *stats = m_Context->GetStats();
        CK_LOG_FMT("PresentSync",
                   "frame=%u syncMode=%d frameUs=%.1f cpuFrame=%lld waitSubmit=%lld waitRender=%lld draws=%u maxGpuLatency=%u transientVB=%u transientIB=%u",
                   frameNumber, syncMode, CKRenderPerfElapsedUs(frameStart),
                   stats ? (long long)stats->CpuTimeFrame : 0ll,
                   stats ? (long long)stats->WaitSubmit : 0ll,
                   stats ? (long long)stats->WaitRender : 0ll,
                   stats ? (unsigned)stats->DrawCalls : 0u,
                   stats ? (unsigned)stats->MaxGpuLatency : 0u,
                   stats ? (unsigned)stats->NumTransientVertexBuffers : 0u,
                   stats ? (unsigned)stats->NumTransientIndexBuffers : 0u);
    }
    if (encoderStatus != CK_OK || frameStatus != CK_OK) {
        CK_LOG_FMT("Rasterizer",
                   "frame submission failed encoderStatus=0x%08X frameStatus=0x%08X",
                   encoderStatus, frameStatus);
    }
    if (frameStatus == CK_OK)
        m_FrameNumber = frameNumber;
    return encoderStatus != CK_OK ? encoderStatus : frameStatus;
}

CKERROR CKRenderPipeline::BindFrameBuffer(CKRenderView view, CKDWORD frameBuffer)
{
    return m_Context
        ? m_Context->SetViewFrameBuffer(view, frameBuffer)
        : CKERR_INVALIDRENDERCONTEXT;
}

CKERROR CKRenderPipeline::ConfigurePostprocessView(const CKRECT &viewport)
{
    if (!m_Context)
        return CKERR_INVALIDRENDERCONTEXT;
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    CKERROR status = m_Context->SetViewRect(CKRP_VIEW_POSTPROCESS, viewport);
    if (status != CK_OK)
        return status;
    status = m_Context->SetViewTransform(
        CKRP_VIEW_POSTPROCESS, &identity, &identity);
    if (status != CK_OK)
        return status;
    return m_Context->SetViewClear(
        CKRP_VIEW_POSTPROCESS, 0, 0, 1.0f, 0);
}

CKBOOL CKRenderPipeline::EnsureSceneFrameBuffer(const CKRECT &viewport)
{
    if (!m_Context || m_ExternalRenderTarget)
        return FALSE;
    CKRasterizerDeviceCapsDesc caps;
    if (m_Context->GetCaps(&caps) != CK_OK || caps.MaxTextureSize == 0)
        return FALSE;
    const CKDWORD viewportWidth = (CKDWORD)((viewport.right > viewport.left) ? (viewport.right - viewport.left) : 1);
    const CKDWORD viewportHeight = (CKDWORD)((viewport.bottom > viewport.top) ? (viewport.bottom - viewport.top) : 1);
    const CKDWORD width = CKPostprocessPass::ScaledDimension(
        viewportWidth, m_Config.RenderScale, caps.MaxTextureSize);
    const CKDWORD height = CKPostprocessPass::ScaledDimension(
        viewportHeight, m_Config.RenderScale, caps.MaxTextureSize);
    return m_Postprocess.EnsureSceneFrameBuffer(width, height);
}
