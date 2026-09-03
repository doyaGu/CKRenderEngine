#ifndef CKDEVICEBACKEND_H
#define CKDEVICEBACKEND_H

// CKRasterizerBackend implemented on top of the v2-shaped internal device
// interface (CKRasterizerDevice + CKRasterizerEncoder). Transitional: it lets
// the translation core move to the backend interface (phase 4.2) while the
// bgfx device is still the v2 class; the native bgfx backend (phase 4.3)
// replaces it and this file goes away with the device interface.

#include "CKRasterizer.h"
#include "CKRasterizerBackend.h"
#include "CKRasterizerDevice.h"

#include <vector>

class CKDeviceBackend : public CKRasterizerBackend {
public:
    explicit CKDeviceBackend(CKRasterizerDevice *device);
    ~CKDeviceBackend() override;

    CKRasterizerDevice *GetDevice() const { return m_Device; }

    // --- Device
    CKERROR Init(const CKBackendInitDesc *Desc) override;
    void Shutdown() override;
    CKERROR Resize(int PosX, int PosY, int Width, int Height) override;
    CKERROR GetDeviceStatus() const override;
    const CKBackendCaps &GetCaps() const override { return m_Caps; }
    CKBOOL IsIdle() const override;
    void SetDebugFlags(CKDWORD Flags) override;

    // --- Resources
    CKERROR CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out) override;
    CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                          const VxImageDescEx *Data) override;
    CKERROR CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out) override;
    CKERROR UpdateBuffer(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data) override;
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateProgram(CKDWORD VertexShader, CKDWORD PixelShader, CKDWORD *Out) override;
    CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const override;
    CKERROR DestroyObject(CKDWORD Object, CKDWORD Type) override;
    void SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name) override;

    // --- Frame
    CKERROR BeginPass(const CKBackendPassDesc *Desc) override;
    void SetPipelineState(const CKBackendPipelineState *State) override;
    void BindTexture(CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler) override;
    CKERROR PushConstants(CKBackendConstantBlock Block, const void *Data, CKDWORD Vec4Count) override;
    void SetMarker(const char *Name) override;
    CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out) override;
    CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out) override;
    CKERROR Draw(const CKBackendDraw *Draw) override;
    CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                 CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect) override;
    CKERROR Present(CKBackendPresentMode Mode, CKDWORD *FrameNumber) override;

    // --- Readback
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKDWORD *AvailableFrame) override;

    // --- Misc
    CKERROR SetPaletteColor(CKDWORD Index, CKDWORD RGBA) override;
    const CKBackendStats &GetStats() const override { return m_Stats; }

    // Test / migration access
    CKRenderView GetCurrentViewForTests() const { return m_CurrentView; }
    CKDWORD GetBlockUniformForTests(CKBackendConstantBlock Block) const { return m_BlockUniforms[Block]; }
    CKDWORD GetSamplerUniformForTests(CKDWORD Slot) const { return Slot < CKRST_BACKEND_SLOT_COUNT ? m_SamplerUniforms[Slot] : 0; }

private:
    struct SlotBinding {
        CKDWORD Texture;
        CKSamplerDesc Sampler;
        CKBOOL HasSampler;
        CKBOOL PendingZero;   // explicit zero binding requested for the next draw
    };

    CKBOOL EnsureEncoder();
    CKBOOL CreateUniforms();
    void DestroyUniforms();
    void RefreshCaps();
    CKBOOL BindGeometry(const CKBackendDraw *Draw);

    CKRasterizerDevice *m_Device;
    CKRasterizerEncoder *m_Encoder;
    CKBackendCaps m_Caps;
    CKBackendStats m_Stats;
    CKBOOL m_Initialized;
    CKDWORD m_DebugFlags;

    // Frame
    CKRenderView m_CurrentView;
    CKDWORD m_NextView;
    CKDWORD m_LastFrameViewCount;
    CKBOOL m_PassOpen;
    CKDWORD m_FramePasses, m_FrameDraws, m_FrameBlits, m_FrameTextureUploads, m_FrameBufferUploads;

    // Draw state
    CKBackendPipelineState m_State;
    SlotBinding m_Slots[CKRST_BACKEND_SLOT_COUNT];
    const char *m_Marker;
    std::vector<CKTransientVertexBuffer> m_TransientVertices;
    std::vector<CKTransientIndexBuffer> m_TransientIndices;

    // Uniforms
    CKDWORD m_BlockUniforms[CKRST_BLOCK_COUNT];
    CKDWORD m_SamplerUniforms[CKRST_BACKEND_SLOT_COUNT];
};

// Backend driver over a device driver: caps, modes and formats are copied
// from the device driver (again on RefreshCaps, the device refines them when
// a context is created); backends are CKDeviceBackend adapters over the
// device contexts the device driver creates.
class CKDeviceBackendDriver : public CKRasterizerBackendDriver {
public:
    explicit CKDeviceBackendDriver(CKRasterizerDeviceDriver *Driver, CKDWORD Index);

    CKRasterizerBackend *CreateBackend() override;
    CKBOOL DestroyBackend(CKRasterizerBackend *Backend) override;
    void RefreshCaps() override;

    CKRasterizerDeviceDriver *GetDeviceDriver() const { return m_Driver; }

private:
    CKRasterizerDeviceDriver *m_Driver;
};

typedef void (*CKDeviceLibraryCloseFunction)(CKRasterizerDeviceLibrary *Device);

// Backend library over a device library. Takes ownership of the device
// library; `CloseDevice` (may be NULL) is called with it on destruction, NULL
// means `delete`.
class CKDeviceBackendLibrary : public CKRasterizerBackendLibrary {
public:
    CKDeviceBackendLibrary(CKRasterizerDeviceLibrary *Device, CKDeviceLibraryCloseFunction CloseDevice);
    ~CKDeviceBackendLibrary() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
    int GetDriverCount() const override { return m_Drivers.Size(); }
    CKRasterizerBackendDriver *GetDriver(CKDWORD Index) const override;
    WIN_HANDLE GetMainWindow() const override { return m_Device ? m_Device->m_MainWindow : NULL; }

    CKRasterizerDeviceLibrary *GetDevice() const { return m_Device; }

private:
    CKRasterizerDeviceLibrary *m_Device;
    CKDeviceLibraryCloseFunction m_CloseDevice;
    XArray<CKDeviceBackendDriver *> m_Drivers;
};

// Translation core over a started device library (bgfx plugin, NULL device,
// test harness). Returns NULL (and closes the device) when the device has no
// driver.
CKRasterizer *CKTranslatedRasterizerStartOverDevice(CKRasterizerDeviceLibrary *Device,
                                                    CKDeviceLibraryCloseFunction CloseDevice);

#endif // CKDEVICEBACKEND_H
