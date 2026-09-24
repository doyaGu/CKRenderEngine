#include "CKBgfxRasterizerContext.h"
#include "CKBgfxResources.h"
#include "CKBgfxInternal.h"
#include "CKRasterizerDrawMarker.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>

static_assert(BGFX_API_VERSION == 153, "Review CKBgfxRasterizer mappings before updating bgfx");

static VxMutex g_BgfxContextMutex;
static CKBgfxRasterizerContext *g_BgfxActiveContext = NULL;

static int CKBgfxScaleWindowCoordinate(int value, CKDWORD drawable,
                                       CKDWORD logical, bool upper)
{
    const int64_t pixels =
        (static_cast<int64_t>(value) * drawable + (upper ? logical - 1 : 0)) /
        logical;
    return static_cast<int>(XMin(pixels, static_cast<int64_t>(UINT16_MAX) + 1));
}

static bool CKBgfxClaimActiveContext(CKBgfxRasterizerContext *Context)
{
    VxMutexLock lock(g_BgfxContextMutex);
    if (g_BgfxActiveContext && g_BgfxActiveContext != Context)
        return false;
    g_BgfxActiveContext = Context;
    return true;
}

static void CKBgfxReleaseActiveContext(CKBgfxRasterizerContext *Context)
{
    VxMutexLock lock(g_BgfxContextMutex);
    if (g_BgfxActiveContext == Context)
        g_BgfxActiveContext = NULL;
}

static void *CKBgfxSdlPointerProperty(SDL_PropertiesID props, const char *name)
{
    return SDL_GetPointerProperty(props, name, NULL);
}

static bool CKBgfxFillSDLPlatformData(WIN_HANDLE Window, bgfx::PlatformData &platformData)
{
    platformData = bgfx::PlatformData();

    SDL_Window *window = static_cast<SDL_Window *>(Window);
    if (!window)
        return false;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props)
        return false;

#if defined(_WIN32)
    platformData.nwh = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER);
    return platformData.nwh != NULL;
#elif defined(__APPLE__)
    platformData.nwh = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER);
    return platformData.nwh != NULL;
#elif defined(__linux__)
    void *waylandSurface = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER);
    if (waylandSurface) {
        platformData.ndt = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER);
        platformData.nwh = waylandSurface;
        platformData.type = bgfx::NativeWindowHandleType::Wayland;
        return platformData.ndt != NULL;
    }

    const Sint64 x11Window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (x11Window != 0) {
        platformData.ndt = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER);
        platformData.nwh = reinterpret_cast<void *>(static_cast<uintptr_t>(x11Window));
        return platformData.ndt != NULL;
    }

    return false;
#else
    return false;
#endif
}

static void CKBgfxResolveDrawableSize(WIN_HANDLE Window, int &width, int &height)
{
    if (!Window)
        return;

    SDL_Window *window = static_cast<SDL_Window *>(Window);
    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSizeInPixels(window, &windowW, &windowH);
    if (windowW > 0 && windowH > 0 && windowW <= UINT16_MAX && windowH <= UINT16_MAX) {
        width = windowW;
        height = windowH;
    }
}

static CKBgfxTextureOrientation CKBgfxMergeOrientation(
    CKBgfxTextureOrientation Current,
    CKBgfxTextureOrientation Incoming,
    CKBOOL FullOverwrite)
{
    if (FullOverwrite)
        return Incoming;
    if (Incoming == CKBGFX_ORIENTATION_UNKNOWN)
        return CKBGFX_ORIENTATION_UNKNOWN;
    if (Incoming == CKBGFX_ORIENTATION_MIXED ||
        Current == CKBGFX_ORIENTATION_MIXED)
        return CKBGFX_ORIENTATION_MIXED;
    if (Current == CKBGFX_ORIENTATION_UNKNOWN || Current == Incoming)
        return Incoming;
    return CKBGFX_ORIENTATION_MIXED;
}
// ===========================================================================
// CKBgfxRasterizerContext
// ===========================================================================

