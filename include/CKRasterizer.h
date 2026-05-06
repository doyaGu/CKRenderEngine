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
    CKRasterizer() : m_MainWindow(NULL) {}
    virtual ~CKRasterizer() = default;

    virtual CKBOOL Start(WIN_HANDLE AppWnd) { return FALSE; }
    virtual void Close() {}

    virtual int GetDriverCount() { return m_Drivers.Size(); }
    virtual CKRasterizerDriver *GetDriver(CKDWORD Index) { return m_Drivers[Index]; }

public:
    WIN_HANDLE m_MainWindow;
    XArray<CKRasterizerDriver *> m_Drivers;
};

// ===========================================================================
// CKRasterizerDriver
// ===========================================================================

class CKRasterizerDriver {
public:
    CKRasterizerDriver()
        : m_Hardware(FALSE),
          m_CapsUpToDate(FALSE),
          m_Owner(NULL),
          m_DriverIndex(0) {}

    virtual ~CKRasterizerDriver() = default;

    virtual CKRasterizerContext *CreateContext() { return NULL; }
    virtual CKBOOL DestroyContext(CKRasterizerContext *Context) { return FALSE; }

    virtual CKERROR GetShaderTarget(CKShaderTargetDesc *Target) const
    {
        if (Target)
            *Target = CKShaderTargetDesc();
        return CKERR_NOTIMPLEMENTED;
    }
    virtual CKERROR GetProgrammableCaps(VxProgCapsDesc &Caps) = 0;

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
    virtual void SetState(CKDrawState State) = 0;
    virtual void SetStencilRef(CKDWORD Ref) = 0;
    virtual void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask = 0xFF) = 0;
    virtual void SetScissor(const CKRECT *Rect) = 0;
    virtual void SetPointSize(float Size) = 0;

    // Transform
    virtual void SetTransform(CKDWORD TransformIndex, CKDWORD Count = 1) = 0;

    // Geometry binding
    virtual void SetVertexLayout(CKDWORD Layout) = 0;
    virtual void SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                                 CKDWORD StartVertex, CKDWORD VertexCount) = 0;
    virtual void SetIndexBuffer(CKDWORD Buffer,
                                CKDWORD StartIndex, CKDWORD IndexCount) = 0;
    virtual void SetInstanceBuffer(CKDWORD Stream, CKDWORD Buffer,
                                   CKDWORD StartInstance, CKDWORD InstanceCount) = 0;
    virtual void SetTransientVertexBuffer(CKDWORD Stream,
                                          CKTransientVertexBuffer *Buffer) = 0;
    virtual void SetTransientIndexBuffer(CKTransientIndexBuffer *Buffer) = 0;
    virtual void SetTransientInstanceBuffer(CKDWORD Stream,
                                            CKTransientInstanceBuffer *Buffer) = 0;

    // Resource binding
    virtual void SetTexture(CKDWORD Stage, CKDWORD Uniform,
                            CKDWORD Texture, CKSamplerDesc *Sampler = NULL) = 0;
    virtual void SetUniform(CKDWORD Uniform, const void *Data,
                            CKDWORD Count = 1) = 0;

    // Compute binding
    virtual void SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                                  CK_ACCESS_MODE Access) = 0;
    virtual void SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                                 CKDWORD Mip, CK_ACCESS_MODE Access) = 0;

    // Occlusion queries
    virtual void SetCondition(CKDWORD Query, CKBOOL Visible) = 0;

    // Debug markers
    virtual void SetMarker(CKSTRING Name) = 0;

    // Submission -- graphics
    virtual void Submit(CKRenderView View, CKDWORD Program,
                        CKDWORD Depth = 0,
                        CKDWORD Flags = CKRST_DISCARD_ALL) = 0;
    virtual void SubmitOcclusionQuery(CKRenderView View, CKDWORD Program,
                                      CKDWORD Query, CKDWORD Depth = 0,
                                      CKDWORD Flags = CKRST_DISCARD_ALL) = 0;
    virtual void SubmitIndirect(CKRenderView View, CKDWORD Program,
                                CKDWORD IndirectBuffer,
                                CKDWORD Start = 0, CKDWORD Count = 1,
                                CKDWORD Depth = 0,
                                CKDWORD Flags = CKRST_DISCARD_ALL) = 0;

    // Submission -- compute
    virtual void Dispatch(CKRenderView View, CKDWORD Program,
                          CKDWORD NumX = 1, CKDWORD NumY = 1, CKDWORD NumZ = 1,
                          CKDWORD Flags = CKRST_DISCARD_ALL) = 0;
    virtual void DispatchIndirect(CKRenderView View, CKDWORD Program,
                                  CKDWORD IndirectBuffer,
                                  CKDWORD Start = 0, CKDWORD Count = 1,
                                  CKDWORD Flags = CKRST_DISCARD_ALL) = 0;

    // Utility
    virtual void Touch(CKRenderView View) = 0;
    virtual void Blit(CKRenderView View,
                      CKDWORD DstTexture, CKDWORD DstMip,
                      CKDWORD DstX, CKDWORD DstY,
                      CKDWORD SrcTexture, CKDWORD SrcMip,
                      const CKRECT *SrcRect) = 0;
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
                          int Zbpp = -1, int StencilBpp = -1) { return FALSE; }
    virtual CKBOOL Resize(int PosX = 0, int PosY = 0,
                          int Width = 0, int Height = 0,
                          CKDWORD Flags = 0) { return FALSE; }

    // --- Resource creation ---
    virtual CKERROR CreateVertexBuffer(CKDWORD Buffer, CKVertexBufferDesc *Desc,
                                       const void *Data) = 0;
    virtual CKERROR CreateIndexBuffer(CKDWORD Buffer, CKIndexBufferDesc *Desc,
                                      CKBOOL Index32, const void *Data) = 0;
    virtual CKERROR CreateTexture(CKDWORD Texture, CKTextureDesc *Desc,
                                  const VxImageDescEx *Data) = 0;
    virtual CKERROR CreateShader(CKDWORD Shader, CKShaderDesc *Desc) = 0;
    virtual CKERROR CreateProgram(CKDWORD Program, CKProgramDesc *Desc) = 0;
    virtual CKERROR CreateUniform(CKDWORD Uniform, CKUniformDesc *Desc) = 0;
    virtual CKERROR CreateVertexLayout(CKDWORD Layout, CKVertexLayoutDesc *Desc) = 0;
    virtual CKERROR CreateFrameBuffer(CKDWORD FrameBuffer, CKFrameBufferDesc *Desc) = 0;
    virtual CKERROR CreateDepthTexture(CKDWORD Texture, CKDepthTextureDesc *Desc) = 0;
    virtual CKERROR CreateOcclusionQuery(CKDWORD Query, CKOcclusionQueryDesc *Desc) = 0;
    virtual CKERROR CreateIndirectBuffer(CKDWORD Buffer, CKIndirectBufferDesc *Desc) = 0;
    virtual CKERROR DeleteObject(CKDWORD Object, CKDWORD Type) = 0;
    virtual void FlushObjects(CKDWORD TypeMask = CKRST_OBJ_ALL) = 0;

    // --- Resource update ---
    virtual CKERROR UpdateVertexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                       CKDWORD Size, const void *Data) = 0;
    virtual CKERROR UpdateIndexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                      CKDWORD Size, const void *Data) = 0;
    virtual CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face,
                                  const CKRECT *Region,
                                  const VxImageDescEx *Data) = 0;

    // --- Readback ---
    virtual CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip,
                                VxImageDescEx *Data) = 0;
    virtual CKERROR ReadFrameBuffer(CKDWORD FrameBuffer,
                                    VxImageDescEx *Data) = 0;

    // --- Occlusion query results ---
    virtual CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD Query,
                                                    CKDWORD *PixelCount = NULL) = 0;

    // --- Palette ---
    virtual void SetPaletteColor(CKDWORD Index, CKDWORD RGBA) = 0;

    // --- Debug text overlay ---
    virtual void DbgTextClear(CKDWORD Color = 0, CKBOOL Small = FALSE) = 0;
    virtual void DbgTextPrintf(CKWORD X, CKWORD Y, CKDWORD Attr,
                               CKSTRING Format, ...) = 0;
    virtual void DbgTextImage(CKWORD X, CKWORD Y, CKWORD Width, CKWORD Height,
                              const void *Data, CKWORD Pitch) = 0;

    // --- Debug flags ---
    virtual void SetDebug(CKDWORD Flags) = 0;

    // --- Statistics ---
    virtual const CKRenderStats *GetStats() = 0;

    // --- Resource naming ---
    virtual void SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name) = 0;

    // --- Shader reflection ---
    virtual CKDWORD GetShaderUniforms(CKDWORD Shader, CKDWORD *Uniforms = NULL,
                                       CKDWORD MaxCount = 0) = 0;
    virtual void GetUniformInfo(CKDWORD Uniform, CKUniformInfo *Info) = 0;

    // --- Framebuffer queries ---
    virtual CKDWORD GetFrameBufferTexture(CKDWORD FrameBuffer,
                                           CKDWORD Attachment = 0) = 0;

    // --- Resource validation ---
    virtual CKBOOL IsTextureValid(CKDWORD Depth, CKBOOL CubeMap, CKWORD NumLayers,
                                  CKDWORD Format, CKDWORD Flags) = 0;
    virtual CKBOOL IsFrameBufferValid(CKDWORD ColorCount,
                                      const CKFrameBufferAttachmentDesc *Color,
                                      const CKFrameBufferAttachmentDesc *DepthStencil = NULL) = 0;

    // --- Texture info ---
    virtual void CalcTextureSize(CKTextureInfo *Info, CKWORD Width, CKWORD Height,
                                 CKWORD Depth, CKBOOL CubeMap, CKBOOL HasMips,
                                 CKWORD NumLayers, CKDWORD Format) = 0;

    // --- Screenshot capture ---
    virtual void RequestScreenShot(CKDWORD FrameBuffer,
                                   CKScreenShotCallback Callback) = 0;

    // --- Render views ---
    virtual CKERROR SetViewName(CKRenderView View, CKSTRING Name) = 0;
    virtual CKERROR SetViewRect(CKRenderView View, const CKRECT &Rect) = 0;
    virtual CKERROR SetViewScissor(CKRenderView View, const CKRECT *Rect) = 0;
    virtual CKERROR SetViewClear(CKRenderView View, CKDWORD Flags,
                                 CKDWORD Color, float Z, CKDWORD Stencil) = 0;
    virtual CKERROR SetViewTransform(CKRenderView View,
                                     const VxMatrix *ViewMatrix,
                                     const VxMatrix *ProjMatrix) = 0;
    virtual CKERROR SetViewFrameBuffer(CKRenderView View, CKDWORD FrameBuffer) = 0;
    virtual CKERROR SetViewMode(CKRenderView View, CK_VIEW_MODE Mode) = 0;
    virtual CKERROR SetViewOrder(CKRenderView Start, CKWORD Count,
                                 const CKRenderView *Order) = 0;
    virtual CKERROR ResetView(CKRenderView View) = 0;
    virtual CKERROR TouchView(CKRenderView View) = 0;

    // --- Transform cache ---
    virtual CKDWORD AllocTransform(VxMatrix *Transform, CKDWORD Count) = 0;

    // --- Transient buffers ---
    virtual CKBOOL AllocTransientVertexBuffer(CKTransientVertexBuffer *Buffer,
                                              CKDWORD VertexCount,
                                              CKDWORD Layout) = 0;
    virtual CKBOOL AllocTransientIndexBuffer(CKTransientIndexBuffer *Buffer,
                                             CKDWORD IndexCount,
                                             CKBOOL Index32 = FALSE) = 0;
    virtual CKBOOL AllocTransientInstanceBuffer(CKTransientInstanceBuffer *Buffer,
                                                CKDWORD InstanceCount,
                                                CKDWORD Layout) = 0;
    virtual CKDWORD GetAvailTransientVertexBuffer(CKDWORD VertexCount,
                                                  CKDWORD Layout) = 0;
    virtual CKDWORD GetAvailTransientIndexBuffer(CKDWORD IndexCount,
                                                 CKBOOL Index32 = FALSE) = 0;
    virtual CKDWORD GetAvailTransientInstanceBuffer(CKDWORD InstanceCount,
                                                    CKDWORD Layout) = 0;

    // --- Encoder and frame ---
    virtual CKRasterizerEncoder *BeginEncoder() = 0;
    virtual void EndEncoder(CKRasterizerEncoder *Encoder) = 0;
    virtual CKERROR Frame(CKBOOL VSync) = 0;

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
        m_State.Lo |= CKRST_STATE_BLEND(SrcColor, DstColor);
        return *this;
    }

    CKDrawStateBuilder &BlendSeparate(VXBLEND_MODE SrcColor, VXBLEND_MODE DstColor,
                                       VXBLEND_MODE SrcAlpha, VXBLEND_MODE DstAlpha)
    {
        m_State.Lo &= ~(0xFFFFUL << 16);
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
    CKDrawState m_State;
};

// ===========================================================================
// CKRasterizerContext inline constructor
// ===========================================================================

inline CKRasterizerContext::CKRasterizerContext()
    : m_Driver(NULL),
      m_PosX(0), m_PosY(0),
      m_Width(0), m_Height(0),
      m_Bpp(0), m_ZBpp(0), m_StencilBpp(0),
      m_Fullscreen(FALSE), m_RefreshRate(0),
      m_Window(NULL)
{
}

#endif // CKRASTERIZER_H
