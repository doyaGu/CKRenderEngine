#include "CKRenderPipeline.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"
#include "CKRenderPerfStats.h"
#include "CKRenderSettings.h"

#include "shaders/generated/dx11/vs_postprocess.bin.h"
#include "shaders/generated/dx11/fs_postprocess.bin.h"
#include "shaders/generated/dx12/vs_postprocess.bin.h"
#include "shaders/generated/dx12/fs_postprocess.bin.h"
#include "shaders/generated/spirv/vs_postprocess.bin.h"
#include "shaders/generated/spirv/fs_postprocess.bin.h"
#include "shaders/generated/glsl/vs_postprocess.bin.h"
#include "shaders/generated/glsl/fs_postprocess.bin.h"
#include "shaders/generated/essl/vs_postprocess.bin.h"
#include "shaders/generated/essl/fs_postprocess.bin.h"
#include "shaders/generated/metal/vs_postprocess.bin.h"
#include "shaders/generated/metal/fs_postprocess.bin.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct CKPostprocessShaderBlobSet {
    CK_SHADER_PROFILE Profile;
    const unsigned char *VS;
    unsigned int VSSize;
    const unsigned char *FS;
    unsigned int FSSize;
};

const CKPostprocessShaderBlobSet g_PostprocessShaderBlobSets[] = {
    {CKRST_SHADER_PROFILE_DX11, s_dx11_vs_postprocess, sizeof(s_dx11_vs_postprocess),
     s_dx11_fs_postprocess, sizeof(s_dx11_fs_postprocess)},
    {CKRST_SHADER_PROFILE_DX12, s_dx12_vs_postprocess, sizeof(s_dx12_vs_postprocess),
     s_dx12_fs_postprocess, sizeof(s_dx12_fs_postprocess)},
    {CKRST_SHADER_PROFILE_SPIRV, s_spirv_vs_postprocess, sizeof(s_spirv_vs_postprocess),
     s_spirv_fs_postprocess, sizeof(s_spirv_fs_postprocess)},
    {CKRST_SHADER_PROFILE_GLSL, s_glsl_vs_postprocess, sizeof(s_glsl_vs_postprocess),
     s_glsl_fs_postprocess, sizeof(s_glsl_fs_postprocess)},
    {CKRST_SHADER_PROFILE_ESSL, s_essl_vs_postprocess, sizeof(s_essl_vs_postprocess),
     s_essl_fs_postprocess, sizeof(s_essl_fs_postprocess)},
    {CKRST_SHADER_PROFILE_MSL, s_metal_vs_postprocess, sizeof(s_metal_vs_postprocess),
     s_metal_fs_postprocess, sizeof(s_metal_fs_postprocess)},
};

static const CKPostprocessShaderBlobSet *FindPostprocessShaderBlobSet(CK_SHADER_PROFILE profile)
{
    for (const CKPostprocessShaderBlobSet &set : g_PostprocessShaderBlobSets) {
        if (set.Profile == profile)
            return &set;
    }
    return nullptr;
}

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

static CKDWORD ScaledDimension(CKDWORD value, float scale)
{
    if (value == 0)
        value = 1;
    const float scaled = (float)value * scale;
    if (scaled <= 1.0f)
        return 1;
    if (scaled >= 16384.0f)
        return 16384;
    return (CKDWORD)(scaled + 0.5f);
}

float CKRenderPipelineClampRenderScale(float scale)
{
    if (!(scale > 0.0f) || !isfinite(scale))
        return 1.0f;
    if (scale < 0.5f)
        return 0.5f;
    if (scale > 2.0f)
        return 2.0f;
    return scale;
}

float CKRenderPipelineClampSharpness(float sharpness)
{
    if (!(sharpness > 0.0f) || !isfinite(sharpness))
        return 0.0f;
    if (sharpness > 1.0f)
        return 1.0f;
    return sharpness;
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
      m_SceneFrameBufferActive(FALSE), m_PostprocessSubmitted(FALSE),
      m_SceneWidth(0), m_SceneHeight(0),
      m_PostVertexShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN),
      m_FrameNumber(0) {
    Vx3DMatrixIdentity(m_OrthoProj);
}

