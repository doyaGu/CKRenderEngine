#ifndef CKRASTERIZER_H
#define CKRASTERIZER_H

#include "VxDefines.h"
#include "VxMath.h"
#include "CKError.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"

class CKRasterizerDriver;
class CKRasterizerContext;
class CKRasterizer;
class CKRasterizerEncoder;

// ---------------------------------------------------------------------------
// CKRasterizerInfo
// ---------------------------------------------------------------------------

struct CKRasterizerInfo {
    XString DllName;
    XString Desc;
    INSTANCE_HANDLE DllInstance;
    CKRST_STARTFUNCTION StartFct;
    CKRST_CLOSEFUNCTION CloseFct;

    CKRasterizerInfo()
    {
        DllInstance = NULL;
        StartFct = NULL;
        CloseFct = NULL;
    }
};

typedef void (*CKRST_GETINFO)(CKRasterizerInfo *);

// ===========================================================================
// CKRasterizer
// ===========================================================================

class CKRasterizer {
public:
    CKRasterizer();
    virtual ~CKRasterizer();

    virtual CKBOOL Start(WIN_HANDLE AppWnd);
    virtual void Close();

    virtual int GetDriverCount();
    virtual CKRasterizerDriver *GetDriver(CKDWORD Index);

public:
    WIN_HANDLE m_MainWindow;
    XArray<CKRasterizerDriver *> m_Drivers;
};

// ===========================================================================
// CKRasterizerDriver
// ===========================================================================

class CKRasterizerDriver {
public:
    CKRasterizerDriver();

    virtual ~CKRasterizerDriver();

    virtual CKRasterizerContext *CreateContext();
    virtual CKBOOL DestroyContext(CKRasterizerContext *Context);

    virtual CKERROR GetShaderTarget(CKShaderTargetDesc *Target) const;
    virtual CKERROR GetProgrammableCaps(VxProgCapsDesc &Caps);
    virtual void InitNULLRasterizerCaps(CKRasterizer *Owner);

public:
    CKBOOL m_Hardware;
    CKBOOL m_CapsUpToDate;
    CKRasterizer *m_Owner;
    CKDWORD m_DriverIndex;
    XArray<VxDisplayMode> m_DisplayModes;
    XClassArray<CKTextureDesc> m_TextureFormats;
    Vx3DCapsDesc m_3DCaps;
    Vx2DCapsDesc m_2DCaps;
    XString m_Desc;
    XArray<CKRasterizerContext *> m_Contexts;
};

// ===========================================================================
// CKRasterizerEncoder
// ===========================================================================

class CKRasterizerEncoder {
public:
    virtual ~CKRasterizerEncoder() = default;

