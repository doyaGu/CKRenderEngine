#include "CKNullBackend.h"
#include "CKRasterizerCapsBaseline.h"

#include <new>
#include <string.h>

// ===========================================================================
// CKNullBackendDriver
// ===========================================================================

CKNullBackendDriver::CKNullBackendDriver()
    : Profile(CKRST_SHADER_PROFILE_DX11), OriginBottomLeft(FALSE), HomogeneousDepth(FALSE)
{
    m_Desc = "NULL Rasterizer";
    m_Hardware = FALSE;
    m_CapsUpToDate = TRUE;
    m_DriverIndex = 0;

    static const int resolutions[][2] = {
        {640, 480},
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1280, 960},
        {1280, 1024},
        {1366, 768},
        {1600, 900},
        {1920, 1080},
    };
    static const int bpps[] = {16, 32};
    for (int i = 0; i < (int)(sizeof(resolutions) / sizeof(resolutions[0])); ++i) {
        for (int j = 0; j < (int)(sizeof(bpps) / sizeof(bpps[0])); ++j) {
            VxDisplayMode displayMode;
            displayMode.Width = resolutions[i][0];
            displayMode.Height = resolutions[i][1];
            displayMode.Bpp = bpps[j];
            displayMode.RefreshRate = 60;
            m_DisplayModes.PushBack(displayMode);
        }
    }

    CKTextureDesc textureDesc;
    textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
    m_TextureFormats.PushBack(textureDesc);

    // Capability baseline (spec 4.9.2). The NULL backend neither transforms
    // nor rasterizes in hardware, so only the hardware / software bits differ.
    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    if (!CKRSTGetCapsBaseline(&m_3DCaps, &m_2DCaps)) {
        m_3DCaps.MinTextureWidth = m_3DCaps.MinTextureHeight = 1;
        m_3DCaps.MaxTextureWidth = m_3DCaps.MaxTextureHeight = 4096;
        m_3DCaps.MaxClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
        m_3DCaps.MaxActiveLights = CKRST_MAX_LIGHTS;
        m_3DCaps.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        m_3DCaps.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        m_2DCaps.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D | CKRST_2DCAPS_GDI;
    }
    m_3DCaps.CKRasterizerSpecificCaps &= ~(CKRST_SPECIFICCAPS_HARDWARE | CKRST_SPECIFICCAPS_HARDWARETL);
    m_3DCaps.CKRasterizerSpecificCaps |= CKRST_SPECIFICCAPS_SOFTWARE;
}

CKNullBackendDriver::~CKNullBackendDriver()
{
    for (int i = 0; i < m_Backends.Size(); ++i) {
        m_Backends[i]->Shutdown();
        delete m_Backends[i];
    }
    m_Backends.Clear();
}

CKNullBackend *CKNullBackendDriver::NewBackend()
{
    return new (std::nothrow) CKNullBackend(this);
}

CKRasterizerBackend *CKNullBackendDriver::CreateBackend()
{
    CKNullBackend *backend = NewBackend();
    if (!backend)
        return NULL;
    m_Backends.PushBack(backend);
    return backend;
}

CKBOOL CKNullBackendDriver::DestroyBackend(CKRasterizerBackend *Backend)
{
    if (!Backend)
        return FALSE;
    for (int i = 0; i < m_Backends.Size(); ++i) {
        if (m_Backends[i] != Backend)
            continue;
        if (!m_Backends[i]->IsIdle())
            return FALSE;
        m_Backends[i]->Shutdown();
        delete m_Backends[i];
        m_Backends.RemoveAt(i);
        return TRUE;
    }
    return FALSE;
}

// ===========================================================================
// CKNullBackendLibrary
// ===========================================================================

CKNullBackendLibrary::CKNullBackendLibrary() : m_MainWindow(NULL) {}

CKNullBackendLibrary::~CKNullBackendLibrary()
{
    Close();
}

CKNullBackendDriver *CKNullBackendLibrary::NewDriver()
{
    return new (std::nothrow) CKNullBackendDriver();
}

CKBOOL CKNullBackendLibrary::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    if (m_Drivers.Size() > 0)
        return TRUE;
    CKNullBackendDriver *driver = NewDriver();
    if (!driver)
        return FALSE;
    driver->m_DriverIndex = 0;
    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKNullBackendLibrary::Close()
{
    for (int i = 0; i < m_Drivers.Size(); ++i)
        delete m_Drivers[i];
    m_Drivers.Clear();
}

