#ifndef CKSDLGPU_BACKEND_H
#define CKSDLGPU_BACKEND_H

#include "CKRasterizerBackend.h"

struct CKSdlGpuDevice;

class CKSdlGpuBackend final : public CKRasterizerBackend {
public:
    CKSdlGpuBackend();
    ~CKSdlGpuBackend() override;
    CKERROR Init(const CKBackendInitDesc *Desc) override;
    void Shutdown() override;
    CKERROR Resize(int PosX, int PosY, int Width, int Height) override;
    CKERROR GetDeviceStatus() const override;
    const CKBackendCaps &GetCaps() const override;
    CKBOOL IsIdle() const override;
    void SetDebugFlags(CKDWORD Flags) override;
    CKERROR CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out) override;
    CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region, const VxImageDescEx *Data) override;
    CKERROR CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out) override;
    CKERROR UpdateBuffer(CKBackendBufferKind Kind, CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data) override;
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out) override;
    CKBOOL SupportsTexture2D(VX_PIXELFORMAT Format) const;
    CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateProgram(const CKBackendProgramDesc *Desc, CKDWORD *Out) override;
    CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const override;
    CKERROR DestroyObject(CKDWORD Object, CKDWORD Type) override;
    void SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name) override;
    CKERROR BeginPass(const CKBackendPassDesc *Desc) override;
    CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out) override;
    CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out) override;
    CKERROR Draw(const CKBackendDraw *Draw) override;
    CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                 CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect) override;
    CKERROR Submit(const CKBackendSubmitDesc &Desc, CKDWORD *SubmissionNumber) override;
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKBackendReadbackTicket *Ticket) override;
    CKBackendReadbackState PollReadback(const CKBackendReadbackTicket &Ticket, CKBOOL Wait) override;
    uint64_t GetDrawApproximationMask() const override;
    const CKBackendStats &GetStats() const override;
private:
    std::unique_ptr<CKSdlGpuDevice> m;
};

#endif