CKBgfxRasterizerContext::CKBgfxRasterizerContext()
    : CKRasterizerContext(), m_PassTarget(0), m_LastDeviceFrame(0),
      m_TargetFrameBuffer(0), m_TargetDepthTexture(0), m_CopyTexture(0),
      m_CopyWidth(0), m_CopyHeight(0), m_FrameDrawCalls(0),
      m_FramePrimitives(0), m_FramePasses(0), m_FrameClears(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0),
      m_LayoutMismatchLogged(FALSE),
      m_BgfxInitialized(FALSE), m_BgfxCreated(FALSE), m_BgfxWindow(NULL), m_BgfxPosX(0), m_BgfxPosY(0),
      m_BgfxWidth(0), m_BgfxHeight(0), m_DrawableWidth(0), m_DrawableHeight(0),
      m_BgfxFullscreen(FALSE), m_RendererName("Unknown"),
      m_RendererType(bgfx::RendererType::Count), m_NativeSupported(0),
      m_VSync(FALSE), m_ResetFlags(BGFX_RESET_NONE), m_ApiThreadId(0),
      m_BgfxShuttingDown{FALSE}, m_FatalError{CK_OK},
      m_FrameInProgress(FALSE), m_PassOpen(FALSE), m_CurrentView(0), m_NextView(0), m_LastFrameViewCount(0),
      m_BgfxFramePasses(0), m_FrameDraws(0), m_FrameBlits(0), m_BgfxFrameTextureUploads(0), m_BgfxFrameBufferUploads(0),
      m_LastSubmitId(0), m_CompletedSubmitId(0),
      m_CachedDrawState(), m_CachedBgfxState(0), m_PointSize(0), m_CurrentLayout(0),
      m_DebugVertexBindingMask(0), m_DebugTextureBindingMask(0),
      m_DebugIndexBuffer(0), m_DebugIndexStart(0), m_DebugIndexCount(0), m_DebugIndexHandle(0),
      m_DrawErrorLogCount(0),
      m_DebugFrameId(0), m_DebugSubmitSerial{0}, m_DebugMissingAnnotationCount{0},
      m_DebugMarkerOverwriteCount{0}, m_DebugMarkerStaleCount{0},
      m_DebugInvalidSubmitCount{0}, m_DebugFatalCount{0},
      m_DebugParsedAnnotationCount{0}, m_DebugRawPrimitiveCount{0},
      m_DebugTransientAllocMissCount{0},
      m_DebugFlags(0), m_DrawMapFlags(0), m_DrawMapActive(FALSE),
      m_DrawMapSubmitActive(FALSE), m_DrawMapMarkerCaptureActive(FALSE),
      m_DebugBgfxFlags(0), m_DebugOverlay(FALSE), m_DebugLogPresentSync(FALSE),
      m_DebugLogTextureBindings(FALSE), m_DebugLogTextures(FALSE), m_DebugLogUniforms(FALSE),
      m_Resources(new CKBgfxResources()),
      m_TransientVBCount(0), m_TransientIBCount(0)
{
    memset(&m_Stats, 0, sizeof(m_Stats));
    memset(m_NativeFormatCaps, 0, sizeof(m_NativeFormatCaps));
    memset(m_DebugVertexBindings, 0, sizeof(m_DebugVertexBindings));
    memset(m_DebugTextureBindings, 0, sizeof(m_DebugTextureBindings));
    m_LastMarker[0] = '\0';
    m_BgfxCallback.SetContext(this);
    for (int i = 0; i < CKRST_MAX_PASSES; ++i) {
        m_DebugViewSubmitSerial[i].store(0, std::memory_order_relaxed);
        m_DebugViewName[i][0] = '\0';
        m_ViewFrameBuffer[i] = 0;
        m_ViewRect[i].left = 0;
        m_ViewRect[i].top = 0;
        m_ViewRect[i].right = 0;
        m_ViewRect[i].bottom = 0;
        m_ViewClearFlags[i] = 0;
        m_ViewClearRecorded[i] = FALSE;
    }
    for (int i = 0; i < CKDRAW_SOURCE_COUNT; ++i)
        m_DebugSourceSubmitCount[i].store(0, std::memory_order_relaxed);
}

void CKBgfxRasterizerContext::RecordTextureWrite(
    CKBgfxTextureRecord *Texture, CKDWORD Mip,
    CKBgfxTextureOrientation Orientation, CKBOOL FullOverwrite)
{
    if (!Texture || Mip >= Texture->MipCount || Mip >= CKBGFX_MAX_TRACKED_MIPS)
        return;
    VxMutexLock lock(m_ResourceStateMutex);
    const CKBgfxTextureOrientation current =
        (CKBgfxTextureOrientation)Texture->ReadbackOrientation[Mip];
    Texture->ReadbackOrientation[Mip] = (CKBYTE)CKBgfxMergeOrientation(
        current, Orientation, FullOverwrite);
}
void CKBgfxRasterizerContext::RecordTextureBlit(
    CKBgfxTextureRecord *Destination, CKDWORD DestinationMip,
    const CKBgfxTextureRecord *Source, CKDWORD SourceMip,
    CKBOOL FullOverwrite)
{
    if (!Destination || !Source ||
        DestinationMip >= Destination->MipCount || SourceMip >= Source->MipCount ||
        DestinationMip >= CKBGFX_MAX_TRACKED_MIPS || SourceMip >= CKBGFX_MAX_TRACKED_MIPS)
        return;
    VxMutexLock lock(m_ResourceStateMutex);
    const CKBgfxTextureOrientation source =
        (CKBgfxTextureOrientation)Source->ReadbackOrientation[SourceMip];
    const CKBgfxTextureOrientation current =
        (CKBgfxTextureOrientation)Destination->ReadbackOrientation[DestinationMip];
    Destination->ReadbackOrientation[DestinationMip] =
        (CKBYTE)CKBgfxMergeOrientation(current, source, FullOverwrite);
}