CKRenderPipeline::~CKRenderPipeline() {
    Shutdown();
}

void CKRenderPipeline::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_Encoder = nullptr;
    m_FrameNumber = 0;
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
    DestroySceneFrameBuffer();
    DestroyPostprocessResources();
    m_Encoder = nullptr;
    m_Context = nullptr;
    m_ResourceIds = CKRenderPipelineResourceIds();
    m_ExternalRenderTarget = FALSE;
    m_SceneFrameBufferActive = FALSE;
    m_PostprocessSubmitted = FALSE;
    m_SceneWidth = 0;
    m_SceneHeight = 0;
    m_PostVertexShaderProfile = CKRST_SHADER_PROFILE_UNKNOWN;
    m_FrameNumber = 0;
    return CK_OK;
}

void CKRenderPipeline::SetResourceIds(const CKRenderPipelineResourceIds &ids) {
    m_ResourceIds = ids;
}

void CKRenderPipeline::SetExternalRenderTarget(CKBOOL enabled) {
    m_ExternalRenderTarget = enabled;
    if (enabled) {
        m_SceneFrameBufferActive = FALSE;
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
            EnsureSceneFrameBuffer(viewport) && EnsurePostprocessResources();
        if (!useSceneFrameBuffer) {
            DestroySceneFrameBuffer();
            return CKERR_NOTIMPLEMENTED;
        }
    } else if (m_SceneFrameBufferActive) {
        DestroySceneFrameBuffer();
    }

    if (!m_ExternalRenderTarget) {
        const CKDWORD sceneFb = useSceneFrameBuffer ? m_ResourceIds.SceneFrameBuffer : 0;
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
        sceneRect.right = (int)m_SceneWidth;
        sceneRect.bottom = (int)m_SceneHeight;
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

    // View 6: Foreground 2D
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
    if (!m_SceneFrameBufferActive || m_PostprocessSubmitted)
        return CK_OK;

    const CKERROR status = SubmitPostprocess();
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
        const CKRenderStats *stats = m_Context->GetStats();
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
    const CKDWORD viewportWidth = (CKDWORD)((viewport.right > viewport.left) ? (viewport.right - viewport.left) : 1);
    const CKDWORD viewportHeight = (CKDWORD)((viewport.bottom > viewport.top) ? (viewport.bottom - viewport.top) : 1);
    const CKDWORD width = ScaledDimension(viewportWidth, m_Config.RenderScale);
    const CKDWORD height = ScaledDimension(viewportHeight, m_Config.RenderScale);

    if (m_SceneFrameBufferActive && m_SceneWidth == width && m_SceneHeight == height)
        return TRUE;

    DestroySceneFrameBuffer();

    CKTextureDesc colorDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, colorDesc.Format);
    colorDesc.Format.Width = (int)width;
    colorDesc.Format.Height = (int)height;
    colorDesc.MipMapCount = 1;
    colorDesc.Depth = 1;
    colorDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB |
                      CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    if (m_Context->CreateTexture(&colorDesc, nullptr,
                                 &m_ResourceIds.SceneColorTexture) != CK_OK)
        return FALSE;

    CKDepthTextureDesc depthDesc = {};
    depthDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipMapCount = 1;
    depthDesc.DepthFormat = CKRST_DEPTHFMT_D24S8;
    CKERROR depthErr = m_Context->CreateDepthTexture(
        &depthDesc, &m_ResourceIds.SceneDepthTexture);
    if (depthErr != CK_OK) {
        depthDesc.DepthFormat = CKRST_DEPTHFMT_D24;
        depthErr = m_Context->CreateDepthTexture(
            &depthDesc, &m_ResourceIds.SceneDepthTexture);
    }
    if (depthErr != CK_OK) {
        depthDesc.DepthFormat = CKRST_DEPTHFMT_D16;
        depthErr = m_Context->CreateDepthTexture(
            &depthDesc, &m_ResourceIds.SceneDepthTexture);
    }
    if (depthErr != CK_OK) {
        m_Context->DeleteObject(m_ResourceIds.SceneColorTexture, CKRST_OBJ_TEXTURE);
        m_ResourceIds.SceneColorTexture = 0;
        return FALSE;
    }

    CKFrameBufferAttachmentDesc colorAttachment;
    colorAttachment.Texture = m_ResourceIds.SceneColorTexture;
    colorAttachment.Mip = 0;
    colorAttachment.Layer = 0;

    CKFrameBufferDesc fbDesc;
    fbDesc.Color = &colorAttachment;
    fbDesc.ColorCount = 1;
    fbDesc.DepthStencil.Texture = m_ResourceIds.SceneDepthTexture;
    fbDesc.DepthStencil.Mip = 0;
    fbDesc.DepthStencil.Layer = 0;

    if (m_Context->CreateFrameBuffer(&fbDesc,
                                     &m_ResourceIds.SceneFrameBuffer) != CK_OK) {
        m_Context->DeleteObject(m_ResourceIds.SceneDepthTexture, CKRST_OBJ_TEXTURE);
        m_Context->DeleteObject(m_ResourceIds.SceneColorTexture, CKRST_OBJ_TEXTURE);
        m_ResourceIds.SceneDepthTexture = 0;
        m_ResourceIds.SceneColorTexture = 0;
        return FALSE;
    }

    m_SceneWidth = width;
    m_SceneHeight = height;
    m_SceneFrameBufferActive = TRUE;
    return TRUE;
}

