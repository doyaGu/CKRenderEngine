#include "CKRecordingBackend.h"

#include <stdio.h>
#include <string.h>

// ===========================================================================
// CKRecordingBackend
// ===========================================================================

CKRecordingBackend::CKRecordingBackend(const CKRasterizerDeviceCaps &Conventions)
    : m_Conventions(Conventions), m_Initialized(FALSE), m_ShuttingDown(FALSE), m_DebugFlags(0), m_PosX(0), m_PosY(0),
      m_Width(0), m_Height(0), m_FrameNumber(0), m_LastSubmitId(0),
      m_CompletedSubmitId(0), m_NextHandle(1), m_PassOpen(FALSE), m_FrameBlits(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0)
{
    memset(m_Textures, 0, sizeof(m_Textures));
    memset(m_Samplers, 0, sizeof(m_Samplers));
}

CKRecordingBackend::~CKRecordingBackend()
{
    Shutdown();
}

CKERROR CKRecordingBackend::Init(const CKRasterizerInitParameters *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Desc->Width <= 0 || Desc->Height <= 0)
        return CKERR_INVALIDPARAMETER;

    const CK_SHADER_FORMAT shaderFormat = m_Conventions.ShaderFormat != CKRST_SHADER_FORMAT_UNKNOWN
        ? m_Conventions.ShaderFormat : CKRST_SHADER_FORMAT_DXIL;
    const CK_SHADER_PROFILE shaderProfile = m_Conventions.ShaderProfile != CKRST_SHADER_PROFILE_UNKNOWN
        ? m_Conventions.ShaderProfile : CKRST_SHADER_PROFILE_DX12;
    bool targetAllowed = Desc->ShaderTargets.Size() == 0;
    for (int i = 0; i < Desc->ShaderTargets.Size(); ++i) {
        const CKFFShaderTarget &target = Desc->ShaderTargets[i];
        if (target.Format == shaderFormat && target.Profile == shaderProfile) {
            targetAllowed = true;
            break;
        }
    }
    if (!targetAllowed) {
        return CKERR_NOTIMPLEMENTED;
    }

    m_PosX = Desc->PosX;
    m_PosY = Desc->PosY;
    m_Width = (CKDWORD)Desc->Width;
    m_Height = (CKDWORD)Desc->Height;
    m_DebugFlags = Desc->DebugFlags;
    m_ShuttingDown = FALSE;
    m_Initialized = TRUE;
    m_Stats = CKRecordingStats();

    m_Caps = CKRasterizerDeviceCaps();
    m_Caps.Features = CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER | CKRST_DEVCAPS_PASSES |
                      CKRST_DEVCAPS_FRAMEBUFFER | CKRST_DEVCAPS_TRANSIENT_BUFFERS | CKRST_DEVCAPS_SCISSOR |
                      CKRST_DEVCAPS_BUFFER_UPDATE | CKRST_DEVCAPS_TEXTURE_UPDATE | CKRST_DEVCAPS_DEPTH_TEXTURE |
                      CKRST_DEVCAPS_TEXTURE_CUBE | CKRST_DEVCAPS_TEXTURE_3D | CKRST_DEVCAPS_TEXTURE_READBACK |
                      CKRST_DEVCAPS_BLIT | CKRST_DEVCAPS_BLEND_EQUATION | CKRST_DEVCAPS_INDEX32;
    m_Caps.MaxTextureSize = 4096;
    m_Caps.MaxTextureBindings = CKFF_TEXTURE_SLOT_COUNT;
    m_Caps.MaxPasses = CKRST_MAX_PASSES;
    m_Caps.MaxMSAASamples = 16;
    m_Caps.ShaderFormat = shaderFormat;
    m_Caps.ShaderProfile = shaderProfile;
    m_Caps.OriginBottomLeft = m_Conventions.OriginBottomLeft;
    m_Caps.HomogeneousDepth = m_Conventions.HomogeneousDepth;
    return CK_OK;
}

