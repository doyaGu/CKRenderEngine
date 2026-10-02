// CKBgfxRasterizerContext pass, draw, blit and command encoding.

#include "CKBgfxRasterizerContext.h"
#include "CKBgfxResources.h"
#include "CKBgfxInternal.h"

#include <algorithm>
#include <stdint.h>
#include <string.h>

static void CKBgfxCopyDebugText(char *Dst, CKDWORD DstSize, CKSTRING Src)
{
    if (!Dst || DstSize == 0)
        return;
    if (!Src)
        Src = (CKSTRING)"";
    strncpy(Dst, Src, DstSize - 1);
    Dst[DstSize - 1] = '\0';
}

static void CKBgfxDestroyDefaultTexture(bgfx::TextureHandle *handle)
{
    if (!handle)
        return;
    if (bgfx::isValid(*handle))
        bgfx::destroy(*handle);
    delete handle;
}

void CKBgfxPackSamplerMetadata(const CKSamplerDesc &sampler,
                               float borderColor[4], float samplerState[4])
{
    borderColor[0] = float((sampler.BorderColor >> 16) & 255) / 255.0f;
    borderColor[1] = float((sampler.BorderColor >> 8) & 255) / 255.0f;
    borderColor[2] = float(sampler.BorderColor & 255) / 255.0f;
    borderColor[3] = float(sampler.BorderColor >> 24) / 255.0f;
    samplerState[0] = float(unsigned(sampler.AddressU) |
                            (unsigned(sampler.AddressV) << 4) |
                            (unsigned(sampler.AddressW) << 8));
    samplerState[1] = float(sampler.MinFilter);
    samplerState[2] = float(sampler.MagFilter);
    CKDWORD mipAndAnisotropy = unsigned(sampler.MipFilter);
    if (sampler.ShaderAnisotropy)
        mipAndAnisotropy |= sampler.MaxAnisotropy << 4;
    samplerState[3] = float(mipAndAnisotropy);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

CKERROR CKBgfxRasterizerContext::BeginPass(const CKRenderPassDesc *Desc)
{
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    const CKERROR status = GetDeviceStatus();
    if (status != CK_OK)
        return status;
    CKRECT rect = Desc->Rect;
    if (rect.left < 0 || rect.top < 0 || rect.right <= rect.left || rect.bottom <= rect.top ||
        rect.right > UINT16_MAX || rect.bottom > UINT16_MAX)
        return CKERR_INVALIDPARAMETER;
    if (!Desc->RenderTarget) rect = WindowPixelRect(rect);
    if (rect.right > UINT16_MAX || rect.bottom > UINT16_MAX)
        return CKERR_INVALIDPARAMETER;
    if ((Desc->ClearFlags & ~(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL)) != 0 ||
        Desc->ClearStencil > 0xff || !(Desc->ClearZ >= 0.0f && Desc->ClearZ <= 1.0f))
        return CKERR_INVALIDPARAMETER;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    CKDWORD attachmentWidth = m_DrawableWidth;
    CKDWORD attachmentHeight = m_DrawableHeight;
    if (Desc->RenderTarget != 0) {
        CKBgfxFrameBufferRecord *rec = GetFrameBuffer(Desc->RenderTarget);
        if (!rec)
            return CKERR_INVALIDPARAMETER;
        frameBuffer = rec->Handle;
        const CKBgfxTextureRecord *color = GetTexture(rec->Desc.ColorTexture);
        if (!color) return CKERR_INVALIDPARAMETER;
        attachmentWidth = (std::max)(CKDWORD(1), CKDWORD(color->Width >> rec->Desc.ColorMip));
        attachmentHeight = (std::max)(CKDWORD(1), CKDWORD(color->Height >> rec->Desc.ColorMip));
    }

    if (m_NextView >= m_CapsDesc.MaxRenderViews)
        return CKERR_OUTOFMEMORY;
    // D3D11's native partial clear uses an internal draw. Keep independent
    // attachment write masks under our control without patching bgfx.
    const bool drawClear = CKBGFX_SHADER_DX11 && m_RendererType == bgfx::RendererType::Direct3D11 && Desc->ClearFlags &&
        (rect.left != 0 || rect.top != 0 || CKDWORD(rect.right) != attachmentWidth ||
         CKDWORD(rect.bottom) != attachmentHeight);
    if (drawClear) {
        const CKERROR prepared = PrepareRectClear();
        if (prepared != CK_OK) return prepared;
    }
    const bgfx::ViewId view = (bgfx::ViewId)m_NextView++;
    const CKDWORD clearFlags = Desc->ClearFlags;
    m_FrameInProgress = TRUE;

    bgfx::setViewMode((bgfx::ViewId)view, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer((bgfx::ViewId)view, frameBuffer);
    bgfx::setViewRect((bgfx::ViewId)view,
                      (uint16_t)rect.left, (uint16_t)rect.top,
                      (uint16_t)(rect.right - rect.left),
                      (uint16_t)(rect.bottom - rect.top));
    uint16_t bgfxClearFlags = 0;
    if (clearFlags & CKRST_CTXCLEAR_COLOR)   bgfxClearFlags |= BGFX_CLEAR_COLOR;
    if (clearFlags & CKRST_CTXCLEAR_DEPTH)   bgfxClearFlags |= BGFX_CLEAR_DEPTH;
    if (clearFlags & CKRST_CTXCLEAR_STENCIL) bgfxClearFlags |= BGFX_CLEAR_STENCIL;
    const CKDWORD a = (Desc->ClearColor >> 24) & 0xFF;
    const CKDWORD r = (Desc->ClearColor >> 16) & 0xFF;
    const CKDWORD g = (Desc->ClearColor >> 8) & 0xFF;
    const CKDWORD b = (Desc->ClearColor >> 0) & 0xFF;
    bgfx::setViewClear((bgfx::ViewId)view, drawClear ? BGFX_CLEAR_NONE : bgfxClearFlags, (r << 24) | (g << 16) | (b << 8) | a,
                       Desc->ClearZ, (uint8_t)Desc->ClearStencil);
    if (drawClear) EncodeRectClear(view, *Desc);
    if ((m_DebugFlags & CKRST_DEBUG_DRAWMAP) != 0 && Desc->Name) {
        bgfx::setViewName((bgfx::ViewId)view, Desc->Name);
        CKBgfxCopyDebugText(m_DebugViewName[view], sizeof(m_DebugViewName[view]), (CKSTRING)Desc->Name);
        if (CKBgfxDrawMapChannelEnabled(m_DrawMapFlags, CKRST_DEBUG_DRAWMAP_PASSES))
            CKBgfxLogf("ViewMap", "frame=%u view=%u name=%s target=%u rect=%d,%d-%d,%d clear=0x%X",
                       m_DebugFrameId, (unsigned)view, m_DebugViewName[view], Desc->RenderTarget,
                       rect.left, rect.top, rect.right, rect.bottom, clearFlags);
    }
    {
        VxMutexLock lock(m_ResourceStateMutex);
        m_ViewFrameBuffer[view] = Desc->RenderTarget;
        m_ViewRect[view] = rect;
        m_ViewClearFlags[view] = clearFlags;
        m_ViewClearRecorded[view] = FALSE;
    }
    RecordViewColorWrite(view, FALSE);
    m_LogicalPass = *Desc;
    m_LogicalPass.Name = nullptr;
    m_DrawPassNeedsResume = false;
    bgfx::touch((bgfx::ViewId)view);

    m_CurrentView = view;
    m_PassOpen = TRUE;
    ++m_BgfxFramePasses;
    return CK_OK;
}

CKBOOL CKBgfxRasterizerContext::AllocTransientVertices(CKDWORD Count, CKDWORD Layout, CKTransientVertexData *Out)
{
    if (!Out || Count == 0 || !m_BgfxInitialized || !m_BgfxCreated)
        return FALSE;
    *Out = CKTransientVertexData();
    CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Layout);
    if (!layoutRec)
        return FALSE;

    const CKDWORD available = bgfx::getAvailTransientVertexBuffer(Count, layoutRec->Layout);
    if (available < Count) {
        RecordTransientAllocMiss("vertex", Count, available);
        return FALSE;
    }
    if (m_TransientVBCount >= MAX_TRANSIENT_VB) {
        RecordTransientAllocMiss("vertex-pool", Count, MAX_TRANSIENT_VB);
        return FALSE;
    }
    bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[m_TransientVBCount];
    bgfx::allocTransientVertexBuffer(tvb, Count, layoutRec->Layout);
    if (tvb->data == NULL) {
        RecordTransientAllocMiss("vertex-alloc", Count, available);
        return FALSE;
    }
    ++m_TransientVBCount;
    Out->Data = tvb->data;
    Out->Count = Count;
    Out->Stride = layoutRec->Layout.m_stride;
    Out->Layout = Layout;
    Out->Token = m_TransientVBCount;
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::AllocTransientIndices(CKDWORD Count, CKBOOL Index32, CKTransientIndexData *Out)
{
    if (!Out || Count == 0 || !m_BgfxInitialized || !m_BgfxCreated ||
        (Index32 && (m_CapsDesc.Features & CKRST_DEVCAPS_INDEX32) == 0))
        return FALSE;
    *Out = CKTransientIndexData();

    const CKDWORD available = bgfx::getAvailTransientIndexBuffer(Count, Index32 ? true : false);
    if (available < Count) {
        RecordTransientAllocMiss("index", Count, available);
        return FALSE;
    }
    if (m_TransientIBCount >= MAX_TRANSIENT_IB) {
        RecordTransientAllocMiss("index-pool", Count, MAX_TRANSIENT_IB);
        return FALSE;
    }
    bgfx::TransientIndexBuffer *tib = &m_TransientIBPool[m_TransientIBCount];
    bgfx::allocTransientIndexBuffer(tib, Count, Index32 ? true : false);
    if (tib->data == NULL) {
        RecordTransientAllocMiss("index-alloc", Count, available);
        return FALSE;
    }
    ++m_TransientIBCount;
    Out->Data = tib->data;
    Out->Count = Count;
    Out->Index32 = Index32;
    Out->Token = m_TransientIBCount;
    return TRUE;
}

// A draw that cannot be submitted: the pending bgfx state is dropped so the
// next draw starts clean.
CKERROR CKBgfxRasterizerContext::DrawFailed(CKERROR Error, const char *Operation)
{
    bgfx::discard(BGFX_DISCARD_ALL);
    ResetDebugBindings();
    if (m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    if (m_DrawErrorLogCount < 16) {
        ++m_DrawErrorLogCount;
        CKBgfxLogf("DrawError", "frame=%u operation=%s error=0x%08X",
                   m_DebugFrameId, Operation ? Operation : "unknown", (unsigned)Error);
    }
    return Error;
}

static void ApplyStencil(CKDrawState state, CKDWORD ref, CKDWORD readMask, CKDWORD writeMask)
{
    const uint32_t fstencil = CKBgfxBuildFrontStencil(state, ref, readMask, writeMask);
    const uint32_t bstencil = CKBgfxBuildBackStencil(state, ref, readMask, writeMask);
    bgfx::setStencil(fstencil, bstencil);
}

CKERROR CKBgfxRasterizerContext::ApplyPipelineState(const CKFFPipelineState &state)
{
    uint64_t bgfxState = 0;
    const CKERROR stateError = CKBgfxTryState(state.State, bgfxState);
    if (stateError != CK_OK)
        return stateError;
    if (!(state.PointSize >= 0.0f && state.PointSize <= 15.0f))
        return CKERR_INVALIDPARAMETER;
    const CKDWORD stencilRef = state.StencilRef & 0xFF;
    const CKDWORD stencilReadMask = state.StencilReadMask & 0xFF;
    const CKDWORD stencilWriteMask = state.StencilWriteMask & 0xFF;
    m_PointSize = (CKDWORD)(state.PointSize + 0.5f);
    m_CachedDrawState = state.State;
    m_CachedBgfxState = bgfxState;
    uint64_t finalState = bgfxState;
    if (m_PointSize > 0)
        finalState |= BGFX_STATE_POINT_SIZE(m_PointSize);
    bgfx::setState(finalState);
    ApplyStencil(state.State, stencilRef, stencilReadMask, stencilWriteMask);

    if (state.ScissorEnabled) {
        if (state.Scissor.left < 0 || state.Scissor.top < 0 ||
            state.Scissor.right < state.Scissor.left || state.Scissor.bottom < state.Scissor.top)
            return CKERR_INVALIDPARAMETER;
        const CKRECT rect = m_LogicalPass.RenderTarget ? state.Scissor : WindowPixelRect(state.Scissor);
        if (rect.left < 0 || rect.top < 0 ||
            rect.right < rect.left || rect.bottom < rect.top ||
            rect.left > 0xffff || rect.top > 0xffff ||
            rect.right - rect.left > 0xffff ||
            rect.bottom - rect.top > 0xffff)
            return CKERR_INVALIDPARAMETER;
        bgfx::setScissor((uint16_t)rect.left, (uint16_t)rect.top,
                         (uint16_t)(rect.right - rect.left), (uint16_t)(rect.bottom - rect.top));
    } else {
        bgfx::setScissor();
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::BindGeometry(const CKDrawCommand *Draw)
{
    // Stream 0
    if (Draw->TransientVertices) {
        const CKTransientVertexData *tv = Draw->TransientVertices;
        if (tv->Token == 0 || tv->Token > m_TransientVBCount || tv->Count == 0)
            return CKERR_INVALIDPARAMETER;
        bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[tv->Token - 1];
        if (tvb->data != tv->Data || tv->Count > tvb->size / XMax((CKDWORD)1, (CKDWORD)tvb->stride))
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(0, tvb, 0, tv->Count);
        m_CurrentLayout = tv->Layout;
        if (m_DrawMapSubmitActive) {
            CKBgfxDrawMapVertexBinding &binding = m_DebugVertexBindings[0];
            binding.Buffer = 0;
            binding.Start = tvb->startVertex;
            binding.Count = tv->Count;
            binding.BgfxHandle = tvb->handle.idx;
            binding.LayoutHandle = tvb->layoutHandle.idx;
            m_DebugVertexBindingMask |= 1u;
        }
    } else if (Draw->VertexBuffer) {
        CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Draw->VertexBuffer);
        CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Draw->Layout);
        if (!rec || !layoutRec ||
            Draw->VertexCount == 0 || Draw->StartVertex > rec->VertexCount ||
            Draw->VertexCount > rec->VertexCount - Draw->StartVertex)
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(0, rec->Handle, Draw->StartVertex, Draw->VertexCount, layoutRec->Handle);
        m_CurrentLayout = Draw->Layout;
        if (m_DrawMapSubmitActive) {
            m_DebugVertexBindings[0].Buffer = Draw->VertexBuffer;
            m_DebugVertexBindings[0].Start = Draw->StartVertex;
            m_DebugVertexBindings[0].Count = Draw->VertexCount;
            m_DebugVertexBindings[0].BgfxHandle = rec->Handle.idx;
            m_DebugVertexBindings[0].LayoutHandle = layoutRec->Handle.idx;
            m_DebugVertexBindingMask |= 1u;
        }
    } else {
        return CKERR_INVALIDPARAMETER;
    }

    // Stream 1 (vertex tweening)
    if (Draw->Stream1Transient) {
        const CKTransientVertexData *tv = Draw->Stream1Transient;
        if (tv->Token == 0 || tv->Token > m_TransientVBCount || tv->Count == 0 || m_CapsDesc.MaxVertexStreams < 2)
            return CKERR_INVALIDPARAMETER;
        bgfx::TransientVertexBuffer *tvb = &m_TransientVBPool[tv->Token - 1];
        if (tvb->data != tv->Data)
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(1, tvb, 0, tv->Count);
    } else if (Draw->Stream1VertexBuffer) {
        CKBgfxVertexBufferRecord *rec = GetVertexBuffer(Draw->Stream1VertexBuffer);
        CKBgfxVertexLayoutRecord *layoutRec = GetVertexLayout(Draw->Stream1Layout);
        if (!rec || !layoutRec ||
            m_CapsDesc.MaxVertexStreams < 2 || Draw->Stream1StartVertex > rec->VertexCount ||
            Draw->VertexCount > rec->VertexCount - Draw->Stream1StartVertex)
            return CKERR_INVALIDPARAMETER;
        bgfx::setVertexBuffer(1, rec->Handle, Draw->Stream1StartVertex, Draw->VertexCount, layoutRec->Handle);
    }

    // Indices
    if (Draw->TransientIndices) {
        const CKTransientIndexData *ti = Draw->TransientIndices;
        if (ti->Token == 0 || ti->Token > m_TransientIBCount || ti->Count == 0)
            return CKERR_INVALIDPARAMETER;
        bgfx::TransientIndexBuffer *tib = &m_TransientIBPool[ti->Token - 1];
        const CKDWORD indexSize = tib->isIndex16 ? 2u : 4u;
        if (tib->data != ti->Data || ti->Count > tib->size / indexSize)
            return CKERR_INVALIDPARAMETER;
        bgfx::setIndexBuffer(tib, 0, ti->Count);
        if (m_DrawMapSubmitActive) {
            m_DebugIndexBuffer = 0;
            m_DebugIndexStart = tib->startIndex;
            m_DebugIndexCount = ti->Count;
            m_DebugIndexHandle = tib->handle.idx;
        }
    } else if (Draw->IndexBuffer) {
        CKBgfxIndexBufferRecord *rec = GetIndexBuffer(Draw->IndexBuffer);
        if (!rec ||
            Draw->IndexCount == 0 || Draw->StartIndex > rec->IndexCount ||
            Draw->IndexCount > rec->IndexCount - Draw->StartIndex)
            return CKERR_INVALIDPARAMETER;
        bgfx::setIndexBuffer(rec->Handle, Draw->StartIndex, Draw->IndexCount);
        if (m_DrawMapSubmitActive) {
            m_DebugIndexBuffer = Draw->IndexBuffer;
            m_DebugIndexStart = Draw->StartIndex;
            m_DebugIndexCount = Draw->IndexCount;
            m_DebugIndexHandle = rec->Handle.idx;
        }
    }
    return CK_OK;
}

std::shared_ptr<bgfx::TextureHandle> CKBgfxRasterizerContext::GetDefaultTexture(const CKFFSamplerBinding &Binding)
{
    const uint64_t key = (uint64_t(Binding.Dimension) << 32) | Binding.DefaultColor;
    const std::weak_ptr<bgfx::TextureHandle> *cached =
        m_DefaultTextures.FindPtr(key);
    if (cached) {
        const std::shared_ptr<bgfx::TextureHandle> texture = cached->lock();
        if (texture) return texture;
    }
    // Programs own these leases. Reap expired keys only on a creation miss,
    // keeping arbitrary historical default colors out of the device cache.
    for (DefaultTextureTable::Iterator it = m_DefaultTextures.Begin();
         it != m_DefaultTextures.End();) {
        if ((*it).expired()) it = m_DefaultTextures.Remove(it);
        else ++it;
    }
    auto texture = std::shared_ptr<bgfx::TextureHandle>(
        new bgfx::TextureHandle{bgfx::kInvalidHandle},
        CKBgfxDestroyDefaultTexture);
    const uint32_t pixels[6] = {Binding.DefaultColor, Binding.DefaultColor, Binding.DefaultColor,
                               Binding.DefaultColor, Binding.DefaultColor, Binding.DefaultColor};
    const auto *data = bgfx::copy(pixels, Binding.Dimension == CKFF_TEXTURE_CUBE ? sizeof(pixels) : sizeof(pixels[0]));
    switch (Binding.Dimension) {
    case CKFF_TEXTURE_2D:
        *texture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::BGRA8, 0, data);
        break;
    case CKFF_TEXTURE_CUBE:
        *texture = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::BGRA8, 0, data);
        break;
    case CKFF_TEXTURE_3D:
        *texture = bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::BGRA8, 0, data);
        break;
    }
    if (!bgfx::isValid(*texture)) return {};
    m_DefaultTextures.Insert(
        key, std::weak_ptr<bgfx::TextureHandle>(texture), TRUE);
    return texture;
}