    // Draw state
    virtual void SetState(CKDrawState State);
    virtual void SetStencilRef(CKDWORD Ref);
    virtual void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask);
    virtual void SetScissor(const CKRECT *Rect);
    virtual void SetPointSize(float Size);

    // Transform
    virtual void SetTransform(CKDWORD TransformIndex, CKDWORD Count = 1);

    // Geometry binding
    virtual void SetVertexLayout(CKDWORD Layout);
    virtual void SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                                 CKDWORD StartVertex, CKDWORD VertexCount);
    virtual void SetIndexBuffer(CKDWORD Buffer,
                                CKDWORD StartIndex, CKDWORD IndexCount);
    virtual void SetInstanceBuffer(CKDWORD Stream, CKDWORD Buffer,
                                   CKDWORD StartInstance, CKDWORD InstanceCount);
    virtual void SetTransientVertexBuffer(CKDWORD Stream,
                                          CKTransientVertexBuffer *Buffer);
    virtual void SetTransientIndexBuffer(CKTransientIndexBuffer *Buffer);
    virtual void SetTransientInstanceBuffer(CKDWORD Stream,
                                            CKTransientInstanceBuffer *Buffer);

    // Resource binding
    virtual void SetTexture(CKDWORD Stage, CKDWORD Uniform,
                            CKDWORD Texture, CKSamplerDesc *Sampler = NULL);
    virtual void SetUniform(CKDWORD Uniform, const void *Data,
                            CKDWORD Count = 1);

    // Compute binding
    virtual void SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                                  CK_ACCESS_MODE Access);
    virtual void SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                                 CKDWORD Mip, CK_ACCESS_MODE Access);

    // Occlusion queries
    virtual void SetCondition(CKDWORD Query, CKBOOL Visible);

    // Debug markers
    virtual void SetMarker(CKSTRING Name);

    // Submission -- graphics
    virtual void Submit(CKRenderView View, CKDWORD Program,
                        CKDWORD Depth = 0,
                        CKDWORD Flags = CKRST_DISCARD_ALL);
    virtual void SubmitOcclusionQuery(CKRenderView View, CKDWORD Program,
                                      CKDWORD Query, CKDWORD Depth = 0,
                                      CKDWORD Flags = CKRST_DISCARD_ALL);
    virtual void SubmitIndirect(CKRenderView View, CKDWORD Program,
                                CKDWORD IndirectBuffer,
                                CKDWORD Start = 0, CKDWORD Count = 1,
                                CKDWORD Depth = 0,
                                CKDWORD Flags = CKRST_DISCARD_ALL);

    // Submission -- compute
    virtual void Dispatch(CKRenderView View, CKDWORD Program,
                          CKDWORD NumX = 1, CKDWORD NumY = 1, CKDWORD NumZ = 1,
                          CKDWORD Flags = CKRST_DISCARD_ALL);
    virtual void DispatchIndirect(CKRenderView View, CKDWORD Program,
                                  CKDWORD IndirectBuffer,
                                  CKDWORD Start = 0, CKDWORD Count = 1,
                                  CKDWORD Flags = CKRST_DISCARD_ALL);

    // Utility
    virtual void Touch(CKRenderView View);
    virtual void Blit(CKRenderView View,
                      CKDWORD DstTexture, CKDWORD DstMip,
                      CKDWORD DstX, CKDWORD DstY,
                      CKDWORD SrcTexture, CKDWORD SrcMip,
                      const CKRECT *SrcRect);
};

// ===========================================================================
// CKRasterizerContext
// ===========================================================================

class CKRasterizerContext {
public:
    CKRasterizerContext();
    virtual ~CKRasterizerContext() = default;

    // --- Context lifecycle ---
    virtual CKBOOL Create(WIN_HANDLE Window, int PosX = 0, int PosY = 0,
                          int Width = 0, int Height = 0, int Bpp = -1,
                          CKBOOL Fullscreen = FALSE, int RefreshRate = 0,
                          int Zbpp = -1, int StencilBpp = -1);
    virtual CKBOOL Resize(int PosX = 0, int PosY = 0,
                          int Width = 0, int Height = 0,
                          CKDWORD Flags = 0);
    virtual void SetAntialias(CKDWORD Samples);

    // --- Resource creation ---
    virtual CKERROR CreateVertexBuffer(CKDWORD Buffer, CKVertexBufferDesc *Desc,
                                       const void *Data);
    virtual CKERROR CreateIndexBuffer(CKDWORD Buffer, CKIndexBufferDesc *Desc,
                                      CKBOOL Index32, const void *Data);
    virtual CKERROR CreateTexture(CKDWORD Texture, CKTextureDesc *Desc,
                                  const VxImageDescEx *Data);
    virtual CKERROR CreateShader(CKDWORD Shader, CKShaderDesc *Desc);
    virtual CKERROR CreateProgram(CKDWORD Program, CKProgramDesc *Desc);
    virtual CKERROR CreateUniform(CKDWORD Uniform, CKUniformDesc *Desc);
    virtual CKERROR CreateVertexLayout(CKDWORD Layout, CKVertexLayoutDesc *Desc);
    virtual CKERROR CreateFrameBuffer(CKDWORD FrameBuffer, CKFrameBufferDesc *Desc);
    virtual CKERROR CreateDepthTexture(CKDWORD Texture, CKDepthTextureDesc *Desc);
    virtual CKERROR CreateOcclusionQuery(CKDWORD Query, CKOcclusionQueryDesc *Desc);
    virtual CKERROR CreateIndirectBuffer(CKDWORD Buffer, CKIndirectBufferDesc *Desc);
    virtual CKERROR DeleteObject(CKDWORD Object, CKDWORD Type);
    virtual void FlushObjects(CKDWORD TypeMask = CKRST_OBJ_ALL);