void CKRecordingBackend::Shutdown()
{
    for (auto &ticket : m_Readbacks) ticket->Error = CKERR_INVALIDOPERATION;
    m_Readbacks.clear();
    if (!m_Initialized)
        return;
    m_ShuttingDown = TRUE;
    m_Initialized = FALSE;
    m_PassOpen = FALSE;
    m_Passes.clear();
    m_Draws.clear();
    m_Objects.clear();
    m_TransientVertices.clear();
    m_TransientIndices.clear();
    for (auto &constants : m_Constants)
        constants.clear();
    memset(m_Textures, 0, sizeof(m_Textures));
    memset(m_Samplers, 0, sizeof(m_Samplers));
}

CKERROR CKRecordingBackend::Resize(int PosX, int PosY, int Width, int Height)
{
    if (!m_Initialized || m_PassOpen)
        return CKERR_INVALIDOPERATION;
    if (Width <= 0 || Height <= 0)
        return CKERR_INVALIDPARAMETER;
    m_PosX = PosX;
    m_PosY = PosY;
    m_Width = (CKDWORD)Width;
    m_Height = (CKDWORD)Height;
    return CK_OK;
}

CKERROR CKRecordingBackend::GetDeviceStatus() const
{
    if (!m_Initialized)
        return m_ShuttingDown ? CKERR_INVALIDOPERATION : CKERR_INVALIDRENDERCONTEXT;
    return CK_OK;
}

CKBOOL CKRecordingBackend::IsIdle() const
{
    return !m_PassOpen;
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

CKDWORD CKRecordingBackend::AllocateHandle(const CKRecordingObject &Object)
{
    const CKDWORD handle = m_NextHandle++;
    m_Objects[handle] = Object;
    return handle;
}

CKRecordingObject *CKRecordingBackend::FindObject(CKDWORD Handle)
{
    std::unordered_map<CKDWORD, CKRecordingObject>::iterator it = m_Objects.find(Handle);
    return it == m_Objects.end() ? NULL : &it->second;
}

const CKRecordingObject *CKRecordingBackend::FindObject(CKDWORD Handle) const
{
    std::unordered_map<CKDWORD, CKRecordingObject>::const_iterator it = m_Objects.find(Handle);
    return it == m_Objects.end() ? NULL : &it->second;
}

int CKRecordingBackend::GetObjectCount(CKDWORD TypeMask) const
{
    int count = 0;
    for (std::unordered_map<CKDWORD, CKRecordingObject>::const_iterator it = m_Objects.begin(); it != m_Objects.end(); ++it)
        if (it->second.Type & TypeMask)
            ++count;
    return count;
}

CKERROR CKRecordingBackend::CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Format.Width <= 0 || Desc->Format.Height <= 0 || (Desc->Flags & CKRST_TEXTURE_DEPTHSTENCIL))
        return CKERR_INVALIDPARAMETER;
    CKRecordingObject object;
    object.Type = CKRST_OBJ_TEXTURE;
    object.Width = (CKDWORD)Desc->Format.Width;
    object.Height = (CKDWORD)Desc->Format.Height;
    object.Depth = Desc->Depth > 0 ? Desc->Depth : 1;
    object.Flags = Desc->Flags;
    object.Format = VxImageDesc2PixelFormat(Desc->Format);
    *Out = AllocateHandle(object);
    if (Data && Data->Image)
        ++m_FrameTextureUploads;
    return CK_OK;
}

CKERROR CKRecordingBackend::UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                                     const VxImageDescEx *Data)
{
    (void)Mip;
    (void)Face;
    (void)Region;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    const CKRecordingObject *object = FindObject(Texture);
    if (!object || object->Type != CKRST_OBJ_TEXTURE || !Data || !Data->Image)
        return CKERR_INVALIDPARAMETER;
    ++m_FrameTextureUploads;
    return CK_OK;
}

