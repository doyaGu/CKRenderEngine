// FFPRecordingContext render-target selection and lifetime.

#include "FFPRecordingContext.h"

// ===========================================================================
// Render targets
// ===========================================================================

CKBOOL FFPRecordingContext::ReleaseTarget()
{
    if (m_Backend) {
        if (m_TargetFrameBuffer &&
            !m_Backend->IsObjectAlive(m_TargetFrameBuffer, CKRST_OBJ_RENDERTARGET))
            return FALSE;
        if (m_TargetDepthTexture &&
            !m_Backend->IsObjectAlive(m_TargetDepthTexture, CKRST_OBJ_TEXTURE))
            return FALSE;
        if (m_TargetFrameBuffer &&
            m_Backend->DestroyObject(m_TargetFrameBuffer, CKRST_OBJ_RENDERTARGET) != CK_OK)
            return FALSE;
    }
    m_TargetFrameBuffer = 0;
    if (m_Backend && m_TargetDepthTexture &&
        m_Backend->DestroyObject(m_TargetDepthTexture, CKRST_OBJ_TEXTURE) != CK_OK) {
        // The framebuffer is already gone, so the old target can no
        // longer remain selected. Retain the depth handle for retry.
        m_Target.Reset();
        m_FFP.SetRenderTargetActive(FALSE);
        UpdateTargetExtents();
        UpdateAlphaTestPrecision();
        return FALSE;
    }
    m_TargetDepthTexture = 0;
    m_Target.Reset();
    m_FFP.SetRenderTargetActive(FALSE);
    UpdateTargetExtents();
    UpdateAlphaTestPrecision();
    return TRUE;
}

void FFPRecordingContext::UpdateAlphaTestPrecision()
{
    const CKTextureDesc *texture = m_Target.IsActive()
        ? m_PublicResources.FindTexture(m_Target.Texture) : NULL;
    m_FFP.SetAlphaTestPrecision(CKFFTargetAlphaTestPrecision(
        texture, m_Bpp));
}

CKBOOL FFPRecordingContext::SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Frame.IsSceneActive() || m_Frame.IsOverlayActive()) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    if (Texture == 0) {
        if ((m_Target.IsActive() || m_TargetFrameBuffer || m_TargetDepthTexture) && !ReleaseTarget())
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
    CKERROR depthErr = m_Backend->CreateDepthTexture(&depthDesc, &depthTexture);
    if (depthErr != CK_OK && !needsStencil) {
        depthDesc.Format = CKRST_DEPTHFMT_D16;
        depthErr = m_Backend->CreateDepthTexture(&depthDesc, &depthTexture);
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
    if (m_Backend->CreateRenderTarget(&rtDesc, &frameBuffer) != CK_OK) {
        m_Backend->DestroyObject(depthTexture, CKRST_OBJ_TEXTURE);
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    if (!ReleaseTarget()) {
        m_Backend->DestroyObject(frameBuffer, CKRST_OBJ_RENDERTARGET);
        m_Backend->DestroyObject(depthTexture, CKRST_OBJ_TEXTURE);
        return FALSE;
    }
    m_Target = target;
    m_TargetFrameBuffer = frameBuffer;
    m_TargetDepthTexture = depthTexture;
    m_FFP.SetRenderTargetActive(TRUE);
    UpdateTargetExtents();
    UpdateAlphaTestPrecision();
    return TRUE;
}