    // --- Resource update ---
    virtual CKERROR UpdateVertexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                       CKDWORD Size, const void *Data);
    virtual CKERROR UpdateIndexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                      CKDWORD Size, const void *Data);
    virtual CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face,
                                  const CKRECT *Region,
                                  const VxImageDescEx *Data);

    // --- Readback ---
    virtual CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip,
                                VxImageDescEx *Data);
    virtual CKERROR ReadFrameBuffer(CKDWORD FrameBuffer,
                                    VxImageDescEx *Data);

    // --- Occlusion query results ---
    virtual CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD Query,
                                                   CKDWORD *PixelCount = NULL);

    // --- Palette ---
    virtual void SetPaletteColor(CKDWORD Index, CKDWORD RGBA);

    // --- Debug text overlay ---
    virtual void DbgTextClear(CKDWORD Color = 0, CKBOOL Small = FALSE);
    virtual void DbgTextPrintf(CKWORD X, CKWORD Y, CKDWORD Attr,
                               CKSTRING Format, ...);
    virtual void DbgTextImage(CKWORD X, CKWORD Y, CKWORD Width, CKWORD Height,
                              const void *Data, CKWORD Pitch);

    // --- Debug flags ---
    virtual void SetDebug(CKDWORD Flags);

    // --- Statistics ---
    virtual const CKRenderStats *GetStats();

    // --- Resource naming ---
    virtual void SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name);

    // --- Shader reflection ---
    virtual CKDWORD GetShaderUniforms(CKDWORD Shader, CKDWORD *Uniforms = NULL,
                                      CKDWORD MaxCount = 0);
    virtual void GetUniformInfo(CKDWORD Uniform, CKUniformInfo *Info);

    // --- Framebuffer queries ---
    virtual CKDWORD GetFrameBufferTexture(CKDWORD FrameBuffer,
                                          CKDWORD Attachment = 0);

    // --- Resource validation ---
    virtual CKBOOL IsTextureValid(CKDWORD Depth, CKBOOL CubeMap, CKWORD NumLayers,
                                  CKDWORD Format, CKDWORD Flags);
    virtual CKBOOL IsFrameBufferValid(CKDWORD ColorCount,
                                      const CKFrameBufferAttachmentDesc *Color,
                                      const CKFrameBufferAttachmentDesc *DepthStencil = NULL);

    // --- Texture info ---
    virtual void CalcTextureSize(CKTextureInfo *Info, CKWORD Width, CKWORD Height,
                                 CKWORD Depth, CKBOOL CubeMap, CKBOOL HasMips,
                                 CKWORD NumLayers, CKDWORD Format);

    // --- Screenshot capture ---
    virtual void RequestScreenShot(CKDWORD FrameBuffer,
                                   CKScreenShotCallback Callback);

    // --- Render views ---
    virtual CKERROR SetViewName(CKRenderView View, CKSTRING Name);
    virtual CKERROR SetViewRect(CKRenderView View, const CKRECT &Rect);
    virtual CKERROR SetViewScissor(CKRenderView View, const CKRECT *Rect);
    virtual CKERROR SetViewClear(CKRenderView View, CKDWORD Flags,
                                 CKDWORD Color, float Z, CKDWORD Stencil);
    virtual CKERROR SetViewTransform(CKRenderView View,
                                     const VxMatrix *ViewMatrix,
                                     const VxMatrix *ProjMatrix);
    virtual CKERROR SetViewFrameBuffer(CKRenderView View, CKDWORD FrameBuffer);
    virtual CKERROR SetViewMode(CKRenderView View, CK_VIEW_MODE Mode);
    virtual CKERROR SetViewOrder(CKRenderView Start, CKWORD Count,
                                 const CKRenderView *Order);
    virtual CKERROR ResetView(CKRenderView View);
    virtual CKERROR TouchView(CKRenderView View);

    // --- Transform cache ---
    virtual CKDWORD AllocTransform(VxMatrix *Transform, CKDWORD Count);

    // --- Transient buffers ---
    virtual CKBOOL AllocTransientVertexBuffer(CKTransientVertexBuffer *Buffer,
                                              CKDWORD VertexCount,
                                              CKDWORD Layout);
    virtual CKBOOL AllocTransientIndexBuffer(CKTransientIndexBuffer *Buffer,
                                             CKDWORD IndexCount,
                                             CKBOOL Index32 = FALSE);
    virtual CKBOOL AllocTransientInstanceBuffer(CKTransientInstanceBuffer *Buffer,
                                                CKDWORD InstanceCount,
                                                CKDWORD Layout);
    virtual CKDWORD GetAvailTransientVertexBuffer(CKDWORD VertexCount,
                                                  CKDWORD Layout);
    virtual CKDWORD GetAvailTransientIndexBuffer(CKDWORD IndexCount,
                                                 CKBOOL Index32 = FALSE);
    virtual CKDWORD GetAvailTransientInstanceBuffer(CKDWORD InstanceCount,
                                                    CKDWORD Layout);

    // --- Encoder and frame ---
    virtual CKRasterizerEncoder *BeginEncoder();
    virtual void EndEncoder(CKRasterizerEncoder *Encoder);
    virtual CKERROR Frame(CKRST_FRAME_SYNC_MODE SyncMode);

