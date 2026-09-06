#include "CKRenderProfile.h"
#include "CKSdlGpuInternal.h"

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
    draw.Desc = *desc; draw.State = desc->Pipeline; draw.Marker = desc->Marker ? desc->Marker : "";
    draw.Desc.Textures = nullptr; draw.Desc.Constants = nullptr; draw.Desc.Marker = nullptr;
    draw.Program = m->Programs.Get(desc->Program); draw.Layout = m->Layouts.Get(desc->Layout);
    draw.Layout1 = m->Layouts.Get(desc->Stream1Layout);
    draw.VB = m->VertexBuffers.Get(desc->VertexBuffer); draw.VB1 = m->VertexBuffers.Get(desc->Stream1VertexBuffer);
    draw.IB = m->IndexBuffers.Get(desc->IndexBuffer);
    if (!draw.Program || (desc->Stream1Layout && !draw.Layout1)) return CKERR_INVALIDPARAMETER;
    const bool procedural = draw.Program->Interface.VertexInputs.empty();
    if (procedural ? (desc->IndexCount || desc->StartVertex) : !draw.Layout) return CKERR_INVALIDPARAMETER;
    struct Bytes { const CKBYTE *Data = nullptr; size_t Size = 0; } vertices, vertices1, indices;
    auto geometry = [&](const std::shared_ptr<CKSdlGpuLayout> &layout, const std::shared_ptr<CKSdlGpuBuffer> &buffer,
                       CKDWORD layoutHandle, const CKBackendTransientVertices *transient, unsigned start, unsigned count,
                       Bytes &snapshot) {
        if (buffer) return uint64_t(start + uint64_t(count)) * layout->Stride <= buffer->Shadow.size();
        if (!transient || !transient->Token) return false;
        const auto allocation = std::lower_bound(m->TransientVertexInfo.begin(), m->TransientVertexInfo.end(),
            transient->Token, [](const CKSdlGpuTransientVertexInfo &info, CKDWORD token) { return info.Token < token; });
        if (allocation == m->TransientVertexInfo.end() || allocation->Token != transient->Token) return false;
        const size_t index = size_t(allocation - m->TransientVertexInfo.begin());
        if (index >= m->TransientVertices.size()) return false;
        const auto &storage = m->TransientVertices[index];
        if (allocation->Layout != layoutHandle || transient->Layout != allocation->Layout ||
            transient->Stride != allocation->Stride || transient->Stride != layout->Stride ||
            transient->Data != storage.data() || uint64_t(transient->Count) * transient->Stride != storage.size() ||
            start > transient->Count || count > transient->Count - start) return false;
        snapshot = {storage.data() + size_t(start) * layout->Stride, size_t(count) * layout->Stride};
        return true;
    };
    if (!procedural && !geometry(draw.Layout, draw.VB, desc->Layout, desc->TransientVertices, desc->StartVertex, desc->VertexCount, vertices)) return CKERR_INVALIDPARAMETER;
    if (!procedural && draw.Layout1 && !geometry(draw.Layout1, draw.VB1, desc->Stream1Layout, desc->Stream1Transient, desc->Stream1StartVertex,
                                  desc->VertexCount, vertices1)) return CKERR_INVALIDPARAMETER;
    if (desc->IndexCount) {
        const CKBYTE *bytes = nullptr;
        unsigned available = 0;
        if (draw.IB) {
            draw.Index32 = draw.IB->Desc.Index32 != FALSE;
            bytes = draw.IB->Shadow.data(); available = unsigned(draw.IB->Shadow.size()) / (draw.Index32 ? 4 : 2);
        } else if (desc->TransientIndices && desc->TransientIndices->Token) {
            const auto &transient = *desc->TransientIndices;
            const auto allocation = std::lower_bound(m->TransientIndexInfo.begin(), m->TransientIndexInfo.end(),
                transient.Token, [](const CKSdlGpuTransientIndexInfo &info, CKDWORD token) { return info.Token < token; });
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
        // Dispatch once by element width so each unaligned-safe read has a
        // compile-time size, instead of a variable-size memcpy per index.
        auto validIndices = [&](auto index) {
            const CKBYTE *first = bytes + size_t(desc->StartIndex) * sizeof(index);
            for (unsigned i = 0; i < desc->IndexCount; ++i) {
                std::memcpy(&index, first + size_t(i) * sizeof(index), sizeof(index));
                if (index >= desc->VertexCount) return false;
            }
            return true;
        };
        if (!(draw.Index32 ? validIndices(CKDWORD(0)) : validIndices(CKWORD(0)))) return CKERR_INVALIDPARAMETER;
        if (!draw.IB) indices = {bytes + size_t(desc->StartIndex) * size, size_t(desc->IndexCount) * size};
    }
    draw.Desc.TransientVertices = nullptr; draw.Desc.Stream1Transient = nullptr; draw.Desc.TransientIndices = nullptr;
    auto &uniforms = draw.Program->UniformLayout;
    static const CKBackendConstants emptyConstants;
    uniforms.Update(desc->Constants ? *desc->Constants : emptyConstants);
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
        draw.Textures[slot] = binding.Texture ? m->Textures.Get(binding.Texture) : draw.Program->DefaultTextures[slot];
        if (!draw.Textures[slot] || draw.Textures[slot]->Depth ||
            draw.Textures[slot]->Info.type != draw.Program->DefaultTextures[slot]->Info.type ||
            (m->Target && draw.Textures[slot] == m->Target->Color)) {
            fprintf(stderr, "SDL_gpu draw rejected: sampler=%u texture=0x%x feedback=%d marker=%s\n",
                    slot, binding.Texture, m->Target && draw.Textures[slot] == m->Target->Color, draw.Marker.c_str());
            return CKERR_INVALIDPARAMETER;
        }
        if (!cached.NativeSampler) cached.NativeSampler = m->Sampler(binding.Sampler);
        if (!cached.NativeSampler) return m->Error;
        draw.Samplers[slot] = cached.NativeSampler;
        const CKDWORD metadata = draw.Program->SamplerMetadataOffsets[slot];
        if (metadata == ~0u) continue;
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
    }
    // Snapshot directly into the batch only after validating the entire draw.
    // Offsets survive arena growth; caller-owned transient data may change as
    // soon as Draw returns. Check aligned sizes before narrowing to GPU offsets.
    auto endOffset = [](uint64_t offset, size_t size) {
        return size ? ((offset + 3) & ~uint64_t(3)) + size : offset;
    };
    if (endOffset(endOffset(m->BatchVertices.size(), vertices.Size), vertices1.Size) > UINT32_MAX ||
        endOffset(m->BatchIndices.size(), indices.Size) > UINT32_MAX ||
        uint64_t(m->UniformArena.size()) + uniforms.Data.size() > UINT32_MAX) return CKERR_OUTOFMEMORY;
    auto append = [](std::vector<CKBYTE> &dst, const Bytes &src, unsigned &offset) {
        if (!src.Size) return;
        dst.resize((dst.size() + 3) & ~size_t(3));
        offset = unsigned(dst.size());
        dst.insert(dst.end(), src.Data, src.Data + src.Size);
    };
    append(m->BatchVertices, vertices, draw.VertexOffset);
    append(m->BatchVertices, vertices1, draw.VertexOffset1);
    append(m->BatchIndices, indices, draw.IndexOffset);
    draw.UniformOffset = unsigned(m->UniformArena.size());
    m->UniformArena.insert(m->UniformArena.end(), uniforms.Data.begin(), uniforms.Data.end());
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
            Draws.clear(); UniformArena.clear(); BatchVertices.clear(); BatchIndices.clear(); Pass.ClearFlags = 0;
            return CK_OK;
        }
        if (!Swapchain) {
            const unsigned previousWidth = SwapWidth, previousHeight = SwapHeight;
            CKRE_PROFILE_SCOPE("CKRE.SDL.Acquire");
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(Commands, Window, &Swapchain, &SwapWidth, &SwapHeight))
                return Fail("WaitAndAcquireGPUSwapchainTexture");
            if (Swapchain && (SwapWidth != previousWidth || SwapHeight != previousHeight))
                SDL_Log("SDL_gpu swapchain: window=%u drawable=%ux%u logical=%ux%u",
                        SDL_GetWindowID(Window), SwapWidth, SwapHeight, Width, Height);
        }
        // A minimized window has no swapchain image. Resource work still submits.
        if (!Swapchain) {
            Draws.clear(); UniformArena.clear(); BatchVertices.clear(); BatchIndices.clear(); Pass.ClearFlags = 0;
            return CK_OK;
        }
    }
    CKRE_PROFILE_VALUE("CKRE.Batch.Draws", Draws.size());
    CKRE_PROFILE_VALUE("CKRE.Batch.VertexBytes", BatchVertices.size());
    CKRE_PROFILE_VALUE("CKRE.Batch.IndexBytes", BatchIndices.size());
    CKRE_PROFILE_VALUE("CKRE.Batch.UniformSnapshotBytes", UniformArena.size());
    auto batchVB = UploadGeometry(BatchVertices, SDL_GPU_BUFFERUSAGE_VERTEX);
    auto batchIB = UploadGeometry(BatchIndices, SDL_GPU_BUFFERUSAGE_INDEX);
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
    for (auto &draw : Draws) {
        auto pipeline = Pipeline(draw, colorFormat, depthFormat, samples);
        if (!pipeline) { SDL_EndGPURenderPass(pass); return Error; }
        SDL_BindGPUGraphicsPipeline(pass, pipeline.get());
        SDL_SetGPUViewport(pass, &viewport);
        SDL_Rect scissor = passRect;
        if (draw.State.ScissorEnabled) {
            scissor.x = std::max(passRect.x, draw.State.Scissor.left);
            scissor.y = std::max(passRect.y, draw.State.Scissor.top);
            scissor.w = std::max(0, std::min(passRect.x + passRect.w, draw.State.Scissor.right) - scissor.x);
            scissor.h = std::max(0, std::min(passRect.y + passRect.h, draw.State.Scissor.bottom) - scissor.y);
        }
        SDL_SetGPUScissor(pass, &scissor); SDL_SetGPUStencilReference(pass, Uint8(draw.State.StencilRef));
        if (!scissor.w || !scissor.h) continue;
        if (!draw.Program->Interface.VertexInputs.empty()) {
            SDL_GPUBufferBinding vb = {draw.VB ? draw.VB->Buffer.get() : batchVB.get(),
                draw.VB ? draw.Desc.StartVertex * draw.Layout->Stride : draw.VertexOffset};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            if (draw.Layout1) {
                SDL_GPUBufferBinding vb1 = {draw.VB1 ? draw.VB1->Buffer.get() : batchVB.get(),
                    draw.VB1 ? draw.Desc.Stream1StartVertex * draw.Layout1->Stride : draw.VertexOffset1};
                SDL_BindGPUVertexBuffers(pass, 1, &vb1, 1);
            }
            SDL_GPUBufferBinding defaults = {draw.Program->DefaultVertices.get(), 0};
            SDL_BindGPUVertexBuffers(pass, draw.Layout1 ? 2 : 1, &defaults, 1);
        }
        SDL_GPUTextureSamplerBinding vertexBindings[16] = {}, fragmentBindings[16] = {};
        for (unsigned slot = 0; slot < draw.Program->Interface.Samplers.size(); ++slot) {
            const auto &decl = draw.Program->Interface.Samplers[slot];
            auto *bindings = decl.Stage == CKRST_SHADER_VERTEX ? vertexBindings : fragmentBindings;
            bindings[decl.NativeSlot] = {draw.Textures[slot]->Image.get(), draw.Samplers[slot].get()};
            draw.Textures[slot]->Referenced = true;
        }
        if (draw.Program->Vertex->Desc.SamplerCount)
            SDL_BindGPUVertexSamplers(pass, 0, vertexBindings, draw.Program->Vertex->Desc.SamplerCount);
        if (draw.Program->Fragment->Desc.SamplerCount)
            SDL_BindGPUFragmentSamplers(pass, 0, fragmentBindings, draw.Program->Fragment->Desc.SamplerCount);
        for (const auto &buffer : draw.Program->UniformLayout.Buffers) {
            const void *data = UniformArena.data() + draw.UniformOffset + buffer.Offset;
            if (buffer.Stage == CKRST_SHADER_VERTEX) SDL_PushGPUVertexUniformData(Commands, buffer.Slot, data, buffer.Size);
            else SDL_PushGPUFragmentUniformData(Commands, buffer.Slot, data, buffer.Size);
        }
        if (!draw.Marker.empty()) SDL_InsertGPUDebugLabel(Commands, draw.Marker.c_str());
        if (draw.Desc.IndexCount) {
            SDL_GPUBufferBinding ib = {draw.IB ? draw.IB->Buffer.get() : batchIB.get(),
                draw.IB ? draw.Desc.StartIndex * (draw.Index32 ? 4u : 2u) : draw.IndexOffset};
            SDL_BindGPUIndexBuffer(pass, &ib, draw.Index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, draw.Desc.IndexCount, 1, 0, 0, 0);
        } else SDL_DrawGPUPrimitives(pass, draw.Desc.VertexCount, 1, 0, 0);
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
    Pass.ClearFlags = 0; Draws.clear(); UniformArena.clear(); BatchVertices.clear(); BatchIndices.clear();
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
    auto trim = [&retained](std::vector<std::vector<CKBYTE>> &allocations) {
        size_t keep = 0;
        while (keep < allocations.size() && retained + allocations[keep].capacity() <= 16u * 1024u * 1024u) {
            retained += allocations[keep].capacity();
            ++keep;
        }
        allocations.resize(keep);
    };
    trim(m->TransientVertices); trim(m->TransientIndices);
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
