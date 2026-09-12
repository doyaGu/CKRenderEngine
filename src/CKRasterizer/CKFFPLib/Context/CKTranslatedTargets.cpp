// CKTranslatedContext render-target selection and lifetime.

#include "CKFFRasterizerContextInternal.h"
#include "CKFFUniformState.h"

// ===========================================================================
// Render targets
// ===========================================================================

void CKTranslatedContext::ReleaseTarget()
{
    if (m_Backend) {
        if (m_TargetFrameBuffer)
            m_Backend->DestroyObject(m_TargetFrameBuffer, CKRST_OBJ_RENDERTARGET);
        if (m_TargetDepthTexture)
            m_Backend->DestroyObject(m_TargetDepthTexture, CKRST_OBJ_TEXTURE);
    }
    m_TargetFrameBuffer = 0;
    m_TargetDepthTexture = 0;
    m_Target = 0;
    m_TargetFace = CKRST_CUBEFACE_XPOS;
    m_TargetWidth = 0;
    m_TargetHeight = 0;
    m_FFP.SetRenderTargetActive(FALSE);
    UpdateTargetExtents();
}

void CKTranslatedContext::UpdateAlphaTestPrecision()
{
    if (m_Target) {
        const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, m_Target);
        if (resource) {
            m_FFP.SetAlphaTestPrecision(CKFFAlphaTestPrecisionForFormat(resource->Texture.Format));
            return;
        }
    }
    VxImageDescEx backbuffer;
    VxPixelFormat2ImageDesc(m_Bpp == 16 ? _16_RGB565 : _32_ARGB8888, backbuffer);
    m_FFP.SetAlphaTestPrecision(CKFFAlphaTestPrecisionForFormat(backbuffer));
}

CKBOOL CKTranslatedContext::SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    if (m_Frame.IsSceneActive() || m_Frame.IsOverlayActive()) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }
    if (Texture == 0) {
        if (m_Target)
            ReleaseTarget();
        UpdateAlphaTestPrecision();
        return TRUE;
    }
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    const int textureWidth = resource->Texture.Format.Width;
    const int textureHeight = resource->Texture.Format.Height;
    if ((resource->Texture.Flags & CKRST_TEXTURE_RENDERTARGET) == 0 ||
        (CKDWORD)Face >= CKRST_CUBEFACE_COUNT || (!cube && Face != CKRST_CUBEFACE_XPOS) ||
        (cube && textureWidth != textureHeight) ||
        (Width > 0 && Width != textureWidth) || (Height > 0 && Height != textureHeight)) {
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    ReleaseTarget();

    CKBackendDepthDesc depthDesc;
    depthDesc.Width = (CKDWORD)textureWidth;
    depthDesc.Height = (CKDWORD)textureHeight;
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

    CKBackendRenderTargetDesc rtDesc;
    rtDesc.ColorTexture = Texture;
    rtDesc.ColorMip = 0;
    rtDesc.ColorLayer = (CKDWORD)Face;
    rtDesc.DepthTexture = depthTexture;
    CKDWORD frameBuffer = 0;
    if (m_Backend->CreateRenderTarget(&rtDesc, &frameBuffer) != CK_OK) {
        m_Backend->DestroyObject(depthTexture, CKRST_OBJ_TEXTURE);
        Diag(CKRST_DIAG_INVALID_TARGET);
        return FALSE;
    }

    m_Target = Texture;
    m_TargetFace = Face;
    m_TargetWidth = (CKDWORD)textureWidth;
    m_TargetHeight = (CKDWORD)textureHeight;
    m_TargetFrameBuffer = frameBuffer;
    m_TargetDepthTexture = depthTexture;
    m_FFP.SetRenderTargetActive(TRUE);
    UpdateTargetExtents();
    UpdateAlphaTestPrecision();
    return TRUE;
}

