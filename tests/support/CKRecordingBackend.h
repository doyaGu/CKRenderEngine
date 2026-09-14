#ifndef CKRECORDINGBACKEND_H
#define CKRECORDINGBACKEND_H

// Test-only resource and command recorder. It draws nothing and lets FFP tests
// inspect emitted resources, passes, bindings, constants, and draw commands.
//
// Objects are handle-table entries (never reused), constant blocks keep the
// last pushed bytes, passes and draws of the current frame are recorded until
// Submit(), and readbacks deliver a zero image. Shader blobs are nominal;
// program interfaces follow the same validation rules as native backends.

#include "CKRasterizerContextData.h"

#include <memory>
#include <unordered_map>
#include <vector>

struct CKRecordingReadback {
    XArray<CKBYTE> Data;
    CKBOOL Complete = FALSE;
    CKERROR Error = CK_OK;
    CKDWORD AvailableFrame = 0;
};
typedef std::shared_ptr<CKRecordingReadback> CKRecordingReadbackTicket;

struct CKRecordingStats {
    CKDWORD Frames;
    CKDWORD Passes;
    CKDWORD Draws;
    CKDWORD Blits;
    CKDWORD TextureUploads;
    CKDWORD BufferUploads;

    CKRecordingStats()
        : Frames(0), Passes(0), Draws(0), Blits(0), TextureUploads(0),
          BufferUploads(0) {}
};

struct CKRecordingObject {
    CKDWORD Type;                  // CKRST_OBJ_*
    CKDWORD Width, Height, Depth;  // textures
    CKDWORD Flags;                 // textures: CKRST_TEXTURE_*
    VX_PIXELFORMAT Format;         // textures
    CKDWORD Size, Stride, Layout;  // buffers (Layout also: vertex layout stride)
    CKBOOL Index32;
    CK_SHADER_STAGE Stage;         // shaders
    CKDWORD VertexShader, PixelShader;
    CKShaderDesc Shader;
    CKFFProgramDesc Program;
    CKRenderTargetDesc Target;
    XString Name;

    CKRecordingObject()
        : Type(0), Width(0), Height(0), Depth(1), Flags(0), Format(UNKNOWN_PF), Size(0), Stride(0), Layout(0),
          Index32(FALSE), Stage(CKRST_SHADER_VERTEX), VertexShader(0), PixelShader(0) {}
};

struct CKRecordingPass {
    CKDWORD RenderTarget;
    CKRECT Rect;
    CKDWORD ClearFlags;
    CKDWORD ClearColor;
    float ClearZ;
    CKDWORD ClearStencil;
    XString Name;
};

struct CKRecordingDraw {
    CKDWORD Pass;                  // index into the frame's passes
    CKDWORD Program;
    CKDWORD VertexCount;
    CKDWORD IndexCount;
    CKDWORD SortKey;
    CKFFPipelineState State;
    CKDWORD Textures[CKFF_TEXTURE_SLOT_COUNT];
    XString Marker;
};

// ===========================================================================
// CKRecordingBackend
// ===========================================================================

class CKRecordingBackend {
public:
    explicit CKRecordingBackend(const CKRasterizerDeviceCaps &Conventions = CKRasterizerDeviceCaps());
    virtual ~CKRecordingBackend();

    // --- Device
    virtual CKERROR Init(const CKRasterizerInitParameters *Desc);
    virtual void Shutdown();
    virtual CKERROR Resize(int PosX, int PosY, int Width, int Height);
    virtual CKERROR GetDeviceStatus() const;
    virtual const CKRasterizerDeviceCaps &GetCaps() const { return m_Caps; }
    virtual CKBOOL IsIdle() const;
    virtual void SetDebugFlags(CKDWORD Flags) { m_DebugFlags = Flags; }