CKBOOL CKBgfxRasterizerContext::GetTextureBottomLeft(CKDWORD Texture,
                                                      CKBOOL &BottomLeft)
{
    CKBgfxTextureRecord *record = GetTexture(Texture);
    if (!record)
        return FALSE;
    VxMutexLock lock(m_ResourceStateMutex);
    const CKBgfxTextureOrientation orientation =
        (CKBgfxTextureOrientation)record->ReadbackOrientation[0];
    if (orientation != CKBGFX_ORIENTATION_TOP_LEFT &&
        orientation != CKBGFX_ORIENTATION_BOTTOM_LEFT)
        return FALSE;
    BottomLeft = orientation == CKBGFX_ORIENTATION_BOTTOM_LEFT ? TRUE : FALSE;
    return TRUE;
}

void CKBgfxRasterizerContext::RecordViewColorWrite(bgfx::ViewId View, CKBOOL HasDraw)
{
    if (View >= CKRST_MAX_PASSES)
        return;

    VxMutexLock lock(m_ResourceStateMutex);
    CKBgfxFrameBufferRecord *frameBuffer = GetFrameBuffer(m_ViewFrameBuffer[View]);
    if (!frameBuffer)
        return;

    const CKBgfxTextureOrientation targetOrientation =
        m_Caps.OriginBottomLeft
            ? CKBGFX_ORIENTATION_BOTTOM_LEFT
            : CKBGFX_ORIENTATION_TOP_LEFT;
    const CKBOOL executeColorClear =
        !m_ViewClearRecorded[View] &&
        (m_ViewClearFlags[View] & CKRST_CTXCLEAR_COLOR) != 0;

    CKBgfxTextureRecord *texture = GetTexture(frameBuffer->Desc.ColorTexture);
    const CKDWORD mip = frameBuffer->Desc.ColorMip;
    if (texture && mip < texture->MipCount && mip < CKBGFX_MAX_TRACKED_MIPS) {
        CKBgfxTextureOrientation current =
            (CKBgfxTextureOrientation)texture->ReadbackOrientation[mip];
        if (executeColorClear) {
            const CKDWORD width = XMax((CKDWORD)1, texture->Width >> mip);
            const CKDWORD height = XMax((CKDWORD)1, texture->Height >> mip);
            const CKBOOL fullClear =
                m_ViewRect[View].left == 0 && m_ViewRect[View].top == 0 &&
                m_ViewRect[View].right == (int)width &&
                m_ViewRect[View].bottom == (int)height;
            current = CKBgfxMergeOrientation(current, targetOrientation, fullClear);
        }
        if (HasDraw)
            current = CKBgfxMergeOrientation(current, targetOrientation, FALSE);
        texture->ReadbackOrientation[mip] = (CKBYTE)current;
    }
    if (executeColorClear)
        m_ViewClearRecorded[View] = TRUE;
}


// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

static bool CKBgfxAllowsShaderTarget(const CKRasterizerInitParameters &Desc, CK_SHADER_PROFILE Profile)
{
    if (Profile == CKRST_SHADER_PROFILE_UNKNOWN)
        return false;
    if (Desc.ShaderTargets.Size() == 0)
        return true;
    for (int i = 0; i < Desc.ShaderTargets.Size(); ++i) {
        const CKFFShaderTarget &target = Desc.ShaderTargets[i];
        if (target.Format == CKRST_SHADER_FORMAT_BGFX && target.Profile == Profile)
            return true;
    }
    return false;
}