CKRasterizerBackendDriver *CKNullBackendLibrary::GetDriver(CKDWORD Index) const
{
    return (int)Index < m_Drivers.Size() ? m_Drivers[(int)Index] : NULL;
}

// ===========================================================================
// CKNullBackend
// ===========================================================================

CKNullBackend::CKNullBackend(CKNullBackendDriver *Driver)
    : m_Driver(Driver), m_Initialized(FALSE), m_ShuttingDown(FALSE), m_DebugFlags(0), m_PosX(0), m_PosY(0),
      m_Width(0), m_Height(0), m_FrameNumber(0), m_NextHandle(1), m_PassOpen(FALSE), m_FrameBlits(0),
      m_FrameTextureUploads(0), m_FrameBufferUploads(0)
{
    memset(m_Textures, 0, sizeof(m_Textures));
    memset(m_Samplers, 0, sizeof(m_Samplers));
    memset(m_Palette, 0, sizeof(m_Palette));
}

CKNullBackend::~CKNullBackend()
{
    Shutdown();
}

CKERROR CKNullBackend::Init(const CKBackendInitDesc *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Desc->Width <= 0 || Desc->Height <= 0)
        return CKERR_INVALIDPARAMETER;
    m_PosX = Desc->PosX;
    m_PosY = Desc->PosY;
    m_Width = (CKDWORD)Desc->Width;
    m_Height = (CKDWORD)Desc->Height;
    m_DebugFlags = Desc->DebugFlags;
    m_ShuttingDown = FALSE;
    m_Initialized = TRUE;
    m_Stats = CKBackendStats();

    m_Caps = CKBackendCaps();
    m_Caps.Features = CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER | CKRST_DEVCAPS_PASSES |
                      CKRST_DEVCAPS_FRAMEBUFFER | CKRST_DEVCAPS_TRANSIENT_BUFFERS | CKRST_DEVCAPS_SCISSOR |
                      CKRST_DEVCAPS_BUFFER_UPDATE | CKRST_DEVCAPS_TEXTURE_UPDATE | CKRST_DEVCAPS_DEPTH_TEXTURE |
                      CKRST_DEVCAPS_TEXTURE_CUBE | CKRST_DEVCAPS_TEXTURE_3D | CKRST_DEVCAPS_TEXTURE_READBACK |
                      CKRST_DEVCAPS_BLIT | CKRST_DEVCAPS_BLEND_EQUATION | CKRST_DEVCAPS_INDEX32;
    m_Caps.MaxTextureSize = 4096;
    m_Caps.MaxTextureBindings = CKRST_BACKEND_SLOT_COUNT;
    m_Caps.MaxPasses = CKRST_MAX_PASSES;
    m_Caps.MaxMSAASamples = 16;
    m_Caps.ShaderProfile = m_Driver ? m_Driver->Profile : CKRST_SHADER_PROFILE_DX11;
    m_Caps.OriginBottomLeft = m_Driver ? m_Driver->OriginBottomLeft : FALSE;
    m_Caps.HomogeneousDepth = m_Driver ? m_Driver->HomogeneousDepth : FALSE;
    return CK_OK;
}

void CKNullBackend::Shutdown()
{
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
}

CKERROR CKNullBackend::Resize(int PosX, int PosY, int Width, int Height)
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

CKERROR CKNullBackend::GetDeviceStatus() const
{
    if (!m_Initialized)
        return m_ShuttingDown ? CKERR_INVALIDOPERATION : CKERR_INVALIDRENDERCONTEXT;
    return CK_OK;
}

