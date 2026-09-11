#include "CKRenderProfile.h"
#include "CKSdlGpuInternal.h"

namespace {

struct CKSdlGpuByteRange {
    const CKBYTE *Data = nullptr;
    size_t Size = 0;
};

bool CKSdlGpuVertexTokenBefore(const CKSdlGpuTransientVertexInfo &info, CKDWORD token)
{
    return info.Token < token;
}

bool CKSdlGpuIndexTokenBefore(const CKSdlGpuTransientIndexInfo &info, CKDWORD token)
{
    return info.Token < token;
}

bool CKSdlGpuValidateVertexGeometry(
    const CKSdlGpuLayout *layout,
    const CKSdlGpuBuffer *buffer,
    CKDWORD layoutHandle,
    const CKBackendTransientVertices *transient,
    CKDWORD start,
    CKDWORD count,
    const std::vector<CKSdlGpuTransientVertexInfo> &allocations,
    const std::vector<std::vector<CKBYTE>> &storage,
    CKSdlGpuByteRange &snapshot)
{
    if (buffer)
        return uint64_t(start) + count <= buffer->Shadow.size() / layout->Stride;
    if (!transient || !transient->Token)
        return false;
    const auto allocation = std::lower_bound(
        allocations.begin(), allocations.end(), transient->Token,
        CKSdlGpuVertexTokenBefore);
    if (allocation == allocations.end() || allocation->Token != transient->Token)
        return false;
    const size_t index = size_t(allocation - allocations.begin());
    if (index >= storage.size())
        return false;
    const auto &bytes = storage[index];
    if (allocation->Layout != layoutHandle || transient->Layout != allocation->Layout ||
        transient->Stride != allocation->Stride || transient->Stride != layout->Stride ||
        transient->Data != bytes.data() ||
        uint64_t(transient->Count) * transient->Stride != bytes.size() ||
        start > transient->Count || count > transient->Count - start)
        return false;
    snapshot.Data = bytes.data() + size_t(start) * layout->Stride;
    snapshot.Size = size_t(count) * layout->Stride;
    return true;
}

template<class Index>
bool CKSdlGpuValidateTransientIndices(const CKBYTE *bytes, CKDWORD start,
                                      CKDWORD count, CKDWORD vertexCount)
{
    const CKBYTE *first = bytes + size_t(start) * sizeof(Index);
    for (CKDWORD i = 0; i < count; ++i) {
        Index index = 0;
        std::memcpy(&index, first + size_t(i) * sizeof(Index), sizeof(Index));
        if (index >= vertexCount)
            return false;
    }
    return true;
}

uint64_t CKSdlGpuAlignedEnd(uint64_t offset, size_t size, unsigned alignment)
{
    if (!size)
        return offset;
    const uint64_t remainder = offset % alignment;
    if (remainder)
        offset += alignment - remainder;
    return offset + size;
}

void CKSdlGpuAppendBytes(std::vector<CKBYTE> &destination,
                         const CKSdlGpuByteRange &source,
                         unsigned alignment,
                         unsigned &offset)
{
    if (!source.Size)
        return;
    const size_t remainder = destination.size() % alignment;
    if (remainder)
        destination.resize(destination.size() + alignment - remainder);
    offset = unsigned(destination.size());
    destination.insert(destination.end(), source.Data, source.Data + source.Size);
}

struct CKSdlGpuVertexBindingState {
    SDL_GPUBuffer *Buffer = nullptr;
    Uint32 Offset = 0;
    bool Valid = false;
};

void CKSdlGpuBindVertexBuffer(SDL_GPURenderPass *pass,
                              CKSdlGpuVertexBindingState *states,
                              Uint32 slot,
                              const SDL_GPUBufferBinding &binding)
{
    CKSdlGpuVertexBindingState &bound = states[slot];
    if (bound.Valid && bound.Buffer == binding.buffer && bound.Offset == binding.offset)
        return;
    SDL_BindGPUVertexBuffers(pass, slot, &binding, 1);
    bound.Buffer = binding.buffer;
    bound.Offset = binding.offset;
    bound.Valid = true;
}

void CKSdlGpuTrimTransientAllocations(std::vector<std::vector<CKBYTE>> &allocations,
                                      uint64_t &retained)
{
    static constexpr uint64_t kRetentionLimit = 16u * 1024u * 1024u;
    size_t keep = 0;
    while (keep < allocations.size() &&
           retained + allocations[keep].capacity() <= kRetentionLimit) {
        retained += allocations[keep].capacity();
        ++keep;
    }
    allocations.resize(keep);
}

} // namespace

CKERROR CKSdlGpuBackend::BeginPass(const CKBackendPassDesc *desc)
{
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || desc->Rect.left < 0 || desc->Rect.top < 0 || desc->Rect.right <= desc->Rect.left ||
        desc->Rect.bottom <= desc->Rect.top) return CKERR_INVALIDPARAMETER;
    auto target = m->Targets.Get(desc->RenderTarget);
    if (desc->RenderTarget && !target) return CKERR_INVALIDPARAMETER;
    const CKERROR error = m->Flush();
    if (error != CK_OK) return error;
    m->Pass = *desc; m->Pass.Name = nullptr; m->Target = std::move(target); m->PassOpen = true;
    ++m->FrameStats.Passes;
    return CK_OK;
}