public:
    CKRasterizerDriver *m_Driver;

    CKDWORD m_PosX;
    CKDWORD m_PosY;
    CKDWORD m_Width;
    CKDWORD m_Height;

    CKDWORD m_Bpp;
    CKDWORD m_ZBpp;
    CKDWORD m_StencilBpp;

    CKDWORD m_Fullscreen;
    CKDWORD m_RefreshRate;

    WIN_HANDLE m_Window;
};

// ===========================================================================
// CKDrawStateBuilder
// ===========================================================================

class CKDrawStateBuilder {
public:
    CKDrawStateBuilder()
    {
        m_State.Lo = CKRST_STATE_WRITE_RGBA
                   | CKRST_STATE_DEPTH_TEST
                   | CKRST_STATE_DEPTH_WRITE
                   | CKRST_STATE_DEPTH_FUNC(VXCMP_LESSEQUAL)
                   | CKRST_STATE_CULL(VXCULL_CCW - 1);
        m_State.Mid = CKRST_STATE_PT(VX_TRIANGLELIST);
        m_State.Hi = 0;
    }

    CKDrawStateBuilder &WriteRGBA(CKBOOL R, CKBOOL G, CKBOOL B, CKBOOL A)
    {
        m_State.Lo &= ~0xFUL;
        if (R) m_State.Lo |= CKRST_STATE_WRITE_R;
        if (G) m_State.Lo |= CKRST_STATE_WRITE_G;
        if (B) m_State.Lo |= CKRST_STATE_WRITE_B;
        if (A) m_State.Lo |= CKRST_STATE_WRITE_A;
        return *this;
    }

    CKDrawStateBuilder &Depth(CKBOOL Test, CKBOOL Write, VXCMPFUNC Func)
    {
        m_State.Lo &= ~(CKRST_STATE_DEPTH_TEST | CKRST_STATE_DEPTH_WRITE | (0xFUL << 6));
        if (Test)  m_State.Lo |= CKRST_STATE_DEPTH_TEST;
        if (Write) m_State.Lo |= CKRST_STATE_DEPTH_WRITE;
        m_State.Lo |= CKRST_STATE_DEPTH_FUNC(Func);
        return *this;
    }

    CKDrawStateBuilder &Cull(VXCULL Mode)
    {
        m_State.Lo &= ~(0x3UL << 10);
        CKDWORD remapped = (Mode >= 1) ? (Mode - 1) : 0;
        m_State.Lo |= CKRST_STATE_CULL(remapped);
        return *this;
    }

    CKDrawStateBuilder &Fill(VXFILL_MODE Mode)
    {
        m_State.Lo &= ~(0x3UL << 12);
        CKDWORD remapped;
        switch (Mode) {
        case VXFILL_SOLID:     remapped = 0; break;
        case VXFILL_WIREFRAME: remapped = 1; break;
        case VXFILL_POINT:     remapped = 2; break;
        default:               remapped = 0; break;
        }
        m_State.Lo |= CKRST_STATE_FILLMODE(remapped);
        return *this;
    }