CKERROR CKBgfxRasterizerContext::Init(const CKRasterizerInitParameters *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (m_BgfxInitialized || m_BgfxCreated)
        return CKERR_INVALIDOPERATION;
    bool targetAllowed = Desc->ShaderTargets.Size() == 0;
    for (int renderer = 0; !targetAllowed && renderer < (int)bgfx::RendererType::Count; ++renderer)
        targetAllowed = CKBgfxAllowsShaderTarget(*Desc, CKBgfxShaderProfile((bgfx::RendererType::Enum)renderer));
    if (!targetAllowed) {
        CKBgfxLogf("Init", "InitDesc.ShaderTargets contains no supported BGFX format/profile pair");
        return CKERR_NOTIMPLEMENTED;
    }
    m_FatalError.store(CK_OK, std::memory_order_release);
    m_BgfxShuttingDown.store(FALSE, std::memory_order_release);
    if (!CKBgfxClaimActiveContext(this)) {
        CKBgfxLogf("Init", "another CKBgfxRasterizerContext is already active");
        return CKERR_INVALIDOPERATION;
    }

    WIN_HANDLE Window = Desc->Window;
    int Width = Desc->Width;
    int Height = Desc->Height;
    m_BgfxWindow = Window;
    m_BgfxPosX = Desc->PosX;
    m_BgfxPosY = Desc->PosY;
    m_BgfxFullscreen = Desc->Fullscreen;
    m_DebugFlags = Desc->DebugFlags;

    if (Width <= 0 || Height <= 0)
    {
        CKRECT rc = {0, 0,
                     CKBgfxConfigPositiveInt("Renderer", "FallbackWidth", 640),
                     CKBgfxConfigPositiveInt("Renderer", "FallbackHeight", 480)};
        if (Window)
            VxGetClientRect(Window, &rc);
        Width = rc.right - rc.left;
        Height = rc.bottom - rc.top;
    }

    if (Width <= 0)
        Width = CKBgfxConfigPositiveInt("Renderer", "FallbackWidth", 640);
    if (Height <= 0)
        Height = CKBgfxConfigPositiveInt("Renderer", "FallbackHeight", 480);

    if (Width > 0xffff || Height > 0xffff) {
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }

    m_BgfxWidth = (CKDWORD)Width;
    m_BgfxHeight = (CKDWORD)Height;
    CKBgfxResolveDrawableSize(Window, Width, Height);
    m_DrawableWidth = (CKDWORD)Width;
    m_DrawableHeight = (CKDWORD)Height;

    bgfx::RendererType::Enum requestedRenderer = bgfx::RendererType::Count;
    if (!CKBgfxParseRequestedRenderer(requestedRenderer)) {
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }
    if (requestedRenderer != bgfx::RendererType::Count &&
        !CKBgfxAllowsShaderTarget(*Desc, CKBgfxShaderProfile(requestedRenderer))) {
        CKBgfxLogf("Init", "requested renderer %s has no matching target in InitDesc.ShaderTargets",
                   CKBgfxRendererTypeName(requestedRenderer));
        CKBgfxReleaseActiveContext(this);
        return CKERR_NOTIMPLEMENTED;
    }

    bgfx::Init init;
    init.type = requestedRenderer;
    if (!CKBgfxFillSDLPlatformData(Window, init.platformData)) {
        CKBgfxLogf("Init", "failed to extract SDL native window data window=%p", Window);
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDPARAMETER;
    }
    CKBgfxLogf("Init",
               "platformData ndt=%p nwh=%p type=%s",
               init.platformData.ndt,
               init.platformData.nwh,
               CKBgfxNativeWindowHandleTypeName(init.platformData.type));
    init.resolution.width = Width;
    init.resolution.height = Height;
    init.resolution.reset = m_ResetFlags;
    init.resolution.maxFrameLatency = CKBGFX_MAX_FRAME_LATENCY;
    init.callback = &m_BgfxCallback;

    if (!bgfx::init(init)) {
        CKBgfxLogf("Init", "bgfx init failed for requested renderer '%s' window=%p size=%dx%d",
                   CKBgfxRendererTypeName(requestedRenderer), Window, Width, Height);
        CKBgfxReleaseActiveContext(this);
        return CKERR_INVALIDOPERATION;
    }

    m_BgfxInitialized = TRUE;
    m_ApiThreadId = VxThread::GetCurrentVxThreadId();
    const bgfx::RendererType::Enum actualRenderer = bgfx::getRendererType();
    if (requestedRenderer != bgfx::RendererType::Count &&
        actualRenderer != requestedRenderer) {
        CKBgfxLogf("Init",
                   "requested renderer %s is unavailable; bgfx selected %s",
                   CKBgfxRendererTypeName(requestedRenderer),
                   CKBgfxRendererTypeName(actualRenderer));
        bgfx::shutdown();
        m_BgfxInitialized = FALSE;
        CKBgfxReleaseActiveContext(this);
        return CKERR_NOTIMPLEMENTED;
    }
    m_RendererType = actualRenderer;
    m_RendererName = CKBgfxRendererTypeName(actualRenderer);
    const CK_SHADER_PROFILE shaderProfile = CKBgfxShaderProfile(actualRenderer);
    if (!CKBgfxAllowsShaderTarget(*Desc, shaderProfile)) {
        CKBgfxLogf("Init", "renderer %s target BGFX/%s is unsupported or absent from InitDesc.ShaderTargets",
                   CKBgfxRendererTypeName(actualRenderer), CKBgfxShaderProfileName(shaderProfile));
        ReleaseBgfx();
        return CKERR_NOTIMPLEMENTED;
    }
    const bgfx::Caps *caps = bgfx::getCaps();
    m_Caps = CKRasterizerDeviceCaps();
    m_Caps.ShaderFormat = CKRST_SHADER_FORMAT_BGFX;
    m_Caps.ShaderProfile = shaderProfile;
    m_Caps.HomogeneousDepth = caps && caps->homogeneousDepth ? TRUE : FALSE;
    m_Caps.OriginBottomLeft = caps && caps->originBottomLeft ? TRUE : FALSE;
    m_CapsDesc = CKBgfxDeviceLimits();
    memset(m_NativeFormatCaps, 0, sizeof(m_NativeFormatCaps));
    if (caps) {
        m_NativeSupported = caps->supported;
        memcpy(m_NativeFormatCaps, caps->formats, sizeof(m_NativeFormatCaps));

        const bgfx::Caps::Limits &limits = caps->limits;
        m_CapsDesc.MaxDrawCalls = limits.maxDrawCalls;
        m_CapsDesc.MaxBlits = limits.maxBlits;
        m_CapsDesc.MaxTextureSize = limits.maxTextureSize;
        m_CapsDesc.MaxTextureLayers = limits.maxTextureLayers;
        m_CapsDesc.MaxRenderViews = XMin((CKDWORD)limits.maxViews,
                                         (CKDWORD)CKRST_MAX_PASSES);
        if (actualRenderer == bgfx::RendererType::Vulkan &&
            m_CapsDesc.MaxRenderViews != 0)
            --m_CapsDesc.MaxRenderViews;
        m_CapsDesc.MaxFrameBuffers = limits.maxFrameBuffers;
        m_CapsDesc.MaxColorAttachments = limits.maxFBAttachments;
        m_CapsDesc.MaxPrograms = limits.maxPrograms;
        m_CapsDesc.MaxShaders = limits.maxShaders;
        m_CapsDesc.MaxTextures = limits.maxTextures;
        m_CapsDesc.MaxTextureBindings = limits.maxTextureSamplers;
        m_CapsDesc.MaxVertexLayouts = limits.maxVertexLayouts;
        m_CapsDesc.MaxVertexStreams = XMin((CKDWORD)limits.maxVertexStreams,
                                           (CKDWORD)CKRST_MAX_VERTEX_STREAMS);
        m_CapsDesc.MaxIndexBuffers = limits.maxIndexBuffers;
        m_CapsDesc.MaxVertexBuffers = limits.maxVertexBuffers;
        m_CapsDesc.MaxDynamicIndexBuffers = limits.maxDynamicIndexBuffers;
        m_CapsDesc.MaxDynamicVertexBuffers = limits.maxDynamicVertexBuffers;
        m_CapsDesc.MaxUniforms = limits.maxUniforms;
        m_CapsDesc.MaxTransientVertexBufferSize = limits.maxTransientVbSize;
        m_CapsDesc.MaxTransientIndexBufferSize = limits.maxTransientIbSize;

        m_CapsDesc.Features = CKRST_DEVCAPS_VERTEX_SHADER |
                              CKRST_DEVCAPS_PIXEL_SHADER |
                              CKRST_DEVCAPS_PASSES |
                              CKRST_DEVCAPS_FRAMEBUFFER |
                              CKRST_DEVCAPS_TRANSIENT_BUFFERS |
                              CKRST_DEVCAPS_SCISSOR |
                              CKRST_DEVCAPS_BUFFER_UPDATE |
                              CKRST_DEVCAPS_TEXTURE_UPDATE |
                              CKRST_DEVCAPS_BLEND_EQUATION |
                              CKRST_DEVCAPS_STENCIL_WRITE_MASK |
                              CKRST_DEVCAPS_TEXTURE_CUBE;
        if (caps->supported & BGFX_CAPS_TEXTURE_READ_BACK)
            m_CapsDesc.Features |= CKRST_DEVCAPS_TEXTURE_READBACK;
        if (caps->supported & BGFX_CAPS_TEXTURE_BLIT)
            m_CapsDesc.Features |= CKRST_DEVCAPS_BLIT;
        if (caps->supported & BGFX_CAPS_INDEX32)
            m_CapsDesc.Features |= CKRST_DEVCAPS_INDEX32;
        if (caps->supported & BGFX_CAPS_TEXTURE_COMPARE_ALL)
            m_CapsDesc.Features |= CKRST_DEVCAPS_TEXTURE_COMPARISON;
        if (caps->supported & BGFX_CAPS_TEXTURE_3D)
            m_CapsDesc.Features |= CKRST_DEVCAPS_TEXTURE_3D;
        if (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_HALF)
            m_CapsDesc.Features |= CKRST_DEVCAPS_VERTEX_ATTRIB_HALF;
        if (caps->supported & BGFX_CAPS_VERTEX_ATTRIB_UINT10)
            m_CapsDesc.Features |= CKRST_DEVCAPS_VERTEX_ATTRIB_UINT10;

        const bgfx::TextureFormat::Enum depthFormats[] = {
            bgfx::TextureFormat::D16,
            bgfx::TextureFormat::D24,
            bgfx::TextureFormat::D24S8,
            bgfx::TextureFormat::D32F,
        };
        const uint64_t depthTextureCaps =
            BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER |
            BGFX_CAPS_FORMAT_TEXTURE_2D;
        for (CKDWORD i = 0; i < sizeof(depthFormats) / sizeof(depthFormats[0]); ++i) {
            if ((m_NativeFormatCaps[depthFormats[i]] & depthTextureCaps) == depthTextureCaps) {
                m_CapsDesc.Features |= CKRST_DEVCAPS_DEPTH_TEXTURE;
                break;
            }
        }
    }
    m_Caps.Features = m_CapsDesc.Features;
    m_Caps.MaxTextureSize = m_CapsDesc.MaxTextureSize;
    m_Caps.MaxTextureBindings = m_CapsDesc.MaxTextureBindings;
    m_Caps.MaxPasses = m_CapsDesc.MaxRenderViews;
    m_Caps.MaxMSAASamples = 16;   // MSAA targets are created on demand (CKRST_TEXTURE_MSAA_Xn)

    CKBgfxLogf("Init", "renderer requested=%s actual=%s",
               CKBgfxRendererTypeName(requestedRenderer), m_RendererName);
    {
        bgfx::RendererType::Enum supported[bgfx::RendererType::Count];
        const uint8_t count = bgfx::getSupportedRenderers((uint8_t)bgfx::RendererType::Count, supported);
        char names[256];
        CKDWORD offset = 0;
        names[0] = '\0';
        for (uint8_t i = 0; i < count && offset + 1 < sizeof(names); ++i) {
            int written = snprintf(names + offset, sizeof(names) - offset,
                                   i == 0 ? "%s" : ",%s",
                                   CKBgfxRendererTypeName(supported[i]));
            if (written <= 0)
                break;
            if ((CKDWORD)written >= sizeof(names) - offset) {
                offset = sizeof(names) - 1;
                break;
            }
            offset += (CKDWORD)written;
        }
        names[sizeof(names) - 1] = '\0';
        CKBgfxLogf("Init", "supported renderers=%s", names);
    }
    if ((m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 || CKBgfxLogEnabled("Config", false)) {
        CKBgfxLogf("DrawMap",
                   "enabled=%u submits=%u resources=%u views=%u markers=%u frame=%u summary=%u renderer=%s profile=%s",
                   (m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 ? 1u : 0u,
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_SUBMITS),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_RESOURCES),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_PASSES),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_MARKERS),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_FRAME),
                   CKBgfxDrawMapChannelEnabled(m_DebugFlags, CKRST_DEBUG_DRAWMAP_SUMMARY),
                   m_RendererName,
                   CKBgfxShaderProfileName(shaderProfile));
    }
    if (caps) {
        CKBgfxLogf("Init",
                   "backend conventions profile=%s(0x%08X) homogeneousDepth=%u originBottomLeft=%u maxTextureSamplers=%u maxUniforms=%u textureCompareAll=%u texture3D=%u rendererMultithreaded=%u",
                   CKBgfxShaderProfileName(shaderProfile),
                   shaderProfile,
                   caps->homogeneousDepth ? 1u : 0u,
                   caps->originBottomLeft ? 1u : 0u,
                   (unsigned)caps->limits.maxTextureSamplers,
                   (unsigned)caps->limits.maxUniforms,
                   (caps->supported & BGFX_CAPS_TEXTURE_COMPARE_ALL) ? 1u : 0u,
                   (caps->supported & BGFX_CAPS_TEXTURE_3D) ? 1u : 0u,
                   (caps->supported & BGFX_CAPS_RENDERER_MULTITHREADED) ? 1u : 0u);
    }
    CKBgfxLogf("Init",
               "texture policy openGLAutoMipWorkaround=%u shaderDepthCompare=%u samplerBaseLevelAlias=%u",
               CKBgfxIsOpenGLRenderer() ? 1u : 0u,
               1u,
               CKBgfxIsOpenGLRenderer() ? 1u : 0u);

    bgfx::setViewRect(0, 0, 0, (uint16_t)Width, (uint16_t)Height);
    bgfx::setViewClear(0,
                        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                        0x000000ff, 1.0f, 0);
    bgfx::touch(0);
    bgfx::frame();
    ConfigureDebug();

    m_BgfxCreated = TRUE;

    return CK_OK;
}

