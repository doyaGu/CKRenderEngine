#ifndef CKRASTERIZERDEVICE_H
#define CKRASTERIZERDEVICE_H

// Internal device interface of the translation core (formerly the public
// CKRasterizer v2 contract). Only CKRasterizerLib and the backend plugins
// include it; the engine talks to the v3 CKRasterizerContext in
// include/CKRasterizer.h.

#include "VxDefines.h"
#include "VxMath.h"
#include "CKError.h"
#include "CKRasterizerDeviceEnums.h"
#include "CKRasterizerDeviceTypes.h"

class CKRasterizerDeviceDriver;
class CKRasterizerDevice;
class CKRasterizerDeviceLibrary;
class CKRasterizerEncoder;

// ---------------------------------------------------------------------------
// CKRasterizerDeviceInfo
// ---------------------------------------------------------------------------

struct CKRasterizerDeviceInfo {
    XString DllName;
    XString Desc;
    INSTANCE_HANDLE DllInstance;
    CKRST_DEVICE_STARTFUNCTION StartFct;
    CKRST_DEVICE_CLOSEFUNCTION CloseFct;
    CKDWORD InterfaceRevision;

    CKRasterizerDeviceInfo()
    {
        DllInstance = NULL;
        StartFct = NULL;
        CloseFct = NULL;
        InterfaceRevision = 0;
    }
};

typedef void (*CKRST_DEVICE_GETINFO)(CKRasterizerDeviceInfo *);

// ===========================================================================
// CKRasterizerDeviceLibrary
// ===========================================================================

class CKRasterizerDeviceLibrary {
public:
    CKRasterizerDeviceLibrary();
    virtual ~CKRasterizerDeviceLibrary();

    virtual CKBOOL Start(WIN_HANDLE AppWnd);
    virtual void Close();

    virtual int GetDriverCount();
    virtual CKRasterizerDeviceDriver *GetDriver(CKDWORD Index);

public:
    WIN_HANDLE m_MainWindow;
    XArray<CKRasterizerDeviceDriver *> m_Drivers;
};

// ===========================================================================
// CKRasterizerDeviceDriver
// ===========================================================================

class CKRasterizerDeviceDriver {
public:
    CKRasterizerDeviceDriver();

    virtual ~CKRasterizerDeviceDriver();

    virtual CKRasterizerDevice *CreateContext();
    virtual CKBOOL DestroyContext(CKRasterizerDevice *Context);

    virtual void InitNULLRasterizerCaps(CKRasterizerDeviceLibrary *Owner);

public:
    CKBOOL m_Hardware;
    CKBOOL m_CapsUpToDate;
    CKRasterizerDeviceLibrary *m_Owner;
    CKDWORD m_DriverIndex;
    XArray<VxDisplayMode> m_DisplayModes;
    XClassArray<CKTextureDesc> m_TextureFormats;
    Vx3DCapsDesc m_3DCaps;
    Vx2DCapsDesc m_2DCaps;
    XString m_Desc;
    XArray<CKRasterizerDevice *> m_Contexts;
};

// ===========================================================================
// CKRasterizerEncoder
// ===========================================================================

class CKRasterizerEncoder {
public:
    virtual ~CKRasterizerEncoder() = default;
    virtual CKERROR GetStatus() const;

    // Draw state
    virtual void SetState(CKDrawState State);
    virtual void SetStencilRef(CKDWORD Ref);
    virtual void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask);
    virtual void SetScissor(const CKRECT *Rect);
    virtual void SetPointSize(float Size);

    // Transform
    virtual void SetTransform(CKDWORD TransformIndex, CKDWORD Count = 1);

    // Geometry binding
    virtual void SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                                 CKDWORD StartVertex, CKDWORD VertexCount,
                                 CKDWORD Layout);
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

    // Clear pending encoder state and recover from a rejected draw.
    virtual void Discard(CKDWORD Flags = CKRST_DISCARD_ALL) = 0;
    // Compute binding
    virtual void SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                                  CK_ACCESS_MODE Access);
    virtual void SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                                 CKDWORD Mip, CK_ACCESS_MODE Access);

    // Occlusion queries
    virtual void SetCondition(CKDWORD Query, CKBOOL Visible);

    // Debug markers
    virtual void SetMarker(CKSTRING Name);
    virtual CKBOOL ConsumeMarker(char *Buffer, CKDWORD BufferSize);

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
// CKRasterizerDevice
// ===========================================================================

class CKRasterizerDevice {
public:
    CKRasterizerDevice();
    virtual ~CKRasterizerDevice();