CKERROR CKBgfxRasterizerContext::BindTextureSlot(const CKFFSamplerBinding &Binding, bgfx::UniformHandle Uniform,
                                      bgfx::TextureHandle DefaultTexture, CKDWORD Texture,
                                      const CKSamplerDesc *Sampler, bool FixedFunctionBorderSampling,
                                      bool ShaderBorderSampling)
{
    static int s_SetTextureLogCount = 0;
    const CKDWORD Stage = Binding.NativeSlot, Slot = Binding.Slot;
    if (Stage >= m_CapsDesc.MaxTextureBindings)
        return CKERR_INVALIDPARAMETER;
    CKBgfxTextureRecord *texRec = GetTexture(Texture);
    if (Texture != 0 && !texRec)
        return CKERR_INVALIDPARAMETER;
    if (texRec && (texRec->Flags & CKRST_TEXTURE_READBACK) != 0)
        return CKERR_NOTIMPLEMENTED;
    if (texRec) {
        const auto dimension = (texRec->Flags & CKRST_TEXTURE_CUBEMAP) ? CKFF_TEXTURE_CUBE :
            ((texRec->Flags & CKRST_TEXTURE_VOLUMEMAP) && texRec->Depth > 1 ? CKFF_TEXTURE_3D : CKFF_TEXTURE_2D);
        if (dimension != Binding.Dimension) return CKERR_INVALIDPARAMETER;
    }
    CKSamplerDesc nativeSampler;
    if (Sampler) {
        nativeSampler = *Sampler;
        nativeSampler.BorderColor = 0;
        // The shared fixed-function fragment shader reads depth values and
        // evaluates comparison/filtering itself. Keep its native sampler
        // ordinary; custom programs still receive hardware comparison state.
        if (FixedFunctionBorderSampling)
            nativeSampler.CompareFunc = CKRST_COMPARE_NONE;
        const bool hasBorder = Sampler->AddressU == CKRST_ADDRESS_BORDER ||
            Sampler->AddressV == CKRST_ADDRESS_BORDER ||
            Sampler->AddressW == CKRST_ADDRESS_BORDER;
        const bool manualBorder = hasBorder && ShaderBorderSampling;
        if (manualBorder) {
            if (nativeSampler.AddressU == CKRST_ADDRESS_BORDER)
                nativeSampler.AddressU = CKRST_ADDRESS_CLAMP;
            if (nativeSampler.AddressV == CKRST_ADDRESS_BORDER)
                nativeSampler.AddressV = CKRST_ADDRESS_CLAMP;
            if (nativeSampler.AddressW == CKRST_ADDRESS_BORDER)
                nativeSampler.AddressW = CKRST_ADDRESS_CLAMP;
            // Explicit shader levels need a linear clamp sample. The shader
            // snaps nearest lookups to texel centers before applying the
            // original min/mag and mip filter choices.
            if (FixedFunctionBorderSampling &&
                (Sampler->MipFilter != CKRST_FILTER_NONE || Sampler->ShaderAnisotropy)) {
                nativeSampler.MinFilter = CKRST_FILTER_LINEAR;
                nativeSampler.MagFilter = CKRST_FILTER_LINEAR;
            }
        } else if (hasBorder) {
            const auto entry = m_BorderPalette.Resolve(Sampler->BorderColor);
            if (!entry.Available)
                return CKERR_NOTIMPLEMENTED;
            nativeSampler.BorderColor = entry.Index;
            if (entry.Added) {
                const CKDWORD argb = Sampler->BorderColor;
                bgfx::setPaletteColor((uint8_t)entry.Index, (argb << 8) | (argb >> 24));
            }
        }
    }
    uint32_t flags = BGFX_SAMPLER_NONE;
    if (!CKBgfxTrySamplerFlags(Sampler ? &nativeSampler : nullptr, flags))
        return CKERR_INVALIDPARAMETER;
    if (Sampler && Sampler->CompareFunc != CKRST_COMPARE_NONE &&
        !FixedFunctionBorderSampling) {
        if (!texRec ||
            (m_CapsDesc.Features & CKRST_DEVCAPS_TEXTURE_COMPARISON) == 0 ||
            (CKBgfxMapFormatCaps(m_NativeFormatCaps[texRec->Format], FALSE, TRUE) &
             CKRST_FORMAT_CAPS_TEXTURE_COMPARE) == 0)
            return CKERR_NOTIMPLEMENTED;
    }
    bgfx::TextureHandle textureHandle = texRec ? texRec->Handle : DefaultTexture;
    bool usingSamplerBase = false;
    if (texRec && !CKBgfxSamplerWantsMipMaps(Sampler) && texRec->MipCount > 1) {
        if (!texRec->SamplerBaseValid || !bgfx::isValid(texRec->SamplerBaseHandle))
            return CKERR_NOTIMPLEMENTED;
        textureHandle = texRec->SamplerBaseHandle;
        usingSamplerBase = true;
    }
    if (m_DebugLogTextureBindings && s_SetTextureLogCount < 80) {
        CKBgfxLogf("BindTexture",
                   "stage=%u slot=%u texture=%u tex=%p texIdx=%u base=%u size=%ux%u fmt=%d sampler=%p",
                   Stage, Slot, Texture, (void *)texRec,
                   bgfx::isValid(textureHandle) ? textureHandle.idx : 0xffff,
                   usingSamplerBase ? 1u : 0u,
                   texRec ? texRec->Width : 0,
                   texRec ? texRec->Height : 0,
                   texRec ? (int)texRec->Format : -1,
                   (const void *)Sampler);
        s_SetTextureLogCount++;
    }
    if (!bgfx::isValid(textureHandle))
        return CKERR_INVALIDPARAMETER;
    bgfx::setTexture((uint8_t)Stage, Uniform, textureHandle, flags);

    if (m_DrawMapSubmitActive && Slot < CKFF_TEXTURE_SLOT_COUNT) {
        m_DebugTextureBindings[Slot].Texture = Texture;
        m_DebugTextureBindings[Slot].Uniform = Slot;
        m_DebugTextureBindings[Slot].BgfxHandle = textureHandle.idx;
        m_DebugTextureBindings[Slot].SamplerFlags = flags;
        m_DebugTextureBindingMask |= (1u << Slot);
    }
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::Draw(const CKDrawCommand *Draw)
{
    m_DrawApproximations = 0;
    if (m_DrawPassNeedsResume) {
        CKRenderPassDesc resume = m_LogicalPass;
        resume.ClearFlags = 0;
        resume.Name = "resume";
        const CKERROR status = BeginPass(&resume);
        if (status != CK_OK) return status;
    }
    if (!Draw || !Draw->Program)
        return CKERR_INVALIDPARAMETER;
    if (!IsReady() || !m_PassOpen)
        return CKERR_INVALIDOPERATION;
    const CKERROR status = GetDeviceStatus();
    if (status != CK_OK)
        return DrawFailed(status, "Draw");

    CKBgfxProgramRecord *rec = GetProgram(Draw->Program);
    if (!rec || !bgfx::isValid(rec->Handle) || rec->PixelShader == 0) {
        RecordInvalidSubmit((CKSTRING)"Draw", m_CurrentView, Draw->Program, (CKSTRING)"invalid_program");
        return DrawFailed(CKERR_INVALIDPARAMETER, "Draw.program");
    }

    CKERROR err = ApplyPipelineState(Draw->Pipeline);
    if (err != CK_OK)
        return DrawFailed(err, "Draw.state");
    if (rec->Interface.VertexInputs.Size() == 0) {
        if (!Draw->VertexCount || Draw->StartVertex || Draw->IndexCount || Draw->IndexBuffer || Draw->TransientIndices)
            return DrawFailed(CKERR_INVALIDPARAMETER, "Draw.procedural");
        bgfx::setVertexCount(Draw->VertexCount);
        m_CurrentLayout = 0;
        err = CK_OK;
    } else err = BindGeometry(Draw);
    if (err != CK_OK)
        return DrawFailed(err, "Draw.geometry");

    float borderSamplers[CKFF_SAMPLER_SLOT_COUNT][4] = {};
    for (int i = 0; i < rec->Samplers.Size(); ++i) {
        const CKBgfxProgramRecord::SamplerBinding &sampler = rec->Samplers[i];
        const CKFFTextureSlot binding = Draw->Textures ? (*Draw->Textures)[sampler.Desc.Slot] : CKFFTextureSlot();
        if (sampler.Desc.NativeSlot < CKFF_SAMPLER_SLOT_COUNT) {
            const CKBgfxTextureRecord *texture = GetTexture(binding.Texture);
            borderSamplers[sampler.Desc.NativeSlot][0] = float(texture ? texture->MipCount : 1u);
            borderSamplers[sampler.Desc.NativeSlot][1] = float(binding.Sampler.MipFilter);
        }
        const bool namedBorderSampling =
            bgfx::isValid(sampler.BorderColorHandle) &&
            bgfx::isValid(sampler.SamplerStateHandle);
        err = BindTextureSlot(sampler.Desc, sampler.Handle, *sampler.DefaultTexture,
            binding.Texture, &binding.Sampler, rec->FixedFunctionBorderSampling,
            rec->FixedFunctionBorderSampling || namedBorderSampling);
        if (err != CK_OK)
            return DrawFailed(err, "Draw.texture");
        if (namedBorderSampling) {
            float borderColor[4], samplerState[4];
            CKBgfxPackSamplerMetadata(binding.Sampler, borderColor, samplerState);
            bgfx::setUniform(sampler.BorderColorHandle, borderColor);
            bgfx::setUniform(sampler.SamplerStateHandle, samplerState);
        }
    }

    // Program creation compiles names, counts and slot mappings. Keep the
    // per-draw path allocation-free while restoring bgfx encoder state.
    for (int i = 0; i < rec->Uniforms.Size(); ++i) {
        const CKBgfxProgramRecord::UniformBinding &uniform = rec->Uniforms[i];
        if (rec->FixedFunctionBorderSampling &&
            uniform.Slot == CKRST_BLOCK_BORDER_SAMPLERS &&
            uniform.Count == CKFF_SAMPLER_SLOT_COUNT) {
            bgfx::setUniform(uniform.Handle, borderSamplers, (uint16_t)uniform.Count);
            continue;
        }
        const auto *source = Draw->Constants ? &(*Draw->Constants)[uniform.Slot].Bytes : nullptr;
        auto &scratch = m_ConstantData[uniform.Slot]; // declaration-sized, allocated at program creation
        const void *bytes = source && source->Size() >= scratch.Size()
            ? source->Begin() : nullptr;
        if (!bytes) {
            memset(scratch.Begin(), 0, scratch.Size());
            if (source && !source->IsEmpty())
                memcpy(scratch.Begin(), source->Begin(), source->Size());
            bytes = scratch.Begin();
        }
        bgfx::setUniform(uniform.Handle, bytes, (uint16_t)uniform.Count);
    }
    if (Draw->Marker) {
        bgfx::setMarker(Draw->Marker);
        if (m_DrawMapMarkerCaptureActive) {
            strncpy(m_LastMarker, Draw->Marker, sizeof(m_LastMarker) - 1);
            m_LastMarker[sizeof(m_LastMarker) - 1] = '\0';
        }
    } else m_LastMarker[0] = '\0';
    if (m_DrawMapSubmitActive)
        TraceSubmit(Draw->Program, rec->Handle, Draw->SortKey, Draw->Pipeline);
    RecordViewColorWrite(m_CurrentView, TRUE);
    bgfx::submit(m_CurrentView, rec->Handle, Draw->SortKey, BGFX_DISCARD_ALL);
    if (m_DrawMapMarkerCaptureActive)
        m_LastMarker[0] = '\0';
    ResetDebugBindings();
    ++m_FrameDraws;
    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::Blit(CKDWORD DstTexture, CKDWORD DstMip, CKDWORD DstLayer, CKDWORD DstX, CKDWORD DstY,
                            CKDWORD SrcTexture, CKDWORD SrcMip, CKDWORD SrcLayer, const CKRECT *SrcRect)
{
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    const CKERROR status = GetDeviceStatus();
    if (status != CK_OK)
        return status;
    CKBgfxTextureRecord *dst = GetTexture(DstTexture);
    CKBgfxTextureRecord *src = GetTexture(SrcTexture);
    if (!dst || !src)
        return CKERR_INVALIDPARAMETER;
    if (DstMip >= dst->MipCount || SrcMip >= src->MipCount)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD dstLayers = (dst->Flags & CKRST_TEXTURE_CUBEMAP) ? 6u : XMax((CKDWORD)1, dst->Depth >> DstMip);
    const CKDWORD srcLayers = (src->Flags & CKRST_TEXTURE_CUBEMAP) ? 6u : XMax((CKDWORD)1, src->Depth >> SrcMip);
    if (DstLayer >= dstLayers || SrcLayer >= srcLayers)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD dstWidth = XMax((CKDWORD)1, dst->Width >> DstMip);
    const CKDWORD dstHeight = XMax((CKDWORD)1, dst->Height >> DstMip);
    const CKDWORD srcWidth = XMax((CKDWORD)1, src->Width >> SrcMip);
    const CKDWORD srcHeight = XMax((CKDWORD)1, src->Height >> SrcMip);
    if (DstX >= dstWidth || DstY >= dstHeight ||
        (SrcRect && (SrcRect->left < 0 || SrcRect->top < 0 ||
                     SrcRect->right <= SrcRect->left ||
                     SrcRect->bottom <= SrcRect->top ||
                     (CKDWORD)SrcRect->right > srcWidth ||
                     (CKDWORD)SrcRect->bottom > srcHeight)))
        return CKERR_INVALIDPARAMETER;
    if ((m_CapsDesc.Features & CKRST_DEVCAPS_BLIT) == 0 ||
        dst->IsDepth || src->IsDepth ||
        dst->Format != src->Format)
        return CKERR_NOTIMPLEMENTED;

    const CKDWORD srcX = SrcRect ? (CKDWORD)SrcRect->left : 0;
    const CKDWORD srcY = SrcRect ? (CKDWORD)SrcRect->top : 0;
    const CKDWORD copiedWidth = SrcRect ? (CKDWORD)(SrcRect->right - SrcRect->left) : srcWidth;
    const CKDWORD copiedHeight = SrcRect ? (CKDWORD)(SrcRect->bottom - SrcRect->top) : srcHeight;
    const CKDWORD actualCopiedWidth = XMin(copiedWidth, dstWidth - DstX);
    const CKDWORD actualCopiedHeight = XMin(copiedHeight, dstHeight - DstY);
    const CKBOOL fullDestination =
        DstX == 0 && DstY == 0 &&
        actualCopiedWidth == dstWidth && actualCopiedHeight == dstHeight;
    if (m_NextView >= m_CapsDesc.MaxRenderViews) return CKERR_OUTOFMEMORY;
    const bgfx::ViewId copyView = (bgfx::ViewId)m_NextView++;
    bgfx::resetView(copyView);
    bgfx::setViewMode(copyView, bgfx::ViewMode::Sequential);
    bgfx::setViewName(copyView, "copy");
    m_FrameInProgress = TRUE;
    m_DrawPassNeedsResume = m_PassOpen != FALSE;
    RecordTextureBlit(dst, DstMip, src, SrcMip, fullDestination);
    bgfx::blit(copyView,
               dst->Handle, (uint8_t)DstMip, (uint16_t)DstX, (uint16_t)DstY, (uint16_t)DstLayer,
               src->Handle, (uint8_t)SrcMip, (uint16_t)srcX, (uint16_t)srcY, (uint16_t)SrcLayer,
               (uint16_t)actualCopiedWidth, (uint16_t)actualCopiedHeight, 1);
    ++m_FrameBlits;
    return CK_OK;
}