CKBOOL CKNullBackend::IsIdle() const
{
    return !m_PassOpen;
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

CKDWORD CKNullBackend::AllocateHandle(const CKNullObject &Object)
{
    const CKDWORD handle = m_NextHandle++;
    m_Objects[handle] = Object;
    return handle;
}

CKNullObject *CKNullBackend::FindObject(CKDWORD Handle)
{
    std::unordered_map<CKDWORD, CKNullObject>::iterator it = m_Objects.find(Handle);
    return it == m_Objects.end() ? NULL : &it->second;
}

const CKNullObject *CKNullBackend::FindObject(CKDWORD Handle) const
{
    std::unordered_map<CKDWORD, CKNullObject>::const_iterator it = m_Objects.find(Handle);
    return it == m_Objects.end() ? NULL : &it->second;
}

int CKNullBackend::GetObjectCount(CKDWORD TypeMask) const
{
    int count = 0;
    for (std::unordered_map<CKDWORD, CKNullObject>::const_iterator it = m_Objects.begin(); it != m_Objects.end(); ++it)
        if (it->second.Type & TypeMask)
            ++count;
    return count;
}

CKERROR CKNullBackend::CreateTexture(const CKTextureDesc *Desc, const VxImageDescEx *Data, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Format.Width <= 0 || Desc->Format.Height <= 0 || (Desc->Flags & CKRST_TEXTURE_DEPTHSTENCIL))
        return CKERR_INVALIDPARAMETER;
    CKNullObject object;
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

CKERROR CKNullBackend::UpdateTexture(CKDWORD Texture, CKDWORD Mip, CKDWORD Face, const CKRECT *Region,
                                     const VxImageDescEx *Data)
{
    (void)Mip;
    (void)Face;
    (void)Region;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    const CKNullObject *object = FindObject(Texture);
    if (!object || object->Type != CKRST_OBJ_TEXTURE || !Data || !Data->Image)
        return CKERR_INVALIDPARAMETER;
    ++m_FrameTextureUploads;
    return CK_OK;
}

CKERROR CKNullBackend::CreateDepthTexture(const CKBackendDepthDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Width == 0 || Desc->Height == 0)
        return CKERR_INVALIDPARAMETER;
    CKNullObject object;
    object.Type = CKRST_OBJ_TEXTURE;
    object.Width = Desc->Width;
    object.Height = Desc->Height;
    object.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL | CKRSTTextureMSAAFlag(Desc->Samples);
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKNullBackend::CreateRenderTarget(const CKBackendRenderTargetDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    const CKNullObject *color = FindObject(Desc->ColorTexture);
    if (!color || color->Type != CKRST_OBJ_TEXTURE || (color->Flags & CKRST_TEXTURE_RENDERTARGET) == 0)
        return CKERR_INVALIDPARAMETER;
    if (Desc->DepthTexture) {
        const CKNullObject *depth = FindObject(Desc->DepthTexture);
        if (!depth || depth->Type != CKRST_OBJ_TEXTURE || (depth->Flags & CKRST_TEXTURE_DEPTHSTENCIL) == 0)
            return CKERR_INVALIDPARAMETER;
    }
    CKNullObject object;
    object.Type = CKRST_OBJ_RENDERTARGET;
    object.Target = *Desc;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKNullBackend::CreateBuffer(const CKBackendBufferDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Size == 0)
        return CKERR_INVALIDPARAMETER;
    CKNullObject object;
    if (Desc->Kind == CKRST_BACKEND_BUFFER_VERTEX) {
        if (Desc->Stride == 0 || (Desc->Size % Desc->Stride) != 0)
            return CKERR_INVALIDPARAMETER;
        object.Type = CKRST_OBJ_VERTEXBUFFER;
        object.Stride = Desc->Stride;
        object.Layout = Desc->Layout;
    } else if (Desc->Kind == CKRST_BACKEND_BUFFER_INDEX) {
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

CKERROR CKNullBackend::UpdateBuffer(CKBackendBufferKind Kind, CKDWORD Buffer, CKDWORD Offset,
                                    CKDWORD Size, const void *Data)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Kind != CKRST_BACKEND_BUFFER_VERTEX && Kind != CKRST_BACKEND_BUFFER_INDEX)
        return CKERR_INVALIDPARAMETER;
    const CKNullObject *object = FindObject(Buffer);
    const CKDWORD expectedType = Kind == CKRST_BACKEND_BUFFER_VERTEX
        ? CKRST_OBJ_VERTEXBUFFER : CKRST_OBJ_INDEXBUFFER;
    if (!object || object->Type != expectedType || !Data ||
        Size == 0 || Offset > object->Size || Size > object->Size - Offset)
        return CKERR_INVALIDPARAMETER;
    ++m_FrameBufferUploads;
    return CK_OK;
}

CKERROR CKNullBackend::CreateVertexLayout(const CKVertexLayoutDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Stride == 0 || Desc->ElementCount == 0 || !Desc->Elements)
        return CKERR_INVALIDPARAMETER;
    CKNullObject object;
    object.Type = CKRST_OBJ_VERTEXLAYOUT;
    object.Stride = Desc->Stride;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKNullBackend::CreateShader(const CKShaderDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Desc || !Desc->Code || Desc->CodeSize == 0 ||
        (Desc->Stage != CKRST_SHADER_VERTEX && Desc->Stage != CKRST_SHADER_PIXEL))
        return CKERR_INVALIDPARAMETER;
    CKNullObject object;
    object.Type = CKRST_OBJ_SHADER;
    object.Stage = Desc->Stage;
    object.Size = Desc->CodeSize;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKERROR CKNullBackend::CreateProgram(CKDWORD VertexShader, CKDWORD PixelShader, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    const CKNullObject *vs = FindObject(VertexShader);
    const CKNullObject *ps = FindObject(PixelShader);
    if (!vs || !ps || vs->Type != CKRST_OBJ_SHADER || ps->Type != CKRST_OBJ_SHADER ||
        vs->Stage != CKRST_SHADER_VERTEX || ps->Stage != CKRST_SHADER_PIXEL)
        return CKERR_INVALIDPARAMETER;
    CKNullObject object;
    object.Type = CKRST_OBJ_PROGRAM;
    object.VertexShader = VertexShader;
    object.PixelShader = PixelShader;
    *Out = AllocateHandle(object);
    return CK_OK;
}

CKBOOL CKNullBackend::IsObjectAlive(CKDWORD Object, CKDWORD Type) const
{
    const CKNullObject *object = FindObject(Object);
    return object && (object->Type & Type) != 0 ? TRUE : FALSE;
}

CKERROR CKNullBackend::DestroyObject(CKDWORD Object, CKDWORD Type)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    std::unordered_map<CKDWORD, CKNullObject>::iterator it = m_Objects.find(Object);
    if (it == m_Objects.end() || (it->second.Type & Type) == 0)
        return CKERR_INVALIDPARAMETER;
    m_Objects.erase(it);
    return CK_OK;
}