CKERROR CKRecordingBackend::CreateDepthTexture(const CKDepthTextureDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Width == 0 || Desc->Height == 0)
        return CKERR_INVALIDPARAMETER;
    CKRecordingObject object;
    object.Type = CKRST_OBJ_TEXTURE;
    object.Width = Desc->Width;
    object.Height = Desc->Height;
    object.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL | CKRSTTextureMSAAFlag(Desc->Samples);
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKRecordingBackend::CreateRenderTarget(const CKRenderTargetDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    const CKRecordingObject *color = FindObject(Desc->ColorTexture);
    if (!color || color->Type != CKRST_OBJ_TEXTURE || (color->Flags & CKRST_TEXTURE_RENDERTARGET) == 0)
        return CKERR_INVALIDPARAMETER;
    if (Desc->DepthTexture) {
        const CKRecordingObject *depth = FindObject(Desc->DepthTexture);
        if (!depth || depth->Type != CKRST_OBJ_TEXTURE || (depth->Flags & CKRST_TEXTURE_DEPTHSTENCIL) == 0)
            return CKERR_INVALIDPARAMETER;
    }
    CKRecordingObject object;
    object.Type = CKRST_OBJ_RENDERTARGET;
    object.Target = *Desc;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKRecordingBackend::CreateBuffer(const CKBufferDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Size == 0)
        return CKERR_INVALIDPARAMETER;
    CKRecordingObject object;
    if (Desc->Kind == CKRST_BUFFER_VERTEX) {
        if (Desc->Stride == 0 || (Desc->Size % Desc->Stride) != 0)
            return CKERR_INVALIDPARAMETER;
        object.Type = CKRST_OBJ_VERTEXBUFFER;
        object.Stride = Desc->Stride;
        object.Layout = Desc->Layout;
    } else if (Desc->Kind == CKRST_BUFFER_INDEX) {
        const CKDWORD indexSize = Desc->Index32 ? 4 : 2;
        if ((Desc->Size % indexSize) != 0)
            return CKERR_INVALIDPARAMETER;
        object.Type = CKRST_OBJ_INDEXBUFFER;
        object.Stride = indexSize;
        object.Index32 = Desc->Index32;
    } else {
        return CKERR_INVALIDPARAMETER;
    }
    object.Size = Desc->Size;
    *Out = AllocateHandle(object);
    if (Desc->InitialData)
        ++m_FrameBufferUploads;
    return CK_OK;
}

CKERROR CKRecordingBackend::UpdateBuffer(const CKBufferUpdateDesc *Desc)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc ||
        (Desc->Kind != CKRST_BUFFER_VERTEX &&
         Desc->Kind != CKRST_BUFFER_INDEX) ||
        (unsigned)Desc->Mode > CKRST_BUFFER_UPDATE_NOOVERWRITE ||
        (Desc->Mode == CKRST_BUFFER_UPDATE_NOOVERWRITE &&
         Desc->Rename))
        return CKERR_INVALIDPARAMETER;
    CKRecordingObject *object = FindObject(Desc->Buffer);
    const CKDWORD expectedType = Desc->Kind == CKRST_BUFFER_VERTEX
        ? CKRST_OBJ_VERTEXBUFFER : CKRST_OBJ_INDEXBUFFER;
    if (!object || object->Type != expectedType || !Desc->Data || Desc->Size == 0 ||
        Desc->Offset > object->Size || Desc->Size > object->Size - Desc->Offset)
        return CKERR_INVALIDPARAMETER;
    ++m_FrameBufferUploads;
    return CK_OK;
}

