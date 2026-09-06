#ifndef CKNULLBACKEND_H
#define CKNULLBACKEND_H

// NULL backend: a generic resource and command recorder that draws nothing.
// The NULL rasterizer composes it with the fallback driver and shader catalog;
// tests can also construct it without a plugin or a fixed-function pipeline.
//
// Objects are handle-table entries (never reused), constant blocks keep the
// last pushed bytes, passes and draws of the current frame are recorded until
// Submit(), and readbacks deliver a zero image. Shader blobs are nominal;
// program interfaces obey the same contract as native backends.

#include "CKRasterizerBackend.h"

#include <unordered_map>
#include <vector>

struct CKNullObject {
    CKDWORD Type;                  // CKRST_OBJ_*
    CKDWORD Width, Height, Depth;  // textures
    CKDWORD Flags;                 // textures: CKRST_TEXTURE_*
    VX_PIXELFORMAT Format;         // textures
    CKDWORD Size, Stride, Layout;  // buffers (Layout also: vertex layout stride)
    CKBOOL Index32;
    CK_SHADER_STAGE Stage;         // shaders
    CKDWORD VertexShader, PixelShader;
    CKShaderDesc Shader;
    CKBackendProgramDesc Program;
    CKBackendRenderTargetDesc Target;

    CKNullObject()
        : Type(0), Width(0), Height(0), Depth(1), Flags(0), Format(UNKNOWN_PF), Size(0), Stride(0), Layout(0),
          Index32(FALSE), Stage(CKRST_SHADER_VERTEX), VertexShader(0), PixelShader(0) {}
};

struct CKNullPass {
    CKDWORD RenderTarget;
    CKRECT Rect;
    CKDWORD ClearFlags;
    CKDWORD ClearColor;
    float ClearZ;
    CKDWORD ClearStencil;
    XString Name;
};

struct CKNullDraw {
    CKDWORD Pass;                  // index into the frame's passes
    CKDWORD Program;
    CKDWORD VertexCount;
    CKDWORD IndexCount;
    CKDWORD SortKey;
    CKBackendPipelineState State;
    CKDWORD Textures[CKBACKEND_MAX_TEXTURE_SLOTS];
    XString Marker;
};

// ===========================================================================
// CKNullBackend
// ===========================================================================

class CKNullBackend : public CKRasterizerBackend {
public:
    explicit CKNullBackend(const CKBackendCaps &Conventions = CKBackendCaps());
    ~CKNullBackend() override;

    // --- Device
    CKERROR Init(const CKBackendInitDesc *Desc) override;
    void Shutdown() override;
    CKERROR Resize(int PosX, int PosY, int Width, int Height) override;
    CKERROR GetDeviceStatus() const override;
    const CKBackendCaps &GetCaps() const override { return m_Caps; }
    CKBOOL IsIdle() const override;
    void SetDebugFlags(CKDWORD Flags) override { m_DebugFlags = Flags; }

    // --- Resources
    CKERROR CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out) override;
    CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                          const VxImageDescEx *Data) override;
    CKERROR CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out) override;
    CKERROR UpdateBuffer(CKBackendBufferKind Kind, CKDWORD Buffer, CKDWORD Offset,
                         CKDWORD Size, const void *Data) override;
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out) override;
    CKERROR CreateProgram(const CKBackendProgramDesc *Desc, CKDWORD *Out) override;
    CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const override;
    CKERROR DestroyObject(CKDWORD Object, CKDWORD Type) override;
    void SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name) override;

    // --- Frame
    CKERROR BeginPass(const CKBackendPassDesc *Desc) override;
    CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out) override;
    CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out) override;
    CKERROR Draw(const CKBackendDraw *Draw) override;
    CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                 CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect) override;
    CKERROR Submit(const CKBackendSubmitDesc &Desc, CKDWORD *FrameNumber) override;

    // --- Readback
    CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKBackendReadbackTicket *Ticket) override;

    // --- Misc
    const CKBackendStats &GetStats() const override { return m_Stats; }

    // --- Records (this frame; cleared by Present)
    const std::vector<CKNullPass> &GetPasses() const { return m_Passes; }
    const std::vector<CKNullDraw> &GetDraws() const { return m_Draws; }
    CKDWORD GetCurrentPass() const { return m_Passes.empty() ? 0 : (CKDWORD)m_Passes.size() - 1; }
    CKBOOL IsPassOpen() const { return m_PassOpen; }
    CKDWORD GetFrameNumber() const { return m_FrameNumber; }
    const CKBackendPipelineState &GetPipelineState() const { return m_State; }
    CKDWORD GetBoundTexture(CKDWORD Slot) const { return Slot < CKBACKEND_MAX_TEXTURE_SLOTS ? m_Textures[Slot] : 0; }
    const std::vector<CKBYTE> &GetConstants(CKDWORD Slot) const {
        static const std::vector<CKBYTE> empty;
        return Slot < CKBACKEND_MAX_CONSTANT_SLOTS ? m_Constants[Slot] : empty;
    }
    const CKNullObject *FindObject(CKDWORD Handle) const;
    int GetObjectCount(CKDWORD TypeMask) const;
    // Pseudo uniform handles the tests key their expectations on.
    CKDWORD GetBlockUniformForTests(CKDWORD Slot) const { return Slot < CKBACKEND_MAX_CONSTANT_SLOTS ? 1 + Slot : 0; }
    CKDWORD GetSamplerUniformForTests(CKDWORD Slot) const { return Slot < CKBACKEND_MAX_TEXTURE_SLOTS ? 100 + Slot : 0; }

protected:
    CKDWORD AllocateHandle(const CKNullObject &Object);
    CKNullObject *FindObject(CKDWORD Handle);

    CKBackendCaps m_Conventions;
    struct Readback : CKBackendReadback { CKDWORD AvailableFrame = 0; };
    std::vector<std::shared_ptr<Readback>> m_Readbacks;
    CKBackendCaps m_Caps;
    CKBackendStats m_Stats;
    CKBOOL m_Initialized;
    CKBOOL m_ShuttingDown;
    CKDWORD m_DebugFlags;
    int m_PosX, m_PosY;
    CKDWORD m_Width, m_Height;
    CKDWORD m_FrameNumber;
    CKDWORD m_NextHandle;
    std::unordered_map<CKDWORD, CKNullObject> m_Objects;

    // Frame
    CKBOOL m_PassOpen;
    std::vector<CKNullPass> m_Passes;
    std::vector<CKNullDraw> m_Draws;
    CKDWORD m_FrameBlits, m_FrameTextureUploads, m_FrameBufferUploads;
    CKBackendPipelineState m_State;
    CKDWORD m_Textures[CKBACKEND_MAX_TEXTURE_SLOTS];
    CKSamplerDesc m_Samplers[CKBACKEND_MAX_TEXTURE_SLOTS];
    std::vector<CKBYTE> m_Constants[CKBACKEND_MAX_CONSTANT_SLOTS];
    XString m_Marker;
    std::vector<std::vector<CKBYTE> > m_TransientVertices;
    std::vector<std::vector<CKBYTE> > m_TransientIndices;
};

#endif // CKNULLBACKEND_H