CKBOOL CKSdlGpuBackend::AllocTransientVertices(CKDWORD count, CKDWORD handle, CKBackendTransientVertices *out)
{
    if (out) *out = CKBackendTransientVertices();
    if (!m->Ready() || !out || !count || !m->NextTransientToken) return FALSE;
    auto layout = m->Layouts.Get(handle);
    if (!layout || uint64_t(count) * layout->Stride > 64u * 1024u * 1024u) return FALSE;
    const size_t index = m->TransientVertexInfo.size();
    if (index == m->TransientVertices.size()) m->TransientVertices.emplace_back();
    auto &storage = m->TransientVertices[index];
    storage.resize(size_t(count) * layout->Stride);
    std::fill(storage.begin(), storage.end(), CKBYTE(0));
    m->TransientVertexInfo.push_back({m->NextTransientToken++, handle, layout->Stride});
    out->Data = storage.data(); out->Count = count; out->Stride = layout->Stride;
    out->Layout = handle; out->Token = m->TransientVertexInfo.back().Token;
    return TRUE;
}

CKBOOL CKSdlGpuBackend::AllocTransientIndices(CKDWORD count, CKBOOL index32, CKBackendTransientIndices *out)
{
    if (out) *out = CKBackendTransientIndices();
    if (!m->Ready() || !out || !count || !m->NextTransientToken || count > 16u * 1024u * 1024u) return FALSE;
    const size_t index = m->TransientIndexInfo.size();
    if (index == m->TransientIndices.size()) m->TransientIndices.emplace_back();
    auto &storage = m->TransientIndices[index];
    storage.resize(size_t(count) * (index32 ? 4 : 2));
    std::fill(storage.begin(), storage.end(), CKBYTE(0));
    m->TransientIndexInfo.push_back({m->NextTransientToken++, index32 ? TRUE : FALSE});
    out->Data = storage.data(); out->Count = count;
    out->Index32 = m->TransientIndexInfo.back().Index32; out->Token = m->TransientIndexInfo.back().Token;
    return TRUE;
}

