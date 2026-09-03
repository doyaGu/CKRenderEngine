#include "CKDeviceBackend.h"
#include "CKFFShaderABI.h"

#include <string.h>

CKDeviceBackend::CKDeviceBackend(CKRasterizerDevice *device)
    : m_Device(device), m_Encoder(NULL), m_Initialized(FALSE), m_DebugFlags(0), m_CurrentView(0), m_NextView(0),
      m_LastFrameViewCount(0), m_PassOpen(FALSE), m_FramePasses(0), m_FrameDraws(0), m_FrameBlits(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0), m_Marker(NULL)
{
    memset(m_Slots, 0, sizeof(m_Slots));
    memset(m_BlockUniforms, 0, sizeof(m_BlockUniforms));
    memset(m_SamplerUniforms, 0, sizeof(m_SamplerUniforms));
}

CKDeviceBackend::~CKDeviceBackend()
{
    Shutdown();
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

CKERROR CKDeviceBackend::Init(const CKBackendInitDesc *Desc)
{
    if (!m_Device || !Desc)
        return CKERR_INVALIDPARAMETER;
    if (m_Initialized)
        return CKERR_INVALIDOPERATION;
    const CKERROR err = m_Device->Create(Desc->Window, Desc->PosX, Desc->PosY, Desc->Width, Desc->Height, Desc->Bpp,
                                         Desc->Fullscreen, Desc->RefreshRate, Desc->ZBpp, Desc->StencilBpp);
    if (err != CK_OK)
        return err;
    m_Initialized = TRUE;
    m_DebugFlags = Desc->DebugFlags;
    m_Device->SetDebug(m_DebugFlags);
    RefreshCaps();
    if (!CreateUniforms()) {
        Shutdown();
        return CKERR_INVALIDOPERATION;
    }
    return CK_OK;
}

void CKDeviceBackend::Shutdown()
{
    if (!m_Device)
        return;
    if (m_Encoder) {
        m_Device->EndEncoder(m_Encoder);
        m_Encoder = NULL;
    }
    DestroyUniforms();
    if (m_Initialized) {
        m_Device->BeginShutdown();
        m_Initialized = FALSE;
    }
    m_TransientVertices.clear();
    m_TransientIndices.clear();
    m_PassOpen = FALSE;
    m_NextView = 0;
    m_LastFrameViewCount = 0;
}

CKERROR CKDeviceBackend::Resize(int PosX, int PosY, int Width, int Height)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (m_Encoder)
        return CKERR_INVALIDOPERATION;
    const CKERROR err = m_Device->Resize(PosX, PosY, Width, Height, 0);
    if (err == CK_OK)
        RefreshCaps();
    return err;
}

CKERROR CKDeviceBackend::GetDeviceStatus() const
{
    return m_Initialized ? m_Device->GetDeviceStatus() : CKERR_INVALIDOPERATION;
}

CKBOOL CKDeviceBackend::IsIdle() const
{
    if (m_Encoder)
        return FALSE;
    return m_Device ? m_Device->IsIdle() : TRUE;
}

void CKDeviceBackend::SetDebugFlags(CKDWORD Flags)
{
    m_DebugFlags = Flags;
    if (m_Initialized)
        m_Device->SetDebug(Flags);
}

void CKDeviceBackend::RefreshCaps()
{
    m_Caps = CKBackendCaps();
    CKRasterizerDeviceCapsDesc caps;
    if (m_Device->GetCaps(&caps) == CK_OK) {
        m_Caps.Features = caps.Features;
        m_Caps.MaxTextureSize = caps.MaxTextureSize;
        m_Caps.MaxTextureBindings = caps.MaxTextureBindings;
        m_Caps.MaxPasses = caps.MaxRenderViews;
    }
    m_Caps.MaxMSAASamples = 16; // the device creates MSAA targets on demand (CKRST_TEXTURE_MSAA_Xn)
    CKRasterizerTargetDesc target;
    if (m_Device->GetTargetDesc(&target) == CK_OK) {
        m_Caps.ShaderProfile = target.ShaderProfile;
        m_Caps.OriginBottomLeft = target.OriginBottomLeft;
        m_Caps.HomogeneousDepth = target.HomogeneousDepth;
    }
}

