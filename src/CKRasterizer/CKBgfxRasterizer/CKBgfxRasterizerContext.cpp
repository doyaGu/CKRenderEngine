#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"

#include <bgfx/platform.h>
#include <SDL3/SDL.h>
#include <stdint.h>
#include <algorithm>
#include <cstdarg>
#include <cstring>

static uint32_t SampleBytesChecksum(const void *data, uint32_t size)
{
    if (!data || size == 0)
        return 0;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t hash = 2166136261u;
    uint32_t step = size > 4096 ? (size / 4096) : 1;
    for (uint32_t i = 0; i < size; i += step) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t FirstDword(const void *data, uint32_t size)
{
    uint32_t value = 0;
    if (data && size >= sizeof(value))
        memcpy(&value, data, sizeof(value));
    return value;
}

static void *CKBgfxSdlPointerProperty(SDL_PropertiesID props, const char *name)
{
    return SDL_GetPointerProperty(props, name, NULL);
}

static bool CKBgfxFillSDLPlatformData(WIN_HANDLE Window, bgfx::PlatformData &platformData)
{
    SDL_Window *window = static_cast<SDL_Window *>(Window);
    if (!window)
        return false;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props)
        return false;

#if defined(_WIN32)
    platformData.nwh = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER);
    return platformData.nwh != NULL;
#elif defined(__APPLE__)
    platformData.nwh = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER);
    return platformData.nwh != NULL;
#elif defined(__linux__)
    void *waylandSurface = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER);
    if (waylandSurface) {
        platformData.ndt = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER);
        platformData.nwh = waylandSurface;
        platformData.type = bgfx::NativeWindowHandleType::Wayland;
        return platformData.ndt != NULL;
    }

    const Sint64 x11Window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (x11Window != 0) {
        platformData.ndt = CKBgfxSdlPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER);
        platformData.nwh = reinterpret_cast<void *>(static_cast<uintptr_t>(x11Window));
        return platformData.ndt != NULL;
    }

    return false;
#else
    return false;
#endif
}

static void CKBgfxResolveFullscreenWindowSize(WIN_HANDLE Window, CKBOOL Fullscreen,
                                              int &width, int &height)
{
    if (!Fullscreen || !Window)
        return;

    SDL_Window *window = static_cast<SDL_Window *>(Window);
    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(window, &windowW, &windowH);
    if (windowW > 0 && windowH > 0) {
        width = windowW;
        height = windowH;
    }
}

// ===========================================================================
// Helper: ensure slot array is large enough and return pointer at index
// ===========================================================================

template <typename T>
static T *&EnsureSlot(XArray<T *> &arr, CKDWORD index)
{
    while ((int)index >= arr.Size())
        arr.PushBack(NULL);
    return arr[index];
}

template <typename T>
static T *GetSlot(XArray<T *> &arr, CKDWORD index)
{
    if (index == 0 || (int)index >= arr.Size())
        return NULL;
    return arr[index];
}

template <typename RecordT>
static void DestroyRecord(RecordT *rec)
{
    if (rec)
    {
        if (bgfx::isValid(rec->Handle))
            bgfx::destroy(rec->Handle);
        delete rec;
    }
}

template <typename RecordT>
static void DestroyAllRecords(XArray<RecordT *> &arr)
{
    for (int i = 0; i < arr.Size(); ++i)
        DestroyRecord(arr[i]);
    arr.Clear();
}

// ===========================================================================
// Helper: CK_DISCARD_FLAGS -> bgfx discard flags
// ===========================================================================

static uint8_t ToBgfxDiscardFlags(CKDWORD flags)
{
    if (flags == CKRST_DISCARD_ALL)
        return BGFX_DISCARD_ALL;
    if (flags == CKRST_DISCARD_NONE)
        return BGFX_DISCARD_NONE;

    uint8_t bgfxFlags = 0;
    if (flags & CKRST_DISCARD_VERTEXBUFFER)
        bgfxFlags |= BGFX_DISCARD_VERTEX_STREAMS;
    if (flags & CKRST_DISCARD_INDEXBUFFER)
        bgfxFlags |= BGFX_DISCARD_INDEX_BUFFER;
    if (flags & CKRST_DISCARD_TEXTURES)
        bgfxFlags |= BGFX_DISCARD_BINDINGS;
    if (flags & CKRST_DISCARD_UNIFORMS)
        bgfxFlags |= BGFX_DISCARD_BINDINGS;
    if (flags & CKRST_DISCARD_STATE)
        bgfxFlags |= BGFX_DISCARD_STATE;
    if (flags & CKRST_DISCARD_TRANSFORM)
        bgfxFlags |= BGFX_DISCARD_TRANSFORM;
    if (flags & CKRST_DISCARD_INSTANCEDATA)
        bgfxFlags |= BGFX_DISCARD_INSTANCE_DATA;
    return bgfxFlags;
}

// ===========================================================================
// CKBgfxEncoder
// ===========================================================================

CKBgfxEncoder::CKBgfxEncoder()
    : m_Active{FALSE}, m_Context(NULL), m_Encoder(NULL),
      m_StencilRef(0), m_StencilReadMask(0xFF), m_StencilWriteMask(0xFF),
      m_CurrentLayout(0), m_PointSize(0),
      m_CachedDrawState(), m_CachedBgfxState(0) {}

CKBgfxEncoder::~CKBgfxEncoder() {}

static void ApplyBgfxState(bgfx::Encoder *encoder, uint64_t bgfxState)
{
    if (encoder)
        encoder->setState(bgfxState);
    else
        bgfx::setState(bgfxState);
}

static void ApplyStencil(bgfx::Encoder *encoder, CKDrawState state,
                         CKDWORD ref, CKDWORD readMask, CKDWORD writeMask)
{
    writeMask &= 0xFF;
    uint32_t fstencil = CKBgfxBuildFrontStencil(state, ref, readMask, writeMask);
    uint32_t bstencil = CKBgfxBuildBackStencil(state, ref, readMask, writeMask);
    if (encoder)
        encoder->setStencil(fstencil, bstencil);
    else
        bgfx::setStencil(fstencil, bstencil);
}

void CKBgfxEncoder::SetState(CKDrawState State)
{
    m_CachedDrawState = State;
    m_CachedBgfxState = CKBgfxState(State);

    uint64_t finalState = m_CachedBgfxState;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);
    ApplyBgfxState(m_Encoder, finalState);
    ApplyStencil(m_Encoder, State, m_StencilRef, m_StencilReadMask, m_StencilWriteMask);
}

void CKBgfxEncoder::SetStencilRef(CKDWORD Ref)
{
    m_StencilRef = Ref & 0xFF;
    if (m_CachedDrawState.Mid & CKRST_STENCIL_ENABLE)
        ApplyStencil(m_Encoder, m_CachedDrawState, m_StencilRef, m_StencilReadMask, m_StencilWriteMask);
}

void CKBgfxEncoder::SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask)
{
    m_StencilReadMask = ReadMask & 0xFF;
    m_StencilWriteMask = WriteMask & 0xFF;
    if (m_CachedDrawState.Mid & CKRST_STENCIL_ENABLE)
        ApplyStencil(m_Encoder, m_CachedDrawState, m_StencilRef, m_StencilReadMask, m_StencilWriteMask);
}

void CKBgfxEncoder::SetScissor(const CKRECT *Rect)
{
    if (!Rect)
    {
        if (m_Encoder)
            m_Encoder->setScissor();
        else
            bgfx::setScissor();
        return;
    }
    uint16_t x = (uint16_t)Rect->left;
    uint16_t y = (uint16_t)Rect->top;
    uint16_t w = (uint16_t)(Rect->right - Rect->left);
    uint16_t h = (uint16_t)(Rect->bottom - Rect->top);
    if (m_Encoder)
        m_Encoder->setScissor(x, y, w, h);
    else
        bgfx::setScissor(x, y, w, h);
}

void CKBgfxEncoder::SetPointSize(float Size)
{
    m_PointSize = (CKDWORD)(Size + 0.5f);
    if (m_PointSize > 15) m_PointSize = 15;

    if (m_CachedBgfxState != 0)
    {
        uint64_t finalState = m_CachedBgfxState;
        if (m_PointSize > 0)
            finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);
        ApplyBgfxState(m_Encoder, finalState);
    }
}

void CKBgfxEncoder::SetTransform(CKDWORD TransformIndex, CKDWORD Count)
{
    if (!m_Context || TransformIndex == CKRST_INVALID_TRANSFORM)
        return;
    if (TransformIndex + Count > CKRST_MAX_TRANSFORMS)
        return;

    const VxMatrix *mtx = &m_Context->m_TransformCache[TransformIndex];
    if (m_Encoder)
        m_Encoder->setTransform((const void *)mtx, (uint16_t)Count);
    else
        bgfx::setTransform((const void *)mtx, (uint16_t)Count);
}

void CKBgfxEncoder::SetVertexLayout(CKDWORD Layout)
{
    m_CurrentLayout = Layout;
}

void CKBgfxEncoder::SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer,
                                     CKDWORD StartVertex, CKDWORD VertexCount)
{
    if (!m_Context)
        return;
    CKBgfxVertexBufferRecord *rec = m_Context->GetVertexBuffer(Buffer);
    if (!rec)
        return;

    bgfx::VertexLayoutHandle layoutHandle = BGFX_INVALID_HANDLE;
    if (m_CurrentLayout != 0)
    {
        CKBgfxVertexLayoutRecord *layoutRec = m_Context->GetVertexLayout(m_CurrentLayout);
        if (layoutRec)
            layoutHandle = layoutRec->Handle;
    }

    if (m_Encoder)
        m_Encoder->setVertexBuffer((uint8_t)Stream, rec->Handle, StartVertex, VertexCount, layoutHandle);
    else
        bgfx::setVertexBuffer((uint8_t)Stream, rec->Handle, StartVertex, VertexCount, layoutHandle);
}

void CKBgfxEncoder::SetIndexBuffer(CKDWORD Buffer,
                                    CKDWORD StartIndex, CKDWORD IndexCount)
{
    if (!m_Context)
        return;
    CKBgfxIndexBufferRecord *rec = m_Context->GetIndexBuffer(Buffer);
    if (!rec)
        return;
    if (m_Encoder)
        m_Encoder->setIndexBuffer(rec->Handle, StartIndex, IndexCount);
    else
        bgfx::setIndexBuffer(rec->Handle, StartIndex, IndexCount);
}

void CKBgfxEncoder::SetInstanceBuffer(CKDWORD Stream, CKDWORD Buffer,
                                       CKDWORD StartInstance, CKDWORD InstanceCount)
{
    if (!m_Context)
        return;
    CKBgfxVertexBufferRecord *rec = m_Context->GetVertexBuffer(Buffer);
    if (!rec)
        return;
    if (m_Encoder)
        m_Encoder->setInstanceDataBuffer(rec->Handle, StartInstance, InstanceCount);
    else
        bgfx::setInstanceDataBuffer(rec->Handle, StartInstance, InstanceCount);
}

void CKBgfxEncoder::SetTransientVertexBuffer(CKDWORD Stream,
                                              CKTransientVertexBuffer *Buffer)
{
    if (!Buffer || !Buffer->Data || !m_Context)
        return;
    bgfx::TransientVertexBuffer *tvb = NULL;
    CKDWORD vbCount = m_Context->m_TransientVBCount.load(std::memory_order_acquire);
    for (CKDWORD i = 0; i < vbCount; ++i)
    {
        if (m_Context->m_TransientVBPool[i].data == (uint8_t *)Buffer->Data)
        {
            tvb = &m_Context->m_TransientVBPool[i];
            break;
        }
    }
    if (!tvb)
        return;
    if (m_Encoder)
        m_Encoder->setVertexBuffer((uint8_t)Stream, tvb, 0, Buffer->VertexCount);
    else
        bgfx::setVertexBuffer((uint8_t)Stream, tvb, 0, Buffer->VertexCount);
}