// Releases every bgfx object and shuts bgfx down.
void CKBgfxRasterizerContext::ReleaseBgfx()
{
    if (!m_BgfxInitialized)
        return;
    for (CKDWORD i = 0; i < CKFF_CONSTANT_SLOT_COUNT; ++i)
        m_ConstantData[i].Clear();
    m_Resources->DestroyAll();
    m_DefaultTextures.Clear();
    ReleaseRectClear();
    for (int i = 0; i < m_BgfxReadbacks.Size(); ++i) {
        std::shared_ptr<NativeReadback> &ticket = m_BgfxReadbacks[i];
        if (bgfx::isValid(ticket->Snapshot)) bgfx::destroy(ticket->Snapshot);
        ticket->Snapshot = BGFX_INVALID_HANDLE;
    }
    bgfx::shutdown();
    m_CompletedSubmitId = m_LastSubmitId;
    m_SubmittedFrames.Clear();
    for (int i = 0; i < m_BgfxReadbacks.Size(); ++i)
        m_BgfxReadbacks[i]->Error = CKERR_INVALIDOPERATION;
    m_BgfxReadbacks.Clear();
    m_BgfxInitialized = FALSE;
    m_BgfxCreated = FALSE;
    m_RendererType = bgfx::RendererType::Count;
    CKBgfxReleaseActiveContext(this);
}