CKBOOL CKDeviceBackend::CreateUniforms()
{
    for (int block = 0; block < CKRST_BLOCK_COUNT; ++block) {
        const CKBackendConstantBlockDesc &info = CKBackendConstantBlockInfo((CKBackendConstantBlock)block);
        CKUniformDesc desc;
        desc.Name = (CKSTRING)info.Name;
        desc.Type = info.Mat4 ? CKRST_UNIFORM_MAT4 : CKRST_UNIFORM_VEC4;
        desc.Count = info.Count;
        if (m_Device->CreateUniform(&desc, &m_BlockUniforms[block]) != CK_OK)
            return FALSE;
    }
    for (CKDWORD slot = 0; slot < CKRST_BACKEND_SLOT_COUNT; ++slot) {
        CKUniformDesc desc;
        desc.Name = (CKSTRING)CKBackendSamplerSlotName(slot);
        desc.Type = CKRST_UNIFORM_SAMPLER;
        desc.Count = 1;
        if (m_Device->CreateUniform(&desc, &m_SamplerUniforms[slot]) != CK_OK)
            return FALSE;
    }
    return TRUE;
}

void CKDeviceBackend::DestroyUniforms()
{
    for (int block = 0; block < CKRST_BLOCK_COUNT; ++block) {
        if (m_BlockUniforms[block])
            m_Device->DeleteObject(m_BlockUniforms[block], CKRST_OBJ_UNIFORM);
        m_BlockUniforms[block] = 0;
    }
    for (CKDWORD slot = 0; slot < CKRST_BACKEND_SLOT_COUNT; ++slot) {
        if (m_SamplerUniforms[slot])
            m_Device->DeleteObject(m_SamplerUniforms[slot], CKRST_OBJ_UNIFORM);
        m_SamplerUniforms[slot] = 0;
    }
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

CKERROR CKDeviceBackend::CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized || !Desc)
        return CKERR_INVALIDPARAMETER;
    const CKERROR err = m_Device->CreateTexture(Desc, Data, Out);
    if (err == CK_OK && Data)
        ++m_FrameTextureUploads;
    return err;
}

CKERROR CKDeviceBackend::UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                                       const VxImageDescEx *Data)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    const CKERROR err = m_Device->UpdateTexture(Texture, Mip, Face, Region, Data);
    if (err == CK_OK)
        ++m_FrameTextureUploads;
    return err;
}

CKERROR CKDeviceBackend::CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized || !Desc)
        return CKERR_INVALIDPARAMETER;
    CKDepthTextureDesc depth = {};
    depth.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL | CKRSTTextureMSAAFlag(Desc->Samples);
    depth.Width = Desc->Width;
    depth.Height = Desc->Height;
    depth.DepthFormat = Desc->Format;
    depth.MipMapCount = 1;
    return m_Device->CreateDepthTexture(&depth, Out);
}

CKERROR CKDeviceBackend::CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized || !Desc || !Desc->ColorTexture)
        return CKERR_INVALIDPARAMETER;
    CKFrameBufferAttachmentDesc color;
    color.Texture = Desc->ColorTexture;
    color.Mip = Desc->ColorMip;
    color.Layer = Desc->ColorLayer;
    CKFrameBufferDesc fb;
    fb.Color = &color;
    fb.ColorCount = 1;
    fb.DepthStencil.Texture = Desc->DepthTexture;
    fb.DepthStencil.Mip = 0;
    fb.DepthStencil.Layer = 0;
    return m_Device->CreateFrameBuffer(&fb, Out);
}

CKERROR CKDeviceBackend::CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized || !Desc || Desc->Size == 0)
        return CKERR_INVALIDPARAMETER;
    CKERROR err;
    if (Desc->Kind == CKRST_BACKEND_BUFFER_VERTEX) {
        if (Desc->Stride == 0 || (Desc->Size % Desc->Stride) != 0)
            return CKERR_INVALIDPARAMETER;
        CKVertexBufferDesc vb;
        vb.m_Flags = CKRST_VB_VALID | (Desc->Dynamic ? CKRST_VB_DYNAMIC : 0);
        vb.m_VertexSize = Desc->Stride;
        vb.m_MaxVertexCount = Desc->Size / Desc->Stride;
        err = m_Device->CreateVertexBuffer(&vb, Desc->InitialData, Out);
    } else {
        const CKDWORD indexSize = Desc->Index32 ? 4 : 2;
        if ((Desc->Size % indexSize) != 0)
            return CKERR_INVALIDPARAMETER;
        CKIndexBufferDesc ib;
        ib.m_Flags = CKRST_VB_VALID | (Desc->Dynamic ? CKRST_VB_DYNAMIC : 0);
        ib.m_MaxIndexCount = Desc->Size / indexSize;
        err = m_Device->CreateIndexBuffer(&ib, Desc->Index32, Desc->InitialData, Out);
    }
    if (err == CK_OK && Desc->InitialData)
        ++m_FrameBufferUploads;
    return err;
}