void CKNullBackend::SetObjectName(CKDWORD Object, CKDWORD Type, const char *Name)
{
    (void)Object;
    (void)Type;
    (void)Name;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

CKERROR CKNullBackend::BeginPass(const CKBackendPassDesc *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Desc->Rect.right <= Desc->Rect.left || Desc->Rect.bottom <= Desc->Rect.top || Desc->Rect.left < 0 ||
        Desc->Rect.top < 0)
        return CKERR_INVALIDPARAMETER;
    if (Desc->RenderTarget != 0) {
        const CKNullObject *target = FindObject(Desc->RenderTarget);
        if (!target || target->Type != CKRST_OBJ_RENDERTARGET)
            return CKERR_INVALIDPARAMETER;
    }
    CKNullPass pass;
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

void CKNullBackend::SetPipelineState(const CKBackendPipelineState *State)
{
    if (State)
        m_State = *State;
}

void CKNullBackend::BindTexture(CKDWORD Slot, CKDWORD Texture, const CKSamplerDesc *Sampler)
{
    if (Slot >= CKRST_BACKEND_SLOT_COUNT)
        return;
    m_Textures[Slot] = Texture;
    if (Sampler)
        m_Samplers[Slot] = *Sampler;
    else
        memset(&m_Samplers[Slot], 0, sizeof(m_Samplers[Slot]));
}

CKERROR CKNullBackend::PushConstants(CKBackendConstantBlock Block, const void *Data, CKDWORD Vec4Count)
{
    if ((int)Block < 0 || (int)Block >= CKRST_BLOCK_COUNT || !Data || Vec4Count == 0)
        return CKERR_INVALIDPARAMETER;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    const CKBackendConstantBlockDesc &info = CKBackendConstantBlockInfo(Block);
    const CKDWORD count = info.Mat4 ? Vec4Count / 4 : Vec4Count;
    if (count == 0 || count > info.Count)
        return CKERR_INVALIDPARAMETER;
    const float *values = static_cast<const float *>(Data);
    m_Constants[Block].assign(values, values + Vec4Count * 4);
    return CK_OK;
}

void CKNullBackend::SetMarker(const char *Name)
{
    m_Marker = Name ? Name : "";
}

CKBOOL CKNullBackend::AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKBackendTransientVertices *Out)
{
    if (!Out || Count == 0 || !m_Initialized)
        return FALSE;
    *Out = CKBackendTransientVertices();
    const CKNullObject *layout = FindObject(Layout);
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

CKBOOL CKNullBackend::AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKBackendTransientIndices *Out)
{
    if (!Out || Count == 0 || !m_Initialized)
        return FALSE;
    *Out = CKBackendTransientIndices();
    m_TransientIndices.push_back(std::vector<CKBYTE>((size_t)Count * (Index32 ? 4 : 2), 0));
    Out->Data = m_TransientIndices.back().data();
    Out->Count = Count;
    Out->Index32 = Index32;
    Out->Token = (CKDWORD)m_TransientIndices.size();
    return TRUE;
}