void CKBgfxEncoder::SetTransientIndexBuffer(CKTransientIndexBuffer *Buffer)
{
    if (!Buffer || !Buffer->Data || !m_Context)
        return;
    bgfx::TransientIndexBuffer *tib = NULL;
    CKDWORD ibCount = m_Context->m_TransientIBCount.load(std::memory_order_acquire);
    for (CKDWORD i = 0; i < ibCount; ++i)
    {
        if (m_Context->m_TransientIBPool[i].data == (uint8_t *)Buffer->Data)
        {
            tib = &m_Context->m_TransientIBPool[i];
            break;
        }
    }
    if (!tib)
        return;
    if (m_Encoder)
        m_Encoder->setIndexBuffer(tib, 0, Buffer->IndexCount);
    else
        bgfx::setIndexBuffer(tib, 0, Buffer->IndexCount);
}

void CKBgfxEncoder::SetTransientInstanceBuffer(CKDWORD Stream,
                                                CKTransientInstanceBuffer *Buffer)
{
    (void)Stream;
    if (!Buffer || !Buffer->Data || !m_Context)
        return;
    bgfx::InstanceDataBuffer *idb = NULL;
    CKDWORD instCount = m_Context->m_TransientInstCount.load(std::memory_order_acquire);
    for (CKDWORD i = 0; i < instCount; ++i)
    {
        if (m_Context->m_TransientInstPool[i].data == (uint8_t *)Buffer->Data)
        {
            idb = &m_Context->m_TransientInstPool[i];
            break;
        }
    }
    if (!idb)
        return;
    if (m_Encoder)
        m_Encoder->setInstanceDataBuffer(idb, Buffer->StartInstance, Buffer->InstanceCount);
    else
        bgfx::setInstanceDataBuffer(idb, Buffer->StartInstance, Buffer->InstanceCount);
}

void CKBgfxEncoder::SetTexture(CKDWORD Stage, CKDWORD Uniform,
                                CKDWORD Texture, CKSamplerDesc *Sampler)
{
    static int s_SetTextureLogCount = 0;
    if (!m_Context)
        return;
    CKBgfxUniformRecord *uniRec = m_Context->GetUniform(Uniform);
    CKBgfxTextureRecord *texRec = m_Context->GetTexture(Texture);
    bgfx::TextureHandle textureHandle = texRec ? texRec->Handle : m_Context->m_DefaultWhiteTexture;
    if (CKBgfxDebugSettings().Log.TextureBindings &&
        s_SetTextureLogCount < 80) {
        CKBgfxLogf("SetTexture",
                 "stage=%u uniform=%u texture=%u uni=%p tex=%p texIdx=%u size=%ux%u fmt=%d sampler=%p",
                 Stage, Uniform, Texture, (void *)uniRec, (void *)texRec,
                 bgfx::isValid(textureHandle) ? textureHandle.idx : 0xffff,
                 texRec ? texRec->Width : 0,
                 texRec ? texRec->Height : 0,
                 texRec ? (int)texRec->Format : -1,
                 (void *)Sampler);
        s_SetTextureLogCount++;
    }
    if (!uniRec || !bgfx::isValid(textureHandle))
        return;
    uint32_t flags = CKBgfxSamplerFlags(Sampler);
    if (m_Encoder)
        m_Encoder->setTexture((uint8_t)Stage, uniRec->Handle, textureHandle, flags);
    else
        bgfx::setTexture((uint8_t)Stage, uniRec->Handle, textureHandle, flags);
}

void CKBgfxEncoder::SetUniform(CKDWORD Uniform, const void *Data, CKDWORD Count)
{
    if (!m_Context)
        return;
    CKBgfxUniformRecord *rec = m_Context->GetUniform(Uniform);
    if (!rec)
        return;
    static int s_uniformLogCount = 0;
    if (CKBgfxDebugSettings().Log.Uniforms && s_uniformLogCount < 256) {
        const float *f = static_cast<const float *>(Data);
        if (f && rec->Type == CKRST_UNIFORM_FLOAT4) {
            CKBgfxLogf("SetUniform",
                     "uniform=%u name=%s handle=%u type=%u count=%u recCount=%u first=(%.3f %.3f %.3f %.3f)",
                     Uniform, rec->Name, rec->Handle.idx, (unsigned)rec->Type, Count, rec->Count,
                     f[0], f[1], f[2], f[3]);
        } else {
            CKBgfxLogf("SetUniform",
                     "uniform=%u name=%s handle=%u type=%u count=%u recCount=%u data=%p",
                     Uniform, rec->Name, rec->Handle.idx, (unsigned)rec->Type, Count, rec->Count, Data);
        }
        ++s_uniformLogCount;
    }
    if (m_Encoder)
        m_Encoder->setUniform(rec->Handle, Data, (uint16_t)Count);
    else
        bgfx::setUniform(rec->Handle, Data, (uint16_t)Count);
}

void CKBgfxEncoder::Submit(CKRenderView View, CKDWORD Program,
                            CKDWORD Depth, CKDWORD Flags)
{
    if (!m_Context)
        return;
    CKBgfxProgramRecord *rec = m_Context->GetProgram(Program);
    bgfx::ProgramHandle ph = BGFX_INVALID_HANDLE;
    if (rec) ph = rec->Handle;
    uint8_t discard = ToBgfxDiscardFlags(Flags);
    if (m_Encoder)
        m_Encoder->submit((bgfx::ViewId)View, ph, Depth, discard);
    else
        bgfx::submit((bgfx::ViewId)View, ph, Depth, discard);
}

void CKBgfxEncoder::Touch(CKRenderView View)
{
    if (m_Encoder)
        m_Encoder->touch((bgfx::ViewId)View);
    else
        bgfx::touch((bgfx::ViewId)View);
}

void CKBgfxEncoder::Blit(CKRenderView View,
                          CKDWORD DstTexture, CKDWORD DstMip,
                          CKDWORD DstX, CKDWORD DstY,
                          CKDWORD SrcTexture, CKDWORD SrcMip,
                          const CKRECT *SrcRect)
{
    if (!m_Context)
        return;
    CKBgfxTextureRecord *dst = m_Context->GetTexture(DstTexture);
    CKBgfxTextureRecord *src = m_Context->GetTexture(SrcTexture);
    if (!dst || !src)
        return;
    if (SrcRect)
    {
        if (m_Encoder)
            m_Encoder->blit((bgfx::ViewId)View,
                            dst->Handle, (uint8_t)DstMip,
                            (uint16_t)DstX, (uint16_t)DstY, 0,
                            src->Handle, (uint8_t)SrcMip,
                            (uint16_t)SrcRect->left, (uint16_t)SrcRect->top, 0,
                            (uint16_t)(SrcRect->right - SrcRect->left),
                            (uint16_t)(SrcRect->bottom - SrcRect->top), 1);
        else
            bgfx::blit((bgfx::ViewId)View,
                        dst->Handle, (uint8_t)DstMip,
                        (uint16_t)DstX, (uint16_t)DstY, 0,
                        src->Handle, (uint8_t)SrcMip,
                        (uint16_t)SrcRect->left, (uint16_t)SrcRect->top, 0,
                        (uint16_t)(SrcRect->right - SrcRect->left),
                        (uint16_t)(SrcRect->bottom - SrcRect->top), 1);
    }
    else
    {
        if (m_Encoder)
            m_Encoder->blit((bgfx::ViewId)View,
                            dst->Handle, (uint8_t)DstMip,
                            (uint16_t)DstX, (uint16_t)DstY, 0,
                            src->Handle, (uint8_t)SrcMip);
        else
            bgfx::blit((bgfx::ViewId)View,
                        dst->Handle, (uint8_t)DstMip,
                        (uint16_t)DstX, (uint16_t)DstY, 0,
                        src->Handle, (uint8_t)SrcMip);
    }
}

void CKBgfxEncoder::SetComputeBuffer(CKDWORD Stage, CKDWORD Buffer,
                                     CK_ACCESS_MODE Access)
{
    if (!m_Context || !m_Encoder)
        return;
    CKBgfxVertexBufferRecord *vb = m_Context->GetVertexBuffer(Buffer);
    if (!vb)
        return;
    bgfx::Access::Enum bgfxAccess = bgfx::Access::Read;
    if (Access == CKRST_ACCESS_WRITE) bgfxAccess = bgfx::Access::Write;
    else if (Access == CKRST_ACCESS_READWRITE) bgfxAccess = bgfx::Access::ReadWrite;
    m_Encoder->setBuffer((uint8_t)Stage, vb->Handle, bgfxAccess);
}

void CKBgfxEncoder::SetComputeImage(CKDWORD Stage, CKDWORD Texture,
                                     CKDWORD Mip, CK_ACCESS_MODE Access)
{
    if (!m_Context || !m_Encoder)
        return;
    CKBgfxTextureRecord *tex = m_Context->GetTexture(Texture);
    if (!tex)
        return;
    bgfx::Access::Enum bgfxAccess = bgfx::Access::Read;
    if (Access == CKRST_ACCESS_WRITE) bgfxAccess = bgfx::Access::Write;
    else if (Access == CKRST_ACCESS_READWRITE) bgfxAccess = bgfx::Access::ReadWrite;
    m_Encoder->setImage((uint8_t)Stage, tex->Handle, (uint8_t)Mip, bgfxAccess);
}

void CKBgfxEncoder::SetCondition(CKDWORD Query, CKBOOL Visible)
{
    if (!m_Context || !m_Encoder)
        return;
    CKBgfxOcclusionQueryRecord *oq = m_Context->GetOcclusionQuery(Query);
    if (!oq)
        return;
    m_Encoder->setCondition(oq->Handle, Visible != FALSE);
}

void CKBgfxEncoder::SetMarker(CKSTRING Name)
{
    if (m_Encoder && Name)
        m_Encoder->setMarker(Name);
}

void CKBgfxEncoder::SubmitOcclusionQuery(CKRenderView View, CKDWORD Program,
                                          CKDWORD Query, CKDWORD Depth,
                                          CKDWORD Flags)
{
    if (!m_Context || !m_Encoder)
        return;
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    CKBgfxOcclusionQueryRecord *oq = m_Context->GetOcclusionQuery(Query);
    if (!prog || !oq)
        return;
    m_Encoder->submit((bgfx::ViewId)View, prog->Handle, oq->Handle, Depth, ToBgfxDiscardFlags(Flags));
}

void CKBgfxEncoder::SubmitIndirect(CKRenderView View, CKDWORD Program,
                                    CKDWORD IndirectBuffer,
                                    CKDWORD Start, CKDWORD Count,
                                    CKDWORD Depth, CKDWORD Flags)
{
    if (!m_Context || !m_Encoder)
        return;
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    CKBgfxIndirectBufferRecord *ib = m_Context->GetIndirectBuffer(IndirectBuffer);
    if (!prog || !ib)
        return;
    m_Encoder->submit((bgfx::ViewId)View, prog->Handle, ib->Handle,
                      (uint16_t)Start, (uint16_t)Count, Depth, ToBgfxDiscardFlags(Flags));
}