CKERROR CKSdlGpuBackend::Draw(const CKBackendDraw *desc)
{
    CKRE_PROFILE_SCOPE("CKRE.SDL.RecordDraw");
    if (!m->Ready() || !m->PassOpen) return CKERR_INVALIDOPERATION;
    if (!desc || !desc->VertexCount) return CKERR_INVALIDPARAMETER;
    CKSdlGpuDraw draw;
    draw.State = desc->Pipeline;
    draw.LayoutHandle = desc->Layout; draw.Layout1Handle = desc->Stream1Layout;
    draw.StartVertex = desc->StartVertex; draw.Stream1StartVertex = desc->Stream1StartVertex;
    draw.VertexCount = desc->VertexCount; draw.StartIndex = desc->StartIndex;
    draw.IndexCount = desc->IndexCount;
#if (defined(CKRE_ENABLE_PROFILING) && CKRE_ENABLE_PROFILING) || \
    (defined(CKRE_ENABLE_FFP_DIAGNOSTICS) && CKRE_ENABLE_FFP_DIAGNOSTICS)
    draw.Marker = std::make_unique<std::string>(desc->Marker ? desc->Marker : "");
#else
    if (m->DebugFlags)
        draw.Marker = std::make_unique<std::string>(desc->Marker ? desc->Marker : "");
#endif
    const auto &programOwner = m->Programs.Borrow(desc->Program);
    const auto &layoutOwner = m->Layouts.Borrow(desc->Layout);
    const auto &layout1Owner = m->Layouts.Borrow(desc->Stream1Layout);
    const auto &vbOwner = m->VertexBuffers.Borrow(desc->VertexBuffer);
    const auto &vb1Owner = m->VertexBuffers.Borrow(desc->Stream1VertexBuffer);
    const auto &ibOwner = m->IndexBuffers.Borrow(desc->IndexBuffer);
    draw.Program = programOwner.get(); draw.Layout = layoutOwner.get(); draw.Layout1 = layout1Owner.get();
    draw.VB = vbOwner.get(); draw.VB1 = vb1Owner.get(); draw.IB = ibOwner.get();
    if (!draw.Program || (desc->Stream1Layout && !draw.Layout1)) return CKERR_INVALIDPARAMETER;
    const bool procedural = draw.Program->Interface.VertexInputs.empty();
    if (procedural ? (desc->IndexCount || desc->StartVertex) : !draw.Layout) return CKERR_INVALIDPARAMETER;
    CKSdlGpuByteRange vertices, vertices1, indices;
    if (!procedural && !CKSdlGpuValidateVertexGeometry(
            draw.Layout, draw.VB, desc->Layout, desc->TransientVertices,
            desc->StartVertex, desc->VertexCount, m->TransientVertexInfo,
            m->TransientVertices, vertices))
        return CKERR_INVALIDPARAMETER;
    if (!procedural && draw.Layout1 && !CKSdlGpuValidateVertexGeometry(
            draw.Layout1, draw.VB1, desc->Stream1Layout, desc->Stream1Transient,
            desc->Stream1StartVertex, desc->VertexCount, m->TransientVertexInfo,
            m->TransientVertices, vertices1))
        return CKERR_INVALIDPARAMETER;
    if (desc->IndexCount) {
        const CKBYTE *bytes = nullptr;
        unsigned available = 0;
        if (draw.IB) {
            draw.Index32 = draw.IB->Desc.Index32 != FALSE;
            bytes = draw.IB->Shadow.data(); available = unsigned(draw.IB->Shadow.size()) / (draw.Index32 ? 4 : 2);
        } else if (desc->TransientIndices && desc->TransientIndices->Token) {
            const auto &transient = *desc->TransientIndices;
            const auto allocation = std::lower_bound(
                m->TransientIndexInfo.begin(), m->TransientIndexInfo.end(),
                transient.Token, CKSdlGpuIndexTokenBefore);
            if (allocation == m->TransientIndexInfo.end() || allocation->Token != transient.Token) return CKERR_INVALIDPARAMETER;
            const size_t index = size_t(allocation - m->TransientIndexInfo.begin());
            if (index >= m->TransientIndices.size()) return CKERR_INVALIDPARAMETER;
            const auto &storage = m->TransientIndices[index];
            if (transient.Index32 != allocation->Index32 || transient.Data != storage.data() ||
                uint64_t(transient.Count) * (transient.Index32 ? 4 : 2) != storage.size()) return CKERR_INVALIDPARAMETER;
            draw.Index32 = transient.Index32 != FALSE;
            bytes = storage.data(); available = transient.Count;
        }
        if (!bytes || desc->StartIndex > available || desc->IndexCount > available - desc->StartIndex) return CKERR_INVALIDPARAMETER;
        const unsigned size = draw.Index32 ? 4 : 2;
        if (draw.IB) {
            unsigned maximum = 0;
            if (!draw.IB->FindMaxIndex(desc->StartIndex, desc->IndexCount, draw.Index32, maximum) ||
                maximum >= desc->VertexCount) return CKERR_INVALIDPARAMETER;
        } else {
            // Transient bytes are caller-writable until Draw returns and cannot
            // share a persistent range cache.
            const bool valid = draw.Index32
                ? CKSdlGpuValidateTransientIndices<CKDWORD>(
                    bytes, desc->StartIndex, desc->IndexCount, desc->VertexCount)
                : CKSdlGpuValidateTransientIndices<CKWORD>(
                    bytes, desc->StartIndex, desc->IndexCount, desc->VertexCount);
            if (!valid)
                return CKERR_INVALIDPARAMETER;
            indices.Data = bytes + size_t(desc->StartIndex) * size;
            indices.Size = size_t(desc->IndexCount) * size;
        }
    }
    auto &uniforms = draw.Program->UniformLayout;
    static const CKBackendConstants emptyConstants;
    uniforms.Update(desc->Constants ? *desc->Constants : emptyConstants);
    CKSdlGpuBindingBatch::Inputs bindingInputs;
    bindingInputs.Hash = draw.Program->Identity;
    for (unsigned slot = 0; slot < draw.Program->Interface.Samplers.size(); ++slot) {
        const auto &decl = draw.Program->Interface.Samplers[slot];
        static const CKBackendTextureBinding emptyBinding;
        const auto &binding = desc->Textures ? (*desc->Textures)[decl.Slot] : emptyBinding;
        auto &cached = m->SamplerBindings[decl.Slot];
        if (std::memcmp(&cached.Sampler, &binding.Sampler, sizeof(binding.Sampler)) != 0) {
            cached.Sampler = binding.Sampler;
            cached.NativeSampler.reset();
        }
        if (decl.MetadataBufferSlot == ~0u && (binding.Sampler.AddressU == CKRST_ADDRESS_BORDER ||
            binding.Sampler.AddressV == CKRST_ADDRESS_BORDER || binding.Sampler.AddressW == CKRST_ADDRESS_BORDER))
            return CKERR_NOTIMPLEMENTED;
        const auto &texture = binding.Texture ? m->Textures.Borrow(binding.Texture) : draw.Program->DefaultTextures[slot];
        if (!texture || texture->Depth ||
            texture->Info.type != draw.Program->DefaultTextures[slot]->Info.type ||
            (m->Target && texture == m->Target->Color)) {
            fprintf(stderr, "SDL_gpu draw rejected: sampler=%u texture=0x%x feedback=%d marker=%s\n",
                    slot, binding.Texture, m->Target && texture == m->Target->Color,
                    draw.Marker ? draw.Marker->c_str() : "");
            return CKERR_INVALIDPARAMETER;
        }
        if (!cached.NativeSampler) cached.NativeSampler = m->Sampler(binding.Sampler);
        if (!cached.NativeSampler) return m->Error;
        bindingInputs.Textures[slot] = texture.get();
        bindingInputs.Samplers[slot] = cached.NativeSampler.get();
        bindingInputs.TextureOwners[slot] = &texture;
        bindingInputs.SamplerOwners[slot] = &cached.NativeSampler;
        bindingInputs.Hash = (bindingInputs.Hash * 16777619u) ^
            (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        bindingInputs.Hash = (bindingInputs.Hash * 16777619u) ^
            (reinterpret_cast<uintptr_t>(cached.NativeSampler.get()) >> 4);
        const CKDWORD metadata = draw.Program->SamplerMetadataOffsets[slot];
        if (metadata == ~0u) continue;
        const CKDWORD metadataBit = 1u << slot;
        if ((draw.Program->SamplerMetadataValidMask & metadataBit) != 0 &&
            std::memcmp(&draw.Program->SamplerMetadata[slot], &binding.Sampler,
                        sizeof(binding.Sampler)) == 0)
            continue;
        draw.Program->SamplerMetadata[slot] = binding.Sampler;
        draw.Program->SamplerMetadataValidMask |= metadataBit;
        float rgba[4];
        rgba[0] = float((binding.Sampler.BorderColor >> 16) & 255) / 255;
        rgba[1] = float((binding.Sampler.BorderColor >> 8) & 255) / 255;
        rgba[2] = float(binding.Sampler.BorderColor & 255) / 255;
        rgba[3] = float(binding.Sampler.BorderColor >> 24) / 255;
        float samplerInfo[4];
        samplerInfo[0] = float(unsigned(binding.Sampler.AddressU) |
                               (unsigned(binding.Sampler.AddressV) << 4) |
                               (unsigned(binding.Sampler.AddressW) << 8));
        samplerInfo[1] = float(binding.Sampler.MinFilter); samplerInfo[2] = float(binding.Sampler.MagFilter);
        samplerInfo[3] = float(binding.Sampler.MipFilter);
        std::memcpy(uniforms.Data.data() + metadata + decl.BorderColorOffset, rgba, sizeof(rgba));
        std::memcpy(uniforms.Data.data() + metadata + decl.SamplerStateOffset, samplerInfo, sizeof(samplerInfo));
        const CKDWORD first = (std::min)(decl.BorderColorOffset, decl.SamplerStateOffset);
        const CKDWORD last = (std::max)(decl.BorderColorOffset, decl.SamplerStateOffset) + sizeof(rgba);
        if (!uniforms.MarkDataChanged(metadata + first, last - first)) return CKERR_INVALIDPARAMETER;
    }
    // Snapshot directly into the batch only after validating the entire draw.
    // Offsets survive arena growth; caller-owned transient data may change as
    // soon as Draw returns. Check aligned sizes before narrowing to GPU offsets.
    if (CKSdlGpuAlignedEnd(
            CKSdlGpuAlignedEnd(m->BatchVertices.size(), vertices.Size, 4u),
            vertices1.Size, 4u) > UINT32_MAX ||
        CKSdlGpuAlignedEnd(m->BatchIndices.size(), indices.Size, 4u) > UINT32_MAX ||
        uint64_t(m->Uniforms.Data.size()) + uniforms.Data.size() > UINT32_MAX) return CKERR_OUTOFMEMORY;
    CKSdlGpuAppendBytes(m->BatchVertices, vertices, 4u, draw.VertexOffset);
    CKSdlGpuAppendBytes(m->BatchVertices, vertices1, 4u, draw.VertexOffset1);
    CKSdlGpuAppendBytes(m->BatchIndices, indices, 4u, draw.IndexOffset);
    m->Uniforms.Snapshot(uniforms, draw.Program->UniformCursor, draw.UniformOffsets);
    draw.Bindings = m->Bindings.Intern(*draw.Program, bindingInputs);
    m->DrawResources.Retain(programOwner);
    m->DrawResources.Retain(layoutOwner); m->DrawResources.Retain(layout1Owner);
    m->DrawResources.Retain(vbOwner); m->DrawResources.Retain(vb1Owner); m->DrawResources.Retain(ibOwner);
    m->DrawApproximations = 0;
    m->Draws.push_back(std::move(draw));
    ++m->FrameStats.Draws;
    // Bound packet retention independently of the number of engine draws.
    return m->Draws.size() >= 256 ? m->Flush() : CK_OK;
}

CKERROR CKSdlGpuDevice::Flush(bool presentWindow)
{
    CKRE_PROFILE_SCOPE("CKRE.SDL.Encode");
    if (Draws.empty() && (!PassOpen || !Pass.ClearFlags)) return CK_OK;
    if (!EnsureCommands()) return Error;
    if (!Target) {
        if (!presentWindow) {
            Draws.clear(); DrawResources.Clear(); Uniforms.Clear(); Bindings.Clear();
            BatchVertices.clear(); BatchIndices.clear(); Pass.ClearFlags = 0;
            return CK_OK;
        }
        const CKERROR acquired = AcquireSwapchain();
        if (acquired != CK_OK) return acquired;
        // A minimized window has no swapchain image. Resource work still submits.
        if (!Swapchain) {
            Draws.clear(); DrawResources.Clear(); Uniforms.Clear(); Bindings.Clear();
            BatchVertices.clear(); BatchIndices.clear(); Pass.ClearFlags = 0;
            return CK_OK;
        }
    }
    CKRE_PROFILE_VALUE("CKRE.Batch.Draws", Draws.size());
    CKRE_PROFILE_VALUE("CKRE.Batch.VertexBytes", BatchVertices.size());
    CKRE_PROFILE_VALUE("CKRE.Batch.IndexBytes", BatchIndices.size());
    CKRE_PROFILE_VALUE("CKRE.Batch.UniformSnapshotBytes", Uniforms.Data.size());
    CKRE_PROFILE_VALUE("CKRE.Batch.BindingGroups", Bindings.Size());
    const auto batch = UploadGeometry(BatchVertices, BatchIndices);
    if (Error != CK_OK) return Error;
    const unsigned width = Target ? std::max(1u, Target->Color->Info.width >> Target->Desc.ColorMip) : SwapWidth;
    const unsigned height = Target ? std::max(1u, Target->Color->Info.height >> Target->Desc.ColorMip) : SwapHeight;
    const bool fullRect = Pass.Rect.left == 0 && Pass.Rect.top == 0 && unsigned(Pass.Rect.right) == width && unsigned(Pass.Rect.bottom) == height;
    SDL_GPUColorTargetInfo color = {};
    color.texture = Target ? (Target->Color->Multisample ? Target->Color->Multisample.get() : Target->Color->Image.get()) : Swapchain;
    color.mip_level = Target ? Target->Desc.ColorMip : 0;
    color.layer_or_depth_plane = Target ? Target->Desc.ColorLayer : 0;
    color.clear_color = {float((Pass.ClearColor >> 16) & 255) / 255, float((Pass.ClearColor >> 8) & 255) / 255,
                         float(Pass.ClearColor & 255) / 255, float(Pass.ClearColor >> 24) / 255};
    color.load_op = fullRect && (Pass.ClearFlags & CKRST_CTXCLEAR_COLOR) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    if (Target && Target->VolumeSlice) {
        if (color.load_op == SDL_GPU_LOADOP_LOAD) {
            if (Target->Color->Defined[Target->Desc.ColorMip * Target->Color->Info.layer_count_or_depth + Target->Desc.ColorLayer]) {
                if (CopyVolumeSlice(*Target->Color, Target->Desc.ColorMip, Target->Desc.ColorLayer,
                                     Target->VolumeSlice.get(), false) != CK_OK) return Error;
            } else color.load_op = SDL_GPU_LOADOP_DONT_CARE;
        }
        color.texture = Target->VolumeSlice.get(); color.mip_level = color.layer_or_depth_plane = 0;
    }
    if (Target && Target->Color->Multisample) {
        color.resolve_texture = Target->Color->Image.get();
        color.resolve_mip_level = color.mip_level; color.resolve_layer = color.layer_or_depth_plane;
        color.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
    }
    SDL_GPUDepthStencilTargetInfo ds = {};
    const bool hasDepth = Target && Target->Depth;
    const SDL_GPUTextureFormat colorFormat = Target ? Target->Color->Info.format : SDL_GetGPUSwapchainTextureFormat(Device, Window);
    const SDL_GPUTextureFormat depthFormat = hasDepth ? Target->Depth->Info.format : SDL_GPU_TEXTUREFORMAT_INVALID;
    const auto samples = CKSdlGpuSampleCount(Target ? Target->Color->Samples : 1);
    if (hasDepth) {
        ds.texture = Target->Depth->Image.get(); ds.clear_depth = Pass.ClearZ; ds.clear_stencil = Uint8(Pass.ClearStencil);
        ds.load_op = fullRect && (Pass.ClearFlags & CKRST_CTXCLEAR_DEPTH) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        ds.stencil_load_op = fullRect && (Pass.ClearFlags & CKRST_CTXCLEAR_STENCIL) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        ds.store_op = ds.stencil_store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(Commands, &color, 1, hasDepth ? &ds : nullptr);
    if (!pass) return Fail("BeginGPURenderPass");
    SDL_GPUViewport viewport = {float(Pass.Rect.left), float(Pass.Rect.top), float(Pass.Rect.right - Pass.Rect.left),
                               float(Pass.Rect.bottom - Pass.Rect.top), 0, 1};
    if (!Target) viewport = {0, 0, float(width), float(height), 0, 1};
    SDL_Rect passRect = {std::max(0, Pass.Rect.left), std::max(0, Pass.Rect.top),
        std::max(0, std::min(int(width), Pass.Rect.right) - std::max(0, Pass.Rect.left)),
        std::max(0, std::min(int(height), Pass.Rect.bottom) - std::max(0, Pass.Rect.top))};
    if (!Target) passRect = {0, 0, int(width), int(height)};
    SDL_SetGPUViewport(pass, &viewport); SDL_SetGPUScissor(pass, &passRect);
    if (!fullRect && Pass.ClearFlags && ClearRect(pass, Pass, colorFormat, depthFormat, samples) != CK_OK) {
        SDL_EndGPURenderPass(pass); return Error;
    }
    SDL_GPUGraphicsPipeline *boundPipeline = nullptr;
    unsigned boundBindings = ~0u;
    CKSdlGpuUniformBindings boundUniforms;
    SDL_Rect boundScissor = passRect;
    bool scissorBound = true;
    bool stencilReferenceBound = false;
    Uint8 boundStencilReference = 0;
    CKSdlGpuVertexBindingState boundVertexBuffers[3];
    SDL_GPUBuffer *boundIndexBuffer = nullptr;
    Uint32 boundIndexOffset = 0;
    SDL_GPUIndexElementSize boundIndexSize = SDL_GPU_INDEXELEMENTSIZE_16BIT;
    bool indexBufferBound = false;
    Bindings.MarkReferenced();
    for (auto &draw : Draws) {
        auto pipeline = Pipeline(draw, colorFormat, depthFormat, samples);
        if (!pipeline) { SDL_EndGPURenderPass(pass); return Error; }
        if (boundPipeline != pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            boundPipeline = pipeline;
            boundBindings = ~0u;
            boundUniforms.Invalidate();
            CKRE_PROFILE_VALUE("CKRE.Batch.PipelineBinds", 1);
        }
        SDL_Rect scissor = passRect;
        if (draw.State.ScissorEnabled) {
            scissor.x = std::max(passRect.x, draw.State.Scissor.left);
            scissor.y = std::max(passRect.y, draw.State.Scissor.top);
            scissor.w = std::max(0, std::min(passRect.x + passRect.w, draw.State.Scissor.right) - scissor.x);
            scissor.h = std::max(0, std::min(passRect.y + passRect.h, draw.State.Scissor.bottom) - scissor.y);
        }
        if (!scissorBound || scissor.x != boundScissor.x || scissor.y != boundScissor.y ||
            scissor.w != boundScissor.w || scissor.h != boundScissor.h) {
            SDL_SetGPUScissor(pass, &scissor);
            boundScissor = scissor;
            scissorBound = true;
        }
        const Uint8 stencilReference = Uint8(draw.State.StencilRef);
        if (!stencilReferenceBound || stencilReference != boundStencilReference) {
            SDL_SetGPUStencilReference(pass, stencilReference);
            boundStencilReference = stencilReference;
            stencilReferenceBound = true;
        }
        if (!scissor.w || !scissor.h) continue;
        if (!draw.Program->Interface.VertexInputs.empty()) {
            SDL_GPUBufferBinding vb = {draw.VB ? draw.VB->Buffer.get() : batch.Buffer.get(),
                draw.VB ? draw.StartVertex * draw.Layout->Stride : draw.VertexOffset};
            CKSdlGpuBindVertexBuffer(pass, boundVertexBuffers, 0, vb);
            if (draw.Layout1) {
                SDL_GPUBufferBinding vb1 = {draw.VB1 ? draw.VB1->Buffer.get() : batch.Buffer.get(),
                    draw.VB1 ? draw.Stream1StartVertex * draw.Layout1->Stride : draw.VertexOffset1};
                CKSdlGpuBindVertexBuffer(pass, boundVertexBuffers, 1, vb1);
            }
            SDL_GPUBufferBinding defaults = {draw.Program->DefaultVertices.get(), 0};
            CKSdlGpuBindVertexBuffer(pass, boundVertexBuffers,
                                     draw.Layout1 ? 2u : 1u, defaults);
        }
        if (boundBindings != draw.Bindings) {
            const auto &bindings = Bindings[draw.Bindings];
            if (draw.Program->Vertex->Desc.SamplerCount)
                SDL_BindGPUVertexSamplers(pass, 0, bindings.Vertex.data(), draw.Program->Vertex->Desc.SamplerCount);
            if (draw.Program->Fragment->Desc.SamplerCount)
                SDL_BindGPUFragmentSamplers(pass, 0, bindings.Fragment.data(), draw.Program->Fragment->Desc.SamplerCount);
            boundBindings = draw.Bindings;
        }
        for (size_t i = 0; i < draw.Program->UniformLayout.Buffers.size(); ++i) {
            const auto &buffer = draw.Program->UniformLayout.Buffers[i];
            const unsigned offset = draw.UniformOffsets[i];
            if (!boundUniforms.NeedsPush(buffer, offset, Uniforms.Data)) continue;
            const void *data = Uniforms.Data.data() + offset;
            if (buffer.Stage == CKRST_SHADER_VERTEX) SDL_PushGPUVertexUniformData(Commands, buffer.Slot, data, buffer.Size);
            else SDL_PushGPUFragmentUniformData(Commands, buffer.Slot, data, buffer.Size);
            CKRE_PROFILE_VALUE("CKRE.Batch.UniformPushBytes", buffer.Size);
            CKRE_PROFILE_VALUE("CKRE.Batch.UniformPushes", 1);
        }
        if (draw.Marker && !draw.Marker->empty()) SDL_InsertGPUDebugLabel(Commands, draw.Marker->c_str());
        if (draw.IndexCount) {
            const Uint32 indexSizeBytes = draw.Index32 ? 4u : 2u;
            SDL_GPUBufferBinding ib = {draw.IB ? draw.IB->Buffer.get() : batch.Buffer.get(),
                draw.IB ? 0u : batch.IndexOffset};
            const Uint32 firstIndex = draw.IB ? draw.StartIndex
                                              : draw.IndexOffset / indexSizeBytes;
            const SDL_GPUIndexElementSize indexSize = draw.Index32
                ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
            if (!indexBufferBound || boundIndexBuffer != ib.buffer ||
                boundIndexOffset != ib.offset || boundIndexSize != indexSize) {
                SDL_BindGPUIndexBuffer(pass, &ib, indexSize);
                boundIndexBuffer = ib.buffer;
                boundIndexOffset = ib.offset;
                boundIndexSize = indexSize;
                indexBufferBound = true;
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.IndexCount, 1, firstIndex, 0, 0);
        } else {
            SDL_DrawGPUPrimitives(pass, draw.VertexCount, 1, 0, 0);
        }
    }
    SDL_EndGPURenderPass(pass);
    if (Target) {
        if (Target->VolumeSlice && CopyVolumeSlice(*Target->Color, Target->Desc.ColorMip,
            Target->Desc.ColorLayer, Target->VolumeSlice.get(), true) != CK_OK) return Error;
        Target->Color->Defined[Target->Desc.ColorMip * Target->Color->Info.layer_count_or_depth + Target->Desc.ColorLayer] = true;
        Target->Color->Referenced = true;
        Target->Color->AttachmentInitialized = true;
        if (Target->Color->AutoMips && Target->Desc.ColorMip == 0) {
            const CKERROR error = GenerateGpuMips(*Target->Color, Target->Desc.ColorLayer);
            if (error != CK_OK) return error;
        }
    }
    Pass.ClearFlags = 0; Draws.clear(); DrawResources.Clear(); Uniforms.Clear(); Bindings.Clear();
    BatchVertices.clear(); BatchIndices.clear();
    return CK_OK;
}

CKERROR CKSdlGpuDevice::AcquireSwapchain()
{
    if (Swapchain) return CK_OK;
    const unsigned previousWidth = SwapWidth, previousHeight = SwapHeight;
    CKRE_PROFILE_SCOPE("CKRE.SDL.Acquire");
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(Commands, Window, &Swapchain, &SwapWidth, &SwapHeight))
        return Fail("WaitAndAcquireGPUSwapchainTexture");
    if (Swapchain && (SwapWidth != previousWidth || SwapHeight != previousHeight))
        SDL_Log("SDL_gpu swapchain: window=%u drawable=%ux%u logical=%ux%u",
                SDL_GetWindowID(Window), SwapWidth, SwapHeight, Width, Height);
    return CK_OK;
}

CKERROR CKSdlGpuBackend::PresentTexture(CKDWORD handle, CKDWORD width, CKDWORD height,
                                        CKBackendPresentSync sync)
{
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    auto source = m->Textures.Get(handle);
    if (!source || source->Depth || source->Multisample ||
        source->Info.type != SDL_GPU_TEXTURETYPE_2D ||
        !width || !height || width > source->Info.width || height > source->Info.height)
        return CKERR_INVALIDPARAMETER;

    if (!CKSdlGpuValidPresentSync(sync)) return CKERR_INVALIDPARAMETER;

    // Finish the native-target scene pass, then let SDL encode its dedicated
    // blit straight to the acquired swapchain image. This keeps the portable
    // texture backbuffer/readback semantics while avoiding another translated
    // draw packet, geometry upload and backend render-pass setup.
    CKERROR error = m->Flush();
    if (error != CK_OK) return error;
    if (sync != CKRST_BACKEND_SYNC_UNCHANGED) {
        const auto mode = sync == CKRST_BACKEND_SYNC_VSYNC
            ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (mode != m->PresentMode) {
            if (m->Swapchain) return CKERR_INVALIDOPERATION;
            if (!SDL_SetGPUSwapchainParameters(m->Device, m->Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
                return m->Fail("SetGPUSwapchainParameters");
            m->PresentMode = mode;
        }
    }
    if (!m->EnsureCommands()) return m->Error;
    error = m->AcquireSwapchain();
    if (error != CK_OK) return error;

    ++m->FrameStats.Passes;
    ++m->FrameStats.Blits;
    if (m->Swapchain) {
        const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(m->Device, m->Window);
        if (m->PresentCopySupported && CKSdlGpuCanCopyPresent(source->Info.format, swapchainFormat,
                                   width, height, m->SwapWidth, m->SwapHeight)) {
            if (!m->PresentCopyLogged) {
                SDL_Log("SDL_gpu present path: exact texture copy format=%u size=%ux%u",
                        unsigned(swapchainFormat), width, height);
                m->PresentCopyLogged = true;
            }
            auto *copy = SDL_BeginGPUCopyPass(m->Commands);
            if (!copy) return m->Fail("BeginGPUCopyPass.present");
            SDL_GPUTextureLocation sourceLocation = {}, destinationLocation = {};
            sourceLocation.texture = source->Image.get();
            destinationLocation.texture = m->Swapchain;
            SDL_CopyGPUTextureToTexture(copy, &sourceLocation, &destinationLocation,
                                        width, height, 1, false);
            SDL_EndGPUCopyPass(copy);
        } else {
            SDL_GPUBlitInfo blit = {};
            blit.source = {source->Image.get(), 0, 0, 0, 0, width, height};
            blit.destination = {m->Swapchain, 0, 0, 0, 0, m->SwapWidth, m->SwapHeight};
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.flip_mode = SDL_FLIP_NONE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(m->Commands, &blit);
        }
        source->Referenced = true;
    }
    m->PassOpen = false;
    m->Target.reset();
    m->Pass = CKBackendPassDesc();
    return CK_OK;
}

CKERROR CKSdlGpuBackend::Blit(CKDWORD dstHandle, CKDWORD dstMip, CKDWORD dstLayer, CKDWORD dx, CKDWORD dy,
    CKDWORD srcHandle, CKDWORD srcMip, CKDWORD srcLayer, const CKRECT *rect)
{
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    auto src = m->Textures.Get(srcHandle), dst = m->Textures.Get(dstHandle);
    if (!src || !dst || src->Depth || dst->Depth || dst->Multisample || src == dst ||
        src->Info.format != dst->Info.format || srcMip >= src->Info.num_levels || dstMip >= dst->Info.num_levels ||
        srcLayer >= CKSdlGpuTextureLayers(*src, srcMip) || dstLayer >= CKSdlGpuTextureLayers(*dst, dstMip)) return CKERR_INVALIDPARAMETER;
    const unsigned sw = std::max(1u, src->Info.width >> srcMip), sh = std::max(1u, src->Info.height >> srcMip);
    const unsigned dw = std::max(1u, dst->Info.width >> dstMip), dh = std::max(1u, dst->Info.height >> dstMip);
    const unsigned sx = rect ? rect->left : 0, sy = rect ? rect->top : 0;
    const unsigned w = rect ? rect->right - rect->left : sw, h = rect ? rect->bottom - rect->top : sh;
    if (sx >= sw || sy >= sh || dx >= dw || dy >= dh || !w || !h || w > sw - sx || h > sh - sy || w > dw - dx || h > dh - dy)
        return CKERR_INVALIDPARAMETER;
    if (dst->AutoMips && dst->Info.type != SDL_GPU_TEXTURETYPE_3D &&
        !(dst->Info.usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) return CKERR_NOTIMPLEMENTED;
    CKERROR error = m->Flush();
    if (error != CK_OK) return error;
    error = m->PreserveTexture(*dst);
    if (error != CK_OK) return error;
    if (!m->EnsureCommands()) return m->Error;
    SDL_GPUTextureLocation source = {}, destination = {};
    source.texture = src->Image.get(); source.mip_level = srcMip; source.x = sx; source.y = sy;
    if (src->Info.type == SDL_GPU_TEXTURETYPE_3D) source.z = srcLayer; else source.layer = srcLayer;
    destination.texture = dst->Image.get(); destination.mip_level = dstMip; destination.x = dx; destination.y = dy;
    if (dst->Info.type == SDL_GPU_TEXTURETYPE_3D) destination.z = dstLayer; else destination.layer = dstLayer;
    auto *copy = SDL_BeginGPUCopyPass(m->Commands);
    if (!copy) return m->Fail("BeginGPUCopyPass.blit");
    SDL_CopyGPUTextureToTexture(copy, &source, &destination, w, h, 1, false);
    SDL_EndGPUCopyPass(copy);
    src->Referenced = true; dst->Defined[dstMip * dst->Info.layer_count_or_depth + dstLayer] = true;
    ++m->FrameStats.Blits;
    return dst->AutoMips && dstMip == 0 ? m->GenerateGpuMips(*dst, dstLayer) : CK_OK;
}

CKERROR CKSdlGpuBackend::ReadTexture(CKDWORD handle, CKDWORD mip, CKReadbackDesc *desc, CKBackendReadbackTicket *out)
{
    if (out) out->reset();
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    auto texture = m->Textures.Get(handle);
    if (!desc || !texture || texture->Depth || mip >= texture->Info.num_levels ||
        texture->Info.type != SDL_GPU_TEXTURETYPE_2D) return CKERR_INVALIDPARAMETER;
    const auto format = texture->Info.format;
    if (format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM && format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
        return CKERR_NOTIMPLEMENTED;
    desc->Width = std::max(1u, texture->Info.width >> mip); desc->Height = std::max(1u, texture->Info.height >> mip);
    desc->RowPitch = desc->Width * 4; desc->RequiredSize = desc->RowPitch * desc->Height;
    desc->Format = format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ? _32_ARGB8888 : _32_ABGR8888;
    desc->YFlip = FALSE;
    if (!out) return CK_OK;
    const CKERROR error = m->Flush();
    if (error != CK_OK) return error;
    if (!m->EnsureCommands()) return m->Error;
    auto ticket = std::make_shared<CKSdlGpuReadback>();
    ticket->Data.resize(desc->RequiredSize);
    SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, desc->RequiredSize, 0};
    ticket->Transfer = CKSdlGpuOwn(m->Device, SDL_CreateGPUTransferBuffer(m->Device, &info), SDL_ReleaseGPUTransferBuffer);
    if (!ticket->Transfer) return m->Fail("CreateGPUTransferBuffer.readback");
    SDL_GPUTextureRegion source = {};
    source.texture = texture->Image.get(); source.mip_level = mip; source.w = desc->Width; source.h = desc->Height; source.d = 1;
    SDL_GPUTextureTransferInfo destination = {ticket->Transfer.get(), 0, 0, 0};
    auto *copy = SDL_BeginGPUCopyPass(m->Commands);
    if (!copy) return m->Fail("BeginGPUCopyPass.readback");
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);
    texture->Referenced = true;
    m->Readbacks.push_back(ticket); *out = std::move(ticket);
    return CK_OK;
}

CKERROR CKSdlGpuBackend::Submit(const CKBackendSubmitDesc &desc, CKDWORD *number)
{
    CKRE_PROFILE_SCOPE("CKRE.SDL.Submit");
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!CKSdlGpuValidPresentSync(desc.Sync)) return CKERR_INVALIDPARAMETER;
    if (desc.Sync != CKRST_BACKEND_SYNC_UNCHANGED) {
        const auto mode = desc.Sync == CKRST_BACKEND_SYNC_VSYNC ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (mode != m->PresentMode) {
            if (m->Swapchain) return CKERR_INVALIDOPERATION;
            if (!SDL_SetGPUSwapchainParameters(m->Device, m->Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
                return m->Fail("SetGPUSwapchainParameters");
            m->PresentMode = mode;
        }
    }
    const CKERROR error = m->Flush(desc.PresentWindow != FALSE);
    if (error != CK_OK) return error;
    if (m->Commands) {
        auto fence = CKRE_PROFILE_CALL("CKRE.SDL.NativeSubmit",
            CKSdlGpuOwn(m->Device, SDL_SubmitGPUCommandBufferAndAcquireFence(m->Commands), SDL_ReleaseGPUFence));
        m->Commands = nullptr; m->Swapchain = nullptr;
        if (!fence) return m->Fail("SubmitGPUCommandBufferAndAcquireFence");
        for (auto &ticket : m->Readbacks) if (!ticket->Fence) ticket->Fence = fence;
        m->Submissions.push_back({std::move(fence), std::move(m->PendingGeometry)});
        m->PendingGeometry.clear();
    }
    m->PassOpen = false; m->Target.reset(); m->Pass = CKBackendPassDesc();
    m->TransientVertexInfo.clear(); m->TransientIndexInfo.clear();
    // Keep common CPU allocations warm, with a shared 16 MiB retention budget.
    // GPU uploads already own their snapshots; trimming cannot affect a fence.
    uint64_t retained = 0;
    CKSdlGpuTrimTransientAllocations(m->TransientVertices, retained);
    CKSdlGpuTrimTransientAllocations(m->TransientIndices, retained);
    m->FrameStats.Frames = ++m->Submission;
    m->Stats = m->FrameStats; m->FrameStats = CKBackendStats();
    if (number) *number = m->Submission;
    m->Collect();
    CKRE_PROFILE_VALUE("CKRE.Queue.PendingSubmissions", m->Submissions.size());
    if (m->Submissions.size() > 3) {
        CKRE_PROFILE_SCOPE("CKRE.SDL.WaitInflight");
        SDL_GPUFence *oldest = m->Submissions.front().Fence.get();
        if (!SDL_WaitForGPUFences(m->Device, true, &oldest, 1)) return m->Fail("WaitForGPUFences.inflight");
        m->Collect();
    }
    return CK_OK;
}
