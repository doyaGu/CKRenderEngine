#include "CKRasterizer.h"

class CKNULLRasterizerEncoder : public CKRasterizerEncoder {
public:
    void SetState(CKDrawState State) override {}
    void SetStencilRef(CKDWORD Ref) override {}
    void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask) override {}
    void SetScissor(const CKRECT *Rect) override {}
    void SetPointSize(float Size) override {}
    void SetTransform(CKDWORD TransformIndex, CKDWORD Count) override {}
    void SetVertexLayout(CKDWORD Layout) override {}
    void SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                         CKDWORD StartVertex, CKDWORD VertexCount) override {}
    void SetIndexBuffer(CKDWORD Buffer,
                        CKDWORD StartIndex, CKDWORD IndexCount) override {}
    void SetInstanceBuffer(CKDWORD Stream, CKDWORD Buffer,
                           CKDWORD StartInstance, CKDWORD InstanceCount) override {}
    void SetTransientVertexBuffer(CKDWORD Stream,
                                  CKTransientVertexBuffer *Buffer) override {}
    void SetTransientIndexBuffer(CKTransientIndexBuffer *Buffer) override {}
    void SetTransientInstanceBuffer(CKDWORD Stream,
                                    CKTransientInstanceBuffer *Buffer) override {}
    void SetTexture(CKDWORD Stage, CKDWORD Uniform,
                    CKDWORD Texture, CKSamplerDesc *Sampler) override {}
    void SetUniform(CKDWORD Uniform, const void *Data, CKDWORD Count) override {}
    void SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                          CK_ACCESS_MODE Access) override {}
    void SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                         CKDWORD Mip, CK_ACCESS_MODE Access) override {}
    void SetCondition(CKDWORD Query, CKBOOL Visible) override {}
    void SetMarker(CKSTRING Name) override {}
    void Submit(CKRenderView View, CKDWORD Program,
                CKDWORD Depth, CKDWORD Flags) override {}
    void SubmitOcclusionQuery(CKRenderView View, CKDWORD Program,
                              CKDWORD Query, CKDWORD Depth, CKDWORD Flags) override {}
    void SubmitIndirect(CKRenderView View, CKDWORD Program,
                        CKDWORD IndirectBuffer, CKDWORD Start, CKDWORD Count,
                        CKDWORD Depth, CKDWORD Flags) override {}
    void Dispatch(CKRenderView View, CKDWORD Program,
                  CKDWORD NumX, CKDWORD NumY, CKDWORD NumZ, CKDWORD Flags) override {}
    void DispatchIndirect(CKRenderView View, CKDWORD Program,
                          CKDWORD IndirectBuffer, CKDWORD Start, CKDWORD Count,
                          CKDWORD Flags) override {}
    void Touch(CKRenderView View) override {}
    void Blit(CKRenderView View, CKDWORD DstTexture, CKDWORD DstMip,
              CKDWORD DstX, CKDWORD DstY,
              CKDWORD SrcTexture, CKDWORD SrcMip, const CKRECT *SrcRect) override {}
};