CKERROR CKDeviceBackend::UpdateBuffer(CKDWORD Buffer, CKDWORD Offset, CKDWORD Size, const void *Data)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    CKERROR err;
    if (m_Device->IsObjectAlive(Buffer, CKRST_OBJ_VERTEXBUFFER))
        err = m_Device->UpdateVertexBuffer(Buffer, Offset, Size, Data);
    else if (m_Device->IsObjectAlive(Buffer, CKRST_OBJ_INDEXBUFFER))
        err = m_Device->UpdateIndexBuffer(Buffer, Offset, Size, Data);
    else
        return CKERR_INVALIDPARAMETER;
    if (err == CK_OK)
        ++m_FrameBufferUploads;
    return err;
}

CKERROR CKDeviceBackend::CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized || !Desc)
        return CKERR_INVALIDPARAMETER;
    return m_Device->CreateVertexLayout(Desc, Out);
}

CKERROR CKDeviceBackend::CreateShader(const CKShaderDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized || !Desc)
        return CKERR_INVALIDPARAMETER;
    return m_Device->CreateShader(Desc, Out);
}

CKERROR CKDeviceBackend::CreateProgram(CKDWORD VertexShader, CKDWORD PixelShader, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized || !VertexShader || !PixelShader)
        return CKERR_INVALIDPARAMETER;
    CKProgramDesc desc;
    desc.VertexShader = VertexShader;
    desc.PixelShader = PixelShader;
    desc.ConsumeShaders = FALSE;
    return m_Device->CreateProgram(&desc, Out);
}

CKBOOL CKDeviceBackend::IsObjectAlive(CKDWORD Object, CKDWORD Type) const
{
    return m_Initialized && Object != 0 && m_Device->IsObjectAlive(Object, Type);
}

CKERROR CKDeviceBackend::DestroyObject(CKDWORD Object, CKDWORD Type)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    return m_Device->DeleteObject(Object, Type);
}

void CKDeviceBackend::SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name)
{
    if (m_Initialized && Object)
        m_Device->SetResourceName(Object, Type, (CKSTRING)Name);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

CKBOOL CKDeviceBackend::EnsureEncoder()
{
    if (m_Encoder)
        return TRUE;
    if (!m_Initialized)
        return FALSE;
    m_Encoder = m_Device->BeginEncoder();
    return m_Encoder != NULL;
}

CKERROR CKDeviceBackend::BeginPass(const CKBackendPassDesc *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (!EnsureEncoder())
        return CKERR_INVALIDOPERATION;
    CKRenderView view;
    CKDWORD clearFlags = Desc->ClearFlags;
    if (m_NextView < CKRST_MAX_RENDER_VIEWS && (m_Caps.MaxPasses == 0 || m_NextView < m_Caps.MaxPasses)) {
        view = (CKRenderView)m_NextView++;
    } else {
        // Out of views: keep drawing into the last pass; a clear would apply
        // to the whole pass, so it is dropped.
        view = m_CurrentView;
        clearFlags = 0;
    }
    m_Device->SetViewMode(view, CKRST_VIEWMODE_SEQUENTIAL);
    m_Device->SetViewFrameBuffer(view, Desc->RenderTarget);
    m_Device->SetViewRect(view, Desc->Rect);
    m_Device->SetViewClear(view, clearFlags, Desc->ClearColor, Desc->ClearZ, Desc->ClearStencil);
    if ((m_DebugFlags & CKRST_DEBUG_DRAWMAP) && Desc->Name)
        m_Device->SetViewName(view, (CKSTRING)Desc->Name);
    m_Encoder->Touch(view);
    m_CurrentView = view;
    m_PassOpen = TRUE;
    ++m_FramePasses;
    return m_Encoder->GetStatus();
}

void CKDeviceBackend::SetPipelineState(const CKBackendPipelineState *State)
{
    if (State)
        m_State = *State;
}

void CKDeviceBackend::BindTexture(CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler)
{
    if (Slot >= CKRST_BACKEND_SLOT_COUNT)
        return;
    m_Slots[Slot].Texture = Texture;
    m_Slots[Slot].HasSampler = Sampler != NULL;
    if (Sampler)
        m_Slots[Slot].Sampler = *Sampler;
}

void CKDeviceBackend::PushConstants(CKBackendConstantBlock Block, const void *Data, CKDWORD Vec4Count)
{
    if ((int)Block < 0 || (int)Block >= CKRST_BLOCK_COUNT || !Data || Vec4Count == 0)
        return;
    if (!EnsureEncoder())
        return;
    const CKBackendConstantBlockDesc &info = CKBackendConstantBlockInfo(Block);
    const CKDWORD count = info.Mat4 ? Vec4Count / 4 : Vec4Count;
    if (count == 0)
        return;
    m_Encoder->SetUniform(m_BlockUniforms[Block], Data, count);
}

void CKDeviceBackend::SetMarker(const char *Name)
{
    m_Marker = Name;
}

CKBOOL CKDeviceBackend::AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out)
{
    if (!Out || Count == 0 || !m_Initialized)
        return FALSE;
    *Out = CKBackendTransientVertices();
    CKTransientVertexBuffer tvb;
    memset(&tvb, 0, sizeof(tvb));
    if (!m_Device->AllocTransientVertexBuffer(&tvb, Count, Layout))
        return FALSE;
    m_TransientVertices.push_back(tvb);
    Out->Data = tvb.Data;
    Out->Count = tvb.VertexCount;
    Out->Stride = tvb.Stride;
    Out->Layout = Layout;
    Out->Token = (CKDWORD)m_TransientVertices.size();
    return TRUE;
}