void CKRenderPipeline::DestroySceneFrameBuffer()
{
    if (!m_Context)
        return;
    if (m_ResourceIds.SceneFrameBuffer)
        m_Context->DeleteObject(m_ResourceIds.SceneFrameBuffer, CKRST_OBJ_FRAMEBUFFER);
    if (m_ResourceIds.SceneDepthTexture)
        m_Context->DeleteObject(m_ResourceIds.SceneDepthTexture, CKRST_OBJ_TEXTURE);
    if (m_ResourceIds.SceneColorTexture)
        m_Context->DeleteObject(m_ResourceIds.SceneColorTexture, CKRST_OBJ_TEXTURE);
    m_ResourceIds.SceneFrameBuffer = 0;
    m_ResourceIds.SceneDepthTexture = 0;
    m_ResourceIds.SceneColorTexture = 0;
    m_SceneFrameBufferActive = FALSE;
    m_PostprocessSubmitted = FALSE;
    m_SceneWidth = 0;
    m_SceneHeight = 0;
}

CKBOOL CKRenderPipeline::EnsurePostprocessResources()
{
    if (!m_Context || !m_Context->m_Driver)
        return FALSE;
    CKRasterizerTargetDesc target;
    if (m_Context->GetTargetDesc(&target) != CK_OK)
        return FALSE;

    if (target.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN)
        return FALSE;
    if (m_PostVertexShaderProfile == target.ShaderProfile &&
        m_Context->IsObjectAlive(m_ResourceIds.PostProgram, CKRST_OBJ_PROGRAM) &&
        m_Context->IsObjectAlive(m_ResourceIds.PostVertexShader, CKRST_OBJ_SHADER) &&
        m_Context->IsObjectAlive(m_ResourceIds.PostPixelShader, CKRST_OBJ_SHADER) &&
        m_Context->IsObjectAlive(m_ResourceIds.PostSamplerUniform, CKRST_OBJ_UNIFORM) &&
        m_Context->IsObjectAlive(m_ResourceIds.PostParamsUniform, CKRST_OBJ_UNIFORM) &&
        m_Context->IsObjectAlive(m_ResourceIds.PostVertexLayout, CKRST_OBJ_VERTEXLAYOUT))
        return TRUE;

    DestroyPostprocessResources();

    const CKPostprocessShaderBlobSet *blobs = FindPostprocessShaderBlobSet(target.ShaderProfile);
    if (!blobs)
        return FALSE;

    CKUniformDesc uniformDesc;
    uniformDesc.Name = (CKSTRING)"s_sceneColor";
    uniformDesc.Type = CKRST_UNIFORM_SAMPLER;
    uniformDesc.Count = 1;
    if (m_Context->CreateUniform(&uniformDesc,
                                 &m_ResourceIds.PostSamplerUniform) != CK_OK) {
        DestroyPostprocessResources();
        return FALSE;
    }

    uniformDesc.Name = (CKSTRING)"u_postParams";
    uniformDesc.Type = CKRST_UNIFORM_VEC4;
    uniformDesc.Count = 1;
    if (m_Context->CreateUniform(&uniformDesc,
                                 &m_ResourceIds.PostParamsUniform) != CK_OK) {
        DestroyPostprocessResources();
        return FALSE;
    }

    CKShaderDesc shaderDesc;
    shaderDesc.Format = CKRST_SHADER_FORMAT_NATIVE;
    shaderDesc.Profile = target.ShaderProfile;

    shaderDesc.Stage = CKRST_SHADER_VERTEX;
    shaderDesc.Code = blobs->VS;
    shaderDesc.CodeSize = blobs->VSSize;
    if (m_Context->CreateShader(&shaderDesc,
                                &m_ResourceIds.PostVertexShader) != CK_OK) {
        DestroyPostprocessResources();
        return FALSE;
    }

    shaderDesc.Stage = CKRST_SHADER_PIXEL;
    shaderDesc.Code = blobs->FS;
    shaderDesc.CodeSize = blobs->FSSize;
    if (m_Context->CreateShader(&shaderDesc,
                                &m_ResourceIds.PostPixelShader) != CK_OK) {
        DestroyPostprocessResources();
        return FALSE;
    }

    CKProgramDesc programDesc;
    programDesc.VertexShader = m_ResourceIds.PostVertexShader;
    programDesc.PixelShader = m_ResourceIds.PostPixelShader;
    programDesc.ConsumeShaders = FALSE;
    if (m_Context->CreateProgram(&programDesc,
                                 &m_ResourceIds.PostProgram) != CK_OK) {
        DestroyPostprocessResources();
        return FALSE;
    }

    CKVertexElementDesc elements[2];
    memset(elements, 0, sizeof(elements));
    elements[0].Attrib = CKRST_ATTRIB_POSITION;
    elements[0].Type = CKRST_ATTRIBTYPE_FLOAT;
    elements[0].Count = 3;
    elements[0].Normalized = FALSE;
    elements[0].AsInt = FALSE;
    elements[0].Offset = 0;
    elements[1].Attrib = CKRST_ATTRIB_TEXCOORD0;
    elements[1].Type = CKRST_ATTRIBTYPE_FLOAT;
    elements[1].Count = 2;
    elements[1].Normalized = FALSE;
    elements[1].AsInt = FALSE;
    elements[1].Offset = 12;

    CKVertexLayoutDesc layoutDesc;
    layoutDesc.Elements = elements;
    layoutDesc.ElementCount = 2;
    layoutDesc.Stride = 20;
    if (m_Context->CreateVertexLayout(&layoutDesc,
                                      &m_ResourceIds.PostVertexLayout) != CK_OK) {
        DestroyPostprocessResources();
        return FALSE;
    }

    m_PostVertexShaderProfile = target.ShaderProfile;
    return TRUE;
}

