#include "CKRasterizer.h"

void CKRasterizerEncoder::SetState(CKDrawState)
{
}

void CKRasterizerEncoder::SetStencilRef(CKDWORD)
{
}

void CKRasterizerEncoder::SetStencilMask(CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetScissor(const CKRECT *)
{
}

void CKRasterizerEncoder::SetPointSize(float)
{
}

void CKRasterizerEncoder::SetTransform(CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetVertexLayout(CKDWORD)
{
}

void CKRasterizerEncoder::SetVertexBuffer(CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetIndexBuffer(CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetInstanceBuffer(CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetTransientVertexBuffer(CKDWORD, CKTransientVertexBuffer *)
{
}

void CKRasterizerEncoder::SetTransientIndexBuffer(CKTransientIndexBuffer *)
{
}

void CKRasterizerEncoder::SetTransientInstanceBuffer(CKDWORD, CKTransientInstanceBuffer *)
{
}

void CKRasterizerEncoder::SetTexture(CKDWORD, CKDWORD, CKDWORD, CKSamplerDesc *)
{
}

void CKRasterizerEncoder::SetUniform(CKDWORD, const void *, CKDWORD)
{
}

void CKRasterizerEncoder::SetComputeBuffer(CKDWORD, CKDWORD, CK_ACCESS_MODE)
{
}

void CKRasterizerEncoder::SetComputeImage(CKDWORD, CKDWORD, CKDWORD, CK_ACCESS_MODE)
{
}

void CKRasterizerEncoder::SetCondition(CKDWORD, CKBOOL)
{
}

void CKRasterizerEncoder::SetMarker(CKSTRING)
{
}

void CKRasterizerEncoder::Submit(CKRenderView, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SubmitOcclusionQuery(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SubmitIndirect(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::Dispatch(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::DispatchIndirect(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::Touch(CKRenderView)
{
}

void CKRasterizerEncoder::Blit(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, const CKRECT *)
{
}

CKRasterizerContext::CKRasterizerContext()
    : m_Driver(NULL),
      m_PosX(0),
      m_PosY(0),
      m_Width(0),
      m_Height(0),
      m_Bpp(0),
      m_ZBpp(0),
      m_StencilBpp(0),
      m_Fullscreen(FALSE),
      m_RefreshRate(0),
      m_Window(NULL)
{
}

CKERROR CKRasterizerContext::CreateVertexBuffer(CKDWORD, CKVertexBufferDesc *, const void *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateIndexBuffer(CKDWORD, CKIndexBufferDesc *, CKBOOL, const void *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateTexture(CKDWORD, CKTextureDesc *, const VxImageDescEx *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateShader(CKDWORD, CKShaderDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateProgram(CKDWORD, CKProgramDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateUniform(CKDWORD, CKUniformDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateVertexLayout(CKDWORD, CKVertexLayoutDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateFrameBuffer(CKDWORD, CKFrameBufferDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateDepthTexture(CKDWORD, CKDepthTextureDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateOcclusionQuery(CKDWORD, CKOcclusionQueryDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateIndirectBuffer(CKDWORD, CKIndirectBufferDesc *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::DeleteObject(CKDWORD, CKDWORD)
{
    return CK_OK;
}

void CKRasterizerContext::FlushObjects(CKDWORD)
{
}

CKERROR CKRasterizerContext::UpdateVertexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::UpdateIndexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::UpdateTexture(CKDWORD, CKDWORD, CKDWORD, const CKRECT *, const VxImageDescEx *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::ReadTexture(CKDWORD, CKDWORD, VxImageDescEx *)
{
    return CKERR_NOTIMPLEMENTED;
}

CKERROR CKRasterizerContext::ReadFrameBuffer(CKDWORD, VxImageDescEx *)
{
    return CKERR_NOTIMPLEMENTED;
}

CK_OCCLUSION_RESULT CKRasterizerContext::GetOcclusionResult(CKDWORD, CKDWORD *)
{
    return CKRST_OCCLUSION_NORESULT;
}

void CKRasterizerContext::SetPaletteColor(CKDWORD, CKDWORD)
{
}

void CKRasterizerContext::DbgTextClear(CKDWORD, CKBOOL)
{
}

void CKRasterizerContext::DbgTextPrintf(CKWORD, CKWORD, CKDWORD, CKSTRING, ...)
{
}

void CKRasterizerContext::DbgTextImage(CKWORD, CKWORD, CKWORD, CKWORD, const void *, CKWORD)
{
}

void CKRasterizerContext::SetDebug(CKDWORD)
{
}

const CKRenderStats *CKRasterizerContext::GetStats()
{
    static CKRenderStats stats = {};
    return &stats;
}

void CKRasterizerContext::SetResourceName(CKDWORD, CKDWORD, CKSTRING)
{
}

CKDWORD CKRasterizerContext::GetShaderUniforms(CKDWORD, CKDWORD *, CKDWORD)
{
    return 0;
}

void CKRasterizerContext::GetUniformInfo(CKDWORD, CKUniformInfo *)
{
}

CKDWORD CKRasterizerContext::GetFrameBufferTexture(CKDWORD, CKDWORD)
{
    return 0;
}

CKBOOL CKRasterizerContext::IsTextureValid(CKDWORD, CKBOOL, CKWORD, CKDWORD, CKDWORD)
{
    return TRUE;
}

CKBOOL CKRasterizerContext::IsFrameBufferValid(CKDWORD, const CKFrameBufferAttachmentDesc *,
                                               const CKFrameBufferAttachmentDesc *)
{
    return TRUE;
}

void CKRasterizerContext::CalcTextureSize(CKTextureInfo *, CKWORD, CKWORD, CKWORD, CKBOOL, CKBOOL, CKWORD, CKDWORD)
{
}

void CKRasterizerContext::RequestScreenShot(CKDWORD, CKScreenShotCallback)
{
}

CKERROR CKRasterizerContext::SetViewName(CKRenderView, CKSTRING)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewRect(CKRenderView, const CKRECT &)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewScissor(CKRenderView, const CKRECT *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewClear(CKRenderView, CKDWORD, CKDWORD, float, CKDWORD)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewTransform(CKRenderView, const VxMatrix *, const VxMatrix *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewFrameBuffer(CKRenderView, CKDWORD)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewMode(CKRenderView, CK_VIEW_MODE)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewOrder(CKRenderView, CKWORD, const CKRenderView *)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::ResetView(CKRenderView)
{
    return CK_OK;
}

CKERROR CKRasterizerContext::TouchView(CKRenderView)
{
    return CK_OK;
}

CKDWORD CKRasterizerContext::AllocTransform(VxMatrix *, CKDWORD)
{
    return 0;
}

CKBOOL CKRasterizerContext::AllocTransientVertexBuffer(CKTransientVertexBuffer *, CKDWORD, CKDWORD)
{
    return FALSE;
}

CKBOOL CKRasterizerContext::AllocTransientIndexBuffer(CKTransientIndexBuffer *, CKDWORD, CKBOOL)
{
    return FALSE;
}

CKBOOL CKRasterizerContext::AllocTransientInstanceBuffer(CKTransientInstanceBuffer *, CKDWORD, CKDWORD)
{
    return FALSE;
}

CKDWORD CKRasterizerContext::GetAvailTransientVertexBuffer(CKDWORD, CKDWORD)
{
    return 0;
}

CKDWORD CKRasterizerContext::GetAvailTransientIndexBuffer(CKDWORD, CKBOOL)
{
    return 0;
}

CKDWORD CKRasterizerContext::GetAvailTransientInstanceBuffer(CKDWORD, CKDWORD)
{
    return 0;
}

CKRasterizerEncoder *CKRasterizerContext::BeginEncoder()
{
    static CKRasterizerEncoder encoder;
    return &encoder;
}

void CKRasterizerContext::EndEncoder(CKRasterizerEncoder *)
{
}

CKERROR CKRasterizerContext::Frame(CKRST_FRAME_SYNC_MODE)
{
    return CK_OK;
}
