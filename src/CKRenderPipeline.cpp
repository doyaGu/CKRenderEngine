#include "CKRenderPipeline.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"
#include "CKRenderDebugConfig.h"

CKRenderPipeline::CKRenderPipeline()
    : m_Context(nullptr), m_Encoder(nullptr) {
    Vx3DMatrixIdentity(m_OrthoProj);
}

CKRenderPipeline::~CKRenderPipeline() {
    Shutdown();
}

void CKRenderPipeline::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_Encoder = nullptr;
    if (m_Context) {
        m_Context->SetViewName(CKRP_VIEW_CLEAR, (CKSTRING)"clear");
        m_Context->SetViewName(CKRP_VIEW_BACKGROUND2D, (CKSTRING)"background2d");
        m_Context->SetViewName(CKRP_VIEW_RENDERFIRST3D, (CKSTRING)"renderfirst3d");
        m_Context->SetViewName(CKRP_VIEW_OPAQUE3D, (CKSTRING)"opaque3d");
        m_Context->SetViewName(CKRP_VIEW_TRANSPARENT, (CKSTRING)"transparent3d");
        m_Context->SetViewName(CKRP_VIEW_FOREGROUND2D, (CKSTRING)"foreground2d");

        // Virtools fixed-function rendering is order-sensitive. Scene graph
        // traversal already handles render-first objects and transparent
        // sorting, so keep bgfx from reordering submissions within a view.
        m_Context->SetViewMode(CKRP_VIEW_CLEAR, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_BACKGROUND2D, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_RENDERFIRST3D, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_OPAQUE3D, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_TRANSPARENT, CKRST_VIEWMODE_SEQUENTIAL);
        m_Context->SetViewMode(CKRP_VIEW_FOREGROUND2D, CKRST_VIEWMODE_SEQUENTIAL);
    }
}

void CKRenderPipeline::Shutdown() {
    m_Encoder = nullptr;
    m_Context = nullptr;
}

void CKRenderPipeline::BeginFrame(
    const CKRECT &viewport, CKDWORD clearFlags, CKDWORD clearColor, float clearZ,
    const VxMatrix &view, const VxMatrix &proj)
{
    if (!m_Context) return;

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

    // View 0: Clear only
    m_Context->SetViewRect(CKRP_VIEW_CLEAR, viewport);
    m_Context->SetViewClear(CKRP_VIEW_CLEAR, clearFlags, clearColor, clearZ, 0);

    // View 1: Background 2D
    m_Context->SetViewRect(CKRP_VIEW_BACKGROUND2D, viewport);
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    m_Context->SetViewTransform(CKRP_VIEW_BACKGROUND2D, &identity, &m_OrthoProj);

    // Log matrices on first few frames and then periodically
    static const bool s_LogFrameMatrices = CKRenderDebugConfigBool("DEBUG_FRAME_LOG", false);
    static int s_logCount = 0;
    if (s_LogFrameMatrices && (s_logCount < 5 || (s_logCount >= 30 && s_logCount < 33))) {
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
    m_Context->SetViewRect(CKRP_VIEW_RENDERFIRST3D, viewport);
    m_Context->SetViewTransform(CKRP_VIEW_RENDERFIRST3D, &view, &proj);

    // View 3: Opaque 3D
    m_Context->SetViewRect(CKRP_VIEW_OPAQUE3D, viewport);
    m_Context->SetViewTransform(CKRP_VIEW_OPAQUE3D, &view, &proj);

    // View 4: Transparent 3D
    m_Context->SetViewRect(CKRP_VIEW_TRANSPARENT, viewport);
    m_Context->SetViewTransform(CKRP_VIEW_TRANSPARENT, &view, &proj);

    // View 5: Foreground 2D
    m_Context->SetViewRect(CKRP_VIEW_FOREGROUND2D, viewport);
    m_Context->SetViewTransform(CKRP_VIEW_FOREGROUND2D, &identity, &m_OrthoProj);

    // Acquire encoder
    m_Encoder = m_Context->BeginEncoder();

    // Touch clear view to ensure it's processed even with no draws
    if (m_Encoder)
        m_Encoder->Touch(CKRP_VIEW_CLEAR);
}

void CKRenderPipeline::EndFrame(CKRST_FRAME_SYNC_MODE syncMode) {
    if (!m_Context) return;

    static const bool s_LogPresentSync = []() {
        return CKRenderDebugConfigBool("DEBUG_LOG_PRESENT_SYNC", false);
    }();
    static int s_PresentSyncLogCount = 0;
    if (s_LogPresentSync && s_PresentSyncLogCount < 64) {
        CK_LOG_FMT("PresentSync", "RenderPipeline/EndFrame syncMode=%d", syncMode);
        ++s_PresentSyncLogCount;
    }

    if (m_Encoder) {
        m_Context->EndEncoder(m_Encoder);
        m_Encoder = nullptr;
    }

    m_Context->Frame(syncMode);
}