void CKBgfxRasterizerContext::Shutdown()
{
    if (!m_BgfxInitialized)
        return;
    m_BgfxShuttingDown.store(TRUE, std::memory_order_release);
    m_FrameInProgress = FALSE;
    m_PassOpen = FALSE;
    m_NextView = 0;
    m_LastFrameViewCount = 0;
    m_TransientVBCount = 0;
    m_TransientIBCount = 0;
    ReleaseBgfx();
    CKBgfxCloseLogFile();
}

CKERROR CKBgfxRasterizerContext::Resize(int PosX, int PosY, int Width, int Height)
{
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (Width <= 0 || Height <= 0 || Width > 0xffff || Height > 0xffff)
        return CKERR_INVALIDPARAMETER;
    if (m_FrameInProgress)
        return CKERR_INVALIDOPERATION;
    m_BgfxPosX = PosX;
    m_BgfxPosY = PosY;
    m_BgfxWidth = (CKDWORD)Width;
    m_BgfxHeight = (CKDWORD)Height;
    CKBgfxResolveDrawableSize(m_BgfxWindow, Width, Height);
    m_DrawableWidth = (CKDWORD)Width;
    m_DrawableHeight = (CKDWORD)Height;

    bgfx::reset((uint32_t)Width, (uint32_t)Height, m_ResetFlags);
    bgfx::setViewRect(0, 0, 0, (uint16_t)Width, (uint16_t)Height);

    return CK_OK;
}