CKBOOL CKDeviceBackend::AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out)
{
    if (!Out || Count == 0 || !m_Initialized)
        return FALSE;
    *Out = CKBackendTransientIndices();
    CKTransientIndexBuffer tib;
    memset(&tib, 0, sizeof(tib));
    if (!m_Device->AllocTransientIndexBuffer(&tib, Count, Index32))
        return FALSE;
    m_TransientIndices.push_back(tib);
    Out->Data = tib.Data;
    Out->Count = tib.IndexCount;
    Out->Index32 = Index32;
    Out->Token = (CKDWORD)m_TransientIndices.size();
    return TRUE;
}

CKBOOL CKDeviceBackend::BindGeometry(const CKBackendDraw *Draw)
{
    if (Draw->TransientVertices) {
        const CKDWORD token = Draw->TransientVertices->Token;
        if (token == 0 || token > m_TransientVertices.size())
            return FALSE;
        m_Encoder->SetTransientVertexBuffer(0, &m_TransientVertices[token - 1]);
    } else if (Draw->VertexBuffer) {
        m_Encoder->SetVertexBuffer(0, Draw->VertexBuffer, Draw->StartVertex, Draw->VertexCount, Draw->Layout);
    } else {
        return FALSE;
    }
    if (Draw->Stream1Transient) {
        const CKDWORD token = Draw->Stream1Transient->Token;
        if (token == 0 || token > m_TransientVertices.size())
            return FALSE;
        m_Encoder->SetTransientVertexBuffer(1, &m_TransientVertices[token - 1]);
    } else if (Draw->Stream1VertexBuffer) {
        m_Encoder->SetVertexBuffer(1, Draw->Stream1VertexBuffer, Draw->Stream1StartVertex, Draw->VertexCount,
                                   Draw->Stream1Layout);
    }
    if (Draw->TransientIndices) {
        const CKDWORD token = Draw->TransientIndices->Token;
        if (token == 0 || token > m_TransientIndices.size())
            return FALSE;
        m_Encoder->SetTransientIndexBuffer(&m_TransientIndices[token - 1]);
    } else if (Draw->IndexBuffer) {
        m_Encoder->SetIndexBuffer(Draw->IndexBuffer, Draw->StartIndex, Draw->IndexCount);
    }
    return m_Encoder->GetStatus() == CK_OK;
}