    // --- Context lifecycle ---
    virtual CKERROR Create(WIN_HANDLE Window, int PosX = 0, int PosY = 0,
                           int Width = 0, int Height = 0, int Bpp = -1,
                           CKBOOL Fullscreen = FALSE, int RefreshRate = 0,
                           int Zbpp = -1, int StencilBpp = -1);
    virtual CKERROR Resize(int PosX = 0, int PosY = 0,
                           int Width = 0, int Height = 0,
                           CKDWORD Flags = 0);
    virtual CKERROR SetAntialias(CKDWORD Samples);
    virtual CKBOOL IsIdle() const;
    virtual CKERROR BeginShutdown();
    virtual CKERROR GetDeviceStatus() const;
    virtual CKERROR GetTargetDesc(CKRasterizerTargetDesc *Target) const;
    virtual CKERROR GetCaps(CKRasterizerDeviceCapsDesc *Caps) const;
    virtual CKERROR GetTextureFormatCaps(VX_PIXELFORMAT Format,
                                         CKTextureFormatCaps *Caps) const;
    virtual CKERROR GetDepthFormatCaps(CK_DEPTH_FORMAT Format,
                                       CKDepthFormatCaps *Caps) const;

    // --- Resource creation ---
    virtual CKERROR CreateVertexBuffer(const CKVertexBufferDesc *Desc,
                                       const void *Data, CKDWORD *OutBuffer);
    virtual CKERROR CreateIndexBuffer(const CKIndexBufferDesc *Desc,
                                      CKBOOL Index32, const void *Data,
                                      CKDWORD *OutBuffer);
    virtual CKERROR CreateTexture(const CKTextureDesc *Desc,
                                  const VxImageDescEx *Data,
                                  CKDWORD *OutTexture);
    virtual CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *OutShader);
    virtual CKERROR CreateProgram(const CKProgramDesc *Desc, CKDWORD *OutProgram);
    virtual CKERROR CreateUniform(const CKUniformDesc *Desc, CKDWORD *OutUniform);
    virtual CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc,
                                       CKDWORD *OutLayout);
    virtual CKERROR CreateFrameBuffer(const CKFrameBufferDesc *Desc,
                                      CKDWORD *OutFrameBuffer);
    virtual CKERROR CreateDepthTexture(const CKDepthTextureDesc *Desc,
                                       CKDWORD *OutTexture);
    virtual CKERROR CreateOcclusionQuery(const CKOcclusionQueryDesc *Desc,
                                         CKDWORD *OutQuery);
    virtual CKERROR CreateIndirectBuffer(const CKIndirectBufferDesc *Desc,
                                         CKDWORD *OutBuffer);
    virtual CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const;
    virtual CKERROR DeleteObject(CKDWORD Object, CKDWORD Type);
    virtual CKERROR FlushObjects(CKDWORD TypeMask = CKRST_OBJ_ALL);

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
                                CKReadbackDesc *Readback,
                                CKDWORD *AvailableFrame);

    // --- Occlusion query results ---
    virtual CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD Query,
                                                   CKDWORD *PixelCount = NULL);

    // --- Palette ---
    virtual CKERROR SetPaletteColor(CKDWORD Index, CKDWORD RGBA);

    // --- Debug text overlay ---
    virtual void DbgTextClear(CKDWORD Color = 0, CKBOOL Small = FALSE);
    virtual void DbgTextPrintf(CKWORD X, CKWORD Y, CKDWORD Attr,
                               CKSTRING Format, ...);
    virtual void DbgTextImage(CKWORD X, CKWORD Y, CKWORD Width, CKWORD Height,
                              const void *Data, CKWORD Pitch);

    // --- Debug flags ---
    virtual void SetDebug(CKDWORD Flags);

    // --- Statistics ---
    virtual const CKRasterizerDeviceStats *GetStats();

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
    virtual CKERROR RequestScreenShot(CKDWORD FrameBuffer,
                                      CKScreenShotCallback Callback,
                                      void *UserData = NULL);
    virtual CKERROR CancelScreenShots(void *UserData);

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
    virtual CKRasterizerEncoder *BeginEncoder(CKBOOL ForceNewEncoder = FALSE);
    virtual CKERROR EndEncoder(CKRasterizerEncoder *Encoder);
    virtual CKERROR Frame(CKRST_FRAME_SYNC_MODE SyncMode,
                          CKDWORD Flags = CKRST_FRAME_NONE,
                          CKDWORD *FrameNumber = NULL);

public:
    CKRasterizerDeviceDriver *m_Driver;

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
    CKBOOL m_Created;
    CKDWORD m_NullFrameNumber;
    void *m_NullBackendState;
};

#endif // CKRASTERIZERDEVICE_H