CKBOOL CKBgfxRasterizerContext::IsIdle() const
{
    return !m_Frame.Open && !m_FrameInProgress && !m_PassOpen ? TRUE : FALSE;
}

CKRECT CKBgfxRasterizerContext::WindowPixelRect(const CKRECT &rect) const
{
    // FFP and readbacks stay in logical pixels. Only window attachments use
    // drawable pixels, including high-DPI windows and scaled client targets.
    CKRECT pixels;
    pixels.left = CKBgfxScaleWindowCoordinate(
        rect.left, m_DrawableWidth, m_BgfxWidth, false);
    pixels.top = CKBgfxScaleWindowCoordinate(
        rect.top, m_DrawableHeight, m_BgfxHeight, false);
    pixels.right = CKBgfxScaleWindowCoordinate(
        rect.right, m_DrawableWidth, m_BgfxWidth, true);
    pixels.bottom = CKBgfxScaleWindowCoordinate(
        rect.bottom, m_DrawableHeight, m_BgfxHeight, true);
    return pixels;
}

CKERROR CKBgfxRasterizerContext::GetDeviceStatus() const
{
    if (!m_BgfxInitialized || !m_BgfxCreated)
        return CKERR_INVALIDRENDERCONTEXT;
    if (m_BgfxShuttingDown.load(std::memory_order_acquire))
        return CKERR_INVALIDOPERATION;
    return m_FatalError.load(std::memory_order_acquire);
}