CKERROR CKRecordingBackend::CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Stride == 0 || Desc->ElementCount == 0 || !Desc->Elements)
        return CKERR_INVALIDPARAMETER;
    CKRecordingObject object;
    object.Type = CKRST_OBJ_VERTEXLAYOUT;
    object.Stride = Desc->Stride;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKRecordingBackend::CreateShader(const CKShaderDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || !Desc->Code || Desc->CodeSize == 0 ||
        Desc->Format != m_Caps.ShaderFormat || Desc->Profile != m_Caps.ShaderProfile ||
        (Desc->Stage != CKRST_SHADER_VERTEX && Desc->Stage != CKRST_SHADER_PIXEL))
        return CKERR_INVALIDPARAMETER;
    CKRecordingObject object;
    object.Type = CKRST_OBJ_SHADER;
    object.Stage = Desc->Stage;
    object.Shader = *Desc;
    object.Size = Desc->CodeSize;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKRecordingBackend::CreateProgram(const CKFFProgramDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    const CKRecordingObject *vs = FindObject(Desc->VertexShader);
    const CKRecordingObject *ps = FindObject(Desc->PixelShader);
    if (!vs || !ps || vs->Type != CKRST_OBJ_SHADER || ps->Type != CKRST_OBJ_SHADER ||
        CKFFValidateProgram(*Desc, vs->Shader, ps->Shader) != CK_OK)
        return CKERR_INVALIDPARAMETER;
    CKRecordingObject object;
    object.Type = CKRST_OBJ_PROGRAM;
    object.VertexShader = Desc->VertexShader;
    object.PixelShader = Desc->PixelShader;
    object.Program = *Desc;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKBOOL CKRecordingBackend::IsObjectAlive(CKDWORD Object, CKDWORD Type) const
{
    const CKRecordingObject *object = FindObject(Object);
    return object && (object->Type & Type) != 0 ? TRUE : FALSE;
}

CKERROR CKRecordingBackend::DestroyObject(CKDWORD Object, CKDWORD Type)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    std::unordered_map<CKDWORD, CKRecordingObject>::iterator it = m_Objects.find(Object);
    if (it == m_Objects.end() || (it->second.Type & Type) == 0)
        return CKERR_INVALIDPARAMETER;
    m_Objects.erase(it);
    return CK_OK;
}

CKERROR CKRecordingBackend::SetObjectName(CKDWORD Object, CKDWORD Type,
                                          const char *Name)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Name)
        return CKERR_INVALIDPARAMETER;
    std::unordered_map<CKDWORD, CKRecordingObject>::iterator it =
        m_Objects.find(Object);
    if (it == m_Objects.end() || (it->second.Type & Type) == 0)
        return CKERR_INVALIDPARAMETER;
    it->second.Name = Name;
    return CK_OK;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

CKERROR CKRecordingBackend::BeginPass(const CKRenderPassDesc *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Desc->Rect.right <= Desc->Rect.left || Desc->Rect.bottom <= Desc->Rect.top || Desc->Rect.left < 0 ||
        Desc->Rect.top < 0)
        return CKERR_INVALIDPARAMETER;
    if (Desc->RenderTarget != 0) {
        const CKRecordingObject *target = FindObject(Desc->RenderTarget);
        if (!target || target->Type != CKRST_OBJ_RENDERTARGET)
            return CKERR_INVALIDPARAMETER;
    }
    CKRecordingPass pass;
    pass.RenderTarget = Desc->RenderTarget;
    pass.Rect = Desc->Rect;
    pass.ClearFlags = Desc->ClearFlags;
    pass.ClearColor = Desc->ClearColor;
    pass.ClearZ = Desc->ClearZ;
    pass.ClearStencil = Desc->ClearStencil;
    pass.Name = Desc->Name ? Desc->Name : "";
    if (m_Passes.size() >= m_Caps.MaxPasses)
        pass.ClearFlags = 0;   // out of passes: the draws join the last one, its clear is dropped
    m_Passes.push_back(pass);
    m_PassOpen = TRUE;
    return CK_OK;
}