void CKBgfxEncoder::Dispatch(CKRenderView View, CKDWORD Program,
                              CKDWORD NumX, CKDWORD NumY, CKDWORD NumZ,
                              CKDWORD Flags)
{
    if (!m_Context || !m_Encoder)
        return;
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    if (!prog)
        return;
    m_Encoder->dispatch((bgfx::ViewId)View, prog->Handle, NumX, NumY, NumZ, ToBgfxDiscardFlags(Flags));
}

void CKBgfxEncoder::DispatchIndirect(CKRenderView View, CKDWORD Program,
                                      CKDWORD IndirectBuffer,
                                      CKDWORD Start, CKDWORD Count,
                                      CKDWORD Flags)
{
    if (!m_Context || !m_Encoder)
        return;
    CKBgfxProgramRecord *prog = m_Context->GetProgram(Program);
    CKBgfxIndirectBufferRecord *ib = m_Context->GetIndirectBuffer(IndirectBuffer);
    if (!prog || !ib)
        return;
    m_Encoder->dispatch((bgfx::ViewId)View, prog->Handle, ib->Handle,
                        (uint16_t)Start, (uint16_t)Count, ToBgfxDiscardFlags(Flags));
}

// ===========================================================================
// CKBgfxRasterizerContext
// ===========================================================================

CKBgfxRasterizerContext::CKBgfxRasterizerContext(CKBgfxRasterizerDriver *driver)
    : m_BgfxInitialized(FALSE), m_RendererName("Unknown"),
      m_DefaultWhiteTexture(BGFX_INVALID_HANDLE),
      m_VSync(FALSE), m_ResetFlags(BGFX_RESET_NONE), m_AntialiasSamples(0),
      m_PendingScreenShotCallback(NULL), m_PendingScreenShotFB(0),
      m_BackbufferReadTarget(NULL), m_BackbufferReadReady{false},
      m_DebugFrameId(0), m_DebugBgfxFlags(0), m_DebugOverlay(FALSE),
      m_TransformCount{0},
      m_TransientVBCount{0}, m_TransientIBCount{0}, m_TransientInstCount{0}
{
    m_Driver = driver;
    m_BgfxCallback.SetContext(this);
}

CKBgfxRasterizerContext::~CKBgfxRasterizerContext()
{
    if (m_BgfxInitialized)
    {
        for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
        {
            if (m_Encoders[i].m_Active.load(std::memory_order_acquire))
            {
                if (m_Encoders[i].m_Encoder)
                    bgfx::end(m_Encoders[i].m_Encoder);
                m_Encoders[i].m_Encoder = NULL;
                m_Encoders[i].m_Active.store(FALSE, std::memory_order_relaxed);
            }
        }

        if (bgfx::isValid(m_DefaultWhiteTexture)) {
            bgfx::destroy(m_DefaultWhiteTexture);
            m_DefaultWhiteTexture = BGFX_INVALID_HANDLE;
        }

        DestroyAllRecords(m_FrameBuffers);
        DestroyAllRecords(m_Textures);
        DestroyAllRecords(m_Programs);
        DestroyAllRecords(m_Shaders);
        DestroyAllRecords(m_Uniforms);
        DestroyAllRecords(m_VertexLayouts);
        DestroyAllRecords(m_VertexBuffers);
        DestroyAllRecords(m_IndexBuffers);
        DestroyAllRecords(m_OcclusionQueries);
        DestroyAllRecords(m_IndirectBuffers);

        bgfx::shutdown();
        m_BgfxInitialized = FALSE;
    }

    CKBgfxCloseLogFile();
}

CKBOOL CKBgfxRasterizerContext::Create(WIN_HANDLE Window, int PosX, int PosY,
                                       int Width, int Height, int Bpp,
                                       CKBOOL Fullscreen, int RefreshRate,
                                       int Zbpp, int StencilBpp)
{
    if (m_BgfxInitialized)
        return Resize(PosX, PosY, Width, Height, 0);

    m_Window = Window;
    m_PosX = PosX;
    m_PosY = PosY;

    if (Width <= 0 || Height <= 0)
    {
        CKRECT rc = {0, 0,
                     CKBgfxConfigPositiveInt("Renderer", "FallbackWidth", 640),
                     CKBgfxConfigPositiveInt("Renderer", "FallbackHeight", 480)};
        if (Window)
            VxGetClientRect(Window, &rc);
        Width = rc.right - rc.left;
        Height = rc.bottom - rc.top;
    }

    if (Width <= 0)
        Width = CKBgfxConfigPositiveInt("Renderer", "FallbackWidth", 640);
    if (Height <= 0)
        Height = CKBgfxConfigPositiveInt("Renderer", "FallbackHeight", 480);

    CKBgfxResolveFullscreenWindowSize(Window, Fullscreen, Width, Height);

    m_Width = Width;
    m_Height = Height;
    m_Bpp = (Bpp > 0) ? Bpp : 32;
    m_ZBpp = (Zbpp > 0) ? Zbpp : 24;
    m_StencilBpp = (StencilBpp > 0) ? StencilBpp : 8;
    m_Fullscreen = Fullscreen;
    m_RefreshRate = RefreshRate;

    const bgfx::RendererType::Enum requestedRenderer = CKBgfxParseRequestedRenderer();

    bgfx::Init init;
    init.type = requestedRenderer;
    if (!CKBgfxFillSDLPlatformData(Window, init.platformData)) {
        CKBgfxLogf("Init", "failed to extract SDL native window data window=%p", Window);
        return FALSE;
    }
    init.resolution.width = Width;
    init.resolution.height = Height;
    init.resolution.reset = m_ResetFlags;
    init.callback = &m_BgfxCallback;

    if (!bgfx::init(init)) {
        CKBgfxLogf("Init", "bgfx init failed for requested renderer '%s' window=%p size=%dx%d",
                   CKBgfxRendererTypeName(requestedRenderer), Window, Width, Height);
        return FALSE;
    }

    m_BgfxInitialized = TRUE;
    const bgfx::RendererType::Enum actualRenderer = bgfx::getRendererType();
    m_RendererName = CKBgfxRendererTypeName(actualRenderer);
    if (m_Driver) {
        CKBgfxRasterizerDriver *driver = static_cast<CKBgfxRasterizerDriver *>(m_Driver);
        driver->m_ShaderTarget.Format = CKRST_SHADER_FORMAT_NATIVE;
        driver->m_ShaderTarget.Profile = CKBgfxShaderProfile(actualRenderer);
        driver->m_ShaderTarget.Version = 0;
        driver->m_ShaderTarget.Flags = 0;
        m_Driver->m_Desc.Format("bgfx %s Driver", m_RendererName);
    }

    CKBgfxLogf("Init", "renderer requested=%s actual=%s",
               CKBgfxRendererTypeName(requestedRenderer), m_RendererName);

    const uint32_t whitePixel = 0xffffffffu;
    const bgfx::Memory *whiteMem = bgfx::copy(&whitePixel, sizeof(whitePixel));
    m_DefaultWhiteTexture = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::BGRA8, 0, whiteMem);
    if (!bgfx::isValid(m_DefaultWhiteTexture))
        CKBgfxLogf("Init", "failed to create default white texture");

    bgfx::setViewRect(0, 0, 0, (uint16_t)Width, (uint16_t)Height);
    bgfx::setViewClear(0,
                        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                        0x000000ff, 1.0f, 0);
    bgfx::touch(0);
    bgfx::frame();
    ConfigureDebug();

    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::Resize(int PosX, int PosY,
                                       int Width, int Height, CKDWORD Flags)
{
    if (!m_BgfxInitialized)
        return FALSE;
    if (Width <= 0 || Height <= 0)
        return FALSE;

    CKBgfxResolveFullscreenWindowSize(m_Window, m_Fullscreen, Width, Height);

    m_PosX = PosX;
    m_PosY = PosY;
    m_Width = Width;
    m_Height = Height;

    bgfx::reset((uint32_t)Width, (uint32_t)Height, m_ResetFlags);
    bgfx::setViewRect(0, 0, 0, (uint16_t)Width, (uint16_t)Height);

    return TRUE;
}

void CKBgfxRasterizerContext::ConfigureDebug()
{
    const CKBgfxDebugConfig &debug = CKBgfxDebugSettings();
    m_DebugBgfxFlags = debug.BgfxFlags;
    m_DebugOverlay = debug.Overlay ? TRUE : FALSE;

    if (m_DebugBgfxFlags != BGFX_DEBUG_NONE)
        bgfx::setDebug(m_DebugBgfxFlags);

    if (debug.Log.Config ||
        m_DebugBgfxFlags != BGFX_DEBUG_NONE ||
        m_DebugOverlay) {
        CKBgfxLogf("Debug", "configured bgfxFlags=0x%X overlay=%d",
                 m_DebugBgfxFlags, m_DebugOverlay ? 1 : 0);
    }
}

void CKBgfxRasterizerContext::DrawDebugOverlay()
{
    if (!m_DebugOverlay)
        return;

    const bgfx::Stats *s = bgfx::getStats();
    bgfx::dbgTextClear(0, false);
    bgfx::dbgTextPrintf(0, 0, 0x4f, "CKBgfx debug frame=%u size=%ux%u",
                        m_DebugFrameId, (unsigned)m_Width, (unsigned)m_Height);
    if (s) {
        bgfx::dbgTextPrintf(0, 1, 0x2f, "bgfx gpuFrame=%u draws=%u blits=%u computes=%u views=%u",
                            s->gpuFrameNum, s->numDraw, s->numBlit, s->numCompute, s->numViews);
        bgfx::dbgTextPrintf(0, 2, 0x2f, "transient vb=%u ib=%u waitSubmit=%lld waitRender=%lld",
                            s->transientVbUsed, s->transientIbUsed,
                            (long long)s->waitSubmit, (long long)s->waitRender);
    }
    bgfx::dbgTextPrintf(0, 3, 0x1f, "views: 0 clear 1 bg2d 2 opaque3d 3 transparent 4 fg2d");
}

// ---------------------------------------------------------------------------
// Accessor helpers
// ---------------------------------------------------------------------------

CKBgfxShaderRecord *CKBgfxRasterizerContext::GetShader(CKDWORD Handle)
{
    return GetSlot(m_Shaders, Handle);
}

CKBgfxProgramRecord *CKBgfxRasterizerContext::GetProgram(CKDWORD Handle)
{
    return GetSlot(m_Programs, Handle);
}

CKBgfxUniformRecord *CKBgfxRasterizerContext::GetUniform(CKDWORD Handle)
{
    return GetSlot(m_Uniforms, Handle);
}

CKBgfxVertexLayoutRecord *CKBgfxRasterizerContext::GetVertexLayout(CKDWORD Handle)
{
    return GetSlot(m_VertexLayouts, Handle);
}

CKBgfxVertexBufferRecord *CKBgfxRasterizerContext::GetVertexBuffer(CKDWORD Handle)
{
    return GetSlot(m_VertexBuffers, Handle);
}

CKBgfxIndexBufferRecord *CKBgfxRasterizerContext::GetIndexBuffer(CKDWORD Handle)
{
    return GetSlot(m_IndexBuffers, Handle);
}

CKBgfxTextureRecord *CKBgfxRasterizerContext::GetTexture(CKDWORD Handle)
{
    return GetSlot(m_Textures, Handle);
}