class CKNULLRasterizerContext : public CKRasterizerContext {
public:
    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY,
                  int Width, int Height, int Bpp,
                  CKBOOL Fullscreen, int RefreshRate,
                  int Zbpp, int StencilBpp) override
    {
        m_Window = Window;
        m_PosX = PosX;
        m_PosY = PosY;
        m_Width = Width;
        m_Height = Height;
        m_Bpp = (Bpp > 0) ? Bpp : 32;
        m_ZBpp = (Zbpp > 0) ? Zbpp : 24;
        m_StencilBpp = (StencilBpp > 0) ? StencilBpp : 8;
        m_Fullscreen = Fullscreen;
        m_RefreshRate = RefreshRate;
        return TRUE;
    }

    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD) override
    {
        m_PosX = PosX;
        m_PosY = PosY;
        if (Width > 0)
            m_Width = Width;
        if (Height > 0)
            m_Height = Height;
        return TRUE;
    }

    CKERROR CreateVertexBuffer(CKDWORD, CKVertexBufferDesc *, const void *) override { return CK_OK; }
    CKERROR CreateIndexBuffer(CKDWORD, CKIndexBufferDesc *, CKBOOL, const void *) override { return CK_OK; }
    CKERROR CreateTexture(CKDWORD, CKTextureDesc *, const VxImageDescEx *) override { return CK_OK; }
    CKERROR CreateShader(CKDWORD, CKShaderDesc *) override { return CK_OK; }
    CKERROR CreateProgram(CKDWORD, CKProgramDesc *) override { return CK_OK; }
    CKERROR CreateUniform(CKDWORD, CKUniformDesc *) override { return CK_OK; }
    CKERROR CreateVertexLayout(CKDWORD, CKVertexLayoutDesc *) override { return CK_OK; }
    CKERROR CreateFrameBuffer(CKDWORD, CKFrameBufferDesc *) override { return CK_OK; }
    CKERROR CreateDepthTexture(CKDWORD, CKDepthTextureDesc *) override { return CK_OK; }
    CKERROR CreateOcclusionQuery(CKDWORD, CKOcclusionQueryDesc *) override { return CK_OK; }
    CKERROR CreateIndirectBuffer(CKDWORD, CKIndirectBufferDesc *) override { return CK_OK; }
    CKERROR DeleteObject(CKDWORD, CKDWORD) override { return CK_OK; }
    void FlushObjects(CKDWORD) override {}

    CKERROR UpdateVertexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *) override { return CK_OK; }
    CKERROR UpdateIndexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *) override { return CK_OK; }
    CKERROR UpdateTexture(CKDWORD, CKDWORD, CKDWORD, const CKRECT *, const VxImageDescEx *) override { return CK_OK; }

    CKERROR ReadTexture(CKDWORD, CKDWORD, VxImageDescEx *) override { return CKERR_NOTIMPLEMENTED; }
    CKERROR ReadFrameBuffer(CKDWORD, VxImageDescEx *) override { return CKERR_NOTIMPLEMENTED; }

    CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD, CKDWORD *) override { return CKRST_OCCLUSION_NORESULT; }

    void SetPaletteColor(CKDWORD, CKDWORD) override {}
    void DbgTextClear(CKDWORD, CKBOOL) override {}
    void DbgTextPrintf(CKWORD, CKWORD, CKDWORD, CKSTRING, ...) override {}
    void DbgTextImage(CKWORD, CKWORD, CKWORD, CKWORD, const void *, CKWORD) override {}
    void SetDebug(CKDWORD) override {}
    const CKRenderStats *GetStats() override { return &m_Stats; }
    void SetResourceName(CKDWORD, CKDWORD, CKSTRING) override {}
    CKDWORD GetShaderUniforms(CKDWORD, CKDWORD *, CKDWORD) override { return 0; }
    void GetUniformInfo(CKDWORD, CKUniformInfo *) override {}
    CKDWORD GetFrameBufferTexture(CKDWORD, CKDWORD) override { return 0; }
    CKBOOL IsTextureValid(CKDWORD, CKBOOL, CKWORD, CKDWORD, CKDWORD) override { return TRUE; }
    CKBOOL IsFrameBufferValid(CKDWORD, const CKFrameBufferAttachmentDesc *,
                              const CKFrameBufferAttachmentDesc *) override { return TRUE; }
    void CalcTextureSize(CKTextureInfo *, CKWORD, CKWORD, CKWORD, CKBOOL, CKBOOL, CKWORD, CKDWORD) override {}
    void RequestScreenShot(CKDWORD, CKScreenShotCallback) override {}

    CKERROR SetViewName(CKRenderView, CKSTRING) override { return CK_OK; }
    CKERROR SetViewRect(CKRenderView, const CKRECT &) override { return CK_OK; }
    CKERROR SetViewScissor(CKRenderView, const CKRECT *) override { return CK_OK; }
    CKERROR SetViewClear(CKRenderView, CKDWORD, CKDWORD, float, CKDWORD) override { return CK_OK; }
    CKERROR SetViewTransform(CKRenderView, const VxMatrix *, const VxMatrix *) override { return CK_OK; }
    CKERROR SetViewFrameBuffer(CKRenderView, CKDWORD) override { return CK_OK; }
    CKERROR SetViewMode(CKRenderView, CK_VIEW_MODE) override { return CK_OK; }
    CKERROR SetViewOrder(CKRenderView, CKWORD, const CKRenderView *) override { return CK_OK; }
    CKERROR ResetView(CKRenderView) override { return CK_OK; }
    CKERROR TouchView(CKRenderView) override { return CK_OK; }

    CKDWORD AllocTransform(VxMatrix *, CKDWORD) override { return 0; }

    CKBOOL AllocTransientVertexBuffer(CKTransientVertexBuffer *, CKDWORD, CKDWORD) override { return FALSE; }
    CKBOOL AllocTransientIndexBuffer(CKTransientIndexBuffer *, CKDWORD, CKBOOL) override { return FALSE; }
    CKBOOL AllocTransientInstanceBuffer(CKTransientInstanceBuffer *, CKDWORD, CKDWORD) override { return FALSE; }
    CKDWORD GetAvailTransientVertexBuffer(CKDWORD, CKDWORD) override { return 0; }
    CKDWORD GetAvailTransientIndexBuffer(CKDWORD, CKBOOL) override { return 0; }
    CKDWORD GetAvailTransientInstanceBuffer(CKDWORD, CKDWORD) override { return 0; }

    CKRasterizerEncoder *BeginEncoder() override { return &m_Encoder; }
    void EndEncoder(CKRasterizerEncoder *) override {}
    CKERROR Frame(CKRST_FRAME_SYNC_MODE) override { return CK_OK; }