    // --- Resources
    virtual CKERROR CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out);
    virtual CKERROR UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                                  const VxImageDescEx *Data);
    virtual CKERROR CreateDepthTexture(const CKDepthTextureDesc *Desc, CKDWORD *Out);
    virtual CKERROR CreateRenderTarget(const CKRenderTargetDesc *Desc, CKDWORD *Out);
    virtual CKERROR CreateBuffer(const CKBufferDesc *Desc, CKDWORD *Out);
    virtual CKERROR UpdateBuffer(const CKBufferUpdateDesc *Desc);
    virtual CKERROR CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out);
    virtual CKERROR CreateShader(const CKShaderDesc *Desc, CKDWORD *Out);
    virtual CKERROR CreateProgram(const CKFFProgramDesc *Desc, CKDWORD *Out);
    virtual CKBOOL IsObjectAlive(CKDWORD Object, CKDWORD Type) const;
    virtual CKERROR DestroyObject(CKDWORD Object, CKDWORD Type);
    virtual CKERROR SetObjectName(CKDWORD Object, CKDWORD Type,
                                  const char *Name);

    // --- Frame
    virtual CKERROR BeginPass(const CKRenderPassDesc *Desc);
    virtual CKBOOL AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKTransientVertexData *Out);
    virtual CKBOOL AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKTransientIndexData *Out);
    virtual CKERROR Draw(const CKDrawCommand *Draw);
    virtual CKERROR Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                         CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect);
    virtual CKERROR PresentTexture(CKDWORD Texture, CKDWORD Width, CKDWORD Height,
                                   CKPresentSync Sync) {
        (void)Texture; (void)Width; (void)Height; (void)Sync;
        return CKERR_NOTIMPLEMENTED;
    }
    virtual CKERROR Submit(CKPresentSync Sync, CKBOOL PresentWindow, CKDWORD *FrameNumber);
    virtual CKQWORD GetLastSubmitId() const { return m_LastSubmitId; }
    virtual CKQWORD GetCompletedSubmitId() { return m_CompletedSubmitId; }

    // --- Readback
    virtual CKERROR ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback,
                                CKRecordingReadbackTicket *Ticket);
    virtual CKReadbackState PollReadback(const CKRecordingReadbackTicket &Ticket,
                                                CKBOOL Wait) {
        (void)Wait;
        if (!Ticket || Ticket->Error != CK_OK)
            return CKRST_READBACK_FAILED;
        return Ticket->Complete ? CKRST_READBACK_READY : CKRST_READBACK_NEEDS_SUBMIT;
    }

    // --- Misc
    virtual uint64_t GetDrawApproximationMask() const { return 0; }
    virtual const CKRecordingStats &GetStats() const { return m_Stats; }

    // --- Records (this frame; cleared by Present)
    const std::vector<CKRecordingPass> &GetPasses() const { return m_Passes; }
    const std::vector<CKRecordingDraw> &GetDraws() const { return m_Draws; }
    CKDWORD GetCurrentPass() const { return m_Passes.empty() ? 0 : (CKDWORD)m_Passes.size() - 1; }
    CKBOOL IsPassOpen() const { return m_PassOpen; }
    CKDWORD GetFrameNumber() const { return m_FrameNumber; }
    const CKFFPipelineState &GetPipelineState() const { return m_State; }
    CKDWORD GetBoundTexture(CKDWORD Slot) const { return Slot < CKFF_TEXTURE_SLOT_COUNT ? m_Textures[Slot] : 0; }
    const std::vector<CKBYTE> &GetConstants(CKDWORD Slot) const {
        static const std::vector<CKBYTE> empty;
        return Slot < CKFF_CONSTANT_SLOT_COUNT ? m_Constants[Slot] : empty;
    }
    const CKRecordingObject *FindObject(CKDWORD Handle) const;
    int GetObjectCount(CKDWORD TypeMask) const;
    // Pseudo uniform handles the tests key their expectations on.
    CKDWORD GetBlockUniformForTests(CKDWORD Slot) const { return Slot < CKFF_CONSTANT_SLOT_COUNT ? 1 + Slot : 0; }
    CKDWORD GetSamplerUniformForTests(CKDWORD Slot) const { return Slot < CKFF_TEXTURE_SLOT_COUNT ? 100 + Slot : 0; }

protected:
    CKDWORD AllocateHandle(const CKRecordingObject &Object);
    CKRecordingObject *FindObject(CKDWORD Handle);

    CKRasterizerDeviceCaps m_Conventions;
    std::vector<CKRecordingReadbackTicket> m_Readbacks;
    CKRasterizerDeviceCaps m_Caps;
    CKRecordingStats m_Stats;
    CKBOOL m_Initialized;
    CKBOOL m_ShuttingDown;
    CKDWORD m_DebugFlags;
    int m_PosX, m_PosY;
    CKDWORD m_Width, m_Height;
    CKDWORD m_FrameNumber;
    CKQWORD m_LastSubmitId;
    CKQWORD m_CompletedSubmitId;
    CKDWORD m_NextHandle;
    std::unordered_map<CKDWORD, CKRecordingObject> m_Objects;

    // Frame
    CKBOOL m_PassOpen;
    std::vector<CKRecordingPass> m_Passes;
    std::vector<CKRecordingDraw> m_Draws;
    CKDWORD m_FrameBlits, m_FrameTextureUploads, m_FrameBufferUploads;
    CKFFPipelineState m_State;
    CKDWORD m_Textures[CKFF_TEXTURE_SLOT_COUNT];
    CKSamplerDesc m_Samplers[CKFF_TEXTURE_SLOT_COUNT];
    std::vector<CKBYTE> m_Constants[CKFF_CONSTANT_SLOT_COUNT];
    XString m_Marker;
    std::vector<std::vector<CKBYTE> > m_TransientVertices;
    std::vector<std::vector<CKBYTE> > m_TransientIndices;
};

#endif // CKRECORDINGBACKEND_H
