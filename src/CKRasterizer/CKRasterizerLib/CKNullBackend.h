#ifndef CKNULLBACKEND_H
#define CKNULLBACKEND_H

// NULL backend: a CKRasterizerBackend that accepts everything and draws
// nothing. It is the engine's fallback when no rasterizer plugin loads and
// the base of the recording backend the contract and pipeline tests run on.
//
// Objects are handle-table entries (never reused), constant blocks keep the
// last pushed data, passes and draws of the current frame are recorded until
// Present(), readbacks deliver a zero image. The backend reports programmable
// shaders (any blob is accepted) so the fixed-function pipeline runs its full
// path; the shader profile is nominal and comes from the driver.

#include "CKRasterizerBackend.h"

#include <unordered_map>
#include <vector>

class CKNullBackend;
class CKNullBackendDriver;

struct CKNullObject {
    CKDWORD Type;                  // CKRST_OBJ_*
    CKDWORD Width, Height, Depth;  // textures
    CKDWORD Flags;                 // textures: CKRST_TEXTURE_*
    VX_PIXELFORMAT Format;         // textures
    CKDWORD Size, Stride, Layout;  // buffers (Layout also: vertex layout stride)
    CKBOOL Index32;
    CK_SHADER_STAGE Stage;         // shaders
    CKDWORD VertexShader, PixelShader;
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
    CKDWORD Textures[CKRST_BACKEND_SLOT_COUNT];
    XString Marker;
};

// ===========================================================================
// CKNullBackendLibrary / CKNullBackendDriver
// ===========================================================================

class CKNullBackendDriver : public CKRasterizerBackendDriver {
public:
    CKNullBackendDriver();
    ~CKNullBackendDriver() override;

    CKRasterizerBackend *CreateBackend() override;
    CKBOOL DestroyBackend(CKRasterizerBackend *Backend) override;

    // Conventions the backends of this driver report (nominal: nothing is
    // rendered). Tests set them before creating a backend.
    CK_SHADER_PROFILE Profile;
    CKBOOL OriginBottomLeft;
    CKBOOL HomogeneousDepth;

protected:
    virtual CKNullBackend *NewBackend();

private:
    XArray<CKNullBackend *> m_Backends;
};

class CKNullBackendLibrary : public CKRasterizerBackendLibrary {
public:
    CKNullBackendLibrary();
    ~CKNullBackendLibrary() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
    int GetDriverCount() const override { return m_Drivers.Size(); }
    CKRasterizerBackendDriver *GetDriver(CKDWORD Index) const override;
    WIN_HANDLE GetMainWindow() const override { return m_MainWindow; }

protected:
    virtual CKNullBackendDriver *NewDriver();

private:
    WIN_HANDLE m_MainWindow;
    XArray<CKNullBackendDriver *> m_Drivers;
};

// ===========================================================================
// CKNullBackend
// ===========================================================================

class CKNullBackend : public CKRasterizerBackend {
public:
    explicit CKNullBackend(CKNullBackendDriver *Driver);
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

    // --- Records (this frame; cleared by Present)
    const std::vector<CKNullPass> &GetPasses() const { return m_Passes; }
    const std::vector<CKNullDraw> &GetDraws() const { return m_Draws; }
    CKDWORD GetCurrentPass() const { return m_Passes.empty() ? 0 : (CKDWORD)m_Passes.size() - 1; }
    CKBOOL IsPassOpen() const { return m_PassOpen; }
    CKDWORD GetFrameNumber() const { return m_FrameNumber; }
    const CKBackendPipelineState &GetPipelineState() const { return m_State; }
    CKDWORD GetBoundTexture(CKDWORD Slot) const { return Slot < CKRST_BACKEND_SLOT_COUNT ? m_Textures[Slot] : 0; }
    const std::vector<float> &GetConstants(CKBackendConstantBlock Block) const { return m_Constants[Block]; }
    CKDWORD GetPaletteColor(CKDWORD Index) const { return Index < 16 ? m_Palette[Index] : 0; }
    const CKNullObject *FindObject(CKDWORD Handle) const;
    int GetObjectCount(CKDWORD TypeMask) const;
    // Pseudo uniform handles the tests key their expectations on.
    CKDWORD GetBlockUniformForTests(CKBackendConstantBlock Block) const { return 1 + (CKDWORD)Block; }
    CKDWORD GetSamplerUniformForTests(CKDWORD Slot) const { return Slot < CKRST_BACKEND_SLOT_COUNT ? 100 + Slot : 0; }
    CKNullBackendDriver *GetDriver() const { return m_Driver; }

protected:
    CKDWORD AllocateHandle(const CKNullObject &Object);
    CKNullObject *FindObject(CKDWORD Handle);

    CKNullBackendDriver *m_Driver;
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
    CKDWORD m_Textures[CKRST_BACKEND_SLOT_COUNT];
    CKSamplerDesc m_Samplers[CKRST_BACKEND_SLOT_COUNT];
    std::vector<float> m_Constants[CKRST_BLOCK_COUNT];
    XString m_Marker;
    std::vector<std::vector<CKBYTE> > m_TransientVertices;
    std::vector<std::vector<CKBYTE> > m_TransientIndices;
    CKDWORD m_Palette[16];
};

#endif // CKNULLBACKEND_H