private:
    CKNULLRasterizerEncoder m_Encoder;
    CKRenderStats m_Stats = {};
};

class CKNULLRasterizerDriver : public CKRasterizerDriver {
public:
    CKNULLRasterizerDriver()
    {
        m_Hardware = FALSE;
        m_Desc = "NULL Rasterizer Driver";

        VxDisplayMode mode;
        mode.Width = 640;
        mode.Height = 480;
        mode.Bpp = 32;
        mode.RefreshRate = 60;
        m_DisplayModes.PushBack(mode);
        mode.Width = 800;
        mode.Height = 600;
        m_DisplayModes.PushBack(mode);
        mode.Width = 1024;
        mode.Height = 768;
        m_DisplayModes.PushBack(mode);
    }

    ~CKNULLRasterizerDriver() override
    {
        for (auto it = m_Contexts.Begin(); it != m_Contexts.End(); ++it)
            delete *it;
        m_Contexts.Clear();
    }

    CKRasterizerContext *CreateContext() override
    {
        auto *ctx = new CKNULLRasterizerContext();
        ctx->m_Driver = this;
        m_Contexts.PushBack(ctx);
        return ctx;
    }

    CKBOOL DestroyContext(CKRasterizerContext *Context) override
    {
        for (auto it = m_Contexts.Begin(); it != m_Contexts.End(); ++it) {
            if (*it == Context) {
                m_Contexts.Remove(it);
                delete Context;
                return TRUE;
            }
        }
        return FALSE;
    }

    CKERROR GetProgrammableCaps(VxProgCapsDesc &Caps) override
    {
        memset(&Caps, 0, sizeof(Caps));
        return CK_OK;
    }
};

class CKNULLRasterizer : public CKRasterizer {
public:
    CKBOOL Start(WIN_HANDLE AppWnd) override
    {
        m_MainWindow = AppWnd;
        m_Drivers.PushBack(new CKNULLRasterizerDriver());
        return TRUE;
    }

    void Close() override
    {
        for (auto it = m_Drivers.Begin(); it != m_Drivers.End(); ++it)
            delete *it;
        m_Drivers.Clear();
    }
};

static CKNULLRasterizer *g_NULLRasterizer = nullptr;

CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd)
{
    g_NULLRasterizer = new CKNULLRasterizer();
    if (!g_NULLRasterizer->Start(AppWnd)) {
        delete g_NULLRasterizer;
        g_NULLRasterizer = nullptr;
        return nullptr;
    }
    return g_NULLRasterizer;
}

void CKNULLRasterizerClose(CKRasterizer *rst)
{
    if (rst) {
        rst->Close();
        delete rst;
    }
    if (rst == g_NULLRasterizer)
        g_NULLRasterizer = nullptr;
}