CKERROR CKNullBackend::Draw(const CKBackendDraw *Draw)
{
    if (!Draw || !Draw->Program)
        return CKERR_INVALIDPARAMETER;
    if (!m_Initialized || !m_PassOpen)
        return CKERR_INVALIDOPERATION;
    const CKNullObject *program = FindObject(Draw->Program);
    if (!program || program->Type != CKRST_OBJ_PROGRAM)
        return CKERR_INVALIDPARAMETER;
    // Buffer handles are not checked: the pipeline tests draw with bare
    // handles. Transient geometry has to come from this frame.
    if (Draw->TransientVertices) {
        if (Draw->TransientVertices->Token == 0 || Draw->TransientVertices->Token > m_TransientVertices.size())
            return CKERR_INVALIDPARAMETER;
    } else if (!Draw->VertexBuffer) {
        return CKERR_INVALIDPARAMETER;
    }
    if (Draw->TransientIndices &&
        (Draw->TransientIndices->Token == 0 || Draw->TransientIndices->Token > m_TransientIndices.size()))
        return CKERR_INVALIDPARAMETER;
    CKNullDraw record;
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

CKERROR CKNullBackend::Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                            CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect)
{
    (void)DstMip;
    (void)SrcMip;
    (void)SrcRect;
    if (!m_Initialized || !m_PassOpen)
        return CKERR_INVALIDOPERATION;
    const CKNullObject *dst = FindObject(DstTexture);
    const CKNullObject *src = FindObject(SrcTexture);
    if (!dst || !src || dst->Type != CKRST_OBJ_TEXTURE || src->Type != CKRST_OBJ_TEXTURE)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD dstLayers = (dst->Flags & CKRST_TEXTURE_CUBEMAP) ? 6 : dst->Depth;
    const CKDWORD srcLayers = (src->Flags & CKRST_TEXTURE_CUBEMAP) ? 6 : src->Depth;
    if (DstLayer >= dstLayers || SrcLayer >= srcLayers || DstX >= dst->Width || DstY >= dst->Height)
        return CKERR_INVALIDPARAMETER;
    if ((dst->Flags & CKRST_TEXTURE_BLIT_DST) == 0)
        return CKERR_NOTIMPLEMENTED;
    ++m_FrameBlits;
    return CK_OK;
}

CKERROR CKNullBackend::Present(CKBackendPresentMode Mode, CKDWORD *FrameNumber)
{
    if (FrameNumber)
        *FrameNumber = 0;
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Mode != CKRST_BACKEND_PRESENT_IMMEDIATE && Mode != CKRST_BACKEND_PRESENT_VSYNC &&
        Mode != CKRST_BACKEND_PRESENT_PRESERVE)
        return CKERR_INVALIDPARAMETER;
    ++m_FrameNumber;
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
CKERROR CKNullBackend::ReadTexture(CKDWORD Texture, CKDWORD Mip, CKReadbackDesc *Readback, CKDWORD *AvailableFrame)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (!Readback)
        return CKERR_INVALIDPARAMETER;
    const CKNullObject *object = FindObject(Texture);
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
    if (!Readback->Data)
        return CK_OK;
    if (Readback->Capacity < Readback->RequiredSize)
        return CKERR_INVALIDPARAMETER;
    memset(Readback->Data, 0, Readback->RequiredSize);
    if (AvailableFrame)
        *AvailableFrame = m_FrameNumber + 1;
    return CK_OK;
}

CKERROR CKNullBackend::SetPaletteColor(CKDWORD Index, CKDWORD RGBA)
{
    if (!m_Initialized)
        return CKERR_INVALIDOPERATION;
    if (Index >= 16)
        return CKERR_INVALIDPARAMETER;
    m_Palette[Index] = RGBA;
    return CK_OK;
}