CKBOOL CKRecordingBackend::AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKTransientVertexData *Out)
{
    if (!Out || Count == 0 || !m_Initialized)
        return FALSE;
    *Out = CKTransientVertexData();
    const CKRecordingObject *layout = FindObject(Layout);
    if (!layout || layout->Type != CKRST_OBJ_VERTEXLAYOUT || layout->Stride == 0)
        return FALSE;
    m_TransientVertices.push_back(std::vector<CKBYTE>((size_t)Count * layout->Stride, 0));
    Out->Data = m_TransientVertices.back().data();
    Out->Count = Count;
    Out->Stride = layout->Stride;
    Out->Layout = Layout;
    Out->Token = (CKDWORD)m_TransientVertices.size();
    return TRUE;
}

CKBOOL CKRecordingBackend::AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKTransientIndexData *Out)
{
    if (!Out || Count == 0 || !m_Initialized)
        return FALSE;
    *Out = CKTransientIndexData();
    m_TransientIndices.push_back(std::vector<CKBYTE>((size_t)Count * (Index32 ? 4 : 2), 0));
    Out->Data = m_TransientIndices.back().data();
    Out->Count = Count;
    Out->Index32 = Index32;
    Out->Token = (CKDWORD)m_TransientIndices.size();
    return TRUE;
}

CKERROR CKRecordingBackend::Draw(const CKDrawCommand *Draw)
{
    if (!Draw || !Draw->Program)
        return CKERR_INVALIDPARAMETER;
    if (!m_Initialized || !m_PassOpen)
        return CKERR_INVALIDOPERATION;
    const CKRecordingObject *program = FindObject(Draw->Program);
    if (!program || program->Type != CKRST_OBJ_PROGRAM)
        return CKERR_INVALIDPARAMETER;
    // Buffer handles are not checked: the pipeline tests draw with bare
    // handles. Transient geometry has to come from this frame.
    if (Draw->TransientVertices) {
        if (Draw->TransientVertices->Token == 0 || Draw->TransientVertices->Token > m_TransientVertices.size())
            return CKERR_INVALIDPARAMETER;
    } else if (!Draw->VertexBuffer && program->Program.VertexInputs.Size() != 0) {
        return CKERR_INVALIDPARAMETER;
    }
    if (Draw->TransientIndices &&
        (Draw->TransientIndices->Token == 0 || Draw->TransientIndices->Token > m_TransientIndices.size()))
        return CKERR_INVALIDPARAMETER;
    m_State = Draw->Pipeline;
    m_Marker = Draw->Marker ? Draw->Marker : "";
    for (CKDWORD slot = 0; slot < CKFF_TEXTURE_SLOT_COUNT; ++slot) {
        const CKFFTextureSlot binding = Draw->Textures ? (*Draw->Textures)[slot] : CKFFTextureSlot();
        m_Textures[slot] = binding.Texture;
        m_Samplers[slot] = binding.Sampler;
    }
    for (CKDWORD slot = 0; slot < CKFF_CONSTANT_SLOT_COUNT; ++slot) {
        if (Draw->Constants) {
            const XArray<CKBYTE> &bytes = (*Draw->Constants)[slot].Bytes;
            m_Constants[slot].assign(bytes.Begin(), bytes.End());
        } else {
            m_Constants[slot].clear();
        }
    }
    CKRecordingDraw record;
    record.Pass = GetCurrentPass();
    record.Program = Draw->Program;
    record.VertexCount = Draw->VertexCount;
    record.IndexCount = Draw->TransientIndices ? Draw->TransientIndices->Count : Draw->IndexCount;
    record.SortKey = Draw->SortKey;
    record.State = m_State;
    memcpy(record.Textures, m_Textures, sizeof(record.Textures));
    record.Marker = m_Marker;
    m_Draws.push_back(record);
    m_Marker = "";
    return CK_OK;
}