    CKDrawStateBuilder &MSAA(CKBOOL Enable)
    {
        if (Enable) m_State.Lo |= CKRST_STATE_MSAA;
        else        m_State.Lo &= ~CKRST_STATE_MSAA;
        return *this;
    }

    CKDrawStateBuilder &AlphaCoverage(CKBOOL Enable)
    {
        if (Enable) m_State.Lo |= CKRST_STATE_ALPHA_COVERAGE;
        else        m_State.Lo &= ~CKRST_STATE_ALPHA_COVERAGE;
        return *this;
    }

    CKDrawStateBuilder &Blend(VXBLEND_MODE SrcColor, VXBLEND_MODE DstColor)
    {
        m_State.Lo &= ~(0xFFFFUL << 16);
        FixupBlendPair(SrcColor, DstColor);
        m_State.Lo |= CKRST_STATE_BLEND(SrcColor, DstColor);
        return *this;
    }

    CKDrawStateBuilder &BlendSeparate(VXBLEND_MODE SrcColor, VXBLEND_MODE DstColor,
                                       VXBLEND_MODE SrcAlpha, VXBLEND_MODE DstAlpha)
    {
        m_State.Lo &= ~(0xFFFFUL << 16);
        FixupBlendPair(SrcColor, DstColor);
        FixupBlendPair(SrcAlpha, DstAlpha);
        m_State.Lo |= CKRST_STATE_BLEND_SEPARATE(SrcColor, DstColor, SrcAlpha, DstAlpha);
        return *this;
    }

    CKDrawStateBuilder &NoBlend()
    {
        m_State.Lo &= ~(0xFFFFUL << 16);
        return *this;
    }

    CKDrawStateBuilder &BlendEquation(VXBLENDOP Color)
    {
        m_State.Mid &= ~0x3FUL;
        m_State.Mid |= CKRST_STATE_BLEND_EQ(Color);
        return *this;
    }

    CKDrawStateBuilder &BlendEquationSeparate(VXBLENDOP Color, VXBLENDOP Alpha)
    {
        m_State.Mid &= ~0x3FUL;
        m_State.Mid |= CKRST_STATE_BLEND_EQ_SEPARATE(Color, Alpha);
        return *this;
    }

    CKDrawStateBuilder &Topology(VXPRIMITIVETYPE PT)
    {
        m_State.Mid &= ~(0x7UL << 6);
        m_State.Mid |= CKRST_STATE_PT(PT);
        return *this;
    }

    CKDrawStateBuilder &Stencil(CKBOOL Enable, VXCMPFUNC Func,
                                 VXSTENCILOP Fail, VXSTENCILOP ZFail, VXSTENCILOP Pass)
    {
        m_State.Mid &= ~(0x7FFFFFUL << 9);
        if (Enable)
            m_State.Mid |= CKRST_STENCIL_OPS(Func, Fail, ZFail, Pass);
        return *this;
    }

    CKDrawStateBuilder &StencilBack(VXCMPFUNC Func,
                                     VXSTENCILOP Fail, VXSTENCILOP ZFail, VXSTENCILOP Pass)
    {
        m_State.Hi &= ~0xFFFFUL;
        m_State.Hi |= CKRST_STENCIL_BACK_OPS(Func, Fail, ZFail, Pass);
        return *this;
    }

    CKDrawStateBuilder &FrontFaceCCW(CKBOOL CCW)
    {
        if (CCW) m_State.Hi |= CKRST_STATE_FRONT_CCW;
        else     m_State.Hi &= ~CKRST_STATE_FRONT_CCW;
        return *this;
    }

    CKDrawState Build() const { return m_State; }

private:
    static void FixupBlendPair(VXBLEND_MODE &src, VXBLEND_MODE &dst)
    {
        if (src == VXBLEND_BOTHSRCALPHA) {
            src = VXBLEND_SRCALPHA;
            dst = VXBLEND_INVSRCALPHA;
        } else if (src == VXBLEND_BOTHINVSRCALPHA) {
            src = VXBLEND_INVSRCALPHA;
            dst = VXBLEND_SRCALPHA;
        }
    }

    CKDrawState m_State;
};

#endif // CKRASTERIZER_H