void CKRenderPipeline::DestroyPostprocessResources()
{
    if (!m_Context)
        return;
    if (m_ResourceIds.PostProgram)
        m_Context->DeleteObject(m_ResourceIds.PostProgram, CKRST_OBJ_PROGRAM);
    if (m_ResourceIds.PostVertexShader)
        m_Context->DeleteObject(m_ResourceIds.PostVertexShader, CKRST_OBJ_SHADER);
    if (m_ResourceIds.PostPixelShader)
        m_Context->DeleteObject(m_ResourceIds.PostPixelShader, CKRST_OBJ_SHADER);
    if (m_ResourceIds.PostSamplerUniform)
        m_Context->DeleteObject(m_ResourceIds.PostSamplerUniform, CKRST_OBJ_UNIFORM);
    if (m_ResourceIds.PostParamsUniform)
        m_Context->DeleteObject(m_ResourceIds.PostParamsUniform, CKRST_OBJ_UNIFORM);
    if (m_ResourceIds.PostVertexLayout)
        m_Context->DeleteObject(m_ResourceIds.PostVertexLayout, CKRST_OBJ_VERTEXLAYOUT);
    m_ResourceIds.PostProgram = 0;
    m_ResourceIds.PostVertexShader = 0;
    m_ResourceIds.PostPixelShader = 0;
    m_ResourceIds.PostSamplerUniform = 0;
    m_ResourceIds.PostParamsUniform = 0;
    m_ResourceIds.PostVertexLayout = 0;
    m_PostVertexShaderProfile = CKRST_SHADER_PROFILE_UNKNOWN;
}