void CKBgfxRasterizerContext::LatchFatalError(CKERROR Error)
{
    if (Error == CK_OK)
        return;
    CKERROR expected = CK_OK;
    m_FatalError.compare_exchange_strong(
        expected, Error, std::memory_order_acq_rel,
        std::memory_order_relaxed);
}

void CKBgfxRasterizerContext::RecordFatalError(CKERROR Error)
{
    m_DebugFatalCount.fetch_add(1, std::memory_order_relaxed);
    LatchFatalError(Error);
}

CKRECT CKBgfxRasterizerContext::GetWindowViewRectForTests() const
{
    CKRECT rect = {};
    for (CKDWORD view = 0; view < m_LastFrameViewCount; ++view)
        if (!m_ViewFrameBuffer[view])
            rect = m_ViewRect[view];
    return rect;
}

CKDWORD CKBgfxRasterizerContext::ExchangeNextViewForTests(CKDWORD Next)
{
    const CKDWORD previous = m_NextView;
    m_NextView = Next == UINT32_MAX ? m_CapsDesc.MaxRenderViews : Next;
    return previous;
}

CKERROR CKBgfxRasterizerContext::GetTextureFormatCaps(VX_PIXELFORMAT Format,
                                                       CKBgfxTextureFormatCaps *Caps) const
{
    if (!Caps)
        return CKERR_INVALIDPARAMETER;
    if (!m_BgfxInitialized || !m_BgfxCreated || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    bgfx::TextureFormat::Enum nativeFormat;
    if (!CKBgfxTryTextureStorageFormat(Format, nativeFormat))
        return CKERR_INVALIDPARAMETER;
    *Caps = CKBgfxTextureFormatCaps();
    Caps->Format = Format;
    const CKBOOL allowReadback =
        (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_READBACK) != 0 &&
        CKBgfxCanExposeReadback(Format, nativeFormat)
            ? TRUE : FALSE;
    Caps->Caps = CKBgfxMapFormatCaps(
        m_NativeFormatCaps[nativeFormat], allowReadback, FALSE);
    if (CKBgfxIsBumpLuminanceFormat(Format))
        Caps->Caps &= CKRST_FORMAT_CAPS_TEXTURE_2D;
    return CK_OK;
}
// ---------------------------------------------------------------------------
// Accessor helpers
// ---------------------------------------------------------------------------

CKBgfxShaderRecord *CKBgfxRasterizerContext::GetShader(CKDWORD Handle)
{
    return m_Resources->Shaders.Get(Handle);
}
CKBgfxProgramRecord *CKBgfxRasterizerContext::GetProgram(CKDWORD Handle)
{
    return m_Resources->Programs.Get(Handle);
}
CKBgfxVertexLayoutRecord *CKBgfxRasterizerContext::GetVertexLayout(CKDWORD Handle)
{
    return m_Resources->VertexLayouts.Get(Handle);
}
CKBgfxVertexBufferRecord *CKBgfxRasterizerContext::GetVertexBuffer(CKDWORD Handle)
{
    return m_Resources->VertexBuffers.Get(Handle);
}
CKBgfxIndexBufferRecord *CKBgfxRasterizerContext::GetIndexBuffer(CKDWORD Handle)
{
    return m_Resources->IndexBuffers.Get(Handle);
}
CKBgfxTextureRecord *CKBgfxRasterizerContext::GetTexture(CKDWORD Handle)
{
    return m_Resources->Textures.Get(Handle);
}
CKBgfxFrameBufferRecord *CKBgfxRasterizerContext::GetFrameBuffer(CKDWORD Handle)
{
    return m_Resources->FrameBuffers.Get(Handle);
}