CKERROR CKDeviceBackend::Draw(const CKBackendDraw *Draw)
{
    if (!Draw || !Draw->Program)
        return CKERR_INVALIDPARAMETER;
    if (!m_PassOpen || !EnsureEncoder())
        return CKERR_INVALIDOPERATION;

    m_Encoder->SetState(m_State.State);
    m_Encoder->SetStencilRef(m_State.StencilRef & 0xFF);
    m_Encoder->SetStencilMask(m_State.StencilReadMask & 0xFF, m_State.StencilWriteMask & 0xFF);
    m_Encoder->SetScissor(m_State.ScissorEnabled ? &m_State.Scissor : NULL);
    m_Encoder->SetPointSize(m_State.PointSize);
    if (m_Encoder->GetStatus() != CK_OK || !BindGeometry(Draw)) {
        const CKERROR status = m_Encoder->GetStatus() != CK_OK ? m_Encoder->GetStatus() : CKERR_INVALIDPARAMETER;
        m_Encoder->Discard();
        return status;
    }
    for (CKDWORD slot = 0; slot < CKRST_BACKEND_SLOT_COUNT; ++slot) {
        if (!m_Slots[slot].Texture)
            continue;
        m_Encoder->SetTexture(slot, m_SamplerUniforms[slot], m_Slots[slot].Texture,
                              m_Slots[slot].HasSampler ? &m_Slots[slot].Sampler : NULL);
    }
    if (m_Marker) {
        m_Encoder->SetMarker((CKSTRING)m_Marker);
        m_Marker = NULL;
    }
    if (m_Encoder->GetStatus() != CK_OK) {
        const CKERROR status = m_Encoder->GetStatus();
        m_Encoder->Discard();
        return status;
    }
    m_Encoder->Submit(m_CurrentView, Draw->Program, Draw->SortKey, CKRST_DISCARD_ALL);
    const CKERROR status = m_Encoder->GetStatus();
    if (status != CK_OK)
        m_Encoder->Discard();
    else
        ++m_FrameDraws;
    return status;
}

CKERROR CKDeviceBackend::Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                              CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect)
{
    if (!DstTexture || !SrcTexture)
        return CKERR_INVALIDPARAMETER;
    if (DstLayer != 0 || SrcLayer != 0)
        return CKERR_NOTIMPLEMENTED; // the device encoder has no layer parameter
    if (!m_PassOpen || !EnsureEncoder())
        return CKERR_INVALIDOPERATION;
    m_Encoder->Blit(m_CurrentView, DstTexture, DstMip, DstX, DstY, SrcTexture, SrcMip, SrcRect);
    const CKERROR status = m_Encoder->GetStatus();
    if (status == CK_OK)
        ++m_FrameBlits;
    return status;
}

CKERROR CKDeviceBackend::Present(CKBackendPresentMode Mode, CKDWORD *FrameNumber)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (m_Encoder) {
        m_Device->EndEncoder(m_Encoder);
        m_Encoder = NULL;
    }
    CKRST_FRAME_SYNC_MODE sync = CKRST_FRAME_SYNC_IMMEDIATE;
    if (Mode == CKRST_BACKEND_PRESENT_VSYNC)
        sync = CKRST_FRAME_SYNC_VSYNC;
    else if (Mode == CKRST_BACKEND_PRESENT_PRESERVE)
        sync = CKRST_FRAME_SYNC_PRESERVE_PRESENT;
    CKDWORD frame = 0;
    const CKERROR status = m_Device->Frame(sync, CKRST_FRAME_NONE, &frame);
    if (FrameNumber)
        *FrameNumber = frame;
    for (CKDWORD view = m_NextView; view < m_LastFrameViewCount; ++view)
        m_Device->ResetView((CKRenderView)view);
    m_LastFrameViewCount = m_NextView;
    m_NextView = 0;
    m_PassOpen = FALSE;
    m_Marker = NULL;
    m_TransientVertices.clear();
    m_TransientIndices.clear();
    ++m_Stats.Frames;
    m_Stats.Passes = m_FramePasses;
    m_Stats.Draws = m_FrameDraws;
    m_Stats.Blits = m_FrameBlits;
    m_Stats.TextureUploads = m_FrameTextureUploads;
    m_Stats.BufferUploads = m_FrameBufferUploads;
    m_FramePasses = m_FrameDraws = m_FrameBlits = m_FrameTextureUploads = m_FrameBufferUploads = 0;
    return status;
}

// ---------------------------------------------------------------------------
// Readback / misc
// ---------------------------------------------------------------------------

CKERROR CKDeviceBackend::ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKDWORD *AvailableFrame)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    return m_Device->ReadTexture(Texture, Mip, Readback, AvailableFrame);
}

CKERROR CKDeviceBackend::SetPaletteColor(CKDWORD Index, CKDWORD RGBA)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    return m_Device->SetPaletteColor(Index, RGBA);
}
