#include "CKSdlGpuRasterizerContext.h"

// CKSdlGpuRasterizerContext render-target selection and lifetime.

// ===========================================================================
// Render targets
// ===========================================================================

CKBOOL CKSdlGpuRasterizerContext::ReleaseTarget()
{
    if (m_TargetFrameBuffer &&
        !IsNativeObjectAlive(m_TargetFrameBuffer, CKRST_OBJ_RENDERTARGET))
        return FALSE;
    if (m_TargetDepthTexture &&
        !IsNativeObjectAlive(m_TargetDepthTexture, CKRST_OBJ_TEXTURE))
        return FALSE;
    if (m_TargetFrameBuffer &&
        DestroyObject(m_TargetFrameBuffer, CKRST_OBJ_RENDERTARGET) != CK_OK)
        return FALSE;
    m_TargetFrameBuffer = 0;
    if (m_TargetDepthTexture &&
        DestroyObject(m_TargetDepthTexture, CKRST_OBJ_TEXTURE) != CK_OK) {
        // The framebuffer is already gone, so the old target can no longer
        // remain selected. Keep the depth handle for a later cleanup retry.
        m_TargetState.Reset();
        m_FFP.SetRenderTargetActive(FALSE);
        UpdateTargetExtents();
        UpdateAlphaTestPrecision();
        return FALSE;
    }
    m_TargetDepthTexture = 0;
    m_TargetState.Reset();
    m_FFP.SetRenderTargetActive(FALSE);
    UpdateTargetExtents();
    UpdateAlphaTestPrecision();
    return TRUE;
}

void CKSdlGpuRasterizerContext::UpdateAlphaTestPrecision()
{
    const CKTextureDesc *texture =
        m_TargetState.IsActive()
            ? m_PublicResources.FindTexture(m_TargetState.Texture) : NULL;
    m_FFP.SetAlphaTestPrecision(
        CKFFTargetAlphaTestPrecision(texture, m_Bpp));
    m_FFP.SetColorTargetFormat(CKFFTargetColorFormat(texture, m_Bpp));
}

CKBOOL CKSdlGpuRasterizerContext::SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_FrameState.IsSceneActive() || m_FrameState.IsOverlayActive()) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    if (Texture == 0) {
        if ((m_TargetState.IsActive() || m_TargetFrameBuffer || m_TargetDepthTexture) && !ReleaseTarget())
            return FALSE;
        return TRUE;
    }
    const CKTextureDesc *texture = m_PublicResources.FindTexture(Texture);
    if (!texture) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    CKFFRenderTargetState target;
    if (!target.Set(Texture, *texture, Width, Height, Face)) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    CKDepthTextureDesc depthDesc;
    depthDesc.Width = target.Width;
    depthDesc.Height = target.Height;
    const CKBOOL needsStencil = m_StencilBpp > 0;
    depthDesc.Format = needsStencil ? CKRST_DEPTHFMT_D24S8 : CKRST_DEPTHFMT_D24;
    CKDWORD depthTexture = 0;
    CKERROR depthErr = CreateDepthTexture(&depthDesc, &depthTexture);
    if (depthErr != CK_OK && !needsStencil) {
        depthDesc.Format = CKRST_DEPTHFMT_D16;
        depthErr = CreateDepthTexture(&depthDesc, &depthTexture);
    }
    if (depthErr != CK_OK) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    CKRenderTargetDesc rtDesc;
    rtDesc.ColorTexture = Texture;
    rtDesc.ColorMip = 0;
    rtDesc.ColorLayer = (CKDWORD)target.Face;
    rtDesc.DepthTexture = depthTexture;
    CKDWORD frameBuffer = 0;
    if (CreateRenderTarget(&rtDesc, &frameBuffer) != CK_OK) {
        DestroyObject(depthTexture, CKRST_OBJ_TEXTURE);
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    // Keep the previously selected target intact until the complete native
    // realization of the replacement exists.
    if (!ReleaseTarget()) {
        DestroyObject(frameBuffer, CKRST_OBJ_RENDERTARGET);
        DestroyObject(depthTexture, CKRST_OBJ_TEXTURE);
        return FALSE;
    }
    m_TargetState = target;
    m_TargetState.DepthFormat = depthDesc.Format;
    m_TargetFrameBuffer = frameBuffer;
    m_TargetDepthTexture = depthTexture;
    m_FFP.SetRenderTargetActive(TRUE);
    UpdateTargetExtents();
    UpdateAlphaTestPrecision();
    return TRUE;
}

CKBOOL CKSdlGpuRasterizerContext::SetBackBufferTexture(CKDWORD Texture)
{
    // Do not touch Context state, including diagnostics, from a caller that
    // is not the render thread selected at Create.
    if (SDL_GetCurrentThreadID() != Thread)
        return FALSE;
    if (!m_Created || m_ShuttingDown || !Ready() || m_FrameState.Open ||
        m_FrameState.PassOpen || PassOpen || Draws.Size() != 0) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    if (Texture == m_BackBufferTexture)
        return TRUE;
    if (Texture == 0) {
        if (!m_Present.SetNativeTargetTexture(0, 0, 0))
            return FALSE;
        m_BackBufferTexture = 0;
        m_FrameState.NativePresented = FALSE;
        m_FrameState.InternalTargets = FALSE;
        m_FrameState.TargetDecided = FALSE;
        return TRUE;
    }

    const CKTextureDesc *desc = m_PublicResources.FindTexture(Texture);
    const std::shared_ptr<CKSdlGpuTexture> native = Textures.Get(Texture);
    const CKDWORD samples = desc ? CKRSTTextureMSAASamples(desc->Flags) : 0;
    if (!desc || !native || native->Depth || !native->Image ||
        !(desc->Flags & CKRST_TEXTURE_RENDERTARGET) ||
        (desc->Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP)) ||
        desc->Format.Width != m_Width || desc->Format.Height != m_Height ||
        desc->Depth > 1 || desc->MipMapCount == CKRST_MIPMAP_GENERATE ||
        desc->MipMapCount > 1 || samples > 1 ||
        native->Info.type != SDL_GPU_TEXTURETYPE_2D ||
        native->Info.width != (Uint32)m_Width ||
        native->Info.height != (Uint32)m_Height ||
        native->Info.num_levels != 1 || native->Samples != 1 ||
        native->Info.sample_count != SDL_GPU_SAMPLECOUNT_1 ||
        (native->Info.usage & (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                               SDL_GPU_TEXTUREUSAGE_SAMPLER)) !=
            (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    // The present stage builds every dependent object into a candidate first;
    // failure leaves the previous native target and binding untouched.
    if (!m_Present.SetNativeTargetTexture(Texture, (CKDWORD)m_Width,
                                          (CKDWORD)m_Height)) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    // Opaque initialization belongs to this presentation binding, not to the
    // texture's earlier life as an ordinary RTT. The first backbuffer pass
    // must therefore establish alpha one even when that RTT was already used.
    native->AttachmentInitialized = false;
    m_BackBufferTexture = Texture;
    m_FrameState.NativePresented = FALSE;
    m_FrameState.InternalTargets = FALSE;
    m_FrameState.TargetDecided = FALSE;
    return TRUE;
}
