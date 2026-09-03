#ifndef CKTRANSLATEDRASTERIZER_H
#define CKTRANSLATEDRASTERIZER_H

// Translation core of the CKRasterizer v3 contract (spec section 5, plan
// phase 1 step 1.4).
//
// A CKTranslatedRasterizer wraps a backend "device library" (the former v2
// CKRasterizer interface, now CKRasterizerDeviceLibrary) and exposes it to the
// engine as the D3D7-shaped v3 contract. Every fixed-function state call is
// mirrored verbatim (so Get* returns exactly what was set) and forwarded to
// the fixed-function pipeline; draws go through CKFixedFunctionPipeline onto
// the device encoder; the frame flow allocates one device view per pass.

#include "CKRasterizer.h"
#include "CKRasterizerDevice.h"
#include "CKFixedFunctionPipeline.h"
#include "CKPostprocessPass.h"

#include <unordered_map>
#include <vector>

class CKTranslatedRasterizer;
class CKTranslatedDriver;
class CKTranslatedContext;

typedef void (*CKTranslatedDeviceCloseFunction)(CKRasterizerDeviceLibrary *Device);

// ===========================================================================
// CKTranslatedRasterizer
// ===========================================================================

class CKTranslatedRasterizer : public CKRasterizer {
public:
    // Takes ownership of `Device`. `CloseDevice` (may be NULL) is called with
    // the device when the rasterizer is destroyed; NULL means `delete`.
    CKTranslatedRasterizer(CKRasterizerDeviceLibrary *Device, CKTranslatedDeviceCloseFunction CloseDevice);
    ~CKTranslatedRasterizer() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;

    CKRasterizerDeviceLibrary *GetDevice() const { return m_Device; }

private:
    CKRasterizerDeviceLibrary *m_Device;
    CKTranslatedDeviceCloseFunction m_CloseDevice;
};

// ===========================================================================
// CKTranslatedDriver
// ===========================================================================

class CKTranslatedDriver : public CKRasterizerDriver {
public:
    CKTranslatedDriver(CKTranslatedRasterizer *Owner, CKRasterizerDeviceDriver *Device, CKDWORD Index);
    ~CKTranslatedDriver() override;

    CKRasterizerContext *CreateContext() override;
    CKBOOL DestroyContext(CKRasterizerContext *Context) override;

    CKRasterizerDeviceDriver *GetDeviceDriver() const { return m_Device; }
    // Copies caps, display modes and texture formats from the device driver.
    // Backends refresh their caps when a context is created, so the context
    // calls this again after Create().
    void SyncCapsFromDevice();

private:
    CKRasterizerDeviceDriver *m_Device;
};

// ===========================================================================
// CKTranslatedContext
// ===========================================================================

class CKTranslatedContext : public CKRasterizerContext {
public:
    CKTranslatedContext(CKTranslatedDriver *Driver, CKRasterizerDevice *Device);
    ~CKTranslatedContext() override;

    // --- Lifecycle ---
    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height,
                  int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) override;
    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) override;
    CKBOOL SetOptions(const CKRasterizerOptions *Options) override;
    CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const override;
    CKERROR GetDeviceStatus() const override;
    CKBOOL BeginShutdown() override;
    CKBOOL IsIdle() const override;

    // --- Frame ---
    CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil,
                 int RectCount, CKRECT *Rects) override;
    CKBOOL BeginScene() override;
    CKBOOL EndScene() override;
    CKBOOL BeginOverlayPhase() override;
    CKBOOL BackToFront(CKBOOL VSync) override;

    // --- State ---
    CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) override;
    CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) override;
    CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) override;
    CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) override;
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override;
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override;
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override;
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override;
    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override;
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override;
    CKBOOL SetMaterial(const CKMaterialData *Data) override;
    CKBOOL SetViewport(const CKViewportData *Data) override;
    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override;
    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override;
    void InitDefaultRenderStatesValue() override;

    // --- Draw ---
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount,
                         VxDrawPrimitiveData *Data) override;
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex,
                           CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override;
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB,
                             CKDWORD MinVertexIndex, CKDWORD VertexCount,
                             CKDWORD StartIndex, int IndexCount) override;

    // --- Resources ---
    CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) override;
    CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel,
                       CKRST_CUBEFACE Face, const CKRECT *Region) override;
    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override;
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override;
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override;
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override;
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override;
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override;
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override;
    CKBOOL DeleteObject(CKDWORD Handle, CKDWORD Type) override;
    CKBOOL FlushObjects(CKDWORD TypeMask) override;
    void SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name) override;

    // --- Targets, readback, copies ---
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override;
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override;
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override;
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override;
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer,
                           CKReadbackCallback Callback, void *User) override;

    // --- Diagnostics ---
    void SetDebugMarker(CKSTRING Name) override;
    const CKRenderStats *GetStats() override;

    // --- Internal access ---
    CKRasterizerDevice *GetDevice() const { return m_Device; }

    // --- Test access (the translated tests read the pipeline and the mirror) ---
    CKFixedFunctionPipeline *GetFFPipelineForTests() { return &m_FFP; }
    CKDWORD GetTargetForTests() const { return m_Target; }
    CKBOOL IsInSceneForTests() const { return m_InScene; }
    CKDWORD GetPassCountForTests() const { return m_FramePasses; }
    int GetLiveResourceCountForTests(CKDWORD TypeMask) const;
    const CKLightData &GetLightForTests(CKDWORD Index) const { return m_Lights[Index]; }
    CKBOOL IsLightEnabledForTests(CKDWORD Index) const { return m_LightEnabled[Index]; }
    const CKMaterialData &GetMaterialForTests() const { return m_Material; }
    const CKViewportData &GetViewportForTests() const { return m_Viewport; }
    const CKRasterizerOptions &GetOptionsForTests() const { return m_Options; }
    CKBOOL GetVertexBufferDescForTests(CKDWORD VB, CKVertexBufferDesc *Desc) const;