CKERROR CKRenderPipeline::SubmitPostprocess()
{
    if (!m_Context || !m_Encoder || !m_SceneFrameBufferActive ||
        !EnsurePostprocessResources())
        return CKERR_NOTIMPLEMENTED;

    struct PostVertex {
        float X, Y, Z;
        float U, V;
    };

    CKTransientVertexBuffer tvb;
    memset(&tvb, 0, sizeof(tvb));
    if (!m_Context->AllocTransientVertexBuffer(&tvb, 3, m_ResourceIds.PostVertexLayout))
        return CKERR_OUTOFMEMORY;

    PostVertex *vertices = (PostVertex *)tvb.Data;
    CKRasterizerTargetDesc target;
    const CKBOOL originBottomLeft =
        m_Context->GetTargetDesc(&target) == CK_OK &&
        target.OriginBottomLeft;
    const float bottomV = originBottomLeft ? 0.0f : 1.0f;
    const float extendedTopV = originBottomLeft ? 2.0f : -1.0f;
    vertices[0] = {-1.0f, -1.0f, 0.0f, 0.0f, bottomV};
    vertices[1] = { 3.0f, -1.0f, 0.0f, 2.0f, bottomV};
    vertices[2] = {-1.0f,  3.0f, 0.0f, 0.0f, extendedTopV};

    CKSamplerDesc sampler;
    memset(&sampler, 0, sizeof(sampler));
    sampler.MinFilter = CKRST_FILTER_LINEAR;
    sampler.MagFilter = CKRST_FILTER_LINEAR;
    sampler.MipFilter = CKRST_FILTER_NONE;
    sampler.AddressU = CKRST_ADDRESS_CLAMP;
    sampler.AddressV = CKRST_ADDRESS_CLAMP;
    sampler.AddressW = CKRST_ADDRESS_CLAMP;
    sampler.CompareFunc = CKRST_COMPARE_NONE;

    const float params[4] = {
        m_SceneWidth > 0 ? 1.0f / (float)m_SceneWidth : 1.0f,
        m_SceneHeight > 0 ? 1.0f / (float)m_SceneHeight : 1.0f,
        m_Config.FXAA ? 1.0f : 0.0f,
        m_Config.Sharpness
    };

    CKDrawState state = CKDrawStateBuilder()
        .Depth(FALSE, FALSE, VXCMP_ALWAYS)
        .Cull(VXCULL_NONE)
        .Build();

    m_Encoder->SetState(state);
    m_Encoder->SetStencilRef(0);
    m_Encoder->SetStencilMask(0xFF, 0xFF);
    m_Encoder->SetScissor(nullptr);
    m_Encoder->SetPointSize(1.0f);
    m_Encoder->SetTransientVertexBuffer(0, &tvb);
    m_Encoder->SetTexture(0, m_ResourceIds.PostSamplerUniform,
                          m_ResourceIds.SceneColorTexture, &sampler);
    m_Encoder->SetUniform(m_ResourceIds.PostParamsUniform, params, 1);
    m_Encoder->Submit(CKRP_VIEW_POSTPROCESS, m_ResourceIds.PostProgram, 0, CKRST_DISCARD_ALL);
    return m_Encoder->GetStatus();
}