CKBgfxFrameBufferRecord *CKBgfxRasterizerContext::GetFrameBuffer(CKDWORD Handle)
{
    return GetSlot(m_FrameBuffers, Handle);
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::CreateVertexBuffer(CKDWORD Buffer,
                                                     CKVertexBufferDesc *Desc,
                                                     const void *Data)
{
    if (!m_BgfxInitialized || !Desc || Buffer == 0)
        return CKERR_INVALIDPARAMETER;

    CKDWORD vertexSize = Desc->m_VertexSize;
    if (vertexSize == 0)
        return CKERR_INVALIDPARAMETER;
    uint64_t totalSize64 = (uint64_t)Desc->m_MaxVertexCount * vertexSize;
    if (totalSize64 > UINT32_MAX)
        return CKERR_OUTOFMEMORY;
    CKDWORD totalSize = (CKDWORD)totalSize64;

    bgfx::VertexLayout layout;
    layout.begin();
    layout.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
    layout.m_stride = (uint16_t)vertexSize;
    layout.end();

    uint16_t flags = BGFX_BUFFER_ALLOW_RESIZE;
    if (Desc->m_Flags & CKRST_VB_COMPUTE_READ)
        flags |= BGFX_BUFFER_COMPUTE_READ;
    if (Desc->m_Flags & CKRST_VB_COMPUTE_WRITE)
        flags |= BGFX_BUFFER_COMPUTE_WRITE;

    bgfx::DynamicVertexBufferHandle handle;
    if (Data)
    {
        const bgfx::Memory *mem = bgfx::copy(Data, totalSize);
        handle = bgfx::createDynamicVertexBuffer(mem, layout, flags);
    }
    else
    {
        handle = bgfx::createDynamicVertexBuffer(Desc->m_MaxVertexCount, layout, flags);
    }

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxVertexBufferRecord();
    rec->Handle = handle;
    rec->Layout = 0;
    rec->VertexSize = vertexSize;

    CKBgfxVertexBufferRecord *&slot = EnsureSlot(m_VertexBuffers, Buffer);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateIndexBuffer(CKDWORD Buffer,
                                                    CKIndexBufferDesc *Desc,
                                                    CKBOOL Index32,
                                                    const void *Data)
{
    if (!m_BgfxInitialized || !Desc || Buffer == 0)
        return CKERR_INVALIDPARAMETER;

    CKDWORD indexSize = Index32 ? 4 : 2;
    uint64_t totalSize64 = (uint64_t)Desc->m_MaxIndexCount * indexSize;
    if (totalSize64 > UINT32_MAX)
        return CKERR_OUTOFMEMORY;
    CKDWORD totalSize = (CKDWORD)totalSize64;

    uint16_t flags = BGFX_BUFFER_ALLOW_RESIZE;
    if (Index32) flags |= BGFX_BUFFER_INDEX32;

    bgfx::DynamicIndexBufferHandle handle;
    if (Data)
    {
        const bgfx::Memory *mem = bgfx::copy(Data, totalSize);
        handle = bgfx::createDynamicIndexBuffer(mem, flags);
    }
    else
    {
        handle = bgfx::createDynamicIndexBuffer(Desc->m_MaxIndexCount, flags);
    }

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxIndexBufferRecord();
    rec->Handle = handle;
    rec->Index32 = Index32;

    CKBgfxIndexBufferRecord *&slot = EnsureSlot(m_IndexBuffers, Buffer);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateTexture(CKDWORD Texture,
                                                CKTextureDesc *Desc,
                                                const VxImageDescEx *Data)
{
    static int s_CreateTextureLogCount = 0;
    if (!m_BgfxInitialized || !Desc || Texture == 0)
        return CKERR_INVALIDPARAMETER;

    uint16_t w = (uint16_t)Desc->Format.Width;
    uint16_t h = (uint16_t)Desc->Format.Height;
    uint16_t d = (uint16_t)std::max<CKDWORD>(1, Desc->Depth);
    if (w == 0 || h == 0)
        return CKERR_INVALIDPARAMETER;

    bool hasMips = Desc->MipMapCount > 1;

    VX_PIXELFORMAT pf = VxImageDesc2PixelFormat(Desc->Format);
    bgfx::TextureFormat::Enum fmt = CKBgfxTextureFormat(pf);

    uint64_t texFlags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE;
    if (Desc->Flags & CKRST_TEXTURE_RENDERTARGET)
        texFlags |= BGFX_TEXTURE_RT;
    if (Desc->Flags & CKRST_TEXTURE_READBACK)
        texFlags |= BGFX_TEXTURE_READ_BACK;
    if (Desc->Flags & CKRST_TEXTURE_COMPUTE_WRITE)
        texFlags |= BGFX_TEXTURE_COMPUTE_WRITE;

    const bgfx::Memory *mem = NULL;
    CKDWORD copiedBytes = 0;
    if (Data && Data->Image)
    {
        CKDWORD bpp = (Data->BitsPerPixel > 0) ? Data->BitsPerPixel : 32;
        CKDWORD rowBytes = (CKDWORD)Data->Width * bpp / 8;
        CKDWORD imgSize = rowBytes * Data->Height;
        if ((Desc->Flags & CKRST_TEXTURE_VOLUMEMAP) && d > 1)
            imgSize *= d;
        if (Data->TotalImageSize != 0)
            imgSize = Data->TotalImageSize;
        if (pf == _DXT1 || pf == _DXT3 || pf == _DXT5)
            imgSize = Data->TotalImageSize;
        copiedBytes = imgSize;
        mem = bgfx::copy(Data->Image, imgSize);
    }

    bgfx::TextureHandle handle;
    if (Desc->Flags & CKRST_TEXTURE_CUBEMAP)
    {
        handle = bgfx::createTextureCube(w, hasMips, 1, fmt, texFlags, mem);
    }
    else if ((Desc->Flags & CKRST_TEXTURE_VOLUMEMAP) && d > 1)
    {
        handle = bgfx::createTexture3D(w, h, d, hasMips, fmt, texFlags, mem);
    }
    else
    {
        handle = bgfx::createTexture2D(w, h, hasMips, 1, fmt, texFlags, mem);
    }

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxTextureRecord();
    rec->Handle = handle;
    rec->Flags = Desc->Flags;
    rec->Width = w;
    rec->Height = h;
    rec->Depth = d;
    rec->IsDepth = FALSE;
    rec->Format = fmt;
    rec->BitsPerPixel = (Desc->Format.BitsPerPixel > 0) ? Desc->Format.BitsPerPixel :
                         ((Data && Data->BitsPerPixel > 0) ? Data->BitsPerPixel : 32);

    CKBgfxTextureRecord *&slot = EnsureSlot(m_Textures, Texture);
    DestroyRecord(slot);
    slot = rec;

    if (CKBgfxDebugSettings().Log.Textures &&
        s_CreateTextureLogCount < 80) {
        CKBgfxLogf("CreateTexture",
                 "id=%u handle=%u size=%ux%u flags=0x%X pf=%d bgfxFmt=%d bpp=%u mips=%u initBytes=%u initFirst=0x%08X initHash=0x%08X",
                 Texture, rec->Handle.idx, rec->Width, rec->Height, Desc->Flags,
                 (int)pf, (int)fmt, rec->BitsPerPixel, Desc->MipMapCount, copiedBytes,
                 FirstDword(Data ? Data->Image : nullptr, copiedBytes),
                 SampleBytesChecksum(Data ? Data->Image : nullptr, copiedBytes));
        s_CreateTextureLogCount++;
    }

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateShader(CKDWORD Shader, CKShaderDesc *Desc)
{
    if (!m_BgfxInitialized || !Desc || Shader == 0)
        return CKERR_INVALIDPARAMETER;
    if (!Desc->Code || Desc->CodeSize == 0)
        return CKERR_INVALIDPARAMETER;
    CKShaderTargetDesc target;
    if (!m_Driver ||
        m_Driver->GetShaderTarget(&target) != CK_OK ||
        Desc->Format != target.Format ||
        Desc->Profile != target.Profile)
        return CKERR_INVALIDPARAMETER;

    const bgfx::Memory *mem = bgfx::copy(Desc->Code, Desc->CodeSize);
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    if (!bgfx::isValid(handle))
        return CKERR_INVALIDPARAMETER;

    auto *rec = new CKBgfxShaderRecord();
    rec->Handle = handle;
    rec->Stage = Desc->Stage;

    CKBgfxShaderRecord *&slot = EnsureSlot(m_Shaders, Shader);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateProgram(CKDWORD Program, CKProgramDesc *Desc)
{
    if (!m_BgfxInitialized || !Desc || Program == 0)
        return CKERR_INVALIDPARAMETER;

    bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;

    CKBgfxShaderRecord *vs = GetShader(Desc->VertexShader);
    if (vs && vs->Stage == CKRST_SHADER_COMPUTE)
    {
        handle = bgfx::createProgram(vs->Handle, false);
        if (!bgfx::isValid(handle))
            return CKERR_INVALIDPARAMETER;

        if (Desc->ConsumeShaders)
        {
            bgfx::destroy(vs->Handle);
            delete vs;
            m_Shaders[Desc->VertexShader] = NULL;
        }
    }
    else
    {
        CKBgfxShaderRecord *ps = GetShader(Desc->PixelShader);
        if (!vs || !ps) {
            CKBgfxLogf("CreateProgram", "missing shaders vs=%p(h=%u) ps=%p(h=%u) shadersSize=%d",
                       vs, Desc->VertexShader, ps, Desc->PixelShader, m_Shaders.Size());
            return CKERR_INVALIDPARAMETER;
        }

        handle = bgfx::createProgram(vs->Handle, ps->Handle, false);
        if (!bgfx::isValid(handle)) {
            CKBgfxLogf("CreateProgram", "bgfx::createProgram failed vs.idx=%u ps.idx=%u",
                       vs->Handle.idx, ps->Handle.idx);
            return CKERR_INVALIDPARAMETER;
        }

        if (Desc->ConsumeShaders)
        {
            bgfx::destroy(vs->Handle);
            delete vs;
            m_Shaders[Desc->VertexShader] = NULL;
            bgfx::destroy(ps->Handle);
            delete ps;
            m_Shaders[Desc->PixelShader] = NULL;
        }
    }

    auto *rec = new CKBgfxProgramRecord();
    rec->Handle = handle;
    rec->SpecializationDwordCount = 0;
    memset(rec->SpecializationDwords, 0, sizeof(rec->SpecializationDwords));
    if (Desc->SpecializationDwords && Desc->SpecializationDwordCount > 0) {
        rec->SpecializationDwordCount = Desc->SpecializationDwordCount;
        if (rec->SpecializationDwordCount > 10)
            rec->SpecializationDwordCount = 10;
        memcpy(rec->SpecializationDwords, Desc->SpecializationDwords,
               rec->SpecializationDwordCount * sizeof(CKDWORD));
    }

    CKBgfxProgramRecord *&slot = EnsureSlot(m_Programs, Program);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateUniform(CKDWORD Uniform, CKUniformDesc *Desc)
{
    if (!m_BgfxInitialized || !Desc || Uniform == 0 || !Desc->Name)
        return CKERR_INVALIDPARAMETER;

    bgfx::UniformType::Enum bgfxType = CKBgfxUniformType(Desc->Type);
    uint16_t num = (uint16_t)(Desc->Count > 0 ? Desc->Count : 1);
    bgfx::UniformHandle handle = bgfx::createUniform(Desc->Name, bgfxType, num);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxUniformRecord();
    rec->Handle = handle;
    rec->Type = Desc->Type;
    rec->Count = Desc->Count;
    strncpy(rec->Name, Desc->Name, sizeof(rec->Name) - 1);
    rec->Name[sizeof(rec->Name) - 1] = '\0';

    CKBgfxUniformRecord *&slot = EnsureSlot(m_Uniforms, Uniform);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateVertexLayout(CKDWORD Layout,
                                                     CKVertexLayoutDesc *Desc)
{
    if (!m_BgfxInitialized || !Desc || Layout == 0 || !Desc->Elements)
        return CKERR_INVALIDPARAMETER;

    bgfx::VertexLayout bgfxLayout;
    bgfxLayout.begin();
    for (CKDWORD i = 0; i < Desc->ElementCount; ++i)
    {
        CKVertexElementDesc &elem = Desc->Elements[i];
        bgfxLayout.add(
            CKBgfxAttrib(elem.Attrib),
            elem.Count,
            CKBgfxAttribType(elem.Type),
            elem.Normalized ? true : false);
    }
    if (Desc->Stride[0] > 0)
        bgfxLayout.m_stride = Desc->Stride[0];
    bgfxLayout.end();

    bgfx::VertexLayoutHandle handle = bgfx::createVertexLayout(bgfxLayout);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxVertexLayoutRecord();
    rec->Handle = handle;
    rec->Layout = bgfxLayout;

    CKBgfxVertexLayoutRecord *&slot = EnsureSlot(m_VertexLayouts, Layout);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateFrameBuffer(CKDWORD FrameBuffer,
                                                    CKFrameBufferDesc *Desc)
{
    if (!m_BgfxInitialized || !Desc || FrameBuffer == 0)
        return CKERR_INVALIDPARAMETER;

    CKDWORD totalAttachments = Desc->ColorCount;
    CKBOOL hasDepth = (Desc->DepthStencil.Texture != 0);
    if (hasDepth) totalAttachments++;

    if (totalAttachments == 0)
        return CKERR_INVALIDPARAMETER;

    bgfx::Attachment *attachments = new bgfx::Attachment[totalAttachments];
    CKDWORD idx = 0;

    for (CKDWORD i = 0; i < Desc->ColorCount; ++i)
    {
        CKBgfxTextureRecord *tex = GetTexture(Desc->Color[i].Texture);
        if (!tex)
        {
            delete[] attachments;
            return CKERR_INVALIDPARAMETER;
        }
        attachments[idx].init(tex->Handle, bgfx::Access::Write,
                              (uint16_t)Desc->Color[i].Layer, 1,
                              (uint16_t)Desc->Color[i].Mip);
        idx++;
    }

    if (hasDepth)
    {
        CKBgfxTextureRecord *depthTex = GetTexture(Desc->DepthStencil.Texture);
        if (!depthTex)
        {
            delete[] attachments;
            return CKERR_INVALIDPARAMETER;
        }
        attachments[idx].init(depthTex->Handle, bgfx::Access::Write,
                              (uint16_t)Desc->DepthStencil.Layer, 1,
                              (uint16_t)Desc->DepthStencil.Mip);
        idx++;
    }

    bgfx::FrameBufferHandle handle = bgfx::createFrameBuffer(
        (uint8_t)totalAttachments, attachments, false);
    delete[] attachments;

    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxFrameBufferRecord();
    rec->Handle = handle;
    rec->FirstColorTexture = (Desc->ColorCount > 0) ? Desc->Color[0].Texture : 0;

    CKBgfxFrameBufferRecord *&slot = EnsureSlot(m_FrameBuffers, FrameBuffer);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateDepthTexture(CKDWORD Texture,
                                                     CKDepthTextureDesc *Desc)
{
    if (!m_BgfxInitialized || !Desc || Texture == 0)
        return CKERR_INVALIDPARAMETER;

    uint16_t w = (uint16_t)Desc->Width;
    uint16_t h = (uint16_t)Desc->Height;
    bgfx::TextureFormat::Enum fmt = CKBgfxDepthFormat(Desc->DepthFormat);
    bool hasMips = Desc->MipMapCount > 1;

    uint64_t texFlags = BGFX_TEXTURE_RT_WRITE_ONLY;
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        w, h, hasMips, 1, fmt, texFlags, NULL);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxTextureRecord();
    rec->Handle = handle;
    rec->Flags = Desc->Flags | CKRST_TEXTURE_DEPTHSTENCIL;
    rec->Width = w;
    rec->Height = h;
    rec->Depth = 1;
    rec->IsDepth = TRUE;
    rec->Format = fmt;
    rec->BitsPerPixel = 0;

    CKBgfxTextureRecord *&slot = EnsureSlot(m_Textures, Texture);
    DestroyRecord(slot);
    slot = rec;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::DeleteObject(CKDWORD Object, CKDWORD Type)
{
    if (!m_BgfxInitialized || Object == 0)
        return CKERR_INVALIDPARAMETER;

    if (Type & CKRST_OBJ_TEXTURE)
    {
        CKBgfxTextureRecord *rec = GetTexture(Object);
        if (rec) { DestroyRecord(rec); m_Textures[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_VERTEXBUFFER)
    {
        CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Object);
        if (rec) { DestroyRecord(rec); m_VertexBuffers[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_INDEXBUFFER)
    {
        CKBgfxIndexBufferRecord *rec = GetIndexBuffer(Object);
        if (rec) { DestroyRecord(rec); m_IndexBuffers[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_SHADER)
    {
        CKBgfxShaderRecord *rec = GetShader(Object);
        if (rec) { DestroyRecord(rec); m_Shaders[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_PROGRAM)
    {
        CKBgfxProgramRecord *rec = GetProgram(Object);
        if (rec) { DestroyRecord(rec); m_Programs[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_UNIFORM)
    {
        CKBgfxUniformRecord *rec = GetUniform(Object);
        if (rec) { DestroyRecord(rec); m_Uniforms[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_VERTEXLAYOUT)
    {
        CKBgfxVertexLayoutRecord *rec = GetVertexLayout(Object);
        if (rec) { DestroyRecord(rec); m_VertexLayouts[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_FRAMEBUFFER)
    {
        CKBgfxFrameBufferRecord *rec = GetFrameBuffer(Object);
        if (rec) { DestroyRecord(rec); m_FrameBuffers[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_OCCLUSIONQUERY)
    {
        CKBgfxOcclusionQueryRecord *rec = GetOcclusionQuery(Object);
        if (rec) { DestroyRecord(rec); m_OcclusionQueries[Object] = NULL; return CK_OK; }
    }
    if (Type & CKRST_OBJ_INDIRECTBUFFER)
    {
        CKBgfxIndirectBufferRecord *rec = GetIndirectBuffer(Object);
        if (rec) { DestroyRecord(rec); m_IndirectBuffers[Object] = NULL; return CK_OK; }
    }

    return CKERR_INVALIDPARAMETER;
}

void CKBgfxRasterizerContext::FlushObjects(CKDWORD TypeMask)
{
    if (!m_BgfxInitialized)
        return;

    if (TypeMask & CKRST_OBJ_TEXTURE)
        DestroyAllRecords(m_Textures);
    if (TypeMask & CKRST_OBJ_VERTEXBUFFER)
        DestroyAllRecords(m_VertexBuffers);
    if (TypeMask & CKRST_OBJ_INDEXBUFFER)
        DestroyAllRecords(m_IndexBuffers);
    if (TypeMask & CKRST_OBJ_SHADER)
        DestroyAllRecords(m_Shaders);
    if (TypeMask & CKRST_OBJ_PROGRAM)
        DestroyAllRecords(m_Programs);
    if (TypeMask & CKRST_OBJ_UNIFORM)
        DestroyAllRecords(m_Uniforms);
    if (TypeMask & CKRST_OBJ_VERTEXLAYOUT)
        DestroyAllRecords(m_VertexLayouts);
    if (TypeMask & CKRST_OBJ_FRAMEBUFFER)
        DestroyAllRecords(m_FrameBuffers);
    if (TypeMask & CKRST_OBJ_OCCLUSIONQUERY)
        DestroyAllRecords(m_OcclusionQueries);
    if (TypeMask & CKRST_OBJ_INDIRECTBUFFER)
        DestroyAllRecords(m_IndirectBuffers);
}

// ---------------------------------------------------------------------------
// Resource update
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::UpdateVertexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                                     CKDWORD Size, const void *Data)
{
    if (!m_BgfxInitialized || !Data || Buffer == 0 || Size == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Buffer);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

    if (rec->VertexSize == 0)
        return CKERR_INVALIDPARAMETER;
    if (Offset % rec->VertexSize != 0)
        return CKERR_INVALIDPARAMETER;
    CKDWORD startVertex = Offset / rec->VertexSize;
    const bgfx::Memory *mem = bgfx::copy(Data, Size);
    bgfx::update(rec->Handle, startVertex, mem);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::UpdateIndexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                                    CKDWORD Size, const void *Data)
{
    if (!m_BgfxInitialized || !Data || Buffer == 0 || Size == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxIndexBufferRecord *rec = GetIndexBuffer(Buffer);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

    CKDWORD indexSize = rec->Index32 ? 4 : 2;
    CKDWORD startIndex = (indexSize > 0) ? Offset / indexSize : 0;
    const bgfx::Memory *mem = bgfx::copy(Data, Size);
    bgfx::update(rec->Handle, startIndex, mem);

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::UpdateTexture(CKDWORD Texture, CKDWORD Mip,
                                                CKDWORD Face, const CKRECT *Region,
                                                const VxImageDescEx *Data)
{
    static int s_UpdateTextureLogCount = 0;
    if (!m_BgfxInitialized || !Data || !Data->Image)
        return CKERR_INVALIDPARAMETER;

    CKBgfxTextureRecord *rec = GetTexture(Texture);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

    bool isCube = (rec->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    bool isVolume = (rec->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && rec->Depth > 1;
    if (!isCube && !isVolume && Face != 0)
        return CKERR_INVALIDPARAMETER;
    if (isVolume && Face >= rec->Depth)
        return CKERR_INVALIDPARAMETER;

    uint16_t x = 0, y = 0;
    uint16_t w = (uint16_t)std::max<CKDWORD>(1, rec->Width >> Mip);
    uint16_t h = (uint16_t)std::max<CKDWORD>(1, rec->Height >> Mip);
    if (Region)
    {
        x = (uint16_t)Region->left;
        y = (uint16_t)Region->top;
        w = (uint16_t)(Region->right - Region->left);
        h = (uint16_t)(Region->bottom - Region->top);
    }
    else if (Data->Width > 0 && Data->Height > 0)
    {
        w = (uint16_t)Data->Width;
        h = (uint16_t)Data->Height;
    }

    bool compressed = (rec->Format == bgfx::TextureFormat::BC1
                    || rec->Format == bgfx::TextureFormat::BC2
                    || rec->Format == bgfx::TextureFormat::BC3);

    const bgfx::Memory *mem = NULL;
    if (compressed)
    {
        CKDWORD imgSize = Data->TotalImageSize;
        if (imgSize == 0)
        {
            CKDWORD blockSize = (rec->Format == bgfx::TextureFormat::BC1) ? 8 : 16;
            CKDWORD blocksW = (w + 3) / 4;
            CKDWORD blocksH = (h + 3) / 4;
            imgSize = blocksW * blocksH * blockSize;
        }
        mem = bgfx::copy(Data->Image, imgSize);
    }
    else
    {
        CKDWORD bpp = rec->BitsPerPixel > 0 ? rec->BitsPerPixel : 32;
        CKDWORD rowBytes = (CKDWORD)w * bpp / 8;
        CKDWORD pitch = (Data->BytesPerLine > 0) ? (CKDWORD)Data->BytesPerLine : rowBytes;
        if (CKBgfxDebugSettings().Log.Textures &&
            s_UpdateTextureLogCount < 120) {
            uint32_t sampleSize = pitch * h;
            CKBgfxLogf("UpdateTexture",
                     "id=%u handle=%u mip=%u face=%u region=%ux%u+%u,%u rec=%ux%u recBpp=%u data=%dx%d dataBpp=%d pitch=%u rowBytes=%u first=0x%08X hash=0x%08X",
                     Texture, rec->Handle.idx, Mip, Face, w, h, x, y,
                     rec->Width, rec->Height, rec->BitsPerPixel,
                     Data->Width, Data->Height, Data->BitsPerPixel,
                     pitch, rowBytes,
                     FirstDword(Data->Image, sampleSize),
                     SampleBytesChecksum(Data->Image, sampleSize));
            s_UpdateTextureLogCount++;
        }
        if (pitch == rowBytes)
        {
            mem = bgfx::copy(Data->Image, rowBytes * h);
        }
        else
        {
            mem = bgfx::alloc(rowBytes * h);
            for (uint16_t row = 0; row < h; ++row)
                memcpy(mem->data + row * rowBytes, (CKBYTE *)Data->Image + row * pitch, rowBytes);
        }
    }

    if (isCube)
        bgfx::updateTextureCube(rec->Handle, 0, (uint8_t)Face, (uint8_t)Mip, x, y, w, h, mem);
    else if (isVolume)
        bgfx::updateTexture3D(rec->Handle, (uint8_t)Mip, x, y, (uint16_t)Face, w, h, 1, mem);
    else
        bgfx::updateTexture2D(rec->Handle, (uint16_t)Face, (uint8_t)Mip, x, y, w, h, mem);

    return CK_OK;
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::ReadTexture(CKDWORD Texture, CKDWORD Mip,
                                              VxImageDescEx *Data)
{
    if (!m_BgfxInitialized || !Data || !Data->Image)
        return CKERR_INVALIDPARAMETER;

    for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
        if (m_Encoders[i].m_Active.load(std::memory_order_acquire))
            return CKERR_INVALIDOPERATION;

    CKBgfxTextureRecord *rec = GetTexture(Texture);
    if (!rec)
        return CKERR_INVALIDPARAMETER;

    if (!(rec->Flags & CKRST_TEXTURE_READBACK))
        return CKERR_INVALIDOPERATION;

    uint32_t frameReady = bgfx::readTexture(rec->Handle, Data->Image, (uint8_t)Mip);

    uint32_t current = bgfx::frame();
    while (current < frameReady)
        current = bgfx::frame();

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::ReadFrameBuffer(CKDWORD FrameBuffer,
                                                  VxImageDescEx *Data)
{
    if (!m_BgfxInitialized || !Data || !Data->Image)
        return CKERR_INVALIDPARAMETER;

    for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
        if (m_Encoders[i].m_Active.load(std::memory_order_acquire))
            return CKERR_INVALIDOPERATION;

    if (FrameBuffer == 0)
    {
        m_BackbufferReadTarget = Data;
        m_BackbufferReadReady.store(false, std::memory_order_relaxed);

        bgfx::FrameBufferHandle invalid = BGFX_INVALID_HANDLE;
        bgfx::requestScreenShot(invalid, "__backbuffer_read__");

        bgfx::frame();
        int waitFrames = 0;
        const int maxWaitFrames = CKBgfxConfigPositiveInt("Readback", "TimeoutFrames", 120);
        while (!m_BackbufferReadReady.load(std::memory_order_acquire) && waitFrames++ < maxWaitFrames)
            bgfx::frame();

        m_BackbufferReadTarget = NULL;
        if (!m_BackbufferReadReady.load(std::memory_order_acquire)) {
            CKBgfxLogf("ReadFrameBuffer", "backbuffer readback timed out");
            return CKERR_INVALIDOPERATION;
        }
        return CK_OK;
    }

    CKBgfxFrameBufferRecord *fbRec = GetFrameBuffer(FrameBuffer);
    if (!fbRec || fbRec->FirstColorTexture == 0)
        return CKERR_INVALIDPARAMETER;

    CKBgfxTextureRecord *texRec = GetTexture(fbRec->FirstColorTexture);
    if (!texRec)
        return CKERR_INVALIDPARAMETER;

    if (!(texRec->Flags & CKRST_TEXTURE_READBACK))
        return CKERR_INVALIDOPERATION;

    uint32_t frameReady = bgfx::readTexture(texRec->Handle, Data->Image, 0);

    uint32_t current = bgfx::frame();
    while (current < frameReady)
        current = bgfx::frame();

    return CK_OK;
}

// ---------------------------------------------------------------------------
// Render views
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::SetViewName(CKRenderView View, CKSTRING Name)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    bgfx::setViewName((bgfx::ViewId)View, Name);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewRect(CKRenderView View, const CKRECT &Rect)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    bgfx::setViewRect((bgfx::ViewId)View,
                       (uint16_t)Rect.left, (uint16_t)Rect.top,
                       (uint16_t)(Rect.right - Rect.left),
                       (uint16_t)(Rect.bottom - Rect.top));
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewScissor(CKRenderView View, const CKRECT *Rect)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    if (Rect)
        bgfx::setViewScissor((bgfx::ViewId)View,
                              (uint16_t)Rect->left, (uint16_t)Rect->top,
                              (uint16_t)(Rect->right - Rect->left),
                              (uint16_t)(Rect->bottom - Rect->top));
    else
        bgfx::setViewScissor((bgfx::ViewId)View);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewClear(CKRenderView View, CKDWORD Flags,
                                               CKDWORD Color, float Z,
                                               CKDWORD Stencil)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;

    uint16_t bgfxClearFlags = 0;
    if (Flags & CKRST_CTXCLEAR_COLOR)   bgfxClearFlags |= BGFX_CLEAR_COLOR;
    if (Flags & CKRST_CTXCLEAR_DEPTH)   bgfxClearFlags |= BGFX_CLEAR_DEPTH;
    if (Flags & CKRST_CTXCLEAR_STENCIL) bgfxClearFlags |= BGFX_CLEAR_STENCIL;

    CKDWORD a = (Color >> 24) & 0xFF;
    CKDWORD r = (Color >> 16) & 0xFF;
    CKDWORD g = (Color >> 8) & 0xFF;
    CKDWORD b = (Color >> 0) & 0xFF;
    CKDWORD bgfxColor = (r << 24) | (g << 16) | (b << 8) | a;

    bgfx::setViewClear((bgfx::ViewId)View, bgfxClearFlags, bgfxColor, Z,
                        (uint8_t)Stencil);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewTransform(CKRenderView View,
                                                   const VxMatrix *ViewMatrix,
                                                   const VxMatrix *ProjMatrix)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    bgfx::setViewTransform((bgfx::ViewId)View,
                            ViewMatrix ? (const void *)ViewMatrix : NULL,
                            ProjMatrix ? (const void *)ProjMatrix : NULL);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewFrameBuffer(CKRenderView View,
                                                     CKDWORD FrameBuffer)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    if (FrameBuffer == 0)
    {
        bgfx::FrameBufferHandle invalid = BGFX_INVALID_HANDLE;
        bgfx::setViewFrameBuffer((bgfx::ViewId)View, invalid);
    }
    else
    {
        CKBgfxFrameBufferRecord *rec = GetFrameBuffer(FrameBuffer);
        if (rec)
            bgfx::setViewFrameBuffer((bgfx::ViewId)View, rec->Handle);
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewMode(CKRenderView View, CK_VIEW_MODE Mode)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    bgfx::ViewMode::Enum bgfxMode;
    switch (Mode) {
    case CKRST_VIEWMODE_DEFAULT:    bgfxMode = bgfx::ViewMode::Default; break;
    case CKRST_VIEWMODE_SEQUENTIAL: bgfxMode = bgfx::ViewMode::Sequential; break;
    case CKRST_VIEWMODE_DEPTH_ASC:  bgfxMode = bgfx::ViewMode::DepthAscending; break;
    case CKRST_VIEWMODE_DEPTH_DESC: bgfxMode = bgfx::ViewMode::DepthDescending; break;
    default:                        bgfxMode = bgfx::ViewMode::Default; break;
    }
    bgfx::setViewMode((bgfx::ViewId)View, bgfxMode);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::SetViewOrder(CKRenderView Start, CKWORD Count,
                                               const CKRenderView *Order)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    bgfx::setViewOrder((bgfx::ViewId)Start, Count, (const bgfx::ViewId *)Order);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::ResetView(CKRenderView View)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    bgfx::resetView((bgfx::ViewId)View);
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::TouchView(CKRenderView View)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;
    bgfx::touch((bgfx::ViewId)View);
    return CK_OK;
}

void CKBgfxRasterizerContext::SetAntialias(CKDWORD Samples)
{
    const CKDWORD clamped = Samples >= 16 ? 16 : Samples >= 8 ? 8 : Samples >= 4 ? 4 : Samples >= 2 ? 2 : 0;
    if (m_AntialiasSamples == clamped)
        return;

    m_AntialiasSamples = clamped;
    m_ResetFlags = CKBgfxBuildResetFlags(m_VSync, m_AntialiasSamples);
    if (m_BgfxInitialized)
        bgfx::reset((uint32_t)m_Width, (uint32_t)m_Height, m_ResetFlags);
}

// ---------------------------------------------------------------------------
// Transform cache
// ---------------------------------------------------------------------------

CKDWORD CKBgfxRasterizerContext::AllocTransform(VxMatrix *Transform, CKDWORD Count)
{
    if (!Transform || Count == 0)
        return CKRST_INVALID_TRANSFORM;

    CKDWORD expected = m_TransformCount.load(std::memory_order_relaxed);
    CKDWORD desired;
    do {
        if (expected + Count > CKRST_MAX_TRANSFORMS)
            return CKRST_INVALID_TRANSFORM;
        desired = expected + Count;
    } while (!m_TransformCount.compare_exchange_weak(
        expected, desired,
        std::memory_order_acq_rel, std::memory_order_relaxed));

    memcpy(&m_TransformCache[expected], Transform, Count * sizeof(VxMatrix));
    return expected;
}

// ---------------------------------------------------------------------------
// Transient buffers
// ---------------------------------------------------------------------------

CKBOOL CKBgfxRasterizerContext::AllocTransientVertexBuffer(
    CKTransientVertexBuffer *Buffer, CKDWORD VertexCount, CKDWORD Layout)
{
    if (!m_BgfxInitialized || !Buffer || VertexCount == 0)
        return FALSE;

    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return FALSE;

    CKDWORD slot = m_TransientVBCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= MAX_TRANSIENT_VB)
    {
        m_TransientVBCount.fetch_sub(1, std::memory_order_relaxed);
        return FALSE;
    }

    bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[slot];
    bgfx::allocTransientVertexBuffer(tvb, VertexCount, layoutRec->Layout);
    if (tvb->data == NULL)
    {
        m_TransientVBCount.fetch_sub(1, std::memory_order_relaxed);
        return FALSE;
    }

    Buffer->Data = tvb->data;
    Buffer->Size = tvb->size;
    Buffer->StartVertex = tvb->startVertex;
    Buffer->VertexCount = VertexCount;
    Buffer->Stride = layoutRec->Layout.m_stride;
    Buffer->Layout = Layout;

    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::AllocTransientIndexBuffer(
    CKTransientIndexBuffer *Buffer, CKDWORD IndexCount, CKBOOL Index32)
{
    if (!m_BgfxInitialized || !Buffer || IndexCount == 0)
        return FALSE;

    CKDWORD slot = m_TransientIBCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= MAX_TRANSIENT_IB)
    {
        m_TransientIBCount.fetch_sub(1, std::memory_order_relaxed);
        return FALSE;
    }

    bgfx::TransientIndexBuffer *tib = &m_TransientIBPool[slot];
    bgfx::allocTransientIndexBuffer(tib, IndexCount, Index32 ? true : false);
    if (tib->data == NULL)
    {
        m_TransientIBCount.fetch_sub(1, std::memory_order_relaxed);
        return FALSE;
    }

    Buffer->Data = tib->data;
    Buffer->Size = tib->size;
    Buffer->StartIndex = tib->startIndex;
    Buffer->IndexCount = IndexCount;
    Buffer->Index32 = Index32;

    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::AllocTransientInstanceBuffer(
    CKTransientInstanceBuffer *Buffer, CKDWORD InstanceCount, CKDWORD Layout)
{
    if (!m_BgfxInitialized || !Buffer || InstanceCount == 0)
        return FALSE;

    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return FALSE;

    CKDWORD slot = m_TransientInstCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= MAX_TRANSIENT_INST)
    {
        m_TransientInstCount.fetch_sub(1, std::memory_order_relaxed);
        return FALSE;
    }

    uint16_t stride = layoutRec->Layout.m_stride;
    if (stride % 16 != 0)
        stride = (uint16_t)((stride + 15) & ~15);

    bgfx::InstanceDataBuffer *idb = &m_TransientInstPool[slot];
    bgfx::allocInstanceDataBuffer(idb, InstanceCount, stride);
    if (idb->data == NULL)
    {
        m_TransientInstCount.fetch_sub(1, std::memory_order_relaxed);
        return FALSE;
    }

    Buffer->Data = idb->data;
    Buffer->Size = idb->size;
    Buffer->StartInstance = 0;
    Buffer->InstanceCount = InstanceCount;
    Buffer->Stride = stride;
    Buffer->Layout = Layout;

    return TRUE;
}

CKDWORD CKBgfxRasterizerContext::GetAvailTransientVertexBuffer(
    CKDWORD VertexCount, CKDWORD Layout)
{
    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return 0;
    return bgfx::getAvailTransientVertexBuffer(VertexCount, layoutRec->Layout);
}

CKDWORD CKBgfxRasterizerContext::GetAvailTransientIndexBuffer(
    CKDWORD IndexCount, CKBOOL Index32)
{
    return bgfx::getAvailTransientIndexBuffer(IndexCount, Index32 ? true : false);
}

CKDWORD CKBgfxRasterizerContext::GetAvailTransientInstanceBuffer(
    CKDWORD InstanceCount, CKDWORD Layout)
{
    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return 0;
    uint16_t stride = layoutRec->Layout.m_stride;
    if (stride % 16 != 0)
        stride = (uint16_t)((stride + 15) & ~15);
    return bgfx::getAvailInstanceDataBuffer(InstanceCount, stride);
}

// ---------------------------------------------------------------------------
// Encoder and frame
// ---------------------------------------------------------------------------

CKRasterizerEncoder *CKBgfxRasterizerContext::BeginEncoder()
{
    if (!m_BgfxInitialized)
        return NULL;

    for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
    {
        CKBOOL expected = FALSE;
        if (m_Encoders[i].m_Active.compare_exchange_strong(
                expected, TRUE, std::memory_order_acq_rel, std::memory_order_relaxed))
        {
            m_Encoders[i].m_Context = this;
            m_Encoders[i].m_Encoder = bgfx::begin(true);
            if (!m_Encoders[i].m_Encoder) {
                m_Encoders[i].m_Context = NULL;
                m_Encoders[i].m_Active.store(FALSE, std::memory_order_release);
                return NULL;
            }
            m_Encoders[i].m_StencilRef = 0;
            m_Encoders[i].m_StencilReadMask = 0xFF;
            m_Encoders[i].m_StencilWriteMask = 0xFF;
            m_Encoders[i].m_CurrentLayout = 0;
            m_Encoders[i].m_PointSize = 0;
            memset(&m_Encoders[i].m_CachedDrawState, 0, sizeof(CKDrawState));
            m_Encoders[i].m_CachedBgfxState = 0;
            return &m_Encoders[i];
        }
    }
    return NULL;
}

void CKBgfxRasterizerContext::EndEncoder(CKRasterizerEncoder *Encoder)
{
    if (!Encoder)
        return;

    CKBgfxEncoder *enc = NULL;
    for (int i = 0; i < CKRST_MAX_ENCODERS; ++i) {
        if (Encoder == &m_Encoders[i]) {
            enc = &m_Encoders[i];
            break;
        }
    }

    if (!enc)
        return;

    if (enc->m_Encoder) {
        bgfx::end(enc->m_Encoder);
        enc->m_Encoder = NULL;
    }

    enc->m_Active.store(FALSE, std::memory_order_release);
}

CKERROR CKBgfxRasterizerContext::Frame(CKRST_FRAME_SYNC_MODE SyncMode)
{
    if (!m_BgfxInitialized)
        return CKERR_INVALIDOPERATION;

    for (int i = 0; i < CKRST_MAX_ENCODERS; ++i)
    {
        if (m_Encoders[i].m_Active.load(std::memory_order_acquire))
        {
            if (m_Encoders[i].m_Encoder)
            {
                bgfx::end(m_Encoders[i].m_Encoder);
                m_Encoders[i].m_Encoder = NULL;
            }
            m_Encoders[i].m_Active.store(FALSE, std::memory_order_release);
        }
    }

    const CKBOOL updatePresentSync = SyncMode != CKRST_FRAME_SYNC_PRESERVE_PRESENT;
    const CKBOOL vsync = SyncMode == CKRST_FRAME_SYNC_VSYNC;
    if (updatePresentSync && vsync != m_VSync)
    {
        m_VSync = vsync;
        m_ResetFlags = CKBgfxBuildResetFlags(vsync, m_AntialiasSamples);
        bgfx::reset((uint32_t)m_Width, (uint32_t)m_Height, m_ResetFlags);
    }

    static int s_PresentSyncLogCount = 0;
    if (CKBgfxDebugSettings().Log.PresentSync && s_PresentSyncLogCount < 64) {
        CKBgfxLogf("PresentSync",
                 "frame=%u syncMode=%d currentVSync=%d resetFlags=0x%X",
                 m_DebugFrameId, SyncMode,
                 m_VSync ? 1 : 0, m_ResetFlags);
        ++s_PresentSyncLogCount;
    }

    DrawDebugOverlay();

    bgfx::frame();
    ++m_DebugFrameId;

    m_TransformCount.store(0, std::memory_order_relaxed);
    m_TransientVBCount.store(0, std::memory_order_relaxed);
    m_TransientIBCount.store(0, std::memory_order_relaxed);
    m_TransientInstCount.store(0, std::memory_order_relaxed);

    return CK_OK;
}

// ===========================================================================
// Resource creation - occlusion queries, indirect buffers
// ===========================================================================

CKERROR CKBgfxRasterizerContext::CreateOcclusionQuery(CKDWORD Query,
                                                       CKOcclusionQueryDesc *Desc)
{
    if (!m_BgfxInitialized || Query == 0)
        return CKERR_INVALIDPARAMETER;

    bgfx::OcclusionQueryHandle handle = bgfx::createOcclusionQuery();
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxOcclusionQueryRecord();
    rec->Handle = handle;

    CKBgfxOcclusionQueryRecord *&slot = EnsureSlot(m_OcclusionQueries, Query);
    DestroyRecord(slot);
    slot = rec;
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateIndirectBuffer(CKDWORD Buffer,
                                                       CKIndirectBufferDesc *Desc)
{
    if (!m_BgfxInitialized || !Desc || Buffer == 0)
        return CKERR_INVALIDPARAMETER;

    bgfx::IndirectBufferHandle handle = bgfx::createIndirectBuffer(Desc->MaxCommands);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxIndirectBufferRecord();
    rec->Handle = handle;
    rec->MaxCommands = Desc->MaxCommands;

    CKBgfxIndirectBufferRecord *&slot = EnsureSlot(m_IndirectBuffers, Buffer);
    DestroyRecord(slot);
    slot = rec;
    return CK_OK;
}

// ===========================================================================
// Occlusion query results
// ===========================================================================

CK_OCCLUSION_RESULT CKBgfxRasterizerContext::GetOcclusionResult(CKDWORD Query,
                                                                 CKDWORD *PixelCount)
{
    CKBgfxOcclusionQueryRecord *oq = GetOcclusionQuery(Query);
    if (!oq)
        return CKRST_OCCLUSION_NORESULT;

    int32_t numPixels = 0;
    bgfx::OcclusionQueryResult::Enum result = bgfx::getResult(oq->Handle, &numPixels);
    if (PixelCount)
        *PixelCount = (CKDWORD)numPixels;

    switch (result)
    {
    case bgfx::OcclusionQueryResult::Invisible: return CKRST_OCCLUSION_INVISIBLE;
    case bgfx::OcclusionQueryResult::Visible:   return CKRST_OCCLUSION_VISIBLE;
    default:                                     return CKRST_OCCLUSION_NORESULT;
    }
}

// ===========================================================================
// Palette, debug text, debug flags
// ===========================================================================

void CKBgfxRasterizerContext::SetPaletteColor(CKDWORD Index, CKDWORD RGBA)
{
    bgfx::setPaletteColor((uint8_t)Index, RGBA);
}

void CKBgfxRasterizerContext::DbgTextClear(CKDWORD Color, CKBOOL Small)
{
    bgfx::dbgTextClear((uint8_t)Color, Small != FALSE);
}

void CKBgfxRasterizerContext::DbgTextPrintf(CKWORD X, CKWORD Y, CKDWORD Attr,
                                             CKSTRING Format, ...)
{
    char buf[4096];
    va_list args;
    va_start(args, Format);
    vsnprintf(buf, sizeof(buf), Format, args);
    va_end(args);
    bgfx::dbgTextPrintf(X, Y, (uint8_t)Attr, "%s", buf);
}

void CKBgfxRasterizerContext::DbgTextImage(CKWORD X, CKWORD Y,
                                            CKWORD Width, CKWORD Height,
                                            const void *Data, CKWORD Pitch)
{
    bgfx::dbgTextImage(X, Y, Width, Height, Data, Pitch);
}

void CKBgfxRasterizerContext::SetDebug(CKDWORD Flags)
{
    uint32_t bgfxFlags = BGFX_DEBUG_NONE;
    if (Flags & CKRST_DEBUG_WIREFRAME) bgfxFlags |= BGFX_DEBUG_WIREFRAME;
    if (Flags & CKRST_DEBUG_IFH)       bgfxFlags |= BGFX_DEBUG_IFH;
    if (Flags & CKRST_DEBUG_STATS)     bgfxFlags |= BGFX_DEBUG_STATS;
    if (Flags & CKRST_DEBUG_TEXT)      bgfxFlags |= BGFX_DEBUG_TEXT;
    if (Flags & CKRST_DEBUG_PROFILER)  bgfxFlags |= BGFX_DEBUG_PROFILER;
    bgfx::setDebug(bgfxFlags);
}

// ===========================================================================
// Statistics
// ===========================================================================

const CKRenderStats *CKBgfxRasterizerContext::GetStats()
{
    const bgfx::Stats *s = bgfx::getStats();
    if (!s)
        return &m_Stats;

    m_Stats.CpuTimeFrame = s->cpuTimeFrame;
    m_Stats.CpuTimerFreq = s->cpuTimerFreq;
    m_Stats.GpuTimeBegin = s->gpuTimeBegin;
    m_Stats.GpuTimeEnd = s->gpuTimeEnd;
    m_Stats.GpuTimerFreq = s->gpuTimerFreq;
    m_Stats.WaitRender = s->waitRender;
    m_Stats.WaitSubmit = s->waitSubmit;
    m_Stats.DrawCalls = (CKDWORD)s->numDraw;
    m_Stats.BlitCalls = (CKDWORD)s->numBlit;
    m_Stats.ComputeCalls = (CKDWORD)s->numCompute;
    m_Stats.MaxGpuLatency = (CKDWORD)s->maxGpuLatency;
    m_Stats.NumUpdatedVertexBuffers = (CKDWORD)s->numDynamicVertexBuffers;
    m_Stats.NumUpdatedIndexBuffers = (CKDWORD)s->numDynamicIndexBuffers;
    m_Stats.NumTransientVertexBuffers = (CKDWORD)s->transientVbUsed;
    m_Stats.NumTransientIndexBuffers = (CKDWORD)s->transientIbUsed;
    m_Stats.NumTransientInstanceBuffers = m_TransientInstCount.load(std::memory_order_relaxed);
    m_Stats.NumViews = (CKWORD)s->numViews;
    if (s->numViews > 0 && s->viewStats)
    {
        m_ViewStatsCache.Resize(s->numViews);
        for (uint16_t i = 0; i < s->numViews; ++i)
        {
            m_ViewStatsCache[i].Name = s->viewStats[i].name;
            m_ViewStatsCache[i].View = s->viewStats[i].view;
            m_ViewStatsCache[i].DrawCalls = 0;
            m_ViewStatsCache[i].CpuTimeBegin = s->viewStats[i].cpuTimeBegin;
            m_ViewStatsCache[i].CpuTimeEnd = s->viewStats[i].cpuTimeEnd;
            m_ViewStatsCache[i].GpuTimeBegin = s->viewStats[i].gpuTimeBegin;
            m_ViewStatsCache[i].GpuTimeEnd = s->viewStats[i].gpuTimeEnd;
        }
        m_Stats.ViewStats = &m_ViewStatsCache[0];
    }
    else
    {
        m_Stats.ViewStats = NULL;
    }
    m_Stats.GpuMemoryMax = (CKDWORD)(s->gpuMemoryMax >> 10);
    m_Stats.GpuMemoryUsed = (CKDWORD)(s->gpuMemoryUsed >> 10);
    m_Stats.Width = (CKDWORD)s->width;
    m_Stats.Height = (CKDWORD)s->height;
    m_Stats.TextWidth = (CKDWORD)s->textWidth;
    m_Stats.TextHeight = (CKDWORD)s->textHeight;

    return &m_Stats;
}

// ===========================================================================
// Resource naming
// ===========================================================================

void CKBgfxRasterizerContext::SetResourceName(CKDWORD Handle, CKDWORD Type,
                                               CKSTRING Name)
{
    if (!Name)
        return;

    int32_t len = (int32_t)strlen(Name);
    switch (Type)
    {
    case CKRST_OBJ_SHADER:
        if (CKBgfxShaderRecord *r = GetShader(Handle))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_TEXTURE:
        if (CKBgfxTextureRecord *r = GetTexture(Handle))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_FRAMEBUFFER:
        if (CKBgfxFrameBufferRecord *r = GetFrameBuffer(Handle))
            bgfx::setName(r->Handle, Name, len);
        break;
    case CKRST_OBJ_VERTEXBUFFER:
    case CKRST_OBJ_INDEXBUFFER:
    case CKRST_OBJ_PROGRAM:
    case CKRST_OBJ_UNIFORM:
    case CKRST_OBJ_VERTEXLAYOUT:
    case CKRST_OBJ_OCCLUSIONQUERY:
    case CKRST_OBJ_INDIRECTBUFFER:
        break;
    default:
        break;
    }
}

// ===========================================================================
// Shader reflection
// ===========================================================================

CKDWORD CKBgfxRasterizerContext::GetShaderUniforms(CKDWORD Shader,
                                                    CKDWORD *Uniforms,
                                                    CKDWORD MaxCount)
{
    CKBgfxShaderRecord *rec = GetShader(Shader);
    if (!rec)
        return 0;

    uint16_t count = bgfx::getShaderUniforms(rec->Handle);

    if (Uniforms && MaxCount > 0)
    {
        uint16_t toQuery = (count < (uint16_t)MaxCount) ? count : (uint16_t)MaxCount;
        bgfx::UniformHandle *buf = new bgfx::UniformHandle[toQuery];
        bgfx::getShaderUniforms(rec->Handle, buf, toQuery);
        for (uint16_t i = 0; i < toQuery; ++i)
            Uniforms[i] = buf[i].idx;
        delete[] buf;
    }
    return (CKDWORD)count;
}

void CKBgfxRasterizerContext::GetUniformInfo(CKDWORD Uniform, CKUniformInfo *Info)
{
    if (!Info)
        return;

    bgfx::UniformHandle handle = { (uint16_t)Uniform };
    bgfx::UniformInfo bgfxInfo;
    bgfx::getUniformInfo(handle, bgfxInfo);

    strncpy(Info->Name, bgfxInfo.name, sizeof(Info->Name) - 1);
    Info->Name[sizeof(Info->Name) - 1] = '\0';
    Info->Count = bgfxInfo.num;

    switch (bgfxInfo.type)
    {
    case bgfx::UniformType::Sampler: Info->Type = CKRST_UNIFORM_SAMPLER; break;
    case bgfx::UniformType::Vec4:    Info->Type = CKRST_UNIFORM_FLOAT4;  break;
    case bgfx::UniformType::Mat3:    Info->Type = CKRST_UNIFORM_FLOAT4;  break;
    case bgfx::UniformType::Mat4:    Info->Type = CKRST_UNIFORM_MATRIX4; break;
    default:                         Info->Type = CKRST_UNIFORM_FLOAT4;  break;
    }
}

// ===========================================================================
// Framebuffer queries
// ===========================================================================

CKDWORD CKBgfxRasterizerContext::GetFrameBufferTexture(CKDWORD FrameBuffer,
                                                        CKDWORD Attachment)
{
    CKBgfxFrameBufferRecord *fb = GetFrameBuffer(FrameBuffer);
    if (!fb)
        return 0;

    bgfx::TextureHandle th = bgfx::getTexture(fb->Handle, (uint8_t)Attachment);
    if (!bgfx::isValid(th))
        return 0;

    for (int i = 0; i < m_Textures.Size(); ++i)
    {
        if (m_Textures[i] && m_Textures[i]->Handle.idx == th.idx)
            return (CKDWORD)i;
    }
    return 0;
}

// ===========================================================================
// Resource validation
// ===========================================================================

CKBOOL CKBgfxRasterizerContext::IsTextureValid(CKDWORD Depth, CKBOOL CubeMap,
                                                CKWORD NumLayers, CKDWORD Format,
                                                CKDWORD Flags)
{
    uint64_t bgfxFlags = 0;
    if (Flags & CKRST_TEXTURE_RENDERTARGET)  bgfxFlags |= BGFX_TEXTURE_RT;
    if (Flags & CKRST_TEXTURE_COMPUTE_WRITE) bgfxFlags |= BGFX_TEXTURE_COMPUTE_WRITE;
    if (Flags & CKRST_TEXTURE_READBACK)      bgfxFlags |= BGFX_TEXTURE_READ_BACK;

    return bgfx::isTextureValid((uint16_t)Depth, CubeMap != FALSE, NumLayers,
                                (bgfx::TextureFormat::Enum)Format, bgfxFlags)
           ? TRUE : FALSE;
}

CKBOOL CKBgfxRasterizerContext::IsFrameBufferValid(CKDWORD ColorCount,
                                                    const CKFrameBufferAttachmentDesc *Color,
                                                    const CKFrameBufferAttachmentDesc *DepthStencil)
{
    static const CKDWORD MAX_ATTACHMENTS = 16;
    bgfx::Attachment attachments[MAX_ATTACHMENTS];
    CKDWORD count = 0;

    for (CKDWORD i = 0; i < ColorCount && count < MAX_ATTACHMENTS; ++i)
    {
        CKBgfxTextureRecord *tex = GetTexture(Color[i].Texture);
        if (!tex) return FALSE;
        attachments[count].init(tex->Handle, bgfx::Access::Write,
                                (uint16_t)Color[i].Layer, 1, (uint16_t)Color[i].Mip);
        ++count;
    }

    if (DepthStencil && DepthStencil->Texture != 0)
    {
        CKBgfxTextureRecord *tex = GetTexture(DepthStencil->Texture);
        if (!tex) return FALSE;
        if (count < MAX_ATTACHMENTS)
        {
            attachments[count].init(tex->Handle, bgfx::Access::Write,
                                    (uint16_t)DepthStencil->Layer, 1,
                                    (uint16_t)DepthStencil->Mip);
            ++count;
        }
    }

    return bgfx::isFrameBufferValid((uint8_t)count, attachments) ? TRUE : FALSE;
}

// ===========================================================================
// Texture info
// ===========================================================================

void CKBgfxRasterizerContext::CalcTextureSize(CKTextureInfo *Info,
                                               CKWORD Width, CKWORD Height,
                                               CKWORD Depth, CKBOOL CubeMap,
                                               CKBOOL HasMips, CKWORD NumLayers,
                                               CKDWORD Format)
{
    if (!Info)
        return;

    bgfx::TextureInfo bgfxInfo;
    bgfx::calcTextureSize(bgfxInfo, Width, Height, Depth, CubeMap != FALSE,
                          HasMips != FALSE, NumLayers,
                          (bgfx::TextureFormat::Enum)Format);

    Info->Format = Format;
    Info->StorageSize = bgfxInfo.storageSize;
    Info->Width = bgfxInfo.width;
    Info->Height = bgfxInfo.height;
    Info->Depth = bgfxInfo.depth;
    Info->NumMips = bgfxInfo.numMips;
    Info->BitsPerPixel = bgfxInfo.bitsPerPixel;
    Info->CubeMap = bgfxInfo.cubeMap ? TRUE : FALSE;
}

// ===========================================================================
// Screenshot capture
// ===========================================================================

void CKBgfxRasterizerContext::RequestScreenShot(CKDWORD FrameBuffer,
                                                 CKScreenShotCallback Callback)
{
    if (!Callback)
        return;

    {
        std::lock_guard<std::mutex> lock(m_ScreenShotMutex);
        m_PendingScreenShotCallback = Callback;
        m_PendingScreenShotFB = FrameBuffer;
    }

    CKBgfxFrameBufferRecord *fb = (FrameBuffer != 0) ? GetFrameBuffer(FrameBuffer) : NULL;
    bgfx::FrameBufferHandle fbHandle = BGFX_INVALID_HANDLE;
    if (fb)
        fbHandle = fb->Handle;
    bgfx::requestScreenShot(fbHandle, "screenshot");
}

// ===========================================================================
// Resource accessors
// ===========================================================================

CKBgfxOcclusionQueryRecord *CKBgfxRasterizerContext::GetOcclusionQuery(CKDWORD Handle)
{
    return GetSlot(m_OcclusionQueries, Handle);
}

CKBgfxIndirectBufferRecord *CKBgfxRasterizerContext::GetIndirectBuffer(CKDWORD Handle)
{
    return GetSlot(m_IndirectBuffers, Handle);
}