private:
    struct Resource {
        CKDWORD Type;                      // CKRST_OBJ_*
        CKDWORD Handle;                    // device handle (== contract handle)
        CKTextureDesc Texture;
        CKVertexBufferDesc VertexBuffer;
        CKIndexBufferDesc IndexBuffer;
        CKRSTVertexLayout Layout;          // canonical layout the engine writes
        CKDWORD FormatFlags;               // fixed-function layout the device sees
        CKDWORD DeviceStride;
        CKDWORD DeviceLayout;
        std::vector<CKBYTE> Shadow;        // Lock storage (canonical layout / indices)
        std::vector<CKBYTE> Scratch;       // interleave target for Unlock
        CKBOOL Locked;
        CKDWORD LockStart;
        CKDWORD LockCount;

        Resource()
            : Type(0), Handle(0), FormatFlags(0), DeviceStride(0), DeviceLayout(0),
              Locked(FALSE), LockStart(0), LockCount(0) {
            memset(&Layout, 0, sizeof(Layout));
        }
    };

    struct PendingReadback {
        CKReadbackCallback Callback;
        void *User;
        CKRECT Rect;
        CKBOOL HasRect;
        VXBUFFER_TYPE Buffer;
        // Filled by the device callback
        CKBOOL Done;
        CKBOOL Success;
        CKDWORD Width;
        CKDWORD Height;
        CKDWORD Pitch;
        VX_PIXELFORMAT Format;
        CKBOOL YFlip;
        std::vector<CKBYTE> Data;
        CKTranslatedContext *Owner;

        PendingReadback()
            : Callback(NULL), User(NULL), HasRect(FALSE), Buffer(VXBUFFER_BACKBUFFER), Done(FALSE),
              Success(FALSE), Width(0), Height(0), Pitch(0), Format(UNKNOWN_PF), YFlip(FALSE), Owner(NULL) {
            Rect.left = Rect.top = Rect.right = Rect.bottom = 0;
        }
    };

    static uint64_t ResourceKey(CKDWORD Type, CKDWORD Handle) { return ((uint64_t)Type << 32) | Handle; }
    Resource *FindResource(CKDWORD Type, CKDWORD Handle);
    const Resource *FindResource(CKDWORD Type, CKDWORD Handle) const;
    void Diag(CKRST_DIAGNOSTIC Kind) { ++m_Stats.Diagnostics[Kind]; }
    CKRST_DIAGNOSTIC DrawRejectDiagnostic() const;
    void RecordDrawApproximations();

    // Frame flow
    void PrepareFrameTarget();
    CKBOOL EnsureEncoder();
    CKBOOL OpenPass(CKDWORD FrameBuffer, const CKRECT &Rect, CKDWORD ClearFlags, CKDWORD Color,
                    float Z, CKDWORD Stencil, const char *Name);
    CKBOOL EnsureDrawPass();
    CKRECT CurrentTargetRect() const;
    CKRECT WindowRect() const;
    CKDWORD CurrentSceneFrameBuffer() const;
    CKBOOL CompositeScene();
    void FinishFrame();
    void ApplyOptions();
    void ResetStateMirror();
    void ReleaseFrameScratch();
    void ReleaseTarget();
    void UpdateAlphaTestPrecision();

    // Draw helpers
    CKBOOL ValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL CheckDeviceForDraw();
    void CountDraw(VXPRIMITIVETYPE Type, int ElementCount);
    CKBOOL SubmitVertexBuffer(VXPRIMITIVETYPE Type, const Resource &VB, CKDWORD IBHandle, CKDWORD BaseVertex,
                              CKDWORD VertexCount, CKDWORD StartIndex, CKDWORD IndexCount);

    // Readback helpers
    static void ReadbackCallbackAdapter(void *UserData, CKDWORD FrameBuffer, CKDWORD Width, CKDWORD Height,
                                        CKDWORD Pitch, VX_PIXELFORMAT Format, const void *Data, CKDWORD Size,
                                        CKBOOL YFlip);
    CKBOOL BuildReadbackImage(const PendingReadback &Readback, VxImageDescEx &Desc, std::vector<CKBYTE> &Pixels) const;
    void DeliverReadbacks();
    void CancelReadbacks();
    CKBOOL ValidateRect(const CKRECT *Rect, CKDWORD Width, CKDWORD Height) const;

    CKTranslatedDriver *m_TranslatedDriver;
    CKRasterizerDevice *m_Device;
    CKFixedFunctionPipeline m_FFP;
    CKPostprocessPass m_Postprocess;
    CKRasterizerOptions m_Options;

    // Verbatim state mirror (spec 4.10)
    CKDWORD m_RenderStates[VXRENDERSTATE_MAXSTATE];
    CKDWORD m_StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE];
    CKDWORD m_Textures[CKRST_MAX_TEXTURE_STAGES];
    VxMatrix m_Matrices[CKRST_MATRIX_SLOT_COUNT];
    CKLightData m_Lights[CKRST_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKRST_MAX_LIGHTS];
    CKMaterialData m_Material;
    CKViewportData m_Viewport;
    VxPlane m_ClipPlanes[CKRST_MAX_USER_CLIP_PLANES];

    // Frame
    CKBOOL m_Created;
    CKBOOL m_ShuttingDown;
    CKBOOL m_InScene;
    CKBOOL m_OverlayPhase;
    CKBOOL m_PassOpen;
    CKBOOL m_SceneFrameBufferUsed;
    CKBOOL m_Composited;
    CKBOOL m_FrameTargetDecided;
    CKRasterizerEncoder *m_Encoder;
    CKRenderView m_CurrentView;
    CKDWORD m_NextView;
    CKDWORD m_LastFrameViewCount;
    CKDWORD m_FrameNumber;
    XString m_Marker;

    // Target
    CKDWORD m_Target;
    CKRST_CUBEFACE m_TargetFace;
    CKDWORD m_TargetWidth;
    CKDWORD m_TargetHeight;
    CKDWORD m_TargetFrameBuffer;
    CKDWORD m_TargetDepthTexture;

    // Resources
    std::unordered_map<uint64_t, Resource> m_Resources;
    std::vector<CKDWORD> m_FrameIndexBuffers;   // per-draw index uploads, deleted after Frame()
    CKDWORD m_CopyTexture;
    CKDWORD m_CopyWidth;
    CKDWORD m_CopyHeight;
    CKDWORD m_AppliedMSAA;

    // Readback
    VxMutex m_ReadbackMutex;
    std::vector<PendingReadback *> m_Readbacks;

    // Stats
    CKRenderStats m_Stats;
    CKDWORD m_FrameDrawCalls;
    CKDWORD m_FramePrimitives;
    CKDWORD m_FramePasses;
    CKDWORD m_FrameClears;
    CKDWORD m_FrameTextureUploads;
    CKDWORD m_FrameBufferUploads;
    CKBOOL m_LayoutMismatchLogged;
};

// ===========================================================================
// Entry points
// ===========================================================================

// Wraps a started device library. Returns NULL (and closes the device) when
// the device has no driver.
CKRasterizer *CKTranslatedRasterizerStart(CKRasterizerDeviceLibrary *Device,
                                          CKTranslatedDeviceCloseFunction CloseDevice);
void CKTranslatedRasterizerClose(CKRasterizer *Rasterizer);

// Translation core on top of the built-in NULL device: the engine's fallback
// when no rasterizer plugin loads, and the device the contract tests run on.
CKRasterizer *CKTranslatedNullRasterizerStart(WIN_HANDLE AppWnd);
void CKTranslatedNullRasterizerClose(CKRasterizer *Rasterizer);
void CKTranslatedNullRasterizerGetInfo(CKRasterizerInfo *Info);

#endif // CKTRANSLATEDRASTERIZER_H