CKERROR CKRecordingBackend::Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                            CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect)
{
    (void)DstMip;
    (void)SrcMip;
    (void)SrcRect;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    const CKRecordingObject *dst = FindObject(DstTexture);
    const CKRecordingObject *src = FindObject(SrcTexture);
    if (!dst || !src || dst->Type != CKRST_OBJ_TEXTURE || src->Type != CKRST_OBJ_TEXTURE)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD dstLayers = (dst->Flags & CKRST_TEXTURE_CUBEMAP) ? 6 : dst->Depth;
    const CKDWORD srcLayers = (src->Flags & CKRST_TEXTURE_CUBEMAP) ? 6 : src->Depth;
    if (DstLayer >= dstLayers || SrcLayer >= srcLayers || DstX >= dst->Width || DstY >= dst->Height)
        return CKERR_INVALIDPARAMETER;
    // Ordinary public textures can receive CopyToTexture too. BLIT_DST
    // identifies internal copy resources; it is not a public usage requirement.
    ++m_FrameBlits;
    return CK_OK;
}

CKERROR CKRecordingBackend::Submit(CKPresentSync Mode, CKBOOL PresentWindow,
                                   CKDWORD *FrameNumber)
{
    (void)PresentWindow;
    if (FrameNumber)
        *FrameNumber = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Mode != CKRST_PRESENT_IMMEDIATE && Mode != CKRST_PRESENT_VSYNC &&
        Mode != CKRST_PRESENT_UNCHANGED)
        return CKERR_INVALIDPARAMETER;
    if (m_LastSubmitId == (CKQWORD)-1)
        return CKERR_INVALIDOPERATION;
    ++m_FrameNumber;
    m_CompletedSubmitId = ++m_LastSubmitId;
    for (auto &ticket : m_Readbacks) ticket->Complete = TRUE;
    m_Readbacks.clear();
    if (FrameNumber)
        *FrameNumber = m_FrameNumber;
    ++m_Stats.Frames;
    m_Stats.Passes = (CKDWORD)m_Passes.size();
    m_Stats.Draws = (CKDWORD)m_Draws.size();
    m_Stats.Blits = m_FrameBlits;
    m_Stats.TextureUploads = m_FrameTextureUploads;
    m_Stats.BufferUploads = m_FrameBufferUploads;
    m_FrameBlits = m_FrameTextureUploads = m_FrameBufferUploads = 0;
    m_Passes.clear();
    m_Draws.clear();
    m_TransientVertices.clear();
    m_TransientIndices.clear();
    m_PassOpen = FALSE;
    m_Marker = "";
    return CK_OK;
}

// Readbacks deliver a zero-filled ARGB image of the texture size, available
// after the next Present().
CKERROR CKRecordingBackend::ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKRecordingReadbackTicket *Ticket)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Readback)
        return CKERR_INVALIDPARAMETER;
    const CKRecordingObject *object = FindObject(Texture);
    if (!object || object->Type != CKRST_OBJ_TEXTURE)
        return CKERR_INVALIDPARAMETER;
    if ((object->Flags & CKRST_TEXTURE_READBACK) == 0)
        return CKERR_INVALIDOPERATION;
    const CKDWORD width = XMax((CKDWORD)1, object->Width >> Mip);
    const CKDWORD height = XMax((CKDWORD)1, object->Height >> Mip);
    Readback->Width = width;
    Readback->Height = height;
    Readback->RowPitch = width * 4;
    Readback->Format = _32_ARGB8888;
    Readback->YFlip = FALSE;
    Readback->RequiredSize = Readback->RowPitch * height;
    if (!Ticket)
        return CK_OK;
    auto pending = std::make_shared<CKRecordingReadback>();
    pending->Data.Resize((int)Readback->RequiredSize);
    memset(pending->Data.Begin(), 0, Readback->RequiredSize);
    pending->AvailableFrame = m_FrameNumber + 1;
    m_Readbacks.push_back(pending);
    *Ticket = pending;
    return CK_OK;
}
